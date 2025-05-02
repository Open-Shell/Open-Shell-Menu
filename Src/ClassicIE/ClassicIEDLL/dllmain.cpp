// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include "resource.h"
#include "..\..\Lib\resource.h"
#include "Settings.h"
#include "SettingsUI.h"
#include "SettingsUIHelper.h"
#include "DownloadHelper.h"
#include "Translations.h"
#include "ResourceHelper.h"
#include "dllmain.h"
#include "ClassicIEDLL.h"

#pragma comment(linker, \
	"\"/manifestdependency:type='Win32' "\
	"name='Microsoft.Windows.Common-Controls' "\
	"version='6.0.0.0' "\
	"processorArchitecture='*' "\
	"publicKeyToken='6595b64144ccf1df' "\
	"language='*'\"")

CClassicIEDLLModule _AtlModule;

static int g_LoadDialogs[]=
{
	IDD_SETTINGS,0x04000000,
	IDD_SETTINGSTREE,0x04000000,
	IDD_LANGUAGE,0x04000000,
	IDD_PROGRESS,0x04000004,
	0
};

static void NewVersionCallback( VersionData &data )
{
	wchar_t path[_MAX_PATH];
	GetModuleFileName(g_Instance,path,_countof(path));
	PathRemoveFileSpec(path);
	PathAppend(path,L"Update.exe");
	wchar_t cmdLine[1024];
	Sprintf(cmdLine,_countof(cmdLine),L"\"%s\" -popup",path);
	STARTUPINFO startupInfo={sizeof(startupInfo)};
	PROCESS_INFORMATION processInfo;
	memset(&processInfo,0,sizeof(processInfo));
	if (CreateProcess(path,cmdLine,NULL,NULL,TRUE,0,NULL,NULL,&startupInfo,&processInfo))
	{
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
	}
}

CSIEAPI void CheckForNewVersionIE( void )
{
	CheckForNewVersion(NULL,COMPONENT_IE,CHECK_AUTO_WAIT,NewVersionCallback);
}

static HANDLE g_DllInitThread;

static DWORD CALLBACK DllInitThread( void* )
{
	InitSettings();
	CString language=GetSettingString(L"Language");
	ParseTranslations(NULL,language);

	HINSTANCE resInstance=LoadTranslationDll(language);

	LoadTranslationResources(resInstance,g_LoadDialogs);

	if (resInstance)
		FreeLibrary(resInstance);
	InitClassicIE(g_Instance);
	return 0;
}

CSIEAPI void WaitDllInitThread( void )
{
	ATLASSERT(g_DllInitThread);
	WaitForSingleObject(g_DllInitThread,INFINITE);
}

CSIEAPI void DllLogToFile( const wchar_t *location, const wchar_t *message, ... )
{
	va_list args;
	va_start(args,message);
	VLogToFile(location,message,args);
	va_end(args);
}

#ifndef _WIN64
CSIEAPI bool DllSaveAdmx( const char *admxFile, const char *admlFile, const char *docFile, const wchar_t *language )
{
	WaitDllInitThread();
	HMODULE dll=NULL;
	if (language[0])
	{
		wchar_t path[_MAX_PATH];
		GetCurrentDirectory(_countof(path),path);
		PathAppend(path,language);
		PathAddExtension(path,L".dll");
		dll=LoadLibraryEx(path,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
	}
	LoadTranslationResources(dll,NULL);
	return SaveAdmx(COMPONENT_IE,admxFile,admlFile,docFile);
}
#endif

CSIEAPI bool DllImportSettingsXml( const wchar_t *fname )
{
	return ImportSettingsXml(fname);
}

CSIEAPI bool DllExportSettingsXml( const wchar_t *fname )
{
	return ExportSettingsXml(fname);
}

// DLL Entry Point
extern "C" BOOL WINAPI DllMain( HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved )
{
	if (dwReason==DLL_PROCESS_ATTACH)
	{
		wchar_t path[_MAX_PATH];
		GetModuleFileName(NULL,path,_countof(path));
		const wchar_t *exe=PathFindFileName(path);
		if (_wcsicmp(exe,L"explorer.exe")==0) return FALSE;
		if (_wcsicmp(exe,L"iexplore.exe")==0)
		{
			DWORD version=GetVersionEx(GetModuleHandle(NULL));
			if (version<0x09000000) return FALSE;

			CRegKey regSettings, regSettingsUser, regPolicy, regPolicyUser;
			bool bUpgrade=OpenSettingsKeys(COMPONENT_EXPLORER,regSettings,regSettingsUser,regPolicy,regPolicyUser);

			CSetting settings[]={
				{L"ShowCaption",CSetting::TYPE_BOOL,0,0,1},
				{L"ShowProgress",CSetting::TYPE_BOOL,0,0,1},
				{L"ShowZone",CSetting::TYPE_BOOL,0,0,1},
				{NULL}
			};

			settings[0].LoadValue(regSettings,regSettingsUser,regPolicy,regPolicyUser);
			settings[1].LoadValue(regSettings,regSettingsUser,regPolicy,regPolicyUser);
			settings[2].LoadValue(regSettings,regSettingsUser,regPolicy,regPolicyUser);

			if (!GetSettingBool(settings[0]) && !GetSettingBool(settings[1]) && !GetSettingBool(settings[2])) return FALSE;
		}

		g_Instance=hInstance;

		g_DllInitThread=CreateThread(NULL,0,DllInitThread,NULL,0,NULL);
	}

	return _AtlModule.DllMain(dwReason, lpReserved); 
}
