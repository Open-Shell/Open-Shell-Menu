// Open-Shell Modern Settings shell folder
// Provides folder that contains all modern settings
//
// To open the folder press Win+R and type:
// shell:::{82E749ED-B971-4550-BAF7-06AA2BF7E836}

// Based on Explorer Data Provider Sample (https://docs.microsoft.com/en-us/windows/win32/shell/samples-explorerdataprovider)

#include "stdafx.h"
#include "ModernSettings.h"
#include "ModernSettingsShellFolder.h"
#include "ResourceHelper.h"
#include <propkey.h>
#include <strsafe.h>
#include <Uxtheme.h>

struct ColumnDescription
{
	const wchar_t* name;
	PROPERTYKEY    key;
};

static const ColumnDescription g_columnDescriptions[] =
{
	{L"Name", PKEY_ItemNameDisplay},
	{L"Keywords", PKEY_Keywords},
	{L"Filename", PKEY_FileName},
};

#define MAGIC 'SMSO'

#pragma pack(1)
struct FVITEMID
{
	USHORT  cb;
	DWORD   magic;
	WORD    size;
	wchar_t data[1];
};
#pragma pack()

static const FVITEMID* PidlToItem(LPCITEMIDLIST pidl)
{
	if (pidl)
	{
		auto item = (const FVITEMID*)pidl;
		if (item->cb && item->magic == MAGIC)
			return item;
	}

	return nullptr;
}

ModernSettings::Setting GetModernSetting(LPCITEMIDLIST pidl)
{
	auto item = PidlToItem(pidl);
	if (item)
	{
		auto settings = GetModernSettings();
		if (settings)
			return settings->get({ item->data, item->size / sizeof(wchar_t) });
	}

	return {};
}

STDAPI StringToStrRet(PCWSTR pszName, STRRET* pStrRet)
{
	pStrRet->uType = STRRET_WSTR;
	return SHStrDup(pszName, &pStrRet->pOleStr);
}

// CModernSettingsShellFolderEnumIDList

class ATL_NO_VTABLE CModernSettingsShellFolderEnumIDList :
	public CComObjectRoot,
	public IEnumIDList
{
public:
	BEGIN_COM_MAP(CModernSettingsShellFolderEnumIDList)
		COM_INTERFACE_ENTRY(IEnumIDList)
	END_COM_MAP()

	// IEnumIDList
	IFACEMETHODIMP Next(ULONG celt, PITEMID_CHILD* rgelt, ULONG* pceltFetched)
	{
		ULONG celtFetched = 0;

		HRESULT hr = (pceltFetched || celt <= 1) ? S_OK : E_INVALIDARG;
		if (SUCCEEDED(hr))
		{
			ULONG i = 0;
			while (SUCCEEDED(hr) && i < celt && m_item < m_items.size())
			{
				hr = m_parent->CreateChildID(m_items[m_item], &rgelt[i]);
				if (SUCCEEDED(hr))
				{
					celtFetched++;
					i++;
				}

				m_item++;
			}
		}

		if (pceltFetched)
			*pceltFetched = celtFetched;

		return (celtFetched == celt) ? S_OK : S_FALSE;
	}
	IFACEMETHODIMP Skip(DWORD celt)
	{
		m_item += celt;
		return S_OK;
	}
	IFACEMETHODIMP Reset()
	{
		m_item = 0;
		return S_OK;
	}
	IFACEMETHODIMP Clone(IEnumIDList** ppenum)
	{
		// this method is rarely used and it's acceptable to not implement it.
		*ppenum = NULL;
		return E_NOTIMPL;
	}

	void Initialize(CModernSettingsShellFolder* parent)
	{
		m_parent = parent;

		m_settings = GetModernSettings();
		if (m_settings)
			m_items = m_settings->enumerate();
	}

private:
	CComPtr<CModernSettingsShellFolder> m_parent;
	std::shared_ptr<ModernSettings> m_settings;
	std::vector<std::wstring_view> m_items;
	DWORD m_item = 0;
};

// Extract icon

