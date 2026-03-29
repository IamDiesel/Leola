#include "GuiManager.h"
#include "ViewDashboard.h"
#include "ViewCatMat.h"
#include "ViewBaby.h"
#include "ViewSettings.h"
#include "LVGL_Driver.h" 
#include "SharedData.h" 
#include <WiFi.h>       

GuiManager gui;

static const lv_point_precise_t walk_pts[] = {{0,0}, {4,12}, {14,12}, {22,8}, {24,3}, {28,3}, {32,9}, {28,14}, {28,24}, {24,24}, {24,18}, {14,18}, {14,24}, {10,24}, {10,18}, {4,12}};
static const lv_point_precise_t sit_pts[] = {{22, 24}, {10, 24}, {10, 16}, {12, 8}, {10, 2}, {13, 5}, {19, 5}, {22, 2}, {20, 8}, {22, 16}, {22, 24}};

static void anim_cat_walk_cb(void * var, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t *)var, v, 0); }
static void anim_bar_cb(void * var, int32_t v) { lv_bar_set_value((lv_obj_t*)var, v, LV_ANIM_OFF); }
static void anim_zzz_y_cb(void * var, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0); }
static void anim_zzz_opa_cb(void * var, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)var, v, 0); }

static void anim_walk_ready_cb(lv_anim_t * a) {
    lv_obj_t * cat = (lv_obj_t *)a->var;
    lv_line_set_points(cat, sit_pts, 11);
}

static void splash_timer_cb(lv_timer_t * timer) {
    // BUGFIX BOOTLOOP: Toetet alle Splash-Screen Animationen hart ab, 
    // bevor die UI Objekte aus dem Speicher geloescht werden!
    lv_anim_del_all(); 
    gui.switchScreen(SCREEN_DASHBOARD, LV_SCR_LOAD_ANIM_NONE);
}

static void btn_shutdown_event_cb(lv_event_t * e) { 
    playToneI2S(600, 100, true);
    intentionalDisconnect = true; 
    if (connected) { pClient->disconnect(); } 
    WiFi.disconnect(true, true); 
    WiFi.mode(WIFI_OFF); 
    
    lv_obj_t * sd_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(sd_scr, lv_color_hex(0x000000), 0);
    lv_obj_t * lbl = lv_label_create(sd_scr); 
    lv_label_set_text(lbl, "Geraet wird heruntergefahren...\n\n(Reset-Taste druecken \nzum Aufwecken)"); 
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0); 
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl); 
    
    lv_scr_load_anim(sd_scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    
    lv_timer_handler(); 
    delay(1500); 
    esp_deep_sleep_start(); 
}

static lv_obj_t * screen_off_overlay = nullptr;
extern void Set_Backlight(uint8_t val);

static void screen_off_click_cb(lv_event_t * e) {
    playToneI2S(800, 100, true);
    Set_Backlight(brightnessPercent); 
    lv_obj_del(screen_off_overlay);
    screen_off_overlay = nullptr;
}

static void btn_screen_off_event_cb(lv_event_t * e) {
    if (screen_off_overlay != nullptr) return;
    
    playToneI2S(800, 100, true);
    screen_off_overlay = lv_obj_create(lv_layer_sys());
    lv_obj_set_size(screen_off_overlay, 360, 360);
    lv_obj_set_style_bg_color(screen_off_overlay, lv_color_black(), 0); 
    lv_obj_set_style_bg_opa(screen_off_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen_off_overlay, 0, 0);
    lv_obj_add_flag(screen_off_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen_off_overlay, screen_off_click_cb, LV_EVENT_PRESSED, NULL);

    Set_Backlight(0);

    GuiManager * manager = (GuiManager *)lv_event_get_user_data(e);
    manager->toggleQuickOverlay(); 
}

static void alarm_wake_timer_cb(lv_timer_t * timer) {
    if (screen_off_overlay != nullptr) {
        if (alarmActive || babyAlarmActive || disconnectAlarmActive) {
            Set_Backlight(brightnessPercent);
            lv_obj_del(screen_off_overlay);
            screen_off_overlay = nullptr;
        }
    }
}

