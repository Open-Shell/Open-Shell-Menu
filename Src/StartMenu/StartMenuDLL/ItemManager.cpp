// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include "ItemManager.h"
#include "MetroLinkManager.h"
#include "FNVHash.h"
#include "Settings.h"
#include "SettingsUI.h"
#include "Translations.h"
#include "ResourceHelper.h"
#include "MenuContainer.h"
#include "LogManager.h"
#include "StartMenuDLL.h"
#include "resource.h"
#include <propkey.h>
#include <CommonControls.h>
#include <wincodec.h>
#include <algorithm>

//#define DISABLE_CACHE
//#define FORCE_ALL_NEW

#ifdef BUILD_SETUP
#undef DISABLE_CACHE
#undef FORCE_ALL_NEW
#endif

#ifdef _DEBUG
_declspec(thread) int CItemManager::RWLock::g_LockState[CItemManager::RWLOCK_COUNT]; // 0 - none, 1 - read, 2 - write
#endif

static bool g_bInvertMetroIcons;

const int MAX_FOLDER_LEVELS=10; // don't go more than 10 levels deep
const int REFRESH_DELAY=5000;
const int CACHE_FILE_VERSION=2;

PROPERTYKEY PKEY_MetroIcon={{0x86D40B4D, 0x9069, 0x443C, {0x81, 0x9A, 0x2A, 0x54, 0x09, 0x0D, 0xCC, 0xEC}}, 2};

// app ID resolver interface as described here: http://www.binrand.com/post/1510934-out-using-system-using-system-collections-generic-using-system.html
interface IApplicationResolver: public IUnknown
{
	STDMETHOD(GetAppIDForShortcut)( IShellItem *psi, LPWSTR *ppszAppID );
	// .... we don't care about the rest of the methods ....
};

GUID CLSID_ApplicationResolver={0x660b90c8,0x73a9,0x4b58,{0x8c,0xae,0x35,0x5b,0x7f,0x55,0x34,0x1b}};
// different IIDs for Win8 and Win8: http://a-whiter.livejournal.com/1266.html
GUID IID_IApplicationResolver7={0x46a6eeff,0x908e,0x4dc6,{0x92,0xa6,0x64,0xbe,0x91,0x77,0xb4,0x1c}};
GUID IID_IApplicationResolver8={0xde25675a,0x72de,0x44b4,{0x93,0x73,0x05,0x17,0x04,0x50,0xc1,0x40}};

///////////////////////////////////////////////////////////////////////////////

interface IResourceContext;