static void BitmapDataToStraightAlpha(void* bits, UINT width, UINT height)
{
	RGBQUAD* data = (RGBQUAD*)bits;
	for (UINT y = 0; y < height; y++)
	{
		for (UINT x = 0; x < width; x++)
		{
			auto alpha = data->rgbReserved;
			if (alpha)
			{
				data->rgbBlue = (BYTE)((DWORD)data->rgbBlue * 255 / alpha);
				data->rgbGreen = (BYTE)((DWORD)data->rgbGreen * 255 / alpha);
				data->rgbRed = (BYTE)((DWORD)data->rgbRed * 255 / alpha);
			}
			data++;
		}
	}
}

HICON IconFromGlyph(UINT glyph, UINT size)
{
	ICONINFO info{};

	info.fIcon = TRUE;
	info.hbmMask = CreateBitmap(size, size, 1, 1, nullptr);

	BITMAPINFO bi{};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = size;
	bi.bmiHeader.biHeight = -((LONG)size);
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;

	void* bits = nullptr;
	info.hbmColor = CreateDIBSection(nullptr, &bi, 0, &bits, nullptr, 0);

	HDC dc = CreateCompatibleDC(nullptr);
	SelectObject(dc, info.hbmColor);

	HFONT font = CreateFontW(size, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 0, 0, IsWin11() ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets");
	SelectObject(dc, font);

	RECT rc{};
	rc.right = size;
	rc.bottom = size;

	auto theme = OpenThemeData(nullptr, L"CompositedWindow::Window");
	DTTOPTS opts{};
	opts.dwSize = sizeof(opts);
	opts.dwFlags = DTT_TEXTCOLOR | DTT_COMPOSITED;
	opts.crText = 0x00FFFFFF;
	DrawThemeTextEx(theme, dc, 0, 0, (LPCWSTR)&glyph, 1, DT_CENTER | DT_VCENTER | DT_SINGLELINE, &rc, &opts);
	CloseThemeData(theme);

	DeleteObject(font);
	DeleteDC(dc);

	BitmapDataToStraightAlpha(bits, size, size);

	HICON retval = CreateIconIndirect(&info);

	DeleteObject(info.hbmColor);
	DeleteObject(info.hbmMask);

	return retval;
}

class ATL_NO_VTABLE GlyphExtractIcon :
	public CComObjectRoot,
	public IExtractIconW
{
public:

	BEGIN_COM_MAP(GlyphExtractIcon)
		COM_INTERFACE_ENTRY(IExtractIconW)
	END_COM_MAP()

	void SetGlyph(USHORT glyph)
	{
		m_glyph = glyph;
	}

	// IExtractIconW methods
	IFACEMETHODIMP GetIconLocation(UINT uFlags, _Out_writes_(cchMax) PWSTR pszIconFile, UINT cchMax, _Out_ int* piIndex, _Out_ UINT* pwFlags)
	{
		StringCchCopy(pszIconFile, cchMax, L"OpenShell-ModernSettingIcon");
		*piIndex = m_glyph;
		*pwFlags = GIL_NOTFILENAME;
		return S_OK;
	}
	IFACEMETHODIMP Extract(_In_ PCWSTR pszFile, UINT nIconIndex, _Out_opt_ HICON* phiconLarge, _Out_opt_ HICON* phiconSmall, UINT nIconSize)
	{
		if (phiconLarge)
			*phiconLarge = IconFromGlyph(nIconIndex, LOWORD(nIconSize));
		if (phiconSmall)
			*phiconSmall = IconFromGlyph(nIconIndex, HIWORD(nIconSize));
		return S_OK;
	}

private:
	USHORT m_glyph = 0;
};


// CModernSettingsShellFolder

// IShellFolder methods

// Translates a display name into an item identifier list.
HRESULT CModernSettingsShellFolder::ParseDisplayName(HWND hwnd, IBindCtx* pbc, PWSTR pszName, ULONG* pchEaten, PIDLIST_RELATIVE* ppidl, ULONG* pdwAttributes)
{
	return E_INVALIDARG;
}

// Allows a client to determine the contents of a folder by
// creating an item identifier enumeration object and returning
// its IEnumIDList interface. The methods supported by that
// interface can then be used to enumerate the folder's contents.
HRESULT CModernSettingsShellFolder::EnumObjects(HWND /* hwnd */, DWORD grfFlags, IEnumIDList** ppenumIDList)
{
	CComObject<CModernSettingsShellFolderEnumIDList>* enumIdList;
	auto hr = CComObject<CModernSettingsShellFolderEnumIDList>::CreateInstance(&enumIdList);
	if (SUCCEEDED(hr))
	{
		enumIdList->Initialize(this);
		hr = enumIdList->QueryInterface(IID_PPV_ARGS(ppenumIDList));
	}

	return hr;
}

// Factory for handlers for the specified item.
HRESULT CModernSettingsShellFolder::BindToObject(PCUIDLIST_RELATIVE pidl, IBindCtx* pbc, REFIID riid, void** ppv)
{
	return E_NOINTERFACE;
}

HRESULT CModernSettingsShellFolder::BindToStorage(PCUIDLIST_RELATIVE pidl, IBindCtx* pbc, REFIID riid, void** ppv)
{
	return BindToObject(pidl, pbc, riid, ppv);
}

// Called to determine the equivalence and/or sort order of two idlists.
HRESULT CModernSettingsShellFolder::CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2)
{
	UINT column = LOWORD(lParam);
	return MAKE_HRESULT(SEVERITY_SUCCESS, 0, (USHORT)(StrCmp(GetColumnDisplayName(pidl1, column).data(), GetColumnDisplayName(pidl2, column).data())));
}

