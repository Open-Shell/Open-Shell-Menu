// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include <stdafx.h>
#include "resource.h"
#include "Settings.h"
#include "SettingsUIHelper.h"
#include "ResourceHelper.h"
#include "ComHelper.h"
#include "FNVHash.h"
#include "StringUtils.h"
#include "Translations.h"
#include "DownloadHelper.h"
#include <uxtheme.h>
#include <map>
#include <algorithm>

int g_LanguageIDs[]={
	0x0401,
	0x0402,
	0x0403,
	0x0405,
	0x0406,
	0x0407,
	0x0408,
	0x0409,
	0x0C0A,
	0x0425,
	0x0429,
	0x040B,
	0x040C,
	0x0491,
	0x040D,
	0x041A,
	0x040E,
	0x040F,
	0x0410,
	0x0411,
	0x0412,
	0x0427,
	0x0426,
	0x0414,
	0x0413,
	0x0415,
	0x0416,
	0x0816,
	0x0418,
	0x0419,
	0x041B,
	0x0424,
	0x081A,
	0x041D,
	0x041E,
	0x041F,
	0x0422,
	0x0804,
	0x0C04,
	0x0404,
};

int g_LanguageIDCount=_countof(g_LanguageIDs);

class CLanguageSettingsDlg: public CResizeableDlg<CLanguageSettingsDlg>
{
public:
	BEGIN_MSG_MAP( CLanguageSettingsDlg )
		MESSAGE_HANDLER( WM_INITDIALOG, OnInitDialog )
		MESSAGE_HANDLER( WM_DESTROY, OnDestroy )
		MESSAGE_HANDLER( WM_SIZE, OnSize )
		NOTIFY_HANDLER( IDC_LISTLANGUAGE, LVN_ITEMCHANGED, OnSelChange )
		NOTIFY_HANDLER( IDC_LISTLANGUAGE, LVN_ITEMCHANGING, OnSelChanging )
		NOTIFY_HANDLER( IDC_LISTLANGUAGE, NM_CUSTOMDRAW, OnCustomDraw )
	END_MSG_MAP()

	BEGIN_RESIZE_MAP
		RESIZE_CONTROL(IDC_LISTLANGUAGE,MOVE_SIZE_X|MOVE_SIZE_Y)
	END_RESIZE_MAP

	void SetGroup( CSetting *pGroup );
	void SetComponent( TSettingsComponent component ) { m_Component=component; }

protected:
	// Handler prototypes:
	//  LRESULT MessageHandler(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	//  LRESULT CommandHandler(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	//  LRESULT NotifyHandler(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSelChange( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnSelChanging( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnCustomDraw( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );

private:
	CSetting *m_pSetting;
	TSettingsComponent m_Component;
	CWindow m_Tooltip;
	HIMAGELIST m_ImageList;
	bool m_bLargeFlags;
	bool m_bLocked;

	struct LangInfo
	{
		CString name;
		int id;
		int image;
		bool operator<( const LangInfo &info ) { return _wcsicmp(name,info.name)<0; }
	};
	std::vector<LangInfo> m_LanguageIDs; // the order matches the items in the listbox

	void UpdateFlags( void );

	void AddFlag( const wchar_t *langName, int langId, HBITMAP bmp );
};

void CLanguageSettingsDlg::AddFlag( const wchar_t *langName, int langId, HBITMAP bmp )
{
	int idx=1;
	for (;idx<(int)m_LanguageIDs.size();idx++)
	{
		if (_wcsicmp(langName,m_LanguageIDs[idx].name)<=0)
			break;
	}
	if (idx==(int)m_LanguageIDs.size() || _wcsicmp(langName,m_LanguageIDs[idx].name)<0)
	{
		LangInfo info;
		info.id=langId;
		info.image=ImageList_AddMasked(m_ImageList,bmp,0xFF00FF);
		info.name=langName;
		m_LanguageIDs.insert(m_LanguageIDs.begin()+idx,info);

		CWindow list=GetDlgItem(IDC_LISTLANGUAGE);
		wchar_t name[256];
		GetLocaleInfo(info.id,LOCALE_SLANGUAGE,name,_countof(name));
		wchar_t text[256];
		Sprintf(text,_countof(text),L"%s - %s",info.name,name);
		LVITEM item={LVIF_IMAGE|LVIF_TEXT};
		item.iItem=idx;
		item.iImage=info.image;
		item.pszText=text;
		ListView_InsertItem(list,&item);
	}
}

