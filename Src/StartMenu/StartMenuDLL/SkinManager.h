// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#pragma once

#include <vector>
#include <map>

const int MAX_SKIN_VERSION=3;

class CSkinParser;
enum TSkinOptionType;

struct MenuBitmap
{
	bool bIsBitmap;
	bool bIsOwned; // only valid if bIsBitmap and bitmap
	bool bIs32; // only valid if bIsBitmap and bitmap

	MenuBitmap( void ) { bIsBitmap=bIsOwned=bIs32=false; bitmap=NULL; }
	void Init( bool bIsColor=false );
	void Reset( bool bIsColor=false );

	HBITMAP GetBitmap( void ) const { return bIsBitmap?bitmap:NULL; }
	COLORREF GetColor( void ) const { return bIsBitmap?0:color; }

	void operator=( HBITMAP bmp ) { bIsBitmap=true; bitmap=bmp; }
	void operator=( COLORREF col ) { bIsBitmap=false; color=col; }

private:
	union
	{
		COLORREF color;
		HBITMAP bitmap;
	};
};

struct MenuSkin
{
	enum TSkinType
	{
		// the first 3 values match the MenuStyle setting
		SKIN_TYPE_CLASSIC1,
		SKIN_TYPE_CLASSIC2,
		SKIN_TYPE_WIN7,
		SKIN_TYPE_ALL_PROGRAMS,

		SKIN_TYPE_COUNT
	};

	unsigned int Hash;
	unsigned int MetroColorHash;
	int Dpi;
	CString About; // the text to show in the About box
	HICON AboutIcon; // the icon to show in the About box
	int Version; // 1 - skin 1.0 (default), 2 - skin 2.0 (future skins)
	TSkinType SkinType;
	bool ForceRTL;
	bool TwoColumns;
	bool Main_FakeGlass;
	bool Submenu_FakeGlass;
	bool ForceTouch;

	struct Variation
	{
		CString label;
		CString labelEn;
		CString tip;
	};
	std::vector<std::pair<int,Variation>> Variations;
	struct Option
	{
		// from skin
		CString name;
		TSkinOptionType type;
		CString label;
		CString tip;
		CString condition;
		int groupId;
		bool bDefValue; // default value when not set by user
		CString disValue; // the value when the setting is disabled

		// current state
		mutable bool bEnabled;
		mutable bool bValue;
		mutable CString sValue;
	};
	std::vector<Option> Options;

	enum TOpacity
	{
		OPACITY_SOLID,
		OPACITY_REGION,
		OPACITY_ALPHA,
		OPACITY_GLASS,
		OPACITY_FULLALPHA,
		OPACITY_FULLGLASS,
	};

	enum TShadow
	{
		SHADOW_ON,
		SHADOW_OFF,
		SHADOW_GLASS, // enabled when glass is available
	};

	enum
	{
		USER_CENTER=10000,
		USER_CENTER1=10001,
		USER_CENTER2=10002,
	};

	enum THAlign
	{
		HALIGN_CENTER,
		HALIGN_CENTER1,
		HALIGN_CENTER2,
		HALIGN_LEFT,
		HALIGN_LEFT1,
		HALIGN_LEFT2,
		HALIGN_RIGHT,
		HALIGN_RIGHT1,
		HALIGN_RIGHT2,
		HALIGN_CORNER,
		HALIGN_NONE,
	};

	enum TVAlign
	{
		VALIGN_CENTER,
		VALIGN_TOP,
		VALIGN_BOTTOM,
		VALIGN_CORNER,
		VALIGN_NONE,
	};

	enum TItemDrawType
	{
		COLUMN1_ITEM,
		COLUMN1_SPLIT,
		COLUMN1_NEW,
		COLUMN1_SEPARATOR,

		COLUMN2_ITEM,
		COLUMN2_SPLIT,
		COLUMN2_NEW,
		COLUMN2_SEPARATOR,
		COLUMN2_INLINE,

		SUBMENU_ITEM,
		SUBMENU_SPLIT,
		SUBMENU_NEW,
		SUBMENU_SEPARATOR,
		SUBMENU_SEPARATOR_SPLIT,

		// special items
		PROGRAMS_TREE_ITEM,
		PROGRAMS_TREE_NEW,
		PROGRAMS_BUTTON,
		PROGRAMS_BUTTON_NEW,
		PROGRAMS_CASCADING,
		PROGRAMS_CASCADING_NEW,
		SHUTDOWN_BUTTON,
		SHUTDOWN_BUTTON_SEARCH,
		SHUTDOWN_BUTTON_JUMP,
		LIST_ITEM,
		LIST_SPLIT,
		LIST_SEPARATOR,
		LIST_SEPARATOR_SPLIT,

