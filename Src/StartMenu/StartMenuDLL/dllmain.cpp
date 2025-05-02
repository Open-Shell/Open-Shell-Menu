// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

// dllmain.cpp : Defines the entry point for the DLL application.

#include "stdafx.h"
#include "StartMenuDLL.h"
#include "Settings.h"
#include "Translations.h"
#include "ResourceHelper.h"
#include "StringSet.h"
#include "resource.h"
#include "..\Lib\resource.h"
#include "SettingsUI.h"
#include "SkinManager.h"
#include "uxtheme.h"
#include "FNVHash.h"
#include "MenuContainer.h"
#include "SearchManager.h"
#include "LogManager.h"
#include <dwmapi.h>

#pragma comment(linker, \
	"\"/manifestdependency:type='Win32' "\
	"name='Microsoft.Windows.Common-Controls' "\
	"version='6.0.0.0' "\
	"processorArchitecture='*' "\
	"publicKeyToken='6595b64144ccf1df' "\
	"language='*'\"")

static int g_LoadDialogs[]=
{
	IDD_SETTINGS,0x04000000,
	IDD_SETTINGSTREE,0x04000000,
	IDD_BROWSEFORICON,0x04000000,
	IDD_LANGUAGE,0x04000000,
	IDD_SKINSETTINGS,0x04000000,
	IDD_CUSTOMTREE,0x04000000,
	IDD_CUSTOMMENU,0x04000000,
	IDD_CUSTOMMENU7,0x04000000,
	IDD_STYLESETTINGS,0x04000000,
	IDD_CUSTOMLIST,0x04000000,
	IDD_PROGRESS,0x04000004,
	0
};

static HANDLE g_DllInitThread;

static DWORD CALLBACK DllInitThread( void* )
{
	CoInitialize(NULL);
	InitSettings();

	wchar_t path[_MAX_PATH];
	GetModuleFileName(g_Instance,path,_countof(path));
	*PathFindFileName(path)=0;

	wchar_t fname[_MAX_PATH];
	Sprintf(fname,_countof(fname),L"%sStartMenuL10N.ini",path);
	CString language=GetSettingString(L"Language");
	ParseTranslations(fname,language);

	HINSTANCE resInstance=LoadTranslationDll(language);

	LoadTranslationResources(resInstance,g_LoadDialogs);

	if (resInstance)
		FreeLibrary(resInstance);

	CoUninitialize();
	return 0;
}

STARTMENUAPI void WaitDllInitThread( void )
{
	ATLASSERT(g_DllInitThread);
	WaitForSingleObject(g_DllInitThread,INFINITE);
}

extern "C" BOOL WINAPI DllMain( HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved )
{
	if (dwReason==DLL_PROCESS_ATTACH)
	{
		LogToFile(STARTUP_LOG,L"StartMenu DLL: DLL_PROCESS_ATTACH");
		g_Instance=hInstance;
		g_DllInitThread=CreateThread(NULL,0,DllInitThread,NULL,0,NULL);
	}
	if (dwReason==DLL_PROCESS_DETACH)
	{
		LogToFile(STARTUP_LOG,L"StartMenu DLL: DLL_PROCESS_DETACH");
	}

	return TRUE;
}
