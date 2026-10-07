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
#include <shared_mutex>
#include <unordered_map>
#include <vector>

static const GUID CLSID_OpenShellStartButtonTap =
{ 0x7d15741f, 0x2f3b, 0x4971, { 0xb8, 0x91, 0x6a, 0x5d, 0x42, 0xd7, 0x1a, 0x34 } };

static const UINT WM_OS_STARTBUTTON_APPLY = WM_APP + 0x35B;
static const UINT WM_OS_STARTBUTTON_DESTROY = WM_APP + 0x35C;

static std::atomic_bool g_StartButtonActive{ false };
static std::atomic_bool g_StartButtonEnabled{ false };
static std::atomic_bool g_AllTaskbars{ false };
static std::atomic_bool g_ConnectStarted{ false };

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

class CWin11StartButtonTap;
static CWin11StartButtonTap *g_Tap = NULL;
static std::shared_mutex g_TapMutex;

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

class CWin11StartButtonTap: public IObjectWithSite, public IVisualTreeServiceCallback2
{
public:
	CWin11StartButtonTap( void )
	{
		_AtlModule.Lock();
	}

	~CWin11StartButtonTap( void )
	{
		{
			std::unique_lock lock(g_TapMutex);
			if (g_Tap == this)
				g_Tap = NULL;
		}

		{
			std::lock_guard lock(m_LifecycleMutex);
			if (m_Visual && m_Advised)
			{
				HRESULT hr = m_Visual->UnadviseVisualTreeChange(static_cast<IVisualTreeServiceCallback*>(this));
				if (FAILED(hr))
					LogToFile(STARTUP_LOG, L"Win11StartButtonTap: final visual tree unadvise failed 0x%08X", hr);
				else
					m_Advised = false;
			}

			ResetElements();
			HRESULT hr = DestroyDispatchWindow();
			if (FAILED(hr))
			{
				// Never leave a window pointing at an object that is being destroyed.
				HWND dispatch = m_Dispatch;
				if (dispatch)
					SetWindowLongPtr(dispatch, GWLP_USERDATA, 0);
				m_Dispatch = NULL;
			}

			m_Visual.Release();
			m_Site.Release();
		}

		g_ConnectStarted = false;
		_AtlModule.Unlock();
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
		return ++m_Refs;
	}

	STDMETHODIMP_(ULONG) Release( void )
	{
		ULONG refs = --m_Refs;
		if (!refs)
			delete this;
		return refs;
	}

	STDMETHODIMP SetSite( IUnknown *site )
	{
		std::lock_guard lifecycleLock(m_LifecycleMutex);

		// A diagnostics endpoint can call SetSite again on the same TAP object.
		// Tear down the previous subscription first so callbacks never outlive
		// the site/service state they were registered against.
		HRESULT hr = DeactivateLocked();
		if (FAILED(hr))
			return hr;

		{
			std::unique_lock lock(g_TapMutex);
			if (g_Tap == this)
				g_Tap = NULL;
		}

		m_Visual.Release();
		m_Site.Release();
		ResetElements();

		if (!site)
		{
			g_ConnectStarted = false;
			return S_OK;
		}

		CComPtr<IVisualTreeService> newVisual;
		hr = site->QueryInterface(__uuidof(IVisualTreeService), (void**)&newVisual);
		if (FAILED(hr))
			return hr;
		if (!newVisual)
			return E_NOINTERFACE;

		m_Site = site;
		m_Visual = newVisual;

		{
			std::unique_lock lock(g_TapMutex);
			g_Tap = this;
		}

		// Stop can race with the asynchronous diagnostics connection. In that
		// case retain the site for a later restart but do not subscribe yet.
		if (!g_StartButtonActive)
			return S_OK;

		hr = ActivateLocked();
		if (FAILED(hr))
			LogToFile(STARTUP_LOG, L"Win11StartButtonTap: activation failed 0x%08X", hr);
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
					IsUnderStartButtonLocked(record.parent);
			}
		}

		// AdviseVisualTreeChange replays the existing tree synchronously. Do not
		// queue partial-state work during that replay; ActivateLocked performs one
		// complete apply after Advise has returned and the dispatch window exists.
		if (interesting && m_Advised)
			RequestApply(false, g_StartButtonActive && g_StartButtonEnabled);
		return S_OK;
	}

	STDMETHODIMP OnElementStateChanged( InstanceHandle, VisualElementState, LPCWSTR )
	{
		return S_OK;
	}

	HRESULT Activate( void )
	{
		std::lock_guard lock(m_LifecycleMutex);
		return ActivateLocked();
	}

	HRESULT Deactivate( void )
	{
		std::lock_guard lock(m_LifecycleMutex);
		return DeactivateLocked();
	}

