#pragma once
#include <lvgl.h>

class ViewBaby {
public:
    static lv_obj_t* build();
    static void update();
};

// Helfer-Funktionen fuer den Kamera-Stream
void ViewBaby_SetImage(const void* src);
void ViewBaby_SetStatus(const char* text);
void ViewBaby_StopStreamOnError(); // NEU: Auto-Stop