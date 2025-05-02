// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include "stdafx.h"
#include "CustomMenu.h"
#include "SettingsParser.h"
#include "Translations.h"
#include "MenuContainer.h"
#include "Settings.h"
#include "SettingsUI.h"
#include "FNVHash.h"
#include "ResourceHelper.h"
#include "resource.h"
#include <algorithm>

// This table defines the standard menu items
static StdMenuItem g_StdMenu[]=
{
	// * means the command is not executable (for things like Settings, or for items that have FOLDERID)
	{L"*programs",MENU_PROGRAMS,&FOLDERID_Programs,&FOLDERID_CommonPrograms},
	{L"*favorites",MENU_FAVORITES,&FOLDERID_Favorites},
	{L"*computer",MENU_COMPUTER,&FOLDERID_ComputerFolder},
	{L"*recent_documents",MENU_DOCUMENTS,&FOLDERID_Recent},
	{L"*settings",MENU_SETTINGS},
	{L"*search",MENU_SEARCH},
	{L"help",MENU_HELP},
	{L"run",MENU_RUN},
	{L"logoff",MENU_LOGOFF},
	{L"undock",MENU_UNDOCK},
	{L"monitor_off",MENU_MONITOROFF},
	{L"disconnect",MENU_DISCONNECT},
	{L"shutdown_box",MENU_SHUTDOWN_BOX},
	{L"*user_files",MENU_USERFILES,&FOLDERID_UsersFiles},
	{L"*user_documents",MENU_USERDOCUMENTS,&FOLDERID_Documents},
	{L"*user_pictures",MENU_USERPICTURES,&FOLDERID_Pictures},
	{L"*control_panel",MENU_CONTROLPANEL,&FOLDERID_ControlPanelFolder},
	{L"pc_settings",MENU_PCSETTINGS},
	{L"windows_security",MENU_SECURITY},
	{L"*network_connections",MENU_NETWORK,&FOLDERID_ConnectionsFolder},
	{L"*printers",MENU_PRINTERS,&FOLDERID_PrintersFolder},
	{L"taskbar_settings",MENU_TASKBAR},
	{L"programs_features",MENU_FEATURES},
	{L"menu_settings",MENU_CLASSIC_SETTINGS},
	{L"search_files",MENU_SEARCH_FILES},
	{L"search_printer",MENU_SEARCH_PRINTER},
	{L"search_computers",MENU_SEARCH_COMPUTERS},
	{L"search_people",MENU_SEARCH_PEOPLE},
	{L"sleep",MENU_SLEEP},
	{L"hibernate",MENU_HIBERNATE},
	{L"restart",MENU_RESTART},
	{L"restart_noupdate",MENU_RESTART_NOUPDATE},
	{L"shutdown",MENU_SHUTDOWN},
	{L"shutdown_noupdate",MENU_SHUTDOWN_NOUPDATE},
	{L"switch_user",MENU_SWITCHUSER},
	{L"lock",MENU_LOCK},
	{L"*recent_programs",MENU_RECENT_PROGRAMS},
	{L"search_box",MENU_SEARCH_BOX},
	{L"*apps",MENU_APPS},

	{L"*fonts",MENU_CUSTOM,&FOLDERID_Fonts},
	{L"*desktop",MENU_CUSTOM,&FOLDERID_Desktop},
	{L"*admin",MENU_CUSTOM,&FOLDERID_CommonAdminTools,&FOLDERID_AdminTools},
	{L"*startup",MENU_CUSTOM,&FOLDERID_Startup,&FOLDERID_CommonStartup},
	{L"*user_music",MENU_CUSTOM,&FOLDERID_Music},
	{L"*user_videos",MENU_CUSTOM,&FOLDERID_Videos},
	{L"*downloads",MENU_CUSTOM,&FOLDERID_Downloads},
	{L"*games",MENU_CUSTOM,&FOLDERID_Games},
	{L"*links",MENU_CUSTOM,&FOLDERID_Links},
	{L"*libraries",MENU_CUSTOM,&FOLDERID_Libraries},
	{L"*lib_documents",MENU_CUSTOM,&FOLDERID_DocumentsLibrary},
	{L"*lib_music",MENU_CUSTOM,&FOLDERID_MusicLibrary},
	{L"*lib_pictures",MENU_CUSTOM,&FOLDERID_PicturesLibrary},
	{L"*lib_videos",MENU_CUSTOM,&FOLDERID_VideosLibrary},
	{L"*lib_tv",MENU_CUSTOM,&FOLDERID_RecordedTVLibrary},
	{L"*homegroup",MENU_CUSTOM,&FOLDERID_HomeGroup},
	{L"*network",MENU_CUSTOM,&FOLDERID_NetworkFolder},
	{L"*devices",MENU_CUSTOM},
	{L"*defaults",MENU_CUSTOM},
};

