#include "main.h"
#include "script.h"
#include "Logger.h"
#include "GameplayFixes.h"

namespace
{
    DWORD WINAPI EarlyStartupThread(LPVOID)
    {
        GameplayFixes::InitializeEarly();
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(module);
        Logger::LogModuleLoaded();
        scriptRegister(module, ScriptMain);

        HANDLE worker = CreateThread(nullptr, 0, EarlyStartupThread, nullptr, 0, nullptr);
        if (worker != nullptr)
            CloseHandle(worker);
        else
            OutputDebugStringA("[GameplayFixesRDR2] Early startup thread could not be created; configuration will initialize in ScriptMain.\n");
        break;
    }
    case DLL_PROCESS_DETACH:
        scriptUnregister(module);
        break;
    }

    return TRUE;
}
