#pragma once

namespace GameplayFixes
{
    struct Settings
    {
        bool enabled = true;
        bool skipIntroSplashesAndLegal = true;
        unsigned int updateIntervalMs = 250;
    };

    void Initialize();
    void Update();
    unsigned int GetUpdateIntervalMs();
}
