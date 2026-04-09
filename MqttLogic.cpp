#pragma GCC optimize ("O3") 
#include "MqttLogic.h"
#include "SharedData.h"
#include "SystemLogic.h" // Fuer isStreamActive und cameraRefreshMs
#include <WiFi.h>
#include <ArduinoJson.h>
#include "GuiManager.h"  

// --- MQTT TOPICS ---
const char* mqtt_topic_pressure = "lolacatmat/sensor/pressure";
const char* mqtt_topic_rssi = "lolacatmat/sensor/mat_rssi";
const char* mqtt_topic_battery = "lolacatmat/sensor/battery";
const char* mqtt_topic_alarm_state = "lolacatmat/switch/alarm/state";
const char* mqtt_topic_alarm_cmd = "lolacatmat/switch/alarm/set";
const char* mqtt_topic_disconnect_state = "lolacatmat/switch/disconnect/state";
const char* mqtt_topic_disconnect_cmd = "lolacatmat/switch/disconnect/set";
const char* mqtt_topic_kippy_status = "lolacatmat/kippy/status";

// --- LOKALE HILFSFUNKTIONEN ---
static void addDeviceToDoc(DynamicJsonDocument& doc, String devId) {
    JsonObject dev = doc.createNestedObject("device");
    JsonArray ids = dev.createNestedArray("identifiers");
    ids.add(devId);
    dev["name"] = "LolaCatMat";
    dev["model"] = "Smart Mat OS";
    dev["manufacturer"] = "Custom";
}

static void publishAutoDiscovery() {
    String devMacStr = WiFi.macAddress();
    devMacStr.replace(":", "");
    String deviceId = "lolacatmat_" + devMacStr;

    DynamicJsonDocument doc(1024); 
    String payload; 

    doc.clear(); doc["name"] = "Druckwert"; doc["unique_id"] = deviceId + "_pressure"; doc["state_topic"] = mqtt_topic_pressure; doc["icon"] = "mdi:weight-kilogram"; doc["state_class"] = "measurement";
    addDeviceToDoc(doc, deviceId); serializeJson(doc, payload);
    mqttClient.publish(("homeassistant/sensor/" + deviceId + "/pressure/config").c_str(), payload.c_str(), true);

    payload = "";
    doc.clear(); doc["name"] = "Matte Signalstärke"; doc["unique_id"] = deviceId + "_rssi"; doc["state_topic"] = mqtt_topic_rssi; doc["device_class"] = "signal_strength"; doc["unit_of_measurement"] = "dBm";
    addDeviceToDoc(doc, deviceId); serializeJson(doc, payload);
    mqttClient.publish(("homeassistant/sensor/" + deviceId + "/mat_rssi/config").c_str(), payload.c_str(), true);

    payload = "";
    doc.clear(); doc["name"] = "Akku ESP32"; doc["unique_id"] = deviceId + "_battery"; doc["state_topic"] = mqtt_topic_battery; doc["device_class"] = "battery"; doc["unit_of_measurement"] = "%";
    addDeviceToDoc(doc, deviceId); serializeJson(doc, payload);
    mqttClient.publish(("homeassistant/sensor/" + deviceId + "/battery/config").c_str(), payload.c_str(), true);

    payload = "";
    doc.clear(); doc["name"] = "Katze auf Matte"; doc["unique_id"] = deviceId + "_alarm"; doc["state_topic"] = mqtt_topic_alarm_state; doc["command_topic"] = mqtt_topic_alarm_cmd; doc["icon"] = "mdi:bell-ring";
    addDeviceToDoc(doc, deviceId); serializeJson(doc, payload);
    mqttClient.publish(("homeassistant/switch/" + deviceId + "/alarm/config").c_str(), payload.c_str(), true);

    payload = "";
    doc.clear(); doc["name"] = "Verbindungsabbruch"; doc["unique_id"] = deviceId + "_disconnect"; doc["state_topic"] = mqtt_topic_disconnect_state; doc["command_topic"] = mqtt_topic_disconnect_cmd; doc["icon"] = "mdi:lan-disconnect";
    addDeviceToDoc(doc, deviceId); serializeJson(doc, payload);
    mqttClient.publish(("homeassistant/switch/" + deviceId + "/disconnect/config").c_str(), payload.c_str(), true);

    payload = "";
    doc.clear(); doc["name"] = "Baby Alarm"; doc["unique_id"] = deviceId + "_baby_alarm"; 
    doc["state_topic"] = "lolacatmat/baby/alarm/state"; doc["command_topic"] = "lolacatmat/baby/alarm/set"; doc["icon"] = "mdi:baby-carriage";
    addDeviceToDoc(doc, deviceId); serializeJson(doc, payload);
    mqttClient.publish(("homeassistant/switch/" + deviceId + "/baby_alarm/config").c_str(), payload.c_str(), true);
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (int i = 0; i < length; i++) message += (char)payload[i];
    String t = String(topic);

    if (t == mqtt_topic_alarm_cmd) {
        if (message == "ON") { isArmed = true; alarmActive = true; muted = false; wakeDisplay(); } 
        else if (message == "OFF") { alarmActive = false; isArmed = false; muted = false; }
        mqttClient.publish(mqtt_topic_alarm_state, alarmActive ? "ON" : "OFF", true);
    } 
    else if (t == mqtt_topic_disconnect_cmd) {
        if (message == "OFF" && disconnectAlarmActive) { disconnectAlarmActive = false; muted = false; isArmed = wasArmedBeforeDisconnect; }
        mqttClient.publish(mqtt_topic_disconnect_state, disconnectAlarmActive ? "ON" : "OFF", true);
    } 
    else if (t == mqtt_topic_kippy_status) {
        isTrackerDataValid = true; isCatAtHome = (message == "home" || message == "zuhause");
    }
    else if (t == mqttBabyTopic) {
        if (message == "baby_alarm") {
            if (isBabyArmed && !babyAlarmActive) {
                babyAlarmActive = true; babyMuted = false; wakeDisplay();
            }
        } 
    }
    else if (t == "lolacatmat/baby/alarm/set") {
        // --- NEU: Baby-Alarm manuell aktivieren/deaktivieren ---
        if (message == "ON") {
            babyAlarmActive = true; babyMuted = false; wakeDisplay();
        } else if (message == "OFF") {
            babyAlarmActive = false; babyMuted = false;
        }
    }
}

