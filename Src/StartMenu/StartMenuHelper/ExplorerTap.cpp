// Windows 11 draws the native Taskbar and Start button in XAML.
// We use XAML Diagnostics TAP to modify the XAML tree.

// This is the TAP implementation that is loaded into Explorer's process by XAML Diagnostics.
// It allows us to examine XAML tree changes and modify properties of XAML elements.

// Currently we support:
// - Hiding the Start button glyph (the icon) and disabling hit testing on the Start button control.

#include "stdafx.h"
#include "ExplorerTap.h"
#include "Settings.h"
#include "StringUtils.h"
#include "..\StartMenuDLL\LogManager.h"

#undef GetCurrentTime
#include <Windows.UI.Xaml.h>
#include <windows.ui.xaml.media.h>
#include <windows.foundation.h>
#include <xamlom.h>
#include <ocidl.h>
#include <atomic>
#include <mutex>
#include <vector>

#include <roapi.h>
#include <wrl/wrappers/corewrappers.h>
using namespace Microsoft::WRL::Wrappers;


// TAP instance and mutex for global access.
static std::mutex g_tapMutex;
static CComPtr<class ExplorerTap> g_tap;
static ExplorerTapSettings g_tapSettings{};

static std::atomic_bool g_connectionInProgress{ false };


static HMODULE GetThisModule()
{
	HMODULE module = NULL;
	GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCTSTR)&GetThisModule, &module);
	return module;
}

// scope_exit is a simple RAII helper that executes a callable on destruction.
template<typename F>
class scope_exit
{
public:
	explicit scope_exit(F&& f) : f_(std::forward<F>(f)) {}
	~scope_exit() { f_(); }

private:
	F f_;
};

namespace wux = ABI::Windows::UI::Xaml;
namespace wf = ABI::Windows::Foundation;

static const GUID CLSID_OpenShellExplorerTap = { 0x7d15741f, 0x2f3b, 0x4971, { 0xb8, 0x91, 0x6a, 0x5d, 0x42, 0xd7, 0x1a, 0x34 } };

class ATL_NO_VTABLE ExplorerTap :
	public CComObjectRootEx<CComMultiThreadModel>,
	public CComCoClass<ExplorerTap, &CLSID_OpenShellExplorerTap>,
	public IObjectWithSite,
	public IVisualTreeServiceCallback2
{
public:
	BEGIN_COM_MAP(ExplorerTap)
		COM_INTERFACE_ENTRY(IObjectWithSite)
		COM_INTERFACE_ENTRY(IVisualTreeServiceCallback)
		COM_INTERFACE_ENTRY(IVisualTreeServiceCallback2)
	END_COM_MAP()

	DECLARE_NO_REGISTRY()
	DECLARE_NOT_AGGREGATABLE(ExplorerTap)

	ExplorerTap()
	{
		RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Media_VisualTreeHelper).Get(), IID_PPV_ARGS(&m_visualTreeHelper));
		m_dispatchWindow.Create(HWND_MESSAGE, nullptr, nullptr, 0, 0, 0U, this);
	}

	~ExplorerTap()
	{
		if (m_dispatchWindow.IsWindow())
			m_dispatchWindow.DestroyWindow();
	}

	STDMETHODIMP SetSite(IUnknown *site)
	{
		// This is called by XAML Diagnostics to establish the connection.
		// It is expected to be called just once (with valid site) and then once more (with NULL) to disconnect.

		// Make sure to reset the connection flag once we are finished (either successfully or with an error)
		auto cleanup = scope_exit([]() {
			g_connectionInProgress = false;
		});

		if (!site)
			return S_OK;

		if (m_site)
			return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);

		HRESULT hr = site->QueryInterface(IID_PPV_ARGS(&m_site));
		if (FAILED(hr))
			return hr;

		hr = site->QueryInterface(IID_PPV_ARGS(&m_visualTreeService));
		if (FAILED(hr))
			return hr;

		// Advise replays the existing tree.
		hr = m_visualTreeService->AdviseVisualTreeChange(static_cast<IVisualTreeServiceCallback*>(this));
		if (FAILED(hr))
			return hr;

		// We have a valid connection; store the TAP instance for global access.
		{
			std::unique_lock lock(g_tapMutex);
			g_tap = this;
		}

		return S_OK;
	}

	STDMETHODIMP GetSite(REFIID riid, void **ppv)
	{
		if (!ppv)
			return E_POINTER;
		*ppv = NULL;

		return m_site->QueryInterface(riid, ppv);
	}

	STDMETHODIMP OnVisualTreeChange(ParentChildRelation relation, VisualElement element, VisualMutationType mutationType)
	{
		bool interesting = false;

		{
			if (mutationType == Remove)
			{
				std::lock_guard lock(m_mutex);
				auto it = std::find(m_elements.begin(), m_elements.end(), element.Handle);
				if (it != m_elements.end())
				{
					m_elements.erase(it);
					interesting = true;
				}
			}
			else if (mutationType == Add)
			{
				if (IsStartControl(element))
				{
					// Start button control was added.
					StartElement record;
					record.handle = element.Handle;

					std::lock_guard lock(m_mutex);
					m_elements.push_back(record);

					interesting = true;
				}
				else if (InstanceHandle parent; IsStartGlyph(element, parent))
				{
					// Start glyph was added.
					// Store glyph handle to the parent Start button record so we can hide it later.
					std::lock_guard lock(m_mutex);
					auto it = std::find(m_elements.begin(), m_elements.end(), parent);
					if (it != m_elements.end())
					{
						it->glyph = element.Handle;
						it->visibilityOverride = false; // A newly added glyph has no override yet.
						interesting = true;
					}
				}
			}
		}

		if (interesting)
			RequestApply();

		return S_OK;
	}

	STDMETHODIMP OnElementStateChanged(InstanceHandle, VisualElementState, LPCWSTR)
	{
		return S_OK;
	}

	void Update()
	{
		// Called when button settings were chaged.
		// Apply state according to current settings.
		RequestApply();
	}

	HRESULT Deactivate()
	{
		// Unadvise the TAP to stop receiving tree change notifications.
		// This will also free reference to our TAP instance (that was held by XAML Diagnostics) and allow the TAP to be destroyed.
		HRESULT hr = m_visualTreeService->UnadviseVisualTreeChange(static_cast<IVisualTreeServiceCallback*>(this));
		if (FAILED(hr))
		{
			LogToFile(STARTUP_LOG, L"Win11StartButtonTap: visual tree unadvise failed 0x%08X", hr);
			return hr;
		}

		// Destroy the dispatch window to reject any already-queued apply work.
		// Also no new work will get queued because the TAP is no longer advised.
		m_dispatchWindow.DestroyWindow();

		// Restore the original state of any elements that were modified.
		{
			std::unique_lock lock(g_tapMutex);
			g_tapSettings.startButtonEnabled = FALSE;
		}
		ApplyState();

		return S_OK;
	}

