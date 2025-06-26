// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include "ClassicIEDLL.h"
#include "Settings.h"
#include "ResourceHelper.h"
#include "SettingsUIHelper.h"
#include <vssym32.h>
#include <dwmapi.h>

static _declspec(thread) SIZE g_SysButtonSize; // the size of the system buttons (close, minimize) for this thread's window
static WNDPROC g_OldClassCaptionProc;
static HBITMAP g_GlowBmp;
static HBITMAP g_GlowBmpMax;
static LONG g_bInjected; // the process is injected
static int g_DPI;
static UINT g_Message; // private message to detect if the caption is subclassed
static ATOM g_SubclassAtom;

struct CustomCaption
{
	int leftPadding;
	int topPadding;
	int iconPadding;
};

static CustomCaption g_CustomCaption[3]={
	{2,3,10}, // Aero
	{4,2,10}, // Aero maximized
	{4,2,10}, // Basic
};

void GetSysButtonSize( HWND hWnd )
{
	TITLEBARINFOEX titleInfo={sizeof(titleInfo)};
	SendMessage(hWnd,WM_GETTITLEBARINFOEX,0,(LPARAM)&titleInfo);
	int buttonLeft=titleInfo.rgrect[2].left;
	if (buttonLeft>titleInfo.rgrect[5].left) buttonLeft=titleInfo.rgrect[5].left;
	int buttonRight=titleInfo.rgrect[2].right;
	if (buttonRight<titleInfo.rgrect[5].right) buttonRight=titleInfo.rgrect[5].right;

	int w=buttonRight-buttonLeft;
	int h=titleInfo.rgrect[5].bottom-titleInfo.rgrect[5].top;
	g_SysButtonSize.cx=w;
	g_SysButtonSize.cy=h;
}

// Subclasses the main IE frame to redraw the caption when the text changes
static LRESULT CALLBACK SubclassFrameProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	if (uMsg==g_Message)
	{
		GetSysButtonSize(hWnd);
		HWND caption=FindWindowEx(hWnd,NULL,L"Client Caption",NULL);
		if (caption)
			InvalidateRect(caption,NULL,FALSE);
		return 0;
	}
	if (uMsg==WM_SETTEXT || uMsg==WM_ACTIVATE)
	{
		HWND caption=FindWindowEx(hWnd,NULL,L"Client Caption",NULL);
		if (caption)
			InvalidateRect(caption,NULL,FALSE);
	}
	if (uMsg==WM_SIZE || uMsg==WM_SETTINGCHANGE)
	{
		GetSysButtonSize(hWnd);
		if (uMsg==WM_SETTINGCHANGE)
		{
			CSettingsLockWrite lock;
			UpdateSettings();
		}
	}
	while (1)
	{
		WNDPROC proc=(WNDPROC)GetProp(hWnd,MAKEINTATOM(g_SubclassAtom));
		if (proc)
			return CallWindowProc(proc,hWnd,uMsg,wParam,lParam);
	}
}

static LRESULT DefCaptionProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	while (1)
	{
		WNDPROC proc=(WNDPROC)GetProp(hWnd,MAKEINTATOM(g_SubclassAtom));
		if (proc)
			return CallWindowProc(proc,hWnd,uMsg,wParam,lParam);
	}
}

