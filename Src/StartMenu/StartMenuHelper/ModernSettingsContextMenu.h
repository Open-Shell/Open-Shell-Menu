// Context menu handler for Open-Shell Modern Settings shell folder

#pragma once
#include "resource.h"
#include "StartMenuHelper_h.h"
#include <shlobj.h>

// CModernSettingsContextMenu

class ATL_NO_VTABLE CModernSettingsContextMenu :
	public CComObjectRootEx<CComSingleThreadModel>,
	public CComCoClass<CModernSettingsContextMenu, &CLSID_ModernSettingsContextMenu>,
	public IContextMenu,
	public IShellExtInit,
	public IObjectWithSite
{
public:
	CModernSettingsContextMenu()
	{
	}

DECLARE_REGISTRY_RESOURCEID_V2_WITHOUT_MODULE(IDR_MODERNSETTINGSCONTEXTMENU, CModernSettingsContextMenu)

DECLARE_NOT_AGGREGATABLE(CModernSettingsContextMenu)

BEGIN_COM_MAP(CModernSettingsContextMenu)
	COM_INTERFACE_ENTRY(IContextMenu)
	COM_INTERFACE_ENTRY(IShellExtInit)
	COM_INTERFACE_ENTRY(IObjectWithSite)
END_COM_MAP()

	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct()
	{
		return S_OK;
	}

	void FinalRelease()
	{
	}

	// IContextMenu
	IFACEMETHODIMP QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags);
	IFACEMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO lpici);
	IFACEMETHODIMP GetCommandString(UINT_PTR idCmd, UINT uType, UINT* pRes, LPSTR pszName, UINT cchMax);

	// IShellExtInit
	IFACEMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject* pdtobj, HKEY hkeyProgID);

	// IObjectWithSite
	IFACEMETHODIMP SetSite(IUnknown* punkSite);
	IFACEMETHODIMP GetSite(REFIID riid, void** ppvSite);

private:
	CComPtr<IDataObject> m_pdtobj;
	CComPtr<IUnknown> m_punkSite;
};

OBJECT_ENTRY_AUTO(__uuidof(ModernSettingsContextMenu), CModernSettingsContextMenu)
