// dllmain.cpp : Defines the entry point for the DLL application.
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#define VC_EXTRALEAN
#define NOMINMAX
#include <windows.h>
#include <NotificationActivationCallback.h>
#include <windows.ui.notifications.h>
#include <wrl/wrappers/corewrappers.h>

#include "DesktopToasts.h"
#include "DesktopNotificationManagerCompat.h"

#define RETURN_IF_FAILED(hr) do { HRESULT _hrTemp = hr; if (FAILED(_hrTemp)) { return _hrTemp; } } while (false)

using namespace ABI::Windows::Data::Xml::Dom;
using namespace ABI::Windows::UI::Notifications;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

DesktopToastActivateHandler g_handler = nullptr;
void* g_handlerContext = nullptr;

class DECLSPEC_UUID("E407B70A-1FBD-4D5E-8822-231C69102472") NotificationActivator WrlSealed WrlFinal
	: public RuntimeClass<RuntimeClassFlags<ClassicCom>, INotificationActivationCallback>
{
public:
   virtual HRESULT STDMETHODCALLTYPE Activate(
	   _In_ LPCWSTR appUserModelId,
	   _In_ LPCWSTR invokedArgs,
	   _In_reads_(dataCount) const NOTIFICATION_USER_INPUT_DATA * data,
	   ULONG dataCount) override
   {
	   if (g_handler)
		   g_handler(g_handlerContext, invokedArgs);

	   return S_OK;
   }
};

// Flag class as COM creatable
CoCreatableClass(NotificationActivator);

HRESULT Initialize(LPCWSTR appUserModelId, DesktopToastActivateHandler handler, void* handlerContext)
{
	RETURN_IF_FAILED(DesktopNotificationManagerCompat::RegisterAumidAndComServer(appUserModelId, __uuidof(NotificationActivator)));
	RETURN_IF_FAILED(DesktopNotificationManagerCompat::RegisterActivator());
	g_handler = handler;
	g_handlerContext = handlerContext;

	return S_OK;
}

HRESULT SetNodeValueString(HSTRING inputString, IXmlNode* node, IXmlDocument* xml)
{
	ComPtr<IXmlText> inputText;
	RETURN_IF_FAILED(xml->CreateTextNode(inputString, &inputText));

	ComPtr<IXmlNode> inputTextNode;
	RETURN_IF_FAILED(inputText.As(&inputTextNode));

	ComPtr<IXmlNode> appendedChild;
	return node->AppendChild(inputTextNode.Get(), &appendedChild);
}

_Use_decl_annotations_
HRESULT SetTextValues(const PCWSTR* textValues, UINT32 textValuesCount, IXmlDocument* toastXml)
{
	ComPtr<IXmlNodeList> nodeList;
	RETURN_IF_FAILED(toastXml->GetElementsByTagName(HStringReference(L"text").Get(), &nodeList));

	UINT32 nodeListLength;
	RETURN_IF_FAILED(nodeList->get_Length(&nodeListLength));

	// If a template was chosen with fewer text elements, also change the amount of strings
	// passed to this method.
	RETURN_IF_FAILED(textValuesCount <= nodeListLength ? S_OK : E_INVALIDARG);

	for (UINT32 i = 0; i < textValuesCount; i++)
	{
		ComPtr<IXmlNode> textNode;
		RETURN_IF_FAILED(nodeList->Item(i, &textNode));

		RETURN_IF_FAILED(SetNodeValueString(HStringReference(textValues[i]).Get(), textNode.Get(), toastXml));
	}

	return S_OK;
}

HRESULT DisplaySimpleToast(LPCWSTR title, LPCWSTR text)
{
	// Construct XML
	ComPtr<IXmlDocument> doc;
	HRESULT hr = DesktopNotificationManagerCompat::CreateXmlDocumentFromString(L"<toast><visual><binding template='ToastGeneric'><text></text><text></text></binding></visual></toast>", &doc);
	if (SUCCEEDED(hr))
	{
		PCWSTR textValues[] = { title, text };
		SetTextValues(textValues, ARRAYSIZE(textValues), doc.Get());

		// Create the notifier
		// Classic Win32 apps MUST use the compat method to create the notifier
		ComPtr<IToastNotifier> notifier;
		hr = DesktopNotificationManagerCompat::CreateToastNotifier(&notifier);
		if (SUCCEEDED(hr))
		{
			// Create the notification itself (using helper method from compat library)
			ComPtr<IToastNotification> toast;
			hr = DesktopNotificationManagerCompat::CreateToastNotification(doc.Get(), &toast);
			if (SUCCEEDED(hr))
			{
				// And show it!
				hr = notifier->Show(toast.Get());
			}
		}
	}

	return hr;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
