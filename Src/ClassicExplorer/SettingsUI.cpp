// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include "ExplorerBand.h"
#include "ExplorerBHO.h"
#include "SettingsUI.h"
#include "Settings.h"
#include "SettingsUIHelper.h"
#include "LanguageSettingsHelper.h"
#include "ResourceHelper.h"
#include "Translations.h"
#include "resource.h"
#include "dllmain.h"
#include <dwmapi.h>

enum
{
	SETTINGS_STYLE_WIN7=1,
	SETTINGS_STYLE_WIN8=2,
	SETTINGS_STYLE_MASK=SETTINGS_STYLE_WIN7|SETTINGS_STYLE_WIN8,
};

static const CStdCommand g_StdCommands[]={
	{L"SEPARATOR",IDS_SEPARATOR_ITEM,IDS_SEPARATOR_TIP},
	{L"up",IDS_UP_ITEM,IDS_UP_TIP,L"UpItem",NULL,L"$Toolbar.GoUp",L",2",NULL,0,L",3"},
	{L"cut",IDS_CUT_ITEM,IDS_CUT_TIP,L"CutItem",NULL,L"$Toolbar.Cut",L"shell32.dll,16762"},
	{L"copy",IDS_COPY_ITEM,IDS_COPY_TIP,L"CopyItem",NULL,L"$Toolbar.Copy",L"shell32.dll,243"},
	{L"paste",IDS_PASTE_ITEM,IDS_PASTE_TIP,L"PasteItem",NULL,L"$Toolbar.Paste",L"shell32.dll,16763"},
	{L"paste_shortcut",IDS_PASTE_LNK_ITEM,IDS_PASTE_SHORTCUT_TIP,L"PasteShortcutItem",NULL,L"$Toolbar.PasteShortcut",L"shell32.dll,16763",NULL,SETTINGS_STYLE_WIN7},
	{L"paste_shortcut",IDS_PASTE_LNK_ITEM,IDS_PASTE_SHORTCUT_TIP,L"PasteShortcutItem",NULL,L"$Toolbar.PasteShortcut",L"imageres.dll,5301",NULL,SETTINGS_STYLE_WIN8},
	{L"delete",IDS_DELETE_ITEM,IDS_DELETE_TIP,L"DeleteItem",NULL,L"$Toolbar.Delete",L"shell32.dll,240"},
	{L"properties",IDS_PROPERTIES_ITEM,IDS_PROPERTIES_TIP,L"PropertiesItem",NULL,L"$Toolbar.Properties",L"shell32.dll,253"},
	{L"email",IDS_EMAIL_ITEM,IDS_EMAIL_TIP,L"EmailItem",NULL,L"$Toolbar.Email",L"shell32.dll,265"},
	{L"settings",IDS_SETTINGS_ITEM,IDS_SETTINGS_TIP,L"SettingsItem",NULL,L"$Toolbar.Settings",L",1"},
	{L"refresh",IDS_REFRESH_ITEM,IDS_REFRESH_TIP,L"RefreshItem",NULL,L"$Toolbar.Refresh",L"shell32.dll,16739"},
	{L"stop",IDS_STOP_ITEM,IDS_STOP_TIP,L"StopItem",NULL,L"$Toolbar.Stop",L"imageres.dll,98"},
	{L"rename",IDS_RENAME_ITEM,IDS_RENAME_TIP,L"RenameItem",NULL,L"$Toolbar.Rename",L"shell32.dll,242"},
	{L"customizefolder",IDS_CUSTOMIZE_ITEM,IDS_CUSTOMIZEFOLDER_TIP,L"CustomizeFolderItem",NULL,L"$Toolbar.CustomizeFolder",L"shell32.dll,274"},
	{L"folderoptions",IDS_FOLDEROPTIONS,IDS_FOLDEROPTIONS_TIP,L"FolderOptionsItem",NULL,L"$Toolbar.FolderOptions",L"imageres.dll,166"},
	{L"newfolder",IDS_NEWFOLDER_ITEM,IDS_NEWFOLDER_TIP,L"NewFolderItem",NULL,L"$Toolbar.NewFolder",L"shell32.dll,319"},
	{L"zipfolder",IDS_ZIP_ITEM,IDS_ZIP_TIP,L"ZipItem",NULL,L"$Toolbar.ZipFolder",L"imageres.dll,174"},
	{L"nav_pane",IDS_NAVPANE_ITEM,IDS_NAVPANE_TIP,L"NavPaneItem",NULL,L"$Toolbar.NavigationPane",L"shell32.dll,16755"},
	{L"details_pane",IDS_DETAILSPANE_ITEM,IDS_DETAILSPANE_TIP,L"DetailsPaneItem",NULL,L"$Toolbar.DetailsPane",L"shell32.dll,16759",NULL,SETTINGS_STYLE_WIN7},
	{L"details_pane",IDS_DETAILSPANE_ITEM,IDS_DETAILSPANE_TIP,L"DetailsPaneItem",NULL,L"$Toolbar.DetailsPane",L"shell32.dll,16814",NULL,SETTINGS_STYLE_WIN8},
	{L"preview_pane",IDS_PREVIEWPANE_ITEM,IDS_PREVIEWPANE_TIP,L"PreviewPaneItem",NULL,L"$Toolbar.PreviewPane",L"shell32.dll,16757"},
	{L"mapdrive",IDS_MAP_DRIVE_ITEM,IDS_MAP_DRIVE_TIP,L"MapDriveItem",NULL,L"$Toolbar.MapDrive",L"shell32.dll,16779"},
	{L"disconnect",IDS_DISCONNECT_ITEM,IDS_DISCONNECT_DRIVE_TIP,L"DisconnectItem",NULL,L"$Toolbar.DisconnectDrive",L"shell32.dll,11"},
	{L"viewtiles",IDS_VIEWTILES_ITEM,IDS_VIEWTILES_TIP,L"TilesItem",NULL,L"$Toolbar.Tiles",L"shell32.dll,62999"},
	{L"viewdetails",IDS_VIEWDETAILS_ITEM,IDS_VIEWDEATAILS_TIP,L"DetailsItem",NULL,L"$Toolbar.Details",L"shell32.dll,62998"},
	{L"viewlist",IDS_VIEWLIST_ITEM,IDS_VIEWLIST_TIP,L"ListItem",NULL,L"$Toolbar.List",L"shell32.dll,63000"},
	{L"viewcontent",IDS_VIEWCONTENT_ITEM,IDS_VIEWCONTENT_TIP,L"ContentItem",NULL,L"$Toolbar.Content",L"shell32.dll,63011"},
	{L"viewicons_small",IDS_VIEWICONS1_ITEM,IDS_VIEWICONS1_TIP,L"Icons1Item",NULL,L"$Toolbar.Small",L"shell32.dll,63010"},
	{L"viewicons_medium",IDS_VIEWICONS2_ITEM,IDS_VIEWICONS2_TIP,L"Icons2Item",NULL,L"$Toolbar.Medium",L"shell32.dll,63009"},
	{L"viewicons_large",IDS_VIEWICONS3_ITEM,IDS_VIEWICONS3_TIP,L"Icons3Item",NULL,L"$Toolbar.Large",L"shell32.dll,63008"},
	{L"viewicons_extralarge",IDS_VIEWICONS4_ITEM,IDS_VIEWICONS4_TIP,L"Icons4Item",NULL,L"$Toolbar.ExtraLarge",L"shell32.dll,63001"},
	{L"moveto",IDS_MOVETO_ITEM,IDS_MOVETO_TIP,L"MoveToItem",NULL,L"$Toolbar.MoveTo",L"",NULL,SETTINGS_STYLE_WIN7},
	{L"moveto",IDS_MOVETO_ITEM,IDS_MOVETO_TIP,L"MoveToItem",NULL,L"$Toolbar.MoveTo",L"imageres.dll,5303",NULL,SETTINGS_STYLE_WIN8},
	{L"copyto",IDS_COPYTO_ITEM,IDS_COPYTO_TIP,L"CopyToItem",NULL,L"$Toolbar.CopyTo",L"",NULL,SETTINGS_STYLE_WIN7},
	{L"copyto",IDS_COPYTO_ITEM,IDS_COPYTO_TIP,L"CopyToItem",NULL,L"$Toolbar.CopyTo",L"imageres.dll,5304",NULL,SETTINGS_STYLE_WIN8},
	{L"undo",IDS_UNDO_ITEM,IDS_UNDO_TIP,L"UndoItem",NULL,L"$Toolbar.Undo",L"",NULL,SETTINGS_STYLE_WIN7},
	{L"undo",IDS_UNDO_ITEM,IDS_UNDO_TIP,L"UndoItem",NULL,L"$Toolbar.Undo",L"imageres.dll,5315",NULL,SETTINGS_STYLE_WIN8},
	{L"redo",IDS_REDO_ITEM,IDS_REDO_TIP,L"RedoItem",NULL,L"$Toolbar.Redo",L"",NULL,SETTINGS_STYLE_WIN7},
	{L"redo",IDS_REDO_ITEM,IDS_REDO_TIP,L"RedoItem",NULL,L"$Toolbar.Redo",L"imageres.dll,5311",NULL,SETTINGS_STYLE_WIN8},
	{L"selectall",IDS_SELECTALL_ITEM,IDS_SELECTALL_TIP,L"SelectAllItem",NULL,L"$Toolbar.SelectAll",L"",NULL,SETTINGS_STYLE_WIN7},
	{L"selectall",IDS_SELECTALL_ITEM,IDS_SELECTALL_TIP,L"SelectAllItem",NULL,L"$Toolbar.SelectAll",L"imageres.dll,5308",NULL,SETTINGS_STYLE_WIN8},
	{L"deselect",IDS_DESELECT_ITEM,IDS_DESELECT_TIP,L"DeselectItem",NULL,L"$Toolbar.Deselect",L"",NULL,SETTINGS_STYLE_WIN7},
	{L"deselect",IDS_DESELECT_ITEM,IDS_DESELECT_TIP,L"DeselectItem",NULL,L"$Toolbar.Deselect",L"imageres.dll,5309",NULL,SETTINGS_STYLE_WIN8},
	{L"invertselection",IDS_INVERT_ITEM,IDS_INVERT_TIP,L"InvertItem",NULL,L"$Toolbar.InvertSelection",L"",NULL,SETTINGS_STYLE_WIN7},
	{L"invertselection",IDS_INVERT_ITEM,IDS_INVERT_TIP,L"InvertItem",NULL,L"$Toolbar.InvertSelection",L"imageres.dll,5310",NULL,SETTINGS_STYLE_WIN8},
	{L"back",IDS_BACK_ITEM,IDS_BACK_TIP,L"BackItem",NULL,L"$Toolbar.Back",L""},
	{L"forward",IDS_FORWARD_ITEM,IDS_FORWARD_TIP,L"ForwardItem",NULL,L"$Toolbar.Forward",L""},
	{L"show_extensions",IDS_SHOW_EXTENSIONS,IDS_SHOW_EXTENSIONS_TIP,L"ShowExtensionsItem",NULL,L"$Toolbar.ShowExtensions"},
	{L"hidden_files",IDS_HIDDEN_FILES,IDS_HIDDEN_FILES_TIP,L"HiddenFilesItem",NULL,L"$Toolbar.ShowHiddenFiles"},
	{L"system_files",IDS_SYSTEM_FILES,IDS_SYSTEM_FILES_TIP,L"SystemFilesItem",NULL,L"$Toolbar.ShowSystemFiles"},
	{L"open <folder name>",IDS_OPEN_ITEM,IDS_OPEN_TIP,L"OpenFolderItem",NULL,NULL,L""},
	{L"sortby <property>",IDS_SORYBY_ITEM,IDS_SORT_TIP,L"SortFolderItem",NULL,NULL,L""},
	{L"groupby <property>",IDS_GROUPBY_ITEM,IDS_GROUP_TIP,L"GroupFolderItem",NULL,NULL,L""},
	{L"",IDS_CUSTOM_ITEM,IDS_CUSTOM_TIP,L"CustomItem",NULL,NULL,L""},
	{NULL},
};

