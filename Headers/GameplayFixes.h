#pragma once

namespace GameplayFixes
{
    struct Settings
    {
        bool enabled = true;
        bool skipIntroSplashes = true;
        bool skipLegal = true;
        bool straightToStoryMode = false;
        bool removeBlackBars = false;
        bool removeCameraFade = false;
        bool dumpNativeHashes = false;
        bool traceCameraNatives = false;
    };

    void InitializeEarly();
    void Initialize();
    void Update();
    unsigned int GetUpdateDelay();
}
