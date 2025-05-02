// Classic Shell (c) 2009-2017, Ivo Beltchev
// Open-Shell (c) 2017-2018, The Open-Shell Team
// Confidential information of Ivo Beltchev. Not for disclosure or distribution without prior written consent from the author

#include <stdafx.h>
#include "resource.h"
#include "Settings.h"
#include "SettingsParser.h"
#include "SettingsUIHelper.h"
#include "ResourceHelper.h"
#include "StringUtils.h"
#include "FNVHash.h"
#include <Uxtheme.h>
#include <VSStyle.h>
#include <propkey.h>
#include <propvarutil.h>
#include <htmlhelp.h>
#include <vector>
#include <map>
#include <algorithm>

///////////////////////////////////////////////////////////////////////////////

// Read/Write lock for accessing the settings. Can't be acquired recursively. Only the main UI thread (the one displaying the settings UI)
// can write the settings, and because of that it shouldn't lock when reading the settings. The settings editing code shouldn't use
// GetSettings#### at all to avoid deadlocks
static SRWLOCK g_SettingsLock;

#ifdef _DEBUG
static _declspec(thread) int g_LockState; // 0 - none, 1 - read, 2 - write
static _declspec(thread) bool g_bUIThread; // set to true in the thread that edits the settings
#endif

CSettingsLockRead::CSettingsLockRead( void )
{
#ifdef _DEBUG
	Assert(g_LockState==0);
	g_LockState=1;
#endif
	AcquireSRWLockShared(&g_SettingsLock);
}

CSettingsLockRead::~CSettingsLockRead( void )
{
#ifdef _DEBUG
	Assert(g_LockState==1);
	g_LockState=0;
#endif
	ReleaseSRWLockShared(&g_SettingsLock);
}

CSettingsLockWrite::CSettingsLockWrite( void )
{
#ifdef _DEBUG
	Assert(g_LockState==0);
	g_LockState=2;
#endif
	AcquireSRWLockExclusive(&g_SettingsLock);
}

CSettingsLockWrite::~CSettingsLockWrite( void )
{
#ifdef _DEBUG
	Assert(g_LockState==2);
	g_LockState=0;
#endif
	ReleaseSRWLockExclusive(&g_SettingsLock);
}

static bool IsVariantTrue( const CComVariant &var )
{
	return (var.vt==VT_I4 && var.intVal==1);
}

bool CSetting::IsEnabled( void ) const
{
	if (IsLocked())
		return false;

	if (depend)
	{
		const wchar_t *name=depend;
		bool checkEnabled=(*name=='#');
		if (checkEnabled)
			name++;

		int len=Strlen(name);
		int val=0;
		wchar_t operation='~';
		const wchar_t operations[]=L"=~<>";
		for (const wchar_t *c=operations;*c;c++)
		{
			const wchar_t *p=wcschr(name,*c);
			if (p)
			{
				operation=*c;
				len=(int)(p-name);
				val=_wtol(p+1);
				break;
			}
		}
		int style, styleMask;
		GetSettingsStyle(style,styleMask);
		for (const CSetting *pSetting=GetAllSettings();pSetting->name;pSetting++)
		{
			if (_wcsnicmp(pSetting->name,name,len)==0 && pSetting->name[len]==0)
			{
				if ((pSetting->flags&styleMask) && !(pSetting->flags&style))
					return true; // if the master setting is not visible in this style, ignore the dependency
				if (checkEnabled && !pSetting->IsEnabled())
					return false;
				if ((pSetting->type==CSetting::TYPE_BOOL || pSetting->type==CSetting::TYPE_INT) && pSetting->GetValue().vt==VT_I4)
				{
					if (operation=='=' && pSetting->GetValue().intVal!=val)
						return false;
					if (operation=='~' && pSetting->GetValue().intVal==val)
						return false;
					if (operation=='<' && pSetting->GetValue().intVal>=val)
						return false;
					if (operation=='>' && pSetting->GetValue().intVal<=val)
						return false;
				}
				if ((pSetting->type==CSetting::TYPE_STRING || pSetting->type==CSetting::TYPE_BITMAP || pSetting->type==CSetting::TYPE_BITMAP_JPG || pSetting->type==CSetting::TYPE_DIRECTORY) && pSetting->GetValue().vt==VT_BSTR)
				{
					if (operation=='~' && *pSetting->GetValue().bstrVal==0)
						return false;
				}
				break;
			}
		}
	}
	return true;
}

bool CSetting::MatchFilter( const wchar_t *filter ) const
{
	if (!*filter) return true;
	CString string=LoadStringEx(nameID);
	StringUpper(string);
	if (wcsstr(string,filter)) return true;
	string=LoadStringEnglish(nameID);
	StringUpper(string);
	if (wcsstr(string,filter)) return true;
	return false;
}

bool CSetting::ShouldLoad( bool bShared ) const
{
	if (type==TYPE_GROUP || type==TYPE_RADIO || pLinkTo)
		return false;
	return (flags&FLAG_SHARED)?bShared:!bShared;
}

bool CSetting::ReadValue( CRegKey &regKey, const wchar_t *valName )
{
	// bool, int, hotkey, color
	if (type==CSetting::TYPE_BOOL || (type==CSetting::TYPE_INT && this[1].type!=CSetting::TYPE_RADIO) || type==CSetting::TYPE_HOTKEY || type==CSetting::TYPE_HOTKEY_ANY || type==CSetting::TYPE_COLOR)
	{
		DWORD val;
		if (regKey.QueryDWORDValue(valName,val)==ERROR_SUCCESS)
		{
			if (type==CSetting::TYPE_BOOL)
				value=CComVariant(val?1:0);
			else
				value=CComVariant((int)val);
			return true;
		}
		return false;
	}

	// radio
	if (type==CSetting::TYPE_INT && this[1].type==CSetting::TYPE_RADIO)
	{
		ULONG len;
		DWORD val;
		if (regKey.QueryStringValue(valName,NULL,&len)==ERROR_SUCCESS)
		{
			CString text;
			regKey.QueryStringValue(valName,text.GetBuffer(len),&len);
			text.ReleaseBuffer(len);
			val=0;
			for (const CSetting *pRadio=this+1;pRadio->type==CSetting::TYPE_RADIO;pRadio++,val++)
			{
				if (_wcsicmp(text,pRadio->name)==0)
				{
					value=CComVariant((int)val);
					return true;
				}
			}
		}
		else if (regKey.QueryDWORDValue(valName,val)==ERROR_SUCCESS)
		{
			value=CComVariant((int)val);
			return true;
		}
		return false;
	}

	// string
	if (type>=CSetting::TYPE_STRING && type!=CSetting::TYPE_MULTISTRING)
	{
		ULONG len;
		if (regKey.QueryStringValue(valName,NULL,&len)==ERROR_SUCCESS)
		{
			value.vt=VT_BSTR;
			value.bstrVal=SysAllocStringLen(NULL,len-1);
			regKey.QueryStringValue(valName,value.bstrVal,&len);
			return true;
		}
		return false;
	}

	// multistring
	if (type==CSetting::TYPE_MULTISTRING)
	{
		ULONG len;
		if (regKey.QueryMultiStringValue(valName,NULL,&len)==ERROR_SUCCESS)
		{
			value.vt=VT_BSTR;
			value.bstrVal=SysAllocStringLen(NULL,len-1);
			regKey.QueryMultiStringValue(valName,value.bstrVal,&len);
			for (int i=0;i<(int)len-1;i++)
				if (value.bstrVal[i]==0)
					value.bstrVal[i]='\n';
			return true;
		}
		else if (regKey.QueryStringValue(valName,NULL,&len)==ERROR_SUCCESS)
		{
			value.vt=VT_BSTR;
			value.bstrVal=SysAllocStringLen(NULL,len);
			regKey.QueryStringValue(valName,value.bstrVal,&len);
			if (len>0)
			{
				value.bstrVal[len-1]='\n';
				value.bstrVal[len]=0;
			}
			return true;
		}
		return false;
	}

	Assert(0);
	return false;
}

void CSetting::LoadValue( CRegKey &regSettings, CRegKey &regSettingsUser, CRegKey &regPolicy, CRegKey &regPolicyUser )
{
	if (!(flags&CSetting::FLAG_NODEFAULT))
		flags|=CSetting::FLAG_DEFAULT;
	flags&=~CSetting::FLAG_LOCKED_REG;
	value=defValue;
	flags&=~CSetting::FLAG_FORCED_DEFAULT;

	wchar_t name2[256];

	// load HKLM group policies
	if (regPolicy)
	{
		Sprintf(name2,_countof(name2),L"%s_State",name);
		DWORD val;
		if (regPolicy.QueryDWORDValue(name2,val)==ERROR_SUCCESS && val<=2)
		{
			if (ReadValue(regPolicy,name))
			{
				if (val==0) // locked to value
				{
					flags|=CSetting::FLAG_LOCKED_REG;
					flags&=~CSetting::FLAG_DEFAULT;
					return;
				}
				else if (val==1) // locked to default
				{
					flags|=CSetting::FLAG_LOCKED_REG;
					return;
				}
				else // change default
				{
					defValue=value;
					flags|=CSetting::FLAG_FORCED_DEFAULT;
				}
			}
		}
	}

	if (regSettings)
	{
		// load HKLM settings
		DWORD val;
		if (regSettings.QueryDWORDValue(name,val)==ERROR_SUCCESS && val==0xDEFA)
		{
			flags|=CSetting::FLAG_LOCKED_REG;
			return;
		}
		else if (ReadValue(regSettings,name))
		{
			flags|=CSetting::FLAG_LOCKED_REG;
			flags&=~CSetting::FLAG_DEFAULT;
			return;
		}
		else
		{
			// check if a default value is selected in HKLM
			Sprintf(name2,_countof(name2),L"%s_Default",name);
			if (ReadValue(regSettings,name2))
			{
				defValue=value;
				flags|=CSetting::FLAG_FORCED_DEFAULT;
			}
		}
	}

	// load HKCU group policies
	if (regPolicyUser)
	{
		Sprintf(name2,_countof(name2),L"%s_State",name);
		DWORD val;
		if (regPolicyUser.QueryDWORDValue(name2,val)==ERROR_SUCCESS && val<=2)
		{
			if (ReadValue(regPolicyUser,name))
			{
				if (val==0) // locked to value
				{
					flags|=CSetting::FLAG_LOCKED_REG;
					flags&=~CSetting::FLAG_DEFAULT;
					return;
				}
				else if (val==1) // locked to default
				{
					flags|=CSetting::FLAG_LOCKED_REG;
					return;
				}
				else // change default
				{
					defValue=value;
					flags|=CSetting::FLAG_FORCED_DEFAULT;
				}
			}
		}
	}

	// load HKCU settings
	if (regSettingsUser)
	{
		if (ReadValue(regSettingsUser,name))
		{
			flags&=~CSetting::FLAG_DEFAULT;
			return;
		}
	}
}

class CSettingsManager
{
public:
	CSettingsManager( void );
	~CSettingsManager( void );
	void Init( CSetting *pSettings, TSettingsComponent component, ICustomSettings *pCustom );

	bool GetSettingBool( const wchar_t *name ) const;
	bool GetSettingBool( const wchar_t *name, bool &bDef ) const;
	int GetSettingInt( const wchar_t *name ) const;
	int GetSettingInt( const wchar_t *name, bool &bDef ) const;
	CString GetSettingString( const wchar_t *name ) const;

	void SaveSettings( bool bShared );
	void LoadSettings( bool bShared );
	CString LoadSettingsXml( const wchar_t *fname );
	CString SaveSettingsXml( const wchar_t *fname );
	void ResetSettings( void );

	CSetting *GetSettings( void ) const { return m_pSettings; }
	ICustomSettings *GetCustom( void ) const { return m_pCustom; }
	bool SetSettingsStyle( int style, int mask ) { if (m_SettingsStyle==style && m_SettingsMask==mask) return false; m_SettingsStyle=style; m_SettingsMask=mask; return true; }
	void GetSettingsStyle( int &style, int &mask ) const { style=m_SettingsStyle; mask=m_SettingsMask; }
	HIMAGELIST GetImageList( HWND tree );
	void ResetImageList( void );
	const wchar_t *GetRegPath( void ) const { return m_RegPath; }
	const wchar_t *GetXMLName( void ) const { return m_XMLName; }

private:
	CSetting *m_pSettings;
	ICustomSettings *m_pCustom;
	int m_SettingsStyle;
	int m_SettingsMask;
	HIMAGELIST m_ImageList;
	const wchar_t *m_RegPath;
	const wchar_t *m_RegPathShared;
	const wchar_t *m_GpPath;
	const wchar_t *m_GpPathShared;
	const wchar_t *m_CompName;
	const wchar_t *m_XMLName;
};

static CSettingsManager g_SettingsManager;

CSettingsManager::CSettingsManager( void )
{
	m_pSettings=NULL;
	m_pCustom=NULL;
	m_SettingsStyle=m_SettingsMask=0;
	m_ImageList=NULL;
	m_RegPath=m_RegPathShared=m_GpPath=m_GpPathShared=m_CompName=m_XMLName=NULL;
}

static void GetRegPaths( TSettingsComponent component, const wchar_t *&regPath, const wchar_t *&gpPath )
{
	switch (component)
	{
		case COMPONENT_EXPLORER:
			regPath=L"Software\\OpenShell\\ClassicExplorer";
			gpPath=L"Software\\Policies\\OpenShell\\ClassicExplorer";
			break;
		case COMPONENT_MENU:
			regPath=L"Software\\OpenShell\\StartMenu";
			gpPath=L"Software\\Policies\\OpenShell\\StartMenu";
			break;
		case COMPONENT_IE:
			regPath=L"Software\\OpenShell\\ClassicIE";
			gpPath=L"Software\\Policies\\OpenShell\\ClassicIE";
			break;
		case COMPONENT_UPDATE:
			regPath=L"";
			gpPath=L"";
			break;
		case COMPONENT_SHARED:
			regPath=L"Software\\OpenShell\\OpenShell";
			gpPath=L"Software\\Policies\\OpenShell\\OpenShell";
			break;
	}
}