// Subclasses the caption window to draw the icon and the text
static LRESULT CALLBACK SubclassCaptionProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	if (uMsg==g_Message)
		return 1;
	if (uMsg==WM_ERASEBKGND)
		return 0;
	if (uMsg==WM_PAINT)
	{
		HTHEME theme=OpenThemeData(hWnd,L"Window");
		if (!theme) return DefCaptionProc(hWnd,uMsg,wParam,lParam);

		// get the icon and the text from the parent
		HWND parent=GetParent(hWnd);
		wchar_t caption[256];
		GetWindowText(parent,caption,_countof(caption));
		HICON hIcon=(HICON)SendMessage(parent,WM_GETICON,ICON_SMALL,0);
		int iconSize=GetSystemMetrics(SM_CXSMICON);

		bool bMaximized=IsZoomed(parent)!=0;
		bool bActive=(parent==GetActiveWindow());
		RECT rc;
		GetClientRect(hWnd,&rc);

		if (!g_DPI)
		{
			HDC hdc=GetDC(NULL);
			g_DPI=GetDeviceCaps(hdc,LOGPIXELSY);
			ReleaseDC(NULL,hdc);
		}

		// create a font from the user settings
		HFONT font=CreateFontSetting(GetSettingString(L"CaptionFont"),g_DPI);
		if (!font)
		{
			LOGFONT lFont;
			GetThemeSysFont(theme,TMT_CAPTIONFONT,&lFont);
			font=CreateFontIndirect(&lFont);
		}

		bool bIcon=GetSettingBool(L"ShowIcon");
		bool bCenter=GetSettingBool(L"CenterCaption");
		bool bGlow=GetSettingBool(bMaximized?L"MaxGlow":L"Glow");

		DTTOPTS opts={sizeof(opts),DTT_COMPOSITED|DTT_TEXTCOLOR};
		opts.crText=GetSettingInt(bMaximized?(bActive?L"MaxColor":L"InactiveMaxColor"):(bActive?L"TextColor":L"InactiveColor"))&0xFFFFFF;

		BOOL bComposition;
		if (SUCCEEDED(DwmIsCompositionEnabled(&bComposition)) && bComposition)
		{
			// Aero Theme
			PAINTSTRUCT ps;
			HDC hdc=BeginPaint(hWnd,&ps);

			BP_PAINTPARAMS paintParams={sizeof(paintParams),BPPF_ERASE};
			HDC hdcPaint=NULL;
			HPAINTBUFFER hBufferedPaint=BeginBufferedPaint(hdc,&ps.rcPaint,BPBF_TOPDOWNDIB,&paintParams,&hdcPaint);
			if (hdcPaint)
			{
				// exclude the caption buttons
				rc.right-=g_SysButtonSize.cx+5;
#if _WIN32_WINNT > _WIN32_WINNT_VISTA
				if (GetWinVersion()==_WIN32_WINNT_VISTA) rc.bottom++;
#endif
				if (!bMaximized)
				{
					rc.left+=g_CustomCaption[0].leftPadding;
					int y=g_CustomCaption[0].topPadding;
					if (y>rc.bottom-iconSize) y=rc.bottom-iconSize;
					if (bIcon)
					{
						DrawIconEx(hdcPaint,rc.left,y,hIcon,iconSize,iconSize,0,NULL,DI_NORMAL|DI_NOMIRROR);
						rc.left+=iconSize;
					}
					rc.left+=g_CustomCaption[0].iconPadding;
					rc.bottom++;
				}
				else
				{
					// when the window is maximized, the caption bar is partially off-screen, so align the icon to the bottom
					rc.left+=g_CustomCaption[1].leftPadding;
					if (bIcon)
					{
						DrawIconEx(hdcPaint,rc.left,rc.bottom-iconSize-g_CustomCaption[1].topPadding,hIcon,iconSize,iconSize,0,NULL,DI_NORMAL|DI_NOMIRROR);
						rc.left+=iconSize;
					}
					rc.left+=g_CustomCaption[1].iconPadding;
					if (GetWinVersion()>=_WIN32_WINNT_WIN10)
						rc.bottom++;
				}
				if (GetWinVersion()<_WIN32_WINNT_WIN10)
					rc.top=rc.bottom-g_SysButtonSize.cy;
				HFONT font0=(HFONT)SelectObject(hdcPaint,font);
				RECT rcText={0,0,0,0};
				opts.dwFlags|=DTT_CALCRECT;
				DrawThemeTextEx(theme,hdcPaint,0,0,caption,-1,DT_VCENTER|DT_NOPREFIX|DT_SINGLELINE|DT_CALCRECT,&rcText,&opts);
				int textWidth=rcText.right-rcText.left;
				if (bCenter && textWidth<rc.right-rc.left)
				{
					rc.left+=(rc.right-rc.left-textWidth)/2;
				}
				if (textWidth>rc.right-rc.left)
					textWidth=rc.right-rc.left;
				opts.dwFlags&=~DTT_CALCRECT;

				if (bGlow)
				{
					HDC hSrc=CreateCompatibleDC(hdcPaint);
					HGDIOBJ bmp0=SelectObject(hSrc,bMaximized?g_GlowBmpMax:g_GlowBmp);
					BLENDFUNCTION func={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
					AlphaBlend(hdcPaint,rc.left-11,rc.top,11,rc.bottom-rc.top,hSrc,0,0,11,24,func);
					AlphaBlend(hdcPaint,rc.left,rc.top,textWidth,rc.bottom-rc.top,hSrc,11,0,2,24,func);
					AlphaBlend(hdcPaint,rc.left+textWidth,rc.top,11,rc.bottom-rc.top,hSrc,13,0,11,24,func);
					SelectObject(hSrc,bmp0);
					DeleteDC(hSrc);
				}
				DrawThemeTextEx(theme,hdcPaint,0,0,caption,-1,DT_VCENTER|DT_NOPREFIX|DT_SINGLELINE|DT_END_ELLIPSIS,&rc,&opts);
				SelectObject(hdcPaint,font0);

				EndBufferedPaint(hBufferedPaint,TRUE);
			}
			EndPaint(hWnd,&ps);
		}
		else
		{
			// Basic Theme
			
			// first draw the caption bar
			DefCaptionProc(hWnd,uMsg,wParam,lParam);

			// then draw the caption directly in the window DC
			HDC hdc=GetWindowDC(hWnd);

			// exclude the caption buttons
			rc.right-=g_SysButtonSize.cx+5;
			rc.top=rc.bottom-g_SysButtonSize.cy;

			rc.left+=g_CustomCaption[2].leftPadding;
			if (bIcon)
			{
				DrawIconEx(hdc,rc.left,rc.bottom-iconSize-g_CustomCaption[2].topPadding,hIcon,iconSize,iconSize,0,NULL,DI_NORMAL|DI_NOMIRROR);
				rc.left+=iconSize;
			}
			rc.left+=g_CustomCaption[2].iconPadding;

			HFONT font0=(HFONT)SelectObject(hdc,font);
			RECT rcText={0,0,0,0};
			opts.dwFlags|=DTT_CALCRECT;
			DrawThemeTextEx(theme,hdc,0,0,caption,-1,DT_VCENTER|DT_NOPREFIX|DT_SINGLELINE|DT_CALCRECT,&rcText,&opts);
			int textWidth=rcText.right-rcText.left;
			if (bCenter && textWidth<rc.right-rc.left)
			{
				rc.left+=(rc.right-rc.left-textWidth)/2;
			}
			if (textWidth>rc.right-rc.left)
				textWidth=rc.right-rc.left;
			opts.dwFlags&=~DTT_CALCRECT;

			if (bGlow)
			{
				HDC hSrc=CreateCompatibleDC(hdc);
				HGDIOBJ bmp0=SelectObject(hSrc,bMaximized?g_GlowBmpMax:g_GlowBmp);
				BLENDFUNCTION func={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
				AlphaBlend(hdc,rc.left-11,rc.top,11,rc.bottom-rc.top,hSrc,0,0,11,24,func);
				AlphaBlend(hdc,rc.left,rc.top,textWidth,rc.bottom-rc.top,hSrc,11,0,2,24,func);
				AlphaBlend(hdc,rc.left+textWidth,rc.top,11,rc.bottom-rc.top,hSrc,13,0,11,24,func);
				SelectObject(hSrc,bmp0);
				DeleteDC(hSrc);
			}
			DrawThemeTextEx(theme,hdc,0,0,caption,-1,DT_VCENTER|DT_NOPREFIX|DT_SINGLELINE|DT_END_ELLIPSIS,&rc,&opts);
			SelectObject(hdc,font0);

			ReleaseDC(hWnd,hdc);
		}

		DeleteObject(font);
		CloseThemeData(theme);
		return 0;
	}
	return DefCaptionProc(hWnd,uMsg,wParam,lParam);
}

// Replacement proc for the "Client Caption" class that hooks the main frame and the caption windows
static LRESULT CALLBACK ClassCaptionProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	if (uMsg==WM_CREATE)
	{
		WNDPROC proc=(WNDPROC)SetWindowLongPtr(hWnd,GWLP_WNDPROC,(LONG_PTR)SubclassCaptionProc);
		SetProp(hWnd,MAKEINTATOM(g_SubclassAtom),(HANDLE)proc);
		HWND frame=GetParent(hWnd);
		proc=(WNDPROC)SetWindowLongPtr(frame,GWLP_WNDPROC,(LONG_PTR)SubclassFrameProc);
		SetProp(frame,MAKEINTATOM(g_SubclassAtom),(HANDLE)proc);
		PostMessage(frame,g_Message,0,0);
	}
	return CallWindowProc(g_OldClassCaptionProc,hWnd,uMsg,wParam,lParam);
}

