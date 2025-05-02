// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#pragma once

#include <vector>

#ifdef CLASSICSTARTMENUDLL_EXPORTS
#define STARTMENUAPI __declspec(dllexport)
#else
#define STARTMENUAPI __declspec(dllimport)
#endif

// Find the taskbar window for the given process
STARTMENUAPI HWND FindTaskBar( DWORD process );

// WH_GETMESSAGE hook for the explorer's GUI thread. The start menu exe uses this hook to inject code into the explorer process
STARTMENUAPI LRESULT CALLBACK HookInject( int code, WPARAM wParam, LPARAM lParam );

// Toggle the start menu. bKeyboard - set to true to show the keyboard cues
STARTMENUAPI HWND ToggleStartMenu( int taskbarId, bool bKeyboard );

STARTMENUAPI void InitManagers( bool bNohook );
STARTMENUAPI void CloseManagers( bool bNohook );
STARTMENUAPI void WaitDllInitThread( void );

STARTMENUAPI bool DllGetSettingBool( const wchar_t *name );
STARTMENUAPI int DllGetSettingInt( const wchar_t *name );
STARTMENUAPI void DllUpdateSettings( void );
STARTMENUAPI CString DllLoadStringEx( int stringID );
STARTMENUAPI void DllLogToFile( const wchar_t *location, const wchar_t *message, ... );

#ifndef _WIN64
enum TSettingsComponent;
STARTMENUAPI bool DllSaveAdmx( TSettingsComponent component, const char *admxFile, const char *admlFile, const char *docFile );
STARTMENUAPI void DllLoadTranslationResources( HINSTANCE hLngInstance, int *pDialogs );
#endif
STARTMENUAPI bool DllExecuteNamedCommand( const wchar_t *command );

#ifdef TRACK_GDI_RESOURCES
STARTMENUAPI void DllDumpResourceLeaks( void );
#endif

// Enable or disable the tooltip for the start button
void EnableStartTooltip( bool bEnable );

struct TaskbarInfo
{
	TaskbarInfo( void ) { taskbarId=pointerId=0; taskBar=startButton=oldButton=rebar=taskList=chevron=news=desktop=NULL; startButtonSize.cx=startButtonSize.cy=0; oldButtonSize.cx=oldButtonSize.cy=0; bTimer=bCustomLook=bReplaceButton=bHideButton=bRecreatingButton=bThemeChanging=false; }
	int taskbarId;
	HWND taskBar;
	HWND startButton; // either own start button or the win7 start button (depending on bReplaceButton)
	HWND oldButton; // win8.1+ start button (child of taskBar)
	HWND rebar;
	HWND taskList;
	HWND chevron;
	HWND news;
	HWND desktop;
	SIZE startButtonSize;
	SIZE oldButtonSize;
	int pointerId;
	bool bTimer;
	bool bCustomLook;
	bool bReplaceButton; // replace start button with own one
	bool bHideButton; // hide old start button (if we have own button)
	bool bRecreatingButton;
	bool bThemeChanging;
	std::vector<HWND> trayButtons; // ordered by Z order (for win10)
	std::vector<HWND> taskbarParts;
	CComPtr<IDropTarget> pOriginalTarget;

	bool HasPart( HWND part ) const;
};

TaskbarInfo *GetTaskbarInfo( size_t taskbarId );
UINT GetTaskbarPosition( HWND taskBar, MONITORINFO *pInfo, HMONITOR *pMonitor, RECT *pRc );

extern HWND STARTMENUAPI g_TaskBar, g_OwnerWindow;
extern HWND g_TopWin7Menu, g_AllPrograms, g_ProgramsButton, g_UserPic; // from the Windows menu
extern HWND g_ProgWin;
extern HMONITOR g_WSMHMonitor;
extern int g_CurrentCSMTaskbar, g_CurrentWSMTaskbar;

enum TMenuMsgParam // wParam for the OpenShellMenu.StartMenuMsg message
{
	MSG_TOGGLE, // toggles the classic start menu
	MSG_TOGGLENEW, // toggles the Windows start menu
	MSG_OPEN, // opens the classic start menu
	MSG_SETTINGS, // show Settings
	MSG_SHIFTWIN, // Shift+Win was pressed
	MSG_DRAG, // an item is dragged on the start button
	MSG_SHIFTDRAG, // an item is dragged on the start button (Shift is pressed)
	MSG_NOP, // does nothing (basically just finds the Windows menu)
	MSG_EXIT, // unhook everything and exit
	MSG_HOTKEYS, // updates the hotkeys
	MSG_NEWTASKBAR, // new taskbar is created, lParam is the HWND
	MSG_WINXMENU, // open the Win+X menu
	MSG_METROTHUMBNAIL, // refresh the Metro thumbnail
	MSG_REDRAWTASKBAR, // redraw taskbar, lParam is the HWND (NULL for all)
	MSG_RELOADSETTINGS, // reloads the settings from the registry
	MSG_SETMONITOR, // sets the start screen monitor
};

STARTMENUAPI extern enum _MINIDUMP_TYPE MiniDumpType;
STARTMENUAPI LONG _stdcall TopLevelFilter( _EXCEPTION_POINTERS *pExceptionInfo );

enum THotkeys
{
	HOTKEYS_NORMAL,
	HOTKEYS_SETTINGS,
	HOTKEYS_CLEAR,
};

// Set the hotkeys and controls for the start menu
void EnableHotkeys( THotkeys enable );

bool PointAroundStartButton( size_t taskbarId, const CPoint &pt=CPoint(GetMessagePos()) );
void ResetHotCorners( void );
void RedrawTaskbars( void );

enum TUpdateTaskbar
{
	TASKBAR_CLEAR,
	TASKBAR_UPDATE,
	TASKBAR_UPDATE_TEXTURE,
	TASKBAR_RECREATE_BUTTONS,
};

void UpdateTaskBars( TUpdateTaskbar update );
HBITMAP GetStartScreenIcon( int size );
