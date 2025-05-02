// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include "resource.h"
#include "Translations.h"
#include "Settings.h"
#include "SettingsUI.h"
#include "ResourceHelper.h"
#include "ItemManager.h"
#include "StartMenuDLL.h"
#include "StartButton.h"
#include "MenuContainer.h"
#include "TouchHelper.h"
#include "dllmain.h"
#include <uxtheme.h>
#include <vsstyle.h>
#include <vssym32.h>
#include <dwmapi.h>

static int START_ICON_SIZE=0;
const int START_BUTTON_PADDING=3;
const int START_BUTTON_OFFSET=2;
const int START_TEXT_PADDING=2;
const int BLEND_PRECISION=1000;

bool g_bAllowMoveButton;

// CStartButton - implementation of a start button (for Windows 8)
class CStartButton: public CWindowImpl<CStartButton>
{
public:
	DECLARE_WND_CLASS_EX(L"OpenShell.CStartButton",CS_DBLCLKS,COLOR_MENU)
	CStartButton( void );

	// message handlers
	BEGIN_MSG_MAP( CStartButton )
		MESSAGE_HANDLER( WM_CREATE, OnCreate )
		MESSAGE_HANDLER( WM_DESTROY, OnDestroy )
		MESSAGE_HANDLER( WM_CLOSE, OnClose )
		MESSAGE_HANDLER( WM_MOUSEACTIVATE, OnMouseActivate )
		MESSAGE_HANDLER( WM_MOUSEMOVE, OnMouseMove )
		MESSAGE_HANDLER( WM_ERASEBKGND, OnEraseBkgnd )
		MESSAGE_HANDLER( WM_TIMER, OnTimer )
		MESSAGE_HANDLER( WM_SETTINGCHANGE, OnSettingChange )
		MESSAGE_HANDLER( WM_THEMECHANGED, OnThemeChanged )
		MESSAGE_HANDLER( WM_POINTERDOWN, OnPointer )
		MESSAGE_HANDLER( WM_POINTERUPDATE, OnPointer )
		MESSAGE_HANDLER( WM_POINTERUP, OnPointer )
	END_MSG_MAP()

	void SetPressed( bool bPressed );
	void UpdateButton( void );
	void TaskBarMouseMove( void );

