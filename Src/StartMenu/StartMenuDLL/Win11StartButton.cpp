// Windows 11 native Start button suppression.
//
// Open-Shell's replacement button is a separate layered window. Windows 11
// renders its own Start button in XAML, so the old HWND hiding logic cannot
// remove the native glyph. This file uses the public XAML diagnostics API to
// suppress only the native Start glyph and its hit target. The XAML element is
// left in layout, so centered taskbar positioning remains owned by Windows.

#include "stdafx.h"
#include "Win11StartButton.h"
#include "Settings.h"
#include "LogManager.h"
#include "ResourceHelper.h"

#include <Windows.UI.Xaml.h>
#include <xamlom.h>
#include <ocidl.h>
#include <unordered_map>
#include <vector>

static const GUID CLSID_OpenShellStartButtonTap =
{ 0x7d15741f, 0x2f3b, 0x4971, { 0xb8, 0x91, 0x6a, 0x5d, 0x42, 0xd7, 0x1a, 0x34 } };

static const UINT WM_OS_STARTBUTTON_APPLY = WM_APP + 0x35B;

static volatile LONG g_StartButtonActive = 0;
static volatile LONG g_ConnectStarted = 0;

static HMODULE GetThisModule( void )
{
	HMODULE module = NULL;
	GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCTSTR)&GetThisModule, &module);
	return module;
}

struct StartElement
{
	InstanceHandle parent;
	CString type;
	CString name;
	bool visibilityOverride;
	bool hitTestOverride;

	StartElement( void )
	{
		parent = 0;
		visibilityOverride = false;
		hitTestOverride = false;
	}
};

class CWin11StartButtonTap;
static CWin11StartButtonTap *g_Tap = NULL;
static SRWLOCK g_TapLock = SRWLOCK_INIT;

static bool ContainsText( const CString &text, const wchar_t *part )
{
	return text.Find(part) >= 0;
}

static bool IsStartControl( const StartElement &element )
{
	if (!ContainsText(element.type, L"ExperienceToggleButton"))
		return false;
	return element.name == L"LaunchListButton" || element.name == L"StartButton";
}

static bool IsStartGlyph( const StartElement &element )
{
	if (element.name == L"Icon")
		return true;
	if (ContainsText(element.type, L"AnimatedVisualPlayer") || ContainsText(element.type, L"AepAnimatedIcon"))
		return true;
	if (ContainsText(element.type, L"FontIcon") || ContainsText(element.type, L"PathIcon") ||
		ContainsText(element.type, L"ImageIcon") || ContainsText(element.type, L"BitmapIcon") ||
		ContainsText(element.type, L"SymbolIcon"))
		return true;
	return false;
}

class CWin11StartButtonTap: public IObjectWithSite, public IVisualTreeServiceCallback2
{
public:
	CWin11StartButtonTap( void )
	{
		m_Refs = 1;
		m_Advised = false;
		m_Dispatch = NULL;
		m_PrimaryStart = 0;
		InitializeCriticalSection(&m_Lock);
	}

	~CWin11StartButtonTap( void )
	{
		if (m_Visual && m_Advised)
			m_Visual->UnadviseVisualTreeChange(static_cast<IVisualTreeServiceCallback*>(this));
		if (m_Dispatch && GetWindowThreadProcessId(m_Dispatch, NULL) == GetCurrentThreadId())
			DestroyWindow(m_Dispatch);

		AcquireSRWLockExclusive(&g_TapLock);
		if (g_Tap == this)
			g_Tap = NULL;
		ReleaseSRWLockExclusive(&g_TapLock);

		InterlockedExchange(&g_ConnectStarted, 0);
		DeleteCriticalSection(&m_Lock);
	}