CStdCommand7 g_StdCommands7[]={
	{L"separator",IDS_SEPARATOR_ITEM,NULL,NULL,NULL,NULL,NULL,0,CStdCommand7::ITEM_SINGLE}, // must be first

	{L"favorites",0,NULL,NULL,L"imageres.dll,1024",&FOLDERID_Favorites,NULL,StdMenuItem::MENU_TRACK},
	{L"recent_documents",0,NULL,NULL,NULL,&FOLDERID_Recent},
	{L"computer",0,NULL,NULL,NULL,&FOLDERID_ComputerFolder,NULL,0,CStdCommand7::ITEM_COMPUTER},
	{L"help",IDS_HELP_ITEM,L"$Menu.Help",L"$Menu.HelpTip",L"imageres.dll,99",NULL,NULL,0,CStdCommand7::ITEM_SINGLE},
	{L"run",IDS_RUN_ITEM,L"$Menu.Run",L"$Menu.RunTip",L"imageres.dll,100",NULL,NULL,0,CStdCommand7::ITEM_SINGLE},
	{L"windows_security",IDS_SECURITY_ITEM,L"$Menu.Security",L"$Menu.SecurityTip",L"shell32.dll,48",NULL,NULL,0,CStdCommand7::ITEM_SINGLE},
	{L"user_files",IDS_SHOW_USERFILES,NULL,L"$Menu.UserFilesTip",NULL,&FOLDERID_UsersFiles},
	{L"user_documents",0,NULL,L"$Menu.UserDocumentsTip",NULL,&FOLDERID_Documents},
	{L"user_pictures",0,NULL,L"$Menu.UserPicturesTip",NULL,&FOLDERID_Pictures},
	{L"user_music",0,NULL,L"$Menu.UserMusicTip",NULL,&FOLDERID_Music},
	{L"user_videos",0,NULL,L"$Menu.UserVideosTip",NULL,&FOLDERID_Videos},
	{L"control_panel",0,L"$Menu.ControlPanel",L"$Menu.ControlPanelTip",NULL,&FOLDERID_ControlPanelFolder,NULL,StdMenuItem::MENU_TRACK},
	{L"pc_settings",IDS_PCSETTINGS,L"$Menu.PCSettings",L"",NULL,NULL,L"shell:appsfolder\\windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel",StdMenuItem::MENU_TRACK,CStdCommand7::ITEM_SINGLE},
	{L"network_connections",0,NULL,L"$Menu.NetworkTip",NULL,&FOLDERID_ConnectionsFolder,NULL,0,CStdCommand7::ITEM_NODRIVES},
	{L"network",0,NULL,NULL,NULL,&FOLDERID_NetworkFolder,NULL,0,CStdCommand7::ITEM_SINGLE},
	{L"printers",0,NULL,L"$Menu.PrintersTip",NULL,&FOLDERID_PrintersFolder,NULL,0,CStdCommand7::ITEM_NODRIVES},
	{L"fonts",0,NULL,NULL,NULL,&FOLDERID_Fonts},
	{L"desktop",0,NULL,NULL,NULL,&FOLDERID_Desktop},
	{L"admin",0,NULL,L"$Menu.AdminToolsTip",L"imageres.dll,114",&FOLDERID_CommonAdminTools,NULL,StdMenuItem::MENU_TRACK,CStdCommand7::ITEM_NODRIVES},
	{L"startup",0,NULL,NULL,NULL,&FOLDERID_Startup,NULL,StdMenuItem::MENU_TRACK,CStdCommand7::ITEM_NODRIVES},
	{L"downloads",0,NULL,L"$Menu.DownloadTip",NULL,&FOLDERID_Downloads},
	{L"games",0,NULL,L"$Menu.GamesTip",NULL,&FOLDERID_Games,NULL,StdMenuItem::MENU_TRACK},
	{L"links",0,NULL,NULL,NULL,&FOLDERID_Links},
	{L"libraries",0,NULL,NULL,NULL,&FOLDERID_Libraries},
	{L"lib_documents",IDS_LIB_DOCS_ITEM,NULL,L"$Menu.DocumentsLibTip",NULL,&FOLDERID_DocumentsLibrary},
	{L"lib_music",IDS_LIB_MUSIC_ITEM,NULL,L"$Menu.MusicLibTip",NULL,&FOLDERID_MusicLibrary},
	{L"lib_pictures",IDS_LIB_PICS_ITEM,NULL,L"$Menu.PicturesLibTip",NULL,&FOLDERID_PicturesLibrary},
	{L"lib_videos",IDS_LIB_VIDEOS_ITEM,NULL,L"$Menu.VideosLibTip",NULL,&FOLDERID_VideosLibrary},
	{L"lib_tv",IDS_LIB_TV_ITEM,NULL,L"$Menu.RecordingsLibTip",NULL,&FOLDERID_RecordedTVLibrary},
	{L"homegroup",0,NULL,L"$Menu.HomegroupTip",NULL,&FOLDERID_HomeGroup,NULL,0,CStdCommand7::ITEM_SINGLE},
	{L"devices",0,NULL,NULL,NULL,NULL,L"::{26EE0668-A00A-44D7-9371-BEB064C98683}\\0\\::{A8A91A66-3A7D-4424-8D24-04E180695C7A}",0,CStdCommand7::ITEM_NODRIVES},
	{L"defaults",0,NULL,NULL,NULL,NULL,L"::{26EE0668-A00A-44D7-9371-BEB064C98683}\\0\\::{17CD9488-1228-4B2F-88CE-4298E93E0966}",0,CStdCommand7::ITEM_SINGLE},
	{L"apps",IDS_METRO_APPS,L"$Menu.Apps",NULL,L",2",NULL,NULL,StdMenuItem::MENU_TRACK,CStdCommand7::ITEM_FOLDER},

	{L"",IDS_CUSTOM_ITEM} // must be last
};