	SIZE GetSize( void ) const { return m_Size; }
	bool GetSmallIcons( void ) const { return m_bSmallIcons; }

protected:
	LRESULT OnCreate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnClose( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled ) { return 0; }
	LRESULT OnEraseBkgnd( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled ) { return 1; }
	LRESULT OnMouseActivate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled ) { return MA_NOACTIVATE; }
	LRESULT OnMouseMove( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnTimer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSettingChange( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnThemeChanged( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnPointer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );

private:
	enum { TIMER_BLEND=1, TIMER_LEAVE=2 };

	size_t m_TaskbarId;
	SIZE m_Size;
	HBITMAP m_Bitmap, m_Blendmap;
	unsigned int *m_Bits, *m_BlendBits;
	HICON m_Icon;
	HFONT m_Font;
	bool m_bHot, m_bPressed;
	bool m_bTrackMouse;
	bool m_bClassic;
	bool m_bRTL;
	bool m_bSmallIcons;
	int m_HotBlend; // 0..BLEND_PRECISION
	CWindow m_Tooltip;
	HTHEME m_Theme;

	// animations
	int m_YOffset;
	int m_Frames[3];

	struct Animation
	{
		std::vector<int> frames;
		int duration; // in ms
		bool bBlend;
	};
	Animation m_Animations[2];

	void ParseAnimation( Animation &animation, const std::vector<unsigned int> &pixels, int &index, int totalFrames );
	void LoadBitmap( void );
	void SetHot( bool bHot );
};

CStartButton::CStartButton( void )
{
	m_Size.cx=m_Size.cy=0;
	m_TaskbarId=-1;
	m_Bitmap=m_Blendmap=NULL;
	m_Bits=m_BlendBits=NULL;
	m_Icon=NULL;
	m_Font=NULL;
	m_bHot=m_bPressed=false;
	m_bTrackMouse=false;
	m_bClassic=m_bSmallIcons=false;
	m_bRTL=false;
	m_HotBlend=0;
	m_Theme=NULL;
}

LRESULT CStartButton::OnCreate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	size_t params=(intptr_t)(((CREATESTRUCT*)lParam)->lpCreateParams);
	m_bRTL=(params&1)!=0;
	m_TaskbarId=params>>1;
	m_bSmallIcons=IsTaskbarSmallIcons();
	std::vector<HMODULE> modules;
	m_Icon=NULL;
	START_ICON_SIZE=0;
	CString iconPath=GetSettingString(L"StartButtonIcon");
	if (_wcsicmp(iconPath,L"none")!=0)
	{
		START_ICON_SIZE=GetSettingInt(L"StartButtonIconSize");
		if (START_ICON_SIZE==0)
			START_ICON_SIZE=GetSystemMetrics(m_bSmallIcons?SM_CXSMICON:SM_CXICON);
		if (START_ICON_SIZE<8) START_ICON_SIZE=8;
		if (START_ICON_SIZE>64) START_ICON_SIZE=64;
		m_Icon=LoadIcon(START_ICON_SIZE,iconPath,modules);
		for (std::vector<HMODULE>::const_iterator it=modules.begin();it!=modules.end();++it)
			FreeLibrary(*it);
		if (!m_Icon)
			m_Icon=(HICON)LoadImage(g_Instance,MAKEINTRESOURCE(IDI_APPICON),IMAGE_ICON,START_ICON_SIZE,START_ICON_SIZE,LR_DEFAULTCOLOR);
	}
	int dpi=CItemManager::GetDPI(false);
	m_Font=CreateFont(MulDiv(10,dpi,72),0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Tahoma");
	int val=1;
	DwmSetWindowAttribute(m_hWnd,DWMWA_EXCLUDED_FROM_PEEK,&val,sizeof(val));
	val=DWMFLIP3D_EXCLUDEABOVE;
	DwmSetWindowAttribute(m_hWnd,DWMWA_FLIP3D_POLICY,&val,sizeof(val));
	LoadBitmap();
	m_Tooltip=CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT|(m_bRTL?WS_EX_LAYOUTRTL:0),TOOLTIPS_CLASS,NULL,WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP,0,0,0,0,NULL,NULL,g_Instance,NULL);
	OnThemeChanged(WM_THEMECHANGED,0,0,bHandled);
	m_bPressed=true;
	SetPressed(false);
	ResizeClient(m_Size.cx,m_Size.cy);
	bHandled=FALSE;
	return 0;
}

LRESULT CStartButton::OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_Bitmap) DeleteObject(m_Bitmap);
	if (m_Blendmap) DeleteObject(m_Blendmap);
	if (m_Icon) DestroyIcon(m_Icon);
	if (m_Font) DeleteObject(m_Font);
	if (m_Theme) CloseThemeData(m_Theme);
	m_Tooltip.DestroyWindow();
	KillTimer(TIMER_BLEND);
	bHandled=FALSE;
	return 0;
}