		ITEM_TYPE_COUNT
	};

	enum TIconSize
	{
		ICON_SIZE_UNDEFINED=-1,
		ICON_SIZE_NONE,
		ICON_SIZE_SMALL,
		ICON_SIZE_LARGE,
		ICON_SIZE_PROGRAMS,
	};

	struct ItemDrawSettings
	{
		// text
		HFONT font;
		bool bOwnFont;
		int glowSize;
		COLORREF textColors[4]; // normal, selected, disabled, disabled+selected
		COLORREF textShadowColors[4];
		RECT textPadding;

		// selection
		MenuBitmap bmpSelection;
		int selSlicesX[6]; // 3 for normal, 6 for split selection
		int selSlicesY[3];

		// arrow
		MenuBitmap bmpArrow;
		SIZE arrSize;
		COLORREF arrColors[2]; // normal, selected
		SIZE arrPadding;

		// icon frame
		MenuBitmap bmpIconFrame;
		int frameSlicesX[3];
		int frameSlicesY[3];
		POINT iconFrameOffset;
		RECT iconPadding;

		// only used by separator items
		MenuBitmap bmpSeparator;
		int sepSlicesX[3];
		int sepHeight;

		// calculated
		TOpacity opacity;
		mutable int itemHeight;
		mutable int textTopOffset;
		mutable int iconTopOffset;
		TIconSize iconSize;
		TEXTMETRIC textMetrics;

		void Init( void );
		void Reset( void );
	};

	ItemDrawSettings ItemSettings[ITEM_TYPE_COUNT];

	// CAPTION SECTION - describes the caption portion of the main menu
	HFONT Caption_font;
	COLORREF Caption_text_color;
	COLORREF Caption_glow_color;
	int Caption_glow_size;
	RECT Caption_padding0;

	// MENU SECTION - describes the menu portion of the main menu
	MenuBitmap Main_bitmap;
	int Main_bitmap_slices_X[9];
	int Main_bitmap_slices_Y[6];
	TOpacity Main_opacity;
	TOpacity Main2_opacity;
	TOpacity Search_opacity;
	TOpacity Jumplist_opacity;
	TShadow Main_shadow;
	MenuBitmap Main_bitmap_search;
	int Main_bitmap_search_slices_X[9];
	int Main_bitmap_search_slices_Y[6];
	MenuBitmap Main_bitmap_jump;
	int Main_bitmap_jump_slices_X[9];
	int Main_bitmap_jump_slices_Y[6];

	RECT Shutdown_padding;

	bool bHasNewItem;
	bool Main_thin_frame;
	TIconSize Main_icon_size;
	TIconSize Main2_icon_size;
	COLORREF Main_background;
	COLORREF Main_background2;
	RECT Main_padding0;
	RECT Main2_padding0;
	RECT Main_search_padding0;
	RECT Main_jump_padding0;
	RECT Main2_search_padding0;
	RECT Main2_jump_padding0;
	int Main_search_indent;
	MenuBitmap Main_separatorV;
	int Main_separatorWidth;
	int Main_separator_slices_Y[3];
	RECT Search_padding;
	bool Search_frame;
	MenuBitmap Search_background;
	int Search_background_slices_X[9];
	int Search_background_slices_Y[9];
	RECT Search_background_padding;
	MenuBitmap Search_background_search;
	int Search_background_search_slices_X[9];
	int Search_background_search_slices_Y[9];
	RECT Search_background_search_padding;
	MenuBitmap Search_background_jump;
	int Search_background_jump_slices_X[9];
	int Search_background_jump_slices_Y[9];
	RECT Search_background_jump_padding;

	MenuBitmap Main_emblems[10];
	SIZE Main_emblem_sizes[10];
	RECT Main_emblem_paddings[10];
	THAlign Main_emblem_alignH1[10];
	THAlign Main_emblem_alignH2[10];
	TVAlign Main_emblem_alignV[10];
	MenuBitmap Main_emblem_mask;
	MenuBitmap Main_emblem_search_mask;
	MenuBitmap Main_emblem_jump_mask;
	MenuBitmap Search_emblem_mask;
	MenuBitmap Search_emblem_search_mask;
	MenuBitmap Search_emblem_jump_mask;