void CLanguageSettingsDlg::UpdateFlags( void )
{
	// add flags from dlls
	for (int pass=0;pass<2;pass++)
	{
		wchar_t path[_MAX_PATH];
		if (pass==0)
		{
			GetModuleFileName(g_Instance,path,_countof(path));
			*PathFindFileName(path)=0;
		}
		else
		{
			Strcpy(path,_countof(path),L"%ALLUSERSPROFILE%\\OpenShell\\Languages");
			DoEnvironmentSubst(path,_countof(path));
		}

		wchar_t find[_MAX_PATH];
		Sprintf(find,_countof(find),L"%s\\*.dll",path);
		WIN32_FIND_DATA data;
		HANDLE h=FindFirstFile(find,&data);
		while (h!=INVALID_HANDLE_VALUE)
		{
			if (!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && wcschr(data.cFileName,'-'))
			{
				wchar_t fname[_MAX_PATH];
				Sprintf(fname,_countof(fname),L"%s\\%s",path,data.cFileName);
				wchar_t language[_MAX_PATH];
				Strcpy(language,_countof(language),data.cFileName);
				PathRemoveExtension(language);

				bool bFound=false;
				for (std::vector<LangInfo>::const_iterator it=m_LanguageIDs.begin();it!=m_LanguageIDs.end();++it)
				{
					if (_wcsicmp(it->name,language)==0)
					{
						bFound=true;
						break;
					}
				}
				int langId=0;
				if (!bFound && GetLocaleInfoEx(language,LOCALE_ILANGUAGE|LOCALE_RETURN_NUMBER,(LPWSTR)&langId,4))
				{
					HINSTANCE hModule=LoadLibraryEx(fname,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
					if (hModule)
					{
						HBITMAP bmp;
						if (m_bLargeFlags)
							bmp=(HBITMAP)LoadImage(hModule,MAKEINTRESOURCE(1),IMAGE_BITMAP,32,16,LR_CREATEDIBSECTION);
						else
							bmp=(HBITMAP)LoadImage(hModule,MAKEINTRESOURCE(2),IMAGE_BITMAP,24,11,LR_CREATEDIBSECTION);
						if (bmp)
						{
							AddFlag(language,langId,bmp);
							DeleteObject(bmp);
						}
						FreeLibrary(hModule);
					}
				}
			}
			if (!FindNextFile(h,&data))
			{
				FindClose(h);
				break;
			}
		}
	}
}

LRESULT CLanguageSettingsDlg::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	m_bLocked=false;
	InitResize();
	EnableThemeDialogTexture(m_hWnd,ETDT_ENABLETAB);

	CWindow list=GetDlgItem(IDC_LISTLANGUAGE);
	if (IsAppThemed())
		SetWindowTheme(list,L"Explorer",NULL);
	HBITMAP bmp;
	HDC hdc=::GetDC(NULL);
	int dpi=GetDeviceCaps(hdc,LOGPIXELSY);
	::ReleaseDC(NULL,hdc);
	m_bLargeFlags=dpi>=144;
	if (m_bLargeFlags)
	{
		m_ImageList=ImageList_Create(32,16,ILC_COLOR24|ILC_MASK|(IsLanguageRTL()?ILC_MIRROR:0),0,0);
		bmp=(HBITMAP)LoadImage(_AtlBaseModule.GetResourceInstance(),MAKEINTRESOURCE(IDB_FLAGS24),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
	}
	else
	{
		m_ImageList=ImageList_Create(24,11,ILC_COLOR24|ILC_MASK|(IsLanguageRTL()?ILC_MIRROR:0),0,0);
		bmp=(HBITMAP)LoadImage(_AtlBaseModule.GetResourceInstance(),MAKEINTRESOURCE(IDB_FLAGS),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
	}
	ImageList_AddMasked(m_ImageList,bmp,0xFF00FF);
	DeleteObject(bmp);
	ListView_SetImageList(list,m_ImageList,LVSIL_SMALL);
	RECT rc;
	list.GetWindowRect(&rc);
	ListView_SetExtendedListViewStyleEx(list,LVS_EX_DOUBLEBUFFER,LVS_EX_DOUBLEBUFFER);
	LVCOLUMN column={LVCF_WIDTH,0,rc.right-rc.left};
	ListView_InsertColumn(list,0,&column);

	m_LanguageIDs.resize(_countof(g_LanguageIDs)+1);
	{
		wchar_t languages[100]={0};
		ULONG size=0;
		ULONG len=_countof(languages);
		GetUserPreferredUILanguages(MUI_LANGUAGE_ID,&size,languages,&len);
		wchar_t *end;
		m_LanguageIDs[0].id=wcstol(languages,&end,16);
		m_LanguageIDs[0].image=I_IMAGENONE;
	}

	for (int i=0;i<=(int)_countof(g_LanguageIDs);i++)
	{
		if (i>0)
		{
			m_LanguageIDs[i].id=g_LanguageIDs[i-1];
			m_LanguageIDs[i].image=i-1;
		}
		wchar_t name[256];
		GetLocaleInfo(m_LanguageIDs[i].id,LOCALE_SNAME,name,_countof(name));
		m_LanguageIDs[i].name=name;
	}

	std::sort(m_LanguageIDs.begin()+1,m_LanguageIDs.end());

	LVITEM item={LVIF_IMAGE|LVIF_TEXT};
	for (int i=0;i<(int)m_LanguageIDs.size();i++)
	{
		wchar_t name[256];
		GetLocaleInfo(m_LanguageIDs[i].id,LOCALE_SLANGUAGE,name,_countof(name));
		wchar_t text[256];
		if (i==0)
			Sprintf(text,_countof(text),L"%s (%s)",LoadStringEx(IDS_OS_LANGUAGE),m_LanguageIDs[i].name);
		else
			Sprintf(text,_countof(text),L"%s - %s",m_LanguageIDs[i].name,name);
		item.iItem=i;
		item.iImage=m_LanguageIDs[i].image;
		item.pszText=text;
		ListView_InsertItem(list,&item);
	}

	UpdateFlags();

	m_Tooltip.Create(TOOLTIPS_CLASS,m_hWnd,NULL,NULL,WS_POPUP|TTS_NOPREFIX);
	TOOLINFO tool={sizeof(tool),TTF_SUBCLASS|TTF_IDISHWND,m_hWnd,'CLSH'};
	tool.uId=(UINT_PTR)list.m_hWnd;
	tool.lpszText=(LPWSTR)L"";
	m_Tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	return TRUE;
}

LRESULT CLanguageSettingsDlg::OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	m_Tooltip.Detach();
	return 0;
}

LRESULT CLanguageSettingsDlg::OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	CResizeableDlg<CLanguageSettingsDlg>::OnSize();
	return 0;
}

