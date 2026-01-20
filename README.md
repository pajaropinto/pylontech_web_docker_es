# Monitor Pylontech para Home Assistant

![Pylontech Battery Monitoring](https://img.shields.io/badge/Version-2.1-blue) ![Docker](https://img.shields.io/badge/Docker-Compatible-green)

**Monitor Pylontech** es una aplicación completa y robusta diseñada para monitorear sistemas de baterías Pylontech a través de un LV-Hub, recopilando datos en tiempo real y publicándolos en un broker MQTT para su integración con Home Assistant y otras plataformas de automatización.

## 📋 Introducción

Esta aplicación se conecta directamente o bien al puerto "Console" de la bateria principal o a un **LV-Hub de Pylontech** en el caso de que este instalado mediante un interface Serial/TCP/IP.
De esta manera se recuperan los datos detallados de cada celda de batería, procesa información de totales del sistema y por batería individual, y publica todos los datos en formato JSON a través de MQTT. 

Incluye una **interfaz web integrada** que permite:
- Visualizar datos en tiempo real de totales del sistema y baterías individuales
- Configurar parámetros del sistema (IP del interface, broker MQTT, número de baterías, etc.)
- Acceder a datos detallados por celda de cada batería

La aplicación está diseñada para ejecutarse de forma continua en un contenedor Docker, asegurando alta disponibilidad y fácil despliegue.

## 🏗️ Arquitectura y Módulos

### 🧠 Módulo Principal (C++)

El núcleo de la aplicación está escrito en C++ y realiza las siguientes funciones:

#### **Conexión y Comunicación con LV-Hub**
- Se conecta a la bateria o al LV-Hub mediante TCP/IP
- Envía comandos `stat` para obtener ciclos de vida de baterías
- Envía comandos `bat` para obtener datos detallados de celdas
- Implementa reintentos automáticos y validación robusta de datos
- Maneja tiempos configurables entre lecturas

#### **Procesamiento de Datos**
- **Totales por batería**: Calcula voltaje total (suma de celdas), corriente promedio, temperatura promedio, SOC promedio, estado de carga/descarga, alarmas y capacidad restante
- **Totales del sistema**: Agrega datos de todas las baterías (voltaje promedio, corriente total, temperatura promedio, SOC promedio, etc.)
- **Validación de datos**: Filtra valores inválidos, NaN, y datos fuera de rango
- **Recarga automática**: Lee el archivo de configuración en cada ciclo para aplicar cambios en tiempo real

#### **Publicación MQTT**
- Publica datos crudos por batería en topics: `homeassistant/pylon/bat01`, `bat02`, ..., `bat15`
- Publica totales por batería en: `homeassistant/pylon/total_battery`
- Publica totales del sistema en: `homeassistant/pylon/total_system`
- Soporta autenticación MQTT y conexión persistente

#### **Servidor Web Integrado**
- Sirve archivos estáticos (HTML, CSS, JS)
- Proporciona API REST para gestión de configuración (`GET /api/config`, `POST /api/config`)
- Incluye soporte para WebSocket MQTT para actualización en tiempo real

### 🌐 Interfaz Web

La interfaz web incluye tres pestañas principales:

#### **Pestaña 1: Totales**
- Muestra tabla con totales del sistema (voltaje, corriente, temperatura, SOC, estado, coulomb, ciclos)
- Muestra tabla resumen de todas las baterías
- Indicador visual de estado de conexión MQTT (discreto y en la parte superior)

#### **Pestaña 2: Baterías**
- Muestra una tabla detallada por cada batería
- Cada tabla incluye datos de todas las celdas (voltaje, corriente, temperatura, SOC, estado, coulomb, balance)
- Actualización automática en tiempo real desde MQTT

#### **Pestaña 3: Configuración**
- Formulario centrado con todos los parámetros del sistema
- Etiquetas alineadas a la izquierda, campos alineados a la derecha
- Botones para cargar y guardar configuración
- Validación de campos numéricos

### 📁 Estructura de Archivos

```
.
├── Dockerfile                 # Definición del contenedor Docker
├── src/
│   └── main.cpp              # Código fuente principal en C++
├── www/
│   ├── index.html            # Página principal con las tres pestañas
│   ├── script.js             # Lógica JavaScript (cambio de pestañas, MQTT, configuración)
│   └── style.css             # Estilos CSS
└── config/
    └── app_config.json       # Archivo de configuración (se crea automáticamente si no existe)
```

## ⚙️ Configuración

El archivo `app_config.json` contiene los siguientes parámetros:

```json
{
    "num_batteries": 15,
    "delay_between_batteries": 1000,
    "delay_between_cycle_battery": 1,
    "battery_tcp_ip": "10.0.0.234",
    "battery_tcp_port": 10034,
    "mqtt_broker_ip": "10.0.0.250",
    "mqtt_broker_port": 1883,
    "mqtt_broker_ws_port": 9001,
    "mqtt_user": "fernan",
    "mqtt_password": "Nabucodonos0_"
}
```

- **`num_batteries`**: Número de baterías a monitorear (1-16)
- **`delay_between_batteries`**: Retardo entre lecturas de baterías consecutivas (ms)
- **`delay_between_cycle_battery`**: Duración del ciclo completo de lectura BAT (minutos)
- **`battery_tcp_ip/port`**: Dirección y puerto del LV-Hub
- **`mqtt_broker_*`**: Configuración del broker MQTT (incluyendo puerto WebSocket para la interfaz web)

## 🐳 Despliegue con Docker

### Descarga desde Docker Hub

La imagen está disponible públicamente en Docker Hub:

```bash
docker pull pajaropinto/pylontech_monitor_es:latest
```

### Ejecución

```bash
docker run -d \
  --name pylontech_monitor \
  --network host \
  -v $(pwd)/config:/app/config \
  -v $(pwd)/www:/app/www \
  pajaropinto/pylontech_monitor_es:2.1
```

### Docker compose
```docker-compose.yml
   services:
    axpert-monitor:
        image: pajaropinto/pylontech_monitor_es:latest
        container_name: pylontech-monitor
        volumes:
            - /main/storage/docker/pylontech_monitor/config:/config
            - /main/storage/docker/pylontech_monitor/log:/log
        restart: always
        network_mode: host
        ports:
            - "61616:61616"
...


### Parámetros de ejecución

- **`--network host`**: Permite acceso directo al puerto 61616 para la interfaz web
- **`-v config:/app/config`**: Monta el directorio de configuración para persistencia
- **`-v www:/app/www`**: Monta los archivos web (opcional, ya incluidos en la imagen)

## 🌐 Acceso a la Interfaz Web

Una vez en ejecución, accede a la interfaz web en:

```
http://[IP_DEL_SERVIDOR]:61616
```

## 📊 Topics MQTT Publicados

| Topic | Contenido |
|-------|-----------|
| `homeassistant/pylon/bat01` - `bat15` | Datos crudos por celda de cada batería |
| `homeassistant/pylon/total_battery` | Totales agregados por batería |
| `homeassistant/pylon/total_system` | Totales del sistema completo |

## 🔒 Características de Robustez

- **Validación de datos**: Rechaza valores inválidos y reintenta lecturas
- **Recarga automática**: Aplica cambios de configuración sin reiniciar
- **Conexión persistente**: Reconecta automáticamente al LV-Hub y broker MQTT
- **Sin bloqueos**: Interfaz web responsive y sin congelamientos
- **Compilación estática**: Imagen Docker ligera y sin dependencias externas

## 📝 Notas Adicionales

- **Requisitos de la conexion con las baterias o el LV-Hub**: Debe de utilizarse un interface Serial/TCP y configurarse de acuerdo a los parametros de la aplicación
- **Broker MQTT**: Debe tener WebSocket habilitado en el puerto 9001 para la interfaz web
- **Home Assistant**: Los datos se integran perfectamente con sensores MQTT de Home Assistant
- **Actualizaciones**: La configuración se puede modificar en tiempo real desde la interfaz web

## 📄 Licencia

Este proyecto es de código abierto y está disponible bajo la licencia MIT.

---

**Desarrollado para la comunidad de Home Assistant y energía solar**  
**Versión 2.2 - Disponible en [Docker Hub](https://hub.docker.com/r/pajaropinto/pylontech_monitor_es)**
