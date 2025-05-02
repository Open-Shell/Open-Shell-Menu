// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#pragma once

enum TStartButtonType
{
	START_BUTTON_AERO,
	START_BUTTON_CLASSIC,
	START_BUTTON_CUSTOM,
//	START_BUTTON_METRO,
};

HWND CreateStartButton( int taskbarId, HWND taskBar, HWND rebar );
void DestroyStartButton( int taskbarId );
void UpdateStartButton( int taskbarId );
void PressStartButton( int taskbarId, bool bPressed );
TStartButtonType GetStartButtonType( void );
SIZE GetStartButtonSize( int taskbarId );
bool IsStartButtonSmallIcons( int taskbarId );
bool IsTaskbarSmallIcons( void );
void TaskBarMouseMove( int taskbarId );

extern bool g_bAllowMoveButton;
