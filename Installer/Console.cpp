#include "Setup.h"
#include <iostream>
int wmain(int argc, wchar_t** argv) {
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        std::wcerr << L"Could not establish the protected system DLL search path. Windows error: " << GetLastError() << L'\n';
        return ERROR_DLL_INIT_FAILED;
    }
    Setup::Command command;
    if (!Setup::ParseArguments(argc, argv, command)) {
        std::wcout << L"Titan Audio Link Setup\n\n"
            L"/status                     Read installed pairs and Windows problem codes\n"
            L"/license                    Read embedded notices and full Microsoft sample license\n"
            L"/install                    Install or update; reuse existing pairs\n"
            L"/add-pair                   Add one independent pair (maximum four)\n"
            L"/remove-pair \"ROOT\\...\"   Remove the exact pair shown by /status\n"
            L"/uninstall                  Remove own pairs and unused own driver packages\n\n"
            L"Administrator rights required for changes. No automatic restart.\n";
        return ERROR_INVALID_PARAMETER;
    }
    auto result = Setup::Execute(command);
    std::wcout << result.detail << L'\n';
    if (result.error) std::wcout << L"Windows error: " << result.error << L'\n';
    if (result.reboot) std::wcout << L"Restart Windows to complete this operation.\n";
    return result.error ? static_cast<int>(result.error) : result.reboot ? ERROR_SUCCESS_REBOOT_REQUIRED : ERROR_SUCCESS;
}