static const KNOWNFOLDERID *g_CommonLinks[]=
{
	&FOLDERID_ComputerFolder,
	&FOLDERID_DesktopRoot,
	&FOLDERID_Desktop,
	&FOLDERID_Documents,
	&FOLDERID_Downloads,
	&FOLDERID_Favorites,
	&FOLDERID_Games,
	&FOLDERID_Links,
	&FOLDERID_Music,
	&FOLDERID_Pictures,
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

const wchar_t *g_DefaultToolbar=
	L"Items=UpItem, NavPaneItem, FolderOptionsItem, CutItem, CopyItem, PasteItem, DeleteItem, PropertiesItem, EmailItem, SEPARATOR, SettingsItem\n"
	L"UpItem.Command=up\n"
	L"UpItem.Tip=$Toolbar.GoUp\n"
	L"UpItem.Icon=,2\n"
	L"UpItem.IconDisabled=,3\n"
	L"NavPaneItem.Command=nav_pane\n"
	L"NavPaneItem.Tip=$Toolbar.NavigationPane\n"
	L"NavPaneItem.Icon=shell32.dll,16755\n"
	L"FolderOptionsItem.Command=folderoptions\n"
	L"FolderOptionsItem.Tip=$Toolbar.FolderOptions\n"
	L"FolderOptionsItem.Icon=imageres.dll,166\n"
	L"FolderOptionsItem.Items=ShowExtensionsItem, HiddenFilesItem, SystemFilesItem\n"
	L"ShowExtensionsItem.Command=show_extensions\n"
	L"ShowExtensionsItem.Label=$Toolbar.ShowExtensions\n"
	L"ShowExtensionsItem.Icon=none\n"
	L"HiddenFilesItem.Command=hidden_files\n"
	L"HiddenFilesItem.Label=$Toolbar.ShowHiddenFiles\n"
	L"HiddenFilesItem.Icon=none\n"
	L"SystemFilesItem.Command=system_files\n"
	L"SystemFilesItem.Label=$Toolbar.ShowSystemFiles\n"
	L"SystemFilesItem.Icon=none\n"
	L"CutItem.Command=cut\n"
	L"CutItem.Tip=$Toolbar.Cut\n"
	L"CutItem.Icon=shell32.dll,16762\n"
	L"CopyItem.Command=copy\n"
	L"CopyItem.Tip=$Toolbar.Copy\n"
	L"CopyItem.Icon=shell32.dll,243\n"
	L"PasteItem.Command=paste\n"
	L"PasteItem.Tip=$Toolbar.Paste\n"
	L"PasteItem.Icon=shell32.dll,16763\n"
	L"DeleteItem.Command=delete\n"
	L"DeleteItem.Tip=$Toolbar.Delete\n"
	L"DeleteItem.Icon=shell32.dll,240\n"
	L"PropertiesItem.Command=properties\n"
	L"PropertiesItem.Tip=$Toolbar.Properties\n"
	L"PropertiesItem.Icon=shell32.dll,253\n"
	L"EmailItem.Command=email\n"
	L"EmailItem.Tip=$Toolbar.Email\n"
	L"EmailItem.Icon=shell32.dll,265\n"
	L"SettingsItem.Command=settings\n"
	L"SettingsItem.Tip=$Toolbar.Settings\n"
	L"SettingsItem.Icon=,1\n";

const wchar_t *g_DefaultToolbar2=
	L"Items=NavPaneItem, FolderOptionsItem, CutItem, CopyItem, PasteItem, DeleteItem, PropertiesItem, EmailItem, SEPARATOR, SettingsItem\n"
	L"NavPaneItem.Command=nav_pane\n"
	L"NavPaneItem.Tip=$Toolbar.NavigationPane\n"
	L"NavPaneItem.Icon=shell32.dll,16755\n"
	L"FolderOptionsItem.Command=folderoptions\n"
	L"FolderOptionsItem.Tip=$Toolbar.FolderOptions\n"
	L"FolderOptionsItem.Icon=imageres.dll,166\n"
	L"FolderOptionsItem.Items=ShowExtensionsItem, HiddenFilesItem, SystemFilesItem\n"
	L"ShowExtensionsItem.Command=show_extensions\n"
	L"ShowExtensionsItem.Label=$Toolbar.ShowExtensions\n"
	L"ShowExtensionsItem.Icon=none\n"
	L"HiddenFilesItem.Command=hidden_files\n"
	L"HiddenFilesItem.Label=$Toolbar.ShowHiddenFiles\n"
	L"HiddenFilesItem.Icon=none\n"
	L"SystemFilesItem.Command=system_files\n"
	L"SystemFilesItem.Label=$Toolbar.ShowSystemFiles\n"
	L"SystemFilesItem.Icon=none\n"
	L"CutItem.Command=cut\n"
	L"CutItem.Tip=$Toolbar.Cut\n"
	L"CutItem.Icon=shell32.dll,16762\n"
	L"CopyItem.Command=copy\n"
	L"CopyItem.Tip=$Toolbar.Copy\n"
	L"CopyItem.Icon=shell32.dll,243\n"
	L"PasteItem.Command=paste\n"
	L"PasteItem.Tip=$Toolbar.Paste\n"
	L"PasteItem.Icon=shell32.dll,16763\n"
	L"DeleteItem.Command=delete\n"
	L"DeleteItem.Tip=$Toolbar.Delete\n"
	L"DeleteItem.Icon=shell32.dll,240\n"
	L"PropertiesItem.Command=properties\n"
	L"PropertiesItem.Tip=$Toolbar.Properties\n"
	L"PropertiesItem.Icon=shell32.dll,253\n"
	L"EmailItem.Command=email\n"
	L"EmailItem.Tip=$Toolbar.Email\n"
	L"EmailItem.Icon=shell32.dll,265\n"
	L"SettingsItem.Command=settings\n"
	L"SettingsItem.Tip=$Toolbar.Settings\n"
	L"SettingsItem.Icon=,1\n";

///////////////////////////////////////////////////////////////////////////////

class CEditToolbarDlg: public CEditCustomItemDlg
{
public:
	CEditToolbarDlg( CTreeItem *pItem, bool bNoLinks, std::vector<HMODULE> &modules ): CEditCustomItemDlg(pItem,modules) { m_bNoLinks=bNoLinks; }

	BEGIN_MSG_MAP( CEditToolbarDlg )
		MESSAGE_HANDLER( WM_INITDIALOG, OnInitDialog )
		COMMAND_ID_HANDLER( IDOK, OnOK )
		COMMAND_ID_HANDLER( IDCANCEL, OnCancel )
		COMMAND_HANDLER( IDC_COMBOCOMMAND, CBN_KILLFOCUS, OnCommandChanged )
		COMMAND_HANDLER( IDC_COMBOCOMMAND, CBN_SELENDOK, OnCommandChanged )
		COMMAND_HANDLER( IDC_BUTTONCOMMAND, BN_CLICKED, OnBrowseCommand )
		COMMAND_HANDLER( IDC_BUTTONLINK, BN_CLICKED, OnBrowseLink )
		COMMAND_HANDLER( IDC_BUTTONICON, BN_CLICKED, OnBrowseIcon )
		COMMAND_HANDLER( IDC_BUTTONICOND, BN_CLICKED, OnBrowseIcon )
		COMMAND_HANDLER( IDC_COMBOLINK, CBN_KILLFOCUS, OnLinkChanged )
		COMMAND_HANDLER( IDC_COMBOLINK, CBN_SELENDOK, OnLinkChanged )
		COMMAND_HANDLER( IDC_EDITICON, EN_KILLFOCUS, OnIconChanged )
		COMMAND_HANDLER( IDC_EDITICOND, EN_KILLFOCUS, OnIconDChanged )
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
	RESIZE_CONTROL(IDC_EDITICOND,MOVE_SIZE_X)
	RESIZE_CONTROL(IDC_BUTTONICOND,MOVE_MOVE_X)
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
	LRESULT OnIconDChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnBrowseCommand( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnBrowseLink( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnBrowseIcon( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnReset( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );

private:
	bool m_bNoLinks;
	int m_Style;
};

LRESULT CEditToolbarDlg::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	m_Style=GetWinVersion()>=WIN_VER_WIN8?SETTINGS_STYLE_WIN8:SETTINGS_STYLE_WIN7;
	CWindow commands=GetDlgItem(IDC_COMBOCOMMAND);
	CWindow links=GetDlgItem(IDC_COMBOLINK);
	InitDialog(commands,g_StdCommands,m_Style,SETTINGS_STYLE_MASK,links,g_CommonLinks);
	SetDlgItemText(IDC_EDITLABEL,m_pItem->label);
	SetDlgItemText(IDC_EDITTIP,m_pItem->tip);
	SetDlgItemText(IDC_EDITICON,m_pItem->icon);
	SetDlgItemText(IDC_EDITICOND,m_pItem->iconD);

	GetDlgItem(IDC_BUTTONRESET).EnableWindow(m_pItem->pStdCommand && *m_pItem->pStdCommand->name);

	UpdateIcons(IDC_ICONN,IDC_ICOND);

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

	str=LoadStringEx(IDS_ICOND_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_EDITICOND);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	str=LoadStringEx(IDS_RESTORE_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tool.uId=(UINT_PTR)(HWND)GetDlgItem(IDC_BUTTONRESET);
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);
	return TRUE;
}

LRESULT CEditToolbarDlg::OnOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	GetDlgItemText(IDC_EDITLABEL,m_pItem->label);
	m_pItem->label.TrimLeft();
	m_pItem->label.TrimRight();
	GetDlgItemText(IDC_EDITTIP,m_pItem->tip);
	m_pItem->tip.TrimLeft();
	m_pItem->tip.TrimRight();

	return CEditCustomItemDlg::OnOK(wNotifyCode,wID,hWndCtl,bHandled);
}

LRESULT CEditToolbarDlg::OnCommandChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	CString text=GetComboText(wNotifyCode,wID);
	if (text==m_pItem->command) return 0;
	m_pItem->SetCommand(text,g_StdCommands,m_Style,SETTINGS_STYLE_MASK);
	GetDlgItem(IDC_BUTTONRESET).EnableWindow(m_pItem->pStdCommand && *m_pItem->pStdCommand->name);
	UpdateIcons(IDC_ICONN,IDC_ICOND);
	return 0;
}

LRESULT CEditToolbarDlg::OnLinkChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	CString text=GetComboText(wNotifyCode,wID);
	if (text==m_pItem->link) return 0;
	m_pItem->link=text;
	UpdateIcons(IDC_ICONN,IDC_ICOND);
	return 0;
}

