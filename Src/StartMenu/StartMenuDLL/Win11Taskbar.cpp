// Windows 11 draws the taskbar with XAML. This file talks to that visual tree
// through the public XAML diagnostics API and applies the existing Open-Shell
// taskbar settings there. The older GDI path is left in place for Windows 10.

#include "stdafx.h"
#include "StartMenuDLL.h"
#include "Win11Taskbar.h"
#include "Settings.h"
#include "SettingsUI.h"
#include "ResourceHelper.h"
#include "LogManager.h"
#include "SkinManager.h"
#include <inspectable.h>
#include <ocidl.h>
#include <shlwapi.h>
#include <unordered_map>
#include <vector>

static const GUID CLSID_OpenShellTaskbarTap=
{ 0x8f4e2c19, 0x6a3b, 0x4d75, { 0x9e, 0x10, 0xc7, 0xb2, 0x5a, 0x48, 0xd6, 0xf1 } };

typedef unsigned __int64 OsHandle;

struct OsSourceInfo
{
	BSTR FileName;
	unsigned int LineNumber;
	unsigned int ColumnNumber;
	unsigned int CharPosition;
	BSTR Hash;
};

struct OsParentChild
{
	OsHandle Parent;
	OsHandle Child;
	unsigned int ChildIndex;
};

struct OsVisualElement
{
	OsHandle Handle;
	OsSourceInfo SrcInfo;
	BSTR Type;
	BSTR Name;
	unsigned int NumChildren;
};

struct OsPropertySource
{
	OsHandle Handle;
	BSTR TargetType;
	BSTR Name;
	int Source;
	OsSourceInfo SrcInfo;
};

struct OsPropertyValue
{
	unsigned int Index;
	BSTR Type;
	BSTR DeclaringType;
	BSTR ValueType;
	BSTR ItemType;
	BSTR Value;
	int Overridden;
	__int64 MetadataBits;
	BSTR PropertyName;
	unsigned int PropertyChainIndex;
};

#ifdef _WIN64
static_assert(sizeof(OsVisualElement)==64, "unexpected XAML element layout");
static_assert(sizeof(OsPropertyValue)==80, "unexpected XAML property layout");
#endif

MIDL_INTERFACE("aa7a8931-80e4-4fec-8f3b-553f87b4966e")
IOsVisualCallback: public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE OnVisualTreeChange( OsParentChild relation, OsVisualElement element, int mutationType )=0;
};

MIDL_INTERFACE("bad9eb88-ae77-4397-b948-5fa2db0a19ea")
IOsVisualCallback2: public IOsVisualCallback
{
	virtual HRESULT STDMETHODCALLTYPE OnElementStateChanged( OsHandle element, int elementState, LPCWSTR context )=0;
};

MIDL_INTERFACE("a593b11a-d17f-48bb-8f66-83910731c8a5")
IOsVisualTree: public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE AdviseVisualTreeChange( IOsVisualCallback *callback )=0;
	virtual HRESULT STDMETHODCALLTYPE UnadviseVisualTreeChange( IOsVisualCallback *callback )=0;
	virtual HRESULT STDMETHODCALLTYPE GetEnums( unsigned int *count, void **enums )=0;
	virtual HRESULT STDMETHODCALLTYPE CreateInstance( BSTR typeName, BSTR value, OsHandle *handle )=0;
	virtual HRESULT STDMETHODCALLTYPE GetPropertyValuesChain( OsHandle handle, unsigned int *sourceCount, OsPropertySource **sources, unsigned int *propertyCount, OsPropertyValue **values )=0;
	virtual HRESULT STDMETHODCALLTYPE SetProperty( OsHandle handle, OsHandle value, unsigned int propertyIndex )=0;
	virtual HRESULT STDMETHODCALLTYPE ClearProperty( OsHandle handle, unsigned int propertyIndex )=0;
	virtual HRESULT STDMETHODCALLTYPE GetCollectionCount( OsHandle handle, unsigned int *count )=0;
	virtual HRESULT STDMETHODCALLTYPE GetCollectionElements( OsHandle handle, unsigned int startIndex, unsigned int *count, void **values )=0;
	virtual HRESULT STDMETHODCALLTYPE AddChild( OsHandle parent, OsHandle child, unsigned int index )=0;
	virtual HRESULT STDMETHODCALLTYPE RemoveChild( OsHandle parent, unsigned int index )=0;
	virtual HRESULT STDMETHODCALLTYPE ClearChildren( OsHandle parent )=0;
};

MIDL_INTERFACE("18c9e2b6-3f43-4116-9f2b-ff935d7770d2")
IOsXamlDiagnostics: public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetDispatcher( IInspectable **dispatcher )=0;
};

MIDL_INTERFACE("dfa2dc9c-1a2d-4917-98f2-939af1d6e0c8")
IOsDispatcherHandler: public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE Invoke( void )=0;
};

MIDL_INTERFACE("603e88e4-a338-4ffe-a457-a5cfb9ceb899")
IOsDispatcherQueue: public IInspectable
{
	virtual HRESULT STDMETHODCALLTYPE CreateTimer( IInspectable **timer )=0;
	virtual HRESULT STDMETHODCALLTYPE get_HasThreadAccess( unsigned char *value )=0;
	virtual HRESULT STDMETHODCALLTYPE TryEnqueue( IOsDispatcherHandler *handler, unsigned char *result )=0;
	virtual HRESULT STDMETHODCALLTYPE TryEnqueueWithPriority( int priority, IOsDispatcherHandler *handler, unsigned char *result )=0;
};

struct OsCompAttrData
{
	DWORD attribute;
	PVOID pData;
	ULONG dataSize;
};

typedef BOOL (WINAPI *OsSetWindowComposition)( HWND hwnd, OsCompAttrData *data );
typedef HRESULT (WINAPI *OsInitXamlDiagnosticsEx)( PCWSTR endpoint, DWORD pid, PCWSTR diagnosticsDll, PCWSTR tapDll, CLSID tapClsid, PCWSTR initData );