private:

	bool IsStartControl(const VisualElement& element)
	{
		if (!element.Type || !element.Name)
			return false;

		// Type has to contain "ExperienceToggleButton" (the Start button is a subclass of that type).
		if (wcsstr(element.Type, L"ExperienceToggleButton"))
		{
			// Older taskbar implementations may expose a distinct x:Name.
			if (_wcsicmp(element.Name, L"StartButton") == 0)
				return true;

			// Newer taskbar implementation uses a LaunchListButton with AutomationId "StartButton" (the glyph is a child Icon element).
			if (wcscmp(element.Name, L"LaunchListButton") == 0)
			{
				auto automationId = GetPropertyString(element.Handle, L"Windows.UI.Xaml.Automation.AutomationProperties.AutomationId");
				if (automationId.CompareNoCase(L"StartButton") == 0)
					return true;
			}
		}

		return false;
	}

	bool IsStartGlyph(const VisualElement& element, InstanceHandle& parent)
	{
		if (!element.Name)
			return false;

		// The Start glyph is an Icon element that is a descendant of a Start button control.
		if (wcscmp(element.Name, L"Icon") == 0)
		{
			auto ancestor = FindStartAncestor(element.Handle);
			if (ancestor)
			{
				parent = ancestor;
				return true;
			}
		}

		return false;
	}

	void RequestApply()
	{
		if (m_dispatchWindow.IsWindow())
			m_dispatchWindow.PostMessage(WM_OS_STARTBUTTON_APPLY, 0);
	}

	void ApplyState()
	{
		ExplorerTapSettings settings{};
		{
			std::unique_lock lock(g_tapMutex);
			settings = g_tapSettings;
		}

		const bool enabled = settings.startButtonEnabled;
		const bool allTaskbars = settings.startButtonAllTaskbars;

		// Both properties share the same set/restore behavior.
		auto applyOverride = [&](InstanceHandle handle, const wchar_t* property, const wchar_t* value, const wchar_t* valueType, bool shouldOverride, bool& hasOverride)
			{
				if (shouldOverride != hasOverride)
				{
					HRESULT hr = shouldOverride ? SetPropertyText(handle, property, value, valueType) : ClearPropertyByName(handle, property);
					if (SUCCEEDED(hr))
						hasOverride = shouldOverride;
				}
			};

		// Apply the requested state to all known Start button elements.
		std::lock_guard lock(m_mutex);
		for (size_t i = 0; i < m_elements.size(); i++)
		{
			auto& element = m_elements[i];

			bool target = allTaskbars || (i == 0); // Only the first Start button is the main taskbar; all others are secondary taskbars.
			applyOverride(element.handle, L"Windows.UI.Xaml.UIElement.IsHitTestVisible", L"False", L"Windows.Foundation.Boolean", enabled && target, element.hitTestOverride);
			applyOverride(element.glyph, L"Windows.UI.Xaml.UIElement.Visibility", L"Collapsed", L"Windows.UI.Xaml.Visibility", enabled && target, element.visibilityOverride);
		}
	}

	template<typename T>
	CComPtr<T> FromHandle(InstanceHandle handle)
	{
		CComPtr<IInspectable> element;
		m_site->GetIInspectableFromHandle(handle, &element);
		if (element)
			return CComQIPtr<T>(element);

		return nullptr;
	}

	InstanceHandle ToHandle(IInspectable* element)
	{
		InstanceHandle handle = 0;
		m_site->GetHandleFromIInspectable(element, &handle);
		return handle;
	}

	// Find the nearest ancestor of the given element that is a Start button control (if any).
	InstanceHandle FindStartAncestor(InstanceHandle elementHandle)
	{
		auto element = FromHandle<wux::IDependencyObject>(elementHandle);
		if (element)
		{
			CComPtr<wux::IDependencyObject> parent;
			m_visualTreeHelper->GetParent(element, &parent);
			if (parent)
			{
				auto parentHandle = ToHandle(parent);
				if (parentHandle)
				{
					{
						// Check whether the parent is a Start button control.
						std::unique_lock lock(m_mutex);
						if (std::find(m_elements.begin(), m_elements.end(), parentHandle) != m_elements.end())
							return parentHandle;
					}

					// Recurse up the tree to find a Start button ancestor.
					return FindStartAncestor(parentHandle);
				}
			}
		}

		// No Start button ancestor found.
		return 0;
	}

	InstanceHandle GetProperty(InstanceHandle handle, const wchar_t* name)
	{
		unsigned int index = 0;
		if (SUCCEEDED(m_visualTreeService->GetPropertyIndex(handle, name, &index)))
		{
			InstanceHandle valueHandle = 0;
			if (SUCCEEDED(m_visualTreeService->GetProperty(handle, index, &valueHandle)))
				return valueHandle;
		}
		return 0;
	}

	CStringW GetPropertyString(InstanceHandle handle, const wchar_t* name)
	{
		CStringW result;

		auto valueHandle = GetProperty(handle, name);
		if (valueHandle)
		{
			auto value = FromHandle<wf::IPropertyValue>(valueHandle);
			if (value)
			{
				HSTRING string{};
				if (SUCCEEDED(value->GetString(&string)))
				{
					result = WindowsGetStringRawBuffer(string, nullptr);
					WindowsDeleteString(string);
				}
			}
		}

		return result;
	}

	HRESULT SetPropertyText(InstanceHandle handle, const wchar_t *name, const wchar_t *valueText, const wchar_t *valueType)
	{
		unsigned int index = 0;
		HRESULT hr = m_visualTreeService->GetPropertyIndex(handle, name, &index);
		if (FAILED(hr))
			return hr;

		CComBSTR type(valueType), value(valueText);
		InstanceHandle created = 0;
		hr = m_visualTreeService->CreateInstance(type, value, &created);
		if (FAILED(hr))
			return hr;

		return m_visualTreeService->SetProperty(handle, created, index);
	}

	HRESULT ClearPropertyByName(InstanceHandle handle, const wchar_t *name)
	{
		unsigned int index = 0;
		HRESULT hr = m_visualTreeService->GetPropertyIndex(handle, name, &index);
		if (FAILED(hr))
			return hr;

		return m_visualTreeService->ClearProperty(handle, index);
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
			ExplorerTap* tap = (ExplorerTap*)GetWindowLongPtr(GWLP_USERDATA);
			if (tap)
				tap->ApplyState();
			return TRUE;
		}
	};

	static const UINT WM_OS_STARTBUTTON_APPLY = WM_APP + 0x35B;

	DispatchWindow m_dispatchWindow;
	CComPtr<wux::Media::IVisualTreeHelperStatics> m_visualTreeHelper;
	CComPtr<IXamlDiagnostics> m_site;
	CComPtr<IVisualTreeService2> m_visualTreeService;

	std::mutex m_mutex;
	struct StartElement
	{
		InstanceHandle handle = 0;
		InstanceHandle glyph = 0;
		bool visibilityOverride = false;
		bool hitTestOverride = false;

		bool operator==(InstanceHandle handle) const
		{
			return this->handle == handle;
		}
	};
	std::vector<StartElement> m_elements;
};

