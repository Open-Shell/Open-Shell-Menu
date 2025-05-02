// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#pragma once

struct StdMenuItem;

const StdMenuItem *ParseCustomMenu( unsigned int &rootSettings );

struct CStdCommand7
{
	enum
	{
		ITEM_SINGLE=1, // this item never has sub-menu
		ITEM_FOLDER=2, // this item always has sub-menu
		ITEM_COMPUTER=4, // this item can be expanded only one level
		ITEM_NODRIVES=8, // this item can never be expanded only one level
	};
	const wchar_t *command;
	int nameID;
	const wchar_t *label;
	const wchar_t *tip;
	const wchar_t *icon;
	const KNOWNFOLDERID *knownFolder;
	const wchar_t *knownLink;
	unsigned int settings;
	unsigned int flags;
	CString displayName; // for the settings UI

	bool operator<( const CStdCommand7 &command ) { return displayName<command.displayName; }
};

extern CStdCommand7 g_StdCommands7[];
extern const int g_StdCommands7Count;
void InitStdCommands7( void );