struct OsElement
{
	OsHandle parent;
	CString type;
	CString name;
	bool overridden;
};

struct OsStyle
{
	bool custom;
	int look;
	int opacity;
	COLORREF color;
	bool hasTexture;
	bool tile;
	bool replaceButton;
	bool allTaskbars;
	wchar_t texture[MAX_PATH];
	OsHandle colorBrush;
	OsHandle clearBrush;
	OsHandle imageBrush;
	bool colorReady;
	bool clearReady;
	bool imageReady;
};

static CRITICAL_SECTION g_Lock;
static INIT_ONCE g_LockOnce=INIT_ONCE_STATIC_INIT;
static thread_local int t_ApplyDepth=0;
static int g_LogBits=0;

static BOOL CALLBACK InitLock( PINIT_ONCE, PVOID, PVOID * )
{
	InitializeCriticalSection(&g_Lock);
	return TRUE;
}

static void LockElements( void )
{
	InitOnceExecuteOnce(&g_LockOnce,InitLock,NULL,NULL);
	EnterCriticalSection(&g_Lock);
}

static void UnlockElements( void )
{
	LeaveCriticalSection(&g_Lock);
}

static void LogOnce( int bit, const wchar_t *text, HRESULT hr )
{
	if (g_LogBits&bit) return;
	g_LogBits|=bit;
	LogToFile(STARTUP_LOG,L"Win11Taskbar: %s 0x%08X",text,hr);
}

static CString CopyBstr( BSTR text )
{
	return text?CString(text):CString();
}

static bool ContainsText( const CString &text, const wchar_t *part )
{
	return text.Find(part)>=0;
}

// The primary Start logo is an AnimatedVisualPlayer named Icon. Other taskbars
// also use Taskbar.AepAnimatedIcon, and sometimes a static Image.
static bool IsStartGlyph( const CString &type, const CString &name )
{
	if (name==L"Icon")
		return true;
	if (ContainsText(type,L"AnimatedVisualPlayer") || ContainsText(type,L"AepAnimatedIcon"))
		return true;
	if (ContainsText(type,L"FontIcon") || ContainsText(type,L"PathIcon") || ContainsText(type,L"ImageIcon") || ContainsText(type,L"BitmapIcon") || ContainsText(type,L"SymbolIcon"))
		return true;
	if (ContainsText(type,L"Image") && !ContainsText(type,L"Imaging"))
		return true;
	return false;
}

class CTaskbarTap;

static CTaskbarTap *g_Tap=NULL;

class CApplyHandler: public IOsDispatcherHandler
{
public:
	CApplyHandler( CTaskbarTap *tap ) { m_Tap=tap; m_Refs=1; }

	STDMETHODIMP QueryInterface( REFIID riid, void **ppv )
	{
		if (!ppv) return E_POINTER;
		if (riid==IID_IUnknown || riid==__uuidof(IOsDispatcherHandler))
		{
			*ppv=static_cast<IOsDispatcherHandler*>(this);
			AddRef();
			return S_OK;
		}
		*ppv=NULL;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef( void ) { return (ULONG)InterlockedIncrement(&m_Refs); }
	STDMETHODIMP_(ULONG) Release( void )
	{
		LONG left=InterlockedDecrement(&m_Refs);
		return (ULONG)left;
	}
	STDMETHODIMP Invoke( void );

private:
	CTaskbarTap *m_Tap;
	LONG m_Refs;
};

class CTaskbarTap: public IObjectWithSite, public IOsVisualCallback2
{
public:
	CTaskbarTap( void )
	{
		m_Refs=1;
		m_XamlThread=0;
		m_Pending=false;
		m_Framework=0;
		m_PrimaryStart=0;
		m_Handler=new CApplyHandler(this);
	}

	~CTaskbarTap( void )
	{
		if (m_Visual)
		{
			m_Visual->UnadviseVisualTreeChange(static_cast<IOsVisualCallback*>(this));
			m_Visual.Release();
		}
		m_Dispatcher.Release();
		delete m_Handler;
		m_Handler=NULL;
	}

	static CTaskbarTap *Create( void )
	{
		LockElements();
		if (!g_Tap)
			g_Tap=new CTaskbarTap();
		CTaskbarTap *tap=g_Tap;
		UnlockElements();
		return tap;
	}

	static CTaskbarTap *Existing( void ) { return g_Tap; }

	STDMETHODIMP QueryInterface( REFIID riid, void **ppv )
	{
		if (!ppv) return E_POINTER;
		if (riid==IID_IUnknown || riid==IID_IObjectWithSite)
			*ppv=static_cast<IObjectWithSite*>(this);
		else if (riid==__uuidof(IOsVisualCallback) || riid==__uuidof(IOsVisualCallback2))
			*ppv=static_cast<IOsVisualCallback2*>(this);
		else
		{
			*ppv=NULL;
			return E_NOINTERFACE;
		}
		AddRef();
		return S_OK;
	}
	STDMETHODIMP_(ULONG) AddRef( void ) { return (ULONG)InterlockedIncrement(&m_Refs); }
	STDMETHODIMP_(ULONG) Release( void )
	{
		LONG left=InterlockedDecrement(&m_Refs);
		if (left==0)
		{
			LockElements();
			if (g_Tap==this)
				g_Tap=NULL;
			UnlockElements();
			delete this;
		}
		return (ULONG)left;
	}

