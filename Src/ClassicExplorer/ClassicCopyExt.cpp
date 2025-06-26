// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

// ClassicCopyExt.cpp : Implementation of CClassicCopyExt

#include "stdafx.h"
#include "ClassicCopyExt.h"
#include "ResourceHelper.h"
#include "dllmain.h"


// CClassicCopyExt - this is a dummy drag and drop handler. Its purpose is to get Explorer to load the DLL when
// a file is being copied or moved.

void InitClassicCopyThread( void );

STDMETHODIMP CClassicCopyExt::Initialize( PCIDLIST_ABSOLUTE pidlFolder, LPDATAOBJECT pDataObj, HKEY hProgID )
{
	InitClassicCopyThread();
	return S_OK;
}

STDMETHODIMP CClassicCopyExt::QueryContextMenu( HMENU hmenu, UINT uMenuIndex, UINT uidFirstCmd, UINT uidLastCmd, UINT uFlags )
{
	return MAKE_HRESULT(SEVERITY_SUCCESS,FACILITY_NULL,0);
}

STDMETHODIMP CClassicCopyExt::GetCommandString( UINT_PTR idCmd, UINT uFlags, UINT* pwReserved, LPSTR pszName, UINT cchMax )
{
	return E_INVALIDARG;
}

STDMETHODIMP CClassicCopyExt::InvokeCommand( LPCMINVOKECOMMANDINFO pCmdInfo )
{
	return E_INVALIDARG;
}

HRESULT WINAPI CClassicCopyExt::UpdateRegistry( BOOL bRegister )
{
	if (GetWinVersion()>=_WIN32_WINNT_WIN8)
		return S_OK;
	return _AtlModule.UpdateRegistryFromResource(IDR_CLASSICCOPYEXT,bRegister);
}
