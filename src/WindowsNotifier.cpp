#include "WindowsNotifier.h"

#include "CalendarDate.h"

#include <cwchar>

namespace WindowsNotifier {

std::wstring ToastLaunchArgsForBlock(const std::string& day, int blockId) {
    if (blockId <= 0) return std::wstring();
    CalendarDate::Date parsed{};
    if (!CalendarDate::Parse(day, parsed)) return std::wstring();

    wchar_t buf[32]{};
    swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%d", blockId);
    return L"--open-calendar --day " + std::wstring(day.begin(), day.end()) +
           L" --block " + buf;
}

std::wstring EscapeToastXml(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (wchar_t ch : text) {
        switch (ch) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            case L'\'': out += L"&apos;"; break;
            default: out += ch; break;
        }
    }
    return out;
}

std::wstring ToastXmlForBlock(const std::string& day, int blockId,
                              const std::wstring& title, const std::wstring& body) {
    if (blockId <= 0 || title.empty() || body.empty()) return std::wstring();
    const std::wstring launch = ToastLaunchArgsForBlock(day, blockId);
    if (launch.empty()) return std::wstring();
    return L"<toast launch=\"" + EscapeToastXml(launch) +
           L"\"><visual><binding template=\"ToastGeneric\"><text>" +
           EscapeToastXml(title) + L"</text><text>" + EscapeToastXml(body) +
           L"</text></binding></visual></toast>";
}

} // namespace WindowsNotifier

#ifdef _WIN32

#include <inspectable.h>
#include <propkey.h>
#include <propsys.h>
#include <roapi.h>
#include <shlobj.h>
#include <windows.data.xml.dom.h>
#include <windows.ui.notifications.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

#ifndef NIIF_RESPECT_QUIET_TIME
#define NIIF_RESPECT_QUIET_TIME 0x00000080
#endif

namespace WindowsNotifier {
namespace {

template <size_t N>
void CopyTruncated(wchar_t (&target)[N], const std::wstring& value) {
    if (N == 0) return;
    wcsncpy_s(target, N, value.c_str(), _TRUNCATE);
}

template <class T>
void SafeRelease(T** ptr) {
    if (*ptr) {
        (*ptr)->Release();
        *ptr = nullptr;
    }
}

void SetError(std::wstring* error, const wchar_t* message, HRESULT hr) {
    if (!error) return;
    *error = message;
    if (FAILED(hr)) {
        wchar_t buf[16]{};
        swprintf_s(buf, L"%08X", static_cast<unsigned int>(hr));
        *error += L" (HRESULT 0x";
        *error += buf;
        *error += L")";
    }
}

std::wstring ShortcutPath() {
    PWSTR programs = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_CREATE, nullptr, &programs);
    if (FAILED(hr) || !programs) return std::wstring();
    std::wstring path = programs;
    CoTaskMemFree(programs);
    if (!path.empty() && path.back() != L'\\') path += L'\\';
    path += kShortcutName;
    return path;
}

} // namespace

bool ShowTrayBalloon(NOTIFYICONDATAW& nid, const std::wstring& title,
                     const std::wstring& body) {
    nid.uFlags |= NIF_INFO;
    CopyTruncated(nid.szInfoTitle, title);
    CopyTruncated(nid.szInfo, body);
    nid.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    return Shell_NotifyIconW(NIM_MODIFY, &nid) != FALSE;
}

