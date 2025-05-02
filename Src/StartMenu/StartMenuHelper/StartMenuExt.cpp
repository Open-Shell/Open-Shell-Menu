// StartMenuExt.cpp : Implementation of CStartMenuExt

#include "stdafx.h"
#include "StartMenuExt.h"
#include "dllmain.h"
#include "StringUtils.h"
#include "Translations.h"
#include "Settings.h"
#include "ComHelper.h"
#include "FileHelper.h"
#include <propvarutil.h>
#include <propkey.h>
#include <vector>

#define PIN_VERB "pin_classic"

// CStartMenuExt

static CLIPFORMAT s_ShellFormat;
#define GetPIDLFolder(pida) (LPCITEMIDLIST)(((LPBYTE)pida)+(pida)->aoffset[0])
#define GetPIDLItem(pida, i) (LPCITEMIDLIST)(((LPBYTE)pida)+(pida)->aoffset[i+1])
// FOLDERID_AppsFolder is defined in the 8.0 SDK, but we don't want to require it
KNOWNFOLDERID FOLDERID_AppsFolder2={0x1E87508D,0x89C2,0x42F0,{0x8A,0x7E,0x64,0x5A,0x0F,0x50,0xCA,0x58}}; // similar to shell:::{4234d49b-0245-4df3-b780-3893943456e1}

