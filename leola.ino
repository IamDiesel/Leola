#include "Display_ST77916.h"
#include "LVGL_Driver.h"
#include "I2C_Driver.h"
#include "TCA9554PWR.h"
#include <lvgl.h>
#include <driver/i2s.h>
#include <esp_wifi.h>
#include <esp_bt.h>

#include "SharedData.h"
#include "SystemLogic.h"
#include "Gui.h" 

void setup() {
    delay(500); // Brownout-Schutz für stabilen Kaltstart

    startTime = millis();
    pinMode(BATTERY_ADC_PIN, INPUT);
    Serial.begin(115200);
    
    I2C_Init(); 
    TCA9554PWR_Init(0x00); 
    Backlight_Init(); 
    LCD_Init(); 
    Lvgl_Init();
    
    Data_Init(); // Hierin passiert die I2S Audio Initialisierung

    // Batterie SOFORT auslesen
    int mv = analogReadMilliVolts(BATTERY_ADC_PIN); 
    batteryPercent = (int)(((mv * 3.0) / 1000.0 - 3.2) * 100.0);
    if (batteryPercent > 100) batteryPercent = 100;
    if (batteryPercent < 0) batteryPercent = 0;

    SystemLogic_Init();
    Gui_Init();
}

void loop() {
    SystemLogic_Update();
    Gui_Update();
    delay(5);
}