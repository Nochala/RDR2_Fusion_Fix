#include "Camera.h"
#include "../../Headers/Logger.h"
#include "main.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>

namespace
{
    constexpr std::uint64_t kFirstPersonMode = 0xD1BA66940E94C547ULL;
    constexpr std::uint64_t kGameplayCameraRendering = 0x8660EA714834E412ULL;
    constexpr std::uint64_t kPlayerId = 0x217E9DC48139933DULL;
    constexpr std::uint64_t kPlayerControlOn = 0x7964097FCE4C244BULL;
    constexpr std::uint64_t kScreenFadedIn = 0x37F9A426FBCF4AF2ULL;
    constexpr std::uint64_t kScreenFadedOut = 0xF5472C80DF2FF847ULL;
    constexpr std::uint64_t kScreenFadingOut = 0x02F39BEFE7B88D00ULL;
    constexpr std::uint64_t kScreenFadingIn = 0x0CECCC63FFA2EF24ULL;

    bool removeFadeEnabled = false;
    bool hadCameraState = false;
    bool wasFirstPerson = false;
    ULONGLONG fadeProbeEnd = 0;
    unsigned int fadeProbeSamples = 0;
    unsigned int fadeProbeStates = 0;

    std::uint64_t NativeValue(std::uint64_t hash)
    {
        nativeInit(hash);
        return *nativeCall();
    }

    bool NativeBoolean(std::uint64_t hash)
    {
        return static_cast<std::uint32_t>(NativeValue(hash)) != 0;
    }

    bool PlayerHasControl()
    {
        nativeInit(kPlayerId);
        const std::uint64_t player = *nativeCall();
        nativeInit(kPlayerControlOn);
        nativePush64(player);
        return static_cast<std::uint32_t>(*nativeCall()) != 0;
    }

    void FinishFadeProbe()
    {
        if (fadeProbeEnd == 0)
            return;

        char message[225] = {};
        std::snprintf(message, sizeof(message),
            "Camera: perspective-switch fade states; samples=%u, fadedIn=%s, fadedOut=%s, fadingIn=%s, fadingOut=%s.",
            fadeProbeSamples,
            (fadeProbeStates & 1) ? "true" : "false",
            (fadeProbeStates & 2) ? "true" : "false",
            (fadeProbeStates & 4) ? "true" : "false",
            (fadeProbeStates & 8) ? "true" : "false");
        Logger::Debug(message);
        fadeProbeEnd = 0;
        fadeProbeSamples = 0;
        fadeProbeStates = 0;
    }
}

namespace Main::Camera
{
    void Initialize(bool removeCameraFade)
    {
        removeFadeEnabled = removeCameraFade;
        hadCameraState = false;
        fadeProbeEnd = 0;
        fadeProbeSamples = 0;
        fadeProbeStates = 0;

        if (removeFadeEnabled)
        {
            Logger::Log("[WARNING] Camera: RemoveCameraFade is not yet effective; native screen-fade cancellation is disabled while the flash effect is investigated.");
            if (Logger::IsDebugEnabled())
                Logger::Debug("Camera: camera-switch fade-state diagnostics enabled.");
        }
    }

    void Update()
    {
        if (!removeFadeEnabled || !Logger::IsDebugEnabled())
            return;

        const std::uint64_t raw = NativeValue(kFirstPersonMode);
        const bool firstPerson = static_cast<std::uint32_t>(raw) != 0;
        const ULONGLONG now = GetTickCount64();

        if (!hadCameraState)
        {
            wasFirstPerson = firstPerson;
            hadCameraState = true;
            char message[130] = {};
            std::snprintf(message, sizeof(message), "Camera: first perspective sample; firstPerson=%s, raw=0x%016llX.",
                firstPerson ? "true" : "false", static_cast<unsigned long long>(raw));
            Logger::Debug(message);
        }
        else if (firstPerson != wasFirstPerson)
        {
            FinishFadeProbe();
            wasFirstPerson = firstPerson;
            const bool gameplayCamera = NativeBoolean(kGameplayCameraRendering);
            const bool playerControl = PlayerHasControl();
            char message[185] = {};
            std::snprintf(message, sizeof(message),
                "Camera: perspective changed; firstPerson=%s, gameplayCamera=%s, playerControl=%s.",
                firstPerson ? "true" : "false", gameplayCamera ? "true" : "false", playerControl ? "true" : "false");
            Logger::Debug(message);
            if (gameplayCamera && playerControl)
                fadeProbeEnd = now + 350;
        }

        if (fadeProbeEnd != 0)
        {
            if (now >= fadeProbeEnd)
            {
                FinishFadeProbe();
                return;
            }

            ++fadeProbeSamples;
            if (NativeBoolean(kScreenFadedIn)) fadeProbeStates |= 1;
            if (NativeBoolean(kScreenFadedOut)) fadeProbeStates |= 2;
            if (NativeBoolean(kScreenFadingIn)) fadeProbeStates |= 4;
            if (NativeBoolean(kScreenFadingOut)) fadeProbeStates |= 8;
        }
    }
}
