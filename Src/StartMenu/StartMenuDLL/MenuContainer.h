// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#pragma once

#include "SkinManager.h"
#include "JumpLists.h"
#include "ItemManager.h"
#include "SearchManager.h"
#include "DragDrop.h"
#include "TouchHelper.h"
#include <vector>
#include <map>
#include <ctxtcall.h>

//#define PREVENT_CLOSING // define this to prevent the menu from closing when it is deactivated (useful for debugging)
//#define REPEAT_ITEMS 10 // define this to repeat each menu item (useful to simulate large menus)
//#define REPEAT_JUMPLIST_ITEMS 2 // define this to repeat jumplist items (useful to simulate large menus)

#ifdef BUILD_SETUP
#undef PREVENT_CLOSING // make sure it is defined in Setup
#undef REPEAT_ITEMS
#undef REPEAT_JUMPLIST_ITEMS
#endif

enum TRecentPrograms;

enum TMenuID
{
	MENU_NO=0,
	MENU_LAST=0,
	MENU_SEPARATOR,
	MENU_EMPTY,
	MENU_EMPTY_TOP,
	MENU_RECENT,
	MENU_JUMPITEM,
	MENU_COLUMN_PADDING,
	MENU_COLUMN_BREAK,
	MENU_PROGRAMS_TREE,

	// standard menu items
	MENU_PROGRAMS,
	MENU_APPS,
	MENU_COMPUTER,
	MENU_FAVORITES,
	MENU_DOCUMENTS,
		MENU_USERFILES,
		MENU_USERDOCUMENTS,
		MENU_USERPICTURES,
	MENU_SETTINGS,
		MENU_CONTROLPANEL,
		MENU_PCSETTINGS,
		MENU_NETWORK,
		MENU_SECURITY,
		MENU_PRINTERS,
		MENU_TASKBAR,
		MENU_FEATURES,
		MENU_CLASSIC_SETTINGS,
	MENU_SEARCH,
		MENU_SEARCH_FILES,
		MENU_SEARCH_PRINTER,
		MENU_SEARCH_COMPUTERS,
		MENU_SEARCH_PEOPLE,
	MENU_HELP,
	MENU_RUN,
	MENU_LOGOFF,
	MENU_DISCONNECT,
	MENU_UNDOCK,
	MENU_MONITOROFF,
	MENU_SHUTDOWN_BOX,
	MENU_SHUTDOWN_BUTTON,

	// additional commands
	MENU_CUSTOM, // used for any custom item
	MENU_SLEEP,
	MENU_HIBERNATE,
	MENU_RESTART,
	MENU_SHUTDOWN,
	MENU_SWITCHUSER,
	MENU_LOCK,
	MENU_RECENT_PROGRAMS,
	MENU_SEARCH_BOX,
	MENU_SEARCH_CATEGORY,
	MENU_SEARCH_EXECUTE,
	MENU_SEARCH_EMPTY,
	MENU_MORE_RESULTS,
	MENU_SEARCH_INTERNET,
	MENU_SEARCH_PROVIDER,

	// internal commands
	MENU_SEARCH_FILES_CUSTOM,
	MENU_LOGOFF_CONFIRM,
	MENU_RESTART_ADVANCED,
	MENU_RESTART_UPDATE,
	MENU_SHUTDOWN_UPDATE,
	MENU_RESTART_NOUPDATE,
	MENU_SHUTDOWN_NOUPDATE,
	MENU_SHUTDOWN_HYBRID,
	
	MENU_IGNORE=1024, // ignore this item
};

struct StdMenuItem
{
	const wchar_t *command;
	TMenuID id;
	const KNOWNFOLDERID *folder1; // NULL if not used
	const KNOWNFOLDERID *folder2; // NULL if not used

	const wchar_t *label; // localization key
	const wchar_t *tip; // default tooltip
	const wchar_t *iconPath;
	const wchar_t *link;
	unsigned int settings;
	const StdMenuItem *submenu;
	CString labelString, tipString; // additional storage for the strings

	// user settings
	enum
	{
		MENU_OPENUP         = 0x000001, // prefer to open up
		MENU_OPENUP_REC     = 0x000002, // children prefer to open up
		MENU_SORTZA         = 0x000004, // sort backwards
		MENU_SORTZA_REC     = 0x000008, // children sort backwards
		MENU_SORTONCE       = 0x000010, // save the sort order the first time the menu is opened
		MENU_ITEMS_FIRST    = 0x000020, // place the custom items before the folder items
		MENU_TRACK          = 0x000040, // track shortcuts from this menu
		MENU_NOTRACK        = 0x000080, // don't track shortcuts from this menu
		MENU_NOEXPAND       = 0x000100, // don't expand this link item
		MENU_SINGLE_EXPAND  = 0x000200, // expand only one level
		MENU_MULTICOLUMN    = 0x000400, // make this item a multi-column item
		MENU_NOEXTENSIONS   = 0x000800, // hide extensions
		MENU_INLINE         = 0x001000, // inline sub-items in the parent menu
		MENU_SPLIT_BUTTON   = 0x002000, // the item is drawn as a split button
		MENU_ITEM_DISABLED  = 0x004000, // the item is disabled
		MENU_NORECENT       = 0x008000, // don't show recent items in the root menu (because a sub-menu uses MENU_RECENT_ITEMS)

		// style
		MENU_STYLE_CLASSIC1 = 0x100000,
		MENU_STYLE_CLASSIC2 = 0x200000,
		MENU_STYLE_MASK     = MENU_STYLE_CLASSIC1|MENU_STYLE_CLASSIC2,
	};
};

struct SpecialFolder
{
	const KNOWNFOLDERID *folder;
	unsigned int settings;
	wchar_t PATH[_MAX_PATH];

	enum
	{
		FOLDER_NOSUBFOLDERS=1, // don't show the subfolders of this folder
		FOLDER_NONEWFOLDER=2, // don't show the "New Folder" command
		FOLDER_NODROP=4, // don't allow reordering, don't show "Sort" and "Auto Arrange" (also implies FOLDER_NONEWFOLDER)
		FOLDER_NOPATH=8, // don't trust the child parsing names
	};
};

extern SpecialFolder g_SpecialFolders[];
void InitializeSpecialFolders( void );

class CMenuAccessible;
class CProgramsTree;
class CMenuContainer;

#ifndef __IFrameworkInputPane_INTERFACE_DEFINED__

