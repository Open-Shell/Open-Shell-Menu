// ## MenuContainer.h
// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

// MenuCommands.cpp - handles the commands and actions of CMenuContainer

#include "stdafx.h"
#include "MenuContainer.h"
#include "StartMenuDLL.h"
#include "Settings.h"
#include "SettingsUI.h"
#include "SettingsUIHelper.h"
#include "FileHelper.h"
#include "Translations.h"
#include "LogManager.h"
#include "FNVHash.h"
#include "ResourceHelper.h"
#include "MetroLinkManager.h"
#include "ProgramsTree.h"
#include "resource.h"
#include <WtsApi32.h>
#include <PowrProf.h>
#include <propvarutil.h>
#include <algorithm>
#include <propkey.h>

static CString g_RenameText;
static POINT g_RenamePos;

// Dialog proc for the Rename dialog box
static INT_PTR CALLBACK RenameDlgProc( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	if (uMsg==WM_INITDIALOG)
	{
		// translate text
		SetWindowText(hwndDlg,FindTranslation(L"Menu.RenameTitle",L"Rename"));
		SetDlgItemText(hwndDlg,IDC_LABEL,FindTranslation(L"Menu.RenamePrompt",L"&New name:"));
		SetDlgItemText(hwndDlg,IDOK,FindTranslation(L"Menu.RenameOK",L"OK"));
		SetDlgItemText(hwndDlg,IDCANCEL,FindTranslation(L"Menu.RenameCancel",L"Cancel"));
		SetDlgItemText(hwndDlg,IDC_EDITNAME,g_RenameText);
		// position near the item
		SetWindowPos(hwndDlg,NULL,g_RenamePos.x,g_RenamePos.y,0,0,SWP_NOZORDER|SWP_NOSIZE);
		SendMessage(hwndDlg,DM_REPOSITION,0,0);
		return TRUE;
	}
	if (uMsg==WM_COMMAND && wParam==IDOK)
	{
		wchar_t buf[1024];
		GetDlgItemText(hwndDlg,IDC_EDITNAME,buf,_countof(buf));
		g_RenameText=buf;

		EndDialog(hwndDlg,1);
		return TRUE;
	}
	if (uMsg==WM_COMMAND && wParam==IDCANCEL)
	{
		EndDialog(hwndDlg,0);
		return TRUE;
	}
	return FALSE;
}

static bool SetShutdownPrivileges()
{
	bool retval = false;

	HANDLE hToken;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &hToken))
	{
		TOKEN_PRIVILEGES tp={1};
		if (LookupPrivilegeValue(NULL, L"SeShutdownPrivilege", &tp.Privileges[0].Luid))
		{
			tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
			if (AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL) && GetLastError() == ERROR_SUCCESS)
				retval = true;
		}
		CloseHandle(hToken);
	}

	return retval;
}

static void DoSearchSubst( wchar_t *buf, int size, const wchar_t *search )
{
	wchar_t search2[256];
	char utf8[1024];
	WcsToMbs(utf8,_countof(utf8),search,CP_UTF8);
	int len=0;
	for (const char *c=utf8;*c;c++)
	{
		if ((*c>='a' && *c<='z') || (*c>='A' && *c<='Z') || (*c>='0' && *c<='9'))
		{
			search2[len++]=*c;
		}
		else if (len<_countof(search2)-4)
		{
			len+=Sprintf(search2+len,_countof(search2)-len,L"%%%02X",(unsigned char)*c);
		}
		else
			break;
	}
	search2[len]=0;
	DWORD_PTR args[100]={(DWORD_PTR)search,(DWORD_PTR)search2};
	wchar_t *pBuf=buf;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_ARGUMENT_ARRAY|FORMAT_MESSAGE_FROM_STRING,buf,0,0,(LPWSTR)&pBuf,0,(va_list*)args);
	Strcpy(buf,size,pBuf);
	LocalFree(pBuf);
}

static DWORD CALLBACK ExecuteCommandThread( void *param )
{
	CoInitialize(NULL);
	const wchar_t *command=(wchar_t*)param;
	wchar_t exe[_MAX_PATH];
	const wchar_t *args=NULL;
	CComString strExe, strArgs;
	if (SUCCEEDED(SHEvaluateSystemCommandTemplate(command,&strExe,NULL,&strArgs)))
	{
		args=strArgs;
		Strcpy(exe,_countof(exe),strExe);
	}
	else
	{
		args=SeparateArguments(command,exe);
	}
	SHELLEXECUTEINFO execute={sizeof(execute),SEE_MASK_FLAG_LOG_USAGE};
	execute.lpFile=exe;
	execute.lpParameters=args;
	execute.nShow=SW_SHOWNORMAL;
	ShellExecuteEx(&execute);
	free(param);
	CoUninitialize();
	return 0;
}

void CMenuContainer::ExecuteCommand( const wchar_t *command, bool bElevated, bool bEnvSubst )
{
	wchar_t text[1024];
	if (bEnvSubst)
	{
		Strcpy(text,_countof(text),command);
		DoEnvironmentSubst(text,_countof(text));
		command=text;
	}
	if (bElevated)
	{
		wchar_t cmdLine[1024];
		Sprintf(cmdLine,_countof(cmdLine),L"-runas %s",command);

		wchar_t exe[_MAX_PATH];
		GetModuleFileName(_AtlBaseModule.GetModuleInstance(),exe,_countof(exe));
		PathRemoveFileSpec(exe);
		PathAppend(exe,L"StartMenu.exe");

		RECT rc;
		if (m_bDestroyed)
			::GetWindowRect(g_TaskBar,&rc);
		else
			GetWindowRect(&rc);
		::SetForegroundWindow(g_OwnerWindow);
		::SetWindowPos(g_OwnerWindow,HWND_TOPMOST,rc.left,rc.top,rc.right-rc.left,rc.bottom-rc.top,0);
		ShellExecute(g_OwnerWindow,L"runas",exe,cmdLine,NULL,SW_SHOWNORMAL);
	}
	else
	{
		CreateThread(NULL,0,ExecuteCommandThread,_wcsdup(command),0,NULL);
	}
}

// Dialog proc for the Log Off dialog box
static INT_PTR CALLBACK LogOffDlgProc( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	if (uMsg==WM_INITDIALOG)
	{
		// translate text
		SendDlgItemMessage(hwndDlg,IDC_STATICICON1,STM_SETICON,lParam,0);
		SetWindowText(hwndDlg,FindTranslation(L"Menu.LogoffTitle",L"Log Off Windows"));
		SetDlgItemText(hwndDlg,IDC_PROMPT,FindTranslation(L"Menu.LogoffPrompt",L"Are you sure you want to log off?"));
		SetDlgItemText(hwndDlg,IDOK,FindTranslation(L"Menu.LogoffYes",L"&Log Off"));
		SetDlgItemText(hwndDlg,IDCANCEL,FindTranslation(L"Menu.LogoffNo",L"&No"));
		return TRUE;
	}
	if (uMsg==WM_COMMAND && wParam==IDOK)
	{
		EndDialog(hwndDlg,1);
		return TRUE;
	}
	if (uMsg==WM_COMMAND && wParam==IDCANCEL)
	{
		EndDialog(hwndDlg,0);
		return TRUE;
	}
	return FALSE;
}

struct ShortcutParams
{
	ShortcutParams( void ) { memset(this,0,sizeof(*this)); }
	wchar_t target[_MAX_PATH+1];
	wchar_t temp[_MAX_PATH];
	wchar_t fname[_MAX_PATH+1];
};

