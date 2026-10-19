#include "GameplayFixes.h"
#include "Logger.h"
#include "Main/Headers/Modules.h"
#include <windows.h>
#include <algorithm>
#include <string>

namespace
{
    GameplayFixes::Settings settings;

    std::wstring GetIniPath()
    {
        wchar_t path[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return {};

        std::wstring iniPath(path, length);
        const size_t separator = iniPath.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
            return {};

        iniPath.resize(separator + 1);
        iniPath += L"GameplayFixesRDR2.ini";
        return iniPath;
    }

    bool ReadIniBoolean(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool defaultValue)
    {
        wchar_t value[16] = {};
        GetPrivateProfileStringW(section, key, defaultValue ? L"true" : L"false", value, 16, path.c_str());

        if (CompareStringOrdinal(value, -1, L"true", -1, TRUE) == CSTR_EQUAL)
            return true;
        if (CompareStringOrdinal(value, -1, L"false", -1, TRUE) == CSTR_EQUAL)
            return false;

        return defaultValue;
    }

    bool WriteIniValue(const std::wstring& path, const wchar_t* section, const wchar_t* key,
        const wchar_t* value, const char* description)
    {
        if (WritePrivateProfileStringW(section, key, value, path.c_str()))
            return true;

        const DWORD error = GetLastError();
        Logger::Log((std::string("[ERROR] INI: could not write ") + description +
            " (Win32 error " + std::to_string(error) + ").").c_str());
        return false;
    }

    bool EnsureIniExists(const std::wstring& path)
    {
        bool success = true;
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                Logger::Log((std::string("[WARNING] INI: unable to inspect existing file (Win32 error ") +
                    std::to_string(error) + ").").c_str());

            success = WriteIniValue(path, L"General", L"Enabled", L"true", "General.Enabled") && success;
            success = WriteIniValue(path, L"General", L"UpdateIntervalMs", L"250", "General.UpdateIntervalMs") && success;
            if (success)
                Logger::Log("[SUCCESS] INI: created default configuration.");
        }
        else if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            Logger::Log("[ERROR] INI: configuration path points to a directory, not a file.");
            return false;
        }

        wchar_t existing[8] = {};
        GetPrivateProfileStringW(L"General", L"Logging", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"General", L"Logging", L"true", "General.Logging") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Startup", L"SkipIntroSplashesAndLegal", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Startup", L"SkipIntroSplashesAndLegal", L"true",
                "Startup.SkipIntroSplashesAndLegal") && success;

        return success;
    }
}

namespace GameplayFixes
{
    void Initialize()
    {
        const std::wstring iniPath = GetIniPath();
        if (!iniPath.empty())
            Logger::SetEnabled(ReadIniBoolean(iniPath, L"General", L"Logging", true));

        Logger::LogStartup();
        if (!iniPath.empty())
        {
            Logger::Log("[INFO] INI: reading GameplayFixesRDR2.ini from the game directory.");
            const bool iniReady = EnsureIniExists(iniPath);
            if (!iniReady)
                Logger::Log("[WARNING] INI: some settings could not be saved; defaults may be used.");

            settings.enabled = ReadIniBoolean(iniPath, L"General", L"Enabled", true);
            settings.skipIntroSplashesAndLegal = ReadIniBoolean(iniPath, L"Startup", L"SkipIntroSplashesAndLegal", true);
            const unsigned int interval = GetPrivateProfileIntW(L"General", L"UpdateIntervalMs", 250, iniPath.c_str());
            settings.updateIntervalMs = (std::min)(interval, 60000u);
            Logger::Log(iniReady ? "[SUCCESS] INI: configuration settings read." :
                "[WARNING] INI: settings read attempted with configuration errors; defaults may be in use.");
        }
        else
            Logger::Log("[ERROR] INI: game directory could not be resolved; using default settings.");

        Logger::Log((std::string("[INFO] Settings: Enabled=") + (settings.enabled ? "true" : "false") +
            ", Logging=true, SkipIntroSplashesAndLegal=" + (settings.skipIntroSplashesAndLegal ? "true" : "false") +
            ", UpdateIntervalMs=" + std::to_string(settings.updateIntervalMs) + ".").c_str());
        Main::Initialize(settings);
        Logger::Log("[INFO] GameplayFixesRDR2: module initialization finished; see individual feature results above.");
    }

    void Update()
    {
        if (settings.enabled)
            Main::Update(settings);
    }

    unsigned int GetUpdateIntervalMs()
    {
        return settings.updateIntervalMs;
    }
}