MIDL_INTERFACE("226C537B-1E76-4D9E-A760-33DB29922F18")
IFrameworkInputPaneHandler : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE Showing( RECT *prcInputPaneScreenLocation, BOOL fEnsureFocusedElementInView ) = 0;
	virtual HRESULT STDMETHODCALLTYPE Hiding( BOOL fEnsureFocusedElementInView ) = 0;
};

MIDL_INTERFACE("5752238B-24F0-495A-82F1-2FD593056796")
IFrameworkInputPane: public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE Advise( IUnknown *pWindow, IFrameworkInputPaneHandler *pHandler, DWORD *pdwCookie ) = 0;
	virtual HRESULT STDMETHODCALLTYPE AdviseWithHWND( HWND hwnd, IFrameworkInputPaneHandler *pHandler, DWORD *pdwCookie ) = 0;
	virtual HRESULT STDMETHODCALLTYPE Unadvise( DWORD dwCookie ) = 0;
	virtual HRESULT STDMETHODCALLTYPE Location( RECT *prcInputPaneScreenLocation ) = 0;
};

#endif

// CUserWindow - implementation for the user picture window
class CUserWindow: public CWindowImpl<CUserWindow>
{
public:
	CUserWindow( void ) { m_pOwner=NULL; m_Bits=NULL; m_bDefaultImage=true; m_Bitmap=m_UserBitmap=m_TimerBitmap=NULL; m_Timer=0; m_Size.cx=m_Size.cy=0; }
	DECLARE_WND_CLASS_EX(L"OpenShell.CUserWindow",0,COLOR_MENU)

	// message handlers
	BEGIN_MSG_MAP( CUserWindow )
		MESSAGE_HANDLER( WM_DESTROY, OnDestroy )
		MESSAGE_HANDLER( WM_MOUSEACTIVATE, OnMouseActivate )
		MESSAGE_HANDLER( WM_NCHITTEST, OnNcHitTest )
		MESSAGE_HANDLER( WM_LBUTTONDOWN, OnLButtonDown )
		MESSAGE_HANDLER( WM_SETCURSOR, OnSetCursor )
		MESSAGE_HANDLER( WM_TIMER, OnTimer )
	END_MSG_MAP()

	void Init( CMenuContainer *pOwner );
	void Update( int alpha=255 );
	void UpdatePartial( POINT pos, const RECT *pClipRect );
	void SetImage( HBITMAP bmp, bool bAnimate );
	void StartImageTimer( HBITMAP bmp );

protected:
	LRESULT OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnMouseActivate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled ) { return MA_NOACTIVATE; }
	LRESULT OnNcHitTest( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled ) { return HTCLIENT; }
	LRESULT OnLButtonDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSetCursor( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnEraseBkgnd( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled ) { return 1; }
	LRESULT OnTimer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );

private:
	CMenuContainer *m_pOwner;
	std::vector<unsigned int> m_Source;
	unsigned int *m_Bits;
	bool m_bDefaultImage;
	HBITMAP m_UserBitmap;
	HBITMAP m_Bitmap;
	SIZE m_Size;
	int m_Timer;
	HBITMAP m_TimerBitmap;
	bool m_bUserBitmapMask;

	enum
	{
		TIMER_BLEND=1,
		TIMER_SET=2,
	};
};

// sound events
enum TMenuSound
{
	SOUND_MAIN,
	SOUND_POPUP,
	SOUND_COMMAND,
	SOUND_DROP,
	SOUND_BUTTON_HOVER,
};

// CMenuContainer - implementation of a single menu box.
class CMenuContainer: public IDropTarget, public IFrameworkInputPaneHandler, public CWindowImplBaseT<CWindow>
{
public:
	virtual ATL::CWndClassInfo &GetWndClassInfo( void )
	{
		static ATL::CWndClassInfo wc =
		{
			{sizeof(WNDCLASSEX),CS_DROPSHADOW|CS_DBLCLKS,StartWindowProc,0,0,NULL,NULL,NULL,(HBRUSH)(COLOR_MENU+1),NULL,L"OpenShell.CMenuContainer",NULL},
			NULL,NULL,IDC_ARROW,TRUE,0,_T("")
		};
		return wc;
	}

	HWND Create( HWND hWndParent, RECT *pRect, DWORD dwStyle, DWORD dwExStyle )
	{
		ATOM atom=GetWndClassInfo().Register(&m_pfnSuperWindowProc);
		return CWindowImplBaseT<CWindow>::Create(hWndParent,pRect,NULL,dwStyle,dwExStyle,0u,atom,NULL);
	}

	// message handlers
	BEGIN_MSG_MAP( CMenuContainer )
		// forward all messages to m_pMenu2 and m_pMenu3 to ensure the context menu functions properly
		if (m_pMenu3)
		{
			if (m_pMenu3->HandleMenuMsg2(uMsg,wParam,lParam,&lResult)==S_OK)
				return TRUE;
		}
		else if (m_pMenu2)
		{
			m_pMenu2->HandleMenuMsg(uMsg,wParam,lParam);
		}
		MESSAGE_HANDLER( WM_CREATE, OnCreate )
		MESSAGE_HANDLER( WM_DESTROY, OnDestroy )
		MESSAGE_HANDLER( WM_SHOWWINDOW, OnShowWindow )
		MESSAGE_HANDLER( WM_PAINT, OnPaint )
		MESSAGE_HANDLER( WM_PRINTCLIENT, OnPaint )
		MESSAGE_HANDLER( WM_ERASEBKGND, OnEraseBkgnd )
		MESSAGE_HANDLER( WM_ACTIVATE, OnActivate )
		MESSAGE_HANDLER( WM_MOUSEACTIVATE, OnMouseActivate )
		MESSAGE_HANDLER( WM_MOUSEMOVE, OnMouseMove )
		MESSAGE_HANDLER( WM_MOUSELEAVE, OnMouseLeave )
		MESSAGE_HANDLER( WM_MOUSEWHEEL, OnMouseWheel )
		MESSAGE_HANDLER( WM_LBUTTONDOWN, OnLButtonDown )
		MESSAGE_HANDLER( WM_LBUTTONDBLCLK, OnLButtonDblClick )
		MESSAGE_HANDLER( WM_LBUTTONUP, OnLButtonUp )
		MESSAGE_HANDLER( WM_RBUTTONDOWN, OnRButtonDown )
		MESSAGE_HANDLER( WM_RBUTTONUP, OnRButtonUp )
		MESSAGE_HANDLER( WM_SETCURSOR, OnSetCursor )
		MESSAGE_HANDLER( WM_CONTEXTMENU, OnContextMenu )
		MESSAGE_HANDLER( WM_VSCROLL, OnVScroll )
		MESSAGE_HANDLER( WM_KEYDOWN, OnKeyDown )
		MESSAGE_HANDLER( WM_SYSKEYDOWN, OnSysKeyDown )
		MESSAGE_HANDLER( WM_CHAR, OnChar )
		MESSAGE_HANDLER( WM_SYSCHAR, OnChar )
		MESSAGE_HANDLER( WM_TIMER, OnTimer )
		MESSAGE_HANDLER( WM_SYSCOMMAND, OnSysCommand )
		MESSAGE_HANDLER( WM_SETTINGCHANGE, OnSettingChange )
		MESSAGE_HANDLER( WM_DISPLAYCHANGE, OnDisplayChange )
		MESSAGE_HANDLER( WM_GETOBJECT, OnGetAccObject )
		MESSAGE_HANDLER( WM_CTLCOLOREDIT, OnColorEdit )
		MESSAGE_HANDLER( WM_GESTURE, OnGesture )
		MESSAGE_HANDLER( WM_GESTURENOTIFY, OnGestureNotify )
		MESSAGE_HANDLER( MCM_REFRESH, OnRefresh )
		MESSAGE_HANDLER( MCM_SETCONTEXTITEM, OnSetContextItem )
		MESSAGE_HANDLER( MCM_REDRAWEDIT, OnRedrawEdit )
		MESSAGE_HANDLER( MCM_REFRESHICONS, OnRefreshIcons )
		MESSAGE_HANDLER( MCM_SETHOTITEM, OnSetHotItem )
		MESSAGE_HANDLER( MCM_WORKAREACHANGED, OnWorkAreaChanged )
		MESSAGE_HANDLER( s_StartMenuMsg, OnStartMenuMsg )
		MESSAGE_HANDLER( WM_POINTERDOWN, OnPointerDown )
		MESSAGE_HANDLER( WM_POINTERUPDATE, OnPointerUpdate )
		MESSAGE_HANDLER( WM_POINTERUP, OnPointerUp )
		COMMAND_CODE_HANDLER( EN_CHANGE, OnEditChange )
		REFLECT_NOTIFICATIONS()
	END_MSG_MAP()