void CSettingsManager::Init( CSetting *pSettings, TSettingsComponent component, ICustomSettings *pCustom )
{
	GetRegPaths(component,m_RegPath,m_GpPath);
	GetRegPaths(COMPONENT_SHARED,m_RegPathShared,m_GpPathShared);
	switch (component)
	{
		case COMPONENT_EXPLORER:
			m_CompName=L"Explorer";
			m_XMLName=L"Explorer Settings.xml";
			break;
		case COMPONENT_MENU:
			m_CompName=L"StartMenu";
			m_XMLName=L"Menu Settings.xml";
			break;
		case COMPONENT_IE:
			m_CompName=L"IE";
			m_XMLName=L"IE Settings.xml";
			break;
		case COMPONENT_UPDATE:
			m_CompName=L"";
			m_XMLName=L"";
			break;
	}
	
	m_pSettings=pSettings;
	m_pCustom=pCustom;
	m_SettingsStyle=m_SettingsMask=0;
	InitializeSRWLock(&g_SettingsLock);
	CSettingsLockWrite lock;
	for (CSetting *pSetting=m_pSettings;pSetting->name;pSetting++)
	{
		if (pSetting->type<0) continue;
#ifdef _DEBUG
		if (pSetting->type==CSetting::TYPE_BOOL)
		{
			Assert(pSetting->defValue.vt==VT_I4 && (pSetting->defValue.intVal==0 || pSetting->defValue.intVal==1));
		}
		else if (pSetting->type==CSetting::TYPE_INT || pSetting->type==CSetting::TYPE_HOTKEY  || pSetting->type==CSetting::TYPE_HOTKEY_ANY || pSetting->type==CSetting::TYPE_COLOR)
		{
			Assert(pSetting->defValue.vt==VT_I4);
		}
		else if (pSetting->type>=CSetting::TYPE_STRING)
		{
			Assert(pSetting->defValue.vt==VT_BSTR);
		}
#endif
		pSetting->value=pSetting->defValue;
		if (!(pSetting->flags&CSetting::FLAG_NODEFAULT))
			pSetting->flags|=CSetting::FLAG_DEFAULT;
	}
	LoadSettings(false);
	LoadSettings(true);
	UpdateSettings();
	m_ImageList=NULL;
}

CSettingsManager::~CSettingsManager( void )
{
	ResetImageList();
}

bool CSettingsManager::GetSettingBool( const wchar_t *name ) const
{
	for (const CSetting *pSetting=m_pSettings;pSetting->name;pSetting++)
	{
		if (pSetting->type==CSetting::TYPE_BOOL && _wcsicmp(pSetting->name,name)==0)
		{
			Assert(!pSetting->pLinkTo);
			CSettingsLockRead lock;
			return IsVariantTrue(pSetting->value);
		}
	}
	Assert(0);
	return false;
}

bool CSettingsManager::GetSettingBool( const wchar_t *name, bool &bDef ) const
{
	for (const CSetting *pSetting=m_pSettings;pSetting->name;pSetting++)
	{
		if (pSetting->type==CSetting::TYPE_BOOL && _wcsicmp(pSetting->name,name)==0)
		{
			Assert(!pSetting->pLinkTo);
			CSettingsLockRead lock;
			bDef=pSetting->IsDefault() && !pSetting->IsForcedDefault();
			return IsVariantTrue(pSetting->value);
		}
	}
	Assert(0);
	bDef=false;
	return 0;
}

int CSettingsManager::GetSettingInt( const wchar_t *name ) const
{
	for (const CSetting *pSetting=m_pSettings;pSetting->name;pSetting++)
	{
		if ((pSetting->type==CSetting::TYPE_INT || pSetting->type==CSetting::TYPE_HOTKEY || pSetting->type==CSetting::TYPE_HOTKEY_ANY || pSetting->type==CSetting::TYPE_COLOR) && _wcsicmp(pSetting->name,name)==0)
		{
			Assert(!pSetting->pLinkTo);
			CSettingsLockRead lock;
			Assert(pSetting->value.vt==VT_I4);
			return pSetting->value.intVal;
		}
	}
	Assert(0);
	return 0;
}

int CSettingsManager::GetSettingInt( const wchar_t *name, bool &bDef ) const
{
	for (const CSetting *pSetting=m_pSettings;pSetting->name;pSetting++)
	{
		if ((pSetting->type==CSetting::TYPE_INT || pSetting->type==CSetting::TYPE_HOTKEY || pSetting->type==CSetting::TYPE_HOTKEY_ANY || pSetting->type==CSetting::TYPE_COLOR) && _wcsicmp(pSetting->name,name)==0)
		{
			Assert(!pSetting->pLinkTo);
			CSettingsLockRead lock;
			Assert(pSetting->value.vt==VT_I4);
			bDef=pSetting->IsDefault() && !pSetting->IsForcedDefault();
			return pSetting->value.intVal;
		}
	}
	Assert(0);
	bDef=false;
	return 0;
}

CString CSettingsManager::GetSettingString( const wchar_t *name ) const
{
	for (const CSetting *pSetting=m_pSettings;pSetting->name;pSetting++)
	{
		if (pSetting->type>=CSetting::TYPE_STRING && _wcsicmp(pSetting->name,name)==0)
		{
			Assert(!pSetting->pLinkTo);
			CSettingsLockRead lock;
			Assert(pSetting->value.vt==VT_BSTR);
			return pSetting->value.bstrVal;
		}
	}
	Assert(0);
	return CString();
}

static bool OpenSettingsKeys( const wchar_t *regPath, const wchar_t *gpPath, CRegKey &regSettings, CRegKey &regSettingsUser, CRegKey &regPolicy, CRegKey &regPolicyUser )
{
	bool bUpgrade=false;
	if (*regPath)
	{
		regSettings.Open(HKEY_LOCAL_MACHINE,regPath,KEY_READ|KEY_WOW64_64KEY);

		wchar_t regPathNew[_MAX_PATH];
		Sprintf(regPathNew,_countof(regPathNew),L"%s\\Settings",regPath);
		if (regSettingsUser.Open(HKEY_CURRENT_USER,regPathNew,KEY_READ|KEY_WOW64_64KEY)!=ERROR_SUCCESS)
		{
			if (regSettingsUser.Open(HKEY_CURRENT_USER,regPath,KEY_READ|KEY_WOW64_64KEY)==ERROR_SUCCESS)
				bUpgrade=true;
		}
	}

	regPolicy.Open(HKEY_LOCAL_MACHINE,gpPath,KEY_READ|KEY_WOW64_64KEY);
	regPolicyUser.Open(HKEY_CURRENT_USER,gpPath,KEY_READ|KEY_WOW64_64KEY);
	return bUpgrade;
}

void CSettingsManager::LoadSettings( bool bShared )
{
	Assert(g_LockState==2);

	// load settings from registry
	CRegKey regSettings, regSettingsUser, regPolicy, regPolicyUser;
	bool bUpgrade=OpenSettingsKeys(bShared?m_RegPathShared:m_RegPath, bShared?m_GpPathShared:m_GpPath, regSettings, regSettingsUser, regPolicy, regPolicyUser);

	for (CSetting *pSetting=m_pSettings;pSetting->name;pSetting++)
	{
		if (pSetting->ShouldLoad(bShared))
			pSetting->LoadValue(regSettings,regSettingsUser,regPolicy,regPolicyUser);
	}
	if (bUpgrade)
		UpgradeSettings(bShared);
}

void CSettingsManager::SaveSettings( bool bShared )
{
	// doesn't need to acquire the lock because it can only run from the UI editing code
	Assert(g_bUIThread);

	const wchar_t *regPath=bShared?m_RegPathShared:m_RegPath;
	if (!*regPath) return;
	wchar_t regPathNew[_MAX_PATH];
	Sprintf(regPathNew,_countof(regPathNew),L"%s\\Settings",regPath);

	// save non-default to HKCU
	CRegKey regSettings;
	if (regSettings.Open(HKEY_CURRENT_USER,regPathNew)!=ERROR_SUCCESS)
		regSettings.Create(HKEY_CURRENT_USER,regPathNew);

	if (!bShared)
	{
		DWORD version=GetVersionEx(g_Instance);
		regSettings.SetDWORDValue(L"Version",version);
	}
	for (const CSetting *pSetting=m_pSettings;pSetting->name;pSetting++)
	{
		if (pSetting->type==CSetting::TYPE_GROUP || pSetting->type==CSetting::TYPE_RADIO || pSetting->pLinkTo)
			continue;
		if (pSetting->flags&(CSetting::FLAG_LOCKED_REG|CSetting::FLAG_NOSAVE))
			continue;
		if (pSetting->flags&CSetting::FLAG_SHARED)
		{
			if (!bShared) continue;
		}
		else
		{
			if (bShared) continue;
		}
		if (pSetting->flags&CSetting::FLAG_DEFAULT)
		{
			regSettings.DeleteValue(pSetting->name);
			continue;
		}
		if (pSetting->type==CSetting::TYPE_BOOL || (pSetting->type==CSetting::TYPE_INT && pSetting[1].type!=CSetting::TYPE_RADIO) || pSetting->type==CSetting::TYPE_HOTKEY || pSetting->type==CSetting::TYPE_HOTKEY_ANY || pSetting->type==CSetting::TYPE_COLOR)
		{
			DWORD val=0;
			if (pSetting->value.vt==VT_I4)
				val=pSetting->value.intVal;
			regSettings.SetDWORDValue(pSetting->name,val);
		}
		if (pSetting->type==CSetting::TYPE_INT && pSetting[1].type==CSetting::TYPE_RADIO)
		{
			DWORD val=0;
			if (pSetting->value.vt==VT_I4)
				val=pSetting->value.intVal;
			for (const CSetting *pRadio=pSetting+1;pRadio->type==CSetting::TYPE_RADIO;pRadio++,val--)
			{
				if (val==0)
				{
					regSettings.SetStringValue(pSetting->name,pRadio->name);
					break;
				}
			}
		}
		if (pSetting->type==CSetting::TYPE_MULTISTRING)
		{
			if (pSetting->value.vt==VT_BSTR)
			{
				int len=Strlen(pSetting->value.bstrVal);
				for (int i=0;i<len;i++)
					if (pSetting->value.bstrVal[i]=='\n')
						pSetting->value.bstrVal[i]=0;
				regSettings.SetMultiStringValue(pSetting->name,pSetting->value.bstrVal);
				for (int i=0;i<len;i++)
					if (pSetting->value.bstrVal[i]==0)
						pSetting->value.bstrVal[i]='\n';
			}
			else
				regSettings.SetMultiStringValue(pSetting->name,L"\0");
		}
		else if (pSetting->type>=CSetting::TYPE_STRING)
		{
			if (pSetting->value.vt==VT_BSTR)
				regSettings.SetStringValue(pSetting->name,pSetting->value.bstrVal);
			else
				regSettings.SetStringValue(pSetting->name,L"");
		}
	}
}

static CComBSTR g_bstrValue(L"value");
static CComBSTR g_bstrTab(L"\n\t");

CString CSettingsManager::LoadSettingsXml( const wchar_t *fname )
{
	CSettingsLockWrite lock;
	CComPtr<IXMLDOMDocument> pDoc;
	if (FAILED(pDoc.CoCreateInstance(L"Msxml2.FreeThreadedDOMDocument")))
		return L"Failed to initialize XML parser";
	pDoc->put_async(VARIANT_FALSE);
	VARIANT_BOOL loaded;
	if (pDoc->load(CComVariant(fname),&loaded)!=S_OK || loaded!=VARIANT_TRUE)
	{
		CComPtr<IXMLDOMParseError> pError;
		pDoc->get_parseError(&pError);
		CComBSTR reason;
		if (pError && SUCCEEDED(pError->get_reason(&reason)))
		{
			return CString(L"XML parsing error: ")+reason;
		}
		return CString(L"XML parsing error");
	}

	CComPtr<IXMLDOMNode> node;
	HRESULT res=pDoc->selectSingleNode(CComBSTR(L"Settings"),&node);
	if (res!=S_OK)
		return CString(L"XML parsing error: The tag 'Settings' is missing.");

	DWORD ver=0;
	{
		CComVariant value;
		CComQIPtr<IXMLDOMElement> element(node);
		if (!element || element->getAttribute(CComBSTR(L"component"),&value)!=S_OK || value.vt!=VT_BSTR)
			return CString(L"XML parsing error: The tag 'Settings' is missing the 'component' attribute.");
		if (_wcsicmp(value.bstrVal,m_CompName)!=0)
		{
			CString error;
			error.Format(L"XML parsing error: This settings file is intended for another component '%s'.",value.bstrVal);
			return error;
		}

		value.Clear();
		if (element && element->getAttribute(CComBSTR(L"version"),&value)==S_OK && value.vt==VT_BSTR)
		{
			wchar_t token[10];
			const wchar_t *str=GetToken(value.bstrVal,token,_countof(token),L".");
			ver=(_wtol(token)&0xFF)<<24;
			str=GetToken(str,token,_countof(token),L".");
			ver|=(_wtol(token)&0xFF)<<16;
			ver|=_wtol(str)&0xFFFF;
		}
	}

	ResetSettings();
	CComPtr<IXMLDOMNode> child;
	node->get_firstChild(&child);
	while (child)
	{
		CComBSTR name;
		child->get_nodeName(&name);

		for (CSetting *pSetting=g_SettingsManager.GetSettings();pSetting->name;pSetting++)
		{
			if (pSetting->type==CSetting::TYPE_GROUP || pSetting->type==CSetting::TYPE_RADIO || pSetting->pLinkTo)
				continue;
			if (pSetting->type>=0 && _wcsicmp(pSetting->name,name)==0)
			{
				if (pSetting->flags&(CSetting::FLAG_LOCKED_REG|CSetting::FLAG_SHARED))
					break;
				if (pSetting->type==CSetting::TYPE_MULTISTRING)
				{
					// load Lines
					CComPtr<IXMLDOMNode> child2;
					child->get_firstChild(&child2);
					std::vector<wchar_t> string;
					while (child2)
					{
						CComBSTR text;
						if (child2->get_text(&text)==S_OK)
						{
							int len=(int)text.Length();
							int pos=(int)string.size();
							string.resize(pos+len+1);
							memcpy(&string[pos],(const wchar_t*)text,len*2);
							string[pos+len]='\n';
						}
						CComPtr<IXMLDOMNode> next;
						child2->get_nextSibling(&next);
						child2=std::move(next);
					}
					string.push_back(0);
					pSetting->value=CComVariant(&string[0]);
					pSetting->flags&=~CSetting::FLAG_DEFAULT;
				}
				else
				{
					CComQIPtr<IXMLDOMElement> element(child);
					if (element)
					{
						CComVariant value;
						if (element->getAttribute(g_bstrValue,&value)==S_OK && value.vt==VT_BSTR)
						{
							if (pSetting->type>=CSetting::TYPE_STRING)
							{
								pSetting->value=value;
								pSetting->flags&=~CSetting::FLAG_DEFAULT;
							}
							else if (pSetting->type==CSetting::TYPE_BOOL || (pSetting->type==CSetting::TYPE_INT && pSetting[1].type!=CSetting::TYPE_RADIO) || pSetting->type==CSetting::TYPE_HOTKEY || pSetting->type==CSetting::TYPE_HOTKEY_ANY || pSetting->type==CSetting::TYPE_COLOR)
							{
								int val=_wtol(value.bstrVal);
								if (pSetting->type==CSetting::TYPE_BOOL)
									pSetting->value=CComVariant(val?1:0);
								else
									pSetting->value=CComVariant(val);
								pSetting->flags&=~CSetting::FLAG_DEFAULT;
							}
							else if (pSetting->type==CSetting::TYPE_INT && pSetting[1].type==CSetting::TYPE_RADIO)
							{
								int val=0;
								for (CSetting *pRadio=pSetting+1;pRadio->type==CSetting::TYPE_RADIO;pRadio++,val++)
								{
									if (_wcsicmp(pRadio->name,value.bstrVal)==0)
									{
										pSetting->value=CComVariant(val);
										pSetting->flags&=~CSetting::FLAG_DEFAULT;
										break;
									}
								}
							}
						}
					}
				}
			}
		}

		CComPtr<IXMLDOMNode> next;
		if (child->get_nextSibling(&next)!=S_OK)
			break;
		child=std::move(next);
	}
	if (ver<0x03090000)
		UpgradeSettings(false);
	UpdateSettings();
	return CString();
}

