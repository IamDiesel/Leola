#include "SharedData.h"
#include "secrets.h"
#include "SystemLogic.h" 
#include "GuiManager.h"  
#include <WiFi.h>
#include <math.h>

#include "AudioOutputI2S.h"
#include "AudioGeneratorAAC.h"
#include "AudioGeneratorWAV.h"
#include "AudioFileSourceHTTPStream.h"
#include "AudioFileSourceBuffer.h" 

// Zugriff auf den Video-Status fuer das Multiplexing
extern volatile bool isStreamActive; 

Preferences preferences;
BLEClient* pClient = nullptr; 
BLEScan* pBLEScan = nullptr;
TaskHandle_t connectTaskHandle = NULL;

QueueHandle_t audioQueue = NULL; 
uint8_t lastBtPackets[MAX_BT_MSGS][10];
int btPacketIdx = 0;
int btPacketCount = 0;

const char* serviceUUID = "FFF0";
const char* charUUID    = "FFF1";

WebServer server(80);
DNSServer dnsServer;
int webSetupMode = 0; 
volatile int pendingWebSetupMode = 0; 
uint32_t webSetupStartTime = 0;
String apPassword = ""; 

String wifiSsid = "";
String wifiPass = "";
String mqttBroker = ""; 
int mqttPort = 1883;
String mqttUser = "";
String mqttPass = "";

String haIP = SECRET_HA_IP;
int haPort = SECRET_HA_PORT;
String camEntity = SECRET_CAM_ENTITY;
String mqttBabyTopic = SECRET_MQTT_BABY_TOPIC;
String mqttCameraTriggerTopic = SECRET_MQTT_CAM_TRIGGER;
volatile bool isBabyArmed = false;

volatile bool requestBabyStream = false;
volatile int babyStreamStatus = 0; 
volatile bool vidFSMode = false;
volatile bool showFps = false; 
volatile int currentFps = 0;

String babyStreamUrl = SECRET_BABY_STREAM_URL;

bool mqttEnabled = true;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

String savedMatMac = SECRET_MAC_MAT;
String savedKippyMac = SECRET_MAC_KIPPY;

bool isSetupScanning = false;
int setupScanMode = 0; 
uint32_t setupScanStartTime = 0; 
bool scanJustFinished = false;
char scanOptionsStr[2048] = "Suche laeuft...";

String scanResultMacs[MAX_SCAN_DEVICES];
String scanResultNames[MAX_SCAN_DEVICES];
int scanResultRssi[MAX_SCAN_DEVICES];
int scanResultCount = 0;
SemaphoreHandle_t bleMutex = NULL; 
volatile bool requestRollerUpdate = false; 

uint32_t lastCatSeenTime = 0;
int catRssi = -100;
bool wifiEnabled = true, matEnabled = true, kippyEnabled = true;
int prioMaster = 60, prioSlave = 50;
float effPrioMat = 0, effPrioKippy = 0, effPrioWifi = 0;
bool isTrackerMode = false, isCatAtHome = false, isTrackerDataValid = false; 
int radarSetupPhase = 0; 
bool pendingRadarTeardown = false, pendingWifiDisconnect = false, isBooting = true; 

volatile bool requestWake = false;
volatile bool requestSleep = false;
volatile bool requestChartUpdate = false; 
volatile bool requestMainTab = false; 
bool force_auto_fit = false;

bool connected = false;
uint32_t lastConnectTime = 0;
bool isArmed = false;                  
bool wasArmedBeforeDisconnect = false; 
bool alarmActive = false;
bool muted = false;
bool wifiStarted = false;
bool timeSynced = false;
uint32_t cooldownUntil = 0;
uint32_t startTime = 0;
uint32_t lastNotifyTime = 0;
int batteryPercent = 100;
int autoHealedDisconnectCount = 0; 

bool babyAlarmActive = false;
bool babyMuted = false;

int32_t rawPressure = 0;
int32_t taraOffset = 0;
int32_t currentPressure = 0;
int32_t currentAvg = 0;
int32_t intervalMaxPressure = -32000;
int32_t pressureHistory[HISTORY_SIZE];
int historyIdx = 0;
int historyCount = 0;
int32_t pressWindow[WINDOW_SIZE];
int pWinIdx = 0;
int pWinCount = 0;

