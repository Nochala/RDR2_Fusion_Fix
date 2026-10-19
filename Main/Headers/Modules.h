#pragma once

namespace GameplayFixes
{
    struct Settings;
}

namespace Main
{
    void InitializeEarly(const GameplayFixes::Settings& settings);
    void Initialize(const GameplayFixes::Settings& settings);
    void Update(const GameplayFixes::Settings& settings);
}
