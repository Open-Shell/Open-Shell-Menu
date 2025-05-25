// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include "JumpLists.h"
#include "ItemManager.h"
#include "ResourceHelper.h"
#include "Translations.h"
#include "FNVHash.h"
#include "LogManager.h"
#include <propkey.h>
#include <StrSafe.h>

static const CLSID CLSID_AutomaticDestinationList={0xf0ae1542, 0xf497, 0x484b, {0xa1, 0x75, 0xa2, 0x0d, 0xb0, 0x91, 0x44, 0xba}};

struct APPDESTCATEGORY
{
	int type;
	union
	{
		wchar_t *name;
		int subType;
	};
	int count;
	int pad[10]; // just in case
};

static const GUID IID_IDestinationList={0x03f1eed2, 0x8676, 0x430b, {0xab, 0xe1, 0x76, 0x5c, 0x1d, 0x8f, 0xe1, 0x47}};
static const GUID IID_IDestinationList10a={0xfebd543d, 0x1f7b, 0x4b38, {0x94, 0x0b, 0x59, 0x33, 0xbd, 0x2c, 0xb2, 0x1b}}; // 10240
static const GUID IID_IDestinationList10b={0x507101cd, 0xf6ad, 0x46c8, {0x8e, 0x20, 0xee, 0xb9, 0xe6, 0xba, 0xc4, 0x7f}}; // 10547

interface IDestinationList: public IUnknown
{
public:
	STDMETHOD(SetMinItems)();
	virtual HRESULT STDMETHODCALLTYPE SetApplicationID( LPCWSTR appUserModelId ) = 0;
	STDMETHOD(GetSlotCount)();
	virtual HRESULT STDMETHODCALLTYPE GetCategoryCount( UINT *pCount ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetCategory( UINT index, int getCatFlags, APPDESTCATEGORY *pCategory ) = 0;
	STDMETHOD(DeleteCategory)();
	virtual HRESULT STDMETHODCALLTYPE EnumerateCategoryDestinations( UINT index, REFIID riid, void **ppvObject ) = 0;
	STDMETHOD(RemoveDestination)( IUnknown *pItem );
	STDMETHOD(ResolveDestination)();
};

static const GUID IID_IAutomaticDestinationList={0xbc10dce3, 0x62f2, 0x4bc6, {0xaf, 0x37, 0xdb, 0x46, 0xed, 0x78, 0x73, 0xc4}};
static const GUID IID_IAutomaticDestinationList10b={0xe9c5ef8d, 0xfd41, 0x4f72, {0xba, 0x87, 0xeb, 0x03 ,0xba, 0xd5, 0x81, 0x7c}}; // 10547

interface IAutomaticDestinationList: public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE Initialize( LPCWSTR appUserModelId, LPCWSTR lnkPath, LPCWSTR ) = 0;
	virtual HRESULT STDMETHODCALLTYPE HasList( BOOL *pHasList ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetList( int listType, unsigned int maxCount, REFIID riid, void **ppvObject ) = 0;
	STDMETHOD(AddUsagePoint)();
	virtual HRESULT STDMETHODCALLTYPE PinItem( IUnknown *pItem, int pinIndex ) = 0; // -1 - pin, -2 - unpin
	STDMETHOD(IsPinned)();
	virtual HRESULT STDMETHODCALLTYPE RemoveDestination( IUnknown *pItem ) = 0;
	STDMETHOD(SetUsageData)();
	STDMETHOD(GetUsageData)();
	STDMETHOD(ResolveDestination)();
	virtual HRESULT STDMETHODCALLTYPE ClearList( int listType ) = 0;
};

interface IAutomaticDestinationList10b: public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE Initialize( LPCWSTR appUserModelId, LPCWSTR lnkPath, LPCWSTR ) = 0;
	virtual HRESULT STDMETHODCALLTYPE HasList( BOOL *pHasList ) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetList( int listType, unsigned int maxCount, unsigned int flags, REFIID riid, void **ppvObject ) = 0;
	STDMETHOD(AddUsagePoint)();
	virtual HRESULT STDMETHODCALLTYPE PinItem( IUnknown *pItem, int pinIndex ) = 0; // -1 - pin, -2 - unpin
	STDMETHOD(IsPinned)();
	virtual HRESULT STDMETHODCALLTYPE RemoveDestination( IUnknown *pItem ) = 0;
	STDMETHOD(SetUsageData)();
	STDMETHOD(GetUsageData)();
	STDMETHOD(ResolveDestination)();
	virtual HRESULT STDMETHODCALLTYPE ClearList( int listType ) = 0;
};