int volumePercent = 50;
int streamVolumePercent = 50; 
int thresholdVal = 150;
bool isDarkMode = true;
int graphTimeSeconds = 150;
bool showTimeOnX = true;
int currentGraphMode = GRAPH_MODE_PRESSURE;
int storedGraphMode = GRAPH_MODE_PRESSURE;
bool autoBleInterval = true;
int manualBleIntervalMs = 1000;
bool pendingBleReconnect = false;
bool intentionalDisconnect = false;
bool disconnectAlarmActive = false;
bool rssiIsZero = false;
volatile bool isBleTabActive = false;
uint32_t intervalHistory[INTERVAL_HISTORY_SIZE];
int intervalIdx = 0;
int intervalCount = 0;
float avgInterval = 0.0;
float stdDevInterval = 0.0;
bool displayIsOff = false;
int brightnessPercent = 80;

int mjpegDropThreshold = 0; 
bool usePcmAudio = false; 
int camHackMode = 0; 
bool audioDebugEnabled = false;
String audioLogs[10];
int audioLogIdx = 0;

void addAudioLog(String msg) {
    if(!audioDebugEnabled) return;
    msg.trim();
    if(msg.length() == 0) return; 
    audioLogs[audioLogIdx] = msg;
    audioLogIdx = (audioLogIdx + 1) % 10;
}

class OverlayLogger : public Print {
    String currentLine = "";
public:
    size_t write(uint8_t c) override {
        Serial.write(c); 
        if (c == '\n') { addAudioLog(currentLine); currentLine = ""; } 
        else if (c != '\r') { currentLine += (char)c; }
        return 1;
    }
    size_t write(const uint8_t *buffer, size_t size) override {
        for(size_t i=0; i<size; i++) write(buffer[i]);
        return size;
    }
};
OverlayLogger myOverlayLogger;

void Data_Init() {
    for(int i=0; i<HISTORY_SIZE; i++) pressureHistory[i] = -32000;
    
    preferences.begin("catmat", false);
    wifiSsid = preferences.getString("wifiSsid", "");
    wifiPass = preferences.getString("wifiPass", "");
    mqttBroker = preferences.getString("mqIP", "");
    mqttPort = preferences.getInt("mqPort", 1883);
    mqttUser = preferences.getString("mqUser", "");
    mqttPass = preferences.getString("mqPass", "");
    mqttEnabled = preferences.getBool("mqttEn", true); 
    savedMatMac = preferences.getString("macM", SECRET_MAC_MAT);
    savedKippyMac = preferences.getString("macK", SECRET_MAC_KIPPY);
    
    babyStreamUrl = preferences.getString("babyUrl", SECRET_BABY_STREAM_URL);
    usePcmAudio = preferences.getBool("usePcm", false); 
    camHackMode = preferences.getInt("camHackM", 0); 
    
    volumePercent = preferences.getInt("volumePercent", 50);
    streamVolumePercent = preferences.getInt("streamVol", 50);
    thresholdVal = preferences.getInt("thr", 150);
    taraOffset = preferences.getUInt("off", 0);
    isDarkMode = preferences.getBool("dark", true);
    graphTimeSeconds = preferences.getInt("gtime", 150);
    showTimeOnX = preferences.getBool("timeX", true);
    currentGraphMode = preferences.getInt("gMode", GRAPH_MODE_PRESSURE);
    autoBleInterval = preferences.getBool("bleAuto", true);
    manualBleIntervalMs = preferences.getInt("bleManMs", 1000);
    brightnessPercent = preferences.getInt("bright", 80);
    audioDebugEnabled = preferences.getBool("audDbg", false);

    mjpegDropThreshold = preferences.getInt("mjDrop", 0);
    
    wifiEnabled = preferences.getBool("wifiEn", true);
    matEnabled = preferences.getBool("matEn", true);
    kippyEnabled = preferences.getBool("kipEn", true);
    prioMaster = preferences.getInt("prioM", 60);
    prioSlave = preferences.getInt("prioS", 50);
    preferences.end();
}

