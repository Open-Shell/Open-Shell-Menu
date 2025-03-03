// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include "resource.h"
#include "StartMenuDLL.h"
#include "StartButton.h"
#include "MenuContainer.h"
#include "SettingsParser.h"
#include "Translations.h"
#include "Settings.h"
#include "SettingsUI.h"
#include "ResourceHelper.h"
#include "LogManager.h"
#include "TouchHelper.h"
#include "IatHookHelper.h"
#include "dllmain.h"
#include <uxtheme.h>
#include <dwmapi.h>
#include <htmlhelp.h>
#include <dbghelp.h>
#include <set>
#include <Thumbcache.h>

#define HOOK_DROPTARGET // define this to replace the IDropTarget of the start button
#define START_TOUCH // touch support for the start button

#ifdef BUILD_SETUP
#ifndef HOOK_DROPTARGET
#define HOOK_DROPTARGET // make sure it is defined in Setup
#endif
#endif

const int MAIN_TASK_BAR=0;
typedef std::map<size_t,TaskbarInfo> id_taskbar_map;
id_taskbar_map g_TaskbarInfos;
static int g_LastTaskbar=MAIN_TASK_BAR;
static int g_NextTaskbar=0;
HWND g_TaskBar, g_OwnerWindow;
HWND g_TopWin7Menu, g_AllPrograms, g_ProgramsButton, g_UserPic; // from the Windows menu
HWND g_ProgWin;
HMONITOR g_WSMHMonitor;
static HWND g_WinStartButton;
static UINT g_StartMenuMsg;
static HWND g_Tooltip;
static TOOLINFO g_StartButtonTool;
static bool g_bHotkeyShift;
static int g_HotkeyCSM, g_HotkeyWSM, g_HotkeyShiftID, g_HotkeyCSMID, g_HotkeyWSMID;
static HHOOK g_ProgHook, g_StartHook, g_StartMouseHook, g_AppManagerHook, g_NewWindowHook, g_StartMenuHook;
static bool g_bAllProgramsTimer;
static bool g_bInMenu;
static DWORD g_LastClickTime;
static DWORD g_LastHoverPos;
static bool g_bCrashDump;
static int g_SkipMetroCount;
static DWORD g_StartButtonOldSizes[12];
const int FIRST_BUTTON_BITMAP=6801;
static HWND g_TopDesktopBar;
static DWORD g_AppManagerThread;
static std::set<HWND> g_EdgeWindows;
static bool g_bTrimHooks;
static DWORD g_TaskbarThreadId;
static HWND g_CurrentTaskList, g_CurrentTaskChevron, g_CurrentRebar, g_CurrentTaskbarPart, g_CurrentTaskbarButton, g_CurrentDesktopButton;
static HBITMAP g_TaskbarTexture;
static SIZE g_TaskbarTextureSize;
static TTaskbarTile g_TaskbarTileH, g_TaskbarTileV;
static RECT g_TaskbarMargins;
int g_CurrentCSMTaskbar=-1, g_CurrentWSMTaskbar=-1;
// ExplorerPatcher taskbar
static bool g_epTaskbar = false;

static void FindWindowsMenu( void );
static void RecreateStartButton( size_t taskbarId );
static bool WindowsMenuOpened( void );

static tSetWindowCompositionAttribute SetWindowCompositionAttribute;

enum
{
	OPEN_NOTHING,
	OPEN_CLASSIC,
	OPEN_WINDOWS,
	OPEN_CUSTOM,
	OPEN_BOTH,
	OPEN_DESKTOP,
	OPEN_CORTANA,
};

// MiniDumpNormal - minimal information
// MiniDumpWithDataSegs - include global variables
// MiniDumpWithFullMemory - include heap
MINIDUMP_TYPE MiniDumpType=MiniDumpNormal;

