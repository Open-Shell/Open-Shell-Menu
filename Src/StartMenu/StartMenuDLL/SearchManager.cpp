// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include "SearchManager.h"
#include "MenuContainer.h"
#include "MetroLinkManager.h"
#include "Settings.h"
#include "SettingsUIHelper.h"
#include "SettingsUI.h"
#include "FNVHash.h"
#include "ResourceHelper.h"
#include "LogManager.h"
#include "Translations.h"
#include <propkey.h>
#include <searchapi.h>
#include <ntquery.h>
#include <algorithm>

// attempt to get more accurate PIDLs for the search results by laundering them through a search folder.
// this produces better search items for the CSC search connector (better icons and behavior) but is slow.
//#define LAUNDER_SEARCH_RESULTS

CSearchManager g_SearchManager;
const CLSID CLSID_CSearchManager2={0x7D096C5F,0xAC08,0x4f1f,{0xBE,0xB7,0x5C,0x22,0xC5,0x17,0xCE,0x39}};

const int RANK_LIST_VERSION=1;
const int RANK_LIST_SIZE=256;

CSearchManager::CSearchManager( void )
{
	m_bInitialized=false;
	m_bRanksLoaded=false;
	memset(m_CriticalSections,0,sizeof(m_CriticalSections));
	memset(m_CriticalSectionOwners,0,sizeof(m_CriticalSectionOwners));
	m_SearchEvent=NULL;
	m_ExitEvent=NULL;
	memset(m_SearchThreads,0,sizeof(m_SearchThreads));
	m_MainThreadId=0;
	m_LastRequestId=0;
	m_LastCompletedId=0;
	m_LastProgramsRequestId=0;
	m_SearchThreadCount=0;
	m_bProgramsFound=m_bSettingsFound=false;
	m_ProgramsHash=m_ProgramsHashOld=m_SettingsHash=m_SettingsHashOld=FNV_HASH0;
}

CSearchManager::~CSearchManager( void )
{
	Close();
}

void CSearchManager::Init( void )
{
	m_bInitialized=true;
	for (int i=0;i<LOCK_COUNT;i++)
		InitializeCriticalSection(&m_CriticalSections[i]);
	m_SearchEvent=CreateEvent(NULL,FALSE,FALSE,NULL);
	m_ExitEvent=CreateEvent(NULL,TRUE,FALSE,NULL);
	for (int i=0;i<_countof(m_SearchThreads);i++)
		m_SearchThreads[i]=CreateThread(NULL,0,StaticSearchThread,this,0,NULL);
	m_MainThreadId=GetCurrentThreadId();
	LoadItemRanks();
}

void CSearchManager::Close( void )
{
	if (!m_bInitialized) return;
	SetEvent(m_ExitEvent);
	WaitForMultipleObjects(_countof(m_SearchThreads),m_SearchThreads,TRUE,INFINITE);
	CloseHandle(m_SearchEvent);
	for (int i=0;i<_countof(m_SearchThreads);i++)
		CloseHandle(m_SearchThreads[i]);
	for (int i=0;i<LOCK_COUNT;i++)
		DeleteCriticalSection(&m_CriticalSections[i]);
	m_bInitialized=false;
}

static CString ParseAutoCompletePath( const CString &searchText )
{
	if (GetSettingBool(L"SearchAutoComplete"))
	{
		const wchar_t *str=searchText;
		if (str[0]>='A' && str[0]<='Z' && str[1]==':')
		{
			const wchar_t *end=wcsrchr(str+2,'\\');
			if (end)
				return searchText.Left((int)(end-str));
			return searchText;
		}
		if (str[0]=='\\' && str[1]=='\\')
		{
			const wchar_t *end=wcsrchr(str+2,'\\');
			if (end)
				return searchText.Left((int)(end-str));
		}
		if (str[0]=='%')
		{
			const wchar_t *end=wcsrchr(str+1,'\\');
			if (end)
				return searchText.Left((int)(end-str));
		}
	}
	return CString();
}

void CSearchManager::BeginSearch( const CString &searchText )
{
	Assert(GetCurrentThreadId()==m_MainThreadId);

	{
		Lock lock(this,LOCK_DATA);
		m_SearchText=searchText;

		// initialize the request with unique ID
		m_SearchRequest.requestId=++m_LastRequestId;
		m_SearchRequest.bSearchPrograms=GetSettingBool(L"SearchPrograms");
		m_SearchRequest.bSearchPath=GetSettingBool(L"SearchPath");
		m_SearchRequest.bSearchMetroApps=GetSettingBool(L"SearchMetroApps");
		m_SearchRequest.bSearchMetroSettings=GetSettingBool(L"SearchMetroSettings");
		m_SearchRequest.bSearchSettings=m_SearchRequest.bSearchPrograms; //GetSettingBool(L"SearchSettings");
		m_SearchRequest.bSearchKeywords=GetSettingBool(L"SearchKeywords");
		m_SearchRequest.bSearchFiles=GetSettingBool(L"SearchFiles");
		m_SearchRequest.bSearchMetadata=GetSettingBool(L"SearchContents");
		m_SearchRequest.bSearchTypes=GetSettingBool(L"SearchCategories");
		m_SearchRequest.bSearchSubWord=GetSettingBool(L"SearchSubWord");
		m_SearchRequest.bUseRanks=GetSettingBool(L"SearchTrack");
		m_SearchRequest.bNoCommonFolders=(SHRestricted(REST_NOCOMMONGROUPS)!=0);
		m_SearchRequest.bPinnedFolder=(GetSettingInt(L"PinnedPrograms")==PINNED_PROGRAMS_PINNED);
		m_SearchRequest.searchText=searchText;
		m_SearchRequest.autoCompletePath=ParseAutoCompletePath(searchText);
	}
	SetEvent(m_SearchEvent);
}

void CSearchManager::CloseMenu( void )
{
	Lock lock(this,LOCK_DATA);
	m_LastRequestId++;
	m_LastProgramsRequestId=m_LastRequestId;
	if (g_LogCategories & LOG_SEARCH)
	{
		for (const auto& item : m_ProgramItems)
		{
			if (item.category == CATEGORY_PROGRAM)
				LOG_MENU(LOG_SEARCH, L"Program: '%s', %d", item.name, item.rank);
		}

		std::sort(m_SettingsItems.begin(), m_SettingsItems.end());

		for (const auto& item : m_SettingsItems)
		{
			if (item.category == CATEGORY_SETTING)
				LOG_MENU(LOG_SEARCH, L"Setting: '%s', %d", item.name, item.rank);
		}
		for (const auto& item : m_SettingsItems)
		{
			if (item.category == CATEGORY_METROSETTING)
				LOG_MENU(LOG_SEARCH, L"MetroSetting: '%s', %d", item.name, item.rank);
		}
	}
	if (m_bProgramsFound)
	{
		m_ProgramItemsOld.swap(m_ProgramItems);
		m_ProgramsHashOld=m_ProgramsHash;
	}
	m_ProgramItems.clear();
	m_ProgramsHash=FNV_HASH0;
	m_bProgramsFound=false;

	if (m_bSettingsFound)
	{
		m_SettingsItemsOld.swap(m_SettingsItems);
		m_SettingsHashOld=m_SettingsHash;
	}
	m_SettingsItems.clear();
	m_SettingsHash=FNV_HASH0;
	m_bSettingsFound=false;
	m_bMetroSettingsFound = false;

	m_IndexedItems.clear();
	m_AutoCompleteItems.clear();
	m_AutoCompletePath.Empty();
	m_LastAutoCompletePath.Empty();
}

bool CSearchManager::CmpRankTime( const CSearchManager::ItemRank &rank1, const CSearchManager::ItemRank &rank2 )
{
	return rank1.lastTime>rank2.lastTime;
}

unsigned int CSearchManager::CalcItemsHash( const std::vector<SearchItem> &items )
{
	unsigned int hash=FNV_HASH0;
	for (std::vector<SearchItem>::const_iterator it=items.begin();it!=items.end();++it)
	{
		hash=CalcFNVHash(&it->category,sizeof(TItemCategory),hash);
		hash=CalcFNVHash(it->name,hash);
		hash=CalcFNVHash(it->keywords,hash);
		hash=CalcFNVHash(&it->pInfo,sizeof(void*),hash);
		hash=CalcFNVHash(&it->rank,sizeof(int),hash);
		hash=CalcFNVHash(&it->bMetroLink,sizeof(bool),hash);
	}
	return hash;
}

