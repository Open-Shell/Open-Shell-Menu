// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

// ExplorerBHO.h : Declaration of the CExplorerBHO

#pragma once
#include "resource.h"       // main symbols
#include <vector>
#include <map>

#include "ClassicExplorer_h.h"
#include "ComHelper.h"
#include <shdispid.h>


// CExplorerBHO

class ATL_NO_VTABLE CExplorerBHO :
	public CComObjectRootEx<CComSingleThreadModel>,
	public CComCoClass<CExplorerBHO, &CLSID_ExplorerBHO>,
	public IObjectWithSiteImpl<CExplorerBHO>,
	public IDispatchImpl<IExplorerBHO, &IID_IExplorerBHO, &LIBID_ClassicExplorerLib, /*wMajor =*/ 1, /*wMinor =*/ 0>,
	public IDispEventImpl<1,CExplorerBHO,&DIID_DWebBrowserEvents2,&LIBID_SHDocVw,1,1>,
	public IDispEventImpl<2,CExplorerBHO,&DIID_DShellFolderViewEvents,&LIBID_Shell32,1,0>
{
public:
	typedef IDispEventImpl<1,CExplorerBHO,&DIID_DWebBrowserEvents2,&LIBID_SHDocVw,1,1> DispEvent1;
	typedef IDispEventImpl<2,CExplorerBHO,&DIID_DShellFolderViewEvents,&LIBID_Shell32,1,0> DispEvent2;
	CExplorerBHO()
	{
		m_bResetStatus=true;
		m_bForceRefresh=false;
		m_bRemapBands=false;
		m_bNoBreadcrumbs=false;
		m_TreeItemHeight=0;
		m_CurIcon=NULL;
		m_CurPath[0]=0;
		m_Rebar=NULL;
		m_TopWindow=NULL;
		m_Breadcrumbs=NULL;
		m_Progress=NULL;
		m_Status=m_Status8=NULL;
		m_DUIView=NULL;
		m_Hook=m_HookKbd=NULL;
		m_Balloon=NULL;
		m_UpButtonIndex=0;
		m_UpHotkey=0;
		m_IconNormal=m_IconHot=m_IconPressed=m_IconDisabled=NULL;
		m_NavigateMsg=0;
		m_AltD=0;
		m_FileSizeWidth=0;
		m_ZoneWidth=0;
	}

	DECLARE_REGISTRY_RESOURCEID_V2_WITHOUT_MODULE(IDR_EXPLORERBHO, CExplorerBHO)

	BEGIN_SINK_MAP( CExplorerBHO )
		SINK_ENTRY_EX(1, DIID_DWebBrowserEvents2, DISPID_DOCUMENTCOMPLETE, OnDocumentComplete)
		SINK_ENTRY_EX(1, DIID_DWebBrowserEvents2, DISPID_ONQUIT, OnQuit)
		SINK_ENTRY_EX(2, DIID_DShellFolderViewEvents, DISPID_SELECTIONCHANGED, OnSelChanged)
	END_SINK_MAP()

	BEGIN_COM_MAP(CExplorerBHO)
		COM_INTERFACE_ENTRY(IExplorerBHO)
		COM_INTERFACE_ENTRY(IObjectWithSite)
		COM_INTERFACE_ENTRY(IDispatch)
	END_COM_MAP()



	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct()
	{
		return S_OK;
	}

	void FinalRelease()
	{
	}

	// Navigation pane styles
	enum
	{
		STYLE_CLASSIC=0, // use classic XP style
		STYLE_SIMPLE=1, // use simple XP style
		STYLE_VISTA=2, // no change
	};

	enum
	{
		SHOW_INFOTIP=1,
		SHOW_FREE_SPACE=2,
		SHOW_ZONE=4,
	};

	enum
	{
		PART_TEXT,
		PART_SIZE,
		PART_ZONE,
	};
public:
	// IObjectWithSite
	STDMETHOD(SetSite)(IUnknown *pUnkSite);

	// DWebBrowserEvents2
	STDMETHOD(OnDocumentComplete)( IDispatch *pDisp, VARIANT *URL );
	STDMETHOD(OnQuit)( void );

	// DShellFolderViewEvents
	STDMETHOD(OnSelChanged)( void );

private:
	// Super-class the toolbar, so it has a different class name. A program called Folder Menu 3 looks for specific controls in Explorer,
	// and our extra toolbar throws it off
	class CToolbar: public CWindowImpl<CToolbar>
	{
	public:
		DECLARE_WND_SUPERCLASS(L"OpenShell.UpButton",TOOLBARCLASSNAME);

		BEGIN_MSG_MAP( CToolbar )
		END_MSG_MAP()
	};

	CComPtr<IShellBrowser> m_pBrowser;
	CComPtr<IWebBrowser2> m_pWebBrowser;
	CComPtr<IDispatch> m_pWebDoc;
	CComPtr<IInternetZoneManager> m_pZoneManager;
	CComPtr<IInternetSecurityManager> m_pSecurityManager;
	std::map<unsigned int,HICON> m_ZoneIconCache;
	bool m_bResetStatus;
	bool m_bForceRefresh;
	bool m_bNoBreadcrumbs;
	bool m_bRemapBands;
	int m_TreeItemHeight;
	int m_UpButtonIndex;
	int m_UpHotkey;
	CToolbar m_Toolbar;
	HWND m_TopWindow;
	HWND m_Rebar;
	HWND m_Balloon;
	HICON m_IconNormal, m_IconHot, m_IconPressed, m_IconDisabled;
	HICON m_CurIcon;
	CAbsolutePidl m_CurPidl;
	wchar_t m_CurPath[1024]; // the current path
	CWindow m_ComboBox;
	CAbsolutePidl m_NavigatePidl;
	UINT m_NavigateMsg; // private message that is posted to the progress bar to navigate to m_NavigatePidl
	HHOOK m_Hook;
	HHOOK m_HookKbd;
	HWND m_Breadcrumbs;
	HWND m_Progress;
	HWND m_Status, m_Status8;
	HWND m_DUIView;
	int m_FileSizeWidth;
	int m_ZoneWidth;
	char m_AltD;

	struct ComboItem
	{
		PIDLIST_ABSOLUTE pidl;
		int indent;
		CString name;
		CString sortName;

		bool operator<( const ComboItem &item ) { return _wcsicmp(sortName,item.sortName)<0; }
	};
	std::vector<ComboItem> m_ComboItems;
	void ClearComboItems( void );
	bool GetStatusText( wchar_t *buf, int size, const wchar_t *oldText, bool bShowTip, bool bShowSpace );
	void GetFileSize( wchar_t *buf, int size );

	static int s_AutoNavDelay;

	static LRESULT CALLBACK HookExplorer( int code, WPARAM wParam, LPARAM lParam );
	static LRESULT CALLBACK HookKeyboard( int code, WPARAM wParam, LPARAM lParam );
	static LRESULT CALLBACK SubclassTreeParentProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );
	static LRESULT CALLBACK SubclassTreeProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );
	static LRESULT CALLBACK SubclassStatusProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );
	static LRESULT CALLBACK SubclassStatusProc8( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );
	static LRESULT CALLBACK SubclassDUIViewProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );
	static LRESULT CALLBACK SubclassRebarProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );
	static LRESULT CALLBACK SubclassBreadcrumbProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );
	static LRESULT CALLBACK SubclassProgressProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );
};

OBJECT_ENTRY_AUTO(__uuidof(ExplorerBHO), CExplorerBHO)

bool ShowTreeProperties( HWND hwndTree );