const int g_StdCommands7Count=_countof(g_StdCommands7);

// This table defines folders that need special treatment
SpecialFolder g_SpecialFolders[]=
{
	{&FOLDERID_Games,SpecialFolder::FOLDER_NONEWFOLDER},
	{&FOLDERID_ComputerFolder,SpecialFolder::FOLDER_NONEWFOLDER},
	{&FOLDERID_RecycleBinFolder,SpecialFolder::FOLDER_NOSUBFOLDERS|SpecialFolder::FOLDER_NODROP},
	{&FOLDERID_NetworkFolder,SpecialFolder::FOLDER_NODROP},
	{&FOLDERID_ConnectionsFolder,SpecialFolder::FOLDER_NODROP|SpecialFolder::FOLDER_NOPATH},
	{&FOLDERID_Recent,SpecialFolder::FOLDER_NODROP},
	{&FOLDERID_ControlPanelFolder,SpecialFolder::FOLDER_NODROP},
	{&FOLDERID_PrintersFolder,SpecialFolder::FOLDER_NODROP},
	{&FOLDERID_HomeGroup,SpecialFolder::FOLDER_NODROP},
	{NULL}
};

static std::vector<StdMenuItem> g_CustomMenu;
static unsigned int g_RootSettings;
static unsigned int g_MenuItemsHash;
static CSettingsParser g_CustomMenuParser;
static bool g_bSpecialFoldersInitialized;

