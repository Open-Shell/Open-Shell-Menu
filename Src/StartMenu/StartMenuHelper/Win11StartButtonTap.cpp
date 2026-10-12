// Windows 11 native Start button suppression.
//
// Open-Shell's replacement button is a separate layered window. Windows 11
// renders its own Start button in XAML, so the old HWND hiding logic cannot
// remove the native glyph. This file uses the public XAML diagnostics API to
// suppress only the native Start glyph and its hit target. The XAML element is
// left in layout, so centered taskbar positioning remains owned by Windows.

#include "stdafx.h"
#include "Win11StartButtonTap.h"
#include "StartMenuHelper_h.h"
#include "dllmain.h"
#include "Settings.h"
#include "StringUtils.h"
#include "..\StartMenuDLL\LogManager.h"

#undef GetCurrentTime
#include <Windows.UI.Xaml.h>
#include <xamlom.h>
#include <ocidl.h>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

// global settings
static std::atomic_bool g_StartButtonEnabled{ false };
static std::atomic_bool g_AllTaskbars{ false };

// TAP instance and mutex for global access.
static CComPtr<class CWin11StartButtonTap> g_Tap;
static std::mutex g_TapMutex;
static std::atomic_bool g_ConnectionInProgress{ false };


static HMODULE GetThisModule( void )
{
	HMODULE module = NULL;
	GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCTSTR)&GetThisModule, &module);
	return module;
}

struct StartElement
{
	InstanceHandle parent = 0;
	CString type;
	CString name;
	bool visibilityOverride = false;
	bool hitTestOverride = false;
	bool startControlResolved = false;
	bool isStartControl = false;
	unsigned int discoveryOrder = 0;
};

static bool ContainsText( const CString &text, const wchar_t *part )
{
	return text.Find(part) >= 0;
}

static bool IsStartControlCandidate( const StartElement &element )
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

template<typename F>
class ScopeExit
{
public:
	explicit ScopeExit(F&& f) : f_(std::forward<F>(f)) {}
	~ScopeExit() { f_(); }

private:
	F f_;
};

template<typename F>
ScopeExit<F> scope_exit(F&& f)
{
	return ScopeExit<F>(std::forward<F>(f));
}

static const GUID CLSID_OpenShellStartButtonTap = { 0x7d15741f, 0x2f3b, 0x4971, { 0xb8, 0x91, 0x6a, 0x5d, 0x42, 0xd7, 0x1a, 0x34 } };