class CAutomaticList
{
public:
	CAutomaticList( const wchar_t *appid );
	bool HasList( void );
	CComPtr<IObjectCollection> GetList( int listType, unsigned int maxCount );
	void PinItem( IUnknown *pItem, int pinIndex );
	bool RemoveDestination( IUnknown *pItem );

private:
	CComPtr<IAutomaticDestinationList> m_pAutoList;
	CComPtr<IAutomaticDestinationList10b> m_pAutoList10b;
};

CAutomaticList::CAutomaticList( const wchar_t *appid )
{
	CComPtr<IUnknown> pAutoListUnk;
	if (SUCCEEDED(pAutoListUnk.CoCreateInstance(CLSID_AutomaticDestinationList)))
	{
		pAutoListUnk->QueryInterface(IID_IAutomaticDestinationList,(void**)&m_pAutoList);
		if (m_pAutoList)
		{
			if (FAILED(m_pAutoList->Initialize(appid,NULL,NULL)))
				m_pAutoList=NULL;
		}
		else if (GetWinVersion()>=_WIN32_WINNT_WIN10)
		{
			pAutoListUnk->QueryInterface(IID_IAutomaticDestinationList10b,(void**)&m_pAutoList10b);
			if (m_pAutoList10b)
			{
				if (FAILED(m_pAutoList10b->Initialize(appid,NULL,NULL)))
					m_pAutoList10b=NULL;
			}
		}
	}
}

bool CAutomaticList::HasList( void )
{
	BOOL hasList;
	if (m_pAutoList)
	{
		if (FAILED(m_pAutoList->HasList(&hasList)) || !hasList)
			return false;
	}
	else if (m_pAutoList10b)
	{
		if (FAILED(m_pAutoList10b->HasList(&hasList)) || !hasList)
			return false;
	}
	else
		return false;
	CComPtr<IObjectCollection> pCollection;
	UINT count;
	pCollection=GetList(1,1);
	if (pCollection && SUCCEEDED(pCollection->GetCount(&count)) && count>0)
		return true;
	pCollection=GetList(0,1);
	if (pCollection && SUCCEEDED(pCollection->GetCount(&count)) && count>0)
		return true;
	return false;
}

CComPtr<IObjectCollection> CAutomaticList::GetList( int listType, unsigned int maxCount )
{
	CComPtr<IObjectCollection> pCollection;
	if (m_pAutoList)
		m_pAutoList->GetList(listType,maxCount,IID_IObjectCollection,(void**)&pCollection);
	else if (m_pAutoList10b)
		m_pAutoList10b->GetList(listType,maxCount,1,IID_IObjectCollection,(void**)&pCollection);
	return pCollection;
}

void CAutomaticList::PinItem( IUnknown *pItem, int pinIndex )
{
	if (m_pAutoList)
		m_pAutoList->PinItem(pItem,pinIndex);
	else if (m_pAutoList10b)
		m_pAutoList10b->PinItem(pItem,pinIndex);
}

bool CAutomaticList::RemoveDestination( IUnknown *pItem )
{
	if (m_pAutoList)
		return SUCCEEDED(m_pAutoList->RemoveDestination(pItem));
	else if (m_pAutoList10b)
		return SUCCEEDED(m_pAutoList10b->RemoveDestination(pItem));
	return false;
}

static CComPtr<IDestinationList> GetCustomList( const wchar_t *appid )
{
	CComPtr<IUnknown> pCustomListUnk;
	if (SUCCEEDED(pCustomListUnk.CoCreateInstance(CLSID_DestinationList)))
	{
		CComPtr<IDestinationList> pCustomList;
		if (GetWinVersion()<_WIN32_WINNT_WIN10)
			pCustomListUnk->QueryInterface(IID_IDestinationList,(void**)&pCustomList);
		else
		{
			if (FAILED(pCustomListUnk->QueryInterface(IID_IDestinationList10a,(void**)&pCustomList)))
				pCustomListUnk->QueryInterface(IID_IDestinationList10b,(void**)&pCustomList);
		}
		if (pCustomList && SUCCEEDED(pCustomList->SetApplicationID(appid)))
			return pCustomList;
	}
	return CComPtr<IDestinationList>();
}