void InitializeSpecialFolders( void )
{
	if (!g_bSpecialFoldersInitialized)
	{
		g_bSpecialFoldersInitialized=true;
		for (int i=0;g_SpecialFolders[i].folder;i++)
		{
			CComPtr<IShellItem> pItem;
			if (SUCCEEDED(ShGetKnownFolderItem(*g_SpecialFolders[i].folder,&pItem)) && pItem)
			{
				CComString pPath;
				pItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&pPath);
				pPath.MakeUpper();
				Strcpy(g_SpecialFolders[i].PATH,_countof(g_SpecialFolders[i].PATH),pPath);
			}
		}
	}
}

static const StdMenuItem *FindStdMenuItem( const wchar_t *command )
{
	for (int i=0;i<_countof(g_StdMenu);i++)
	{
		const wchar_t *cmd=g_StdMenu[i].command;
		if (*cmd=='*') cmd++;
		if (_wcsicmp(cmd,command)==0)
			return &g_StdMenu[i];
	}
	return NULL;
}

static unsigned int ParseItemSettings( const wchar_t *name )
{
	wchar_t buf[256];
	Sprintf(buf,_countof(buf),L"%s.Settings",name);
	const wchar_t *str=g_CustomMenuParser.FindSetting(buf);
	if (!str) return 0;

	unsigned int settings=0;
	while(*str)
	{
		wchar_t token[256];
		str=GetToken(str,token,_countof(token),L", \t|;");
		if (_wcsicmp(token,L"OPEN_UP")==0) settings|=StdMenuItem::MENU_OPENUP;
		if (_wcsicmp(token,L"OPEN_UP_CHILDREN")==0) settings|=StdMenuItem::MENU_OPENUP_REC;
		if (_wcsicmp(token,L"SORT_ZA")==0) settings|=StdMenuItem::MENU_SORTZA;
		if (_wcsicmp(token,L"SORT_ZA_CHILDREN")==0) settings|=StdMenuItem::MENU_SORTZA_REC;
		if (_wcsicmp(token,L"SORT_ONCE")==0) settings|=StdMenuItem::MENU_SORTONCE;
		if (_wcsicmp(token,L"ITEMS_FIRST")==0) settings|=StdMenuItem::MENU_ITEMS_FIRST;
		if (_wcsicmp(token,L"TRACK_RECENT")==0) settings|=StdMenuItem::MENU_TRACK;
		if (_wcsicmp(token,L"NOTRACK_RECENT")==0) settings|=StdMenuItem::MENU_NOTRACK;
		if (_wcsicmp(token,L"NOEXPAND")==0) settings|=StdMenuItem::MENU_NOEXPAND;
		if (_wcsicmp(token,L"SINGLE_EXPAND")==0) settings|=StdMenuItem::MENU_SINGLE_EXPAND;
		if (_wcsicmp(token,L"MULTICOLUMN")==0) settings|=StdMenuItem::MENU_MULTICOLUMN;
		if (_wcsicmp(token,L"NOEXTENSIONS")==0) settings|=StdMenuItem::MENU_NOEXTENSIONS;
		if (_wcsicmp(token,L"INLINE")==0) settings|=StdMenuItem::MENU_INLINE;
		if (_wcsicmp(token,L"SPLIT")==0) settings|=StdMenuItem::MENU_SPLIT_BUTTON;
	}
	return settings;
}

