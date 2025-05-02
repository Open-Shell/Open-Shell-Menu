// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

// dllmain.cpp : Implementation of DllMain.

#include "stdafx.h"
#include "resource.h"
#include "..\Lib\resource.h"
#include "dllmain.h"
#include "ShareOverlay.h"
#include "SettingsUI.h"
#include "Settings.h"
#include "Translations.h"
#include "ResourceHelper.h"
#include "FNVHash.h"
#include <uxtheme.h>
#include <dwmapi.h>

#pragma comment(linker, \
	"\"/manifestdependency:type='Win32' "\
	"name='Microsoft.Windows.Common-Controls' "\
	"version='6.0.0.0' "\
	"processorArchitecture='*' "\
	"publicKeyToken='6595b64144ccf1df' "\
	"language='*'\"")

CClassicExplorerModule _AtlModule;
bool g_bLogLevel;

void InitClassicCopyProcess( void );
void InitClassicCopyThread( void );
void FreeClassicCopyThread( void );

bool g_bHookCopyThreads;
bool g_bExplorerExe;
LPCWSTR g_LoadedSettingsAtom;

static int g_LoadDialogs[]=
{
	IDD_SETTINGS,0x04000000,
	IDD_SETTINGSTREE,0x04000000,
	IDD_BROWSEFORICON,0x04000000,
	IDD_LANGUAGE,0x04000000,
	IDD_CUSTOMTOOLBAR,0x04000000,
	IDD_CUSTOMTREE,0x04000000,
	IDD_PROGRESS,0x04000004,
	0
};

struct FindChild
{
	const wchar_t *className;
	HWND hWnd;
};

static BOOL CALLBACK EnumChildProc( HWND hwnd, LPARAM lParam )
{
	FindChild &find=*(FindChild*)lParam;
	wchar_t name[256];
	GetClassName(hwnd,name,_countof(name));
	if (_wcsicmp(name,find.className)!=0) return TRUE;
	find.hWnd=hwnd;
	return FALSE;
}

HWND FindChildWindow( HWND hwnd, const wchar_t *className )
{
	FindChild find={className};
	EnumChildWindows(hwnd,EnumChildProc,(LPARAM)&find);
	return find.hWnd;
}

static DWORD g_TlsIndex;

TlsData *GetTlsData( void )
{
	void *pData=TlsGetValue(g_TlsIndex);
	if (!pData)
	{
		pData=(void*)LocalAlloc(LPTR,sizeof(TlsData));
		memset(pData,0,sizeof(TlsData));
		TlsSetValue(g_TlsIndex,pData);
	}
	return (TlsData*)pData;
}

static HANDLE g_DllInitThread;

