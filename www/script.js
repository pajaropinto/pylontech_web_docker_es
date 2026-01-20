// === CAMBIO DE PESTAÑAS ===
function openTab(evt, tabName) {
    const tabContents = document.getElementsByClassName("tab-content");
    for (let i = 0; i < tabContents.length; i++) {
        tabContents[i].style.display = "none";
    }

    const tabButtons = document.getElementsByClassName("tab-button");
    for (let i = 0; i < tabButtons.length; i++) {
        tabButtons[i].className = tabButtons[i].className.replace(" active", "");
    }

    document.getElementById(tabName).style.display = "block";
    evt.currentTarget.className += " active";
}

// === CONFIGURACIÓN DEL SISTEMA ===
document.addEventListener('DOMContentLoaded', () => {
    const loadBtn = document.getElementById('loadConfigBtn');
    const saveBtn = document.getElementById('saveConfigBtn');
    const statusDiv = document.getElementById('configStatus');

    if (loadBtn && saveBtn) {
        const configFields = [
            'num_batteries',
            'delay_between_batteries',
            'delay_between_cycle_battery',
            'battery_tcp_ip',
            'battery_tcp_port',
            'mqtt_broker_ip',
            'mqtt_broker_port',
            'mqtt_broker_ws_port',
            'mqtt_user',
            'mqtt_password'
        ];

        function showConfigStatus(message, isError = false) {
            statusDiv.textContent = message;
            statusDiv.style.color = isError ? 'red' : 'green';
        }

        async function loadConfig() {
            try {
                const response = await fetch('/api/config');
                if (!response.ok) throw new Error(`HTTP error! status: ${response.status}`);
                const config = await response.json();

                configFields.forEach(field => {
                    const elem = document.getElementById(field);
                    if (elem) {
                        elem.value = config[field] !== undefined ? config[field] : '';
                    }
                });
                showConfigStatus('✅ Configuración cargada correctamente.');
            } catch (error) {
                console.error('Error al cargar la configuración:', error);
                showConfigStatus('❌ Error al cargar la configuración: ' + error.message, true);
            }
        }

        async function saveConfig() {
            try {
                const config = {};
                configFields.forEach(field => {
                    const elem = document.getElementById(field);
                    if (elem) {
                        let value = elem.value;
                        if ([
                            'num_batteries',
                            'delay_between_batteries',
                            'delay_between_cycle_battery',
                            'battery_tcp_port',
                            'mqtt_broker_port',
                            'mqtt_broker_ws_port'
                        ].includes(field)) {
                            value = parseInt(value, 10);
                            if (isNaN(value)) {
                                throw new Error(`El campo "${field}" debe ser un número válido.`);
                            }
                        }
                        config[field] = value;
                    }
                });

                const response = await fetch('/api/config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(config)
                });

                if (!response.ok) throw new Error(`HTTP error! status: ${response.status}`);
                showConfigStatus('✅ Configuración guardada correctamente.');
            } catch (error) {
                console.error('Error al guardar la configuración:', error);
                showConfigStatus('❌ Error al salvar: ' + error.message, true);
            }
        }

        loadBtn.addEventListener('click', loadConfig);
        saveBtn.addEventListener('click', saveConfig);
        loadConfig();
    }
});

// === MONITOREO MQTT ===
let mqttClient = null;
const batteryRawData = {}; // Almacena datos crudos de baterías

function updateSystemTable(data) {
    document.getElementById('sys_voltage').textContent = data.voltage || '-';
    document.getElementById('sys_current').textContent = data.corriente || '-';
    document.getElementById('sys_temp').textContent = data.temperatura || '-';
    document.getElementById('sys_soc').textContent = data.soc || '-';
    document.getElementById('sys_status').textContent = data.estado || '-';
    document.getElementById('sys_coulomb').textContent = data.coulomb || '-';
    document.getElementById('sys_cycle').textContent = data.cycle || '-';
}

function updateBatteryTable(data) {
    const tbody = document.getElementById('batteryTableBody');
    if (!tbody) return;

    tbody.innerHTML = '';

    if (!data || typeof data !== 'object') return;

    const sortedBatteries = Object.keys(data).sort((a, b) => {
        const numA = parseInt(a.replace('battery_', ''));
        const numB = parseInt(b.replace('battery_', ''));
        return numA - numB;
    });

    sortedBatteries.forEach(key => {
        const bat = data[key];
        if (!bat) return;

        const row = document.createElement('tr');
        row.innerHTML = `
            <td>${bat.no_battery || key}</td>
            <td>${bat.voltage || '-'}</td>
            <td>${bat.corriente || '-'}</td>
            <td>${bat.temperatura || '-'}</td>
            <td>${bat.soc || '-'}</td>
            <td>${bat.estado || '-'}</td>
            <td>${bat.coulomb || '-'}</td>
            <td>${bat.cycle || '-'}</td>
        `;
        tbody.appendChild(row);
    });
}

