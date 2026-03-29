#include "SystemLogic.h"
#include "SharedData.h"
#include <WiFi.h>
#include <ArduinoJson.h> 
#include "lvgl.h" 
#include <HTTPClient.h>      
#include "secrets.h"         
#include "GuiManager.h"      
#include "ViewBaby.h"        
#include <JPEGDEC.h> 

const char* mqtt_topic_pressure = "lolacatmat/sensor/pressure";
const char* mqtt_topic_rssi = "lolacatmat/sensor/mat_rssi";
const char* mqtt_topic_battery = "lolacatmat/sensor/battery";
const char* mqtt_topic_alarm_state = "lolacatmat/switch/alarm/state";
const char* mqtt_topic_alarm_cmd = "lolacatmat/switch/alarm/set";
const char* mqtt_topic_disconnect_state = "lolacatmat/switch/disconnect/state";
const char* mqtt_topic_disconnect_cmd = "lolacatmat/switch/disconnect/set";
const char* mqtt_topic_kippy_status = "lolacatmat/kippy/status"; 

volatile int pendingScreenshotMode = 0; 
volatile bool screenshotModeActive = false;

volatile bool isStreamActive = false; 
int cameraRefreshMs = 300; 
volatile bool requestImageLoad = false; 

void SystemLogic_TriggerImageLoad() {
    requestImageLoad = true;
}

static lv_image_dsc_t cam_img_dsc[2] = {{0}, {0}};
static uint8_t dsc_idx = 0;
static uint8_t* jpg_bufs[2] = {nullptr, nullptr}; 

JPEGDEC jpeg;
volatile uint16_t* jpeg_decode_target = nullptr;
volatile uint16_t jpeg_decode_width = 0;
volatile uint16_t jpeg_decode_height = 0;

int JPEGDraw(JPEGDRAW *pDraw) {
    if (!jpeg_decode_target) return 0;
    for (int y = 0; y < pDraw->iHeight; y++) {
        int absolute_y = pDraw->y + y;
        if (absolute_y >= jpeg_decode_height) break; 
        
        int absolute_x = pDraw->x;
        int draw_width = pDraw->iWidth;
        if (absolute_x + draw_width > jpeg_decode_width) {
            draw_width = jpeg_decode_width - absolute_x;
        }
        if (draw_width <= 0) continue;

        memcpy((void*)&jpeg_decode_target[absolute_y * jpeg_decode_width + absolute_x],
               &pDraw->pPixels[y * pDraw->iWidth],
               draw_width * 2);
    }
    return 1; 
}

String getSetupHtml() {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0, user-scalable=no'>";
    html += "<title>LolaCatMat Setup</title>";
    html += "<style>body{background:#1a1a1a;color:#fff;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;padding:20px;text-align:center;}";
    html += "h2{color:#00A0FF;margin-bottom:5px;}p{color:#aaa;font-size:14px;margin-bottom:30px;}";
    html += "input{width:100%;padding:14px;margin:5px 0 20px 0;border-radius:8px;border:1px solid #333;background:#2a2a2a;color:#fff;font-size:16px;box-sizing:border-box;}";
    html += "button{background:#00A0FF;color:#fff;border:none;padding:16px;width:100%;border-radius:8px;font-size:16px;font-weight:bold;cursor:pointer;margin-top:10px;}";
    html += "h3{text-align:left;border-bottom:1px solid #333;padding-bottom:5px;margin-top:30px;color:#ddd;}";
    html += "label{display:block;text-align:left;margin-top:15px;font-size:13px;color:#00A0FF;text-transform:uppercase;letter-spacing:1px;font-weight:bold;}</style></head><body>";
    
    html += "<h2>LolaCatMat</h2><p>Smart Home Konfiguration</p><form action='/save' method='POST'>";
    
    if (webSetupMode == 1) {
        html += "<h3>WLAN Verbindung</h3>";
        html += "<label>WLAN Netzwerkname (SSID)</label><input type='text' name='ssid' placeholder='z.B. Router_123' value='" + wifiSsid + "'>";
        html += "<label>WLAN Passwort</label><input type='password' name='pass' placeholder='WLAN Passwort eingeben' value='" + wifiPass + "'>";
    }
    
    html += "<h3>MQTT Broker Einstellungen</h3>";
    html += "<label>Broker IP-Adresse (Pflichtfeld)</label><input type='text' name='mqIP' placeholder='z.B. 192.168.1.50' value='" + mqttBroker + "'>";
    html += "<label>Broker Port (Standard: 1883)</label><input type='number' name='mqPort' placeholder='1883' value='" + String(mqttPort) + "'>";
    html += "<label>Benutzername (Optional)</label><input type='text' name='mqUser' placeholder='MQTT Benutzer' value='" + mqttUser + "'>";
    html += "<label>Passwort (Optional)</label><input type='password' name='mqPass' placeholder='MQTT Passwort' value='" + mqttPass + "'>";
    
    html += "<h3>Baby Monitor & Audio</h3>";
    html += "<label>Babyphone Video URL (MJPEG oder Snapshot)</label><input type='text' name='camEntity' placeholder='http://192.168.../api/stream.mjpeg' value='" + camEntity + "'>";
    html += "<label>Babyphone Audio Stream URL</label><input type='text' name='babyUrl' placeholder='http://192.168.../api/stream.aac' value='" + babyStreamUrl + "'>";
    
    html += "<h3>Home Assistant & Fallbacks</h3>";
    html += "<label>Home Assistant IP-Adresse</label><input type='text' name='haIP' placeholder='z.B. 192.168.1.100' value='" + haIP + "'>";
    html += "<label>Home Assistant Port (Standard: 8123)</label><input type='number' name='haPort' placeholder='8123' value='" + String(haPort) + "'>";
    html += "<label>Snapshot Trigger Topic (MQTT)</label><input type='text' name='mqttCamTrig' placeholder='camera/trigger/snapshot' value='" + mqttCameraTriggerTopic + "'>";
    html += "<label>Baby Alarm Ausloeser (MQTT Topic)</label><input type='text' name='mqttBaby' placeholder='smartmat/baby/cry/state' value='" + mqttBabyTopic + "'>";

    html += "<button type='submit'>Speichern & Neustart</button></form></body></html>";
    return html;
}