// Called by the Shell to create the View Object and return it.
HRESULT CModernSettingsShellFolder::CreateViewObject(HWND hwnd, REFIID riid, void** ppv)
{
	HRESULT hr = E_NOINTERFACE;
	*ppv = NULL;

	if (riid == IID_IShellView)
	{
		SFV_CREATE csfv = { sizeof(csfv), 0 };
		hr = QueryInterface(IID_PPV_ARGS(&csfv.pshf));
		if (SUCCEEDED(hr))
		{
			hr = SHCreateShellFolderView(&csfv, (IShellView**)ppv);
			csfv.pshf->Release();
		}
	}

	return hr;
}

// Retrieves the attributes of one or more file objects or subfolders.
HRESULT CModernSettingsShellFolder::GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, ULONG* rgfInOut)
{
	*rgfInOut &= SFGAO_CANLINK;
	return S_OK;
}

// Retrieves an OLE interface that can be used to carry out
// actions on the specified file objects or folders.
HRESULT CModernSettingsShellFolder::GetUIObjectOf(HWND hwnd, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, UINT* /* prgfInOut */, void** ppv)
{
	HRESULT hr = E_NOINTERFACE;
	*ppv = nullptr;

	if (riid == IID_IContextMenu)
	{
		// The default context menu will call back for IQueryAssociations to determine the
		// file associations with which to populate the menu.
		const DEFCONTEXTMENU dcm = { hwnd, nullptr, m_pidl, static_cast<IShellFolder2*>(this), cidl, apidl, nullptr, 0, nullptr };
		hr = SHCreateDefaultContextMenu(&dcm, riid, ppv);
	}
	else if (riid == IID_IExtractIconW)
	{
		hr = E_INVALIDARG;

		auto s = GetModernSetting(*apidl);
		if (s)
		{
			if (!s.icon.empty())
			{
				CComPtr<IDefaultExtractIconInit> pdxi;
				hr = SHCreateDefaultExtractIcon(IID_PPV_ARGS(&pdxi));
				if (SUCCEEDED(hr))
				{
					WCHAR icon_path[MAX_PATH];

					StringCchCopy(icon_path, _countof(icon_path), s.icon.data());
					auto location = PathParseIconLocation(icon_path);

					hr = pdxi->SetNormalIcon(icon_path, location);
					if (SUCCEEDED(hr))
						hr = pdxi->QueryInterface(riid, ppv);
				}
			}
			else
			{
				auto glyph = !s.glyph.empty() ? s.glyph.front() : 0xe115;

				CComObject<GlyphExtractIcon>* extract;
				hr = CComObject<GlyphExtractIcon>::CreateInstance(&extract);
				if (SUCCEEDED(hr))
				{
					extract->SetGlyph(glyph);
					hr = extract->QueryInterface(riid, ppv);
				}
			}
		}
	}
	else if (riid == IID_IDataObject)
	{
		hr = SHCreateDataObject(m_pidl, cidl, apidl, nullptr, riid, ppv);
	}
	else if (riid == IID_IQueryAssociations)
	{
		WCHAR szFolderViewImplClassID[64];
		hr = StringFromGUID2(CLSID_ModernSettingsShellFolder, szFolderViewImplClassID, ARRAYSIZE(szFolderViewImplClassID));
		if (SUCCEEDED(hr))
		{
			const ASSOCIATIONELEMENT assocItem = { ASSOCCLASS_CLSID_STR, nullptr, szFolderViewImplClassID };
			hr = AssocCreateForClasses(&assocItem, 1, riid, ppv);
		}
	}

	return hr;
}

