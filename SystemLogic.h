#pragma once

void SystemLogic_Init();
void SystemLogic_Update();
void executeDisconnectLogic(bool intentional);

// Globale Variablen & Funktionen fuer den Kamera-Stream
extern volatile bool isStreamActive;
extern int cameraRefreshMs;
extern volatile bool requestImageLoad;

void SystemLogic_TriggerImageLoad();