bool EnsureToastShortcut(const std::wstring& exePath, std::wstring* error) {
    if (exePath.empty()) {
        SetError(error, L"Toast shortcut path is empty", E_INVALIDARG);
        return false;
    }

    const std::wstring shortcutPath = ShortcutPath();
    if (shortcutPath.empty()) {
        SetError(error, L"Could not find Start Menu Programs folder", E_FAIL);
        return false;
    }

    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&link));
    if (FAILED(hr) || !link) {
        SetError(error, L"Could not create toast shortcut", hr);
        return false;
    }

    hr = link->SetPath(exePath.c_str());
    if (SUCCEEDED(hr)) hr = link->SetArguments(L"");
    if (SUCCEEDED(hr)) hr = link->SetIconLocation(exePath.c_str(), 0);

    IPropertyStore* store = nullptr;
    if (SUCCEEDED(hr)) hr = link->QueryInterface(IID_PPV_ARGS(&store));
    if (SUCCEEDED(hr) && store) {
        PROPVARIANT pv{};
        PropVariantInit(&pv);
        pv.vt = VT_LPWSTR;
        pv.pwszVal = const_cast<PWSTR>(kAppUserModelId);
        hr = store->SetValue(PKEY_AppUserModel_ID, pv);
        if (SUCCEEDED(hr)) hr = store->Commit();
    }
    SafeRelease(&store);

    IPersistFile* file = nullptr;
    if (SUCCEEDED(hr)) hr = link->QueryInterface(IID_PPV_ARGS(&file));
    if (SUCCEEDED(hr) && file) hr = file->Save(shortcutPath.c_str(), TRUE);
    SafeRelease(&file);
    SafeRelease(&link);

    if (FAILED(hr)) {
        SetError(error, L"Could not save toast shortcut", hr);
        return false;
    }
    if (error) error->clear();
    return true;
}

bool ShowToast(const std::string& day, int blockId, const std::wstring& title,
               const std::wstring& body, std::wstring* error) {
    if (day.empty() || blockId <= 0 || title.empty() || body.empty()) {
        SetError(error, L"Toast notification content is empty", E_INVALIDARG);
        return false;
    }
    HRESULT appIdHr = SetCurrentProcessExplicitAppUserModelID(kAppUserModelId);
    if (FAILED(appIdHr)) {
        SetError(error, L"Could not set toast AppUserModelID", appIdHr);
        return false;
    }

    HRESULT init = RoInitialize(RO_INIT_SINGLETHREADED);
    const bool uninit = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
        SetError(error, L"Could not initialize WinRT notifications", init);
        return false;
    }

    using Microsoft::WRL::ComPtr;
    using Microsoft::WRL::Wrappers::HStringReference;
    using ABI::Windows::Data::Xml::Dom::IXmlDocument;
    using ABI::Windows::UI::Notifications::IToastNotification;
    using ABI::Windows::UI::Notifications::IToastNotificationFactory;
    using ABI::Windows::UI::Notifications::IToastNotificationManagerStatics;
    using ABI::Windows::UI::Notifications::IToastNotifier;

    HRESULT hr = S_OK;
    ComPtr<IToastNotificationManagerStatics> manager;
    hr = RoGetActivationFactory(
        HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager).Get(),
        IID_PPV_ARGS(&manager));

    ComPtr<IToastNotifier> notifier;
    if (SUCCEEDED(hr)) {
        hr = manager->CreateToastNotifierWithId(HStringReference(kAppUserModelId).Get(),
                                                &notifier);
    }

    ComPtr<IInspectable> inspectable;
    if (SUCCEEDED(hr)) {
        hr = RoActivateInstance(
            HStringReference(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument).Get(),
            &inspectable);
    }

    ComPtr<IXmlDocument> document;
    if (SUCCEEDED(hr)) hr = inspectable.As(&document);
    const std::wstring xml = ToastXmlForBlock(day, blockId, title, body);
    if (SUCCEEDED(hr)) {
        hr = document->LoadXml(HStringReference(xml.c_str(),
                                                static_cast<UINT32>(xml.size())).Get());
    }

    ComPtr<IToastNotificationFactory> factory;
    if (SUCCEEDED(hr)) {
        hr = RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotification).Get(),
            IID_PPV_ARGS(&factory));
    }

    ComPtr<IToastNotification> toast;
    if (SUCCEEDED(hr)) hr = factory->CreateToastNotification(document.Get(), &toast);
    if (SUCCEEDED(hr)) hr = notifier->Show(toast.Get());

    if (uninit) RoUninitialize();
    if (FAILED(hr)) {
        SetError(error, L"Could not show toast notification", hr);
        return false;
    }
    if (error) error->clear();
    return true;
}

} // namespace WindowsNotifier

#endif
