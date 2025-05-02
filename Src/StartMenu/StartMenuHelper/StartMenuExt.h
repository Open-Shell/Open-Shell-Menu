// StartMenuExt.h : Declaration of the CStartMenuExt

#pragma once
#include "resource.h"       // main symbols

#include "StartMenuHelper_h.h"
#include <shlobj.h>


// CStartMenuExt

class ATL_NO_VTABLE CStartMenuExt :
	public CComObjectRootEx<CComSingleThreadModel>,
	public CComCoClass<CStartMenuExt, &CLSID_StartMenuExt>,
	public IShellExtInit,
	public IContextMenu
{
public:
	CStartMenuExt()
	{
		m_PinFolder1[0]=m_PinFolder2[0]=m_FileName[0]=0;
		m_bInPinFolder1=m_bInPinFolder2=false;
	}

	static HRESULT WINAPI UpdateRegistry( BOOL bRegister );

DECLARE_NOT_AGGREGATABLE(CStartMenuExt)

BEGIN_COM_MAP(CStartMenuExt)
	COM_INTERFACE_ENTRY(IShellExtInit)
	COM_INTERFACE_ENTRY(IContextMenu)
END_COM_MAP()



	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct()
	{
		m_FilePidl=NULL;
		return S_OK;
	}

	void FinalRelease()
	{
		if (m_FilePidl) ILFree(m_FilePidl);
		m_FilePidl=NULL;
	}

	// from IShellExtInit
	STDMETHODIMP Initialize( PCIDLIST_ABSOLUTE pidlFolder, IDataObject *pData, HKEY hkey );

	// from IContextMenu
	STDMETHODIMP QueryContextMenu(HMENU, UINT, UINT, UINT, UINT);
	STDMETHODIMP InvokeCommand( CMINVOKECOMMANDINFO *pInfo );
	STDMETHODIMP GetCommandString( UINT_PTR idCmd, UINT uFlags, UINT* pwReserved, LPSTR pszName, UINT cchMax );

	wchar_t m_PinFolder1[_MAX_PATH]; // ending with \ 
	wchar_t m_PinFolder2[_MAX_PATH]; // ending with \ 
	wchar_t m_FileName[_MAX_PATH];
	LPITEMIDLIST m_FilePidl;
	bool m_bInPinFolder1, m_bInPinFolder2;
};

OBJECT_ENTRY_AUTO(__uuidof(StartMenuExt), CStartMenuExt)
