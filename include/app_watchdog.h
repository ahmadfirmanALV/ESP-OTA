#pragma once

bool appWatchdogInit();
bool appWatchdogSubscribeCurrentTask(const char* taskName);
void appWatchdogFeed();