	STDMETHODIMP QueryInterface( REFIID riid, void **ppv )
	{
		if (!ppv)
			return E_POINTER;
		*ppv = NULL;

		if (riid == IID_IUnknown || riid == IID_IObjectWithSite)
			*ppv = static_cast<IObjectWithSite*>(this);
		else if (riid == __uuidof(IVisualTreeServiceCallback) || riid == __uuidof(IVisualTreeServiceCallback2))
			*ppv = static_cast<IVisualTreeServiceCallback2*>(this);
		else
			return E_NOINTERFACE;

		AddRef();
		return S_OK;
	}

	STDMETHODIMP_(ULONG) AddRef( void )
	{
		return (ULONG)InterlockedIncrement(&m_Refs);
	}

	STDMETHODIMP_(ULONG) Release( void )
	{
		LONG refs = InterlockedDecrement(&m_Refs);
		if (!refs)
			delete this;
		return (ULONG)refs;
	}

	STDMETHODIMP SetSite( IUnknown *site )
	{
		if (m_Visual && m_Advised)
		{
			ApplyState(false);
			m_Visual->UnadviseVisualTreeChange(static_cast<IVisualTreeServiceCallback*>(this));
			m_Advised = false;
		}
		m_Visual.Release();
		m_Site.Release();

		if (!site)
		{
			AcquireSRWLockExclusive(&g_TapLock);
			if (g_Tap == this)
				g_Tap = NULL;
			ReleaseSRWLockExclusive(&g_TapLock);
			InterlockedExchange(&g_ConnectStarted, 0);
			return S_OK;
		}

		EnterCriticalSection(&m_Lock);
		m_Elements.clear();
		m_PrimaryStart = 0;
		LeaveCriticalSection(&m_Lock);

		m_Site = site;
		HRESULT hr = site->QueryInterface(__uuidof(IVisualTreeService), (void**)&m_Visual);
		if (FAILED(hr) || !m_Visual)
			return hr;

		if (!CreateDispatchWindow())
			return HRESULT_FROM_WIN32(GetLastError());

		AcquireSRWLockExclusive(&g_TapLock);
		g_Tap = this;
		ReleaseSRWLockExclusive(&g_TapLock);

		// Advise replays the existing tree. OnVisualTreeChange only records
		// element handles; all property access is dispatched afterwards.
		hr = m_Visual->AdviseVisualTreeChange(static_cast<IVisualTreeServiceCallback*>(this));
		if (SUCCEEDED(hr))
		{
			m_Advised = true;
			RequestApply(false);
		}
		LogToFile(STARTUP_LOG, L"Win11StartButton: visual tree advise 0x%08X", hr);
		return hr;
	}

	STDMETHODIMP GetSite( REFIID riid, void **ppv )
	{
		if (!ppv)
			return E_POINTER;
		*ppv = NULL;
		if (!m_Site)
			return E_FAIL;
		return m_Site->QueryInterface(riid, ppv);
	}

	STDMETHODIMP OnVisualTreeChange( ParentChildRelation relation, VisualElement element, VisualMutationType mutationType )
	{
		bool interesting = false;

		EnterCriticalSection(&m_Lock);
		if (mutationType == Remove)
		{
			auto it = m_Elements.find(element.Handle);
			if (it != m_Elements.end())
			{
				interesting = it->second.visibilityOverride || it->second.hitTestOverride || IsStartControl(it->second);
				bool wasPrimary = element.Handle == m_PrimaryStart;
				m_Elements.erase(it);
				if (wasPrimary)
				{
					m_PrimaryStart = 0;
					for (auto candidate = m_Elements.begin(); candidate != m_Elements.end(); ++candidate)
					{
						if (IsStartControl(candidate->second))
						{
							m_PrimaryStart = candidate->first;
							break;
						}
					}
				}
			}
		}
		else if (mutationType == Add)
		{
			StartElement record;
			record.parent = relation.Parent;
			record.type = element.Type ? element.Type : L"";
			record.name = element.Name ? element.Name : L"";

			auto previous = m_Elements.find(element.Handle);
			if (previous != m_Elements.end())
			{
				record.visibilityOverride = previous->second.visibilityOverride;
				record.hitTestOverride = previous->second.hitTestOverride;
			}
			m_Elements[element.Handle] = record;
			if (IsStartControl(record) && !m_PrimaryStart)
				m_PrimaryStart = element.Handle;

			interesting = IsStartControl(record) || IsStartGlyph(record) || IsUnderStartButtonLocked(record.parent);
		}
		LeaveCriticalSection(&m_Lock);

		if (interesting)
			RequestApply(false);
		return S_OK;
	}

