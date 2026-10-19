#pragma once

namespace Logger
{
    void LogModuleLoaded();
    void LogScriptEntry();
    void SetEnabled(bool enabled);
    void SetDebugEnabled(bool enabled);
    bool IsDebugEnabled();
    void Debug(const char* message);
    void LogStartup();
    void Log(const char* message);
}
