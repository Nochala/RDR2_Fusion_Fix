#include "SkipIntro.h"
#include "Logger.h"
#include <windows.h>
#include <string>
#include <vector>
#include <cstring>

namespace
{
    const char startupData[] = R"GFSTART(<?xml version="1.0" encoding="UTF-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
 <disabledFiles />
 <includedXmlFiles itemType="CDataFileMgr__DataFileArray" />
 <includedDataFiles />
 <dataFiles itemType="CDataFileMgr__DataFile">
  <Item>
   <filename>platform:/data/cdimages/scaleform_platform_pc.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/data/ui/value_conversion.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/data/ui/widgets.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/textures/ui/ui_photo_stickers.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/textures/ui/ui_platform.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/data/ui/stylesCatalog</filename>
   <fileType>aWeaponizeDisputants</fileType>
  </Item>
  <Item>
   <filename>platform:/data/cdimages/scaleform_frontend.rpf</filename>
   <fileType>RPF_FILE_PRE_INSTALL</fileType>
  </Item>
  <Item>
   <filename>platform:/textures/ui/ui_startup_textures.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/data/ui/startup_data.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/GameplayFixesRDR2_data/boot_launcher_flow.ymt</filename>
   <fileType>STREAMING_FILE</fileType>
   <registerAs>boot_flow/boot_launcher_flow</registerAs>
   <overlay value="false"/>
   <patchFile value="false"/>
  </Item>
 </dataFiles>
 <contentChangeSets itemType="CDataFileMgr__ContentChangeSet" />
 <patchFiles />
</CDataFileMgr__ContentsOfDataFileXml>
)GFSTART";
    const char bootFlowData[] = R"GFBOOT(<?xml version="1.0" encoding="UTF-8"?>
<rage__fwuiFlowBlock>
 <ID>boot_flow</ID>
 <EntryPoints>
  <Item>
   <ID>default_entry</ID>
   <Target>wait_for_profile</Target>
  </Item>
  <Item>
   <ID>sign_out</ID>
   <Target>wait_for_profile</Target>
  </Item>
 </EntryPoints>
 <ExitPoints>
  <Item>
   <ID>exit</ID>
  </Item>
 </ExitPoints>
 <FlowRoot>
  <ID>input_context_switch</ID>
  <State type="StateSetInputContext">
   <ContextType>BOOT_FLOW</ContextType>
  </State>
  <Children>
   <Item>
    <ID>wait_for_profile</ID>
    <State type="StateWaitForProfileLoad" />
    <LinkMap>
     <Item key="next"><Target>exit</Target><LinkInfo>LINK_TO_EXTERNAL</LinkInfo></Item>
     <Item key="exit"><Target>exit</Target><LinkInfo>LINK_TO_EXTERNAL</LinkInfo></Item>
     <Item key="yes"><Target>exit</Target><LinkInfo>LINK_TO_EXTERNAL</LinkInfo></Item>
    </LinkMap>
   </Item>
  </Children>
 </FlowRoot>
</rage__fwuiFlowBlock>
)GFBOOT";

    std::wstring GameRoot()
    {
        wchar_t path[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return {};

        std::wstring root(path, length);
        const auto separator = root.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
            return {};

        root.resize(separator + 1);
        return root;
    }

    bool Exists(const std::wstring& path)
    {
        return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    bool ReadExact(const std::wstring& path, const char* expected, size_t length)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        LARGE_INTEGER fileSize = {};
        bool matching = false;
        if (GetFileSizeEx(file, &fileSize) && fileSize.QuadPart == static_cast<LONGLONG>(length))
        {
            std::vector<char> contents(length);
            DWORD read = 0;
            matching = ReadFile(file, contents.data(), static_cast<DWORD>(contents.size()), &read, nullptr) &&
                read == length && memcmp(contents.data(), expected, length) == 0;
        }
        CloseHandle(file);
        return matching;
    }

    bool WriteNew(const std::wstring& path, const char* data, size_t length)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        DWORD written = 0;
        const bool good = WriteFile(file, data, static_cast<DWORD>(length), &written, nullptr) && written == length;
        const DWORD error = good ? ERROR_SUCCESS : GetLastError();
        CloseHandle(file);
        if (!good)
        {
            DeleteFileW(path.c_str());
            SetLastError(error == ERROR_SUCCESS ? ERROR_WRITE_FAULT : error);
        }
        return good;
    }

    bool RemoveOwned(const std::wstring& path, const char* data, size_t length)
    {
        if (!Exists(path))
            return true;
        return ReadExact(path, data, length) && DeleteFileW(path.c_str());
    }
}