static DWORD CALLBACK DllInitThread( void* )
{
	{
		g_bLogLevel=false;
		CRegKey regKey;
		if (regKey.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\ClassicExplorer\\Settings",KEY_READ|KEY_WOW64_64KEY)==ERROR_SUCCESS)
		{
			DWORD log;
			if (regKey.QueryDWORDValue(L"LogLevel",log)==ERROR_SUCCESS)
				g_bLogLevel=log!=0;
		}
	}
	InitSettings();

	wchar_t path[_MAX_PATH];
	GetModuleFileName(g_Instance,path,_countof(path));
	*PathFindFileName(path)=0;
	wchar_t fname[_MAX_PATH];
	Sprintf(fname,_countof(fname),L"%sExplorerL10N.ini",path);
	CString language=GetSettingString(L"Language");
	ParseTranslations(fname,language);

	HINSTANCE resInstance=LoadTranslationDll(language);

	LoadTranslationResources(resInstance,g_LoadDialogs);

	if (resInstance)
		FreeLibrary(resInstance);

	if (GetSettingBool(L"ShareOverlay") && (g_bExplorerExe || !GetSettingBool(L"ShareExplorer")))
		CShareOverlay::InitOverlay(GetSettingString(L"ShareOverlayIcon"),GetSettingBool(L"ShareOverlayHidden"));
	return 0;
}

void WaitDllInitThread( void )
{
	ATLASSERT(g_DllInitThread);
	WaitForSingleObject(g_DllInitThread,INFINITE);
}

// DLL Entry Point
extern "C" BOOL WINAPI DllMain( HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved )
{
	if (dwReason==DLL_PROCESS_ATTACH)
	{
		g_Instance=hInstance;
		g_TlsIndex=TlsAlloc();
		if (g_TlsIndex==TLS_OUT_OF_INDEXES) 
			return FALSE; // TLS failure

		CRegKey regSettings, regSettingsUser, regPolicy, regPolicyUser;
		bool bUpgrade=OpenSettingsKeys(COMPONENT_EXPLORER,regSettings,regSettingsUser,regPolicy,regPolicyUser);

		enum
		{
			SETTING_SHARE_OVERLAY,
			SETTING_SHARE_EXPLORER,
			SETTING_FILE_EXPLORER,
			SETTING_REPLACE_FILE,
			SETTING_REPLACE_FOLDER,
			SETTING_MORE,
			SETTING_WHITE_LIST,
			SETTING_BLACK_LIST,
		};

		CSetting settings[]={
			{L"ShareOverlay",CSetting::TYPE_BOOL,0,0,0},
			{L"ShareExplorer",CSetting::TYPE_BOOL,0,0,1},
			{L"FileExplorer",CSetting::TYPE_BOOL,0,0,1},
			{L"ReplaceFileUI",CSetting::TYPE_BOOL,0,0,1},
			{L"ReplaceFolderUI",CSetting::TYPE_BOOL,0,0,1},
			{L"EnableMore",CSetting::TYPE_BOOL,0,0,0},
			{L"ProcessWhiteList",CSetting::TYPE_STRING,0,0,L""},
			{L"ProcessBlackList",CSetting::TYPE_STRING,0,0,L""},
			{NULL}
		};

		wchar_t path[_MAX_PATH];
		GetModuleFileName(NULL,path,_countof(path));
		const wchar_t *exe=PathFindFileName(path);
		g_bExplorerExe=(_wcsicmp(exe,L"explorer.exe")==0 || _wcsicmp(exe,L"verclsid.exe")==0);
		bool bReplaceUI=false;
		if (GetWinVersion()<=WIN_VER_WIN7)
		{
			settings[SETTING_REPLACE_FILE].LoadValue(regSettings,regSettingsUser,regPolicy,regPolicyUser);
			settings[SETTING_REPLACE_FOLDER].LoadValue(regSettings,regSettingsUser,regPolicy,regPolicyUser);
			settings[SETTING_MORE].LoadValue(regSettings,regSettingsUser,regPolicy,regPolicyUser);
			bReplaceUI=(GetSettingBool(settings[SETTING_REPLACE_FILE]) || GetSettingBool(settings[SETTING_REPLACE_FOLDER]) || GetSettingBool(settings[SETTING_MORE]));
		}

		settings[SETTING_FILE_EXPLORER].LoadValue(regSettings,regSettingsUser,regPolicy,regPolicyUser);
		if (_wcsicmp(exe,L"regsvr32.exe")!=0 && _wcsicmp(exe,L"msiexec.exe")!=0 && _wcsicmp(exe,L"ClassicExplorerSettings.exe")!=0 && !g_bExplorerExe)
		{
			// some arbitrary app
			settings[SETTING_SHARE_OVERLAY].LoadValue(regSettings,regSettingsUser,regPolicy,regPolicyUser);
			settings[SETTING_SHARE_EXPLORER].LoadValue(regSettings,regSettingsUser,regPolicy,regPolicyUser);
			if ((!GetSettingBool(settings[SETTING_SHARE_OVERLAY]) || GetSettingBool(settings[SETTING_SHARE_EXPLORER])) && (!bReplaceUI || GetSettingBool(settings[SETTING_FILE_EXPLORER])))
				return FALSE;

			settings[SETTING_WHITE_LIST].LoadValue(regSettings,regSettingsUser,regPolicy,regPolicyUser);
			CString whiteList=GetSettingString(settings[SETTING_WHITE_LIST]);
			if (!whiteList.IsEmpty())
			{
				// check for whitelisted process names
				const wchar_t *str=whiteList;
				bool bFound=false;
				while (*str)
				{
					wchar_t token[_MAX_PATH];
					str=GetToken(str,token,_countof(token),L",;");
					wchar_t *start=token;
					while (*start==' ')
						start++;
					wchar_t *end=start+Strlen(start);
					while (end>start && end[-1]==' ')
						end--;
					*end=0;
					if (_wcsicmp(exe,start)==0)
					{
						bFound=true;
						break;
					}
				}
				if (!bFound)
					return FALSE;
			}
			else
			{
				settings[SETTING_BLACK_LIST].LoadValue(regSettings,regSettingsUser,regPolicy,regPolicyUser);
				// check for blacklisted process names
				CString blackList=GetSettingString(settings[SETTING_BLACK_LIST]);
				const wchar_t *str=blackList;
				while (*str)
				{
					wchar_t token[_MAX_PATH];
					str=GetToken(str,token,_countof(token),L",;");
					wchar_t *start=token;
					while (*start==' ')
						start++;
					wchar_t *end=start+Strlen(start);
					while (end>start && end[-1]==' ')
						end--;
					*end=0;
					if (_wcsicmp(exe,start)==0)
						return FALSE;
				}
			}
		}

		g_LoadedSettingsAtom=(LPCWSTR)GlobalAddAtom(L"ClassicExplorer.LoadedSettings");

		g_bHookCopyThreads=(bReplaceUI && (g_bExplorerExe || !GetSettingBool(settings[SETTING_FILE_EXPLORER])));
		if (g_bHookCopyThreads)
		{
			InitClassicCopyProcess();
			InitClassicCopyThread();
		}

		g_DllInitThread=CreateThread(NULL,0,DllInitThread,NULL,0,NULL);
	}

	if (dwReason==DLL_THREAD_ATTACH)
	{
		if (g_bHookCopyThreads)
			InitClassicCopyThread();
	}

	if (dwReason==DLL_THREAD_DETACH)
	{
		void *pData=TlsGetValue(g_TlsIndex);
		if (pData)
			LocalFree((HLOCAL)pData);
		TlsSetValue(g_TlsIndex,NULL);
		if (g_bHookCopyThreads)
			FreeClassicCopyThread();
	}

	if (dwReason==DLL_PROCESS_DETACH)
	{
		void *pData=TlsGetValue(g_TlsIndex);
		if (pData)
			LocalFree((HLOCAL)pData);
		TlsSetValue(g_TlsIndex,NULL);
		TlsFree(g_TlsIndex);
		GlobalDeleteAtom((ATOM)(uintptr_t)g_LoadedSettingsAtom);
	}

	return _AtlModule.DllMain(dwReason, lpReserved);
}
