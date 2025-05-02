// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#pragma once

#include "ItemManager.h"
#include <atldbcli.h>
#include <vector>
#include <list>

const int MAX_SEARCH_RESULTS=100; // per category

class CSearchManager
{
public:
	CSearchManager( void );
	~CSearchManager( void );
	void Init( void );
	void Close( void );


	enum TItemCategory
	{
		CATEGORY_INVALID,
		CATEGORY_PROGRAM,
		CATEGORY_SETTING,
		CATEGORY_METROSETTING,
		CATEGORY_FILE,
		CATEGORY_ITEM,
		CATEGORY_INTERNET,
		CATEGORY_AUTOCOMPLETE,

		CATEGORY_MASK=15
	};

	struct SearchCategory
	{
		SearchCategory( void ) = default;
		SearchCategory( const SearchCategory &cat )
		{
			search.Clone(cat.search);
			categoryHash=cat.categoryHash;
			name=cat.name;
			items=cat.items;
		}
		CAbsolutePidl search;
		unsigned int categoryHash;
		CString name;
		struct Item
		{
			CString name;
			CAbsolutePidl pidl;
		};
		std::vector<Item> items;
	};

	struct SearchResults
	{
		bool bSearching;
		bool bResults;
		CString currentString;
		CString autoCompletePath;
		std::vector<const CItemManager::ItemInfo*> programs;
		std::vector<const CItemManager::ItemInfo*> settings;
		std::vector<const CItemManager::ItemInfo*> metrosettings;
		std::vector<const CItemManager::ItemInfo*> autocomplete;
		std::list<SearchCategory> indexed;
	};

	void BeginSearch( const CString &searchText );
	void GetSearchResults( SearchResults &results );
	void AddItemRank( unsigned int hash );
	void CloseMenu( void );

	void LaunchExternalSearch( PIDLIST_ABSOLUTE root, unsigned int categoryHash, const CString &searchText );
	void LaunchInternetSearch( const CString &searchText );

private:
	struct ItemRank
	{
		unsigned int hash; // hash of the item name in caps
		int rank; // number of times it was used
		int lastTime; // the last time it was updated (hi dword of FILETIME)

		ItemRank( unsigned int _hash=0, int _rank=0, int _lastTime=0 ) { hash=_hash; rank=_rank; lastTime=_lastTime; }
		bool operator<( const ItemRank &rank ) const { return hash<rank.hash; }
	};

	bool m_bRanksLoaded;

	struct SearchItem
	{
		TItemCategory category;
		CString name; // uppercase
		CString keywords; // uppercase
		const CItemManager::ItemInfo *pInfo;
		int rank; // ignore the item if rank<0
		bool bMetroLink;

		SearchItem( void ) { category=CATEGORY_INVALID; pInfo=NULL; rank=0; bMetroLink=false; }

		// 0 - no match, 1 - match name, 2 - match keywords
		int MatchText( const wchar_t *search, bool bSearchSubWord ) const { return MatchTextInt(search,name,bSearchSubWord)?2:(MatchTextInt(search,keywords,bSearchSubWord)?1:0); }
		bool operator<( const SearchItem &item ) const { return rank>item.rank || (rank==item.rank && wcscmp(name,item.name)<0); }

	private:
		static bool MatchTextInt( const wchar_t *search, const CString &text, bool bSearchSubWord );
	};

	bool m_bInitialized;

	CString m_SearchText;
	CString m_AutoCompletePath;

	volatile int m_LastRequestId;
	volatile int m_LastProgramsRequestId;
	volatile int m_LastCompletedId;

	struct SearchRequest
	{
		int requestId;
		bool bSearchPrograms;
		bool bSearchPath;
		bool bSearchMetroApps;
		bool bSearchMetroSettings;
		bool bSearchSettings;
		bool bSearchKeywords;
		bool bSearchFiles;
		bool bSearchMetadata;
		bool bSearchTypes;
		bool bSearchSubWord;
		bool bUseRanks;
		bool bNoCommonFolders;
		bool bPinnedFolder;
		DWORD searchTime;
		CString searchText;
		CString autoCompletePath;
	};

