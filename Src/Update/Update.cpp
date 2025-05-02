// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#define STRICT_TYPED_ITEMIDS
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// some CString constructors will be explicit
#include <windows.h>
#include <atlbase.h>
#include <atlwin.h>
#include <atlstr.h>
#include "resource.h"
#include "StringUtils.h"
#include "Settings.h"
#include "SettingsUIHelper.h"
#include "DownloadHelper.h"
#include "ResourceHelper.h"
#include "Translations.h"
#include <shlobj.h>
#include "DesktopToasts/DesktopToasts.h"


void ClosingSettings( HWND hWnd, int flags, int command )
{
}

void SettingChangedCallback( const CSetting *pSetting )
{
}

void UpgradeSettings( bool bShared )
{
}

void UpdateSettings( void )
{
	CRegKey regKey;
	wchar_t language[100]=L"";
	if (regKey.Open(HKEY_LOCAL_MACHINE,L"Software\\OpenShell\\OpenShell",KEY_READ|KEY_WOW64_64KEY)==ERROR_SUCCESS)
	{
		ULONG size=_countof(language);
		if (regKey.QueryStringValue(L"DefaultLanguage",language,&size)!=ERROR_SUCCESS)
			language[0]=0;
	}
	UpdateSetting(L"Language",language,false);
}

static int g_LoadDialogs[]=
{
	IDD_UPDATE,0x04000000,
	IDD_PROGRESS,0x04000004,
	0
};

static CSetting g_Settings[]={
{L"Update",CSetting::TYPE_GROUP},
	{L"Language",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_SHARED},
	{L"Update",CSetting::TYPE_BOOL,0,0,1,CSetting::FLAG_SHARED},
	{L"Nightly",CSetting::TYPE_BOOL,0,0,0,CSetting::FLAG_SHARED},

	{NULL}
};

const int SETTING_UPDATE=2;
const int SETTING_NIGHTLY=3;

///////////////////////////////////////////////////////////////////////////////

class CUpdateDlg: public CResizeableDlg<CUpdateDlg>
{
public:
	CUpdateDlg( void ) { m_Version=GetVersionEx(g_Instance); m_Data.Clear(); m_Data.newVersion=0xFFFFFFFF; m_Font=NULL; }
	BEGIN_MSG_MAP( CUpdateDlg )
		MESSAGE_HANDLER( WM_INITDIALOG, OnInitDialog )
		MESSAGE_HANDLER( WM_DESTROY, OnDestroy )
		MESSAGE_HANDLER( WM_SIZE, OnSize )
		MESSAGE_HANDLER( WM_GETMINMAXINFO, OnGetMinMaxInfo )
		MESSAGE_HANDLER( WM_CTLCOLORSTATIC, OnColorStatic )
		COMMAND_HANDLER( IDC_CHECKAUTOCHECK, BN_CLICKED, OnCheckAuto )
		COMMAND_HANDLER( IDC_CHECKNIGHTLY, BN_CLICKED, OnCheckNightly )
		COMMAND_HANDLER( IDC_BUTTONCHECKNOW, BN_CLICKED, OnCheckNow )
		COMMAND_HANDLER( IDC_BUTTONDOWNLOAD, BN_CLICKED, OnDownload )
		COMMAND_HANDLER( IDC_CHECKDONT, BN_CLICKED, OnDontRemind )
		COMMAND_HANDLER( IDOK, BN_CLICKED, OnOK )
		COMMAND_HANDLER( IDCANCEL, BN_CLICKED, OnCancel )
		NOTIFY_HANDLER( IDC_LINKWEB, NM_CLICK, OnWeb )
		NOTIFY_HANDLER( IDC_LINKWEB, NM_RETURN, OnWeb )
	END_MSG_MAP()

	BEGIN_RESIZE_MAP
		RESIZE_CONTROL(IDC_STATICLATEST,MOVE_SIZE_X)
		RESIZE_CONTROL(IDC_EDITTEXT,MOVE_SIZE_X|MOVE_SIZE_Y)
		RESIZE_CONTROL(IDC_BUTTONDOWNLOAD,MOVE_MOVE_Y)
		RESIZE_CONTROL(IDC_CHECKDONT,MOVE_MOVE_Y)
		RESIZE_CONTROL(IDC_LINKWEB,MOVE_MOVE_Y)
		RESIZE_CONTROL(IDOK,MOVE_MOVE_X|MOVE_MOVE_Y)
		RESIZE_CONTROL(IDCANCEL,MOVE_MOVE_X|MOVE_MOVE_Y)
	END_RESIZE_MAP