	STDMETHODIMP SetSite( IUnknown *site )
	{
		if (m_Visual)
		{
			m_Visual->UnadviseVisualTreeChange(static_cast<IOsVisualCallback*>(this));
			m_Visual.Release();
		}
		m_Dispatcher.Release();
		m_Site.Release();
		if (!site)
			return S_OK;

		m_Site=site;
		m_XamlThread=GetCurrentThreadId();
		CComPtr<IOsXamlDiagnostics> diagnostics;
		if (SUCCEEDED(site->QueryInterface(__uuidof(IOsXamlDiagnostics),(void**)&diagnostics)) && diagnostics)
		{
			CComPtr<IInspectable> dispatcher;
			if (SUCCEEDED(diagnostics->GetDispatcher(&dispatcher)) && dispatcher)
				dispatcher->QueryInterface(__uuidof(IOsDispatcherQueue),(void**)&m_Dispatcher);
		}
		HRESULT hr=site->QueryInterface(__uuidof(IOsVisualTree),(void**)&m_Visual);
		if (FAILED(hr) || !m_Visual)
		{
			LogToFile(STARTUP_LOG,L"Win11Taskbar: visual tree service missing 0x%08X",hr);
			return hr;
		}
		hr=m_Visual->AdviseVisualTreeChange(static_cast<IOsVisualCallback*>(this));
		LogToFile(STARTUP_LOG,L"Win11Taskbar: advise visual tree 0x%08X",hr);
		if (SUCCEEDED(hr))
			ApplyStyles();
		return hr;
	}

	STDMETHODIMP GetSite( REFIID riid, void **ppv )
	{
		if (!ppv) return E_POINTER;
		*ppv=NULL;
		if (!m_Site) return E_FAIL;
		return m_Site->QueryInterface(riid,ppv);
	}

	STDMETHODIMP OnVisualTreeChange( OsParentChild relation, OsVisualElement element, int mutationType )
	{
		m_XamlThread=GetCurrentThreadId();
		CString type=CopyBstr(element.Type);
		CString name=CopyBstr(element.Name);
		bool interesting=false;

		LockElements();
		if (mutationType==1)
		{
			m_Elements.erase(element.Handle);
			if (element.Handle==m_PrimaryStart)
				m_PrimaryStart=0;
		}
		else if (mutationType==0)
		{
			OsElement rec;
			rec.parent=relation.Parent;
			rec.type=type;
			rec.name=name;
			rec.overridden=false;
			std::unordered_map<OsHandle,OsElement>::iterator previous=m_Elements.find(element.Handle);
			if (previous!=m_Elements.end())
				rec.overridden=previous->second.overridden;
			m_Elements[element.Handle]=rec;
			if (!m_PrimaryStart && ContainsText(type,L"ExperienceToggleButton"))
				m_PrimaryStart=element.Handle;
			bool underStart=false;
			OsHandle parent=relation.Parent;
			for (int depth=0;depth<24 && parent;depth++)
			{
				std::unordered_map<OsHandle,OsElement>::iterator pit=m_Elements.find(parent);
				if (pit==m_Elements.end())
					break;
				if (ContainsText(pit->second.type,L"ExperienceToggleButton") || ContainsText(pit->second.name,L"LaunchListButton") || pit->second.name==L"StartButton")
				{
					underStart=true;
					break;
				}
				parent=pit->second.parent;
			}
			bool glyph=IsStartGlyph(type,name) && (underStart || ContainsText(type,L"AnimatedVisualPlayer") || ContainsText(type,L"AepAnimatedIcon"));
			interesting=(name==L"BackgroundFill" || name==L"BackgroundStroke" || ContainsText(type,L"TaskbarFrame") || ContainsText(type,L"TaskbarBackground") || ContainsText(type,L"ExperienceToggleButton") || glyph);
		}
		bool pending=m_Pending;
		UnlockElements();
		if (interesting && g_ElementLogs<12 && (ContainsText(type,L"Taskbar") || name==L"BackgroundFill" || name==L"BackgroundStroke" || ContainsText(type,L"ExperienceToggleButton")))
		{
			g_ElementLogs++;
			LogToFile(STARTUP_LOG,L"Win11Taskbar: element type=%s name=%s",(const wchar_t*)type,(const wchar_t*)name);
		}

		if (t_ApplyDepth)
		{
			m_Pending=true;
			return S_OK;
		}
		if (interesting || pending)
			ApplyStyles();
		return S_OK;
	}

	STDMETHODIMP OnElementStateChanged( OsHandle, int, LPCWSTR ) { return S_OK; }

	void RequestApply( void )
	{
		if (!m_Visual)
			return;
		if (m_XamlThread==0 || GetCurrentThreadId()==m_XamlThread)
		{
			if (ApplyStyles()!=RPC_E_WRONG_THREAD)
				return;
		}
		if (m_Dispatcher && m_Handler)
		{
			unsigned char queued=0;
			HRESULT hr=m_Dispatcher->TryEnqueue(m_Handler,&queued);
			if (SUCCEEDED(hr) && queued)
				return;
			LogOnce(1,L"dispatcher enqueue failed",hr);
		}
		m_Pending=true;
	}

	HRESULT ApplyStyles( void )
	{
		if (!m_Visual)
			return E_FAIL;
		t_ApplyDepth++;
		m_Pending=false;

		OsStyle style;
		memset(&style,0,sizeof(style));
		ReadStyle(&style);
		static int loggedStyle=0;
		if (!loggedStyle)
		{
			loggedStyle=1;
			LogToFile(STARTUP_LOG,L"Win11Taskbar: style custom=%d look=%d opacity=%d texture=%d button=%d",style.custom?1:0,style.look,style.opacity,style.hasTexture?1:0,style.replaceButton?1:0);
		}

		std::vector<OsHandle> handles;
		LockElements();
		for (std::unordered_map<OsHandle,OsElement>::iterator it=m_Elements.begin();it!=m_Elements.end();++it)
		{
			bool iconish=IsStartGlyph(it->second.type,it->second.name);
			bool startControl=ContainsText(it->second.type,L"ExperienceToggleButton");
			if (it->second.name==L"BackgroundFill" || it->second.name==L"BackgroundStroke" || iconish || startControl)
				handles.push_back(it->first);
		}
		UnlockElements();

		HRESULT result=S_OK;
		for (size_t i=0;i<handles.size();i++)
		{
			HRESULT hr=StyleElement(handles[i],&style);
			if (hr==RPC_E_WRONG_THREAD)
			{
				result=hr;
				break;
			}
		}

		bool again=m_Pending;
		t_ApplyDepth--;
		if (again && result!=RPC_E_WRONG_THREAD)
			return ApplyStyles();
		if (result!=RPC_E_WRONG_THREAD)
			LogStartChildren();
		return result;
	}