	static bool CloseStartMenu( void );
	static bool IsMenuOpened( void ) { return !s_Menus.empty(); }
	static bool HasInputHandler( void ) { return s_pFrameworkInputPane!=NULL; }
	static bool CanShowMenu( void ) { return s_Menus.empty() || !s_bPreventClosing; }
	static bool IsMenuWindow( HWND hWnd );
	static bool IgnoreTaskbarTimers( void ) { return !s_Menus.empty() && (s_TaskbarState&ABS_AUTOHIDE); }
	static HWND ToggleStartMenu( int taskbarId, bool bKeyboard, bool bAllPrograms );
	static bool ProcessMouseMessage( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam );
	static void RefreshIcons( void );
	static void RefreshSearch( void );
	static void PlayMenuSound( TMenuSound sound );

protected:
	// IUnknown
	virtual STDMETHODIMP QueryInterface( REFIID riid, void **ppvObject );

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
	virtual HRESULT STDMETHODCALLTYPE DragEnter( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect );
	virtual HRESULT STDMETHODCALLTYPE DragOver( DWORD grfKeyState, POINTL pt, DWORD *pdwEffect );
	virtual HRESULT STDMETHODCALLTYPE DragLeave( void );
	virtual HRESULT STDMETHODCALLTYPE Drop( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect );

	// IFrameworkInputPaneHandler
	virtual HRESULT STDMETHODCALLTYPE Showing( RECT *prcInputPaneScreenLocation, BOOL fEnsureFocusedElementInView );
	virtual HRESULT STDMETHODCALLTYPE Hiding( BOOL fEnsureFocusedElementInView );

