// StartMenuHelper.cpp : Implementation of DLL Exports.


#include "stdafx.h"
#include "resource.h"
#include "StartMenuHelper_h.h"
#include "dllmain.h"
#include "ResourceHelper.h"
#include "Settings.h"
#include "StringUtils.h"
#include "..\StartMenuDLL\LogManager.h"
#include <AccCtrl.h>
#include <Aclapi.h>
#include <Taskschd.h>

//#define EXPLORER_CLSID L"{c71c41f1-ddad-42dc-a8fc-f5bfc61df957}"
//const CLSID g_ExplorerClsid= {0xC71C41F1, 0xDDAD, 0x42DC, {0xA8, 0xFC, 0xF5, 0xBF, 0xC6, 0x1D, 0xF9, 0x57}};
//#define EXPLORER_DLL L"twinui.dll"

#define EXPLORER_CLSID L"{ECD4FC4D-521C-11D0-B792-00A0C90312E1}"
const CLSID g_ExplorerClsid= {0xECD4FC4D, 0x521C, 0x11D0, {0xB7, 0x92, 0x00, 0xA0, 0xC9, 0x03, 0x12, 0xE1}};
#define EXPLORER_DLL L"explorerframe.dll"

#define EMULATION_CLSID L"{D3214FBB-3CA1-406a-B3E8-3EB7C393A15E}"
const CLSID g_EmulationClsid= {0xD3214FBB, 0x3CA1, 0x406A, {0xB3, 0xE8, 0x3E, 0xB7, 0xC3, 0x93, 0xA1, 0x5E}};
#define EMULATION_KEY L"TreatAs"

#define SHELLEXT_NAME L"StartMenuExt"

static void AdjustPrivileges( void )
{
	HANDLE hToken;
	if (OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&hToken))
	{
		{
			TOKEN_PRIVILEGES tp={1};
			if (LookupPrivilegeValue(NULL,L"SeBackupPrivilege",&tp.Privileges[0].Luid))
				tp.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
			AdjustTokenPrivileges(hToken,FALSE,&tp,sizeof(TOKEN_PRIVILEGES),NULL,NULL); 
		}
		{
			TOKEN_PRIVILEGES tp={1};
			if (LookupPrivilegeValue(NULL,L"SeRestorePrivilege",&tp.Privileges[0].Luid))
				tp.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
			AdjustTokenPrivileges(hToken,FALSE,&tp,sizeof(TOKEN_PRIVILEGES),NULL,NULL); 
		}
		CloseHandle(hToken);
	}
}

static void AddShellExt(const wchar_t* progID, const LPSECURITY_ATTRIBUTES sa)
{
	HKEY hkey = NULL;

	if (RegCreateKeyEx(HKEY_CLASSES_ROOT, CString(progID) + L"\\ShellEx\\ContextMenuHandlers\\" SHELLEXT_NAME, NULL, NULL, REG_OPTION_BACKUP_RESTORE, KEY_WRITE, sa, &hkey, NULL) == ERROR_SUCCESS)
	{
		wchar_t val[] = L"{E595F05F-903F-4318-8B0A-7F633B520D2B}";
		RegSetValueEx(hkey, NULL, NULL, REG_SZ, (BYTE*)val, sizeof(val));
		RegCloseKey(hkey);
	}
}

