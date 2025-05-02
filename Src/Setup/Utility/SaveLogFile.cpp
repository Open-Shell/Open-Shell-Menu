// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#define STRICT_TYPED_ITEMIDS
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// some CString constructors will be explicit
#include <windows.h>
#include <stdio.h>
#include <atlstr.h>
#include "ResourceHelper.h"
#include "ComHelper.h"
#include "StringUtils.h"
#include "Settings.h"
#include <shlobj.h>
#include <wuapi.h>
#include <Psapi.h>
#include <propkey.h>
#include <searchapi.h>
#define SECURITY_WIN32
#include <Security.h>
#include <set>
#include <map>
#include <algorithm>

extern HINSTANCE g_hInstance;

struct CompareStrings
{
	bool operator()( const CString &str1, const CString &str2 ) const
	{
		return _wcsicmp(str1,str2)<0;
	}
};

const CLSID CLSID_CSearchManager2={0x7D096C5F,0xAC08,0x4f1f,{0xBE,0xB7,0x5C,0x22,0xC5,0x17,0xCE,0x39}};

///////////////////////////////////////////////////////////////////////////////
// dummy functions

void ClosingSettings( HWND hWnd, int flags, int command )
{
}

void SettingChangedCallback( const CSetting *pSetting )
{
}

void UpgradeSettings( bool bShared )
{
}

void UpdateSettings( void )
{
}

///////////////////////////////////////////////////////////////////////////////

static const wchar_t *g_Tabs=L"\t\t\t\t\t\t\t\t\t\t";
static const wchar_t *GetTabs( int count )
{
	if (count>10) count=10;
	return g_Tabs+(10-count);
}

DWORD GetFileVersion( const wchar_t *fname, DWORD *pBuild )
{
	DWORD dwHandle;
	DWORD dwLen=GetFileVersionInfoSize(fname,&dwHandle);
	if (!dwLen) 
		return 0;

	std::vector<char> buf(dwLen);
	if (!GetFileVersionInfo(fname,dwHandle,dwLen,&buf[0]))
		return 0;

	VS_FIXEDFILEINFO *pFileInfo;
	UINT len;
	if (!VerQueryValue(&buf[0],L"\\",(void**)&pFileInfo,&len))
		return 0;
	if (pBuild)
		*pBuild=LOWORD(pFileInfo->dwFileVersionLS);
	return (HIWORD(pFileInfo->dwFileVersionMS)<<24)|(LOWORD(pFileInfo->dwFileVersionMS)<<16)|HIWORD(pFileInfo->dwFileVersionLS);
}

static LONG ReadRegistryValue( HKEY root, const wchar_t *keyName, const wchar_t *valName, DWORD &value )
{
	CRegKey regKey;
	LONG res=regKey.Open(root,keyName,KEY_READ|KEY_WOW64_64KEY);
	if (res==ERROR_SUCCESS)
		res=regKey.QueryDWORDValue(valName,value);
	return res;
}

static LONG ReadRegistryValue( HKEY root, const wchar_t *keyName, const wchar_t *valName, CString &value )
{
	value.Empty();
	wchar_t text[1024];
	ULONG size=_countof(text);
	CRegKey regKey;
	LONG res=regKey.Open(root,keyName,KEY_READ|KEY_WOW64_64KEY);
	if (res==ERROR_SUCCESS)
	{
		res=regKey.QueryStringValue(valName,text,&size);
		if (res==ERROR_SUCCESS)
			value=text;
	}
	return res;
}

static void WriteRegKey( FILE *f, CRegKey &key, int tabs, const wchar_t *annotations[][2]=NULL )
{
	std::vector<BYTE> buf(65536);
	for (int index=0;;index++)
	{
		wchar_t name[256];
		DWORD len=_countof(name);
		DWORD type;
		DWORD size=(int)buf.size();
		if (RegEnumValue(key,index,name,&len,NULL,&type,&buf[0],&size)!=ERROR_SUCCESS)
			break;
		fwprintf(f,L"%s%s: ",GetTabs(tabs),name);
		switch (type)
		{
			case REG_DWORD:
				{
					DWORD val=*(DWORD*)&buf[0];
					fwprintf(f,L"0x%08X (%d)",val,val);
				}
				break;
			case REG_SZ:
			case REG_EXPAND_SZ:
				{
					CString val=(wchar_t*)&buf[0];
					val.Replace(L"\r",L"\\r");
					val.Replace(L"\n",L"\\n");
					fwprintf(f,L"%s",(const wchar_t*)val);
				}
				break;
			case REG_MULTI_SZ:
				for (const wchar_t *str=(wchar_t*)&buf[0];*str;str+=Strlen(str)+1)
				{
					CString val=str;
					val.Replace(L"\r",L"\\r");
					val.Replace(L"\n",L"\\n");
					fwprintf(f,L"%s\\0",(const wchar_t*)val);
				}
				break;
		}
		if (annotations)
		{
			for (const wchar_t **a=&annotations[0][0];*a;a+=2)
			{
				if (_wcsicmp(a[0],name)==0)
				{
					fwprintf(f,L" - %s",a[1]);
					break;
				}
			}
		}
		fwprintf(f,L"\r\n");
	}
}

