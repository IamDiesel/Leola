#pragma GCC optimize ("O3") 
#include "WebSetupLogic.h"
#include "SharedData.h"
#include "VideoLogic.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "lvgl.h" 
#include <BLEDevice.h> 

volatile int pendingScreenshotMode = 0; 
volatile bool screenshotModeActive = false;

extern bool lvgl_port_lock(uint32_t timeout_ms);
extern void lvgl_port_unlock(void);

// --- FIX: DIE FORWARD DEKLARATION ---
static void handleScreenshotIndex();

static String getSetupHtml() {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0, user-scalable=no'>";
    html += "<title>LolaCatMat Setup</title>";
    html += "<style>body{background:#1a1a1a;color:#fff;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;padding:20px;text-align:center;}";
    html += "h2{color:#00A0FF;margin-bottom:5px;}p{color:#aaa;font-size:14px;margin-bottom:30px;}";
    html += "input,select{width:100%;padding:14px;margin:5px 0 20px 0;border-radius:8px;border:1px solid #333;background:#2a2a2a;color:#fff;font-size:16px;box-sizing:border-box;}";
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
    
    html += "<label>Audio Format</label><select name='pcm'>";
    html += "<option value='0'" + String(!usePcmAudio ? " selected" : "") + ">AAC (Kompression)</option>";
    html += "<option value='1'" + String(usePcmAudio ? " selected" : "") + ">PCM S16 LE / WAV (CPU schonend)</option>";
    html += "</select>";
    html += "<label>Kamera Aufloesung (Hack)</label><select name='camHackM'>";
    html += "<option value='0'" + String(camHackMode == 0 ? " selected" : "") + ">0: Deaktiviert (Normaler Stream)</option>";
    html += "<option value='1'" + String(camHackMode == 1 ? " selected" : "") + ">1: 320x240</option>";
    html += "<option value='2'" + String(camHackMode == 2 ? " selected" : "") + ">2: 480x360 (16:9)</option>";
    html += "<option value='3'" + String(camHackMode == 3 ? " selected" : "") + ">3: 640x360</option>";
    html += "<option value='4'" + String(camHackMode == 4 ? " selected" : "") + ">4: 640x480</option>";
    html += "<option value='5'" + String(camHackMode == 5 ? " selected" : "") + ">5: 800x450</option>";
    html += "<option value='6'" + String(camHackMode == 6 ? " selected" : "") + ">6: 800x600</option>";
    html += "<option value='7'" + String(camHackMode == 7 ? " selected" : "") + ">7: 1024x768</option>";
    html += "<option value='8'" + String(camHackMode == 8 ? " selected" : "") + ">8: 1280x720 (HD)</option>";
    html += "<option value='9'" + String(camHackMode == 9 ? " selected" : "") + ">9: 1280x960</option>";
    html += "</select>";
    html += "<h3>Home Assistant & Fallbacks</h3>";
    html += "<label>Home Assistant IP-Adresse</label><input type='text' name='haIP' placeholder='z.B. 192.168.1.100' value='" + haIP + "'>";
    html += "<label>Home Assistant Port (Standard: 8123)</label><input type='number' name='haPort' placeholder='8123' value='" + String(haPort) + "'>";
    html += "<label>Snapshot Trigger Topic (MQTT)</label><input type='text' name='mqttCamTrig' placeholder='camera/trigger/snapshot' value='" + mqttCameraTriggerTopic + "'>";
    html += "<label>Baby Alarm Ausloeser (MQTT Topic)</label><input type='text' name='mqttBaby' placeholder='smartmat/baby/cry/state' value='" + mqttBabyTopic + "'>";
    html += "<button type='submit'>Speichern & Neustart</button></form></body></html>";
    return html;
}

static void handleRoot() { 
    if (screenshotModeActive) { handleScreenshotIndex(); } 
    else { server.send(200, "text/html", getSetupHtml()); }
}

static String sanitizeString(String input) { input.trim(); input.replace("\xE2\x80\x8B", ""); input.replace("\xC2\xA0", ""); return input; }

static void handleSave() {
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
    if (server.hasArg("pcm")) { usePcmAudio = server.arg("pcm").toInt() == 1; preferences.putBool("usePcm", usePcmAudio); }
    if (server.hasArg("camHackM")) { camHackMode = server.arg("camHackM").toInt(); preferences.putInt("camHackM", camHackMode); }
    preferences.end();
    
    String successHtml = "<!DOCTYPE html><html><body style='background:#1a1a1a;color:#fff;text-align:center;padding:50px;font-family:sans-serif;'>";
    successHtml += "<h2 style='color:#00FF00;'>Erfolgreich gespeichert!</h2><p>Die LolaCatMat startet jetzt neu und verbindet sich.</p></body></html>";
    server.send(200, "text/html", successHtml);
    delay(2000); ESP.restart(); 
}

static void handleCaptivePortal() { server.sendHeader("Location", String("http://") + server.client().localIP().toString(), true); server.send(302, "text/plain", ""); }

