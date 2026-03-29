# Leola – Smart Home Touch Display

Ein multifunktionales, lokales Smart Home Dashboard auf Basis des Waveshare ESP32-S3 Touch LCD (1.85 Zoll). Das System dient als Kommandozentrale für eine intelligente Katzenmatte (BLE), als latenzfreies Video-Babyphone und als Home Assistant Satellit.

<img width="1287" height="443" alt="image" src="https://github.com/user-attachments/assets/f490a457-8898-493d-af23-52903f420960" />




---

## 🌟 Kernfunktionen

* **Katzensensorik & Tracking (BLE):**
  * Verbindet sich per Bluetooth Low Energy (BLE) mit einer smarten Katzenmatte.
  * Live-Auswertung von Drucksensordaten zur Anwesenheitserkennung inkl. Alarmierung.
  * "Radar-Modus" zur Erkennung eines "Kippy"-Trackers über die Bluetooth-Signalstärke (RSSI) oder direkten Home Assistant Status.
* **Latenzfreies Babyphone (Video & Audio):**
  * Empfängt den kontinuierlichen MJPEG-Stream (Multipart) nicht direkt von der Kamera, sondern ressourcenschonend über den Home Assistant (Frigate / go2rtc) Server.
  * Die Video-Dekodierung (`JPEGDEC`) wurde stark optimiert, um hochauflösende Streams flüssig und nahezu verzögerungsfrei auf das Display zu bringen.
  * **Live-Audio:** Empfängt die vom Home Assistant (go2rtc) bereitgestellte Audiospur und gibt den Ton über den integrierten I2S-Lautsprecher des ESP32 aus.
  * Akustische I2S-Audio-Ausgabe für Baby-Alarme (gesteuert über die Reolink Home Assistant Integration).
* **Home Assistant Integration (MQTT):**
  * Vollautomatische Einbindung via MQTT Auto-Discovery.
  * Meldet Batteriestatus, WLAN/BLE-Signalstärken und Alarmzustände.
  * Fernsteuerung der Matte (Mute, Alarm-Reset) direkt über das HA-Dashboard.
* **Smartes Touch-Interface (LVGL v9):**
  * Intuitive Benutzeroberfläche mit Sidebar-Navigation (Dashboard, Babyphone, Katzenmatte, Settings).
* **Captive Portal Web-Setup:**
  * Bei fehlender WLAN-Verbindung spannt der ESP32 einen eigenen Hotspot (`LolaCatMat-Setup`) auf. Über ein Web-Interface lassen sich WLAN, MQTT-Broker, Home Assistant IPs und Kamera-URLs bequem konfigurieren.

---

## 🏗 Hardware & Systemarchitektur

* **Display/Mikrocontroller:** Waveshare ESP32-S3 Touch LCD 1.85C (360x360 Pixel kapazitiver Touch, 16MB Flash, 8MB PSRAM). Die Display- und Touch-Treiber (ST77916, CST816) sind direkt im Code integriert.
* **Sensoren:** Custom Bluetooth Katzenmatte.
* **Kamera:** Reolink E1 Pro (oder kompatible RTSP/RTMP-Kameras mit Audio-Erkennung).
* **Backend:** Home Assistant mit Reolink-Integration, Kippy-Integration, Frigate NVR und `go2rtc`.

---

## 📚 Benötigte Arduino Bibliotheken

Bevor der Code in der Arduino IDE kompiliert werden kann, müssen folgende externe Bibliotheken über den **Bibliotheksverwalter** installiert werden:

1. **lvgl** (Zwingend Version 9.x!) - Grafikbibliothek für die Benutzeroberfläche.
2. **JPEGDEC** (von Larry Bank) - Für die schnelle, RAM-schonende Video-Dekodierung.
3. **ArduinoJson** (von Benoit Blanchon) - Für die MQTT Payload-Verarbeitung.
4. **PubSubClient** (von Nick O'Leary) - Für die MQTT-Kommunikation.

*(Hinweis: Spezielle Waveshare-Bibliotheken müssen nicht händisch heruntergeladen werden, da die nötigen I2C/SPI-Treiber und der IO-Expander bereits direkt im Projektordner liegen.)*

---

## 🚀 Inbetriebnahme & Konfiguration

### 1. Arduino IDE Settings (Board-Konfiguration)
Damit der ESP32-S3 den Videostream puffern kann und das Display funktioniert, wähle unter `Werkzeuge` folgende exakte Einstellungen für das **ESP32S3 Dev Module**:
* **Flash Size:** `16MB (128Mb)`
* **Partition Scheme:** `16M Flash (3MB APP/9.9MB FATFS)`
* **PSRAM:** `OPI PSRAM` *(Absolut kritisch für den Kamera-Puffer!)*
* **USB Mode:** `Hardware CDC and JTAG`
* **USB CDC On Boot:** `Enabled`

### 2. Reolink Kamera konfigurieren
Damit das Video latenzfrei und ohne Bildfehler auf dem Display ankommt, muss der Sub-Stream der Kamera korrekt eingestellt werden.
1. Öffne den Reolink Windows/Mac Client oder das Web-Interface der Kamera.
2. Gehe zu **Netzwerk -> Erweitert -> Port-Einstellungen** und aktiviere zwingend **HTTP (Port 80)** und **RTMP (Port 1935)**.
3. Gehe zu den **Stream-Einstellungen** und konfiguriere den **Sub-Stream (Flüssig / Fluency)** wie folgt:
   * **Auflösung:** 640x360
   * **Framerate (FPS):** 10 (oder 15)
   * **Maximale Bitrate:** 512 kbps (oder höher)

### 3. Video-Backend konfigurieren (Frigate / go2rtc)
Home Assistant (Frigate) greift den Stream über RTMP ab und wandelt ihn in ein ESP-kompatibles Format um. Füge dies in deine `frigate.yml` ein:

```yaml
go2rtc:
  streams:
    # HTTP-FLV Sub-Stream der Reolink Kamera abgreifen (Sonderzeichen im Passwort URL-enkodieren!)
    Bog_Cam_sub:
      - "http://<IP_DER_KAMERA>/flv?port=1935&app=bcs&stream=channel0_sub.bcs&user=frigate&password=<DEIN_PASSWORT>"

    # Formatwandlung auf MJPEG für das Display. #q=2 für Top-Qualität, gedrosselt auf 5 FPS für Zero-Delay.
    baby_video_esp:
      - "ffmpeg:Bog_Cam_sub#video=mjpeg#fps=5#q=2"
```

### 4. Home Assistant: Babyschrei-Alarm Automation
Der ESP32 erkennt das Baby-Schreien nicht selbst, sondern verlässt sich auf die KI der Reolink-Kamera. Lege **händisch eine Automation in Home Assistant** an, um den Status per MQTT an das Display zu senden:

```yaml
alias: "LolaCatMat: Baby Alarm Weiterleitung"
description: "Sendet den Reolink Babyschrei-Alarm per MQTT an das Display"
trigger:
  - platform: state
    entity_id: binary_sensor.reolink_e1_pro_baby_crying  # Passe den Namen an deine Kamera an
    to: "on"
action:
  - service: mqtt.publish
    data:
      topic: "smartmat/baby/cry/state"  # Muss mit dem Setup im ESP32 übereinstimmen
      payload: "baby_alarm"
mode: single
```

### 5. Home Assistant: Kippy Tracker Status Weiterleitung
Damit die Katzenmatte weiß, ob die Katze zuhause oder unterwegs ist, muss auch der Status des Kippy GPS-Trackers kontinuierlich an den ESP32 gesendet werden. Lege dafür diese Automation an:

```yaml
alias: "LolaCatMat: Kippy Tracker Status"
description: "Sendet den aktuellen Status des Kippy Trackers an das Display"
trigger:
  - platform: state
    entity_id: device_tracker.kippy_lola_gps  # Entität deines Trackers anpassen
action:
  - service: mqtt.publish
    data:
      topic: "lolacatmat/kippy/status"  # Fest im ESP32 Code hinterlegtes Topic
      payload: "{{ states('device_tracker.kippy_lola_gps') }}"
mode: single
```

### 6. ESP32 Ersteinrichtung (Captive Portal)
1. Flashe den Quellcode auf das Board.
2. Der ESP32 startet einen eigenen Access Point namens `LolaCatMat-Setup`.
3. Verbinde dich per Smartphone und das Setup-Portal öffnet sich (ansonsten `192.168.4.1` im Browser aufrufen).
4. Trage WLAN-Daten, MQTT-Broker IP, das MQTT-Topic für den Babyschrei-Alarm (siehe Schritt 4) und die Home Assistant IP ein.
5. Setze die **Babyphone Video URL** auf deinen Frigate-Stream:
   `http://<IP_DEINES_HA_SERVERS>:1984/api/stream.mjpeg?src=baby_video_esp`
6. Setze die **Babyphone Audio Stream URL** auf die Audiospur deiner Kamera (ebenfalls über go2rtc abrufbar):
   `http://<IP_DEINES_HA_SERVERS>:1984/api/stream.aac?src=Bog_Cam_sub` *(Hinweis: Dateiendung je nach Audio-Codec deiner Kamera, z.B. .aac oder .mp3)*
7. Speichern. Das System startet neu, verbindet sich und das Dashboard ist einsatzbereit!