static HRESULT CreateLink( const wchar_t *source, LPITEMIDLIST sourcePidl, const wchar_t *dir, const wchar_t *fname )
{
	HRESULT hr;
	{
		CComPtr<IShellLink> pLink;
		hr=pLink.CoCreateInstance(CLSID_ShellLink);
		if (FAILED(hr)) return hr;

		if (sourcePidl)
			hr=pLink->SetIDList(sourcePidl);
		else
			hr=pLink->SetPath(source);
		if (FAILED(hr)) return hr;

		if (dir)
			pLink->SetWorkingDirectory(dir);
		CComQIPtr<IPersistFile> pFile(pLink);
		if (!pFile) return E_FAIL;
		hr=pFile->Save(fname,TRUE);
	}
	{
		// reopen the link and set the "no new" property. without reopening the original properties are lost
		CComPtr<IShellLink> pLink;
		hr=pLink.CoCreateInstance(CLSID_ShellLink);
		CComQIPtr<IPersistFile> pFile(pLink);
		hr=pFile->Load(fname,STGM_READWRITE);
		CComQIPtr<IPropertyStore> pStore(pLink);
		if (pStore)
		{
			PROPVARIANT val;
			InitPropVariantFromBoolean(TRUE,&val);
			pStore->SetValue(PKEY_AppUserModel_ExcludeFromShowInNewInstall,val);
			PropVariantClear(&val);
			pStore->Commit();
		}
		hr=pFile->Save(fname,TRUE);
	}
	HANDLE h=CreateFile(fname,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	if (h!=INVALID_HANDLE_VALUE)
	{
		FILETIME curTime;
		GetSystemTimeAsFileTime(&curTime);
		SetFileTime(h,&curTime,&curTime,&curTime);
		CloseHandle(h);
	}
	return S_OK;
}

HRESULT WINAPI CStartMenuExt::UpdateRegistry( BOOL bRegister )
{
	return _AtlModule.UpdateRegistryFromResource(IDR_STARTMENUEXT,bRegister);
}

STDMETHODIMP CStartMenuExt::Initialize( PCIDLIST_ABSOLUTE pidlFolder, IDataObject *pData, HKEY hkey )
{
	WaitDllInitThread();
	LoadSettings();
	s_ShellFormat=RegisterClipboardFormat(CFSTR_SHELLIDLIST);
	if (GetSettingBool(L"DisablePinExt"))
		return E_FAIL;
	bool bUsePinned;
	bool bDef;
	int setting=GetSettingInt(L"PinnedPrograms",bDef);
	if (bDef)
		bUsePinned=(GetSettingInt(L"MenuStyle")==2); // pinned is default in Win7 style
	else
		bUsePinned=(setting==1);
	if (bUsePinned)
	{
		Sprintf(m_PinFolder1,_countof(m_PinFolder1),L"%s\\",GetSettingString(L"PinnedItemsPath"));
		DoEnvironmentSubst(m_PinFolder1,_countof(m_PinFolder1));
		m_PinFolder2[0]=0;
	}
	else
	{
		CString str=GetSettingString(L"FolderStartMenu");
		if (!str.IsEmpty())
		{
			Sprintf(m_PinFolder1,_countof(m_PinFolder1),L"%s\\",str);
			DoEnvironmentSubst(m_PinFolder1,_countof(m_PinFolder1));
		}
		else
		{
			CComString pPath;
			if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_StartMenu,0,NULL,&pPath)))
				Sprintf(m_PinFolder1,_countof(m_PinFolder1),L"%s\\",(const wchar_t*)pPath);
		}

		str=GetSettingString(L"FolderCommonStartMenu");
		if (!str.IsEmpty())
		{
			Sprintf(m_PinFolder2,_countof(m_PinFolder2),L"%s\\",str);
			DoEnvironmentSubst(m_PinFolder2,_countof(m_PinFolder2));
		}
		else
		{
			CComString pPath;
			if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_CommonStartMenu,0,NULL,&pPath)))
				Sprintf(m_PinFolder2,_countof(m_PinFolder2),L"%s\\",(const wchar_t*)pPath);
		}
	}

	FORMATETC fmt1={CF_HDROP,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
	FORMATETC fmt2={s_ShellFormat,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
	STGMEDIUM stg={TYMED_HGLOBAL};

	if (pData && SUCCEEDED(pData->GetData(&fmt1,&stg)))
	{
		HDROP hDrop=(HDROP)GlobalLock(stg.hGlobal);
		if (hDrop)
		{
			if (DragQueryFile(hDrop,0xFFFFFFFF,NULL,0)==1)
			{
				DragQueryFile(hDrop,0,m_FileName,_countof(m_FileName));
				const wchar_t *fname=PathFindFileName(m_FileName);
				m_bInPinFolder1=(fname-m_FileName==Strlen(m_PinFolder1) && _wcsnicmp(m_PinFolder1,m_FileName,(fname-m_FileName))==0);
				m_bInPinFolder2=(m_PinFolder2[0] && fname-m_FileName==Strlen(m_PinFolder2) && _wcsnicmp(m_PinFolder2,m_FileName,(fname-m_FileName))==0);
				GlobalUnlock(stg.hGlobal);
				ReleaseStgMedium(&stg);
				return S_OK;
			}
			GlobalUnlock(stg.hGlobal);
		}
		ReleaseStgMedium(&stg);
	}
	else if (pData && SUCCEEDED(pData->GetData(&fmt2,&stg)))
	{
		CIDA *pIda=(CIDA*)GlobalLock(stg.hGlobal);
		if (pIda)
		{
			if (pIda->cidl==1)
			{
				LPCITEMIDLIST folderPidl=GetPIDLFolder(pIda);
				LPCITEMIDLIST itemPidl=GetPIDLItem(pIda,0);
				m_bInPinFolder1=m_bInPinFolder2=false;
				LPITEMIDLIST appsFolder;
				if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_AppsFolder2,0,NULL,&appsFolder)) && ILIsEqual(folderPidl,appsFolder))
					m_FilePidl=ILCombine(folderPidl,itemPidl);
				GlobalUnlock(stg.hGlobal);
				ReleaseStgMedium(&stg);
				return m_FilePidl?S_OK:E_INVALIDARG;
			}
			GlobalUnlock(stg.hGlobal);
		}
		ReleaseStgMedium(&stg);
	}
	return E_INVALIDARG;
}

