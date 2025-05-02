// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include <stdafx.h>
#include <dbghelp.h>
#include <wuapi.h>
#include "StringUtils.h"
#include "ResourceHelper.h"
#include "TrackResources.h"
#include "ComHelper.h"
#include "Settings.h"
#include "psapi.h"
#include "Legacy.h"

#include "StartMenuDLL\StartMenuDLL.h"
#include "StartMenuDLL\SettingsUI.h"
#include "StartMenuDLL\resource.h"
#include "StartMenuDLL\LogManager.h"

static HHOOK g_StartHook;

static void UnhookStartMenu( void )
{
	if (g_StartHook)
		UnhookWindowsHookEx(g_StartHook);
	g_StartHook=NULL;
}

enum THookMode
{
	HOOK_NONE, // don't hook Explorer, running as a separate exe
	HOOK_NORMAL, // hook Explorer normally, no retry
	HOOK_STARTUP, // retry to hook Explorer
	HOOK_STARTUP2, // retry to hook Explorer (for secondary desktops)
};

static bool HookStartMenu( THookMode mode, HWND &menu )
{
	HMODULE hHookModule=GetModuleHandle(L"StartMenuDLL.dll");

	// find the Progman window and the start button

	DllLogToFile(STARTUP_LOG,L"StartMenu: hooking Explorer");
	HWND progWin=NULL;
	bool bFindAppManager=(mode==HOOK_STARTUP && GetWinVersion()>=WIN_VER_WIN8 && GetWinVersion()<=WIN_VER_WIN81);
	for (int i=0;i<120;i++) // retry for 1 minute
	{
		if (bFindAppManager)
			bFindAppManager=!FindWindow(L"ApplicationManager_DesktopShellWindow",NULL);
		if (!bFindAppManager)
		{
			progWin=FindWindowEx(NULL,NULL,L"Progman",NULL);
			if (progWin) break;
			if (mode!=HOOK_STARTUP && mode!=HOOK_STARTUP2) return true; // the Progman window may not be created yet (if Explorer is currently restarting)
		}
		DllLogToFile(STARTUP_LOG,L"StartMenu: can't find Progman, retrying");
		Sleep(500);
	}
	if (!progWin)
		return true;
	DWORD process=0;
	DWORD thread=GetWindowThreadProcessId(progWin,&process);
	HANDLE hProcess=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,process);
	if (hProcess)
	{
		bool success=false;
		wchar_t path[MAX_PATH];
		if (GetModuleFileNameEx(hProcess,NULL,path,_countof(path)))
		{
			if (_wcsicmp(PathFindFileName(path),L"explorer.exe")==0)
				success=true;
			else
				DllLogToFile(STARTUP_LOG,L"StartMenu: found wrong process %s",path);
		}
		else
			DllLogToFile(STARTUP_LOG,L"StartMenu: failed to get process name");
		CloseHandle(hProcess);
		if (!success)
			return false;
	}
	else
		DllLogToFile(STARTUP_LOG,L"StartMenu: failed to open process %d",process);

	for (int i=0;i<10;i++) // retry for 5 sec
	{
		if (FindTaskBar(process)) break;
		if (mode!=HOOK_STARTUP && mode!=HOOK_STARTUP2) return false; // the taskbar may not be created yet (if Explorer is currently restarting)
		DllLogToFile(STARTUP_LOG,L"StartMenu: can't find taskbar, retrying");
		Sleep(500);
	}

	if (mode==HOOK_NONE)
	{
		menu=ToggleStartMenu(-1,false);
		return true;
	}

	// install hooks in the explorer process
	thread=GetWindowThreadProcessId(g_TaskBar,NULL);
	g_StartHook=SetWindowsHookEx(WH_GETMESSAGE,HookInject,hHookModule,thread);
	if (!g_StartHook)
	{
		int err=GetLastError();
		DllLogToFile(STARTUP_LOG,L"StartMenu: hook failed: 0x%08X",err);
	}
	PostMessage(g_TaskBar,WM_NULL,0,0); // make sure there is one message in the queue

	return true;
}

static UINT g_TaskbarCreatedMsg; // the "TaskbarCreated" message

