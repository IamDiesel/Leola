#pragma once
#include <Arduino.h>

// Initialisiert die BLE Hardware und Scanner
void BleLogic_Init();

// Führt die kompletten BLE-Abläufe aus. 
// Gibt 'true' zurück, wenn SystemLogic danach pausieren soll (z.B. im Setup-Scan)
bool BleLogic_Update();