static DWORD WINAPI SaveCrashDump( void *pExceptionInfo )
{
	HMODULE dbghelp=NULL;
	{
		wchar_t path[_MAX_PATH]=L"%LOCALAPPDATA%";
		DoEnvironmentSubst(path,_countof(path));

		dbghelp=LoadLibrary(L"dbghelp.dll");

		LPCTSTR szResult = NULL;

		typedef BOOL (WINAPI *MINIDUMPWRITEDUMP)(HANDLE hProcess, DWORD dwPid, HANDLE hFile, MINIDUMP_TYPE DumpType,
			CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
			CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
			CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam
			);
		MINIDUMPWRITEDUMP dump=NULL;
		if (dbghelp)
			dump=(MINIDUMPWRITEDUMP)GetProcAddress(dbghelp,"MiniDumpWriteDump");
		if (dump)
		{
			HANDLE file;
			for (int i=1;;i++)
			{
				wchar_t fname[_MAX_PATH];
				Sprintf(fname,_countof(fname),L"%s\\CSM_Crash%d.dmp",path,i);
				file=CreateFile(fname,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
				if (file!=INVALID_HANDLE_VALUE || GetLastError()!=ERROR_FILE_EXISTS) break;
			}
			if (file!=INVALID_HANDLE_VALUE)
			{
				_MINIDUMP_EXCEPTION_INFORMATION ExInfo;
				ExInfo.ThreadId = GetCurrentThreadId();
				ExInfo.ExceptionPointers = (_EXCEPTION_POINTERS*)pExceptionInfo;
				ExInfo.ClientPointers = NULL;

				dump(GetCurrentProcess(),GetCurrentProcessId(),file,MiniDumpType,&ExInfo,NULL,NULL);
				CloseHandle(file);
			}
		}
	}
	if (dbghelp) FreeLibrary(dbghelp);
	TerminateProcess(GetCurrentProcess(),10);
	return 0;
}

LONG _stdcall TopLevelFilter( _EXCEPTION_POINTERS *pExceptionInfo )
{
	if (pExceptionInfo->ExceptionRecord->ExceptionCode==EXCEPTION_STACK_OVERFLOW)
	{
		// start a new thread to get a fresh stack (hoping there is enough stack left for CreateThread)
		HANDLE thread=CreateThread(NULL,0,SaveCrashDump,pExceptionInfo,0,NULL);
		WaitForSingleObject(thread,INFINITE);
		CloseHandle(thread);
	}
	else
		SaveCrashDump(pExceptionInfo);
	return EXCEPTION_CONTINUE_SEARCH;
}

void InvalidParameterHandler( const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved )
{
	*(int*)0=0; // force a crash to generate a dump
}

///////////////////////////////////////////////////////////////////////////////

interface ISwitchModeManager: public IUnknown
{
	STDMETHOD(method3)();
	STDMETHOD(method4)();
	STDMETHOD(method5)();
	STDMETHOD(method6)();
	STDMETHOD(method7)();
	STDMETHOD(ShowLauncherTipContextMenu)( POINT *pt );
};

interface IImmersiveLauncherThumbnailProvider: public IUnknown
{
	STDMETHOD(GetBitmap)( SIZE size, int scale, int, ISharedBitmap **ppBitmap );
};

interface IImmersiveMonitor: public IUnknown
{
	STDMETHOD(method3)();
	STDMETHOD(method4)();
	STDMETHOD(GetHandle)(HMONITOR*);
};

interface IImmersiveLauncher80: public IUnknown
{
	STDMETHOD(ShowStartView)( int method );
	STDMETHOD(method4)();
	STDMETHOD(method5)();
	STDMETHOD(method6)();
	STDMETHOD(method7)();
	STDMETHOD(Dismiss)( int method );
};

interface IImmersiveLauncher81: public IUnknown
{
	STDMETHOD(ShowStartView)( int method, int flags );
	STDMETHOD(method4)();
	STDMETHOD(method5)();
	STDMETHOD(method6)();
	STDMETHOD(method7)();
	STDMETHOD(method8)();
	STDMETHOD(method9)();
	STDMETHOD(IsVisible)(BOOL *);
	STDMETHOD(method11)();
	STDMETHOD(method12)();
	STDMETHOD(method13)();
	STDMETHOD(method14)();
	STDMETHOD(method15)();
	STDMETHOD(method16)();
	STDMETHOD(method17)();
	STDMETHOD(ConnectToMonitor)(IUnknown *);
	STDMETHOD(GetMonitor)(IImmersiveMonitor **);
};

interface IImmersiveLauncher10RS: public IUnknown
{
	STDMETHOD(ShowStartView)( int method, int flags );
	STDMETHOD(method4)();
	STDMETHOD(method5)();
	STDMETHOD(method6)();
	STDMETHOD(IsVisible)(BOOL *);
	STDMETHOD(method8)();
	STDMETHOD(method9)();
	STDMETHOD(ConnectToMonitor)(IUnknown *);
	STDMETHOD(GetMonitor)(IImmersiveMonitor **);
};

static const GUID SID_SwitchModeManager={0x085920a1,0x28d3,0x44c1,{0x89,0x7d,0x3b,0xe6,0xd0,0x4b,0x2e,0x07}};
static const GUID IID_ISwitchModeManager={0x976c17be,0xe2d5,0x4f36,{0x93,0x4a,0x7e,0x82,0xf7,0x10,0xea,0xe1}};

static const GUID SID_ImmersiveLauncherThumbnailProvider={0x66ce8036,0x400c,0x42f7,{0x99,0x34,0x02,0xf8,0x84,0xfe,0x27,0x4f}};
static const GUID IID_IImmersiveLauncherThumbnailProvider={0x35c01454,0x53f4,0x4818,{0xba,0x8c,0x7a,0xba,0xdc,0x0f,0xfe,0xe6}};

static const GUID SID_ImmersiveLauncher={0x6f86e01c,0xc649,0x4d61,{0xbe,0x23,0xf1,0x32,0x2d,0xde,0xca,0x9d}};
static const GUID IID_IImmersiveLauncher80={0xfd8b3e33,0xa1f7,0x4e9a,{0x80,0xad,0x80,0x02,0xc7,0x46,0xbe,0x37}};
static const GUID IID_IImmersiveLauncher81={0x93f91f5a,0xa4ca,0x4205,{0x9b,0xeb,0xce,0x4d,0x17,0xc7,0x08,0xf9}};
static const GUID IID_IImmersiveLauncher10RS={0xd8d60399,0xa0f1,0xf987,{0x55,0x51,0x32,0x1f,0xd1,0xb4,0x98,0x64}}; // 14257

static const GUID IID_IImmersiveLauncherProvider={0x6d5140c1,0x7436,0x11ce,{0x80,0x34,0x00,0xaa,0x00,0x60,0x09,0xfa}};

static const CLSID CLSID_ImmersiveShell={0xc2f03a33, 0x21f5, 0x47fa, {0xb4, 0xbb, 0x15, 0x63, 0x62, 0xa2, 0xf2, 0x39}};

static const GUID SID_LauncherTipContextMenu={0xb8c1db5f, 0xcbb3, 0x48bc, {0xaf, 0xd9, 0xce, 0x6b, 0x88, 0x0c, 0x79, 0xed}};

interface ILauncherTipContextMenu: public IUnknown
{
	STDMETHOD(ShowLauncherTipContextMenu)( POINT *pt );
};

interface IImmersiveMonitorService: public IUnknown
{
	STDMETHOD(method3)();
	STDMETHOD(method4)();
	STDMETHOD(method5)();
	STDMETHOD(GetFromHandle)(HMONITOR, IUnknown **);
	STDMETHOD(method7)();
	STDMETHOD(method8)();
	STDMETHOD(method9)();
	STDMETHOD(method10)();
	STDMETHOD(method11)();
	STDMETHOD(method12)();
	STDMETHOD(method13)();
	STDMETHOD(SetImmersiveMonitor)(IUnknown *);
};

static const GUID SID_IImmersiveMonitorService={0x47094e3a,0x0cf2,0x430f,{0x80,0x6f,0xcf,0x9e,0x4f,0x0f,0x12,0xdd}};
static const GUID IID_IImmersiveMonitorService={0x4d4c1e64,0xe410,0x4faa,{0xba,0xfa,0x59,0xca,0x06,0x9b,0xfe,0xc2}};


struct StartScreenThumbInfo
{
	SIZE size;
	HBITMAP bitmap;
	HANDLE event;
};

static bool CreateImmersiveShell( CComPtr<IUnknown> &ptr )
{
	if (GetWinVersion()<WIN_VER_WIN8)
		return false;
	ptr.CoCreateInstance(CLSID_ImmersiveShell);
	return ptr.p!=NULL;
}

///////////////////////////////////////////////////////////////////////////////

// COwnerWindow - a special window used as owner for some UI elements, like the ones created by IContextMenu::InvokeCommand.
// A menu window cannot be used because it may disappear immediately after InvokeCommand. Some UI elements, like the UAC-related
// stuff can be created long after InvokeCommand returns and the menu may be deleted by then.
class COwnerWindow: public CWindowImpl<COwnerWindow>
{
public:
	DECLARE_WND_CLASS_EX(L"OpenShell.COwnerWindow",0,COLOR_MENU)

	// message handlers
	BEGIN_MSG_MAP( COwnerWindow )
		MESSAGE_HANDLER( WM_ACTIVATE, OnActivate )
		MESSAGE_HANDLER( WM_SYSCOLORCHANGE, OnColorChange )
		MESSAGE_HANDLER( WM_SETTINGCHANGE, OnSettingChange )
		MESSAGE_HANDLER( WM_DISPLAYCHANGE, OnDisplayChange )
	END_MSG_MAP()

protected:
	LRESULT OnActivate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
	{
		if (LOWORD(wParam)!=WA_INACTIVE)
			return 0;

		if (CMenuContainer::s_bPreventClosing)
			return 0;

		// check if another menu window is being activated
		// if not, close all menus
		for (std::vector<CMenuContainer*>::const_iterator it=CMenuContainer::s_Menus.begin();it!=CMenuContainer::s_Menus.end();++it)
			if ((*it)->m_hWnd==(HWND)lParam)
				return 0;

		for (std::vector<CMenuContainer*>::reverse_iterator it=CMenuContainer::s_Menus.rbegin();it!=CMenuContainer::s_Menus.rend();++it)
			if (!(*it)->m_bDestroyed)
				(*it)->PostMessage(WM_CLOSE);

		return 0;
	}


	LRESULT OnColorChange( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
	{
		CMenuContainer::s_Skin.Hash=0;
		return 0;
	}


	LRESULT OnSettingChange( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
	{
		if (wParam==SPI_SETWORKAREA)
		{
			if (!CMenuContainer::s_Menus.empty())
				CMenuContainer::s_Menus[0]->NotifyDisplayChange();
		}
		return 0;
	}


	LRESULT OnDisplayChange( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
	{
		if (!CMenuContainer::s_Menus.empty())
			CMenuContainer::s_Menus[0]->NotifyDisplayChange();
		return 0;
	}
};

static COwnerWindow g_Owner;

///////////////////////////////////////////////////////////////////////////////

bool TaskbarInfo::HasPart( HWND part ) const
{
	for (std::vector<HWND>::const_iterator it=taskbarParts.begin();it!=taskbarParts.end();++it)
		if (*it==part)
			return true;
	return false;
}

static const TaskbarInfo *GetDefaultTaskbarInfo( void )
{
	if (GetSettingBool(L"AllTaskbars"))
	{
		HMONITOR monitor=MonitorFromPoint(CPoint(GetMessagePos()),MONITOR_DEFAULTTONEAREST);
		for (std::map<size_t,TaskbarInfo>::const_iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
		{
			MONITORINFO info;
			HMONITOR monitor2=NULL;
			if (GetTaskbarPosition(it->second.taskBar,&info,&monitor2,NULL)!=0xFFFFFFFF && monitor2==monitor)
				return &it->second;
		}
		id_taskbar_map::const_iterator it=g_TaskbarInfos.find(g_LastTaskbar);
		if (it!=g_TaskbarInfos.end())
			return &it->second;
	}
	return &g_TaskbarInfos.begin()->second;
}

TaskbarInfo *GetTaskbarInfo( size_t taskbarId )
{
	std::map<size_t,TaskbarInfo>::iterator it=g_TaskbarInfos.find(taskbarId);
	return (it==g_TaskbarInfos.end())?NULL:&it->second;
}

static TaskbarInfo *FindTaskBarInfoButton( HWND button )
{
	for (id_taskbar_map::iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
		if (it->second.startButton==button || it->second.oldButton==button)
			return &it->second;
	return NULL;
}

static TaskbarInfo *FindTaskBarInfoBar( HWND bar )
{
	for (id_taskbar_map::iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
		if (it->second.taskBar==bar)
			return &it->second;
	return NULL;
}

static LRESULT CALLBACK HookProgManThread( int code, WPARAM wParam, LPARAM lParam );
static LRESULT CALLBACK HookDesktopThread( int code, WPARAM wParam, LPARAM lParam );
static LRESULT CALLBACK HookDesktopThreadMouse(int code, WPARAM wParam, LPARAM lParam);

static BOOL CALLBACK FindTooltipEnum( HWND hwnd, LPARAM lParam )
{
	// look for tooltip control in the current thread that has a tool for g_TaskBar+g_StartButton
	wchar_t name[256];
	GetClassName(hwnd,name,_countof(name));
	if (_wcsicmp(name,TOOLTIPS_CLASS)!=0) return TRUE;
	TOOLINFO info={sizeof(info),0,g_TaskBar,(UINT_PTR)g_WinStartButton};
	if (SendMessage(hwnd,TTM_GETTOOLINFO,0,(LPARAM)&info))
	{
		g_Tooltip=hwnd;
		return FALSE;
	}
	return TRUE;
}

static BOOL CALLBACK FindStartButtonEnum( HWND hwnd, LPARAM lParam )
{
	// look for top-level window in the current thread with class "button"
	wchar_t name[256];
	GetClassName(hwnd,name,_countof(name));
	if (_wcsicmp(name,L"button")!=0) return TRUE;
	g_WinStartButton=hwnd;
	return FALSE;
}

static BOOL CALLBACK FindTaskBarEnum( HWND hwnd, LPARAM lParam )
{
	// look for top-level window with class "Shell_TrayWnd" and process ID=lParam
	DWORD process;
	GetWindowThreadProcessId(hwnd,&process);
	if (process!=lParam) return TRUE;
	wchar_t name[256];
	GetClassName(hwnd,name,_countof(name));
	if (_wcsicmp(name,L"Shell_TrayWnd")!=0) return TRUE;
	g_TaskBar=hwnd;
	return FALSE;
}

// Find the taskbar window for the given process
STARTMENUAPI HWND FindTaskBar( DWORD process )
{
	g_WinStartButton=NULL;
	g_TaskBar=NULL;
	g_Tooltip=NULL;
	// find the taskbar
	EnumWindows(FindTaskBarEnum,process);
	if (!g_TaskBar)
		g_TaskBar=FindWindowEx(GetDesktopWindow(),NULL,L"Shell_TrayWnd",NULL);
	if (g_TaskBar)
	{
		// find start button
		if (GetWinVersion()==WIN_VER_WIN7)
			EnumThreadWindows(GetWindowThreadProcessId(g_TaskBar,NULL),FindStartButtonEnum,NULL);
		if (GetWindowThreadProcessId(g_TaskBar,NULL)==GetCurrentThreadId())
		{
			// find tooltip
			if (g_WinStartButton)
			{
				EnumThreadWindows(GetWindowThreadProcessId(g_TaskBar,NULL),FindTooltipEnum,NULL);
				if (g_Tooltip)
				{
					g_StartButtonTool.cbSize=sizeof(g_StartButtonTool);
					g_StartButtonTool.hwnd=g_TaskBar;
					g_StartButtonTool.uId=(UINT_PTR)g_WinStartButton;
					SendMessage(g_Tooltip,TTM_GETTOOLINFO,0,(LPARAM)&g_StartButtonTool);
				}
			}
			g_OwnerWindow=g_Owner.Create(NULL,0,0,WS_POPUP,WS_EX_TOOLWINDOW|WS_EX_TOPMOST);
		}
	}
	return g_TaskBar;
}

#ifdef HOOK_DROPTARGET
class CStartMenuTarget: public IDropTarget
{
public:
	CStartMenuTarget( int taskbarId ) { m_RefCount=1; m_TaskbarId=taskbarId; }
	// IUnknown
	virtual STDMETHODIMP QueryInterface( REFIID riid, void **ppvObject )
	{
		*ppvObject=NULL;
		if (IID_IUnknown==riid || IID_IDropTarget==riid)
		{
			AddRef();
			*ppvObject=(IDropTarget*)this;
			return S_OK;
		}
		return E_NOINTERFACE;
	}

	virtual ULONG STDMETHODCALLTYPE AddRef( void ) 
	{ 
		return InterlockedIncrement(&m_RefCount);
	}

	virtual ULONG STDMETHODCALLTYPE Release( void )
	{
		long nTemp=InterlockedDecrement(&m_RefCount);
		if (!nTemp) delete this;
		return nTemp;
	}

	// IDropTarget
	virtual HRESULT STDMETHODCALLTYPE DragEnter( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect )
	{
		FORMATETC format1={(CLIPFORMAT)RegisterClipboardFormat(CFSTR_SHELLIDLIST),NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
		FORMATETC format2={(CLIPFORMAT)RegisterClipboardFormat(CFSTR_INETURL),NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
		if (pDataObj->QueryGetData(&format1)==S_OK || pDataObj->QueryGetData(&format2)==S_OK)
		{
			PostMessage(g_TaskBar,g_StartMenuMsg,(grfKeyState&MK_SHIFT)?MSG_SHIFTDRAG:MSG_DRAG,m_TaskbarId);
		}
		*pdwEffect=DROPEFFECT_NONE;
		return S_OK;
	}

	virtual HRESULT STDMETHODCALLTYPE DragOver( DWORD grfKeyState, POINTL pt, DWORD *pdwEffect ) { return *pdwEffect=DROPEFFECT_NONE; return S_OK; }
	virtual HRESULT STDMETHODCALLTYPE DragLeave( void ) { return S_OK; }
	virtual HRESULT STDMETHODCALLTYPE Drop( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect ) { return *pdwEffect=DROPEFFECT_NONE; return S_OK; }

private:
	LONG m_RefCount;
	int m_TaskbarId;
};

#endif

static CComPtr<IDropTarget> g_pOriginalTarget;

static void FindTaskBar( void )
{
	if (!g_TaskBar)
	{
		g_StartMenuMsg=RegisterWindowMessage(L"OpenShellMenu.StartMenuMsg");
		FindTaskBar(GetCurrentProcessId());
		if (g_TaskBar)
		{
			g_HotkeyShiftID=GlobalAddAtom(L"OpenShellMenu.HotkeyShift");
			g_HotkeyCSMID=GlobalAddAtom(L"OpenShellMenu.HotkeyCSM");
			g_HotkeyWSMID=GlobalAddAtom(L"OpenShellMenu.HotkeyWSM");
			EnableHotkeys(HOTKEYS_NORMAL);
			srand(GetTickCount());
		}
		if (!g_TaskBar) g_TaskBar=(HWND)1;
	}
}

void EnableStartTooltip( bool bEnable )
{
	if (g_Tooltip)
	{
		SendMessage(g_Tooltip,TTM_POP,0,0);
		if (bEnable)
			SendMessage(g_Tooltip,TTM_UPDATETIPTEXT,0,(LPARAM)&g_StartButtonTool);
		else
		{
			TOOLINFO info=g_StartButtonTool;
			info.lpszText=(LPWSTR)L"";
			SendMessage(g_Tooltip,TTM_UPDATETIPTEXT,0,(LPARAM)&info);
		}
	}
}

// Restore the original drop target
static void UnhookDropTarget( void )
{
	if (g_pOriginalTarget)
	{
		RevokeDragDrop(g_WinStartButton);
		if (g_pOriginalTarget)
			RegisterDragDrop(g_WinStartButton,g_pOriginalTarget);
		g_pOriginalTarget=NULL;
	}
}

// Toggle the start menu. bKeyboard - set to true to show the keyboard cues
STARTMENUAPI HWND ToggleStartMenu( int taskbarId, bool bKeyboard )
{
	if (taskbarId==-1)
	{
		if (g_TaskbarInfos.find(-1)==g_TaskbarInfos.end())
		{
			g_TaskbarInfos[-1].taskBar=g_TaskBar;
		}
	}
	g_LastTaskbar=taskbarId;
	return CMenuContainer::ToggleStartMenu(taskbarId,bKeyboard,false);
}

UINT GetTaskbarPosition( HWND taskBar, MONITORINFO *pInfo, HMONITOR *pMonitor, RECT *pRc )
{
	if (!IsWindow(taskBar))
		return 0xFFFFFFFF;
	if (taskBar==g_TaskBar)
	{
		APPBARDATA appbar={sizeof(appbar),taskBar};
		SHAppBarMessage(ABM_GETTASKBARPOS,&appbar);
		if (pRc)
		{
			if (RECT rc; GetWindowRgnBox(taskBar,&rc)!=ERROR)
			{
				MapWindowPoints(taskBar,NULL,(POINT*)&rc,2);
				appbar.rc=rc;
			}
			*pRc=appbar.rc;
			RECT rc;
			GetWindowRect(taskBar,&rc);
			if (appbar.uEdge==ABE_LEFT || appbar.uEdge==ABE_RIGHT)
			{
				if (pRc->top<rc.top) pRc->top=rc.top;
				if (pRc->bottom>rc.bottom) pRc->bottom=rc.bottom;
			}
			else if (appbar.uEdge==ABE_TOP || appbar.uEdge==ABE_BOTTOM)
			{
				if (pRc->left<rc.left) pRc->left=rc.left;
				if (pRc->right>rc.right) pRc->right=rc.right;
			}
		}
		HMONITOR monitor=MonitorFromRect(&appbar.rc,MONITOR_DEFAULTTONEAREST);
		if (pMonitor) *pMonitor=monitor;
		if (pInfo)
		{
			pInfo->cbSize=sizeof(MONITORINFO);
			GetMonitorInfo(monitor,pInfo);
		}
		return appbar.uEdge;
	}
	RECT rc;
	if (GetWindowRgnBox(taskBar,&rc)!=ERROR)
		MapWindowPoints(taskBar,NULL,(POINT*)&rc,2);
	else
		GetWindowRect(taskBar,&rc);
	MONITORINFO info={sizeof(info)};
	HMONITOR monitor=MonitorFromRect(&rc,MONITOR_DEFAULTTONEAREST);
	GetMonitorInfo(monitor,&info);
	if (pMonitor) *pMonitor=monitor;
	int dx=rc.left+rc.right-info.rcWork.left-info.rcWork.right;
	int dy=rc.top+rc.bottom-info.rcWork.top-info.rcWork.bottom;
	if (pInfo) *pInfo=info;
	bool bAutoHide=false;
	if (pRc)
	{
		GetWindowRect(taskBar,pRc);
		APPBARDATA appbar={sizeof(appbar)};
		bAutoHide=(SHAppBarMessage(ABM_GETSTATE,&appbar)&ABS_AUTOHIDE)!=0;
	}
	if (dx<-abs(dy))
	{
		if (bAutoHide && pRc->left<info.rcWork.left)
			OffsetRect(pRc,info.rcWork.left-pRc->left,0);
		 return ABE_LEFT;
	}
	if (dx>abs(dy))
	{
		if (bAutoHide && pRc->right>info.rcWork.right)
			OffsetRect(pRc,info.rcWork.right-pRc->right,0);
		return ABE_RIGHT;
	}
	if (dy<-abs(dx))
	{
		if (bAutoHide && pRc->top<info.rcWork.top)
			OffsetRect(pRc,0,info.rcWork.top-pRc->top);
		return ABE_TOP;
	}
	if (bAutoHide && pRc->bottom>info.rcWork.bottom)
		OffsetRect(pRc,0,info.rcWork.bottom-pRc->bottom);
	return ABE_BOTTOM;
}

// Returns true if the mouse is on the taskbar portion of the start button
bool PointAroundStartButton( size_t taskbarId, const CPoint &pt )
{
	const TaskbarInfo *taskBar=GetTaskbarInfo(taskbarId);
	if (!taskBar || !(taskBar->startButton || taskBar->oldButton)) return false;
	CRect rc;
	GetWindowRect(taskBar->taskBar,&rc);
	if (!PtInRect(&rc,pt))
		return false;

	bool rtl=GetWindowLongPtr(taskBar->taskBar,GWL_EXSTYLE)&WS_EX_LAYOUTRTL;

	CRect rcStart;
	if (taskBar->startButton)
		GetWindowRect(taskBar->startButton,&rcStart);

	CRect rcOld;
	if (taskBar->oldButton)
	{
		GetWindowRect(taskBar->oldButton,&rcOld);

		if (IsWin11())
		{
			// on Win11 the Start button rectangle is a bit smaller that actual XAML active area
			// lets make it a bit wider to avoid accidental original Start menu triggers
			const int adjust=ScaleForDpi(taskBar->taskBar,1);
			if (rtl)
				rcOld.left-=adjust;
			else
				rcOld.right+=adjust;
		}
	}

	rc.UnionRect(&rcStart,&rcOld);

	// check if the point is inside the start button rect
	UINT uEdge=GetTaskbarPosition(taskBar->taskBar,NULL,NULL,NULL);
	if (uEdge==ABE_LEFT || uEdge==ABE_RIGHT)
		return pt.y>=rc.top && pt.y<rc.bottom;
	else if (rtl)
		return pt.x>rc.left && pt.x<=rc.right;
	else
		return pt.x>=rc.left && pt.x<rc.right;
}

// declare few interfaces so we don't need the Win8 SDK
#ifndef __IAppVisibility_INTERFACE_DEFINED__
typedef enum MONITOR_APP_VISIBILITY
{
	MAV_UNKNOWN	= 0,
	MAV_NO_APP_VISIBLE	= 1,
	MAV_APP_VISIBLE	= 2
} MONITOR_APP_VISIBILITY;

MIDL_INTERFACE("6584CE6B-7D82-49C2-89C9-C6BC02BA8C38")
IAppVisibilityEvents : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE AppVisibilityOnMonitorChanged( HMONITOR hMonitor, MONITOR_APP_VISIBILITY previousMode, MONITOR_APP_VISIBILITY currentMode ) = 0;
	virtual HRESULT STDMETHODCALLTYPE LauncherVisibilityChange( BOOL currentVisibleState ) = 0;
};

MIDL_INTERFACE("2246EA2D-CAEA-4444-A3C4-6DE827E44313")
IAppVisibility : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE GetAppVisibilityOnMonitor( HMONITOR hMonitor, MONITOR_APP_VISIBILITY *pMode ) = 0;
	virtual HRESULT STDMETHODCALLTYPE IsLauncherVisible( BOOL *pfVisible ) = 0;
	virtual HRESULT STDMETHODCALLTYPE Advise( IAppVisibilityEvents *pCallback, DWORD *pdwCookie ) = 0;
	virtual HRESULT STDMETHODCALLTYPE Unadvise( DWORD dwCookie ) = 0;
};

#endif

void ResetHotCorners( void )
{
	for (std::set<HWND>::const_iterator it=g_EdgeWindows.begin();it!=g_EdgeWindows.end();++it)
		ShowWindow(*it,SW_SHOW);
	g_EdgeWindows.clear();
}

void RedrawTaskbars( void )
{
	for (id_taskbar_map::const_iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
		InvalidateRect(it->second.taskBar,NULL,TRUE);
}

static CComPtr<IAppVisibility> g_pAppVisibility;
static DWORD g_AppVisibilityMonitorCookie;

class CMonitorModeEvents: public IAppVisibilityEvents
{
public:
	CMonitorModeEvents( void ) { m_RefCount=1; }
	// IUnknown
	virtual STDMETHODIMP QueryInterface( REFIID riid, void **ppvObject )
	{
		*ppvObject=NULL;
		if (IID_IUnknown==riid || __uuidof(IAppVisibilityEvents)==riid)
		{
			AddRef();
			*ppvObject=(IDropTarget*)this;
			return S_OK;
		}
		return E_NOINTERFACE;
	}

	virtual ULONG STDMETHODCALLTYPE AddRef( void )
	{
		return InterlockedIncrement(&m_RefCount);
	}

	virtual ULONG STDMETHODCALLTYPE Release( void )
	{
		long nTemp=InterlockedDecrement(&m_RefCount);
		if (!nTemp) delete this;
		return nTemp;
	}

	// IAppVisibilityEvents
	virtual HRESULT STDMETHODCALLTYPE AppVisibilityOnMonitorChanged( HMONITOR hMonitor, MONITOR_APP_VISIBILITY previousMode, MONITOR_APP_VISIBILITY currentMode )
	{
		if (GetWinVersion()<WIN_VER_WIN10)
		{
			ResetHotCorners();
			if (IsWin81Update1() && GetSettingBool(L"CustomTaskbar"))
				PostMessage(g_TaskBar,g_StartMenuMsg,MSG_REDRAWTASKBAR,0);
		}
		return S_OK;
	}

	virtual HRESULT STDMETHODCALLTYPE LauncherVisibilityChange( BOOL currentVisibleState )
	{
		CComPtr<IUnknown> pImmersiveShell;
		if (GetWinVersion()>=WIN_VER_WIN10 && CreateImmersiveShell(pImmersiveShell))
		{
			int taskbarId=-1;
			if (currentVisibleState)
			{
				taskbarId=MAIN_TASK_BAR;
				CComPtr<IImmersiveMonitor> pMonitor;
				{
					CComPtr<IImmersiveLauncher81> pLauncher;
					IUnknown_QueryService(pImmersiveShell,SID_ImmersiveLauncher,IID_IImmersiveLauncher81,(void**)&pLauncher);
					if (pLauncher)
						pLauncher->GetMonitor(&pMonitor);
				}
				if (!pMonitor)
				{
					CComPtr<IImmersiveLauncher10RS> pLauncher;
					IUnknown_QueryService(pImmersiveShell,SID_ImmersiveLauncher,IID_IImmersiveLauncher10RS,(void**)&pLauncher);
					if (pLauncher)
						pLauncher->GetMonitor(&pMonitor);
				}
				if (pMonitor)
				{
					HMONITOR monitor;
					if (SUCCEEDED(pMonitor->GetHandle(&monitor)))
					{
						for (id_taskbar_map::const_iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
						{
							if (monitor==MonitorFromWindow(it->second.taskBar,MONITOR_DEFAULTTONULL))
							{
								taskbarId=it->second.taskbarId;
								break;
							}
						}
					}
				}
			}
			if (g_CurrentWSMTaskbar!=taskbarId)
			{
				if (g_CurrentWSMTaskbar!=-1 && g_CurrentWSMTaskbar!=g_CurrentCSMTaskbar)
					PressStartButton(g_CurrentWSMTaskbar,false);
				g_CurrentWSMTaskbar=taskbarId;
				if (g_CurrentWSMTaskbar!=-1)
					PressStartButton(g_CurrentWSMTaskbar,true);
			}
		}
		else
		{
			ResetHotCorners();
			if (IsWin81Update1() && GetSettingBool(L"CustomTaskbar"))
				PostMessage(g_TaskBar,g_StartMenuMsg,MSG_REDRAWTASKBAR,0);
		}
		return S_OK;
	}

private:
	LONG m_RefCount;
};

static const CLSID CLSID_MetroMode={0x7E5FE3D9,0x985F,0x4908,{0x91, 0xF9, 0xEE, 0x19, 0xF9, 0xFD, 0x15, 0x14}};

BOOL CALLBACK AppVisibleProc( HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData )
{
	bool *pData=(bool*)dwData;
	MONITOR_APP_VISIBILITY mode;
	if (SUCCEEDED(g_pAppVisibility->GetAppVisibilityOnMonitor(hMonitor,&mode)) && mode==MAV_APP_VISIBLE)
		*pData=true;
	return !*pData;
}

enum TMetroMode
{
	METRO_NONE,
	METRO_LAUNCHER,
	METRO_APP,
};

static TMetroMode GetMetroMode( HMONITOR hMonitor )
{
	if (!g_pAppVisibility) return METRO_NONE;

	BOOL bLauncher;
	if (SUCCEEDED(g_pAppVisibility->IsLauncherVisible(&bLauncher)) && bLauncher)
	{
		if (!hMonitor) return METRO_LAUNCHER;
		HWND launcher=FindWindow(L"ImmersiveLauncher",NULL);
		if (launcher && hMonitor==MonitorFromWindow(launcher,MONITOR_DEFAULTTONULL))
				return METRO_LAUNCHER;
	}

	if (hMonitor)
	{
		MONITOR_APP_VISIBILITY mode;
		if (SUCCEEDED(g_pAppVisibility->GetAppVisibilityOnMonitor(hMonitor,&mode)) && mode==MAV_APP_VISIBLE)
			return METRO_APP;
	}
	else
	{
		bool bAppVisible=false;
		EnumDisplayMonitors(NULL,NULL,AppVisibleProc,(LPARAM)&bAppVisible);
		if (bAppVisible) return METRO_APP;
	}
	return METRO_NONE;
}

static bool GetWin10TabletMode( void )
{
	CRegKey regKey;
	if (regKey.Open(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\ImmersiveShell",KEY_READ|KEY_WOW64_64KEY)==ERROR_SUCCESS)
	{
		DWORD val;
		return regKey.QueryDWORDValue(L"TabletMode",val)==ERROR_SUCCESS && val;
	}
	return false;
}

static LRESULT CALLBACK HookAppManager( int code, WPARAM wParam, LPARAM lParam )
{
	if (code==HC_ACTION && wParam)
	{
		MSG *msg=(MSG*)lParam;
		if (msg->message==g_StartMenuMsg && (msg->wParam==MSG_WINXMENU || msg->wParam==MSG_METROTHUMBNAIL || msg->wParam==MSG_SHIFTWIN))
		{
			HWND hwnd=FindWindow(L"ModeInputWnd",NULL);
			if (hwnd)
			{
				DWORD process;
				GetWindowThreadProcessId(hwnd,&process);
				if (process==GetCurrentProcessId())
				{
					IObjectWithSite *pObject=(IObjectWithSite*)GetWindowLongPtr(hwnd,0);
					if (pObject)
					{
						CComPtr<IUnknown> pSite;
						pObject->GetSite(IID_IUnknown,(void**)&pSite);
						if (pSite)
						{
							if (msg->wParam==MSG_WINXMENU)
							{
								CPoint pt(msg->lParam);
								CComPtr<ISwitchModeManager> pSwitchModeManager;
								IUnknown_QueryService(pSite,SID_SwitchModeManager,IID_ISwitchModeManager,(void**)&pSwitchModeManager);
								if (pSwitchModeManager)
								{
									pSwitchModeManager->ShowLauncherTipContextMenu(&pt);
									// set the current immersive monitor AFTER the menu returns (this way Search is shown in the correct monitor)
									CComPtr<IImmersiveMonitorService> pMonitorService;
									IUnknown_QueryService(pSite,SID_IImmersiveMonitorService,IID_IImmersiveMonitorService,(void**)&pMonitorService);
									if (pMonitorService)
									{
										HMONITOR monitor=MonitorFromPoint(pt,MONITOR_DEFAULTTONEAREST);
										if (GetWinVersion()==WIN_VER_WIN8)
										{
											CComPtr<IUnknown> pMonitor;
											pMonitorService->GetFromHandle(monitor,&pMonitor);
											if (pMonitor)
												pMonitorService->SetImmersiveMonitor(pMonitor);
										}
										else if (GetWinVersion()>WIN_VER_WIN8)
										{
											// doesn't seem to be doing anything on 8.1, but do it just in case
											CComPtr<IUnknown> pMonitor;
											pMonitorService->GetFromHandle(monitor,&pMonitor);
											if (pMonitor)
											{
												CComPtr<IImmersiveLauncher81> pLauncher;
												IUnknown_QueryService(pSite,SID_ImmersiveLauncher,IID_IImmersiveLauncher81,(void**)&pLauncher);
												if (pLauncher)
													pLauncher->ConnectToMonitor(pMonitor);
											}
										}
									}
								}
							}
							if (msg->wParam==MSG_METROTHUMBNAIL)
							{
								StartScreenThumbInfo &info=*(StartScreenThumbInfo*)msg->lParam;
								CComPtr<IUnknown> pLauncher;
								IUnknown_QueryService(pSite,SID_ImmersiveLauncher,IID_IImmersiveLauncherProvider,(void**)&pLauncher);
								if (pLauncher)
								{
									CComPtr<IImmersiveLauncherThumbnailProvider> pProvider;
									IUnknown_QueryService(pLauncher,SID_ImmersiveLauncherThumbnailProvider,IID_IImmersiveLauncherThumbnailProvider,(void**)&pProvider);
									if (pProvider)
									{
										CComPtr<ISharedBitmap> pBitmap;
										if (SUCCEEDED(pProvider->GetBitmap(info.size,100,1,&pBitmap)) && pBitmap)
										{
											pBitmap->Detach(&info.bitmap);
										}
									}
								}
							}
							if (msg->wParam==MSG_SHIFTWIN)
							{
								if (GetWinVersion()==WIN_VER_WIN8)
								{
									HMONITOR monitor=(HMONITOR)msg->lParam;
									if (monitor)
									{
										CComPtr<IImmersiveMonitorService> pMonitorService;
										IUnknown_QueryService(pSite,SID_IImmersiveMonitorService,IID_IImmersiveMonitorService,(void**)&pMonitorService);
										if (pMonitorService)
										{
											CComPtr<IUnknown> pMonitor;
											pMonitorService->GetFromHandle(monitor,&pMonitor);
											if (pMonitor)
												pMonitorService->SetImmersiveMonitor(pMonitor);
										}
									}
									CComPtr<IImmersiveLauncher80> pLauncher;
									IUnknown_QueryService(pSite,SID_ImmersiveLauncher,IID_IImmersiveLauncher80,(void**)&pLauncher);
									if (pLauncher)
										pLauncher->ShowStartView(5);
								}
							}
						}
					}
				}
			}
			if (msg->wParam==MSG_METROTHUMBNAIL)
			{
				// set the event no matter if successful
				StartScreenThumbInfo &info=*(StartScreenThumbInfo*)msg->lParam;
				SetEvent(info.event);
			}
		}
		int corner;
		if ((msg->message==WM_MOUSEMOVE || msg->message==WM_LBUTTONDOWN) && (corner=GetSettingInt(L"DisableHotCorner"))>0)
		{
			{
				// ignore the mouse messages if there is a menu
				GUITHREADINFO info={sizeof(info)};
				if (GetGUIThreadInfo(GetCurrentThreadId(),&info) && (info.flags&GUI_INMENUMODE))
					return CallNextHookEx(NULL,code,wParam,lParam);
			}
			CPoint pt(GetMessagePos());
			HMONITOR monitor=MonitorFromPoint(pt,MONITOR_DEFAULTTONEAREST);
			if (GetMetroMode(monitor)!=METRO_NONE)
			{
				if (!IsWin81Update1())
					return CallNextHookEx(NULL,code,wParam,lParam);
				typedef BOOL (WINAPI *tGetWindowBand)(HWND,DWORD*);
				static tGetWindowBand GetWindowBand=(tGetWindowBand)GetProcAddress(GetModuleHandle(L"user32.dll"),"GetWindowBand");
				for (id_taskbar_map::const_iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
				{
					DWORD band;
					if (!GetWindowBand || !GetWindowBand(it->second.taskBar,&band) || band==1)
						continue;
					UINT uEdge=GetTaskbarPosition(it->second.taskBar,NULL,NULL,NULL);
					if (uEdge!=ABE_BOTTOM)
						continue;

					// check if the mouse is over the taskbar
					RECT taskRect;
					GetWindowRect(it->second.taskBar,&taskRect);
					if (PtInRect(&taskRect,pt))
					{
						POINT pt2=pt;
						ScreenToClient(it->second.taskBar,&pt2);
						if (pt2.x<32)
						{
							if (msg->message==WM_LBUTTONDOWN)
							{
								// forward the mouse click to the taskbar
								PostMessage(it->second.taskBar,WM_NCLBUTTONDOWN,MK_LBUTTON,MAKELONG(pt.x,pt.y));
								msg->message=WM_NULL;
							}
							wchar_t className[256]={0};
							GetClassName(msg->hwnd,className,_countof(className));
							if (wcscmp(className,L"ImmersiveSwitchList")==0)
							{
								// suppress the opening of the ImmersiveSwitchList
								msg->message=WM_NULL;
								ShowWindow(msg->hwnd,SW_HIDE); // hide the popup
							}
							if (wcscmp(className,L"EdgeUiInputWndClass")==0)
							{
								// suppress the hot corners
								msg->message=WM_NULL;
							}
						}
						break;
					}
				}
				return CallNextHookEx(NULL,code,wParam,lParam);
			}
			if (corner==1)
			{
				for (id_taskbar_map::const_iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
				{
					UINT uEdge=GetTaskbarPosition(it->second.taskBar,NULL,NULL,NULL);
					if (uEdge==ABE_BOTTOM)
					{
						// check if the mouse is over the taskbar
						RECT taskRect;
						GetWindowRect(it->second.taskBar,&taskRect);
						if (PtInRect(&taskRect,pt))
						{
							POINT pt2=pt;
							ScreenToClient(it->second.taskBar,&pt2);
							if (pt2.x<32)
							{
								corner=2;
								if (msg->message==WM_LBUTTONDOWN)
								{
									// forward the mouse click to the taskbar
									PostMessage(it->second.taskBar,WM_NCLBUTTONDOWN,MK_LBUTTON,MAKELONG(pt.x,pt.y));
									msg->message=WM_NULL;
								}
								wchar_t className[256]={0};
								GetClassName(msg->hwnd,className,_countof(className));
								if (wcscmp(className,L"ImmersiveSwitchList")==0)
								{
									// suppress the opening of the ImmersiveSwitchList
									msg->message=WM_NULL;
									ShowWindow(msg->hwnd,SW_HIDE); // hide the popup
								}
							}
							break;
						}
					}
				}
			}
			if (corner==2)
			{
				wchar_t className[256]={0};
				GetClassName(msg->hwnd,className,_countof(className));
				if (wcscmp(className,L"EdgeUiInputWndClass")==0)
				{
					// suppress the hot corners
					msg->message=WM_NULL;
					ShowWindow(msg->hwnd,SW_HIDE);
					g_EdgeWindows.insert(msg->hwnd);
				}
			}
		}
	}
	return CallNextHookEx(NULL,code,wParam,lParam);
}

static LRESULT CALLBACK HookNewWindow( int code, WPARAM wParam, LPARAM lParam )
{
	if (code==HCBT_CREATEWND)
	{
		CBT_CREATEWND *pCreate=(CBT_CREATEWND*)lParam;
		if (pCreate->lpcs->lpszClass>(LPTSTR)0xFFFF && (_wcsicmp(pCreate->lpcs->lpszClass,L"Shell_SecondaryTrayWnd")==0 ||
			_wcsicmp(pCreate->lpcs->lpszClass,L"ToolbarWindow32")==0 || _wcsicmp(pCreate->lpcs->lpszClass,L"TrayClockWClass")==0 || _wcsicmp(pCreate->lpcs->lpszClass,L"ClockButton")==0))
				PostMessage(g_TaskBar,g_StartMenuMsg,MSG_NEWTASKBAR,wParam);
	}
	return CallNextHookEx(NULL,code,wParam,lParam);
}

// Set the hotkeys and controls for the start menu
void EnableHotkeys( THotkeys enable )
{
	if (g_bTrimHooks) return;
	if (!g_TaskBar)
		return;
	if (GetWindowThreadProcessId(g_TaskBar,NULL)!=GetCurrentThreadId())
	{
		PostMessage(g_TaskBar,g_StartMenuMsg,MSG_HOTKEYS,enable);
		return;
	}

	// must be executed in the same thread as the start button (otherwise RegisterHotKey doesn't work). also prevents race conditions
	bool bHook=(enable==HOTKEYS_SETTINGS || (enable==HOTKEYS_NORMAL && GetSettingInt(L"ShiftWin")!=0));
	if (bHook)
	{
		RegisterHotKey(g_TaskBar,g_HotkeyShiftID,MOD_SHIFT|MOD_WIN,0);
		g_bHotkeyShift=true;
	}
	else if (g_bHotkeyShift)
	{
		UnregisterHotKey(g_TaskBar,g_HotkeyShiftID);
		g_bHotkeyShift=false;
	}

	if (g_HotkeyCSM)
		UnregisterHotKey(g_TaskBar,g_HotkeyCSMID);
	g_HotkeyCSM=0;

	if (g_HotkeyWSM)
		UnregisterHotKey(g_TaskBar,g_HotkeyWSMID);
	g_HotkeyWSM=0;

	if (enable==HOTKEYS_NORMAL)
	{
		g_HotkeyCSM=GetSettingInt(L"CSMHotkey");
		if (g_HotkeyCSM)
		{
			int mod=MOD_NOREPEAT;
			if (g_HotkeyCSM&(HOTKEYF_SHIFT<<8)) mod|=MOD_SHIFT;
			if (g_HotkeyCSM&(HOTKEYF_CONTROL<<8)) mod|=MOD_CONTROL;
			if (g_HotkeyCSM&(HOTKEYF_ALT<<8)) mod|=MOD_ALT;
			RegisterHotKey(g_TaskBar,g_HotkeyCSMID,mod,g_HotkeyCSM&255);
		}

		g_HotkeyWSM=GetSettingInt(L"WSMHotkey");
		if (g_HotkeyWSM)
		{
			int mod=MOD_NOREPEAT;
			if (g_HotkeyWSM&(HOTKEYF_SHIFT<<8)) mod|=MOD_SHIFT;
			if (g_HotkeyWSM&(HOTKEYF_CONTROL<<8)) mod|=MOD_CONTROL;
			if (g_HotkeyWSM&(HOTKEYF_ALT<<8)) mod|=MOD_ALT;
			RegisterHotKey(g_TaskBar,g_HotkeyWSMID,mod,g_HotkeyWSM&255);
		}
	}
}

bool IsTouchTaskbar(void)
{
	if (!IsWin11())
		return false;

	CRegKey regKey;
	if (regKey.Open(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer") != ERROR_SUCCESS)
		return false;

	DWORD val;
	return regKey.QueryDWORDValue(L"TabletPostureTaskbar", val) == ERROR_SUCCESS && val;
}

static void UpdateStartButtonPosition(const TaskbarInfo* taskBar, const WINDOWPOS* pPos)
{
	if (IsStartButtonSmallIcons(taskBar->taskbarId) != IsTaskbarSmallIcons())
		RecreateStartButton(taskBar->taskbarId);

	RECT rcTask;
	GetWindowRect(taskBar->taskBar, &rcTask);
	if (IsTouchTaskbar())
	{
		if (RECT rc; GetWindowRgnBox(taskBar->taskBar, &rc) != ERROR)
		{
			MapWindowPoints(taskBar->taskBar, NULL, (POINT*)&rc, 2);
			rcTask = rc;
		}
	}
	MONITORINFO info;
	UINT uEdge = GetTaskbarPosition(taskBar->taskBar, &info, NULL, NULL);
	DWORD buttonFlags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSIZE;
	if (IsWindowVisible(taskBar->taskBar))
		buttonFlags |= SWP_SHOWWINDOW;
	else
		buttonFlags |= SWP_HIDEWINDOW;

	APPBARDATA appbar = { sizeof(appbar) };
	if (SHAppBarMessage(ABM_GETSTATE, &appbar) & ABS_AUTOHIDE)
	{
		bool bHide = false;
		if (uEdge == ABE_LEFT)
			bHide = (rcTask.right < info.rcMonitor.left + 5);
		else if (uEdge == ABE_RIGHT)
			bHide = (rcTask.left > info.rcMonitor.right - 5);
		else if (uEdge == ABE_TOP)
			bHide = (rcTask.bottom < info.rcMonitor.top + 5);
		else
			bHide = (rcTask.top > info.rcMonitor.bottom - 5);
		if (bHide)
			buttonFlags = (buttonFlags & ~SWP_SHOWWINDOW) | SWP_HIDEWINDOW;
	}
	if (uEdge == ABE_TOP || uEdge == ABE_BOTTOM)
	{
		if (rcTask.left < info.rcMonitor.left) rcTask.left = info.rcMonitor.left;
		if (rcTask.right > info.rcMonitor.right) rcTask.right = info.rcMonitor.right;
	}
	else
	{
		if (rcTask.top < info.rcMonitor.top) rcTask.top = info.rcMonitor.top;
	}

	HWND zPos = NULL;
	if (pPos->flags & SWP_NOZORDER)
		buttonFlags |= SWP_NOZORDER;
	else
	{
		zPos = pPos->hwndInsertAfter;
		if (zPos == HWND_TOP && !(GetWindowLongPtr(taskBar->startButton, GWL_EXSTYLE) & WS_EX_TOPMOST))
			zPos = HWND_TOPMOST;
		if (zPos == HWND_TOPMOST && !(GetWindowLongPtr(taskBar->taskBar, GWL_EXSTYLE) & WS_EX_TOPMOST))
			zPos = HWND_TOP;
		if (zPos == HWND_BOTTOM)
			buttonFlags |= SWP_NOZORDER;
		if (zPos == taskBar->startButton)
			buttonFlags |= SWP_NOZORDER;
	}

	if (!IsStartButtonSmallIcons(taskBar->taskbarId))
	{
		bool bClassic;
		if (GetWinVersion() < WIN_VER_WIN8)
			bClassic = !IsAppThemed();
		else
		{
			HIGHCONTRAST contrast = { sizeof(contrast) };
			bClassic = (SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) && (contrast.dwFlags & HCF_HIGHCONTRASTON));
		}
		if (!bClassic)
		{
			if (uEdge == ABE_TOP)
				OffsetRect(&rcTask, 0, -1);
			else if (uEdge == ABE_BOTTOM)
				OffsetRect(&rcTask, 0, 1);
		}
	}

	RECT rcOldButton;
	if (taskBar->oldButton)
		GetWindowRect(taskBar->oldButton, &rcOldButton);

	int x, y;
	if (uEdge == ABE_LEFT || uEdge == ABE_RIGHT)
	{
		if (GetSettingInt(L"StartButtonType") != START_BUTTON_CUSTOM || !GetSettingBool(L"StartButtonAlign"))
			x = (rcTask.left + rcTask.right - taskBar->startButtonSize.cx) / 2;
		else if (uEdge == ABE_LEFT)
			x = rcTask.left;
		else
			x = rcTask.right - taskBar->startButtonSize.cx;
		y = taskBar->oldButton ? rcOldButton.top : rcTask.top;
	}
	else
	{
		if (GetWindowLongPtr(taskBar->rebar, GWL_EXSTYLE) & WS_EX_LAYOUTRTL)
			x = (taskBar->oldButton ? rcOldButton.right : rcTask.right) - taskBar->startButtonSize.cx;
		else
			x = taskBar->oldButton ? rcOldButton.left : rcTask.left;
		if (GetSettingInt(L"StartButtonType") != START_BUTTON_CUSTOM || !GetSettingBool(L"StartButtonAlign"))
			y = (rcTask.top + rcTask.bottom - taskBar->startButtonSize.cy) / 2;
		else if (uEdge == ABE_TOP)
			y = rcTask.top;
		else
			y = rcTask.bottom - taskBar->startButtonSize.cy;

		// Start button on Win11 is a bit shifted to the right
		// We will shift our Aero button to cover original button
		if (IsWin11() && (x == info.rcMonitor.left) && (GetStartButtonType() == START_BUTTON_AERO) && !g_epTaskbar)
			x += ScaleForDpi(taskBar->taskBar, 6);
	}

	RECT rcButton = { x, y, x + taskBar->startButtonSize.cx, y + taskBar->startButtonSize.cy };
	RECT rc;
	IntersectRect(&rc, &rcButton, &info.rcMonitor);
	HRGN rgn = CreateRectRgn(rc.left - x, rc.top - y, rc.right - x, rc.bottom - y);
	if (!SetWindowRgn(taskBar->startButton, rgn, FALSE))
	{
		AddTrackedObject(rgn);
		DeleteObject(rgn);
	}

	SetWindowPos(taskBar->startButton, zPos, x, y, 0, 0, buttonFlags);

	if (buttonFlags & SWP_SHOWWINDOW)
		UpdateStartButton(taskBar->taskbarId);
}

static LRESULT CALLBACK SubclassWin81StartButton( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	TaskbarInfo* taskBar = GetTaskbarInfo((int)dwRefData);

	if (uMsg==WM_WINDOWPOSCHANGING)
	{
		// keep the original start button hidden at all times
		if (taskBar && taskBar->bHideButton)
		{
			((WINDOWPOS*)lParam)->flags&=~SWP_SHOWWINDOW;
		}
	}
	if (uMsg==WM_WINDOWPOSCHANGED)
	{
		if (taskBar && taskBar->bReplaceButton)
		{
			UpdateStartButtonPosition(taskBar,(WINDOWPOS*)lParam);
		}
	}
	if (uMsg==WM_SIZE)
	{
		RECT rc;
		GetWindowRect(hWnd,&rc);
		rc.right-=rc.left;
		rc.bottom-=rc.top;
		if (taskBar && (taskBar->oldButtonSize.cx!=rc.right || taskBar->oldButtonSize.cy!=rc.bottom))
		{
			taskBar->oldButtonSize.cx=rc.right;
			taskBar->oldButtonSize.cy=rc.bottom;
			RECT rcTask;
			GetWindowRect(taskBar->taskBar,&rcTask);
			PostMessage(taskBar->taskBar,WM_SIZE,SIZE_RESTORED,MAKELONG(rcTask.right-rcTask.left,rcTask.bottom-rcTask.top));
		}
	}
	if (uMsg==WM_POINTERACTIVATE && CMenuContainer::IsMenuOpened())
		return MA_NOACTIVATE;
#ifdef START_TOUCH
	if (uMsg==WM_POINTERUP || uMsg==WM_POINTERUPDATE || uMsg==WM_POINTERUP)
	{
		POINTER_INPUT_TYPE type;
		GetPointerType2(GET_POINTERID_WPARAM(wParam),&type);
		if (type==PT_TOUCH)
		{
			return SendMessage(GetParent(hWnd),uMsg,wParam,lParam);
		}
	}
#endif
	if (uMsg==WM_PAINT && GetWinVersion()>=WIN_VER_WIN10)
	{
		g_CurrentTaskbarButton=hWnd;
		LRESULT res=DefSubclassProc(hWnd,uMsg,wParam,lParam);
		g_CurrentTaskbarButton=NULL;
		return res;
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

static bool g_bSuppressMessage243;

static LRESULT CALLBACK SubclassWin7StartButton( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==243 && g_bSuppressMessage243)
	{
		// HACK: if the start button is smaller than the original, some NC clicks on the taskbar may be interpreted as clicks on the start button and open the WSM
		// This is prevented by suppressing message 243 while processing WM_NCLBUTTONDOWN on the taskbar
		return 0;
	}
	if (uMsg==WM_WINDOWPOSCHANGING)
	{
		// keep the Win7 start button hidden at all times
		((WINDOWPOS*)lParam)->flags&=~SWP_SHOWWINDOW;
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

static LRESULT CALLBACK SubclassUserPicProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_WINDOWPOSCHANGING && !(((WINDOWPOS*)lParam)->flags&SWP_NOMOVE))
	{
		if (GetSettingBool(L"HideUserPic"))
		{
			((WINDOWPOS*)lParam)->x=-32000;
			((WINDOWPOS*)lParam)->y=-32000;
		}
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

static LRESULT CALLBACK SubclassTopMenuProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_ACTIVATE && GetSettingBool(L"CascadeAll"))
	{
		if (!wParam)
		{
			if (CMenuContainer::s_bPreventClosing) return 0;
			// check if another menu window is being activated
			// if not, close all menus
			for (std::vector<CMenuContainer*>::const_iterator it=CMenuContainer::s_Menus.begin();it!=CMenuContainer::s_Menus.end();++it)
				if ((*it)->m_hWnd==(HWND)lParam)
					return 0;
		}
	}
	if (uMsg==WM_WINDOWPOSCHANGED && (((WINDOWPOS*)lParam)->flags&SWP_SHOWWINDOW))
	{
		g_LastHoverPos=GetMessagePos();
		if (g_ProgramsButton && GetSettingInt(L"InitiallySelect")==1)
			PostMessage(hWnd,WM_CLEAR,'CLSH',0);
		g_CurrentWSMTaskbar=MAIN_TASK_BAR;
		PressStartButton(MAIN_TASK_BAR,true);
	}
	if (uMsg==WM_CLEAR && wParam=='CLSH' && g_ProgramsButton)
	{
		SetFocus(g_ProgramsButton);
		return 0;
	}
	if (uMsg==WM_SHOWWINDOW)
	{
		if (!wParam)
		{
			CMenuContainer::CloseProgramsMenu();
			g_CurrentWSMTaskbar=-1;
			PressStartButton(MAIN_TASK_BAR,false);
		}
		g_bAllProgramsTimer=false;
		if (g_ProgramsButton) KillTimer(g_ProgramsButton,'CLSM');
	}
	if (uMsg==WM_DESTROY)
		g_TopWin7Menu=NULL;
	if (uMsg==WM_ACTIVATEAPP && !wParam)
	{
		if (CMenuContainer::s_bPreventClosing) return 0;
	}
	if (uMsg==WM_MOUSEACTIVATE && GetSettingBool(L"CascadeAll") && CMenuContainer::IsMenuOpened())
	{
		CPoint pt(GetMessagePos());
		if (g_ProgramsButton && WindowFromPoint(pt)==g_ProgramsButton)
			return MA_NOACTIVATEANDEAT;
		CMenuContainer::CloseProgramsMenu();
		return MA_ACTIVATEANDEAT;
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

static LRESULT CALLBACK SubclassProgramsProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_COMMAND && wParam==IDOK && GetSettingBool(L"CascadeAll"))
	{
		if (GetKeyState(VK_SHIFT)<0)
		{
			if (CMenuContainer::IsMenuOpened())
				return 0; // ignore shift+click when the menu is opened
		}
		else
		{
			if (!CMenuContainer::IsMenuOpened())
				CMenuContainer::ToggleStartMenu(MAIN_TASK_BAR,GetKeyState(VK_SPACE)<0 || GetKeyState(VK_RETURN)<0 || GetKeyState(VK_LEFT)<0 || GetKeyState(VK_RIGHT)<0,true);
			return 0;
		}
	}
	if (uMsg==WM_DRAWITEM && wParam==IDOK && CMenuContainer::IsMenuOpened())
	{
		DRAWITEMSTRUCT *pDraw=(DRAWITEMSTRUCT*)lParam;
		pDraw->itemState=ODS_HOTLIGHT; // draw highlighted when the menu is open
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

static BOOL CALLBACK FindWindowsMenuProc( HWND hwnd, LPARAM lParam )
{
	wchar_t name[100];
	GetClassName(hwnd,name,_countof(name));
	if (_wcsicmp(name,L"DV2ControlHost")==0)
	{
		HWND w1=hwnd;
		if (GetWinVersion()==WIN_VER_VISTA)
		{
			w1=FindWindowEx(w1,NULL,L"Desktop Open Pane Host",NULL);
			if (!w1) return TRUE;
		}
		w1=FindWindowEx(w1,NULL,L"Desktop More Programs Pane",NULL);
		if (!w1) return TRUE;

		g_TopWin7Menu=hwnd;
		g_AllPrograms=w1;
		g_ProgramsButton=GetDlgItem(w1,IDOK); // this may not exist
		return FALSE;
	}
	return TRUE;
}

static void FindWindowsMenu( void )
{
	if (g_TopWin7Menu) return;
	if (GetWinVersion()<WIN_VER_WIN8)
	{
		Assert(GetCurrentThreadId()==GetWindowThreadProcessId(g_TaskBar,NULL));
		EnumThreadWindows(GetCurrentThreadId(),FindWindowsMenuProc,0);
		if (g_TopWin7Menu)
		{
			g_UserPic=FindWindow(L"Desktop User Picture",NULL);
			SetWindowSubclass(g_UserPic,SubclassUserPicProc,'CLSH',0);
			SetWindowSubclass(g_TopWin7Menu,SubclassTopMenuProc,'CLSH',0);
			SetWindowSubclass(g_AllPrograms,SubclassProgramsProc,'CLSH',0);
		}
	}
}

static void PrintTaskbarBackground( HDC hdc, const RECT &rcClient, const RECT &rcPaint, UINT uEdge )
{
	FillRect(hdc,&rcPaint,(HBRUSH)GetStockObject(BLACK_BRUSH));
	HDC hsrc=CreateCompatibleDC(hdc);
	HGDIOBJ bmp0=SelectObject(hsrc,g_TaskbarTexture);

	int countH=1, countV=1;
	int offsX=0, offsY=0;
	if (g_TaskbarTileH==TILE_TILE)
	{
		countH=(rcClient.right-rcClient.left+g_TaskbarTextureSize.cx-1)/g_TaskbarTextureSize.cx;
		if (uEdge==ABE_LEFT)
			offsX=rcClient.right-rcClient.left-countH*g_TaskbarTextureSize.cx;
	}
	if (g_TaskbarTileV==TILE_TILE)
	{
		countV=(rcClient.bottom-rcClient.top+g_TaskbarTextureSize.cy-1)/g_TaskbarTextureSize.cy;
		if (uEdge==ABE_TOP)
			offsY=rcClient.bottom-rcClient.top-countV*g_TaskbarTextureSize.cy;
	}

	for (int y=0;y<countV;y++)
	{
		RECT rSrc={0,0,g_TaskbarTextureSize.cx,g_TaskbarTextureSize.cy};
		RECT rDst=rcClient;
		if (g_TaskbarTileV==TILE_TILE)
		{
			rDst.top+=g_TaskbarTextureSize.cy*y+offsY;
			rDst.bottom=rDst.top+g_TaskbarTextureSize.cy;
		}
		if (rDst.bottom<=rcPaint.top) continue;
		if (rDst.top>=rcPaint.bottom) break;
		for (int x=0;x<countH;x++)
		{
			if (g_TaskbarTileH==TILE_TILE)
			{
				rDst.left=rcClient.left+g_TaskbarTextureSize.cx*x+offsX;
				rDst.right=rDst.left+g_TaskbarTextureSize.cx;
			}
			if (rDst.right<=rcPaint.left) continue;
			if (rDst.left>=rcPaint.right) break;
			MarginsBlit(hsrc,hdc,rSrc,rDst,g_TaskbarMargins,true);
		}
	}

	SelectObject(hsrc,bmp0);
	DeleteDC(hsrc);
}

static void ComputeTaskbarColors( int *data )
{
	bool bDefLook;
	int look=GetSettingInt(L"TaskbarLook",bDefLook);
	if (GetWinVersion()<WIN_VER_WIN10 || look==TASKBAR_AEROGLASS || (look==TASKBAR_TRANSPARENT && g_TaskbarTexture))
	{
		memset(data,0,16);
	}
	else
	{
		bool bTransparent;
		DWORD color0=GetMetroTaskbarColor(bTransparent);
		data[0]=look+1; // 1 - opaque, 2 - transparent, 3 - glass
		if (bDefLook)
			data[0]=bTransparent?2:1;
		data[1]=0x13;
		int a=GetSettingInt(L"TaskbarOpacity")*255/100;
		if ((TTaskbarLook)GetSettingInt(L"TaskbarLook")==TASKBAR_OPAQUE)
			a=255;
		if (a<0) a=0;
		if (a>255) a=255;
		bool bDefColor;
		DWORD color=GetSettingInt(L"TaskbarColor",bDefColor);
		if (bDefColor)
			color=color0;
		data[2]=(color&0xFFFFFF)|(a<<24);
	}
	data[3]=0;
}

static void ShowWinX( void )
{
	if (IsWin11())
	{
		HWND hwnd=FindWindowEx(NULL,NULL,L"Shell_TrayWnd",NULL);
		if (hwnd)
			PostMessage(hwnd,WM_HOTKEY,590,MAKELPARAM(MOD_WIN,'X'));

		return;
	}

	if (GetWinVersion()>=WIN_VER_WIN10)
	{
		CComPtr<IUnknown> pImmersiveShell;
		if (CreateImmersiveShell(pImmersiveShell))
		{
			CComPtr<IImmersiveMonitorService> pMonitorService;
			IUnknown_QueryService(pImmersiveShell,SID_IImmersiveMonitorService,IID_IImmersiveMonitorService,(void**)&pMonitorService);
			if (pMonitorService)
			{
				CPoint pt(GetMessagePos());
				HMONITOR monitor=MonitorFromPoint(pt,MONITOR_DEFAULTTONEAREST);
				CComPtr<IUnknown> pMonitor;
				pMonitorService->GetFromHandle(monitor,&pMonitor);
				if (pMonitorService)
				{
					CComPtr<ILauncherTipContextMenu> pMenu;
					IUnknown_QueryService(pMonitor,SID_LauncherTipContextMenu,SID_LauncherTipContextMenu,(void**)&pMenu);
					if (pMenu)
						pMenu->ShowLauncherTipContextMenu(&pt);
				}
			}
		}
	}
	else if (g_AppManagerThread)
		PostThreadMessage(g_AppManagerThread,g_StartMenuMsg,MSG_WINXMENU,GetMessagePos());
}

static LRESULT CALLBACK SubclassTrayButtonProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_WINDOWPOSCHANGING)
	{
		const TaskbarInfo *taskBar=GetTaskbarInfo((int)dwRefData);
		if (taskBar && (taskBar->bReplaceButton || taskBar->bHideButton))
		{
			WINDOWPOS *pPos=(WINDOWPOS*)lParam;
			if (!(pPos->flags&SWP_NOMOVE) || !(pPos->flags&SWP_NOSIZE))
			{
				if (pPos->flags&(SWP_NOMOVE|SWP_NOSIZE))
				{
					RECT rc;
					GetWindowRect(hWnd,&rc);
					MapWindowPoints(NULL,GetParent(hWnd),(POINT*)&rc,2);
					if (pPos->flags&SWP_NOMOVE)
					{
						pPos->x=rc.left;
						pPos->y=rc.top;
					}
					else
					{
						pPos->cx=rc.right-rc.left;
						pPos->cy=rc.bottom-rc.top;
					}
				}
				int dx=0, dy=0;
				UINT uEdge=GetTaskbarPosition(taskBar->taskBar,NULL,NULL,NULL);
				if (uEdge==ABE_LEFT || uEdge==ABE_RIGHT)
				{
					dy=taskBar->startButtonSize.cy-taskBar->oldButtonSize.cy;
				}
				else
				{
					dx=taskBar->startButtonSize.cx-taskBar->oldButtonSize.cx;
				}
				if (dx || dy)
				{
					pPos->x+=dx;
					pPos->y+=dy;
					pPos->flags&=~(SWP_NOMOVE);
				}
			}
		}
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

static LRESULT CALLBACK SubclassTaskBarProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_MOUSEACTIVATE && (HIWORD(lParam)==WM_MBUTTONDOWN || GetWinVersion()>=WIN_VER_WIN10))
	{
		if (GetWinVersion()>=WIN_VER_WIN10 && CMenuContainer::IsMenuOpened() && CMenuContainer::HasInputHandler() && GetFocus())
		{
			// Win10: if the taskbar is clicked while the menu is opened, check if the touch keyboard button was clicked and ignore the activation
			// Otherwise the click on the touch keyboard button will deactivate the menu (possibly because both run on the same thread)
			// On Win8.1 the button already handles this and returns MA_NOACTIVATE
			HWND hwnd=WindowFromPoint(CPoint(GetMessagePos()));
			if (hwnd)
			{
				wchar_t className[256];
				GetClassName(hwnd,className,_countof(className));
				if (_wcsicmp(className,L"TIPBand")==0)
					return MA_NOACTIVATE;
			}
		}
		FindWindowsMenu();
		if (g_TopWin7Menu && WindowsMenuOpened())
		{
			DefSubclassProc(hWnd,uMsg,wParam,lParam);
			return MA_ACTIVATEANDEAT; // ignore the next middle click, so it doesn't re-open the start menu
		}
	}
	TaskbarInfo *taskBar=GetTaskbarInfo((int)dwRefData);
	if (taskBar && (uMsg==WM_NCMOUSEMOVE || uMsg==WM_MOUSEMOVE) && PointAroundStartButton((int)dwRefData))
		TaskBarMouseMove(taskBar->taskbarId);
	if (taskBar && uMsg==WM_POINTERACTIVATE && CMenuContainer::IsMenuOpened())
		return MA_NOACTIVATE;
	static int touchTime;
#ifdef START_TOUCH
	if (taskBar && (uMsg==WM_NCPOINTERDOWN || (uMsg==WM_POINTERDOWN && IS_POINTER_INCONTACT_WPARAM(wParam) && IS_POINTER_PRIMARY_WPARAM(wParam) && IS_POINTER_FIRSTBUTTON_WPARAM(wParam))))
	{
		POINTER_INPUT_TYPE type;
		GetPointerType2(GET_POINTERID_WPARAM(wParam),&type);
		if (type==PT_TOUCH)
		{
			if (PointAroundStartButton((int)dwRefData,CPoint(lParam)))
			{
				taskBar->pointerId=GET_POINTERID_WPARAM(wParam);
				touchTime=GetMessageTime();
				return 0;
			}
		}
	}
	if (taskBar && (uMsg==WM_NCPOINTERUPDATE || uMsg==WM_POINTERUPDATE) && taskBar->pointerId==GET_POINTERID_WPARAM(wParam))
	{
		if (uMsg==WM_NCPOINTERUPDATE)
		{
			POINTER_INFO info;
			if (GetPointerInfo2(taskBar->pointerId,&info) && (info.pointerFlags&4)!=0)
			{
				int time=GetMessageTime();
				if (time-touchTime>500)
				{
					taskBar->pointerId=0;
					ShowWinX();
					return 0;
				}
			}
		}
		else if (IS_POINTER_INCONTACT_WPARAM(wParam))
		{
			int time=GetMessageTime();
			if (time-touchTime>500)
			{
				taskBar->pointerId=0;
				PostMessage(taskBar->startButton,WM_RBUTTONUP,0,MAKELPARAM(-1,-1));
				return 0;
			}
		}
		else
			uMsg=WM_POINTERUP;
	}
	if (taskBar && (uMsg==WM_POINTERUP || uMsg==WM_NCPOINTERUP) && taskBar->pointerId==GET_POINTERID_WPARAM(wParam))
	{
		if (PointAroundStartButton((int)dwRefData,CPoint(lParam)))
		{
			int control=GetSettingInt(L"MouseClick");
			if (control==OPEN_BOTH && GetWinVersion()>=WIN_VER_WIN10)
				control=GetWin10TabletMode()?OPEN_WINDOWS:OPEN_CLASSIC;
			if (control==OPEN_CLASSIC)
				ToggleStartMenu(taskBar->taskbarId,false);
			else if (control==OPEN_WINDOWS)
				PostMessage(g_ProgWin,WM_SYSCOMMAND,SC_TASKLIST,'WSMM');
		}
		taskBar->pointerId=0;
		return 0;
	}
#endif
	if (uMsg==WM_SHOWWINDOW && taskBar)
	{
		if (taskBar->bReplaceButton)
			ShowWindow(taskBar->startButton,wParam?SW_SHOW:SW_HIDE);
	}
	if (uMsg==WM_WINDOWPOSCHANGING && taskBar && taskBar->bReplaceButton)
	{
		WINDOWPOS *pPos=(WINDOWPOS*)lParam;
		if (!(pPos->flags&SWP_NOZORDER) && pPos->hwndInsertAfter==HWND_BOTTOM)
		{
			SetWindowPos(taskBar->startButton,HWND_BOTTOM,0,0,0,0,SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOSIZE|SWP_NOMOVE);
			pPos->hwndInsertAfter=taskBar->startButton;
		}
	}
	if (uMsg==WM_WINDOWPOSCHANGED && taskBar)
	{
		if (taskBar->bReplaceButton)
		{
			UpdateStartButtonPosition(taskBar,(WINDOWPOS*)lParam);
		}
		if (taskBar->oldButton && GetWinVersion()<WIN_VER_WIN10)
		{
			UINT uEdge=GetTaskbarPosition(hWnd,NULL,NULL,NULL);
			int x=(uEdge==ABE_BOTTOM?-1:0);
			SetWindowPos(taskBar->oldButton,NULL,x,0,0,0,SWP_NOSIZE|SWP_NOZORDER);
		}
	}
	if (uMsg==WM_THEMECHANGED && taskBar)
	{
		if (taskBar->bReplaceButton)
		{
			RecreateStartButton((int)dwRefData);
		}
		taskBar->bThemeChanging=true;
		LRESULT res=DefSubclassProc(hWnd,uMsg,wParam,lParam);
		taskBar->bThemeChanging=false;
		return res;
	}
	if ((uMsg==WM_DWMCOLORIZATIONCOLORCHANGED || uMsg==WM_SETTINGCHANGE || uMsg==0x5CB || uMsg==0x5BB) && taskBar && taskBar->bCustomLook && SetWindowCompositionAttribute && GetWinVersion()>=WIN_VER_WIN10)
	{
		LRESULT res=DefSubclassProc(hWnd,uMsg,wParam,lParam);
		int data[4];
		ComputeTaskbarColors(data);
		WINCOMPATTRDATA attrData={0x13,&data,sizeof(data)};
		SetWindowCompositionAttribute(hWnd,&attrData);
		return res;
	}
	if ((uMsg==WM_DWMCOLORIZATIONCOLORCHANGED || uMsg==WM_SETTINGCHANGE) && taskBar && taskBar->bCustomLook && SetWindowCompositionAttribute && GetWinVersion()<WIN_VER_WIN10)
	{
		LRESULT res=DefSubclassProc(hWnd,uMsg,wParam,lParam);
		UpdateTaskBars(TASKBAR_UPDATE_TEXTURE);
		return res;
	}
	if (uMsg==WM_PAINT && taskBar)
	{
		if (taskBar->bCustomLook)
		{
			TTaskbarLook look=(TTaskbarLook)GetSettingInt(L"TaskbarLook");
			WORD winVer=GetWinVersion();
			BOOL blurBehind;
			int margin;
			enum { FLAG_BLUR=1, FLAG_MARGIN=2, FLAG_ATTRIBUTE=4};
			int flags=0;
			if (winVer==WIN_VER_WIN7)
			{
				blurBehind=look==TASKBAR_GLASS;
				margin=look==TASKBAR_OPAQUE?0:-1;
				flags=FLAG_BLUR|FLAG_MARGIN;
			}
			else if (winVer==WIN_VER_WIN8)
			{
				blurBehind=look==TASKBAR_OPAQUE;
				margin=look==TASKBAR_OPAQUE?0:-1;
				flags=FLAG_BLUR|FLAG_MARGIN|((look==TASKBAR_TRANSPARENT && g_TaskbarTexture)?FLAG_ATTRIBUTE:0);
			}
			else if (winVer==WIN_VER_WIN81)
			{
				blurBehind=look==TASKBAR_OPAQUE;
				margin=look==TASKBAR_OPAQUE?0:-1;
				flags=FLAG_BLUR|FLAG_MARGIN|((look==TASKBAR_OPAQUE || g_TaskbarTexture)?FLAG_ATTRIBUTE:0);
			}
			else if (winVer>=WIN_VER_WIN10)
			{
				blurBehind=TRUE;
				margin=look==TASKBAR_OPAQUE?0:-1;
				flags=(look==TASKBAR_AEROGLASS?FLAG_BLUR:0)|(look!=TASKBAR_GLASS?FLAG_MARGIN:0)|FLAG_ATTRIBUTE;
			}

			if (flags&FLAG_BLUR)
			{
				DWM_BLURBEHIND blur={DWM_BB_ENABLE,blurBehind};
				DwmEnableBlurBehindWindow(hWnd,&blur);
			}
			if (flags&FLAG_MARGIN)
			{
				MARGINS margins={margin};
				DwmExtendFrameIntoClientArea(hWnd,&margins);
			}
			if (SetWindowCompositionAttribute && (flags&FLAG_ATTRIBUTE))
			{
				int data[4];
				ComputeTaskbarColors(data);
				WINCOMPATTRDATA attrData={0x13,&data,sizeof(data)};
				SetWindowCompositionAttribute(hWnd,&attrData);
			}
			if (g_TaskbarTexture && IsAppThemed())
			{
				// draw taskbar background (behind start button and separators)
				PAINTSTRUCT ps;
				HDC hdc=BeginPaint(hWnd,&ps);
				RECT rc;
				GetClientRect(hWnd,&rc);
				UINT uEdge=GetTaskbarPosition(hWnd,NULL,NULL,NULL);
				PrintTaskbarBackground(hdc,rc,ps.rcPaint,uEdge);
				EndPaint(hWnd,&ps);
				return 0;
			}
		}
		if (taskBar->bReplaceButton && g_WinStartButton && !IsAppThemed())
		{
			// prevent painting of the default classic button
			PAINTSTRUCT ps;
			HDC hdc=BeginPaint(hWnd,&ps);
			EndPaint(hWnd,&ps);
			return 0;
		}
	}
	if (uMsg==WM_PRINTCLIENT && g_TaskbarTexture)
	{
		// print taskbar background - for background of buttons and tray area
		HDC hdc=(HDC)wParam;
		RECT rc;
		GetClientRect(hWnd,&rc);
		UINT uEdge=GetTaskbarPosition(hWnd,NULL,NULL,NULL);
		PrintTaskbarBackground(hdc,rc,rc,uEdge);
		return 0;
	}
	if (uMsg==0x5C5 && taskBar && taskBar->bCustomLook && IsWin81Update1()) // some secret message when the taskbar is raised to the top
	{
		// reset the opaqueness
		PostMessage(g_TaskBar,g_StartMenuMsg,MSG_REDRAWTASKBAR,(LPARAM)hWnd);
	}
	if (uMsg==WM_DESTROY && taskBar)
	{
		if (taskBar->bReplaceButton)
			DestroyStartButton(taskBar->taskbarId);
		g_TaskbarInfos.erase(g_TaskbarInfos.find(taskBar->taskbarId));
	}
	if (uMsg==WM_TIMER && wParam=='CLSM')
	{
		if (GetMetroMode(NULL)==METRO_NONE)
		{
			KillTimer(hWnd,'CLSM');
			return 0;
		}
		SetForegroundWindow(hWnd);
		LogToFile(STARTUP_LOG,L"StartMenu DLL: skipping Metro");
		if (GetSettingInt(L"SkipMetroCount")<0)
		{
			INPUT inputs[4]={
				{INPUT_KEYBOARD},
				{INPUT_KEYBOARD},
				{INPUT_KEYBOARD},
				{INPUT_KEYBOARD},
			};
			inputs[0].ki.wVk=VK_LWIN;
			inputs[1].ki.wVk='D';
			inputs[2].ki.wVk='D';
			inputs[2].ki.dwFlags=KEYEVENTF_KEYUP;
			inputs[3].ki.wVk=VK_LWIN;
			inputs[3].ki.dwFlags=KEYEVENTF_KEYUP;
			SendInput(_countof(inputs),inputs,sizeof(INPUT));
		}
		else
		{
			HWND hwnd=FindWindow(L"ModeInputWnd",NULL);
			if (hwnd)
			{
				DWORD process;
				GetWindowThreadProcessId(hwnd,&process);
				if (process==GetCurrentProcessId())
				{
					IObjectWithSite *pObject=(IObjectWithSite*)GetWindowLongPtr(hwnd,0);
					if (pObject)
					{
						CComPtr<IUnknown> pSite;
						pObject->GetSite(IID_IUnknown,(void**)&pSite);
						if (pSite)
						{
							CComPtr<IImmersiveLauncher80> pLauncher;
							IUnknown_QueryService(pSite,SID_ImmersiveLauncher,IID_IImmersiveLauncher80,(void**)&pLauncher);
							if (pLauncher)
								pLauncher->Dismiss(5);
						}
					}
				}
			}
		}
		g_SkipMetroCount--;
		if (g_SkipMetroCount<=0)
			KillTimer(hWnd,'CLSM');
		return 0;
	}
	if (uMsg==WM_MOVE)
	{
		ResetHotCorners();
	}
	if (uMsg==WM_NCLBUTTONDOWN && taskBar && GetWinVersion()==WIN_VER_WIN7 && taskBar->bReplaceButton)
	{
		g_bSuppressMessage243=true;
		LRESULT res=DefSubclassProc(hWnd,uMsg,wParam,lParam);
		g_bSuppressMessage243=false;
		return res;
	}
	if (uMsg==WM_PARENTNOTIFY && taskBar && LOWORD(wParam)==WM_CREATE && GetWinVersion()>=WIN_VER_WIN10)
	{
		wchar_t name[100];
		HWND child=(HWND)lParam;
		GetClassName(child,name,_countof(name));
		if (_wcsicmp(name,L"TrayButton")==0 && GetParent(child)==hWnd)
		{
			bool bFound=false;
			for (std::vector<HWND>::const_iterator it=taskBar->trayButtons.begin();it!=taskBar->trayButtons.end();++it)
				if (*it==child)
				{
					bFound=true;
					break;
				}
			if (!bFound)
			{
				taskBar->trayButtons.push_back(child);
				SetWindowSubclass(child,SubclassTrayButtonProc,'CLSH',taskBar->taskbarId);
			}
		}
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

static LRESULT CALLBACK SubclassTaskListProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_PAINT && g_TaskbarTexture)
	{
		wchar_t name[100];
		GetClassName(hWnd,name,_countof(name));
		if (_wcsicmp(name,L"MSTaskSwWClass")==0)
		{
			// draw taskbar background (behind task list)
			PAINTSTRUCT ps;
			HDC hdc=BeginPaint(hWnd,&ps);
			DrawThemeParentBackground(hWnd,hdc,NULL);
			EndPaint(hWnd,&ps);
			return 0;
		}
	}

	if (uMsg==WM_PAINT || uMsg==WM_PRINT || uMsg==WM_PRINTCLIENT)
	{
		g_CurrentTaskList=hWnd;
		LRESULT res=DefSubclassProc(hWnd,uMsg,wParam,lParam);
		g_CurrentTaskList=NULL;
		return res;
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

static LRESULT CALLBACK SubclassTrayChevronProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_PAINT || uMsg==WM_PRINT || uMsg==WM_PRINTCLIENT)
	{
		g_CurrentTaskChevron=hWnd;
		LRESULT res=DefSubclassProc(hWnd,uMsg,wParam,lParam);
		g_CurrentTaskChevron=NULL;
		return res;
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

static LRESULT CALLBACK SubclassDesktopButtonProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_PAINT || uMsg==WM_PRINT || uMsg==WM_PRINTCLIENT)
	{
		g_CurrentDesktopButton=hWnd;
		LRESULT res=DefSubclassProc(hWnd,uMsg,wParam,lParam);
		g_CurrentDesktopButton=NULL;
		return res;
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

static LRESULT CALLBACK SubclassRebarProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_WINDOWPOSCHANGING)
	{
		const TaskbarInfo *taskBar=GetTaskbarInfo((int)dwRefData);
		if (taskBar && (taskBar->bReplaceButton || taskBar->bHideButton))
		{
			WINDOWPOS *pPos=(WINDOWPOS*)lParam;
			if (!(pPos->flags&SWP_NOMOVE) || !(pPos->flags&SWP_NOSIZE))
			{
				if (pPos->flags&(SWP_NOMOVE|SWP_NOSIZE))
				{
					RECT rc;
					GetWindowRect(hWnd,&rc);
					MapWindowPoints(NULL,GetParent(hWnd),(POINT*)&rc,2);
					if (pPos->flags&SWP_NOMOVE)
					{
						pPos->x=rc.left;
						pPos->y=rc.top;
					}
					else
					{
						pPos->cx=rc.right-rc.left;
						pPos->cy=rc.bottom-rc.top;
					}
				}
				int dx=0, dy=0;
				UINT uEdge=GetTaskbarPosition(taskBar->taskBar,NULL,NULL,NULL);
				if (taskBar->oldButton)
				{
					if (uEdge==ABE_LEFT || uEdge==ABE_RIGHT)
					{
						dy=taskBar->startButtonSize.cy-taskBar->oldButtonSize.cy;
					}
					else
					{
						dx=taskBar->startButtonSize.cx-taskBar->oldButtonSize.cx;
					}
				}
				else
				{
					if (uEdge==ABE_LEFT || uEdge==ABE_RIGHT)
					{
						dy=taskBar->startButtonSize.cy-pPos->y;
					}
					else
					{
						dx=taskBar->startButtonSize.cx-pPos->x;
					}
				}
				if (dx || dy)
				{
					pPos->x+=dx;
					pPos->cx-=dx;
					pPos->y+=dy;
					pPos->cy-=dy;
					pPos->flags&=~(SWP_NOMOVE|SWP_NOSIZE);
				}
			}
		}
	}
	if (uMsg==WM_PAINT || uMsg==WM_PRINT || uMsg==WM_PRINTCLIENT)
	{
		g_CurrentRebar=hWnd;
		LRESULT res=DefSubclassProc(hWnd,uMsg,wParam,lParam);
		g_CurrentRebar=NULL;
		return res;
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

static LRESULT CALLBACK SubclassTaskbarPartProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_PAINT || uMsg==WM_PRINT || uMsg==WM_PRINTCLIENT)
	{
		g_CurrentTaskbarPart=hWnd;
		LRESULT res=DefSubclassProc(hWnd,uMsg,wParam,lParam);
		g_CurrentTaskbarPart=NULL;
		return res;
	}
	if (uMsg==WM_NCDESTROY)
	{
		TaskbarInfo *info=GetTaskbarInfo((int)dwRefData);
		if (info)
		{
			for (std::vector<HWND>::const_iterator it=info->taskbarParts.begin();it!=info->taskbarParts.end();++it)
				if (*it==hWnd)
				{
					info->taskbarParts.erase(it);
					break;
				}
		}
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

static void HandleTaskbarParts( TaskbarInfo &taskBar, bool bPrimary )
{
	if (taskBar.rebar)
	{
		int count=(int)SendMessage(taskBar.rebar,RB_GETBANDCOUNT,0,0);
		for (int i=0;i<count;i++)
		{
			REBARBANDINFO info={sizeof(info),RBBIM_CHILD};
			if (SendMessage(taskBar.rebar,RB_GETBANDINFO,i,(LPARAM)&info))
			{
				wchar_t name[100];
				GetClassName(info.hwndChild,name,_countof(name));
				if (_wcsicmp(name,L"ToolbarWindow32")==0 && !taskBar.HasPart(info.hwndChild))
				{
					SetWindowSubclass(info.hwndChild,SubclassTaskbarPartProc,'CLSH',taskBar.taskbarId);
					taskBar.taskbarParts.push_back(info.hwndChild);
				}
			}
		}
	}
	if (bPrimary)
	{
		HWND tray=FindWindowEx(taskBar.taskBar,NULL,L"TrayNotifyWnd",NULL);
		if (tray)
		{
			HWND clock=FindWindowEx(tray,NULL,L"TrayClockWClass",NULL);
			if (clock && !taskBar.HasPart(clock))
			{
				SetWindowSubclass(clock,SubclassTaskbarPartProc,'CLSH',taskBar.taskbarId);
				taskBar.taskbarParts.push_back(clock);
			}
			if (GetWinVersion()<=WIN_VER_WIN81)
			{
				taskBar.desktop=FindWindowEx(tray,NULL,L"TrayShowDesktopButtonWClass",NULL);
				if (taskBar.desktop)
					SetWindowSubclass(taskBar.desktop,SubclassDesktopButtonProc,'CLSH',taskBar.taskbarId);
			}
		}
	}
	else if (GetWinVersion()>=WIN_VER_WIN10)
	{
		HWND clock=FindWindowEx(taskBar.taskBar,NULL,L"ClockButton",NULL);
		if (clock && !taskBar.HasPart(clock))
		{
			SetWindowSubclass(clock,SubclassTaskbarPartProc,'CLSH',taskBar.taskbarId);
			taskBar.taskbarParts.push_back(clock);
		}
	}
}

static void HandleSecondaryTaskbar( HWND hwnd )
{
	int taskbarId=g_NextTaskbar++;
	TaskbarInfo &taskBar=g_TaskbarInfos[taskbarId];
	taskBar.taskBar=hwnd;
	taskBar.taskbarId=taskbarId;

	taskBar.rebar=FindWindowEx(hwnd,NULL,L"WorkerW",NULL);
	if (taskBar.rebar)
	{
		SetWindowSubclass(taskBar.rebar,SubclassRebarProc,'CLSH',taskbarId);
		taskBar.taskList=FindWindowEx(taskBar.rebar,NULL,L"MSTaskListWClass",NULL);
		if (taskBar.taskList)
			SetWindowSubclass(taskBar.taskList,SubclassTaskListProc,'CLSH',taskbarId);
	}
	if (GetWinVersion()>WIN_VER_WIN8)
	{
		taskBar.oldButton=FindWindowEx(taskBar.taskBar,NULL,L"Start",NULL);
		if (taskBar.oldButton)
		{
			if (GetWinVersion()>=WIN_VER_WIN10)
			{
				taskBar.pOriginalTarget=(IDropTarget*)GetProp(taskBar.oldButton,L"OleDropTargetInterface");
				if (taskBar.pOriginalTarget)
					RevokeDragDrop(taskBar.oldButton);
			}

			CStartMenuTarget *pNewTarget=new CStartMenuTarget(taskBar.taskbarId);
			RegisterDragDrop(taskBar.oldButton,pNewTarget);
			pNewTarget->Release();
			if (GetWinVersion()<WIN_VER_WIN10 && GetTaskbarPosition(taskBar.taskBar,NULL,NULL,NULL)==ABE_BOTTOM)
				SetWindowPos(taskBar.oldButton,NULL,-1,0,0,0,SWP_NOSIZE|SWP_NOZORDER);
			SetWindowSubclass(taskBar.oldButton,SubclassWin81StartButton,'CLSH',taskBar.taskbarId);
		}
	}
	if (GetWinVersion()>=WIN_VER_WIN10)
	{
		for (HWND button=FindWindowEx(taskBar.taskBar,NULL,L"TrayButton",NULL);button;button=FindWindowEx(taskBar.taskBar,button,L"TrayButton",NULL))
		{
			taskBar.trayButtons.push_back(button);
			SetWindowSubclass(button,SubclassTrayButtonProc,'CLSH',taskBar.taskbarId);
		}
		HWND search=FindWindowEx(taskBar.taskBar,NULL,L"TrayDummySearchControl",NULL);
		if (search)
		{
			taskBar.trayButtons.push_back(search);
			SetWindowSubclass(search,SubclassTrayButtonProc,'CLSH',taskBar.taskbarId);
		}
	}
	HandleTaskbarParts(taskBar,false);
	SetWindowSubclass(taskBar.taskBar,SubclassTaskBarProc,'CLSH',taskbarId);
	UpdateTaskBars(TASKBAR_UPDATE);
	UpdateTaskBars(TASKBAR_UPDATE_TEXTURE);
}

static BOOL CALLBACK HookAllTaskbarsEnum( HWND hwnd, LPARAM lParam )
{
	// look for top-level windows with class "Shell_SecondaryTrayWnd" in the current thread
	if (GetWindowThreadProcessId(hwnd,NULL)!=GetCurrentThreadId()) return TRUE;
	wchar_t name[256];
	GetClassName(hwnd,name,_countof(name));
	if (_wcsicmp(name,L"Shell_SecondaryTrayWnd")==0)
		HandleSecondaryTaskbar(hwnd);
	return TRUE;
}

void UpdateTaskBars( TUpdateTaskbar update )
{
	if (update==TASKBAR_UPDATE_TEXTURE)
	{
		if (g_TaskbarTexture)
			DeleteObject(g_TaskbarTexture);
		g_TaskbarTexture=NULL;
		if (GetSettingBool(L"CustomTaskbar"))
		{
			g_TaskbarTileH=g_TaskbarTileV=TILE_STRETCH;
			g_TaskbarMargins.left=g_TaskbarMargins.right=g_TaskbarMargins.top=g_TaskbarMargins.bottom=0;
			TTaskbarLook look=(TTaskbarLook)GetSettingInt(L"TaskbarLook");
			bool bDefOpacity;
			int opacity=GetSettingInt(L"TaskbarOpacity",bDefOpacity);
			if (look==TASKBAR_OPAQUE)
				opacity=100, bDefOpacity=true;
			bool bDefColor;
			COLORREF color=GetSettingInt(L"TaskbarColor",bDefColor);
			wchar_t fname[_MAX_PATH];
			Strcpy(fname,_countof(fname),GetSettingString(L"TaskbarTexture"));
			DoEnvironmentSubst(fname,_countof(fname));
			if (*fname)
			{
				g_TaskbarTexture=LoadImageResource(NULL,fname,false,true);
				g_TaskbarTileH=(TTaskbarTile)GetSettingInt(L"TaskbarTileH");
				g_TaskbarTileV=(TTaskbarTile)GetSettingInt(L"TaskbarTileV");
				if (g_TaskbarTileH==TILE_STRETCH)
				{
					CString borders=GetSettingString(L"TaskbarBordersH");
					if (!borders.IsEmpty())
					{
						wchar_t token[256];
						const wchar_t *str=GetToken(borders,token,_countof(token),L", \t");
						g_TaskbarMargins.left=_wtol(token);
						if (g_TaskbarMargins.left<0) g_TaskbarMargins.left=0;
						str=GetToken(str,token,_countof(token),L", \t");
						g_TaskbarMargins.right=_wtol(token);
						if (g_TaskbarMargins.right<0) g_TaskbarMargins.right=0;
					}
				}
				if (g_TaskbarTileV==TILE_STRETCH)
				{
					CString borders=GetSettingString(L"TaskbarBordersV");
					if (!borders.IsEmpty())
					{
						wchar_t token[256];
						const wchar_t *str=GetToken(borders,token,_countof(token),L", \t");
						g_TaskbarMargins.top=_wtol(token);
						if (g_TaskbarMargins.top<0) g_TaskbarMargins.top=0;
						str=GetToken(str,token,_countof(token),L", \t");
						g_TaskbarMargins.bottom=_wtol(token);
						if (g_TaskbarMargins.bottom<0) g_TaskbarMargins.bottom=0;
					}
				}
			}
			else if (GetWinVersion()<WIN_VER_WIN10 && (!bDefColor || !bDefOpacity))
			{
				if (bDefColor && GetWinVersion()>WIN_VER_WIN7)
				{
					color=GetSystemGlassColor8();
					color=((color&0xFF)<<16)|(color&0xFF00)|((color>>16)&0xFF);
				}
				BITMAPINFO bi={0};
				bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
				bi.bmiHeader.biWidth=bi.bmiHeader.biHeight=32;
				bi.bmiHeader.biPlanes=1;
				bi.bmiHeader.biBitCount=32;

				HDC hdc=CreateCompatibleDC(NULL);
				unsigned int *bits;
				g_TaskbarTexture=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,(void**)&bits,NULL,0);
				if (g_TaskbarTexture)
				{
					unsigned int val=((color&0xFF)<<16)|(color&0x00FF00)|((color>>16)&0xFF)|0xFF000000;
					int count=bi.bmiHeader.biWidth*bi.bmiHeader.biHeight;
					for (int i=0;i<count;i++)
						bits[i]=val;
				}
				DeleteDC(hdc);
			}
			if (g_TaskbarTexture)
			{
				BITMAP info;
				GetObject(g_TaskbarTexture,sizeof(info),&info);
				int a=255;
				BOOL comp=FALSE;
				if (look!=TASKBAR_OPAQUE && SUCCEEDED(DwmIsCompositionEnabled(&comp)) && comp)
					a=opacity*255/100;
				if (a<0) a=0;
				if (a<255)
				{
					int count=info.bmHeight*info.bmWidthBytes;
					unsigned char *ptr=(unsigned char*)info.bmBits;
					for (int i=0;i<count;i++,ptr++)
						*ptr=*ptr*a/255;
				}
				g_TaskbarTextureSize.cx=info.bmWidth;
				g_TaskbarTextureSize.cy=info.bmHeight;
				int countH=g_TaskbarTileH==TILE_TILE?(255+info.bmWidth)/info.bmWidth:1;
				int countV=g_TaskbarTileV==TILE_TILE?(255+info.bmHeight)/info.bmHeight:1;
				if (countH>1 || countV>1)
				{
					// pretile texture
					BITMAPINFO bi={0};
					bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
					bi.bmiHeader.biWidth=countH*info.bmWidth;
					bi.bmiHeader.biHeight=countV*info.bmHeight;
					bi.bmiHeader.biPlanes=1;
					bi.bmiHeader.biBitCount=32;

					HDC hdc=CreateCompatibleDC(NULL);
					HDC hsrc=CreateCompatibleDC(hdc);
					unsigned int *bits;
					HBITMAP bmp=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,(void**)&bits,NULL,0);
					if (bmp)
					{
						HGDIOBJ bmp01=SelectObject(hdc,bmp);
						HGDIOBJ bmp02=SelectObject(hsrc,g_TaskbarTexture);
						for (int y=0;y<countV;y++)
							for (int x=0;x<countH;x++)
								BitBlt(hdc,x*info.bmWidth,y*info.bmHeight,info.bmWidth,info.bmHeight,hsrc,0,0,SRCCOPY);
						SelectObject(hsrc,bmp02);
						SelectObject(hdc,bmp01);
						DeleteObject(g_TaskbarTexture);
						g_TaskbarTexture=bmp;
						g_TaskbarTextureSize.cx*=countH;
						g_TaskbarTextureSize.cy*=countV;
					}
					DeleteDC(hsrc);
					DeleteDC(hdc);
				}
			}
			for (id_taskbar_map::iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
				RedrawWindow(it->second.taskBar,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN);
		}
		return;
	}
	bool bButton=false, bCustomLook= false, bAll= false;
	if (update==TASKBAR_CLEAR)
	{
		if (g_TaskbarTexture)
			DeleteObject(g_TaskbarTexture);
		g_TaskbarTexture=NULL;
	}
	else
	{
		bButton=GetSettingBool(L"EnableStartButton");
		bCustomLook=GetSettingBool(L"CustomTaskbar");
		bAll=GetSettingBool(L"AllTaskbars");
	}
	if (g_bTrimHooks)
		bButton=false;
	for (id_taskbar_map::iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
	{
		TaskbarInfo &taskBar=it->second;
		bool bButton2=bButton && (bAll || taskBar.taskBar==g_TaskBar);
		bool bHideButton2=bButton;
		if (taskBar.oldButton)
		{
			if (bHideButton2 && !bButton2)
			{
				// reposition rebar
				if (taskBar.oldButton)
				{
					RECT rc;
					GetWindowRect(taskBar.oldButton,&rc);
					taskBar.oldButtonSize.cx=rc.right-rc.left;
					taskBar.oldButtonSize.cy=rc.bottom-rc.top;
				}
				RECT rcTask;
				GetWindowRect(taskBar.taskBar,&rcTask);
				PostMessage(taskBar.taskBar,WM_SIZE,SIZE_RESTORED,MAKELONG(rcTask.right-rcTask.left,rcTask.bottom-rcTask.top));
			}
			if (taskBar.bHideButton!=bHideButton2)
			{
				// show/hide 8.1 button
				taskBar.bHideButton=bHideButton2;
				if (bHideButton2)
				{
					ShowWindow(taskBar.oldButton,SW_HIDE);
				}
				else
				{
					ShowWindow(taskBar.oldButton,SW_SHOW);
				}
			}
		}
		if (taskBar.bReplaceButton!=bButton2)
		{
			// create or destroy button
			taskBar.bReplaceButton=bButton2;

			if (bButton2)
			{
				RecreateStartButton(it->first);
				if (g_WinStartButton)
				{
					ShowWindow(g_WinStartButton,SW_HIDE);
					SetWindowSubclass(g_WinStartButton,SubclassWin7StartButton,'CLSH',0);
					if (GetWinVersion()==WIN_VER_WIN7)
					{
						// Windows 7 draws the start button on the taskbar as well
						// so we zero out the bitmap resources
						HMODULE hExplorer=GetModuleHandle(NULL);
						for (int res=0;res<_countof(g_StartButtonOldSizes);res++)
						{
							HRSRC hrSrc=FindResource(hExplorer,MAKEINTRESOURCE(res+FIRST_BUTTON_BITMAP),RT_BITMAP);
							if (hrSrc)
							{
								HGLOBAL hRes=LoadResource(hExplorer,hrSrc);
								if (hRes)
								{
									void *pRes=LockResource(hRes);
									if (pRes)
									{
										DWORD old;
										BITMAPINFOHEADER *pHeader=(BITMAPINFOHEADER*)pRes;
										if (pHeader->biWidth)
										{
											g_StartButtonOldSizes[res]=MAKELONG(pHeader->biWidth,pHeader->biHeight);
											VirtualProtect(pRes,sizeof(BITMAPINFOHEADER),PAGE_READWRITE,&old);
											pHeader->biHeight=pHeader->biWidth=0;
											VirtualProtect(pRes,sizeof(BITMAPINFOHEADER),old,&old);
										}
									}
								}
							}
						}
					}
					SendMessage(taskBar.taskBar,WM_SETTINGCHANGE,0,0);
				}
			}
			else
			{
				if (taskBar.startButton && taskBar.startButton!=g_WinStartButton)
				{
					RevokeDragDrop(taskBar.startButton);
					DestroyStartButton(taskBar.taskbarId);
				}
				taskBar.startButton=taskBar.taskBar==g_TaskBar?g_WinStartButton:NULL;
				taskBar.startButtonSize.cx=taskBar.startButtonSize.cy=0;
				if (g_WinStartButton && g_WinStartButton==taskBar.startButton)
				{
					// restore the bitmap sizes
					HMODULE hExplorer=GetModuleHandle(NULL);
					for (int res=0;res<_countof(g_StartButtonOldSizes);res++)
					{
						HRSRC hrSrc=FindResource(hExplorer,MAKEINTRESOURCE(res+FIRST_BUTTON_BITMAP),RT_BITMAP);
						if (hrSrc)
						{
							HGLOBAL hRes=LoadResource(hExplorer,hrSrc);
							if (hRes)
							{
								void *pRes=LockResource(hRes);
								if (pRes)
								{
									DWORD old;
									BITMAPINFOHEADER *pHeader=(BITMAPINFOHEADER*)pRes;
									if (g_StartButtonOldSizes[res])
									{
										VirtualProtect(pRes,sizeof(BITMAPINFOHEADER),PAGE_READWRITE,&old);
										pHeader->biWidth=LOWORD(g_StartButtonOldSizes[res]);
										pHeader->biHeight=HIWORD(g_StartButtonOldSizes[res]);
										VirtualProtect(pRes,sizeof(BITMAPINFOHEADER),old,&old);
									}
								}
							}
						}
					}
					RemoveWindowSubclass(g_WinStartButton,SubclassWin7StartButton,'CLSH');
					SendMessage(g_WinStartButton,WM_THEMECHANGED,0,0);
					ShowWindow(g_WinStartButton,SW_SHOW);
				}
			}
		}
		else if (update==TASKBAR_RECREATE_BUTTONS && bButton2)
		{
			RecreateStartButton(it->first);
		}
		if (taskBar.bCustomLook!=bCustomLook)
		{
			// set custom look
			taskBar.bCustomLook=bCustomLook;

			if (!bCustomLook && GetWinVersion()<WIN_VER_WIN10)
			{
				DWM_BLURBEHIND blur={DWM_BB_ENABLE,GetWinVersion()<WIN_VER_WIN8};
				DwmEnableBlurBehindWindow(taskBar.taskBar,&blur);
				if (GetWinVersion()==WIN_VER_WIN7)
				{
					MARGINS margins={0};
					DwmExtendFrameIntoClientArea(taskBar.taskBar,&margins);
				}
			}
		}
	}
	for (id_taskbar_map::iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
	{
		TaskbarInfo &taskBar=it->second;
		SendMessage(taskBar.taskBar,WM_SETTINGCHANGE,0,0);
		InvalidateRect(taskBar.taskBar,NULL,TRUE);
		PostMessage(taskBar.taskBar,WM_THEMECHANGED,0,0);
	}
}

///////////////////////////////////////////////////////////////////////////////
// hooks for animating the start button

typedef void (WINAPI *tDwmpBeginTransitionRequest)(int param);
typedef void (WINAPI *tDwmpTransitionWindowWithRects)(HWND,int,RECT*,RECT*,RECT*,RECT*,RECT*);
typedef void (WINAPI *tDwmpEndTransitionRequest)(int param);

static IatHookData *g_DwmpBTRHook, *g_DwmpTWWRHook, *g_DwmpETRHook;

static tDwmpBeginTransitionRequest g_DwmpBeginTransitionRequest;
static tDwmpTransitionWindowWithRects g_DwmpTransitionWindowWithRects;
static tDwmpEndTransitionRequest g_DwmpEndTransitionRequest;

static HWND g_TransitionButton;
static HWND g_TransitionBar;
static RECT g_TransitionClip;
static POINT g_TransitionVector;
static bool g_bTransitionIn;

void WINAPI DwmpBeginTransitionRequest2( int param )
{
	g_TransitionButton=NULL;
	((tDwmpBeginTransitionRequest)g_DwmpBTRHook->oldProc)(param);
}

void WINAPI DwmpTransitionWindowWithRects2( HWND hWnd, int flags, RECT *prcClient1, RECT *prcStart, RECT *prcClient2, RECT *prcEnd, RECT *prcClip )
{
	g_TransitionButton=NULL;
	const TaskbarInfo *taskBar=FindTaskBarInfoBar(hWnd);
	if (taskBar && taskBar->bCustomLook)
	{
		g_TransitionBar=hWnd;
		TTaskbarLook look=(TTaskbarLook)GetSettingInt(L"TaskbarLook");
		DWM_BLURBEHIND blur={DWM_BB_ENABLE,look==TASKBAR_OPAQUE};
		DwmEnableBlurBehindWindow(hWnd,&blur);
		int data[4];
		ComputeTaskbarColors(data);
		WINCOMPATTRDATA attrData={0x13,&data,sizeof(data)};
		SetWindowCompositionAttribute(hWnd,&attrData);
	}
	if (taskBar && taskBar->startButton && prcStart && prcEnd && prcClip)
	{
		g_TransitionButton=taskBar->startButton;
		g_TransitionVector.x=prcEnd->left-prcStart->left;
		g_TransitionVector.y=prcEnd->top-prcStart->top;
		g_TransitionClip=*prcClip;
		g_bTransitionIn=true;
		switch (GetTaskbarPosition(taskBar->taskBar,NULL,NULL,NULL))
		{
			case ABE_LEFT:
				g_bTransitionIn=g_TransitionVector.x>0;
				break;
			case ABE_TOP:
				g_bTransitionIn=g_TransitionVector.y>0;
				break;
			case ABE_RIGHT:
				g_bTransitionIn=g_TransitionVector.x<0;
				break;
			default:
				g_bTransitionIn=g_TransitionVector.y<0;
		}
	}
	((tDwmpTransitionWindowWithRects)g_DwmpTWWRHook->oldProc)(hWnd,flags,prcClient1,prcStart,prcClient2,prcEnd,prcClip);
}

void WINAPI DwmpEndTransitionRequest2( int param )
{
	((tDwmpEndTransitionRequest)g_DwmpETRHook->oldProc)(param);
	if (g_TransitionBar)
	{
		TTaskbarLook look=(TTaskbarLook)GetSettingInt(L"TaskbarLook");
		DWM_BLURBEHIND blur={DWM_BB_ENABLE,look==TASKBAR_OPAQUE};
		DwmEnableBlurBehindWindow(g_TransitionBar,&blur);
		int data[4];
		ComputeTaskbarColors(data);
		WINCOMPATTRDATA attrData={0x13,&data,sizeof(data)};
		SetWindowCompositionAttribute(g_TransitionBar,&attrData);
		g_TransitionBar=NULL;
	}
	if (g_TransitionButton)
	{
		HWND button=g_TransitionButton;
		g_TransitionButton=NULL;
		g_DwmpBeginTransitionRequest(15);
		RECT rcClient;
		GetClientRect(button,&rcClient);
		RECT rcStart, rcEnd;
		if (g_bTransitionIn)
		{
			GetWindowRect(button,&rcEnd);
			rcStart=rcEnd;
			OffsetRect(&rcStart,-g_TransitionVector.x,-g_TransitionVector.y);
		}
		else
		{
			GetWindowRect(button,&rcStart);
			rcEnd=rcStart;
			int dx=0, dy=0;
			if (g_TransitionVector.x<0) // left
			{
				dx=g_TransitionClip.left-rcStart.right;
				if (dx>0) dx=g_TransitionVector.x;
			}
			else if (g_TransitionVector.x>0) // right
			{
				dx=g_TransitionClip.right-rcStart.left;
				if (dx<0) dx=g_TransitionVector.x;
			}
			else if (g_TransitionVector.y<0) // top
			{
				dy=g_TransitionClip.top-rcStart.bottom;
				if (dy>0) dy=g_TransitionVector.y;
			}
			else if (g_TransitionVector.y>0) // bottom
			{
				dy=g_TransitionClip.bottom-rcStart.top;
				if (dy<0) dy=g_TransitionVector.y;
			}
			OffsetRect(&rcEnd,dx,dy);
		}
		g_DwmpTransitionWindowWithRects(button,0x21800046,&rcClient,&rcStart,&rcClient,&rcEnd,&g_TransitionClip);
		g_DwmpEndTransitionRequest(15);
	}
}

///////////////////////////////////////////////////////////////////////////////
// hooks for skinning the taskbar

typedef void (WINAPI *tSHFillRectClr)(HDC hdc, const RECT *pRect, COLORREF color);

static IatHookData *g_SHFillRectClrHook, *g_StretchDIBitsHook;
static IatHookData *g_DrawThemeBackgroundHook, *g_DrawThemeTextHook, *g_DrawThemeTextExHook, *g_DrawThemeTextCtlHook, *g_SetWindowCompositionAttributeHook;

static tSHFillRectClr g_SHFillRectClr;

static void WINAPI SHFillRectClr2( HDC hdc, const RECT *pRect, COLORREF color )
{
	if (!g_CurrentTaskList || !g_TaskbarTexture || GetCurrentThreadId()!=g_TaskbarThreadId)
		g_SHFillRectClr(hdc,pRect,color);
}

static IatHookData* g_ExtTextOutWHook = nullptr;

// used by ExplorerPatcher's custom implementation of `SHFillRectClr`
static BOOL WINAPI ExtTextOutW2(HDC hdc, int X, int Y, UINT fuOptions, const RECT* lprc, LPCWSTR lpString, UINT cbCount, const INT* lpDx)
{
	if (fuOptions != ETO_OPAQUE || lpString || cbCount || lpDx || !g_CurrentTaskList || !g_TaskbarTexture || GetCurrentThreadId() != g_TaskbarThreadId)
		return ExtTextOutW(hdc, X, Y, fuOptions, lprc, lpString, cbCount, lpDx);

	return FALSE;
}

static HRESULT STDAPICALLTYPE DrawThemeBackground2( HTHEME hTheme, HDC hdc, int iPartId, int iStateId, LPCRECT pRect, LPCRECT pClipRect )
{
	if (g_CurrentTaskList && g_TaskbarTexture && iPartId==1 && iStateId==0 && GetCurrentThreadId()==g_TaskbarThreadId)
	{
		HWND taskbar=GetAncestor(g_CurrentTaskList,GA_ROOT);
		RECT rcClient;
		GetClientRect(taskbar,&rcClient);
		MapWindowPoints(taskbar,g_CurrentTaskList,(POINT*)&rcClient,2);
		PrintTaskbarBackground(hdc,rcClient,*pRect,0);
		return S_OK;
	}
	if (g_CurrentDesktopButton && g_TaskbarTexture && GetCurrentThreadId()==g_TaskbarThreadId && hTheme==GetWindowTheme(g_CurrentDesktopButton))
	{
		HWND taskbar=GetAncestor(g_CurrentDesktopButton,GA_ROOT);
		RECT rcClient;
		GetClientRect(taskbar,&rcClient);
		MapWindowPoints(taskbar,g_CurrentDesktopButton,(POINT*)&rcClient,2);
		UINT uEdge=GetTaskbarPosition(taskbar,NULL,NULL,NULL);
		PrintTaskbarBackground(hdc,rcClient,*pRect,uEdge);
		return DrawThemeBackground(hTheme,hdc,iPartId,iStateId,pRect,pClipRect);
	}
	return DrawThemeBackground(hTheme,hdc,iPartId,iStateId,pRect,pClipRect);
}

// toolbar text, rebar band titles, clock
static HRESULT STDAPICALLTYPE DrawThemeText2( HTHEME hTheme, HDC hdc, int iPartId, int iStateId, LPCWSTR pszText, int iCharCount, DWORD dwTextFlags, DWORD dwTextFlags2, LPCRECT pRect )
{
	if ((g_CurrentRebar || g_CurrentTaskbarPart) && GetCurrentThreadId()==g_TaskbarThreadId && GetSettingBool(L"CustomTaskbar"))
	{
		bool bDef;
		COLORREF color=GetSettingInt(L"TaskbarTextColor",bDef)&0xFFFFFF;
		if (!bDef)
		{
			// change the color for the toolbar titles, the toolbar buttons and the clock
			DTTOPTS options={sizeof(options),DTT_TEXTCOLOR};
			options.crText=color;
			return DrawThemeTextEx(hTheme,hdc,iPartId,iStateId,pszText,iCharCount,dwTextFlags,(RECT*)pRect,&options);
		}
	}
	return DrawThemeText(hTheme,hdc,iPartId,iStateId,pszText,iCharCount,dwTextFlags,dwTextFlags2,pRect);
}

// taskbar text
static HRESULT STDAPICALLTYPE DrawThemeTextEx2( HTHEME hTheme, HDC hdc, int iPartId, int iStateId, LPCWSTR pszText, int iCharCount, DWORD dwFlags, LPRECT pRect, const DTTOPTS *pOptions )
{
	if ((g_CurrentTaskList || g_CurrentTaskbarPart) && GetCurrentThreadId()==g_TaskbarThreadId && GetSettingBool(L"CustomTaskbar"))
	{
		bool bDef;
		COLORREF color=GetSettingInt(L"TaskbarTextColor",bDef)&0xFFFFFF;
		if (!bDef)
		{
			// change the color dor the taskbar buttons
			DTTOPTS options=*pOptions;
			options.dwFlags|=DTT_TEXTCOLOR;
			options.crText=color;
			return DrawThemeTextEx(hTheme,hdc,iPartId,iStateId,pszText,iCharCount,dwFlags,pRect,&options);
		}
	}
	return DrawThemeTextEx(hTheme,hdc,iPartId,iStateId,pszText,iCharCount,dwFlags,pRect,pOptions);
}

static BLENDFUNCTION g_AlphaFunc={AC_SRC_OVER,0,255,AC_SRC_ALPHA};

static int WINAPI StretchDIBits2( HDC hdc, int xDest, int yDest, int DestWidth, int DestHeight, int xSrc, int ySrc, int SrcWidth, int SrcHeight, CONST VOID *lpBits, CONST BITMAPINFO *lpbmi, UINT iUsage, DWORD rop )
{
	if ((g_CurrentTaskChevron || g_CurrentTaskbarButton) && g_TaskbarTexture && GetCurrentThreadId()==g_TaskbarThreadId)
	{
		HDC hsrc=CreateCompatibleDC(hdc);
		BITMAPINFO bi={0};
		bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth=DestWidth;
		bi.bmiHeader.biHeight=DestHeight;
		bi.bmiHeader.biPlanes=1;
		bi.bmiHeader.biBitCount=32;
		HBITMAP bitmap=CreateDIBSection(hsrc,&bi,DIB_RGB_COLORS,NULL,NULL,0);
		HGDIOBJ bmp0=SelectObject(hsrc,bitmap);
		int res=StretchDIBits(hsrc,0,0,DestWidth,DestHeight,xSrc,ySrc,SrcWidth,SrcHeight,lpBits,lpbmi,iUsage,SRCCOPY);
		AlphaBlend(hdc,xDest,yDest,DestWidth,DestHeight,hsrc,0,0,DestWidth,DestHeight,g_AlphaFunc);
		SelectObject(hsrc,bmp0);
		DeleteObject(bitmap);
		DeleteDC(hsrc);
		return res;
	}
	return StretchDIBits(hdc,xDest,yDest,DestWidth,DestHeight,xSrc,ySrc,SrcWidth,SrcHeight,lpBits,lpbmi,iUsage,rop);
}

static BOOL WINAPI SetWindowCompositionAttribute2( HWND hwnd, WINCOMPATTRDATA *pAttrData )
{
	if (pAttrData->attribute==0x13 && GetCurrentThreadId()==g_TaskbarThreadId)
	{
		const TaskbarInfo *taskBar=FindTaskBarInfoBar(hwnd);
		if (taskBar && taskBar->bCustomLook)
		{
			int data[4];
			ComputeTaskbarColors(data);
			WINCOMPATTRDATA attrData={0x13,&data,sizeof(data)};
			if (data[0]==3 && taskBar->bThemeChanging)
			{
				// send extra attribute when dealing with glass. without it the image behind the glass may not update when the taskbar is resized
				SetWindowCompositionAttribute(hwnd,pAttrData);
			}
			return SetWindowCompositionAttribute(hwnd,&attrData);
		}
	}
	return SetWindowCompositionAttribute(hwnd,pAttrData);
}

///////////////////////////////////////////////////////////////////////////////
// hooks for preventing shell hotkeys registration on Win11

using ShellRegisterHotKey_t = BOOL(WINAPI*)(HWND, int, UINT, UINT, HWND);

static IatHookData* g_ShellRegisterHotKeyHook;
static ShellRegisterHotKey_t g_ShellRegisterHotKey;

static BOOL WINAPI ShellRegisterHotKeyHook(HWND hWnd, int id, UINT fsModifiers, UINT vk, HWND hWndTarget)
{
	// Win key
	if (fsModifiers == MOD_WIN && vk == 0)
		return FALSE;

	// Ctrl+Esc
	if (fsModifiers == MOD_CONTROL && vk == VK_ESCAPE)
		return FALSE;

	return g_ShellRegisterHotKey(hWnd, id, fsModifiers, vk, hWndTarget);
}

// one-time APC function to unregister shell hotkeys
void NTAPI DisableShellHotkeysFunc(ULONG_PTR Parameter)
{
	UnregisterHotKey(NULL, 1);
	UnregisterHotKey(NULL, 2);
}

///////////////////////////////////////////////////////////////////////////////

static void OpenCortana( void )
{
	if (GetWinVersion()>=WIN_VER_WIN10)
		ShellExecute(NULL,NULL,L"shell:::{2559a1f8-21d7-11d4-bdaf-00c04f60b9f0}",NULL,NULL,SW_SHOWNORMAL);
}

static void InitStartMenuDLL( void )
{
	static bool initCalled = false;
	if (initCalled)
		return;

	initCalled = true;

	LogToFile(STARTUP_LOG, L"StartMenu DLL: InitStartMenuDLL");
	WaitDllInitThread();

	InitializeIatHooks();
	if (IsWin81Update1())
	{
		HMODULE dwm=GetModuleHandle(L"dwmapi.dll");
		if (dwm)
		{
			g_DwmpBeginTransitionRequest=(tDwmpBeginTransitionRequest)GetProcAddress(dwm,MAKEINTRESOURCEA(138));
			g_DwmpTransitionWindowWithRects=(tDwmpTransitionWindowWithRects)GetProcAddress(dwm,MAKEINTRESOURCEA(141));
			g_DwmpEndTransitionRequest=(tDwmpEndTransitionRequest)GetProcAddress(dwm,MAKEINTRESOURCEA(140));
			if (g_DwmpBeginTransitionRequest && g_DwmpTransitionWindowWithRects && g_DwmpEndTransitionRequest)
			{
				g_DwmpBTRHook=SetIatHook(GetModuleHandle(NULL),"dwmapi.dll",MAKEINTRESOURCEA(138),DwmpBeginTransitionRequest2);
				g_DwmpTWWRHook=SetIatHook(GetModuleHandle(NULL),"dwmapi.dll",MAKEINTRESOURCEA(141),DwmpTransitionWindowWithRects2);
				g_DwmpETRHook=SetIatHook(GetModuleHandle(NULL),"dwmapi.dll",MAKEINTRESOURCEA(140),DwmpEndTransitionRequest2);
				if (!g_DwmpBTRHook || !g_DwmpTWWRHook || !g_DwmpETRHook)
				{
					ClearIatHook(g_DwmpBTRHook);
					g_DwmpBTRHook=NULL;
					ClearIatHook(g_DwmpTWWRHook);
					g_DwmpTWWRHook=NULL;
					ClearIatHook(g_DwmpETRHook);
					g_DwmpETRHook=NULL;
				}
			}
		}
	}

	if (GetSettingBool(L"CustomTaskbar"))
	{
		auto module=GetModuleHandle(L"taskbar.dll");
		if (!module)
		{
			module = GetModuleHandle(L"ep_taskbar.5.dll");
			if (!module)
				module = GetModuleHandle(L"ep_taskbar.2.dll");

			if (module)
				g_epTaskbar = true;
		}
		if (!module)
			module=GetModuleHandle(NULL);

		if (GetWinVersion()>=WIN_VER_WIN10)
		{
			HMODULE shlwapi=GetModuleHandle(L"shlwapi.dll");
			if (shlwapi)
			{
				g_SHFillRectClr=(tSHFillRectClr)GetProcAddress(shlwapi,MAKEINTRESOURCEA(197));
				if (g_SHFillRectClr)
				{
					g_SHFillRectClrHook=SetIatHook(module,"shlwapi.dll",MAKEINTRESOURCEA(197),SHFillRectClr2);
					if (!g_SHFillRectClrHook)
						g_SHFillRectClrHook=SetIatHook(module,"api-ms-win-shlwapi-winrt-storage-l1-1-1.dll",MAKEINTRESOURCEA(197),SHFillRectClr2);
				}
			}
			g_StretchDIBitsHook=SetIatHook(module,"gdi32.dll","StretchDIBits",StretchDIBits2);
			if (!g_StretchDIBitsHook)
				g_StretchDIBitsHook=SetIatHook(module,"ext-ms-win-gdi-draw-l1-1-0.dll","StretchDIBits",StretchDIBits2);

			// ExplorerPatcher compatibility
			if (g_epTaskbar)
				g_ExtTextOutWHook = SetIatHook(module, "gdi32.dll", "ExtTextOutW", ExtTextOutW2);
		}

		{
			HWND dlg=CreateWindow(L"#32770",L"",WS_POPUP,0,0,0,0,NULL,0,0,0);
			HWND toolbar=CreateWindow(TOOLBARCLASSNAME,L"",WS_CHILD|TBS_TOOLTIPS,0,0,0,0,dlg,0,0,0);
			DestroyWindow(dlg);
		}

		if (GetWinVersion()<=WIN_VER_WIN81)
			g_DrawThemeBackgroundHook=SetIatHook(module,"uxtheme.dll","DrawThemeBackground",DrawThemeBackground2);
		g_DrawThemeTextHook=SetIatHook(module,"uxtheme.dll","DrawThemeText",DrawThemeText2);
		g_DrawThemeTextExHook=SetIatHook(module,"uxtheme.dll","DrawThemeTextEx",DrawThemeTextEx2);
		g_DrawThemeTextCtlHook=SetIatHook(GetModuleHandle(L"comctl32.dll"),"uxtheme.dll","DrawThemeText",DrawThemeText2);
		if (GetWinVersion()>=WIN_VER_WIN10)
			g_SetWindowCompositionAttributeHook=SetIatHook(module,"user32.dll","SetWindowCompositionAttribute",SetWindowCompositionAttribute2);
	}

	g_TaskbarThreadId=GetCurrentThreadId();
	g_bTrimHooks=GetWinVersion()==WIN_VER_WIN7 && (GetSettingInt(L"CompatibilityFixes")&COMPATIBILITY_TRIM_HOOKS);
	InitManagers(false);
	int level=GetSettingInt(L"CrashDump");
	if (level>=1 && level<=3)
	{
		if (level==1) MiniDumpType=MiniDumpNormal;
		if (level==2) MiniDumpType=MiniDumpWithDataSegs;
		if (level==3) MiniDumpType=MiniDumpWithFullMemory;
		SetUnhandledExceptionFilter(TopLevelFilter);
		_set_invalid_parameter_handler(InvalidParameterHandler);
		g_bCrashDump=true;
	}
	FindTaskBar();
	g_ProgWin=FindWindowEx(NULL,NULL,L"Progman",NULL);
	DWORD progThread=GetWindowThreadProcessId(g_ProgWin,NULL);
	g_ProgHook=SetWindowsHookEx(WH_GETMESSAGE,HookProgManThread,NULL,progThread);
	g_StartHook=SetWindowsHookEx(WH_GETMESSAGE,HookDesktopThread,NULL,GetCurrentThreadId());
	if (IsWin11())
	{
		g_StartMouseHook=SetWindowsHookEx(WH_MOUSE,HookDesktopThreadMouse,NULL,GetCurrentThreadId());

		// hook ShellRegisterHotKey to prevent twinui.dll to install shell hotkeys (Win, Ctrl+Esc)
		// without these hotkeys there is standard WM_SYSCOMMAND+SC_TASKLIST sent when start menu is invoked by keyboard shortcut
		g_ShellRegisterHotKey = (ShellRegisterHotKey_t)GetProcAddress(GetModuleHandle(L"user32.dll"), MAKEINTRESOURCEA(2671));
		auto twinui = GetModuleHandle(L"twinui.dll");

		if (g_ShellRegisterHotKey && twinui)
		{
			g_ShellRegisterHotKeyHook = SetIatHook(twinui, "user32.dll" ,MAKEINTRESOURCEA(2671), ShellRegisterHotKeyHook);

			// unregister shell hotkeys as they may be registered already
			// this has to be done from context of thread that registered them
			auto hwnd = FindWindow(L"ApplicationManager_ImmersiveShellWindow", NULL);
			if (hwnd)
			{
				auto thread = OpenThread(THREAD_SET_CONTEXT, FALSE, GetWindowThreadProcessId(hwnd, NULL));
				if (thread)
				{
					QueueUserAPC(DisableShellHotkeysFunc, thread, 0);
					CloseHandle(thread);
				}
			}
		}
	}

	HWND hwnd=FindWindow(L"OpenShellMenu.CStartHookWindow",L"StartHookWindow");
	LoadLibrary(L"StartMenuDLL.dll"); // keep the DLL from unloading
	if (hwnd) PostMessage(hwnd,WM_CLEAR,0,0); // tell the exe to unhook this hook
	if (GetWinVersion()>=WIN_VER_WIN8)
	{
		SetWindowCompositionAttribute=(tSetWindowCompositionAttribute)GetProcAddress(GetModuleHandle(L"user32.dll"),"SetWindowCompositionAttribute");
	}
	int taskbarId=g_NextTaskbar++;
	TaskbarInfo &taskBar=g_TaskbarInfos[taskbarId];
	taskBar.taskBar=g_TaskBar;
	taskBar.taskbarId=taskbarId;
	taskBar.rebar=FindWindowEx(g_TaskBar,NULL,REBARCLASSNAME,NULL);
	if (taskBar.rebar)
	{
		SetWindowSubclass(taskBar.rebar,SubclassRebarProc,'CLSH',taskbarId);
		// TaskBand window
		HWND hwnd=FindWindowEx(taskBar.rebar,NULL,L"MSTaskSwWClass",NULL);
		if (hwnd)
		{
			taskBar.taskList=hwnd;
			// TaskList window
			// it has to be visible, otherwise it won't receive WM_PAINT that we need to intercept
			// in such case we will intercept parent instead
			hwnd=FindWindowEx(hwnd,NULL,L"MSTaskListWClass",NULL);
			if (hwnd&&IsWindowVisible(hwnd))
				taskBar.taskList=hwnd;
		}
		if (taskBar.taskList)
			SetWindowSubclass(taskBar.taskList,SubclassTaskListProc,'CLSH',taskbarId);
	}
	if (GetWinVersion()>WIN_VER_WIN8)
	{
		taskBar.oldButton=FindWindowEx(taskBar.taskBar,NULL,L"Start",NULL);
		if (taskBar.oldButton)
		{
			if (GetWinVersion()>=WIN_VER_WIN10)
			{
				taskBar.pOriginalTarget=(IDropTarget*)GetProp(taskBar.oldButton,L"OleDropTargetInterface");
				if (taskBar.pOriginalTarget)
					RevokeDragDrop(taskBar.oldButton);
			}

			CStartMenuTarget *pNewTarget=new CStartMenuTarget(taskBar.taskbarId);
			RegisterDragDrop(taskBar.oldButton,pNewTarget);
			pNewTarget->Release();
			if (GetWinVersion()<WIN_VER_WIN10 && GetTaskbarPosition(taskBar.taskBar,NULL,NULL,NULL)==ABE_BOTTOM)
				SetWindowPos(taskBar.oldButton,NULL,-1,0,0,0,SWP_NOSIZE|SWP_NOZORDER);
			SetWindowSubclass(taskBar.oldButton,SubclassWin81StartButton,'CLSH',taskBar.taskbarId);
		}
	}
	if (GetWinVersion()>=WIN_VER_WIN10)
	{
		for (HWND button=FindWindowEx(g_TaskBar,NULL,L"TrayButton",NULL);button;button=FindWindowEx(g_TaskBar,button,L"TrayButton",NULL))
		{
			taskBar.trayButtons.push_back(button);
			SetWindowSubclass(button,SubclassTrayButtonProc,'CLSH',taskBar.taskbarId);
		}
		HWND search=FindWindowEx(g_TaskBar,NULL,L"TrayDummySearchControl",NULL);
		if (search)
		{
			taskBar.trayButtons.push_back(search);
			SetWindowSubclass(search,SubclassTrayButtonProc,'CLSH',taskBar.taskbarId);
		}
		HWND tray=FindWindowEx(g_TaskBar,NULL,L"TrayNotifyWnd",NULL);
		if (tray)
			taskBar.chevron=FindWindowEx(tray,NULL,L"Button",NULL);
		if (taskBar.chevron)
			SetWindowSubclass(taskBar.chevron,SubclassTrayChevronProc,'CLSH',taskBar.taskbarId);
		taskBar.news=FindWindowEx(g_TaskBar,NULL,L"DynamicContent2",NULL);
		if (taskBar.news)
			SetWindowSubclass(taskBar.news,SubclassTrayChevronProc,'CLSH',taskBar.taskbarId);
	}

	HandleTaskbarParts(taskBar,true);
if (!g_bTrimHooks)
	SetWindowSubclass(taskBar.taskBar,SubclassTaskBarProc,'CLSH',taskbarId);
	taskBar.startButton=g_WinStartButton;

#ifdef HOOK_DROPTARGET
	if (g_WinStartButton)
	{
		g_pOriginalTarget=(IDropTarget*)GetProp(g_WinStartButton,L"OleDropTargetInterface");
		if (g_pOriginalTarget)
			RevokeDragDrop(g_WinStartButton);

		CStartMenuTarget *pNewTarget=new CStartMenuTarget(taskbarId);
		RegisterDragDrop(g_WinStartButton,pNewTarget);
		pNewTarget->Release();
	}
#endif

	if (GetWinVersion()>=WIN_VER_WIN8)
	{
		g_pAppVisibility.CoCreateInstance(CLSID_MetroMode);
		if (g_pAppVisibility)
		{
			CMonitorModeEvents *monitor=new CMonitorModeEvents();
			g_pAppVisibility->Advise(monitor,&g_AppVisibilityMonitorCookie);
			monitor->Release();
		}
		if (GetWinVersion()<WIN_VER_WIN10)
		{
			HWND hwndAppManager=FindWindow(L"ApplicationManager_DesktopShellWindow",NULL);
			if (hwndAppManager)
			{
				g_AppManagerThread=GetWindowThreadProcessId(hwndAppManager,NULL);
				g_AppManagerHook=SetWindowsHookEx(WH_GETMESSAGE,HookAppManager,g_Instance,g_AppManagerThread);
			}
			if (GetWinVersion()==WIN_VER_WIN8 && GetSettingBool(L"SkipMetro"))
			{
				g_SkipMetroCount=abs(GetSettingInt(L"SkipMetroCount"));
				SetTimer(g_TaskBar,'CLSM',500,NULL);
				PostMessage(g_TaskBar,WM_TIMER,'CLSM',0);
			}
		}

		EnumWindows(HookAllTaskbarsEnum,0);
	}
	g_NewWindowHook=SetWindowsHookEx(WH_CBT,HookNewWindow,g_Instance,GetCurrentThreadId());

	UpdateTaskBars(TASKBAR_RECREATE_BUTTONS);
	UpdateTaskBars(TASKBAR_UPDATE_TEXTURE);
}

static void RecreateStartButton( size_t taskbarId )
{
	for (id_taskbar_map::iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
	{
		TaskbarInfo &taskBar=it->second;
		if (taskbarId>=0 && taskBar.taskbarId!=taskbarId)
			continue;
		if (taskBar.bRecreatingButton)
			continue;

		taskBar.bRecreatingButton=true;
		{
			if (taskBar.startButton && taskBar.startButton!=g_WinStartButton)
			{
				RevokeDragDrop(taskBar.startButton);
				DestroyStartButton(taskBar.taskbarId);
			}
			taskBar.startButton=CreateStartButton(taskBar.taskbarId,taskBar.taskBar,taskBar.rebar);
			CStartMenuTarget *pNewTarget=new CStartMenuTarget(taskBar.taskbarId);
			RegisterDragDrop(taskBar.startButton,pNewTarget);
			pNewTarget->Release();
		}
		taskBar.bRecreatingButton=false;

		taskBar.startButtonSize=GetStartButtonSize(taskBar.taskbarId);
		if (taskBar.oldButton)
		{
			RECT rc;
			GetWindowRect(taskBar.oldButton,&rc);
			taskBar.oldButtonSize.cx=rc.right-rc.left;
			taskBar.oldButtonSize.cy=rc.bottom-rc.top;
		}

		RECT rcTask;
		GetWindowRect(taskBar.taskBar,&rcTask);
		PostMessage(taskBar.taskBar,WM_SIZE,SIZE_RESTORED,MAKELONG(rcTask.right-rcTask.left,rcTask.bottom-rcTask.top));
		if (taskBar.taskBar==g_TaskBar)
		{
			for (auto btn : taskBar.trayButtons)
			{
				RECT rc;
				GetWindowRect(btn,&rc);
				MapWindowPoints(NULL,taskBar.taskBar,(POINT*)&rc,2); // convert to taskbar coordinates
				SetWindowPos(btn,HWND_TOP,rc.left,rc.top,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER);
			}
		}
	}
}

static DWORD WINAPI ExitThreadProc( void *param )
{
	Sleep(1000); // wait a second! hopefully by then the hooks will be finished and no more of our code will be executing
	// send WM_CLOSE to the window in StartMenu.exe to close that process
	if (param) PostMessage((HWND)param,WM_CLOSE,0,0);
	FreeLibraryAndExitThread(g_Instance,0);
}

static void CleanStartMenuDLL( void )
{
	ClearIatHook(g_DwmpBTRHook);
	g_DwmpBTRHook=NULL;
	ClearIatHook(g_DwmpTWWRHook);
	g_DwmpTWWRHook=NULL;
	ClearIatHook(g_DwmpETRHook);
	g_DwmpETRHook=NULL;
	ClearIatHook(g_SHFillRectClrHook);
	g_SHFillRectClrHook=NULL;
	ClearIatHook(g_StretchDIBitsHook);
	g_StretchDIBitsHook=NULL;
	ClearIatHook(g_ExtTextOutWHook);
	g_ExtTextOutWHook = NULL;

	ClearIatHook(g_DrawThemeBackgroundHook);
	g_DrawThemeBackgroundHook=NULL;
	ClearIatHook(g_DrawThemeTextHook);
	g_DrawThemeTextHook=NULL;
	ClearIatHook(g_DrawThemeTextExHook);
	g_DrawThemeTextExHook=NULL;
	ClearIatHook(g_DrawThemeTextCtlHook);
	g_DrawThemeTextCtlHook=NULL;
	ClearIatHook(g_SetWindowCompositionAttributeHook);
	g_SetWindowCompositionAttributeHook=NULL;
	ClearIatHook(g_ShellRegisterHotKeyHook);
	g_ShellRegisterHotKeyHook=NULL;

	CloseManagers(false);
	ClearIatHooks();

	// cleanup
	if (g_Owner.m_hWnd) g_Owner.DestroyWindow();
	CloseSettings();
	CMenuContainer::CloseStartMenu();
	CMenuFader::ClearAll();
	UnhookDropTarget();
	EnableHotkeys(HOTKEYS_CLEAR);
	HWND hwnd=FindWindow(L"OpenShellMenu.CStartHookWindow",L"StartHookWindow");
	UnhookWindowsHookEx(g_ProgHook);
	UnhookWindowsHookEx(g_StartHook);
	if (g_StartMouseHook) UnhookWindowsHookEx(g_StartMouseHook);
	g_StartMouseHook=NULL;
	if (g_AppManagerHook) UnhookWindowsHookEx(g_AppManagerHook);
	g_AppManagerHook=NULL;
	if (g_NewWindowHook) UnhookWindowsHookEx(g_NewWindowHook);
	g_NewWindowHook=NULL;
	if (g_pAppVisibility)
	{
		g_pAppVisibility->Unadvise(g_AppVisibilityMonitorCookie);
		g_pAppVisibility=NULL;
	}
	ResetHotCorners();
	UpdateTaskBars(TASKBAR_CLEAR);
	g_WinStartButton=NULL;
	for (id_taskbar_map::const_iterator it=g_TaskbarInfos.begin();it!=g_TaskbarInfos.end();++it)
	{
		if (it->second.rebar)
			RemoveWindowSubclass(it->second.rebar,SubclassRebarProc,'CLSH');
		if (it->second.taskList)
			RemoveWindowSubclass(it->second.taskList,SubclassTaskListProc,'CLSH');
		if (it->second.oldButton)
		{
			RemoveWindowSubclass(it->second.oldButton,SubclassWin81StartButton,'CLSH');
			if (GetWinVersion()<WIN_VER_WIN10 && GetTaskbarPosition(it->second.taskBar,NULL,NULL,NULL)==ABE_BOTTOM)
				SetWindowPos(it->second.oldButton,NULL,0,0,0,0,SWP_NOSIZE|SWP_NOZORDER);
			RevokeDragDrop(it->second.oldButton);
			if (it->second.pOriginalTarget)
				RegisterDragDrop(it->second.oldButton,it->second.pOriginalTarget);
		}
if (!g_bTrimHooks)
		RemoveWindowSubclass(it->second.taskBar,SubclassTaskBarProc,'CLSH');
		for (std::vector<HWND>::const_iterator it2=it->second.trayButtons.begin();it2!=it->second.trayButtons.end();++it2)
		{
			RemoveWindowSubclass(*it2,SubclassTrayButtonProc,'CLSH');
		}
		for (std::vector<HWND>::const_iterator it2=it->second.taskbarParts.begin();it2!=it->second.taskbarParts.end();++it2)
		{
			RemoveWindowSubclass(*it2,SubclassTaskbarPartProc,'CLSH');
		}
		if (it->second.chevron)
			RemoveWindowSubclass(it->second.chevron,SubclassTrayChevronProc,'CLSH');
		if (it->second.news)
			RemoveWindowSubclass(it->second.news,SubclassTrayChevronProc,'CLSH');
		if (it->second.desktop)
			RemoveWindowSubclass(it->second.desktop,SubclassDesktopButtonProc,'CLSH');
		if (it->second.bTimer)
			KillTimer(it->second.startButton,'CLSM');
		RECT rcTask;
		GetWindowRect(it->second.taskBar,&rcTask);
		PostMessage(it->second.taskBar,WM_SIZE,SIZE_RESTORED,MAKELONG(rcTask.right-rcTask.left,rcTask.bottom-rcTask.top));
		PostMessage(it->second.taskBar,WM_THEMECHANGED,0,0);
	}
	g_TaskbarInfos.clear();
	if (g_TopWin7Menu)
	{
		RemoveWindowSubclass(g_UserPic,SubclassUserPicProc,'CLSH');
		RemoveWindowSubclass(g_TopWin7Menu,SubclassTopMenuProc,'CLSH');
		RemoveWindowSubclass(g_AllPrograms,SubclassProgramsProc,'CLSH');
	}
	if (g_bCrashDump)
	{
		SetUnhandledExceptionFilter(NULL);
		g_bCrashDump=false;
	}

	// we need to unload the DLL here. but we can't just call FreeLibrary because it will unload the code
	// while it is still executing. So we create a separate thread and use FreeLibraryAndExitThread
	CreateThread(NULL,0,ExitThreadProc,(void*)hwnd,0,NULL);
}

///////////////////////////////////////////////////////////////////////////////


static BOOL CALLBACK FindImmersiveWindows( HWND hwnd, LPARAM lParam )
{
	wchar_t name[100];
	GetClassName(hwnd,name,_countof(name));
	if (wcscmp(name,L"ImmersiveLauncher")==0)
		((HWND*)lParam)[0]=hwnd;
	if (wcscmp(name,L"ImmersiveBackgroundWindow")==0)
		((HWND*)lParam)[1]=hwnd;
	if (wcscmp(name,L"SearchPane")==0)
		((HWND*)lParam)[2]=hwnd;
	return TRUE;
}

static bool WindowsMenuOpened( void )
{
	FindWindowsMenu();
	CComPtr<IUnknown> pImmersiveShell;
	if (GetWinVersion()>=WIN_VER_WIN10 && CreateImmersiveShell(pImmersiveShell))
	{
		{
			CComPtr<IImmersiveLauncher81> pLauncher;
			IUnknown_QueryService(pImmersiveShell,SID_ImmersiveLauncher,IID_IImmersiveLauncher81,(void**)&pLauncher);
			BOOL bIsVisible;
			if (pLauncher && SUCCEEDED(pLauncher->IsVisible(&bIsVisible)))
				return bIsVisible!=0;
		}
		{
			CComPtr<IImmersiveLauncher10RS> pLauncher;
			IUnknown_QueryService(pImmersiveShell,SID_ImmersiveLauncher,IID_IImmersiveLauncher10RS,(void**)&pLauncher);
			BOOL bIsVisible;
			if (pLauncher && SUCCEEDED(pLauncher->IsVisible(&bIsVisible)))
				return bIsVisible!=0;
		}
	}
	if (GetWinVersion()>=WIN_VER_WIN8)
	{
		return GetMetroMode(NULL)!=METRO_NONE;
	}
	else
	{
		return g_TopWin7Menu && IsWindowVisible(g_TopWin7Menu);
	}
}

static void OpenStartScreen( HMONITOR monitor )
{
	CComPtr<IUnknown> pImmersiveShell;
	if (CreateImmersiveShell(pImmersiveShell))
	{
		CComPtr<IUnknown> pMonitor;
		if (GetWinVersion()==WIN_VER_WIN8)
		{
			if (monitor)
			{
				CComPtr<IImmersiveMonitorService> pMonitorService;
				IUnknown_QueryService(pImmersiveShell,SID_IImmersiveMonitorService,IID_IImmersiveMonitorService,(void**)&pMonitorService);
				if (pMonitorService)
				{
					CComPtr<IUnknown> pMonitor;
					pMonitorService->GetFromHandle(monitor,&pMonitor);
					if (pMonitor)
						pMonitorService->SetImmersiveMonitor(pMonitor);
				}
			}
			CComPtr<IImmersiveLauncher80> pLauncher;
			IUnknown_QueryService(pImmersiveShell,SID_ImmersiveLauncher,IID_IImmersiveLauncher80,(void**)&pLauncher);
			if (pLauncher)
				pLauncher->ShowStartView(5);
			return;
		}
		if (monitor)
		{
			CComPtr<IImmersiveMonitorService> pMonitorService;
			IUnknown_QueryService(pImmersiveShell,SID_IImmersiveMonitorService,IID_IImmersiveMonitorService,(void**)&pMonitorService);
			if (pMonitorService)
				pMonitorService->GetFromHandle(monitor,&pMonitor);
		}
		{
			CComPtr<IImmersiveLauncher81> pLauncher;
			IUnknown_QueryService(pImmersiveShell,SID_ImmersiveLauncher,IID_IImmersiveLauncher81,(void**)&pLauncher);
			if (pLauncher)
			{
				if (pMonitor)
					pLauncher->ConnectToMonitor(pMonitor);
				HRESULT hr=pLauncher->ShowStartView(GetWinVersion()>=WIN_VER_WIN10?11:5,0);
				return;
			}
		}
		{
			CComPtr<IImmersiveLauncher10RS> pLauncher;
			IUnknown_QueryService(pImmersiveShell,SID_ImmersiveLauncher,IID_IImmersiveLauncher10RS,(void**)&pLauncher);
			if (pLauncher)
			{
				if (pMonitor)
					pLauncher->ConnectToMonitor(pMonitor);
				HRESULT hr=pLauncher->ShowStartView(GetWinVersion()>=WIN_VER_WIN10?11:5,0);
				return;
			}
		}
	}
	else if (g_AppManagerThread)
		PostThreadMessage(g_AppManagerThread,g_StartMenuMsg,MSG_SHIFTWIN,(LPARAM)monitor);
}

// WH_GETMESSAGE hook for the Progman window
static LRESULT CALLBACK HookProgManThread( int code, WPARAM wParam, LPARAM lParam )
{
	if (code==HC_ACTION && wParam)
	{
		MSG *msg=(MSG*)lParam;
		if (msg->message==WM_SYSCOMMAND && (msg->wParam&0xFFF0)==SC_TASKLIST)
		{
			if (GetWinVersion()<WIN_VER_WIN8 && !CMenuContainer::CanShowMenu())
				msg->message=WM_NULL;
			// Win button pressed
			if (msg->lParam=='WSMK' || msg->lParam=='WSMM' || msg->lParam=='WSMH')
			{
				if ((g_AppManagerThread || GetWinVersion()>=WIN_VER_WIN10) && (msg->lParam=='WSMM' || msg->lParam=='WSMH' || (g_TaskbarInfos.size()>1 && GetSettingBool(L"OpenMouseMonitor"))))
				{
					if (!WindowsMenuOpened())
					{
						HMONITOR monitor=msg->lParam=='WSMH'?g_WSMHMonitor:MonitorFromPoint(CPoint(GetMessagePos()),MONITOR_DEFAULTTONULL);
						OpenStartScreen(monitor);
						msg->message=WM_NULL;
					}
				}
			}
			else if (msg->lParam=='CSM')
			{
				msg->message=WM_NULL;
				PostMessage(g_TaskBar,g_StartMenuMsg,MSG_TOGGLE,0);
			}
			else
			{
				FindTaskBar();
				int control=GetSettingInt(L"WinKey");
				if (control==OPEN_BOTH)
				{
					if (GetWinVersion()>=WIN_VER_WIN10)
						control=GetWin10TabletMode()?OPEN_WINDOWS:OPEN_CLASSIC;
					else
						control=GetMetroMode(MonitorFromPoint(CPoint(GetMessagePos()),MONITOR_DEFAULTTONEAREST))?OPEN_WINDOWS:OPEN_CLASSIC;
				}
				if (control==OPEN_DESKTOP)
				{
					TMetroMode metro=GetMetroMode(MonitorFromPoint(CPoint(GetMessagePos()),MONITOR_DEFAULTTONEAREST));
					if (metro==METRO_NONE)
						control=OPEN_CLASSIC;
					else if (metro==METRO_APP)
						control=OPEN_WINDOWS;
					else
					{
						msg->message=WM_NULL;
						SetForegroundWindow(GetDefaultTaskbarInfo()->taskBar);
					}
				}

				if (control==OPEN_WINDOWS)
				{
					FindWindowsMenu();
					if (g_TopWin7Menu && WindowsMenuOpened())
					{
						const TaskbarInfo *taskBar=GetDefaultTaskbarInfo();
						SetForegroundWindow(taskBar->startButton?taskBar->startButton:taskBar->taskBar);
						msg->message=WM_NULL;
					}
					else if (GetWinVersion()>=WIN_VER_WIN8 && g_TaskbarInfos.size()>1 && GetSettingBool(L"OpenMouseMonitor") && !WindowsMenuOpened())
					{
						HMONITOR monitor=MonitorFromPoint(CPoint(GetMessagePos()),MONITOR_DEFAULTTONULL);
						OpenStartScreen(monitor);
						msg->message=WM_NULL;
					}
					else
					{
						PostMessage(g_TaskBar,g_StartMenuMsg,MSG_NOP,0);
					}
				}
				else
				{
					msg->message=WM_NULL;
					if (control==OPEN_CLASSIC)
						PostMessage(g_TaskBar,g_StartMenuMsg,MSG_TOGGLE,0);
					else if (control==OPEN_CUSTOM)
					{
						CString commandText=GetSettingString(L"WinKeyCommand");
						if (!commandText.IsEmpty())
						{
							wchar_t expandedCommand[_MAX_PATH]{};
							::ExpandEnvironmentStrings(commandText, expandedCommand, _countof(expandedCommand));
							ShellExecute(NULL,NULL,expandedCommand,NULL,NULL,SW_SHOWNORMAL);
						}
					}
				}
			}
		}
		if ((msg->message==WM_MOUSEMOVE || msg->message==WM_LBUTTONDOWN) && GetWinVersion()>=WIN_VER_WIN8 && HIWORD(msg->lParam)<10 && GetSettingInt(L"DisableHotCorner")==2)
		{
			if (msg->hwnd!=g_TopDesktopBar || !g_TopDesktopBar || !IsWindow(g_TopDesktopBar))
			{
				wchar_t name[100];
				if (!GetClassName(msg->hwnd,name,_countof(name)) || _wcsicmp(name,L"WorkerW")!=0)
					return CallNextHookEx(NULL,code,wParam,lParam);
				HWND parent=GetParent(msg->hwnd);
				if (parent!=g_ProgWin && !FindWindowEx(parent,NULL,L"SHELLDLL_DefView",NULL))
					return CallNextHookEx(NULL,code,wParam,lParam);
				g_EdgeWindows.erase(g_TopDesktopBar);
				g_TopDesktopBar=msg->hwnd;
			}
			g_EdgeWindows.insert(g_TopDesktopBar);
			ShowWindow(g_TopDesktopBar,SW_HIDE);
			msg->message=WM_NULL;
		}
	}
	return CallNextHookEx(NULL,code,wParam,lParam);
}

// WH_MOUSE hook for taskbar thread (Win11+)
static LRESULT CALLBACK HookDesktopThreadMouse(int code, WPARAM wParam, LPARAM lParam)
{
	if (code == HC_ACTION)
	{
		// we need to steal mouse messages that are issues in start button area
		// so that they won't get to XAML framework that is handling original start button
		auto info = (const MOUSEHOOKSTRUCT*)lParam;
		{
			auto taskBar = FindTaskBarInfoButton(info->hwnd); // click on start button
			if (!taskBar)
			{
				taskBar = FindTaskBarInfoBar(GetAncestor(info->hwnd, GA_ROOT)); // click on taskbar
				if (taskBar && !PointAroundStartButton(taskBar->taskbarId))
					taskBar = NULL;
			}

			if (taskBar && (info->hwnd != taskBar->startButton) && taskBar->oldButton)
			{
				// steal messages from other than our custom button window
				PostMessage(taskBar->oldButton, (UINT)wParam, 0, MAKELPARAM(info->pt.x, info->pt.y));
				return 1;
			}
		}
	}

	return CallNextHookEx(NULL, code, wParam, lParam);
}

// WH_GETMESSAGE hook for the taskbar thread
static LRESULT CALLBACK HookDesktopThread( int code, WPARAM wParam, LPARAM lParam )
{
	if (code==HC_ACTION && wParam && !g_bInMenu)
	{
		MSG *msg=(MSG*)lParam;
		FindTaskBar();
		if (IsSettingsMessage(msg))
		{
			msg->message=WM_NULL;
			return 0;
		}
if (!g_bTrimHooks)
{
		if (((msg->message>=WM_MOUSEFIRST && msg->message<=WM_MOUSELAST) || msg->message==WM_MOUSEHOVER || msg->message==WM_MOUSELEAVE) && GetWinVersion()<=WIN_VER_WIN7 && CMenuContainer::ProcessMouseMessage(msg->hwnd,msg->message,msg->wParam,msg->lParam))
		{
			msg->message=WM_NULL;
			return 0;
		}
}
		if (msg->message==g_StartMenuMsg && msg->hwnd==g_TaskBar)
		{
			msg->message=WM_NULL;
			static bool bProcessing; // prevent reentry
			if (!bProcessing)
			{
				FindWindowsMenu();
				bProcessing=true;
				if (msg->wParam==MSG_TOGGLE || (msg->wParam==MSG_OPEN && !CMenuContainer::IsMenuOpened()))
				{
					const TaskbarInfo *taskBar=GetDefaultTaskbarInfo();
					ToggleStartMenu(taskBar->taskbarId,true);
				}
				else if (msg->wParam==MSG_TOGGLENEW)
				{
					PostMessage(g_ProgWin,WM_SYSCOMMAND,SC_TASKLIST,'WSMK');
				}
				else if (msg->wParam==MSG_SETTINGS)
				{
					if (GetSettingBool(L"EnableSettings"))
						EditSettings(false,0);
				}
				else if (msg->wParam==MSG_SHIFTWIN)
				{
					const TaskbarInfo *taskBar=GetDefaultTaskbarInfo();
					int control=GetSettingInt(L"ShiftWin");
					if (control==OPEN_BOTH)
					{
						if (GetWinVersion()>=WIN_VER_WIN10)
							control=GetWin10TabletMode()?OPEN_WINDOWS:OPEN_CLASSIC;
						else
							control=GetMetroMode(MonitorFromPoint(CPoint(GetMessagePos()),MONITOR_DEFAULTTONEAREST))?OPEN_WINDOWS:OPEN_CLASSIC;
					}
					if (control==OPEN_CLASSIC)
						ToggleStartMenu(taskBar->taskbarId,true);
					else if (control==OPEN_WINDOWS)
						PostMessage(g_ProgWin,WM_SYSCOMMAND,SC_TASKLIST,'WSMK');
					else if (control==OPEN_CORTANA)
						OpenCortana();
					else if (control==OPEN_CUSTOM)
					{
						CString commandText=GetSettingString(L"ShiftWinCommand");
						if (!commandText.IsEmpty())
						{
							wchar_t expandedCommand[_MAX_PATH]{};
							::ExpandEnvironmentStrings(commandText, expandedCommand, _countof(expandedCommand));
							ShellExecute(NULL,NULL,expandedCommand,NULL,NULL,SW_SHOWNORMAL);
						}
					}
				}
				else if (msg->wParam==MSG_DRAG || msg->wParam==MSG_SHIFTDRAG)
				{
					const TaskbarInfo *taskBar=GetTaskbarInfo((int)msg->lParam);
					if (taskBar)
					{
						int control=GetSettingInt((msg->wParam==MSG_DRAG)?L"MouseClick":L"ShiftClick");
						if (control==OPEN_BOTH && GetWinVersion()>=WIN_VER_WIN10)
							control=GetWin10TabletMode()?OPEN_WINDOWS:OPEN_CLASSIC;
						if (control==OPEN_CLASSIC || (control==OPEN_WINDOWS && GetWinVersion()>=WIN_VER_WIN8))
							ToggleStartMenu(taskBar->taskbarId,true);
						else if (control==OPEN_WINDOWS)
							PostMessage(g_ProgWin,WM_SYSCOMMAND,SC_TASKLIST,'WSMM');
					}
				}
				else if (msg->wParam==MSG_EXIT && CMenuContainer::CanShowMenu())
				{
					LRESULT res=CallNextHookEx(NULL,code,wParam,lParam);
					CleanStartMenuDLL();
					return res; // we should exit as quickly as possible now. the DLL is about to be unloaded
				}
				else if (msg->wParam==MSG_HOTKEYS)
				{
					EnableHotkeys((THotkeys)msg->lParam);
				}
				else if (msg->wParam==MSG_NEWTASKBAR)
				{
					HWND child=(HWND)msg->lParam;
					if (IsWindow(child))
					{
						wchar_t className[100];
						GetClassName(child,className,_countof(className));
						if (_wcsicmp(className,L"Shell_SecondaryTrayWnd")==0)
							HandleSecondaryTaskbar((HWND)msg->lParam);
						else if (_wcsicmp(className,L"ToolbarWindow32")==0)
						{
							HWND taskbar=GetAncestor(child,GA_ROOT);
							TaskbarInfo *info=FindTaskBarInfoBar(taskbar);
							if (info && !info->HasPart(child) && GetParent(child)==info->rebar)
							{
								SetWindowSubclass(child,SubclassTaskbarPartProc,'CLSH',info->taskbarId);
								info->taskbarParts.push_back(child);
							}
						}
						else if (_wcsicmp(className,L"TrayClockWClass")==0 || _wcsicmp(className,L"ClockButton")==0)
						{
							HWND taskbar=GetAncestor(child,GA_ROOT);
							TaskbarInfo *info=FindTaskBarInfoBar(taskbar);
							if (info && !info->HasPart(child))
							{
								SetWindowSubclass(child,SubclassTaskbarPartProc,'CLSH',info->taskbarId);
								info->taskbarParts.push_back(child);
							}
						}
					}
				}
				else if (msg->wParam==MSG_REDRAWTASKBAR)
				{
					if (msg->lParam)
						InvalidateRect((HWND)msg->lParam,NULL,TRUE);
					else
						RedrawTaskbars();
				}
				else if (msg->wParam==MSG_RELOADSETTINGS)
				{
					LoadSettings();
					UpdateTaskBars(TASKBAR_RECREATE_BUTTONS);
					UpdateTaskBars(TASKBAR_UPDATE_TEXTURE);
					ResetHotCorners();
					RedrawTaskbars();
				}
				bProcessing=false;
			}
		}
if (!g_bTrimHooks)
{
		if (msg->message==WM_HOTKEY && msg->hwnd==g_TaskBar)
		{
			if (msg->wParam==g_HotkeyShiftID)
				PostMessage(g_TaskBar,g_StartMenuMsg,MSG_SHIFTWIN,0);
			else if (msg->wParam==g_HotkeyCSMID)
			{
				msg->message=WM_NULL;
				const TaskbarInfo *taskBar=GetDefaultTaskbarInfo();
				if (taskBar->startButton)
					SetForegroundWindow(taskBar->startButton);
				ToggleStartMenu(taskBar->taskbarId,true);
			}
			else if (msg->wParam==g_HotkeyWSMID)
				PostMessage(g_ProgWin,WM_SYSCOMMAND,SC_TASKLIST,'WSMK');
		}

		if (msg->message==WM_KEYDOWN && msg->hwnd==g_TaskBar && (msg->wParam==VK_SPACE || msg->wParam==VK_RETURN))
		{
			GUITHREADINFO info={sizeof(info)};
			if (!GetGUIThreadInfo(GetCurrentThreadId(),&info) || !(info.flags&GUI_INMENUMODE))
			{
				FindWindowsMenu();
				int control=GetSettingInt(L"WinKey");
				if (control==OPEN_BOTH)
				{
					if (GetWinVersion()>=WIN_VER_WIN10)
						control=GetWin10TabletMode()?OPEN_WINDOWS:OPEN_CLASSIC;
					else
						control=GetMetroMode(MonitorFromWindow(g_TaskBar,MONITOR_DEFAULTTONEAREST))?OPEN_WINDOWS:OPEN_CLASSIC;
				}
				if (control==OPEN_CLASSIC)
				{
					msg->message=WM_NULL;
					const TaskbarInfo *taskBar=FindTaskBarInfoBar(g_TaskBar);
					if (taskBar->startButton)
						SetForegroundWindow(taskBar->startButton);
					ToggleStartMenu(taskBar->taskbarId,true);
				}
			}
		}

		if (msg->message==WM_KEYDOWN && msg->wParam==VK_TAB && CMenuContainer::IsMenuWindow(msg->hwnd))
		{
			// the taskbar steals the Tab key. we need to forward it to the menu instead
			SendMessage(msg->hwnd,msg->message,msg->wParam,msg->lParam);
			msg->message=WM_NULL;
		}

		if (msg->message==WM_SYSKEYDOWN && msg->wParam==VK_RETURN && CMenuContainer::IsMenuWindow(msg->hwnd))
		{
			// the taskbar steals the Alt+Enter key. we need to forward it to the menu instead
			SendMessage(msg->hwnd,msg->message,msg->wParam,msg->lParam);
			msg->message=WM_NULL;
		}
}
		bool bClick=(msg->message==WM_LBUTTONDOWN || msg->message==WM_LBUTTONDBLCLK || msg->message==WM_MBUTTONDOWN || msg->message==WM_MBUTTONDBLCLK);
		bool bNcClick=(msg->message==WM_NCLBUTTONDOWN || msg->message==WM_NCLBUTTONDBLCLK || msg->message==WM_NCMBUTTONDOWN || msg->message==WM_NCMBUTTONDBLCLK);
		bool bMiddle=(msg->message==WM_NCMBUTTONDOWN || msg->message==WM_MBUTTONDOWN || msg->message==WM_NCMBUTTONDBLCLK || msg->message==WM_MBUTTONDBLCLK);
		if (bClick || bNcClick)
		{
			const TaskbarInfo *taskBar=NULL;
			if (bClick)
				taskBar=FindTaskBarInfoButton(msg->hwnd); // click on start button
			if (!taskBar)
			{
				taskBar=FindTaskBarInfoBar(msg->hwnd); // click on taskbar
				if (taskBar && !PointAroundStartButton(taskBar->taskbarId))
					taskBar=NULL;
			}
			if (taskBar)
			{
				if (msg->message==WM_LBUTTONDOWN && GetWinVersion()==WIN_VER_WIN7 && msg->hwnd==taskBar->startButton)
				{
					// on Win7 ignore the click if the mouse is not over the start button (clicks on the context menu are sent to the start button)
					CPoint pt(GetMessagePos());
					if (WindowFromPoint(pt)!=msg->hwnd)
					{
						return CallNextHookEx(NULL,code,wParam,lParam);
					}
				}
				// left or middle click on start button
				FindWindowsMenu();
				const wchar_t *name;
				const wchar_t *command;
				if (bMiddle)
				{
					name=L"MiddleClick";
					command=L"MiddleClickCommand";
				}
				else if (GetKeyState(VK_SHIFT)<0)
				{
					name=L"ShiftClick";
					command=L"ShiftClickCommand";
				}
				else
				{
					name=L"MouseClick";
					command=L"MouseClickCommand";
				}

				int control=GetSettingInt(name);
				if (control==OPEN_BOTH && GetWinVersion()>=WIN_VER_WIN10)
					control=GetWin10TabletMode()?OPEN_WINDOWS:OPEN_CLASSIC;
				if (control==OPEN_CLASSIC)
				{
					// click on the start button - toggle the menu
					DWORD keyboard;
					SystemParametersInfo(SPI_GETKEYBOARDCUES,NULL,&keyboard,0);
					ToggleStartMenu(taskBar->taskbarId,keyboard!=0);
				}
				else if (control==OPEN_WINDOWS)
					PostMessage(g_ProgWin,WM_SYSCOMMAND,SC_TASKLIST,'WSMM');
				else if (control==OPEN_CORTANA)
					OpenCortana();
				else if (control==OPEN_CUSTOM)
				{
					CString commandText=GetSettingString(command);
					if (!commandText.IsEmpty())
					{
						wchar_t expandedCommand[_MAX_PATH]{};
						::ExpandEnvironmentStrings(commandText, expandedCommand, _countof(expandedCommand));
						ShellExecute(NULL,NULL,expandedCommand,NULL,NULL,SW_SHOWNORMAL);
					}
				}
				msg->message=WM_NULL;
			}
		}

		if (msg->message==WM_LBUTTONUP)
		{
			// ignore button up on the win81 start button
			const TaskbarInfo *taskBar=FindTaskBarInfoButton(msg->hwnd);
			if (taskBar && taskBar->oldButton==msg->hwnd)
				msg->message=WM_NULL;
		}
if (!g_bTrimHooks)
{
		if (msg->message==WM_TIMER && FindTaskBarInfoBar(msg->hwnd) && CMenuContainer::IgnoreTaskbarTimers())
		{
			// stop the taskbar timer messages. prevents the auto-hide taskbar from closing
			msg->message=WM_NULL;
		}

		if (msg->message==WM_MOUSEMOVE && g_ProgramsButton && msg->hwnd==g_ProgramsButton && GetSettingBool(L"CascadeAll") && !(msg->wParam&MK_SHIFT))
		{
			DWORD pos=GetMessagePos();
			if (pos!=g_LastHoverPos && !g_bAllProgramsTimer)
			{
				g_bAllProgramsTimer=true;
				bool bDef;
				DWORD time=GetSettingInt(L"AllProgramsDelay",bDef);
				if (bDef)
					SystemParametersInfo(SPI_GETMENUSHOWDELAY,NULL,&time,0);
				SetTimer(g_ProgramsButton,'CLSM',time,NULL);
			}
			g_LastHoverPos=pos;
		}
		if (msg->message==WM_TIMER && msg->wParam=='CLSM' && g_ProgramsButton && msg->hwnd==g_ProgramsButton)
		{
			g_bAllProgramsTimer=false;
			KillTimer(g_ProgramsButton,'CLSM');
			DWORD pos=GetMessagePos();
			if (pos==g_LastHoverPos)
				PostMessage(g_AllPrograms,WM_COMMAND,IDOK,(LPARAM)g_ProgramsButton);
			msg->message=WM_NULL;
		}
		if (msg->message==WM_MOUSELEAVE && g_ProgramsButton && msg->hwnd==g_ProgramsButton)
		{
			g_bAllProgramsTimer=false;
			KillTimer(g_ProgramsButton,'CLSM');
		}

		// handle hover
		if (msg->message==WM_MOUSEMOVE)
		{
			TaskbarInfo *taskBar=FindTaskBarInfoButton(msg->hwnd);
			if (taskBar && !CMenuContainer::IsMenuOpened() && !WindowsMenuOpened())
			{
				if (GetSettingInt(L"Hover") && !taskBar->bTimer)
				{
					taskBar->bTimer=true;
					int time=GetSettingInt(L"StartHoverDelay");
					SetTimer(msg->hwnd,'CLSM',time,NULL);
				}
				if (msg->hwnd==taskBar->oldButton)
				{
					APPBARDATA appbar={sizeof(appbar)};
					if (SHAppBarMessage(ABM_GETSTATE,&appbar)&ABS_AUTOHIDE)
						SendMessage(taskBar->taskBar,WM_NCHITTEST,0,GetMessagePos());
				}
			}
		}
		if (msg->message==WM_MOUSELEAVE)
		{
			TaskbarInfo *taskBar=FindTaskBarInfoButton(msg->hwnd);
			if (taskBar)
			{
				taskBar->bTimer=false;
				KillTimer(msg->hwnd,'CLSM');
				if (taskBar->oldButton==msg->hwnd)
				{
					RECT rc;
					GetWindowRect(taskBar->oldButton,&rc);
					CPoint pt(GetMessagePos());
					if (PtInRect(&rc,pt))
					{
						wchar_t className[256]={0};
						GetClassName(WindowFromPoint(pt),className,_countof(className));
						if (wcscmp(className,L"ImmersiveSwitchList")==0 || wcscmp(className,L"EdgeUiInputWndClass")==0)
						{
							msg->message=WM_NULL;
							TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,msg->hwnd,0};
							TrackMouseEvent(&track);
						}
					}
				}
			}
		}
		if ((msg->message==WM_NCMOUSEMOVE || msg->message==WM_NCMOUSELEAVE) && (msg->wParam==HTCAPTION || !IsAppThemed()) && GetSettingInt(L"Hover")) // HACK: in Classic mode the start menu can show up even if wParam is not HTCAPTION (most likely a bug in Windows)
		{
			TaskbarInfo *taskBar=FindTaskBarInfoBar(msg->hwnd);
			if (taskBar)
			{
				if (!CMenuContainer::IsMenuOpened() && !WindowsMenuOpened() && PointAroundStartButton(taskBar->taskbarId))
				{
					if (!taskBar->bTimer)
					{
						taskBar->bTimer=true;
						int time=GetSettingInt(L"StartHoverDelay");
						SetTimer(taskBar->startButton,'CLSM',time,NULL);
					}
				}
				else
				{
					if (taskBar->bTimer)
					{
						taskBar->bTimer=false;
						KillTimer(taskBar->startButton,'CLSM');
					}
				}
			}
		}
		if (msg->message==WM_TIMER && msg->wParam=='CLSM' && CMenuContainer::CanShowMenu())
		{
			TaskbarInfo *taskBar=FindTaskBarInfoButton(msg->hwnd);
			if (taskBar)
			{
				KillTimer(msg->hwnd,'CLSM');
				msg->message=WM_NULL;
				if (taskBar->bTimer && !CMenuContainer::IsMenuOpened() && !WindowsMenuOpened())
				{
					CPoint pt(GetMessagePos());
					if (WindowFromPoint(pt)==msg->hwnd || PointAroundStartButton(taskBar->taskbarId))
					{
						int control=GetSettingInt(L"Hover");
						if (control==OPEN_CLASSIC)
						{
							PostMessage(g_ProgWin,WM_SYSCOMMAND,SC_TASKLIST,'CSM');
						}
						else if (control==OPEN_WINDOWS)
						{
							FindWindowsMenu();
							PostMessage(g_ProgWin,WM_SYSCOMMAND,SC_TASKLIST,'WSMM');
						}
						else if (control==OPEN_CUSTOM)
						{
							CString commandText=GetSettingString(L"HoverCommand");
							if (!commandText.IsEmpty())
							{
								wchar_t expandedCommand[_MAX_PATH]{};
								::ExpandEnvironmentStrings(commandText, expandedCommand, _countof(expandedCommand));
								ShellExecute(NULL,NULL,expandedCommand,NULL,NULL,SW_SHOWNORMAL);
							}
						}
					}
				}
				taskBar->bTimer=false;
			}
		}
}
		// context menu
		if (msg->message==WM_NCRBUTTONUP || msg->message==WM_RBUTTONUP)
		{
			TaskbarInfo *taskBar=FindTaskBarInfoButton(msg->hwnd);
			DWORD winVer=GetWinVersion();
			if (!taskBar && winVer>=WIN_VER_WIN8)
			{
				taskBar=FindTaskBarInfoBar(msg->hwnd);
				if (taskBar && !PointAroundStartButton(taskBar->taskbarId))
					taskBar=NULL;
			}
			if (taskBar)
			{
				CPoint pt0(GetMessagePos());
				if (msg->message==WM_RBUTTONUP && msg->hwnd==taskBar->startButton && msg->lParam==MAKELPARAM(-1,-1))
				{
					RECT rc;
					GetWindowRect(msg->hwnd,&rc);
					pt0.x=(rc.left+rc.right)/2;
					pt0.y=(rc.top+rc.bottom)/2;
				}
				bool bShowCSMenu=false, bShowWinX=false, bShowWin7=false;
				if (msg->hwnd==taskBar->taskBar && taskBar->bReplaceButton)
					bShowWinX=true;
				else
				{
					bShowCSMenu=(GetSettingBool(L"ShiftRight")==(GetKeyState(VK_SHIFT)<0));
					bShowWinX=winVer>=WIN_VER_WIN8 && !bShowCSMenu;
				}
				bShowWin7=!bShowCSMenu && g_WinStartButton && msg->hwnd!=g_WinStartButton;

				if (bShowCSMenu || bShowWinX || bShowWin7)
				{
					msg->message=WM_NULL;
					if (CMenuContainer::IsMenuOpened())
						CMenuContainer::CloseStartMenu();
				}
				if (bShowCSMenu)
				{
					// additional commands for the context menu
					enum
					{
						CMD_SETTINGS=1,
						CMD_HELP,
						CMD_EXIT,
						CMD_OPEN,
						CMD_OPEN_ALL,
						CMD_EXPLORER,
						CMD_OPEN_PINNED,
					};

					// right-click on the start button - open the context menu (Settings, Help, Exit)
					HMENU menu=CreatePopupMenu();
					CString titleFmt=LoadStringEx(IDS_MENU_TITLE);
					if (!titleFmt.IsEmpty())
					{
						CString title;
						DWORD ver=GetVersionEx(g_Instance);
						title.Format(titleFmt,ver>>24,(ver>>16)&0xFF,ver&0xFFFF);

						AppendMenu(menu,MF_STRING,0,title);
						EnableMenuItem(menu,0,MF_BYPOSITION|MF_DISABLED);
						SetMenuDefaultItem(menu,0,TRUE);
						AppendMenu(menu,MF_SEPARATOR,0,0);
					}
					int count0=GetMenuItemCount(menu);
					if (GetSettingBool(L"EnableExplorer"))
					{
						if (!GetSettingString(L"ExplorerPath").IsEmpty())
							AppendMenu(menu,MF_STRING,CMD_EXPLORER,FindTranslation(L"Menu.Explorer",L"Windows Explorer"));
						AppendMenu(menu,MF_STRING,CMD_OPEN,FindTranslation(L"Menu.Open",L"&Open"));
						if (!SHRestricted(REST_NOCOMMONGROUPS))
							AppendMenu(menu,MF_STRING,CMD_OPEN_ALL,FindTranslation(L"Menu.OpenAll",L"O&pen All Users"));
						if (GetSettingInt(L"PinnedPrograms")==PINNED_PROGRAMS_PINNED)
							AppendMenu(menu,MF_STRING,CMD_OPEN_PINNED,FindTranslation(L"Menu.OpenPinned",L"O&pen Pinned"));
						AppendMenu(menu,MF_SEPARATOR,0,0);
					}
					if (GetSettingBool(L"EnableSettings"))
						AppendMenu(menu,MF_STRING,CMD_SETTINGS,FindTranslation(L"Menu.MenuSettings",L"Settings"));
					if (HasHelp())
						AppendMenu(menu,MF_STRING,CMD_HELP,FindTranslation(L"Menu.MenuHelp",L"Help"));
					if (GetSettingBool(L"EnableExit"))
					{
						AppendMenu(menu,MF_STRING,CMD_EXIT,FindTranslation(L"Menu.MenuExit",L"Exit"));
						if (!CMenuContainer::CanShowMenu())
							EnableMenuItem(menu,CMD_EXIT,MF_BYCOMMAND|MF_DISABLED);
					}
					if (GetMenuItemCount(menu)>count0)
					{
						MENUITEMINFO mii={sizeof(mii)};
						mii.fMask=MIIM_BITMAP;
						mii.hbmpItem=HBMMENU_POPUP_CLOSE;
						SetMenuItemInfo(menu,CMD_EXIT,FALSE,&mii);
						MENUINFO info={sizeof(info),MIM_STYLE,MNS_CHECKORBMP};
						SetMenuInfo(menu,&info);
						g_bInMenu=true;
						SetForegroundWindow(msg->hwnd);
						int res=TrackPopupMenu(menu,TPM_RIGHTBUTTON|TPM_RETURNCMD|(IsLanguageRTL()?TPM_LAYOUTRTL:0),pt0.x,pt0.y,0,msg->hwnd,NULL);
						DestroyMenu(menu);
						g_bInMenu=false;
						if (res==CMD_SETTINGS)
						{
							EditSettings(false,0);
						}
						if (res==CMD_HELP)
						{
							ShowHelp();
							return TRUE;
						}
						if (res==CMD_EXIT)
						{
							LRESULT res=CallNextHookEx(NULL,code,wParam,lParam);
							CleanStartMenuDLL();
							return res; // we should exit as quickly as possible now. the DLL is about to be unloaded
						}
						if (res==CMD_OPEN || res==CMD_OPEN_ALL)
						{
							CComString pPath;
							if (SUCCEEDED(ShGetKnownFolderPath((res==CMD_OPEN)?FOLDERID_StartMenu:FOLDERID_CommonStartMenu,&pPath)))
								ShellExecute(NULL,L"open",pPath,NULL,NULL,SW_SHOWNORMAL);
						}
						if (res==CMD_OPEN_PINNED) // open pinned folder
						{
							SHELLEXECUTEINFO execute={sizeof(execute)};
							CString path=GetSettingString(L"PinnedItemsPath");
							execute.lpVerb=L"open";
							execute.lpFile=path;
							execute.nShow=SW_SHOWNORMAL;
							execute.fMask=SEE_MASK_DOENVSUBST;
							ShellExecuteEx(&execute);
						}
						if (res==CMD_EXPLORER)
						{
							CString path=GetSettingString(L"ExplorerPath");
							ITEMIDLIST blank={0};
							SHELLEXECUTEINFO execute={sizeof(execute)};
							execute.lpVerb=L"open";
							execute.lpFile=path;
							execute.nShow=SW_SHOWNORMAL;
							if (_wcsicmp(path,L"computer")==0)
								execute.lpFile=L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}";
							else if (_wcsicmp(path,L"libraries")==0)
								execute.lpFile=L"::{031E4825-7B94-4DC3-B131-E946B44C8DD5}";
							else if (_wcsicmp(path,L"desktop")==0)
							{
								execute.fMask=SEE_MASK_IDLIST;
								execute.lpIDList=&blank;
								execute.lpFile=NULL;
							}
							else
							{
								execute.fMask=SEE_MASK_DOENVSUBST;
							}
							ShellExecuteEx(&execute);
						}
					}
				}
				else if (bShowWinX)
				{
					ShowWinX();
				}
				else if (bShowWin7)
				{
					CPoint pt(GetMessagePos());
					ScreenToClient(g_WinStartButton,&pt);
					PostMessage(g_WinStartButton,WM_RBUTTONUP,wParam,MAKELONG(pt.x,pt.y));
				}
			}
		}

	}
	return CallNextHookEx(NULL,code,wParam,lParam);
}

HBITMAP GetStartScreenIcon( int size )
{
	// for sizes >=64, use image directly
	// for sizes>=32, get 64x64 and scale down
	// for sizes<32 use the system background color
	StartScreenThumbInfo info={{size<64?64:size}};
	info.size.cy=info.size.cx;
	if (size>=32 && g_AppManagerThread && GetWinVersion()==WIN_VER_WIN8)
	{
		info.event=CreateEvent(NULL,TRUE,FALSE,NULL);
		PostThreadMessage(g_AppManagerThread,g_StartMenuMsg,MSG_METROTHUMBNAIL,(LPARAM)&info);
		WaitForSingleObject(info.event,100); // if it takes more than 100ms, screw it
		CloseHandle(info.event);
	}
	info.size.cy=info.size.cx*3/4;

	BITMAPINFO bi={0};
	bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth=bi.bmiHeader.biHeight=size;
	bi.bmiHeader.biPlanes=1;
	bi.bmiHeader.biBitCount=32;

	HDC hDst=CreateCompatibleDC(NULL);
	unsigned int *bits;
	HBITMAP bitmap=CreateDIBSection(hDst,&bi,DIB_RGB_COLORS,(void**)&bits,NULL,0);
	HGDIOBJ bmp0=SelectObject(hDst,bitmap);
	RECT rc={0,size/8,size,size*7/8};

	if (info.bitmap)
	{
		HDC hSrc=CreateCompatibleDC(hDst);
		HGDIOBJ bmp02=SelectObject(hSrc,info.bitmap);
		SetStretchBltMode(hDst,HALFTONE);
		StretchBlt(hDst,rc.left,rc.top,rc.right-rc.left,rc.bottom-rc.top,hSrc,0,0,info.size.cx,info.size.cy,SRCCOPY);
		SelectObject(hSrc,bmp02);
		DeleteDC(hSrc);
		DeleteObject(info.bitmap);
	}
	else
	{
		typedef int (WINAPI *TGetImmersiveUserColorSetPreference)(bool bForceCheckRegistry, bool bSkipCheckOnFail);
		typedef DWORD (WINAPI *TGetImmersiveColorFromColorSetEx)(UINT dwImmersiveColorSet, UINT dwImmersiveColorType, bool bIgnoreHighContrast, UINT dwHighContrastCacheMode);
		typedef int (WINAPI *TGetImmersiveColorTypeFromName)(const wchar_t *name);
		COLORREF color=0;
		HMODULE hUxTheme=GetModuleHandle(L"uxtheme.dll");
		if (hUxTheme)
		{
			TGetImmersiveUserColorSetPreference GetImmersiveUserColorSetPreference=(TGetImmersiveUserColorSetPreference)GetProcAddress(hUxTheme,MAKEINTRESOURCEA(98));
			TGetImmersiveColorFromColorSetEx GetImmersiveColorFromColorSetEx=(TGetImmersiveColorFromColorSetEx)GetProcAddress(hUxTheme,MAKEINTRESOURCEA(95));
			TGetImmersiveColorTypeFromName GetImmersiveColorTypeFromName=(TGetImmersiveColorTypeFromName)GetProcAddress(hUxTheme,MAKEINTRESOURCEA(96));
			if (GetImmersiveUserColorSetPreference && GetImmersiveColorFromColorSetEx && GetImmersiveColorTypeFromName)
			{
				int type=GetImmersiveColorTypeFromName(L"ImmersiveStartBackground");
				int set=GetImmersiveUserColorSetPreference(false,false);
				color=GetImmersiveColorFromColorSetEx(set,type,false,0)&0xFFFFFF;
			}
		}
		SetDCBrushColor(hDst,color);
		FillRect(hDst,&rc,(HBRUSH)GetStockObject(DC_BRUSH));
		HICON hIcon=(HICON)LoadImage(g_Instance,MAKEINTRESOURCE(GetWinVersion()>=WIN_VER_WIN10?IDI_START10:IDI_START),IMAGE_ICON,size,size,LR_DEFAULTCOLOR);
		DrawIconEx(hDst,0,0,hIcon,size,size,0,NULL,DI_NORMAL);
		DestroyIcon(hIcon);
	}
	SelectObject(hDst,bmp0);
	DeleteDC(hDst);
	
	int i=0;
	int n=size*rc.top;
	for (;i<n;i++)
		bits[i]=0;
	n=size*rc.bottom;
	for (;i<n;i++)
		bits[i]|=0xFF000000;
	n=size*size;
	for (;i<n;i++)
		bits[i]=0;

	return bitmap;
}

// WH_GETMESSAGE hook for the explorer's GUI thread. The start menu exe uses this hook to inject code into the explorer process
STARTMENUAPI LRESULT CALLBACK HookInject( int code, WPARAM wParam, LPARAM lParam )
{
	if (code==HC_ACTION && !g_TaskBar)
		InitStartMenuDLL();
	return CallNextHookEx(NULL,code,wParam,lParam);
}

STARTMENUAPI void InitManagers( bool bNohook )
{
	InitializeSkinManager(bNohook);
	g_ItemManager.Init(); // must be after skin manager because it uses the metro colors
	g_SearchManager.Init();
}

STARTMENUAPI void CloseManagers( bool bNohook )
{
	g_SearchManager.Close();
	g_ItemManager.Close();
	CloseSkinManager(bNohook);
}

STARTMENUAPI bool DllGetSettingBool( const wchar_t *name )
{
	return GetSettingBool(name);
}

STARTMENUAPI int DllGetSettingInt( const wchar_t *name )
{
	return GetSettingInt(name);
}

STARTMENUAPI void DllUpdateSettings( void )
{
	CSettingsLockWrite lock;
	UpdateSettings();
}

STARTMENUAPI CString DllLoadStringEx( int stringID )
{
	return LoadStringEx(stringID);
}

STARTMENUAPI void DllLogToFile( const wchar_t *location, const wchar_t *message, ... )
{
	va_list args;
	va_start(args,message);
	VLogToFile(location,message,args);
	va_end(args);
}

#ifndef _WIN64
STARTMENUAPI bool DllSaveAdmx( TSettingsComponent component, const char *admxFile, const char *admlFile, const char *docFile )
{
	return SaveAdmx(component,admxFile,admlFile,docFile);
}

STARTMENUAPI void DllLoadTranslationResources( HINSTANCE hLngInstance, int *pDialogs )
{
	LoadTranslationResources(hLngInstance,pDialogs);
}
#endif

#ifdef TRACK_GDI_RESOURCES
STARTMENUAPI void DllDumpResourceLeaks( void )
{
	DumpResourceLeaks();
}
#endif