void CStartButton::UpdateButton( void )
{
	BLENDFUNCTION func={AC_SRC_OVER,0,255,AC_SRC_ALPHA};

	HDC hSrc=CreateCompatibleDC(NULL);
	RECT rc;
	GetWindowRect(&rc);
	SIZE size={rc.right-rc.left,rc.bottom-rc.top};
	if (m_bClassic)
	{
		if (m_bRTL)
			SetLayout(hSrc,LAYOUT_RTL);
		HGDIOBJ bmp0=SelectObject(hSrc,m_Blendmap);
		RECT rc={0,0,m_Size.cx,m_Size.cy};
		FillRect(hSrc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
		InflateRect(&rc,-START_BUTTON_OFFSET,-START_BUTTON_OFFSET);
		int offset=0;
		if (m_Theme)
		{
			int state=m_bPressed?PBS_PRESSED:(m_bHot?PBS_HOT:PBS_NORMAL);
			DrawThemeBackground(m_Theme,hSrc,BP_PUSHBUTTON,state,&rc,NULL);
		}
		else
		{
			DrawFrameControl(hSrc,&rc,DFC_BUTTON,DFCS_BUTTONPUSH|(m_bPressed?DFCS_PUSHED:0));
			offset=m_bPressed?1:0;
		}
		if (m_Icon)
			DrawIconEx(hSrc,START_BUTTON_PADDING+START_BUTTON_OFFSET+offset,(m_Size.cy-START_ICON_SIZE)/2+offset,m_Icon,0,0,0,NULL,DI_NORMAL|DI_NOMIRROR);
		rc.left+=START_BUTTON_PADDING+START_ICON_SIZE+START_TEXT_PADDING+offset;
		rc.top+=START_BUTTON_PADDING+offset;
		rc.right-=START_BUTTON_PADDING+START_TEXT_PADDING-offset;
		rc.bottom-=START_BUTTON_PADDING-offset;
		HFONT font0=(HFONT)SelectObject(hSrc,m_Font);
		COLORREF color=GetSysColor(COLOR_BTNTEXT);
		if (m_Theme)
		{
			int state=m_bPressed?PBS_PRESSED:(m_bHot?PBS_HOT:PBS_NORMAL);
			if (FAILED(GetThemeColor(m_Theme,BP_PUSHBUTTON,state,TMT_TEXTCOLOR,&color)))
				color=GetSysColor(COLOR_BTNTEXT);
		}
		SetTextColor(hSrc,color);
		SetBkMode(hSrc,TRANSPARENT);
		CString startStr=GetSettingString(L"StartButtonText");
		const wchar_t *startText=startStr;
		if (startText[0]=='$')
			startText=FindTranslation(startText+1,L"Start");
		DrawText(hSrc,startText,-1,&rc,DT_NOPREFIX|DT_SINGLELINE|DT_VCENTER);
		SelectObject(hSrc,bmp0);
		// mark the button pixels as opaque
		for (int y=START_BUTTON_OFFSET;y<m_Size.cy-START_BUTTON_OFFSET;y++)
			for (int x=START_BUTTON_OFFSET;x<m_Size.cx-START_BUTTON_OFFSET;x++)
				m_BlendBits[y*m_Size.cx+x]|=0xFF000000;
		SelectObject(hSrc,m_Blendmap);

		POINT pos={0,0};
		UpdateLayeredWindow(m_hWnd,NULL,NULL,&size,hSrc,&pos,0,&func,ULW_ALPHA);
		SelectObject(hSrc,font0);
		SelectObject(hSrc,bmp0);
	}
	else
	{
		int image=-1;
		int frame1, frame2, blend;
		if (m_bPressed) image=m_Frames[2];
		else if (m_HotBlend==0) image=m_Frames[0];
		else if (m_HotBlend==BLEND_PRECISION) image=m_Frames[1];
		else
		{
			const Animation &animation=m_Animations[m_bHot?0:1];
			int count=(int)animation.frames.size()-1;
			blend=m_bHot?m_HotBlend:(BLEND_PRECISION-m_HotBlend);
			if (count<1 || animation.duration==0)
			{
				image=m_Frames[m_bHot?1:0];
			}
			else if (!animation.bBlend)
			{
				int index=(blend*count+50)/BLEND_PRECISION; // [0..count]
				image=animation.frames[index];
			}
			else
			{
				int index=(blend*count)/BLEND_PRECISION; // [0..count-1]
				blend=(blend*count)%BLEND_PRECISION;

				frame1=animation.frames[index];
				frame2=animation.frames[index+1];
			}
		}

		if (image!=-1)
		{
			HGDIOBJ bmp0=SelectObject(hSrc,m_Bitmap);
			POINT pos={0,image*m_Size.cy+m_YOffset};
			UpdateLayeredWindow(m_hWnd,NULL,NULL,&size,hSrc,&pos,0,&func,ULW_ALPHA);
			SelectObject(hSrc,bmp0);
		}
		else if (m_Bits)
		{
			// blend the two images
			int n=m_Size.cx*m_Size.cy;
			int n1=frame1*n;
			int n2=frame2*n;
			for (int i=0;i<n;i++)
			{
				unsigned int pixel1=m_Bits[i+n1];
				unsigned int pixel2=m_Bits[i+n2];
				int a1=(pixel1>>24);
				int r1=(pixel1>>16)&255;
				int g1=(pixel1>>8)&255;
				int b1=(pixel1)&255;
				int a2=(pixel2>>24);
				int r2=(pixel2>>16)&255;
				int g2=(pixel2>>8)&255;
				int b2=(pixel2)&255;
				int a=a1+(a2-a1)*blend/BLEND_PRECISION;
				int r=r1+(r2-r1)*blend/BLEND_PRECISION;
				int g=g1+(g2-g1)*blend/BLEND_PRECISION;
				int b=b1+(b2-b1)*blend/BLEND_PRECISION;
				m_BlendBits[i]=(a<<24)|(r<<16)|(g<<8)|b;
			}
			HGDIOBJ bmp0=SelectObject(hSrc,m_Blendmap);
			POINT pos={0,0};
			UpdateLayeredWindow(m_hWnd,NULL,NULL,&size,hSrc,&pos,0,&func,ULW_ALPHA);
			SelectObject(hSrc,bmp0);
		}
	}
	DeleteDC(hSrc);
}

void CStartButton::SetHot( bool bHot )
{
	if (m_bHot!=bHot)
	{
		m_bHot=bHot;
		if (!m_bPressed)
		{
			if (bHot)
				CMenuContainer::PlayMenuSound(SOUND_BUTTON_HOVER);
			SetTimer(TIMER_BLEND,30);
		}
	}
}

LRESULT CStartButton::OnMouseMove( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	SetTimer(TIMER_LEAVE,30);
	SetHot(true);
	return 0;
}

void CStartButton::TaskBarMouseMove( void )
{
	SetHot(true);
	SetTimer(CStartButton::TIMER_LEAVE,30);
}

LRESULT CStartButton::OnTimer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==TIMER_BLEND)
	{
		int duration=m_Animations[m_bHot?0:1].duration;
		int blend;
		if (duration>0)
		{
			int dp=(30*BLEND_PRECISION)/duration;
			blend=m_HotBlend+(m_bHot?dp:-dp);
			if (blend<0) blend=0;
			if (blend>BLEND_PRECISION) blend=BLEND_PRECISION;
		}
		else
		{
			blend=m_bHot?BLEND_PRECISION:0;
		}
		if (blend!=m_HotBlend)
		{
			m_HotBlend=blend;
			UpdateButton();
		}
		else
			KillTimer(TIMER_BLEND);
	}
	else if (wParam==TIMER_LEAVE)
	{
		CPoint pt(GetMessagePos());
		if (WindowFromPoint(pt)!=m_hWnd && !PointAroundStartButton(m_TaskbarId))
		{
			KillTimer(TIMER_LEAVE);
			SetHot(false);
		}
	}
	else
		bHandled=FALSE;
	return 0;
}