// --- DIE EIGENTLICHE MQTT TASK ---
static void mqttTask(void * pvParameters) {
    int16_t lastSentP = -999; 
    bool lastSentAlarm = false; 
    bool lastSentDisconnect = false; 
    uint32_t lastGetTime = 0; 
    uint32_t lastCameraTrigger = 0;
    bool lastSentBabyAlarm = false;

    for(;;) {
        if (webSetupMode > 0 || pendingWebSetupMode > 0 || screenshotModeActive || pendingScreenshotMode > 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        
        if (mqttEnabled && mqttBroker.length() >= 4 && wifiEnabled && wifiStarted && WiFi.status() == WL_CONNECTED && !isSetupScanning && effPrioWifi > 0) {
            if (!timeSynced) { 
                configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov"); 
                timeSynced = true; 
            }
            IPAddress brokerIP; 
            if (brokerIP.fromString(mqttBroker)) { mqttClient.setServer(brokerIP, mqttPort); } 
            else { mqttClient.setServer(mqttBroker.c_str(), mqttPort); }
            
            mqttClient.setCallback(mqttCallback); 
            
            if (!mqttClient.connected()) {
                String clientId = "LolaCatMat-" + String(random(0xffff), HEX); 
                bool success = false;
                if (mqttUser.length() > 0) { success = mqttClient.connect(clientId.c_str(), mqttUser.c_str(), mqttPass.c_str()); } 
                else { success = mqttClient.connect(clientId.c_str()); }
                
                if (success) { 
                    publishAutoDiscovery(); 
                    mqttClient.subscribe(mqtt_topic_alarm_cmd); 
                    mqttClient.subscribe(mqtt_topic_disconnect_cmd); 
                    mqttClient.subscribe(mqtt_topic_kippy_status); 
                    mqttClient.subscribe(mqttBabyTopic.c_str()); 
                    mqttClient.subscribe("lolacatmat/baby/alarm/set"); 

                    // --- NEU: FORCE SYNC BEIM BOOT/RECONNECT ---
                    // Wir drehen die letzten Stats kuenstlich um, damit der ESP seinen Zustand 
                    // zwingend an den MQTT Broker sendet und alte Werte ("ON") ueberschreibt.
                    lastSentAlarm = !alarmActive;
                    lastSentDisconnect = !disconnectAlarmActive;
                    lastSentBabyAlarm = !babyAlarmActive;
                    lastSentP = -999;
                }
            } else {
                mqttClient.loop();
                if (connected && abs(currentPressure - lastSentP) > 10) { mqttClient.publish(mqtt_topic_pressure, String(currentPressure).c_str(), false); lastSentP = currentPressure; }
                if (alarmActive != lastSentAlarm) { mqttClient.publish(mqtt_topic_alarm_state, alarmActive ? "ON" : "OFF", true); lastSentAlarm = alarmActive; }
                if (disconnectAlarmActive != lastSentDisconnect) { mqttClient.publish(mqtt_topic_disconnect_state, disconnectAlarmActive ? "ON" : "OFF", true); lastSentDisconnect = disconnectAlarmActive; }
                
                if (babyAlarmActive != lastSentBabyAlarm) {
                    mqttClient.publish("lolacatmat/baby/alarm/state", babyAlarmActive ? "ON" : "OFF", true);
                    lastSentBabyAlarm = babyAlarmActive;
                }

                if (isStreamActive && gui.getCurrentScreen() == SCREEN_BABY) {
                    if (millis() - lastCameraTrigger >= cameraRefreshMs) {
                        lastCameraTrigger = millis();
                        mqttClient.publish(mqttCameraTriggerTopic.c_str(), "TRIGGER", false);
                    }
                }

                int syncDelay = (effPrioWifi < 30.0) ? 15000 : 10000; 
                if (millis() - lastGetTime > syncDelay) { 
                    lastGetTime = millis(); 
                    mqttClient.publish(mqtt_topic_battery, String(batteryPercent).c_str(), true); 
                    if (connected) { mqttClient.publish(mqtt_topic_rssi, String(pClient->getRssi()).c_str(), true); } 
                }
            }
        } else {
            if (mqttClient.connected()) {
                mqttClient.disconnect();
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(50)); 
    }
}

void MqttLogic_Init() {
    xTaskCreatePinnedToCore(mqttTask, "MQTTTask", 8192, NULL, 1, NULL, 0); 
}