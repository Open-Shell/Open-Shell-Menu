// dllmain.cpp : Implementation of DllMain.

#include "stdafx.h"
#include "resource.h"
#include "StartMenuHelper_h.h"
#include "dllmain.h"
#include "Settings.h"
#include "Translations.h"
#include "StringUtils.h"
#include "ResourceHelper.h"
#include "..\StartMenuDLL\LogManager.h"

CStartMenuHelperModule _AtlModule;

void UpgradeSettings( bool bShared )
{
}

void UpdateSettings( void )
{
	CRegKey regKey;
	wchar_t language[100]=L"";
	if (regKey.Open(HKEY_LOCAL_MACHINE,L"Software\\OpenShell\\OpenShell",KEY_READ|KEY_WOW64_64KEY)==ERROR_SUCCESS)
	{
		ULONG size=_countof(language);
		if (regKey.QueryStringValue(L"DefaultLanguage",language,&size)!=ERROR_SUCCESS)
			language[0]=0;
	}
	UpdateSetting(L"Language",language,false);
}

void ClosingSettings( HWND hWnd, int flags, int command )
{
}

void SettingChangedCallback( const CSetting *pSetting )
{
}

CSetting g_Settings[]={
{L"MenuStyleGroup",CSetting::TYPE_GROUP},
	{L"MenuStyle",CSetting::TYPE_INT,0,0,2},
		{L"Classic1",CSetting::TYPE_RADIO},
		{L"Classic2",CSetting::TYPE_RADIO},
		{L"Win7",CSetting::TYPE_RADIO},

{L"GeneralBehavior",CSetting::TYPE_GROUP},
	{L"AutoStart",CSetting::TYPE_BOOL,0,0,1},
	{L"PinnedPrograms",CSetting::TYPE_INT,0,0,1},
		{L"FastItems",CSetting::TYPE_RADIO},
		{L"PinnedItems",CSetting::TYPE_RADIO},
	{L"DisablePinExt",CSetting::TYPE_BOOL,0,0,0},
	{L"FolderStartMenu",CSetting::TYPE_STRING,0,0,L""},
	{L"FolderCommonStartMenu",CSetting::TYPE_STRING,0,0,L""},
	{L"PinnedItemsPath",CSetting::TYPE_DIRECTORY,0,0,L"%APPDATA%\\OpenShell\\Pinned"},

{L"Language",CSetting::TYPE_GROUP},
	{L"Language",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_COLD|CSetting::FLAG_SHARED},

{NULL}
};

static HANDLE g_DllInitThread;

static DWORD CALLBACK DllInitThread( void* )
{
	CoInitialize(NULL);
	InitSettings(g_Settings,COMPONENT_MENU,NULL);

	wchar_t path[_MAX_PATH];
	path[0]=0;
#ifdef BUILD_SETUP
	{
		CRegKey regKey;
		if (regKey.Open(HKEY_LOCAL_MACHINE,L"Software\\OpenShell\\OpenShell",KEY_READ|KEY_WOW64_64KEY)==ERROR_SUCCESS)
		{
			ULONG size=_countof(path);
			if (regKey.QueryStringValue(L"Path",path,&size)!=ERROR_SUCCESS)
				path[0]=0;
		}
	}
#endif
	if (!path[0])
	{
		GetModuleFileName(g_Instance,path,_countof(path));
		PathRemoveFileSpec(path);
	}

	wchar_t fname[_MAX_PATH];
	Sprintf(fname,_countof(fname),L"%sStartMenuHelperL10N.ini",path);
	CString language=GetSettingString(L"Language");
	ParseTranslations(fname,language);

	CoUninitialize();
	return 0;
}

void WaitDllInitThread( void )
{
	ATLASSERT(g_DllInitThread);
	WaitForSingleObject(g_DllInitThread,INFINITE);
	LogToFile(STARTUP_LOG,L"StartMenuHelper: WaitDllInitThread");
}

// DLL Entry Point
extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason==DLL_PROCESS_ATTACH)
	{
		LogToFile(STARTUP_LOG,L"StartMenuHelper: DLL_PROCESS_ATTACH");
		g_Instance=hInstance;
		g_DllInitThread=CreateThread(NULL,0,DllInitThread,NULL,0,NULL);
	}
	if (dwReason==DLL_PROCESS_DETACH)
	{
		LogToFile(STARTUP_LOG,L"StartMenuHelper: DLL_PROCESS_DETACH");
	}
	return _AtlModule.DllMain(dwReason, lpReserved); 
}