void CSearchManager::LoadItemRanks( void )
{
	Assert(GetCurrentThreadId()==m_MainThreadId);
	Lock lock(this,LOCK_RANKS);
	m_ItemRanks.clear();
	if (GetSettingBool(L"SearchTrack"))
	{
		CRegKey regKey;
		if (regKey.Open(HKEY_CURRENT_USER,GetSettingsRegPath(),KEY_READ)==ERROR_SUCCESS)
		{
			ULONG size=0;
			regKey.QueryBinaryValue(L"ItemRanks",NULL,&size);
			if (size>0 && (size%sizeof(ItemRank))==0)
			{
				m_ItemRanks.resize(size/sizeof(ItemRank));
				regKey.QueryBinaryValue(L"ItemRanks",&m_ItemRanks[0],&size);
				if (m_ItemRanks[0].hash!='CLSH' || m_ItemRanks[0].rank!=RANK_LIST_VERSION)
					m_ItemRanks.clear();
				else if (m_ItemRanks.size()>RANK_LIST_SIZE)
				{
					std::sort(m_ItemRanks.begin()+1,m_ItemRanks.end(),CmpRankTime);
					m_ItemRanks.resize(RANK_LIST_SIZE);
				}
			}
		}
		if (m_ItemRanks.empty())
		{
			ItemRank rank('CLSH',RANK_LIST_VERSION);
			m_ItemRanks.push_back(rank);
		}
		std::sort(m_ItemRanks.begin()+1,m_ItemRanks.end());
	}
	m_bRanksLoaded=true;
}

void CSearchManager::AddItemRank( unsigned int hash )
{
	Assert(GetCurrentThreadId()==m_MainThreadId);
	Assert(m_bRanksLoaded);
	Lock lock(this,LOCK_RANKS);
	if (GetSettingBool(L"SearchTrack"))
	{
		FILETIME curTime;
		GetSystemTimeAsFileTime(&curTime);
		ItemRank rank(hash,1,curTime.dwHighDateTime);
		std::vector<ItemRank>::iterator it=std::lower_bound(m_ItemRanks.begin()+1,m_ItemRanks.end(),rank);
		if (it!=m_ItemRanks.end() && it->hash==rank.hash)
		{
			it->rank++;
			it->lastTime=curTime.dwHighDateTime;
		}
		else
		{
			m_ItemRanks.push_back(rank);
			std::sort(m_ItemRanks.begin()+1,m_ItemRanks.end());
		}
	}
	else
	{
		m_ItemRanks.clear();
		ItemRank rank('CLSH',RANK_LIST_VERSION);
		m_ItemRanks.push_back(rank);
	}
	CRegKey reg;
	if (reg.Open(HKEY_CURRENT_USER,GetSettingsRegPath())!=ERROR_SUCCESS)
		reg.Create(HKEY_CURRENT_USER,GetSettingsRegPath());

	reg.SetBinaryValue(L"ItemRanks",&m_ItemRanks[0],sizeof(ItemRank)*(int)m_ItemRanks.size());
}

// Extensions to look for in the PATH directories
static const wchar_t *g_pProgramExtensions[]=
{
	L".EXE",
	L".COM",
	L".BAT",
	L".CMD",
	L".MSC",
	L".CPL",
	L".SCR",
};

bool CSearchManager::AddSearchItem( IShellItem *pItem, const wchar_t *name, int flags, TItemCategory category, SearchRequest &searchRequest )
{
	CAbsolutePidl pidl;
	if (FAILED(SHGetIDListFromObject(pItem,&pidl)))
		return false;

	SearchItem item;
	item.category=CATEGORY_INVALID;
	item.pInfo=g_ItemManager.GetItemInfo(pItem,pidl,CItemManager::INFO_LINK|((flags&COLLECT_METRO)?CItemManager::INFO_METRO:0));
	{
		CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
		item.bMetroLink=item.pInfo->IsMetroLink();
		if ((flags&COLLECT_ONLY_METRO) && !item.bMetroLink)
			return false;
		if (item.bMetroLink && (!searchRequest.bSearchMetroApps || _wcsicmp(item.pInfo->GetAppid(),DESKTOP_APP_ID)==0))
			return false;
		item.category=category;
		if (item.bMetroLink)
		{
			item.name=item.pInfo->GetMetroName();
			StringUpper(item.name);
		}
		else
			item.name=name;
	}
	if (flags&COLLECT_KEYWORDS)
	{
		CComQIPtr<IShellItem2> pItem2=pItem;
		PROPVARIANT val;
		PropVariantInit(&val);
		pItem2->GetProperty(PKEY_Keywords,&val);
		if (val.vt==VT_EMPTY)
			pItem2->GetProperty(PKEY_HighKeywords,&val);
		wchar_t keywords[2048];
		int len=0;
		if (val.vt==VT_BSTR || val.vt==VT_LPWSTR)
		{
			len+=Strcpy(keywords+len,_countof(keywords)-len,L";");
			len+=Strcpy(keywords+len,_countof(keywords)-len,val.pwszVal);
		}
		else if (val.vt==(VT_VECTOR|VT_BSTR) || val.vt==(VT_VECTOR|VT_LPWSTR))
		{
			for (ULONG i=0;i<val.calpwstr.cElems;i++)
			{
				len+=Strcpy(keywords+len,_countof(keywords)-len,L";");
				len+=Strcpy(keywords+len,_countof(keywords)-len,val.calpwstr.pElems[i]);
			}
		}
		if (len>0)
		{
			CharUpper(keywords);
			item.keywords+=keywords;
		}
		PropVariantClear(&val);
	}

	Lock lock(this,LOCK_DATA);
	if (category==CATEGORY_PROGRAM || category==CATEGORY_SETTING || category==CATEGORY_METROSETTING)
	{
		if (searchRequest.requestId<m_LastProgramsRequestId)
			return false;
	}
	else
	{
		if (searchRequest.requestId!=m_LastRequestId)
			return false;
	}
	bool res=true;
	if (category==CATEGORY_PROGRAM || category==CATEGORY_SETTING || category==CATEGORY_METROSETTING)
	{
		std::vector<SearchItem> &items=(category==CATEGORY_PROGRAM)?m_ProgramItems:m_SettingsItems;
		if (category==CATEGORY_SETTING || category==CATEGORY_METROSETTING)
		{
			// remove duplicate settings
			for (std::vector<SearchItem>::const_iterator it=items.begin();it!=items.end();++it)
			{
				if (wcscmp(it->name,item.name)==0 && it->bMetroLink==item.bMetroLink)
				{
					item.category=CATEGORY_INVALID;
					break;
				}
			}
		}

		if (searchRequest.bUseRanks)
		{
			Lock lock(this,LOCK_RANKS);
			Assert(m_bRanksLoaded);
			unsigned int hash;
			CComString pName;
			if (SUCCEEDED(pItem->GetDisplayName(SIGDN_PARENTRELATIVEPARSING,&pName)))
			{
				pName.MakeUpper();
				hash=CalcFNVHash(pName);
			}
			else
				hash=CalcFNVHash(item.name);
			ItemRank rank(hash);
			std::vector<ItemRank>::const_iterator it=std::lower_bound(m_ItemRanks.begin()+1,m_ItemRanks.end(),rank);
			if (it!=m_ItemRanks.end() && it->hash==rank.hash)
				item.rank=it->rank*2;
		}

		items.push_back(item);
		if (item.category==CATEGORY_METROSETTING)
			m_bMetroSettingsFound=true;
	}
	else if (category==CATEGORY_AUTOCOMPLETE)
	{
		item.rank=(flags&COLLECT_IS_FOLDER)?1:0;
		m_AutoCompleteItems.push_back(item);
	}
	if (!(flags&COLLECT_NOREFRESH))
	{
		DWORD time=GetTickCount();
		int dt=(time-searchRequest.searchTime);
		if (dt>1000)
		{
			CMenuContainer::RefreshSearch();
			searchRequest.searchTime=time;
		}
	}
	return res;
}

void CSearchManager::CollectSearchItems( IShellItem *pFolder, int flags, TItemCategory category, SearchRequest &searchRequest )
{
	CComPtr<IEnumShellItems> pEnum;
	pFolder->BindToHandler(NULL,BHID_EnumItems,IID_IEnumShellItems,(void**)&pEnum);
	if (!pEnum) return;

	CComPtr<IShellItem> pChild;
	while (pChild=NULL,pEnum->Next(1,&pChild,NULL)==S_OK)
	{
		if (category==CATEGORY_PROGRAM || category==CATEGORY_SETTING || category==CATEGORY_METROSETTING)
		{
			if (searchRequest.requestId<m_LastProgramsRequestId)
				break;
		}
		else
		{
			if (searchRequest.requestId!=m_LastRequestId)
				break;
		}
		SFGAOF itemFlags;
		if (SUCCEEDED(pChild->GetAttributes(SFGAO_FOLDER|SFGAO_STREAM|SFGAO_LINK|SFGAO_HIDDEN,&itemFlags)))
		{
			if (itemFlags&SFGAO_HIDDEN)
				continue;
			if ((flags&COLLECT_RECURSIVE) && (itemFlags&(SFGAO_FOLDER|SFGAO_STREAM|SFGAO_LINK))==SFGAO_FOLDER)
			{
				// go into subfolders but not archives or links to folders
				CollectSearchItems(pChild,flags,category,searchRequest);
				if (category==CATEGORY_PROGRAM || category==CATEGORY_SETTING || category==CATEGORY_METROSETTING)
				{
					if (searchRequest.requestId<m_LastProgramsRequestId)
						break;
				}
				else
				{
					if (searchRequest.requestId!=m_LastRequestId)
						break;
				}
			}
			if ((flags&COLLECT_FOLDERS) || !(itemFlags&SFGAO_FOLDER))
			{
				CComString pName;
				if (SUCCEEDED(pChild->GetDisplayName((flags&COLLECT_PROGRAMS)?SIGDN_PARENTRELATIVEPARSING:SIGDN_PARENTRELATIVEEDITING,&pName)))
				{
					pName.MakeUpper();
					bool bSkip=false;
					if (flags&COLLECT_PROGRAMS)
					{
						bSkip=true;
						const wchar_t *ext=PathFindExtension(pName);
						for (int i=0;i<_countof(g_pProgramExtensions);i++)
							if (wcscmp(ext,g_pProgramExtensions[i])==0)
							{
								bSkip=false;
								break;
							}
					}
					if (!bSkip)
						AddSearchItem(pChild,pName,flags|((itemFlags&SFGAO_FOLDER)?COLLECT_IS_FOLDER:0),category,searchRequest);
				}
			}
		}
	}
}

