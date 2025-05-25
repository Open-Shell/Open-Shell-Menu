// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include "MetroLinkManager.h"
#include "ItemManager.h"
#include "LogManager.h"
#include "FNVHash.h"
#include "ResourceHelper.h"
#include "Translations.h"
#include <propkey.h>
#include <map>

PROPERTYKEY PKEY_MetroAppLink={{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 10}; // pidl
PROPERTYKEY PKEY_MetroAppLauncher={{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 14}; // =1 for metro apps
PROPERTYKEY PKEY_MetroIconColor={{0x86D40B4D, 0x9069, 0x443C, {0x81, 0x9A, 0x2A, 0x54, 0x09, 0x0D, 0xCC, 0xEC}}, 4};
PROPERTYKEY PKEY_MetroPackageName={{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 21};
PROPERTYKEY PKEY_MetroPackagePath={{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 15};
PROPERTYKEY PKEY_AppUserModel_ParentID={{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 19}; // non-empty for content tiles
PROPERTYKEY PKEY_AppUserModel_InstalledBy={{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 18};
PROPERTYKEY PKEY_Launcher_AppState={{0x0ded77b3, 0xc614, 0x456c, {0xae, 0x5b, 0x28, 0x5b, 0x38, 0xd7, 0xb0, 0x1b}}, 7};

// FOLDERID_AppsFolder is defined in the 8.0 SDK, but we don't want to require it
KNOWNFOLDERID FOLDERID_AppsFolder2={0x1E87508D,0x89C2,0x42F0,{0x8A,0x7E,0x64,0x5A,0x0F,0x50,0xCA,0x58}}; // similar to shell:::{4234d49b-0245-4df3-b780-3893943456e1}
GUID CLSID_PinExt={0x90AA3A4E,0x1CBA,0x4233,{0xB8,0xBB,0x53,0x57,0x73,0xD4,0x84,0x49}};
const wchar_t *MetroAppClassId=L"Launcher.ImmersiveApplication";

// Returns a list of links for all metro apps
void GetMetroLinks( std::vector<MetroLink> &links, bool bLog, std::vector<CString> *pNonApps10 )
{
	LOG_MENU(LOG_APPS,L"Collect Metro Links (start)");
	if (bLog && !(g_LogCategories&LOG_APPS))
		bLog=false;
	CComPtr<IShellItem> pApps;
	if (FAILED(ShGetKnownFolderItem(FOLDERID_AppsFolder2,&pApps)))
		return;
	CComPtr<IEnumShellItems> pEnum;
	pApps->BindToHandler(NULL,BHID_EnumItems,IID_IEnumShellItems,(void**)&pEnum);

	CComPtr<IShellItem> pChild;
	while (pEnum && (pChild=NULL,pEnum->Next(1,&pChild,NULL)==S_OK))
	{
		if (bLog)
		{
			CComString pName;
			pChild->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName);
			LOG_MENU(LOG_APPS,L"App: %s",(const wchar_t*)pName);
		}
		CComPtr<IPropertyStore> pStore;
		if (FAILED(pChild->BindToHandler(NULL,BHID_PropertyStore,IID_IPropertyStore,(void**)&pStore)))
		{
			LOG_MENU(LOG_APPS,L"    No Store");
			continue;
		}
		PROPVARIANT val;
		PropVariantInit(&val);
		bool bNonApp=false;
		if (FAILED(pStore->GetValue(PKEY_MetroAppLauncher,&val)))
		{
			LOG_MENU(LOG_APPS,L"    No launcher");
			bNonApp=true;
		}
		else
		{
			if ((val.vt!=VT_I4 && val.vt!=VT_UI4) || !val.intVal)
			{
				LOG_MENU(LOG_APPS,L"    No launcher: %d",val.vt);
				bNonApp=true;
			}
			PropVariantClear(&val);
		}
		if (GetWinVersion()>=_WIN32_WINNT_WIN10)
		{
			PIDLIST_ABSOLUTE pidl;
			if (!bNonApp && SUCCEEDED(SHGetIDListFromObject(pChild,&pidl)))
			{
				links.resize(links.size()+1);
				MetroLink &link=*links.rbegin();
				link.pidl.Attach(pidl);
				link.appid=GetPropertyStoreString(pStore,PKEY_AppUserModel_ID);
				link.pItem=pChild;
			}
			else
				bNonApp=true;
			if (bNonApp && pNonApps10)
				pNonApps10->push_back(GetPropertyStoreString(pStore,PKEY_AppUserModel_ID));
			continue;
		}
		if (bNonApp)
			continue;
		if (FAILED(pStore->GetValue(PKEY_MetroAppLink,&val)))
		{
			LOG_MENU(LOG_APPS,L"    No link");
			continue;
		}
		if (val.vt!=(VT_VECTOR|VT_UI1) || !val.caub.pElems)
		{
			LOG_MENU(LOG_APPS,L"    No link: %d",val.vt);
			PropVariantClear(&val);
			continue;
		}
		PIDLIST_ABSOLUTE pidl=ILCloneFull((PIDLIST_ABSOLUTE)val.caub.pElems);
		PropVariantClear(&val);
		links.resize(links.size()+1);
		MetroLink &link=*links.rbegin();
		link.pidl.Attach(pidl);
		SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&link.pItem);
		if (bLog && link.pItem)
		{
			CComString pName;
			link.pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName);
			LOG_MENU(LOG_APPS,L"    Link: %s",(const wchar_t*)pName);
		}
	}
	LOG_MENU(LOG_APPS,L"Collect Metro Links (end)");
}

void ExecuteMetroLink( const CItemManager::ItemInfo *pInfo )
{
	SHELLEXECUTEINFO execute={sizeof(execute),SEE_MASK_CLASSNAME|SEE_MASK_INVOKEIDLIST|SEE_MASK_FLAG_LOG_USAGE|SEE_MASK_FLAG_NO_UI};
	wchar_t path[_MAX_PATH];
	{
		CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
		Strcpy(path,_countof(path),pInfo->GetPath());
	}
	execute.lpFile=path;
	execute.nShow=SW_SHOWNORMAL;
	execute.lpClass=MetroAppClassId;
	BOOL res=ShellExecuteEx(&execute);
	LOG_MENU(LOG_EXECUTE,L"ExecuteMetroLink: 0x%08X 0x%p",res?0:GetLastError(),execute.hInstApp);

	// create UserAssist entry
	{
		CRegKey regKey;
		if (regKey.Open(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced")!=ERROR_SUCCESS)
			return;
		DWORD val;
		if (regKey.QueryDWORDValue(L"Start_TrackProgs",val)==ERROR_SUCCESS && !val)
			return;
	}

	CRegKey regKeyLink;
	if (regKeyLink.Open(HKEY_CURRENT_USER,USERASSIST_LINKS_KEY,KEY_READ|KEY_WRITE)!=ERROR_SUCCESS)
		return;

	EncodeUserAssistPath(path);
	EncodeRot13(path);
	UserAssistData data;
	DWORD size=sizeof(data);
	if (regKeyLink.QueryBinaryValue(path,&data,&size)!=ERROR_SUCCESS)
		memset(&data,0,sizeof(data));
	GetSystemTimeAsFileTime(&data.timestamp);
	data.count++;
	regKeyLink.SetBinaryValue(path,&data,sizeof(data));
}

/*
void ExecutePCSettings( void )
{
		const wchar_t *appid=L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel";
		CComPtr<IShellItem> pItem;
		if (SUCCEEDED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder2,0,appid,IID_IShellItem,(void**)&pItem)))
		{
			CAbsolutePidl pidl;
			if (SUCCEEDED(SHGetIDListFromObject(pItem,&pidl)))
			{
				SHELLEXECUTEINFO execute={sizeof(execute),SEE_MASK_IDLIST|SEE_MASK_FLAG_LOG_USAGE};
				execute.lpIDList=pidl;
				execute.nShow=SW_SHOWNORMAL;
				ShellExecuteEx(&execute);
			}
		}
	}
}
*/

CComPtr<IContextMenu> GetMetroPinMenu( const wchar_t *appid )
{
	if (GetWinVersion()<_WIN32_WINNT_WIN10 && !IsWin81Update1()) return NULL;
	CComPtr<IShellFolder> pAppFolder;
	{
		CAbsolutePidl pidl;
		if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_AppsFolder2,KF_FLAG_DEFAULT,NULL,&pidl)))
			SHBindToObject(NULL,pidl,NULL,IID_IShellFolder,(void **)&pAppFolder);
	}
	CComPtr<IContextMenu> pPinMenu;
	if (pAppFolder)
	{
		PIDLIST_RELATIVE child;
		if (SUCCEEDED(pAppFolder->ParseDisplayName(NULL,NULL,(LPWSTR)appid,NULL,&child,NULL)))
		{
			CComPtr<IDataObject> pDataObject;
			if (SUCCEEDED(pAppFolder->GetUIObjectOf(NULL,1,(PCUITEMID_CHILD*)&child,IID_IDataObject,NULL,(void**)&pDataObject)))
			{
				CRegKey regKey;
				if (regKey.Open(HKEY_CLASSES_ROOT,MetroAppClassId,KEY_READ)==ERROR_SUCCESS)
				{
					CComPtr<IShellExtInit> pInit;
					pInit.CoCreateInstance(CLSID_PinExt);
					if (pInit && SUCCEEDED(pInit->Initialize(NULL,pDataObject,regKey)))
						pPinMenu=CComQIPtr<IContextMenu>(pInit);
				}
			}
			ILFree(child);
		}
	}
	return pPinMenu;
}

static const wchar_t *g_UninstallableApps[]={
	L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel",
	L"Microsoft.WindowsStore_8wekyb3d8bbwe!App",
	L"Microsoft.Windows.Cortana_cw5n1h2txyewy!CortanaUI",
};

// Returns true if the app should not be uninstalled
bool IsProtectedApp( const wchar_t *appid )
{
	for (int i=0;i<_countof(g_UninstallableApps);i++)
	{
		if (_wcsicmp(appid,g_UninstallableApps[i])==0)
			return true;
	}
	return false;
}

// Returns true if uninstalling is allowed by the policy
bool GetUninstallPolicy( void )
{
	if (GetWinVersion()<_WIN32_WINNT_WIN8)
		return false;
	CRegKey regKey;
	if (regKey.Open(HKEY_CURRENT_USER,L"Software\\Policies\\Microsoft\\Windows\\Explorer",KEY_READ)==ERROR_SUCCESS)
	{
		DWORD val;
		if (regKey.QueryDWORDValue(L"NoUninstallFromStart",val)==ERROR_SUCCESS && val)
			return false;
	}
	return true;
}

// Checks if the app can be uninstalled
bool CanUninstallMetroApp( const wchar_t *appid )
{
	if (IsProtectedApp(appid) || !GetUninstallPolicy())
		return false;

	CComPtr<IShellItem2> pAppItem;
	if (FAILED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder2,0,appid,IID_IShellItem2,(void**)&pAppItem)))
		return false;

	CComPtr<IPropertyStore> pStore;
	PROPERTYKEY keys[]={
		PKEY_MetroPackagePath,
		PKEY_AppUserModel_ParentID,
		PKEY_AppUserModel_InstalledBy,
		PKEY_AppUserModel_IsDualMode,
		PKEY_Launcher_AppState,
	};
	if (FAILED(pAppItem->GetPropertyStoreForKeys(keys,_countof(keys),GPS_FASTPROPERTIESONLY,IID_IPropertyStore,(void**)&pStore)))
		return false;

	CString parentid=GetPropertyStoreString(pStore,PKEY_AppUserModel_ParentID);
	if (!parentid.IsEmpty())
		return false; // this is a child tile
	CString packagePath=GetPropertyStoreString(pStore,PKEY_MetroPackagePath);
	if (packagePath.IsEmpty() || GetFileAttributes(packagePath)==INVALID_FILE_ATTRIBUTES)
		return false;
	PROPVARIANT val;
	PropVariantInit(&val);
	bool res=true;
	if (SUCCEEDED(pStore->GetValue(PKEY_AppUserModel_InstalledBy,&val)))
	{
		if (val.vt==VT_UI4 && val.intVal==1)
			res=false; // from CAppTileHelper::IsInboxAppTile
	}
	PropVariantClear(&val);

	if (res && SUCCEEDED(pStore->GetValue(PKEY_AppUserModel_IsDualMode,&val)))
	{
		if (val.vt==VT_BOOL && val.boolVal)
			res=false;
	}
	PropVariantClear(&val);

	if (!res) return false;

	if (SUCCEEDED(pStore->GetValue(PKEY_Launcher_AppState,&val)))
	{
		if (val.vt!=VT_UI4 || val.intVal==4 || val.intVal==6)
			res=false;
	}
	else
		res=false;
	PropVariantClear(&val);

	return res;
}

