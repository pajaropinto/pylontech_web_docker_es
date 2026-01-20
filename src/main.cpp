#include "json.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <vector>


// === SERVIDOR WEB (Mongoose v7+) ===
#define MG_ENABLE_SOCKET 1
#define MG_ENABLE_DIRLIST 0
#define MG_ENABLE_HTTP_WEBSOCKET 0
#define MG_ENABLE_MQTT 0
#define MG_ENABLE_CUSTOM_IO 0
#include "mongoose.h"

using json = nlohmann::json;
using namespace std;
namespace fs = std::filesystem;

struct BatteryData {
  int id = 0;
  vector<json> cells;
  int cycle_count = 0;
};
vector<BatteryData> g_batteries;

struct AppConfig {
  int num_batteries = 15;
  int delay_between_batteries = 2000;
  int delay_between_cycle_battery = 5;
  string battery_tcp_ip = "10.0.0.234";
  int battery_tcp_port = 10034;
  string mqtt_broker_ip = "10.0.0.250";
  int mqtt_broker_port = 1883;
  string mqtt_user = "fernan";
  string mqtt_password = "Nabucodonos0_";
};
AppConfig g_config;

void log_message(const string &msg) { cout << msg << endl; }

void load_config() {
  try {
    if (!fs::exists("config/app_config.json")) {
      log_message(
          "⚠️ config/app_config.json no existe. Creando predeterminado.");
      json default_cfg = {{"num_batteries", 15},
                          {"delay_between_batteries", 2000},
                          {"delay_between_cycle_battery", 5},
                          {"battery_tcp_ip", "10.0.0.234"},
                          {"battery_tcp_port", 10034},
                          {"mqtt_broker_ip", "10.0.0.250"},
                          {"mqtt_broker_port", 1883},
                          {"mqtt_user", "fernan"},
                          {"mqtt_password", "Nabucodonos0_"}};
      fs::create_directories("config");
      ofstream f("config/app_config.json");
      f << default_cfg.dump(4) << endl;
    }

    ifstream f("config/app_config.json");
    json j;
    f >> j;

    g_config.num_batteries = j.value("num_batteries", 15);
    g_config.delay_between_batteries = j.value("delay_between_batteries", 2000);
    g_config.delay_between_cycle_battery =
        j.value("delay_between_cycle_battery", 5);
    g_config.battery_tcp_ip = j.value("battery_tcp_ip", "10.0.0.234");
    g_config.battery_tcp_port = j.value("battery_tcp_port", 10034);
    g_config.mqtt_broker_ip = j.value("mqtt_broker_ip", "10.0.0.250");
    g_config.mqtt_broker_port = j.value("mqtt_broker_port", 1883);
    g_config.mqtt_user = j.value("mqtt_user", "fernan");
    g_config.mqtt_password = j.value("mqtt_password", "Nabucodonos0_");

    log_message("✅ Configuración cargada:");
    log_message("   num_batteries: " + to_string(g_config.num_batteries));
    log_message("   delay_between_batteries: " +
                to_string(g_config.delay_between_batteries) + " ms");
    log_message("   delay_between_cycle_battery: " +
                to_string(g_config.delay_between_cycle_battery) + " min");
  } catch (const exception &e) {
    log_message("❌ Error al cargar config: " + string(e.what()));
  }
}