static void ParseMenuItem( StdMenuItem &item, const wchar_t *name )
{
	wchar_t buf[1024];
	const wchar_t *str;
	Sprintf(buf,_countof(buf),L"%s.Link",name);
	str=g_CustomMenuParser.FindSetting(buf);
	if (str)
	{
		// parse link
		item.link=str;
		const wchar_t *c=wcschr(item.link,'|');
		if (c)
		{
			for (c++;*c==' ';)
				c++;
			item.link=c;
		}
	}

	Sprintf(buf,_countof(buf),L"%s.Command",name);
	str=g_CustomMenuParser.FindSetting(buf);
	int custom7=-1;
	if (str)
	{
		// parse command
		const StdMenuItem *pItem=FindStdMenuItem(str);
		if (pItem)
		{
			item.id=pItem->id;
			item.folder1=pItem->folder1;
			item.folder2=pItem->folder2;
			if (item.id==MENU_CONTROLPANEL && GetSettingBool(L"ControlPanelCategories"))
				item.command=L"::{26EE0668-A00A-44D7-9371-BEB064C98683}";
			else if (item.id==MENU_SHUTDOWN_BOX && GetSettingInt(L"MenuStyle")==MENU_WIN7)
				item.id=MENU_SHUTDOWN_BUTTON;
			else if (*pItem->command!='*')
				item.command=pItem->command;
		}
		else
		{
			item.id=MENU_CUSTOM;
			item.command=str;
		}

		for (int i=0;i<g_StdCommands7Count;i++)
			if (_wcsicmp(g_StdCommands7[i].command,str)==0)
			{
				custom7=i;
				break;
			}
	}

	Sprintf(buf,_countof(buf),L"%s.Label",name);
	str=g_CustomMenuParser.FindSetting(buf);
	if (str)
	{
		// parse name
		if (*str=='$')
		{
			item.label=FindTranslation(str+1,NULL);
			if (!item.label)
				item.label=str;
		}
		else
			item.label=str;
	}

	Sprintf(buf,_countof(buf),L"%s.Tip",name);
	str=g_CustomMenuParser.FindSetting(buf);
	if (str)
	{
		// parse name
		if (*str=='$')
			item.tip=FindTranslation(str+1,NULL);
		else
			item.tip=str;
	}

	Sprintf(buf,_countof(buf),L"%s.Icon",name);
	item.iconPath=g_CustomMenuParser.FindSetting(buf);

	item.settings=ParseItemSettings(name);
	if (custom7>=0)
	{
		if (g_StdCommands7[custom7].flags&CStdCommand7::ITEM_SINGLE)
			item.settings|=StdMenuItem::MENU_NOEXPAND;
		if (g_StdCommands7[custom7].flags&CStdCommand7::ITEM_FOLDER)
			item.settings&=~StdMenuItem::MENU_NOEXPAND;
	}
}