static void AddRegistryKeys( bool bPin )
{
	AdjustPrivileges();
	PSID pEveryoneSID=NULL, pAdminSID=NULL;

	// Create a well-known SID for the Everyone group.
	SID_IDENTIFIER_AUTHORITY SIDAuthWorld=SECURITY_WORLD_SID_AUTHORITY;
	if (!AllocateAndInitializeSid(&SIDAuthWorld,1,SECURITY_WORLD_RID,0,0,0,0,0,0,0,&pEveryoneSID))
		return;

	// Create a SID for the BUILTIN\Administrators group.
	SID_IDENTIFIER_AUTHORITY SIDAuthNT=SECURITY_NT_AUTHORITY;
	if (!AllocateAndInitializeSid(&SIDAuthNT,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&pAdminSID))
	{
		if (pEveryoneSID) FreeSid(pEveryoneSID);
	}

	EXPLICIT_ACCESS ea[2];
	memset(&ea,0,sizeof(ea));

	// Initialize an EXPLICIT_ACCESS structure for an ACE.
	// The ACE will allow Everyone read access to the key.
	ea[0].grfAccessPermissions=KEY_READ;
	ea[0].grfAccessMode=SET_ACCESS;
	ea[0].grfInheritance=NO_INHERITANCE;
	ea[0].Trustee.TrusteeForm=TRUSTEE_IS_SID;
	ea[0].Trustee.TrusteeType=TRUSTEE_IS_WELL_KNOWN_GROUP;
	ea[0].Trustee.ptstrName=(LPTSTR)pEveryoneSID;

	// Initialize an EXPLICIT_ACCESS structure for an ACE.
	// The ACE will allow the Administrators group full access to
	// the key.
	ea[1].grfAccessPermissions=KEY_ALL_ACCESS;
	ea[1].grfAccessMode=SET_ACCESS;
	ea[1].grfInheritance=NO_INHERITANCE;
	ea[1].Trustee.TrusteeForm=TRUSTEE_IS_SID;
	ea[1].Trustee.TrusteeType=TRUSTEE_IS_GROUP;
	ea[1].Trustee.ptstrName=(LPTSTR)pAdminSID;

	// Create a new ACL that contains the new ACEs.
	PACL pACL=NULL;
	if (SetEntriesInAcl(2,ea,NULL,&pACL)==ERROR_SUCCESS)
	{
		char buf[SECURITY_DESCRIPTOR_MIN_LENGTH];
		SECURITY_DESCRIPTOR *psd=(SECURITY_DESCRIPTOR*)buf;
		if (InitializeSecurityDescriptor(psd,SECURITY_DESCRIPTOR_REVISION))
		{  
			if (SetSecurityDescriptorDacl(psd,TRUE,pACL,FALSE))
			{
				SECURITY_ATTRIBUTES sa={sizeof(sa),psd};
				HKEY hkey=NULL;
				if (RegCreateKeyEx(HKEY_CLASSES_ROOT,L"CLSID\\" EXPLORER_CLSID L"\\" EMULATION_KEY,NULL,NULL,REG_OPTION_BACKUP_RESTORE,KEY_WRITE,&sa,&hkey,NULL)==ERROR_SUCCESS)
				{
					wchar_t val[]=EMULATION_CLSID;
					RegSetValueEx(hkey,NULL,NULL,REG_SZ,(BYTE*)val,sizeof(val));
					RegCloseKey(hkey);
				}

				if (bPin)
				{
					AddShellExt(L"Launcher.ImmersiveApplication", &sa);
					AddShellExt(L"Launcher.DesktopPackagedApplication", &sa);
					AddShellExt(L"Launcher.SystemSettings", &sa);
				}
			}
		} 
		LocalFree(pACL);
	}
	FreeSid(pEveryoneSID);
	FreeSid(pAdminSID);
}

static void RemoveShellExt(const wchar_t* progID)
{
	HKEY hkey = NULL;
	if (RegCreateKeyEx(HKEY_CLASSES_ROOT, CString(progID) + L"\\ShellEx\\ContextMenuHandlers", NULL, NULL, REG_OPTION_BACKUP_RESTORE, KEY_WRITE | DELETE, NULL, &hkey, NULL) == ERROR_SUCCESS)
	{
		RegDeleteTree(hkey, SHELLEXT_NAME);
		RegCloseKey(hkey);
	}
}

static void RemoveRegistryKeys( bool bPin )
{
	AdjustPrivileges();
	HKEY hkey=NULL;
	if (RegCreateKeyEx(HKEY_CLASSES_ROOT,L"CLSID\\" EXPLORER_CLSID,NULL,NULL,REG_OPTION_BACKUP_RESTORE,KEY_WRITE|DELETE,NULL,&hkey,NULL)==ERROR_SUCCESS)
	{
		RegDeleteTree(hkey,EMULATION_KEY);
		RegCloseKey(hkey);
	}

	if (bPin)
	{
		RemoveShellExt(L"Launcher.ImmersiveApplication");
		RemoveShellExt(L"Launcher.DesktopPackagedApplication");
		RemoveShellExt(L"Launcher.SystemSettings");
	}
}

// Used to determine whether the DLL can be unloaded by OLE
STDAPI DllCanUnloadNow(void)
{
	return _AtlModule.DllCanUnloadNow();
}

typedef HRESULT (__stdcall *FDllGetClassObject)(REFCLSID,REFIID,LPVOID*);

static HMODULE g_ExplorerModule=NULL;