// CStartHookWindow is a hidden window that waits for the "TaskbarCreated" message and rehooks the explorer process
// Also when the start menu wants to shut down it sends WM_CLOSE to this window, which unhooks explorer and exits

const int WM_OPEN=WM_USER+10;

const int TIMER_HOOK=1;

class CStartHookWindow: public CWindowImpl<CStartHookWindow>
{
public:

	DECLARE_WND_CLASS(L"OpenShellMenu.CStartHookWindow")

	BEGIN_MSG_MAP( CStartHookWindow )
		MESSAGE_HANDLER( WM_OPEN, OnOpen )
		MESSAGE_HANDLER( WM_CLOSE, OnClose )
		MESSAGE_HANDLER( WM_CLEAR, OnClear )
		MESSAGE_HANDLER( WM_TIMER, OnTimer )
		MESSAGE_HANDLER( g_TaskbarCreatedMsg, OnTaskbarCreated )
	END_MSG_MAP()

protected:
	// Handler prototypes:
	//  LRESULT MessageHandler(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	//  LRESULT CommandHandler(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	//  LRESULT NotifyHandler(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnOpen( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnClose( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnClear( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnTimer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnTaskbarCreated( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
};

LRESULT CStartHookWindow::OnOpen( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (g_TaskBar) ::PostMessage(g_TaskBar,RegisterWindowMessage(L"OpenShellMenu.StartMenuMsg"),wParam,lParam);
	return 0;
}

LRESULT CStartHookWindow::OnClose( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	UnhookStartMenu();
	Sleep(100);
	PostQuitMessage(0);
	return 0;
}

LRESULT CStartHookWindow::OnClear( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	UnhookStartMenu();
	return 0;
}

LRESULT CStartHookWindow::OnTaskbarCreated( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	DllLogToFile(STARTUP_LOG,L"StartMenu: Taskbar Created");
	SetTimer(TIMER_HOOK,100);
	return 0;
}

LRESULT CStartHookWindow::OnTimer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==TIMER_HOOK)
	{
		UnhookStartMenu();
		HWND menu;
		HookStartMenu(HOOK_NORMAL,menu);
		if (g_StartHook)
			KillTimer(TIMER_HOOK);
	}
	return 0;
}

enum
{
	CMD_NONE=-1,
	CMD_TOGGLE_NEW=-2,
};

static const GUID IID_IWpnTileSink={0xe716b283,0x6be7,0x4e6f,{0xa8,0x8f,0x1c,0xde,0x47,0xd5,0xe3,0x55}};
interface IWpnTileSink : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE TileSessionUpdated(unsigned long,void const *,unsigned long) = 0;
	virtual HRESULT STDMETHODCALLTYPE TileSessionNotificationCleared(unsigned long,void const *,unsigned long) = 0;
	virtual HRESULT STDMETHODCALLTYPE TileSessionImageDownloadUpdated(unsigned long,int) = 0;
	virtual HRESULT STDMETHODCALLTYPE TileRequestNotificationCompleted(void const * ptr64,unsigned long) = 0;
	virtual HRESULT STDMETHODCALLTYPE TileSessionImageDownloadUpdated2(unsigned long,int) = 0;
};

class CWpnTileSink : public IWpnTileSink
{
	// from IUnknown
	virtual HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void **ppvObject );
	virtual ULONG STDMETHODCALLTYPE AddRef( void ) { return 1; }
	virtual ULONG STDMETHODCALLTYPE Release( void ) { return 1; }

	virtual HRESULT STDMETHODCALLTYPE TileSessionUpdated(unsigned long a,void const *b,unsigned long c)
	{
		return S_OK;
	}

	virtual HRESULT STDMETHODCALLTYPE TileSessionNotificationCleared(unsigned long a,void const *b,unsigned long c)
	{
		return S_OK;
	}

	virtual HRESULT STDMETHODCALLTYPE TileSessionImageDownloadUpdated(unsigned long a,int b)
	{
		return S_OK;
	}

	virtual HRESULT STDMETHODCALLTYPE TileRequestNotificationCompleted(void const * a,unsigned long b)
	{
		return S_OK;
	}

	virtual HRESULT STDMETHODCALLTYPE TileSessionImageDownloadUpdated2(unsigned long a,int b)
	{
		return S_OK;
	}
};