	void LogStartChildren( void )
	{
		std::vector<CString> lines;
		int buttons=0;
		LockElements();
		for (std::unordered_map<OsHandle,OsElement>::iterator it=m_Elements.begin();it!=m_Elements.end();++it)
		{
			if (ContainsText(it->second.type,L"ExperienceToggleButton"))
				buttons++;
		}
		static int seenButtons=0;
		if (buttons<=seenButtons)
		{
			UnlockElements();
			return;
		}
		seenButtons=buttons;
		for (std::unordered_map<OsHandle,OsElement>::iterator it=m_Elements.begin();it!=m_Elements.end() && lines.size()<24;++it)
		{
			bool start=false;
			OsHandle parent=it->second.parent;
			for (int depth=0;depth<24 && parent;depth++)
			{
				std::unordered_map<OsHandle,OsElement>::iterator pit=m_Elements.find(parent);
				if (pit==m_Elements.end())
					break;
				if (ContainsText(pit->second.type,L"ExperienceToggleButton") || pit->second.name==L"LaunchListButton")
				{
					start=true;
					break;
				}
				parent=pit->second.parent;
			}
			if (!start)
				continue;
			if (it->second.name.IsEmpty() && !IsStartGlyph(it->second.type,it->second.name))
				continue;
			CString line;
			line.Format(L"%s|%s",(const wchar_t*)it->second.type,(const wchar_t*)it->second.name);
			lines.push_back(line);
		}
		UnlockElements();
		LogToFile(STARTUP_LOG,L"Win11Taskbar: start buttons %d",buttons);
		for (size_t i=0;i<lines.size();i++)
			LogToFile(STARTUP_LOG,L"Win11Taskbar: start child %s",(const wchar_t*)lines[i]);
	}

private:
	struct Ancestor
	{
		bool taskbar;
		bool startButton;
		OsHandle startButtonHandle;
	};

	void ReadStyle( OsStyle *style )
	{
		WaitDllInitThread();
		style->custom=GetSettingBool(L"CustomTaskbar");
		style->look=GetSettingInt(L"TaskbarLook");
		style->opacity=GetSettingInt(L"TaskbarOpacity");
		if (style->look==TASKBAR_OPAQUE)
			style->opacity=100;
		if (style->opacity<0) style->opacity=0;
		if (style->opacity>100) style->opacity=100;
		bool defaultColor=false;
		style->color=GetSettingInt(L"TaskbarColor",defaultColor);
		if (defaultColor)
		{
			bool transparent=false;
			style->color=GetMetroTaskbarColor(transparent);
		}
		Strcpy(style->texture,_countof(style->texture),GetSettingString(L"TaskbarTexture"));
		DoEnvironmentSubst(style->texture,_countof(style->texture));
		style->hasTexture=style->texture[0] && GetFileAttributes(style->texture)!=INVALID_FILE_ATTRIBUTES;
		style->tile=GetSettingInt(L"TaskbarTileH")==TILE_TILE || GetSettingInt(L"TaskbarTileV")==TILE_TILE;
		style->replaceButton=GetSettingBool(L"EnableStartButton");
		style->allTaskbars=GetSettingBool(L"AllTaskbars");
	}

	bool Describe( OsHandle handle, OsElement *element, Ancestor *ancestor )
	{
		LockElements();
		std::unordered_map<OsHandle,OsElement>::iterator it=m_Elements.find(handle);
		if (it==m_Elements.end())
		{
			UnlockElements();
			return false;
		}
		*element=it->second;
		ancestor->taskbar=false;
		ancestor->startButton=false;
		ancestor->startButtonHandle=0;
		OsHandle parent=it->second.parent;
		for (int depth=0;depth<24 && parent;depth++)
		{
			std::unordered_map<OsHandle,OsElement>::iterator pit=m_Elements.find(parent);
			if (pit==m_Elements.end())
				break;
			if (ContainsText(pit->second.type,L"TaskbarFrame") || ContainsText(pit->second.type,L"TaskbarBackground") || ContainsText(pit->second.name,L"TaskbarFrame") || ContainsText(pit->second.name,L"TaskbarBackground"))
				ancestor->taskbar=true;
			if (ContainsText(pit->second.type,L"ExperienceToggleButton") || ContainsText(pit->second.type,L"LaunchListButton") || ContainsText(pit->second.name,L"LaunchListButton") || pit->second.name==L"StartButton")
			{
				ancestor->startButton=true;
				if (!ancestor->startButtonHandle)
					ancestor->startButtonHandle=parent;
			}
			parent=pit->second.parent;
		}
		UnlockElements();
		return true;
	}

	void Remember( OsHandle handle, bool overridden )
	{
		LockElements();
		std::unordered_map<OsHandle,OsElement>::iterator it=m_Elements.find(handle);
		if (it!=m_Elements.end())
			it->second.overridden=overridden;
		UnlockElements();
	}

	HRESULT CreateValue( const wchar_t *suffix, const wchar_t *value, OsHandle *handle )
	{
		const wchar_t *prefixes[2]={ L"Microsoft.UI.Xaml", L"Windows.UI.Xaml" };
		int order[2]={ 0, 1 };
		if (m_Framework==2)
		{
			order[0]=1;
			order[1]=0;
		}
		HRESULT hr=E_FAIL;
		for (int i=0;i<2;i++)
		{
			wchar_t typeName[256];
			Sprintf(typeName,_countof(typeName),L"%s%s",prefixes[order[i]],suffix);
			BSTR typeBstr=SysAllocString(typeName);
			BSTR valueBstr=SysAllocString(value?value:L"");
			OsHandle created=0;
			hr=m_Visual->CreateInstance(typeBstr,valueBstr,&created);
			SysFreeString(typeBstr);
			SysFreeString(valueBstr);
			if (SUCCEEDED(hr))
			{
				m_Framework=order[i]+1;
				*handle=created;
				return hr;
			}
		}
		return hr;
	}

