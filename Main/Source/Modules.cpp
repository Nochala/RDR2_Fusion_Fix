#include "Modules.h"
#include "GameplayFixes.h"
#include "SkipIntro.h"
#include "RemoveBlackBars.h"
#include "Camera.h"
#include "NativeDiagnostics.h"

namespace Main
{
    void InitializeEarly(const GameplayFixes::Settings& settings)
    {
        SkipIntro::Initialize(settings.enabled && settings.skipIntroSplashes,
            settings.enabled && settings.skipLegal, settings.enabled && settings.straightToStoryMode);
    }

    void Initialize(const GameplayFixes::Settings& settings)
    {
        SkipIntro::FinishStartup();
        RemoveBlackBars::Initialize(settings.enabled && settings.removeBlackBars);
        Camera::Initialize(settings.enabled && settings.removeCameraFade);
        NativeDiagnostics::Initialize(settings.enabled && settings.dumpNativeHashes,
            settings.enabled && settings.traceCameraNatives);
    }

    void Update(const GameplayFixes::Settings& settings)
    {
        if (settings.removeCameraFade)
            Camera::Update();
        if (settings.traceCameraNatives)
            NativeDiagnostics::Update();
    }
}
