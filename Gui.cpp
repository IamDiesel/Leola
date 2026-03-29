#include "Gui.h"
#include "GuiManager.h"
#include "ViewDashboard.h"
#include "ViewCatMat.h"
#include "ViewBaby.h"
#include "ViewSettings.h"
#include "ViewPopups.h"
#include "LVGL_Driver.h"

void Gui_Init() {
    gui.init();
    ViewPopups::init();
}

void Gui_Update() {
    // 0 bedeutet: Warte sicher, bis der Treiber-Task den LVGL-Speicher freigibt
    if (lvgl_port_lock(0)) {
        ViewDashboard::update();
        ViewCatMat::update();
        ViewBaby::update();     
        ViewSettings::update();
        ViewPopups::update();
        
        // KRITISCHER FIX: lv_timer_handler() wurde hier GELOESCHT!
        // Der Aufruf erfolgt bereits sicher im Hintergrund ueber die LVGL_Driver.cpp
        
        lvgl_port_unlock();
    }
}