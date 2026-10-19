#include "Modules.h"
#include "GameplayFixes.h"
#include "SkipIntro.h"

namespace Main
{
    void Initialize(const GameplayFixes::Settings& settings)
    {
        SkipIntro::Initialize(settings.enabled && settings.skipIntroSplashesAndLegal);
    }

    void Update(const GameplayFixes::Settings&)
    {
    }
}
