// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include "resource.h"
#include "StartMenuDLL.h"
#include "StartButton.h"
#include "Settings.h"
#include "SkinManager.h"
#include "FNVHash.h"
#include "SettingsUIHelper.h"
#include "LanguageSettingsHelper.h"
#include "SettingsUI.h"
#include "ResourceHelper.h"
#include "MenuContainer.h"
#include "Translations.h"
#include "CustomMenu.h"
#include "dllmain.h"
#include <uxtheme.h>
#include <dwmapi.h>
#include <htmlhelp.h>
#define SECURITY_WIN32
#include <Security.h>

const int DEFAULT_GLASS_OPACITY=50; // 50%
const int DEFAULT_TASK_OPACITY7=25; // 25%
const int DEFAULT_TASK_OPACITY8=55; // 55%
const int DEFAULT_TASK_OPACITY10=85; // 85%

///////////////////////////////////////////////////////////////////////////////

CString RgbToBgr(const wchar_t* str)
{
	CString retval;
	retval.Format(L"%06X", RgbToBgr(ParseColor(str)));

	return retval;
}

CString BgrToRgb(const wchar_t* str)
{
	return RgbToBgr(str);
}

class CSkinSettingsDlg: public CResizeableDlg<CSkinSettingsDlg>
{
public:
	enum {
		IDC_EDITBOX=101,
		IDC_BROWSEBTN=102,
		WM_EDITKEY=WM_APP+11,
	};

	CSkinSettingsDlg( void );
	void Create( MenuSkin::TSkinType skinType, HWND hWndParent, DLGTEMPLATE *pTemplate );

	BEGIN_MSG_MAP( CSkinSettingsDlg )
		MESSAGE_HANDLER( WM_INITDIALOG, OnInitDialog )
		MESSAGE_HANDLER( WM_DESTROY, OnDestroy )
		MESSAGE_HANDLER( WM_SIZE, OnSize )
		MESSAGE_HANDLER( WM_EDITKEY, OnEditKey )
		COMMAND_HANDLER( IDC_EDITBOX, EN_CHANGE, OnEditChange )
		COMMAND_HANDLER( IDC_EDITBOX, EN_KILLFOCUS, OnKillFocus )
		COMMAND_HANDLER( IDC_BROWSEBTN, BN_CLICKED, OnBrowse )
		COMMAND_HANDLER( IDC_BROWSEBTN, BN_KILLFOCUS, OnKillFocus )
		COMMAND_HANDLER( IDC_COMBOSKIN, CBN_SELENDOK, OnSelEndOK )
		COMMAND_HANDLER( IDC_ABOUT, BN_CLICKED, OnAbout )
		COMMAND_HANDLER( IDC_BUTTONRESET, BN_CLICKED, OnReset )
		NOTIFY_HANDLER( IDC_SKINOPTIONS, NM_CUSTOMDRAW, OnCustomDraw )
		NOTIFY_HANDLER( IDC_SKINOPTIONS, TVN_SELCHANGED, OnSelChanged )
		NOTIFY_HANDLER( IDC_SKINOPTIONS, TVN_ITEMEXPANDING, OnExpanding )
		NOTIFY_HANDLER( IDC_SKINOPTIONS, NM_CLICK, OnClick )
		NOTIFY_HANDLER( IDC_SKINOPTIONS, NM_DBLCLK, OnClick )
		NOTIFY_HANDLER( IDC_SKINOPTIONS, TVN_KEYDOWN, OnKeyDown )
		NOTIFY_HANDLER( IDC_SKINOPTIONS, TVN_GETINFOTIP, OnGetInfoTip )
	END_MSG_MAP()

	BEGIN_RESIZE_MAP
		RESIZE_CONTROL(IDC_COMBOSKIN,MOVE_SIZE_X)
		RESIZE_CONTROL(IDC_ABOUT,MOVE_MOVE_X)
		RESIZE_CONTROL(IDC_BUTTONRESET,MOVE_MOVE_X)
		RESIZE_CONTROL(IDC_STATICVER,MOVE_SIZE_X)
		RESIZE_CONTROL(IDC_SKINOPTIONS,MOVE_SIZE_X|MOVE_SIZE_Y)
		RESIZE_CONTROL(IDC_STATICALLPROGS,MOVE_SIZE_X|MOVE_MOVE_Y)
	END_RESIZE_MAP

