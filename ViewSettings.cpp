#include "ViewSettings.h"
#include "GuiManager.h"
#include "SharedData.h"
#include "SystemLogic.h"
#include "LVGL_Driver.h"
#include <WiFi.h>

#define TEXT_COLOR (isDarkMode ? lv_color_white() : lv_color_black())

static lv_obj_t * label_thr_val;
static lv_obj_t * label_time_val;
static lv_obj_t * label_mjpeg_drop_val;
static lv_obj_t * label_cam_ref_val; 
static lv_obj_t * sl_master;
static lv_obj_t * sl_slave;
static lv_obj_t * lbl_master;
static lv_obj_t * lbl_slave;
static lv_obj_t * lbl_sw_wifi;
static lv_obj_t * lbl_sw_mat;
static lv_obj_t * lbl_sw_kippy;
static lv_obj_t * lbl_setup_mat;
static lv_obj_t * lbl_setup_kip;
static lv_obj_t * text_ble_info;
static lv_obj_t * text_sys_info;

static lv_obj_t * qr_overlay;      
static lv_obj_t * qr_screenshot;      
static lv_obj_t * btn_stop_screenshot; 
static lv_obj_t * lbl_qr_ip; 

static lv_obj_t * scan_overlay; 
static lv_obj_t * scan_spinner; 
static lv_obj_t * lbl_scan_info;
static lv_obj_t * dd_scan_results; 
static lv_obj_t * btn_save_mac; 
static lv_obj_t * btn_cancel_mac;
static lv_obj_t * btn_rescan_mac; 
static lv_obj_t * btn_continue_mac;

static const int cam_intervals[] = {10, 20, 30, 50, 75, 100, 200, 300, 500, 750, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000};

static void update_sliders_ui() {
    if (!matEnabled || (!kippyEnabled && !wifiEnabled)) { 
        lv_obj_add_state(sl_master, LV_STATE_DISABLED); 
        lv_obj_set_style_text_color(lbl_master, lv_color_hex(0x555555), 0); 
    } else { 
        lv_obj_clear_state(sl_master, LV_STATE_DISABLED); 
        lv_obj_set_style_text_color(lbl_master, TEXT_COLOR, 0); 
    }
    
    if (!kippyEnabled || !wifiEnabled) { 
        lv_obj_add_state(sl_slave, LV_STATE_DISABLED); 
        lv_obj_set_style_text_color(lbl_slave, lv_color_hex(0x555555), 0); 
    } else { 
        lv_obj_clear_state(sl_slave, LV_STATE_DISABLED); 
        lv_obj_set_style_text_color(lbl_slave, TEXT_COLOR, 0); 
    }
}

static lv_obj_t* create_text_label(lv_obj_t* parent, const char* text) {
    lv_obj_t * lbl = lv_label_create(parent); lv_label_set_text(lbl, text); lv_obj_set_style_text_color(lbl, TEXT_COLOR, 0); return lbl;
}
static lv_obj_t* create_white_label(lv_obj_t* parent, const char* text) {
    lv_obj_t * lbl = lv_label_create(parent); lv_label_set_text(lbl, text); lv_obj_set_style_text_color(lbl, lv_color_white(), 0); return lbl;
}
static lv_obj_t* create_header(lv_obj_t* parent, const char* text) {
    lv_obj_t * header = lv_label_create(parent); lv_label_set_text(header, text); lv_obj_set_style_text_font(header, &lv_font_montserrat_14, 0); lv_obj_set_style_text_color(header, lv_color_hex(0x00A0FF), 0); lv_obj_set_style_pad_top(header, 15, 0); return header;
}
static lv_obj_t* create_helper_cont(lv_obj_t* parent, int height) {
    lv_obj_t * cont = lv_obj_create(parent); lv_obj_set_size(cont, 220, height); lv_obj_set_style_bg_opa(cont, 0, 0); lv_obj_set_style_border_width(cont, 0, 0); lv_obj_set_style_pad_all(cont, 0, 0); lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE); return cont;
}

static void btn_back_cb(lv_event_t * e) { 
    playToneI2S(800, 100, true);
    gui.switchScreen(SCREEN_DASHBOARD, LV_SCR_LOAD_ANIM_MOVE_BOTTOM); 
}

static void btn_stop_screenshot_cb(lv_event_t * e) { 
    playToneI2S(600, 100, true); 
    lv_obj_add_flag(qr_overlay, LV_OBJ_FLAG_HIDDEN); 
    pendingScreenshotMode = 2; 
}

static void easter_egg_cb(lv_event_t * e) {
    playToneI2S(800, 100, true);
    static uint32_t last_click = 0; static int click_count = 0; uint32_t now = millis();
    if (now - last_click > 1500) click_count = 0; 
    click_count++; last_click = now;
    lv_obj_t * btn = (lv_obj_t *)lv_event_get_target(e); lv_obj_t * lbl = lv_obj_get_child(btn, 0);

    if (click_count >= 5) {
        click_count = 0; if (lbl) lv_label_set_text(lbl, LV_SYMBOL_LIST " INFORMATIONEN"); 
        String ipStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "Kein_WLAN"; 
        String qrData = "http://" + ipStr;
        lv_qrcode_update(qr_screenshot, qrData.c_str(), qrData.length()); 
        
        if (lbl_qr_ip) lv_label_set_text_fmt(lbl_qr_ip, "Link: %s", qrData.c_str());
        
        lv_obj_clear_flag(qr_overlay, LV_OBJ_FLAG_HIDDEN); 
        lv_obj_move_foreground(qr_overlay);
        
        pendingScreenshotMode = 1; 
    } else { if (lbl) lv_label_set_text_fmt(lbl, LV_SYMBOL_LIST " INFORMATIONEN (%d/5)", click_count); }
}

