// Windows 11 native Start button suppression bridge.
//
// XAML Diagnostics retains its TAP site object for the diagnostics session.
// The TAP therefore lives in StartMenuHelper, which may remain resident in
// Explorer, while StartMenuDLL can still unload/reload on Exit.

#include "stdafx.h"
#include "Win11StartButton.h"
#include "Settings.h"
#include "LogManager.h"
#include "ResourceHelper.h"

typedef void (__cdecl *StartWin11StartButtonTap_t)( BOOL enabled, BOOL allTaskbars );
typedef void (__cdecl *UpdateWin11StartButtonTap_t)( BOOL enabled, BOOL allTaskbars );
typedef void (__cdecl *StopWin11StartButtonTap_t)( void );

static HMODULE g_StartButtonTapModule = NULL;
static StartWin11StartButtonTap_t g_StartButtonTapStart = NULL;
static UpdateWin11StartButtonTap_t g_StartButtonTapUpdate = NULL;
static StopWin11StartButtonTap_t g_StartButtonTapStop = NULL;

static bool LoadStartButtonTap( void )
{
	if (g_StartButtonTapModule)
		return true;

#ifdef _WIN64
	const wchar_t helperName[] = L"StartMenuHelper64.dll";
#else
	const wchar_t helperName[] = L"StartMenuHelper32.dll";
#endif

	// Prefer a helper next to StartMenuDLL for local development builds, but
	// installed Open-Shell keeps StartMenuHelper in System32.
	HMODULE module = NULL;
	wchar_t path[MAX_PATH];
	DWORD pathLength = GetModuleFileName(g_Instance, path, _countof(path));
	if (pathLength && pathLength < _countof(path))
	{
		wchar_t *name = wcsrchr(path, L'\\');
		if (name)
		{
			name++;
			const size_t remaining = path + _countof(path) - name;
			if (_countof(helperName) <= remaining)
			{
				wcscpy_s(name, remaining, helperName);
				module = LoadLibrary(path);
			}
		}
	}

	if (!module)
		module = LoadLibraryEx(helperName, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

	if (!module)
	{
		LogToFile(STARTUP_LOG, L"Win11StartButton: unable to load TAP helper 0x%08X", GetLastError());
		return false;
	}

	StartWin11StartButtonTap_t start =
		(StartWin11StartButtonTap_t)GetProcAddress(module, "StartWin11StartButtonTap");
	UpdateWin11StartButtonTap_t update =
		(UpdateWin11StartButtonTap_t)GetProcAddress(module, "UpdateWin11StartButtonTap");
	StopWin11StartButtonTap_t stop =
		(StopWin11StartButtonTap_t)GetProcAddress(module, "StopWin11StartButtonTap");
	if (!start || !update || !stop)
	{
		LogToFile(STARTUP_LOG, L"Win11StartButton: TAP helper exports are unavailable");
		FreeLibrary(module);
		return false;
	}

	g_StartButtonTapModule = module;
	g_StartButtonTapStart = start;
	g_StartButtonTapUpdate = update;
	g_StartButtonTapStop = stop;
	return true;
}

void StartWin11StartButtonMonitor( void )
{
	if (!IsWin11() || !GetSettingBool(L"EnableStartButton") || !LoadStartButtonTap())
		return;

	g_StartButtonTapStart(TRUE, GetSettingBool(L"AllTaskbars"));
}

void UpdateWin11StartButtonMonitor( void )
{
	if (!IsWin11())
		return;

	BOOL enabled = GetSettingBool(L"EnableStartButton");
	BOOL allTaskbars = GetSettingBool(L"AllTaskbars");
	if (!g_StartButtonTapModule)
	{
		// Start on first enable if the helper was not needed at startup.
		if (enabled && LoadStartButtonTap())
			g_StartButtonTapStart(enabled, allTaskbars);
		return;
	}

	g_StartButtonTapUpdate(enabled, allTaskbars);
}

void StopWin11StartButtonMonitor( void )
{
	if (!IsWin11() || !g_StartButtonTapModule)
		return;

	g_StartButtonTapStop();

	// Drop only StartMenuDLL's explicit reference. XAML Diagnostics owns the TAP
	// module reference for the diagnostics session after a successful connection.
	FreeLibrary(g_StartButtonTapModule);
	g_StartButtonTapModule = NULL;
	g_StartButtonTapStart = NULL;
	g_StartButtonTapUpdate = NULL;
	g_StartButtonTapStop = NULL;
}