void handleRoot() { server.send(200, "text/html", getSetupHtml()); }

String sanitizeString(String input) {
    input.trim();
    input.replace("\xE2\x80\x8B", ""); 
    input.replace("\xC2\xA0", "");     
    return input;
}

void handleSave() {
    preferences.begin("catmat", false);
    if (server.hasArg("ssid")) { String s = sanitizeString(server.arg("ssid")); preferences.putString("wifiSsid", s); }
    if (server.hasArg("pass")) { String p = sanitizeString(server.arg("pass")); preferences.putString("wifiPass", p); }
    if (server.hasArg("mqIP")) { String i = sanitizeString(server.arg("mqIP")); i.replace(" ", ""); preferences.putString("mqIP", i); }
    if (server.hasArg("mqPort")) { String pt = sanitizeString(server.arg("mqPort")); int p_int = pt.toInt(); if (p_int <= 0) p_int = 1883; preferences.putInt("mqPort", p_int); }
    if (server.hasArg("mqUser")) { String u = sanitizeString(server.arg("mqUser")); preferences.putString("mqUser", u); }
    if (server.hasArg("mqPass")) { String pw = sanitizeString(server.arg("mqPass")); preferences.putString("mqPass", pw); }
    
    if (server.hasArg("haIP")) { String i = sanitizeString(server.arg("haIP")); i.replace(" ", ""); preferences.putString("haIP", i); }
    if (server.hasArg("haPort")) { String pt = sanitizeString(server.arg("haPort")); int p_int = pt.toInt(); if (p_int <= 0) p_int = 8123; preferences.putInt("haPort", p_int); }
    if (server.hasArg("camEntity")) { String u = sanitizeString(server.arg("camEntity")); preferences.putString("camEntity", u); }
    if (server.hasArg("mqttBaby")) { String pw = sanitizeString(server.arg("mqttBaby")); preferences.putString("mqttBaby", pw); }
    if (server.hasArg("mqttCamTrig")) { String tr = sanitizeString(server.arg("mqttCamTrig")); preferences.putString("mqttCamTrig", tr); }
    if (server.hasArg("babyUrl")) { String u = sanitizeString(server.arg("babyUrl")); preferences.putString("babyUrl", u); }
    preferences.end();
    
    String successHtml = "<!DOCTYPE html><html><body style='background:#1a1a1a;color:#fff;text-align:center;padding:50px;font-family:sans-serif;'>";
    successHtml += "<h2 style='color:#00FF00;'>Erfolgreich gespeichert!</h2><p>Die LolaCatMat startet jetzt neu und verbindet sich.</p></body></html>";
    server.send(200, "text/html", successHtml);
    delay(2000); ESP.restart(); 
}

void handleCaptivePortal() {
    server.sendHeader("Location", String("http://") + server.client().localIP().toString(), true);
    server.send(302, "text/plain", "");
}

void addDeviceToDoc(DynamicJsonDocument& doc, String devId) {
    JsonObject dev = doc.createNestedObject("device");
    JsonArray ids = dev.createNestedArray("identifiers");
    ids.add(devId);
    dev["name"] = "LolaCatMat";
    dev["model"] = "Smart Mat OS";
    dev["manufacturer"] = "Custom";
}

void publishAutoDiscovery() {
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

void mqttCallback(char* topic, byte* payload, unsigned int length) {
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
        if (message == "OFF") {
            babyAlarmActive = false; babyMuted = false;
        }
    }
}

extern bool lvgl_port_lock(uint32_t timeout_ms);
extern void lvgl_port_unlock(void);