// === FUNCIÓN PARA PESTAÑA 2 CON TABLAS ===
function updateBatteriesDisplay() {
    const container = document.getElementById('batteriesContainer');
    if (!container) return;

    // Verificar si estamos en la pestaña 2
    const tab2 = document.getElementById('tab2');
    if (!tab2 || tab2.style.display !== 'block') return;

    // Obtener configuración para saber cuántas baterías esperar
    fetch('/api/config')
        .then(response => response.json())
        .then(config => {
            const numBatteries = config.num_batteries || 15;
            let html = '';

            // Mostrar cada batería
            for (let i = 1; i <= numBatteries; i++) {
                const batId = i < 10 ? `bat0${i}` : `bat${i}`;
                const data = batteryRawData[batId];

                if (data && Object.keys(data).length > 0) {
                    // Crear tabla para esta batería
                    let tableHtml = `
                        <h3 style="margin-top: 0; color: #333;">🔋 Batería ${i}</h3>
                        <table class="data-table battery-detail-table" style="width: 100%; border-collapse: collapse; margin-bottom: 15px;">
                            <thead>
                                <tr>
                                    <th style="border: 1px solid #ddd; padding: 8px; background-color: #f2f2f2;">Celda</th>
                                    <th style="border: 1px solid #ddd; padding: 8px; background-color: #f2f2f2;">Voltaje (V)</th>
                                    <th style="border: 1px solid #ddd; padding: 8px; background-color: #f2f2f2;">Corriente (A)</th>
                                    <th style="border: 1px solid #ddd; padding: 8px; background-color: #f2f2f2;">Temperatura (°C)</th>
                                    <th style="border: 1px solid #ddd; padding: 8px; background-color: #f2f2f2;">SOC (%)</th>
                                    <th style="border: 1px solid #ddd; padding: 8px; background-color: #f2f2f2;">Estado</th>
                                    <th style="border: 1px solid #ddd; padding: 8px; background-color: #f2f2f2;">Coulomb (Ah)</th>
                                    <th style="border: 1px solid #ddd; padding: 8px; background-color: #f2f2f2;">Balance</th>
                                </tr>
                            </thead>
                            <tbody>
                    `;

                    // Ordenar celdas por número
                    const sortedCells = Object.keys(data).sort((a, b) => parseInt(a) - parseInt(b));

                    sortedCells.forEach(cellNum => {
                        const cell = data[cellNum];
                        if (!cell) return;

                        tableHtml += `
                            <tr style="background-color: ${parseInt(cellNum) % 2 === 0 ? '#f9f9f9' : 'white'};">
                                <td style="border: 1px solid #ddd; padding: 8px; text-align: center;">${cellNum}</td>
                                <td style="border: 1px solid #ddd; padding: 8px; text-align: center;">${cell.voltage !== undefined ? parseFloat(cell.voltage).toFixed(3) : '-'}</td>
                                <td style="border: 1px solid #ddd; padding: 8px; text-align: center;">${cell.corriente !== undefined ? parseFloat(cell.corriente).toFixed(3) : '-'}</td>
                                <td style="border: 1px solid #ddd; padding: 8px; text-align: center;">${cell.temperatura !== undefined ? parseFloat(cell.temperatura).toFixed(1) : '-'}</td>
                                <td style="border: 1px solid #ddd; padding: 8px; text-align: center;">${cell.soc !== undefined ? parseFloat(cell.soc).toFixed(1) : '-'}</td>
                                <td style="border: 1px solid #ddd; padding: 8px; text-align: center;">${cell.estado || '-'}</td>
                                <td style="border: 1px solid #ddd; padding: 8px; text-align: center;">${cell.coulomb !== undefined ? parseFloat(cell.coulomb).toFixed(3) : '-'}</td>
                                <td style="border: 1px solid #ddd; padding: 8px; text-align: center;">${cell.balance ? 'Sí' : 'No'}</td>
                            </tr>
                        `;
                    });

                    tableHtml += `
                            </tbody>
                        </table>
                    `;

                    html += `<div style="margin-bottom: 30px;">${tableHtml}</div>`;
                } else {
                    html += `
                        <div style="margin-bottom: 20px; padding: 15px; border: 1px solid #eee; border-radius: 8px; background: #fafafa;">
                            <h3 style="margin-top: 0; color: #666;">🔋 Batería ${i}</h3>
                            <p style="color: #888;">Esperando datos...</p>
                        </div>
                    `;
                }
            }

            container.innerHTML = html;
        })
        .catch(error => {
            console.error('Error al cargar configuración:', error);
            container.innerHTML = '<p style="color: red;">❌ Error al cargar la configuración</p>';
        });
}

