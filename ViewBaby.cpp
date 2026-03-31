#include "ViewBaby.h"
#include "GuiManager.h"
#include "SharedData.h"
#include "SystemLogic.h" 
#include <WiFi.h>

static lv_obj_t * top_bar_obj;
static lv_obj_t * top_label_clock; 
static lv_obj_t * top_icon_ble; 
static lv_obj_t * top_icon_ha; 
static lv_obj_t * top_label_bat; 

static int s_top_clock_min = -1;
static int s_top_iconBleState = -1;
static int s_top_wifiStatus = -1;
static int s_top_batteryPercent = -1;

static lv_obj_t * cam_image_obj; 
static lv_obj_t * cam_touch_overlay; 
static lv_obj_t * lbl_cam_status;
static lv_obj_t * lbl_play_icon; 
static lv_obj_t * btn_ptt; 
static lv_obj_t * btn_mute; 
static lv_obj_t * lbl_mute;

static lv_obj_t * btn_audio_toggle;
static lv_obj_t * lbl_audio_icon;

static lv_obj_t * btn_fs;
static lv_obj_t * lbl_fs;

static lv_obj_t * lbl_fps; // Das neue FPS Label fuer den Normalmodus

static lv_obj_t * debug_panel; 
static lv_obj_t * debug_label;

static lv_obj_t * fs_black_overlay; // Der schwarze Vorhang fuer das Vollbild

static uint32_t s_lastBtnColor = 0; 
static int s_lastBtnStateForText = -1;

static void btn_audio_toggle_event_cb(lv_event_t * e) {
    playToneI2S(800, 100, true); 
    requestBabyStream = !requestBabyStream; 
}

static void btn_fs_event_cb(lv_event_t * e) {
    if (!isStreamActive) return; 
    
    playToneI2S(800, 100, true); 
    
    // Vorhang zuziehen und sofort zeichnen lassen, um flackern zu vermeiden
    lv_obj_clear_flag(fs_black_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_refr_now(NULL); 
    
    vidFSMode = true; 
}

static void cam_image_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        playToneI2S(800, 100, true); 
        
        isStreamActive = !isStreamActive; 
        requestBabyStream = isStreamActive; 
        
        if (isStreamActive) {
            lv_obj_add_flag(lbl_play_icon, LV_OBJ_FLAG_HIDDEN); 
            lv_obj_clear_flag(lbl_cam_status, LV_OBJ_FLAG_HIDDEN); 
            lv_label_set_text(lbl_cam_status, LV_SYMBOL_WIFI " Verbinde...");
        } else {
            lv_obj_clear_flag(lbl_play_icon, LV_OBJ_FLAG_HIDDEN); 
            lv_obj_clear_flag(lbl_cam_status, LV_OBJ_FLAG_HIDDEN); 
            lv_label_set_text(lbl_cam_status, LV_SYMBOL_PAUSE " Pausiert");
        }
    } 
    else if (code == LV_EVENT_LONG_PRESSED && audioDebugEnabled) {
        playToneI2S(1000, 100, true); 
        lv_obj_clear_flag(debug_panel, LV_OBJ_FLAG_HIDDEN);
        addAudioLog("Phase 1: Audio bereit.");
    }
}

static void btn_ptt_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_PRESSED) {
        playToneI2S(800, 100, true); 
        lv_obj_set_style_bg_color(btn_ptt, lv_color_hex(0x00FF00), 0);
        lv_label_set_text(lv_obj_get_child(btn_ptt, 0), LV_SYMBOL_AUDIO " TALK");
    }
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_style_bg_color(btn_ptt, lv_color_hex(0x4FA5D6), 0);
        lv_label_set_text(lv_obj_get_child(btn_ptt, 0), LV_SYMBOL_MUTE " PTT");
    }
}

static void btn_mute_event_cb(lv_event_t * e) { 
    playToneI2S(800, 100, true); 
    if (babyAlarmActive) {
        if (!babyMuted) babyMuted = true; 
        else { babyAlarmActive = false; babyMuted = false; }
    } else {
        isBabyArmed = !isBabyArmed;
    }
}