bool CSearchManager::SearchScope::ParseSearchConnector( const wchar_t *fname )
{
	CComPtr<IXMLDOMDocument> pDoc;
	if (FAILED(pDoc.CoCreateInstance(L"Msxml2.FreeThreadedDOMDocument")))
		return false;
	pDoc->put_async(VARIANT_FALSE);
	VARIANT_BOOL loaded;
	if (pDoc->load(CComVariant(fname),&loaded)!=S_OK || loaded!=VARIANT_TRUE)
		return false;

	CComPtr<IXMLDOMNode> pRoot;
	HRESULT res=pDoc->selectSingleNode(CComBSTR(L"searchConnectorDescription"),&pRoot);
	if (res!=S_OK) return false;

	CComPtr<IXMLDOMNode> pScope;
	res=pRoot->selectSingleNode(CComBSTR(L"scope"),&pScope);
	if (res==S_OK)
	{
		CComPtr<IXMLDOMNode> pScopeItem;
		pScope->get_firstChild(&pScopeItem);
		while (pScopeItem)
		{
			CComBSTR name;
			pScopeItem->get_nodeName(&name);
			if (_wcsicmp(name,L"scopeItem")==0)
			{
				CComPtr<IXMLDOMNode> pUrl;
				CComBSTR url;
				if (pScopeItem->selectSingleNode(CComBSTR(L"url"),&pUrl)==S_OK && pUrl->get_text(&url)==S_OK)
				{
					if (_wcsnicmp(url,L"file:",5)==0)
						return false; // ignore files
					CComPtr<IXMLDOMNode> pMode, pDepth;
					CComBSTR mode, depth;
					bool bExclude=(pScopeItem->selectSingleNode(CComBSTR(L"mode"),&pMode)==S_OK && pMode->get_text(&mode)==S_OK && _wcsicmp(mode,L"exclude")==0);
					bool bShallow=(pScopeItem->selectSingleNode(CComBSTR(L"depth"),&pDepth)==S_OK && pDepth->get_text(&depth)==S_OK && _wcsicmp(depth,L"shallow")==0);
					CString url2;
					if (bExclude)
						url2.Format(bShallow?L"-=%s":L"-%s",(const wchar_t*)url);
					else
						url2.Format(bShallow?L"=%s":L"%s",(const wchar_t*)url);
					StringUpper(url2);
					roots.push_back(url2);
				}
			}

			CComPtr<IXMLDOMNode> pNext;
			if (pScopeItem->get_nextSibling(&pNext)!=S_OK)
				break;
			pScopeItem=std::move(pNext);
		}
		return true;
	}

	CComPtr<IXMLDOMNode> pTemplate;
	res=pRoot->selectSingleNode(CComBSTR(L"templateInfo"),&pTemplate);
	if (res==S_OK)
	{
		CComPtr<IXMLDOMNode> pType;
		res=pTemplate->selectSingleNode(CComBSTR(L"folderType"),&pType);
		if (res==S_OK)
		{
			CComBSTR type;
			bCommunications=(pType->get_text(&type)==S_OK && _wcsicmp(type,L"{91475FE5-586B-4EBA-8D75-D17434B8CDF6}")==0);
		}
	}

	res=pRoot->selectSingleNode(CComBSTR(L"simpleLocation"),&pScope);
	if (res==S_OK)
	{
		CComPtr<IXMLDOMNode> pUrl;
		res=pScope->selectSingleNode(CComBSTR(L"url"),&pUrl);
		if (res!=S_OK) return false;
		CComBSTR url;
		if (pUrl->get_text(&url)!=S_OK)
			return false;
		if (_wcsnicmp(url,L"file:",5)==0)
			return false; // ignore files
		CString url2(url);
		StringUpper(url2);
		roots.push_back(url2);
		return true;
	}
	return false;
}

