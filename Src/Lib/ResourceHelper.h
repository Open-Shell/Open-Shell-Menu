// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#pragma once

#include <shobjidl.h>
#include <vector>

// Loads the best DLL for the given language
HINSTANCE LoadTranslationDll( const wchar_t *langSetting );

// Loads all strings and dialogs from hLngInstance
// pDialogs is a NULL-terminated list of dialog IDs. They are loaded from hLngInstance if possible, otherwise from g_Instance
void LoadTranslationResources( HINSTANCE hLngInstance, int *pDialogs );

// Returns a localized string
CString LoadStringEx( int stringID );
CString LoadStringEnglish( int stringID );

// Returns a localized dialog template
DLGTEMPLATE *LoadDialogEx( int dlgID );

// Loads an icon. path can be a path to .ico file, or in the format "module.dll, number"
HICON LoadIcon( int iconSize, const wchar_t *path, std::vector<HMODULE> &modules );

// Loads the icon for the given pidl (file or folder)
HICON LoadIcon( int iconSize, PIDLIST_ABSOLUTE pidl );

// Extracts icon of a given size from a specified location the way Shell does it
HICON ShExtractIcon( const wchar_t *path, int index, int iconSize );
HICON ShExtractIcon( const char *path, int index, int iconSize );

// Converts an icon to a bitmap. pBits may be NULL. If bDestroyIcon is true, hIcon will be destroyed
HBITMAP BitmapFromIcon( HICON hIcon, int iconSize, unsigned int **pBits, bool bDestroyIcon );

// Loads an image file into a bitmap and optionally resizes it
HBITMAP LoadImageFile( const wchar_t *path, const SIZE *pSize, bool bUseAlpha, bool bPremultiply, std::vector<unsigned int> *pButtonAnim, UINT dpi=0 );

// Loads a bitmap from a IMAGE resource
HBITMAP LoadImageResource( HMODULE hModule, const wchar_t *name, bool bTopDown, bool bPremultiply );

// Premultiplies a DIB section by the alpha channel and a given color
void PremultiplyBitmap( HBITMAP hBitmap, COLORREF rgb );

// Creates a grayscale version of an icon
HICON CreateDisabledIcon( HICON hIcon, int iconSize );

// Returns the version of a given module
DWORD GetVersionEx( HINSTANCE hInstance, DWORD *pBuild=NULL );

const int WIN_VER_VISTA=0x600;
const int WIN_VER_WIN7 =0x601;
const int WIN_VER_WIN8 =0x602;
const int WIN_VER_WIN81=0x603;
const int WIN_VER_WIN10=0xA00;

// Returns the Windows version - 0x600, 0x601, ...
WORD GetWinVersion( void );

// Returns true if the version is Win81 Update1
bool IsWin81Update1( void );

// Returns true if the version is Windows10 RS1 or later
bool IsWin10RS1( void );

// Returns true if the version is Windows10 RS4 (Spring Creator Update) or later
bool IsWin10RS4( void );

// Returns true if the version is Windows11 or later
bool IsWin11();

// Wrapper for IShellFolder::ParseDisplayName
HRESULT ShParseDisplayName( const wchar_t *pszName, PIDLIST_ABSOLUTE *ppidl, SFGAOF sfgaoIn, SFGAOF *psfgaoOut );

// Separates the arguments from the program
// May return NULL if no arguments are found
const wchar_t *SeparateArguments( const wchar_t *command, wchar_t *program );

// Replaces some common paths with environment variables
void UnExpandEnvStrings( const wchar_t *src, wchar_t *dst, int size );

void StringUpper( CString &str );

// Create a font from the user settings
HFONT CreateFontSetting( const wchar_t *fontStr, int dpi );

// Return DPI of given window (or system DPI on older systems)
UINT GetDpi(HWND hwnd = nullptr);

// Scale given value according to given DPI
int ScaleForDpi(UINT dpi, int value);
// Scale given value according to DPI of window
int ScaleForDpi(HWND hwnd, int value);

extern HINSTANCE g_Instance;

const int ANIM_BUTTON_TAG1='ANM';
const int ANIM_BUTTON_TAG2='BTN';