STDMETHODIMP CStartMenuExt::QueryContextMenu( HMENU menu, UINT uMenuIndex, UINT uidFirstCmd, UINT uidLastCmd, UINT uFlags )
{
	if (uFlags&(CMF_DEFAULTONLY|CMF_VERBSONLY))
		return MAKE_HRESULT(SEVERITY_SUCCESS,FACILITY_NULL,0);
	if (m_bInPinFolder2)
		return MAKE_HRESULT(SEVERITY_SUCCESS,FACILITY_NULL,0); // can't unpin from common folder

	bool bDirectory=(PathIsDirectory(m_FileName)!=FALSE);
	bool bFakeDirectory=bDirectory && IsFakeFolder(m_FileName);
	const wchar_t *ext=PathFindExtension(m_FileName);
	if (bDirectory || m_FilePidl || _wcsicmp(ext,L".exe")==0 || _wcsicmp(ext,L".lnk")==0)
	{
		if (m_bInPinFolder1)
		{
			if (!bFakeDirectory && _wcsicmp(ext,L".lnk")!=0)
				return MAKE_HRESULT(SEVERITY_SUCCESS,FACILITY_NULL,0); // can only unpin fake folders and links
			InsertMenu(menu,uMenuIndex,MF_BYPOSITION,uidFirstCmd,FindTranslation(L"Menu.UnpinStartCs",L"Unpin from Start menu (Open-Shell)"));
		}
		else
		{
			if (bFakeDirectory)
				return MAKE_HRESULT(SEVERITY_SUCCESS,FACILITY_NULL,0); // can't pin fake folders
			InsertMenu(menu,uMenuIndex,MF_BYPOSITION,uidFirstCmd,FindTranslation(L"Menu.PinStartCs",L"Pin to Start menu (Open-Shell)"));
		}
		return MAKE_HRESULT(SEVERITY_SUCCESS,FACILITY_NULL,1);
	}
	return MAKE_HRESULT(SEVERITY_SUCCESS,FACILITY_NULL,0);
}

static CString GetFileDescription( const wchar_t *fname )
{
	DWORD q;
	DWORD size=GetFileVersionInfoSizeEx(FILE_VER_GET_LOCALISED|FILE_VER_GET_NEUTRAL,fname,&q);
	if (size)
	{
		std::vector<char> buf(size);
		if (GetFileVersionInfoEx(FILE_VER_GET_LOCALISED|FILE_VER_GET_NEUTRAL,fname,0,size,&buf[0]))
		{
			wchar_t languages[100];
			size=0;
			DWORD len=_countof(languages);
			GetUserPreferredUILanguages(MUI_LANGUAGE_ID,&size,languages,&len);

			for (const wchar_t *lang=languages;*lang;lang+=Strlen(lang)+1)
			{
				wchar_t key[100];
				Sprintf(key,_countof(key),L"\\StringFileInfo\\%s%04X\\FileDescription",lang,1200);
				const wchar_t *text=NULL;
				UINT q;
				if (VerQueryValue(&buf[0],key,(void**)&text,&q))
					return text;
			}

			DWORD *langs;
			UINT count;
			if (VerQueryValue(&buf[0],L"\\VarFileInfo\\Translation",(void**)&langs,&count))
			{
				count/=4;
				for (DWORD i=0;i<count;i++)
				{
					if (HIWORD(langs[i])==1200)
					{
						wchar_t key[100];
						Sprintf(key,_countof(key),L"\\StringFileInfo\\%04X%04X\\FileDescription",LOWORD(langs[i]),1200);
						const wchar_t *text=NULL;
						UINT q;
						if (VerQueryValue(&buf[0],key,(void**)&text,&q))
							return text;
					}
				}
			}
		}
	}
	return CString();
}