// === MANEJADOR DE EVENTOS HTTP ===
static void fn(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;

    // Obtener URI
    string uri(hm->uri.buf, hm->uri.len);

    // API: GET /api/config
    if (uri == "/api/config") {
      if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
        string config_path = "config/app_config.json";
        ifstream f(config_path);
        if (!f.is_open()) {
          mg_http_reply(c, 500, "", "{\"error\":\"Config file not found\"}");
          return;
        }
        json j;
        try {
          f >> j;
          string response = j.dump();
          mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s",
                        response.c_str());
        } catch (...) {
          mg_http_reply(c, 500, "", "{\"error\":\"Invalid config\"}");
        }
        return;
      }
      // API: POST /api/config
      else if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
        string body(hm->body.buf, hm->body.len);
        try {
          json j = json::parse(body);
          if (!j.contains("num_batteries") || !j.contains("battery_tcp_ip")) {
            mg_http_reply(c, 400, "",
                          "{\"error\":\"Missing required fields\"}");
            return;
          }
          ofstream f("config/app_config.json");
          f << j.dump(4) << endl;
          mg_http_reply(c, 200, "", "{\"status\":\"OK\"}");
        } catch (...) {
          mg_http_reply(c, 400, "", "{\"error\":\"Invalid JSON\"}");
        }
        return;
      }
    }

    // Servir archivos estáticos
    string filepath = "www" + uri;
    if (uri == "/")
      filepath = "www/index.html";

    // Prevenir directory traversal
    if (filepath.find("..") != string::npos) {
      mg_http_reply(c, 403, "", "{\"error\":\"Forbidden\"}");
      return;
    }

    // Verificar existencia
    if (!fs::exists(filepath)) {
      mg_http_reply(c, 404, "", "{\"error\":\"Not found\"}");
      return;
    }

    // Servir archivo
    mg_http_serve_opts opts = {0};
    mg_http_serve_file(c, hm, filepath.c_str(), &opts);
  }
}

string send_battery_command(const string &cmd) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
    return "";

  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(g_config.battery_tcp_port);
  if (inet_pton(AF_INET, g_config.battery_tcp_ip.c_str(),
                &serv_addr.sin_addr) <= 0) {
    close(sockfd);
    return "";
  }

  int flags = fcntl(sockfd, F_GETFL, 0);
  fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
  int res = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
  if (res < 0) {
    if (errno == EINPROGRESS) {
      fd_set write_fds;
      FD_ZERO(&write_fds);
      FD_SET(sockfd, &write_fds);
      struct timeval tv;
      tv.tv_sec = 5;
      tv.tv_usec = 0;
      res = select(sockfd + 1, nullptr, &write_fds, nullptr, &tv);
      if (res <= 0) {
        close(sockfd);
        return "";
      }
      int so_error;
      socklen_t len = sizeof(so_error);
      getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);
      if (so_error != 0) {
        close(sockfd);
        return "";
      }
    } else {
      close(sockfd);
      return "";
    }
  }
  fcntl(sockfd, F_SETFL, flags);

  string full_cmd = cmd + "\r";
  if (send(sockfd, full_cmd.c_str(), full_cmd.length(), 0) <= 0) {
    close(sockfd);
    return "";
  }

  string response;
  char buffer[1024];
  fd_set read_fds;
  struct timeval timeout;

  while (true) {
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    int activity = select(sockfd + 1, &read_fds, nullptr, nullptr, &timeout);
    if (activity <= 0)
      break;

    ssize_t bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0)
      break;

    buffer[bytes] = '\0';
    response += string(buffer, bytes);
  }

  close(sockfd);

  while (!response.empty() &&
         (response.back() == '\n' || response.back() == '\r')) {
    response.pop_back();
  }

  return response;
}

bool is_valid_cycle_count(int cycles) {
  if (cycles <= 0)
    return false;
  if (cycles > 100000)
    return false;
  return true;
}

int parse_stat_response(const string &resp) {
  size_t pos = resp.find("CYCLE Times");
  if (pos != string::npos) {
    size_t colon = resp.find(':', pos);
    if (colon != string::npos) {
      string num_part = resp.substr(colon + 1);
      size_t start = num_part.find_first_not_of(" \t");
      if (start != string::npos) {
        num_part = num_part.substr(start);
        string clean_num;
        for (char c : num_part) {
          if (isdigit(c)) {
            clean_num += c;
          } else {
            break;
          }
        }
        if (!clean_num.empty()) {
          try {
            int value = stoi(clean_num);
            if (is_valid_cycle_count(value)) {
              return value;
            }
          } catch (...) {
            // stoi falló
          }
        }
      }
    }
  }
  return -1;
}

