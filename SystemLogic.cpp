#pragma GCC optimize ("O3") 
#include "SystemLogic.h"
#include "SharedData.h"
#include "VideoLogic.h"      
#include "MqttLogic.h"       
#include "WebSetupLogic.h"   
#include "BleLogic.h"        
#include <WiFi.h>
#include <esp_wifi.h> 
#include "secrets.h"         
#include "GuiManager.h"      

volatile bool isStreamActive = false; 
int cameraRefreshMs = 300; 

void SystemLogic_Init() {
    esp_wifi_set_ps(WIFI_PS_NONE);

    Audio_Init(); 
    BleLogic_Init();     
    MqttLogic_Init(); 
    WebSetupLogic_Init(); 
    
    if (wifiSsid == "") { pendingWebSetupMode = 1; } 
    preferences.begin("catmat", true); 
    haIP = preferences.getString("haIP", SECRET_HA_IP); 
    haPort = preferences.getInt("haPort", SECRET_HA_PORT); 
    camEntity = preferences.getString("camEntity", SECRET_CAM_SNAPSHOT_PATH); 
    mqttBabyTopic = preferences.getString("mqttBaby", SECRET_MQTT_BABY_TOPIC); 
    mqttCameraTriggerTopic = preferences.getString("mqttCamTrig", SECRET_MQTT_CAM_TRIGGER);
    cameraRefreshMs = preferences.getInt("camRef", 300); 
    showFps = preferences.getBool("showFps", false);
    preferences.end();
}

void SystemLogic_Update() {
    static ScreenID lastScreen = SCREEN_DASHBOARD;
    ScreenID currentScreen = gui.getCurrentScreen();
    
    // --- FIX: NUR BEENDEN BEIM VERLASSEN ---
    // Wenn du den Babyscreen verlässt, wird der RAM restlos freigegeben.
    if (currentScreen != lastScreen) {
        if (lastScreen == SCREEN_BABY) { 
            VideoLogic_Stop(); 
        }
        lastScreen = currentScreen;
    }

    // --- FIX: START NUR AUF KOMMANDO ---
    // Startet die Task aus dem Nichts, sobald dein Play-Button isStreamActive auf true setzt!
    if (isStreamActive) {
        VideoLogic_Start();
    }

    WebSetupLogic_Update(); 

    calcMultiplex(); 
    if (webSetupMode > 0 || pendingWebSetupMode > 0) return; 

    if (BleLogic_Update()) return;

    if (!wifiEnabled && wifiStarted) { WiFi.disconnect(true, false); wifiStarted = false; timeSynced = false; }
    
    if (wifiEnabled && !wifiStarted && !isTrackerMode && effPrioWifi > 0) { 
        WiFi.setTxPower(WIFI_POWER_19_5dBm); 
        WiFi.begin(wifiSsid.c_str(), wifiPass.c_str()); 
        wifiStarted = true; 
    }
    
    if (!muted && !isTrackerMode) { 
        static uint32_t lastBeep = 0; 
        if (alarmActive) { 
            if (millis() - lastBeep > 2500) { playCatAlarmI2S(); lastBeep = millis(); } 
        } else if (disconnectAlarmActive) { 
            if (millis() - lastBeep > 3000) { playToneI2S(440, 200, false); playToneI2S(349, 200, false); playToneI2S(261, 500, false); lastBeep = millis(); } 
        } else if (babyAlarmActive && !babyMuted) {
            if (millis() - lastBeep > 2500) { playBabyAlarmI2S(); lastBeep = millis(); }
        }
    }
    
    static uint32_t lastBatRead = 0; 
    if (millis() - lastBatRead > 5000) { 
        int mv = analogReadMilliVolts(BATTERY_ADC_PIN); 
        batteryPercent = (int)(((mv * 3.0) / 1000.0 - 3.2) * 100.0); 
        lastBatRead = millis(); 
    }
    
    static uint32_t lastHistoryUpdate = 0; 
    if (millis() - lastHistoryUpdate >= 1000) { 
        lastHistoryUpdate = millis(); 
        int32_t valToPush = -32000; 
        if (isTrackerMode) { valToPush = (millis() - lastCatSeenTime < 5000) ? catRssi : -32000; } 
        else { 
            switch(currentGraphMode) { 
                case GRAPH_MODE_PRESSURE: if (connected) { valToPush = (intervalMaxPressure != -32000) ? intervalMaxPressure : currentPressure; intervalMaxPressure = -32000; } break; 
                case GRAPH_MODE_BLE_RSSI: valToPush = connected ? pClient->getRssi() : -32000; break; 
                case GRAPH_MODE_WLAN_RSSI: valToPush = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -32000; break; 
                case GRAPH_MODE_BLE_INTERVAL: valToPush = connected ? (int32_t)avgInterval : -32000; break; 
                case GRAPH_MODE_KIPPY_RSSI: valToPush = (millis() - lastCatSeenTime < 5000) ? catRssi : -32000; break; 
            } 
        } 
        pressureHistory[historyIdx] = valToPush; historyIdx = (historyIdx + 1) % HISTORY_SIZE; if (historyCount < HISTORY_SIZE) historyCount++; requestChartUpdate = true; 
    }
}