	static void FreeProperties( OsPropertySource *sources, unsigned int sourceCount, OsPropertyValue *values, unsigned int valueCount )
	{
		if (sources)
		{
			for (unsigned int i=0;i<sourceCount;i++)
			{
				SysFreeString(sources[i].TargetType);
				SysFreeString(sources[i].Name);
				SysFreeString(sources[i].SrcInfo.FileName);
				SysFreeString(sources[i].SrcInfo.Hash);
			}
			CoTaskMemFree(sources);
		}
		if (values)
		{
			for (unsigned int i=0;i<valueCount;i++)
			{
				SysFreeString(values[i].Type);
				SysFreeString(values[i].DeclaringType);
				SysFreeString(values[i].ValueType);
				SysFreeString(values[i].ItemType);
				SysFreeString(values[i].Value);
				SysFreeString(values[i].PropertyName);
			}
			CoTaskMemFree(values);
		}
	}

	HRESULT FindProperty( OsHandle handle, const wchar_t *name, unsigned int *index )
	{
		unsigned int sourceCount=0;
		unsigned int valueCount=0;
		OsPropertySource *sources=NULL;
		OsPropertyValue *values=NULL;
		HRESULT hr=m_Visual->GetPropertyValuesChain(handle,&sourceCount,&sources,&valueCount,&values);
		if (FAILED(hr))
		{
			FreeProperties(sources,sourceCount,values,valueCount);
			return hr;
		}
		bool found=false;
		bool foundReadOnly=false;
		unsigned int readOnlyIndex=0;
		for (unsigned int i=0;i<valueCount;i++)
		{
			if (!values[i].PropertyName || wcscmp(values[i].PropertyName,name)!=0)
				continue;
			if ((values[i].MetadataBits&0x2)==0)
			{
				*index=values[i].Index;
				found=true;
				break;
			}
			foundReadOnly=true;
			readOnlyIndex=values[i].Index;
		}
		FreeProperties(sources,sourceCount,values,valueCount);
		if (!found && foundReadOnly)
		{
			*index=readOnlyIndex;
			found=true;
		}
		return found?S_OK:HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
	}

	HRESULT SetNamed( OsHandle handle, const wchar_t *name, OsHandle value )
	{
		unsigned int index=0;
		HRESULT hr=FindProperty(handle,name,&index);
		if (FAILED(hr))
			return hr;
		return m_Visual->SetProperty(handle,value,index);
	}

	// Primitive and enum values use the type reported for that property.
	HRESULT SetPrimitive( OsHandle handle, const wchar_t *name, const wchar_t *valueText )
	{
		unsigned int sourceCount=0;
		unsigned int valueCount=0;
		OsPropertySource *sources=NULL;
		OsPropertyValue *values=NULL;
		HRESULT hr=m_Visual->GetPropertyValuesChain(handle,&sourceCount,&sources,&valueCount,&values);
		if (FAILED(hr))
		{
			FreeProperties(sources,sourceCount,values,valueCount);
			return hr;
		}
		BSTR typeName=NULL;
		unsigned int index=0;
		bool found=false;
		for (unsigned int i=0;i<valueCount;i++)
		{
			if (!values[i].PropertyName || wcscmp(values[i].PropertyName,name)!=0)
				continue;
			if (values[i].Type)
			{
				if (typeName)
					SysFreeString(typeName);
				typeName=SysAllocString(values[i].Type);
			}
			index=values[i].Index;
			found=true;
			if ((values[i].MetadataBits&0x2)==0)
				break;
		}
		FreeProperties(sources,sourceCount,values,valueCount);
		if (!found || !typeName)
		{
			if (typeName)
				SysFreeString(typeName);
			return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
		}
		BSTR valueBstr=SysAllocString(valueText);
		OsHandle created=0;
		hr=m_Visual->CreateInstance(typeName,valueBstr,&created);
		SysFreeString(valueBstr);
		if (FAILED(hr))
		{
			static int logged=0;
			if (logged<4)
			{
				logged++;
				LogToFile(STARTUP_LOG,L"Win11Taskbar: value %s=%s failed 0x%08X type=%s",name,valueText,hr,typeName);
			}
			SysFreeString(typeName);
			return hr;
		}
		SysFreeString(typeName);
		return m_Visual->SetProperty(handle,created,index);
	}

	HRESULT ClearNamed( OsHandle handle, const wchar_t *name )
	{
		unsigned int index=0;
		HRESULT hr=FindProperty(handle,name,&index);
		if (FAILED(hr))
			return hr;
		return m_Visual->ClearProperty(handle,index);
	}

	HRESULT EnsureClear( OsStyle *style, OsHandle *handle )
	{
		if (style->clearReady)
		{
			*handle=style->clearBrush;
			return style->clearBrush?S_OK:E_FAIL;
		}
		style->clearReady=true;
		HRESULT hr=CreateValue(L".Media.SolidColorBrush",L"#00000000",&style->clearBrush);
		if (FAILED(hr))
			LogOnce(2,L"transparent brush failed",hr);
		*handle=style->clearBrush;
		return hr;
	}

	HRESULT EnsureColor( OsStyle *style, OsHandle *handle )
	{
		if (style->colorReady)
		{
			*handle=style->colorBrush;
			return style->colorBrush?S_OK:E_FAIL;
		}
		style->colorReady=true;
		int alpha=style->opacity*255/100;
		if (style->look==TASKBAR_OPAQUE)
			alpha=255;
		wchar_t text[16];
		Sprintf(text,_countof(text),L"#%02X%02X%02X%02X",alpha,GetRValue(style->color),GetGValue(style->color),GetBValue(style->color));
		HRESULT hr=CreateValue(L".Media.SolidColorBrush",text,&style->colorBrush);
		if (FAILED(hr))
			LogOnce(4,L"color brush failed",hr);
		*handle=style->colorBrush;
		return hr;
	}

	static CString FileUri( const wchar_t *path )
	{
		CString uri=(path[0]==L'\\' && path[1]==L'\\')?L"file:":L"file:///";
		for (const wchar_t *p=path;*p;p++)
		{
			if (*p==L'\\') uri+=L'/';
			else if (*p==L' ') uri+=L"%20";
			else if (*p==L'%') uri+=L"%25";
			else if (*p==L'#') uri+=L"%23";
			else uri+=*p;
		}
		return uri;
	}