HRESULT STDMETHODCALLTYPE CWpnTileSink::QueryInterface( REFIID riid, void **ppvObject )
{
	*ppvObject=NULL;
	if (riid==IID_IUnknown || riid==IID_IWpnTileSink)
	{
		*ppvObject=this;
		return S_OK;
	}
	return E_NOINTERFACE;
}

MIDL_INTERFACE("0e467ac1-65f2-48d6-8bf2-375430548a87")
IWpnPresentationEndpoint : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE TileCreateSession( unsigned long * ) = 0;
	virtual HRESULT STDMETHODCALLTYPE TileCloseSession( unsigned long ) = 0;
	virtual HRESULT STDMETHODCALLTYPE TileUpdateSession( unsigned long,const void *,unsigned long ) = 0; // (1, str, 1)
	virtual HRESULT STDMETHODCALLTYPE TileRequestNotification( const void *, unsigned long ) = 0; // (str, 1)*/
};

const CLSID CLSID_WpnPlatform={0x0c9281f9, 0x6da1, 0x4006, {0x87, 0x29, 0xde, 0x6e, 0x6b, 0x61, 0x58, 0x1c}};
MIDL_INTERFACE("9fa045cb-b9b3-47ba-842f-e2ab458f2b0c")
IWpnPlatform : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE CreateAppEndpoint( struct IWpnAppEndpoint **ppEndpoint )	= 0;
	virtual HRESULT STDMETHODCALLTYPE CreatePresentationEndpoint(struct IWpnPresentationEndpoint ** ppEndpoint )	= 0;
/*	virtual HRESULT STDMETHODCALLTYPE CreateRegistrationEndpoint(struct IWpnRegistrationEndpoint * ptr64 * ptr64)	= 0;
	virtual HRESULT STDMETHODCALLTYPE CreateSettingsEndpoint(struct IWpnSettingsEndpoint * ptr64 * ptr64)	= 0;
	virtual HRESULT STDMETHODCALLTYPE CreateIdleTaskEndpoint(struct IWpnIdleTaskEndpoint * ptr64 * ptr64)	= 0;
	virtual HRESULT STDMETHODCALLTYPE CreateTestEndpoint(struct IWpnTestEndpoint * ptr64 * ptr64)	= 0;*/
};

//const wchar_t *g_AppId=L"microsoft.windowscommunicationsapps_8wekyb3d8bbwe!microsoft.windowslive.mail";
//const wchar_t *g_AppId=L"Microsoft.BingNews_8wekyb3d8bbwe!AppexNews";
//const wchar_t *g_AppId=L"Microsoft.WindowsStore_8wekyb3d8bbwe!App";
//const wchar_t *g_AppId=L"Microsoft.BingWeather_8wekyb3d8bbwe!App";
const wchar_t *g_AppId=L"microsoft.windowscommunicationsapps_8wekyb3d8bbwe!microsoft.windowslive.calendar";

static DWORD g_winVer = GetVersionEx(GetModuleHandle(L"user32.dll"));

bool WasOsUpgrade()
{
	CRegKey regKey;
	if (regKey.Open(HKEY_LOCAL_MACHINE, L"Software\\OpenShell\\OpenShell", KEY_READ | KEY_WOW64_64KEY) == ERROR_SUCCESS)
	{
		DWORD ver;
		if (regKey.QueryDWORDValue(L"WinVersion", ver) == ERROR_SUCCESS)
		{
			if (ver < g_winVer)
				return true;
		}
	}

	return false;
}

// starts new instance of StartMenu.exe with "-upgrade" command line parameter
// UAC dialog is shown to ensure it will run with administrator privileges
void RunOsUpgradeTaskAsAdmin()
{
#ifdef _WIN64
	wchar_t path[_MAX_PATH] = L"%windir%\\System32\\StartMenuHelper64.dll";
#else
	wchar_t path[_MAX_PATH] = L"%windir%\\System32\\StartMenuHelper32.dll";
#endif
	DoEnvironmentSubst(path, _countof(path));
	if (GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES)
	{
		GetModuleFileName(NULL, path, _countof(path));
		CoInitialize(NULL);
		ShellExecute(NULL, L"runas", path, L"-upgrade", NULL, SW_SHOWNORMAL);
		CoUninitialize();
	}
}

