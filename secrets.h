#pragma once

// WLAN Zugangsdaten
#define SECRET_WIFI_SSID "SSID"
#define SECRET_WIFI_PASS "WIFIPW"

// Home Assistant Server
#define SECRET_HA_IP "192.168.2.44"
#define SECRET_HA_PORT 8123
#define SECRET_HA_TOKEN "veryLongHomeAssistantPermanentToken"

// Kamera & Snapshot Pfade
#define SECRET_CAM_ENTITY "camera.bog_standardauflosung"
#define SECRET_CAM_SNAPSHOT_PATH "http://192.168.2.44:1984/stream.html?src=baby_video_mjpeg"
//#define SECRET_CAM_SNAPSHOT_PATH "/local/snap/bog_snapshot.jpg"

// Bluetooth MAC Adressen
#define SECRET_MAC_MAT "20:24:08:29:13:f0"
#define SECRET_MAC_KIPPY "e4:10:27:02:9f:a3"

// Default MQTT Topics
#define SECRET_MQTT_BABY_TOPIC "lolacatmat/baby/cry/state"
#define SECRET_MQTT_CAM_TRIGGER "camera/trigger/snapshot"

#define SECRET_BABY_STREAM_URL "http://192.168.2.44:1984/api/stream.aac?src=baby_audio_esp"
//#define SECRET_BABY_STREAM_URL "http://192.168.2.44:1984/stream.html?src=baby_video_mjpeg"