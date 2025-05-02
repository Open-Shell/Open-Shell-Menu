// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include "resource.h"
#include "SkinManager.h"
#include "ItemManager.h"
#include "LogManager.h"
#include "SettingsParser.h"
#include "Settings.h"
#include "SettingsUI.h"
#include "Translations.h"
#include "ResourceHelper.h"
#include "FNVHash.h"
#include "dllmain.h"
#include "IatHookHelper.h"
#include "MenuContainer.h"
#include "ProgramsTree.h"
#include <dwmapi.h>
#include <math.h>

//#define SIMULATE_METRO_COLORS "win8_blue"

#ifdef BUILD_SETUP
#undef SIMULATE_METRO_COLORS
#endif

static struct
{
	const wchar_t *name;
	int code;
} g_SystemColors[]=
{
	{L"SystemScrollbar",COLOR_SCROLLBAR},
	{L"SystemBackground",COLOR_BACKGROUND},
	{L"SystemActiveCaption",COLOR_ACTIVECAPTION},
	{L"SystemInactiveCaption",COLOR_INACTIVECAPTION},
	{L"SystemMenu",COLOR_MENU},
	{L"SystemWindow",COLOR_WINDOW},
	{L"SystemWindowFrame",COLOR_WINDOWFRAME},
	{L"SystemMenuText",COLOR_MENUTEXT},
	{L"SystemWindowText",COLOR_WINDOWTEXT},
	{L"SystemCaptionText",COLOR_CAPTIONTEXT},
	{L"SystemActiveBorder",COLOR_ACTIVEBORDER},
	{L"SystemInactiveBorder",COLOR_INACTIVEBORDER},
	{L"SystemAppWorkspace",COLOR_APPWORKSPACE},
	{L"SystemHighlight",COLOR_HIGHLIGHT},
	{L"SystemHighlightText",COLOR_HIGHLIGHTTEXT},
	{L"SystemBtnFace",COLOR_BTNFACE},
	{L"SystemBtnShadow",COLOR_BTNSHADOW},
	{L"SystemGrayText",COLOR_GRAYTEXT},
	{L"SystemBtnText",COLOR_BTNTEXT},
	{L"SystemInactiveCaptionText",COLOR_INACTIVECAPTIONTEXT},
	{L"SystemBtnHighlight",COLOR_BTNHIGHLIGHT},
	{L"System3DDKShadow",COLOR_3DDKSHADOW},
	{L"System3DLight",COLOR_3DLIGHT},
	{L"SystemInfoText",COLOR_INFOTEXT},
	{L"SystemInfoBK",COLOR_INFOBK},
	{L"SystemHotLight",COLOR_HOTLIGHT},
	{L"SystemGradientActiveCaption",COLOR_GRADIENTACTIVECAPTION},
	{L"SystemGradientInactiveCaption",COLOR_GRADIENTINACTIVECAPTION},
	{L"SystemMenuHilight",COLOR_MENUHILIGHT},
	{L"SystemMenuBar",COLOR_MENUBAR},
};

wchar_t MenuSkin::s_SkinError[1024];

const RECT DEFAULT_ICON_PADDING={3,3,3,3};
const RECT DEFAULT_TEXT_PADDING={1,2,8,2};
const SIZE DEFAULT_ARROW_PADDING={5,7};
const int DEFAULT_SEPARATOR_WIDTH=4;
const int DEFAULT_SEPARATOR_HEIGHT=8;
const POINT DEFAULT_USER_IMAGE_PADDING={-4,8};
const RECT DEFAULT_SEARCH_PADDING={4,4,4,4};

static int g_GlassColorR, g_GlassColorG, g_GlassColorB, g_GlassColorA;

#ifdef SIMULATE_METRO_COLORS
struct SimMetroColor
{
	unsigned int nameHash;
	COLORREF color;
};
static std::vector<SimMetroColor> g_MetroColors;

static int GetImmersiveUserColorSetPreference( bool bForceCheckRegistry, bool bSkipCheckOnFail )
{
	return 0;
}

static COLORREF GetImmersiveColorFromColorSetEx( UINT dwImmersiveColorSet, UINT dwImmersiveColorType, bool bIgnoreHighContrast, UINT dwHighContrastCacheMode )
{
	return g_MetroColors[dwImmersiveColorType].color;
}

static int GetImmersiveColorTypeFromName( const wchar_t *name )
{
	if (wcsncmp(name,L"Immersive",9)==0)
	{
		unsigned int crc=CalcFNVHash(name+9);
		for (int i=0;i<(int)g_MetroColors.size();i++)
			if (g_MetroColors[i].nameHash==crc)
				return i;
	}
	return -1;
}

#else
typedef int (WINAPI *TGetImmersiveUserColorSetPreference)(bool bForceCheckRegistry, bool bSkipCheckOnFail);
typedef COLORREF (WINAPI *TGetImmersiveColorFromColorSetEx)(UINT dwImmersiveColorSet, UINT dwImmersiveColorType, bool bIgnoreHighContrast, UINT dwHighContrastCacheMode);
typedef int (WINAPI *TGetImmersiveColorTypeFromName)(const wchar_t *name);

static TGetImmersiveUserColorSetPreference GetImmersiveUserColorSetPreference;
static TGetImmersiveColorFromColorSetEx GetImmersiveColorFromColorSetEx;
static TGetImmersiveColorTypeFromName GetImmersiveColorTypeFromName;
#endif
static int g_CurrentMetroColorSet;

void MenuBitmap::Init( bool bIsColor )
{
	bIsBitmap=!bIsColor;
	bIsOwned=false;
	bitmap=NULL;
}

void MenuBitmap::Reset( bool bIsColor )
{
	if (bIsOwned && GetBitmap())
	{
		BOOL res=DeleteObject(bitmap);
		Assert(res);
	}
	Init(bIsColor);
}

MenuSkin::MenuSkin( void )
{
	Hash=0;
	AboutIcon=NULL;
	BOwnHintFont=false;
	BHasScrollbar=false;
	BHasMetroColors=false;
	Main_bitmap.Init();
	Main_bitmap_search.Init();
	Main_bitmap_jump.Init();
	Caption_font=NULL;
	User_font=NULL;
	Main_separatorV.Init();
	Main_pager.Init();
	Main_pager_arrows.Init();
	User_bitmap.Init();
	User_mask.Init();
	Programs_icon.Init();
	Programs_icon_selected.Init();
	Scrollbar_button.Init();
	Scrollbar_arrows.Init();
	Scrollbar_background.Init();
	Scrollbar_thumb.Init();
	Scrollbar_gripper.Init();
	Submenu_bitmap.Init();
	Submenu_separatorV.Init();
	Submenu_pager.Init();
	Submenu_pager_arrows.Init();
	Pin_bitmap.Init();
	More_bitmap.Init();
	Shutdown_bitmap.Init();
	Search_bitmap.Init();
	Search_arrow.Init();
	Search_background.Init();
	Search_background_search.Init();
	Search_background_jump.Init();
	for (int i=0;i<_countof(Main_emblems);i++)
		Main_emblems[i].Init();
	Main_emblem_mask.Init();
	Main_emblem_search_mask.Init();
	Main_emblem_jump_mask.Init();
	Search_emblem_mask.Init();
	Search_emblem_search_mask.Init();
	Search_emblem_jump_mask.Init();
	for (int i=0;i<_countof(Patterns);i++)
		Patterns[i].Init();
	Main_pattern_mask.Init();
	Main_pattern_search_mask.Init();
	Main_pattern_jump_mask.Init();
	Search_pattern_mask.Init();
	Search_pattern_search_mask.Init();
	Search_pattern_jump_mask.Init();
	Search_hint_font=NULL;
	Search_underline_font=NULL;
	for (int i=0;i<_countof(ItemSettings);i++)
		ItemSettings[i].Init();
}

void MenuSkin::ItemDrawSettings::Init( void )
{
	font=NULL;
	bOwnFont=true;
	glowSize=0;
	memset(textColors,0,sizeof(textColors));
	memset(textShadowColors,-1,sizeof(textShadowColors));
	memset(&textPadding,0,sizeof(textPadding));
	memset(selSlicesX,0,sizeof(selSlicesX));
	memset(selSlicesY,0,sizeof(selSlicesY));
	arrSize.cx=arrSize.cy=0;
	memset(arrColors,0,sizeof(arrColors));
	arrPadding.cx=arrPadding.cy=0;
	memset(frameSlicesX,0,sizeof(frameSlicesX));
	memset(frameSlicesY,0,sizeof(frameSlicesY));
	iconFrameOffset.x=iconFrameOffset.y=0;
	memset(&iconPadding,0,sizeof(iconPadding));
	memset(sepSlicesX,0,sizeof(sepSlicesX));
	sepHeight=0;
	opacity=MenuSkin::OPACITY_SOLID;
	textTopOffset=iconTopOffset=0;

	bmpSelection.Init();
	bmpArrow.Init();
	bmpIconFrame.Init();
	bmpSeparator.Init();
}

void MenuSkin::ItemDrawSettings::Reset( void )
{
	if (font && bOwnFont)
	{
		DeleteObject(font);
		font=NULL;
		bOwnFont=true;
	}
	bmpSelection.Reset();
	bmpArrow.Reset();
	bmpIconFrame.Reset();
	bmpSeparator.Reset();
}

MenuSkin::~MenuSkin( void )
{
	Reset();
}

void MenuSkin::Reset( void )
{
	if (AboutIcon) DestroyIcon(AboutIcon);
	AboutIcon=NULL;
	Main_bitmap.Reset();
	Main_bitmap_search.Reset();
	Main_bitmap_jump.Reset();
	if (Caption_font) DeleteObject(Caption_font);
	Caption_font=NULL;
	if (User_font) DeleteObject(User_font);
	User_font=NULL;
	Main_separatorV.Reset();
	User_bitmap.Reset();
	User_mask.Reset();
	Programs_icon.Reset();
	Programs_icon_selected.Reset();
	Scrollbar_button.Reset();
	Scrollbar_arrows.Reset();
	Scrollbar_background.Reset();
	Scrollbar_thumb.Reset();
	Scrollbar_gripper.Reset();
	Submenu_bitmap.Reset();
	Submenu_separatorV.Reset();
	Main_pager.Reset();
	Main_pager_arrows.Reset();
	Submenu_pager.Reset();
	Submenu_pager_arrows.Reset();
	Pin_bitmap.Reset();
	More_bitmap.Reset();
	Shutdown_bitmap.Reset();
	Search_bitmap.Reset();
	Search_arrow.Reset();
	Search_background.Reset();
	Search_background_search.Reset();
	Search_background_jump.Reset();
	for (int i=0;i<_countof(Main_emblems);i++)
		Main_emblems[i].Reset();
	Main_emblem_mask.Reset();
	Main_emblem_search_mask.Reset();
	Main_emblem_jump_mask.Reset();
	Search_emblem_mask.Reset();
	Search_emblem_search_mask.Reset();
	Search_emblem_jump_mask.Reset();
	for (int i=0;i<_countof(Patterns);i++)
	{
		Patterns[i].Reset();
		PatternBits[i].clear();
	}
	Main_pattern_mask.Reset();
	Main_pattern_search_mask.Reset();
	Main_pattern_jump_mask.Reset();
	Search_pattern_mask.Reset();
	Search_pattern_search_mask.Reset();
	Search_pattern_jump_mask.Reset();
	PatternPretileWidth=0;
	if (Search_hint_font && BOwnHintFont) DeleteObject(Search_hint_font);
	BOwnHintFont=false;
	BHasScrollbar=false;
	BHasMetroColors=false;
	Search_hint_font=NULL;
	if (Search_underline_font) DeleteObject(Search_underline_font);
	Search_underline_font=NULL;
	for (int i=0;i<_countof(ItemSettings);i++)
		ItemSettings[i].Reset();

	for (std::vector<CustomBitmap>::const_iterator it=CustomBitmaps.begin();it!=CustomBitmaps.end();++it)
		DeleteObject(it->bitmap);
	CustomBitmaps.clear();

	Options.clear();
	Variations.clear();
	Hash=0;
	MetroColorHash=0;
	MetroColors.clear();
}

static void GetErrorMessage( wchar_t *err, int size, DWORD code )
{
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,NULL,code,0,err,size,NULL);
}

int MenuSkin::ScaleSkinElement( int num, int scale ) const
{
	int ddpi=Dpi-96;
	if (ddpi<=0)
		return num;
	if (num<0)
		return (num*(9600+ddpi*scale)-4800)/9600;
	else
		return (num*(9600+ddpi*scale)+4800)/9600;
}

COLORREF MenuSkin::GetMetroColor( const wchar_t *names ) const
{
	unsigned int crc=CalcFNVHash(names);
	std::map<unsigned int,MetroColor>::const_iterator it=MetroColors.find(crc);
	if (it!=MetroColors.end())
		return it->second.color;

	MetroColor data={0};

	wchar_t name[100];
	while (*names)
	{
		if (names[0]=='#')
		{
			wchar_t *end;
			data.color=wcstoul(names+1,&end,16);
			break;
		}
		if (names[0]=='$')
			names++;
		names=GetToken(names,name,_countof(name),L"|");

		bool bFound=false;
		if (wcsncmp(name,L"System",6)==0)
		{
			for (int i=0;i<_countof(g_SystemColors);i++)
			{
				if (wcscmp(name,g_SystemColors[i].name)==0)
				{
					data.colorType=-2-g_SystemColors[i].code;
					COLORREF color=GetSysColor(g_SystemColors[i].code);
					data.color=((color&0xFF)<<16)|(color&0xFF00)|((color>>16)&0xFF);
					bFound=true;
					break;
				}
			}
		}

		if (!bFound)
		{
			BHasMetroColors=true;
			if (GetImmersiveUserColorSetPreference!=NULL)
			{
				wchar_t text[256];
				if (wcsncmp(name,L"Immersive",9)==0)
					wcscpy_s(text,name);
				else
					Sprintf(text,_countof(text),L"Immersive%s",name);
				int type=GetImmersiveColorTypeFromName(text);
				data.colorType=type<0?-1:type;
				if (type>=0)
				{
					DWORD color=GetImmersiveColorFromColorSetEx(g_CurrentMetroColorSet,type,true,0);
					data.color=((color&0xFF)<<16)|(color&0xFF00)|((color>>16)&0xFF);
					bFound=true;
				}
			}
		}
		if (bFound)
			break;
	}

	data.color|=0xFF000000;
	MetroColors[crc]=data;
	return data.color;
}

unsigned int MenuSkin::CalcMetroColorHash( int set ) const
{
	// if set is -1 returns the currently cached colors
	unsigned int hash=FNV_HASH0;
	for (std::map<unsigned int,MetroColor>::const_iterator it=MetroColors.begin();it!=MetroColors.end();++it)
	{
		COLORREF color=0;
		if (set==-1)
			color=it->second.color;
		else if (it->second.colorType<-1)
		{
			color=GetSysColor(-2-it->second.colorType);
			color=((color&0xFF)<<16)|(color&0xFF00)|((color>>16)&0xFF);
		}
		else if (it->second.colorType>=0)
		{
			color=GetImmersiveColorFromColorSetEx(set,it->second.colorType,true,0);
			color=((color&0xFF)<<16)|(color&0xFF00)|((color>>16)&0xFF);
		}
		color|=0xFF000000;
		hash=CalcFNVHash(&color,sizeof(color),hash);
	}
	return hash;
}

int MenuSkin::LoadSkinNumbers( const wchar_t *str, int *numbers, int count, TSkinNumberType type ) const
{
	memset(numbers,0,count*4);
	for (int i=0;i<count;i++)
	{
		wchar_t token[256];
		str=GetToken(str,token,_countof(token),L", \t");
		if (!token[0])
		{
			count=i;
			break;
		}
		int num;
		if (token[0]=='#')
		{
			wchar_t *end;
			num=wcstoul(token+1,&end,16);
		}
		else if (token[0]=='$')
			num=GetMetroColor(token+1);
		else
			num=_wtol(token);
		if (type==NUMBERS_COLORS_RGB)
			numbers[i]=num&0xFFFFFF;
		else if (type==NUMBERS_COLORS)
			numbers[i]=((num&0xFF)<<16)|(num&0xFF00)|((num>>16)&0xFF);
		else if (type==NUMBERS_COLORS_ABGR)
		{
			if (Strlen(token+1)==6)
				num|=0xFF000000;
			numbers[i]=((num&0xFF)<<16)|(num&0xFF00FF00)|((num>>16)&0xFF);
		}
		else
			numbers[i]=num;
	}

	if (type==NUMBERS_PADDING || type==NUMBERS_SIZE)
	{
		int scale=0;
		for (int i=0;i<count;i++)
		{
			wchar_t token[256];
			str=GetToken(str,token,_countof(token),L", \t");
			if (token[0] && token[Strlen(token)-1]=='%')
			{
				scale=_wtol(token);
				if (scale>100) scale=100;
			}
			numbers[i]=ScaleSkinElement(numbers[i],scale);
		}
	}
	return count;
}

RECT MenuSkin::ScaleSkinElement( const RECT &rect ) const
{
	RECT res;
	res.left=ScaleSkinElement(rect.left);
	res.right=ScaleSkinElement(rect.right);
	res.top=ScaleSkinElement(rect.top);
	res.bottom=ScaleSkinElement(rect.bottom);
	return res;
}

POINT MenuSkin::ScaleSkinElement( const POINT &point ) const
{
	POINT res;
	res.x=ScaleSkinElement(point.x);
	res.y=ScaleSkinElement(point.y);
	return res;
}

SIZE MenuSkin::ScaleSkinElement( const SIZE &size ) const
{
	SIZE res;
	res.cx=ScaleSkinElement(size.cx);
	res.cy=ScaleSkinElement(size.cy);
	return res;
}