	void SetGroup( CSetting *pGroup );

protected:
	// Handler prototypes:
	//  LRESULT MessageHandler(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	//  LRESULT CommandHandler(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	//  LRESULT NotifyHandler(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSelEndOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnAbout( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnReset( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnCustomDraw( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnSelChanged( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnExpanding( int idCtrl, LPNMHDR pnmh, BOOL& bHandled ) { return TRUE; } // prevent collapsing
	LRESULT OnClick( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnKeyDown( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnGetInfoTip( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnEditKey( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnEditChange( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnKillFocus( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnBrowse( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );

private:

	MenuSkin::TSkinType m_SkinType;
	MenuSkin m_CurrentSkin;
	CWindow m_Tree;
	CWindow m_EditBox;
	CWindow m_BrowseButton;
	TSkinOptionType m_EditMode;
	HTREEITEM m_EditItem;
	int m_EditItemIndex;
	bool m_bLoadSuccess;
	bool m_bIgnoreFocus;
	CSetting *m_pSetting;
	std::vector<CString> m_SkinNames;
	int m_SkinIndex;
	int m_VariationIndex;

	void InitSkinUI( void );
	void UpdateSkinSettings( void );
	void StoreSkinOptions( void );
	void ToggleItem( HTREEITEM hItem );
	void ItemSelected( HTREEITEM hItem, int index, bool bEnabled );
	void ApplyEditBox( void );
	void UpdateEditPosition( void );
};

CSkinSettingsDlg::CSkinSettingsDlg( void )
{
	m_SkinType=MenuSkin::SKIN_TYPE_COUNT;
	m_pSetting=NULL;
	m_SkinIndex=m_VariationIndex=0;
	m_EditItem=NULL;
	m_EditMode=SKIN_OPTION_NONE;
	m_EditItemIndex=-1;
	m_bIgnoreFocus=false;
	m_bLoadSuccess=false;
}

void CSkinSettingsDlg::Create( MenuSkin::TSkinType skinType, HWND hWndParent, DLGTEMPLATE *pTemplate )
{
	m_SkinType=skinType;
	CResizeableDlg<CSkinSettingsDlg>::Create(hWndParent,pTemplate);
}

// Subclass the edit control to forward some keyboard messages to the parent and to act as a hotkey control
static LRESULT CALLBACK SubclassEditProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_CHAR && wParam==VK_RETURN)
		return 0;
	if (uMsg==WM_GETDLGCODE && wParam==VK_RETURN)
		return DLGC_WANTALLKEYS;
	if (uMsg==WM_KEYDOWN)
	{
		if (wParam==VK_UP || wParam==VK_DOWN || wParam==VK_PRIOR || wParam==VK_NEXT || (wParam==VK_SPACE && GetKeyState(VK_CONTROL)<0))
			return SendMessage(GetParent(hWnd),uMsg,wParam,lParam);
		if (wParam==VK_RETURN || wParam==VK_ESCAPE)
			return SendMessage(GetParent(GetParent(hWnd)),CSkinSettingsDlg::WM_EDITKEY,wParam,0);
	}
	if (uMsg==WM_KILLFOCUS)
	{
		if (wParam && (HWND)wParam==GetDlgItem(GetParent(hWnd),CSkinSettingsDlg::IDC_BROWSEBTN))
			return 0;
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

// Subclass the tooltip to delay the tip when the mouse moves from one tree item to the next
static LRESULT CALLBACK SubclassInfoTipProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==TTM_UPDATE)
	{
		int time=(int)SendMessage(hWnd,TTM_GETDELAYTIME,TTDT_RESHOW,0);
		SetTimer(hWnd,'CLSH',time,NULL);
		return 0;
	}
	if (uMsg==WM_TIMER && wParam=='CLSH')
	{
		KillTimer(hWnd,wParam);
		DefSubclassProc(hWnd,TTM_UPDATE,0,0);
		return 0;
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

// Subclass the tree control to forward the command messages to the parent
static LRESULT CALLBACK SubclassTreeProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_COMMAND)
		return SendMessage(GetParent(hWnd),uMsg,wParam,lParam);
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

LRESULT CSkinSettingsDlg::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	InitResize();
	EnableThemeDialogTexture(m_hWnd,ETDT_ENABLETAB);

	m_Tree=GetDlgItem(IDC_SKINOPTIONS);
	SetWindowSubclass(m_Tree,SubclassTreeProc,'CLSH',0);
	TreeView_SetImageList(m_Tree,GetSettingsImageList(m_Tree),TVSIL_NORMAL);
	m_Tree.SendMessage(TVM_SETEXTENDEDSTYLE,TVS_EX_DOUBLEBUFFER,TVS_EX_DOUBLEBUFFER);
	m_Tree.SetWindowLong(GWL_STYLE,m_Tree.GetWindowLong(GWL_STYLE)|TVS_SHOWSELALWAYS); // should be in the dialog template, but I don't want to have to edit all language DLLs
	if (IsAppThemed())
	{
		m_Tree.SetWindowLong(GWL_STYLE,m_Tree.GetWindowLong(GWL_STYLE)|TVS_TRACKSELECT);
		SetWindowTheme(m_Tree,L"Explorer",NULL);
	}

	m_SkinNames.clear();
	m_SkinNames.push_back(LoadStringEx(IDS_DEFAULT_SKIN));
	wchar_t find[_MAX_PATH];
	GetSkinsPath(find);
	Strcat(find,_countof(find),L"1.txt");
	if (GetFileAttributes(find)!=INVALID_FILE_ATTRIBUTES)
	{
		m_SkinNames.push_back(L"Custom");
	}

	*PathFindFileName(find)=0;
	Strcat(find,_countof(find),m_SkinType==MenuSkin::SKIN_TYPE_WIN7?L"*.skin7":L"*.skin");
	WIN32_FIND_DATA data;
	HANDLE h=FindFirstFile(find,&data);
	while (h!=INVALID_HANDLE_VALUE)
	{
		if (!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))
		{
			*PathFindExtension(data.cFileName)=0;
			m_SkinNames.push_back(data.cFileName);
		}
		if (!FindNextFile(h,&data))
		{
			FindClose(h);
			break;
		}
	}

	CWindow skins=GetDlgItem(IDC_COMBOSKIN);
	for (std::vector<CString>::const_iterator it=m_SkinNames.begin();it!=m_SkinNames.end();++it)
		skins.SendMessage(CB_ADDSTRING,0,(LPARAM)(const wchar_t *)*it);

	CWindow tooltip=TreeView_GetToolTips(m_Tree);
	tooltip.SendMessage(TTM_SETDELAYTIME,TTDT_AUTOPOP,10000);
	tooltip.SendMessage(TTM_SETDELAYTIME,TTDT_INITIAL,1000);
	tooltip.SendMessage(TTM_SETDELAYTIME,TTDT_RESHOW,1000);
	SetWindowSubclass(tooltip,SubclassInfoTipProc,'CLSH',0);

	TOOLINFO tool={sizeof(tool),TTF_SUBCLASS,m_hWnd,'CLSH'};
	CString str=LoadStringEx(IDS_SETTING_LOCKED);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	skins.GetClientRect(&tool.rect);
	skins.MapWindowPoints(m_hWnd,&tool.rect);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	GetDlgItem(IDC_STATICALLPROGS).ShowWindow(m_SkinType==MenuSkin::SKIN_TYPE_ALL_PROGRAMS?SW_SHOW:SW_HIDE);

	HINSTANCE hInstance=_AtlBaseModule.GetResourceInstance();
	HFONT font=m_Tree.GetFont();
	m_EditBox=CreateWindow(L"EDIT",NULL,WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,m_Tree,(HMENU)IDC_EDITBOX,hInstance,NULL);
	m_EditBox.SetFont(font);
	SetWindowSubclass(m_EditBox,SubclassEditProc,'CLSH',0);
	m_BrowseButton=CreateWindow(L"BUTTON",L"...",WS_CHILD|BS_PUSHBUTTON,0,0,0,0,m_Tree,(HMENU)IDC_BROWSEBTN,hInstance,NULL);
	m_BrowseButton.SetFont(font);
	m_EditMode=SKIN_OPTION_NONE;
	m_EditItem=NULL;
	m_EditItemIndex=-1;
	m_bIgnoreFocus=false;

	return TRUE;
}

LRESULT CSkinSettingsDlg::OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	m_EditMode=SKIN_OPTION_NONE;
	bHandled=FALSE;
	return 0;
}

// Fills the tree with the options for the current skin
void CSkinSettingsDlg::InitSkinUI( void )
{
	m_Tree.SetRedraw(FALSE);
	TreeView_DeleteAllItems(m_Tree);
	m_VariationIndex=-1;
	m_EditItemIndex=-1;
	m_EditMode=SKIN_OPTION_NONE;
	m_BrowseButton.ShowWindow(SW_HIDE);
	m_EditBox.ShowWindow(SW_HIDE);

	m_bLoadSuccess=m_CurrentSkin.LoadMenuSkin(m_SkinNames[m_SkinIndex],NULL,L"",m_SkinType,0,96);
	if (!m_bLoadSuccess)
		m_CurrentSkin.Reset();

	if (m_CurrentSkin.Version>MAX_SKIN_VERSION)
	{
		GetDlgItem(IDC_STATICVER).ShowWindow(SW_SHOW);
		m_CurrentSkin.Variations.clear();
		m_CurrentSkin.Options.clear();
	}
	else
		GetDlgItem(IDC_STATICVER).ShowWindow(SW_HIDE);

	CWindow label=GetDlgItem(IDC_STATICOPT);

	if (m_CurrentSkin.Options.empty() && m_CurrentSkin.Variations.empty())
	{
		label.ShowWindow(SW_HIDE);
		m_Tree.SetRedraw(TRUE);
		m_Tree.ShowWindow(SW_HIDE);
	}
	else
	{
		// init variations
		if (!m_CurrentSkin.Variations.empty())
		{
			const wchar_t *variaton=L"";
			if (m_pSetting[1].value.vt==VT_BSTR)
				variaton=m_pSetting[1].value.bstrVal;

			m_VariationIndex=0;
			CString vars=LoadStringEx(IDC_SKINVARIATION);
			TVINSERTSTRUCT insert={NULL,TVI_LAST,{TVIF_STATE|TVIF_TEXT|TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_PARAM,NULL,TVIS_EXPANDED,TVIS_EXPANDED|TVIS_OVERLAYMASK,(LPWSTR)(LPCWSTR)vars,0,SETTING_STATE_SETTING,SETTING_STATE_SETTING}};
			insert.item.lParam=(LPARAM)(m_pSetting+1);
			if (m_pSetting[1].IsLocked())
			{
				insert.item.state|=INDEXTOOVERLAYMASK(1);
				insert.item.iImage=insert.item.iSelectedImage=SETTING_STATE_SETTING|SETTING_STATE_DISABLED;
			}
			HTREEITEM hVars=TreeView_InsertItem(m_Tree,&insert);
			int n=(int)m_CurrentSkin.Variations.size();
			for (int i=0;i<n;i++)
			{
				const MenuSkin::Variation &var=m_CurrentSkin.Variations[i].second;
				if (_wcsicmp(var.label,variaton)==0 || _wcsicmp(var.labelEn,variaton)==0)
					m_VariationIndex=i;
				TVINSERTSTRUCT insert2={hVars,TVI_LAST,{TVIF_TEXT|TVIF_PARAM,NULL,0,0,(LPWSTR)(LPCWSTR)var.label}};
				insert2.item.lParam=-i-1;
				TreeView_InsertItem(m_Tree,&insert2);
			}
		}

		HTREEITEM hLastGroup=NULL;
		int n=(int)m_CurrentSkin.Options.size();
		for (int i=0;i<n;i++)
		{
			const MenuSkin::Option &option=m_CurrentSkin.Options[i];
			TVINSERTSTRUCT insert={NULL,TVI_LAST,{TVIF_TEXT|TVIF_PARAM|TVIF_STATE|TVIF_IMAGE|TVIF_SELECTEDIMAGE,NULL,TVIS_EXPANDED,TVIS_OVERLAYMASK|TVIS_EXPANDED,(LPWSTR)(LPCWSTR)(LPWSTR)(LPCWSTR)option.label}};
			if (option.type==SKIN_OPTION_GROUP)
				insert.item.iImage=insert.item.iSelectedImage=SETTING_STATE_SETTING;
			else if (option.groupId>=0)
				insert.hParent=hLastGroup;
			insert.item.lParam=i;
			if (m_pSetting[2].IsLocked())
				insert.item.state|=INDEXTOOVERLAYMASK(1);
			HTREEITEM hItem=TreeView_InsertItem(m_Tree,&insert);
			if (option.type==SKIN_OPTION_GROUP)
				hLastGroup=hItem;
		}
		UpdateSkinSettings();
		label.ShowWindow(SW_SHOW);
		m_Tree.SetRedraw(TRUE);
		m_Tree.Invalidate();
		m_Tree.ShowWindow(SW_SHOW);
	}
}

void CSkinSettingsDlg::UpdateSkinSettings( void )
{
	HTREEITEM hItem=TreeView_GetRoot(m_Tree);
	if (!hItem) return;
	TVITEM item={TVIF_PARAM|TVIF_IMAGE|TVIF_SELECTEDIMAGE,hItem};
	TreeView_GetItem(m_Tree,&item);
	if (item.lParam==(LPARAM)(m_pSetting+1))
	{
		// has variations
		for (HTREEITEM hVar=TreeView_GetChild(m_Tree,hItem);hVar;hVar=TreeView_GetNextSibling(m_Tree,hVar))
		{
			item.hItem=hVar;
			TreeView_GetItem(m_Tree,&item);
			int image=SETTING_STATE_RADIO;
			if (-1-(int)item.lParam==m_VariationIndex)
				image|=SETTING_STATE_CHECKED;
			if (m_pSetting[1].IsLocked())
				image|=SETTING_STATE_DISABLED;
			if (item.iImage!=image)
			{
				item.iImage=item.iSelectedImage=image;
				TreeView_SetItem(m_Tree,&item);
				RECT rc;
				TreeView_GetItemRect(m_Tree,hVar,&rc,FALSE);
				m_Tree.InvalidateRect(&rc);
			}
		}
		hItem=TreeView_GetNextSibling(m_Tree,hItem);
	}

	bool bLocked=m_pSetting[2].IsLocked();
	std::map<CString,CString> options;
	if (m_pSetting[2].value.vt==VT_BSTR)
		m_CurrentSkin.ParseOptionsString(m_pSetting[2].value.bstrVal,options);
	{
		std::vector<const wchar_t*> values;
		m_CurrentSkin.ComputeOptionStates(options,values,false);
	}

	HTREEITEM hLastGroup=NULL;
	while (hItem)
	{
		wchar_t text[256];
		item.hItem=hItem;
		item.pszText=text;
		TreeView_GetItem(m_Tree,&item);
		int idx=(int)item.lParam;
		const MenuSkin::Option &option=m_CurrentSkin.Options[idx];
		if (option.type==SKIN_OPTION_GROUP)
		{
			hLastGroup=hItem;
			hItem=TreeView_GetChild(m_Tree,hItem);
		}
		else
		{
			int image=option.groupId>=0?SETTING_STATE_RADIO:SETTING_STATE_CHECKBOX;
			if (option.bValue)
				image|=SETTING_STATE_CHECKED;
			if (!option.bEnabled || bLocked)
				image|=SETTING_STATE_DISABLED;
			if (option.bValue && option.type>SKIN_OPTION_BOOL)
				Sprintf(text,_countof(text),L"%s: %s",option.label,(option.type==SKIN_OPTION_COLOR)?BgrToRgb(option.sValue):option.sValue);
			else
				Sprintf(text,_countof(text),L"%s",option.label);

			item.iImage=item.iSelectedImage=image;
			item.mask|=TVIF_TEXT;
			TreeView_SetItem(m_Tree,&item);
			item.mask&=~TVIF_TEXT;
			RECT rc;
			TreeView_GetItemRect(m_Tree,hItem,&rc,FALSE);
			m_Tree.InvalidateRect(&rc);

			hItem=TreeView_GetNextSibling(m_Tree,hItem);
			if (!hItem && hLastGroup)
			{
				hItem=TreeView_GetNextSibling(m_Tree,hLastGroup);
				hLastGroup=NULL;
			}
		}
	}
}

void CSkinSettingsDlg::StoreSkinOptions( void )
{
	std::vector<wchar_t> buffer;
	m_CurrentSkin.SerializeOptionStates(buffer);

	CSettingsLockWrite lock;
	const wchar_t *strNew=&buffer[0];
	const wchar_t *strOld=m_pSetting[2].value.vt==VT_BSTR?m_pSetting[2].value.bstrVal:L"";
	if (wcscmp(strNew,strOld)!=0)
		SetSettingsDirty();

	m_pSetting[2].value=CComVariant(strNew);
}

LRESULT CSkinSettingsDlg::OnCustomDraw( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	NMTVCUSTOMDRAW *pDraw=(NMTVCUSTOMDRAW*)pnmh;
	if (pDraw->nmcd.dwDrawStage==CDDS_PREPAINT)
	{
		UpdateEditPosition();
		return CDRF_NOTIFYITEMDRAW;
	}
	else if (pDraw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT)
	{
		TVITEM item={TVIF_IMAGE|TVIF_STATE,(HTREEITEM)pDraw->nmcd.dwItemSpec,0,TVIS_SELECTED};
		TreeView_GetItem(m_Tree,&item);
		if ((item.iImage&SETTING_STATE_DISABLED) && (!(item.state&TVIS_SELECTED) || IsAppThemed()))
			pDraw->clrText=GetSysColor(COLOR_GRAYTEXT);
		return CDRF_NOTIFYPOSTPAINT;
	}
	else if (pDraw->nmcd.dwDrawStage==CDDS_ITEMPOSTPAINT)
	{
		if (IS_INTRESOURCE(pDraw->nmcd.lItemlParam) && m_CurrentSkin.Options[pDraw->nmcd.lItemlParam].bValue && m_CurrentSkin.Options[pDraw->nmcd.lItemlParam].type==SKIN_OPTION_COLOR)
		{
			RECT rc;
			if (TreeView_GetItemRect(m_Tree,(HTREEITEM)pDraw->nmcd.dwItemSpec,&rc,TRUE))
			{
				const wchar_t *str=m_CurrentSkin.Options[pDraw->nmcd.lItemlParam].sValue;
				SetDCBrushColor(pDraw->nmcd.hdc,ParseColor(str));
				SelectObject(pDraw->nmcd.hdc,GetStockObject(DC_BRUSH));
				SelectObject(pDraw->nmcd.hdc,GetStockObject(BLACK_PEN));
				Rectangle(pDraw->nmcd.hdc,rc.right,rc.top,rc.right+rc.bottom-rc.top,rc.bottom-1);
			}
		}
	}
	return CDRF_DODEFAULT;
}

LRESULT CSkinSettingsDlg::OnSelChanged( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	HTREEITEM hItem=TreeView_GetSelection(m_Tree);
	if (hItem)
	{
		TVITEM item={TVIF_PARAM|TVIF_IMAGE,hItem};
		TreeView_GetItem(m_Tree,&item);
		if (IS_INTRESOURCE(item.lParam))
			ItemSelected(item.hItem,(int)item.lParam,(item.iImage&SETTING_STATE_CHECKED)!=0);
		else
			ItemSelected(NULL,-1,false);
	}
	else
		ItemSelected(NULL,-1,false);
	return 0;
}

void CSkinSettingsDlg::ToggleItem( HTREEITEM hItem )
{
	if (!hItem) return;
	TVITEM item={TVIF_PARAM|TVIF_IMAGE,hItem};
	TreeView_GetItem(m_Tree,&item);
	if (item.iImage&SETTING_STATE_DISABLED)
		return;
	if (IS_INTRESOURCE(item.lParam))
	{
		const MenuSkin::Option &option=m_CurrentSkin.Options[item.lParam];
		if (option.groupId>=0)
		{
			// radio button
			if (item.iImage&SETTING_STATE_CHECKED)
				return;
			for (std::vector<MenuSkin::Option>::const_iterator it=m_CurrentSkin.Options.begin();it!=m_CurrentSkin.Options.end();++it)
			{
				if (it->type==SKIN_OPTION_GROUP || it->groupId!=option.groupId)
					continue;
				it->bValue=&option==&*it;
			}
		}
		else
		{
			option.bValue=!(item.iImage&SETTING_STATE_CHECKED);
		}
		StoreSkinOptions();
	}
	else if (IS_INTRESOURCE(-item.lParam))
	{
		// select variation
		if (item.iImage&SETTING_STATE_CHECKED)
			return;
		m_VariationIndex=-1-(int)item.lParam;
		CSettingsLockWrite lock;
		const wchar_t *strNew=m_CurrentSkin.Variations[m_VariationIndex].second.labelEn;
		const wchar_t *strOld=m_pSetting[1].value.vt==VT_BSTR?m_pSetting[1].value.bstrVal:L"???";
		if (wcscmp(strNew,strOld)!=0)
			SetSettingsDirty();
		m_pSetting[1].value=CComVariant(strNew);
	}
	UpdateSkinSettings();
	if (IS_INTRESOURCE(item.lParam))
		ItemSelected(hItem,(int)item.lParam,m_CurrentSkin.Options[item.lParam].bValue);
}

LRESULT CSkinSettingsDlg::OnClick( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	DWORD pos=GetMessagePos();
	TVHITTESTINFO test={{(short)LOWORD(pos),(short)HIWORD(pos)}};
	m_Tree.ScreenToClient(&test.pt);
	if (TreeView_HitTest(m_Tree,&test))
	{
		if (test.flags&(TVHT_ONITEMICON|TVHT_ONITEMLABEL))
		{
			if (test.flags&TVHT_ONITEMLABEL)
			{
				TVITEM item={TVIF_STATE|TVIF_PARAM,test.hItem};
				TreeView_GetItem(m_Tree,&item);
				if (!(item.state&TVIS_SELECTED) && IS_INTRESOURCE(item.lParam))
				{
					const MenuSkin::Option &option=m_CurrentSkin.Options[item.lParam];
					if (option.type>SKIN_OPTION_BOOL && option.bValue && option.groupId==-1)
						return 0;
				}
			}
			ToggleItem(test.hItem);
		}
		if (test.flags&TVHT_ONITEMRIGHT)
		{
			TVITEM item={TVIF_STATE|TVIF_PARAM,test.hItem};
			TreeView_GetItem(m_Tree,&item);
			if (item.lParam!=m_EditItemIndex && IS_INTRESOURCE(item.lParam))
			{
				const MenuSkin::Option &option=m_CurrentSkin.Options[item.lParam];
				if (option.type==SKIN_OPTION_COLOR && option.bValue)
				{
					RECT rc;
					if (TreeView_GetItemRect(m_Tree,item.hItem,&rc,TRUE) && test.pt.x>=rc.right && test.pt.x<rc.right+rc.bottom-rc.top)
					{
						if (!(item.state&TVIS_SELECTED))
							TreeView_SelectItem(m_Tree,item.hItem);
						ItemSelected(item.hItem,(int)item.lParam,true);
						SendMessage(WM_COMMAND,MAKEWPARAM(IDC_BROWSEBTN,BN_CLICKED));
					}
				}
			}
		}
	}

	return 0;
}

LRESULT CSkinSettingsDlg::OnKeyDown( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	NMTVKEYDOWN *pKey=(NMTVKEYDOWN*)pnmh;
	if (pKey->wVKey==VK_SPACE)
	{
		ToggleItem(TreeView_GetSelection(m_Tree));
		return 1;
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CSkinSettingsDlg::OnGetInfoTip( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	NMTVGETINFOTIP *pTip=(NMTVGETINFOTIP*)pnmh;
	TVITEM item={TVIF_STATE|TVIF_IMAGE|TVIF_PARAM,pTip->hItem,0,TVIS_OVERLAYMASK};
	TreeView_GetItem(m_Tree,&item);
	int len=0;
	if (IS_INTRESOURCE(item.lParam))
	{
		len=Strcpy(pTip->pszText,pTip->cchTextMax,m_CurrentSkin.Options[item.lParam].tip);
	}
	else if (IS_INTRESOURCE(-item.lParam))
	{
		len=Strcpy(pTip->pszText,pTip->cchTextMax,m_CurrentSkin.Variations[-1-item.lParam].second.tip);
	}
	if (item.state&TVIS_OVERLAYMASK)
	{
		Sprintf(pTip->pszText+len,pTip->cchTextMax-len,L"\r\n%s",LoadStringEx(IDS_SETTING_LOCKED));
	}
	return 0;
}

LRESULT CSkinSettingsDlg::OnEditKey( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==VK_RETURN && !(m_EditBox.GetWindowLong(GWL_STYLE)&ES_READONLY))
		ItemSelected(m_EditItem,m_EditItemIndex,true);
	return 0;
}

LRESULT CSkinSettingsDlg::OnEditChange( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	UpdateEditPosition();
	return 0;
}

LRESULT CSkinSettingsDlg::OnKillFocus( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	if (!m_bIgnoreFocus && m_EditItemIndex>=0)
	{
		ApplyEditBox();
		UpdateEditPosition();
	}
	return 0;
}

LRESULT CSkinSettingsDlg::OnBrowse( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	if (m_EditMode==SKIN_OPTION_IMAGE)
	{
		m_bIgnoreFocus=true;
		CString str;
		m_EditBox.GetWindowText(str);
		str.TrimLeft(); str.TrimRight();
		wchar_t text[1024];
		if (_wcsicmp(PathFindExtension(str),L".bmp")==0 || _wcsicmp(PathFindExtension(str),L".png")==0 || _wcsicmp(PathFindExtension(str),L".jpg")==0)
		{
			Strcpy(text,_countof(text),str);
			DoEnvironmentSubst(text,_countof(text));
		}
		else
			text[0]=0;
		if (BrowseForBitmap(m_hWnd,text,true))
		{
			m_EditBox.SetWindowText(text);
		}
		SendMessage(WM_NEXTDLGCTL,(LPARAM)m_EditBox.m_hWnd,TRUE);
		m_EditBox.SetFocus();
		m_bIgnoreFocus=false;
	}
	else if (m_EditMode==SKIN_OPTION_COLOR)
	{
		m_bIgnoreFocus=true;
		CString str;
		m_EditBox.GetWindowText(str);
		str.TrimLeft(); str.TrimRight();
		COLORREF val=RgbToBgr(ParseColor(str));
		static COLORREF customColors[16];
		CHOOSECOLOR choose={sizeof(choose),m_hWnd,NULL,val,customColors};
		choose.Flags=CC_ANYCOLOR|CC_FULLOPEN|CC_RGBINIT;
		if (ChooseColor(&choose))
		{
			wchar_t text[100];
			Sprintf(text,_countof(text),L"%06X",BgrToRgb(choose.rgbResult));
			m_EditBox.SetWindowText(text);
			ApplyEditBox();
			m_Tree.Invalidate();
		}
		SendMessage(WM_NEXTDLGCTL,(LPARAM)m_EditBox.m_hWnd,TRUE);
		m_EditBox.SetFocus();
		m_bIgnoreFocus=false;
	}
	return 0;
}

void CSkinSettingsDlg::ApplyEditBox( void )
{
	if (m_EditItemIndex>=0)
	{
		CString str;
		m_EditBox.GetWindowText(str);
		str.TrimLeft(); str.TrimRight();
		auto& option=m_CurrentSkin.Options[m_EditItemIndex];
		if (option.type==SKIN_OPTION_COLOR)
			option.sValue=RgbToBgr(str);
		else
			option.sValue=str;
		StoreSkinOptions();
	}
}

void CSkinSettingsDlg::ItemSelected( HTREEITEM hItem, int index, bool bEnabled )
{
	if (m_EditItemIndex>=0)
	{
		ApplyEditBox();
		const MenuSkin::Option &option=m_CurrentSkin.Options[m_EditItemIndex];
		wchar_t text[256];
		if (option.bValue && option.type>SKIN_OPTION_BOOL)
			Sprintf(text,_countof(text),L"%s: %s",option.label,(option.type==SKIN_OPTION_COLOR)?BgrToRgb(option.sValue):option.sValue);
		else
			Sprintf(text,_countof(text),L"%s",option.label);
		TVITEM item={TVIF_TEXT,m_EditItem,0,0,text};
		TreeView_SetItem(m_Tree,&item);
		m_EditItemIndex=-1;
	}
	m_EditItem=NULL;
	TSkinOptionType mode=SKIN_OPTION_NONE;
	CString text;
	if (index>=0)
	{
		const MenuSkin::Option &option=m_CurrentSkin.Options[index];
		if (option.type>SKIN_OPTION_BOOL)
			mode=option.type;
		if (option.type==SKIN_OPTION_COLOR)
			text=BgrToRgb(option.sValue);
		else
			text=option.sValue;
	}

	RECT rc;
	m_EditMode=mode;
	if (mode!=SKIN_OPTION_NONE)
	{
		CString str=m_CurrentSkin.Options[index].label;
		TVITEM item={TVIF_TEXT,hItem,0,0,(LPWSTR)(LPCWSTR)str};
		TreeView_SetItem(m_Tree,&item);
		m_Tree.GetClientRect(&rc);
		int w=rc.right;
		TreeView_GetItemRect(m_Tree,hItem,&rc,TRUE);
		rc.left=rc.right;
		rc.right=w;
		if (bEnabled && mode>SKIN_OPTION_BOOL)
		{
			m_EditItem=hItem;
			m_EditItemIndex=index;
		}
	}

	if (bEnabled && (mode==SKIN_OPTION_IMAGE || mode==SKIN_OPTION_COLOR))
	{
		RECT rc2=rc;
		int width=(rc2.bottom-rc2.top)*3/2;
		rc2.left=rc2.right-width;
		m_BrowseButton.SetWindowPos(NULL,&rc2,SWP_NOZORDER|SWP_SHOWWINDOW);
		m_BrowseButton.EnableWindow(bEnabled);
		rc.right=rc2.left;
	}
	else
		m_BrowseButton.ShowWindow(SW_HIDE);

	if (bEnabled && mode>SKIN_OPTION_BOOL)
	{
		m_EditBox.SetWindowText(text);
		m_EditBox.SendMessage(EM_SETREADONLY,!bEnabled,0);
		m_EditBox.SetWindowPos(NULL,&rc,SWP_NOZORDER|SWP_SHOWWINDOW);
		SendMessage(WM_NEXTDLGCTL,(LPARAM)m_EditBox.m_hWnd,TRUE);
	}
	else
		m_EditBox.ShowWindow(SW_HIDE);
}

void CSkinSettingsDlg::UpdateEditPosition( void )
{
	if (m_EditMode==SKIN_OPTION_NONE) return;

	RECT rc;
	m_Tree.GetClientRect(&rc);
	int w=rc.right;
	TreeView_GetItemRect(m_Tree,m_EditItem,&rc,TRUE);
	rc.left=rc.right;
	rc.right=w;
	w-=rc.left;
	int width=(rc.bottom-rc.top)*3/2;

	HDC hdc=CreateCompatibleDC(NULL);
	HFONT font0=(HFONT)SelectObject(hdc,m_EditBox.GetFont());
	SIZE size;
	CString str;
	m_EditBox.GetWindowText(str);
	GetTextExtentPoint(hdc,str,str.GetLength(),&size);
	SelectObject(hdc,font0);
	DeleteDC(hdc);
	DWORD margins=(DWORD)m_EditBox.SendMessage(EM_GETMARGINS);
	size.cx+=HIWORD(margins)+LOWORD(margins)+12;
	if (m_EditMode==SKIN_OPTION_IMAGE || m_EditMode==SKIN_OPTION_COLOR)
		size.cx+=width;
	if (size.cx<w)
		rc.right=rc.left+size.cx;

	if (m_EditMode==SKIN_OPTION_IMAGE || m_EditMode==SKIN_OPTION_COLOR)
	{
		RECT rc2=rc;
		rc2.left=rc2.right-width;
		m_BrowseButton.SetWindowPos(NULL,&rc2,SWP_NOZORDER|SWP_NOCOPYBITS);
		rc.right=rc2.left;
	}

	m_EditBox.SetWindowPos(NULL,&rc,SWP_NOZORDER|SWP_NOCOPYBITS);
}

LRESULT CSkinSettingsDlg::OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	CResizeableDlg<CSkinSettingsDlg>::OnSize();
	UpdateEditPosition();

	CWindow skins=GetDlgItem(IDC_COMBOSKIN);
	CWindow tooltip=TreeView_GetToolTips(m_Tree);
	TOOLINFO tool={sizeof(tool),TTF_SUBCLASS,m_hWnd,'CLSH'};
	skins.GetClientRect(&tool.rect);
	skins.MapWindowPoints(m_hWnd,&tool.rect);
	tooltip.SendMessage(TTM_NEWTOOLRECT,0,(LPARAM)&tool);

	return 0;
}

LRESULT CSkinSettingsDlg::OnSelEndOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	m_SkinIndex=(int)SendDlgItemMessage(IDC_COMBOSKIN,CB_GETCURSEL);
	{
		CSettingsLockWrite lock;
		const wchar_t *strNew=m_SkinIndex==0?L"":m_SkinNames[m_SkinIndex].GetString();
		const wchar_t *strOld=m_pSetting[0].value.vt==VT_BSTR?m_pSetting[0].value.bstrVal:L"???";
		if (wcscmp(strNew,strOld)!=0)
			SetSettingsDirty();
		m_pSetting[0].value=CComBSTR(strNew);
	}
	InitSkinUI();
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

LRESULT CSkinSettingsDlg::OnAbout( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	int idx=(int)SendDlgItemMessage(IDC_COMBOSKIN,CB_GETCURSEL,0,0);
	const wchar_t *name=m_SkinNames[idx];
	wchar_t caption[256];
	Sprintf(caption,_countof(caption),LoadStringEx(IDS_SKIN_ABOUT),name);
	if (!m_bLoadSuccess)
	{
		MessageBox(LoadStringEx(IDS_SKIN_FAIL),caption,MB_OK|MB_ICONERROR);
		return TRUE;
	}
	TASKDIALOGCONFIG task={sizeof(task),m_hWnd,NULL,TDF_ENABLE_HYPERLINKS|TDF_ALLOW_DIALOG_CANCELLATION|TDF_USE_HICON_MAIN,TDCBF_OK_BUTTON};
	task.pszWindowTitle=caption;
	task.pszContent=m_CurrentSkin.About;
	task.hMainIcon=m_CurrentSkin.AboutIcon?m_CurrentSkin.AboutIcon:LoadIcon(NULL,IDI_INFORMATION);
	task.pfCallback=TaskDialogCallbackProc;
	TaskDialogIndirect(&task,NULL,NULL,NULL);
	return TRUE;
}

LRESULT CSkinSettingsDlg::OnReset( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	{
		CSettingsLockWrite lock;
		if (!m_pSetting[0].IsLocked())
			m_pSetting[0].value=m_pSetting[0].defValue;
		if (!m_pSetting[1].IsLocked())
			m_pSetting[1].value=m_pSetting[1].defValue;
		if (!m_pSetting[2].IsLocked())
			m_pSetting[2].value=m_pSetting[2].defValue;
		SetSettingsDirty();
	}
	SetGroup(m_pSetting-1);
	return TRUE;
}

void CSkinSettingsDlg::SetGroup( CSetting *pGroup )
{
	m_pSetting=pGroup+1;
	// the skin settings are never default
	{
		CSettingsLockWrite lock;
		m_pSetting[0].flags&=~CSetting::FLAG_DEFAULT;
		m_pSetting[1].flags&=~CSetting::FLAG_DEFAULT;
		m_pSetting[2].flags&=~CSetting::FLAG_DEFAULT;
	}

	const wchar_t *skin=L"";
	if (m_pSetting[0].value.vt==VT_BSTR)
		skin=m_pSetting[0].value.bstrVal;

	m_SkinIndex=-1;
	if (!*skin)
		SendDlgItemMessage(IDC_COMBOSKIN,CB_SETCURSEL,0);
	else
	{
		int n=(int)m_SkinNames.size();
		for (int i=1;i<n;i++)
		{
			if (_wcsicmp(skin,m_SkinNames[i])==0)
			{
				SendDlgItemMessage(IDC_COMBOSKIN,CB_SETCURSEL,i);
				m_SkinIndex=i;
				break;
			}
		}
	}

	if (m_SkinIndex<0)
	{
		CSettingsLockWrite lock;
		m_pSetting[0].value=CComVariant(m_SkinNames[0]);
		m_pSetting[1].value=CComVariant(L"");
		m_pSetting[2].value=CComVariant(L"");
		SendDlgItemMessage(IDC_COMBOSKIN,CB_SETCURSEL,0);
		m_SkinIndex=0;
	}
	GetDlgItem(IDC_COMBOSKIN).EnableWindow(!m_pSetting[0].IsLocked());

	InitSkinUI();
}

class CSkinSettingsPanel: public ISettingsPanel
{
public:
	CSkinSettingsPanel( MenuSkin::TSkinType skinType ) { m_SkinType=skinType; }
	virtual HWND Create( HWND parent );
	virtual HWND Activate( CSetting *pGroup, const RECT &rect, bool bReset );
	virtual bool Validate( HWND parent ) { return true; }

private:
	MenuSkin::TSkinType m_SkinType;

	static CSkinSettingsDlg s_Dialogs[MenuSkin::SKIN_TYPE_COUNT];
};

CSkinSettingsDlg CSkinSettingsPanel::s_Dialogs[MenuSkin::SKIN_TYPE_COUNT];

HWND CSkinSettingsPanel::Create( HWND parent )
{
	CSkinSettingsDlg &dlg=s_Dialogs[m_SkinType];
	if (!dlg.m_hWnd)
		dlg.Create(m_SkinType,parent,LoadDialogEx(IDD_SKINSETTINGS));
	return dlg.m_hWnd;
}

HWND CSkinSettingsPanel::Activate( CSetting *pGroup, const RECT &rect, bool bReset )
{
	CSkinSettingsDlg &dlg=s_Dialogs[m_SkinType];
	dlg.SetGroup(pGroup);
	dlg.SetWindowPos(HWND_TOP,&rect,SWP_SHOWWINDOW);
	return dlg.m_hWnd;
}

static CSkinSettingsPanel g_SkinSettingsPanelClassic1(MenuSkin::SKIN_TYPE_CLASSIC1);
static CSkinSettingsPanel g_SkinSettingsPanelClassic2(MenuSkin::SKIN_TYPE_CLASSIC2);
static CSkinSettingsPanel g_SkinSettingsPanelWin7(MenuSkin::SKIN_TYPE_WIN7);
static CSkinSettingsPanel g_SkinSettingsPanelAll(MenuSkin::SKIN_TYPE_ALL_PROGRAMS);

///////////////////////////////////////////////////////////////////////////////

static const CStdCommand g_StdCommands[]={
	{L"SEPARATOR",IDS_SEPARATOR_ITEM,IDS_SEPARATOR_TIP},
	{L"COLUMN_BREAK",IDS_COLUMN_BREAK_ITEM,IDS_BREAK_TIP},
	{L"COLUMN_PADDING",IDS_COLUMN_PADDING_ITEM,IDS_PADDING_TIP},
	{L"programs",IDS_PROGRAMS_ITEM,IDS_PROGRAMS_TIP,L"ProgramsMenu",L"$Menu.Programs",L"$Menu.ProgramsTip",L"shell32.dll,326",&FOLDERID_Programs,StdMenuItem::MENU_TRACK},
	{L"apps",IDS_APPS_ITEM,IDS_APPS_TIP,L"AppsMenu",L"$Menu.Apps",L"",L",2",NULL,StdMenuItem::MENU_TRACK},
	{L"favorites",IDS_SHOW_FAVORITES,IDS_FAVORITES_TIP,L"FavoritesItem",L"$Menu.Favorites",L"",L"imageres.dll,1024",&FOLDERID_Favorites},
	{L"recent_documents",IDS_SHOW_DOCUMENTS,IDS_DOCUMENTS_TIP,L"RecentDocumentsItem",L"$Menu.Documents",L"",L"shell32.dll,327",&FOLDERID_Recent,StdMenuItem::MENU_ITEMS_FIRST},
	{L"computer",IDS_SHOW_COMPUTER,IDS_COMPUTER_TIP,L"ComputerItem",L"",L"",L"",&FOLDERID_ComputerFolder},
	{L"settings",IDS_SETTINGS_ITEM,IDS_SETTINGS_MENU_TIP,L"SettingsMenu",L"$Menu.Settings",L"",L"shell32.dll,330"},
	{L"search",IDS_SEARCH_MENU_ITEM,IDS_SEARCH_TIP,L"SearchMenu",L"$Menu.Search",L"",L"shell32.dll,323"},
	{L"search_box",IDS_SEARCH_BOX_ITEM,IDS_SEARCH_BOX_TIP,L"SearchBoxItem",L"$Menu.SearchBox",NULL,L"none",NULL,StdMenuItem::MENU_TRACK|StdMenuItem::MENU_OPENUP},
	{L"help",IDS_HELP_ITEM,IDS_HELP_TIP,L"HelpItem",L"$Menu.Help",L"$Menu.HelpTip",L"imageres.dll,99"},
	{L"run",IDS_RUN_ITEM,IDS_RUN_TIP,L"RunItem",L"$Menu.Run",L"$Menu.RunTip",L"shell32.dll,328"},
	{L"logoff",IDS_SHUTDOWN_LOGOFF,IDS_LOGOFF_TIP,L"LogOffItem",L"$Menu.Logoff",L"$Menu.LogOffTip",L"shell32.dll,325",NULL,StdMenuItem::MENU_STYLE_CLASSIC1},
	{L"logoff",IDS_SHUTDOWN_LOGOFF,IDS_LOGOFF_TIP,L"LogOffItem",L"$Menu.Logoff",L"$Menu.LogOffTip",L"none",NULL,StdMenuItem::MENU_STYLE_CLASSIC2},
	{L"undock",IDS_UNDOCK_ITEM,IDS_UNDOCK_TIP,L"UndockItem",L"$Menu.Undock",L"$Menu.UndockTip",L"shell32.dll,331",NULL,StdMenuItem::MENU_STYLE_CLASSIC1},
	{L"undock",IDS_UNDOCK_ITEM,IDS_UNDOCK_TIP,L"UndockItem",L"$Menu.Undock",L"$Menu.UndockTip",L"none",NULL,StdMenuItem::MENU_STYLE_CLASSIC2},
	{L"disconnect",IDS_DISCONNECT_ITEM,IDS_DISCONNECT_TIP,L"DisconnectItem",L"$Menu.Disconnect",L"$Menu.DisconnectTip",L"shell32.dll,329",NULL,StdMenuItem::MENU_STYLE_CLASSIC1},
	{L"disconnect",IDS_DISCONNECT_ITEM,IDS_DISCONNECT_TIP,L"DisconnectItem",L"$Menu.Disconnect",L"$Menu.DisconnectTip",L"none",NULL,StdMenuItem::MENU_STYLE_CLASSIC2},
	{L"shutdown_box",IDS_SHUTDOWN_SHUTDOWN_BOX,IDS_SHUTDOWNBOX_TIP,L"ShutdownBoxItem",L"$Menu.ShutdownBox",NULL,L"shell32.dll,329",NULL,StdMenuItem::MENU_SPLIT_BUTTON},
	{L"user_files",IDS_SHOW_USERFILES,IDS_USERFILES_TIP,L"UserFilesItem",NULL,L"$Menu.UserFilesTip",L"",&FOLDERID_UsersFiles},
	{L"user_documents",IDS_SHOW_USERDOCS,IDS_USERDOCS_TIP,L"UserDocumentsItem",NULL,L"$Menu.UserDocumentsTip",L"",&FOLDERID_Documents},
	{L"user_pictures",IDS_SHOW_USERPICS,IDS_USERPICS_TIP,L"UserPicturesItem",NULL,L"$Menu.UserPicturesTip",L"",&FOLDERID_Pictures},
	{L"control_panel",IDS_SHOW_CP,IDS_CONTROLPANEL_TIP,L"ControlPanelItem",L"$Menu.ControlPanel",L"$Menu.ControlPanelTip",L"shell32.dll,137",&FOLDERID_ControlPanelFolder,StdMenuItem::MENU_TRACK},
	{L"pc_settings",IDS_PCSETTINGS,IDS_PCSETTINGS_TIP,L"PCSettingsItem",L"$Menu.PCSettings",L"",L"%windir%\\ImmersiveControlPanel\\SystemSettings.exe,10",NULL,StdMenuItem::MENU_TRACK},
	{L"windows_security",IDS_SECURITY_ITEM,IDS_SECURITY_TIP,L"SecurityItem",L"$Menu.Security",L"$Menu.SecurityTip",L"shell32.dll,48"},
	{L"network_connections",IDS_SHOW_NETWORK,IDS_NETWORK_TIP,L"NetworkItem",L"$Menu.Network",L"$Menu.NetworkTip",L"shell32.dll,257",&FOLDERID_ConnectionsFolder},
	{L"printers",IDS_SHOW_PRINTERS,IDS_PRINTERS_TIP,L"PrintersItem",L"$Menu.Printers",L"$Menu.PrintersTip",L"shell32.dll,138",&FOLDERID_PrintersFolder},
	{L"taskbar_settings",IDS_TASKBAR_ITEM,IDS_TASKBAR_TIP,L"TaskbarSettingsItem",L"$Menu.Taskbar",L"$Menu.TaskbarTip",L"shell32.dll,40"},
	{L"programs_features",IDS_FEATURES_ITEM,IDS_FEATURES_TIP,L"ProgramsFeaturesItem",L"$Menu.Features",L"$Menu.FeaturesTip",L"shell32.dll,271"},
	{L"menu_settings",IDS_MENU_SETTINGS_ITEM,IDS_MENU_TIP,L"MenuSettingsItem",L"$Menu.ClassicSettings",L"$Menu.SettingsTip",L",1"},
	{L"search_files",IDS_SEARCH_FILES_ITEM,IDS_SEARCHFI_TIP,L"SearchFilesItem",L"$Menu.SearchFiles",L"$Menu.SearchFilesTip",L"shell32.dll,134"},
	{L"search_printer",IDS_SEARCH_PRINTER_ITEM,IDS_SEARCHPR_TIP,L"SearchPrinterItem",L"$Menu.SearchPrinter",L"$Menu.SearchPrintersTip",L"shell32.dll,1006"},
	{L"search_computers",IDS_SEARCH_COMP_ITEM,IDS_SEARCHCO_TIP,L"SearchComputersItem",L"$Menu.SearchComputers",L"$Menu.SearchComputersTip",L"shell32.dll,135"},
	{L"search_people",IDS_SEARCH_PEOPLE_ITEM,IDS_SEARCHPE_TIP,L"SearchPeopleItem",L"$Menu.SearchPeople",NULL,L"shell32.dll,269"},
	{L"sleep",IDS_SHUTDOWN_SLEEP,IDS_SLEEP_TIP,L"SleepItem",L"$Menu.Sleep",L"$Menu.SleepTip",L"none"},
	{L"hibernate",IDS_SHUTDOWN_HIBERNATE,IDS_HIBERNATE_TIP,L"HibernateItem",L"$Menu.Hibernate",L"$Menu.HibernateTip",L"none"},
	{L"restart",IDS_SHUTDOWN_RESTART,IDS_RESTART_TIP,L"RestartItem",L"$Menu.Restart",L"$Menu.RestartTip",L"none"},
	{L"restart_noupdate",IDS_SHUTDOWN_RESTART_NU,IDS_RESTART_NU_TIP,L"RestartItem",L"$Menu.Restart",L"$Menu.RestartTip",L"none"},
	{L"shutdown",IDS_SHUTDOWN_SHUTDOWN,IDS_SHUTDOWN_TIP,L"ShutdownItem",L"$Menu.Shutdown",L"$Menu.ShutdownTip",L"none"},
	{L"shutdown_noupdate",IDS_SHUTDOWN_SHUTDOWN_NU,IDS_SHUTDOWN_NU_TIP,L"ShutdownItem",L"$Menu.Shutdown",L"$Menu.ShutdownTip",L"none"},
	{L"switch_user",IDS_SHUTDOWN_SWITCH,IDS_SWITCH_TIP,L"SwitchUserItem",L"$Menu.SwitchUser",L"$Menu.SwitchUserTip",L"none"},
	{L"lock",IDS_SHUTDOWN_LOCK,IDS_LOCK_TIP,L"LockItem",L"$Menu.Lock",L"$Menu.LockTip",L"none"},
	{L"recent_programs",IDS_SHOW_RECENT,IDS_RECENT_TIP,L"RecentPrograms",NULL,NULL,L"none"},
	{L"monitor_off",IDS_MONITOR_OFF_ITEM,IDS_MONITOR_OFF_TIP,L"MonitorOffItem",L"$Menu.MonitorOff",NULL,L"none"},
	{L"",IDS_CUSTOM_ITEM,IDS_CUSTOM_TIP,L"CustomItem",NULL,NULL,L""},
	{NULL},
};

const wchar_t *g_DefaultStartMenu1=
L"Items=COLUMN_PADDING, ProgramsMenu, AppsMenu, COLUMN_BREAK, FavoritesItem, RecentDocumentsItem, ComputerItem, SettingsMenu, SearchMenu, HelpItem, RunItem, SEPARATOR, LogOffItem, UndockItem, DisconnectItem, ShutdownBoxItem, SearchBoxItem\n"
L"ProgramsMenu.Command=programs\n"
L"ProgramsMenu.Label=$Menu.Programs\n"
L"ProgramsMenu.Tip=$Menu.ProgramsTip\n"
L"ProgramsMenu.Icon=shell32.dll,326\n"
L"AppsMenu.Command=apps\n"
L"AppsMenu.Label=$Menu.Apps\n"
L"AppsMenu.Icon=,2\n"
L"FavoritesItem.Command=favorites\n"
L"FavoritesItem.Label=$Menu.Favorites\n"
L"FavoritesItem.Icon=imageres.dll,1024\n"
L"RecentDocumentsItem.Command=recent_documents\n"
L"RecentDocumentsItem.Items=UserFilesItem, UserDocumentsItem, UserPicturesItem\n"
L"RecentDocumentsItem.Label=$Menu.Documents\n"
L"RecentDocumentsItem.Icon=shell32.dll,327\n"
L"RecentDocumentsItem.Settings=ITEMS_FIRST\n"
L"SettingsMenu.Command=settings\n"
L"SettingsMenu.Items=PCSettingsItem, ControlPanelItem, SEPARATOR, SecurityItem, NetworkItem, PrintersItem, TaskbarSettingsItem, ProgramsFeaturesItem, SEPARATOR, MenuSettingsItem\n"
L"SettingsMenu.Label=$Menu.Settings\n"
L"SettingsMenu.Icon=shell32.dll,330\n"
L"SearchMenu.Command=search\n"
L"SearchMenu.Items=SearchFilesItem, SearchPrinterItem, SearchComputersItem, SearchPeopleItem\n"
L"SearchMenu.Label=$Menu.Search\n"
L"SearchMenu.Icon=shell32.dll,323\n"
L"ComputerItem.Command=computer\n"
L"HelpItem.Command=help\n"
L"HelpItem.Label=$Menu.Help\n"
L"HelpItem.Tip=$Menu.HelpTip\n"
L"HelpItem.Icon=imageres.dll,99\n"
L"RunItem.Command=run\n"
L"RunItem.Label=$Menu.Run\n"
L"RunItem.Tip=$Menu.RunTip\n"
L"RunItem.Icon=shell32.dll,328\n"
L"LogOffItem.Command=logoff\n"
L"LogOffItem.Label=$Menu.Logoff\n"
L"LogOffItem.Tip=$Menu.LogOffTip\n"
L"LogOffItem.Icon=shell32.dll,325\n"
L"UndockItem.Command=undock\n"
L"UndockItem.Label=$Menu.Undock\n"
L"UndockItem.Tip=$Menu.UndockTip\n"
L"UndockItem.Icon=shell32.dll,331\n"
L"DisconnectItem.Command=disconnect\n"
L"DisconnectItem.Label=$Menu.Disconnect\n"
L"DisconnectItem.Tip=$Menu.DisconnectTip\n"
L"DisconnectItem.Icon=shell32.dll,329\n"
L"ShutdownBoxItem.Command=shutdown_box\n"
L"ShutdownBoxItem.Label=$Menu.ShutdownBox\n"
L"ShutdownBoxItem.Icon=shell32.dll,329\n"
L"ShutdownBoxItem.Items=SwitchUserItem, LockItem, SleepItem, HibernateItem, RestartNUItem, RestartItem, ShutdownNUItem, ShutdownItem\n"
L"ShutdownBoxItem.Settings=SPLIT\n"
L"SearchBoxItem.Command=search_box\n"
L"SearchBoxItem.Label=$Menu.SearchBox\n"
L"SearchBoxItem.Icon=none\n"
L"SearchBoxItem.Settings=TRACK_RECENT, OPEN_UP\n"
L"UserFilesItem.Command=user_files\n"
L"UserFilesItem.Tip=$Menu.UserFilesTip\n"
L"UserDocumentsItem.Command=user_documents\n"
L"UserDocumentsItem.Tip=$Menu.UserDocumentsTip\n"
L"UserPicturesItem.Command=user_pictures\n"
L"UserPicturesItem.Tip=$Menu.UserPicturesTip\n"
L"ControlPanelItem.Command=control_panel\n"
L"ControlPanelItem.Icon=shell32.dll,137\n"
L"ControlPanelItem.Label=$Menu.ControlPanel\n"
L"ControlPanelItem.Tip=$Menu.ControlPanelTip\n"
L"ControlPanelItem.Settings=TRACK_RECENT\n"
L"PCSettingsItem.Command=pc_settings\n"
L"PCSettingsItem.Link=shell:appsfolder\\windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel\n"
L"PCSettingsItem.Label=$Menu.PCSettings\n"
L"PCSettingsItem.Settings=TRACK_RECENT\n"
L"SecurityItem.Command=windows_security\n"
L"SecurityItem.Label=$Menu.Security\n"
L"SecurityItem.Tip=$Menu.SecurityTip\n"
L"SecurityItem.Icon=shell32.dll,48\n"
L"NetworkItem.Command=network_connections\n"
L"NetworkItem.Icon=shell32.dll,257\n"
L"NetworkItem.Label=$Menu.Network\n"
L"NetworkItem.Tip=$Menu.NetworkTip\n"
L"PrintersItem.Command=printers\n"
L"PrintersItem.Icon=shell32.dll,138\n"
L"PrintersItem.Label=$Menu.Printers\n"
L"PrintersItem.Tip=$Menu.PrintersTip\n"
L"TaskbarSettingsItem.Command=taskbar_settings\n"
L"TaskbarSettingsItem.Label=$Menu.Taskbar\n"
L"TaskbarSettingsItem.Icon=shell32.dll,40\n"
L"TaskbarSettingsItem.Tip=$Menu.TaskbarTip\n"
L"ProgramsFeaturesItem.Command=programs_features\n"
L"ProgramsFeaturesItem.Label=$Menu.Features\n"
L"ProgramsFeaturesItem.Icon=shell32.dll,271\n"
L"ProgramsFeaturesItem.Tip=$Menu.FeaturesTip\n"
L"MenuSettingsItem.Command=menu_settings\n"
L"MenuSettingsItem.Label=$Menu.ClassicSettings\n"
L"MenuSettingsItem.Icon=,1\n"
L"MenuSettingsItem.Tip=$Menu.SettingsTip\n"
L"SearchFilesItem.Command=search_files\n"
L"SearchFilesItem.Label=$Menu.SearchFiles\n"
L"SearchFilesItem.Tip=$Menu.SearchFilesTip\n"
L"SearchFilesItem.Icon=shell32.dll,134\n"
L"SearchPrinterItem.Command=search_printer\n"
L"SearchPrinterItem.Label=$Menu.SearchPrinter\n"
L"SearchPrinterItem.Tip=$Menu.SearchPrintersTip\n"
L"SearchPrinterItem.Icon=shell32.dll,1006\n"
L"SearchComputersItem.Command=search_computers\n"
L"SearchComputersItem.Label=$Menu.SearchComputers\n"
L"SearchComputersItem.Tip=$Menu.SearchComputersTip\n"
L"SearchComputersItem.Icon=shell32.dll,135\n"
L"SearchPeopleItem.Command=search_people\n"
L"SearchPeopleItem.Label=$Menu.SearchPeople\n"
L"SearchPeopleItem.Icon=shell32.dll,269\n"
L"SwitchUserItem.Command=switch_user\n"
L"SwitchUserItem.Label=$Menu.SwitchUser\n"
L"SwitchUserItem.Tip=$Menu.SwitchUserTip\n"
L"SwitchUserItem.Icon=none\n"
L"LockItem.Command=lock\n"
L"LockItem.Label=$Menu.Lock\n"
L"LockItem.Tip=$Menu.LockTip\n"
L"LockItem.Icon=none\n"
L"SleepItem.Command=sleep\n"
L"SleepItem.Label=$Menu.Sleep\n"
L"SleepItem.Tip=$Menu.SleepTip\n"
L"SleepItem.Icon=none\n"
L"HibernateItem.Command=hibernate\n"
L"HibernateItem.Label=$Menu.Hibernate\n"
L"HibernateItem.Tip=$Menu.HibernateTip\n"
L"HibernateItem.Icon=none\n"
L"RestartNUItem.Command=restart_noupdate\n"
L"RestartNUItem.Label=$Menu.Restart\n"
L"RestartNUItem.Tip=$Menu.RestartTip\n"
L"RestartNUItem.Icon=none\n"
L"RestartItem.Command=restart\n"
L"RestartItem.Label=$Menu.Restart\n"
L"RestartItem.Tip=$Menu.RestartTip\n"
L"RestartItem.Icon=none\n"
L"ShutdownNUItem.Command=shutdown_noupdate\n"
L"ShutdownNUItem.Label=$Menu.Shutdown\n"
L"ShutdownNUItem.Tip=$Menu.ShutdownTip\n"
L"ShutdownNUItem.Icon=none\n"
L"ShutdownItem.Command=shutdown\n"
L"ShutdownItem.Label=$Menu.Shutdown\n"
L"ShutdownItem.Tip=$Menu.ShutdownTip\n"
L"ShutdownItem.Icon=none\n"
;

const wchar_t *g_DefaultStartMenu2=
L"Items=COLUMN_PADDING, ProgramsMenu, AppsMenu, SearchBoxItem, COLUMN_BREAK, FavoritesItem, UserFilesItem, UserDocumentsItem, UserPicturesItem, ComputerItem, RecentDocumentsItem, SEPARATOR, PCSettingsItem, ControlPanelItem, SecurityItem, NetworkItem, PrintersItem, SEPARATOR, SearchMenu, HelpItem, RunItem, COLUMN_PADDING, SEPARATOR, ShutdownBoxItem\n"
L"ProgramsMenu.Command=programs\n"
L"ProgramsMenu.Label=$Menu.Programs\n"
L"ProgramsMenu.Icon=shell32.dll,326\n"
L"AppsMenu.Command=apps\n"
L"AppsMenu.Label=$Menu.Apps\n"
L"AppsMenu.Icon=,2\n"
L"FavoritesItem.Command=favorites\n"
L"FavoritesItem.Label=$Menu.Favorites\n"
L"FavoritesItem.Icon=imageres.dll,1024\n"
L"ComputerItem.Command=computer\n"
L"RecentDocumentsItem.Command=recent_documents\n"
L"RecentDocumentsItem.Settings=ITEMS_FIRST\n"
L"SearchMenu.Command=search\n"
L"SearchMenu.Items=SearchFilesItem, SearchPrinterItem, SearchComputersItem, SearchPeopleItem\n"
L"SearchMenu.Label=$Menu.Search\n"
L"SearchMenu.Icon=shell32.dll,323\n"
L"HelpItem.Command=help\n"
L"HelpItem.Label=$Menu.Help\n"
L"HelpItem.Tip=$Menu.HelpTip\n"
L"HelpItem.Icon=imageres.dll,99\n"
L"RunItem.Command=run\n"
L"RunItem.Label=$Menu.Run\n"
L"RunItem.Tip=$Menu.RunTip\n"
L"RunItem.Icon=shell32.dll,328\n"
L"ShutdownBoxItem.Command=shutdown_box\n"
L"ShutdownBoxItem.Label=$Menu.ShutdownBox\n"
L"ShutdownBoxItem.Icon=shell32.dll,329\n"
L"ShutdownBoxItem.Items=SwitchUserItem, LogOffItem, LockItem, SEPARATOR, SleepItem, HibernateItem, SEPARATOR, RestartNUItem, RestartItem, UndockItem, DisconnectItem, ShutdownNUItem, ShutdownItem\n"
L"ShutdownBoxItem.Settings=SPLIT\n"
L"SearchBoxItem.Command=search_box\n"
L"SearchBoxItem.Label=$Menu.SearchBox\n"
L"SearchBoxItem.Icon=none\n"
L"SearchBoxItem.Settings=TRACK_RECENT, OPEN_UP\n"
L"UserFilesItem.Command=user_files\n"
L"UserFilesItem.Tip=$Menu.UserFilesTip\n"
L"UserDocumentsItem.Command=user_documents\n"
L"UserDocumentsItem.Tip=$Menu.UserDocumentsTip\n"
L"UserPicturesItem.Command=user_pictures\n"
L"UserPicturesItem.Tip=$Menu.UserPicturesTip\n"
L"ControlPanelItem.Command=control_panel\n"
L"ControlPanelItem.Icon=shell32.dll,137\n"
L"ControlPanelItem.Label=$Menu.ControlPanel\n"
L"ControlPanelItem.Tip=$Menu.ControlPanelTip\n"
L"ControlPanelItem.Settings=TRACK_RECENT\n"
L"PCSettingsItem.Command=pc_settings\n"
L"PCSettingsItem.Link=shell:appsfolder\\windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel\n"
L"PCSettingsItem.Label=$Menu.PCSettings\n"
L"PCSettingsItem.Settings=TRACK_RECENT\n"
L"SecurityItem.Command=windows_security\n"
L"SecurityItem.Icon=shell32.dll,48\n"
L"SecurityItem.Label=$Menu.Security\n"
L"SecurityItem.Tip=$Menu.SecurityTip\n"
L"NetworkItem.Command=network_connections\n"
L"NetworkItem.Icon=shell32.dll,257\n"
L"NetworkItem.Label=$Menu.Network\n"
L"NetworkItem.Tip=$Menu.NetworkTip\n"
L"PrintersItem.Command=printers\n"
L"PrintersItem.Icon=shell32.dll,138\n"
L"PrintersItem.Label=$Menu.Printers\n"
L"PrintersItem.Tip=$Menu.PrintersTip\n"
L"SearchFilesItem.Command=search_files\n"
L"SearchFilesItem.Label=$Menu.SearchFiles\n"
L"SearchFilesItem.Tip=$Menu.SearchFilesTip\n"
L"SearchFilesItem.Icon=shell32.dll,134\n"
L"SearchPrinterItem.Command=search_printer\n"
L"SearchPrinterItem.Label=$Menu.SearchPrinter\n"
L"SearchPrinterItem.Tip=$Menu.SearchPrintersTip\n"
L"SearchPrinterItem.Icon=shell32.dll,1006\n"
L"SearchComputersItem.Command=search_computers\n"
L"SearchComputersItem.Label=$Menu.SearchComputers\n"
L"SearchComputersItem.Tip=$Menu.SearchComputersTip\n"
L"SearchComputersItem.Icon=shell32.dll,135\n"
L"SearchPeopleItem.Command=search_people\n"
L"SearchPeopleItem.Label=$Menu.SearchPeople\n"
L"SearchPeopleItem.Icon=shell32.dll,269\n"
L"SwitchUserItem.Command=switch_user\n"
L"SwitchUserItem.Label=$Menu.SwitchUser\n"
L"SwitchUserItem.Tip=$Menu.SwitchUserTip\n"
L"SwitchUserItem.Icon=none\n"
L"LockItem.Command=lock\n"
L"LockItem.Label=$Menu.Lock\n"
L"LockItem.Tip=$Menu.LockTip\n"
L"LockItem.Icon=none\n"
L"LogOffItem.Command=logoff\n"
L"LogOffItem.Label=$Menu.LogOffShort\n"
L"LogOffItem.Icon=none\n"
L"LogOffItem.Tip=$Menu.LogOffTip\n"
L"SleepItem.Command=sleep\n"
L"SleepItem.Label=$Menu.Sleep\n"
L"SleepItem.Tip=$Menu.SleepTip\n"
L"SleepItem.Icon=none\n"
L"HibernateItem.Command=hibernate\n"
L"HibernateItem.Label=$Menu.Hibernate\n"
L"HibernateItem.Tip=$Menu.HibernateTip\n"
L"HibernateItem.Icon=none\n"
L"RestartNUItem.Command=restart_noupdate\n"
L"RestartNUItem.Label=$Menu.Restart\n"
L"RestartNUItem.Tip=$Menu.RestartTip\n"
L"RestartNUItem.Icon=none\n"
L"RestartItem.Command=restart\n"
L"RestartItem.Label=$Menu.Restart\n"
L"RestartItem.Tip=$Menu.RestartTip\n"
L"RestartItem.Icon=none\n"
L"UndockItem.Command=undock\n"
L"UndockItem.Label=$Menu.Undock\n"
L"UndockItem.Tip=$Menu.UndockTip\n"
L"UndockItem.Icon=none\n"
L"DisconnectItem.Command=disconnect\n"
L"DisconnectItem.Label=$Menu.Disconnect\n"
L"DisconnectItem.Tip=$Menu.DisconnectTip\n"
L"DisconnectItem.Icon=none\n"
L"ShutdownNUItem.Command=shutdown_noupdate\n"
L"ShutdownNUItem.Label=$Menu.Shutdown\n"
L"ShutdownNUItem.Tip=$Menu.ShutdownTip\n"
L"ShutdownNUItem.Icon=none\n"
L"ShutdownItem.Command=shutdown\n"
L"ShutdownItem.Label=$Menu.Shutdown\n"
L"ShutdownItem.Tip=$Menu.ShutdownTip\n"
L"ShutdownItem.Icon=none\n"
;

const wchar_t *g_DefaultStartMenu7=
L"Item1.Command=user_files\n"
L"Item1.Settings=NOEXPAND\n"
L"Item2.Command=lib_documents\n"
L"Item2.Settings=NOEXPAND\n"
L"Item3.Command=lib_pictures\n"
L"Item3.Settings=NOEXPAND\n"
L"Item4.Command=lib_music\n"
L"Item4.Settings=NOEXPAND\n"
L"Item5.Command=lib_videos\n"
L"Item5.Settings=ITEM_DISABLED\n"
L"Item6.Command=lib_tv\n"
L"Item6.Settings=ITEM_DISABLED\n"
L"Item7.Command=downloads\n"
L"Item7.Settings=ITEM_DISABLED\n"
L"Item8.Command=homegroup\n"
L"Item8.Settings=ITEM_DISABLED\n"
L"Item9.Command=separator\n"
L"Item10.Command=games\n"
L"Item10.Settings=TRACK_RECENT|NOEXPAND|ITEM_DISABLED\n"
L"Item11.Command=favorites\n"
L"Item11.Settings=ITEM_DISABLED\n"
L"Item12.Command=recent_documents\n"
L"Item13.Command=computer\n"
L"Item13.Settings=NOEXPAND\n"
L"Item14.Command=network\n"
L"Item14.Settings=ITEM_DISABLED\n"
L"Item15.Command=network_connections\n"
L"Item15.Settings=ITEM_DISABLED\n"
L"Item16.Command=separator\n"
L"Item17.Command=control_panel\n"
L"Item17.Settings=TRACK_RECENT\n"
L"Item18.Command=pc_settings\n"
L"Item18.Settings=TRACK_RECENT\n"
L"Item19.Command=admin\n"
L"Item19.Settings=TRACK_RECENT|ITEM_DISABLED\n"
L"Item20.Command=devices\n"
L"Item20.Settings=NOEXPAND\n"
L"Item21.Command=defaults\n"
L"Item22.Command=help\n"
L"Item23.Command=run\n"
L"Item24.Command=apps\n"
L"Item24.Settings=ITEM_DISABLED\n"
L"Item25.Command=windows_security\n"
;

const wchar_t *g_DefaultStartMenu781=
L"Item1.Command=user_files\n"
L"Item1.Settings=NOEXPAND\n"
L"Item2.Command=user_documents\n"
L"Item2.Settings=NOEXPAND\n"
L"Item3.Command=user_pictures\n"
L"Item3.Settings=NOEXPAND\n"
L"Item4.Command=user_music\n"
L"Item4.Settings=NOEXPAND\n"
L"Item5.Command=user_videos\n"
L"Item5.Settings=ITEM_DISABLED\n"
L"Item6.Command=downloads\n"
L"Item6.Settings=ITEM_DISABLED\n"
L"Item7.Command=homegroup\n"
L"Item7.Settings=ITEM_DISABLED\n"
L"Item8.Command=separator\n"
L"Item9.Command=games\n"
L"Item9.Settings=TRACK_RECENT|NOEXPAND|ITEM_DISABLED\n"
L"Item10.Command=favorites\n"
L"Item10.Settings=ITEM_DISABLED\n"
L"Item11.Command=recent_documents\n"
L"Item12.Command=computer\n"
L"Item12.Settings=NOEXPAND\n"
L"Item13.Command=network\n"
L"Item13.Settings=ITEM_DISABLED\n"
L"Item14.Command=network_connections\n"
L"Item14.Settings=ITEM_DISABLED\n"
L"Item15.Command=separator\n"
L"Item16.Command=pc_settings\n"
L"Item16.Settings=TRACK_RECENT\n"
L"Item17.Command=control_panel\n"
L"Item17.Settings=TRACK_RECENT\n"
L"Item18.Command=admin\n"
L"Item18.Settings=TRACK_RECENT|ITEM_DISABLED\n"
L"Item19.Command=devices\n"
L"Item19.Settings=NOEXPAND\n"
L"Item20.Command=defaults\n"
L"Item21.Command=help\n"
L"Item22.Command=run\n"
L"Item23.Command=apps\n"
L"Item23.Settings=ITEM_DISABLED\n"
L"Item24.Command=windows_security\n"
;

static const KNOWNFOLDERID *g_CommonLinks[]=
{
	&FOLDERID_CommonAdminTools,
	&FOLDERID_ComputerFolder,
	&FOLDERID_DesktopRoot,
	&FOLDERID_Desktop,
	&FOLDERID_Documents,
	&FOLDERID_Downloads,
	&FOLDERID_Fonts,
	&FOLDERID_Games,
	&FOLDERID_Links,
	&FOLDERID_Music,
	&FOLDERID_Pictures,
	&FOLDERID_RecycleBinFolder,
	&FOLDERID_Videos,
	&FOLDERID_Profile,
	&FOLDERID_HomeGroup,
	&FOLDERID_Libraries,
	&FOLDERID_DocumentsLibrary,
	&FOLDERID_MusicLibrary,
	&FOLDERID_PicturesLibrary,
	&FOLDERID_VideosLibrary,
	&FOLDERID_NetworkFolder,
	NULL,
};

///////////////////////////////////////////////////////////////////////////////

class CEditMenuDlg: public CEditCustomItemDlg
{
public:
	CEditMenuDlg( CTreeItem *pItem, std::vector<HMODULE> &modules, int style, int mask ): CEditCustomItemDlg(pItem,modules) { m_Style=style; m_StyleMask=mask; }

	BEGIN_MSG_MAP( CEditMenuDlg )
		MESSAGE_HANDLER( WM_INITDIALOG, OnInitDialog )
		COMMAND_ID_HANDLER( IDOK, OnOK )
		COMMAND_ID_HANDLER( IDCANCEL, OnCancel )
		COMMAND_HANDLER( IDC_COMBOCOMMAND, CBN_KILLFOCUS, OnCommandChanged )
		COMMAND_HANDLER( IDC_COMBOCOMMAND, CBN_SELENDOK, OnCommandChanged )
		COMMAND_HANDLER( IDC_BUTTONCOMMAND, BN_CLICKED, OnBrowseCommand )
		COMMAND_HANDLER( IDC_BUTTONLINK, BN_CLICKED, OnBrowseLink )
		COMMAND_HANDLER( IDC_BUTTONICON, BN_CLICKED, OnBrowseIcon )
		COMMAND_HANDLER( IDC_COMBOLINK, CBN_KILLFOCUS, OnLinkChanged )
		COMMAND_HANDLER( IDC_COMBOLINK, CBN_SELENDOK, OnLinkChanged )
		COMMAND_HANDLER( IDC_EDITICON, EN_KILLFOCUS, OnIconChanged )
		COMMAND_HANDLER( IDC_CHECKTRACK, BN_CLICKED, OnCheckTrack )
		COMMAND_HANDLER( IDC_CHECKNOTRACK, BN_CLICKED, OnCheckTrack )
		COMMAND_HANDLER( IDC_CHECKMULTICOLUMN, BN_CLICKED, OnCheckMulti )
		COMMAND_HANDLER( IDC_BUTTONRESET, BN_CLICKED, OnReset )
		CHAIN_MSG_MAP( CEditCustomItemDlg )
	END_MSG_MAP()

	virtual BEGIN_RESIZE_MAP
		RESIZE_CONTROL(IDC_COMBOCOMMAND,MOVE_SIZE_X)
		RESIZE_CONTROL(IDC_COMBOLINK,MOVE_SIZE_X)
		RESIZE_CONTROL(IDC_BUTTONCOMMAND,MOVE_MOVE_X)
		RESIZE_CONTROL(IDC_BUTTONLINK,MOVE_MOVE_X)
		RESIZE_CONTROL(IDC_EDITLABEL,MOVE_SIZE_X)
		RESIZE_CONTROL(IDC_EDITTIP,MOVE_SIZE_X)
		RESIZE_CONTROL(IDC_EDITICON,MOVE_SIZE_X)
		RESIZE_CONTROL(IDC_BUTTONICON,MOVE_MOVE_X)
		RESIZE_CONTROL(IDOK,MOVE_MOVE_X)
		RESIZE_CONTROL(IDCANCEL,MOVE_MOVE_X)
	END_RESIZE_MAP

protected:
	// Handler prototypes:
	//  LRESULT MessageHandler(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	//  LRESULT CommandHandler(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	//  LRESULT NotifyHandler(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnCommandChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnLinkChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnIconChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnCheckTrack( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnCheckMulti( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnBrowseCommand( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnBrowseLink( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnBrowseIcon( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnReset( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );

private:
	int m_Style, m_StyleMask;
};

LRESULT CEditMenuDlg::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	CWindow commands=GetDlgItem(IDC_COMBOCOMMAND);
	CWindow links=GetDlgItem(IDC_COMBOLINK);
	InitDialog(commands,g_StdCommands,m_Style,m_StyleMask,links,g_CommonLinks);
	SetDlgItemText(IDC_EDITLABEL,m_pItem->label);
	SetDlgItemText(IDC_EDITTIP,m_pItem->tip);
	SetDlgItemText(IDC_EDITICON,m_pItem->icon);

	BOOL bEnable=!(m_pItem->pStdCommand && m_pItem->pStdCommand->knownFolder);
	links.EnableWindow(bEnable);
	GetDlgItem(IDC_BUTTONLINK).EnableWindow(bEnable);
	GetDlgItem(IDC_BUTTONRESET).EnableWindow(m_pItem->pStdCommand && *m_pItem->pStdCommand->name);

	if (m_pItem->settings&StdMenuItem::MENU_TRACK)
		m_pItem->settings&=~StdMenuItem::MENU_NOTRACK;
	CheckDlgButton(IDC_CHECKSORTZA,(m_pItem->settings&StdMenuItem::MENU_SORTZA)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKSORTZAREC,(m_pItem->settings&StdMenuItem::MENU_SORTZA_REC)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKSORTONCE,(m_pItem->settings&StdMenuItem::MENU_SORTONCE)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKOPENUP,(m_pItem->settings&StdMenuItem::MENU_OPENUP)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKOPENUPREC,(m_pItem->settings&StdMenuItem::MENU_OPENUP_REC)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKNOEXPAND,(m_pItem->settings&StdMenuItem::MENU_NOEXPAND)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKNOTRACK,(m_pItem->settings&StdMenuItem::MENU_NOTRACK)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKITEMSFIRST,(m_pItem->settings&StdMenuItem::MENU_ITEMS_FIRST)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKINLINE,(m_pItem->settings&StdMenuItem::MENU_INLINE)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKSPLIT,(m_pItem->settings&StdMenuItem::MENU_SPLIT_BUTTON)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKNOEXT,(m_pItem->settings&StdMenuItem::MENU_NOEXTENSIONS)?BST_CHECKED:BST_UNCHECKED);
	if (m_pItem->pStdCommand && (wcscmp(m_pItem->pStdCommand->name,L"programs")==0 || wcscmp(m_pItem->pStdCommand->name,L"apps")==0))
	{
		CheckDlgButton(IDC_CHECKMULTICOLUMN,BST_CHECKED);
		GetDlgItem(IDC_CHECKMULTICOLUMN).EnableWindow(FALSE);
		CheckDlgButton(IDC_CHECKTRACK,(m_pItem->settings&StdMenuItem::MENU_NOTRACK)?BST_UNCHECKED:BST_CHECKED);
	}
	else
	{
		CheckDlgButton(IDC_CHECKMULTICOLUMN,(m_pItem->settings&StdMenuItem::MENU_MULTICOLUMN)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(IDC_CHECKTRACK,(m_pItem->settings&StdMenuItem::MENU_TRACK)?BST_CHECKED:BST_UNCHECKED);
	}

	UpdateIcons(IDC_ICONN,0);
	SendDlgItemMessage(IDC_EDITLABEL,EM_SETCUEBANNER,TRUE,(LPARAM)(const wchar_t*)LoadStringEx(IDS_NO_TEXT));

	CWindow tooltip=CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT,TOOLTIPS_CLASS,NULL,WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP,0,0,0,0,m_hWnd,NULL,g_Instance,NULL);
	tooltip.SendMessage(TTM_SETMAXTIPWIDTH,0,GetSystemMetrics(SM_CXSCREEN)/2);

	TOOLINFO tool={sizeof(tool),TTF_SUBCLASS|TTF_IDISHWND,m_hWnd,(UINT_PTR)(HWND)commands};
	CString str=LoadStringEx(IDS_COMMAND_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);
	tool.uId=(UINT_PTR)(HWND)commands.GetWindow(GW_CHILD);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_LINK_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)links;
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);
	tool.uId=(UINT_PTR)(HWND)links.GetWindow(GW_CHILD);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_TEXT_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_EDITLABEL);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_TIP_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_EDITTIP);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_ICON_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_EDITICON);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_SORTZA_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKSORTZA);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_SORTZAREC_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKSORTZAREC);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_SORTONCE_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKSORTONCE);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_OPENUP_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKOPENUP);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_OPENUPREC_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKOPENUPREC);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_NOEXPAND_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKNOEXPAND);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_MULTICOLUMN_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKMULTICOLUMN);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_TRACK_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKTRACK);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_NOTRACK_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKNOTRACK);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_ITEMSFIRST_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKITEMSFIRST);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_INLINE_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKINLINE);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_SPLIT_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKSPLIT);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_NOEXTENSIONS_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKNOEXT);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_RESTORE_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_BUTTONRESET);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);
	return TRUE;
}

