// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#define STRICT_TYPED_ITEMIDS
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// some CString constructors will be explicit
#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <Psapi.h>
#include <atlstr.h>
#include "StringUtils.h"
#include "ResourceHelper.h"
#include "ClassicIEDLL\ClassicIEDLL.h"

// Manifest to enable the 6.0 common controls
#pragma comment(linker, \
	"\"/manifestdependency:type='Win32' "\
	"name='Microsoft.Windows.Common-Controls' "\
	"version='6.0.0.0' "\
	"processorArchitecture='*' "\
	"publicKeyToken='6595b64144ccf1df' "\
	"language='*'\"")

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
			if (_wcsicmp(PathFindFileName(path),L"ClassicIE_32.exe")==0)
			{
				SetForegroundWindow(hwnd);
				bFound=true;
			}
		}
		CloseHandle(hProcess);
	}
	return !bFound;
}

void ZoneConfigure( HWND hWnd, const wchar_t *url )
{
	// use undocumented function 383 from shlwapi
	typedef void (WINAPI* FZoneConfigureW)(HWND,LPCWSTR);
	FZoneConfigureW ZoneConfigureW;

	HMODULE	hShlwapi=LoadLibrary(L"shlwapi.dll");
	if(hShlwapi)
	{
		ZoneConfigureW=(FZoneConfigureW)GetProcAddress(hShlwapi,MAKEINTRESOURCEA(383));
		if(ZoneConfigureW)
			ZoneConfigureW(hWnd,url);
		FreeLibrary(hShlwapi);
	}
}

