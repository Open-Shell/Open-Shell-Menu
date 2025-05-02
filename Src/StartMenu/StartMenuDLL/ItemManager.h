// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#pragma once

#include "ComHelper.h"
#include <map>
#include <set>
#include <list>
#include <vector>

interface IImageList2;
interface IWICImagingFactory;

// CItemManager - global cache for item information

class CItemManager;
extern CItemManager g_ItemManager;

class CItemManager
{
public:
	CItemManager( void );
	~CItemManager( void );

	static int SMALL_ICON_SIZE;
	static int LARGE_ICON_SIZE;
	static int EXTRA_LARGE_ICON_SIZE;

	// Initializes the manager. Called from DllMain
	void Init( void );
	void Close( void );
	void ResetTempIcons( void );
	void LoadCacheFile( void );
	void SaveCacheFile( void );
	void ClearCache( void );

	static int GetDPI( bool bOverride ) { return (bOverride && s_DPIOverride)?s_DPIOverride:s_DPI; }
	static bool GetDPIOverride( void ) { return s_DPIOverride!=0; }

	enum TIconSizeType
	{
		ICON_SIZE_TYPE_SMALL,
		ICON_SIZE_TYPE_LARGE,
		ICON_SIZE_TYPE_EXTRA_LARGE,
		ICON_SIZE_TYPE_SMALL_METRO,
		ICON_SIZE_TYPE_LARGE_METRO,
		ICON_SIZE_TYPE_EXTRA_LARGE_METRO,

		ICON_SIZE_COUNT
	};

	int GetIconSize(TIconSizeType iconSizeType) const;

	struct IconInfo
	{
		TIconSizeType sizeType;
		bool bTemp; // the icon will be destroyed when the menu closes
		bool bMetro; // this is a Metro icon. it may depend on the system color
		FILETIME timestamp;
		HBITMAP bitmap; // bitmaps are guaranteed to be valid on the main thread (if the handle is read atomically)