const wchar_t *g_StartMenuItems7a=L"Items=COLUMN_PADDING, ProgramsMenu, SearchBoxItem, COLUMN_BREAK, ";
const wchar_t *g_StartMenuItems7b=
L" COLUMN_PADDING, ShutdownBoxItem\n"
L"ProgramsMenu.Command=programs\n"
L"ProgramsMenu.Label=$Menu.Programs\n"
L"ProgramsMenu.Tip=$Menu.ProgramsTip\n"
L"ProgramsMenu.Icon=none\n"
L"ProgramsMenu.Items=AppsMenu\n"
L"ProgramsMenu.Settings=ITEMS_FIRST\n"
L"AppsMenu.Command=apps\n"
L"AppsMenu.Label=$Menu.Apps\n"
L"AppsMenu.Icon=,2\n"
L"SearchBoxItem.Command=search_box\n"
L"SearchBoxItem.Label=$Menu.SearchPrograms\n"
L"SearchBoxItem.Icon=none\n"
L"SearchBoxItem.Settings=TRACK_RECENT, OPEN_UP\n"
L"ShutdownBoxItem.Command=shutdown_box\n"
L"ShutdownBoxItem.Label=shutdown\n"
L"ShutdownBoxItem.Icon=none\n"
L"ShutdownBoxItem.Items=SwitchUserItem, SecurityItem, LogOffItem, LockItem, DisconnectItem, UndockItem, SEPARATOR, RestartNUItem, RestartItem, SEPARATOR, SleepItem, HibernateItem, ShutdownNUItem, ShutdownItem\n"
L"ShutdownBoxItem.Settings=SPLIT\n"
L"SwitchUserItem.Command=switch_user\n"
L"SwitchUserItem.Label=$Menu.SwitchUser\n"
L"SwitchUserItem.Tip=$Menu.SwitchUserTip\n"
L"SwitchUserItem.Icon=none\n"
L"SecurityItem.Command=windows_security\n"
L"SecurityItem.Icon=none\n"
L"SecurityItem.Label=$Menu.Security\n"
L"SecurityItem.Tip=$Menu.SecurityTip\n"
L"LogOffItem.Command=logoff\n"
L"LogOffItem.Label=$Menu.LogOffShort\n"
L"LogOffItem.Tip=$Menu.LogOffTip\n"
L"LogOffItem.Icon=none\n"
L"LockItem.Command=lock\n"
L"LockItem.Label=$Menu.Lock\n"
L"LockItem.Tip=$Menu.LockTip\n"
L"LockItem.Icon=none\n"
L"DisconnectItem.Command=disconnect\n"
L"DisconnectItem.Label=$Menu.Disconnect\n"
L"DisconnectItem.Tip=$Menu.DisconnectTip\n"
L"DisconnectItem.Icon=none\n"
L"UndockItem.Command=undock\n"
L"UndockItem.Label=$Menu.Undock\n"
L"UndockItem.Tip=$Menu.UndockTip\n"
L"UndockItem.Icon=none\n"
L"RestartNUItem.Command=restart_noupdate\n"
L"RestartNUItem.Label=$Menu.Restart\n"
L"RestartNUItem.Tip=$Menu.RestartTip\n"
L"RestartNUItem.Icon=none\n"
L"RestartItem.Command=restart\n"
L"RestartItem.Label=$Menu.Restart\n"
L"RestartItem.Tip=$Menu.RestartTip\n"
L"RestartItem.Icon=none\n"
L"SleepItem.Command=sleep\n"
L"SleepItem.Label=$Menu.Sleep\n"
L"SleepItem.Tip=$Menu.SleepTip\n"
L"SleepItem.Icon=none\n"
L"HibernateItem.Command=hibernate\n"
L"HibernateItem.Label=$Menu.Hibernate\n"
L"HibernateItem.Tip=$Menu.HibernateTip\n"
L"HibernateItem.Icon=none\n"
L"ShutdownNUItem.Command=shutdown_noupdate\n"
L"ShutdownNUItem.Label=$Menu.Shutdown\n"
L"ShutdownNUItem.Tip=$Menu.ShutdownTip\n"
L"ShutdownNUItem.Icon=none\n"
L"ShutdownItem.Command=shutdown\n"
L"ShutdownItem.Label=$Menu.Shutdown\n"
L"ShutdownItem.Tip=$Menu.ShutdownTip\n"
L"ShutdownItem.Icon=none\n"
;