LRESULT CLanguageSettingsDlg::OnSelChange( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	// set setting
	CWindow list=GetDlgItem(IDC_LISTLANGUAGE);
	int idx=ListView_GetNextItem(list,-1,LVNI_SELECTED);
	if (idx<0) return 0;

	const wchar_t *name=idx>0?m_LanguageIDs[idx].name.GetString():L"";
	CSettingsLockWrite lock;
	CComVariant val(name);
	if (m_pSetting->value!=val)
		SetSettingsDirty();
	m_pSetting->value=std::move(val);

	if (_wcsicmp(m_pSetting->value.bstrVal,m_pSetting->defValue.bstrVal)==0)
		m_pSetting->flags|=CSetting::FLAG_DEFAULT;
	else
		m_pSetting->flags&=~CSetting::FLAG_DEFAULT;
	return 0;
}

LRESULT CLanguageSettingsDlg::OnSelChanging( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	return m_bLocked?TRUE:FALSE;
}

LRESULT CLanguageSettingsDlg::OnCustomDraw( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	NMLVCUSTOMDRAW *pDraw=(NMLVCUSTOMDRAW*)pnmh;
	if (!m_bLocked)
	{
		bHandled=FALSE;
		return 0;
	}
	if (pDraw->nmcd.dwDrawStage==CDDS_PREPAINT)
		return CDRF_NOTIFYITEMDRAW;
	if (pDraw->nmcd.dwDrawStage!=CDDS_ITEMPREPAINT)
	{
		bHandled=FALSE;
		return 0;
	}
	pDraw->clrTextBk=GetSysColor(COLOR_BTNFACE);
	return CDRF_DODEFAULT;
}