json parse_bat_table_to_flat_json(const string &resp) {
  json result;
  istringstream iss(resp);
  string line;

  while (getline(iss, line)) {
    if (line.empty())
      continue;
    if (!isdigit(line[0]))
      continue;

    string cleaned;
    bool in_space = false;
    for (char c : line) {
      if (c == ' ') {
        if (!in_space) {
          cleaned += ' ';
          in_space = true;
        }
      } else {
        cleaned += c;
        in_space = false;
      }
    }

    vector<string> fields;
    istringstream field_stream(cleaned);
    string field;
    while (field_stream >> field) {
      fields.push_back(field);
    }

    if (fields.size() < 11)
      continue;

    try {
      int cell_num = stoi(fields[0]);
      if (cell_num < 0 || cell_num > 14)
        continue;

      string suffix = "_" + to_string(cell_num);

      double voltage = stod(fields[1]) / 1000.0;
      double current = stod(fields[2]) / 1000.0;
      double temperature = stod(fields[3]) / 1000.0;
      string state = fields[4];
      string state_voltage = fields[5];
      string state_current = fields[6];
      string state_temperature = fields[7];

      string soc_str = fields[8];
      if (!soc_str.empty() && soc_str.back() == '%') {
        soc_str.pop_back();
      }
      double soc = stod(soc_str);

      long coulomb_mah = 0;
      if (!fields[9].empty() && isdigit(fields[9][0])) {
        coulomb_mah = stol(fields[9]);
      }
      bool balance = (fields[10] == "Y");

      result["numero_celda" + suffix] = cell_num;
      result["voltage" + suffix] = voltage;
      result["corriente" + suffix] = current;
      result["temperatura" + suffix] = temperature;
      result["estado" + suffix] = state;
      result["estado_voltage" + suffix] = state_voltage;
      result["estado_corriente" + suffix] = state_current;
      result["estado_temperatura" + suffix] = state_temperature;
      result["soc" + suffix] = soc;
      result["coulomb" + suffix] = coulomb_mah / 1000.0;
      result["balance" + suffix] = balance;

    } catch (...) {
      continue;
    }
  }

  return result;
}

vector<json> flat_json_to_cells(const json &flat) {
  vector<json> cells;
  for (int i = 0; i <= 14; ++i) {
    string prefix = "_" + to_string(i);
    if (flat.contains("numero_celda" + prefix)) {
      json cell;
      cell["numero_celda"] = flat["numero_celda" + prefix];
      cell["voltage"] = flat["voltage" + prefix];
      cell["corriente"] = flat["corriente" + prefix];
      cell["temperatura"] = flat["temperatura" + prefix];
      cell["estado"] = flat["estado" + prefix];
      cell["estado_voltage"] = flat["estado_voltage" + prefix];
      cell["estado_corriente"] = flat["estado_corriente" + prefix];
      cell["estado_temperatura"] = flat["estado_temperatura" + prefix];
      cell["soc"] = flat["soc" + prefix];
      cell["coulomb"] = flat["coulomb" + prefix];
      cell["balance"] = flat["balance" + prefix];
      cells.push_back(cell);
    }
  }
  return cells;
}