private:
	void ResetElements( void )
	{
		std::lock_guard lock(m_Mutex);
		m_Elements.clear();
		m_PrimaryStart = 0;
		m_NextDiscoveryOrder = 0;
	}

	HRESULT RequestApply( bool synchronous, bool enabled )
	{
		HWND dispatch = m_Dispatch;
		if (!dispatch || !IsWindow(dispatch))
			return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);

		if (!synchronous)
		{
			if (!PostMessage(dispatch, WM_OS_STARTBUTTON_APPLY, enabled ? 1 : 0, 0))
				return HRESULT_FROM_WIN32(GetLastError());
			return S_OK;
		}

		DWORD_PTR result = 0;
		SetLastError(ERROR_SUCCESS);
		LRESULT sent = SendMessageTimeout(dispatch, WM_OS_STARTBUTTON_APPLY, enabled ? 1 : 0, 0,
			SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result);
		if (sent)
			return static_cast<HRESULT>(result);

		DWORD error = GetLastError();
		if (!error)
			error = ERROR_TIMEOUT;
		return HRESULT_FROM_WIN32(error);
	}

	HRESULT ActivateLocked( void )
	{
		if (!m_Visual)
			return E_UNEXPECTED;

		if (!m_Advised)
		{
			ResetElements();

			// Advise replays the existing tree. The replay only records handles;
			// no property work is queued until m_Advised becomes true below.
			HRESULT hr = m_Visual->AdviseVisualTreeChange(static_cast<IVisualTreeServiceCallback*>(this));
			if (FAILED(hr))
			{
				ResetElements();
				return hr;
			}
			m_Advised = true;
		}

		HRESULT hr = CreateDispatchWindow();
		if (FAILED(hr))
		{
			// If the callback was registered by this activation, undo it. If
			// unadvise itself fails, keep the subscription state intact so a
			// later activation can retry only the dispatch-window creation.
			if (m_Visual && m_Advised)
			{
				HRESULT unadvise = m_Visual->UnadviseVisualTreeChange(
					static_cast<IVisualTreeServiceCallback*>(this));
				if (SUCCEEDED(unadvise))
				{
					m_Advised = false;
					ResetElements();
				}
				else
				{
					LogToFile(STARTUP_LOG,
						L"Win11StartButtonTap: rollback unadvise failed 0x%08X", unadvise);
				}
			}
			return hr;
		}

		m_AllowEnable = true;
		return RequestApply(false, g_StartButtonActive && g_StartButtonEnabled);
	}

	HRESULT DeactivateLocked( void )
	{
		// Reject any already-queued enable work before restoring state. This is
		// independent of the global active flag so SetSite replacement is safe
		// even while Open-Shell itself remains active.
		m_AllowEnable = false;

		// Restore our XAML overrides before removing the callback. If the UI
		// thread is unavailable, keep the subscription alive rather than leave
		// the native Start button hidden with no path left to restore it.
		HWND dispatch = m_Dispatch;
		if (dispatch && IsWindow(dispatch))
		{
			HRESULT hr = RequestApply(true, false);
			if (FAILED(hr))
			{
				LogToFile(STARTUP_LOG,
					L"Win11StartButtonTap: synchronous restore failed 0x%08X", hr);
				return hr;
			}
		}

		if (m_Visual && m_Advised)
		{
			HRESULT hr = m_Visual->UnadviseVisualTreeChange(
				static_cast<IVisualTreeServiceCallback*>(this));
			if (FAILED(hr))
			{
				LogToFile(STARTUP_LOG,
					L"Win11StartButtonTap: visual tree unadvise failed 0x%08X", hr);
				return hr;
			}
			m_Advised = false;
		}

		ResetElements();
		return DestroyDispatchWindow();
	}

	HRESULT CreateDispatchWindow( void )
	{
		HWND dispatch = m_Dispatch;
		if (dispatch)
		{
			if (IsWindow(dispatch))
				return S_OK;
			m_Dispatch = NULL;
		}

		static const wchar_t CLASS_NAME[] = L"OpenShell.Win11StartButtonTap";
		WNDCLASS wc = {};
		HMODULE module = GetThisModule();
		if (!module)
			return E_FAIL;

		wc.lpfnWndProc = DispatchProc;
		wc.hInstance = module;
		wc.lpszClassName = CLASS_NAME;
		if (!RegisterClass(&wc))
		{
			DWORD error = GetLastError();
			if (error != ERROR_CLASS_ALREADY_EXISTS)
				return HRESULT_FROM_WIN32(error);
		}

		dispatch = CreateWindowEx(0, CLASS_NAME, L"", 0, 0, 0, 0, 0,
			HWND_MESSAGE, NULL, module, this);
		if (!dispatch)
			return HRESULT_FROM_WIN32(GetLastError());

		m_Dispatch = dispatch;
		return S_OK;
	}

	HRESULT DestroyDispatchWindow( void )
	{
		HWND dispatch = m_Dispatch;
		if (!dispatch)
			return S_OK;
		if (!IsWindow(dispatch))
		{
			m_Dispatch = NULL;
			return S_OK;
		}

		if (GetWindowThreadProcessId(dispatch, NULL) == GetCurrentThreadId())
		{
			SetWindowLongPtr(dispatch, GWLP_USERDATA, 0);
			if (!DestroyWindow(dispatch))
				return HRESULT_FROM_WIN32(GetLastError());
			m_Dispatch = NULL;
			return S_OK;
		}

		DWORD_PTR result = 0;
		SetLastError(ERROR_SUCCESS);
		LRESULT sent = SendMessageTimeout(dispatch, WM_OS_STARTBUTTON_DESTROY, 0, 0,
			SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result);
		if (!sent && IsWindow(dispatch))
		{
			DWORD error = GetLastError();
			if (!error)
				error = ERROR_TIMEOUT;
			return HRESULT_FROM_WIN32(error);
		}

		if (IsWindow(dispatch))
			return E_FAIL;

		m_Dispatch = NULL;
		return S_OK;
	}

	static LRESULT CALLBACK DispatchProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
	{
		CWin11StartButtonTap *tap = (CWin11StartButtonTap*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
		if (msg == WM_NCCREATE)
		{
			CREATESTRUCT *create = (CREATESTRUCT*)lParam;
			tap = (CWin11StartButtonTap*)create->lpCreateParams;
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)tap);
		}
		if (msg == WM_OS_STARTBUTTON_DESTROY)
		{
			SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
			DestroyWindow(hwnd);
			return 0;
		}
		if (msg == WM_OS_STARTBUTTON_APPLY && tap)
		{
			// A queued enable request must never re-apply overrides once teardown
			// has started, even if Open-Shell is replacing the diagnostics site.
			bool enabled = wParam != 0 && tap->m_AllowEnable &&
				g_StartButtonActive && g_StartButtonEnabled;
			return static_cast<LRESULT>(tap->ApplyState(enabled));
		}
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	bool IsUnderStartButtonLocked( InstanceHandle parent ) const
	{
		for (int depth = 0; depth < 24 && parent; depth++)
		{
			auto it = m_Elements.find(parent);
			if (it == m_Elements.end())
				break;
			if (it->second.isStartControl)
				return true;
			parent = it->second.parent;
		}
		return false;
	}

	InstanceHandle GetStartAncestor( InstanceHandle handle )
	{
		std::lock_guard lock(m_Mutex);
		InstanceHandle result = 0;
		auto it = m_Elements.find(handle);
		if (it != m_Elements.end())
		{
			InstanceHandle parent = it->second.parent;
			for (int depth = 0; depth < 24 && parent; depth++)
			{
				auto pit = m_Elements.find(parent);
				if (pit == m_Elements.end())
					break;
				if (pit->second.isStartControl)
				{
					result = parent;
					break;
				}
				parent = pit->second.parent;
			}
		}
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

		for (size_t i = 0; i < elements.size(); i++)
		{
			InstanceHandle handle = elements[i].first;
			StartElement record = elements[i].second;

			if (record.isStartControl)
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
						else if (SUCCEEDED(firstError))
						{
							firstError = hr;
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
					else if (SUCCEEDED(firstError))
					{
						firstError = hr;
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
					else if (SUCCEEDED(firstError))
					{
						firstError = hr;
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
				else if (SUCCEEDED(firstError))
				{
					firstError = hr;
				}
			}
		}

		return firstError;
	}

	std::atomic<ULONG> m_Refs{ 1 };
	std::atomic_bool m_Advised{ false };
	std::atomic_bool m_AllowEnable{ false };
	std::atomic<HWND> m_Dispatch{ NULL };
	std::mutex m_LifecycleMutex;
	std::mutex m_Mutex;
	CComPtr<IUnknown> m_Site;
	CComPtr<IVisualTreeService> m_Visual;
	InstanceHandle m_PrimaryStart = 0;
	unsigned int m_NextDiscoveryOrder = 0;
	// Keep the replayed ancestry while subscribed. Parent links are required to
	// recognize glyphs under a Start control, and the map is cleared on unadvise.
	std::unordered_map<InstanceHandle, StartElement> m_Elements;
};

static CComPtr<CWin11StartButtonTap> GetTapRef( void )
{
	std::shared_lock lock(g_TapMutex);
	return CComPtr<CWin11StartButtonTap>(g_Tap);
}

class CStartButtonTapFactory: public IClassFactory
{
public:
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

	STDMETHODIMP_(ULONG) AddRef( void ) { return ++m_Refs; }
	STDMETHODIMP_(ULONG) Release( void ) { return --m_Refs; }

	STDMETHODIMP CreateInstance( IUnknown *outer, REFIID riid, void **ppv )
	{
		if (!ppv)
			return E_POINTER;
		*ppv = NULL;
		if (outer)
			return CLASS_E_NOAGGREGATION;

		CComPtr<CWin11StartButtonTap> tap;
		tap.Attach(new CWin11StartButtonTap());
		if (!tap)
			return E_OUTOFMEMORY;
		return tap->QueryInterface(riid, ppv);
	}

	STDMETHODIMP LockServer( BOOL lock )
	{
		if (lock)
			_AtlModule.Lock();
		else
			_AtlModule.Unlock();
		return S_OK;
	}

private:
	std::atomic<ULONG> m_Refs{ 1 };
};

static CStartButtonTapFactory g_Factory;

HRESULT GetWin11StartButtonTapClassObject( REFCLSID clsid, REFIID riid, LPVOID *ppv )
{
	if (!IsEqualGUID(clsid, CLSID_OpenShellStartButtonTap))
		return CLASS_E_CLASSNOTAVAILABLE;
	return g_Factory.QueryInterface(riid, ppv);
}

typedef HRESULT (WINAPI *InitXamlDiagnosticsEx_t)( LPCWSTR, DWORD, LPCWSTR, LPCWSTR, CLSID, LPCWSTR );

static DWORD FinishConnectThread( HMODULE moduleReference, HMODULE runtime, bool connected )
{
	if (runtime)
		FreeLibrary(runtime);
	if (!connected)
		g_ConnectStarted = false;

	// Keep a private StartMenuHelper reference while this worker is running.
	// Release it atomically with thread termination so a failed diagnostics
	// connection cannot unload the helper underneath the worker's return path.
	FreeLibraryAndExitThread(moduleReference, 0);
	return 0;
}

static DWORD WINAPI ConnectThread( LPVOID param )
{
	HMODULE moduleReference = (HMODULE)param;
	HMODULE runtime = LoadLibraryEx(L"Windows.UI.Xaml.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!runtime)
		return FinishConnectThread(moduleReference, NULL, false);

	InitXamlDiagnosticsEx_t init = (InitXamlDiagnosticsEx_t)GetProcAddress(runtime, "InitializeXamlDiagnosticsEx");
	if (!init)
		return FinishConnectThread(moduleReference, runtime, false);

	HMODULE module = GetThisModule();
	wchar_t dllPath[MAX_PATH];
	if (!module || !GetModuleFileName(module, dllPath, _countof(dllPath)))
		return FinishConnectThread(moduleReference, runtime, false);

	const wchar_t *endpoints[] = { L"VisualDiagConnection1", L"VisualDiagConnection2" };
	HRESULT last = E_FAIL;
	for (int retry = 0; retry < 8 && g_StartButtonActive; retry++)
	{
		for (int i = 0; i < _countof(endpoints); i++)
		{
			last = init(endpoints[i], GetCurrentProcessId(), NULL,
				dllPath, CLSID_OpenShellStartButtonTap, NULL);
			if (SUCCEEDED(last))
			{
				LogToFile(STARTUP_LOG, L"Win11StartButton: connected using %s", endpoints[i]);
				return FinishConnectThread(moduleReference, runtime, true);
			}
		}
		Sleep(500);
	}

	LogToFile(STARTUP_LOG, L"Win11StartButton: connection failed 0x%08X", last);
	return FinishConnectThread(moduleReference, runtime, false);
}

static void EnsureConnection( void )
{
	// Connection probing retries endpoints and may sleep, so keep it off the
	// Explorer taskbar thread. ConnectThread is the only worker we need here.
	bool expected = false;
	if (!g_ConnectStarted.compare_exchange_strong(expected, true))
		return;

	HMODULE moduleReference = NULL;
	if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCTSTR)&ConnectThread, &moduleReference))
	{
		HANDLE thread = CreateThread(NULL, 0, ConnectThread, moduleReference, 0, NULL);
		if (thread)
		{
			CloseHandle(thread);
			return;
		}
		FreeLibrary(moduleReference);
	}

	// A worker was not started, so connection can be attempted again later.
	g_ConnectStarted = false;
}

