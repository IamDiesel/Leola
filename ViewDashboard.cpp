#include "ViewDashboard.h"
#include "GuiManager.h"
#include "SharedData.h"
#include <WiFi.h>

static lv_obj_t * dash_label_clock; 
static lv_obj_t * dash_icon_ble; 
static lv_obj_t * dash_icon_ha; 
static lv_obj_t * dash_label_bat; 

static lv_obj_t * btnBaby;
static lv_obj_t * lblBaby;
static lv_obj_t * btnCat;
static lv_obj_t * lblCat;

static int s_clock_min = -1;
static int s_iconBleState = -1;
static int s_wifiStatus = -1;
static int s_batteryPercent = -1;

static bool s_lastBabyAlarm = false;
static bool s_lastBabyMuted = false;
static bool s_lastBlinkBaby = false;
static bool s_lastCatAlarm = false;
static bool s_lastCatMuted = false;
static bool s_lastBlinkCat = false;

static void set_scr_opaque_black(lv_obj_t * scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

static void btn_baby_event_cb(lv_event_t * e) {
    lv_indev_t * indev = lv_indev_get_act();
    if (indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) return;

    playToneI2S(800, 100, true);
    if (babyAlarmActive && !babyMuted) babyMuted = true;
    else gui.switchScreen(SCREEN_BABY, LV_SCR_LOAD_ANIM_NONE); 
}

static void btn_cat_event_cb(lv_event_t * e) {
    lv_indev_t * indev = lv_indev_get_act();
    if (indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) return;

    playToneI2S(800, 100, true);
    if (alarmActive && !muted) muted = true;
    else gui.switchScreen(SCREEN_CATMAT, LV_SCR_LOAD_ANIM_NONE); 
}

lv_obj_t* ViewDashboard::build() {
    s_clock_min = -1; s_iconBleState = -1; s_wifiStatus = -1; s_batteryPercent = -1;
    s_lastBabyAlarm = false; s_lastBabyMuted = false; s_lastBlinkBaby = false;
    s_lastCatAlarm = false; s_lastCatMuted = false; s_lastBlinkCat = false;

    lv_obj_t* scr = lv_obj_create(NULL);
    if (!scr) return nullptr;

    set_scr_opaque_black(scr);
    
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE); 
    lv_obj_add_event_cb(scr, GuiManager::gestureEventWrapper, LV_EVENT_GESTURE, &gui);

    btnBaby = lv_btn_create(scr);
    lv_obj_set_size(btnBaby, lv_pct(50), lv_pct(100));
    lv_obj_align(btnBaby, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btnBaby, lv_color_hex(0x4FA5D6), 0); 
    lv_obj_set_style_radius(btnBaby, 0, 0);
    lv_obj_set_style_border_width(btnBaby, 0, 0);
    lv_obj_add_event_cb(btnBaby, btn_baby_event_cb, LV_EVENT_CLICKED, NULL);

    lblBaby = lv_label_create(btnBaby);
    lv_label_set_text(lblBaby, "Baby\nMonitor");
    lv_obj_center(lblBaby);
    lv_obj_set_style_text_color(lblBaby, lv_color_white(), 0);
    lv_obj_set_style_text_font(lblBaby, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(lblBaby, LV_TEXT_ALIGN_CENTER, 0);

    btnCat = lv_btn_create(scr);
    lv_obj_set_size(btnCat, lv_pct(50), lv_pct(100));
    lv_obj_align(btnCat, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btnCat, lv_color_hex(0xE67E22), 0); 
    lv_obj_set_style_radius(btnCat, 0, 0);
    lv_obj_set_style_border_width(btnCat, 0, 0);
    lv_obj_add_event_cb(btnCat, btn_cat_event_cb, LV_EVENT_CLICKED, NULL);

    lblCat = lv_label_create(btnCat);
    lv_label_set_text(lblCat, "CatMat");
    lv_obj_center(lblCat);
    lv_obj_set_style_text_color(lblCat, lv_color_white(), 0);
    lv_obj_set_style_text_font(lblCat, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(lblCat, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t * top_bar = lv_obj_create(scr);
    lv_obj_set_size(top_bar, 360, 55); 
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(top_bar, 150, 0); 
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_CLICKABLE);

    dash_label_clock = lv_label_create(top_bar); 
    lv_obj_set_style_text_font(dash_label_clock, &lv_font_montserrat_14, 0); 
    lv_obj_set_style_text_color(dash_label_clock, lv_color_hex(0xAAAAAA), 0); 
    lv_label_set_text(dash_label_clock, "--:--"); 
    lv_obj_align(dash_label_clock, LV_ALIGN_TOP_MID, 0, 5); 
    
    dash_icon_ble = lv_label_create(top_bar); 
    lv_label_set_text(dash_icon_ble, LV_SYMBOL_BLUETOOTH); 
    lv_obj_align(dash_icon_ble, LV_ALIGN_TOP_MID, -60, 25); 
    
    dash_icon_ha = lv_label_create(top_bar); 
    lv_label_set_text(dash_icon_ha, LV_SYMBOL_HOME); 
    lv_obj_align(dash_icon_ha, LV_ALIGN_TOP_MID, -20, 25); 
    
    dash_label_bat = lv_label_create(top_bar); 
    lv_label_set_text(dash_label_bat, LV_SYMBOL_BATTERY_FULL " 100%"); 
    lv_obj_set_style_text_color(dash_label_bat, lv_color_white(), 0); 
    lv_obj_align(dash_label_bat, LV_ALIGN_TOP_MID, 40, 25); 

    lv_obj_t* swipe_blocker = lv_obj_create(scr);
    lv_obj_set_size(swipe_blocker, 360, 80);
    lv_obj_align(swipe_blocker, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(swipe_blocker, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(swipe_blocker, 150, 0);
    lv_obj_set_style_border_width(swipe_blocker, 0, 0);
    lv_obj_add_flag(swipe_blocker, LV_OBJ_FLAG_CLICKABLE); 
    lv_obj_add_flag(swipe_blocker, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(swipe_blocker, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_blocker = lv_label_create(swipe_blocker);
    lv_label_set_text(lbl_blocker, LV_SYMBOL_SETTINGS "   " LV_SYMBOL_UP);
    lv_obj_set_style_text_color(lbl_blocker, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(lbl_blocker, LV_ALIGN_TOP_MID, 0, 15);

    return scr;
}

void ViewDashboard::update() {
    if (gui.getCurrentScreen() != SCREEN_DASHBOARD) return;

    bool fastBlink = (millis() % 1000 < 500);

    if (babyAlarmActive && !babyMuted) {
        if (fastBlink != s_lastBlinkBaby || !s_lastBabyAlarm || s_lastBabyMuted) {
            lv_obj_set_style_bg_color(btnBaby, fastBlink ? lv_color_hex(0xFF0000) : lv_color_hex(0xAA0000), 0);
            lv_label_set_text(lblBaby, LV_SYMBOL_BELL "\nBABY\nALARM!\n(Klick = Mute)");
            s_lastBabyAlarm = true; s_lastBabyMuted = false; s_lastBlinkBaby = fastBlink;
        }
    } else {
        if (s_lastBabyAlarm || s_lastBabyMuted != babyMuted) {
            lv_obj_set_style_bg_color(btnBaby, lv_color_hex(0x4FA5D6), 0);
            lv_label_set_text(lblBaby, "Baby\nMonitor");
            s_lastBabyAlarm = false; s_lastBabyMuted = babyMuted;
        }
    }

    if (alarmActive && !muted) {
        if (fastBlink != s_lastBlinkCat || !s_lastCatAlarm || s_lastCatMuted) {
            lv_obj_set_style_bg_color(btnCat, fastBlink ? lv_color_hex(0xFF0000) : lv_color_hex(0xAA0000), 0);
            lv_label_set_text(lblCat, LV_SYMBOL_BELL "\nCATMAT\nALARM!\n(Klick = Mute)");
            s_lastCatAlarm = true; s_lastCatMuted = false; s_lastBlinkCat = fastBlink;
        }
    } else {
        if (s_lastCatAlarm || s_lastCatMuted != muted) {
            lv_obj_set_style_bg_color(btnCat, lv_color_hex(0xE67E22), 0);
            lv_label_set_text(lblCat, "CatMat");
            s_lastCatAlarm = false; s_lastCatMuted = muted;
        }
    }

    if (timeSynced) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 10)) {
            if (s_clock_min != timeinfo.tm_min) {
                lv_label_set_text_fmt(dash_label_clock, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                s_clock_min = timeinfo.tm_min;
            }
        }
    }

    int curIconBleState = (matEnabled || kippyEnabled) ? (matEnabled && !connected ? ((millis() % 600 < 300) ? 1 : 2) : 3) : 0;
    if (s_iconBleState != curIconBleState) {
        lv_color_t ble_col = (curIconBleState == 1) ? lv_color_hex(0xFF8800) : ((curIconBleState == 3) ? lv_color_hex(0x00FF00) : lv_color_hex(0x555555));
        lv_obj_set_style_text_color(dash_icon_ble, ble_col, 0); s_iconBleState = curIconBleState;
    }
            
    int curWifiStatus = !wifiEnabled ? 0 : (WiFi.status() == WL_CONNECTED ? 1 : 2);
    if (s_wifiStatus != curWifiStatus) {
        lv_color_t wifi_col = (curWifiStatus == 0) ? lv_color_hex(0x555555) : (curWifiStatus == 1 ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000));
        lv_obj_set_style_text_color(dash_icon_ha, wifi_col, 0); s_wifiStatus = curWifiStatus;
    }
            
    if (s_batteryPercent != batteryPercent) {
        const char * bat_icon = LV_SYMBOL_BATTERY_EMPTY;
        if (batteryPercent >= 80) bat_icon = LV_SYMBOL_BATTERY_FULL; else if (batteryPercent >= 60) bat_icon = LV_SYMBOL_BATTERY_3;
        else if (batteryPercent >= 40) bat_icon = LV_SYMBOL_BATTERY_2; else if (batteryPercent >= 15) bat_icon = LV_SYMBOL_BATTERY_1;
        lv_label_set_text_fmt(dash_label_bat, "%s %d%%", bat_icon, batteryPercent);
        lv_obj_set_style_text_color(dash_label_bat, batteryPercent < 15 ? lv_color_hex(0xFF0000) : lv_color_white(), 0); 
        s_batteryPercent = batteryPercent;
    }
}