#include "ViewPopups.h"
#include "SharedData.h"
#include "GuiManager.h"

static lv_obj_t * dual_alarm_container = nullptr;
static lv_obj_t * pnl_baby = nullptr;
static lv_obj_t * lbl_baby_alarm = nullptr;
static lv_obj_t * pnl_cat = nullptr;
static lv_obj_t * lbl_cat_alarm = nullptr;

void ViewPopups::init() {
    if (dual_alarm_container != nullptr) return; 

    lv_obj_t * sys_layer = lv_layer_top();

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
    // --- Alarm Cross-Logic ---
    bool catNeedsPopup = (alarmActive && !muted);
    bool babyNeedsPopup = (babyAlarmActive && !babyMuted);
    
    ScreenID scr = gui.getCurrentScreen();
    
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
        showCat = catNeedsPopup; 
    } else if (scr == SCREEN_CATMAT) {
        showBaby = babyNeedsPopup; 
    }

    if (!showCat && !showBaby) {
        lv_obj_add_flag(dual_alarm_container, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(dual_alarm_container, LV_OBJ_FLAG_HIDDEN);
    bool fastBlink = (millis() % 600 < 300);

    if (showCat && showBaby) {
        lv_obj_clear_flag(pnl_baby, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pnl_cat, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(pnl_baby, 360, 180);
        lv_obj_align(pnl_baby, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_size(pnl_cat, 360, 180);
        lv_obj_align(pnl_cat, LV_ALIGN_BOTTOM_MID, 0, 0);
    } else if (showBaby) {
        lv_obj_clear_flag(pnl_baby, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pnl_cat, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(pnl_baby, 360, 360);
        lv_obj_align(pnl_baby, LV_ALIGN_CENTER, 0, 0);
    } else if (showCat) {
        lv_obj_add_flag(pnl_baby, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pnl_cat, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(pnl_cat, 360, 360);
        lv_obj_align(pnl_cat, LV_ALIGN_CENTER, 0, 0);
    }

    if (showBaby) lv_obj_set_style_bg_color(pnl_baby, fastBlink ? lv_color_hex(0xFF0000) : lv_color_hex(0x660000), 0);
    if (showCat) lv_obj_set_style_bg_color(pnl_cat, fastBlink ? lv_color_hex(0xFF4400) : lv_color_hex(0x882200), 0);
}