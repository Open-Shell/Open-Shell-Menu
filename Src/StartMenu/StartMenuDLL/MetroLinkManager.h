// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#pragma once

#include <vector>
#include "ItemManager.h"
#include "LogManager.h"

#define METRO_APP_ROOT L"%LOCALAPPDATA%\\Microsoft\\Windows\\Application Shortcuts"
extern PROPERTYKEY PKEY_MetroAppLink; // pidl
extern PROPERTYKEY PKEY_MetroAppLauncher; // =1 for metro apps
extern PROPERTYKEY PKEY_MetroIconColor;
extern KNOWNFOLDERID FOLDERID_AppsFolder2;

#define DESKTOP_APP_ID L"Microsoft.Windows.Desktop"
#define SEARCH_APP_ID L"Windows.UI.Search"

struct MetroLink
{
	CAbsolutePidl pidl;
	CString appid; // only for Windows 10
	CComPtr<IShellItem> pItem;
};

// Returns a list of links for all metro apps
void GetMetroLinks( std::vector<MetroLink> &links, bool bLog, std::vector<CString> *pNonApps10=NULL );

// Executes the metro link of the given item info
void ExecuteMetroLink( const CItemManager::ItemInfo *pInfo );

// Returns true if the app is still valid
bool IsValidApp( const wchar_t *appid );

// Returns true if the app should not be uninstalled
bool IsProtectedApp( const wchar_t *appid );

// Returns true if uninstalling is allowed by the policy
bool GetUninstallPolicy( void );

// Checks if the app can be uninstalled
bool CanUninstallMetroApp( const wchar_t *appid );

// Uninstalls the app with the given id
void UninstallMetroApp( const wchar_t *appid );

// Asks for confirmation to uninstall the specified app
void UninstallMetroApp( HWND parent, const wchar_t *name, const wchar_t *appid );

// Creates a context menu to pin/unpin the metro app to the taskbar
CComPtr<IContextMenu> GetMetroPinMenu( const wchar_t *appid );

// Determines if Edge is the default browser
bool IsEdgeDefaultBrowser( void );

// Returns full package name for given App ID, of the package installed now
CString GetPackageFullName(const wchar_t* appId);

// Returns the folder where the package is installed
CString GetPackageInstallPath(const wchar_t* packageFullName);

// Returns the small logo of the app, from the manifest of the package installed now
CString GetAppLogoName(const wchar_t* appId);