// Retrieves the display name for the specified file object or subfolder.
HRESULT CModernSettingsShellFolder::GetDisplayNameOf(PCUITEMID_CHILD pidl, SHGDNF shgdnFlags, STRRET* pName)
{
	auto setting = GetModernSetting(pidl);
	if (!setting)
		return E_INVALIDARG;

	HRESULT hr = S_OK;

	if (shgdnFlags & SHGDN_FORPARSING)
	{
		if (shgdnFlags & SHGDN_INFOLDER)
		{
			// This form of the display name needs to be handled by ParseDisplayName.
			hr = StringToStrRet(setting.fileName.data(), pName);
		}
		else
		{
			WCHAR szDisplayName[MAX_PATH];
			CComString pszThisFolder;
			hr = SHGetNameFromIDList(m_pidl, (shgdnFlags & SHGDN_FORADDRESSBAR) ? SIGDN_DESKTOPABSOLUTEEDITING : SIGDN_DESKTOPABSOLUTEPARSING, &pszThisFolder);
			if (SUCCEEDED(hr))
			{
				StringCchCopy(szDisplayName, ARRAYSIZE(szDisplayName), pszThisFolder);
				StringCchCat(szDisplayName, ARRAYSIZE(szDisplayName), L"\\");
				StringCchCat(szDisplayName, ARRAYSIZE(szDisplayName), setting.fileName.data());

				hr = StringToStrRet(szDisplayName, pName);
			}
		}
	}
	else
	{
		hr = StringToStrRet(setting.description.data(), pName);
	}

	return hr;
}

// Sets the display name of a file object or subfolder, changing the item identifier in the process.
HRESULT CModernSettingsShellFolder::SetNameOf(HWND /* hwnd */, PCUITEMID_CHILD /* pidl */,	PCWSTR /* pszName */, DWORD /* uFlags */, PITEMID_CHILD* ppidlOut)
{
	*ppidlOut = NULL;
	return E_NOTIMPL;
}

// IShellFolder2 methods

// Requests the GUID of the default search object for the folder.
HRESULT CModernSettingsShellFolder::GetDefaultSearchGUID(GUID* /* pguid */)
{
	return E_NOTIMPL;
}

HRESULT CModernSettingsShellFolder::EnumSearches(IEnumExtraSearch** ppEnum)
{
	*ppEnum = NULL;
	return E_NOINTERFACE;
}

// Retrieves the default sorting and display column (indices from GetDetailsOf).
HRESULT CModernSettingsShellFolder::GetDefaultColumn(DWORD /* dwRes */, ULONG* pSort, ULONG* pDisplay)
{
	*pSort = 0;
	*pDisplay = 0;
	return S_OK;
}

// Retrieves the default state for a specified column.
HRESULT CModernSettingsShellFolder::GetDefaultColumnState(UINT iColumn, SHCOLSTATEF* pcsFlags)
{
	if (iColumn < _countof(g_columnDescriptions))
	{
		*pcsFlags = SHCOLSTATE_ONBYDEFAULT | SHCOLSTATE_TYPE_STR;
		return S_OK;
	}

	return E_INVALIDARG;
}