extern "C" void StartWin11StartButtonTap( BOOL enabled, BOOL allTaskbars )
{
	g_StartButtonEnabled = enabled != FALSE;
	g_AllTaskbars = allTaskbars != FALSE;
	g_StartButtonActive = true;

	auto tap = GetTapRef();
	if (tap)
	{
		HRESULT hr = tap->Activate();
		if (FAILED(hr))
			LogToFile(STARTUP_LOG, L"Win11StartButtonTap: start activation failed 0x%08X", hr);
		return;
	}

	EnsureConnection();
}

extern "C" void UpdateWin11StartButtonTap( BOOL enabled, BOOL allTaskbars )
{
	g_StartButtonEnabled = enabled != FALSE;
	g_AllTaskbars = allTaskbars != FALSE;

	if (!g_StartButtonActive)
		return;

	auto tap = GetTapRef();
	if (tap)
	{
		HRESULT hr = tap->Activate();
		if (FAILED(hr))
			LogToFile(STARTUP_LOG, L"Win11StartButtonTap: update activation failed 0x%08X", hr);
	}
	else
	{
		EnsureConnection();
	}
}

extern "C" void StopWin11StartButtonTap( void )
{
	g_StartButtonActive = false;

	auto tap = GetTapRef();
	if (tap)
	{
		HRESULT hr = tap->Deactivate();
		if (FAILED(hr))
			LogToFile(STARTUP_LOG, L"Win11StartButtonTap: deactivate failed 0x%08X", hr);
	}
}