LRESULT CStartButton::OnSettingChange( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	UpdateButton();
	bHandled=FALSE;
	return 0;
}

LRESULT CStartButton::OnThemeChanged( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_Theme) CloseThemeData(m_Theme);
	m_Theme=NULL;
	HIGHCONTRAST contrast={sizeof(contrast)};
	if (GetWinVersion()>=WIN_VER_WIN8 && SystemParametersInfo(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) && (contrast.dwFlags&HCF_HIGHCONTRASTON))
	{
		// only use themes on Win8 with high contrast
		m_Theme=OpenThemeData(m_hWnd,L"button");
		UpdateButton();
	}
	return 0;
}

LRESULT CStartButton::OnPointer( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	POINTER_INPUT_TYPE type;
	GetPointerType2(GET_POINTERID_WPARAM(wParam),&type);
	if (type==PT_TOUCH)
		return GetParent().SendMessage(uMsg,wParam,lParam);
	bHandled=FALSE;
	return 0;
}

void CStartButton::SetPressed( bool bPressed )
{
	if (m_bPressed!=bPressed)
	{
		m_bPressed=bPressed;
		m_HotBlend=m_bHot?BLEND_PRECISION:0;
		KillTimer(TIMER_BLEND);
		TOOLINFO tool={sizeof(tool),TTF_CENTERTIP|TTF_SUBCLASS|TTF_IDISHWND|TTF_TRANSPARENT|(m_bRTL?TTF_RTLREADING:0U),m_hWnd};
		tool.uId=(UINT_PTR)m_hWnd;
		CString startStr=GetSettingString(L"StartButtonTip");
		const wchar_t *startText=startStr;
		if (startText[0]=='$')
			startText=FindTranslation(startText+1,L"Start");
		wchar_t buf[256];
		Strcpy(buf,_countof(buf),startText);
		DoEnvironmentSubst(buf,_countof(buf));
		tool.lpszText=buf;
		m_Tooltip.SendMessage(bPressed?TTM_DELTOOL:TTM_ADDTOOL,0,(LPARAM)&tool);
		UpdateButton();
	}
}