void CSearchManager::SearchThread( void )
{
	HANDLE events[2]={m_SearchEvent,m_ExitEvent};
	SearchRequest searchRequest;
	searchRequest.requestId=-1;
	InterlockedIncrement(&m_SearchThreadCount);
	while (1)
	{
		if (searchRequest.requestId==m_LastRequestId)
		{
			Lock lock(this,LOCK_DATA);
			if (searchRequest.requestId==m_LastRequestId)
			{
				m_LastCompletedId=searchRequest.requestId;
				CMenuContainer::RefreshSearch();
			}
		}
		long count=InterlockedDecrement(&m_SearchThreadCount);
//		Trace(L"Search thread count: %d",count);
		if (WaitForMultipleObjects(2,events,FALSE,INFINITE)!=WAIT_OBJECT_0)
			break;
		count=InterlockedIncrement(&m_SearchThreadCount);
//		Trace(L"Search thread count: %d",count);
		{
			Lock lock(this,LOCK_DATA);
			if (m_SearchRequest.requestId!=m_LastRequestId)
				continue;
			searchRequest=m_SearchRequest;
			m_SearchRequest.requestId=0;
			m_IndexedItems.clear();
			if (!searchRequest.autoCompletePath.IsEmpty() && searchRequest.autoCompletePath==m_LastAutoCompletePath)
				continue;
			m_AutoCompleteItems.clear();
			m_AutoCompletePath=searchRequest.autoCompletePath;
			m_LastAutoCompletePath.Empty();
		}
//		Trace(L"Search request: %d",searchRequest.requestId);

		searchRequest.searchTime=GetTickCount();

		// find programs
		if (searchRequest.autoCompletePath.IsEmpty())
		{
			if (searchRequest.searchText.IsEmpty() || wcsncmp(searchRequest.searchText,L"\\\\",2)==0)
				continue;
			Lock lock(this,LOCK_PROGRAMS);
			if (searchRequest.requestId<m_LastProgramsRequestId)
				continue;
			if (m_ProgramItems.empty() && searchRequest.bSearchPrograms)
			{
				if (!m_ProgramItemsOld.empty())
				{
					CMenuContainer::RefreshSearch();
				}
				// collect programs from the start menu
				{
					CComPtr<IShellItem> pFolder;
					if (SUCCEEDED(ShGetKnownFolderItem(FOLDERID_StartMenu,&pFolder)))
						CollectSearchItems(pFolder,COLLECT_RECURSIVE|COLLECT_METRO|COLLECT_NOREFRESH,CATEGORY_PROGRAM,searchRequest);
					if (searchRequest.requestId<m_LastProgramsRequestId)
						continue;
				}

				// common start menu
				if (!searchRequest.bNoCommonFolders)
				{
					CComPtr<IShellItem> pFolder;
					if (SUCCEEDED(ShGetKnownFolderItem(FOLDERID_CommonStartMenu,&pFolder)))
						CollectSearchItems(pFolder,COLLECT_RECURSIVE|COLLECT_METRO|COLLECT_NOREFRESH,CATEGORY_PROGRAM,searchRequest);
					if (searchRequest.requestId<m_LastProgramsRequestId)
						continue;
				}

				// pinned folder
				if (searchRequest.bPinnedFolder)
				{
					wchar_t path[_MAX_PATH];
					Strcpy(path,_countof(path),GetSettingString(L"PinnedItemsPath"));
					DoEnvironmentSubst(path,_MAX_PATH);
					CComPtr<IShellItem> pFolder;
					if (SUCCEEDED(SHCreateItemFromParsingName(path,NULL,IID_IShellItem,(void**)&pFolder)))
						CollectSearchItems(pFolder,COLLECT_METRO|COLLECT_NOREFRESH,CATEGORY_PROGRAM,searchRequest);
					if (searchRequest.requestId<m_LastProgramsRequestId)
						continue;
				}

				// games
				{
					CComPtr<IShellItem> pFolder;
					if (SUCCEEDED(ShGetKnownFolderItem(FOLDERID_Games,&pFolder)))
						CollectSearchItems(pFolder,COLLECT_RECURSIVE|COLLECT_METRO|COLLECT_NOREFRESH,CATEGORY_PROGRAM,searchRequest);
					if (searchRequest.requestId<m_LastProgramsRequestId)
						continue;
				}

				// collect programs in PATH
				if (searchRequest.bSearchPath)
				{
					CString PATH;
					PATH.GetEnvironmentVariable(L"PATH");
					for (const wchar_t *pPath=PATH;*pPath;)
					{
						wchar_t token[_MAX_PATH];
						pPath=GetToken(pPath,token,_countof(token),L";");
						PathRemoveBackslash(token);
						DoEnvironmentSubst(token,_countof(token));
						CComPtr<IShellItem> pFolder;
						if (SUCCEEDED(SHCreateItemFromParsingName(token,NULL,IID_IShellItem,(void**)&pFolder)))
							CollectSearchItems(pFolder,COLLECT_PROGRAMS|COLLECT_NOREFRESH,CATEGORY_PROGRAM,searchRequest);
						if (searchRequest.requestId<m_LastProgramsRequestId)
							continue;
					}
				}
				if (searchRequest.requestId<m_LastProgramsRequestId)
					continue;

				// Metro links
				if (GetWinVersion()>=WIN_VER_WIN8 && searchRequest.bSearchMetroApps)
				{
					std::vector<MetroLink> links;
					GetMetroLinks(links,true);
					for (std::vector<MetroLink>::const_iterator it=links.begin();it!=links.end();++it)
					{
						if (GetWinVersion()<WIN_VER_WIN10)
							AddSearchItem(it->pItem,L"",COLLECT_PROGRAMS|COLLECT_METRO|COLLECT_ONLY_METRO,CATEGORY_PROGRAM,searchRequest);
						else
						{
							CComString pName;
							if (SUCCEEDED(it->pItem->GetDisplayName(SIGDN_NORMALDISPLAY,&pName)))
							AddSearchItem(it->pItem,pName,COLLECT_PROGRAMS|COLLECT_METRO,CATEGORY_PROGRAM,searchRequest);
						}
						if (searchRequest.requestId<m_LastProgramsRequestId)
							continue;
					}
				}
				bool bRefresh=false;
				{
					Lock lock(this,LOCK_DATA);
					m_bProgramsFound=true;
					m_ProgramsHash=CalcItemsHash(m_ProgramItems);
					bRefresh=(m_ProgramsHash!=m_ProgramsHashOld);
				}
				if (bRefresh)
					CMenuContainer::RefreshSearch();
				searchRequest.searchTime=GetTickCount();
			}

			if (m_SettingsItems.empty() && searchRequest.bSearchSettings)
			{
				if (!m_SettingsItemsOld.empty())
				{
					CMenuContainer::RefreshSearch();
				}
				// collect items from the control panel, admin tools, and the god mode
				{
					CComPtr<IShellItem> pFolder;
					if (SUCCEEDED(ShGetKnownFolderItem(FOLDERID_ControlPanelFolder,&pFolder)))
						CollectSearchItems(pFolder,COLLECT_FOLDERS|COLLECT_NOREFRESH,CATEGORY_SETTING,searchRequest);
					if (searchRequest.requestId<m_LastProgramsRequestId)
						continue;
				}
				{
					CComPtr<IShellItem> pFolder;
					if (SUCCEEDED(ShGetKnownFolderItem(FOLDERID_AdminTools,&pFolder)))
						CollectSearchItems(pFolder,COLLECT_RECURSIVE|COLLECT_NOREFRESH,CATEGORY_SETTING,searchRequest);
					if (searchRequest.requestId<m_LastProgramsRequestId)
						continue;
				}
				if (!searchRequest.bNoCommonFolders)
				{
					CComPtr<IShellItem> pFolder;
					if (SUCCEEDED(ShGetKnownFolderItem(FOLDERID_CommonAdminTools,&pFolder)))
						CollectSearchItems(pFolder,COLLECT_RECURSIVE|COLLECT_NOREFRESH,CATEGORY_SETTING,searchRequest);
					if (searchRequest.requestId<m_LastProgramsRequestId)
						continue;
				}
				{
					CComPtr<IShellItem> pFolder;
					if (SUCCEEDED(SHCreateItemFromParsingName(L"shell:::{ED7BA470-8E54-465E-825C-99712043E01C}",NULL,IID_IShellItem,(void**)&pFolder)))
						CollectSearchItems(pFolder,(searchRequest.bSearchKeywords?COLLECT_KEYWORDS:0)|COLLECT_NOREFRESH,CATEGORY_SETTING,searchRequest);
					if (searchRequest.requestId<m_LastProgramsRequestId)
						continue;
				}
				if (searchRequest.bSearchMetroSettings)
				{
					CComPtr<IShellItem> pFolder;
					if (SUCCEEDED(SHCreateItemFromParsingName(L"shell:::{82E749ED-B971-4550-BAF7-06AA2BF7E836}",NULL,IID_IShellItem,(void**)&pFolder)))
						CollectSearchItems(pFolder,(searchRequest.bSearchKeywords?COLLECT_KEYWORDS:0)|COLLECT_NOREFRESH,CATEGORY_METROSETTING,searchRequest);
					if (searchRequest.requestId<m_LastProgramsRequestId)
						continue;
				}
			}
			bool bRefresh=false;
			{
				Lock lock(this,LOCK_DATA);
				m_bSettingsFound=true;
				m_SettingsHash=CalcItemsHash(m_SettingsItems);
				bRefresh=(m_SettingsHash!=m_SettingsHashOld);
			}
			if (bRefresh)
				CMenuContainer::RefreshSearch();
			searchRequest.searchTime=GetTickCount();
		}

		if (searchRequest.requestId!=m_LastRequestId)
			continue;

		if (!searchRequest.autoCompletePath.IsEmpty())
		{
			CComPtr<IShellItem> pFolder;
			wchar_t path[_MAX_PATH];
			Strcpy(path,_countof(path),searchRequest.autoCompletePath);
			DoEnvironmentSubst(path,_countof(path));
			if (SUCCEEDED(SHCreateItemFromParsingName(path,NULL,IID_IShellItem,(void**)&pFolder)))
			{
				SFGAOF itemFlags;
				if (SUCCEEDED(pFolder->GetAttributes(SFGAO_FOLDER|SFGAO_STREAM|SFGAO_LINK,&itemFlags)) && (itemFlags&(SFGAO_FOLDER|SFGAO_STREAM|SFGAO_LINK))==SFGAO_FOLDER)
					CollectSearchItems(pFolder,COLLECT_FOLDERS,CATEGORY_AUTOCOMPLETE,searchRequest);
			}
			{
				Lock lock(this,LOCK_DATA);
				if (searchRequest.requestId==m_LastRequestId)
					m_LastAutoCompletePath=searchRequest.autoCompletePath;
			}
			continue;
		}

		if (searchRequest.requestId!=m_LastRequestId || (!searchRequest.bSearchFiles && !searchRequest.bSearchMetroSettings))
			continue;
		CMenuContainer::RefreshSearch();
		searchRequest.searchTime=GetTickCount();

		CDataSource dataSource;
		CSession session;
		if (SUCCEEDED(dataSource.OpenFromInitializationString(L"provider=Search.CollatorDSO.1;EXTENDED PROPERTIES=\"Application=Windows\"")) && SUCCEEDED(session.Open(dataSource)))
		{
			std::vector<SearchScope> scopeList;

			if (searchRequest.bSearchMetroSettings && !m_bMetroSettingsFound)
			{
				scopeList.push_back(SearchScope());
				SearchScope &scope=*scopeList.rbegin();
				scope.bFiles=true;
				scope.name=FindTranslation(L"Search.CategoryPCSettings",L"Settings");
				scope.categoryHash=CATEGORY_METROSETTING;
				scope.roots.push_back(L"FILE:");
			}
			if (searchRequest.bSearchFiles)
			{
				// prepare roots
				CComPtr<IShellLibrary> pLibrary;
				pLibrary.CoCreateInstance(CLSID_ShellLibrary);
				if (searchRequest.bSearchTypes && pLibrary)
				{
					CComPtr<IShellItem> pLibraries;
					std::vector<CComPtr<IShellItem>> libraries;
					static KNOWNFOLDERID defaultLibraries[]=
					{
						FOLDERID_DocumentsLibrary,
						FOLDERID_MusicLibrary,
						FOLDERID_PicturesLibrary,
						FOLDERID_VideosLibrary,
					};
					{
						for (int i=0;i<_countof(defaultLibraries);i++)
						{
							CComPtr<IShellItem> pItem;
							if (SUCCEEDED(ShGetKnownFolderItem(defaultLibraries[i],&pItem)))
								libraries.push_back(pItem);
						}
					}
					if (SUCCEEDED(ShGetKnownFolderItem(FOLDERID_Libraries,&pLibraries)))
					{
						CComPtr<IEnumShellItems> pEnum;
						pLibraries->BindToHandler(NULL,BHID_EnumItems,IID_IEnumShellItems,(void**)&pEnum);
						if (pEnum)
						{
							CComPtr<IShellItem> pItem;
							while (pItem=NULL,pEnum->Next(1,&pItem,NULL)==S_OK)
							{
								bool bFound=false;
								for (size_t i=0;i<libraries.size();i++)
								{
									int order=1;
									if (libraries[i] && SUCCEEDED(libraries[i]->Compare(pItem,SICHINT_CANONICAL,&order)) && order)
									{
										bFound=true;
										break;
									}
								}
								if (!bFound)
									libraries.push_back(pItem);
							}
						}
					}
					for (std::vector<CComPtr<IShellItem>>::const_iterator it=libraries.begin();it!=libraries.end();++it)
					{
						if (!*it) continue;
						CComString pName;
						if (SUCCEEDED(pLibrary->LoadLibraryFromItem(*it,STGM_READ)) && SUCCEEDED((*it)->GetDisplayName(SIGDN_NORMALDISPLAY,&pName)))
						{
							scopeList.push_back(SearchScope());
							SearchScope &scope=*scopeList.rbegin();
							scope.bFiles=true;
							scope.name=pName;
							LOG_MENU(LOG_SEARCH,L"Category: %s",scope.name);
							SHGetIDListFromObject(*it,&scope.search);
							scope.categoryHash=CATEGORY_FILE;
							CComString pName2;
							if (SUCCEEDED((*it)->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName2)))
								scope.categoryHash|=(CalcFNVHash(pName)&~CATEGORY_MASK);
							CComPtr<IShellItemArray> pArray;
							if (SUCCEEDED(pLibrary->GetFolders(LFF_FORCEFILESYSTEM,IID_IShellItemArray,(void**)&pArray)) && pArray)
							{
								CComPtr<IEnumShellItems> pEnum2;
								if (SUCCEEDED(pArray->EnumItems(&pEnum2)) && pEnum2)
								{
									CComPtr<IShellItem> pFolder;
									while (pFolder=NULL,pEnum2->Next(1,&pFolder,NULL)==S_OK)
									{
										CComString pPath;
										if (SUCCEEDED(pFolder->GetDisplayName(SIGDN_FILESYSPATH,&pPath)) && pPath)
										{
											pPath.MakeUpper();
											for (wchar_t *str=(wchar_t*)(const wchar_t*)pPath;*str;str++)
												if (*str=='\\')
													*str='/';
											CString path;
											path.Format(L"FILE:%s/",(const wchar_t*)pPath);
											path.Replace(L"'",L"''");
											scope.roots.push_back(path);
											LOG_MENU(LOG_SEARCH,L"    Scope: %s",path);
										}
									}
								}
							}
							if (scope.roots.empty())
								scopeList.pop_back();
						}
					}
				}
				else
				{
					// one for files
					scopeList.push_back(SearchScope());
					SearchScope &scope=*scopeList.rbegin();
					scope.bFiles=true;
					scope.name=FindTranslation(L"Search.CategoryFiles",L"Files");
					scope.categoryHash=CATEGORY_FILE;
					scope.categoryHash|=(CalcFNVHash(L"Files")&~CATEGORY_MASK);
					scope.roots.push_back(L"FILE:");
				}
				{
					// search connectors
					CComPtr<IShellItem> pSearches;
					if (SUCCEEDED(ShGetKnownFolderItem(FOLDERID_SavedSearches,&pSearches)))
					{
						CComPtr<IEnumShellItems> pEnum;
						pSearches->BindToHandler(NULL,BHID_EnumItems,IID_IEnumShellItems,(void**)&pEnum);
						PROPERTYKEY keyStartMenu;
						PSGetPropertyKeyFromName(L"System.StartMenu.IncludeInScope",&keyStartMenu);
						if (pEnum)
						{
							CComPtr<IShellItem> pItem;
							while (pItem=NULL,pEnum->Next(1,&pItem,NULL)==S_OK)
							{
								CComString pName;
								pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName);
								LOG_MENU(LOG_SEARCH,L"Search Root: %s",(const wchar_t*)pName);
								if (_wcsicmp(PathFindExtension(pName),L".searchconnector-ms")!=0)
								{
									LOG_MENU(LOG_SEARCH,L"Ignoring: not a search connector");
									continue;
								}
								CComPtr<IPropertyStore> pStore;
								pItem->BindToHandler(NULL,BHID_PropertyStore,IID_IPropertyStore,(void**)&pStore);
								if (!pStore)
								{
									LOG_MENU(LOG_SEARCH,L"Ignoring: no store");
									continue;
								}
								PROPVARIANT val;
								PropVariantInit(&val);
								if (FAILED(pStore->GetValue(keyStartMenu,&val)))
								{
									LOG_MENU(LOG_SEARCH,L"Ignoring: no start menu1");
									continue;
								}
								bool bStartMenu=(val.vt==VT_BOOL && val.boolVal);
								PropVariantClear(&val);
								if (!bStartMenu)
								{
									LOG_MENU(LOG_SEARCH,L"Ignoring: no start menu2");
									continue;
								}
								scopeList.push_back(SearchScope());
								SearchScope &scope=*scopeList.rbegin();
								if (!scope.ParseSearchConnector(pName))
								{
									scopeList.pop_back();
									LOG_MENU(LOG_SEARCH,L"Ignoring: failed to parse searchconnector-ms");
									continue;
								}
								if (GetWinVersion()>=WIN_VER_WIN10)
								{
									// ignore search connector using the WINRT scope - looks like it just duplicates the last search
									bool bWinRT=false;
									for (std::vector<CString>::const_iterator it=scope.roots.begin();it!=scope.roots.end();++it)
									{
										if (wcsncmp(*it,L"WINRT://",8)==0)
										{
											bWinRT=true;
											break;
										}
									}
									if (bWinRT)
									{
										scopeList.pop_back();
										LOG_MENU(LOG_SEARCH,L"Ignoring: uses WINRT scope");
										continue;
									}
								}
								scope.bFiles=false;
								SHGetIDListFromObject(pItem,&scope.search);
								scope.categoryHash=CATEGORY_ITEM;
								scope.categoryHash|=(CalcFNVHash(pName)&~CATEGORY_MASK);
								pName.Clear();
								pItem->GetDisplayName(SIGDN_NORMALDISPLAY,&pName);
								scope.name=pName;
								LOG_MENU(LOG_SEARCH,L"Category: %s",scope.name);
								if (g_LogCategories&LOG_SEARCH)
								{
									for (std::vector<CString>::const_iterator it=scope.roots.begin();it!=scope.roots.end();++it)
										LOG_MENU(LOG_SEARCH,L"    Scope: %s",*it);
								}
							}
						}
					}
				}
				if (searchRequest.bSearchTypes && pLibrary)
				{
					// one for uncategorized files
					scopeList.push_back(SearchScope());
					SearchScope &scope=*scopeList.rbegin();
					scope.bFiles=true;
					scope.name=FindTranslation(L"Search.CategoryFiles",L"Files");
					scope.categoryHash=CATEGORY_FILE;
					scope.roots.push_back(L"FILE:");
				}
			}

			const wchar_t *columns=L"System.ItemUrl, System.ItemType, Path, System.ItemPathDisplay, System.ItemNameDisplay";
			const wchar_t *order=L"System.Search.Rank DESC, System.DateModified DESC, System.ItemNameDisplay ASC";
			const wchar_t *orderComm=L"System.Contact.FileAsName ASC, System.Message.DateReceived DESC, System.Search.Rank DESC";

			CComPtr<IPropertyStore> pStore;
			pStore.CoCreateInstance(CLSID_InMemoryPropertyStore);
			if (!pStore) continue;
			CComPtr<IBindCtx> pBindCtx0;
			CreateBindCtx(0,&pBindCtx0);
			if (!pBindCtx0) continue;
			pBindCtx0->RegisterObjectParam((LPOLESTR)STR_PARSE_WITH_PROPERTIES,pStore);

#ifdef LAUNDER_SEARCH_RESULTS
			CComPtr<ISearchFolderItemFactory> pSearchFactory;
			pSearchFactory.CoCreateInstance(CLSID_SearchFolderItemFactory);
			if (!pSearchFactory) continue;
			CComPtr<IConditionFactory2> pConditionFactory;
			pConditionFactory.CoCreateInstance(CLSID_ConditionFactory);
			if (!pConditionFactory) continue;
#endif			

			CCommand<CAccessor<CDataAccessor>,CRowset> command0;

			{
				CComPtr<ISearchManager> pSearchManager;
				pSearchManager.CoCreateInstance(CLSID_CSearchManager2);
				if (!pSearchManager) continue;
				CComPtr<ISearchCatalogManager> pCatalogManager;
				pSearchManager->GetCatalog(L"SystemIndex",&pCatalogManager);
				if (!pCatalogManager) continue;
				CComPtr<ISearchQueryHelper> pQueryHelper;
				pCatalogManager->GetQueryHelper(&pQueryHelper);
				if (!pQueryHelper) continue;
				pQueryHelper->put_QuerySelectColumns(columns);
				pQueryHelper->put_QuerySorting(order);
				pQueryHelper->put_QueryWhereRestrictions(L"AND NOT System.Shell.SFGAOFlagsStrings = SOME ARRAY['superhidden'] AND System.Shell.OmitFromView!='true'");
				if (!searchRequest.bSearchMetadata)
					pQueryHelper->put_QueryContentProperties(L"System.ItemNameDisplay");
				CComString pQuery;
				pQueryHelper->GenerateSQLFromUserQuery(searchRequest.searchText,&pQuery);
				if (g_LogCategories&LOG_SEARCH_SQL)
				{
					wchar_t *query=const_cast<wchar_t*>((const wchar_t*)pQuery);
					int len=Strlen(query);
					for (int i=0;i<len;i+=1000)
					{
						wchar_t c=0;
						if (i+1000<len)
						{
							c=query[i+1000];
							query[i+1000]=0;
						}
						LOG_MENU(LOG_SEARCH_SQL,i==0?L"Where: %s":L"       %s",query+i);
						if (c)
							query[i+1000]=c;
					}
				}
				HRESULT hr=command0.Open(session,pQuery);
				if (FAILED(hr))
				{
					LOG_MENU(LOG_SEARCH_SQL,L"Where failed: 0x%08X",hr);
					continue;
				}
			}

			unsigned int whereid=0xFFFFFFFF;
			CComQIPtr<IRowsetInfo> pInfo=command0.GetInterface();
			if (pInfo)
			{
				DBPROPID propids[1]={MSIDXSPROP_WHEREID};
				DBPROPIDSET propset={propids,1,DBPROPSET_MSIDXS_ROWSETEXT};
				ULONG csets;
				DBPROPSET *props=NULL;
				if (SUCCEEDED(pInfo->GetProperties(1,&propset,&csets,&props)) && props)
				{
					if (props->rgProperties)
					{
						if (props->rgProperties[0].vValue.vt==VT_UI4)
							whereid=props->rgProperties[0].vValue.uintVal;
						VariantClear(&props->rgProperties[0].vValue);
					}
					if (props->rgProperties)
						CoTaskMemFree(props->rgProperties);
					CoTaskMemFree(props);
				}
			}
			if (whereid==0xFFFFFFFF)
			{
				command0.Close();
				continue;
			}
			for (auto it=scopeList.begin();it!=scopeList.end();++it)
			{
				if (it->roots.empty())
					continue;
				wchar_t query[8192];
				int len=Sprintf(query,_countof(query),L"SELECT TOP %d %s FROM SystemIndex WHERE REUSEWHERE(%u)",MAX_SEARCH_RESULTS,columns,whereid);
				if (it->roots.size()==1 && it->roots[0]==L"FILE:")
				{
					if (it->categoryHash==CATEGORY_METROSETTING)
					{
						len+=Strcpy(query+len,_countof(query)-len,L" AND System.Search.Store='FILE' AND System.FileName NOT LIKE 'Classic_%' AND System.ItemType='.settingcontent-ms'");
						wchar_t userPath[_MAX_PATH]=L"%LOCALAPPDATA%\\Packages\\windows.immersivecontrolpanel_cw5n1h2txyewy\\LocalState\\Indexed\\Settings";
						DoEnvironmentSubst(userPath,_countof(userPath));
						len+=Sprintf(query+len,_countof(query)-len,L" AND SCOPE='%s'",userPath);
					}
					else
					{
						len+=Strcpy(query+len,_countof(query)-len,L" AND System.Search.Store='FILE' AND System.ItemType!='.settingcontent-ms'");
						for (auto it2=scopeList.begin();it2!=it;++it2)
						{
							if (it2->categoryHash==CATEGORY_METROSETTING)
								continue;
							for (std::vector<CString>::iterator it3=it2->roots.begin();it3!=it2->roots.end();++it3)
							{
								if (wcsncmp(*it3,L"FILE:",5)==0)
									len+=Sprintf(query+len,_countof(query)-len,L" AND NOT SCOPE='%s'",*it3);
							}
						}
						if (searchRequest.bSearchPrograms)
						{
							// remove start menu/programs
							{
								CComString pPath;
								if (SUCCEEDED(ShGetKnownFolderPath(FOLDERID_StartMenu,&pPath)))
									len+=Sprintf(query+len,_countof(query)-len,L" AND NOT SCOPE='%s'",(const wchar_t*)pPath);
							}
							{
								CComString pPath;
								if (SUCCEEDED(ShGetKnownFolderPath(FOLDERID_Programs,&pPath)))
									len+=Sprintf(query+len,_countof(query)-len,L" AND NOT SCOPE='%s'",(const wchar_t*)pPath);
							}
						}
						if (searchRequest.bSearchPrograms || searchRequest.bNoCommonFolders)
						{
							// remove common start menu/programs
							{
								CComString pPath;
								if (SUCCEEDED(ShGetKnownFolderPath(FOLDERID_CommonStartMenu,&pPath)))
									len+=Sprintf(query+len,_countof(query)-len,L" AND NOT SCOPE='%s'",(const wchar_t*)pPath);
							}
							{
								CComString pPath;
								if (SUCCEEDED(ShGetKnownFolderPath(FOLDERID_CommonPrograms,&pPath)))
									len+=Sprintf(query+len,_countof(query)-len,L" AND NOT SCOPE='%s'",(const wchar_t*)pPath);
							}
						}
					}
				}
				else
				{
					for (std::vector<CString>::iterator it2=it->roots.begin();it2!=it->roots.end();++it2)
					{
						const wchar_t *scope=*it2;
						if (scope[0]=='-')
						{
							bool bShallow=false;
							scope++;
							if (scope[0]=='=')
							{
								bShallow=true;
								scope++;
							}
							if (bShallow)
								len+=Sprintf(query+len,_countof(query)-len,L" AND NOT DIRECTORY='%s'",scope);
							else
								len+=Sprintf(query+len,_countof(query)-len,L" AND NOT SCOPE='%s'",scope);
						}
					}
					len+=Sprintf(query+len,_countof(query)-len,L" AND (");
					bool bFirst=true;
					for (std::vector<CString>::iterator it2=it->roots.begin();it2!=it->roots.end();++it2)
					{
						const wchar_t *scope=*it2;
						bool bExclude=false, bShallow=false;
						if (scope[0]=='-')
						{
							bExclude=true;
							scope++;
						}
						if (scope[0]=='=')
						{
							bShallow=true;
							scope++;
						}
						if (!bExclude)
						{
							if (bShallow)
								len+=Sprintf(query+len,_countof(query)-len,bFirst?L"DIRECTORY='%s'":L" OR DIRECTORY='%s'",scope);
							else
								len+=Sprintf(query+len,_countof(query)-len,bFirst?L"SCOPE='%s'":L" OR SCOPE='%s'",scope);
							bFirst=false;
						}
					}
					len+=Strcpy(query+len,_countof(query)-len,L")");
				}
				len+=Sprintf(query+len,_countof(query)-len,L" ORDER BY %s",it->bCommunications?orderComm:order);

				if (g_LogCategories&LOG_SEARCH_SQL)
				{
					for (int i=0;i<len;i+=1000)
					{
						wchar_t c=0;
						if (i+1000<len)
						{
							c=query[i+1000];
							query[i+1000]=0;
						}
						LOG_MENU(LOG_SEARCH_SQL,i==0?L"Query: %s":L"       %s",query+i);
						if (c)
							query[i+1000]=c;
					}
				}

				CCommand<CAccessor<CDataAccessor>,CRowset> command;
				HRESULT hr=command.Open(session,query);
				if (FAILED(hr))
				{
					LOG_MENU(LOG_SEARCH_SQL,L"Query failed: 0x%08X",hr);
					continue;
				}

				CComQIPtr<IRowsetInfo> pInfo=command.GetInterface();
				DBPROPID propids[1]={MSIDXSPROP_RESULTS_FOUND};
				DBPROPIDSET propset={propids,1,DBPROPSET_MSIDXS_ROWSETEXT};
				ULONG csets;
				DBPROPSET *props=NULL;
				if (SUCCEEDED(pInfo->GetProperties(1,&propset,&csets,&props)) && props)
				{
					if (props->rgProperties)
					{
						if (props->rgProperties[0].vValue.vt==VT_I4)
							it->resultCount=props->rgProperties[0].vValue.intVal;
						VariantClear(&props->rgProperties[0].vValue);
					}
					if (props->rgProperties)
						CoTaskMemFree(props->rgProperties);
					CoTaskMemFree(props);
				}
				LOG_MENU(LOG_SEARCH_SQL,L"Query results: %d",it->resultCount);
				if (it->resultCount>0)
				{
					SearchCategory *pCategory=NULL;
					{
						Lock lock(this,LOCK_DATA);
						m_IndexedItems.push_back(SearchCategory());
						pCategory=&*m_IndexedItems.rbegin();
						pCategory->name=it->name;
						pCategory->categoryHash=it->categoryHash;
						pCategory->search.Clone(it->search);
					}
					while (command.MoveNext()==S_OK)
					{
						LOG_MENU(LOG_SEARCH_SQL,L"Result: %s, %s, %s, %s, %s",command.itemUrl,command.itemType,command.parsingPath,command.displayPath,command.displayName);
						SearchCategory::Item item;
						if (it->bFiles)
						{
							const wchar_t *path=wcsrchr(command.itemUrl,'/');
							if (!path || _wcsicmp(path+1,command.displayName)!=0)
								item.name=command.displayName;
						}
						if (command.itemUrl[0])
						{
							if (!it->bFiles)
							{
#ifdef LAUNDER_SEARCH_RESULTS
								CComPtr<ICondition> pCondition;
								hr=pConditionFactory->CreateStringLeaf(PKEY_ItemUrl,COP_EQUAL,command.itemUrl,NULL,CONDITION_CREATION_DEFAULT,IID_PPV_ARGS(&pCondition));
								if (pCondition)
								{
									pSearchFactory->SetCondition(pCondition);

									CAbsolutePidl pidl0;
									hr=pSearchFactory->GetIDList(&pidl0);
									CComPtr<IShellFolder> pFolder;
									hr=SHBindToObject(NULL,pidl0,NULL,IID_IShellFolder,(void**)&pFolder);
									if (SUCCEEDED(hr))
									{
										CComPtr<IEnumIDList> pEnum;
										pFolder->EnumObjects(NULL,SHCONTF_FOLDERS|SHCONTF_NONFOLDERS,&pEnum);
										PITEMID_CHILD child;
										if (pEnum && pEnum->Next(1,&child,NULL)==S_OK)
										{
											item.pidl.Attach(ILCombine(pidl0,child));
											ILFree(child);
										}
									}
								}
#else
								PROPVARIANT val;
								val.vt=VT_LPWSTR;
								val.pwszVal=command.itemType;
								pStore->SetValue(PKEY_ItemType,val);
								val.pwszVal=command.parsingPath;
								pStore->SetValue(PKEY_ParsingPath,val);
								val.pwszVal=command.displayPath;
								pStore->SetValue(PKEY_ItemPathDisplay,val);
								val.pwszVal=command.displayName;
								pStore->SetValue(PKEY_ItemNameDisplay,val);
								item.name=command.displayName;
								hr=SHParseDisplayName(command.itemUrl,pBindCtx0,&item.pidl,0,NULL);
#endif
							}
							else
							{
								hr=SHParseDisplayName(command.itemUrl,NULL,&item.pidl,0,NULL);
								if (FAILED(hr) && _wcsnicmp(command.itemUrl,L"file:",5)==0)
								{
									for (wchar_t *str=command.itemUrl;*str;++str)
									{
										if (*str=='/')
											*str='\\';
									}
									hr=SHParseDisplayName(command.itemUrl+5,NULL,&item.pidl,0,NULL);
								}
							}
							if (SUCCEEDED(hr))
							{
								Lock lock(this,LOCK_DATA);
								if (searchRequest.requestId!=m_LastRequestId)
									break;
								pCategory->items.push_back(item);
								DWORD time=GetTickCount();
								int dt=(time-searchRequest.searchTime);
								if (dt>1000)
								{
									CMenuContainer::RefreshSearch();
									searchRequest.searchTime=time;
								}
							}
						}
					}
				}
				command.Close();
				if (searchRequest.requestId!=m_LastRequestId)
					break;
			}
			command0.Close();
		}
	}
}