int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow )
{
	if (wcsncmp(lpCmdLine,L"zone ",5)==0)
	{
		wchar_t token[100];
		const wchar_t *url=GetToken(lpCmdLine+5,token,_countof(token),L" ");
		ZoneConfigure((HWND)(uintptr_t)_wtol(token),url);
		return 0;
	}

	{
		const wchar_t *pXml=wcsstr(lpCmdLine,L"-xml ");
		if (pXml)
		{
			wchar_t xml[_MAX_PATH];
			GetToken(pXml+5,xml,_countof(xml),L" ");
			CoInitialize(NULL);
			bool res=DllImportSettingsXml(xml);
			CoUninitialize();
			return res?0:1;
		}
	}

	{
		const wchar_t *pBackup=wcsstr(lpCmdLine,L"-backup ");
		if (pBackup)
		{
			wchar_t xml[_MAX_PATH];
			GetToken(pBackup+8,xml,_countof(xml),L" ");
			CoInitialize(NULL);
			bool res=DllExportSettingsXml(xml);
			CoUninitialize();
			return res?0:1;
		}
	}

#ifndef _WIN64
	const wchar_t *pSaveAdmx=wcsstr(lpCmdLine,L"-saveadmx ");
	if (pSaveAdmx)
	{
		wchar_t language[100];
		GetToken(pSaveAdmx+10,language,_countof(language),L" ");
		if (!DllSaveAdmx("ClassicIE.admx","ClassicIE.adml","ClassicIEADMX.txt",language))
			return 1;
		return 0;
	}
#endif

	WaitDllInitThread();

	DWORD settings=GetIESettings();

	HWND topWindow=(HWND)(uintptr_t)_wtol(lpCmdLine);
	if (topWindow)
	{
		DWORD processId;
		DWORD threadId=GetWindowThreadProcessId(topWindow,&processId);
		bool bWrongBitness=false;
		
		{
			HANDLE hProcess=OpenProcess(PROCESS_QUERY_INFORMATION,FALSE,processId);

			if (hProcess)
			{
				BOOL bWow64;
#ifdef _WIN64
				bWrongBitness=(IsWow64Process(hProcess,&bWow64) && bWow64); // the current process is 64-bit, but the target is wow64 (32-bit)
#else
				if (IsWow64Process(GetCurrentProcess(),&bWow64) && bWow64)
				{
					bWrongBitness=(!IsWow64Process(hProcess,&bWow64) || !bWow64); // the current process is 32-bit, but the target is 64-bit
				}
#endif
				CloseHandle(hProcess);
			}
		}

		if (bWrongBitness)
		{
			wchar_t path[_MAX_PATH];
			GetModuleFileName(hInstance,path,_countof(path));
			PathRemoveFileSpec(path);
#ifdef _WIN64
			PathAppend(path,L"ClassicIE_32.exe");
#else
			PathAppend(path,L"ClassicIE_64.exe");
#endif
			wchar_t cmdLine[1024];
			Sprintf(cmdLine,_countof(cmdLine),L"%s %s",path,lpCmdLine);
			STARTUPINFO startupInfo={sizeof(startupInfo)};
			PROCESS_INFORMATION processInfo;
			memset(&processInfo,0,sizeof(processInfo));
			if (CreateProcess(path,cmdLine,NULL,NULL,TRUE,0,NULL,NULL,&startupInfo,&processInfo))
			{
				CloseHandle(processInfo.hThread);
				CloseHandle(processInfo.hProcess);
			}
			return settings;
		}

		CheckForNewVersionIE();

		if (!(settings&IE_SETTING_CAPTION))
			return settings;

		HWND caption=FindWindowEx(topWindow,NULL,L"Client Caption",NULL);
		DllLogToFile(CIE_LOG,L"exe: topWindow=%p, caption=%p",topWindow,caption);
		UINT message=RegisterWindowMessage(L"ClassicIE.Injected");
		if (caption)
		{
			if (SendMessage(caption,message,0,0)!=0)
				return settings;

			{
				HANDLE hToken;
				if (OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&hToken))
				{
					TOKEN_PRIVILEGES tp={1};
					if (LookupPrivilegeValue(NULL,L"SeDebugPrivilege",&tp.Privileges[0].Luid))
						tp.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
					AdjustTokenPrivileges(hToken,FALSE,&tp,sizeof(TOKEN_PRIVILEGES),NULL,NULL); 
					CloseHandle(hToken);
				}
			}

#ifdef _WIN64
			HMODULE hHookModule=GetModuleHandle(L"ClassicIEDLL_64.dll");
#else
			HMODULE hHookModule=GetModuleHandle(L"ClassicIEDLL_32.dll");
#endif

			HANDLE hProcess=OpenProcess(PROCESS_ALL_ACCESS,FALSE,processId);
			if (hProcess)
			{
				wchar_t path[_MAX_PATH];
				GetModuleFileName(hHookModule,path,_countof(path));
				void *remotePath=VirtualAllocEx(hProcess,NULL,sizeof(path),MEM_COMMIT,PAGE_READWRITE);
				if (remotePath)
				{
					if (WriteProcessMemory(hProcess,remotePath,path,sizeof(path),NULL))
					{
						HANDLE hThread=CreateRemoteThread(hProcess,NULL,0,(LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandle(L"kernel32.dll"),"LoadLibraryW"),remotePath,0,NULL);
						if (hThread)
						{
							WaitForSingleObject(hThread,INFINITE);
							CloseHandle(hThread);
						}
					}
					VirtualFreeEx(hProcess,remotePath,sizeof(path),MEM_RELEASE);
				}
				CloseHandle(hProcess);
			}
		}
		return settings;
	}

#ifndef _WIN64
	if (*lpCmdLine)
#endif
		return settings;

	// if 32-bit exe is called with no arguments, show the settings

	INITCOMMONCONTROLSEX init={sizeof(init),ICC_STANDARD_CLASSES};
	InitCommonControlsEx(&init);

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
	Sprintf(mutexName,_countof(mutexName),L"ClassicIESettings.Mutex.%s.%s",userName,deskName);
	free(deskName);

	HANDLE hMutex=CreateMutex(NULL,TRUE,mutexName);
	if (GetLastError()==ERROR_ALREADY_EXISTS || GetLastError()==ERROR_ACCESS_DENIED)
	{
		EnumWindows(FindSettingsEnum,0);
		return 0;
	}

	ShowIESettings();
	return 0;
}