		void SetPath( const wchar_t *path );
		const CString &GetPath( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ICONS)); return PATH; }

		private:
			CString PATH; // metro icon paths start with # and are not saved to cache file

		friend class CItemManager;
	};

	enum
	{
		INFO_LINK=1, // bLink, appid, bNoPin, bNoNew, targetPidl, targetPath, arguments
		INFO_METRO=2, // bLink, bMetroLink, bMetroApp, appid, metroName, package, packagePath, iconPath, color
		INFO_LINK_APPID=4, // the appid is validated by the app resolver (for jumplists and UserAssist items). Can only be used from the main thread

		INFO_SMALL_ICON=16,
		INFO_LARGE_ICON=32,
		INFO_EXTRA_LARGE_ICON=64,

		INFO_NO_PATH=8192, // don't trust the parsing name
		INFO_VALIDATE_FILE=16384, // if the path doesn't exist returns NULL
		INFO_REFRESH_NOW=32768,
		INFO_STARTSCREEN_ICON=65536,

		INFO_DATA=INFO_LINK|INFO_METRO|INFO_LINK_APPID,
		INFO_ICON=INFO_SMALL_ICON|INFO_LARGE_ICON|INFO_EXTRA_LARGE_ICON,
	};

	enum TLocation
	{
		LOCATION_UNKNOWN,
		LOCATION_START_MENU,
		LOCATION_GAMES,
		LOCATION_DESKTOP,
		LOCATION_TASKBAR,
		LOCATION_METRO,
	};

	struct ItemInfo
	{
		ItemInfo( void )
		{
			smallIcon=largeIcon=extraLargeIcon=NULL;
			validFlags=refreshFlags=0;
			bIconOnly=bTemp=bLink=bExplicitAppId=bNoPin=bNoNew=bMetroLink=bMetroApp=bProtectedLink=false;
			writestamp.dwHighDateTime=writestamp.dwLowDateTime=0;
			createstamp.dwHighDateTime=createstamp.dwLowDateTime=0;
			location=LOCATION_UNKNOWN;
		}

		// PATH never changes after the item is created. it can be accessed without a lock
		CString PATH;

		// these are replaced atomically with pointers that are always valid
		const IconInfo *smallIcon;
		const IconInfo *largeIcon;
		const IconInfo *extraLargeIcon;

		const CAbsolutePidl &GetPidl( void ) const { Assert(GetCurrentThreadId()==g_ItemManager.m_MainThreadId || RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return pidl; }
		bool IsLink( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return bLink; }
		bool IsMetroLink( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return bMetroLink; }
		bool IsMetroApp( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return bMetroApp; }
		bool IsProtectedLink( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return bProtectedLink; }
		bool IsNoPin( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return bNoPin; }
		bool IsNoNew( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return bNoNew; }
		bool IsExplicitAppId( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return bExplicitAppId; }
		const CString &GetPath( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return path; }
		const CString &GetAppid( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return appid; }
		const CString &GetTargetPATH( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return targetPATH; }
		const CAbsolutePidl &GetTargetPidl( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return targetPidl; }
		const CString &GetMetroName( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return metroName; }
		const CString &GetIconPath( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return iconPath; }
		const CString &GetPackagePath( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return packagePath; }
		TLocation GetLocation( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return location; }

	private:
		CAbsolutePidl pidl;
		CAbsolutePidl newPidl;
		FILETIME writestamp; // valid only for items with paths. the rest are assumed to never change
		FILETIME createstamp; // valid only for items with paths. the rest are assumed to never change
		bool bIconOnly;
		bool bTemp; // the item and its icon will be destroyed when the menu closes (only allowed for small-icon items)
		bool bLink;
		bool bMetroLink;
		bool bMetroApp;
		bool bProtectedLink; // Metro link in the common Programs folder
		bool bNoPin; // the link shouldn't be pinned
		bool bNoNew; // the link shouldn't be new
		bool bExplicitAppId;
		CString path; // only for a file
		CString packagePath; // only for a metro app
		TLocation location;

		int validFlags;
		int refreshFlags; // 0 if not in the queue, the item can't be deleted if this is !=0

		CAbsolutePidl targetPidl;
		CString targetPATH;
		CString appid;
		CString metroName;
		CString iconPath;
		DWORD iconColor;

		int iconIndex; // used only if bIconOnly

		const CAbsolutePidl &GetLatestPidl( void ) const { Assert(RWLock::ThreadHasReadLock(RWLOCK_ITEMS)); return newPidl?newPidl:pidl; }

		friend class CItemManager;
	};

	const ItemInfo *GetItemInfo( IShellItem *pItem, PIDLIST_ABSOLUTE pidl, int refreshFlags, TLocation location=LOCATION_UNKNOWN );
	const ItemInfo *GetItemInfo( CString path, int refreshFlags, TLocation location=LOCATION_UNKNOWN );
	const ItemInfo *GetCustomIcon( const wchar_t *location, int index, TIconSizeType iconSizeType, bool bTemp );
	const ItemInfo *GetCustomIcon( const wchar_t *path, TIconSizeType iconSizeType );
	const ItemInfo* GetLinkIcon(IShellLink* link, TIconSizeType iconSizeType);
	const ItemInfo *GetMetroAppInfo10( const wchar_t *appid );
	void UpdateItemInfo( const ItemInfo *pInfo, int refreshFlags, bool bHasWriteLock=false );
	void WaitForShortcuts( const POINT &balloonPos );
	bool IsTaskbarPinned( const wchar_t *appid );
	void UpdateNewPrograms( const POINT &balloonPos );
	bool IsNewProgram( PIDLIST_ABSOLUTE pidl, bool bFolder, bool bMetroApp );
	bool HasNewPrograms( bool bReal ) { return m_bHasNewPrograms[bReal?0:1]; }
	bool HasNewApps( bool bReal ) { return m_bHasNewApps[bReal?0:1]; }
	void RefreshInfos( void );

	void RemoveNewItem( PIDLIST_ABSOLUTE pItem1, PIDLIST_ABSOLUTE pItem2, bool bFolder );
	void RemoveNewItems( bool bPrograms, bool bMetro );
	void SaveOldItems( void );

	enum TRWLock
	{
		RWLOCK_ITEMS,
		RWLOCK_ICONS,
		RWLOCK_COUNT,
	};

	class RWLock
	{
	public:
		RWLock( CItemManager *pThis, bool bWrite, TRWLock index )
		{
			if (index==RWLOCK_COUNT)
			{
				m_pLock=NULL;
				return;
			}
#ifdef _DEBUG
			m_pState=&g_LockState[index];
			Assert(*m_pState==0);
#endif
			m_pLock=&pThis->m_RWLocks[index];
			m_bWrite=bWrite;
			if (bWrite)
				AcquireSRWLockExclusive(m_pLock);
			else
				AcquireSRWLockShared(m_pLock);
#ifdef _DEBUG
			*m_pState=bWrite?2:1;
#endif
		}

		~RWLock( void )
		{
			if (!m_pLock) return;
#ifdef _DEBUG
			Assert(*m_pState==(m_bWrite?2:1));
			*m_pState=0;
#endif
			if (m_bWrite)
				ReleaseSRWLockExclusive(m_pLock);
			else
				ReleaseSRWLockShared(m_pLock);
		}

#ifdef _DEBUG
	static bool ThreadHasReadLock( TRWLock index ) { return g_LockState[index]!=0; }
	static bool ThreadHasWriteLock( TRWLock index ) { return g_LockState[index]==2; }
#endif

	private:
		SRWLOCK *m_pLock;
		bool m_bWrite;

#ifdef _DEBUG
		int *m_pState;
		static _declspec(thread) int g_LockState[RWLOCK_COUNT]; // 0 - none, 1 - read, 2 - write
#endif
	};

private:
	static int s_DPI;
	static int s_DPIOverride;

	enum TLock
	{
		LOCK_CLEANUP,
		LOCK_COUNT,
	};

	SRWLOCK m_RWLocks[RWLOCK_COUNT];
	CRITICAL_SECTION m_CriticalSections[LOCK_COUNT];
	DWORD m_CriticalSectionOwners[LOCK_COUNT];
	HANDLE m_StartEvent; // start the refresh thread
	HANDLE m_WorkEvent; // kicks off the refresh thread
	HANDLE m_ExitEvent; // exit all threads
	HANDLE m_DoneEvent; // done preloading start menu items
	HANDLE m_PreloadItemsThread;
	HANDLE m_RefreshInfoThread;
	HANDLE m_SaveCacheThread;
	DWORD m_MainThreadId, m_PreloadItemsThreadId, m_RefreshInfoThreadId;

	// per-thread info used to load icons
	struct LoadIconData
	{
		int m_IconSizes[ICON_SIZE_COUNT];
		HIMAGELIST m_TempLists[ICON_SIZE_COUNT];
		CComPtr<IImageList2> m_pTempLists[ICON_SIZE_COUNT];
		CComPtr<IWICImagingFactory> m_pFactory;

		void Init( void );
		void Close( void );
	};

	LoadIconData m_LoadIconData[3]; // one for each thread (main, preload, refresh)
	LoadIconData &GetLoadIconData( void );

	class Lock
	{
	public:
		Lock( CItemManager *pThis, TLock index )
		{
			m_pSection=&pThis->m_CriticalSections[index];
			EnterCriticalSection(m_pSection);
			m_pOwner=&pThis->m_CriticalSectionOwners[index];
			if (!*m_pOwner)
				*m_pOwner=GetCurrentThreadId();
			else
				m_pOwner=NULL;
		}

		~Lock( void )
		{
			if (m_pOwner) *m_pOwner=0;
			LeaveCriticalSection(m_pSection);
		}

	private:
		CRITICAL_SECTION *m_pSection;
		DWORD *m_pOwner;
	};

	bool ThreadHasLock( TLock index ) { return m_CriticalSectionOwners[index]==GetCurrentThreadId(); }

	// requires LOCK_ITEMS to be held
	void QueueItemInfo( ItemInfo *pInfo, int refreshFlags );
	// doesn't require a lock
	void RefreshItemInfo( ItemInfo *pInfo, int refreshFlags, IShellItem *pItem, bool bHasWriteLock );

	void FindInCache( unsigned int hash, int &refreshFlags, const IconInfo *&smallIcon, const IconInfo *&largeIcon, const IconInfo *&extraLargeIcon );
	void StoreInCache( unsigned int hash, const wchar_t *path, HBITMAP hSmallBitmap, HBITMAP hLargeBitmap, HBITMAP hExtraLargeBitmap, int refreshFlags, const IconInfo *&smallIcon, const IconInfo *&largeIcon, const IconInfo *&extraLargeIcon, bool bTemp, bool bMetro );
	void LoadShellIcon( IShellItem *pItem, int refreshFlags, const IconInfo *&smallIcon, const IconInfo *&largeIcon, const IconInfo *&extraLargeIcon, const DWORD *pMetroColor );
	void LoadMetroIcon( IShellItem *pItem, int &refreshFlags, const IconInfo *&smallIcon, const IconInfo *&largeIcon, const IconInfo *&extraLargeIcon, const DWORD *pMetroColor );
	void LoadCustomIcon( const wchar_t *iconPath, int iconIndex, int refreshFlags, const IconInfo *&smallIcon, const IconInfo *&largeIcon, const IconInfo *&extraLargeIcon, bool bTemp );
	HICON LoadShellIcon( int index, int iconSize );
	HICON LoadShellIcon( int iconSize, IExtractIcon *pExtractW, const wchar_t *location, IExtractIconA *pExtractA, const char *locationA, int index );
	HBITMAP BitmapFromIcon( HICON hIcon, int iconSize, bool bDestroyIcon=true );

	bool m_bInitialized;

	// sizes for all shell image lists <size, list>
	std::vector<std::pair<int,int>> m_ListSizes;

	// the key is a hash of the path or the pidl
	std::multimap<unsigned int,ItemInfo> m_ItemInfos;

	// the key is a hash of the uppercase appid (win10 only)
	std::map<unsigned int,const ItemInfo*> m_MetroItemInfos10;
	// hashes of appids that are for sure not valid
	std::set<unsigned int> m_BlackListInfos10;

	// the key is a hash of the location and index
	std::multimap<unsigned int,IconInfo> m_IconInfos;

	// bitmaps that were replaced but may still be used by the main thread
	std::vector<HBITMAP> m_OldBitmaps;

	const IconInfo *m_DefaultSmallIcon;
	const IconInfo *m_DefaultLargeIcon;
	const IconInfo *m_DefaultExtraLargeIcon;

	// list of items to process in background
	std::list<ItemInfo*> m_ItemQueue;
	std::list<ItemInfo*> m_ItemQueueLow; // lower priority

	std::vector<const ItemInfo*> m_NewPrograms;
	std::vector<const ItemInfo*> m_NewProgramRoots;
	unsigned int m_TransientHash;
	bool m_bHasNewPrograms[2]; // 0 - for real, 1 - filtered
	bool m_bHasNewApps[2]; // 0 - for real, 1 - filtered

	bool m_bPreloadIcons;
	bool m_bPreloadFavorites;
	enum TLoadingStage
	{
		LOAD_STOPPED, // the loading threads are not running
		LOAD_STOPPING, // the loading threads are stopping
		LOAD_LOADING, // the loading threads are running
	};
	volatile TLoadingStage m_LoadingStage;
	int m_LastCacheSave;
	COLORREF m_OldSysAccentColor;
	bool m_bOldInvertIcons;

	void LoadFolderItems( IShellItem *pFolder, int refreshFlags, int levels, TLocation location );
	void LoadMetroItems( int refreshFlags );
	void PreloadItemsThread( void );
	void CreateDefaultIcons( void );
	static DWORD CALLBACK StaticPreloadItemsThread( void *param );
	void RefreshInfoThread( void );
	static DWORD CALLBACK StaticRefreshInfoThread( void *param );
	static DWORD CALLBACK SaveCacheFileThread( void *param );

	// all paths are in caps and end with \ 
	CString m_RootStartMenu1;
	CString m_RootStartMenu2;
	CString m_RootStartMenu3;
	CString m_RootCommonPrograms;
	CString m_RootGames;
	CString m_RootDesktop;
	CString m_RootTaskbar;
	CString m_RootMetro;
	// can be called from any thread
	TLocation DetermineLocation( const wchar_t *PATH );

	struct ModuleInfo
	{
		CString PATH;
		FILETIME timestamp;
	};
	static bool CompareModuleTimeStamp( const CString &PATH, const FILETIME &timestamp, std::vector<ModuleInfo> &modules );

	struct KnownPathGuid
	{
		CComString path;
		CComString guid;
		int pathLen;
	};

	struct OldItemInfo
	{
		unsigned int hash;
		FILETIME timestamp;

		bool operator<( const OldItemInfo &x ) const { return hash<x.hash; }
	};

	std::vector<OldItemInfo> m_OldItemInfos;

	void LoadOldItems( void );
	bool IsPathUsed( CRegKey &regKey, const wchar_t *path, const FILETIME &createstamp, const KnownPathGuid *knownPaths, int knownPathsCount, bool bMetroApp );
	void AddOldItems( const std::vector<unsigned> &hashes );
};