	void Run( void );
	void UpdateData( void );

protected:
	// Handler prototypes:
	//  LRESULT MessageHandler(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	//  LRESULT CommandHandler(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	//  LRESULT NotifyHandler(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnCancel( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnColorStatic( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnCheckAuto( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnCheckNightly( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnCheckNow( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnDownload( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnDontRemind( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnWeb( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );

private:
	CWindow m_Tooltip;
	VersionData m_Data;
	DWORD m_Version;
	HFONT m_Font;

	void UpdateUI( void );

	static void NewVersionCallback( VersionData &data );
};

static CUpdateDlg g_UpdateDlg;

LRESULT CUpdateDlg::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	InitResize(MOVE_MODAL);

	HICON icon=(HICON)LoadImage(g_Instance,MAKEINTRESOURCE(IDI_APPICON),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_DEFAULTCOLOR);
	SendMessage(WM_SETICON,ICON_BIG,(LPARAM)icon);
	icon=(HICON)LoadImage(g_Instance,MAKEINTRESOURCE(IDI_APPICON),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_DEFAULTCOLOR);
	SendMessage(WM_SETICON,ICON_SMALL,(LPARAM)icon);
	SetDlgItemText(IDC_STATICLATEST,L"");

	HDC hdc=::GetDC(NULL);
	int dpi=GetDeviceCaps(hdc,LOGPIXELSY);
	::ReleaseDC(NULL,hdc);
	m_Font=CreateFont(-MulDiv(9,dpi,72),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,FIXED_PITCH,L"Consolas");
	if (m_Font)
		GetDlgItem(IDC_EDITTEXT).SetFont(m_Font);
	m_Tooltip.Create(TOOLTIPS_CLASS,m_hWnd,NULL,NULL,WS_POPUP|TTS_NOPREFIX);

	TOOLINFO tool={sizeof(tool),TTF_SUBCLASS|TTF_IDISHWND,m_hWnd,(UINT_PTR)GetDlgItem(IDC_LINKWEB).m_hWnd};
	CString str=LoadStringEx(IDS_WEBSITE_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	m_Tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	bool check=true;
	if (g_Settings[SETTING_UPDATE].value.vt==VT_I4)
		check=g_Settings[SETTING_UPDATE].value.intVal!=0;
	CheckDlgButton(IDC_CHECKAUTOCHECK,check?BST_CHECKED:BST_UNCHECKED);
	GetDlgItem(IDC_CHECKAUTOCHECK).EnableWindow(!(g_Settings[SETTING_UPDATE].flags&CSetting::FLAG_LOCKED_MASK));
	GetDlgItem(IDC_BUTTONCHECKNOW).EnableWindow(!(g_Settings[SETTING_UPDATE].flags&CSetting::FLAG_LOCKED_MASK) || check);

	bool nightly = false;
	if (g_Settings[SETTING_NIGHTLY].value.vt == VT_I4)
		nightly = g_Settings[SETTING_NIGHTLY].value.intVal != 0;
	CheckDlgButton(IDC_CHECKNIGHTLY, nightly ? BST_CHECKED : BST_UNCHECKED);
	GetDlgItem(IDC_CHECKNIGHTLY).EnableWindow(!(g_Settings[SETTING_NIGHTLY].flags & CSetting::FLAG_LOCKED_MASK) && check);

	UpdateUI();

	return TRUE;
}

LRESULT CUpdateDlg::OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_Font) DeleteObject(m_Font);
	return 0;
}

LRESULT CUpdateDlg::OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	CResizeableDlg<CUpdateDlg>::OnSize();
	return 0;
}

LRESULT CUpdateDlg::OnOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	SaveSettings();
	DestroyWindow();
	return 0;
}

LRESULT CUpdateDlg::OnCancel( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	DestroyWindow();
	return 0;
}