TStartButtonType GetStartButtonType( void )
{
	bool bDef;
	TStartButtonType buttonType=(TStartButtonType)GetSettingInt(L"StartButtonType",bDef);
	if (bDef)
	{
		bool bClassic;
		if (GetWinVersion()<WIN_VER_WIN8)
			bClassic=!IsAppThemed();
		else
		{
			HIGHCONTRAST contrast={sizeof(contrast)};
			bClassic=(SystemParametersInfo(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) && (contrast.dwFlags&HCF_HIGHCONTRASTON));
		}
		buttonType=bClassic?START_BUTTON_CLASSIC:START_BUTTON_AERO;
	}
	return buttonType;
}

void CStartButton::ParseAnimation( Animation &animation, const std::vector<unsigned int> &pixels, int &index, int totalFrames )
{
	animation.duration=0;
	animation.bBlend=true;
	if (index>=(int)pixels.size())
	{
		animation.frames.clear();
		return;
	}

	animation.duration=((pixels[index]&255)*1000)/60;
	animation.bBlend=((pixels[index]>>16)&255)==1;
	int ranges=(pixels[index]>>8)&255;
	if (ranges>0 && index+ranges<(int)pixels.size())
	{
		animation.frames.clear();
		for (int r=1;r<=ranges;r++)
		{
			int from=pixels[index+r]&255;
			if (from>totalFrames-1) from=totalFrames-1;
			int to=(pixels[index+r]>>16)&255;
			if (to>totalFrames-1) to=totalFrames-1;
			if (from<to)
			{
				for (int i=from;i<=to;i++)
					animation.frames.push_back(i);
			}
			else if (from>to)
			{
				for (int i=from;i>=to;i--)
					animation.frames.push_back(i);
			}
			else
			{
				animation.frames.push_back(from);
			}
		}
	}
	index+=ranges+1;
}