static void StartStartMenu( void )
{
	STARTUPINFO startupInfo={sizeof(STARTUPINFO)};
	PROCESS_INFORMATION processInfo;
	memset(&processInfo,0,sizeof(processInfo));
	wchar_t path[_MAX_PATH];
	path[0]=0;
	{
		CRegKey regKey;
		if (regKey.Open(HKEY_LOCAL_MACHINE,L"Software\\OpenShell\\OpenShell",KEY_READ|KEY_WOW64_64KEY)==ERROR_SUCCESS)
		{
			ULONG size=_countof(path);
			if (regKey.QueryStringValue(L"Path",path,&size)!=ERROR_SUCCESS)
				path[0]=0;
		}
	}
	if (!path[0])
	{
		GetModuleFileName(g_Instance,path,_countof(path));
		PathRemoveFileSpec(path);
	}
	PathAppend(path,L"StartMenu.exe");
	LogToFile(STARTUP_LOG,L"StartMenuHelper: starting \"%s\" -startup",path);
	if (CreateProcess(path,(LPWSTR)L"StartMenu.exe -startup",NULL,NULL,TRUE,0,NULL,NULL,&startupInfo,&processInfo))
	{
		CloseHandle(processInfo.hProcess);
		CloseHandle(processInfo.hThread);
	}
	else
		LogToFile(STARTUP_LOG,L"StartMenuHelper: starting failed: 0x%08X",GetLastError());
}

// Returns a class factory to create an object of the requested type
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
	WaitDllInitThread();
	if (rclsid==g_EmulationClsid)
	{
		LogToFile(STARTUP_LOG,L"StartMenuHelper: DllGetClassObject1");
		HRESULT res=E_FAIL;
		if (!g_ExplorerModule)
			g_ExplorerModule=LoadLibrary(EXPLORER_DLL);
		if (g_ExplorerModule)
		{
			LogToFile(STARTUP_LOG,L"StartMenuHelper: DllGetClassObject2");
			if (GetSettingBool(L"AutoStart"))
				StartStartMenu();
			FDllGetClassObject func=(FDllGetClassObject)GetProcAddress(g_ExplorerModule,"DllGetClassObject");
			if (func)
				res=func(g_ExplorerClsid,riid,ppv);
		}
		return res;
	}
	return _AtlModule.DllGetClassObject(rclsid, riid, ppv);
}
/*
const wchar_t *TASK_NAME=L"Open-Shell Start Menu";
const wchar_t *TASK_XML=
L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
L"  <RegistrationInfo>\r\n"
L"    <Date>2014-11-23T10:00:00</Date>\r\n"
L"    <Author>Open-Shell</Author>\r\n"
L"    <Description>A task that launches the start menu at logon. It also repairs the installation after an upgrade of Windows.</Description>\r\n"
L"  </RegistrationInfo>\r\n"
L"  <Triggers>\r\n"
L"    <LogonTrigger>\r\n"
L"      <Enabled>true</Enabled>\r\n"
L"    </LogonTrigger>\r\n"
L"  </Triggers>\r\n"
L"  <Principals>\r\n"
L"    <Principal id=\"Author\">\r\n"
L"      <RunLevel>LeastPrivilege</RunLevel>\r\n"
L"      <GroupId>BUILTIN\\Users</GroupId>\r\n"
L"    </Principal>\r\n"
L"  </Principals>\r\n"
L"  <Settings>\r\n"
L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n"
L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n"
L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n"
L"    <AllowHardTerminate>false</AllowHardTerminate>\r\n"
L"    <StartWhenAvailable>false</StartWhenAvailable>\r\n"
L"    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>\r\n"
L"    <IdleSettings>\r\n"
L"      <StopOnIdleEnd>true</StopOnIdleEnd>\r\n"
L"      <RestartOnIdle>false</RestartOnIdle>\r\n"
L"    </IdleSettings>\r\n"
L"    <AllowStartOnDemand>false</AllowStartOnDemand>\r\n"
L"    <Enabled>true</Enabled>\r\n"
L"    <Hidden>false</Hidden>\r\n"
L"    <RunOnlyIfIdle>false</RunOnlyIfIdle>\r\n"
L"    <WakeToRun>false</WakeToRun>\r\n"
L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n"
L"  </Settings>\r\n"
L"  <Actions Context=\"Author\">\r\n"
L"    <Exec>\r\n"
L"      <Command>\"%s\"</Command>\r\n"
L"      <Arguments>-autorun</Arguments>\r\n"
L"    </Exec>\r\n"
L"  </Actions>\r\n"
L"</Task>\r\n";
*/
static void InstallUpgradeTask( bool bInstall )
{
	wchar_t exePath[_MAX_PATH];
	exePath[0]=0;
	{
		CRegKey regKey;
		if (regKey.Open(HKEY_LOCAL_MACHINE,L"Software\\OpenShell\\OpenShell",KEY_READ|KEY_WRITE|KEY_WOW64_64KEY)==ERROR_SUCCESS)
		{
			if (bInstall)
			{
				ULONG size=_countof(exePath);
				if (regKey.QueryStringValue(L"Path",exePath,&size)==ERROR_SUCCESS)
				{
					PathAppend(exePath,L"StartMenu.exe");
				}
				else
				{
					exePath[0]=0;
				}
				regKey.SetDWORDValue(L"WinVersion",GetVersionEx(GetModuleHandle(L"user32.dll")));
			}
			else
			{
				regKey.DeleteValue(L"WinVersion");
			}
		}
	}
/*
	CComPtr<ITaskService> pService;
	pService.CoCreateInstance(CLSID_TaskScheduler);
	if (pService && SUCCEEDED(pService->Connect(CComVariant(),CComVariant(),CComVariant(),CComVariant())))
	{
		CComPtr<ITaskFolder> pFolder;
		if (SUCCEEDED(pService->GetFolder(CComBSTR(L""),&pFolder)) && pFolder)
		{
			pFolder->DeleteTask(CComBSTR(TASK_NAME),0);
			if (bInstall)
			{
				wchar_t buf[4096];
				Sprintf(buf,_countof(buf),TASK_XML,exePath);
				CComPtr<IRegisteredTask> pTask;
				pFolder->RegisterTask(CComBSTR(TASK_NAME),CComBSTR(buf),TASK_CREATE,CComVariant(),CComVariant(),TASK_LOGON_INTERACTIVE_TOKEN,CComVariant(L""),&pTask);
			}
		}
	}
*/
}


