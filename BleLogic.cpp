#pragma GCC optimize ("O3") 
#include "BleLogic.h"
#include "SharedData.h"
#include <WiFi.h>

// --- CALLBACKS ---
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

void connectTask(void* param) {
    if (isSetupScanning) { connectTaskHandle = NULL; vTaskDelete(NULL); return; }
    
    if (pClient->connect(BLEAddress(savedMatMac.c_str()))) { 
        BLERemoteService* pSvc = pClient->getService(serviceUUID); 
        if (pSvc) { 
            BLERemoteCharacteristic* pChar = pSvc->getCharacteristic(charUUID); 
            if (pChar && pChar->canNotify()) { 
                pChar->registerForNotify(notifyCallback); 
            } else { intentionalDisconnect = true; pClient->disconnect(); } 
        } else { intentionalDisconnect = true; pClient->disconnect(); } 
    }
    connectTaskHandle = NULL; vTaskDelete(NULL);
}

// --- INIT ---
void BleLogic_Init() {
    bleMutex = xSemaphoreCreateMutex(); 
    BLEDevice::init(""); 
    pClient = BLEDevice::createClient(); 
    pClient->setClientCallbacks(new MyClientCallbacks()); 
    pBLEScan = BLEDevice::getScan(); 
    pBLEScan->setAdvertisedDeviceCallbacks(new MyScanCallbacks()); 
    pBLEScan->setActiveScan(true); 
}

