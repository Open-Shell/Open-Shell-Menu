// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

// MenuContainer.cpp - contains the main logic of CMenuContainer

#include "stdafx.h"
#include "MenuContainer.h"
#include "Accessibility.h"
#include "StartMenuDLL.h"
#include "StartButton.h"
#include "Settings.h"
#include "Translations.h"
#include "CustomMenu.h"
#include "LogManager.h"
#include "FNVHash.h"
#include "ResourceHelper.h"
#include "SettingsUIHelper.h"
#include "DownloadHelper.h"
#include "SettingsUI.h"
#include "MetroLinkManager.h"
#include "ProgramsTree.h"
#include "dllmain.h"
#include "resource.h"
#include <uxtheme.h>
#include <vsstyle.h>
#include <vssym32.h>
#include <WtsApi32.h>
#include <Lm.h>
#include <Dsgetdc.h>
#include <PowrProf.h>
#include <dwmapi.h>
#include <propkey.h>
#define SECURITY_WIN32
#include <Security.h>
#include <algorithm>
#include <wuapi.h>

struct StdMenuOption
{
	TMenuID id;
	int options;
};

// Options for special menu items
enum
{
	MENU_NONE     = 0,
	MENU_ENABLED  = 1, // the item shows in the menu
	MENU_EXPANDED = 2, // the item is expanded
};

static StdMenuOption g_StdOptions[]=
{
	{MENU_COMPUTER,MENU_NONE}, // MENU_ENABLED|MENU_EXPANDED from settings
	{MENU_FAVORITES,MENU_NONE}, // MENU_ENABLED|MENU_EXPANDED from settings, check policy
	{MENU_DOCUMENTS,MENU_NONE}, // MENU_ENABLED|MENU_EXPANDED from settings, check policy
	{MENU_HELP,MENU_ENABLED}, // check policy
	{MENU_RUN,MENU_ENABLED}, // check policy
	{MENU_LOGOFF,MENU_ENABLED}, // MENU_ENABLED from settings, check policy
	{MENU_RESTART,MENU_ENABLED}, // MENU_ENABLED
	{MENU_RESTART_NOUPDATE,MENU_ENABLED}, // only available when there are updates
	{MENU_DISCONNECT,MENU_NONE}, // MENU_ENABLED if in a remote session, check policy
	{MENU_SHUTDOWN_BOX,MENU_ENABLED}, // MENU_NONE if in a remote session, check policy
	{MENU_SHUTDOWN_BUTTON,MENU_ENABLED|MENU_EXPANDED}, // MENU_ENABLED|MENU_EXPANDED from the settings
	{MENU_SHUTDOWN,MENU_ENABLED}, // MENU_NONE if in a remote session, check policy
	{MENU_SHUTDOWN_NOUPDATE,MENU_ENABLED}, // only available when there are updates
	{MENU_UNDOCK,MENU_ENABLED}, // from settings, check policy
	{MENU_CONTROLPANEL,MENU_ENABLED|MENU_EXPANDED}, // MENU_EXPANDED from settings, check policy
	{MENU_NETWORK,MENU_ENABLED}, // MENU_EXPANDED from settings, check policy
	{MENU_SECURITY,MENU_ENABLED}, // MENU_ENABLED if in a remote session
	{MENU_PRINTERS,MENU_ENABLED}, // MENU_EXPANDED from settings, check policy
	{MENU_TASKBAR,MENU_ENABLED}, // check policy
	{MENU_FEATURES,MENU_ENABLED}, // no setting (prevents the Programs and Features from expanding), check policy (for control panel)
	{MENU_CLASSIC_SETTINGS,MENU_ENABLED}, // MENU_ENABLED from ini file
	{MENU_SEARCH,MENU_ENABLED}, // check policy
	{MENU_SEARCH_BOX,MENU_NONE}, // check settings
	{MENU_SEARCH_PRINTER,MENU_NONE}, // MENU_ENABLED if Active Directory is available
	{MENU_SEARCH_COMPUTERS,MENU_NONE}, // MENU_ENABLED if Active Directory is available, check policy
	{MENU_SEARCH_PEOPLE,MENU_NONE}, // MENU_ENABLED if %ProgramFiles%\Windows Mail\wab.exe exists
	{MENU_USERFILES,MENU_ENABLED}, // check policy
	{MENU_USERDOCUMENTS,MENU_ENABLED}, // check policy
	{MENU_USERPICTURES,MENU_ENABLED}, // check policy
	{MENU_SLEEP,MENU_ENABLED}, // check power caps
	{MENU_HIBERNATE,MENU_ENABLED}, // check power caps
	{MENU_LOCK,MENU_ENABLED}, // check power settings
	{MENU_SWITCHUSER,MENU_ENABLED}, // check group policy
	{MENU_APPS,MENU_ENABLED}, // enable on Win8+
	{MENU_PCSETTINGS,MENU_ENABLED}, // enable on Win8+
};

///////////////////////////////////////////////////////////////////////////////

void CUserWindow::Init( CMenuContainer *pOwner )
{
	const MenuSkin &skin=CMenuContainer::s_Skin;
	m_pOwner=pOwner;
	m_Size=skin.User_bitmapSize;
	BITMAPINFO bi={0};
	bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth=m_Size.cx;
	bi.bmiHeader.biHeight=m_Size.cy;
	bi.bmiHeader.biPlanes=1;
	bi.bmiHeader.biBitCount=32;
	HDC hdc=CreateCompatibleDC(NULL);
	m_Bitmap=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,(void**)&m_Bits,NULL,0);
	DeleteDC(hdc);
	memset(m_Bits,0,m_Size.cx*m_Size.cy*4);
	m_Source.resize(m_Size.cx*m_Size.cy*2);
	m_bUserBitmapMask=(skin.User_mask.bIsBitmap && skin.User_mask.bIs32 && skin.User_maskSize.cx==skin.User_image_size && skin.User_maskSize.cy==skin.User_image_size);
	if (m_bUserBitmapMask)
		m_UserBitmap=CMenuContainer::LoadUserImage(skin.User_image_size,skin.User_mask.GetBitmap());
	else
		m_UserBitmap=CMenuContainer::LoadUserImage(skin.User_image_size,NULL);
	m_TimerBitmap=(HBITMAP)1;
	SetImage(NULL,false);
}

void CUserWindow::StartImageTimer( HBITMAP bmp )
{
	if (m_TimerBitmap==bmp) return;
	m_TimerBitmap=bmp;
	DWORD time;
	SystemParametersInfo(SPI_GETMOUSEHOVERTIME,NULL,&time,0);
	SetTimer(TIMER_SET,time);
}

void CUserWindow::SetImage( HBITMAP bmp, bool bAnimate )
{
	m_bDefaultImage=!bmp;
	EnableWindow(m_bDefaultImage);
	int size=m_Size.cx*m_Size.cy;
	memcpy(&m_Source[0],m_Bits,size*4);

	HDC hdst=CreateCompatibleDC(NULL);
	RECT rc={0,0,m_Size.cx,m_Size.cy};
	HGDIOBJ bmp0=SelectObject(hdst,m_Bitmap);
	FillRect(hdst,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
	HDC hsrc=CreateCompatibleDC(NULL);
	HGDIOBJ bmp01=GetCurrentObject(hsrc,OBJ_BITMAP);
	int iconSize=bmp?CItemManager::EXTRA_LARGE_ICON_SIZE:CMenuContainer::s_Skin.User_image_size;
	if (bmp)
	{
		SelectObject(hsrc,bmp);
		BitBlt(hdst,(m_Size.cx-iconSize)/2,(m_Size.cy-iconSize)/2,iconSize,iconSize,hsrc,0,0,SRCCOPY);
	}
	else
	{
		if (m_UserBitmap)
		{
			SelectObject(hsrc,m_UserBitmap);
			BitBlt(hdst,CMenuContainer::s_Skin.User_image_offset.x,CMenuContainer::s_Skin.User_image_offset.y,iconSize,iconSize,hsrc,0,0,SRCCOPY);
		}
		if (CMenuContainer::s_Skin.User_bitmap.GetBitmap())
		{
			SelectObject(hsrc,CMenuContainer::s_Skin.User_bitmap.GetBitmap());
			BLENDFUNCTION func={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
			AlphaBlend(hdst,0,0,m_Size.cx,m_Size.cy,hsrc,0,0,m_Size.cx,m_Size.cy,func);
		}
	}
	SelectObject(hsrc,bmp01);
	DeleteDC(hsrc);
	SelectObject(hdst,bmp0);
	DeleteDC(hdst);

	memcpy(&m_Source[size],m_Bits,size*4);
	if (!bmp && !m_bUserBitmapMask)
	{
		int dx=CMenuContainer::s_Skin.User_image_offset.x;
		int dy=m_Size.cy-CMenuContainer::s_Skin.User_image_offset.y-1;
		for (int y=0;y<iconSize;y++)
			for (int x=0;x<iconSize;x++)
				m_Source[size+(dy-y)*m_Size.cx+x+dx]|=0xFF000000;
	}
	if (bAnimate)
	{
		m_Timer=0;
		SetTimer(TIMER_BLEND,15);
	}
	else
	{
		memcpy(m_Bits,&m_Source[size],size*4);
		Update();
	}
}

void CUserWindow::Update( int alpha )
{
	BLENDFUNCTION func={AC_SRC_OVER,0,(BYTE)alpha,AC_SRC_ALPHA};

	HDC hSrc=CreateCompatibleDC(NULL);
	HGDIOBJ bmp0=SelectObject(hSrc,m_Bitmap);
	POINT srcPos={0,0};
	UpdateLayeredWindow(m_hWnd,NULL,NULL,&m_Size,hSrc,&srcPos,0,&func,ULW_ALPHA);
	SelectObject(hSrc,bmp0);
	DeleteDC(hSrc);
}

void CUserWindow::UpdatePartial( POINT pos, const RECT *pClipRect )
{
	BLENDFUNCTION func={AC_SRC_OVER,0,255,AC_SRC_ALPHA};

	SIZE size=m_Size;
	POINT srcPos={0,0};
	if (pClipRect)
	{
		if (pos.x<pClipRect->left)
		{
			int dx=pClipRect->left-pos.x;
			pos.x+=dx;
			size.cx-=dx;
			srcPos.x+=dx;
		}
		if (pos.x+size.cx>pClipRect->right)
			size.cx=pClipRect->right-pos.x;

		if (pos.y<pClipRect->top)
		{
			int dy=pClipRect->top-pos.y;
			pos.y+=dy;
			size.cy-=dy;
			srcPos.y+=dy;
		}
		if (pos.y+size.cy>pClipRect->bottom)
			size.cy=pClipRect->bottom-pos.y;
	}
	if (size.cx<0) size.cx=0;
	if (size.cy<0) size.cy=0;

	HDC hSrc=CreateCompatibleDC(NULL);
	HGDIOBJ bmp0=SelectObject(hSrc,m_Bitmap);
	UpdateLayeredWindow(m_hWnd,NULL,&pos,&size,hSrc,&srcPos,0,&func,ULW_ALPHA);
	SelectObject(hSrc,bmp0);
	DeleteDC(hSrc);
}

LRESULT CUserWindow::OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_Bitmap) DeleteObject(m_Bitmap);
	m_Bitmap=NULL;
	if (m_UserBitmap) DeleteObject(m_UserBitmap);
	m_UserBitmap=NULL;
	return 0;
}

LRESULT CUserWindow::OnLButtonDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_bDefaultImage)
		m_pOwner->RunUserCommand(true);
	return 0;
}

LRESULT CUserWindow::OnSetCursor( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_bDefaultImage)
		SetCursor(LoadCursor(NULL,IDC_HAND));
	else
		bHandled=FALSE;
	return TRUE;
}

LRESULT CUserWindow::OnTimer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	const int BLEND_STEP=5;
	if (wParam==TIMER_BLEND)
	{
		m_Timer+=BLEND_STEP;
		if (m_Timer>100) m_Timer=100;

		// blend the two images
		int n=m_Size.cx*m_Size.cy;
		for (int i=0;i<n;i++)
		{
			unsigned int pixel1=m_Source[i];
			unsigned int pixel2=m_Source[i+n];
			int a1=(pixel1>>24);
			int r1=(pixel1>>16)&255;
			int g1=(pixel1>>8)&255;
			int b1=(pixel1)&255;
			int a2=(pixel2>>24);
			int r2=(pixel2>>16)&255;
			int g2=(pixel2>>8)&255;
			int b2=(pixel2)&255;
			int a=a1+(a2-a1)*m_Timer/100;
			int r=r1+(r2-r1)*m_Timer/100;
			int g=g1+(g2-g1)*m_Timer/100;
			int b=b1+(b2-b1)*m_Timer/100;
			m_Bits[i]=(a<<24)|(r<<16)|(g<<8)|b;
		}

		Update();
		if (m_Timer==100)
			KillTimer(TIMER_BLEND);
	}
	if (wParam==TIMER_SET)
	{
		SetImage(m_TimerBitmap,true);
		m_TimerBitmap=(HBITMAP)1;
		KillTimer(TIMER_SET);
	}
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

int CMenuContainer::s_MaxRecentDocuments=15;
int CMenuContainer::s_ScrollMenus=0;
bool CMenuContainer::s_bRTL=false;
bool CMenuContainer::s_bKeyboardCues=false;
bool CMenuContainer::s_bOverrideFirstDown=false;
bool CMenuContainer::s_bExpandRight=true;
TRecentPrograms CMenuContainer::s_RecentPrograms=RECENT_PROGRAMS_NONE;
bool CMenuContainer::s_bBehindTaskbar=true;
bool CMenuContainer::s_bShowTopEmpty=false;
bool CMenuContainer::s_bNoDragDrop=false;
bool CMenuContainer::s_bNoContextMenu=false;
bool CMenuContainer::s_bExpandLinks=false;
bool CMenuContainer::s_bSingleClickFolders=false;
bool CMenuContainer::s_bLogicalSort=false;
bool CMenuContainer::s_bExtensionSort=false;
bool CMenuContainer::s_bAllPrograms=false;
bool CMenuContainer::s_bNoCommonFolders=false;
bool CMenuContainer::s_bNoRun=false;
bool CMenuContainer::s_bNoClose=false;
bool CMenuContainer::s_bHasTouch=false;
char CMenuContainer::s_bActiveDirectory=-1;
bool CMenuContainer::s_bPreventClosing=false;
bool CMenuContainer::s_bDragClosed=false;
bool CMenuContainer::s_bTempHidden=false;
bool CMenuContainer::s_bDisableHover=false;
bool CMenuContainer::s_bHasUpdates=false;
CMenuContainer *CMenuContainer::s_pDragSource=NULL;
bool CMenuContainer::s_bDragFromTree;
bool CMenuContainer::s_bDragMovable;
bool CMenuContainer::s_bRightDrag;
bool CMenuContainer::s_bLockWorkArea;
bool CMenuContainer::s_bPendingSearchEnter;
std::vector<CMenuContainer*> CMenuContainer::s_Menus;
volatile HWND CMenuContainer::s_FirstMenu, CMenuContainer::s_SearchMenu;
CSearchManager::SearchResults CMenuContainer::s_SearchResults;
std::map<unsigned int,int> CMenuContainer::s_MenuScrolls;
CString CMenuContainer::s_MRUShortcuts[MRU_PROGRAMS_COUNT];
bool CMenuContainer::s_bMRULoaded=false;
const CItemManager::ItemInfo *CMenuContainer::s_JumpAppInfo;
CJumpList CMenuContainer::s_JumpList;
int CMenuContainer::s_TaskBarId;
HWND CMenuContainer::s_TaskBar;
HWND CMenuContainer::s_StartButton;	// custom start button (if any)
UINT CMenuContainer::s_TaskBarEdge;
RECT CMenuContainer::s_StartRect;
HWND CMenuContainer::s_LastFGWindow;
HTHEME CMenuContainer::s_Theme;
HTHEME CMenuContainer::s_PagerTheme;
CWindow CMenuContainer::s_Tooltip;
CWindow CMenuContainer::s_TooltipBalloon;
int CMenuContainer::s_TipShowTime;
int CMenuContainer::s_TipHideTime;
int CMenuContainer::s_TipShowTimeFolder;
int CMenuContainer::s_TipHideTimeFolder;
DWORD CMenuContainer::s_HotPos;
int CMenuContainer::s_HotItem;
CMenuContainer *CMenuContainer::s_pHotMenu;
int CMenuContainer::s_TipItem;
CMenuContainer *CMenuContainer::s_pTipMenu;
RECT CMenuContainer::s_MenuLimits;
RECT CMenuContainer::s_MainMenuLimits;
DWORD CMenuContainer::s_TaskbarState;
DWORD CMenuContainer::s_HoverTime;
DWORD CMenuContainer::s_SplitHoverTime;
DWORD CMenuContainer::s_ProgramsHoverTime;
DWORD CMenuContainer::s_XMouse;
DWORD CMenuContainer::s_SubmenuStyle;
CLIPFORMAT CMenuContainer::s_ShellFormat;
CLIPFORMAT CMenuContainer::s_ShellUrlFormat;
CLIPFORMAT CMenuContainer::s_DescriptorFormat;
CLIPFORMAT CMenuContainer::s_ContentsFormat;
CLIPFORMAT CMenuContainer::s_MetroLinkFormat;
CLIPFORMAT CMenuContainer::s_PreferredEffectFormat;
CLIPFORMAT CMenuContainer::s_DropDescriptionFormat;
MenuSkin CMenuContainer::s_Skin;
CMenuContainer::StartMenuParams CMenuContainer::s_StartMenuParams;
UINT CMenuContainer::s_StartMenuMsg;
std::vector<CMenuFader*> CMenuFader::s_Faders;
bool CMenuContainer::s_bWin7Style;
CMenuContainer::TMenuMode CMenuContainer::s_MenuMode;
CMenuContainer::TMenuMode CMenuContainer::s_PreSearchMenuMode;
TMenuID CMenuContainer::s_ShutdownCommand;
int CMenuContainer::s_MenuHeight;
int CMenuContainer::s_MenuMaxHeight[2];
int CMenuContainer::s_MenuWidthNormal;
int CMenuContainer::s_MenuWidthJump;
int CMenuContainer::s_MenuWidthMax;
int CMenuContainer::s_BackgroundW1, CMenuContainer::s_BackgroundW2, CMenuContainer::s_BackgroundH1, CMenuContainer::s_BackgroundH2;
CMenuContainer::OldMenuState CMenuContainer::s_OldMenuState;
CUserWindow CMenuContainer::s_UserPicture;
RECT CMenuContainer::s_UserPictureRect;
int CMenuContainer::s_ProgramsScrollPos;
HBITMAP CMenuContainer::s_ArrowsBitmap;
unsigned int CMenuContainer::s_LastArrowColor;
char CMenuContainer::s_HasMoreResults;
int CMenuContainer::s_ProgramsWidth, CMenuContainer::s_JumplistWidth;
CComPtr<IFrameworkInputPane> CMenuContainer::s_pFrameworkInputPane;
CString CMenuContainer::s_PinFolder;

LRESULT CALLBACK CMenuContainer::SubclassSearchBox( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	CMenuContainer *pParent=(CMenuContainer*)uIdSubclass;
	CWindow box(hWnd);
	if (uMsg==WM_PAINT && !pParent->m_bNoSearchDraw)
	{
		// use buffered paint to allow the edit box to appear on a glass background
		pParent->m_bNoSearchDraw=true;

		PAINTSTRUCT ps;
		HDC hdc=box.BeginPaint(&ps);

		BP_PAINTPARAMS paintParams={sizeof(paintParams)};
		HDC hdcPaint=NULL;
		HPAINTBUFFER hBufferedPaint=BeginBufferedPaint(hdc,&ps.rcPaint,BPBF_TOPDOWNDIB,&paintParams,&hdcPaint);
		if (hdcPaint)
		{
			SendMessage(hWnd,WM_PRINTCLIENT,(WPARAM)hdcPaint,PRF_CLIENT);
			BufferedPaintSetAlpha(hBufferedPaint,&ps.rcPaint,255);
			EndBufferedPaint(hBufferedPaint,TRUE);
		}
		box.EndPaint(&ps);

		pParent->m_bSearchDrawn=true;
		pParent->m_bNoSearchDraw=false;
		return 0;
	}
	if (uMsg==WM_PRINTCLIENT)
	{
		LRESULT res=DefSubclassProc(hWnd,uMsg,wParam,lParam);
		HDC hdc=(HDC)wParam;
		if ((lParam&PRF_CLIENT) && ::GetWindowTextLength(hWnd)==0 && ((GetSettingInt(L"SearchBox")==SEARCHBOX_NORMAL && GetSettingBool(L"SearchSelect")) || GetFocus()!=hWnd) && pParent->m_SearchIndex>=0)
		{
			RECT rc;
			::SendMessage(hWnd,EM_GETRECT,0,(LPARAM)&rc);
			if (s_bRTL)
				rc.right-=3;
			else
				rc.left+=3;
			HGDIOBJ font0=SelectObject(hdc,s_Skin.Search_hint_font);
			SetBkColor(hdc,GetSysColor(COLOR_WINDOW));
			SetBkMode(hdc,TRANSPARENT);
			SetTextColor(hdc,s_Skin.Search_text_colors[1]);
			if (GetSettingBool(L"SearchHint"))
				DrawText(hdc,GetSettingString(L"SearchHintText"),-1,&rc,DT_SINGLELINE|DT_EDITCONTROL|(s_bRTL?DT_RIGHT:DT_LEFT));
			else
				DrawText(hdc,pParent->m_Items[pParent->m_SearchIndex].name,-1,&rc,DT_SINGLELINE|DT_EDITCONTROL|(s_bRTL?DT_RIGHT:DT_LEFT));
			SelectObject(hdc,font0);
		}
		return res;
	}
	if (uMsg==WM_KEYDOWN)
	{
		if (wParam==VK_TAB)
		{
			s_bPendingSearchEnter=false;
			// forward Tabs to the parent
			return pParent->SendMessage(uMsg,wParam,lParam);
		}
		if (wParam==VK_UP || wParam==VK_DOWN)
		{
			s_bPendingSearchEnter=false;
			// forward up/down keys
			CMenuContainer *pSearchMenu=s_Menus[s_Menus.size()-1];
			if (pSearchMenu->m_Options&CONTAINER_SEARCH)
				return pSearchMenu->SendMessage(uMsg,wParam,lParam); // forward to the search menu
			else
				return pParent->SendMessage(uMsg,wParam,lParam); // forward to the parent
		}
		if (wParam==VK_PRIOR || wParam==VK_NEXT)
		{
			s_bPendingSearchEnter=false;
			// forward page keys
			CMenuContainer *pSearchMenu=s_Menus[s_Menus.size()-1];
			if (pSearchMenu->m_Options&CONTAINER_SEARCH)
				return pSearchMenu->SendMessage(uMsg,wParam,lParam); // forward to the search menu
			else
				return pParent->SendMessage(uMsg,wParam,lParam); // forward to the parent
		}
		if (wParam==VK_LEFT || wParam==VK_RIGHT)
		{
			// forward left/right keys
			if (::GetWindowTextLength(hWnd)==0)
				return pParent->SendMessage(uMsg,wParam,lParam); // forward to the parent
		}
		if (wParam==VK_RETURN)
		{
			// forward Enter to the submenu, or execute the current string
			CMenuContainer *pSearchMenu=s_Menus[s_Menus.size()-1];
			bool bShift=GetKeyState(VK_SHIFT)<0;
			bool bCtrl=GetKeyState(VK_CONTROL)<0;
			if ((pSearchMenu->m_Options&CONTAINER_SEARCH) || (!pSearchMenu->m_bSubMenu && s_MenuMode==MODE_SEARCH))
			{
				if (pSearchMenu->m_HotItem>=0 && pSearchMenu->m_HotItem<(int)pSearchMenu->m_Items.size() &&
					pSearchMenu->m_Items[pSearchMenu->m_HotItem].id!=MENU_EMPTY && pSearchMenu->m_Items[pSearchMenu->m_HotItem].id!=MENU_SEARCH_EMPTY)
				{
					pSearchMenu->SendMessage(WM_KEYDOWN,VK_RETURN);
				}
				else if (!s_bNoRun && (!s_SearchResults.bSearching || s_SearchResults.bResults))
				{
					CString command;
					CWindow(hWnd).GetWindowText(command);
					if (!command.IsEmpty())
						pSearchMenu->ExecuteCommand(command,bShift && bCtrl,true);
				}
				else if (s_SearchResults.bSearching && !s_SearchResults.bResults)
				{
					s_bPendingSearchEnter=true;
				}
			}
			return 0;
		}
		if (wParam==VK_ESCAPE)
		{
			s_bPendingSearchEnter=false;
			// forward Esc to the parent or clear the string
			if (box.GetWindowTextLength()==0)
				pParent->PostMessage(WM_KEYDOWN,VK_ESCAPE);
			else
				box.SetWindowText(L"");
			return 0;
		}
		if (wParam==VK_BACK && GetKeyState(VK_CONTROL)<0)
		{
			wchar_t text[1024];
			::GetWindowText(hWnd,text,_countof(text));
			int len=Strlen(text);
			int pos=(int)::SendMessage(hWnd,EM_GETSEL,0,0);
			if (LOWORD(pos)==HIWORD(pos))
			{
				pos=LOWORD(pos);
				if (pos>len) pos=len;
				int start=0;
				for (int i=0;i<pos;i++)
				{
					if (i>0 && (text[i-1]==' ' || text[i-1]=='\\') && text[i]!=text[i-1])
						start=i;
				}
				pos=MAKELONG(start,pos);
			}
			int start=LOWORD(pos);
			int end=HIWORD(pos);
			if (start>len) start=len;
			if (end>len) end=len;
			memmove(text+start,text+end,(len-end+1)*2);
			::SetWindowText(hWnd,text);
			::SendMessage(hWnd,EM_SETSEL,start,start);
			return 0;
		}
	}
	if (uMsg==WM_SYSCHAR)
	{
		CMenuContainer *pSearchMenu=s_Menus[s_Menus.size()-1];
		if (pSearchMenu->m_Options&CONTAINER_SEARCH)
		{
			if (!pSearchMenu->SendMessage(WM_CHAR,wParam,lParam))
				return 0;
		}
		pParent->SendMessage(WM_CHAR,wParam,lParam);
	}
	if (uMsg==WM_SYSKEYDOWN)
	{
		if (s_bWin7Style)
		{
			pParent->SendMessage(WM_SYSKEYDOWN,wParam,lParam);
			if (wParam==VK_MENU)
				pParent->ShowKeyboardCues(true);
		}
		else
		{
			CMenuContainer *pSearchMenu=s_Menus[s_Menus.size()-1];
			if (pSearchMenu->m_Options&CONTAINER_SEARCH)
				pSearchMenu->SendMessage(WM_SYSKEYDOWN,wParam,lParam);
		}
	}
	if (uMsg==WM_CHAR && (wParam==VK_RETURN || wParam==10 || wParam==VK_ESCAPE || wParam==VK_TAB || wParam==127))
	{
		// prevent a beep when Enter, Esc or Tab is pressed
		return 0;
	}
	if (uMsg==WM_MOUSEACTIVATE)
	{
		pParent->ActivateItem(pParent->m_SearchIndex,ACTIVATE_SELECT,NULL);
		s_bDisableHover=!s_bWin7Style;
		// close all sub-menus
		CloseSubMenus(CLOSE_SKIP_SEARCH,pParent);
		::SetFocus(hWnd);
		if (s_bBehindTaskbar && s_TaskBar)
			pParent->SetWindowPos(s_TaskBar,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE);
		return MA_NOACTIVATE;
	}
	if (uMsg==WM_SETFOCUS)
	{
		pParent->SetSearchState(::GetWindowTextLength(hWnd)>0?SEARCH_TEXT:SEARCH_BLANK);
	}
	if (uMsg==WM_KILLFOCUS)
	{
		s_bPendingSearchEnter=false;
		if (!s_bPreventClosing)
		{
#ifndef PREVENT_CLOSING
			box.SetWindowText(L"");
			pParent->SetSearchState(SEARCH_NONE);
#endif
		}
	}
	if (uMsg==WM_CONTEXTMENU && (lParam&0xFFFFFFFF)==0xFFFFFFFF)
	{
		if (s_bWin7Style)
			return pParent->SendMessage(WM_CONTEXTMENU,wParam,lParam);
		else
		{
			CMenuContainer *pSearchMenu=s_Menus[s_Menus.size()-1];
			if (pSearchMenu->m_Options&CONTAINER_SEARCH)
				return pSearchMenu->SendMessage(WM_CONTEXTMENU,wParam,lParam);
		}
	}
	if (uMsg==WM_MOUSEWHEEL)
	{
		if (s_bWin7Style)
			return pParent->SendMessage(WM_MOUSEWHEEL,wParam,lParam);
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

LRESULT CALLBACK CMenuContainer::SubclassScrollbar( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	CMenuContainer *pParent=(CMenuContainer*)uIdSubclass;
	if (!pParent->m_ScrollTheme)
	{
		return DefSubclassProc(hWnd,uMsg,wParam,lParam);
	}
	DWORD oldThread=g_CustomScrollbarThread;
	g_CustomScrollbarThread=GetCurrentThreadId();
	g_CustomScrollbarTheme=pParent->m_ScrollTheme;
	LRESULT res=DefSubclassProc(hWnd,uMsg,wParam,lParam);
	g_CustomScrollbarThread=oldThread;
	if (!oldThread)
		g_CustomScrollbarTheme=NULL;
	return res;
}

int CMenuContainer::CompareMenuString( const wchar_t *str1, const wchar_t *str2 )
{
	if (s_bExtensionSort)
	{
		const wchar_t *ext1=PathFindExtension(str1);
		const wchar_t *ext2=PathFindExtension(str2);
		int cmp=CompareString(LOCALE_USER_DEFAULT,LINGUISTIC_IGNORECASE,ext1,-1,ext2,-1)-CSTR_EQUAL;
		if (cmp) return cmp;
	}
	if (s_bLogicalSort)
		return StrCmpLogicalW(str1,str2);
	else
		return CompareString(LOCALE_USER_DEFAULT,LINGUISTIC_IGNORECASE,str1,-1,str2,-1)-CSTR_EQUAL;
}

CMenuContainer::CMenuContainer( CMenuContainer *pParent, int index, int options, const StdMenuItem *pStdItem, PIDLIST_ABSOLUTE path1, PIDLIST_ABSOLUTE path2 )
{
	m_RefCount=1;
	m_bSubMenu=(index>=0); // this may be true even if pParent is NULL (in case you want to show only sub-menus somewhere, use index=0 and pParent=NULL)
	m_HoverItem=m_ContextItem=m_HotItem=m_InsertMark=-1;
	m_DragHoverItem=m_DragIndex=-1;
	m_ClickIndex=GetKeyState(VK_LBUTTON)<0?-2:-1;
	m_bHoverArrow=m_bClickArrow=false;
	m_pParent=pParent;
	m_ParentIndex=pParent?index:-1;
	m_Options=options;
	m_pStdItem=pStdItem;
	m_Bitmap=NULL;
	m_BitmapOffset=0;
	m_Region=NULL;
	m_PaintOffset.x=m_PaintOffset.y=0;
	m_Path1[0].Clone(path1);
	if (!s_bNoCommonFolders)
		m_Path2[0].Clone(path2);

	if (options&CONTAINER_ALLPROGRAMS)
	{
		ShGetKnownFolderIDList(FOLDERID_Programs,&m_Path1[1]);
		if (!s_bNoCommonFolders)
			ShGetKnownFolderIDList(FOLDERID_CommonPrograms,&m_Path2[1]);
	}

	Assert(path1 || !path2);

	m_FolderHash[0]=m_FolderHash[1]=0;
	if (m_Options&CONTAINER_APPS)
		m_FolderHash[0]=CalcFNVHash(L"apps");

	InitializeSpecialFolders();

	m_bDestroyed=false;
	s_Menus.push_back(this);
	m_Submenu=-1;
	m_SubJumpItem=-1;
	m_bTrackMouse=false;
	m_RefreshPosted=0;

	m_SubShowTime=0;
	m_bInsertAfter=false;
	m_bHotArrow=false;

	m_DragHoverTime=0;

	m_MaxWidth=0;
	m_bTwoColumns=false;
	memset(&m_rMenu,0,sizeof(m_rMenu));
	memset(&m_rContent,0,sizeof(m_rContent));
	memset(&m_rContent2,0,sizeof(m_rContent2));
	memset(&m_rUser1,0,sizeof(m_rUser1));
	memset(&m_rUser2,0,sizeof(m_rUser2));
	memset(&m_rPadding,0,sizeof(m_rPadding));

	m_ExtraTop=m_ExtraBottom=m_ExtraBorder=0;
	m_MouseWheel=0;
	m_ScrollCount=m_ScrollHeight=m_ScrollOffset=m_ScrollButtonSize=0;
	m_bScrollUp=m_bScrollDown=m_bScrollUpHot=m_bScrollDownHot=false;
	m_SearchIndex=-1;
	m_SearchItemCount=0;
	m_SearchProvidersCount=0;
	m_bScrollTimerMouse=false;
	m_bScrollTimerTouch=false;
	m_bNoSearchDraw=false;
	m_bSearchDrawn=false;
	m_bInSearchUpdate=false;
	m_bDisableProgHover=false;
	m_bClosing=false;
	m_bRefreshItems=false;
	m_bWorkAreaPosted=false;
	m_SearchIcons=NULL;
	m_SearchState=SEARCH_NONE;
	m_SearchCategoryHash=CSearchManager::CATEGORY_INVALID;
	m_pProgramsTree=NULL;
	m_ProgramButtonIndex=m_ProgramTreeIndex=-1;
	m_OriginalCount=0;
	m_OriginalScrollCount=-1;
	m_SearchScrollCount=m_SearchScrollHeight=m_SearchScrollPos=0;
	m_SearchHash=0;
	m_pDropTargetInfo=NULL;
	m_DropTargetIndex=-1;
	m_PanPosY=m_Overpan=m_PointerId=0;
	m_InputCookie=0;
	m_ScrollTheme=NULL;

	CoCreateInstance(CLSID_DragDropHelper,NULL,CLSCTX_INPROC_SERVER,IID_IDropTargetHelper,(void**)&m_pDropTargetHelper);
	m_pDragSourceHelper=CComQIPtr<IDragSourceHelper2>(m_pDropTargetHelper);
	LOG_MENU(LOG_OPEN,L"Open Menu, ptr=%p, index=%d, options=%08X",this,index,options);
}

CMenuContainer::~CMenuContainer( void )
{
	ClearItems(m_Items.begin(),m_Items.end());
	if (std::find(s_Menus.begin(),s_Menus.end(),m_pParent)!=s_Menus.end()) // check if the parent is still alive
	{
		if (m_pParent->m_Submenu==m_ParentIndex)
		{
			if (!m_pParent->m_bDestroyed)
			{
				m_pParent->InvalidateItem(m_ParentIndex);
				if (m_pParent->m_HotItem<0 && !(m_Options&CONTAINER_SEARCH) && (!m_pParent->m_pProgramsTree || !m_pParent->m_pProgramsTree->m_hWnd || m_pParent->m_pProgramsTree->m_hWnd!=GetFocus()))
					m_pParent->SetHotItem(m_ParentIndex);
			}
			m_pParent->SetSubmenu(-1);
		}
	}
	if (m_Bitmap) DeleteObject(m_Bitmap);
	if (m_Region) DeleteObject(m_Region);

	// must be here and not in OnDestroy because during drag/drop a menu can close while still processing messages
	s_Menus.erase(std::find(s_Menus.begin(),s_Menus.end(),this));
	if (m_SearchIcons)
		DeleteObject(m_SearchIcons);
	if (m_pProgramsTree) m_pProgramsTree->Release();
}

void CMenuContainer::AddFirstFolder( IShellItem *pFolder, std::vector<MenuItem> &items, int options )
{
	CAbsolutePidl ignore;
	if (!m_pParent || (m_Options&CONTAINER_ALLPROGRAMS))
	{
		// remove the Programs subfolder from the main menu. it will be added separately
		ShGetKnownFolderIDList(FOLDERID_Programs,&ignore);
	}

	CShellItemEnumerator enumerator(pFolder);
	if (!enumerator.IsValid()) return;
	
	CComPtr<IShellItem> pChild;
	CAbsolutePidl childPidl;
	int refreshFlags=CItemManager::INFO_SMALL_ICON;
	if (!m_bSubMenu)
	{
		if (s_Skin.Main_icon_size==MenuSkin::ICON_SIZE_LARGE)
			refreshFlags=CItemManager::INFO_LARGE_ICON;
		else if (s_Skin.Main_icon_size==MenuSkin::ICON_SIZE_NONE)
			refreshFlags=0;
	}
	if (options&CONTAINER_PROGRAMS)
		refreshFlags|=CItemManager::INFO_LINK|CItemManager::INFO_METRO;
	if (options&CONTAINER_NOPATH)
		refreshFlags|=CItemManager::INFO_NO_PATH;
	while (enumerator.GetNext(pChild,childPidl))
	{
		SFGAOF flags=0;
		HRESULT hrFlags=pChild->GetAttributes(SFGAO_FOLDER|SFGAO_STREAM|SFGAO_LINK|SFGAO_HIDDEN,&flags);
		if (FAILED(hrFlags)) // check if the item is a folder, archive or a link
			flags=0;
		if (flags&SFGAO_HIDDEN)
			continue;
		CComString pName;
		if (SUCCEEDED(pChild->GetDisplayName(SIGDN_PARENTRELATIVEPARSING,&pName)))
		{
			LOG_MENU(LOG_OPEN,L"%s, 0x%08X, 0x%08X",(const wchar_t*)pName,flags,hrFlags);
			MenuItem item(MENU_NO);
			if (ignore && ILIsEqual(childPidl,ignore))
				continue;
			item.pItemInfo=g_ItemManager.GetItemInfo(pChild,childPidl,refreshFlags);
			bool bLibrary=_wcsicmp(PathFindExtension(pName),L".library-ms")==0;
			bool bStartScreen=(
#ifndef STARTSCREEN_WIN7
				GetWinVersion()>=WIN_VER_WIN8 &&
#endif
				wcscmp(pName,STARTSCREEN_COMMAND)==0);
			const wchar_t *pStr=pName;
			if (!(pStr[0]&0xFF00) && isalpha(pStr[0]) && pStr[1]==':' && Strlen(pStr)<=3)
				item.drive=(char)toupper(pStr[0]);
			if (bStartScreen)
			{
				if (GetWinVersion()>=WIN_VER_WIN10)
					item.name=FindTranslation(L"Menu.StartMenu",L"Start Menu");
				else
					item.name=FindTranslation(L"Menu.StartScreen",L"Start Screen");
				pName.MakeUpper();
				item.nameHash=CalcFNVHash(pName);
			}
			else
			{
				CComString pName2;
				if (SUCCEEDED(pChild->GetDisplayName(SIGDN_NORMALDISPLAY,&pName2)))
				{
					if ((options&CONTAINER_CONTROLPANEL) && (_wcsnicmp(pName2,L"::{26EE0668-A00A-44D7-9371-BEB064C98683}\\0",42)==0 || Strlen(pName2)==0))
						continue; // if the display name starts with the control panel GUID, something's wrong (like the Intel crap)
					pName.MakeUpper();
					item.nameHash=CalcFNVHash(pName);
					pName.Clear();
					item.SetName(pName2,(options&CONTAINER_NOEXTENSIONS)!=0);
				}
				else
				{
					if (options&CONTAINER_CONTROLPANEL)
						continue; // control panel items should have a normal name
					item.SetName(pName,(options&CONTAINER_NOEXTENSIONS)!=0);
					pName.MakeUpper();
					item.nameHash=CalcFNVHash(pName);
				}

				if (bLibrary) flags&=~SFGAO_STREAM;
				item.bLink=(flags&SFGAO_LINK)!=0;
				item.bFolder=(!(options&CONTAINER_CONTROLPANEL) && !(options&CONTAINER_NOSUBFOLDERS) && (flags&SFGAO_FOLDER) && (!(flags&(SFGAO_STREAM|SFGAO_LINK)) || (s_bExpandLinks && item.bLink)));
				{
					CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
					if (item.pItemInfo->IsMetroLink())
					{
						if ((m_bSubMenu && GetSettingBool(L"HideProgramsMetro")) || _wcsicmp(item.pItemInfo->GetAppid(),DESKTOP_APP_ID)==0)
							continue;
						item.bLink=item.bMetroLink=true;
						if (!item.pItemInfo->GetMetroName().IsEmpty())
							item.name=item.pItemInfo->GetMetroName();
					}
					item.bMetroApp=item.pItemInfo->IsMetroApp();
				}
			}
			item.pItem1=childPidl.Detach();
			item.bStartScreen=bStartScreen;
			item.bPrograms=(options&CONTAINER_PROGRAMS)!=0;
			items.push_back(item);
#ifdef REPEAT_ITEMS
			for (int i=0;i<REPEAT_ITEMS;i++)
			{
				item.pItem1=ILCloneFull(item.pItem1);
				items.push_back(item);
			}
#endif
		}
	}
}

void CMenuContainer::AddSecondFolder( IShellItem *pFolder, std::vector<MenuItem> &items, int options )
{
	CAbsolutePidl ignore;
	if (!m_pParent || (m_Options&CONTAINER_ALLPROGRAMS))
	{
		// remove the Programs subfolder from the main menu. it will be added separately
		ShGetKnownFolderIDList(FOLDERID_CommonPrograms,&ignore);
	}

	CShellItemEnumerator enumerator(pFolder);
	if (!enumerator.IsValid()) return;
	
	CComPtr<IShellItem> pChild;
	CAbsolutePidl childPidl;
	int refreshFlags=CItemManager::INFO_SMALL_ICON;
	if (!m_bSubMenu)
	{
		if (s_Skin.Main_icon_size==MenuSkin::ICON_SIZE_LARGE)
			refreshFlags=CItemManager::INFO_LARGE_ICON;
		else if (s_Skin.Main_icon_size==MenuSkin::ICON_SIZE_NONE)
			refreshFlags=0;
	}
	if (options&CONTAINER_PROGRAMS)
		refreshFlags|=CItemManager::INFO_LINK|CItemManager::INFO_METRO;
	while (enumerator.GetNext(pChild,childPidl))
	{
		SFGAOF flags=0;
		HRESULT hrFlags=pChild->GetAttributes(SFGAO_FOLDER|SFGAO_STREAM|SFGAO_LINK|SFGAO_HIDDEN,&flags);
		if (FAILED(hrFlags)) // check if the item is a folder, archive or a link
			flags=0;
		if (flags&SFGAO_HIDDEN)
			continue;
		CComString pName;
		if (SUCCEEDED(pChild->GetDisplayName(SIGDN_PARENTRELATIVEPARSING,&pName)))
		{
			LOG_MENU(LOG_OPEN,L"%s, 0x%08X, 0x%08X",(const wchar_t*)pName,flags,hrFlags);
			bool bLibrary=_wcsicmp(PathFindExtension(pName),L".library-ms")==0;
			bool bStartScreen=(
#ifndef STARTSCREEN_WIN7
				GetWinVersion()>=WIN_VER_WIN8 &&
#endif
				wcscmp(pName,STARTSCREEN_COMMAND)==0);
			MenuItem item(MENU_NO);
			if (bStartScreen)
			{
				if (GetWinVersion()>=WIN_VER_WIN10)
					item.name=FindTranslation(L"Menu.StartMenu",L"Start Menu");
				else
					item.name=FindTranslation(L"Menu.StartScreen",L"Start Screen");
				pName.MakeUpper();
				item.nameHash=CalcFNVHash(pName);
			}
			else
			{
				CComString pName2;
				if (SUCCEEDED(pChild->GetDisplayName(SIGDN_NORMALDISPLAY,&pName2)))
				{
					pName.MakeUpper();
					item.nameHash=CalcFNVHash(pName);
					pName.Clear();
					item.SetName(pName2,(options&CONTAINER_NOEXTENSIONS)!=0);
				}
				else
				{
					item.SetName(pName,(options&CONTAINER_NOEXTENSIONS)!=0);
					pName.MakeUpper();
					item.nameHash=CalcFNVHash(pName);
				}
			}
			pName.Clear();

			if (ignore && ILIsEqual(childPidl,ignore))
				continue;

			// look for another item with the same name
			bool bFound=false;
			for (std::vector<MenuItem>::iterator it=items.begin();it!=items.end();++it)
			{
				if (item.nameHash==it->nameHash)
				{
					it->pItem2=childPidl.Detach();
					bFound=true;
					break;
				}
			}

			if (!bFound)
			{
				// new item
				if (!bStartScreen)
				{
					item.pItemInfo=g_ItemManager.GetItemInfo(pChild,childPidl,refreshFlags);

					if (bLibrary) flags&=~SFGAO_STREAM;
					item.bLink=(flags&SFGAO_LINK)!=0;
					item.bFolder=(!(options&CONTAINER_CONTROLPANEL) && !(options&CONTAINER_NOSUBFOLDERS) && (flags&SFGAO_FOLDER) && (!(flags&(SFGAO_STREAM|SFGAO_LINK)) || (s_bExpandLinks && item.bLink)));
					{
						CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
						if (item.pItemInfo->IsMetroLink())
						{
							if ((m_bSubMenu && GetSettingBool(L"HideProgramsMetro")) || _wcsicmp(item.pItemInfo->GetAppid(),DESKTOP_APP_ID)==0)
								continue;
							item.bLink=item.bMetroLink=true;
							if (!item.pItemInfo->GetMetroName().IsEmpty())
								item.name=item.pItemInfo->GetMetroName();
							item.bMetroApp=item.pItemInfo->IsMetroApp();
						}
					}
				}
				item.pItem1=childPidl.Detach();
				item.bStartScreen=bStartScreen;
				item.bPrograms=(options&CONTAINER_PROGRAMS)!=0;
				items.push_back(item);
#ifdef REPEAT_ITEMS
				for (int i=0;i<REPEAT_ITEMS;i++)
				{
					item.pItem1=ILCloneFull(item.pItem1);
					items.push_back(item);
				}
#endif
			}
		}
	}
}

static int GetStdOptions( TMenuID id )
{
	for (int i=0;i<_countof(g_StdOptions);i++)
		if (g_StdOptions[i].id==id)
		{
			return g_StdOptions[i].options;
			break;
		}
	return MENU_ENABLED|MENU_EXPANDED;
}

static const CItemManager::ItemInfo *GetInternetIcon( bool bDoc )
{
	wchar_t path[_MAX_PATH];
	DWORD size=_countof(path);
	if (SUCCEEDED(AssocQueryString(0,bDoc?ASSOCSTR_DEFAULTICON:ASSOCSTR_EXECUTABLE,L"http",L"open",path,&size)))
	{
		if (bDoc)
			return g_ItemManager.GetCustomIcon(path,CItemManager::ICON_SIZE_TYPE_SMALL);
		CAbsolutePidl pidl;
		pidl.Attach(ILCreateFromPath(path));
		CComPtr<IShellItem> pItem;
		if (pidl && SUCCEEDED(SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pItem)))
			return g_ItemManager.GetItemInfo(pItem,pidl,CItemManager::INFO_SMALL_ICON);
	}
	return g_ItemManager.GetCustomIcon(L"shell32.dll,14",CItemManager::ICON_SIZE_TYPE_SMALL);
}

void CMenuContainer::AddInternetSearch( size_t index )
{
	MenuItem item(MENU_SEARCH_INTERNET);
	item.name=FindTranslation(L"Menu.SearchInternet",L"Search the Internet");
	item.pItemInfo=GetInternetIcon(false);
	m_Items.insert(m_Items.begin()+index,1,item);
}

void CMenuContainer::AddStandardItems( void )
{
	if (m_pStdItem && m_pStdItem->id!=MENU_NO)
	{
		bool bItemsFirst=(m_Options&(CONTAINER_ITEMS_FIRST|CONTAINER_SEARCH))==CONTAINER_ITEMS_FIRST;
		if (!m_Items.empty() && !(s_bWin7Style && !m_bSubMenu && GetSettingInt(L"ProgramsStyle")==PROGRAMS_HIDDEN))
		{
			MenuItem item(MENU_SEPARATOR);
			if (m_pStdItem->id==MENU_COLUMN_PADDING)
				item.bAlignBottom=true;
			if (bItemsFirst)
				m_Items.insert(m_Items.begin(),item);
			else
				m_Items.push_back(item);
		}
		size_t menuIdx=bItemsFirst?0:m_Items.size();
		bool bBreak=false, bAlignBottom=false, bInlineFirst=false;
		const StdMenuItem *pInlineParent=NULL;
		int searchProviderIndex=-1;
		m_SearchProvidersCount=0;
		bool bSecondColumn=false;
		for (const StdMenuItem *pStdItem=m_pStdItem;;pStdItem++)
		{
			if (pStdItem->id==MENU_LAST)
			{
				if (pInlineParent)
				{
					pStdItem=pInlineParent;
					pInlineParent=NULL;
					continue;
				}
				break;
			}
			if (pStdItem->id==MENU_IGNORE)
				continue;
			if (m_bSubMenu && pStdItem->id==s_ShutdownCommand)
				continue;

			if (pStdItem->id==MENU_COLUMN_BREAK && !m_bSubMenu && s_Skin.TwoColumns)
				bSecondColumn=true;

			int stdOptions=GetStdOptions(pStdItem->id);
			if (!(stdOptions&MENU_ENABLED)) continue;

			if (s_bWin7Style && m_bSubMenu && pStdItem->command && (pStdItem->id==MENU_SWITCHUSER || pStdItem->id==MENU_SECURITY || pStdItem->id==MENU_LOGOFF || pStdItem->id==MENU_LOCK || pStdItem->id==MENU_DISCONNECT
				|| pStdItem->id==MENU_UNDOCK || pStdItem->id==MENU_RESTART || pStdItem->id==MENU_SLEEP || pStdItem->id==MENU_HIBERNATE || pStdItem->id==MENU_SHUTDOWN
				|| pStdItem->id==MENU_RESTART_NOUPDATE || pStdItem->id==MENU_SHUTDOWN_NOUPDATE))
			{
				CString commands=GetSettingString(L"ShutdownW7");
				const wchar_t *str=commands;
				bool bFound=false, bEmpty=true;
				const wchar_t *commandName=pStdItem->command;
				if (pStdItem->id==MENU_SHUTDOWN_NOUPDATE)
					commandName=L"shutdown";
				else if (pStdItem->id==MENU_RESTART_NOUPDATE)
					commandName=L"restart";
				while (*str)
				{
					wchar_t token[100];
					str=GetToken(str,token,_countof(token),L" \t;,");
					if (*token) bEmpty=false;
					if (_wcsicmp(commandName,token)==0)
					{
						bFound=true;
						break;
					}
				}
				if (!bFound && !bEmpty) continue;
			}

			if (pStdItem->id==MENU_SEPARATOR && menuIdx>0 && m_Items[menuIdx-1].id==MENU_SEPARATOR)
			{
				// prevent double separators unless that's exactly what is requested
				if (pStdItem>m_pStdItem && pStdItem[-1].id!=MENU_SEPARATOR)
					continue;
			}
			if (pStdItem->id==MENU_COLUMN_BREAK)
			{
				bBreak=true;
				continue;
			}
			if (pStdItem->id==MENU_COLUMN_PADDING)
			{
				bAlignBottom=true;
				continue;
			}

			if (!pInlineParent && pStdItem->submenu && (pStdItem->settings&StdMenuItem::MENU_INLINE))
			{
				pInlineParent=pStdItem;
				pStdItem=pInlineParent->submenu-1;
				bInlineFirst=true;
				continue;
			}
			if (s_bWin7Style && pStdItem->id==MENU_SEARCH_BOX)
			{
				MenuItem item(MENU_EMPTY);
				m_Items.insert(m_Items.begin()+menuIdx,1,item);
				menuIdx++;
				searchProviderIndex=(int)menuIdx;
				if (GetSettingBool(L"SearchInternet"))
				{
					AddInternetSearch(menuIdx);
					menuIdx++;
					m_SearchProvidersCount++;
				}
			}

			bool bSearchProvider7=false;
			if (s_bWin7Style && !m_bSubMenu && pStdItem->command)
			{
				// if after environment substitution there are %1 or %2 left, this is likely a search provider
				wchar_t buf[1024];
				Strcpy(buf,_countof(buf),pStdItem->command);
				DoEnvironmentSubst(buf,_countof(buf));
				const wchar_t *ptr=wcsstr(buf,L"%1");
				if (ptr && !isalnum(ptr[2]))
					bSearchProvider7=true;
				else
				{
					ptr=wcsstr(buf,L"%2");
					if (ptr && !isalnum(ptr[2]))
						bSearchProvider7=true;
				}
			}

			MenuItem item(pStdItem->id);
			item.pStdItem=pStdItem;
			if (pInlineParent)
				item.bInline=true;

			if (!bSearchProvider7)
			{
				item.bBreak=bBreak;
				bBreak=false;
			}
			item.bAlignBottom=bAlignBottom;
			item.bInlineFirst=bInlineFirst;
			bAlignBottom=bInlineFirst=false;
			TNetworkType networkType=NETWORK_NONE;

			Assert(pStdItem->folder1 || !pStdItem->folder2);
			if (pStdItem->id==MENU_APPS)
			{
				wchar_t path[_MAX_PATH]=METRO_APP_ROOT;
				DoEnvironmentSubst(path,_countof(path));
				item.pItem1=ILCreateFromPath(path);
				item.bFolder=true;
				item.name=FindTranslation(L"Menu.Apps",L"Apps");
			}
			else if (pStdItem->id==MENU_PROGRAMS && GetSettingInt(L"PinnedPrograms")==PINNED_PROGRAMS_PINNED)
			{
				ShGetKnownFolderIDList(FOLDERID_StartMenu,&item.pItem1);
				if (!s_bNoCommonFolders)
					ShGetKnownFolderIDList(FOLDERID_CommonStartMenu,&item.pItem2);
				item.bFolder=true;
			}
			else if (pStdItem->folder1)
			{
				ShGetKnownFolderIDList(*pStdItem->folder1,&item.pItem1);
				if (pStdItem->folder2)
					ShGetKnownFolderIDList(*pStdItem->folder2,&item.pItem2);
				if (!item.pItem1 && item.pItem2)
				{
					item.pItem1=item.pItem2;
					item.pItem2=NULL;
				}
				item.bFolder=(stdOptions&MENU_EXPANDED) && !(item.pStdItem->settings&StdMenuItem::MENU_NOEXPAND);
			}
			else if (pStdItem->link)
			{
				SFGAOF flags=0;
				wchar_t buf[1024];
				Strcpy(buf,_countof(buf),item.pStdItem->link);
				DoEnvironmentSubst(buf,_countof(buf));
				bool bLibrary=_wcsicmp(PathFindExtension(buf),L".library-ms")==0;
				wchar_t *second=wcschr(buf,';');
				if (!bLibrary && second && !(item.pStdItem->settings&StdMenuItem::MENU_NOEXPAND))
				{
					wchar_t *end= second;
					while (end>buf)
					{
						*end=0;
						end--;
						if (*end!=' ')
							break;
					}
					MenuParseDisplayName(buf,&item.pItem1,&flags,&networkType);
					second++;
					while (*second==' ')
						second++;
					SFGAOF flags2=0;
					TNetworkType networkType2;
					MenuParseDisplayName(second,&item.pItem2,&flags2,&networkType2);
					if (!item.pItem1 && item.pItem2)
					{
						item.pItem1=item.pItem2;
						item.pItem2=NULL;
						flags=flags2;
						networkType=networkType2;
					}
					if (item.pItem1)
					{
						item.bLink=(flags&SFGAO_LINK)!=0;
						item.bFolder=((flags&SFGAO_FOLDER) && (!(flags&(SFGAO_STREAM|SFGAO_LINK)) || (s_bExpandLinks && item.bLink)));
					}
				}
				else if (SUCCEEDED(MenuParseDisplayName(buf,&item.pItem1,&flags,&networkType)))
				{
					if (bLibrary) flags&=~SFGAO_STREAM;
					item.bLink=(flags&SFGAO_LINK)!=0;
					item.bFolder=((flags&SFGAO_FOLDER) && !(item.pStdItem->settings&StdMenuItem::MENU_NOEXPAND) && (!(flags&(SFGAO_STREAM|SFGAO_LINK)) || (s_bExpandLinks && item.bLink)));
				}
			}
			if ((pStdItem->submenu && pStdItem->id!=MENU_SEARCH_BOX && (stdOptions&MENU_EXPANDED)) || pStdItem->id==MENU_RECENT_PROGRAMS)
				item.bFolder=true;

			item.bSplit=item.bFolder && (item.pStdItem->settings&StdMenuItem::MENU_SPLIT_BUTTON)!=0;

			// get icon
			MenuSkin::TIconSize mainIconSize=!bSecondColumn ? s_Skin.Main_icon_size : s_Skin.Main2_icon_size;
			if (item.bInline && mainIconSize==MenuSkin::ICON_SIZE_NONE)
				mainIconSize=s_Skin.Main_icon_size;
			
			CItemManager::TIconSizeType iconSizeType;
			int refreshFlags;
			if (bSearchProvider7 || m_bSubMenu)
			{
				iconSizeType=CItemManager::ICON_SIZE_TYPE_SMALL;
				refreshFlags=CItemManager::INFO_SMALL_ICON;
			}
			else if (s_bWin7Style)
			{
				iconSizeType=CItemManager::ICON_SIZE_TYPE_EXTRA_LARGE;
				refreshFlags=CItemManager::INFO_EXTRA_LARGE_ICON;
			}
			else if (mainIconSize==MenuSkin::ICON_SIZE_LARGE)
			{
				iconSizeType=CItemManager::ICON_SIZE_TYPE_LARGE;
				refreshFlags=CItemManager::INFO_LARGE_ICON;
			}
			else if (mainIconSize==MenuSkin::ICON_SIZE_SMALL)
			{
				iconSizeType=CItemManager::ICON_SIZE_TYPE_SMALL;
				refreshFlags=CItemManager::INFO_SMALL_ICON;
			}
			else
			{
				iconSizeType=CItemManager::ICON_SIZE_TYPE_SMALL;
				refreshFlags=0;
			}
			if (pStdItem->link)
				refreshFlags|=CItemManager::INFO_LINK|CItemManager::INFO_METRO;
			if ((refreshFlags&CItemManager::INFO_ICON)==0)
				item.pItemInfo=NULL;
			else if (pStdItem->iconPath)
			{
				if (_wcsicmp(pStdItem->iconPath,L"none")==0)
					item.pItemInfo=NULL;
				else
					item.pItemInfo=g_ItemManager.GetCustomIcon(pStdItem->iconPath,iconSizeType);
			}
			else if (networkType!=NETWORK_NONE && networkType!=NETWORK_FILE)
			{
				item.pItemInfo=g_ItemManager.GetCustomIcon(GetDefaultNetworkIcon(networkType),iconSizeType);
			}
			else if (item.pItem1)
			{
				CComPtr<IShellItem> pItem;
				if (SUCCEEDED(SHCreateItemFromIDList(item.pItem1,IID_IShellItem,(void**)&pItem)))
				{
					item.pItemInfo=g_ItemManager.GetItemInfo(pItem,item.pItem1,refreshFlags);
					CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
					item.bLink=item.bMetroLink=item.pItemInfo->IsMetroLink();
					item.bMetroApp=item.pItemInfo->IsMetroApp();
				}
			}
			else
				item.pItemInfo=g_ItemManager.GetCustomIcon(NULL,iconSizeType);

			// get name
			if (pStdItem->label && _wcsicmp(pStdItem->label,L"none")==0)
			{
				item.name.Empty();
			}
			else if (pStdItem->label)
			{
				if (item.id==MENU_LOGOFF)
				{
					// construct the text Log Off <username>...
					wchar_t user[256]={0};
					ULONG size=_countof(user);
					if (!GetUserNameEx(NameDisplay,user,&size))
					{
						// GetUserNameEx may fail (for example on Home editions). use the login name
						size=_countof(user);
						GetUserName(user,&size);
					}
					item.name.Format(pStdItem->label,user);
				}
				else if (item.id==MENU_SHUTDOWN_BUTTON)
				{
					s_ShutdownCommand=MENU_NO;
					int shutdown=GetSettingInt(L"ShutdownCommand");
					if (shutdown<0) shutdown=0;
					if (shutdown>=SHUTDOWN_TYPE_COUNT) shutdown=SHUTDOWN_TYPE_COUNT-1;

					bool enabled[SHUTDOWN_TYPE_COUNT]={0};
					enabled[SHUTDOWN_TYPE_SHUTDOWN]=(GetStdOptions(MENU_SHUTDOWN)&MENU_ENABLED)!=0;
					enabled[SHUTDOWN_TYPE_RESTART]=(GetStdOptions(MENU_RESTART)&MENU_ENABLED)!=0;
					enabled[SHUTDOWN_TYPE_LOGOFF]=(GetStdOptions(MENU_LOGOFF)&MENU_ENABLED)!=0;
					enabled[SHUTDOWN_TYPE_SLEEP]=(GetStdOptions(MENU_SLEEP)&MENU_ENABLED)!=0;
					enabled[SHUTDOWN_TYPE_HIBERNATE]=(GetStdOptions(MENU_HIBERNATE)&MENU_ENABLED)!=0;
					enabled[SHUTDOWN_TYPE_LOCK]=(GetStdOptions(MENU_LOCK)&MENU_ENABLED)!=0;
					enabled[SHUTDOWN_TYPE_SWITCHUSER]=(GetStdOptions(MENU_SWITCHUSER)&MENU_ENABLED)!=0;
					enabled[SHUTDOWN_TYPE_SHUTDOWN_BOX]=(GetStdOptions(MENU_SHUTDOWN_BOX)&MENU_ENABLED)!=0;
					if (!enabled[shutdown])
					{
						for (int i=1;i<SHUTDOWN_TYPE_COUNT;i++)
						{
							if (enabled[i])
							{
								shutdown=i;
								break;
							}
						}
					}
					if (!enabled[shutdown])
					{
						continue;
					}

					if (shutdown==SHUTDOWN_TYPE_SHUTDOWN)
					{
						item.name=FindTranslation(L"Menu.Shutdown",L"Sh&ut Down");
						if (s_bHasUpdates)
							const_cast<StdMenuItem*>(item.pStdItem)->tip=FindTranslation(L"Menu.ShutdownUpdate",L"Update and shut down");
						else
							const_cast<StdMenuItem*>(item.pStdItem)->tip=FindTranslation(L"Menu.ShutdownTip",L"");
						s_ShutdownCommand=MENU_SHUTDOWN;
					}
					if (shutdown==SHUTDOWN_TYPE_RESTART)
					{
						item.name=FindTranslation(L"Menu.Restart",L"&Restart");
						if (s_bHasUpdates && GetWinVersion()>=WIN_VER_WIN8)
							const_cast<StdMenuItem*>(item.pStdItem)->tip=FindTranslation(L"Menu.RestartUpdate",L"Update and restart");
						else
							const_cast<StdMenuItem*>(item.pStdItem)->tip=FindTranslation(L"Menu.RestartTip",L"");
						s_ShutdownCommand=MENU_RESTART;
					}
					if (shutdown==SHUTDOWN_TYPE_LOGOFF)
					{
						item.name=FindTranslation(L"Menu.LogOffShort",L"Log off");
						const_cast<StdMenuItem*>(item.pStdItem)->tip=FindTranslation(L"Menu.LogOffTip",L"");
						s_ShutdownCommand=MENU_LOGOFF;
					}
					if (shutdown==SHUTDOWN_TYPE_SLEEP)
					{
						item.name=FindTranslation(L"Menu.Sleep",L"&Sleep");
						const_cast<StdMenuItem*>(item.pStdItem)->tip=FindTranslation(L"Menu.SleepTip",L"");
						s_ShutdownCommand=MENU_SLEEP;
					}
					if (shutdown==SHUTDOWN_TYPE_HIBERNATE)
					{
						item.name=FindTranslation(L"Menu.Hibernate",L"&Hibernate");
						const_cast<StdMenuItem*>(item.pStdItem)->tip=FindTranslation(L"Menu.HibernateTip",L"");
						s_ShutdownCommand=MENU_HIBERNATE;
					}
					if (shutdown==SHUTDOWN_TYPE_SHUTDOWN_BOX)
					{
						item.name=FindTranslation(L"Menu.ShutdownBox",L"Sh&ut Down...");
						const_cast<StdMenuItem*>(item.pStdItem)->tip=L"";
						s_ShutdownCommand=MENU_SHUTDOWN_BOX;
					}
					if (shutdown==SHUTDOWN_TYPE_LOCK)
					{
						item.name=FindTranslation(L"Menu.Lock",L"Lock");
						const_cast<StdMenuItem*>(item.pStdItem)->tip=FindTranslation(L"Menu.LockTip",L"");
						s_ShutdownCommand=MENU_LOCK;
					}
					if (shutdown==SHUTDOWN_TYPE_SWITCHUSER)
					{
						item.name=FindTranslation(L"Menu.SwitchUser",L"Switch User");
						const_cast<StdMenuItem*>(item.pStdItem)->tip=FindTranslation(L"Menu.SwitchUserTip",L"");
						s_ShutdownCommand=MENU_SWITCHUSER;
					}
				}
				else if (s_bHasUpdates && m_bSubMenu && item.id==MENU_SHUTDOWN)
					item.name=FindTranslation(L"Menu.ShutdownUpdate",L"Update and shut down");
				else if (s_bHasUpdates && m_bSubMenu && item.id==MENU_RESTART && GetWinVersion()>=WIN_VER_WIN8)
					item.name=FindTranslation(L"Menu.RestartUpdate",L"Update and restart");
				else
					item.name=pStdItem->label;
			}
			else if (item.pItem1)
			{
				if (item.pItemInfo)
				{
					CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
					if (item.pItemInfo->IsMetroLink())
						item.name=item.pItemInfo->GetMetroName();
				}
				if (item.name.IsEmpty())
				{
					SHFILEINFO info={0};
					SHGetFileInfo((LPCWSTR)item.pItem1,0,&info,sizeof(info),SHGFI_PIDL|SHGFI_DISPLAYNAME);
					item.name=info.szDisplayName;
				}
				if (wcschr(item.name,'&'))
					item.name.Replace(L"&",L"&&");
			}
			else if (item.name.IsEmpty() && item.id!=MENU_SEPARATOR && item.id!=MENU_SEARCH_BOX && !item.bInline)
			{
				if ((pStdItem->command && wcscmp(pStdItem->command,L"<blank>")!=0) || pStdItem->link || pStdItem->folder1 || pStdItem->submenu || pStdItem->id!=MENU_CUSTOM)
					item.name=LoadStringEx(IDS_NO_TEXT);
				else
				{
					item.id=MENU_SEPARATOR;
					item.bBlankSeparator=true;
				}
			}

			item.bPrograms=(item.id==MENU_PROGRAMS || item.id==MENU_FAVORITES);
			if (item.bInline)
				item.bFolder=false;

			if (bSearchProvider7)
			{
				item.bInline=false;
				item.bFolder=false;
				item.id=MENU_SEARCH_PROVIDER;
				if (searchProviderIndex>=0)
				{
					m_Items.insert(m_Items.begin()+searchProviderIndex,1,item);
					searchProviderIndex++;
					menuIdx++;
					m_SearchProvidersCount++;
				}
			}
			else
			{
				m_Items.insert(m_Items.begin()+menuIdx,1,item);
				menuIdx++;
			}
		}
	}
}

void CMenuContainer::UpdateAccelerators( int first, int last )
{
	TRecentKeys recentKeys=(TRecentKeys)GetSettingInt(L"RecentProgKeys");

	for (int i=first;i<last;i++)
	{
		MenuItem &item=m_Items[i];
		if (item.id==MENU_SEPARATOR || item.id==MENU_EMPTY || item.id==MENU_EMPTY_TOP || item.id==MENU_SEARCH_EMPTY || item.id==MENU_SEARCH_BOX || item.name.IsEmpty() || (item.id==MENU_RECENT && recentKeys!=RECENT_KEYS_NORMAL))
			continue;

		const wchar_t *name=item.name;
		wchar_t buf[2]={name[0],0};
		while (1)
		{
			const wchar_t *c=wcschr(name,'&');
			if (!c || !c[1]) break;
			if (c[1]!='&')
			{
				buf[0]=c[1];
				item.bCustomAccelerator=true;
				break;
			}
			name=c+1;
		}
		CharUpper(buf); // always upper case
		item.accelerator=buf[0];
	}
}

void CMenuContainer::UpdateAutoComplete( const wchar_t *text )
{
	m_bInSearchUpdate=true;
	m_SearchBox.SetWindowText(text);
	m_bInSearchUpdate=false;
	int len=Strlen(text);
	m_SearchBox.SendMessage(EM_SETSEL,len,len);
}

struct UserAssistItem
{
	wchar_t name[_MAX_PATH];
	float rank;
	FILETIME timestamp;
	unsigned int mfuHash; // original case
	unsigned int nameHash; // all caps
	const CItemManager::ItemInfo *pLinkInfo;

	bool operator<( const UserAssistItem &item ) { return (rank>item.rank) || (rank==item.rank && CompareFileTime(&timestamp,&item.timestamp)>0); }
};

static const wchar_t *g_MfuIgnoreWords[]={
	L"DOCUMENTATION",
	L"HELP",
	L"INSTALL",
	L"MORE INFO",
	L"READ ME",
	L"READ FIRST",
	L"README",
	L"REMOVE",
	L"SETUP",
	L"SUPPORT",
	L"WHAT'S NEW",
};

static const wchar_t *g_MfuIgnoreExes[]={
	L"APPLAUNCH.EXE",
	L"CONTROL.EXE",
	L"DFSVC.EXE",
	L"DLLHOST.EXE",
	L"GUESTMODEMSG.EXE",
	L"HH.EXE",
	L"INSTALL.EXE",
	L"ISUNINST.EXE",
	L"LNKSTUB.EXE",
	L"MMC.EXE",
	L"MSHTA.EXE",
	L"MSIEXEC.EXE",
	L"MSOOBE.EXE",
	L"RUNDLL32.EXE",
	L"SETUP.EXE",
	L"ST5UNST.EXE",
	L"UNWISE.EXE",
	L"UNWISE32.EXE",
	L"WERFAULT.EXE",
	L"WINHLP32.EXE",
	L"WLRMDR.EXE",
	L"WUAPP.EXE",
};

static bool IgnoreUserAssistItem(const UserAssistItem& uaItem)
{
	static constexpr const wchar_t* ignoredNames[] =
	{
		DESKTOP_APP_ID,
		L"Microsoft.Windows.ShellExperienceHost_cw5n1h2txyewy!App",
	};

	for (const auto& name : ignoredNames)
	{
		if (_wcsicmp(uaItem.name, name) == 0)
			return true;
	}

	return false;
}

void CMenuContainer::GetRecentPrograms( std::vector<MenuItem> &items, int maxCount )
{
	bool bShowMetro=GetSettingBool(L"RecentMetroApps");
	const std::vector<MenuItem> &mainItems=s_Menus[0]->m_Items;

	int iconSizeFlag=(!m_bSubMenu && s_Skin.Main_icon_size==MenuSkin::ICON_SIZE_LARGE)?CItemManager::INFO_LARGE_ICON:CItemManager::INFO_SMALL_ICON;
	if (s_RecentPrograms==RECENT_PROGRAMS_RECENT)
	{
		for (int i=0;i<MRU_PROGRAMS_COUNT;i++)
		{
			if (s_MRUShortcuts[i].IsEmpty()) break;
			CComPtr<IShellItem> pItem;
			CAbsolutePidl pidl;
			bool bApp=(wcsncmp(s_MRUShortcuts[i],L"APP:",4)==0);
			if (bApp)
			{
				if (SUCCEEDED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder2,0,(const wchar_t*)s_MRUShortcuts[i]+4,IID_IShellItem,(void**)&pItem)) && FAILED(SHGetIDListFromObject(pItem,&pidl)))
					pItem=NULL;
			}
			else if (SUCCEEDED(MenuParseDisplayName(s_MRUShortcuts[i],&pidl,NULL,NULL)))
				SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pItem);
			if (pItem)
			{
				CComString pName;
				if (SUCCEEDED(pItem->GetDisplayName(SIGDN_NORMALDISPLAY,&pName)))
				{
					if (bApp && wcsncmp(pName,L"@{",2)==0)
						continue;
					// new item
					MenuItem item(MENU_RECENT);
					item.bLink=true;
					int refreshFlags=CItemManager::INFO_LINK|CItemManager::INFO_LINK_APPID|CItemManager::INFO_METRO|iconSizeFlag;
					item.pItemInfo=g_ItemManager.GetItemInfo(pItem,pidl,refreshFlags);

					// see if it is already in the main menu
					bool bFound=false;
					{
						CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
						for (std::vector<MenuItem>::const_iterator it=mainItems.begin();it!=mainItems.end();++it)
						{
							if (!it->pItemInfo) continue;
							if (it->pItemInfo==item.pItemInfo)
							{
								bFound=true;
								break;
							}
							if (!it->pItemInfo->IsLink() && !it->pItemInfo->IsMetroApp())
								continue;
							// if the item is an app, check if the link has the same target pidl
							// if the item has appid, compare by appid. otherwise compare by target path (shouldn't happen)
							if (bApp && it->pItemInfo->IsLink())
							{
								// for apps compare by pidl
								if (ILIsEqual(pidl,it->pItemInfo->GetTargetPidl()))
								{
									bFound=true;
									break;
								}
							}
							else if (!item.pItemInfo->GetAppid().IsEmpty())
							{
								if (_wcsicmp(item.pItemInfo->GetAppid(),it->pItemInfo->GetAppid())==0)
								{
									bFound=true;
									break;
								}
							}
							else
							{
								if (!item.pItemInfo->GetTargetPATH().IsEmpty() && wcscmp(item.pItemInfo->GetTargetPATH(),it->pItemInfo->GetTargetPATH())==0)
								{
									bFound=true;
									break;
								}
							}
						}
					}
					if (bFound) continue;
					{
						CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
						if (item.pItemInfo->IsMetroApp() && !bShowMetro)
							continue;
						if (item.pItemInfo->IsMetroLink())
						{
							if (!bShowMetro) continue;
							item.bMetroLink=true;
							if (_wcsicmp(item.pItemInfo->GetAppid(),DESKTOP_APP_ID)==0)
								continue;
							if (item.pItemInfo->GetMetroName().IsEmpty())
								item.SetName(pName,(m_Options&CONTAINER_NOEXTENSIONS)!=0);
							else
								item.SetName(item.pItemInfo->GetMetroName(),false);
						}
						else
							item.SetName(pName,(m_Options&CONTAINER_NOEXTENSIONS)!=0);
						item.bMetroApp=item.pItemInfo->IsMetroApp();
					}
					item.pItem1=pidl.Detach();

					items.push_back(item);
				}
			}
			if ((int)items.size()==maxCount)
				break;
		}
	}
	else if (s_RecentPrograms==RECENT_PROGRAMS_FREQUENT)
	{
		ULONGLONG curTime;
		GetSystemTimeAsFileTime((FILETIME*)&curTime);
		std::vector<UserAssistItem> uaItems;
		CRegKey regKeyApp, regKeyLink;
		if (regKeyApp.Open(HKEY_CURRENT_USER,USERASSIST_APPIDS_KEY,KEY_READ)!=ERROR_SUCCESS ||
			regKeyLink.Open(HKEY_CURRENT_USER,USERASSIST_LINKS_KEY,KEY_READ)!=ERROR_SUCCESS)
		{
			LOG_MENU(LOG_MFU,L"Failed to open UserAssist registry key");
			return;
		}

		// collect links with positive rank from regKeyLink
		for (int idx=0;;idx++)
		{
			UserAssistItem uaItem;
			memset(&uaItem,0,sizeof(uaItem));
			DWORD len=_countof(uaItem.name);
			UserAssistData data;
			memset(&data,0,sizeof(data));
			DWORD size=sizeof(data);
			DWORD type;
			LONG res=RegEnumValue(regKeyLink,idx,uaItem.name,&len,NULL,&type,(BYTE*)&data,&size);
			if (res==ERROR_NO_MORE_ITEMS)
				break;
			if (!*uaItem.name)
				continue;
			if (type!=REG_BINARY)
			{
				LOG_MENU(LOG_MFU,L"Not binary: '%s'",uaItem.name);
				continue;
			}
			if (size!=sizeof(data))
			{
				LOG_MENU(LOG_MFU,L"Wrong size (%d): '%s'",size,uaItem.name);
				continue;
			}
			uaItem.mfuHash=CalcFNVHash(uaItem.name);
			EncodeRot13(uaItem.name);
			if (data.timestamp.dwLowDateTime==0 && data.timestamp.dwHighDateTime==0)
			{
				LOG_MENU(LOG_MFU,L"Zero timestamp: '%s'",uaItem.name);
				continue;
			}
			// determine rank
			uaItem.rank=data.count/10.f;
			float weight=0.95f;
			for (int i=0, idx=data.last;i<_countof(data.history);i++,idx=(idx+9)%10,weight-=0.07f)
			{
				float h=data.history[idx];
				if (h<0 || h>1) break;
				uaItem.rank+=h*weight;
			}
			if (uaItem.rank==0)
			{
				LOG_MENU(LOG_MFU,L"Zero rank: '%s'",uaItem.name);
				continue;
			}

			// decode name
			const wchar_t *ext=PathFindExtension(uaItem.name);
			if (uaItem.name[0]=='{')
			{
				wchar_t *end=wcschr(uaItem.name+1,'}');
				if (end && end[1]=='\\')
				{
					end[1]=0;
					GUID guid;
					HRESULT hr=CLSIDFromString(uaItem.name,&guid);
					end[1]='\\';
					if (SUCCEEDED(hr))
					{
						CComString pPath;
						if (SUCCEEDED(SHGetKnownFolderPath(guid,KF_FLAG_DONT_VERIFY,NULL,&pPath)))
						{
							wchar_t path[_MAX_PATH];
							Sprintf(path,_countof(path),L"%s%s",(const wchar_t*)pPath,end+1);
							Strcpy(uaItem.name,_countof(uaItem.name),path);
						}
					}
				}
			}
			LOG_MENU(LOG_MFU,L"UserAssist: '%s', %d, %.3f",uaItem.name,data.count,uaItem.rank);
			bool bIgnore=false;
			wchar_t NAME[_MAX_PATH];
			Strcpy(NAME,_countof(NAME),uaItem.name);
			CharUpper(NAME);
			uaItem.nameHash=CalcFNVHash(NAME);
			for (std::vector<UserAssistItem>::const_iterator it=uaItems.begin();it!=uaItems.end();++it)
			{
				if (uaItem.nameHash==it->nameHash)
				{
					LOG_MENU(LOG_MFU,L"UserAssist: Dropping: Already in list");
					bIgnore=true;
					break;
				}
			}
			if (bIgnore) continue;
			const wchar_t *fname=PathFindFileName(NAME);
			for (int i=0;i<_countof(g_MfuIgnoreWords);i++)
				if (wcswcs(fname,g_MfuIgnoreWords[i]))
				{
					LOG_MENU(LOG_MFU,L"UserAssist: Dropping: Contains bad word");
					bIgnore=true;
					break;
				}
			if (bIgnore) continue;

			uaItem.pLinkInfo=g_ItemManager.GetItemInfo(uaItem.name,CItemManager::INFO_LINK|CItemManager::INFO_LINK_APPID|CItemManager::INFO_METRO|CItemManager::INFO_VALIDATE_FILE);
			if (!uaItem.pLinkInfo)
			{
				LOG_MENU(LOG_MFU,L"UserAssist: Dropping: File missing");
				continue;
			}
			wchar_t appid[_MAX_PATH];
			{
				CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
				if (uaItem.pLinkInfo->GetLocation()!=CItemManager::LOCATION_START_MENU && uaItem.pLinkInfo->GetLocation()!=CItemManager::LOCATION_METRO && uaItem.pLinkInfo->GetLocation()!=CItemManager::LOCATION_GAMES)
				{
					LOG_MENU(LOG_MFU,L"UserAssist: Dropping: Not from start menu");
					continue;
				}
				if (uaItem.pLinkInfo->IsNoPin())
				{
					LOG_MENU(LOG_MFU,L"UserAssist: Dropping: No pin");
					continue;
				}
				if (uaItem.pLinkInfo->GetAppid().IsEmpty())
				{
					LOG_MENU(LOG_MFU,L"UserAssist: Dropping: No AppId");
					continue;
				}
				if (!uaItem.pLinkInfo->GetTargetPATH().IsEmpty())
				{
					const wchar_t *fname=PathFindFileName(uaItem.pLinkInfo->GetTargetPATH());
					for (int i=0;i<_countof(g_MfuIgnoreExes);i++)
						if (wcswcs(fname,g_MfuIgnoreExes[i]))
						{
							LOG_MENU(LOG_MFU,L"UserAssist: Dropping: Target contains bad word: '%s'",uaItem.pLinkInfo->GetTargetPATH());
							bIgnore=true;
							break;
						}
				}
				if (uaItem.pLinkInfo->IsMetroLink())
				{
					if (!bShowMetro)
					{
						LOG_MENU(LOG_MFU,L"UserAssist: Dropping: No show metro");
						continue;
					}
					if (_wcsicmp(uaItem.pLinkInfo->GetAppid(),DESKTOP_APP_ID)==0)
					{
						LOG_MENU(LOG_MFU,L"UserAssist: Dropping: Ignore desktop");
						continue;
					}
					if (uaItem.pLinkInfo->GetMetroName().IsEmpty())
					{
						LOG_MENU(LOG_MFU,L"UserAssist: Dropping: No Metro name");
						continue;
					}
				}
				if (bIgnore) continue;
				Strcpy(appid,_countof(appid),uaItem.pLinkInfo->GetAppid());
			}

			uaItem.timestamp=data.timestamp;
			size=sizeof(data);
			EncodeRot13(appid);
			if (regKeyApp.QueryBinaryValue(appid,&data,&size)!=ERROR_SUCCESS)
			{
				LOG_MENU(LOG_MFU,L"UserAssist: Dropping: Missing appid entry: '%s'",appid);
				continue;
			}
			if (CompareFileTime(&uaItem.timestamp,&data.timestamp)<0)
				uaItem.timestamp=data.timestamp;

			{
				float rank=data.count/10.f;
				float weight=0.95f;
				for (int i=0, idx=data.last;i<_countof(data.history);i++,idx=(idx+9)%10,weight-=0.07f)
				{
					float h=data.history[idx];
					if (h<0 || h>1) break;
					rank+=h*weight;
				}
				if (uaItem.rank<rank)
				{
					uaItem.rank=rank;
					LOG_MENU(LOG_MFU,L"UserAssist: Corrected rank: %.3f",rank);
				}
			}

			uaItems.push_back(uaItem);
		}

		if (GetWinVersion()>=WIN_VER_WIN10 && bShowMetro)
		{
			// collect apps with positive rank from regKeyApp
			for (int idx=0;;idx++)
			{
				UserAssistItem uaItem;
				memset(&uaItem,0,sizeof(uaItem));
				DWORD len=_countof(uaItem.name);
				UserAssistData data;
				memset(&data,0,sizeof(data));
				DWORD size=sizeof(data);
				DWORD type;
				LONG res=RegEnumValue(regKeyApp,idx,uaItem.name,&len,NULL,&type,(BYTE*)&data,&size);
				if (res==ERROR_NO_MORE_ITEMS)
					break;
				if (!*uaItem.name)
					continue;
				if (type!=REG_BINARY)
				{
					LOG_MENU(LOG_MFU,L"Not binary: '%s'",uaItem.name);
					continue;
				}
				if (size!=sizeof(data))
				{
					LOG_MENU(LOG_MFU,L"Wrong size (%d): '%s'",size,uaItem.name);
					continue;
				}
				uaItem.mfuHash=CalcFNVHash(uaItem.name);
				EncodeRot13(uaItem.name);
				if (data.timestamp.dwLowDateTime==0 && data.timestamp.dwHighDateTime==0)
				{
					LOG_MENU(LOG_MFU,L"Zero timestamp: '%s'",uaItem.name);
					continue;
				}

				// determine rank
				uaItem.rank=data.count/10.f;
				float weight=0.95f;
				for (int i=0, idx=data.last;i<_countof(data.history);i++,idx=(idx+9)%10,weight-=0.07f)
				{
					float h=data.history[idx];
					if (h<0 || h>1) break;
					uaItem.rank+=h*weight;
				}
				if (uaItem.rank==0)
				{
					LOG_MENU(LOG_MFU,L"Zero rank: '%s'",uaItem.name);
					continue;
				}

				if (IgnoreUserAssistItem(uaItem))
				{
					LOG_MENU(LOG_MFU,L"UserAssist: Dropping: Ignore '%s'",uaItem.name);
					continue;
				}

				uaItem.pLinkInfo=g_ItemManager.GetMetroAppInfo10(uaItem.name);
				if (!uaItem.pLinkInfo)
				{
					LOG_MENU(LOG_MFU,L"UserAssist: Dropping '%s': Not a valid app",uaItem.name);
					continue;
				}

				LOG_MENU(LOG_MFU,L"UserAssist: '%s', %d, %.3f",uaItem.name,data.count,uaItem.rank);
				{
					CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
					if (uaItem.pLinkInfo->GetMetroName().IsEmpty() || wcsncmp(uaItem.pLinkInfo->GetMetroName(), L"@{",2)==0)
					{
						LOG_MENU(LOG_MFU, L"UserAssist: Dropping: No metro name");
						continue;
					}
					if (uaItem.pLinkInfo->IsNoPin())
					{
						LOG_MENU(LOG_MFU,L"UserAssist: Dropping: No pin");
						continue;
					}
					if (uaItem.pLinkInfo->GetPackagePath().IsEmpty() || GetFileAttributes(uaItem.pLinkInfo->GetPackagePath())==INVALID_FILE_ATTRIBUTES)
					{
						LOG_MENU(LOG_MFU,L"UserAssist: Dropping: Missing package path");
						continue;
					}

					CAbsolutePidl pidl=uaItem.pLinkInfo->GetPidl();

					// see if it is already in the main menu
					bool bFound=false;
					for (std::vector<MenuItem>::const_iterator it=mainItems.begin();it!=mainItems.end();++it)
					{
						if (!it->pItemInfo) continue;
						if (!it->pItemInfo->IsLink()) continue;
						if (ILIsEqual(pidl,it->pItemInfo->GetTargetPidl()))
						{
							LOG_MENU(LOG_MFU,L"UserAssist: Dropping '%s', already in main menu",uaItem.name);
							bFound=true;
							break;
						}
					}
					if (bFound) continue;
				}

				uaItem.timestamp=data.timestamp;
				uaItems.push_back(uaItem);
			}
		}

		// sort by rank
		std::sort(uaItems.begin(),uaItems.end());

		g_ItemManager.WaitForShortcuts(CPoint((s_StartRect.left+s_StartRect.right)/2,(s_StartRect.top+s_StartRect.bottom)/2));

		// create a menu item for each program
		for (int i=0;i<(int)uaItems.size();i++)
		{
			UserAssistItem &uaItem=uaItems[i];
			if (!uaItem.pLinkInfo) continue;

			bool bMetroLink, bExplicitAppId, bMetroApp;
			CString appid, targetPATH, metroName;
			{
				CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
				bMetroLink=uaItem.pLinkInfo->IsMetroLink();
				bMetroApp=uaItem.pLinkInfo->IsMetroApp();
				bExplicitAppId=uaItem.pLinkInfo->IsExplicitAppId();
				appid=uaItem.pLinkInfo->GetAppid();
				targetPATH=uaItem.pLinkInfo->GetTargetPATH();
				if (bMetroLink)
					metroName=uaItem.pLinkInfo->GetMetroName();
				// clear all other items with the same link target
				for (int j=i+1;j<(int)uaItems.size();j++)
				{
					UserAssistItem &uaItem2=uaItems[j];
					if (!uaItem2.pLinkInfo) continue;
					if (_wcsicmp(appid,uaItem2.pLinkInfo->GetAppid())==0)
					{
						LOG_MENU(LOG_MFU,L"UserAssist: Dropping '%s', duplicate appid '%s'",uaItem2.name,appid);
						uaItem2.pLinkInfo=NULL;
					}
				}
			}

			if (bExplicitAppId)
			{
				CRegKey keyPin;
				if (keyPin.Open(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileAssociation\\NoStartPageAppUserModelIDs",KEY_READ)==ERROR_SUCCESS)
				{
					if (keyPin.QueryValue(appid,NULL,NULL,NULL)==ERROR_SUCCESS)
					{
						LOG_MENU(LOG_MFU,L"UserAssist: Dropping '%s', NoStartPageAppUserModelIDs",uaItem.name);
						continue;
					}
				}
			}
			else
			{
				wchar_t text[1024];
				{
					Sprintf(text,_countof(text),L"Applications\\%s",PathFindFileName(targetPATH));
					CRegKey keyPin;
					if (keyPin.Open(HKEY_CLASSES_ROOT,text,KEY_READ)==ERROR_SUCCESS)
					{
						if (keyPin.QueryValue(L"NoStartPage",NULL,NULL,NULL)==ERROR_SUCCESS)
						{
							LOG_MENU(LOG_MFU,L"UserAssist: Dropping '%s', NoStartPage",uaItem.name);
							continue;
						}
					}
				}
				if (wcschr(appid,'\\'))
				{
					Sprintf(text,_countof(text),L"Applications\\%s",PathFindFileName(appid));
					CRegKey keyPin;
					if (keyPin.Open(HKEY_CLASSES_ROOT,text,KEY_READ)==ERROR_SUCCESS)
					{
						if (keyPin.QueryValue(L"NoStartPage",NULL,NULL,NULL)==ERROR_SUCCESS)
						{
							LOG_MENU(LOG_MFU,L"UserAssist: Dropping '%s', NoStartPage",uaItem.name);
							continue;
						}
					}
				}
			}

			// see if it is in the taskbar
			if (!bMetroLink && g_ItemManager.IsTaskbarPinned(appid))
			{
				LOG_MENU(LOG_MFU,L"UserAssist: Dropping '%s', pinned to taskbar",uaItem.name);
				continue;
			}
			// see if it is already in the main menu
			bool bFound=false;
			{
				CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
				for (std::vector<MenuItem>::const_iterator it=mainItems.begin();it!=mainItems.end();++it)
				{
					if (!it->pItemInfo) continue;
					if (it->pItemInfo==uaItem.pLinkInfo)
					{
						LOG_MENU(LOG_MFU,L"UserAssist: Dropping '%s', already in main menu",uaItem.name);
						bFound=true;
						break;
					}
					if (!it->pItemInfo->IsLink() && !it->pItemInfo->IsMetroApp())
						continue;
					if (_wcsicmp(appid,it->pItemInfo->GetAppid())==0)
					{
						LOG_MENU(LOG_MFU,L"UserAssist: Dropping '%s', already in main menu",uaItem.name);
						bFound=true;
						break;
					}
				}
			}
			if (bFound) continue;

			MenuItem item(MENU_RECENT);
			item.bLink=true;
			item.pItemInfo=uaItem.pLinkInfo;
			item.mfuHash=uaItem.mfuHash;
			item.bMetroLink=bMetroLink;
			item.bMetroApp=bMetroApp;
			g_ItemManager.UpdateItemInfo(uaItem.pLinkInfo,iconSizeFlag);
			CComPtr<IShellItem> pItem;
			if (bMetroLink)
			{
				item.SetName(metroName,false);
				item.pItem1=ILCloneFull(uaItem.pLinkInfo->GetPidl());
				items.push_back(item);
			}
			else if (item.name.IsEmpty() && SUCCEEDED(SHCreateItemFromIDList(uaItem.pLinkInfo->GetPidl(),IID_IShellItem,(void**)&pItem)))
			{
				CComString pName;
				if (SUCCEEDED(pItem->GetDisplayName(SIGDN_NORMALDISPLAY,&pName)))
				{
					item.SetName(pName,(m_Options&CONTAINER_NOEXTENSIONS)!=0);
					item.pItem1=ILCloneFull(uaItem.pLinkInfo->GetPidl());
					items.push_back(item);
				}
			}
			
			if ((int)items.size()==maxCount)
				break;
		}
	}

	TRecentKeys recentKeys=(TRecentKeys)GetSettingInt(L"RecentProgKeys");
	for (int idx=0;idx<(int)items.size();idx++)
	{
		MenuItem &item=items[idx];
		item.mruOrder=idx;
		if (recentKeys==RECENT_KEYS_DIGITS)
			item.name.Replace(L"&",L"&&");
		if (idx<10)
		{
			if (recentKeys>=RECENT_KEYS_DIGITS)
			{
				item.accelerator=((idx+1)%10)+'0';
				item.bCustomAccelerator=true;
			}
			if (recentKeys==RECENT_KEYS_DIGITS)
			{
				CString str=item.name;
				item.name.Format(L"&%d %s",(idx+1)%10,str);
				item.nameOffset=item.name.GetLength()-str.GetLength();
			}
		}
	}
}

void CMenuContainer::ClearItems( const std::vector<MenuItem>::iterator &begin, const std::vector<MenuItem>::iterator &end )
{
	for (std::vector<MenuItem>::iterator it=begin;it!=end;++it)
	{
		if (it->pItem1) ILFree(it->pItem1);
		if (it->pItem2) ILFree(it->pItem2);
	}
	if (s_pDragSource==this && m_Items.begin()+m_DragIndex>=begin)
		s_pDragSource=NULL; // freeing the dragged item
	m_Items.erase(begin,end);
}

void CMenuContainer::AddJumpListItems( std::vector<MenuItem> &items )
{
	s_JumpList.Clear();
	g_ItemManager.UpdateItemInfo(s_JumpAppInfo,CItemManager::INFO_LINK_APPID);
	CString appid;
	{
		CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
		appid=s_JumpAppInfo->GetAppid();
	}
	if (!appid.IsEmpty())
	{
		int maxHeight=65536, sepHeight=0, itemHeight=0;
		if (s_bWin7Style)
		{
			itemHeight=s_Skin.ItemSettings[MenuSkin::LIST_ITEM].itemHeight;
			sepHeight=s_Skin.ItemSettings[MenuSkin::LIST_SEPARATOR].itemHeight;
			maxHeight=m_Items[m_OriginalCount-1].itemRect.top-s_Skin.Main_jump_padding.top;
		}
		GetJumplist(appid,s_JumpList,GetSettingInt(L"MaxJumplists"),maxHeight,sepHeight,itemHeight);
	}

	for (int g=0;g<(int)s_JumpList.groups.size();g++)
	{
		const CJumpGroup &group=s_JumpList.groups[g];
		if (group.bHidden) continue;
		{
			MenuItem item(MENU_SEPARATOR);
			item.SetName(group.name,false);
			items.push_back(item);
		}
		for (int i=0;i<(int)group.items.size();i++)
		{
			const CJumpItem &jumpItem=group.items[i];
			if (jumpItem.bHidden) continue;

			MenuItem item(MENU_NO);
			if (jumpItem.type==CJumpItem::TYPE_LINK)
			{
				item.SetName(jumpItem.name,false);
				CComQIPtr<IShellLink> pLink(jumpItem.pItem);
				if (pLink)
				{
					pLink->GetIDList(&item.pItem1);
					item.pItemInfo = g_ItemManager.GetLinkIcon(pLink, CItemManager::ICON_SIZE_TYPE_SMALL);
				}
			}
			else if (jumpItem.type==CJumpItem::TYPE_ITEM)
			{
				item.SetName(jumpItem.name,false);
				CComQIPtr<IShellItem> pItem(jumpItem.pItem);
				if (pItem)
					SHGetIDListFromObject(pItem,&item.pItem1);
			}
			else if (jumpItem.type==CJumpItem::TYPE_SEPARATOR)
			{
				item.id=MENU_SEPARATOR;
			}
			item.bSplit=(jumpItem.type!=CJumpItem::TYPE_SEPARATOR && group.type!=CJumpGroup::TYPE_TASKS);
			if (item.pItem1 && !item.pItemInfo)
			{
				CComPtr<IShellItem> pItem;
				if (SUCCEEDED(SHCreateItemFromIDList(item.pItem1,IID_IShellItem,(void**)&pItem)))
				{
					// do some pidl laundering. sometimes the pidls from the jumplists may contain weird hidden data, which affects the icon
					// so do a round-trip convertion of the pidl to a display name
					CComString pName;
					if (SUCCEEDED(pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName)))
					{
						CComPtr<IShellItem> pItem2;
						CAbsolutePidl pidl2;
						if (SUCCEEDED(MenuParseDisplayName(pName,&pidl2,NULL,NULL)) && SUCCEEDED(SHCreateItemFromIDList(pidl2,IID_IShellItem,(void**)&pItem2)))
						{
							int order;
							if (SUCCEEDED(pItem->Compare(pItem2,SICHINT_CANONICAL,&order)) && order==0)
							{
								ILFree(item.pItem1);
								item.pItem1=pidl2.Detach();
								pItem=std::move(pItem2);
							}
						}
					}
					item.pItemInfo=g_ItemManager.GetItemInfo(pItem,item.pItem1,CItemManager::INFO_SMALL_ICON);
				}
			}
			item.jumpIndex=MAKELONG(g,i);
			items.push_back(item);
#ifdef REPEAT_JUMPLIST_ITEMS
			for (int i=0;i<REPEAT_JUMPLIST_ITEMS;i++)
			{
				item.pItem1=ILCloneFull(item.pItem1);
				items.push_back(item);
			}
#endif
		}
	}

	if (items.empty() || (m_bTwoColumns && items.size()==m_OriginalCount))
	{
		// add (Empty) item to the empty submenus
		MenuItem item(MENU_EMPTY);
		item.name=FindTranslation(L"Menu.Empty",L"(Empty)");
		items.push_back(item);
	}
}

// Initialize the m_Items list
void CMenuContainer::InitItems( void )
{
	ClearItems(m_Items.begin(),m_Items.end());
	m_pShellView=NULL;
	m_RefreshPosted=0;
	m_bRefreshItems=false;
	SetSubmenu(-1);
	SetContextItem(-1);
	s_HotPos=GetMessagePos();
	m_ScrollCount=0;
	m_SearchScrollCount=0;
	m_SearchScrollHeight=0;
	m_SearchScrollPos=0;
	m_SearchIndex=-1;

	if (m_Options&CONTAINER_JUMPLIST)
	{
		AddJumpListItems(m_Items);
		m_ScrollCount=0;
		if (m_Items.size()>0 && m_Items[0].id!=MENU_EMPTY)
			m_ScrollCount=(int)m_Items.size();
		m_OriginalScrollCount=m_ScrollCount;
		UpdateAccelerators(0,(int)m_Items.size());
		return;
	}

	if ((m_Options&CONTAINER_DOCUMENTS) && s_MaxRecentDocuments>0) // create the recent documents list
	{
		Assert(m_Path1[0] && !m_Path2[0]);

		// find all documents

		// with many recent files it takes a long time to go through the IShellFolder enumeration
		// so use FindFirstFile directly
		wchar_t recentPath[_MAX_PATH];
		SHGetPathFromIDList(m_Path1[0],recentPath);
		wchar_t find[_MAX_PATH];
		Sprintf(find,_countof(find),L"%s\\*.lnk",recentPath);

		std::vector<Document> docs;

		WIN32_FIND_DATA data;
		HANDLE h=FindFirstFile(find,&data);
		while (h!=INVALID_HANDLE_VALUE)
		{
			Document doc;
			doc.name.Format(L"%s\\%s",recentPath,data.cFileName);
			doc.time=data.ftLastWriteTime;
			docs.push_back(doc);
			if (!FindNextFile(h,&data))
			{
				FindClose(h);
				break;
			}
		}

		// sort by time
		std::sort(docs.begin(),docs.end());

		size_t count=0;
		CComPtr<IShellLink> pLink;
		if (SUCCEEDED(pLink.CoCreateInstance(CLSID_ShellLink)))
		{
			CComQIPtr<IPersistFile> pFile(pLink);
			if (pFile)
			{
				// go through the items until we find s_MaxRecentDocuments documents
				for (std::vector<Document>::const_iterator it=docs.begin();it!=docs.end();++it)
				{
					wchar_t path[_MAX_PATH];
					// find the target of the lnk file
					if (SUCCEEDED(pFile->Load(it->name,STGM_READ)) && SUCCEEDED(pLink->GetPath(path,_countof(path),&data,0)))
					{
						// check if it is link to a file or directory
						if (path[0] && !(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))
						{
							CAbsolutePidl pidl;
							MenuParseDisplayName(it->name,&pidl,NULL,NULL);
							CComPtr<IShellItem> pItem;
							CComString pName;
							if (pidl && SUCCEEDED(SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pItem)) && SUCCEEDED(pItem->GetDisplayName(SIGDN_NORMALDISPLAY,&pName)))
							{
								if (_wcsicmp(PathFindExtension(pName),L".lnk")==0)
									continue;
								MenuItem item(MENU_NO);
								item.pItem1=pidl.Detach();
								item.pItemInfo=g_ItemManager.GetItemInfo(pItem,item.pItem1,CItemManager::INFO_SMALL_ICON);
								item.SetName(pName,(m_Options&CONTAINER_NOEXTENSIONS)!=0);

								m_Items.push_back(item);
								count++;
								if ((int)count>=s_MaxRecentDocuments) break;
							}
						}
					}
				}
			}
		}
	}

	if ((m_Options&CONTAINER_APPS) && GetWinVersion()>=WIN_VER_WIN8)
	{
		std::vector<MetroLink> links;
		GetMetroLinks(links,true);

		for (std::vector<MetroLink>::iterator it=links.begin();it!=links.end();++it)
		{
			const CItemManager::ItemInfo *pInfo=g_ItemManager.GetItemInfo(it->pItem,it->pidl,CItemManager::INFO_METRO|CItemManager::INFO_SMALL_ICON);
			CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
			if (_wcsicmp(pInfo->GetAppid(),DESKTOP_APP_ID)==0)
				continue;
			CString name;
			if (pInfo->IsMetroLink())
				name=pInfo->GetMetroName();
			else
			{
				CComString pName;
				if (FAILED(it->pItem->GetDisplayName(SIGDN_NORMALDISPLAY,&pName)))
					continue;
				if (wcsncmp(pName,L"@{",2)==0)
					continue; // bad name
				name=pName;
			}
			MenuItem item(MENU_NO);
			item.SetName(name,false);
			StringUpper(name);
			item.nameHash=CalcFNVHash(name);
			item.pItemInfo=pInfo;
			item.pItem1=it->pidl.Detach();
			item.bMetroLink=pInfo->IsMetroLink();
			item.bMetroApp=item.pItemInfo->IsMetroApp();
			m_Items.push_back(item);
		}
	}

	// add first folder
	if (!(m_Options&CONTAINER_DOCUMENTS) && !(m_Options&CONTAINER_APPS))
	{
		if (m_Path1[0])
		{
			CComPtr<IShellItem> pItem;
			if (SUCCEEDED(SHCreateItemFromIDList(m_Path1[0],IID_IShellItem,(void**)&pItem)))
			{
				pItem->BindToHandler(NULL,BHID_SFViewObject,IID_IShellView,(void**)&m_pShellView);
				m_pDropFolder[0]=pItem;
				CComString pName;
				if (SUCCEEDED(pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName)))
				{
					pName.MakeUpper();
					m_FolderHash[0]=CalcFNVHash(pName);
				}
				AddFirstFolder(pItem,m_Items,m_Options);
			}
		}

		// add second folder
		if (m_Path2[0])
		{
			CComPtr<IShellItem> pItem;
			if (SUCCEEDED(SHCreateItemFromIDList(m_Path2[0],IID_IShellItem,(void**)&pItem)))
			{
				CComString pName;
				if (SUCCEEDED(pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName)))
				{
					pName.MakeUpper();
					m_FolderHash[0]=CalcFNVHash(pName,m_FolderHash[0]);
				}
				AddSecondFolder(pItem,m_Items,m_Options);
			}
		}
	}

	if (m_Options&CONTAINER_ALLPROGRAMS)
	{
		std::vector<MenuItem> items;
		if (m_Path1[1])
		{
			CComPtr<IShellItem> pItem;
			if (SUCCEEDED(SHCreateItemFromIDList(m_Path1[1],IID_IShellItem,(void**)&pItem)))
			{
				m_pDropFolder[1]=pItem;
				CComString pName;
				if (SUCCEEDED(pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName)))
				{
					pName.MakeUpper();
					m_FolderHash[1]=CalcFNVHash(pName);
				}
				AddFirstFolder(pItem,items,m_Options);
			}
		}

		// add second folder
		if (m_Path2[1])
		{
			CComPtr<IShellItem> pItem;
			if (SUCCEEDED(SHCreateItemFromIDList(m_Path2[1],IID_IShellItem,(void**)&pItem)))
			{
				CComString pName;
				if (SUCCEEDED(pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName)))
				{
					pName.MakeUpper();
					m_FolderHash[1]=CalcFNVHash(pName,m_FolderHash[1]);
				}
				AddSecondFolder(pItem,items,m_Options);
			}
		}
		if (!items.empty())
		{
			if (!m_Items.empty())
			{
				MenuItem item(MENU_SEPARATOR);
				item.priority=1;
				m_Items.push_back(item);
			}
			for (std::vector<MenuItem>::iterator it=items.begin();it!=items.end();++it)
				it->priority=2;
			m_Items.insert(m_Items.end(),items.begin(),items.end());
		}
	}

	// sort m_Items or read order from the registry
	LoadItemOrder();

	if (m_Items.size()>MAX_MENU_ITEMS)
	{
		for (size_t i=MAX_MENU_ITEMS;i<m_Items.size();i++)
		{
			if (m_Items[i].pItem1) ILFree(m_Items[i].pItem1);
			if (m_Items[i].pItem2) ILFree(m_Items[i].pItem2);
		}
		m_Items.resize(MAX_MENU_ITEMS);
	}

	if (m_Options&CONTAINER_CONTROLPANEL && !(m_Options&CONTAINER_NOSUBFOLDERS))
	{
		// expand Administrative Tools when displaying as a menu. must be done after the sorting because we don't want the folder to jump to the top
		unsigned int AdminToolsHash=CalcFNVHash(L"::{D20EA4E1-3957-11D2-A40B-0C5020524153}");
		for (std::vector<MenuItem>::iterator it=m_Items.begin();it!=m_Items.end();++it)
			if (it->nameHash==AdminToolsHash)
			{
				it->bFolder=true;
				break;
			}
	}

	if (m_Items.empty() && m_Path1[0] && m_pDropFolder[0])
	{
		// add (Empty) item to the empty submenus
		MenuItem item(m_bSubMenu?MENU_EMPTY:MENU_EMPTY_TOP);
		item.name=FindTranslation(L"Menu.Empty",L"(Empty)");
		m_Items.push_back(item);
	}

	if (!m_bSubMenu)
	{
		for (std::vector<MenuItem>::iterator it=m_Items.begin();it!=m_Items.end();++it)
		{
			MenuItem &item=*it;
			if (item.bLink && !item.bFolder && item.pItem1)
				g_ItemManager.UpdateItemInfo(item.pItemInfo,CItemManager::INFO_LINK_APPID);
		}
	}

	if (m_Options&CONTAINER_RECENT)
	{
		int nRecent=GetSettingInt(L"MaxRecentPrograms");
		if (nRecent<0)
			nRecent=-nRecent;
		if (nRecent>MRU_PROGRAMS_COUNT) nRecent=MRU_PROGRAMS_COUNT;
		if (nRecent>0)
		{
			// prepend recent programs
			std::vector<MenuItem> items;
			GetRecentPrograms(items,nRecent);
			if (!items.empty())
			{
				MenuItem item(MENU_SEPARATOR);
				if (GetSettingBool(L"RecentProgsTop"))
				{
					items.push_back(item);
					m_Items.insert(m_Items.begin(),items.begin(),items.end());
				}
				else
				{
					m_Items.push_back(item);
					m_Items.insert(m_Items.end(),items.begin(),items.end());
				}
			}
		}
	}

	if (!m_bSubMenu && GetSettingBool(L"EnableJumplists"))
	{
		for (std::vector<MenuItem>::iterator it=m_Items.begin();it!=m_Items.end();++it)
		{
			MenuItem &item=*it;
			if (item.bLink && !item.bFolder && item.pItem1)
			{
				CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
				item.bFolder=(!item.pItemInfo->GetAppid().IsEmpty() && HasJumplist(item.pItemInfo->GetAppid()));
				item.bHasJumpList=item.bFolder;
				item.bSplit=item.bFolder;
			}
		}
	}

	m_ScrollCount=(int)m_Items.size();
	if (m_ScrollCount>0 && m_Items[m_ScrollCount-1].id==MENU_PROGRAMS_TREE)
		m_ScrollCount--;

	if (s_bWin7Style && !m_bSubMenu && GetSettingInt(L"ProgramsStyle")!=PROGRAMS_HIDDEN)
	{
		MenuItem item(MENU_PROGRAMS_TREE);
		m_Items.push_back(item);
	}
	AddStandardItems();

	// remove trailing separators
	while (!m_Items.empty() && m_Items[m_Items.size()-1].id==MENU_SEPARATOR && !m_Items[m_Items.size()-1].bInline)
		m_Items.pop_back();

	if (m_Items.empty())
	{
		// add (Empty) item to the empty submenus
		MenuItem item(MENU_EMPTY);
		item.name=FindTranslation(L"Menu.Empty",L"(Empty)");
		m_Items.push_back(item);
	}

	if (m_bSubMenu)
	{
		m_ScrollCount=(int)m_Items.size();
		m_SearchIndex=-1;
		for (int i=0;i<(int)m_Items.size();i++)
		{
			if (m_Items[i].id==MENU_SEARCH_BOX)
				m_SearchIndex=i;
		}
	}
	else
	{
		m_ProgramButtonIndex=m_ProgramTreeIndex=-1;
		m_SearchIndex=-1;
		for (int i=0;i<(int)m_Items.size();i++)
		{
			if (m_Items[i].id==MENU_PROGRAMS_TREE)
				m_ProgramTreeIndex=i;
			if (m_Items[i].id==MENU_PROGRAMS)
				m_ProgramButtonIndex=i;
			if (m_Items[i].id==MENU_SEARCH_BOX)
				m_SearchIndex=i;
		}
		m_SearchItemCount=1;
		if (s_bWin7Style)
			m_SearchItemCount=m_SearchProvidersCount+2;
	}

	m_OriginalCount=(int)m_Items.size();
	m_OriginalScrollCount=m_ScrollCount;
	UpdateAccelerators(0,m_OriginalCount);
}

int CMenuContainer::AddSearchItems( const std::vector<SearchItem> &items, const CString &categoryName, unsigned int categoryHash, int originalCount )
{
	if (m_Items.size()>MAX_MENU_ITEMS-2)
		return 0;
	bool bFirst=true;
	int count=0;
	for (std::vector<SearchItem>::const_iterator it=items.begin();it!=items.end();++it)
	{
		CComPtr<IShellItem> pItem;
		if (FAILED(SHCreateItemFromIDList(it->info->GetPidl(),IID_IShellItem,(void**)&pItem)))
			continue;

		CComString pName;
		if (SUCCEEDED(pItem->GetDisplayName(categoryHash==CSearchManager::CATEGORY_AUTOCOMPLETE?SIGDN_PARENTRELATIVEEDITING:SIGDN_NORMALDISPLAY,&pName)))
		{
			if (bFirst)
			{
				bFirst=false;
				if (!categoryName.IsEmpty())
				{
					MenuItem item(MENU_SEARCH_CATEGORY);
					item.name.Format(L"%s (%d)",categoryName,originalCount);
					item.nameHash=CalcFNVHash(categoryName);
					item.categoryHash=categoryHash;
					if (categoryHash!=CSearchManager::CATEGORY_PROGRAM || categoryHash!=CSearchManager::CATEGORY_SETTING)
						item.bSplit=(s_Skin.More_bitmap_Size.cx>0);
					m_Items.push_back(item);
				}
			}

			MenuItem item(MENU_NO);
			item.categoryHash=categoryHash;
			item.pItemInfo=it->info;
			g_ItemManager.UpdateItemInfo(it->info,CItemManager::INFO_SMALL_ICON);
			bool bMetroLink, bMetroApp;
			CString metroName;
			{
				CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
				bMetroLink=item.pItemInfo->IsMetroLink();
				bMetroApp=item.pItemInfo->IsMetroApp();
				if (bMetroLink)
					metroName=item.pItemInfo->GetMetroName();
			}
			if (!it->name.IsEmpty())
				item.SetName(it->name,false);
			else if (bMetroLink && !metroName.IsEmpty())
					item.SetName(metroName,false);
			else
				item.SetName(pName,(m_Options&CONTAINER_NOEXTENSIONS)!=0);
			item.bMetroLink=bMetroLink;
			item.bMetroApp=bMetroApp;
			item.pItem1=ILCloneFull(it->info->GetPidl());
			wchar_t name[_MAX_PATH];
			Strcpy(name,_countof(name),item.name);
			CharUpper(name);
			item.nameHash=CalcFNVHash(name,categoryHash);
			m_Items.push_back(item);
			count++;
		}
	}
	return count;
}

// Initialize the m_Items list with the search results
bool CMenuContainer::InitSearchItems( void )
{
	if (m_bSubMenu)
		ClearItems(m_Items.begin(),m_Items.end());
	else
		ClearItems(m_Items.begin()+m_OriginalCount,m_Items.end());
	m_RefreshPosted=0;
	SetSubmenu(-1);
	SetContextItem(-1);
	s_HotPos=GetMessagePos();
	m_ScrollCount=0;
	g_SearchManager.GetSearchResults(s_SearchResults);
	bool bAutoComlpete=!s_SearchResults.autoCompletePath.IsEmpty();
	m_SearchScrollCount=0;
	m_SearchScrollHeight=0;
	m_SearchScrollPos=0;
	unsigned int runCategoryHash=0;
	CString runCommand;
	CComString runExe;
	if (!bAutoComlpete && !s_bNoRun && s_SearchResults.programs.empty() && s_SearchResults.settings.empty() && s_SearchResults.metrosettings.empty())
	{
		if (s_bWin7Style)
			m_SearchBox.GetWindowText(runCommand);
		else
			m_pParent->m_SearchBox.GetWindowText(runCommand);

		// validate execute item
		if (_wcsnicmp(runCommand,L"www.",4)==0 || _wcsnicmp(runCommand,L"http:",5)==0)
			runCategoryHash=CSearchManager::CATEGORY_INTERNET;
		else if (_wcsnicmp(runCommand,L"shell:",6)==0)
		{
			CAbsolutePidl pidl;
			if (SUCCEEDED(SHParseDisplayName(runCommand,NULL,&pidl,0,NULL)))
				runCategoryHash=CSearchManager::CATEGORY_PROGRAM;
		}
		else if (wcsncmp(runCommand,L"\\\\",2)!=0 && SUCCEEDED(SHEvaluateSystemCommandTemplate(runCommand,&runExe,NULL,NULL)))
			runCategoryHash=CSearchManager::CATEGORY_PROGRAM;
		if (runCategoryHash)
			s_SearchResults.programs.push_back(NULL);
	}
	std::vector<SearchItem> items;
	std::vector<int> counts;
	int sepHeight=0, itemHeight=0, maxHeight=0, reservedHeight=0;
	int maxCount=0;
	if (s_bWin7Style)
	{
		sepHeight=s_Skin.ItemSettings[s_Skin.More_bitmap_Size.cx?MenuSkin::LIST_SEPARATOR_SPLIT:MenuSkin::LIST_SEPARATOR].itemHeight;
		itemHeight=s_Skin.ItemSettings[MenuSkin::LIST_ITEM].itemHeight;
		// total height minus the search box and the "more results"/"search internet", if present
		maxHeight=m_Items[m_SearchIndex].itemRect.top-s_Skin.Main_search_padding.top-s_Skin.Search_padding.top;
		maxHeight-=itemHeight*(m_SearchItemCount-1);
		if (!s_SearchResults.bSearching && !HasMoreResults())
			maxHeight+=itemHeight;
	}
	if (bAutoComlpete)
	{
		items.reserve(s_SearchResults.autocomplete.size());
		for (std::vector<const CItemManager::ItemInfo*>::const_iterator it=s_SearchResults.autocomplete.begin();it!=s_SearchResults.autocomplete.end() && (int)items.size()<MAX_MENU_ITEMS;++it)
			items.push_back(SearchItem(*it));
		int count=AddSearchItems(items,L"",CSearchManager::CATEGORY_AUTOCOMPLETE,0);
		if (s_bWin7Style)
		{
			m_SearchScrollHeight=maxHeight/itemHeight;
			m_SearchScrollCount=count;
		}
	}
	if (s_bWin7Style)
	{
		// calculate the allowed counts per category
		int selectedCount=0;
		if (!s_SearchResults.programs.empty())
		{
			counts.push_back((int)s_SearchResults.programs.size());
			if (m_SearchCategoryHash==CSearchManager::CATEGORY_PROGRAM)
				selectedCount=(int)s_SearchResults.programs.size();
		}
		if (!s_SearchResults.metrosettings.empty())
		{
			counts.push_back((int)s_SearchResults.metrosettings.size());
			if (m_SearchCategoryHash==CSearchManager::CATEGORY_METROSETTING)
				selectedCount=(int)s_SearchResults.metrosettings.size();
		}
		if (!s_SearchResults.settings.empty())
		{
			counts.push_back((int)s_SearchResults.settings.size());
			if (m_SearchCategoryHash==CSearchManager::CATEGORY_SETTING)
				selectedCount=(int)s_SearchResults.settings.size();
		}
		for (std::list<CSearchManager::SearchCategory>::const_iterator it=s_SearchResults.indexed.begin();it!=s_SearchResults.indexed.end();++it)
		{
			if (!it->items.empty())
			{
				counts.push_back((int)it->items.size());
				if (m_SearchCategoryHash==it->categoryHash)
					selectedCount=(int)it->items.size();
			}
		}

		int n=(int)counts.size();
		if (n>0)
		{
			int totalCount=(maxHeight-n*sepHeight)/itemHeight;
			maxCount=1;
			if (totalCount>n)
			{
				std::sort(counts.begin(),counts.end());
				for (int i=0;i<n;i++)
				{
					if (counts[i]*(n-i)<=totalCount)
					{
						maxCount=counts[i];
						totalCount-=maxCount;
					}
					else
					{
						maxCount=(totalCount+n-i-1)/(n-i);
						break;
					}
				}
			}
			if (maxCount<3) maxCount=3;

			reservedHeight=selectedCount>0?(sepHeight+selectedCount*itemHeight):0;
			if (reservedHeight>maxHeight) reservedHeight=maxHeight;
			maxHeight-=reservedHeight;
		}
	}

	// add categories
	std::list<CSearchManager::SearchCategory>::const_iterator it=s_SearchResults.indexed.begin();
	for (size_t idx=0;idx<s_SearchResults.indexed.size()+3;idx++)
	{
		items.clear();
		unsigned int categoryHash;
		if (idx==0)
			categoryHash=CSearchManager::CATEGORY_PROGRAM;
		else if (idx==1)
			categoryHash=CSearchManager::CATEGORY_METROSETTING;
		else if (idx==2)
			categoryHash=CSearchManager::CATEGORY_SETTING;
		else
			categoryHash=it->categoryHash;

		int count=0;
		if (s_bWin7Style)
		{
			int h=(categoryHash==m_SearchCategoryHash?reservedHeight:maxHeight);
			count=(h-sepHeight)/itemHeight;
			if (categoryHash!=m_SearchCategoryHash && count>maxCount)
				count=maxCount;
		}
		else
		{
			count=GetSettingInt(categoryHash==m_SearchCategoryHash?L"SearchResultsMax":L"SearchResults");
		}
		if (count<=0)
		{
			if (idx>=3) ++it;
			continue;
		}

		const wchar_t *name;
		int originalCount=0;
		if (idx==0)
		{
			originalCount=(int)s_SearchResults.programs.size();
			if (count>originalCount)
				count=originalCount;
			items.reserve(count);
			for (std::vector<const CItemManager::ItemInfo*>::const_iterator it=s_SearchResults.programs.begin();it!=s_SearchResults.programs.end() && (int)items.size()<count;++it)
				items.push_back(SearchItem(*it));
			name=FindTranslation(L"Search.CategoryPrograms",L"Programs");
		}
		else if (idx==1)
		{
			originalCount=(int)s_SearchResults.metrosettings.size();
			if (count>originalCount)
				count=originalCount;
			items.reserve(count);
			for (std::vector<const CItemManager::ItemInfo*>::const_iterator it=s_SearchResults.metrosettings.begin();it!=s_SearchResults.metrosettings.end() && (int)items.size()<count;++it)
				items.push_back(SearchItem(*it));
			name=FindTranslation(L"Search.CategoryPCSettings", L"Settings");
		}
		else if (idx==2)
		{
			originalCount=(int)s_SearchResults.settings.size();
			if (count>originalCount)
				count=originalCount;
			items.reserve(count);
			for (std::vector<const CItemManager::ItemInfo*>::const_iterator it=s_SearchResults.settings.begin();it!=s_SearchResults.settings.end() && (int)items.size()<count;++it)
				items.push_back(SearchItem(*it));
			name=FindTranslation(L"Search.CategorySettings",L"Control Panel");
		}
		else
		{
			originalCount=(int)it->items.size();
			if (count>originalCount)
				count=originalCount;
			items.reserve(count);
			for (int i=0;i<count;i++)
			{
				PIDLIST_ABSOLUTE pidl=it->items[i].pidl;
				CComPtr<IShellItem> pItem;
				if (SUCCEEDED(SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pItem)))
					items.push_back(SearchItem(it->items[i].name,g_ItemManager.GetItemInfo(pItem,pidl,0)));
			}
			name=it->name;
			++it;
		}
		if (items.empty())
			continue;
		if (idx==0 && runCategoryHash)
		{
			MenuItem item1(MENU_SEARCH_CATEGORY);
			item1.categoryHash=runCategoryHash;
			item1.name=runCategoryHash==CSearchManager::CATEGORY_INTERNET?FindTranslation(L"Search.CategoryInternet",L"Internet"):FindTranslation(L"Search.CategoryPrograms",L"Programs");
			item1.nameHash=CalcFNVHash(item1.name);
			m_Items.push_back(item1);

			MenuItem item2(MENU_SEARCH_EXECUTE);
			item2.name=runCommand;
			if (runCategoryHash==CSearchManager::CATEGORY_INTERNET)
				item2.pItemInfo=GetInternetIcon(true);
			else if (runExe)
			{
				item2.pItemInfo=g_ItemManager.GetItemInfo(CString(runExe),CItemManager::INFO_SMALL_ICON);
				CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
				item2.pItem1=ILCloneFull(item2.pItemInfo->GetPidl());
			}
			else
				item2.pItemInfo=g_ItemManager.GetCustomIcon(L"imageres.dll,100",CItemManager::ICON_SIZE_TYPE_SMALL);
			m_Items.push_back(item2);
			count=1;
		}
		else
		{
			count=AddSearchItems(items,name,categoryHash,originalCount);
		}
		if (s_bWin7Style && categoryHash!=m_SearchCategoryHash && count>0)
			maxHeight-=sepHeight+count*itemHeight;
	}
	if (s_bWin7Style)
	{
		UpdateAccelerators(m_OriginalCount,(int)m_Items.size());
		MenuItem &item=m_Items[m_SearchIndex-m_SearchItemCount+1];
		if (s_SearchResults.bSearching)
		{
			item.id=MENU_SEARCH_EMPTY;
			item.name=FindTranslation(L"Menu.Searching",L"Searching...");
			item.pItemInfo=g_ItemManager.GetCustomIcon(L"imageres.dll,8",CItemManager::ICON_SIZE_TYPE_SMALL);
		}
		else
		{
			item.id=MENU_MORE_RESULTS;
			item.name=FindTranslation(L"Menu.MoreResults",L"See more results");
			item.pItemInfo=g_ItemManager.GetCustomIcon(L"imageres.dll,177",CItemManager::ICON_SIZE_TYPE_SMALL);
		}
	}
	else
	{
		m_ScrollCount=(int)m_Items.size();
		bool bInternet=GetSettingBool(L"SearchInternet");
		if (s_SearchResults.bSearching)
		{
			MenuItem item(MENU_SEARCH_EMPTY);
			item.name=FindTranslation(L"Menu.Searching",L"Searching...");
			item.pItemInfo=g_ItemManager.GetCustomIcon(L"imageres.dll,8",CItemManager::ICON_SIZE_TYPE_SMALL);
			m_Items.push_back(item);
		}
		else
		{
			if (m_Items.empty())
			{
				MenuItem item(MENU_SEARCH_EMPTY);
				item.name=FindTranslation(L"Menu.NoMatch",L"No items match your search.");
				m_Items.push_back(item);
			}
			if (HasMoreResults())
			{
				{
					MenuItem item(MENU_SEPARATOR);
					m_Items.push_back(item);
				}
				MenuItem item(MENU_MORE_RESULTS);
				item.name=FindTranslation(L"Menu.MoreResults",L"See more results");
				item.pItemInfo=g_ItemManager.GetCustomIcon(L"imageres.dll,177",CItemManager::ICON_SIZE_TYPE_SMALL);
				m_Items.push_back(item);
			}
			else if (bInternet)
			{
				MenuItem item(MENU_SEPARATOR);
				m_Items.push_back(item);
			}
			if (bInternet)
				AddInternetSearch(m_Items.size());
		}
		AddStandardItems();
		UpdateAccelerators(m_ScrollCount,(int)m_Items.size());
	}

	if (!s_SearchResults.bSearching && (m_Items.empty() || (m_bTwoColumns && m_Items.size()==m_OriginalCount)))
	{
		MenuItem item(MENU_SEARCH_EMPTY);
		item.name=FindTranslation(L"Menu.NoMatch",L"No items match your search.");
		m_Items.push_back(item);
	}
	return s_SearchResults.bSearching;
}

HBITMAP CMenuContainer::GetArrowsBitmap( unsigned int color )
{
	if (!s_ArrowsBitmap)
	{
		int id=s_Skin.Dpi>=144?IDB_ARROWS150:IDB_ARROWS;
		s_ArrowsBitmap=(HBITMAP)LoadImage(g_Instance,MAKEINTRESOURCE(id),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
		s_LastArrowColor=0xFFFFFF;
	}
	if (s_LastArrowColor!=color)
	{
		s_LastArrowColor=color;
		color=0xFF000000|(color<<16)|(color&0xFF00)|((color>>16)&0xFF);
		BITMAP info;
		GetObject(s_ArrowsBitmap,sizeof(info),&info);
		int n=info.bmWidth*info.bmHeight;
		for (int p=0;p<n;p++)
		{
			unsigned int &pixel=((unsigned int*)info.bmBits)[p];
			pixel=((pixel>>24)*0x01010101)&color;
		}
	}
	return s_ArrowsBitmap;
}

// Calculate the size and create the background bitmaps
void CMenuContainer::InitWindow( bool bDontShrink )
{
	POINT corner;
	RECT rc;
	GetWindowRect(&rc);
	corner.x=(m_Options&CONTAINER_LEFT)?rc.left:rc.right;
	corner.y=(m_Options&CONTAINER_TOP)?rc.top:rc.bottom;
	RECT menuRect;
	InitWindowInternal(bDontShrink,corner,menuRect);
	InitWindowFinalize(menuRect);
}

void CMenuContainer::InitWindowInternal( bool bDontShrink, const POINT &corner, RECT &menuRect )
{
	bool bRecentReverse=GetSettingInt(L"MaxRecentPrograms")<0;
	bool bRecentByName=GetSettingBool(L"RecentSortName");

	int firstRecent=-1, lastRecent=-1;
	if ((bRecentReverse || bRecentByName) && (!m_bSubMenu || (m_Options&CONTAINER_RECENT)))
	{
		for (int i=0;i<(int)m_Items.size();i++)
		{
			if (m_Items[i].id==MENU_RECENT)
			{
				if (firstRecent==-1) firstRecent=i;
				lastRecent=i+1;
			}
			else if (firstRecent!=-1)
				break;
		}
		if (firstRecent>=0)
			std::sort(m_Items.begin()+firstRecent,m_Items.begin()+lastRecent,MenuItem::MruOrderComparator());
	}

	m_bTwoColumns=(!m_bSubMenu && s_Skin.TwoColumns);
	if (!m_pParent && !s_Theme && IsAppThemed())
	{
		s_Theme=OpenThemeData(m_hWnd,L"toolbar");
		s_PagerTheme=OpenThemeData(m_hWnd,L"scrollbar");
	}
	if (!m_pParent && !s_Tooltip.m_hWnd)
	{
		s_Tooltip=CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT|(s_bRTL?WS_EX_LAYOUTRTL:0),TOOLTIPS_CLASS,NULL,WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP,0,0,0,0,NULL,NULL,g_Instance,NULL);
		s_Tooltip.SendMessage(TTM_SETMAXTIPWIDTH,0,500);
		TOOLINFO tool={sizeof(tool),TTF_ABSOLUTE|TTF_TRACK|TTF_TRANSPARENT|(s_bRTL?TTF_RTLREADING:0U)};
		tool.uId=1;
		s_Tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);
	}
	RECT menuPadding[2];
	if (m_bSubMenu)
	{
		menuPadding[0]=s_Skin.Submenu_padding;
		memset(&menuPadding[1],0,sizeof(menuPadding[1]));
	}
	else if (s_MenuMode==MODE_SEARCH)
	{
		menuPadding[0]=s_Skin.Main_search_padding;
		memset(&menuPadding[1],0,sizeof(menuPadding[1]));
	}
	else if (s_MenuMode==MODE_JUMPLIST)
	{
		menuPadding[0]=s_Skin.Main_padding;
		menuPadding[1]=s_Skin.Main_jump_padding;
	}
	else
	{
		menuPadding[0]=s_Skin.Main_padding;
		menuPadding[1]=s_Skin.Main2_padding;
	}

	// calculate maximum height
	int maxHeight[2]; // excluding padding
	int maxWidth=m_MaxWidth;
	int borderHeight=0;
	{
		const RECT &mainRect=m_bSubMenu?s_MenuLimits:s_MainMenuLimits;
		maxHeight[0]=maxHeight[1]=(mainRect.bottom-mainRect.top);
		// adjust for padding
		RECT rc={0,0,0,0};
		AdjustWindowRect(&rc,GetWindowLong(GWL_STYLE),FALSE);
		maxWidth-=rc.right-rc.left;
		maxWidth-=menuPadding[0].left+menuPadding[0].right;
		if (m_bSubMenu)
		{
			borderHeight=rc.bottom;
			maxHeight[0]-=menuPadding[0].top+menuPadding[0].bottom;
		}
		else
		{
			if (m_Options&CONTAINER_TOP)
				maxHeight[0]=maxHeight[1]=mainRect.bottom-corner.y;
			else
				maxHeight[0]=maxHeight[1]=corner.y-mainRect.top;
			maxHeight[0]-=rc.bottom-rc.top;
			maxHeight[1]-=rc.bottom-rc.top;
			maxHeight[0]-=menuPadding[0].top+menuPadding[0].bottom;
			maxHeight[1]-=menuPadding[1].top+menuPadding[1].bottom;
			if (s_UserPicture.m_hWnd && !(m_Options&CONTAINER_TOP) && s_Skin.User_bitmap_outside)
			{
				maxHeight[0]-=s_Skin.User_bitmapSize.cy-s_Skin.User_frame_position.x;
				maxHeight[1]-=s_Skin.User_bitmapSize.cy-s_Skin.User_frame_position.x;
			}
		}
	}
#ifdef _DEBUG
//	maxHeight[0]/=3; maxHeight[1]/=3; // uncomment to test for smaller screen
#endif

	HDC hdc=CreateCompatibleDC(NULL);
	int arrowSize[2];
	if (m_bSubMenu)
	{
		const MenuSkin::ItemDrawSettings &settings=s_Skin.ItemSettings[MenuSkin::SUBMENU_ITEM];
		arrowSize[0]=settings.arrPadding.cx+settings.arrPadding.cy;
		if (m_Options&CONTAINER_JUMPLIST)
			arrowSize[0]+=s_Skin.Pin_bitmap_Size.cx;
		else
			arrowSize[0]+=settings.arrSize.cx;
	}
	else
	{
		const MenuSkin::ItemDrawSettings &settings=s_Skin.ItemSettings[MenuSkin::COLUMN1_ITEM];
		arrowSize[0]=settings.arrPadding.cx+settings.arrPadding.cy+settings.arrSize.cx;
	}
	{
		const MenuSkin::ItemDrawSettings &settings=s_Skin.ItemSettings[MenuSkin::COLUMN2_ITEM];
		arrowSize[1]=settings.arrPadding.cx+settings.arrPadding.cy+settings.arrSize.cx;
	}
	int expandoSize=s_Skin.GetArrowsBitmapSizes()[6].y;

	HGDIOBJ font0=GetCurrentObject(hdc,OBJ_FONT);
	int fixedWidth[2]={-1,-1};
	int maxItemWidth[2]={65536,65536};
	if (m_bSubMenu)
	{
		int numChar=GetSettingInt(L"MaxMenuWidth");
		maxItemWidth[0]=numChar?s_Skin.ItemSettings[MenuSkin::SUBMENU_ITEM].textMetrics.tmAveCharWidth*numChar:65536;
	}
	else
	{
		int numChar=GetSettingInt(L"MaxMainMenuWidth");
		int width=s_Skin.ItemSettings[MenuSkin::COLUMN1_ITEM].textMetrics.tmAveCharWidth;
		maxItemWidth[0]=numChar?width*numChar:65536;
		maxItemWidth[1]=numChar?s_Skin.ItemSettings[MenuSkin::COLUMN2_ITEM].textMetrics.tmAveCharWidth*numChar:65536;
		if (s_bWin7Style)
		{
			if (s_MenuMode==MODE_SEARCH)
			{
				fixedWidth[0]=s_MenuWidthNormal-s_Skin.Main_search_padding.left-s_Skin.Main_search_padding.right;
				fixedWidth[1]=0;
				maxItemWidth[0]=65536;
			}
			else if (s_MenuMode==MODE_JUMPLIST)
			{
				fixedWidth[0]=width*s_ProgramsWidth;
				fixedWidth[1]=width*s_JumplistWidth;
				maxItemWidth[0]=65536;
				maxItemWidth[1]=65536;
			}
			else
			{
				fixedWidth[0]=width*s_ProgramsWidth;
				maxItemWidth[0]=65536;
				maxItemWidth[1]=65536;
			}
		}
		s_MenuWidthJump=width*(s_ProgramsWidth+s_JumplistWidth)+s_Skin.Main_padding.left+s_Skin.Main_padding.right+s_Skin.Main_jump_padding.left+s_Skin.Main_jump_padding.right;
	}
	m_ScrollButtonSize=s_Skin.ItemSettings[m_bSubMenu?MenuSkin::SUBMENU_ITEM:MenuSkin::COLUMN1_ITEM].itemHeight;
	if (!s_bHasTouch)
		m_ScrollButtonSize/=2;
	if (m_ScrollButtonSize<MIN_SCROLL_HEIGHT) m_ScrollButtonSize=MIN_SCROLL_HEIGHT;

	// calculate item sizes
	std::vector<int> columnWidths;
	columnWidths.push_back(0);

	bool bMultiColumn=s_ScrollMenus!=0 && (m_Options&CONTAINER_MULTICOLUMN);
	int shutdownIndex=-1;

	{
		int row=0, column=0, subColumn=0;
		int y=0;
		int maxw=0;
		int index=0;
		for (int i=0;i<(int)m_Items.size();i++)
		{
			MenuItem &item=m_Items[i];

			if (m_bTwoColumns && column==0 && i>0 && item.bBreak)
			{
				// start a new column
				column++;
				columnWidths.push_back(0);
				row=0;
				y=0;
				if (s_UserPicture.m_hWnd)
				{
					if (!s_Skin.User_bitmap_outside || (m_Options&CONTAINER_TOP))
						y=s_Skin.User_bitmapSize.cy+s_Skin.User_image_padding.x+s_Skin.User_image_padding.y;
					else
						y=s_Skin.User_frame_position.x+s_Skin.User_image_padding.y-menuPadding[0].top;
				}
				index=1;
			}
			item.bNew=false;
			if (m_bTwoColumns && i<m_OriginalCount && s_MenuMode!=MODE_NORMAL && (i<=m_SearchIndex-m_SearchItemCount || i>m_SearchIndex) && item.id!=MENU_SHUTDOWN_BUTTON)
			{
				// hide original items based on mode
				if (s_MenuMode==MODE_JUMPLIST)
				{
					if (column==1)
					{
						item.itemRect.top=item.itemRect.bottom=0;
						continue;
					}
				}
				if (s_MenuMode==MODE_SEARCH)
				{
					item.itemRect.top=item.itemRect.bottom=0;
					continue;
				}
			}
			if (i==m_OriginalCount)
			{
				y=0;
				row=0;
				column=index=(s_MenuMode==MODE_JUMPLIST)?1:0;
				if (column==1 && columnWidths.size()==1)
					columnWidths.push_back(0);
			}
			if (item.id!=MENU_RECENT && !(m_Options&CONTAINER_SEARCH) && s_Skin.bHasNewItem && i<m_OriginalCount)
			{
				if (item.id==MENU_PROGRAMS)
				{
					if (s_bWin7Style && GetWinVersion()>=WIN_VER_WIN8 && GetSettingBool(L"AllProgramsMetro"))
						item.bNew=g_ItemManager.HasNewPrograms(false) || g_ItemManager.HasNewApps(false);
					else
						item.bNew=g_ItemManager.HasNewPrograms(false);
				}
				else if (item.id==MENU_APPS)
					item.bNew=g_ItemManager.HasNewApps(false);
				else if (item.bFolder)
					item.bNew=(item.pItem1 && g_ItemManager.IsNewProgram(item.pItem1,true,false)) || (item.pItem2 && g_ItemManager.IsNewProgram(item.pItem2,true,false));
				else if (item.pItemInfo)
					item.bNew=(item.pItem1 && g_ItemManager.IsNewProgram(item.pItem1,false,item.bMetroApp));
			}
			if (item.id==MENU_SHUTDOWN_BUTTON)
			{
				if (s_MenuMode==MODE_SEARCH)
					item.drawType=MenuSkin::SHUTDOWN_BUTTON_SEARCH;
				else if (s_MenuMode==MODE_JUMPLIST)
					item.drawType=MenuSkin::SHUTDOWN_BUTTON_JUMP;
				else
					item.drawType=MenuSkin::SHUTDOWN_BUTTON;
			}
			else if (s_bWin7Style && item.id==MENU_SEARCH_CATEGORY)
			{
				item.drawType=(item.bSplit)?MenuSkin::LIST_SEPARATOR_SPLIT:MenuSkin::LIST_SEPARATOR;
			}
			else if (s_bWin7Style && item.id==MENU_PROGRAMS)
			{
				if (GetSettingInt(L"ProgramsStyle")==PROGRAMS_INLINE)
					item.drawType=item.bNew?MenuSkin::PROGRAMS_BUTTON_NEW:MenuSkin::PROGRAMS_BUTTON;
				else
					item.drawType=item.bNew?MenuSkin::PROGRAMS_CASCADING_NEW:MenuSkin::PROGRAMS_CASCADING;
			}
			else if (s_bWin7Style && m_bTwoColumns && (s_MenuMode==MODE_SEARCH || s_MenuMode==MODE_JUMPLIST) && i>=m_OriginalCount)
			{
				if (item.id==MENU_SEPARATOR)
					item.drawType=MenuSkin::LIST_SEPARATOR;
				else if (item.jumpIndex<0)
					item.drawType=MenuSkin::LIST_ITEM;
				else
					item.drawType=(s_JumpList.groups[LOWORD(item.jumpIndex)].type==CJumpGroup::TYPE_TASKS)?MenuSkin::LIST_ITEM:MenuSkin::LIST_SPLIT;
			}
			else if (s_bWin7Style && (i>m_SearchIndex-m_SearchItemCount && i<m_SearchIndex))
			{
				item.drawType=MenuSkin::LIST_ITEM;
			}
			else if (m_bTwoColumns && index==1)
			{
				if (item.bInline) item.drawType=MenuSkin::COLUMN2_INLINE;
				else if (item.id==MENU_SEPARATOR) item.drawType=MenuSkin::COLUMN2_SEPARATOR;
				else if (item.bSplit) item.drawType=MenuSkin::COLUMN2_SPLIT;
				else if (item.bNew) item.drawType=MenuSkin::COLUMN2_NEW;
				else item.drawType=MenuSkin::COLUMN2_ITEM;
			}
			else if (m_bSubMenu)
			{
				if (item.id==MENU_SEPARATOR) item.drawType=MenuSkin::SUBMENU_SEPARATOR;
				else if (item.id==MENU_SEARCH_CATEGORY) item.drawType=(item.bSplit)?MenuSkin::SUBMENU_SEPARATOR_SPLIT:MenuSkin::SUBMENU_SEPARATOR;
				else if (item.bSplit) item.drawType=MenuSkin::SUBMENU_SPLIT;
				else if (item.bNew) item.drawType=MenuSkin::SUBMENU_NEW;
				else item.drawType=MenuSkin::SUBMENU_ITEM;
			}
			else
			{
				if (item.id==MENU_SEPARATOR) item.drawType=MenuSkin::COLUMN1_SEPARATOR;
				else if (item.bSplit) item.drawType=MenuSkin::COLUMN1_SPLIT;
				else if (item.bNew) item.drawType=MenuSkin::COLUMN1_NEW;
				else item.drawType=MenuSkin::COLUMN1_ITEM;
			}
			const MenuSkin::ItemDrawSettings &settings=s_Skin.ItemSettings[item.drawType];

			SelectObject(hdc,settings.font);
			int w=0, h=0;
			int iconSize=0;
			if (settings.iconSize==MenuSkin::ICON_SIZE_SMALL)
				iconSize=g_ItemManager.SMALL_ICON_SIZE;
			else if (settings.iconSize==MenuSkin::ICON_SIZE_LARGE)
				iconSize=g_ItemManager.LARGE_ICON_SIZE;
			if (item.id==MENU_PROGRAMS_TREE)
				h=0; // hide it for now
			else if (!s_bShowTopEmpty && m_Items.size()>1 && (m_Items[i].id==MENU_EMPTY_TOP || (i>0 && m_Items[i-1].id==MENU_EMPTY_TOP)))
				h=0; // this is the first (Empty) item in the top menu. hide it for now
			else if (item.bInline)
			{
				h=iconSize+settings.iconPadding.top+settings.iconPadding.bottom;
				w=iconSize+settings.iconPadding.left+settings.iconPadding.right;
			}
			else if ((item.id==MENU_SEPARATOR || item.id==MENU_SEARCH_CATEGORY) && !item.bBlankSeparator)
			{
				if (!item.name.IsEmpty())
				{
					h=settings.itemHeight;
					RECT rcText={0,0,0,0};
					DrawText(hdc,item.name,-1,&rcText,DT_CALCRECT|DT_SINGLELINE|DT_HIDEPREFIX);
					w=rcText.right;
					if (w>maxItemWidth[index]) w=maxItemWidth[index];
					int textEnd=w;
					w+=settings.textPadding.left+settings.textPadding.right+arrowSize[index];
					int textStart=settings.textPadding.left;
					if (item.id==MENU_SEARCH_CATEGORY)
					{
						w+=settings.iconPadding.left+settings.iconPadding.right+expandoSize;
						textStart+=settings.iconPadding.left+settings.iconPadding.right+expandoSize;
						if (item.bSplit)
							w+=settings.arrPadding.cx+settings.arrPadding.cy+s_Skin.More_bitmap_Size.cx+1;
					}
					textEnd+=textStart;
					item.textStart=(short)textStart;
					item.textEnd=(short)textEnd;
				}
				else if (y>0 || (i>0 && i==m_ProgramTreeIndex+1))
					h=settings.sepHeight;
				else
					h=0; // ignore separators at the top of the column
			}
			else if (item.id==MENU_SEARCH_BOX)
			{
				h=settings.textMetrics.tmHeight*12/8+s_Skin.Search_padding.top+s_Skin.Search_padding.bottom; // 12 DLUs
				w=settings.textMetrics.tmAveCharWidth*25;
			}
			else if (s_bWin7Style && i>m_SearchIndex-m_SearchItemCount && i<m_SearchIndex && (s_MenuMode!=MODE_SEARCH || (item.id==MENU_MORE_RESULTS && !HasMoreResults())))
			{
				h=0;
			}
			else if (item.id==MENU_SHUTDOWN_BUTTON)
			{
				shutdownIndex=i;
				RECT rcText={0,0,0,0};
				DrawText(hdc,item.name,-1,&rcText,DT_CALCRECT|DT_HIDEPREFIX|DT_SINGLELINE);
				w=rcText.right;
				if (s_bHasUpdates)
					w+=settings.iconPadding.left+settings.iconPadding.right+s_Skin.Shutdown_bitmap_Size.cx;
				if (w<settings.textMetrics.tmAveCharWidth*6)
					w=settings.textMetrics.tmAveCharWidth*6;
				w+=s_Skin.Shutdown_padding.left+s_Skin.Shutdown_padding.right+settings.textPadding.left+settings.textPadding.right+
						settings.arrPadding.cx+settings.arrPadding.cy+settings.arrSize.cx;
				h=s_Skin.Shutdown_padding.top+s_Skin.Shutdown_padding.bottom+settings.itemHeight;
			}
			else if (s_bWin7Style && item.id==MENU_PROGRAMS)
			{
				if (GetSettingInt(L"ProgramsStyle")!=PROGRAMS_HIDDEN)
					h=settings.itemHeight;
				else
					h=0;
				w=fixedWidth[0];
			}
			else
			{
				h=settings.itemHeight;
				RECT rcText={0,0,0,0};
				DrawText(hdc,item.name,-1,&rcText,DT_CALCRECT|DT_SINGLELINE|(item.id==MENU_NO?DT_NOPREFIX:DT_HIDEPREFIX));
				w=rcText.right;
				if (w>maxItemWidth[index]) w=maxItemWidth[index];
				w+=settings.iconPadding.left+settings.iconPadding.right+settings.textPadding.left+settings.textPadding.right+arrowSize[index]+iconSize;
			}
			if (bMultiColumn && y>0 && y+h>maxHeight[0])
			{
				if (item.id==MENU_SEPARATOR && !item.bBlankSeparator && !item.bInline)
					h=0; // ignore separators at the bottom of the column
				else
				{
					// start a new column
					column++;
					columnWidths.push_back(0);
					row=0;
					y=0;
				}
			}
			else if (item.id==MENU_SEPARATOR && !item.bBlankSeparator && !item.bInline && m_bTwoColumns && column==0 && i+1<(int)m_Items.size() && m_Items[i+1].bBreak)
				h=0;
			item.row=row;
			item.column=column;
			item.itemRect.top=y;
			item.itemRect.bottom=y+h;
			item.itemRect.left=0;
			if (item.bInline)
			{
				if (item.bInlineFirst)
				{
					subColumn=0;
				}
				else
				{
					item.row=row=row-1;
					item.itemRect.top-=h;
					item.itemRect.bottom-=h;
					h=0;
				}
				item.itemRect.left=w*subColumn;
				if (item.id==MENU_SEPARATOR)
				{
					w=0;
					subColumn--;
				}
				subColumn++;
			}
			item.itemRect.right=item.itemRect.left+w;
			if (columnWidths[column]<item.itemRect.right)
				columnWidths[column]=item.itemRect.right;
			y+=h;
			row++;
		}
	}

	SelectObject(hdc,font0);
	DeleteDC(hdc);

	if (columnWidths.size()==1)
	{
		if (s_bWin7Style && !m_bSubMenu)
			columnWidths.push_back(0);
		else
			m_bTwoColumns=false;
	}
	else if (columnWidths.size()==2 && s_bWin7Style && !m_bSubMenu && columnWidths[1]<CItemManager::EXTRA_LARGE_ICON_SIZE)
		columnWidths[1]=CItemManager::EXTRA_LARGE_ICON_SIZE;

	// calculate width of each column
	if (!m_bTwoColumns && GetSettingBool(L"SameSizeColumns"))
	{
		int maxw=0;
		for (size_t i=0;i<columnWidths.size();i++)
			if (maxw<columnWidths[i])
				maxw=columnWidths[i];
		for (size_t i=0;i<columnWidths.size();i++)
			columnWidths[i]=maxw;
	}
	if (bDontShrink && columnWidths[0]<m_rContent.right-m_rContent.left)
		columnWidths[0]=m_rContent.right-m_rContent.left;
	if (s_bWin7Style)
	{
		if (fixedWidth[0]>=0)
			columnWidths[0]=fixedWidth[0];
		if (fixedWidth[1]>=0)
			columnWidths[1]=fixedWidth[1];
	}

	if (s_ScrollMenus==2 && columnWidths.size()>1 && m_bSubMenu)
	{
		// auto - determine if we should have 1 column or many
		int width=0;
		for (size_t i=0;i<columnWidths.size();i++)
		{
			if (i>0) width+=s_Skin.Submenu_separatorWidth;
			width+=columnWidths[i];
		}
		if (width>maxWidth)
		{
			bMultiColumn=false;
			// the columns don't fit on screen, switch to one scrollable column
			int y=0;
			columnWidths.resize(1);
			columnWidths[0]=0;
			for (size_t i=0;i<m_Items.size();i++)
			{
				MenuItem &item=m_Items[i];
				const MenuSkin::ItemDrawSettings &settings=s_Skin.ItemSettings[item.drawType];
				int h=settings.itemHeight;
				if (item.id==MENU_SEPARATOR && !item.bBlankSeparator && item.name.IsEmpty())
				{
					h=(y==0)?0:settings.sepHeight;
				}
				if (columnWidths[0]<item.itemRect.right) columnWidths[0]=item.itemRect.right;
				item.row=(int)i;
				item.column=0;
				item.itemRect.top=y;
				item.itemRect.bottom=y+h;
				y+=h;
			}
		}
	}

	// calculate the horizontal position of each item
	int maxw=0;
	int maxh[2]={0,0};
	int scrollWidth=GetSystemMetrics(SM_CXVSCROLL);
	{
		m_ColumnOffsets.resize(columnWidths.size());
		for (size_t i=0;i<columnWidths.size();i++)
		{
			if (i>0) maxw+=m_bSubMenu?s_Skin.Submenu_separatorWidth:s_Skin.Main_separatorWidth;
			m_ColumnOffsets[i]=maxw;
			maxw+=columnWidths[i];
		}
		columnWidths.push_back(maxw);
		bool bInline=false;
		for (int i=0;i<(int)m_Items.size();i++)
		{
			MenuItem &item=m_Items[i];
			if (item.bInline)
			{
				item.itemRect.left+=m_ColumnOffsets[item.column];
				item.itemRect.right+=m_ColumnOffsets[item.column];
				bInline=true;
			}
			else if (item.id==MENU_SHUTDOWN_BUTTON)
			{
				int x=s_Skin.ItemSettings[MenuSkin::COLUMN1_ITEM].textMetrics.tmAveCharWidth*s_ProgramsWidth;
				x+=s_Skin.Main_padding.left+s_Skin.Main_padding.right+s_Skin.Main2_padding.left;
				item.itemRect.right+=x-item.itemRect.left;
				item.itemRect.left=x;
			}
			else if (s_bWin7Style && item.id==MENU_SEARCH_BOX)
			{
				item.itemRect.left=0;
				item.itemRect.right=s_Skin.ItemSettings[MenuSkin::COLUMN1_ITEM].textMetrics.tmAveCharWidth*s_ProgramsWidth;
			}
			else
			{
				item.itemRect.left=m_ColumnOffsets[item.column];
				item.itemRect.right=item.itemRect.left+columnWidths[item.column];
				if (s_MenuMode==MODE_SEARCH && !m_bSubMenu && (item.id==MENU_NO || item.id==MENU_SEARCH_EXECUTE) && i>=m_OriginalCount)
				{
					if ((item.categoryHash&CSearchManager::CATEGORY_MASK)!=CSearchManager::CATEGORY_AUTOCOMPLETE)
						item.itemRect.left+=s_Skin.Main_search_indent;
					else if (s_bWin7Style)
						item.itemRect.right-=scrollWidth;
				}
			}
			if (m_bTwoColumns)
			{
				if (maxh[item.column]<item.itemRect.bottom)
					maxh[item.column]=item.itemRect.bottom;
			}
			else
			{
				if (maxh[0]<item.itemRect.bottom)
					maxh[0]=item.itemRect.bottom;
			}
		}

		if (bInline)
		{
			// center inline groups
			for (size_t i=0;i<m_Items.size();i++)
			{
				MenuItem &item=m_Items[i];
				if (item.bInlineFirst)
				{
					int i1=(int)i;
					bool bSepLeft=(item.id==MENU_SEPARATOR);
					int w=item.itemRect.right-item.itemRect.left;
					for (i++;i<m_Items.size();i++)
					{
						if (!m_Items[i].bInline || m_Items[i].bInlineFirst)
							break;
						w+=(m_Items[i].itemRect.right-m_Items[i].itemRect.left);
					}
					w=(columnWidths[item.column]-w);
					int i2=(int)i;
					i--;
					bool bSepRight=(m_Items[i].id==MENU_SEPARATOR);
					if (!bSepLeft && !bSepRight)
						w/=2; // centered
					else if (bSepRight)
						w=0;
					int first=-1, last=-1;
					for (int j=i1;j<i2;j++)
					{
						if (m_Items[j].id!=MENU_SEPARATOR)
						{
							if (first<0) first=j;
							last=j;
						}
						OffsetRect(&m_Items[j].itemRect,w,0);
						m_Items[j].bInlineFirst=false;
					}
					if (first>=0)
					{
						m_Items[first].bInlineFirst=true;
						m_Items[last].bInlineLast=true;
					}
				}
			}
		}
	}

	if (m_Bitmap)
	{
		DeleteObject(m_Bitmap);
		m_Bitmap=NULL;
	}
	if (m_Region)
	{
		DeleteObject(m_Region);
		m_Region=NULL;
	}

	m_ExtraTop=m_ExtraBottom=m_ExtraBorder=0;
	if (!bMultiColumn && maxh[0]>maxHeight[0] && m_ScrollCount>0 && m_bSubMenu)
	{
		int d=menuPadding[0].top+menuPadding[0].bottom+maxh[0]-maxHeight[1];
		if (d<=menuPadding[0].top)
			m_ExtraTop=d;
		else
		{
			m_ExtraTop=menuPadding[0].top;
			d-=menuPadding[0].top;
			if (d<=menuPadding[0].bottom)
				m_ExtraBottom=d;
			else
				m_ExtraBottom=menuPadding[0].bottom;
		}
		m_ExtraBorder=borderHeight;
		maxHeight[0]=maxHeight[1];
	}

	int totalWidth, totalHeight;
	memset(&m_rContent2,0,sizeof(m_rContent2));
	bool bSetMenuSize=(s_MenuHeight==-1);
	if (!m_bSubMenu)
	{
		if (s_MenuMaxHeight[0]==-1)
		{
			s_MenuMaxHeight[0]=maxh[0];
			s_MenuMaxHeight[1]=maxh[1];
		}
		else if (bSetMenuSize)
		{
			maxh[0]=s_MenuMaxHeight[0];
			maxh[1]=s_MenuMaxHeight[1];
		}
	}
	{
		int w1=maxw, w2=0;
		int h1=(maxh[0]<maxHeight[0]?maxh[0]:maxHeight[0]);
		int h2=(maxh[1]<maxHeight[1]?maxh[1]:maxHeight[1]);
		if (m_bTwoColumns && columnWidths.size()>2)
		{
			w1=columnWidths[0];
			w2=columnWidths[1];

			if (s_bWin7Style)
			{
				if (bSetMenuSize)
				{
					int minh=s_Skin.ItemSettings[MenuSkin::LIST_ITEM].itemHeight*GetSettingInt(L"MinMainHeight");
					if (h1<minh)
						h1=minh;
					s_MenuHeight=h1+menuPadding[0].top+menuPadding[0].bottom;
					int q=h2+menuPadding[1].top+menuPadding[1].bottom;
					if (s_MenuHeight<q)
						s_MenuHeight=q;
					q=maxHeight[0]+menuPadding[0].top+menuPadding[0].bottom;
					if (s_MenuHeight>q)
						s_MenuHeight=q;
				}
				h1=s_MenuHeight-menuPadding[0].top-menuPadding[0].bottom;
				h2=s_MenuHeight-menuPadding[1].top-menuPadding[1].bottom;
			}
		}
		if (!m_bSubMenu)
		{
			if (s_bWin7Style)
			{
				s_BackgroundW1=w1;
				s_BackgroundW2=w2;
				s_BackgroundH1=h1;
				s_BackgroundH2=h2;
				if (s_OldMenuState.mode==MODE_UNKNOWN)
					CreateBackground(w1,w2,h1,h2,totalWidth,totalHeight,true);
				else
					CreateContentRects(w1,w2,h1,h2,totalWidth,totalHeight);
			}
			else if (s_Skin.Main_bitmap.GetBitmap() || s_Skin.User_image_size || m_bTwoColumns || s_Skin.User_name_position.left!=s_Skin.User_name_position.right)
			{
				CreateBackground(w1,w2,h1,h2,totalWidth,totalHeight,true);
			}
			else
			{
				m_rContent.left=menuPadding[0].left;
				m_rContent.top=menuPadding[0].top;
				m_rContent.right=menuPadding[0].left+w1;
				m_rContent.bottom=menuPadding[0].top+h1;
				totalWidth=menuPadding[0].left+menuPadding[0].right+w1;
				totalHeight=menuPadding[0].top+menuPadding[0].bottom+h1;
			}
			if (bSetMenuSize)
			{
				s_MenuWidthMax=s_MenuWidthJump;
				if (s_MenuWidthMax<totalWidth)
					s_MenuWidthMax=totalWidth;
				if (s_MenuMode==MODE_NORMAL)
					s_MenuWidthNormal=totalWidth;
			}
		}
		else
		{
			if (bDontShrink && h1<m_rContent.bottom-m_rContent.top)
				h1=m_rContent.bottom-m_rContent.top;

			if (s_Skin.Submenu_bitmap.GetBitmap())
				CreateSubmenuRegion(w1,h1);

			m_rContent.left=menuPadding[0].left;
			m_rContent.top=menuPadding[0].top-m_ExtraTop;
			m_rContent.right=m_rContent.left+w1;
			m_rContent.bottom=m_rContent.top+h1;
			totalWidth=menuPadding[0].left+menuPadding[0].right+w1;
			totalHeight=menuPadding[0].top+menuPadding[0].bottom+h1-m_ExtraTop-m_ExtraBottom;
		}
		// offset the items
		for (size_t i=0;i<m_Items.size();i++)
		{
			MenuItem &item=m_Items[i];
			int dx=m_rContent.left;
			int dy=m_rContent.top;
			if (m_bTwoColumns && item.column==1)
			{
				dx=m_rContent2.left-m_ColumnOffsets[1];
				dy=m_rContent2.top;
			}
			if (item.id==MENU_SHUTDOWN_BUTTON)
				dx=0;
			OffsetRect(&item.itemRect,dx,dy);
		}

		if (m_bTwoColumns && columnWidths.size()>2)
		{
			int dh1=0, dh2=0;
			for (int i=0;i<m_OriginalCount;i++)
			{
				const MenuItem &item=m_Items[i];
				if (item.column==0)
				{
					if (dh1<item.itemRect.bottom)
						dh1=item.itemRect.bottom;
				}
				else
				{
					if (dh2<item.itemRect.bottom)
						dh2=item.itemRect.bottom;
				}
			}
			m_rContent.bottom=totalHeight-menuPadding[0].bottom;
			m_rContent2.bottom=totalHeight-menuPadding[1].bottom;
			dh1=m_rContent.bottom-dh1;
			dh2=(s_MenuMode==MODE_SEARCH?m_rContent.bottom:m_rContent2.bottom)-dh2;
			if (!s_bWin7Style)
			{
				if (dh1<0) dh1=0;
				if (dh2<0) dh2=0;
			}

			bool bAlign1=false, bAlign2=false;
			for (int i=0;i<m_OriginalCount;i++)
			{
				MenuItem &item=m_Items[i];
				if (item.column==0)
				{
					if (item.bAlignBottom)
						bAlign1=true;
					if (bAlign1)
						OffsetRect(&item.itemRect,0,dh1);
				}
				else
				{
					if (item.bAlignBottom)
						bAlign2=true;
					if (bAlign2)
						OffsetRect(&item.itemRect,0,dh2);
				}
				if (item.id==MENU_SHUTDOWN_BUTTON)
				{
					item.itemRect.left+=s_Skin.Shutdown_padding.left;
					item.itemRect.right-=s_Skin.Shutdown_padding.right;
					item.itemRect.top+=s_Skin.Shutdown_padding.top;
					item.itemRect.bottom-=s_Skin.Shutdown_padding.bottom;
				}
			}
			// trim overlapping items
			if (s_MenuMode==MODE_NORMAL || s_MenuMode==MODE_JUMPLIST || s_MenuMode==MODE_PROGRAMS)
			{
				int extra=-dh1;
				if (extra>0)
				{
					// remove extra programs
					std::vector<MenuItem>::reverse_iterator pBegin=m_Items.rend(), pEnd=m_Items.rend(); // pinned
					std::vector<MenuItem>::reverse_iterator rBegin=m_Items.rend(), rEnd=m_Items.rend(); // recent
					for (std::vector<MenuItem>::reverse_iterator it=m_Items.rbegin();it!=m_Items.rend();++it)
					{
						if (it->id==MENU_NO && it->column==0)
						{
							if (pBegin==m_Items.rend())
								pBegin=it;
						}
						else
						{
							if (pEnd==m_Items.rend() && pBegin!=m_Items.rend())
								pEnd=it;
						}

						if (it->id==MENU_RECENT && it->column==0)
						{
							if (rBegin==m_Items.rend())
								rBegin=it;
						}
						else
						{
							if (rEnd==m_Items.rend() && rBegin!=m_Items.rend())
								rEnd=it;
						}
					}

					// remove recent
					int dy=0;
					for (std::vector<MenuItem>::reverse_iterator it=rBegin;it!=rEnd;++it)
					{
						int h=it->itemRect.bottom-it->itemRect.top;
						it->itemRect.bottom=it->itemRect.top;
						extra-=h;
						dy+=h;
						if (it+1==rEnd && pBegin!=pEnd)
						{
							// removing the last recent, remove the separator between recent and pinned
							if (GetSettingBool(L"RecentProgsTop"))
								it=pEnd;
							else
								it=rEnd;
							Assert(it->id==MENU_SEPARATOR);
							h=it->itemRect.bottom-it->itemRect.top;
							it->itemRect.bottom=it->itemRect.top;
							extra-=h;
							dy+=h;
							break;
						}
						if (extra<=0) break;
					}

					if (dy>0 && GetSettingBool(L"RecentProgsTop") && pBegin!=pEnd)
					{
						// move pinned and the separator
						for (std::vector<MenuItem>::reverse_iterator it=pBegin;it!=rBegin;++it)
						{
							it->itemRect.top-=dy;
							it->itemRect.bottom-=dy;
						}
					}

					// remove pinned
					for (std::vector<MenuItem>::reverse_iterator it=pBegin;it!=pEnd && extra>0;++it)
					{
						int h=it->itemRect.bottom-it->itemRect.top;
						it->itemRect.bottom=it->itemRect.top;
						extra-=h;
						if (extra<=0) break;
					}
				}
			}
			if (s_MenuMode==MODE_NORMAL || s_MenuMode==MODE_PROGRAMS)
			{
				int extra=-dh2;
				if (extra>0)
				{
					// remove extra items
					for (std::vector<MenuItem>::reverse_iterator it=m_Items.rbegin();it!=m_Items.rend();++it)
					{
						if (it->id==MENU_SHUTDOWN_BUTTON) continue;
						if (it->column==0) break;
						int h=it->itemRect.bottom-it->itemRect.top;
						it->itemRect.bottom=it->itemRect.top;
						extra-=h;
						if (extra<=0) break;
					}
				}
			}
		}

		if (firstRecent>=0)
		{
			// reorder recent items
			int top=m_Items[firstRecent].itemRect.top;
			int firstRow=m_Items[firstRecent].row;
			if (bRecentByName)
				std::sort(m_Items.begin()+firstRecent,m_Items.begin()+lastRecent,MenuItem::MruNameComparator());

			TRecentKeys recentKeys=(TRecentKeys)GetSettingInt(L"RecentProgKeys");
			if (recentKeys>=RECENT_KEYS_DIGITS)
			{
				// reassign accelerators
				int num=0;
				for (int idx=firstRecent;idx<lastRecent;idx++)
				{
					MenuItem &item=m_Items[idx];
					item.accelerator=(num<10)?((num+1)%10)+'0':0;
					item.bCustomAccelerator=true;
					if (recentKeys==RECENT_KEYS_DIGITS)
					{
						CString str;
						if (item.nameOffset>0)
							str=(const wchar_t*)item.name+item.nameOffset;
						else
							str=item.name;
						if (num<10)
							item.name.Format(L"&%d %s",(num+1)%10,str);
						else
							item.name=str;
						item.nameOffset=item.name.GetLength()-str.GetLength();
					}
					if (item.itemRect.bottom>item.itemRect.top)
						num++;
				}
			}

			if (bRecentReverse)
				std::reverse(m_Items.begin()+firstRecent,m_Items.begin()+lastRecent);
			for (int i=firstRecent;i<lastRecent;i++)
			{
				RECT &rc=m_Items[i].itemRect;
				int h=rc.bottom-rc.top;
				rc.top=top;
				top+=h;
				rc.bottom=top;
				m_Items[i].row=firstRow-firstRecent+i;
			}
		}
	}

	// create pager
	if (!bMultiColumn && maxh[0]>maxHeight[0] && m_ScrollCount>0 && (m_bSubMenu || !s_bWin7Style))
	{
		int d=maxh[0]-maxHeight[0];
		m_ScrollHeight=m_Items[m_ScrollCount-1].itemRect.bottom-d-m_rContent.top;
		if (m_ScrollHeight<=0)
		{
			if (m_ScrollCount<(int)m_Items.size())
				d=m_Items[m_ScrollCount].itemRect.bottom-m_rContent.top;
			m_ScrollOffset=m_ScrollHeight=0;
			for (int i=0;i<m_ScrollCount;i++)
				m_Items[i].itemRect.bottom=m_Items[i].itemRect.top;
		}
		for (size_t i=m_ScrollCount;i<m_Items.size();i++)
			if (m_Items[i].column==0)
				OffsetRect(&m_Items[i].itemRect,0,-d);

		std::map<unsigned int,int>::iterator it=s_MenuScrolls.find(m_FolderHash[0]);
		if (it!=s_MenuScrolls.end())
		{
			m_ScrollOffset=it->second; // restore the scroll position if the same menu has been opened before
			if (m_ScrollOffset>d) m_ScrollOffset=d;
		}
		else if ((m_Options&CONTAINER_SEARCH) && !bDontShrink)
		{
			// fit current category
			if (m_SearchCategoryHash!=CSearchManager::CATEGORY_INVALID)
			{
				for (int i=0;i<m_ScrollCount;i++)
					if (m_Items[i].id==MENU_SEARCH_CATEGORY && m_Items[i].categoryHash==m_SearchCategoryHash)
					{
						size_t first=i;
						size_t last=i;
						for (i++;i<m_ScrollCount;i++)
						{
							if (m_Items[i].categoryHash==m_SearchCategoryHash)
								last=i;
							else
								break;
						}
						int top=m_Items[first].itemRect.top;
						int bottom=m_Items[last].itemRect.bottom;
						if (m_ScrollOffset<bottom-m_ScrollHeight+m_ScrollButtonSize)
							m_ScrollOffset=bottom-m_ScrollHeight+m_ScrollButtonSize;
						if (m_ScrollOffset>top)
							m_ScrollOffset=top;
						break;
					}
			}
		}
		else
			m_ScrollOffset=0;
	}
	else
		m_ScrollOffset=m_ScrollHeight=0;
	UpdateScroll();
	m_bScrollUpHot=m_bScrollDownHot=false;

	// hide items that didn't fit
	if (!m_bSubMenu)
	{
		for (int i=m_ScrollCount;i<m_OriginalCount;i++)
		{
			int bottom=m_Items[i].column==0?m_rContent.bottom:m_rContent2.bottom;
			if (m_Items[i].itemRect.bottom>bottom)
				m_Items[i].itemRect.bottom=m_Items[i].itemRect.top;
		}
	}

	m_rMenu.left=m_rMenu.top=0;
	m_rMenu.right=totalWidth;
	m_rMenu.bottom=totalHeight;
	int oldOffset=m_BitmapOffset;
	if (!m_bSubMenu)
	{
		m_BitmapOffset=0;
		if (s_bWin7Style && s_Skin.Main_opacity!=MenuSkin::OPACITY_SOLID)
		{
			if (!(m_Options&CONTAINER_LEFT) && !s_bRTL)
				m_BitmapOffset=s_MenuWidthMax-totalWidth;
			if ((m_Options&CONTAINER_LEFT) && s_bRTL)
				m_BitmapOffset=s_MenuWidthMax-totalWidth;
			totalWidth=s_MenuWidthMax;
		}
	}
	if (m_BitmapOffset>0)
	{
		OffsetRect(&m_rMenu,m_BitmapOffset,0);
		OffsetRect(&m_rContent,m_BitmapOffset,0);
		OffsetRect(&m_rContent2,m_BitmapOffset,0);
		if (m_Region && !s_bRTL)
			OffsetRgn(m_Region,m_BitmapOffset,0);
		for (std::vector<MenuItem>::iterator it=m_Items.begin();it!=m_Items.end();++it)
			OffsetRect(&it->itemRect,m_BitmapOffset,0);
	}
	if (m_SearchIndex>=0)
	{
		m_Items[m_SearchIndex].itemRect.left+=s_Skin.Search_padding.left;
		m_Items[m_SearchIndex].itemRect.top+=s_Skin.Search_padding.top;
		m_Items[m_SearchIndex].itemRect.bottom-=s_Skin.Search_padding.bottom;
		m_Items[m_SearchIndex].itemRect.right-=s_Skin.Search_padding.right;
		RECT itemRect;
		GetItemRect(m_SearchIndex,itemRect);
		itemRect.right-=(itemRect.bottom-itemRect.top);
		if (!s_Skin.Search_frame)
			InflateRect(&itemRect,-1,-3);
		if (m_SearchBox.m_hWnd)
		{
			if (oldOffset==m_BitmapOffset)
				m_SearchBox.SetWindowPos(NULL,&itemRect,SWP_NOZORDER);
		}
		else
		{
			m_SearchBox.Create(L"EDIT",m_hWnd,itemRect,NULL,WS_CHILD|(s_Skin.Search_frame?WS_BORDER:0)|ES_AUTOHSCROLL|ES_WANTRETURN);
			SetWindowSubclass(m_SearchBox,SubclassSearchBox,(UINT_PTR)this,0);
			int index=(m_bTwoColumns && m_Items[m_SearchIndex].column==1)?1:0;
			if (index==1 && (m_SearchIndex==0 || m_Items[m_SearchIndex-1].column==0))
				m_SearchBox.SetFont(s_Skin.ItemSettings[MenuSkin::COLUMN2_ITEM].font);
			else
				m_SearchBox.SetFont(s_Skin.ItemSettings[MenuSkin::COLUMN1_ITEM].font);
			m_SearchIcons=(HBITMAP)LoadImage(g_Instance,MAKEINTRESOURCE(IDB_SEARCH_ICONS),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
			PremultiplyBitmap(m_SearchIcons,0xFFFFFF);
		}
	}

	if (m_bTwoColumns && s_MenuMode==MODE_JUMPLIST)
	{
		// trim jumplist items after creating the search box
		int maxH=m_rContent2.bottom;
		if (m_SearchIndex>=0)
		{
			int h=m_Items[m_SearchIndex].itemRect.top-s_Skin.Search_padding.top-s_Skin.Search_background_jump_padding.top;
			if (maxH>h) maxH=h;
		}
		if (shutdownIndex>=0)
		{
			int h=m_Items[shutdownIndex].itemRect.top;
			if (maxH>h) maxH=h;
		}

		for (std::vector<MenuItem>::iterator it=m_Items.begin()+m_OriginalCount;it!=m_Items.end();++it)
		{
			if (it->itemRect.bottom>maxH)
				it->itemRect.bottom=it->itemRect.top;
		}
	}

	// calculate padding rect
	if (m_bSubMenu)
	{
		m_rPadding.top=-1;
		m_rPadding.left=m_rPadding.right=m_rPadding.bottom=0;
		int padColumn=-1;
		for (size_t i=0;i<m_Items.size();i++)
		{
			MenuItem &item=m_Items[i];
			if (item.id==MENU_NO)
			{
				m_rPadding.left=item.itemRect.left;
				m_rPadding.right=item.itemRect.right;
				m_rPadding.top=item.itemRect.bottom;
				m_rPadding.bottom=-1;
				padColumn=item.column;
			}
			else if (m_rPadding.bottom==-1 && padColumn==item.column)
			{
				m_rPadding.bottom=item.itemRect.top;
			}
		}
		if (m_rPadding.top>=0 && m_rPadding.bottom==-1)
		{
			m_rPadding.bottom=totalHeight-(m_bSubMenu?menuPadding[0].bottom:menuPadding[0].bottom);
		}
	}
	menuRect.top=menuRect.left=0;
	menuRect.right=totalWidth;
	menuRect.bottom=totalHeight;
	if ((m_bSubMenu?s_Skin.Submenu_opacity:s_Skin.Main_opacity)==MenuSkin::OPACITY_SOLID)
		AdjustWindowRect(&menuRect,GetWindowLong(GWL_STYLE),FALSE);
	int dx=corner.x-((m_Options&CONTAINER_LEFT)?menuRect.left:menuRect.right);
	int dy;
	if (m_Options&CONTAINER_SEARCH)
	{
		RECT itemRect;
		m_pParent->GetItemRect(m_ParentIndex,itemRect);
		m_pParent->MapWindowPoints(NULL,&itemRect);
		dy=(m_Options&CONTAINER_TOP)?(itemRect.top-menuPadding[0].top-menuRect.top):(itemRect.bottom+menuPadding[0].bottom-menuRect.bottom);
	}
	else
	{
		dy=corner.y-((m_Options&CONTAINER_TOP)?menuRect.top:menuRect.bottom);
	}
	OffsetRect(&menuRect,dx,dy);
	if (m_bSubMenu)
	{
		// make the menu fit on screen
		int dy2=0;
		if (menuRect.bottom>s_MenuLimits.bottom)
			dy2=s_MenuLimits.bottom-menuRect.bottom;
		if (menuRect.top+dy2<s_MenuLimits.top)
			dy2=s_MenuLimits.top-menuRect.top;
		OffsetRect(&menuRect,0,dy2);
	}
	if (s_UserPicture.m_hWnd && s_MenuMode!=MODE_SEARCH && columnWidths.size()>2 && columnWidths[1]==0)
	{
		s_UserPicture.DestroyWindow();
		s_UserPictureRect.top=s_UserPictureRect.bottom=0;
	}
	if (s_UserPicture.m_hWnd)
	{
		s_UserPictureRect.bottom=s_Skin.User_frame_position.x;
		if (!s_Skin.User_bitmap_outside || (m_Options&CONTAINER_TOP))
			s_UserPictureRect.bottom=m_rContent2.top+s_Skin.User_bitmapSize.cy+s_Skin.User_image_padding.x;
		s_UserPictureRect.bottom+=menuRect.top;
		s_UserPictureRect.top=s_UserPictureRect.bottom-s_Skin.User_bitmapSize.cy;
		if (s_bRTL)
			s_UserPictureRect.left=menuRect.right-(m_rContent2.left+m_rContent2.right+s_Skin.User_bitmapSize.cx)/2;
		else
			s_UserPictureRect.left=menuRect.left+(m_rContent2.left+m_rContent2.right-s_Skin.User_bitmapSize.cx)/2;
		s_UserPictureRect.right=s_UserPictureRect.left+s_Skin.User_bitmapSize.cx;
	}

	// create scrollbar
	if (m_SearchScrollCount>m_SearchScrollHeight)
	{
		if (!m_Scrollbar.m_hWnd)
		{
			m_Scrollbar.Create(WC_SCROLLBAR,m_hWnd,NULL,NULL,WS_CHILD|SBS_VERT);
			SetWindowSubclass(m_Scrollbar,SubclassScrollbar,(UINT_PTR)this,0);
			if (s_Skin.BHasScrollbar)
				m_ScrollTheme=OpenThemeData(m_Scrollbar,L"scrollbar");
		}
		RECT rcScroll=m_rContent;
		rcScroll.bottom=rcScroll.top+m_SearchScrollHeight*s_Skin.ItemSettings[MenuSkin::LIST_ITEM].itemHeight;
		rcScroll.left=rcScroll.right-scrollWidth;
		SCROLLINFO info={sizeof(info),SIF_ALL,0,m_SearchScrollCount-1,(UINT)m_SearchScrollHeight};
		m_Scrollbar.SetScrollInfo(SB_CTL,&info,FALSE);
		m_Scrollbar.SetWindowPos(NULL,&rcScroll,SWP_NOZORDER|SWP_SHOWWINDOW);
	}
	else if (m_Scrollbar.m_hWnd)
		m_Scrollbar.ShowWindow(SW_HIDE);

	m_bTrackMouse=false;
	m_bScrollTimerMouse=false;
	m_bScrollTimerTouch=false;
	m_InsertMark=-1;
	m_HotItem=-1;
	m_bHotArrow=false;
	SetSubmenu(-1);
	m_MouseWheel=0;

	if (!m_bSubMenu)
	{
		TOOLINFO tool={sizeof(tool),TTF_SUBCLASS|TTF_TRANSPARENT|(s_bRTL?TTF_RTLREADING:0U)};
		tool.hwnd=m_hWnd;
		tool.uId=2;
		s_Tooltip.SendMessage(TTM_DELTOOL,0,(LPARAM)&tool);
		tool.uId=3;
		s_Tooltip.SendMessage(TTM_DELTOOL,0,(LPARAM)&tool);
		tool.uId=(UINT_PTR)s_UserPicture.m_hWnd;
		s_Tooltip.SendMessage(TTM_DELTOOL,0,(LPARAM)&tool);

		if (m_rUser1.left<m_rUser1.right || m_rUser2.left<m_rUser2.right || s_UserPicture.m_hWnd)
		{
			// construct the text Log Off <username>...
			wchar_t user[256]={0};
			ULONG size=_countof(user);
			if (!GetUserNameEx(NameDisplay,user,&size))
			{
				// GetUserNameEx may fail (for example on Home editions). use the login name
				DWORD size=_countof(user);
				GetUserName(user,&size);
			}
			tool.lpszText=user;

			if (m_rUser1.left<m_rUser1.right)
			{
				tool.uId=2;
				tool.rect=m_rUser1;
				s_Tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);
			}
			if (m_rUser2.left<m_rUser2.right)
			{
				tool.uId=3;
				tool.rect=m_rUser2;
				s_Tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);
			}
			if (s_UserPicture.m_hWnd)
			{
				tool.uFlags|=TTF_IDISHWND;
				tool.uId=(UINT_PTR)s_UserPicture.m_hWnd;
				s_Tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);
			}
		}
	}

	if (m_pParent && (m_pParent->m_Items[m_ParentIndex].id==MENU_PROGRAMS || m_pParent->m_Items[m_ParentIndex].id==MENU_APPS))
	{
		ULONGLONG curTime;
		GetSystemTimeAsFileTime((FILETIME*)&curTime);
		CRegKey regKey;
		if (regKey.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu",KEY_WRITE)!=ERROR_SUCCESS)
			regKey.Create(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu");

		if (m_pParent->m_Items[m_ParentIndex].id==MENU_PROGRAMS)
			regKey.SetQWORDValue(L"LastProgramsTime",curTime);
		else if (m_pParent->m_Items[m_ParentIndex].id==MENU_APPS)
			regKey.SetQWORDValue(L"LastAppsTime",curTime);
	}
}

void CMenuContainer::InitWindowFinalize( const RECT &menuRect )
{
	SetWindowPos(NULL,&menuRect,SWP_NOZORDER|SWP_NOACTIVATE|SWP_DEFERERASE);
	// for some reason the region must be set after the call to SetWindowPos. otherwise it doesn't work for RTL windows
	if (m_bSubMenu || !s_bWin7Style || s_OldMenuState.mode==MODE_UNKNOWN)
		ApplyRegion(TRUE);
	Invalidate();
}

void CMenuContainer::ApplyRegion( BOOL bRedraw )
{
	if (m_Region)
	{
		int size=GetRegionData(m_Region,0,NULL);
		std::vector<char> buf(size);
		GetRegionData(m_Region,size,(RGNDATA*)&buf[0]);
		XFORM xform={1,0,0,1};
		if (s_bRTL)
		{
			// mirror the region (again)
			xform.eM11=-1;
			xform.eDx=(float)(m_rMenu.right+m_rMenu.left-m_BitmapOffset);
		}
		HRGN rgn=ExtCreateRegion(&xform,size,(RGNDATA*)&buf[0]);

		if (!SetWindowRgn(rgn,bRedraw))
			DeleteObject(rgn); // otherwise the OS takes ownership of the region, no need to free
	}
	else
		SetWindowRgn(NULL,bRedraw);
}

void CMenuContainer::UpdateScroll( void )
{
	if (m_ScrollHeight==0)
		m_bScrollUp=m_bScrollDown=false;
	else
	{
		m_bScrollUp=(m_ScrollOffset>0);
		m_bScrollDown=(m_ScrollOffset+m_ScrollHeight<m_Items[m_ScrollCount-1].itemRect.bottom-m_rContent.top);
	}
}

void CMenuContainer::UpdateScroll( const POINT *pt, bool bTouch )
{
	if (m_bScrollUp)
	{
		RECT rc=m_rContent;
		rc.bottom=rc.top+m_ScrollButtonSize;
		bool bHot=pt && PtInRect(&rc,*pt);
		if (m_bScrollUpHot!=bHot)
		{
			m_bScrollUpHot=bHot;
			Invalidate();
		}
	}
	else
		m_bScrollUpHot=false;

	if (m_bScrollDown)
	{
		RECT rc=m_rContent;
		rc.bottom=m_rContent.top+m_ScrollHeight;
		rc.top=rc.bottom-m_ScrollButtonSize;
		bool bHot=pt && PtInRect(&rc,*pt);
		if (m_bScrollDownHot!=bHot)
		{
			m_bScrollDownHot=bHot;
			Invalidate();
		}
	}
	else
		m_bScrollDownHot=false;

	if (m_bScrollUpHot || m_bScrollDownHot)
	{
		if (!bTouch && !m_bScrollTimerMouse)
		{
			m_bScrollTimerMouse=true;
			SetTimer(TIMER_SCROLL_MOUSE,50);
		}
		if (bTouch && !m_bScrollTimerTouch)
		{
			m_bScrollTimerTouch=true;
			SetTimer(TIMER_SCROLL_TOUCH,50);
		}
	}
	else if (!bTouch)
	{
		if (m_bScrollTimerMouse)
		{
			m_bScrollTimerMouse=false;
			KillTimer(TIMER_SCROLL_MOUSE);
		}
	}
	else
	{
		if (m_bScrollTimerTouch)
		{
			m_bScrollTimerTouch=false;
			KillTimer(TIMER_SCROLL_TOUCH);
		}
	}
}

LRESULT CMenuContainer::OnCreate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	MenuSkin::TOpacity opacity=(m_bSubMenu?s_Skin.Submenu_opacity:s_Skin.Main_opacity);
	if (opacity==MenuSkin::OPACITY_ALPHA || opacity==MenuSkin::OPACITY_FULLALPHA)
	{
		MARGINS margins={-1};
		DwmExtendFrameIntoClientArea(m_hWnd,&margins);
	}
	else if ((opacity==MenuSkin::OPACITY_GLASS || opacity==MenuSkin::OPACITY_FULLGLASS) && GetWinVersion()>=WIN_VER_WIN10)
	{
		tSetWindowCompositionAttribute SetWindowCompositionAttribute=(tSetWindowCompositionAttribute)GetProcAddress(GetModuleHandle(L"user32.dll"),"SetWindowCompositionAttribute");
		if (SetWindowCompositionAttribute)
		{
			int a=((GetSettingInt(L"GlassOpacity")*255)/100)<<24;
			int data[4]={3,0x13};
			bool bDef=true;
			if (GetSettingBool(L"GlassOverride"))
				data[2]=(GetSettingInt(L"GlassColor",bDef)&0xFFFFFF)|a;
			if (bDef)
			{
				int dr, dg, db;
				GetMetroGlassColor(dr,dg,db);
				data[2]=dr|(dg<<8)|(db<<16)|a;
			}
			WINCOMPATTRDATA attrData={0x13,&data,sizeof(data)};
			SetWindowCompositionAttribute(m_hWnd,&attrData);
		}
	}

	if (!m_pParent)
		BufferedPaintInit();
	if (this==s_Menus[0])
		s_FirstMenu=m_hWnd;
	else
		InitWindow();
	if (m_Options&CONTAINER_SEARCH)
		s_SearchMenu=m_hWnd;
	s_HotPos=GetMessagePos();
	m_pAccessible=NULL;
	if (GetSettingBool(L"EnableAccessibility"))
	{
		if (SUCCEEDED(m_pAccessibleContext.CoCreateInstance(CLSID_ContextSwitcher)))
		{
			CreateAccessibleData createData={this};
			ComCallData callData={};
			callData.pUserDefined=&createData;
			if (SUCCEEDED(m_pAccessibleContext->ContextCallback(CreateAccessible,&callData,IID_IAccessible,4,NULL)))
			{
				if (FAILED(CoGetInterfaceAndReleaseStream(createData.pStream,IID_IAccessible,(void**)&m_pAccessible)))
				{
					m_pAccessibleContext=NULL;
				}
			}
			else
			{
				m_pAccessibleContext=NULL;
			}
		}
		NotifyWinEvent(EVENT_SYSTEM_MENUPOPUPSTART,m_hWnd,OBJID_CLIENT,CHILDID_SELF);
	}
	m_pDropTargetProxy=new CDropTargetProxy(this);
	RegisterDragDrop(m_hWnd,m_pDropTargetProxy);
	if (!m_bSubMenu && s_pFrameworkInputPane)
		s_pFrameworkInputPane->AdviseWithHWND(m_hWnd,this,&m_InputCookie);
	PlayMenuSound(m_bSubMenu?SOUND_POPUP:SOUND_MAIN);
	return 0;
}

HRESULT __stdcall CMenuContainer::CreateAccessible( ComCallData *pData )
{
	CreateAccessibleData *pCreateData=(CreateAccessibleData*)pData->pUserDefined;
	CComPtr<CMenuAccessible> pAccessible=new CMenuAccessible(pCreateData->pMenu);
	HRESULT hr=CoMarshalInterThreadInterfaceInStream(IID_IAccessible,pAccessible,&pCreateData->pStream);
	if (FAILED(hr))
	{
		pAccessible->Reset();
	}
	return hr;
}

HRESULT __stdcall CMenuContainer::ReleaseAccessible( ComCallData *pData )
{
	return CoDisconnectContext(INFINITE);
}

bool CMenuContainer::GetItemRect( int index, RECT &rc )
{
	if (index>=0 && index<(int)m_Items.size())
	{
		rc=m_Items[index].itemRect;
		if (m_ScrollHeight>0 && index<m_ScrollCount)
		{
			OffsetRect(&rc,0,-m_ScrollOffset);
			if (m_bScrollUp && rc.bottom<=m_rContent.top+m_ScrollButtonSize)
				return false;
			if (m_bScrollDown && rc.top>=m_rContent.top+m_ScrollHeight-m_ScrollButtonSize)
				return false;
		}
		if (m_SearchScrollCount>m_SearchScrollHeight && index>=m_OriginalCount)
		{
			OffsetRect(&rc,0,-m_SearchScrollPos*(rc.bottom-rc.top));
		}
	}
	return true;
}

int CMenuContainer::HitTest( const POINT &pt, bool *bArrow, bool bDrop )
{
	if (m_bScrollUp && pt.y<m_rContent.top+m_ScrollButtonSize)
		return -1;
	int start=0;
	if (m_bScrollDown && pt.y>=m_rContent.top+m_ScrollHeight-m_ScrollButtonSize)
		start=m_ScrollCount;
	int n=(int)m_Items.size();
	for (int i=start;i<n;i++)
	{
		if (!CanSelectItem(i,false)) continue;
		RECT rc=m_Items[i].itemRect;
		if (m_Items[i].id==MENU_SEARCH_BOX)
		{
			rc.left=rc.right-(rc.bottom-rc.top);
		}
		if (m_ScrollHeight>0 && i<m_ScrollCount)
		{
			OffsetRect(&rc,0,-m_ScrollOffset);
		}
		else if (m_SearchScrollCount>m_SearchScrollHeight && i>=m_OriginalCount)
		{
			OffsetRect(&rc,0,-m_SearchScrollPos*(rc.bottom-rc.top));
		}
		else if (bDrop && m_bTwoColumns && i<n-1 && m_Items[i+1].column==0 && m_Items[i+1].bAlignBottom)
			rc.bottom=m_Items[i+1].itemRect.top; // when dropping on the padding of the first column, assume the item above the padding was hit
		if (PtInRect(&rc,pt))
		{
			if (bArrow)
			{
				*bArrow=false;
				const MenuItem &item=m_Items[i];
				if (item.bSplit)
				{
					const MenuSkin::ItemDrawSettings &settings=s_Skin.ItemSettings[item.drawType];
					int arrWidth=settings.arrPadding.cx+settings.arrPadding.cy;
					if (item.jumpIndex>=0)
						arrWidth+=s_Skin.Pin_bitmap_Size.cx;
					else if (item.id==MENU_SEARCH_CATEGORY)
						arrWidth+=s_Skin.More_bitmap_Size.cx;
					else
						arrWidth+=settings.arrSize.cx;
					*bArrow=(pt.x>=item.itemRect.right-arrWidth);
				}
				else if (item.id==MENU_SEARCH_CATEGORY && item.categoryHash>=CSearchManager::CATEGORY_FILE)
				{
					int x=pt.x-rc.left;
					*bArrow=(x>=item.textStart && x<item.textEnd);
				}
			}
			return i;
		}
	}
	return -1;
}

void CMenuContainer::InvalidateItem( int index )
{
	if (index>=0)
	{
		RECT rc;
		GetItemRect(index,rc);
		InvalidateRect(&rc);
	}
}

void CMenuContainer::SetHotItem( int index, bool bArrow, bool bShowTip )
{
	if (index<0 && (m_Options&CONTAINER_SEARCH))
		return;
	if (index>=0)
		s_bOverrideFirstDown=false;
	if (index==m_HotItem && bArrow==m_bHotArrow) return;
	if ((index>=0)!=(m_HotItem>=0))
	{
		InvalidateItem(m_Submenu);
		InvalidateItem(m_ContextItem);
	}
	if (m_HotItem>=0 && m_HotItem==m_ProgramTreeIndex && s_MenuMode==MODE_PROGRAMS)
		m_pProgramsTree->Invalidate();
	else
		InvalidateItem(m_HotItem);
	if (index>=0 && index==m_ProgramTreeIndex && s_MenuMode==MODE_PROGRAMS)
		m_pProgramsTree->Invalidate();
	else
	{
		InvalidateItem(index);
		if (index>=0 && m_pProgramsTree && m_pProgramsTree->m_hWnd && m_pProgramsTree->m_hWnd==GetFocus())
			SetFocus();
	}
	m_HotItem=index;
	m_bHotArrow=bArrow;
	s_pTipMenu=NULL;
	s_TipItem=-1;
	UpdateUserPicture();
	if (index>=0)
	{
		s_pHotMenu=this;
		s_HotItem=index;
	}
	else if (s_pHotMenu==this)
	{
		s_pHotMenu=NULL;
		s_HotItem=-1;
		if (s_Tooltip.m_hWnd)
		{
			TOOLINFO tool={sizeof(tool),TTF_ABSOLUTE|TTF_TRACK|TTF_TRANSPARENT};
			tool.uId=1;
			s_Tooltip.SendMessage(TTM_TRACKACTIVATE,FALSE,(LPARAM)&tool);
		}
	}
	else
		return;
	if (index>=0 && index<(int)m_Items.size())
	{
		if (bShowTip)
		{
			int show, hide;
			if (m_Items[index].bFolder && m_Items[index].id!=MENU_SHUTDOWN_BUTTON)
				show=s_TipShowTimeFolder, hide=s_TipHideTimeFolder;
			else
				show=s_TipShowTime, hide=s_TipHideTime;
			if (s_Tooltip.m_hWnd)
			{
				TOOLINFO tool={sizeof(tool),TTF_ABSOLUTE|TTF_TRACK|TTF_TRANSPARENT};
				tool.uId=1;
				s_Tooltip.SendMessage(TTM_TRACKACTIVATE,FALSE,(LPARAM)&tool);
				if (!s_Menus[0]->m_bDestroyed && hide>0)
				{
					s_pTipMenu=s_pHotMenu;
					s_TipItem=s_HotItem;
					s_Menus[0]->SetTimer(TIMER_TOOLTIP_SHOW,show);
				}
			}
		}
		NotifyWinEvent(EVENT_OBJECT_FOCUS,m_hWnd,OBJID_CLIENT,index+1);
	}
}

void CMenuContainer::SetSubmenu( int index )
{
	if (m_Submenu==index) return;
	m_Submenu=index;
	UpdateUserPicture();
}

void CMenuContainer::SetContextItem( int index )
{
	if (m_ContextItem==index) return;
	m_ContextItem=index;
	UpdateUserPicture();
}

void CMenuContainer::SetClickItem( int index )
{
	if (m_ClickIndex==index) return;
	m_ClickIndex=index;
	InvalidateItem(m_ClickIndex);
}

void CMenuContainer::UpdateUserPicture( void )
{
	if (m_bTwoColumns && s_UserPicture.m_hWnd && s_UserPicture.IsWindowVisible())
	{
		HBITMAP bmp=NULL;
		int bmpIndex=m_HotItem>=0?m_HotItem:(m_ContextItem>=0?m_ContextItem:m_Submenu);
		if (bmpIndex>=0 && bmpIndex<m_OriginalCount && bmpIndex<(int)m_Items.size() && m_Items[bmpIndex].column==1 && m_Items[bmpIndex].pItemInfo && m_Items[bmpIndex].pItemInfo->extraLargeIcon)
			bmp=m_Items[bmpIndex].pItemInfo->extraLargeIcon->bitmap;
		s_UserPicture.StartImageTimer(bmp);
	}
}

void CMenuContainer::SetInsertMark( int index, bool bAfter )
{
	if (index==m_InsertMark && bAfter==m_bInsertAfter) return;
	RECT rc;
	if (GetInsertRect(rc))
		InvalidateRect(&rc);
	m_InsertMark=index;
	m_bInsertAfter=bAfter;
	if (GetInsertRect(rc))
		InvalidateRect(&rc);
}

bool CMenuContainer::GetInsertRect( RECT &rc )
{
	if (m_InsertMark<0 || m_InsertMark>=(int)m_Items.size())
		return false;
	const MenuItem &item=m_Items[m_InsertMark];
	rc=item.itemRect;
	if (m_bInsertAfter)
		rc.top=rc.bottom;
	if (m_ScrollHeight>0 && m_InsertMark<m_ScrollCount)
		rc.top-=m_ScrollOffset;
	const POINT *sizes=s_Skin.GetArrowsBitmapSizes();
	int h=sizes[1].y;
	rc.top-=h/2;
	rc.bottom=rc.top+h;
	return true;
}

LRESULT CMenuContainer::OnSetContextItem( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	for (int i=0;i<(int)m_Items.size();i++)
	{
		if (m_Items[i].nameHash==wParam)
		{
			SetContextItem(i);
			break;
		}
	}
	return 0;
}

LRESULT CMenuContainer::OnColorEdit( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (!m_bNoSearchDraw)
		PostMessage(MCM_REDRAWEDIT);

	HDC hdc=(HDC)wParam;
	SetTextColor(hdc,s_Skin.Search_text_colors[0]);
	SetBkColor(hdc,s_Skin.Search_text_background);
	SetDCBrushColor(hdc,s_Skin.Search_text_background);
	return (LRESULT)GetStockObject(DC_BRUSH);
}

LRESULT CMenuContainer::OnGesture( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	GESTUREINFO info={sizeof(info)};
	if (GetGestureInfo((HGESTUREINFO)lParam,&info) && info.dwID==GID_PAN && info.hwndTarget==m_hWnd)
	{
		if (m_Submenu>=0)
		{
			SetActiveWindow();
			CloseSubMenus(0,this);
		}
		if (info.dwFlags&GF_BEGIN)
		{
			m_PanPosY=info.ptsLocation.y;
			BeginPanningFeedback(m_hWnd);
			m_Overpan=0;
		}
		else if (info.dwFlags&GF_END)
		{
			EndPanningFeedback(m_hWnd,TRUE);
			m_Overpan=0;
			s_HotPos=GetMessagePos();
		}
		else
		{
			int dy=info.ptsLocation.y-m_PanPosY;
			m_Overpan+=dy;
			if (dy!=0 && (m_ScrollHeight>0 || m_SearchScrollCount>0))
			{
				bool bOverpan=false;
				if (m_SearchScrollCount>0)
				{
					int height=s_Skin.ItemSettings[MenuSkin::LIST_ITEM].itemHeight;
					int count=dy/height;
					m_PanPosY+=count*height;
					int pos0=m_SearchScrollPos;
					int pos=m_SearchScrollPos-count;
					m_SearchScrollPos=m_Scrollbar.SetScrollPos(SB_CTL,pos);
					if (m_SearchScrollPos!=pos0)
						InvalidateRect(&m_rContent);
					bOverpan=(m_SearchScrollPos!=pos);
				}
				else
				{
					m_PanPosY=info.ptsLocation.y;
					int scroll=m_ScrollOffset;
					scroll-=dy;
					if (scroll<0)
					{
						scroll=0;
						bOverpan=true;
					}
					int total=m_Items[m_ScrollCount-1].itemRect.bottom-m_rContent.top-m_ScrollHeight;
					if (scroll>total)
					{
						scroll=total;
						bOverpan=true;
					}
					if (m_ScrollOffset!=scroll)
					{
						m_ScrollOffset=scroll;
						UpdateScroll();
						Invalidate();
						s_HotPos=-1;
					}
				}
				if (bOverpan)
					UpdatePanningFeedback(m_hWnd,0,m_Overpan,info.dwFlags&GF_INERTIA);
				else
					m_Overpan=0;
			}
		}
		CloseGestureInfoHandle((HGESTUREINFO)lParam);
		return 0;
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CMenuContainer::OnGestureNotify( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (!s_bHasTouch)
	{
		bHandled=FALSE;
		return 0;
	}
	GESTURENOTIFYSTRUCT *pNotify=(GESTURENOTIFYSTRUCT*)lParam;
	if (pNotify->hwndTarget==m_hWnd)
	{
		// if clicked on a scrollable item
		bool bScrollable=false;
		POINT pt={pNotify->ptsLocation.x,pNotify->ptsLocation.y};
		ScreenToClient(&pt);
		int idx=HitTest(pt,NULL);
		if (idx>=0)
		{
			if (idx<m_ScrollCount && m_ScrollHeight>0)
				bScrollable=true;
			else if (idx>=m_OriginalCount && m_SearchScrollCount>0)
				bScrollable=true;
		}
		if (bScrollable)
		{
			GESTURECONFIG config={GID_PAN,GC_PAN_WITH_SINGLE_FINGER_VERTICALLY|GC_PAN_WITH_INERTIA,GC_PAN_WITH_SINGLE_FINGER_HORIZONTALLY};
			SetGestureConfig(pNotify->hwndTarget,0,1,&config,sizeof(config));
		}
		else
		{
			GESTURECONFIG config={0,0,GC_ALLGESTURES};
			SetGestureConfig(pNotify->hwndTarget,0,1,&config,sizeof(config));
		}
	}
	else if (m_pProgramsTree && pNotify->hwndTarget==m_pProgramsTree->m_hWnd)
	{
		GESTURECONFIG config={0,GC_ALLGESTURES,0};
		SetGestureConfig(pNotify->hwndTarget,0,1,&config,sizeof(config));
	}
	else if (pNotify->hwndTarget==m_Scrollbar.m_hWnd)
	{
		GESTURECONFIG config={0,0,GC_ALLGESTURES};
		SetGestureConfig(pNotify->hwndTarget,0,1,&config,sizeof(config));
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CMenuContainer::OnPointerDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	m_PointerId=0;
	if (s_bHasTouch && IS_POINTER_INCONTACT_WPARAM(wParam) && IS_POINTER_PRIMARY_WPARAM(wParam) && IS_POINTER_FIRSTBUTTON_WPARAM(wParam))
	{
		CPoint pt(lParam);
		ScreenToClient(&pt);
		UpdateScroll(&pt,true);
		if (m_bScrollTimerTouch)
		{
			m_PointerId=GET_POINTERID_WPARAM(wParam);
			return 0;
		}
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CMenuContainer::OnPointerUpdate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (s_bHasTouch && m_PointerId==GET_POINTERID_WPARAM(wParam))
	{
		if (IS_POINTER_INCONTACT_WPARAM(wParam) && IS_POINTER_FIRSTBUTTON_WPARAM(wParam))
		{
			CPoint pt(lParam);
			ScreenToClient(&pt);
			UpdateScroll(&pt,true);
		}
		else
		{
			KillTimer(TIMER_SCROLL_TOUCH);
			m_bScrollTimerTouch=false;
			if (m_bScrollUpHot || m_bScrollDownHot)
			{
				m_bScrollUpHot=m_bScrollDownHot=false;
				Invalidate();
			}
			m_PointerId=0;
		}
		return 0;
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CMenuContainer::OnPointerUp( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (s_bHasTouch && m_PointerId==GET_POINTERID_WPARAM(wParam))
	{
		KillTimer(TIMER_SCROLL_TOUCH);
		m_bScrollTimerTouch=false;
		if (m_bScrollUpHot || m_bScrollDownHot)
		{
			m_bScrollUpHot=m_bScrollDownHot=false;
			Invalidate();
		}
		m_PointerId=0;
		return 0;
	}
	bHandled=FALSE;
	return 0;
}

STDMETHODIMP CMenuContainer::QueryInterface( REFIID riid, void **ppvObject )
{
	if (riid==IID_IUnknown || riid==IID_IDropTarget)
	{
		*ppvObject=static_cast<IDropTarget*>(this);
		AddRef();
		return S_OK;
	}
	if (riid==__uuidof(IFrameworkInputPaneHandler))
	{
		*ppvObject=static_cast<IFrameworkInputPaneHandler*>(this);
		AddRef();
		return S_OK;
	}
	*ppvObject=NULL;
	return E_FAIL;
}

HRESULT STDMETHODCALLTYPE CMenuContainer::Showing( RECT *prcInputPaneScreenLocation, BOOL fEnsureFocusedElementInView )
{
	NotifyDisplayChange();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CMenuContainer::Hiding( BOOL fEnsureFocusedElementInView )
{
	NotifyDisplayChange();
	return S_OK;
}

LRESULT CMenuContainer::OnRedrawEdit( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	m_SearchBox.RedrawWindow();
	return 0;
}

LRESULT CMenuContainer::OnRefreshIcons( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	for (std::vector<CMenuContainer*>::iterator it=s_Menus.begin();it!=s_Menus.end();++it)
		if (!(*it)->m_bDestroyed)
		{
			(*it)->Invalidate();
			if (m_pProgramsTree && m_pProgramsTree->m_hWnd)
				m_pProgramsTree->Invalidate();
		}
	return 0;
}

void CMenuContainer::RefreshIcons( void )
{
	// this is called from the background thread
	HWND first=s_FirstMenu; // must copy into a temp variable because we don't want the value to change in the middle of the next two lines
	if (first)
		::PostMessage(first,MCM_REFRESHICONS,0,0);
}

void CMenuContainer::RefreshSearch( void )
{
	// this is called from the background thread
	HWND search=s_SearchMenu; // must copy into a temp variable because we don't want the value to change in the middle of the next two lines
	if (search)
		::PostMessage(search,MCM_REFRESH,0,0);
}

LRESULT CMenuContainer::OnSetHotItem( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	int index=(int)wParam;
	while (index<(int)m_Items.size() && !CanSelectItem(index,false))
		index++;
	if (index<(int)m_Items.size())
		SetHotItem(index,false,true);
	return 0;
}

LRESULT CMenuContainer::OnStartMenuMsg( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (lParam)
		s_StartMenuParams=*(StartMenuParams*)lParam;
	return 0;
}

void CMenuContainer::SetSearchState( TSearchState state )
{
	if (m_SearchState==state)
		return;
	if (m_Submenu!=-1)
	{
		InvalidateRect(&m_Items[m_Submenu].itemRect);
 		SetSubmenu(-1);
	}
	InvalidateRect(&m_Items[m_SearchIndex].itemRect);
	if (m_SearchState==SEARCH_TEXT && state!=SEARCH_TEXT)
	{
		// close the search menu
		m_SearchScrollCount=0;
		if (s_MenuMode==MODE_SEARCH)
			SetMenuMode(s_PreSearchMenuMode);
		else
			CloseSubMenus(CLOSE_ONLY_SEARCH,this); // can't use CLOSE_POST here because the menu needs to be closed while m_bInSearchUpdate is set
		g_SearchManager.BeginSearch(CString());
	}
	m_SearchState=state;
	if (m_SearchState==SEARCH_NONE)
		s_bDisableHover=false;
}

LRESULT CMenuContainer::OnEditChange( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	s_bPendingSearchEnter=false;
	m_SearchBox.RedrawWindow();
	wchar_t text[256];
	m_SearchBox.GetWindowText(text,_countof(text));
	DoEnvironmentSubst(text,_countof(text));
	unsigned int hash=CalcFNVHash(text);
	if (m_SearchHash!=hash && !m_bInSearchUpdate)
		SetTimer(TIMER_SEARCH,100);
	m_SearchHash=hash;
	return 0;
}

void CMenuContainer::UpdateSearchResults( bool bForceShowAll )
{
	if (m_bInSearchUpdate)
		return;
	m_bInSearchUpdate=true;
	wchar_t text[256];
	m_SearchBox.GetWindowText(text,_countof(text));
	DoEnvironmentSubst(text,_countof(text));
	wchar_t *pText=text;
	while (*pText==' ' || *pText=='\t')
		pText++;
	TSearchState state=SEARCH_NONE;
	if (*pText)
	{
		int len=Strlen(pText);
		while (len>0 && (pText[len-1]==' ' || pText[len-1]=='\t'))
			len--;
		pText[len]=0;
		CharUpper(pText);
		s_SearchResults.currentString=pText;
		g_SearchManager.BeginSearch(s_SearchResults.currentString);
		s_SearchResults.bSearching=true;
		s_bPendingSearchEnter=false;
		if (s_bWin7Style)
		{
			MenuItem &item=m_Items[m_SearchIndex-m_SearchItemCount+1];
			item.id=MENU_SEARCH_EMPTY;
			item.name=FindTranslation(L"Menu.Searching",L"Searching...");
			item.pItemInfo=g_ItemManager.GetCustomIcon(L"imageres.dll,8",CItemManager::ICON_SIZE_TYPE_SMALL);
		}
		if (m_Submenu!=m_SearchIndex && s_MenuMode!=MODE_SEARCH)
		{
			ActivateData data;
			data.bNoModifiers=true;
			ActivateItem(m_SearchIndex,ACTIVATE_OPEN_SEARCH,NULL,&data);
		}
		Assert(s_SearchMenu);
		RefreshSearch();
		state=SEARCH_TEXT;
	}
	else if (GetFocus()==m_SearchBox.m_hWnd)
	{
		state=SEARCH_BLANK;
	}
	SetSearchState(state);
	m_bInSearchUpdate=false;
}

// Turn on the keyboard cues from now on. This is done when a keyboard action is detected
void CMenuContainer::ShowKeyboardCues( bool alt )
{
	if (!GetSettingBool(L"EnableAccelerators"))
		return;

	if (GetSettingBool(L"AltAccelerators") && !alt)
		return;

	if (!s_bKeyboardCues)
	{
		s_bKeyboardCues=true;
		for (std::vector<CMenuContainer*>::const_iterator it=s_Menus.begin();it!=s_Menus.end();++it)
			(*it)->Invalidate();
	}
}

void CMenuContainer::SetActiveWindow( void )
{
	HWND active=GetActiveWindow();
	if (active!=m_hWnd && active!=m_SearchBox.m_hWnd)
		::SetActiveWindow(m_hWnd);
	if (!m_bSubMenu && s_bBehindTaskbar && s_TaskBar)
		SetWindowPos(s_TaskBar,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE); // make sure the top menu stays behind the taskbar
}

void CMenuContainer::PostRefreshMessage( void )
{
	if (!m_bDestroyed)
	{
		if (!InterlockedExchange(&m_RefreshPosted,1))
			PostMessage(MCM_REFRESH);
	}
}

LRESULT CMenuContainer::OnSysCommand( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if ((wParam&0xFFF0)==SC_KEYMENU)
	{
		// stops Alt from activating the window menu
		ShowKeyboardCues(true);
		s_bOverrideFirstDown=false;
	}
	else
		bHandled=FALSE;
	return 0;
}

LRESULT CMenuContainer::OnSettingChange( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==SPI_SETWORKAREA)
		NotifyDisplayChange();
	bHandled=FALSE;
	return 0;
}

void CMenuContainer::NotifyDisplayChange( void )
{
	if (!m_bSubMenu && !m_bWorkAreaPosted && !s_bLockWorkArea)
	{
		m_bWorkAreaPosted=true;
		PostMessage(MCM_WORKAREACHANGED);
	}
}

LRESULT CMenuContainer::OnDisplayChange( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	NotifyDisplayChange();
	bHandled=FALSE;
	return 0;
}

LRESULT CMenuContainer::OnWorkAreaChanged( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	Assert(!m_bSubMenu);
	Assert(!s_bLockWorkArea);

	// resize main menu
	RECT taskbarRect;
	UINT uEdge=GetTaskbarPosition(s_TaskBar,NULL,NULL,&taskbarRect);
	if (uEdge==s_TaskBarEdge)
	{
		RECT area=CalculateWorkArea(taskbarRect);
		if (memcmp(&area,&s_MainMenuLimits,sizeof(RECT))!=0)
		{
			s_MainMenuLimits=area;
			s_MenuHeight=-1;
			POINT corner=CalculateCorner();
			RECT menuRect;
			InitWindowInternal(false,corner,menuRect);
			if (s_MenuMode==MODE_PROGRAMS)
			{
				// resize programs tree
				MenuItem &item=m_Items[m_ProgramTreeIndex];
				item.itemRect=m_rContent;
				item.itemRect.bottom=m_Items[m_ProgramTreeIndex+1].itemRect.top;
				RECT rc;
				m_pProgramsTree->GetWindowRect(&rc);
				int dh=rc.bottom-rc.top;
				m_pProgramsTree->GetClientRect(&rc);
				dh-=rc.bottom;
				int itemHeight=TreeView_GetItemHeight(m_pProgramsTree->m_hWnd);
				rc=item.itemRect;
				int h=rc.bottom-rc.top;
				int n=(h-dh)/itemHeight;
				rc.bottom=rc.top+n*itemHeight+dh;
				m_pProgramsTree->SetWindowPos(NULL,&rc,SWP_NOZORDER|SWP_NOACTIVATE);
			}
			else if (s_MenuMode==MODE_SEARCH)
			{
				// HACK: before initializing the search items we need to know the size of the menu, so InitWindow needs to be called twice
				InitSearchItems();
				InitWindowInternal(false,corner,menuRect);
			}

			if (s_UserPicture.m_hWnd && s_UserPictureRect.top<s_UserPictureRect.bottom)
			{
				s_UserPicture.SetWindowPos(NULL,&s_UserPictureRect,SWP_NOZORDER|SWP_NOACTIVATE);
				s_UserPicture.Update();
			}
			InitWindowFinalize(menuRect);
			UpdateWindow();
		}
	}

	m_bWorkAreaPosted=false;
	return 0;
}

LRESULT CMenuContainer::OnTimer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==TIMER_HOVER)
	{
		LOG_MENU(LOG_MOUSE,L"End Hover, hover=%d%s, hot=%d, submenu=%d",m_HoverItem,m_bHoverArrow?L"(arrow)":L"",m_HotItem,m_Submenu);
		// the mouse hovers over an item. open it.
		if (m_HoverItem>=0)
		{
			if (m_HoverItem!=m_Submenu && m_HoverItem!=m_SubJumpItem && m_HoverItem==m_HotItem && m_bHoverArrow==m_bHotArrow && !s_bDisableHover && m_SearchState<=SEARCH_BLANK)
			{
				if (m_HoverItem==m_ProgramTreeIndex)
					m_pProgramsTree->SetFocus();
				ActivateItem(m_HoverItem,ACTIVATE_OPEN,NULL);
				m_SubShowTime=GetTickCount();
			}
			if (m_HoverItem==m_ProgramButtonIndex)
				m_bDisableProgHover=true;
			m_HoverItem=-1;
			KillTimer(TIMER_HOVER);
		}
		return 0;
	}
	if (wParam==TIMER_SCROLL_MOUSE || wParam==TIMER_SCROLL_TOUCH)
	{
		if (wParam==TIMER_SCROLL_MOUSE && s_bHasTouch && GetKeyState(VK_LBUTTON)>=0)
			return 0;
		int speed=GetSettingInt(m_bSubMenu?L"SubMenuScrollSpeed":L"MainMenuScrollSpeed");
		if (speed<1) speed=1;
		if (speed>20) speed=20;
		int scroll=m_ScrollOffset;
		if (m_bScrollUp && m_bScrollUpHot)
		{
			scroll-=s_Skin.ItemSettings[m_bSubMenu?MenuSkin::SUBMENU_ITEM:MenuSkin::COLUMN1_ITEM].itemHeight*speed/6;
			if (scroll<0) scroll=0;
		}
		else if (m_bScrollDown && m_bScrollDownHot)
		{
			scroll+=s_Skin.ItemSettings[m_bSubMenu?MenuSkin::SUBMENU_ITEM:MenuSkin::COLUMN1_ITEM].itemHeight*speed/6;
			int total=m_Items[m_ScrollCount-1].itemRect.bottom-m_rContent.top-m_ScrollHeight;
			if (scroll>total) scroll=total;
		}
		if (m_ScrollOffset!=scroll)
		{
			m_ScrollOffset=scroll;
			UpdateScroll();
			if (!m_bScrollUp && !m_bScrollDown)
				KillTimer(wParam);
			Invalidate();
		}
	}
	if (wParam==TIMER_TOOLTIP_SHOW)
	{
		KillTimer(TIMER_TOOLTIP_SHOW);

		if (!s_pHotMenu || s_pHotMenu->m_bDestroyed)
			return 0;
		if (s_pHotMenu!=s_pTipMenu || s_HotItem!=s_TipItem)
			return 0;

		if (std::find(s_Menus.begin(),s_Menus.end(),s_pHotMenu)==s_Menus.end())
			return 0;

		if (s_HotItem>=(int)s_pHotMenu->m_Items.size())
			return 0;

		if (!m_bSubMenu && s_MenuMode==MODE_SEARCH && s_SearchResults.bSearching)
			return 0;

		TOOLINFO tool={sizeof(tool),TTF_ABSOLUTE|TTF_TRACK|TTF_TRANSPARENT|(s_bRTL?TTF_RTLREADING:0U)};
		tool.uId=1;

		wchar_t text[1024];
		if (!s_pHotMenu->GetDescription(s_HotItem,text,_countof(text)))
			return 0;

		RECT rc;
		s_pHotMenu->GetItemRect(s_HotItem,rc);
		s_pHotMenu->MapWindowPoints(NULL,&rc);
		DWORD pos=GetMessagePos();
		POINT pt={(short)LOWORD(pos),(short)HIWORD(pos)};
		if (PtInRect(&rc,pt))
		{
			pt.x+=8;
			pt.y+=16;
		}
		else if (!(s_pHotMenu->m_Options&CONTAINER_SEARCH))
		{
			pt.x=(rc.left+rc.right)/2;
			pt.y=rc.bottom;
		}
		else
			return 0;

		tool.lpszText=text;
		s_Tooltip.SendMessage(TTM_UPDATETIPTEXT,0,(LPARAM)&tool);
		s_Tooltip.SendMessage(TTM_TRACKPOSITION,0,MAKELONG(pt.x,pt.y));
		s_Tooltip.SendMessage(TTM_TRACKACTIVATE,TRUE,(LPARAM)&tool);

		// make sure the tooltip is inside the monitor
		s_Tooltip.GetWindowRect(&rc);
		int dx=0, dy=0;
		if (rc.left<s_MenuLimits.left) dx=s_MenuLimits.left-rc.left;
		if (rc.right+dx>s_MenuLimits.right) dx-=rc.right-s_MenuLimits.right;
		if (rc.top<s_MenuLimits.top) dy=s_MenuLimits.top-rc.top;
		if (rc.bottom+dy>s_MenuLimits.bottom) dy-=rc.bottom-s_MenuLimits.bottom;
		if (dx || dy)
			s_Tooltip.SendMessage(TTM_TRACKPOSITION,0,MAKELONG(pt.x+dx,pt.y+dy));

		if (s_pHotMenu->m_Items[s_HotItem].bFolder && s_pHotMenu->m_Items[s_HotItem].id!=MENU_SHUTDOWN_BUTTON)
			SetTimer(TIMER_TOOLTIP_HIDE,s_TipHideTimeFolder);
		else
			SetTimer(TIMER_TOOLTIP_HIDE,s_TipHideTime);
		return 0;
	}
	if (wParam==TIMER_TOOLTIP_HIDE)
	{
		TOOLINFO tool={sizeof(tool),TTF_ABSOLUTE|TTF_TRACK|TTF_TRANSPARENT};
		tool.uId=1;
		s_Tooltip.SendMessage(TTM_TRACKACTIVATE,FALSE,(LPARAM)&tool);
		KillTimer(TIMER_TOOLTIP_HIDE);
		return 0;
	}
	if (wParam==TIMER_BALLOON_HIDE)
	{
		TOOLINFO tool={sizeof(tool)};
		tool.uId=1;
		if (s_TooltipBalloon.m_hWnd)
			s_TooltipBalloon.SendMessage(TTM_TRACKACTIVATE,FALSE,(LPARAM)&tool);
		KillTimer(TIMER_BALLOON_HIDE);
	}
	if (wParam==TIMER_SEARCH)
	{
		UpdateSearchResults(false);
		KillTimer(TIMER_SEARCH);
	}
	if (wParam==TIMER_DRAG)
	{
		if (!s_bDragClosed)
		{
			// if the mouse is outside of the menu for more than 4 seconds close the menu
			DWORD pos=GetMessagePos();
			POINT pt={(short)LOWORD(pos),(short)HIWORD(pos)};
			HWND hWnd=WindowFromPoint(pt);
			if (hWnd) hWnd=GetAncestor(hWnd,GA_ROOT);
			wchar_t name[256];
			if (hWnd)
				GetClassName(hWnd,name,_countof(name));
			else
				name[0]=0;

			if (_wcsicmp(name,L"OpenShell.CMenuContainer")!=0)
			{
				int dt=GetMessageTime()-m_DragTime;
				if (dt>GetSettingInt(L"DragHideDelay"))
				{
					HideStartMenu();
					KillTimer(TIMER_DRAG);
					s_bDragClosed=true;
				}
			}
			else
			{
				m_DragTime=GetMessageTime();
			}
		}
	}
	return 0;
}

// Handle right-click and the menu keyboard button
LRESULT CMenuContainer::OnContextMenu( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (s_bNoContextMenu) return 0;
	POINT pt={(short)LOWORD(lParam),(short)HIWORD(lParam)};
	int index;
	BOOL bPad=FALSE;
	if (pt.x!=-1 || pt.y!=-1)
	{
		POINT pt2=pt;
		ScreenToClient(&pt2);
		index=HitTest(pt2,NULL);
		if (index<0)
			bPad=PtInRect(&m_rPadding,pt2);
		if (index<0 && !bPad) return 0;
		ActivateItem(index,ACTIVATE_MENU,&pt);
	}
	else
	{
		index=m_HotItem;
		if (index<0 && !bPad) return 0;
		ActivateItem(index,ACTIVATE_MENU,NULL);
	}
	return 0;
}

LRESULT CMenuContainer::OnVScroll( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	int pos0=m_SearchScrollPos;
	int pos=pos0;
	switch (LOWORD(wParam))
	{
		case SB_TOP:
			pos=0;
			break;
		case SB_BOTTOM:
			pos=m_SearchScrollCount;
			break;
		case SB_LINEUP:
			pos--;
			break;
		case SB_LINEDOWN:
			pos++;
			break;
		case SB_PAGEUP:
			pos-=m_SearchScrollHeight;
			break;
		case SB_PAGEDOWN:
			pos+=m_SearchScrollHeight;
			break;
		case SB_THUMBTRACK:
			pos=HIWORD(wParam);
			break;
		default:
			return 0;
	}
	m_SearchScrollPos=m_Scrollbar.SetScrollPos(SB_CTL,pos);
	if (m_SearchScrollPos!=pos0)
		InvalidateRect(&m_rContent);
	return 0;
}

bool CMenuContainer::CanSelectItem( int index, bool bKeyboard )
{
	if (!m_bSubMenu)
	{
		if (s_MenuMode==MODE_PROGRAMS && index<m_ScrollCount)
			return false;
		if (s_bWin7Style && s_MenuMode!=MODE_PROGRAMS && index==m_ProgramTreeIndex)
			return false;
	}
	const MenuItem &item=m_Items[index];
	if (!m_bSubMenu && s_MenuMode==MODE_SEARCH && index<m_OriginalCount && item.id!=MENU_SEARCH_BOX && item.id!=MENU_SHUTDOWN_BUTTON && item.id!=MENU_MORE_RESULTS && item.id!=MENU_SEARCH_INTERNET && item.id!=MENU_SEARCH_PROVIDER)
		return false;
	if (item.id==MENU_SEPARATOR || item.id==MENU_SEARCH_EMPTY)
		return false;
	if (item.itemRect.bottom==item.itemRect.top)
		return false;
	if (bKeyboard && item.id==MENU_SEARCH_BOX && GetSettingInt(L"SearchBox")!=SEARCHBOX_NORMAL)
		return false;
	return true;
}

LRESULT CMenuContainer::OnKeyDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	ShowKeyboardCues((HIWORD(lParam)&KF_ALTDOWN)!=0);
	bool bOldOverride=s_bOverrideFirstDown;
	s_bOverrideFirstDown=false;

	if (wParam==VK_TAB)
	{
		if (s_bWin7Style && m_bTwoColumns)
		{
			if (s_MenuMode==MODE_SEARCH)
				return 0; // no tabbing in search because the search box keeps the focus at all times
			// tab order - 0 - left column, 1 - all programs, 2 - search, 3 - right column, 4 - shutdown
			int tab=1; // so if nothing is selected the first tab goes to the search box
			if (m_SearchBox.m_hWnd && GetFocus()==m_SearchBox.m_hWnd)
				tab=2;
			else if (m_pProgramsTree && m_pProgramsTree->m_hWnd && m_pProgramsTree->m_hWnd==GetFocus())
				tab=0;
			else if (m_HotItem>=0)
			{
				const MenuItem &item=m_Items[m_HotItem];
				if (item.id==MENU_PROGRAMS)
					tab=1;
				else if (item.id==MENU_SEARCH_BOX)
					tab=2;
				else if (item.id==MENU_SHUTDOWN_BUTTON)
					tab=4;
				else
					tab=(item.column==0?0:3);
			}
			if (GetKeyState(VK_SHIFT)<0)
			{
				tab=(tab+4)%5;
				if (tab==2 && m_SearchIndex==-1)
					tab=1;
				if (tab==4 && GetSettingInt(L"ShutdownCommand")==SHUTDOWN_TYPE_NONE)
					tab=3;
			}
			else
			{
				tab=(tab+1)%5;
				if (tab==2 && m_SearchIndex==-1)
					tab=3;
				if (tab==4 && GetSettingInt(L"ShutdownCommand")==SHUTDOWN_TYPE_NONE)
					tab=0;
			}
			int index=-1;
			if (tab==0 || tab==3)
			{
				int column=(tab==0)?0:1;
				int miny=32768;
				for (int i=0;i<(int)m_Items.size();i++)
					if (m_Items[i].column==column && m_Items[i].itemRect.top<miny && CanSelectItem(i))
					{
						miny=m_Items[i].itemRect.top;
						index=i;
					}
			}
			else if (tab==1)
				index=m_ProgramButtonIndex;
			else if (tab==2)
				index=m_SearchIndex;
			else if (tab==4)
			{
				for (int i=0;i<(int)m_Items.size();i++)
					if (m_Items[i].id==MENU_SHUTDOWN_BUTTON)
					{
						index=i;
						break;
					}
			}
			if (index>=0)
			{
				CloseSubMenus(CLOSE_KEEP_MODE,this);
				ActivateItem(index,ACTIVATE_SELECT,NULL);
			}
			return 0;
		}
		else if (m_SearchBox.m_hWnd && m_SearchState==SEARCH_NONE)
		{
			// destroy old submenus
			CloseSubMenus(0,this);
			ActivateItem(m_SearchIndex,ACTIVATE_SELECT,NULL);
			return 0;
		}
	}

	int index=m_HotItem;

	if (index>=0 && m_SearchState==SEARCH_NONE)
	{
		if (wParam==VK_F2)
		{
			if (m_Items[index].id==MENU_NO && m_Items[index].pItem1 && !m_Items[index].pItem2)
			{
				ActivateItem(index,ACTIVATE_RENAME,NULL);
				if (IsWindow()) PostMessage(MCM_SETHOTITEM,index);
			}
			return 0;
		}
		if (wParam==VK_DELETE)
		{
			if ((m_Items[index].id==MENU_NO || m_Items[index].id==MENU_RECENT) && m_Items[index].pItem1 && !m_Items[index].pItem2)
			{
				ActivateItem(index,ACTIVATE_DELETE,NULL);
				if (IsWindow()) PostMessage(MCM_SETHOTITEM,index);
			}
			return 0;
		}
	}
	if (!m_bSubMenu && m_SearchIndex>=0 && wParam=='F' && GetKeyState(VK_CONTROL)<0)
	{
		ActivateItem(m_SearchIndex,ACTIVATE_SELECT,NULL);
		return 0;
	}

	if (wParam==VK_HOME || wParam==VK_END)
	{
		// select first or last in the given column
		int column=-1;
		if (s_bWin7Style && m_bTwoColumns && m_HotItem>=0)
			column=m_Items[m_HotItem].column;
		int index=-1;
		for (int i=0;i<(int)m_Items.size();i++)
		{
			const MenuItem &item=m_Items[i];
			if ((column==-1 || item.column==column) && CanSelectItem(i) && item.id!=MENU_SEARCH_BOX)
			{
				if (s_bWin7Style && m_bTwoColumns && (item.id==MENU_PROGRAMS || item.id==MENU_MORE_RESULTS || item.id==MENU_SEARCH_INTERNET || item.id==MENU_SEARCH_PROVIDER || item.id==MENU_SHUTDOWN_BUTTON))
					continue;
				index=i;
				if (wParam==VK_HOME) break;
			}
		}
		if (index>=0)
		{
			CloseSubMenus(CLOSE_KEEP_MODE,this);
			ActivateItem(index,ACTIVATE_SELECT,NULL);
		}
		return 0;
	}

	if (wParam==VK_PRIOR || wParam==VK_NEXT)
	{
		int index=m_HotItem<0?0:m_HotItem;
		if (m_ScrollHeight>0)
		{
			// scrolling menu
			if (index>=m_ScrollCount) return 0;
			int count=m_ScrollHeight/s_Skin.ItemSettings[m_bSubMenu?MenuSkin::SUBMENU_ITEM:MenuSkin::COLUMN1_ITEM].itemHeight-1;
			int newIndex=index;
			for (int i=0;i<count;i++)
			{
				if (wParam==VK_PRIOR)
				{
					newIndex--;
					if (newIndex<0) break;
				}
				else
				{
					newIndex++;
					if (newIndex>=m_ScrollCount) break;
				}
				if (CanSelectItem(newIndex))
					index=newIndex;
			}
		}
		else if (m_SearchScrollCount>m_SearchScrollHeight)
		{
			int index0=index;
			if (wParam==VK_PRIOR)
			{
				if (index<m_OriginalCount)
					index=m_OriginalCount;
				else
				{
					index-=m_SearchScrollHeight;
					if (index<m_OriginalCount) index=m_OriginalCount;
				}
			}
			else
			{
				if (index<m_OriginalCount)
					index=m_OriginalCount;
				else
				{
					int last=(int)m_Items.size()-1;
					index+=m_SearchScrollHeight;
					if (index>last) index=last;
				}
			}
			if (index0==index) return 0;
			if (m_Items[index].pItemInfo)
			{
				CString path;
				{
					CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
					path=m_Items[index].pItemInfo->GetPath();
				}
				if (!path.IsEmpty())
					UpdateAutoComplete(path);
			}
		}
		else
		{
			// multiple columns
			if (!m_bSubMenu) return 0;
			if (wParam==VK_PRIOR)
			{
				int column=m_Items[index].column;
				if (index>0 && m_Items.rbegin()->column>0 && m_Items[index-1].column!=column)
					column--;
				for (int i=0;i<index;i++)
					if (CanSelectItem(i) && m_Items[i].column==column)
					{
						index=i;
						break;
					}
			}
			else
			{
				int column=m_Items[index].column;
				if (index<(int)m_Items.size()-1 && m_Items.rbegin()->column>0 && m_Items[index+1].column!=column)
					column++;
				for (int i=index;i<(int)m_Items.size();i++)
					if (CanSelectItem(i) && m_Items[i].column==column)
						index=i;
			}
		}
		if (index>=0)
		{
			CloseSubMenus(0,this);
			ActivateItem(index,ACTIVATE_SELECT,NULL);
		}
	}

	if (wParam!=VK_UP && wParam!=VK_DOWN && wParam!=VK_LEFT && wParam!=VK_RIGHT && wParam!=VK_ESCAPE && wParam!=VK_RETURN)
		return TRUE;

	if (index<0 && m_SearchState!=SEARCH_NONE)
		index=m_SearchIndex;
	if (index<0) index=-1;
	bool bProgramsTree=(!m_bSubMenu && s_MenuMode==MODE_PROGRAMS && GetFocus()==m_pProgramsTree->m_hWnd);
	if (bProgramsTree)
		index=m_ProgramTreeIndex;

	int n=(int)m_Items.size();

	if (wParam==VK_UP)
	{
		// previous item
		int best=-1;
		if (index<0)
		{
			// no item is selected - find the first selectable item in the last column then go up
			int col=(m_bTwoColumns?1:0);
			index=0;
			for (int i=0;i<n;i++)
			{
				const MenuItem &item=m_Items[i];
				if (item.column==col && CanSelectItem(i) && (!item.bInline || item.bInlineFirst) && (s_MenuMode!=MODE_SEARCH || item.id!=MENU_SHUTDOWN_BUTTON))
				{
					index=i;
					break;
				}
			}
			best=index;
		}
		if (m_SearchScrollCount>0 && index==m_SearchIndex-m_SearchItemCount+1)
		{
			best=m_OriginalCount+m_SearchScrollCount-1;
		}
		else if (m_SearchScrollCount>0 && index>m_OriginalCount)
		{
			best=index-1;
		}
		else
		{
			int col=m_Items[index].column;
			int x0=m_Items[index].itemRect.left;
			int y0=m_Items[index].itemRect.top;
			int scrollOffset=0;
			if (m_ScrollCount>0)
				scrollOffset=m_Items[m_ScrollCount-1].itemRect.bottom-m_rContent.top-m_ScrollHeight;
			if (index<m_ScrollCount)
				y0-=scrollOffset;
			int dist=0x7FFFFFFF;
			for (int i=1;i<n;i++)
			{
				int idx=(index+n-i)%n;
				if (!CanSelectItem(idx))
					continue;
				const MenuItem &item=m_Items[idx];
				int d=0x7FFFFFFE;
				int bottom=item.itemRect.bottom;
				if (idx<m_ScrollCount)
					bottom-=scrollOffset;
				if (!(m_Options&CONTAINER_SEARCH))
				{
					if (m_SearchScrollCount>0 && i>=m_OriginalCount)
						continue;
					if (item.column==col && bottom<=y0)
					{
						d=((y0-bottom)<<16)+abs(item.itemRect.left-x0);
					}
					else if (item.bInline && !item.bInlineFirst)
						continue;
					else if (s_MenuMode==MODE_SEARCH && item.id==MENU_SHUTDOWN_BUTTON)
						continue;
				}
				if (d<dist)
				{
					dist=d;
					best=idx;
				}
			}
		}
		if (best>=0)
		{
			ActivateItem(best,ACTIVATE_SELECT,NULL);
			if ((m_Items[best].categoryHash&CSearchManager::CATEGORY_MASK)==CSearchManager::CATEGORY_AUTOCOMPLETE && m_Items[best].pItemInfo)
			{
				CString path;
				{
					CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
					path=m_Items[best].pItemInfo->GetPath();
				}
				if (!path.IsEmpty())
				{
					if (m_Options&CONTAINER_SEARCH)
						m_pParent->UpdateAutoComplete(path);
					else
						UpdateAutoComplete(path);
				}
			}
			if (!m_bSubMenu && s_MenuMode==MODE_PROGRAMS && best==m_ProgramTreeIndex)
				m_pProgramsTree->SelectLast();
		}
	}
	if (wParam==VK_DOWN)
	{
		// next item
		if (bOldOverride)
			index=-1;
		int best=-1;
		if (index<0)
		{
			// no item is selected - find the last selectable item then go down
			index=0;
			for (int i=n-1;i>=0;i--)
			{
				const MenuItem &item=m_Items[i];
				if (CanSelectItem(i) && (!item.bInline || item.bInlineFirst) && (s_MenuMode!=MODE_SEARCH || item.id!=MENU_SHUTDOWN_BUTTON))
				{
					index=i;
					break;
				}
			}
			best=index;
		}
		if (m_SearchScrollCount>0 && index==m_SearchIndex)
		{
			best=m_OriginalCount;
		}
		else if (m_SearchScrollCount>0 && index>=m_OriginalCount && index<n-1)
		{
			best=index+1;
		}
		else
		{
			int col=m_Items[index].column;
			int x0=m_Items[index].itemRect.left;
			int y0=m_Items[index].itemRect.bottom;
			int scrollOffset=0;
			if (m_ScrollCount>0)
				scrollOffset=m_Items[m_ScrollCount-1].itemRect.bottom-m_rContent.top-m_ScrollHeight;
			if (index<m_ScrollCount)
				y0-=scrollOffset;
			int dist=0x7FFFFFFF;
			for (int i=1;i<n;i++)
			{
				int idx=(index+i)%n;
				if (!CanSelectItem(idx))
					continue;
				const MenuItem &item=m_Items[idx];
				int top=item.itemRect.top;
				if (idx<m_ScrollCount)
					top-=scrollOffset;
				int d=0x7FFFFFFE;
				if (!(m_Options&CONTAINER_SEARCH))
				{
					if (m_SearchScrollCount>0 && i>=m_OriginalCount)
						continue;
					if (item.column==col && top>=y0)
					{
						d=((top-y0)<<16)+abs(item.itemRect.left-x0);
					}
					else if (item.bInline && !item.bInlineFirst)
						continue;
					else if (s_MenuMode==MODE_SEARCH && item.id==MENU_SHUTDOWN_BUTTON)
						continue;
				}
				if (d<dist)
				{
					dist=d;
					best=idx;
				}
			}
		}
		if (best>=0)
		{
			ActivateItem(best,ACTIVATE_SELECT,NULL);
			if ((m_Items[best].categoryHash&CSearchManager::CATEGORY_MASK)==CSearchManager::CATEGORY_AUTOCOMPLETE && m_Items[best].pItemInfo)
			{
				CString path;
				{
					CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
					path=m_Items[best].pItemInfo->GetPath();
				}
				if (!path.IsEmpty())
				{
					if (m_Options&CONTAINER_SEARCH)
						m_pParent->UpdateAutoComplete(path);
					else
						UpdateAutoComplete(path);
				}
			}
			if (!m_bSubMenu && s_MenuMode==MODE_PROGRAMS && best==m_ProgramTreeIndex)
				m_pProgramsTree->SelectFirst();
		}
	}
	if (wParam==VK_ESCAPE && s_MenuMode==MODE_JUMPLIST)
	{
		SetMenuMode(MODE_NORMAL,true);
		return 0;
	}
	bool bBack=((wParam==VK_LEFT && !s_bRTL) || (wParam==VK_RIGHT && s_bRTL));
	if (wParam==VK_ESCAPE || (bBack && GetKeyState(VK_CONTROL)>=0 && (s_Menus.size()>1 || (s_Menus.size()==1 && m_bSubMenu))))
	{
		// close top menu
		if (!s_Menus[s_Menus.size()-1]->m_bDestroyed)
			s_Menus[s_Menus.size()-1]->PostMessage(WM_CLOSE);
		if (s_Menus.size()>=2 && !s_Menus[s_Menus.size()-2]->m_bDestroyed)
			s_Menus[s_Menus.size()-2]->SetActiveWindow();
		if (s_Menus.size()==1)
		{
			if (m_bSubMenu)
			{
				::SetFocus(g_ProgramsButton);
			}
			else
			{
				// HACK: stops the call to SetActiveWindow(NULL). The correct behavior is to not close the taskbar when Esc is pressed
				s_TaskbarState&=~ABS_AUTOHIDE;
			}
		}
	}
	else if (bBack && index>=0)
	{
		if (m_Items[index].bInline && !m_Items[index].bInlineFirst)
		{
			index--;
			while (!CanSelectItem(index))
				index--;
			if (index>=0)
				ActivateItem(index,ACTIVATE_SELECT,NULL);
		}
		else
		{
			if (s_MenuMode==MODE_JUMPLIST && m_Items[index].jumpIndex>=0)
			{
				SetMenuMode(MODE_NORMAL,true);
				return 0;
			}
			int column=(m_Items[index].column+(int)m_ColumnOffsets.size()-1)%(int)m_ColumnOffsets.size();
			int y0=(m_Items[index].itemRect.top+m_Items[index].itemRect.bottom)/2;
			if (index<m_ScrollCount) y0-=m_ScrollOffset;
			if (m_bTwoColumns && bProgramsTree)
			{
				HTREEITEM hItem=TreeView_GetSelection(m_pProgramsTree->m_hWnd);
				RECT rc={0};
				if (hItem)
					TreeView_GetItemRect(m_pProgramsTree->m_hWnd,hItem,&rc,TRUE);
				m_pProgramsTree->MapWindowPoints(m_hWnd,&rc);
				y0=(rc.top+rc.bottom)/2;
			}
			int dist=INT_MAX;
			index=-1;
			for (int i=0;i<n;i++)
			{
				if (m_Items[i].column==column && CanSelectItem(i))
				{
					if (m_Items[i].bInline && !m_Items[i].bInlineLast)
						continue;
					int ytop=m_Items[i].itemRect.top+1, ybottom=m_Items[i].itemRect.bottom-1;
					if (i<m_ScrollCount)
					{
						ytop-=m_ScrollOffset;
						ybottom-=m_ScrollOffset;
					}
					int d=min(abs(ytop-y0),abs(ybottom-y0));
					if (dist>d)
					{
						index=i;
						dist=d;
					}
				}
			}
			if (index>=0)
			{
				ActivateItem(index,ACTIVATE_SELECT,NULL);
				if (s_MenuMode==MODE_PROGRAMS && index==m_ProgramTreeIndex)
				{
					POINT pt={0,y0};
					MapWindowPoints(m_pProgramsTree->m_hWnd,&pt,1);
					m_pProgramsTree->SelectItem(pt.y);
				}
			}
		}
	}

	bool bForward=((wParam==VK_RIGHT && !s_bRTL) || (wParam==VK_LEFT && s_bRTL));
	if (wParam==VK_RETURN || bForward)
	{
		// open submenu
		if (index>=0)
		{
			if (m_Items[index].bFolder && (bForward || !m_Items[index].bSplit) && (wParam==VK_RETURN || GetKeyState(VK_CONTROL)>=0))
				ActivateItem(index,ACTIVATE_OPEN_KBD,NULL);
			else if (wParam==VK_RETURN)
				ActivateItem(index,ACTIVATE_EXECUTE,NULL);
			else if (bForward)
			{
				if (m_Items[index].bInline && !m_Items[index].bInlineLast)
				{
					index++;
					while (!CanSelectItem(index))
						index++;
					if (index>=0)
						ActivateItem(index,ACTIVATE_SELECT,NULL);
				}
				else
				{
					int column=(m_Items[index].column+1)%(int)m_ColumnOffsets.size();
					int y0=(m_Items[index].itemRect.top+m_Items[index].itemRect.bottom)/2;
					if (index<m_ScrollCount) y0-=m_ScrollOffset;
					if (m_bTwoColumns && bProgramsTree)
					{
						HTREEITEM hItem=TreeView_GetSelection(m_pProgramsTree->m_hWnd);
						RECT rc={0};
						if (hItem)
							TreeView_GetItemRect(m_pProgramsTree->m_hWnd,hItem,&rc,TRUE);
						m_pProgramsTree->MapWindowPoints(m_hWnd,&rc);
						y0=(rc.top+rc.bottom)/2;
					}
					int dist=INT_MAX;
					index=-1;
					for (int i=0;i<n;i++)
					{
						if (m_Items[i].column==column && CanSelectItem(i) && (!m_Items[i].bInline || m_Items[i].bInlineFirst))
						{
							int ytop=m_Items[i].itemRect.top+1, ybottom=m_Items[i].itemRect.bottom-1;
							if (i<m_ScrollCount)
							{
								ytop-=m_ScrollOffset;
								ybottom-=m_ScrollOffset;
							}
							int d=min(abs(ytop-y0),abs(ybottom-y0));
							if (dist>d)
							{
								index=i;
								dist=d;
							}
						}
					}
					if (index>=0)
					{
						ActivateItem(index,ACTIVATE_SELECT,NULL);
						if (s_MenuMode==MODE_PROGRAMS && index==m_ProgramTreeIndex)
						{
							POINT pt={y0,0};
							MapWindowPoints(m_pProgramsTree->m_hWnd,&pt,1);
							m_pProgramsTree->SelectItem(pt.y);
						}
					}
				}
			}
		}
		else if (bForward)
		{
			for (int i=n-1;i>=0;i--)
			{
				if (CanSelectItem(i) && (!m_Items[i].bInline || m_Items[i].bInlineFirst))
				{
					ActivateItem(i,ACTIVATE_SELECT,NULL);
					break;
				}
			}
		}
	}
	return 0;
}

LRESULT CMenuContainer::OnSysKeyDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==VK_RETURN && m_HotItem>=0)
	{
		int index=m_HotItem;
		if (m_Items[index].pItem1 && !m_Items[index].pItem2)
		{
			POINT pt={0,0};
			ActivateItem(index,ACTIVATE_PROPERTIES,&pt);
			PostMessage(MCM_SETHOTITEM,index);
		}
	}
	else
		bHandled=FALSE;
	return 0;
}

LRESULT CMenuContainer::OnChar( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (!GetSettingBool(L"EnableAccelerators"))
		return TRUE;

	if (GetSettingBool(L"AltAccelerators") && !(HIWORD(lParam) & KF_ALTDOWN))
		return TRUE;

	if (wParam>=0xD800 && wParam<=0xDBFF)
		return TRUE; // don't support supplementary characters

	// find the current menu item
	int index=m_HotItem;
	if (index<0) index=-1;

	// find the next item with that accelerator
	wchar_t buf[2]={(wchar_t)wParam,0};
	CharUpper(buf);

	int n=(int)m_Items.size();

	int first=-1, count=0, firstCustom=-1, countCustom=0;
	for (int i=1;i<=n;i++)
	{
		int idx=(index+2*n+i)%n;
		if (m_Items[idx].accelerator==buf[0] && CanSelectItem(idx))
		{
			if (first==-1)
				first=idx;
			count++;
			if (m_Items[idx].bCustomAccelerator)
			{
				if (firstCustom==-1)
					firstCustom=idx;
				countCustom++;
			}
		}
	}

	if (count==0)
		return TRUE; // no item was found

	if (countCustom>1 || (countCustom==0 && count>1))
	{
		// multiple items have the same accelerator. select the next one
		ActivateItem(first,ACTIVATE_SELECT,NULL);
		return 0;
	}

	if (countCustom==1)
		first=firstCustom;

	// exactly 1 item has that accelerator
	if (m_Items[first].bHasJumpList && GetSettingInt(L"JumplistKeys")==0)
	{
		ActivateItem(first,ACTIVATE_SELECT,NULL);
		return 0;
	}
	ActivateData data;
	data.bNoModifiers=true;
	if (!m_Items[first].bFolder || (!m_Items[first].bHasJumpList && m_Items[first].bSplit) || (m_Items[first].bHasJumpList && GetSettingInt(L"JumplistKeys")==1))
	{
		ActivateItem(first,ACTIVATE_EXECUTE,NULL,&data);
		return 0;
	}

	// m_Items[first].bFolder
	ActivateItem(first,ACTIVATE_OPEN_KBD,NULL,&data);

	return 0;
}

LRESULT CMenuContainer::OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	LOG_MENU(LOG_OPEN,L"Close Menu, ptr=%p",this);
	if (m_pAccessible)
	{
		NotifyWinEvent(EVENT_SYSTEM_MENUPOPUPEND,m_hWnd,OBJID_CLIENT,CHILDID_SELF);
		m_pAccessibleContext->ContextCallback(ReleaseAccessible,NULL,IID_IAccessible,4,NULL);
		m_pAccessibleContext=NULL;
		m_pAccessible=NULL;
	}
	if (m_pDropTargetHelper && m_pDragObject)
	{
		m_pDropTargetHelper->DragLeave();
	}
	m_pDragObject=NULL;
	m_pDropTargetHelper=NULL;
	m_pDropTargetProxy->Reset();
	m_pDropTargetProxy=NULL;
	RevokeDragDrop(m_hWnd);
	// remember the scroll position
	if (m_ScrollHeight>0 && m_FolderHash[0])
		s_MenuScrolls[m_FolderHash[0]]=m_ScrollOffset;
	else
		s_MenuScrolls.erase(m_FolderHash[0]);

	if (s_pHotMenu==this)
	{
		s_pHotMenu=NULL;
		s_HotItem=-1;
	}
	if (s_pTipMenu==this)
	{
		s_pTipMenu=NULL;
		TOOLINFO tool={sizeof(tool),TTF_ABSOLUTE|TTF_TRACK|TTF_TRANSPARENT};
		tool.uId=1;
		s_Tooltip.SendMessage(TTM_TRACKACTIVATE,FALSE,(LPARAM)&tool);
	}
	if ((m_Options&CONTAINER_SEARCH) && !m_pParent->m_bDestroyed && !m_pParent->m_bInSearchUpdate)
	{
		m_pParent->m_SearchBox.SetWindowText(L"");
	}
	m_bDestroyed=true;
	if (this==s_Menus[0])
	{
		// cleanup when the last menu is closed
		if (s_Theme)
			CloseThemeData(s_Theme);
		s_Theme=NULL;
		if (s_PagerTheme)
			CloseThemeData(s_PagerTheme);
		s_PagerTheme=NULL;
		if (s_Tooltip.m_hWnd)
			s_Tooltip.DestroyWindow();
		s_Tooltip.m_hWnd=NULL;
		s_TooltipBalloon.m_hWnd=NULL; // the balloon tooltip is owned, no need to be destroyed
		if (s_UserPicture.m_hWnd)
			s_UserPicture.DestroyWindow();
		s_UserPicture.m_hWnd=NULL;
		s_pHotMenu=NULL;
		s_HotItem=-1;
		if (!m_bSubMenu)
			EnableStartTooltip(true);
		BufferedPaintUnInit();
		if (!m_bSubMenu && (s_TaskbarState&ABS_AUTOHIDE))
		{
			HWND capture=GetCapture();
			if (!capture || !(capture==s_TaskBar || ::IsChild(s_TaskBar,capture)))
				::SetActiveWindow(NULL); // close the taskbar if it is auto-hide and doesn't have the mouse capture
		}
		if (s_XMouse)
			SystemParametersInfo(SPI_SETACTIVEWINDOWTRACKING,NULL,(PVOID)TRUE,SPIF_SENDCHANGE);
		g_SearchManager.CloseMenu();
		if (m_pProgramsTree && m_pProgramsTree->m_hWnd && s_MenuMode==MODE_PROGRAMS)
			s_ProgramsScrollPos=m_pProgramsTree->GetScrollPos(SB_VERT);
		s_bAllPrograms=false;
		if ((m_Options&CONTAINER_ALLPROGRAMS) && g_TopWin7Menu && ::IsWindowVisible(g_TopWin7Menu))
		{
			::ShowWindow(g_UserPic,SW_SHOW);
			::SetFocus(g_ProgramsButton);
			CPoint pt(GetMessagePos());
			RECT rc;
			::GetWindowRect(g_TopWin7Menu,&rc);
			if (PtInRect(&rc,pt))
			{
				::ScreenToClient(g_TopWin7Menu,&pt);
				::PostMessage(g_TopWin7Menu,WM_MOUSEMOVE,0,MAKELONG(pt.x,pt.y));
			}
		}
		s_FirstMenu=NULL;
		if (!(m_Options&CONTAINER_ALLPROGRAMS))
		{
			g_CurrentCSMTaskbar=-1;
			PressStartButton(s_TaskBarId,false);
		}
		s_TaskBar=s_StartButton=NULL;
		s_TaskBarId=-1;
		g_ItemManager.SaveCacheFile();
		if (s_ArrowsBitmap) DeleteObject(s_ArrowsBitmap);
		s_ArrowsBitmap=NULL;
		CloseLog();
	}
	if (m_Options&CONTAINER_SEARCH)
		s_SearchMenu=NULL;
	if (m_ScrollTheme)
	{
		CloseThemeData(m_ScrollTheme);
		m_ScrollTheme=NULL;
	}

    if (s_pFrameworkInputPane && m_InputCookie)
        s_pFrameworkInputPane->Unadvise(m_InputCookie);

	return 0;
}

LRESULT CMenuContainer::OnShowWindow( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (!wParam && !m_bSubMenu && s_UserPicture)
		s_UserPicture.ShowWindow(SW_HIDE);
	bHandled=FALSE;
	return 0;
}

LRESULT CMenuContainer::OnRefresh( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	m_RefreshPosted=0;
	if ((m_Options&CONTAINER_SEARCH) || (!m_bSubMenu && s_MenuMode==MODE_SEARCH))
	{
		// update search results
		unsigned int hash=0;
		int index=m_ContextItem==-1?m_HotItem:m_ContextItem;
		if (index>=0)
		{
			if (index>=m_OriginalCount && index<(int)m_Items.size())
				hash=m_Items[index].nameHash;
			else if (s_bWin7Style && index>m_SearchIndex-m_SearchItemCount && index<=m_SearchIndex && m_Items[index].id!=MENU_SEARCH_EMPTY)
				hash=m_SearchIndex-index+1;
		}
		bool bSearching=InitSearchItems();
		InitWindow(bSearching && s_MenuMode!=MODE_SEARCH);
		Invalidate();
		int hotItem=-1;
		if (s_bWin7Style && hash==1)
			hotItem=m_SearchIndex;
		else if (s_bWin7Style && hash==2)
			hotItem=m_SearchIndex-1;
		else if (s_bWin7Style && hash==3)
			hotItem=m_SearchIndex-2;
		else
		{
			for (int i=m_OriginalCount;i<(int)m_Items.size();i++)
			{
				if (hash && m_Items[i].nameHash==hash)
				{
					hotItem=i;
					break;
				}
			}
		}
		if (m_ContextItem!=-1)
			SetContextItem(hotItem);
		if (hotItem==-1 && m_OriginalCount<(int)m_Items.size())
		{
			if (s_SearchResults.autoCompletePath.IsEmpty() && wcsncmp(s_SearchResults.currentString,L"\\\\",2)!=0)
			{
				if (m_Items[m_OriginalCount].id==MENU_SEARCH_EMPTY)
				{
					if (!bSearching)
					{
						for (int i=0;i<(int)m_Items.size();i++)
						{
							if (m_Items[i].id==MENU_MORE_RESULTS)
							{
								hotItem=i;
								break;
							}
							else if (m_Items[i].id==MENU_SEARCH_INTERNET || m_Items[i].id==MENU_SEARCH_PROVIDER)
							{
								hotItem=i;
								break;
							}
						}
					}
				}
				else if (m_Items[m_OriginalCount].id==MENU_SEARCH_CATEGORY)
					hotItem=m_OriginalCount+1;
			}
			else
				hotItem=-1;
		}
		if (hotItem>=0)
		{
			ActivateItem(hotItem,ACTIVATE_SELECT,NULL);
			if (s_bPendingSearchEnter)
			{
				ActivateItem(hotItem,ACTIVATE_EXECUTE,NULL);
				s_bPendingSearchEnter=false;
			}
		}
		else
			SetHotItem(-1);
	}
	else if (s_MenuMode==MODE_JUMPLIST)
	{
		OpenJumpList(m_SubJumpItem,false);
	}
	else
	{
		// updates the menu after drag/drop, delete, or rename operation
		for (std::vector<CMenuContainer*>::reverse_iterator it=s_Menus.rbegin();*it!=this;++it)
			if (!(*it)->m_bDestroyed)
				(*it)->PostMessage(WM_CLOSE);
		if (m_ScrollHeight>0 && m_FolderHash[0])
			s_MenuScrolls[m_FolderHash[0]]=m_ScrollOffset;
		else
			s_MenuScrolls.erase(m_FolderHash[0]);
		if (!m_bSubMenu && !s_bWin7Style)
			s_MenuMaxHeight[0]=-1;
		InitItems();
		InitWindow();
		Invalidate();
		SetFocus();
		SetHotItem(-1);
	}
	return 0;
}

LRESULT CMenuContainer::OnActivate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (LOWORD(wParam)!=WA_INACTIVE)
	{
		if (s_Tooltip.m_hWnd)
			s_Tooltip.SetWindowPos(HWND_TOP,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE);
		return 0;
	}
#ifndef PREVENT_CLOSING
	if (s_bPreventClosing)
		return 0;

	if (lParam)
	{
		// check if another menu window is being activated
		// if not, close all menus
		for (std::vector<CMenuContainer*>::const_iterator it=s_Menus.begin();it!=s_Menus.end();++it)
			if ((*it)->m_hWnd==(HWND)lParam || (*it)->m_SearchBox.m_hWnd==(HWND)lParam)
				return 0;

		if ((HWND)lParam==g_OwnerWindow || (HWND)lParam==g_TopWin7Menu)
			return 0;
	}

	for (std::vector<CMenuContainer*>::reverse_iterator it=s_Menus.rbegin();it!=s_Menus.rend();++it)
		if ((*it)->m_hWnd && !(*it)->m_bDestroyed)
		{
			(*it)->PostMessage(WM_CLOSE);
			(*it)->m_bClosing=true;
		}
	if (g_TopWin7Menu && s_bAllPrograms) ::PostMessage(g_TopWin7Menu,WM_CLOSE,0,0);
#endif

	return 0;
}

LRESULT CMenuContainer::OnMouseActivate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_Submenu>=0 || (m_Options&CONTAINER_SEARCH))
		return MA_NOACTIVATE;
	bHandled=FALSE;
	return 0;
}

LRESULT CMenuContainer::OnMouseMove( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (!m_bTrackMouse)
	{
		TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,m_hWnd,0};
		TrackMouseEvent(&track);
		m_bTrackMouse=true;
	}
	if (!(wParam&MK_LBUTTON) && m_ClickIndex==-2)
		SetClickItem(-1);
	if (s_HotPos==GetMessagePos())
		return 0; // HACK - ignore the mouse if it hasn't moved since last time. otherwise the mouse can override the keyboard navigation
	s_HotPos=GetMessagePos();
	POINT pt={(short)LOWORD(lParam),(short)HIWORD(lParam)};
	bool bArrow=false;
	int index=HitTest(pt,&bArrow);
	if (index!=m_ProgramButtonIndex)
		m_bDisableProgHover=false;
	if (GetCapture()==m_hWnd)
	{
		if (m_ClickIndex!=index)
		{
			if (!DragOut(m_ClickIndex,(m_Options&CONTAINER_APPS)!=0))
				SetHotItem(-2);
		}
		else
			SetHotItem(index,bArrow,true);
	}
	else
	{
		if (index>=0 && m_Items[index].id==MENU_SEPARATOR)
			index=m_HotItem;
		SetHotItem(index,bArrow,true);

		UpdateScroll(&pt,false);

		if (m_Submenu<0 && !(m_Options&CONTAINER_SEARCH) && m_SearchState==SEARCH_NONE)
			SetFocus();
		if (index>=0)
		{
			if ((m_Submenu>=0 && index!=m_Submenu) || (m_Submenu<0 && m_Items[index].bFolder) || (s_MenuMode==MODE_JUMPLIST && m_SubJumpItem>=0 && index!=m_SubJumpItem && index<m_OriginalCount))
			{
				// initialize the hover timer
				if ((m_HoverItem!=index || m_bHoverArrow!=bArrow) && !s_bDisableHover && m_SearchState<=SEARCH_BLANK)
				{
					m_HoverItem=index;
					m_bHoverArrow=bArrow;
					if (m_Items[m_HotItem].id==MENU_SHUTDOWN_BUTTON && !bArrow)
						KillTimer(TIMER_HOVER);
					else
					{
						int time=s_HoverTime;
						if (!bArrow && m_Items[m_HotItem].bSplit)
							time=s_SplitHoverTime;
						else if (s_bWin7Style && index==m_ProgramButtonIndex && GetSettingInt(L"ProgramsStyle")==PROGRAMS_INLINE)
							time=m_bDisableProgHover?-1:s_ProgramsHoverTime;
						if (time>=0)
							SetTimer(TIMER_HOVER,time);
					}
					LOG_MENU(LOG_MOUSE,L"Start Hover, index=%d",index);
				}
			}
			else
				m_HoverItem=-1;
		}
		else
			m_HoverItem=-1;
	}

	return 0;
}

LRESULT CMenuContainer::OnMouseLeave( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_bTrackMouse) // HACK - somehow after a context menu WM_MOUSELEAVE comes even if we are not tracking the mouse
	{
		UpdateScroll(NULL,false);
		SetHotItem(-1);
		m_bTrackMouse=false;
		m_bDisableProgHover=false;
		if (m_HoverItem!=-1)
		{
			KillTimer(TIMER_HOVER);
			m_HoverItem=-1;
		}
	}
	return 0;
}

LRESULT CMenuContainer::OnMouseWheel( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	POINT pt={(short)LOWORD(lParam),(short)HIWORD(lParam)};
	HWND hwnd=WindowFromPoint(pt);
	if (hwnd!=m_hWnd)
	{
		for (std::vector<CMenuContainer*>::iterator it=s_Menus.begin();it!=s_Menus.end();++it)
		{
			if ((*it)->m_hWnd==hwnd)
			{
				(*it)->SendMessage(uMsg,wParam,lParam);
				return 0;
			}
		}
	}
	if (m_ScrollCount<1 && m_SearchScrollCount<1) return 0; // nothing to scroll
	UINT lines;
	if (!SystemParametersInfo(SPI_GETWHEELSCROLLLINES,0,&lines,FALSE))
		lines=3;
	if (lines<1) lines=1;

	m_MouseWheel+=lines*(short)HIWORD(wParam);
	int n=m_MouseWheel/WHEEL_DELTA;
	m_MouseWheel-=n*WHEEL_DELTA;
	if (m_SearchScrollCount>0)
	{
		int pos=m_SearchScrollPos;
		m_SearchScrollPos=m_Scrollbar.SetScrollPos(SB_CTL,m_SearchScrollPos-n);
		if (m_SearchScrollPos!=pos)
		{
			Invalidate();
			s_HotPos=-1;
			ScreenToClient(&pt);
			OnMouseMove(WM_MOUSEMOVE,LOWORD(wParam),MAKELONG(pt.x,pt.y),bHandled);
		}
	}
	else
	{
		int scroll=m_ScrollOffset;
		scroll-=n*s_Skin.ItemSettings[m_bSubMenu?MenuSkin::SUBMENU_ITEM:MenuSkin::COLUMN1_ITEM].itemHeight;
		if (scroll<0) scroll=0;
		int total=m_Items[m_ScrollCount-1].itemRect.bottom-m_rContent.top-m_ScrollHeight;
		if (scroll>total) scroll=total;
		if (m_ScrollOffset!=scroll)
		{
			m_ScrollOffset=scroll;
			UpdateScroll();
			Invalidate();
			s_HotPos=-1;
			ScreenToClient(&pt);
			OnMouseMove(WM_MOUSEMOVE,LOWORD(wParam),MAKELONG(pt.x,pt.y),bHandled);
		}
	}
	return 0;
}

bool CMenuContainer::GetDescription( int index, wchar_t *text, int size )
{
	if (index<0 || index>=(int)m_Items.size())
		return false;
	const MenuItem &item=m_Items[index];
	bool bLabel=false;
	if (item.bStartScreen)
		return false;
	if (item.bInline)
	{
		int len=0;
		for (const wchar_t *c=item.name;*c && len<size-1;c++)
			if (c[0]!='&' || c[1]=='&')
				text[len++]=*c;
		text[len]=0;
		bLabel=len>0;
		text+=len;
		size-=len;
	}
	if (item.id==MENU_PROGRAMS && s_MenuMode==MODE_PROGRAMS)
		return false;
	if (item.pStdItem && item.pStdItem->tip)
	{
		if (_wcsicmp(item.pStdItem->tip,L"none")==0)
			return false;
		// get the tip for the standard item
		if (bLabel)
			Sprintf(text,size,L"\r\n%s",item.pStdItem->tip);
		else
			Strcpy(text,size,item.pStdItem->tip);
		return true;
	}

	if (item.id==MENU_SEARCH_CATEGORY && item.categoryHash>=CSearchManager::CATEGORY_FILE && m_bHotArrow)
	{
		Sprintf(text,size,L"%s (Ctrl+Enter)",FindTranslation(L"Menu.MoreResults",L"See more results"));
		return true;
	}
	if (item.id==MENU_NO && item.categoryHash==CSearchManager::CATEGORY_METROSETTING)
	{
		// try to get the description from the XML file. looks like it is always the same as the display name though
		CComPtr<IXMLDOMDocument> pDoc;
		if (SUCCEEDED(pDoc.CoCreateInstance(L"Msxml2.FreeThreadedDOMDocument")))
		{
			pDoc->put_async(VARIANT_FALSE);
			VARIANT_BOOL loaded;
			if (pDoc->load(CComVariant(item.pItemInfo->PATH),&loaded)==S_OK && loaded==VARIANT_TRUE)
			{
				CComPtr<IXMLDOMNode> pDescription;
				HRESULT res=pDoc->selectSingleNode(CComBSTR(L"PCSettings/SearchableContent/SettingInformation/Description"),&pDescription);
				if (res==S_OK)
				{
					CComBSTR desc;
					if (pDescription->get_text(&desc)==S_OK)
					{
						if (SUCCEEDED(SHLoadIndirectString(desc,text,size,NULL)))
							return true;
					}
				}
			}
		}
	}
	if (item.jumpIndex>=0 && item.id!=MENU_SEPARATOR && item.id!=MENU_EMPTY)
	{
		const CJumpGroup &group=s_JumpList.groups[LOWORD(item.jumpIndex)];
		const CJumpItem &jumpItem=group.items[HIWORD(item.jumpIndex)];
		if (m_bHotArrow)
		{
			if (group.type==CJumpGroup::TYPE_PINNED)
				Strcpy(text,size,FindTranslation(L"Jumplist.UnpinTip",L"Unpin from this list"));
			else
				Strcpy(text,size,FindTranslation(L"Jumplist.PinTip",L"Pin to this list"));
			return true;
		}
		if (jumpItem.type==CJumpItem::TYPE_ITEM)
		{
			CComQIPtr<IShellItem> pItem(jumpItem.pItem);
			if (pItem)
			{
				{
					CComString pName;
					if (SUCCEEDED(pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEEDITING,&pName)))
					{
						Strcpy(text,size,pName);
						return true;
					}
				}
				CComPtr<IQueryInfo> pQueryInfo;
				if (SUCCEEDED(pItem->BindToHandler(NULL,BHID_SFUIObject,IID_IQueryInfo,(void**)&pQueryInfo)))
				{
					CComString pTip;
					if (FAILED(pQueryInfo->GetInfoTip(QITIPF_LINKNOTARGET,&pTip)) || !pTip)
						return false;

					Strcpy(text,size,pTip);
					return true;
				}
			}
		}
		else if (jumpItem.type==CJumpItem::TYPE_LINK)
		{
			CComQIPtr<IShellLink> pLink(jumpItem.pItem);
			if (pLink)
			{
				if (SUCCEEDED(pLink->GetDescription(text,size)) && text[0])
					return true;
				if (jumpItem.bHasArguments)
				{
					// don't use default tip for items with arguments
					Strcpy(text,size,item.name);
					return true;
				}
				if (pLink->GetPath(text,size,NULL,0)==S_OK)
					return true;
			}
		}
	}

	if ((item.categoryHash&CSearchManager::CATEGORY_MASK)==CSearchManager::CATEGORY_FILE)
	{
		// for search files show the path
		if (item.pItemInfo)
		{
			bool bShowPath;
			{
				CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
				bShowPath=!item.pItemInfo->GetPath().IsEmpty();
			}
			if (bShowPath)
			{
				CComPtr<IShellItem> pItem;
				if (SUCCEEDED(SHCreateItemFromIDList(item.pItemInfo->GetPidl(),IID_IShellItem,(void**)&pItem)))
				{
					CComString pName;
					if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH,&pName)))
					{
						Strcpy(text,size,pName);
						return true;
					}
				}
			}
		}
	}

	if (item.pItem1)
	{
		if (item.pItemInfo)
		{
			CString path;
			{
				CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
				path=item.pItemInfo->GetPath();
			}
			if (!path.IsEmpty() && PathIsNetworkPath(path))
			{
				Strcpy(text,size,path);
				return true;
			}
		}

		// get the tip from the shell
		CComPtr<IShellItem> pItem;
		if (FAILED(SHCreateItemFromIDList(item.pItem1,IID_IShellItem,(void**)&pItem)))
			return bLabel;

		CComPtr<IQueryInfo> pQueryInfo;
		if (FAILED(pItem->BindToHandler(NULL,BHID_SFUIObject,IID_IQueryInfo,(void**)&pQueryInfo)))
			return bLabel;

		CComString pTip;
		HRESULT hr=pQueryInfo->GetInfoTip(QITIPF_DEFAULT,&pTip);
		if (FAILED(hr) || !pTip)
			return bLabel;

		if (bLabel)
			Sprintf(text,size,L"\r\n%s",(const wchar_t*)pTip);
		else
			Strcpy(text,size,pTip);
		return true;
	}
	return bLabel;
}

LRESULT CMenuContainer::OnLButtonDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (!GetCapture())
	{
		if (m_Submenu<0 && !(m_Options&CONTAINER_SEARCH) && m_SearchState==SEARCH_NONE)
			SetFocus();
		POINT pt={(short)LOWORD(lParam),(short)HIWORD(lParam)};
		SetClickItem(-1);
		if (m_rUser1.left<m_rUser1.right && PtInRect(&m_rUser1,pt))
		{
			RunUserCommand(true);
		}
		if (m_rUser2.left<m_rUser2.right && PtInRect(&m_rUser2,pt))
		{
			RunUserCommand(false);
		}
		bool bArrow=false;
		int index=HitTest(pt,&bArrow);
		if (index<0)
		{
			if (m_Submenu>=0)
			{
				SetActiveWindow(); // must be done before the children are destroyed
				// close all child menus
				CloseSubMenus(0,this);
				SetHotItem(-1); // must be done after the children are destroyed
			}
			return 0;
		}
		const MenuItem &item=m_Items[index];
		if (item.id==MENU_SEPARATOR) return 0;
		if (index==m_ProgramButtonIndex && GetSettingInt(L"ProgramsStyle")==PROGRAMS_INLINE)
		{
			m_bDisableProgHover=true;
			KillTimer(TIMER_HOVER);
		}
		SetClickItem(index);
		m_bClickArrow=bArrow;
		SetCapture();
	}
	return 0;
}

LRESULT CMenuContainer::OnLButtonDblClick( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	SetClickItem(-1);
	// execute item under the mouse
	POINT pt={(short)LOWORD(lParam),(short)HIWORD(lParam)};
	bool bArrow;
	int index=HitTest(pt,&bArrow);
	if (index<0) return 0;
	const MenuItem &item=m_Items[index];
	if (item.id==MENU_SEPARATOR) return 0;
	ClientToScreen(&pt);
	if (s_bWin7Style && item.id==MENU_PROGRAMS) // only single clicks for All Programs
		OnLButtonDown(WM_LBUTTONDOWN,wParam,lParam,bHandled);
	else if (!bArrow && item.id!=MENU_APPS) // ignore double-click on the split arrow and Apps folder
		ActivateItem(index,ACTIVATE_EXECUTE,&pt);
	return 0;
}

LRESULT CMenuContainer::OnLButtonUp( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (GetCapture()==m_hWnd)
		ReleaseCapture();
	else if (m_ClickIndex!=-2)
		return 0;
	POINT pt={(short)LOWORD(lParam),(short)HIWORD(lParam)};
	bool bArrow=false;
	int index=HitTest(pt,&bArrow);
	if (m_ClickIndex!=-2 && (index!=m_ClickIndex || bArrow!=m_bClickArrow))
	{
		InvalidateItem(m_ClickIndex);
		SetHotItem(-1);
		return 0;
	}
	if (index<0) return 0;
	const MenuItem &item=m_Items[index];
	POINT pt2=pt;
	ClientToScreen(&pt2);
	if (!item.bFolder || (s_bSingleClickFolders && item.id!=MENU_PROGRAMS && item.id!=MENU_APPS && !bArrow)) // never open All Programs, Apps folder, or jumplists with single click
	{
		if (item.jumpIndex>=0 && m_bHotArrow)
		{
			const CJumpGroup &group=s_JumpList.groups[LOWORD(item.jumpIndex)];
			const CJumpItem &jumpItem=group.items[HIWORD(item.jumpIndex)];
			PinJumpItem(s_JumpAppInfo,s_JumpList,LOWORD(item.jumpIndex),HIWORD(item.jumpIndex),group.type!=CJumpGroup::TYPE_PINNED,-1);
			PostRefreshMessage();
		}
		else if (item.id==MENU_SEARCH_CATEGORY && item.categoryHash>=CSearchManager::CATEGORY_FILE && m_bHotArrow)
		{
			ActivateData data;
			data.bArrow=true;
			ActivateItem(index,ACTIVATE_EXECUTE,&pt2,&data);
		}
		else
			ActivateItem(index,ACTIVATE_EXECUTE,&pt2);
	}
	else
	{
		const MenuItem &item=m_Items[index];
		if (item.bSplit)
		{
			if (!bArrow)
			{
				ActivateItem(index,ACTIVATE_EXECUTE,&pt2);
				return 0;
			}
			if ((index==m_Submenu || index==m_SubJumpItem) && (!m_SubShowTime || (int)(GetTickCount()-m_SubShowTime)>500))
			{
				// second click on the arrow closes the menus
				SetActiveWindow();
				// destroy old submenus
				CloseSubMenus(0,this);
				SetHotItem(index,true,true);
				KillTimer(TIMER_HOVER);
				return 0;
			}
		}
		else if (s_bWin7Style && item.id==MENU_PROGRAMS && GetSettingInt(L"ProgramsStyle")==PROGRAMS_INLINE && m_SubShowTime && (int)(GetTickCount()-m_SubShowTime)<500)
			return 0; // ignore clicks soon after the programs open
		if (index!=m_Submenu)
		{
			ActivateItem(index,ACTIVATE_OPEN,NULL);
		}
	}
	return 0;
}

LRESULT CMenuContainer::OnRButtonDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (!GetCapture())
	{
		POINT pt={(short)LOWORD(lParam),(short)HIWORD(lParam)};
		SetClickItem(-1);
		int index=HitTest(pt,NULL);
		if (index>=0)
		{
			if (m_Items[index].id==MENU_SEPARATOR) return 0;
		}
		else if (!PtInRect(&m_rPadding,pt))
			return 0;
		SetClickItem(index);
		SetCapture();
	}
	return 0;
}

LRESULT CMenuContainer::OnRButtonUp( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (GetCapture()!=m_hWnd)
		return 0;
	ReleaseCapture();
	if (s_bNoContextMenu) return 0;
	POINT pt={(short)LOWORD(lParam),(short)HIWORD(lParam)};
	bool bArrow=false;
	int index=HitTest(pt,&bArrow);
	if (index>=0)
	{
		if (m_Items[index].id==MENU_SEPARATOR) return 0;
		if (m_Items[index].id!=MENU_SEARCH_CATEGORY || m_Items[index].categoryHash<CSearchManager::CATEGORY_FILE)
			bArrow=false;
	}
	else if (!PtInRect(&m_rPadding,pt))
		return 0;
	SetHotItem(index,bArrow,true);
	ClientToScreen(&pt);
	ActivateData data;
	data.bArrow=bArrow;
	ActivateItem(index,ACTIVATE_MENU,&pt,&data);
	return 0;
}

LRESULT CMenuContainer::OnSetCursor( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_rUser1.left<m_rUser1.right)
	{
		DWORD pos=GetMessagePos();
		POINT pt={(short)LOWORD(pos),(short)HIWORD(pos)};
		ScreenToClient(&pt);
		if (PtInRect(&m_rUser1,pt))
		{
			SetCursor(LoadCursor(NULL,IDC_HAND));
			return TRUE;
		}
	}
	if (m_rUser2.left<m_rUser2.right)
	{
		DWORD pos=GetMessagePos();
		POINT pt={(short)LOWORD(pos),(short)HIWORD(pos)};
		ScreenToClient(&pt);
		if (PtInRect(&m_rUser2,pt))
		{
			SetCursor(LoadCursor(NULL,IDC_HAND));
			return TRUE;
		}
	}
	bHandled=FALSE;
	return 0;
}

void CMenuContainer::PlayMenuSound( TMenuSound sound )
{
	const wchar_t *setting=NULL;
	switch (sound)
	{
		case SOUND_MAIN:
			setting=L"SoundMain";
			break;
		case SOUND_POPUP:
			setting=L"SoundPopup";
			break;
		case SOUND_COMMAND:
			setting=L"SoundCommand";
			break;
		case SOUND_DROP:
			setting=L"SoundDrop";
			break;
		case SOUND_BUTTON_HOVER:
			setting=L"SoundButtonHover";
			break;
	}
	CString str=GetSettingString(setting);
	if (_wcsicmp(PathFindExtension(str),L".wav")==0)
	{
		wchar_t path[_MAX_PATH];
		Strcpy(path,_countof(path),str);
		DoEnvironmentSubst(path,_countof(path));
		PlaySound(path,NULL,SND_FILENAME|SND_ASYNC|SND_NODEFAULT|SND_SYSTEM);
	}
	else if (_wcsicmp(str,L"none")==0)
		return;
	else
		PlaySound(str,NULL,SND_APPLICATION|SND_ALIAS|SND_ASYNC|SND_NODEFAULT|SND_SYSTEM);
}

void CMenuContainer::SaveItemOrder( const std::vector<SortMenuItem> &items )
{
	if ((m_Options&CONTAINER_DROP) && m_FolderHash[0])
	{
		// save item names in the registry
		CRegKey regOrder;
		wchar_t name[100];
		if (regOrder.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu\\Order")!=ERROR_SUCCESS)
			regOrder.Create(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu\\Order");
		std::vector<unsigned int> hashes[2];
		for (std::vector<SortMenuItem>::const_iterator it=items.begin();it!=items.end();++it)
		{
			if (m_FolderHash[1] && it->priority==2)
				hashes[1].push_back(it->nameHash);
			else
				hashes[0].push_back(it->nameHash);
		}

		Sprintf(name,_countof(name),L"%08X",m_FolderHash[0]);
		if (hashes[0].empty())
			regOrder.SetBinaryValue(name,NULL,0);
		else
			regOrder.SetBinaryValue(name,&hashes[0][0],(int)hashes[0].size()*4);
		if (m_FolderHash[1])
		{
			Sprintf(name,_countof(name),L"%08X",m_FolderHash[1]);
			if (hashes[1].empty())
				regOrder.SetBinaryValue(name,NULL,0);
			else
				regOrder.SetBinaryValue(name,&hashes[1][0],(int)hashes[1].size()*4);
		}
	}
}

void CMenuContainer::LoadItemOrder( void )
{
	bool bLoaded=false;
	if ((m_Options&CONTAINER_DROP) && m_FolderHash[0])
	{
		// load item names from the registry
		std::vector<unsigned int> hashes[2];
		CRegKey regOrder;
		if (regOrder.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu\\Order",KEY_READ)==ERROR_SUCCESS)
		{
			wchar_t name[100];
			{
				ULONG size=0;
				Sprintf(name,_countof(name),L"%08X",m_FolderHash[0]);
				regOrder.QueryBinaryValue(name,NULL,&size);
				if (size>0 && !(size&3))
				{
					hashes[0].resize(size/4);
					regOrder.QueryBinaryValue(name,&hashes[0][0],&size);
					bLoaded=true;
				}
			}
			if (m_FolderHash[1])
			{
				ULONG size=0;
				Sprintf(name,_countof(name),L"%08X",m_FolderHash[1]);
				regOrder.QueryBinaryValue(name,NULL,&size);
				if (size>0 && !(size&3))
				{
					hashes[1].resize(size/4);
					regOrder.QueryBinaryValue(name,&hashes[1][0],&size);
					bLoaded=true;
				}
			}
		}
		if (hashes[0].size()==1 && hashes[0][0]=='AUTO')
		{
			m_Options|=CONTAINER_AUTOSORT;
			for (std::vector<MenuItem>::iterator it=m_Items.begin();it!=m_Items.end();++it)
				it->row=0;
		}
		else
		{
			m_Options&=~CONTAINER_AUTOSORT;

			// assign each m_Item an index based on its position in items. store in row
			// unknown items get the index of the blank item, or at the end
			for (std::vector<MenuItem>::iterator it=m_Items.begin();it!=m_Items.end();++it)
			{
				unsigned int hash=it->nameHash;
				const std::vector<unsigned int> &hashRef=(it->priority==0?hashes[0]:hashes[1]);
				it->row=(int)hashRef.size();
				for (int i=0;i<(int)hashRef.size();i++)
				{
					if (hashRef[i]==hash)
					{
						it->row=i;
						break;
					}
					else if (hashRef[i]==FNV_HASH0)
						it->row=i;
				}
				if (m_Options&CONTAINER_SORTZA)
					it->row=-it->row;
			}
		}
	}
	else
	{
		for (std::vector<MenuItem>::iterator it=m_Items.begin();it!=m_Items.end();++it)
			it->row=0;
	}

	if (!(m_Options&CONTAINER_DOCUMENTS) || GetSettingInt(L"SortRecentDocuments")!=2)
	{
		// sort by row, then by bFolder, then by name
		s_bExtensionSort=(m_Options&CONTAINER_DOCUMENTS) && GetSettingInt(L"SortRecentDocuments")==1;
		std::sort(m_Items.begin(),m_Items.end());
		s_bExtensionSort=false;
	}
	if (m_Options&CONTAINER_SORTZA)
		std::reverse(m_Items.begin(),m_Items.end());

	if ((m_Options&CONTAINER_DROP) && (m_Options&CONTAINER_SORTONCE) && !bLoaded)
	{
		std::vector<SortMenuItem> items;
		for (std::vector<MenuItem>::const_iterator it=m_Items.begin();it!=m_Items.end();++it)
		{
			if (it->id==MENU_NO)
			{
				SortMenuItem item(*it);
				items.push_back(item);
			}
		}
		SaveItemOrder(items);
	}
}

void CMenuContainer::AddMRUAppId( const wchar_t *appid )
{
	wchar_t path[256];
	Sprintf(path,_countof(path),L"APP:%s",appid);
	AddMRUShortcut(path);
}

void CMenuContainer::AddMRUShortcut( const wchar_t *path )
{
	if (!*path) return;
	Assert(s_bMRULoaded);
	bool bFound=false;
	for (int i=0;i<MRU_PROGRAMS_COUNT;i++)
	{
		if (_wcsicmp(s_MRUShortcuts[i],path)==0)
		{
			if (i>0)
			{
				CString str=s_MRUShortcuts[i];
				for (;i>0;i--)
					s_MRUShortcuts[i]=s_MRUShortcuts[i-1];
				s_MRUShortcuts[0]=str;
			}
			bFound=true;
			break;
		}
	}

	if (!bFound)
	{
		for (int i=MRU_PROGRAMS_COUNT-1;i>0;i--)
			s_MRUShortcuts[i]=s_MRUShortcuts[i-1];
		s_MRUShortcuts[0]=path;
	}

	SaveMRUShortcuts();
}

void CMenuContainer::DeleteMRUAppId( const wchar_t *appid )
{
	wchar_t path[256];
	Sprintf(path,_countof(path),L"APP:%s",appid);
	DeleteMRUShortcut(path);
}

void CMenuContainer::DeleteMRUShortcut( const wchar_t *path )
{
	Assert(s_bMRULoaded);
	if (path)
	{
		for (int i=0;i<MRU_PROGRAMS_COUNT;i++)
		{
			if (_wcsicmp(s_MRUShortcuts[i],path)==0)
			{
				for (int j=i;j<MRU_PROGRAMS_COUNT-1;j++)
					s_MRUShortcuts[j]=s_MRUShortcuts[j+1];
				s_MRUShortcuts[MRU_PROGRAMS_COUNT-1].Empty();
				i--;
			}
		}
	}
	else
	{
		for (int i=0;i<MRU_PROGRAMS_COUNT;i++)
			s_MRUShortcuts[i].Empty();
	}

	SaveMRUShortcuts();
}

void CMenuContainer::SaveMRUShortcuts( void )
{
	Assert(s_bMRULoaded);
	CRegKey regMRU;
	if (regMRU.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu\\MRU",KEY_READ|KEY_WRITE)!=ERROR_SUCCESS)
		regMRU.Create(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu\\MRU");

	bool bDelete=false;
	for (int i=0;i<MRU_PROGRAMS_COUNT;i++)
	{
		wchar_t name[10];
		Sprintf(name,_countof(name),L"%d",i);
		if (s_MRUShortcuts[i].IsEmpty())
			bDelete=true; // delete the rest!
		if (bDelete)
		{
			wchar_t path[256];
			ULONG size=_countof(path);
			if (regMRU.QueryStringValue(name,path,&size)!=ERROR_SUCCESS)
				break;
			regMRU.DeleteValue(name);
		}
		else
			regMRU.SetStringValue(name,s_MRUShortcuts[i]);
	}
}

void CMenuContainer::LoadMRUShortcuts( void )
{
	for (int i=0;i<MRU_PROGRAMS_COUNT;i++)
		s_MRUShortcuts[i].Empty();
	CRegKey regMRU;
	if (regMRU.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu\\MRU",KEY_READ)==ERROR_SUCCESS)
	{
		for (int i=0;i<MRU_PROGRAMS_COUNT;i++)
		{
			wchar_t name[10];
			Sprintf(name,_countof(name),L"%d",i);
			wchar_t path[256];
			ULONG size=_countof(path);
			if (regMRU.QueryStringValue(name,path,&size)!=ERROR_SUCCESS)
				break;
			s_MRUShortcuts[i]=path;
		}
	}
	s_bMRULoaded=true;
}

void CMenuContainer::RemoveMFUShortcut( unsigned int hash, bool bAppId )
{
	CRegKey regKey;
	if (regKey.Open(HKEY_CURRENT_USER,bAppId?USERASSIST_APPIDS_KEY:USERASSIST_LINKS_KEY,KEY_READ|KEY_WRITE)==ERROR_SUCCESS)
	{
		for (int idx=0;;idx++)
		{
			wchar_t name[_MAX_PATH];
			DWORD len=_countof(name);
			LONG res=RegEnumValue(regKey,idx,name,&len,NULL,NULL,NULL,0);
			if (res==ERROR_NO_MORE_ITEMS)
				break;
			if (CalcFNVHash(name)==hash)
			{
				regKey.DeleteValue(name);
				return;
			}
		}
	}
}

LRESULT CMenuContainer::OnGetAccObject( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if ((DWORD)lParam==(DWORD)OBJID_CLIENT && m_pAccessible)
	{
		return LresultFromObject(IID_IAccessible,wParam,m_pAccessible);
	}
	else
	{
		bHandled=FALSE;
		return 0;
	}
}

///////////////////////////////////////////////////////////////////////////////

bool CMenuContainer::CloseStartMenu( void )
{
	if (s_Menus.empty()) return false;
	if (!CMenuContainer::CanShowMenu()) return false;
 
	HWND taskBar=s_TaskBar?s_TaskBar:g_TaskBar;
	::SetActiveWindow(s_StartButton?s_StartButton:taskBar);
	CloseSubMenus(0,NULL);

	if (s_LastFGWindow && s_LastFGWindow!=GetDesktopWindow() && s_LastFGWindow!=GetShellWindow() && taskBar)
	{
		// don't activate the last application if it was a full-screen window on the same monitor as the taskbar.
		// leave the taskbar up instead
		MONITORINFO info={sizeof(info)};
		GetMonitorInfo(MonitorFromWindow(taskBar,MONITOR_DEFAULTTOPRIMARY),&info);
		RECT rc;
		::GetWindowRect(s_LastFGWindow,&rc);
		if (memcmp(&info.rcMonitor,&rc,sizeof(RECT))!=0)
			SetForegroundWindow(s_LastFGWindow);
	}

	return true;
}

void CMenuContainer::HideStartMenu( void )
{
	for (std::vector<CMenuContainer*>::iterator it=s_Menus.begin();it!=s_Menus.end();++it)
		if (!(*it)->m_bDestroyed)
			(*it)->ShowWindow(SW_HIDE);
}

bool CMenuContainer::IsMenuWindow( HWND hWnd )
{
	for (std::vector<CMenuContainer*>::iterator it=s_Menus.begin();it!=s_Menus.end();++it)
		if (hWnd==(*it)->m_hWnd || (*it)->IsChild(hWnd))
			return true;
	return false;
}

bool CMenuContainer::CloseProgramsMenu( void )
{
	if (s_Menus.empty()) return false;

	for (std::vector<CMenuContainer*>::const_reverse_iterator it=s_Menus.rbegin();it!=s_Menus.rend();++it)
		if (!(*it)->m_bDestroyed)
			(*it)->PostMessage(WM_CLOSE);

	return true;
}

static void NewVersionCallback( VersionData &data )
{
	wchar_t path[_MAX_PATH];
	GetModuleFileName(g_Instance,path,_countof(path));
	PathRemoveFileSpec(path);
	PathAppend(path,L"Update.exe");
	wchar_t cmdLine[1024];
	Sprintf(cmdLine,_countof(cmdLine),L"\"%s\" -popup",path);
	STARTUPINFO startupInfo={sizeof(startupInfo)};
	// don't display busy cursor as we are doing this on background
	startupInfo.dwFlags=STARTF_FORCEOFFFEEDBACK;
	PROCESS_INFORMATION processInfo{};
	if (CreateProcess(path,cmdLine,NULL,NULL,TRUE,0,NULL,NULL,&startupInfo,&processInfo))
	{
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
	}
}

static bool CheckForUpdates( void )
{
	bool bHasUpdates=false;
	if (GetWinVersion()<WIN_VER_WIN8)
	{
		CRegKey regKey;
		if (regKey.Open(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\UAS",KEY_READ)==ERROR_SUCCESS)
		{
			DWORD val;
			if (regKey.QueryDWORDValue(L"UpdateCount",val)==ERROR_SUCCESS)
			{
				LOG_MENU(LOG_OPEN,L"WU: UpdateCount=%d",val);
				bHasUpdates=val>0;
			}
		}
	}
	if (!bHasUpdates)
	{
		CComPtr<ISystemInformation> pSysInfo;
		pSysInfo.CoCreateInstance(CLSID_SystemInformation);
		if (pSysInfo)
		{
			VARIANT_BOOL reboot;
			if (SUCCEEDED(pSysInfo->get_RebootRequired(&reboot)) && reboot)
			{
				LOG_MENU(LOG_OPEN,L"WU: Reboot required");
				bHasUpdates=true;
			}
		}
	}
	if (!bHasUpdates && GetWinVersion()>=WIN_VER_WIN8)
	{
		typedef HRESULT (WINAPI *FGetAutoUpdateNotification)(DWORD,DWORD*,DWORD*,DWORD*);
		HMODULE mod=LoadLibrary(L"wuaext.dll");
		if (mod)
		{
			FGetAutoUpdateNotification fun=(FGetAutoUpdateNotification)GetProcAddress(mod,"GetAutoUpdateNotification");
			if (fun)
			{
				DWORD a,b,c;
				HRESULT hr=fun(0,&a,&b,&c);
				bHasUpdates=(a==1 || a==2);
				LOG_MENU(LOG_OPEN,L"WU: GetAutoUpdateNotification: %d, %d, %d, %d",hr,a,b,c);
			}
			FreeLibrary(mod);
		}
	}
	return bHasUpdates;
}

static void CreateStartScreenFile( const wchar_t *fname )
{
	wchar_t link[_MAX_PATH];
	GetModuleFileName(g_Instance,link,_countof(link));
	PathRemoveFileSpec(link);
	PathAppend(link,L"Start Screen.lnk");
	CopyFile(link,fname,TRUE);
}

bool CMenuContainer::HasMoreResults( void )
{
	if (s_HasMoreResults==-1)
		s_HasMoreResults=(GetSettingBool(L"MoreResults") && GetSettingBool(L"SearchFiles") && HasSearchService())?1:0;
	return s_HasMoreResults!=0;
}

RECT CMenuContainer::CalculateWorkArea( const RECT &taskbarRect )
{
	RECT rc;
	if (!GetSettingBool(L"AlignToWorkArea"))
	{
		rc = s_MenuLimits;
		if ((s_TaskBarEdge == ABE_LEFT || s_TaskBarEdge == ABE_RIGHT) && GetSettingBool(L"ShowNextToTaskbar"))
		{
			// when the taskbar is on the side and the menu is not on top of it
			// the start button is assumed at the top
			if (s_TaskBarEdge == ABE_LEFT)
				rc.left = taskbarRect.right;
			else
				rc.right = taskbarRect.left;
		}
		else
		{
			if (s_TaskBarEdge == ABE_BOTTOM)
			{
				// taskbar is at the bottom
				rc.bottom = taskbarRect.top;
			}
			else if (s_TaskBarEdge == ABE_TOP)
			{
				// taskbar is at the top
				rc.top = taskbarRect.bottom;
			}
			else
			{
				// taskbar is on the side, start button must be at the top
				rc.top = s_StartRect.bottom;
			}
		}
	}
	else
	{
		// Get working area of the monitor the specified taskbar is on
		MONITORINFO info{ sizeof(MONITORINFO) };
		HMONITOR mon = MonitorFromRect(&taskbarRect, 0);
		GetMonitorInfo(mon, &info);
		rc = info.rcWork;
	}
	if (!s_bLockWorkArea)
	{
		// exclude floating keyboard
		if (s_pFrameworkInputPane)
		{
			RECT kbdRect;
			if (SUCCEEDED(s_pFrameworkInputPane->Location(&kbdRect)))
			{
				if (rc.top<kbdRect.bottom && rc.bottom>kbdRect.top && rc.left<kbdRect.right && rc.right>kbdRect.left)
				{
					HMONITOR monitor=MonitorFromWindow(m_hWnd,MONITOR_DEFAULTTONULL);
					if (monitor)
					{
						MONITORINFO info={sizeof(info)};
						GetMonitorInfo(monitor,&info);
						if (kbdRect.bottom==info.rcMonitor.bottom && kbdRect.left==info.rcMonitor.left && kbdRect.right==info.rcMonitor.right)
						{
							// the keyboard is docked at the bottom of the monitor
							rc.bottom=kbdRect.top;
						}
					}
				}
			}
		}
	}

	return rc;
}

// Calculates start menu position
POINT CMenuContainer::CalculateCorner( void )
{
	RECT margin={0,0,0,0};
	if (IsAppThemed())
		AdjustWindowRect(&margin,GetWindowLong(GWL_STYLE),FALSE);

	POINT corner;
	if (m_Options&CONTAINER_LEFT)
		corner.x=max(s_MainMenuLimits.left,s_StartRect.left)+margin.left;
	else
		corner.x=min(s_MainMenuLimits.right,s_StartRect.right)+margin.right;

	if (m_Options&CONTAINER_TOP)
	{
		if (s_bBehindTaskbar)
			corner.y=max(s_MainMenuLimits.top,s_StartRect.top)+margin.top;
		else
			corner.y=max(s_MainMenuLimits.top,s_StartRect.top);
	}
	else
		corner.y=s_MainMenuLimits.bottom+margin.bottom;

	corner.x+=GetSettingInt(L"HorizontalMenuOffset");
	corner.y+=GetSettingInt(L"VerticalMenuOffset");

	return corner;
}

// Toggles the start menu
HWND CMenuContainer::ToggleStartMenu( int taskbarId, bool bKeyboard, bool bAllPrograms )
{
	s_bAllPrograms=false;
	if (bAllPrograms)
	{
		if (CloseProgramsMenu())
		{
			return NULL;
		}

		::ShowWindow(g_UserPic,SW_HIDE);
	}
	else
	{
		if (!CanShowMenu())
			return NULL;
		if (!bKeyboard) s_LastFGWindow=NULL;
		bool bSameTaskbar=(s_TaskBarId==taskbarId);
		if (CloseStartMenu())
		{
			if (bSameTaskbar)
				return NULL;
		}

		s_LastFGWindow=GetForegroundWindow();
		SetForegroundWindow(GetTaskbarInfo(taskbarId)->startButton);
		EnableStartTooltip(false);
	}

	{
		CRegKey regKey;
		if (regKey.Open(HKEY_CURRENT_USER,GetSettingsRegPath())!=ERROR_SUCCESS)
			regKey.Create(HKEY_CURRENT_USER,GetSettingsRegPath());

		DWORD val;
		if (regKey.QueryDWORDValue(L"ShowedStyle2",val)!=ERROR_SUCCESS)
		{
			regKey.SetDWORDValue(L"ShowedStyle2",1);
			if (GetSettingBool(L"EnableSettings") && !IsSettingLocked(L"MenuStyle") && !IsSettingForcedDefault(L"MenuStyle"))
			{
				if (regKey.QueryDWORDValue(L"ShowedStyle2",val)==ERROR_SUCCESS && val)
				{
					EditSettings(false,IDS_STYLE_SETTINGS);
					return NULL;
				}
			}
		}
	}

	if (GetSettingInt(L"CompatibilityFixes")&COMPATIBILITY_CRASH_TEST)
		*(int*)0=0;
	g_ItemManager.RefreshInfos();
	s_bWin7Style=GetSettingInt(L"MenuStyle")==MENU_WIN7;

	if (!s_StartMenuMsg)
		s_StartMenuMsg=RegisterWindowMessage(L"OpenShellMenu.StartMenuMsg");
	s_StartMenuParams.uEdge=0xFFFFFFFF;
	s_TaskBarId=taskbarId;
	TaskbarInfo *taskBar=GetTaskbarInfo(taskbarId);
	s_TaskBar=taskBar->taskBar;
	s_StartButton=taskBar->startButton;
	s_bAllPrograms=bAllPrograms;
	s_MenuMode=MODE_NORMAL;
	s_PreSearchMenuMode=MODE_NORMAL;
	s_MenuHeight=-1;
	s_MenuMaxHeight[0]=-1;
	s_MenuWidthJump=0;
	s_MenuWidthMax=0;
	s_MenuWidthNormal=-1;
	s_ProgramsWidth=GetSettingInt(L"ProgramsWidth");
	s_JumplistWidth=GetSettingInt(L"JumplistWidth");
	s_BackgroundW1=s_BackgroundW2=s_BackgroundH1=s_BackgroundH2=0;
	ClearOldState();
	int categories=0;
	{
		CRegKey regKey;
		if (regKey.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu\\Settings",KEY_READ|KEY_WOW64_64KEY)==ERROR_SUCCESS)
		{
			DWORD log;
			if (regKey.QueryDWORDValue(L"LogCategories",log)==ERROR_SUCCESS)
				categories=log;
		}
	}
#ifdef BUILD_BETA
	categories=LOG_ALL;
#endif
	if (categories!=0)
	{
		wchar_t path[_MAX_PATH]=L"%LOCALAPPDATA%\\OpenShell";
		DoEnvironmentSubst(path,_MAX_PATH);
		SHCreateDirectory(NULL,path);
		Strcat(path,_countof(path),L"\\StartMenuLog.txt");
		InitLog(categories,path);
	}

	{
		CSettingsLockWrite lock;
		UpdateDefaultSettings();
	}

	// initialize all settings
	bool bErr=false;
	HMONITOR initialMonitor=MonitorFromWindow(s_TaskBar,MONITOR_DEFAULTTONEAREST);
	// note: GetTaskbarPosition properly identifies monitor in case of multi-monitor setup and automatic taskbar hiding
	GetTaskbarPosition(s_TaskBar,NULL,&initialMonitor,NULL);

	int dpi=CItemManager::GetDPI(true);
	if (!CItemManager::GetDPIOverride() && GetWinVersion()>=WIN_VER_WIN81)
	{
		HMODULE shModule=GetModuleHandle(L"Shcore.dll");
		if (shModule)
		{
			typedef HRESULT (WINAPI *tGetDpiForMonitor)( HMONITOR monitor, UINT dpiType, UINT *dpiX, UINT *dpiY );
			tGetDpiForMonitor GetDpiForMonitor=(tGetDpiForMonitor)GetProcAddress(shModule,"GetDpiForMonitor");
			if (GetDpiForMonitor)
			{
				UINT dpiX, dpiY;
				if (SUCCEEDED(GetDpiForMonitor(initialMonitor,0,&dpiX,&dpiY)))
					dpi=dpiY;
			}
		}
	}
	if (bAllPrograms)
	{
		bErr=!s_Skin.LoadMenuSkin(GetSettingString(L"SkinA"),GetSettingString(L"SkinVariationA"),GetSettingString(L"SkinOptionsA"),MenuSkin::SKIN_TYPE_ALL_PROGRAMS,LOADMENU_RESOURCES,dpi);
		if (bErr)
			s_Skin.LoadDefaultMenuSkin(MenuSkin::SKIN_TYPE_ALL_PROGRAMS,LOADMENU_RESOURCES,dpi);
	}
	else if (s_bWin7Style)
	{
		bErr=!s_Skin.LoadMenuSkin(GetSettingString(L"SkinW7"),GetSettingString(L"SkinVariationW7"),GetSettingString(L"SkinOptionsW7"),MenuSkin::SKIN_TYPE_WIN7,LOADMENU_RESOURCES,dpi);
		if (bErr)
			s_Skin.LoadDefaultMenuSkin(MenuSkin::SKIN_TYPE_WIN7,LOADMENU_RESOURCES,dpi);
	}
	else if (GetSettingInt(L"MenuStyle")==1)
	{
		bErr=!s_Skin.LoadMenuSkin(GetSettingString(L"SkinC2"),GetSettingString(L"SkinVariationC2"),GetSettingString(L"SkinOptionsC2"),MenuSkin::SKIN_TYPE_CLASSIC2,LOADMENU_RESOURCES,dpi);
		if (bErr)
			s_Skin.LoadDefaultMenuSkin(MenuSkin::SKIN_TYPE_CLASSIC2,LOADMENU_RESOURCES,dpi);
	}
	else
	{
		bErr=!s_Skin.LoadMenuSkin(GetSettingString(L"SkinC1"),GetSettingString(L"SkinVariationC1"),GetSettingString(L"SkinOptionsC1"),MenuSkin::SKIN_TYPE_CLASSIC1,LOADMENU_RESOURCES,dpi);
		if (bErr)
			s_Skin.LoadDefaultMenuSkin(MenuSkin::SKIN_TYPE_CLASSIC1,LOADMENU_RESOURCES,dpi);
	}

	const MenuSkin &s_Skin=CMenuContainer::s_Skin; // shadow s_Skin with a const reference to catch any modifications to non-mutable members
	g_CurrentCSMTaskbar=s_TaskBarId;
	PressStartButton(s_TaskBarId,true);
	g_ItemManager.ResetTempIcons();
	s_ScrollMenus=GetSettingInt(L"ScrollType");
	s_bExpandLinks=GetSettingBool(L"ExpandFolderLinks");
	s_bSingleClickFolders=GetSettingBool(L"SingleClickFolders");
	s_bLogicalSort=GetSettingBool(L"NumericSort");
	s_MaxRecentDocuments=GetSettingInt(L"MaxRecentDocuments");
	s_ShellFormat=RegisterClipboardFormat(CFSTR_SHELLIDLIST);
	s_ShellUrlFormat=RegisterClipboardFormat(CFSTR_INETURL);
	s_DescriptorFormat=RegisterClipboardFormat(CFSTR_FILEDESCRIPTOR);
	s_ContentsFormat=RegisterClipboardFormat(CFSTR_FILECONTENTS);
	s_MetroLinkFormat=RegisterClipboardFormat(L"OpenShell.MetroLink");
	s_PreferredEffectFormat=RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT);
	s_DropDescriptionFormat=RegisterClipboardFormat(CFSTR_DROPDESCRIPTION);
	s_bNoCommonFolders=(SHRestricted(REST_NOCOMMONGROUPS)!=0);
	s_bNoRun=(SHRestricted(REST_NORUN)!=0);
	s_bNoClose=(SHRestricted(REST_NOCLOSE)!=0);
	s_bHasTouch=GetWinVersion()>=WIN_VER_WIN8 && GetSettingBool(L"EnableTouch") && (GetSystemMetrics(SM_DIGITIZER)&NID_INTEGRATED_TOUCH)!=0;
	s_HasMoreResults=-1;
	s_bDisableHover=false;
	s_bDragClosed=false;
	s_bPendingSearchEnter=false;
	InitTouchHelper();

	bool bRemote=GetSystemMetrics(SM_REMOTESESSION)!=0;
	wchar_t wabPath[_MAX_PATH]=L"%ProgramFiles%\\Windows Mail\\wab.exe";
	DoEnvironmentSubst(wabPath,_countof(wabPath));
	HANDLE hWab=CreateFile(wabPath,0,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	bool bPeople=(hWab!=INVALID_HANDLE_VALUE);
	if (bPeople) CloseHandle(hWab);
	s_bRTL=s_Skin.ForceRTL || IsLanguageRTL();

	{
		APPBARDATA appbar={sizeof(appbar)};
		s_TaskbarState=(DWORD)SHAppBarMessage(ABM_GETSTATE,&appbar);
	}

	// the taskbar on Windows 7 (and most likely later versions) is always on top even though it doesn't have the ABS_ALWAYSONTOP flag.
	if (GetWinVersion()>=WIN_VER_WIN7)
	{
		// also check the WS_EX_TOPMOST style - maybe some tool like DisableTaskbarOnTop is messing with it
		if (::GetWindowLong(s_TaskBar,GWL_EXSTYLE)&WS_EX_TOPMOST)
			s_TaskbarState|=ABS_ALWAYSONTOP;
	}

	if (!bAllPrograms && (s_TaskbarState&ABS_AUTOHIDE))
	{
		::SetActiveWindow(s_TaskBar);
	}
	if (!bAllPrograms)
	{
		// set the taskbar as the foreground window so all keyboard input is sent to the taskbar thread (and be later captured by the start menu)
		SetForegroundWindow(s_TaskBar);
	}

	if (s_bActiveDirectory==-1)
	{
		DOMAIN_CONTROLLER_INFO *info;
		DWORD err=DsGetDcName(NULL,NULL,NULL,NULL,DS_RETURN_FLAT_NAME,&info);
		if (err==ERROR_SUCCESS)
		{
			s_bActiveDirectory=1;
			NetApiBufferFree(info);
		}
		else
			s_bActiveDirectory=0;
	}

	if (GetWinVersion()>=WIN_VER_WIN8 && !s_pFrameworkInputPane && !(GetSettingInt(L"CompatibilityFixes")&COMPATIBILITY_NO_TOUCH_KBD))
	{
		s_pFrameworkInputPane.CoCreateInstance(CLSID_FrameworkInputPane);
	}

	s_bHasUpdates=(!bRemote || GetSettingBool(L"RemoteShutdown")) && GetSettingBool(L"CheckWinUpdates") && CheckForUpdates();

	// Check control panel options for power buttons
	bool bHibernate = true, bSleep = true, bLock = true;
	{
		CRegKey regKeyButtons;
		if (regKeyButtons.Open(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FlyoutMenuSettings", KEY_READ) == ERROR_SUCCESS)
		{
			DWORD dwValue = 1;
			if (regKeyButtons.QueryDWORDValue(L"ShowHibernateOption", dwValue) == ERROR_SUCCESS)
				if (dwValue == 0)
					bHibernate = false;

			if (regKeyButtons.QueryDWORDValue(L"ShowLockOption", dwValue) == ERROR_SUCCESS)
				if (dwValue == 0)
					bLock = false;

			if (regKeyButtons.QueryDWORDValue(L"ShowSleepOption", dwValue) == ERROR_SUCCESS)
				if (dwValue == 0)
					bSleep = false;
		}
	}

	if (bHibernate || bSleep)
	{
		SYSTEM_POWER_CAPABILITIES powerCaps;
		GetPwrCapabilities(&powerCaps);

		// no sleep capabilities, turn off the sleep option
		if (!(powerCaps.SystemS1 || powerCaps.SystemS2 || powerCaps.SystemS3 || powerCaps.AoAc))
		{
			bSleep = false;
		}

		// no hibernate capabilities, turn off hibernate option
		if (!powerCaps.HiberFilePresent)
		{
			bHibernate = false;
			/*	disabled for now, use group policy to hide Hibernate
					// disable hibernate if hybrid sleep (fast s4) is enabled
					SYSTEM_POWER_STATUS status;
					if (GetSystemPowerStatus(&status) && (status.ACLineStatus==0 || status.ACLineStatus==1))
					{
						GUID *pScheme;
						if (PowerGetActiveScheme(NULL,&pScheme)==ERROR_SUCCESS)
						{
							DWORD index;
							if ((status.ACLineStatus==1?PowerReadACValueIndex:PowerReadDCValueIndex)(NULL,pScheme,&GUID_SLEEP_SUBGROUP,&GUID_HIBERNATE_FASTS4_POLICY,&index)==ERROR_SUCCESS && index)
								bHibernate=false;
							LocalFree(pScheme);
						}
					}*/
		}
	}

	for (int i=0;i<_countof(g_StdOptions);i++)
	{
		switch (g_StdOptions[i].id)
		{
			case MENU_COMPUTER:
				if (s_bWin7Style)
					g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				else
				{
					g_StdOptions[i].options=0;
					int show=GetSettingInt(L"Computer");
					if (show==1)
						g_StdOptions[i].options=MENU_ENABLED;
					else if (show==2 || show==3)
						g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				}
				break;
			case MENU_FAVORITES:
				if (s_bWin7Style)
					g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				else
				{
					g_StdOptions[i].options=0;
					int show=GetSettingInt(L"Favorites");
					if (show==1)
						g_StdOptions[i].options=MENU_ENABLED;
					else if (show==2)
						g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				}
				break;
			case MENU_DOCUMENTS:
				if (s_bWin7Style)
					g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				else
				{
					g_StdOptions[i].options=0;
					int show=GetSettingInt(L"Documents");
					if (show==1)
						g_StdOptions[i].options=MENU_ENABLED;
					else if (show==2)
						g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				}
				break;
			case MENU_LOGOFF:
				if (s_bWin7Style)
					g_StdOptions[i].options=SHRestricted(REST_STARTMENULOGOFF)!=1?MENU_ENABLED|MENU_EXPANDED:0;
				else
					g_StdOptions[i].options=GetSettingBool(L"LogOff")?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_DISCONNECT:
				g_StdOptions[i].options=(bRemote && !SHRestricted(REST_NODISCONNECT))?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_SHUTDOWN:
			case MENU_RESTART:
				g_StdOptions[i].options=(!s_bNoClose && (!bRemote || GetSettingBool(L"RemoteShutdown")))?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_SHUTDOWN_NOUPDATE:
				g_StdOptions[i].options=(s_bHasUpdates && !s_bNoClose && (!bRemote || GetSettingBool(L"RemoteShutdown")))?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_RESTART_NOUPDATE:
				g_StdOptions[i].options=(s_bHasUpdates && GetWinVersion()>=WIN_VER_WIN8 && !s_bNoClose && (!bRemote || GetSettingBool(L"RemoteShutdown")))?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_SHUTDOWN_BOX:
				g_StdOptions[i].options=0;
				if (!bRemote || GetSettingBool(L"RemoteShutdown"))
				{
					int show=GetSettingInt(L"Shutdown");
					if (show==1)
						g_StdOptions[i].options=MENU_ENABLED;
					else if (show==2)
						g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				}
				break;
			case MENU_SHUTDOWN_BUTTON:
				g_StdOptions[i].options=GetSettingInt(L"ShutdownCommand")>SHUTDOWN_TYPE_NONE?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_UNDOCK:
				{
					HW_PROFILE_INFO info;
					GetCurrentHwProfile(&info);
					if ((info.dwDockInfo&(DOCKINFO_DOCKED|DOCKINFO_UNDOCKED))!=DOCKINFO_DOCKED)
						g_StdOptions[i].options=0;
					else if (s_bWin7Style)
						g_StdOptions[i].options=SHRestricted(REST_NOSMEJECTPC)?0:MENU_ENABLED|MENU_EXPANDED;
					else
						g_StdOptions[i].options=GetSettingBool(L"Undock")?MENU_ENABLED|MENU_EXPANDED:0;
				}
				break;
			case MENU_CONTROLPANEL:
				if (s_bWin7Style)
					g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				else
				{
					g_StdOptions[i].options=0;
					int show=GetSettingInt(L"ControlPanel");
					if (show==1)
						g_StdOptions[i].options=MENU_ENABLED;
					else if (show==2)
						g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				}
				break;
			case MENU_SECURITY:
				g_StdOptions[i].options=bRemote?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_NETWORK:
				if (s_bWin7Style)
					g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				else
				{
					g_StdOptions[i].options=0;
					int show=GetSettingInt(L"Network");
					if (show==1)
						g_StdOptions[i].options=MENU_ENABLED;
					else if (show==2)
						g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				}
				break;
			case MENU_PRINTERS:
				if (s_bWin7Style)
					g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				else
				{
					g_StdOptions[i].options=0;
					int show=GetSettingInt(L"Printers");
					if (show==1)
						g_StdOptions[i].options=MENU_ENABLED;
					else if (show==2)
						g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				}
				break;

			case MENU_SEARCH_PRINTER:
				g_StdOptions[i].options=s_bActiveDirectory==1?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_SEARCH_COMPUTERS:
				g_StdOptions[i].options=(s_bActiveDirectory==1 && !SHRestricted(REST_HASFINDCOMPUTERS))?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_SEARCH_PEOPLE:
				g_StdOptions[i].options=bPeople?MENU_ENABLED|MENU_EXPANDED:0;
				break;

			case MENU_HELP:
				if (s_bWin7Style)
					g_StdOptions[i].options=SHRestricted(REST_NOSMHELP)?0:MENU_ENABLED|MENU_EXPANDED;
				else
					g_StdOptions[i].options=GetSettingBool(L"Help")?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_RUN:
				if (s_bWin7Style)
					g_StdOptions[i].options=s_bNoRun?0:MENU_ENABLED|MENU_EXPANDED;
				else
					g_StdOptions[i].options=GetSettingBool(L"Run")?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_TASKBAR:
				g_StdOptions[i].options=!SHRestricted(REST_NOSETTASKBAR)?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_FEATURES:
				g_StdOptions[i].options=(!SHRestricted(REST_NOSETFOLDERS) && !SHRestricted(REST_NOCONTROLPANEL))?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_CLASSIC_SETTINGS:
				g_StdOptions[i].options=GetSettingBool(L"EnableSettings")?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_SEARCH:
				g_StdOptions[i].options=GetSettingBool(L"Search")?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_SEARCH_BOX:
				g_StdOptions[i].options=GetSettingInt(L"SearchBox")!=SEARCHBOX_HIDDEN?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_USERFILES:
				if (s_bWin7Style)
					g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				else
				{
					g_StdOptions[i].options=0;
					int show=GetSettingInt(L"UserFiles");
					if (show==1)
						g_StdOptions[i].options=MENU_ENABLED;
					else if (show==2)
						g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				}
				break;
			case MENU_USERDOCUMENTS:
				if (s_bWin7Style)
					g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				else
				{
					g_StdOptions[i].options=0;
					int show=GetSettingInt(L"UserDocuments");
					if (show==1)
						g_StdOptions[i].options=MENU_ENABLED;
					else if (show==2)
						g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				}
				break;
			case MENU_USERPICTURES:
				if (s_bWin7Style)
					g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				else
				{
					g_StdOptions[i].options=0;
					int show=GetSettingInt(L"UserPictures");
					if (show==1)
						g_StdOptions[i].options=MENU_ENABLED;
					else if (show==2)
						g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
				}
				break;
			case MENU_LOCK:
				g_StdOptions[i].options=(bLock)?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_SLEEP:
				g_StdOptions[i].options=(!s_bNoClose && bSleep)?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_HIBERNATE:
				g_StdOptions[i].options=(!s_bNoClose && bHibernate)?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_SWITCHUSER:
				{
					g_StdOptions[i].options=MENU_ENABLED|MENU_EXPANDED;
					CComPtr<IShellDispatch2> pShellDisp;
					if (SUCCEEDED(CoCreateInstance(CLSID_Shell,NULL,CLSCTX_SERVER,IID_IShellDispatch2,(void**)&pShellDisp)))
					{
						long val;
						if (SUCCEEDED(pShellDisp->IsRestricted(CComBSTR(L"System"),CComBSTR(L"HideFastUserSwitching"),&val)) && val)
							g_StdOptions[i].options=0;
					}
				}
				break;
			case MENU_APPS:
				g_StdOptions[i].options=(s_bWin7Style || (GetWinVersion()>=WIN_VER_WIN8 && GetSettingBool(L"ShowAppsMenu")))?MENU_ENABLED|MENU_EXPANDED:0;
				break;
			case MENU_PCSETTINGS:
				g_StdOptions[i].options=(GetWinVersion()>=WIN_VER_WIN8)?MENU_ENABLED|MENU_EXPANDED:0;
				break;
		}
		LOG_MENU(LOG_OPEN,L"ItemOptions[%d]=%d",i,g_StdOptions[i].options);
	}	

	s_bNoDragDrop=!GetSettingBool(L"EnableDragDrop");
	s_bNoContextMenu=!GetSettingBool(L"EnableContextMenu");
	s_bKeyboardCues=bKeyboard&&GetSettingBool(L"EnableAccelerators")&&!GetSettingBool(L"AltAccelerators");
	s_RecentPrograms=(TRecentPrograms)GetSettingInt(L"RecentPrograms");
	if (s_RecentPrograms!=RECENT_PROGRAMS_NONE)
		LoadMRUShortcuts();
	s_ShutdownCommand=MENU_NO;

	// create the top menu from the Start Menu folders
	CAbsolutePidl path1;
	CAbsolutePidl path2;
	s_PinFolder.Empty();
	if (bAllPrograms || GetSettingInt(L"PinnedPrograms")==PINNED_PROGRAMS_FAST)
	{
		ShGetKnownFolderIDList(FOLDERID_StartMenu,&path1);
		CComString pPath;
		ShGetKnownFolderPath(FOLDERID_StartMenu,&pPath);
		if (pPath)
			s_PinFolder=pPath;
		if (!s_bNoCommonFolders)
			ShGetKnownFolderIDList(FOLDERID_CommonStartMenu,&path2);
	}
	else
	{
		wchar_t path[_MAX_PATH];
		Strcpy(path,_countof(path),GetSettingString(L"PinnedItemsPath"));
		DoEnvironmentSubst(path,_countof(path));
		SHCreateDirectory(NULL,path);
		s_PinFolder=path;
		SHParseDisplayName(path,NULL,&path1,0,NULL);
	}
#ifndef STARTSCREEN_WIN7
	if (GetWinVersion()>=WIN_VER_WIN8)
#endif
	{
		bool bPinned=GetSettingInt(L"PinnedPrograms")==PINNED_PROGRAMS_PINNED;
		bool bShortcut=GetSettingBool(L"StartScreenShortcut");
		wchar_t path[_MAX_PATH];
		Sprintf(path,_countof(path),L"%s\\%s",GetSettingString(L"PinnedItemsPath"),STARTSCREEN_COMMAND);
		DoEnvironmentSubst(path,_countof(path));
		if (bPinned)
		{
			if (GetFileAttributes(path)==INVALID_FILE_ATTRIBUTES)
			{
				if (bShortcut)
					CreateStartScreenFile(path);
			}
			else if (!bShortcut)
				DeleteFile(path);
		}
		CComString pPath;
		ShGetKnownFolderPath(FOLDERID_StartMenu,&pPath);
		Sprintf(path,_countof(path),L"%s\\" STARTSCREEN_COMMAND,(const wchar_t*)pPath);
		if (GetFileAttributes(path)==INVALID_FILE_ATTRIBUTES)
		{
			if (!bPinned && bShortcut)
				CreateStartScreenFile(path);
		}
		else if (bPinned || !bShortcut)
			DeleteFile(path);
	}

	int options=CONTAINER_PROGRAMS|CONTAINER_DRAG|CONTAINER_DROP;
	unsigned int rootSettings=0;
	const StdMenuItem *pRoot=NULL;
	if (bAllPrograms)
	{
		options|=CONTAINER_ALLPROGRAMS;
	}
	else
	{
		pRoot=ParseCustomMenu(rootSettings);
	}

	bool bTopMost=(s_TaskbarState&ABS_ALWAYSONTOP)!=0 || bAllPrograms;

	SystemParametersInfo(SPI_GETACTIVEWINDOWTRACKING,NULL,&s_XMouse,0);
	if (s_XMouse)
		SystemParametersInfo(SPI_SETACTIVEWINDOWTRACKING,NULL,(PVOID)FALSE,SPIF_SENDCHANGE);

	s_bBehindTaskbar=!bAllPrograms;
	s_bShowTopEmpty=false;
	DWORD dwStyle=WS_POPUP|WS_CLIPCHILDREN;
	s_SubmenuStyle=WS_POPUP;

	bool bTheme=IsAppThemed()!=FALSE;
	if (bTheme)
	{
		if (s_Skin.Main_opacity==MenuSkin::OPACITY_SOLID)
			dwStyle|=WS_BORDER;
		if (s_Skin.Submenu_opacity==MenuSkin::OPACITY_SOLID)
			s_SubmenuStyle|=WS_BORDER;
	}
	else
	{
		if (s_Skin.Main_opacity==MenuSkin::OPACITY_SOLID)
			dwStyle|=s_Skin.Main_thin_frame?WS_BORDER:WS_DLGFRAME;
		if (s_Skin.Submenu_opacity==MenuSkin::OPACITY_SOLID)
			s_SubmenuStyle|=s_Skin.Submenu_thin_frame?WS_BORDER:WS_DLGFRAME;
	}

	if (s_bWin7Style)
	{
		for (int i=MenuSkin::SHUTDOWN_BUTTON;i<=MenuSkin::SHUTDOWN_BUTTON_JUMP;i++)
		{
			const MenuSkin::ItemDrawSettings &settings=s_Skin.ItemSettings[i];
			int textHeight=settings.textMetrics.tmHeight+settings.textPadding.top+settings.textPadding.bottom;
			int iconHeight=0;
			if (s_bHasUpdates)
				iconHeight=s_Skin.Shutdown_bitmap_Size.cy;
			iconHeight+=settings.iconPadding.top+settings.iconPadding.bottom;
			if (iconHeight<textHeight)
			{
				settings.iconTopOffset=(textHeight-iconHeight)/2;
				settings.textTopOffset=0;
				settings.itemHeight=textHeight;
			}
			else
			{
				settings.iconTopOffset=0;
				settings.textTopOffset=(iconHeight-textHeight)/2;
				settings.itemHeight=iconHeight;
			}
		}
	}

	s_HoverTime=GetSettingInt(L"MenuDelay");
	s_SplitHoverTime=((s_HoverTime?s_HoverTime:100)*GetSettingInt(L"SplitMenuDelay"))/100;
	s_ProgramsHoverTime=((s_HoverTime?s_HoverTime:100)*GetSettingInt(L"ProgramsMenuDelay"))/100;

	CMenuContainer *pStartMenu;
	if (bAllPrograms)
		pStartMenu=new CSubMenuContainer(NULL,bAllPrograms?0:-1,options,pRoot,path1,path2);
	else
		pStartMenu=new CMenuContainer(NULL,bAllPrograms?0:-1,options,pRoot,path1,path2);

	HWND owner=NULL;
	if (bAllPrograms)
	{
		APPBARDATA appbar={sizeof(appbar),s_TaskBar};
		SHAppBarMessage(ABM_GETTASKBARPOS,&appbar);
		if (appbar.uEdge==ABE_LEFT || appbar.uEdge==ABE_RIGHT)
			owner=g_TopWin7Menu;
	}

	RECT dummyRc;
	{
		MONITORINFO info={sizeof(MONITORINFO)};
		GetMonitorInfo(initialMonitor,&info);
		RECT rc={info.rcMonitor.left,info.rcMonitor.top,info.rcMonitor.left+100,info.rcMonitor.top+100};
		if (!pStartMenu->Create(owner,&rc,bAllPrograms?s_SubmenuStyle:dwStyle,WS_EX_TOOLWINDOW|((bTopMost || !s_bBehindTaskbar)?WS_EX_TOPMOST:0)|(s_bRTL?WS_EX_LAYOUTRTL:0)))
		{
			delete pStartMenu;
			return NULL;
		}
		dummyRc=rc;
	}

	if (GetSettingBool(L"MenuShadow") && s_Skin.Main_shadow==MenuSkin::SHADOW_ON)
		SetClassLongPtr(pStartMenu->m_hWnd,GCL_STYLE,GetClassLongPtr(pStartMenu->m_hWnd,GCL_STYLE)|CS_DROPSHADOW);
	else
		SetClassLongPtr(pStartMenu->m_hWnd,GCL_STYLE,GetClassLongPtr(pStartMenu->m_hWnd,GCL_STYLE)&~CS_DROPSHADOW);

	if (s_bWin7Style && !s_bAllPrograms && s_Skin.User_bitmapSize.cx>0)
	{
		s_UserPicture.Create(pStartMenu->m_hWnd,NULL,NULL,WS_POPUP,WS_EX_TOOLWINDOW|WS_EX_LAYERED);
		s_UserPicture.Init(pStartMenu);
	}
	dummyRc.right++;
	pStartMenu->SetWindowPos(NULL,&dummyRc,SWP_NOZORDER|SWP_NOACTIVATE);

	memset(&s_StartRect,0,sizeof(s_StartRect));

	HMONITOR s_MenuMonitor=MonitorFromWindow(bAllPrograms?g_ProgramsButton:pStartMenu->m_hWnd,MONITOR_DEFAULTTONEAREST);
	s_bLockWorkArea=true;
	RECT taskbarRect;
	if (initialMonitor!=s_MenuMonitor && s_StartMenuParams.uEdge==0xFFFFFFFF)
	{
		// somebody has moved the menu to another monitor. recalculate the working area and the taskbar orientation
		MONITORINFO info={sizeof(info)};
		GetMonitorInfo(s_MenuMonitor,&info);
		s_MenuLimits=info.rcMonitor;
		s_StartButton=s_TaskBar=NULL;
		if (bAllPrograms)
		{
			::GetWindowRect(g_ProgramsButton,&s_StartRect);
		}
		else
		{
			RECT rc;
			pStartMenu->GetWindowRect(&rc);
			s_StartRect=rc;
			taskbarRect=info.rcMonitor;
			int dx=(rc.left+rc.right-info.rcMonitor.left-info.rcMonitor.right)/2;
			int dy=(rc.top+rc.bottom-info.rcMonitor.top-info.rcMonitor.bottom)/2;
			if (dx<0 && dy<0)
			{
				// top-left corner
				s_StartRect.bottom=s_StartRect.top;
				s_StartRect.right=s_StartRect.left;
				if (rc.top-info.rcMonitor.top>rc.left-info.rcMonitor.left)
				{
					// top
					taskbarRect.bottom=rc.top;
					s_TaskBarEdge=ABE_TOP;
				}
				else
				{
					// left
					taskbarRect.right=rc.left;
					s_TaskBarEdge=ABE_LEFT;
				}
			}
			else if (dx<0 && dy>=0)
			{
				// bottom-left corner
				s_StartRect.top=s_StartRect.bottom;
				s_StartRect.right=s_StartRect.left;
				if (info.rcMonitor.bottom-rc.bottom>rc.left-info.rcMonitor.left)
				{
					// bottom
					taskbarRect.top=rc.bottom;
					s_TaskBarEdge=ABE_BOTTOM;
				}
				else
				{
					// left
					taskbarRect.right=rc.left;
					s_TaskBarEdge=ABE_LEFT;
				}
			}
			else if (dx>=0 && dy<0)
			{
				// top-right corner
				s_StartRect.bottom=s_StartRect.top;
				s_StartRect.left=s_StartRect.right;
				if (rc.top-info.rcMonitor.top>info.rcMonitor.right-rc.right)
				{
					// top
					taskbarRect.bottom=rc.top;
					s_TaskBarEdge=ABE_TOP;
				}
				else
				{
					// right
					taskbarRect.left=rc.right;
					s_TaskBarEdge=ABE_RIGHT;
				}
			}
			else
			{
				// bottom-right corner
				s_StartRect.top=s_StartRect.bottom;
				s_StartRect.left=s_StartRect.right;
				if (info.rcMonitor.bottom-rc.bottom>info.rcMonitor.right-rc.right)
				{
					// bottom
					taskbarRect.top=rc.bottom;
					s_TaskBarEdge=ABE_BOTTOM;
				}
				else
				{
					// right
					taskbarRect.left=rc.right;
					s_TaskBarEdge=ABE_RIGHT;
				}
			}
		}
	}
	else if (s_StartMenuParams.uEdge!=0xFFFFFFFF)
	{
		s_StartButton=s_StartMenuParams.startButton;
		s_TaskBar=s_StartMenuParams.taskbar;
		s_TaskBarEdge=s_StartMenuParams.uEdge;
		s_MenuLimits=s_StartMenuParams.monitorRect;
		s_StartRect=s_StartMenuParams.startButtonRect;
		taskbarRect=s_StartMenuParams.taskbarRect;
	}
	else
	{
		s_bLockWorkArea=false;
		MONITORINFO info={sizeof(info)};
		GetMonitorInfo(s_MenuMonitor,&info);
		s_TaskBarEdge=GetTaskbarPosition(s_TaskBar,NULL,NULL,&taskbarRect);
		RECT rc=taskbarRect;
		LOG_MENU(LOG_OPEN,L"Taskbar Rect: %d, %d, %d, %d",rc.left,rc.top,rc.right,rc.bottom);
		{
			RECT box;
			if (::GetWindowRgnBox(s_TaskBar,&box))
			{
				LOG_MENU(LOG_OPEN,L"Taskbar Region: %d, %d, %d, %d",box.left,box.top,box.right,box.bottom);
				::GetWindowRect(s_TaskBar,&rc);
				OffsetRect(&box,rc.left,rc.top);
				IntersectRect(&rc,&taskbarRect,&box);
			}
		}
		switch (s_TaskBarEdge)
		{
			case ABE_LEFT: rc.right=info.rcWork.right; break;
			case ABE_RIGHT: rc.left=info.rcWork.left; break;
			case ABE_TOP: rc.bottom=info.rcWork.bottom; break;
			case ABE_BOTTOM: rc.top=info.rcWork.top; break;
		}
		IntersectRect(&s_MenuLimits,&rc,&info.rcMonitor);

		if (s_StartButton)
		{
			::GetWindowRect(s_StartButton,&s_StartRect);
		}
		else if (s_TaskBar)
		{
			// no start button. try to guess the rect
			if (taskBar->oldButton)
			{
				::GetWindowRect(taskBar->oldButton,&s_StartRect);
			}
			else
			{
				s_StartRect=taskbarRect;
				if (s_TaskBarEdge==ABE_LEFT || s_TaskBarEdge==ABE_RIGHT)
				{
					::GetWindowRect(taskBar->rebar,&rc);
					s_StartRect.bottom=rc.top;
				}
				else if (::GetWindowLong(s_TaskBar,GWL_EXSTYLE)&WS_EX_LAYOUTRTL)
					s_StartRect.left=s_StartRect.right-(s_StartRect.bottom-s_StartRect.top);
				else
					s_StartRect.right=s_StartRect.left+(s_StartRect.bottom-s_StartRect.top);
			}
		}
	}
	LOG_MENU(LOG_OPEN,L"Main Rect: %d, %d, %d, %d",s_MenuLimits.left,s_MenuLimits.top,s_MenuLimits.right,s_MenuLimits.bottom);
	LOG_MENU(LOG_OPEN,L"Start Rect: %d, %d, %d, %d",s_StartRect.left,s_StartRect.top,s_StartRect.right,s_StartRect.bottom);
	LOG_MENU(LOG_OPEN,L"Start Button: %08X",s_StartButton);

	s_TipShowTime=400;
	s_TipHideTime=4000;
	CString delay=GetSettingString(L"InfotipDelay");
	if (!delay.IsEmpty())
	{
		wchar_t token[256];
		const wchar_t *str=GetToken(delay,token,_countof(token),L", \t");
		int time=_wtol(token);
		if (time>=0) s_TipShowTime=time;
		str=GetToken(str,token,_countof(token),L", \t");
		time=_wtol(token);
		if (time>=0) s_TipHideTime=time;
	}
	if (s_TipShowTime>30000) s_TipShowTime=30000;
	if (s_TipHideTime>30000) s_TipHideTime=30000;

	s_TipHideTimeFolder=s_TipShowTimeFolder=0;
	delay=GetSettingString(L"FolderInfotipDelay");
	if (!delay.IsEmpty())
	{
		wchar_t token[256];
		const wchar_t *str=GetToken(delay,token,_countof(token),L", \t");
		int time=_wtol(token);
		if (time>=0) s_TipShowTimeFolder=time;
		str=GetToken(str,token,_countof(token),L", \t");
		time=_wtol(token);
		if (time>=0) s_TipHideTimeFolder=time;
	}
	if (s_TipShowTimeFolder>30000) s_TipShowTimeFolder=30000;
	if (s_TipHideTimeFolder>30000) s_TipHideTimeFolder=30000;

	DWORD animFlags=0;
	{
		int anim=GetSettingInt(bAllPrograms?L"SubMenuAnimation":L"MainMenuAnimation");
		if (anim==3) animFlags=((rand()<RAND_MAX/2)?AW_BLEND:AW_SLIDE);
		else if (anim==1) animFlags=AW_BLEND;
		else if (anim==2) animFlags=AW_SLIDE;
	}

	s_Skin.Submenu_padding=s_Skin.Submenu_padding0;
	s_Skin.Caption_padding=s_Skin.Caption_padding0;
	s_Skin.Main_padding=s_Skin.Main_padding0;
	s_Skin.Main2_padding=s_Skin.Main2_padding0;
	s_Skin.Main_search_padding=s_Skin.Main_search_padding0;
	s_Skin.Main_jump_padding=s_Skin.Main_jump_padding0;

	{
		RECT margin={0,0,0,0};
		AdjustWindowRect(&margin,s_SubmenuStyle,FALSE);
		s_Skin.Submenu_padding.left+=margin.left; if (s_Skin.Submenu_padding.left<0) s_Skin.Submenu_padding.left=0;
		s_Skin.Submenu_padding.right-=margin.right; if (s_Skin.Submenu_padding.right<0) s_Skin.Submenu_padding.right=0;
		s_Skin.Submenu_padding.top+=margin.top; if (s_Skin.Submenu_padding.top<0) s_Skin.Submenu_padding.top=0;
		s_Skin.Submenu_padding.bottom-=margin.bottom; if (s_Skin.Submenu_padding.bottom<0) s_Skin.Submenu_padding.bottom=0;
	}

	POINT corner;

	memset(&s_MainMenuLimits,0,sizeof(RECT));
	if (bAllPrograms)
	{
		RECT rc;
		if (!::GetWindowRect(g_ProgramsButton,&rc))
			memset(&rc,0,sizeof(rc));
		if (s_StartRect.right+s_StartRect.left<s_MenuLimits.left+s_MenuLimits.right)
		{
			// start button on the left
			options|=CONTAINER_LEFT;
			corner.x=rc.right-s_Skin.Submenu_padding.left+s_Skin.AllPrograms_offset;
			s_bExpandRight=true;
		}
		else
		{
			// start button on the right
			s_bExpandRight=false;
			corner.x=rc.left+s_Skin.Submenu_padding.right-s_Skin.AllPrograms_offset;
		}
		corner.y=rc.bottom;
		options|=CONTAINER_MULTICOLUMN|CONTAINER_MULTICOL_REC;
		pStartMenu->m_Options=options;
		s_MainMenuLimits=s_MenuLimits;
	}
	else
	{
		RECT margin={0,0,0,0};
		AdjustWindowRect(&margin,dwStyle,FALSE);
		if (s_Skin.Main_bitmap_slices_X[1]>0)
		{
			s_Skin.Caption_padding.left+=margin.left; if (s_Skin.Caption_padding.left<0) s_Skin.Caption_padding.left=0;
			s_Skin.Caption_padding.top+=margin.top; if (s_Skin.Caption_padding.top<0) s_Skin.Caption_padding.top=0;
			s_Skin.Caption_padding.bottom-=margin.bottom; if (s_Skin.Caption_padding.bottom<0) s_Skin.Caption_padding.bottom=0;
		}
		else
		{
			// no caption
			s_Skin.Main_padding.left+=margin.left; if (s_Skin.Main_padding.left<0) s_Skin.Main_padding.left=0;
			if (s_Skin.Main2_padding.left>=0)
			{
				s_Skin.Main2_padding.left+=margin.left; if (s_Skin.Main2_padding.left<0) s_Skin.Main2_padding.left=0;
			}
		}
		s_Skin.Main_padding.right-=margin.right; if (s_Skin.Main_padding.right<0) s_Skin.Main_padding.right=0;
		s_Skin.Main_padding.top+=margin.top; if (s_Skin.Main_padding.top<0) s_Skin.Main_padding.top=0;
		s_Skin.Main_padding.bottom-=margin.bottom; if (s_Skin.Main_padding.bottom<0) s_Skin.Main_padding.bottom=0;
		if (s_Skin.Main2_padding.left>=0)
		{
			s_Skin.Main2_padding.right-=margin.right; if (s_Skin.Main2_padding.right<0) s_Skin.Main2_padding.right=0;
			s_Skin.Main2_padding.top+=margin.top; if (s_Skin.Main2_padding.top<0) s_Skin.Main2_padding.top=0;
			s_Skin.Main2_padding.bottom-=margin.bottom; if (s_Skin.Main2_padding.bottom<0) s_Skin.Main2_padding.bottom=0;
		}
		if (s_bWin7Style)
		{
			s_Skin.Main_search_padding.left+=margin.left; if (s_Skin.Main_search_padding.left<0) s_Skin.Main_search_padding.left=0;
			s_Skin.Main_search_padding.right-=margin.right; if (s_Skin.Main_search_padding.right<0) s_Skin.Main_search_padding.right=0;
			s_Skin.Main_search_padding.top+=margin.top; if (s_Skin.Main_search_padding.top<0) s_Skin.Main_search_padding.top=0;
			s_Skin.Main_search_padding.bottom-=margin.bottom; if (s_Skin.Main_search_padding.bottom<0) s_Skin.Main_search_padding.bottom=0;

			s_Skin.Main_jump_padding.right-=margin.right; if (s_Skin.Main_jump_padding.right<0) s_Skin.Main_jump_padding.right=0;
			s_Skin.Main_jump_padding.top+=margin.top; if (s_Skin.Main_jump_padding.top<0) s_Skin.Main_jump_padding.top=0;
			s_Skin.Main_jump_padding.bottom-=margin.bottom; if (s_Skin.Main_jump_padding.bottom<0) s_Skin.Main_jump_padding.bottom=0;
		}

		if (!bTheme)
			memset(&margin,0,sizeof(margin)); // in Classic mode don't offset the main menu by the border size

		// determine options
		if (s_TaskBarEdge==ABE_LEFT || s_TaskBarEdge==ABE_RIGHT || s_TaskBarEdge==ABE_TOP)
		{
			options|=CONTAINER_TOP;
			animFlags|=AW_VER_POSITIVE;
		}
		else
		{
			animFlags|=AW_VER_NEGATIVE;
		}

		if (s_StartRect.right+s_StartRect.left<s_MenuLimits.left+s_MenuLimits.right)
		{
			// start button on the left
			options|=CONTAINER_LEFT;
			s_bExpandRight=true;
		}
		else
		{
			// start button on the right
			s_bExpandRight=false;
		}

		if ((s_TaskBarEdge==ABE_LEFT || s_TaskBarEdge==ABE_RIGHT) && !GetSettingBool(L"ShowNextToTaskbar"))
			s_bBehindTaskbar=false;

		if (GetSettingBool(L"MainSortZA")) options|=CONTAINER_SORTZA;
		if (GetSettingBool(L"MainSortOnce")) options|=CONTAINER_SORTONCE;
		if (s_RecentPrograms!=RECENT_PROGRAMS_NONE && !(rootSettings&StdMenuItem::MENU_NORECENT))
			options|=CONTAINER_RECENT;
		pStartMenu->m_Options=options;

		s_MainMenuLimits=pStartMenu->CalculateWorkArea(taskbarRect);
		corner=pStartMenu->CalculateCorner();
	}

	// reposition start menu
	if (bTopMost || !s_bBehindTaskbar)
		animFlags|=AW_TOPMOST;
	pStartMenu->SetWindowPos((animFlags&AW_TOPMOST)?HWND_TOPMOST:HWND_TOP,corner.x,corner.y,0,0,((initialMonitor!=s_MenuMonitor && !bAllPrograms)?SWP_NOMOVE|SWP_NOSIZE:0)|SWP_NOACTIVATE);

	pStartMenu->InitItems();
	pStartMenu->m_MaxWidth=s_MainMenuLimits.right-s_MainMenuLimits.left;
	g_ItemManager.UpdateNewPrograms(CPoint((s_StartRect.left+s_StartRect.right)/2,(s_StartRect.top+s_StartRect.bottom)/2));

	pStartMenu->InitWindow();
	pStartMenu->SetHotItem((bKeyboard && bAllPrograms)?0:-1);
	bool bTreeSelected=false;
	if (s_bWin7Style && GetSettingInt(L"ProgramsStyle")==PROGRAMS_INLINE && GetSettingBool(L"OpenPrograms"))
	{
		pStartMenu->SetMenuMode(MODE_PROGRAMS);
		if (pStartMenu->m_SearchIndex<0 || GetSettingInt(L"SearchBox")!=SEARCHBOX_NORMAL || !GetSettingBool(L"SearchSelect"))
		{
			bTreeSelected=true;
			pStartMenu->SetHotItem(pStartMenu->m_ProgramTreeIndex,false,false);
		}
	}

	if (bAllPrograms)
	{
		::InvalidateRect(g_ProgramsButton,NULL,TRUE);
		::UpdateWindow(g_ProgramsButton);
	}

	BOOL animate;
	if ((animFlags&(AW_BLEND|AW_SLIDE))==0)
		animate=FALSE;
	else
		SystemParametersInfo(SPI_GETMENUANIMATION,NULL,&animate,0);

	if (s_TaskBar && s_bBehindTaskbar)
		::SetWindowPos(s_TaskBar,bTopMost?HWND_TOPMOST:HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE); // bring the start button on top

	int speed=0;
	if (animate)
	{
		speed=GetSettingInt(bAllPrograms?L"SubMenuAnimationSpeed":L"MainMenuAnimationSpeed");
		if (speed<=0) speed=MENU_ANIM_SPEED;
		else if (speed>=10000) speed=10000;
	}
	{
		RECT rc;
		pStartMenu->GetWindowRect(&rc);
		pStartMenu->AnimateMenu(animFlags,speed,rc);
	}

	s_bOverrideFirstDown=false;
	if (pStartMenu->m_SearchIndex>=0 && GetSettingInt(L"SearchBox")==SEARCHBOX_NORMAL && GetSettingBool(L"SearchSelect"))
	{
		pStartMenu->ActivateItem(pStartMenu->m_SearchIndex,ACTIVATE_SELECT,NULL);
		if (pStartMenu->m_bTwoColumns && pStartMenu->m_Items[pStartMenu->m_SearchIndex].column==0 && pStartMenu->m_SearchIndex+1<(int)pStartMenu->m_Items.size() && pStartMenu->m_Items[pStartMenu->m_SearchIndex+1].column==1)
			s_bOverrideFirstDown=true;
	}
	else if (!bTreeSelected)
	{
		pStartMenu->SetFocus();
		if (!bAllPrograms)
			pStartMenu->SetHotItem(-1);
	}
	SetForegroundWindow(pStartMenu->m_hWnd);
	SwitchToThisWindow(pStartMenu->m_hWnd,FALSE); // just in case
	if (s_TaskBar && s_bBehindTaskbar)
	{
		// position the start button on top
		if (s_StartButton)
			::SetWindowPos(s_StartButton,bTopMost?HWND_TOPMOST:HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
		// position the start menu behind the taskbar
		pStartMenu->SetWindowPos(s_TaskBar,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
	}
	if (bErr && GetSettingBool(L"ReportSkinErrors") && !*MenuSkin::s_SkinError)
	{
		Strcpy(MenuSkin::s_SkinError,_countof(MenuSkin::s_SkinError),LoadStringEx(IDS_SKIN_ERR_UNKNOWN));
	}
	if (*MenuSkin::s_SkinError && GetSettingBool(L"ReportSkinErrors") && s_StartButton)
	{
		Strcat(MenuSkin::s_SkinError,_countof(MenuSkin::s_SkinError),LoadStringEx(IDS_SKIN_ERR_DISABLE));
		s_TooltipBalloon=CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|(s_bRTL?WS_EX_LAYOUTRTL:0),TOOLTIPS_CLASS,NULL,WS_POPUP|TTS_BALLOON|TTS_CLOSE|TTS_NOPREFIX,0,0,0,0,pStartMenu->m_hWnd,NULL,g_Instance,NULL);
		s_TooltipBalloon.SendMessage(TTM_SETMAXTIPWIDTH,0,500);
		TOOLINFO tool={sizeof(tool),TTF_TRANSPARENT|TTF_TRACK|(s_bRTL?TTF_RTLREADING:0U)};
		tool.uId=1;
		tool.lpszText=MenuSkin::s_SkinError;
		s_TooltipBalloon.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);
		if (bErr)
		{
			s_TooltipBalloon.SendMessage(TTM_SETTITLE,TTI_ERROR,(LPARAM)(const wchar_t*)LoadStringEx(IDS_SKIN_ERR));
		}
		else
		{
			s_TooltipBalloon.SendMessage(TTM_SETTITLE,TTI_WARNING,(LPARAM)(const wchar_t*)LoadStringEx(IDS_SKIN_WARN));
		}
		RECT rc;
		::GetWindowRect(s_StartButton,&rc);
		s_TooltipBalloon.SendMessage(TTM_TRACKPOSITION,0,MAKELONG((rc.left+rc.right)/2,(rc.top+rc.bottom)/2));
		s_TooltipBalloon.SendMessage(TTM_TRACKACTIVATE,TRUE,(LPARAM)&tool);
		pStartMenu->SetTimer(TIMER_BALLOON_HIDE,10000);
	}
	CheckForNewVersion(NULL,COMPONENT_MENU,CHECK_AUTO,NewVersionCallback);

	return pStartMenu->m_hWnd;
}

bool CMenuContainer::ProcessMouseMessage( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	if (uMsg==WM_MOUSEMOVE)
	{
		if (!s_bAllPrograms)
			return false;
		if (hwnd && hwnd==g_ProgramsButton)
			return true;
		for (std::vector<CMenuContainer*>::const_iterator it=s_Menus.begin();it!=s_Menus.end();++it)
			if ((*it)->m_hWnd==hwnd && (*it)->m_ContextItem<0)
			{
				(*it)->SendMessage(WM_MOUSEMOVE,wParam,lParam);
				return true;
			}
	}
	if (uMsg==WM_MOUSEHOVER)
	{
		if (hwnd && hwnd==g_ProgramsButton && GetSettingBool(L"CascadeAll"))
			return true;
		if (!s_bAllPrograms)
			return false;
		for (std::vector<CMenuContainer*>::const_iterator it=s_Menus.begin();it!=s_Menus.end();++it)
			if ((*it)->m_hWnd==hwnd)
				return false;
		// if the mouse hovers over some window, close the menus
		CloseSubMenus(0,NULL);
		::ShowWindow(g_UserPic,SW_SHOW);
		CPoint pt(GetMessagePos());
		RECT rc;
		::GetWindowRect(g_TopWin7Menu,&rc);
		if (PtInRect(&rc,pt))
		{
			::ScreenToClient(g_TopWin7Menu,&pt);
			::PostMessage(g_TopWin7Menu,WM_MOUSEMOVE,0,MAKELONG(pt.x,pt.y));
		}
		return false;
	}
	return false;
}

// Win7 stuff
void CMenuContainer::SetMenuMode( TMenuMode mode, bool bKeyboard )
{
	if (mode==s_MenuMode) return;
	InitializeOldState();

	if (mode==MODE_SEARCH)
	{
		// switch to search - delete all additional items
		ClearItems(m_Items.begin()+m_OriginalCount,m_Items.end());
	}
	if (s_MenuMode==MODE_SEARCH)
	{
		// switch from search - restore scroll count
		m_ScrollCount=m_OriginalScrollCount;
	}

	s_MenuMode=mode;

	s_bOverrideFirstDown=false;
	if (s_OldMenuState.mode==MODE_PROGRAMS)
	{
		// hide programs tree
		m_pProgramsTree->ShowWindow(SW_HIDE);
		s_ProgramsScrollPos=m_pProgramsTree->GetScrollPos(SB_VERT);
	}

	if (s_MenuMode==MODE_JUMPLIST)
	{
		// from any to jumplist
		InitWindow();
		if (bKeyboard)
		{
			for (int i=m_OriginalCount;i<(int)m_Items.size();i++)
			{
				if (m_Items[i].jumpIndex>=0)
				{
					SetHotItem(i);
					break;
				}
			}
		}
		else
			SetHotItem(-1);
	}
	else if (s_MenuMode==MODE_SEARCH)
	{
		// from any to search
		InitWindow();
		s_SearchMenu=m_hWnd;
	}
	else if (s_OldMenuState.mode==MODE_JUMPLIST || s_OldMenuState.mode==MODE_SEARCH)
	{
		// from search and jumplist to program or normal
		if (m_bRefreshItems)
			InitItems();
		else
			ClearItems(m_Items.begin()+m_OriginalCount,m_Items.end());
		s_JumpAppInfo=NULL;
		int hotItem=m_HotItem;
		InitWindow();
		if (s_UserPicture.m_hWnd && s_UserPictureRect.top<s_UserPictureRect.bottom)
		{
			s_UserPicture.SetWindowPos(NULL,&s_UserPictureRect,SWP_NOZORDER|SWP_NOACTIVATE);
			s_UserPicture.Update();
		}
		if (s_OldMenuState.mode==MODE_JUMPLIST && m_SubJumpItem!=-1)
		{
			if (bKeyboard || m_HotItem>=m_OriginalCount)
			{
				s_OldMenuState.hotItem=m_SubJumpItem;
				s_OldMenuState.bHotArrow=true;
				SetHotItem(m_SubJumpItem);
			}
			else
				SetHotItem(hotItem);
		}
		m_SubJumpItem=-1;
		s_SearchMenu=NULL;
	}
	else
	{
		InvalidateRect(&m_rContent);
	}

	if (s_MenuMode==MODE_PROGRAMS)
	{
		for (std::vector<CMenuContainer*>::reverse_iterator it=s_Menus.rbegin();*it!=this;++it)
			if (!(*it)->m_bDestroyed)
				(*it)->PostMessage(WM_CLOSE);

		// initialize programs tree
		MenuItem &item=m_Items[m_ProgramTreeIndex];
		item.itemRect=m_rContent;
		item.itemRect.bottom=m_Items[m_ProgramTreeIndex+1].itemRect.top;
		bool bNewTree=!m_pProgramsTree;
		if (bNewTree)
		{
			m_pProgramsTree=new CProgramsTree();
			m_pProgramsTree->Create(this);
			m_pProgramsTree->CreateItems();
		}
		SetHotItem(m_ProgramTreeIndex);
		RECT rc;
		m_pProgramsTree->GetWindowRect(&rc);
		int dh=rc.bottom-rc.top;
		m_pProgramsTree->GetClientRect(&rc);
		dh-=rc.bottom;
		int itemHeight=TreeView_GetItemHeight(m_pProgramsTree->m_hWnd);
		rc=item.itemRect;
		int h=rc.bottom-rc.top;
		int n=(h-dh)/itemHeight;
		rc.bottom=rc.top+n*itemHeight+dh;
		m_pProgramsTree->SetWindowPos(NULL,&rc,SWP_NOZORDER|SWP_NOACTIVATE);
		if (bNewTree)
		{
			for (int i=0;i<s_ProgramsScrollPos;i++)
				m_pProgramsTree->SendMessage(WM_VSCROLL,SB_LINEDOWN); // using SetSrollPos doesn't quite work because the contents are not scrolled during WM_PRINT
		}

		ULONGLONG curTime;
		GetSystemTimeAsFileTime((FILETIME*)&curTime);
		CRegKey regKey;
		if (regKey.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\StarMenu",KEY_WRITE)!=ERROR_SUCCESS)
			regKey.Create(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu");
		regKey.SetQWORDValue(L"LastProgramsTime",curTime);
		if (s_OldMenuState.mode!=MODE_SEARCH)
			m_pProgramsTree->SetFocus();
		TreeView_SelectItem(m_pProgramsTree->m_hWnd,NULL);
	}

	if (m_Bitmap)
	{
		DeleteObject(m_Bitmap);
		m_Bitmap=NULL;
	}
	if (s_UserPicture.m_hWnd && s_MenuMode!=MODE_NORMAL && s_MenuMode!=MODE_PROGRAMS)
		s_UserPicture.SetWindowPos(NULL,0,0,0,0,SWP_NOZORDER|SWP_NOSIZE|SWP_NOMOVE|SWP_HIDEWINDOW|SWP_NOACTIVATE);
	if (!m_bClosing && s_Skin.Main_opacity!=MenuSkin::OPACITY_SOLID && GetSettingBool(L"MainMenuAnimate") && IsWindowVisible())
		AnimateBackground();
	else
	{
		int tw, th;
		CreateBackground(s_BackgroundW1,s_BackgroundW2,s_BackgroundH1,s_BackgroundH2,tw,th,true);
		OffsetRect(&m_rContent,m_BitmapOffset,0);
		OffsetRect(&m_rContent2,m_BitmapOffset,0);
		if (m_Region && !s_bRTL)
			OffsetRgn(m_Region,m_BitmapOffset,0);
		if (m_SearchBox.m_hWnd && m_SearchIndex>=0)
		{
			RECT itemRect;
			GetItemRect(m_SearchIndex,itemRect);
			itemRect.right-=(itemRect.bottom-itemRect.top);
			if (!s_Skin.Search_frame)
				InflateRect(&itemRect,-1,-3);
			m_SearchBox.SetWindowPos(NULL,&itemRect,SWP_NOZORDER);
		}
		Invalidate();
		ApplyRegion(TRUE);
	}

	if (s_MenuMode==MODE_PROGRAMS)
	{
		// show programs tree
		m_pProgramsTree->ShowWindow(SW_SHOW);
		if (s_OldMenuState.mode!=MODE_SEARCH)
			m_pProgramsTree->SetFocus();
		m_pProgramsTree->RedrawWindow();
	}
	if (s_UserPicture.m_hWnd && (s_MenuMode==MODE_NORMAL || s_MenuMode==MODE_PROGRAMS) && !m_bClosing && IsWindowVisible())
		s_UserPicture.SetWindowPos(NULL,0,0,0,0,SWP_NOZORDER|SWP_NOSIZE|SWP_NOMOVE|SWP_SHOWWINDOW|SWP_NOACTIVATE);
	ClearOldState();
}

void CMenuContainer::AnimateBackground( void )
{
	TMenuMode newMode=s_MenuMode;
	int newHotItem=m_HotItem;
	bool bNewHotArrow=m_bHotArrow;
	int width1=s_OldMenuState.mode==MODE_JUMPLIST?s_MenuWidthJump:s_MenuWidthNormal;
	int width2=s_MenuMode==MODE_JUMPLIST?s_MenuWidthJump:s_MenuWidthNormal;

	int totalDW=width2-width1;
	int totalDX=m_BitmapOffset-s_OldMenuState.bitmapOffset;
	m_BitmapOffset=s_OldMenuState.bitmapOffset;
	m_rMenu.left=m_BitmapOffset;
	m_rMenu.right=m_rMenu.left+width1;

	for (std::vector<MenuItem>::iterator it=m_Items.begin();it!=m_Items.end();++it)
	{
		if (it->column==1 && it->id!=MENU_SHUTDOWN_BUTTON)
			it->itemRect.right-=totalDW;
		OffsetRect(&it->itemRect,-totalDX,0);
	}

	LARGE_INTEGER time0;
	QueryPerformanceCounter(&time0);
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);

	unsigned int *bits1=NULL, *bits2=NULL;
	HBITMAP bitmap1=NULL, bitmap2=NULL;

	int oldDW=0, oldDX=0;
	HDC hDst=CreateCompatibleDC(NULL);
	if (s_bRTL) SetLayout(hDst,LAYOUT_RTL);
	HGDIOBJ bmp0=GetCurrentObject(hDst,OBJ_BITMAP);
	int step=25;
	for (s_OldMenuState.blend=step;s_OldMenuState.blend<100;s_OldMenuState.blend+=step)
	{
		int dx=(totalDX*s_OldMenuState.blend)/100;
		int dw=(totalDW*s_OldMenuState.blend)/100;

		int ddw=dw-oldDW; oldDW=dw;
		int ddx=dx-oldDX; oldDX=dx;

		for (std::vector<MenuItem>::iterator it=m_Items.begin();it!=m_Items.end();++it)
		{
			if (it->column==1 && it->id!=MENU_SHUTDOWN_BUTTON)
				it->itemRect.right+=ddw;
			OffsetRect(&it->itemRect,ddx,0);
		}

		for (std::vector<MenuItem>::iterator it=s_OldMenuState.items.begin();it!=s_OldMenuState.items.end();++it)
		{
			if (it->column==1 && it->id!=MENU_SHUTDOWN_BUTTON)
				it->itemRect.right+=ddw;
			OffsetRect(&it->itemRect,ddx,0);
		}

		if (ddx && s_MenuMode==MODE_PROGRAMS)
		{
			RECT rc;
			m_pProgramsTree->GetWindowRect(&rc);
			::MapWindowPoints(NULL,m_hWnd,(POINT*)&rc,2);
			m_pProgramsTree->SetWindowPos(NULL,rc.left+ddx,rc.top,0,0,SWP_NOZORDER|SWP_NOSIZE);
		}
		if (m_SearchBox.m_hWnd && m_SearchIndex>=0)
		{
			RECT itemRect;
			GetItemRect(m_SearchIndex,itemRect);
			itemRect.right-=(itemRect.bottom-itemRect.top);
			if (!s_Skin.Search_frame)
				InflateRect(&itemRect,-1,-3);
			m_SearchBox.SetWindowPos(NULL,&itemRect,SWP_NOZORDER);
		}

		m_BitmapOffset+=ddx;
		m_rMenu.left=m_BitmapOffset;
		m_rMenu.right=m_rMenu.left+width1+dw;

		BITMAPINFO dib={sizeof(dib)};
		dib.bmiHeader.biWidth=(m_rMenu.right-m_rMenu.left);
		dib.bmiHeader.biHeight=-(m_rMenu.bottom-m_rMenu.top);
		dib.bmiHeader.biPlanes=1;
		dib.bmiHeader.biBitCount=32;
		dib.bmiHeader.biCompression=BI_RGB;

		// draw old menu
		s_MenuMode=s_OldMenuState.mode;
		m_HotItem=s_OldMenuState.hotItem;
		m_bHotArrow=s_OldMenuState.bHotArrow;
		m_Items.swap(s_OldMenuState.items);

		int tw, th;
		HBITMAP bmp1=bitmap1;
		RECT rContentA1, rContentB1;
		if (!bmp1)
		{
			Assert(!m_Bitmap);
			CreateBackground(s_OldMenuState.w1,s_OldMenuState.w2+dw,s_OldMenuState.h1,s_OldMenuState.h2,tw,th,false);
			Assert(tw==m_rMenu.right-m_rMenu.left && th==m_rMenu.bottom-m_rMenu.top);
			rContentA1=m_rContent;
			rContentB1=m_rContent2;
			OffsetRect(&m_rContent,m_BitmapOffset,0);
			OffsetRect(&m_rContent2,m_BitmapOffset,0);

			bmp1=CreateDIBSection(hDst,&dib,DIB_RGB_COLORS,(void**)&bits1,NULL,0);
			SelectObject(hDst,bmp1);
			SetViewportOrgEx(hDst,-m_BitmapOffset,0,NULL);
			DrawBackground(hDst,m_rMenu);
			if (s_MenuMode==MODE_PROGRAMS)
			{
				RECT rc;
				m_pProgramsTree->GetWindowRect(&rc);
				::MapWindowPoints(NULL,m_hWnd,(POINT*)&rc,2);
				SetViewportOrgEx(hDst,rc.left-m_BitmapOffset,rc.top,NULL);
				m_pProgramsTree->GetClientRect(&rc);
				m_pProgramsTree->DrawTree(hDst,rc);
				m_pProgramsTree->Print(hDst,PRF_NONCLIENT);
			}
			if (m_Bitmap)
				DeleteObject(m_Bitmap);
			m_Bitmap=NULL;
			SetViewportOrgEx(hDst,0,0,NULL);
			SelectObject(hDst,bmp0);
			if (totalDW==0)
				bitmap1=bmp1;
		}
		else
		{
			CreateContentRects(s_OldMenuState.w1,s_OldMenuState.w2+dw,s_OldMenuState.h1,s_OldMenuState.h2,tw,th);
			rContentA1=m_rContent;
			rContentB1=m_rContent2;
			OffsetRect(&m_rContent,m_BitmapOffset,0);
			OffsetRect(&m_rContent2,m_BitmapOffset,0);
		}
		bool bOpaqueA1=(s_Skin.Main_opacity==MenuSkin::OPACITY_GLASS || s_Skin.Main_opacity==MenuSkin::OPACITY_ALPHA);
		bool bOpaqueB1=(s_Skin.Main2_opacity==MenuSkin::OPACITY_GLASS || s_Skin.Main2_opacity==MenuSkin::OPACITY_ALPHA);
		if (s_MenuMode==MODE_JUMPLIST)
			bOpaqueB1=(s_Skin.Jumplist_opacity==MenuSkin::OPACITY_GLASS || s_Skin.Jumplist_opacity==MenuSkin::OPACITY_ALPHA);

		// draw new menu
		s_MenuMode=newMode;
		m_HotItem=newHotItem;
		m_bHotArrow=bNewHotArrow;
		m_Items.swap(s_OldMenuState.items);

		HBITMAP bmp2=bitmap2;
		RECT rContentA2, rContentB2;
		if (!bmp2)
		{
			if (s_BackgroundW2>0)
				CreateBackground(s_BackgroundW1,s_BackgroundW2+dw-totalDW,s_BackgroundH1,s_BackgroundH2,tw,th,true);
			else
				CreateBackground(s_BackgroundW1+dw-totalDW,s_BackgroundW2,s_BackgroundH1,s_BackgroundH2,tw,th,true);
			Assert(tw==m_rMenu.right-m_rMenu.left && th==m_rMenu.bottom-m_rMenu.top);
			rContentA2=m_rContent;
			rContentB2=m_rContent2;
			OffsetRect(&m_rContent,m_BitmapOffset,0);
			OffsetRect(&m_rContent2,m_BitmapOffset,0);

			bmp2=CreateDIBSection(hDst,&dib,DIB_RGB_COLORS,(void**)&bits2,NULL,0);
			SelectObject(hDst,bmp2);
			SetViewportOrgEx(hDst,-m_BitmapOffset,0,NULL);
			DrawBackground(hDst,m_rMenu);
			if (s_MenuMode==MODE_PROGRAMS)
			{
				RECT rc;
				m_pProgramsTree->GetWindowRect(&rc);
				::MapWindowPoints(NULL,m_hWnd,(POINT*)&rc,2);
				SetViewportOrgEx(hDst,rc.left-m_BitmapOffset,rc.top,NULL);
				m_pProgramsTree->GetClientRect(&rc);
				m_pProgramsTree->DrawTree(hDst,rc);
				m_pProgramsTree->Print(hDst,PRF_NONCLIENT);
			}
			if (m_Bitmap)
				DeleteObject(m_Bitmap);
			m_Bitmap=NULL;
			SetViewportOrgEx(hDst,0,0,NULL);
			SelectObject(hDst,bmp0);
			if (totalDW==0)
				bitmap2=bmp2;

			if (m_Region && !s_bRTL)
				OffsetRgn(m_Region,m_BitmapOffset,0);
		}
		else
		{
			if (s_BackgroundW2>0)
				CreateContentRects(s_BackgroundW1,s_BackgroundW2+dw-totalDW,s_BackgroundH1,s_BackgroundH2,tw,th);
			else
				CreateContentRects(s_BackgroundW1+dw-totalDW,s_BackgroundW2,s_BackgroundH1,s_BackgroundH2,tw,th);
			rContentA2=m_rContent;
			rContentB2=m_rContent2;
			OffsetRect(&m_rContent,m_BitmapOffset,0);
			OffsetRect(&m_rContent2,m_BitmapOffset,0);
		}

		bool bOpaqueA2=(s_Skin.Main_opacity==MenuSkin::OPACITY_GLASS || s_Skin.Main_opacity==MenuSkin::OPACITY_ALPHA);
		bool bOpaqueB2=(s_Skin.Main2_opacity==MenuSkin::OPACITY_GLASS || s_Skin.Main2_opacity==MenuSkin::OPACITY_ALPHA);
		if (s_MenuMode==MODE_JUMPLIST)
			bOpaqueB2=(s_Skin.Jumplist_opacity==MenuSkin::OPACITY_GLASS || s_Skin.Jumplist_opacity==MenuSkin::OPACITY_ALPHA);

		if (s_bRTL)
		{
			int q;
			q=rContentA1.right;
			rContentA1.right=dib.bmiHeader.biWidth-rContentA1.left;
			rContentA1.left=dib.bmiHeader.biWidth-q;
			q=rContentB1.right;
			rContentB1.right=dib.bmiHeader.biWidth-rContentB1.left;
			rContentB1.left=dib.bmiHeader.biWidth-q;
			q=rContentA2.right;
			rContentA2.right=dib.bmiHeader.biWidth-rContentA2.left;
			rContentA2.left=dib.bmiHeader.biWidth-q;
			q=rContentB2.right;
			rContentB2.right=dib.bmiHeader.biWidth-rContentB2.left;
			rContentB2.left=dib.bmiHeader.biWidth-q;
		}

		Assert(!m_Bitmap);
		unsigned int *bits3;
		m_Bitmap=CreateDIBSection(hDst,&dib,DIB_RGB_COLORS,(void**)&bits3,NULL,0);

		// blend
		int blend2=s_OldMenuState.blend;
		int blend1=100-blend2;
		for (int y=0,i=0;y<-dib.bmiHeader.biHeight;y++)
		{
			for (int x=0;x<dib.bmiHeader.biWidth;x++,i++)
			{
				unsigned int pixel1=bits1[i];
				int r1=pixel1&255;
				int g1=(pixel1>>8)&255;
				int b1=(pixel1>>16)&255;
				int a1=pixel1>>24;
				if ((bOpaqueA1 && x>=rContentA1.left && x<rContentA1.right && y>=rContentA1.top && y<rContentA1.bottom) ||
						(bOpaqueB1 && x>=rContentB1.left && x<rContentB1.right && y>=rContentB1.top && y<rContentB1.bottom))
					a1=255;

				unsigned int pixel2=bits2[i];
				int r2=pixel2&255;
				int g2=(pixel2>>8)&255;
				int b2=(pixel2>>16)&255;
				int a2=pixel2>>24;
				if ((bOpaqueA2 && x>=rContentA2.left && x<rContentA2.right && y>=rContentA2.top && y<rContentA2.bottom) ||
						(bOpaqueB2 && x>=rContentB2.left && x<rContentB2.right && y>=rContentB2.top && y<rContentB2.bottom))
					a2=255;

				r2=(r1*blend1+r2*blend2)/100;
				g2=(g1*blend1+g2*blend2)/100;
				b2=(b1*blend1+b2*blend2)/100;
				a2=(a1*blend1+a2*blend2)/100;
				bits3[i]=(a2<<24)|(b2<<16)|(g2<<8)|r2;
			}
		}
		if (!bitmap1) DeleteObject(bmp1);
		if (!bitmap2) DeleteObject(bmp2);

		// draw
		Invalidate();
		ApplyRegion(TRUE);
		UpdateWindow();
		DeleteObject(m_Bitmap);
		m_Bitmap=NULL;

		LARGE_INTEGER time;
		QueryPerformanceCounter(&time);
		LONGLONG dt=(time.QuadPart-time0.QuadPart)*1000/freq.QuadPart;
		if (dt<33)
			Sleep(33-(int)dt);
		time0=time;
	}
	SelectObject(hDst,bmp0);
	DeleteDC(hDst);
	if (bitmap1) DeleteObject(bitmap1);
	if (bitmap2) DeleteObject(bitmap2);

	int ddw=totalDW-oldDW;
	int ddx=totalDX-oldDX;
	for (std::vector<MenuItem>::iterator it=m_Items.begin();it!=m_Items.end();++it)
	{
		if (it->column==1 && it->id!=MENU_SHUTDOWN_BUTTON)
			it->itemRect.right+=ddw;
		OffsetRect(&it->itemRect,ddx,0);
	}
	if (ddx && s_MenuMode==MODE_PROGRAMS)
	{
		RECT rc;
		m_pProgramsTree->GetWindowRect(&rc);
		::MapWindowPoints(NULL,m_hWnd,(POINT*)&rc,2);
		m_pProgramsTree->SetWindowPos(NULL,rc.left+ddx,rc.top,0,0,SWP_NOZORDER|SWP_NOSIZE);
	}
	if (m_SearchBox.m_hWnd && m_SearchIndex>=0)
	{
		RECT itemRect;
		GetItemRect(m_SearchIndex,itemRect);
		itemRect.right-=(itemRect.bottom-itemRect.top);
		if (!s_Skin.Search_frame)
			InflateRect(&itemRect,-1,-3);
		m_SearchBox.SetWindowPos(NULL,&itemRect,SWP_NOZORDER);
	}

	m_BitmapOffset+=ddx;
	m_rMenu.left=m_BitmapOffset;
	m_rMenu.right=m_rMenu.left+width2;
	int tw, th;
	CreateBackground(s_BackgroundW1,s_BackgroundW2,s_BackgroundH1,s_BackgroundH2,tw,th,true);
	OffsetRect(&m_rContent,m_BitmapOffset,0);
	OffsetRect(&m_rContent2,m_BitmapOffset,0);
	if (m_Region && !s_bRTL)
		OffsetRgn(m_Region,m_BitmapOffset,0);

	Invalidate();
	ApplyRegion(TRUE);
}

void CMenuContainer::InitializeOldState( void )
{
	s_OldMenuState.mode=s_MenuMode;
	s_OldMenuState.w1=s_BackgroundW1;
	s_OldMenuState.w2=s_BackgroundW2;
	s_OldMenuState.h1=s_BackgroundH1;
	s_OldMenuState.h2=s_BackgroundH2;
	s_OldMenuState.bitmapOffset=m_BitmapOffset;
	s_OldMenuState.hotItem=m_HotItem;
	s_OldMenuState.bHotArrow=m_bHotArrow;
	s_OldMenuState.items.resize(m_Items.size());
	for (size_t i=0;i<m_Items.size();i++)
	{
		s_OldMenuState.items[i].id=m_Items[i].id;
		s_OldMenuState.items[i].name=m_Items[i].name;
		s_OldMenuState.items[i].pItemInfo=m_Items[i].pItemInfo;
		s_OldMenuState.items[i].drawType=m_Items[i].drawType;
		s_OldMenuState.items[i].column=m_Items[i].column;
		s_OldMenuState.items[i].row=m_Items[i].row;
		s_OldMenuState.items[i].itemRect=m_Items[i].itemRect;
		s_OldMenuState.items[i].bFolder=m_Items[i].bFolder;
		s_OldMenuState.items[i].bSplit=m_Items[i].bSplit;
		s_OldMenuState.items[i].bNew=m_Items[i].bNew;
	}
}

void CMenuContainer::ClearOldState( void )
{
	s_OldMenuState.mode=MODE_UNKNOWN;
	s_OldMenuState.items.clear();
}

void CMenuContainer::OpenJumpList( int index, bool bKeyboard )
{
	if (s_MenuMode==MODE_JUMPLIST)
	{
		InitializeOldState();
	}

	ClearItems(m_Items.begin()+m_OriginalCount,m_Items.end());
	Assert(m_Items[index].bHasJumpList);
	s_JumpAppInfo=m_Items[index].pItemInfo;
	m_SubJumpItem=index;
	AddJumpListItems(m_Items);
	UpdateAccelerators(m_OriginalCount,(int)m_Items.size());

	// add jumplist contents
	if (s_MenuMode==MODE_JUMPLIST)
	{
		InitWindow();
		AnimateBackground();
		ClearOldState();
		if (bKeyboard)
		{
			for (int i=m_OriginalCount;i<(int)m_Items.size();i++)
			{
				if (m_Items[i].jumpIndex>=0)
				{
					SetHotItem(i);
					break;
				}
			}
		}
		else
			SetHotItem(-1);
	}
	else
		SetMenuMode(MODE_JUMPLIST,bKeyboard);
}

void CMenuContainer::OpenSearchList( void )
{
	s_PreSearchMenuMode=s_MenuMode==MODE_PROGRAMS?MODE_PROGRAMS:MODE_NORMAL;
	SetMenuMode(MODE_SEARCH);
}