DWORD CALLBACK CSearchManager::StaticSearchThread( void *param )
{
//	SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_IDLE);
	OleInitialize(NULL);
	((CSearchManager*)param)->SearchThread();
	OleUninitialize();
	return 0;
}

void CSearchManager::GetSearchResults( SearchResults &results )
{
	results.programs.clear();
	results.settings.clear();
	results.metrosettings.clear();
	results.indexed.clear();
	results.autocomplete.clear();
	results.autoCompletePath.Empty();
	Lock lock(this,LOCK_DATA);
	results.autoCompletePath=m_AutoCompletePath;
	bool bSearchSubWord=GetSettingBool(L"SearchSubWord");
	if (m_AutoCompletePath.IsEmpty())
	{
		{
			std::vector<SearchItem> &programs=m_bProgramsFound?m_ProgramItems:m_ProgramItemsOld;
			std::sort(programs.begin(),programs.end());
			std::vector<const SearchItem*> foundItems;
			for (std::vector<SearchItem>::const_iterator it=programs.begin();it!=programs.end();++it)
			{
				if (it->category==CATEGORY_PROGRAM && it->MatchText(m_SearchText,bSearchSubWord))
				{
					bool bDuplicate=false;
					bool bAppResolved=false;
					for (std::vector<const SearchItem*>::const_iterator it2=foundItems.begin();it2!=foundItems.end();++it2)
					{
						if (wcscmp(it->name,(*it2)->name)==0 && it->bMetroLink==(*it2)->bMetroLink)
						{
							if (!bAppResolved)
							{
								bAppResolved=true;
								g_ItemManager.UpdateItemInfo(it->pInfo,CItemManager::INFO_LINK_APPID);
							}
							g_ItemManager.UpdateItemInfo((*it2)->pInfo,CItemManager::INFO_LINK_APPID);
							CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
							if (it->pInfo->GetAppid()==(*it2)->pInfo->GetAppid())
							{
								bDuplicate=true;
								break;
							}
						}
					}
					if (!bDuplicate)
					{
						results.programs.push_back(it->pInfo);
						foundItems.push_back(&*it);
					}
				}
			}
		}

		{
			std::vector<SearchItem> &settings=m_bSettingsFound?m_SettingsItems:m_SettingsItemsOld;
			for (std::vector<SearchItem>::iterator it=settings.begin();it!=settings.end();++it)
			{
				int match=(it->category==CATEGORY_SETTING || it->category==CATEGORY_METROSETTING)?it->MatchText(m_SearchText,bSearchSubWord):0;
				it->rank=(it->rank&0xFFFFFFFE)|(match>>1);
			}
			std::sort(settings.begin(),settings.end());
			for (std::vector<SearchItem>::const_iterator it=settings.begin();it!=settings.end();++it)
			{
				if (it->MatchText(m_SearchText, bSearchSubWord))
				{
					if (it->category==CATEGORY_SETTING)
						results.settings.push_back(it->pInfo);
					if (it->category==CATEGORY_METROSETTING)
						results.metrosettings.push_back(it->pInfo);
				}
			}
		}

		results.indexed=m_IndexedItems;
	}
	else
	{
		std::sort(m_AutoCompleteItems.begin(),m_AutoCompleteItems.end());
		Assert(_wcsnicmp(m_SearchText,m_AutoCompletePath,m_AutoCompletePath.GetLength())==0);
		CString filter=m_SearchText.Mid(m_AutoCompletePath.GetLength()+1);
		if (!filter.IsEmpty())
			filter+='*';
		for (std::vector<SearchItem>::const_iterator it=m_AutoCompleteItems.begin();it!=m_AutoCompleteItems.end();++it)
		{
			if (filter.IsEmpty() || PathMatchSpec(it->name,filter))
				results.autocomplete.push_back(it->pInfo);
		}
	}
	results.bResults=(!results.programs.empty() || !results.settings.empty() || !results.metrosettings.empty() || !results.indexed.empty() || !results.autocomplete.empty());
	results.bSearching=(m_LastCompletedId!=m_LastRequestId);
}

