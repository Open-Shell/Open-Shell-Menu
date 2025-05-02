// Open-Shell Modern Settings shell folder
// Provides folder that contains all modern settings

#pragma once
#include "resource.h"
#include "ComHelper.h"
#include "StartMenuHelper_h.h"
#include <shlobj.h>
#include <string>

// CModernSettingsShellFolder

class ATL_NO_VTABLE CModernSettingsShellFolder :
	public CComObjectRootEx<CComSingleThreadModel>,
	public CComCoClass<CModernSettingsShellFolder, &CLSID_ModernSettingsShellFolder>,
	public IShellFolder2,
	public IPersistFolder2
{
public:
	CModernSettingsShellFolder()
	{
	}

DECLARE_REGISTRY_RESOURCEID_V2_WITHOUT_MODULE(IDR_MODERNSETTINGSSHELLFOLDER, CModernSettingsShellFolder)

DECLARE_NOT_AGGREGATABLE(CModernSettingsShellFolder)

BEGIN_COM_MAP(CModernSettingsShellFolder)
	COM_INTERFACE_ENTRY(IShellFolder)
	COM_INTERFACE_ENTRY(IShellFolder2)
	COM_INTERFACE_ENTRY(IPersist)
	COM_INTERFACE_ENTRY(IPersistFolder)
	COM_INTERFACE_ENTRY(IPersistFolder2)
END_COM_MAP()

	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct()
	{
		return S_OK;
	}

	void FinalRelease()
	{
	}

	// IShellFolder
	IFACEMETHODIMP ParseDisplayName(HWND hwnd, IBindCtx* pbc, PWSTR pszName, ULONG* pchEaten, PIDLIST_RELATIVE* ppidl, ULONG* pdwAttributes);
	IFACEMETHODIMP EnumObjects(HWND hwnd, DWORD grfFlags, IEnumIDList** ppenumIDList);
	IFACEMETHODIMP BindToObject(PCUIDLIST_RELATIVE pidl, IBindCtx* pbc, REFIID riid, void** ppv);
	IFACEMETHODIMP BindToStorage(PCUIDLIST_RELATIVE pidl, IBindCtx* pbc, REFIID riid, void** ppv);
	IFACEMETHODIMP CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2);
	IFACEMETHODIMP CreateViewObject(HWND hwnd, REFIID riid, void** ppv);
	IFACEMETHODIMP GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, ULONG* rgfInOut);
	IFACEMETHODIMP GetUIObjectOf(HWND hwnd, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, UINT* prgfInOut, void** ppv);
	IFACEMETHODIMP GetDisplayNameOf(PCUITEMID_CHILD pidl, SHGDNF shgdnFlags, STRRET* pName);
	IFACEMETHODIMP SetNameOf(HWND hwnd, PCUITEMID_CHILD pidl, PCWSTR pszName, DWORD uFlags, PITEMID_CHILD* ppidlOut);

	// IShellFolder2
	IFACEMETHODIMP GetDefaultSearchGUID(GUID* pGuid);
	IFACEMETHODIMP EnumSearches(IEnumExtraSearch** ppenum);
	IFACEMETHODIMP GetDefaultColumn(DWORD dwRes, ULONG* pSort, ULONG* pDisplay);
	IFACEMETHODIMP GetDefaultColumnState(UINT iColumn, SHCOLSTATEF* pbState);
	IFACEMETHODIMP GetDetailsEx(PCUITEMID_CHILD pidl, const PROPERTYKEY* pkey, VARIANT* pv);
	IFACEMETHODIMP GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS* pDetails);
	IFACEMETHODIMP MapColumnToSCID(UINT iColumn, PROPERTYKEY* pkey);

	// IPersist
	IFACEMETHODIMP GetClassID(CLSID* pClassID);

	// IPersistFolder
	IFACEMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidl);

	// IPersistFolder2
	IFACEMETHODIMP GetCurFolder(PIDLIST_ABSOLUTE* ppidl);

	HRESULT CreateChildID(const std::wstring_view& fileName, PITEMID_CHILD* ppidl);

private:
	std::wstring_view GetColumnDisplayName(PCUITEMID_CHILD pidl, UINT iColumn);

	CAbsolutePidl m_pidl;             // where this folder is in the name space
};

OBJECT_ENTRY_AUTO(__uuidof(ModernSettingsShellFolder), CModernSettingsShellFolder)
