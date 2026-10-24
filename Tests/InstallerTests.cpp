#include "../Installer/Setup.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <objbase.h>

namespace fs = std::filesystem;
namespace {
unsigned assertions = 0;
void Check(bool condition, const char* message) { ++assertions; if (!condition) throw std::runtime_error(message); }
const char* Fixture =
    "[Version]\r\nSignature=\"$Windows NT$\"\r\nClass=MEDIA\r\nClassGuid={4D36E96C-E325-11CE-BFC1-08002BE10318}\r\n"
    "Provider=%Provider%\r\nCatalogFile=TitanAudioLink.cat\r\nPnpLockDown=1\r\nDriverVer=10/24/2026,0.1.0.0\r\n"
    "[Manufacturer]\r\n%Provider%=Models,NTamd64.10.0...19041\r\n"
    "[Models.NTamd64.10.0...19041]\r\n%Description%=TitanAudioLink_SA,ROOT\\TitanAudioLink\r\n"
    "[TitanAudioLink_SA.NT]\r\nCopyFiles=TitanAudioLink_SA.CopyList\r\n"
    "[DestinationDirs]\r\nTitanAudioLink_SA.CopyList=13\r\n[TitanAudioLink_SA.CopyList]\r\nTitanAudioLink.sys\r\n"
    "[TitanAudioLink_SA.NT.Services]\r\nAddService=TitanAudioLink,2,AudioService\r\n"
    "[AudioService]\r\nServiceType=1\r\nStartType=3\r\nServiceBinary=%13%\\TitanAudioLink.sys\r\n"
    "[Strings]\r\nProvider=\"Titanfyre\"\r\nDescription=\"Titan Audio Link\"\r\n";
std::string Replace(std::string value, const std::string& from, const std::string& to) {
    size_t at = value.find(from); if (at == std::string::npos) throw std::runtime_error("Test fixture replacement missing");
    value.replace(at, from.size(), to); return value;
}
struct Directory {
    fs::path path;
    Directory() {
        wchar_t parent[MAX_PATH]{}; if (!GetTempPathW(MAX_PATH, parent)) throw std::runtime_error("No test temp directory");
        GUID guid{}; if (FAILED(CoCreateGuid(&guid))) throw std::runtime_error("No test identifier");
        wchar_t id[40]{}; StringFromGUID2(guid, id, 40); path = fs::path(parent) / (std::wstring(L"TitanAudioInstallerTests-") + id);
        if (!fs::create_directory(path)) throw std::runtime_error("Test directory already exists");
    }
    ~Directory() { std::error_code ignored; for (const auto* name : {L"test.inf", L"test.cat"}) fs::remove(path / name, ignored); fs::remove(path, ignored); }
    fs::path Write(const std::string& content, const wchar_t* name = L"test.inf") {
        auto file = path / name; std::ofstream output(file, std::ios::binary | std::ios::trunc);
        output.write(content.data(), static_cast<std::streamsize>(content.size())); if (!output) throw std::runtime_error("Cannot write test fixture");
        return file;
    }
};
bool Parse(std::initializer_list<const wchar_t*> arguments, Setup::Command& command) {
    return Setup::ParseArguments(static_cast<int>(arguments.size()), arguments.begin(), command);
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        Setup::Command command;
        Check(Parse({L"setup", L"/install"}, command) && command.action == Setup::Action::Install, "Install command");
        Check(Parse({L"setup", L"/INSTALL"}, command), "Case insensitive command");
        Check(Parse({L"setup", L"/add-pair"}, command) && command.action == Setup::Action::AddPair, "Add pair command");
        Check(Parse({L"setup", L"/uninstall"}, command) && command.action == Setup::Action::Uninstall, "Uninstall command");
        Check(Parse({L"setup", L"/status"}, command) && command.action == Setup::Action::Status, "Status command");
        Check(Parse({L"setup", L"/license"}, command) && command.action == Setup::Action::License, "License command");
        auto license = Setup::LicenseText();
        Check(license.find(L"Microsoft Public License (MS-PL)") != std::wstring::npos &&
            license.find(L"Copyright (c) 2015 Microsoft") != std::wstring::npos &&
            license.find(L"particular purpose and non-infringement") != std::wstring::npos,
            "Full Microsoft sample license embedded in executable");
        Check(license.find(L"bb68ed5e770bce04dc5395ec9258dafab034184d") != std::wstring::npos &&
            license.find(L"Copyright (c) Microsoft Corporation") != std::wstring::npos, "Upstream attribution embedded in executable");
        Check(Parse({L"setup", L"/remove-pair", L"ROOT\\MEDIA\\0000"}, command) && command.instance == L"ROOT\\MEDIA\\0000", "Exact root selector");
        for (const auto* bad : {L"", L"ROOT\\MEDIA", L"USB\\MEDIA\\0000", L"ROOT\\Other\\0000", L"ROOT\\TitanAudioLinkEvil\\0000", L"ROOT\\MEDIA\\*", L"ROOT\\MEDIA\\..", L"ROOT\\MEDIA\\a/b", L"ROOT\\MEDIA\\a b", L"ROOT\\MEDIA\\a\n", L"ROOT\\MEDIA\\a\\b"})
            Check(!Parse({L"setup", L"/remove-pair", bad}, command), "Unsafe or unrelated instance rejected");
        Check(!Parse({L"setup"}, command), "Empty command rejected");
        Check(!Parse({L"setup", L"/install", L"extra"}, command), "Unexpected arguments rejected");
        Check(!Parse({L"setup", L"/remove-pair"}, command), "Missing selector rejected");
        Check(!Parse({L"setup", L"/force"}, command), "Force flag rejected");
        Check(!Setup::ParseArguments(2, nullptr, command), "Null argv rejected");
        Check(Setup::CanAdd(0) && Setup::CanAdd(3) && !Setup::CanAdd(4) && !Setup::CanAdd(100), "Pair limit boundary");
        Check(Setup::ValidInstance(L"ROOT\\TitanAudioLink\\0000"), "Explicit hardware root instance");
        Check(!Setup::ValidInstance(L"ROOT\\MEDIA\\" + std::wstring(220, L'A')), "Oversized selector rejected");
        for (const auto* good : {L"oem0.inf", L"OEM123.INF", L"oem123456.inf"}) Check(Setup::PublishedInfName(good), "Published INF accepted");
        for (const auto* bad : {L"oem.inf", L"oem*.inf", L"..\\oem1.inf", L"C:\\oem1.inf", L"oem-1.inf", L"oem1.inf:stream", L"oem1.dll"}) Check(!Setup::PublishedInfName(bad), "Arbitrary driver path rejected");
        Directory temporary;
        Check(Setup::PackageMatches(temporary.Write(Fixture).wstring()), "Exact minimal INF identity accepted");
        const std::pair<const char*, const char*> changes[] = {
            {"Class=MEDIA", "Class=System"},
            {"{4D36E96C-E325-11CE-BFC1-08002BE10318}", "{4D36E97D-E325-11CE-BFC1-08002BE10318}"},
            {"CatalogFile=TitanAudioLink.cat", "CatalogFile=Other.cat"},
            {"Provider=\"Titanfyre\"", "Provider=\"Other\""},
            {"PnpLockDown=1", "PnpLockDown=0"},
            {"ROOT\\TitanAudioLink", "ROOT\\OtherAudio"},
            {"%Description%=TitanAudioLink_SA", "%Description%=OtherInstall"},
            {"AddService=TitanAudioLink,2", "AddService=OtherService,2"},
            {"AddService=TitanAudioLink,2", "AddService=TitanAudioLink,0"},
            {"ServiceType=1", "ServiceType=16"},
            {"StartType=3", "StartType=0"},
            {"ServiceBinary=%13%\\TitanAudioLink.sys", "ServiceBinary=%12%\\Other.sys"},
            {"TitanAudioLink_SA.CopyList=13", "TitanAudioLink_SA.CopyList=12"},
            {"NTamd64.10.0...19041", "NTarm64.10.0...19041"}
        };
        for (const auto& change : changes) {
            bool rejected = !Setup::PackageMatches(temporary.Write(Replace(Fixture, change.first, change.second)).wstring());
            if (!rejected) std::cerr << "Unexpectedly accepted INF mutation: " << change.first << " -> " << change.second << '\n';
            Check(rejected, "Mismatched package identity rejected");
        }
        Check(!Setup::PackageMatches(temporary.Write(std::string(Fixture) + "\r\n[Models.NTamd64.10.0...19041]\r\nOther=OtherInstall,ROOT\\Other\r\n").wstring()), "Extra hardware model rejected");
        Check(!Setup::PackageMatches(temporary.Write("not an INF").wstring()), "Invalid INF rejected");
        Check(!Setup::PackageMatches((temporary.path / L"missing.inf").wstring()), "Missing INF rejected");
        Check(!Setup::CatalogTrusted(temporary.Write("unsigned catalog placeholder", L"test.cat").wstring()), "Unsigned catalog refused");
        Check(!Setup::CatalogTrusted((temporary.path / L"missing.cat").wstring()), "Missing catalog refused");
        if (argc >= 2) Check(Setup::PackageMatches(argv[1]), "Actual private driver INF identity accepted");
        if (argc == 3) Check(Setup::CatalogTrusted(argv[2]), "Existing genuine Microsoft hardware catalog and root policy accepted");
        // No call to Execute, device-install API, certificate-store mutation or driver start.
        std::cout << assertions << " installer parsing and package guard assertions passed. No system mutations.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