bool CSearchManager::SearchItem::MatchTextInt( const wchar_t *search, const CString &text, bool bSearchSubWord )
{
	if (text.IsEmpty()) return false;
	if (bSearchSubWord)
	{
		// split search into tokens and see if all are found
		for (const wchar_t *pSearch=search;*pSearch;)
		{
			wchar_t token[100];
			pSearch=GetToken(pSearch,token,_countof(token),L" ");
			if (FindNLSStringEx(LOCALE_NAME_USER_DEFAULT,FIND_FROMSTART|LINGUISTIC_IGNORECASE|LINGUISTIC_IGNOREDIACRITIC,text,-1,token,-1,NULL,NULL,NULL,0)<0)
				return false;
		}
	}
	else
	{
		// split search into tokens, then see if any of the words start with those tokens
		for (const wchar_t *pSearch=search;*pSearch;)
		{
			wchar_t token[100];
			pSearch=GetToken(pSearch,token,_countof(token),L" ");
			bool bFound=false;
			int len=Strlen(token);
			for (const wchar_t *pName=text;*pName;)
			{
				while (*pName && wcschr(L" \t.,$&[]{}();|",*pName))
					pName++;
				if (FindNLSStringEx(LOCALE_NAME_USER_DEFAULT,FIND_STARTSWITH|LINGUISTIC_IGNORECASE|LINGUISTIC_IGNOREDIACRITIC,pName,-1,token,len,NULL,NULL,NULL,0)>=0)
				{
					bFound=true;
					break;
				}
				while (*pName && !wcschr(L" \t.,$&[]{}();|",*pName))
					pName++;
			}
			if (!bFound)
				return false;
		}
	}
	return true;
}

