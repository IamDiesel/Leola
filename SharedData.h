#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h> 
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <WebServer.h>   
#include <DNSServer.h>   
#include <ESPmDNS.h>     

#define BATTERY_ADC_PIN 8
#define I2S_BCLK       48
#define I2S_LRCK       38
#define I2S_DOUT       47
#define I2S_NUM        I2S_NUM_0

#define HISTORY_SIZE 900 
#define WINDOW_SIZE 12
#define INTERVAL_HISTORY_SIZE 5
#define MAX_SCAN_DEVICES 40 
#define MAX_BT_MSGS 5

enum GraphMode { GRAPH_MODE_PRESSURE = 0, GRAPH_MODE_BLE_RSSI, GRAPH_MODE_WLAN_RSSI, GRAPH_MODE_BLE_INTERVAL, GRAPH_MODE_KIPPY_RSSI };

// ANGEPASST: soundType hinzugefügt (0=UI, 1=Baby, 2=Cat)
struct AudioMsg { uint16_t freq; uint32_t duration; bool isUiSound; uint8_t soundType; };

// Debug Logging Variablen
extern bool audioDebugEnabled;
extern String audioLogs[10];
extern int audioLogIdx;
extern void addAudioLog(String msg);

extern bool babyAlarmActive;
extern bool babyMuted;

extern QueueHandle_t audioQueue;
extern uint8_t lastBtPackets[MAX_BT_MSGS][10];
extern int btPacketIdx;
extern int btPacketCount;

extern const char* serviceUUID;
extern const char* charUUID;

extern String wifiSsid;
extern String wifiPass;
extern String mqttBroker;
extern int mqttPort;
extern String mqttUser;
extern String mqttPass;
extern String haIP;
extern int haPort;
extern String camEntity;
extern String mqttBabyTopic;
extern String mqttCameraTriggerTopic;
extern String babyStreamUrl;

extern int webSetupMode; 
extern volatile int pendingWebSetupMode; 
extern uint32_t webSetupStartTime;
extern String apPassword; 

extern volatile int pendingScreenshotMode; 
extern volatile bool screenshotModeActive; 
extern volatile bool isBabyArmed;

extern WebServer server;
extern DNSServer dnsServer;

extern WiFiClient espClient;
extern PubSubClient mqttClient;

extern String savedMatMac;
extern String savedKippyMac;

extern bool isSetupScanning;
extern int setupScanMode; 
extern uint32_t setupScanStartTime; 
extern bool scanJustFinished;
extern char scanOptionsStr[2048];

extern String scanResultMacs[MAX_SCAN_DEVICES];
extern String scanResultNames[MAX_SCAN_DEVICES];
extern int scanResultRssi[MAX_SCAN_DEVICES];
extern int scanResultCount;

extern SemaphoreHandle_t bleMutex; 
extern volatile bool requestRollerUpdate; 

extern Preferences preferences;
extern BLEClient* pClient; 
extern BLEScan* pBLEScan;
extern TaskHandle_t connectTaskHandle; 

extern uint32_t lastCatSeenTime;
extern int catRssi;

extern bool wifiEnabled;
extern bool matEnabled;
extern bool kippyEnabled;
extern int prioMaster; 
extern int prioSlave;  
extern float effPrioMat;
extern float effPrioKippy;
extern float effPrioWifi;

extern bool isTrackerMode; 
extern bool isCatAtHome;        
extern bool isTrackerDataValid; 
extern int radarSetupPhase; 
extern bool pendingRadarTeardown;
extern bool pendingWifiDisconnect; 
extern bool isBooting; 

extern volatile bool requestWake;
extern volatile bool requestSleep;
extern volatile bool requestChartUpdate; 
extern volatile bool requestMainTab; 
extern bool force_auto_fit; 

extern bool connected;
extern uint32_t lastConnectTime; 
extern bool isArmed;                 
extern bool wasArmedBeforeDisconnect;
extern bool alarmActive;
extern bool muted;
extern bool wifiStarted;
extern bool timeSynced; 
extern uint32_t cooldownUntil;
extern uint32_t startTime;
extern uint32_t lastNotifyTime; 
extern int batteryPercent;
extern int autoHealedDisconnectCount; 

extern int32_t rawPressure;
extern int32_t taraOffset;
extern int32_t currentPressure;
extern int32_t currentAvg; 
extern int32_t intervalMaxPressure; 
extern int32_t pressureHistory[HISTORY_SIZE];
extern int historyIdx;
extern int historyCount;
extern int32_t pressWindow[WINDOW_SIZE];
extern int pWinIdx;
extern int pWinCount;

extern int volumePercent;
extern int streamVolumePercent;
extern int thresholdVal; 
extern bool isDarkMode; 
extern int graphTimeSeconds; 
extern bool showTimeOnX; 
extern int currentGraphMode;
extern int storedGraphMode; 
extern bool autoBleInterval;
extern int manualBleIntervalMs;
extern bool pendingBleReconnect; 
extern bool intentionalDisconnect; 
extern bool disconnectAlarmActive; 
extern bool rssiIsZero; 
extern volatile bool isBleTabActive; 
extern uint32_t intervalHistory[INTERVAL_HISTORY_SIZE];
extern int intervalIdx;
extern int intervalCount;
extern float avgInterval;
extern float stdDevInterval;
extern bool displayIsOff;
extern int brightnessPercent;
extern bool mqttEnabled;
extern volatile bool requestBabyStream;
extern volatile int babyStreamStatus; // 0=Aus, 1=Fehler/Laden, 2=Spielt
extern volatile bool vidFSMode; //video in fullscreen mode via DMA
extern volatile bool showFps;
extern volatile int currentFps;

extern int mjpegDropThreshold; // <--- NEU: HIER EINFÜGEN

void Data_Init();
void Audio_Init();
void calcMultiplex();
void playToneI2S(uint16_t freq, uint32_t duration_ms, bool isUiSound = false);

// NEU: Die spezifischen Alarm-Profile
void playBabyAlarmI2S();
void playCatAlarmI2S();

void fullReset();
void wakeDisplay();
void sleepDisplay();
void factoryReset();