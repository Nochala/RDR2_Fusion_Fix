#include "../Headers/Logger.h"
#include <windows.h>
#include <cstdio>
#include <string>

namespace
{
    bool loggingEnabled = true;
    bool debugEnabled = false;

    bool BuildGamePath(const wchar_t* filename, wchar_t (&path)[MAX_PATH])
    {
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return false;

        DWORD directoryEnd = length;
        while (directoryEnd > 0 && path[directoryEnd - 1] != L'\\' && path[directoryEnd - 1] != L'/')
            --directoryEnd;
        if (directoryEnd == 0)
            return false;

        DWORD position = directoryEnd;
        for (DWORD i = 0; filename[i] != L'\0'; ++i)
        {
            if (position + 1 >= MAX_PATH)
                return false;
            path[position++] = filename[i];
        }
        path[position] = L'\0';
        return true;
    }

    bool IsFalse(const wchar_t* value)
    {
        const wchar_t expected[] = L"false";
        for (unsigned int i = 0; i < 5; ++i)
        {
            const wchar_t character = value[i];
            if (character != expected[i] && character != expected[i] - (L'a' - L'A'))
                return false;
        }
        return value[5] == L'\0';
    }

    std::string FormatLine(const char* message)
    {
        SYSTEMTIME time = {};
        GetLocalTime(&time);
        char prefix[64] = {};
        std::snprintf(prefix, sizeof(prefix), "%04u-%02u-%02u %02u:%02u:%02u.%03u ",
            static_cast<unsigned int>(time.wYear), static_cast<unsigned int>(time.wMonth),
            static_cast<unsigned int>(time.wDay), static_cast<unsigned int>(time.wHour),
            static_cast<unsigned int>(time.wMinute), static_cast<unsigned int>(time.wSecond),
            static_cast<unsigned int>(time.wMilliseconds));
        return std::string(prefix) + message + "\r\n";
    }

    bool Append(const wchar_t* path, const char* content, DWORD length, DWORD& error, bool reset = false)
    {
        HANDLE file = CreateFileW(path, reset ? GENERIC_WRITE : FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, reset ? CREATE_ALWAYS : OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = GetLastError();
            return false;
        }

        DWORD written = 0;
        const bool success = WriteFile(file, content, length, &written, nullptr) && written == length;
        error = success ? ERROR_SUCCESS : GetLastError();
        if (!success && error == ERROR_SUCCESS)
            error = ERROR_WRITE_FAULT;
        CloseHandle(file);
        return success;
    }
}

namespace Logger
{
    void LogModuleLoaded()
    {
        wchar_t iniPath[MAX_PATH] = {};
        if (!BuildGamePath(L"GameplayFixesRDR2.ini", iniPath))
        {
            OutputDebugStringA("[GameplayFixesRDR2] Unable to resolve INI path during DLL load.\n");
            return;
        }

        wchar_t value[16] = {};
        GetPrivateProfileStringW(L"General", L"Logging", L"true", value, 16, iniPath);
        if (IsFalse(value))
            return;

        wchar_t logPath[MAX_PATH] = {};
        if (!BuildGamePath(L"GameplayFixesRDR2.log", logPath))
        {
            OutputDebugStringA("[GameplayFixesRDR2] Unable to resolve log path during DLL load.\n");
            return;
        }

        constexpr char message[] = "[BOOT] GameplayFixesRDR2.asi loaded; waiting for ScriptMain.\r\n";
        DWORD error = ERROR_SUCCESS;
        if (!Append(logPath, message, sizeof(message) - 1, error, true))
            OutputDebugStringA("[GameplayFixesRDR2] Cannot create game-directory log during DLL load.\n");
    }

    void LogScriptEntry()
    {
        wchar_t iniPath[MAX_PATH] = {};
        if (BuildGamePath(L"GameplayFixesRDR2.ini", iniPath))
        {
            wchar_t value[16] = {};
            GetPrivateProfileStringW(L"General", L"Logging", L"true", value, 16, iniPath);
            if (IsFalse(value))
            {
                loggingEnabled = false;
                return;
            }
        }

        loggingEnabled = true;
        Log("[BOOT] ScriptMain entry reached; starting gameplay initialization.");
    }

    void SetEnabled(bool enabled)
    {
        loggingEnabled = enabled;
    }

    void SetDebugEnabled(bool enabled)
    {
        debugEnabled = enabled;
    }

    bool IsDebugEnabled()
    {
        return loggingEnabled && debugEnabled;
    }

    void Debug(const char* message)
    {
        if (!IsDebugEnabled() || message == nullptr)
            return;

        const std::string line = std::string("[DEBUG] ") + message;
        Log(line.c_str());
    }

    void Log(const char* message)
    {
        if (!loggingEnabled || message == nullptr)
            return;

        wchar_t logPath[MAX_PATH] = {};
        if (!BuildGamePath(L"GameplayFixesRDR2.log", logPath))
        {
            OutputDebugStringA("[GameplayFixesRDR2] Unable to resolve game-directory log path.\n");
            return;
        }

        const std::string line = FormatLine(message);
        DWORD error = ERROR_SUCCESS;
        if (!Append(logPath, line.c_str(), static_cast<DWORD>(line.size()), error))
        {
            OutputDebugStringA("[GameplayFixesRDR2] Cannot append to game-directory log.\n");
            OutputDebugStringA(line.c_str());
        }
    }

    void LogStartup()
    {
        Log("[SUCCESS] GameplayFixesRDR2 ScriptMain started; logging is enabled.");
    }
}