	LRESULT OnCreate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnShowWindow( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnRefresh( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnPaint( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnEraseBkgnd( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled ) { return 1; }
	LRESULT OnActivate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnMouseActivate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnMouseMove( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnMouseLeave( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnMouseWheel( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnLButtonDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnLButtonDblClick( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnLButtonUp( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnRButtonDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnRButtonUp( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSetCursor( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnContextMenu( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnVScroll( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnKeyDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSysKeyDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnChar( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnTimer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSysCommand( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSettingChange( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnDisplayChange( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnWorkAreaChanged( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnGetAccObject( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSetContextItem( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnColorEdit( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnGesture( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnGestureNotify( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnPointerDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnPointerUpdate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnPointerUp( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnRedrawEdit( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnRefreshIcons( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSetHotItem( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnStartMenuMsg( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnEditChange( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	virtual void OnFinalMessage( HWND ) { Release(); }

protected:
	CMenuContainer( CMenuContainer *pParent, int index, int options, const StdMenuItem *pStdItem, PIDLIST_ABSOLUTE path1, PIDLIST_ABSOLUTE path2 );
	~CMenuContainer( void );

private:
	// options when creating a container
	enum
	{
		CONTAINER_MULTICOLUMN  = 0x0000001, // use multiple columns instead of a single scrolling column
		CONTAINER_MULTICOL_REC = 0x0000002, // the children will be multi-column
		CONTAINER_CONTROLPANEL = 0x0000004, // this is the control panel, don't go into subfolders
		CONTAINER_PROGRAMS     = 0x0000008, // this is a folder from the Start Menu hierarchy (drop operations prefer link over move)
		CONTAINER_DOCUMENTS    = 0x0000010, // sort by time, limit the count (for recent documents)
		CONTAINER_ALLPROGRAMS  = 0x0000020, // this is the main menu of All Programs (combines the Start Menu and Programs folders)
		CONTAINER_RECENT       = 0x0000040, // insert recent programs (sorted by time)
		CONTAINER_LINK         = 0x0000080, // this is an expanded link to a folder (always scrolling)
		CONTAINER_ITEMS_FIRST  = 0x0000100, // put standard items at the top
		CONTAINER_DRAG         = 0x0000200, // allow items to be dragged out
		CONTAINER_DROP         = 0x0000400, // allow dropping of items
		CONTAINER_LEFT         = 0x0000800, // the window is aligned on the left
		CONTAINER_TOP          = 0x0001000, // the window is aligned on the top
		CONTAINER_AUTOSORT     = 0x0002000, // the menu is always in alphabetical order
		CONTAINER_OPENUP_REC   = 0x0004000, // the container's children will prefer to open up instead of down
		CONTAINER_SORTZA       = 0x0008000, // the container will sort backwards by default
		CONTAINER_SORTZA_REC   = 0x0010000, // the container's children will sort backwards by default
		CONTAINER_SORTONCE     = 0x0020000, // the container will save the sort order the first time the menu is opened
		CONTAINER_TRACK        = 0x0040000, // track shortcuts from this menu
		CONTAINER_NOSUBFOLDERS = 0x0080000, // don't go into subfolders
		CONTAINER_NONEWFOLDER  = 0x0100000, // don't show the "New Folder" command
		CONTAINER_NOPATH       = 0x0200000, // don't trust the child parsing names
		CONTAINER_SEARCH       = 0x0400000, // this is he search results submenu
		CONTAINER_NOEXTENSIONS = 0x0800000, // hide extensions
		CONTAINER_JUMPLIST     = 0x1000000, // this is a jumplist menu
		CONTAINER_APPS         = 0x2000000, // this is the folder for Metro apps
	};

	// description of a menu item
	struct MenuItem
	{
		MenuItem( TMenuID _id=MENU_NO )
		{
			id=_id;
			pStdItem=NULL;
			nameHash=0;
			pItemInfo=NULL;
			drawType=MenuSkin::COLUMN1_ITEM;
			column=row=0;
			memset(&itemRect,0,sizeof(itemRect));
			bFolder=bLink=bPrograms=bAlignBottom=bBreak=bInline=bInlineFirst=bInlineLast=bSplit=bHasJumpList=bMetroLink=bMetroApp=bBlankSeparator=bNew=bStartScreen=bCustomAccelerator=false;
			priority=0;
			pItem1=pItem2=NULL;
			mfuHash=0;
			categoryHash=0;
			jumpIndex=-1;
			accelerator=0;
			drive=0;
			textStart=textEnd=0;
			mruOrder=-1;
			nameOffset=0;
		}

		TMenuID id; // if pStdItem!=NULL, this is pStdItem->id. otherwise it can only be MENU_NO, MENU_SEPARATOR, MENU_EMPTY or MENU_EMPTY_TOP
		const StdMenuItem *pStdItem; // NULL if not a standard menu item
		CString name;
		unsigned int nameHash;
		const CItemManager::ItemInfo *pItemInfo;
		MenuSkin::TItemDrawType drawType;
		int column;
		int row;
		RECT itemRect;
		bool bFolder:1; // this is a folder - draw arrow
		bool bLink:1; // this is a link (if a link to a folder is expanded it is always single-column)
		bool bPrograms:1; // this item is part of the Start Menu folder hierarchy
		bool bAlignBottom:1; // two-column menu: this item is aligned to the bottom
		bool bBreak:1; // two-column menu: this item starts the second column
		bool bInline:1; // this item is inlined in the parent menu
		bool bInlineFirst:1; // this item is the first from the inlined group
		bool bInlineLast:1; // this item is the last from the inlined group
		bool bSplit:1; // split button item
		bool bHasJumpList:1; // this item has a jump list
		bool bMetroLink:1; // this is a Windows 8 Metro shortcut
		bool bMetroApp:1; // this is a Windows 10 Metro app item
		bool bBlankSeparator:1; // this is a blank separator that is the same size as normal items
		bool bNew:1; // this is a highlighted new item
		bool bStartScreen:1; // this is the start screen shortcut item
		bool bCustomAccelerator:1; // the accelerator is specified explicitly
		char priority; // used for sorting of the All Programs menu (0 for Start Menu, 1 for the separator, 2 for the Programs)
		char drive;
		wchar_t accelerator; // accelerator character, 0 if none

		// pair of shell items. 2 items are used to combine a user folder with a common folder (I.E. user programs/common programs)
		PIDLIST_ABSOLUTE pItem1;
		PIDLIST_ABSOLUTE pItem2;

		unsigned int mfuHash; // MENU_RECENT
		short mruOrder; // MENU_RECENT
		short nameOffset; // MENU_RECENT
		unsigned int categoryHash; // search item (lower 4 bits correspond to category)
		short textStart, textEnd; // start and end horizontal offset of the text
		int jumpIndex; // MENU_JUMPITEM: MAKELONG(group,item)

		bool operator<( const MenuItem &item ) const
		{
			if (priority<item.priority) return true;
			if (priority>item.priority) return false;
			if (row<item.row) return true;
			if (row>item.row) return false;
			if ((bFolder && !bHasJumpList) && !(item.bFolder && !item.bHasJumpList)) return true;
			if (!(bFolder && !bHasJumpList) && (item.bFolder && !item.bHasJumpList)) return false;
			if (drive && !item.drive) return true;
			if (!drive && item.drive) return false;
			if (drive && item.drive) return drive<item.drive;
			return CompareMenuString(name,item.name)<0;
		}

		void SetName( const wchar_t *_name, bool bNoExtensions )
		{
			if (bNoExtensions)
			{
				const wchar_t *end=wcsrchr(_name,'.');
				if (end)
				{
					name=CString(_name,(int)(end-_name));
					return;
				}
			}
			name=_name;
		}

		void SetName( const CString &_name, bool bNoExtensions )
		{
			if (bNoExtensions)
			{
				const wchar_t *start=_name;
				const wchar_t *end=wcsrchr(start,'.');
				if (end)
				{
					name=CString(start,(int)(end-start));
					return;
				}
			}
			name=_name;
		}

		void Clone( const MenuItem &item )
		{
			*this=item;
			if (pItem1) pItem1=ILCloneFull(pItem1);
			if (pItem2) pItem2=ILCloneFull(pItem2);
		}

		struct MruNameComparator
		{
			bool operator()( const MenuItem &item1, const MenuItem &item2 ) const
			{
				return CompareMenuString((const wchar_t*)item1.name+item1.nameOffset,(const wchar_t*)item2.name+item2.nameOffset)<0;
			}
		};

		struct MruOrderComparator
		{
			bool operator()( const MenuItem &item1, const MenuItem &item2 ) const
			{
				return item1.mruOrder<item2.mruOrder;
			}
		};
	};

	struct SortMenuItem
	{
		CString name;
		unsigned int nameHash;
		bool bFolder;
		bool bHasJumpList;
		char priority;
		char drive;

		SortMenuItem( const CString &_name, unsigned _nameHash, bool _bFolder, bool _bHasJumpList, char _priority ,char _drive) { name=_name; nameHash=_nameHash; bFolder=_bFolder; bHasJumpList=_bHasJumpList; priority=_priority; drive=_drive; }
		SortMenuItem( const MenuItem &item ) { name=item.name; nameHash=item.nameHash; bFolder=item.bFolder; bHasJumpList=item.bHasJumpList; priority=item.priority; drive=item.drive; }
		bool operator<( const SortMenuItem &x ) const
		{
			if (priority<x.priority) return true;
			if (priority>x.priority) return false;
			if ((bFolder && !bHasJumpList) && !(x.bFolder && !x.bHasJumpList)) return true;
			if (!(bFolder && !bHasJumpList) && (x.bFolder && !x.bHasJumpList)) return false;
			if (drive && !x.drive) return true;
			if (!drive && x.drive) return false;
			if (drive && x.drive) return drive<x.drive;
			return CompareMenuString(name,x.name)<0;
		}
	};

	// Recent document item (sorts by time, newer first)
	struct Document
	{
		CString name;
		FILETIME time;

		Document( void ) { time.dwHighDateTime=time.dwLowDateTime=0; }
		bool operator<( const Document &x ) const { return CompareFileTime(&time,&x.time)>0; }
	};

	LONG m_RefCount;
	bool m_bSubMenu;
	bool m_bDestroyed; // the menu is destroyed but not yet deleted
	bool m_bTrackMouse;
	bool m_bRefreshItems;
	bool m_bWorkAreaPosted;
	volatile DWORD m_RefreshPosted;
	int m_Options;
	const StdMenuItem *m_pStdItem; // the first item
	CMenuContainer *m_pParent; // parent menu
	int m_ParentIndex; // the index of this menu in the parent (usually matches m_pParent->m_Submenu)
	int m_Submenu; // the item index of the opened submenu
	int m_SubShowTime; // the time when the submenu was shown
	int m_HotItem;
	int m_InsertMark;
	bool m_bInsertAfter;
	bool m_bHotArrow;
	unsigned int m_FolderHash[2]; // hash of the path to use for per-menu settings
	CAbsolutePidl m_Path1[2];
	CAbsolutePidl m_Path2[2];
	CComPtr<IShellItem> m_pDropFolder[2]; // the primary folder (used only as a drop target)
	CComPtr<IShellView> m_pShellView; // keep the view alive because some buggy namespace extensions clean up if there is no view
	CComPtr<IContextCallback> m_pAccessibleContext;
	CComPtr<IAccessible> m_pAccessible;
	CComPtr<CDropTargetProxy> m_pDropTargetProxy;
	DWORD m_InputCookie;
	std::vector<int> m_ColumnOffsets;

	std::vector<MenuItem> m_Items; // all items in the menu (including separators)
	CComQIPtr<IContextMenu2> m_pMenu2; // additional interfaces used when a context menu is displayed
	CComQIPtr<IContextMenu3> m_pMenu3;

	int m_DragHoverTime;
	int m_DragHoverItem;
	int m_DragIndex; // the index of the item being dragged
	CComPtr<IDropTargetHelper> m_pDropTargetHelper; // to show images while dragging
	CComPtr<IDragSourceHelper2> m_pDragSourceHelper;
	CComPtr<IDataObject> m_pDragObject;
	CComPtr<IDropTarget> m_pDropTarget;
	const CItemManager::ItemInfo *m_pDropTargetInfo;
	int m_DropTargetIndex;
	int m_DragTime;

	int m_ClickIndex; // the index of the last clicked item (-2 until the mouse enters the menu for the first time)
	bool m_bClickArrow;
	int m_HoverItem; // item under the mouse (used for opening a submenu when the mouse hovers over an item)
	bool m_bHoverArrow;
	int m_ContextItem; // force this to be the hot item while a context menu is up
	HBITMAP m_Bitmap; // the background bitmap
	int m_BitmapOffset; // the horizontal offset of the main menu background from the window
	HRGN m_Region; // the outline region
	POINT m_PaintOffset;
	int m_MaxWidth;
	bool m_bTwoColumns;
	RECT m_rMenu;
	RECT m_rContent;
	RECT m_rContent2;
	RECT m_rUser1; // the user image (0,0,0,0 if the user image is not shown)
	RECT m_rUser2; // the user name (0,0,0,0 if the user name is not shown)
	RECT m_rPadding; // padding in the menu where right-click is possible
	int m_ExtraTop, m_ExtraBottom; // amount of padding that the menu background virtually extends beyond the screen (not actually drawn)
	int m_ExtraBorder; // hight of the border by which the menu extends beyond the screen (solid background with window border)

	int m_ScrollCount; // number of items to scroll in the pager
	int m_ScrollHeight; // 0 - don't scroll
	int m_ScrollOffset;
	int m_ScrollButtonSize;
	int m_MouseWheel;
	bool m_bScrollUp, m_bScrollDown;
	bool m_bScrollUpHot, m_bScrollDownHot;
	bool m_bScrollTimerMouse;
	bool m_bScrollTimerTouch;
	bool m_bNoSearchDraw;
	bool m_bSearchDrawn;
	bool m_bInSearchUpdate;
	bool m_bDisableProgHover;
	bool m_bClosing;
	int m_SearchIndex;
	int m_SearchProvidersCount;
	int m_SearchItemCount;
	CWindow m_SearchBox;
	unsigned int m_SearchHash;
	HBITMAP m_SearchIcons;

	struct SearchItem
	{
		SearchItem( const CString &_name, const CItemManager::ItemInfo *_info ) { name=_name; info=_info; }
		SearchItem( const CItemManager::ItemInfo *_info ) { info=_info; }
		CString name;
		const CItemManager::ItemInfo *info;
	};

	int AddSearchItems( const std::vector<SearchItem> &items, const CString &categoryName, unsigned int categoryHash, int originalCount );
	void GetRecentPrograms( std::vector<MenuItem> &items, int maxCount );

	// additional commands for the context menu
	enum
	{
		CMD_OPEN=1,
		CMD_OPEN_ALL,
		CMD_SORT,
		CMD_AUTOSORT,
		CMD_NEWFOLDER,
		CMD_NEWSHORTCUT,
		CMD_DELETEMRU,
		CMD_DELETEALL,
		CMD_EXPLORE,
		CMD_PIN,
		CMD_PINSETTING,
		CMD_TOGGLE,
		CMD_DELETE,
		CMD_RENAME,
		CMD_MARKOLD,
		CMD_UNINSTALL,

		CMD_LAST,
		CMD_MAX=32767
	};

	// ways to activate a menu item
	enum TActivateType
	{
		ACTIVATE_SELECT, // just selects the item
		ACTIVATE_OPEN, // opens the submenu or selects if not a menu
		ACTIVATE_OPEN_KBD, // same as above, but when done with a keyboard
		ACTIVATE_OPEN_SEARCH, // opens the search results submenu
		ACTIVATE_EXECUTE, // executes the item
		ACTIVATE_MENU, // shows context menu
		ACTIVATE_MENU_BACKGROUND, // shows context menu for the menu itself
		ACTIVATE_RENAME, // renames the item
		ACTIVATE_DELETE, // deletes the item
		ACTIVATE_PROPERTIES, // shows the properties of the item
	};

	// search state
	enum TSearchState
	{
		SEARCH_NONE, // the search is inactive
		SEARCH_BLANK, // the search box has the focus but is blank
		SEARCH_TEXT, // the search box has the focus and is not blank
	};

	TSearchState m_SearchState;
	unsigned int m_SearchCategoryHash;

	enum
	{
		// timer ID
		TIMER_HOVER=1,
		TIMER_SCROLL_MOUSE=2,
		TIMER_SCROLL_TOUCH=3,
		TIMER_TOOLTIP_SHOW=4,
		TIMER_TOOLTIP_HIDE=5,
		TIMER_BALLOON_HIDE=6,
		TIMER_SEARCH=7,
		TIMER_DRAG=8,

		MCM_REFRESH=WM_USER+10, // posted to force the container to refresh its contents
		MCM_SETCONTEXTITEM=WM_USER+11, // sets the item for the context menu. wParam is the nameHash of the item
		MCM_REDRAWEDIT=WM_USER+12, // redraw the search edit box
		MCM_REFRESHICONS=WM_USER+13, // refreshes the icon list and redraws all menus
		MCM_SETHOTITEM=WM_USER+14, // sets the hot item
		MCM_WORKAREACHANGED=WM_USER+15, // when the working area is changed

		// some constants
		MIN_SCROLL_HEIGHT=13, // the scroll buttons are at least this tall
		MAX_MENU_ITEMS=2000,
		MENU_ANIM_SPEED=200,
		MENU_ANIM_SPEED_SUBMENU=100,
		MENU_FADE_SPEED=400,
		MRU_PROGRAMS_COUNT=40,
	};

	void AddFirstFolder( IShellItem *pFolder, std::vector<MenuItem> &items, int options );
	void AddSecondFolder( IShellItem *pFolder, std::vector<MenuItem> &items, int options );

	struct ActivateData
	{
		ActivateData( void ) { bNoModifiers=bProgramsTree=bExpanded=bArrow=bAutoSort=bApps=false; command=0; pNewItemInfo=NULL; hTreeItem=NULL; }
		bool bNoModifiers;
		bool bProgramsTree; // this is an item from the tree
		bool bExpanded; // for tree items
		bool bArrow; // the arrow was clicked
		bool bAutoSort; // the programs folder is auto-sort
		bool bApps;
		HTREEITEM hTreeItem;
		CAbsolutePidl parent; // parent folder
		int command; // menu command
		const CItemManager::ItemInfo *pNewItemInfo; // returns the new item created by a rename or new folder
	};

	void InitItems( void );
	bool InitSearchItems( void );
	void InitWindow( bool bDontShrink=false );
	void InitWindowInternal( bool bDontShrink, const POINT &corner, RECT &menuRect );
	void InitWindowFinalize( const RECT &menuRect );

	// pPt - optional point in screen space (used only by ACTIVATE_EXECUTE and ACTIVATE_MENU)
	void ActivateItem( int index, TActivateType type, const POINT *pPt, ActivateData *pData=NULL );
	void ActivateTreeItem( const void *treeItem, RECT &itemRect, TActivateType type, const POINT *pPt, ActivateData *pData=NULL );
	void DragTreeItem( const void *treeItem, bool bApps );
	void ShowKeyboardCues( bool alt );
	void SetActiveWindow( void );
	void CreateBackground( int width1, int width2, int height1, int height2, int &totalWidth, int &totalHeight, bool bCreateRegion ); // width1/2, height1/2 - the first and second content area
	void BlendPatterns( unsigned int *bits, int width, int height );
	void CreateContentRects( int width1, int width2, int height1, int height2, int &totalWidth, int &totalHeight );
	void CreateSubmenuRegion( int width, int height ); // width, height - the content area
	void ApplyRegion( BOOL bRedraw );
	void PostRefreshMessage( void );
	void SaveItemOrder( const std::vector<SortMenuItem> &items );
	void LoadItemOrder( void );
	void RemoveMFUShortcut( unsigned int hash, bool bAppId );
	void FadeOutItem( int index );
	bool GetItemRect( int index, RECT &rc );
	int HitTest( const POINT &pt, bool *bArrow, bool bDrop=false );
	bool DragOut( int index, bool bApp );
	bool DragOutApps( const CItemManager::ItemInfo *pInfo );
	CComPtr<IDataObject> CreateMetroDataObject( const CItemManager::ItemInfo *pInfo );
	void GetDragEffect( DWORD &grfKeyState, DWORD *pdwEffect );
	void InvalidateItem( int index );
	void SetHotItem( int index, bool bArrow=false, bool bShowTip=false );
	void SetSubmenu( int index );
	void SetContextItem( int index );
	void SetClickItem( int index );
	void UpdateUserPicture( void );
	void SetInsertMark( int index, bool bAfter );
	bool GetInsertRect( RECT &rc );
	void DrawBackground( HDC hdc, const RECT &drawRect );
	bool GetDescription( int index, wchar_t *text, int size );
	void UpdateScroll( void );
	void UpdateScroll( const POINT *pt, bool bTouch );
	bool CanSelectItem( int index, bool bKeyboard=true );
	void SetSearchState( TSearchState state );
	void UpdateSearchResults( bool bForceShowAll );
	void AddInternetSearch( size_t index );
	void AddStandardItems( void );
	void UpdateAccelerators( int first, int last );
	void ExecuteCommand( const wchar_t *command, bool bElevated, bool bEnvSubst );
	void RunUserCommand( bool bPicture );
	void OpenSubMenu( int index, TActivateType type, bool bShift );
	void UpdateAutoComplete( const wchar_t *text );
	bool HasMoreResults( void );
	RECT CalculateWorkArea( const RECT &taskbarRect );
	POINT CalculateCorner( void );
	void NotifyDisplayChange( void );

#define AW_TOPMOST 0x00100000
	void AnimateMenu( int flags, int speed, const RECT &rect );

	enum
	{
		COLLECT_RECURSIVE = 1, // go into subfolders
		COLLECT_PROGRAMS  = 2, // only collect programs (.exe, .com, etc)
		COLLECT_FOLDERS   = 4, // include folder items
		COLLECT_METRO     = 8, // check for metro links (non-recursive)
		COLLECT_KEYWORDS  =16, // include the keywords
	};

	static int s_MaxRecentDocuments; // limit for the number of recent documents
	static int s_ScrollMenus; // global scroll menus setting
	static bool s_bRTL; // RTL layout
	static bool s_bKeyboardCues; // show keyboard cues
	static bool s_bOverrideFirstDown; // the first down key from the search box will select the top item
	static bool s_bExpandRight; // prefer expanding submenus to the right
	static bool s_bBehindTaskbar; // the main menu is behind the taskbar (when the taskbar is horizontal)
	static bool s_bShowTopEmpty; // shows the empty item on the top menu so the user can drag items there
	static bool s_bNoDragDrop; // disables drag/drop
	static bool s_bNoContextMenu; // disables the context menu
	static bool s_bExpandLinks; // expand links to folders
	static bool s_bSingleClickFolders; // open links to folders with one click instead of two
	static bool s_bLogicalSort; // use StrCmpLogical instead of CompareString
	static bool s_bExtensionSort; // sort file names by extension
	static bool s_bAllPrograms; // this is the All Programs menu of the Windows start menu
	static bool s_bNoCommonFolders; // don't show the common folders (start menu and programs)
	static bool s_bNoRun; // don't show run, disable autocomplete
	static bool s_bNoClose; // don't show shutdown, restart, sleep and hibernate
	static bool s_bHasTouch; // the device has integrated touch digitizer
	static char s_bActiveDirectory; // the Active Directory services are available (-1 - uninitialized)
	static bool s_bPreventClosing; // prevents the menus from closing even if they lose focus
	static bool s_bDragClosed; // the menu was closed during a drag operation
	static bool s_bTempHidden; // the menu windows are temporarily hidden
	static bool s_bDisableHover; // disable hovering while the search box has the focus
	static bool s_bHasUpdates; // the OS wants to install updates
	static CMenuContainer *s_pDragSource; // the source of the current drag operation
	static bool s_bDragFromTree; // the source is the programs tree
	static bool s_bDragMovable; // the dragged item is normal original item
	static bool s_bRightDrag; // dragging with the right mouse button
	static bool s_bLockWorkArea; // changes to the work area are ignored
	static bool s_bPendingSearchEnter; // Enter was pressed before the search results were ready
	static RECT s_MenuLimits; // area of the main monitor accessible to all menus
	static RECT s_MainMenuLimits; // area of the main monitor accessible by the main menu
	static DWORD s_TaskbarState; // the state of the taskbar (ABS_AUTOHIDE and ABS_ALWAYSONTOP)
	static DWORD s_HoverTime;
	static DWORD s_SplitHoverTime;
	static DWORD s_ProgramsHoverTime;
	static DWORD s_XMouse;
	static DWORD s_SubmenuStyle;
	static int s_TaskBarId;
	static HWND s_TaskBar, s_StartButton; // the current taskbar and start button
	static UINT s_TaskBarEdge;
	static RECT s_StartRect; // the bounds of the start button
	static HWND s_LastFGWindow; // stores the foreground window to restore later when the menu closes
	static HMONITOR s_MenuMonitor;
	static HTHEME s_Theme;
	static HTHEME s_PagerTheme;
	static CWindow s_Tooltip;
	static CWindow s_TooltipBalloon;
	static int s_TipShowTime;
	static int s_TipHideTime;
	static int s_TipShowTimeFolder;
	static int s_TipHideTimeFolder;
	static DWORD s_HotPos; // last mouse position over a hot item (used to ignore WM_MOUSEMOVE when the mouse didn't really move)
	static int s_HotItem;
	static CMenuContainer *s_pHotMenu; // the menu with the hot item
	static int s_TipItem; // the item that needs a tooltip
	static CMenuContainer *s_pTipMenu;
	static HBITMAP s_ArrowsBitmap;
	static unsigned int s_LastArrowColor;
	static CComPtr<IFrameworkInputPane> s_pFrameworkInputPane;
	static CString s_PinFolder;
	static CLIPFORMAT s_ShellFormat; // CFSTR_SHELLIDLIST
	static CLIPFORMAT s_ShellUrlFormat; // CFSTR_INETURL
	static CLIPFORMAT s_DescriptorFormat; // CFSTR_FILEDESCRIPTOR
	static CLIPFORMAT s_ContentsFormat; // CFSTR_FILECONTENTS
	static CLIPFORMAT s_PreferredEffectFormat;
	static CLIPFORMAT s_MetroLinkFormat;
	static CLIPFORMAT s_DropDescriptionFormat;

	static TRecentPrograms s_RecentPrograms; // show and track recent items

	static std::vector<CMenuContainer*> s_Menus; // all menus, in cascading order
	static volatile HWND s_FirstMenu, s_SearchMenu;
	static CSearchManager::SearchResults s_SearchResults;
	static std::map<unsigned int,int> s_MenuScrolls; // scroll offset for each sub menu
	static char s_HasMoreResults; // -1 - uninitialized
	static int s_ProgramsWidth, s_JumplistWidth;

	static CString s_MRUShortcuts[MRU_PROGRAMS_COUNT];
	static bool s_bMRULoaded;

	static const CItemManager::ItemInfo *s_JumpAppInfo;
	static CJumpList s_JumpList;

	static MenuSkin s_Skin;

	friend class CMetroDataObject;
	friend class COwnerWindow;
	friend class CMenuAccessible;
	friend LRESULT CALLBACK SubclassTopMenuProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );
	friend HRESULT CreatePinLink( PCIDLIST_ABSOLUTE sourcePidl, const wchar_t *name, const wchar_t *iconPath, int iconIndex );

	static void AddMRUShortcut( const wchar_t *path );
	static void AddMRUAppId( const wchar_t *appid );
	static void DeleteMRUShortcut( const wchar_t *path );
	static void DeleteMRUAppId( const wchar_t *appid );
	static void SaveMRUShortcuts( void );
	static void LoadMRUShortcuts( void );
	static HBITMAP LoadUserImage( int size, HBITMAP hMask );
	static int CompareMenuString( const wchar_t *str1, const wchar_t *str2 );
	static LRESULT CALLBACK SubclassSearchBox( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );
	static LRESULT CALLBACK SubclassScrollbar( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );
	void ClearItems( const std::vector<MenuItem>::iterator &begin, const std::vector<MenuItem>::iterator &end );
	void AddJumpListItems( std::vector<MenuItem> &items );
	static HBITMAP GetArrowsBitmap( unsigned int color );
	static bool CloseProgramsMenu( void );
	static void HideStartMenu( void );
	static void SetDropTip( IDataObject *pDataObj, bool bPin );

	enum
	{
		CLOSE_POST         =1,
		CLOSE_SKIP_LAST    =2,
		CLOSE_SKIP_SEARCH  =4,
		CLOSE_ONLY_SEARCH  =8,
		CLOSE_KEEP_MODE   =16,
	};
	static void CloseSubMenus( int flags, CMenuContainer *pAfter );

	struct CreateAccessibleData
	{
		CMenuContainer *pMenu;
		IStream *pStream;
	};
	static HRESULT __stdcall CreateAccessible( ComCallData *pData );
	static HRESULT __stdcall ReleaseAccessible( ComCallData *pData );

	// To control the placement of the start menu, send OpenShellMenu.StartMenuMsg message right after the start menu is created but before it is displayed
	// The lParam must point to StartMenuParams
	// monitorRect - the entire area available to the start menu (sub-menus will use it). It is usually the monitor area but can be less if the Desktop app is docked in Win8
	// uEdge - the location of the taskbar - ABE_BOTTOM, ABE_LEFT, etc
	// taskbarRect - the bounding box of the taskbar. When the taskbar is at the top or bottom, the main menu will try to not overlap that rect. When the taskbar is on the side the behavior depends on the ShowNextToTaskbar setting
	// startButtonRect - the bounding box of the start button. When the taskbar is on the side the main menu will appear below that box if ShowNextToTaskbar is not set
	// taskbar - the taskbar window (optional). The main menu will try to stay in front of that window
	// startButton - the start button window (optional). The main menu will try to stay behind that window
	struct StartMenuParams
	{
		HWND startButton;
		HWND taskbar;
		RECT startButtonRect;
		RECT taskbarRect;
		RECT monitorRect;
		DWORD uEdge;
	};

	static StartMenuParams s_StartMenuParams;
	static UINT s_StartMenuMsg;

	// Win7 stuff
	enum TMenuMode
	{
		MODE_UNKNOWN=-1,
		MODE_NORMAL,
		MODE_PROGRAMS, // the programs tree is shown
		MODE_SEARCH, // the search results are shown
		MODE_JUMPLIST, // a jumplist is shown
	};

	void SetMenuMode( TMenuMode mode, bool bKeyboard=false );
	void OpenJumpList( int index, bool bKeyboard );
	void OpenSearchList( void );
	void AnimateBackground( void );

	CProgramsTree *m_pProgramsTree;
	int m_ProgramButtonIndex;
	int m_ProgramTreeIndex;
	int m_SubJumpItem; // the currently expanded embedded jumplist
	int m_OriginalCount; // the number of orginal items in the main menu
	int m_OriginalScrollCount; // the initial number of scrolled items
	int m_SearchScrollCount;
	int m_SearchScrollHeight; // in items
	int m_SearchScrollPos; // in items
	CWindow m_Scrollbar;
	HTHEME m_ScrollTheme;

	static bool s_bWin7Style;
	static TMenuMode s_MenuMode; // always MODE_NORMAL if s_bWin7Style is false
	static TMenuMode s_PreSearchMenuMode;
	static TMenuID s_ShutdownCommand;
	static int s_MenuHeight; // main menu height in normal mode
	static int s_MenuMaxHeight[2]; // main menu max height in normal mode
	static CUserWindow s_UserPicture;
	static RECT s_UserPictureRect;
	static int s_ProgramsScrollPos;

	// main menu animation
	static int s_MenuWidthNormal; // main menu width in normal modes
	static int s_MenuWidthJump; // main menu width in jumplist mode
	static int s_MenuWidthMax; // the larger of the two widths
	static int s_BackgroundW1, s_BackgroundW2, s_BackgroundH1, s_BackgroundH2;
	struct OldMenuState
	{
		TMenuMode mode;
		int w1, w2, h1, h2;
		int bitmapOffset;
		int hotItem;
		bool bHotArrow;
		std::vector<MenuItem> items;
		int blend; // from 0 to 100
	};
	static OldMenuState s_OldMenuState;

	void InitializeOldState( void );
	static void ClearOldState( void );

	friend class CProgramsTree;
	friend class CUserWindow;
	friend void CloseSkinManager( bool bCloseIat );

	// Gestures
	int m_PanPosY;
	int m_Overpan;
	int m_PointerId;
};

class CSubMenuContainer: public CMenuContainer
{
public:
	virtual ATL::CWndClassInfo &GetWndClassInfo( void )
	{
		static ATL::CWndClassInfo wc =
		{
			{sizeof(WNDCLASSEX),CS_DROPSHADOW|CS_DBLCLKS,StartWindowProc,0,0,NULL,NULL,NULL,(HBRUSH)(COLOR_MENU+1),NULL,L"OpenShell.CMenuContainer",NULL},
			NULL,NULL,IDC_ARROW,TRUE,0,_T("")
		};
		return wc;
	}

	CSubMenuContainer( CMenuContainer *pParent, int index, int options, const StdMenuItem *pStdItem, PIDLIST_ABSOLUTE path1, PIDLIST_ABSOLUTE path2 )
		: CMenuContainer(pParent,index,options,pStdItem,path1,path2)
	{}
};

class CMenuFader: public CWindowImpl<CMenuFader>
{
public:
	CMenuFader( HBITMAP bmp, HRGN region, int duration, RECT &rect );
	~CMenuFader( void );
	DECLARE_WND_CLASS_EX(L"OpenShell.CMenuFader",0,COLOR_MENU)

	// message handlers
	BEGIN_MSG_MAP( CMenuFader )
		MESSAGE_HANDLER( WM_ERASEBKGND, OnEraseBkgnd )
		MESSAGE_HANDLER( WM_TIMER, OnTimer )
	END_MSG_MAP()

	void Create( void );

	static void ClearAll( void );

protected:
	LRESULT OnEraseBkgnd( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnTimer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	virtual void OnFinalMessage( HWND ) { PostQuitMessage(0); delete this; }

private:
	int m_Time0;
	int m_Duration;
	int m_LastTime;
	HBITMAP m_Bitmap;
	HRGN m_Region;
	RECT m_Rect;

	static std::vector<CMenuFader*> s_Faders;
};

struct WINCOMPATTRDATA
{
	DWORD attribute;
	PVOID pData;
	ULONG dataSize;
};
typedef BOOL (WINAPI *tSetWindowCompositionAttribute)( HWND hwnd, WINCOMPATTRDATA *pAttrData );

void MarginsBlit( HDC hSrc, HDC hDst, const RECT &rSrc, const RECT &rDst, const RECT &rMargins, bool bAlpha );
