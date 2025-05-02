// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

// ClassicCopyExt.h : Declaration of the CClassicCopyExt

#pragma once
#include "resource.h"       // main symbols

#include "ClassicExplorer_h.h"
#include <vector>

// CClassicCopyExt

class ATL_NO_VTABLE CClassicCopyExt :
	public CComObjectRootEx<CComSingleThreadModel>,
	public CComCoClass<CClassicCopyExt, &CLSID_ClassicCopyExt>,
	public IShellExtInit,
	public IContextMenu 

{
public:
	CClassicCopyExt()
	{
	}

	static HRESULT WINAPI UpdateRegistry( BOOL bRegister );

DECLARE_NOT_AGGREGATABLE(CClassicCopyExt)

BEGIN_COM_MAP(CClassicCopyExt)
	COM_INTERFACE_ENTRY(IShellExtInit)
	COM_INTERFACE_ENTRY(IContextMenu)
END_COM_MAP()



	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct()
	{
		return S_OK;
	}

	void FinalRelease()
	{
	}

public:
	// IShellExtInit
	STDMETHODIMP Initialize(PCIDLIST_ABSOLUTE, LPDATAOBJECT, HKEY);

	// IContextMenu
	STDMETHODIMP GetCommandString(UINT_PTR, UINT, UINT*, LPSTR, UINT);
	STDMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO);
	STDMETHODIMP QueryContextMenu(HMENU, UINT, UINT, UINT, UINT);
};

OBJECT_ENTRY_AUTO(__uuidof(ClassicCopyExt), CClassicCopyExt)
