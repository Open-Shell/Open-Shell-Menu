#pragma once

struct ExplorerTapSettings
{
	BOOL startButtonEnabled;
	BOOL startButtonAllTaskbars;
};

extern "C" void UpdateExplorerTap(const ExplorerTapSettings* settings);
extern "C" void StopExplorerTap();