	MenuBitmap Patterns[4];
	SIZE Pattern_sizes[4];
	std::vector<unsigned int> PatternBits[4];
	int PatternPretileWidth;
	MenuBitmap Main_pattern_mask;
	MenuBitmap Main_pattern_search_mask;
	MenuBitmap Main_pattern_jump_mask;
	MenuBitmap Search_pattern_mask;
	MenuBitmap Search_pattern_search_mask;
	MenuBitmap Search_pattern_jump_mask;

	// SUB-MENU SECTION - describes the menu portion of the sub-menu
	MenuBitmap Submenu_bitmap;
	int Submenu_bitmap_slices_X[6];
	int Submenu_bitmap_slices_Y[3];
	TOpacity Submenu_opacity;
	TShadow Submenu_shadow;
	COLORREF Submenu_background;
	RECT Submenu_padding0;
	int Submenu_offset;
	int AllPrograms_offset;
	bool Submenu_thin_frame;
	MenuBitmap Submenu_separatorV;
	int Submenu_separatorWidth;
	int Submenu_separator_slices_Y[3];
	MenuBitmap Submenu_pager;
	int Submenu_pager_slices_X[3];
	int Submenu_pager_slices_Y[3];
	MenuBitmap Submenu_pager_arrows;
	SIZE Submenu_pager_arrow_Size;

	MenuBitmap Pin_bitmap;
	SIZE Pin_bitmap_Size;

	MenuBitmap More_bitmap;
	SIZE More_bitmap_Size;

	MenuBitmap Shutdown_bitmap;
	SIZE Shutdown_bitmap_Size;

	// SEARCH SECTION
	MenuBitmap Search_bitmap;
	MenuBitmap Search_arrow;
	SIZE Search_arrow_size;
	HFONT Search_hint_font;
	HFONT Search_underline_font;
	bool BOwnHintFont;
	COLORREF Search_text_colors[2];
	COLORREF Search_text_background;

	MenuBitmap Main_pager;
	int Main_pager_slices_X[3];
	int Main_pager_slices_Y[3];
	MenuBitmap Main_pager_arrows;
	SIZE Main_pager_arrow_Size;

	MenuBitmap User_bitmap;
	SIZE User_bitmapSize;
	MenuBitmap User_mask;
	SIZE User_maskSize;
	bool User_bitmap_outside;
	POINT User_frame_position;
	POINT User_image_offset;
	int User_image_size;
	int User_image_alpha;
	POINT User_image_padding; // top, bottom

	RECT User_name_position;
	THAlign User_name_align;
	HFONT User_font;
	COLORREF User_text_color;
	COLORREF User_glow_color;
	int User_glow_size;

	// PROGRAMS SECTION
	COLORREF Programs_background;
	int Programs_indent;
	MenuBitmap Programs_icon;
	SIZE Programs_icon_size;
	MenuBitmap Programs_icon_selected;

	// SCROLLBAR SECTION
	MenuBitmap Scrollbar_button;
	int Scrollbar_button_slices_X[3];
	int Scrollbar_button_slices_Y[3];
	MenuBitmap Scrollbar_arrows;
	SIZE Scrollbar_arrows_size;
	MenuBitmap Scrollbar_background;
	int Scrollbar_background_slices_X[3];
	int Scrollbar_background_slices_Y[3];
	MenuBitmap Scrollbar_thumb;
	int Scrollbar_thumb_slices_X[3];
	int Scrollbar_thumb_slices_Y[3];
	MenuBitmap Scrollbar_gripper;
	SIZE Scrollbar_gripper_size;
	bool BHasScrollbar;
	mutable bool BHasMetroColors;

	// calculated
	mutable RECT Caption_padding;
	mutable RECT Main_padding;
	mutable RECT Main2_padding;
	mutable RECT Main_search_padding;
	mutable RECT Main_jump_padding;
	mutable RECT Main2_search_padding;
	mutable RECT Main2_jump_padding;
	mutable bool bHasMain2_search_padding; // the skin defines Main2_search_padding (otherwise the old search-mode behavior is kept)
	mutable RECT Submenu_padding;

	MenuSkin( void );
	~MenuSkin( void );
	void Reset( void );

	static wchar_t s_SkinError[1024]; // parsing error. must end on \r\n