LRESULT CEditToolbarDlg::OnIconChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	CString text;
	GetDlgItemText(IDC_EDITICON,text);
	text.TrimLeft();
	text.TrimRight();
	if (text==m_pItem->icon) return 0;
	m_pItem->icon=text;
	UpdateIcons(IDC_ICONN,IDC_ICOND);
	return 0;
}

LRESULT CEditToolbarDlg::OnIconDChanged( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	CString text;
	GetDlgItemText(IDC_EDITICOND,text);
	text.TrimLeft();
	text.TrimRight();
	if (text==m_pItem->iconD) return 0;
	m_pItem->iconD=text;
	UpdateIcons(IDC_ICONN,IDC_ICOND);
	return 0;
}

LRESULT CEditToolbarDlg::OnBrowseCommand( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
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

LRESULT CEditToolbarDlg::OnBrowseLink( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
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

LRESULT CEditToolbarDlg::OnBrowseIcon( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	wchar_t text[_MAX_PATH];
	if (wID==IDC_BUTTONICON) wID=IDC_EDITICON;
	if (wID==IDC_BUTTONICOND) wID=IDC_EDITICOND;
	GetDlgItemText(wID,text,_countof(text));
	if (wID==IDC_EDITICOND && !*text)
		GetDlgItemText(IDC_EDITICON,text,_countof(text));
	if (BrowseIconHelper(m_hWnd,text))
	{
		SetDlgItemText(wID,text);
		SendMessage(WM_COMMAND,MAKEWPARAM(wID,EN_KILLFOCUS));
	}
	return 0;
}

LRESULT CEditToolbarDlg::OnReset( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	if (!m_pItem->pStdCommand || !*m_pItem->pStdCommand->name)
		return 0;
	m_pItem->link.Empty();
	m_pItem->label=m_pItem->pStdCommand->label;
	m_pItem->tip=m_pItem->pStdCommand->tip;
	m_pItem->icon=m_pItem->pStdCommand->icon;
	m_pItem->iconD=m_pItem->pStdCommand->iconD;
	SendDlgItemMessage(IDC_COMBOLINK,CB_SETCURSEL,-1);
	SetDlgItemText(IDC_EDITLABEL,m_pItem->label);
	SetDlgItemText(IDC_EDITTIP,m_pItem->tip);
	SetDlgItemText(IDC_EDITICON,m_pItem->icon);
	SetDlgItemText(IDC_EDITICOND,m_pItem->iconD);

	UpdateIcons(IDC_ICONN,IDC_ICOND);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

class CCustomToolbarDlg: public CCustomTreeDlg
{
public:
	CCustomToolbarDlg( void ): CCustomTreeDlg(false,g_StdCommands,GetWinVersion()>=WIN_VER_WIN8?SETTINGS_STYLE_WIN8:SETTINGS_STYLE_WIN7,SETTINGS_STYLE_MASK) {}

protected:
	virtual void ParseTreeItemExtra( CTreeItem *pItem, CSettingsParser &parser );
	virtual void SerializeItemExtra( CTreeItem *pItem, std::vector<wchar_t> &stringBuilder );
	virtual bool EditItem( CTreeItem *pItem, HWND tree, HTREEITEM hItem, std::vector<HMODULE> &modules );
};

void CCustomToolbarDlg::ParseTreeItemExtra( CTreeItem *pItem, CSettingsParser &parser )
{
	wchar_t text[256];
	Sprintf(text,_countof(text),L"%s.IconDisabled",pItem->name);
	pItem->iconD=parser.FindSetting(text,L"");
}

void CCustomToolbarDlg::SerializeItemExtra( CTreeItem *pItem, std::vector<wchar_t> &stringBuilder )
{
	if (!pItem->iconD.IsEmpty())
	{
		wchar_t text[2048];
		Sprintf(text,_countof(text),L"%s.IconDisabled=%s\n",pItem->name,pItem->iconD);
		AppendString(stringBuilder,text);
	}
}

bool CCustomToolbarDlg::EditItem( CTreeItem *pItem, HWND tree, HTREEITEM hItem, std::vector<HMODULE> &modules )
{
	bool bNoLinks=(TreeView_GetParent(tree,hItem) || TreeView_GetChild(tree,hItem));
	return CEditToolbarDlg(pItem,bNoLinks,modules).Run(m_hWnd,IDD_CUSTOMTOOLBAR);
}

class CCustomToolbarPanel: public ISettingsPanel
{
public:
	virtual HWND Create( HWND parent );
	virtual HWND Activate( CSetting *pGroup, const RECT &rect, bool bReset );
	virtual bool Validate( HWND parent ) { return true; }

private:
	static CCustomToolbarDlg s_Dialog;
};

CCustomToolbarDlg CCustomToolbarPanel::s_Dialog;

HWND CCustomToolbarPanel::Create( HWND parent )
{
	if (!s_Dialog.m_hWnd)
		s_Dialog.Create(parent,LoadDialogEx(IDD_CUSTOMTREE));
	return s_Dialog.m_hWnd;
}

HWND CCustomToolbarPanel::Activate( CSetting *pGroup, const RECT &rect, bool bReset )
{
	s_Dialog.SetGroup(pGroup,bReset);
	s_Dialog.SetWindowPos(HWND_TOP,&rect,SWP_SHOWWINDOW);
	return s_Dialog.m_hWnd;
}

static CCustomToolbarPanel g_CustomToolbarPanel;

///////////////////////////////////////////////////////////////////////////////

static CSetting g_Settings[]={
{L"Basic",CSetting::TYPE_GROUP,IDS_BASIC_SETTINGS},
	{L"EnableSettings",CSetting::TYPE_BOOL,0,0,1,CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"ProcessWhiteList",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"ProcessBlackList",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},
	{L"NoInitialToolbar",CSetting::TYPE_BOOL,0,0,0,CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},

{L"NavigationPane",CSetting::TYPE_GROUP,IDS_NAVIGATION_SETTINGS},
	{L"TreeStyle",CSetting::TYPE_INT,IDS_TREE_STYLE,IDS_TREE_STYLE_TIP,2,CSetting::FLAG_WARM|CSetting::FLAG_BASIC},
		{L"XPClassic",CSetting::TYPE_RADIO,IDS_XPCLASSIC,IDS_XPCLASSIC_TIP},
		{L"XPSimple",CSetting::TYPE_RADIO,IDS_XPSIMPLE,IDS_XPSIMPLE_TIP},
		{L"Vista",CSetting::TYPE_RADIO,IDS_VISTA,IDS_VISTA_TIP},
	{L"HScrollbar",CSetting::TYPE_INT,IDS_SCROLLBAR,IDS_SCROLLBAR_TIP,0,CSetting::FLAG_WARM},
		{L"Default",CSetting::TYPE_RADIO,IDS_SCROLLBAR_DEFAULT,IDS_SCROLLBAR_DEFAULT_TIP},
		{L"Off",CSetting::TYPE_RADIO,IDS_SCROLLBAR_OFF,IDS_SCROLLBAR_OFF_TIP},
		{L"On",CSetting::TYPE_RADIO,IDS_SCROLLBAR_ON,IDS_SCROLLBAR_ON_TIP},
	{L"NoFadeButtons",CSetting::TYPE_BOOL,IDS_NO_FADE,IDS_NO_FADE_TIP,0,CSetting::FLAG_WARM,L"TreeStyle"},
	{L"TreeItemSpacing",CSetting::TYPE_INT,IDS_TREE_SPACING,IDS_TREE_SPACING_TIP,0,CSetting::FLAG_WARM},
	{L"FullIndent",CSetting::TYPE_BOOL,IDS_FULL_INDENT,IDS_FULL_INDENT_TIP,0,CSetting::FLAG_WARM,L"TreeStyle"},
	{L"AutoNavigate",CSetting::TYPE_INT,IDS_NAVIGATE,IDS_NAVIGATE_TIP,0,CSetting::FLAG_WARM|CSetting::FLAG_BASIC},
		{L"Default",CSetting::TYPE_RADIO,IDS_NAV_DEFAULT,IDS_NAV_DEFAULT_TIP},
		{L"KeyboardOnly",CSetting::TYPE_RADIO,IDS_NAV_KBD,IDS_NAV_KBD_TIP},
		{L"Always",CSetting::TYPE_RADIO,IDS_NAV_ALWAYS,IDS_NAV_ALWAYS_TIP},
	{L"AutoNavDelay",CSetting::TYPE_INT,IDS_NAV_DELAY,IDS_NAV_DELAY_TIP,100,CSetting::FLAG_WARM,L"AutoNavigate"},
	{L"AltEnter",CSetting::TYPE_BOOL,IDS_ALT_ENTER,IDS_ALT_ENTER_TIP,1,CSetting::FLAG_WARM|CSetting::FLAG_BASIC},
	{L"FixFolderScroll",CSetting::TYPE_BOOL,IDS_FIX_SCROLL,IDS_FIX_SCROLL_TIP,1,CSetting::FLAG_WARM},

{L"TitleBar",CSetting::TYPE_GROUP,IDS_TITLE_SETTINGS},
	{L"ShowCaption",CSetting::TYPE_BOOL,IDS_SHOW_CAPTION,IDS_SHOW_CAPTION_TIP,0,CSetting::FLAG_WARM},
	{L"ShowIcon",CSetting::TYPE_BOOL,IDS_SHOW_ICON,IDS_SHOW_ICON_TIP,0,CSetting::FLAG_WARM},
	{L"DisableBreadcrumbs",CSetting::TYPE_BOOL,IDS_NO_BREADCRUMBS,IDS_NO_BREADCRUMBS_TIP,0,CSetting::FLAG_WARM|CSetting::FLAG_BASIC},
	{L"AddressBarHistory",CSetting::TYPE_INT,IDS_ADDRESS_HISTORY,IDS_ADDRESS_HISTORY_TIP,0,CSetting::FLAG_WARM},
		{L"NormalHistory",CSetting::TYPE_RADIO,IDS_NORMAL_HISTORY,IDS_NORMAL_HISTORY_TIP},
		{L"SimplePath",CSetting::TYPE_RADIO,IDS_SIMPLE_PATH,IDS_SIMPLE_PATH_TIP},
		{L"ExtendedPath",CSetting::TYPE_RADIO,IDS_EXTENDED_PATH,IDS_EXTENDED_PATH_TIP},
	{L"HideSearch",CSetting::TYPE_BOOL,IDS_HIDE_SEARCH,IDS_HIDE_SEARCH_TIP,0,CSetting::FLAG_WARM},
	{L"AddressAltD",CSetting::TYPE_STRING,IDS_ALT_D,IDS_ALT_D_TIP,L"",CSetting::FLAG_WARM},
	{L"UpHotkey2",CSetting::TYPE_HOTKEY_ANY,IDS_UP_HOTKEY,IDS_UP_HOTKEY_TIP,0,CSetting::FLAG_WARM},

{L"UpButton",CSetting::TYPE_GROUP,IDS_UP_SETTINGS},
	{L"ShowUpButton",CSetting::TYPE_INT,IDS_SHOW_UP,IDS_SHOW_UP_TIP,2,CSetting::FLAG_WARM|CSetting::FLAG_BASIC},
		{L"DontShow",CSetting::TYPE_RADIO,IDS_DONT_SHOW,IDS_DONT_SHOW_TIP},
		{L"BeforeBack",CSetting::TYPE_RADIO,IDS_BEFORE_BACK,IDS_BEFORE_BACK_TIP},
		{L"AfterBack",CSetting::TYPE_RADIO,IDS_AFTER_BACK,IDS_AFTER_BACK_TIP},
	{L"UpIconNormal",CSetting::TYPE_ICON,IDS_UP_NORMAL,IDS_UP_NORMAL_TIP,L",4",CSetting::FLAG_WARM,L"ShowUpButton"},
	{L"UpIconPressed",CSetting::TYPE_ICON,IDS_UP_PRESSED,IDS_UP_PRESSED_TIP,L",5",CSetting::FLAG_WARM,L"ShowUpButton"},
	{L"UpIconHot",CSetting::TYPE_ICON,IDS_UP_HOT,IDS_UP_HOT_TIP,L",6",CSetting::FLAG_WARM,L"ShowUpButton"},
	{L"UpIconDisabled",CSetting::TYPE_ICON,IDS_UP_DISABLED,IDS_UP_DISABLED_TIP,L",7",CSetting::FLAG_WARM,L"ShowUpButton"},
	{L"UpIconSize",CSetting::TYPE_INT,IDS_UP_SIZE,IDS_UP_SIZE_TIP,0,CSetting::FLAG_WARM,L"ShowUpButton"}, // 30 for DPI<120 and 36 for DPI>=120
	{L"UpHotkey",CSetting::TYPE_HOTKEY_ANY,IDS_UP_HOTKEY,IDS_UP_HOTKEY_TIP,0,CSetting::FLAG_WARM},

{L"Toolbar",CSetting::TYPE_GROUP,IDS_TOOLBAR_SETTINGS},
	{L"UseBigButtons",CSetting::TYPE_BOOL,IDS_BIG_BUTTONS,IDS_BIG_BUTTONS_TIP,1,CSetting::FLAG_WARM},
	{L"SmallIconSize",CSetting::TYPE_INT,IDS_SMALL_SIZE,IDS_SMALL_SIZE_TIP,-1,CSetting::FLAG_WARM}, // 16 for DPI<120 and 24 for DPI>=120
	{L"LargeIconSize",CSetting::TYPE_INT,IDS_LARGE_SIZE,IDS_LARGE_SIZE_TIP,-1,CSetting::FLAG_WARM}, // 24 for DPI<120 and 32 for DPI>=120
	{L"MenuIconSize",CSetting::TYPE_INT,IDS_MENU_SIZE,IDS_MENU_SIZE_TIP,16,CSetting::FLAG_WARM},
	{L"ToolbarListMode",CSetting::TYPE_BOOL,IDS_LIST_MODE,IDS_LIST_MODE_TIP,0,CSetting::FLAG_WARM},
	{L"SameSizeButtons",CSetting::TYPE_BOOL,IDS_SAME_SIZE,IDS_SAME_SIZE_TIP,0,CSetting::FLAG_WARM,L"ToolbarListMode=0"},
	{L"ResizeableToolbar",CSetting::TYPE_BOOL,IDS_RESIZEABLE,IDS_RESIZEABLE_TIP,0,CSetting::FLAG_WARM},

{L"CustomToolbar",CSetting::TYPE_GROUP,IDS_BUTTONS_SETTINGS,0,0,0,NULL,NULL,&g_CustomToolbarPanel},
	{L"ToolbarItems",CSetting::TYPE_MULTISTRING,0,0,g_DefaultToolbar,CSetting::FLAG_WARM},

{L"StatusBar",CSetting::TYPE_GROUP,IDS_STATUS_SETTINGS},
	{L"ShowFreeSpace",CSetting::TYPE_BOOL,IDS_FREE_SPACE,IDS_FREE_SPACE_TIP,1,CSetting::FLAG_WARM|CSetting::FLAG_BASIC},
	{L"ShowInfoTip",CSetting::TYPE_BOOL,IDS_INFO_TIP,IDS_INFO_TIP_TIP,1,CSetting::FLAG_WARM,L"ShowFreeSpace"},
	{L"ForceRefreshWin7",CSetting::TYPE_BOOL,IDS_FORCE_REFRESH,IDS_FORCE_REFRESH_TIP,1,CSetting::FLAG_WARM,L"ShowFreeSpace"},

{L"StatusBar8",CSetting::TYPE_GROUP,IDS_STATUS_SETTINGS},
	{L"ShowStatusBar",CSetting::TYPE_BOOL,IDS_SHOWSTATUSBAR,IDS_SHOWSTATUSBAR_TIP,1,CSetting::FLAG_WARM|CSetting::FLAG_BASIC},
	{L"ShowFreeSpace2",CSetting::TYPE_BOOL,IDS_FREE_SPACE,IDS_FREE_SPACE_TIP,1,CSetting::FLAG_WARM,L"ShowStatusBar",L"ShowStatusBar"},
	{L"ShowZone",CSetting::TYPE_BOOL,IDS_SHOW_ZONE,IDS_SHOW_ZONE_TIP,1,CSetting::FLAG_WARM,L"ShowStatusBar",L"ShowStatusBar"},
	{L"ShowInfoTip2",CSetting::TYPE_BOOL,IDS_INFO_TIP,IDS_INFO_TIP_TIP,1,CSetting::FLAG_WARM,L"ShowStatusBar",L"ShowStatusBar"},
	{L"StatusBarFont",CSetting::TYPE_FONT,IDS_STATUS_FONT,IDS_STATUS_FONT_TIP,L"Segoe UI, normal, 9",CSetting::FLAG_WARM,L"ShowStatusBar",L"ShowStatusBar"},

{L"FilePane",CSetting::TYPE_GROUP,IDS_FILEPANE_SETTINGS},
	{L"ShareOverlay",CSetting::TYPE_BOOL,IDS_SHARE,IDS_SHARE_TIP,0,CSetting::FLAG_COLD|CSetting::FLAG_BASIC},
	{L"ShareOverlayIcon",CSetting::TYPE_ICON,IDS_SHARE_ICON,IDS_SHARE_ICON_TIP,L"%windir%\\system32\\imageres.dll,164",CSetting::FLAG_COLD,L"ShareOverlay",L"ShareOverlay"},
	{L"ShareOverlayHidden",CSetting::TYPE_BOOL,IDS_SHARE_HIDDEN,IDS_SHARE_HIDDEN_TIP,0,CSetting::FLAG_COLD,L"ShareOverlay",L"ShareOverlay"},
	{L"ShareExplorer",CSetting::TYPE_BOOL,IDS_SHARE_EXPLORER,IDS_SHARE_EXPLORER_TIP,1,CSetting::FLAG_COLD,L"ShareOverlay",L"ShareOverlay"},
	{L"ShowHeaders",CSetting::TYPE_BOOL,IDS_HEADERS,IDS_HEADERS_TIP,0,CSetting::FLAG_WARM},
	{L"HideScrollTip",CSetting::TYPE_BOOL,IDS_SCROLLTIP,IDS_SCROLLTIP_TIP,0,CSetting::FLAG_WARM},

{L"FileOperation",CSetting::TYPE_GROUP,IDS_FILE_SETTINGS},
	{L"ReplaceFileUI",CSetting::TYPE_BOOL,IDS_FILE_UI,IDS_FILE_UI_TIP,1,CSetting::FLAG_WARM|CSetting::FLAG_BASIC},
	{L"ReplaceFolderUI",CSetting::TYPE_BOOL,IDS_FOLDER_UI,IDS_FOLDER_UI_TIP,1,CSetting::FLAG_WARM|CSetting::FLAG_BASIC},
	{L"OverwriteAlertLevel",CSetting::TYPE_INT,IDS_ALERT_LEVEL,IDS_ALERT_LEVEL_TIP,0,CSetting::FLAG_WARM,L"ReplaceFileUI",L"ReplaceFileUI"},
		{L"NoAlert",CSetting::TYPE_RADIO,IDS_NO_ALERT,IDS_NO_ALERT_TIP},
		{L"SystemFiles",CSetting::TYPE_RADIO,IDS_SYS_FILES,IDS_SYS_FILES_TIP},
		{L"ReadOnlyFiles",CSetting::TYPE_RADIO,IDS_RO_FILES,IDS_RO_FILES_TIP},
	{L"EnableMore",CSetting::TYPE_BOOL,IDS_MORE,IDS_MORE_TIP,0,CSetting::FLAG_WARM},
	{L"MoreProgressDelay",CSetting::TYPE_INT,IDS_MORE_DELAY,IDS_MORE_DELAY_TIP,-1,CSetting::FLAG_WARM,L"EnableMore",L"EnableMore"}, // 500 for Windows 7 Aero, and 0 otherwise
	{L"FileExplorer",CSetting::TYPE_BOOL,IDS_FILE_EXPLORER,IDS_FILE_EXPLORER_TIP,1,CSetting::FLAG_COLD},

{L"Language",CSetting::TYPE_GROUP,IDS_LANGUAGE_SETTINGS,0,0,0,NULL,NULL,GetLanguageSettings(COMPONENT_EXPLORER)},
	{L"Language",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_COLD|CSetting::FLAG_SHARED},

{NULL}
};

void UpgradeSettings( bool bShared )
{
}

void UpdateSettings( void )
{
	HDC hdc=GetDC(NULL);
	int dpi=GetDeviceCaps(hdc,LOGPIXELSY);
	ReleaseDC(NULL,hdc);
	UpdateSetting(L"SmallIconSize",CComVariant((dpi>=120)?24:16),false);
	UpdateSetting(L"LargeIconSize",CComVariant((dpi>=120)?32:24),false);
	UpdateSetting(L"UpIconSize",CComVariant((dpi>=120)?36:30),false);
	FindSetting(L"UpHotkey2")->pLinkTo=FindSetting(L"UpHotkey");

	if (GetWinVersion()>=WIN_VER_WIN8)
	{
		// Windows 8
		HideSettingGroup(L"StatusBar",true);
			HideSetting(L"ShowFreeSpace",true);
			FindSetting(L"ShowFreeSpace2")->pLinkTo=FindSetting(L"ShowFreeSpace");
			FindSetting(L"ShowInfoTip2")->pLinkTo=FindSetting(L"ShowInfoTip");

		HideSettingGroup(L"UpButton",true);
			UpdateSetting(L"ShowUpButton",CComVariant(0),false); HideSetting(L"ShowUpButton",true);

		HideSettingGroup(L"FileOperation",true);
			UpdateSetting(L"ReplaceFileUI",CComVariant(0),false); HideSetting(L"ReplaceFileUI",true);
			UpdateSetting(L"ReplaceFolderUI",CComVariant(0),false); HideSetting(L"ReplaceFolderUI",true);

		UpdateSetting(L"ShowCaption",CComVariant(0),false); HideSetting(L"ShowCaption",true);
		UpdateSetting(L"ShowIcon",CComVariant(0),false); HideSetting(L"ShowIcon",true);
		UpdateSetting(L"FixFolderScroll",CComVariant(0),false);
		UpdateSetting(L"ToolbarItems",CComVariant(g_DefaultToolbar2),false);

		if (GetWinVersion()>=WIN_VER_WIN10)
		{
			FindSetting(L"TreeStyle")[1].flags|=CSetting::FLAG_HIDDEN;
		}
	}
	else
	{
		// Windows 7
		int delay=0;
		BOOL comp;
		if (SUCCEEDED(DwmIsCompositionEnabled(&comp)) && comp)
			delay=500;
		UpdateSetting(L"MoreProgressDelay",CComVariant(delay),false);
		HideSetting(L"UpHotkey2",true);
		HideSettingGroup(L"StatusBar8",true);
	}

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

static bool g_bCopyHook0; // initial state of the copy hook before the settings are edited

void InitSettings( void )
{
	InitSettings(g_Settings,COMPONENT_EXPLORER,NULL);
	g_bCopyHook0=GetWinVersion()<WIN_VER_WIN8 && (GetSettingBool(L"ReplaceFileUI") || GetSettingBool(L"ReplaceFolderUI") || GetSettingBool(L"EnableMore"));
}

void ClosingSettings( HWND hWnd, int flags, int command )
{
	if (command==IDOK)
	{
		bool bCopyHook=GetWinVersion()<WIN_VER_WIN8 && (GetSettingBool(L"ReplaceFileUI") || GetSettingBool(L"ReplaceFolderUI") || GetSettingBool(L"EnableMore"));

		if ((flags&CSetting::FLAG_COLD) || (bCopyHook && !g_bCopyHook0))
			MessageBox(hWnd,LoadStringEx(IDS_NEW_SETTINGS2),LoadStringEx(IDS_APP_TITLE),MB_OK|MB_ICONWARNING);
		else if (flags&CSetting::FLAG_WARM)
			MessageBox(hWnd,LoadStringEx(IDS_NEW_SETTINGS1),LoadStringEx(IDS_APP_TITLE),MB_OK|MB_ICONINFORMATION);
	}
}

void SettingChangedCallback( const CSetting *pSetting )
{
}

void EditSettings( void )
{
	STARTUPINFO startupInfo={sizeof(startupInfo)};
	PROCESS_INFORMATION processInfo;
	memset(&processInfo,0,sizeof(processInfo));
	wchar_t path[_MAX_PATH];
	GetModuleFileName(g_Instance,path,_countof(path));
	wchar_t *end=PathFindFileName(path);
	*end=0;
	Strcpy(end,(int)(path+_countof(path)-end),L"ClassicExplorerSettings.exe");
	if (CreateProcess(NULL,path,NULL,NULL,TRUE,0,NULL,NULL,&startupInfo,&processInfo))
	{
		CloseHandle(processInfo.hProcess);
		CloseHandle(processInfo.hThread);
	}
}

void ShowSettingsMenu( HWND parent, int x, int y )
{
	HMENU menu=CreatePopupMenu();
	AppendMenu(menu,MF_STRING,CBandWindow::ID_SETTINGS,FindTranslation(L"Toolbar.Settings",L"Classic Explorer Settings"));
	int size=16;
	HBITMAP shellBmp=NULL;
	HICON icon=(HICON)LoadImage(g_Instance,MAKEINTRESOURCE(IDI_APPICON),IMAGE_ICON,size,size,LR_DEFAULTCOLOR);
	if (icon)
	{
		shellBmp=BitmapFromIcon(icon,size,NULL,true);
		MENUITEMINFO mii={sizeof(mii)};
		mii.fMask=MIIM_BITMAP;
		mii.hbmpItem=shellBmp;
		SetMenuItemInfo(menu,CBandWindow::ID_SETTINGS,FALSE,&mii);
	}
	MENUINFO info={sizeof(info),MIM_STYLE,MNS_CHECKORBMP};
	SetMenuInfo(menu,&info);

	DWORD pos=GetMessagePos();
	if (!GetSettingBool(L"EnableSettings"))
		EnableMenuItem(menu,0,MF_BYPOSITION|MF_GRAYED);
	int res=TrackPopupMenu(menu,TPM_RIGHTBUTTON|TPM_RETURNCMD,x,y,0,parent,NULL);
	DestroyMenu(menu);
	if (shellBmp) DeleteObject(shellBmp);
	if (res==CBandWindow::ID_SETTINGS)
		EditSettings();
}

void ShowExplorerSettings( void )
{
	WaitDllInitThread();
	if (!GetSettingBool(L"EnableSettings"))
		return;
	wchar_t title[100];
	DWORD ver=GetVersionEx(g_Instance);
	if (ver)
		Sprintf(title,_countof(title),LoadStringEx(IDS_SETTINGS_TITLE_VER),ver>>24,(ver>>16)&0xFF,ver&0xFFFF);
	else
		Sprintf(title,_countof(title),LoadStringEx(IDS_SETTINGS_TITLE));
	EditSettings(title,true,0);
}

bool DllImportSettingsXml( const wchar_t *fname )
{
	return ImportSettingsXml(fname);
}

bool DllExportSettingsXml( const wchar_t *fname )
{
	return ExportSettingsXml(fname);
}

#ifndef _WIN64
bool DllSaveAdmx( const char *admxFile, const char *admlFile, const char *docFile, const wchar_t *language )
{
	WaitDllInitThread();
	HMODULE dll=NULL;
	if (language[0])
	{
		wchar_t path[_MAX_PATH];
		GetCurrentDirectory(_countof(path),path);
		PathAppend(path,language);
		PathAddExtension(path,L".dll");
		dll=LoadLibraryEx(path,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
	}
	LoadTranslationResources(dll,NULL);
	return SaveAdmx(COMPONENT_EXPLORER,admxFile,admlFile,docFile);
}
#endif