static HRESULT CALLBACK TaskDialogCallbackProc( HWND hwnd, UINT uNotification, WPARAM wParam, LPARAM lParam, LONG_PTR dwRefData )
{
	if (uNotification==TDN_HYPERLINK_CLICKED)
	{
		ShellExecute(hwnd,L"open",(const wchar_t*)lParam,NULL,NULL,SW_SHOWNORMAL);
	}
	return S_OK;
}

void CLanguageSettingsDlg::SetGroup( CSetting *pGroup )
{
	m_bLocked=false;
	m_pSetting=pGroup+1;

	const wchar_t *language=L"";
	if (m_pSetting->value.vt==VT_BSTR && m_pSetting->value.bstrVal)
		language=m_pSetting->value.bstrVal;
	int len=Strlen(language);

	CWindow list=GetDlgItem(IDC_LISTLANGUAGE);
	for (int idx=0;idx<(int)m_LanguageIDs.size();idx++)
	{
		const wchar_t *name=idx>0?m_LanguageIDs[idx].name.GetString():L"";
		if (_wcsicmp(language,name)==0)
		{
			ListView_SetItemState(list,idx,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
			ListView_EnsureVisible(list,idx,FALSE);
			break;
		}
	}
	m_bLocked=m_pSetting->IsLocked();

	TOOLINFO tool={sizeof(tool),0,m_hWnd,'CLSH'};
	tool.uId=(UINT_PTR)list.m_hWnd;
	CString str;
	if (m_bLocked)
		str=LoadStringEx(IDS_SETTING_LOCKED);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	m_Tooltip.SendMessage(TTM_UPDATETIPTEXT,0,(LPARAM)&tool);
	ListView_SetBkColor(list,GetSysColor(m_bLocked?COLOR_BTNFACE:COLOR_WINDOW));
}

class CLanguageSettingsPanel: public ISettingsPanel
{
public:
	CLanguageSettingsPanel( TSettingsComponent component ) { s_Dialog.SetComponent(component); }
	virtual HWND Create( HWND parent );
	virtual HWND Activate( CSetting *pGroup, const RECT &rect, bool bReset );
	virtual bool Validate( HWND parent ) { return true; }

private:
	static CLanguageSettingsDlg s_Dialog;
};

CLanguageSettingsDlg CLanguageSettingsPanel::s_Dialog;

ISettingsPanel *GetLanguageSettings( TSettingsComponent component )
{
	static CLanguageSettingsPanel panel(component);
	return &panel;
}

HWND CLanguageSettingsPanel::Create( HWND parent )
{
	if (!s_Dialog.m_hWnd)
		s_Dialog.Create(parent,LoadDialogEx(IDD_LANGUAGE));
	return s_Dialog.m_hWnd;
}

HWND CLanguageSettingsPanel::Activate( CSetting *pGroup, const RECT &rect, bool bReset )
{
	s_Dialog.SetGroup(pGroup);
	s_Dialog.SetWindowPos(HWND_TOP,&rect,SWP_SHOWWINDOW);
	return s_Dialog.m_hWnd;
}