void GuiManager::init() {
    currentScreen = (ScreenID)99; 

    lv_timer_create(alarm_wake_timer_cb, 500, NULL);

    lv_obj_t * splash_screen = lv_obj_create(NULL); 
    if (!splash_screen) return; 

    lv_obj_set_style_bg_color(splash_screen, lv_color_hex(0x111111), 0); 
    lv_obj_set_style_border_width(splash_screen, 0, 0);
    lv_obj_set_style_bg_opa(splash_screen, LV_OPA_COVER, 0); 

    lv_obj_t * splash_title = lv_label_create(splash_screen); 
    lv_label_set_text(splash_title, "Lola OS 3.0"); 
    lv_obj_set_style_text_font(splash_title, &lv_font_montserrat_14, 0); 
    lv_obj_set_style_text_color(splash_title, lv_color_hex(0x00A0FF), 0); 
    lv_obj_align(splash_title, LV_ALIGN_TOP_MID, 0, 20); 

    lv_obj_t * splash_bar = lv_bar_create(splash_screen); 
    lv_obj_set_size(splash_bar, 200, 10); 
    lv_obj_align(splash_bar, LV_ALIGN_CENTER, 0, 10); 
    lv_bar_set_range(splash_bar, 0, 100); 
    lv_obj_set_style_bg_color(splash_bar, lv_color_hex(0x00A0FF), LV_PART_INDICATOR);

    lv_obj_t * splash_mat = lv_obj_create(splash_screen); 
    lv_obj_set_size(splash_mat, 60, 4); 
    lv_obj_set_style_bg_color(splash_mat, lv_color_hex(0x555555), 0); 
    lv_obj_set_style_border_width(splash_mat, 0, 0); 
    lv_obj_set_style_radius(splash_mat, 2, 0); 
    lv_obj_align(splash_mat, LV_ALIGN_CENTER, 60, 0); 

    lv_color_t baby_col = lv_color_hex(0x4FA5D6); 
    lv_obj_t * baby_cont = lv_obj_create(splash_screen);
    lv_obj_set_size(baby_cont, 60, 70);
    lv_obj_align(baby_cont, LV_ALIGN_BOTTOM_MID, 0, -30); 
    lv_obj_set_style_bg_opa(baby_cont, 0, 0); 
    lv_obj_set_style_border_width(baby_cont, 0, 0);
    lv_obj_set_style_pad_all(baby_cont, 0, 0);

    static const lv_point_precise_t body_pts[] = {{15, 30}, {45, 30}, {50, 45}, {45, 60}, {15, 60}, {10, 45}, {15, 30}};
    lv_obj_t * l_body = lv_line_create(baby_cont); lv_line_set_points(l_body, body_pts, 7); lv_obj_set_style_line_width(l_body, 3, 0); lv_obj_set_style_line_color(l_body, baby_col, 0); lv_obj_set_style_line_rounded(l_body, true, 0);
    
    static const lv_point_precise_t head_pts[] = {{20, 15}, {40, 15}, {45, 22}, {40, 30}, {20, 30}, {15, 22}, {20, 15}};
    lv_obj_t * l_head = lv_line_create(baby_cont); lv_line_set_points(l_head, head_pts, 7); lv_obj_set_style_line_width(l_head, 2, 0); lv_obj_set_style_line_color(l_head, lv_color_white(), 0);
    
    static const lv_point_precise_t eye_l_pts[] = {{23, 22}, {25, 24}, {27, 22}};
    static const lv_point_precise_t eye_r_pts[] = {{33, 22}, {35, 24}, {37, 22}};
    lv_obj_t * l_e_l = lv_line_create(baby_cont); lv_line_set_points(l_e_l, eye_l_pts, 3); lv_obj_set_style_line_width(l_e_l, 1, 0); lv_obj_set_style_line_color(l_e_l, lv_color_white(), 0);
    lv_obj_t * l_e_r = lv_line_create(baby_cont); lv_line_set_points(l_e_r, eye_r_pts, 3); lv_obj_set_style_line_width(l_e_r, 1, 0); lv_obj_set_style_line_color(l_e_r, lv_color_white(), 0);
    
    static const lv_point_precise_t paci_pts[] = {{28, 26}, {32, 26}, {32, 28}, {28, 28}, {28, 26}}; 
    static const lv_point_precise_t paci_handle[] = {{29, 28}, {31, 28}, {31, 30}, {29, 30}, {29, 28}}; 
    lv_obj_t * l_paci = lv_line_create(baby_cont); lv_line_set_points(l_paci, paci_pts, 5); lv_obj_set_style_line_width(l_paci, 2, 0); lv_obj_set_style_line_color(l_paci, lv_color_hex(0xE67E22), 0); 
    lv_obj_t * l_paci_h = lv_line_create(baby_cont); lv_line_set_points(l_paci_h, paci_handle, 5); lv_obj_set_style_line_width(l_paci_h, 2, 0); lv_obj_set_style_line_color(l_paci_h, lv_color_white(), 0);
    
    lv_obj_t * lbl_zzz = lv_label_create(splash_screen); lv_label_set_text(lbl_zzz, "zzzZZZ"); lv_obj_set_style_text_color(lbl_zzz, lv_color_hex(0xAAAAAA), 0); lv_obj_align_to(lbl_zzz, baby_cont, LV_ALIGN_TOP_MID, 10, 5); 
    lv_anim_t a_zzz_y; lv_anim_init(&a_zzz_y); lv_anim_set_var(&a_zzz_y, lbl_zzz); lv_anim_set_values(&a_zzz_y, 0, -30); lv_anim_set_time(&a_zzz_y, 1500); lv_anim_set_exec_cb(&a_zzz_y, anim_zzz_y_cb); lv_anim_set_path_cb(&a_zzz_y, lv_anim_path_ease_out); lv_anim_set_delay(&a_zzz_y, 500); lv_anim_set_repeat_count(&a_zzz_y, 2); lv_anim_start(&a_zzz_y);
    lv_anim_t a_zzz_opa; lv_anim_init(&a_zzz_opa); lv_anim_set_var(&a_zzz_opa, lbl_zzz); lv_anim_set_values(&a_zzz_opa, 100, 0); lv_anim_set_time(&a_zzz_opa, 1500); lv_anim_set_exec_cb(&a_zzz_opa, anim_zzz_opa_cb); lv_anim_set_path_cb(&a_zzz_opa, lv_anim_path_linear); lv_anim_set_delay(&a_zzz_opa, 500); lv_anim_set_repeat_count(&a_zzz_opa, 2); lv_anim_start(&a_zzz_opa);

    lv_obj_t * cat_walker = lv_line_create(splash_screen); 
    lv_line_set_points(cat_walker, walk_pts, 16); 
    lv_obj_set_style_line_width(cat_walker, 3, 0); 
    lv_obj_set_style_line_color(cat_walker, lv_color_hex(0xFFFFFF), 0); 
    lv_obj_set_style_line_rounded(cat_walker, true, 0); 
    lv_obj_align(cat_walker, LV_ALIGN_CENTER, -80, -22); 

    lv_anim_t a_walk; lv_anim_init(&a_walk); 
    lv_anim_set_var(&a_walk, cat_walker); 
    lv_anim_set_values(&a_walk, 0, 135); 
    lv_anim_set_time(&a_walk, 2000); 
    lv_anim_set_exec_cb(&a_walk, anim_cat_walk_cb); 
    lv_anim_set_path_cb(&a_walk, lv_anim_path_linear); 
    lv_anim_set_ready_cb(&a_walk, anim_walk_ready_cb);
    lv_anim_start(&a_walk);

    lv_anim_t a_bar; lv_anim_init(&a_bar); lv_anim_set_var(&a_bar, splash_bar); lv_anim_set_values(&a_bar, 0, 100); lv_anim_set_time(&a_bar, 3500); lv_anim_set_exec_cb(&a_bar, anim_bar_cb); lv_anim_start(&a_bar);

    lv_scr_load(splash_screen);

    lv_timer_t * t = lv_timer_create(splash_timer_cb, 3600, NULL);
    lv_timer_set_repeat_count(t, 1);
}

