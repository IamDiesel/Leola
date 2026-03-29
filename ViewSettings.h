#pragma once
#include <lvgl.h>

class ViewSettings {
public:
    static lv_obj_t* build();
    static void update();
};