static void SaveSettingValue( IXMLDOMDocument *pDoc, IXMLDOMNode *pParent, const wchar_t *name, const CComVariant &value )
{
	CComPtr<IXMLDOMElement> setting;
	pDoc->createElement(CComBSTR(name),&setting);
	setting->setAttribute(g_bstrValue,value);
	CComPtr<IXMLDOMText> text;
	CComPtr<IXMLDOMNode> nu;
	pDoc->createTextNode(g_bstrTab,&text);
	pParent->appendChild(text,&nu);
	nu=NULL;
	pParent->appendChild(setting,&nu);
}

CString CSettingsManager::SaveSettingsXml( const wchar_t *fname )
{
	// doesn't need to acquire the lock because it can only run from the UI editing code
	Assert(g_bUIThread);

	CComPtr<IXMLDOMDocument> pDoc;
	HRESULT res=pDoc.CoCreateInstance(L"Msxml2.FreeThreadedDOMDocument");
	if (FAILED(res))
		return L"Failed to initialize XML parser";

	CComPtr<IXMLDOMElement> pRoot;
	pDoc->createElement(CComBSTR(L"Settings"),&pRoot);
	CComPtr<IXMLDOMProcessingInstruction> pi;
	if (SUCCEEDED(pDoc->createProcessingInstruction(CComBSTR(L"xml"),CComBSTR(L"version=\"1.0\""),&pi)))
	{
		CComPtr<IXMLDOMNode> nu;
		pDoc->appendChild(pi,&nu);
	}

	{
		CComPtr<IXMLDOMNode> nu;
		pDoc->appendChild(pRoot,&nu);
	}

	pRoot->setAttribute(CComBSTR(L"component"),CComVariant(m_CompName));

	wchar_t version[100];
	DWORD ver=GetVersionEx(g_Instance);
	Sprintf(version,_countof(version),L"%d.%d.%d",ver>>24,(ver>>16)&0xFF,ver&0xFFFF);
	pRoot->setAttribute(CComBSTR(L"version"),CComVariant(version));

	for (const CSetting *pSetting=m_pSettings;pSetting->name;pSetting++)
	{
		if (pSetting->type==CSetting::TYPE_GROUP || pSetting->type==CSetting::TYPE_RADIO || pSetting->pLinkTo)
			continue;
		if (pSetting->flags&(CSetting::FLAG_LOCKED_REG|CSetting::FLAG_NOSAVE|CSetting::FLAG_DEFAULT|CSetting::FLAG_SHARED))
			continue;
		if (pSetting->type==CSetting::TYPE_MULTISTRING)
		{
			CComPtr<IXMLDOMElement> setting;
			pDoc->createElement(CComBSTR(pSetting->name),&setting);
			CComPtr<IXMLDOMText> text;
			CComPtr<IXMLDOMNode> nu;
			pDoc->createTextNode(g_bstrTab,&text);
			pRoot->appendChild(text,&nu);
			nu=NULL;
			pRoot->appendChild(setting,&nu);
			CComBSTR tabs(L"\n\t\t");
			CComBSTR name(L"Line");
			if (pSetting->value.vt==VT_BSTR)
			{
				for (const wchar_t *str=pSetting->value.bstrVal;*str;)
				{
					int len;
					const wchar_t *end=wcschr(str,'\n');
					if (end)
						len=(int)(end-str);
					else
						len=Strlen(str);
					CComPtr<IXMLDOMElement> line;
					pDoc->createElement(name,&line);
					line->put_text(CComBSTR(len,str));
					nu=NULL;
					text=NULL;
					pDoc->createTextNode(tabs,&text);
					setting->appendChild(text,&nu);
					nu=NULL;
					setting->appendChild(line,&nu);
					if (!end) break;
					str=end+1;
				}
			}
			nu=NULL;
			text=NULL;
			pDoc->createTextNode(g_bstrTab,&text);
			setting->appendChild(text,&nu);
			continue;
		}
		else if (pSetting->type==CSetting::TYPE_BOOL || (pSetting->type==CSetting::TYPE_INT && pSetting[1].type!=CSetting::TYPE_RADIO) || pSetting->type>=CSetting::TYPE_HOTKEY || pSetting->type>=CSetting::TYPE_HOTKEY_ANY || pSetting->type>=CSetting::TYPE_STRING)
		{
			SaveSettingValue(pDoc,pRoot,pSetting->name,pSetting->value);
		}
		else if (pSetting->type==CSetting::TYPE_INT && pSetting[1].type==CSetting::TYPE_RADIO)
		{
			DWORD val=0;
			if (pSetting->value.vt==VT_I4)
				val=pSetting->value.intVal;
			for (const CSetting *pRadio=pSetting+1;pRadio->type==CSetting::TYPE_RADIO;pRadio++,val--)
			{
				if (val==0)
				{
					SaveSettingValue(pDoc,pRoot,pSetting->name,CComVariant(pRadio->name));
					break;
				}
			}
		}
	}
	CComPtr<IXMLDOMText> text;
	CComPtr<IXMLDOMNode> nu;
	pDoc->createTextNode(CComBSTR(L"\n"),&text);
	pRoot->appendChild(text,&nu);
	if (FAILED(pDoc->save(CComVariant(fname))))
		return CString(L"Failed to save XML file ")+fname;
	return CString();
}

void CSettingsManager::ResetSettings( void )
{
	Assert(g_LockState==2); // must be locked for writing
	for (CSetting *pSetting=m_pSettings;pSetting->name;pSetting++)
	{
		if (pSetting->type==CSetting::TYPE_GROUP || pSetting->type==CSetting::TYPE_RADIO || pSetting->pLinkTo)
			continue;
		if (pSetting->flags&(CSetting::FLAG_LOCKED_REG|CSetting::FLAG_NORESET))
			continue;
		if (!(pSetting->flags&CSetting::FLAG_NODEFAULT))
			pSetting->flags|=CSetting::FLAG_DEFAULT;
		pSetting->value=pSetting->defValue;
	}
}