// DllRegisterServer - Adds entries to the system registry
STDAPI DllRegisterServer(void)
{
	WaitDllInitThread();
	CoInitialize(NULL);
	// registers object, typelib and all interfaces in typelib
	HRESULT hr = _AtlModule.DllRegisterServer(FALSE);
#ifdef BUILD_SETUP
	BOOL bWow64=FALSE;
	// GetVersion lies! it always returns Vista when running inside msiexec. must check version of some system dll
	WORD winVer=HIWORD(GetVersionEx(GetModuleHandle(L"user32.dll")));
	if (SUCCEEDED(hr) && winVer>=WIN_VER_WIN8 && IsWow64Process(GetCurrentProcess(),&bWow64) && !bWow64)
	{
		AddRegistryKeys(winVer>=WIN_VER_WIN10);
		InstallUpgradeTask(true);
	}
	
#endif
	CoUninitialize();
	return hr;
}


// DllUnregisterServer - Removes entries from the system registry
STDAPI DllUnregisterServer(void)
{
	WaitDllInitThread();
	CoInitialize(NULL);
#ifdef BUILD_SETUP
	BOOL bWow64=FALSE;
	WORD winVer=HIWORD(GetVersionEx(GetModuleHandle(L"user32.dll")));
	if (winVer>=WIN_VER_WIN8 && IsWow64Process(GetCurrentProcess(),&bWow64) && !bWow64)
	{
		RemoveRegistryKeys(winVer>=WIN_VER_WIN10);
		InstallUpgradeTask(false);
	}
#endif
	HRESULT hr = _AtlModule.DllUnregisterServer(FALSE);
	CoUninitialize();
	return hr;
}

// DllInstall - Adds/Removes entries to the system registry per user
//              per machine.	
STDAPI DllInstall(BOOL bInstall, LPCWSTR pszCmdLine)
{
	WaitDllInitThread();
	HRESULT hr = E_FAIL;
	static const wchar_t szUserSwitch[] = _T("user");

	if (pszCmdLine != NULL)
	{
		if (_wcsnicmp(pszCmdLine, szUserSwitch, _countof(szUserSwitch)) == 0)
		{
			AtlSetPerUserRegistration(true);
		}
	}

	if (bInstall)
	{
		hr = DllRegisterServer();
		if (FAILED(hr))
		{
			DllUnregisterServer();
		}
	}
	else
	{
		hr = DllUnregisterServer();
	}

	return hr;
}