	bool LoadMenuSkin( const wchar_t *fname, const wchar_t *variation, const wchar_t *optionsStr, TSkinType skinType, unsigned int flags, int dpi );
	void LoadDefaultMenuSkin( TSkinType skinType, unsigned int flags, int dpi );
	const POINT *GetArrowsBitmapSizes( void ) const; // insert left, middle size, insert right, right arrow, left arrow, down arrow, total size
	void PretilePatterns( int width );
	void ParseOptionsString( const wchar_t *optionsStr, std::map<CString,CString> &options ) const;
	bool ComputeOptionStates( const std::map<CString,CString> &options, std::vector<const wchar_t*> &values, bool bTranslateValues ) const;
	void SerializeOptionStates( std::vector<wchar_t> &buffer ) const;

private:
	enum TSkinNumberType
	{
		NUMBERS_COLORS, // 0xBBGGRR
		NUMBERS_COLORS_ABGR, // 0xAABBGGRR
		NUMBERS_COLORS_RGB, // 0xRRGGBB
		NUMBERS_PADDING, // scaled by %
		NUMBERS_SIZE, // scaled by %
		NUMBERS_SLICES, // no modification
		NUMBERS_OTHER, // no modification
	};

	struct MetroColor
	{
		int colorType;
		COLORREF color;
	};

	struct CustomBitmap
	{
		HBITMAP bitmap;
		CString path;
		FILETIME timestamp;
	};

	mutable std::map<unsigned int,MetroColor> MetroColors;
	mutable std::vector<CustomBitmap> CustomBitmaps;

	COLORREF GetMetroColor( const wchar_t *name ) const;
	unsigned int CalcMetroColorHash( int set=-1 ) const;
	int LoadSkinNumbers( const wchar_t *str, int *numbers, int count, TSkinNumberType type ) const;
	HFONT LoadSkinFont( const wchar_t *str, const wchar_t *name, int weight, float size, bool bScale ) const;
	void LoadSkinTintColors( CSkinParser &parser, const wchar_t *name, COLORREF &tintColor1, COLORREF &tintColor2, COLORREF &tintColor3, COLORREF backgroundColor ) const;
	bool LoadSkinColors( CSkinParser &parser, const wchar_t *name, COLORREF *colors, int count, COLORREF backgroundColor, TSkinNumberType type=NUMBERS_COLORS ) const;
	bool LoadSkinBackground( HMODULE hMod, CSkinParser &parser, const wchar_t *name, MenuBitmap &bitmap, COLORREF bkColor, int *slicesX, int countX, int *slicesY, int countY, bool bMirror, bool bAllowColor=false, bool bPremultiply=true ) const;
	MenuBitmap LoadSkinBitmap( HMODULE hMod, int index, int maskIndex, COLORREF tintColor1, COLORREF tintColor2, COLORREF tintColor3, bool bPremultiply=true ) const;
	bool LoadSkinBitmap( HMODULE hMod, CSkinParser &parser, const wchar_t *name, MenuBitmap &bitmap, SIZE *size, bool bMirror, bool bPremultiply=true ) const;
	bool LoadSkinItem( HMODULE hMod, CSkinParser &parser, const wchar_t *name, MenuSkin::ItemDrawSettings &settings, MenuSkin::ItemDrawSettings *pDefaults, COLORREF background, bool bRTL ) const;
	bool LoadSkin( HMODULE hMod, const wchar_t *variation, const wchar_t *optionsStr, TSkinType skinType, unsigned int flags, int dpi );
	int ScaleSkinElement( int num, int scale=100 ) const;
	RECT ScaleSkinElement( const RECT &rect ) const;
	POINT ScaleSkinElement( const POINT &point ) const;
	SIZE ScaleSkinElement( const SIZE &size ) const;
	static THAlign ParseHAlign( const wchar_t *str );
	static TVAlign ParseVAlign( const wchar_t *str );
	static TIconSize ParseIconSize( const wchar_t *str );
};

enum
{
	LOADMENU_RESOURCES=1,
};

// Returns the path to the skin files. path must be _MAX_PATH characters
void GetSkinsPath( wchar_t *path );

// Returns the system glass color
void GetSystemGlassColor( int &dr, int &dg, int &db, int &da, int &dc );
void GetMetroGlassColor( int &dr, int &dg, int &db );

COLORREF GetMetroTaskbarColor( bool &bTransparent );
COLORREF GetSystemGlassColor8( void );
COLORREF GetSystemAccentColor( void );

extern DWORD g_CustomScrollbarThread;
extern HTHEME g_CustomScrollbarTheme;

void InitializeSkinManager( bool bInitIat );
void CloseSkinManager( bool bCloseIat );