HIMAGELIST CSettingsManager::GetImageList( HWND tree )
{
	if (m_ImageList) return m_ImageList;
	HTHEME theme=OpenThemeData(GetParent(tree),L"button");
	HDC hdc=CreateCompatibleDC(NULL);
	int iconSize=(TreeView_GetItemHeight(tree)<32)?16:32;
	int checkSize=16;
	if (theme)
	{
		SIZE val={16,16};
		if (GetThemePartSize(theme,hdc,BP_RADIOBUTTON,RBS_UNCHECKEDNORMAL,NULL,TS_DRAW,&val)==S_OK)
			checkSize=val.cx;
	}
	int imageSize=iconSize>checkSize?iconSize:checkSize;
	int iconOffset=(imageSize-iconSize)/2;
	int checkOffset=(imageSize-checkSize)/2;
	m_ImageList=ImageList_Create(imageSize,imageSize,ILC_COLOR32|ILC_MASK|((GetWindowLong(tree,GWL_EXSTYLE)&WS_EX_LAYOUTRTL)?ILC_MIRROR:0),0,23);
	BITMAPINFO dib={sizeof(dib)};
	dib.bmiHeader.biWidth=imageSize;
	dib.bmiHeader.biHeight=-imageSize;
	dib.bmiHeader.biPlanes=1;
	dib.bmiHeader.biBitCount=32;
	dib.bmiHeader.biCompression=BI_RGB;
	HDC hdcMask=CreateCompatibleDC(NULL);
	HBITMAP bmp=CreateDIBSection(hdc,&dib,DIB_RGB_COLORS,NULL,NULL,0);
	HBITMAP bmpMask=CreateDIBSection(hdcMask,&dib,DIB_RGB_COLORS,NULL,NULL,0);

	for (int i=0;i<13;i++)
	{
		HGDIOBJ bmp0=SelectObject(hdc,bmp);
		HGDIOBJ bmp1=SelectObject(hdcMask,bmpMask);
		RECT rc={0,0,imageSize,imageSize};
		FillRect(hdc,&rc,(HBRUSH)(COLOR_WINDOW+1));
		FillRect(hdcMask,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
		if (i==1)
		{
			HICON icon=(HICON)LoadImage(_AtlBaseModule.GetResourceInstance(),MAKEINTRESOURCE(IDI_ICONLOCK),IMAGE_ICON,iconSize,iconSize,LR_DEFAULTCOLOR);
			DrawIconEx(hdc,iconOffset,iconOffset,icon,iconSize,iconSize,0,NULL,DI_NORMAL);
			DrawIconEx(hdcMask,iconOffset,iconOffset,icon,iconSize,iconSize,0,NULL,DI_MASK);
			DestroyIcon(icon);
		}
		else if (i==2 || i==3)
		{
			HMODULE hShell32=GetModuleHandle(L"shell32.dll");
			if (hShell32)
			{
				HICON icon=(HICON)LoadImage(hShell32,MAKEINTRESOURCE(151),IMAGE_ICON,iconSize,iconSize,LR_DEFAULTCOLOR);
				DrawIconEx(hdc,iconOffset,iconOffset,icon,iconSize,iconSize,0,NULL,DI_NORMAL);
				DestroyIcon(icon);
			}
		}
		else if (i==12)
		{
			HICON icon=(HICON)LoadImage(_AtlBaseModule.GetResourceInstance(),MAKEINTRESOURCE(IDI_ICONWARNING),IMAGE_ICON,iconSize,iconSize,LR_DEFAULTCOLOR);
			DrawIconEx(hdc,iconOffset,iconOffset,icon,iconSize,iconSize,0,NULL,DI_NORMAL);
			DrawIconEx(hdcMask,iconOffset,iconOffset,icon,iconSize,iconSize,0,NULL,DI_MASK);
			DestroyIcon(icon);
		}
		else if (i>3)
		{
			RECT rcCheck={checkOffset,checkOffset,checkOffset+checkSize,checkOffset+checkSize};
			if (theme)
			{
				if ((i-4)&4)
				{
					int state=(i-4)&3;
					if (state==0) state=RBS_UNCHECKEDNORMAL;
					else if (state==1) state=RBS_UNCHECKEDDISABLED;
					else if (state==2) state=RBS_CHECKEDNORMAL;
					else state=RBS_CHECKEDDISABLED;
					DrawThemeBackground(theme,hdc,BP_RADIOBUTTON,state,&rcCheck,NULL);
				}
				else
				{
					int state=(i-4)&3;
					if (state==0) state=CBS_UNCHECKEDNORMAL;
					else if (state==1) state=CBS_UNCHECKEDDISABLED;
					else if (state==2) state=CBS_CHECKEDNORMAL;
					else state=CBS_CHECKEDDISABLED;
					DrawThemeBackground(theme,hdc,BP_CHECKBOX,state,&rcCheck,NULL);
				}
			}
			else
			{
				UINT state=DFCS_BUTTONCHECK|DFCS_FLAT;
				if ((i-4)&1) state|=DFCS_INACTIVE;
				if ((i-4)&2) state|=DFCS_CHECKED;
				if ((i-4)&4) state|=DFCS_BUTTONRADIO;
				DrawFrameControl(hdc,&rcCheck,DFC_BUTTON,state);
			}
		}
		SelectObject(hdc,bmp0);
		SelectObject(hdcMask,bmp1);
		ImageList_Add(m_ImageList,bmp,bmpMask);
	}

	// create color images
	{
		HGDIOBJ bmp0=SelectObject(hdc,bmp);
		HGDIOBJ bmp1=SelectObject(hdcMask,bmpMask);
		RECT rc={0,0,imageSize,imageSize};
		FillRect(hdc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
		FillRect(hdcMask,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
		SelectObject(hdc,bmp0);
		SelectObject(hdcMask,bmp1);

		for (int i=0;i<10;i++)
			ImageList_Add(m_ImageList,bmp,bmpMask);
	}

	DeleteObject(bmp);
	DeleteObject(bmpMask);
	DeleteDC(hdc);
	DeleteDC(hdcMask);

	if (theme) CloseThemeData(theme);
	ImageList_SetOverlayImage(m_ImageList,1,1);
	ImageList_SetOverlayImage(m_ImageList,12,2);
	return m_ImageList;
}

void CSettingsManager::ResetImageList( void )
{
	if (m_ImageList) ImageList_Destroy(m_ImageList);
	m_ImageList=NULL;
}

///////////////////////////////////////////////////////////////////////////////

class CSettingsDlg: public CResizeableDlg<CSettingsDlg>
{
public:
	CSettingsDlg( void );
	void Init( CSetting *pSettings, ICustomSettings *pCustom, int tab, const wchar_t* appId );

	BEGIN_MSG_MAP( CSettingsDlg )
		MESSAGE_HANDLER( WM_INITDIALOG, OnInitDialog )
		MESSAGE_HANDLER( WM_DESTROY, OnDestroy )
		MESSAGE_HANDLER( WM_SIZE, OnSize )
		MESSAGE_HANDLER( WM_GETMINMAXINFO, OnGetMinMaxInfo )
		MESSAGE_HANDLER( WM_KEYDOWN, OnKeyDown )
		MESSAGE_HANDLER( WM_SYSCOMMAND, OnSysCommand )
		MESSAGE_HANDLER( WM_CLEAR, OnResetUI )
		COMMAND_HANDLER( IDOK, BN_CLICKED, OnOK )
		COMMAND_HANDLER( IDCANCEL, BN_CLICKED, OnCancel )
		COMMAND_HANDLER( IDC_BUTTONBACKUP, BN_CLICKED, OnBackup )
		COMMAND_HANDLER( IDC_CHECKALL, BN_CLICKED, OnCheckAll )
		COMMAND_HANDLER( IDC_EDITSEARCH, EN_CHANGE, OnSearchChange )
		NOTIFY_HANDLER( IDC_TABSETTINGS, TCN_SELCHANGING, OnSelChanging )
		NOTIFY_HANDLER( IDC_TABSETTINGS, TCN_SELCHANGE, OnSelChange )
		NOTIFY_HANDLER( IDC_BUTTONBACKUP, BCN_DROPDOWN, OnDropDown )
		NOTIFY_HANDLER( IDC_LINKHELP, NM_CLICK, OnHelp )
		NOTIFY_HANDLER( IDC_LINKHELP, NM_RETURN, OnHelp )
		NOTIFY_HANDLER( IDC_LINKWEB, NM_CLICK, OnWeb )
		NOTIFY_HANDLER( IDC_LINKWEB, NM_RETURN, OnWeb )
		NOTIFY_HANDLER( IDC_SYSLINKLOC, NM_CLICK, OnLink )
		NOTIFY_HANDLER( IDC_SYSLINKLOC, NM_RETURN, OnLink )
	END_MSG_MAP()

	BEGIN_RESIZE_MAP
		RESIZE_CONTROL(IDC_TABSETTINGS,MOVE_SIZE_X|MOVE_SIZE_Y)
		RESIZE_CONTROL(IDC_LINKHELP,MOVE_MOVE_X)
		RESIZE_CONTROL(IDC_LINKWEB,MOVE_MOVE_Y)
		RESIZE_CONTROL(IDC_SYSLINKLOC,MOVE_SIZE_X|MOVE_MOVE_Y)
		RESIZE_CONTROL(IDC_BUTTONBACKUP,MOVE_MOVE_X|MOVE_MOVE_Y)
		RESIZE_CONTROL(IDOK,MOVE_MOVE_X|MOVE_MOVE_Y)
		RESIZE_CONTROL(IDCANCEL,MOVE_MOVE_X|MOVE_MOVE_Y)
	END_RESIZE_MAP

	bool GetOnTop( void ) const { return m_bOnTop; }
	void SetDirty( void ) { m_bDirty=true; }

protected:
	// Handler prototypes:
	//  LRESULT MessageHandler(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	//  LRESULT CommandHandler(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	//  LRESULT NotifyHandler(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
	LRESULT OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnKeyDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnSysCommand( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnResetUI( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnCancel( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnBackup( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnCheckAll( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnSearchChange( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled );
	LRESULT OnSelChanging( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnSelChange( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnDropDown( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnHelp( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnWeb( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );
	LRESULT OnLink( int idCtrl, LPNMHDR pnmh, BOOL& bHandled );

private:
	CSetting *m_pSettings;
	ICustomSettings *m_pCustom;
	CWindow m_Tabs;
	int m_Index;
	HWND m_Panel;
	int m_InitialTab;
	bool m_bBasic;
	bool m_bOnTop;
	bool m_bIgnoreEdit;
	bool m_bDirty;
	CString m_FilterText;
	const wchar_t* m_AppId;

	void AddTabs( int name, const CSetting *pSelect=NULL );
	void SetCurTab( int index, bool bReset, const CSetting *pSelect=NULL );
	bool IsTabValid( void );
	void StorePlacement( void );

	struct Placement
	{
		RECT rc;
		unsigned int tab;
		bool basic;
		bool top;
		BOOL maximized;
	};

	bool IsVisible( const CSetting *pSetting ) const;

	friend void SetSettingsStyle( int style, int mask );
	friend void SelectSettingsTab( int tab, bool bAdvanced, const CSetting *pSelect );
};

CSettingsDlg::CSettingsDlg( void )
{
	m_pSettings=NULL;
	m_pCustom=NULL;
	m_Index=0;
	m_Panel=NULL;
	m_InitialTab=0;
	m_bBasic=false;
	m_bOnTop=false;
	m_bIgnoreEdit=false;
	m_bDirty=false;
	m_AppId=NULL;
}

void CSettingsDlg::Init( CSetting *pSettings, ICustomSettings *pCustom, int tab, const wchar_t* appId )
{
	m_pSettings=pSettings;
	m_pCustom=pCustom;
	m_InitialTab=tab;
	m_FilterText.Empty();
	m_bDirty=false;
	m_AppId=appId;
}

// Subclass the tooltip to delay the tip when the mouse moves from one tree item to the next
static LRESULT CALLBACK SubclassSearchBoxProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	if (uMsg==WM_GETDLGCODE && wParam==VK_ESCAPE)
		return DLGC_WANTALLKEYS;
	if (uMsg==WM_CHAR && wParam==VK_ESCAPE)
	{
		if (GetWindowTextLength(hWnd)>0)
			SetWindowText(hWnd,L"");
		else
			SetFocus(GetParent(hWnd));
		return 0;
	}
	return DefSubclassProc(hWnd,uMsg,wParam,lParam);
}

LRESULT CSettingsDlg::OnInitDialog( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
#ifdef _DEBUG
	g_bUIThread=true;
#endif

	if (m_AppId)
	{
		// attempt to make the dialog have its own icon
		CComPtr<IPropertyStore> pStore;
		if (SUCCEEDED(SHGetPropertyStoreForWindow(m_hWnd,IID_IPropertyStore,(void**)&pStore)))
		{
			PROPVARIANT val;
			InitPropVariantFromString(m_AppId,&val);
			pStore->SetValue(PKEY_AppUserModel_ID,val);
		}
	}

	InitResize(MOVE_MODAL);
	HMENU menu=GetSystemMenu(FALSE);
	bool bAdded=false;
	int n=GetMenuItemCount(menu);
	for (int i=0;i<n;i++)
	{
		int id=GetMenuItemID(menu,i);
		if (id==SC_MAXIMIZE)
		{
			InsertMenu(menu,i+1,MF_BYPOSITION|MF_STRING,1,LoadStringEx(IDS_ALWAYS_ON_TOP));
			bAdded=true;
			break;
		}
	}
	if (!bAdded)
		InsertMenu(menu,SC_CLOSE,MF_BYCOMMAND|MF_STRING,1,LoadStringEx(IDS_ALWAYS_ON_TOP));

	Placement pos;
	bool bPosValid=false;
	CRegKey regSettings;
	if (regSettings.Open(HKEY_CURRENT_USER,GetSettingsRegPath())==ERROR_SUCCESS)
	{
		ULONG size=sizeof(pos);
		bPosValid=(regSettings.QueryBinaryValue(L"CSettingsDlg",&pos,&size)==ERROR_SUCCESS && (size==sizeof(pos) || size==sizeof(pos)-4));
		regSettings.Close();
		if (bPosValid && size==sizeof(pos)-4)
			pos.maximized=FALSE;
	}
	if (!bPosValid)
	{
		memset(&pos,0,sizeof(pos));
		pos.basic=true;
	}

	m_bOnTop=pos.top;
	if (m_bOnTop)
	{
		CheckMenuItem(menu,1,MF_BYCOMMAND|MF_CHECKED);
	}

	HINSTANCE hInstance=_AtlBaseModule.GetResourceInstance();

	HICON icon=(HICON)LoadImage(hInstance,MAKEINTRESOURCE(1),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_DEFAULTCOLOR);
	SendMessage(WM_SETICON,ICON_BIG,(LPARAM)icon);
	icon=(HICON)LoadImage(hInstance,MAKEINTRESOURCE(1),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_DEFAULTCOLOR);
	SendMessage(WM_SETICON,ICON_SMALL,(LPARAM)icon);
	icon=(HICON)LoadImage(GetModuleHandle(L"shell32.dll"),MAKEINTRESOURCE(323),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_DEFAULTCOLOR);
	SendDlgItemMessage(IDC_ICONSEARCH,STM_SETICON,(WPARAM)icon);
	SendDlgItemMessage(IDC_EDITSEARCH,EM_SETCUEBANNER,FALSE,(LPARAM)(const wchar_t*)LoadStringEx(IDS_SEARCH_PROMPT));
	SetWindowSubclass(GetDlgItem(IDC_EDITSEARCH),SubclassSearchBoxProc,'CLSH',0);

	CWindow tooltip;
	tooltip.Create(TOOLTIPS_CLASS,m_hWnd,NULL,NULL,WS_POPUP|TTS_NOPREFIX);
	TOOLINFO tool={sizeof(tool),TTF_SUBCLASS|TTF_IDISHWND,m_hWnd,(UINT_PTR)GetDlgItem(IDC_LINKWEB).m_hWnd};
	CString str=LoadStringEx(IDS_WEBSITE_TIP);
	tool.lpszText=(LPWSTR)(LPCWSTR)str;
	tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);

	{
		CString text;
		GetDlgItemText(IDC_SYSLINKLOC,text);
		const wchar_t *str=text;
		const wchar_t *link=wcswcs(str,L"href=\"");
		if (link)
		{
			link+=6;
			const wchar_t *end=wcschr(link,'"');
			if (end)
			{
				CString url=text.Mid((int)(link-str),(int)(end-link));
				tool.uId=(UINT_PTR)GetDlgItem(IDC_SYSLINKLOC).m_hWnd;
				tool.lpszText=(LPWSTR)(LPCWSTR)url;
				tooltip.SendMessage(TTM_ADDTOOL,0,(LPARAM)&tool);
			}
		}
	}

	CWindow backup=GetDlgItem(IDC_BUTTONBACKUP);
	backup.SetWindowLong(GWL_STYLE,backup.GetWindowLong(GWL_STYLE)|BS_SPLITBUTTON);
	BUTTON_SPLITINFO info={BCSIF_STYLE,NULL,BCSS_NOSPLIT};
	backup.SendMessage(BCM_SETSPLITINFO,0,(LPARAM)&info);

	GetDlgItem(IDC_LINKHELP).ShowWindow(HasHelp()?SW_SHOW:SW_HIDE);

	CWindow parent=GetParent();
	if (parent)
	{
		RECT rc1,rc2;
		GetWindowRect(&rc1);
		parent.GetWindowRect(&rc2);
		OffsetRect(&rc1,(rc2.left+rc2.right)/2-(rc1.left+rc1.right)/2,(rc2.top+rc2.bottom)/2-(rc1.top+rc1.bottom)/2);
		if (rc1.top<rc2.top) OffsetRect(&rc1,0,rc2.top-rc1.top);
		SetWindowPos(NULL,rc1.left,rc1.top,rc1.right-rc1.left,rc1.bottom-rc1.top,SWP_NOZORDER);
		SendMessage(DM_REPOSITION,0,0);
	}

	m_bBasic=pos.basic;
	CheckDlgButton(IDC_CHECKALL,m_bBasic?BST_UNCHECKED:BST_CHECKED);

	SIZE maxSize={0,0};
	m_Tabs=GetDlgItem(IDC_TABSETTINGS);
	m_Panel=NULL;
	int idx=0;
	for (const CSetting *pSetting=m_pSettings;pSetting->name;pSetting++)
	{
		if (pSetting->type!=CSetting::TYPE_GROUP)
			continue;
		ISettingsPanel *pPanel=pSetting->pPanel;
		if (!pPanel) pPanel=GetDefaultSettings(NULL,NULL);
		HWND panel=pPanel->Create(m_hWnd);
		RECT rc;
		::GetWindowRect(panel,&rc);
		if (maxSize.cx<rc.right-rc.left)
			maxSize.cx=rc.right-rc.left;
		if (maxSize.cy<rc.bottom-rc.top)
			maxSize.cy=rc.bottom-rc.top;
	}

	RECT rc;
	m_Tabs.GetWindowRect(&rc);
	TabCtrl_AdjustRect(m_Tabs,FALSE,&rc);
	int dx=maxSize.cx-(rc.right-rc.left);
	int dy=maxSize.cy-(rc.bottom-rc.top);
	if (dx<0) dx=0;
	if (dy<0) dy=0;
	if (dx || dy)
	{
		GetWindowRect(&rc);
		rc.left-=dx/2;
		rc.right+=dx-dx/2;
		rc.top-=dy/2;
		rc.bottom+=dy-dy/2;
		SetWindowPos(NULL,&rc,SWP_NOZORDER);
		InitResize(MOVE_MODAL|MOVE_REINITIALIZE);
	}

	{
		CSettingsLockWrite lock;
		for (CSetting *pSetting=m_pSettings;pSetting->name;pSetting++)
		{
			pSetting->tempValue=pSetting->value;
			pSetting->tempFlags=pSetting->flags;
		}
	}

	if (m_InitialTab)
		pos.tab=m_InitialTab;
	AddTabs(pos.tab);
	if (pos.tab && bPosValid)
	{
		SetStoreRect(pos.rc);
		if (pos.maximized)
			ShowWindow(SW_MAXIMIZE);
	}

	return TRUE;
}

LRESULT CSettingsDlg::OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	RemoveWindowSubclass(GetDlgItem(IDC_EDITSEARCH),SubclassSearchBoxProc,'CLSH');
	bHandled=FALSE;
#ifdef _DEBUG
	g_bUIThread=false;
#endif
	return 0;
}

bool CSettingsDlg::IsVisible( const CSetting *pSetting ) const
{
	if (pSetting->flags&CSetting::FLAG_HIDDEN)
		return false;
	int style, mask;
	GetSettingsStyle(style,mask);
	if ((pSetting->flags&mask) && !(pSetting->flags&style))
		return false;
	return true;
}

void CSettingsDlg::AddTabs( int name, const CSetting *pSelect )
{
	TabCtrl_DeleteAllItems(m_Tabs);
	int idx=0;
	if (pSelect)
	{
		m_bIgnoreEdit=true;
		m_FilterText.Empty();
		SetDlgItemText(IDC_EDITSEARCH,L"");
		m_bIgnoreEdit=false;
	}
	for (const CSetting *pSetting=m_pSettings;pSetting->name;pSetting++)
	{
		if (pSetting->type!=CSetting::TYPE_GROUP)
			continue;
		if (m_FilterText.IsEmpty())
		{
			if (!m_bBasic && pSetting->nameID==IDS_BASIC_SETTINGS)
				continue;
			if (m_bBasic && pSetting->nameID!=IDS_BASIC_SETTINGS && !(pSetting->flags&CSetting::FLAG_BASIC))
				continue;
			if (!IsVisible(pSetting))
				continue;
		}
		else
		{
			if (pSetting->nameID!=IDS_BASIC_SETTINGS)
				continue;
		}
		CString str=LoadStringEx(m_FilterText.IsEmpty()?pSetting->nameID:IDS_SETTING_SEARCH);
		TCITEM tab={TCIF_PARAM|TCIF_TEXT,0,0,(LPWSTR)(LPCWSTR)str,0,0,(LPARAM)pSetting};
		int i=TabCtrl_InsertItem(m_Tabs,1000,&tab);
		if (pSetting->nameID==name)
			idx=i;
	}
	m_Index=-1;
	TabCtrl_SetCurSel(m_Tabs,idx);
	m_Tabs.InvalidateRect(NULL);
	SetCurTab(idx,false,pSelect);
}

LRESULT CSettingsDlg::OnSize( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	CResizeableDlg<CSettingsDlg>::OnSize();
	RECT rc;
	m_Tabs.GetWindowRect(&rc);
	::MapWindowPoints(NULL,m_hWnd,(POINT*)&rc,2);
	TabCtrl_AdjustRect(m_Tabs,FALSE,&rc);
	if (m_Panel) ::SetWindowPos(m_Panel,HWND_TOP,rc.left,rc.top,rc.right-rc.left,rc.bottom-rc.top,0);
	return 0;
}

LRESULT CSettingsDlg::OnKeyDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==VK_TAB && GetKeyState(VK_CONTROL)<0)
	{
		int sel=TabCtrl_GetCurSel(m_Tabs);
		if (GetKeyState(VK_SHIFT)<0)
		{
			if (sel>0)
			{
				TabCtrl_SetCurSel(m_Tabs,sel-1);
				SetCurTab(sel-1,false);
			}
		}
		else
		{
			if (sel<TabCtrl_GetItemCount(m_Tabs)-1)
			{
				TabCtrl_SetCurSel(m_Tabs,sel+1);
				SetCurTab(sel+1,false);
			}
		}
	}
	return 0;
}

LRESULT CSettingsDlg::OnSysCommand( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (wParam==1)
	{
		HMENU menu=GetSystemMenu(FALSE);
		m_bOnTop=!m_bOnTop;
		CheckMenuItem(menu,1,MF_BYCOMMAND|(m_bOnTop?MF_CHECKED:MF_UNCHECKED));
		SetWindowPos(m_bOnTop?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE);
		return 0;
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CSettingsDlg::OnResetUI( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	AddTabs(-1);
	return 0;
}

void CSettingsDlg::StorePlacement( void )
{
	Placement pos;
	GetPlacementRect(pos.rc);
	int idx=TabCtrl_GetCurSel(m_Tabs);
	if (idx<0) return;
	TCITEM tab={TCIF_PARAM};
	TabCtrl_GetItem(m_Tabs,idx,&tab);
	CSetting *pGroup=(CSetting*)tab.lParam;
	pos.tab=pGroup->nameID;
	pos.basic=m_bBasic;
	pos.top=m_bOnTop;
	pos.maximized=IsZoomed();

	CRegKey regSettings;
	if (regSettings.Open(HKEY_CURRENT_USER,GetSettingsRegPath())!=ERROR_SUCCESS)
		regSettings.Create(HKEY_CURRENT_USER,GetSettingsRegPath());
	regSettings.SetBinaryValue(L"CSettingsDlg",&pos,sizeof(pos));
}

LRESULT CSettingsDlg::OnOK( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	if (IsTabValid())
	{
		int flags=0;
		for (const CSetting *pSetting=m_pSettings;pSetting->name;pSetting++)
		{
			if (pSetting->type==CSetting::TYPE_GROUP || pSetting->type==CSetting::TYPE_RADIO || pSetting->pLinkTo)
				continue;
			if (pSetting->value!=pSetting->tempValue)
				flags|=pSetting->flags&(CSetting::FLAG_WARM|CSetting::FLAG_COLD);
		}
		g_SettingsManager.SaveSettings(false);
		g_SettingsManager.SaveSettings(true);
		ClosingSettings(m_hWnd,flags,IDOK);
		StorePlacement();
		DestroyWindow();
	}
	return TRUE;
}

LRESULT CSettingsDlg::OnCancel( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	if (m_bDirty)
	{
		int res=MessageBox(LoadStringEx(IDS_UNSAVED_CHANGES),LoadStringEx(IDS_UNSAVED_TITLE),MB_YESNOCANCEL);
		if (res==IDCANCEL)
		{
			return TRUE;
		}

		if (res==IDYES)
		{
			if (IsTabValid())
			{
				int flags=0;
				for (const CSetting *pSetting=m_pSettings;pSetting->name;pSetting++)
				{
					if (pSetting->type==CSetting::TYPE_GROUP || pSetting->type==CSetting::TYPE_RADIO || pSetting->pLinkTo)
						continue;
					if (pSetting->value!=pSetting->tempValue)
						flags|=pSetting->flags&(CSetting::FLAG_WARM|CSetting::FLAG_COLD);
				}
				g_SettingsManager.SaveSettings(false);
				g_SettingsManager.SaveSettings(true);
				ClosingSettings(m_hWnd,flags,IDOK);
				StorePlacement();
				DestroyWindow();
			}
			return TRUE;
		}
	}

	StorePlacement();
	DestroyWindow();
	// restore all settings
	{
		CSettingsLockWrite lock;
		for (CSetting *pSetting=m_pSettings;pSetting->name;pSetting++)
		{
			pSetting->value=pSetting->tempValue;
			pSetting->flags=pSetting->tempFlags;
		}
	}
	ClosingSettings(m_hWnd,0,IDCANCEL);
	return TRUE;
}

LRESULT CSettingsDlg::OnBackup( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	HMENU menu=CreatePopupMenu();
	AppendMenu(menu,MF_STRING,1,LoadStringEx(IDS_BACKUP_SAVE));
	AppendMenu(menu,MF_STRING,2,LoadStringEx(IDS_BACKUP_LOAD));
	AppendMenu(menu,MF_STRING,3,LoadStringEx(IDS_BACKUP_RESET));
	if (m_pCustom)
		m_pCustom->AppendBackupMenu(menu,4);
	TPMPARAMS params={sizeof(params)};
	GetDlgItem(IDC_BUTTONBACKUP).GetWindowRect(&params.rcExclude);
	if (GetWindowLong(GWL_EXSTYLE)&WS_EX_LAYOUTRTL)
	{
		int q=params.rcExclude.left;
		params.rcExclude.left=params.rcExclude.right;
		params.rcExclude.right=q;
	}
	int res=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_VERTICAL,params.rcExclude.left,params.rcExclude.bottom,m_hWnd,&params);
	DestroyMenu(menu);

	// remove the next mouse click if it is on the Backup button
	MSG msg;
	if (PeekMessage(&msg,NULL,WM_LBUTTONDOWN,WM_LBUTTONDBLCLK,PM_NOREMOVE) && PtInRect(&params.rcExclude,msg.pt))
		PeekMessage(&msg,NULL,WM_LBUTTONDOWN,WM_LBUTTONDBLCLK,PM_REMOVE);

	if (res==1)
	{
		// save
		wchar_t path[_MAX_PATH];
		Strcpy(path,_countof(path),g_SettingsManager.GetXMLName());
		OPENFILENAME ofn={sizeof(ofn)};
		ofn.hwndOwner=m_hWnd;
		wchar_t filters[256];
		Strcpy(filters,_countof(filters),LoadStringEx(IDS_XML_FILTERS));
		for (wchar_t *c=filters;*c;c++)
			if (*c=='|') *c=0;
		ofn.lpstrFilter=filters;
		ofn.nFilterIndex=1;
		ofn.lpstrFile=path;
		ofn.nMaxFile=_MAX_PATH;
		CString title=LoadStringEx(IDS_XML_TITLE_SAVE);
		ofn.lpstrTitle=title;
		ofn.lpstrDefExt=L".xml";
		ofn.Flags=OFN_DONTADDTORECENT|OFN_ENABLESIZING|OFN_EXPLORER|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT|OFN_HIDEREADONLY|OFN_NOCHANGEDIR;
		if (GetSaveFileName(&ofn))
		{
			CString err=g_SettingsManager.SaveSettingsXml(path);
			if (!err.IsEmpty())
			{
				wchar_t text[1024];
				Sprintf(text,_countof(text),LoadStringEx(IDS_ERROR_SAVING_XML),path);
				::MessageBox(m_hWnd,text,LoadStringEx(IDS_ERROR_TITLE),MB_OK|MB_ICONERROR);
			}
		}
	}
	if (res==2)
	{
		// load
		wchar_t path[_MAX_PATH];
		path[0]=0;
		OPENFILENAME ofn={sizeof(ofn)};
		ofn.hwndOwner=m_hWnd;
		wchar_t filters[256];
		Strcpy(filters,_countof(filters),LoadStringEx(IDS_XML_FILTERS));
		for (wchar_t *c=filters;*c;c++)
			if (*c=='|') *c=0;
		ofn.lpstrFilter=filters;
		ofn.nFilterIndex=1;
		ofn.lpstrFile=path;
		ofn.nMaxFile=_MAX_PATH;
		CString title=LoadStringEx(IDS_XML_TITLE_LOAD);
		ofn.lpstrTitle=title;
		ofn.Flags=OFN_DONTADDTORECENT|OFN_ENABLESIZING|OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_HIDEREADONLY|OFN_NOCHANGEDIR;
		if (GetOpenFileName(&ofn))
		{
			SetCurTab(m_Index,true); // reload tab once to force-close any active edit boxes
			CString error=g_SettingsManager.LoadSettingsXml(path);
			if (!error.IsEmpty())
			{
				wchar_t text[1024];
				int len=Sprintf(text,_countof(text),LoadStringEx(IDS_ERROR_LOADING_XML),path);
				Sprintf(text+len,_countof(text)-len,L"\r\n\r\n%s",error);
				::MessageBox(m_hWnd,text,LoadStringEx(IDS_ERROR_TITLE),MB_OK|MB_ICONERROR);
			}
			SetSettingsDirty();
			SetCurTab(m_Index,true); // reload tab again to show the new settings
		}
	}
	if (res==3)
	{
		// reset
		if (::MessageBox(m_hWnd,LoadStringEx(IDS_RESET_CONFIRM),LoadStringEx(IDS_RESET_TITLE),MB_YESNO|MB_ICONWARNING)==IDYES)
		{
			{
				CSettingsLockWrite lock;
				g_SettingsManager.ResetSettings();
				UpdateSettings();
				SetSettingsDirty();
			}
			SetCurTab(m_Index,true);
		}
	}
	if (m_pCustom && res>3)
		m_pCustom->ExecuteBackupMenu(res);
	return TRUE;
}

LRESULT CSettingsDlg::OnCheckAll( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	bool bBasic=!IsDlgButtonChecked(IDC_CHECKALL)==BST_CHECKED;
	if (m_bBasic!=bBasic)
	{
		m_bBasic=bBasic;
		AddTabs(-1);
	}
	return 0;
}

LRESULT CSettingsDlg::OnSearchChange( WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled )
{
	if (!m_bIgnoreEdit)
	{
		GetDlgItemText(wID,m_FilterText);
		m_FilterText.Trim();
		StringUpper(m_FilterText);
		GetDlgItem(IDC_CHECKALL).EnableWindow(m_FilterText.IsEmpty());
		AddTabs(-1);
		::SetFocus(hWndCtl);
	}
	return 0;
}

void CSettingsDlg::SetCurTab( int index, bool bReset, const CSetting *pSelect )
{
	if (m_Index==index && !bReset) return;
	m_Index=index;
	TCITEM tab={TCIF_PARAM};
	BOOL res=TabCtrl_GetItem(m_Tabs,index,&tab);
	if (!res)
	{
		if (m_Panel) ::ShowWindow(m_Panel,SW_HIDE);
		m_Panel=NULL;
		return;
	}
	CSetting *pGroup=(CSetting*)tab.lParam;
	ISettingsPanel *pPanel=pGroup->pPanel;
	if (!pPanel) pPanel=GetDefaultSettings(&m_FilterText,pSelect);
	RECT rc;
	m_Tabs.GetWindowRect(&rc);
	::MapWindowPoints(NULL,m_hWnd,(POINT*)&rc,2);
	TabCtrl_AdjustRect(m_Tabs,FALSE,&rc);
	HWND hwnd=pPanel->Activate(pGroup,rc,bReset);
	if (hwnd!=m_Panel)
	{
		if (m_Panel) ::ShowWindow(m_Panel,SW_HIDE);
		m_Panel=hwnd;
		::SetFocus(m_Panel);
	}
}

LRESULT CSettingsDlg::OnSelChanging( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	return !IsTabValid();
}

LRESULT CSettingsDlg::OnSelChange( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	SetCurTab(TabCtrl_GetCurSel(m_Tabs),false);
	return 0;
}

LRESULT CSettingsDlg::OnDropDown( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	SendMessage(WM_COMMAND,IDC_BUTTONBACKUP);
	return 0;
}

LRESULT CSettingsDlg::OnHelp( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	ShowHelp();
	return 0;
}

LRESULT CSettingsDlg::OnWeb( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	ShellExecute(m_hWnd,NULL,L"https://open-shell.github.io/Open-Shell-Menu",NULL,NULL,SW_SHOWNORMAL);
	return 0;
}

LRESULT CSettingsDlg::OnLink( int idCtrl, LPNMHDR pnmh, BOOL& bHandled )
{
	CString text;
	GetDlgItemText(idCtrl,text);
	const wchar_t *str=text;
	const wchar_t *link=wcswcs(str,L"href=\"");
	if (!link) return 0;
	link+=6;
	const wchar_t *end=wcschr(link,'"');
	if (end)
	{
		CString url=text.Mid((int)(link-str),(int)(end-link));
		ShellExecute(m_hWnd,NULL,url,NULL,NULL,SW_SHOWNORMAL);
	}
	return 0;
}

bool CSettingsDlg::IsTabValid( void )
{
	int idx=TabCtrl_GetCurSel(m_Tabs);
	if (idx<0) return true;
	TCITEM tab={TCIF_PARAM};
	TabCtrl_GetItem(m_Tabs,idx,&tab);
	CSetting *pGroup=(CSetting*)tab.lParam;
	ISettingsPanel *pPanel=pGroup->pPanel;
	if (!pPanel) pPanel=GetDefaultSettings(NULL,NULL);
	return pPanel->Validate(m_hWnd);
}

static CSettingsDlg g_SettingsDlg;

void EditSettings( const wchar_t *title, bool bModal, int tab, const wchar_t* appId )
{
	if (g_SettingsDlg.m_hWnd)
	{
		HWND top=GetWindow(g_SettingsDlg,GW_ENABLEDPOPUP);
		if (!top) top=g_SettingsDlg.m_hWnd;
		SetForegroundWindow(top);
		SetActiveWindow(top);
	}
	else
	{
		{
			CSettingsLockWrite lock;
			g_SettingsManager.LoadSettings(true);
			UpdateSettings();
		}
		DLGTEMPLATE *pTemplate=LoadDialogEx(IDD_SETTINGS);
		g_SettingsManager.ResetImageList();
		g_SettingsDlg.Init(g_SettingsManager.GetSettings(),g_SettingsManager.GetCustom(),tab,appId);
		g_SettingsDlg.Create(NULL,pTemplate);
		g_SettingsDlg.SetWindowText(title);
		g_SettingsDlg.SetWindowPos(HWND_TOPMOST,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE|(g_SettingsDlg.GetOnTop()?0:SWP_NOZORDER)|SWP_SHOWWINDOW);
		SetForegroundWindow(g_SettingsDlg.m_hWnd);
		if (bModal)
		{
			MSG msg;
			while (g_SettingsDlg.m_hWnd && GetMessage(&msg,0,0,0))
			{
				if (IsSettingsMessage(&msg)) continue;
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}
}

void CloseSettings( void )
{
	if (g_SettingsDlg.m_hWnd)
		g_SettingsDlg.DestroyWindow();
}

void SetSettingsDirty( void )
{
	g_SettingsDlg.SetDirty();
}

void SelectSettingsTab( int tab, bool bAdvanced, const CSetting *pSelect )
{
	if (g_SettingsDlg.m_hWnd)
	{
		g_SettingsDlg.GetDlgItem(IDC_CHECKALL).EnableWindow(TRUE);
		if (bAdvanced && g_SettingsDlg.m_bBasic)
		{
			g_SettingsDlg.m_bBasic=false;
			g_SettingsDlg.CheckDlgButton(IDC_CHECKALL,BST_CHECKED);
		}
		g_SettingsDlg.AddTabs(tab,pSelect);
	}
}

// Process the dialog messages for the settings box
bool IsSettingsMessage( MSG *msg )
{
	if (!g_SettingsDlg) return false;
	if (msg->hwnd!=g_SettingsDlg && !IsChild(g_SettingsDlg,msg->hwnd)) return false;
	// only process keyboard messages. if we process all messages the settings box gets stuck. I don't know why.
	if (msg->message<WM_KEYFIRST || msg->message>WM_KEYLAST) return false;

	// don't process any messages if a menu is up
	GUITHREADINFO threadInfo={sizeof(threadInfo)};
	GetGUIThreadInfo(GetCurrentThreadId(),&threadInfo);
	if (threadInfo.flags&(GUI_INMENUMODE|GUI_INMOVESIZE)) return false;

	// handle global keys
	if (msg->message==WM_KEYDOWN && msg->wParam==VK_TAB && GetKeyState(VK_CONTROL)<0)
	{
		g_SettingsDlg.SendMessage(WM_KEYDOWN,VK_TAB,msg->lParam);
		return true;
	}
	if (msg->message==WM_KEYDOWN && msg->wParam==VK_RETURN && GetKeyState(VK_CONTROL)<0)
	{
		g_SettingsDlg.SendMessage(WM_COMMAND,IDOK,0);
		return true;
	}
	if (msg->message==WM_KEYDOWN && msg->wParam==VK_F1 && GetKeyState(VK_CONTROL)>=0 && GetKeyState(VK_SHIFT)>=0 && GetKeyState(VK_MENU)>=0)
	{
		ShowHelp();
	}
	if (msg->message==WM_KEYDOWN && (msg->wParam==VK_F3 || (msg->wParam=='F' && GetKeyState(VK_CONTROL)<0)))
	{
		g_SettingsDlg.GotoDlgCtrl(g_SettingsDlg.GetDlgItem(IDC_EDITSEARCH));
		return true;
	}
	return IsDialogMessage(g_SettingsDlg,msg)!=0;
}

bool ImportSettingsXml( const wchar_t *fname )
{
#ifdef _DEBUG
	g_bUIThread=true; // hack to allow settings to be loaded by this thread
#endif
	CString error=g_SettingsManager.LoadSettingsXml(fname);
	if (error.IsEmpty())
	{
		g_SettingsManager.SaveSettings(false);

		// we have successfuly imported settings from XML
		// so there is no need to show settings dialog when start menu is triggered for the first time
		CRegKey regKey;
		if (regKey.Open(HKEY_CURRENT_USER,GetSettingsRegPath())==ERROR_SUCCESS)
			regKey.SetDWORDValue(L"ShowedStyle2",1);

		return true;
	}

	if (AttachConsole(ATTACH_PARENT_PROCESS))
	{
		HANDLE hConsole=GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD q;
		WriteConsole(hConsole,L"\r\n",2,&q,0);
		WriteConsole(hConsole,(const wchar_t*)error,error.GetLength(),&q,0);
		WriteConsole(hConsole,L"\r\n",2,&q,0);
	}
	return false;
}

bool ExportSettingsXml( const wchar_t *fname )
{
#ifdef _DEBUG
	g_bUIThread=true; // hack to allow settings to be loaded by this thread
#endif
	CString error=g_SettingsManager.SaveSettingsXml(fname);
	if (error.IsEmpty())
		return true;

	if (AttachConsole(ATTACH_PARENT_PROCESS))
	{
		HANDLE hConsole=GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD q;
		WriteConsole(hConsole,L"\r\n",2,&q,0);
		WriteConsole(hConsole,(const wchar_t*)error,error.GetLength(),&q,0);
		WriteConsole(hConsole,L"\r\n",2,&q,0);
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////

void InitSettings( CSetting *pSettings, TSettingsComponent component, ICustomSettings *pCustom )
{
	g_SettingsManager.Init(pSettings,component,pCustom);
}

void LoadSettings( void )
{
	CSettingsLockWrite lock;
	g_SettingsManager.LoadSettings(false);
	g_SettingsManager.LoadSettings(true);
}

void SaveSettings( void )
{
#ifdef _DEBUG
	g_bUIThread=true;
#endif
	CSettingsLockRead lock;
	g_SettingsManager.SaveSettings(false);
	g_SettingsManager.SaveSettings(true);
}

void UpdateDefaultSettings( void )
{
	if (!g_SettingsDlg.m_hWnd)
		UpdateSettings();
}

bool GetSettingBool( const wchar_t *name )
{
	return g_SettingsManager.GetSettingBool(name);
}

int GetSettingInt( const wchar_t *name )
{
	return g_SettingsManager.GetSettingInt(name);
}

int GetSettingInt( const wchar_t *name, bool &bDef )
{
	return g_SettingsManager.GetSettingInt(name,bDef);
}

bool GetSettingBool( const wchar_t *name, bool &bDef )
{
	return g_SettingsManager.GetSettingBool(name,bDef);
}

CString GetSettingString( const wchar_t *name )
{
	return g_SettingsManager.GetSettingString(name);
}

HIMAGELIST GetSettingsImageList( HWND tree )
{
	return g_SettingsManager.GetImageList(tree);
}

const wchar_t *GetSettingsRegPath( void )
{
	return g_SettingsManager.GetRegPath();
}

// Finds a setting by name
CSetting *FindSetting( const wchar_t *name )
{
	Assert(g_LockState==2); // must be locked for writing
	for (CSetting *pSetting=g_SettingsManager.GetSettings();pSetting->name;pSetting++)
		if (pSetting->type>=0 && wcscmp(pSetting->name,name)==0)
			return pSetting;
	Assert(0);
	return NULL;
}

bool IsSettingLocked( const wchar_t *name )
{
	for (CSetting *pSetting=g_SettingsManager.GetSettings();pSetting->name;pSetting++)
		if (pSetting->type>=0 && wcscmp(pSetting->name,name)==0)
		{
			Assert(!pSetting->pLinkTo);
			CSettingsLockRead lock;
			return (pSetting->flags&CSetting::FLAG_LOCKED_MASK)!=0;
		}
	Assert(0);
	return false;
}

bool IsSettingForcedDefault( const wchar_t *name )
{
	for (CSetting *pSetting=g_SettingsManager.GetSettings();pSetting->name;pSetting++)
		if (pSetting->type>=0 && wcscmp(pSetting->name,name)==0)
		{
			Assert(!pSetting->pLinkTo);
			CSettingsLockRead lock;
			return pSetting->IsForcedDefault();
		}
	Assert(0);
	return false;
}

// Updates the setting with a new default value and locked flag
void UpdateSetting( const wchar_t *name, const CComVariant &defValue, bool bLockedGP )
{
	CSetting *pSetting=FindSetting(name);
	Assert(pSetting && !pSetting->pLinkTo);
	if (bLockedGP)
		pSetting->flags|=CSetting::FLAG_LOCKED_GP|CSetting::FLAG_DEFAULT;
	else
		pSetting->flags&=~CSetting::FLAG_LOCKED_GP;
	if (!(pSetting->flags&CSetting::FLAG_FORCED_DEFAULT))
		pSetting->defValue=defValue;
	if (pSetting->flags&CSetting::FLAG_DEFAULT)
		pSetting->value=pSetting->defValue;
}

// Updates the setting with a new text and a warning flag
void UpdateSettingText( const wchar_t *name, int nameID, int tipID, bool bWarning )
{
	CSetting *pSetting=FindSetting(name);
	if (bWarning)
		pSetting->flags|=CSetting::FLAG_WARNING;
	else
		pSetting->flags&=~CSetting::FLAG_WARNING;
	if (nameID>=0)
		pSetting->nameID=nameID;
	if (tipID>=0)
		pSetting->tipID=tipID;
}

void HideSetting( const wchar_t *name, bool bHide )
{
	CSetting *pSetting=FindSetting(name);
	Assert(pSetting);
	if (bHide)
		pSetting->flags|=CSetting::FLAG_HIDDEN;
	else
		pSetting->flags&=~CSetting::FLAG_HIDDEN;
}

void HideSettingGroup( const wchar_t *name, bool bHide )
{
	Assert(g_LockState==2); // must be locked for writing
	for (CSetting *pSetting=g_SettingsManager.GetSettings();pSetting->name;pSetting++)
		if (pSetting->type==CSetting::TYPE_GROUP && wcscmp(pSetting->name,name)==0)
		{
			if (bHide)
				pSetting->flags|=CSetting::FLAG_HIDDEN;
			else
				pSetting->flags&=~CSetting::FLAG_HIDDEN;
			return;
		}
	Assert(0);
}

void UpdateGroupText( const wchar_t *name, int nameID )
{
	Assert(g_LockState==2); // must be locked for writing
	for (CSetting *pSetting=g_SettingsManager.GetSettings();pSetting->name;pSetting++)
		if (pSetting->type==CSetting::TYPE_GROUP && wcscmp(pSetting->name,name)==0)
		{
			pSetting->nameID=nameID;
			return;
		}
	Assert(0);
}

const CSetting *GetAllSettings( void )
{
	Assert(g_bUIThread);
	return g_SettingsManager.GetSettings();
}

void SetSettingsStyle( int style, int mask )
{
	Assert((style&~mask)==0);
	if (g_SettingsManager.SetSettingsStyle(style,mask) && g_SettingsDlg.m_hWnd)
		g_SettingsDlg.PostMessage(WM_CLEAR);
}

void GetSettingsStyle( int &style, int &mask )
{
	g_SettingsManager.GetSettingsStyle(style,mask);
}

bool HasHelp( void )
{
	wchar_t path[_MAX_PATH];
	GetModuleFileName(_AtlBaseModule.GetResourceInstance(),path,_countof(path));
	*PathFindFileName(path)=0;
	wchar_t topic[_MAX_PATH];
	Sprintf(topic,_countof(topic),L"%sOpenShell.chm",path);
	return (GetFileAttributes(topic)!=INVALID_FILE_ATTRIBUTES);
}

void ShowHelp( void )
{
	wchar_t path[_MAX_PATH];
	GetModuleFileName(_AtlBaseModule.GetResourceInstance(),path,_countof(path));
	*PathFindFileName(path)=0;
	wchar_t topic[_MAX_PATH];
	Sprintf(topic,_countof(topic),L"%sOpenShell.chm::/%s.html",path,PathFindFileName(g_SettingsManager.GetRegPath()));
	HtmlHelp(GetDesktopWindow(),topic,HH_DISPLAY_TOPIC,NULL);
}

// Opens the registry keys for the settings of the given component
// Returns true if the user settings were read from the old HKCU location. That means the settings may need to be upgraded
bool OpenSettingsKeys( TSettingsComponent component, CRegKey &regSettings, CRegKey &regSettingsUser, CRegKey &regPolicy, CRegKey &regPolicyUser )
{
	const wchar_t *regPath, *gpPath;
	GetRegPaths(component,regPath,gpPath);
	return OpenSettingsKeys(regPath,gpPath,regSettings,regSettingsUser,regPolicy,regPolicyUser);
}

bool GetSettingBool( const CSetting &setting )
{
	Assert(setting.type==CSetting::TYPE_BOOL);
	return setting.value.vt==VT_I4 && setting.value.intVal==1;
}

CString GetSettingString( const CSetting &setting )
{
	Assert(setting.type==CSetting::TYPE_STRING || setting.type==CSetting::TYPE_DIRECTORY);
	if (setting.value.vt!=VT_BSTR)
		return CString();
	return setting.value.bstrVal;
}

#ifndef _WIN64

namespace
{
enum TSupportedOn
{
	SUPPORTED_NEVER,
	SUPPORTED_ALWAYS,
	SUPPORTED_WIN7,
	SUPPORTED_WIN78,
	SUPPORTED_WIN781,
	SUPPORTED_WIN8,
	SUPPORTED_WIN881,
	SUPPORTED_WIN81,
	SUPPORTED_CLASSIC1_STYLE,
	SUPPORTED_CLASSIC2_STYLE,
	SUPPORTED_CLASSIC_STYLE,
	SUPPORTED_WIN7_STYLE,
};

struct AdmxDoc
{
	AdmxDoc( void ) { supportedOn=SUPPORTED_ALWAYS; }
	TSupportedOn supportedOn;
	CStringA text;
	CStringA nameOverride;
	CStringA tipOverride;
	CStringA tipAddition;
};

}

static bool ParseAdmxDoc( const char *docFile, std::map<CString,AdmxDoc> &docMap )
{
	FILE *fDoc;
	if (fopen_s(&fDoc,docFile,"rt")!=0)
		return false;

	char buf[2048];
	while (fgets(buf,_countof(buf),fDoc))
	{
		const char *str=buf;
		if (str[0]==(char)0xEF && str[1]==(char)0xBB && str[2]==(char)0xBF)
			str+=3;
		if (str[0]==';') continue;
		char name[100];
		str=GetToken(str,name,_countof(name),". \r\n");
		char setting[100];
		str=GetToken(str,setting,_countof(setting),"= \r\n");
		while (*str==' ' || *str=='=')
			str++;
		char value[2048];
		GetToken(str,value,_countof(value),"\r\n");
		str=value;
		CString nameStr(name);
		if (strcmp(setting,"text")==0)
			docMap[L"%"+nameStr+L"%"].text=str;
		else if (strcmp(setting,"supportedOn")==0)
		{
			if (_stricmp(str,"never")==0)
				docMap[nameStr].supportedOn=SUPPORTED_NEVER;
			else if (_stricmp(str,"always")==0)
				docMap[nameStr].supportedOn=SUPPORTED_ALWAYS;
			else if (_stricmp(str,"win7")==0)
				docMap[nameStr].supportedOn=SUPPORTED_WIN7;
			else if (_stricmp(str,"win78")==0)
				docMap[nameStr].supportedOn=SUPPORTED_WIN78;
			else if (_stricmp(str,"win781")==0)
				docMap[nameStr].supportedOn=SUPPORTED_WIN781;
			else if (_stricmp(str,"win8")==0)
				docMap[nameStr].supportedOn=SUPPORTED_WIN8;
			else if (_stricmp(str,"win881")==0)
				docMap[nameStr].supportedOn=SUPPORTED_WIN881;
			else if (_stricmp(str,"win81")==0)
				docMap[nameStr].supportedOn=SUPPORTED_WIN81;
			else if (_stricmp(str,"classic1")==0)
				docMap[nameStr].supportedOn=SUPPORTED_CLASSIC1_STYLE;
			else if (_stricmp(str,"classic2")==0)
				docMap[nameStr].supportedOn=SUPPORTED_CLASSIC2_STYLE;
			else if (_stricmp(str,"classic")==0)
				docMap[nameStr].supportedOn=SUPPORTED_CLASSIC_STYLE;
			else if (_stricmp(str,"win7_style")==0)
				docMap[nameStr].supportedOn=SUPPORTED_WIN7_STYLE;
		}
		else if (strcmp(setting,"nameOverride")==0)
		{
			docMap[nameStr].nameOverride=str;
		}
		else if (strcmp(setting,"tipOverride")==0)
		{
			docMap[nameStr].tipOverride=str;
			docMap[nameStr].tipOverride.Replace("\\n","\r\n");
		}
		else if (strcmp(setting,"tipAddition")==0)
		{
			docMap[nameStr].tipAddition=str;
			docMap[nameStr].tipAddition.Replace("\\n","\r\n");
		}
	}

	fclose(fDoc);
	return true;
}

static CStringA EscapeXmlString( CStringA text )
{
	text.Replace("&","&amp;");
	text.Replace("<","&lt;");
	text.Replace(">","&gt;");
	text.Replace("\"","&quot;");
	return text;
}

static CStringA ReplaceStrings( CStringA text, const std::map<CString,AdmxDoc> &doc )
{
	for (int i=0;i<text.GetLength();i++)
	{
		if (text[i]=='%')
		{
			int end=i;
			for (int j=i+2;j<text.GetLength();j++)
			{
				if (text[j]=='%')
				{
					end=j;
					break;
				}
				if (!isalnum(text[j]) && text[j]!='_')
					break;
			}
			if (end>i)
			{
				CStringA key=text.Mid(i,end-i+1);
				const std::map<CString,AdmxDoc>::const_iterator it=doc.find(CString(key));
				if (it!=doc.end())
				{
					text.Replace(key,EscapeXmlString(CStringA(it->second.text)));
				}
				else
				{
					Assert(0);
				}
			}
		}
	}
	return text;
}

static CStringA LoadStringUTF8( int stringID )
{
	CString wstr=LoadStringEx(stringID);
	CStringA str;
	if (!wstr.IsEmpty())
	{
		int len=WideCharToMultiByte(CP_UTF8,0,wstr,-1,NULL,0,NULL,NULL)-1;
		if (len>0)
		{
			char *buf=str.GetBuffer(len);
			WideCharToMultiByte(CP_UTF8,0,wstr,-1,buf,len,NULL,NULL);
			str.ReleaseBufferSetLength(len);
		}
	}
	return str;
}

static const char *g_AdmxText1a=
"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
"<policyDefinitions xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" revision=\"0.9\" schemaVersion=\"1.0\" xmlns=\"http://schemas.microsoft.com/GroupPolicy/2006/07/PolicyDefinitions\">\r\n"
"\t<policyNamespaces>\r\n";

static const char *g_AdmxText1cs=
"\t\t<target prefix=\"classic_shell\" namespace=\"Open-Shell.Policies.OpenShell\" />\r\n"
"\t</policyNamespaces>\r\n"
"\t<supersededAdm fileName=\"\" />\r\n"
"\t<resources minRequiredRevision=\"1.0\"/>\r\n"
"\t<supportedOn>\r\n"
"\t\t<definitions>\r\n"
"\t\t\t<definition name=\"SUPPORTED_CS404\" displayName=\"$(string.SUPPORTED_CS404)\"/>\r\n"
"\t\t</definitions>\r\n"
"\t</supportedOn>\r\n"
"\t<categories>\r\n"
"\t\t<category name=\"OpenShell\" displayName=\"$(string.OpenShellCat)\" explainText=\"$(string.OpenShellCatHelp)\"/>\r\n";

static const char *g_AdmxText1csm=
"\t\t<target prefix=\"classic_start_menu\" namespace=\"Open-Shell.Policies.Menu\" />\r\n"
"\t\t<using prefix=\"classic_shell\" namespace=\"Open-Shell.Policies.OpenShell\" />\r\n"
"\t</policyNamespaces>\r\n"
"\t<supersededAdm fileName=\"\" />\r\n"
"\t<resources minRequiredRevision=\"1.0\"/>\r\n"
"\t<supportedOn>\r\n"
"\t\t<definitions>\r\n"
"\t\t\t<definition name=\"SUPPORTED_CS404_WIN7\" displayName=\"$(string.SUPPORTED_CS404_WIN7)\"/>\r\n"
"\t\t\t<definition name=\"SUPPORTED_CS404_WIN78\" displayName=\"$(string.SUPPORTED_CS404_WIN78)\"/>\r\n"
"\t\t\t<definition name=\"SUPPORTED_CS404_WIN781\" displayName=\"$(string.SUPPORTED_CS404_WIN781)\"/>\r\n"
"\t\t\t<definition name=\"SUPPORTED_CS404_WIN8\" displayName=\"$(string.SUPPORTED_CS404_WIN8)\"/>\r\n"
"\t\t\t<definition name=\"SUPPORTED_CS404_WIN881\" displayName=\"$(string.SUPPORTED_CS404_WIN881)\"/>\r\n"
"\t\t\t<definition name=\"SUPPORTED_CS404_WIN81\" displayName=\"$(string.SUPPORTED_CS404_WIN81)\"/>\r\n"
"\t\t\t<definition name=\"SUPPORTED_CS404_CLASSIC1_STYLE\" displayName=\"$(string.SUPPORTED_CS404_CLASSIC1_STYLE)\"/>\r\n"
"\t\t\t<definition name=\"SUPPORTED_CS404_CLASSIC2_STYLE\" displayName=\"$(string.SUPPORTED_CS404_CLASSIC2_STYLE)\"/>\r\n"
"\t\t\t<definition name=\"SUPPORTED_CS404_CLASSIC_STYLE\" displayName=\"$(string.SUPPORTED_CS404_CLASSIC_STYLE)\"/>\r\n"
"\t\t\t<definition name=\"SUPPORTED_CS404_WIN7_STYLE\" displayName=\"$(string.SUPPORTED_CS404_WIN7_STYLE)\"/>\r\n"
"\t\t</definitions>\r\n"
"\t</supportedOn>\r\n"
"\t<categories>\r\n"
"\t\t<category name=\"StartMenu\" displayName=\"$(string.MenuCat)\" explainText=\"$(string.MenuCatHelp)\">\r\n"
"\t\t\t<parentCategory ref=\"classic_shell:OpenShell\" />\r\n"
"\t\t</category>\r\n";

static const char *g_AdmxText1ce=
"\t\t<target prefix=\"classic_explorer\" namespace=\"Open-Shell.Policies.ClassicExplorer\" />\r\n"
"\t\t<using prefix=\"classic_shell\" namespace=\"Open-Shell.Policies.OpenShell\" />\r\n"
"\t</policyNamespaces>\r\n"
"\t<supersededAdm fileName=\"\" />\r\n"
"\t<resources minRequiredRevision=\"1.0\"/>\r\n"
"\t<supportedOn>\r\n"
"\t\t<definitions>\r\n"
"\t\t\t<definition name=\"SUPPORTED_CS404_WIN7\" displayName=\"$(string.SUPPORTED_CS404_WIN7)\"/>\r\n"
"\t\t\t<definition name=\"SUPPORTED_CS404_WIN881\" displayName=\"$(string.SUPPORTED_CS404_WIN881)\"/>\r\n"
"\t\t</definitions>\r\n"
"\t</supportedOn>\r\n"
"\t<categories>\r\n"
"\t\t<category name=\"ClassicExplorer\" displayName=\"$(string.ClassicExplorerCat)\" explainText=\"$(string.ClassicExplorerCatHelp)\">\r\n"
"\t\t\t<parentCategory ref=\"classic_shell:OpenShell\" />\r\n"
"\t\t</category>\r\n";

static const char *g_AdmxText1cie=
"\t\t<target prefix=\"classic_ie\" namespace=\"Open-Shell.Policies.ClassicIE\" />\r\n"
"\t\t<using prefix=\"classic_shell\" namespace=\"Open-Shell.Policies.OpenShell\" />\r\n"
"\t</policyNamespaces>\r\n"
"\t<supersededAdm fileName=\"\" />\r\n"
"\t<resources minRequiredRevision=\"1.0\"/>\r\n"
"\t<supportedOn>\r\n"
"\t\t<definitions>\r\n"
"\t\t\t<definition name=\"SUPPORTED_CS404\" displayName=\"$(string.SUPPORTED_CS404)\"/>\r\n"
"\t\t</definitions>\r\n"
"\t</supportedOn>\r\n"
"\t<categories>\r\n"
"\t\t<category name=\"ClassicIE\" displayName=\"$(string.ClassicIECat)\" explainText=\"$(string.ClassicIECatHelp)\">\r\n"
"\t\t\t<parentCategory ref=\"classic_shell:OpenShell\" />\r\n"
"\t\t</category>\r\n";

static const char *g_AdmxText1b=
"\t</categories>\r\n"
"\t<policies>\r\n";

static const char *g_AdmxText2=
"\t</policies>\r\n"
"</policyDefinitions>\r\n";

static const char *g_AdmlText1a=
"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
"<policyDefinitionResources xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" revision=\"1.0\" schemaVersion=\"1.0\" xmlns=\"http://schemas.microsoft.com/GroupPolicy/2006/07/PolicyDefinitions\">\r\n"
"\t<displayName>%Title%</displayName>\r\n"
"\t<description>%Title%</description>\r\n"
"\t<resources>\r\n"
"\t\t<stringTable>\r\n"
"\t\t\t<string id=\"SettingState1\">%State1%</string>\r\n"
"\t\t\t<string id=\"SettingState2\">%State2%</string>\r\n"
"\t\t\t<string id=\"SettingState3\">%State3%</string>\r\n";

static const char *g_AdmlText1cs=
"\t\t\t<string id=\"OpenShellCat\">%OpenShellCat%</string>\r\n"
"\t\t\t<string id=\"OpenShellCatHelp\">%OpenShellCatHelp%</string>\r\n"
"\t\t\t<string id=\"SUPPORTED_CS404\">%SUPPORTED_CS404%</string>\r\n";

static const char *g_AdmlText1csm=
"\t\t\t<string id=\"MenuCat\">%MenuCat%</string>\r\n"
"\t\t\t<string id=\"MenuCatHelp\">%MenuCatHelp%</string>\r\n"
"\t\t\t<string id=\"SUPPORTED_CS404_WIN7\">%SUPPORTED_CS404%\r\n"
"%SUPPORTED_CS404_WIN7%</string>\r\n"
"\t\t\t<string id=\"SUPPORTED_CS404_WIN78\">%SUPPORTED_CS404%\r\n"
"%SUPPORTED_CS404_WIN78%</string>\r\n"
"\t\t\t<string id=\"SUPPORTED_CS404_WIN781\">%SUPPORTED_CS404%\r\n"
"%SUPPORTED_CS404_WIN781%</string>\r\n"
"\t\t\t<string id=\"SUPPORTED_CS404_WIN8\">%SUPPORTED_CS404%\r\n"
"%SUPPORTED_CS404_WIN8%</string>\r\n"
"\t\t\t<string id=\"SUPPORTED_CS404_WIN881\">%SUPPORTED_CS404%\r\n"
"%SUPPORTED_CS404_WIN881%</string>\r\n"
"\t\t\t<string id=\"SUPPORTED_CS404_WIN81\">%SUPPORTED_CS404%\r\n"
"%SUPPORTED_CS404_WIN81%</string>\r\n"
"\t\t\t<string id=\"SUPPORTED_CS404_CLASSIC1_STYLE\">%SUPPORTED_CS404%\r\n"
"%SUPPORTED_CS404_CLASSIC1_STYLE%</string>\r\n"
"\t\t\t<string id=\"SUPPORTED_CS404_CLASSIC2_STYLE\">%SUPPORTED_CS404%\r\n"
"%SUPPORTED_CS404_CLASSIC2_STYLE%</string>\r\n"
"\t\t\t<string id=\"SUPPORTED_CS404_CLASSIC_STYLE\">%SUPPORTED_CS404%\r\n"
"%SUPPORTED_CS404_CLASSIC_STYLE%</string>\r\n"
"\t\t\t<string id=\"SUPPORTED_CS404_WIN7_STYLE\">%SUPPORTED_CS404%\r\n"
"%SUPPORTED_CS404_WIN7_STYLE%</string>\r\n";

static const char *g_AdmlText1ce=
"\t\t\t<string id=\"ClassicExplorerCat\">%ClassicExplorerCat%</string>\r\n"
"\t\t\t<string id=\"ClassicExplorerCatHelp\">%ClassicExplorerCatHelp%</string>\r\n"
"\t\t\t<string id=\"SUPPORTED_CS404_WIN7\">%SUPPORTED_CS404%\r\n"
"%SUPPORTED_CS404_WIN7%</string>\r\n"
"\t\t\t<string id=\"SUPPORTED_CS404_WIN881\">%SUPPORTED_CS404%\r\n"
"%SUPPORTED_CS404_WIN881%</string>\r\n";

static const char *g_AdmlText1cie=
"\t\t\t<string id=\"ClassicIECat\">Classic IE</string>\r\n"
"\t\t\t<string id=\"ClassicIECatHelp\">Classic IE group policy settings</string>\r\n"
"\t\t\t<string id=\"SUPPORTED_CS404\">%SUPPORTED_CS404%\r\n"
"%SUPPORTED_IE9%</string>\r\n";

static const char *g_AdmlText2=
"\t\t</stringTable>\r\n"
"\t\t<presentationTable>\r\n";

static const char *g_AdmlText3=
"\t\t</presentationTable>\r\n"
"\t</resources>\r\n"
"</policyDefinitionResources>\r\n";

static const char *g_StateTip=
"%State1Help%\r\n\r\n"
"%State2Help%\r\n\r\n"
"%State3Help%\r\n";

bool SaveAdmx( TSettingsComponent component, const char *admxFile, const char *admlFile, const char *docFile )
{
	const char *keyName, *prefix, *catName;
	const char *admxText1, *admlText1;
	switch (component)
	{
		case COMPONENT_EXPLORER:
			keyName="Software\\Policies\\OpenShell\\ClassicExplorer";
			prefix="CE_";
			catName="ClassicExplorer";
			admxText1=g_AdmxText1ce;
			admlText1=g_AdmlText1ce;
			break;
		case COMPONENT_MENU:
			keyName="Software\\Policies\\OpenShell\\StartMenu";
			prefix="CSM_";
			catName="StartMenu";
			admxText1=g_AdmxText1csm;
			admlText1=g_AdmlText1csm;
			break;
		case COMPONENT_IE:
			keyName="Software\\Policies\\OpenShell\\ClassicIE";
			prefix="CIE_";
			catName="ClassicIE";
			admxText1=g_AdmxText1cie;
			admlText1=g_AdmlText1cie;
			break;
		case COMPONENT_SHARED:
			keyName="Software\\Policies\\OpenShell\\OpenShell";
			prefix="CS_";
			catName="OpenShell";
			admxText1=g_AdmxText1cs;
			admlText1=g_AdmlText1cs;
			break;
	}

	std::map<CString,AdmxDoc> docMap;
	if (!ParseAdmxDoc(docFile,docMap))
		return false;

	FILE *fAdmx, *fAdml;

	if (fopen_s(&fAdmx,admxFile,"wb")!=0)
		return false;

	if (fopen_s(&fAdml,admlFile,"wb")!=0)
	{
		fclose(fAdmx);
		return false;
	}

	fprintf_s(fAdmx,g_AdmxText1a);
	fprintf_s(fAdmx,admxText1);
	fprintf_s(fAdmx,g_AdmxText1b);

	fprintf_s(fAdml,"%s",(const char*)ReplaceStrings(CStringA(g_AdmlText1a),docMap));
	fprintf_s(fAdml,"%s",(const char*)ReplaceStrings(CStringA(admlText1),docMap));

	// policies and strings
	int stringIdx=1;
	for (CSetting *pSetting=g_SettingsManager.GetSettings();pSetting->name;pSetting++)
	{
		if (pSetting->pLinkTo || pSetting->type==CSetting::TYPE_GROUP || pSetting->type==CSetting::TYPE_RADIO)
			continue;

		if (pSetting->flags&CSetting::FLAG_SHARED)
		{
			if (component!=COMPONENT_SHARED) continue;
		}
		else
		{
			if (component==COMPONENT_SHARED) continue;
		}

		AdmxDoc doc0;
		const AdmxDoc *pDoc=&doc0;
		{
			std::map<CString,AdmxDoc>::const_iterator it=docMap.find(pSetting->name);
			if (it!=docMap.end())
				pDoc=&it->second;
		}
		if (pDoc->supportedOn==SUPPORTED_NEVER)
			continue;

		// name string
		int nameIdx=stringIdx++;
		fprintf_s(fAdml,"\t\t\t<string id=\"%sstr_%d\">%s</string>\r\n",prefix,nameIdx,(const char*)EscapeXmlString(pDoc->nameOverride.IsEmpty()?LoadStringUTF8(pSetting->nameID):pDoc->nameOverride));

		// tip string
		int tipIdx=stringIdx++;
		CStringA tip=pDoc->tipOverride;
		if (tip.IsEmpty()) tip=LoadStringUTF8(pSetting->tipID);
		tip+=pDoc->tipAddition;
		fprintf_s(fAdml,"\t\t\t<string id=\"%sstr_%d\">%s\r\n\r\n%s</string>\r\n",prefix,tipIdx,(const char*)EscapeXmlString(tip),(const char*)ReplaceStrings(g_StateTip,docMap));

		// policy
		fprintf_s(fAdmx,"\t\t<policy name=\"%s%S\" class=\"Both\" displayName=\"$(string.%sstr_%d)\" explainText=\"$(string.%sstr_%d)\" presentation=\"$(presentation.%s%S)\" key=\"%s\">\r\n",
			prefix,pSetting->name,prefix,nameIdx,prefix,tipIdx,prefix,pSetting->name,keyName);
		fprintf_s(fAdmx,"\t\t\t<parentCategory ref=\"%s\" />\r\n",catName);
		switch (pDoc->supportedOn)
		{
			case SUPPORTED_WIN7:
				fprintf_s(fAdmx,"\t\t\t<supportedOn ref=\"SUPPORTED_CS404_WIN7\" />\r\n");
				break;
			case SUPPORTED_WIN78:
				fprintf_s(fAdmx,"\t\t\t<supportedOn ref=\"SUPPORTED_CS404_WIN78\" />\r\n");
				break;
			case SUPPORTED_WIN781:
				fprintf_s(fAdmx,"\t\t\t<supportedOn ref=\"SUPPORTED_CS404_WIN781\" />\r\n");
				break;
			case SUPPORTED_WIN8:
				fprintf_s(fAdmx,"\t\t\t<supportedOn ref=\"SUPPORTED_CS404_WIN8\" />\r\n");
				break;
			case SUPPORTED_WIN881:
				fprintf_s(fAdmx,"\t\t\t<supportedOn ref=\"SUPPORTED_CS404_WIN881\" />\r\n");
				break;
			case SUPPORTED_WIN81:
				fprintf_s(fAdmx,"\t\t\t<supportedOn ref=\"SUPPORTED_CS404_WIN81\" />\r\n");
				break;
			case SUPPORTED_CLASSIC1_STYLE:
				fprintf_s(fAdmx,"\t\t\t<supportedOn ref=\"SUPPORTED_CS404_CLASSIC1_STYLE\" />\r\n");
				break;
			case SUPPORTED_CLASSIC2_STYLE:
				fprintf_s(fAdmx,"\t\t\t<supportedOn ref=\"SUPPORTED_CS404_CLASSIC2_STYLE\" />\r\n");
				break;
			case SUPPORTED_CLASSIC_STYLE:
				fprintf_s(fAdmx,"\t\t\t<supportedOn ref=\"SUPPORTED_CS404_CLASSIC_STYLE\" />\r\n");
				break;
			case SUPPORTED_WIN7_STYLE:
				fprintf_s(fAdmx,"\t\t\t<supportedOn ref=\"SUPPORTED_CS404_WIN7_STYLE\" />\r\n");
				break;
			default:
				if (component==COMPONENT_SHARED || component==COMPONENT_IE)
					fprintf_s(fAdmx,"\t\t\t<supportedOn ref=\"SUPPORTED_CS404\" />\r\n");
				else
					fprintf_s(fAdmx,"\t\t\t<supportedOn ref=\"classic_shell:SUPPORTED_CS404\" />\r\n");
				break;
		}
		fprintf_s(fAdmx,"\t\t\t<elements>\r\n");

		fprintf_s(fAdmx,"\t\t\t\t<enum id=\"State\" valueName=\"%S_State\">\r\n",pSetting->name);
		fprintf_s(fAdmx,"\t\t\t\t\t<item displayName=\"$(string.SettingState1)\"><value><decimal value=\"0\" /></value></item>\r\n");
		fprintf_s(fAdmx,"\t\t\t\t\t<item displayName=\"$(string.SettingState2)\"><value><decimal value=\"1\" /></value></item>\r\n");
		fprintf_s(fAdmx,"\t\t\t\t\t<item displayName=\"$(string.SettingState3)\"><value><decimal value=\"2\" /></value></item>\r\n");
		fprintf_s(fAdmx,"\t\t\t\t</enum>\r\n");

		if (pSetting->type==CSetting::TYPE_BOOL)
		{
			fprintf_s(fAdmx,"\t\t\t\t<boolean id=\"Value\" valueName=\"%S\">\r\n",pSetting->name);
			fprintf_s(fAdmx,"\t\t\t\t\t<trueValue><decimal value=\"1\" /></trueValue>\r\n");
			fprintf_s(fAdmx,"\t\t\t\t\t<falseValue><decimal value=\"0\" /></falseValue>\r\n");
			fprintf_s(fAdmx,"\t\t\t\t</boolean>\r\n");
		}
		else if (pSetting->type==CSetting::TYPE_INT && pSetting[1].type==CSetting::TYPE_RADIO)
		{
			// radio options
			fprintf_s(fAdmx,"\t\t\t\t<enum id=\"Value\" valueName=\"%S\">\r\n",pSetting->name);
			for (int i=1;pSetting[i].type==CSetting::TYPE_RADIO;i++)
			{
				const AdmxDoc *pRadioDoc=&doc0;
				{
					wchar_t name[100];
					Sprintf(name,_countof(name),L"%s_%s",pSetting->name,pSetting[i].name);
					std::map<CString,AdmxDoc>::const_iterator it=docMap.find(name);
					if (it!=docMap.end())
						pRadioDoc=&it->second;
				}

				int radioIdx=stringIdx++;
				fprintf_s(fAdml,"\t\t\t<string id=\"%sstr_%d\">%s</string>\r\n",prefix,radioIdx,(const char*)(pRadioDoc->nameOverride.IsEmpty()?LoadStringUTF8(pSetting[i].nameID):pRadioDoc->nameOverride));
				fprintf_s(fAdmx,"\t\t\t\t\t<item displayName=\"$(string.%sstr_%d)\"><value><string>%S</string></value></item>\r\n",prefix,radioIdx,pSetting[i].name);
			}
			fprintf_s(fAdmx,"\t\t\t\t</enum>\r\n");
		}
		else if (pSetting->type==CSetting::TYPE_INT || pSetting->type==CSetting::TYPE_HOTKEY || pSetting->type==CSetting::TYPE_HOTKEY_ANY || pSetting->type==CSetting::TYPE_COLOR)
		{
			fprintf_s(fAdmx,"\t\t\t\t<decimal id=\"Value\" valueName=\"%S\"/>\r\n",pSetting->name);
		}
		else if (pSetting->type==CSetting::TYPE_STRING || pSetting->type==CSetting::TYPE_ICON || pSetting->type==CSetting::TYPE_BITMAP || pSetting->type==CSetting::TYPE_BITMAP_JPG || pSetting->type==CSetting::TYPE_SOUND || pSetting->type==CSetting::TYPE_FONT || pSetting->type==CSetting::TYPE_DIRECTORY)
		{
			fprintf_s(fAdmx,"\t\t\t\t<text id=\"Value\" valueName=\"%S\"/>\r\n",pSetting->name);
		}
		else if (pSetting->type==CSetting::TYPE_MULTISTRING)
		{
			fprintf_s(fAdmx,"\t\t\t\t<multiText id=\"Value\" valueName=\"%S\"/>\r\n",pSetting->name);
		}
		else
		{
			Assert(0);
		}

		fprintf_s(fAdmx,"\t\t\t</elements>\r\n");
		fprintf_s(fAdmx,"\t\t</policy>\r\n\r\n");
	}

	fprintf_s(fAdml,g_AdmlText2);

	CStringA stateText=ReplaceStrings(CStringA("\t\t\t\t<dropdownList refId=\"State\" defaultItem=\"0\" noSort=\"true\">%State%</dropdownList>\r\n"),docMap);
	// presentation
	for (CSetting *pSetting=g_SettingsManager.GetSettings();pSetting->name;pSetting++)
	{
		if (pSetting->pLinkTo || pSetting->type==CSetting::TYPE_GROUP || pSetting->type==CSetting::TYPE_RADIO)
			continue;

		if (pSetting->flags&CSetting::FLAG_SHARED)
		{
			if (component!=COMPONENT_SHARED) continue;
		}
		else
		{
			if (component==COMPONENT_SHARED) continue;
		}

		AdmxDoc doc0;
		const AdmxDoc *pDoc=&doc0;
		std::map<CString,AdmxDoc>::const_iterator it=docMap.find(pSetting->name);
		if (it!=docMap.end())
			pDoc=&it->second;
		if (pDoc->supportedOn==SUPPORTED_NEVER)
			continue;

		fprintf_s(fAdml,"\t\t\t<presentation id=\"%s%S\">\r\n",prefix,pSetting->name);
		fprintf_s(fAdml,stateText);

		CStringA name=EscapeXmlString(pDoc->nameOverride.IsEmpty()?LoadStringUTF8(pSetting->nameID):pDoc->nameOverride);

		if (pSetting->type==CSetting::TYPE_BOOL)
		{
			fprintf_s(fAdml,"\t\t\t\t<checkBox refId=\"Value\">%s</checkBox>\r\n",(const char*)name);
		}
		else if (pSetting->type==CSetting::TYPE_INT && pSetting[1].type==CSetting::TYPE_RADIO)
		{
			fprintf_s(fAdml,"\t\t\t\t<dropdownList refId=\"Value\" defaultItem=\"0\" noSort=\"true\">%s</dropdownList>\r\n",(const char*)name);
		}
		else if (pSetting->type==CSetting::TYPE_INT || pSetting->type==CSetting::TYPE_HOTKEY || pSetting->type==CSetting::TYPE_HOTKEY_ANY || pSetting->type==CSetting::TYPE_COLOR)
		{
			fprintf_s(fAdml,"\t\t\t\t<decimalTextBox refId=\"Value\" spin=\"false\">%s</decimalTextBox>\r\n",(const char*)name);
		}
		else if (pSetting->type==CSetting::TYPE_STRING || pSetting->type==CSetting::TYPE_ICON || pSetting->type==CSetting::TYPE_BITMAP || pSetting->type==CSetting::TYPE_BITMAP_JPG || pSetting->type==CSetting::TYPE_SOUND || pSetting->type==CSetting::TYPE_FONT || pSetting->type==CSetting::TYPE_DIRECTORY)
		{
			fprintf_s(fAdml,"\t\t\t\t<textBox refId=\"Value\"><label>%s</label></textBox>\r\n",(const char*)name);
		}
		else if (pSetting->type==CSetting::TYPE_MULTISTRING)
		{
			fprintf_s(fAdml,"\t\t\t\t<multiTextBox refId=\"Value\">%s</multiTextBox>\r\n",(const char*)name);
		}
		fprintf_s(fAdml,"\t\t\t</presentation>\r\n");
	}

	fprintf_s(fAdmx,g_AdmxText2);
	fprintf_s(fAdml,g_AdmlText3);

	fclose(fAdmx);
	fclose(fAdml);
	return true;
}

#endif

static wchar_t g_LogFileName[_MAX_PATH];

void VLogToFile( const wchar_t *location, const wchar_t *message, va_list args )
{
	if (g_LogFileName[0]==0)
	{
		g_LogFileName[0]='*';
		wchar_t token[_MAX_PATH];
		location=GetToken(location,token,_countof(token),L"|");
		if (token[0]!='-')
		{
			CRegKey regKey;
			if (regKey.Open(HKEY_CURRENT_USER,token,KEY_READ|KEY_WOW64_64KEY)!=ERROR_SUCCESS)
				return;
			DWORD log;
			location=GetToken(location,token,_countof(token),L"|");
			if (regKey.QueryDWORDValue(token,log)!=ERROR_SUCCESS || log==0)
				return;
		}
		location=GetToken(location,token,_countof(token),L"|");
		DoEnvironmentSubst(token,_countof(token));
		Strcpy(g_LogFileName,_countof(g_LogFileName),token);
		PathRemoveFileSpec(token);
		SHCreateDirectory(NULL,token);
	}
	if (g_LogFileName[0]!='*')
	{
		wchar_t text[1024];
		int len=Sprintf(text,_countof(text),L"%10u %5u ",GetTickCount(),GetCurrentProcessId());
		len+=Vsprintf(text+len,_countof(text)-2-len,message,args);
		Strcpy(text+len,3,L"\r\n");
		len+=2;
		FILE *f;
		if (_wfopen_s(&f,g_LogFileName,L"ab")==0)
		{
			fseek(f,0,SEEK_END);
			if (ftell(f)==0)
				fwrite(L"\xFEFF",2,1,f);
			fwrite(text,2,len,f);
			fclose(f);
		}
	}
}

void LogToFile( const wchar_t *location, const wchar_t *message, ... )
{
	if (g_LogFileName[0]=='*') return;
	va_list args;
	va_start(args,message);
	VLogToFile(location,message,args);
	va_end(args);
}