static void WriteFolder( FILE *f, const wchar_t *path, int tabs, bool bRecursive )
{
	wchar_t find[_MAX_PATH];
	Sprintf(find,_countof(find),L"%s\\*.*",path);
	std::vector<CString> folders;
	WIN32_FIND_DATA data;
	HANDLE h=FindFirstFile(find,&data);
	while (h!=INVALID_HANDLE_VALUE)
	{
		wchar_t fname[_MAX_PATH];
		Sprintf(fname,_countof(fname),L"%s\\%s",path,data.cFileName);
		if (data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
		{
			if (bRecursive && wcscmp(data.cFileName,L".")!=0 && wcscmp(data.cFileName,L"..")!=0)
				folders.push_back(fname);
		}
		else
		{
			fwprintf(f,L"%s%s",GetTabs(tabs),data.cFileName);
			FILETIME localTime;
			FileTimeToLocalFileTime(&data.ftLastWriteTime,&localTime);
			SYSTEMTIME sysTime;
			FileTimeToSystemTime(&localTime,&sysTime);
			fwprintf(f,L", date: %04d/%02d/%02d, time: %02d:%02d:%02d",sysTime.wYear,sysTime.wMonth,sysTime.wDay,sysTime.wHour,sysTime.wMinute,sysTime.wSecond);
			const wchar_t *ext=PathFindExtension(data.cFileName);
			if (_wcsicmp(ext,L".lnk")==0)
			{
				// find target, args and appid
				CComPtr<IShellItem> pItem;
				SHCreateItemFromParsingName(fname,NULL,IID_IShellItem,(void**)&pItem);
				if (pItem)
				{
					CComPtr<IShellLink> pLink;
					if (SUCCEEDED(pItem->BindToHandler(NULL,BHID_SFUIObject,IID_IShellLink,(void**)&pLink)))
					{
						CComPtr<IShellItem> pTarget;
						CComString target;
						CAbsolutePidl pidl;
						if (FAILED(pLink->GetIDList(&pidl)))
							fwprintf(f,L" target='no pidl'");
						else if (FAILED(SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pTarget)))
							fwprintf(f,L" target='no item'");
						else if (FAILED(pTarget->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&target)))
							fwprintf(f,L" target='no name'");
						else
							fwprintf(f,L" target='%s'",(const wchar_t*)target);
						wchar_t args[256];
						if (FAILED(pLink->GetArguments(args,_countof(args))))
							args[0]=0;
						CComQIPtr<IPropertyStore> pStore(pLink);
						CString appid;
						if (pStore)
						{
							PROPVARIANT val;
							PropVariantInit(&val);
							if (SUCCEEDED(pStore->GetValue(PKEY_AppUserModel_ID,&val)) && val.vt==VT_BSTR && val.bstrVal)
								appid=val.bstrVal;
							PropVariantClear(&val);
							if (!args[0] && SUCCEEDED(pStore->GetValue(PKEY_Link_Arguments,&val)) && val.vt==VT_BSTR && val.bstrVal)
								Strcpy(args,_countof(args),val.bstrVal);
							PropVariantClear(&val);
						}
						if (args[0])
							fwprintf(f,L" args='%s'",args);
						if (!appid.IsEmpty())
							fwprintf(f,L" appid='%s'",(const wchar_t*)appid);
					}
				}
			}
			else
			{
				DWORD build;
				DWORD ver=GetFileVersion(fname,&build);
				if (ver)
					fwprintf(f,L", version: %d.%d.%d.%d",ver>>24,(ver>>16)&255,ver&65535,build);
				if (_wcsicmp(ext,L".exe")==0)
				{
					CString policy;
					ReadRegistryValue(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",fname,policy);
					if (!policy.IsEmpty())
						fwprintf(f,L" usercompat='%s'",(const wchar_t*)policy);
					ReadRegistryValue(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",fname,policy);
					if (!policy.IsEmpty())
						fwprintf(f,L" compat='%s'",(const wchar_t*)policy);
				}
			}
			fwprintf(f,L"\r\n");
		}
		if (!FindNextFile(h,&data))
		{
			FindClose(h);
			break;
		}
	}
	for (std::vector<CString>::const_iterator it=folders.begin();it!=folders.end();++it)
	{
		fwprintf(f,L"%s%s\r\n",GetTabs(tabs),PathFindFileName(*it));
		WriteFolder(f,*it,tabs+1,true);
	}
}

static void WriteSettings( FILE *f, TSettingsComponent component )
{
	CRegKey regSettings, regSettingsUser, regPolicy, regPolicyUser;
	bool bUpgrade=OpenSettingsKeys(component,regSettings,regSettingsUser,regPolicy,regPolicyUser);
	if (regSettingsUser)
	{
		fwprintf(f,L"\t\t%s:\r\n",bUpgrade?L"User settings (old)":L"User settings");
		WriteRegKey(f,regSettingsUser,3);
		fwprintf(f,L"\r\n");
	}
	if (regSettings)
	{
		fwprintf(f,L"\t\tCommon settings:\r\n");
		WriteRegKey(f,regSettings,3);
		fwprintf(f,L"\r\n");
	}
	if (regPolicyUser)
	{
		fwprintf(f,L"\t\tUser policies:\r\n");
		WriteRegKey(f,regPolicyUser,3);
		fwprintf(f,L"\r\n");
	}
	if (regPolicy)
	{
		fwprintf(f,L"\t\tCommon policies:\r\n");
		WriteRegKey(f,regPolicy,3);
		fwprintf(f,L"\r\n");
	}
}

static void WriteProcessInfo( FILE *f, HANDLE hProcess, int tabs )
{
	HMODULE hMods[1024];
	std::set<CString,CompareStrings> names;
	DWORD cbNeeded;
	if (EnumProcessModules(hProcess,hMods,sizeof(hMods),&cbNeeded))
	{
		int count=cbNeeded/sizeof(HMODULE);
		for (int i=0;i<count;i++)
		{
			wchar_t fname[_MAX_PATH];
			if (GetModuleFileNameEx(hProcess,hMods[i],fname,_countof(fname)))
				names.insert(fname);
		}
	}

	for (std::set<CString,CompareStrings>::const_iterator it=names.begin();it!=names.end();++it)
	{
		DWORD ver=GetFileVersion(*it,NULL);
		fwprintf(f,L"%s%s  (%d.%d.%d)\r\n",GetTabs(tabs),(const wchar_t*)(*it),ver>>24,(ver>>16)&255,ver&65535);
	}
}

PROPERTYKEY PKEY_ProductVersion={{0x0CEF7D53, 0xFA64, 0x11D1, {0xA2, 0x03, 0x00, 0x00, 0xF8, 0x1F, 0xED, 0xEE}}, 8};

static BOOL CALLBACK MonitorEnumProc( HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData )
{
	FILE *f=(FILE*)dwData;
	HWND taskbar=FindWindowEx(NULL,NULL,L"Shell_TrayWnd",NULL);
	HMONITOR mon=taskbar?MonitorFromWindow(taskbar,MONITOR_DEFAULTTONULL):NULL;
	if (mon!=hMonitor)
	{
		for (taskbar=FindWindowEx(NULL,NULL,L"Shell_SecondaryTrayWnd",NULL);taskbar;taskbar=FindWindowEx(NULL,taskbar,L"Shell_SecondaryTrayWnd",NULL))
		{
			mon=MonitorFromWindow(taskbar,MONITOR_DEFAULTTONULL);
			if (mon==hMonitor)
				break;
		}
	}
	fwprintf(f,L"\tMonitor: %d, %d - (%d x %d)\r\n",lprcMonitor->left,lprcMonitor->top,lprcMonitor->right-lprcMonitor->left,lprcMonitor->bottom-lprcMonitor->top);
	if (mon==hMonitor)
	{
		RECT rc;
		GetWindowRect(taskbar,&rc);
		fwprintf(f,L"\t\tTaskbar: %d, %d - (%d x %d)\r\n",rc.left,rc.top,rc.right-rc.left,rc.bottom-rc.top);
	}
	return TRUE;
}

static const wchar_t *g_ExplorerRegAnnotations[][2]={
	{L"Start_TrackDocs",L"track documents"},
	{L"Start_TrackProgs",L"track programs"},
	{L"TaskbarSizeMove",L"unlocked taskbar"},
	{L"TaskbarSmallIcons",L"small icons"},
	{NULL}
};

struct GroupInfo
{
	CString desc;
	CString group;
	const wchar_t *status;
};

static void WriteLogFile( FILE *f )
{
	// windows version
	BOOL b64=FALSE;
#ifdef _WIN64
	b64=TRUE;
#else
	IsWow64Process(GetCurrentProcess(),&b64);
#endif
	DWORD winVer=GetVersionEx(GetModuleHandle(L"user32.dll"));
	fwprintf(f,L"System\r\n");
	fwprintf(f,L"\tWindows version (real): %d.%02d.%d %d-bit\r\n",(winVer>>24),(winVer>>16)&255,winVer&65535,b64?64:32);
	DWORD ver2=GetWinVersion();
	fwprintf(f,L"\tWindows version (reported): %d.%02d\r\n",(ver2>>8),ver2&255);
	CString strVer1, strVer2;
	ReadRegistryValue(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"ProductName",strVer1);
	ReadRegistryValue(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"CurrentVersion",strVer2);
	fwprintf(f,L"\tWindows version (registry): %s %s\r\n",(const wchar_t*)strVer1,(const wchar_t*)strVer2);

	wchar_t user1[256]={0}, user2[256]={0};
	ULONG size=_countof(user1);
	GetComputerName(user1,&size);
	fwprintf(f,L"\tComputer: %s\r\n",user1);
	size=_countof(user1);
	GetUserName(user1,&size);
	size=_countof(user2);
	if (GetUserNameEx(NameDisplay,user2,&size))
		fwprintf(f,L"\tUser: %s (%s)\r\n",user1,user2);
	else
		fwprintf(f,L"\tUser: %s (0x%08X)\r\n",user1,GetLastError());

	{
		wchar_t languages[100];
		ULONG count=0;
		ULONG len=_countof(languages);
		GetUserPreferredUILanguages(MUI_LANGUAGE_NAME,&size,languages,&len);
		if (len>1)
		{
			for (ULONG i=0;i<len-2;i++)
				if (languages[i]==0)
					languages[i]='|';
			fwprintf(f,L"\tUser Languages: %s\r\n",languages);
		}
		count=0;
		len=_countof(languages);
		GetThreadPreferredUILanguages(MUI_LANGUAGE_NAME,&size,languages,&len);
		if (len>1)
		{
			for (ULONG i=0;i<len-2;i++)
				if (languages[i]==0)
					languages[i]='|';
			fwprintf(f,L"\tThread Languages: %s\r\n",languages);
		}
	}

	EnumDisplayMonitors(NULL,NULL,MonitorEnumProc,(LPARAM)f);
	{
		int touch=GetSystemMetrics(SM_DIGITIZER);
		wchar_t touchCaps[256]=L"";
		if (touch&NID_INTEGRATED_TOUCH)
			Strcat(touchCaps,_countof(touchCaps),L"integrated touch, ");
		if (touch&NID_EXTERNAL_TOUCH)
			Strcat(touchCaps,_countof(touchCaps),L"external touch, ");
		if (touch&NID_INTEGRATED_PEN)
			Strcat(touchCaps,_countof(touchCaps),L"integrated pen, ");
		if (touch&NID_EXTERNAL_PEN)
			Strcat(touchCaps,_countof(touchCaps),L"external pen ");
		if (touch&NID_MULTI_INPUT)
			Strcat(touchCaps,_countof(touchCaps),L"multi input, ");
		if (touch&NID_READY)
			Strcat(touchCaps,_countof(touchCaps),L"ready, ");
		if (Strlen(touchCaps)>=2)
			touchCaps[Strlen(touchCaps)-2]=0;
		else
			Strcpy(touchCaps,_countof(touchCaps),L"None");
		fwprintf(f,L"\tTouch capabilities: %s\r\n",touchCaps);
	}

	{
		CComString pPath;
		if (FAILED(SHGetKnownFolderPath(FOLDERID_StartMenu,0,NULL,&pPath)))
			pPath.Clear();
		fwprintf(f,L"\tStart Menu folder: '%s'\r\n",pPath?pPath:L"");
		pPath.Clear();
		if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs,0,NULL,&pPath)))
			pPath.Clear();
		fwprintf(f,L"\tPrograms folder: '%s'\r\n",pPath?pPath:L"");
		pPath.Clear();
		if (FAILED(SHGetKnownFolderPath(FOLDERID_CommonStartMenu,0,NULL,&pPath)))
			pPath.Clear();
		fwprintf(f,L"\tCommon Start Menu folder: '%s'\r\n",pPath?pPath:L"");
		pPath.Clear();
		if (FAILED(SHGetKnownFolderPath(FOLDERID_CommonPrograms,0,NULL,&pPath)))
			pPath.Clear();
		fwprintf(f,L"\tCommon Programs folder: '%s'\r\n",pPath?pPath:L"");
	}
	if (HIWORD(winVer)<WIN_VER_WIN8)
	{
		DWORD count;
		if (ReadRegistryValue(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\UAS",L"UpdateCount",count)==ERROR_SUCCESS)
			fwprintf(f,L"\tPending updates: %d\r\n",count);
	}
	else
	{
		typedef HRESULT (WINAPI *FGetAutoUpdateNotification)(DWORD,DWORD*,DWORD*,DWORD*);
		HMODULE mod=LoadLibrary(L"wuaext.dll");
		if (mod)
		{
			FGetAutoUpdateNotification fun=(FGetAutoUpdateNotification)GetProcAddress(mod,"GetAutoUpdateNotification");
			if (fun)
			{
				DWORD a,b,c;
				HRESULT hr=fun(0,&a,&b,&c);
				fwprintf(f,L"\tPending updates (0x%08X): %d, %d, %d\r\n",hr,a,b,c);
			}
			FreeLibrary(mod);
		}
	}
	{
		CComPtr<ISystemInformation> pSysInfo;
		pSysInfo.CoCreateInstance(CLSID_SystemInformation);
		if (pSysInfo)
		{
			VARIANT_BOOL reboot;
			if (SUCCEEDED(pSysInfo->get_RebootRequired(&reboot)) && reboot)
			{
				fwprintf(f,L"\tWindows Update: Reboot required\r\n");
			}
		}
	}

	if (HIWORD(winVer)>=WIN_VER_WIN81)
	{
		DWORD metro;
		if (ReadRegistryValue(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartPage",L"OpenAtLogon",metro)==ERROR_SUCCESS)
			fwprintf(f,L"\tSkip to Desktop: %d\r\n",1-metro);
		else
			fwprintf(f,L"\tSkip to Desktop: unset\r\n");
	}

	{
		CRegKey regKey;
		const wchar_t *key=L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
		if (regKey.Open(HKEY_CURRENT_USER,key,KEY_READ|KEY_WOW64_64KEY)==ERROR_SUCCESS)
		{
			fwprintf(f,L"\t\r\n\tHKCU\\%s:\r\n",key);
			WriteRegKey(f,regKey,2,g_ExplorerRegAnnotations);
			fwprintf(f,L"\r\n");
		}
	}

	fwprintf(f,L"\tPolicies:\r\n");
	fwprintf(f,L"\t\tSHRestricted(REST_HASFINDCOMPUTERS)=%d\r\n",SHRestricted(REST_HASFINDCOMPUTERS));
	fwprintf(f,L"\t\tSHRestricted(REST_NOCHANGESTARMENU)=%d\r\n",SHRestricted(REST_NOCHANGESTARMENU));
	fwprintf(f,L"\t\tSHRestricted(REST_NOCLOSE)=%d\r\n",SHRestricted(REST_NOCLOSE));
	fwprintf(f,L"\t\tSHRestricted(REST_NOCOMMONGROUPS)=%d\r\n",SHRestricted(REST_NOCOMMONGROUPS));
	fwprintf(f,L"\t\tSHRestricted(REST_NOCONTROLPANEL)=%d\r\n",SHRestricted(REST_NOCONTROLPANEL));
	fwprintf(f,L"\t\tSHRestricted(REST_NODISCONNECT)=%d\r\n",SHRestricted(REST_NODISCONNECT));
	fwprintf(f,L"\t\tSHRestricted(REST_NOFAVORITESMENU)=%d\r\n",SHRestricted(REST_NOFAVORITESMENU));
	fwprintf(f,L"\t\tSHRestricted(REST_NOFIND)=%d\r\n",SHRestricted(REST_NOFIND));
	fwprintf(f,L"\t\tSHRestricted(REST_NONETWORKCONNECTIONS)=%d\r\n",SHRestricted(REST_NONETWORKCONNECTIONS));
	fwprintf(f,L"\t\tSHRestricted(REST_NORECENTDOCSMENU)=%d\r\n",SHRestricted(REST_NORECENTDOCSMENU));
	fwprintf(f,L"\t\tSHRestricted(REST_NORUN)=%d\r\n",SHRestricted(REST_NORUN));
	fwprintf(f,L"\t\tSHRestricted(REST_NOSETFOLDERS)=%d\r\n",SHRestricted(REST_NOSETFOLDERS));
	fwprintf(f,L"\t\tSHRestricted(REST_NOSETTASKBAR)=%d\r\n",SHRestricted(REST_NOSETTASKBAR));
	fwprintf(f,L"\t\tSHRestricted(REST_NOSMEJECTPC)=%d\r\n",SHRestricted(REST_NOSMEJECTPC));
	fwprintf(f,L"\t\tSHRestricted(REST_NOSMHELP)=%d\r\n",SHRestricted(REST_NOSMHELP));
	fwprintf(f,L"\t\tSHRestricted(REST_NOSMMYDOCS)=%d\r\n",SHRestricted(REST_NOSMMYDOCS));
	fwprintf(f,L"\t\tSHRestricted(REST_NOSTRCMPLOGICAL)=%d\r\n",SHRestricted(REST_NOSTRCMPLOGICAL));
	fwprintf(f,L"\t\tSHRestricted(REST_STARTMENULOGOFF)=%d\r\n",SHRestricted(REST_STARTMENULOGOFF));
	fwprintf(f,L"\t\tSHRestricted(REST_FORCESTARTMENULOGOFF)=%d\r\n",SHRestricted(REST_FORCESTARTMENULOGOFF));

	{
		CRegKey regKey;
		const wchar_t *key=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
		if (regKey.Open(HKEY_LOCAL_MACHINE,key,KEY_READ|KEY_WOW64_64KEY)==ERROR_SUCCESS)
		{
			fwprintf(f,L"\r\n\tHKLM\\%s:\r\n",key);
			WriteRegKey(f,regKey,2);
			fwprintf(f,L"\r\n");
		}
	}

	{
		CRegKey regKey;
		const wchar_t *key=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
		if (regKey.Open(HKEY_CURRENT_USER,key,KEY_READ|KEY_WOW64_64KEY)==ERROR_SUCCESS)
		{
			fwprintf(f,L"\r\n\tHKCU\\%s:\r\n",key);
			WriteRegKey(f,regKey,2);
			fwprintf(f,L"\r\n");
		}
	}

	{
		CComString pPath;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Startup,0,NULL,&pPath)))
		{
			fwprintf(f,L"\r\n\t%s:\r\n",(const wchar_t*)pPath);
			WriteFolder(f,pPath,2,false);
			fwprintf(f,L"\r\n");
		}
	}

	{
		CComString pPath;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_CommonStartup,0,NULL,&pPath)))
		{
			fwprintf(f,L"\r\n\t%s:\r\n",(const wchar_t*)pPath);
			WriteFolder(f,pPath,2,false);
			fwprintf(f,L"\r\n");
		}
	}

	{
		CComPtr<ISearchManager> pSearchManager;
		pSearchManager.CoCreateInstance(CLSID_CSearchManager2);
		if (pSearchManager)
		{
			CComPtr<ISearchCatalogManager> pCatalogManager;
			pSearchManager->GetCatalog(L"SystemIndex",&pCatalogManager);
			if (pCatalogManager)
			{
				CComPtr<ISearchCrawlScopeManager> pSearchCrawlScopeManager;
				pCatalogManager->GetCrawlScopeManager(&pSearchCrawlScopeManager);
				if (pSearchCrawlScopeManager)
				{
					CComPtr<IEnumSearchRoots> pSearchRoots;
					pSearchCrawlScopeManager->EnumerateRoots(&pSearchRoots);
					if (pSearchRoots)
					{
						fwprintf(f,L"\r\nSearch Roots:\r\n");
						CComPtr<ISearchRoot> pSearchRoot;
						while (pSearchRoots->Next(1,&pSearchRoot,NULL)==S_OK)
						{
							PWSTR pszUrl=NULL;
							if (SUCCEEDED(pSearchRoot->get_RootURL(&pszUrl)))
							{
								fwprintf(f,L"\t%s\r\n",pszUrl);
								CoTaskMemFree(pszUrl);
							}
							pSearchRoot=NULL;
						}
						fwprintf(f,L"\r\n");
					}

					CComPtr<IEnumSearchScopeRules> pSearchRules;
					pSearchCrawlScopeManager->EnumerateScopeRules(&pSearchRules);
					if (pSearchRoots)
					{
						fwprintf(f,L"Search Rules:\r\n");
						CComPtr<ISearchScopeRule> pSearchRule;
						while (pSearchRules->Next(1,&pSearchRule,NULL)==S_OK)
						{
							BOOL fIncluded=FALSE;
							pSearchRule->get_IsIncluded(&fIncluded);
							PWSTR pszUrl=NULL;
							if (SUCCEEDED(pSearchRule->get_PatternOrURL(&pszUrl)))
							{
								fwprintf(f,L"\t%s: %s\r\n",fIncluded?L"Include":L"Exclude",pszUrl);
								CoTaskMemFree(pszUrl);
							}
							pSearchRule=NULL;
						}
						fwprintf(f,L"\r\n");
					}
				}
			}
		}
	}

	// programs
	fwprintf(f,L"\r\n\tInstalled Programs:\r\n");
	std::set<CString,CompareStrings> programs;

	CComPtr<IShellItem> pPrograms;
	SHGetKnownFolderItem(FOLDERID_ChangeRemovePrograms,KF_FLAG_DEFAULT,NULL,IID_IShellItem,(void**)&pPrograms);
	if (pPrograms)
	{
		CComPtr<IEnumShellItems> pEnum;
		pPrograms->BindToHandler(NULL,BHID_EnumItems,IID_IEnumShellItems,(void**)&pEnum);
		CComPtr<IShellItem> pProgram;
		while (pEnum && (pProgram=NULL,pEnum->Next(1,&pProgram,NULL))==S_OK)
		{
			CComString pName;
			pProgram->GetDisplayName(SIGDN_NORMALDISPLAY,&pName);
			CString name(pName);
			CComQIPtr<IShellItem2> pProgram2(pProgram);
			if (pProgram2)
			{
				CComString pVersion;
				if (SUCCEEDED(pProgram2->GetString(PKEY_ProductVersion,&pVersion)))
					name+=L"  ("+CString(pVersion)+L")";
			}
			programs.insert(name);
		}
	}

	for (std::set<CString,CompareStrings>::const_iterator it=programs.begin();it!=programs.end();++it)
		fwprintf(f,L"\t\t%s\r\n",(const wchar_t*)(*it));

	fwprintf(f,L"\r\nOpen-Shell\r\n");
	wchar_t csPath[_MAX_PATH]=L"";
	// classic shell version
	{
		CRegKey regKey;
		DWORD err=regKey.Open(HKEY_LOCAL_MACHINE,L"Software\\OpenShell\\OpenShell",KEY_READ|KEY_WOW64_64KEY);
		if (err!=ERROR_SUCCESS)
		{
			fwprintf(f,L"\tFailed to read HKLM\\Software\\OpenShell\\OpenShell - 0x%08X\r\n",err);
		}
		else
		{
			wchar_t language[100]=L"";
			ULONG size=_countof(language);
			if (regKey.QueryStringValue(L"DefaultLanguage",language,&size)==ERROR_SUCCESS)
				fwprintf(f,L"\tDefault language: '%s'\r\n",language);

			size=_countof(csPath);
			if (regKey.QueryStringValue(L"Path",csPath,&size)==ERROR_SUCCESS)
			{
				fwprintf(f,L"\tOpen-Shell path: '%s'\r\n",csPath);
				PathRemoveBackslash(csPath);
			}

			DWORD val;
			if (regKey.QueryDWORDValue(L"Version",val)==ERROR_SUCCESS)
				fwprintf(f,L"\tOpen-Shell version: %d.%d.%d\r\n",val>>24,(val>>16)&0xFF,val&0xFFFF);

			if (regKey.QueryDWORDValue(L"WinVersion",val)==ERROR_SUCCESS)
				fwprintf(f,L"\tWin version during installation: %d.%02d.%d\r\n",val>>24,(val>>16)&0xFF,val&0xFFFF);
		}
	}

	// language files
	fwprintf(f,L"\t%s:\r\n",csPath);
	if (csPath[0])
		WriteFolder(f,csPath,2,true);

	{
		wchar_t path[_MAX_PATH];
		Strcpy(path,_countof(path),L"%ALLUSERSPROFILE%\\OpenShell");
		DoEnvironmentSubst(path,_countof(path));
		fwprintf(f,L"\t%s:\r\n",path);
		WriteFolder(f,path,2,true);
	}

	// installed components and settings
	wchar_t fname[_MAX_PATH];
	Sprintf(fname,_countof(fname),L"%s\\ClassicExplorer32.dll",csPath);
	bool bClassicExplorer=GetFileAttributes(fname)!=INVALID_FILE_ATTRIBUTES;
	Sprintf(fname,_countof(fname),L"%s\\StartMenu.exe",csPath);
	bool bClassicMenu=GetFileAttributes(fname)!=INVALID_FILE_ATTRIBUTES;
	Sprintf(fname,_countof(fname),L"%s\\ClassicIE_32.exe",csPath);
	bool bClassicIE=GetFileAttributes(fname)!=INVALID_FILE_ATTRIBUTES;
	Sprintf(fname,_countof(fname),L"%s\\Update.exe",csPath);
	bool bClassicUpdate=GetFileAttributes(fname)!=INVALID_FILE_ATTRIBUTES;

	fwprintf(f,L"\r\nInstalled components:\r\n");
	if (bClassicExplorer)
	{
		fwprintf(f,L"  Classic Explorer\r\n");
		WriteSettings(f,COMPONENT_EXPLORER);
	}
	if (bClassicMenu)
	{
		fwprintf(f,L"  Open-Shell Menu\r\n");
		WriteSettings(f,COMPONENT_MENU);
	}
	if (bClassicIE)
	{
		fwprintf(f,L"  Classic IE\r\n");
		WriteSettings(f,COMPONENT_IE);
	}
	if (bClassicUpdate)
	{
		fwprintf(f,L"  Open-Shell Update\r\n\r\n");
	}
	fwprintf(f,L"  Shared Settings\r\n");
	WriteSettings(f,COMPONENT_SHARED);


	// check for disabled addons
	if (bClassicExplorer || bClassicIE)
	{
		fwprintf(f,L"Explorer addons:\r\n");
		CString text;
		if (ReadRegistryValue(HKEY_CURRENT_USER,L"Software\\Microsoft\\Internet Explorer\\Main",L"Enable Browser Extensions",text)==ERROR_SUCCESS)
			fwprintf(f,L"\tEnable Browser Extensions (user): %s\r\n",(const wchar_t*)text);
		if (ReadRegistryValue(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Internet Explorer\\Main",L"Enable Browser Extensions",text)==ERROR_SUCCESS)
			fwprintf(f,L"\tEnable Browser Extensions: %s\r\n",(const wchar_t*)text);
		if (ReadRegistryValue(HKEY_CURRENT_USER,L"Software\\Microsoft\\Internet Explorer\\Main",L"Isolation",text)==ERROR_SUCCESS)
			fwprintf(f,L"\tIsolation (user): %s\r\n",(const wchar_t*)text);
		if (ReadRegistryValue(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Internet Explorer\\Main",L"Isolation",text)==ERROR_SUCCESS)
			fwprintf(f,L"\tIsolation: %s\r\n",(const wchar_t*)text);
	}
	if (bClassicExplorer)
	{
		DWORD flags;
		if (ReadRegistryValue(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Ext\\Settings\\{553891B7-A0D5-4526-BE18-D3CE461D6310}",L"Flags",flags)==ERROR_SUCCESS)
			fwprintf(f,L"\tExplorerBand flags: 0x%08X\r\n",flags);
		CString policy;
		if (ReadRegistryValue(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Ext\\CLSID",L"{553891B7-A0D5-4526-BE18-D3CE461D6310}",policy)==ERROR_SUCCESS)
			fwprintf(f,L"\tExplorerBand policy: %s\r\n",(const wchar_t*)policy);
		if (ReadRegistryValue(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Ext\\Settings\\{449D0D6E-2412-4E61-B68F-1CB625CD9E52}",L"Flags",flags)==ERROR_SUCCESS)
			fwprintf(f,L"\tExplorerBHO flags: 0x%08X\r\n",flags);
		if (ReadRegistryValue(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Ext\\CLSID",L"{449D0D6E-2412-4E61-B68F-1CB625CD9E52}",policy)==ERROR_SUCCESS)
			fwprintf(f,L"\tExplorerBHO policy: %s\r\n",(const wchar_t*)policy);
	}
	if (bClassicIE)
	{
		DWORD flags;
		CString policy;
		if (ReadRegistryValue(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Ext\\Settings\\{EA801577-E6AD-4BD5-8F71-4BE0154331A4}",L"Flags",flags)==ERROR_SUCCESS)
			fwprintf(f,L"\tClassicIE flags: 0x%08X\r\n",flags);
		if (ReadRegistryValue(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Ext\\CLSID",L"{EA801577-E6AD-4BD5-8F71-4BE0154331A4}",policy)==ERROR_SUCCESS)
			fwprintf(f,L"\tClassicIE policy: %s\r\n",(const wchar_t*)policy);
	}
	if (bClassicExplorer || bClassicIE)
		fwprintf(f,L"\r\n");

	if (bClassicMenu)
	{
		if (HIWORD(winVer)>=WIN_VER_WIN8)
		{
			CString guid;
			LONG res=ReadRegistryValue(HKEY_CLASSES_ROOT,L"CLSID\\{ECD4FC4D-521C-11D0-B792-00A0C90312E1}\\TreatAs",NULL,guid);
			if (res!=ERROR_SUCCESS)
				fwprintf(f,L"TreatAs: 0x%08X\r\n",res);
			else
			{
				if (guid.IsEmpty())
					guid=L"(empty)";
				fwprintf(f,L"TreatAs: %s%s\r\n",(const wchar_t*)guid,_wcsicmp(guid,L"{D3214FBB-3CA1-406a-B3E8-3EB7C393A15E}")==0?L" (correct)":L" (wrong)");
			}

			CString emulation;
			res=ReadRegistryValue(HKEY_CLASSES_ROOT,L"CLSID\\{D3214FBB-3CA1-406A-B3E8-3EB7C393A15E}",NULL,emulation);
			if (res!=ERROR_SUCCESS)
				fwprintf(f,L"Emulation: 0x%08X\r\n",res);
			else
			{
				if (emulation.IsEmpty())
					emulation=L"(empty)";
				fwprintf(f,L"Emulation: %s%s\r\n",(const wchar_t*)emulation,_wcsicmp(emulation,L"StartMenuEmulation")==0?L" (correct)":L" (wrong)");
			}

			CString server;
			res=ReadRegistryValue(HKEY_CLASSES_ROOT,L"CLSID\\{D3214FBB-3CA1-406A-B3E8-3EB7C393A15E}\\InprocServer32",NULL,server);
			if (res!=ERROR_SUCCESS)
				fwprintf(f,L"Server: 0x%08X\r\n",res);
			else
			{
				const wchar_t *state=L" (wrong)";
				if (server.IsEmpty())
					server=L"(empty)";
				else if (GetFileAttributes(server)==INVALID_FILE_ATTRIBUTES)
					state=L" (missing file)";
				else
					state=L" (correct)";
				fwprintf(f,L"Server: %s%s\r\n",(const wchar_t*)server,state);
			}
		}
	}
}

static void WriteLogFileAdmin( FILE *f )
{
	fwprintf(f,L"\r\nServices:\r\n");
	{
		CRegKey regKey;
		if (regKey.Open(HKEY_LOCAL_MACHINE,L"SYSTEM\\ControlSet001\\Control\\ServiceGroupOrder",KEY_READ|KEY_WOW64_64KEY)==ERROR_SUCCESS)
			WriteRegKey(f,regKey,1);
		SC_HANDLE hManager=OpenSCManager(NULL,NULL,SC_MANAGER_ENUMERATE_SERVICE);
		
		if (hManager)
		{
			const wchar_t *status[]={
				L"",
				L"stopped",
				L"start pending",
				L"stop pending",
				L"running",
				L"continue pending",
				L"pause pending",
				L"paused",
			};
			std::vector<BYTE> buf(256*1024);
			DWORD size=0, count=0, resume=0;
			std::map<CString,GroupInfo,CompareStrings> services;
			if (EnumServicesStatusEx(hManager,SC_ENUM_PROCESS_INFO,SERVICE_DRIVER|SERVICE_WIN32,SERVICE_STATE_ALL,&buf[0],(int)buf.size(),&size,&count,&resume,NULL))
			{
				const ENUM_SERVICE_STATUS_PROCESS *pService=(ENUM_SERVICE_STATUS_PROCESS*)&buf[0];
				for (DWORD i=0;i<count;i++,pService++)
				{
					DWORD idx=pService->ServiceStatusProcess.dwCurrentState;
					if (idx>=_countof(status)) idx=0;
					GroupInfo &info=services[pService->lpServiceName];
					info.desc=pService->lpDisplayName;
					info.status=status[idx];
					CString name;
					name.Format(L"SYSTEM\\ControlSet001\\services\\%s",pService->lpServiceName);
					ReadRegistryValue(HKEY_LOCAL_MACHINE,name,L"Group",info.group);
				}
			}
			CloseServiceHandle(hManager);
			for (std::map<CString,GroupInfo,CompareStrings>::const_iterator it=services.begin();it!=services.end();++it)
				if (it->second.group.IsEmpty())
					fwprintf(f,L"\t%s (%s): %s\r\n",(const wchar_t*)it->first,(const wchar_t*)it->second.desc,it->second.status);
				else
					fwprintf(f,L"\t%s (%s): %s (%s)\r\n",(const wchar_t*)it->first,(const wchar_t*)it->second.desc,it->second.status,(const wchar_t*)it->second.group);
		}
	}

	HANDLE hToken;
	if (OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&hToken))
	{
		TOKEN_PRIVILEGES tp={1};
		if (LookupPrivilegeValue(NULL,L"SeDebugPrivilege",&tp.Privileges[0].Luid))
			tp.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
		AdjustTokenPrivileges(hToken,FALSE,&tp,sizeof(TOKEN_PRIVILEGES),NULL,NULL); 
		CloseHandle(hToken);
	}

	// processes
	fwprintf(f,L"\r\nProcesses:\r\n");
	std::vector<DWORD> explorers;
	std::vector<DWORD> menus;
	{
		DWORD processes[1024];
		DWORD cbNeeded;
		if (EnumProcesses(processes,sizeof(processes),&cbNeeded))
		{
			int count=cbNeeded/sizeof(DWORD);
			std::sort(processes,processes+count);
			for (int i=0;i<count;i++)
			{
				HANDLE hProcess=OpenProcess(PROCESS_QUERY_INFORMATION,FALSE,processes[i]);
				if (!hProcess)
					hProcess=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,processes[i]);
				if (hProcess)
				{
					wchar_t fname[_MAX_PATH];
					DWORD size=_countof(fname);
					if (QueryFullProcessImageName(hProcess,0,fname,&size))
					{
						const wchar_t *name=PathFindFileName(fname);
						if (_wcsicmp(name,L"explorer.exe")==0)
						{
							explorers.push_back(processes[i]);
						}
						else if (_wcsicmp(name,L"StartMenu.exe")==0)
						{
							menus.push_back(processes[i]);
						}
					}
					else
						fname[0]=0;
					fwprintf(f,L"\t%5d, %s",processes[i],fname);
					HANDLE hToken;
					const wchar_t *level=L"Unknown";
					if (OpenProcessToken(hProcess,TOKEN_QUERY|TOKEN_QUERY_SOURCE,&hToken)) 
					{
						DWORD dwLengthNeeded;
						if (!GetTokenInformation(hToken,TokenIntegrityLevel,NULL,0,&dwLengthNeeded))
						{
							TOKEN_MANDATORY_LABEL *pTIL=(TOKEN_MANDATORY_LABEL*)malloc(dwLengthNeeded);
							if (pTIL)
							{
								if (GetTokenInformation(hToken,TokenIntegrityLevel,pTIL,dwLengthNeeded,&dwLengthNeeded))
								{
									DWORD dwIntegrityLevel=*GetSidSubAuthority(pTIL->Label.Sid,(DWORD)(UCHAR)(*GetSidSubAuthorityCount(pTIL->Label.Sid)-1));
									if (dwIntegrityLevel>=SECURITY_MANDATORY_SYSTEM_RID)
										level=L"System";
									else if (dwIntegrityLevel>=SECURITY_MANDATORY_HIGH_RID)
										level=L"High";
									else if (dwIntegrityLevel>=SECURITY_MANDATORY_MEDIUM_RID)
										level=L"Medium";
									else
										level=L"Low";
								}
								free(pTIL);
							}
						}
						CloseHandle(hToken);
					}
					fwprintf(f,L"  (%s integrity level)\r\n",level);
					CloseHandle(hProcess);
				}
			}
		}
	}

	HWND progWin=FindWindowEx(NULL,NULL,L"Progman",NULL);
	DWORD desktopId=0;
	if (progWin)
		GetWindowThreadProcessId(progWin,&desktopId);

	// interesting processes
	for (std::vector<DWORD>::const_iterator it=explorers.begin();it!=explorers.end();++it)
	{
		fwprintf(f,L"\r\nExplorer process: %d%s\r\n",*it,(*it==desktopId)?L" (desktop)":L"");
		HANDLE hProcess=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,*it);
		if (hProcess)
		{
			WriteProcessInfo(f,hProcess,1);
			CloseHandle(hProcess);
		}
		else
			fwprintf(f,L"\tFailed to get modules\r\n");
	}
	for (std::vector<DWORD>::const_iterator it=menus.begin();it!=menus.end();++it)
	{
		fwprintf(f,L"\r\nStartMenu process: %d\r\n",*it);
		HANDLE hProcess=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,*it);
		if (hProcess)
		{
			WriteProcessInfo(f,hProcess,1);
			CloseHandle(hProcess);
		}
		else
			fwprintf(f,L"\tFailed to get modules\r\n");
	}
}

int SaveLogFile( const wchar_t *fname, bool bAdmin )
{
	FILE *f=NULL;
	if (_wfopen_s(&f,fname,bAdmin?L"ab":L"wb")!=0 || !f)
		return 1;

	fseek(f,0,SEEK_END);
	if (ftell(f)==0)
		fwrite(L"\xFEFF",2,1,f);
	CoInitialize(NULL);
	if (bAdmin)
		WriteLogFileAdmin(f);
	else
		WriteLogFile(f);
	fclose(f);

	if (!bAdmin)
	{
		wchar_t exe[_MAX_PATH];
		GetModuleFileName(NULL,exe,_countof(exe));
		wchar_t cmdLine[1024];
		Sprintf(cmdLine,_countof(cmdLine),L"saveloga \"%s\"",fname);
		if ((intptr_t)ShellExecute(NULL,L"runas",exe,cmdLine,NULL,SW_SHOWNORMAL)<=32)
		{
			f=NULL;
			if (_wfopen_s(&f,fname,L"ab")==0 && f)
			{
				WriteLogFileAdmin(f);
				fclose(f);
			}
		}
	}

	CoUninitialize();
	return 0;
}

bool ExtractUtility64( const wchar_t *fname, wchar_t *exe )
{
	FILE *f=NULL;
	if (_wfopen_s(&f,fname,L"wb")!=0 || !f)
		return false;

	fwprintf(f,L"\xFEFF");


	HRSRC hResInfo=FindResource(g_hInstance,MAKEINTRESOURCE(1),L"FILE");
	if (!hResInfo)
	{
		fwprintf(f,L"Error extracting Utility64.exe\r\n");
		fclose(f);
		return false;
	}
	HGLOBAL hRes=LoadResource(g_hInstance,hResInfo);
	void *ptr=LockResource(hRes);
	DWORD size=SizeofResource(g_hInstance,hResInfo);
	bool res=false;
	Strcpy(exe,_MAX_PATH,L"%TEMP%\\Utility64.exe");
	DoEnvironmentSubst(exe,_MAX_PATH);
	HANDLE h=CreateFile(exe,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
	if (h)
	{
		DWORD q;
		res=(WriteFile(h,ptr,size,&q,NULL) && q==size);
		CloseHandle(h);
	}
	if (!res)
		fwprintf(f,L"Error saving %s\r\n",exe);
	fclose(f);
	return res;
}

static void ShowSaveLogFileInternal( void )
{
	// save log file
	wchar_t fname[_MAX_PATH];
	fname[0]=0;
	OPENFILENAME ofn={sizeof(ofn)};
	ofn.lpstrFilter=L"Text files (*.txt)\0*.txt\0";
	ofn.nFilterIndex=1;
	ofn.lpstrFile=fname;
	ofn.nMaxFile=_MAX_PATH;
	ofn.lpstrTitle=L"Save log file";
	ofn.lpstrDefExt=L".txt";
	ofn.Flags=OFN_ENABLESIZING|OFN_EXPLORER|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT|OFN_HIDEREADONLY|OFN_NOCHANGEDIR;
	if (GetSaveFileName(&ofn))
	{
		wchar_t exe[_MAX_PATH];
		BOOL bWow64=FALSE;
		IsWow64Process(GetCurrentProcess(),&bWow64);
		if (bWow64)
		{
			if (!ExtractUtility64(fname,exe))
				return;
		}
		else
			GetModuleFileName(NULL,exe,_countof(exe));

		STARTUPINFO startupInfo={sizeof(startupInfo)};
		PROCESS_INFORMATION processInfo;
		memset(&processInfo,0,sizeof(processInfo));
		wchar_t cmdLine[1024];
		Sprintf(cmdLine,_countof(cmdLine),L"%s savelog \"%s\"",PathFindFileName(exe),fname);
		if (CreateProcess(exe,cmdLine,NULL,NULL,FALSE,0,NULL,NULL,&startupInfo,&processInfo))
		{
			CloseHandle(processInfo.hThread);
			WaitForSingleObject(processInfo.hProcess,INFINITE);
			CloseHandle(processInfo.hProcess);
		}
	}
}

void ShowSaveLogFile( void )
{
	CoInitialize(NULL);
	ShowSaveLogFileInternal();
	CoUninitialize();
}
