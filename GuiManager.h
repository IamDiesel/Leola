#pragma once
#include <lvgl.h>
#include <Preferences.h>

enum ScreenID {
    SCREEN_DASHBOARD = 0,
    SCREEN_CATMAT,
    SCREEN_BABY,
    SCREEN_SETTINGS
};

class GuiManager {
public:
    void init();
    void switchScreen(ScreenID newScreen, lv_scr_load_anim_t anim_type);
    ScreenID getCurrentScreen() const;
    
    // NEU: Diese Funktion ist jetzt public, damit Buttons das Menue schliessen duerfen!
    void toggleQuickOverlay();
    
    static void gestureEventWrapper(lv_event_t * e);
    static void volumeSliderWrapper(lv_event_t * e);
    static void brightnessSliderWrapper(lv_event_t * e);

    Preferences preferences;
private:
    ScreenID currentScreen;
    lv_obj_t* quickOverlay = nullptr;

    void handleGesture(lv_event_t * e);
    
    lv_obj_t* createDashboard();
    lv_obj_t* createCatMatPlaceholder();
    lv_obj_t* createBabyPlaceholder();
    lv_obj_t* createSettingsPlaceholder();
};

extern GuiManager gui;