	// LOCK_DATA
	SearchRequest m_SearchRequest;
	std::vector<SearchItem> m_ProgramItems; // also LOCK_PROGRAMS
	std::vector<SearchItem> m_SettingsItems; // also LOCK_PROGRAMS
	std::vector<SearchItem> m_ProgramItemsOld;
	std::vector<SearchItem> m_SettingsItemsOld;
	unsigned int m_ProgramsHash;
	unsigned int m_ProgramsHashOld;
	unsigned int m_SettingsHash;
	unsigned int m_SettingsHashOld;
	bool m_bProgramsFound;
	bool m_bSettingsFound;
	bool m_bMetroSettingsFound = false;
	std::vector<SearchItem> m_AutoCompleteItems;
	std::list<SearchCategory> m_IndexedItems;
	std::vector<ItemRank> m_ItemRanks;
	CString m_LastAutoCompletePath;

	enum
	{
		COLLECT_RECURSIVE  =0x01, // go into subfolders
		COLLECT_PROGRAMS   =0x02, // only collect programs (.exe, .com, etc)
		COLLECT_FOLDERS    =0x04, // include folder items
		COLLECT_METRO      =0x08, // check for metro links (non-recursive)
		COLLECT_ONLY_METRO =0x10, // collect only metro links
		COLLECT_KEYWORDS   =0x20, // include the keywords
		COLLECT_LIBRARY    =0x40, // the folder is a library
		COLLECT_NOREFRESH  =0x80, // suppress the refresh message

		COLLECT_IS_FOLDER =0x8000
	};

	bool AddSearchItem( IShellItem *pItem, const wchar_t *name, int flags, TItemCategory category, SearchRequest &searchRequest );
	void CollectSearchItems( IShellItem *pFolder, int flags, TItemCategory category, SearchRequest &searchRequest );
	void CollectIndexItems( IShellItem *pFolder, int flags, TItemCategory category, const wchar_t *groupName );

	enum TLock
	{
		LOCK_DATA,
		LOCK_PROGRAMS,
		LOCK_RANKS,
		LOCK_COUNT,
	};

	CRITICAL_SECTION m_CriticalSections[LOCK_COUNT];
	DWORD m_CriticalSectionOwners[LOCK_COUNT];

	class Lock
	{
	public:
		Lock( CSearchManager *pThis, TLock index )
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

	HANDLE m_SearchEvent;
	HANDLE m_ExitEvent;
	HANDLE m_SearchThreads[8];
	volatile long m_SearchThreadCount;
	DWORD m_MainThreadId;

	void LoadItemRanks( void );
	void SearchThread( void );
	static DWORD CALLBACK StaticSearchThread( void *param );

	static bool CmpRankTime( const CSearchManager::ItemRank &rank1, const CSearchManager::ItemRank &rank2 );
	static unsigned int CalcItemsHash( const std::vector<SearchItem> &items );

	struct SearchScope
	{
		SearchScope( void ) { resultCount=0; categoryHash=0; bFiles=true; bCommunications=false; }
		CAbsolutePidl search;
		unsigned int categoryHash;
		CString name;
		bool bFiles;
		bool bCommunications;

		std::vector<CString> roots;
		int resultCount;

		bool ParseSearchConnector( const wchar_t *fname );
	};

	class CDataAccessor
	{
	public:
		wchar_t itemUrl[_MAX_PATH];
		wchar_t itemType[_MAX_PATH];
		wchar_t parsingPath[_MAX_PATH];
		wchar_t displayPath[_MAX_PATH];
		wchar_t displayName[_MAX_PATH];

	//Output Accessor
	BEGIN_COLUMN_MAP(CDataAccessor)
		COLUMN_ENTRY(1, itemUrl)
		COLUMN_ENTRY(2, itemType)
		COLUMN_ENTRY(3, parsingPath)
		COLUMN_ENTRY(4, displayPath)
		COLUMN_ENTRY(5, displayName)
	END_COLUMN_MAP()
	};
};

extern CSearchManager g_SearchManager;

bool HasSearchService( void );
