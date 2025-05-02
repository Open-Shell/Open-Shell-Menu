// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include "StringSet.h"
#include "StringUtils.h"
#include "Settings.h"
#include "Translations.h"
#include "ResourceHelper.h"
#include "Assert.h"
#include <vector>
#include <wincodec.h>
#include "TrackResources.h"

static CStringSet g_ResStrings;
static std::map<int,std::vector<char> > g_ResDialogs;

// Loads all strings and dialogs from hLngInstance
// pDialogs is a NULL-terminated list of dialog IDs. They are loaded from hLngInstance if possible, otherwise from g_Instance
void LoadTranslationResources( HINSTANCE hLngInstance, int *pDialogs )
{
	Assert(g_Instance);
	DWORD lngVersion=0;
	if (hLngInstance)
	{
		LoadTranslationOverrides(hLngInstance);
		g_ResStrings.Init(hLngInstance);
		lngVersion=GetVersionEx(hLngInstance);
	}
	if (!pDialogs)
		return;
	for (int i=0;pDialogs[i];i+=2)
	{
		int dlgId=pDialogs[i];
		DWORD dlgVersion=pDialogs[i+1];
		HINSTANCE hInst=hLngInstance;
		HRSRC hrsrc=NULL;
		if (hLngInstance && lngVersion>=dlgVersion)
			hrsrc=FindResource(hInst,MAKEINTRESOURCE(dlgId),RT_DIALOG);
		if (!hrsrc)
		{
			hInst=g_Instance;
			hrsrc=FindResource(hInst,MAKEINTRESOURCE(dlgId),RT_DIALOG);
		}
		if (hrsrc)
		{
			HGLOBAL hglb=LoadResource(hInst,hrsrc);
			if (hglb)
			{
				// finally lock the resource
				LPVOID res=LockResource(hglb);
				std::vector<char> &dlg=g_ResDialogs[dlgId];
				dlg.resize(SizeofResource(hInst,hrsrc));
				if (!dlg.empty())
					memcpy(&dlg[0],res,dlg.size());
			}
		}
	}
}

