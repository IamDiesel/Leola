#pragma once

void SystemLogic_Init();
void SystemLogic_Update();

extern volatile bool isStreamActive;
extern int cameraRefreshMs;