class ATL_NO_VTABLE CWin11StartButtonTap :
	public CComObjectRootEx<CComMultiThreadModel>,
	public CComCoClass<CWin11StartButtonTap, &CLSID_OpenShellStartButtonTap>,
	public IObjectWithSite,
	public IVisualTreeServiceCallback2
{
public:
	BEGIN_COM_MAP(CWin11StartButtonTap)
		COM_INTERFACE_ENTRY(IObjectWithSite)
		COM_INTERFACE_ENTRY(IVisualTreeServiceCallback)
		COM_INTERFACE_ENTRY(IVisualTreeServiceCallback2)
	END_COM_MAP()

	DECLARE_NO_REGISTRY()
	DECLARE_NOT_AGGREGATABLE(CWin11StartButtonTap)

	CWin11StartButtonTap()
	{
		m_DispatchWindow.Create(HWND_MESSAGE, nullptr, nullptr, 0, 0, 0U, this);
	}

	~CWin11StartButtonTap()
	{
		if (m_DispatchWindow.IsWindow())
			m_DispatchWindow.DestroyWindow();
	}

	STDMETHODIMP SetSite( IUnknown *site )
	{
		// This is called by XAML Diagnostics to establish the connection.
		// It is expected to be called just once (with valid site) and then once more (with NULL) to disconnect.

		// Make sure to reset the connection flag once we are finished (either successfully or with an error)
		auto cleanup = scope_exit([]() {
			g_ConnectionInProgress = false;
		});

		if (m_Site)
			return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);

		m_Site = site;
		if (!site)
			return S_OK;

		CComPtr<IVisualTreeService> visual;
		HRESULT hr = site->QueryInterface(__uuidof(IVisualTreeService), (void**)&visual);
		if (FAILED(hr))
			return hr;

		// Advise replays the existing tree. The replay only records handles;
		// no property work is queued until m_AllowEnable becomes true below.
		hr = visual->AdviseVisualTreeChange(static_cast<IVisualTreeServiceCallback*>(this));
		if (FAILED(hr))
			return hr;

		// Store the visual service so that we can Unadvise later. The service is also used to query property values and set overrides.
		m_Visual = visual;

		// Enable the TAP to queue work. This is done after Advise so that the initial replay does not queue any work.
		m_AllowEnable = true;

		// Queue an initial apply to ensure that any existing Start button is suppressed.
		RequestApply(g_StartButtonEnabled);

		// We have a valid connection; store the TAP instance for global access.
		{
			std::unique_lock lock(g_TapMutex);
			g_Tap = this;
		}

		return S_OK;
	}

	STDMETHODIMP GetSite( REFIID riid, void **ppv )
	{
		if (!ppv)
			return E_POINTER;
		*ppv = NULL;

		return m_Site->QueryInterface(riid, ppv);
	}

	STDMETHODIMP OnVisualTreeChange( ParentChildRelation relation, VisualElement element, VisualMutationType mutationType )
	{
		// VisualElement is an [in] parameter. The XAML diagnostics runtime owns
		// the BSTR fields; copy the values we need but never free callback input.
		bool interesting = false;

		{
			std::lock_guard lock(m_Mutex);
			if (mutationType == Remove)
			{
				auto it = m_Elements.find(element.Handle);
				if (it != m_Elements.end())
				{
					interesting = it->second.visibilityOverride || it->second.hitTestOverride ||
						it->second.isStartControl || IsStartControlCandidate(it->second);
					bool wasPrimary = element.Handle == m_PrimaryStart;
					m_Elements.erase(it);
					if (wasPrimary)
						m_PrimaryStart = FindPrimaryStartLocked();
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
					record.startControlResolved = previous->second.startControlResolved;
					record.isStartControl = previous->second.isStartControl;
					record.discoveryOrder = previous->second.discoveryOrder;
				}
				else
				{
					record.discoveryOrder = ++m_NextDiscoveryOrder;
				}
				m_Elements[element.Handle] = record;

				interesting = IsStartControlCandidate(record) || IsStartGlyph(record) ||
					FindStartAncestorLocked(record.parent) != 0;
			}
		}

		// AdviseVisualTreeChange replays the existing tree synchronously. Do not
		// queue partial-state work during that replay; ActivateLocked performs one
		// complete apply after Advise has returned and the dispatch window exists.
		if (interesting && m_AllowEnable)
			RequestApply(g_StartButtonEnabled);

		return S_OK;
	}

	STDMETHODIMP OnElementStateChanged( InstanceHandle, VisualElementState, LPCWSTR )
	{
		return S_OK;
	}

	void Activate()
	{
		RequestApply(g_StartButtonEnabled);
	}

	HRESULT Deactivate()
	{
		// Don't queue any more work.
		m_AllowEnable = false;

		if (m_Visual)
		{
			// Unadvise the TAP to stop receiving tree change notifications.
			// This will also free reference to our TAP instance (that was held by XAML Diagnostics) and allow the TAP to be destroyed.
			HRESULT hr = m_Visual->UnadviseVisualTreeChange(static_cast<IVisualTreeServiceCallback*>(this));
			if (FAILED(hr))
			{
				LogToFile(STARTUP_LOG, L"Win11StartButtonTap: visual tree unadvise failed 0x%08X", hr);
				return hr;
			}
		}

		// Destroy the dispatch window to reject any already-queued apply work.
		// Also no new work will get queued because m_AllowEnable is false and the TAP is no longer advised.
		m_DispatchWindow.DestroyWindow();

		// Restore the original state of any elements that were modified.
		ApplyState(false);

		return S_OK;
	}

