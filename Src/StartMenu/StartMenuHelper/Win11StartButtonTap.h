#pragma once

#include <ocidl.h>

HRESULT GetWin11StartButtonTapClassObject( REFCLSID clsid, REFIID riid, LPVOID *ppv );

extern "C" void StartWin11StartButtonTap( BOOL enabled, BOOL allTaskbars );
extern "C" void UpdateWin11StartButtonTap( BOOL enabled, BOOL allTaskbars );
extern "C" void StopWin11StartButtonTap( void );