static BOOL CALLBACK EnumTopWindows( HWND hwnd, LPARAM lParam )
{
	DWORD processId;
	DWORD threadId=GetWindowThreadProcessId(hwnd,&processId);
	if (processId==GetCurrentProcessId())
	{
		HWND caption=FindWindowEx(hwnd,NULL,L"Client Caption",NULL);
		if (caption)
		{
			LogToFile(CIE_LOG,L"InitClassicIE: caption=%p",caption);
			if (!g_OldClassCaptionProc)
				g_OldClassCaptionProc=(WNDPROC)SetClassLongPtr(caption,GCLP_WNDPROC,(LONG_PTR)ClassCaptionProc);
			WNDPROC proc=(WNDPROC)SetWindowLongPtr(caption,GWLP_WNDPROC,(LONG_PTR)SubclassCaptionProc);
			SetProp(caption,MAKEINTATOM(g_SubclassAtom),(HANDLE)proc);
			proc=(WNDPROC)SetWindowLongPtr(hwnd,GWLP_WNDPROC,(LONG_PTR)SubclassFrameProc);
			SetProp(hwnd,MAKEINTATOM(g_SubclassAtom),(HANDLE)proc);
			PostMessage(hwnd,g_Message,0,0);
		}
	}
	return TRUE;
}