	HRESULT EnsureImage( OsStyle *style, OsHandle *handle )
	{
		if (style->imageReady)
		{
			*handle=style->imageBrush;
			return style->imageBrush?S_OK:E_FAIL;
		}
		style->imageReady=true;
		CString uri=FileUri(style->texture);
		OsHandle bitmap=0;
		HRESULT hr=CreateValue(L".Media.Imaging.BitmapImage",uri,&bitmap);
		if (FAILED(hr))
		{
			LogOnce(8,L"bitmap failed",hr);
			return hr;
		}
		hr=CreateValue(L".Media.ImageBrush",L"",&style->imageBrush);
		if (FAILED(hr))
		{
			LogOnce(8,L"image brush failed",hr);
			return hr;
		}
		hr=SetNamed(style->imageBrush,L"ImageSource",bitmap);
		if (FAILED(hr))
		{
			LogOnce(8,L"ImageSource failed",hr);
			style->imageBrush=0;
			return hr;
		}
		OsHandle stretch=0;
		const wchar_t *stretchName=style->tile?L"None":L"Fill";
		if (SUCCEEDED(CreateValue(L".Media.Stretch",stretchName,&stretch)))
			SetNamed(style->imageBrush,L"Stretch",stretch);
		if (style->tile)
		{
			OsHandle tile=0;
			if (SUCCEEDED(CreateValue(L".Media.TileMode",L"Tile",&tile)))
				SetNamed(style->imageBrush,L"TileMode",tile);
		}
		*handle=style->imageBrush;
		return S_OK;
	}

	HRESULT FillBrush( OsStyle *style, OsHandle *handle )
	{
		if (style->hasTexture)
		{
			HRESULT hr=EnsureImage(style,handle);
			if (SUCCEEDED(hr))
				return hr;
		}
		if (style->look==TASKBAR_OPAQUE)
			return EnsureColor(style,handle);
		return EnsureClear(style,handle);
	}