CString GetPropertyStoreString( IPropertyStore *pStore, REFPROPERTYKEY key );

class CShellItemEnumerator
{
public:
	CShellItemEnumerator( IShellItem *pFolder );
	bool IsValid( void ) const;
	bool GetNext( CComPtr<IShellItem> &pChild, CAbsolutePidl &childPidl );

private:
	CComPtr<IEnumIDList> m_pEnumPidls;
	CComPtr<IEnumShellItems> m_pEnumItems;
	CAbsolutePidl m_Root;
};

struct UserAssistData
{
	int pad1;
	int count;
	int pad2[2];
	float history[10];
	int last;
	FILETIME timestamp;
	int pad3;
};

void EncodeUserAssistPath( wchar_t *path );
void EncodeRot13( wchar_t *text );

enum TNetworkType
{
	NETWORK_NONE,
	NETWORK_SERVER, // \\server
	NETWORK_SHARE,  // \\server\share
	NETWORK_DRIVE,  // Q:
	NETWORK_FOLDER, // either \\server\share\folder or Q:\folder
	NETWORK_FILE, // something with extension
};

HRESULT MenuParseDisplayName( const wchar_t *path, PIDLIST_ABSOLUTE *pPidl, SFGAOF *pFlags, TNetworkType *pNetworkType );
const wchar_t *GetDefaultNetworkIcon( TNetworkType networkType );
bool MenuGetFileTimestamp( const wchar_t *path, FILETIME *pWriteTime, FILETIME *pCreateTime );
STDAPI ShGetKnownFolderPath( REFKNOWNFOLDERID rfid, PWSTR *pPath );
STDAPI ShGetKnownFolderIDList(REFKNOWNFOLDERID rfid, PIDLIST_ABSOLUTE *pPidl );
STDAPI ShGetKnownFolderItem(REFKNOWNFOLDERID rfid, IShellItem **ppItem );
HBITMAP ColorizeMonochromeImage(HBITMAP bitmap, DWORD color);

#define TASKBAR_PINNED_ROOT L"%APPDATA%\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar"
#define STARTSCREEN_COMMAND L"startscreen.lnk"
#define USERASSIST_LINKS_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\UserAssist\\{F4E57C4B-2036-45F0-A9AB-443BCFE33D9F}\\Count"
#define USERASSIST_APPIDS_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\UserAssist\\{CEBFF5CD-ACE2-4F4F-9178-9926F41749EA}\\Count"


//#define STARTSCREEN_WIN7
#ifdef BUILD_SETUP
#undef STARTSCREEN_WIN7
#endif
