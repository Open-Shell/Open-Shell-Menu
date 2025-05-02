// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

// dllmain.h : Declaration of module class.
#pragma once

#include "ClassicExplorer_h.h"
#include <vector>

class CClassicExplorerModule : public CAtlDllModuleT< CClassicExplorerModule >
{
public :
	DECLARE_LIBID(LIBID_ClassicExplorerLib)
	DECLARE_REGISTRY_APPID_RESOURCEID(IDR_CLASSICEXPLORER, "{65843E27-A491-429F-84A0-30A947E20F92}")
};

extern class CClassicExplorerModule _AtlModule;

// Some utility functions used by various modules
HWND FindChildWindow( HWND hwnd, const wchar_t *className );
INT_PTR RunSettingsDialog( HWND hWndParent, DLGPROC lpDialogFunc );

struct TlsData
{
	// the bho instance that runs on this thread
	class CExplorerBHO *bho;

	// the band instance that runs on this thread
	class CExplorerBand *band;

	// one hook for each copy thread
	HHOOK copyHook;

	// bCopyMultiFile is true if the first dialog in this thread is multi-file (IDD_FILEMULTI)
	// if so, all the rest are multi-file. this makes the UI consistent (like the position of the Yes button doesn't change)
	bool bCopyMultiFile;
};

TlsData *GetTlsData( void );
extern LPCWSTR g_LoadedSettingsAtom;
extern bool g_bLogLevel;

void WaitDllInitThread( void );