function updateMqttStatus(status, message) {
    const statusDiv = document.getElementById('mqttStatus');
    if (statusDiv) {
        statusDiv.className = `status-indicator status-${status}`;
        statusDiv.textContent = message;
    }
}

function connectMqtt() {
    fetch('/api/config')
        .then(response => response.json())
        .then(config => {
            const wsPort = config.mqtt_broker_ws_port || 9001;
            const brokerIp = config.mqtt_broker_ip || '10.0.0.250';
            const MQTT_BROKER = `ws://${brokerIp}:${wsPort}`;

            const SYSTEM_TOPIC = 'homeassistant/pylon/total_system';
            const BATTERY_TOPIC = 'homeassistant/pylon/total_battery';

            // Suscribirse a topics de baterías individuales
            const batteryTopics = [];
            const numBatteries = config.num_batteries || 15;
            for (let i = 1; i <= numBatteries; i++) {
                const batId = i < 10 ? `0${i}` : `${i}`;
                batteryTopics.push(`homeassistant/pylon/bat${batId}`);
            }

            mqttClient = mqtt.connect(MQTT_BROKER);

            mqttClient.on('connect', () => {
                console.log('MQTT conectado');
                updateMqttStatus('connected', '✅ Conectado al broker MQTT');
                mqttClient.subscribe([SYSTEM_TOPIC, BATTERY_TOPIC, ...batteryTopics]);
            });

            mqttClient.on('message', (topic, message) => {
                try {
                    const data = JSON.parse(message.toString());

                    if (topic === SYSTEM_TOPIC) {
                        updateSystemTable(data);
                    } else if (topic === BATTERY_TOPIC) {
                        updateBatteryTable(data);
                    } else if (topic.startsWith('homeassistant/pylon/bat')) {
                        // ✅ CORRECCIÓN CLAVE: extraer el ID correcto del topic
                        const batId = topic.split('/').pop(); // Obtiene "bat01", "bat02", etc.
                        batteryRawData[batId] = data;

                        // Actualizar vista si estamos en la pestaña 2
                        const tab2 = document.getElementById('tab2');
                        if (tab2 && tab2.style.display === 'block') {
                            setTimeout(updateBatteriesDisplay, 50);
                        }
                    }
                } catch (e) {
                    console.error('Error al parsear mensaje MQTT:', e);
                }
            });

            mqttClient.on('error', (error) => {
                console.error('Error MQTT:', error);
                updateMqttStatus('disconnected', '❌ Error de conexión MQTT');
            });

            mqttClient.on('close', () => {
                console.log('Conexión MQTT cerrada');
                updateMqttStatus('disconnected', '⚠️ Desconectado del broker MQTT');
                setTimeout(connectMqtt, 5000);
            });

            mqttClient.on('reconnect', () => {
                console.log('Reconectando a MQTT...');
                updateMqttStatus('connecting', '🔄 Reconectando al broker MQTT...');
            });
        })
        .catch(error => {
            console.error('Error al cargar configuración para MQTT:', error);
            updateMqttStatus('disconnected', '❌ No se pudo obtener configuración MQTT');
        });
}

// Observador mejorado
let mqttInitialized = false;
let lastTabCheck = 0;

const observer = new MutationObserver(() => {
    const now = Date.now();
    // Limitar a una vez por segundo para evitar sobrecarga
    if (now - lastTabCheck < 1000) return;
    lastTabCheck = now;

    const tab1 = document.getElementById('tab1');
    const tab2 = document.getElementById('tab2');

    // Iniciar MQTT si estamos en Totales o Baterías
    if ((tab1 && tab1.style.display === 'block') || (tab2 && tab2.style.display === 'block')) {
        if (!mqttInitialized) {
            mqttInitialized = true;
            connectMqtt();
        }
    }

    // Actualizar vista de baterías si estamos en la pestaña 2
    if (tab2 && tab2.style.display === 'block') {
        // Mostrar mensaje inicial
        const container = document.getElementById('batteriesContainer');
        if (container && container.innerHTML.includes('Conectando')) {
            container.innerHTML = '<p>Cargando datos de baterías...</p>';
        }
        // Actualizar con datos reales
        setTimeout(updateBatteriesDisplay, 100);
    }
});

observer.observe(document.body, {
    childList: true,
    subtree: true
});