static void handleScreenshotIndex() {
    String html = "<!DOCTYPE html><html><body style='background:#1a1a1a;color:#fff;text-align:center;font-family:sans-serif;padding:50px;'>";
    html += "<h2>LolaCatMat Screenshot</h2><br><button onclick=\"window.location.href='/take'\" style='padding:20px;font-size:20px;background:#00A0FF;color:#fff;border:none;border-radius:10px;margin:10px;cursor:pointer;width:80%;max-width:300px;'>Foto Speichern</button><br><br><button onclick=\"window.location.href='/exit'\" style='padding:20px;font-size:20px;background:#AA0000;color:#fff;border:none;border-radius:10px;margin:10px;cursor:pointer;width:80%;max-width:300px;'>Beenden</button></body></html>";
    server.send(200, "text/html", html);
}

static void handleScreenshotTake() {
    lvgl_port_lock(portMAX_DELAY); lv_draw_buf_t * snapshot = lv_snapshot_take(lv_scr_act(), LV_COLOR_FORMAT_RGB565); lvgl_port_unlock();
    if (snapshot == NULL) { server.send(500, "text/plain", "Fehler: PSRAM!"); return; }
    uint32_t w = snapshot->header.w; uint32_t h = snapshot->header.h; uint32_t imageSize = snapshot->data_size; uint32_t fileSize = 66 + imageSize; int32_t negH = -(int32_t)h; 
    uint8_t bmpHeader[66] = { 0x42, 0x4D, (uint8_t)(fileSize), (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24), 0, 0, 0, 0, 66, 0, 0, 0, 40, 0, 0, 0, (uint8_t)(w), (uint8_t)(w >> 8), (uint8_t)(w >> 16), (uint8_t)(w >> 24), (uint8_t)(negH), (uint8_t)(negH >> 8), (uint8_t)(negH >> 16), (uint8_t)(negH >> 24), 1, 0, 16, 0, 3, 0, 0, 0, (uint8_t)(imageSize), (uint8_t)(imageSize >> 8), (uint8_t)(imageSize >> 16), (uint8_t)(imageSize >> 24), 0x13, 0x0B, 0, 0, 0x13, 0x0B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0xF8, 0x00, 0x00, 0xE0, 0x07, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
    server.sendHeader("Content-Disposition", "attachment; filename=\"LolaCatMat_Screenshot.bmp\""); server.setContentLength(fileSize); server.send(200, "image/bmp", ""); server.sendContent((const char*)bmpHeader, 66); server.sendContent((const char*)snapshot->data, imageSize);
    lvgl_port_lock(portMAX_DELAY); lv_snapshot_free((lv_image_dsc_t *)snapshot); lvgl_port_unlock();
}

static void handleScreenshotExit() {
    server.send(200, "text/html", "<body style='background:#1a1a1a;color:#fff;text-align:center;font-family:sans-serif;padding:50px;'><h2>Beendet. Das Kamerabild auf der CatMat laeuft jetzt wieder im Turbo-Modus.</h2></body>"); 
    pendingScreenshotMode = 2; 
}

void WebSetupLogic_Init() {
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleCaptivePortal);
    server.on("/take", handleScreenshotTake);
    server.on("/exit", handleScreenshotExit);
    server.on("/screenshot", handleScreenshotIndex);
}

void WebSetupLogic_Update() {
    if (pendingScreenshotMode == 1) {
        server.begin(); 
        screenshotModeActive = true; 
        pendingScreenshotMode = 0;
    } 
    else if (pendingScreenshotMode == 2) { 
        server.stop(); 
        screenshotModeActive = false; 
        pendingScreenshotMode = 0; 
    }
    else if (pendingWebSetupMode > 0) {
        webSetupMode = pendingWebSetupMode; 
        pendingWebSetupMode = 0;
        
        webSetupStartTime = millis(); 
        matEnabled = false; 
        kippyEnabled = false;
        
        isStreamActive = false; 
        VideoLogic_Stop(); 
        
        if (connected && pClient != nullptr) { 
            intentionalDisconnect = true; 
            pClient->disconnect(); 
            uint32_t waitBle = millis();
            while (connected && (millis() - waitBle < 2000)) { delay(20); }
        }
        connected = false;
        
        if (pBLEScan != nullptr) {
            if (pBLEScan->isScanning()) pBLEScan->stop(); 
            pBLEScan->clearResults();
        }
        
        delay(200); 
        BLEDevice::deinit(true); 
        delay(200); 
        
        if (webSetupMode == 1) { 
            WiFi.disconnect(true, true); delay(100); 
            WiFi.mode(WIFI_OFF); delay(100);
            WiFi.mode(WIFI_AP); WiFi.setTxPower(WIFI_POWER_19_5dBm); 
            WiFi.softAP("LolaCatMat-Setup", apPassword.c_str()); delay(500); 
            dnsServer.start(53, "*", WiFi.softAPIP()); 
        } else if (webSetupMode == 2) { 
            if (WiFi.status() == WL_CONNECTED) { 
                MDNS.begin("lolacatmat"); 
            } 
        }
        
        server.begin(); 
    }

    if (webSetupMode > 0 || screenshotModeActive) { 
        if (webSetupMode == 1) dnsServer.processNextRequest(); 
        server.handleClient(); 
        if (webSetupMode > 0 && (millis() - webSetupStartTime > 300000)) { ESP.restart(); } 
    }
}