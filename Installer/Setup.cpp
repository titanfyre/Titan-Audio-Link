// Copyright (c) 2026 TitanCSA. MIT License.
// Uses the author's Connection Trace Studio installer as its starting point.
#include "Setup.h"
#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <mscat.h>
#include <sddl.h>
#include <objbase.h>
#include <filesystem>
#include <set>
#include <system_error>
#include <algorithm>

namespace fs = std::filesystem;
namespace {
constexpr wchar_t Hardware[] = L"ROOT\\TitanAudioLink";
constexpr wchar_t Service[] = L"TitanAudioLink";
constexpr wchar_t Model[] = L"TitanAudioLink_SA";
constexpr wchar_t Catalog[] = L"TitanAudioLink.cat";
constexpr wchar_t Driver[] = L"TitanAudioLink.sys";
constexpr wchar_t InfName[] = L"TitanAudioLink.inf";
struct Devices {
    HDEVINFO h = INVALID_HANDLE_VALUE;
    ~Devices() { if (h != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(h); }
};
struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
};
struct Inf { HINF h; ~Inf() { SetupCloseInfFile(h); } };
struct ServiceHandle { SC_HANDLE h; ~ServiceHandle() { if (h) CloseServiceHandle(h); } };
struct Lock {
    HANDLE h = nullptr;
    bool owned = false;
    ~Lock() { if (owned) ReleaseMutex(h); if (h) CloseHandle(h); }
};
std::wstring Message(DWORD error) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring message = buffer ? buffer : L"Operation failed";
    if (buffer) LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) message.pop_back();
    return message + L" (Windows error " + std::to_wstring(error) + L")";
}
void Require(bool valid) {
    if (!valid) { DWORD error = GetLastError(); throw std::system_error(static_cast<int>(error ? error : ERROR_GEN_FAILURE), std::system_category()); }
}
bool Elevated() {
    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.h)) return false;
    TOKEN_ELEVATION elevation{}; DWORD bytes = 0;
    return GetTokenInformation(token.h, TokenElevation, &elevation, sizeof(elevation), &bytes) && elevation.TokenIsElevated;
}
std::wstring Instance(HDEVINFO set, SP_DEVINFO_DATA& data) {
    wchar_t value[MAX_DEVICE_ID_LEN]{};
    Require(SetupDiGetDeviceInstanceIdW(set, &data, value, MAX_DEVICE_ID_LEN, nullptr) != FALSE);
    return value;
}
std::wstring Property(HDEVINFO set, SP_DEVINFO_DATA& data, DWORD property) {
    wchar_t value[1024]{}; DWORD type = 0, bytes = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(set, &data, property, &type,
        reinterpret_cast<BYTE*>(value), sizeof(value), &bytes)) return {};
    if (type != REG_SZ || bytes < sizeof(wchar_t) || bytes > sizeof(value) || value[bytes / sizeof(wchar_t) - 1]) return {};
    return value;
}
bool Ours(HDEVINFO set, SP_DEVINFO_DATA& data) {
    if (!IsEqualGUID(data.ClassGuid, GUID_DEVCLASS_MEDIA)) return false;
    DWORD type = 0, bytes = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &data, SPDRP_HARDWAREID, &type, nullptr, 0, &bytes);
    if (type != REG_MULTI_SZ || bytes < 2 * sizeof(wchar_t) || bytes > 65536 || bytes % sizeof(wchar_t)) return false;
    std::vector<wchar_t> ids(bytes / sizeof(wchar_t) + 2, 0);
    if (!SetupDiGetDeviceRegistryPropertyW(set, &data, SPDRP_HARDWAREID, &type,
        reinterpret_cast<BYTE*>(ids.data()), bytes, nullptr) || type != REG_MULTI_SZ) return false;
    // Reject malformed MULTI_SZ rather than searching beyond the reported bytes.
    if (ids[bytes / sizeof(wchar_t) - 1] || ids[bytes / sizeof(wchar_t) - 2]) return false;
    bool matched = false;
    for (const wchar_t* p = ids.data(); *p; p += wcslen(p) + 1) if (_wcsicmp(p, Hardware) == 0) matched = true;
    if (!matched) return false;
    auto instance = Instance(set, data);
    return Setup::ValidInstance(instance);
}
std::wstring BoundInf(HDEVINFO set, SP_DEVINFO_DATA& data) {
    HKEY key = SetupDiOpenDevRegKey(set, &data, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_QUERY_VALUE);
    if (key == INVALID_HANDLE_VALUE) return {};
    wchar_t value[256]{}; DWORD type = 0, bytes = sizeof(value);
    LSTATUS result = RegQueryValueExW(key, L"InfPath", nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) || bytes > sizeof(value) || value[bytes / sizeof(wchar_t) - 1]) return {};
    std::wstring name = value;
    if (!Setup::PublishedInfName(name)) return {};
    CharLowerBuffW(name.data(), static_cast<DWORD>(name.size()));
    return name;
}
bool Field(HINF inf, const wchar_t* section, const wchar_t* key, DWORD index, const wchar_t* expected) {
    INFCONTEXT line{}; wchar_t value[1024]{};
    return SetupFindFirstLineW(inf, section, key, &line) && SetupGetStringFieldW(&line, index, value, 1024, nullptr) && !_wcsicmp(value, expected);
}
bool Number(HINF inf, const wchar_t* section, const wchar_t* key, DWORD index, int expected) {
    INFCONTEXT line{}; int value = 0;
    return SetupFindFirstLineW(inf, section, key, &line) && SetupGetIntField(&line, index, &value) && value == expected;
}
bool OwnPackage(const fs::path& path) {
    GUID classId{}; wchar_t className[128]{};
    if (!SetupDiGetINFClassW(path.c_str(), &classId, className, 128, nullptr) || !IsEqualGUID(classId, GUID_DEVCLASS_MEDIA)) return false;
    HINF opened = SetupOpenInfFileW(path.c_str(), nullptr, INF_STYLE_WIN4, nullptr);
    if (opened == INVALID_HANDLE_VALUE) return false;
    Inf inf{opened};
    if (!Field(opened, L"Version", L"Class", 1, L"MEDIA") || !Field(opened, L"Version", L"Provider", 1, L"Titanfyre") || !Field(opened, L"Version", L"CatalogFile", 1, Catalog) ||
        !Number(opened, L"Version", L"PnpLockDown", 1, 1)) return false;
    // Resolve manufacturer and architecture decoration rather than accepting an
    // unrelated hardware ID hidden in an unused section of the INF.
    INFCONTEXT manufacturer{}; wchar_t base[256]{}, decoration[256]{};
    if (SetupGetLineCountW(opened, L"Manufacturer") != 1 ||
        !SetupFindFirstLineW(opened, L"Manufacturer", nullptr, &manufacturer) ||
        SetupGetFieldCount(&manufacturer) != 2 ||
        !SetupGetStringFieldW(&manufacturer, 1, base, 256, nullptr) ||
        !SetupGetStringFieldW(&manufacturer, 2, decoration, 256, nullptr) ||
        (_wcsicmp(decoration, L"NTamd64.10.0...19041") && _wcsicmp(decoration, L"NTamd64.10.0...22000"))) return false;
    std::wstring models = std::wstring(base) + L"." + decoration;
    INFCONTEXT model{}; wchar_t section[256]{}, hardware[256]{};
    if (SetupGetLineCountW(opened, models.c_str()) != 1 || !SetupFindFirstLineW(opened, models.c_str(), nullptr, &model) ||
        SetupGetFieldCount(&model) != 2 || !SetupGetStringFieldW(&model, 1, section, 256, nullptr) ||
        !SetupGetStringFieldW(&model, 2, hardware, 256, nullptr) || _wcsicmp(section, Model) || _wcsicmp(hardware, Hardware)) return false;
    if (!Field(opened, L"TitanAudioLink_SA.NT.Services", L"AddService", 1, Service) ||
        !Number(opened, L"TitanAudioLink_SA.NT.Services", L"AddService", 2, 2)) return false;
    INFCONTEXT service{}; wchar_t serviceSection[256]{};
    if (!SetupFindFirstLineW(opened, L"TitanAudioLink_SA.NT.Services", L"AddService", &service) ||
        !SetupGetStringFieldW(&service, 3, serviceSection, 256, nullptr)) return false;
    // SetupAPI expands the predefined DIRID 13 to "." when reading an INF
    // outside an installation context. Verify the copy destination separately.
    INFCONTEXT copy{}; wchar_t copied[256]{};
    if (!Field(opened, L"TitanAudioLink_SA.NT", L"CopyFiles", 1, L"TitanAudioLink_SA.CopyList") ||
        !Number(opened, L"DestinationDirs", L"TitanAudioLink_SA.CopyList", 1, 13) ||
        SetupGetLineCountW(opened, L"TitanAudioLink_SA.CopyList") != 1 ||
        !SetupFindFirstLineW(opened, L"TitanAudioLink_SA.CopyList", nullptr, &copy) ||
        !SetupGetStringFieldW(&copy, 1, copied, 256, nullptr) || _wcsicmp(copied, Driver)) return false;
    return Number(opened, serviceSection, L"ServiceType", 1, SERVICE_KERNEL_DRIVER) &&
        Number(opened, serviceSection, L"StartType", 1, SERVICE_DEMAND_START) &&
        Field(opened, serviceSection, L"ServiceBinary", 1, L".\\TitanAudioLink.sys");
}
bool MicrosoftCatalog(const fs::path& path) {
    WINTRUST_FILE_INFO file{}; file.cbStruct = sizeof(file); file.pcwszFilePath = path.c_str();
    WINTRUST_DATA data{}; data.cbStruct = sizeof(data); data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE; data.dwUnionChoice = WTD_CHOICE_FILE; data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY; data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status = WinVerifyTrust(reinterpret_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data); bool accepted = false;
    if (status == ERROR_SUCCESS) {
        auto provider = WTHelperProvDataFromStateData(data.hWVTStateData);
        auto signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
        if (signer && signer->csCertChain && signer->pChainContext) {
            wchar_t name[256]{}, organization[256]{};
            auto certificate = signer->pasCertChain[0].pCert;
            CertGetNameStringW(certificate, CERT_NAME_ATTR_TYPE, 0, const_cast<char*>(szOID_COMMON_NAME), name, 256);
            CertGetNameStringW(certificate, CERT_NAME_ATTR_TYPE, 0, const_cast<char*>(szOID_ORGANIZATION_NAME), organization, 256);
            CERT_CHAIN_POLICY_PARA policy{}; policy.cbSize = sizeof(policy);
            CERT_CHAIN_POLICY_STATUS verdict{}; verdict.cbSize = sizeof(verdict);
            // Root public-key policy, not a subject-name check. No test-root flag.
            bool microsoftRoot = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_MICROSOFT_ROOT,
                signer->pChainContext, &policy, &verdict) && verdict.dwError == ERROR_SUCCESS;
            if (!microsoftRoot) {
                policy.dwFlags = MICROSOFT_ROOT_CERT_CHAIN_POLICY_CHECK_APPLICATION_ROOT_FLAG;
                verdict.dwError = ERROR_SUCCESS;
                microsoftRoot = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_MICROSOFT_ROOT,
                    signer->pChainContext, &policy, &verdict) && verdict.dwError == ERROR_SUCCESS;
            }
            accepted = microsoftRoot && !_wcsicmp(name, L"Microsoft Windows Hardware Compatibility Publisher") &&
                !_wcsicmp(organization, L"Microsoft Corporation");
        }
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE; WinVerifyTrust(reinterpret_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return accepted;
}
bool CatalogMember(const fs::path& directory, const wchar_t* name) {
    auto path = directory / name, catalog = directory / Catalog;
    Handle file; file.h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file.h == INVALID_HANDLE_VALUE) return false;
    HCATADMIN admin = nullptr; if (!CryptCATAdminAcquireContext2(&admin, nullptr, L"SHA256", nullptr, 0)) return false;
    struct Admin { HCATADMIN h; ~Admin() { CryptCATAdminReleaseContext(h, 0); } } release{admin};
    BYTE hash[64]{}; DWORD bytes = sizeof(hash);
    if (!CryptCATAdminCalcHashFromFileHandle2(admin, file.h, &bytes, hash, 0) || bytes > 64) return false;
    wchar_t tag[129]{}; constexpr wchar_t hex[] = L"0123456789ABCDEF";
    for (DWORD i = 0; i < bytes; ++i) { tag[i * 2] = hex[hash[i] >> 4]; tag[i * 2 + 1] = hex[hash[i] & 15]; }
    WINTRUST_CATALOG_INFO member{}; member.cbStruct = sizeof(member); member.pcwszCatalogFilePath = catalog.c_str();
    member.pcwszMemberTag = tag; member.pcwszMemberFilePath = path.c_str(); member.hMemberFile = file.h;
    member.pbCalculatedFileHash = hash; member.cbCalculatedFileHash = bytes; member.hCatAdmin = admin;
    WINTRUST_DATA data{}; data.cbStruct = sizeof(data); data.dwUIChoice = WTD_UI_NONE; data.dwUnionChoice = WTD_CHOICE_CATALOG;
    data.pCatalog = &member; data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_NONE;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2; LONG result = WinVerifyTrust(reinterpret_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    data.dwStateAction = WTD_STATEACTION_CLOSE; WinVerifyTrust(reinterpret_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return result == ERROR_SUCCESS;
}
fs::path WindowsDirectory() {
    wchar_t value[MAX_PATH]{}; DWORD size = GetWindowsDirectoryW(value, MAX_PATH);
    Require(size > 0 && size < MAX_PATH); return value;
}
void PlainDirectory(const fs::path& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) Require(false);
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        SetLastError(ERROR_REPARSE_TAG_INVALID); Require(false);
    }
}
struct Temporary {
    fs::path directory;
    bool owned = false;
    Handle files[3];
    ~Temporary() {
        for (auto& file : files) { if (file.h != INVALID_HANDLE_VALUE) { CloseHandle(file.h); file.h = INVALID_HANDLE_VALUE; } }
        if (!owned) return;
        for (auto name : {InfName, Catalog, Driver}) DeleteFileW((directory / name).c_str());
        RemoveDirectoryW(directory.c_str());
    }
    void Create() {
        // Fail before creating a directory when the self-contained signed payload is absent.
        HRSRC resources[3]{};
        for (int i = 0; i < 3; ++i) {
            resources[i] = FindResourceW(nullptr, MAKEINTRESOURCEW(101 + i), RT_RCDATA);
            if (!resources[i]) { SetLastError(ERROR_RESOURCE_DATA_NOT_FOUND); Require(false); }
        }
        auto windows = WindowsDirectory(), parent = windows / L"Temp";
        PlainDirectory(windows); PlainDirectory(parent);
        GUID guid{}; HRESULT result = CoCreateGuid(&guid);
        if (FAILED(result)) { SetLastError(static_cast<DWORD>(result)); Require(false); }
        wchar_t id[40]{}; Require(StringFromGUID2(guid, id, 40) != 0);
        directory = parent / (std::wstring(L"TitanAudioLink-") + id);
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        Require(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)",
            SDDL_REVISION_1, &descriptor, nullptr) != FALSE);
        SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
        bool made = CreateDirectoryW(directory.c_str(), &security) != FALSE; DWORD error = GetLastError();
        LocalFree(descriptor); if (!made) { SetLastError(error); Require(false); } owned = true;
        PlainDirectory(directory);
        const wchar_t* names[] = {InfName, Catalog, Driver};
        for (int i = 0; i < 3; ++i) {
            HGLOBAL loaded = LoadResource(nullptr, resources[i]); Require(loaded != nullptr);
            DWORD size = SizeofResource(nullptr, resources[i]); void* data = LockResource(loaded);
            Require(data && size && size <= 16 * 1024 * 1024);
            // FILE_SHARE_READ preserves the verified bytes until staging completes.
            files[i].h = CreateFileW((directory / names[i]).c_str(), GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            Require(files[i].h != INVALID_HANDLE_VALUE); DWORD written = 0;
            Require(WriteFile(files[i].h, data, size, &written, nullptr) && written == size);
            Require(FlushFileBuffers(files[i].h) != FALSE);
            CloseHandle(files[i].h); files[i].h = INVALID_HANDLE_VALUE;
            files[i].h = CreateFileW((directory / names[i]).c_str(), GENERIC_READ,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Require(files[i].h != INVALID_HANDLE_VALUE);
        }
    }
};
void Enumerate(Devices& devices, std::vector<SP_DEVINFO_DATA>& targets) {
    devices.h = SetupDiGetClassDevsW(&GUID_DEVCLASS_MEDIA, nullptr, nullptr, 0); Require(devices.h != INVALID_HANDLE_VALUE);
    for (DWORD index = 0; ; ++index) {
        if (index >= 65536) { SetLastError(ERROR_BUFFER_OVERFLOW); Require(false); }
        SP_DEVINFO_DATA data{}; data.cbSize = sizeof(data);
        if (!SetupDiEnumDeviceInfo(devices.h, index, &data)) { if (GetLastError() != ERROR_NO_MORE_ITEMS) Require(false); break; }
        if (Ours(devices.h, data)) targets.push_back(data);
    }
}
bool DeviceRestartRequired(HDEVINFO devices, SP_DEVINFO_DATA& data) {
    SP_DEVINSTALL_PARAMS_W parameters{}; parameters.cbSize = sizeof(parameters);
    return SetupDiGetDeviceInstallParamsW(devices, &data, &parameters) && (parameters.Flags & (DI_NEEDRESTART | DI_NEEDREBOOT));
}
Setup::Result Install(bool adding) {
    SYSTEM_INFO platform{}; GetNativeSystemInfo(&platform);
    if (platform.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64)
        return {ERROR_NOT_SUPPORTED, false, L"This driver package supports Windows x64 only. No device was created."};
    Devices devices; std::vector<SP_DEVINFO_DATA> targets; Enumerate(devices, targets);
    if (adding && !Setup::CanAdd(static_cast<unsigned>(targets.size())))
        return {ERROR_TOO_MANY_NAMES, false, L"This build supports at most four virtual pairs. Remove a selected pair before adding another."};
    Temporary temp; temp.Create();
    if (!OwnPackage(temp.directory / InfName)) return {ERROR_INVALID_DATA, false, L"Installation refused: the embedded INF does not match the Titan Audio Link MEDIA class, root hardware ID, kernel service and catalog identity."};
    if (!MicrosoftCatalog(temp.directory / Catalog)) return {ERROR_INVALID_IMAGE_HASH, false, L"Installation refused: the catalog requires a trusted Microsoft Windows Hardware Compatibility Publisher signature chained to a Microsoft production root. Unsigned or test-signed packages are not installed."};
    if (!CatalogMember(temp.directory, InfName) || !CatalogMember(temp.directory, Driver))
        return {ERROR_INVALID_IMAGE_HASH, false, L"Installation refused: the embedded INF or SYS does not match the trusted catalog."};
    SP_DEVINFO_DATA selected{}; selected.cbSize = sizeof(selected); bool created = false;
    if (adding || targets.empty()) {
        Require(SetupDiCreateDeviceInfoW(devices.h, L"MEDIA", &GUID_DEVCLASS_MEDIA, L"Titan Audio Link", nullptr, DICD_GENERATE_ID, &selected) != FALSE);
        constexpr wchar_t ids[] = L"ROOT\\TitanAudioLink\0";
        Require(SetupDiSetDeviceRegistryPropertyW(devices.h, &selected, SPDRP_HARDWAREID, reinterpret_cast<const BYTE*>(ids), sizeof(ids)) != FALSE);
        if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, devices.h, &selected)) {
            DWORD error = GetLastError(); BOOL restart = FALSE;
            bool removed = DiUninstallDevice(nullptr, devices.h, &selected, 0, &restart) != FALSE;
            return {error, restart != FALSE, L"Windows could not register the new audio pair. " +
                std::wstring(removed ? L"The new node was removed. " : L"Refresh the inventory to check whether Windows retained a partial node. ") + Message(error)};
        }
        created = true;
    }
    BOOL reboot = FALSE;
    if (!UpdateDriverForPlugAndPlayDevicesW(nullptr, Hardware, (temp.directory / InfName).c_str(), INSTALLFLAG_NONINTERACTIVE, &reboot)) {
        DWORD error = GetLastError(); BOOL rollbackReboot = FALSE;
        bool rolledBack = !created || DiUninstallDevice(nullptr, devices.h, &selected, 0, &rollbackReboot) != FALSE;
        return {error, reboot != FALSE || rollbackReboot != FALSE || !rolledBack,
            L"Windows could not install the audio driver. " + std::wstring(created ?
            (rolledBack ? L"The newly created pair was removed. " : L"The new pair could not be removed; restart if requested and run Uninstall. ") :
            L"Existing pairs were retained. ") + Message(error)};
    }
    if (created) reboot = reboot || DeviceRestartRequired(devices.h, selected);
    return {0, reboot != FALSE, adding ? L"An independent virtual speaker / microphone pair was added. Existing physical defaults were not changed by this installer." :
        L"Titan Audio Link is installed or updated. Existing pairs were reused; no extra pair was added when one already existed."};
}
std::set<std::wstring> OwnPackages() {
    std::set<std::wstring> packages; auto directory = WindowsDirectory() / L"INF";
    WIN32_FIND_DATAW entry{}; HANDLE search = FindFirstFileW((directory / L"oem*.inf").c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) { if (GetLastError() != ERROR_FILE_NOT_FOUND) Require(false); return packages; }
    struct Search { HANDLE h; ~Search() { FindClose(h); } } cleanup{search}; unsigned count = 0;
    do {
        if (++count > 16384) { SetLastError(ERROR_BUFFER_OVERFLOW); Require(false); }
        if ((entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || !Setup::PublishedInfName(entry.cFileName)) continue;
        if (OwnPackage(directory / entry.cFileName)) { std::wstring name = entry.cFileName; CharLowerBuffW(name.data(), static_cast<DWORD>(name.size())); packages.insert(name); }
    } while (FindNextFileW(search, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) Require(false);
    return packages;
}
Setup::Result Remove(const std::wstring& requested, bool all) {
    Devices devices; std::vector<SP_DEVINFO_DATA> targets; Enumerate(devices, targets);
    auto packages = all ? OwnPackages() : std::set<std::wstring>{};
    bool reboot = false, removed = false;
    for (auto& data : targets) {
        auto instance = Instance(devices.h, data);
        if (!all && _wcsicmp(instance.c_str(), requested.c_str())) continue;
        // Revalidate the exact enumerated root node immediately before mutation.
        if (!Ours(devices.h, data)) return {ERROR_INVALID_DATA, reboot, L"Removal refused: the selected node no longer matches Titan Audio Link."};
        BOOL restart = FALSE;
        if (!DiUninstallDevice(nullptr, devices.h, &data, 0, &restart)) {
            DWORD error = GetLastError(); return {error, reboot || restart != FALSE, L"Could not remove pair " + instance + L". " + Message(error)};
        }
        reboot = reboot || restart != FALSE; removed = true;
        if (!all) break;
    }
    if (!all && !removed) return {ERROR_NOT_FOUND, false, L"Removal refused: that exact Titan Audio Link root instance was not found. Refresh and select a current pair."};
    for (const auto& name : packages) {
        // Zero flags deliberately preserve every package Windows still considers in use.
        if (!SetupUninstallOEMInfW(name.c_str(), 0, nullptr)) {
            DWORD error = GetLastError();
            return {error, reboot, L"Audio pairs were removed, but Windows retained package " + name + L". No force deletion was attempted. " + Message(error)};
        }
    }
    return {0, reboot, all ? (removed || !packages.empty() ? L"Titan Audio Link pairs and unused own driver packages were removed." : L"Titan Audio Link is not installed.") :
        L"The selected pair was removed. Other audio devices and pairs were retained."};
}
}

bool Setup::ValidInstance(const std::wstring& instance) noexcept {
    if (instance.size() < 8 || instance.size() >= MAX_DEVICE_ID_LEN || _wcsnicmp(instance.c_str(), L"ROOT\\", 5)) return false;
    unsigned separators = 0;
    for (wchar_t value : instance) {
        if (value == L'\\') { ++separators; continue; }
        if (!((value >= L'A' && value <= L'Z') || (value >= L'a' && value <= L'z') ||
              (value >= L'0' && value <= L'9') || value == L'_' || value == L'&' || value == L'-')) return false;
    }
    bool ownPrefix = !_wcsnicmp(instance.c_str(), L"ROOT\\MEDIA\\", 11) || !_wcsnicmp(instance.c_str(), L"ROOT\\TitanAudioLink\\", 20);
    return ownPrefix && separators == 2 && instance.back() != L'\\' && instance.find(L"\\\\") == std::wstring::npos;
}
bool Setup::PublishedInfName(const std::wstring& name) noexcept {
    if (name.size() < 8 || name.size() > 32 || _wcsnicmp(name.c_str(), L"oem", 3) || _wcsicmp(name.c_str() + name.size() - 4, L".inf")) return false;
    for (size_t i = 3; i < name.size() - 4; ++i) if (name[i] < L'0' || name[i] > L'9') return false;
    return true;
}
bool Setup::CanAdd(unsigned existing) noexcept { return existing < MaximumPairs; }
std::wstring Setup::LicenseText() {
    std::wstring combined;
    for (int id : {105, 104}) {
        HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
        if (!resource) throw std::runtime_error("Embedded license resource is missing");
        HGLOBAL loaded = LoadResource(nullptr, resource); DWORD bytes = SizeofResource(nullptr, resource);
        const auto* source = static_cast<const char*>(LockResource(loaded));
        if (!source || !bytes || bytes > 128 * 1024) throw std::runtime_error("Embedded license resource is invalid");
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, static_cast<int>(bytes), nullptr, 0);
        if (!count) throw std::runtime_error("Embedded license is not valid UTF-8");
        std::wstring text(static_cast<size_t>(count), L'\0');
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, static_cast<int>(bytes), text.data(), count))
            throw std::runtime_error("Could not decode embedded license");
        if (!text.empty() && text.front() == 0xFEFF) text.erase(0, 1);
        // Native edit controls expect CRLF even if repository files use LF.
        for (size_t index = 0; index < text.size(); ++index)
            if (text[index] == L'\n' && (!index || text[index - 1] != L'\r')) text.insert(index++, 1, L'\r');
        combined += text + L"\r\n\r\n";
    }
    return combined;
}
bool Setup::ParseArguments(int argc, const wchar_t* const* argv, Command& command) noexcept {
    try {
        if (argc < 2 || argc > 3 || !argv || !argv[1]) return false;
        Command parsed;
        if (argc == 2) {
            if (!_wcsicmp(argv[1], L"/status")) parsed.action = Action::Status;
            else if (!_wcsicmp(argv[1], L"/license")) parsed.action = Action::License;
            else if (!_wcsicmp(argv[1], L"/install")) parsed.action = Action::Install;
            else if (!_wcsicmp(argv[1], L"/add-pair")) parsed.action = Action::AddPair;
            else if (!_wcsicmp(argv[1], L"/uninstall")) parsed.action = Action::Uninstall;
            else return false;
        } else {
            if (_wcsicmp(argv[1], L"/remove-pair") || !argv[2] || !ValidInstance(argv[2])) return false;
            parsed.action = Action::RemovePair; parsed.instance = argv[2];
        }
        command = std::move(parsed); return true;
    } catch (...) { return false; }
}
bool Setup::PackageMatches(const std::wstring& path) noexcept { try { return OwnPackage(fs::path(path)); } catch (...) { return false; } }
bool Setup::CatalogTrusted(const std::wstring& path) noexcept { try { return MicrosoftCatalog(fs::path(path)); } catch (...) { return false; } }
Setup::Inventory Setup::Inspect() noexcept {
    Inventory inventory;
    try {
        Devices devices; std::vector<SP_DEVINFO_DATA> targets; Enumerate(devices, targets);
        for (auto& data : targets) {
            Device device; device.instance = Instance(devices.h, data); device.service = Property(devices.h, data, SPDRP_SERVICE);
            device.inf = BoundInf(devices.h, data); ULONG flags = 0, problem = 0;
            CONFIGRET result = CM_Get_DevNode_Status(&flags, &problem, data.DevInst, 0);
            device.present = result == CR_SUCCESS; device.started = device.present && (flags & DN_STARTED) != 0;
            device.problem = device.present ? problem : CM_PROB_DEVICE_NOT_THERE;
            inventory.devices.push_back(std::move(device));
        }
        ServiceHandle manager{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
        if (!manager.h) inventory.serviceError = GetLastError();
        else {
            ServiceHandle service{OpenServiceW(manager.h, Service, SERVICE_QUERY_STATUS)};
            if (!service.h) { DWORD error = GetLastError(); if (error != ERROR_SERVICE_DOES_NOT_EXIST) inventory.serviceError = error; }
            else {
                inventory.serviceInstalled = true; SERVICE_STATUS_PROCESS state{}; DWORD bytes = 0;
                if (!QueryServiceStatusEx(service.h, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&state), sizeof(state), &bytes)) inventory.serviceError = GetLastError();
                else inventory.serviceRunning = state.dwCurrentState == SERVICE_RUNNING;
            }
        }
    } catch (const std::system_error& error) { inventory.error = static_cast<DWORD>(error.code().value()); }
    catch (...) { inventory.error = ERROR_NOT_ENOUGH_MEMORY; }
    return inventory;
}
std::wstring Setup::Describe(const Inventory& inventory) {
    if (inventory.error) return L"Could not inspect the audio device inventory. " + Message(inventory.error);
    std::wstring text = L"Virtual pairs: " + std::to_wstring(inventory.devices.size()) + L" / " + std::to_wstring(MaximumPairs) +
        L"\r\nKernel service: " + (inventory.serviceError ? L"status unavailable" : !inventory.serviceInstalled ? L"not installed" : inventory.serviceRunning ? L"running" : L"stopped");
    if (inventory.serviceError) text += L" — " + Message(inventory.serviceError);
    for (const auto& device : inventory.devices) {
        text += L"\r\n\r\n" + device.instance + L"\r\n  " + (device.present ? (device.started ? std::wstring(L"Started") : std::wstring(L"Not started")) : std::wstring(L"Not present")) +
            L"; Windows problem code " + std::to_wstring(device.problem) + L"; service " + (device.service.empty() ? std::wstring(L"not assigned") : device.service) +
            L"; package " + (device.inf.empty() ? std::wstring(L"not assigned") : device.inf);
    }
    return text;
}
Setup::Result Setup::Execute(const Command& command) noexcept {
    try {
        if (command.action == Action::Status) { auto inventory = Inspect(); return {inventory.error, false, Describe(inventory)}; }
        if (command.action == Action::License) return {0, false, LicenseText()};
        if (!Elevated()) return {ERROR_ELEVATION_REQUIRED, false, L"Administrator rights are required to change audio devices. Run this setup as administrator."};
        if (command.action == Action::RemovePair && !ValidInstance(command.instance)) return {ERROR_INVALID_PARAMETER, false, L"Provide the exact root instance shown by /status. No device was removed."};
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        Require(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1, &descriptor, nullptr) != FALSE);
        SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE}; Lock lock;
        lock.h = CreateMutexW(&security, FALSE, L"Global\\TitanAudioLinkSetup"); DWORD error = GetLastError(); LocalFree(descriptor);
        if (!lock.h) return {error, false, L"Could not open the installation lock. " + Message(error)};
        DWORD wait = WaitForSingleObject(lock.h, 0);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return {wait == WAIT_TIMEOUT ? ERROR_BUSY : GetLastError(), false, L"Another Titan Audio Link installation is running. Wait for it to finish."};
        lock.owned = true;
        switch (command.action) {
        case Action::Install: return Install(false);
        case Action::AddPair: return Install(true);
        case Action::RemovePair: return Remove(command.instance, false);
        case Action::Uninstall: return Remove({}, true);
        default: return {ERROR_INVALID_PARAMETER, false, L"Unknown installer action."};
        }
    } catch (const std::system_error& error) {
        DWORD code = static_cast<DWORD>(error.code().value());
        if (code == ERROR_RESOURCE_DATA_NOT_FOUND) return {code, false, L"Installation is unavailable: this preview installer has no embedded Microsoft-signed driver package. Obtain a signed release from the project before installing."};
        return {code, false, L"The operation could not complete. " + Message(code)};
    } catch (...) { return {ERROR_NOT_ENOUGH_MEMORY, false, L"The operation could not complete because resources are unavailable. Review the current device inventory before retrying."}; }
}
bool Setup::Restart() noexcept {
    Handle token; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token.h)) return false;
    TOKEN_PRIVILEGES privileges{}; privileges.PrivilegeCount = 1;
    if (!LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid)) return false;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED; SetLastError(ERROR_SUCCESS);
    if (!AdjustTokenPrivileges(token.h, FALSE, &privileges, 0, nullptr, nullptr) || GetLastError() != ERROR_SUCCESS) return false;
    return ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_INSTALLATION | SHTDN_REASON_FLAG_PLANNED) != FALSE;
}