// Uninstalls the app with the given id
void UninstallMetroApp( const wchar_t *appid )
{
	auto packageName = GetPackageFullName(appid);
	if (!packageName.IsEmpty())
	{
		wchar_t command[1024];
		Sprintf(command, _countof(command), L"Remove-AppxPackage %s", packageName);
		ShellExecute(NULL, L"open", L"powershell.exe", command, NULL, SW_HIDE);
	}
}

// Asks for confirmation to uninstall the specified app
void UninstallMetroApp( HWND parent, const wchar_t *name, const wchar_t *appid )
{
	wchar_t text[1024];
	Sprintf(text,_countof(text),FindTranslation(L"Menu.UninstallPrompt",L"Are you sure you want to uninstall %s?"),name);
	CString title=FindTranslation(L"Menu.UninstallTitle",L"Uninstall");

	HICON hIcon=NULL;
	CComPtr<IShellItem> pAppItem;
	if (SUCCEEDED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder2,0,appid,IID_IShellItem,(void**)&pAppItem)))
	{
		CAbsolutePidl pidl;
		SHGetIDListFromObject(pAppItem,&pidl);
		const CItemManager::ItemInfo *pItemInfo=g_ItemManager.GetItemInfo(pAppItem,pidl,CItemManager::INFO_LINK|CItemManager::INFO_METRO);
		g_ItemManager.UpdateItemInfo(pItemInfo,CItemManager::INFO_LARGE_ICON|CItemManager::INFO_REFRESH_NOW);
		HBITMAP hMonoBitmap=CreateBitmap(CItemManager::LARGE_ICON_SIZE,CItemManager::LARGE_ICON_SIZE,1,1,NULL);
		ICONINFO info={TRUE,0,0,hMonoBitmap,pItemInfo->largeIcon->bitmap};
		hIcon=CreateIconIndirect(&info);
		DeleteObject(hMonoBitmap);
	}

	TASKDIALOGCONFIG task={sizeof(task),parent,NULL,TDF_ALLOW_DIALOG_CANCELLATION|TDF_USE_HICON_MAIN,TDCBF_YES_BUTTON|TDCBF_NO_BUTTON};
	task.pszWindowTitle=title;
	task.pszContent=text;
	task.hMainIcon=hIcon?hIcon:LoadIcon(NULL,IDI_QUESTION);
	int res;
	if (FAILED(TaskDialogIndirect(&task,&res,NULL,NULL)))
		res=0;
	if (hIcon) DestroyIcon(hIcon);

	if (res==IDYES)
		UninstallMetroApp(appid);
}