private:

	void RequestApply( bool enabled )
	{
		if (m_DispatchWindow.IsWindow())
			m_DispatchWindow.PostMessage(WM_OS_STARTBUTTON_APPLY, enabled ? 1 : 0);
	}

	// Caller holds m_Mutex. Both event classification and property
	// application use the same bounded Start-control ancestry lookup.
	InstanceHandle FindStartAncestorLocked( InstanceHandle parent ) const
	{
		for (int depth = 0; depth < 24 && parent; depth++)
		{
			auto it = m_Elements.find(parent);
			if (it == m_Elements.end())
				break;
			if (it->second.isStartControl)
				return parent;
			parent = it->second.parent;
		}
		return 0;
	}

	InstanceHandle GetStartAncestor( InstanceHandle handle )
	{
		std::lock_guard lock(m_Mutex);
		auto it = m_Elements.find(handle);
		return it == m_Elements.end() ? 0 : FindStartAncestorLocked(it->second.parent);
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

	InstanceHandle FindPrimaryStartLocked( void ) const
	{
		InstanceHandle primary = 0;
		unsigned int bestOrder = 0;
		for (auto it = m_Elements.begin(); it != m_Elements.end(); ++it)
		{
			if (!it->second.isStartControl)
				continue;
			if (!primary || it->second.discoveryOrder < bestOrder)
			{
				primary = it->first;
				bestOrder = it->second.discoveryOrder;
			}
		}
		return primary;
	}

	HRESULT ResolveStartControl( InstanceHandle handle, const StartElement &element, bool *isStartControl )
	{
		*isStartControl = false;
		if (!IsStartControlCandidate(element))
			return S_OK;

		// Older taskbar implementations may expose a distinct x:Name.
		if (element.name.CompareNoCase(L"StartButton") == 0)
		{
			*isStartControl = true;
			return S_OK;
		}

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

		for (unsigned int i = 0; i < valueCount; i++)
		{
			if (!values[i].PropertyName || !values[i].Value)
				continue;
			if (_wcsicmp(values[i].PropertyName, L"AutomationId") != 0 &&
				_wcsicmp(values[i].PropertyName, L"AutomationProperties.AutomationId") != 0)
				continue;
			if (_wcsicmp(values[i].Value, L"StartButton") == 0)
			{
				*isStartControl = true;
				break;
			}
		}

		FreeProperties(sources, sourceCount, values, valueCount);
		return S_OK;
	}

	void SetControlClassification( InstanceHandle handle, bool isStartControl )
	{
		std::lock_guard lock(m_Mutex);
		auto it = m_Elements.find(handle);
		if (it != m_Elements.end())
		{
			it->second.startControlResolved = true;
			it->second.isStartControl = isStartControl;
			m_PrimaryStart = FindPrimaryStartLocked();
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
		std::lock_guard lock(m_Mutex);
		auto it = m_Elements.find(handle);
		if (it != m_Elements.end())
		{
			if (visibility)
				it->second.visibilityOverride = *visibility;
			if (hitTest)
				it->second.hitTestOverride = *hitTest;
		}
	}

	HRESULT ApplyState( bool enabled )
	{
		if (!m_Visual)
			return E_UNEXPECTED;

		HRESULT firstError = S_OK;
		std::vector<std::pair<InstanceHandle, StartElement>> elements;
		{
			std::lock_guard lock(m_Mutex);
			elements.reserve(m_Elements.size());
			for (const auto &element : m_Elements)
				elements.push_back(element);
		}

		// Classification is only needed when applying overrides. During teardown,
		// use the classifications that produced the existing overrides; querying
		// new candidates adds risk and work without helping restoration.
		if (enabled)
		{
			for (size_t i = 0; i < elements.size(); i++)
			{
				StartElement &record = elements[i].second;
				if (!IsStartControlCandidate(record) || record.startControlResolved)
					continue;

				bool isStartControl = false;
				HRESULT hr = ResolveStartControl(elements[i].first, record, &isStartControl);
				if (SUCCEEDED(hr))
				{
					record.startControlResolved = true;
					record.isStartControl = isStartControl;
					SetControlClassification(elements[i].first, isStartControl);
				}
				else if (SUCCEEDED(firstError))
				{
					firstError = hr;
				}
			}
		}

		InstanceHandle primaryStart = 0;
		{
			std::lock_guard lock(m_Mutex);
			primaryStart = m_PrimaryStart;
		}

		const bool allTaskbars = g_AllTaskbars;

		// Both properties share the same set/restore behavior.
		auto applyOverride = [&](InstanceHandle handle, const wchar_t *property, const wchar_t *value,
			bool shouldOverride, bool hasOverride, bool visibility)
		{
			if (shouldOverride == hasOverride)
				return;
			HRESULT hr = shouldOverride ? SetPropertyText(handle, property, value) :
				ClearPropertyByName(handle, property);
			if (SUCCEEDED(hr))
			{
				bool updated = shouldOverride;
				if (visibility)
					SetOverrideFlags(handle, &updated, NULL);
				else
					SetOverrideFlags(handle, NULL, &updated);
			}
			else if (SUCCEEDED(firstError))
				firstError = hr;
		};

		for (size_t i = 0; i < elements.size(); i++)
		{
			InstanceHandle handle = elements[i].first;
			StartElement record = elements[i].second;

			// Restoration depends on what we changed, not on whether a
			// dynamically rebuilt XAML tree still has the same ancestry.
			if (!enabled)
			{
				if (record.hitTestOverride)
					applyOverride(handle, L"IsHitTestVisible", L"False", false, true, false);
				if (record.visibilityOverride)
					applyOverride(handle, L"Visibility", L"Collapsed", false, true, true);
				continue;
			}
			if (record.isStartControl)
			{
				bool target = allTaskbars || !primaryStart || handle == primaryStart;
				applyOverride(handle, L"IsHitTestVisible", L"False",
					enabled && target, record.hitTestOverride, false);
				continue;
			}

			if (!IsStartGlyph(record))
				continue;
			InstanceHandle startAncestor = GetStartAncestor(handle);
			if (!startAncestor)
				continue;
			bool target = allTaskbars || !primaryStart || startAncestor == primaryStart;
			applyOverride(handle, L"Visibility", L"Collapsed",
				enabled && target, record.visibilityOverride, true);
		}

		return firstError;
	}

	class DispatchWindow : public CWindowImpl<DispatchWindow>
	{
	public:
		//	DECLARE_WND_CLASS(L"OpenShell.ExplorerTap")
		BEGIN_MSG_MAP(DispatchWindow)
			MESSAGE_HANDLER(WM_NCCREATE, OnNcCreate)
			MESSAGE_HANDLER(WM_OS_STARTBUTTON_APPLY, OnApply)
		END_MSG_MAP()
		LRESULT OnNcCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
		{
			CREATESTRUCT* create = (CREATESTRUCT*)lParam;
			SetWindowLongPtr(GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
			return TRUE;
		}
		LRESULT OnApply(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
		{
			CWin11StartButtonTap* tap = (CWin11StartButtonTap*)GetWindowLongPtr(GWLP_USERDATA);
			if (tap)
				tap->ApplyState(wParam != 0);
			return TRUE;
		}
	};

	static const UINT WM_OS_STARTBUTTON_APPLY = WM_APP + 0x35B;

	DispatchWindow m_DispatchWindow;
	CComPtr<IUnknown> m_Site;
	CComPtr<IVisualTreeService> m_Visual;
	std::atomic_bool m_AllowEnable{ false };

	std::mutex m_Mutex; // Protects the site reference and visual-element state.
	InstanceHandle m_PrimaryStart = 0;
	unsigned int m_NextDiscoveryOrder = 0;
	// Keep the replayed ancestry while subscribed. Parent links are required to
	// recognize glyphs under a Start control, and the map is cleared on unadvise.
	std::unordered_map<InstanceHandle, StartElement> m_Elements;
};

OBJECT_ENTRY_AUTO(CLSID_OpenShellStartButtonTap, CWin11StartButtonTap)


typedef HRESULT (WINAPI *InitXamlDiagnosticsEx_t)( LPCWSTR, DWORD, LPCWSTR, LPCWSTR, CLSID, LPCWSTR );

static DWORD WINAPI ConnectThread( LPVOID param )
{
	HMODULE moduleReference = (HMODULE)param;
	HRESULT last = E_FAIL;
	HMODULE runtime = LoadLibraryEx(L"Windows.UI.Xaml.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (runtime)
	{
		InitXamlDiagnosticsEx_t init = (InitXamlDiagnosticsEx_t)GetProcAddress(runtime, "InitializeXamlDiagnosticsEx");
		if (init)
		{
			HMODULE module = GetThisModule();
			wchar_t dllPath[MAX_PATH];
			DWORD pathLength = module ? GetModuleFileName(module, dllPath, _countof(dllPath)) : 0;
			if (pathLength > 0 && pathLength < _countof(dllPath))
			{
				for (int retry = 0; retry < 8; retry++)
				{
					last = init(L"VisualDiagConnection1", GetCurrentProcessId(), L"", dllPath, CLSID_OpenShellStartButtonTap, NULL);
					if (SUCCEEDED(last))
					{
						LogToFile(STARTUP_LOG, L"Win11StartButton: connected (%d attempts)", retry + 1);
						break;
					}

					Sleep(500);
				}
			}
		}
		FreeLibrary(runtime);
	}

	if (FAILED(last))
	{
		LogToFile(STARTUP_LOG, L"Win11StartButton: connection failed 0x%08X", last);

		// Allow connection attempts to be retried on the next occurrence if the thread creation failed.
		g_ConnectionInProgress = false;
	}

	// Release the worker's module reference atomically with thread termination.
	FreeLibraryAndExitThread(moduleReference, 0);
	return 0;
}

static void EnsureConnection()
{
	// Connection probing retries endpoints and may sleep, so keep it off the
	// Explorer taskbar thread. ConnectThread is the only worker we need here.

	// Prevent multiple concurrent connection attempts. If a previous attempt is still in progress, let it complete and retry on the next occurrence.
	bool expected = false;
	if (!g_ConnectionInProgress.compare_exchange_strong(expected, true))
		return;

	HMODULE moduleReference = NULL;
	if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCTSTR)&ConnectThread, &moduleReference))
	{
		HANDLE thread = CreateThread(NULL, 0, ConnectThread, moduleReference, 0, NULL);
		if (thread)
		{
			CloseHandle(thread);
			// The worker thread owns the module reference and will free it on exit.
			return;
		}

		FreeLibrary(moduleReference);
	}

	// Allow connection attempts to be retried on the next occurrence if the thread creation failed.
	g_ConnectionInProgress = false;
}


extern "C" void UpdateWin11StartButtonTap( BOOL enabled, BOOL allTaskbars )
{
	g_StartButtonEnabled = enabled != FALSE;
	g_AllTaskbars = allTaskbars != FALSE;

	CComPtr<CWin11StartButtonTap> tap;
	{
		std::unique_lock lock(g_TapMutex);
		tap = g_Tap;
	}

	if (tap)
	{
		tap->Activate();
	}
	else
	{
		if (enabled)
			EnsureConnection();
	}
}

extern "C" void StopWin11StartButtonTap( void )
{
	CComPtr<CWin11StartButtonTap> tap;
	{
		std::unique_lock lock(g_TapMutex);
		tap = g_Tap;
		g_Tap = nullptr;
	}

	if (tap)
	{
		if (SUCCEEDED(tap->Deactivate()))
		{
			// XAML diagnostics runtime loaded the TAP module and owns a reference for the diagnostics session. Release it now that the TAP is no longer advised.
			FreeLibrary(GetThisModule());
		}
	}
}