// Retrieves detailed information, identified by a property set ID (FMTID) and property ID (PID), on an item in a Shell folder.
HRESULT CModernSettingsShellFolder::GetDetailsEx(PCUITEMID_CHILD pidl, const PROPERTYKEY* pkey, VARIANT* pv)
{
	for (const auto& desc : g_columnDescriptions)
	{
		if (IsEqualPropertyKey(*pkey, desc.key))
		{
			auto str = GetColumnDisplayName(pidl, (UINT)std::distance(g_columnDescriptions, &desc));

			pv->vt = VT_BSTR;
			pv->bstrVal = SysAllocString(str.data());
			return pv->bstrVal ? S_OK : E_OUTOFMEMORY;
		}
	}

	return S_OK;
}

//  Retrieves detailed information, identified by a column index, on an item in a Shell folder.
HRESULT CModernSettingsShellFolder::GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS* pDetails)
{
	pDetails->cxChar = 24;

	if (!pidl)
	{
		// No item means we're returning information about the column itself.

		if (iColumn >= _countof(g_columnDescriptions))
		{
			// GetDetailsOf is called with increasing column indices until failure.
			return E_FAIL;
		}

		pDetails->fmt = LVCFMT_LEFT;
		return StringToStrRet(g_columnDescriptions[iColumn].name, &pDetails->str);
	}

	auto str = GetColumnDisplayName(pidl, iColumn);
	return StringToStrRet(str.data(), &pDetails->str);
}

//  Converts a column name to the appropriate property set ID (FMTID) and property ID (PID).
HRESULT CModernSettingsShellFolder::MapColumnToSCID(UINT iColumn, PROPERTYKEY* pkey)
{
	if (iColumn < _countof(g_columnDescriptions))
	{
		*pkey = g_columnDescriptions[iColumn].key;
		return S_OK;
	}

	return E_FAIL;
}

// IPersist method
HRESULT CModernSettingsShellFolder::GetClassID(CLSID* pClassID)
{
	*pClassID = CLSID_ModernSettingsShellFolder;
	return S_OK;
}

// IPersistFolder method
HRESULT CModernSettingsShellFolder::Initialize(PCIDLIST_ABSOLUTE pidl)
{
	m_pidl = pidl;
	return m_pidl ? S_OK : E_FAIL;
}

// IPersistFolder2 methods
// Retrieves the PIDLIST_ABSOLUTE for the folder object.
HRESULT CModernSettingsShellFolder::GetCurFolder(PIDLIST_ABSOLUTE* ppidl)
{
	*ppidl = NULL;
	HRESULT hr = m_pidl ? S_OK : E_FAIL;
	if (SUCCEEDED(hr))
	{
		*ppidl = ILCloneFull(m_pidl);
		hr = *ppidl ? S_OK : E_OUTOFMEMORY;
	}
	return hr;
}

HRESULT CModernSettingsShellFolder::CreateChildID(const std::wstring_view& fileName, PITEMID_CHILD* ppidl)
{
	auto size = fileName.size() * sizeof(wchar_t);

	// Sizeof an object plus the next cb plus the characters in the string.
	UINT nIDSize = sizeof(FVITEMID) + sizeof(USHORT) + (WORD)size;

	// Allocate and zero the memory.
	FVITEMID* lpMyObj = (FVITEMID*)CoTaskMemAlloc(nIDSize);

	HRESULT hr = lpMyObj ? S_OK : E_OUTOFMEMORY;
	if (SUCCEEDED(hr))
	{
		ZeroMemory(lpMyObj, nIDSize);
		lpMyObj->cb = static_cast<short>(nIDSize - sizeof(lpMyObj->cb));
		lpMyObj->magic = MAGIC;
		lpMyObj->size = (WORD)size;
		memcpy(lpMyObj->data, fileName.data(), size);

		*ppidl = (PITEMID_CHILD)lpMyObj;
	}

	return hr;
}

std::wstring_view CModernSettingsShellFolder::GetColumnDisplayName(PCUITEMID_CHILD pidl, UINT iColumn)
{
	auto setting = GetModernSetting(pidl);
	if (setting)
	{
		switch (iColumn)
		{
		case 0:
			return setting.description;
		case 1:
			return setting.keywords;
		case 2:
			return setting.fileName;
		}
	}

	return {};
}
