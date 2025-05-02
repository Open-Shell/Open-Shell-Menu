// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#pragma once

class ISettingsPanel
{
public:
	virtual HWND Create( HWND parent )=0;
	virtual HWND Activate( struct CSetting *pGroup, const RECT &rect, bool bReset )=0;
	virtual bool Validate( HWND parent )=0;
};

struct CSetting
{
	enum Type
	{
		TYPE_GROUP=-2,
		TYPE_RADIO=-1,
		TYPE_BOOL=1,
		TYPE_INT,
		TYPE_HOTKEY,
		TYPE_HOTKEY_ANY,
		TYPE_COLOR, // requires the high 8 bits of the flags to contain an index into the color palette

		// string types
		TYPE_STRING,
		TYPE_ICON,
		TYPE_BITMAP,
		TYPE_BITMAP_JPG,
		TYPE_SOUND,
		TYPE_FONT,
		TYPE_MULTISTRING,
		TYPE_DIRECTORY,
	};

	enum
	{
		// initial settings
		FLAG_WARM=           0x00001,
		FLAG_COLD=           0x00002,
		FLAG_BASIC=          0x00004,
		FLAG_HIDDEN=         0x00008,
		FLAG_SHARED=         0x00010,
		FLAG_NORESET=        0x00020,
		FLAG_NODEFAULT=      0x00040,
		FLAG_NOSAVE=         0x00080,
		FLAG_CALLBACK=       0x00100,

		FLAG_MENU_CLASSIC1=  0x00200,
		FLAG_MENU_CLASSIC2=  0x00400,
		FLAG_MENU_CLASSIC_BOTH=FLAG_MENU_CLASSIC1|FLAG_MENU_CLASSIC2,
		FLAG_MENU_WIN7=      0x00800,
		FLAG_MENU_MASK=FLAG_MENU_CLASSIC1|FLAG_MENU_CLASSIC2|FLAG_MENU_WIN7,

		// for run-time use only
		FLAG_DEFAULT=        0x01000,
		FLAG_FORCED_DEFAULT= 0x02000,
		FLAG_LOCKED_REG=     0x04000, // locked by HKLM registry setting
		FLAG_LOCKED_GP=      0x08000, // locked by a group policy
		FLAG_LOCKED_MASK=FLAG_LOCKED_REG|FLAG_LOCKED_GP,
		FLAG_WARNING=        0x10000, // show a warning icon
		// top 8 bits need to stay unused. that's where we store the color index
	};

	const wchar_t *name;
	Type type;
	int nameID, tipID; // resource strings
	CComVariant defValue; // default value
	unsigned int flags;
	const wchar_t *depend;
	const wchar_t *nest;
	ISettingsPanel *pPanel; // custom panel for editing this group of settings

	CSetting *pLinkTo; // if this is not NULL, then the value is stored in the linked setting (the default value, radio settings, etc. must match between the two)

	// runtime use only
	CComVariant value; // current value
	CComVariant tempValue; // the value is stored here when editing begins and restored if the editing is canceled
	unsigned int tempFlags;

	bool IsEnabled( void ) const;
	bool IsDefault( void ) const { return ((pLinkTo?pLinkTo->flags:flags)&FLAG_DEFAULT)!=0; }
	bool IsForcedDefault( void ) const { return ((pLinkTo?pLinkTo->flags:flags)&FLAG_FORCED_DEFAULT)!=0; }
	bool IsLocked( void ) const { return ((pLinkTo?pLinkTo->flags:flags)&FLAG_LOCKED_MASK)!=0; }
	bool IsLockedReg( void ) const { return ((pLinkTo?pLinkTo->flags:flags)&FLAG_LOCKED_REG)!=0; }
	bool ShouldLoad( bool bShared ) const;
	bool MatchFilter( const wchar_t *filter ) const;
	const CComVariant &GetValue( void ) const { return pLinkTo?pLinkTo->value:value; }

	void LoadValue( CRegKey &regSettings, CRegKey &regSettingsUser, CRegKey &regPolicy, CRegKey &regPolicyUser );

private:
	bool ReadValue( CRegKey &regKey, const wchar_t *valName );
};