static DWORD WINAPI NewShortcutThread( void *param )
{
	ShortcutParams *pParams=(ShortcutParams*)param;
	HANDLE hFile=CreateFile(pParams->fname,0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	if (hFile!=INVALID_HANDLE_VALUE)
	{
		// run the shortcut wizard
		wchar_t cmdLine[1024];
		Sprintf(cmdLine,_countof(cmdLine),L"rundll32.exe appwiz.cpl,NewLinkHere %s",pParams->fname);

		STARTUPINFO startupInfo={sizeof(startupInfo)};
		PROCESS_INFORMATION processInfo;
		memset(&processInfo,0,sizeof(processInfo));
		wchar_t exe[_MAX_PATH]=L"%windir%\\system32\\rundll32.exe";
		DoEnvironmentSubst(exe,_countof(exe));
		if (CreateProcess(exe,cmdLine,NULL,NULL,FALSE,0,NULL,pParams->temp,&startupInfo,&processInfo))
		{
			CloseHandle(processInfo.hThread);
			WaitForSingleObject(processInfo.hProcess,INFINITE);
			CloseHandle(processInfo.hProcess);

			// see what the file was renamed to
			struct {
				DWORD  FileNameLength;
				wchar_t FileName[_MAX_PATH];
			} nameInfo={0};
			BOOL bInfo=GetFileInformationByHandleEx(hFile,FileNameInfo,&nameInfo,sizeof(nameInfo));
			CloseHandle(hFile);
			if (bInfo)
			{
				// move to the final target folder
				int len=Sprintf(pParams->fname,_countof(pParams->fname)-1,L"%s\\%s",pParams->temp,PathFindFileName(nameInfo.FileName));
				pParams->fname[len+1]=0;
				SHFILEOPSTRUCT shfop={g_OwnerWindow,FO_MOVE,pParams->fname,pParams->target};
				SHFileOperation(&shfop);
			}
		}
		else
			CloseHandle(hFile);
		DeleteFile(pParams->fname);
	}
	delete pParams;
	return 0;
}

static DWORD WINAPI SleepThread( void *param )
{
	SetSuspendState((intptr_t)param != 0,FALSE,FALSE);
	return 0;
}

void CMenuContainer::CloseSubMenus( int flags, CMenuContainer *pAfter )
{
	if (s_MenuMode==MODE_JUMPLIST && !(flags&CLOSE_KEEP_MODE))
	{
		if (pAfter && !pAfter->m_bSubMenu)
		{
			pAfter->SetMenuMode(MODE_NORMAL);
		}
	}
	for (int i=(int)s_Menus.size()-((flags&CLOSE_SKIP_LAST)?2:1);i>=0 && s_Menus[i]!=pAfter;i--)
		if (!s_Menus[i]->m_bDestroyed)
		{
			if ((flags&CLOSE_SKIP_SEARCH) && (s_Menus[i]->m_Options&CONTAINER_SEARCH))
				continue;
			if ((flags&CLOSE_ONLY_SEARCH) && !(s_Menus[i]->m_Options&CONTAINER_SEARCH))
				continue;
			if (flags&CLOSE_POST)
			{
				s_Menus[i]->ShowWindow(SW_HIDE);
				s_Menus[i]->PostMessage(WM_CLOSE);
				s_Menus[i]->m_bClosing=true;
			}
			else
				s_Menus[i]->DestroyWindow();
		}
}

void CMenuContainer::OpenSubMenu( int index, TActivateType type, bool bShift )
{
	const MenuItem &item=m_Items[index];
	if (m_bTwoColumns && s_bWin7Style)
	{
		if (item.bHasJumpList)
		{
			SetActiveWindow();
			CloseSubMenus(CLOSE_KEEP_MODE,this);
			OpenJumpList(index,type==ACTIVATE_OPEN_KBD);
			return;
		}
		if (item.id==MENU_SEARCH_BOX)
		{
			SetActiveWindow();
			CloseSubMenus(CLOSE_KEEP_MODE,this);
			OpenSearchList();
			return;
		}
		if (item.id==MENU_PROGRAMS && GetSettingInt(L"ProgramsStyle")==PROGRAMS_INLINE)
		{
			SetActiveWindow();
			CloseSubMenus(CLOSE_KEEP_MODE,this);
			SetMenuMode(s_MenuMode==MODE_PROGRAMS?MODE_NORMAL:MODE_PROGRAMS);
			if (s_MenuMode==MODE_NORMAL)
				SetHotItem(m_ProgramButtonIndex);
			return;
		}
	}
	// open a submenu - create a new menu object
	const StdMenuItem *pSubMenu=item.pStdItem?item.pStdItem->submenu:NULL;
	bool bOpenUp=false;

	int options=(type==ACTIVATE_OPEN_SEARCH)?CONTAINER_DRAG|CONTAINER_SEARCH:CONTAINER_DRAG|CONTAINER_DROP;
	if (item.id==MENU_CONTROLPANEL)
		options|=CONTAINER_CONTROLPANEL;
	if (item.id==MENU_DOCUMENTS)
		options|=CONTAINER_DOCUMENTS;
	if (item.id==MENU_APPS)
		options|=CONTAINER_APPS;
	if (item.bPrograms)
		options|=CONTAINER_PROGRAMS;
	if (item.bLink || (m_Options&CONTAINER_LINK))
		options|=CONTAINER_LINK;
	if ((m_Options&CONTAINER_TRACK) || item.id==MENU_PROGRAMS || item.id==MENU_APPS)
		options|=CONTAINER_TRACK;

	if (item.id==MENU_PROGRAMS && GetSettingInt(L"PinnedPrograms")==PINNED_PROGRAMS_PINNED)
		options|=CONTAINER_ALLPROGRAMS;

	if (item.id==MENU_RECENT_PROGRAMS)
		options|=CONTAINER_RECENT;

	if (m_Options&CONTAINER_OPENUP_REC)
	{
		options|=CONTAINER_OPENUP_REC;
		bOpenUp=true;
	}
	if (m_Options&CONTAINER_SORTZA_REC)
		options|=CONTAINER_SORTZA|CONTAINER_SORTZA_REC;

	if (item.pStdItem)
	{
		if (item.pStdItem->settings&StdMenuItem::MENU_OPENUP)
			bOpenUp=true;
		if (item.pStdItem->settings&StdMenuItem::MENU_OPENUP_REC)
			options|=CONTAINER_OPENUP_REC;
		if (item.pStdItem->settings&StdMenuItem::MENU_SORTZA)
			options|=CONTAINER_SORTZA;
		if (item.pStdItem->settings&StdMenuItem::MENU_SORTZA_REC)
			options|=CONTAINER_SORTZA_REC;
		if (item.pStdItem->settings&StdMenuItem::MENU_SORTONCE)
			options|=CONTAINER_SORTONCE;
		if (item.pStdItem->settings&StdMenuItem::MENU_ITEMS_FIRST)
			options|=CONTAINER_ITEMS_FIRST;
		if (item.pStdItem->settings&StdMenuItem::MENU_TRACK)
			options|=CONTAINER_TRACK;
		if (item.pStdItem->settings&StdMenuItem::MENU_NOTRACK)
			options&=~CONTAINER_TRACK;
		if (item.pStdItem->settings&StdMenuItem::MENU_MULTICOLUMN)
			options|=CONTAINER_MULTICOL_REC;
		if (item.pStdItem->settings&StdMenuItem::MENU_NOEXTENSIONS)
			options|=CONTAINER_NOEXTENSIONS;
		if (item.pStdItem->settings&StdMenuItem::MENU_SINGLE_EXPAND)
			options|=CONTAINER_NOSUBFOLDERS;
	}

	if (item.id==MENU_NETWORK)
		options|=CONTAINER_NOPATH;

	if (item.bHasJumpList)
		options=CONTAINER_JUMPLIST|CONTAINER_DRAG|CONTAINER_DROP;
	else if (item.id==MENU_COMPUTER && !s_bWin7Style)
	{
		if (GetSettingInt(L"Computer")==3)
			options|=CONTAINER_NOSUBFOLDERS;
	}
	else if (item.pItem1 && item.pItemInfo)
	{
		CString PATH;
		{
			CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
			if (item.bLink)
				PATH=item.pItemInfo->GetTargetPATH();
			if (PATH.IsEmpty())
				PATH=item.pItemInfo->PATH;
		}
		if (!PATH.IsEmpty())
		{
			for (int i=0;g_SpecialFolders[i].folder;i++)
			{
				if (PATH==g_SpecialFolders[i].PATH)
				{
					if (g_SpecialFolders[i].settings&SpecialFolder::FOLDER_NOSUBFOLDERS)
						options|=CONTAINER_NOSUBFOLDERS;
					if (g_SpecialFolders[i].settings&SpecialFolder::FOLDER_NONEWFOLDER)
						options|=CONTAINER_NONEWFOLDER;
					if (g_SpecialFolders[i].settings&SpecialFolder::FOLDER_NOPATH)
						options|=CONTAINER_NOPATH;
					if (g_SpecialFolders[i].settings&SpecialFolder::FOLDER_NODROP)
						options&=~CONTAINER_DROP;
					break;
				}
			}
		}
	}

	if (m_Options&CONTAINER_NOEXTENSIONS)
		options|=CONTAINER_NOEXTENSIONS;

	if (item.id==MENU_PROGRAMS || item.id==MENU_APPS || item.bFolder || (m_Options&CONTAINER_MULTICOL_REC))
		options|=CONTAINER_MULTICOL_REC;
	if ((options&CONTAINER_MULTICOL_REC) && !bShift)
		options|=CONTAINER_MULTICOLUMN;
	if (options&CONTAINER_SEARCH)
		options&=~(CONTAINER_MULTICOL_REC|CONTAINER_MULTICOLUMN);

	CMenuContainer *pMenu=new CSubMenuContainer(this,index,options,pSubMenu,item.pItem1,item.pItem2);
	if (type==ACTIVATE_OPEN_SEARCH)
	{
		pMenu->InitSearchItems();
	}
	else
	{
		s_JumpAppInfo=item.bHasJumpList?item.pItemInfo:NULL;
		pMenu->InitItems();
	}

	RECT itemRect;
	GetItemRect(index,itemRect);
	MapWindowPoints(NULL,&itemRect);
	RECT border={-s_Skin.Submenu_padding.left+s_Skin.Submenu_offset,-s_Skin.Submenu_padding.top,s_Skin.Submenu_padding.right-s_Skin.Submenu_offset,s_Skin.Submenu_padding.bottom};
	if (s_bRTL)
	{
		// swap and change signs
		int q=border.left; border.left=-border.right; border.right=-q;
	}
	AdjustWindowRect(&border,s_SubmenuStyle,FALSE);

	if (m_bSubMenu)
		pMenu->m_MaxWidth=s_MenuLimits.right-s_MenuLimits.left;
	else if (s_bExpandRight)
		pMenu->m_MaxWidth=s_MenuLimits.right-itemRect.right-border.left;
	else
		pMenu->m_MaxWidth=itemRect.left+border.right-s_MenuLimits.left;

	DWORD animFlags=AW_TOPMOST;
	{
		bool bDef;
		int anim=GetSettingInt(L"SubMenuAnimation",bDef);
		if (bDef)
		{
			DWORD fade;
			SystemParametersInfo(SPI_GETMENUFADE,NULL,&fade,0);
			anim=fade?1:2;
		}
		if (anim==3) animFlags|=((rand()<RAND_MAX/2)?AW_BLEND:AW_SLIDE);
		else if (anim==1) animFlags|=AW_BLEND;
		else if (anim==2) animFlags|=AW_SLIDE;
	}
	if (type!=ACTIVATE_OPEN_SEARCH)
		animFlags|=AW_ACTIVATE;

	BOOL animate;
	if ((animFlags&(AW_BLEND|AW_SLIDE))==0 || (m_Submenu>=0 && !GetSettingBool(L"SubMenuAnimationAlways")))
		animate=FALSE;
	else
		SystemParametersInfo(SPI_GETMENUANIMATION,NULL,&animate,0);

	// destroy old submenus
	SetActiveWindow();
	CloseSubMenus(CLOSE_SKIP_LAST,this);

	// open submenu
	HWND parent=GetParent();
	pMenu->Create(parent,NULL,s_SubmenuStyle,WS_EX_TOOLWINDOW|WS_EX_TOPMOST|(s_bRTL?WS_EX_LAYOUTRTL:0));

	if (GetSettingBool(L"MenuShadow") && s_Skin.Submenu_shadow==MenuSkin::SHADOW_ON)
		SetClassLongPtr(pMenu->m_hWnd,GCL_STYLE,GetClassLongPtr(pMenu->m_hWnd,GCL_STYLE)|CS_DROPSHADOW);
	else
		SetClassLongPtr(pMenu->m_hWnd,GCL_STYLE,GetClassLongPtr(pMenu->m_hWnd,GCL_STYLE)&~CS_DROPSHADOW);

	if (!parent && s_TaskBar)
	{
		// place sub-menus in front of the taskbar
		if (type==ACTIVATE_OPEN_SEARCH)
			pMenu->SetWindowPos(s_TaskBar,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE);
		else
			pMenu->SetWindowPos(s_TaskBar,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE);
	}
	RECT rc2;
	pMenu->GetWindowRect(&rc2);

	// position new menu
	int w=rc2.right-rc2.left;
	int h=rc2.bottom-rc2.top;

	if (s_bExpandRight)
	{
		if (itemRect.right+border.left+w<=s_MenuLimits.right)
		{
			// right
			rc2.left=itemRect.right+border.left;
			rc2.right=rc2.left+w;
			animFlags|=AW_HOR_POSITIVE;
			pMenu->m_Options|=CONTAINER_LEFT;
		}
		else if (itemRect.left+border.right-w>=s_MenuLimits.left)
		{
			// left
			rc2.right=itemRect.left+border.right;
			rc2.left=rc2.right-w;
			animFlags|=AW_HOR_NEGATIVE;
		}
		else
		{
			// right again
			rc2.right=s_MenuLimits.right;
			rc2.left=rc2.right-w;
			if (!s_bRTL)
			{
				int minx=m_bSubMenu?s_MenuLimits.left:(itemRect.right+border.left);
				if (rc2.left<minx)
				{
					rc2.left=minx;
					rc2.right=minx+w;
				}
			}
			animFlags|=AW_HOR_POSITIVE;
			pMenu->m_Options|=CONTAINER_LEFT;
		}
	}
	else
	{
		if (itemRect.left+border.right-w>=s_MenuLimits.left)
		{
			// left
			rc2.right=itemRect.left+border.right;
			rc2.left=rc2.right-w;
			animFlags|=AW_HOR_NEGATIVE;
		}
		else if (itemRect.right+border.left+w<=s_MenuLimits.right)
		{
			// right
			rc2.left=itemRect.right+border.left;
			rc2.right=rc2.left+w;
			animFlags|=AW_HOR_POSITIVE;
			pMenu->m_Options|=CONTAINER_LEFT;
		}
		else
		{
			// left again
			rc2.left=s_MenuLimits.left;
			rc2.right=rc2.left+w;
			if (s_bRTL)
			{
				int maxx=m_bSubMenu?s_MenuLimits.right:(itemRect.left+border.right);
				if (rc2.right>maxx)
				{
					rc2.left=maxx-w;
					rc2.right=maxx;
				}
			}
			animFlags|=AW_HOR_NEGATIVE;
		}
	}

	if (s_bRTL)
		animFlags^=(AW_HOR_POSITIVE|AW_HOR_NEGATIVE); // RTL flips the animation

	if (bOpenUp)
	{
		if (itemRect.bottom+border.bottom-h>=s_MenuLimits.top)
		{
			// up
			rc2.bottom=itemRect.bottom+border.bottom;
			rc2.top=rc2.bottom-h;
		}
		else if (itemRect.top+border.top+h<=s_MenuLimits.bottom)
		{
			// down
			rc2.top=itemRect.top+border.top;
			rc2.bottom=rc2.top+h;
			pMenu->m_Options|=CONTAINER_TOP;
		}
		else
		{
			// up again
			rc2.top=s_MenuLimits.top-pMenu->m_ExtraBorder;
			rc2.bottom=rc2.top+h;
		}
	}
	else
	{
		if (itemRect.top+border.top+h<=s_MenuLimits.bottom)
		{
			// down
			rc2.top=itemRect.top+border.top;
			rc2.bottom=rc2.top+h;
			pMenu->m_Options|=CONTAINER_TOP;
		}
		else if (itemRect.bottom+border.bottom-h>=s_MenuLimits.top)
		{
			// up
			rc2.bottom=itemRect.bottom+border.bottom;
			rc2.top=rc2.bottom-h;
		}
		else
		{
			// down again
			rc2.bottom=s_MenuLimits.bottom+pMenu->m_ExtraBorder;
			rc2.top=rc2.bottom-h;
			pMenu->m_Options|=CONTAINER_TOP;
		}
	}

	SetSubmenu(index);
	m_SubShowTime=0;
	InvalidateItem(index);
	if (type!=ACTIVATE_OPEN_SEARCH)
		SetHotItem(index);
	UpdateWindow();

	if (type!=ACTIVATE_OPEN_SEARCH)
	{
		pMenu->SetFocus();

		int hotItem=-1;
		if (type==ACTIVATE_OPEN_KBD)
		{
			bool bLast=item.id==MENU_SHUTDOWN_BUTTON && GetSettingBool(L"SelectLastShutdown");
			for (int i=0;i<(int)pMenu->m_Items.size();i++)
				if (pMenu->CanSelectItem(i))
				{
					hotItem=i;
					if (!bLast)
						break;
				}
		}
		pMenu->SetHotItem(hotItem);
	}

	int speed=0;
	if (animate)
	{
		speed=GetSettingInt(L"SubMenuAnimationSpeed");
		if (speed<=0) speed=MENU_ANIM_SPEED_SUBMENU;
		else if (speed>=10000) speed=10000;
	}
	pMenu->AnimateMenu(animFlags,speed,rc2);

	if (s_Tooltip.m_hWnd)
		s_Tooltip.SetWindowPos(HWND_TOP,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE);
}

class ExitGuard
{
public:
	ExitGuard( void ) { m_bArmed=true; }
	~ExitGuard( void ) { Assert(!m_bArmed); }
	void Disarm( void ) { m_bArmed=false; }
private:
	bool m_bArmed;
};

// Win10
MIDL_INTERFACE("833EE9A0-2999-432C-8EF2-87A8EC2D748D")
IUxUpdateManager_Win10 : public IUnknown
{
	STDMETHOD(GetUxStateVariableBOOL)(enum UxUpdateStateVariable, int*, int*);
	STDMETHOD(GetUxStateVariableDWORD)(UxUpdateStateVariable, DWORD*, int*);
	STDMETHOD(GetUxStateVariableSYSTEMTIME)(UxUpdateStateVariable, SYSTEMTIME*, int*);
	STDMETHOD(SetUxStateVariableBOOL)(UxUpdateStateVariable, int);
	STDMETHOD(SetUxStateVariableDWORD)(UxUpdateStateVariable, DWORD);
	STDMETHOD(SetUxStateVariableSYSTEMTIME)(UxUpdateStateVariable, SYSTEMTIME);
	STDMETHOD(DeleteUxStateVariable)(UxUpdateStateVariable);
	STDMETHOD(GetNextRebootTaskRunTime)(int*, SYSTEMTIME*);
	STDMETHOD(CreateRebootTasks)(const wchar_t*, SYSTEMTIME);
	STDMETHOD(CreateUpdateResultsTaskSchedule)(void);
	STDMETHOD(CreateMigrationResultsTaskSchedule)(void);
	STDMETHOD(CreateUpdateLogonNotificationTaskSchedule)(void);
	STDMETHOD(CreateUpdateNotificationTaskSchedule)(SYSTEMTIME);
	STDMETHOD(CreateLogonRebootTaskSchedule)(void);
	STDMETHOD(DidUXRebootTaskWakeUpDevice)(int*);
	STDMETHOD(RemoveUpdateResultsTaskSchedule)(void);
	STDMETHOD(RemoveLogonRebootTaskSchedule)(void);
	STDMETHOD(RemoveMigrationResultsTaskSchedule)(void);
	STDMETHOD(EnableRebootTasks)(void);
	STDMETHOD(DisableRebootTasks)(void);
	STDMETHOD(ValidateAndRecoverRebootTasks)(void);
	STDMETHOD(RebootToCompleteInstall)(DWORD, int, DWORD*, short, short, DWORD);
	STDMETHOD(IsRestartAllowed)(DWORD, int, DWORD, int*);
	STDMETHOD(GetIsWaaSOutOfDate)(DWORD, int, int, int*, DWORD*);
	STDMETHOD(GetWaaSHoursOutOfDate)(int, int, DWORD*);
	STDMETHOD(GetCachedPolicy)(DWORD, VARIANT*, DWORD*, DWORD*);
	STDMETHOD(GetEnterpriseCachedPolicy)(DWORD, VARIANT*, DWORD*, DWORD*);
	STDMETHOD(GetCachedSettingValue)(DWORD, short, VARIANT*);
	STDMETHOD(GetOptInToMU)(int*);
	STDMETHOD(SetOptInToMU)(int);
	STDMETHOD(SetAndModifyShutdownFlags)(DWORD, DWORD*);
	STDMETHOD(GetIsFlightingEnabled)(int*);
	STDMETHOD(GetIsCTA)(int*);
	STDMETHOD(NotifyStateVariableChange)(void);
	STDMETHOD(GetAlwaysAllowMeteredNetwork)(int*);
};

// Win11
MIDL_INTERFACE("B96BA95F-9479-4656-B7A1-6F3A69091910")
IUxUpdateManager_Win11 : public IUnknown
{
	STDMETHOD(GetUxStateVariableBOOL)(enum UxUpdateStateVariable, int*, int*);
	STDMETHOD(GetUxStateVariableDWORD)(UxUpdateStateVariable, DWORD*, int*);
	STDMETHOD(GetUxStateVariableSYSTEMTIME)(UxUpdateStateVariable, SYSTEMTIME*, int*);
	STDMETHOD(SetUxStateVariableBOOL)(UxUpdateStateVariable, int);
	STDMETHOD(SetUxStateVariableDWORD)(UxUpdateStateVariable, DWORD);
	STDMETHOD(SetUxStateVariableSYSTEMTIME)(UxUpdateStateVariable, SYSTEMTIME);
	STDMETHOD(DeleteUxStateVariable)(UxUpdateStateVariable);
	STDMETHOD(GetNextScheduledRebootTaskRunTime)(SYSTEMTIME*);
	STDMETHOD(GetIsRebootTaskScheduledToRun)(int*);
	STDMETHOD(CreateRebootTasks)(const wchar_t*, SYSTEMTIME);
	STDMETHOD(CreateUpdateResultsTaskSchedule)(void);
	STDMETHOD(CreateMigrationResultsTaskSchedule)(void);
	STDMETHOD(CreateUpdateLogonNotificationTaskSchedule)(void);
	STDMETHOD(CreateUpdateNotificationTaskSchedule)(SYSTEMTIME);
	STDMETHOD(CreateLogonRebootTaskSchedule)(void);
	STDMETHOD(DidUXRebootTaskWakeUpDevice)(int*);
	STDMETHOD(RemoveUpdateResultsTaskSchedule)(void);
	STDMETHOD(RemoveLogonRebootTaskSchedule)(void);
	STDMETHOD(RemoveMigrationResultsTaskSchedule)(void);
	STDMETHOD(EnableRebootTasks)(void);
	STDMETHOD(DisableRebootTasks)(void);
	STDMETHOD(ValidateAndRecoverRebootTasks)(void);
	STDMETHOD(RebootToCompleteInstall)(DWORD, int, DWORD*, int, int, double);
	STDMETHOD(IsRestartAllowed)(DWORD, int, double, int*);
	STDMETHOD(GetIsWaaSOutOfDate)(DWORD, int, int, int*, DWORD*);
	STDMETHOD(GetWaaSHoursOutOfDate)(int, int, DWORD*);
	STDMETHOD(GetDeviceEndOfServiceDate)(int, int*, FILETIME*);
	STDMETHOD(GetCachedPolicy)(DWORD, VARIANT*, DWORD*, DWORD*);
	STDMETHOD(GetEnterpriseCachedPolicy)(DWORD, VARIANT*, DWORD*, DWORD*);
	STDMETHOD(GetOptInToMU)(int*);
	STDMETHOD(SetOptInToMU)(int);
	STDMETHOD(SetAndModifyShutdownFlags)(DWORD, DWORD*);
	STDMETHOD(GetIsFlightingEnabled)(int*);
	STDMETHOD(GetIsCTA)(int*);
	STDMETHOD(NotifyStateVariableChange)(void);
	STDMETHOD(GetAlwaysAllowMeteredNetwork)(int*);
	STDMETHOD(SetInstallAtShutdown)(int);
	STDMETHOD(GetUxStateVariableValueOrDefaultBOOL)(UxUpdateStateVariable, int, int*);
	STDMETHOD(GetUxStateVariableValueOrDefaultDWORD)(UxUpdateStateVariable, DWORD, DWORD*);
	STDMETHOD(GetUxStateVariableValueOrDefaultSYSTEMTIME)(UxUpdateStateVariable, SYSTEMTIME, SYSTEMTIME*);
	STDMETHOD(GetSuggestedRebootTime)(int, SYSTEMTIME, SYSTEMTIME*, int*);
	STDMETHOD(GetSuggestedActiveHours)(DWORD, DWORD*, DWORD*, int*);
	STDMETHOD(GetIsIntervalAcceptableForActiveHours)(SYSTEMTIME, SYSTEMTIME, int*);
	STDMETHOD(GetSmartScheduledPredictionsAccurate)(int*);
	STDMETHOD(EvaluateAndStoreRebootDowntimePrediction)(void);
	STDMETHOD(GetCachedRebootDowntimePrediction)(DWORD*);
	STDMETHOD(GetAlwaysAllowCTADownload)(int*);
};

MIDL_INTERFACE("07F3AFAC-7C8A-4CE7-A5E0-3D24EE8A77E0")
IUpdateSessionOrchestrator : public IUnknown
{
	STDMETHOD(CreateUpdateSession)(enum UpdateSessionType, const GUID&, void**);
	STDMETHOD(GetCurrentActiveUpdateSessions)(class IUsoSessionCollection**);
	STDMETHOD(LogTaskRunning)(const wchar_t*);
	STDMETHOD(CreateUxUpdateManager)(IUnknown**);
};

DWORD WindowsUpdateAdjustShutdownFlags(DWORD flags)
{
	DWORD retval = flags;

	{
		// "EnhancedShutdownEnabled" value must exist if Windows updates are prepared
		// otherwise there is no need to do anything

		CRegKey key;
		if (key.Open(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WindowsUpdate\\Orchestrator", KEY_READ) != ERROR_SUCCESS)
			return retval;

		DWORD value;
		if (key.QueryDWORDValue(L"EnhancedShutdownEnabled", value) != ERROR_SUCCESS)
			return retval;
	}

	// this is what standard Windows shutdown handling does inside shutdownux!UsoCommitHelper::SetAndModifyShutdownFlags

	static const GUID CLSID_UpdateSessionOrchestrator = { 0xb91d5831,0xb1bd,0x4608,{0x81,0x98,0xd7,0x2e,0x15,0x50,0x20,0xf7} };

	CComPtr<IUpdateSessionOrchestrator> updateSessionOrchestrator;
	if (SUCCEEDED(updateSessionOrchestrator.CoCreateInstance(CLSID_UpdateSessionOrchestrator, nullptr, CLSCTX_LOCAL_SERVER)))
	{
		CComPtr<IUnknown> mgr;
		if (SUCCEEDED(updateSessionOrchestrator->CreateUxUpdateManager(&mgr)))
		{
			// call to IUxUpdateManager::SetAndModifyShutdownFlags will ensure that Windows updates will be dismissed if there is no `SHUTDOWN_INSTALL_UPDATES` flag provided
			// it also provides recommended shutdown flags in some cases (so we will use them as advised)
			//
			// the method is implemented by `UxUpdateManager::SetAndModifyShutdownFlags` in `usosvc.dll` (Win10) / `usosvcimpl.dll` (Win11)

			if (CComPtr<IUxUpdateManager_Win10> updateManager; SUCCEEDED(mgr.QueryInterface(&updateManager)))
			{
				DWORD newFlags;
				if (SUCCEEDED(updateManager->SetAndModifyShutdownFlags(flags, &newFlags)))
					retval = newFlags;
			}
			else if (CComPtr<IUxUpdateManager_Win11> updateManager; SUCCEEDED(mgr.QueryInterface(&updateManager)))
			{
				DWORD newFlags;
				if (SUCCEEDED(updateManager->SetAndModifyShutdownFlags(flags, &newFlags)))
					retval = newFlags;
			}
		}
	}

	return retval;
}

static TOKEN_ELEVATION_TYPE GetCurrentTokenElevationType()
{
	TOKEN_ELEVATION_TYPE retval = TokenElevationTypeDefault;

	HANDLE token;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
	{
		TOKEN_ELEVATION_TYPE elevationType;
		DWORD returnLength;
		if (GetTokenInformation(token, TokenElevationType, &elevationType, sizeof(elevationType), &returnLength) && returnLength == sizeof(elevationType))
			retval = elevationType;

		CloseHandle(token);
	}

	return retval;
}

static BOOL WINAPI WinStationGetLoggedOnCount(ULONG* pUserSessions, ULONG* pDeviceSessions)
{
	static auto p = static_cast<decltype(&WinStationGetLoggedOnCount)>((void*)GetProcAddress(GetModuleHandle(L"winsta.dll"), "WinStationGetLoggedOnCount"));
	if (p)
		return p(pUserSessions, pDeviceSessions);

	// fall-back
	return FALSE;
}

static bool ProceedWithShutdown(DWORD flags)
{
	// this logic is inspired by user32!DisplayExitWindowsWarnings function (called from ExitWindowsEx)

	ULONG userSessions = 0;
	ULONG deviceSessions = 0;

	WinStationGetLoggedOnCount(&userSessions, &deviceSessions);

	// we can proceed if there is at most one user session and no device sessions
	if (userSessions <= 1 && deviceSessions == 0)
		return true;

	// otherwise inform user that somebody else is using the machine and ask for confirmation

	UINT msgId = 0;

	if (flags & SHUTDOWN_RESTART)
	{
		if (userSessions <= 1)
			msgId = 755;					// One or more devices on your network are using the computer resources. Restarting Windows might cause them to lose data.
		else if (deviceSessions != 0)
			msgId = 756;					// Other people and devices are using the computer resources. Restarting Windows might cause them to lose data.
		else
			msgId = 714;					// Other people are logged on to this computer. Restarting Windows might cause them to lose data.
	}
	else
	{
		if (userSessions <= 1)
			msgId = 753;					// One or more devices on your network are using the computer resources.Shutting down Windows might cause them to lose data.
		else if (deviceSessions != 0)
			msgId = 754;					// Other people and devices are are using the computer resources. Shutting down Windows might cause them to lose data.
		else
			msgId = 713;					// Other people are logged on to this computer. Shutting down Windows might cause them to lose data.
	}

	WCHAR message[MAX_PATH]{};
	LoadString(GetModuleHandle(L"user32.dll"), msgId, message, _countof(message));

	return MessageBox(NULL, message, L"Open-Shell", MB_YESNO | MB_ICONEXCLAMATION | MB_DEFBUTTON1 | MB_SYSTEMMODAL | MB_SETFOREGROUND | MB_SERVICE_NOTIFICATION) != IDNO;
}

static bool ExecuteShutdownCommand(TMenuID menuCommand)
{
	DWORD flags = 0;

	switch (menuCommand)
	{
	case MENU_RESTART: // restart
	case MENU_RESTART_NOUPDATE:
	case MENU_RESTART_UPDATE: // update and restart
	case MENU_RESTART_ADVANCED: // advanced restart
		flags = SHUTDOWN_RESTART;

		if (menuCommand == MENU_RESTART_UPDATE)
			flags |= SHUTDOWN_INSTALL_UPDATES;

		if (menuCommand == MENU_RESTART_ADVANCED)
			flags |= SHUTDOWN_RESTART_BOOTOPTIONS;

		break;

	case MENU_SHUTDOWN: // shutdown
	case MENU_SHUTDOWN_NOUPDATE:
	case MENU_SHUTDOWN_UPDATE: // update and shutdown
	case MENU_SHUTDOWN_HYBRID: // hybrid shutdown
		flags = SHUTDOWN_POWEROFF;

		if (menuCommand == MENU_SHUTDOWN_UPDATE)
			flags |= SHUTDOWN_INSTALL_UPDATES;

		if (menuCommand == MENU_SHUTDOWN_HYBRID)
		{
			CRegKey regPower;
			if (regPower.Open(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power", KEY_READ) == ERROR_SUCCESS)
			{
				DWORD val = 0;
				if (regPower.QueryDWORDValue(L"HiberbootEnabled", val) == ERROR_SUCCESS && val == 1)
					flags |= SHUTDOWN_HYBRID;
			}
		}
		break;
	}

	if (flags)
	{
		if (!ProceedWithShutdown(flags))
			return true;

		flags |= SHUTDOWN_FORCE_OTHERS;

		if (SetShutdownPrivileges())
		{
			flags = WindowsUpdateAdjustShutdownFlags(flags);
			InitiateShutdown(NULL, NULL, 0, flags, SHTDN_REASON_FLAG_PLANNED);
		}
		else
		{
			// we don't have shutdown rights
			// lets try silent elevate via SystemSettingsAdminFlows (for limited admin users only)
			if (GetCurrentTokenElevationType() == TokenElevationTypeLimited)
			{
				flags = WindowsUpdateAdjustShutdownFlags(flags);

				wchar_t cmdLine[32]{};
				Sprintf(cmdLine, _countof(cmdLine), L"Shutdown %d %d", flags, SHTDN_REASON_FLAG_PLANNED);

				SHELLEXECUTEINFO sei{};
				sei.cbSize = sizeof(sei);
				sei.lpFile = L"%systemroot%\\system32\\SystemSettingsAdminFlows.exe";
				sei.lpParameters = cmdLine;
				sei.lpVerb = L"runas";
				sei.fMask = SEE_MASK_DOENVSUBST | SEE_MASK_FLAG_NO_UI;

				ShellExecuteEx(&sei);
			}
		}

		return true;
	}

	return false;
}

NTSTATUS
NTAPI
NtPowerInformation(
	_In_ POWER_INFORMATION_LEVEL InformationLevel,
	_In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
	_In_ ULONG InputBufferLength,
	_Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
	_In_ ULONG OutputBufferLength
);

static bool ConnectedStandby()
{
	SYSTEM_POWER_CAPABILITIES powerCaps{};
	GetPwrCapabilities(&powerCaps);

	if (powerCaps.AoAc)
	{
		static auto pNtPowerInformation = static_cast<decltype(&NtPowerInformation)>((void*)GetProcAddress(GetModuleHandle(L"ntdll.dll"), "NtPowerInformation"));
		if (pNtPowerInformation)
			pNtPowerInformation(ScreenOff, NULL, 0, NULL, 0);

		return true;
	}

	return false;
}

static bool ExecuteSysCommand( TMenuID menuCommand )
{
	CComPtr<IShellDispatch2> pShellDisp;
	HRESULT hr;
	switch (menuCommand)
	{
		case MENU_TASKBAR: // show taskbar properties
			hr=CoCreateInstance(CLSID_Shell,NULL,CLSCTX_SERVER,IID_IShellDispatch2,(void**)&pShellDisp);
			if (SUCCEEDED(hr))
			{
				hr=pShellDisp->TrayProperties();
				if (FAILED(hr))
					LOG_MENU(LOG_EXECUTE,L"Failed to TrayProperties, 0x08%x",hr);
			}
			else
				LOG_MENU(LOG_EXECUTE,L"Failed to create dispatch, 0x08%x",hr);
			return true;

		case MENU_FEATURES:
			hr=CoCreateInstance(CLSID_Shell,NULL,CLSCTX_SERVER,IID_IShellDispatch2,(void**)&pShellDisp);
			if (SUCCEEDED(hr))
			{
				hr=pShellDisp->ControlPanelItem(CComBSTR(L"appwiz.cpl"));
				if (FAILED(hr))
					LOG_MENU(LOG_EXECUTE,L"Failed to ControlPanelItem(appwiz.cpl), 0x08%x",hr);
			}
			else
				LOG_MENU(LOG_EXECUTE,L"Failed to create dispatch, 0x08%x",hr);
			return true;

		case MENU_SECURITY:
			{
				CComPtr<IShellDispatch4> pShellDisp4;
				hr=CoCreateInstance(CLSID_Shell,NULL,CLSCTX_SERVER,IID_IShellDispatch4,(void**)&pShellDisp4);
				if (SUCCEEDED(hr))
				{
					hr=pShellDisp4->WindowsSecurity();
					if (FAILED(hr))
						LOG_MENU(LOG_EXECUTE,L"Failed to WindowsSecurity, 0x08%x",hr);
				}
				else
					LOG_MENU(LOG_EXECUTE,L"Failed to create dispatch, 0x08%x",hr);
			}
			return true;

		case MENU_SEARCH_FILES: // show the search UI
			hr=CoCreateInstance(CLSID_Shell,NULL,CLSCTX_SERVER,IID_IShellDispatch2,(void**)&pShellDisp);
			if (SUCCEEDED(hr))
			{
				hr=pShellDisp->FindFiles();
				if (FAILED(hr))
					LOG_MENU(LOG_EXECUTE,L"Failed to FindFiles, 0x08%x",hr);
			}
			else
				LOG_MENU(LOG_EXECUTE,L"Failed to create dispatch, 0x08%x",hr);
			return true;

		case MENU_SEARCH_PRINTER: // search for network printers
			hr=CoCreateInstance(CLSID_Shell,NULL,CLSCTX_SERVER,IID_IShellDispatch2,(void**)&pShellDisp);
			if (SUCCEEDED(hr))
			{
				hr=pShellDisp->FindPrinter(CComBSTR(L""),CComBSTR(L""),CComBSTR(L""));
				if (FAILED(hr))
					LOG_MENU(LOG_EXECUTE,L"Failed to FindPrinter, 0x08%x",hr);
			}
			else
				LOG_MENU(LOG_EXECUTE,L"Failed to create dispatch, 0x08%x",hr);
			return true;

		case MENU_SEARCH_COMPUTERS: // search for computers
			hr=CoCreateInstance(CLSID_Shell,NULL,CLSCTX_SERVER,IID_IShellDispatch2,(void**)&pShellDisp);
			if (SUCCEEDED(hr))
			{
				hr=pShellDisp->FindComputer();
				if (FAILED(hr))
					LOG_MENU(LOG_EXECUTE,L"Failed to FindComputer, 0x08%x",hr);
			}
			else
				LOG_MENU(LOG_EXECUTE,L"Failed to create dispatch, 0x08%x",hr);
			return true;

		case MENU_SEARCH_PEOPLE: // search for people using Windows Mail
			{
				SHELLEXECUTEINFO execute={sizeof(execute),SEE_MASK_DOENVSUBST,NULL,L"open"};
				execute.lpFile=L"%ProgramFiles%\\Windows Mail\\wab.exe";
				execute.lpParameters=L"/find";
				execute.lpDirectory=L"%ProgramFiles%\\Windows Mail";
				execute.nShow=SW_SHOWNORMAL;
				ShellExecuteEx(&execute);
			}
			return true;

		case MENU_HELP: // show Windows help
			hr=CoCreateInstance(CLSID_Shell,NULL,CLSCTX_SERVER,IID_IShellDispatch2,(void**)&pShellDisp);
			if (SUCCEEDED(hr))
			{
				hr=pShellDisp->Help();
				if (FAILED(hr))
					LOG_MENU(LOG_EXECUTE,L"Failed to Help, 0x08%x",hr);
			}
			else
				LOG_MENU(LOG_EXECUTE,L"Failed to create dispatch, 0x08%x",hr);
			return true;

		case MENU_RUN: // show the Run box
			if (GetWinVersion()>=WIN_VER_WIN10)
			{
				ShellExecute(NULL,NULL,L"shell:::{2559a1f3-21d7-11d4-bdaf-00c04f60b9f0}",NULL,NULL,SW_SHOWNORMAL);
			}
			else
			{
				hr=CoCreateInstance(CLSID_Shell,NULL,CLSCTX_SERVER,IID_IShellDispatch2,(void**)&pShellDisp);
				if (SUCCEEDED(hr))
				{
					hr=pShellDisp->FileRun();
					if (FAILED(hr))
						LOG_MENU(LOG_EXECUTE,L"Failed to FileRun, 0x08%x",hr);
				}
				else
					LOG_MENU(LOG_EXECUTE,L"Failed to create dispatch, 0x08%x",hr);
			}
			return true;

		case MENU_LOGOFF: // log off
			ExitWindowsEx(EWX_LOGOFF,0);
			return true;

		case MENU_LOGOFF_CONFIRM:
			{
				HMODULE hShell32=GetModuleHandle(L"Shell32.dll");
				HICON icon=LoadIcon(hShell32,MAKEINTRESOURCE(45));
				INT_PTR res=DialogBoxParam(g_Instance,MAKEINTRESOURCE(IsLanguageRTL()?IDD_LOGOFFR:IDD_LOGOFF),NULL,LogOffDlgProc,(LPARAM)icon);
				DestroyIcon(icon);
				if (res)
					ExitWindowsEx(EWX_LOGOFF,0);
			}
			return true;

		case MENU_SWITCHUSER: // switch_user
			if (GetWinVersion()>=WIN_VER_WIN10)
			{
				// on Windows 10 this value must be set to 1. For some reason non-admin code has permissions to do so
				CRegKey regSwitch;
				if (regSwitch.Create(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Authentication\\LogonUI\\UserSwitch",NULL,0,KEY_SET_VALUE)==ERROR_SUCCESS)
					regSwitch.SetDWORDValue(L"Enabled",1);
			}
			WTSDisconnectSession(WTS_CURRENT_SERVER_HANDLE,WTS_CURRENT_SESSION,FALSE); // same as "disconnect"
			return true;

		case MENU_LOCK: // lock
			LockWorkStation();
			return true;

		case MENU_SLEEP:
			if (GetSystemMetrics(SM_REMOTESESSION))
			{
				WTSDisconnectSession(WTS_CURRENT_SERVER_HANDLE,WTS_CURRENT_SESSION,FALSE);
				Sleep(250);
			}
			if (!ConnectedStandby())
				CreateThread(NULL,0,SleepThread,(void*)FALSE,0,NULL);
			return true;

		case MENU_HIBERNATE:
			if (GetSystemMetrics(SM_REMOTESESSION))
			{
				WTSDisconnectSession(WTS_CURRENT_SERVER_HANDLE,WTS_CURRENT_SESSION,FALSE);
				Sleep(250);
			}
			CreateThread(NULL,0,SleepThread,(void*)TRUE,0,NULL);
			return true;

		case MENU_DISCONNECT: // disconnect the current Terminal Services session (remote desktop)
			WTSDisconnectSession(WTS_CURRENT_SERVER_HANDLE,WTS_CURRENT_SESSION,FALSE);
			return true;

		case MENU_UNDOCK: // undock the PC
			hr=CoCreateInstance(CLSID_Shell,NULL,CLSCTX_SERVER,IID_IShellDispatch2,(void**)&pShellDisp);
			if (SUCCEEDED(hr))
			{
				hr=pShellDisp->EjectPC();
				if (FAILED(hr))
					LOG_MENU(LOG_EXECUTE,L"Failed to EjectPC, 0x08%x",hr);
			}
			else
				LOG_MENU(LOG_EXECUTE,L"Failed to create dispatch, 0x08%x",hr);
			return true;

		case MENU_MONITOROFF:
			::SendMessage(g_TaskBar,WM_SYSCOMMAND,SC_MONITORPOWER,2);
			return true;

		case MENU_SHUTDOWN_BOX: // shutdown - ask to shutdown, log off, sleep, etc
			hr=CoCreateInstance(CLSID_Shell,NULL,CLSCTX_SERVER,IID_IShellDispatch2,(void**)&pShellDisp);
			if (SUCCEEDED(hr))
			{
				hr=pShellDisp->ShutdownWindows();
				if (FAILED(hr))
					LOG_MENU(LOG_EXECUTE,L"Failed to ShutdownWindows, 0x08%x",hr);
			}
			else
				LOG_MENU(LOG_EXECUTE,L"Failed to create dispatch, 0x08%x",hr);
			return true;

		case MENU_PCSETTINGS:
			if (GetWinVersion()>=WIN_VER_WIN8)
			{
				ShellExecute(NULL,NULL,L"shell:appsfolder\\windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel",NULL,NULL,SW_SHOWNORMAL);
			}
			return true;

		default:
			if (ExecuteShutdownCommand(menuCommand))
				return true;
			return false;
	}
}

STARTMENUAPI bool DllExecuteNamedCommand( const wchar_t *command )
{
	static struct NamedCommand
	{
		const wchar_t *name;
		TMenuID command;
	} s_NamedCommands[]=
	{
		{L"help",MENU_HELP},
		{L"run",MENU_RUN},
		{L"logoff",MENU_LOGOFF},
		{L"undock",MENU_UNDOCK},
		{L"monitor_off",MENU_MONITOROFF},
		{L"disconnect",MENU_DISCONNECT},
		{L"shutdown_box",MENU_SHUTDOWN_BOX},
		{L"windows_security",MENU_SECURITY},
		{L"taskbar_settings",MENU_TASKBAR},
		{L"search_files",MENU_SEARCH_FILES},
		{L"search_printer",MENU_SEARCH_PRINTER},
		{L"search_computers",MENU_SEARCH_COMPUTERS},
		{L"search_people",MENU_SEARCH_PEOPLE},
		{L"sleep",MENU_SLEEP},
		{L"hibernate",MENU_HIBERNATE},
		{L"restart",MENU_RESTART},
		{L"shutdown",MENU_SHUTDOWN},
		{L"switch_user",MENU_SWITCHUSER},
		{L"lock",MENU_LOCK},
		{L"programs_features",MENU_FEATURES},

		{L"confirm_logoff",MENU_LOGOFF_CONFIRM},
		{L"advanced_boot",MENU_RESTART_ADVANCED},
		{L"update_restart",MENU_RESTART_UPDATE},
		{L"update_shutdown",MENU_SHUTDOWN_UPDATE},
		{L"hybrid_shutdown",MENU_SHUTDOWN_HYBRID},
	};

	TMenuID menuCommand=MENU_NO;
	for (int i=0;i<_countof(s_NamedCommands);i++)
	{
		if (wcscmp(command,s_NamedCommands[i].name)==0)
		{
			menuCommand=s_NamedCommands[i].command;
			break;
		}
	}

	switch (menuCommand)
	{
		case MENU_NO:
			return false;

		case MENU_HELP:
			if (SHRestricted(REST_NOSMHELP)) return false;
			break;

		case MENU_RUN:
			if (SHRestricted(REST_NORUN)) return false;
			break;

		case MENU_LOGOFF:
		case MENU_LOGOFF_CONFIRM:
			if (SHRestricted(REST_STARTMENULOGOFF)==1) return false;
			break;

		case MENU_DISCONNECT:
			if (SHRestricted(REST_NODISCONNECT)) return false;
			break;

		case MENU_UNDOCK:
			if (SHRestricted(REST_NOSMEJECTPC)) return false;
			break;

		case MENU_SEARCH_COMPUTERS:
			if (SHRestricted(REST_HASFINDCOMPUTERS)) return false;
			break;

		case MENU_SWITCHUSER:
			{
				CComPtr<IShellDispatch2> pShellDisp;
				if (SUCCEEDED(CoCreateInstance(CLSID_Shell,NULL,CLSCTX_SERVER,IID_IShellDispatch2,(void**)&pShellDisp)))
				{
					long val;
					if (SUCCEEDED(pShellDisp->IsRestricted(CComBSTR(L"System"),CComBSTR(L"HideFastUserSwitching"),&val)) && val)
						return false;
				}
			}
			break;

		case MENU_TASKBAR:
			if (SHRestricted(REST_NOSETTASKBAR)) return false;
			break;

		case MENU_FEATURES:
			if (SHRestricted(REST_NOSETFOLDERS) || SHRestricted(REST_NOCONTROLPANEL)) return false;
			break;

		case MENU_RESTART:
		case MENU_SHUTDOWN:
		case MENU_RESTART_ADVANCED:
		case MENU_RESTART_UPDATE:
		case MENU_SHUTDOWN_UPDATE:
		case MENU_SHUTDOWN_HYBRID:
			if (SHRestricted(REST_NOCLOSE)) return false;
			break;
	}
	
	ExecuteSysCommand(menuCommand);
	return true;
}

static HRESULT CreatePinLink( PCIDLIST_ABSOLUTE sourcePidl, const wchar_t *name, const wchar_t *iconPath, int iconIndex )
{
	wchar_t path[_MAX_PATH];
	Sprintf(path,_countof(path),L"%s\\%s.lnk",CMenuContainer::s_PinFolder,name);
	wchar_t finalPath[_MAX_PATH];
	PathYetAnotherMakeUniqueName(finalPath,path,NULL,PathFindFileName(path));

	HRESULT hr;
	{
		CComPtr<IShellLink> pLink;
		hr=pLink.CoCreateInstance(CLSID_ShellLink);
		if (FAILED(hr)) return hr;

		hr=pLink->SetIDList(sourcePidl);
		if (FAILED(hr)) return hr;

		if (iconPath)
		{
			hr=pLink->SetIconLocation(iconPath,iconIndex);
			if (FAILED(hr)) return hr;
		}

		CComQIPtr<IPersistFile> pFile(pLink);
		if (!pFile) return E_FAIL;
		hr=pFile->Save(finalPath,TRUE);
	}
	{
		// reopen the link and set the "no new" property. without reopening the original properties are lost
		CComPtr<IShellLink> pLink;
		hr=pLink.CoCreateInstance(CLSID_ShellLink);
		CComQIPtr<IPersistFile> pFile(pLink);
		hr=pFile->Load(finalPath,STGM_READWRITE);
		CComQIPtr<IPropertyStore> pStore(pLink);
		if (pStore)
		{
			PROPVARIANT val;
			InitPropVariantFromBoolean(TRUE,&val);
			pStore->SetValue(PKEY_AppUserModel_ExcludeFromShowInNewInstall,val);
			PropVariantClear(&val);
			pStore->Commit();
		}
		hr=pFile->Save(finalPath,TRUE);
	}
	HANDLE h=CreateFile(finalPath,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	if (h!=INVALID_HANDLE_VALUE)
	{
		FILETIME curTime;
		GetSystemTimeAsFileTime(&curTime);
		SetFileTime(h,&curTime,&curTime,&curTime);
		CloseHandle(h);
	}
	return S_OK;
}

// This function "activates" an item. The item can be activated in multiple ways:
// ACTIVATE_SELECT - select the item, make sure it is visible
// ACTIVATE_OPEN - if the item is a submenu, it is opened. otherwise the item is just selected (but all submenus are closed first)
// ACTIVATE_OPEN_KBD - same as above, but when done with a keyboard
// ACTIVATE_OPEN_SEARCH - opens the search results submenu
// ACTIVATE_EXECUTE - executes the item. it can be a shell item or a command item
// ACTIVATE_MENU - shows the context menu for the item
// ACTIVATE_RENAME - renames the item
// ACTIVATE_DELETE - deletes the item
// ACTIVATE_PROPERTIES - shows the properties of the item
void CMenuContainer::ActivateItem( int index, TActivateType type, const POINT *pPt, ActivateData *pData )
{
	LOG_MENU(LOG_EXECUTE,L"Activate Item, ptr=%p, index=%d, type=%d",this,index,type);
	if (index<0)
	{
		if (type==ACTIVATE_SELECT)
		{
			if (!(m_Options&CONTAINER_SEARCH))
				SetFocus();
			SetHotItem(-1);
			return;
		}
		else if (type==ACTIVATE_MENU)
		{
			index=0;
			type=ACTIVATE_MENU_BACKGROUND;
		}
		else
			return;
	}

	// make a const copy of the item and use it instead. the m_Items array can be reallocated at any time
	const MenuItem item=m_Items[index];
	CAbsolutePidl pItemPidl1, pItemPidl2;
	pItemPidl1.Clone(item.pItem1);
	pItemPidl2.Clone(item.pItem2);
	((MenuItem&)item).pItem1=NULL; // hack to ensure the pidls are not used anywhere here
	((MenuItem&)item).pItem2=NULL;

	if (type==ACTIVATE_SELECT)
	{
		// set the hot item
		if (item.id==MENU_SEARCH_BOX)
		{
			m_SearchBox.SetFocus();
			SetHotItem(-1);
		}
		else
		{
			if (item.id==MENU_PROGRAMS_TREE && m_pProgramsTree && m_pProgramsTree->m_hWnd)
				m_pProgramsTree->SetFocus();
			else if (!(m_Options&CONTAINER_SEARCH) && (m_bSubMenu || s_MenuMode!=MODE_SEARCH))
				SetFocus();
			SetHotItem(index,false,true);
		}
		if (m_ScrollHeight>0 && index<m_ScrollCount)
		{
			// scroll the pager to make this item visible
			RECT rc=item.itemRect;
			OffsetRect(&rc,0,-m_rContent.top);
			int pos=m_ScrollOffset;
			int total=m_Items[m_ScrollCount-1].itemRect.bottom-m_rContent.top-m_ScrollHeight;
			if (pos>rc.top-m_ScrollButtonSize)
				pos=rc.top-m_ScrollButtonSize;
			else if (pos<rc.bottom+m_ScrollButtonSize-m_ScrollHeight)
				pos=rc.bottom+m_ScrollButtonSize-m_ScrollHeight;
			if (pos<0) pos=0;
			if (pos>total) pos=total;
			if (m_ScrollOffset!=pos)
			{
				m_ScrollOffset=pos;
				UpdateScroll();
				Invalidate();
			}
		}
		if (m_SearchScrollCount>m_SearchScrollHeight && index>=m_OriginalCount)
		{
			// scroll the search results to make this item visible
			int idx=index-m_OriginalCount;
			int pos=m_SearchScrollPos;
			if (pos>idx) pos=idx;
			if (pos+m_SearchScrollHeight<idx+1)
				pos=idx-m_SearchScrollHeight+1;
			if (m_SearchScrollPos!=pos)
			{
				m_SearchScrollPos=m_Scrollbar.SetScrollPos(SB_CTL,pos);
				InvalidateRect(&m_rContent);
			}
		}
		return;
	}

	bool bShift=(!pData || !pData->bNoModifiers) && GetKeyState(VK_SHIFT)<0;
	bool bCtrl=(!pData || !pData->bNoModifiers) && GetKeyState(VK_CONTROL)<0;

	if (type==ACTIVATE_OPEN || type==ACTIVATE_OPEN_KBD || type==ACTIVATE_OPEN_SEARCH)
	{
		if (item.id==MENU_SEARCH_BOX && type!=ACTIVATE_OPEN_SEARCH)
			return;
		s_HotPos=GetMessagePos();
		if (!item.bFolder && item.id!=MENU_SEARCH_BOX)
		{
			SetActiveWindow();
			// destroy old submenus
			CloseSubMenus(0,this);

			// just select the item
			ActivateItem(index,ACTIVATE_SELECT,NULL);
			return;
		}

		OpenSubMenu(index,type,bShift);
		return;
	}

	bool bKeepOpen=(type==ACTIVATE_EXECUTE) && bShift && !bCtrl && (!item.bMetroLink || GetWinVersion()>=WIN_VER_WIN10);
	bool bTrackRecent=false;
	if (s_RecentPrograms!=RECENT_PROGRAMS_NONE)
	{
		if (item.id==MENU_RECENT)
			bTrackRecent=true;
		else if (!m_bSubMenu || (m_Options&CONTAINER_TRACK))
			bTrackRecent=item.id==MENU_NO && (!item.bFolder || item.bHasJumpList) && !item.pStdItem && (item.categoryHash&15)<=CSearchManager::CATEGORY_SETTING;
	}

	CString searchText;
	for (CMenuContainer *pSearchMenu=this;pSearchMenu;pSearchMenu=pSearchMenu->m_pParent)
		if (pSearchMenu->m_SearchBox.m_hWnd)
		{
			pSearchMenu->m_SearchBox.GetWindowText(searchText);
			break;
		}

	if (type==ACTIVATE_EXECUTE)
	{
		if (item.id==MENU_EMPTY || item.id==MENU_EMPTY_TOP) return;
		if (item.bFolder && pItemPidl1 && !item.bSplit && !GetSettingBool(L"EnableExplorer"))
				return;
		if (item.id==MENU_SEARCH_BOX)
		{
			// the search button was pressed
			m_SearchBox.SetFocus();
			CloseSubMenus(CLOSE_POST,this);
			m_SearchBox.SetWindowText(L"");
			return;
		}
		if (item.id==MENU_SEARCH_CATEGORY)
		{
			if (bCtrl || (pData && pData->bArrow))
			{
				for (std::list<CSearchManager::SearchCategory>::const_iterator it=s_SearchResults.indexed.begin();it!=s_SearchResults.indexed.end();++it)
				{
					if (item.categoryHash==it->categoryHash)
					{
						if (bKeepOpen)
							LockSetForegroundWindow(LSFW_LOCK);
						else
						{
							LockSetForegroundWindow(LSFW_UNLOCK);
							FadeOutItem(index);
							CloseSubMenus(CLOSE_POST,NULL);
						}
						PlayMenuSound(SOUND_COMMAND);
						g_SearchManager.LaunchExternalSearch(it->search,it->categoryHash,searchText);
						break;
					}
				}
			}
			else
			{
				m_SearchCategoryHash=(m_SearchCategoryHash==item.categoryHash)?CSearchManager::CATEGORY_INVALID:item.categoryHash;
				RefreshSearch();
			}
			return;
		}

		if (item.jumpIndex>=0)
		{
			if (item.id==MENU_NO)
			{
				if (bKeepOpen)
					LockSetForegroundWindow(LSFW_LOCK);
				else
				{
					LockSetForegroundWindow(LSFW_UNLOCK);
					FadeOutItem(index);
					CloseSubMenus(CLOSE_POST,NULL);
				}
				PlayMenuSound(SOUND_COMMAND);
				ExecuteJumpItem(s_JumpAppInfo,s_JumpList.groups[LOWORD(item.jumpIndex)].items[HIWORD(item.jumpIndex)],g_OwnerWindow);
			}
			return;
		}
		if (g_LogCategories&LOG_ITEMS)
		{
			LOG_MENU(LOG_EXECUTE,L"item.bLink: %d",item.bLink?1:0);
			LOG_MENU(LOG_EXECUTE,L"item.bMetroLink: %d",item.bMetroLink?1:0);
			LOG_MENU(LOG_EXECUTE,L"item.bMetroApp: %d",item.bMetroApp?1:0);
			if (!item.pItemInfo)
			{
				LOG_MENU(LOG_EXECUTE,L"No pItemInfo");
			}
			else
			{
				{
					CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
					LOG_MENU(LOG_EXECUTE,L"bLink: %d",item.pItemInfo->IsLink()?1:0);
					LOG_MENU(LOG_EXECUTE,L"bMetroLink: %d",item.pItemInfo->IsMetroLink()?1:0);
					LOG_MENU(LOG_EXECUTE,L"bMetroApp: %d",item.pItemInfo->IsMetroApp()?1:0);
					LOG_MENU(LOG_EXECUTE,L"bProtectedLink: %d",item.pItemInfo->IsProtectedLink()?1:0);
					LOG_MENU(LOG_EXECUTE,L"bNoPin: %d",item.pItemInfo->IsNoPin()?1:0);
					LOG_MENU(LOG_EXECUTE,L"bNoNew: %d",item.pItemInfo->IsNoNew()?1:0);
					LOG_MENU(LOG_EXECUTE,L"path: %s",item.pItemInfo->GetPath());
					LOG_MENU(LOG_EXECUTE,L"PATH: %s",item.pItemInfo->PATH);
					LOG_MENU(LOG_EXECUTE,L"targetPATH: %s",item.pItemInfo->GetTargetPATH());
					LOG_MENU(LOG_EXECUTE,L"appid: %s",item.pItemInfo->GetAppid());
					LOG_MENU(LOG_EXECUTE,L"metroName: %s",item.pItemInfo->GetMetroName());
					LOG_MENU(LOG_EXECUTE,L"iconPath: %s",item.pItemInfo->GetIconPath());
				}
				if (item.pItemInfo->smallIcon)
				{
					CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ICONS);
					LOG_MENU(LOG_EXECUTE,L"smallIcon: %s",item.pItemInfo->smallIcon->GetPath());
				}
				if (item.pItemInfo->largeIcon)
				{
					CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ICONS);
					LOG_MENU(LOG_EXECUTE,L"largeIcon: %s",item.pItemInfo->largeIcon->GetPath());
				}
			}
		}
		if (item.bMetroLink)
		{
			LockSetForegroundWindow(LSFW_UNLOCK);
			FadeOutItem(index);
			PlayMenuSound(SOUND_COMMAND);
			ExecuteMetroLink(item.pItemInfo);
			if (bTrackRecent)
			{
				CString path;
				{
					CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
					path=item.pItemInfo->GetPath();
				}
				AddMRUShortcut(path);
			}
			g_ItemManager.RemoveNewItem(pItemPidl1,NULL,false);
			if (!(m_Options&CONTAINER_LINK))
				g_SearchManager.AddItemRank(item.nameHash);
			// close all menus when launching Metro apps
			CloseSubMenus(CLOSE_POST,NULL);
			return;
		}
		if (!pItemPidl1)
		{
			if (item.id<MENU_PROGRAMS) return; // non-executable item
			if (item.bFolder && item.pStdItem && item.pStdItem->submenu && !item.pStdItem->command && item.id!=MENU_SHUTDOWN_BUTTON)
				return; // non-executable item
		}

		// when executing an item close the whole menu
		if (!bKeepOpen)
		{
			if (g_TopWin7Menu && s_bAllPrograms)
			{
				// send, don't post. the top menu must be closed immediately. otherwise its closing may interfere with launching the command
				::SendMessage(g_TopWin7Menu,WM_CLOSE,0,0);
			}
			else
			{
				s_bPreventClosing=true; // hack: prevents any other closing behavior to occur while the item is being executed
				CloseSubMenus(CLOSE_POST,NULL);
				s_bPreventClosing=false;
			}
		}
	}

	if (type==ACTIVATE_MENU || type==ACTIVATE_MENU_BACKGROUND)
	{
		// when showing the context menu close all submenus
		if (!(m_Options&CONTAINER_SEARCH))
			SetActiveWindow();
		if (s_MenuMode==MODE_NORMAL)
			CloseSubMenus(0,this);
		if (m_bTwoColumns && s_MenuMode==MODE_JUMPLIST && index<m_OriginalCount)
		{
			CloseSubMenus(0,this);
			SetMenuMode(MODE_NORMAL);
		}
	}

	if (type!=ACTIVATE_MENU_BACKGROUND)
		SetHotItem(index);

	bool bCommand=(item.pStdItem && item.pStdItem->command && *item.pStdItem->command) || item.id==MENU_SEARCH_EXECUTE || item.bStartScreen || !pItemPidl1; // this is a special executable command

	if (type==ACTIVATE_EXECUTE && bCommand)
	{
		if (item.bStartScreen)
		{
			g_WSMHMonitor=MonitorFromWindow(m_hWnd,MONITOR_DEFAULTTONULL);
			::PostMessage(g_ProgWin,WM_SYSCOMMAND,SC_TASKLIST,'WSMH');
			return;
		}

		if (bKeepOpen)
			LockSetForegroundWindow(LSFW_LOCK);
		else
		{
			LockSetForegroundWindow(LSFW_UNLOCK);
			if (item.id!=MENU_SLEEP && item.id!=MENU_HIBERNATE)
				FadeOutItem(index);
			// flush all messages to close the menus
			// m_hWnd is not valid after this point
			MSG msg;
			while (PeekMessage(&msg,NULL,0,0,PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
		PlayMenuSound(SOUND_COMMAND);

		// special handling for command items
		TMenuID menuCommand=(item.id==MENU_SHUTDOWN_BUTTON)?s_ShutdownCommand:item.id;

		// translate command
		switch (menuCommand)
		{
			case MENU_SEARCH_FILES:
				if (!GetSettingString(L"SearchFilesCommand").IsEmpty())
					menuCommand=MENU_SEARCH_FILES_CUSTOM;
				break;
			case MENU_LOGOFF: // log off
				if (GetSettingBool(L"ConfirmLogOff"))
					menuCommand=MENU_LOGOFF_CONFIRM;
				break;
			case MENU_RESTART:
				if (GetWinVersion()>=WIN_VER_WIN8 && bShift)
					menuCommand=MENU_RESTART_ADVANCED;
				else if (s_bHasUpdates && GetWinVersion()>=WIN_VER_WIN8)
					menuCommand=MENU_RESTART_UPDATE;
				else
					menuCommand=MENU_RESTART_NOUPDATE;
				break;
			case MENU_SHUTDOWN:
				if (s_bHasUpdates)
					menuCommand=MENU_SHUTDOWN_UPDATE;
				else if (GetWinVersion()>=WIN_VER_WIN8 && !bShift && GetSettingBool(L"HybridShutdown"))
					menuCommand=MENU_SHUTDOWN_HYBRID;
				else
					menuCommand=MENU_SHUTDOWN_NOUPDATE;
				break;
		}

		switch (menuCommand)
		{
			case MENU_CLASSIC_SETTINGS: // show our settings
#ifdef PREVENT_CLOSING
				EditSettings(true,0);
#else
				EditSettings(false,0);
#endif
				break;
			case MENU_MORE_RESULTS:
				g_SearchManager.LaunchExternalSearch(NULL,CSearchManager::CATEGORY_INVALID,searchText);
				break;
			case MENU_SEARCH_INTERNET:
				g_SearchManager.LaunchInternetSearch(searchText);
				break;
			case MENU_SEARCH_EXECUTE:
				ExecuteCommand(item.name,bShift && bCtrl,true);
				break;
			case MENU_SEARCH_FILES_CUSTOM:
				ExecuteCommand(GetSettingString(L"SearchFilesCommand"),bShift && bCtrl,true);
				break;

			default:
				if (!ExecuteSysCommand(menuCommand) && item.pStdItem && item.pStdItem->command && *item.pStdItem->command)
				{
					wchar_t buf[1024];
					Strcpy(buf,_countof(buf),item.pStdItem->command);
					DoEnvironmentSubst(buf,_countof(buf));
					if (!searchText.IsEmpty() && (wcswcs(buf,L"%1") || wcswcs(buf,L"%2")))
						DoSearchSubst(buf,_countof(buf),searchText);
					ExecuteCommand(buf,bShift && bCtrl,false);
				}
		}
		return;
	}

	bool bHasMenu=false;
	if (pItemPidl1)
		bHasMenu=true;
	else if (type==ACTIVATE_MENU && (item.id==MENU_EMPTY || item.id==MENU_EMPTY_TOP || item.id==MENU_SEARCH_CATEGORY))
		bHasMenu=true;
	else if (type==ACTIVATE_MENU && item.id==MENU_APPS && (g_ItemManager.HasNewApps(true) || (pData && pData->bProgramsTree)))
		bHasMenu=true;
	if (!bHasMenu)
		return;

	bool bUninstallPolicy=GetUninstallPolicy();

	bool _bProtectedLink=false;
	bool _bMetroApp=false;
	bool _bExplicitAppId=false;
	bool _bIsLink=false;
	CString _path;
	CItemManager::TLocation _location=CItemManager::LOCATION_UNKNOWN;
	CString _appId;
	if (item.pItemInfo && item.id!=MENU_APPS)
	{
		CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
		_bProtectedLink=item.pItemInfo->IsProtectedLink();
		_bMetroApp=item.pItemInfo->IsMetroApp();
		_path=item.pItemInfo->GetPath();
		_location=item.pItemInfo->GetLocation();
		_appId=item.pItemInfo->GetAppid();
		_bIsLink=item.pItemInfo->IsLink();
		_bExplicitAppId=item.pItemInfo->IsExplicitAppId();
	}

	// create a context menu for the selected item. the context menu can be shown (ACTIVATE_MENU) or its default
	// item can be executed automatically (ACTIVATE_EXECUTE)
	CComPtr<IContextMenu> pMenu;
	HMENU menu=CreatePopupMenu();
	CComPtr<IContextMenu> pSecondaryMenu;
	int secondaryCmd=CMD_LAST;

	CComPtr<IShellItem> pItem;
	int insertBefore=-1, insertSecondary=-1;
	if (item.id==MENU_APPS)
	{
		insertBefore=0;
		if (g_ItemManager.HasNewApps(true))
			InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_MARKOLD,FindTranslation(L"Menu.RemoveHighlight",L"Remove highlight"));
		else if (!pData || !pData->bProgramsTree)
			return;
	}
	else if (item.id==MENU_EMPTY || item.id==MENU_EMPTY_TOP || item.id==MENU_SEARCH_CATEGORY || type==ACTIVATE_MENU_BACKGROUND)
	{
		insertBefore=0;
	}
	else
	{
		bool bQueryMenu=true;
		if (item.jumpIndex>=0)
		{
			const CJumpItem &jumpItem=s_JumpList.groups[LOWORD(item.jumpIndex)].items[HIWORD(item.jumpIndex)];
			// only items or links with no arguments can have a context menu
			bQueryMenu=((type==ACTIVATE_MENU || type==ACTIVATE_PROPERTIES) && (jumpItem.type==CJumpItem::TYPE_ITEM || (jumpItem.type==CJumpItem::TYPE_LINK && !jumpItem.bHasArguments)) && !_path.IsEmpty());
		}
		if (bQueryMenu)
		{
			SHCreateItemFromIDList(pItemPidl1,IID_IShellItem,(void**)&pItem);
			CComQIPtr<IShellItem2> pItem2(pItem);
			if (pItem2 &&
				((item.categoryHash&CSearchManager::CATEGORY_MASK)!=CSearchManager::CATEGORY_ITEM ||
				(GetSettingInt(L"CompatibilityFixes")&COMPATIBILITY_UPDATE_ITEMS))) // don't update search items because we don't have the right bind context for them
				pItem2->Update(NULL);
			if (!pItem || FAILED(pItem->BindToHandler(NULL,BHID_SFUIObject,IID_IContextMenu,(void**)&pMenu)))
			{
				DestroyMenu(menu);
				return;
			}

			UINT flags=CMF_DEFAULTONLY;
			if (type==ACTIVATE_MENU)
			{
				flags=CMF_NORMAL|CMF_CANRENAME;
				if (bShift) flags|=CMF_EXTENDEDVERBS;
			}
			if (type==ACTIVATE_DELETE || type==ACTIVATE_PROPERTIES)
				flags=CMF_NORMAL;
			if (type==ACTIVATE_RENAME)
				flags=CMF_NORMAL|CMF_CANRENAME;
			if (type==ACTIVATE_EXECUTE && bShift && bCtrl)
				flags|=CMF_EXTENDEDVERBS;
			HRESULT hr=pMenu->QueryContextMenu(menu,0,CMD_LAST,CMD_MAX,flags);
			if (FAILED(hr))
			{
				DestroyMenu(menu);
				return;
			}
			secondaryCmd=CMD_LAST+LOWORD(hr)+10;
		}

		if (item.bFolder && pItemPidl2)
		{
			// context menu for a double folder - remove most commands, add Open All Users
			int n=GetMenuItemCount(menu);
			for (int i=0;i<n;i++)
			{
				int id=GetMenuItemID(menu,i);
				char command[256];
				if (id<CMD_LAST || id>CMD_MAX || FAILED(pMenu->GetCommandString(id-CMD_LAST,GCS_VERBA,NULL,command,_countof(command))))
					command[0]=0;
				if (_stricmp(command,"open")==0)
				{
					if (GetSettingBool(L"EnableExplorer"))
					{
						if (!s_bNoCommonFolders)
							InsertMenu(menu,i+1,MF_BYPOSITION|MF_STRING,CMD_OPEN_ALL,FindTranslation(L"Menu.OpenAll",L"O&pen All Users"));
						InsertMenu(menu,i+2,MF_BYPOSITION|MF_SEPARATOR,0,0);
						i+=2;
						n+=2;
						continue;
					}
				}
				else if (_stricmp(command,"rename")==0 || _stricmp(command,"delete")==0)
				{
					if (item.id!=MENU_PROGRAMS) continue;
				}
				else if (_stricmp(command,"properties")==0)
				{
					insertBefore=i;
					continue;
				}
				DeleteMenu(menu,i,MF_BYPOSITION);
				i--;
				n--;
			}
		}
		else if (type==ACTIVATE_MENU && item.id==MENU_RECENT)
		{
			// context menu for a recent item - leave just open and runas
			bool bHasUninstall=false;
			int n=GetMenuItemCount(menu);
			for (int i=0;i<n;i++)
			{
				int id=GetMenuItemID(menu,i);
				char command[256];
				if (id<CMD_LAST || id>CMD_MAX || FAILED(pMenu->GetCommandString(id-CMD_LAST,GCS_VERBA,NULL,command,_countof(command))))
						command[0]=0;
				if (_stricmp(command,"properties")==0)
				{
					insertBefore=i;
					continue;
				}
				if (item.bMetroLink)
				{
					if (_stricmp(command,"pin_classic")==0 || _stricmp(command,"properties")==0)
						continue;
				}
				else
				{
					if (bShift)
					{
						if (_stricmp(command,"delete")!=0 && _stricmp(command,"rename")!=0)
							continue;
					}
					else
					{
						if (_stricmp(command,"open")==0 || _stricmp(command,"opencontaining")==0 || _stricmp(command,"runas")==0 || _stricmp(command,"runasuser")==0
							|| _stricmp(command,"taskbarpin")==0 || _stricmp(command,"taskbarunpin")==0 || _stricmp(command,"pin_classic")==0 || _stricmp(command,"properties")==0)
							continue;
						if (_stricmp(command,"uninstall")==0)
						{
							bHasUninstall=true;
							if (item.bMetroApp && bUninstallPolicy && !IsProtectedApp(_appId))
								continue;
						}
					}
				}
				DeleteMenu(menu,i,MF_BYPOSITION);
				i--;
				n--;
			}
			if (insertBefore==-1)
				insertBefore=n;

			if (item.bMetroLink)
			{
				InsertMenu(menu,0,MF_BYPOSITION|MF_STRING,CMD_OPEN,FindTranslation(L"Menu.Open",L"&Open"));
				SetMenuDefaultItem(menu,0,TRUE);
				insertBefore++;
				if (GetWinVersion()<WIN_VER_WIN10 && bUninstallPolicy && !bHasUninstall && !IsProtectedApp(_appId))
				{
					InsertMenu(menu,insertBefore,MF_BYPOSITION|MF_STRING,CMD_UNINSTALL,FindTranslation(L"Menu.Uninstall",L"&Uninstall"));
					insertBefore++;
				}
				insertSecondary=1;
				n++;
			}
			if (item.bMetroApp && IsWin10RS1() && !bHasUninstall && CanUninstallMetroApp(_appId))
			{
				InsertMenu(menu,insertBefore,MF_BYPOSITION|MF_STRING,CMD_UNINSTALL,FindTranslation(L"Menu.Uninstall",L"&Uninstall"));
				insertBefore++;
			}

			if (n>0)
				InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_SEPARATOR,0,0);
			InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_DELETEMRU,FindTranslation(L"Menu.RemoveList",L"Remove &from this list"));
			if (s_RecentPrograms==RECENT_PROGRAMS_RECENT)
				InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_DELETEALL,FindTranslation(L"Menu.RemoveAll",L"C&lear recent items list"));
			if (pItemPidl1 && GetSettingBool(L"EnableExplorer"))
				InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_EXPLORE,FindTranslation(L"Menu.Explore",L"&Explore"));
			InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_SEPARATOR,0,0);
		}
		else if (type==ACTIVATE_MENU && item.bMetroLink && (_bProtectedLink || (m_Options&(CONTAINER_APPS|CONTAINER_SEARCH))))
		{
			// context menu for a Metro link - just open, properties and Explore
			int n=GetMenuItemCount(menu);
			for (int i=0;i<n;i++)
			{
				int id=GetMenuItemID(menu,i);
				char command[256];
				if (id<CMD_LAST || id>CMD_MAX || FAILED(pMenu->GetCommandString(id-CMD_LAST,GCS_VERBA,NULL,command,_countof(command))))
					command[0]=0;
				if (_stricmp(command,"properties")==0 || _stricmp(command,"pin_classic")==0)
				{
					insertBefore=i;
					continue;
				}
				DeleteMenu(menu,i,MF_BYPOSITION);
				i--;
				n--;
			}
			InsertMenu(menu,0,MF_BYPOSITION|MF_STRING,CMD_OPEN,FindTranslation(L"Menu.Open",L"&Open"));
			SetMenuDefaultItem(menu,0,TRUE);
			insertBefore++;
			if (GetWinVersion()<WIN_VER_WIN10 && bUninstallPolicy && !IsProtectedApp(_appId))
			{
				InsertMenu(menu,insertBefore,MF_BYPOSITION|MF_STRING,CMD_UNINSTALL,FindTranslation(L"Menu.Uninstall",L"&Uninstall"));
				insertBefore++;
			}
			insertSecondary=1;
			if (GetSettingBool(L"EnableExplorer"))
			{
				InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_SEPARATOR,0,0);
				InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_EXPLORE,FindTranslation(L"Menu.Explore",L"&Explore"));
				InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_SEPARATOR,0,0);
			}
		}
		else if (type==ACTIVATE_MENU && ((m_Options&CONTAINER_SEARCH) || (s_MenuMode==MODE_SEARCH && index>m_OriginalCount)))
		{
			// context menu for a search item - remove delete, rename and link
			int n=GetMenuItemCount(menu);
			for (int i=0;i<n;i++)
			{
				int id=GetMenuItemID(menu,i);
				char command[256];
				if (id<CMD_LAST || id>CMD_MAX || FAILED(pMenu->GetCommandString(id-CMD_LAST,GCS_VERBA,NULL,command,_countof(command))))
					command[0]=0;
				if (_stricmp(command,"properties")==0)
				{
					insertBefore=i;
					continue;
				}
				if (_stricmp(command,"delete")!=0 && _stricmp(command,"rename")!=0)
					continue;
				DeleteMenu(menu,i,MF_BYPOSITION);
				i--;
				n--;
			}
			bool last=insertBefore==-1;
			if (last)
				insertBefore=n;

			CSearchManager::TItemCategory cat=(CSearchManager::TItemCategory)(item.categoryHash&CSearchManager::CATEGORY_MASK);
			if (pItemPidl1 && (cat<=CSearchManager::CATEGORY_FILE || cat==CSearchManager::CATEGORY_AUTOCOMPLETE))
			{
				bool bExplore=GetSettingBool(L"EnableExplorer");
				bool bPin=!s_PinFolder.IsEmpty() && (cat==CSearchManager::CATEGORY_SETTING || cat==CSearchManager::CATEGORY_METROSETTING);
				if (bExplore || bPin)
				{
					if (n>0)
						InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_SEPARATOR,0,0);
					if (bPin)
						InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_PINSETTING,FindTranslation(L"Menu.PinStartCs",L"Pin to Start menu (Open-Shell)"));
					if (bExplore)
						InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_EXPLORE,FindTranslation(L"Menu.Explore",L"&Explore"));
					if (!last)
						InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_SEPARATOR,0,0);
				}
			}
		}
		else if (item.jumpIndex>=0)
		{
			// context menu for a jumplist item - just properties
			int n=GetMenuItemCount(menu);
			for (int i=0;i<n;i++)
			{
				int id=GetMenuItemID(menu,i);
				char command[256];
				if (id<CMD_LAST || id>CMD_MAX || FAILED(pMenu->GetCommandString(id-CMD_LAST,GCS_VERBA,NULL,command,_countof(command))))
					command[0]=0;
				if (_stricmp(command,"properties")==0)
					continue;
				DeleteMenu(menu,i,MF_BYPOSITION);
				i--;
				n--;
			}
			insertBefore=0;
			const CJumpGroup &group=s_JumpList.groups[LOWORD(item.jumpIndex)];
			InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_OPEN,FindTranslation(L"Menu.Open",L"&Open"));
			SetMenuDefaultItem(menu,0,TRUE);
			if (group.type!=CJumpGroup::TYPE_TASKS)
			{
				InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_SEPARATOR,0,0);
				if (group.type==CJumpGroup::TYPE_PINNED)
					InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_PIN,FindTranslation(L"JumpList.Unpin",L"&Unpin from this list"));
				else
				{
					InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_PIN,FindTranslation(L"JumpList.Pin",L"P&in to this list"));
					InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_DELETEMRU,FindTranslation(L"JumpList.Remove",L"Remove &from this list"));
				}
				if (n>0)
				{
					InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_SEPARATOR,0,0);
					if (GetSettingBool(L"EnableExplorer"))
						InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_EXPLORE,FindTranslation(L"Menu.Explore",L"&Explore"));
				}
			}
		}
		else
		{
			bool bHasUninstall=false;
			int n=GetMenuItemCount(menu);
			for (int i=0;i<n;i++)
			{
				int id=GetMenuItemID(menu,i);
				char command[256];
				if (id<CMD_LAST || id>CMD_MAX || FAILED(pMenu->GetCommandString(id-CMD_LAST,GCS_VERBA,NULL,command,_countof(command))))
					command[0]=0;
				if (_stricmp(command,"properties")==0)
				{
					insertBefore=i;
					continue;
				}
				bool bDelete=false;
				if (item.pStdItem && (_stricmp(command,"rename")==0 || _stricmp(command,"delete")==0))
					bDelete=true;
				else if (item.bMetroLink && _stricmp(command,"rename")==0)
					bDelete=true;
				else if (_stricmp(command,"uninstall")==0)
				{
					bHasUninstall=true;
					if ((!item.bMetroApp && !item.bMetroLink) || !bUninstallPolicy || IsProtectedApp(_appId))
						bDelete=true;
				}
				else if (item.bStartScreen)
				{
					if (_stricmp(command,"rename")==0 || _stricmp(command,"delete")==0)
						EnableMenuItem(menu,i,MF_GRAYED|MF_BYPOSITION);
					else if (_stricmp(command,"pin_classic")==0 && IsSettingLocked(L"StartScreenShortcut"))
						EnableMenuItem(menu,i,MF_GRAYED|MF_BYPOSITION);
					else if (_stricmp(command,"open")==0 || _stricmp(command,"opencontaining")==0 || _stricmp(command,"runas")==0)
						bDelete=true;
				}
				if (bDelete)
				{
					DeleteMenu(menu,i,MF_BYPOSITION);
					i--;
					n--;
				}
			}
			if (insertBefore==-1)
				insertBefore=n;
			if (item.bMetroLink)
			{
				InsertMenu(menu,0,MF_BYPOSITION|MF_STRING,CMD_OPEN,FindTranslation(L"Menu.Open",L"&Open"));
				SetMenuDefaultItem(menu,0,TRUE);
				insertBefore++;
				if (GetWinVersion()<WIN_VER_WIN10 && !bHasUninstall && bUninstallPolicy && !IsProtectedApp(_appId))
				{
					InsertMenu(menu,insertBefore,MF_BYPOSITION|MF_STRING,CMD_UNINSTALL,FindTranslation(L"Menu.Uninstall",L"&Uninstall"));
					insertBefore++;
				}
				insertSecondary=1;
			}
			else if (item.bMetroApp && !bHasUninstall && bUninstallPolicy && IsWin10RS1() && CanUninstallMetroApp(_appId))
			{
				InsertMenu(menu,insertBefore,MF_BYPOSITION|MF_STRING,CMD_UNINSTALL,FindTranslation(L"Menu.Uninstall",L"&Uninstall"));
				insertBefore++;
			}
		}
	}

	if (item.bMetroLink && type==ACTIVATE_MENU && insertSecondary>=0)
	{
		pSecondaryMenu=GetMetroPinMenu(_appId);
		if (pSecondaryMenu)
		{
			pSecondaryMenu->QueryContextMenu(menu,insertSecondary,secondaryCmd,CMD_MAX,CMF_NORMAL);
		}
	}

	s_HotPos=GetMessagePos();
	int res=0;
	if (type==ACTIVATE_EXECUTE)
	{
		// just pick the default item
		res=GetMenuDefaultItem(menu,FALSE,0);
		if (bShift && bCtrl)
		{
			// find the runas verb if available
			res=-1;
			char command[256];
			int n=GetMenuItemCount(menu);
			for (int i=0;i<n;i++)
			{
				int id=GetMenuItemID(menu,i);
				if (id>=CMD_LAST && id<=CMD_MAX && SUCCEEDED(pMenu->GetCommandString(id-CMD_LAST,GCS_VERBA,NULL,command,_countof(command))))
				{
					if (_stricmp(command,"runas")==0)
					{
						res=id;
						break;
					}
				}
			}
			if (res==-1)
			{
				CComString pName;
				if (SUCCEEDED(pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName)))
				{
					ExecuteCommand(pName,true,false);
					DestroyMenu(menu);
					s_HotPos=GetMessagePos();
					return;
				}
				res=0;
			}
		}
		if (res<0) res=0;
	}
	else if (type==ACTIVATE_RENAME || type==ACTIVATE_DELETE || type==ACTIVATE_PROPERTIES)
	{
		if ((type==ACTIVATE_RENAME || type==ACTIVATE_DELETE) && item.bStartScreen)
			res=0;
		else if (type==ACTIVATE_DELETE && item.id==MENU_RECENT)
			res=CMD_DELETEMRU;
		else if ((type==ACTIVATE_RENAME || type==ACTIVATE_DELETE) && item.bMetroLink && (_bProtectedLink || (m_Options&(CONTAINER_APPS|CONTAINER_SEARCH))))
			res=0;
		else
		{
			const char *name;
			switch (type)
			{
				case ACTIVATE_RENAME: name="rename"; break;
				case ACTIVATE_DELETE: name="delete"; break;
				case ACTIVATE_PROPERTIES: name="properties"; break;
			}
			char command[256];
			int n=GetMenuItemCount(menu);
			for (int i=0;i<n;i++)
			{
				int id=GetMenuItemID(menu,i);
				if (id>=CMD_LAST && id<=CMD_MAX && SUCCEEDED(pMenu->GetCommandString(id-CMD_LAST,GCS_VERBA,NULL,command,_countof(command))))
				{
					if (_stricmp(command,name)==0)
					{
						res=id;
						break;
					}
				}
			}
			if (res<0) res=0;
		}
	}
	else
	{
		if (!GetSettingBool(L"EnableExplorer"))
		{
			// disable the Open verb
			char command[256];
			int n=GetMenuItemCount(menu);
			for (int i=0;i<n;i++)
			{
				int id=GetMenuItemID(menu,i);
				if (id>=CMD_LAST && id<=CMD_MAX && SUCCEEDED(pMenu->GetCommandString(id-CMD_LAST,GCS_VERBA,NULL,command,_countof(command))))
				{
					if ((item.bFolder && !item.bSplit && pItemPidl1 && _stricmp(command,"open")==0) || _stricmp(command,"opencontaining")==0)
					{
						EnableMenuItem(menu,i,MF_BYPOSITION|MF_GRAYED);
					}
				}
			}
		}

		// show the context menu
		m_pMenu2=pMenu;
		m_pMenu3=pMenu;
		HBITMAP shellBmp=NULL;
		HBITMAP newFolderBmp=NULL;
		HBITMAP newShortcutBmp=NULL;
		if ((item.id==MENU_NO || item.id==MENU_EMPTY || type==ACTIVATE_MENU_BACKGROUND || (item.id==MENU_APPS && pData && pData->bProgramsTree)) && item.jumpIndex<0 && index<m_OriginalCount && (m_Options&CONTAINER_DROP)) // clicked on a movable item or the background
		{
			HMENU menu2=menu;
			int subMenuIdx=-1;
			InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_SEPARATOR,0,0);
			if (GetMenuItemCount(menu)>0)
			{
				if (GetSettingBool(L"CascadingMenu"))
				{
					menu2=CreatePopupMenu();
					subMenuIdx=insertBefore;
					insertBefore=0;
				}
			}
			bool bSort=false, bAutoSort=false, bNew=false, bMarkOld=false;
			if (pData && pData->bProgramsTree)
			{
				bNew=!(item.pItemInfo && _location==CItemManager::LOCATION_METRO) && !pData->bApps && GetSettingBool(L"ShowNewFolder");
				bSort=true;
				bAutoSort=pData->bAutoSort;
			}
			else
			{
				int n=0;
				for (std::vector<MenuItem>::const_iterator it=m_Items.begin();it!=m_Items.end();++it)
					if (it->id==MENU_NO)
						n++;
				if (n>1)
					bSort=true; // more than 1 movable items
				wchar_t path[_MAX_PATH];
				if (!(m_Options&CONTAINER_APPS) && !(item.pItemInfo && _location==CItemManager::LOCATION_METRO) && !(m_Options&CONTAINER_NONEWFOLDER) && GetSettingBool(L"ShowNewFolder") && SHGetPathFromIDList(m_Path1[item.priority==2?1:0],path))
					bNew=true;
				bAutoSort=(m_Options&CONTAINER_AUTOSORT)!=0;
			}
			bMarkOld=item.bNew && pItemPidl1;

			if (bSort)
				InsertMenu(menu2,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_SORT,FindTranslation(L"Menu.SortByName",L"Sort &by Name"));

			if (m_FolderHash[0])
				InsertMenu(menu2,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_AUTOSORT,FindTranslation(L"Menu.AutoArrange",L"&Auto Arrange"));
			if (bAutoSort)
			{
				EnableMenuItem(menu2,CMD_SORT,MF_BYCOMMAND|MF_GRAYED);
				CheckMenuItem(menu2,CMD_AUTOSORT,MF_BYCOMMAND|MF_CHECKED);
			}
			if (bNew)
			{
				InsertMenu(menu2,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_NEWFOLDER,FindTranslation(L"Menu.NewFolder",L"New Folder"));
				InsertMenu(menu2,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_NEWSHORTCUT,FindTranslation(L"Menu.NewShortcut",L"New Shortcut"));
			}
			if (bMarkOld)
				InsertMenu(menu2,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_MARKOLD,FindTranslation(L"Menu.RemoveHighlight",L"Remove highlight"));
			if (menu!=menu2 && GetMenuItemCount(menu2)==0)
			{
				DestroyMenu(menu2);
				menu2=menu;
			}
			else
				InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_SEPARATOR,0,0);

			if (bNew || menu!=menu2)
			{
				int size=GetSystemMetrics(SM_CXSMICON);
				if (bNew)
				{
					HMODULE hShell32=GetModuleHandle(L"shell32.dll");
					HICON hIcon=(HICON)LoadImage(hShell32,MAKEINTRESOURCE(319),IMAGE_ICON,size,size,LR_DEFAULTCOLOR);
					if (hIcon)
					{
						newFolderBmp=BitmapFromIcon(hIcon,size,NULL,true);
						MENUITEMINFO mii={sizeof(mii)};
						mii.fMask=MIIM_BITMAP;
						mii.hbmpItem=newFolderBmp;
						SetMenuItemInfo(menu2,CMD_NEWFOLDER,FALSE,&mii);
					}
					hIcon=(HICON)LoadImage(hShell32,MAKEINTRESOURCE(16769),IMAGE_ICON,size,size,LR_DEFAULTCOLOR);
					if (hIcon)
					{
						newShortcutBmp=BitmapFromIcon(hIcon,size,NULL,true);
						MENUITEMINFO mii={sizeof(mii)};
						mii.fMask=MIIM_BITMAP;
						mii.hbmpItem=newShortcutBmp;
						SetMenuItemInfo(menu2,CMD_NEWSHORTCUT,FALSE,&mii);
					}
				}
				if (menu!=menu2)
				{
					InsertMenu(menu,subMenuIdx,MF_BYPOSITION|MF_POPUP,(UINT_PTR)menu2,FindTranslation(L"Menu.Organize",L"Organize Start menu"));
					HICON hIcon=(HICON)LoadImage(g_Instance,MAKEINTRESOURCE(IDI_APPICON),IMAGE_ICON,size,size,LR_DEFAULTCOLOR);
					if (hIcon)
					{
						shellBmp=BitmapFromIcon(hIcon,size,NULL,true);
						MENUITEMINFO mii={sizeof(mii)};
						mii.fMask=MIIM_BITMAP;
						mii.hbmpItem=shellBmp;
						SetMenuItemInfo(menu,subMenuIdx,TRUE,&mii);
					}
				}
			}
		}
		if (item.id==MENU_SEARCH_CATEGORY)
		{
			InsertMenu(menu,0,MF_BYPOSITION|MF_STRING,CMD_TOGGLE,item.categoryHash==m_SearchCategoryHash?FindTranslation(L"Menu.Collapse",L"Coll&apse"):FindTranslation(L"Menu.Expand",L"Exp&and"));
			if (item.categoryHash>=CSearchManager::CATEGORY_FILE)
			{
				wchar_t text[256];
				Sprintf(text,_countof(text),L"%s\t(Ctrl+Enter)",FindTranslation(L"Menu.MoreResults",L"See more results"));
				InsertMenu(menu,1,MF_BYPOSITION|MF_STRING,CMD_EXPLORE,text);
			}
			SetMenuDefaultItem(menu,(pData && pData->bArrow)?1:0,TRUE);
		}

		if (item.id==MENU_PROGRAMS)
		{
			bool bNew;
			if (s_bWin7Style && GetWinVersion()>=WIN_VER_WIN8 && GetSettingBool(L"AllProgramsMetro"))
				bNew=g_ItemManager.HasNewPrograms(true) || g_ItemManager.HasNewApps(true);
			else
				bNew=g_ItemManager.HasNewPrograms(true);
			if (bNew)
				InsertMenu(menu,insertBefore++,MF_BYPOSITION|MF_STRING,CMD_MARKOLD,FindTranslation(L"Menu.RemoveHighlight",L"Remove highlight"));
		}
		if (pData && pData->bProgramsTree && item.bFolder && type==ACTIVATE_MENU)
		{
			InsertMenu(menu,0,MF_BYPOSITION|MF_STRING,CMD_TOGGLE,pData->bExpanded?FindTranslation(L"Menu.Collapse",L"Coll&apse"):FindTranslation(L"Menu.Expand",L"Exp&and"));
			SetMenuDefaultItem(menu,0,TRUE);
			InsertMenu(menu,1,MF_BYPOSITION|MF_SEPARATOR,0,0);
		}

		// remove multiple separators
		{
			bool bSeparator=true;
			int n=GetMenuItemCount(menu);
			for (int i=0;i<n;i++)
			{
				MENUITEMINFO info={sizeof(info),MIIM_FTYPE};
				if (GetMenuItemInfo(menu,i,TRUE,&info))
				{
					if (info.fType==MFT_SEPARATOR && bSeparator)
					{
						DeleteMenu(menu,i,MF_BYPOSITION);
						i--;
						n--;
					}
					bSeparator=(info.fType==MFT_SEPARATOR);
				}
			}
			if (n>0 && bSeparator)
				DeleteMenu(menu,n-1,MF_BYPOSITION);
		}

		TPMPARAMS params={sizeof(params)}, *pParams=NULL;
		POINT pt2;
		if (pPt)
			pt2=*pPt;
		else
		{
			GetItemRect(index,params.rcExclude);
			MapWindowPoints(NULL,&params.rcExclude);
			pt2.x=params.rcExclude.left;
			pt2.y=params.rcExclude.top;
			pParams=&params;
		}
		SetContextItem(index);
		InvalidateItem(index);
		KillTimer(TIMER_HOVER);
		res=0;
		if (GetMenuItemCount(menu)>0)
		{
			LOG_MENU(LOG_EXECUTE,L"Open context menu, ptr=%p",this);
			res=TrackPopupMenuEx(menu,TPM_RIGHTBUTTON|TPM_RETURNCMD|TPM_VERTICAL|(IsLanguageRTL()?TPM_LAYOUTRTL:0),pt2.x,pt2.y,m_hWnd,pParams);
			LOG_MENU(LOG_EXECUTE,L"Select context menu, ptr=%p, index=%d",this,res);
		}
		SetContextItem(-1);
		if (m_HotItem<0 && !m_bDestroyed) SetHotItem(index);
		if (m_pMenu2) m_pMenu2.Release();
		if (m_pMenu3) m_pMenu3.Release();
		if (newFolderBmp) DeleteObject(newFolderBmp);
		if (newShortcutBmp) DeleteObject(newShortcutBmp);
		if (shellBmp) DeleteObject(shellBmp);
	}

	ExitGuard guard; // no returns are allowed until the end cleanup
	if (pData) pData->command=res;


	if (type==ACTIVATE_EXECUTE)
	{
		if (bKeepOpen)
			LockSetForegroundWindow(LSFW_LOCK);
		else
		{
			LockSetForegroundWindow(LSFW_UNLOCK);
			FadeOutItem(index);
		}
		PlayMenuSound(SOUND_COMMAND);
	}

	if (res==CMD_PINSETTING)
	{
		CString iconPath;
		if (item.pItemInfo)
		{
			CItemManager::RWLock lock(&g_ItemManager, false, CItemManager::RWLOCK_ITEMS);
			if (_wcsicmp(PathFindExtension(item.pItemInfo->GetPath()), L".settingcontent-ms") == 0)
				iconPath = L"%windir%\\ImmersiveControlPanel\\systemsettings.exe";
		}

		CreatePinLink(pItemPidl1, item.name, iconPath.IsEmpty() ? nullptr : iconPath.GetString(), 0);
		m_bRefreshItems=true;
	}

	// handle our standard commands
	if (item.jumpIndex>=0 && res!=CMD_EXPLORE && res<CMD_LAST && item.id!=MENU_EMPTY)
	{
		const CJumpGroup &group=s_JumpList.groups[LOWORD(item.jumpIndex)];
		const CJumpItem &jumpItem=group.items[HIWORD(item.jumpIndex)];
		if (res==CMD_OPEN)
		{
			ExecuteJumpItem(s_JumpAppInfo,jumpItem,g_OwnerWindow);
		}
		else if (res==CMD_PIN)
		{
			PinJumpItem(s_JumpAppInfo,s_JumpList,LOWORD(item.jumpIndex),HIWORD(item.jumpIndex),group.type!=CJumpGroup::TYPE_PINNED,-1);
			PostRefreshMessage();
		}
		else if (res==CMD_DELETEMRU)
		{
			RemoveJumpItem(s_JumpAppInfo,s_JumpList,LOWORD(item.jumpIndex),HIWORD(item.jumpIndex));
			PostRefreshMessage();
		}
		res=0;
	}
	if (res==CMD_OPEN && item.bMetroLink)
	{
		ExecuteMetroLink(item.pItemInfo);
		if (bTrackRecent)
			AddMRUShortcut(_path);
		g_ItemManager.RemoveNewItem(pItemPidl1,NULL,false);
		if (!(m_Options&CONTAINER_LINK))
			g_SearchManager.AddItemRank(item.nameHash);

		// close all menus when launching Metro apps
		CloseSubMenus(CLOSE_POST,NULL);
		res=0;
	}
	if (res==CMD_OPEN_ALL)
	{
		SHELLEXECUTEINFO execute={sizeof(execute),SEE_MASK_IDLIST|SEE_MASK_INVOKEIDLIST};
		execute.lpVerb=L"open";
		execute.lpIDList=pItemPidl2;
		execute.nShow=SW_SHOWNORMAL;
		ShellExecuteEx(&execute);
		res=0;
	}

	if (res==CMD_SORT)
	{
		if (pData && pData->bProgramsTree && m_pProgramsTree)
		{
			m_pProgramsTree->OrderElements(pData->hTreeItem,TreeView_GetParent(m_pProgramsTree->m_hWnd,pData->hTreeItem),std::vector<unsigned int>(),false,true);
		}
		else
		{
			std::vector<SortMenuItem> items;
			for (int i=0;i<m_OriginalCount;i++)
			{
				if (m_Items[i].id==MENU_NO)
				{
					SortMenuItem item(m_Items[i]);
					items.push_back(item);
				}
			}
			std::sort(items.begin(),items.end());
			if (m_Options&CONTAINER_SORTZA)
				std::reverse(items.begin(),items.end());
			SaveItemOrder(items);
			PostRefreshMessage();
		}
		res=0;
	}
	if (res==CMD_MARKOLD)
	{
		if (item.id==MENU_APPS)
			g_ItemManager.RemoveNewItems(false,true);
		else if (item.id==MENU_PROGRAMS)
		{
			g_ItemManager.RemoveNewItems(true,s_bWin7Style && GetWinVersion()>=WIN_VER_WIN8 && GetSettingBool(L"AllProgramsMetro"));
			if (m_pProgramsTree)
				m_pProgramsTree->ClearAllNew();
		}
		else
			g_ItemManager.RemoveNewItem(pItemPidl1,pItemPidl2,item.bFolder);
		PostRefreshMessage();
	}
	if (res==CMD_AUTOSORT)
	{
		if (pData && pData->bProgramsTree)
		{
			m_pProgramsTree->OrderElements(pData->hTreeItem,TreeView_GetParent(m_pProgramsTree->m_hWnd,pData->hTreeItem),std::vector<unsigned int>(),!pData->bAutoSort,true);
		}
		else
		{
			if (m_FolderHash[0])
			{
				CRegKey regOrder;
				if (regOrder.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu\\Order")!=ERROR_SUCCESS)
					regOrder.Create(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu\\Order");
				wchar_t name[100];
				Sprintf(name,_countof(name),L"%08X",m_FolderHash[0]);
				if (m_Options&CONTAINER_AUTOSORT)
					regOrder.SetBinaryValue(name,NULL,0);
				else
				{
					DWORD cAuto='AUTO';
					regOrder.SetBinaryValue(name,&cAuto,4);
				}
				if (m_FolderHash[1])
				{
					Sprintf(name,_countof(name),L"%08X",m_FolderHash[1]);
					if (m_Options&CONTAINER_AUTOSORT)
						regOrder.SetBinaryValue(name,NULL,0);
					else
					{
						DWORD cAuto='AUTO';
						regOrder.SetBinaryValue(name,&cAuto,4);
					}
				}
			}
			PostRefreshMessage();
		}
		res=0;
	}

	if (res==CMD_NEWFOLDER)
	{
		g_RenameText=item.name;
		if (pPt)
			g_RenamePos=*pPt;
		else
		{
			g_RenamePos.x=item.itemRect.left;
			g_RenamePos.y=item.itemRect.top;
			ClientToScreen(&g_RenamePos);
		}
		bool bAllPrograms=s_bAllPrograms;
		for (std::vector<CMenuContainer*>::iterator it=s_Menus.begin();it!=s_Menus.end();++it)
			(*it)->EnableWindow(FALSE); // disable all menus
		if (bAllPrograms) ::EnableWindow(g_TopWin7Menu,FALSE);

		CComPtr<IShellFolder> pFolder; // have to use IShellFolder for renaming because it's the only one that supports changing the display name
		if (pItemPidl1)
		{
			PCUITEMID_CHILD pidl;
			SHBindToParent(pItemPidl1,IID_IShellFolder,(void**)&pFolder,&pidl);
		}
		else if (pData && pData->bProgramsTree)
		{
			CComPtr<IShellFolder> pDesktop;
			if (SUCCEEDED(SHGetDesktopFolder(&pDesktop)))
				pDesktop->BindToObject(pData->parent,NULL,IID_IShellFolder,(void**)&pFolder);
		}
		else
		{
			CComPtr<IShellFolder> pDesktop;
			if (SUCCEEDED(SHGetDesktopFolder(&pDesktop)))
				pDesktop->BindToObject(m_Path1[0],NULL,IID_IShellFolder,(void**)&pFolder);
		}

		if (pFolder)
		{
			CComPtr<IContextMenu> pMenu2;
			HMENU menu2=CreatePopupMenu();

			std::vector<unsigned int> items;
			{
				CComPtr<IEnumIDList> pEnum;
				if (pFolder->EnumObjects(NULL,SHCONTF_FOLDERS,&pEnum)!=S_OK) pEnum=NULL;

				PITEMID_CHILD child;
				while (pEnum && pEnum->Next(1,&child,NULL)==S_OK)
				{
					STRRET str;
					if (SUCCEEDED(pFolder->GetDisplayNameOf(child,SHGDN_INFOLDER|SHGDN_FORPARSING,&str)))
					{
						CComString pName;
						StrRetToStr(&str,child,&pName);
						items.push_back(CalcFNVHash(pName));
					}
					ILFree(child);
				}
			}

			s_bPreventClosing=true;
			if (SUCCEEDED(pFolder->CreateViewObject(g_OwnerWindow,IID_IContextMenu,(void**)&pMenu2)))
			{
				if (SUCCEEDED(pMenu2->QueryContextMenu(menu2,0,1,32767,CMF_NORMAL)))
				{
					CMINVOKECOMMANDINFOEX info={sizeof(info),CMIC_MASK_UNICODE};
					info.lpVerb="NewFolder";
					info.lpVerbW=L"NewFolder";
					info.nShow=SW_SHOWNORMAL;
					info.fMask|=CMIC_MASK_NOASYNC;
					info.hwnd=g_OwnerWindow;
					pMenu2->InvokeCommand((CMINVOKECOMMANDINFO*)&info);
				}
			}
			DestroyMenu(menu2);
			s_bPreventClosing=false;

			PITEMID_CHILD newPidl=NULL;
			unsigned int newHash=0;
			{
				CComPtr<IEnumIDList> pEnum;
				if (pFolder->EnumObjects(NULL,SHCONTF_FOLDERS,&pEnum)!=S_OK) pEnum=NULL;

				PITEMID_CHILD child;
				while (pEnum && pEnum->Next(1,&child,NULL)==S_OK)
				{
					STRRET str;
					if (SUCCEEDED(pFolder->GetDisplayNameOf(child,SHGDN_INFOLDER|SHGDN_FORPARSING,&str)))
					{
						CComString pName;
						StrRetToStr(&str,child,&pName);
						unsigned int hash=CalcFNVHash(pName);
						if (std::find(items.begin(),items.end(),hash)==items.end())
						{
							if (SUCCEEDED(pFolder->GetDisplayNameOf(child,SHGDN_INFOLDER|SHGDN_FOREDITING,&str)))
							{
								CComString pName2;
								StrRetToStr(&str,child,&pName2);
								g_RenameText=pName2;
							}
							else
								g_RenameText=pName;
							pName.MakeUpper();
							newHash=CalcFNVHash(pName,item.priority==2?CalcFNVHash(L"\\"):FNV_HASH0);
							newPidl=child;
							break;
						}
					}
					ILFree(child);
				}
			}

			if (!pData || !pData->bProgramsTree)
			{
				PostRefreshMessage();
				if (!m_bDestroyed)
					PostMessage(MCM_SETCONTEXTITEM,newHash);
			}
			// show the Rename dialog box
			s_bPreventClosing=true;
			if (newPidl && DialogBox(g_Instance,MAKEINTRESOURCE(s_bRTL?IDD_RENAMER:IDD_RENAME),g_OwnerWindow,RenameDlgProc))
			{
				PITEMID_CHILD newPidl2=NULL;
				if (SUCCEEDED(pFolder->SetNameOf(g_OwnerWindow,newPidl,g_RenameText,SHGDN_INFOLDER,&newPidl2)))
				{
					ILFree(newPidl);
					newPidl=newPidl2;
					if (!pData || !pData->bProgramsTree)
					{
						PostRefreshMessage();
						StringUpper(g_RenameText);
						newHash=CalcFNVHash(g_RenameText,item.priority==2?CalcFNVHash(L"\\"):FNV_HASH0);
						if (!m_bDestroyed)
							PostMessage(MCM_SETCONTEXTITEM,newHash);
					}
				}
			}
			if (newPidl && pData && pData->bProgramsTree)
			{
				CComPtr<IShellItem> pNewItem;
				if (SUCCEEDED(SHCreateItemWithParent(NULL,pFolder,newPidl,IID_IShellItem,(void**)&pNewItem)))
				{
					CAbsolutePidl newAbsPidl;
					if (SUCCEEDED(SHGetIDListFromObject(pNewItem,&newAbsPidl)))
						pData->pNewItemInfo=g_ItemManager.GetItemInfo(pNewItem,newAbsPidl,0);
				}
			}
			if (newPidl) ILFree(newPidl);
			for (std::vector<CMenuContainer*>::iterator it=s_Menus.begin();it!=s_Menus.end();++it)
				if (!(*it)->m_bDestroyed)
					(*it)->EnableWindow(TRUE); // enable all menus
			if (bAllPrograms) ::EnableWindow(g_TopWin7Menu,TRUE);
			if (!m_bDestroyed)
			{
				SetForegroundWindow(m_hWnd);
				SetActiveWindow();
				if (pData && pData->bProgramsTree)
					m_pProgramsTree->SetFocus();
				else
					SetFocus();
				Invalidate();
				if (m_HotItem<0) SetHotItem(index);
			}
			s_bPreventClosing=false;
		}
		SetContextItem(-1);
		res=0;
	}

	if (res==CMD_NEWSHORTCUT)
	{
		wchar_t target[_MAX_PATH+1];
		if (pData && pData->bProgramsTree)
			SHGetPathFromIDList(pData->parent,target);
		else
			SHGetPathFromIDList(m_Path1[0],target);
		target[Strlen(target)+1]=0;
		wchar_t fname[_MAX_PATH+1];

		// first try in the original folder
		PathMakeUniqueName(fname,_countof(fname)-1,L"scut.lnk",L"New Shortcut.lnk",target);
		HANDLE hFile=CreateFile(fname,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
		if (hFile!=INVALID_HANDLE_VALUE)
		{
			CloseHandle(hFile);

			// just run the shortcut wizard
			wchar_t cmdLine[1024];
			Sprintf(cmdLine,_countof(cmdLine),L"rundll32.exe appwiz.cpl,NewLinkHere %s",fname);

			STARTUPINFO startupInfo={sizeof(startupInfo)};
			PROCESS_INFORMATION processInfo;
			memset(&processInfo,0,sizeof(processInfo));
			wchar_t exe[_MAX_PATH]=L"%windir%\\system32\\rundll32.exe";
			DoEnvironmentSubst(exe,_countof(exe));
			if (CreateProcess(exe,cmdLine,NULL,NULL,FALSE,0,NULL,target,&startupInfo,&processInfo))
			{
				CloseHandle(processInfo.hThread);
				CloseHandle(processInfo.hProcess);
			}
		}
		else if (GetLastError()==ERROR_ACCESS_DENIED)
		{
			// there was a problem, most likely UAC didn't let us create a folder

			// create a temp folder just for us
			wchar_t temp[_MAX_PATH];
			GetTempPath(_countof(temp),temp);
			Strcat(temp,_countof(temp),L"OpenShell");
			CreateDirectory(temp,NULL);

			// make a unique link file and keep a handle to the file
			PathMakeUniqueName(fname,_countof(fname)-1,L"scut.lnk",L"New Shortcut.lnk",temp);

			HANDLE hFile=CreateFile(fname,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
			if (hFile!=INVALID_HANDLE_VALUE)
			{
				CloseHandle(hFile);
				// wait for the wizard to finish in a separate thread and close the menu
				// otherwise it appears behind the menu
				ShortcutParams *pParams=new ShortcutParams;
				memcpy(pParams->target,target,sizeof(target));
				memcpy(pParams->temp,temp,sizeof(temp));
				memcpy(pParams->fname,fname,sizeof(fname));
				CreateThread(NULL,0,NewShortcutThread,pParams,0,NULL);
			}
		}
		res=0;
	}

	if (res==CMD_DELETEMRU && item.id==MENU_RECENT && s_RecentPrograms!=RECENT_PROGRAMS_NONE)
	{
		if (s_RecentPrograms==RECENT_PROGRAMS_RECENT)
		{
			CComString pName;
			if (_bMetroApp)
				DeleteMRUAppId(_appId);
			else if (SUCCEEDED(pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName)))
				DeleteMRUShortcut(pName);
		}
		else if (s_RecentPrograms==RECENT_PROGRAMS_FREQUENT)
		{
			RemoveMFUShortcut(item.mfuHash,_bMetroApp);
		}
		PostRefreshMessage();
		res=0;
	}
	if (res==CMD_DELETEALL && item.id==MENU_RECENT && s_RecentPrograms==RECENT_PROGRAMS_RECENT)
	{
		DeleteMRUShortcut(NULL);
		PostRefreshMessage();
		res=0;
	}

	if (res==CMD_EXPLORE)
	{
		if (item.id==MENU_SEARCH_CATEGORY)
		{
				for (std::list<CSearchManager::SearchCategory>::const_iterator it=s_SearchResults.indexed.begin();it!=s_SearchResults.indexed.end();++it)
				{
					if (item.categoryHash==it->categoryHash)
					{
						FadeOutItem(index);
						PlayMenuSound(SOUND_COMMAND);
						g_SearchManager.LaunchExternalSearch(it->search,it->categoryHash,searchText);
						break;
					}
				}
		}
		else
		{
			SHOpenFolderAndSelectItems(pItemPidl1,0,NULL,0);
		}
		res=0;
	}

	if (res==CMD_UNINSTALL)
	{
		UninstallMetroApp(g_OwnerWindow,item.name,_appId);
		res=0;
	}

	if (item.id==MENU_SEARCH_CATEGORY && res==CMD_TOGGLE)
	{
		m_SearchCategoryHash=(m_SearchCategoryHash==item.categoryHash)?CSearchManager::CATEGORY_INVALID:item.categoryHash;
		RefreshSearch();
		res=0;
	}

	// handle the shell commands
	if (res>=CMD_LAST)
	{
		// handle special verbs
		char command[256];
		if (FAILED(pMenu->GetCommandString(res-CMD_LAST,GCS_VERBA,NULL,command,_countof(command))))
			command[0]=0;
		if (_stricmp(command,"rename")==0)
		{
			// show the Rename dialog box
			CComPtr<IShellFolder> pFolder; // have to use IShellFolder for renaming because it's the only one that supports changing the display name
			PCUITEMID_CHILD pidl;
			s_bPreventClosing=true;
			STRRET str;
			if (SUCCEEDED(SHBindToParent(pItemPidl1,IID_IShellFolder,(void**)&pFolder,&pidl)) && SUCCEEDED(pFolder->GetDisplayNameOf(pidl,SHGDN_FOREDITING,&str)))
			{
				CComString pName;
				StrRetToStr(&str,pidl,&pName);
				g_RenameText=pName;
			}
			else
				g_RenameText=item.name;
			if (pPt)
				g_RenamePos=*pPt;
			else
			{
				g_RenamePos.x=item.itemRect.left;
				g_RenamePos.y=item.itemRect.top;
				ClientToScreen(&g_RenamePos);
			}
			for (std::vector<CMenuContainer*>::iterator it=s_Menus.begin();it!=s_Menus.end();++it)
				(*it)->EnableWindow(FALSE); // disable all menus
			bool bAllPrograms=s_bAllPrograms;
			if (bAllPrograms) ::EnableWindow(g_TopWin7Menu,FALSE);

			SetContextItem(index);
			InvalidateItem(index);
			bool bRenamed=DialogBox(g_Instance,MAKEINTRESOURCE(s_bRTL?IDD_RENAMER:IDD_RENAME),g_OwnerWindow,RenameDlgProc)!=0;
			SetContextItem(-1);
			if (m_HotItem<0) SetHotItem(index);

			if (bRenamed)
			{
				if (GetWinVersion()>=WIN_VER_WIN8)
				{
					SetForegroundWindow(m_hWnd);
					SetActiveWindow();
					if (pData && pData->bProgramsTree)
						m_pProgramsTree->SetFocus();
					else
						SetFocus();
				}
				// perform the rename operation
				PITEMID_CHILD newPidl;
				if (SUCCEEDED(pFolder->SetNameOf(g_OwnerWindow,pidl,g_RenameText,SHGDN_INFOLDER,&newPidl)))
				{
					STRRET str;
					if (SUCCEEDED(pFolder->GetDisplayNameOf(newPidl,SHGDN_INFOLDER|SHGDN_FORPARSING,&str)))
					{
						CComString pName;
						StrRetToStr(&str,newPidl,&pName);
						pName.MakeUpper();
						m_Items[index].name=g_RenameText;
						m_Items[index].nameHash=CalcFNVHash(pName);

						if (!(m_Options&CONTAINER_AUTOSORT) && (!pData || !pData->bProgramsTree))
						{
							std::vector<SortMenuItem> items;
							for (int i=0;i<m_OriginalCount;i++)
							{
								if (m_Items[i].id==MENU_NO)
								{
									SortMenuItem item(m_Items[i]);
									items.push_back(item);
								}
							}
							SaveItemOrder(items);
						}

						// win7: if this is a pinned link with implicit app id, make it explicit - otherwise the app resolver will generate a new one
						if (!m_bSubMenu && item.id==MENU_NO && GetWinVersion()<=WIN_VER_WIN7 && !_appId.IsEmpty() && _bIsLink && !_bExplicitAppId && SUCCEEDED(pFolder->GetDisplayNameOf(newPidl,SHGDN_FORPARSING,&str)))
						{
							CComString pPath;
							StrRetToStr(&str,newPidl,&pPath);
							CComPtr<IShellLink> pLink;
							pLink.CoCreateInstance(CLSID_ShellLink);
							CComQIPtr<IPersistFile> pFile(pLink);
							if (pFile && SUCCEEDED(pFile->Load(pPath,STGM_READWRITE)))
							{
								CComQIPtr<IPropertyStore> pStore(pLink);
								if (pStore)
								{
									PROPVARIANT val;
									InitPropVariantFromString(_appId,&val);
									if (SUCCEEDED(pStore->SetValue(PKEY_AppUserModel_ID,val)) && SUCCEEDED(pStore->Commit()))
										pFile->Save(pPath,TRUE);
									PropVariantClear(&val);
								}
							}
						}

						if (pData)
						{
							CComPtr<IShellItem> pNewItem;
							if (SUCCEEDED(SHCreateItemWithParent(NULL,pFolder,newPidl,IID_IShellItem,(void**)&pNewItem)))
							{
								CAbsolutePidl newAbsPidl;
								if (SUCCEEDED(SHGetIDListFromObject(pNewItem,&newAbsPidl)))
									pData->pNewItemInfo=g_ItemManager.GetItemInfo(pNewItem,newAbsPidl,0);
							}
						}
					}
					ILFree(newPidl);
				}
				if (!pData || !pData->bProgramsTree)
					PostRefreshMessage();
			}
			for (std::vector<CMenuContainer*>::iterator it=s_Menus.begin();it!=s_Menus.end();++it)
				if (!(*it)->m_bDestroyed)
					(*it)->EnableWindow(TRUE); // enable all menus
			if (bAllPrograms) ::EnableWindow(g_TopWin7Menu,TRUE);
			if (!m_bDestroyed)
			{
				SetForegroundWindow(m_hWnd);
				SetActiveWindow();
				if (pData && pData->bProgramsTree)
					m_pProgramsTree->SetFocus();
				else
					SetFocus();
			}
			s_bPreventClosing=false;
			s_HotPos=GetMessagePos();
			res=CMD_RENAME;
		}
		else if (_stricmp(command,"uninstall")==0 && _bMetroApp && !_appId.IsEmpty())
		{
			UninstallMetroApp(g_OwnerWindow,item.name,_appId);
		}
		else
		{
			bool bRefreshMain=_stricmp(command,"pin_classic")==0;
			bool bRefresh=(_stricmp(command,"delete")==0 || _stricmp(command,"link")==0);

			if (item.bStartScreen && _stricmp(command,"pin_classic")==0)
			{
				{
					CSettingsLockWrite lock;
					CSetting *pSetting=FindSetting(L"StartScreenShortcut");
					if (!pSetting->IsLocked())
					{
						pSetting->value=CComVariant(0);
						pSetting->flags&=~CSetting::FLAG_DEFAULT;
					}
				}
				SaveSettings();
			}

			IContextMenu *pInvokeMenu=pMenu;
			int verbOffset=CMD_LAST;
			if (pSecondaryMenu && res>=secondaryCmd)
			{
				pInvokeMenu=pSecondaryMenu;
				verbOffset=secondaryCmd;
			}
			CMINVOKECOMMANDINFOEX info={sizeof(info),CMIC_MASK_UNICODE|CMIC_MASK_FLAG_LOG_USAGE};
			info.lpVerb=MAKEINTRESOURCEA(res-verbOffset);
			info.lpVerbW=MAKEINTRESOURCEW(res-verbOffset);
			info.nShow=SW_SHOWNORMAL;
			bool bOpenTruePath=false;
			wchar_t targetlnkPath[_MAX_PATH]; // path to target.lnk in a fake folder
			wchar_t dir[_MAX_PATH];
			if (SHGetPathFromIDList(pItemPidl1,dir))
			{
				if (_stricmp(command,"open")==0 && GetSettingBool(L"OpenTruePath") && GetFakeFolder(targetlnkPath,_countof(targetlnkPath),dir))
					bOpenTruePath=true;
				else
				{
					PathRemoveFileSpec(dir);
					if (GetFileAttributes(dir)!=INVALID_FILE_ATTRIBUTES)
						info.lpDirectoryW=dir;
				}
			}
			if (pPt)
			{
				info.fMask|=CMIC_MASK_PTINVOKE;
				info.ptInvoke=*pPt;
			}
			if (type==ACTIVATE_MENU)
			{
				if (bCtrl) info.fMask|=CMIC_MASK_CONTROL_DOWN;
				if (bShift) info.fMask|=CMIC_MASK_SHIFT_DOWN;
			}

			if (bRefresh || bRefreshMain)
				info.fMask|=CMIC_MASK_NOASYNC; // wait for delete/link commands to finish so we can refresh the menu

			// we don't want our virtual folder to appear in Explorer's frequent list
			if (item.pItemInfo && wcsncmp(item.pItemInfo->PATH, L"::{82E749ED-B971-4550-BAF7-06AA2BF7E836}", 40) == 0)
				info.fMask &= ~CMIC_MASK_FLAG_LOG_USAGE;

			s_bPreventClosing=true;
			for (auto& it : s_Menus)
			{
				it->EnableWindow(FALSE); // disable all menus
				it->SetWindowPos(HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			}
			bool bAllPrograms=s_bAllPrograms;
			if (bAllPrograms) ::EnableWindow(g_TopWin7Menu,FALSE);
			info.hwnd=g_OwnerWindow;

			RECT rc;
			GetWindowRect(&rc);
			::SetForegroundWindow(g_OwnerWindow);
			::SetWindowPos(g_OwnerWindow,HWND_TOPMOST,rc.left,rc.top,rc.right-rc.left,rc.bottom-rc.top,0);
			LOG_MENU(LOG_EXECUTE,L"Invoke command, ptr=%p, command='%S'",this,command);
			bool executeSuccess;
			if (bOpenTruePath) // we are trying to open a fake folder, directly open target.lnk instead
			{
				HINSTANCE hinst=ShellExecute(NULL,NULL,targetlnkPath,NULL,NULL,SW_SHOWNORMAL);
				LOG_MENU(LOG_EXECUTE,L"Invoke command, ptr=%p, res=%d",this,hinst);
				executeSuccess=static_cast<int>(reinterpret_cast<uintptr_t>(hinst))>=32;
			}
			else
			{
				HRESULT hr=pInvokeMenu->InvokeCommand((LPCMINVOKECOMMANDINFO)&info);
				LOG_MENU(LOG_EXECUTE,L"Invoke command, ptr=%p, res=%d",this,hr);
				executeSuccess=SUCCEEDED(hr);
			}
			if (type==ACTIVATE_EXECUTE && executeSuccess)
			{
				if (bTrackRecent)
				{
					if (_bMetroApp)
						AddMRUAppId(_appId);
					else if (_path.IsEmpty())
					{
						CComString pName;
						if (SUCCEEDED(pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName)))
							AddMRUShortcut(pName);
					}
					else
						AddMRUShortcut(_path);
				}
				g_ItemManager.RemoveNewItem(pItemPidl1,NULL,false);
				if (!(m_Options&CONTAINER_LINK))
				{
					// update item ranks
					CComString pName;
					if (_bMetroApp)
					{
						CString APPID=_appId;
						APPID.MakeUpper();
						g_SearchManager.AddItemRank(CalcFNVHash(APPID));
					}
					else if (SUCCEEDED(pItem->GetDisplayName(SIGDN_PARENTRELATIVEPARSING,&pName))) // can't use item.name because the extension may be removed
					{
						pName.MakeUpper();
						g_SearchManager.AddItemRank(CalcFNVHash(pName));
					}
				}
			}
			for (auto& it : s_Menus)
			{
				if (!it->m_bDestroyed)
				{
					it->EnableWindow(TRUE); // enable all menus
					it->SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				}
			}
			if (bAllPrograms) ::EnableWindow(g_TopWin7Menu,TRUE);
			if (bRefreshMain && m_bSubMenu)
			{
				CMenuContainer *pMain=s_Menus[0];
				if (!pMain->m_bSubMenu && !pMain->m_bDestroyed)
				{
					SetForegroundWindow(pMain->m_hWnd);
					pMain->SetActiveWindow();
					pMain->SetFocus();
					CloseSubMenus(CLOSE_POST,pMain);
					pMain->PostRefreshMessage();
				}
			}
			else if ((bRefresh || bKeepOpen || bRefreshMain) && !m_bDestroyed)
			{
				SetForegroundWindow(m_hWnd);
				SetActiveWindow();
				if (m_Options&CONTAINER_SEARCH)
				{
					m_pParent->m_SearchBox.SetFocus();
					SetWindowPos(HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
				}
				else if (pData && pData->bProgramsTree)
					m_pProgramsTree->SetFocus();
				else
					SetFocus();
			}
			s_bPreventClosing=false;

			if (!bKeepOpen && !bRefresh && !bRefreshMain)
			{
				HWND active=GetActiveWindow();
				if (active!=m_hWnd && active!=g_OwnerWindow)
				{
					// if after all the window is not active, then another application was launched - close all menus
					CloseSubMenus(CLOSE_POST,NULL);
					if (g_TopWin7Menu && s_bAllPrograms) ::PostMessage(g_TopWin7Menu,WM_CLOSE,0,0);
				}
			}
			if (_stricmp(command,"delete")==0)
				res=CMD_DELETE;

			if ((bRefresh && (!pData || !pData->bProgramsTree)) || (bRefreshMain && !m_bSubMenu))
			{
				if (bRefreshMain && !m_bSubMenu && (s_MenuMode==MODE_SEARCH || s_MenuMode==MODE_JUMPLIST))
					m_bRefreshItems=true;
				else
					PostRefreshMessage(); // refresh the menu after an item was deleted or created
			}
		}
	}
	DestroyMenu(menu);
	s_HotPos=GetMessagePos();
	if (pData && res) pData->command=res;
	LOG_MENU(LOG_EXECUTE,L"Exit activate, ptr=%p",this);
	guard.Disarm();
}

void CMenuContainer::ActivateTreeItem( const void *treeItem, RECT &itemRect, TActivateType type, const POINT *pPt, ActivateData *pData )
{
	AddRef(); // prevent the menu from being deleted while processing the operation
	Assert(pData && pData->bProgramsTree);
	const CProgramsTree::CTreeItem *pTreeItem=(CProgramsTree::CTreeItem*)treeItem;
	MenuItem &item=m_Items[m_ProgramTreeIndex];
	item.id=MENU_NO;
	if (pTreeItem->bApps)
		item.id=MENU_APPS;
	else if (pTreeItem->bEmpty)
		item.id=MENU_EMPTY;
	item.name=pTreeItem->name;
	item.pItemInfo=pTreeItem->pItemInfo1;
	RECT rc=item.itemRect;
	item.itemRect=itemRect;
	item.pItem1=pTreeItem->pItemInfo1?(PIDLIST_ABSOLUTE)pTreeItem->pItemInfo1->GetPidl():NULL;
	item.pItem2=pTreeItem->pItemInfo2?(PIDLIST_ABSOLUTE)pTreeItem->pItemInfo2->GetPidl():NULL;
	item.bFolder=pTreeItem->bFolder;
	item.bMetroLink=false;
	item.bMetroApp=false;
	item.bNew=pTreeItem->bNew;
	if (pTreeItem->pItemInfo1)
	{
		CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
		item.bMetroLink=pTreeItem->pItemInfo1->IsMetroLink();
		item.bMetroApp=pTreeItem->pItemInfo1->IsMetroApp();
	}
	pData->bApps=pTreeItem->bApps;
	ActivateItem(m_ProgramTreeIndex,type,pPt,pData);
	item.id=MENU_PROGRAMS_TREE;
	item.itemRect=rc;
	item.name.Empty();
	item.pItemInfo=NULL;
	item.pItem1=NULL;
	item.pItem2=NULL;
	Release();
}

void CMenuContainer::DragTreeItem( const void *treeItem, bool bApp )
{
	const CProgramsTree::CTreeItem *pTreeItem=(CProgramsTree::CTreeItem*)treeItem;
	MenuItem &item=m_Items[m_ProgramTreeIndex];
	item.id=MENU_NO;
	item.name=pTreeItem->name;
	item.pItemInfo=pTreeItem->pItemInfo1;
	item.pItem2=pTreeItem->pItemInfo2?(PIDLIST_ABSOLUTE)pTreeItem->pItemInfo2->GetPidl():NULL;
	item.bFolder=pTreeItem->bFolder;
	item.bMetroLink=false;
	item.bMetroApp=false;
	if(item.pItemInfo)
	{
		item.pItem1=pTreeItem->pItemInfo1->GetPidl();
		CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
		item.bMetroLink=item.pItemInfo->IsMetroLink();
		item.bMetroApp=item.pItemInfo->IsMetroApp();
	}
	DragOut(m_ProgramTreeIndex,bApp);
	item.id=MENU_PROGRAMS_TREE;
	item.name.Empty();
	item.pItemInfo=NULL;
	item.pItem1=NULL;
	item.pItem2=NULL;
}

void CMenuContainer::RunUserCommand( bool bPicture )
{
	CString command=GetSettingString(bPicture?L"UserPictureCommand":L"UserNameCommand");
	if (!command.IsEmpty())
		ExecuteCommand(command,false,true);
}

static DWORD WINAPI FaderThreadProc( void *param )
{
	((CMenuFader*)param)->Create();
	MSG msg;
	while (GetMessage(&msg,NULL,0,0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return 0;
}

void CMenuContainer::FadeOutItem( int index )
{
	int speed=GetSettingInt(L"MenuFadeSpeed");
	if (speed<=0) return;

	RECT rc;

	HBITMAP bmp=NULL;
	HRGN region=NULL;
	if (index==m_ProgramTreeIndex)
	{
		HWND tree=m_pProgramsTree->m_hWnd;
		HTREEITEM hItem=TreeView_GetSelection(tree);
		TreeView_GetItemRect(tree,hItem,&rc,FALSE);

		BITMAPINFO dib={sizeof(dib)};
		dib.bmiHeader.biWidth=rc.right-rc.left;
		dib.bmiHeader.biHeight=rc.top-rc.bottom;
		dib.bmiHeader.biPlanes=1;
		dib.bmiHeader.biBitCount=32;
		dib.bmiHeader.biCompression=BI_RGB;

		HDC hdc=CreateCompatibleDC(NULL);
		if (s_bRTL) SetLayout(hdc,LAYOUT_RTL);
		unsigned int *bits;
		bmp=CreateDIBSection(hdc,&dib,DIB_RGB_COLORS,(void**)&bits,NULL,0);
		HGDIOBJ bmp0=SelectObject(hdc,bmp);
		SetViewportOrgEx(hdc,-rc.left,-rc.top,NULL);
		m_pProgramsTree->SendMessage(WM_PRINTCLIENT,(WPARAM)hdc,PRF_CLIENT);
		SelectObject(hdc,bmp0);
		DeleteDC(hdc);
		m_pProgramsTree->MapWindowPoints(NULL,&rc);
	}
	else
	{
		GetItemRect(index,rc);
		BITMAPINFO dib={sizeof(dib)};
		dib.bmiHeader.biWidth=rc.right-rc.left;
		dib.bmiHeader.biHeight=rc.top-rc.bottom;
		dib.bmiHeader.biPlanes=1;
		dib.bmiHeader.biBitCount=32;
		dib.bmiHeader.biCompression=BI_RGB;

		HDC hdc=CreateCompatibleDC(NULL);
		if (s_bRTL) SetLayout(hdc,LAYOUT_RTL);
		unsigned int *bits;
		bmp=CreateDIBSection(hdc,&dib,DIB_RGB_COLORS,(void**)&bits,NULL,0);
		HGDIOBJ bmp0=SelectObject(hdc,bmp);
		SetViewportOrgEx(hdc,-rc.left,-rc.top,NULL);

		// create a region from the opaque pixels of the selection bitmap
		MenuSkin::TItemDrawType drawType=m_Items[index].drawType;
		if (drawType==MenuSkin::COLUMN1_NEW)
			drawType=MenuSkin::COLUMN1_ITEM;
		else if (drawType==MenuSkin::COLUMN2_NEW)
			drawType=MenuSkin::COLUMN2_ITEM;
		else if (drawType==MenuSkin::SUBMENU_NEW)
			drawType=MenuSkin::SUBMENU_ITEM;
		const MenuSkin::ItemDrawSettings &settings=s_Skin.ItemSettings[drawType];
		if (settings.bmpSelection.GetBitmap() && settings.bmpSelection.bIs32)
		{
			HDC hdc2=CreateCompatibleDC(hdc);
			SetLayout(hdc2,0);
			HGDIOBJ bmp02=SelectObject(hdc2,settings.bmpSelection.GetBitmap());
			FillRect(hdc,&rc,(HBRUSH)GetStockObject(WHITE_BRUSH));
			RECT rSrc={0,0,settings.selSlicesX[0]+settings.selSlicesX[1]+settings.selSlicesX[2],settings.selSlicesY[0]+settings.selSlicesY[1]+settings.selSlicesY[2]};
			RECT rMargins={settings.selSlicesX[0],settings.selSlicesY[0],settings.selSlicesX[2],settings.selSlicesY[2]};
			if (m_Items[index].id==MENU_SHUTDOWN_BUTTON)
			{
				rSrc.right+=settings.selSlicesX[3]+settings.selSlicesX[4]+settings.selSlicesX[5];
				rMargins.right=settings.selSlicesX[5];
			}
			int w=dib.bmiHeader.biWidth;
			int h=-dib.bmiHeader.biHeight;
			if (rMargins.left>w) rMargins.left=w;
			if (rMargins.right>w) rMargins.right=w;
			if (rMargins.top>h) rMargins.top=h;
			if (rMargins.bottom>h) rMargins.bottom=h;
			MarginsBlit(hdc2,hdc,rSrc,rc,rMargins,false);
			SelectObject(hdc2,bmp02);
			DeleteDC(hdc2);
			SelectObject(hdc,bmp0);

			for (int y=0;y<h;y++)
			{
				int minx=-1, maxx=-1;
				int yw=y*w;
				for (int x=0;x<w;x++)
				{
					if ((bits[yw+x]>>24)>=32)
					{
						if (minx==-1) minx=x; // first non-transparent pixel
						if (maxx<x) maxx=x; // last non-transparent pixel
					}
				}
				if (minx>=0)
				{
					maxx++;
					HRGN r=CreateRectRgn(minx,y,maxx,y+1);
					AddTrackedObject(r);
					if (!region)
						region=r;
					else
					{
						CombineRgn(region,region,r,RGN_OR);
						DeleteObject(r);
					}
				}
			}

			SelectObject(hdc,bmp);
		}

		DrawBackground(hdc,rc);

		SelectObject(hdc,bmp0);
		DeleteDC(hdc);
		MapWindowPoints(NULL,&rc);
	}

	if (bmp)
	{
		CMenuFader *pFader=new CMenuFader(bmp,region,speed,rc);
		CreateThread(NULL,0,FaderThreadProc,pFader,0,NULL);
	}
}

///////////////////////////////////////////////////////////////////////////////

CMenuFader::CMenuFader( HBITMAP bmp, HRGN region, int duration, RECT &rect )
{
	m_Bitmap=bmp;
	m_Region=region;
	m_Duration=duration;
	m_Rect=rect;
	s_Faders.push_back(this);
}

CMenuFader::~CMenuFader( void )
{
	if (m_Bitmap) DeleteObject(m_Bitmap);
	if (m_Region) DeleteObject(m_Region);
	s_Faders.erase(std::find(s_Faders.begin(),s_Faders.end(),this));
}

void CMenuFader::Create( void )
{
	bool bRtl=false;
	if (m_Rect.left>m_Rect.right)
	{
		bRtl=true;
		int q=m_Rect.left; m_Rect.left=m_Rect.right; m_Rect.right=q;
	}
	CWindowImpl<CMenuFader>::Create(NULL,&m_Rect,NULL,WS_POPUP,WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_LAYERED|(bRtl?WS_EX_LAYOUTRTL:0));
	ShowWindow(SW_SHOWNOACTIVATE);
	if (m_Region)
	{
		SetWindowRgn(m_Region);
		m_Region=NULL;
	}
	SetTimer(1,20);
	m_Time0=0;
	m_LastTime=0;
	PostMessage(WM_TIMER,0,0);
	SetLayeredWindowAttributes(m_hWnd,0,255,LWA_ALPHA);
}

LRESULT CMenuFader::OnEraseBkgnd( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	RECT rc;
	GetClientRect(&rc);
	HDC hdc=(HDC)wParam;

	// draw the background
	HDC hdc2=CreateCompatibleDC(hdc);
	HGDIOBJ bmp0=SelectObject(hdc2,m_Bitmap);
	BitBlt(hdc,0,0,rc.right,rc.bottom,hdc2,0,0,SRCCOPY);
	SelectObject(hdc2,bmp0);
	DeleteDC(hdc2);
	return 1;
}

LRESULT CMenuFader::OnTimer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_Time0==0)
		m_Time0=GetMessageTime();
	int t=GetMessageTime()-m_Time0;
	const int MAX_DELTA=80; // allow at most 80ms between redraws. if more, slow down time
	if (t>MAX_DELTA+m_LastTime)
	{
		m_Time0+=t-MAX_DELTA-m_LastTime;
		t=MAX_DELTA+m_LastTime;
	}
	m_LastTime=t;
	if (t<m_Duration)
	{
		SetLayeredWindowAttributes(m_hWnd,0,(m_Duration-t)*255/m_Duration,LWA_ALPHA);
		RedrawWindow();
	}
	else
	{
		KillTimer(1);
		PostMessage(WM_CLOSE);
	}
	return 0;
}

void CMenuFader::ClearAll( void )
{
	while (!s_Faders.empty())
		s_Faders[0]->SendMessage(WM_CLOSE);
}
