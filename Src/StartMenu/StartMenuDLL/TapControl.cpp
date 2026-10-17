#include "stdafx.h"
#include "../StartMenuHelper/ExplorerTap.h"
#include "TapControl.h"
#include "Settings.h"
#include "LogManager.h"
#include "ResourceHelper.h"

using UpdateExplorerTap_t = decltype(&UpdateExplorerTap);
using StopExplorerTap_t = decltype(&StopExplorerTap);

static HMODULE g_explorerTapModule = NULL;
static UpdateExplorerTap_t g_updateExplorerTap = NULL;
static StopExplorerTap_t g_stopExplorerTap = NULL;

#ifdef _WIN64
#define TAP_HELPER_NAME L"StartMenuHelper64.dll"
#else
#define TAP_HELPER_NAME L"StartMenuHelper32.dll"
#endif

static bool LoadExplorerTap()
{
	if (g_explorerTapModule)
		return true;

	// Prefer a helper next to StartMenuDLL for local development builds
	wchar_t path[MAX_PATH];
	GetModuleFileName(g_Instance, path, _countof(path));
	PathRemoveFileSpec(path);
	PathAppend(path, TAP_HELPER_NAME);

	auto module = LoadLibrary(path);

	// Fall-back to System32 (standard location for installed versions).
	if (!module)
		module = LoadLibraryEx(TAP_HELPER_NAME, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

	if (!module)
	{
		LogToFile(STARTUP_LOG, L"TapControl: unable to load TAP helper 0x%08X", GetLastError());
		return false;
	}

	auto update = (UpdateExplorerTap_t)GetProcAddress(module, "UpdateExplorerTap");
	auto stop = (StopExplorerTap_t)GetProcAddress(module, "StopExplorerTap");
	if (!update || !stop)
	{
		LogToFile(STARTUP_LOG, L"TapControl: TAP helper exports are unavailable");
		FreeLibrary(module);
		return false;
	}

	g_explorerTapModule = module;
	g_updateExplorerTap = update;
	g_stopExplorerTap = stop;
	return true;
}

void UpdateTap()
{
	if (!IsWin11())
		return;

	ExplorerTapSettings settings{};
	settings.startButtonEnabled = GetSettingBool(L"EnableStartButton");
	settings.startButtonAllTaskbars = GetSettingBool(L"AllTaskbars");

	// Start on first enable if the helper was not needed at startup.
	if (settings.startButtonEnabled)
		LoadExplorerTap();

	if (g_updateExplorerTap)
		g_updateExplorerTap(&settings);
}

void StopTap()
{
	if (g_stopExplorerTap)
		g_stopExplorerTap();

	// Drop only StartMenuDLL's explicit reference. XAML Diagnostics owns the TAP
	// module reference for the diagnostics session after a successful connection.
	if (g_explorerTapModule)
		FreeLibrary(g_explorerTapModule);

	g_explorerTapModule = NULL;
	g_updateExplorerTap = NULL;
	g_stopExplorerTap = NULL;
}