void CStartButton::LoadBitmap( void )
{
	m_Size.cx=m_Size.cy=0;
	if (m_Bitmap) DeleteObject(m_Bitmap);
	if (m_Blendmap) DeleteObject(m_Blendmap);
	m_Bitmap=m_Blendmap=NULL;
	m_Bits=m_BlendBits=NULL;
	TStartButtonType buttonType=GetStartButtonType();
	m_bClassic=(buttonType==START_BUTTON_CLASSIC);
	wchar_t path[_MAX_PATH];
	SIZE size={0,0};
	if (buttonType==START_BUTTON_CUSTOM)
	{
		Strcpy(path,_countof(path),GetSettingString(L"StartButtonPath"));
		DoEnvironmentSubst(path,_countof(path));
		size.cx=GetSettingInt(L"StartButtonSize");
	}

	m_YOffset=0;
	m_Frames[0]=0; // Normal
	m_Frames[1]=1; // Hot
	m_Frames[2]=2; // Pressed
	m_Animations[0].frames.resize(2); m_Animations[0].frames[0]=0; m_Animations[0].frames[1]=1; m_Animations[0].duration=300; m_Animations[0].bBlend=true; // NH
	m_Animations[1].frames.resize(2); m_Animations[1].frames[0]=1; m_Animations[1].frames[1]=0; m_Animations[1].duration=300; m_Animations[1].bBlend=true; // HN

	if (m_bClassic)
	{
		// classic theme
		HDC hdc=CreateCompatibleDC(NULL);
		HFONT font0=(HFONT)SelectObject(hdc,m_Font);
		RECT rc={0,0,0,0};
		CString startStr=GetSettingString(L"StartButtonText");
		const wchar_t *startText=startStr;
		if (startText[0]=='$')
			startText=FindTranslation(startText+1,L"Start");
		DrawText(hdc,startText,-1,&rc,DT_NOPREFIX|DT_SINGLELINE|DT_CALCRECT);
		m_Size.cx=rc.right+START_ICON_SIZE+2*START_TEXT_PADDING+2*START_BUTTON_PADDING+2*START_BUTTON_OFFSET;
		m_Size.cy=rc.bottom;
		if (m_Size.cy<START_ICON_SIZE) m_Size.cy=START_ICON_SIZE;
		m_Size.cy+=2*START_BUTTON_PADDING+2*START_BUTTON_OFFSET;
		DeleteDC(hdc);
	}
	else
	{
		bool bResource=false;
		std::vector<unsigned int> buttonAnim;
		if (*path)
		{
			m_Bitmap=LoadImageFile(path,&size,true,true,&buttonAnim);
		}
		if (!m_Bitmap)
		{
			int id;
			int dpi=GetDpi(GetParent());
			if (dpi<120)
				id=IDB_BUTTON96;
			else if (dpi<144)
				id=IDB_BUTTON120;
			else if (dpi<168)
				id=IDB_BUTTON144;
			else
				id=IDB_BUTTON180;
			m_Bitmap=LoadImageResource(g_Instance,MAKEINTRESOURCE(id),true,true);
			bResource=true;
		}
		BITMAP info;
		GetObject(m_Bitmap,sizeof(info),&info);
		m_Size.cx=info.bmWidth;
		m_Size.cy=info.bmHeight;
		m_Bits=(unsigned int*)info.bmBits;
		if (buttonAnim.empty() && info.bmWidth>=10 && (m_Bits[0]&0xFFFFFF)==ANIM_BUTTON_TAG1 && (m_Bits[1]&0xFFFFFF)==ANIM_BUTTON_TAG2)
		{
			m_YOffset=(m_Bits[2]>>16)&255;
			if (m_YOffset>info.bmHeight) m_YOffset=info.bmHeight;
			if (m_YOffset>0)
			{
				int size=info.bmWidth*m_YOffset;
				buttonAnim.resize(size);
				memcpy(&buttonAnim[0],m_Bits,size*4);
				m_Bits+=size;
				m_Size.cy=info.bmHeight-m_YOffset;
			}
		}
		if (!buttonAnim.empty())
		{
			int total=buttonAnim[2]&255;
			if (total<1) total=1;
			if (total>info.bmHeight-1) total=info.bmHeight-1;
			m_Size.cy/=total;
			m_Frames[0]=buttonAnim[3]&255; // Normal
			if (m_Frames[0]>total-1) m_Frames[0]=total-1;
			m_Frames[1]=buttonAnim[4]&255; // Hot
			if (m_Frames[1]>total-1) m_Frames[1]=total-1;
			m_Frames[2]=buttonAnim[5]&255; // Pressed
			if (m_Frames[2]>total-1) m_Frames[2]=total-1;

			m_Animations[0].frames[0]=m_Animations[1].frames[1]=m_Frames[0];
			m_Animations[0].frames[1]=m_Animations[1].frames[0]=m_Frames[1];
			int index=6;
			ParseAnimation(m_Animations[0],buttonAnim,index,total);
			ParseAnimation(m_Animations[1],buttonAnim,index,total);
		}
		else
		{
			m_Size.cy/=3;
		}
	}
	if (m_Size.cx>0)
	{
		BITMAPINFO bi={0};
		bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth=m_Size.cx;
		bi.bmiHeader.biHeight=-m_Size.cy;
		bi.bmiHeader.biPlanes=1;
		bi.bmiHeader.biBitCount=32;
		HDC hdc=CreateCompatibleDC(NULL);
		m_Blendmap=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,(void**)&m_BlendBits,NULL,0);
		DeleteDC(hdc);
	}
}

