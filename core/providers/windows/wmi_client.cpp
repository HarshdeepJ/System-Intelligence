#include "wmi_client.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <comdef.h>
#include <wbemidl.h>

#pragma comment(lib, "wbemuuid.lib")

namespace sysintel {

namespace {

std::string variant_to_string(const VARIANT& var) {
    switch (var.vt) {
        case VT_BSTR: {
            _bstr_t bstr(var.bstrVal);
            return std::string(static_cast<const char*>(bstr));
        }
        case VT_I4:
            return std::to_string(var.lVal);
        case VT_UI4:
            return std::to_string(var.ulVal);
        case VT_I8:
            return std::to_string(var.llVal);
        case VT_UI8:
            return std::to_string(var.ullVal);
        case VT_BOOL:
            return var.boolVal ? "true" : "false";
        case VT_R4:
            return std::to_string(var.fltVal);
        case VT_R8:
            return std::to_string(var.dblVal);
        default:
            return "";
    }
}

}  // namespace

WmiClient::WmiClient() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // RPC_E_CHANGED_MODE means this thread already initialized COM with a
    // different concurrency model -- fine, we simply don't own uninit'ing it.
    com_initialized_ = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

    // Security initialization can only happen once per process; if it was
    // already done elsewhere (RPC_E_TOO_LATE) that's fine for a read-only
    // local WMI query, so we don't treat it as fatal.
    CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                          RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);

    IWbemLocator* locator = nullptr;
    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                           reinterpret_cast<LPVOID*>(&locator));
    if (FAILED(hr)) {
        return;
    }

    IWbemServices* services = nullptr;
    hr = locator->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0, nullptr,
                                 nullptr, &services);
    if (FAILED(hr)) {
        locator->Release();
        return;
    }

    hr = CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                            RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
                            EOAC_NONE);
    if (FAILED(hr)) {
        services->Release();
        locator->Release();
        return;
    }

    locator_ = locator;
    services_ = services;
    ok_ = true;
}

WmiClient::~WmiClient() {
    if (services_) {
        reinterpret_cast<IWbemServices*>(services_)->Release();
    }
    if (locator_) {
        reinterpret_cast<IWbemLocator*>(locator_)->Release();
    }
    if (com_initialized_) {
        CoUninitialize();
    }
}

std::vector<WmiRow> WmiClient::query(const std::string& wql) {
    std::vector<WmiRow> rows;
    if (!ok_) {
        return rows;
    }

    auto* services = reinterpret_cast<IWbemServices*>(services_);

    std::wstring wide_wql(wql.begin(), wql.end());
    IEnumWbemClassObject* enumerator = nullptr;
    HRESULT hr = services->ExecQuery(
        _bstr_t(L"WQL"), _bstr_t(wide_wql.c_str()),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);
    if (FAILED(hr) || !enumerator) {
        return rows;
    }

    IWbemClassObject* obj = nullptr;
    ULONG returned = 0;
    while (enumerator->Next(WBEM_INFINITE, 1, &obj, &returned) == S_OK) {
        WmiRow row;

        SAFEARRAY* names = nullptr;
        if (SUCCEEDED(obj->GetNames(nullptr, WBEM_FLAG_ALWAYS, nullptr, &names)) && names) {
            long lower_bound = 0;
            long upper_bound = 0;
            SafeArrayGetLBound(names, 1, &lower_bound);
            SafeArrayGetUBound(names, 1, &upper_bound);

            for (long i = lower_bound; i <= upper_bound; ++i) {
                BSTR prop_name = nullptr;
                SafeArrayGetElement(names, &i, &prop_name);

                VARIANT value;
                VariantInit(&value);
                if (SUCCEEDED(obj->Get(prop_name, 0, &value, nullptr, nullptr))) {
                    _bstr_t name_bstr(prop_name);
                    row[static_cast<const char*>(name_bstr)] = variant_to_string(value);
                    VariantClear(&value);
                }
                SysFreeString(prop_name);
            }
            SafeArrayDestroy(names);
        }

        rows.push_back(std::move(row));
        obj->Release();
    }

    enumerator->Release();
    return rows;
}

}  // namespace sysintel
