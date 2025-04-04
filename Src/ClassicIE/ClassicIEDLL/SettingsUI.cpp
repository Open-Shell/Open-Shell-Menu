// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include "Settings.h"
#include "SettingsUIHelper.h"
#include "LanguageSettingsHelper.h"
#include "ResourceHelper.h"
#include "Translations.h"
#include "resource.h"
#include "dllmain.h"
#include "ClassicIEDLL.h"
#include <dwmapi.h>
#include <vssym32.h>

///////////////////////////////////////////////////////////////////////////////

static CSetting g_Settings[]={
{L"Basic",CSetting::TYPE_GROUP,IDS_BASIC_SETTINGS},
	{L"EnableSettings",CSetting::TYPE_BOOL,0,0,1,CSetting::FLAG_HIDDEN|CSetting::FLAG_NOSAVE},

{L"TitleBar",CSetting::TYPE_GROUP,IDS_TITLE_SETTINGS},
	{L"ShowCaption",CSetting::TYPE_BOOL,IDS_SHOW_CAPTION,IDS_SHOW_CAPTION_TIP,1,CSetting::FLAG_WARM|CSetting::FLAG_BASIC},
	{L"ShowIcon",CSetting::TYPE_BOOL,IDS_SHOW_ICON,IDS_SHOW_ICON_TIP,1,CSetting::FLAG_WARM|CSetting::FLAG_BASIC,L"ShowCaption"},
	{L"CenterCaption",CSetting::TYPE_BOOL,IDS_CENTER_CAPTION,IDS_CENTER_CAPTION_TIP,0,CSetting::FLAG_WARM|CSetting::FLAG_BASIC,L"ShowCaption"},
	{L"CaptionFont",CSetting::TYPE_FONT,IDS_CAPTION_FONT,IDS_CAPTION_FONT_TIP,L"Segoe UI, normal, 9",CSetting::FLAG_WARM,L"ShowCaption"},
	{L"TextColor",CSetting::TYPE_COLOR,IDS_TEXT_COLOR,IDS_TEXT_COLOR_TIP,0,CSetting::FLAG_WARM,L"ShowCaption"},
	{L"MaxColor",CSetting::TYPE_COLOR,IDS_MAXTEXT_COLOR,IDS_MAXTEXT_COLOR_TIP,0,CSetting::FLAG_WARM|(1<<24),L"ShowCaption"},
	{L"InactiveColor",CSetting::TYPE_COLOR,IDS_INTEXT_COLOR,IDS_INTEXT_COLOR_TIP,0,CSetting::FLAG_WARM|(2<<24),L"ShowCaption"},
	{L"InactiveMaxColor",CSetting::TYPE_COLOR,IDS_MAXINTEXT_COLOR,IDS_MAXINTEXT_COLOR_TIP,0,CSetting::FLAG_WARM|(3<<24),L"ShowCaption"},
	{L"Glow",CSetting::TYPE_BOOL,IDS_GLOW,IDS_GLOW_TIP,0,CSetting::FLAG_WARM,L"ShowCaption"},
	{L"GlowColor",CSetting::TYPE_COLOR,IDS_GLOW_COLOR,IDS_GLOW_COLOR_TIP,0xFFFFFF,CSetting::FLAG_WARM|(4<<24),L"#Glow",L"Glow"},
	{L"MaxGlow",CSetting::TYPE_BOOL,IDS_MAXGLOW,IDS_MAXGLOW_TIP,0,CSetting::FLAG_WARM,L"ShowCaption"},
	{L"MaxGlowColor",CSetting::TYPE_COLOR,IDS_MAXGLOW_COLOR,IDS_MAXGLOW_COLOR_TIP,0xFFFFFF,CSetting::FLAG_WARM|(5<<24),L"#MaxGlow",L"MaxGlow"},

{L"StatusBar",CSetting::TYPE_GROUP,IDS_STATUS_SETTINGS},
	{L"ShowProgress",CSetting::TYPE_BOOL,IDS_SHOW_PROGRESS,IDS_SHOW_PROGRESS_TIP,1,CSetting::FLAG_WARM|CSetting::FLAG_BASIC},
	{L"ShowZone",CSetting::TYPE_BOOL,IDS_SHOW_ZONE,IDS_SHOW_ZONE_TIP,1,CSetting::FLAG_WARM|CSetting::FLAG_BASIC},
	{L"ShowProtected",CSetting::TYPE_BOOL,IDS_SHOW_PROTECTED,IDS_SHOW_PROTECTED_TIP,1,CSetting::FLAG_WARM,L"ShowZone",L"ShowZone"},

{L"Language",CSetting::TYPE_GROUP,IDS_LANGUAGE_SETTINGS,0,0,0,NULL,NULL,GetLanguageSettings(COMPONENT_IE)},
	{L"Language",CSetting::TYPE_STRING,0,0,L"",CSetting::FLAG_SHARED},

{NULL}
};