// Loads the best DLL for the current language
HINSTANCE LoadTranslationDll( const wchar_t *langSetting )
{
	Assert(g_Instance);
	CString language=langSetting;
	wchar_t languages[100]={0};
	if (!language.IsEmpty())
	{
		Strcpy(languages,_countof(languages)-1,language);
	}
	else
	{
		ULONG size=0;
		ULONG len=_countof(languages);
		GetUserPreferredUILanguages(MUI_LANGUAGE_NAME,&size,languages,&len);
	}

	wchar_t path1[_MAX_PATH];
	GetModuleFileName(g_Instance,path1,_countof(path1));
	*PathFindFileName(path1)=0;

	wchar_t path2[_MAX_PATH]=L"%ALLUSERSPROFILE%\\OpenShell\\Languages\\";
	DoEnvironmentSubst(path2,_countof(path2));

	DWORD curVersion=GetVersionEx(g_Instance);
	HINSTANCE resInstance=NULL;
	for (const wchar_t *lang=languages;*lang;lang+=Strlen(lang)+1)
	{
		if (_wcsicmp(lang,L"en")==0 || _wcsnicmp(lang,L"en-",3)==0)
			break; // English
		wchar_t fname[_MAX_PATH];
		DWORD ver1=0, ver2=0, build1=0, build2=0;
		Sprintf(fname,_countof(fname),L"%s%s.dll",path1,lang);
		HINSTANCE resInstance1=LoadLibraryEx(fname,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
		if (resInstance1)
		{
			ver1=GetVersionEx(resInstance1,&build1);
			if (ver1<MIN_LANGUAGE_VERSION || ver1>curVersion)
			{
				ver1=0;
				FreeLibrary(resInstance1);
				resInstance1=NULL;
			}
		}
		Sprintf(fname,_countof(fname),L"%s%s.dll",path2,lang);
		HINSTANCE resInstance2=LoadLibraryEx(fname,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
		if (resInstance2)
		{
			ver2=GetVersionEx(resInstance2,&build2);
			if (ver2<MIN_LANGUAGE_VERSION || ver2>curVersion)
			{
				ver2=0;
				FreeLibrary(resInstance2);
				resInstance2=NULL;
			}
		}
		if (ver1 || ver2)
		{
			if (ver1>ver2 || (ver1==ver2 && build1>build2))
			{
				resInstance=resInstance1;
				if (resInstance2) FreeLibrary(resInstance2);
			}
			else
			{
				resInstance=resInstance2;
				if (resInstance1) FreeLibrary(resInstance1);
			}
			break;
		}
	}
	return resInstance;
}

// Returns a localized string
CString LoadStringEx( int stringID )
{
	CString str=g_ResStrings.GetString(stringID);
	if (str.IsEmpty())
		str.LoadString(g_Instance,stringID);
	return str;
}

CString LoadStringEnglish( int stringID )
{
	CString str;
	str.LoadString(g_Instance,stringID);
	return str;
}

// Returns a localized dialog template
DLGTEMPLATE *LoadDialogEx( int dlgID )
{
	std::map<int,std::vector<char> >::iterator it=g_ResDialogs.find(dlgID);
	if (it==g_ResDialogs.end())
		return NULL;
	if (it->second.empty())
		return NULL;
	return (DLGTEMPLATE*)&it->second[0];
}

// Loads an icon. path can be a path to .ico file, or in the format "module.dll, number"
HICON LoadIcon( int iconSize, const wchar_t *path, std::vector<HMODULE> &modules )
{
	wchar_t text[1024];
	Strcpy(text,_countof(text),path);
	DoEnvironmentSubst(text,_countof(text));
	wchar_t *c=wcsrchr(text,',');
	if (c)
	{
		// resource file
		*c=0;
		const wchar_t *res=c+1;
		int idx=_wtol(res);
		if (idx>0) res=MAKEINTRESOURCE(idx);
		if (!text[0])
			return (HICON)LoadImage(_AtlBaseModule.GetResourceInstance(),res,IMAGE_ICON,iconSize,iconSize,LR_DEFAULTCOLOR);
		HMODULE hMod=GetModuleHandle(PathFindFileName(text));
		if (!hMod)
		{
			hMod=LoadLibraryEx(text,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
			if (!hMod) return NULL;
			modules.push_back(hMod);
		}
		return (HICON)LoadImage(hMod,res,IMAGE_ICON,iconSize,iconSize,LR_DEFAULTCOLOR);
	}
	else
	{
		return (HICON)LoadImage(NULL,text,IMAGE_ICON,iconSize,iconSize,LR_DEFAULTCOLOR|LR_LOADFROMFILE);
	}
}

HICON LoadIcon( int iconSize, PIDLIST_ABSOLUTE pidl )
{
	HICON hIcon=NULL;
	CComPtr<IShellFolder> pFolder;
	PCITEMID_CHILD child;
	if (SUCCEEDED(SHBindToParent(pidl,IID_IShellFolder,(void**)&pFolder,&child)))
	{
		bool bLarge=(iconSize>GetSystemMetrics(SM_CXSMICON));
		LONG lSize;
		if (bLarge)
			lSize=MAKELONG(iconSize,GetSystemMetrics(SM_CXSMICON));
		else
			lSize=MAKELONG(GetSystemMetrics(SM_CXICON),iconSize);
		CComPtr<IExtractIcon> pExtract;
		if (SUCCEEDED(pFolder->GetUIObjectOf(NULL,1,&child,IID_IExtractIcon,NULL,(void**)&pExtract)))
		{
			// get the icon location
			wchar_t location[_MAX_PATH];
			int index=0;
			UINT flags=0;
			if (pExtract->GetIconLocation(0,location,_countof(location),&index,&flags)==S_OK)
			{
				if (flags&GIL_NOTFILENAME)
				{
					// extract the icon
					HICON hIcon2=NULL;
					HRESULT hr=pExtract->Extract(location,index,bLarge?&hIcon:&hIcon2,bLarge?&hIcon2:&hIcon,lSize);
					if (FAILED(hr))
						hIcon=hIcon2=NULL;
					if (hr==S_FALSE)
						flags=0;
					if (hIcon2) DestroyIcon(hIcon2); // HACK!!! Even though Extract should support NULL, not all implementations do. For example shfusion.dll crashes
				}
				if (!(flags&GIL_NOTFILENAME))
				{
					if (ExtractIconEx(location,index==-1?0:index,bLarge?&hIcon:NULL,bLarge?NULL:&hIcon,1)!=1)
						hIcon=NULL;
				}
			}
		}
		else
		{
			// try again using the ANSI version
			CComPtr<IExtractIconA> pExtractA;
			if (SUCCEEDED(pFolder->GetUIObjectOf(NULL,1,&child,IID_IExtractIconA,NULL,(void**)&pExtractA)))
			{
				// get the icon location
				char location[_MAX_PATH];
				int index=0;
				UINT flags=0;
				if (pExtractA->GetIconLocation(0,location,_countof(location),&index,&flags)==S_OK)
				{
					if (flags&GIL_NOTFILENAME)
					{
						// extract the icon
						HICON hIcon2=NULL;
						HRESULT hr=pExtractA->Extract(location,index,bLarge?&hIcon:&hIcon2,bLarge?&hIcon2:&hIcon,lSize);
						if (FAILED(hr))
							hIcon=hIcon2=NULL;
						if (hr==S_FALSE)
							flags=0;
						if (hIcon2) DestroyIcon(hIcon2); // HACK!!! Even though Extract should support NULL, not all implementations do. For example shfusion.dll crashes
					}
					if (!(flags&GIL_NOTFILENAME))
					{
						if (ExtractIconExA(location,index==-1?0:index,bLarge?&hIcon:NULL,bLarge?NULL:&hIcon,1)!=1)
							hIcon=NULL;
					}
				}
			}
		}
	}

	return hIcon;
}

HICON ShExtractIcon( const wchar_t *path, int index, int iconSize )
{
	HICON hIcon;

	typedef UINT (WINAPI *FSHExtractIconsW)( LPCWSTR pszFileName, int nIconIndex, int cxIcon, int cyIcon, HICON *phIcon, UINT *pIconId, UINT nIcons, UINT flags );
	static FSHExtractIconsW s_SHExtractIconsW;

	if (!s_SHExtractIconsW)
	{
		HMODULE hShell32=GetModuleHandle(L"Shell32.dll");
		if (hShell32)
			s_SHExtractIconsW=(FSHExtractIconsW)GetProcAddress(hShell32,"SHExtractIconsW");
	}

	if (s_SHExtractIconsW)
	{
		UINT id;
		if (!s_SHExtractIconsW(path,index,iconSize,iconSize,&hIcon,&id,1,LR_DEFAULTCOLOR))
			hIcon=NULL;
	}
	else
	{
		if (ExtractIconEx(path,index,&hIcon,NULL,1)!=1)
			return NULL;
	}
	if (hIcon)
	{
		AddTrackedIcon(hIcon);
	}
	return hIcon;
}

HICON ShExtractIcon( const char *path, int index, int iconSize )
{
	wchar_t pathW[_MAX_PATH];
	MbsToWcs(pathW,_countof(pathW),path);
	return ShExtractIcon(pathW,index,iconSize);
}

// Converts an icon to a bitmap. pBits may be NULL. If bDestroyIcon is true, hIcon will be destroyed
HBITMAP BitmapFromIcon( HICON hIcon, int iconSize, unsigned int **pBits, bool bDestroyIcon )
{
	BITMAPINFO bi={0};
	bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth=bi.bmiHeader.biHeight=iconSize;
	bi.bmiHeader.biPlanes=1;
	bi.bmiHeader.biBitCount=32;
	RECT rc={0,0,iconSize,iconSize};

	HDC hdc=CreateCompatibleDC(NULL);
	unsigned int *bits;
	HBITMAP bmp=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,(void**)&bits,NULL,0);
	HGDIOBJ bmp0=SelectObject(hdc,bmp);
	FillRect(hdc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
	DrawIconEx(hdc,0,0,hIcon,iconSize,iconSize,0,NULL,DI_NORMAL);
	SelectObject(hdc,bmp0);
	DeleteDC(hdc);
	if (bDestroyIcon) DestroyIcon(hIcon);
	if (pBits) *pBits=bits;
	return bmp;
}

// Premultiplies a DIB section by the alpha channel and a given color
void PremultiplyBitmap( HBITMAP hBitmap, COLORREF rgb )
{
	if (hBitmap == NULL) return;
	BITMAP info;
	GetObject(hBitmap,sizeof(info),&info);
	int n=info.bmWidth*info.bmHeight;
	int mr=(rgb)&255;
	int mg=(rgb>>8)&255;
	int mb=(rgb>>16)&255;
	// pre-multiply the alpha
	for (int i=0;i<n;i++)
	{
		unsigned int &pixel=((unsigned int*)info.bmBits)[i];
		int a=(pixel>>24);
		int r=(pixel>>16)&255;
		int g=(pixel>>8)&255;
		int b=(pixel)&255;
		r=(r*a*mr)/(255*255);
		g=(g*a*mg)/(255*255);
		b=(b*a*mb)/(255*255);
		pixel=(a<<24)|(r<<16)|(g<<8)|b;
	}
}

// Creates a grayscale version of an icon
HICON CreateDisabledIcon( HICON hIcon, int iconSize )
{
	// convert normal icon to grayscale
	ICONINFO info;
	GetIconInfo(hIcon,&info);

	unsigned int *bits;
	HBITMAP bmp=BitmapFromIcon(hIcon,iconSize,&bits,false);

	int n=iconSize*iconSize;
	for (int i=0;i<n;i++)
	{
		unsigned int &pixel=bits[i];
		int r=(pixel&255);
		int g=((pixel>>8)&255);
		int b=((pixel>>16)&255);
		int l=(77*r+151*g+28*b)/256;
		pixel=(pixel&0xFF000000)|(l*0x010101);
	}

	if (info.hbmColor) DeleteObject(info.hbmColor);
	info.hbmColor=bmp;
	hIcon=CreateIconIndirect(&info);
	DeleteObject(bmp);
	if (info.hbmMask) DeleteObject(info.hbmMask);
	return hIcon;
}

// Loads an image file into a bitmap and optionally resizes it
HBITMAP LoadImageFile( const wchar_t *path, const SIZE *pSize, bool bUseAlpha, bool bPremultiply, std::vector<unsigned int> *pButtonAnim )
{
	HBITMAP srcBmp=NULL;
	if (_wcsicmp(PathFindExtension(path),L".bmp")==0)
	{
		srcBmp=(HBITMAP)LoadImage(NULL,path,IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION|LR_LOADFROMFILE);
	}
	if (srcBmp && !pSize)
		return srcBmp;
	CComPtr<IWICImagingFactory> pFactory;
	if (FAILED(pFactory.CoCreateInstance(CLSID_WICImagingFactory)))
	{
		if (FAILED(pFactory.CoCreateInstance(CLSID_WICImagingFactory1)))
		{
			if (srcBmp) DeleteObject(srcBmp);
			return NULL;
		}
	}

	CComPtr<IWICBitmapSource> pBitmap;
	if (srcBmp)
	{
		CComPtr<IWICBitmap> pBitmap2;
		if (FAILED(pFactory->CreateBitmapFromHBITMAP(srcBmp,NULL,bUseAlpha?WICBitmapUseAlpha:WICBitmapIgnoreAlpha,&pBitmap2)))
		{
			DeleteObject(srcBmp);
			return NULL;
		}
		pBitmap=pBitmap2;
		DeleteObject(srcBmp);
	}
	else
	{
		CComPtr<IWICBitmapDecoder> pDecoder;
		if (FAILED(pFactory->CreateDecoderFromFilename(path,NULL,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&pDecoder)))
			return NULL;

		CComPtr<IWICBitmapFrameDecode> pFrame;
		if (FAILED(pDecoder->GetFrame(0,&pFrame)))
			return NULL;
		pBitmap=pFrame;
	}

	CComPtr<IWICFormatConverter> pConverter;
	if (FAILED(pFactory->CreateFormatConverter(&pConverter)))
		return NULL;
	pConverter->Initialize(pBitmap,bPremultiply?GUID_WICPixelFormat32bppPBGRA:GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,NULL,0,WICBitmapPaletteTypeMedianCut);
	pBitmap=pConverter;

	int frameCount=1;
	int Yoffset=0;
	if (pButtonAnim)
	{
		frameCount=3;
		UINT width=0, height=0;
		pBitmap->GetSize(&width,&height);
		if (width>=3)
		{
			unsigned int data[3];
			WICRect rect={0,0,3,1};
			if (SUCCEEDED(pBitmap->CopyPixels(&rect,12,12,(BYTE*)data)) && (data[0]&0xFFFFFF)==ANIM_BUTTON_TAG1 && (data[1]&0xFFFFFF)==ANIM_BUTTON_TAG2)
			{
				Yoffset=(data[2]>>16)&255;
				if (Yoffset<(int)height)
				{
					frameCount=data[2]&255;
					if (frameCount<1) frameCount=1;
					if (frameCount>(int)(height-1)) frameCount=height-1;
					pButtonAnim->resize(Yoffset*width);
					rect.Width=width;
					rect.Height=Yoffset;
					pBitmap->CopyPixels(&rect,width*4,Yoffset*width*4,(BYTE*)&(*pButtonAnim)[0]);
				}
			}
		}
	}

	int frameWidthS, frameHeightS;
	int frameWidthD, frameHeightD;
	{
		UINT width=0, height=0;
		pBitmap->GetSize(&width,&height);
		frameWidthS=width;
		frameHeightS=(height-Yoffset)/frameCount;
		frameWidthD=frameWidthS;
		frameHeightD=frameHeightS;

		// if cx>0, use as width
		// if cx<0, use -cx as width limit
		// if cy==0, scale proportionally
		if (pSize && pSize->cx && (pSize->cx>0 || -pSize->cx<frameWidthS))
		{
			frameWidthD=abs(pSize->cx);
			if (pSize->cy)
				frameHeightD=pSize->cy;
			else
				frameHeightD=frameWidthD*frameHeightS/frameWidthS;
		}
	}

	BITMAPINFO bi={0};
	bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth=frameWidthD;
	bi.bmiHeader.biHeight=-frameHeightD*frameCount;
	bi.bmiHeader.biPlanes=1;
	bi.bmiHeader.biBitCount=32;

	HDC hdc=CreateCompatibleDC(NULL);
	BYTE *pBits;
	HBITMAP bmp=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,(void**)&pBits,NULL,0);
	DeleteDC(hdc);

	for (int frame=0;frame<frameCount;frame++)
	{
		CComPtr<IWICBitmapSource> pFrame=pBitmap;
		if (frameCount>1)
		{
			CComPtr<IWICBitmapClipper> pClipper;
			if (FAILED(pFactory->CreateBitmapClipper(&pClipper)))
			{
				DeleteObject(bmp);
				bmp=NULL;
				break;
			}
			WICRect rect={0,frameHeightS*frame+Yoffset,frameWidthS,frameHeightS};
			pClipper->Initialize(pBitmap,&rect);
			pFrame=pClipper;
		}
		if (frameWidthS!=frameWidthD || frameHeightS!=frameHeightD)
		{
			CComPtr<IWICBitmapScaler> pScaler;
			if (FAILED(pFactory->CreateBitmapScaler(&pScaler)))
			{
				DeleteObject(bmp);
				bmp=NULL;
				break;
			}
			pScaler->Initialize(pFrame,frameWidthD,frameHeightD,WICBitmapInterpolationModeFant);
			pFrame=pScaler;
		}
		int stride=frameWidthD*4;
		int frameSize=frameHeightD*stride;
		pFrame->CopyPixels(NULL,stride,frameSize,pBits+frameSize*frame);
	}

	return bmp;
}

// Loads a bitmap from a IMAGE resource
HBITMAP LoadImageResource( HMODULE hModule, const wchar_t *name, bool bTopDown, bool bPremultiply )
{
	CComPtr<IWICImagingFactory> pFactory;
	if (FAILED(pFactory.CoCreateInstance(CLSID_WICImagingFactory)))
	{
		if (FAILED(pFactory.CoCreateInstance(CLSID_WICImagingFactory1)))
			return NULL;
	}

	CComPtr<IWICBitmapSource> pBitmap;
	if (hModule)
	{
		HRSRC src=FindResource(hModule,name,L"IMAGE");
		if (!src) return NULL;
		HGLOBAL hGlobal=LoadResource(hModule,src);
		if (!hGlobal) return NULL;
		void *pngData=LockResource(hGlobal);
		if (!pngData) return NULL;
		DWORD size=SizeofResource(hModule,src);
		CComPtr<IStream> pStream;
		pStream.Attach(SHCreateMemStream((BYTE*)pngData,size));
		if (!pStream) return NULL;

		CComPtr<IWICBitmapDecoder> pDecoder;
		if (FAILED(pFactory->CreateDecoderFromStream(pStream,NULL,WICDecodeMetadataCacheOnLoad,&pDecoder)))
			return NULL;
		CComPtr<IWICBitmapFrameDecode> pFrame;
		if (FAILED(pDecoder->GetFrame(0,&pFrame)))
			return NULL;
		pBitmap=pFrame;
	}
	else
	{
		if (_wcsicmp(PathFindExtension(name),L".bmp")==0)
		{
			HBITMAP bmp=(HBITMAP)LoadImage(NULL,name,IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION|LR_LOADFROMFILE);
			if (!bmp)
				return NULL;
			CComPtr<IWICBitmap> pBitmap2;
			if (FAILED(pFactory->CreateBitmapFromHBITMAP(bmp,NULL,WICBitmapUseAlpha,&pBitmap2)))
				pBitmap2=NULL;
			DeleteObject(bmp);
			if (!pBitmap2)
				return NULL;
			pBitmap=pBitmap2;
		}
		else
		{
			CComPtr<IWICBitmapDecoder> pDecoder;
			if (FAILED(pFactory->CreateDecoderFromFilename(name,NULL,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&pDecoder)))
				return NULL;
			CComPtr<IWICBitmapFrameDecode> pFrame;
			if (FAILED(pDecoder->GetFrame(0,&pFrame)))
				return NULL;
			pBitmap=pFrame;
		}
	}

	CComPtr<IWICFormatConverter> pConverter;
	if (FAILED(pFactory->CreateFormatConverter(&pConverter)))
		return NULL;
	pConverter->Initialize(pBitmap,bPremultiply?GUID_WICPixelFormat32bppPBGRA:GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,NULL,0,WICBitmapPaletteTypeMedianCut);

	UINT width=0, height=0;
	pConverter->GetSize(&width,&height);

	BITMAPINFO bi={0};
	bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth=width;
	bi.bmiHeader.biHeight=bTopDown?-(int)height:(int)height;
	bi.bmiHeader.biPlanes=1;
	bi.bmiHeader.biBitCount=32;

	HDC hdc=CreateCompatibleDC(NULL);
	BYTE *bits;
	HBITMAP bmp=CreateDIBSection(hdc,&bi,DIB_RGB_COLORS,(void**)&bits,NULL,0);
	DeleteDC(hdc);

	if (bTopDown)
	{
		pConverter->CopyPixels(NULL,width*4,width*height*4,bits);
	}
	else
	{
		for (UINT y=0;y<height;y++)
		{
			WICRect rc={0,(INT)y,(INT)width,1};
			pConverter->CopyPixels(&rc,width*4,width*4,bits+(height-y-1)*width*4);
		}
	}

	return bmp;
}

// Returns the version of a given module
DWORD GetVersionEx( HINSTANCE hInstance, DWORD *pBuild )
{
	// get the DLL version. this is a bit hacky. the standard way is to use GetFileVersionInfo and such API.
	// but it takes a file name instead of module handle so it will probably load the DLL a second time.
	// the header of the version resource is a fixed size so we can count on VS_FIXEDFILEINFO to always
	// be at offset 40
	HRSRC hResInfo=FindResource(hInstance,MAKEINTRESOURCE(VS_VERSION_INFO),RT_VERSION);
	if (!hResInfo)
		return 0;
	HGLOBAL hRes=LoadResource(hInstance,hResInfo);
	void *pRes=LockResource(hRes);
	if (!pRes) return 0;

	VS_FIXEDFILEINFO *pVer=(VS_FIXEDFILEINFO*)((char*)pRes+40);
	if (pBuild)
		*pBuild=LOWORD(pVer->dwProductVersionLS);
	return ((HIWORD(pVer->dwProductVersionMS)&255)<<24)|((LOWORD(pVer->dwProductVersionMS)&255)<<16)|HIWORD(pVer->dwProductVersionLS);
}

// Returns the Windows version - 0x600, 0x601, ...
WORD GetWinVersion( void )
{
	static WORD version;
	if (!version)
	{
		#pragma warning(suppress:4996)
		DWORD ver=GetVersion();
		version=MAKEWORD(HIBYTE(ver),LOBYTE(ver));
	}
	return version;
}

static bool IsWin81Update1Helper( void )
{
	DWORD build;
	DWORD ver=GetVersionEx(GetModuleHandle(L"user32.dll"),&build);
	return HIWORD(ver)==WIN_VER_WIN81 && build>17000;
}

// Returns true if the version is Win81 Update1 or later
bool IsWin81Update1( void )
{
	static bool bIsUpdate1=IsWin81Update1Helper();
	return bIsUpdate1;
}

static bool IsWin10RS1Helper( void )
{
	DWORD build;
	DWORD ver=GetVersionEx(GetModuleHandle(L"user32.dll"),&build);
	return ver>MAKELONG(14000,10<<8);
}

// Returns true if the version is Windows10 RS1 or later
bool IsWin10RS1( void )
{
	static bool bIsRS1=IsWin10RS1Helper();
	return bIsRS1;
}

typedef LONG NTSTATUS, *PNTSTATUS;
#define STATUS_SUCCESS (0x00000000)
typedef NTSTATUS (WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
// *DO NOT USE DIRECTLY* : Call GetOSVersion() instead.
//
// The functions above return a windows version
// that is rather not user-readable. This code should give
// us a number we can reference with the "public" windows builds
// such as what is returned by  'winver.exe'.
RTL_OSVERSIONINFOW GetRealOSVersion() {
	HMODULE hMod = ::GetModuleHandleW(L"ntdll.dll");
	if (hMod) {
		RtlGetVersionPtr fxPtr = (RtlGetVersionPtr)::GetProcAddress(hMod, "RtlGetVersion");
		if (fxPtr != nullptr) {
			RTL_OSVERSIONINFOW rovi = { 0 };
			rovi.dwOSVersionInfoSize = sizeof(rovi);
			if ( STATUS_SUCCESS == fxPtr(&rovi) ) {
				return rovi;
			}
		}
	}
	RTL_OSVERSIONINFOW rovi = { 0 };
	return rovi;
}

static RTL_OSVERSIONINFOW GetOSVersion()
{
	// cache result to avoid calling the dll multiple times.
	static RTL_OSVERSIONINFOW ver = GetRealOSVersion();
	return ver;
}

static bool IsWin10RS4Helper( void )
{
	auto version = GetOSVersion();
	return version.dwMajorVersion > 8 && version.dwBuildNumber > 17131;
}

// Returns true if the version is Windows10 RS4 (Spring Creator Update) or later
bool IsWin10RS4( void )
{
	static bool bIsRS4=IsWin10RS4Helper();
	return bIsRS4;
}

static bool IsWin11Helper()
{
	auto version = GetOSVersion();
	return version.dwMajorVersion >= 10 && version.dwBuildNumber >= 22000;
}

// Returns true if the version is Windows11 or later
bool IsWin11(void)
{
	static bool bIsWin11 = IsWin11Helper();
	return bIsWin11;
}

// Wrapper for IShellFolder::ParseDisplayName
HRESULT ShParseDisplayName( const wchar_t *pszName, PIDLIST_ABSOLUTE *ppidl, SFGAOF sfgaoIn, SFGAOF *psfgaoOut )
{
	static ITEMIDLIST ilRoot={0};
	static CComPtr<IShellFolder> pDesktop;
	if (_wcsicmp(pszName,L"::{Desktop}")==0)
	{
		*ppidl=ILCloneFull((PIDLIST_ABSOLUTE)&ilRoot);
		if (psfgaoOut)
		{
			*psfgaoOut=0;
			if (sfgaoIn&SFGAO_FOLDER)
				*psfgaoOut|=SFGAO_FOLDER;
		}
		return S_OK;
	}
	else
	{
		*ppidl=NULL;
		if (!pDesktop)
		{
			HRESULT hr=SHGetDesktopFolder(&pDesktop);
			if (FAILED(hr))
				return hr;
		}
		SFGAOF flags=sfgaoIn;
		HRESULT hr=pDesktop->ParseDisplayName(NULL,NULL,(wchar_t*)pszName,NULL,(PIDLIST_RELATIVE*)ppidl,psfgaoOut?&flags:NULL);
		if (FAILED(hr))
			return hr;
		if (psfgaoOut)
			*psfgaoOut=flags;
		return hr;
	}
}

// Separates the arguments from the program
// May return NULL if no arguments are found
const wchar_t *SeparateArguments( const wchar_t *command, wchar_t *program )
{
	if (command[0]=='"')
	{
		// quoted program - just GetToken will work
		return GetToken(command,program,_MAX_PATH,L" ");
	}

	// skip leading spaces
	while (*command==' ')
		command++;
	const wchar_t *args=wcschr(command,' ');
	if (!args)
	{
		// no spaces - the whole thing is a program
		Strcpy(program,_MAX_PATH,command);
		return NULL;
	}

	int len=(int)(args-command);
	if (len>_MAX_PATH-1) len=_MAX_PATH-1;
	memcpy(program,command,len*2);
	program[len]=0;

	const wchar_t *space=command;
	while (*space)
	{
		space=wcschr(space+1,' ');
		if (!space)
			space=command+Strlen(command);
		len=(int)(space-command);
		if (len>=_MAX_PATH) break;
		wchar_t prog2[_MAX_PATH];
		memcpy(prog2,command,len*2);
		prog2[len]=0;
		if (len>0 && prog2[len-1]=='\\')
			prog2[len-1]=0;
		WIN32_FIND_DATA data;
		HANDLE h=FindFirstFile(prog2,&data);
		if (h!=INVALID_HANDLE_VALUE)
		{
			// found a valid file
			FindClose(h);
			memcpy(program,command,len*2);
			program[len]=0;
			if (*space)
				args=space+1;
			else
				args=NULL;
		}
	}

	while (args && *args==' ')
		args++;
	return args;
}
struct CommonEnvVar
{
	const wchar_t *name;
	wchar_t value[_MAX_PATH];
	int len;
};

CommonEnvVar g_CommonEnvVars[]={
	{L"USERPROFILE"},
	{L"ALLUSERSPROFILE"},
	{L"SystemRoot"},
	{L"SystemDrive"},
};

void UnExpandEnvStrings( const wchar_t *src, wchar_t *dst, int size )
{
	static bool bInit=false;
	if (!bInit)
	{
		bInit=true;
		for (int i=0;i<_countof(g_CommonEnvVars);i++)
		{
			int len=GetEnvironmentVariable(g_CommonEnvVars[i].name,g_CommonEnvVars[i].value,_MAX_PATH);
			if (len<=_MAX_PATH)
				g_CommonEnvVars[i].len=len;
		}
	}

	for (int i=0;i<_countof(g_CommonEnvVars);i++)
	{
		int len=g_CommonEnvVars[i].len;
		if (_wcsnicmp(src,g_CommonEnvVars[i].value,len)==0)
		{
			const wchar_t *name=g_CommonEnvVars[i].name;
			if (Strlen(src)-len+Strlen(name)+3>size)
				break; // not enough space
			Sprintf(dst,size,L"%%%s%%%s",name,src+len);
			return;
		}
	}
	Strcpy(dst,size,src);
}

void StringUpper( CString &str )
{
	int len=str.GetLength();
	wchar_t *buf=str.GetBuffer(len);
	CharUpper(buf);
	str.ReleaseBufferSetLength(len);
}

// Create a font from the user settings
HFONT CreateFontSetting( const wchar_t *fontStr, int dpi )
{
	const wchar_t *str=fontStr;
	if (!*str) return NULL;

	wchar_t name[256];
	while (*str==' ')
		str++;
	str=GetToken(str,name,_countof(name),L",");
	int len=Strlen(name);
	while (len>0 && name[len-1]==' ')
		name[--len]=0;
	while (*str==' ')
		str++;
	wchar_t token[256];
	str=GetToken(str,token,_countof(token),L",");
	len=Strlen(token);
	while (len>0 && token[len-1]==' ')
		token[--len]=0;
	int weight=FW_NORMAL;
	bool bItalic=false;
	if (_wcsicmp(token,L"bold")==0)
		weight=FW_BOLD;
	else if (_wcsicmp(token,L"italic")==0)
		bItalic=1;
	else if (_wcsicmp(token,L"bold_italic")==0)
		weight=FW_BOLD, bItalic=true;
	str=GetToken(str,token,_countof(token),L", \t");
	int size=-_wtol(token);
	return CreateFont(MulDiv(size,dpi,72),0,0,0,weight,bItalic?1:0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,name);
}

static UINT WINAPI GetDpiForWindow(HWND hwnd)
{
	static auto p = static_cast<decltype(&GetDpiForWindow)>((void*)GetProcAddress(GetModuleHandle(L"user32.dll"), "GetDpiForWindow"));
	if (p)
		return p(hwnd);

	return 0;
}

UINT GetDpi(HWND hwnd)
{
	UINT dpi = GetDpiForWindow(hwnd);
	if (!dpi)
	{
		// fall-back for older systems
		HDC hdc = GetDC(nullptr);
		dpi = GetDeviceCaps(hdc, LOGPIXELSY);
		ReleaseDC(nullptr, hdc);
	}

	return dpi;
}

int ScaleForDpi(HWND hwnd, int value)
{
	return MulDiv(value, GetDpi(hwnd), USER_DEFAULT_SCREEN_DPI);
}