ScreenID GuiManager::getCurrentScreen() const { return currentScreen; }

void GuiManager::switchScreen(ScreenID newScreen, lv_scr_load_anim_t anim_type) {
    if (currentScreen == newScreen) return;

    if (currentScreen == SCREEN_BABY) {
        requestBabyStream = false;
    }

    lv_obj_t* nextScr = nullptr;
    switch(newScreen) {
        case SCREEN_DASHBOARD: nextScr = ViewDashboard::build(); break;
        case SCREEN_CATMAT:    nextScr = ViewCatMat::build(); break;
        case SCREEN_BABY:      nextScr = ViewBaby::build(); break;
        case SCREEN_SETTINGS:  nextScr = ViewSettings::build(); break;
    }
    
    if (nextScr != nullptr) {
        lv_scr_load_anim(nextScr, anim_type, 0, 50, true);
        currentScreen = newScreen;
    }
}

void GuiManager::toggleQuickOverlay() {
    if (quickOverlay != nullptr) { lv_obj_del(quickOverlay); quickOverlay = nullptr; return; }
    
    quickOverlay = lv_obj_create(lv_layer_sys()); 
    lv_obj_set_size(quickOverlay, 360, 360); 
    lv_obj_set_style_bg_opa(quickOverlay, 0, 0); 
    lv_obj_set_style_border_width(quickOverlay, 0, 0);
    lv_obj_clear_flag(quickOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(quickOverlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(quickOverlay, [](lv_event_t* e){
        GuiManager * manager = (GuiManager *)lv_event_get_user_data(e);
        manager->toggleQuickOverlay();
    }, LV_EVENT_CLICKED, this);

    lv_obj_add_event_cb(quickOverlay, gestureEventWrapper, LV_EVENT_GESTURE, this);

    lv_obj_t* panel = lv_obj_create(quickOverlay);
    lv_obj_set_size(panel, 360, 240);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(panel, 240, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_GESTURE_BUBBLE); 

    lv_obj_add_event_cb(panel, [](lv_event_t* e){}, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t* label_vol = lv_label_create(panel); 
    lv_label_set_text(label_vol, "Alarm- / UI-Lautstaerke"); 
    lv_obj_align(label_vol, LV_ALIGN_TOP_MID, 0, 10); 
    lv_obj_set_style_text_color(label_vol, lv_color_white(), 0);
    
    lv_obj_t* slider_vol = lv_slider_create(panel); 
    lv_obj_set_size(slider_vol, 200, 15); 
    lv_obj_align(slider_vol, LV_ALIGN_TOP_MID, 0, 35); 
    lv_slider_set_range(slider_vol, 0, 100); 
    lv_slider_set_value(slider_vol, volumePercent, LV_ANIM_OFF); 
    lv_obj_add_event_cb(slider_vol, volumeSliderWrapper, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_flag(slider_vol, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t* label_stream_vol = lv_label_create(panel); 
    lv_label_set_text(label_stream_vol, "Babyphone Stream"); 
    lv_obj_align(label_stream_vol, LV_ALIGN_TOP_MID, 0, 65); 
    lv_obj_set_style_text_color(label_stream_vol, lv_color_hex(0x4FA5D6), 0); 
    
    lv_obj_t* slider_stream_vol = lv_slider_create(panel); 
    lv_obj_set_size(slider_stream_vol, 200, 15); 
    lv_obj_align(slider_stream_vol, LV_ALIGN_TOP_MID, 0, 90); 
    lv_slider_set_range(slider_stream_vol, 0, 100); 
    lv_slider_set_value(slider_stream_vol, streamVolumePercent, LV_ANIM_OFF); 
    lv_obj_add_event_cb(slider_stream_vol, [](lv_event_t* e){
        playToneI2S(1000, 50, true);
        lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e);
        GuiManager * manager = (GuiManager *)lv_event_get_user_data(e);
        streamVolumePercent = lv_slider_get_value(slider);
        manager->preferences.begin("catmat", false);
        manager->preferences.putInt("streamVol", streamVolumePercent);
        manager->preferences.end();
    }, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_flag(slider_stream_vol, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t* label_br = lv_label_create(panel); 
    lv_label_set_text(label_br, "Helligkeit"); 
    lv_obj_align(label_br, LV_ALIGN_TOP_MID, 0, 120); 
    lv_obj_set_style_text_color(label_br, lv_color_white(), 0);
    
    lv_obj_t* slider_br = lv_slider_create(panel); 
    lv_obj_set_size(slider_br, 200, 15); 
    lv_obj_align(slider_br, LV_ALIGN_TOP_MID, 0, 145); 
    lv_slider_set_range(slider_br, 5, 100); 
    lv_slider_set_value(slider_br, brightnessPercent, LV_ANIM_OFF); 
    lv_obj_add_event_cb(slider_br, brightnessSliderWrapper, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_flag(slider_br, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t * btn_off = lv_btn_create(panel); 
    lv_obj_set_size(btn_off, 80, 40); 
    lv_obj_align(btn_off, LV_ALIGN_TOP_MID, -50, 185); 
    lv_obj_set_style_bg_color(btn_off, lv_color_hex(0x555555), 0); 
    lv_obj_add_event_cb(btn_off, btn_screen_off_event_cb, LV_EVENT_CLICKED, this); 
    lv_obj_add_flag(btn_off, LV_OBJ_FLAG_GESTURE_BUBBLE);
    
    lv_obj_t * lbl_off = lv_label_create(btn_off); 
    lv_label_set_text(lbl_off, LV_SYMBOL_EYE_CLOSE); 
    lv_obj_center(lbl_off);

    lv_obj_t * btn_sd = lv_btn_create(panel); 
    lv_obj_set_size(btn_sd, 80, 40); 
    lv_obj_align(btn_sd, LV_ALIGN_TOP_MID, 50, 185); 
    lv_obj_set_style_bg_color(btn_sd, lv_color_hex(0xAA0000), 0); 
    lv_obj_add_event_cb(btn_sd, btn_shutdown_event_cb, LV_EVENT_CLICKED, NULL); 
    lv_obj_add_flag(btn_sd, LV_OBJ_FLAG_GESTURE_BUBBLE);
    
    lv_obj_t * lbl_sd = lv_label_create(btn_sd); 
    lv_label_set_text(lbl_sd, LV_SYMBOL_POWER); 
    lv_obj_center(lbl_sd);
}

void GuiManager::volumeSliderWrapper(lv_event_t * e) {
    playToneI2S(1000, 50, true);
    lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e);
    GuiManager * manager = (GuiManager *)lv_event_get_user_data(e);
    volumePercent = lv_slider_get_value(slider);
    
    manager->preferences.begin("catmat", false);
    manager->preferences.putInt("volumePercent", volumePercent);
    manager->preferences.end();
}

extern void Set_Backlight(uint8_t val); 
void GuiManager::brightnessSliderWrapper(lv_event_t * e) {
    playToneI2S(1000, 50, true);
    lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e);
    GuiManager * manager = (GuiManager *)lv_event_get_user_data(e);
    brightnessPercent = lv_slider_get_value(slider);
    Set_Backlight(brightnessPercent); 
    manager->preferences.begin("catmat", false);
    manager->preferences.putInt("bright", brightnessPercent);
    manager->preferences.end();
}

void GuiManager::gestureEventWrapper(lv_event_t * e) { GuiManager * manager = (GuiManager *)lv_event_get_user_data(e); manager->handleGesture(e); }

void GuiManager::handleGesture(lv_event_t * e) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (quickOverlay != nullptr && dir == LV_DIR_TOP) { toggleQuickOverlay(); return; }
    if (quickOverlay == nullptr) {
        if (currentScreen == SCREEN_DASHBOARD) {
            if (dir == LV_DIR_LEFT)   switchScreen(SCREEN_CATMAT, LV_SCR_LOAD_ANIM_NONE);
            if (dir == LV_DIR_RIGHT)  switchScreen(SCREEN_BABY, LV_SCR_LOAD_ANIM_NONE);
            if (dir == LV_DIR_TOP)    switchScreen(SCREEN_SETTINGS, LV_SCR_LOAD_ANIM_NONE);
            if (dir == LV_DIR_BOTTOM) toggleQuickOverlay();
        } 
        else if (currentScreen == SCREEN_CATMAT) { if (dir == LV_DIR_RIGHT)  switchScreen(SCREEN_DASHBOARD, LV_SCR_LOAD_ANIM_NONE); if (dir == LV_DIR_BOTTOM) toggleQuickOverlay(); }
        else if (currentScreen == SCREEN_BABY) { if (dir == LV_DIR_LEFT)   switchScreen(SCREEN_DASHBOARD, LV_SCR_LOAD_ANIM_NONE); if (dir == LV_DIR_BOTTOM) toggleQuickOverlay(); }
        else if (currentScreen == SCREEN_SETTINGS) { if (dir == LV_DIR_RIGHT || dir == LV_DIR_LEFT) switchScreen(SCREEN_DASHBOARD, LV_SCR_LOAD_ANIM_NONE); }
    }
}