void calcMultiplex() {
    if (isStreamActive && !isArmed) {
        effPrioMat = 0;
        effPrioKippy = 0;
        effPrioWifi = 100.0;
        return;
    }
    if (!wifiEnabled && !kippyEnabled && !matEnabled) { effPrioMat = 0; effPrioKippy = 0; effPrioWifi = 0; return; }
    if (!matEnabled && (!kippyEnabled || !wifiEnabled)) { effPrioMat = 0; effPrioWifi = wifiEnabled ? 100.0 : 0.0; effPrioKippy = kippyEnabled ? 100.0 : 0.0; return; }
    
    if (matEnabled) {
        effPrioMat = (float)prioMaster;
        float remaining = 100.0 - effPrioMat;
        if (wifiEnabled && kippyEnabled) { effPrioKippy = remaining * ((float)prioSlave / 100.0); effPrioWifi = remaining - effPrioKippy; } 
        else if (wifiEnabled) { effPrioWifi = remaining; effPrioKippy = 0; } 
        else if (kippyEnabled) { effPrioKippy = remaining; effPrioWifi = 0; } 
        else { effPrioMat = 100.0; effPrioWifi = 0; effPrioKippy = 0; }
    } else {
        effPrioMat = 0;
        effPrioKippy = (float)prioSlave;
        effPrioWifi = 100.0 - effPrioKippy;
    }
}

class AudioOutputKeepAlive : public AudioOutputI2S {
private:
    bool isInitialized = false; 
public:
    // Sicherer Hardware DMA Puffer (6x256)
    AudioOutputKeepAlive() : AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 6, 256) {
        SetPinout(I2S_BCLK, I2S_LRCK, I2S_DOUT);
    }
    
    virtual bool begin() override {
        if (!isInitialized) {
            bool res = AudioOutputI2S::begin();
            isInitialized = true;
            return res;
        }
        return true; 
    }
    virtual bool stop() override { return true; }
};

AudioOutputKeepAlive *globalAudioOut = nullptr;