LRESULT CUpdateDlg::OnColorStatic( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_Data.bNewVersion && lParam==(LPARAM)GetDlgItem(IDC_STATICLATEST).m_hWnd)
	{
		HDC hdc=(HDC)wParam;
		SetTextColor(hdc,0xFF);
		SetBkMode(hdc,TRANSPARENT);
		return (LRESULT)GetSysColorBrush(COLOR_3DFACE);
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CUpdateDlg::OnCheckAuto( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	CSettingsLockWrite lock;
	bool check=IsDlgButtonChecked(IDC_CHECKAUTOCHECK)==BST_CHECKED;
	g_Settings[SETTING_UPDATE].value=CComVariant(check?1:0);
	g_Settings[SETTING_UPDATE].flags&=~CSetting::FLAG_DEFAULT;
	GetDlgItem(IDC_CHECKNIGHTLY).EnableWindow(check);
	UpdateUI();
	return 0;
}

LRESULT CUpdateDlg::OnCheckNightly(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
{
	CSettingsLockWrite lock;
	bool check = IsDlgButtonChecked(IDC_CHECKNIGHTLY) == BST_CHECKED;
	g_Settings[SETTING_NIGHTLY].value = CComVariant(check ? 1 : 0);
	g_Settings[SETTING_NIGHTLY].flags &= ~CSetting::FLAG_DEFAULT;
	UpdateUI();
	return 0;
}

LRESULT CUpdateDlg::OnCheckNow( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	UpdateData();
	UpdateUI();
	return 0;
}

static HRESULT CALLBACK TaskDialogCallbackProc( HWND hwnd, UINT uNotification, WPARAM wParam, LPARAM lParam, LONG_PTR dwRefData )
{
	if (uNotification==TDN_HYPERLINK_CLICKED)
	{
		ShellExecute(hwnd,L"open",(const wchar_t*)lParam,NULL,NULL,SW_SHOWNORMAL);
	}
	return S_OK;
}

LRESULT CUpdateDlg::OnDownload( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	if (m_Data.bNewVersion)
	{
		if (!m_Data.downloadUrl.IsEmpty())
		{
			CString fname, error;
			DWORD res=DownloadNewVersion(m_hWnd,COMPONENT_UPDATE,m_Data.downloadUrl,m_Data.downloadSigner,fname,error);
			if (res==2)
				return 0;
			if (res)
			{
				SetDlgItemText(IDC_STATICLATEST,L"");
				PIDLIST_ABSOLUTE pidl;
				if (SUCCEEDED(SHParseDisplayName(fname,NULL,&pidl,0,NULL)))
				{
					HRESULT hr=SHOpenFolderAndSelectItems(pidl,0,NULL,0);
					ILFree(pidl);
					if (SUCCEEDED(hr))
						return 0;
				}
				ShellExecute(NULL,NULL,fname,NULL,NULL,SW_SHOWNORMAL);
			}
			else
			{
				error+=LoadStringEx(IDS_DOWNLOAD_TIP)+L"\r\n\r\n"+m_Data.updateLink;
				TASKDIALOGCONFIG task={sizeof(task),m_hWnd,NULL,TDF_ENABLE_HYPERLINKS|TDF_ALLOW_DIALOG_CANCELLATION|TDF_USE_HICON_MAIN,TDCBF_OK_BUTTON};
				CString title=LoadStringEx(IDS_UPDATE_TITLE);
				task.pszWindowTitle=title;
				task.pszContent=error;
				task.hMainIcon=LoadIcon(NULL,IDI_ERROR);
				task.pfCallback=TaskDialogCallbackProc;
				TaskDialogIndirect(&task,NULL,NULL,NULL);
			}
		}
	}
	return 0;
}

LRESULT CUpdateDlg::OnDontRemind( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	CRegKey regKey;
	if (regKey.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\OpenShell",KEY_READ|KEY_WRITE)!=ERROR_SUCCESS)
		regKey.Create(HKEY_CURRENT_USER,L"Software\\OpenShell\\OpenShell");
	if (m_Data.bNewVersion)
	{
		m_Data.bIgnoreVersion=(IsDlgButtonChecked(IDC_CHECKDONT)==BST_CHECKED);
		regKey.SetDWORDValue(L"RemindedVersion",m_Data.bIgnoreVersion?m_Data.newVersion:0);
	}
	return 0;
}

LRESULT CUpdateDlg::OnWeb( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	ShellExecute(m_hWnd,NULL,L"https://open-shell.github.io/Open-Shell-Menu/",NULL,NULL,SW_SHOWNORMAL);
	return 0;
}

void CUpdateDlg::NewVersionCallback( VersionData &data )
{
	g_UpdateDlg.m_Data.Swap(data);
}

void CUpdateDlg::UpdateData( void )
{
	if (!CheckForNewVersion(m_hWnd,COMPONENT_UPDATE,CHECK_UPDATE,NewVersionCallback))
	{
		m_Data.Clear();
	}
}

void CUpdateDlg::UpdateUI( void )
{
	if (m_Data.bValid)
	{
		if (m_Data.bNewVersion)
		{
			SetDlgItemText(IDC_STATICLATEST,LoadStringEx(IDS_OUTOFDATE));
			SetDlgItemText(IDC_EDITTEXT,m_Data.news);
			GetDlgItem(IDC_EDITTEXT).ShowWindow(SW_SHOW);
			GetDlgItem(IDC_BUTTONDOWNLOAD).ShowWindow(SW_SHOW);
			bool check=true;
			if (g_Settings[SETTING_UPDATE].value.vt==VT_I4)
				check=g_Settings[SETTING_UPDATE].value.intVal!=0;
			GetDlgItem(IDC_CHECKDONT).ShowWindow(check?SW_SHOW:SW_HIDE);
			CheckDlgButton(IDC_CHECKDONT,m_Data.bIgnoreVersion?BST_CHECKED:BST_UNCHECKED);
			TOOLINFO tool={sizeof(tool),TTF_SUBCLASS|TTF_IDISHWND,m_hWnd,(UINT_PTR)GetDlgItem(IDC_BUTTONDOWNLOAD).m_hWnd};
			tool.lpszText=(LPWSTR)(LPCWSTR)m_Data.downloadUrl;
			m_Tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);
		}
		else
		{
			SetDlgItemText(IDC_STATICLATEST,LoadStringEx(IDS_UPDATED));
			SetDlgItemText(IDC_EDITTEXT,L"");
			GetDlgItem(IDC_EDITTEXT).ShowWindow(SW_HIDE);
			GetDlgItem(IDC_BUTTONDOWNLOAD).ShowWindow(SW_HIDE);
			GetDlgItem(IDC_CHECKDONT).ShowWindow(SW_HIDE);
		}
	}
	else
	{
		SetDlgItemText(IDC_STATICLATEST,(m_Data.newVersion==0)?LoadStringEx(IDS_UPDATE_FAIL).GetString():L"");
		SetDlgItemText(IDC_EDITTEXT,L"");
		GetDlgItem(IDC_EDITTEXT).ShowWindow(SW_HIDE);
		GetDlgItem(IDC_BUTTONDOWNLOAD).ShowWindow(SW_HIDE);
		GetDlgItem(IDC_CHECKDONT).ShowWindow(SW_HIDE);
	}
	Invalidate();
}

void CUpdateDlg::Run( void )
{
	if (m_hWnd)
		return;

	DLGTEMPLATE *pTemplate=LoadDialogEx(IDD_UPDATE);
	Create(NULL,pTemplate);
	MSG msg;
	while (m_hWnd && GetMessage(&msg,0,0,0))
	{
		if ((msg.hwnd==m_hWnd || IsChild(msg.hwnd)) && IsDialogMessage(&msg))
			continue;
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

///////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK SubclassBalloonProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_LBUTTONUP)
	{
		LRESULT res=DefSubclassProc(hWnd,uMsg,wParam,lParam);
		if (SendMessage(hWnd,TTM_GETCURRENTTOOL,0,0))
		{
			TOOLINFO tool={sizeof(tool)};
			tool.uId=1;
			SendMessage(hWnd,TTM_TRACKACTIVATE,FALSE,(LPARAM)&tool);
			g_UpdateDlg.Run();
		}
		return res;
	}
	if (uMsg==WM_MOUSEACTIVATE)
		return MA_NOACTIVATE;
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

///////////////////////////////////////////////////////////////////////////////

class COwnerWindow: public CWindowImpl<COwnerWindow>
{
public:
	DECLARE_WND_CLASS_EX(L"OpenShellUpdate.COwnerWindow",0,COLOR_MENU)

	// message handlers
	BEGIN_MSG_MAP( COwnerWindow )
		MESSAGE_HANDLER( WM_CLEAR, OnClear )
	END_MSG_MAP()

protected:
	LRESULT OnClear( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
	{
		if (g_UpdateDlg)
		{
			g_UpdateDlg.PostMessage(WM_CLOSE);
		}
		return 0;
	}
};

///////////////////////////////////////////////////////////////////////////////

class UpdateToasts : public DesktopToasts
{
public:
	UpdateToasts() : DesktopToasts(L"OpenShell.Update") {}

private:
	void OnToastActivate(LPCWSTR invokedArgs) override
	{
		g_UpdateDlg.Run();
	}
};

///////////////////////////////////////////////////////////////////////////////

int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpstrCmdLine, int nCmdShow )
{
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
	Sprintf(mutexName,_countof(mutexName),L"OpenShellUpdate.Mutex.%s.%s",userName,deskName);
	free(deskName);

	HANDLE hMutex=CreateMutex(NULL,TRUE,mutexName);
	if (GetLastError()==ERROR_ALREADY_EXISTS || GetLastError()==ERROR_ACCESS_DENIED)
		return 0;

	CoInitialize(NULL);
	g_Instance=hInstance;
	InitSettings(g_Settings,COMPONENT_UPDATE,NULL);
	CString language=GetSettingString(L"Language");
	ParseTranslations(NULL,language);

	HINSTANCE resInstance=LoadTranslationDll(language);

	LoadTranslationResources(resInstance,g_LoadDialogs);

	if (resInstance)
		FreeLibrary(resInstance);

	int time0=timeGetTime();

	COwnerWindow ownerWindow;
	ownerWindow.Create(NULL,0,0,WS_POPUP);

	UpdateToasts toasts;

	if (wcsstr(lpstrCmdLine,L"-popup")!=NULL)
	{
		g_UpdateDlg.UpdateData();
		// wait at least 5 seconds before showing the balloon
		int sleep=5000-(timeGetTime()-time0);
		if (sleep>0)
			Sleep(sleep);

		auto title = LoadStringEx(IDS_UPDATE_TITLE);
		auto message = LoadStringEx(IDS_NEWVERSION);

		if (toasts)
		{
			toasts.DisplaySimpleToast(title, message);
		}
		else
		{
			HWND balloon = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | (IsLanguageRTL() ? WS_EX_LAYOUTRTL : 0), TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_CLOSE | TTS_NOPREFIX, 0, 0, 0, 0, NULL, NULL, g_Instance, NULL);
			SendMessage(balloon, TTM_SETMAXTIPWIDTH, 0, 500);
			TOOLINFO tool = { sizeof(tool),TTF_ABSOLUTE | TTF_TRANSPARENT | TTF_TRACK | (IsLanguageRTL() ? TTF_RTLREADING : 0U) };
			tool.uId = 1;
			tool.lpszText = (wchar_t*)(const wchar_t*)message;
			SendMessage(balloon, TTM_ADDTOOL, 0, (LPARAM)&tool);
			SendMessage(balloon, TTM_SETTITLE, (WPARAM)LoadIcon(g_Instance, MAKEINTRESOURCE(IDI_APPICON)), (LPARAM)(const wchar_t*)title);
			APPBARDATA appbar = { sizeof(appbar) };
			SHAppBarMessage(ABM_GETTASKBARPOS, &appbar);
			MONITORINFO info = { sizeof(info) };
			GetMonitorInfo(MonitorFromWindow(appbar.hWnd, MONITOR_DEFAULTTOPRIMARY), &info);
			SendMessage(balloon, TTM_TRACKPOSITION, 0, 0);
			SendMessage(balloon, TTM_TRACKACTIVATE, TRUE, (LPARAM)&tool);
			RECT rc;
			GetWindowRect(balloon, &rc);
			LONG pos;
			if (appbar.uEdge == ABE_LEFT)
				pos = MAKELONG(info.rcWork.left, info.rcWork.bottom - rc.bottom + rc.top);
			else if (appbar.uEdge == ABE_RIGHT)
				pos = MAKELONG(info.rcWork.right - rc.right + rc.left, info.rcWork.bottom - rc.bottom + rc.top);
			else if (appbar.uEdge == ABE_TOP)
				pos = MAKELONG(IsLanguageRTL() ? info.rcWork.left : info.rcWork.right - rc.right + rc.left, info.rcWork.top);
			else
				pos = MAKELONG(IsLanguageRTL() ? info.rcWork.left : info.rcWork.right - rc.right + rc.left, info.rcWork.bottom - rc.bottom + rc.top);
			SendMessage(balloon, TTM_TRACKPOSITION, 0, pos);
			SetWindowSubclass(balloon, SubclassBalloonProc, 0, 'CLSH');
			PlaySound(L"SystemNotification", NULL, SND_APPLICATION | SND_ALIAS | SND_ASYNC | SND_NODEFAULT | SND_SYSTEM);
			int time0 = timeGetTime();
			while (IsWindowVisible(balloon))
			{
				if (time0 && (timeGetTime() - time0) >= 15000)
				{
					time0 = 0;
					TOOLINFO tool = { sizeof(tool) };
					tool.uId = 1;
					SendMessage(balloon, TTM_TRACKACTIVATE, FALSE, (LPARAM)&tool);
				}
				MSG msg;
				while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
				{
					TranslateMessage(&msg);
					DispatchMessage(&msg);
				}
				Sleep(10);
			}
		}
	}
	else if (wcsstr(lpstrCmdLine, L"-ToastActivated"))
	{
		g_UpdateDlg.UpdateData();
		// dialog will be shown once toast is activated (UpdateToasts::OnToastActivate)
	}
	else
	{
		g_UpdateDlg.Run();
	}

	// process messages for a while
	for (int i = 0; i < 100; i++)
	{
		MSG msg;
		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		Sleep(10);
	}

	ownerWindow.DestroyWindow();
	CoUninitialize();
	return 0;
}
