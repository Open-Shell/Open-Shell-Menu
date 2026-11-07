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
#include <appmodel.h>
#include <appxpackaging.h>
#include <shlwapi.h>
#include <map>
#include <vector>

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
		if (GetWinVersion()>=WIN_VER_WIN10)
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
	if (GetWinVersion()<WIN_VER_WIN10 && !IsWin81Update1()) return NULL;
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
	if (GetWinVersion()<WIN_VER_WIN8)
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
	if (GetWinVersion()<WIN_VER_WIN10)
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

// Splits an appid (PackageFamilyName!AppName) into its two parts. Returns false if it is not one
static bool SplitAppId(const wchar_t* appId, CString& family, CString& appName)
{
	const wchar_t* bang = appId ? wcschr(appId, '!') : nullptr;
	if (!bang)
		return false;
	family.SetString(appId, (int)(bang - appId));
	appName = bang + 1;
	return true;
}

// A package family can also list a bundle and resource packages; the app is in the main package, the one
// without a resource id. A full name is Name_Version_Architecture_ResourceId_PublisherId, and no part contains '_'
static bool IsMainPackage(const wchar_t* fullName)
{
	const wchar_t* resourceId = fullName;
	for (int part = 0; part < 3 && resourceId; part++)
	{
		resourceId = wcschr(resourceId, '_');
		if (resourceId) resourceId++;
	}
	return resourceId && *resourceId == '_';
}

// The full name of the app's package as installed now, found from the package family name in the appid.
// The full name contains the version, so it changes with every update, while the appid does not. A shortcut to a
// Metro app keeps the full name of the package it was created with, which is gone after an update. And with such a
// shortcut in the Start Menu, the Apps folder item for the appid is the shortcut, without a package.
// Returns an empty string if the app is not installed, or before Windows 8
static CString GetInstalledPackageFullName(const wchar_t* appId)
{
	static auto pGetPackagesByPackageFamily = static_cast<decltype(&GetPackagesByPackageFamily)>((void*)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "GetPackagesByPackageFamily"));
	CString family, appName;
	if (!pGetPackagesByPackageFamily || !SplitAppId(appId, family, appName))
		return {};

	UINT32 count = 0, bufferLength = 0;
	if (pGetPackagesByPackageFamily(family, &count, nullptr, &bufferLength, nullptr) != ERROR_INSUFFICIENT_BUFFER || count == 0)
		return {};
	std::vector<PWSTR> names(count);
	std::vector<wchar_t> buffer(bufferLength);
	if (pGetPackagesByPackageFamily(family, &count, names.data(), &bufferLength, buffer.data()) != ERROR_SUCCESS)
		return {};

	for (PWSTR name : names)
	{
		if (IsMainPackage(name))
			return name;
	}
	return {};
}

// Returns the folder where the package is installed, or an empty string
CString GetPackageInstallPath(const wchar_t* packageFullName)
{
	static auto pGetPackagePathByFullName = static_cast<decltype(&GetPackagePathByFullName)>((void*)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "GetPackagePathByFullName"));
	if (!pGetPackagePathByFullName || !packageFullName || !*packageFullName)
		return {};
	wchar_t path[_MAX_PATH];
	UINT32 length = _countof(path);
	if (pGetPackagePathByFullName(packageFullName, &length, path) != ERROR_SUCCESS)
		return {};
	return path;
}

// Opens the list of applications in the manifest of an installed package. Returns null if the manifest can't be read
static CComPtr<IAppxManifestApplicationsEnumerator> ReadManifestApplications(const wchar_t* packagePath)
{
	CString path = packagePath;
	path += L"\\AppxManifest.xml";
	CComPtr<IAppxFactory> factory;
	CComPtr<IStream> stream;
	CComPtr<IAppxManifestReader> reader;
	CComPtr<IAppxManifestApplicationsEnumerator> apps;
	if (FAILED(factory.CoCreateInstance(__uuidof(AppxFactory))) || FAILED(SHCreateStreamOnFileEx(path, STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, nullptr, &stream)) ||
		FAILED(factory->CreateManifestReader(stream, &reader)) || FAILED(reader->GetApplications(&apps)))
		return nullptr;
	return apps;
}

// The small logo of a manifest application: Square44x44Logo, or SmallLogo in Windows 8 manifests
static CString GetManifestLogo(IAppxManifestApplication* app)
{
	for (const wchar_t* attribute : {L"Square44x44Logo", L"SmallLogo"})
	{
		CComString logo;
		if (SUCCEEDED(app->GetStringValue(attribute, &logo)) && logo && *logo)
			return (const wchar_t*)logo;
	}
	return {};
}

// Returns the small logo of the app (a resource name like Assets\AppList.png), from the manifest of the package
// installed now, or an empty string
CString GetAppLogoName(const wchar_t* appId)
{
	CString family, appName;
	if (!SplitAppId(appId, family, appName))
		return {};
	CString packagePath = GetPackageInstallPath(GetInstalledPackageFullName(appId));
	CComPtr<IAppxManifestApplicationsEnumerator> apps = packagePath.IsEmpty() ? nullptr : ReadManifestApplications(packagePath);
	if (!apps)
		return {};

	BOOL bHasCurrent = FALSE;
	for (; SUCCEEDED(apps->GetHasCurrent(&bHasCurrent)) && bHasCurrent; apps->MoveNext(&bHasCurrent))
	{
		CComPtr<IAppxManifestApplication> app;
		CComString id;
		if (SUCCEEDED(apps->GetCurrent(&app)) && SUCCEEDED(app->GetStringValue(L"Id", &id)) && _wcsicmp(id, appName) == 0)
			return GetManifestLogo(app);
	}
	return {};
}

CString GetPackageFullName(const wchar_t* appId)
{
	CString packageName = GetInstalledPackageFullName(appId);
	if (!packageName.IsEmpty())
		return packageName;

	CComPtr<IShellItem> item;
	if (SUCCEEDED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder, 0, appId, IID_PPV_ARGS(&item))))
	{
		CComPtr<IPropertyStore> store;
		if (SUCCEEDED(item->BindToHandler(nullptr, BHID_PropertyStore, IID_PPV_ARGS(&store))))
			return GetPropertyStoreString(store, PKEY_MetroPackageName);
	}

	return {};
}
