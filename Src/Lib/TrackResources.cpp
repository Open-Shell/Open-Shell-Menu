#include <stdafx.h>
#include <map>
#include <vector>
#include "StringUtils.h"
#include "ResourceHelper.h"
#include "Assert.h"

typedef std::pair<const char*,int> TrackedResource;
typedef std::map<HGDIOBJ,TrackedResource> TrackedResourceMap;

static TrackedResourceMap g_TrackedGdiResources;
static TrackedResourceMap g_TrackedUserResources;

HDC TrackCreateCompatibleDC( HDC hdc, const char *file, int line )
{
	HDC res=CreateCompatibleDC(hdc);
	if (res)
		g_TrackedGdiResources[res]=TrackedResource(file,line);
	return res;
}

HFONT TrackCreateFont( int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName, const char *file, int line )
{
	HFONT res=CreateFont(cHeight,cWidth,cEscapement,cOrientation,cWeight,bItalic,bUnderline,bStrikeOut,iCharSet,iOutPrecision,iClipPrecision,iQuality,iPitchAndFamily,pszFaceName);
	if (res)
		g_TrackedGdiResources[res]=TrackedResource(file,line);
	return res;
}

HFONT TrackCreateFontIndirect( const LOGFONT *lplf, const char *file, int line )
{
	HFONT res=CreateFontIndirect(lplf);
	if (res)
		g_TrackedGdiResources[res]=TrackedResource(file,line);
	return res;
}

HANDLE TrackLoadImage( HINSTANCE hInst, LPCWSTR name, UINT type, int cx, int cy, UINT fuLoad, const char *file, int line )
{
	HANDLE res=LoadImage(hInst,name,type,cx,cy,fuLoad);
	if (type==IMAGE_BITMAP)
		g_TrackedGdiResources[res]=TrackedResource(file,line);
	else
		g_TrackedUserResources[res]=TrackedResource(file,line);
	return res;
}

HBITMAP TrackCreateDIBSection( HDC hdc, CONST BITMAPINFO *lpbmi, UINT usage, VOID **ppvBits, HANDLE hSection, DWORD offset, const char *file, int line )
{
	HBITMAP res=CreateDIBSection(hdc,lpbmi,usage,ppvBits,hSection,offset);
	if (res)
		g_TrackedGdiResources[res]=TrackedResource(file,line);
	return res;
}

HBITMAP TrackCreateBitmap( int nWidth, int nHeight, UINT nPlanes, UINT nBitCount, CONST VOID *lpBits, const char *file, int line )
{
	HBITMAP res=CreateBitmap(nWidth,nHeight,nPlanes,nBitCount,lpBits);
	if (res)
		g_TrackedGdiResources[res]=TrackedResource(file,line);
	return res;
}

HBITMAP TrackCreateCompatibleBitmap( HDC hdc, int cx, int cy, const char *file, int line )
{
	HBITMAP res=CreateCompatibleBitmap(hdc,cx,cy);
	if (res)
		g_TrackedGdiResources[res]=TrackedResource(file,line);
	return res;
}

void TrackAddTrackedObject( HGDIOBJ obj, const char *file, int line )
{
	if (obj)
		g_TrackedGdiResources[obj]=TrackedResource(file,line);
}

void TrackAddTrackedIcon( HICON hIcon, const char *file, int line )
{
	if (hIcon)
		g_TrackedUserResources[hIcon]=TrackedResource(file,line);
}



BOOL TrackDeleteDC( HDC hdc )
{
	Assert(hdc);
	if (g_TrackedGdiResources.find(hdc)!=g_TrackedGdiResources.end())
		g_TrackedGdiResources.erase(hdc);
	else
		Assert(0);
	return DeleteDC(hdc);
}

BOOL TrackDeleteObject( HGDIOBJ obj )
{
	Assert(obj);
	if (g_TrackedGdiResources.find(obj)!=g_TrackedGdiResources.end())
		g_TrackedGdiResources.erase(obj);
	else
		Assert(0);
	return DeleteObject(obj);
}

BOOL TrackDestroyIcon( HICON hIcon )
{
	Assert(hIcon);
	if (g_TrackedUserResources.find(hIcon)!=g_TrackedUserResources.end())
		g_TrackedUserResources.erase(hIcon);
	else
		Assert(0);
	return DestroyIcon(hIcon);
}


static void DumpBitmapLeaks( const std::vector<HBITMAP> &bitmaps )
{
	int index=0;
	for (std::vector<HBITMAP>::const_iterator it=bitmaps.begin();it!=bitmaps.end();++it)
	{
		BITMAP info;
		GetObject(*it,sizeof(info),&info);
		// ... 
	}
}


struct GdiTableCell
{
	void *pKernel;
	unsigned short nProcess;
	unsigned short nCount;
	unsigned short nUpper;
	unsigned short nType;
	void *pUser;
};

static const GdiTableCell *GetGdiTable( void )
{
	HMODULE gdi32=GetModuleHandle(L"gdi32.dll");
	FARPROC GdiQueryTable=GetProcAddress(gdi32,"GdiQueryTable");
	if (GdiQueryTable)
	{
		// GdiQueryTable();
		if (GetWinVersion()>= _WIN32_WINNT_WIN8)
			return *(GdiTableCell**)((char*)GdiQueryTable+0x6b1b0);
		else
			return *(GdiTableCell**)((char*)GdiQueryTable+0x29db0);
	}
	return NULL;
}

void DumpResourceLeaks( void )
{
	GdiFlush();
	Trace("GDI objects %d",GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS));
	Trace("USER objects %d",GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS));
	const GdiTableCell *cells=GetGdiTable();
	if (!cells) return;

	int counts[16]={0};
	unsigned short pid=(unsigned short)GetCurrentProcessId();
	int n=65536;
	std::vector<HBITMAP> bitmaps;
	for (int i=0;i<n;i++)
	{
		if (cells[i].pKernel && cells[i].nProcess==pid)
		{
			HGDIOBJ handle=(HGDIOBJ)(intptr_t)((cells[i].nUpper<<16)|i);
			const TrackedResource *res=NULL;
			TrackedResourceMap::const_iterator it=g_TrackedGdiResources.find(handle);
			if (it!=g_TrackedGdiResources.end())
				res=&it->second;
			DWORD type=GetObjectType(handle);
			if (!(type&~15))
				counts[type]++;
			switch (type)
			{
				case OBJ_BITMAP:
					{
						BITMAP info;
						GetObject(handle,sizeof(info),&info);
						bitmaps.push_back((HBITMAP)handle);
					}
					break;
				case OBJ_BRUSH:
					{
						LOGBRUSH info;
						GetObject(handle,sizeof(info),&info);
						int q=0;
					}
					break;
				case OBJ_DC:
					{
						int q=0;
					}
					break;
				case OBJ_FONT:
					{
						LOGFONT info;
						GetObject(handle,sizeof(info),&info);
						int q=0;
					}
					break;
				case OBJ_PEN:
					{
						LOGPEN info;
						GetObject(handle,sizeof(info),&info);
						int q=0;
					}
					break;
				case OBJ_REGION:
					{
						int q=0;
					}
					break;
				case OBJ_MEMDC:
					{
						int q=0;
					}
					break;
			}
		}
	}
	DumpBitmapLeaks(bitmaps);
}