void ViewBaby_SetImage(const void* src) {
    if (gui.getCurrentScreen() != SCREEN_BABY) return; 
    
    if (cam_image_obj != nullptr) {
        lv_image_dsc_t* dsc = (lv_image_dsc_t*)src;
        if (dsc->header.w > 0 && dsc->header.h > 0) {
            uint32_t scale_x = (312 * 256) / dsc->header.w;
            uint32_t scale_y = (176 * 256) / dsc->header.h;
            uint32_t final_scale = (scale_x < scale_y) ? scale_x : scale_y;
            
            lv_image_set_scale(cam_image_obj, final_scale);
            lv_image_set_pivot(cam_image_obj, dsc->header.w / 2, dsc->header.h / 2);
            lv_image_set_inner_align(cam_image_obj, LV_IMAGE_ALIGN_CENTER);
            lv_image_set_src(cam_image_obj, src);

            if (lbl_cam_status != nullptr) lv_obj_add_flag(lbl_cam_status, LV_OBJ_FLAG_HIDDEN); 
        } else {
            if (lbl_cam_status != nullptr) {
                lv_obj_clear_flag(lbl_cam_status, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(lbl_cam_status, "Fehler: Bildmasse 0x0");
            }
            ViewBaby_StopStreamOnError();
        }
    }
}

void ViewBaby_SetStatus(const char* text) {
    if (gui.getCurrentScreen() != SCREEN_BABY) return; 
    if (lbl_cam_status != nullptr) {
        lv_obj_clear_flag(lbl_cam_status, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lbl_cam_status, text);
    }
}

void ViewBaby_StopStreamOnError() {
    requestBabyStream = false; 
    isStreamActive = false; 
    if (gui.getCurrentScreen() != SCREEN_BABY) return;
    
    if (lbl_play_icon != nullptr) {
        lv_obj_clear_flag(lbl_play_icon, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t* ViewBaby::build() {
    s_top_clock_min = -1; s_top_iconBleState = -1; s_top_wifiStatus = -1; s_top_batteryPercent = -1;
    s_lastBtnColor = 0; s_lastBtnStateForText = -1;
    
    requestBabyStream = false; 
    isStreamActive = false; 

    lv_obj_t* scr = lv_obj_create(NULL);
    if (!scr) return nullptr;

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(scr, GuiManager::gestureEventWrapper, LV_EVENT_GESTURE, &gui);

    top_bar_obj = lv_obj_create(scr);
    lv_obj_set_size(top_bar_obj, 360, 55); 
    lv_obj_align(top_bar_obj, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top_bar_obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(top_bar_obj, 150, 0); 
    lv_obj_set_style_border_width(top_bar_obj, 0, 0);
    lv_obj_clear_flag(top_bar_obj, LV_OBJ_FLAG_SCROLLABLE);

    top_label_clock = lv_label_create(top_bar_obj); 
    lv_obj_set_style_text_font(top_label_clock, &lv_font_montserrat_14, 0); 
    lv_obj_set_style_text_color(top_label_clock, lv_color_hex(0xAAAAAA), 0); 
    lv_label_set_text(top_label_clock, "--:--"); 
    lv_obj_align(top_label_clock, LV_ALIGN_TOP_MID, 0, 5); 
    
    top_icon_ble = lv_label_create(top_bar_obj); 
    lv_label_set_text(top_icon_ble, LV_SYMBOL_BLUETOOTH); 
    lv_obj_align(top_icon_ble, LV_ALIGN_TOP_MID, -60, 25); 
    
    top_icon_ha = lv_label_create(top_bar_obj); 
    lv_label_set_text(top_icon_ha, LV_SYMBOL_HOME); 
    lv_obj_align(top_icon_ha, LV_ALIGN_TOP_MID, -20, 25); 
    
    top_label_bat = lv_label_create(top_bar_obj); 
    lv_label_set_text(top_label_bat, LV_SYMBOL_BATTERY_FULL " 100%"); 
    lv_obj_set_style_text_color(top_label_bat, lv_color_white(), 0); 
    lv_obj_align(top_label_bat, LV_ALIGN_TOP_MID, 40, 25); 

    cam_image_obj = lv_image_create(scr);
    lv_obj_set_size(cam_image_obj, 320, 180); 
    lv_obj_align(cam_image_obj, LV_ALIGN_CENTER, 0, 0); 
    lv_obj_set_style_bg_color(cam_image_obj, lv_color_hex(0x00A0FF), 0);
    lv_obj_set_style_bg_opa(cam_image_obj, 255, 0);
    lv_image_set_inner_align(cam_image_obj, LV_IMAGE_ALIGN_DEFAULT); 

    lbl_fps = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_fps, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_fps, lv_color_hex(0x00FF00), 0);
    lv_obj_align_to(lbl_fps, cam_image_obj, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_obj_add_flag(lbl_fps, LV_OBJ_FLAG_HIDDEN); 

    lbl_play_icon = lv_label_create(cam_image_obj);
    lv_label_set_text(lbl_play_icon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(lbl_play_icon, &lv_font_montserrat_48, 0); 
    lv_obj_set_style_text_color(lbl_play_icon, lv_color_white(), 0);
    lv_obj_set_style_bg_color(lbl_play_icon, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lbl_play_icon, 150, 0); 
    lv_obj_set_style_pad_all(lbl_play_icon, 15, 0);
    lv_obj_set_style_radius(lbl_play_icon, 40, 0); 
    lv_obj_align(lbl_play_icon, LV_ALIGN_CENTER, 5, 0); 

    lbl_cam_status = lv_label_create(scr);
    lv_label_set_long_mode(lbl_cam_status, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(lbl_cam_status, 260); 
    lv_obj_set_style_text_align(lbl_cam_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl_cam_status, LV_SYMBOL_IMAGE " Klick fuer Stream");
    lv_obj_set_style_text_color(lbl_cam_status, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align_to(lbl_cam_status, cam_image_obj, LV_ALIGN_OUT_TOP_MID, 0, -5); 

    cam_touch_overlay = lv_obj_create(scr);
    lv_obj_set_size(cam_touch_overlay, 320, 180);
    lv_obj_align(cam_touch_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(cam_touch_overlay, 0, 0); 
    lv_obj_set_style_border_width(cam_touch_overlay, 0, 0);
    lv_obj_clear_flag(cam_touch_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cam_touch_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(cam_touch_overlay, LV_OBJ_FLAG_GESTURE_BUBBLE); 
    lv_obj_add_event_cb(cam_touch_overlay, cam_image_event_cb, LV_EVENT_ALL, NULL);

    btn_audio_toggle = lv_btn_create(scr);
    lv_obj_set_size(btn_audio_toggle, 60, 60); 
    lv_obj_align_to(btn_audio_toggle, cam_touch_overlay, LV_ALIGN_BOTTOM_LEFT, 5, -5);
    lv_obj_set_style_bg_color(btn_audio_toggle, lv_color_hex(0x555555), 0);
    lv_obj_set_style_bg_opa(btn_audio_toggle, 180, 0); 
    lv_obj_set_style_radius(btn_audio_toggle, 30, 0); 
    lv_obj_set_style_border_width(btn_audio_toggle, 0, 0);
    lv_obj_set_style_pad_all(btn_audio_toggle, 0, 0); // Perfekte Zentrierung
    lv_obj_add_event_cb(btn_audio_toggle, btn_audio_toggle_event_cb, LV_EVENT_CLICKED, NULL);

    lbl_audio_icon = lv_label_create(btn_audio_toggle);
    lv_label_set_text(lbl_audio_icon, LV_SYMBOL_MUTE);
    lv_obj_set_style_transform_scale(lbl_audio_icon, 384, 0); 
    lv_obj_set_style_text_color(lbl_audio_icon, lv_color_white(), 0);
    lv_obj_center(lbl_audio_icon);

    btn_fs = lv_btn_create(scr);
    lv_obj_set_size(btn_fs, 60, 60); 
    lv_obj_align_to(btn_fs, cam_touch_overlay, LV_ALIGN_BOTTOM_RIGHT, -5, -5);
    lv_obj_set_style_bg_color(btn_fs, lv_color_hex(0x555555), 0);
    lv_obj_set_style_bg_opa(btn_fs, 180, 0); 
    lv_obj_set_style_radius(btn_fs, 30, 0); 
    lv_obj_set_style_border_width(btn_fs, 0, 0);
    lv_obj_set_style_pad_all(btn_fs, 0, 0); // Perfekte Zentrierung
    lv_obj_add_event_cb(btn_fs, btn_fs_event_cb, LV_EVENT_CLICKED, NULL);

    lbl_fs = lv_label_create(btn_fs);
    lv_label_set_text(lbl_fs, "[ ]"); 
    lv_obj_set_style_transform_scale(lbl_fs, 384, 0); 
    lv_obj_set_style_text_color(lbl_fs, lv_color_white(), 0);
    lv_obj_center(lbl_fs);

    debug_panel = lv_obj_create(scr);
    lv_obj_set_size(debug_panel, 280, 200);
    lv_obj_align(debug_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(debug_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(debug_panel, 220, 0); 
    lv_obj_add_flag(debug_panel, LV_OBJ_FLAG_HIDDEN); 
    lv_obj_add_flag(debug_panel, LV_OBJ_FLAG_CLICKABLE); 
    
    lv_obj_add_event_cb(debug_panel, [](lv_event_t * e) {
        lv_obj_t * panel = (lv_obj_t *)lv_event_get_target(e);
        playToneI2S(600, 100, true); 
        lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, NULL);
    
    debug_label = lv_label_create(debug_panel);
    lv_obj_set_width(debug_label, 250);
    lv_label_set_long_mode(debug_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(debug_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(debug_label, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(debug_label, "Warte auf Logs...\n(Tippen zum Schliessen)");

    btn_ptt = lv_btn_create(scr);
    lv_obj_set_size(btn_ptt, 100, 40);
    lv_obj_align(btn_ptt, LV_ALIGN_BOTTOM_MID, -55, -35); 
    lv_obj_set_style_radius(btn_ptt, 20, 0); 
    lv_obj_set_style_bg_color(btn_ptt, lv_color_hex(0x4FA5D6), 0);
    lv_obj_add_event_cb(btn_ptt, btn_ptt_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t * lbl_ptt = lv_label_create(btn_ptt);
    lv_label_set_text(lbl_ptt, LV_SYMBOL_MUTE " PTT");
    lv_obj_center(lbl_ptt);

    btn_mute = lv_btn_create(scr);
    lv_obj_set_size(btn_mute, 100, 40);
    lv_obj_align(btn_mute, LV_ALIGN_BOTTOM_MID, 55, -35); 
    lv_obj_set_style_radius(btn_mute, 20, 0); 
    lv_obj_add_event_cb(btn_mute, btn_mute_event_cb, LV_EVENT_CLICKED, NULL);
    
    lbl_mute = lv_label_create(btn_mute);
    lv_label_set_text(lbl_mute, "LADE...");
    lv_obj_center(lbl_mute);

    // Der schwarze Vorhang fuer das Vollbild
    fs_black_overlay = lv_obj_create(scr);
    lv_obj_set_size(fs_black_overlay, 360, 360);
    lv_obj_center(fs_black_overlay);
    lv_obj_set_style_bg_color(fs_black_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(fs_black_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(fs_black_overlay, 0, 0);
    lv_obj_clear_flag(fs_black_overlay, LV_OBJ_FLAG_CLICKABLE); 
    lv_obj_add_flag(fs_black_overlay, LV_OBJ_FLAG_HIDDEN); 

    return scr;
}

void ViewBaby::update() {
    if (gui.getCurrentScreen() != SCREEN_BABY) return;

    // Vorhang wieder oeffnen, wenn Vollbild beendet wurde
    if (!vidFSMode && !lv_obj_has_flag(fs_black_overlay, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(fs_black_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    
    // FPS im Normalmodus anzeigen
    if (showFps && isStreamActive) {
        lv_obj_clear_flag(lbl_fps, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(lbl_fps, "FPS: %d", currentFps);
    } else {
        lv_obj_add_flag(lbl_fps, LV_OBJ_FLAG_HIDDEN);
    }

    int current_ui_state = 0; 
    if (!requestBabyStream) current_ui_state = 0;
    else if (babyStreamStatus == 2) current_ui_state = 2;
    else current_ui_state = 1; 
    
    static int last_ui_state = -1;
    if (last_ui_state != current_ui_state) {
        if (current_ui_state == 0) {
            lv_obj_set_style_bg_color(btn_audio_toggle, lv_color_hex(0x555555), 0);
            lv_label_set_text(lbl_audio_icon, LV_SYMBOL_MUTE);
        } else if (current_ui_state == 1) {
            lv_obj_set_style_bg_color(btn_audio_toggle, lv_color_hex(0xFF0000), 0);
            lv_label_set_text(lbl_audio_icon, LV_SYMBOL_VOLUME_MAX);
        } else if (current_ui_state == 2) {
            lv_obj_set_style_bg_color(btn_audio_toggle, lv_color_hex(0x00AA00), 0); 
            lv_label_set_text(lbl_audio_icon, LV_SYMBOL_VOLUME_MAX);
        }
        last_ui_state = current_ui_state;
    }

    if (!lv_obj_has_flag(debug_panel, LV_OBJ_FLAG_HIDDEN)) {
        String fullLog = "";
        for(int i=0; i<10; i++) {
            int idx = (audioLogIdx + i) % 10;
            if(audioLogs[idx].length() > 0) {
                fullLog += audioLogs[idx] + "\n";
            }
        }
        lv_label_set_text(debug_label, fullLog.c_str());
    }
    
    bool fastBlink = (millis() % 600 < 300);

    int curBtnState = babyAlarmActive ? (babyMuted ? 4 : 3) : (isBabyArmed ? 2 : 1);
    uint32_t targetBtnColor = 0x555555; const char* targetBtnText = "";

    switch(curBtnState) {
        case 1: targetBtnText = LV_SYMBOL_BELL " OFF"; targetBtnColor = 0x555555; break;
        case 2: targetBtnText = LV_SYMBOL_BELL " ON";  targetBtnColor = 0x00A0FF; break;
        case 3: targetBtnText = LV_SYMBOL_MUTE " MUTE";     targetBtnColor = fastBlink ? 0xFF0000 : 0x660000; break;
        case 4: targetBtnText = LV_SYMBOL_REFRESH " RESET";  targetBtnColor = 0xFF8800; break;
    }

    if (s_lastBtnStateForText != curBtnState) {
        lv_label_set_text(lbl_mute, targetBtnText);
        s_lastBtnStateForText = curBtnState;
    }
    if (s_lastBtnColor != targetBtnColor) {
        lv_obj_set_style_bg_color(btn_mute, lv_color_hex(targetBtnColor), 0);
        s_lastBtnColor = targetBtnColor;
    }

    if (timeSynced) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 10)) {
            if (s_top_clock_min != timeinfo.tm_min) {
                lv_label_set_text_fmt(top_label_clock, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                s_top_clock_min = timeinfo.tm_min;
            }
        }
    }

    int curIconBleState = (matEnabled || kippyEnabled) ? (matEnabled && !connected ? (fastBlink ? 1 : 2) : 3) : 0;
    if (s_top_iconBleState != curIconBleState) {
        lv_color_t ble_col = (curIconBleState == 1) ? lv_color_hex(0xFF8800) : ((curIconBleState == 3) ? lv_color_hex(0x00FF00) : lv_color_hex(0x555555));
        lv_obj_set_style_text_color(top_icon_ble, ble_col, 0); s_top_iconBleState = curIconBleState;
    }
            
    int curWifiStatus = !wifiEnabled ? 0 : (WiFi.status() == WL_CONNECTED ? 1 : 2);
    if (s_top_wifiStatus != curWifiStatus) {
        lv_color_t wifi_col = (curWifiStatus == 0) ? lv_color_hex(0x555555) : (curWifiStatus == 1 ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000));
        lv_obj_set_style_text_color(top_icon_ha, wifi_col, 0); s_top_wifiStatus = curWifiStatus;
    }
            
    if (s_top_batteryPercent != batteryPercent) {
        const char * bat_icon = LV_SYMBOL_BATTERY_EMPTY;
        if (batteryPercent >= 80) bat_icon = LV_SYMBOL_BATTERY_FULL; else if (batteryPercent >= 60) bat_icon = LV_SYMBOL_BATTERY_3;
        else if (batteryPercent >= 40) bat_icon = LV_SYMBOL_BATTERY_2; else if (batteryPercent >= 15) bat_icon = LV_SYMBOL_BATTERY_1;
        lv_label_set_text_fmt(top_label_bat, "%s %d%%", bat_icon, batteryPercent);
        lv_obj_set_style_text_color(top_label_bat, batteryPercent < 15 ? lv_color_hex(0xFF0000) : lv_color_white(), 0); 
        s_top_batteryPercent = batteryPercent;
    }
}