// --- UPDATE LOOP ---
bool BleLogic_Update() {
    if (effPrioKippy == 0 && pBLEScan->isScanning()) {
        pBLEScan->stop();
        pBLEScan->clearResults();
    }

    if (isSetupScanning) { 
        if (millis() - setupScanStartTime > 45000) { 
            isSetupScanning = false; 
            if (pBLEScan->isScanning()) pBLEScan->stop(); 
        } else { 
            if (!pBLEScan->isScanning() && !scanJustFinished) { 
                if (connectTaskHandle != NULL) { return true; } 
                if (connected) { intentionalDisconnect = true; pClient->disconnect(); } 
                if (wifiStarted) { WiFi.disconnect(true, false); wifiStarted = false; } 
                pBLEScan->setInterval(200); pBLEScan->setWindow(100); pBLEScan->start(5, scanEndedCB, false); 
            } 
        } 
        static uint32_t lastUiStringUpdate = 0; 
        if (millis() - lastUiStringUpdate > 1000) { 
            lastUiStringUpdate = millis(); 
            if (bleMutex != NULL && xSemaphoreTake(bleMutex, pdMS_TO_TICKS(30)) == pdTRUE) { 
                String options = ""; 
                for(int i=0; i<scanResultCount; i++) { 
                    String m = scanResultMacs[i]; String n = scanResultNames[i]; 
                    if (n.length() > 14) n = n.substring(0, 12) + ".."; 
                    String entry = m + " | " + n + " | " + String(scanResultRssi[i]); 
                    options += entry; if (i < scanResultCount - 1) options += "\n"; 
                } 
                if (scanResultCount == 0) options = "Suche laeuft..."; 
                strncpy(scanOptionsStr, options.c_str(), sizeof(scanOptionsStr) - 1); 
                scanOptionsStr[sizeof(scanOptionsStr) - 1] = '\0'; 
                xSemaphoreGive(bleMutex); 
                requestRollerUpdate = true; 
            } 
        } 
        if (scanJustFinished) scanJustFinished = false; 
        return true; // Exakt wie das alte "return;" im Monolithen!
    }
    
    if (!matEnabled && connected) { intentionalDisconnect = true; pClient->disconnect(); }
    if (!kippyEnabled && pBLEScan->isScanning() && !isTrackerMode) { pBLEScan->stop(); pBLEScan->clearResults(); }
    
    if (!wifiEnabled && !matEnabled && kippyEnabled && !isTrackerMode) { 
        isTrackerMode = true; intentionalDisconnect = true; radarSetupPhase = 1; 
        storedGraphMode = currentGraphMode; currentGraphMode = GRAPH_MODE_KIPPY_RSSI; 
        historyIdx = 0; historyCount = 0; 
        for(int i=0; i<HISTORY_SIZE; i++) pressureHistory[i] = -32000; 
        requestChartUpdate = true; 
    } else if ((wifiEnabled || matEnabled) && isTrackerMode) { 
        isTrackerMode = false; pendingRadarTeardown = true; 
        currentGraphMode = storedGraphMode; historyIdx = 0; historyCount = 0; 
        for(int i=0; i<HISTORY_SIZE; i++) pressureHistory[i] = -32000; 
        requestChartUpdate = true; 
    }
    
    static uint32_t stateWaitTimer = 0; 
    if (radarSetupPhase == 1) { 
        if (connected) { pClient->disconnect(); } stateWaitTimer = millis(); radarSetupPhase = 2; 
    } else if (radarSetupPhase == 2) { 
        if (!connected || (millis() - stateWaitTimer > 2000)) { connected = false; radarSetupPhase = 3; } 
    } else if (radarSetupPhase == 3) { 
        if (wifiStarted) { WiFi.disconnect(true, false); wifiStarted = false; } 
        if (pBLEScan->isScanning()) pBLEScan->stop(); 
        pBLEScan->clearResults(); 
        pBLEScan->setInterval(100); 
        pBLEScan->setWindow(30); 
        pBLEScan->start(0, scanEndedCB, false); 
        radarSetupPhase = 0; 
    }
    
    if (pendingRadarTeardown) { 
        pendingRadarTeardown = false; 
        if (pBLEScan->isScanning()) pBLEScan->stop(); 
        pBLEScan->clearResults(); 
        if (wifiEnabled) { 
            WiFi.setTxPower(WIFI_POWER_19_5dBm); 
            WiFi.begin(wifiSsid.c_str(), wifiPass.c_str()); 
            wifiStarted = true; 
        } 
    }
    
    if (pendingBleReconnect) { if (connected) { pClient->disconnect(); } pendingBleReconnect = false; }
    
    if (connected && matEnabled && !isTrackerMode) { 
        static uint32_t lastRssiCheck = 0; 
        if (millis() - lastRssiCheck > 2000) { 
            lastRssiCheck = millis(); int currentRssi = pClient->getRssi(); static int zeroCount = 0; 
            if (currentRssi == 0) { rssiIsZero = true; zeroCount++; if (zeroCount >= 3) { executeDisconnectLogic(false); pClient->disconnect(); zeroCount = 0; } } else { rssiIsZero = false; zeroCount = 0; } 
        } 
    }
    
    if (!isSetupScanning && !connected && matEnabled && !isTrackerMode && effPrioMat > 0) { 
        static uint32_t lastTry = 0; 
        if (millis() - lastTry > 5000 && connectTaskHandle == NULL) { 
            lastTry = millis(); if (pBLEScan->isScanning()) { pBLEScan->stop(); delay(150); } 
            xTaskCreate(connectTask, "ConnectTask", 4096, NULL, 1, &connectTaskHandle); 
        } 
    }
    
    if (radarSetupPhase == 0 && !pendingRadarTeardown) { 
        if (isTrackerMode) { 
            static uint32_t lastRadarRestart = 0; 
            if (millis() - lastRadarRestart > 2000) { 
                lastRadarRestart = millis(); 
                if (pBLEScan->isScanning()) pBLEScan->stop(); 
                pBLEScan->clearResults(); 
                pBLEScan->start(0, scanEndedCB, false); 
            } 
        } else if (kippyEnabled && effPrioKippy > 0 && !isSetupScanning) { 
            if (connected && (millis() - lastConnectTime < 5000)) { 
                if (pBLEScan->isScanning()) pBLEScan->stop(); 
            } else { 
                static uint32_t bgScanTimer = millis(); 
                static bool isBgScanning = false; 
                uint32_t activeTimeMs = (effPrioKippy * 10000) / 100.0; 
                uint32_t idleTimeMs = 10000 - activeTimeMs; 
                if (isBgScanning) { 
                    if (millis() - bgScanTimer > activeTimeMs) { 
                        pBLEScan->stop(); pBLEScan->clearResults(); isBgScanning = false; bgScanTimer = millis(); 
                    } 
                } else { 
                    if (millis() - bgScanTimer > idleTimeMs) { 
                        pBLEScan->setInterval(100); pBLEScan->setWindow(30); pBLEScan->start(0, scanEndedCB, false); isBgScanning = true; bgScanTimer = millis(); 
                    } 
                } 
            } 
        } 
    }
    return false; // Alles okay, SystemLogic darf weiterarbeiten!
}