_Success_(return != FALSE)
BOOL WINAPI SystemParametersInfoForDpi(_In_ UINT uiAction, _In_ UINT uiParam, _Pre_maybenull_ _Post_valid_ PVOID pvParam, _In_ UINT fWinIni, _In_ UINT dpi)
{
	static auto p = static_cast<decltype(&SystemParametersInfoForDpi)>((void*)GetProcAddress(GetModuleHandle(L"user32.dll"), "SystemParametersInfoForDpi"));
	if (p)
		return p(uiAction, uiParam, pvParam, fWinIni, dpi);

	// fall-back for older systems
	return SystemParametersInfo(uiAction, uiParam, pvParam, fWinIni);
}

HFONT MenuSkin::LoadSkinFont( const wchar_t *str, const wchar_t *name, int weight, float size, bool bScale ) const
{
	DWORD quality=DEFAULT_QUALITY;
	int smoothing=GetSettingInt(L"FontSmoothing");
	if (smoothing==1)
		quality=NONANTIALIASED_QUALITY;
	else if (smoothing==2)
		quality=ANTIALIASED_QUALITY;
	if (smoothing==3)
		quality=CLEARTYPE_QUALITY;
	wchar_t token[256];
	bool bItalic=false;
	int scale=bScale?100:0;
	if (str)
	{
		if (_wcsicmp(str,L"none")==0)
			return NULL;
		str=GetToken(str,token,_countof(token),L", \t");
		name=token;
		wchar_t token2[256];
		str=GetToken(str,token2,_countof(token2),L", \t");
		weight=FW_NORMAL;
		if (_wcsicmp(token2,L"bold")==0)
			weight=FW_BOLD;
		else if (_wcsicmp(token2,L"italic")==0)
			bItalic=true;
		else if (_wcsicmp(token2,L"bold_italic")==0)
			weight=FW_BOLD, bItalic=true;
		str=GetToken(str,token2,_countof(token2),L", \t");
		size=(float)_wtof(token2);
		str=GetToken(str,token2,_countof(token2),L", \t");
		if (token2[0] && token2[Strlen(token2)-1]=='%')
		{
			scale=_wtol(token2);
			if (scale<0) scale=0;
			if (scale>100) scale=100;
		}
	}
	else if (!name)
	{
		// get the default menu font
		NONCLIENTMETRICS metrics={sizeof(metrics)};
		SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,Dpi);
		metrics.lfMenuFont.lfQuality=(BYTE)quality;
		return CreateFontIndirect(&metrics.lfMenuFont);
	}
	size=ScaleSkinElement((int)(size*96),scale)/72.f;
	return CreateFont((int)size,0,0,0,weight,bItalic?1:0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,quality,DEFAULT_PITCH,name);
}

static HICON LoadSkinIcon( HMODULE hMod, int index )
{
	if (hMod)
	{
		return (HICON)LoadImage(hMod,MAKEINTRESOURCE(index),IMAGE_ICON,0,0,LR_DEFAULTSIZE);
	}
	else
	{
		wchar_t path[_MAX_PATH];
		GetSkinsPath(path);
		wchar_t fname[_MAX_PATH];
		Sprintf(fname,_countof(fname),L"%s%d.ico",path,index);
		return (HICON)LoadImage(NULL,fname,IMAGE_ICON,0,0,LR_DEFAULTSIZE|LR_LOADFROMFILE);
	}
}

struct DWMCOLORIZATIONPARAMS
{
	 DWORD ColorizationColor;
	 DWORD ColorizationAfterglow;
	 DWORD ColorizationColorBalance;
	 DWORD ColorizationAfterglowBalance;
	 DWORD ColorizationBlurBalance;
	 DWORD ColorizationGlassReflectionIntensity;
	 DWORD ColorizationOpaqueBlend;
	 DWORD extra; // Win8 has extra parameter
};

static bool GetSkinColorizationParameters( DWMCOLORIZATIONPARAMS *params )
{
	typedef HRESULT (WINAPI *tGetColorizationParameters)(DWMCOLORIZATIONPARAMS *params);

	// HACK: the system function DwmGetColorizationColor is buggy on Win 7. its calculations can overflow and return a totally wrong value
	// (try orange color with full intensity and no transparency - you'll get alpha=0 and green color). so here we use the undocumented
	// function GetColorizationParameters exported by dwmapi.dll, ordinal 127 and then compute the colors manually using integer math
	HMODULE hMod=GetModuleHandle(L"dwmapi.dll");
	if (!hMod) return false;
	tGetColorizationParameters GetColorizationParameters=(tGetColorizationParameters)GetProcAddress(hMod,MAKEINTRESOURCEA(127));
	if (!GetColorizationParameters || FAILED(GetColorizationParameters(params)))
		return false;
	if (GetWinVersion()>=WIN_VER_WIN8)
		params->ColorizationOpaqueBlend=0; // Win8 has no transparency
	return true;
}

COLORREF GetSystemGlassColor8( void )
{
	COLORREF color=0;
	DWMCOLORIZATIONPARAMS params;
	if (GetSkinColorizationParameters(&params))
	{
		int r=(params.ColorizationColor>>16)&255;
		int g=(params.ColorizationColor>>8)&255;
		int b=(params.ColorizationColor)&255;
		int gray=217*(100-params.ColorizationColorBalance)+50;
		r=(r*params.ColorizationColorBalance+gray)/100;
		g=(g*params.ColorizationColorBalance+gray)/100;
		b=(b*params.ColorizationColorBalance+gray)/100;
		r=(r*200+127)/255;
		g=(g*200+127)/255;
		b=(b*200+127)/255;
		color=(r<<16)|(g<<8)|b|(params.ColorizationColor&0xFF000000);
	}
	return color;
}

void GetSystemGlassColor( int &dr, int &dg, int &db, int &da, int &dc )
{
	dr=dg=db=da=dc=0;
	DWMCOLORIZATIONPARAMS params;
	if (GetSkinColorizationParameters(&params))
	{
		// boost the color balance to better match the Windows 7 menu
		params.ColorizationColorBalance=(int)(100.f*powf(params.ColorizationColorBalance/100.f,0.5f));
		int ir=(params.ColorizationColor>>16)&255;
		int ig=(params.ColorizationColor>>8)&255;
		int ib=(params.ColorizationColor)&255;

		int ir2=(params.ColorizationAfterglow>>16)&255;
		int ig2=(params.ColorizationAfterglow>>8)&255;
		int ib2=(params.ColorizationAfterglow)&255;

		int brightness=(ir*21+ig*72+ib*7)/255; // [0..100]
		int glowBalance=(brightness*params.ColorizationAfterglowBalance)/100; // [0..100]

		dr=MulDiv(ir2*glowBalance+ir*100,params.ColorizationColorBalance*255,10000);
		dg=MulDiv(ig2*glowBalance+ig*100,params.ColorizationColorBalance*255,10000);
		db=MulDiv(ib2*glowBalance+ib*100,params.ColorizationColorBalance*255,10000);
		dc=(glowBalance+100)*params.ColorizationColorBalance*255/10000;

		da=(100-params.ColorizationAfterglowBalance-params.ColorizationBlurBalance)*255/100;
		if (params.ColorizationOpaqueBlend || da>=255)
			da=255;
		else if (da<=0)
			dr=dg=db=da=0;
	}
}

void GetMetroGlassColor( int &dr, int &dg, int &db )
{
	if (GetImmersiveUserColorSetPreference!=NULL)
	{
		int type=GetImmersiveColorTypeFromName(L"ImmersiveStartBackground");
		if (type>=0)
		{
			int set=GetImmersiveUserColorSetPreference(false,false);
			DWORD color=GetImmersiveColorFromColorSetEx(set,type,true,0);
			db=((color>>16)&255);
			dg=((color>>8)&255);
			dr=((color)&255);
			return;
		}
	}
	dr=dg=db=0;
}