	STDMETHODIMP OnElementStateChanged( InstanceHandle, VisualElementState, LPCWSTR )
	{
		return S_OK;
	}

	void RequestApply( bool synchronous )
	{
		if (!m_Dispatch)
			return;
		if (synchronous)
			SendMessage(m_Dispatch, WM_OS_STARTBUTTON_APPLY, 0, 0);
		else
			PostMessage(m_Dispatch, WM_OS_STARTBUTTON_APPLY, 0, 0);
	}

private:
	static LRESULT CALLBACK DispatchProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
	{
		CWin11StartButtonTap *tap = (CWin11StartButtonTap*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
		if (msg == WM_NCCREATE)
		{
			CREATESTRUCT *create = (CREATESTRUCT*)lParam;
			tap = (CWin11StartButtonTap*)create->lpCreateParams;
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)tap);
		}
		if (msg == WM_OS_STARTBUTTON_APPLY && tap)
		{
			bool enabled = InterlockedCompareExchange(&g_StartButtonActive, 0, 0) != 0 &&
				GetSettingBool(L"EnableStartButton");
			tap->ApplyState(enabled);
			return 0;
		}
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	bool CreateDispatchWindow( void )
	{
		if (m_Dispatch)
			return true;

		static const wchar_t CLASS_NAME[] = L"OpenShell.Win11StartButtonTap";
		WNDCLASS wc = {};
		HMODULE module = GetThisModule();
		if (!module)
			return false;

		wc.lpfnWndProc = DispatchProc;
		wc.hInstance = module;
		wc.lpszClassName = CLASS_NAME;
		if (!RegisterClass(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			return false;

		m_Dispatch = CreateWindowEx(0, CLASS_NAME, L"", 0, 0, 0, 0, 0,
			HWND_MESSAGE, NULL, module, this);
		return m_Dispatch != NULL;
	}

	bool IsUnderStartButtonLocked( InstanceHandle parent ) const
	{
		for (int depth = 0; depth < 24 && parent; depth++)
		{
			auto it = m_Elements.find(parent);
			if (it == m_Elements.end())
				break;
			if (IsStartControl(it->second))
				return true;
			parent = it->second.parent;
		}
		return false;
	}

	InstanceHandle GetStartAncestor( InstanceHandle handle )
	{
		InstanceHandle result = 0;
		EnterCriticalSection(&m_Lock);
		auto it = m_Elements.find(handle);
		if (it != m_Elements.end())
		{
			InstanceHandle parent = it->second.parent;
			for (int depth = 0; depth < 24 && parent; depth++)
			{
				auto pit = m_Elements.find(parent);
				if (pit == m_Elements.end())
					break;
				if (IsStartControl(pit->second))
				{
					result = parent;
					break;
				}
				parent = pit->second.parent;
			}
		}
		LeaveCriticalSection(&m_Lock);
		return result;
	}

	static void FreeProperties( PropertyChainSource *sources, unsigned int sourceCount,
		PropertyChainValue *values, unsigned int valueCount )
	{
		if (sources)
		{
			for (unsigned int i = 0; i < sourceCount; i++)
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
			for (unsigned int i = 0; i < valueCount; i++)
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

	HRESULT FindProperty( InstanceHandle handle, const wchar_t *name, unsigned int *index, CString *typeName )
	{
		unsigned int sourceCount = 0;
		unsigned int valueCount = 0;
		PropertyChainSource *sources = NULL;
		PropertyChainValue *values = NULL;
		HRESULT hr = m_Visual->GetPropertyValuesChain(handle, &sourceCount, &sources, &valueCount, &values);
		if (FAILED(hr))
		{
			FreeProperties(sources, sourceCount, values, valueCount);
			return hr;
		}

		bool found = false;
		for (unsigned int i = 0; i < valueCount; i++)
		{
			if (!values[i].PropertyName || wcscmp(values[i].PropertyName, name) != 0)
				continue;
			if (values[i].MetadataBits & 0x2) // IsPropertyReadOnly
				continue;

			*index = values[i].Index;
			if (typeName)
				*typeName = values[i].Type ? values[i].Type : L"";
			found = true;
			break;
		}
		FreeProperties(sources, sourceCount, values, valueCount);
		return found ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
	}

	HRESULT SetPropertyText( InstanceHandle handle, const wchar_t *name, const wchar_t *valueText )
	{
		unsigned int index = 0;
		CString typeName;
		HRESULT hr = FindProperty(handle, name, &index, &typeName);
		if (FAILED(hr) || typeName.IsEmpty())
			return FAILED(hr) ? hr : E_FAIL;

		CComBSTR type(typeName);
		CComBSTR value(valueText);
		InstanceHandle created = 0;
		hr = m_Visual->CreateInstance(type, value, &created);
		if (FAILED(hr))
			return hr;
		return m_Visual->SetProperty(handle, created, index);
	}

	HRESULT ClearPropertyByName( InstanceHandle handle, const wchar_t *name )
	{
		unsigned int index = 0;
		HRESULT hr = FindProperty(handle, name, &index, NULL);
		if (FAILED(hr))
			return hr;
		return m_Visual->ClearProperty(handle, index);
	}

	void SetOverrideFlags( InstanceHandle handle, bool *visibility, bool *hitTest )
	{
		EnterCriticalSection(&m_Lock);
		auto it = m_Elements.find(handle);
		if (it != m_Elements.end())
		{
			if (visibility)
				it->second.visibilityOverride = *visibility;
			if (hitTest)
				it->second.hitTestOverride = *hitTest;
		}
		LeaveCriticalSection(&m_Lock);
	}

	void ApplyState( bool enabled )
	{
		if (!m_Visual)
			return;

		std::vector<std::pair<InstanceHandle, StartElement>> elements;
		InstanceHandle primaryStart = 0;
		EnterCriticalSection(&m_Lock);
		primaryStart = m_PrimaryStart;
		for (auto it = m_Elements.begin(); it != m_Elements.end(); ++it)
			elements.push_back(*it);
		LeaveCriticalSection(&m_Lock);

		const bool allTaskbars = GetSettingBool(L"AllTaskbars");

		for (size_t i = 0; i < elements.size(); i++)
		{
			InstanceHandle handle = elements[i].first;
			StartElement record = elements[i].second;

			if (IsStartControl(record))
			{
				bool target = allTaskbars || !primaryStart || handle == primaryStart;
				if (enabled && target)
				{
					if (!record.hitTestOverride)
					{
						HRESULT hr = SetPropertyText(handle, L"IsHitTestVisible", L"False");
						if (SUCCEEDED(hr))
						{
							bool value = true;
							SetOverrideFlags(handle, NULL, &value);
						}
					}
				}
				else if (record.hitTestOverride)
				{
					HRESULT hr = ClearPropertyByName(handle, L"IsHitTestVisible");
					if (SUCCEEDED(hr))
					{
						bool value = false;
						SetOverrideFlags(handle, NULL, &value);
					}
				}
				continue;
			}

			if (!IsStartGlyph(record))
				continue;

			InstanceHandle startAncestor = GetStartAncestor(handle);
			if (!startAncestor)
				continue;
			bool target = allTaskbars || !primaryStart || startAncestor == primaryStart;

			if (enabled && target)
			{
				if (!record.visibilityOverride)
				{
					HRESULT hr = SetPropertyText(handle, L"Visibility", L"Collapsed");
					if (SUCCEEDED(hr))
					{
						bool value = true;
						SetOverrideFlags(handle, &value, NULL);
					}
				}
			}
			else if (record.visibilityOverride)
			{
				HRESULT hr = ClearPropertyByName(handle, L"Visibility");
				if (SUCCEEDED(hr))
				{
					bool value = false;
					SetOverrideFlags(handle, &value, NULL);
				}
			}
		}
	}

	LONG m_Refs;
	bool m_Advised;
	HWND m_Dispatch;
	CRITICAL_SECTION m_Lock;
	CComPtr<IUnknown> m_Site;
	CComPtr<IVisualTreeService> m_Visual;
	InstanceHandle m_PrimaryStart;
	std::unordered_map<InstanceHandle, StartElement> m_Elements;
};

static CWin11StartButtonTap *GetTapRef( void )
{
	CWin11StartButtonTap *tap = NULL;
	AcquireSRWLockShared(&g_TapLock);
	tap = g_Tap;
	if (tap)
		tap->AddRef();
	ReleaseSRWLockShared(&g_TapLock);
	return tap;
}

class CStartButtonTapFactory: public IClassFactory
{
public:
	CStartButtonTapFactory( void ) { m_Refs = 1; }

	STDMETHODIMP QueryInterface( REFIID riid, void **ppv )
	{
		if (!ppv)
			return E_POINTER;
		*ppv = NULL;
		if (riid != IID_IUnknown && riid != IID_IClassFactory)
			return E_NOINTERFACE;
		*ppv = static_cast<IClassFactory*>(this);
		AddRef();
		return S_OK;
	}

	STDMETHODIMP_(ULONG) AddRef( void ) { return (ULONG)InterlockedIncrement(&m_Refs); }
	STDMETHODIMP_(ULONG) Release( void ) { return (ULONG)InterlockedDecrement(&m_Refs); }

	STDMETHODIMP CreateInstance( IUnknown *outer, REFIID riid, void **ppv )
	{
		if (!ppv)
			return E_POINTER;
		*ppv = NULL;
		if (outer)
			return CLASS_E_NOAGGREGATION;

		CWin11StartButtonTap *tap = new CWin11StartButtonTap();
		if (!tap)
			return E_OUTOFMEMORY;
		HRESULT hr = tap->QueryInterface(riid, ppv);
		tap->Release();
		return hr;
	}

	STDMETHODIMP LockServer( BOOL ) { return S_OK; }

private:
	LONG m_Refs;
};

static CStartButtonTapFactory g_Factory;

// xamlom.h declares DllGetClassObject through the platform headers, so export
// our implementation under that name via a linker alias.
extern "C" HRESULT STDMETHODCALLTYPE OpenShellStartButtonDllGetClassObject( REFCLSID clsid, REFIID riid, LPVOID *ppv )
{
	if (!IsEqualGUID(clsid, CLSID_OpenShellStartButtonTap))
		return CLASS_E_CLASSNOTAVAILABLE;
	return g_Factory.QueryInterface(riid, ppv);
}

#ifdef _M_IX86
#pragma comment(linker, "/EXPORT:DllGetClassObject=_OpenShellStartButtonDllGetClassObject@12,PRIVATE")
#else
#pragma comment(linker, "/EXPORT:DllGetClassObject=OpenShellStartButtonDllGetClassObject,PRIVATE")
#endif

typedef HRESULT (WINAPI *InitXamlDiagnosticsEx_t)( LPCWSTR, DWORD, LPCWSTR, LPCWSTR, CLSID, LPCWSTR );

struct ConnectAttempt
{
	InitXamlDiagnosticsEx_t init;
	const wchar_t *endpoint;
	wchar_t dllPath[MAX_PATH];
	HRESULT hr;
};

static DWORD WINAPI ConnectAttemptThread( LPVOID param )
{
	ConnectAttempt *attempt = (ConnectAttempt*)param;
	attempt->hr = attempt->init(attempt->endpoint, GetCurrentProcessId(), NULL,
		attempt->dllPath, CLSID_OpenShellStartButtonTap, NULL);
	return 0;
}

static DWORD WINAPI ConnectThread( LPVOID )
{
	HMODULE runtime = LoadLibraryEx(L"Windows.UI.Xaml.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!runtime)
	{
		InterlockedExchange(&g_ConnectStarted, 0);
		return 0;
	}

	InitXamlDiagnosticsEx_t init = (InitXamlDiagnosticsEx_t)GetProcAddress(runtime, "InitializeXamlDiagnosticsEx");
	if (!init)
	{
		FreeLibrary(runtime);
		InterlockedExchange(&g_ConnectStarted, 0);
		return 0;
	}

	HMODULE module = GetThisModule();
	wchar_t dllPath[MAX_PATH];
	if (!module || !GetModuleFileName(module, dllPath, _countof(dllPath)))
	{
		FreeLibrary(runtime);
		InterlockedExchange(&g_ConnectStarted, 0);
		return 0;
	}

	const wchar_t *endpoints[] = { L"VisualDiagConnection1", L"VisualDiagConnection2" };
	HRESULT last = E_FAIL;
	for (int retry = 0; retry < 8 && InterlockedCompareExchange(&g_StartButtonActive, 0, 0); retry++)
	{
		for (int i = 0; i < _countof(endpoints); i++)
		{
			ConnectAttempt attempt = {};
			attempt.init = init;
			attempt.endpoint = endpoints[i];
			Strcpy(attempt.dllPath, _countof(attempt.dllPath), dllPath);
			attempt.hr = E_FAIL;

			HANDLE thread = CreateThread(NULL, 0, ConnectAttemptThread, &attempt, 0, NULL);
			if (!thread)
				continue;
			WaitForSingleObject(thread, INFINITE);
			CloseHandle(thread);
			last = attempt.hr;
			if (SUCCEEDED(last))
			{
				LogToFile(STARTUP_LOG, L"Win11StartButton: connected using %s", endpoints[i]);
				FreeLibrary(runtime);
				return 0;
			}
		}
		Sleep(500);
	}

	LogToFile(STARTUP_LOG, L"Win11StartButton: connection failed 0x%08X", last);
	FreeLibrary(runtime);
	InterlockedExchange(&g_ConnectStarted, 0);
	return 0;
}

static void EnsureConnection( void )
{
	if (InterlockedCompareExchange(&g_ConnectStarted, 1, 0) != 0)
		return;

	HANDLE thread = CreateThread(NULL, 0, ConnectThread, NULL, 0, NULL);
	if (thread)
		CloseHandle(thread);
	else
		InterlockedExchange(&g_ConnectStarted, 0);
}

void StartWin11StartButtonMonitor( void )
{
	if (!IsWin11())
		return;

	InterlockedExchange(&g_StartButtonActive, 1);
	CWin11StartButtonTap *tap = GetTapRef();
	if (tap)
	{
		tap->RequestApply(false);
		tap->Release();
		return;
	}
	EnsureConnection();
}

void UpdateWin11StartButtonMonitor( void )
{
	if (!IsWin11())
		return;

	CWin11StartButtonTap *tap = GetTapRef();
	if (tap)
	{
		tap->RequestApply(false);
		tap->Release();
	}
	else if (InterlockedCompareExchange(&g_StartButtonActive, 0, 0))
	{
		EnsureConnection();
	}
}

void StopWin11StartButtonMonitor( void )
{
	if (!IsWin11())
		return;

	InterlockedExchange(&g_StartButtonActive, 0);
	CWin11StartButtonTap *tap = GetTapRef();
	if (tap)
	{
		// Synchronous restore: don't let Open-Shell exit while the native
		// Start button is still carrying our XAML overrides.
		tap->RequestApply(true);
		tap->Release();
	}
}
