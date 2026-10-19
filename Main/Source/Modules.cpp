#include "Modules.h"
#include "GameplayFixes.h"
#include "SkipIntro.h"

namespace Main
{
    void InitializeEarly(const GameplayFixes::Settings& settings)
    {
        SkipIntro::Initialize(settings.enabled && settings.skipIntroSplashes,
            settings.enabled && settings.skipLegal);
    }

    void Initialize(const GameplayFixes::Settings&)
    {
    }

    void Update(const GameplayFixes::Settings&)
    {
    }
}