OBJECT_ENTRY_AUTO(CLSID_OpenShellExplorerTap, ExplorerTap)


typedef HRESULT (WINAPI *InitXamlDiagnosticsEx_t)( LPCWSTR, DWORD, LPCWSTR, LPCWSTR, CLSID, LPCWSTR );

static DWORD WINAPI ConnectThread(LPVOID param)
{
	HRESULT hr = E_FAIL;
	HMODULE runtime = LoadLibraryEx(L"Windows.UI.Xaml.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (runtime)
	{
		auto init = (InitXamlDiagnosticsEx_t)GetProcAddress(runtime, "InitializeXamlDiagnosticsEx");
		if (init)
		{
			wchar_t dllPath[MAX_PATH];
			GetModuleFileName(GetThisModule(), dllPath, _countof(dllPath));

			for (int retry = 0; retry < 8; retry++)
			{
				// Try to connect to the XAML diagnostics service. This will fail if the service is not yet available (e.g. Explorer has not yet initialized its XAML tree). Retry a few times with a delay.
				hr = init(L"VisualDiagConnection1", GetCurrentProcessId(), L"", dllPath, CLSID_OpenShellExplorerTap, NULL);
				if (SUCCEEDED(hr))
				{
					LogToFile(STARTUP_LOG, L"ExplorerTap: connected (%d attempts)", retry + 1);
					break;
				}

				Sleep(500);
			}
		}
		FreeLibrary(runtime);
	}

	if (FAILED(hr))
	{
		LogToFile(STARTUP_LOG, L"ExplorerTap: connection failed 0x%08X", hr);

		// Allow connection attempts to be retried on the next occurrence if the thread creation failed.
		g_connectionInProgress = false;
	}

	// Release the worker's module reference atomically with thread termination.
	FreeLibraryAndExitThread((HMODULE)param, 0);
	return 0;
}

static void EnsureConnection()
{
	// Connection probing retries endpoints and may sleep, so keep it off the
	// Explorer taskbar thread. ConnectThread is the only worker we need here.

	// Prevent multiple concurrent connection attempts. If a previous attempt is still in progress, let it complete and retry on the next occurrence.
	bool expected = false;
	if (!g_connectionInProgress.compare_exchange_strong(expected, true))
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
	g_connectionInProgress = false;
}


extern "C" void UpdateExplorerTap(const ExplorerTapSettings* settings)
{
	CComPtr<ExplorerTap> tap;
	{
		std::unique_lock lock(g_tapMutex);
		tap = g_tap;
		g_tapSettings = *settings;
	}

	if (tap)
	{
		tap->Update();
	}
	else
	{
		if (settings->startButtonEnabled)
			EnsureConnection();
	}
}

extern "C" void StopExplorerTap()
{
	CComPtr<ExplorerTap> tap;
	{
		std::unique_lock lock(g_tapMutex);
		tap = g_tap;
		g_tap = nullptr;
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

// WinRT wrappers (these API are not available on Win7)

#pragma warning(suppress: 4273) // C4273: 'RoGetActivationFactory': inconsistent dll linkage
HRESULT WINAPI RoGetActivationFactory(HSTRING activatableClassId, REFIID iid, void** factory)
{
	static auto p = static_cast<decltype(&RoGetActivationFactory)>((void*)GetProcAddress(GetModuleHandle(L"combase.dll"), "RoGetActivationFactory"));
	if (p)
		return p(activatableClassId, iid, factory);
	return E_NOTIMPL;
}

STDAPI WindowsCreateStringReference(PCWSTR sourceString, UINT32 length, HSTRING_HEADER* hstringHeader, HSTRING* string)
{
	static auto p = static_cast<decltype(&WindowsCreateStringReference)>((void*)GetProcAddress(GetModuleHandle(L"combase.dll"), "WindowsCreateStringReference"));
	if (p)
		return p(sourceString, length, hstringHeader, string);
	return E_NOTIMPL;
}

STDAPI WindowsDeleteString(HSTRING string)
{
	static auto p = static_cast<decltype(&WindowsDeleteString)>((void*)GetProcAddress(GetModuleHandle(L"combase.dll"), "WindowsDeleteString"));
	if (p)
		return p(string);
	return E_NOTIMPL;
}

STDAPI_(PCWSTR) WindowsGetStringRawBuffer(HSTRING string, UINT32* length)
{
	static auto p = static_cast<decltype(&WindowsGetStringRawBuffer)>((void*)GetProcAddress(GetModuleHandle(L"combase.dll"), "WindowsGetStringRawBuffer"));
	if (p)
		return p(string, length);
	return nullptr;
}
