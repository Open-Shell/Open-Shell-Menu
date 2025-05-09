// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <Psapi.h>
#include "StringUtils.h"

// Find and activate the Settings window
static BOOL CALLBACK FindSettingsEnum( HWND hwnd, LPARAM lParam )
{
	wchar_t className[256];
	if (!GetClassName(hwnd,className,_countof(className)) || _wcsicmp(className,L"#32770")!=0)
		return TRUE;
	DWORD process=0;
	GetWindowThreadProcessId(hwnd,&process);
	HANDLE hProcess=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,process);
	bool bFound=false;
	if (hProcess!=INVALID_HANDLE_VALUE)
	{
		wchar_t path[_MAX_PATH];
		if (GetModuleFileNameEx(hProcess,NULL,path,_countof(path)))
		{
			if (_wcsicmp(PathFindFileName(path),L"ClassicExplorerSettings.exe")==0)
			{
				SetForegroundWindow(hwnd);
				bFound=true;
			}
		}
		CloseHandle(hProcess);
	}
	return !bFound;
}

HMODULE LoadClassicExplorerDll( void )
{
	wchar_t path[_MAX_PATH];
	GetModuleFileName(NULL,path,_countof(path));
	*PathFindFileName(path)=0;
	PathAppend(path,L"ClassicExplorer32.dll");
	return LoadLibrary(path);
}

// A simple program that loads ClassicExplorer32.dll and calls the ShowExplorerSettings function
// Why not use rundll32 instead? Because it doesn't include the correct manifest for comctl32.dll
int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpstrCmdLine, int nCmdShow )
{
	INITCOMMONCONTROLSEX init={sizeof(init),ICC_STANDARD_CLASSES};
	InitCommonControlsEx(&init);

	{
		const wchar_t *pXml=wcsstr(lpstrCmdLine,L"-xml ");
		if (pXml)
		{
			wchar_t xml[_MAX_PATH];
			GetToken(pXml+5,xml,_countof(xml),L" ");
			HMODULE dll=LoadClassicExplorerDll();
			if (!dll) return 1;
			typedef bool (*tImportSettingsXml)( const wchar_t *fname );
			tImportSettingsXml DllImportSettingsXml=(tImportSettingsXml)GetProcAddress(dll,"DllImportSettingsXml");
			if (!DllImportSettingsXml)
				return 1;
			CoInitialize(NULL);
			bool res=DllImportSettingsXml(xml);
			CoUninitialize();
			return res?0:1;
		}
	}

	{
		const wchar_t *pBackup=wcsstr(lpstrCmdLine,L"-backup ");
		if (pBackup)
		{
			wchar_t xml[_MAX_PATH];
			GetToken(pBackup+8,xml,_countof(xml),L" ");

			HMODULE dll=LoadClassicExplorerDll();
			if (!dll) return 1;
			typedef bool (*tExportSettingsXml)( const wchar_t *fname );
			tExportSettingsXml DllExportSettingsXml=(tExportSettingsXml)GetProcAddress(dll,"DllExportSettingsXml");
			if (!DllExportSettingsXml)
				return 1;

			CoInitialize(NULL);
			bool res=DllExportSettingsXml(xml);
			CoUninitialize();
			return res?0:1;
		}
	}

#ifndef _WIN64
	const wchar_t *pSaveAdmx=wcsstr(lpstrCmdLine,L"-saveadmx ");
	if (pSaveAdmx)
	{
		wchar_t language[100];
		GetToken(pSaveAdmx+10,language,_countof(language),L" ");

		HMODULE dll=LoadClassicExplorerDll();
		if (!dll) return 1;
		typedef bool (*tSaveAdmx)( const char *admxFile, const char *admlFile, const char *docFile, const wchar_t *language );
		tSaveAdmx SaveAdmx=(tSaveAdmx)GetProcAddress(dll,"DllSaveAdmx");
		if (!SaveAdmx || !SaveAdmx("ClassicExplorer.admx","ClassicExplorer.adml","ClassicExplorerADMX.txt",language))
			return 1;
		return 0;
	}
#endif

	// prevent multiple instances from running on the same desktop
	// the assumption is that multiple desktops for the same user will have different name (but may repeat across users)
	wchar_t userName[256];
	DWORD len=_countof(userName);
	GetUserName(userName,&len);
	len=0;
	HANDLE desktop=GetThreadDesktop(GetCurrentThreadId());
	GetUserObjectInformation(desktop,UOI_NAME,NULL,0,&len);
	wchar_t *deskName=(wchar_t*)malloc(len);
	GetUserObjectInformation(desktop,UOI_NAME,deskName,len,&len);

	wchar_t mutexName[1024];
	Sprintf(mutexName,_countof(mutexName),L"ClassicExplorerSettings.Mutex.%s.%s",userName,deskName);
	free(deskName);

	HANDLE hMutex=CreateMutex(NULL,TRUE,mutexName);
	if (GetLastError()==ERROR_ALREADY_EXISTS || GetLastError()==ERROR_ACCESS_DENIED)
	{
		EnumWindows(FindSettingsEnum,0);
		return 0;
	}

	HMODULE dll=LoadClassicExplorerDll();
	if (!dll) return 1;

	FARPROC proc=GetProcAddress(dll,"ShowExplorerSettings");
	if (!proc) return 2;

	proc();
	return 0;
}