// Returns true if the given app has a non-empty jumplist
bool HasJumplist( const wchar_t *appid )
{
	Assert(GetWinVersion()>=_WIN32_WINNT_WIN7);

	CComPtr<IDestinationList> pCustomList=GetCustomList(appid);
	if (pCustomList)
	{
		UINT count;
		if (SUCCEEDED(pCustomList->GetCategoryCount(&count)) && count>0)
		{
			// skip Settings app (it reports one category with unsupported type, thus jump-list will be empty)
			if (wcscmp(appid, L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel") == 0)
				return false;

			return true;
		}
	}

	if (CAutomaticList(appid).HasList())
		return true;

	return false;
}

static unsigned int CalcLinkHash( IShellLink *pLink )
{
	CAbsolutePidl pidl;
	if (FAILED(pLink->GetIDList(&pidl)))
		return 0;

	unsigned int hash=FNV_HASH0;
	CComString pName;
	if (SUCCEEDED(SHGetNameFromIDList(pidl,SIGDN_DESKTOPABSOLUTEPARSING,&pName)))
	{
		pName.MakeUpper();
		hash=CalcFNVHash(pName);
	}
	CComQIPtr<IPropertyStore> pStore=pLink;
	if (pStore)
		hash=CalcFNVHash(GetPropertyStoreString(pStore,PKEY_Link_Arguments),hash);
	return hash;
}

static void AddJumpItem( CJumpGroup &group, IUnknown *pUnknown, std::vector<CComPtr<IShellItem>> &ignoreItems, std::vector<unsigned int> &ignoreLinks )
{
	CJumpItem item;
	item.type=CJumpItem::TYPE_UNKNOWN;
	item.pItem=pUnknown;
	item.hash=0;
	item.bHidden=false;
	item.bHasArguments=false;
	CComQIPtr<IShellItem> pItem=pUnknown;
	if (pItem)
	{
		for (std::vector<CComPtr<IShellItem>>::const_iterator it=ignoreItems.begin();it!=ignoreItems.end();++it)
		{
			int order;
			if (SUCCEEDED(pItem->Compare(*it,SICHINT_CANONICAL|SICHINT_TEST_FILESYSPATH_IF_NOT_EQUAL,&order)) && order==0)
				return;
		}
		item.type=CJumpItem::TYPE_ITEM;
		CComString pName;
		if (FAILED(pItem->GetDisplayName(SIGDN_NORMALDISPLAY,&pName)))
			return;
		item.name=pName;
		pName.Clear();
		if (SUCCEEDED(pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pName)))
		{
			LOG_MENU(LOG_OPEN,L"Jumplist Item Path: %s",(const wchar_t*)pName);
			pName.MakeUpper();
			item.hash=CalcFNVHash(pName);
		}
		LOG_MENU(LOG_OPEN,L"Jumplist Item Name: %s",item.name);
		group.items.push_back(item);
		return;
	}

	CComQIPtr<IShellLink> pLink=pUnknown;
	if (pLink)
	{
		unsigned int hash=CalcLinkHash(pLink);
		for (std::vector<unsigned int>::const_iterator it=ignoreLinks.begin();it!=ignoreLinks.end();++it)
		{
			if (hash==*it)
				return;
		}
		item.type=CJumpItem::TYPE_LINK;
		CComQIPtr<IPropertyStore> pStore(pLink);
		if (pStore)
		{
			PROPVARIANT val;
			PropVariantInit(&val);
			if (group.type==CJumpGroup::TYPE_TASKS && SUCCEEDED(pStore->GetValue(PKEY_AppUserModel_IsDestListSeparator,&val)) && val.vt==VT_BOOL && val.boolVal)
			{
				item.type=CJumpItem::TYPE_SEPARATOR;
				PropVariantClear(&val);
			}
			else
			{
				CString str=GetPropertyStoreString(pStore,PKEY_Title);
				if (!str.IsEmpty())
				{
					wchar_t name[256];
					SHLoadIndirectString(str,name,_countof(name),NULL);
					item.name=name;
				}
			}
		}
		CAbsolutePidl pidl;
		if (SUCCEEDED(pLink->GetIDList(&pidl)))
		{
			CComString pName;
			if (item.name.IsEmpty())
			{
				if (SUCCEEDED(SHGetNameFromIDList(pidl,SIGDN_NORMALDISPLAY,&pName)))
				{
					item.name=pName;
				}
			}
			pName.Clear();
			if (SUCCEEDED(SHGetNameFromIDList(pidl,SIGDN_DESKTOPABSOLUTEPARSING,&pName)))
			{
				LOG_MENU(LOG_OPEN,L"Jumplist Link Path: %s",(const wchar_t*)pName);
				pName.MakeUpper();
				item.hash=CalcFNVHash(pName);
			}
			CComQIPtr<IPropertyStore> pStore(pLink);
			if (pStore)
			{
				CString args=GetPropertyStoreString(pStore,PKEY_Link_Arguments);
				if (!args.IsEmpty())
				{
					LOG_MENU(LOG_OPEN,L"Jumplist Link Args: %s",args);
					item.hash=CalcFNVHash(args,item.hash);
					item.bHasArguments=true;
				}
			}
		}
		LOG_MENU(LOG_OPEN,L"Jumplist Link Name: %s",item.name);
#ifdef _DEBUG
		LogPropertyStore(LOG_OPEN, pStore);
#endif
		if (!item.name.IsEmpty())
			group.items.push_back(item);
		return;
	}
}

static void AddJumpCollection( CJumpGroup &group, IObjectCollection *pCollection, std::vector<CComPtr<IShellItem>> &ignoreItems, std::vector<unsigned int> &ignoreLinks )
{
	UINT count;
	if (SUCCEEDED(pCollection->GetCount(&count)))
	{
		for (UINT i=0;i<count;i++)
		{
			CComPtr<IUnknown> pUnknown;
			if (SUCCEEDED(pCollection->GetAt(i,IID_IUnknown,(void**)&pUnknown)) && pUnknown)
				AddJumpItem(group,pUnknown,ignoreItems,ignoreLinks);
		}
	}
}

// Returns the jumplist for the given shortcut
bool GetJumplist( const wchar_t *appid, CJumpList &list, int maxCount, int maxHeight, int sepHeight, int itemHeight )
{
	Assert(GetWinVersion()>=_WIN32_WINNT_WIN7);
	list.Clear();

	UINT categoryCount=0;
	CComPtr<IDestinationList> pCustomList=GetCustomList(appid);
	if (pCustomList)
	{
		if (FAILED(pCustomList->GetCategoryCount(&categoryCount)))
			categoryCount=0;
	}

	list.groups.reserve(categoryCount+2);

	std::vector<CComPtr<IShellItem>> ignoreItems;
	std::vector<unsigned int> ignoreLinks;
	CAutomaticList autoList(appid);
	{
		// add pinned
		CComPtr<IObjectCollection> pPinnedList=autoList.GetList(0,maxCount);
		if (pPinnedList)
		{
			Assert(list.groups.empty());
			list.groups.resize(list.groups.size()+1);
			CJumpGroup &group=*list.groups.rbegin();
			group.type=CJumpGroup::TYPE_PINNED;
			group.name=FindTranslation(L"JumpList.Pinned",L"Pinned");
			AddJumpCollection(group,pPinnedList,ignoreItems,ignoreLinks);
			for (std::vector<CJumpItem>::const_iterator it=group.items.begin();it!=group.items.end();++it)
			{
				CComQIPtr<IShellItem> pShellItem(it->pItem);
				if (pShellItem)
					ignoreItems.push_back(pShellItem);
				else
				{
					CComQIPtr<IShellLink> pLink(it->pItem);
					if (pLink)
					{
						unsigned int hash=CalcLinkHash(pLink);
						if (hash)
							ignoreLinks.push_back(hash);
					}
				}
			}
		}
	}

	int taskIndex=-1;
	for (UINT catIndex=0;catIndex<categoryCount;catIndex++)
	{
		APPDESTCATEGORY category={0};
		if (SUCCEEDED(pCustomList->GetCategory(catIndex,1,&category)))
		{
			if (category.type==0)
			{
				// custom group
				if (category.name)
				{
					wchar_t name[256];
					SHLoadIndirectString(category.name,name,_countof(name),NULL);
					CoTaskMemFree(category.name);
					CComPtr<IObjectCollection> pCollection;
					if (SUCCEEDED(pCustomList->EnumerateCategoryDestinations(catIndex,IID_IObjectCollection,(void**)&pCollection)))
					{
						list.groups.resize(list.groups.size()+1);
						CJumpGroup &group=*list.groups.rbegin();
						group.name=name;
						group.type=CJumpGroup::TYPE_CUSTOM;
						AddJumpCollection(group,pCollection,ignoreItems,ignoreLinks);
					}
				}
			}
			else if (category.type==1)
			{
				// standard group
				if (category.subType==1 || category.subType==2)
				{
					CComPtr<IObjectCollection> pCollection=autoList.GetList(3-category.subType,maxCount);
					if (pCollection)
					{
						list.groups.resize(list.groups.size()+1);
						CJumpGroup &group=*list.groups.rbegin();
						if (category.subType==1)
						{
							group.type=CJumpGroup::TYPE_FREQUENT;
							group.name=FindTranslation(L"JumpList.Frequent",L"Frequent");
						}
						else
						{
							group.type=CJumpGroup::TYPE_RECENT;
							group.name=FindTranslation(L"JumpList.Recent",L"Recent");
						}
						AddJumpCollection(group,pCollection,ignoreItems,ignoreLinks);
					}
				}
			}
			else if (category.type==2 && taskIndex==-1)
			{
				taskIndex=catIndex;
			}
		}
	}
	if (taskIndex!=-1)
	{
		// add tasks
		CComPtr<IObjectCollection> pCollection;
		if (SUCCEEDED(pCustomList->EnumerateCategoryDestinations(taskIndex,IID_IObjectCollection,(void**)&pCollection)))
		{
			list.groups.resize(list.groups.size()+1);
			CJumpGroup &group=*list.groups.rbegin();
			group.name=FindTranslation(L"JumpList.Tasks",L"Tasks");
			group.type=CJumpGroup::TYPE_TASKS;
			AddJumpCollection(group,pCollection,ignoreItems,ignoreLinks);
		}
	}

	if (categoryCount==0)
	{
		// add recent
		CComPtr<IObjectCollection> pRecentList=autoList.GetList(1,maxCount);
		if (pRecentList)
		{
			list.groups.resize(list.groups.size()+1);
			CJumpGroup &group=*list.groups.rbegin();
			group.type=CJumpGroup::TYPE_RECENT;
			group.name=FindTranslation(L"JumpList.Recent",L"Recent");
			AddJumpCollection(group,pRecentList,ignoreItems,ignoreLinks);
		}
	}

	// limit the item count (not tasks or pinned)
	for (std::vector<CJumpGroup>::iterator it=list.groups.begin();it!=list.groups.end();++it)
	{
		CJumpGroup &group=*it;
		if (group.type==CJumpGroup::TYPE_TASKS || group.type==CJumpGroup::TYPE_PINNED)
			maxHeight-=sepHeight+(int)group.items.size()*itemHeight;
	}


	for (std::vector<CJumpGroup>::iterator it=list.groups.begin();it!=list.groups.end();++it)
	{
		CJumpGroup &group=*it;
		if (group.type!=CJumpGroup::TYPE_TASKS && group.type!=CJumpGroup::TYPE_PINNED)
		{
			maxHeight-=sepHeight;
			for (std::vector<CJumpItem>::iterator it2=group.items.begin();it2!=group.items.end();++it2)
				if (!it2->bHidden)
				{
					it2->bHidden=(maxCount<=0 || maxHeight<itemHeight);
					maxCount--;
					maxHeight-=itemHeight;
				}
		}
	}

	// hide empty groups
	for (std::vector<CJumpGroup>::iterator it=list.groups.begin();it!=list.groups.end();++it)
	{
		CJumpGroup &group=*it;
		group.bHidden=true;
		for (std::vector<CJumpItem>::const_iterator it2=group.items.begin();it2!=group.items.end();++it2)
			if (!it2->bHidden)
			{
				group.bHidden=false;
				break;
			}
	}

	return true;
}

// Executes the given item using the correct application
bool ExecuteJumpItem( const CItemManager::ItemInfo *pAppInfo, const CJumpItem &item, HWND hwnd )
{
	Assert(GetWinVersion()>=_WIN32_WINNT_WIN7);
	if (!item.pItem)
		return false;

	if (item.type==CJumpItem::TYPE_ITEM)
	{
		CComQIPtr<IShellItem> pItem(item.pItem);
		if (!pItem)
			return false;

		SHELLEXECUTEINFO execute={sizeof(execute),SEE_MASK_IDLIST|SEE_MASK_FLAG_LOG_USAGE};
		execute.nShow=SW_SHOWNORMAL;
		CAbsolutePidl pidl;
		if (SUCCEEDED(SHGetIDListFromObject(pItem,&pidl)))
		{
			execute.lpIDList=pidl;
			ShellExecuteEx(&execute);
		}
		return true;
	}

	if (item.type==CJumpItem::TYPE_LINK)
	{
		//  Name:     System.AppUserModel.HostEnvironment -- PKEY_AppUserModel_HostEnvironment
		//  Type:     UInt32 -- VT_UI4
		//  FormatID: {9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, 14
		static const PROPERTYKEY PKEY_AppUserModel_HostEnvironment = { {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 14 };

		//  Name:     System.AppUserModel.ActivationContext -- PKEY_AppUserModel_ActivationContext
		//  Type:     String -- VT_LPWSTR
		//  FormatID: {9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, 20
		static const PROPERTYKEY PKEY_AppUserModel_ActivationContext = { {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 20 };

		CComQIPtr<IContextMenu> pMenu(item.pItem);
		CStringA params;

		CComQIPtr<IShellLink> pLink(item.pItem);
		if (pLink)
		{
			CComQIPtr<IPropertyStore> store(pLink);
			if (store)
			{
				auto appId = GetPropertyStoreString(store, PKEY_AppUserModel_ID);
				if (!appId.IsEmpty())
				{
					CComPtr<IShellItem2> target;
					if (SUCCEEDED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder, 0, appId, IID_PPV_ARGS(&target))))
					{
						ULONG modern = 0;
						if (SUCCEEDED(target->GetUInt32(PKEY_AppUserModel_HostEnvironment, &modern)) && modern)
						{
							CComQIPtr<IContextMenu> targetMenu;
							if (SUCCEEDED(target->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&targetMenu))))
							{
								pMenu = targetMenu;
								params = CT2CA(GetPropertyStoreString(store, PKEY_AppUserModel_ActivationContext));
							}
						}
					}
				}
			}
		}

		// invoke the link through its context menu
		if (!pMenu)
			return false;

		HRESULT hr;
		HMENU menu=CreatePopupMenu();
		hr=pMenu->QueryContextMenu(menu,0,1,1000,CMF_DEFAULTONLY);
		if (FAILED(hr))
		{
			DestroyMenu(menu);
			return false;
		}
		int id=GetMenuDefaultItem(menu,FALSE,0);
		if (id>0)
		{
			CMINVOKECOMMANDINFO command={sizeof(command),CMIC_MASK_FLAG_LOG_USAGE};
			command.lpVerb=MAKEINTRESOURCEA(id-1);
			if (!params.IsEmpty())
				command.lpParameters = params;
			wchar_t path[_MAX_PATH];
			GetModuleFileName(NULL,path,_countof(path));
			if (_wcsicmp(PathFindFileName(path),L"explorer.exe")==0)
				command.fMask|=CMIC_MASK_ASYNCOK;
			command.hwnd=hwnd;
			command.nShow=SW_SHOWNORMAL;
			hr=pMenu->InvokeCommand(&command);
		}
		DestroyMenu(menu);
	}

	return true;
}

