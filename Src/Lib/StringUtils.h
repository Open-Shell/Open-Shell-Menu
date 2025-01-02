// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

// StringUtils is also available under the CPOL license as part of the FormatString article
// on CodeProject: http://www.codeproject.com/KB/string/FormatString.aspx

#ifndef _STRINGUTILS_H
#define _STRINGUTILS_H

// StringUtils provides secure implementations of common string functions.
// All strings are guaranteed to be zero-terminated and to not contain partial
// double-byte characters or partial surrogate pairs.
// It also provides char and wchar_t overrides of the same functions.
//
///////////////////////////////////////////////////////////////////////////////

// Configuration of the string functions

// If defined, enables the use of WideCharToMultiByte and MultiByteToWideChar
// If not defined, wcstombs and mbstowcs are used instead. they always use the default code page
// and don't support surrogate pairs
#define STR_USE_WIN32_CONV

// If defined, enables the use of Windows localization functions - GetNumberFormat, GetTimeFormat, etc
// If not defined, then the numbers, currency, time and date use fixed formats
#define STR_USE_WIN32_NLS

// If defined, enables support for SYSTEMTIME, FILETIME and DATE
// If not defined, only time_t is supported as a time format
#define STR_USE_WIN32_TIME

// If defined, enables the use of IsDBCSLeadByte to support DBCS code pages
// If not defined, isleadbyte is used instead
#define STR_USE_WIN32_DBCS

// enables support for STL strings and streams
//#define STR_USE_STL

#include <string.h>
#include <stdarg.h>

#if defined(_WIN32) || defined(_WIN64)
#define VC_EXTRALEAN
#define NOMINMAX
#include <windows.h>
#endif

#ifdef STR_USE_STL
#include <string>
#endif

// _countof: compute the number of elements in a statically-allocated array
// VS2005 supports this, but earlier versions do not
#ifndef _countof
#define _countof(x) (sizeof(x)/sizeof((x)[0]))
#endif

// Returns 1 if the ANSI code page is single-byte, or 2 if it is double-byte (DBCS)
int GetMaxCharSize( void );

// Returns the length of a string
inline int Strlen( const char *str ) { return (int)strlen(str); }
inline int Strlen( const wchar_t *str ) { return (int)wcslen(str); }

// Copies src to dst. size is the size of dst in characters, including the terminating 0.
// Returns the number of characters copied, excluding the terminating 0.
// The return value is <=size-1. If size is 0, returns 0 and does nothing.
// The result in dst is always 0 terminated.
int Strcpy( char *dst, int size, const char *src );
int Strcpy( wchar_t *dst, int size, const wchar_t *src );

// Copies src to dst. size is the size of dst in characters, including the terminating 0.
// Copies up to len characters and always appends terminating 0.
int Strncpy( char *dst, int size, const char *src, int len );
int Strncpy( wchar_t *dst, int size, const wchar_t *src, int len );

// Appends src to dst. size is the size of dst in bytes, including the terminating 0.
// Returns the number of characters copied, excluding the terminating 0.
// The return value is <=size-1-strlen(dst). If size>=strlen(dst), returns 0 and does nothing.
// The result in dst is always 0 terminated.
int Strcat( char *dst, int size, const char *src );
int Strcat( wchar_t *dst, int size, const wchar_t *src );

// Writes formatted string to dst. size is the size of dst in characters, including the terminating 0.
// Returns the number of characters written, excluding the terminating 0.
// The return value is <=size-1. If size is 0, returns 0 and does nothing.
// The result in dst is always 0 terminated.
int _cdecl Sprintf( char *dst, int size, const char *format, ... );
int _cdecl Sprintf( wchar_t *dst, int size, const wchar_t *format, ... );
int _cdecl Vsprintf( char *dst, int size, const char *format, va_list args );
int _cdecl Vsprintf( wchar_t *dst, int size, const wchar_t *format, va_list args );

// Outputs a formatted debug string
void Trace( const char *format, ... );
void Trace( const wchar_t *format, ... );

// Converts between multi-byte and wide characters. size is the size of dst in characters, including the
// terminating 0.
// Returns the number of characters copied, excluding the terminating 0.
// The return value is <=size-1. If size is 0, returns 0 and does nothing.
// The result in dst is always 0 terminated.
// If dst is NULL the size is ignored and the function just returns the number of characters (not counting the 0)
#ifdef STR_USE_WIN32_CONV
int MbsToWcs( wchar_t *dst, int size, const char *src, int codePage=CP_ACP );
int WcsToMbs( char *dst, int size, const wchar_t *src, int codePage=CP_ACP );
#else
int MbsToWcs( wchar_t *dst, int size, const char *src );
int WcsToMbs( char *dst, int size, const wchar_t *src );
#endif

const char *GetToken( const char *text, char *token, int size, const char *separators );
const wchar_t *GetToken( const wchar_t *text, wchar_t *token, int size, const wchar_t *separators );

#endif
