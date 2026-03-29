#pragma once
#include <lvgl.h>

class ViewDashboard {
public:
    static lv_obj_t* build();
    static void update(); // NEU: Damit die Uhrzeit tickt!
};