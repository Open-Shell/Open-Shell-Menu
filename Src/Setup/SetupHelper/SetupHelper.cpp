// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2024, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#define STRICT_TYPED_ITEMIDS
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// some CString constructors will be explicit
#define VC_EXTRALEAN
#define NOMINMAX
#define _ATL_ALL_WARNINGS
#include <windows.h>
#include <atlstr.h>
#include "ResourceHelper.h"

///////////////////////////////////////////////////////////////////////////////

int ExitStartMenu(const wchar_t* regPath, const wchar_t* exeName, const wchar_t* updaterClass)
{
	HKEY hKey=NULL;
	if (RegCreateKeyEx(HKEY_LOCAL_MACHINE,regPath,0,NULL,REG_OPTION_NON_VOLATILE,KEY_READ|KEY_QUERY_VALUE|KEY_WOW64_64KEY,NULL,&hKey,NULL)==ERROR_SUCCESS)
	{
		DWORD type=0;
		wchar_t path[_MAX_PATH];
		DWORD size=sizeof(path);
		if (RegQueryValueEx(hKey,L"Path",0,&type,(BYTE*)path,&size)==ERROR_SUCCESS && type==REG_SZ)
		{
			STARTUPINFO startupInfo={sizeof(startupInfo)};
			PROCESS_INFORMATION processInfo;
			memset(&processInfo,0,sizeof(processInfo));
			wcscat_s(path,exeName);
			HANDLE h=CreateFile(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
			if (h!=INVALID_HANDLE_VALUE)
			{
				CloseHandle(h);
				wcscat_s(path,L" -exit");
				if (CreateProcess(NULL,path,NULL,NULL,TRUE,0,NULL,NULL,&startupInfo,&processInfo))
				{
					CloseHandle(processInfo.hThread);
					WaitForSingleObject(processInfo.hProcess,5000);
					CloseHandle(processInfo.hProcess);
				}
			}
		}
		RegCloseKey(hKey);
	}
	HWND updateOwner=FindWindow(updaterClass,NULL);
	if (updateOwner)
		PostMessage(updateOwner,WM_CLEAR,0,0);
	return 0;
}

int ExitStartMenu()
{
	// terminate Open-Shell Start Menu in clean way
	ExitStartMenu(L"SOFTWARE\\OpenShell\\OpenShell", L"StartMenu.exe", L"OpenShellUpdate.COwnerWindow");

	// try to terminate also ClassicShell Start Menu (to make upgrade a bit easier)
	ExitStartMenu(L"SOFTWARE\\IvoSoft\\ClassicShell", L"ClassicStartMenu.exe", L"ClassicShellUpdate.COwnerWindow");

	return 0;
}

int FixVersion( void )
{
	HKEY hKey=NULL;
	if (RegCreateKeyEx(HKEY_LOCAL_MACHINE,L"SOFTWARE\\OpenShell\\OpenShell",0,NULL,REG_OPTION_NON_VOLATILE,KEY_READ|KEY_WRITE|KEY_WOW64_64KEY,NULL,&hKey,NULL)==ERROR_SUCCESS)
	{
		DWORD winVer=GetVersionEx(GetModuleHandle(L"user32.dll"));
		RegSetValueEx(hKey,L"WinVersion",NULL,REG_DWORD,(BYTE*)&winVer,sizeof(DWORD));
		RegCloseKey(hKey);
	}
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

// Setup Helper - performs custom actions during Open-Shell install/uninstall
// Usage:
//   exitSM // exits the start menu if it is running
//   fixVersion // sets the correct OS version

int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpstrCmdLine, int nCmdShow )
{
//	MessageBox(NULL,lpstrCmdLine,L"Command Line",MB_OK|MB_SYSTEMMODAL);

	int count;
	wchar_t *const *params=CommandLineToArgvW(lpstrCmdLine,&count);
	if (!params) return 1;

	for (;count>0;count--,params++)
	{
		if (_wcsicmp(params[0],L"exitSM")==0)
		{
			return ExitStartMenu();
		}
		if (_wcsicmp(params[0],L"fixVersion")==0)
		{
			return FixVersion();
		}
	}
	return 1;
}