void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    uint32_t now = millis();
    if (lastNotifyTime != 0) {
        uint32_t delta = now - lastNotifyTime; intervalHistory[intervalIdx] = delta; intervalIdx = (intervalIdx + 1) % INTERVAL_HISTORY_SIZE;
        if (intervalCount < INTERVAL_HISTORY_SIZE) intervalCount++;
        if (intervalCount > 0) {
            float sum = 0; for (int i = 0; i < intervalCount; i++) sum += intervalHistory[i];
            avgInterval = sum / intervalCount; float varianceSum = 0;
            for (int i = 0; i < intervalCount; i++) varianceSum += std::pow(intervalHistory[i] - avgInterval, 2);
            stdDevInterval = std::sqrt(varianceSum / intervalCount);
        }
    }
    lastNotifyTime = now; 
    
    if (length >= 10 && pData[0] == 0xA5 && pData[1] == 0x5A && pData[9] == 0x99) {
        for (int i = 0; i < 10; i++) { lastBtPackets[btPacketIdx][i] = pData[i]; }
        btPacketIdx = (btPacketIdx + 1) % MAX_BT_MSGS;
        if (btPacketCount < MAX_BT_MSGS) btPacketCount++;

        rawPressure = (pData[5] << 8) | pData[6];
        int32_t tempPressure = rawPressure - taraOffset; 
        currentPressure = tempPressure;
        if (tempPressure > intervalMaxPressure) intervalMaxPressure = tempPressure;
        
        pressWindow[pWinIdx] = tempPressure; pWinIdx = (pWinIdx + 1) % WINDOW_SIZE;
        if (pWinCount < WINDOW_SIZE) pWinCount++;
        if (pWinCount > 0) {
            int32_t sum = 0; for (int i = 0; i < pWinCount; i++) sum += pressWindow[i];
            currentAvg = sum / pWinCount;
            
            if (isArmed && !alarmActive && !disconnectAlarmActive && millis() > cooldownUntil) {
                if (pWinCount >= WINDOW_SIZE && abs(tempPressure - currentAvg) > thresholdVal) { 
                    alarmActive = true; muted = false; wakeDisplay(); requestMainTab = true; 
                }
            }
        }
    }
}

void executeDisconnectLogic(bool intentional) {
    connected = false; pWinCount = 0; pWinIdx = 0; intervalCount = 0; intervalIdx = 0; avgInterval = 0.0; stdDevInterval = 0.0;
    if (!intentional && !pendingBleReconnect && !isTrackerMode && matEnabled && (millis() - startTime > 15000)) { 
        wasArmedBeforeDisconnect = isArmed; disconnectAlarmActive = true; 
        if (!alarmActive) { muted = false; } 
        wakeDisplay(); requestMainTab = true; 
    }
}

class MyClientCallbacks : public BLEClientCallbacks {
    void onConnect(BLEClient* _pClient) { 
        connected = true; rssiIsZero = false; lastConnectTime = millis(); lastNotifyTime = millis();
        if (disconnectAlarmActive) { isArmed = wasArmedBeforeDisconnect; disconnectAlarmActive = false; muted = false; autoHealedDisconnectCount++; }
    }
    void onDisconnect(BLEClient* _pClient) { if (connected) executeDisconnectLogic(intentionalDisconnect); intentionalDisconnect = false; }
};

class MyScanCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        String devMac = String(advertisedDevice.getAddress().toString().c_str()); devMac.toLowerCase();
        String devName = advertisedDevice.haveName() ? String(advertisedDevice.getName().c_str()) : "";

        if (isSetupScanning) {
            if (bleMutex != NULL && xSemaphoreTake(bleMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                bool exists = false;
                for(int i=0; i<scanResultCount; i++) {
                    if (scanResultMacs[i] == devMac) { 
                        exists = true; 
                        if (devName != "" && scanResultNames[i] == "Unbekannt") scanResultNames[i] = devName;
                        scanResultRssi[i] = advertisedDevice.getRSSI(); break; 
                    }
                }
                if (!exists && scanResultCount < MAX_SCAN_DEVICES) {
                    scanResultMacs[scanResultCount] = devMac;
                    scanResultNames[scanResultCount] = (devName == "") ? "Unbekannt" : devName;
                    scanResultRssi[scanResultCount] = advertisedDevice.getRSSI();
                    scanResultCount++;
                }
                xSemaphoreGive(bleMutex);
            }
            return; 
        }
        String kippyLower = savedKippyMac; kippyLower.toLowerCase();
        if ((kippyEnabled || isTrackerMode) && (devMac == kippyLower)) { catRssi = advertisedDevice.getRSSI(); lastCatSeenTime = millis(); }
    }
};

void scanEndedCB(BLEScanResults results) { if (isSetupScanning) scanJustFinished = true; }

void setUiStatus(const char* msg) {
    if (lvgl_port_lock(portMAX_DELAY)) {
        ViewBaby_SetStatus(msg);
        lvgl_port_unlock();
    }
}

