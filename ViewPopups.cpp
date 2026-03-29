#include "ViewPopups.h"
#include "SharedData.h"
#include "GuiManager.h"
#include <WiFi.h>

static lv_obj_t * web_setup_overlay = nullptr;
static lv_obj_t * web_setup_qr = nullptr;
static lv_obj_t * web_setup_lbl = nullptr;

static lv_obj_t * dual_alarm_container = nullptr;
static lv_obj_t * pnl_baby = nullptr;
static lv_obj_t * lbl_baby_alarm = nullptr;
static lv_obj_t * pnl_cat = nullptr;
static lv_obj_t * lbl_cat_alarm = nullptr;

static void btn_web_cancel_cb(lv_event_t * e) { ESP.restart(); }

void ViewPopups::init() {
    if (web_setup_overlay != nullptr) return; 

    lv_obj_t * sys_layer = lv_layer_top();

    // --- Web Setup Overlay ---
    web_setup_overlay = lv_obj_create(sys_layer);
    lv_obj_set_size(web_setup_overlay, 360, 360);
    lv_obj_set_style_bg_color(web_setup_overlay, lv_color_black(), 0);
    lv_obj_add_flag(web_setup_overlay, LV_OBJ_FLAG_HIDDEN);

    web_setup_lbl = lv_label_create(web_setup_overlay);
    lv_obj_set_style_text_color(web_setup_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_align(web_setup_lbl, LV_TEXT_ALIGN_CENTER, 0);

    web_setup_qr = lv_qrcode_create(web_setup_overlay);
    lv_qrcode_set_size(web_setup_qr, 160);
    lv_qrcode_set_dark_color(web_setup_qr, lv_color_black());
    lv_qrcode_set_light_color(web_setup_qr, lv_color_white());
    lv_obj_align(web_setup_qr, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t * btn_cancel = lv_btn_create(web_setup_overlay);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(btn_cancel, btn_web_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_c = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_c, "Abbrechen");
    lv_obj_center(lbl_c);

    // --- Dual Alarm Container ---
    dual_alarm_container = lv_obj_create(sys_layer);
    lv_obj_set_size(dual_alarm_container, 360, 360);
    lv_obj_set_style_bg_color(dual_alarm_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(dual_alarm_container, 220, 0); 
    lv_obj_clear_flag(dual_alarm_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dual_alarm_container, LV_OBJ_FLAG_HIDDEN);

    pnl_baby = lv_btn_create(dual_alarm_container);
    lv_obj_set_style_bg_color(pnl_baby, lv_color_hex(0xFF0000), 0);
    lv_obj_add_event_cb(pnl_baby, [](lv_event_t* e){ babyMuted = true; }, LV_EVENT_CLICKED, NULL);
    lbl_baby_alarm = lv_label_create(pnl_baby);
    lv_obj_set_style_text_font(lbl_baby_alarm, &lv_font_montserrat_20, 0);
    lv_label_set_text(lbl_baby_alarm, LV_SYMBOL_BELL " BABY ALARM!");
    lv_obj_center(lbl_baby_alarm);

    pnl_cat = lv_btn_create(dual_alarm_container);
    lv_obj_set_style_bg_color(pnl_cat, lv_color_hex(0xFF4400), 0);
    lv_obj_add_event_cb(pnl_cat, [](lv_event_t* e){ muted = true; }, LV_EVENT_CLICKED, NULL);
    lbl_cat_alarm = lv_label_create(pnl_cat);
    lv_obj_set_style_text_font(lbl_cat_alarm, &lv_font_montserrat_20, 0);
    lv_label_set_text(lbl_cat_alarm, LV_SYMBOL_BELL " KATZEN ALARM!");
    lv_obj_center(lbl_cat_alarm);
}

void ViewPopups::update() {
    static bool webOverlayRendered = false;
    if (webSetupMode > 0) {
        if (!webOverlayRendered) {
            lv_obj_clear_flag(web_setup_overlay, LV_OBJ_FLAG_HIDDEN);
            if (webSetupMode == 1) {
                lv_label_set_text_fmt(web_setup_lbl, "WLAN SETUP\nSSID: LolaCatMat-Setup\nPW: %s", apPassword.c_str());
                lv_obj_align(web_setup_lbl, LV_ALIGN_TOP_MID, 0, 40); 
                String qrData = "WIFI:S:LolaCatMat-Setup;T:WPA;P:" + apPassword + ";;";
                lv_qrcode_update(web_setup_qr, qrData.c_str(), qrData.length());
                lv_obj_clear_flag(web_setup_qr, LV_OBJ_FLAG_HIDDEN);
            } else {
                String ipStr = WiFi.localIP().toString();
                lv_label_set_text_fmt(web_setup_lbl, "MQTT SETUP AKTIV\n\nGehe zu:\nhttp://%s", ipStr.c_str());
                lv_obj_align(web_setup_lbl, LV_ALIGN_TOP_MID, 0, 40); 
                String qrData = "http://" + ipStr;
                lv_qrcode_update(web_setup_qr, qrData.c_str(), qrData.length());
                lv_obj_clear_flag(web_setup_qr, LV_OBJ_FLAG_HIDDEN);
            }
            webOverlayRendered = true; 
        }
        return; 
    } else {
        lv_obj_add_flag(web_setup_overlay, LV_OBJ_FLAG_HIDDEN);
        webOverlayRendered = false;
    }

    // --- Alarm Cross-Logic ---
    bool catNeedsPopup = (alarmActive && !muted);
    bool babyNeedsPopup = (babyAlarmActive && !babyMuted);
    
    ScreenID scr = gui.getCurrentScreen();
    
    // Auf dem Dashboard gibt es KEINE Overlays!
    if (scr == SCREEN_DASHBOARD) {
        lv_obj_add_flag(dual_alarm_container, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    bool showCat = false;
    bool showBaby = false;

    if (scr == SCREEN_SETTINGS) {
        showCat = catNeedsPopup;
        showBaby = babyNeedsPopup;
    } else if (scr == SCREEN_BABY) {
        showCat = catNeedsPopup; // Katze überlagert Baby
    } else if (scr == SCREEN_CATMAT) {
        showBaby = babyNeedsPopup; // Baby überlagert Katze
    }

    if (!showCat && !showBaby) {
        lv_obj_add_flag(dual_alarm_container, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(dual_alarm_container, LV_OBJ_FLAG_HIDDEN);
    bool fastBlink = (millis() % 600 < 300);

    if (showCat && showBaby) {
        // Horizontal geteiltes Overlay!
        lv_obj_clear_flag(pnl_baby, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pnl_cat, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(pnl_baby, 360, 180);
        lv_obj_align(pnl_baby, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_size(pnl_cat, 360, 180);
        lv_obj_align(pnl_cat, LV_ALIGN_BOTTOM_MID, 0, 0);
    } else if (showBaby) {
        // Nur Baby maximiert
        lv_obj_clear_flag(pnl_baby, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pnl_cat, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(pnl_baby, 360, 360);
        lv_obj_align(pnl_baby, LV_ALIGN_CENTER, 0, 0);
    } else if (showCat) {
        // Nur Katze maximiert
        lv_obj_add_flag(pnl_baby, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pnl_cat, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(pnl_cat, 360, 360);
        lv_obj_align(pnl_cat, LV_ALIGN_CENTER, 0, 0);
    }

    if (showBaby) lv_obj_set_style_bg_color(pnl_baby, fastBlink ? lv_color_hex(0xFF0000) : lv_color_hex(0x660000), 0);
    if (showCat) lv_obj_set_style_bg_color(pnl_cat, fastBlink ? lv_color_hex(0xFF4400) : lv_color_hex(0x882200), 0);
}