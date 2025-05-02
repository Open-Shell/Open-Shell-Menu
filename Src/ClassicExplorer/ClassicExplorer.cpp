// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

// ClassicExplorer.cpp : Implementation of DLL Exports.

#include "stdafx.h"
#include "resource.h"
#include "ClassicExplorer_h.h"
#include "dllmain.h"

// Used to determine whether the DLL can be unloaded by OLE
STDAPI DllCanUnloadNow(void)
{
	return _AtlModule.DllCanUnloadNow();
}

extern bool g_bExplorerExe;

// Returns a class factory to create an object of the requested type
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
	WaitDllInitThread();
	if (!g_bExplorerExe && rclsid!=CLSID_ShareOverlay)
		return CLASS_E_CLASSNOTAVAILABLE;
	return _AtlModule.DllGetClassObject(rclsid, riid, ppv);
}


// DllRegisterServer - Adds entries to the system registry
STDAPI DllRegisterServer(void)
{
	WaitDllInitThread();
	// registers object, typelib and all interfaces in typelib
	HRESULT res=_AtlModule.DllRegisterServer();
	if (SUCCEEDED(res))
	{
		// mark the extensions as compatible with the enhanced protected mode of IE10
		// they are not technically IE extensions, but it complains about them as
		// being incompatible
		CComPtr<ICatRegister> catRegister;
		catRegister.CoCreateInstance(CLSID_StdComponentCategoriesMgr);
		if (catRegister)
		{
			CATID CATID_AppContainerCompatible={0x59fb2056,0xd625,0x48d0,{0xa9,0x44,0x1a,0x85,0xb5,0xab,0x26,0x40}};
			catRegister->RegisterClassImplCategories(CLSID_ExplorerBHO,1,&CATID_AppContainerCompatible);
			catRegister->RegisterClassImplCategories(CLSID_ExplorerBand,1,&CATID_AppContainerCompatible);
		}
	}
	return res;
}


// DllUnregisterServer - Removes entries from the system registry
STDAPI DllUnregisterServer(void)
{
	WaitDllInitThread();
	return _AtlModule.DllUnregisterServer();
}

// DllInstall - Adds/Removes entries to the system registry per user
//              per machine.	
STDAPI DllInstall(BOOL bInstall, LPCWSTR pszCmdLine)
{
	WaitDllInitThread();
	HRESULT hr = E_FAIL;
	static const wchar_t szUserSwitch[] = L"user";

	if (pszCmdLine != NULL)
	{
		if (_wcsnicmp(pszCmdLine, szUserSwitch, _countof(szUserSwitch)) == 0)
		{
			AtlSetPerUserRegistration(true);
		}
	}

	if (bInstall)
	{	
		hr = DllRegisterServer();
		if (FAILED(hr))
		{	
			DllUnregisterServer();
		}
	}
	else
	{
		hr = DllUnregisterServer();
	}

	return hr;
}