// Determines if Edge is the default browser
bool IsEdgeDefaultBrowser( void )
{
	if (GetWinVersion()<_WIN32_WINNT_WIN10)
		return false;
	CRegKey userKey;
	if (userKey.Open(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\http\\UserChoice",KEY_READ)==ERROR_SUCCESS)
	{
		wchar_t text[_MAX_PATH]=L"";
		ULONG size=_countof(text);
		if (userKey.QueryStringValue(L"ProgId",text,&size)==ERROR_SUCCESS)
		{
			wchar_t path[_MAX_PATH];
			Sprintf(path,_countof(path),L"%s\\Application",text);
			CRegKey appKey;
			if (userKey.Open(HKEY_CLASSES_ROOT,path,KEY_READ)==ERROR_SUCCESS)
			{
				size=_countof(text);
				if (userKey.QueryStringValue(L"AppUserModelID",text,&size)==ERROR_SUCCESS)
				{
					if (_wcsicmp(text,L"Microsoft.MicrosoftEdge_8wekyb3d8bbwe!MicrosoftEdge")==0)
						return true;
				}
			}
		}
	}
	return false;
}

CString GetPackageFullName(const wchar_t* appId)
{
	CComPtr<IShellItem> item;
	if (SUCCEEDED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder, 0, appId, IID_PPV_ARGS(&item))))
	{
		CComPtr<IPropertyStore> store;
		if (SUCCEEDED(item->BindToHandler(nullptr, BHID_PropertyStore, IID_PPV_ARGS(&store))))
			return GetPropertyStoreString(store, PKEY_MetroPackageName);
	}

	return {};
}