void CSearchManager::LaunchExternalSearch( PIDLIST_ABSOLUTE root, unsigned int categoryHash, const CString &searchText )
{
	Assert(GetCurrentThreadId()==m_MainThreadId);
	if (searchText.IsEmpty()) return;

	CComPtr<IConditionFactory2> pConditionFactory;
	pConditionFactory.CoCreateInstance(CLSID_ConditionFactory);
	if (!pConditionFactory) return;
	
	CComPtr<ISearchFolderItemFactory> pSearchFactory;
	pSearchFactory.CoCreateInstance(CLSID_SearchFolderItemFactory);
	if (!pSearchFactory) return;

	std::vector<CComPtr<ICondition>> conditions;
	CComPtr<ICondition> pCondition;

	if (!root)
	{
		if (categoryHash==CATEGORY_FILE)
		{
			// uncategorized files
			PROPERTYKEY keyGroup;
			if (FAILED(PSGetPropertyKeyFromName(L"System.StartMenu.Group",&keyGroup)))
				return;
			if (FAILED(pConditionFactory->CreateStringLeaf(keyGroup,COP_EQUAL,L"files",NULL,CONDITION_CREATION_DEFAULT,IID_PPV_ARGS(&pCondition))))
				return;
			conditions.push_back(pCondition);
		}
		else if ((categoryHash&CATEGORY_MASK)==CATEGORY_FILE)
		{
			// all files
			if (FAILED(pConditionFactory->CreateStringLeaf(PKEY_Search_Store,COP_EQUAL,L"file",NULL,CONDITION_CREATION_DEFAULT,IID_PPV_ARGS(&pCondition))))
				return;
			conditions.push_back(pCondition);
		}
		pCondition=NULL;
	}

	PROPERTYKEY keyString;
	if (!GetSettingBool(L"SearchContents"))
		keyString=PKEY_ItemNameDisplay;
	else if (FAILED(PSGetPropertyKeyFromName(L"System.Generic.String",&keyString)))
		return;

	// create condition for each word
	const wchar_t *str=searchText;
	const wchar_t *str0=str;
	while (*str)
	{
		while (*str==' ')
			str++;
		CONDITION_OPERATION oper=*str=='"'?COP_WORD_EQUAL:COP_WORD_STARTSWITH;
		wchar_t token[100];
		str=GetToken(str,token,_countof(token),L" ");
		if (SUCCEEDED(pConditionFactory->CreateStringLeaf(keyString,oper,token,NULL,CONDITION_CREATION_DEFAULT,IID_PPV_ARGS(&pCondition))))
		{
			conditions.push_back(pCondition);
			pCondition=NULL;
		}
	}

	if (root)
	{
		PCIDLIST_ABSOLUTE pidls[1]={root};
		CComPtr<IShellItemArray> pArray;
		if (SUCCEEDED(SHCreateShellItemArrayFromIDLists(1,pidls,&pArray)))
			pSearchFactory->SetScope(pArray);
	}
	else if (categoryHash==CATEGORY_FILE)
	{
		CAbsolutePidl root2;
		SHParseDisplayName(L"::{DAF95313-E44D-46AF-BE1B-CBACEA2C3065}",NULL,&root2,0,NULL);
		PCIDLIST_ABSOLUTE pidls[1]={root2};
		CComPtr<IShellItemArray> pArray;
		if (SUCCEEDED(SHCreateShellItemArrayFromIDLists(1,pidls,&pArray)))
			pSearchFactory->SetScope(pArray);
	}

	if (conditions.size()==1)
		pSearchFactory->SetCondition(conditions[0]);
	else
	{
		CComPtr<ICondition> pConditionAnd;
		if (SUCCEEDED(pConditionFactory->CreateCompoundFromArray(CT_AND_CONDITION,&conditions[0].p,(ULONG)conditions.size(),CONDITION_CREATION_DEFAULT,IID_PPV_ARGS(&pConditionAnd))))
			pSearchFactory->SetCondition(pConditionAnd);
	}

	CComPtr<IShellItem> pSearchItem;
	if (FAILED(pSearchFactory->GetShellItem(IID_IShellItem,(void**)&pSearchItem)))
		return;

	SHELLEXECUTEINFO execute={sizeof(execute)};
	execute.lpVerb=L"open";
	execute.nShow=SW_SHOWNORMAL;
	execute.fMask=SEE_MASK_IDLIST;
	CAbsolutePidl pidl;
	if (SUCCEEDED(SHGetIDListFromObject(pSearchItem,&pidl)))
	{
/*
		// Unsucessful attempt to use some undocumented APIs to highlight the search term in Explorer.
		// The seach box is populated with the text but the items are still not higlighted

		const GUID IID_IFilterCondition={0xFCA2857D,0x1760,0x4AD3,{0x8C,0x63,0xC9,0xB6,0x02,0xFC,0xBA,0xEA}};

		interface IFilterCondition: public IPersistStream
		{
		};

		const GUID IID_IShellFolder3={0x711B2CFD,0x93D1,0x422B,{0xBD,0xF4,0x69,0xBE,0x92,0x3F,0x24,0x49}};

		typedef DWORD FILTERIDLISTTYPE;

		interface IShellFolder3: public IShellFolder2
		{
			STDMETHOD(CreateFilteredIDList)(IFilterCondition *, FILTERIDLISTTYPE, IPropertyStore *, PITEMID_CHILD *)=0;
		//	STDMETHOD(GetFilteredIDListType)(PCITEMID_CHILD, FILTERIDLISTTYPE *);
		//	STDMETHOD(ModifyFilteredIDList)(PCITEMID_CHILD, IFilterCondition *, PITEMID_CHILD *);
		//	STDMETHOD(ReparentFilteredIDList)(PCIDLIST_RELATIVE, PIDLIST_RELATIVE *);
		//	STDMETHOD(CreateStackedIDList)(PROPERTYKEY const &, PIDLIST_ABSOLUTE *);
		//	STDMETHOD(GetStackedKey)(PROPERTYKEY *);
		//	STDMETHOD(EnumObjectsEx)(HWND, IBindCtx *, ULONG, IItemFilter *, IEnumIDList **);
		//	STDMETHOD(GetConditions)(PROPERTYKEY const &, IQueryUnit *, REFIID, PVOID *);
		//	STDMETHOD(GetAutoListFlags)(ULONG *);
		};

		typedef HRESULT (WINAPI*tSHCreateFilter)( PCWSTR Name, PCWSTR InFolder, PROPERTYKEY const &PropertyKey, INT Type, ICondition *Condition, REFIID riid, PVOID *ppv );

		tSHCreateFilter SHCreateFilter=(tSHCreateFilter)GetProcAddress(GetModuleHandle(L"shell32.dll"),MAKEINTRESOURCEA(818));
		if (SHCreateFilter)
		{
			PROPERTYKEY wordWheel=PKEY_Kind;
			wordWheel.pid=5;
			CComPtr<IFilterCondition> pFilter;
			HRESULT hr=SHCreateFilter(searchText,NULL,wordWheel,7,pCondition,IID_IFilterCondition,(void**)&pFilter);
			if (pFilter)
			{
				CComPtr<IShellFolder3> pShellFolder3;
				SHBindToObject(NULL,pidl,NULL,IID_IShellFolder3,(void**)&pShellFolder3);
				if (pShellFolder3)
				{
					PITEMID_CHILD pChild;
					hr=pShellFolder3->CreateFilteredIDList(pFilter,2,NULL,&pChild);
					if (SUCCEEDED(hr))
					{
						pSearchItem=NULL;
						if (SUCCEEDED(SHCreateItemWithParent(NULL,pShellFolder3,pChild,IID_IShellItem,(void**)&pSearchItem)))
						{
							CAbsolutePidl pidl2;
							if (SUCCEEDED(SHGetIDListFromObject(pSearchItem,&pidl2)))
								pidl.Swap(pidl2);
						}
						ILFree(pChild);
					}
				}
			}
		}*/
		execute.lpIDList=pidl;
		ShellExecuteEx(&execute);
	}
}