const StdMenuItem *ParseCustomMenu( unsigned int &rootSettings )
{
	TMenuStyle menuStyle=(TMenuStyle)GetSettingInt(L"MenuStyle");
	CString menuText;
	if (menuStyle==MENU_CLASSIC1)
		menuText=GetSettingString(L"MenuItems1");
	else if (menuStyle==MENU_CLASSIC2)
		menuText=GetSettingString(L"MenuItems2");
	else
		menuText=GetSettingString(L"MenuItems7");
	unsigned int hash=FNV_HASH0;
	if (GetSettingBool(L"ControlPanelCategories")) hash+=1;
	if (GetSettingInt(L"ProgramsStyle")==PROGRAMS_INLINE) hash+=2;
	if (GetSettingBool(L"AllProgramsMetro")) hash+=4;
	hash=CalcFNVHash(menuText,hash);

	if (hash!=g_MenuItemsHash)
	{
		if (GetSettingInt(L"MenuStyle")==MENU_WIN7)
		{
			wchar_t buf[16384];
			int len=Strcpy(buf,_countof(buf),g_StartMenuItems7a);

			CSettingsParser parser;
			parser.LoadText(menuText,menuText.GetLength());
			parser.ParseText();
			for (int i=1;;i++)
			{
				wchar_t item[100];
				Sprintf(item,_countof(item),L"Item%d.Settings",i);
				const wchar_t *settings=parser.FindSetting(item);
				if (settings && wcsstr(settings,L"ITEM_DISABLED"))
					continue;
				Sprintf(item,_countof(item),L"Item%d.Command",i);
				const wchar_t *command=parser.FindSetting(item);
				if (!command)
				{
					Sprintf(item,_countof(item),L"Item%d.Link",i);
					if (!parser.FindSetting(item))
						break;
				}
				if (command && (_wcsicmp(command,L"shutdown_box")==0 || _wcsicmp(command,L"recent_programs")==0 || _wcsicmp(command,L"search_box")==0 || _wcsicmp(command,L"programs")==0))
				{
					// these commands can't be in the right column because they are already in other parts of the menu
					continue;
				}
				if (command && _wcsicmp(command,L"separator")==0)
				{
					len+=Sprintf(buf+len,_countof(buf)-len,L"SEPARATOR, ");
				}
				else
				{
					len+=Sprintf(buf+len,_countof(buf)-len,L"Item%d, ",i);
				}
			}
			len+=Strcat(buf+len,_countof(buf)-len,g_StartMenuItems7b);
			len+=Strcat(buf+len,_countof(buf)-len,menuText);

			for (int i=1;;i++)
			{
				wchar_t item[100];
				Sprintf(item,_countof(item),L"Item%d.Settings",i);
				const wchar_t *settings=parser.FindSetting(item);
				if (settings && wcsstr(settings,L"ITEM_DISABLED"))
					continue;
				Sprintf(item,_countof(item),L"Item%d.Command",i);
				const wchar_t *command=parser.FindSetting(item);
				Sprintf(item,_countof(item),L"Item%d.Link",i);
				const wchar_t *link=parser.FindSetting(item);
				if (!command)
				{
					if (!link)
						break;
					else
						continue;
				}

				Sprintf(item,_countof(item),L"Item%d.Label",i);
				const wchar_t *label=parser.FindSetting(item);
				Sprintf(item,_countof(item),L"Item%d.icon",i);
				const wchar_t *icon=parser.FindSetting(item);
				Sprintf(item,_countof(item),L"Item%d.Tip",i);
				const wchar_t *tip=parser.FindSetting(item);

				// handle special items
				for (int j=1;j<g_StdCommands7Count-1;j++)
				{
					const CStdCommand7 &stdCommand=g_StdCommands7[j];
					if (_wcsicmp(command,stdCommand.command)==0)
					{
						if (!link && stdCommand.knownLink)
							len+=Sprintf(buf+len,_countof(buf)-len,L"Item%d.Link=%s\n",i,stdCommand.knownLink);
						if (!label && stdCommand.label)
							len+=Sprintf(buf+len,_countof(buf)-len,L"Item%d.Label=%s\n",i,stdCommand.label);
						if (!icon && stdCommand.icon)
							len+=Sprintf(buf+len,_countof(buf)-len,L"Item%d.Icon=%s\n",i,stdCommand.icon);
						if (!tip && stdCommand.tip)
							len+=Sprintf(buf+len,_countof(buf)-len,L"Item%d.Tip=%s\n",i,stdCommand.tip);
					}
				}
			}
			if (GetWinVersion()<WIN_VER_WIN8 || GetSettingInt(L"ProgramsStyle")==PROGRAMS_INLINE || !GetSettingBool(L"AllProgramsMetro"))
				len+=Strcpy(buf+len,_countof(buf)-len,L"ProgramsMenu.Items=\n");

			menuText=buf;
		}
		g_RootSettings=0;
		g_MenuItemsHash=hash;
		g_CustomMenu.clear();
		g_CustomMenuParser.Reset();
		g_CustomMenuParser.LoadText(menuText,menuText.GetLength());
		g_CustomMenuParser.ParseText();

		std::vector<CSettingsParser::TreeItem> items;
		g_CustomMenuParser.ParseTree(L"Items",items);
		g_CustomMenu.resize(items.size());
		for (size_t i=0;i<items.size();i++)
		{
			const wchar_t *name=items[i].name;
			StdMenuItem &item=g_CustomMenu[i];

			item.command=0;
			item.id=MENU_NO;
			item.folder1=item.folder2=NULL;
			item.label=item.tip=item.iconPath=item.link=NULL;
			item.settings=0;
			item.submenu=NULL;

			// handle special names
			if (!*name)
			{
				item.id=MENU_LAST;
				continue;
			}
			if (_wcsicmp(name,L"SEPARATOR")==0)
			{
				item.id=MENU_SEPARATOR;
				continue;
			}
			if (_wcsicmp(name,L"COLUMN_PADDING")==0)
			{
				item.id=MENU_COLUMN_PADDING;
				continue;
			}
			if (_wcsicmp(name,L"COLUMN_BREAK")==0)
			{
				item.id=MENU_COLUMN_BREAK;
				continue;
			}

			// handle custom items
			item.id=MENU_CUSTOM;
			ParseMenuItem(item,name);
			if (item.id==MENU_RECENT_PROGRAMS)
				g_RootSettings|=StdMenuItem::MENU_NORECENT;
			int idx=items[i].children;
			if (idx>=0)
				item.submenu=&g_CustomMenu[idx];
		}

		for (std::vector<StdMenuItem>::iterator it=g_CustomMenu.begin();it!=g_CustomMenu.end();++it)
			if (it->id==MENU_RECENT_PROGRAMS)
			{
				g_RootSettings|=StdMenuItem::MENU_NORECENT;
				break;
			}

		// if there is no break, add one after Programs
		if (!g_CustomMenu.empty())
		{
			bool bBreak=false;
			int after=-1;
			for (int i=0;g_CustomMenu[i].id!=MENU_LAST;i++)
			{
				if (g_CustomMenu[i].id==MENU_COLUMN_BREAK)
					bBreak=true;
				if (g_CustomMenu[i].id==MENU_PROGRAMS)
					after=i;
			}
			if (!bBreak && after>=0)
			{
				// add break
				StdMenuItem br={NULL,MENU_COLUMN_BREAK};
				const StdMenuItem *pBase=&g_CustomMenu[0];
				g_CustomMenu.insert(g_CustomMenu.begin()+after+1,br);

				// fix submenu pointers
				for (std::vector<StdMenuItem>::iterator it=g_CustomMenu.begin();it!=g_CustomMenu.end();++it)
					if (it->submenu)
					{
						int idx=(int)(it->submenu-pBase);
						if (idx>after+1)
							idx++;
						it->submenu=&g_CustomMenu[idx];
					}
			}
		}
		// ignore extra search boxes
		bool bSearchBox=false;
		for (std::vector<StdMenuItem>::iterator it=g_CustomMenu.begin();it!=g_CustomMenu.end();++it)
		{
			if (it->id==MENU_SEARCH_BOX)
			{
				if (!bSearchBox)
					bSearchBox=true;
				else
					it->id=MENU_IGNORE;
			}
		}
	}

	rootSettings=g_RootSettings;
	return &g_CustomMenu[0];
}