// -------------------------------------------------------------
// STABILER MJPEG VIDEO TASK (Inklusive Drop-Puffer und vTaskDelay)
// -------------------------------------------------------------
#define MAX_JPEG_DOWNLOAD_SIZE 256000    
#define MAX_PIXEL_BUF_SIZE (320 * 180 * 2) 

void videoTask(void * pvParameters) {
    uint32_t waitTimer = 0;
    
    uint8_t* download_buf = (uint8_t*)heap_caps_malloc(MAX_JPEG_DOWNLOAD_SIZE, MALLOC_CAP_SPIRAM);
    jpg_bufs[0] = (uint8_t*)heap_caps_malloc(MAX_PIXEL_BUF_SIZE, MALLOC_CAP_SPIRAM);
    jpg_bufs[1] = (uint8_t*)heap_caps_malloc(MAX_PIXEL_BUF_SIZE, MALLOC_CAP_SPIRAM);

    if(!download_buf || !jpg_bufs[0] || !jpg_bufs[1]) {
        setUiStatus("Kritischer RAM Fehler!");
        vTaskDelay(portMAX_DELAY); 
    }

    HTTPClient http; 
    http.setReuse(true);       
    http.setTimeout(3000); 

    for(;;) {
        if (!isStreamActive || gui.getCurrentScreen() != SCREEN_BABY) {
            vTaskDelay(pdMS_TO_TICKS(200)); 
            continue;
        }
        
        if (WiFi.status() != WL_CONNECTED) {
            setUiStatus("WLAN fehlt... Retry");
            vTaskDelay(pdMS_TO_TICKS(2000)); 
            continue; 
        }
        
        String url = camEntity;
        if (!url.startsWith("http")) {
            if (!url.startsWith("/")) url = "/" + url;
            url = "http://" + haIP + ":" + String(haPort) + url;
        }
        
        setUiStatus("Verbinde Stream...");
        http.begin(url); 
        int httpCode = http.GET();
        
        if (httpCode == HTTP_CODE_OK) {
            WiFiClient * stream = http.getStreamPtr();
            stream->setNoDelay(true); //Reduzieren von Videodelay
            setUiStatus("Stream laeuft!");

            char headerBuf[128];
            int headerLen = 0;

            while (isStreamActive && gui.getCurrentScreen() == SCREEN_BABY && stream->connected()) {
                
                int frameSize = 0;
                bool headerDone = false;
                headerLen = 0;
                
                while (stream->connected()) {
                    if (stream->available()) {
                        char c = stream->read();
                        if (c == '\n') {
                            headerBuf[headerLen] = '\0';
                            String line = String(headerBuf);
                            line.trim(); 
                            line.toLowerCase();
                            
                            if (line.startsWith("content-length:")) {
                                frameSize = line.substring(15).toInt();
                            }
                            
                            if (line.length() == 0) { 
                                if (frameSize > 0) {
                                    headerDone = true;
                                    break; 
                                }
                            }
                            headerLen = 0; 
                        } else if (c != '\r') {
                            if (headerLen < 127) headerBuf[headerLen++] = c;
                        }
                    } else {
                        vTaskDelay(1);
                    }
                }

                if (!headerDone) break;

                if (frameSize > 0 && frameSize <= MAX_JPEG_DOWNLOAD_SIZE) {
                    size_t bytesRead = 0;
                    uint32_t startReadTime = millis();
                    
                    while (bytesRead < frameSize && stream->connected() && (millis() - startReadTime < 3000)) {
                        size_t toRead = frameSize - bytesRead;
                        size_t readNow = stream->read(download_buf + bytesRead, toRead);
                        if (readNow > 0) {
                            bytesRead += readNow;
                        } else {
                            vTaskDelay(1); 
                        }
                    }

                    if (bytesRead == frameSize) {
                        if (download_buf[0] == 0xFF && download_buf[1] == 0xD8) {
                            
                            // =======================================================
                            // Latenz-Dropper mit integriertem vTaskDelay(1) Watchdog-Schutz
                            // =======================================================
                            if (mjpegDropThreshold == 0 && stream->available() > 0) {
                                vTaskDelay(1); 
                                continue; 
                            } else if (mjpegDropThreshold > 0 && stream->available() > mjpegDropThreshold) {
                                vTaskDelay(1); 
                                continue; 
                            }

                            if (jpeg.openRAM(download_buf, frameSize, JPEGDraw)) {
                                //640x320 Bild wird halbiert w=320 h=180
                                uint16_t w = jpeg.getWidth() / 2; 
                                uint16_t h = jpeg.getHeight() / 2; 
                                uint32_t raw_size = w * h * 2; 

                                if (raw_size <= MAX_PIXEL_BUF_SIZE) {
                                    dsc_idx = 1 - dsc_idx; 
                                    jpeg_decode_target = (uint16_t*)jpg_bufs[dsc_idx]; 
                                    jpeg_decode_width = w; 
                                    jpeg_decode_height = h; 
                                    
                                    // Hardware Fractional Scaling: Ueberspringt Pixel für mehr Speed!
                                    jpeg.decode(0, 0, JPEG_SCALE_HALF); 

                                    if (lvgl_port_lock(portMAX_DELAY)) {
                                        lv_image_cache_drop(&cam_img_dsc[dsc_idx]);
                                        cam_img_dsc[dsc_idx].header.magic = LV_IMAGE_HEADER_MAGIC;
                                        cam_img_dsc[dsc_idx].header.cf = LV_COLOR_FORMAT_RGB565; 
                                        cam_img_dsc[dsc_idx].header.w = w; 
                                        cam_img_dsc[dsc_idx].header.h = h;
                                        cam_img_dsc[dsc_idx].header.stride = w * 2; 
                                        cam_img_dsc[dsc_idx].header.flags = 0;
                                        cam_img_dsc[dsc_idx].data_size = raw_size; 
                                        cam_img_dsc[dsc_idx].data = jpg_bufs[dsc_idx];
                                        ViewBaby_SetImage(&cam_img_dsc[dsc_idx]); 
                                        lvgl_port_unlock();
                                    }
                                }
                            }
                        } else {
                            break; 
                        }
                    } else {
                        break; 
                    }
                } else if (frameSize > MAX_JPEG_DOWNLOAD_SIZE) {
                     size_t skipped = 0;
                     uint32_t startSkip = millis();
                     while (skipped < frameSize && stream->connected() && (millis() - startSkip < 3000)) {
                         size_t toSkip = frameSize - skipped;
                         if (toSkip > MAX_JPEG_DOWNLOAD_SIZE) toSkip = MAX_JPEG_DOWNLOAD_SIZE;
                         size_t readNow = stream->read(download_buf, toSkip);
                         if (readNow > 0) skipped += readNow;
                         else vTaskDelay(1);
                     }
                }
                
                if (stream->available() == 0) {
                    vTaskDelay(pdMS_TO_TICKS(5)); 
                } else {
                    vTaskDelay(1); 
                }
            }
        } else {
            char errBuf[256]; snprintf(errBuf, sizeof(errBuf), "HTTP %d Retry", httpCode); setUiStatus(errBuf);
        }
        
        http.end(); 
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}

void mqttSyncTask(void * pvParameters) {
    int16_t lastSentP = -999; bool lastSentAlarm = false; bool lastSentDisconnect = false; uint32_t lastGetTime = 0; 
    
    uint32_t lastCameraTrigger = 0;
    bool lastSentBabyAlarm = false;

    for(;;) {
        if (pendingScreenshotMode == 1) {
            server.on("/", []() {
                String html = "<!DOCTYPE html><html><body style='background:#1a1a1a;color:#fff;text-align:center;font-family:sans-serif;padding:50px;'>";
                html += "<h2>LolaCatMat Screenshot</h2><br><button onclick=\"window.location.href='/take'\" style='padding:20px;font-size:20px;background:#00A0FF;color:#fff;border:none;border-radius:10px;margin:10px;cursor:pointer;width:80%;max-width:300px;'>Foto Speichern</button><br><br><button onclick=\"window.location.href='/exit'\" style='padding:20px;font-size:20px;background:#AA0000;color:#fff;border:none;border-radius:10px;margin:10px;cursor:pointer;width:80%;max-width:300px;'>Beenden</button></body></html>";
                server.send(200, "text/html", html);
            });
            server.on("/take", []() {
                lvgl_port_lock(portMAX_DELAY); 
                lv_draw_buf_t * snapshot = lv_snapshot_take(lv_scr_act(), LV_COLOR_FORMAT_RGB565); 
                lvgl_port_unlock();
                if (snapshot == NULL) { server.send(500, "text/plain", "Fehler: PSRAM!"); return; }
                uint32_t w = snapshot->header.w; uint32_t h = snapshot->header.h; uint32_t imageSize = snapshot->data_size; uint32_t fileSize = 66 + imageSize; int32_t negH = -(int32_t)h; 
                uint8_t bmpHeader[66] = { 0x42, 0x4D, (uint8_t)(fileSize), (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24), 0, 0, 0, 0, 66, 0, 0, 0, 40, 0, 0, 0, (uint8_t)(w), (uint8_t)(w >> 8), (uint8_t)(w >> 16), (uint8_t)(w >> 24), (uint8_t)(negH), (uint8_t)(negH >> 8), (uint8_t)(negH >> 16), (uint8_t)(negH >> 24), 1, 0, 16, 0, 3, 0, 0, 0, (uint8_t)(imageSize), (uint8_t)(imageSize >> 8), (uint8_t)(imageSize >> 16), (uint8_t)(imageSize >> 24), 0x13, 0x0B, 0, 0, 0x13, 0x0B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0xF8, 0x00, 0x00, 0xE0, 0x07, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
                server.sendHeader("Content-Disposition", "attachment; filename=\"LolaCatMat_Screenshot.bmp\""); server.setContentLength(fileSize); server.send(200, "image/bmp", ""); server.sendContent((const char*)bmpHeader, 66); server.sendContent((const char*)snapshot->data, imageSize);
                lvgl_port_lock(portMAX_DELAY); 
                lv_snapshot_free((lv_image_dsc_t *)snapshot); 
                lvgl_port_unlock();
            });
            server.on("/exit", []() { server.send(200, "text/html", "<body style='background:#1a1a1a;color:#fff;text-align:center;font-family:sans-serif;padding:50px;'><h2>Beendet.</h2></body>"); pendingScreenshotMode = 2; });
            server.begin(); screenshotModeActive = true; pendingScreenshotMode = 0;
        } else if (pendingScreenshotMode == 2) { server.stop(); screenshotModeActive = false; pendingScreenshotMode = 0; }
        
        if (pendingWebSetupMode > 0) {
            webSetupStartTime = millis(); matEnabled = false; kippyEnabled = false;
            if (connected) { intentionalDisconnect = true; if(pClient) pClient->disconnect(); }
            if (pBLEScan != nullptr && pBLEScan->isScanning()) pBLEScan->stop(); delay(150); BLEDevice::deinit(true); delay(150); 
            if (pendingWebSetupMode == 1) { WiFi.disconnect(true, true); delay(100); WiFi.mode(WIFI_AP); WiFi.setTxPower(WIFI_POWER_8_5dBm); uint32_t randNum = esp_random() % 90000000 + 10000000; apPassword = String(randNum); WiFi.softAP("LolaCatMat-Setup", apPassword.c_str()); delay(500); dnsServer.start(53, "*", WiFi.softAPIP()); } 
            else if (pendingWebSetupMode == 2) { if (WiFi.status() == WL_CONNECTED) MDNS.begin("lolacatmat"); }
            server.on("/", handleRoot); server.on("/save", HTTP_POST, handleSave); server.onNotFound(handleCaptivePortal); server.begin();
            webSetupMode = pendingWebSetupMode; pendingWebSetupMode = 0; 
        }
        
        if (webSetupMode > 0 || screenshotModeActive) { if (webSetupMode == 1) dnsServer.processNextRequest(); server.handleClient(); if (webSetupMode > 0 && (millis() - webSetupStartTime > 300000)) { ESP.restart(); } vTaskDelay(20 / portTICK_PERIOD_MS); continue; }
        
        if (mqttBroker.length() >= 4 && wifiEnabled && wifiStarted && WiFi.status() == WL_CONNECTED && !isSetupScanning && effPrioWifi > 0) {
            if (!timeSynced) { configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov"); timeSynced = true; }
            IPAddress brokerIP; if (brokerIP.fromString(mqttBroker)) { mqttClient.setServer(brokerIP, mqttPort); } else { mqttClient.setServer(mqttBroker.c_str(), mqttPort); }
            mqttClient.setCallback(mqttCallback); 
            if (!mqttClient.connected()) {
                String clientId = "LolaCatMat-" + String(random(0xffff), HEX); bool success = false;
                if (mqttUser.length() > 0) { success = mqttClient.connect(clientId.c_str(), mqttUser.c_str(), mqttPass.c_str()); } else { success = mqttClient.connect(clientId.c_str()); }
                if (success) { 
                    publishAutoDiscovery(); 
                    mqttClient.subscribe(mqtt_topic_alarm_cmd); 
                    mqttClient.subscribe(mqtt_topic_disconnect_cmd); 
                    mqttClient.subscribe(mqtt_topic_kippy_status); 
                    mqttClient.subscribe(mqttBabyTopic.c_str()); 
                    mqttClient.subscribe("lolacatmat/baby/alarm/set"); 
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
                if (millis() - lastGetTime > syncDelay) { lastGetTime = millis(); mqttClient.publish(mqtt_topic_battery, String(batteryPercent).c_str(), true); if (connected) { mqttClient.publish(mqtt_topic_rssi, String(pClient->getRssi()).c_str(), true); } }
            }
        }
        vTaskDelay(50 / portTICK_PERIOD_MS); 
    }
}

void connectTask(void* param) {
    if (isSetupScanning) { connectTaskHandle = NULL; vTaskDelete(NULL); return; }
    if (pClient->connect(BLEAddress(savedMatMac.c_str()))) { BLERemoteService* pSvc = pClient->getService(serviceUUID); if (pSvc) { BLERemoteCharacteristic* pChar = pSvc->getCharacteristic(charUUID); if (pChar && pChar->canNotify()) { pChar->registerForNotify(notifyCallback); } else { intentionalDisconnect = true; pClient->disconnect(); } } else { intentionalDisconnect = true; pClient->disconnect(); } }
    connectTaskHandle = NULL; vTaskDelete(NULL);
}

void SystemLogic_Init() {
    bleMutex = xSemaphoreCreateMutex(); Audio_Init(); BLEDevice::init(""); pClient = BLEDevice::createClient(); pClient->setClientCallbacks(new MyClientCallbacks()); pBLEScan = BLEDevice::getScan(); pBLEScan->setAdvertisedDeviceCallbacks(new MyScanCallbacks()); pBLEScan->setActiveScan(true); mqttClient.setBufferSize(1024); 
    xTaskCreatePinnedToCore(mqttSyncTask, "MQTTTask", 8192, NULL, 1, NULL, 0); 
    xTaskCreatePinnedToCore(videoTask, "VideoTask", 8192, NULL, 2, NULL, 1); 
    
    if (wifiSsid == "") { pendingWebSetupMode = 1; } 
    preferences.begin("catmat", true); 
    haIP = preferences.getString("haIP", SECRET_HA_IP); 
    haPort = preferences.getInt("haPort", SECRET_HA_PORT); 
    camEntity = preferences.getString("camEntity", SECRET_CAM_SNAPSHOT_PATH); 
    mqttBabyTopic = preferences.getString("mqttBaby", SECRET_MQTT_BABY_TOPIC); 
    mqttCameraTriggerTopic = preferences.getString("mqttCamTrig", SECRET_MQTT_CAM_TRIGGER);
    
    cameraRefreshMs = preferences.getInt("camRef", 300); 
    preferences.end();
}

void SystemLogic_Update() {
    calcMultiplex(); if (webSetupMode > 0 || pendingWebSetupMode > 0) return; 
    if (isSetupScanning) { if (millis() - setupScanStartTime > 45000) { isSetupScanning = false; if (pBLEScan->isScanning()) pBLEScan->stop(); } else { if (!pBLEScan->isScanning() && !scanJustFinished) { if (connectTaskHandle != NULL) { return; } if (connected) { intentionalDisconnect = true; pClient->disconnect(); } if (wifiStarted) { WiFi.disconnect(true, false); wifiStarted = false; } pBLEScan->setInterval(200); pBLEScan->setWindow(100); pBLEScan->start(5, scanEndedCB, false); } } static uint32_t lastUiStringUpdate = 0; if (millis() - lastUiStringUpdate > 1000) { lastUiStringUpdate = millis(); if (bleMutex != NULL && xSemaphoreTake(bleMutex, pdMS_TO_TICKS(30)) == pdTRUE) { String options = ""; for(int i=0; i<scanResultCount; i++) { String m = scanResultMacs[i]; String n = scanResultNames[i]; if (n.length() > 14) n = n.substring(0, 12) + ".."; String entry = m + " | " + n + " | " + String(scanResultRssi[i]); options += entry; if (i < scanResultCount - 1) options += "\n"; } if (scanResultCount == 0) options = "Suche laeuft..."; strncpy(scanOptionsStr, options.c_str(), sizeof(scanOptionsStr) - 1); scanOptionsStr[sizeof(scanOptionsStr) - 1] = '\0'; xSemaphoreGive(bleMutex); requestRollerUpdate = true; } } if (scanJustFinished) scanJustFinished = false; return; }
    if (!wifiEnabled && wifiStarted) { WiFi.disconnect(true, false); wifiStarted = false; timeSynced = false; }
    if (wifiEnabled && !wifiStarted && !isTrackerMode && effPrioWifi > 0) { WiFi.setTxPower(WIFI_POWER_8_5dBm); WiFi.begin(wifiSsid.c_str(), wifiPass.c_str()); wifiStarted = true; }
    if (!matEnabled && connected) { intentionalDisconnect = true; pClient->disconnect(); }
    if (!kippyEnabled && pBLEScan->isScanning() && !isTrackerMode) { pBLEScan->stop(); pBLEScan->clearResults(); }
    if (!wifiEnabled && !matEnabled && kippyEnabled && !isTrackerMode) { isTrackerMode = true; intentionalDisconnect = true; radarSetupPhase = 1; storedGraphMode = currentGraphMode; currentGraphMode = GRAPH_MODE_KIPPY_RSSI; historyIdx = 0; historyCount = 0; for(int i=0; i<HISTORY_SIZE; i++) pressureHistory[i] = -32000; requestChartUpdate = true; } else if ((wifiEnabled || matEnabled) && isTrackerMode) { isTrackerMode = false; pendingRadarTeardown = true; currentGraphMode = storedGraphMode; historyIdx = 0; historyCount = 0; for(int i=0; i<HISTORY_SIZE; i++) pressureHistory[i] = -32000; requestChartUpdate = true; }
    static uint32_t stateWaitTimer = 0; if (radarSetupPhase == 1) { if (connected) { pClient->disconnect(); } stateWaitTimer = millis(); radarSetupPhase = 2; } else if (radarSetupPhase == 2) { if (!connected || (millis() - stateWaitTimer > 2000)) { connected = false; radarSetupPhase = 3; } } else if (radarSetupPhase == 3) { if (wifiStarted) { WiFi.disconnect(true, false); wifiStarted = false; } if (pBLEScan->isScanning()) pBLEScan->stop(); pBLEScan->clearResults(); pBLEScan->setInterval(100); pBLEScan->setWindow(99); pBLEScan->start(0, scanEndedCB, false); radarSetupPhase = 0; }
    if (pendingRadarTeardown) { pendingRadarTeardown = false; if (pBLEScan->isScanning()) pBLEScan->stop(); pBLEScan->clearResults(); if (wifiEnabled) { WiFi.setTxPower(WIFI_POWER_8_5dBm); WiFi.begin(wifiSsid.c_str(), wifiPass.c_str()); wifiStarted = true; } }
    if (pendingBleReconnect) { if (connected) { pClient->disconnect(); } pendingBleReconnect = false; }
    if (connected && matEnabled && !isTrackerMode) { static uint32_t lastRssiCheck = 0; if (millis() - lastRssiCheck > 2000) { lastRssiCheck = millis(); int currentRssi = pClient->getRssi(); static int zeroCount = 0; if (currentRssi == 0) { rssiIsZero = true; zeroCount++; if (zeroCount >= 3) { executeDisconnectLogic(false); pClient->disconnect(); zeroCount = 0; } } else { rssiIsZero = false; zeroCount = 0; } } }
    if (!isSetupScanning && !connected && matEnabled && !isTrackerMode && effPrioMat > 0) { static uint32_t lastTry = 0; if (millis() - lastTry > 5000 && connectTaskHandle == NULL) { lastTry = millis(); if (pBLEScan->isScanning()) { pBLEScan->stop(); delay(150); } xTaskCreate(connectTask, "ConnectTask", 4096, NULL, 1, &connectTaskHandle); } }
    if (radarSetupPhase == 0 && !pendingRadarTeardown) { if (isTrackerMode) { static uint32_t lastRadarRestart = 0; if (millis() - lastRadarRestart > 2000) { lastRadarRestart = millis(); if (pBLEScan->isScanning()) pBLEScan->stop(); pBLEScan->clearResults(); pBLEScan->start(0, scanEndedCB, false); } } else if (kippyEnabled && effPrioKippy > 0 && !isSetupScanning) { if (connected && (millis() - lastConnectTime < 5000)) { if (pBLEScan->isScanning()) pBLEScan->stop(); } else { static uint32_t bgScanTimer = millis(); static bool isBgScanning = false; uint32_t activeTimeMs = (effPrioKippy * 10000) / 100.0; uint32_t idleTimeMs = 10000 - activeTimeMs; if (isBgScanning) { if (millis() - bgScanTimer > activeTimeMs) { pBLEScan->stop(); pBLEScan->clearResults(); isBgScanning = false; bgScanTimer = millis(); } } else { if (millis() - bgScanTimer > idleTimeMs) { pBLEScan->setInterval(100); pBLEScan->setWindow(99); pBLEScan->start(0, scanEndedCB, false); isBgScanning = true; bgScanTimer = millis(); } } } } }
    
    if (!muted && !isTrackerMode) { 
        static uint32_t lastBeep = 0; 
        if (alarmActive) { 
            if (millis() - lastBeep > 2500) { 
                playCatAlarmI2S(); 
                lastBeep = millis(); 
            } 
        } else if (disconnectAlarmActive) { 
            if (millis() - lastBeep > 3000) { 
                playToneI2S(440, 200, false); 
                playToneI2S(349, 200, false); 
                playToneI2S(261, 500, false); 
                lastBeep = millis(); 
            } 
        } else if (babyAlarmActive && !babyMuted) {
            if (millis() - lastBeep > 2500) { 
                playBabyAlarmI2S(); 
                lastBeep = millis(); 
            }
        }
    }
    
    static uint32_t lastBatRead = 0; if (millis() - lastBatRead > 5000) { int mv = analogReadMilliVolts(BATTERY_ADC_PIN); batteryPercent = (int)(((mv * 3.0) / 1000.0 - 3.2) * 100.0); lastBatRead = millis(); }
    static uint32_t lastHistoryUpdate = 0; if (millis() - lastHistoryUpdate >= 1000) { lastHistoryUpdate = millis(); int32_t valToPush = -32000; if (isTrackerMode) { valToPush = (millis() - lastCatSeenTime < 5000) ? catRssi : -32000; } else { switch(currentGraphMode) { case GRAPH_MODE_PRESSURE: if (connected) { valToPush = (intervalMaxPressure != -32000) ? intervalMaxPressure : currentPressure; intervalMaxPressure = -32000; } break; case GRAPH_MODE_BLE_RSSI: valToPush = connected ? pClient->getRssi() : -32000; break; case GRAPH_MODE_WLAN_RSSI: valToPush = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -32000; break; case GRAPH_MODE_BLE_INTERVAL: valToPush = connected ? (int32_t)avgInterval : -32000; break; case GRAPH_MODE_KIPPY_RSSI: valToPush = (millis() - lastCatSeenTime < 5000) ? catRssi : -32000; break; } } pressureHistory[historyIdx] = valToPush; historyIdx = (historyIdx + 1) % HISTORY_SIZE; if (historyCount < HISTORY_SIZE) historyCount++; requestChartUpdate = true; }
}