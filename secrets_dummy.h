#pragma once

// WLAN Zugangsdaten
#define SECRET_WIFI_SSID "DEIN_WLAN_NAME"
#define SECRET_WIFI_PASS "DEIN_WLAN_PASSWORT"

// Home Assistant Server
#define SECRET_HA_IP "192.168.1.100"
#define SECRET_HA_PORT 8123
#define SECRET_HA_TOKEN "DEIN_LONG_LIVED_ACCESS_TOKEN"

// Kamera & Snapshot Pfade
#define SECRET_CAM_ENTITY "camera.deine_kamera"
#define SECRET_CAM_SNAPSHOT_PATH "/local/snap/snapshot.jpg"

// Bluetooth MAC Adressen
#define SECRET_MAC_MAT "00:00:00:00:00:00"
#define SECRET_MAC_KIPPY "00:00:00:00:00:00"

// Default MQTT Topics
#define SECRET_MQTT_BABY_TOPIC "smartmat/baby/cry/state"
#define SECRET_MQTT_CAM_TRIGGER "camera/trigger/snapshot"