#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace Setup {
constexpr unsigned MaximumPairs = 4;
enum class Action { Status, License, Install, AddPair, RemovePair, Uninstall };
struct Command { Action action = Action::Status; std::wstring instance; };
struct Result { DWORD error = 0; bool reboot = false; std::wstring detail; };
struct Device {
    std::wstring instance, service, inf;
    DWORD problem = 0;
    bool present = false, started = false;
};
struct Inventory {
    DWORD error = 0, serviceError = 0;
    bool serviceInstalled = false, serviceRunning = false;
    std::vector<Device> devices;
};
// Parsing and package predicates never install, remove or start a driver.
bool ParseArguments(int argc, const wchar_t* const* argv, Command& command) noexcept;
bool ValidInstance(const std::wstring& instance) noexcept;
bool PublishedInfName(const std::wstring& name) noexcept;
bool CanAdd(unsigned existing) noexcept;
bool PackageMatches(const std::wstring& path) noexcept;
bool CatalogTrusted(const std::wstring& path) noexcept;
Inventory Inspect() noexcept;
std::wstring Describe(const Inventory& inventory);
std::wstring LicenseText();
Result Execute(const Command& command) noexcept;
bool Restart() noexcept;
}
