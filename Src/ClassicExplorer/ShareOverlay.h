// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

// ShareOverlay.h : Declaration of the CShareOverlay

#pragma once
#include "resource.h"       // main symbols
#include <lm.h>

#include "ClassicExplorer_h.h"

// CShareOverlay

class ATL_NO_VTABLE CShareOverlay :
	public CComObjectRootEx<CComSingleThreadModel>,
	public CComCoClass<CShareOverlay, &CLSID_ShareOverlay>,
	public IShellIconOverlayIdentifier
{
public:
	CShareOverlay( void );

	DECLARE_REGISTRY_RESOURCEID_V2_WITHOUT_MODULE(IDR_SHAREOVERLAY, CShareOverlay)

	DECLARE_PROTECT_FINAL_CONSTRUCT()

	HRESULT FinalConstruct( void )
	{
		return S_OK;
	}

	void FinalRelease( void );

public:

	HRESULT _InternalQueryInterface( REFIID iid, void** ppvObject );

	// IShellIconOverlayIdentifier
	STDMETHOD (IsMemberOf)( LPCWSTR pwszPath, DWORD dwAttrib );
	STDMETHOD (GetOverlayInfo)( LPWSTR pwszIconFile, int cchMax, int * pIndex, DWORD * pdwFlags );
	STDMETHOD (GetPriority)( int * pIPriority );

	static void InitOverlay( const wchar_t *icon, bool showHidden );

private:
	CComPtr<IShellFolder> m_pDesktop;
	CRITICAL_SECTION m_Section;
	SHARE_INFO_502 *m_pShareInfo;
	DWORD m_ShareCount;
	int m_UpdateTime;

	void UpdateShareInfo( void );

	static bool s_bEnabled;
	static bool s_bShowHidden;
	static int s_Index;
	static wchar_t s_IconPath[_MAX_PATH];
};

OBJECT_ENTRY_AUTO(__uuidof(ShareOverlay), CShareOverlay)
