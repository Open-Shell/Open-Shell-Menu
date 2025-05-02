// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently,
// but are changed infrequently

#pragma once

#include "targetver.h"

#define STRICT_TYPED_ITEMIDS

#define _ATL_APARTMENT_THREADED
#define _ATL_NO_AUTOMATIC_NAMESPACE
#define _ATL_MODULES // compatibility with /permissive-
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// some CString constructors will be explicit

#include "resource.h"
#include <atlbase.h>
#include <atlcom.h>
#include <atlctl.h>
#include <atlstr.h>

using namespace ATL;

#include <shobjidl.h>
#include <exdispid.h>
#include <shlguid.h>
#include <shlobj.h>

#include "StringUtils.h"