static std::map<int,CStartButton> g_StartButtons;

HWND CreateStartButton( int taskbarId, HWND taskBar, HWND rebar )
{
	bool bRTL=(GetWindowLongPtr(rebar,GWL_EXSTYLE)&WS_EX_LAYOUTRTL)!=0;
	DWORD styleTopmost=GetWindowLongPtr(taskBar,GWL_EXSTYLE)&WS_EX_TOPMOST;
	CStartButton &button=g_StartButtons[taskbarId];
	button.Create(taskBar,NULL,NULL,WS_POPUP,styleTopmost|WS_EX_TOOLWINDOW|WS_EX_LAYERED,0U,(void*)(intptr_t)(taskbarId*2+(bRTL?1:0)));
	return button.m_hWnd;
}

void DestroyStartButton( int taskbarId )
{
	std::map<int,CStartButton>::iterator it=g_StartButtons.find(taskbarId);
	if (it!=g_StartButtons.end())
	{
		if (it->second.m_hWnd)
			it->second.DestroyWindow();
		g_StartButtons.erase(it);
	}
}

void UpdateStartButton( int taskbarId )
{
	std::map<int,CStartButton>::iterator it=g_StartButtons.find(taskbarId);
	if (it!=g_StartButtons.end())
		it->second.UpdateButton();
}

void PressStartButton( int taskbarId, bool bPressed )
{
	std::map<int,CStartButton>::iterator it=g_StartButtons.find(taskbarId);
	if (it!=g_StartButtons.end())
		it->second.SetPressed(bPressed);
}

SIZE GetStartButtonSize( int taskbarId )
{
	std::map<int,CStartButton>::iterator it=g_StartButtons.find(taskbarId);
	if (it!=g_StartButtons.end())
		return it->second.GetSize();
	SIZE size={0,0};
	return size;
}

bool IsStartButtonSmallIcons( int taskbarId )
{
	std::map<int,CStartButton>::iterator it=g_StartButtons.find(taskbarId);
	if (it!=g_StartButtons.end())
		return it->second.GetSmallIcons();
	return false;
}

bool IsTaskbarSmallIcons( void )
{
	CRegKey regKey;
	if (regKey.Open(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced")!=ERROR_SUCCESS)
		return true;
	DWORD val;
	return regKey.QueryDWORDValue(L"TaskbarSmallIcons",val)!=ERROR_SUCCESS || val;
}

void TaskBarMouseMove( int taskbarId )
{
	std::map<int,CStartButton>::iterator it=g_StartButtons.find(taskbarId);
	if (it!=g_StartButtons.end())
		it->second.TaskBarMouseMove();
}
