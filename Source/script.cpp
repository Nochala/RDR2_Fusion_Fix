#include "script.h"
#include "main.h"
#include "GameplayFixes.h"
#include "Logger.h"

void ScriptMain()
{
    GameplayFixes::InitializeEarly();
    Logger::LogScriptEntry();
    GameplayFixes::Initialize();
    Logger::Log("[SUCCESS] ScriptMain initialization returned successfully.");

    bool firstUpdate = true;
    while (true)
    {
        GameplayFixes::Update();
        if (firstUpdate)
        {
            Logger::Log("[SUCCESS] ScriptMain completed its first update.");
            firstUpdate = false;
        }
        WAIT(GameplayFixes::GetUpdateDelay());
    }
}