void UpgradeSettings( bool bShared )
{
}

void UpdateSettings( void )
{
	bool bWin8=(GetWinVersion()>=_WIN32_WINNT_WIN8);

	BOOL bComposition=0;
	if (FAILED(DwmIsCompositionEnabled(&bComposition)))
		bComposition=FALSE;

	if (bComposition && bWin8)
	{
		// check for High Contrast theme on Win8
		HIGHCONTRAST contrast={sizeof(contrast)};
		if (SystemParametersInfo(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) && (contrast.dwFlags&HCF_HIGHCONTRASTON))
			bComposition=FALSE;
		else
		{
			// check for Basic theme
			DWORD color;
			BOOL opaque;
			if (SUCCEEDED(DwmGetColorizationColor(&color,&opaque)) && opaque)
				bComposition=FALSE;
		}
	}

	UpdateSetting(L"Glow",CComVariant(bComposition?1:0),false);
	UpdateSetting(L"MaxGlow",CComVariant(bComposition?1:0),false);
	UpdateSetting(L"CenterCaption",CComVariant((bWin8 && GetWinVersion()<_WIN32_WINNT_WIN10)?1:0),false);

	// create a dummy window to get a theme
	HWND hwnd=CreateWindow(L"#32770",L"",WS_OVERLAPPEDWINDOW,0,0,0,0,NULL,NULL,NULL,0);
	HTHEME theme=OpenThemeData(hwnd,L"Window");
	if (theme)
	{
		HDC hdc=GetDC(NULL);
		int dpi=GetDeviceCaps(hdc,LOGPIXELSY);
		ReleaseDC(NULL,hdc);

		LOGFONT font;
		GetThemeSysFont(theme,TMT_CAPTIONFONT,&font);
		wchar_t text[256];
		const wchar_t *type=font.lfItalic?L"italic":L"normal";
		if (font.lfWeight>=FW_BOLD)
			type=font.lfItalic?L"bold_italic":L"bold";
		Sprintf(text,_countof(text),L"%s, %s, %d",font.lfFaceName,type,(-font.lfHeight*72+dpi/2)/dpi);
		UpdateSetting(L"CaptionFont",CComVariant(text),false);

		int color=GetThemeSysColor(theme,COLOR_CAPTIONTEXT);
		UpdateSetting(L"TextColor",CComVariant(color),false);
		UpdateSetting(L"MaxColor",CComVariant(color),false);
		if (bWin8)
			color=GetThemeSysColor(theme,COLOR_INACTIVECAPTIONTEXT);
		UpdateSetting(L"InactiveColor",CComVariant(color),false);
		UpdateSetting(L"InactiveMaxColor",CComVariant(color),false);

		CloseThemeData(theme);
	}
	else
	{
		int color=GetSysColor(COLOR_CAPTIONTEXT);
		UpdateSetting(L"TextColor",CComVariant(color),false);
		UpdateSetting(L"MaxColor",CComVariant(color),false);
		color=GetSysColor(COLOR_INACTIVECAPTIONTEXT);
		UpdateSetting(L"InactiveColor",CComVariant(color),false);
		UpdateSetting(L"InactiveMaxColor",CComVariant(color),false);
	}
	DestroyWindow(hwnd);

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

void InitSettings( void )
{
	InitSettings(g_Settings,COMPONENT_IE,NULL);
}

void ClosingSettings( HWND hWnd, int flags, int command )
{
	if (command==IDOK)
	{
		if (flags&CSetting::FLAG_WARM)
		{
			if (FindWindow(L"IEFrame",NULL))
				MessageBox(hWnd,LoadStringEx(IDS_NEW_SETTINGS),LoadStringEx(IDS_APP_TITLE),MB_OK|MB_ICONINFORMATION);
		}
	}
}

void SettingChangedCallback( const CSetting *pSetting )
{
}

CSIEAPI void ShowIESettings( void )
{
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

CSIEAPI DWORD GetIESettings( void )
{
	DWORD res=0;
	if (GetSettingBool(L"ShowCaption")) res|=IE_SETTING_CAPTION;
	if (GetSettingBool(L"ShowProgress")) res|=IE_SETTING_PROGRESS;
	if (GetSettingBool(L"ShowZone")) res|=IE_SETTING_ZONE;
	if (GetSettingBool(L"ShowProtected")) res|=IE_SETTING_PROTECTED;
	return res;
}