LRESULT CEditMenuDlg::OnOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	GetDlgItemText(IDC_EDITLABEL,m_pItem->label);
	m_pItem->label.TrimLeft();
	m_pItem->label.TrimRight();
	GetDlgItemText(IDC_EDITTIP,m_pItem->tip);
	m_pItem->tip.TrimLeft();
	m_pItem->tip.TrimRight();

	m_pItem->settings=0;
	bool bForceMultiColumn=(m_pItem->pStdCommand && (wcscmp(m_pItem->pStdCommand->name,L"programs")==0 || wcscmp(m_pItem->pStdCommand->name,L"apps")==0));
	if (IsDlgButtonChecked(IDC_CHECKSORTZA)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_SORTZA;
	if (IsDlgButtonChecked(IDC_CHECKSORTZAREC)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_SORTZA_REC;
	if (IsDlgButtonChecked(IDC_CHECKSORTONCE)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_SORTONCE;
	if (IsDlgButtonChecked(IDC_CHECKOPENUP)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_OPENUP;
	if (IsDlgButtonChecked(IDC_CHECKOPENUPREC)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_OPENUP_REC;
	if (IsDlgButtonChecked(IDC_CHECKNOEXPAND)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_NOEXPAND;
	if (IsDlgButtonChecked(IDC_CHECKTRACK)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_TRACK;
	if (IsDlgButtonChecked(IDC_CHECKNOTRACK)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_NOTRACK;
	if (IsDlgButtonChecked(IDC_CHECKITEMSFIRST)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_ITEMS_FIRST;
	if (IsDlgButtonChecked(IDC_CHECKINLINE)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_INLINE;
	if (IsDlgButtonChecked(IDC_CHECKSPLIT)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_SPLIT_BUTTON;
	if (IsDlgButtonChecked(IDC_CHECKNOEXT)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_NOEXTENSIONS;
	if (bForceMultiColumn)
	{
		// special handling of the Programs menu
		// it is always MULTICOLUMN
		// it is always tracking, unless NOTRACK is set
		m_pItem->settings&=~StdMenuItem::MENU_TRACK;
	}
	else
	{
		if (IsDlgButtonChecked(IDC_CHECKMULTICOLUMN)==BST_CHECKED)
			m_pItem->settings|=StdMenuItem::MENU_MULTICOLUMN;
	}

	return CEditCustomItemDlg::OnOK(wNotifyCode,wID,hWndCtl,bHandled);
}

LRESULT CEditMenuDlg::OnCommandChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	CString text=GetComboText(wNotifyCode,wID);
	if (text==m_pItem->command) return 0;
	m_pItem->SetCommand(text,g_StdCommands,m_Style,m_StyleMask);
	BOOL bEnable=!(m_pItem->pStdCommand && m_pItem->pStdCommand->knownFolder);
	GetDlgItem(IDC_COMBOLINK).EnableWindow(bEnable);
	GetDlgItem(IDC_BUTTONLINK).EnableWindow(bEnable);
	GetDlgItem(IDC_BUTTONRESET).EnableWindow(m_pItem->pStdCommand && *m_pItem->pStdCommand->name);
	if (m_pItem->pStdCommand && (wcscmp(m_pItem->pStdCommand->name,L"programs")==0 || wcscmp(m_pItem->pStdCommand->name,L"apps")==0))
	{
		CheckDlgButton(IDC_CHECKMULTICOLUMN,BST_CHECKED);
		GetDlgItem(IDC_CHECKMULTICOLUMN).EnableWindow(FALSE);
	}
	else
	{
		CheckDlgButton(IDC_CHECKMULTICOLUMN,(m_pItem->settings&StdMenuItem::MENU_MULTICOLUMN)?BST_CHECKED:BST_UNCHECKED);
		GetDlgItem(IDC_CHECKMULTICOLUMN).EnableWindow(TRUE);
	}
	UpdateIcons(IDC_ICONN,0);
	return 0;
}

LRESULT CEditMenuDlg::OnLinkChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	CString text=GetComboText(wNotifyCode,wID);
	if (text==m_pItem->link) return 0;
	m_pItem->link=text;
	UpdateIcons(IDC_ICONN,0);
	return 0;
}

LRESULT CEditMenuDlg::OnIconChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	CString text;
	GetDlgItemText(IDC_EDITICON,text);
	text.TrimLeft();
	text.TrimRight();
	if (text==m_pItem->icon) return 0;
	m_pItem->icon=text;
	UpdateIcons(IDC_ICONN,0);
	return 0;
}

LRESULT CEditMenuDlg::OnCheckTrack( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	if (IsDlgButtonChecked(wID)==BST_CHECKED)
		CheckDlgButton(IDC_CHECKTRACK+IDC_CHECKNOTRACK-wID,BST_UNCHECKED);
	return 0;
}

LRESULT CEditMenuDlg::OnCheckMulti( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	if (IsDlgButtonChecked(IDC_CHECKMULTICOLUMN)==BST_CHECKED)
		m_pItem->settings|=StdMenuItem::MENU_MULTICOLUMN;
	else
		m_pItem->settings&=~StdMenuItem::MENU_MULTICOLUMN;
	return 0;
}

LRESULT CEditMenuDlg::OnBrowseCommand( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	wchar_t text[_MAX_PATH];
	GetDlgItemText(IDC_COMBOCOMMAND,text,_countof(text));
	if (BrowseCommandHelper(m_hWnd,text))
	{
		SetDlgItemText(IDC_COMBOCOMMAND,text);
		SendMessage(WM_COMMAND,MAKEWPARAM(IDC_COMBOCOMMAND,CBN_KILLFOCUS));
	}
	return 0;
}

LRESULT CEditMenuDlg::OnBrowseLink( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	wchar_t text[_MAX_PATH];
	GetDlgItemText(IDC_COMBOLINK,text,_countof(text));
	if (BrowseLinkHelper(m_hWnd,text,false))
	{
		SetDlgItemText(IDC_COMBOLINK,text);
		SendMessage(WM_COMMAND,MAKEWPARAM(IDC_COMBOLINK,CBN_KILLFOCUS));
	}
	return 0;
}

LRESULT CEditMenuDlg::OnBrowseIcon( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	wchar_t text[_MAX_PATH];
	GetDlgItemText(IDC_EDITICON,text,_countof(text));
	if (BrowseIconHelper(m_hWnd,text))
	{
		SetDlgItemText(IDC_EDITICON,text);
		SendMessage(WM_COMMAND,MAKEWPARAM(IDC_EDITICON,EN_KILLFOCUS));
	}
	return 0;
}

LRESULT CEditMenuDlg::OnReset( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	if (!m_pItem->pStdCommand || !*m_pItem->pStdCommand->name)
		return 0;
	m_pItem->link.Empty();
	m_pItem->label=m_pItem->pStdCommand->label;
	m_pItem->tip=m_pItem->pStdCommand->tip;
	m_pItem->icon=m_pItem->pStdCommand->icon;
	m_pItem->iconD=m_pItem->pStdCommand->iconD;
	m_pItem->settings=m_pItem->pStdCommand->settings&~m_StyleMask;
	SendDlgItemMessage(IDC_COMBOLINK,CB_SETCURSEL,-1);
	SetDlgItemText(IDC_EDITLABEL,m_pItem->label);
	SetDlgItemText(IDC_EDITTIP,m_pItem->tip);
	SetDlgItemText(IDC_EDITICON,m_pItem->icon);
	CheckDlgButton(IDC_CHECKSORTZA,(m_pItem->settings&StdMenuItem::MENU_SORTZA)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKSORTZAREC,(m_pItem->settings&StdMenuItem::MENU_SORTZA_REC)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKSORTONCE,(m_pItem->settings&StdMenuItem::MENU_SORTONCE)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKOPENUP,(m_pItem->settings&StdMenuItem::MENU_OPENUP)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKOPENUPREC,(m_pItem->settings&StdMenuItem::MENU_OPENUP_REC)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKNOEXPAND,(m_pItem->settings&StdMenuItem::MENU_NOEXPAND)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKMULTICOLUMN,(m_pItem->settings&StdMenuItem::MENU_MULTICOLUMN)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKTRACK,(m_pItem->settings&StdMenuItem::MENU_TRACK)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKNOTRACK,(m_pItem->settings&StdMenuItem::MENU_NOTRACK)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKITEMSFIRST,(m_pItem->settings&StdMenuItem::MENU_ITEMS_FIRST)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKINLINE,(m_pItem->settings&StdMenuItem::MENU_INLINE)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKSPLIT,(m_pItem->settings&StdMenuItem::MENU_SPLIT_BUTTON)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKNOEXT,(m_pItem->settings&StdMenuItem::MENU_NOEXTENSIONS)?BST_CHECKED:BST_UNCHECKED);
	if (m_pItem->pStdCommand && (wcscmp(m_pItem->pStdCommand->name,L"programs")==0 || wcscmp(m_pItem->pStdCommand->name,L"apps")==0))
	{
		CheckDlgButton(IDC_CHECKMULTICOLUMN,BST_CHECKED);
		GetDlgItem(IDC_CHECKMULTICOLUMN).EnableWindow(FALSE);
	}
	else
		GetDlgItem(IDC_CHECKMULTICOLUMN).EnableWindow(TRUE);

	UpdateIcons(IDC_ICONN,IDC_ICOND);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

class CCustomMenuDlg: public CCustomTreeDlg
{
public:
	CCustomMenuDlg( int style, int mask ): CCustomTreeDlg(true,g_StdCommands,style,mask) {}
	bool Validate( void );

	static void UpdateWarnings( TMenuStyle menuStyle );

protected:
	virtual void ParseTreeItemExtra( CTreeItem *pItem, CSettingsParser &parser );
	virtual void SerializeItemExtra( CTreeItem *pItem, std::vector<wchar_t> &stringBuilder );
	virtual bool EditItem( CTreeItem *pItem, HWND tree, HTREEITEM hItem, std::vector<HMODULE> &modules );
	virtual void ItemsChanged( void ) { UpdateWarnings(m_Style==StdMenuItem::MENU_STYLE_CLASSIC1?MENU_CLASSIC1:MENU_CLASSIC2); }
};

void CCustomMenuDlg::ParseTreeItemExtra( CTreeItem *pItem, CSettingsParser &parser )
{
	pItem->settings=0;
	wchar_t text[256];
	Sprintf(text,_countof(text),L"%s.Settings",pItem->name);
	const wchar_t *str=parser.FindSetting(text);
	if (!str) return;

	while (*str)
	{
		wchar_t token[256];
		str=GetToken(str,token,_countof(token),L", \t|;");
		if (_wcsicmp(token,L"OPEN_UP")==0) pItem->settings|=StdMenuItem::MENU_OPENUP;
		if (_wcsicmp(token,L"OPEN_UP_CHILDREN")==0) pItem->settings|=StdMenuItem::MENU_OPENUP_REC;
		if (_wcsicmp(token,L"SORT_ZA")==0) pItem->settings|=StdMenuItem::MENU_SORTZA;
		if (_wcsicmp(token,L"SORT_ZA_CHILDREN")==0) pItem->settings|=StdMenuItem::MENU_SORTZA_REC;
		if (_wcsicmp(token,L"SORT_ONCE")==0) pItem->settings|=StdMenuItem::MENU_SORTONCE;
		if (_wcsicmp(token,L"ITEMS_FIRST")==0) pItem->settings|=StdMenuItem::MENU_ITEMS_FIRST;
		if (_wcsicmp(token,L"TRACK_RECENT")==0) pItem->settings|=StdMenuItem::MENU_TRACK;
		if (_wcsicmp(token,L"NOTRACK_RECENT")==0) pItem->settings|=StdMenuItem::MENU_NOTRACK;
		if (_wcsicmp(token,L"NOEXPAND")==0) pItem->settings|=StdMenuItem::MENU_NOEXPAND;
		if (_wcsicmp(token,L"MULTICOLUMN")==0) pItem->settings|=StdMenuItem::MENU_MULTICOLUMN;
		if (_wcsicmp(token,L"INLINE")==0) pItem->settings|=StdMenuItem::MENU_INLINE;
		if (_wcsicmp(token,L"SPLIT")==0) pItem->settings|=StdMenuItem::MENU_SPLIT_BUTTON;
		if (_wcsicmp(token,L"NOEXTENSIONS")==0) pItem->settings|=StdMenuItem::MENU_NOEXTENSIONS;
	}
}

void CCustomMenuDlg::SerializeItemExtra( CTreeItem *pItem, std::vector<wchar_t> &stringBuilder )
{
	if (!pItem->settings) return;
	wchar_t text[256];
	Sprintf(text,_countof(text),L"%s.Settings=",pItem->name);
	AppendString(stringBuilder,text);
	if (pItem->settings&StdMenuItem::MENU_OPENUP) AppendString(stringBuilder,L"OPEN_UP|");
	if (pItem->settings&StdMenuItem::MENU_OPENUP_REC) AppendString(stringBuilder,L"OPEN_UP_CHILDREN|");
	if (pItem->settings&StdMenuItem::MENU_SORTZA) AppendString(stringBuilder,L"SORT_ZA|");
	if (pItem->settings&StdMenuItem::MENU_SORTZA_REC) AppendString(stringBuilder,L"SORT_ZA_CHILDREN|");
	if (pItem->settings&StdMenuItem::MENU_SORTONCE) AppendString(stringBuilder,L"SORT_ONCE|");
	if (pItem->settings&StdMenuItem::MENU_ITEMS_FIRST) AppendString(stringBuilder,L"ITEMS_FIRST|");
	if (pItem->settings&StdMenuItem::MENU_TRACK) AppendString(stringBuilder,L"TRACK_RECENT|");
	if (pItem->settings&StdMenuItem::MENU_NOTRACK) AppendString(stringBuilder,L"NOTRACK_RECENT|");
	if (pItem->settings&StdMenuItem::MENU_NOEXPAND) AppendString(stringBuilder,L"NOEXPAND|");
	if (pItem->settings&StdMenuItem::MENU_MULTICOLUMN) AppendString(stringBuilder,L"MULTICOLUMN|");
	if (pItem->settings&StdMenuItem::MENU_INLINE) AppendString(stringBuilder,L"INLINE|");
	if (pItem->settings&StdMenuItem::MENU_SPLIT_BUTTON) AppendString(stringBuilder,L"SPLIT|");
	if (pItem->settings&StdMenuItem::MENU_NOEXTENSIONS) AppendString(stringBuilder,L"NOEXTENSIONS|");
	stringBuilder[stringBuilder.size()-1]='\n';
}

bool CCustomMenuDlg::EditItem( CTreeItem *pItem, HWND tree, HTREEITEM hItem, std::vector<HMODULE> &modules )
{
	return CEditMenuDlg(pItem,modules,m_Style,m_StyleMask).Run(m_hWnd,IDD_CUSTOMMENU);
}

void CCustomMenuDlg::UpdateWarnings( TMenuStyle menuStyle )
{
	const CSetting *pSetting=FindSetting(menuStyle==MENU_CLASSIC1?L"MenuItems1":L"MenuItems2");
	Assert(pSetting->value.vt==VT_BSTR);
	CString items=pSetting->value.bstrVal;

	bool bWarning;
	items.MakeLower();
	items.Replace('\r','\n');
	bWarning=!wcsstr(items,L".command=favorites\n");
	UpdateSettingText(L"Favorites",-1,bWarning?IDS_SHOW_FAVORITES_TIP2:IDS_SHOW_FAVORITES_TIP,bWarning);

	bWarning=!wcsstr(items,L".command=recent_documents\n");
	UpdateSettingText(L"Documents",-1,bWarning?IDS_SHOW_DOCUMENTS_TIP2:IDS_SHOW_DOCUMENTS_TIP,bWarning);

	bWarning=!wcsstr(items,L".command=computer\n");
	UpdateSettingText(L"Computer",-1,bWarning?IDS_SHOW_COMPUTER_TIP2:IDS_SHOW_COMPUTER_TIP,bWarning);

	bWarning=!wcsstr(items,L".command=user_files\n");
	UpdateSettingText(L"UserFiles",-1,bWarning?IDS_SHOW_USERFILES_TIP2:IDS_SHOW_USERFILES_TIP,bWarning);

	bWarning=!wcsstr(items,L".command=user_documents\n");
	UpdateSettingText(L"UserDocuments",-1,bWarning?IDS_SHOW_USERDOCS_TIP2:IDS_SHOW_USERDOCS_TIP,bWarning);

	bWarning=!wcsstr(items,L".command=user_pictures\n");
	UpdateSettingText(L"UserPictures",-1,bWarning?IDS_SHOW_USERPICS_TIP2:IDS_SHOW_USERPICS_TIP,bWarning);

	bWarning=!wcsstr(items,L".command=control_panel\n");
	UpdateSettingText(L"ControlPanel",-1,bWarning?IDS_SHOW_CP_TIP2:IDS_SHOW_CP_TIP,bWarning);

	bWarning=!wcsstr(items,L".command=network_connections\n");
	UpdateSettingText(L"Network",-1,bWarning?IDS_SHOW_NETWORK_TIP2:IDS_SHOW_NETWORK_TIP,bWarning);

	bWarning=!wcsstr(items,L".command=printers\n");
	UpdateSettingText(L"Printers",-1,bWarning?IDS_SHOW_PRINTERS_TIP2:IDS_SHOW_PRINTERS_TIP,bWarning);

	bWarning=!wcsstr(items,L".command=shutdown_box\n");
	UpdateSettingText(L"Shutdown",-1,bWarning?IDS_SHOW_SHUTDOWN_TIP2:IDS_SHOW_SHUTDOWN_TIP,bWarning);

	bWarning=!wcsstr(items,L".command=search_box\n");
	UpdateSettingText(L"SearchBox",-1,bWarning?IDS_SHOW_SEARCH_BOX_TIP2:IDS_SHOW_SEARCH_BOX_TIP,bWarning);

	bWarning=!wcsstr(items,L".command=search\n");
	UpdateSettingText(L"Search",-1,bWarning?IDS_SHOW_SEARCH_TIP2:IDS_SHOW_SEARCH_TIP,bWarning);

	bWarning=!wcsstr(items,L".command=apps\n");
	UpdateSettingText(L"ShowAppsMenu",-1,bWarning?IDS_SHOW_APPS_MENU_TIP2:IDS_SHOW_APPS_MENU_TIP,bWarning);

	bWarning=!wcsstr(items,L".command=help\n");
	UpdateSettingText(L"Help",-1,bWarning?IDS_SHOW_HELP_TIP2:IDS_SHOW_HELP_TIP,bWarning);

	bWarning=!wcsstr(items,L".command=run\n");
	UpdateSettingText(L"Run",-1,bWarning?IDS_SHOW_RUN_TIP2:IDS_SHOW_RUN_TIP,bWarning);

	bWarning=!wcsstr(items,L".command=logoff\n");
	UpdateSettingText(L"LogOff",-1,bWarning?IDS_SHOW_LOGOFF_TIP2:IDS_SHOW_LOGOFF_TIP,bWarning);

	bWarning=!wcsstr(items,L".command=undock\n");
	UpdateSettingText(L"Undock",-1,bWarning?IDS_SHOW_UNDOCK_TIP2:IDS_SHOW_UNDOCK_TIP,bWarning);
}

class CCustomMenuPanel: public ISettingsPanel
{
public:
	CCustomMenuPanel( int index ) { m_Index=index; }
	virtual HWND Create( HWND parent );
	virtual HWND Activate( CSetting *pGroup, const RECT &rect, bool bReset );
	virtual bool Validate( HWND parent ) { return true; }

private:
	int m_Index;
	static CCustomMenuDlg s_Dialogs[2];
};

CCustomMenuDlg CCustomMenuPanel::s_Dialogs[2]={
	CCustomMenuDlg(StdMenuItem::MENU_STYLE_CLASSIC1,StdMenuItem::MENU_STYLE_MASK),
	CCustomMenuDlg(StdMenuItem::MENU_STYLE_CLASSIC2,StdMenuItem::MENU_STYLE_MASK),
};

HWND CCustomMenuPanel::Create( HWND parent )
{
	if (!s_Dialogs[m_Index].m_hWnd)
		s_Dialogs[m_Index].Create(parent,LoadDialogEx(IDD_CUSTOMTREE));
	return s_Dialogs[m_Index].m_hWnd;
}

HWND CCustomMenuPanel::Activate( CSetting *pGroup, const RECT &rect, bool bReset )
{
	s_Dialogs[m_Index].SetGroup(pGroup,bReset);
	s_Dialogs[m_Index].SetWindowPos(HWND_TOP,&rect,SWP_SHOWWINDOW);
	return s_Dialogs[m_Index].m_hWnd;
}

static CCustomMenuPanel g_CustomMenuPanel1(0);
static CCustomMenuPanel g_CustomMenuPanel2(1);

///////////////////////////////////////////////////////////////////////////////

class CCustomMenuDlg7: public CResizeableDlg<CCustomMenuDlg7>
{
public:
	CCustomMenuDlg7( void ) {}
	~CCustomMenuDlg7( void ) {}

	BEGIN_MSG_MAP( CCustomMenuDlg7 )
		MESSAGE_HANDLER( WM_INITDIALOG, OnInitDialog )
		MESSAGE_HANDLER( WM_SIZE, OnSize )
		MESSAGE_HANDLER( WM_CONTEXTMENU, OnContextMenu )
		REFLECT_NOTIFICATIONS()
	END_MSG_MAP()

	BEGIN_RESIZE_MAP
		RESIZE_CONTROL(IDC_LISTITEMS,MOVE_SIZE_X|MOVE_SIZE_Y)
		RESIZE_CONTROL(IDC_STATICHINT,MOVE_SIZE_X|MOVE_MOVE_Y)
	END_RESIZE_MAP

	void SetGroup( CSetting *pGroup, bool bReset );

protected:
	LRESULT OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnContextMenu( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );

private:

	struct MenuItem
	{
		CString command;
		CString link;
		CString label;
		CString tip;
		CString icon;
		unsigned int settings;
		int stdItemIndex;
	};

	class CItemList: public CWindowImpl<CItemList, CWindow>
	{
	public:
		CItemList( void ) { m_bDragging=m_bDisabled=false; m_Line=m_Column=-1; }
		enum {
			WM_RESIZE_COMBO=WM_USER+200,
			WM_HANDLE_TAB,
			TIMER_SCROLL_UP=1,
			TIMER_SCROLL_DOWN=2,
		};

		BEGIN_MSG_MAP( CSettingsTree )
			MESSAGE_HANDLER( WM_DESTROY, OnDestroy )
			MESSAGE_HANDLER( WM_LBUTTONDOWN, OnLButtonDown )
			MESSAGE_HANDLER( WM_LBUTTONDBLCLK, OnLButtonDblClick )
			MESSAGE_HANDLER( WM_LBUTTONUP, OnLButtonUp )
			MESSAGE_HANDLER( WM_MOUSEMOVE, OnMouseMove)
			MESSAGE_HANDLER( WM_RBUTTONDOWN, OnRButtonDown )
			MESSAGE_HANDLER( WM_GETDLGCODE, OnGetDlgCode )
			MESSAGE_HANDLER( WM_KEYDOWN, OnKeyDown )
			MESSAGE_HANDLER( WM_CAPTURECHANGED, OnCaptureChanged )
			MESSAGE_HANDLER( WM_HSCROLL, OnScroll )
			MESSAGE_HANDLER( WM_VSCROLL, OnScroll )
			MESSAGE_HANDLER( WM_TIMER, OnTimer )
			MESSAGE_HANDLER( WM_RESIZE_COMBO, OnResizeCombo )
			MESSAGE_HANDLER( WM_HANDLE_TAB, OnHandleTab )
			COMMAND_CODE_HANDLER( CBN_SELENDOK, OnSelEndOk )
			NOTIFY_CODE_HANDLER( HDN_ENDTRACK, OnEndTrack )
			NOTIFY_CODE_HANDLER( HDN_DIVIDERDBLCLICK, OnEndTrack )
			NOTIFY_CODE_HANDLER( HDN_ITEMCLICK, OnEndTrack )
			REFLECTED_NOTIFY_CODE_HANDLER( NM_CUSTOMDRAW, OnCustomDraw )
		END_MSG_MAP()

		void SetDisabled( bool bDisabled ) { m_bDisabled=bDisabled; }
		void SubclassWindow( HWND hWnd, CCustomMenuDlg7 *pOwner );

		void UpdateItem( int index );

		int LoadIcon( const MenuItem &item );
		unsigned int GetIconKey( const MenuItem &item ) const;
		HICON LoadIcon( const MenuItem &item, bool bSmall ) const;

		void EditItem( int index );
		void DeleteItem( int index );
		void InsertItem( int index, int copy );
		void MoveItem( int from, int to );

	protected:
		LRESULT OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
		LRESULT OnLButtonDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
		LRESULT OnLButtonDblClick( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
		LRESULT OnLButtonUp( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
		LRESULT OnMouseMove( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
		LRESULT OnRButtonDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled ) { return 0; }
		LRESULT OnGetDlgCode( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
		LRESULT OnKeyDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
		LRESULT OnCaptureChanged( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
		LRESULT OnScroll( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
		LRESULT OnTimer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
		LRESULT OnResizeCombo( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
		LRESULT OnHandleTab( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
		LRESULT OnEndTrack( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
		LRESULT OnCustomDraw( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
		LRESULT OnSelEndOk( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );

	private:
		CCustomMenuDlg7 *m_pOwner;
		CWindow m_Combo;
		HIMAGELIST m_ImageList;
		int m_Line;
		int m_Column;
		bool m_bDisabled;
		bool m_bThemed;
		bool m_bDragging;
		int m_ClickedItem;

		mutable std::vector<HMODULE> m_Modules;
		std::map<unsigned int,int> m_IconCache;

		friend class CCustomMenuDlg7;

		void CreateCombo( int line, int column );
		void DestroyCombo( void );
		static LRESULT CALLBACK SubclassGridCombo( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData );
	};

	CItemList m_List;
	CSetting *m_pSetting;

	std::vector<MenuItem> m_Items;

	void SerializeData( void );

	friend class CEditMenuDlg7;
};

class CEditMenuDlg7: public CResizeableDlg<CEditMenuDlg7>
{
public:
	CEditMenuDlg7( CCustomMenuDlg7 *pOwner, CCustomMenuDlg7::MenuItem *pItem ) { m_pOwner=pOwner; m_pItem=pItem; }

	BEGIN_MSG_MAP( CEditMenuDlg7 )
		MESSAGE_HANDLER( WM_INITDIALOG, OnInitDialog )
		COMMAND_ID_HANDLER( IDOK, OnOK )
		COMMAND_ID_HANDLER( IDCANCEL, OnCancel )
		COMMAND_HANDLER( IDC_COMBOCOMMAND, CBN_KILLFOCUS, OnCommandChanged )
		COMMAND_HANDLER( IDC_COMBOCOMMAND, CBN_SELENDOK, OnCommandChanged )
		COMMAND_HANDLER( IDC_BUTTONCOMMAND, BN_CLICKED, OnBrowseCommand )
		COMMAND_HANDLER( IDC_BUTTONLINK, BN_CLICKED, OnBrowseLink )
		COMMAND_HANDLER( IDC_BUTTONICON, BN_CLICKED, OnBrowseIcon )
		COMMAND_HANDLER( IDC_EDITLINK2, EN_KILLFOCUS, OnLinkChanged )
		COMMAND_HANDLER( IDC_EDITICON, EN_KILLFOCUS, OnIconChanged )
		COMMAND_HANDLER( IDC_BUTTONRESET, BN_CLICKED, OnReset )
		MESSAGE_HANDLER( WM_SIZE, OnSize )
		MESSAGE_HANDLER( WM_GETMINMAXINFO, OnGetMinMaxInfo )
	END_MSG_MAP()

	BEGIN_RESIZE_MAP
		RESIZE_CONTROL(IDC_COMBOCOMMAND,MOVE_SIZE_X)
		RESIZE_CONTROL(IDC_EDITLINK2,MOVE_SIZE_X)
		RESIZE_CONTROL(IDC_BUTTONCOMMAND,MOVE_MOVE_X)
		RESIZE_CONTROL(IDC_BUTTONLINK,MOVE_MOVE_X)
		RESIZE_CONTROL(IDC_EDITLABEL,MOVE_SIZE_X)
		RESIZE_CONTROL(IDC_EDITTIP,MOVE_SIZE_X)
		RESIZE_CONTROL(IDC_EDITICON,MOVE_SIZE_X)
		RESIZE_CONTROL(IDC_BUTTONICON,MOVE_MOVE_X)
		RESIZE_CONTROL(IDOK,MOVE_MOVE_X)
		RESIZE_CONTROL(IDCANCEL,MOVE_MOVE_X)
	END_RESIZE_MAP

	bool Run( void );

	enum { SETTINGS_MASK=StdMenuItem::MENU_NOEXPAND|StdMenuItem::MENU_SINGLE_EXPAND|StdMenuItem::MENU_ITEM_DISABLED };

protected:
	// Handler prototypes:
	//  LRESULT MessageHandler(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	//  LRESULT CommandHandler(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	//  LRESULT NotifyHandler(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnCancel( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnCommandChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnLinkChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnIconChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnBrowseCommand( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnBrowseLink( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnBrowseIcon( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnReset( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );

private:
	CCustomMenuDlg7 *m_pOwner;
	CCustomMenuDlg7::MenuItem *m_pItem;
	CCustomMenuDlg7::MenuItem m_StoredItem;

	bool m_bResult;
	HICON m_hIcon;
	unsigned int m_IconKey;

	CString GetComboText( WORD wNotifyCode, WORD wID );
	void UpdateIcon( void );
	void UpdateControls( void );
	void StorePlacement( void );
};

void CEditMenuDlg7::UpdateControls( void )
{
	BOOL bEnable=(m_pItem->stdItemIndex>0);
	GetDlgItem(IDC_EDITLABEL).EnableWindow(bEnable);
	GetDlgItem(IDC_STATICINFOTIP).EnableWindow(bEnable);
	GetDlgItem(IDC_EDITTIP).EnableWindow(bEnable);
	GetDlgItem(IDC_STATICICON).EnableWindow(bEnable);
	GetDlgItem(IDC_EDITICON).EnableWindow(bEnable);
	GetDlgItem(IDC_BUTTONICON).EnableWindow(bEnable);

	GetDlgItem(IDC_EDITLINK2).EnableWindow(bEnable && m_pItem->stdItemIndex==g_StdCommands7Count-1);
	GetDlgItem(IDC_BUTTONLINK).EnableWindow(bEnable && m_pItem->stdItemIndex==g_StdCommands7Count-1);
	GetDlgItem(IDC_BUTTONRESET).EnableWindow(bEnable && m_pItem->stdItemIndex<g_StdCommands7Count-1);

	bEnable=bEnable && !(g_StdCommands7[m_pItem->stdItemIndex].flags&CStdCommand7::ITEM_SINGLE);
	GetDlgItem(IDC_CHECKSORTZA).EnableWindow(bEnable);
	GetDlgItem(IDC_CHECKSORTZAREC).EnableWindow(bEnable);
	GetDlgItem(IDC_CHECKSORTONCE).EnableWindow(bEnable);
	GetDlgItem(IDC_CHECKOPENUP).EnableWindow(bEnable);
	GetDlgItem(IDC_CHECKOPENUPREC).EnableWindow(bEnable);
	GetDlgItem(IDC_CHECKNOEXT).EnableWindow(bEnable);
	GetDlgItem(IDC_CHECKTRACK).EnableWindow(bEnable);
	GetDlgItem(IDC_CHECKMULTICOLUMN).EnableWindow(bEnable);
	GetDlgItem(IDC_STATICTEXT).EnableWindow(bEnable);
}

LRESULT CEditMenuDlg7::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	m_hIcon=NULL;
	m_IconKey=0;
	m_StoredItem=*m_pItem;
	if (m_pItem->stdItemIndex<g_StdCommands7Count-1)
	{
		const CStdCommand7 &command=g_StdCommands7[m_pItem->stdItemIndex];
		if (m_pItem->label.IsEmpty())
			m_pItem->label=command.label;
		if (m_pItem->tip.IsEmpty())
			m_pItem->tip=command.tip;
		if (m_pItem->icon.IsEmpty())
			m_pItem->icon=command.icon;
	}

	InitResize(MOVE_HORIZONTAL|MOVE_GRIPPER);
	CWindow commands=GetDlgItem(IDC_COMBOCOMMAND);
	for (int i=0;i<g_StdCommands7Count-1;i++)
	{
		int idx=(int)commands.SendMessage(CB_ADDSTRING,0,(LPARAM)(const wchar_t*)g_StdCommands7[i].command);
		commands.SendMessage(CB_SETITEMDATA,idx,i);
	}
	for (int i=0;i<g_StdCommands7Count-1;i++)
	{
		if (commands.SendMessage(CB_GETITEMDATA,i)==m_pItem->stdItemIndex)
		{
			commands.SendMessage(CB_SETCURSEL,i);
			break;
		}
	}
	commands.SetWindowText(m_pItem->command);

	COMBOBOXINFO info={sizeof(info)};
	commands.SendMessage(CB_GETCOMBOBOXINFO,0,(LPARAM)&info);
	SHAutoComplete(info.hwndItem,SHACF_FILESYSTEM);
	SendDlgItemMessage(IDC_EDITLINK2,CB_GETCOMBOBOXINFO,0,(LPARAM)&info);
	SHAutoComplete(info.hwndItem,SHACF_FILESYSTEM);

	SetDlgItemText(IDC_EDITLINK2,m_pItem->link);
	SetDlgItemText(IDC_EDITLABEL,m_pItem->label);
	SetDlgItemText(IDC_EDITTIP,m_pItem->tip);
	SetDlgItemText(IDC_EDITICON,m_pItem->icon);

	CheckDlgButton(IDC_CHECKSORTZA,(m_pItem->settings&StdMenuItem::MENU_SORTZA)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKSORTZAREC,(m_pItem->settings&StdMenuItem::MENU_SORTZA_REC)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKSORTONCE,(m_pItem->settings&StdMenuItem::MENU_SORTONCE)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKOPENUP,(m_pItem->settings&StdMenuItem::MENU_OPENUP)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKOPENUPREC,(m_pItem->settings&StdMenuItem::MENU_OPENUP_REC)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKNOEXT,(m_pItem->settings&StdMenuItem::MENU_NOEXTENSIONS)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKTRACK,(m_pItem->settings&StdMenuItem::MENU_TRACK)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_CHECKMULTICOLUMN,(m_pItem->settings&StdMenuItem::MENU_MULTICOLUMN)?BST_CHECKED:BST_UNCHECKED);

	UpdateControls();
	UpdateIcon();
	SendDlgItemMessage(IDC_EDITLABEL,EM_SETCUEBANNER,TRUE,(LPARAM)(const wchar_t*)LoadStringEx(IDS_NO_TEXT));

	CWindow tooltip=CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT,TOOLTIPS_CLASS,NULL,WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP,0,0,0,0,m_hWnd,NULL,g_Instance,NULL);
	tooltip.SendMessage(TTM_SETMAXTIPWIDTH,0,GetSystemMetrics(SM_CXSCREEN)/2);

	TOOLINFO tool={sizeof(tool),TTF_SUBCLASS|TTF_IDISHWND,m_hWnd,(UINT_PTR)(HWND)commands};
	CString str=LoadStringEx(IDS_COMMAND_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);
	tool.uId=(UINT_PTR)(HWND)commands.GetWindow(GW_CHILD);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_LINK_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_EDITLINK2);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_TEXT_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_EDITLABEL);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_TIP_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_EDITTIP);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_ICON_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_EDITICON);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_SORTZA_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKSORTZA);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_SORTZAREC_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKSORTZAREC);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_SORTONCE_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKSORTONCE);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_OPENUP_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKOPENUP);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_OPENUPREC_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKOPENUPREC);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_MULTICOLUMN_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKMULTICOLUMN);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_TRACK_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKTRACK);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_NOEXTENSIONS_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_CHECKNOEXT);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_RESTORE_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_BUTTONRESET);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	RECT rc;
	CRegKey regSettings;
	if (regSettings.Open(HKEY_CURRENT_USER,GetSettingsRegPath())==ERROR_SUCCESS)
	{
		ULONG size=sizeof(rc);
		if (regSettings.QueryBinaryValue(L"CEditMenuDlg7",&rc,&size)==ERROR_SUCCESS && size==sizeof(rc))
			SetStoreRect(rc);
	}
	return TRUE;
}

LRESULT CEditMenuDlg7::OnOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	SetFocus();
	GetDlgItemText(IDC_EDITLABEL,m_pItem->label);
	m_pItem->label.TrimLeft();
	m_pItem->label.TrimRight();
	GetDlgItemText(IDC_EDITTIP,m_pItem->tip);
	m_pItem->tip.TrimLeft();
	m_pItem->tip.TrimRight();

	m_pItem->settings&=SETTINGS_MASK;
	if (IsDlgButtonChecked(IDC_CHECKSORTZA)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_SORTZA;
	if (IsDlgButtonChecked(IDC_CHECKSORTZAREC)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_SORTZA_REC;
	if (IsDlgButtonChecked(IDC_CHECKSORTONCE)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_SORTONCE;
	if (IsDlgButtonChecked(IDC_CHECKOPENUP)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_OPENUP;
	if (IsDlgButtonChecked(IDC_CHECKOPENUPREC)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_OPENUP_REC;
	if (IsDlgButtonChecked(IDC_CHECKTRACK)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_TRACK;
	if (IsDlgButtonChecked(IDC_CHECKNOEXT)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_NOEXTENSIONS;
	if (IsDlgButtonChecked(IDC_CHECKMULTICOLUMN)==BST_CHECKED) m_pItem->settings|=StdMenuItem::MENU_MULTICOLUMN;

	m_pItem=NULL;
	::EnableWindow(GetAncestor(m_pOwner->m_hWnd,GA_ROOT),TRUE);
	m_bResult=true;
	StorePlacement();
	DestroyWindow();
	return 0;
}

LRESULT CEditMenuDlg7::OnCancel( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	*m_pItem=m_StoredItem;
	m_pItem=NULL;
	::EnableWindow(GetAncestor(m_pOwner->m_hWnd,GA_ROOT),TRUE);
	m_bResult=false;
	StorePlacement();
	DestroyWindow();
	return 0;
}

LRESULT CEditMenuDlg7::OnCommandChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	CString text=GetComboText(wNotifyCode,wID);
	if (!m_pItem || text==m_pItem->command) return 0;
	m_pItem->command=text;
	m_pItem->stdItemIndex=g_StdCommands7Count-1;
	for (int i=0;i<g_StdCommands7Count-1;i++)
		if (_wcsicmp(g_StdCommands7[i].command,text)==0)
		{
			m_pItem->stdItemIndex=i;
			break;
		}
	UpdateControls();
	UpdateIcon();
	return 0;
}

LRESULT CEditMenuDlg7::OnLinkChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	CString text=GetComboText(wNotifyCode,wID);
	if (!m_pItem || text==m_pItem->link) return 0;
	m_pItem->link=text;
	UpdateIcon();
	return 0;
}

LRESULT CEditMenuDlg7::OnIconChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	CString text;
	GetDlgItemText(IDC_EDITICON,text);
	text.TrimLeft();
	text.TrimRight();
	if (!m_pItem || text==m_pItem->icon) return 0;
	m_pItem->icon=text;
	UpdateIcon();
	return 0;
}

LRESULT CEditMenuDlg7::OnBrowseCommand( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	wchar_t text[_MAX_PATH];
	GetDlgItemText(IDC_COMBOCOMMAND,text,_countof(text));
	if (BrowseCommandHelper(m_hWnd,text))
	{
		SetDlgItemText(IDC_COMBOCOMMAND,text);
		SendMessage(WM_COMMAND,MAKEWPARAM(IDC_COMBOCOMMAND,CBN_KILLFOCUS));
	}
	return 0;
}

LRESULT CEditMenuDlg7::OnBrowseLink( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	wchar_t text[_MAX_PATH];
	GetDlgItemText(IDC_EDITLINK2,text,_countof(text));
	if (BrowseLinkHelper(m_hWnd,text,false))
	{
		SetDlgItemText(IDC_EDITLINK2,text);
		SendMessage(WM_COMMAND,MAKEWPARAM(IDC_EDITLINK2,EN_KILLFOCUS));
	}
	return 0;
}

LRESULT CEditMenuDlg7::OnBrowseIcon( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	wchar_t text[_MAX_PATH];
	GetDlgItemText(IDC_EDITICON,text,_countof(text));
	if (BrowseIconHelper(m_hWnd,text))
	{
		SetDlgItemText(IDC_EDITICON,text);
		SendMessage(WM_COMMAND,MAKEWPARAM(IDC_EDITICON,EN_KILLFOCUS));
	}
	return 0;
}

LRESULT CEditMenuDlg7::OnReset( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	if (m_pItem->stdItemIndex<g_StdCommands7Count-1)
	{
		const CStdCommand7 &command=g_StdCommands7[m_pItem->stdItemIndex];
		m_pItem->link.Empty();
		SetDlgItemText(IDC_EDITLINK2,L"");
		m_pItem->label=command.label;
		SetDlgItemText(IDC_EDITLABEL,m_pItem->label);
		m_pItem->tip=command.tip;
		SetDlgItemText(IDC_EDITTIP,m_pItem->tip);
		m_pItem->icon=command.icon;
		SetDlgItemText(IDC_EDITICON,m_pItem->icon);

		m_pItem->settings=command.settings|(m_pItem->settings&SETTINGS_MASK);
		CheckDlgButton(IDC_CHECKSORTZA,(m_pItem->settings&StdMenuItem::MENU_SORTZA)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(IDC_CHECKSORTZAREC,(m_pItem->settings&StdMenuItem::MENU_SORTZA_REC)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(IDC_CHECKSORTONCE,(m_pItem->settings&StdMenuItem::MENU_SORTONCE)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(IDC_CHECKOPENUP,(m_pItem->settings&StdMenuItem::MENU_OPENUP)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(IDC_CHECKOPENUPREC,(m_pItem->settings&StdMenuItem::MENU_OPENUP_REC)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(IDC_CHECKNOEXT,(m_pItem->settings&StdMenuItem::MENU_NOEXTENSIONS)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(IDC_CHECKTRACK,(m_pItem->settings&StdMenuItem::MENU_TRACK)?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(IDC_CHECKMULTICOLUMN,(m_pItem->settings&StdMenuItem::MENU_MULTICOLUMN)?BST_CHECKED:BST_UNCHECKED);
		UpdateIcon();
	}
	return 0;
}

LRESULT CEditMenuDlg7::OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	CResizeableDlg<CEditMenuDlg7>::OnSize();
	return 0;
}

void CEditMenuDlg7::UpdateIcon( void )
{
	unsigned int key=m_pOwner->m_List.GetIconKey(*m_pItem);

	if (key!=m_IconKey)
	{
		m_IconKey=key;
		if (m_hIcon) DestroyIcon(m_hIcon);
		m_hIcon=m_pOwner->m_List.LoadIcon(*m_pItem,false);
		SendDlgItemMessage(IDC_ICONN,STM_SETICON,(WPARAM)m_hIcon);
	}
}

CString CEditMenuDlg7::GetComboText( WORD wNotifyCode, WORD wID )
{
	CString text;
	if (wNotifyCode==CBN_SELENDOK)
	{
		int idx=(int)SendDlgItemMessage(wID,CB_GETCURSEL);
		text.Empty();
		if (idx>=0)
		{
			int len=(int)SendDlgItemMessage(wID,CB_GETLBTEXTLEN,idx);
			SendDlgItemMessage(wID,CB_GETLBTEXT,idx,(LPARAM)text.GetBuffer(len+1));
			text.ReleaseBuffer(len);
		}
	}
	else
		GetDlgItemText(wID,text);
	text.TrimLeft();
	text.TrimRight();
	return text;
}

void CEditMenuDlg7::StorePlacement( void )
{
	RECT rc;
	GetStoreRect(rc);

	CRegKey regSettings;
	if (regSettings.Open(HKEY_CURRENT_USER,GetSettingsRegPath())!=ERROR_SUCCESS)
		regSettings.Create(HKEY_CURRENT_USER,GetSettingsRegPath());
	regSettings.SetBinaryValue(L"CEditMenuDlg7",&rc,sizeof(rc));
}

bool CEditMenuDlg7::Run( void )
{
	HWND parent=GetAncestor(m_pOwner->m_hWnd,GA_ROOT);
	DLGTEMPLATE *pTemplate=LoadDialogEx(IDD_CUSTOMMENU7);
	Create(parent,pTemplate);
	ShowWindow(SW_SHOWNORMAL);
	::EnableWindow(parent,FALSE);
	MSG msg;
	while (m_hWnd && GetMessage(&msg,0,0,0))
	{
		if (IsDialogMessage(&msg)) continue;
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return m_bResult;
}

///////////////////////////////////////////////////////////////////////////////

unsigned int CCustomMenuDlg7::CItemList::GetIconKey( const MenuItem &item ) const
{
	if (!item.icon.IsEmpty())
		return CalcFNVHash(item.icon);
	else if (item.stdItemIndex>=0 && item.stdItemIndex<g_StdCommands7Count-1)
		return item.stdItemIndex+1;
	else if (!item.link.IsEmpty())
		return CalcFNVHash(item.link);
	return 0;
}

HICON CCustomMenuDlg7::CItemList::LoadIcon( const MenuItem &item, bool bSmall ) const
{
	if (!item.icon.IsEmpty())
	{
		return ::LoadIcon(GetSystemMetrics(bSmall?SM_CXSMICON:SM_CXICON),item.icon,m_Modules);
	}
	else
	{
		CAbsolutePidl pidl;
		if (item.stdItemIndex>=0 && item.stdItemIndex<g_StdCommands7Count-1)
		{
			const CStdCommand7 &command=g_StdCommands7[item.stdItemIndex];
			if (command.icon)
				return ::LoadIcon(GetSystemMetrics(bSmall?SM_CXSMICON:SM_CXICON),command.icon,m_Modules);
			if (command.knownFolder)
				ShGetKnownFolderIDList(*command.knownFolder,&pidl);
			else if (command.knownLink)
			{
				MenuParseDisplayName(command.knownLink,&pidl,NULL,NULL);
			}
		}
		else if (!item.link.IsEmpty())
		{
			const wchar_t *c=wcschr(item.link,'|');
			if (c)
			{
				for (c++;*c==' ';)
					c++;
			}
			else
				c=item.link;
			wchar_t buf[_MAX_PATH];
			Strcpy(buf,_countof(buf),c);
			DoEnvironmentSubst(buf,_countof(buf));

			wchar_t *second=wcschr(buf,';');
			if (second)
			{
				wchar_t *end= second;
				while (end>buf)
				{
					*end=0;
					end--;
					if (*end!=' ')
						break;
				}
			}

			TNetworkType networkType;
			MenuParseDisplayName(buf,&pidl,NULL,&networkType);
			// disable for now, to match the tree item icon loading
			if (pidl && GetWinVersion()>=WIN_VER_WIN8 && _wcsicmp(PathFindExtension(buf),L".lnk")==0)
			{
				CComPtr<IShellItem> pItem;
				if (SUCCEEDED(SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pItem)))
				{
					const CItemManager::ItemInfo *pItemInfo=g_ItemManager.GetItemInfo(pItem,pidl,CItemManager::INFO_LINK|CItemManager::INFO_METRO);
					if (pItemInfo)
					{
						bool bMetroLink;
						{
							CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
							bMetroLink=pItemInfo->IsMetroLink();
						}
						if (bMetroLink)
						{
							g_ItemManager.UpdateItemInfo(pItemInfo,(bSmall?CItemManager::INFO_SMALL_ICON:CItemManager::INFO_LARGE_ICON)|CItemManager::INFO_REFRESH_NOW);
							const CItemManager::IconInfo *pIconInfo=bSmall?pItemInfo->smallIcon:pItemInfo->largeIcon;
							if (pIconInfo && pIconInfo->bitmap)
							{
								int iconSize=GetSystemMetrics(bSmall?SM_CXSMICON:SM_CXICON);
								BITMAP bmpInfo;
								GetObject(pIconInfo->bitmap,sizeof(bmpInfo),&bmpInfo);

								std::vector<char> buf((iconSize+1)*iconSize,-1);
								HBITMAP bmpMask=CreateBitmap(iconSize,iconSize,1,8,&buf[0]);

								HBITMAP bmpColor=pIconInfo->bitmap;
								if (bmpInfo.bmWidth!=iconSize || bmpInfo.bmHeight!=iconSize)
								{
									HDC hSrc=CreateCompatibleDC(NULL);
									HDC hDst=CreateCompatibleDC(hSrc);

									BITMAPINFO bi={0};
									bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
									bi.bmiHeader.biWidth=bi.bmiHeader.biHeight=iconSize;
									bi.bmiHeader.biPlanes=1;
									bi.bmiHeader.biBitCount=32;
									bmpColor=CreateDIBSection(hDst,&bi,DIB_RGB_COLORS,NULL,NULL,0);

									HGDIOBJ bmp01=SelectObject(hSrc,pIconInfo->bitmap);
									HGDIOBJ bmp02=SelectObject(hDst,bmpColor);
									StretchBlt(hDst,0,0,bi.bmiHeader.biWidth,bi.bmiHeader.biHeight,hSrc,0,0,bmpInfo.bmWidth,bmpInfo.bmHeight,SRCCOPY);
									SelectObject(hSrc,bmp01);
									SelectObject(hDst,bmp02);
									DeleteDC(hSrc);
									DeleteDC(hDst);
								}
								ICONINFO info={TRUE,0,0,bmpMask,bmpColor};
								HICON hIcon=CreateIconIndirect(&info);
								DeleteObject(bmpMask);
								if (bmpColor!=pIconInfo->bitmap)
									DeleteObject(bmpColor);
								return hIcon;
							}
						}
					}
				}
			}
			if (networkType!=NETWORK_NONE && networkType!=NETWORK_FILE)
				return ::LoadIcon(GetSystemMetrics(bSmall?SM_CXSMICON:SM_CXICON),GetDefaultNetworkIcon(networkType),m_Modules);
		}
		if (pidl)
			return ::LoadIcon(GetSystemMetrics(bSmall?SM_CXSMICON:SM_CXICON),pidl);
	}
	return NULL;
}

int CCustomMenuDlg7::CItemList::LoadIcon( const MenuItem &item )
{
	if (item.stdItemIndex==0)
		return -1;
	unsigned int key=GetIconKey(item);
	std::map<unsigned int,int>::const_iterator it=m_IconCache.find(key);
	if (it!=m_IconCache.end())
		return it->second;

	// extract icon
	HICON hIcon=LoadIcon(item,true);
	if (!hIcon)
	{
		m_IconCache[key]=0;
		return 0;
	}
	int index=ImageList_AddIcon(m_ImageList,hIcon);
	DestroyIcon(hIcon);
	m_IconCache[key]=index;
	return index;
}

void CCustomMenuDlg7::CItemList::UpdateItem( int index )
{
	const MenuItem &menuItem=m_pOwner->m_Items[index];
	const wchar_t *text=g_StdCommands7[menuItem.stdItemIndex].displayName;
	if (menuItem.stdItemIndex==g_StdCommands7Count-1)
	{
		if (!menuItem.label.IsEmpty())
			text=menuItem.label;
		else if (!menuItem.command.IsEmpty())
			text=menuItem.command;
	}
	ListView_SetItemText(m_hWnd,index,1,(wchar_t*)(const wchar_t*)text);
	CString str;
	if (menuItem.settings&StdMenuItem::MENU_ITEM_DISABLED)
		str=LoadStringEx(IDS_ITEM_HIDE);
	else if (g_StdCommands7[menuItem.stdItemIndex].flags&CStdCommand7::ITEM_SINGLE)
		str=LoadStringEx(IDS_ITEM_SHOW2);
	else if ((menuItem.settings&StdMenuItem::MENU_NOEXPAND) && !(g_StdCommands7[menuItem.stdItemIndex].flags&CStdCommand7::ITEM_FOLDER))
		str=LoadStringEx(IDS_ITEM_SHOW);
	else if ((menuItem.settings&StdMenuItem::MENU_SINGLE_EXPAND) && !(g_StdCommands7[menuItem.stdItemIndex].flags&CStdCommand7::ITEM_NODRIVES))
		if (g_StdCommands7[menuItem.stdItemIndex].flags&CStdCommand7::ITEM_COMPUTER)
			str=LoadStringEx(IDS_ITEM_DRIVES);
		else
			str=LoadStringEx(IDS_ITEM_LINKS);
	else
		str=LoadStringEx(IDS_ITEM_MENU);
	ListView_SetItemText(m_hWnd,index,2,(wchar_t*)(const wchar_t*)str);
}

void CCustomMenuDlg7::CItemList::SubclassWindow( HWND hWnd, CCustomMenuDlg7 *pOwner )
{
	m_pOwner=pOwner;
	CWindowImpl<CItemList,CWindow>::SubclassWindow(hWnd);
	CString str;
	{
		str=LoadStringEx(IDS_COLUMN_ICON);
		HDC hdc=CreateCompatibleDC(NULL);
		HFONT font=GetFont();
		HGDIOBJ font0=SelectObject(hdc,font);
		SIZE size;
		if (GetTextExtentPoint32(hdc,str,str.GetLength(),&size))
			size.cx+=20;
		else
			size.cx=0;
		if (size.cx<50) size.cx=50;
		
		SelectObject(hdc,font0);
		DeleteDC(hdc);
		LVCOLUMN column={LVCF_WIDTH|LVCF_TEXT,0,size.cx,(LPWSTR)(LPCWSTR)str};
		ListView_InsertColumn(m_hWnd,0,&column);
	}
	{
		// TODO: scale the size with DPI
		str=LoadStringEx(IDS_COLUMN_COMMAND);
		LVCOLUMN column={LVCF_WIDTH|LVCF_TEXT,0,300,(LPWSTR)(LPCWSTR)str};
		ListView_InsertColumn(m_hWnd,1,&column);
	}
	{
		str=LoadStringEx(IDS_COLUMN_DISPLAY);
		LVCOLUMN column={LVCF_WIDTH|LVCF_TEXT,0,200,(LPWSTR)(LPCWSTR)str};
		ListView_InsertColumn(m_hWnd,2,&column);
	}
	ListView_SetExtendedListViewStyle(m_hWnd,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
	int iconSize=GetSystemMetrics(SM_CXSMICON);
	m_ImageList=ImageList_Create(iconSize,iconSize,ILC_COLOR32|ILC_MASK|ILC_MIRROR,1,16);
	ListView_SetImageList(m_hWnd,m_ImageList,LVSIL_SMALL);
	SHFILEINFO info;
	if (SHGetFileInfo(L"file",FILE_ATTRIBUTE_NORMAL,&info,sizeof(info),SHGFI_USEFILEATTRIBUTES|SHGFI_ICON|SHGFI_SMALLICON))
	{
		AddTrackedIcon(info.hIcon);
		ImageList_AddIcon(m_ImageList,info.hIcon);
		DestroyIcon(info.hIcon);
	}
	m_bThemed=(IsAppThemed()!=0);
	if (m_bThemed)
		SetWindowTheme(m_hWnd,L"Explorer",NULL);

	m_Combo.Create(WC_COMBOBOX,hWnd,NULL,NULL,WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL);
	m_Combo.SetFont(GetFont());
	RECT rc;
	m_Combo.GetWindowRect(&rc);
	HIMAGELIST images=ImageList_Create(2,rc.bottom-rc.top,ILC_COLOR,1,1);
	ListView_SetImageList(m_hWnd,images,LVSIL_STATE);
	m_Combo.DestroyWindow();
}

LRESULT CCustomMenuDlg7::CItemList::OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	DestroyCombo();
	for (std::vector<HMODULE>::const_iterator it=m_Modules.begin();it!=m_Modules.end();++it)
		FreeLibrary(*it);
	m_Modules.clear();
	m_IconCache.clear();
	return 0;
}

LRESULT CCustomMenuDlg7::CItemList::OnLButtonDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	LVHITTESTINFO test={{(short)LOWORD(lParam),(short)HIWORD(lParam)}};
	int index=ListView_HitTest(m_hWnd,&test);
	DestroyCombo();
	if (index>=0)
	{
		ListView_SetItemState(m_hWnd,index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
		SetCapture();
		m_ClickedItem=index;
		return 0;
	}
	else
	{
		m_Line=m_Column=-1;
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CCustomMenuDlg7::CItemList::OnLButtonDblClick( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	LVHITTESTINFO test={{(short)LOWORD(lParam),(short)HIWORD(lParam)}};
	int index=ListView_HitTest(m_hWnd,&test);
	DestroyCombo();
	m_Line=m_Column=-1;
	if (index>=0)
		EditItem(index);
	else if (test.flags&LVHT_NOWHERE)
	{
		RECT rc;
		ListView_GetItemRect(m_hWnd,m_pOwner->m_Items.size()-1,&rc,LVIR_BOUNDS);
		if (test.pt.y>rc.bottom)
			InsertItem((int)m_pOwner->m_Items.size(),-1);
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CCustomMenuDlg7::CItemList::OnLButtonUp( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (GetCapture()==m_hWnd)
	{
		if (m_bDragging)
		{
			LVINSERTMARK mark={sizeof(mark),0,-1};
			if (ListView_GetInsertMark(m_hWnd,&mark) && mark.iItem>=0)
			{
				// move m_ClickedItem to mark.iItem
				int index=mark.iItem;
				if (mark.dwFlags&LVIM_AFTER)
					index++;
				MoveItem(m_ClickedItem,index);
			}
			ReleaseCapture();
		}
		else
		{
			ReleaseCapture();
			LVHITTESTINFO test={{(short)LOWORD(lParam),(short)HIWORD(lParam)}};
			int index=ListView_SubItemHitTest(m_hWnd,&test);
			DestroyCombo();
			if (index>=0 && (test.iSubItem==1 || test.iSubItem==2))
			{
				CreateCombo(index,test.iSubItem);
				ListView_SetItemState(m_hWnd,index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
				return 0;
			}
			else
			{
				m_Line=m_Column=-1;
			}
		}
	}
	return 0;
}

LRESULT CCustomMenuDlg7::CItemList::OnMouseMove( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (GetCapture()==m_hWnd)
	{
		LVHITTESTINFO test={{(short)LOWORD(lParam),(short)HIWORD(lParam)}};
		int index=ListView_HitTest(m_hWnd,&test);
		if (index!=m_ClickedItem)
		{
			m_bDragging=true;
		}
		if (m_bDragging)
		{
			if (index==-1)
			{
				LVINSERTMARK mark={sizeof(mark),0,-1};
				ListView_SetInsertMark(m_hWnd,&mark);
			}
			else
			{
				LVINSERTMARK mark={sizeof(mark),0,index};
				RECT rc;
				ListView_GetItemRect(m_hWnd,index,&rc,LVIR_BOUNDS);
				if (test.pt.y>(rc.top+rc.bottom)/2)
					mark.dwFlags=LVIM_AFTER;
				ListView_SetInsertMark(m_hWnd,&mark);
				if (mark.dwFlags==LVIM_AFTER && index<(int)m_pOwner->m_Items.size()-1)
					index++;
			}
			RECT rc;
			::GetWindowRect(ListView_GetHeader(m_hWnd),&rc);
			::MapWindowPoints(NULL,m_hWnd,(POINT*)&rc,2);
			if (test.pt.y<rc.bottom)
			{
				SetTimer(TIMER_SCROLL_UP,100);
				KillTimer(TIMER_SCROLL_DOWN);
			}
			else
			{
				KillTimer(TIMER_SCROLL_UP);
				GetClientRect(&rc);
				if (test.pt.y>rc.bottom)
					SetTimer(TIMER_SCROLL_DOWN,100);
				else
					KillTimer(TIMER_SCROLL_DOWN);
			}
		}
	}
	else
		bHandled=FALSE;
	return 0;
}

LRESULT CCustomMenuDlg7::CItemList::OnGetDlgCode( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (lParam)
	{
		MSG *msg=(MSG*)lParam;
		if (msg->message==WM_KEYDOWN && msg->wParam==VK_ESCAPE)
		{
			bHandled=FALSE;
			return 0;
		}
	}
	return DLGC_WANTALLKEYS;
}

LRESULT CCustomMenuDlg7::CItemList::OnKeyDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	int index=ListView_GetNextItem(m_hWnd,-1,LVNI_FOCUSED);
	if (wParam==VK_DOWN && GetKeyState(VK_CONTROL)<0)
	{
		if (index>=0 && index<ListView_GetItemCount(m_hWnd)-1)
			MoveItem(index,index+2);
	}
	else if (wParam==VK_UP && GetKeyState(VK_CONTROL)<0)
	{
		if (index>0)
			MoveItem(index,index-1);
	}
	else if (wParam==VK_RETURN)
	{
		if (index>=0)
			EditItem(index);
	}
	else if (wParam==VK_DELETE)
	{
		if (index>=0)
			DeleteItem(index);
	}
	else if (wParam==VK_INSERT)
	{
		InsertItem(index+1,-1);
	}
	else if (wParam==VK_TAB)
	{
		SendMessage(WM_HANDLE_TAB,GetKeyState(VK_SHIFT)<0);
	}
	else
		bHandled=FALSE;
	return 0;
}

LRESULT CCustomMenuDlg7::CItemList::OnCaptureChanged( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	LVINSERTMARK mark={sizeof(mark),0,-1};
	ListView_SetInsertMark(m_hWnd,&mark);
	KillTimer(TIMER_SCROLL_UP);
	KillTimer(TIMER_SCROLL_DOWN);
	m_bDragging=false;
	return 0;
}

LRESULT CCustomMenuDlg7::CItemList::OnScroll( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	PostMessage(WM_RESIZE_COMBO);
	bHandled=FALSE;
	return 0;
}

LRESULT CCustomMenuDlg7::CItemList::OnTimer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==TIMER_SCROLL_UP)
	{
		RECT rc;
		if (ListView_GetItemRect(m_hWnd,0,&rc,LVIR_BOUNDS))
			ListView_Scroll(m_hWnd,0,rc.top-rc.bottom);
	}
	if (wParam==TIMER_SCROLL_DOWN)
	{
		RECT rc;
		if (ListView_GetItemRect(m_hWnd,0,&rc,LVIR_BOUNDS))
			ListView_Scroll(m_hWnd,0,rc.bottom-rc.top);
	}
	return 0;
}

LRESULT CCustomMenuDlg7::CItemList::OnResizeCombo( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (lParam==1)
	{
		SetFocus();
		DestroyCombo();
		return 0;
	}
	if (m_Combo.m_hWnd && m_Line>=0 && m_Column>=1)
	{
		RECT rc;
		ListView_GetSubItemRect(m_hWnd,m_Line,m_Column,LVIR_BOUNDS,&rc);
		if (m_Line>=ListView_GetTopIndex(m_hWnd))
		{
			m_Combo.SetWindowPos(NULL,&rc,SWP_NOZORDER|SWP_SHOWWINDOW);
			m_Combo.SetFocus();
			if (wParam)
				m_Combo.SendMessage(CB_SHOWDROPDOWN,TRUE);
		}
		else
			m_Combo.SetWindowPos(NULL,&rc,SWP_NOZORDER|SWP_HIDEWINDOW);
	}
	return 0;
}

LRESULT CCustomMenuDlg7::CItemList::OnHandleTab( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	int line=-1, column=-1;
	if (m_Combo.m_hWnd && m_Line>=0)
	{
		line=m_Line;
		column=m_Column;
	}
	else
	{
		line=ListView_GetNextItem(m_hWnd,-1,LVNI_FOCUSED);
		if (line<0) line=0;
		column=0;
	}
	if (wParam)
	{
		// back
		if (column>0)
			column--;
		else
		{
			line--;
			column=2;
		}
		if (line<0) return 0;
	}
	else
	{
		// forward
		if (column<2)
			column++;
		else
		{
			line++;
			column=0;
		}
		if (line>=ListView_GetItemCount(m_hWnd))
			return 0;
	}
	if (m_Combo.m_hWnd)
	{
		SetFocus();
		DestroyCombo();
	}
	ListView_EnsureVisible(m_hWnd,line,FALSE);
	ListView_SetItemState(m_hWnd,line,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
	if (column>0)
	{
		CreateCombo(line,column);
	}
	return 0;
}

LRESULT CCustomMenuDlg7::CItemList::OnEndTrack( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	PostMessage(WM_RESIZE_COMBO);
	bHandled=FALSE;
	return 0;
}

LRESULT CCustomMenuDlg7::CItemList::OnCustomDraw( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	NMLVCUSTOMDRAW *pDraw=(NMLVCUSTOMDRAW*)pnmh;
	if (pDraw->nmcd.dwDrawStage==CDDS_PREPAINT)
		return CDRF_NOTIFYITEMDRAW;
	if (pDraw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT)
	{
		const CCustomMenuDlg7::MenuItem &menuItem=m_pOwner->m_Items[pDraw->nmcd.dwItemSpec];
		if (menuItem.settings&StdMenuItem::MENU_ITEM_DISABLED)
			pDraw->clrText=GetSysColor(COLOR_GRAYTEXT);
		if (m_pOwner->m_pSetting->IsLocked())
			pDraw->clrTextBk=GetSysColor(COLOR_BTNFACE);
		return CDRF_NOTIFYPOSTPAINT;
	}
	if (pDraw->nmcd.dwDrawStage==CDDS_ITEMPOSTPAINT)
	{
		const CCustomMenuDlg7::MenuItem &menuItem=m_pOwner->m_Items[pDraw->nmcd.dwItemSpec];
		int icon=LoadIcon(menuItem);
		if (icon>=0)
		{
			int iconSize=GetSystemMetrics(SM_CXSMICON);
			RECT rc;
			ListView_GetItemRect(m_hWnd,pDraw->nmcd.dwItemSpec,&rc,LVIR_ICON);
			int x=(rc.right-iconSize)/2;
			int y=(rc.top+rc.bottom-iconSize)/2;
			if (!m_bThemed && (ListView_GetItemState(m_hWnd,pDraw->nmcd.dwItemSpec,LVIS_SELECTED)&LVIS_SELECTED))
				ImageList_DrawEx(m_ImageList,icon,pDraw->nmcd.hdc,x,y,0,0,CLR_NONE,CLR_DEFAULT,ILD_SELECTED);
			else if (menuItem.settings&StdMenuItem::MENU_ITEM_DISABLED)
				ImageList_DrawEx(m_ImageList,icon,pDraw->nmcd.hdc,x,y,0,0,CLR_NONE,GetSysColor(COLOR_WINDOW),ILD_SELECTED);
			else
				ImageList_DrawEx(m_ImageList,icon,pDraw->nmcd.hdc,x,y,0,0,CLR_NONE,CLR_DEFAULT,ILD_NORMAL);
		}
	}
	return CDRF_DODEFAULT;
}

LRESULT CCustomMenuDlg7::CItemList::OnSelEndOk( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	if (m_Line<0) return 0;
	CWindow combo=hWndCtl;
	int sel=(int)combo.SendMessage(CB_GETCURSEL);
	if (sel<0) return 0;
	MenuItem &menuItem=m_pOwner->m_Items[m_Line];
	if (m_Column==1)
	{
		// type
		const CStdCommand7 &command=g_StdCommands7[sel];
		menuItem.stdItemIndex=sel;
		menuItem.command=command.command;
		menuItem.link.Empty();
		menuItem.label=command.label;
		menuItem.tip=command.tip;
		menuItem.icon=command.icon;
		menuItem.settings=command.settings|(menuItem.settings&CEditMenuDlg7::SETTINGS_MASK);
	}
	if (m_Column==2)
	{
		// state
		menuItem.settings&=~CEditMenuDlg7::SETTINGS_MASK;
		if (sel==0)
			menuItem.settings|=StdMenuItem::MENU_ITEM_DISABLED;
		else if (sel==1 && !(g_StdCommands7[menuItem.stdItemIndex].flags&(CStdCommand7::ITEM_SINGLE|CStdCommand7::ITEM_FOLDER)))
			menuItem.settings|=StdMenuItem::MENU_NOEXPAND;
		else if (sel==3 && !(g_StdCommands7[menuItem.stdItemIndex].flags&CStdCommand7::ITEM_NODRIVES))
			menuItem.settings|=StdMenuItem::MENU_SINGLE_EXPAND;
	}
	UpdateItem(m_Line);
	RECT rc;
	ListView_GetItemRect(m_hWnd,m_Line,&rc,LVIR_BOUNDS);
	InvalidateRect(&rc);
	m_pOwner->SerializeData();
	return 0;
}

void CCustomMenuDlg7::CItemList::EditItem( int index )
{
	if (m_pOwner->m_pSetting->IsLocked())
		return;
	DestroyCombo();
	CEditMenuDlg7 dlg(m_pOwner,&m_pOwner->m_Items[index]);
	if (dlg.Run())
	{
		UpdateItem(index);
		m_pOwner->SerializeData();
	}
	else
		UpdateItem(index);
}

void CCustomMenuDlg7::CItemList::DeleteItem( int index )
{
	if (m_pOwner->m_pSetting->IsLocked())
		return;
	DestroyCombo();
	ListView_DeleteItem(m_hWnd,index);
	m_pOwner->m_Items.erase(m_pOwner->m_Items.begin()+index);
	m_pOwner->SerializeData();
}

void CCustomMenuDlg7::CItemList::InsertItem( int index, int copy )
{
	if (m_pOwner->m_pSetting->IsLocked())
		return;
	DestroyCombo();
	MenuItem menuItem;
	if (copy>=0)
		menuItem=m_pOwner->m_Items[copy];
	else
	{
		menuItem.stdItemIndex=1;
		menuItem.command=g_StdCommands7[1].command;
		menuItem.settings=0;
	}
	m_pOwner->m_Items.insert(m_pOwner->m_Items.begin()+index,menuItem);
	LVITEM item={LVIF_IMAGE,index};
	item.iImage=I_IMAGENONE;
	ListView_InsertItem(m_hWnd,&item);
	ListView_EnsureVisible(m_hWnd,index,FALSE);
	UpdateItem(index);
	m_pOwner->SerializeData();
	
	ListView_SetItemState(m_hWnd,index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
	m_Line=index;
	if (copy<0)
	{
		m_Column=1;
		CreateCombo(m_Line,1);
	}
}

void CCustomMenuDlg7::CItemList::MoveItem( int from, int to )
{
	if (m_pOwner->m_pSetting->IsLocked())
		return;
	DestroyCombo();
	SetRedraw(FALSE);
	MenuItem menuItem=m_pOwner->m_Items[from];
	m_pOwner->m_Items.erase(m_pOwner->m_Items.begin()+from);
	ListView_DeleteItem(m_hWnd,from);
	if (from<to) to--;
	m_pOwner->m_Items.insert(m_pOwner->m_Items.begin()+to,menuItem);

	LVITEM item={LVIF_IMAGE,to};
	item.iImage=I_IMAGENONE;
	ListView_InsertItem(m_hWnd,&item);
	UpdateItem(to);
	ListView_SetItemState(m_hWnd,to,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
	ListView_EnsureVisible(m_hWnd,to,FALSE);
	SetRedraw(TRUE);
	Invalidate();
	m_pOwner->SerializeData();
}

LRESULT CALLBACK CCustomMenuDlg7::CItemList::SubclassGridCombo( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_KEYDOWN)
	{
		if (wParam==VK_ESCAPE || (wParam==VK_RETURN && !::SendMessage(hWnd,CB_GETDROPPEDSTATE,0,0)))
		{
			::PostMessage(::GetParent(hWnd),WM_RESIZE_COMBO,0,1);
		}
		if (wParam==VK_TAB)
		{
			::PostMessage(::GetParent(hWnd),WM_HANDLE_TAB,GetKeyState(VK_SHIFT)<0,0);
		}
	}
	if (uMsg==WM_GETDLGCODE)
		return DLGC_WANTALLKEYS;
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

void CCustomMenuDlg7::CItemList::CreateCombo( int line, int column )
{
	if (m_pOwner->m_pSetting->IsLocked())
		return;
	m_Combo.Create(WC_COMBOBOX,m_hWnd,NULL,NULL,WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL);
	m_Combo.SetFont(GetFont());
	m_Combo.SendMessage(CB_SETEXTENDEDUI,TRUE);
	SetWindowSubclass(m_Combo,SubclassGridCombo,'CLSH',0);

	m_Line=line;
	m_Column=column;
	const CCustomMenuDlg7::MenuItem &menuItem=m_pOwner->m_Items[line];
	if (column==1)
	{
		for (int i=0;i<g_StdCommands7Count;i++)
			m_Combo.SendMessage(CB_ADDSTRING,0,(LPARAM)(const wchar_t*)g_StdCommands7[i].displayName);
		m_Combo.SendMessage(CB_SETCURSEL,menuItem.stdItemIndex);
	}
	else
	{
		Assert(column==2);
		CString str=LoadStringEx(IDS_ITEM_HIDE);
		m_Combo.SendMessage(CB_ADDSTRING,0,(LPARAM)(const wchar_t*)str);
		if (g_StdCommands7[menuItem.stdItemIndex].flags&(CStdCommand7::ITEM_SINGLE|CStdCommand7::ITEM_FOLDER))
		{
			str=LoadStringEx((g_StdCommands7[menuItem.stdItemIndex].flags&CStdCommand7::ITEM_SINGLE)?IDS_ITEM_SHOW2:IDS_ITEM_MENU);
			m_Combo.SendMessage(CB_ADDSTRING,0,(LPARAM)(const wchar_t*)str);
		}
		else
		{
			str=LoadStringEx(IDS_ITEM_SHOW);
			m_Combo.SendMessage(CB_ADDSTRING,0,(LPARAM)(const wchar_t*)str);
			str=LoadStringEx(IDS_ITEM_MENU);
			m_Combo.SendMessage(CB_ADDSTRING,0,(LPARAM)(const wchar_t*)str);
			if (g_StdCommands7[menuItem.stdItemIndex].flags&CStdCommand7::ITEM_COMPUTER)
			{
				str=LoadStringEx(IDS_ITEM_DRIVES);
				m_Combo.SendMessage(CB_ADDSTRING,0,(LPARAM)(const wchar_t*)str);
			}
			else if (!(g_StdCommands7[menuItem.stdItemIndex].flags&CStdCommand7::ITEM_NODRIVES))
			{
				str=LoadStringEx(IDS_ITEM_LINKS);
				m_Combo.SendMessage(CB_ADDSTRING,0,(LPARAM)(const wchar_t*)str);
			}
		}
		if (menuItem.settings&StdMenuItem::MENU_ITEM_DISABLED)
			m_Combo.SendMessage(CB_SETCURSEL,0);
		else if ((g_StdCommands7[menuItem.stdItemIndex].flags&(CStdCommand7::ITEM_SINGLE|CStdCommand7::ITEM_FOLDER)) || (menuItem.settings&StdMenuItem::MENU_NOEXPAND))
			m_Combo.SendMessage(CB_SETCURSEL,1);
		else if (!(g_StdCommands7[menuItem.stdItemIndex].flags&CStdCommand7::ITEM_NODRIVES) && (menuItem.settings&StdMenuItem::MENU_SINGLE_EXPAND))
			m_Combo.SendMessage(CB_SETCURSEL,3);
		else
			m_Combo.SendMessage(CB_SETCURSEL,2);
	}
	PostMessage(WM_RESIZE_COMBO);
}

void CCustomMenuDlg7::CItemList::DestroyCombo( void )
{
	if (m_Combo.m_hWnd)
	{
		RemoveWindowSubclass(m_Combo,SubclassGridCombo,'CLSH');
		m_Combo.DestroyWindow();
	}
}

LRESULT CCustomMenuDlg7::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	m_pSetting=NULL;

	InitResize();
	EnableThemeDialogTexture(m_hWnd,ETDT_ENABLETAB);
	m_List.SubclassWindow(GetDlgItem(IDC_LISTITEMS),this);

	TOOLINFO tool={sizeof(tool),TTF_SUBCLASS|TTF_IDISHWND,m_hWnd,'CLSH'};
	tool.uId=(UINT_PTR)m_List.m_hWnd;
	tool.lpszText=(LPWSTR)L"";
	::SendMessage(ListView_GetToolTips(m_List),TTM_ADDTOOL,0,(LPARAM)&tool);

	return TRUE;
}

void CCustomMenuDlg7::SetGroup( CSetting *pGroup, bool bReset )
{
	if (!m_pSetting || bReset) // optimization - initialize the UI only the first time
	{
		m_pSetting=pGroup+1;
		m_List.SetDisabled(m_pSetting->IsLocked());
		const wchar_t *pText=L"";
		if (m_pSetting->value.vt==VT_BSTR)
			pText=m_pSetting->value.bstrVal;

		CSettingsParser parser;
		parser.LoadText(pText,Strlen(pText));
		parser.ParseText();

		m_Items.clear();
		for (int i=1;;i++)
		{
			wchar_t item[100];
			Sprintf(item,_countof(item),L"Item%d.Command",i);
			const wchar_t *command=parser.FindSetting(item);
			Sprintf(item,_countof(item),L"Item%d.Link",i);
			const wchar_t *link=parser.FindSetting(item);
			if (!command && !link)
				break;

			MenuItem menuItem;
			menuItem.stdItemIndex=g_StdCommands7Count-1;
			menuItem.settings=0;
			if (command)
			{
				menuItem.command=command;
				for (int i=0;i<g_StdCommands7Count-1;i++)
					if (_wcsicmp(command,g_StdCommands7[i].command)==0)
					{
						menuItem.stdItemIndex=i;
						break;
					}
				if (menuItem.stdItemIndex==0)
				{
					m_Items.push_back(menuItem);
					continue;
				}
			}
			if (link) menuItem.link=link;
			Sprintf(item,_countof(item),L"Item%d.Label",i);
			const wchar_t *str=parser.FindSetting(item);
			if (str) menuItem.label=str;
			Sprintf(item,_countof(item),L"Item%d.Tip",i);
			str=parser.FindSetting(item);
			if (str) menuItem.tip=str;
			Sprintf(item,_countof(item),L"Item%d.Icon",i);
			str=parser.FindSetting(item);
			if (str) menuItem.icon=str;
			Sprintf(item,_countof(item),L"Item%d.Settings",i);
			str=parser.FindSetting(item);
			if (str)
			{
				while (*str)
				{
					wchar_t token[256];
					str=GetToken(str,token,_countof(token),L", \t|;");
					if (_wcsicmp(token,L"OPEN_UP")==0) menuItem.settings|=StdMenuItem::MENU_OPENUP;
					if (_wcsicmp(token,L"OPEN_UP_CHILDREN")==0) menuItem.settings|=StdMenuItem::MENU_OPENUP_REC;
					if (_wcsicmp(token,L"SORT_ZA")==0) menuItem.settings|=StdMenuItem::MENU_SORTZA;
					if (_wcsicmp(token,L"SORT_ZA_CHILDREN")==0) menuItem.settings|=StdMenuItem::MENU_SORTZA_REC;
					if (_wcsicmp(token,L"SORT_ONCE")==0) menuItem.settings|=StdMenuItem::MENU_SORTONCE;
					if (_wcsicmp(token,L"TRACK_RECENT")==0) menuItem.settings|=StdMenuItem::MENU_TRACK;
					if (_wcsicmp(token,L"NOEXPAND")==0) menuItem.settings|=StdMenuItem::MENU_NOEXPAND;
					else if (_wcsicmp(token,L"SINGLE_EXPAND")==0) menuItem.settings|=StdMenuItem::MENU_SINGLE_EXPAND;
					if (_wcsicmp(token,L"MULTICOLUMN")==0) menuItem.settings|=StdMenuItem::MENU_MULTICOLUMN;
					if (_wcsicmp(token,L"NOEXTENSIONS")==0) menuItem.settings|=StdMenuItem::MENU_NOEXTENSIONS;
					if (_wcsicmp(token,L"ITEM_DISABLED")==0) menuItem.settings|=StdMenuItem::MENU_ITEM_DISABLED;
				}
			}
			m_Items.push_back(menuItem);
		}
	}
	m_List.DestroyCombo();
	ListView_DeleteAllItems(m_List);
	int idx=0;
	for (std::vector<MenuItem>::const_iterator it=m_Items.begin();it!=m_Items.end();++it,idx++)
	{
		LVITEM item={LVIF_IMAGE,idx};
		item.iImage=I_IMAGENONE;
		ListView_InsertItem(m_List,&item);
		m_List.UpdateItem(idx);
	}

	TOOLINFO tool={sizeof(tool),0,m_hWnd,'CLSH'};
	tool.uId=(UINT_PTR)m_List.m_hWnd;
	CString str;
	if (m_pSetting->IsLocked())
		str=LoadStringEx(IDS_MENU_LOCKED);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	::SendMessage(ListView_GetToolTips(m_List),TTM_UPDATETIPTEXT,0,(LPARAM)&tool);

	ListView_SetBkColor(m_List,GetSysColor(m_pSetting->IsLocked()?COLOR_BTNFACE:COLOR_WINDOW));
}

LRESULT CCustomMenuDlg7::OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	CResizeableDlg<CCustomMenuDlg7>::OnSize();
	return 0;
}

LRESULT CCustomMenuDlg7::OnContextMenu( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	CWindow list=(HWND)wParam;
	if (list.m_hWnd!=m_List.m_hWnd)
		return 0;
	if (m_pSetting->IsLocked())
		return 0;
	if (GetCapture()==list)
	{
		ReleaseCapture();
		return 0;
	}
	POINT pt;
	int index;
	if ((DWORD)lParam==0xFFFFFFFF)
	{
		// keyboard
		index=ListView_GetNextItem(list,-1,LVNI_FOCUSED);
		if (index>=0)
		{
			RECT rc;
			ListView_GetItemRect(list,m_List.m_Line,&rc,LVIR_BOUNDS);
			pt.x=(rc.left+rc.right)/2;
			pt.y=(rc.top+rc.bottom)/2;
		}
		else
			pt.x=pt.y=0;
		m_List.ClientToScreen(&pt);
	}
	else
	{
		DWORD pos=GetMessagePos();
		pt.x=(short)LOWORD(pos);
		pt.y=(short)HIWORD(pos);
		LVHITTESTINFO test={pt};
		m_List.ScreenToClient(&test.pt);
		index=ListView_HitTest(list,&test);
	}

	enum {
		CMD_EDIT=1,
		CMD_DELETE,
		CMD_DUPLICATE,
		CMD_INSERT,
		CMD_RESETALL,
		CMD_MOVE_UP,
		CMD_MOVE_DOWN,
	};

	m_List.DestroyCombo();

	HMENU menu=CreatePopupMenu();
	wchar_t text[256];
	if (index>=0)
	{
		ListView_SetItemState(list,index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
		Sprintf(text,_countof(text),L"%s\tEnter",LoadStringEx(IDS_MENU_EDIT));
		AppendMenu(menu,MF_STRING,CMD_EDIT,text);
		SetMenuDefaultItem(menu,CMD_EDIT,FALSE);
		Sprintf(text,_countof(text),L"%s\tDel",LoadStringEx(IDS_MENU_DELETE));
		AppendMenu(menu,MF_STRING,CMD_DELETE,text);
		AppendMenu(menu,MF_STRING,CMD_DUPLICATE,LoadStringEx(IDS_MENU_DUPLICATE));
		Sprintf(text,_countof(text),L"%s\tCtrl+Up",LoadStringEx(IDS_MENU_MOVE_UP));
		AppendMenu(menu,MF_STRING,CMD_MOVE_UP,text);
		Sprintf(text,_countof(text),L"%s\tCtrl+Down",LoadStringEx(IDS_MENU_MOVE_DOWN));
		AppendMenu(menu,MF_STRING,CMD_MOVE_DOWN,text);
		if (index==0)
			EnableMenuItem(menu,CMD_MOVE_UP,MF_BYCOMMAND|MF_GRAYED);
		if (index==(int)m_Items.size()-1)
			EnableMenuItem(menu,CMD_MOVE_DOWN,MF_BYCOMMAND|MF_GRAYED);
		AppendMenu(menu,MF_SEPARATOR,0,0);
	}
	Sprintf(text,_countof(text),L"%s\tInsert",LoadStringEx(IDS_MENU_INSERT));
	AppendMenu(menu,MF_STRING,CMD_INSERT,text);
	if (index<0)
		SetMenuDefaultItem(menu,CMD_INSERT,FALSE);
	AppendMenu(menu,MF_STRING,CMD_RESETALL,LoadStringEx(IDS_RESET_MENU));

	int res=TrackPopupMenu(menu,TPM_RIGHTBUTTON|TPM_RETURNCMD,pt.x,pt.y,0,m_hWnd,NULL);
	DestroyMenu(menu);
	if (res==CMD_EDIT)
		m_List.EditItem(index);
	if (res==CMD_DELETE)
		m_List.DeleteItem(index);
	if (res==CMD_INSERT)
		m_List.InsertItem(index<0?(int)m_Items.size():(index+1),-1);
	if (res==CMD_DUPLICATE)
		m_List.InsertItem(index<0?(int)m_Items.size():(index+1),index);
	if (res==CMD_MOVE_UP)
		m_List.MoveItem(index,index-1);
	if (res==CMD_MOVE_DOWN)
		m_List.MoveItem(index,index+2);
	if (res==CMD_RESETALL)
	{
		if (::MessageBox(m_hWnd,LoadStringEx(IDS_RESET_MENU_WARN),LoadStringEx(IDS_RESET_MENU),MB_YESNO|MB_ICONWARNING)==IDYES)
		{
			{
				CSettingsLockWrite lock;
				m_pSetting->value=m_pSetting->defValue;
				m_pSetting->flags|=CSetting::FLAG_DEFAULT;
				SetSettingsDirty();
			}
			SetGroup(m_pSetting-1,true);
		}
	}
	return 0;
}

static void AppendString( std::vector<wchar_t> &stringBuilder, const wchar_t *format, ... )
{
	va_list args;
	va_start(args,format);
	wchar_t buf[1024];
	int len=Vsprintf(buf,_countof(buf),format,args);
	va_end(args);
	int pos=(int)stringBuilder.size();
	stringBuilder.resize(pos+len);
	memcpy(&stringBuilder[pos],buf,len*2);
}

void CCustomMenuDlg7::SerializeData( void )
{
	std::vector<wchar_t> stringBuilder;
	for (int i=0;i<(int)m_Items.size();i++)
	{
		const MenuItem &menuItem=m_Items[i];
		const CStdCommand7 &command=g_StdCommands7[menuItem.stdItemIndex];
		if (menuItem.stdItemIndex<g_StdCommands7Count-1)
		{
			AppendString(stringBuilder,L"Item%d.Command=%s\n",i+1,command.command);
		}
		else
		{
			if (!menuItem.command.IsEmpty())
				AppendString(stringBuilder,L"Item%d.Command=%s\n",i+1,menuItem.command);
			if (!menuItem.link.IsEmpty())
				AppendString(stringBuilder,L"Item%d.Link=%s\n",i+1,menuItem.link);
			if (menuItem.command.IsEmpty() && menuItem.link.IsEmpty())
				AppendString(stringBuilder,L"Item%d.Command=<blank>\n",i+1,menuItem.command);
		}
		if (!menuItem.label.IsEmpty())
			AppendString(stringBuilder,L"Item%d.Label=%s\n",i+1,menuItem.label);
		if (!menuItem.tip.IsEmpty())
			AppendString(stringBuilder,L"Item%d.Tip=%s\n",i+1,menuItem.tip);
		if (!menuItem.icon.IsEmpty())
			AppendString(stringBuilder,L"Item%d.Icon=%s\n",i+1,menuItem.icon);
		if (menuItem.settings)
		{
			AppendString(stringBuilder,L"Item%d.Settings=",i+1);
			if (menuItem.settings&StdMenuItem::MENU_OPENUP) AppendString(stringBuilder,L"OPEN_UP|");
			if (menuItem.settings&StdMenuItem::MENU_OPENUP_REC) AppendString(stringBuilder,L"OPEN_UP_CHILDREN|");
			if (menuItem.settings&StdMenuItem::MENU_SORTZA) AppendString(stringBuilder,L"SORT_ZA|");
			if (menuItem.settings&StdMenuItem::MENU_SORTZA_REC) AppendString(stringBuilder,L"SORT_ZA_CHILDREN|");
			if (menuItem.settings&StdMenuItem::MENU_SORTONCE) AppendString(stringBuilder,L"SORT_ONCE|");
			if (menuItem.settings&StdMenuItem::MENU_TRACK) AppendString(stringBuilder,L"TRACK_RECENT|");
			if (menuItem.settings&StdMenuItem::MENU_NOEXPAND) AppendString(stringBuilder,L"NOEXPAND|");
			else if (menuItem.settings&StdMenuItem::MENU_SINGLE_EXPAND) AppendString(stringBuilder,L"SINGLE_EXPAND|");
			if (menuItem.settings&StdMenuItem::MENU_MULTICOLUMN) AppendString(stringBuilder,L"MULTICOLUMN|");
			if (menuItem.settings&StdMenuItem::MENU_NOEXTENSIONS) AppendString(stringBuilder,L"NOEXTENSIONS|");
			if (menuItem.settings&StdMenuItem::MENU_ITEM_DISABLED) AppendString(stringBuilder,L"ITEM_DISABLED|");
			stringBuilder[stringBuilder.size()-1]='\n';
		}
	}
	stringBuilder.push_back(0);
	CSettingsLockWrite lock;
	m_pSetting->value=CComVariant(&stringBuilder[0]);
	m_pSetting->flags&=~CSetting::FLAG_DEFAULT;
	SetSettingsDirty();
}

class CCustomMenuPanel7: public ISettingsPanel
{
public:
	virtual HWND Create( HWND parent );
	virtual HWND Activate( CSetting *pGroup, const RECT &rect, bool bReset );
	virtual bool Validate( HWND parent ) { return true; }

private:
	static CCustomMenuDlg7 s_Dialog;
};

CCustomMenuDlg7 CCustomMenuPanel7::s_Dialog;

HWND CCustomMenuPanel7::Create( HWND parent )
{
	if (!s_Dialog.m_hWnd)
		s_Dialog.Create(parent,LoadDialogEx(IDD_CUSTOMLIST));
	return s_Dialog.m_hWnd;
}

HWND CCustomMenuPanel7::Activate( CSetting *pGroup, const RECT &rect, bool bReset )
{
	s_Dialog.SetGroup(pGroup,bReset);
	s_Dialog.SetWindowPos(HWND_TOP,&rect,SWP_SHOWWINDOW);
	return s_Dialog.m_hWnd;
}

static CCustomMenuPanel7 g_CustomMenuPanel7;

///////////////////////////////////////////////////////////////////////////////

class CMenuStyleDlg: public CResizeableDlg<CMenuStyleDlg>
{
public:
	BEGIN_MSG_MAP( CMenuStyleDlg )
		MESSAGE_HANDLER( WM_INITDIALOG, OnInitDialog )
		MESSAGE_HANDLER( WM_DESTROY, OnDestroy )
		MESSAGE_HANDLER( WM_SIZE, OnSize )

		// menu style
		MESSAGE_HANDLER( WM_SETCURSOR, OnSetCursor )
		COMMAND_HANDLER( IDC_STATIC_CLASSIC, STN_CLICKED, OnClick )
		COMMAND_HANDLER( IDC_STATIC_TWO_COLUMNS, STN_CLICKED, OnClick )
		COMMAND_HANDLER( IDC_STATIC_WIN7, STN_CLICKED, OnClick )
		COMMAND_HANDLER( IDC_RADIO_CLASSIC, BN_CLICKED, OnClick )
		COMMAND_HANDLER( IDC_RADIO_TWO_COLUMNS, BN_CLICKED, OnClick )
		COMMAND_HANDLER( IDC_RADIO_WIN7, BN_CLICKED, OnClick )

		// button style
		COMMAND_HANDLER( IDC_CHECKENABLED, BN_CLICKED, OnEnabled )
		COMMAND_HANDLER( IDC_STATICAERO, STN_CLICKED, OnButtonStyle )
		COMMAND_HANDLER( IDC_STATICCLASSIC, STN_CLICKED, OnButtonStyle )
		COMMAND_HANDLER( IDC_STATICCUSTOM, STN_CLICKED, OnButtonStyle )
		COMMAND_HANDLER( IDC_RADIOAERO, BN_CLICKED, OnButtonStyle )
		COMMAND_HANDLER( IDC_RADIOCLASSIC, BN_CLICKED, OnButtonStyle )
		COMMAND_HANDLER( IDC_RADIOCUSTOM, BN_CLICKED, OnButtonStyle )
		COMMAND_HANDLER( IDC_BUTTONPICK, BN_CLICKED, OnPick )
		NOTIFY_HANDLER( IDC_LINKADVANCED, NM_CLICK, OnAdvanced )
		NOTIFY_HANDLER( IDC_LINKADVANCED, NM_RETURN, OnAdvanced )

		NOTIFY_HANDLER( IDC_SKIN_CLASSIC, NM_CLICK, OnSkin )
		NOTIFY_HANDLER( IDC_SKIN_CLASSIC, NM_RETURN, OnSkin )
		NOTIFY_HANDLER( IDC_SKIN_CLASSIC2, NM_CLICK, OnSkin )
		NOTIFY_HANDLER( IDC_SKIN_CLASSIC2, NM_RETURN, OnSkin )
		NOTIFY_HANDLER( IDC_SKIN_WIN7, NM_CLICK, OnSkin )
		NOTIFY_HANDLER( IDC_SKIN_WIN7, NM_RETURN, OnSkin )
	END_MSG_MAP()

	BEGIN_RESIZE_MAP
		RESIZE_CONTROL(IDC_STATICTITLE,MOVE_SIZE_X)
		RESIZE_CONTROL(IDC_STATICEDGE,MOVE_SIZE_X)
	END_RESIZE_MAP

	static void UpdateDefaults( void );

	void Update( bool bForce );

protected:
	// Handler prototypes:
	//  LRESULT MessageHandler(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	//  LRESULT CommandHandler(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	//  LRESULT NotifyHandler(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSetCursor( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnClick( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnEnabled( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnButtonStyle( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnPick( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnAdvanced( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnSkin( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );

protected:
	CWindow m_ImageClassic1, m_ImageClassic2, m_ImageWin7;
	CWindow m_Tooltip;
	CWindow m_ButtonAero, m_ButtonClassic, m_ButtonCustom;
	HICON m_hIcon;
	CString m_IconPath;

	void UpdateIcon( bool bForce );
};

static void UpdateButtons( bool bForce );

LRESULT CMenuStyleDlg::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	InitResize();
	EnableThemeDialogTexture(m_hWnd,ETDT_ENABLETAB);
	m_ImageClassic1=GetDlgItem(IDC_STATIC_CLASSIC);
	m_ImageClassic2=GetDlgItem(IDC_STATIC_TWO_COLUMNS);
	m_ImageWin7=GetDlgItem(IDC_STATIC_WIN7);
	HDC hdc=::GetDC(NULL);
	int dpi=GetDeviceCaps(hdc,LOGPIXELSY);
	::ReleaseDC(NULL,hdc);
	bool bLargeBitmaps=dpi>=144;
	{
		HBITMAP bmp=LoadImageResource(g_Instance,MAKEINTRESOURCE(bLargeBitmaps?IDB_STYLE_CLASSIC1150:IDB_STYLE_CLASSIC1),true,true);
		m_ImageClassic1.SendMessage(STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)bmp);
		bmp=LoadImageResource(g_Instance,MAKEINTRESOURCE(bLargeBitmaps?IDB_STYLE_CLASSIC2150:IDB_STYLE_CLASSIC2),true,true);
		m_ImageClassic2.SendMessage(STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)bmp);
		bmp=LoadImageResource(g_Instance,MAKEINTRESOURCE(bLargeBitmaps?IDB_STYLE_WIN7150:IDB_STYLE_WIN7),true,true);
		m_ImageWin7.SendMessage(STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)bmp);
	}

	bool bLocked=IsSettingLocked(L"MenuStyle");
	CString tip1=LoadStringEx(bLocked?IDS_SETTING_LOCKED:IDS_CLASSIC_STYLE_TIP);
	CString tip2=LoadStringEx(bLocked?IDS_SETTING_LOCKED:IDS_TWO_COLUMN_STYLE_TIP);
	CString tip3=LoadStringEx(bLocked?IDS_SETTING_LOCKED:IDS_WIN7_STYLE_TIP);
	bool bRTL=IsLanguageRTL();
	m_Tooltip=CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT|(bRTL?WS_EX_LAYOUTRTL:0),TOOLTIPS_CLASS,NULL,WS_POPUP|TTS_NOPREFIX,0,0,0,0,m_hWnd,NULL,g_Instance,NULL);
	m_Tooltip.SendMessage(TTM_SETMAXTIPWIDTH,0,500);
	TOOLINFO tool={sizeof(tool),TTF_IDISHWND|TTF_SUBCLASS|(bRTL?TTF_RTLREADING:0U)};
	tool.uId=(UINT_PTR)m_ImageClassic1.m_hWnd;
	tool.lpszText=(wchar_t*)(const wchar_t*)tip1;
	m_Tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);
	tool.uId=(UINT_PTR)m_ImageClassic2.m_hWnd;
	tool.lpszText=(wchar_t*)(const wchar_t*)tip2;
	m_Tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);
	tool.uId=(UINT_PTR)m_ImageWin7.m_hWnd;
	tool.lpszText=(wchar_t*)(const wchar_t*)tip3;
	m_Tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	m_hIcon=NULL;
	m_IconPath="?";
	SetDlgItemText(IDC_CHECKENABLED,LoadStringEx(GetWinVersion()==WIN_VER_WIN8?IDS_ENABLE_BUTTON:IDS_ENABLE_BUTTON2));
	m_ButtonAero=GetDlgItem(IDC_STATICAERO);
	m_ButtonClassic=GetDlgItem(IDC_STATICCLASSIC);
	m_ButtonCustom=GetDlgItem(IDC_STATICCUSTOM);
	return TRUE;
}

LRESULT CMenuStyleDlg::OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_hIcon) DestroyIcon(m_hIcon);
	m_hIcon=NULL;
	{
		HBITMAP bmp=(HBITMAP)m_ImageClassic1.SendMessage(STM_GETIMAGE,IMAGE_BITMAP);
		if (bmp) DeleteObject(bmp);
		bmp=(HBITMAP)m_ImageClassic2.SendMessage(STM_GETIMAGE,IMAGE_BITMAP);
		if (bmp) DeleteObject(bmp);
		bmp=(HBITMAP)m_ImageWin7.SendMessage(STM_GETIMAGE,IMAGE_BITMAP);
		if (bmp) DeleteObject(bmp);
	}
	UpdateButtons(false);
	bHandled=FALSE;
	return 0;
}

LRESULT CMenuStyleDlg::OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	CResizeableDlg<CMenuStyleDlg>::OnSize();
	return 0;
}

LRESULT CMenuStyleDlg::OnSetCursor( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if ((HWND)wParam==m_ImageClassic1.m_hWnd || (HWND)wParam==m_ImageClassic2.m_hWnd || (HWND)wParam==m_ImageWin7.m_hWnd || (HWND)wParam==m_ButtonAero.m_hWnd || (HWND)wParam==m_ButtonClassic.m_hWnd || (HWND)wParam==m_ButtonCustom.m_hWnd)
	{
		SetCursor(LoadCursor(NULL,IDC_HAND));
		return TRUE;
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CMenuStyleDlg::OnClick( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	static bool bProcessing;
	if (bProcessing) return 0;

	if (IsSettingLocked(L"MenuStyle"))
		return 0;
	bProcessing=true;
	bool bReset=false;
	{
		TMenuStyle style, style0=(TMenuStyle)GetSettingInt(L"MenuStyle");
		int styleFlag;
		CSettingsLockWrite lock;

		CSetting *pSetting=FindSetting(L"MenuStyle");
		if (wID==IDC_STATIC_WIN7 || wID==IDC_RADIO_WIN7)
		{
			style=MENU_WIN7;
			styleFlag=CSetting::FLAG_MENU_WIN7;
			SendMessage(WM_NEXTDLGCTL,(LPARAM)GetDlgItem(IDC_RADIO_WIN7).m_hWnd,TRUE);
		}
		else if (wID==IDC_STATIC_TWO_COLUMNS || wID==IDC_RADIO_TWO_COLUMNS)
		{
			style=MENU_CLASSIC2;
			styleFlag=CSetting::FLAG_MENU_CLASSIC2;
			SendMessage(WM_NEXTDLGCTL,(LPARAM)GetDlgItem(IDC_RADIO_TWO_COLUMNS).m_hWnd,TRUE);
		}
		else
		{
			style=MENU_CLASSIC1;
			styleFlag=CSetting::FLAG_MENU_CLASSIC1;
			SendMessage(WM_NEXTDLGCTL,(LPARAM)GetDlgItem(IDC_RADIO_CLASSIC).m_hWnd,TRUE);
		}
		pSetting->value=CComVariant(style);
		CheckDlgButton(IDC_RADIO_CLASSIC,pSetting->value.intVal==MENU_CLASSIC1?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(IDC_RADIO_TWO_COLUMNS,pSetting->value.intVal==MENU_CLASSIC2?BST_CHECKED:BST_UNCHECKED);
		CheckDlgButton(IDC_RADIO_WIN7,pSetting->value.intVal==MENU_WIN7?BST_CHECKED:BST_UNCHECKED);
		pSetting->flags&=~CSetting::FLAG_DEFAULT;
		SetSettingsDirty();

		SetSettingsStyle(styleFlag,CSetting::FLAG_MENU_MASK);
		UpdateDefaults();
		CCustomMenuDlg::UpdateWarnings(style);
	}
	bProcessing=false;
	return 0;
}

LRESULT CMenuStyleDlg::OnEnabled( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	BOOL bEnabled=(IsDlgButtonChecked(IDC_CHECKENABLED)==BST_CHECKED);
	{
		CSettingsLockWrite lock;
		CSetting *pSetting=FindSetting(L"EnableStartButton");
		pSetting->value=CComVariant(bEnabled);
		pSetting->flags&=~CSetting::FLAG_DEFAULT;
		SetSettingsDirty();
	}
	Update(false);
	return 0;
}

LRESULT CMenuStyleDlg::OnButtonStyle( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	int style=START_BUTTON_AERO;
	if (IsDlgButtonChecked(IDC_RADIOCLASSIC)==BST_CHECKED)
		style=START_BUTTON_CLASSIC;
	else if (IsDlgButtonChecked(IDC_RADIOCUSTOM)==BST_CHECKED)
		style=START_BUTTON_CUSTOM;

	if (wID==IDC_STATICAERO || wID==IDC_RADIOAERO)
	{
		style=START_BUTTON_AERO;
		SendMessage(WM_NEXTDLGCTL,(LPARAM)GetDlgItem(IDC_RADIOAERO).m_hWnd,TRUE);
	}
	else if (wID==IDC_STATICCLASSIC || wID==IDC_RADIOCLASSIC)
	{
		style=START_BUTTON_CLASSIC;
		SendMessage(WM_NEXTDLGCTL,(LPARAM)GetDlgItem(IDC_RADIOCLASSIC).m_hWnd,TRUE);
	}
	else
	{
		style=START_BUTTON_CUSTOM;
		SendMessage(WM_NEXTDLGCTL,(LPARAM)GetDlgItem(IDC_RADIOCUSTOM).m_hWnd,TRUE);
	}

	CheckDlgButton(IDC_RADIOAERO,style==START_BUTTON_AERO?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_RADIOCLASSIC,style==START_BUTTON_CLASSIC?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_RADIOCUSTOM,style==START_BUTTON_CUSTOM?BST_CHECKED:BST_UNCHECKED);

	{
		CSettingsLockWrite lock;
		CSetting *pSetting=FindSetting(L"StartButtonType");
		pSetting->value=CComVariant(style);
		pSetting->flags&=~CSetting::FLAG_DEFAULT;
		SetSettingsDirty();
	}
	Update(false);
	return 0;
}

LRESULT CMenuStyleDlg::OnPick( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	CString str=GetSettingString(L"StartButtonPath");
	wchar_t path[_MAX_PATH];
	if (_wcsicmp(PathFindExtension(str),L".bmp")==0 || _wcsicmp(PathFindExtension(str),L".png")==0)
	{
		Strcpy(path,_countof(path),str);
		DoEnvironmentSubst(path,_countof(path));
	}
	else
		path[0]=0;
	if (BrowseForBitmap(m_hWnd,path,false))
	{
		{
			CSettingsLockWrite lock;
			CSetting *pSetting=FindSetting(L"StartButtonPath");
			pSetting->value=CComVariant(path);
			pSetting->flags&=~CSetting::FLAG_DEFAULT;
			SetSettingsDirty();
		}
		Update(true);
	}
	return 0;
}

LRESULT CMenuStyleDlg::OnAdvanced( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	SelectSettingsTab(IDS_START_BUTTON,true,NULL);
	return 0;
}

LRESULT CMenuStyleDlg::OnSkin( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	SelectSettingsTab(IDS_SKIN_SETTINGS,false,NULL);
	return 0;
}

static HICON CreateIconFromButton( HBITMAP bitmap, int frames )
{
	BITMAP bmpInfo;
	GetObject(bitmap,sizeof(bmpInfo),&bmpInfo);
	int width=bmpInfo.bmWidth;
	int height=bmpInfo.bmHeight/frames;
	if (height<1) height=1;
	std::vector<char> buf((width+1)*height,-1);
	HBITMAP bmpMask=CreateBitmap(width,height,1,8,&buf[0]);

	HDC hSrc=CreateCompatibleDC(NULL);
	HDC hDst=CreateCompatibleDC(hSrc);

	BITMAPINFO bi={0};
	bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth=width;
	bi.bmiHeader.biHeight=height;
	bi.bmiHeader.biPlanes=1;
	bi.bmiHeader.biBitCount=32;
	HBITMAP bmpColor=CreateDIBSection(hDst,&bi,DIB_RGB_COLORS,NULL,NULL,0);

	HGDIOBJ bmp01=SelectObject(hSrc,bitmap);
	HGDIOBJ bmp02=SelectObject(hDst,bmpColor);
	BitBlt(hDst,0,0,width,height,hSrc,0,0,SRCCOPY);
	SelectObject(hSrc,bmp01);
	SelectObject(hDst,bmp02);
	DeleteDC(hSrc);
	DeleteDC(hDst);

	ICONINFO info={TRUE,0,0,bmpMask,bmpColor};
	HICON hIcon=CreateIconIndirect(&info);
	DeleteObject(bmpMask);
	DeleteObject(bmpColor);
	return hIcon;
}

void CMenuStyleDlg::UpdateIcon( bool bForce )
{
	wchar_t path[_MAX_PATH];
	Strcpy(path,_countof(path),GetSettingString(L"StartButtonPath"));
	DoEnvironmentSubst(path,_countof(path));
	if (m_IconPath==path && !bForce)
		return;
	m_IconPath=path;
	const int MAX_ICON_SIZE=40;
	SIZE size={-MAX_ICON_SIZE,0};
	std::vector<unsigned int> buttonAnim;
	int frames=3;
	HBITMAP bitmap=LoadImageFile(path,&size,true,false,&buttonAnim);
	if (bitmap)
	{
		if (!buttonAnim.empty())
		{
			frames=buttonAnim[2]&255;
			if (frames<1) frames=1;
		}
	}
	else
	{
		frames=1;
		HDC hdc=CreateCompatibleDC(NULL);
		BITMAPINFO bi={0};
		bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth=MAX_ICON_SIZE;
		bi.bmiHeader.biHeight=MAX_ICON_SIZE;
		bi.bmiHeader.biPlanes=1;
		bi.bmiHeader.biBitCount=32;
		bitmap=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,NULL,NULL,0);
		SelectObject(hdc,GetStockObject(BLACK_PEN));
		SelectObject(hdc,GetStockObject(WHITE_BRUSH));
		HGDIOBJ bmp02=SelectObject(hdc,bitmap);
		Rectangle(hdc,0,0,MAX_ICON_SIZE,MAX_ICON_SIZE);
		SelectObject(hdc,bmp02);
		DeleteDC(hdc);
	}
	HICON hIcon=CreateIconFromButton(bitmap,frames);
	DeleteObject(bitmap);
	SendDlgItemMessage(IDC_STATICCUSTOM,STM_SETICON,(WPARAM)hIcon,0);
	if (m_hIcon) DestroyIcon(m_hIcon);
	m_hIcon=hIcon;
}

void CMenuStyleDlg::Update( bool bForce )
{
	int type=GetSettingInt(L"MenuStyle");
	CheckDlgButton(IDC_RADIO_CLASSIC,type==MENU_CLASSIC1?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_RADIO_TWO_COLUMNS,type==MENU_CLASSIC2?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_RADIO_WIN7,type==MENU_WIN7?BST_CHECKED:BST_UNCHECKED);
	GetDlgItem(IDC_SKIN_CLASSIC).ShowWindow(type==MENU_CLASSIC1?SW_SHOW:SW_HIDE);
	GetDlgItem(IDC_SKIN_CLASSIC2).ShowWindow(type==MENU_CLASSIC2?SW_SHOW:SW_HIDE);
	GetDlgItem(IDC_SKIN_WIN7).ShowWindow(type==MENU_WIN7?SW_SHOW:SW_HIDE);

	BOOL bEnabled=!IsSettingLocked(L"MenuStyle");
	GetDlgItem(IDC_RADIO_CLASSIC).EnableWindow(bEnabled);
	GetDlgItem(IDC_RADIO_TWO_COLUMNS).EnableWindow(bEnabled);
	GetDlgItem(IDC_RADIO_WIN7).EnableWindow(bEnabled);

	bEnabled=GetSettingBool(L"EnableStartButton");
	TStartButtonType buttonType=GetStartButtonType();
	CheckDlgButton(IDC_CHECKENABLED,bEnabled?BST_CHECKED:BST_UNCHECKED);
	GetDlgItem(IDC_CHECKENABLED).EnableWindow(!IsSettingLocked(L"EnableStartButton"));

	CheckDlgButton(IDC_RADIOAERO,buttonType==START_BUTTON_AERO?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_RADIOCLASSIC,buttonType==START_BUTTON_CLASSIC?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(IDC_RADIOCUSTOM,buttonType==START_BUTTON_CUSTOM?BST_CHECKED:BST_UNCHECKED);
	BOOL bEnabled2=bEnabled && !IsSettingLocked(L"StartButtonType");
	GetDlgItem(IDC_RADIOAERO).EnableWindow(bEnabled2);
	GetDlgItem(IDC_RADIOCLASSIC).EnableWindow(bEnabled2);
	GetDlgItem(IDC_RADIOCUSTOM).EnableWindow(bEnabled2);
	m_ButtonAero.EnableWindow(bEnabled2);
	m_ButtonClassic.EnableWindow(bEnabled2);
	m_ButtonCustom.EnableWindow(bEnabled2);

	bEnabled2=bEnabled && buttonType==START_BUTTON_CUSTOM && !IsSettingLocked(L"StartButtonPath");
	GetDlgItem(IDC_BUTTONPICK).EnableWindow(bEnabled2);
	UpdateIcon(bForce);
	UpdateButtons(bForce);
}

void CMenuStyleDlg::UpdateDefaults( void )
{
	const CSetting *pSetting=FindSetting(L"MenuStyle");
	Assert(pSetting->value.vt==VT_I4);
	TMenuStyle menuStyle=(TMenuStyle)pSetting->value.intVal;
	UpdateSetting(L"SearchBox",CComVariant(menuStyle!=MENU_CLASSIC1?SEARCHBOX_NORMAL:SEARCHBOX_TAB),false);
	UpdateSetting(L"RecentPrograms",CComVariant(menuStyle!=MENU_CLASSIC1?RECENT_PROGRAMS_FREQUENT:RECENT_PROGRAMS_RECENT),false);
	UpdateSetting(L"MaxRecentPrograms",CComVariant(menuStyle!=MENU_CLASSIC1?10:5),false);
	UpdateSetting(L"RecentProgsTop",CComVariant(menuStyle!=MENU_CLASSIC1?0:1),false);
	UpdateSetting(L"RecentProgKeys",CComVariant(menuStyle!=MENU_CLASSIC1?RECENT_KEYS_HIDDEN_DIGITS:RECENT_KEYS_DIGITS),false);
	UpdateSetting(L"PinnedPrograms",CComVariant(menuStyle!=MENU_WIN7?PINNED_PROGRAMS_FAST:PINNED_PROGRAMS_PINNED),false);
}

class CStyleSettingsPanel: public ISettingsPanel
{
public:
	virtual HWND Create( HWND parent );
	virtual HWND Activate( CSetting *pGroup, const RECT &rect, bool bReset );
	virtual bool Validate( HWND parent ) { return true; }

private:
	static CMenuStyleDlg s_Dialog;
};

CMenuStyleDlg CStyleSettingsPanel::s_Dialog;

HWND CStyleSettingsPanel::Create( HWND parent )
{
	if (!s_Dialog.m_hWnd)
		s_Dialog.Create(parent,LoadDialogEx(IDD_STYLESETTINGS));
	return s_Dialog.m_hWnd;
}

HWND CStyleSettingsPanel::Activate( CSetting *pGroup, const RECT &rect, bool bReset )
{
	s_Dialog.SetWindowPos(HWND_TOP,&rect,SWP_SHOWWINDOW);
	s_Dialog.Update(false);
	return s_Dialog.m_hWnd;
}

static CStyleSettingsPanel g_StyleSettingsPanel;

///////////////////////////////////////////////////////////////////////////////

CSetting g_Settings[]={
{L"MenuStyleGroup",CSetting::TYPE_GROUP,IDS_STYLE_SETTINGS,0,0,CSetting::FLAG_BASIC,NULL,NULL,&g_StyleSettingsPanel},
	{L"MenuStyle",CSetting::TYPE_INT,0,0,2,CSetting::FLAG_NORESET},
		{L"Classic1",CSetting::TYPE_RADIO},
		{L"Classic2",CSetting::TYPE_RADIO},
		{L"Win7",CSetting::TYPE_RADIO},

{L"Basic",CSetting::TYPE_GROUP,IDS_BASIC_SETTINGS},
	{L"EnableSettings",CSetting::TYPE_BOOL,0,0,1,CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"CrashDump",CSetting::TYPE_INT,0,0,0,CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"OldProgramsAge",CSetting::TYPE_INT,0,0,48,CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"FolderStartMenu",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"FolderCommonStartMenu",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"FolderPrograms",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"FolderCommonPrograms",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"CompatibilityFixes",CSetting::TYPE_INT,0,0,0,CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"AutoStartDelay",CSetting::TYPE_INT,0,0,0,CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	// needed for backwards compatibility
	{L"DefaultMenuStyle",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"MenuItems",CSetting::TYPE_MULTISTRING,0,0,L"",CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"Skin1",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"SkinOptions1",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"SkinVariation1",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"Skin2",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"SkinOptions2",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"SkinVariation2",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},

{L"Controls",CSetting::TYPE_GROUP,IDS_CONTROLS_SETTINGS},
	{L"MouseClick",CSetting::TYPE_INT,IDS_LCLICK,IDS_LCLICK_TIP,1,CSetting::FLAG_BASIC},
		{L"Nothing",CSetting::TYPE_RADIO,IDS_OPEN_NOTHING,IDS_OPEN_NOTHING_TIP},
		{L"ClassicMenu",CSetting::TYPE_RADIO,IDS_OPEN_CSM,IDS_OPEN_CSM_TIP},
		{L"WindowsMenu",CSetting::TYPE_RADIO,IDS_OPEN_WSM,IDS_OPEN_WSM_TIP},
		{L"Command",CSetting::TYPE_RADIO,IDS_OPEN_CMD,IDS_OPEN_CMD_TIP},
		{L"Both",CSetting::TYPE_RADIO,IDS_OPEN_BOTH,IDS_OPEN_BOTH_TIP,0,CSetting::FLAG_HIDDEN},
	{L"MouseClickCommand",CSetting::TYPE_STRING,IDS_OPEN_CMD_TEXT,IDS_OPEN_CMD_TEXT_TIP,"%userprofile%",0,L"MouseClick=3",L"Command"},
	{L"ShiftClick",CSetting::TYPE_INT,IDS_SHIFT_LCLICK,IDS_SHIFT_LCLICK_TIP,2,CSetting::FLAG_BASIC},
		{L"Nothing",CSetting::TYPE_RADIO,IDS_OPEN_NOTHING,IDS_OPEN_NOTHING_TIP},
		{L"ClassicMenu",CSetting::TYPE_RADIO,IDS_OPEN_CSM,IDS_OPEN_CSM_TIP},
		{L"WindowsMenu",CSetting::TYPE_RADIO,IDS_OPEN_WSM,IDS_OPEN_WSM_TIP},
		{L"Command",CSetting::TYPE_RADIO,IDS_OPEN_CMD,IDS_OPEN_CMD_TIP},
		{L"Both",CSetting::TYPE_RADIO,IDS_OPEN_BOTH,IDS_OPEN_BOTH_TIP,0,CSetting::FLAG_HIDDEN},
/*		{L"Desktop",CSetting::TYPE_RADIO,IDS_OPEN_DESKTOP,IDS_OPEN_DESKTOP_TIP,0,CSetting::FLAG_HIDDEN},
		{L"Cortana",CSetting::TYPE_RADIO,IDS_OPEN_CORTANA,IDS_OPEN_CORTANA_TIP},*/
	{L"ShiftClickCommand",CSetting::TYPE_STRING,IDS_OPEN_CMD_TEXT,IDS_OPEN_CMD_TEXT_TIP,"%systemdrive%",0,L"ShiftClick=3",L"Command"},
	{L"WinKey",CSetting::TYPE_INT,IDS_WIN_KEY,IDS_WIN_KEY_TIP,1,CSetting::FLAG_BASIC},
		{L"Nothing",CSetting::TYPE_RADIO,IDS_OPEN_NOTHING,IDS_OPEN_NOTHING_TIP},
		{L"ClassicMenu",CSetting::TYPE_RADIO,IDS_OPEN_CSM,IDS_OPEN_CSM_TIP},
		{L"WindowsMenu",CSetting::TYPE_RADIO,IDS_OPEN_WSM,IDS_OPEN_WSM_TIP},
		{L"Command",CSetting::TYPE_RADIO,IDS_OPEN_CMD,IDS_OPEN_CMD_TIP},
		{L"Both",CSetting::TYPE_RADIO,IDS_OPEN_BOTH,IDS_OPEN_BOTH_TIP},
		{L"Desktop",CSetting::TYPE_RADIO,IDS_OPEN_DESKTOP,IDS_OPEN_DESKTOP_TIP},
	{L"WinKeyCommand",CSetting::TYPE_STRING,IDS_OPEN_CMD_TEXT,IDS_OPEN_CMD_TEXT_TIP,"cmd",0,L"WinKey=3",L"Command"},
	{L"ShiftWin",CSetting::TYPE_INT,IDS_SHIFT_WIN,IDS_SHIFT_WIN_TIP,2,CSetting::FLAG_BASIC},
		{L"Nothing",CSetting::TYPE_RADIO,IDS_OPEN_NOTHING,IDS_OPEN_NOTHING_TIP},
		{L"ClassicMenu",CSetting::TYPE_RADIO,IDS_OPEN_CSM,IDS_OPEN_CSM_TIP},
		{L"WindowsMenu",CSetting::TYPE_RADIO,IDS_OPEN_WSM,IDS_OPEN_WSM_TIP},
		{L"Command",CSetting::TYPE_RADIO,IDS_OPEN_CMD,IDS_OPEN_CMD_TIP},
		{L"Both",CSetting::TYPE_RADIO,IDS_OPEN_BOTH,IDS_OPEN_BOTH_TIP},
/*		{L"Desktop",CSetting::TYPE_RADIO,IDS_OPEN_DESKTOP,IDS_OPEN_DESKTOP_TIP,0,CSetting::FLAG_HIDDEN},
		{L"Cortana",CSetting::TYPE_RADIO,IDS_OPEN_CORTANA,IDS_OPEN_CORTANA_TIP},*/
	{L"ShiftWinCommand",CSetting::TYPE_STRING,IDS_OPEN_CMD_TEXT,IDS_OPEN_CMD_TEXT_TIP,"powershell",0,L"ShiftWin=3",L"Command"},
	{L"MiddleClick",CSetting::TYPE_INT,IDS_MCLICK,IDS_MCLICK_TIP,0},
		{L"Nothing",CSetting::TYPE_RADIO,IDS_OPEN_NOTHING,IDS_OPEN_NOTHING_TIP},
		{L"ClassicMenu",CSetting::TYPE_RADIO,IDS_OPEN_CSM,IDS_OPEN_CSM_TIP},
		{L"WindowsMenu",CSetting::TYPE_RADIO,IDS_OPEN_WSM,IDS_OPEN_WSM_TIP},
		{L"Command",CSetting::TYPE_RADIO,IDS_OPEN_CMD,IDS_OPEN_CMD_TIP},
/*		{L"Both",CSetting::TYPE_RADIO,IDS_OPEN_BOTH,IDS_OPEN_BOTH_TIP,0,CSetting::FLAG_HIDDEN},
		{L"Desktop",CSetting::TYPE_RADIO,IDS_OPEN_DESKTOP,IDS_OPEN_DESKTOP_TIP,0,CSetting::FLAG_HIDDEN},
		{L"Cortana",CSetting::TYPE_RADIO,IDS_OPEN_CORTANA,IDS_OPEN_CORTANA_TIP},*/
	{L"MiddleClickCommand",CSetting::TYPE_STRING,IDS_OPEN_CMD_TEXT,IDS_OPEN_CMD_TEXT_TIP,"taskmgr",0,L"MiddleClick=3",L"Command"},
	{L"Hover",CSetting::TYPE_INT,IDS_HOVER,IDS_HOVER_TIP,0},
		{L"Nothing",CSetting::TYPE_RADIO,IDS_OPEN_NOTHING,IDS_OPEN_NOTHING_TIP},
		{L"ClassicMenu",CSetting::TYPE_RADIO,IDS_OPEN_CSM,IDS_OPEN_CSM_TIP},
		{L"WindowsMenu",CSetting::TYPE_RADIO,IDS_OPEN_WSM,IDS_OPEN_WSM_TIP},
		{L"Command",CSetting::TYPE_RADIO,IDS_OPEN_CMD,IDS_OPEN_CMD_TIP},
	{L"HoverCommand",CSetting::TYPE_STRING,IDS_OPEN_CMD_TEXT,IDS_OPEN_CMD_TEXT_TIP,"",0,L"Hover=3",L"Command"},
	{L"StartHoverDelay",CSetting::TYPE_INT,IDS_HOVER_DELAY,IDS_HOVER_DELAY_TIP,1000,0,L"Hover",L"Hover"},
	{L"ShiftRight",CSetting::TYPE_BOOL,IDS_RIGHT_SHIFT,IDS_RIGHT_SHIFT_TIP,0},
	{L"CSMHotkey",CSetting::TYPE_HOTKEY,IDS_CSM_HOTKEY,IDS_CSM_HOTKEY_TIP,0},
	{L"WSMHotkey",CSetting::TYPE_HOTKEY,IDS_WSM_HOTKEY,IDS_WSM_HOTKEY_TIP,0},

{L"SpecialItems",CSetting::TYPE_GROUP,IDS_SHOW_ITEMS,0,0,CSetting::FLAG_MENU_CLASSIC_BOTH},
	{L"Computer",CSetting::TYPE_INT,IDS_SHOW_COMPUTER,IDS_SHOW_COMPUTER_TIP,1,CSetting::FLAG_BASIC|CSetting::FLAG_MENU_CLASSIC_BOTH},
		{L"Hide",CSetting::TYPE_RADIO,IDS_ITEM_HIDE,IDS_ITEM_HIDE_TIP},
		{L"Show",CSetting::TYPE_RADIO,IDS_ITEM_SHOW,IDS_ITEM_SHOW_TIP},
		{L"Menu",CSetting::TYPE_RADIO,IDS_ITEM_MENU,IDS_ITEM_MENU_TIP},
		{L"Drives",CSetting::TYPE_RADIO,IDS_ITEM_DRIVES,IDS_ITEM_DRIVES_TIP},
	{L"Favorites",CSetting::TYPE_INT,IDS_SHOW_FAVORITES,IDS_SHOW_FAVORITES_TIP,0,CSetting::FLAG_BASIC|CSetting::FLAG_MENU_CLASSIC_BOTH},
		{L"Hide",CSetting::TYPE_RADIO,IDS_ITEM_HIDE,IDS_ITEM_HIDE_TIP},
		{L"Show",CSetting::TYPE_RADIO,IDS_ITEM_SHOW,IDS_ITEM_SHOW_TIP},
		{L"Menu",CSetting::TYPE_RADIO,IDS_ITEM_MENU,IDS_ITEM_MENU_TIP},
	{L"Documents",CSetting::TYPE_INT,IDS_SHOW_DOCUMENTS,IDS_SHOW_DOCUMENTS_TIP,2,CSetting::FLAG_BASIC|CSetting::FLAG_MENU_CLASSIC_BOTH},
		{L"Hide",CSetting::TYPE_RADIO,IDS_ITEM_HIDE,IDS_ITEM_HIDE_TIP},
		{L"Show",CSetting::TYPE_RADIO,IDS_ITEM_SHOW,IDS_ITEM_SHOW_TIP},
		{L"Menu",CSetting::TYPE_RADIO,IDS_ITEM_MENU,IDS_ITEM_MENU_TIP},
	{L"MaxRecentDocuments",CSetting::TYPE_INT,IDS_MAX_DOCS,IDS_MAX_DOCS_TIP,15,CSetting::FLAG_MENU_CLASSIC_BOTH,L"Documents=2",L"Documents"},
	{L"SortRecentDocuments",CSetting::TYPE_INT,IDS_SORT_DOCS,IDS_SORT_DOCS_TIP,0,CSetting::FLAG_MENU_CLASSIC_BOTH,L"Documents=2",L"Documents"},
		{L"Name",CSetting::TYPE_RADIO,IDS_SORT_NAME,IDS_SORT_NAME_TIP},
		{L"Extension",CSetting::TYPE_RADIO,IDS_SORT_EXT,IDS_SORT_EXT_TIP},
		{L"Date",CSetting::TYPE_RADIO,IDS_SORT_DATE,IDS_SORT_DATE_TIP},
	{L"UserFiles",CSetting::TYPE_INT,IDS_SHOW_USERFILES,IDS_SHOW_USERFILES_TIP,1,CSetting::FLAG_MENU_CLASSIC_BOTH},
		{L"Hide",CSetting::TYPE_RADIO,IDS_ITEM_HIDE,IDS_ITEM_HIDE_TIP},
		{L"Show",CSetting::TYPE_RADIO,IDS_ITEM_SHOW,IDS_ITEM_SHOW_TIP},
		{L"Menu",CSetting::TYPE_RADIO,IDS_ITEM_MENU,IDS_ITEM_MENU_TIP},
	{L"UserDocuments",CSetting::TYPE_INT,IDS_SHOW_USERDOCS,IDS_SHOW_USERDOCS_TIP,1,CSetting::FLAG_MENU_CLASSIC_BOTH},
		{L"Hide",CSetting::TYPE_RADIO,IDS_ITEM_HIDE,IDS_ITEM_HIDE_TIP},
		{L"Show",CSetting::TYPE_RADIO,IDS_ITEM_SHOW,IDS_ITEM_SHOW_TIP},
		{L"Menu",CSetting::TYPE_RADIO,IDS_ITEM_MENU,IDS_ITEM_MENU_TIP},
	{L"UserPictures",CSetting::TYPE_INT,IDS_SHOW_USERPICS,IDS_SHOW_USERPICS_TIP,1,CSetting::FLAG_MENU_CLASSIC_BOTH},
		{L"Hide",CSetting::TYPE_RADIO,IDS_ITEM_HIDE,IDS_ITEM_HIDE_TIP},
		{L"Show",CSetting::TYPE_RADIO,IDS_ITEM_SHOW,IDS_ITEM_SHOW_TIP},
		{L"Menu",CSetting::TYPE_RADIO,IDS_ITEM_MENU,IDS_ITEM_MENU_TIP},
	{L"ControlPanel",CSetting::TYPE_INT,IDS_SHOW_CP,IDS_SHOW_CP_TIP,2,CSetting::FLAG_BASIC|CSetting::FLAG_MENU_CLASSIC_BOTH},
		{L"Hide",CSetting::TYPE_RADIO,IDS_ITEM_HIDE,IDS_ITEM_HIDE_TIP},
		{L"Show",CSetting::TYPE_RADIO,IDS_ITEM_SHOW,IDS_ITEM_SHOW_TIP},
		{L"Menu",CSetting::TYPE_RADIO,IDS_ITEM_MENU,IDS_ITEM_MENU_TIP},
	{L"ControlPanelCategories",CSetting::TYPE_BOOL,IDS_CP_CATEGORIES,IDS_CP_CATEGORIES_TIP,0,CSetting::FLAG_MENU_CLASSIC_BOTH,L"#ControlPanel",L"ControlPanel"},
	{L"Network",CSetting::TYPE_INT,IDS_SHOW_NETWORK,IDS_SHOW_NETWORK_TIP,1,CSetting::FLAG_MENU_CLASSIC_BOTH},
		{L"Hide",CSetting::TYPE_RADIO,IDS_ITEM_HIDE,IDS_ITEM_HIDE_TIP},
		{L"Show",CSetting::TYPE_RADIO,IDS_ITEM_SHOW,IDS_ITEM_SHOW_TIP},
		{L"Menu",CSetting::TYPE_RADIO,IDS_ITEM_MENU,IDS_ITEM_MENU_TIP},
	{L"Printers",CSetting::TYPE_INT,IDS_SHOW_PRINTERS,IDS_SHOW_PRINTERS_TIP,1,CSetting::FLAG_MENU_CLASSIC_BOTH},
		{L"Hide",CSetting::TYPE_RADIO,IDS_ITEM_HIDE,IDS_ITEM_HIDE_TIP},
		{L"Show",CSetting::TYPE_RADIO,IDS_ITEM_SHOW,IDS_ITEM_SHOW_TIP},
		{L"Menu",CSetting::TYPE_RADIO,IDS_ITEM_MENU,IDS_ITEM_MENU_TIP},
	{L"Shutdown",CSetting::TYPE_INT,IDS_SHOW_SHUTDOWN,IDS_SHOW_SHUTDOWN_TIP,2,CSetting::FLAG_MENU_CLASSIC_BOTH},
		{L"Hide",CSetting::TYPE_RADIO,IDS_ITEM_HIDE,IDS_ITEM_HIDE_TIP},
		{L"Show",CSetting::TYPE_RADIO,IDS_ITEM_SHOW,IDS_ITEM_SHOW_TIP},
		{L"Menu",CSetting::TYPE_RADIO,IDS_ITEM_MENU,IDS_ITEM_MENU_TIP},
	{L"LogOff",CSetting::TYPE_BOOL,IDS_SHOW_LOGOFF,IDS_SHOW_LOGOFF_TIP,1,CSetting::FLAG_MENU_CLASSIC_BOTH|CSetting::FLAG_BASIC},
	{L"ConfirmLogOff",CSetting::TYPE_BOOL,IDS_CONFIRM_LOGOFF,IDS_CONFIRM_LOGOFF_TIP,0,CSetting::FLAG_MENU_CLASSIC_BOTH,L"#LogOff",L"LogOff"},
	{L"Undock",CSetting::TYPE_BOOL,IDS_SHOW_UNDOCK,IDS_SHOW_UNDOCK_TIP,1,CSetting::FLAG_MENU_CLASSIC_BOTH},
	{L"Search",CSetting::TYPE_BOOL,IDS_SHOW_SEARCH,IDS_SHOW_SEARCH_TIP,1,CSetting::FLAG_MENU_CLASSIC_BOTH},
	{L"ShowAppsMenu",CSetting::TYPE_BOOL,IDS_SHOW_APPS_MENU,IDS_SHOW_APPS_MENU_TIP,1,CSetting::FLAG_MENU_CLASSIC_BOTH},
	{L"Help",CSetting::TYPE_BOOL,IDS_SHOW_HELP,IDS_SHOW_HELP_TIP,1,CSetting::FLAG_MENU_CLASSIC_BOTH},
	{L"Run",CSetting::TYPE_BOOL,IDS_SHOW_RUN,IDS_SHOW_RUN_TIP,1,CSetting::FLAG_MENU_CLASSIC_BOTH},

{L"MainMenu",CSetting::TYPE_GROUP,IDS_MAIN_MENU_SETTINGS},
	{L"ProgramsStyle",CSetting::TYPE_INT,IDS_INLINE_PROGS,IDS_INLINE_PROGS_TIP,1,CSetting::FLAG_MENU_WIN7|CSetting::FLAG_BASIC},
		{L"Cascade",CSetting::TYPE_RADIO,IDS_MAIN_CASCADE,IDS_MAIN_CASCADE_TIP},
		{L"Inline",CSetting::TYPE_RADIO,IDS_MAIN_INLINE,IDS_MAIN_INLINE_TIP},
		{L"Hidden",CSetting::TYPE_RADIO,IDS_MAIN_HIDDEN,IDS_MAIN_HIDDEN_TIP},
	{L"AllProgramsMetro",CSetting::TYPE_BOOL,IDS_SHOW_APPS,IDS_SHOW_APPS_TIP,1,CSetting::FLAG_MENU_WIN7,NULL,L"Inline"},
	{L"FoldersFirst",CSetting::TYPE_BOOL,IDS_FOLDERS_FIRST,IDS_FOLDERS_FIRST_TIP,0,CSetting::FLAG_MENU_WIN7,L"ProgramsStyle=1",L"Inline"},
	{L"OpenPrograms",CSetting::TYPE_BOOL,IDS_OPEN_PROGRAMS,IDS_OPEN_PROGRAMS_TIP,0,CSetting::FLAG_MENU_WIN7,L"ProgramsStyle=1",L"Inline"},
	{L"ProgramsMenuDelay",CSetting::TYPE_INT,IDS_PROGRAMS_DELAY,IDS_PROGRAMS_DELAY_TIP,200,CSetting::FLAG_MENU_WIN7,L"ProgramsStyle=1",L"Inline"}, // 2x system delay time
	{L"HideProgramsMetro",CSetting::TYPE_BOOL,IDS_HIDE_APPS,IDS_HIDE_APPS_TIP,1},
	{L"PinnedPrograms",CSetting::TYPE_INT,IDS_PINNED_PROGRAMS,IDS_PINNED_PROGRAMS_TIP,PINNED_PROGRAMS_PINNED},
		{L"FastItems",CSetting::TYPE_RADIO,IDS_FAST_ITEMS,IDS_FAST_ITEMS_TIP},
		{L"PinnedItems",CSetting::TYPE_RADIO,IDS_PINNED_ITEMS,IDS_PINNED_ITEMS_TIP},
	{L"PinnedItemsPath",CSetting::TYPE_DIRECTORY,IDS_PINNED_PATH,IDS_PINNED_PATH_TIP,L"%APPDATA%\\OpenShell\\Pinned",0,L"PinnedPrograms=1",L"PinnedItems"},
	{L"RecentPrograms",CSetting::TYPE_INT,IDS_RECENT_PROGRAMS,IDS_RECENT_PROGRAMS_TIP,RECENT_PROGRAMS_RECENT,CSetting::FLAG_BASIC},
		{L"None",CSetting::TYPE_RADIO,IDS_NO_RECENT,IDS_NO_RECENT_TIP},
		{L"Recent",CSetting::TYPE_RADIO,IDS_SHOW_RECENT,IDS_SHOW_RECENT_TIP},
		{L"Frequent",CSetting::TYPE_RADIO,IDS_SHOW_FREQUENT,IDS_SHOW_FREQUENT_TIP},
	{L"RecentMetroApps",CSetting::TYPE_BOOL,IDS_RECENT_METRO,IDS_RECENT_METRO_TIP,1,0,L"RecentPrograms",L"RecentPrograms"},
	{L"MaxRecentPrograms",CSetting::TYPE_INT,IDS_MAX_PROGS,IDS_MAX_PROGS_TIP,5,0,L"RecentPrograms",L"RecentPrograms"},
	{L"RecentProgsTop",CSetting::TYPE_BOOL,IDS_RECENT_TOP,IDS_RECENT_TOP_TIP,1,0,L"RecentPrograms",L"RecentPrograms"},
	{L"RecentSortName",CSetting::TYPE_BOOL,IDS_RECENT_NAME,IDS_RECENT_NAME_TIP,0,0,L"RecentPrograms",L"RecentPrograms"},
	{L"RecentProgKeys",CSetting::TYPE_INT,IDS_RECENT_KEYS,IDS_RECENT_KEYS_TIP,RECENT_KEYS_DIGITS,0,L"RecentPrograms",L"RecentPrograms"},
		{L"NoKey",CSetting::TYPE_RADIO,IDS_KEY_NOTHING,IDS_KEY_NOTHING_TIP,0,0,L"RecentPrograms"},
		{L"Normal",CSetting::TYPE_RADIO,IDS_KEY_NORMAL,IDS_KEY_NORMAL_TIP,0,0,L"RecentPrograms"},
		{L"Digits",CSetting::TYPE_RADIO,IDS_KEY_DIGITS,IDS_KEY_DIGITS_TIP,0,0,L"RecentPrograms"},
		{L"HiddenDigits",CSetting::TYPE_RADIO,IDS_KEY_HIDDEN,IDS_KEY_HIDDEN_TIP,0,0,L"RecentPrograms"},
	{L"EnableJumplists",CSetting::TYPE_BOOL,IDS_JUMPLISTS,IDS_JUMPLISTS_TIP,1},
	{L"MaxJumplists",CSetting::TYPE_INT,IDS_MAX_JUMPLISTS,IDS_MAX_JUMPLISTS_TIP,10,0,L"EnableJumplists",L"EnableJumplists"},
	{L"JumplistKeys",CSetting::TYPE_INT,IDS_JUMPLIST_KEYS,IDS_JUMPLISTS_KEY_TIP,0,0,L"EnableJumplists",L"EnableJumplists"},
		{L"Select",CSetting::TYPE_RADIO,IDS_KEY_SELECT,IDS_KEY_SELECT_TIP,0,0,L"EnableJumplists"},
		{L"Run",CSetting::TYPE_RADIO,IDS_KEY_RUN,IDS_KEY_RUN_TIP,0,0,L"EnableJumplists"},
		{L"Open",CSetting::TYPE_RADIO,IDS_KEY_OPEN,IDS_KEY_OPEN_TIP,0,0,L"EnableJumplists"},
	{L"ShutdownCommand",CSetting::TYPE_INT,IDS_SHUTDOWN_COMMAND,IDS_SHUTDOWN_COMMAND_TIP,1,CSetting::FLAG_MENU_WIN7|CSetting::FLAG_BASIC},
		{L"CommandNone",CSetting::TYPE_RADIO,IDS_SHUTDOWN_NONE,IDS_SHUTDOWN_NONE_TIP},
		{L"CommandShutdown",CSetting::TYPE_RADIO,IDS_SHUTDOWN_SHUTDOWN,IDS_SHUTDOWN_SHUTDOWN_TIP},
		{L"CommandRestart",CSetting::TYPE_RADIO,IDS_SHUTDOWN_RESTART,IDS_SHUTDOWN_RESTART_TIP},
		{L"CommandLogOff",CSetting::TYPE_RADIO,IDS_SHUTDOWN_LOGOFF,IDS_SHUTDOWN_LOGOFF_TIP},
		{L"CommandSleep",CSetting::TYPE_RADIO,IDS_SHUTDOWN_SLEEP,IDS_SHUTDOWN_SLEEP_TIP},
		{L"CommandHibernate",CSetting::TYPE_RADIO,IDS_SHUTDOWN_HIBERNATE,IDS_SHUTDOWN_HIBERNATE_TIP},
		{L"CommandLock",CSetting::TYPE_RADIO,IDS_SHUTDOWN_LOCK,IDS_SHUTDOWN_LOCK_TIP},
		{L"CommandSwitch",CSetting::TYPE_RADIO,IDS_SHUTDOWN_SWITCH,IDS_SHUTDOWN_SWITCH_TIP},
		{L"CommandShutdownBox",CSetting::TYPE_RADIO,IDS_SHUTDOWN_SHUTDOWN_BOX,IDS_SHUTDOWN_SHUTDOWN_BOX_TIP},
	{L"ShutdownW7",CSetting::TYPE_STRING,IDS_SHUTDOWN7,IDS_SHUTDOWN7_TIP,L"",CSetting::FLAG_MENU_WIN7,L"ShutdownCommand",L"ShutdownCommand"},
	{L"RemoteShutdown",CSetting::TYPE_BOOL,IDS_SHOW_RSHUTDOWN,IDS_SHOW_RSHUTDOWN_TIP,0,0,L"ShutdownCommand",L"ShutdownCommand"},
	{L"HybridShutdown",CSetting::TYPE_BOOL,IDS_HYBRID_SHUTDOWN,IDS_HYBRID_SHUTDOWN_TIP,1,0,L"ShutdownCommand",L"ShutdownCommand"},
	{L"ConfirmLogOff2",CSetting::TYPE_BOOL,IDS_CONFIRM_LOGOFF,IDS_CONFIRM_LOGOFF_TIP,0,CSetting::FLAG_MENU_WIN7,NULL,L"ShutdownCommand"},
	{L"SelectLastShutdown",CSetting::TYPE_BOOL,IDS_SELECT_LAST,IDS_SELECT_LAST_TIP,0,CSetting::FLAG_MENU_WIN7,NULL,L"ShutdownCommand"},
	{L"ProgramsWidth",CSetting::TYPE_INT,IDS_PROG_WIDTH,IDS_PROG_WIDTH_TIP,40,CSetting::FLAG_MENU_WIN7},
	{L"JumplistWidth",CSetting::TYPE_INT,IDS_JUMP_WIDTH,IDS_JUMP_WIDTH_TIP,40,CSetting::FLAG_MENU_WIN7},
	{L"MinMainHeight",CSetting::TYPE_INT,IDS_MIN_HEIGHT,IDS_MIN_HEIGHT_TIP,20,CSetting::FLAG_MENU_WIN7},
	{L"MaxRecentDocuments2",CSetting::TYPE_INT,IDS_MAX_DOCS,IDS_MAX_DOCS_TIP,15,CSetting::FLAG_MENU_WIN7},
	{L"SortRecentDocuments2",CSetting::TYPE_INT,IDS_SORT_DOCS,IDS_SORT_DOCS_TIP,0,CSetting::FLAG_MENU_WIN7},
		{L"Name",CSetting::TYPE_RADIO,IDS_SORT_NAME,IDS_SORT_NAME_TIP},
		{L"Extension",CSetting::TYPE_RADIO,IDS_SORT_EXT,IDS_SORT_EXT_TIP},
		{L"Date",CSetting::TYPE_RADIO,IDS_SORT_DATE,IDS_SORT_DATE_TIP},
	{L"StartScreenShortcut",CSetting::TYPE_BOOL,IDS_STARTSCREEN_ITEM,IDS_STARTSCREEN_ITEM_TIP,1},
	{L"ControlPanelCategories2",CSetting::TYPE_BOOL,IDS_CP_CATEGORIES2,IDS_CP_CATEGORIES_TIP,0,CSetting::FLAG_MENU_WIN7},
	{L"MainSortZA",CSetting::TYPE_BOOL,IDS_MAIN_SORTZA,IDS_SORTZA_TIP,0},
	{L"MainSortOnce",CSetting::TYPE_BOOL,IDS_MAIN_SORTONCE,IDS_SORTONCE_TIP,0},

{L"GeneralBehavior",CSetting::TYPE_GROUP,IDS_BEHAVIOR_SETTINGS},
	{L"AutoStart",CSetting::TYPE_BOOL,IDS_AUTOSTART,IDS_AUTOSTART_TIP,1,CSetting::FLAG_BASIC},
	{L"HighlightNew",CSetting::TYPE_BOOL,IDS_HIGHLIGHT_NEW,IDS_HIGHLIGHT_NEW_TIP,1},
		{L"HighlightNewApps",CSetting::TYPE_BOOL,IDS_HIGHLIGHT_NEWAPPS,IDS_HIGHLIGHT_NEWAPPS_TIP,1,0,L"#HighlightNew",L"HighlightNew"},
	{L"CheckWinUpdates",CSetting::TYPE_BOOL,IDS_CHECK_UPDATES,IDS_CHECK_UPDATES_TIP,1},
	{L"MenuDelay",CSetting::TYPE_INT,IDS_MENU_DELAY,IDS_MENU_DELAY_TIP,-1}, // system delay time
	{L"SplitMenuDelay",CSetting::TYPE_INT,IDS_SPLIT_DELAY,IDS_SPLIT_DELAY_TIP,200}, // 2x system delay time
	{L"InfotipDelay",CSetting::TYPE_STRING,IDS_TIP_DELAY,IDS_TIP_DELAY_TIP,L"400,4000"},
	{L"FolderInfotipDelay",CSetting::TYPE_STRING,IDS_FTIP_DELAY,IDS_FTIP_DELAY_TIP,L"0,0"},
	{L"EnableDragDrop",CSetting::TYPE_BOOL,IDS_DRAG_DROP,IDS_DRAG_DROP_TIP,1},
	{L"DragHideDelay",CSetting::TYPE_INT,IDS_DRAG_DELAY,IDS_DRAG_DELAY_TIP,4000},
	{L"ScrollType",CSetting::TYPE_INT,IDS_SCROLL_TYPE,IDS_SCROLL_TYPE_TIP,1},
		{L"Scroll",CSetting::TYPE_RADIO,IDS_SCROLL_YES,IDS_SCROLL_YES_TIP},
		{L"NoScroll",CSetting::TYPE_RADIO,IDS_SCROLL_NO,IDS_SCROLL_NO_TIP},
		{L"Auto",CSetting::TYPE_RADIO,IDS_SCROLL_AUTO,IDS_SCROLL_AUTO_TIP},
	{L"SameSizeColumns",CSetting::TYPE_BOOL,IDS_SAME_COLUMNS,IDS_SAME_COLUMNS_TIP,1,0,L"ScrollType",L"ScrollType"},
	{L"UserPicturePath",CSetting::TYPE_BITMAP,IDS_PIC_PATH,IDS_PIC_PATH_TIP,L""},
	{L"UserPictureCommand",CSetting::TYPE_STRING,IDS_PIC_COMMAND,IDS_PIC_COMMAND_TIP,L"control nusrmgr.cpl"},
	{L"UserNameCommand",CSetting::TYPE_STRING,IDS_NAME_COMMAND,IDS_NAME_COMMAND_TIP,L"control nusrmgr.cpl"},
	{L"SearchFilesCommand",CSetting::TYPE_STRING,IDS_SEARCH_COMMAND,IDS_SEARCH_COMMAND_TIP,L"search-ms:",CSetting::FLAG_MENU_CLASSIC_BOTH},
	{L"ExpandFolderLinks",CSetting::TYPE_BOOL,IDS_EXPAND_LINKS,IDS_EXPAND_LINKS_TIP,1},
	{L"SingleClickFolders",CSetting::TYPE_BOOL,IDS_NO_DBLCLICK,IDS_NO_DBLCLICK_TIP,0},
	{L"OpenTruePath",CSetting::TYPE_BOOL,IDS_OPEN_TRUE_PATH,IDS_OPEN_TRUE_PATH_TIP,1},
	{L"EnableTouch",CSetting::TYPE_BOOL,IDS_ENABLE_TOUCH,IDS_ENABLE_TOUCH_TIP,1},
	{L"EnableAccessibility",CSetting::TYPE_BOOL,IDS_ACCESSIBILITY,IDS_ACCESSIBILITY_TIP,1},
	{L"ShowNextToTaskbar",CSetting::TYPE_BOOL,IDS_NEXTTASKBAR,IDS_NEXTTASKBAR_TIP,1},
	{L"PreCacheIcons",CSetting::TYPE_BOOL,IDS_CACHE_ICONS,IDS_CACHE_ICONS_TIP,1,CSetting::FLAG_COLD},
	{L"DelayIcons",CSetting::TYPE_BOOL,IDS_DELAY_ICONS,IDS_DELAY_ICONS_TIP,1,CSetting::FLAG_COLD},
	{L"BoldSettings",CSetting::TYPE_BOOL,IDS_BOLD_SETTINGS,IDS_BOLD_SETTINGS_TIP,1},
	{L"ReportSkinErrors",CSetting::TYPE_BOOL,IDS_SKIN_ERRORS,IDS_SKIN_ERRORS_TIP,0},
	{L"EnableAccelerators",CSetting::TYPE_BOOL,IDS_ENABLE_ACCELERATORS,IDS_ENABLE_ACCELERATORS_TIP,1},
	{L"AltAccelerators",CSetting::TYPE_BOOL,IDS_ALT_ACCELERATORS,IDS_ALT_ACCELERATORS_TIP,0,0,L"EnableAccelerators",L"EnableAccelerators"},

{L"SearchBoxSettings",CSetting::TYPE_GROUP,IDS_SEARCH_BOX},
	{L"SearchBox",CSetting::TYPE_INT,IDS_SHOW_SEARCH_BOX,IDS_SHOW_SEARCH_BOX_TIP,SEARCHBOX_TAB,CSetting::FLAG_BASIC},
		{L"Hide",CSetting::TYPE_RADIO,IDS_SEARCH_BOX_HIDE,IDS_SEARCH_BOX_HIDE_TIP},
		{L"Normal",CSetting::TYPE_RADIO,IDS_SEARCH_BOX_SHOW,IDS_SEARCH_BOX_SHOW_TIP},
		{L"Tab",CSetting::TYPE_RADIO,IDS_SEARCH_BOX_TAB,IDS_SEARCH_BOX_TAB_TIP},
	{L"SearchSelect",CSetting::TYPE_BOOL,IDS_SEARCH_BOX_SEL,IDS_SEARCH_BOX_SEL_TIP,1,0,L"SearchBox=1",L"Normal"},
	{L"SearchHint",CSetting::TYPE_BOOL,IDS_SEARCH_HINT,IDS_SEARCH_HINT_TIP,0,0,L"SearchBox"},
		{L"SearchHintText",CSetting::TYPE_STRING,IDS_NEW_SEARCH_HINT,IDS_NEW_SEARCH_HINT_TIP,L"",0,L"#SearchHint",L"SearchHint"},
	{L"SearchTrack",CSetting::TYPE_BOOL,IDS_SEARCH_TRACK,IDS_SEARCH_TRACK_TIP,1,0,L"SearchBox"},
	{L"SearchResults",CSetting::TYPE_INT,IDS_SEARCH_MAX2,IDS_SEARCH_MAX_TIP2,5,CSetting::FLAG_MENU_CLASSIC_BOTH,L"SearchBox"},
	{L"SearchResultsMax",CSetting::TYPE_INT,IDS_SEARCH_MAX3,IDS_SEARCH_MAX_TIP3,20,CSetting::FLAG_MENU_CLASSIC_BOTH,L"SearchBox"},
	{L"SearchAutoComplete",CSetting::TYPE_BOOL,IDS_SEARCH_AUTO,IDS_SEARCH_AUTO_TIP,1,0,L"SearchBox"},
	{L"SearchPrograms",CSetting::TYPE_BOOL,IDS_SEARCH_PROGRAMS,IDS_SEARCH_PROGRAMS_TIP,1,0,L"SearchBox"},
		{L"SearchPath",CSetting::TYPE_BOOL,IDS_SEARCH_PATH,IDS_SEARCH_PATH_TIP,1,0,L"#SearchPrograms",L"SearchPrograms"},
		{L"SearchMetroApps",CSetting::TYPE_BOOL,IDS_SEARCH_METRO,IDS_SEARCH_METRO_TIP,1,0,L"#SearchPrograms",L"SearchPrograms"},
		{L"SearchMetroSettings",CSetting::TYPE_BOOL,IDS_SEARCH_METROS,IDS_SEARCH_METROS_TIP,1,0,L"#SearchPrograms",L"SearchPrograms"},
		{L"SearchKeywords",CSetting::TYPE_BOOL,IDS_SEARCH_KEYWORDS,IDS_SEARCH_KEYWORDS_TIP,1,0,L"#SearchPrograms",L"SearchPrograms"},
		{L"SearchSubWord",CSetting::TYPE_BOOL,IDS_SUB_WORD,IDS_SUB_WORD_TIP,1,0,L"#SearchPrograms",L"SearchPrograms"},
	{L"SearchFiles",CSetting::TYPE_BOOL,IDS_SEARCH_FILES,IDS_SEARCH_FILES_TIP,1,0,L"SearchBox"},
		{L"SearchContents",CSetting::TYPE_BOOL,IDS_SEARCH_CONTENTS,IDS_SEARCH_CONTENTS_TIP,1,0,L"#SearchFiles",L"SearchFiles"},
		{L"SearchCategories",CSetting::TYPE_BOOL,IDS_SEARCH_CATEGORIES,IDS_SEARCH_CATEGORIES_TIP,1,0,L"#SearchFiles",L"SearchFiles"},
	{L"SearchInternet",CSetting::TYPE_BOOL,IDS_SEARCH_INTERNET,IDS_SEARCH_INTERNET_TIP,1,0,L"SearchBox"},
	{L"MoreResults",CSetting::TYPE_BOOL,IDS_MORE_RESULTS,IDS_MORE_RESULTS_TIP,1,0,L"SearchBox"},

{L"Look",CSetting::TYPE_GROUP,IDS_LOOK_SETTINGS},
	{L"SmallIconSize",CSetting::TYPE_INT,IDS_SMALL_SIZE_SM,IDS_SMALL_SIZE_SM_TIP,-1,CSetting::FLAG_COLD}, // 16 for DPI<=96, 20 for DPI<=120, 24 otherwise
	{L"LargeIconSize",CSetting::TYPE_INT,IDS_LARGE_SIZE_SM,IDS_LARGE_SIZE_SM_TIP,-1,CSetting::FLAG_COLD}, // 32 for DPI<=96, 40 for DPI<=120, 48 otherwise
	{L"InvertMetroIcons",CSetting::TYPE_BOOL,IDS_INVERT_ICONS,IDS_INVERT_ICONS_TIP,0},
	{L"MaxMainMenuWidth",CSetting::TYPE_INT,IDS_MENU_WIDTH,IDS_MENU_WIDTH_TIP,60,CSetting::FLAG_MENU_CLASSIC_BOTH},
	{L"MaxMenuWidth",CSetting::TYPE_INT,IDS_SUBMENU_WIDTH,IDS_SUBMENU_WIDTH_TIP,60},
	{L"AlignToWorkArea",CSetting::TYPE_BOOL,IDS_ALIGN_WORK_AREA,IDS_ALIGN_WORK_AREA_TIP,0},
	{L"HorizontalMenuOffset",CSetting::TYPE_INT,IDS_HOR_OFFSET,IDS_HOR_OFFSET_TIP,0},
	{L"VerticalMenuOffset",CSetting::TYPE_INT,IDS_VERT_OFFSET,IDS_VERT_OFFSET_TIP,0 },
	{L"OverrideDPI",CSetting::TYPE_INT,IDS_DPI_OVERRIDE,IDS_DPI_OVERRIDE_TIP,0,CSetting::FLAG_COLD},
	{L"MainMenuAnimate",CSetting::TYPE_BOOL,IDS_ANIMATION7,IDS_ANIMATION7_TIP,1,CSetting::FLAG_MENU_WIN7},
	{L"MainMenuAnimation",CSetting::TYPE_INT,IDS_ANIMATION,IDS_ANIMATION_TIP,-1}, // system animation type
		{L"None",CSetting::TYPE_RADIO,IDS_ANIM_NONE,IDS_ANIM_NONE_TIP},
		{L"Fade",CSetting::TYPE_RADIO,IDS_ANIM_FADE,IDS_ANIM_FADE_TIP},
		{L"Slide",CSetting::TYPE_RADIO,IDS_ANIM_SLIDE,IDS_ANIM_SLIDE_TIP},
		{L"Random",CSetting::TYPE_RADIO,IDS_ANIM_RANDOM,IDS_ANIM_RANDOM_TIP},
	{L"MainMenuAnimationSpeed",CSetting::TYPE_INT,IDS_ANIM_SPEED,IDS_ANIM_SPEED_TIP,200,0,L"MainMenuAnimation"},
	{L"SubMenuAnimation",CSetting::TYPE_INT,IDS_SUB_ANIMATION,IDS_SUB_ANIMATION_TIP,-1}, // system animation type
		{L"None",CSetting::TYPE_RADIO,IDS_ANIM_NONE,IDS_ANIM_NONE_TIP},
		{L"Fade",CSetting::TYPE_RADIO,IDS_ANIM_FADE,IDS_ANIM_FADE_TIP},
		{L"Slide",CSetting::TYPE_RADIO,IDS_ANIM_SLIDE,IDS_ANIM_SLIDE_TIP},
		{L"Random",CSetting::TYPE_RADIO,IDS_ANIM_RANDOM,IDS_ANIM_RANDOM_TIP},
	{L"SubMenuAnimationSpeed",CSetting::TYPE_INT,IDS_SUB_ANIM_SPEED,IDS_SUB_ANIM_SPEED_TIP,200,0,L"SubMenuAnimation"},
	{L"SubMenuAnimationAlways",CSetting::TYPE_BOOL,IDS_SUB_ANIM_ALWAYS,IDS_SUB_ANIM_ALWAYS_TIP,0,0,L"SubMenuAnimation"},
	{L"MainMenuScrollSpeed",CSetting::TYPE_INT,IDS_SCROLL_SPEED,IDS_SCROLL_SPEED_TIP,3,CSetting::FLAG_MENU_CLASSIC_BOTH},
	{L"SubMenuScrollSpeed",CSetting::TYPE_INT,IDS_SUB_SCROLL_SPEED,IDS_SUB_SCROLL_SPEED_TIP,3},
	{L"MenuFadeSpeed",CSetting::TYPE_INT,IDS_FADE_SPEED,IDS_FADE_SPEED_TIP,400},
	{L"MenuCaption",CSetting::TYPE_STRING,IDS_MENU_CAPTION,IDS_MENU_CAPTION_TIP,L"",CSetting::FLAG_MENU_CLASSIC_BOTH},
	{L"MenuUsername",CSetting::TYPE_STRING,IDS_MENU_USERNAME,IDS_MENU_USERNAME_TIP,L"",CSetting::FLAG_MENU_CLASSIC_BOTH},
	{L"NumericSort",CSetting::TYPE_BOOL,IDS_NUMERIC_SORT,IDS_NUMERIC_SORT_TIP,1},
	{L"FontSmoothing",CSetting::TYPE_INT,IDS_FONT_SMOOTHING,IDS_FONT_SMOOTHING_TIP,0},
		{L"Default",CSetting::TYPE_RADIO,IDS_SMOOTH_DEFAULT,IDS_SMOOTH_DEFAULT_TIP},
		{L"None",CSetting::TYPE_RADIO,IDS_SMOOTH_NONE,IDS_SMOOTH_NONE_TIP},
		{L"Standard",CSetting::TYPE_RADIO,IDS_SMOOTH_STD,IDS_SMOOTH_STD_TIP},
		{L"ClearType",CSetting::TYPE_RADIO,IDS_SMOOTH_CLEAR,IDS_SMOOTH_CLEAR_TIP},
	{L"MenuShadow",CSetting::TYPE_BOOL,IDS_MENU_SHADOW,IDS_MENU_SHADOW_TIP,1},
	{L"AeroGlass",CSetting::TYPE_BOOL,IDS_MENU_GLASS,IDS_MENU_GLASS_TIP,0,CSetting::FLAG_CALLBACK},
	{L"EnableGlass",CSetting::TYPE_BOOL,IDS_MENU_GLASS2,IDS_MENU_GLASS2_TIP,1},
	{L"GlassOverride",CSetting::TYPE_BOOL,IDS_GLASS_OVERRIDE,IDS_GLASS_OVERRIDE_TIP,0},
	{L"GlassColor",CSetting::TYPE_COLOR,IDS_GLASS_COLOR,IDS_GLASS_COLOR_TIP,0,0,L"GlassOverride",L"GlassOverride"},
	{L"GlassIntensity",CSetting::TYPE_INT,IDS_GLASS_INTENSITY,IDS_GLASS_INTENSITY_TIP,0,0,L"GlassOverride",L"GlassOverride"},
	{L"GlassBlending",CSetting::TYPE_INT,IDS_GLASS_BLENDING,IDS_GLASS_BLENDING_TIP,0,0,L"GlassOverride",L"GlassOverride"},
	{L"GlassOpacity",CSetting::TYPE_INT,IDS_GLASS_OPACITY,IDS_GLASS_OPACITY_TIP,DEFAULT_GLASS_OPACITY,0,L"EnableGlass"},

{L"Skin",CSetting::TYPE_GROUP,IDS_SKIN_SETTINGS,0,0,CSetting::FLAG_BASIC|CSetting::FLAG_MENU_CLASSIC1,NULL,NULL,&g_SkinSettingsPanelClassic1},
	{L"SkinC1",CSetting::TYPE_STRING,0,0,L"Windows Aero"},
	{L"SkinVariationC1",CSetting::TYPE_STRING,0,0,L""},
	{L"SkinOptionsC1",CSetting::TYPE_MULTISTRING,0,0,L""},

{L"Skin",CSetting::TYPE_GROUP,IDS_SKIN_SETTINGS,0,0,CSetting::FLAG_BASIC|CSetting::FLAG_MENU_CLASSIC2,NULL,NULL,&g_SkinSettingsPanelClassic2},
	{L"SkinC2",CSetting::TYPE_STRING,0,0,L"Windows Aero"},
	{L"SkinVariationC2",CSetting::TYPE_STRING,0,0,L""},
	{L"SkinOptionsC2",CSetting::TYPE_MULTISTRING,0,0,L""},

{L"Skin7",CSetting::TYPE_GROUP,IDS_SKIN_SETTINGS,0,0,CSetting::FLAG_BASIC|CSetting::FLAG_MENU_WIN7,NULL,NULL,&g_SkinSettingsPanelWin7},
	{L"SkinW7",CSetting::TYPE_STRING,0,0,L"Windows Aero"},
	{L"SkinVariationW7",CSetting::TYPE_STRING,0,0,L""},
	{L"SkinOptionsW7",CSetting::TYPE_MULTISTRING,0,0,L""},

{L"StartButton",CSetting::TYPE_GROUP,IDS_START_BUTTON},
	{L"EnableStartButton",CSetting::TYPE_BOOL,IDS_ENABLE_BUTTON,IDS_ENABLE_BUTTON_TIP,1},
	{L"AllTaskbars",CSetting::TYPE_BOOL,IDS_ENABLE_TASKBARS,IDS_ENABLE_TASKBARS_TIP,1,0,L"EnableStartButton"},
	{L"StartButtonTip",CSetting::TYPE_STRING,IDS_BUTTON_TIP,IDS_BUTTON_TIP_TIP,L"$Menu.Start",0,L"EnableStartButton"},
	{L"StartButtonType",CSetting::TYPE_INT,IDS_BUTTON_TYPE,IDS_BUTTON_TYPE_TIP,0,0,L"EnableStartButton"},
		{L"AeroButton",CSetting::TYPE_RADIO,IDS_AERO_BUTTON,IDS_AERO_BUTTON_TIP},
		{L"ClasicButton",CSetting::TYPE_RADIO,IDS_CLASSIC_BUTTON,IDS_CLASSIC_BUTTON_TIP},
		{L"CustomButton",CSetting::TYPE_RADIO,IDS_CUSTOM_BUTTON,IDS_CUSTOM_BUTTON_TIP},
	{L"StartButtonPath",CSetting::TYPE_BITMAP,IDS_BUTTON_IMAGE,IDS_BUTTON_IMAGE_TIP,L"",0,L"#StartButtonType=2",L"CustomButton"},
	{L"StartButtonSize",CSetting::TYPE_INT,IDS_BUTTON_SIZE,IDS_BUTTON_SIZE_TIP2,0,0,L"#StartButtonType=2",L"CustomButton"},
	{L"StartButtonAlign",CSetting::TYPE_BOOL,IDS_BUTTON_ALIGN,IDS_BUTTON_ALIGN_TIP,0,0,L"#StartButtonType=2",L"CustomButton"},
	{L"StartButtonIcon",CSetting::TYPE_ICON,IDS_BUTTON_ICON,IDS_BUTTON_ICON_TIP,L",1",0,L"#StartButtonType=1",L"ClasicButton"},
	{L"StartButtonIconSize",CSetting::TYPE_INT,IDS_BUTTON_ICON_SIZE,IDS_BUTTON_ICON_SIZE_TIP,0,0,L"#StartButtonType=1",L"ClasicButton"},
	{L"StartButtonText",CSetting::TYPE_STRING,IDS_BUTTON_TEXT,IDS_BUTTON_TEXT_TIP,L"$Menu.Start",0,L"#StartButtonType=1",L"ClasicButton"},

{L"Taskbar",CSetting::TYPE_GROUP,IDS_TASKBAR_SETTINGS,0,0,CSetting::FLAG_BASIC},
	{L"CustomTaskbar",CSetting::TYPE_BOOL,IDS_TASK_CUSTOM,IDS_TASK_CUSTOM_TIP,0,CSetting::FLAG_CALLBACK},
	{L"TaskbarLook",CSetting::TYPE_INT,IDS_TASK_LOOK,IDS_TASK_LOOK_TIP,1,CSetting::FLAG_CALLBACK,L"CustomTaskbar",L"CustomTaskbar"},
		{L"Opaque",CSetting::TYPE_RADIO,IDS_TASK_OPAQUE,IDS_TASK_OPAQUE_TIP},
		{L"Transparent",CSetting::TYPE_RADIO,IDS_TASK_TRANS,IDS_TASK_TRANS_TIP},
		{L"Glass",CSetting::TYPE_RADIO,IDS_TASK_GLASS,IDS_TASK_GLASS_TIP},
		{L"AeroGlass",CSetting::TYPE_RADIO,IDS_TASK_AEROGLASS,IDS_TASK_AEROGLASS_TIP,0,CSetting::FLAG_HIDDEN},
	{L"TaskbarOpacity",CSetting::TYPE_INT,IDS_TASK_OPACITY,IDS_TASK_OPACITY_TIP,DEFAULT_TASK_OPACITY10,CSetting::FLAG_CALLBACK,L"TaskbarLook",L"CustomTaskbar"},
	{L"TaskbarColor",CSetting::TYPE_COLOR,IDS_TASK_COLOR,IDS_TASK_COLOR_TIP,0,CSetting::FLAG_CALLBACK,L"CustomTaskbar",L"CustomTaskbar"},
	{L"TaskbarTextColor",CSetting::TYPE_COLOR,IDS_TASK_TEXTCOLOR,IDS_TASK_TEXTCOLOR_TIP,0xFFFFFF,CSetting::FLAG_COLD|(1<<24),L"CustomTaskbar",L"CustomTaskbar"},
	{L"TaskbarTexture",CSetting::TYPE_BITMAP_JPG,IDS_TASK_TEXTURE,IDS_TASK_TEXTURE_TIP,L"",CSetting::FLAG_COLD,L"CustomTaskbar",L"CustomTaskbar"},
	{L"TaskbarTileH",CSetting::TYPE_INT,IDS_TASK_STRETCHH,IDS_TASK_STRETCHH_TIP,1,CSetting::FLAG_CALLBACK,L"#TaskbarTexture",L"TaskbarTexture"},
		{L"Tile",CSetting::TYPE_RADIO,IDS_TASK_TILE,IDS_TASK_TILE_TIP},
		{L"Stretch",CSetting::TYPE_RADIO,IDS_TASK_STRETCH,IDS_TASK_STRETCH_TIP},
		{L"TaskbarBordersH",CSetting::TYPE_STRING,IDS_TASK_BORDERS,IDS_TASK_BORDERS_TIP,L"0,0",CSetting::FLAG_CALLBACK,L"#TaskbarTileH=1",L"TaskbarTileH"},
	{L"TaskbarTileV",CSetting::TYPE_INT,IDS_TASK_STRETCHV,IDS_TASK_STRETCHV_TIP,1,CSetting::FLAG_CALLBACK,L"#TaskbarTexture",L"TaskbarTexture"},
		{L"Tile",CSetting::TYPE_RADIO,IDS_TASK_TILE,IDS_TASK_TILE_TIP},
		{L"Stretch",CSetting::TYPE_RADIO,IDS_TASK_STRETCH,IDS_TASK_STRETCH_TIP},
		{L"TaskbarBordersV",CSetting::TYPE_STRING,IDS_TASK_BORDERS,IDS_TASK_BORDERS_TIP,L"0,0",CSetting::FLAG_CALLBACK,L"#TaskbarTileV=1",L"TaskbarTileV"},

{L"Metro",CSetting::TYPE_GROUP,IDS_METRO_SETTINGS},
	{L"SkipMetro",CSetting::TYPE_BOOL,IDS_SKIP_METRO,IDS_SKIP_METRO_TIP,1,CSetting::FLAG_BASIC},
	{L"SkipMetroCount",CSetting::TYPE_INT,0,0,10,CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"DisableHotCorner",CSetting::TYPE_INT,IDS_HOT_CORNERS,IDS_HOT_CORNERS_TIP,1},
		{L"DisableNone",CSetting::TYPE_RADIO,IDS_DISABLE_NONE,IDS_DISABLE_NONE_TIP},
		{L"DisableStart",CSetting::TYPE_RADIO,IDS_DISABLE_START,IDS_DISABLE_START_TIP},
		{L"DisableAll",CSetting::TYPE_RADIO,IDS_DISABLE_ALL,IDS_DISABLE_ALL_TIP},
	{L"OpenMouseMonitor",CSetting::TYPE_BOOL,IDS_MOUSE_MONITOR,IDS_MOUSE_MONITOR_TIP,0},

{L"CustomMenu1",CSetting::TYPE_GROUP,IDS_CUSTOM_SETTINGS,0,0,CSetting::FLAG_MENU_CLASSIC1,NULL,NULL,&g_CustomMenuPanel1},
	{L"MenuItems1",CSetting::TYPE_MULTISTRING,0,0,L""},

{L"CustomMenu2",CSetting::TYPE_GROUP,IDS_CUSTOM_SETTINGS,0,0,CSetting::FLAG_MENU_CLASSIC2,NULL,NULL,&g_CustomMenuPanel2},
	{L"MenuItems2",CSetting::TYPE_MULTISTRING,0,0,L""},

{L"CustomMenu7",CSetting::TYPE_GROUP,IDS_CUSTOM_SETTINGS,0,0,CSetting::FLAG_BASIC|CSetting::FLAG_MENU_WIN7,NULL,NULL,&g_CustomMenuPanel7},
	{L"MenuItems7",CSetting::TYPE_MULTISTRING,0,0,L""},

{L"WindowsMenu",CSetting::TYPE_GROUP,IDS_WSM_SETTINGS},
	{L"CascadeAll",CSetting::TYPE_BOOL,IDS_CASCADE_ALL,IDS_CASCADE_ALL_TIP,0},
	{L"AllProgramsDelay",CSetting::TYPE_INT,IDS_ALL_DELAY,IDS_ALL_DELAY_TIP,-1,0,L"CascadeAll",L"CascadeAll"}, // system hover time
	{L"InitiallySelect",CSetting::TYPE_INT,IDS_ALL_SELECT,IDS_ALL_SELECT_TIP,0,0,L"CascadeAll",L"CascadeAll"},
		{L"SelectSearch",CSetting::TYPE_RADIO,IDS_SELECT_SEARCH,IDS_SELECT_SEARCH_TIP},
		{L"SelectButton",CSetting::TYPE_RADIO,IDS_SELECT_BUTTON,IDS_SELECT_BUTTON_TIP},
	{L"HideUserPic",CSetting::TYPE_BOOL,IDS_HIDE_PIC,IDS_HIDE_PIC_TIP,0},

{L"AllProgramsSkin",CSetting::TYPE_GROUP,IDS_ALL_SKIN_SETTINGS,0,0,0,NULL,NULL,&g_SkinSettingsPanelAll},
	{L"SkinA",CSetting::TYPE_STRING,0,0,L""},
	{L"SkinVariationA",CSetting::TYPE_STRING,0,0,L""},
	{L"SkinOptionsA",CSetting::TYPE_MULTISTRING,0,0,L""},

{L"ContextMenu",CSetting::TYPE_GROUP,IDS_CONTEXT_MENU_SETTINGS},
	{L"EnableContextMenu",CSetting::TYPE_BOOL,IDS_CONTEXT_MENU,IDS_CONTEXT_MENU_TIP,1},
	{L"CascadingMenu",CSetting::TYPE_BOOL,IDS_CASCADE_MENU,IDS_CASCADE_MENU_TIP,0,0,L"EnableContextMenu",L"EnableContextMenu"},
	{L"ShowNewFolder",CSetting::TYPE_BOOL,IDS_NEW_FOLDER,IDS_NEW_FOLDER_TIP,1,0,L"EnableContextMenu",L"EnableContextMenu"},
	{L"EnableExit",CSetting::TYPE_BOOL,IDS_EXIT,IDS_EXIT_TIP,1},
	{L"EnableExplorer",CSetting::TYPE_BOOL,IDS_EXPLORER,IDS_EXPLORER_TIP,1},
	{L"ExplorerPath",CSetting::TYPE_STRING,IDS_EXPLORER_PATH,IDS_EXPLORER_PATH_TIP,L"computer",0,L"EnableExplorer",L"EnableExplorer"},
	{L"DisablePinExt",CSetting::TYPE_BOOL,IDS_NOPIN,IDS_NOPIN_TIP,0},

{L"Sounds",CSetting::TYPE_GROUP,IDS_SOUND_SETTINGS},
	{L"SoundMain",CSetting::TYPE_SOUND,IDS_SOUND_MAIN,IDS_SOUND_MAIN_TIP,L"MenuPopup"},
	{L"SoundPopup",CSetting::TYPE_SOUND,IDS_SOUND_POPUP,IDS_SOUND_POPUP_TIP,L"MenuPopup"},
	{L"SoundCommand",CSetting::TYPE_SOUND,IDS_SOUND_COMMAND,IDS_SOUND_COMMAND_TIP,L"MenuCommand"},
	{L"SoundDrop",CSetting::TYPE_SOUND,IDS_SOUND_DROP,IDS_SOUND_DROP_TIP,L"MoveMenuItem"},
	{L"SoundButtonHover",CSetting::TYPE_SOUND,IDS_SOUND_HOVER,IDS_SOUND_HOVER_TIP,L""},

{L"Language",CSetting::TYPE_GROUP,IDS_LANGUAGE_SETTINGS_SM,0,0,0,NULL,NULL,GetLanguageSettings(COMPONENT_MENU)},
	{L"Language",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_COLD|CSetting::FLAG_SHARED},
	{L"Update",CSetting::TYPE_BOOL,0,0,1,CSetting::FLAG_SHARED}, // this is here only to generate the ADMX entry

{NULL}
};

void UpgradeSettings( bool bShared )
{
	if (bShared) return;

	// determine if we are upgrading
	if (FindSetting(L"DefaultMenuStyle")->IsDefault() &&
		FindSetting(L"MenuItems")->IsDefault() &&
		FindSetting(L"Skin1")->IsDefault() &&
		FindSetting(L"SkinOptions1")->IsDefault() &&
		FindSetting(L"SkinVariation1")->IsDefault() &&
		FindSetting(L"Skin2")->IsDefault() &&
		FindSetting(L"SkinOptions2")->IsDefault() &&
		FindSetting(L"SkinVariation2")->IsDefault())
			return;

	// determine if the old menu was using two columns
	bool bTwoColumns=false;
	const wchar_t *oldStyle=L"Classic";
	const CSetting *pSetting=FindSetting(L"DefaultMenuStyle");
	if (pSetting->value.vt==VT_BSTR)
		oldStyle=pSetting->value.bstrVal;

	// XP skin is always two columns
	pSetting=FindSetting(L"Skin1");
	if (pSetting->IsDefault())
	{
		if (_wcsicmp(oldStyle,L"XP")==0)
			bTwoColumns=true;
	}
	else if (pSetting->value.vt==VT_BSTR && _wcsicmp(pSetting->value.bstrVal,L"Windows XP Luna")==0)
		bTwoColumns=true;

	if (!bTwoColumns)
	{
		pSetting=FindSetting(L"SkinOptions1");
		if (pSetting->IsDefault())
		{
			// default setting for XP and Vista uses two columns
			if (_wcsicmp(oldStyle,L"Classic")!=0)
				bTwoColumns=true;
		}
		else if (pSetting->value.vt==VT_BSTR && wcswcs(pSetting->value.bstrVal,L"DA60029B"))
			bTwoColumns=true;
	}

	// preserve menu items
	const CSetting *pSettingItems0=FindSetting(L"MenuItems");
	CSetting *pSettingItems=FindSetting(bTwoColumns?L"MenuItems2":L"MenuItems1");
	if (!pSettingItems->IsLocked() && !pSettingItems0->IsDefault())
	{
		CString items=pSettingItems0->value;
		items.Replace(L"Command=documents\n",L"Command=recent_documents\n");
		items.Replace(L"Command=recent_items\n",L"Command=recent_programs\n");
		items.Replace(L"Command=control_panel_categories\n",L"Command=control_panel\n");
		pSettingItems->value=items;
		pSettingItems->flags&=~CSetting::FLAG_DEFAULT;
	}

	// set initial menu style
	CSetting *pSettingStyle=FindSetting(L"MenuStyle");
	if (!pSettingStyle->IsLocked())
	{
		pSettingStyle->value=(bTwoColumns?1:0);
		pSettingStyle->flags&=~CSetting::FLAG_DEFAULT;
		SetSettingsStyle(bTwoColumns?CSetting::FLAG_MENU_CLASSIC2:CSetting::FLAG_MENU_CLASSIC1,CSetting::FLAG_MENU_MASK);
	}

	// set skin
	CSetting *pSettingSkin=FindSetting(bTwoColumns?L"SkinC2":L"SkinC1");
	if (!pSettingSkin->IsLocked())
	{
		pSetting=FindSetting(L"Skin1");
		if (!pSetting->IsDefault())
		{
			pSettingSkin->value=pSetting->value;
			pSettingSkin->flags&=~CSetting::FLAG_DEFAULT;
		}
	}
	CSetting *pSettingOpt=FindSetting(bTwoColumns?L"SkinOptionsC2":L"SkinOptionsC1");
	if (!pSettingOpt->IsLocked())
	{
		pSetting=FindSetting(L"SkinOptions1");
		if (!pSetting->IsDefault())
		{
			pSettingOpt->value=pSetting->value;
			pSettingOpt->flags&=~CSetting::FLAG_DEFAULT;
		}
	}
	CSetting *pSettingVar=FindSetting(bTwoColumns?L"SkinVariationC2":L"SkinVariationC1");
	if (!pSettingVar->IsLocked())
	{
		pSetting=FindSetting(L"SkinVariation1");
		if (!pSetting->IsDefault())
		{
			pSettingVar->value=pSetting->value;
			pSettingVar->flags&=~CSetting::FLAG_DEFAULT;
		}
	}

	// set skin2
	pSettingSkin=FindSetting(L"SkinA");
	if (!pSettingSkin->IsLocked())
	{
		pSetting=FindSetting(L"Skin2");
		if (!pSetting->IsDefault())
		{
			pSettingSkin->value=pSetting->value;
			pSettingSkin->flags&=~CSetting::FLAG_DEFAULT;
		}
	}
	pSettingOpt=FindSetting(L"SkinOptionsA");
	if (!pSettingOpt->IsLocked())
	{
		pSetting=FindSetting(L"SkinOptions2");
		if (!pSetting->IsDefault())
		{
			pSettingOpt->value=pSetting->value;
			pSettingOpt->flags&=~CSetting::FLAG_DEFAULT;
		}
	}
	pSettingVar=FindSetting(L"SkinVariationA");
	if (!pSettingVar->IsLocked())
	{
		pSetting=FindSetting(L"SkinVariation2");
		if (!pSetting->IsDefault())
		{
			pSettingVar->value=pSetting->value;
			pSettingVar->flags&=~CSetting::FLAG_DEFAULT;
		}
	}
}

static CString GetWindowsBrandingString()
{
	CString retval;

	if (GetWinVersion() >= WIN_VER_WIN10)
	{
		auto winbrand = LoadLibraryEx(L"winbrand.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (winbrand)
		{
			PWSTR (WINAPI * BrandingFormatString)(PCWSTR pstrFormat);
			BrandingFormatString = (decltype(BrandingFormatString))GetProcAddress(winbrand, "BrandingFormatString");
			if (BrandingFormatString)
			{
				auto osName = BrandingFormatString(L"%WINDOWS_LONG%");
				if (osName)
				{
					retval = osName;
					GlobalFree(osName);
				}
			}

			FreeLibrary(winbrand);
		}
	}

	if (retval.IsEmpty())
	{
		// fallback for older Windows
		wchar_t title[256] = L"Windows";

		if (CRegKey reg; reg.Open(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion", KEY_READ) == ERROR_SUCCESS)
		{
			ULONG size = _countof(title);
			reg.QueryStringValue(L"ProductName", title, &size);
		}

		retval = title;
	}

	return retval;
}

void UpdateSettings( void )
{
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

	TMenuStyle menuStyle=MENU_CLASSIC1;
	{
		const CSetting *pSetting=FindSetting(L"MenuStyle");
		if (pSetting && pSetting->value.vt==VT_I4)
			menuStyle=(TMenuStyle)pSetting->value.intVal;
	}
	FindSetting(L"MaxRecentDocuments2")->pLinkTo=FindSetting(L"MaxRecentDocuments");
	FindSetting(L"SortRecentDocuments2")->pLinkTo=FindSetting(L"SortRecentDocuments");
	FindSetting(L"ControlPanelCategories2")->pLinkTo=FindSetting(L"ControlPanelCategories");
	FindSetting(L"ConfirmLogOff2")->pLinkTo=FindSetting(L"ConfirmLogOff");
	CMenuStyleDlg::UpdateDefaults();
	CCustomMenuDlg::UpdateWarnings(menuStyle);

	int dpi=FindSetting(L"OverrideDPI")->value.intVal;
	if (dpi==0)
	{
		HDC hdc=GetDC(NULL);
		dpi=GetDeviceCaps(hdc,LOGPIXELSY);
		ReleaseDC(NULL,hdc);
	}
	else if (dpi<96) dpi=96;
	else if (dpi>480) dpi=480;

	int iconSize=16;
	if (dpi>=240)
		iconSize=40;	// for 250% scaling
	else if (dpi>=216)
		iconSize=36;	// for 225% scaling
	else if (dpi>=192)
		iconSize=32;	// for 200% scaling
	else if (dpi>=168)
		iconSize=28;	// for 175% scaling
	else if (dpi>=144)
		iconSize=24;	// for 150% scaling
	else if (dpi>=120)
		iconSize=20;	// for 125% scaling
	UpdateSetting(L"SmallIconSize",CComVariant(iconSize),false);
	UpdateSetting(L"LargeIconSize",CComVariant(iconSize*2),false);

	DWORD time;
	SystemParametersInfo(SPI_GETMENUSHOWDELAY,NULL,&time,0);
	UpdateSetting(L"MenuDelay",CComVariant((int)time),false);
	SystemParametersInfo(SPI_GETMOUSEHOVERTIME,NULL,&time,0);
	UpdateSetting(L"AllProgramsDelay",CComVariant((int)time),false);

	int animation=0;
	BOOL animate;
	SystemParametersInfo(SPI_GETMENUANIMATION,NULL,&animate,0);
	if (animate)
	{
		DWORD fade;
		SystemParametersInfo(SPI_GETMENUFADE,NULL,&fade,0);
		animation=fade?1:2;
	}
	UpdateSetting(L"MainMenuAnimation",CComVariant((int)animation),false);
	UpdateSetting(L"SubMenuAnimation",CComVariant((int)animation),false);

	DWORD fade;
	SystemParametersInfo(SPI_GETSELECTIONFADE,NULL,&fade,0);
	UpdateSetting(L"MenuFadeSpeed",CComVariant(fade?400:0),false);

	UpdateSetting(L"Favorites",CComVariant(0),SHRestricted(REST_NOFAVORITESMENU)!=0);
	if (SHRestricted(REST_NORECENTDOCSMENU))
		UpdateSetting(L"Documents",CComVariant(0),true);
	else
		UpdateSetting(L"Documents",CComVariant(2),false);

	DWORD logoff1=SHRestricted(REST_STARTMENULOGOFF);
	DWORD logoff2=SHRestricted(REST_FORCESTARTMENULOGOFF);
	UpdateSetting(L"LogOff",CComVariant((logoff1!=1)?1:0),logoff1 || logoff2);

	bool bNoClose=SHRestricted(REST_NOCLOSE)!=0;
	UpdateSetting(L"Shutdown",CComVariant(bNoClose?0:2),bNoClose);
	UpdateSetting(L"RemoteShutdown",CComVariant(0),bNoClose);

	bool bNoUndock=SHRestricted(REST_NOSMEJECTPC)!=0;
	UpdateSetting(L"Undock",CComVariant(bNoUndock?0:1),bNoUndock);

	bool bNoSetFolders=SHRestricted(REST_NOSETFOLDERS)!=0; // hide control panel, printers, network
	bool bNoControlPanel=bNoSetFolders || SHRestricted(REST_NOCONTROLPANEL);
	UpdateSetting(L"ControlPanel",CComVariant(bNoControlPanel?0:1),bNoControlPanel);

	bool bNoNetwork=bNoSetFolders || SHRestricted(REST_NONETWORKCONNECTIONS);
	UpdateSetting(L"Network",CComVariant(bNoNetwork?0:1),bNoNetwork);

	UpdateSetting(L"Printers",CComVariant(bNoSetFolders?0:1),bNoSetFolders);

	bool bNoHelp=SHRestricted(REST_NOSMHELP)!=0;
	UpdateSetting(L"Help",CComVariant(bNoHelp?0:1),bNoHelp);

	bool bNoRun=SHRestricted(REST_NORUN)!=0;
	UpdateSetting(L"Run",CComVariant(bNoRun?0:1),bNoRun);
	UpdateSetting(L"SearchAutoComplete",CComVariant(bNoRun?0:1),bNoRun);

	bool bNoSearch=SHRestricted(REST_NOFIND)!=0;
	UpdateSetting(L"Search",CComVariant(bNoSearch?0:1),bNoSearch);

	bool bNoDocs=SHRestricted(REST_NOSMMYDOCS)!=0;
	UpdateSetting(L"UserFiles",CComVariant(bNoDocs?0:1),bNoDocs);
	UpdateSetting(L"UserDocuments",CComVariant(bNoDocs?0:1),bNoDocs);
	UpdateSetting(L"UserPictures",CComVariant(bNoDocs?0:1),bNoDocs);
	
	bool bNoEdit=SHRestricted(REST_NOCHANGESTARMENU)!=0;
	UpdateSetting(L"EnableDragDrop",CComVariant(bNoEdit?0:1),bNoEdit);
	UpdateSetting(L"EnableContextMenu",CComVariant(bNoEdit?0:1),bNoEdit);

	UpdateSetting(L"NumericSort",CComVariant(SHRestricted(REST_NOSTRCMPLOGICAL)?0:1),false);

	UpdateSetting(L"MenuCaption",CComVariant(GetWindowsBrandingString()),false);

	wchar_t title[256]{};
	ULONG size=_countof(title);
	if (!GetUserNameEx(NameDisplay,title,&size))
	{
		// GetUserNameEx may fail (for example on Home editions). use the login name
		size=_countof(title);
		GetUserName(title,&size);
	}
	UpdateSetting(L"MenuUsername",CComVariant(title),false);

	if (GetWinVersion()>=WIN_VER_WIN8)
	{
		HideSettingGroup(L"WindowsMenu",true);
		HideSettingGroup(L"AllProgramsSkin",true);
		UpdateSetting(L"CascadeAll",CComVariant(0),false); HideSetting(L"CascadeAll",true);
		HIGHCONTRAST contrast={sizeof(contrast)};
		if (SystemParametersInfo(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) && (contrast.dwFlags&HCF_HIGHCONTRASTON))
			UpdateSetting(L"StartButtonType",CComVariant(START_BUTTON_CLASSIC),false);
		else
			UpdateSetting(L"StartButtonType",CComVariant(START_BUTTON_AERO),false);

		HideSetting(L"EnableTouch",!(GetSystemMetrics(SM_DIGITIZER)&NID_INTEGRATED_TOUCH));

		bool bStartScreen=GetWinVersion()<WIN_VER_WIN10;
		CSetting *pSetting=FindSetting(L"MouseClick")+3;
		pSetting->nameID=bStartScreen?IDS_OPEN_WSS:IDS_OPEN_WSM;
		pSetting->tipID=bStartScreen?IDS_OPEN_WSS_TIP:IDS_OPEN_WSM_TIP;

		pSetting=FindSetting(L"ShiftClick")+3;
		pSetting->nameID=bStartScreen?IDS_OPEN_WSS:IDS_OPEN_WSM;
		pSetting->tipID=bStartScreen?IDS_OPEN_WSS_TIP:IDS_OPEN_WSM_TIP;

		pSetting=FindSetting(L"WinKey")+3;
		pSetting->nameID=bStartScreen?IDS_OPEN_WSS:IDS_OPEN_WSM;
		pSetting->tipID=bStartScreen?IDS_OPEN_WSS_TIP:IDS_OPEN_WSM_TIP;

		pSetting=FindSetting(L"ShiftWin")+3;
		pSetting->nameID=bStartScreen?IDS_OPEN_WSS:IDS_OPEN_WSM;
		pSetting->tipID=bStartScreen?IDS_OPEN_WSS_TIP:IDS_OPEN_WSM_TIP;

		pSetting=FindSetting(L"MiddleClick")+3;
		pSetting->nameID=bStartScreen?IDS_OPEN_WSS:IDS_OPEN_WSM;
		pSetting->tipID=bStartScreen?IDS_OPEN_WSS_TIP:IDS_OPEN_WSM_TIP;

		pSetting=FindSetting(L"Hover")+3;
		pSetting->nameID=bStartScreen?IDS_OPEN_WSS:IDS_OPEN_WSM;
		pSetting->tipID=bStartScreen?IDS_OPEN_WSS_TIP:IDS_OPEN_WSM_TIP;

		if (bStartScreen)
			UpdateSettingText(L"WSMHotkey",IDS_WSS_HOTKEY,IDS_WSS_HOTKEY_TIP,false);
		else
			UpdateSettingText(L"OpenMouseMonitor",IDS_MOUSE_MONITOR2,IDS_MOUSE_MONITOR_TIP2,false);
		UpdateSettingText(L"ShiftRight",IDS_RIGHT_SHIFTX,IDS_RIGHT_SHIFTX_TIP,false);

		if (GetWinVersion()>=WIN_VER_WIN81)
		{
			if (GetWinVersion()>=WIN_VER_WIN10)
			{
				UpdateGroupText(L"Metro",IDS_METRO_SETTINGS10);
				UpdateSetting(L"SkipMetro",CComVariant(0),false); HideSetting(L"SkipMetro",true);
				UpdateSetting(L"DisableHotCorner",CComVariant(0),false); HideSetting(L"DisableHotCorner",true);
			}
			else
			{
				UpdateGroupText(L"Metro",IDS_METRO_SETTINGS81);
			}
			UpdateSettingText(L"EnableStartButton",IDS_ENABLE_BUTTON2,IDS_ENABLE_BUTTON_TIP2,false);
			UpdateSetting(L"EnableStartButton",CComVariant(0),false);
			pSetting=FindSetting(L"SkipMetro");
			CRegKey regSkip;
			DWORD skip=0;
			if (regSkip.Open(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartPage",KEY_READ)==ERROR_SUCCESS)
				if (regSkip.QueryDWORDValue(L"OpenAtLogon",skip)!=ERROR_SUCCESS)
					skip=0;
			pSetting->value=CComVariant(skip==0?1:0);
		}
		else
		{
			UpdateSetting(L"SearchMetroSettings",CComVariant(0),false); HideSetting(L"SearchMetroSettings",true);
		}
	}
	else
	{
		// Windows 7
		HideSettingGroup(L"Metro",true);
		UpdateSetting(L"SkipMetro",CComVariant(0),false); HideSetting(L"SkipMetro",true);
		UpdateSetting(L"EnableStartButton",CComVariant(0),false);
		UpdateSetting(L"AllTaskbars",CComVariant(0),false); HideSetting(L"AllTaskbars",true);
		UpdateSetting(L"AllProgramsMetro",CComVariant(0),false); HideSetting(L"AllProgramsMetro",true);
		UpdateSetting(L"HideProgramsMetro",CComVariant(0),false); HideSetting(L"HideProgramsMetro",true);
		UpdateSetting(L"ShowAppsMenu",CComVariant(0),false); HideSetting(L"ShowAppsMenu",true);
		UpdateSetting(L"InvertMetroIcons",CComVariant(0),false); HideSetting(L"InvertMetroIcons",true);
		UpdateSetting(L"AeroGlass",CComVariant(0),false); HideSetting(L"AeroGlass",true);
		UpdateSettingText(L"EnableStartButton",IDS_ENABLE_BUTTON2,IDS_ENABLE_BUTTON_TIP2,false);
		UpdateSetting(L"StartButtonType",CComVariant(IsAppThemed()?START_BUTTON_AERO:START_BUTTON_CLASSIC),false);

		FindSetting(L"WinKey")[4].flags|=CSetting::FLAG_HIDDEN;
		FindSetting(L"WinKey")[5].flags|=CSetting::FLAG_HIDDEN;
		FindSetting(L"ShiftWin")[4].flags|=CSetting::FLAG_HIDDEN;
		UpdateSetting(L"RecentMetroApps",CComVariant(0),false); HideSetting(L"RecentMetroApps",true);
		UpdateSetting(L"SearchMetroApps",CComVariant(0),false); HideSetting(L"SearchMetroApps",true);
		UpdateSetting(L"SearchMetroSettings",CComVariant(0),false); HideSetting(L"SearchMetroSettings",true);
		UpdateSetting(L"HybridShutdown",CComVariant(0),false); HideSetting(L"HybridShutdown",true);
		UpdateSetting(L"EnableTouch",CComVariant(0),false); HideSetting(L"EnableTouch",true);
#ifndef STARTSCREEN_WIN7
		UpdateSetting(L"StartScreenShortcut",CComVariant(0),false); HideSetting(L"StartScreenShortcut",true);
#endif
		UpdateSetting(L"HighlightNewApps",CComVariant(0),false); HideSetting(L"HighlightNewApps",true);
	}
	if (GetWinVersion()<WIN_VER_WIN10)
	{
		int dr, dg, db, da, dc;
		GetSystemGlassColor(dr,dg,db,da,dc);
		if (dc>0)
		{
			dr/=dc;
			dg/=dc;
			db/=dc;
		}
		if (dr<0) dr=0; if (dr>255) dr=255;
		if (dg<0) dg=0; if (dg>255) dg=255;
		if (db<0) db=0; if (db>255) db=255;
		if (da<0) da=0; if (da>255) da=255;
		UpdateSetting(L"GlassColor",CComVariant(dr|(dg<<8)|(db<<16)),false);
		UpdateSetting(L"GlassIntensity",CComVariant(dc*100/255),false);
		UpdateSetting(L"GlassBlending",CComVariant(100-da*100/255),false);
		HideSetting(L"GlassOpacity",true);
		UpdateSetting(L"EnableGlass",CComVariant(1),false); HideSetting(L"EnableGlass",true);
		UpdateSetting(L"TaskbarOpacity",CComVariant(GetWinVersion()<=WIN_VER_WIN7?DEFAULT_TASK_OPACITY7:DEFAULT_TASK_OPACITY8),false);
		if (GetWinVersion()>WIN_VER_WIN7)
		{
			int color=GetSystemGlassColor8();
			UpdateSetting(L"TaskbarColor",CComVariant(RgbToBgr(color)),false);
		}

		if (GetWinVersion()<=WIN_VER_WIN7)
		{
			UpdateSetting(L"TaskbarLook",CComVariant(TASKBAR_GLASS),false);
		}
		else
		{
			FindSetting(L"TaskbarLook")[3].flags|=CSetting::FLAG_HIDDEN;
			UpdateSetting(L"TaskbarLook",CComVariant(TASKBAR_TRANSPARENT),false);
		}
/*
		FindSetting(L"ShiftClick")[6].flags|=CSetting::FLAG_HIDDEN;
		FindSetting(L"ShiftWin")[6].flags|=CSetting::FLAG_HIDDEN;
		FindSetting(L"MiddleClick")[6].flags|=CSetting::FLAG_HIDDEN;*/
	}
	else
	{
		// Windows 10
		int dr, dg, db;
		GetMetroGlassColor(dr,dg,db);
		if (dr<0) dr=0; if (dr>255) dr=255;
		if (dg<0) dg=0; if (dg>255) dg=255;
		if (db<0) db=0; if (db>255) db=255;
		UpdateSetting(L"GlassColor",CComVariant(dr|(dg<<8)|(db<<16)),false);
		HideSetting(L"GlassIntensity",true);
		HideSetting(L"GlassBlending",true);
		UpdateSetting(L"AeroGlass",CComVariant(0),false); HideSetting(L"AeroGlass",true);

		bool bTransparent;
		int color=GetMetroTaskbarColor(bTransparent)&0xFFFFFF;
		UpdateSetting(L"TaskbarLook",CComVariant(bTransparent?TASKBAR_TRANSPARENT:TASKBAR_OPAQUE),false);
		UpdateSetting(L"TaskbarColor",CComVariant(color),false);

		FindSetting(L"MouseClick")[4].flags&=~CSetting::FLAG_HIDDEN;
		FindSetting(L"ShiftClick")[4].flags&=~CSetting::FLAG_HIDDEN;
		FindSetting(L"WinKey")[5].flags|=CSetting::FLAG_HIDDEN;
	}

	bool bWSearch=HasSearchService();
	UpdateSettingText(L"SearchFiles",-1,bWSearch?IDS_SEARCH_FILES_TIP:IDS_SEARCH_FILES_TIP2,!bWSearch);

	UpdateSetting(L"MenuItems1",CComVariant(g_DefaultStartMenu1),false);
	UpdateSetting(L"MenuItems2",CComVariant(g_DefaultStartMenu2),false);
	{
		// make games disabled by default if the folder doesn't exist (like on a server)
		
		const wchar_t *defaultMenu, *gameSettings0, *gameSettings1, *gameSettings2;
		if (GetWinVersion()<WIN_VER_WIN81)
		{
			defaultMenu=g_DefaultStartMenu7;
			gameSettings0=L"Item10.Command=games\n";
			gameSettings1=L"Item10.Settings=TRACK_RECENT|NOEXPAND|ITEM_DISABLED\n";
			gameSettings2=L"Item10.Settings=TRACK_RECENT|NOEXPAND              \n";
		}
		else
		{
			defaultMenu=g_DefaultStartMenu781;
			gameSettings0=L"Item9.Command=games\n";
			gameSettings1=L"Item9.Settings=TRACK_RECENT|NOEXPAND|ITEM_DISABLED\n";
			gameSettings2=L"Item9.Settings=TRACK_RECENT|NOEXPAND              \n";
		}
		Assert(wcswcs(defaultMenu,gameSettings0));
		Assert(wcswcs(defaultMenu,gameSettings1));
		CAbsolutePidl pidl;
		if (!IsWin10RS4() && SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_Games,0,NULL,&pidl)) && !ILIsEmpty(pidl))
		{
			std::vector<wchar_t> buf(Strlen(defaultMenu)+1);
			wchar_t *str=&buf[0];
			Strcpy(str,(int)buf.size(),defaultMenu);
			memcpy(wcswcs(str,gameSettings1),gameSettings2,Strlen(gameSettings2)*2);
			UpdateSetting(L"MenuItems7",CComVariant(str),false);
		}
		else
		{
			UpdateSetting(L"MenuItems7",CComVariant(defaultMenu),false);
		}
	}
	InitStdCommands7();

	{
		const wchar_t *skin12, *skin3;
		const wchar_t *options1=L"", *options2=L"", *options3=L"";
		bool bClassic;
		if (GetWinVersion()<WIN_VER_WIN8)
			bClassic=!IsAppThemed();
		else
		{
			HIGHCONTRAST contrast={sizeof(contrast)};
			bClassic=(SystemParametersInfo(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) && (contrast.dwFlags&HCF_HIGHCONTRASTON));
		}

		if (bClassic)
		{
			skin12=L"Classic Skin";
			skin3=L"Classic Skin";
			HIGHCONTRAST contrast={sizeof(contrast)};
			if (SystemParametersInfo(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) && (contrast.dwFlags&HCF_HIGHCONTRASTON))
			{
				options1=L"CAPTION=1\nUSER_IMAGE=0\nUSER_NAME=0\nCENTER_NAME=0\nSMALL_ICONS=0\nTHICK_BORDER=0\nSOLID_SELECTION=1\n";
				options2=L"NO_ICONS=1\nUSER_IMAGE=1\nUSER_NAME=0\nCENTER_NAME=0\nSMALL_ICONS=0\nTHICK_BORDER=0\nSOLID_SELECTION=1\n";
				options3=L"USER_IMAGE=1\nSMALL_ICONS=0\nTHICK_BORDER=0\nSOLID_SELECTION=1\n";
			}
			else
			{
				options1=L"CAPTION=1\nUSER_IMAGE=0\nUSER_NAME=0\nCENTER_NAME=0\nSMALL_ICONS=0\nTHICK_BORDER=0\nSOLID_SELECTION=0\n";
				options2=L"NO_ICONS=1\nUSER_IMAGE=1\nUSER_NAME=0\nCENTER_NAME=0\nSMALL_ICONS=0\nTHICK_BORDER=0\nSOLID_SELECTION=0\n";
				options3=L"USER_IMAGE=1\nSMALL_ICONS=0\nTHICK_BORDER=0\nSOLID_SELECTION=0\n";
			}
		}
		else if (GetWinVersion()<WIN_VER_WIN8)
		{
			BOOL comp=FALSE;
			skin12=(SUCCEEDED(DwmIsCompositionEnabled(&comp)) && comp)?L"Windows Aero":L"Windows Basic";
			skin3=L"Windows Aero";
			options1=L"CAPTION=1\nUSER_IMAGE=0\nUSER_NAME=0\nCENTER_NAME=0\nSMALL_ICONS=0\nLARGE_FONT=0\nDISABLE_MASK=0\nWHITE_SUBMENUS=1\n";
			options2=L"NO_ICONS=1\nUSER_IMAGE=1\nUSER_NAME=0\nCENTER_NAME=0\nSMALL_ICONS=0\nLARGE_FONT=0\nDISABLE_MASK=0\nWHITE_SUBMENUS=1\n";
			options3=L"USER_IMAGE=1\nSMALL_ICONS=0\nLARGE_FONT=0\nDISABLE_MASK=0\nWHITE_SUBMENUS=1\n";
		}
		else if (GetWinVersion()<WIN_VER_WIN10)
		{
			skin12=L"Windows 8";
			skin3=L"Windows 8";
			options1=L"CAPTION=1\nUSER_IMAGE=0\nUSER_NAME=0\nCENTER_NAME=0\nSMALL_ICONS=0\nLARGE_FONT=0\nDISABLE_MASK=0\nOPAQUE=0\nWHITE_SUBMENUS=1\n";
			options2=L"NO_ICONS=1\nUSER_IMAGE=1\nUSER_NAME=0\nCENTER_NAME=0\nSMALL_ICONS=0\nLARGE_FONT=0\nDISABLE_MASK=0\nOPAQUE=0\nGLASS_SHADOW=0\nBLACK_TEXT=0\nBLACK_FRAMES=0\nWHITE_SUBMENUS=1\n";
			options3=L"USER_IMAGE=1\nSMALL_ICONS=0\nLARGE_FONT=0\nDISABLE_MASK=0\nOPAQUE=0\nGLASS_SHADOW=0\nBLACK_TEXT=0\nBLACK_FRAMES=0\nWHITE_SUBMENUS=1\n";
		}
		else
		{
			skin12=L"Immersive";
			skin3=L"Immersive";
			options1=L"CAPTION=1\nUSER_IMAGE=0\nUSER_NAME=0\nCENTER_NAME=0\nSMALL_ICONS=0\nLARGE_FONT=0\nICON_FRAMES=1\nOPAQUE=0\n";
			options2=L"NO_ICONS=1\nUSER_IMAGE=1\nCENTER_NAME=0\nSMALL_ICONS=0\nLARGE_FONT=0\nICON_FRAMES=1\nOPAQUE=0\n";
			options3=L"USER_IMAGE=1\nSMALL_ICONS=0\nLARGE_FONT=0\nICON_FRAMES=1\nOPAQUE=0\n";
		}
		UpdateSetting(L"SkinC1",CComVariant(skin12),false);
		UpdateSetting(L"SkinOptionsC1",CComVariant(options1),false);
		UpdateSetting(L"SkinC2",CComVariant(skin12),false);
		UpdateSetting(L"SkinOptionsC2",CComVariant(options2),false);
		UpdateSetting(L"SkinW7",CComVariant(skin3),false);
		UpdateSetting(L"SkinOptionsW7",CComVariant(options3),false);
	}

	int flags=0;
	if (menuStyle==MENU_CLASSIC1) flags=CSetting::FLAG_MENU_CLASSIC1;
	if (menuStyle==MENU_CLASSIC2) flags=CSetting::FLAG_MENU_CLASSIC2;
	if (menuStyle==MENU_WIN7) flags=CSetting::FLAG_MENU_WIN7;
	SetSettingsStyle(flags,CSetting::FLAG_MENU_MASK);
}

class MenuCustomSettings: public ICustomSettings
{
public:
	virtual int AppendBackupMenu( HMENU menu, int id );
	virtual void ExecuteBackupMenu( int id );

private:
	int m_FirstId;
};

int MenuCustomSettings::AppendBackupMenu( HMENU menu, int id )
{
	m_FirstId=id;
	AppendMenu(menu,MF_STRING,m_FirstId,LoadStringEx(IDS_CLEAR_CACHE));
	return 1;
}

void MenuCustomSettings::ExecuteBackupMenu( int id )
{
	if (id==m_FirstId)
	{
		g_ItemManager.ClearCache();
	}
}

static MenuCustomSettings g_CustomSettings;

void InitSettings( void )
{
	if (GetWinVersion()>WIN_VER_WIN8)
	{
		for (CSetting *pSetting=g_Settings;pSetting->name;pSetting++)
			if (wcscmp(pSetting->name,L"SkipMetro")==0)
			{
				pSetting->flags|=CSetting::FLAG_NODEFAULT;
				break;
			}
	}
	InitSettings(g_Settings,COMPONENT_MENU,&g_CustomSettings);
}

static int g_ButtonPath, g_ButtonSize, g_ButtonIcon, g_ButtonIconSize, g_ButtonText, g_ButtonTip;
static bool g_bButtonEnable, g_bTaskbarsChanged, g_bTaskbarsUpdated;

static void StoreButtonSettings( void )
{
	g_bButtonEnable=GetSettingBool(L"EnableStartButton");
	g_ButtonTip=CalcFNVHash(GetSettingString(L"StartButtonTip"));
	g_ButtonPath=GetSettingInt(L"StartButtonType");
	g_ButtonIcon=0;
	g_ButtonText=0;
	if (g_ButtonPath==START_BUTTON_CLASSIC)
	{
		g_ButtonIcon=CalcFNVHash(GetSettingString(L"StartButtonIcon"));
		g_ButtonText=CalcFNVHash(GetSettingString(L"StartButtonText"));
	}
	if (g_ButtonPath==START_BUTTON_CUSTOM)
		g_ButtonPath=CalcFNVHash(GetSettingString(L"StartButtonPath"));
	g_ButtonSize=GetSettingInt(L"StartButtonSize");
	g_ButtonIconSize=GetSettingInt(L"StartButtonIconSize");
}

static void UpdateButtons( bool bForce )
{
	int tip=CalcFNVHash(GetSettingString(L"StartButtonTip"));
	int path=GetSettingInt(L"StartButtonType");
	int icon=0;
	int text=0;
	if (path==START_BUTTON_CLASSIC)
	{
		icon=CalcFNVHash(GetSettingString(L"StartButtonIcon"));
		text=CalcFNVHash(GetSettingString(L"StartButtonText"));
	}
	if (path==START_BUTTON_CUSTOM)
		path=CalcFNVHash(GetSettingString(L"StartButtonPath"));
	bool bRecreate=(g_bButtonEnable!=GetSettingBool(L"EnableStartButton") || path!=g_ButtonPath || g_ButtonSize!=GetSettingInt(L"StartButtonSize")
		|| g_ButtonIcon!=icon || g_ButtonIconSize!=GetSettingInt(L"StartButtonIconSize") || g_ButtonText!=text || g_ButtonTip!=tip);
	if ((g_bTaskbarsChanged && !g_bTaskbarsUpdated) || bForce || bRecreate)
	{
		UpdateTaskBars(bRecreate?TASKBAR_RECREATE_BUTTONS:TASKBAR_UPDATE);
		UpdateTaskBars(TASKBAR_UPDATE_TEXTURE);
		g_bTaskbarsUpdated=true;
	}
	StoreButtonSettings();
}

void ClosingSettings( HWND hWnd, int flags, int command )
{
	EnableHotkeys(HOTKEYS_NORMAL);
	bool bRecreate=false;
	if (command==IDOK)
	{
		if (flags&CSetting::FLAG_COLD)
			MessageBox(hWnd,LoadStringEx(IDS_NEW_SETTINGS),LoadStringEx(IDS_APP_TITLE),MB_OK|MB_ICONWARNING);
		if (GetWinVersion()>WIN_VER_WIN8)
		{
			CRegKey regSkip;
			if (regSkip.Open(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartPage",KEY_WRITE)==ERROR_SUCCESS)
				regSkip.SetDWORDValue(L"OpenAtLogon",GetSettingBool(L"SkipMetro")?0:1);
		}
	}
	else if (command==IDCANCEL && g_bTaskbarsChanged)
	{
		g_bTaskbarsUpdated=false;
	}
	UpdateButtons(false);
	ResetHotCorners();
	RedrawTaskbars();
}

void SettingChangedCallback( const CSetting *pSetting )
{
	if (wcscmp(pSetting->name,L"CustomTaskbar")==0 || wcscmp(pSetting->name,L"TaskbarLook")==0 || wcscmp(pSetting->name,L"TaskbarOpacity")==0 || wcscmp(pSetting->name,L"TaskbarColor")==0 || wcscmp(pSetting->name,L"TaskbarTextColor")==0)
	{
		UpdateTaskBars(TASKBAR_UPDATE);
		UpdateTaskBars(TASKBAR_UPDATE_TEXTURE);
		g_bTaskbarsChanged=true;
		g_bTaskbarsUpdated=true;
	}
	else if (wcscmp(pSetting->name,L"TaskbarTexture")==0 || wcscmp(pSetting->name,L"TaskbarTileH")==0 || wcscmp(pSetting->name,L"TaskbarTileV")==0 || wcscmp(pSetting->name,L"TaskbarBordersH")==0 || wcscmp(pSetting->name,L"TaskbarBordersV")==0)
	{
		UpdateTaskBars(TASKBAR_UPDATE_TEXTURE);
		g_bTaskbarsChanged=true;
		g_bTaskbarsUpdated=true;
	}
/* not sure if I want to ever show the AeroGlass option
	if (wcscmp(pSetting->name,L"AeroGlass")==0)
	{
		CSettingsLockWrite lock;
		Assert(pSetting->value.vt==VT_I4);
		if (pSetting->value.intVal)
			FindSetting(L"TaskbarLook")[4].flags&=~CSetting::FLAG_HIDDEN;
		else
			FindSetting(L"TaskbarLook")[4].flags|=CSetting::FLAG_HIDDEN;
	}*/
}

void EditSettings( bool bModal, int tab )
{
#ifndef BUILD_SETUP
	wchar_t path[_MAX_PATH];
	GetModuleFileName(NULL,path,_countof(path));
	if (_wcsicmp(PathFindFileName(path),L"StartMenu.exe")==0)
		bModal=true;
#endif
	EnableHotkeys(HOTKEYS_SETTINGS);
	StoreButtonSettings();
	g_bTaskbarsChanged=false;
	g_bTaskbarsUpdated=true;

	wchar_t title[100];
	DWORD ver=GetVersionEx(g_Instance);
	if (ver)
		Sprintf(title,_countof(title),LoadStringEx(IDS_SETTINGS_TITLE_VER),ver>>24,(ver>>16)&0xFF,ver&0xFFFF);
	else
		Sprintf(title,_countof(title),LoadStringEx(IDS_SETTINGS_TITLE));
	EditSettings(title,bModal,tab,L"OpenShell.StartMenu.Settings");
}

bool DllImportSettingsXml( const wchar_t *fname )
{
	return ImportSettingsXml(fname);
}

bool DllExportSettingsXml( const wchar_t *fname )
{
	return ExportSettingsXml(fname);
}
