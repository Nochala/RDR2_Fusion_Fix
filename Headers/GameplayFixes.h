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
        bool disableIdleCamera = false;
        bool disableCameraSmoothing = false;
        bool dumpNativeHashes = false;
    };

    void InitializeEarly();
    void Initialize();
    void Update();
    unsigned int GetUpdateDelay();
}
