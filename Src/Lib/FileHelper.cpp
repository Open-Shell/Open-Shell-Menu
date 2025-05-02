#include "stdafx.h"
#include "StringUtils.h"

bool CreateFakeFolder( const wchar_t *source, const wchar_t *fname )
{
	// create directory
	CreateDirectory(fname,NULL);
	if (!SetFileAttributes(fname,FILE_ATTRIBUTE_READONLY))
		return false;

	// create shortcut
	wchar_t path[_MAX_PATH];
	Sprintf(path,_countof(path),L"%s\\target.lnk",fname);
	{
		CComPtr<IShellLink> pLink;
		HRESULT hr=pLink.CoCreateInstance(CLSID_ShellLink);
		if (FAILED(hr)) return false;
		pLink->SetPath(source);
		CComQIPtr<IPersistFile> pFile(pLink);
		if (!pFile) return false;
		hr=pFile->Save(path,TRUE);
		if (FAILED(hr)) return false;
	}

	// create desktop.ini
	Sprintf(path,_countof(path),L"%s\\desktop.ini",fname);
	HANDLE f=CreateFile(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM,NULL);
	if (f==INVALID_HANDLE_VALUE) return false;
	const char *desktopIni="[.ShellClassInfo]\r\nCLSID2={0AFACED1-E828-11D1-9187-B532F1E9575D}\r\nFlags=2\r\n";
	DWORD q;
	WriteFile(f,desktopIni,Strlen(desktopIni),&q,NULL);
	CloseHandle(f);
	return true;
}

void DeleteFakeFolder( const wchar_t *fname )
{
	wchar_t path[_MAX_PATH];
	Sprintf(path,_countof(path),L"%s\\target.lnk",fname);
	BOOL res;
	res=SetFileAttributes(path,GetFileAttributes(path)&~FILE_ATTRIBUTE_READONLY);
	res=DeleteFile(path);
	Sprintf(path,_countof(path),L"%s\\desktop.ini",fname);
	res=SetFileAttributes(path,GetFileAttributes(path)&~FILE_ATTRIBUTE_READONLY);
	res=DeleteFile(path);
	res=SetFileAttributes(fname,GetFileAttributes(fname)&~FILE_ATTRIBUTE_READONLY);
	res=RemoveDirectory(fname);
}

bool IsFakeFolder( const wchar_t *fname )
{
	wchar_t path[_MAX_PATH];
	Sprintf(path,_countof(path),L"%s\\target.lnk",fname);
	if (GetFileAttributes(path)!=INVALID_FILE_ATTRIBUTES)
	{
		Sprintf(path,_countof(path),L"%s\\desktop.ini",fname);
		DWORD attrib=GetFileAttributes(path);
		if (attrib!=INVALID_FILE_ATTRIBUTES && (attrib&FILE_ATTRIBUTE_SYSTEM))
			return true;
	}
	return false;
}

bool GetFakeFolder( wchar_t *dst, int len, const wchar_t *src )
{
	Sprintf(dst,len,L"%s\\target.lnk",src);
	if (GetFileAttributes(dst)!=INVALID_FILE_ATTRIBUTES)
	{
		wchar_t path[_MAX_PATH];
		Sprintf(path,_countof(path),L"%s\\desktop.ini",src);
		DWORD attrib=GetFileAttributes(path);
		if (attrib!=INVALID_FILE_ATTRIBUTES && (attrib&FILE_ATTRIBUTE_SYSTEM))
			return true;
	}
	return false;
}