void publish_raw_battery(int battery_index) {
  if (battery_index >= (int)g_batteries.size())
    return;
  const auto &bat = g_batteries[battery_index];
  if (bat.cells.empty())
    return;

  json celdas;
  for (const auto &cell : bat.cells) {
    int num = cell["numero_celda"].get<int>();
    celdas[to_string(num)] = {
        {"voltage", cell["voltage"]},
        {"corriente", cell["corriente"]},
        {"temperatura", cell["temperatura"]},
        {"estado", cell["estado"]},
        {"estado_voltage", cell["estado_voltage"]},
        {"estado_corriente", cell["estado_corriente"]},
        {"estado_temperatura", cell["estado_temperatura"]},
        {"soc", cell["soc"]},
        {"coulomb", cell["coulomb"]},
        {"balance", cell["balance"]}};
  }

  string bat_id = (bat.id < 10 ? "0" : "") + to_string(bat.id);
  string topic = "homeassistant/pylon/bat" + bat_id;

  string payload = celdas.dump();
  size_t pos;
  while ((pos = payload.find("'")) != string::npos) {
    payload.replace(pos, 1, "'\"'\"'");
  }
  string cmd = "mosquitto_pub -h " + g_config.mqtt_broker_ip + " -p " +
               to_string(g_config.mqtt_broker_port) + " -u " +
               g_config.mqtt_user + " -P " + g_config.mqtt_password + " -t \"" +
               topic + "\" -m '" + payload + "'";
  int result = system(cmd.c_str());
  if (result == 0) {
    log_message("✅ MQTT: " + topic);
  } else {
    log_message("❌ MQTT error: " + topic);
  }
}

void publish_total_battery() {
  json total_msg;

  for (int i = 0; i < (int)g_batteries.size(); ++i) {
    const auto &bat = g_batteries[i];
    if (bat.cells.empty())
      continue;

    double v_sum = 0, i_sum = 0, t_sum = 0, s_sum = 0;
    long coulomb_mah_total = 0;
    int alarm_v = 0, alarm_i = 0, alarm_t = 0;
    int n = bat.cells.size();

    for (const auto &cell : bat.cells) {
      v_sum += cell["voltage"].get<double>();
      i_sum += cell["corriente"].get<double>();
      t_sum += cell["temperatura"].get<double>();
      s_sum += cell["soc"].get<double>();
      coulomb_mah_total +=
          static_cast<long>(cell["coulomb"].get<double>() * 1000);

      if (cell["estado_voltage"].get<string>() != "Normal")
        alarm_v = 1;
      if (cell["estado_corriente"].get<string>() != "Normal")
        alarm_i = 1;
      if (cell["estado_temperatura"].get<string>() != "Normal")
        alarm_t = 1;
    }

    double avg_i = i_sum / n;
    string status = "Inactiva";
    if (avg_i > 0.1) {
      status = "Cargando";
    } else if (avg_i < -0.1) {
      status = "Descargando";
    }

    double voltage_total = round(v_sum * 100) / 100;
    double current_avg = round((i_sum / n) * 100) / 100;
    double temp_avg = round((t_sum / n) * 100) / 100;
    double soc_avg = round((s_sum / n) * 100) / 100;
    double coulomb_ah = coulomb_mah_total / 1000.0;
    double coulomb_rounded = round(coulomb_ah * 100) / 100;

    string key = "battery_" + to_string(bat.id);
    total_msg[key] = {{"no_battery", bat.id},
                      {"voltage", voltage_total},
                      {"corriente", current_avg},
                      {"temperatura", temp_avg},
                      {"soc", soc_avg},
                      {"estado", status},
                      {"alarma_voltage", alarm_v},
                      {"alarma_corriente", alarm_i},
                      {"alarma_temperatura", alarm_t},
                      {"coulomb", coulomb_rounded},
                      {"cycle", bat.cycle_count}};
  }

  string topic = "homeassistant/pylon/total_battery";
  string payload = total_msg.dump();
  size_t pos;
  while ((pos = payload.find("'")) != string::npos) {
    payload.replace(pos, 1, "'\"'\"'");
  }
  string cmd = "mosquitto_pub -h " + g_config.mqtt_broker_ip + " -p " +
               to_string(g_config.mqtt_broker_port) + " -u " +
               g_config.mqtt_user + " -P " + g_config.mqtt_password + " -t \"" +
               topic + "\" -m '" + payload + "'";
  int result = system(cmd.c_str());
  if (result == 0) {
    log_message("✅ MQTT TOTAL: " + topic);
  } else {
    log_message("❌ MQTT TOTAL error: " + topic);
  }
}