static void btn_ap_setup_cb(lv_event_t * e) { playToneI2S(800, 100, true); if(isSetupScanning) return; pendingWebSetupMode = 1; }
static void btn_sta_setup_cb(lv_event_t * e) { playToneI2S(800, 100, true); if(isSetupScanning) return; pendingWebSetupMode = 2; }

static void slider_thr_event_cb(lv_event_t * e) { playToneI2S(1000, 50, true); lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e); int val = lv_slider_get_value(slider); val = (val / 10) * 10; lv_slider_set_value(slider, val, LV_ANIM_OFF); thresholdVal = val; lv_label_set_text_fmt(label_thr_val, "Limit: %d Digits", thresholdVal); preferences.begin("catmat", false); preferences.putInt("thr", thresholdVal); preferences.end(); }
static void slider_time_event_cb(lv_event_t * e) { playToneI2S(1000, 50, true); lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e); int val = lv_slider_get_value(slider); val = (val / 5) * 5; lv_slider_set_value(slider, val, LV_ANIM_OFF); graphTimeSeconds = val; lv_label_set_text_fmt(label_time_val, "Graph Dauer: %d s", graphTimeSeconds); requestChartUpdate = true; preferences.begin("catmat", false); preferences.putInt("gtime", graphTimeSeconds); preferences.end(); }
static void switch_time_x_event_cb(lv_event_t * e) { playToneI2S(800, 100, true); showTimeOnX = lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED); preferences.begin("catmat", false); preferences.putBool("timeX", showTimeOnX); preferences.end(); requestChartUpdate = true; }
static void dd_graph_mode_event_cb(lv_event_t * e) { playToneI2S(800, 100, true); lv_obj_t * dropdown = (lv_obj_t *)lv_event_get_target(e); currentGraphMode = lv_dropdown_get_selected(dropdown); historyIdx = 0; historyCount = 0; for(int i=0; i<HISTORY_SIZE; i++) pressureHistory[i] = -32000; requestChartUpdate = true; preferences.begin("catmat", false); preferences.putInt("gMode", currentGraphMode); preferences.end(); }
static void btn_tara_event_cb(lv_event_t * e) { playToneI2S(800, 100, true); taraOffset = rawPressure; preferences.begin("catmat", false); preferences.putUInt("off", taraOffset); preferences.end(); }
static void switch_dark_event_cb(lv_event_t * e) { playToneI2S(800, 100, true); isDarkMode = lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED); preferences.begin("catmat", false); preferences.putBool("dark", isDarkMode); preferences.end(); gui.switchScreen(SCREEN_DASHBOARD, LV_SCR_LOAD_ANIM_MOVE_BOTTOM); }

static void slider_cam_ref_event_cb(lv_event_t * e) { 
    playToneI2S(1000, 50, true);
    lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e); 
    int idx = lv_slider_get_value(slider); 
    if (idx < 0) idx = 0; if (idx > 19) idx = 19; 
    cameraRefreshMs = cam_intervals[idx]; 
    
    if (cameraRefreshMs < 1000) {
        lv_label_set_text_fmt(label_cam_ref_val, "Update Intervall: %d ms", cameraRefreshMs); 
    } else {
        lv_label_set_text_fmt(label_cam_ref_val, "Update Intervall: %d Sek", cameraRefreshMs / 1000); 
    }
}
static void slider_cam_ref_release_cb(lv_event_t * e) { 
    preferences.begin("catmat", false); 
    preferences.putInt("camRef", cameraRefreshMs); 
    preferences.end(); 
}

static void slider_mjpeg_drop_event_cb(lv_event_t * e) {
    playToneI2S(1000, 50, true);
    lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    mjpegDropThreshold = val * 1024; 
    if (val == 0) lv_label_set_text(label_mjpeg_drop_val, "Latenz-Drop: Aggressiv (0 KB)");
    else lv_label_set_text_fmt(label_mjpeg_drop_val, "Latenz-Drop: %d KB", val);
}
static void slider_mjpeg_drop_release_cb(lv_event_t * e) {
    preferences.begin("catmat", false); 
    preferences.putInt("mjDrop", mjpegDropThreshold); 
    preferences.end();
}

static void sw_wifi_cb(lv_event_t * e) { playToneI2S(800, 100, true); wifiEnabled = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED); preferences.begin("catmat", false); preferences.putBool("wifiEn", wifiEnabled); preferences.end(); calcMultiplex(); update_sliders_ui(); }
static void sw_mqtt_cb(lv_event_t * e) { playToneI2S(800, 100, true); mqttEnabled = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED); preferences.begin("catmat", false); preferences.putBool("mqttEn", mqttEnabled); preferences.end(); }
static void sw_mat_cb(lv_event_t * e) { playToneI2S(800, 100, true); matEnabled = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED); preferences.begin("catmat", false); preferences.putBool("matEn", matEnabled); preferences.end(); calcMultiplex(); update_sliders_ui(); }
static void sw_kippy_cb(lv_event_t * e) { playToneI2S(800, 100, true); kippyEnabled = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED); preferences.begin("catmat", false); preferences.putBool("kipEn", kippyEnabled); preferences.end(); calcMultiplex(); update_sliders_ui(); }
static void sl_master_cb(lv_event_t * e) { playToneI2S(1000, 50, true); prioMaster = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e)); preferences.begin("catmat", false); preferences.putInt("prioM", prioMaster); preferences.end(); calcMultiplex(); }
static void sl_slave_cb(lv_event_t * e) { playToneI2S(1000, 50, true); prioSlave = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e)); preferences.begin("catmat", false); preferences.putInt("prioS", prioSlave); preferences.end(); calcMultiplex(); }