DWORD PerformOsUpgradeTask(bool silent)
{
	CRegKey regKey;
	DWORD error = regKey.Open(HKEY_LOCAL_MACHINE, L"Software\\OpenShell\\OpenShell", KEY_WRITE | KEY_WOW64_64KEY);
	const wchar_t *nl = error == ERROR_SUCCESS ? L"\r\n\r\n" : L"\r\n";
	if (error == ERROR_SUCCESS)
	{
		regKey.SetDWORDValue(L"WinVersion", g_winVer);

		// run regsvr32 StartMenuHelper
#ifdef _WIN64
		wchar_t cmdLine[_MAX_PATH] = L"regsvr32 /s \"%windir%\\System32\\StartMenuHelper64.dll\"";
#else
		wchar_t cmdLine[_MAX_PATH] = L"regsvr32 /s \"%windir%\\System32\\StartMenuHelper32.dll\"";
#endif
		DoEnvironmentSubst(cmdLine, _countof(cmdLine));

		wchar_t exe[_MAX_PATH] = L"%windir%\\System32\\regsvr32.exe";
		DoEnvironmentSubst(exe, _countof(exe));

		STARTUPINFO startupInfo = { sizeof(startupInfo) };
		PROCESS_INFORMATION processInfo;
		memset(&processInfo, 0, sizeof(processInfo));
		if (CreateProcess(exe, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &startupInfo, &processInfo))
		{
			CloseHandle(processInfo.hThread);
			WaitForSingleObject(processInfo.hProcess, INFINITE);
			GetExitCodeProcess(processInfo.hProcess, &error);
			CloseHandle(processInfo.hProcess);
		}
		else
		{
			error = GetLastError();
		}
	}

	if (!silent)
	{
		if (error)
		{
			wchar_t msg[1024];
			int len = Sprintf(msg, _countof(msg), L"%s%s", DllLoadStringEx(IDS_UPGRADE_ERROR), nl);
			FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, 0, msg + len, _countof(msg) - len, NULL);
			MessageBox(NULL, msg, DllLoadStringEx(IDS_APP_TITLE), MB_OK | MB_ICONERROR);
		}
		else
		{
			MessageBox(NULL, DllLoadStringEx(IDS_UPGRADE_SUCCESS), DllLoadStringEx(IDS_APP_TITLE), MB_OK | MB_ICONINFORMATION);
		}
	}

	return error;
}

