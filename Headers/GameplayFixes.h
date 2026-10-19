#pragma once

namespace GameplayFixes
{
    struct Settings
    {
        bool enabled = true;
        bool skipIntroSplashes = true;
        bool skipLegal = true;
        unsigned int updateIntervalMs = 250;
    };

    void InitializeEarly();
    void Initialize();
    void Update();
    unsigned int GetUpdateIntervalMs();
}
