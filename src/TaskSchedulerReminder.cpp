#include "TaskSchedulerReminder.h"

#include "CalendarDate.h"

#include <cwchar>

namespace TaskSchedulerReminder {
namespace {

} // namespace

std::wstring ReminderCheckArguments() {
    return L"--reminder-check";
}

std::wstring FormatStartBoundaryIsoLocal(const ReminderMinute& minute) {
    CalendarDate::Date parsed;
    if (!CalendarDate::Parse(minute.day, parsed) || minute.minute < 0 || minute.minute >= 1440)
        return std::wstring();

    wchar_t buf[32]{};
    swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%hsT%02d:%02d:00",
             minute.day.c_str(), minute.minute / 60, minute.minute % 60);
    return std::wstring(buf);
}

} // namespace TaskSchedulerReminder

#ifdef _WIN32

#include <comdef.h>
#include <taskschd.h>

namespace TaskSchedulerReminder {
namespace {

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
        *error += L" (HRESULT 0x";
        wchar_t buf[16]{};
        swprintf_s(buf, L"%08X", static_cast<unsigned int>(hr));
        *error += buf;
        *error += L")";
    }
}

bool GetRootFolder(ITaskService** serviceOut, ITaskFolder** folderOut,
                   std::wstring* error) {
    *serviceOut = nullptr;
    *folderOut = nullptr;

    ITaskService* service = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&service));
    if (FAILED(hr) || !service) {
        SetError(error, L"Could not create Task Scheduler service", hr);
        return false;
    }

    hr = service->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) {
        SetError(error, L"Could not connect to Task Scheduler", hr);
        service->Release();
        return false;
    }

    ITaskFolder* folder = nullptr;
    hr = service->GetFolder(_bstr_t(L"\\"), &folder);
    if (FAILED(hr) || !folder) {
        SetError(error, L"Could not open Task Scheduler root folder", hr);
        service->Release();
        return false;
    }

    *serviceOut = service;
    *folderOut = folder;
    return true;
}

} // namespace

bool RegisterReminderCheckTask(const std::wstring& exePath,
                               const std::wstring& startBoundaryIsoLocal,
                               std::wstring* error) {
    if (exePath.empty() || startBoundaryIsoLocal.empty()) {
        SetError(error, L"Task path or trigger time is empty", E_INVALIDARG);
        return false;
    }

    ITaskService* service = nullptr;
    ITaskFolder* folder = nullptr;
    if (!GetRootFolder(&service, &folder, error)) return false;

    ITaskDefinition* task = nullptr;
    HRESULT hr = service->NewTask(0, &task);
    if (FAILED(hr) || !task) {
        SetError(error, L"Could not create task definition", hr);
        SafeRelease(&folder);
        SafeRelease(&service);
        return false;
    }

    IRegistrationInfo* regInfo = nullptr;
    if (SUCCEEDED(task->get_RegistrationInfo(&regInfo)) && regInfo) {
        regInfo->put_Author(_bstr_t(L"X-TODO"));
        regInfo->put_Description(_bstr_t(L"Checks due X-TODO calendar reminders."));
        regInfo->Release();
    }

    ITaskSettings* settings = nullptr;
    if (SUCCEEDED(task->get_Settings(&settings)) && settings) {
        settings->put_StartWhenAvailable(VARIANT_TRUE);
        settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        settings->Release();
    }

    ITriggerCollection* triggers = nullptr;
    ITrigger* trigger = nullptr;
    ITimeTrigger* timeTrigger = nullptr;
    hr = task->get_Triggers(&triggers);
    if (SUCCEEDED(hr)) hr = triggers->Create(TASK_TRIGGER_TIME, &trigger);
    if (SUCCEEDED(hr)) hr = trigger->QueryInterface(IID_PPV_ARGS(&timeTrigger));
    if (SUCCEEDED(hr)) {
        hr = timeTrigger->put_StartBoundary(_bstr_t(startBoundaryIsoLocal.c_str()));
        if (SUCCEEDED(hr)) timeTrigger->put_Enabled(VARIANT_TRUE);
    }
    SafeRelease(&timeTrigger);
    SafeRelease(&trigger);
    SafeRelease(&triggers);
    if (FAILED(hr)) {
        SetError(error, L"Could not configure task trigger", hr);
        SafeRelease(&task);
        SafeRelease(&folder);
        SafeRelease(&service);
        return false;
    }

    IActionCollection* actions = nullptr;
    IAction* action = nullptr;
    IExecAction* exec = nullptr;
    hr = task->get_Actions(&actions);
    if (SUCCEEDED(hr)) hr = actions->Create(TASK_ACTION_EXEC, &action);
    if (SUCCEEDED(hr)) hr = action->QueryInterface(IID_PPV_ARGS(&exec));
    if (SUCCEEDED(hr)) hr = exec->put_Path(_bstr_t(exePath.c_str()));
    if (SUCCEEDED(hr)) hr = exec->put_Arguments(_bstr_t(ReminderCheckArguments().c_str()));
    SafeRelease(&exec);
    SafeRelease(&action);
    SafeRelease(&actions);
    if (FAILED(hr)) {
        SetError(error, L"Could not configure task action", hr);
        SafeRelease(&task);
        SafeRelease(&folder);
        SafeRelease(&service);
        return false;
    }

    IRegisteredTask* registeredTask = nullptr;
    hr = folder->RegisterTaskDefinition(_bstr_t(kTaskName), task,
                                        TASK_CREATE_OR_UPDATE,
                                        _variant_t(), _variant_t(),
                                        TASK_LOGON_INTERACTIVE_TOKEN,
                                        _variant_t(L""), &registeredTask);
    SafeRelease(&registeredTask);
    SafeRelease(&task);
    SafeRelease(&folder);
    SafeRelease(&service);
    if (FAILED(hr)) {
        SetError(error, L"Could not register reminder task", hr);
        return false;
    }
    if (error) error->clear();
    return true;
}

bool DeleteReminderCheckTask(std::wstring* error) {
    ITaskService* service = nullptr;
    ITaskFolder* folder = nullptr;
    if (!GetRootFolder(&service, &folder, error)) return false;

    HRESULT hr = folder->DeleteTask(_bstr_t(kTaskName), 0);
    SafeRelease(&folder);
    SafeRelease(&service);
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
        SetError(error, L"Could not delete reminder task", hr);
        return false;
    }
    if (error) error->clear();
    return true;
}

} // namespace TaskSchedulerReminder

#endif