// Images in the tree image list
enum {
	SETTING_STATE_NONE=1,
	SETTING_STATE_SETTING=2,
	SETTING_STATE_CHECKBOX=4,
	SETTING_STATE_RADIO=8,

	// additional flags
	SETTING_STATE_DISABLED=1,
	SETTING_STATE_CHECKED=2,

	SETTING_IMAGE_COLOR=13, // 10 blank images to use for custom colors
};

enum TSettingsComponent
{
	COMPONENT_EXPLORER,
	COMPONENT_MENU,
	COMPONENT_IE,
	COMPONENT_UPDATE,
	COMPONENT_SHARED,
};

class ICustomSettings
{
public:
	virtual int AppendBackupMenu( HMENU menu, int id ) = 0;
	virtual void ExecuteBackupMenu( int id ) = 0;
};

void InitSettings( CSetting *pSettings, TSettingsComponent component, ICustomSettings *pCustom );
void LoadSettings( void );
void SaveSettings( void );
void UpdateDefaultSettings( void );
void EditSettings( const wchar_t *title, bool bModal, int tab, const wchar_t *appId = nullptr );
void CloseSettings( void );
void SetSettingsDirty( void );
void SelectSettingsTab( int tab, bool bAdvanced, const CSetting *pSelect );
void UpdateSettings( void ); // implemented by the user
void UpgradeSettings( bool bShared ); // implemented by the user (called when converting 3.0 settings to 4.0)
void ClosingSettings( HWND hWnd, int flags, int command ); // implemented by the user
void SettingChangedCallback( const CSetting *pSetting ); // implemented by the user
bool IsSettingsMessage( MSG *msg );
bool ImportSettingsXml( const wchar_t *fname );
bool ExportSettingsXml( const wchar_t *fname );
const CSetting *GetAllSettings( void );
void SetSettingsStyle( int style, int mask );
void GetSettingsStyle( int &style, int &mask );

// Finds a setting by name
CSetting *FindSetting( const wchar_t *name );
// Updates the setting with a new default value and locked flag
void UpdateSetting( const wchar_t *name, const CComVariant &defValue, bool bLockedGP );
// Updates the setting with a new tooltip and a warning flag
void UpdateSettingText( const wchar_t *name, int nameID, int tipID, bool bWarning );

void HideSetting( const wchar_t *name, bool bHide );
void HideSettingGroup( const wchar_t *name, bool bHide );
void UpdateGroupText( const wchar_t *name, int nameID );

bool GetSettingBool( const wchar_t *name );
int GetSettingInt( const wchar_t *name );
CString GetSettingString( const wchar_t *name );
bool IsSettingLocked( const wchar_t *name );
bool IsSettingForcedDefault( const wchar_t *name );

// In some cases the default can change dynamically, so the setting may be out of date. Use bDef to detect if the default value should be used
int GetSettingInt( const wchar_t *name, bool &bDef );
bool GetSettingBool( const wchar_t *name, bool &bDef );

struct CSettingsLockRead
{
	CSettingsLockRead( void );
	~CSettingsLockRead( void );
};

struct CSettingsLockWrite
{
	CSettingsLockWrite( void );
	~CSettingsLockWrite( void );
};

bool HasHelp( void );
void ShowHelp( void );

#ifndef _WIN64
bool SaveAdmx( TSettingsComponent component, const char *admxFile, const char *admlFile, const char *docFile );
#endif

// Opens the registry keys for the settings of the given component
// Returns true if the user settings were read from the old HKCU location. That means the settings may need to be upgraded
bool OpenSettingsKeys( TSettingsComponent component, CRegKey &regSettings, CRegKey &regSettingsUser, CRegKey &regPolicy, CRegKey &regPolicyUser );

// Get a value from a manually loaded setting
bool GetSettingBool( const CSetting &setting );
CString GetSettingString( const CSetting &setting );

// the format for the location is key_name|value_name|file_name
void VLogToFile( const wchar_t *location, const wchar_t *message, va_list args );
void LogToFile( const wchar_t *location, const wchar_t *message, ... );
