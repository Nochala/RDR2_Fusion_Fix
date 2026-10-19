#include "main.h"
#include "script.h"
#include "Logger.h"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(module);
        Logger::LogModuleLoaded();
        scriptRegister(module, ScriptMain);
        break;
    case DLL_PROCESS_DETACH:
        scriptUnregister(module);
        break;
    }

    return TRUE;
}
