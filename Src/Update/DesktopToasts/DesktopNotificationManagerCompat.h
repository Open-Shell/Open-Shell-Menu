// ******************************************************************
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THE CODE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH
// THE CODE OR THE USE OR OTHER DEALINGS IN THE CODE.
// ******************************************************************

#pragma once
#include <string>
#include <memory>
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define NOMINMAX
#include <Windows.h>
#include <windows.ui.notifications.h>
#include <wrl.h>
#define TOAST_ACTIVATED_LAUNCH_ARG L"-ToastActivated"

using namespace ABI::Windows::UI::Notifications;

class DesktopNotificationHistoryCompat;

namespace DesktopNotificationManagerCompat
{
    /// <summary>
    /// If not running under the Desktop Bridge, you must call this method to register your AUMID with the Compat library and to
    /// register your COM CLSID and EXE in LocalServer32 registry. Feel free to call this regardless, and we will no-op if running
    /// under Desktop Bridge. Call this upon application startup, before calling any other APIs.
    /// </summary>
    /// <param name="aumid">An AUMID that uniquely identifies your application.</param>
    /// <param name="clsid">The CLSID of your NotificationActivator class.</param>
    HRESULT RegisterAumidAndComServer(const wchar_t *aumid, GUID clsid);

    /// <summary>
    /// Registers your module to handle COM activations. Call this upon application startup.
    /// </summary>
    HRESULT RegisterActivator();

    /// <summary>
    /// Creates a toast notifier. You must have called RegisterActivator first (and also RegisterAumidAndComServer if you're a classic Win32 app), or this will throw an exception.
    /// </summary>
    HRESULT CreateToastNotifier(IToastNotifier** notifier);

    /// <summary>
    /// Creates an XmlDocument initialized with the specified string. This is simply a convenience helper method.
    /// </summary>
    HRESULT CreateXmlDocumentFromString(const wchar_t *xmlString, ABI::Windows::Data::Xml::Dom::IXmlDocument** doc);

    /// <summary>
    /// Creates a toast notification. This is simply a convenience helper method.
    /// </summary>
    HRESULT CreateToastNotification(ABI::Windows::Data::Xml::Dom::IXmlDocument* content, IToastNotification** notification);

    /// <summary>
    /// Gets the DesktopNotificationHistoryCompat object. You must have called RegisterActivator first (and also RegisterAumidAndComServer if you're a classic Win32 app), or this will throw an exception.
    /// </summary>
    HRESULT get_History(std::unique_ptr<DesktopNotificationHistoryCompat>* history);

    /// <summary>
    /// Gets a boolean representing whether http images can be used within toasts. This is true if running under Desktop Bridge.
    /// </summary>
    bool CanUseHttpImages();
}

class DesktopNotificationHistoryCompat
{
public:

    /// <summary>
    /// Removes all notifications sent by this app from action center.
    /// </summary>
    HRESULT Clear();

    /// <summary>
    /// Gets all notifications sent by this app that are currently still in Action Center.
    /// </summary>
    HRESULT GetHistory(ABI::Windows::Foundation::Collections::IVectorView<ToastNotification*>** history);

    /// <summary>
    /// Removes an individual toast, with the specified tag label, from action center.
    /// </summary>
    /// <param name="tag">The tag label of the toast notification to be removed.</param>
    HRESULT Remove(const wchar_t *tag);

    /// <summary>
    /// Removes a toast notification from the action using the notification's tag and group labels.
    /// </summary>
    /// <param name="tag">The tag label of the toast notification to be removed.</param>
    /// <param name="group">The group label of the toast notification to be removed.</param>
    HRESULT RemoveGroupedTag(const wchar_t *tag, const wchar_t *group);

    /// <summary>
    /// Removes a group of toast notifications, identified by the specified group label, from action center.
    /// </summary>
    /// <param name="group">The group label of the toast notifications to be removed.</param>
    HRESULT RemoveGroup(const wchar_t *group);

    /// <summary>
    /// Do not call this. Instead, call DesktopNotificationManagerCompat.get_History() to obtain an instance.
    /// </summary>
    DesktopNotificationHistoryCompat(const wchar_t *aumid, Microsoft::WRL::ComPtr<IToastNotificationHistory> history);

private:
    std::wstring m_aumid;
    Microsoft::WRL::ComPtr<IToastNotificationHistory> m_history = nullptr;
};