int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpstrCmdLine, int nCmdShow )
{
/*	CoInitialize(NULL);
	{
		CComPtr<IWpnPlatform> pWpnPlatform;
		HRESULT hr=pWpnPlatform.CoCreateInstance(CLSID_WpnPlatform);
		CComPtr<IWpnPresentationEndpoint> pUnknown;
		hr=pWpnPlatform->CreatePresentationEndpoint(&pUnknown);
		CWpnTileSink sink;
		DWORD cookie;
		CComQIPtr<IConnectionPoint> pConnectionPoint=pUnknown;
		hr=pConnectionPoint->Advise(&sink,&cookie);
		CComPtr<IWpnPresentationEndpoint> pWpnPresentationEndpoint=pUnknown;
		DWORD session;
		hr=pWpnPresentationEndpoint->TileCreateSession(&session);
		struct
		{
			const wchar_t *appid;
			void *ptr1;
			void *ptr2;
			__int64 idx;
		} request={g_AppId,0,0,1};
		hr=pWpnPresentationEndpoint->TileRequestNotification(&request,1);

		MSG msg;
	while(true)
	{
		struct
		{
			const wchar_t *appid;
			__int64 idx;
		} updateData={g_AppId,1};
		hr=pWpnPresentationEndpoint->TileUpdateSession(session,&updateData,1);

		while (PeekMessage(&msg,0,0,0,PM_REMOVE))
//		while (GetMessage(&msg,0,0,0))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		Sleep(100);
	}
		int q=0;
	}
	CoUninitialize();
	return 0;*/
/*	DWORD count;
	pStore->GetCount(&count);
	for (DWORD i=0;i<count;i++)
	{
		PROPERTYKEY key;
		if (SUCCEEDED(pStore->GetAt(i,&key)))
		{
			CComString pPropName;
			PSGetNameFromPropertyKey(key,&pPropName);
			PROPVARIANT val;
			PropVariantInit(&val);
			pStore->GetValue(key,&val);
			PropVariantClear(&val);
		}
	}*/

	// one-time import from Classic Shell
	ImportLegacyData();

	DllLogToFile(STARTUP_LOG,L"StartMenu: start '%s'",lpstrCmdLine);

	if (wcsstr(lpstrCmdLine,L"-startup") || (wcsstr(lpstrCmdLine,L"-autorun") && HIWORD(g_winVer)<WIN_VER_WIN8))
	{
		WaitDllInitThread();
		if (!DllGetSettingBool(L"AutoStart"))
		{
			DllLogToFile(STARTUP_LOG,L"StartMenu: quit - no AutoStart");
			return 0;
		}
#ifdef BUILD_BETA
		if (GetKeyState(VK_SHIFT)<0)
			return 0;
#endif
		int sleep=DllGetSettingInt(L"AutoStartDelay");
		if (sleep>0)
			Sleep(sleep);
	}
	else if (wcsstr(lpstrCmdLine,L"-autorun")) // on Win8+
	{
		WaitDllInitThread();
		if (WasOsUpgrade())
		{
			// this is an upgrade
			MessageBox(NULL, DllLoadStringEx(IDS_UPGRADE_WIN), DllLoadStringEx(IDS_APP_TITLE), MB_OK);
			RunOsUpgradeTaskAsAdmin();
		}
		if (!DllGetSettingBool(L"AutoStart"))
		{
			DllLogToFile(STARTUP_LOG,L"StartMenu: quit - no AutoStart");
			return 0;
		}
		int sleep=DllGetSettingInt(L"AutoStartDelay");
		if (sleep>0)
			Sleep(sleep);
	}

	if (wcsstr(lpstrCmdLine,L"-upgrade"))
	{
		WaitDllInitThread();

		if (WasOsUpgrade())
		{
			const bool silent = wcsstr(lpstrCmdLine, L"-silent") != nullptr;
			return PerformOsUpgradeTask(silent);
		}

		return 0;
	}

	const wchar_t *pCmd=wcsstr(lpstrCmdLine,L"-cmd ");
	if (pCmd)
	{
		WaitDllInitThread();
		CoInitialize(NULL);
		wchar_t command[100];
		GetToken(pCmd+5,command,_countof(command),L" ");
		if (DllExecuteNamedCommand(command))
		{
			MSG msg;
			while (PeekMessage(&msg,0,0,0,PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
		CoUninitialize();
		return 0;
	}

#ifndef _WIN64
	const wchar_t *pSaveAdmx=wcsstr(lpstrCmdLine,L"-saveadmx ");
	if (pSaveAdmx)
	{
		WaitDllInitThread();
		wchar_t language[100];
		GetToken(pSaveAdmx+10,language,_countof(language),L" ");
		HMODULE dll=NULL;
		if (language[0])
		{
			wchar_t path[_MAX_PATH];
			GetCurrentDirectory(_countof(path),path);
			PathAppend(path,language);
			PathAddExtension(path,L".dll");
			dll=LoadLibraryEx(path,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
		}
		DllLoadTranslationResources(dll,NULL);
		if (!DllSaveAdmx(COMPONENT_MENU,"OpenShellStartMenu.admx","OpenShellStartMenu.adml","MenuADMX.txt"))
			return 1;
		if (!DllSaveAdmx(COMPONENT_SHARED,"OpenShell.admx","OpenShell.adml","OpenShellADMX.txt"))
			return 1;
		return 0;
	}
#endif

	wchar_t path[_MAX_PATH];
	GetModuleFileName(NULL,path,_countof(path));
	*PathFindFileName(path)=0;
	SetCurrentDirectory(path);
	const wchar_t *pRunAs=wcsstr(lpstrCmdLine,L"-runas");
	if (pRunAs)
	{
		pRunAs+=7;
		CoInitialize(NULL);
		wchar_t exe[_MAX_PATH];
		const wchar_t *args=NULL;
		CComString strExe, strArgs;
		if (SUCCEEDED(SHEvaluateSystemCommandTemplate(pRunAs,&strExe,NULL,&strArgs)))
		{
			args=strArgs;
			Strcpy(exe,_countof(exe),strExe);
		}
		else
		{
			args=SeparateArguments(pRunAs,exe);
		}
		SHELLEXECUTEINFO execute={sizeof(execute),SEE_MASK_DOENVSUBST|SEE_MASK_FLAG_LOG_USAGE};
		execute.lpFile=exe;
		execute.lpParameters=args;
		execute.nShow=SW_SHOWNORMAL;
		ShellExecuteEx(&execute);
		CoUninitialize();
		return 0;
	}

	WaitDllInitThread();
	int open=CMD_NONE;
	if (wcsstr(lpstrCmdLine,L"-togglenew")!=NULL) open=CMD_TOGGLE_NEW;
	else if (wcsstr(lpstrCmdLine,L"-toggle")!=NULL) open=MSG_TOGGLE;
	else if (wcsstr(lpstrCmdLine,L"-open")!=NULL) open=MSG_OPEN;
	else if (wcsstr(lpstrCmdLine,L"-settings")!=NULL) open=MSG_SETTINGS;
	else if (wcsstr(lpstrCmdLine,L"-reloadsettings")!=NULL) open=MSG_RELOADSETTINGS;
	else if (wcsstr(lpstrCmdLine,L"-exit")!=NULL) open=MSG_EXIT;

	{
		const wchar_t *pXml=wcsstr(lpstrCmdLine,L"-xml ");
		if (pXml)
		{
			wchar_t xml[_MAX_PATH];
			GetToken(pXml+5,xml,_countof(xml),L" ");
			CoInitialize(NULL);
			bool res=DllImportSettingsXml(xml);
			CoUninitialize();
			if (res)
				open=MSG_RELOADSETTINGS;
			else
				return 1;
		}
	}

	{
		const wchar_t *pBackup=wcsstr(lpstrCmdLine,L"-backup ");
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

	const wchar_t *pNoHook=wcsstr(lpstrCmdLine,L"-nohook");
	bool bHookExplorer=!pNoHook;
	if (pNoHook)
	{
		pNoHook+=7;
		if (*pNoHook=='1') MiniDumpType=MiniDumpNormal;
		if (*pNoHook=='2') MiniDumpType=MiniDumpWithDataSegs;
		if (*pNoHook=='3') MiniDumpType=MiniDumpWithFullMemory;
	}

	if (!bHookExplorer)
		SetUnhandledExceptionFilter(TopLevelFilter);

#ifndef BUILD_SETUP
	if (wcsstr(lpstrCmdLine,L"-testsettings")!=NULL || GetKeyState(VK_SHIFT)<0)
	{
		CoInitialize(NULL);
		InitManagers(true);
		EditSettings(true,0);
		CloseManagers(true);
		CoUninitialize();
#ifdef TRACK_GDI_RESOURCES
		DllDumpResourceLeaks();
#endif
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
	bool bDefaultDesktop=(_wcsicmp(deskName,L"Default")==0);

	wchar_t mutexName[1024];
	Sprintf(mutexName,_countof(mutexName),L"OpenShellMenu.Mutex.%s.%s",userName,deskName);
	free(deskName);

	if (open==CMD_NONE)
		DllLogToFile(STARTUP_LOG,L"StartMenu: mutex %s",mutexName);
	HWND progWin=FindWindowEx(NULL,NULL,L"Progman",NULL);
	DWORD process;
	if (progWin)
		GetWindowThreadProcessId(progWin,&process);

	HANDLE hMutex=NULL;
	if (bHookExplorer)
	{
		hMutex=CreateMutex(NULL,TRUE,mutexName);
		if (GetLastError()==ERROR_ALREADY_EXISTS || GetLastError()==ERROR_ACCESS_DENIED)
		{
			if (open==MSG_TOGGLE || open==CMD_TOGGLE_NEW)
			{
				if (progWin)
				{
					AllowSetForegroundWindow(process);
					HWND taskBar=FindTaskBar(process);
					if (taskBar)
						PostMessage(taskBar,RegisterWindowMessage(L"OpenShellMenu.StartMenuMsg"),(open==CMD_TOGGLE_NEW)?MSG_TOGGLENEW:MSG_TOGGLE,0);
					else
						PostMessage(progWin,WM_SYSCOMMAND,SC_TASKLIST,(open==CMD_TOGGLE_NEW)?'WSMK':'CSM');
				}
			}
			else if (open!=CMD_NONE)
			{
				AllowSetForegroundWindow(process);
				HWND hwnd=FindWindow(L"OpenShellMenu.CStartHookWindow",L"StartHookWindow");
				if (hwnd) PostMessage(hwnd,WM_OPEN,open,0);
			}
			if (open==MSG_EXIT && hMutex && WaitForSingleObject(hMutex,2000)==WAIT_OBJECT_0)
				ReleaseMutex(hMutex);
			if (open==CMD_NONE)
				DllLogToFile(STARTUP_LOG,L"StartMenu: exit (mutex exists)");
			return 0;
		}
	}
	if (open!=CMD_NONE && open!=MSG_OPEN && open!=MSG_SETTINGS)
	{
		if (hMutex) ReleaseMutex(hMutex);
		return 0;
	}

	OleInitialize(NULL);
	if (!bHookExplorer)
	{
		DllUpdateSettings();
		InitManagers(true);
	}
	CStartHookWindow window;
	window.Create(NULL,NULL,L"StartHookWindow",WS_POPUP);

	g_TaskbarCreatedMsg=RegisterWindowMessage(L"TaskbarCreated");
	typedef BOOL (WINAPI *tChangeWindowMessageFilterEx)(HWND hWnd, UINT message, DWORD action, PCHANGEFILTERSTRUCT pChangeFilterStruct );
	HMODULE hUser32=GetModuleHandle(L"user32.dll");
	tChangeWindowMessageFilterEx ChangeWindowMessageFilterEx=(tChangeWindowMessageFilterEx)GetProcAddress(hUser32,"ChangeWindowMessageFilterEx");
	if (ChangeWindowMessageFilterEx)
	{
		ChangeWindowMessageFilterEx(window,g_TaskbarCreatedMsg,MSGFLT_ADD,NULL);
		ChangeWindowMessageFilterEx(window,WM_CLEAR,MSGFLT_ADD,NULL);
		ChangeWindowMessageFilterEx(window,WM_OPEN,MSGFLT_ADD,NULL);
		ChangeWindowMessageFilterEx(window,WM_CLOSE,MSGFLT_ADD,NULL);
	}
	else
	{
		ChangeWindowMessageFilter(g_TaskbarCreatedMsg,MSGFLT_ADD);
		ChangeWindowMessageFilter(WM_CLEAR,MSGFLT_ADD);
		ChangeWindowMessageFilter(WM_OPEN,MSGFLT_ADD);
		ChangeWindowMessageFilter(WM_CLOSE,MSGFLT_ADD);
	}

	MSG msg;
	HWND menu=NULL;
	if (HookStartMenu(bHookExplorer?(bDefaultDesktop?HOOK_STARTUP:HOOK_STARTUP2):HOOK_NONE,menu))
	{
		if (bHookExplorer && open>=0)
			window.PostMessage(WM_OPEN,open,MSG_OPEN);
		DllLogToFile(STARTUP_LOG,L"StartMenu: start message loop");
		while ((bHookExplorer || IsWindow(menu)) && GetMessage(&msg,0,0,0))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	DllLogToFile(STARTUP_LOG,L"StartMenu: end message loop");
	window.DestroyWindow();
	if (!bHookExplorer)
	{
		CloseManagers(true);
	}
	OleUninitialize();

	if (hMutex) ReleaseMutex(hMutex);
#ifdef TRACK_GDI_RESOURCES
	DllDumpResourceLeaks();
#endif
	return 0;
}