MIDL_INTERFACE("6e21e72b-b9b0-42ae-a686-983cf784edcd")
IResourceMap : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetUri(const wchar_t **pUri ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetSubtree(const wchar_t *propName, IResourceMap **pSubTree ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetString( const wchar_t *propName, wchar_t *pString ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetStringForContext( IResourceContext *pContext, const wchar_t *propName, wchar_t *pString ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetFilePath( const wchar_t *propName, wchar_t **pPath ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetFilePathForContext( IResourceContext *pContext, const wchar_t *propName, wchar_t **pPath ) = 0;
};

///////////////////////////////////////////////////////////////////////////////

enum RESOURCE_SCALE
{
	RES_SCALE_100=0,
	RES_SCALE_140=1,
	RES_SCALE_180=2,
	RES_SCALE_80 =3,
};

MIDL_INTERFACE("e3c22b30-8502-4b2f-9133-559674587e51")
IResourceContext : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetLanguage( void ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetHomeRegion( wchar_t *pRegion ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetLayoutDirection( enum RESOURCE_LAYOUT_DIRECTION *pDirection ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetTargetSize( WORD *pSize ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetScale( RESOURCE_SCALE *pScale ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetContrast( enum RESOURCE_CONTRAST *pContrast ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetAlternateForm( wchar_t *pForm ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetQualifierValue( const wchar_t *name, wchar_t *pValue ) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetLanguage( const wchar_t *language ) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetHomeRegion( const wchar_t *region ) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetLayoutDirection( enum RESOURCE_LAYOUT_DIRECTION direction ) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetTargetSize( WORD size ) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetScale( RESOURCE_SCALE scale ) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetContrast( void ) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetAlternateForm( const wchar_t *form ) = 0;
};

///////////////////////////////////////////////////////////////////////////////

const CLSID CLSID_ResourceManager={0xdbce7e40, 0x7345, 0x439d, {0xb1, 0x2c, 0x11, 0x4a, 0x11, 0x81, 0x9a, 0x09}};
MIDL_INTERFACE("130a2f65-2be7-4309-9a58-a9052ff2b61c")
IResourceManager : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE Initialize( void ) = 0;
	virtual HRESULT STDMETHODCALLTYPE InitializeForCurrentApplication( void ) = 0;
	virtual HRESULT STDMETHODCALLTYPE InitializeForPackage( const wchar_t *name ) = 0;
	virtual HRESULT STDMETHODCALLTYPE InitializeForFile( const wchar_t *fname ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetMainResourceMap( REFIID riid, void **ppvObject ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetResourceMap( void ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetDefaultContext( REFIID riid, void **ppvObject ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetReference( void ) = 0;
};

///////////////////////////////////////////////////////////////////////////////

extern PROPERTYKEY PKEY_MetroPackagePath;
extern PROPERTYKEY PKEY_MetroPackageName;

static CComPtr<IApplicationResolver> g_pAppResolver;

// Creates the app id resolver object
static void CreateAppResolver( void )
{
	if (GetWinVersion()>=WIN_VER_WIN7)
	{
		CComPtr<IUnknown> pUnknown;
		pUnknown.CoCreateInstance(CLSID_ApplicationResolver);
		if (GetWinVersion()==WIN_VER_WIN7)
			g_pAppResolver=CComQIPtr<IApplicationResolver,&IID_IApplicationResolver7>(pUnknown);
		else
			g_pAppResolver=CComQIPtr<IApplicationResolver,&IID_IApplicationResolver8>(pUnknown);
	}
}

static bool DetectGrayscaleImage( const unsigned int *bits, int stride, int width, int height )
{
	if (width==0 || height==0)
		return false;
	int transparent=0;
	for (int y=0;y<height;y++,bits+=stride)
	{
		for (int x=0;x<width;x++)
		{
			unsigned int pixel=bits[x];
			int a=(pixel>>24)&255;
			int r=(pixel>>16)&255;
			int g=(pixel>>8)&255;
			int b=(pixel)&255;
			if (abs(a-r)>2 || abs(r-g)>2 || abs(r-b)>2 || abs(g-b)>2)
				return false; // found colored pixel
			if (!(pixel&0xFF000000))
				transparent++;
		}
	}
	if ((transparent*100)/(width*height)<5)
		return false; // less than 5% transparent pixels
	return true;
}

static void CreateMonochromeImage( unsigned int *bits, int stride, int width, int height, DWORD metroColor )
{
	int r0=(metroColor)&255;
	int g0=(metroColor>>8)&255;
	int b0=(metroColor>>16)&255;
	for (int y=0;y<height;y++,bits+=stride)
	{
		for (int x=0;x<width;x++)
		{
			unsigned int &pixel=bits[x];
			int a=pixel>>24;
			int r=(r0*a)/255;
			int g=(g0*a)/255;
			int b=(b0*a)/255;
			pixel=(a<<24)|(r<<16)|(g<<8)|b;
		}
	}
}

HBITMAP ColorizeMonochromeImage(HBITMAP bitmap, DWORD color)
{
	{
		BITMAP info{};
		GetObject(bitmap, sizeof(info), &info);
		if (!DetectGrayscaleImage((const unsigned int*)info.bmBits, info.bmWidth, info.bmWidth, info.bmHeight))
			return nullptr;
	}

	HBITMAP bmp = (HBITMAP)CopyImage(bitmap, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
	if (bmp)
	{
		BITMAP info{};
		GetObject(bmp, sizeof(info), &info);

		CreateMonochromeImage((unsigned int*)info.bmBits, info.bmWidth, info.bmWidth, info.bmHeight, color);
	}

	return bmp;
}

static HBITMAP BitmapFromMetroIcon( HICON hIcon, int bitmapSize, int iconSize, DWORD metroColor, bool bDestroyIcon=true )
{
	ICONINFO info;
	BITMAP bmpInfo;
	GetIconInfo(hIcon,&info);
	AddTrackedObject(info.hbmColor);
	AddTrackedObject(info.hbmMask);
	if (info.hbmColor)
	{
		GetObject(info.hbmColor,sizeof(bmpInfo),&bmpInfo);
		iconSize=bmpInfo.bmWidth;
		if (iconSize>bitmapSize)
			iconSize=bitmapSize;
	}
	if (info.hbmColor) DeleteObject(info.hbmColor);
	if (info.hbmMask) DeleteObject(info.hbmMask);

	BITMAPINFO bi={0};
	bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth=bi.bmiHeader.biHeight=bitmapSize;
	bi.bmiHeader.biPlanes=1;
	bi.bmiHeader.biBitCount=32;
	RECT rc={0,0,bitmapSize,bitmapSize};

	HDC hdc=CreateCompatibleDC(NULL);
	unsigned int *bits;
	HBITMAP bmp=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,(void**)&bits,NULL,0);
	HGDIOBJ bmp0=SelectObject(hdc,bmp);
	int offset=(bitmapSize-iconSize)/2;
	bool bInvert=g_bInvertMetroIcons;
	if (g_bInvertMetroIcons)
	{
		FillRect(hdc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
		DrawIconEx(hdc,offset,offset,hIcon,iconSize,iconSize,0,NULL,DI_NORMAL);
		SelectObject(hdc,bmp0);
		unsigned int *bits2=bits+offset*(bitmapSize+1);
		if (DetectGrayscaleImage(bits2,bitmapSize,iconSize,iconSize))
		{
			CreateMonochromeImage(bits2,bitmapSize,iconSize,iconSize,metroColor);
		}
		else
		{
			SelectObject(hdc,bmp);
			bInvert=false;
		}
	}

	if (!bInvert)
	{
		SetDCBrushColor(hdc,metroColor&0xFFFFFF);
		FillRect(hdc,&rc,(HBRUSH)GetStockObject(DC_BRUSH));
		DrawIconEx(hdc,offset,offset,hIcon,iconSize,iconSize,0,NULL,DI_NORMAL);
		SelectObject(hdc,bmp0);
		int n=bitmapSize*bitmapSize;
		for (int i=0;i<n;i++)
			bits[i]|=0xFF000000;
	}
	DeleteDC(hdc);

	if (bDestroyIcon) DestroyIcon(hIcon);
	return bmp;
}

static HBITMAP BitmapFromMetroBitmap( HBITMAP hBitmap, int bitmapSize, DWORD metroColor, bool bDestroyBitmap=true )
{
	BITMAP info;
	GetObject(hBitmap,sizeof(info),&info);
	bool bGrayscale=DetectGrayscaleImage((const unsigned int*)info.bmBits,info.bmWidth,info.bmWidth,info.bmHeight);

	BITMAPINFO bi={0};
	bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth=bi.bmiHeader.biHeight=bitmapSize;
	bi.bmiHeader.biPlanes=1;
	bi.bmiHeader.biBitCount=32;
	RECT rc={0,0,bitmapSize,bitmapSize};

	HDC hdc=CreateCompatibleDC(NULL);
	HDC hsrc=CreateCompatibleDC(NULL);
	unsigned int *bits;
	HBITMAP bmp=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,(void**)&bits,NULL,0);
	HGDIOBJ bmp0=SelectObject(hdc,bmp);
	HGDIOBJ bmp02=SelectObject(hsrc,hBitmap);
	int offset=(bitmapSize-info.bmWidth)/2;
	if (g_bInvertMetroIcons && bGrayscale)
	{
		FillRect(hdc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
		BitBlt(hdc,offset,offset,info.bmWidth,info.bmHeight,hsrc,0,0,SRCCOPY);
		SelectObject(hdc,bmp0);
		unsigned int *bits2=bits+offset*(bitmapSize+1);
		CreateMonochromeImage(bits2,bitmapSize,info.bmWidth,info.bmHeight,metroColor);
	}
	else
	{
		if (metroColor!=0xFFFFFFFF && bGrayscale)
		{
			SetDCBrushColor(hdc,metroColor&0xFFFFFF);
			FillRect(hdc,&rc,(HBRUSH)GetStockObject(DC_BRUSH));
			BLENDFUNCTION func={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
			AlphaBlend(hdc,offset,offset,info.bmWidth,info.bmHeight,hsrc,0,0,info.bmWidth,info.bmHeight,func);
			SelectObject(hdc,bmp0);

			int n=bitmapSize*bitmapSize;
			for (int i=0;i<n;i++)
				bits[i]|=0xFF000000;
		}
		else
		{
			int n=bitmapSize*bitmapSize;
			for (int i=0;i<n;i++)
				bits[i]=0;
			BitBlt(hdc,offset,offset,info.bmWidth,info.bmHeight,hsrc,0,0,SRCCOPY);
		}
	}
	SelectObject(hsrc,bmp02);
	DeleteDC(hdc);
	DeleteDC(hsrc);

	if (bDestroyBitmap) DeleteObject(hBitmap);
	return bmp;
}

///////////////////////////////////////////////////////////////////////////////

static HBITMAP LoadMetroBitmap0(const wchar_t *path, int bitmapSize, DWORD metroColor = 0xFFFFFFFF)
{
	SIZE size={-bitmapSize,bitmapSize};
	HBITMAP hBitmap=LoadImageFile(path,&size,true,true,NULL);
	if (hBitmap)
	{
		if (metroColor==0xFFFFFFFF)
			return hBitmap;
		return BitmapFromMetroBitmap(hBitmap,bitmapSize,metroColor);
	}
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////

static bool ParseMetroBitmapLocation( wchar_t *location )
{
	CharUpper(location);
	while (1)
	{
		wchar_t *scale=wcswcs(location,L"SCALE-");
		if (!scale) return false;
		if (scale==location || scale[-1]=='\\' || scale[-1]=='.')
		{
			scale+=6;
			if (scale[0]>='0' && scale[0]<='9' && scale[1]>='0' && scale[1]<='9')
			{
				int digits=2;
				while (scale[digits]>='0' && scale[digits]<='9')
					digits++;
				scale[0]='%';
				scale[1]='d';
				if (digits>2)
					memmove(scale+2,scale+digits,Strlen(scale+digits)*2+2);
				return true;
			}
		}
		else
			scale+=6;
		location=scale;
	}
}

static int g_MetroIconScales[]={80,100,140,180}; // 24, 30, 42, 54 pixels

static HBITMAP LoadMetroBitmap( const wchar_t *location, int bitmapSize, DWORD metroColor )
{
	// pick the best sized icon
	int start;
	if (bitmapSize<=26)
		start=0;
	else if (bitmapSize<=32)
		start=1;
	else if (bitmapSize<=44)
		start=2;
	else
		start=3;
	wchar_t path[_MAX_PATH];
	bool bFound=false;
	for (int i=start;i<_countof(g_MetroIconScales);i++) // correct size or bigger (will scale down)
	{
		Sprintf(path,_countof(path),location,g_MetroIconScales[i]);
		if (GetFileAttributes(path)!=INVALID_FILE_ATTRIBUTES)
		{
			bFound=true;
			break;
		}
	}
	if (!bFound)
	{
		for (int i=start-1;i>=0;i--) // smaller (will scale up)
		{
			Sprintf(path,_countof(path),location,g_MetroIconScales[i]);
			if (GetFileAttributes(path)!=INVALID_FILE_ATTRIBUTES)
			{
				bFound=true;
				break;
			}
		}
	}
	if (bFound)
	{
		SIZE size={2-bitmapSize,bitmapSize-2};
		HBITMAP hBitmap=LoadImageFile(path,&size,true,true,NULL);
		if (hBitmap)
			return BitmapFromMetroBitmap(hBitmap,bitmapSize,metroColor);
	}
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////

static bool ParseMetroBitmapLocation2( wchar_t *location )
{
	CharUpper(location);
	wchar_t *png=wcswcs(location,L".PNG-");
	if (!png) return false;
	Strcpy(png,100,L".TARGETSIZE-%d.PNG");
	return true;
}

static int g_MetroIconSizes2[]={16,20,24,32,40,48,64};

static HBITMAP LoadMetroBitmap2( const wchar_t *location, int bitmapSize, DWORD metroColor )
{
	// pick the best sized icon
	int start=-1;
	for (int i=0;i<_countof(g_MetroIconSizes2);i++)
	{
		if (g_MetroIconSizes2[i]>=bitmapSize)
			break;
		start++;
	}
	if (start<0) start=0;
	wchar_t path[_MAX_PATH];
	int iconSize=0;
	for (int i=start;i<_countof(g_MetroIconSizes2);i++) // correct size or bigger (will scale down)
	{
		Sprintf(path,_countof(path),location,g_MetroIconSizes2[i]);
		if (GetFileAttributes(path)!=INVALID_FILE_ATTRIBUTES)
		{
			iconSize=g_MetroIconSizes2[i];
			break;
		}
	}
	if (!iconSize)
	{
		for (int i=start-1;i>=0;i--) // smaller (will scale up)
		{
			Sprintf(path,_countof(path),location,g_MetroIconSizes2[i]);
			if (GetFileAttributes(path)!=INVALID_FILE_ATTRIBUTES)
			{
				iconSize=g_MetroIconSizes2[i];
				break;
			}
		}
	}
	if (iconSize)
	{
		if (iconSize>bitmapSize)
			iconSize=bitmapSize;
		SIZE size={iconSize,iconSize};
		HBITMAP hBitmap=LoadImageFile(path,&size,true,true,NULL);
		if (hBitmap)
			return BitmapFromMetroBitmap(hBitmap,bitmapSize,metroColor);
	}
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////

void CItemManager::LoadIconData::Init( void )
{
	m_IconSizes[0]=SMALL_ICON_SIZE;
	m_IconSizes[1]=LARGE_ICON_SIZE;
	m_IconSizes[2]=EXTRA_LARGE_ICON_SIZE;
	m_IconSizes[3]=SMALL_ICON_SIZE-2;
	m_IconSizes[4]=LARGE_ICON_SIZE-2;
	m_IconSizes[5]=EXTRA_LARGE_ICON_SIZE-2;

	for (int i=0;i<_countof(m_TempLists);i++)
	{
		m_TempLists[i]=ImageList_Create(m_IconSizes[i],m_IconSizes[i],ILC_COLOR32,0,1);
		if (m_TempLists[i])
		{
			ImageList_SetImageCount(m_TempLists[i],1);
			HIMAGELIST_QueryInterface(m_TempLists[i],IID_IImageList2,(void**)&m_pTempLists[i]);
		}
	}
	if (FAILED(m_pFactory.CoCreateInstance(CLSID_WICImagingFactory)))
		m_pFactory.CoCreateInstance(CLSID_WICImagingFactory1);
}

void CItemManager::LoadIconData::Close( void )
{
	for (int i=0;i<_countof(m_TempLists);i++)
	{
		m_pTempLists[i]=NULL;
		if (m_TempLists[i])
			ImageList_Destroy(m_TempLists[i]);
	}
	m_pFactory=NULL;
}

int CItemManager::SMALL_ICON_SIZE;
int CItemManager::LARGE_ICON_SIZE;
int CItemManager::EXTRA_LARGE_ICON_SIZE=64;
int CItemManager::s_DPI;
int CItemManager::s_DPIOverride;

CItemManager g_ItemManager;

CItemManager::CItemManager( void )
{
	m_bInitialized=false;

	memset(m_CriticalSections,0,sizeof(m_CriticalSections));
	memset(m_CriticalSectionOwners,0,sizeof(m_CriticalSectionOwners));
	m_StartEvent=m_WorkEvent=m_ExitEvent=m_DoneEvent=m_PreloadItemsThread=m_RefreshInfoThread=m_SaveCacheThread=NULL;
	m_MainThreadId=m_PreloadItemsThreadId=m_RefreshInfoThreadId=0;
	m_DefaultSmallIcon=m_DefaultLargeIcon=m_DefaultExtraLargeIcon=NULL;
	m_bHasNewPrograms[0]=m_bHasNewPrograms[1]=m_bHasNewApps[0]=m_bHasNewApps[1]=m_bPreloadIcons=m_bPreloadFavorites=false;
	m_LoadingStage=LOAD_STOPPED;
	m_LastCacheSave=0;
	m_TransientHash=1;
}

CItemManager::~CItemManager( void )
{
	Close();
}

void CItemManager::Init( void )
{
	m_MainThreadId=GetCurrentThreadId();

	{
		// get the DPI setting
		HDC hdc=GetDC(NULL);
		s_DPI=GetDeviceCaps(hdc,LOGPIXELSY);
		ReleaseDC(NULL,hdc);
		s_DPIOverride=GetSettingInt(L"OverrideDPI");
		if (s_DPIOverride!=0)
		{
			if (s_DPIOverride<96) s_DPIOverride=96;
			if (s_DPIOverride>480) s_DPIOverride=480;
		}
	}

	SMALL_ICON_SIZE=GetSettingInt(L"SmallIconSize");
	LARGE_ICON_SIZE=GetSettingInt(L"LargeIconSize");
	m_OldSysAccentColor=GetSystemAccentColor();
	g_bInvertMetroIcons=GetSettingBool(L"InvertMetroIcons");
	m_bOldInvertIcons=g_bInvertMetroIcons;
	m_LoadIconData[0].Init();

	bool bRTL=IsLanguageRTL();

	CComString pPath;
	if (SUCCEEDED(ShGetKnownFolderPath(FOLDERID_StartMenu,&pPath)))
	{
		m_RootStartMenu1=pPath;
		m_RootStartMenu1+=L"\\";
		StringUpper(m_RootStartMenu1);
	}
	pPath.Clear();
	if (SUCCEEDED(ShGetKnownFolderPath(FOLDERID_CommonStartMenu,&pPath)))
	{
		m_RootStartMenu2=pPath;
		m_RootStartMenu2+=L"\\";
		StringUpper(m_RootStartMenu2);
	}
	pPath.Clear();
	if (SUCCEEDED(ShGetKnownFolderPath(FOLDERID_CommonPrograms,&pPath)))
	{
		m_RootCommonPrograms=pPath;
		m_RootCommonPrograms+=L"\\";
		StringUpper(m_RootCommonPrograms);
	}
	pPath.Clear();
	if (SUCCEEDED(ShGetKnownFolderPath(FOLDERID_Desktop,&pPath)))
	{
		m_RootDesktop=pPath;
		m_RootDesktop+=L"\\";
		StringUpper(m_RootDesktop);
	}
	pPath.Clear();

	m_RootGames=L"::{ED228FDF-9EA8-4870-83B1-96B02CFE0D52}\\";
	wchar_t text[_MAX_PATH];
	Sprintf(text,_countof(text),L"%s\\",GetSettingString(L"PinnedItemsPath"));
	DoEnvironmentSubst(text,_countof(text));
	m_RootStartMenu3=text;
	StringUpper(m_RootStartMenu3);
	Strcpy(text,_countof(text),TASKBAR_PINNED_ROOT L"\\");
	DoEnvironmentSubst(text,_countof(text));
	m_RootTaskbar=text;
	StringUpper(m_RootTaskbar);
	Strcpy(text,_countof(text),METRO_APP_ROOT L"\\");
	DoEnvironmentSubst(text,_countof(text));
	m_RootMetro=text;
	StringUpper(m_RootMetro);

	for (int i=0;i<=SHIL_LAST;i++)
	{
		CComPtr<IImageList> pList;
		if (SUCCEEDED(SHGetImageList(i,IID_IImageList,(void**)&pList)))
		{
			int width, height;
			pList->GetIconSize(&width,&height);
			m_ListSizes.emplace_back(width,i);
		}
	}
	std::sort(m_ListSizes.begin(),m_ListSizes.end());

	CreateDefaultIcons();
	LoadCacheFile();

	ItemInfo &item=m_ItemInfos.emplace(0,ItemInfo())->second;
	item.bIconOnly=true;
	item.smallIcon=m_DefaultSmallIcon;
	item.largeIcon=m_DefaultLargeIcon;
	item.extraLargeIcon=m_DefaultExtraLargeIcon;

	for (int i=0;i<RWLOCK_COUNT;i++)
		InitializeSRWLock(&m_RWLocks[i]);
	for (int i=0;i<LOCK_COUNT;i++)
		InitializeCriticalSection(&m_CriticalSections[i]);
	m_bPreloadIcons=GetSettingBool(L"PreCacheIcons");
	m_bPreloadFavorites=(GetSettingInt(L"Favorites")==2);

	m_LoadingStage=LOAD_LOADING;
	m_StartEvent=CreateEvent(NULL,TRUE,FALSE,NULL);
	m_WorkEvent=CreateEvent(NULL,FALSE,FALSE,NULL);
	m_ExitEvent=CreateEvent(NULL,TRUE,FALSE,NULL);
	m_DoneEvent=CreateEvent(NULL,TRUE,FALSE,NULL);

	m_PreloadItemsThread=CreateThread(NULL,0,StaticPreloadItemsThread,this,0,&m_PreloadItemsThreadId);
	m_RefreshInfoThread=CreateThread(NULL,0,StaticRefreshInfoThread,this,0,&m_RefreshInfoThreadId);

	LoadOldItems();

	m_bInitialized=true;
}

void CItemManager::Close( void )
{
	if (!m_bInitialized) return;

	m_LoadingStage=LOAD_STOPPING;
	SetEvent(m_ExitEvent);
	if (m_PreloadItemsThread)
	{
		WaitForSingleObject(m_PreloadItemsThread,INFINITE);
		CloseHandle(m_PreloadItemsThread);
		m_PreloadItemsThread=NULL;
	}
	if (m_RefreshInfoThread)
	{
		WaitForSingleObject(m_RefreshInfoThread,INFINITE);
		CloseHandle(m_RefreshInfoThread);
		m_RefreshInfoThread=NULL;
	}
	if (m_SaveCacheThread)
	{
		WaitForSingleObject(m_SaveCacheThread,INFINITE);
		CloseHandle(m_SaveCacheThread);
		m_SaveCacheThread=NULL;
	}
	m_LoadingStage=LOAD_STOPPED;

	for (std::multimap<unsigned int,IconInfo>::const_iterator it=m_IconInfos.begin();it!=m_IconInfos.end();++it)
	{
		if (it->second.bitmap)
			DeleteObject(it->second.bitmap);
	}

	for (int i=0;i<LOCK_COUNT;i++)
		DeleteCriticalSection(&m_CriticalSections[i]);
	g_pAppResolver=NULL;
	CloseHandle(m_StartEvent);
	CloseHandle(m_WorkEvent);
	CloseHandle(m_ExitEvent);
	CloseHandle(m_DoneEvent);
	m_LoadIconData[0].Close();
	m_bInitialized=false;
}

void CItemManager::CreateDefaultIcons( void )
{
	SHFILEINFO info;
	int index;
	if (SHGetFileInfo(L"file",FILE_ATTRIBUTE_NORMAL,&info,sizeof(info),SHGFI_USEFILEATTRIBUTES|SHGFI_SYSICONINDEX))
		index=info.iIcon;
	else
		index=-1;
	IconInfo icon;
	icon.bTemp=false;
	icon.bMetro=false;
	icon.timestamp.dwLowDateTime=icon.timestamp.dwHighDateTime=0;
	icon.sizeType=ICON_SIZE_TYPE_SMALL;
	if (index>=0)
		icon.bitmap=BitmapFromIcon(LoadShellIcon(index,SMALL_ICON_SIZE),SMALL_ICON_SIZE);
	else
		icon.bitmap=NULL;
	m_DefaultSmallIcon=&m_IconInfos.emplace(0,icon)->second;

	icon.sizeType=ICON_SIZE_TYPE_LARGE;
	if (index>=0)
		icon.bitmap=BitmapFromIcon(LoadShellIcon(index,LARGE_ICON_SIZE),LARGE_ICON_SIZE);
	else
		icon.bitmap=NULL;
	m_DefaultLargeIcon=&m_IconInfos.emplace(0,icon)->second;

	icon.sizeType=ICON_SIZE_TYPE_EXTRA_LARGE;
	if (index>=0)
		icon.bitmap=BitmapFromIcon(LoadShellIcon(index,EXTRA_LARGE_ICON_SIZE),EXTRA_LARGE_ICON_SIZE);
	else
		icon.bitmap=NULL;
	m_DefaultExtraLargeIcon=&m_IconInfos.emplace(0,icon)->second;
}

CItemManager::LoadIconData &CItemManager::GetLoadIconData( void )
{
	DWORD thread=GetCurrentThreadId();
	if (thread==m_RefreshInfoThreadId)
		return m_LoadIconData[2];
	if (thread==m_PreloadItemsThreadId)
		return m_LoadIconData[1];
	Assert(thread==m_MainThreadId);
	return m_LoadIconData[0];
}

void CItemManager::ResetTempIcons( void )
{
	Assert(GetCurrentThreadId()==m_MainThreadId);
	Lock cleanupLock(this,LOCK_CLEANUP);
	RWLock itemLock(this,true,RWLOCK_ITEMS);
	RWLock iconLock(this,true,RWLOCK_ICONS);

	COLORREF sysColor=GetSystemAccentColor();
	bool bResetMetro=m_OldSysAccentColor!=sysColor;
	m_OldSysAccentColor=sysColor;

	g_bInvertMetroIcons=GetSettingBool(L"InvertMetroIcons");
	if (m_bOldInvertIcons!=g_bInvertMetroIcons)
		bResetMetro=true;
	m_bOldInvertIcons=g_bInvertMetroIcons;

	if (bResetMetro)
	{
		for (std::map<unsigned int,const ItemInfo*>::iterator it=m_MetroItemInfos10.begin();it!=m_MetroItemInfos10.end();)
		{
			std::map<unsigned int,const ItemInfo*>::iterator next=it; ++next;
			if (it->second && !it->second->packagePath.IsEmpty())
			{
				const_cast<ItemInfo*>(it->second)->validFlags&=~(INFO_ICON|INFO_METRO);
				m_MetroItemInfos10.erase(it);
			}
			it=next;
		}
	}
	{
		// remove temp items from the queue
		std::list<ItemInfo*>::iterator it=m_ItemQueue.begin();
		while (it!=m_ItemQueue.end())
		{
			std::list<ItemInfo*>::iterator next=it; ++next;
			if ((*it)->bTemp)
			{
				Assert(!(*it)->largeIcon && !(*it)->extraLargeIcon && ((*it)->smallIcon==m_DefaultSmallIcon || (*it)->smallIcon->bTemp));
				m_ItemQueue.erase(it);
			}
			it=next;
		}
	}

	int metroFlags=bResetMetro?INFO_METRO:0;
	{
		// remove temp items from the cache
		std::multimap<unsigned int,ItemInfo>::iterator it=m_ItemInfos.begin();
		while (it!=m_ItemInfos.end())
		{
			std::multimap<unsigned int,ItemInfo>::iterator next=it; ++next;
			if (it->second.bTemp)
			{
				Assert(it->second.largeIcon==m_DefaultLargeIcon && it->second.extraLargeIcon==m_DefaultExtraLargeIcon && (it->second.smallIcon==m_DefaultSmallIcon || it->second.smallIcon->bTemp));
				m_ItemInfos.erase(it);
			}
			else
			{
				if (it->second.smallIcon && (it->second.smallIcon->bTemp || (it->second.smallIcon->bMetro && bResetMetro)))
				{
					it->second.smallIcon=m_DefaultSmallIcon;
					it->second.validFlags&=~(INFO_SMALL_ICON|metroFlags);
				}
				if (it->second.largeIcon && (it->second.largeIcon->bTemp || (it->second.largeIcon->bMetro && bResetMetro)))
				{
					it->second.largeIcon=m_DefaultLargeIcon;
					it->second.validFlags&=~(INFO_LARGE_ICON|metroFlags);
				}
				if (it->second.extraLargeIcon && (it->second.extraLargeIcon->bTemp || (it->second.extraLargeIcon->bMetro && bResetMetro)))
				{
					it->second.extraLargeIcon=m_DefaultExtraLargeIcon;
					it->second.validFlags&=~(INFO_EXTRA_LARGE_ICON|metroFlags);
				}
			}
			it=next;
		}
	}

	{
		// remove temp icons
		std::multimap<unsigned int,IconInfo>::iterator it=m_IconInfos.begin();
		while (it!=m_IconInfos.end())
		{
			std::multimap<unsigned int,IconInfo>::iterator next=it; ++next;
			if (it->second.bTemp || (it->second.bMetro && bResetMetro))
			{
				if (it->second.bitmap)
					DeleteObject(it->second.bitmap);
				m_IconInfos.erase(it);
			}
			it=next;
		}
	}

	{
		// delete old bitmaps
		for (std::vector<HBITMAP>::iterator it=m_OldBitmaps.begin();it!=m_OldBitmaps.end();++it)
			DeleteObject(*it);
		m_OldBitmaps.clear();
	}
	m_TransientHash=1;
}

static bool ComparePidls( PIDLIST_ABSOLUTE pidl1, PIDLIST_ABSOLUTE pidl2 )
{
	if (!pidl1 && !pidl2) return true;
	if (!pidl1 || !pidl2) return false;
	int size1=ILGetSize(pidl1);
	int size2=ILGetSize(pidl2);
	if (size1!=size2) return false;
	return memcmp(pidl1,pidl2,size1)==0;
}

const CItemManager::ItemInfo *CItemManager::GetItemInfo( IShellItem *pItem, PIDLIST_ABSOLUTE pidl, int refreshFlags, TLocation location )
{
	Assert(!RWLock::ThreadHasReadLock(RWLOCK_ITEMS));
	if ((refreshFlags&INFO_METRO) && GetWinVersion()<WIN_VER_WIN8)
		refreshFlags&=~INFO_METRO;
	CComString pName;
	if (refreshFlags&INFO_NO_PATH)
		refreshFlags&=~INFO_NO_PATH;
	else
		pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName);

	CString path, PATH;
	unsigned int hash;
	if (pName)
	{
		path=pName;
		PATH=path;
		StringUpper(PATH);
		hash=CalcFNVHash(PATH);
	}
	else
	{
		hash=CalcFNVHash(pidl,ILGetSize(pidl));
	}

	FILETIME writeTime={0}, createTime={0};
	if (!path.IsEmpty())
	{
		if (!MenuGetFileTimestamp(path,&writeTime,&createTime))
		{
			if (refreshFlags&INFO_VALIDATE_FILE)
				return NULL;
			path.Empty();
		}
	}

	bool bDelay=GetSettingBool(L"DelayIcons");
	ItemInfo *pInfo=NULL;
	{
		RWLock lock(this,true,RWLOCK_ITEMS);
		std::multimap<unsigned int,ItemInfo>::iterator it=m_ItemInfos.find(hash);
		for (;it!=m_ItemInfos.end() && it->first==hash;++it)
		{
			if ((!PATH.IsEmpty() && wcscmp(PATH,it->second.PATH)==0) || (PATH.IsEmpty() && ILIsEqual(pidl,it->second.GetPidl())))
			{
				pInfo=&it->second;
				break;
			}
		}
		if (!pInfo)
		{
			pInfo=&m_ItemInfos.emplace(hash,ItemInfo())->second;
			pInfo->pidl.Clone(pidl);
			pInfo->path=path;
			pInfo->PATH=PATH;
			pInfo->createstamp=createTime;
			pInfo->writestamp=writeTime;
			pInfo->smallIcon=m_DefaultSmallIcon;
			pInfo->largeIcon=m_DefaultLargeIcon;
			pInfo->extraLargeIcon=m_DefaultExtraLargeIcon;
			pInfo->validFlags=0;
		}
		else
		{
			if (!pInfo->packagePath.IsEmpty())
				MenuGetFileTimestamp(pInfo->packagePath,&writeTime,&createTime);
			if (CompareFileTime(&pInfo->writestamp,&writeTime)!=0)
			{
				if (!PATH.IsEmpty() && !ComparePidls(pInfo->pidl,pidl) && !ComparePidls(pInfo->newPidl,pidl))
					pInfo->newPidl.Clone(pidl);
				pInfo->writestamp=writeTime;
				pInfo->validFlags=0;
			}
		}
		if (pInfo->location==LOCATION_UNKNOWN && !pInfo->PATH.IsEmpty())
		{
			if (location!=LOCATION_UNKNOWN)
				pInfo->location=location;
			else
				pInfo->location=DetermineLocation(pInfo->PATH);
		}
		refreshFlags&=~pInfo->validFlags;
	}

	if (refreshFlags)
	{
		int refreshMask=bDelay?INFO_DATA:(INFO_DATA|INFO_ICON);
		if (refreshFlags&refreshMask)
			RefreshItemInfo(pInfo,refreshFlags&refreshMask,pItem,false);
		refreshFlags&=~refreshMask;
		if (refreshFlags)
		{
			RWLock lock(this,true,RWLOCK_ITEMS);
			QueueItemInfo(pInfo,refreshFlags);
		}
	}

	return pInfo;
}

const CItemManager::ItemInfo *CItemManager::GetItemInfo( CString path, int refreshFlags, TLocation location )
{
	Assert(!RWLock::ThreadHasReadLock(RWLOCK_ITEMS));
	Assert(!path.IsEmpty());
	if ((refreshFlags&INFO_METRO) && GetWinVersion()<WIN_VER_WIN8)
		refreshFlags&=~INFO_METRO;
	CString PATH=path;
	StringUpper(PATH);
	unsigned int hash=CalcFNVHash(PATH);

	FILETIME writeTime={0}, createTime={0};
	if (!MenuGetFileTimestamp(path,&writeTime,&createTime))
	{
		if (refreshFlags&INFO_VALIDATE_FILE)
			return NULL;
		path.Empty();
	}

	bool bDelay=GetSettingBool(L"DelayIcons");
	ItemInfo *pInfo=NULL;
	{
		RWLock lock(this,true,RWLOCK_ITEMS);
		std::multimap<unsigned int,ItemInfo>::iterator it=m_ItemInfos.find(hash);
		for (;it!=m_ItemInfos.end() && it->first==hash;++it)
		{
			if (wcscmp(PATH,it->second.PATH)==0)
			{
				pInfo=&it->second;
				break;
			}
		}
		if (!pInfo)
		{
			pInfo=&m_ItemInfos.emplace(hash,ItemInfo())->second;
			if (!PATH.IsEmpty())
				MenuParseDisplayName(path,&pInfo->pidl,NULL,NULL);
			if (pInfo->pidl)
				pInfo->path=path;
			pInfo->PATH=PATH;
			pInfo->createstamp=createTime;
			pInfo->writestamp=writeTime;
			pInfo->smallIcon=m_DefaultSmallIcon;
			pInfo->largeIcon=m_DefaultLargeIcon;
			pInfo->extraLargeIcon=m_DefaultExtraLargeIcon;
			pInfo->validFlags=0;
		}
		else
		{
			if (!pInfo->packagePath.IsEmpty())
				MenuGetFileTimestamp(pInfo->packagePath,&writeTime,&createTime);
			if (CompareFileTime(&pInfo->writestamp,&writeTime)!=0)
			{
				CAbsolutePidl pidl;
				if (!PATH.IsEmpty())
					MenuParseDisplayName(PATH,&pidl,NULL,NULL);
				if (!ComparePidls(pInfo->pidl,pidl) && !ComparePidls(pInfo->newPidl,pidl))
					pInfo->newPidl.Swap(pidl);
				pInfo->writestamp=writeTime;
				pInfo->validFlags=0;
			}
		}
		if (pInfo->location==LOCATION_UNKNOWN && !pInfo->PATH.IsEmpty())
		{
			if (location!=LOCATION_UNKNOWN)
				pInfo->location=location;
			else
				pInfo->location=DetermineLocation(pInfo->PATH);
		}
		refreshFlags&=~pInfo->validFlags;
	}

	if (refreshFlags)
	{
		int refreshMask=bDelay?INFO_DATA:(INFO_DATA|INFO_ICON);
		if (refreshFlags&refreshMask)
			RefreshItemInfo(pInfo,refreshFlags&refreshMask,NULL,false);
		refreshFlags&=~refreshMask;
		if (refreshFlags)
		{
			RWLock lock(this,true,RWLOCK_ITEMS);
			QueueItemInfo(pInfo,refreshFlags);
		}
	}

	return pInfo;
}

void CItemManager::UpdateItemInfo( const ItemInfo *pInfo, int refreshFlags, bool bHasWriteLock )
{
	if (bHasWriteLock)
	{
		Assert(RWLock::ThreadHasWriteLock(RWLOCK_ITEMS));
	}
	else
	{
		Assert(!RWLock::ThreadHasReadLock(RWLOCK_ITEMS));
	}
	refreshFlags&=~pInfo->validFlags; // potentially out of lock, assuming validFlags is atomic
	if (refreshFlags)
	{
		RWLock lock(this,true,bHasWriteLock?RWLOCK_COUNT:RWLOCK_ITEMS);
		if ((refreshFlags&INFO_ICON) && !(refreshFlags&INFO_REFRESH_NOW) && GetSettingBool(L"DelayIcons"))
		{
			QueueItemInfo(const_cast<ItemInfo*>(pInfo),refreshFlags&INFO_ICON);
			refreshFlags&=~INFO_ICON;
		}
		if (refreshFlags)
			RefreshItemInfo(const_cast<ItemInfo*>(pInfo),refreshFlags,NULL,true);
	}
}

const CItemManager::ItemInfo *CItemManager::GetCustomIcon( const wchar_t *location, int index, TIconSizeType iconSizeType, bool bTemp )
{
	Assert(!RWLock::ThreadHasReadLock(RWLOCK_ITEMS));
	unsigned int hash=CalcFNVHash(location,CalcFNVHash(&index,4));
	ItemInfo *pInfo=NULL;
	bool bDelay=GetSettingBool(L"DelayIcons");
	int refreshFlags=0;
	{
		RWLock lock(this,true,RWLOCK_ITEMS);
		std::multimap<unsigned int,ItemInfo>::iterator it=m_ItemInfos.find(hash);
		for (;it!=m_ItemInfos.end() && it->first==hash;++it)
		{
			if (it->second.bIconOnly && it->second.bTemp==bTemp)
			{
				pInfo=&it->second;
				break;
			}
		}
		if (!pInfo)
		{
			pInfo=&m_ItemInfos.emplace(hash,ItemInfo())->second;
			pInfo->bIconOnly=true;
			pInfo->bTemp=bTemp;
			pInfo->iconPath=location;
			pInfo->iconIndex=index;
			pInfo->smallIcon=m_DefaultSmallIcon;
			pInfo->largeIcon=m_DefaultLargeIcon;
			pInfo->extraLargeIcon=m_DefaultExtraLargeIcon;
		}

		if (iconSizeType==ICON_SIZE_TYPE_SMALL) refreshFlags|=INFO_SMALL_ICON;
		if (iconSizeType==ICON_SIZE_TYPE_LARGE) refreshFlags|=INFO_LARGE_ICON;
		if (iconSizeType==ICON_SIZE_TYPE_EXTRA_LARGE) refreshFlags|=INFO_EXTRA_LARGE_ICON;
		refreshFlags&=~pInfo->validFlags;

		if (refreshFlags && bDelay)
		{
			QueueItemInfo(pInfo,refreshFlags);
			refreshFlags=0;
		}
	}
	if (!bDelay && refreshFlags)
	{
		RefreshItemInfo(pInfo,refreshFlags,NULL,false);
	}
	return pInfo;
}

const CItemManager::ItemInfo *CItemManager::GetCustomIcon( const wchar_t *path, TIconSizeType iconSizeType )
{
	Assert(!RWLock::ThreadHasReadLock(RWLOCK_ITEMS));
	if (!path)
	{
		RWLock lock(this,false,RWLOCK_ITEMS);
		return &m_ItemInfos.find(0)->second;
	}
	wchar_t text[1024];
	Strcpy(text,_countof(text),path);
	DoEnvironmentSubst(text,_countof(text));
	wchar_t *c=wcsrchr(text,',');
	int index=0;
	if (c)
	{
		*c=0;
		index=-_wtol(c+1);
	}
	// special handling for Apps icon
	if (!text[0] && index==-IDI_APPSICON)
	{
		if (IsWin11())
			index=-IDI_APPSICON11;
		else if (GetWinVersion()==WIN_VER_WIN10)
			index=-IDI_APPSICON10;
	}
	return GetCustomIcon(text,index,iconSizeType,false);
}

const CItemManager::ItemInfo* CItemManager::GetLinkIcon(IShellLink* link, TIconSizeType iconSizeType)
{
	wchar_t location[_MAX_PATH];
	int index;

	if (link->GetIconLocation(location, _countof(location), &index) == S_OK && location[0])
		return GetCustomIcon(location, index, iconSizeType, (index == 0)); // assuming that if index!=0 the icon comes from a permanent location like a dll or exe

	CComQIPtr<IPropertyStore> store(link);
	if (store)
	{
		//  Name:     System.AppUserModel.DestListLogoUri -- PKEY_AppUserModel_DestListLogoUri
		//  Type:     String -- VT_LPWSTR
		//  FormatID: {9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, 29
		static const PROPERTYKEY PKEY_AppUserModel_DestListLogoUri = { {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 29 };

		auto logoUri = GetPropertyStoreString(store, PKEY_AppUserModel_DestListLogoUri);
		if (!logoUri.IsEmpty())
		{
			auto appId = GetPropertyStoreString(store, PKEY_AppUserModel_ID);
			if (!appId.IsEmpty())
			{
				CComPtr<IResourceManager> resManager;
				if (SUCCEEDED(resManager.CoCreateInstance(CLSID_ResourceManager)))
				{
					if (SUCCEEDED(resManager->InitializeForPackage(GetPackageFullName(appId))))
					{
						CComPtr<IResourceMap> resMap;
						if (SUCCEEDED(resManager->GetMainResourceMap(IID_PPV_ARGS(&resMap))))
						{
							CComPtr<IResourceContext> resContext;
							if (SUCCEEDED(resManager->GetDefaultContext(IID_PPV_ARGS(&resContext))))
								resContext->SetTargetSize(GetIconSize(iconSizeType));

							CComString location;
							if (SUCCEEDED(resMap->GetFilePath(logoUri, &location)))
								return GetCustomIcon(location, -65536, iconSizeType, true);
						}
					}
				}
			}
		}
	}

	return nullptr;
}

const CItemManager::ItemInfo *CItemManager::GetMetroAppInfo10( const wchar_t *appid )
{
	wchar_t APPID[256];
	Strcpy(APPID,_countof(APPID),appid);
	CharUpper(APPID);
	if (APPID[0]=='\\' || APPID[0]=='{' || APPID[1]==':' || wcsstr(APPID,L".AUTOGENERATED."))
		return NULL; // attempt to recognize appids that are for sure not Metro
	unsigned int hash=CalcFNVHash(APPID);
	const ItemInfo *pInfo=NULL;
	{
		RWLock lock(this,false,RWLOCK_ITEMS);
		// the key is a hash of the uppercase appid
		std::map<unsigned int,const ItemInfo*>::const_iterator it=m_MetroItemInfos10.find(hash);
		if (it!=m_MetroItemInfos10.end())
			pInfo=it->second;
		else if (m_BlackListInfos10.find(hash)!=m_BlackListInfos10.end())
			return NULL;
	}
	if (pInfo)
	{
		UpdateItemInfo(pInfo,INFO_LINK|INFO_METRO);
		return pInfo;
	}

	CComPtr<IShellItem> pItem;
	if (SUCCEEDED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder2,0,appid,IID_IShellItem,(void**)&pItem)))
	{
		CAbsolutePidl pidl;
		if (SUCCEEDED(SHGetIDListFromObject(pItem,&pidl)))
			pInfo=GetItemInfo(pItem,pidl,INFO_LINK|INFO_METRO,LOCATION_METRO);
	}

	{
		RWLock lock(this,true,RWLOCK_ITEMS);
		std::map<unsigned int,const ItemInfo*>::const_iterator it=m_MetroItemInfos10.find(hash);
		if (it!=m_MetroItemInfos10.end() && it->second)
			return it->second;
		else
		{
			if (!pInfo)
				m_BlackListInfos10.insert(hash);
			if (pInfo && !pInfo->IsMetroApp())
				pInfo=NULL;
			m_MetroItemInfos10[hash]=pInfo;
			return pInfo;
		}
	}
}

// requires LOCK_ITEMS to be held
void CItemManager::QueueItemInfo( ItemInfo *pInfo, int refreshFlags )
{
	Assert(RWLock::ThreadHasWriteLock(RWLOCK_ITEMS));
	DWORD thread=GetCurrentThreadId();
	Assert(thread!=m_RefreshInfoThreadId);
	pInfo->refreshFlags|=refreshFlags&~pInfo->validFlags&(INFO_DATA|INFO_ICON);
	if (pInfo->refreshFlags)
	{
		std::list<ItemInfo*> &queue=(thread==m_PreloadItemsThreadId)?m_ItemQueueLow:m_ItemQueue;
		for (std::list<ItemInfo*>::const_iterator it=queue.begin();it!=queue.end();++it)
		{
			if (*it==pInfo)
				return;
		}
		queue.push_back(pInfo);
		SetEvent(m_WorkEvent);
		if (thread!=m_PreloadItemsThreadId)
			SetEvent(m_StartEvent);
	}
}

void CItemManager::WaitForShortcuts( const POINT &balloonPos )
{
	if (m_PreloadItemsThreadId)
	{
		if (WaitForSingleObject(m_DoneEvent,1000)!=WAIT_TIMEOUT)
			return;
		CWindow tooltip=CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|(IsLanguageRTL()?WS_EX_LAYOUTRTL:0),TOOLTIPS_CLASS,NULL,WS_POPUP|TTS_BALLOON|TTS_NOPREFIX,0,0,0,0,NULL,NULL,g_Instance,NULL);
		tooltip.SendMessage(TTM_SETMAXTIPWIDTH,0,500);
		TOOLINFO tool={sizeof(tool),TTF_TRANSPARENT|TTF_TRACK|(IsLanguageRTL()?TTF_RTLREADING:0U)};
		tool.uId=1;
		CString message=LoadStringEx(IDS_MENU_BUSY);
		tool.lpszText=(LPWSTR)(const wchar_t*)message;
		tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);
		tooltip.SendMessage(TTM_SETTITLE,TTI_INFO,(LPARAM)(const wchar_t*)LoadStringEx(IDS_APP_TITLE));
		tooltip.SendMessage(TTM_TRACKPOSITION,0,MAKELONG(balloonPos.x,balloonPos.y));
		tooltip.SendMessage(TTM_TRACKACTIVATE,TRUE,(LPARAM)&tool);
		tooltip.SendMessage(TTM_UPDATE);
		WaitForSingleObject(m_DoneEvent,INFINITE);
		tooltip.DestroyWindow();
	}
}

bool CItemManager::IsTaskbarPinned( const wchar_t *appid )
{
	Assert(GetCurrentThreadId()==m_MainThreadId);
	RWLock lock(this,true,RWLOCK_ITEMS);
	const ItemInfo *pInfo=NULL;
	for (std::multimap<unsigned int,ItemInfo>::const_iterator it=m_ItemInfos.begin();it!=m_ItemInfos.end();++it)
	{
		if (it->second.bLink && it->second.location==LOCATION_TASKBAR)
		{
			UpdateItemInfo(&it->second,INFO_LINK_APPID,true);
			if (wcscmp(it->second.appid,appid)==0)
			{
				if (GetFileAttributes(it->second.path)!=INVALID_FILE_ATTRIBUTES)
					return true;
			}
		}
	}
	return false;
}

void EncodeRot13( wchar_t *text )
{
	for (;*text;text++)
	{
		if (*text>='a' && *text<='z')
			*text=(*text-'a'+13)%26+'a';
		else if (*text>='A' && *text<='Z')
			*text=(*text-'A'+13)%26+'A';
	}
}

static KNOWNFOLDERID g_KnownPrefixes[]=
{
	FOLDERID_SystemX86,
	FOLDERID_System,
	FOLDERID_Windows,
	FOLDERID_ProgramFilesX86,
	FOLDERID_ProgramFilesX64,
	FOLDERID_Programs,
	FOLDERID_CommonPrograms,
	FOLDERID_StartMenu,
	FOLDERID_CommonStartMenu,
};

void EncodeUserAssistPath( wchar_t *path )
{
	for (int i=0;i<_countof(g_KnownPrefixes);i++)
	{
		CComString knownPath;
		if (FAILED(SHGetKnownFolderPath(g_KnownPrefixes[i],0,NULL,&knownPath)))
			continue;
		int len=Strlen(knownPath);
		if (_wcsnicmp(path,knownPath,len)==0)
		{
			CComString guid;
			StringFromCLSID(g_KnownPrefixes[i],&guid);
			wchar_t name[_MAX_PATH];
			Sprintf(name,_countof(name),L"%s%s",(const wchar_t*)guid,path+len);
			Strcpy(path,_MAX_PATH,name);
			return;
		}
	}
}

bool CItemManager::IsPathUsed( CRegKey &regKey, const wchar_t *path, const FILETIME &createstamp, const KnownPathGuid *knownPaths, int knownPathsCount, bool bMetroApp )
{
	{
		unsigned int hash=CalcFNVHash(path);
		OldItemInfo key={hash};
		std::vector<OldItemInfo>::const_iterator it=std::lower_bound(m_OldItemInfos.begin(),m_OldItemInfos.end(),key);
		if (it!=m_OldItemInfos.end() && it->hash==hash)
		{
			if (CompareFileTime(&createstamp,&it->timestamp)<0)
				return true;
		}
	}

	wchar_t name[_MAX_PATH];
	if (knownPaths)
	{
		for (int i=0;i<knownPathsCount;i++)
		{
			if (i==knownPathsCount-1)
				Strcpy(name,_countof(name),path);
			else if (wcsncmp(path,knownPaths[i].path,knownPaths[i].pathLen)==0)
			{
				Sprintf(name,_countof(name),L"%s%s", (const wchar_t*)knownPaths[i].guid,path+knownPaths[i].pathLen);
				break;
			}
		}
	}
	else
	{
		Strcpy(name,_countof(name),path);
	}
	EncodeRot13(name);
	UserAssistData data;
	DWORD size=sizeof(data);
	if (regKey.QueryBinaryValue(name,&data,&size)==ERROR_SUCCESS)
	{
		if (bMetroApp)
		{
			// count is unreliable, the timestamp can be 0
			return ((data.timestamp.dwLowDateTime|data.timestamp.dwHighDateTime)==0 || CompareFileTime(&createstamp,&data.timestamp)<0);
		}
		else
		{
			return (data.count>0 && CompareFileTime(&createstamp,&data.timestamp)<0);
		}
	}
	return false;
}

void CItemManager::UpdateNewPrograms( const POINT &balloonPos )
{
	Assert(GetCurrentThreadId()==m_MainThreadId);
	m_NewPrograms.clear();
	m_NewProgramRoots.clear();
	m_bHasNewPrograms[0]=m_bHasNewPrograms[1]=m_bHasNewApps[0]=m_bHasNewApps[1]=false;
#ifdef FORCE_ALL_NEW
	m_bHasNewPrograms[0]=m_bHasNewPrograms[1]=m_bHasNewApps[0]=m_bHasNewApps[1]=true;
#endif
	if (!GetSettingBool(L"HighlightNew"))
		return;
	bool bNewApps=GetSettingBool(L"HighlightNewApps");
/*	{
		// the new programs need the MFU system to be enabled
		CRegKey regKey;
		if (regKey.Open(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",KEY_READ)!=ERROR_SUCCESS)
			return;
		DWORD val;
		if (regKey.QueryDWORDValue(L"Start_TrackProgs",val)==ERROR_SUCCESS && !val)
			return;
	}
*/
	LONGLONG installTime=0;
	{
		CRegKey regKey;
		if (regKey.Open(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",KEY_READ)==ERROR_SUCCESS)
		{
			DWORD time=0;
			if (regKey.QueryDWORDValue(L"InstallDate",time)==ERROR_SUCCESS)
			{
				installTime=Int32x32To64(time,10000000)+116444736000000000;
			}
		}
		FILETIME ft={(DWORD)installTime,(DWORD)(installTime>>32)};
		SYSTEMTIME st;
		GetSystemTime(&st);
		LOG_MENU(LOG_NEW,L"Current time: %02d.%02d.%04d:%02d:%02d",st.wDay,st.wMonth,st.wYear,st.wHour,st.wMinute);
		FileTimeToSystemTime(&ft,&st);
		LOG_MENU(LOG_NEW,L"Install time: %02d.%02d.%04d:%02d:%02d",st.wDay,st.wMonth,st.wYear,st.wHour,st.wMinute);
	}

	CRegKey regKeyExe;
	regKeyExe.Open(HKEY_CURRENT_USER,USERASSIST_APPIDS_KEY,KEY_READ);
	CRegKey regKeyLink;
	regKeyLink.Open(HKEY_CURRENT_USER,USERASSIST_LINKS_KEY,KEY_READ);

	KnownPathGuid knownPaths[_countof(g_KnownPrefixes)+1];

	int OLD_PROGRAMS_AGE=GetSettingInt(L"OldProgramsAge");
	if (OLD_PROGRAMS_AGE<0) OLD_PROGRAMS_AGE=0;
	if (OLD_PROGRAMS_AGE>48) OLD_PROGRAMS_AGE=48;
	const int INSTALL_GRACE_PERIOD=12; // ignore programs installed within 12 hours of system install
	int knownPathsCount=0;
	for (int i=0;i<_countof(g_KnownPrefixes);i++)
	{
		if (SUCCEEDED(SHGetKnownFolderPath(g_KnownPrefixes[i],0,NULL,&knownPaths[knownPathsCount].path)))
		{
			StringFromCLSID(g_KnownPrefixes[i],&knownPaths[knownPathsCount].guid);
			knownPaths[knownPathsCount].path.MakeUpper();
			knownPaths[knownPathsCount].pathLen=Strlen(knownPaths[knownPathsCount].path);
			knownPathsCount++;
		}
	}
	knownPaths[knownPathsCount++].pathLen=0;

	LONGLONG curTime;
	GetSystemTimeAsFileTime((FILETIME*)&curTime);
	WaitForShortcuts(balloonPos);
	LARGE_INTEGER newestProgram={0}, newestApp={0};
	{
		RWLock lock(this,true,RWLOCK_ITEMS);
		for (std::multimap<unsigned int,ItemInfo>::const_iterator it=m_ItemInfos.begin();it!=m_ItemInfos.end();++it)
		{
			if (it->second.location!=LOCATION_START_MENU && it->second.location!=LOCATION_METRO)
				continue;
			if ((it->second.bMetroLink || it->second.bMetroApp) && !bNewApps)
				continue;
			if (!it->second.bLink && !it->second.bMetroApp)
			{
				LOG_MENU(LOG_NEW,L"Ignoring new: %s not a link",it->second.path);
				continue;
			}
			if (it->second.bNoNew)
			{
				LOG_MENU(LOG_NEW,L"Ignoring new: %s suppressed",it->second.path);
				continue;
			}
#ifdef FORCE_ALL_NEW
			m_NewPrograms.push_back(&it->second);
			continue;
#endif
			LONGLONG timestamp=it->second.createstamp.dwLowDateTime|(((LONGLONG)it->second.createstamp.dwHighDateTime)<<32);
			int hours1=(int)((curTime-timestamp)/36000000000);
			if (hours1<0)
			{
				LOG_MENU(LOG_NEW,L"Ignoring new: %s creation time too new - %d hours",it->second.path,hours1);
				continue;
			}
			if (hours1>OLD_PROGRAMS_AGE)
			{
				LOG_MENU(LOG_NEW,L"Ignoring new: %s creation time too old - %d hours",it->second.path,hours1);
				continue;
			}
			if (wcswcs(PathFindFileName(it->second.PATH),L"UNINSTALL"))
			{
				LOG_MENU(LOG_NEW,L"Ignoring new: %s contains UNINSTALL",it->second.path);
				continue;
			}
			if (it->second.location==LOCATION_START_MENU)
			{
				if (wcscmp(PathFindExtension(it->second.targetPATH),L".EXE")!=0)
				{
					LOG_MENU(LOG_NEW,L"Ignoring new: %s target not exe",it->second.path);
					continue;
				}
			}

			if (it->second.bLink && GetFileAttributes(it->second.path)==INVALID_FILE_ATTRIBUTES)
			{
				LOG_MENU(LOG_NEW,L"Ignoring new: %s missing file",it->second.path);
				continue;
			}
			// existing link to exe that is newer than 48 hours
			int hours2=0, hours3=0;
			if (it->second.location==LOCATION_START_MENU)
			{
				HANDLE h=CreateFile(it->second.targetPATH,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
				if (h==INVALID_HANDLE_VALUE)
				{
					LOG_MENU(LOG_NEW,L"Ignoring new: %s failed to read attributes",it->second.path);
					continue;
				}

				FILE_BASIC_INFO info;
				hours2=0;
				if (GetFileInformationByHandleEx(h,FileBasicInfo,&info,sizeof(info)))
					hours2=(int)((curTime-info.ChangeTime.QuadPart)/36000000000);
				CloseHandle(h);
				if (hours2<0)
				{
					LOG_MENU(LOG_NEW,L"Ignoring new: %s target change time too new - %d hours",it->second.path,hours2);
					continue; // the exe is too old
				}
				if (hours2>OLD_PROGRAMS_AGE)
				{
					LOG_MENU(LOG_NEW,L"Ignoring new: %s target change time too old - %d hours",it->second.path,hours2);
					continue; // the exe is too old
				}
				hours3=(int)((info.ChangeTime.QuadPart-installTime)/36000000000);
				if (hours3<INSTALL_GRACE_PERIOD)
				{
					LOG_MENU(LOG_NEW,L"Ignoring new: %s too soon after install - %d hours",it->second.path,hours3);
					continue; // too soon after install
				}

				if (regKeyLink.m_hKey && IsPathUsed(regKeyLink,it->second.PATH,it->second.createstamp,knownPaths,knownPathsCount,it->second.bMetroApp))
				{
					LOG_MENU(LOG_NEW,L"Ignoring new: %s shortcut used after it was created",it->second.path);
					continue;  // the shortcut was used after it was created
				}
				if (regKeyExe.m_hKey)
				{
					UpdateItemInfo(&it->second,INFO_LINK_APPID,true);
					CString appid=it->second.appid;
					appid.MakeUpper();
					if (IsPathUsed(regKeyExe,appid,it->second.createstamp,NULL,0,it->second.bMetroApp))
					{
						LOG_MENU(LOG_NEW,L"Ignoring new: %s exe used after the shortcut was created",it->second.path);
						continue; // the exe was used after the shortcut was created
					}
				}
				if (newestProgram.QuadPart<info.ChangeTime.QuadPart)
					newestProgram.QuadPart=info.ChangeTime.QuadPart;
			}

			if (it->second.location==LOCATION_METRO)
			{
				hours3=(int)((timestamp-installTime)/36000000000);
				if (hours3<INSTALL_GRACE_PERIOD)
				{
					LOG_MENU(LOG_NEW,L"Ignoring new: %s too soon after install - %d hours",it->second.path,hours3);
					continue; // too soon after install
				}
				CString appid=it->second.appid;
				appid.MakeUpper();
				if (regKeyExe.m_hKey && IsPathUsed(regKeyExe,appid,it->second.createstamp,NULL,0,it->second.bMetroApp))
				{
					LOG_MENU(LOG_NEW,L"Ignoring new: %s app id used after app was created",it->second.path);
					continue; // the exe was used after the shortcut was created
				}
				if (it->second.bLink && regKeyLink.m_hKey && IsPathUsed(regKeyLink,it->second.PATH,it->second.createstamp,knownPaths,knownPathsCount,it->second.bMetroApp))
				{
					LOG_MENU(LOG_NEW,L"Ignoring new: %s shortcut used after it was created",it->second.path);
					continue;  // the shortcut was used after it was created
				}
				if (newestApp.QuadPart<timestamp)
					newestApp.QuadPart=timestamp;
			}

			m_NewPrograms.push_back(&it->second);
			LOG_MENU(LOG_NEW,L"Accepting new: highlighting %s, created %d hours, target changed %d hours, since install %d hours, %I64X",it->second.path,hours1,hours2,hours3,timestamp);
		}
	}

	if (newestProgram.QuadPart || newestApp.QuadPart)
	{
		CRegKey regKey;
		ULONGLONG val1, val2;
		if (regKey.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu",KEY_READ)==ERROR_SUCCESS)
		{
			if (regKey.QueryQWORDValue(L"LastProgramsTime",val1)!=ERROR_SUCCESS)
				val1=0;
			if (regKey.QueryQWORDValue(L"LastAppsTime",val2)!=ERROR_SUCCESS)
				val2=0;
		}
		if (newestProgram.QuadPart)
		{
			LOG_MENU(LOG_NEW,L"LastProgramsTime: %I64X",val1);
			m_bHasNewPrograms[0]=true;
			m_bHasNewPrograms[1]=(val1<(ULONGLONG)newestProgram.QuadPart);
		}
		if (newestApp.QuadPart)
		{
			LOG_MENU(LOG_NEW,L"LastAppsTime: %I64X",val2);
			m_bHasNewApps[0]=true;
			m_bHasNewApps[1]=(val2<(ULONGLONG)newestApp.QuadPart);
		}
	}

	static const KNOWNFOLDERID *newProgramRoots[]={
		&FOLDERID_StartMenu,
		&FOLDERID_CommonStartMenu,
		&FOLDERID_Programs,
		&FOLDERID_CommonPrograms,
	};
	for (int i=0;i<_countof(newProgramRoots);i++)
	{
		CComPtr<IShellItem> pFolder;
		if (SUCCEEDED(ShGetKnownFolderItem(*newProgramRoots[i],&pFolder)))
		{
			CAbsolutePidl pidl;
			if (SUCCEEDED(SHGetIDListFromObject(pFolder,&pidl)))
				m_NewProgramRoots.push_back(GetItemInfo(pFolder,pidl,0));
		}
	}
	if (GetWinVersion()>=WIN_VER_WIN8)
	{
		wchar_t path[_MAX_PATH]=METRO_APP_ROOT;
		DoEnvironmentSubst(path,_countof(path));
		CAbsolutePidl pidl;
		pidl.Attach(ILCreateFromPath(path));
		CComPtr<IShellItem> pFolder;
		if (SUCCEEDED(SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pFolder)))
			m_NewProgramRoots.push_back(GetItemInfo(pFolder,pidl,0));
	}
}

bool CItemManager::IsNewProgram( PIDLIST_ABSOLUTE pidl, bool bFolder, bool bMetroApp )
{
	Assert(GetCurrentThreadId()==m_MainThreadId);
	if (m_NewPrograms.empty()) return false;
	if (!bMetroApp)
	{
		// check if the item is under the approved roots
		bool bValid=false;
		for (std::vector<const ItemInfo*>::const_iterator it=m_NewProgramRoots.begin();it!=m_NewProgramRoots.end();++it)
		{
			if (ILIsParent((*it)->GetPidl(),pidl,FALSE))
			{
				bValid=true;
				break;
			}
		}
		if (!bValid) return false;
	}
	for (std::vector<const ItemInfo*>::const_iterator it=m_NewPrograms.begin();it!=m_NewPrograms.end();++it)
	{
		if (bFolder && ILIsParent(pidl,(*it)->GetPidl(),FALSE))
			return true;
		if (!bFolder && ILIsEqual(pidl,(*it)->GetPidl()))
			return true;
	}
	return false;
}

void CItemManager::LoadOldItems( void )
{
	m_OldItemInfos.clear();
	CRegKey regItems;
	if (regItems.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu",KEY_READ)==ERROR_SUCCESS)
	{
		ULONG size=0;
		regItems.QueryBinaryValue(L"OldItems",NULL,&size);
		if (size>0 && (size%sizeof(OldItemInfo))==0)
		{
			m_OldItemInfos.resize(size/sizeof(OldItemInfo));
			regItems.QueryBinaryValue(L"OldItems",&m_OldItemInfos[0],&size);
		}
	}

	std::sort(m_OldItemInfos.begin(),m_OldItemInfos.end());
}

void CItemManager::RemoveNewItem( PIDLIST_ABSOLUTE pItem1, PIDLIST_ABSOLUTE pItem2, bool bFolder )
{
	std::vector<unsigned> hashes;
	{
		RWLock lock(this,false,RWLOCK_ITEMS);
		for (size_t i=0;i<m_NewPrograms.size();i++)
		{
			const ItemInfo *pInfo=m_NewPrograms[i];
			bool bFound=false;
			if (bFolder && (ILIsParent(pItem1,pInfo->GetPidl(),FALSE) || (pItem2 && ILIsParent(pItem2,pInfo->GetPidl(),FALSE))))
				bFound=true;
			if (!bFound && !bFolder && ILIsEqual(pItem1,pInfo->GetPidl()))
				bFound=true;
			if (bFound)
			{
				unsigned hash;
				if (pInfo->bMetroApp)
				{
					CString appid=pInfo->appid;
					appid.MakeUpper();
					hash=CalcFNVHash(appid);
				}
				else if (pInfo->bLink)
				{
					hash=CalcFNVHash(pInfo->PATH);
				}
				else continue;
				m_NewPrograms.erase(m_NewPrograms.begin()+i);
				hashes.push_back(hash);
				i--;
			}
		}
	}
	AddOldItems(hashes);
}

void CItemManager::RemoveNewItems( bool bPrograms, bool bMetro )
{
	std::vector<unsigned> hashes;
	{
		RWLock lock(this,false,RWLOCK_ITEMS);
		for (size_t i=0;i<m_NewPrograms.size();i++)
		{
			const ItemInfo *pInfo=m_NewPrograms[i];
			if (pInfo->location==LOCATION_METRO && !bMetro)
				continue;
			if (pInfo->location!=LOCATION_METRO && !bPrograms)
				continue;
			unsigned hash;
			if (pInfo->bMetroApp)
			{
				CString appid=pInfo->appid;
				appid.MakeUpper();
				hash=CalcFNVHash(appid);
			}
			else if (pInfo->bLink)
			{
				hash=CalcFNVHash(pInfo->PATH);
			}
			else continue;
			m_NewPrograms.erase(m_NewPrograms.begin()+i);
			hashes.push_back(hash);
			i--;
		}
	}
	AddOldItems(hashes);
	if (bMetro)
		m_bHasNewApps[0]=m_bHasNewApps[1]=false;
	if (bPrograms)
		m_bHasNewPrograms[0]=m_bHasNewPrograms[1]=false;
}

void CItemManager::AddOldItems( const std::vector<unsigned> &hashes )
{
	// purge items older than 48 hours
	LONGLONG timestamp;
	GetSystemTimeAsFileTime((FILETIME*)&timestamp);
	timestamp-=36000000000ll*48;
	for (size_t i=0;i<m_OldItemInfos.size();i++)
	{
		if (CompareFileTime((FILETIME*)&timestamp,&m_OldItemInfos[i].timestamp)>0)
		{
			m_OldItemInfos.erase(m_OldItemInfos.begin()+i);
			i--;
		}
	}

	for (std::vector<unsigned>::const_iterator it=hashes.begin();it!=hashes.end();++it)
	{
		OldItemInfo key={*it};
		GetSystemTimeAsFileTime(&key.timestamp);
		std::vector<OldItemInfo>::iterator it2=std::lower_bound(m_OldItemInfos.begin(),m_OldItemInfos.end(),key);
		if (it2!=m_OldItemInfos.end() && it2->hash==*it)
			it2->timestamp=key.timestamp;
		else
			m_OldItemInfos.insert(it2,key);
	}

	CRegKey regItems;
	if (regItems.Open(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu")!=ERROR_SUCCESS)
		regItems.Create(HKEY_CURRENT_USER,L"Software\\OpenShell\\StartMenu");

	if (m_OldItemInfos.empty())
		regItems.SetBinaryValue(L"OldItems",NULL,0);
	else
		regItems.SetBinaryValue(L"OldItems",&m_OldItemInfos[0],ULONG((size_t)m_OldItemInfos.size()*sizeof(OldItemInfo)));
}

CString GetPropertyStoreString( IPropertyStore *pStore, REFPROPERTYKEY key )
{
	PROPVARIANT val;
	PropVariantInit(&val);
	CString res;
	if (SUCCEEDED(pStore->GetValue(key,&val)))
	{
		if (val.vt==VT_LPWSTR || val.vt==VT_BSTR)
			res=val.pwszVal;
		else if (val.vt==VT_LPSTR)
			res=val.pszVal;
	}
	PropVariantClear(&val);
	return res;
}

// doesn't require the lock to be held
void CItemManager::RefreshItemInfo( ItemInfo *pInfo, int refreshFlags, IShellItem *pItem0, bool bHasWriteLock )
{
	ItemInfo newInfo;

	{
		// get info from pInfo
		RWLock lock(this,false,bHasWriteLock?RWLOCK_COUNT:RWLOCK_ITEMS);
		newInfo=*pInfo;
	}

	CComPtr<IShellItem> pItem;
	CComPtr<IShellItem> pAppItem;
	bool bStartScreen=
#ifndef STARTSCREEN_WIN7
		GetWinVersion()>=WIN_VER_WIN8 &&
#endif
		(_wcsicmp(PathFindFileName(newInfo.PATH),STARTSCREEN_COMMAND)==0);
	bool bValidateIcons=!bStartScreen; // hack - don't mark the icon as valid, so we have to load it next time
	{
		// do the real work

		int pInfo=0; // shadow the pInfo parameter while in this block
		if ((refreshFlags&INFO_DATA) || !(newInfo.bIconOnly || newInfo.bMetroLink))
		{
			if (pItem0)
				pItem=pItem0;
			else
			{
				const CAbsolutePidl &pidl=newInfo.newPidl?newInfo.newPidl:newInfo.pidl;
				if (pidl)
				{
					if (FAILED(SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pItem)))
						return;
				}
			}
		}

		if (refreshFlags&INFO_DATA)
		{
			Assert(!newInfo.bIconOnly);

			// these two are always updated even if INFO_LINK is not requested
			newInfo.bLink=false;
			newInfo.bNoPin=false;
			newInfo.bNoNew=false;
			newInfo.bExplicitAppId=false;
			newInfo.targetPATH.Empty();
			newInfo.targetPidl.Clear();
			if (refreshFlags&INFO_LINK_APPID)
			{
				newInfo.appid.Empty();
			}

			if (refreshFlags&INFO_METRO)
			{
				newInfo.bMetroLink=false;
				newInfo.bMetroApp=false;
				newInfo.bProtectedLink=false;
				newInfo.metroName.Empty();
				newInfo.iconPath.Empty();
				newInfo.iconColor=0;
			}

			// refresh link and metro
			SFGAOF flags=0;
			if (pItem && SUCCEEDED(pItem->GetAttributes(SFGAO_LINK|SFGAO_FILESYSTEM,&flags)))
			{
				CComQIPtr<IPropertyStore> pStore;
				if (flags&SFGAO_LINK)
				{
					// get link properties
					CComPtr<IShellLink> pLink;
					if (SUCCEEDED(pItem->BindToHandler(NULL,BHID_SFUIObject,IID_IShellLink,(void**)&pLink)))
					{
						newInfo.bLink=true;
						pStore=pLink;
#ifdef _DEBUG
						LOG_MENU(LOG_OPEN, L"Link: %s", newInfo.path);
						LOG_MENU(LOG_OPEN, L"Link property store:");
						LogPropertyStore(LOG_OPEN, pStore);
#endif
						if (SUCCEEDED(pLink->GetIDList(&newInfo.targetPidl)))
						{
							wchar_t path[_MAX_PATH];
							if (SUCCEEDED(SHGetPathFromIDList(newInfo.targetPidl,path)))
							{
								CharUpper(path);
								newInfo.targetPATH=path;
							}

							CComPtr<IShellItem> target;
							if (SUCCEEDED(SHCreateItemFromIDList(newInfo.targetPidl, IID_PPV_ARGS(&target))))
							{
								CComPtr<IPropertyStore> store;
								if (SUCCEEDED(target->BindToHandler(nullptr, BHID_PropertyStore, IID_PPV_ARGS(&store))))
								{
#ifdef _DEBUG
									LOG_MENU(LOG_OPEN, L"Target property store:");
									LogPropertyStore(LOG_OPEN, store);
#endif
									PROPVARIANT val;
									PropVariantInit(&val);
									if (SUCCEEDED(store->GetValue(PKEY_MetroAppLauncher, &val)) && (val.vt == VT_I4 || val.vt == VT_UI4) && val.intVal)
									{
										newInfo.bLink = false;
										pItem = std::move(target);
										pStore = store;
									}
									PropVariantClear(&val);
								}
							}
						}
					}
				}
				else if (!(flags&SFGAO_FILESYSTEM))
				{
					newInfo.bLink=false;
					pItem->BindToHandler(NULL,BHID_PropertyStore,IID_IPropertyStore,(void**)&pStore);
				}
				if (pStore)
				{
					if (GetWinVersion()>=WIN_VER_WIN7)
					{
						newInfo.appid=GetPropertyStoreString(pStore,PKEY_AppUserModel_ID);
						newInfo.bExplicitAppId=!newInfo.appid.IsEmpty();
					}
					if (!newInfo.appid.IsEmpty() && (refreshFlags&INFO_METRO) && (GetWinVersion()<=WIN_VER_WIN8 || _wcsicmp(newInfo.appid,SEARCH_APP_ID)!=0))
					{
						PROPVARIANT val;
						PropVariantInit(&val);
						if (SUCCEEDED(pStore->GetValue(PKEY_MetroAppLauncher,&val)) && (val.vt==VT_I4 || val.vt==VT_UI4) && val.intVal)
						{
							// get Metro properties
							PropVariantClear(&val);
							newInfo.bMetroLink=newInfo.bLink;
							newInfo.bMetroApp=!newInfo.bLink;
							newInfo.bProtectedLink=false;
							if (newInfo.bMetroApp)
							{
								pAppItem=pItem;
								newInfo.packagePath=GetPropertyStoreString(pStore,PKEY_MetroPackagePath);
								if (!newInfo.packagePath.IsEmpty())
								{
									FILETIME writeTime={0}, createTime={0};
									if (MenuGetFileTimestamp(newInfo.packagePath,&writeTime,&createTime))
									{
										newInfo.writestamp=writeTime;
										newInfo.createstamp=createTime;
									}
								}
							}
							if (newInfo.bMetroLink && wcsncmp(newInfo.PATH,m_RootCommonPrograms,m_RootCommonPrograms.GetLength())==0)
							{
								const wchar_t *str=newInfo.PATH;
								newInfo.bProtectedLink=m_RootCommonPrograms.GetLength()==(PathFindFileName(str)-str);
							}
							if (SUCCEEDED(pStore->GetValue(PKEY_MetroIconColor,&val)) && (val.vt==VT_I4 || val.vt==VT_UI4))
								newInfo.iconColor=val.intVal;
							if (pAppItem || SUCCEEDED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder2,0,newInfo.appid,IID_IShellItem,(void**)&pAppItem)))
							{
								CComString pName;
								if (SUCCEEDED(pAppItem->GetDisplayName(SIGDN_NORMALDISPLAY,&pName)))
								{
									newInfo.metroName=pName;
								}
							}
						}
						PropVariantClear(&val);
					}
					if (!newInfo.bMetroLink && !newInfo.bMetroApp)
					{
						PROPVARIANT val;
						PropVariantInit(&val);
						if (SUCCEEDED(pStore->GetValue(PKEY_AppUserModel_PreventPinning,&val)) && val.vt==VT_BOOL && val.boolVal)
							newInfo.bNoPin=true;
						PropVariantClear(&val);
						if (SUCCEEDED(pStore->GetValue(PKEY_AppUserModel_ExcludeFromShowInNewInstall,&val)) && val.vt==VT_BOOL && val.boolVal)
							newInfo.bNoNew=true;
						PropVariantClear(&val);
					}
				}

				if (newInfo.bLink && newInfo.appid.IsEmpty() && (refreshFlags&INFO_LINK_APPID))
				{
					Assert(GetCurrentThreadId()==m_MainThreadId);
					if (!g_pAppResolver)
						CreateAppResolver();
					if (g_pAppResolver)
					{
						CComString pAppId;
						if (SUCCEEDED(g_pAppResolver->GetAppIDForShortcut(pItem,&pAppId)))
							newInfo.appid=pAppId;
					}
				}
			}
		}
		if (!newInfo.bIconOnly)
		{
			if (!newInfo.bLink)
			{
				newInfo.targetPidl.Clear();
				newInfo.targetPATH.Empty();
				newInfo.iconPath.Empty();
				newInfo.bNoPin=newInfo.bNoNew=false;
				if (!newInfo.bMetroApp)
				{
					newInfo.bExplicitAppId=false;
					newInfo.appid.Empty();
					newInfo.packagePath.Empty();
				}
			}
			else if (newInfo.bMetroLink)
			{
				newInfo.targetPidl.Clear();
				newInfo.targetPATH.Empty();
			}
		}

		if (refreshFlags&INFO_ICON)
		{
			// load icons
			if (newInfo.bIconOnly)
			{
				LoadCustomIcon(newInfo.iconPath,newInfo.iconIndex,refreshFlags&INFO_ICON,newInfo.smallIcon,newInfo.largeIcon,newInfo.extraLargeIcon,newInfo.bTemp);
			}
			else if (newInfo.bMetroLink || newInfo.bMetroApp)
			{
				if (pAppItem || SUCCEEDED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder2,0,newInfo.appid,IID_IShellItem,(void**)&pAppItem)))
				{
					int iconFlags=refreshFlags&INFO_ICON;
					LoadMetroIcon(pAppItem,iconFlags,newInfo.smallIcon,newInfo.largeIcon,newInfo.extraLargeIcon,&newInfo.iconColor);
					if (iconFlags)
						LoadShellIcon(pItem?pItem:pAppItem,iconFlags,newInfo.smallIcon,newInfo.largeIcon,newInfo.extraLargeIcon,&newInfo.iconColor);
				}
			}
			else if (_wcsicmp(PathFindExtension(newInfo.path),L".settingcontent-ms")==0)
			{
				wchar_t iconPath[_MAX_PATH]=L"%windir%\\ImmersiveControlPanel\\systemsettings.exe";
				DoEnvironmentSubst(iconPath,_countof(iconPath));
				newInfo.iconPath=iconPath;
				newInfo.iconIndex=-10;
				LoadCustomIcon(newInfo.iconPath,newInfo.iconIndex,refreshFlags&INFO_ICON,newInfo.smallIcon,newInfo.largeIcon,newInfo.extraLargeIcon,false);
			}
			else
			{
				LoadShellIcon(pItem,(refreshFlags&INFO_ICON)|(bStartScreen?INFO_STARTSCREEN_ICON:0),newInfo.smallIcon,newInfo.largeIcon,newInfo.extraLargeIcon,NULL);
			}
		}
	}

	{
		// store info in pInfo
		RWLock lock(this,true,bHasWriteLock?RWLOCK_COUNT:RWLOCK_ITEMS);

		if (refreshFlags&INFO_DATA)
		{
			pInfo->bLink=newInfo.bLink;
			pInfo->bMetroLink=newInfo.bMetroLink;
			pInfo->bMetroApp=newInfo.bMetroApp;
			pInfo->bProtectedLink=newInfo.bProtectedLink;
			pInfo->bNoPin=newInfo.bNoPin;
			pInfo->bNoNew=newInfo.bNoNew;
			pInfo->bExplicitAppId=newInfo.bExplicitAppId;
			pInfo->targetPidl.Swap(newInfo.targetPidl);
			pInfo->targetPATH=newInfo.targetPATH;
			pInfo->packagePath=newInfo.packagePath;
			if ((refreshFlags&INFO_LINK_APPID) || !newInfo.appid.IsEmpty())
				pInfo->appid=newInfo.appid;
			if (!pInfo->appid.IsEmpty())
				refreshFlags|=INFO_LINK_APPID; // appid is valid, no need to resolve
			pInfo->metroName=newInfo.metroName;
			pInfo->iconPath=newInfo.iconPath;
			pInfo->iconColor=newInfo.iconColor;
			if (pInfo->bMetroApp)
			{
				pInfo->writestamp=newInfo.writestamp;
				pInfo->createstamp=newInfo.createstamp;
			}
		}

		if (refreshFlags&INFO_SMALL_ICON)
			pInfo->smallIcon=newInfo.smallIcon;
		if (refreshFlags&INFO_LARGE_ICON)
			pInfo->largeIcon=newInfo.largeIcon;
		if (refreshFlags&INFO_EXTRA_LARGE_ICON)
			pInfo->extraLargeIcon=newInfo.extraLargeIcon;
		if (bValidateIcons)
			pInfo->validFlags|=refreshFlags&(INFO_DATA|INFO_ICON);
		else
			pInfo->validFlags|=refreshFlags&INFO_DATA;
		pInfo->refreshFlags&=~refreshFlags;
	}
}

void CItemManager::RefreshInfos( void )
{
	Assert(GetCurrentThreadId()==m_MainThreadId);
	RWLock lock(this,true,RWLOCK_ITEMS);
	for (std::multimap<unsigned int,ItemInfo>::iterator it=m_ItemInfos.begin();it!=m_ItemInfos.end();++it)
	{
		if (it->second.newPidl)
		{
			it->second.pidl.Swap(it->second.newPidl);
			it->second.newPidl.Clear();
			it->second.validFlags=0;
		}
	}
}

HBITMAP CItemManager::BitmapFromIcon( HICON hIcon, int iconSize, bool bDestroyIcon )
{
	if (!hIcon) return NULL;
	LoadIconData &data=GetLoadIconData();
	if (!data.m_pFactory)
	{
		if (bDestroyIcon)
			DestroyIcon(hIcon);
		return NULL;
	}

	CComPtr<IWICBitmap> pBitmap;
	HRESULT hr=data.m_pFactory->CreateBitmapFromHICON(hIcon,&pBitmap);
	if (bDestroyIcon)
		DestroyIcon(hIcon);
	if (FAILED(hr))
		return NULL;

	CComPtr<IWICFormatConverter> pConverter;
	if (FAILED(data.m_pFactory->CreateFormatConverter(&pConverter)))
		return NULL;

	UINT width=0, height=0;
	if (SUCCEEDED(pBitmap->GetSize(&width,&height)) && width==iconSize && height==iconSize)
	{
		pConverter->Initialize(pBitmap,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,NULL,0,WICBitmapPaletteTypeMedianCut);
	}
	else
	{
		CComPtr<IWICBitmapScaler> pScaler;
		if (FAILED(data.m_pFactory->CreateBitmapScaler(&pScaler)))
			return NULL;
		pScaler->Initialize(pBitmap,iconSize,iconSize,WICBitmapInterpolationModeFant);
		pConverter->Initialize(pScaler,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,NULL,0,WICBitmapPaletteTypeMedianCut);
	}

	BITMAPINFO bi={0};
	bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth=iconSize;
	bi.bmiHeader.biHeight=-iconSize;
	bi.bmiHeader.biPlanes=1;
	bi.bmiHeader.biBitCount=32;
	HDC hdc=CreateCompatibleDC(NULL);
	BYTE *bits;
	HBITMAP bmp=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,(void**)&bits,NULL,0);
	DeleteDC(hdc);
	if (!bmp) return NULL;
	hr=pConverter->CopyPixels(NULL,iconSize*4,iconSize*iconSize*4,bits);
	if (FAILED(hr))
	{
		DeleteObject(bmp);
		return NULL;
	}
	return bmp;
}

HICON CItemManager::LoadShellIcon( int index, int iconSize )
{
	LoadIconData &data=GetLoadIconData();
	int sizeIndex=-1;
	for (int i=0;i<_countof(data.m_IconSizes);i++)
	{
		if (data.m_IconSizes[i]==iconSize)
		{
			sizeIndex=i;
			break;
		}
	}
	if (sizeIndex==-1) return NULL;
	int listType=0;
	for (std::vector<std::pair<int,int>>::const_iterator it=m_ListSizes.begin();it!=m_ListSizes.end();++it)
	{
		listType=it->second;
		if (it->first>=iconSize)
			break;
	}
	CComPtr<IImageList> pImageList;
	if (FAILED(SHGetImageList(listType,IID_IImageList,(void**)&pImageList)))
		return NULL;
	HICON hIcon;

	if (data.m_pTempLists[sizeIndex])
	{
		if (SUCCEEDED(data.m_pTempLists[sizeIndex]->ReplaceFromImageList(0,pImageList,index,NULL,0)) && SUCCEEDED(data.m_pTempLists[sizeIndex]->GetIcon(0,ILD_TRANSPARENT,&hIcon)))
		{
			AddTrackedIcon(hIcon);
			return hIcon;
		}
	}
	if (FAILED(pImageList->GetIcon(index,ILD_TRANSPARENT,&hIcon)))
		return NULL;
	AddTrackedIcon(hIcon);
	return hIcon;
}

HICON CItemManager::LoadShellIcon( int iconSize, IExtractIcon *pExtractW, const wchar_t *location, IExtractIconA *pExtractA, const char *locationA, int index )
{
	HICON hIcon=NULL, hIcon2=NULL;
	HRESULT hr;
	if (iconSize<=GetSystemMetrics(SM_CXSMICON))
	{
		// small icon is closer
		if (pExtractW)
			hr=pExtractW->Extract(location,index,&hIcon2,&hIcon,MAKELONG(iconSize,iconSize));
		else
			hr=pExtractA->Extract(locationA,index,&hIcon2,&hIcon,MAKELONG(iconSize,iconSize));
	}
	else
	{
		// large icon is closer
		if (pExtractW)
			hr=pExtractW->Extract(location,index,&hIcon,&hIcon2,MAKELONG(iconSize,iconSize));
		else
			hr=pExtractA->Extract(locationA,index,&hIcon,&hIcon2,MAKELONG(iconSize,iconSize));
	}
	if (FAILED(hr)) return NULL;

	if (hIcon2) DestroyIcon(hIcon2);
	return hIcon;
}

void CItemManager::LoadShellIcon( IShellItem *pItem, int refreshFlags, const IconInfo *&smallIcon, const IconInfo *&largeIcon, const IconInfo *&extraLargeIcon, const DWORD *pMetroColor )
{
	if (!pItem)
		return;

	wchar_t location[_MAX_PATH];
	int index=0;
	if (refreshFlags&INFO_STARTSCREEN_ICON)
	{
		CComPtr<IShellLink> pLink;
		if (SUCCEEDED(pItem->BindToHandler(NULL,BHID_SFUIObject,IID_IShellLink,(void**)&pLink)))
		{
			if (SUCCEEDED(pLink->GetIconLocation(location,_countof(location),&index)) && (!location[0] || _wcsicmp(PathFindFileName(location),L"StartScreen.exe")==0))
			{
				unsigned int hash=CalcFNVHash(STARTSCREEN_COMMAND);
				HBITMAP hSmallBitmap=NULL, hLargeBitmap=NULL, hExtraLargeBitmap=NULL;
				if (refreshFlags&INFO_SMALL_ICON)
				{
					hSmallBitmap=GetStartScreenIcon(SMALL_ICON_SIZE);
				}

				if (refreshFlags&INFO_LARGE_ICON)
				{
					hLargeBitmap=GetStartScreenIcon(LARGE_ICON_SIZE);
				}

				if (refreshFlags&INFO_EXTRA_LARGE_ICON)
				{
					hExtraLargeBitmap=GetStartScreenIcon(EXTRA_LARGE_ICON_SIZE);
				}

				StoreInCache(hash,NULL,hSmallBitmap,hLargeBitmap,hExtraLargeBitmap,refreshFlags,smallIcon,largeIcon,extraLargeIcon,false,false);
				return;
			}
		}
	}

	// get the IExtractIcon object
	CComPtr<IExtractIcon> pExtractW;
	CComPtr<IExtractIconA> pExtractA;

	bool bNotFileName;
	bool bTransient=false;
	char locationA[_MAX_PATH];

	// get the icon location
	if (SUCCEEDED(pItem->BindToHandler(NULL,BHID_SFUIObject,IID_IExtractIconW,(void**)&pExtractW)))
	{
		UINT iconFlags=0;
		if (pExtractW->GetIconLocation(0,location,_countof(location),&index,&iconFlags)!=S_OK)
			return;

		bNotFileName=(iconFlags&GIL_NOTFILENAME)!=0;
		bTransient=(iconFlags&GIL_DONTCACHE)!=0;
	}
	else if (SUCCEEDED(pItem->BindToHandler(NULL,BHID_SFUIObject,IID_IExtractIconA,(void**)&pExtractA)))
	{
		UINT iconFlags=0;
		if (pExtractA->GetIconLocation(0,locationA,_countof(locationA),&index,&iconFlags)!=S_OK)
			return;

		MbsToWcs(location,_countof(location),locationA);
		bNotFileName=(iconFlags&GIL_NOTFILENAME)!=0;
		bTransient=(iconFlags&GIL_DONTCACHE)!=0;
	}
	else
		return;

	unsigned int hash;
	if (bTransient)
		hash=m_TransientHash++;
	else if (location[0])
		hash=CalcFNVHash(location,CalcFNVHash(&index,4));
	else
	{
		// if the location is blank, use the parsing name for the hash
		CComString pName;
		pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName);
		hash=CalcFNVHash(pName,CalcFNVHash(&index,4));
	}

	if (pMetroColor)
		hash=CalcFNVHash(pMetroColor,4,hash);

	if (!bTransient)
		FindInCache(hash,refreshFlags,smallIcon,largeIcon,extraLargeIcon);
	if (!refreshFlags) return;

	// extract icon
	HBITMAP hSmallBitmap=NULL, hLargeBitmap=NULL, hExtraLargeBitmap=NULL;

	wchar_t metroLocation[_MAX_PATH];
	Strcpy(metroLocation,_countof(metroLocation),location);
	if (pMetroColor && ParseMetroBitmapLocation(metroLocation))
	{
		if (refreshFlags&INFO_SMALL_ICON)
		{
			hSmallBitmap=LoadMetroBitmap(metroLocation,SMALL_ICON_SIZE,*pMetroColor);
		}
		if (refreshFlags&INFO_LARGE_ICON)
		{
			hLargeBitmap=LoadMetroBitmap(metroLocation,LARGE_ICON_SIZE,*pMetroColor);
		}
		if (refreshFlags&INFO_EXTRA_LARGE_ICON)
		{
			hExtraLargeBitmap=LoadMetroBitmap(metroLocation,EXTRA_LARGE_ICON_SIZE,*pMetroColor);
		}
		if (hSmallBitmap || hLargeBitmap || hExtraLargeBitmap)
		{
			metroLocation[1]='#';
			StoreInCache(hash,metroLocation,hSmallBitmap,hLargeBitmap,hExtraLargeBitmap,refreshFlags,smallIcon,largeIcon,extraLargeIcon,bTransient,true);
			return;
		}
	}
	
	Strcpy(metroLocation,_countof(metroLocation),location);
	if (pMetroColor && GetWinVersion()>=WIN_VER_WIN10 && ParseMetroBitmapLocation2(metroLocation))
	{
		if (refreshFlags&INFO_SMALL_ICON)
		{
			hSmallBitmap=LoadMetroBitmap2(metroLocation,SMALL_ICON_SIZE,*pMetroColor);
		}
		if (refreshFlags&INFO_LARGE_ICON)
		{
			hLargeBitmap=LoadMetroBitmap2(metroLocation,LARGE_ICON_SIZE,*pMetroColor);
		}
		if (refreshFlags&INFO_EXTRA_LARGE_ICON)
		{
			hExtraLargeBitmap=LoadMetroBitmap2(metroLocation,EXTRA_LARGE_ICON_SIZE,*pMetroColor);
		}
		if (hSmallBitmap || hLargeBitmap || hExtraLargeBitmap)
		{
			metroLocation[1]='#';
			StoreInCache(hash,metroLocation,hSmallBitmap,hLargeBitmap,hExtraLargeBitmap,refreshFlags,smallIcon,largeIcon,extraLargeIcon,bTransient,true);
			return;
		}
	}

	if (GetWinVersion()>=WIN_VER_WIN10)
		pMetroColor=NULL;

	int smallIconSize=SMALL_ICON_SIZE;
	int largeIconSize=LARGE_ICON_SIZE;
	int extraLargeIconSize=EXTRA_LARGE_ICON_SIZE;
	HICON hSmallIcon=NULL, hLargeIcon=NULL, hExtraLargeIcon=NULL;
	if (bNotFileName)
	{
		CAbsolutePidl pidl;
		SHFILEINFO info;
		if (SUCCEEDED(SHGetIDListFromObject(pItem,&pidl)) && SHGetFileInfo((const wchar_t*)(PIDLIST_ABSOLUTE)pidl,0,&info,sizeof(info),SHGFI_PIDL|SHGFI_SYSICONINDEX))
		{
			if (refreshFlags&INFO_SMALL_ICON)
				hSmallIcon=LoadShellIcon(info.iIcon,smallIconSize);
			if (refreshFlags&INFO_LARGE_ICON)
				hLargeIcon=LoadShellIcon(info.iIcon,largeIconSize);
			if (refreshFlags&INFO_EXTRA_LARGE_ICON)
				hExtraLargeIcon=LoadShellIcon(info.iIcon,extraLargeIconSize);
		}
		else
		{
			// fall back to the extractor
			if (refreshFlags&INFO_SMALL_ICON)
				hSmallIcon=LoadShellIcon(smallIconSize,pExtractW,location,pExtractA,locationA,index);
			if (refreshFlags&INFO_LARGE_ICON)
				hLargeIcon=LoadShellIcon(largeIconSize,pExtractW,location,pExtractA,locationA,index);
			if (refreshFlags&INFO_EXTRA_LARGE_ICON)
				hExtraLargeIcon=LoadShellIcon(extraLargeIconSize,pExtractW,location,pExtractA,locationA,index);
		}
	}
	else
	{
		DoEnvironmentSubst(location,_countof(location));
		if (refreshFlags&INFO_SMALL_ICON)
			hSmallIcon=ShExtractIcon(location,index==-1?0:index,smallIconSize);
		if (refreshFlags&INFO_LARGE_ICON)
			hLargeIcon=ShExtractIcon(location,index==-1?0:index,largeIconSize);
		if (refreshFlags&INFO_EXTRA_LARGE_ICON)
			hExtraLargeIcon=ShExtractIcon(location,index==-1?0:index,extraLargeIconSize);
	}
	if (hSmallIcon)
	{
		if (pMetroColor)
			hSmallBitmap=BitmapFromMetroIcon(hSmallIcon,SMALL_ICON_SIZE,smallIconSize,*pMetroColor);
		else
			hSmallBitmap=BitmapFromIcon(hSmallIcon,smallIconSize);
	}
	if (hLargeIcon)
	{
		if (pMetroColor)
			hLargeBitmap=BitmapFromMetroIcon(hLargeIcon,LARGE_ICON_SIZE,largeIconSize,*pMetroColor);
		else
			hLargeBitmap=BitmapFromIcon(hLargeIcon,largeIconSize);
	}
	if (hExtraLargeIcon)
	{
		if (pMetroColor)
			hExtraLargeBitmap=BitmapFromMetroIcon(hExtraLargeIcon,EXTRA_LARGE_ICON_SIZE,extraLargeIconSize,*pMetroColor);
		else
			hExtraLargeBitmap=BitmapFromIcon(hExtraLargeIcon,extraLargeIconSize);
	}
	if (pMetroColor)
		location[1]='#';
	StoreInCache(hash,bNotFileName?NULL:location,hSmallBitmap,hLargeBitmap,hExtraLargeBitmap,refreshFlags,smallIcon,largeIcon,extraLargeIcon,bTransient,false);
}

static bool SetResContextTargetSize( IResourceContext *pResContext, int size )
{
	if (GetWinVersion()>=WIN_VER_WIN10)
	{
		if (SUCCEEDED(pResContext->SetTargetSize(size)))
			return true;
	}
	RESOURCE_SCALE scale;
	if (size<=24)
		scale=RES_SCALE_80;
	else if (size<=30)
		scale=RES_SCALE_100;
	else if (size<=42)
		scale=RES_SCALE_140;
	else
		scale=RES_SCALE_180;
	return SUCCEEDED(pResContext->SetScale(scale));
}

void CItemManager::LoadMetroIcon( IShellItem *pItem, int &refreshFlags, const IconInfo *&smallIcon, const IconInfo *&largeIcon, const IconInfo *&extraLargeIcon, const DWORD *pMetroColor )
{
	unsigned int hash;
	{
		CComString pName;
		pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName);
		hash=CalcFNVHash(pName);
	}

	hash=CalcFNVHash(pMetroColor,4,hash);

	FindInCache(hash,refreshFlags,smallIcon,largeIcon,extraLargeIcon);
	if (!refreshFlags) return;

	// extract icon
	HBITMAP hSmallBitmap=NULL, hLargeBitmap=NULL, hExtraLargeBitmap=NULL;

	CComPtr<IPropertyStore> pStore;
	pItem->BindToHandler(NULL,BHID_PropertyStore,IID_IPropertyStore,(void**)&pStore);
	if (!pStore) return;

	CString packageName=GetPropertyStoreString(pStore,PKEY_MetroPackageName);
	if (packageName.IsEmpty()) return;
	CString iconName=GetPropertyStoreString(pStore,PKEY_MetroIcon);
	if (iconName.IsEmpty()) return;

	CComPtr<IResourceManager> pResManager;
	if (FAILED(pResManager.CoCreateInstance(CLSID_ResourceManager)))
		return;
	if (FAILED(pResManager->InitializeForPackage(packageName)))
		return;
	CComPtr<IResourceMap> pResMap;
	if (FAILED(pResManager->GetMainResourceMap(IID_PPV_ARGS(&pResMap))))
		return;
	CComPtr<IResourceContext> pResContext;
	if (FAILED(pResManager->GetDefaultContext(IID_PPV_ARGS(&pResContext))))
		return;
	int iconFlags=0;
	if ((refreshFlags&INFO_SMALL_ICON) && SetResContextTargetSize(pResContext,SMALL_ICON_SIZE))
	{
		CComString pLocation;
		if (SUCCEEDED(pResMap->GetFilePath(iconName,&pLocation)))
		{
			hSmallBitmap=LoadMetroBitmap0(pLocation,SMALL_ICON_SIZE,*pMetroColor);
			refreshFlags&=~INFO_SMALL_ICON;
			StoreInCache(hash,L"",hSmallBitmap,NULL,NULL,INFO_SMALL_ICON,smallIcon,largeIcon,extraLargeIcon,false,true);
		}
	}
	if ((refreshFlags&INFO_LARGE_ICON) && SetResContextTargetSize(pResContext,LARGE_ICON_SIZE))
	{
		CComString pLocation;
		if (SUCCEEDED(pResMap->GetFilePath(iconName,&pLocation)))
		{
			hLargeBitmap=LoadMetroBitmap0(pLocation,LARGE_ICON_SIZE,*pMetroColor);
			refreshFlags&=~INFO_LARGE_ICON;
			StoreInCache(hash,L"",NULL,hLargeBitmap,NULL,INFO_LARGE_ICON,smallIcon,largeIcon,extraLargeIcon,false,true);
		}
	}
	if ((refreshFlags&INFO_EXTRA_LARGE_ICON) && SetResContextTargetSize(pResContext,EXTRA_LARGE_ICON_SIZE))
	{
		CComString pLocation;
		if (SUCCEEDED(pResMap->GetFilePath(iconName,&pLocation)))
		{
			hExtraLargeBitmap=LoadMetroBitmap0(pLocation,EXTRA_LARGE_ICON_SIZE,*pMetroColor);
			refreshFlags&=~INFO_EXTRA_LARGE_ICON;
			StoreInCache(hash,L"",NULL,NULL,hExtraLargeBitmap,INFO_EXTRA_LARGE_ICON,smallIcon,largeIcon,extraLargeIcon,false,true);
		}
	}
}

void CItemManager::FindInCache( unsigned int hash, int &refreshFlags, const IconInfo *&smallIcon, const IconInfo *&largeIcon, const IconInfo *&extraLargeIcon )
{
	// look in the cache
	RWLock lock(this,false,RWLOCK_ICONS);
	std::multimap<unsigned int,IconInfo>::iterator it=m_IconInfos.find(hash);
	for (;it!=m_IconInfos.end() && it->first==hash;++it)
	{
		if ((refreshFlags&INFO_SMALL_ICON) && it->second.sizeType==ICON_SIZE_TYPE_SMALL)
		{
			smallIcon=&it->second;
			refreshFlags&=~INFO_SMALL_ICON;
		}
		if ((refreshFlags&INFO_LARGE_ICON) && it->second.sizeType==ICON_SIZE_TYPE_LARGE)
		{
			largeIcon=&it->second;
			refreshFlags&=~INFO_LARGE_ICON;
		}
		if ((refreshFlags&INFO_EXTRA_LARGE_ICON) && it->second.sizeType==ICON_SIZE_TYPE_EXTRA_LARGE)
		{
			extraLargeIcon=&it->second;
			refreshFlags&=~INFO_EXTRA_LARGE_ICON;
		}
	}
}

void CItemManager::StoreInCache( unsigned int hash, const wchar_t *path, HBITMAP hSmallBitmap, HBITMAP hLargeBitmap, HBITMAP hExtraLargeBitmap, int refreshFlags, const IconInfo *&smallIcon, const IconInfo *&largeIcon, const IconInfo *&extraLargeIcon, bool bTemp, bool bMetro )
{
	RWLock lock(this,true,RWLOCK_ICONS);
	std::multimap<unsigned int,IconInfo>::iterator it=m_IconInfos.find(hash);
	for (;it!=m_IconInfos.end() && it->first==hash;++it)
	{
		if ((refreshFlags&INFO_SMALL_ICON) && it->second.sizeType==ICON_SIZE_TYPE_SMALL)
		{
			if (hSmallBitmap)
			{
				HBITMAP old=it->second.bitmap;
				it->second.bitmap=hSmallBitmap;
				if (old) m_OldBitmaps.push_back(old);
				hSmallBitmap=NULL;
			}
			smallIcon=&it->second;
			refreshFlags&=~INFO_SMALL_ICON;
		}
		if ((refreshFlags&INFO_LARGE_ICON) && it->second.sizeType==ICON_SIZE_TYPE_LARGE)
		{
			if (hLargeBitmap)
			{
				HBITMAP old=it->second.bitmap;
				it->second.bitmap=hLargeBitmap;
				if (old) m_OldBitmaps.push_back(old);
				hLargeBitmap=NULL;
			}
			largeIcon=&it->second;
			refreshFlags&=~INFO_LARGE_ICON;
		}
		if ((refreshFlags&INFO_EXTRA_LARGE_ICON) && it->second.sizeType==ICON_SIZE_TYPE_EXTRA_LARGE)
		{
			if (hExtraLargeBitmap)
			{
				HBITMAP old=it->second.bitmap;
				it->second.bitmap=hExtraLargeBitmap;
				if (old) m_OldBitmaps.push_back(old);
				hExtraLargeBitmap=NULL;
			}
			extraLargeIcon=&it->second;
			refreshFlags&=~INFO_EXTRA_LARGE_ICON;
		}
	}

	if ((refreshFlags&INFO_SMALL_ICON) && hSmallBitmap)
	{
		IconInfo *pInfo=&m_IconInfos.emplace(hash,IconInfo())->second;
		pInfo->sizeType=ICON_SIZE_TYPE_SMALL;
		pInfo->bTemp=bTemp;
		pInfo->bMetro=bMetro;
		pInfo->SetPath(path);
		pInfo->bitmap=hSmallBitmap;
		smallIcon=pInfo;
	}
	if ((refreshFlags&INFO_LARGE_ICON) && hLargeBitmap)
	{
		IconInfo *pInfo=&m_IconInfos.emplace(hash,IconInfo())->second;
		pInfo->sizeType=ICON_SIZE_TYPE_LARGE;
		pInfo->bTemp=bTemp;
		pInfo->bMetro=bMetro;
		pInfo->SetPath(path);
		pInfo->bitmap=hLargeBitmap;
		largeIcon=pInfo;
	}
	if ((refreshFlags&INFO_EXTRA_LARGE_ICON) && hExtraLargeBitmap)
	{
		IconInfo *pInfo=&m_IconInfos.emplace(hash,IconInfo())->second;
		pInfo->sizeType=ICON_SIZE_TYPE_EXTRA_LARGE;
		pInfo->bTemp=bTemp;
		pInfo->bMetro=bMetro;
		pInfo->SetPath(path);
		pInfo->bitmap=hExtraLargeBitmap;
		extraLargeIcon=pInfo;
	}
}

void CItemManager::IconInfo::SetPath( const wchar_t *path )
{
	Assert(RWLock::ThreadHasWriteLock(RWLOCK_ICONS));
	if (path && (_wcsicmp(path,L"shell32.dll")==0 || _wcsicmp(path,L"imageres.dll")==0))
	{
		PATH=path;
		StringUpper(PATH);
		timestamp.dwHighDateTime=timestamp.dwLowDateTime=0;
		return;
	}
	if (path && path[0] && path[1]=='#')
	{
		PATH=path;
		StringUpper(PATH);
		timestamp.dwHighDateTime=timestamp.dwLowDateTime=0;
		return;
	}
	if (path && path[0] && path[1]==':' && !PathIsNetworkPath(path))
	{
		WIN32_FILE_ATTRIBUTE_DATA attributes={0};
		if (GetFileAttributesEx(path,GetFileExInfoStandard,&attributes))
		{
			PATH=path;
			StringUpper(PATH);
			timestamp=attributes.ftLastWriteTime;
			return;
		}
	}
	PATH.Empty();
	timestamp.dwHighDateTime=timestamp.dwLowDateTime=0;
}

void CItemManager::LoadCustomIcon(const wchar_t *iconPath, int iconIndex, int refreshFlags, const IconInfo *&smallIcon, const IconInfo *&largeIcon, const IconInfo *&extraLargeIcon, bool bTemp)
{
	unsigned int hash = CalcFNVHash(iconPath, CalcFNVHash(&iconIndex, 4));

	FindInCache(hash, refreshFlags, smallIcon, largeIcon, extraLargeIcon);
	if (!refreshFlags)
		return;

	auto ExtractIconAsBitmap = [&](int iconSize) -> HBITMAP {

		if (iconIndex == -65536)
			return LoadMetroBitmap0(iconPath, iconSize);

		HICON hIcon;

		if (!*iconPath)
			hIcon = (HICON)LoadImage(g_Instance, MAKEINTRESOURCE(-iconIndex), IMAGE_ICON, iconSize, iconSize, LR_DEFAULTCOLOR);
		else
			hIcon = ShExtractIcon(iconPath, iconIndex == -1 ? 0 : iconIndex, iconSize);

		if (hIcon)
			return BitmapFromIcon(hIcon, iconSize);

		return nullptr;
	};

	// extract icon
	HBITMAP hSmallBitmap = nullptr, hLargeBitmap = nullptr, hExtraLargeBitmap = nullptr;

	if (refreshFlags & INFO_SMALL_ICON)
		hSmallBitmap = ExtractIconAsBitmap(SMALL_ICON_SIZE);

	if (refreshFlags & INFO_LARGE_ICON)
		hLargeBitmap = ExtractIconAsBitmap(LARGE_ICON_SIZE);

	if (refreshFlags & INFO_EXTRA_LARGE_ICON)
		hExtraLargeBitmap = ExtractIconAsBitmap(EXTRA_LARGE_ICON_SIZE);

	StoreInCache(hash, bTemp ? nullptr : iconPath, hSmallBitmap, hLargeBitmap, hExtraLargeBitmap, refreshFlags, smallIcon, largeIcon, extraLargeIcon, bTemp, false);
}

// Recursive function to preload the items for a folder
void CItemManager::LoadFolderItems( IShellItem *pFolder, int refreshFlags, int levels, TLocation location )
{
	CShellItemEnumerator enumerator(pFolder);
	if (!enumerator.IsValid()) return;

	int queueFlags=refreshFlags&INFO_ICON;
	refreshFlags&=~INFO_ICON;
	CComPtr<IShellItem> pChild;
	CAbsolutePidl childPidl;
	while (enumerator.GetNext(pChild,childPidl))
	{
		if (location==CItemManager::LOCATION_DESKTOP)
		{
			// collect only links from the desktop. this is an attempt to skip the SkyDrive item, which is causing problems.
			SFGAOF attr=0;
			if (FAILED(pChild->GetAttributes(SFGAO_LINK,&attr)) || !(attr&SFGAO_LINK))
				continue;
		}
		if (m_LoadingStage!=LOAD_LOADING) break;

		ItemInfo *pItemInfo=const_cast<ItemInfo*>(GetItemInfo(pChild,childPidl,refreshFlags,location));
		if (queueFlags)
		{
			RWLock lock(this,true,RWLOCK_ITEMS);
			QueueItemInfo(pItemInfo,queueFlags);
		}

		if (levels>1)
		{
			SFGAOF flags=0;
			if (SUCCEEDED(pChild->GetAttributes(SFGAO_FOLDER|SFGAO_STREAM|SFGAO_LINK,&flags)) && (flags&(SFGAO_FOLDER|SFGAO_STREAM|SFGAO_LINK))==SFGAO_FOLDER)
			{
				// go into subfolders but not archives or links to folders
				LoadFolderItems(pChild,refreshFlags|queueFlags,levels-1,location);
			}
		}
	}
}

CItemManager::TLocation CItemManager::DetermineLocation( const wchar_t *PATH )
{
	if (wcsncmp(PATH,m_RootStartMenu1,m_RootStartMenu1.GetLength())==0)
		return LOCATION_START_MENU;
	if (wcsncmp(PATH,m_RootStartMenu2,m_RootStartMenu2.GetLength())==0)
		return LOCATION_START_MENU;
	if (wcsncmp(PATH,m_RootStartMenu3,m_RootStartMenu3.GetLength())==0)
		return LOCATION_START_MENU;
	if (wcsncmp(PATH,m_RootGames,m_RootGames.GetLength())==0)
		return LOCATION_GAMES;
	if (wcsncmp(PATH,m_RootDesktop,m_RootDesktop.GetLength())==0)
		return LOCATION_DESKTOP;
	if (wcsncmp(PATH,m_RootTaskbar,m_RootTaskbar.GetLength())==0)
		return LOCATION_TASKBAR;
	if (wcsncmp(PATH,m_RootMetro,m_RootMetro.GetLength())==0)
		return LOCATION_METRO;
	return LOCATION_UNKNOWN;
}

// Recursive function to preload the metro apps
void CItemManager::LoadMetroItems( int refreshFlags )
{
	std::vector<MetroLink> links;
	std::vector<CString> nonApps10;
	GetMetroLinks(links,false,&nonApps10);
	int queueFlags=refreshFlags&INFO_ICON;
	refreshFlags&=~queueFlags;
	for (std::vector<MetroLink>::const_iterator it=links.begin();it!=links.end();++it)
	{
		ItemInfo *pItemInfo=const_cast<ItemInfo*>(GetItemInfo(it->pItem,it->pidl,refreshFlags,LOCATION_METRO));
		if (queueFlags)
		{
			RWLock lock(this,true,RWLOCK_ITEMS);
			QueueItemInfo(pItemInfo,queueFlags);
		}
		if (m_LoadingStage!=LOAD_LOADING) break;
	}
	if (GetWinVersion()>=WIN_VER_WIN10)
	{
		wchar_t APPID[256];
		for (std::vector<MetroLink>::const_iterator it=links.begin();it!=links.end();++it)
		{
			const ItemInfo *pInfo=GetItemInfo(it->pItem,it->pidl,INFO_LINK|INFO_LINK_APPID|INFO_METRO);
			Strcpy(APPID,_countof(APPID),it->appid);
			CharUpper(APPID);
			unsigned int hash=CalcFNVHash(APPID);
			{
				RWLock lock(this,true,RWLOCK_ITEMS);
				std::map<unsigned int,const ItemInfo*>::const_iterator it2=m_MetroItemInfos10.find(hash);
				if (it2==m_MetroItemInfos10.end())
					m_MetroItemInfos10[hash]=pInfo;
				std::set<unsigned int>::iterator it3=m_BlackListInfos10.find(hash);
				if (it3!=m_BlackListInfos10.end())
					m_BlackListInfos10.erase(it3);
			}
		}

		{
			RWLock lock(this,true,RWLOCK_ITEMS);
			for (std::vector<CString>::const_iterator it=nonApps10.begin();it!=nonApps10.end();++it)
			{
				Strcpy(APPID,_countof(APPID),*it);
				CharUpper(APPID);
				unsigned int hash=CalcFNVHash(APPID);
				std::map<unsigned int,const ItemInfo*>::const_iterator it2=m_MetroItemInfos10.find(hash);
				if (it2==m_MetroItemInfos10.end())
					m_MetroItemInfos10[hash]=NULL;
				std::set<unsigned int>::iterator it3=m_BlackListInfos10.find(hash);
				if (it3!=m_BlackListInfos10.end())
					m_BlackListInfos10.erase(it3);
			}
		}
	}
}

static KNOWNFOLDERID FOLDERID_TaskbarPinned={'TASK', 'BA', 'R', {'P', 'I', 'N', 'N', 'E', 'D'}};
static KNOWNFOLDERID FOLDERID_ClassicPinned={'CLAS', 'SI', 'C', {'P', 'I', 'N', 'N', 'E', 'D'}};
static KNOWNFOLDERID FOLDERID_MetroApps={'MET', 'R', 'O', {'A', 'P', 'P', 'S'}};

static struct
{
	KNOWNFOLDERID folder;
	int refreshFlags;
	int levels;
	CItemManager::TLocation location;
} g_CacheFolders[]=
{
	{FOLDERID_StartMenu,CItemManager::INFO_LINK|CItemManager::INFO_METRO|CItemManager::INFO_SMALL_ICON,MAX_FOLDER_LEVELS,CItemManager::LOCATION_START_MENU},
	{FOLDERID_CommonStartMenu,CItemManager::INFO_LINK|CItemManager::INFO_METRO|CItemManager::INFO_SMALL_ICON,MAX_FOLDER_LEVELS,CItemManager::LOCATION_START_MENU},
	{FOLDERID_Desktop,CItemManager::INFO_LINK,1,CItemManager::LOCATION_DESKTOP},
	{FOLDERID_PublicDesktop,CItemManager::INFO_LINK,1,CItemManager::LOCATION_DESKTOP},
	{FOLDERID_TaskbarPinned,CItemManager::INFO_LINK,1,CItemManager::LOCATION_TASKBAR},
	{FOLDERID_ClassicPinned,CItemManager::INFO_LINK|CItemManager::INFO_METRO|CItemManager::INFO_SMALL_ICON,1,CItemManager::LOCATION_START_MENU},
	{FOLDERID_MetroApps,CItemManager::INFO_LINK|CItemManager::INFO_METRO,1,CItemManager::LOCATION_METRO},
	{FOLDERID_ControlPanelFolder,CItemManager::INFO_SMALL_ICON,1,CItemManager::LOCATION_UNKNOWN},
	{FOLDERID_Favorites,CItemManager::INFO_SMALL_ICON,MAX_FOLDER_LEVELS,CItemManager::LOCATION_UNKNOWN},
};

const int NUM_WATCHED_DIRS=7;

void CItemManager::PreloadItemsThread( void )
{
	int dirCount=0;
	HANDLE handles[NUM_WATCHED_DIRS+1];
	DWORD dirMasks[NUM_WATCHED_DIRS]={0};
	DWORD dirMask=0xFFFFFFFF;
	while (1)
	{
		for (int i=0;i<_countof(g_CacheFolders);i++)
		{
			if (m_LoadingStage!=LOAD_LOADING) break;
			if (i==NUM_WATCHED_DIRS && dirMask==0xFFFFFFFF)
				SetEvent(m_DoneEvent);
			if (!(dirMask&(1<<i))) continue;
			if (g_CacheFolders[i].folder==FOLDERID_Favorites && !m_bPreloadFavorites) continue;
			CAbsolutePidl pidl;
			CComPtr<IShellItem> pFolder;
			if (g_CacheFolders[i].folder==FOLDERID_TaskbarPinned)
			{
				wchar_t path[_MAX_PATH]=TASKBAR_PINNED_ROOT;
				DoEnvironmentSubst(path,_countof(path));
				if (FAILED(SHParseDisplayName(path,NULL,&pidl,0,NULL)) || !pidl) continue;
				if (FAILED(SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pFolder)) || !pFolder) continue;
			}
			else if (g_CacheFolders[i].folder==FOLDERID_ClassicPinned)
			{
				if (GetSettingInt(L"PinnedPrograms")!=PINNED_PROGRAMS_PINNED) continue;
				wchar_t path[_MAX_PATH];
				Strcpy(path,_countof(path),GetSettingString(L"PinnedItemsPath"));
				DoEnvironmentSubst(path,_countof(path));
				if (FAILED(SHParseDisplayName(path,NULL,&pidl,0,NULL)) || !pidl) continue;
				if (FAILED(SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pFolder)) || !pFolder) continue;
			}
			else if (g_CacheFolders[i].folder==FOLDERID_MetroApps)
			{
				if (GetWinVersion()<WIN_VER_WIN8) continue;
				wchar_t path[_MAX_PATH]=METRO_APP_ROOT;
				DoEnvironmentSubst(path,_countof(path));
				pidl.Attach(ILCreateFromPath(path));
			}
			else
			{
				if (FAILED(ShGetKnownFolderIDList(g_CacheFolders[i].folder,&pidl)) || !pidl) continue;
				if (FAILED(SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pFolder)) || !pFolder) continue;
			}

			wchar_t path[_MAX_PATH];
			if (i<NUM_WATCHED_DIRS && dirMask==0xFFFFFFFF && SUCCEEDED(SHGetPathFromIDList(pidl,path)))
			{
				handles[dirCount]=FindFirstChangeNotification(path,TRUE,FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_LAST_WRITE);
				if (handles[dirCount]!=INVALID_HANDLE_VALUE)
				{
					dirMasks[dirCount]=1<<i;
					dirCount++;
				}
			}
			int refreshFlags=g_CacheFolders[i].refreshFlags;
			if (!m_bPreloadIcons)
				refreshFlags&=CItemManager::INFO_DATA;
			if (refreshFlags)
			{
				if (g_CacheFolders[i].folder!=FOLDERID_MetroApps)
				{
					if (g_CacheFolders[i].location==CItemManager::LOCATION_DESKTOP)
					{
						int compat=GetSettingInt(L"CompatibilityFixes");
						if (compat&COMPATIBILITY_SKIP_DESKTOP)
							continue;
					}
					LoadFolderItems(pFolder,refreshFlags,g_CacheFolders[i].levels,g_CacheFolders[i].location);
				}
				else
					LoadMetroItems(refreshFlags);
			}
		}
		if (dirMask==0xFFFFFFFF)
			SetEvent(m_DoneEvent);
		if (dirCount==0)
			break;
		handles[dirCount]=m_ExitEvent;
		DWORD wait=WaitForMultipleObjects(dirCount+1,handles,FALSE,INFINITE);
		if (wait<WAIT_OBJECT_0 || wait>WAIT_OBJECT_0+dirCount-1)
			break;

		int dir=wait-WAIT_OBJECT_0;
		dirMask=dirMasks[dir];

		if (!FindNextChangeNotification(handles[dir]))
			ResetEvent(handles[dir]); // so we don't wake on this event again
		Sleep(1000); // hopefully whatever file operation is in progress will finish in a second
	}
	for (int i=0;i<dirCount;i++)
		FindCloseChangeNotification(handles[i]);
}

DWORD CALLBACK CItemManager::StaticPreloadItemsThread( void *param )
{
	CItemManager *pThis=(CItemManager*)param;
	SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_IDLE);
	CoInitialize(NULL);
	pThis->m_LoadIconData[1].Init();
	wchar_t path[_MAX_PATH];
	GetModuleFileName(g_Instance,path,_countof(path));
	LoadLibrary(path); // stop the DLL from unloading
	pThis->PreloadItemsThread();
	pThis->m_LoadIconData[1].Close();
	CoUninitialize();
	FreeLibraryAndExitThread(g_Instance,0); // release the DLL
	return 0;
}

void CItemManager::RefreshInfoThread( void )
{
	WaitForSingleObject(m_StartEvent,REFRESH_DELAY);
	bool bRefresh=false;
	while (1)
	{
		HANDLE handles[2]={m_WorkEvent,m_ExitEvent};
		WaitForMultipleObjects(2,handles,FALSE,INFINITE);
		bRefresh=false;
		int t0=GetTickCount();
		while (1)
		{
			if (m_LoadingStage!=LOAD_LOADING)
				return;

			ItemInfo *pItemInfo=NULL;
			int refreshFlags=0;
			{
				Lock cleanupLock(this,LOCK_CLEANUP);
				{
					RWLock lock(this,true,RWLOCK_ITEMS);
					if (!m_ItemQueue.empty())
					{
						pItemInfo=*m_ItemQueue.begin();
						m_ItemQueue.pop_front();
					}
					else if (!m_ItemQueueLow.empty())
					{
						pItemInfo=*m_ItemQueueLow.begin();
						m_ItemQueueLow.pop_front();
					}
					else
						break;
					refreshFlags=pItemInfo->refreshFlags;
				}
				if (refreshFlags && pItemInfo->bTemp)
				{
					// temp items must be refreshed inside LOCK_CLEANUP because the cleanup process will delete all such items
					RefreshItemInfo(pItemInfo,refreshFlags,NULL,false);
					refreshFlags=0;
				}
			}
			if (refreshFlags)
			{
				// non-temp items should be refreshed outside LOCK_CLEANUP
				RefreshItemInfo(pItemInfo,refreshFlags,NULL,false);
			}

			bRefresh=true;
			int t=GetTickCount();
			if (t-t0>100)
			{
				CMenuContainer::RefreshIcons();
				t0=t;
				bRefresh=false;
			}
		}
		if (bRefresh)
			CMenuContainer::RefreshIcons();
	}
}

DWORD CALLBACK CItemManager::StaticRefreshInfoThread( void *param )
{
	CItemManager *pThis=(CItemManager*)param;
	volatile DWORD MAIN_THREAD=pThis->m_MainThreadId;
	SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_IDLE);
	CoInitialize(NULL);
	pThis->m_LoadIconData[2].Init();
	pThis->RefreshInfoThread();
	pThis->m_LoadIconData[2].Close();
	CoUninitialize();
	return MAIN_THREAD-MAIN_THREAD;
}

namespace
{
	struct IconData
	{
		unsigned int key;
		CItemManager::TIconSizeType sizeType;
		int PATHLen;
		FILETIME timestamp;
		int bitmapW, bitmapH;
	};

	struct ItemData
	{
		unsigned int key;
		FILETIME writestamp;
		FILETIME createstamp;
		bool bIconOnly;
		bool bLink;
		bool bMetroLink;
		bool bProtectedLink;
		bool bNoPin;
		bool bNoNew;
		bool bExplicitAppId;
		int pidlSize;
		int pathLen;
		int PATHLen;
		int smallIcon;
		int largeIcon;
		int extraLargeIcon;
		int validFlags;
		int targetPidlSize;
		int targetPATHLen;
		int appidLen;
		int metroNameLen;
		int iconPathLen;
		DWORD iconColor;
		int iconIndex;
	};
}

static DWORD ReadCacheFile( HANDLE file )
{
	DWORD data;
	DWORD q;
	if (!ReadFile(file,&data,4,&q,NULL) || q!=4)
		return 0;
	return data;
}

static bool ReadCacheFile( HANDLE file, IconData &data )
{
	DWORD q;
	return ReadFile(file,&data,sizeof(data),&q,NULL)!=0 && q==sizeof(data);
}

static bool ReadCacheFile( HANDLE file, ItemData &data )
{
	DWORD q;
	return ReadFile(file,&data,sizeof(data),&q,NULL)!=0 && q==sizeof(data);
}

static bool ReadCacheFile( HANDLE file, CAbsolutePidl &data, int size )
{
	if (size>0)
	{
		PIDLIST_ABSOLUTE pidl=(PIDLIST_ABSOLUTE)CoTaskMemAlloc(size);
		if (!pidl) return false;
		DWORD q;
		if (!ReadFile(file,pidl,size,&q,NULL) || q!=size)
		{
			ILFree(pidl);
			return false;
		}
		data.Attach(pidl);
	}
	return true;
}

static bool ReadCacheFile( HANDLE file, CString &data, int len )
{
	if (len>0)
	{
		DWORD q;
		if (!ReadFile(file,data.GetBuffer(len),len*2,&q,NULL) || q!=len*2)
		{
			data.Empty();
			return false;
		}
		data.ReleaseBuffer(len);
	}
	return true;
}

static bool ReadCacheFile( HANDLE file, HDC hdc, HBITMAP &data, int width, int height )
{
	std::vector<DWORD> bits(width*height);
	if (bits.empty()) return false;
	BITMAPINFO bi={0};
	bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth=width;
	bi.bmiHeader.biHeight=height;
	bi.bmiHeader.biPlanes=1;
	bi.bmiHeader.biBitCount=32;
	DWORD q;
	int size=width*height*4;
	if (!ReadFile(file,&bits[0],size,&q,NULL) || q!=size)
		return false;
	unsigned int *pBits;
	data=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,(void**)&pBits,NULL,0);
	if (!data)
		return false;
	memcpy(pBits,&bits[0],size);
	return true;
}

static void WriteCacheFile( HANDLE file, DWORD data )
{
	DWORD q;
	WriteFile(file,&data,sizeof(data),&q,NULL);
}

static void WriteCacheFile( HANDLE file, const IconData &data )
{
	DWORD q;
	WriteFile(file,&data,sizeof(data),&q,NULL);
}

static void WriteCacheFile( HANDLE file, const ItemData &data )
{
	DWORD q;
	WriteFile(file,&data,sizeof(data),&q,NULL);
}

static void WriteCacheFile( HANDLE file, PIDLIST_ABSOLUTE data, int size )
{
	if (size>0)
	{
		DWORD q;
		WriteFile(file,data,size,&q,NULL);
	}
}

static void WriteCacheFile( HANDLE file, const CString &data )
{
	if (!data.IsEmpty())
	{
		DWORD q;
		WriteFile(file,(const wchar_t*)data,data.GetLength()*2,&q,NULL);
	}
}

static void WriteCacheFile( HANDLE file, HDC hdc, HBITMAP data, int width, int height )
{
	std::vector<DWORD> bits(width*height);
	if (bits.empty()) return;
	BITMAPINFO bi={0};
	bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth=width;
	bi.bmiHeader.biHeight=height;
	bi.bmiHeader.biPlanes=1;
	bi.bmiHeader.biBitCount=32;
	GetDIBits(hdc,data,0,height,&bits[0],&bi,DIB_RGB_COLORS);
	DWORD q;
	WriteFile(file,&bits[0],width*height*4,&q,NULL);
}

bool CItemManager::CompareModuleTimeStamp( const CString &PATH, const FILETIME &timestamp, std::vector<ModuleInfo> &modules )
{
	for (std::vector<ModuleInfo>::const_iterator it=modules.begin();it!=modules.end();++it)
	{
		if (it->PATH==PATH)
			return (CompareFileTime(&timestamp,&it->timestamp)==0);
	}
	WIN32_FILE_ATTRIBUTE_DATA attributes={0};
	if (GetFileAttributesEx(PATH,GetFileExInfoStandard,&attributes))
	{
		ModuleInfo info={PATH,attributes.ftLastWriteTime};
		modules.push_back(info);
		return (CompareFileTime(&timestamp,&info.timestamp)==0);
	}
	else
	{
		ModuleInfo info={PATH};
		modules.push_back(info);
		return false;
	}
}

void CItemManager::LoadCacheFile( void )
{
#ifdef DISABLE_CACHE
		return;
#endif
	// load cached icons and info
	wchar_t path[_MAX_PATH]=L"%LOCALAPPDATA%\\OpenShell\\DataCache.db";
	DoEnvironmentSubst(path,_MAX_PATH);

	HANDLE file=CreateFile(path,GENERIC_READ,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	if (file==INVALID_HANDLE_VALUE) return;

	bool bError=true;
	DWORD tag=ReadCacheFile(file);
	std::vector<ModuleInfo> modules;
	ModuleInfo stdInfo={L"SHELL32.DLL"};
	modules.push_back(stdInfo);
	stdInfo.PATH=L"IMAGERES.DLL";
	modules.push_back(stdInfo);
	m_BlackListInfos10.clear();
	if (tag=='CLSH')
	{
		tag=ReadCacheFile(file);
		std::vector<const IconInfo*> remapIcons(1);
		remapIcons[0]=NULL;
		DWORD build;
		if (tag==GetVersionEx(g_Instance,&build) && ReadCacheFile(file)==CACHE_FILE_VERSION)
		{
			int size1=ReadCacheFile(file);
			int size2=ReadCacheFile(file);
			int size3=ReadCacheFile(file);
			int langHash=ReadCacheFile(file);
			bError=false;
			tag=ReadCacheFile(file);
			HDC hdc=CreateCompatibleDC(NULL);
			while (tag=='ICON')
			{
				IconData data;
				if (!ReadCacheFile(file,data))
				{
					bError=true;
					break;
				}
				IconInfo info;
				info.sizeType=data.sizeType;
				info.timestamp=data.timestamp;
				info.bTemp=false;
				info.bMetro=false;
				if (!ReadCacheFile(file,info.PATH,data.PATHLen))
				{
					bError=true;
					break;
				}
				if (size1==SMALL_ICON_SIZE && size2==LARGE_ICON_SIZE && size3==EXTRA_LARGE_ICON_SIZE && CompareModuleTimeStamp(info.PATH,info.timestamp,modules))
				{
					if (!ReadCacheFile(file,hdc,info.bitmap,data.bitmapW,data.bitmapH))
					{
						bError=true;
						break;
					}
					remapIcons.push_back(&m_IconInfos.emplace(data.key,info)->second);
				}
				else
				{
					int size=data.bitmapW*data.bitmapH*4;
					SetFilePointer(file,size,NULL,FILE_CURRENT);
					remapIcons.push_back(NULL);
				}
				tag=ReadCacheFile(file);
			}
			DeleteDC(hdc);
			if (tag!='ITEM')
			{
				bError=true;
			}
			else
			{
				wchar_t languages[100];
				DWORD size=0;
				DWORD len=_countof(languages);
				GetUserPreferredUILanguages(MUI_LANGUAGE_ID,&size,languages,&len);
				bError=(CalcFNVHash(languages,len*2,FNV_HASH0)!=langHash);
			}
			while (!bError && tag=='ITEM')
			{
				ItemData data;
				if (!ReadCacheFile(file,data))
				{
					bError=true;
					break;
				}
				ItemInfo &info=m_ItemInfos.emplace(data.key,ItemInfo())->second;

				info.writestamp=data.writestamp;
				info.createstamp=data.createstamp;
				info.bIconOnly=data.bIconOnly;
				info.bTemp=false;
				info.bLink=data.bLink;
				info.bMetroLink=data.bMetroLink;
				info.bProtectedLink=data.bProtectedLink;
				info.bNoPin=data.bNoPin;
				info.bNoNew=data.bNoNew;
				info.bExplicitAppId=data.bExplicitAppId;
				info.validFlags=data.validFlags;
				info.refreshFlags=0;
				info.iconColor=data.iconColor;
				info.iconIndex=data.iconIndex;

				info.smallIcon=data.smallIcon<(int)remapIcons.size()?remapIcons[data.smallIcon]:NULL;
				if (!info.smallIcon)
				{
					info.validFlags&=~INFO_SMALL_ICON;
					info.smallIcon=m_DefaultSmallIcon;
				}
				info.largeIcon=data.largeIcon<(int)remapIcons.size()?remapIcons[data.largeIcon]:NULL;
				if (!info.largeIcon)
				{
					info.validFlags&=~INFO_LARGE_ICON;
					info.largeIcon=m_DefaultLargeIcon;
				}
				info.extraLargeIcon=data.extraLargeIcon<(int)remapIcons.size()?remapIcons[data.extraLargeIcon]:NULL;
				if (!info.extraLargeIcon)
				{
					info.validFlags&=~INFO_EXTRA_LARGE_ICON;
					info.extraLargeIcon=m_DefaultExtraLargeIcon;
				}

				bError=bError || !ReadCacheFile(file,info.pidl,data.pidlSize);
				bError=bError || !ReadCacheFile(file,info.path,data.pathLen);
				bError=bError || !ReadCacheFile(file,info.PATH,data.PATHLen);
				bError=bError || !ReadCacheFile(file,info.targetPidl,data.targetPidlSize);
				bError=bError || !ReadCacheFile(file,info.targetPATH,data.targetPATHLen);
				bError=bError || !ReadCacheFile(file,info.appid,data.appidLen);
				bError=bError || !ReadCacheFile(file,info.metroName,data.metroNameLen);
				bError=bError || !ReadCacheFile(file,info.iconPath,data.iconPathLen);

				tag=ReadCacheFile(file);
			}
			if (tag=='BLAK')
			{
				int count=ReadCacheFile(file);
				for (int i=0;i<count;i++)
				{
					unsigned int hash=ReadCacheFile(file);
					m_BlackListInfos10.insert(hash);
				}
			}
		}
	}

	CloseHandle(file);
	if (bError)
	{
		m_ItemInfos.clear();
		for (std::multimap<unsigned int,IconInfo>::const_iterator it=m_IconInfos.begin();it!=m_IconInfos.end();++it)
		{
			if (it->second.bitmap)
				DeleteObject(it->second.bitmap);
		}
		m_IconInfos.clear();
		CreateDefaultIcons();
	}
}

DWORD CALLBACK CItemManager::SaveCacheFileThread( void *param )
{
	CItemManager *pThis=(CItemManager*)param;
	wchar_t path[_MAX_PATH]=L"%LOCALAPPDATA%\\OpenShell";
	DoEnvironmentSubst(path,_MAX_PATH);
	SHCreateDirectory(NULL,path);
	Strcat(path,_countof(path),L"\\DataCache.tmp");
	HANDLE file=CreateFile(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
	if (file==INVALID_HANDLE_VALUE) return 0;

	WriteCacheFile(file,'CLSH');
	WriteCacheFile(file,GetVersionEx(g_Instance));
	WriteCacheFile(file,CACHE_FILE_VERSION);
	WriteCacheFile(file,SMALL_ICON_SIZE);
	WriteCacheFile(file,LARGE_ICON_SIZE);
	WriteCacheFile(file,EXTRA_LARGE_ICON_SIZE);
	{
		wchar_t languages[100];
		DWORD size=0;
		DWORD len=_countof(languages);
		GetUserPreferredUILanguages(MUI_LANGUAGE_ID,&size,languages,&len);
		WriteCacheFile(file,CalcFNVHash(languages,len*2,FNV_HASH0));
	}

	std::vector<const std::pair<const unsigned int,IconInfo>*> iconInfos;
	{
		RWLock lock(pThis,false,RWLOCK_ICONS);
		for (std::multimap<unsigned int,IconInfo>::const_iterator it=pThis->m_IconInfos.begin();it!=pThis->m_IconInfos.end();++it)
		{
			if (!it->second.PATH.IsEmpty() && it->second.PATH[1]!='#' && it->first!=0)
				iconInfos.push_back(&*it);
		}
	}

	std::vector<const std::pair<const unsigned int,ItemInfo>*> itemInfos;
	std::vector<unsigned int> blackList;
	{
		RWLock lock(pThis,false,RWLOCK_ITEMS);
		for (std::multimap<unsigned int,ItemInfo>::const_iterator it=pThis->m_ItemInfos.begin();it!=pThis->m_ItemInfos.end();++it)
		{
			if (it->first!=0)
				itemInfos.push_back(&*it);
		}
		for (std::set<unsigned int>::const_iterator it=pThis->m_BlackListInfos10.begin();it!=pThis->m_BlackListInfos10.end();++it)
			blackList.push_back(*it);
	}

	HDC hdc=CreateCompatibleDC(NULL);
	std::map<const IconInfo*,int> remapIcons;
	int iconIndex=1;
	// save cached icons and info
	for (std::vector<const std::pair<const unsigned int,IconInfo>*>::const_iterator it=iconInfos.begin();it!=iconInfos.end();++it)
	{
		RWLock lock(pThis,false,RWLOCK_ICONS);
		if ((*it)->second.bTemp || (*it)->second.bMetro) continue;
		remapIcons[&(*it)->second]=iconIndex++;
		IconData data;
		data.key=(*it)->first;
		data.sizeType=(*it)->second.sizeType;
		data.timestamp=(*it)->second.timestamp;
		data.PATHLen=(*it)->second.PATH.GetLength();
		BITMAP bmp;
		GetObject((*it)->second.bitmap,sizeof(bmp),&bmp);
		data.bitmapW=bmp.bmWidth;
		data.bitmapH=bmp.bmHeight;

		WriteCacheFile(file,'ICON');
		WriteCacheFile(file,data);
		WriteCacheFile(file,(*it)->second.PATH);
		WriteCacheFile(file,hdc,(*it)->second.bitmap,data.bitmapW,data.bitmapH);
	}
	DeleteDC(hdc);

	FILE *log=NULL;
	if (g_LogCategories&LOG_CACHE)
	{
		wchar_t path3[_MAX_PATH]=L"%LOCALAPPDATA%\\OpenShell\\DataCache.txt";
		DoEnvironmentSubst(path3,_countof(path3));
		if (_wfopen_s(&log,path3,L"wb")==0)
		{
			wchar_t bom=0xFEFF;
			fwrite(&bom,2,1,log);
		}
	}
	for (std::vector<const std::pair<const unsigned int,ItemInfo>*>::const_iterator it=itemInfos.begin();it!=itemInfos.end();++it)
	{
		RWLock lock(pThis,false,RWLOCK_ITEMS);
		if ((*it)->second.bTemp || (*it)->second.path.IsEmpty()) continue;

		ItemData data;
		data.key=(*it)->first;
		data.writestamp=(*it)->second.writestamp;
		data.createstamp=(*it)->second.createstamp;
		data.bIconOnly=(*it)->second.bIconOnly;
		data.bLink=(*it)->second.bLink;
		data.bMetroLink=(*it)->second.bMetroLink;
		data.bProtectedLink=(*it)->second.bProtectedLink;
		data.bNoPin=(*it)->second.bNoPin;
		data.bNoNew=(*it)->second.bNoNew;
		data.bExplicitAppId=(*it)->second.bExplicitAppId;
		data.pidlSize=(*it)->second.GetLatestPidl()?ILGetSize((*it)->second.GetLatestPidl()):0;
		data.pathLen=(*it)->second.path.GetLength();
		data.PATHLen=(*it)->second.PATH.GetLength();

		std::map<const IconInfo*,int>::const_iterator remapIt=remapIcons.find((*it)->second.smallIcon);
		data.smallIcon=(remapIt==remapIcons.end()?0:remapIt->second);
		remapIt=remapIcons.find((*it)->second.largeIcon);
		data.largeIcon=(remapIt==remapIcons.end()?0:remapIt->second);
		remapIt=remapIcons.find((*it)->second.extraLargeIcon);
		data.extraLargeIcon=(remapIt==remapIcons.end()?0:remapIt->second);

		data.validFlags=(*it)->second.validFlags;
		data.targetPidlSize=(*it)->second.targetPidl?ILGetSize((*it)->second.targetPidl):0;
		data.targetPATHLen=(*it)->second.targetPATH.GetLength();
		data.appidLen=(*it)->second.appid.GetLength();
		data.metroNameLen=(*it)->second.metroName.GetLength();
		data.iconPathLen=(*it)->second.iconPath.GetLength();
		data.iconColor=(*it)->second.iconColor;
		data.iconIndex=(*it)->second.iconIndex;

		WriteCacheFile(file,'ITEM');
		WriteCacheFile(file,data);
		WriteCacheFile(file,(*it)->second.GetLatestPidl(),data.pidlSize);
		WriteCacheFile(file,(*it)->second.path);
		WriteCacheFile(file,(*it)->second.PATH);
		WriteCacheFile(file,(*it)->second.targetPidl,data.targetPidlSize);
		WriteCacheFile(file,(*it)->second.targetPATH);
		WriteCacheFile(file,(*it)->second.appid);
		WriteCacheFile(file,(*it)->second.metroName);
		WriteCacheFile(file,(*it)->second.iconPath);
		if (log) fwprintf(log,L"0x%08X - %s\r\n",(*it)->first,(const wchar_t*)(*it)->second.PATH);
	}
	{
		WriteCacheFile(file,'BLAK');
		WriteCacheFile(file,(DWORD)blackList.size());
		for (std::vector<unsigned int>::const_iterator it=blackList.begin();it!=blackList.end();++it)
			WriteCacheFile(file,*it);
	}
	if (log) fclose(log);

	CloseHandle(file);
	wchar_t path2[_MAX_PATH]=L"%LOCALAPPDATA%\\OpenShell\\DataCache.db";
	DoEnvironmentSubst(path2,_MAX_PATH);
	MoveFileEx(path,path2,MOVEFILE_REPLACE_EXISTING);
	return 0;
}

void CItemManager::SaveCacheFile( void )
{
#ifdef DISABLE_CACHE
		return;
#endif
	if (g_LogCategories&LOG_CACHE)
	{
		SaveCacheFileThread(this);
		return;
	}
	int time=GetTickCount();
	if (m_LastCacheSave && (time-m_LastCacheSave)<300000) // at least 5 minutes since the last save
		return;
	m_LastCacheSave=time;
	if (m_SaveCacheThread)
	{
		if (WaitForSingleObject(m_SaveCacheThread,0)==WAIT_TIMEOUT)
			return;
		CloseHandle(m_SaveCacheThread);
		m_SaveCacheThread=NULL;
	}
	m_SaveCacheThread=CreateThread(NULL,0,SaveCacheFileThread,this,0,NULL);
}

void CItemManager::ClearCache( void )
{
	Lock cleanupLock(this,LOCK_CLEANUP);
	RWLock itemLock(this,true,RWLOCK_ITEMS);
	RWLock iconLock(this,true,RWLOCK_ICONS);

	wchar_t path[_MAX_PATH]=L"%LOCALAPPDATA%\\OpenShell\\DataCache.db";
	DoEnvironmentSubst(path,_MAX_PATH);
	DeleteFile(path);

	m_BlackListInfos10.clear();
	m_ItemInfos.clear();
	for (std::multimap<unsigned int,IconInfo>::const_iterator it=m_IconInfos.begin();it!=m_IconInfos.end();++it)
	{
		if (it->second.bitmap)
			DeleteObject(it->second.bitmap);
	}
	m_IconInfos.clear();
	m_MetroItemInfos10.clear();
	CreateDefaultIcons();
	ItemInfo &item=m_ItemInfos.emplace(0,ItemInfo())->second;
	item.bIconOnly=true;
	item.smallIcon=m_DefaultSmallIcon;
	item.largeIcon=m_DefaultLargeIcon;
	item.extraLargeIcon=m_DefaultExtraLargeIcon;
}

int CItemManager::GetIconSize(TIconSizeType iconSizeType) const
{
	switch (iconSizeType)
	{
	case ICON_SIZE_TYPE_SMALL:
	case ICON_SIZE_TYPE_SMALL_METRO:
		return SMALL_ICON_SIZE;

	case ICON_SIZE_TYPE_LARGE:
	case ICON_SIZE_TYPE_LARGE_METRO:
		return LARGE_ICON_SIZE;

	case ICON_SIZE_TYPE_EXTRA_LARGE:
	case ICON_SIZE_TYPE_EXTRA_LARGE_METRO:
		return EXTRA_LARGE_ICON_SIZE;
	}

	return 0;
}

// retrieves the pidl and the SFGAO_FOLDER, SFGAO_STREAM, SFGAO_LINK flags for the path
// for paths starting with \\ tries to guess if it is a folder or a link based on the extension
HRESULT MenuParseDisplayName( const wchar_t *path, PIDLIST_ABSOLUTE *pPidl, SFGAOF *pFlags, TNetworkType *pNetworkType )
{
	if (pFlags)
		*pFlags=0;
	*pPidl=NULL;
	if (pNetworkType)
		*pNetworkType=NETWORK_NONE;
	if (PathIsNetworkPath(path))
	{
		*pPidl=SHSimpleIDListFromPath(path);
		if (!*pPidl)
			return E_FAIL;
		if (pFlags || pNetworkType)
		{
			const wchar_t *ext=PathFindExtension(path);
			if (!*ext)
			{
				if (pFlags)
					*pFlags=SFGAO_FOLDER;
				if (pNetworkType)
				{
					if (path[1]==':')
					{
						// drive:
						if (path[2]=='\\' && path[3]) // drive:\folder
							*pNetworkType=NETWORK_FOLDER;
						else
							*pNetworkType=NETWORK_DRIVE;
					}
					else if (path[0]=='\\' && path[1]=='\\')
					{
						// \\server
						const wchar_t *c=wcschr(path+2,'\\');
						if (c)
						{
							// \\server\share[\folder]
							*pNetworkType=wcschr(c+2,'\\')?NETWORK_FOLDER:NETWORK_SHARE;
						}
						else
							*pNetworkType=NETWORK_SERVER;
					}
					else
					{
						// something else
						*pNetworkType=NETWORK_FOLDER;
					}
				}
			}
			else if (_wcsicmp(ext,L".lnk")==0)
			{
				if (pFlags)
					*pFlags=SFGAO_LINK;
				if (pNetworkType)
					*pNetworkType=NETWORK_FILE;
			}
			else
			{
				if (pFlags)
					*pFlags=0;
				if (pNetworkType)
					*pNetworkType=NETWORK_FILE;
			}
		}
		return S_OK;
	}
	else
	{
		return ShParseDisplayName(path,pPidl,SFGAO_FOLDER|SFGAO_STREAM|SFGAO_LINK,pFlags);
	}
}

const wchar_t *GetDefaultNetworkIcon( TNetworkType networkType )
{
	switch (networkType)
	{
		case NETWORK_SERVER:
			return L"imageres.dll,109";
		case NETWORK_SHARE:
			return L"imageres.dll,143";
		case NETWORK_DRIVE:
			return L"imageres.dll,33";
		case NETWORK_FOLDER:
			return L"imageres.dll,3";
		default:
			return L"imageres.dll,2";
	}
}

bool MenuGetFileTimestamp( const wchar_t *path, FILETIME *pWriteTime, FILETIME *pCreateTime )
{
	if (PathIsNetworkPath(path))
	{
		memset(pWriteTime,0,sizeof(FILETIME));
		memset(pCreateTime,0,sizeof(FILETIME));
		return true;
	}
	else
	{
		WIN32_FILE_ATTRIBUTE_DATA attributes={0};
		if (!GetFileAttributesEx(path,GetFileExInfoStandard,&attributes))
		{
			memset(pWriteTime,0,sizeof(FILETIME));
			memset(pCreateTime,0,sizeof(FILETIME));
			return false;
		}
		else
		{
			*pWriteTime=attributes.ftLastWriteTime;
			*pCreateTime=attributes.ftCreationTime;
			return true;
		}
	}
}

static CString GetKnownFolderSetting( REFKNOWNFOLDERID rfid )
{
	if (rfid==FOLDERID_CommonAdminTools)
		return L"shell:::{D20EA4E1-3957-11d2-A40B-0C5020524153}";

	wchar_t path[_MAX_PATH];
	path[0]=0;
	if (rfid==FOLDERID_StartMenu)
		Strcpy(path,_countof(path),GetSettingString(L"FolderStartMenu"));

	if (rfid==FOLDERID_CommonStartMenu)
	{
		Strcpy(path,_countof(path),GetSettingString(L"FolderCommonStartMenu"));
	}
	else if (rfid==FOLDERID_Programs)
	{
		CString str=GetSettingString(L"FolderPrograms");
		if (!str.IsEmpty())
			Strcpy(path,_countof(path),str);
		else
		{
			str=GetSettingString(L"FolderStartMenu");
			if (!str.IsEmpty())
				Sprintf(path,_countof(path),L"%s\\Programs",str);
		}
	}
	else if (rfid==FOLDERID_CommonPrograms)
	{
		CString str=GetSettingString(L"FolderCommonPrograms");
		if (!str.IsEmpty())
			Strcpy(path,_countof(path),str);
		else
		{
			str=GetSettingString(L"FolderCommonStartMenu");
			if (!str.IsEmpty())
				Sprintf(path,_countof(path),L"%s\\Programs",str);
		}
	}
	else if (rfid==FOLDERID_Favorites)
	{
		if (IsEdgeDefaultBrowser())
			Strcpy(path,_countof(path),L"%LOCALAPPDATA%\\Packages\\Microsoft.MicrosoftEdge_8wekyb3d8bbwe\\AC\\MicrosoftEdge\\User\\Default\\Favorites");
	}
	if (!path[0])
		return CString();
	DoEnvironmentSubst(path,_countof(path));
	return path;
}

STDAPI ShGetKnownFolderPath( REFKNOWNFOLDERID rfid, PWSTR *pPath )
{
	CString path=GetKnownFolderSetting(rfid);
	if (!path.IsEmpty())
	{
		int size=path.GetLength()+1;
		*pPath=(PWSTR)CoTaskMemAlloc(size*2);
		if (!*pPath)
			return E_FAIL;
		Strcpy(*pPath,size,path);
		return S_OK;
	}
	return SHGetKnownFolderPath(rfid,0,NULL,pPath);
}

STDAPI ShGetKnownFolderIDList(REFKNOWNFOLDERID rfid, PIDLIST_ABSOLUTE *pPidl )
{
	CString path=GetKnownFolderSetting(rfid);
	if (!path.IsEmpty())
	{
		return ShParseDisplayName(path,pPidl,0,NULL);
	}
	return SHGetKnownFolderIDList(rfid,0,NULL,pPidl);
}

STDAPI ShGetKnownFolderItem(REFKNOWNFOLDERID rfid, IShellItem **ppItem )
{
#ifndef BUILD_SETUP
	if (rfid==FOLDERID_Games && (GetTickCount()&16))
		return E_FAIL;
#endif
	// Skip getting the Games menu on RS4
	if(IsWin10RS4() && rfid==FOLDERID_Games)
	{
		return E_FAIL;
	}
	CString path=GetKnownFolderSetting(rfid);
	if (!path.IsEmpty())
	{
		return SHCreateItemFromParsingName(path,NULL,IID_IShellItem,(void**)ppItem);
	}
	return SHGetKnownFolderItem(rfid,KF_FLAG_DEFAULT,NULL,IID_IShellItem,(void**)ppItem);
}

///////////////////////////////////////////////////////////////////////////////

CShellItemEnumerator::CShellItemEnumerator( IShellItem *pFolder )
{
	int compat=GetSettingInt(L"CompatibilityFixes");
	if (!(compat&COMPATIBILITY_ENUM_SHELLITEM))
	{
		CComPtr<IShellFolder> pFolder0;
		HRESULT hr=pFolder->BindToHandler(NULL,BHID_SFObject,IID_IShellFolder,(void**)&pFolder0);
		if (FAILED(hr))
		{
			CComQIPtr<IShellItem2> pFolder2=pFolder;
			if (pFolder2)
			{
				// The operation may fail if pFolder has a simple pidl (like from a network path). Update the shell item and try again
				pFolder2->Update(NULL);
				hr=pFolder->BindToHandler(NULL,BHID_SFObject,IID_IShellFolder,(void**)&pFolder0);
				if (FAILED(hr))
					return;
			}
		}
		if (FAILED(SHGetIDListFromObject(pFolder,&m_Root)))
			return;
		hr=pFolder0->EnumObjects(NULL,SHCONTF_FOLDERS|SHCONTF_NONFOLDERS,(IEnumIDList**)&m_pEnumPidls);
	}
	else
	{
		HRESULT hr=pFolder->BindToHandler(NULL,BHID_EnumItems,IID_IEnumShellItems,(void**)&m_pEnumItems);
		if (FAILED(hr))
		{
			m_pEnumItems=NULL;
			// The operation may fail if pFolder has a simple pidl (like from a network path). Update the shell item and try again
			CComQIPtr<IShellItem2> pFolder2=pFolder;
			if (pFolder2)
			{
				pFolder2->Update(NULL);
				hr=pFolder->BindToHandler(NULL,BHID_EnumItems,IID_IEnumShellItems,(void**)&m_pEnumItems);
				if (FAILED(hr))
				{
					m_pEnumItems=NULL;
					return;
				}
			}
		}
		if (compat&COMPATIBILITY_ENUM_FIX_PIDLS)
			SHGetIDListFromObject(pFolder,&m_Root);
	}
}

bool CShellItemEnumerator::IsValid( void ) const
{
	return m_pEnumPidls || m_pEnumItems;
}

bool CShellItemEnumerator::GetNext( CComPtr<IShellItem> &pChild, CAbsolutePidl &childPidl )
{
	pChild=NULL;
	childPidl.Clear();
	if (m_pEnumPidls)
	{
		CAbsolutePidl child;
		if (m_pEnumPidls->Next(1,(PITEMID_CHILD*)&child,NULL)!=S_OK)
			return false;

		CAbsolutePidl pidl;
		pidl.Attach(ILCombine(m_Root,child));
		if (FAILED(SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pChild)))
			return false;
		childPidl.Swap(pidl);
		return true;
	}
	else
	{
		CComPtr<IShellItem> pChild0;
		if (m_pEnumItems->Next(1,&pChild0,NULL)!=S_OK)
			return false;

		CAbsolutePidl pidl0;
		if (FAILED(SHGetIDListFromObject(pChild0,&pidl0)))
			return false;

		if (m_Root)
		{
			CAbsolutePidl pidl;
			pidl.Attach(ILCombine(m_Root,ILFindLastID(pidl0)));
			if (SUCCEEDED(SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pChild)))
			{
				childPidl.Swap(pidl);
				return true;
			}
		}

		childPidl.Swap(pidl0);
		pChild=pChild0;
		return true;
	}
}
