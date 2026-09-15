#pragma once
#include <map>
#include <string>
#include <vector>

namespace sysintel {

// One property-name -> stringified-value row from a WMI query result.
using WmiRow = std::map<std::string, std::string>;

// A minimal, reusable WMI query client. WMI's raw COM API (IWbemLocator,
// IWbemServices, SAFEARRAYs of property names, VARIANTs...) is verbose
// enough that repeating it per collector would be its own maintenance
// burden -- this wraps it once so every inventory collector just writes a
// WQL string and gets back plain strings.
class WmiClient {
public:
    WmiClient();
    ~WmiClient();

    WmiClient(const WmiClient&) = delete;
    WmiClient& operator=(const WmiClient&) = delete;

    // False if COM/WMI setup failed (e.g. WMI service unavailable) -- every
    // query() call after that just returns an empty result rather than
    // crashing, so a collector can treat "WMI itself is broken" the same way
    // it treats "this property wasn't populated": Availability::kUnavailable.
    bool ok() const { return ok_; }

    std::vector<WmiRow> query(const std::string& wql);

private:
    bool ok_ = false;
    bool com_initialized_ = false;
    void* locator_ = nullptr;   // IWbemLocator*
    void* services_ = nullptr;  // IWbemServices*
};

}  // namespace sysintel