void InitClassicIE( HMODULE hModule )
{
	CRegKey regKey;
	if (regKey.Open(HKEY_CURRENT_USER,GetSettingsRegPath())==ERROR_SUCCESS)
	{
		DWORD val;
		if (regKey.QueryDWORDValue(L"CustomAero",val)==ERROR_SUCCESS)
		{
			g_CustomCaption[0].leftPadding=(val&255);
			g_CustomCaption[0].topPadding=((val>>8)&255);
			g_CustomCaption[0].iconPadding=((val>>16)&255);
		}
		if (regKey.QueryDWORDValue(L"CustomAeroMax",val)==ERROR_SUCCESS)
		{
			g_CustomCaption[1].leftPadding=(val&255);
			g_CustomCaption[1].topPadding=((val>>8)&255);
			g_CustomCaption[1].iconPadding=((val>>16)&255);
		}
		if (regKey.QueryDWORDValue(L"CustomBasic",val)==ERROR_SUCCESS)
		{
			g_CustomCaption[2].leftPadding=(val&255);
			g_CustomCaption[2].topPadding=((val>>8)&255);
			g_CustomCaption[2].iconPadding=((val>>16)&255);
		}
	}

	g_Message=RegisterWindowMessage(L"ClassicIE.Injected");
	g_SubclassAtom=GlobalAddAtom(L"ClassicIE.Subclass");
	ChangeWindowMessageFilter(g_Message,MSGFLT_ADD);
	g_GlowBmp=(HBITMAP)LoadImage(g_Instance,MAKEINTRESOURCE(IDB_GLOW),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
	PremultiplyBitmap(g_GlowBmp,GetSettingInt(L"GlowColor"));
	g_GlowBmpMax=(HBITMAP)LoadImage(g_Instance,MAKEINTRESOURCE(IDB_GLOW),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
	PremultiplyBitmap(g_GlowBmpMax,GetSettingInt(L"MaxGlowColor"));

	EnumWindows(EnumTopWindows,0);
}