void publish_total_system() {
  double sys_voltage_sum = 0;
  double sys_current_sum = 0;
  double sys_temp_sum = 0;
  double sys_soc_sum = 0;
  long sys_coulomb_mah_total = 0;
  int sys_alarm_v = 0, sys_alarm_i = 0, sys_alarm_t = 0;
  int sys_cycle_max = 0;
  int valid_batteries = 0;

  for (int i = 0; i < (int)g_batteries.size(); ++i) {
    const auto &bat = g_batteries[i];
    if (bat.cells.empty())
      continue;

    double v_sum = 0, i_sum = 0, t_sum = 0, s_sum = 0;
    long coulomb_mah = 0;
    int alarm_v = 0, alarm_i = 0, alarm_t = 0;
    int n = bat.cells.size();

    for (const auto &cell : bat.cells) {
      v_sum += cell["voltage"].get<double>();
      i_sum += cell["corriente"].get<double>();
      t_sum += cell["temperatura"].get<double>();
      s_sum += cell["soc"].get<double>();
      coulomb_mah += static_cast<long>(cell["coulomb"].get<double>() * 1000);

      if (cell["estado_voltage"].get<string>() != "Normal")
        alarm_v = 1;
      if (cell["estado_corriente"].get<string>() != "Normal")
        alarm_i = 1;
      if (cell["estado_temperatura"].get<string>() != "Normal")
        alarm_t = 1;
    }

    double bat_voltage = v_sum;
    double bat_current = i_sum / n;
    double bat_temp = t_sum / n;
    double bat_soc = s_sum / n;

    sys_voltage_sum += bat_voltage;
    sys_current_sum += bat_current;
    sys_temp_sum += bat_temp;
    sys_soc_sum += bat_soc;
    sys_coulomb_mah_total += coulomb_mah;

    if (alarm_v)
      sys_alarm_v = 1;
    if (alarm_i)
      sys_alarm_i = 1;
    if (alarm_t)
      sys_alarm_t = 1;

    if (bat.cycle_count > sys_cycle_max) {
      sys_cycle_max = bat.cycle_count;
    }

    valid_batteries++;
  }

  if (valid_batteries == 0)
    return;

  double sys_voltage_avg =
      round((sys_voltage_sum / valid_batteries) * 100) / 100;
  double sys_current_total = round(sys_current_sum * 100) / 100;
  double sys_temp_avg = round((sys_temp_sum / valid_batteries) * 100) / 100;
  double sys_soc_avg = round((sys_soc_sum / valid_batteries) * 100) / 100;
  double sys_coulomb_ah = sys_coulomb_mah_total / 1000.0;
  double sys_coulomb_rounded = round(sys_coulomb_ah * 100) / 100;

  string sys_status = "Inactiva";
  if (sys_current_total > 0.5) {
    sys_status = "Cargando";
  } else if (sys_current_total < -0.5) {
    sys_status = "Descargando";
  }

  json total_system = {{"voltage", sys_voltage_avg},
                       {"corriente", sys_current_total},
                       {"temperatura", sys_temp_avg},
                       {"soc", sys_soc_avg},
                       {"estado", sys_status},
                       {"alarma_voltage", sys_alarm_v},
                       {"alarma_corriente", sys_alarm_i},
                       {"alarma_temperatura", sys_alarm_t},
                       {"coulomb", sys_coulomb_rounded},
                       {"cycle", sys_cycle_max}};

  string topic = "homeassistant/pylon/total_system";
  string payload = total_system.dump();
  size_t pos;
  while ((pos = payload.find("'")) != string::npos) {
    payload.replace(pos, 1, "'\"'\"'");
  }
  string cmd = "mosquitto_pub -h " + g_config.mqtt_broker_ip + " -p " +
               to_string(g_config.mqtt_broker_port) + " -u " +
               g_config.mqtt_user + " -P " + g_config.mqtt_password + " -t \"" +
               topic + "\" -m '" + payload + "'";
  int result = system(cmd.c_str());
  if (result == 0) {
    log_message("✅ MQTT SYSTEM: " + topic);
  } else {
    log_message("❌ MQTT SYSTEM error: " + topic);
  }
}