void InitStdCommands7( void )
{
	for (int i=0;i<g_StdCommands7Count;i++)
	{
		CStdCommand7 &command=g_StdCommands7[i];
		if (command.nameID)
			command.displayName=LoadStringEx(command.nameID);
		else if (command.knownFolder)
		{
			CAbsolutePidl pidl;
			if (SUCCEEDED(ShGetKnownFolderIDList(*command.knownFolder,&pidl)))
			{
				if (_wcsicmp(command.command,L"control_panel")==0)
				{
					ILRemoveLastID(pidl);
				}
				CComString pName;
				if (SUCCEEDED(SHGetNameFromIDList(pidl,SIGDN_NORMALDISPLAY,&pName)))
					command.displayName=pName;
			}
		}
		else if (command.knownLink)
		{
			CAbsolutePidl pidl;
			if (SUCCEEDED(MenuParseDisplayName(command.knownLink,&pidl,NULL,NULL)))
			{
				CComString pName;
				if (SUCCEEDED(SHGetNameFromIDList(pidl,SIGDN_NORMALDISPLAY,&pName)))
					command.displayName=pName;
			}
		}
		if (command.displayName.IsEmpty())
			command.displayName=command.command;
	}
	std::sort(g_StdCommands7+1,g_StdCommands7+g_StdCommands7Count-1);
}