void audioTask(void *pvParameters) {
    audioLogger = &myOverlayLogger; 
    AudioMsg msg;
    int16_t sample[2];

    AudioGenerator *decoder = nullptr; 
    AudioFileSourceHTTPStream *httpFile = nullptr;
    AudioFileSourceBuffer *buffFile = nullptr; 
    uint8_t *preallocBuffer = nullptr;         
    
    bool streamRunning = false;
    int lastStreamVol = -1;
    float cachedStreamGain = 0.25f;
    int lastUiVol = -1;
    float cachedUiGain = 0.25f;

    uint32_t uiCheckCounter = 0;

    while(1) {
        bool shouldStream = requestBabyStream && !alarmActive && !disconnectAlarmActive && (WiFi.status() == WL_CONNECTED);

        if (shouldStream && !streamRunning) {
            babyStreamStatus = 1; 
            
            lastStreamVol = streamVolumePercent;
            float linearVol = (float)streamVolumePercent / 100.0f;
            cachedStreamGain = linearVol * linearVol; 
            globalAudioOut->SetGain(cachedStreamGain);
            
            httpFile = new AudioFileSourceHTTPStream(babyStreamUrl.c_str());
            
            if (usePcmAudio) {
                // WUNSCH: Software-Puffer erzwungen in den internen SRAM!
                preallocBuffer = (uint8_t*)heap_caps_malloc(16384, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                
                if (preallocBuffer) {
                    buffFile = new AudioFileSourceBuffer(httpFile, preallocBuffer, 16384);
                } else {
                    // Fallbacks, falls der SRAM fragmentiert ist
                    preallocBuffer = (uint8_t*)heap_caps_malloc(8192, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                    if (preallocBuffer) {
                        buffFile = new AudioFileSourceBuffer(httpFile, preallocBuffer, 8192);
                    } else {
                        buffFile = new AudioFileSourceBuffer(httpFile, 4096); 
                    }
                }
                decoder = new AudioGeneratorWAV();
            } else {
                preallocBuffer = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                if (preallocBuffer) {
                    buffFile = new AudioFileSourceBuffer(httpFile, preallocBuffer, 4096);
                } else {
                    buffFile = new AudioFileSourceBuffer(httpFile, 2048); 
                }
                decoder = new AudioGeneratorAAC();
            }
            
            if (decoder->begin(buffFile, globalAudioOut)) {
                streamRunning = true;
                babyStreamStatus = 2; 
                addAudioLog(usePcmAudio ? "PCM-Stream gestartet" : "AAC-Stream gestartet");
                uiCheckCounter = 0; 
            } else {
                addAudioLog("Stream Fehler");
                babyStreamStatus = 1; 
                delete decoder; decoder = nullptr;
                delete buffFile; buffFile = nullptr;
                delete httpFile; httpFile = nullptr;
                if (preallocBuffer) { heap_caps_free(preallocBuffer); preallocBuffer = nullptr; }
                vTaskDelay(pdMS_TO_TICKS(1000)); 
            }
        } 
        else if (!shouldStream && streamRunning) {
            if (decoder->isRunning()) decoder->stop(); 
            delete decoder; decoder = nullptr;
            delete buffFile; buffFile = nullptr;
            delete httpFile; httpFile = nullptr;
            if (preallocBuffer) { heap_caps_free(preallocBuffer); preallocBuffer = nullptr; }
            streamRunning = false;
            babyStreamStatus = 0; 
            addAudioLog("Baby-Stream beendet");
            
            sample[0] = 0; sample[1] = 0;
            for(int i=0; i<16000; i++) {
                globalAudioOut->ConsumeSample(sample);
                if (i % 128 == 0 && (requestBabyStream || uxQueueMessagesWaiting(audioQueue) > 0)) break;
            }
        }

        if (streamRunning) {
            if ((uiCheckCounter++ & 0x1F) == 0) {
                if (streamVolumePercent != lastStreamVol) {
                    lastStreamVol = streamVolumePercent;
                    float linearVol = (float)streamVolumePercent / 100.0f;
                    cachedStreamGain = linearVol * linearVol; 
                    globalAudioOut->SetGain(cachedStreamGain);
                }
                if (uxQueueMessagesWaiting(audioQueue) > 0) {
                    if (xQueueReceive(audioQueue, &msg, 0) == pdTRUE) {} 
                }
            }
            
            if (decoder->isRunning()) {
                if (!decoder->loop()) decoder->stop(); 
            } else {
                streamRunning = false;
                babyStreamStatus = 1; 
                delete decoder; decoder = nullptr;
                delete buffFile; buffFile = nullptr;
                delete httpFile; httpFile = nullptr;
                if (preallocBuffer) { heap_caps_free(preallocBuffer); preallocBuffer = nullptr; }
            }
            vTaskDelay(pdMS_TO_TICKS(1)); 
        } 
        else {
            babyStreamStatus = requestBabyStream ? 1 : 0;
            
            if (xQueueReceive(audioQueue, &msg, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (!muted || msg.isUiSound) {
                    if (volumePercent != lastUiVol) {
                        lastUiVol = volumePercent;
                        float linearVol = (float)volumePercent / 100.0f;
                        cachedUiGain = linearVol * linearVol;
                    }
                    globalAudioOut->SetGain(cachedUiGain); 
                    globalAudioOut->SetRate(16000);

                    int numSamples = msg.duration * 16; 
                    if (numSamples < 16) numSamples = 16;
                    if (numSamples > 32000) numSamples = 32000; 

                    float currentPhase = 0.0f;
                    float phaseInc1 = 0.0f;
                    float phaseInc2 = 0.0f;
                    
                    if (msg.soundType == 0) { phaseInc1 = (2.0f * M_PI * 200.0f) / 16000.0f; }
                    else if (msg.soundType == 1) { 
                        phaseInc1 = (2.0f * M_PI * 600.0f) / 16000.0f; 
                        phaseInc2 = (2.0f * M_PI * 800.0f) / 16000.0f; 
                    }

                    for (int i = 0; i < numSamples; i++) {
                        int16_t val = 0;
                        if (msg.soundType == 0) {
                            float env = 1.0f;
                            if (i <= 64) env = exp(-60.0f * ((float)i / numSamples)); 
                            else env = 0.0f; 
                            val = (int16_t)(sin(currentPhase) * env * 25000.0f);
                            currentPhase += phaseInc1;
                        }
                        else if (msg.soundType == 1) {
                            int halfSamples = numSamples / 2;
                            float progress = (i < halfSamples) ? ((float)i / halfSamples) : ((float)(i - halfSamples) / halfSamples);
                            currentPhase += (i < halfSamples) ? phaseInc1 : phaseInc2;
                            float env = sin(progress * M_PI); 
                            val = (int16_t)(sin(currentPhase) * env * 12000.0f); 
                        }
                        else if (msg.soundType == 2) {
                            float progress = (float)i / numSamples;
                            float sweep = sin(progress * M_PI);
                            float currentFreq = 700.0f + (400.0f * sweep); 
                            currentPhase += (2.0f * M_PI * currentFreq) / 16000.0f;
                            val = (int16_t)(sin(currentPhase) * sweep * 12000.0f); 
                        }
                        if (currentPhase > 2.0f * M_PI) currentPhase -= 2.0f * M_PI;
                        sample[0] = val; sample[1] = val;
                        globalAudioOut->ConsumeSample(sample);
                    }
                    sample[0] = 0; sample[1] = 0;
                    for (int i = 0; i < 16000; i++) {
                        globalAudioOut->ConsumeSample(sample);
                        if (i % 128 == 0 && uxQueueMessagesWaiting(audioQueue) > 0) break; 
                    }
                }
            }
        }
    }
}

void Audio_Init() {
    audioQueue = xQueueCreate(15, sizeof(AudioMsg));
    globalAudioOut = new AudioOutputKeepAlive();
    globalAudioOut->SetRate(16000);
    globalAudioOut->begin();
    
    // Audio Task sicher auf Core 0
    xTaskCreatePinnedToCore(audioTask, "AudioTask", 8192, NULL, 5, NULL, 0); 
}

void playToneI2S(uint16_t freq, uint32_t duration_ms, bool isUiSound) {
    static uint32_t last_tone_time = 0;
    if (isUiSound) {
        if (millis() - last_tone_time < 150) return; 
        last_tone_time = millis();
    }
    if(audioQueue != NULL) { 
        AudioMsg msg; 
        msg.freq = freq; 
        msg.duration = duration_ms; 
        msg.isUiSound = isUiSound;
        msg.soundType = 0; 
        xQueueSend(audioQueue, &msg, 0); 
    }
}

void playBabyAlarmI2S() {
    if(audioQueue != NULL && !muted) {
        AudioMsg msg;
        msg.freq = 0; 
        msg.duration = 1000; 
        msg.isUiSound = false;
        msg.soundType = 1; 
        xQueueSend(audioQueue, &msg, 0);
    }
}

void playCatAlarmI2S() {
    if(audioQueue != NULL && !muted) {
        AudioMsg msg;
        msg.freq = 0; 
        msg.duration = 800; 
        msg.isUiSound = false;
        msg.soundType = 2; 
        xQueueSend(audioQueue, &msg, 0);
    }
}

void fullReset() { 
    alarmActive = false; 
    disconnectAlarmActive = false; 
    muted = false; 
    isArmed = false; 
    cooldownUntil = millis() + 5000; 
}
void wakeDisplay() { requestWake = true; }
void sleepDisplay() { requestSleep = true; }

void factoryReset() {
    preferences.begin("catmat", false); preferences.clear(); preferences.end();
    delay(1000); ESP.restart();
}

void audio_info(const char *info) { Serial.print("AUDIO INFO: "); Serial.println(info); }
void audio_showstreaminfo(const char *info) { Serial.print("AUDIO FORMAT: "); Serial.println(info); }
void audio_bitrate(const char *info) { Serial.print("AUDIO BITRATE: "); Serial.println(info); }
void audio_eof_stream(const char *info) { Serial.print("AUDIO STREAM ENDE: "); Serial.println(info); }