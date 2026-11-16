// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#pragma once

#include "StartMenuDLL.h"

enum TMenuStyle
{
	MENU_CLASSIC1,
	MENU_CLASSIC2,
	MENU_WIN7,
};

enum TProgramsStyle
{
	PROGRAMS_CASCADING,
	PROGRAMS_INLINE,
	PROGRAMS_HIDDEN,
};

// the SecondPane setting: which columns get the second-pane look in the Classic two-column menu
enum TSecondPane
{
	SECOND_PANE_LAST, // the column after the last column break
	SECOND_PANE_NONE, // no column: all columns look like the first pane
	SECOND_PANE_FIRST, // the column before the first column break, drawn on the left
};

enum TRecentPrograms
{
	RECENT_PROGRAMS_NONE,
	RECENT_PROGRAMS_RECENT,
	RECENT_PROGRAMS_FREQUENT,
};

enum TPinnedPrograms
{
	PINNED_PROGRAMS_FAST,
	PINNED_PROGRAMS_PINNED,
};

enum TRecentKeys
{
	RECENT_KEYS_NONE,
	RECENT_KEYS_NORMAL,
	RECENT_KEYS_DIGITS,
	RECENT_KEYS_HIDDEN_DIGITS,
};

enum TShutdownType
{
	SHUTDOWN_TYPE_NONE,
	SHUTDOWN_TYPE_SHUTDOWN,
	SHUTDOWN_TYPE_RESTART,
	SHUTDOWN_TYPE_LOGOFF,
	SHUTDOWN_TYPE_SLEEP,
	SHUTDOWN_TYPE_HIBERNATE,
	SHUTDOWN_TYPE_LOCK,
	SHUTDOWN_TYPE_SWITCHUSER,
	SHUTDOWN_TYPE_SHUTDOWN_BOX,

	SHUTDOWN_TYPE_COUNT
};

enum TSearchBoxType
{
	SEARCHBOX_HIDDEN,
	SEARCHBOX_NORMAL,
	SEARCHBOX_TAB,
};

enum TTaskbarLook
{
	TASKBAR_OPAQUE,
	TASKBAR_TRANSPARENT,
	TASKBAR_GLASS,
	TASKBAR_AEROGLASS,
};

enum TTaskbarTile
{
	TILE_TILE,
	TILE_STRETCH,
};

enum
{
	COMPATIBILITY_ENUM_SHELLITEM= 1, // use IEnumShellItems to enumerate items
	COMPATIBILITY_ENUM_FIX_PIDLS= 2, // regenerate child pidls when enumerating folders (requres COMPATIBILITY_ENUM_SHELLITEM)
	COMPATIBILITY_SKIP_DESKTOP=   4, // don't collect items from desktop
	COMPATIBILITY_TRIM_HOOKS=     8, // less hooking for Win7
	COMPATIBILITY_UPDATE_ITEMS=  16, // update the shell items for all search results
	COMPATIBILITY_NO_TOUCH_KBD=  32, // disable the touch keyboard support

	COMPATIBILITY_CRASH_TEST=     0x80000000
};

void InitSettings( void );
STARTMENUAPI void EditSettings( bool bModal, int tab );
STARTMENUAPI bool DllImportSettingsXml( const wchar_t *fname );
STARTMENUAPI bool DllExportSettingsXml( const wchar_t *fname );