	HRESULT StyleElement( OsHandle handle, OsStyle *style )
	{
		OsElement element;
		Ancestor ancestor;
		if (!Describe(handle,&element,&ancestor))
			return S_FALSE;

		bool fill=element.name==L"BackgroundFill" && ContainsText(element.type,L"Rectangle") && ancestor.taskbar;
		bool stroke=element.name==L"BackgroundStroke" && ContainsText(element.type,L"Rectangle") && ancestor.taskbar;
		bool startControl=ContainsText(element.type,L"ExperienceToggleButton");
		bool icon=ancestor.startButton && IsStartGlyph(element.type,element.name);
		bool silence=startControl && style->replaceButton && (style->allTaskbars || !m_PrimaryStart || handle==m_PrimaryStart);
		if (icon && style->replaceButton && !style->allTaskbars && m_PrimaryStart && ancestor.startButtonHandle!=m_PrimaryStart)
			icon=false;
		if (startControl && !silence && !element.overridden)
			startControl=false;
		if (!fill && !stroke && !icon && !startControl)
			return S_FALSE;

		// The Windows button stays clickable after its glyph is collapsed, and on a
		// second taskbar that glyph is not always a child we can see. Hide the button
		// itself and take it out of hit testing. Opacity keeps its layout slot.
		if (startControl)
		{
			if (!silence)
			{
				HRESULT hrOpacity=SetPrimitive(handle,L"Opacity",L"1");
				if (hrOpacity==RPC_E_WRONG_THREAD)
					return hrOpacity;
				HRESULT hrHit=SetPrimitive(handle,L"IsHitTestVisible",L"True");
				if (hrHit==RPC_E_WRONG_THREAD)
					return hrHit;
				if (SUCCEEDED(hrOpacity) || SUCCEEDED(hrHit))
					Remember(handle,false);
				return SUCCEEDED(hrOpacity)?hrOpacity:hrHit;
			}
			HRESULT hrOpacity=SetPrimitive(handle,L"Opacity",L"0");
			if (hrOpacity==RPC_E_WRONG_THREAD)
				return hrOpacity;
			HRESULT hrHit=SetPrimitive(handle,L"IsHitTestVisible",L"False");
			if (hrHit==RPC_E_WRONG_THREAD)
				return hrHit;
			if (SUCCEEDED(hrOpacity) || SUCCEEDED(hrHit))
			{
				Remember(handle,true);
				static int silenced=0;
				if (silenced<4)
				{
					silenced++;
					LogToFile(STARTUP_LOG,L"Win11Taskbar: silenced start button %d opacity=0x%08X hit=0x%08X",silenced,hrOpacity,hrHit);
				}
			}
			else
				LogOnce(256,L"silence start button failed",FAILED(hrOpacity)?hrOpacity:hrHit);
			return SUCCEEDED(hrOpacity)?hrOpacity:hrHit;
		}

		if (icon)
		{
			if (!style->replaceButton)
			{
				if (!element.overridden)
					return S_FALSE;
				OsHandle visible=0;
				HRESULT hr=CreateValue(L".Visibility",L"Visible",&visible);
				if (FAILED(hr))
					return hr;
				hr=SetNamed(handle,L"Visibility",visible);
				if (hr==RPC_E_WRONG_THREAD)
					return hr;
				if (SUCCEEDED(hr))
					Remember(handle,false);
				return hr;
			}
			OsHandle collapsed=0;
			HRESULT hr=CreateValue(L".Visibility",L"Collapsed",&collapsed);
			if (FAILED(hr))
			{
				LogOnce(16,L"visibility value failed",hr);
				return hr;
			}
			hr=SetNamed(handle,L"Visibility",collapsed);
			if (SUCCEEDED(hr))
			{
				Remember(handle,true);
				static int hidCount=0;
				if (hidCount<8)
				{
					hidCount++;
					LogToFile(STARTUP_LOG,L"Win11Taskbar: hid start glyph %d type=%s name=%s",hidCount,(const wchar_t*)element.type,(const wchar_t*)element.name);
				}
			}
			else if (hr!=RPC_E_WRONG_THREAD)
				LogOnce(16,L"Start glyph visibility failed",hr);
			return hr;
		}

		if (!style->custom)
		{
			if (!element.overridden)
				return S_FALSE;
			HRESULT hr=ClearNamed(handle,L"Fill");
			if (hr==RPC_E_WRONG_THREAD)
				return hr;
			HRESULT hrStroke=ClearNamed(handle,L"Stroke");
			if (hrStroke==RPC_E_WRONG_THREAD)
				return hrStroke;
			if (SUCCEEDED(hr) || SUCCEEDED(hrStroke))
				Remember(handle,false);
			return SUCCEEDED(hr)?hr:hrStroke;
		}

		OsHandle brush=0;
		HRESULT hr=fill?FillBrush(style,&brush):EnsureClear(style,&brush);
		if (FAILED(hr))
			return hr;
		HRESULT hrFill=SetNamed(handle,L"Fill",brush);
		if (hrFill==RPC_E_WRONG_THREAD)
			return hrFill;
		OsHandle clear=0;
		HRESULT hrClear=EnsureClear(style,&clear);
		HRESULT hrStroke=S_OK;
		if (SUCCEEDED(hrClear))
		{
			hrStroke=SetNamed(handle,L"Stroke",clear);
			if (hrStroke==RPC_E_WRONG_THREAD)
				return hrStroke;
		}
		if (SUCCEEDED(hrFill) || SUCCEEDED(hrStroke))
		{
			Remember(handle,true);
			if (fill && style->hasTexture)
				LogOnce(64,L"replaced the taskbar background",hrFill);
			else if (fill)
				LogOnce(64,L"set the taskbar background",hrFill);
		}
		else if (hrFill!=HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
			LogOnce(128,L"background property failed",hrFill);
		return SUCCEEDED(hrFill)?hrFill:hrStroke;
	}

	LONG m_Refs;
	DWORD m_XamlThread;
	bool m_Pending;
	int m_Framework;
	OsHandle m_PrimaryStart;
	CComPtr<IUnknown> m_Site;
	CComPtr<IOsVisualTree> m_Visual;
	CComPtr<IOsDispatcherQueue> m_Dispatcher;
	CApplyHandler *m_Handler;
	std::unordered_map<OsHandle,OsElement> m_Elements;
	static int g_ElementLogs;
};

int CTaskbarTap::g_ElementLogs=0;

STDMETHODIMP CApplyHandler::Invoke( void )
{
	if (m_Tap)
		m_Tap->ApplyStyles();
	return S_OK;
}

class CTapFactory: public IClassFactory
{
public:
	CTapFactory( void ) { m_Refs=1; }
	STDMETHODIMP QueryInterface( REFIID riid, void **ppv )
	{
		if (!ppv) return E_POINTER;
		if (riid==IID_IUnknown || riid==IID_IClassFactory)
		{
			*ppv=static_cast<IClassFactory*>(this);
			AddRef();
			return S_OK;
		}
		*ppv=NULL;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef( void ) { return (ULONG)InterlockedIncrement(&m_Refs); }
	STDMETHODIMP_(ULONG) Release( void )
	{
		LONG left=InterlockedDecrement(&m_Refs);
		return (ULONG)left;
	}
	STDMETHODIMP CreateInstance( IUnknown *outer, REFIID riid, void **ppv )
	{
		if (ppv) *ppv=NULL;
		if (outer) return CLASS_E_NOAGGREGATION;
		CTaskbarTap *tap=CTaskbarTap::Create();
		if (!tap) return E_OUTOFMEMORY;
		return tap->QueryInterface(riid,ppv);
	}
	STDMETHODIMP LockServer( BOOL ) { return S_OK; }

private:
	LONG m_Refs;
};

static CTapFactory g_Factory;

// The Windows headers already declare DllGetClassObject as a dllimport.
// Export our own function under that name so the XAML diagnostics loader can find it.
extern "C" HRESULT STDMETHODCALLTYPE OpenShellDllGetClassObject( REFCLSID clsid, REFIID riid, LPVOID *ppv )
{
	if (!ppv) return E_POINTER;
	*ppv=NULL;
	if (!IsEqualGUID(clsid,CLSID_OpenShellTaskbarTap))
		return CLASS_E_CLASSNOTAVAILABLE;
	return g_Factory.QueryInterface(riid,ppv);
}
#pragma comment(linker, "/EXPORT:DllGetClassObject=OpenShellDllGetClassObject,PRIVATE")

static bool SameText( const wchar_t *left, const wchar_t *right )
{
	return _wcsicmp(left,right)==0;
}

static void ReadAccentSettings( bool *custom, int *look, int *opacity, COLORREF *color )
{
	WaitDllInitThread();
	*custom=GetSettingBool(L"CustomTaskbar");
	*look=GetSettingInt(L"TaskbarLook");
	*opacity=GetSettingInt(L"TaskbarOpacity");
	if (*look==TASKBAR_OPAQUE)
		*opacity=100;
	if (*opacity<0) *opacity=0;
	if (*opacity>100) *opacity=100;
	bool defaultColor=false;
	*color=GetSettingInt(L"TaskbarColor",defaultColor);
	if (defaultColor)
	{
		bool transparent=false;
		*color=GetMetroTaskbarColor(transparent);
	}
}

static OsSetWindowComposition g_SetWindowComposition=NULL;
static bool g_BridgeAccented=false;

static BOOL CALLBACK AccentChild( HWND hwnd, LPARAM param )
{
	wchar_t name[128];
	if (!GetClassName(hwnd,name,_countof(name)))
		return TRUE;
	if (wcscmp(name,L"Windows.UI.Composition.DesktopWindowContentBridge")!=0)
		return TRUE;
	if (g_SetWindowComposition)
		g_SetWindowComposition(hwnd,(OsCompAttrData*)param);
	return TRUE;
}

static BOOL CALLBACK AccentTray( HWND hwnd, LPARAM param )
{
	wchar_t name[64];
	if (!GetClassName(hwnd,name,_countof(name)))
		return TRUE;
	if (wcscmp(name,L"Shell_TrayWnd")==0 || wcscmp(name,L"Shell_SecondaryTrayWnd")==0)
		EnumChildWindows(hwnd,AccentChild,param);
	return TRUE;
}

static void ApplyBridgeAccent( void )
{
	if (!g_SetWindowComposition)
		g_SetWindowComposition=(OsSetWindowComposition)GetProcAddress(GetModuleHandle(L"user32.dll"),"SetWindowCompositionAttribute");
	if (!g_SetWindowComposition)
		return;

	bool custom=false;
	int look=TASKBAR_OPAQUE;
	int opacity=100;
	COLORREF color=0;
	ReadAccentSettings(&custom,&look,&opacity,&color);
	if (!custom)
	{
		if (!g_BridgeAccented)
			return;
		int clear[4]={ 0, 0, 0, 0 };
		OsCompAttrData attr={ 0x13, clear, sizeof(clear) };
		EnumWindows(AccentTray,(LPARAM)&attr);
		g_BridgeAccented=false;
		return;
	}

	int alpha=opacity*255/100;
	if (look==TASKBAR_OPAQUE)
		alpha=255;
	int data[4];
	data[0]=(look==TASKBAR_AEROGLASS)?0:(look+1);
	data[1]=0x13;
	data[2]=(color&0xFFFFFF)|(alpha<<24);
	data[3]=0;
	OsCompAttrData attr={ 0x13, data, sizeof(data) };
	EnumWindows(AccentTray,(LPARAM)&attr);
	g_BridgeAccented=true;
}

struct OsConnectAttempt
{
	wchar_t endpoint[96];
	wchar_t tapPath[MAX_PATH];
	HRESULT hr;
};

// The diagnostics API attaches to the visual tree of the process that calls it.
// Calling it from StartMenu.exe looks up Explorer and returns ERROR_NOT_FOUND.
// Each attempt needs its own thread: a second call on the same thread does not connect.
static DWORD WINAPI ConnectAttemptThread( LPVOID param )
{
	OsConnectAttempt *attempt=(OsConnectAttempt*)param;
	attempt->hr=E_FAIL;
	HMODULE runtime=LoadLibrary(L"Windows.UI.Xaml.dll");
	if (!runtime)
	{
		attempt->hr=HRESULT_FROM_WIN32(GetLastError());
		return 0;
	}
	OsInitXamlDiagnosticsEx init=(OsInitXamlDiagnosticsEx)GetProcAddress(runtime,"InitializeXamlDiagnosticsEx");
	if (!init)
	{
		attempt->hr=HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
		return 0;
	}
	attempt->hr=init(attempt->endpoint,GetCurrentProcessId(),NULL,attempt->tapPath,CLSID_OpenShellTaskbarTap,NULL);
	return 0;
}

static DWORD WINAPI ConnectLoopThread( LPVOID )
{
	wchar_t tapPath[MAX_PATH];
	HMODULE module=g_Instance?g_Instance:GetModuleHandle(L"StartMenuDLL.dll");
	if (!module || !GetModuleFileName(module,tapPath,_countof(tapPath)))
	{
		LogToFile(STARTUP_LOG,L"Win11Taskbar: StartMenuDLL path missing");
		return 0;
	}
	LogToFile(STARTUP_LOG,L"Win11Taskbar: connecting inside Explorer");
	HRESULT last=E_FAIL;
	// Windows.UI.Xaml registers ports named VisualDiagConnection1, VisualDiagConnection2, ...
	for (int attempt=1;attempt<=8;attempt++)
	{
		for (int port=1;port<=2;port++)
		{
			OsConnectAttempt data;
			memset(&data,0,sizeof(data));
			Sprintf(data.endpoint,_countof(data.endpoint),L"VisualDiagConnection%d",port);
			Strcpy(data.tapPath,_countof(data.tapPath),tapPath);
			data.hr=E_FAIL;
			HANDLE thread=CreateThread(NULL,0,ConnectAttemptThread,&data,0,NULL);
			if (!thread)
			{
				last=HRESULT_FROM_WIN32(GetLastError());
				LogToFile(STARTUP_LOG,L"Win11Taskbar: connect thread failed 0x%08X",last);
				return 0;
			}
			WaitForSingleObject(thread,INFINITE);
			CloseHandle(thread);
			last=data.hr;
			if (SUCCEEDED(last))
			{
				LogToFile(STARTUP_LOG,L"Win11Taskbar: in-process connect 0x%08X %s",last,data.endpoint);
				return 0;
			}
			if (attempt<=2 || attempt==8)
				LogToFile(STARTUP_LOG,L"Win11Taskbar: in-process connect 0x%08X %s try %d",last,data.endpoint,attempt);
		}
		Sleep(500);
	}
	LogToFile(STARTUP_LOG,L"Win11Taskbar: in-process connect failed 0x%08X",last);
	return 0;
}

void StartWin11TaskbarConnect( void )
{
	if (!IsWin11())
		return;
	wchar_t exe[MAX_PATH];
	if (!GetModuleFileName(NULL,exe,_countof(exe)))
		return;
	if (!SameText(PathFindFileName(exe),L"explorer.exe"))
		return;
	static LONG started=0;
	if (InterlockedCompareExchange(&started,1,0)!=0)
		return;
	HANDLE thread=CreateThread(NULL,0,ConnectLoopThread,NULL,0,NULL);
	if (thread)
		CloseHandle(thread);
	else
		InterlockedExchange(&started,0);
}

STARTMENUAPI void ConnectWin11Taskbar( DWORD explorerPid )
{
	if (!explorerPid)
		return;
	StartWin11TaskbarConnect();
}

void ApplyWin11Taskbar( void )
{
	if (!IsWin11())
		return;
	if (t_ApplyDepth)
		return;
	ApplyBridgeAccent();
	CTaskbarTap *tap=CTaskbarTap::Existing();
	if (tap)
		tap->RequestApply();
}