void CSearchManager::LaunchInternetSearch( const CString &searchText )
{
	Assert(GetCurrentThreadId()==m_MainThreadId);
	if (searchText.IsEmpty()) return;

	if (IsEdgeDefaultBrowser())
	{
		wchar_t search[256];
		Sprintf(search,_countof(search),L"? %s",searchText);
		ShellExecute(NULL,NULL,L"shell:appsfolder\\Microsoft.MicrosoftEdge_8wekyb3d8bbwe!MicrosoftEdge",search,NULL,SW_SHOWNORMAL);
	}
	else
	{
		wchar_t path[_MAX_PATH];
		DWORD size=_countof(path);
		if (SUCCEEDED(AssocQueryString(0,ASSOCSTR_EXECUTABLE,L"http",L"open",path,&size)))
		{
			wchar_t search[256];
			Sprintf(search,_countof(search),L"\"? %s\"",searchText);
			ShellExecute(NULL,NULL,path,search,NULL,SW_SHOWNORMAL);
		}
	}
}

bool HasSearchService( void )
{
	bool bWSearch=false;
	SC_HANDLE hManager=OpenSCManager(NULL,NULL,SC_MANAGER_CONNECT);
	if (hManager)
	{
		SC_HANDLE hService=OpenService(hManager,L"WSearch",SERVICE_QUERY_STATUS);
		if(hService)
		{
			// service is installed
			SERVICE_STATUS status;
			if(QueryServiceStatus(hService,&status))
			{
				bWSearch=(status.dwCurrentState==SERVICE_RUNNING);
			}
			CloseServiceHandle(hService);
		}
		CloseServiceHandle(hManager);
	}
	return bWSearch;
}
