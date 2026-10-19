#include "script.h"
#include "main.h"
#include "GameplayFixes.h"

void ScriptMain()
{
    GameplayFixes::Initialize();

    while (true)
    {
        GameplayFixes::Update();
        WAIT(GameplayFixes::GetUpdateIntervalMs());
    }
}