// Removes the given item from the jumplist
void RemoveJumpItem( const CItemManager::ItemInfo *pAppInfo, CJumpList &list, int groupIdx, int itemIdx )
{
	CString appid;
	{
		CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
		appid=pAppInfo->GetAppid();
	}
	CJumpGroup &group=list.groups[groupIdx];
	if (group.type==CJumpGroup::TYPE_FREQUENT || group.type==CJumpGroup::TYPE_RECENT)
	{
		if (CAutomaticList(appid).RemoveDestination(group.items[itemIdx].pItem))
			group.items.erase(group.items.begin()+itemIdx);
	}
	else
	{
		CComPtr<IDestinationList> pCustomList=GetCustomList(appid);
		if (pCustomList)
		{
			if (SUCCEEDED(pCustomList->RemoveDestination(group.items[itemIdx].pItem)))
				group.items.erase(group.items.begin()+itemIdx);
		}
	}
}

// Pins or unpins the given item from the jumplist
void PinJumpItem( const CItemManager::ItemInfo *pAppInfo, const CJumpList &list, int groupIdx, int itemIdx, bool bPin, int pinIndex )
{
	CString appid;
	{
		CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
		appid=pAppInfo->GetAppid();
	}
	const CJumpGroup &group=list.groups[groupIdx];
	CAutomaticList(appid).PinItem(group.items[itemIdx].pItem,bPin?pinIndex:-2);
}