COLORREF GetMetroTaskbarColor( bool &bTransparent )
{
	bTransparent=true;
	if (GetImmersiveUserColorSetPreference!=NULL)
	{
		CRegKey regTheme;
		if (regTheme.Open(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",KEY_READ)==ERROR_SUCCESS)
		{
			DWORD trans=1;
			bTransparent=regTheme.QueryDWORDValue(L"EnableTransparency",trans)!=ERROR_SUCCESS || trans;
		}
		int type=GetImmersiveColorTypeFromName(bTransparent?L"ImmersiveSystemAccentDark3":L"ImmersiveSystemAccentDark2");
		if (type>=0)
		{
			int set=GetImmersiveUserColorSetPreference(false,false);
			return GetImmersiveColorFromColorSetEx(set,type,true,0);
		}
	}
	return 0;
}

COLORREF GetSystemAccentColor( void )
{
	if (GetImmersiveUserColorSetPreference!=NULL && GetWinVersion()>=WIN_VER_WIN10)
	{
		int type=GetImmersiveColorTypeFromName(L"ImmersiveSystemAccent");
		if (type>=0)
		{
			int set=GetImmersiveUserColorSetPreference(false,false);
			return GetImmersiveColorFromColorSetEx(set,type,true,0);
		}
	}
	return 0;
}

static void GetMenuGlassColor( int &dr, int &dg, int &db, int &da )
{
	if (GetWinVersion()>=WIN_VER_WIN10)
	{
		bool bDef=true;
		if (GetSettingBool(L"GlassOverride"))
		{
			COLORREF color=GetSettingInt(L"GlassColor",bDef);
			db=((color>>16)&255);
			dg=((color>>8)&255);
			dr=((color)&255);
		}
		if (bDef)
			GetMetroGlassColor(dr,dg,db);
		dr*=255;
		dg*=255;
		db*=255;
		da=255;
	}
	else
	{
		bool bDef=true;
		if (GetSettingBool(L"GlassOverride"))
		{
			bool bDef1, bDef2, bDef3;
			COLORREF color=GetSettingInt(L"GlassColor",bDef1);
			int intensity=GetSettingInt(L"GlassIntensity",bDef2);
			da=GetSettingInt(L"GlassBlending",bDef3);
			bDef=bDef1 && bDef2 && bDef3;
			if (!bDef)
			{
				db=((color>>16)&255);
				dg=((color>>8)&255);
				dr=((color)&255);

				if (intensity<0) intensity=0;
				if (intensity>100) intensity=100;
				intensity=(intensity*255)/100;
				dr*=intensity;
				dg*=intensity;
				db*=intensity;

				if (da<0) da=0;
				if (da>100) da=100;
				da=((100-da)*255)/100;
			}
		}

		if (bDef)
		{
			int dc;
			GetSystemGlassColor(dr,dg,db,da,dc);
		}
	}
}

const COLORREF DEFAULT_GLASS_COLOR=0x00000000;
const COLORREF DEFAULT_GLASS_CONFLICT=0x00000001;

static void BlendColor( int &r, int &g, int &b, int a1, int a2, int a3, COLORREF tintColor1, COLORREF tintColor2, COLORREF tintColor3 )
{
	int tr1, tg1, tb1;
	int ta2=tintColor2>>24, tr2=(tintColor2)&255, tg2=(tintColor2>>8)&255, tb2=(tintColor2>>16)&255;
	int ta3=tintColor3>>24, tr3=(tintColor3)&255, tg3=(tintColor3>>8)&255, tb3=(tintColor3>>16)&255;
	int a1m=1;
	if (tintColor1==DEFAULT_GLASS_COLOR)
	{
		a1m=g_GlassColorA;
		tr1=g_GlassColorR;
		tg1=g_GlassColorG;
		tb1=g_GlassColorB;
	}
	else
	{
		a1*=tintColor1>>24;
		tr1=(tintColor1)&255;
		tg1=(tintColor1>>8)&255;
		tb1=(tintColor1>>16)&255;
	}
	a2*=ta2;
	a3*=ta3;
	int a4=255*255-a1*a1m-a2-a3; if (a4<0) a4=0;
	r=(tr1*a1+tr2*a2+tr3*a3+r*a4)/(255*255);
	g=(tg1*a1+tg2*a2+tg3*a3+g*a4)/(255*255);
	b=(tb1*a1+tb2*a2+tb3*a3+b*a4)/(255*255);
	if (r>255) r=255;
	if (g>255) g=255;
	if (b>255) b=255;
}

static void BlendColor( COLORREF &color, COLORREF mask, COLORREF tintColor1, COLORREF tintColor2, COLORREF tintColor3 )
{
	int r=(color)&255, g=(color>>8)&255, b=(color>>16)&255;
	int a1=(mask)&255, a2=(mask>>8)&255, a3=(mask>>16)&255;
	BlendColor(r,g,b,a1,a2,a3,tintColor1,tintColor2,tintColor3);
	color=(b<<16)|(g<<8)|r;
}

static HBITMAP DuplicateBitmap( HBITMAP src )
{
	BITMAP info;
	GetObject(src,sizeof(info),&info);
	BITMAPINFO bi={0};
	bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth=info.bmWidth;
	bi.bmiHeader.biHeight=info.bmHeight;
	bi.bmiHeader.biPlanes=info.bmPlanes;
	bi.bmiHeader.biBitCount=info.bmBitsPixel;
	HDC hdc=CreateCompatibleDC(NULL);
	void *bits;
	HBITMAP dst=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,&bits,NULL,0);
	DeleteDC(hdc);
	memcpy(bits,info.bmBits,info.bmHeight*info.bmWidthBytes);
	return dst;
}

void MenuSkin::LoadSkinTintColors( CSkinParser &parser, const wchar_t *name, COLORREF &tintColor1, COLORREF &tintColor2, COLORREF &tintColor3, COLORREF backgroundColor ) const
{
	wchar_t name2[256];
	Sprintf(name2,_countof(name2),L"%s_tint1",name);
	const wchar_t *str=parser.FindSetting(name2);
	if (str && _wcsicmp(str,L"$Default")!=0 && LoadSkinNumbers(str,(int*)&tintColor1,1,NUMBERS_COLORS_ABGR))
	{
		if (tintColor1==DEFAULT_GLASS_COLOR)
			tintColor1=DEFAULT_GLASS_CONFLICT;
	}
	else
		tintColor1=DEFAULT_GLASS_COLOR;

	Sprintf(name2,_countof(name2),L"%s_tint2",name);
	str=parser.FindSetting(name2);
	if (!str || _wcsicmp(str,L"$Default")==0 || !LoadSkinNumbers(str,(int*)&tintColor2,1,NUMBERS_COLORS_ABGR))
		tintColor2=backgroundColor|0xFF000000;
	
	Sprintf(name2,_countof(name2),L"%s_tint3",name);
	str=parser.FindSetting(name2);
	if (!str || _wcsicmp(str,L"$Default")==0 || !LoadSkinNumbers(str,(int*)&tintColor3,1,NUMBERS_COLORS_ABGR))
		tintColor3=0;
}

bool MenuSkin::LoadSkinColors( CSkinParser &parser, const wchar_t *name, COLORREF *colors, int count, COLORREF backgroundColor, TSkinNumberType type ) const
{
	const wchar_t *str=parser.FindSetting(name);
	if (!str || !LoadSkinNumbers(str,(int*)colors,count,type))
		return false;

	if (count>10) return false;
	wchar_t name2[256];
	Sprintf(name2,_countof(name2),L"%s_mask",name);
	str=parser.FindSetting(name2);
	if (str)
	{
		COLORREF masks[10], tintColor1, tintColor2, tintColor3;
		if (LoadSkinNumbers(str,(int*)masks,count,NUMBERS_COLORS))
		{
			LoadSkinTintColors(parser,name,tintColor1,tintColor2,tintColor3,backgroundColor);
			for (int i=0;i<count;i++)
			{
				COLORREF a=colors[i]&0xFF000000;
				BlendColor(colors[i],masks[i],tintColor1,tintColor2,tintColor3);
				if (type==NUMBERS_COLORS_ABGR)
					colors[i]|=a;
			}
		}
	}
	return true;
}

MenuBitmap MenuSkin::LoadSkinBitmap( HMODULE hMod, int index, int maskIndex, COLORREF tintColor1, COLORREF tintColor2, COLORREF tintColor3, bool bPremultiply ) const
{
	// if maskIndex=0, no mask (black mask)
	// if maskIndex>0 - bitmap ID
	// if maskIndex<0 - color RGB
	MenuBitmap res;
	res.Init();
	wchar_t err[1024];
	HBITMAP bmp=NULL;
	if (index>0)
	{
		if ((index&0xFF000000)==0x0F000000)
		{
			bmp=DuplicateBitmap(CustomBitmaps[index&0xFFFFFF].bitmap);
		}
		else if (hMod)
		{
			bmp=(HBITMAP)LoadImage(hMod,MAKEINTRESOURCE(index),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
			if (!bmp)
				bmp=LoadImageResource(hMod,MAKEINTRESOURCE(index),false,false);
			if (!bmp)
			{
				GetErrorMessage(err,_countof(err),GetLastError());
				Sprintf(MenuSkin::s_SkinError,_countof(MenuSkin::s_SkinError),LoadStringEx(IDS_SKIN_ERR_BMPRES),index,err);
				return res;
			}
		}
		else
		{
			wchar_t path[_MAX_PATH];
			GetSkinsPath(path);
			wchar_t fname[_MAX_PATH];
			Sprintf(fname,_countof(fname),L"%s%d.bmp",path,index);
			bmp=(HBITMAP)LoadImage(NULL,fname,IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION|LR_LOADFROMFILE);
			if (!bmp)
			{
				GetErrorMessage(err,_countof(err),GetLastError());
				Sprintf(MenuSkin::s_SkinError,_countof(MenuSkin::s_SkinError),LoadStringEx(IDS_SKIN_ERR_BMPFILE),fname,err);
				return res;
			}
		}
	}

	HBITMAP bmpMask=NULL;
	BITMAP infoMask={0};
	if (maskIndex>0)
	{
		if ((maskIndex&0xFF000000)==0x0F000000)
		{
			bmpMask=DuplicateBitmap(CustomBitmaps[maskIndex&0xFFFFFF].bitmap);
		}
		else if (hMod)
		{
			bmpMask=(HBITMAP)LoadImage(hMod,MAKEINTRESOURCE(maskIndex),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
			if (!bmpMask)
				bmpMask=LoadImageResource(hMod,MAKEINTRESOURCE(maskIndex),false,false);
			if (!bmpMask)
			{
				GetErrorMessage(err,_countof(err),GetLastError());
				Sprintf(MenuSkin::s_SkinError,_countof(MenuSkin::s_SkinError),LoadStringEx(IDS_SKIN_ERR_MASKRES),maskIndex,err);
			}
		}
		else
		{
			wchar_t path[_MAX_PATH];
			GetSkinsPath(path);
			wchar_t fname[_MAX_PATH];
			Sprintf(fname,_countof(fname),L"%s%d.bmp",path,maskIndex);
			bmpMask=(HBITMAP)LoadImage(NULL,fname,IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION|LR_LOADFROMFILE);
			if (!bmpMask)
			{
				GetErrorMessage(err,_countof(err),GetLastError());
				Sprintf(MenuSkin::s_SkinError,_countof(MenuSkin::s_SkinError),LoadStringEx(IDS_SKIN_ERR_MASKFILE),fname,err);
			}
		}
		if (!bmpMask)
		{
			res=bmpMask;
			return res;
		}
		GetObject(bmpMask,sizeof(infoMask),&infoMask);
		if (index<0)
		{
			unsigned int colorRGB=index&0xFFFFFF;
			BITMAPINFO bi={0};
			bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
			bi.bmiHeader.biWidth=infoMask.bmWidth;
			bi.bmiHeader.biHeight=infoMask.bmHeight;
			bi.bmiHeader.biPlanes=1;
			bi.bmiHeader.biBitCount=32;
			HDC hdc=CreateCompatibleDC(NULL);
			unsigned int *bits;
			bmp=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,(void**)&bits,NULL,0);
			if (infoMask.bmBitsPixel==32)
			{
				unsigned int *ptrMask=(unsigned int*)infoMask.bmBits;
				for (int i=infoMask.bmWidth*infoMask.bmHeight-1;i>=0;i--)
					bits[i]=colorRGB|(ptrMask[i]&0xFF000000);
			}
			else
			{
				for (int i=infoMask.bmWidth*infoMask.bmHeight-1;i>=0;i--)
					bits[i]=colorRGB|0xFF000000;
			}
			DeleteDC(hdc);
		}
	}
	else if (maskIndex<0)
	{
		infoMask.bmBits=&maskIndex;
	}

	BITMAP info;
	GetObject(bmp,sizeof(info),&info);

	if (bmpMask && index>0 && (info.bmWidth!=infoMask.bmWidth || info.bmHeight!=infoMask.bmHeight))
	{
		Sprintf(MenuSkin::s_SkinError,_countof(MenuSkin::s_SkinError),LoadStringEx(IDS_SKIN_ERR_MASKSIZE),index,maskIndex);
	}

	if (maskIndex<0 || (bmpMask && info.bmWidth==infoMask.bmWidth && info.bmHeight==infoMask.bmHeight))
	{
		// apply color mask
		unsigned char *ptr=(unsigned char*)info.bmBits;
		int stride=info.bmBitsPixel/8;
		int pad=stride*info.bmWidth;
		pad=((pad+3)&~3)-pad;
		unsigned char *ptrMask=(unsigned char*)infoMask.bmBits;
		int strideMask=infoMask.bmBitsPixel/8;
		int padMask=strideMask*info.bmWidth;
		padMask=((padMask+3)&~3)-padMask;

		for (int y=0;y<info.bmHeight;y++)
		{
			for (int x=0;x<info.bmWidth;x++,ptr+=stride,ptrMask+=strideMask)
			{
				int r=ptr[2];
				int g=ptr[1];
				int b=ptr[0];
				BlendColor(r,g,b,ptrMask[2],ptrMask[1],ptrMask[0],tintColor1,tintColor2,tintColor3);
				ptr[2]=(unsigned char)r;
				ptr[1]=(unsigned char)g;
				ptr[0]=(unsigned char)b;
			}
			ptr+=pad;
			ptrMask+=padMask;
		}
	}
	if (bmpMask) DeleteObject(bmpMask);

	int n=info.bmWidth*info.bmHeight;
	res.bIs32=false;
	res.bIsOwned=true;
	res=bmp;
	if (info.bmBitsPixel<32)
		return res;

	// HACK: when LoadImage reads a 24-bit image it creates a 32-bit bitmap with 0 in the alpha channel
	// we use that to detect 24-bit images and don't pre-multiply the alpha
	for (int i=0;i<n;i++)
	{
		unsigned int &pixel=((unsigned int*)info.bmBits)[i];
		if (pixel&0xFF000000)
		{
			res.bIs32=true;
			break;
		}
	}

	if (res.bIs32 && bPremultiply)
	{
		// 32-bit bitmap detected. pre-multiply the alpha
		for (int i=0;i<n;i++)
		{
			unsigned int &pixel=((unsigned int*)info.bmBits)[i];
			int a=(pixel>>24);
			int r=(pixel>>16)&255;
			int g=(pixel>>8)&255;
			int b=(pixel)&255;
			r=(r*a)/255;
			g=(g*a)/255;
			b=(b*a)/255;
			pixel=(a<<24)|(r<<16)|(g<<8)|b;
		}
	}
	return res;
}

static void MirrorBitmap( HBITMAP bmp )
{
	BITMAP info;
	GetObject(bmp,sizeof(info),&info);

	unsigned char *ptr=(unsigned char*)info.bmBits;
	if (!ptr) return;
	int stride=info.bmBitsPixel/8;
	int pitch=(stride*info.bmWidth+3)&~3;
	for (int y=0;y<info.bmHeight;y++,ptr+=pitch)
	{
		unsigned char *start=ptr;
		unsigned char *end=ptr+stride*(info.bmWidth-1);
		for (int x=0;x<info.bmWidth;x+=2,start+=stride,end-=stride)
		{
			char q[4];
			memcpy(q,start,stride);
			memcpy(start,end,stride);
			memcpy(end,q,stride);
		}
	}
}

static bool FindToken( const wchar_t *str, const wchar_t *token )
{
	wchar_t text[256];
	while (*str)
	{
		str=GetToken(str,text,_countof(text),L" \t,");
		if (_wcsicmp(text,token)==0)
			return true;
	}
	return false;
}

bool MenuSkin::LoadSkinBackground( HMODULE hMod, CSkinParser &parser, const wchar_t *name, MenuBitmap &bitmap, COLORREF backgroundColor, int *slicesX, int countX, int *slicesY, int countY, bool bMirror, bool bAllowColor, bool bPremultiply ) const
{
	if (countX)
		memset(slicesX,0,countX*4);
	else if (slicesX)
		*slicesX=0;
	if (countY)
		memset(slicesY,0,countY*4);
	else if (slicesY)
		*slicesY=0;

	const wchar_t *str=parser.FindSetting(name);
	if (!str)
		return true;

	int id;
	if (str[0]=='#' || str[0]=='$')
	{
		LoadSkinNumbers(str,&id,1,NUMBERS_COLORS_RGB);
		id|=0xFF000000;
	}
	else if (str[0]=='@')
	{
		id=_wtol(str+1);
		if (id<0 || id>=(int)CustomBitmaps.size()) return true;
		id|=0x0F000000;
	}
	else
	{
		id=_wtol(str);
		if (id<=0) return true;
	}

	int id2;
	wchar_t name2[256];
	Sprintf(name2,_countof(name2),L"%s_mask",name);
	str=parser.FindSetting(name2);
	if (!str)
		id2=0;
	else if (str[0]=='#' || str[0]=='$')
	{
		LoadSkinNumbers(str,&id2,1,NUMBERS_COLORS_RGB);
		id2|=0xFF000000;
	}
	else if (str[0]=='@')
	{
		id2=_wtol(str+1);
		if (id2<0 || id2>=(int)CustomBitmaps.size()) return true;
		id2|=0x0F000000;
	}
	else
	{
		id2=_wtol(str);
		if (id2<0) id2=0;
	}

	if (id2)
	{
		COLORREF tintColor1, tintColor2, tintColor3;
		LoadSkinTintColors(parser,name,tintColor1,tintColor2,tintColor3,backgroundColor);
		if (id>0 || id2>0)
		{
			bitmap=LoadSkinBitmap(hMod,id,id2,tintColor1,tintColor2,tintColor3,bPremultiply);
		}
		else
		{
			COLORREF color=((id>>16)&0xFF)|(id&0xFF00)|((id&0xFF)<<16);
			COLORREF mask=((id2>>16)&0xFF)|(id2&0xFF00)|((id2&0xFF)<<16);
			BlendColor(color,mask,tintColor1,tintColor2,tintColor3);
			bitmap=color;
		}
	}
	else if (id>0)
	{
		bitmap=LoadSkinBitmap(hMod,id,0,0,0,0,bPremultiply);
	}
	else
	{
		COLORREF color=((id>>16)&0xFF)|(id&0xFF00)|((id&0xFF)<<16);
		bitmap=color;
	}

	if (!bitmap.bIsBitmap)
	{
		if (!bAllowColor)
			Sprintf(MenuSkin::s_SkinError,_countof(MenuSkin::s_SkinError),LoadStringEx(IDS_SKIN_ERR_RECBITMAP),name);
		return bAllowColor;
	}

	if (!bitmap.GetBitmap())
		return false;
	if (bMirror)
		MirrorBitmap(bitmap.GetBitmap());

	BITMAP info;
	if ((!countX && slicesX) || (!countY && slicesY))
		GetObject(bitmap.GetBitmap(),sizeof(info),&info);
	if (countX)
	{
		Sprintf(name2,_countof(name2),L"%s_slices_X",name);
		str=parser.FindSetting(name2);
		if (str)
			LoadSkinNumbers(str,slicesX,countX,NUMBERS_SLICES);
		else
			memset(slicesX,0,countX*4);
	}
	else if (slicesX)
		*slicesX=info.bmWidth;
	if (countY)
	{
		Sprintf(name2,_countof(name2),L"%s_slices_Y",name);
		str=parser.FindSetting(name2);
		if (str)
			LoadSkinNumbers(str,slicesY,countY,NUMBERS_SLICES);
		else
			memset(slicesY,0,countY*4);
	}
	else if (slicesY)
		*slicesY=info.bmHeight;
	return true;
}

bool MenuSkin::LoadSkinBitmap( HMODULE hMod, CSkinParser &parser, const wchar_t *name, MenuBitmap &bitmap, SIZE *size, bool bMirror, bool bPremultiply ) const
{
	int cx, cy;
	if (!LoadSkinBackground(hMod,parser,name,bitmap,0,&cx,0,&cy,0,bMirror,false,bPremultiply))
		return false;
	if (size)
	{
		size->cx=cx;
		size->cy=cy;
	}
	return true;
}

MenuSkin::THAlign MenuSkin::ParseHAlign( const wchar_t *str )
{
	if (str)
	{
		if (_wcsicmp(str,L"center1")==0)
			return HALIGN_CENTER1;
		else if (_wcsicmp(str,L"center2")==0)
			return HALIGN_CENTER2;
		else if (_wcsicmp(str,L"left")==0)
			return HALIGN_LEFT;
		else if (_wcsicmp(str,L"left1")==0)
			return HALIGN_LEFT1;
		else if (_wcsicmp(str,L"left2")==0)
			return HALIGN_LEFT2;
		else if (_wcsicmp(str,L"right")==0)
			return HALIGN_RIGHT;
		else if (_wcsicmp(str,L"right1")==0)
			return HALIGN_RIGHT1;
		else if (_wcsicmp(str,L"right2")==0)
			return HALIGN_RIGHT2;
		else if (_wcsicmp(str,L"corner")==0)
			return HALIGN_CORNER;
		else if (_wcsicmp(str,L"none")==0)
			return HALIGN_NONE;
	}
	return HALIGN_CENTER;
}

MenuSkin::TIconSize MenuSkin::ParseIconSize( const wchar_t *str )
{
	if (str)
	{
		if (_wcsicmp(str,L"small")==0)
			return ICON_SIZE_SMALL;
		else if (_wcsicmp(str,L"large")==0)
			return ICON_SIZE_LARGE;
		else if (_wcsicmp(str,L"none")==0)
			return ICON_SIZE_NONE;
	}
	return ICON_SIZE_UNDEFINED;
}

MenuSkin::TVAlign MenuSkin::ParseVAlign( const wchar_t *str )
{
	if (str)
	{
		if (_wcsicmp(str,L"top")==0)
			return VALIGN_TOP;
		else if (_wcsicmp(str,L"bottom")==0)
			return VALIGN_BOTTOM;
		else if (_wcsicmp(str,L"corner")==0)
			return VALIGN_CORNER;
		else if (_wcsicmp(str,L"none")==0)
			return VALIGN_NONE;
	}
	return VALIGN_CENTER;
}

bool MenuSkin::LoadSkinItem( HMODULE hMod, CSkinParser &parser, const wchar_t *name, MenuSkin::ItemDrawSettings &settings, MenuSkin::ItemDrawSettings *pDefaults, COLORREF backgroundColor, bool bRTL ) const
{
	wchar_t name2[256];
	const wchar_t *str;
	Sprintf(name2,_countof(name2),L"%s_font",name);
	str=parser.FindSetting(name2);
	if (str)
	{
		settings.font=LoadSkinFont(str,NULL,0,0,true);
		settings.bOwnFont=true;
	}
	else if (pDefaults)
	{
		settings.font=pDefaults->font;
		settings.bOwnFont=false;
	}
	else
	{
		settings.font=LoadSkinFont(NULL,NULL,0,0,true);
		settings.bOwnFont=true;
	}

	Sprintf(name2,_countof(name2),L"%s_glow_size",name);
	str=parser.FindSetting(name2);
	if (str)
		settings.glowSize=_wtol(str);
	else if (pDefaults)
		settings.glowSize=pDefaults->glowSize;
	else
		settings.glowSize=0;

	Sprintf(name2,_countof(name2),L"%s_text_color",name);
	if (LoadSkinColors(parser,name2,settings.textColors,_countof(settings.textColors),backgroundColor))
	{}
	else if (pDefaults)
		memcpy(settings.textColors,pDefaults->textColors,sizeof(settings.textColors));
	else
	{
		settings.textColors[0]=GetSysColor(COLOR_MENUTEXT);
		settings.textColors[1]=GetSysColor(COLOR_HIGHLIGHTTEXT);
		settings.textColors[2]=GetSysColor(COLOR_GRAYTEXT);
		settings.textColors[3]=GetSysColor(COLOR_HIGHLIGHTTEXT);
	}
	Sprintf(name2,_countof(name2),L"%s_text_shadow_color",name);
	str=parser.FindSetting(name2);
	if (str && wcscmp(str,L"none")==0)
		memset(settings.textShadowColors,-1,sizeof(settings.textShadowColors));
	else if (LoadSkinColors(parser,name2,settings.textShadowColors,_countof(settings.textShadowColors),backgroundColor))
	{}
	else if (pDefaults)
		memcpy(settings.textShadowColors,pDefaults->textShadowColors,sizeof(settings.textShadowColors));
	else
		memset(settings.textShadowColors,-1,sizeof(settings.textShadowColors));
	
	Sprintf(name2,_countof(name2),L"%s_text_padding",name);
	str=parser.FindSetting(name2);
	if (str)
		LoadSkinNumbers(str,(int*)&settings.textPadding,4,NUMBERS_PADDING);
	else if (pDefaults)
		settings.textPadding=pDefaults->textPadding;
	else
		settings.textPadding=ScaleSkinElement(DEFAULT_TEXT_PADDING);

	Sprintf(name2,_countof(name2),L"%s_icon_padding",name);
	str=parser.FindSetting(name2);
	if (str)
		LoadSkinNumbers(str,(int*)&settings.iconPadding,4,NUMBERS_PADDING);
	else if (pDefaults)
		settings.iconPadding=pDefaults->iconPadding;
	else
		settings.iconPadding=ScaleSkinElement(DEFAULT_ICON_PADDING);

	Sprintf(name2,_countof(name2),L"%s_arrow_color",name);
	if (LoadSkinColors(parser,name2,settings.arrColors,_countof(settings.arrColors),backgroundColor))
	{}
	else if (pDefaults)
		memcpy(settings.arrColors,pDefaults->arrColors,sizeof(settings.arrColors));
	else
	{
		settings.arrColors[0]=settings.textColors[0];
		settings.arrColors[1]=settings.textColors[1];
	}

	Sprintf(name2,_countof(name2),L"%s_selection",name);
	str=parser.FindSetting(name2);
	if (str)
	{
		if (!LoadSkinBackground(hMod,parser,name2,settings.bmpSelection,backgroundColor,settings.selSlicesX,_countof(settings.selSlicesX),settings.selSlicesY,_countof(settings.selSlicesY),bRTL,true))
			return false;
	}
	else if (pDefaults)
	{
		settings.bmpSelection=pDefaults->bmpSelection;
		settings.bmpSelection.bIsOwned=false;
		memcpy(settings.selSlicesX,pDefaults->selSlicesX,sizeof(settings.selSlicesX));
		memcpy(settings.selSlicesY,pDefaults->selSlicesY,sizeof(settings.selSlicesY));
	}
	else
	{
		settings.bmpSelection=GetSysColor(COLOR_HIGHLIGHT);
	}

	{
		const POINT *sizes=GetArrowsBitmapSizes();
		settings.arrSize.cx=sizes[3].y-sizes[3].x;
		settings.arrSize.cy=sizes[6].y;
	}
	Sprintf(name2,_countof(name2),L"%s_arrow",name);
	str=parser.FindSetting(name2);
	if (str && wcscmp(str,L"0")==0)
	{}
	else if (str)
	{
		if (!LoadSkinBitmap(hMod,parser,name2,settings.bmpArrow,&settings.arrSize,bRTL))
			return false;
		settings.arrSize.cy/=2;
	}
	else if (pDefaults)
	{
		settings.bmpArrow=pDefaults->bmpArrow;
		settings.bmpArrow.bIsOwned=false;
		settings.arrSize=pDefaults->arrSize;
	}

	Sprintf(name2,_countof(name2),L"%s_arrow_padding",name);
	str=parser.FindSetting(name2);
	if (str)
		LoadSkinNumbers(str,(int*)&settings.arrPadding,2,NUMBERS_PADDING);
	else if (pDefaults)
		settings.arrPadding=pDefaults->arrPadding;
	else
		settings.arrPadding=ScaleSkinElement(DEFAULT_ARROW_PADDING);

	Sprintf(name2,_countof(name2),L"%s_icon_frame",name);
	str=parser.FindSetting(name2);
	if (str)
	{
		if (!LoadSkinBackground(hMod,parser,name2,settings.bmpIconFrame,backgroundColor,settings.frameSlicesX,_countof(settings.frameSlicesX),settings.frameSlicesY,_countof(settings.frameSlicesY),bRTL))
			return false;

		if (settings.bmpIconFrame.GetBitmap())
		{
			Sprintf(name2,_countof(name2),L"%s_icon_frame_offset",name);
			str=parser.FindSetting(name2);
			if (str)
				LoadSkinNumbers(str,(int*)&settings.iconFrameOffset,2,NUMBERS_PADDING);
			else
				memset(&settings.iconFrameOffset,0,sizeof(settings.iconFrameOffset));
		}
	}
	else if (pDefaults)
	{
		settings.bmpIconFrame=pDefaults->bmpIconFrame;
		settings.bmpIconFrame.bIsOwned=false;
		memcpy(settings.frameSlicesX,pDefaults->frameSlicesX,sizeof(settings.frameSlicesX));
		memcpy(settings.frameSlicesY,pDefaults->frameSlicesY,sizeof(settings.frameSlicesY));
		settings.iconFrameOffset=pDefaults->iconFrameOffset;
	}

	return true;
}

void MenuSkin::ParseOptionsString( const wchar_t *optionsStr, std::map<CString,CString> &options ) const
{
	options.clear();
	wchar_t text[1024];
	const wchar_t *str=optionsStr;
	while (*str)
	{
		str=GetToken(str,text,_countof(text),L"\n");
		wchar_t token[256];
		const wchar_t *val=GetToken(text,token,_countof(token),L"=");
		if (*token)
			options[token]=val;
	}

	if (wcschr(optionsStr,'|'))
	{
		// backwards compatibility for old-style settings
		const wchar_t *end=wcschr(optionsStr,'\n');
		if (end && wcschr(end+1,'\n'))
			return; // multiple lines
		for (std::vector<Option>::const_iterator it=Options.begin();it!=Options.end();++it)
		{
			std::map<CString,CString>::const_iterator find=options.find(it->name);
			if (find==options.end())
			{
				unsigned int hash=CalcFNVHash(it->name)&0xFFFFFFFE;
				wchar_t text[100];
				Sprintf(text,_countof(text),L"%08X|",hash);
				if (wcsstr(optionsStr,text))
					options[it->name]=L"0";
				else
				{
					Sprintf(text,_countof(text),L"%08X|",hash+1);
					if (wcsstr(optionsStr,text))
						options[it->name]=L"1";
				}
			}
		}
	}
}

bool MenuSkin::ComputeOptionStates( const std::map<CString,CString> &options, std::vector<const wchar_t*> &values, bool bTranslateValues ) const
{
	values.clear();
	if (SkinType==SKIN_TYPE_ALL_PROGRAMS)
		values.push_back(L"ALL_PROGRAMS");
	if (SkinType==SKIN_TYPE_CLASSIC2)
		values.push_back(L"TWO_COLUMNS");
	// for compatibility with existing skins
	if (Dpi>=144)
		values.push_back(L"HIGH_DPI");
	if (Dpi>=240)
		values.push_back(L"240_DPI");	// 250% scaling
	else if (Dpi>=216)
		values.push_back(L"216_DPI");	// 225% scaling
	else if (Dpi>=192)
		values.push_back(L"192_DPI");	// 200% scaling
	else if (Dpi>=168)
		values.push_back(L"168_DPI");	// 175% scaling
	else if (Dpi>=144)
		values.push_back(L"144_DPI");	// 150% scaling
	else if (Dpi>=120)
		values.push_back(L"120_DPI");	// 125% scaling
	if (ForceTouch || (GetWinVersion()>=WIN_VER_WIN8 && GetSettingBool(L"EnableTouch") && (GetSystemMetrics(SM_DIGITIZER)&NID_INTEGRATED_TOUCH)!=0))
		values.push_back(L"TOUCH_ENABLED");
	if (GetSettingInt(L"SearchBox")!=SEARCHBOX_HIDDEN)
		values.push_back(L"SEARCHBOX");

	// determine bValue for radio buttons (they don't have conditions)
	for (std::vector<Option>::const_iterator it=Options.begin();it!=Options.end();++it)
	{
		const Option &option=*it;
		if (option.type==SKIN_OPTION_GROUP && option.groupId==-1) continue;
		std::map<CString,CString>::const_iterator find=options.find(option.name);
		if (option.type==SKIN_OPTION_BOOL)
			option.bValue=find!=options.end()?find->second==L"1":option.bDefValue;
		else
			option.bValue=find!=options.end() || option.bDefValue;
	}

	// determine if exactly one option is selected from each group
	for (std::vector<Option>::const_iterator it=Options.begin();it!=Options.end();++it)
	{
		const Option &option=*it;
		if (option.type!=SKIN_OPTION_GROUP) continue;
		int count=0;
		const Option *defOption=NULL;
		for (std::vector<Option>::const_iterator it2=it+1;it2!=Options.end();++it2)
		{
			const Option &option2=*it2;
			if (option2.type!=SKIN_OPTION_GROUP && option2.groupId==option.groupId)
			{
				if (option2.bValue)
					count++;
				if (!defOption && option2.bDefValue)
					defOption=&option2;
			}
		}
		if (count!=1)
		{
			for (std::vector<Option>::const_iterator it2=it+1;it2!=Options.end();++it2)
			{
				const Option &option2=*it2;
				if (option2.type!=SKIN_OPTION_GROUP && option2.groupId==option.groupId)
				{
					if (!defOption) defOption=&option2;
					option2.bValue=defOption==&option2;
				}
			}
		}
	}

	// calculate values
	for (std::vector<Option>::const_iterator it=Options.begin();it!=Options.end();++it)
	{
		const Option &option=*it;
		if (option.type==SKIN_OPTION_GROUP) continue;
		option.bEnabled=(option.condition.IsEmpty() || EvalCondition(option.condition,values.empty()?NULL:&values[0],(int)values.size()));

		const CString *optValue=NULL;
		{
			std::map<CString,CString>::const_iterator it=options.find(option.name);
			if (it!=options.end())
				optValue=&it->second;
		}
		if (option.type==SKIN_OPTION_BOOL)
		{
			option.sValue.Empty();
			if (option.groupId==-1)
			{
				if (!option.bEnabled)
					option.bValue=option.disValue==L"1";
				else if (optValue)
					option.bValue=*optValue==L"1";
				else
					option.bValue=option.bDefValue;
			}
		}
		else
		{
			if (option.groupId==-1)
				option.bValue=option.bEnabled && optValue;

			if (option.bValue && optValue)
				option.sValue=*optValue;
			else
				option.sValue=option.disValue;

			// translate some value types
			if (bTranslateValues)
			{
				if (option.type==SKIN_OPTION_IMAGE)
				{
					if (option.sValue.IsEmpty())
						option.sValue=L"0";
					else
					{
						wchar_t fname[_MAX_PATH];
						Strcpy(fname,_countof(fname),option.sValue);
						DoEnvironmentSubst(fname,_countof(fname));
						HBITMAP bmp=LoadImageResource(NULL,fname,false,false);
						if (bmp)
						{
							WIN32_FILE_ATTRIBUTE_DATA attributes={0};
							GetFileAttributesEx(fname,GetFileExInfoStandard,&attributes);
							option.sValue.Format(L"@%d",(int)CustomBitmaps.size());
							CustomBitmap custom;
							custom.bitmap=bmp;
							custom.path=fname;
							custom.timestamp=attributes.ftLastWriteTime;
							CustomBitmaps.push_back(custom);
						}
						else
						{
							Sprintf(MenuSkin::s_SkinError,_countof(MenuSkin::s_SkinError),LoadStringEx(IDS_SKIN_ERR_BMPFILE),fname,L"");
							return false;
						}
					}
				}
				else if (option.type==SKIN_OPTION_COLOR)
				{
					wchar_t *end;
					COLORREF color=wcstoul(option.sValue,&end,16);
					color=((color>>16)&0xFF)|(color&0xFF00)|((color&0xFF)<<16);
					option.sValue.Format(L"#%06X",color);
				}
			}
		}

		if (option.bValue)
			values.push_back(option.name);
	}
	return true;
}

void MenuSkin::SerializeOptionStates( std::vector<wchar_t> &buffer ) const
{
	for (std::vector<Option>::const_iterator it=Options.begin();it!=Options.end();++it)
	{
		if (it->type==SKIN_OPTION_GROUP) continue;
		wchar_t line[1024];
		int len=0;
		if (it->type==SKIN_OPTION_BOOL)
			len=Sprintf(line,_countof(line),L"%s=%d\n",it->name,it->bValue?1:0);
		else if (it->bValue)
			len=Sprintf(line,_countof(line),L"%s=%s\n",it->name,it->sValue);

		if (len)
			buffer.insert(buffer.end(),line,line+len);
	}
	buffer.push_back(0);
}

// new -> old
static const wchar_t *g_SkinSettingAliases[]={
	L"Main2_selection", L"Main_selection2",
	L"Main2_selection_mask", L"Main_selection_mask2",
	L"Main2_selection_slices_X", L"Main_selection_slices_X2",
	L"Main2_selection_slices_Y", L"Main_selection_slices_Y2",
	L"Main2_split_selection", L"Main_split_selection2",
	L"Main2_split_selection_mask", L"Main_split_selection_mask2",
	L"Main2_split_selection_slices_X", L"Main_split_selection_slices_X2",
	L"Main2_split_selection_slices_Y", L"Main_split_selection_slices_Y2",
	L"Main2_separator", L"Main_separator2",
	L"Main2_separator_slices_X", L"Main_separator_slices_X2",
	L"Main2_icon_frame", L"Main_icon_frame2",
	L"Main2_icon_frame_slices_X", L"Main_icon_frame_slices_X2",
	L"Main2_icon_frame_slices_Y", L"Main_icon_frame_slices_Y2",
	L"Main2_icon_frame_offset", L"Main_icon_frame_offset2",
	L"Main2_font",L"Main_font2",
	L"Main2_separator_font", L"Main_separator_font2",
	L"Main2_glow_size", L"Main_glow_size2",
	L"Main2_separator_glow_size", L"Main_separator_glow_size2",
	L"Main2_text_color", L"Main_text_color2",
	L"Main2_separator_text_color", L"Main_separator_text_color2",
	L"Main2_arrow", L"Main_arrow2",
	L"Main2_arrow_color", L"Main_arrow_color2",
	L"Main2_arrow_padding", L"Main_arrow_padding2",
	L"Main2_padding", L"Main_padding2",
	L"Main2_icon_padding", L"Main_icon_padding2",
	L"Main2_text_padding", L"Main_text_padding2",
	L"Main2_separator_text_padding", L"Main_separator_text_padding2",
	L"Main2_opacity", L"Main_opacity2",
	L"Main_separatorV_slices_Y", L"Main_separator_slices_Y",
	NULL
};

// Load the skin from the module. If hMod is NULL loads the "custom" skin from 1.txt
bool MenuSkin::LoadSkin( HMODULE hMod, const wchar_t *variation, const wchar_t *optionsStr, TSkinType skinType, unsigned int flags, int dpi )
{
	Version=1;
	SkinType=skinType;
	Dpi=dpi;
	CSkinParser parser;
	parser.SetAliases(g_SkinSettingAliases);
	if (hMod)
	{
		HRSRC hResInfo=FindResource(hMod,MAKEINTRESOURCE((hMod==g_Instance && skinType==SKIN_TYPE_WIN7)?2:1),L"SKIN");
		if (!hResInfo)
		{
			Strcpy(MenuSkin::s_SkinError,_countof(MenuSkin::s_SkinError),LoadStringEx(IDS_SKIN_ERR_FIND_RES1));
			return false;
		}
		if (!parser.LoadText(hMod,hResInfo))
		{
			Strcpy(MenuSkin::s_SkinError,_countof(MenuSkin::s_SkinError),LoadStringEx(IDS_SKIN_ERR_LOAD_RES1));
			return false;
		}
	}
	else
	{
		wchar_t path[_MAX_PATH];
		GetSkinsPath(path);
		Strcat(path,_countof(path),L"1.txt");
		if (!parser.LoadText(path))
		{
			Sprintf(MenuSkin::s_SkinError,_countof(MenuSkin::s_SkinError),LoadStringEx(IDS_SKIN_ERR_LOAD_FILE1),path);
			return false;
		}
	}
	parser.ParseText();

	const wchar_t *str=NULL;

	if (skinType==SKIN_TYPE_CLASSIC1)
		str=parser.FindSetting(L"Classic1_options");
	else if (skinType==SKIN_TYPE_CLASSIC2)
		str=parser.FindSetting(L"Classic2_options");
	else if (skinType==SKIN_TYPE_ALL_PROGRAMS)
		str=parser.FindSetting(L"AllPrograms_options");
	CString optionsList;
	if (str)
		optionsList=str;
	if (optionsList.IsEmpty() || FindToken(optionsList,L"variations"))
	{
		for (int i=1;;i++)
		{
			wchar_t name[20];
			Sprintf(name,_countof(name),L"Variation%d",i);
			str=parser.FindSetting(name);
			if (str)
			{
				LOG_MENU(LOG_OPEN,L"Variation setting: '%s'",str);
				wchar_t token[256];
				str=GetToken(str,token,_countof(token),L", \t");
				int  res=_wtol(token);
				str=GetToken(str,token,_countof(token),L", \t");
				Variation var;
				if (token[0]=='#')
				{
					int id=_wtol(token+1);
					Strcpy(token,_countof(token),LoadStringEnglish(id));
					wchar_t *find=wcschr(token,'#');
					if (find)
						*find=0;
					var.labelEn=token;
					Strcpy(token,_countof(token),LoadStringEx(id));
					
				}
				wchar_t *find=wcschr(token,'#');
				if (find)
				{
					var.tip=find+1;
					*find=0;
				}
				var.label=token;
				if (var.labelEn.IsEmpty())
					var.labelEn=var.label;
				Variations.emplace_back(res,var);
				LOG_MENU(LOG_OPEN,L"Variation found: name=%s, id=%d",token,res);
			}
			else
				break;
		}
	}

	if (variation)
	{
		for (std::vector<std::pair<int,Variation>>::const_iterator it=Variations.begin();it!=Variations.end();++it)
			if (_wcsicmp(variation,it->second.label)==0 || _wcsicmp(variation,it->second.labelEn)==0)
			{
				if (it->first<=1) break;
				LOG_MENU(LOG_OPEN,L"Loading variation: name=%s, id=%d",it->second,it->first);
				if (hMod)
				{
					HRSRC hResInfo=FindResource(hMod,MAKEINTRESOURCE(it->first),L"SKIN");
					if (!hResInfo)
					{
						Sprintf(MenuSkin::s_SkinError,_countof(MenuSkin::s_SkinError),LoadStringEx(IDS_SKIN_ERR_FIND_RES),it->first);
						break;
					}
					if (!parser.LoadVariation(hMod,hResInfo))
					{
						Sprintf(MenuSkin::s_SkinError,_countof(MenuSkin::s_SkinError),LoadStringEx(IDS_SKIN_ERR_LOAD_RES),it->first);
						break;
					}
				}
				else
				{
					wchar_t path[_MAX_PATH];
					GetSkinsPath(path);
					wchar_t name[20];
					Sprintf(name,_countof(name),L"%d.txt",it->first);
					Strcat(path,_countof(path),name);
					if (!parser.LoadVariation(path))
					{
						Sprintf(MenuSkin::s_SkinError,_countof(MenuSkin::s_SkinError),LoadStringEx(IDS_SKIN_ERR_LOAD_FILE),path);
						break;
					}
				}

				break;
			}
	}

	// parse options
	std::vector<CString> currentGroup;
	int groupId=-1;
	for (int lineIdx=0;;lineIdx++)
	{
		MenuSkin::Option option;
		CString label;
		if (!parser.ParseOption(option.name,option.type,label,option.bDefValue,option.condition,option.disValue,lineIdx))
			break;
		if (option.name.IsEmpty())
			continue;

		if (option.type!=SKIN_OPTION_GROUP && !optionsList.IsEmpty() && !FindToken(optionsList,option.name))
			continue;
		int find=label.Find('#');
		if (find>=0)
		{
			option.label=label.Mid(0,find);
			option.tip=label.Mid(find+1);
		}
		else
			option.label=label;
		if (option.type==SKIN_OPTION_GROUP)
		{
			const wchar_t *str=option.condition;
			while (*str)
			{
				wchar_t token[100];
				str=GetToken(str,token,_countof(token),L" \t|");
				CString name=token;
				currentGroup.push_back(name);
			}
			option.condition.Empty();
			option.groupId=++groupId;
			option.bEnabled=true;
			option.bValue=false;
		}
		else
		{
			option.groupId=-1;
			for (std::vector<CString>::const_iterator it=currentGroup.begin();it!=currentGroup.end();++it)
			{
				if (option.name==*it)
				{
					option.groupId=groupId;
					option.condition.Empty(); // radio groups can't be disabled
					break;
				}
			}

			if (option.name==L"TWO_COLUMNS" && option.type==SKIN_OPTION_BOOL)
			{
				// disable TWO_COLUMNS if it exists
				option.condition=L"FALSE";
				option.bDefValue=(skinType==SKIN_TYPE_CLASSIC2);
				option.disValue.Empty();
			}
		}

		option.bValue=false;
		Options.push_back(option);
	}
	// remove empty groups
	for (size_t i=0;i<Options.size();)
	{
		if (Options[i].type==SKIN_OPTION_GROUP && (i+1>=Options.size() || Options[i].groupId!=Options[i+1].groupId))
			Options.erase(Options.begin()+i);
		else
			i++;
	}

	str=parser.FindSetting(L"ForceTouch");
	ForceTouch=(str && _wtol(str)==1); // do before ComputeOptionStates

	std::map<CString,CString> options;
	ParseOptionsString(optionsStr,options);
	std::vector<const wchar_t*> values; // list of all true values
	if (!ComputeOptionStates(options,values,true))
		return false;

	if (values.empty())
		parser.FilterConditions(NULL,0);
	else
		parser.FilterConditions(&values[0],(int)values.size());

	std::vector<std::pair<CString,CString>> macros;
	for (std::vector<Option>::const_iterator it=Options.begin();it!=Options.end();++it)
	{
		if (it->type>SKIN_OPTION_BOOL && it->bValue)
			macros.push_back(std::make_pair(it->name,it->sValue));
	}
	if (!macros.empty())
		parser.ApplyMacros(macros);

	// parse settings
	str=parser.FindSetting(L"About");
	if (str)
	{
		if (str[0]=='#')
			About=LoadStringEx(_wtol(str+1));
		else
			About=str;
		About.Replace(L"\\n",L"\n");
	}
	else About.Empty();

	str=parser.FindSetting(L"AboutIcon");
	if (str) AboutIcon=LoadSkinIcon(hMod,_wtol(str));

	str=parser.FindSetting(L"Version");
	if (str)
		Version=_wtol(str);

	if (Version>MAX_SKIN_VERSION && (flags&LOADMENU_RESOURCES))
	{
		Strcpy(MenuSkin::s_SkinError,_countof(MenuSkin::s_SkinError),LoadStringEx(IDS_SKIN_ERR_VERSION));
		return false;
	}

	ForceRTL=false;
	if (!hMod)
	{
		str=parser.FindSetting(L"ForceRTL");
		if (str && _wtol(str))
			ForceRTL=true;
	}

	bool bRTL=ForceRTL || IsLanguageRTL();

	g_GlassColorR=g_GlassColorG=g_GlassColorB=g_GlassColorA=0;
	GetMenuGlassColor(g_GlassColorR,g_GlassColorG,g_GlassColorB,g_GlassColorA);

	if (skinType!=SKIN_TYPE_ALL_PROGRAMS && (flags&LOADMENU_RESOURCES))
	{
		if (!LoadSkinColors(parser,L"Main_background",&Main_background,1,GetSysColor(COLOR_MENU)))
			Main_background=GetSysColor(COLOR_MENU);

		// CAPTION SECTION - describes the caption portion of the main menu
		str=parser.FindSetting(L"Caption_font");
		Caption_font=LoadSkinFont(str,L"Segoe UI",FW_NORMAL,18,true);
		if (!LoadSkinColors(parser,L"Caption_text_color",&Caption_text_color,1,Main_background))
			Caption_text_color=0xFFFFFF;

		if (!LoadSkinColors(parser,L"Caption_glow_color",&Caption_glow_color,1,Main_background))
			Caption_glow_color=0xFFFFFF;

		str=parser.FindSetting(L"Caption_glow_size");
		if (str)
			Caption_glow_size=_wtol(str);
		else
			Caption_glow_size=0;

		str=parser.FindSetting(L"Caption_padding");
		if (str)
			LoadSkinNumbers(str,(int*)&Caption_padding0,4,NUMBERS_PADDING);
		else
			memset(&Caption_padding0,0,sizeof(Caption_padding0));


		// MENU SECTION - describes the menu portion of the main menu
		TwoColumns=(skinType==SKIN_TYPE_CLASSIC2 || skinType==SKIN_TYPE_WIN7);

		memset(Main_bitmap_slices_X,0,sizeof(Main_bitmap_slices_X));
		if (!LoadSkinBackground(hMod,parser,L"Main_bitmap",Main_bitmap,Main_background,Main_bitmap_slices_X+(TwoColumns?3:0),6,Main_bitmap_slices_Y,6,false))
			return false;

		str=parser.FindSetting(L"Main_opacity");
		Main_opacity=MenuSkin::OPACITY_SOLID;
		if (str && Main_bitmap.GetBitmap())
		{
			if (_wcsicmp(str,L"region")==0) Main_opacity=MenuSkin::OPACITY_REGION;
			if (_wcsicmp(str,L"alpha")==0) Main_opacity=MenuSkin::OPACITY_ALPHA;
			if (_wcsicmp(str,L"glass")==0) Main_opacity=MenuSkin::OPACITY_GLASS;
			if (_wcsicmp(str,L"fullalpha")==0) Main_opacity=MenuSkin::OPACITY_FULLALPHA;
			if (_wcsicmp(str,L"fullglass")==0) Main_opacity=MenuSkin::OPACITY_FULLGLASS;
		}
		Main2_opacity=Main_opacity;
		str=parser.FindSetting(L"Main2_opacity");
		if (str)
		{
			if (Main2_opacity==MenuSkin::OPACITY_ALPHA || Main2_opacity==MenuSkin::OPACITY_FULLALPHA)
			{
				if (_wcsicmp(str,L"alpha")==0) Main2_opacity=MenuSkin::OPACITY_ALPHA;
				if (_wcsicmp(str,L"fullalpha")==0) Main2_opacity=MenuSkin::OPACITY_FULLALPHA;
			}
			if (Main2_opacity==MenuSkin::OPACITY_GLASS || Main2_opacity==MenuSkin::OPACITY_FULLGLASS)
			{
				if (_wcsicmp(str,L"glass")==0) Main2_opacity=MenuSkin::OPACITY_GLASS;
				if (_wcsicmp(str,L"fullglass")==0) Main2_opacity=MenuSkin::OPACITY_FULLGLASS;
			}
		}
		Search_opacity=Main_opacity;
		str=parser.FindSetting(L"Search_opacity");
		if (str)
		{
			if (Search_opacity==MenuSkin::OPACITY_ALPHA || Search_opacity==MenuSkin::OPACITY_FULLALPHA)
			{
				if (_wcsicmp(str,L"alpha")==0) Search_opacity=MenuSkin::OPACITY_ALPHA;
				if (_wcsicmp(str,L"fullalpha")==0) Search_opacity=MenuSkin::OPACITY_FULLALPHA;
			}
			if (Main2_opacity==MenuSkin::OPACITY_GLASS || Main2_opacity==MenuSkin::OPACITY_FULLGLASS)
			{
				if (_wcsicmp(str,L"glass")==0) Search_opacity=MenuSkin::OPACITY_GLASS;
				if (_wcsicmp(str,L"fullglass")==0) Search_opacity=MenuSkin::OPACITY_FULLGLASS;
			}
		}
		Jumplist_opacity=Main_opacity;
		str=parser.FindSetting(L"Jumplist_opacity");
		if (str)
		{
			if (Jumplist_opacity==MenuSkin::OPACITY_ALPHA || Main2_opacity==MenuSkin::OPACITY_FULLALPHA)
			{
				if (_wcsicmp(str,L"alpha")==0) Jumplist_opacity=MenuSkin::OPACITY_ALPHA;
				if (_wcsicmp(str,L"fullalpha")==0) Jumplist_opacity=MenuSkin::OPACITY_FULLALPHA;
			}
			if (Jumplist_opacity==MenuSkin::OPACITY_GLASS || Main2_opacity==MenuSkin::OPACITY_FULLGLASS)
			{
				if (_wcsicmp(str,L"glass")==0) Jumplist_opacity=MenuSkin::OPACITY_GLASS;
				if (_wcsicmp(str,L"fullglass")==0) Jumplist_opacity=MenuSkin::OPACITY_FULLGLASS;
			}
		}

		Main_shadow=SHADOW_ON;
		str=parser.FindSetting(L"Main_shadow");
		if (str)
		{
			if (_wcsicmp(str,L"1")==0) Main_shadow=MenuSkin::SHADOW_ON;
			if (_wcsicmp(str,L"0")==0) Main_shadow=MenuSkin::SHADOW_OFF;
			if (_wcsicmp(str,L"glass")==0) Main_shadow=MenuSkin::SHADOW_GLASS;
		}

		str=parser.FindSetting(L"Main_thin_frame");
		Main_thin_frame=(str && _wtol(str));

		for (int i=0;i<_countof(Main_emblems);i++)
		{
			wchar_t name[100];
			Sprintf(name,_countof(name),L"Main_emblem%d",i+1);
			if (!LoadSkinBitmap(hMod,parser,name,Main_emblems[i],&Main_emblem_sizes[i],bRTL))
				return false;
			Sprintf(name,_countof(name),L"Main_emblem%d_padding",i+1);
			str=parser.FindSetting(name);
			if (str)
				LoadSkinNumbers(str,(int*)&Main_emblem_paddings[i],4,NUMBERS_PADDING);
			else
				memset(&Main_emblem_paddings[i],0,sizeof(Main_emblem_paddings[i]));
			Sprintf(name,_countof(name),L"Main_emblem%d_alignH",i+1);
			str=parser.FindSetting(name);
			if (str)
			{
				wchar_t token[100];
				str=GetToken(str,token,_countof(token),L"|");
				Main_emblem_alignH1[i]=ParseHAlign(token);
				Main_emblem_alignH2[i]=Main_emblem_alignH1[i];
				if (*str)
				{
					str=GetToken(str,token,_countof(token),L"|");
					Main_emblem_alignH2[i]=ParseHAlign(token);
				}
			}
			else
			{
				Main_emblem_alignH1[i]=HALIGN_CENTER;
				Main_emblem_alignH2[i]=HALIGN_CENTER;
			}
			Sprintf(name,_countof(name),L"Main_emblem%d_alignV",i+1);
			Main_emblem_alignV[i]=ParseVAlign(parser.FindSetting(name));
		}
		if (!LoadSkinBackground(hMod,parser,L"Main_emblem_mask",Main_emblem_mask,0,NULL,0,NULL,0,false,true,false))
			return false;

		if (!LoadSkinBitmap(hMod,parser,L"Pattern1",Patterns[0],&Pattern_sizes[0],bRTL))
			return false;
		if (!LoadSkinBitmap(hMod,parser,L"Pattern2",Patterns[1],&Pattern_sizes[1],bRTL))
			return false;
		if (!LoadSkinBitmap(hMod,parser,L"Pattern3",Patterns[2],&Pattern_sizes[2],bRTL))
			return false;
		if (!LoadSkinBitmap(hMod,parser,L"Pattern4",Patterns[3],&Pattern_sizes[3],bRTL))
			return false;
		if (!LoadSkinBackground(hMod,parser,L"Main_pattern_mask",Main_pattern_mask,0,NULL,0,NULL,0,false,true,false))
			return false;

		Main_icon_size=ParseIconSize(parser.FindSetting(L"Main_icon_size"));
		Main2_icon_size=ParseIconSize(parser.FindSetting(L"Main2_icon_size"));

		str=parser.FindSetting(L"Main_large_icons");
		bool Main_large_icons2=(str && _wtol(str));
		if (Main_large_icons2)
			Main_icon_size=ICON_SIZE_LARGE;

		if (skinType==SKIN_TYPE_WIN7)
			Main2_icon_size=ICON_SIZE_NONE;
		else
		{
			str=parser.FindSetting(L"Main_no_icons2");
			if (str && _wtol(str))
				Main2_icon_size=ICON_SIZE_NONE;
		}

		if (Main_icon_size==ICON_SIZE_UNDEFINED)
			Main_icon_size=ICON_SIZE_SMALL;
		if (Main2_icon_size==ICON_SIZE_UNDEFINED)
			Main2_icon_size=Main_icon_size;

		str=parser.FindSetting(L"Main_padding");
		if (str)
			LoadSkinNumbers(str,(int*)&Main_padding0,4,NUMBERS_PADDING);
		else
			memset(&Main_padding0,0,sizeof(Main_padding0));

		str=parser.FindSetting(L"Main_search_padding");
		if (str)
			LoadSkinNumbers(str,(int*)&Main_search_padding0,4,NUMBERS_PADDING);
		else
			memset(&Main_search_padding0,0,sizeof(Main_search_padding0));

		str=parser.FindSetting(L"Main_jump_padding");
		if (str)
			LoadSkinNumbers(str,(int*)&Main_jump_padding0,4,NUMBERS_PADDING);
		else
			memset(&Main_jump_padding0,0,sizeof(Main_jump_padding0));

		str=parser.FindSetting(L"Main2_padding");
		if (str)
			LoadSkinNumbers(str,(int*)&Main2_padding0,4,NUMBERS_PADDING);
		else
			memset(&Main2_padding0,-1,sizeof(Main2_padding0));

		str=parser.FindSetting(L"Main_search_indent");
		if (str)
			LoadSkinNumbers(str,(int*)&Main_search_indent,1,NUMBERS_SIZE);
		else
			Main_search_indent=0;

		// first column
		if (!LoadSkinItem(hMod,parser,L"Main",ItemSettings[COLUMN1_ITEM],NULL,Main_background,bRTL))
			return false;
		if (!LoadSkinItem(hMod,parser,L"Main_split",ItemSettings[COLUMN1_SPLIT],&ItemSettings[COLUMN1_ITEM],Main_background,bRTL))
			return false;
		if (!LoadSkinItem(hMod,parser,L"Main_new",ItemSettings[COLUMN1_NEW],&ItemSettings[COLUMN1_ITEM],Main_background,bRTL))
			return false;
		if (!LoadSkinItem(hMod,parser,L"Main_separator",ItemSettings[COLUMN1_SEPARATOR],&ItemSettings[COLUMN1_ITEM],Main_background,bRTL))
			return false;
		ItemSettings[COLUMN1_SEPARATOR].sepHeight=ScaleSkinElement(DEFAULT_SEPARATOR_HEIGHT);
		str=parser.FindSetting(L"Main_separator");
		if (str)
		{
			if (!LoadSkinBackground(hMod,parser,L"Main_separator",ItemSettings[COLUMN1_SEPARATOR].bmpSeparator,Main_background,ItemSettings[COLUMN1_SEPARATOR].sepSlicesX,_countof(ItemSettings[COLUMN1_SEPARATOR].sepSlicesX),&ItemSettings[COLUMN1_SEPARATOR].sepHeight,0,bRTL))
				return false;
		}

		bHasNewItem=parser.FindSetting(L"Main_new_selection")!=NULL;

		// second column
		if (!LoadSkinItem(hMod,parser,L"Main2",ItemSettings[COLUMN2_ITEM],&ItemSettings[COLUMN1_ITEM],Main_background,bRTL))
			return false;
		if (!LoadSkinItem(hMod,parser,L"Main2_split",ItemSettings[COLUMN2_SPLIT],&ItemSettings[COLUMN2_ITEM],Main_background,bRTL))
			return false;
		if (!LoadSkinItem(hMod,parser,L"Main2_new",ItemSettings[COLUMN2_NEW],&ItemSettings[COLUMN2_ITEM],Main_background,bRTL))
			return false;
		ItemSettings[COLUMN2_SEPARATOR].sepHeight=ScaleSkinElement(DEFAULT_SEPARATOR_HEIGHT);
		str=parser.FindSetting(L"Main2_separator");
		if (str)
		{
			if (!LoadSkinBackground(hMod,parser,L"Main2_separator",ItemSettings[COLUMN2_SEPARATOR].bmpSeparator,Main_background,ItemSettings[COLUMN2_SEPARATOR].sepSlicesX,_countof(ItemSettings[COLUMN2_SEPARATOR].sepSlicesX),&ItemSettings[COLUMN2_SEPARATOR].sepHeight,0,bRTL))
				return false;
		}
		else
		{
			ItemSettings[COLUMN2_SEPARATOR].bmpSeparator=ItemSettings[COLUMN1_SEPARATOR].bmpSeparator;
			ItemSettings[COLUMN2_SEPARATOR].bmpSeparator.bIsOwned=false;
			ItemSettings[COLUMN2_SEPARATOR].sepHeight=ItemSettings[COLUMN1_SEPARATOR].sepHeight;
			memcpy(ItemSettings[COLUMN2_SEPARATOR].sepSlicesX,ItemSettings[COLUMN1_SEPARATOR].sepSlicesX,sizeof(ItemSettings[COLUMN2_SEPARATOR].sepSlicesX));
		}

		Main_separatorWidth=ScaleSkinElement(DEFAULT_SEPARATOR_WIDTH);
		str=parser.FindSetting(L"Main_separatorV");
		if (str)
		{
			if (!LoadSkinBackground(hMod,parser,L"Main_separatorV",Main_separatorV,Main_background,&Main_separatorWidth,0,Main_separator_slices_Y,_countof(Main_separator_slices_Y),false))
				return false;
		}

		if (!LoadSkinBackground(hMod,parser,L"Main_pager",Main_pager,Main_background,Main_pager_slices_X,_countof(Main_pager_slices_X),Main_pager_slices_Y,_countof(Main_pager_slices_Y),bRTL))
			return false;

		if (!LoadSkinBitmap(hMod,parser,L"Main_pager_arrows",Main_pager_arrows,&Main_pager_arrow_Size,bRTL))
			return false;
		Main_pager_arrow_Size.cx/=2;
		Main_pager_arrow_Size.cy/=2;

		if (!LoadSkinBitmap(hMod,parser,L"User_bitmap",User_bitmap,&User_bitmapSize,bRTL))
			return false;
		if (!LoadSkinBitmap(hMod,parser,L"User_mask",User_mask,&User_maskSize,bRTL))
			return false;
		User_bitmap_outside=false;
		str=parser.FindSetting(L"User_bitmap_outside");
		if (str)
		{
			User_bitmap_outside=_wtol(str)>0;
		}

		str=parser.FindSetting(L"User_image_size");
		if (str)
		{
			LoadSkinNumbers(str,(int*)&User_image_size,1,(skinType==SKIN_TYPE_WIN7)?NUMBERS_OTHER:NUMBERS_SIZE);
			if (User_image_size<0) User_image_size=0;
			if (User_image_size>128) User_image_size=128;
		}
		else
			User_image_size=(skinType==SKIN_TYPE_WIN7)?48:0;

		if (User_bitmapSize.cx==0 && User_maskSize.cx>0 && User_image_size>0)
		{
			User_bitmapSize.cx=User_bitmapSize.cy=User_image_size>64?User_image_size:64;
		}

		memset(&User_frame_position,0,sizeof(User_frame_position));
		str=parser.FindSetting(L"User_frame_position");
		if (str)
		{
			if (skinType!=SKIN_TYPE_WIN7)
			{
				wchar_t token[256];
				const wchar_t *str2=GetToken(str,token,_countof(token),L", \t");
				if (_wcsicmp(token,L"center")==0)
					User_frame_position.x=MenuSkin::USER_CENTER;
				else if (_wcsicmp(token,L"center1")==0)
					User_frame_position.x=MenuSkin::USER_CENTER1;
				else if (_wcsicmp(token,L"center2")==0)
					User_frame_position.x=MenuSkin::USER_CENTER2;
				if (User_frame_position.x==0)
					LoadSkinNumbers(str,(int*)&User_frame_position,2,NUMBERS_PADDING);
				else
					LoadSkinNumbers(str2,(int*)&User_frame_position.y,1,NUMBERS_PADDING);
			}
			else
			{
				LoadSkinNumbers(str,(int*)&User_frame_position,1,NUMBERS_PADDING);
			}
		}
		else if (skinType==SKIN_TYPE_WIN7)
			User_frame_position.x=36;

		str=parser.FindSetting(L"User_image_offset");
		if (str && User_bitmap.GetBitmap())
			LoadSkinNumbers(str,(int*)&User_image_offset,2,NUMBERS_OTHER);
		else if (skinType==SKIN_TYPE_WIN7)
		{
			User_image_offset.x=(User_bitmapSize.cx-User_image_size)/2;
			User_image_offset.y=(User_bitmapSize.cy-User_image_size)/2;
		}
		else
			User_image_offset.x=User_image_offset.y=2;

		if (skinType==SKIN_TYPE_WIN7)
		{
			if (User_image_size>User_bitmapSize.cx)
				User_image_size=User_bitmapSize.cx;
			if (User_image_size>User_bitmapSize.cy)
				User_image_size=User_bitmapSize.cy;
			if (User_image_offset.x>User_bitmapSize.cx-User_image_size)
				User_image_offset.x=User_bitmapSize.cx-User_image_size;
			if (User_image_offset.y>User_bitmapSize.cy-User_image_size)
				User_image_offset.y=User_bitmapSize.cy-User_image_size;
		}

		str=parser.FindSetting(L"User_image_alpha");
		if (str)
			User_image_alpha=_wtol(str)&255;
		else
			User_image_alpha=255;

		User_image_padding=ScaleSkinElement(DEFAULT_USER_IMAGE_PADDING);
		str=parser.FindSetting(L"User_image_padding");
		if (str)
			LoadSkinNumbers(str,(int*)&User_image_padding,2,NUMBERS_PADDING);

		str=parser.FindSetting(L"User_name_position");
		if (str)
			LoadSkinNumbers(str,(int*)&User_name_position,4,NUMBERS_SIZE);
		else
			memset(&User_name_position,0,sizeof(User_name_position));

		User_name_align=ParseHAlign(parser.FindSetting(L"User_name_align"));

		str=parser.FindSetting(L"User_font");
		User_font=LoadSkinFont(str,L"Segoe UI",FW_NORMAL,18,false);
		if (!LoadSkinColors(parser,L"User_text_color",&User_text_color,1,Main_background))
			User_text_color=GetSysColor(COLOR_MENUTEXT);

		if (!LoadSkinColors(parser,L"User_glow_color",&User_glow_color,1,Main_background))
			User_glow_color=0xFFFFFF;

		str=parser.FindSetting(L"User_glow_size");
		if (str)
			User_glow_size=_wtol(str);
		else
			User_glow_size=0;

		if (Main_bitmap_slices_X[1]==0)
		{
			Main_bitmap_slices_X[0]=Main_bitmap_slices_X[2]=0;
			memset(&Caption_padding0,0,sizeof(Caption_padding0));
		}

		str=parser.FindSetting(L"Search_hint_font");
		if (str)
		{
			Search_hint_font=LoadSkinFont(str,NULL,0,0,true);
			BOwnHintFont=true;
		}
		else
		{
			Search_hint_font=ItemSettings[COLUMN1_ITEM].font;
			BOwnHintFont=false;
		}

		if (!LoadSkinColors(parser,L"Search_text_color",Search_text_colors,2,Main_background))
		{
			Search_text_colors[0]=GetSysColor(COLOR_WINDOWTEXT);
			Search_text_colors[1]=GetSysColor(COLOR_GRAYTEXT);
		}
		if (!LoadSkinColors(parser,L"Search_text_background",&Search_text_background,1,Main_background))
			Search_text_background=GetSysColor(COLOR_WINDOW);

		if (skinType==SKIN_TYPE_WIN7)
		{
			memset(Main_bitmap_search_slices_X,0,sizeof(Main_bitmap_search_slices_X));
			if (!LoadSkinBackground(hMod,parser,L"Main_bitmap_search",Main_bitmap_search,Main_background,Main_bitmap_search_slices_X+3,6,Main_bitmap_search_slices_Y,6,false))
				return false;
			memset(Main_bitmap_jump_slices_X,0,sizeof(Main_bitmap_jump_slices_X));
			if (!LoadSkinBackground(hMod,parser,L"Main_bitmap_jump",Main_bitmap_jump,Main_background,Main_bitmap_jump_slices_X+3,6,Main_bitmap_jump_slices_Y,6,false))
				return false;

			if (!LoadSkinBackground(hMod,parser,L"Main_emblem_search_mask",Main_emblem_search_mask,0,NULL,0,NULL,0,false,true,false))
				return false;
			if (!LoadSkinBackground(hMod,parser,L"Main_emblem_jump_mask",Main_emblem_jump_mask,0,NULL,0,NULL,0,false,true,false))
				return false;
			if (!LoadSkinBackground(hMod,parser,L"Main_pattern_search_mask",Main_pattern_search_mask,0,NULL,0,NULL,0,false,true,false))
				return false;
			if (!LoadSkinBackground(hMod,parser,L"Main_pattern_jump_mask",Main_pattern_jump_mask,0,NULL,0,NULL,0,false,true,false))
				return false;

			// Shutdown button
			if (!LoadSkinItem(hMod,parser,L"Shutdown",ItemSettings[SHUTDOWN_BUTTON],&ItemSettings[COLUMN1_ITEM],Main_background,bRTL))
				return false;
			str=parser.FindSetting(L"Shutdown_text_color");
			if (!str)
				ItemSettings[SHUTDOWN_BUTTON].textColors[0]=ItemSettings[SHUTDOWN_BUTTON].textColors[1]=GetSysColor(COLOR_BTNTEXT);
			str=parser.FindSetting(L"Shutdown_arrow_color");
			if (!str)
				ItemSettings[SHUTDOWN_BUTTON].arrColors[0]=ItemSettings[SHUTDOWN_BUTTON].arrColors[1]=GetSysColor(COLOR_BTNTEXT);
			if (!LoadSkinItem(hMod,parser,L"Shutdown_search",ItemSettings[SHUTDOWN_BUTTON_SEARCH],&ItemSettings[SHUTDOWN_BUTTON],Main_background,bRTL))
				return false;
			memcpy(ItemSettings[SHUTDOWN_BUTTON_SEARCH].selSlicesX,ItemSettings[SHUTDOWN_BUTTON].selSlicesX,sizeof(ItemSettings[SHUTDOWN_BUTTON_SEARCH].selSlicesX));
			memcpy(ItemSettings[SHUTDOWN_BUTTON_SEARCH].selSlicesY,ItemSettings[SHUTDOWN_BUTTON].selSlicesY,sizeof(ItemSettings[SHUTDOWN_BUTTON_SEARCH].selSlicesY));
			if (!LoadSkinItem(hMod,parser,L"Shutdown_jump",ItemSettings[SHUTDOWN_BUTTON_JUMP],&ItemSettings[SHUTDOWN_BUTTON],Main_background,bRTL))
				return false;
			memcpy(ItemSettings[SHUTDOWN_BUTTON_JUMP].selSlicesX,ItemSettings[SHUTDOWN_BUTTON].selSlicesX,sizeof(ItemSettings[SHUTDOWN_BUTTON_JUMP].selSlicesX));
			memcpy(ItemSettings[SHUTDOWN_BUTTON_JUMP].selSlicesY,ItemSettings[SHUTDOWN_BUTTON].selSlicesY,sizeof(ItemSettings[SHUTDOWN_BUTTON_JUMP].selSlicesY));
			str=parser.FindSetting(L"Shutdown_padding");
			if (str)
				LoadSkinNumbers(str,(int*)&Shutdown_padding,4,NUMBERS_PADDING);
			else
				memset(&Shutdown_padding,0,sizeof(Shutdown_padding));

			// List items
			if (!LoadSkinItem(hMod,parser,L"List",ItemSettings[LIST_ITEM],&ItemSettings[COLUMN1_ITEM],Main_background,bRTL))
				return false;
			if (!LoadSkinItem(hMod,parser,L"List_split",ItemSettings[LIST_SPLIT],&ItemSettings[LIST_ITEM],Main_background,bRTL))
				return false;
			if (!LoadSkinItem(hMod,parser,L"List_separator",ItemSettings[LIST_SEPARATOR],&ItemSettings[LIST_ITEM],Main_background,bRTL))
				return false;
			if (!LoadSkinItem(hMod,parser,L"List_separator_split",ItemSettings[LIST_SEPARATOR_SPLIT],&ItemSettings[LIST_SPLIT],Main_background,bRTL))
				return false;
			ItemSettings[LIST_SEPARATOR].sepHeight=ScaleSkinElement(DEFAULT_SEPARATOR_HEIGHT);
			str=parser.FindSetting(L"List_separator");
			if (str)
			{
				if (!LoadSkinBackground(hMod,parser,L"List_separator",ItemSettings[LIST_SEPARATOR].bmpSeparator,Main_background,ItemSettings[LIST_SEPARATOR].sepSlicesX,_countof(ItemSettings[LIST_SEPARATOR].sepSlicesX),&ItemSettings[LIST_SEPARATOR].sepHeight,0,bRTL))
					return false;
			}
			else
			{
				ItemSettings[LIST_SEPARATOR].bmpSeparator=ItemSettings[COLUMN1_SEPARATOR].bmpSeparator;
				ItemSettings[LIST_SEPARATOR].bmpSeparator.bIsOwned=false;
				ItemSettings[LIST_SEPARATOR].sepHeight=ItemSettings[COLUMN1_SEPARATOR].sepHeight;
				memcpy(ItemSettings[LIST_SEPARATOR].sepSlicesX,ItemSettings[COLUMN1_SEPARATOR].sepSlicesX,sizeof(ItemSettings[COLUMN1_SEPARATOR].sepSlicesX));
			}
			str=parser.FindSetting(L"List_separator_split");
			if (str)
			{
				if (!LoadSkinBackground(hMod,parser,L"List_separator_split",ItemSettings[LIST_SEPARATOR_SPLIT].bmpSeparator,Main_background,ItemSettings[LIST_SEPARATOR_SPLIT].sepSlicesX,_countof(ItemSettings[LIST_SEPARATOR_SPLIT].sepSlicesX),&ItemSettings[LIST_SEPARATOR_SPLIT].sepHeight,0,bRTL))
					return false;
			}
			else
			{
				ItemSettings[LIST_SEPARATOR_SPLIT].bmpSeparator=ItemSettings[COLUMN1_SEPARATOR].bmpSeparator;
				ItemSettings[LIST_SEPARATOR_SPLIT].bmpSeparator.bIsOwned=false;
				ItemSettings[LIST_SEPARATOR_SPLIT].sepHeight=ItemSettings[COLUMN1_SEPARATOR].sepHeight;
				memcpy(ItemSettings[LIST_SEPARATOR_SPLIT].sepSlicesX,ItemSettings[COLUMN1_SEPARATOR].sepSlicesX,sizeof(ItemSettings[COLUMN1_SEPARATOR].sepSlicesX));
			}

			// PROGRAMS SECTION
			str=parser.FindSetting(L"Programs_background");
			if (str && _wcsicmp(str,L"transparent")==0)
				Programs_background=0;
			else
			{
				if (!LoadSkinColors(parser,L"Programs_background",&Programs_background,1,Main_background,NUMBERS_COLORS_ABGR))
					Programs_background=GetSysColor(COLOR_WINDOW);

				// premultiply alpha
				int a=Programs_background>>24;
				if (a==0)
					Programs_background|=0xFF000000;
				else if (a<255)
				{
					int r=(Programs_background&255)*a/255;
					int g=((Programs_background>>8)&255)*a/255;
					int b=((Programs_background>>16)&255)*a/255;
					Programs_background=(a<<24)|(b<<16)|(g<<8)|r;
				}
			}

			if (!LoadSkinItem(hMod,parser,L"Programs_button",ItemSettings[PROGRAMS_BUTTON],&ItemSettings[COLUMN1_ITEM],Programs_background,bRTL))
				return false;
			if (!LoadSkinItem(hMod,parser,L"Programs_button_new",ItemSettings[PROGRAMS_BUTTON_NEW],&ItemSettings[COLUMN1_NEW],Programs_background,bRTL))
				return false;
			if (!LoadSkinItem(hMod,parser,L"Programs_cascading",ItemSettings[PROGRAMS_CASCADING],&ItemSettings[PROGRAMS_BUTTON],Programs_background,bRTL))
				return false;
			if (!LoadSkinItem(hMod,parser,L"Programs_cascading_new",ItemSettings[PROGRAMS_CASCADING_NEW],&ItemSettings[PROGRAMS_BUTTON_NEW],Programs_background,bRTL))
				return false;
			if (!LoadSkinItem(hMod,parser,L"Programs",ItemSettings[PROGRAMS_TREE_ITEM],&ItemSettings[COLUMN1_ITEM],Programs_background,bRTL))
				return false;
			if (!LoadSkinItem(hMod,parser,L"Programs_new",ItemSettings[PROGRAMS_TREE_NEW],&ItemSettings[PROGRAMS_TREE_ITEM],Programs_background,bRTL))
				return false;
			str=parser.FindSetting(L"Programs_indent");
			if (str)
				LoadSkinNumbers(str,&Programs_indent,1,NUMBERS_SIZE);
			else
				Programs_indent=0;

			if (!LoadSkinBitmap(hMod,parser,L"Programs_icon",Programs_icon,&Programs_icon_size,bRTL))
				return false;
			if (Programs_icon.GetBitmap())
			{
				SIZE selSize;
				if (!LoadSkinBitmap(hMod,parser,L"Programs_icon_selected",Programs_icon_selected,&selSize,bRTL))
					return false;
				if (!Programs_icon_selected.GetBitmap() || selSize.cx!=Programs_icon_size.cx || selSize.cy!=Programs_icon_size.cy)
				{
					Programs_icon_selected.Reset();
					Programs_icon_selected=Programs_icon;
					Programs_icon_selected.bIsOwned=false;
				}
			}
			Programs_icon_size.cy/=2;

			if (parser.FindSetting(L"Scrollbar_button") && parser.FindSetting(L"Scrollbar_thumb") && parser.FindSetting(L"Scrollbar_background") && parser.FindSetting(L"Scrollbar_arrows"))
			{
				BHasScrollbar=true;
				if (!LoadSkinBackground(hMod,parser,L"Scrollbar_button",Scrollbar_button,Programs_background,Scrollbar_button_slices_X,_countof(Scrollbar_button_slices_X),Scrollbar_button_slices_Y,_countof(Scrollbar_button_slices_Y),bRTL,true))
					return false;
				if (!LoadSkinBackground(hMod,parser,L"Scrollbar_thumb",Scrollbar_thumb,Programs_background,Scrollbar_thumb_slices_X,_countof(Scrollbar_thumb_slices_X),Scrollbar_thumb_slices_Y,_countof(Scrollbar_thumb_slices_Y),bRTL,true))
					return false;
				if (!LoadSkinBackground(hMod,parser,L"Scrollbar_background",Scrollbar_background,Programs_background,Scrollbar_background_slices_X,_countof(Scrollbar_background_slices_X),Scrollbar_background_slices_Y,_countof(Scrollbar_background_slices_Y),bRTL,true))
					return false;
				if (!LoadSkinBitmap(hMod,parser,L"Scrollbar_arrows",Scrollbar_arrows,&Scrollbar_arrows_size,bRTL))
					return false;
				Scrollbar_arrows_size.cx/=2;
				Scrollbar_arrows_size.cy/=3;

				if (parser.FindSetting(L"Scrollbar_gripper"))
				{
					if (!LoadSkinBitmap(hMod,parser,L"Scrollbar_gripper",Scrollbar_gripper,&Scrollbar_gripper_size,bRTL))
						return false;
					Scrollbar_gripper_size.cy/=3;
				}
				else
					Scrollbar_gripper_size.cx=Scrollbar_gripper_size.cy=0;
			}
		}

		// SEARCH SECTION
		if (!LoadSkinBitmap(hMod,parser,L"Search_bitmap",Search_bitmap,NULL,false))
			return false;

		if (!LoadSkinBitmap(hMod,parser,L"Search_arrow",Search_arrow,&Search_arrow_size,bRTL))
			return false;
		Search_arrow_size.cy/=2;

		COLORREF searchColor=GetSysColor(COLOR_WINDOW);
		str=parser.FindSetting(L"Search_padding");
		if (str)
			LoadSkinNumbers(str,(int*)&Search_padding,4,NUMBERS_PADDING);
		else
			Search_padding=ScaleSkinElement(DEFAULT_SEARCH_PADDING);
		str=parser.FindSetting(L"Search_frame");
		Search_frame=!str || _wtol(str)!=0;
		str=parser.FindSetting(L"Search_background");
		if (str)
		{
			if (!LoadSkinBackground(hMod,parser,L"Search_background",Search_background,searchColor,Search_background_slices_X,_countof(Search_background_slices_X),Search_background_slices_Y,_countof(Search_background_slices_Y),false,true))
				return false;
		}
		str=parser.FindSetting(L"Search_background_padding");
		if (str)
			LoadSkinNumbers(str,(int*)&Search_background_padding,4,NUMBERS_PADDING);
		else
			memset(&Search_background_padding,0,sizeof(Search_background_padding));

		if (!LoadSkinBackground(hMod,parser,L"Search_emblem_mask",Search_emblem_mask,0,NULL,0,NULL,0,false,true,false))
			return false;
		if (!LoadSkinBackground(hMod,parser,L"Search_pattern_mask",Search_pattern_mask,0,NULL,0,NULL,0,false,true,false))
			return false;

		str=parser.FindSetting(L"Search_background_search");
		if (str)
		{
			if (!LoadSkinBackground(hMod,parser,L"Search_background_search",Search_background_search,searchColor,Search_background_search_slices_X,_countof(Search_background_search_slices_X),Search_background_search_slices_Y,_countof(Search_background_search_slices_Y),false,true))
				return false;
		}
		else
		{
			Search_background_search=Search_background;
			Search_background_search.bIsOwned=false;
			memcpy(Search_background_search_slices_X,Search_background_slices_X,sizeof(Search_background_search_slices_X));
			memcpy(Search_background_search_slices_Y,Search_background_slices_Y,sizeof(Search_background_search_slices_Y));
		}
		str=parser.FindSetting(L"Search_background_search_padding");
		if (str)
			LoadSkinNumbers(str,(int*)&Search_background_search_padding,4,NUMBERS_PADDING);
		else
			memcpy(&Search_background_search_padding,&Search_background_padding,sizeof(Search_background_search_padding));

		str=parser.FindSetting(L"Search_emblem_search_mask");
		if (str)
		{
			if (!LoadSkinBackground(hMod,parser,L"Search_emblem_search_mask",Search_emblem_search_mask,0,NULL,0,NULL,0,false,true,false))
				return false;
		}
		else
		{
			Search_emblem_search_mask=Search_emblem_mask;
			Search_emblem_search_mask.bIsOwned=false;
		}

		str=parser.FindSetting(L"Search_pattern_search_mask");
		if (str)
		{
			if (!LoadSkinBackground(hMod,parser,L"Search_pattern_search_mask",Search_pattern_search_mask,0,NULL,0,NULL,0,false,true,false))
				return false;
		}
		else
		{
			Search_pattern_search_mask=Search_pattern_mask;
			Search_pattern_search_mask.bIsOwned=false;
		}

		str=parser.FindSetting(L"Search_background_jump");
		if (str)
		{
			if (!LoadSkinBackground(hMod,parser,L"Search_background_jump",Search_background_jump,searchColor,Search_background_jump_slices_X,_countof(Search_background_jump_slices_X),Search_background_jump_slices_Y,_countof(Search_background_jump_slices_Y),false,true))
				return false;
		}
		else
		{
			Search_background_jump=Search_background;
			Search_background_jump.bIsOwned=false;
			memcpy(Search_background_jump_slices_X,Search_background_slices_X,sizeof(Search_background_jump_slices_X));
			memcpy(Search_background_jump_slices_Y,Search_background_slices_Y,sizeof(Search_background_jump_slices_Y));
		}
		str=parser.FindSetting(L"Search_background_jump_padding");
		if (str)
			LoadSkinNumbers(str,(int*)&Search_background_jump_padding,4,NUMBERS_PADDING);
		else
			memcpy(&Search_background_jump_padding,&Search_background_padding,sizeof(Search_background_jump_padding));

		str=parser.FindSetting(L"Search_emblem_jump_mask");
		if (str)
		{
			if (!LoadSkinBackground(hMod,parser,L"Search_emblem_jump_mask",Search_emblem_jump_mask,0,NULL,0,NULL,0,false,true,false))
				return false;
		}
		else
		{
			Search_emblem_jump_mask=Search_emblem_mask;
			Search_emblem_jump_mask.bIsOwned=false;
		}

		str=parser.FindSetting(L"Search_pattern_jump_mask");
		if (str)
		{
			if (!LoadSkinBackground(hMod,parser,L"Search_pattern_jump_mask",Search_pattern_jump_mask,0,NULL,0,NULL,0,false,true,false))
				return false;
		}
		else
		{
			Search_pattern_jump_mask=Search_pattern_mask;
			Search_pattern_jump_mask.bIsOwned=false;
		}

		HMODULE hImageRes=NULL;
		str=parser.FindSetting(L"Pin_bitmap");
		if (str)
		{
			if (!LoadSkinBitmap(hMod,parser,L"Pin_bitmap",Pin_bitmap,&Pin_bitmap_Size,bRTL))
				return false;
			Pin_bitmap_Size.cx/=2;
			Pin_bitmap_Size.cy/=2;
		}
		else
		{
			int iconSize=CItemManager::SMALL_ICON_SIZE;
			Pin_bitmap_Size.cx=Pin_bitmap_Size.cy=iconSize;
			BITMAPINFO bi={0};
			bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
			bi.bmiHeader.biWidth=bi.bmiHeader.biHeight=iconSize*2;
			bi.bmiHeader.biPlanes=1;
			bi.bmiHeader.biBitCount=32;
			RECT rc={0,0,iconSize*2,iconSize*2};

			HDC hdc=CreateCompatibleDC(NULL);
			unsigned int *bits;
			HBITMAP bmp=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,(void**)&bits,NULL,0);
			HGDIOBJ bmp0=SelectObject(hdc,bmp);
			FillRect(hdc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));

			HMODULE hMod=GetModuleHandle(L"imageres.dll");
			if (!hMod)
			{
				hMod=LoadLibraryEx(L"imageres.dll",NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
				hImageRes=hMod;
			}
			if (hMod)
			{
				HICON hIcon=(HICON)LoadImage(hMod,MAKEINTRESOURCE(5101),IMAGE_ICON,iconSize,iconSize,LR_DEFAULTCOLOR);
				DrawIconEx(hdc,0,0,hIcon,iconSize,iconSize,0,NULL,DI_NORMAL);
				DrawIconEx(hdc,0,iconSize,hIcon,iconSize,iconSize,0,NULL,DI_NORMAL);
				DestroyIcon(hIcon);
				hIcon=(HICON)LoadImage(hMod,MAKEINTRESOURCE(5100),IMAGE_ICON,iconSize,iconSize,LR_DEFAULTCOLOR);
				DrawIconEx(hdc,iconSize,0,hIcon,iconSize,iconSize,0,NULL,DI_NORMAL);
				DrawIconEx(hdc,iconSize,iconSize,hIcon,iconSize,iconSize,0,NULL,DI_NORMAL);
				DestroyIcon(hIcon);
			}
			SelectObject(hdc,bmp0);
			DeleteDC(hdc);
			Pin_bitmap=bmp;
			Pin_bitmap.bIsOwned=true;
			Pin_bitmap.bIs32=true;
		}

		str=parser.FindSetting(L"More_bitmap");
		if (str)
		{
			if (wcscmp(str,L"none")==0)
				More_bitmap_Size.cx=More_bitmap_Size.cy=0;
			else if (!LoadSkinBitmap(hMod,parser,L"More_bitmap",More_bitmap,&More_bitmap_Size,bRTL))
				return false;
			else
				More_bitmap_Size.cy/=2;
		}
		else
		{
			int iconSize=CItemManager::SMALL_ICON_SIZE;
			More_bitmap_Size.cx=More_bitmap_Size.cy=iconSize;
			BITMAPINFO bi={0};
			bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
			bi.bmiHeader.biWidth=iconSize;
			bi.bmiHeader.biHeight=iconSize*2;
			bi.bmiHeader.biPlanes=1;
			bi.bmiHeader.biBitCount=32;
			RECT rc={0,0,iconSize,iconSize*2};

			HDC hdc=CreateCompatibleDC(NULL);
			unsigned int *bits;
			HBITMAP bmp=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,(void**)&bits,NULL,0);
			HGDIOBJ bmp0=SelectObject(hdc,bmp);
			FillRect(hdc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));

			HMODULE hMod=GetModuleHandle(L"shell32.dll");
			if (hMod)
			{
				HICON hIcon=(HICON)LoadImage(hMod,MAKEINTRESOURCE(323),IMAGE_ICON,iconSize,iconSize,LR_DEFAULTCOLOR);
				DrawIconEx(hdc,0,0,hIcon,iconSize,iconSize,0,NULL,DI_NORMAL);
				DrawIconEx(hdc,0,iconSize,hIcon,iconSize,iconSize,0,NULL,DI_NORMAL);
				DestroyIcon(hIcon);
			}
			SelectObject(hdc,bmp0);
			DeleteDC(hdc);
			More_bitmap=bmp;
			More_bitmap.bIsOwned=true;
			More_bitmap.bIs32=true;
		}

		if (skinType==SKIN_TYPE_WIN7)
		{
			str=parser.FindSetting(L"Shutdown_bitmap");
			if (str)
			{
				if (!LoadSkinBitmap(hMod,parser,L"Shutdown_bitmap",Shutdown_bitmap,&Shutdown_bitmap_Size,bRTL))
					return false;
			}
			else
			{
				HMODULE hMod=GetModuleHandle(L"shell32.dll");
/*				if (!hMod)
				{
					if (hImageRes)
						hMod=hImageRes;
					else
					{
						hMod=LoadLibraryEx(L"imageres.dll",NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
						hImageRes=hMod;
					}
				}*/
				if (hMod)
				{
					int iconSize=Dpi>=144?24:16;
					HICON hIcon=(HICON)LoadImage(hMod,MAKEINTRESOURCE(47),IMAGE_ICON,iconSize,iconSize,LR_DEFAULTCOLOR);
					if (hIcon)
					{
						Shutdown_bitmap=BitmapFromIcon(hIcon,iconSize,NULL,TRUE);
						Shutdown_bitmap.bIsOwned=true;
						Shutdown_bitmap.bIs32=true;
						Shutdown_bitmap_Size.cx=iconSize;
						Shutdown_bitmap_Size.cy=iconSize;
					}
				}
			}
		}
		if (hImageRes)
			FreeLibrary(hImageRes);

	}

	// SUB-MENU SECTION - describes the menu portion of the sub-menu
	if (!LoadSkinColors(parser,L"Submenu_background",&Submenu_background,1,GetSysColor(COLOR_MENU)))
		Submenu_background=GetSysColor(COLOR_MENU);

	if (flags&LOADMENU_RESOURCES)
	{
		memset(Submenu_bitmap_slices_X,0,sizeof(Submenu_bitmap_slices_X));
		// the submenu bitmap is mirrored here, unlike the main bitmap, which is mirrored in CreateBackground
		if (!LoadSkinBackground(hMod,parser,L"Submenu_bitmap",Submenu_bitmap,Submenu_background,Submenu_bitmap_slices_X+3,6,Submenu_bitmap_slices_Y,_countof(Submenu_bitmap_slices_Y),bRTL))
			return false;

		str=parser.FindSetting(L"Submenu_opacity");
		Submenu_opacity=MenuSkin::OPACITY_SOLID;
		if (str && Submenu_bitmap.GetBitmap())
		{
			if (_wcsicmp(str,L"region")==0) Submenu_opacity=MenuSkin::OPACITY_REGION;
			if (_wcsicmp(str,L"alpha")==0) Submenu_opacity=MenuSkin::OPACITY_ALPHA;
			if (_wcsicmp(str,L"glass")==0) Submenu_opacity=MenuSkin::OPACITY_GLASS;
			if (_wcsicmp(str,L"fullalpha")==0) Submenu_opacity=MenuSkin::OPACITY_FULLALPHA;
			if (_wcsicmp(str,L"fullglass")==0) Submenu_opacity=MenuSkin::OPACITY_FULLGLASS;
		}

		Submenu_shadow=SHADOW_ON;
		str=parser.FindSetting(L"Submenu_shadow");
		if (str)
		{
			if (_wcsicmp(str,L"1")==0) Submenu_shadow=MenuSkin::SHADOW_ON;
			if (_wcsicmp(str,L"0")==0) Submenu_shadow=MenuSkin::SHADOW_OFF;
			if (_wcsicmp(str,L"glass")==0) Submenu_shadow=MenuSkin::SHADOW_GLASS;
		}

		if (!LoadSkinItem(hMod,parser,L"Submenu",ItemSettings[SUBMENU_ITEM],NULL,Submenu_background,bRTL))
			return false;
		if (!LoadSkinItem(hMod,parser,L"Submenu_split",ItemSettings[SUBMENU_SPLIT],&ItemSettings[SUBMENU_ITEM],Submenu_background,bRTL))
			return false;
		if (!LoadSkinItem(hMod,parser,L"Submenu_new",ItemSettings[SUBMENU_NEW],&ItemSettings[SUBMENU_ITEM],Submenu_background,bRTL))
			return false;
		if (!LoadSkinItem(hMod,parser,L"Submenu_separator",ItemSettings[SUBMENU_SEPARATOR],&ItemSettings[SUBMENU_ITEM],Submenu_background,bRTL))
			return false;
		if (!LoadSkinItem(hMod,parser,L"Submenu_separator_split",ItemSettings[SUBMENU_SEPARATOR_SPLIT],&ItemSettings[SUBMENU_SPLIT],Submenu_background,bRTL))
			return false;
		ItemSettings[SUBMENU_SEPARATOR].sepHeight=ScaleSkinElement(DEFAULT_SEPARATOR_HEIGHT);
		str=parser.FindSetting(L"Submenu_separator");
		if (str)
		{
			if (!LoadSkinBackground(hMod,parser,L"Submenu_separator",ItemSettings[SUBMENU_SEPARATOR].bmpSeparator,Main_background,ItemSettings[SUBMENU_SEPARATOR].sepSlicesX,_countof(ItemSettings[SUBMENU_SEPARATOR].sepSlicesX),&ItemSettings[SUBMENU_SEPARATOR].sepHeight,0,bRTL))
				return false;
		}
		ItemSettings[SUBMENU_SEPARATOR_SPLIT].sepHeight=ScaleSkinElement(DEFAULT_SEPARATOR_HEIGHT);
		str=parser.FindSetting(L"Submenu_separator_split");
		if (str)
		{
			if (!LoadSkinBackground(hMod,parser,L"Submenu_separator_split",ItemSettings[SUBMENU_SEPARATOR_SPLIT].bmpSeparator,Main_background,ItemSettings[SUBMENU_SEPARATOR_SPLIT].sepSlicesX,_countof(ItemSettings[SUBMENU_SEPARATOR_SPLIT].sepSlicesX),&ItemSettings[SUBMENU_SEPARATOR_SPLIT].sepHeight,0,bRTL))
				return false;
		}
		else
		{
			ItemSettings[SUBMENU_SEPARATOR_SPLIT].bmpSeparator=ItemSettings[SUBMENU_SEPARATOR].bmpSeparator;
			ItemSettings[SUBMENU_SEPARATOR_SPLIT].bmpSeparator.bIsOwned=false;
			ItemSettings[SUBMENU_SEPARATOR_SPLIT].sepHeight=ItemSettings[SUBMENU_SEPARATOR].sepHeight;
			memcpy(ItemSettings[SUBMENU_SEPARATOR_SPLIT].sepSlicesX,ItemSettings[SUBMENU_SEPARATOR].sepSlicesX,sizeof(ItemSettings[SUBMENU_SEPARATOR].sepSlicesX));
		}

		str=parser.FindSetting(L"Submenu_padding");
		if (str)
			LoadSkinNumbers(str,(int*)&Submenu_padding0,4,NUMBERS_PADDING);
		else
			memset(&Submenu_padding0,0,sizeof(Submenu_padding0));
		str=parser.FindSetting(L"Submenu_offset");
		if (str)
			LoadSkinNumbers(str,(int*)&Submenu_offset,1,NUMBERS_SIZE);
		else
			Submenu_offset=0;

		str=parser.FindSetting(L"AllPrograms_offset");
		if (str)
			LoadSkinNumbers(str,(int*)&AllPrograms_offset,1,NUMBERS_SIZE);
		else
			AllPrograms_offset=0;

		str=parser.FindSetting(L"Submenu_thin_frame");
		Submenu_thin_frame=(str && _wtol(str));

		str=parser.FindSetting(L"Submenu_separatorV");
		if (str)
		{
			if (!LoadSkinBackground(hMod,parser,L"Submenu_separatorV",Submenu_separatorV,Submenu_background,&Submenu_separatorWidth,0,Submenu_separator_slices_Y,_countof(Submenu_separator_slices_Y),bRTL))
				return false;
		}
		if (!Submenu_separatorV.GetBitmap())
			Submenu_separatorWidth=ScaleSkinElement(DEFAULT_SEPARATOR_WIDTH);
		if (!LoadSkinBackground(hMod,parser,L"Submenu_pager",Submenu_pager,Submenu_background,Submenu_pager_slices_X,_countof(Submenu_pager_slices_X),Submenu_pager_slices_Y,_countof(Submenu_pager_slices_Y),bRTL))
			return false;

		if (!LoadSkinBitmap(hMod,parser,L"Submenu_pager_arrows",Submenu_pager_arrows,&Submenu_pager_arrow_Size,bRTL))
			return false;
		Submenu_pager_arrow_Size.cx/=2;
		Submenu_pager_arrow_Size.cy/=2;
	}

	Main_FakeGlass=false;
	Submenu_FakeGlass=false;
	WORD winVer=GetWinVersion();
	if ((winVer>=WIN_VER_WIN8 && winVer<WIN_VER_WIN10 && !GetSettingBool(L"AeroGlass")) || (winVer>=WIN_VER_WIN10 && !GetSettingBool(L"EnableGlass")))
	{
		// replace GLASS with ALPHA and enable the fake glass (alpha with less opacity)
		if (Main_opacity==OPACITY_GLASS)
		{
			Main_opacity=OPACITY_ALPHA;
			Main_FakeGlass=true;
		}
		if (Main_opacity==OPACITY_FULLGLASS)
		{
			Main_opacity=OPACITY_FULLALPHA;
			Main_FakeGlass=true;
		}
		if (Main2_opacity==OPACITY_GLASS)
			Main2_opacity=OPACITY_ALPHA;
		if (Main2_opacity==OPACITY_FULLGLASS)
			Main2_opacity=OPACITY_FULLALPHA;
		if (Submenu_opacity==OPACITY_GLASS)
		{
			Submenu_opacity=OPACITY_ALPHA;
			Submenu_FakeGlass=true;
		}
		if (Submenu_opacity==OPACITY_FULLGLASS)
		{
			Submenu_opacity=OPACITY_FULLALPHA;
			Submenu_FakeGlass=true;
		}
	}

	if (Main_shadow==SHADOW_GLASS && Main_opacity!=OPACITY_GLASS && Main_opacity!=OPACITY_FULLGLASS)
		Main_shadow=SHADOW_OFF;
	if (Submenu_shadow==SHADOW_GLASS && Submenu_opacity!=OPACITY_GLASS && Submenu_opacity!=OPACITY_FULLGLASS)
		Submenu_shadow=SHADOW_OFF;

	ItemSettings[COLUMN2_INLINE]=ItemSettings[COLUMN2_ITEM];
	ItemSettings[COLUMN2_INLINE].bOwnFont=false;
	ItemSettings[COLUMN2_INLINE].bmpSelection.bIsOwned=false;
	ItemSettings[COLUMN2_INLINE].bmpArrow.bIsOwned=false;
	ItemSettings[COLUMN2_INLINE].bmpIconFrame.bIsOwned=false;
	ItemSettings[COLUMN2_INLINE].bmpSeparator.bIsOwned=false;

	HDC hdc=CreateCompatibleDC(NULL);
	HGDIOBJ font0=GetCurrentObject(hdc,OBJ_FONT);
	for (int i=0;i<_countof(ItemSettings);i++)
	{
		ItemDrawSettings &settings=ItemSettings[i];

		switch (i)
		{
			case COLUMN1_ITEM:
			case COLUMN1_SPLIT:
			case COLUMN1_NEW:
			case COLUMN1_SEPARATOR:
				settings.iconSize=Main_icon_size;
				settings.opacity=Main_opacity;
				break;
			case COLUMN2_ITEM:
			case COLUMN2_SPLIT:
			case COLUMN2_NEW:
			case COLUMN2_SEPARATOR:
				settings.iconSize=Main2_icon_size;
				settings.opacity=Main2_opacity;
				break;
			case COLUMN2_INLINE:
				settings.iconSize=Main2_icon_size!=ICON_SIZE_NONE?Main2_icon_size:Main_icon_size;
				settings.opacity=Main2_opacity;
				break;
			case SUBMENU_ITEM:
			case SUBMENU_SPLIT:
			case SUBMENU_NEW:
			case SUBMENU_SEPARATOR:
			case SUBMENU_SEPARATOR_SPLIT:
				settings.iconSize=ICON_SIZE_SMALL;
				settings.opacity=Submenu_opacity;
				break;

			// special items
			case PROGRAMS_TREE_ITEM:
			case PROGRAMS_TREE_NEW:
				settings.iconSize=ICON_SIZE_SMALL;
				settings.opacity=(((Main_opacity==OPACITY_FULLALPHA || Main_opacity==OPACITY_FULLGLASS) && Programs_background<0xFF000000) || Programs_background==0)?Main_opacity:OPACITY_SOLID;
				break;
			case LIST_ITEM:
			case LIST_SPLIT:
			case LIST_SEPARATOR:
			case LIST_SEPARATOR_SPLIT:
				settings.iconSize=ICON_SIZE_SMALL;
				settings.opacity=(Search_opacity==OPACITY_FULLALPHA || Search_opacity==OPACITY_FULLGLASS)?Search_opacity:OPACITY_SOLID;
				break;

			case PROGRAMS_BUTTON:
			case PROGRAMS_BUTTON_NEW:
			case PROGRAMS_CASCADING:
			case PROGRAMS_CASCADING_NEW:
				settings.iconSize=ICON_SIZE_PROGRAMS;
				settings.opacity=(Main_opacity==OPACITY_FULLALPHA || Main_opacity==OPACITY_FULLGLASS)?Main_opacity:OPACITY_SOLID;
				if (Programs_icon.GetBitmap())
				{
					int dx=(Main_icon_size==ICON_SIZE_LARGE?g_ItemManager.LARGE_ICON_SIZE:g_ItemManager.SMALL_ICON_SIZE)-Programs_icon_size.cx;
					if (dx>0)
						settings.iconPadding.right+=dx;
				}
				break;

			case SHUTDOWN_BUTTON:
				settings.iconSize=ICON_SIZE_NONE;
				settings.opacity=Main2_opacity;
				break;
			case SHUTDOWN_BUTTON_SEARCH:
				settings.iconSize=ICON_SIZE_NONE;
				settings.opacity=Search_opacity;
				break;
			case SHUTDOWN_BUTTON_JUMP:
				settings.iconSize=ICON_SIZE_NONE;
				settings.opacity=Jumplist_opacity;
				break;
		}

		if (settings.font)
		{
			SelectObject(hdc,settings.font);
			GetTextMetrics(hdc,&settings.textMetrics);
		}
		else
		{
			memset(&settings.textMetrics,0,sizeof(settings.textMetrics));
		}

		int textHeight=settings.font?settings.textMetrics.tmHeight+settings.textPadding.top+settings.textPadding.bottom:0;
		if (i==COLUMN2_INLINE)
			textHeight=0;
		int iconHeight=0;
		if (i==LIST_SEPARATOR_SPLIT || i==SUBMENU_SEPARATOR_SPLIT)
			iconHeight=More_bitmap_Size.cy;
		else if (i==SHUTDOWN_BUTTON || i==SHUTDOWN_BUTTON_SEARCH || i==SHUTDOWN_BUTTON_JUMP)
			iconHeight=Shutdown_bitmap_Size.cy;
		else if (settings.iconSize==ICON_SIZE_SMALL)
			iconHeight=g_ItemManager.SMALL_ICON_SIZE;
		else if (settings.iconSize==ICON_SIZE_LARGE)
			iconHeight=g_ItemManager.LARGE_ICON_SIZE;
		else if (settings.iconSize==ICON_SIZE_PROGRAMS)
			iconHeight=Programs_icon_size.cy?Programs_icon_size.cy:10;
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
	SelectObject(hdc,font0);
	DeleteDC(hdc);

	{
		LOGFONT lfont;
		GetObject(ItemSettings[(skinType==SKIN_TYPE_WIN7)?LIST_SEPARATOR:SUBMENU_SEPARATOR].font,sizeof(lfont),&lfont);
		lfont.lfUnderline=TRUE;
		Search_underline_font=CreateFontIndirect(&lfont);
	}

#ifndef SIMULATE_METRO_COLORS
	if (!*s_SkinError && BHasMetroColors && GetWinVersion()==WIN_VER_WIN7)
	{
		Strcpy(s_SkinError,_countof(s_SkinError),LoadStringEx(IDS_SKIN_ERR_METRO_COLOR));
		return false;
	}
#endif
	return true;
}

