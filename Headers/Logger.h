#pragma once

namespace Logger
{
    void LogModuleLoaded();
    void SetEnabled(bool enabled);
    void LogStartup();
    void Log(const char* message);
}