int main() {
  log_message("🔋 Iniciando monitor de baterías Pylontech...");

  // === INICIAR SERVIDOR WEB ===
  struct mg_mgr mgr;
  mg_mgr_init(&mgr);
  mg_http_listen(&mgr, "http://0.0.0.0:61616", fn, NULL);
  thread web_thread([&mgr]() {
    log_message("🌐 Servidor web iniciado en http://0.0.0.0:61616");
    while (true) {
      mg_mgr_poll(&mgr, 1000);
    }
  });
  web_thread.detach();

  load_config();
  g_batteries.resize(g_config.num_batteries);
  for (int i = 0; i < g_config.num_batteries; ++i) {
    g_batteries[i].id = i + 1;
    g_batteries[i].cycle_count = 0;
  }

  while (true) {
    load_config();
    if ((int)g_batteries.size() != g_config.num_batteries) {
      g_batteries.resize(g_config.num_batteries);
      for (int i = 0; i < g_config.num_batteries; ++i) {
        g_batteries[i].id = i + 1;
        g_batteries[i].cycle_count = 0;
      }
    }

    log_message("📊 Leyendo datos STAT (con validación robusta)...");
    bool all_valid = false;
    int stat_retries = 0;
    const int MAX_STAT_RETRIES = 5;

    while (!all_valid && stat_retries < MAX_STAT_RETRIES) {
      all_valid = true;
      vector<int> temp_cycles(g_config.num_batteries, 0);

      for (int i = 1; i <= g_config.num_batteries; ++i) {
        string cmd_stat = "stat " + to_string(i);
        log_message("📡 Enviando: " + cmd_stat);
        string resp_stat = send_battery_command(cmd_stat);

        int cycles = parse_stat_response(resp_stat);
        if (cycles == -1) {
          log_message("⚠️ Batería " + to_string(i) +
                      ": ciclo inválido. Reintentando...");
          all_valid = false;
          break;
        }

        temp_cycles[i - 1] = cycles;
        log_message("   Batería " + to_string(i) +
                    ": ciclos = " + to_string(cycles));

        this_thread::sleep_for(
            chrono::milliseconds(g_config.delay_between_batteries));
      }

      if (all_valid) {
        for (int i = 0; i < g_config.num_batteries; ++i) {
          g_batteries[i].cycle_count = temp_cycles[i];
        }
        log_message("✅ Todos los ciclos son válidos.");
      } else {
        stat_retries++;
        log_message("🔄 Reintento " + to_string(stat_retries) + "/" +
                    to_string(MAX_STAT_RETRIES) + " de STAT...");
        this_thread::sleep_for(chrono::seconds(2));
      }
    }

    if (!all_valid) {
      log_message(
          "❌ Máximo de reintentos alcanzado. Usando últimos valores válidos.");
    }

    int total_ms = g_config.delay_between_cycle_battery * 60 * 1000;
    int time_per_cycle =
        g_config.num_batteries * g_config.delay_between_batteries;
    int repetitions = max(1, total_ms / time_per_cycle);

    log_message("🔄 Ejecutando " + to_string(repetitions) +
                " ciclos de BAT...");

    for (int rep = 0; rep < repetitions; ++rep) {
      for (int i = 1; i <= g_config.num_batteries; ++i) {
        string cmd_bat = "bat " + to_string(i);
        log_message("📡 Enviando: " + cmd_bat);
        string resp_bat = send_battery_command(cmd_bat);

        json bat_flat = parse_bat_table_to_flat_json(resp_bat);
        if (i - 1 < (int)g_batteries.size()) {
          g_batteries[i - 1].cells = flat_json_to_cells(bat_flat);
        }

        publish_raw_battery(i - 1);

        this_thread::sleep_for(
            chrono::milliseconds(g_config.delay_between_batteries));
      }

      publish_total_battery();
      publish_total_system();
    }

    log_message("🔚 Ciclo completo finalizado. Reiniciando...");
  }

  return 0;
}