STDMETHODIMP CStartMenuExt::InvokeCommand( CMINVOKECOMMANDINFO *pInfo )
{
	if (pInfo->lpVerb==NULL || (!IS_INTRESOURCE(pInfo->lpVerb) && strcmp(pInfo->lpVerb,PIN_VERB)==0))
	{
		bool bDirectory=(*m_FileName && PathIsDirectory(m_FileName)!=FALSE);
		bool bFakeDirectory=bDirectory && IsFakeFolder(m_FileName);
		const wchar_t *fname=PathFindFileName(m_FileName);
		const wchar_t *ext=PathFindExtension(fname);
		if (m_bInPinFolder1)
		{
			// the file is in the user pin folder - unpin
			if (!bFakeDirectory && _wcsicmp(ext,L".lnk")!=0)
				return E_FAIL; // can only unpin fake folders and links
			if (bFakeDirectory)
				DeleteFakeFolder(m_FileName);
			else
			{
				SetFileAttributes(m_FileName,GetFileAttributes(m_FileName)&~FILE_ATTRIBUTE_READONLY);
				DeleteFile(m_FileName);
			}
		}
		else if (!m_bInPinFolder2)
		{
			// the file is not in any pin folder - pin

			if (bFakeDirectory)
				return E_FAIL; // can't pin fake folders

			wchar_t locName[_MAX_PATH];
			int locIndex;
			bool bHasLocName=(*m_FileName && SHGetLocalizedName(m_FileName,locName,_countof(locName),&locIndex)==S_OK);
			wchar_t finalPath[_MAX_PATH];
			bool res=false;
			if (bDirectory)
			{
				int len=Strlen(fname);
				while (len>0 && (fname[len-1]=='\\' || fname[len-1]==':'))
					len--;
				wchar_t path[_MAX_PATH];
				Sprintf(path,_countof(path),L"%s%.*s",m_PinFolder1,len,fname);
				PathYetAnotherMakeUniqueName(finalPath,path,NULL,PathFindFileName(path));
				res=CreateFakeFolder(m_FileName,finalPath);
			}
			else
			{
				wchar_t name[_MAX_FNAME];
				name[0]=0;
				if (m_FilePidl)
				{
					CComPtr<IShellItem> pItem;
					SHCreateItemFromIDList(m_FilePidl,IID_IShellItem,(void**)&pItem);
					if (pItem)
					{
						wchar_t *pName;
						if (SUCCEEDED(pItem->GetDisplayName(SIGDN_NORMALDISPLAY,&pName)))
						{
							Strcpy(name,_countof(name),pName);
							CoTaskMemFree(pName);
						}
					}
				}
				else if (_wcsicmp(ext,L".exe")==0 && !bHasLocName)
				{
					CString desc=GetFileDescription(m_FileName);
					Strcpy(name,_countof(name),desc);
				}

				if (!name[0])
				{
					memcpy(name,fname,(ext-fname)*2);
					name[ext-fname]=0;
				}

				wchar_t path[_MAX_PATH];
				Sprintf(path,_countof(path),L"%s%s.lnk",m_PinFolder1,name);
				PathYetAnotherMakeUniqueName(finalPath,path,NULL,PathFindFileName(path));
				if (m_FilePidl)
				{
					res=SUCCEEDED(CreateLink(NULL,m_FilePidl,NULL,finalPath));
				}
				else if (_wcsicmp(ext,L".lnk")==0)
				{
					res=SUCCEEDED(CreateLink(m_FileName,NULL,NULL,finalPath));
				}
				else
				{
					wchar_t dir[_MAX_PATH];
					Strcpy(dir,_countof(dir),m_FileName);
					PathRemoveFileSpec(dir);
					res=SUCCEEDED(CreateLink(m_FileName,NULL,dir,finalPath));
				}
			}
			if (!res)
				return E_FAIL;
			if (bHasLocName)
			{
				SHSetLocalizedName(finalPath,locName,locIndex);
			}
		}
		return S_OK;
	}
	return E_FAIL;
}

STDMETHODIMP CStartMenuExt::GetCommandString( UINT_PTR idCmd, UINT uFlags, UINT* pwReserved, LPSTR pszName, UINT cchMax )
{
	if (idCmd==0)
	{
		if (uFlags==GCS_VERBA)
		{
			Strcpy(pszName,cchMax,PIN_VERB);
			return S_OK;
		}
		return E_NOTIMPL;
	}

	return E_INVALIDARG;
}
