#pragma once
#include <Arduino.h>

extern volatile int pendingScreenshotMode; 
extern volatile bool screenshotModeActive;

void WebSetupLogic_Init();
void WebSetupLogic_Update();