namespace Main::SkipIntro
{
    void Initialize(bool enabled)
    {
        const std::wstring root = GameRoot();
        if (root.empty())
        {
            Logger::Log("[ERROR] SkipIntro: unable to resolve the RDR2 game directory.");
            return;
        }

        const std::wstring dataDirectory = root + L"x64\\GameplayFixesRDR2_data";
        const std::wstring flowPath = dataDirectory + L"\\boot_launcher_flow.ymt";
        const std::wstring startupPath = root + L"x64\\data\\startup.meta";
        const size_t flowLength = sizeof(bootFlowData) - 1;
        const size_t startupLength = sizeof(startupData) - 1;

        if (!enabled)
        {
            const bool startupRemoved = RemoveOwned(startupPath, startupData, startupLength);
            const bool flowRemoved = RemoveOwned(flowPath, bootFlowData, flowLength);
            if (startupRemoved && flowRemoved)
            {
                RemoveDirectoryW(dataDirectory.c_str());
                Logger::Log("[SUCCESS] SkipIntro: disabled; owned boot-flow files removed if present. Restart RDR2 to apply.");
            }
            else
                Logger::Log("[ERROR] SkipIntro: cleanup incomplete; existing boot-flow files were not owned by this mod or could not be removed.");
            return;
        }

        if ((Exists(startupPath) && !ReadExact(startupPath, startupData, startupLength)) ||
            (Exists(flowPath) && !ReadExact(flowPath, bootFlowData, flowLength)))
        {
            Logger::Log("[ERROR] SkipIntro: conflicting startup files detected; existing files were left untouched.");
            return;
        }

        if (Exists(startupPath) && Exists(flowPath))
        {
            Logger::Log("[SUCCESS] SkipIntro: startup splash/legal replacements are already installed.");
            return;
        }

        if (!CreateDirectoryW(dataDirectory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        {
            const DWORD error = GetLastError();
            Logger::Log((std::string("[ERROR] SkipIntro: failed to create asset directory (Win32 error ") +
                std::to_string(error) + ").").c_str());
            return;
        }

        bool createdFlow = false;
        if (!Exists(flowPath))
        {
            if (!WriteNew(flowPath, bootFlowData, flowLength))
            {
                const DWORD error = GetLastError();
                Logger::Log((std::string("[ERROR] SkipIntro: failed to create boot-flow asset (Win32 error ") +
                    std::to_string(error) + ").").c_str());
                return;
            }
            createdFlow = true;
        }

        if (!Exists(startupPath) && !WriteNew(startupPath, startupData, startupLength))
        {
            const DWORD error = GetLastError();
            if (createdFlow && !RemoveOwned(flowPath, bootFlowData, flowLength))
                Logger::Log("[ERROR] SkipIntro: unable to remove newly created boot-flow asset after startup.meta failure.");
            Logger::Log((std::string("[ERROR] SkipIntro: could not create startup.meta (Win32 error ") +
                std::to_string(error) + "); existing startup modifications were not overwritten.").c_str());
            return;
        }

        Logger::Log("[SUCCESS] SkipIntro: startup splash/legal replacement installed. Restart RDR2 to activate.");
    }
}