static void btn_scan_mat_cb(lv_event_t * e) {
    playToneI2S(800, 100, true);
    if (bleMutex != NULL && xSemaphoreTake(bleMutex, pdMS_TO_TICKS(50)) == pdTRUE) { scanResultCount = 0; strcpy(scanOptionsStr, "Suche laeuft..."); xSemaphoreGive(bleMutex); }
    requestRollerUpdate = true; setupScanStartTime = millis(); isSetupScanning = true; setupScanMode = 1; scanJustFinished = false;
    lv_label_set_text(lbl_scan_info, "Suche Matte..."); lv_obj_clear_flag(scan_overlay, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(scan_overlay); 
}
static void btn_scan_kip_cb(lv_event_t * e) {
    playToneI2S(800, 100, true);
    if (bleMutex != NULL && xSemaphoreTake(bleMutex, pdMS_TO_TICKS(50)) == pdTRUE) { scanResultCount = 0; strcpy(scanOptionsStr, "Suche laeuft..."); xSemaphoreGive(bleMutex); }
    requestRollerUpdate = true; setupScanStartTime = millis(); isSetupScanning = true; setupScanMode = 2; scanJustFinished = false;
    lv_label_set_text(lbl_scan_info, "Suche Tracker..."); lv_obj_clear_flag(scan_overlay, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(scan_overlay);
}

static void btn_rescan_mac_cb(lv_event_t * e) {
    playToneI2S(800, 100, true);
    if (bleMutex != NULL && xSemaphoreTake(bleMutex, pdMS_TO_TICKS(50)) == pdTRUE) { scanResultCount = 0; strcpy(scanOptionsStr, "Suche laeuft..."); xSemaphoreGive(bleMutex); }
    requestRollerUpdate = true; setupScanStartTime = millis(); isSetupScanning = true; scanJustFinished = false;
}
static void btn_continue_mac_cb(lv_event_t * e) {
    playToneI2S(800, 100, true);
    if (!isSetupScanning) { setupScanStartTime = millis(); isSetupScanning = true; scanJustFinished = false; }
}
static void btn_cancel_mac_cb(lv_event_t * e) {
    playToneI2S(600, 100, true);
    lv_obj_add_flag(scan_overlay, LV_OBJ_FLAG_HIDDEN); isSetupScanning = false; setupScanMode = 0; scanJustFinished = false;
    pendingRadarTeardown = true;
}
static void btn_save_mac_cb(lv_event_t * e) {
    playToneI2S(800, 100, true);
    uint16_t idx = lv_roller_get_selected(dd_scan_results);
    if (bleMutex != NULL && xSemaphoreTake(bleMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (idx < scanResultCount) {
            if (setupScanMode == 1) { savedMatMac = scanResultMacs[idx]; preferences.begin("catmat", false); preferences.putString("macM", savedMatMac); preferences.end(); } 
            else if (setupScanMode == 2) { savedKippyMac = scanResultMacs[idx]; preferences.begin("catmat", false); preferences.putString("macK", savedKippyMac); preferences.end(); }
        }
        xSemaphoreGive(bleMutex);
    }
    lv_obj_add_flag(scan_overlay, LV_OBJ_FLAG_HIDDEN); isSetupScanning = false; setupScanMode = 0; scanJustFinished = false;
    pendingRadarTeardown = true; 
}

lv_obj_t* ViewSettings::build() {
    lv_obj_t* scr = lv_obj_create(NULL);
    if(!scr) return nullptr;
    lv_obj_set_style_bg_color(scr, isDarkMode ? lv_color_hex(0x111111) : lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(scr, GuiManager::gestureEventWrapper, LV_EVENT_GESTURE, &gui);

    lv_obj_t * top_bar = lv_obj_create(scr); lv_obj_set_size(top_bar, 360, 50); lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0); lv_obj_set_style_bg_opa(top_bar, 0, 0); lv_obj_set_style_border_width(top_bar, 0, 0); lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * btn_back = lv_btn_create(top_bar); lv_obj_set_size(btn_back, 140, 40); lv_obj_set_style_radius(btn_back, 20, 0); lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x333333), 0); lv_obj_align(btn_back, LV_ALIGN_CENTER, 0, 5); lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = create_white_label(btn_back, LV_SYMBOL_LEFT " ZURUECK"); lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_14, 0); lv_obj_center(lbl_back);

    lv_obj_t * scroll_cont = lv_obj_create(scr); lv_obj_set_size(scroll_cont, 360, 310); lv_obj_align(scroll_cont, LV_ALIGN_BOTTOM_MID, 0, 0); lv_obj_set_style_bg_opa(scroll_cont, 0, 0); lv_obj_set_style_border_width(scroll_cont, 0, 0); lv_obj_set_style_pad_all(scroll_cont, 10, 0); lv_obj_set_flex_flow(scroll_cont, LV_FLEX_FLOW_COLUMN); lv_obj_set_flex_align(scroll_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_row(scroll_cont, 15, 0); 
    lv_obj_add_event_cb(scroll_cont, GuiManager::gestureEventWrapper, LV_EVENT_GESTURE, &gui);

    lv_obj_t * title = create_text_label(scroll_cont, "Einstellungen"); lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0); lv_obj_set_style_pad_bottom(title, 10, 0);

    create_header(scroll_cont, LV_SYMBOL_SETTINGS " SENSOR & GRAPH");
    label_thr_val = create_text_label(scroll_cont, ""); lv_label_set_text_fmt(label_thr_val, "Limit: %d Digits", thresholdVal);
    lv_obj_t * slider_thr = lv_slider_create(scroll_cont); lv_obj_set_size(slider_thr, 200, 10); lv_slider_set_range(slider_thr, 50, 600); lv_slider_set_value(slider_thr, thresholdVal, LV_ANIM_OFF); lv_obj_add_event_cb(slider_thr, slider_thr_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    label_time_val = create_text_label(scroll_cont, ""); lv_label_set_text_fmt(label_time_val, "Graph Dauer: %d s", graphTimeSeconds);
    lv_obj_t * slider_time = lv_slider_create(scroll_cont); lv_obj_set_size(slider_time, 200, 10); lv_slider_set_range(slider_time, 10, 900); lv_slider_set_value(slider_time, graphTimeSeconds, LV_ANIM_OFF); lv_obj_add_event_cb(slider_time, slider_time_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t * cont_tx = create_helper_cont(scroll_cont, 40); lv_obj_t * label_time_x = create_text_label(cont_tx, "X-Achse: Uhrzeit"); lv_obj_align(label_time_x, LV_ALIGN_LEFT_MID, 10, 0); lv_obj_t * switch_time_x = lv_switch_create(cont_tx); lv_obj_align(switch_time_x, LV_ALIGN_RIGHT_MID, -10, 0); if(showTimeOnX) lv_obj_add_state(switch_time_x, LV_STATE_CHECKED); lv_obj_add_event_cb(switch_time_x, switch_time_x_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    create_text_label(scroll_cont, "Graph Datenquelle:"); lv_obj_t * dd_graph_mode = lv_dropdown_create(scroll_cont); lv_obj_set_width(dd_graph_mode, 200); lv_dropdown_set_options(dd_graph_mode, "Druckwert\nBLE RSSI\nWLAN RSSI\nBLE Intervall\nKippy RSSI"); lv_dropdown_set_selected(dd_graph_mode, currentGraphMode); lv_obj_add_event_cb(dd_graph_mode, dd_graph_mode_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t * btn_tara = lv_btn_create(scroll_cont); lv_obj_set_size(btn_tara, 200, 40); lv_obj_set_style_bg_color(btn_tara, lv_color_hex(0x333333), 0); lv_obj_add_event_cb(btn_tara, btn_tara_event_cb, LV_EVENT_CLICKED, NULL); lv_obj_t * label_tara = create_white_label(btn_tara, "TARA KALIBRIEREN"); lv_obj_center(label_tara);

    create_header(scroll_cont, LV_SYMBOL_VIDEO " BABY-MONITOR & AUDIO");
    label_cam_ref_val = create_text_label(scroll_cont, ""); 
    if (cameraRefreshMs < 1000) lv_label_set_text_fmt(label_cam_ref_val, "Update Intervall: %d ms", cameraRefreshMs);
    else lv_label_set_text_fmt(label_cam_ref_val, "Update Intervall: %d Sek", cameraRefreshMs / 1000);
    int start_idx = 10; for (int i=0; i<20; i++) { if (cameraRefreshMs <= cam_intervals[i]) { start_idx = i; break; } }
    lv_obj_t * slider_cam_ref = lv_slider_create(scroll_cont); lv_obj_set_size(slider_cam_ref, 200, 10); lv_slider_set_range(slider_cam_ref, 0, 19); lv_slider_set_value(slider_cam_ref, start_idx, LV_ANIM_OFF); lv_obj_add_event_cb(slider_cam_ref, slider_cam_ref_event_cb, LV_EVENT_VALUE_CHANGED, NULL); lv_obj_add_event_cb(slider_cam_ref, slider_cam_ref_release_cb, LV_EVENT_RELEASED, NULL);

    label_mjpeg_drop_val = create_text_label(scroll_cont, "");
    if (mjpegDropThreshold == 0) lv_label_set_text(label_mjpeg_drop_val, "Latenz-Drop: Aggressiv (0 KB)");
    else lv_label_set_text_fmt(label_mjpeg_drop_val, "Latenz-Drop: %d KB", mjpegDropThreshold / 1024);
    
    lv_obj_t * slider_mjpeg_drop = lv_slider_create(scroll_cont); 
    lv_obj_set_size(slider_mjpeg_drop, 200, 10); 
    lv_slider_set_range(slider_mjpeg_drop, 0, 16); 
    lv_slider_set_value(slider_mjpeg_drop, mjpegDropThreshold / 1024, LV_ANIM_OFF); 
    lv_obj_add_event_cb(slider_mjpeg_drop, slider_mjpeg_drop_event_cb, LV_EVENT_VALUE_CHANGED, NULL); 
    lv_obj_add_event_cb(slider_mjpeg_drop, slider_mjpeg_drop_release_cb, LV_EVENT_RELEASED, NULL);

    // Audio Format Schalter
    lv_obj_t * cont_audio_fmt = create_helper_cont(scroll_cont, 40);
    lv_obj_t * label_audio_fmt = create_text_label(cont_audio_fmt, "Audio: PCM (WAV)");
    lv_obj_align(label_audio_fmt, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t * switch_audio_fmt = lv_switch_create(cont_audio_fmt);
    lv_obj_align(switch_audio_fmt, LV_ALIGN_RIGHT_MID, -10, 0);
    if(usePcmAudio) lv_obj_add_state(switch_audio_fmt, LV_STATE_CHECKED);
    lv_obj_add_event_cb(switch_audio_fmt, [](lv_event_t* e) {
        playToneI2S(800, 100, true);
        usePcmAudio = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
        Preferences prefs; prefs.begin("catmat", false);
        prefs.putBool("usePcm", usePcmAudio);
        prefs.end();
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // --- NEU: Der Hardware-Schalter fuer den Kamera Hack ---
    lv_obj_t * cont_cam_hack = create_helper_cont(scroll_cont, 40);
    lv_obj_t * label_cam_hack = create_text_label(cont_cam_hack, "Kamera API Hack (320x240)");
    lv_obj_align(label_cam_hack, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t * switch_cam_hack = lv_switch_create(cont_cam_hack);
    lv_obj_align(switch_cam_hack, LV_ALIGN_RIGHT_MID, -10, 0);
    if(useBabyCamHack) lv_obj_add_state(switch_cam_hack, LV_STATE_CHECKED);
    lv_obj_add_event_cb(switch_cam_hack, [](lv_event_t* e) {
        playToneI2S(800, 100, true);
        useBabyCamHack = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
        Preferences prefs; prefs.begin("catmat", false);
        prefs.putBool("camHack", useBabyCamHack);
        prefs.end();
    }, LV_EVENT_VALUE_CHANGED, NULL);
    // -------------------------------------------------------

    create_header(scroll_cont, LV_SYMBOL_LIST " SYSTEM & BLE");
    
    lv_obj_t * cont_ad = create_helper_cont(scroll_cont, 40); 
    lv_obj_t * label_ad = create_text_label(cont_ad, "Audio Debug Overlay"); 
    lv_obj_align(label_ad, LV_ALIGN_LEFT_MID, 10, 0); 
    lv_obj_t * switch_ad = lv_switch_create(cont_ad); 
    lv_obj_align(switch_ad, LV_ALIGN_RIGHT_MID, -10, 0); 
    if(audioDebugEnabled) lv_obj_add_state(switch_ad, LV_STATE_CHECKED); 
    lv_obj_add_event_cb(switch_ad, [](lv_event_t* e){
        playToneI2S(800, 100, true);
        audioDebugEnabled = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
        preferences.begin("catmat", false); preferences.putBool("audDbg", audioDebugEnabled); preferences.end();
    }, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * cont_fps = create_helper_cont(scroll_cont, 40);
    lv_obj_t * lbl_fps_title = create_text_label(cont_fps, "Video FPS Anzeige");
    lv_obj_align(lbl_fps_title, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t * sw_fps = lv_switch_create(cont_fps);
    lv_obj_align(sw_fps, LV_ALIGN_RIGHT_MID, -10, 0);
    if(showFps) lv_obj_add_state(sw_fps, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_fps, [](lv_event_t* e) {
        lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
        showFps = lv_obj_has_state(sw, LV_STATE_CHECKED);
        Preferences prefs; prefs.begin("catmat", false); 
        prefs.putBool("showFps", showFps); 
        prefs.end();
        playToneI2S(800, 100, true); 
    }, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * cont_dm = create_helper_cont(scroll_cont, 40); lv_obj_t * label_dark = create_text_label(cont_dm, "Dark Mode"); lv_obj_align(label_dark, LV_ALIGN_LEFT_MID, 10, 0); lv_obj_t * switch_dark = lv_switch_create(cont_dm); lv_obj_align(switch_dark, LV_ALIGN_RIGHT_MID, -10, 0); if(isDarkMode) lv_obj_add_state(switch_dark, LV_STATE_CHECKED); lv_obj_add_event_cb(switch_dark, switch_dark_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    create_header(scroll_cont, LV_SYMBOL_WIFI " FUNKVERBINDUNGEN");
    lv_obj_t * cont_wlan = create_helper_cont(scroll_cont, 40); lbl_sw_wifi = create_text_label(cont_wlan, LV_SYMBOL_WIFI " WLAN"); lv_obj_align(lbl_sw_wifi, LV_ALIGN_LEFT_MID, 10, 0); lv_obj_t * sw_wifi = lv_switch_create(cont_wlan); lv_obj_align(sw_wifi, LV_ALIGN_RIGHT_MID, -10, 0); if(wifiEnabled) lv_obj_add_state(sw_wifi, LV_STATE_CHECKED); lv_obj_add_event_cb(sw_wifi, sw_wifi_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t * cont_mqtt = create_helper_cont(scroll_cont, 40); lv_obj_t * lbl_sw_mqtt = create_text_label(cont_mqtt, LV_SYMBOL_LIST " MQTT"); lv_obj_align(lbl_sw_mqtt, LV_ALIGN_LEFT_MID, 10, 0); lv_obj_t * sw_mqtt = lv_switch_create(cont_mqtt); lv_obj_align(sw_mqtt, LV_ALIGN_RIGHT_MID, -10, 0); if(mqttEnabled) lv_obj_add_state(sw_mqtt, LV_STATE_CHECKED); lv_obj_add_event_cb(sw_mqtt, sw_mqtt_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t * cont_mat = create_helper_cont(scroll_cont, 40); lbl_sw_mat = create_text_label(cont_mat, LV_SYMBOL_BLUETOOTH " BT Matte"); lv_obj_align(lbl_sw_mat, LV_ALIGN_LEFT_MID, 10, 0); lv_obj_t * sw_mat = lv_switch_create(cont_mat); lv_obj_align(sw_mat, LV_ALIGN_RIGHT_MID, -10, 0); if(matEnabled) lv_obj_add_state(sw_mat, LV_STATE_CHECKED); lv_obj_add_event_cb(sw_mat, sw_mat_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t * cont_kip = create_helper_cont(scroll_cont, 40); lbl_sw_kippy = create_text_label(cont_kip, LV_SYMBOL_BLUETOOTH " BT Kippy"); lv_obj_align(lbl_sw_kippy, LV_ALIGN_LEFT_MID, 10, 0); lv_obj_t * sw_kippy = lv_switch_create(cont_kip); lv_obj_align(sw_kippy, LV_ALIGN_RIGHT_MID, -10, 0); if(kippyEnabled) lv_obj_add_state(sw_kippy, LV_STATE_CHECKED); lv_obj_add_event_cb(sw_kippy, sw_kippy_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lbl_master = create_text_label(scroll_cont, ""); lv_label_set_text_fmt(lbl_master, "Prio Matte: %d%%", prioMaster); sl_master = lv_slider_create(scroll_cont); lv_obj_set_size(sl_master, 200, 10); lv_slider_set_range(sl_master, 0, 100); lv_slider_set_value(sl_master, prioMaster, LV_ANIM_OFF); lv_obj_add_event_cb(sl_master, sl_master_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lbl_slave = create_text_label(scroll_cont, ""); lv_label_set_text_fmt(lbl_slave, "Split Kippy/WLAN: %d%%", prioSlave); sl_slave = lv_slider_create(scroll_cont); lv_obj_set_size(sl_slave, 200, 10); lv_slider_set_range(sl_slave, 0, 100); lv_slider_set_value(sl_slave, prioSlave, LV_ANIM_OFF); lv_obj_add_event_cb(sl_slave, sl_slave_cb, LV_EVENT_VALUE_CHANGED, NULL); update_sliders_ui();

    lv_obj_t * btn_ap_setup = lv_btn_create(scroll_cont); lv_obj_set_size(btn_ap_setup, 200, 40); lv_obj_set_style_bg_color(btn_ap_setup, lv_color_hex(0x00A0FF), 0); lv_obj_add_event_cb(btn_ap_setup, btn_ap_setup_cb, LV_EVENT_CLICKED, NULL); lv_obj_t * l_ap = create_white_label(btn_ap_setup, "Setup: WLAN & MQTT"); lv_obj_center(l_ap);
    lv_obj_t * btn_sta_setup = lv_btn_create(scroll_cont); lv_obj_set_size(btn_sta_setup, 200, 40); lv_obj_set_style_bg_color(btn_sta_setup, lv_color_hex(0x00A0FF), 0); lv_obj_add_event_cb(btn_sta_setup, btn_sta_setup_cb, LV_EVENT_CLICKED, NULL); lv_obj_t * l_sta = create_white_label(btn_sta_setup, "Setup: Nur MQTT"); lv_obj_center(l_sta);
    
    create_header(scroll_cont, LV_SYMBOL_SETTINGS " GERAETE SETUP");
    lv_obj_t * cont_mac1 = create_helper_cont(scroll_cont, 60); lbl_setup_mat = create_text_label(cont_mac1, ""); lv_obj_set_width(lbl_setup_mat, 120); lv_label_set_text_fmt(lbl_setup_mat, "Matte:\n%s", savedMatMac.c_str()); lv_obj_align(lbl_setup_mat, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_t * btn_scan_mat = lv_btn_create(cont_mac1); lv_obj_set_size(btn_scan_mat, 90, 35); lv_obj_set_style_bg_color(btn_scan_mat, lv_color_hex(0x333333), 0); lv_obj_align(btn_scan_mat, LV_ALIGN_RIGHT_MID, -5, 0); lv_obj_add_event_cb(btn_scan_mat, btn_scan_mat_cb, LV_EVENT_CLICKED, NULL); lv_obj_t * l_scan_mat = create_white_label(btn_scan_mat, LV_SYMBOL_REFRESH " Suche"); lv_obj_center(l_scan_mat);
    lv_obj_t * cont_mac2 = create_helper_cont(scroll_cont, 60); lbl_setup_kip = create_text_label(cont_mac2, ""); lv_obj_set_width(lbl_setup_kip, 120); lv_label_set_text_fmt(lbl_setup_kip, "Kippy:\n%s", savedKippyMac.c_str()); lv_obj_align(lbl_setup_kip, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_t * btn_scan_kip = lv_btn_create(cont_mac2); lv_obj_set_size(btn_scan_kip, 90, 35); lv_obj_set_style_bg_color(btn_scan_kip, lv_color_hex(0x333333), 0); lv_obj_align(btn_scan_kip, LV_ALIGN_RIGHT_MID, -5, 0); lv_obj_add_event_cb(btn_scan_kip, btn_scan_kip_cb, LV_EVENT_CLICKED, NULL); lv_obj_t * l_scan_kip = create_white_label(btn_scan_kip, LV_SYMBOL_REFRESH " Suche"); lv_obj_center(l_scan_kip);
    
    lv_obj_t * btn_info = lv_btn_create(scroll_cont); lv_obj_set_size(btn_info, 220, 40); lv_obj_set_style_bg_color(btn_info, lv_color_hex(0x222222), 0); lv_obj_add_event_cb(btn_info, easter_egg_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_info = lv_label_create(btn_info); lv_label_set_text(lbl_info, LV_SYMBOL_LIST " INFORMATIONEN"); lv_obj_set_style_text_color(lbl_info, lv_color_hex(0x00A0FF), 0); lv_obj_center(lbl_info);

    lv_obj_t * btn_restart = lv_btn_create(scroll_cont); lv_obj_set_size(btn_restart, 220, 40); lv_obj_set_style_bg_color(btn_restart, lv_color_hex(0xAA0000), 0); lv_obj_add_event_cb(btn_restart, [](lv_event_t* e){ playToneI2S(800, 100, true); ESP.restart(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_restart = create_white_label(btn_restart, LV_SYMBOL_POWER " NEUSTART"); lv_obj_center(lbl_restart);

    text_ble_info = create_text_label(scroll_cont, "Lade BLE Daten..."); lv_label_set_long_mode(text_ble_info, LV_LABEL_LONG_WRAP); lv_obj_set_width(text_ble_info, 220); lv_label_set_recolor(text_ble_info, true); 
    text_sys_info = create_text_label(scroll_cont, "Lade System Daten..."); lv_label_set_long_mode(text_sys_info, LV_LABEL_LONG_WRAP); lv_obj_set_width(text_sys_info, 220);
    lv_obj_t * spacer = lv_obj_create(scroll_cont); lv_obj_set_size(spacer, 10, 40); lv_obj_set_style_bg_opa(spacer, 0, 0); lv_obj_set_style_border_width(spacer, 0, 0);

    // QR CODE OVERLAY
    qr_overlay = lv_obj_create(scr); 
    lv_obj_set_size(qr_overlay, 280, 280); 
    lv_obj_center(qr_overlay); 
    lv_obj_set_style_bg_color(qr_overlay, lv_color_hex(0x222222), 0); 
    lv_obj_set_style_border_color(qr_overlay, lv_color_hex(0x00A0FF), 0); 
    lv_obj_set_style_border_width(qr_overlay, 2, 0); 
    lv_obj_add_flag(qr_overlay, LV_OBJ_FLAG_HIDDEN); 
    
    qr_screenshot = lv_qrcode_create(qr_overlay); 
    lv_qrcode_set_size(qr_screenshot, 130); 
    lv_qrcode_set_dark_color(qr_screenshot, lv_color_hex(0x00A0FF)); 
    lv_qrcode_set_light_color(qr_screenshot, lv_color_hex(0x222222)); 
    lv_obj_set_style_border_width(qr_screenshot, 0, 0);
    lv_obj_set_style_pad_all(qr_screenshot, 0, 0);
    lv_obj_align(qr_screenshot, LV_ALIGN_TOP_MID, 0, 20);
    
    lbl_qr_ip = lv_label_create(qr_overlay);
    lv_obj_align_to(lbl_qr_ip, qr_screenshot, LV_ALIGN_OUT_BOTTOM_MID, 0, 15);
    lv_obj_set_style_text_color(lbl_qr_ip, lv_color_white(), 0);
    lv_label_set_text(lbl_qr_ip, "Link: ---");

    btn_stop_screenshot = lv_btn_create(qr_overlay); 
    lv_obj_set_size(btn_stop_screenshot, 160, 40); 
    lv_obj_align(btn_stop_screenshot, LV_ALIGN_BOTTOM_MID, 0, -15); 
    lv_obj_set_style_bg_color(btn_stop_screenshot, lv_color_hex(0xAA0000), 0); 
    lv_obj_add_event_cb(btn_stop_screenshot, btn_stop_screenshot_cb, LV_EVENT_CLICKED, NULL); 
    lv_obj_t * l_stop = create_white_label(btn_stop_screenshot, LV_SYMBOL_STOP " Beenden"); 
    lv_obj_center(l_stop);

    // SCAN OVERLAY
    scan_overlay = lv_obj_create(scr); lv_obj_set_size(scan_overlay, lv_pct(100), lv_pct(100)); lv_obj_set_style_bg_color(scan_overlay, lv_color_hex(0x111111), 0); lv_obj_set_style_bg_opa(scan_overlay, 255, 0); lv_obj_set_style_border_width(scan_overlay, 0, 0); lv_obj_add_flag(scan_overlay, LV_OBJ_FLAG_HIDDEN);
    scan_spinner = lv_spinner_create(scan_overlay); lv_obj_set_size(scan_spinner, 24, 24); lv_obj_align(scan_spinner, LV_ALIGN_TOP_MID, -70, 15);
    lbl_scan_info = lv_label_create(scan_overlay); lv_label_set_text(lbl_scan_info, "Suche..."); lv_obj_set_style_text_color(lbl_scan_info, lv_color_white(), 0); lv_obj_align(lbl_scan_info, LV_ALIGN_TOP_MID, 25, 18);
    dd_scan_results = lv_roller_create(scan_overlay); lv_obj_set_width(dd_scan_results, 260); lv_obj_align(dd_scan_results, LV_ALIGN_TOP_MID, 0, 55); lv_obj_set_style_text_font(dd_scan_results, &lv_font_montserrat_12, 0); lv_roller_set_visible_row_count(dd_scan_results, 4); 
    btn_rescan_mac = lv_btn_create(scan_overlay); lv_obj_set_size(btn_rescan_mac, 110, 40); lv_obj_align(btn_rescan_mac, LV_ALIGN_BOTTOM_MID, -60, -60); lv_obj_set_style_bg_color(btn_rescan_mac, lv_color_hex(0x555555), 0); lv_obj_add_event_cb(btn_rescan_mac, btn_rescan_mac_cb, LV_EVENT_CLICKED, NULL); lv_obj_t * l_rescan = create_white_label(btn_rescan_mac, "Neu"); lv_obj_center(l_rescan); 
    btn_continue_mac = lv_btn_create(scan_overlay); lv_obj_set_size(btn_continue_mac, 110, 40); lv_obj_align(btn_continue_mac, LV_ALIGN_BOTTOM_MID, 60, -60); lv_obj_set_style_bg_color(btn_continue_mac, lv_color_hex(0xFF8800), 0); lv_obj_add_event_cb(btn_continue_mac, btn_continue_mac_cb, LV_EVENT_CLICKED, NULL); lv_obj_t * l_continue = create_white_label(btn_continue_mac, "Weiter"); lv_obj_center(l_continue); 
    btn_cancel_mac = lv_btn_create(scan_overlay); lv_obj_set_size(btn_cancel_mac, 110, 40); lv_obj_align(btn_cancel_mac, LV_ALIGN_BOTTOM_MID, -60, -10); lv_obj_set_style_bg_color(btn_cancel_mac, lv_color_hex(0xAA0000), 0); lv_obj_add_event_cb(btn_cancel_mac, btn_cancel_mac_cb, LV_EVENT_CLICKED, NULL); lv_obj_t * l_cancel = create_white_label(btn_cancel_mac, "Abbruch"); lv_obj_center(l_cancel); 
    btn_save_mac = lv_btn_create(scan_overlay); lv_obj_set_size(btn_save_mac, 110, 40); lv_obj_align(btn_save_mac, LV_ALIGN_BOTTOM_MID, 60, -10); lv_obj_set_style_bg_color(btn_save_mac, lv_color_hex(0x00A0FF), 0); lv_obj_add_event_cb(btn_save_mac, btn_save_mac_cb, LV_EVENT_CLICKED, NULL); lv_obj_t * l_save = create_white_label(btn_save_mac, "Sichern"); lv_obj_center(l_save);

    return scr;
}

void ViewSettings::update() {
    if (gui.getCurrentScreen() != SCREEN_SETTINGS) return;

    if (!lv_obj_has_flag(scan_overlay, LV_OBJ_FLAG_HIDDEN)) {
        if (isSetupScanning) {
            lv_obj_clear_flag(scan_spinner, LV_OBJ_FLAG_HIDDEN);
            uint32_t restzeit = (45000 - (millis() - setupScanStartTime)) / 1000;
            if (restzeit > 45) restzeit = 0; 
            lv_label_set_text_fmt(lbl_scan_info, "Sucht... (%ds)\nGefunden: %d", restzeit, scanResultCount);
            if (scanResultCount > 0) lv_obj_clear_flag(btn_save_mac, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(btn_save_mac, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(scan_spinner, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text_fmt(lbl_scan_info, "Suche pausiert.\nGefunden: %d", scanResultCount);
            if (scanResultCount > 0) lv_obj_clear_flag(btn_save_mac, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(btn_save_mac, LV_OBJ_FLAG_HIDDEN);
        }
        
        static int lastRenderedCount = -1;
        if (lastRenderedCount != scanResultCount || requestRollerUpdate) {
            requestRollerUpdate = false; 
            uint16_t current_sel = lv_roller_get_selected(dd_scan_results);
            lv_roller_set_options(dd_scan_results, scanOptionsStr, LV_ROLLER_MODE_NORMAL);
            if(current_sel < scanResultCount && scanResultCount > 0) lv_roller_set_selected(dd_scan_results, current_sel, LV_ANIM_OFF);
            lastRenderedCount = scanResultCount;
        }
    }

    static uint32_t lastInfoUpdate = 0;
    if (millis() - lastInfoUpdate > 1000) { 
        lastInfoUpdate = millis();
        static char bleInfoBuf[512];
        snprintf(bleInfoBuf, sizeof(bleInfoBuf), "Matte: %s (%d dBm)\nIntervall: %d ms\n\nKippy: %s\nSignal: %d dBm\n\nWLAN HA: %s", 
            matEnabled ? (connected ? "Verbunden" : "Getrennt") : "Deaktiviert", connected ? (pClient ? pClient->getRssi() : 0) : 0, (int)avgInterval, 
            kippyEnabled ? ((lastCatSeenTime == 0 || millis() - lastCatSeenTime > 30000) ? "Abwesend" : "Aktiv") : "Deaktiviert", 
            (kippyEnabled && lastCatSeenTime != 0 && (millis() - lastCatSeenTime <= 30000)) ? catRssi : 0, wifiEnabled ? "Aktiv" : "Deaktiviert");
        lv_label_set_text(text_ble_info, bleInfoBuf);

        uint32_t sec = (millis() - startTime) / 1000;
        static char sysInfoBuf[512];
        snprintf(sysInfoBuf, sizeof(sysInfoBuf), "WLAN: %s (%d dBm)\nIP: %s\nHA Alarm: %s\n\nESP Akku: %d %%\nSpeicher: %d KB\nUptime: %02dh %02dm %02ds", 
            WiFi.status() == WL_CONNECTED ? "Verbunden" : "Getrennt", WiFi.RSSI(), WiFi.localIP().toString().c_str(), alarmActive ? "Aktiv" : "Standby", 
            batteryPercent, ESP.getFreeHeap() / 1024, sec / 3600, (sec % 3600) / 60, sec % 60);
        lv_label_set_text(text_sys_info, sysInfoBuf);
    }
}