bool MenuSkin::LoadMenuSkin( const wchar_t *fname, const wchar_t *variation, const wchar_t *optionsStr, TSkinType skinType, unsigned int flags, int dpi )
{
	s_SkinError[0]=0;
	wchar_t path[_MAX_PATH];
	GetSkinsPath(path);

	if (!*fname || _wcsicmp(fname,LoadStringEx(IDS_DEFAULT_SKIN))==0)
	{
		Reset();
		LoadDefaultMenuSkin(skinType,flags,dpi);
		Hash=0;
		return true;
	}

	BOOL bTheme=IsAppThemed();
	BOOL bComp;
	if (!bTheme || FAILED(DwmIsCompositionEnabled(&bComp)))
		bComp=FALSE;
	BOOL bEnableTouch=(GetWinVersion()>=WIN_VER_WIN8 && GetSettingBool(L"EnableTouch") && (GetSystemMetrics(SM_DIGITIZER)&NID_INTEGRATED_TOUCH)!=0);
	bool bAeroGlass=GetSettingBool(L"AeroGlass");
	bool bEnableGlass=GetSettingBool(L"EnableGlass");
	bool bSearchBox=GetSettingInt(L"SearchBox")!=SEARCHBOX_HIDDEN;
	int settings[]={
		(bAeroGlass?1:0)|(bEnableGlass?2:0)|(bSearchBox?4:0),
		GetSettingInt(L"FontSmoothing"),
		bTheme,
		bComp,
		bEnableTouch
	};

	unsigned int skinHash=CalcFNVHash(settings,sizeof(settings));
	if (variation)
		skinHash=CalcFNVHash(variation,skinHash);

	int colors[4]={0};
	GetMenuGlassColor(colors[0],colors[1],colors[2],colors[3]);

	skinHash=CalcFNVHash(&colors,sizeof(colors),skinHash);
	skinHash=CalcFNVHash(optionsStr,skinHash);

	unsigned int metroColorHash;
	if (GetImmersiveUserColorSetPreference!=NULL)
	{
		g_CurrentMetroColorSet=GetImmersiveUserColorSetPreference(false,false);
		metroColorHash=CalcMetroColorHash(g_CurrentMetroColorSet);
	}
	else
		metroColorHash=CalcMetroColorHash(0);

	if (_wcsicmp(fname,L"Custom")==0)
	{
		Reset();
		if (!LoadSkin(NULL,variation,optionsStr,skinType,flags,dpi))
		{
			Reset();
			return false;
		}
	}
	else
	{
		Strcat(path,_countof(path),fname);
		Strcat(path,_countof(path),(skinType==SKIN_TYPE_WIN7)?L".skin7":L".skin");
		skinHash=CalcFNVHash(path,skinHash);
		WIN32_FILE_ATTRIBUTE_DATA attributes={0};
		if (GetFileAttributesEx(path,GetFileExInfoStandard,&attributes))
			skinHash=CalcFNVHash(&attributes.ftLastWriteTime,sizeof(FILETIME),skinHash);
		if (Hash==skinHash && MetroColorHash==metroColorHash && dpi==Dpi)
		{
			bool bDifferent=false;
			for (std::vector<CustomBitmap>::const_iterator it=CustomBitmaps.begin();it!=CustomBitmaps.end();++it)
			{
				if (!GetFileAttributesEx(it->path,GetFileExInfoStandard,&attributes) || CompareFileTime(&attributes.ftLastWriteTime,&it->timestamp)!=0)
				{
					bDifferent=true;
					break;
				}
			}
			if (!bDifferent)
				return true;
		}
		Reset();
		HMODULE hMod=LoadLibraryEx(path,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
		if (!hMod)
		{
			wchar_t err[1024];
			GetErrorMessage(err,_countof(err),GetLastError());
			Sprintf(s_SkinError,_countof(s_SkinError),LoadStringEx(IDS_SKIN_ERR_LOAD),path,err);
			return false;
		}

		if (!LoadSkin(hMod,variation,optionsStr,skinType,flags,dpi))
		{
			Reset();
			FreeLibrary(hMod);
			return false;
		}

		FreeLibrary(hMod);
	}
	Hash=skinHash;

	MetroColorHash=CalcMetroColorHash();

	if (!bComp)
	{
		if (Main_opacity!=OPACITY_SOLID)
			Main_opacity=OPACITY_REGION;

		if (Main2_opacity!=OPACITY_SOLID)
			Main2_opacity=OPACITY_REGION;

		if (Submenu_opacity!=OPACITY_SOLID)
			Submenu_opacity=OPACITY_REGION;

		for (int i=0;i<_countof(ItemSettings);i++)
			if (ItemSettings[i].opacity!=OPACITY_SOLID)
				ItemSettings[i].opacity=OPACITY_REGION;
	}

	return true;
}

static POINT g_ArrowSizes96[]=
{
	{0,3}, // insert left
	{3,6}, // middle size
	{6,9}, // insert right
	{10,14}, // right arrow
	{14,18}, // left arrow
	{18,25}, // down arrow
	{25,7}, // total size
};

static POINT g_ArrowSizes144[]=
{
	{0,4}, // insert left
	{3,8}, // middle size
	{7,11}, // insert right
	{12,18}, // right arrow
	{18,24}, // left arrow
	{24,35}, // down arrow
	{35,11}, // total size
};

const POINT *MenuSkin::GetArrowsBitmapSizes( void ) const
{
	return Dpi>=144?g_ArrowSizes144:g_ArrowSizes96;
}

void MenuSkin::PretilePatterns( int width )
{
	if (PatternPretileWidth>=width) return;
	PatternPretileWidth=width;
	for (int i=0;i<4;i++)
	{
		if (Patterns[i].GetBitmap())
		{
			BITMAP info;
			GetObject(Patterns[i].GetBitmap(),sizeof(info),&info);
			const unsigned char *src=(unsigned char*)info.bmBits;
			int width2=((width+info.bmWidth-1)/info.bmWidth)*info.bmWidth;

			// convert the pattern to 32-bit, pretile horizontally, flip vertically
			PatternBits[i].resize(info.bmHeight*width2);
			unsigned int *dst=&PatternBits[i][(info.bmHeight-1)*width2];
			for (int y=0;y<info.bmHeight;y++,src+=info.bmWidthBytes,dst-=width2)
			{
				if (info.bmBitsPixel==24)
				{
					for (int x=0;x<info.bmWidth;x++)
					{
						const unsigned char *pixel=src+x*3;
						dst[x]=pixel[0]|(pixel[1]<<8)|(pixel[2]<<16)|0xFF000000;
					}
				}
				else if (info.bmBitsPixel==32)
					memcpy(dst,src,info.bmWidth*4);
				for (int x=info.bmWidth;x<width2;x+=info.bmWidth)
					memcpy(dst+x,dst,info.bmWidth*4);
			}
		}
	}
}

void MenuSkin::LoadDefaultMenuSkin( TSkinType skinType, unsigned int flags, int dpi )
{
	LoadSkin(g_Instance,NULL,L"",skinType,flags,dpi);
}

void GetSkinsPath( wchar_t *path )
{
	GetModuleFileName(g_Instance,path,_MAX_PATH);
	*PathFindFileName(path)=0;
	Strcat(path,_MAX_PATH,L"Skins\\");

#ifndef BUILD_SETUP
	if (!PathIsDirectory(path))
	{
		*PathFindFileName(path) = 0;
		Strcat(path,_MAX_PATH,L"..\\Skins\\");
	}
#endif
}

static IatHookData *g_DrawThemeBackgroundHook;

DWORD g_CustomScrollbarThread;
HTHEME g_CustomScrollbarTheme;

static HRESULT STDAPICALLTYPE DrawThemeBackgroundSkin( HTHEME hTheme, HDC hdc, int iPartId, int iStateId, LPCRECT pRect, LPCRECT pClipRect )
{
	if (g_CustomScrollbarThread && g_CustomScrollbarThread==GetCurrentThreadId() && hTheme==g_CustomScrollbarTheme)
	{
		CProgramsTree::DrawScrollbarBackground(hdc,iPartId,iStateId,pRect);
		return S_OK;
	}
	return DrawThemeBackground(hTheme,hdc,iPartId,iStateId,pRect,pClipRect);
}

void InitializeSkinManager( bool bInitIat )
{
	if (bInitIat)
		InitializeIatHooks();
	HMODULE hComCtl32=GetModuleHandle(L"comctl32.dll");
	if (hComCtl32)
		g_DrawThemeBackgroundHook=SetIatHook(hComCtl32,"uxtheme.dll","DrawThemeBackground",DrawThemeBackgroundSkin);
#ifdef SIMULATE_METRO_COLORS
	char path[_MAX_PATH];
	GetModuleFileNameA(g_Instance,path,_countof(path));
	*PathFindFileNameA(path)=0;
	Strcat(path,_countof(path),"..\\MetroColors\\" SIMULATE_METRO_COLORS ".txt");
	FILE *f;
	fopen_s(&f,path,"rt");
	if (f)
	{
		for (int i=0;;i++)
		{
			char buf[200]={0};
			fgets(buf,_countof(buf),f);
			if (Strlen(buf)<10) break;
			char *end;
			CString name(buf+9);
			name.Trim(L"\r\n");
			unsigned int crc=CalcFNVHash(name);
			SimMetroColor color;
			color.nameHash=crc;
			color.color=strtoul(buf,&end,16);
			color.color=((color.color&0xFF)<<16)|(color.color&0xFF00FF00)|((color.color>>16)&0xFF);
			g_MetroColors.push_back(color);
		}
		fclose(f);
	}
#else
	HMODULE hUxTheme=LoadLibrary(L"uxtheme.dll");
	DWORD ver=GetVersionEx(hUxTheme);
	if (hUxTheme && ver>=0x6020000)
	{
		GetImmersiveUserColorSetPreference=(TGetImmersiveUserColorSetPreference)GetProcAddress(hUxTheme,MAKEINTRESOURCEA(98));
		GetImmersiveColorFromColorSetEx=(TGetImmersiveColorFromColorSetEx)GetProcAddress(hUxTheme,MAKEINTRESOURCEA(95));
		GetImmersiveColorTypeFromName=(TGetImmersiveColorTypeFromName)GetProcAddress(hUxTheme,MAKEINTRESOURCEA(96));

		if (!GetImmersiveColorFromColorSetEx || !GetImmersiveColorTypeFromName)
			GetImmersiveUserColorSetPreference=NULL;
	}
#endif
}

void CloseSkinManager( bool bCloseIat )
{
	CMenuContainer::s_Skin.Reset();
	ClearIatHook(g_DrawThemeBackgroundHook);
	g_DrawThemeBackgroundHook=NULL;
	if (bCloseIat)
		ClearIatHooks();
}
