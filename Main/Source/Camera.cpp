#include "Camera.h"
#include "../../Headers/Logger.h"
#include "main.h"
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace
{
    constexpr std::uint64_t isPlayerControlOn = 0x7964097FCE4C244BULL;
    constexpr std::uint64_t isGameplayCamRendering = 0x8660EA714834E412ULL;
    constexpr std::uint64_t isGameplayCamShaking = 0xEA4C5F4AA0A4DBEFULL;
    constexpr std::uint64_t setGameplayCamShakeAmplitude = 0x570E35F5C4A44838ULL;
    constexpr std::uint64_t isInCinematicMode = 0x74F1D22EFA71FAB8ULL;
    constexpr std::uint64_t isCinematicCamRendering = 0xBF7C780731AADBF8ULL;
    constexpr std::uint64_t isControlPressed = 0xF3A21BCD95725A4AULL;
    constexpr std::uint64_t disableCinematicModeThisFrame = 0x8910C24B7E0046ECULL;
    constexpr std::uint64_t invalidateCinematicVehicleIdleMode = 0x634F4A0562CF19B8ULL;
    constexpr std::uint32_t cinematicCameraControl = 0x620A6C5E;
    constexpr std::uint32_t cinematicCameraHoldControl = 0xD7E7B375;
    constexpr DWORD inactiveThresholdMs = 15000;
    constexpr ULONGLONG vehicleRefreshMs = 1000;
    constexpr ULONGLONG manualCameraAllowanceMs = 3000;

    bool enabled = false;
    bool shakeReductionEnabled = false;
    ULONGLONG nextVehicleRefresh = 0;
    ULONGLONG manualCameraAllowanceUntil = 0;

    bool InvokeBoolean(std::uint64_t hash)
    {
        nativeInit(hash);
        return *nativeCall() != 0;
    }

    bool ControlPressed(std::uint32_t control)
    {
        nativeInit(isControlPressed);
        nativePush64(0);
        nativePush64(control);
        return *nativeCall() != 0;
    }

    void Invoke(std::uint64_t hash)
    {
        nativeInit(hash);
        nativeCall();
    }

    void ReduceShake()
    {
        const float amplitude = 0.35f;
        std::uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(amplitude));
        std::memcpy(&bits, &amplitude, sizeof(bits));
        nativeInit(setGameplayCamShakeAmplitude);
        nativePush64(bits);
        nativeCall();
    }
}

namespace Main::Camera
{
    void Initialize(bool active, bool reduceCameraShake)
    {
        enabled = active;
        shakeReductionEnabled = reduceCameraShake;
        nextVehicleRefresh = 0;
        manualCameraAllowanceUntil = 0;
        if (enabled)
            Logger::Log("[INFO] DisableIdleCamera: idle cinematic camera suppression enabled.");
        if (shakeReductionEnabled)
            Logger::Log("[INFO] ReduceCameraShake: gameplay camera shake amplitude reduction enabled.");
    }

    void Update()
    {
        if (!enabled && !shakeReductionEnabled)
            return;

        nativeInit(isPlayerControlOn);
        nativePush64(0);
        if (*nativeCall() == 0 || !InvokeBoolean(isGameplayCamRendering))
            return;

        if (shakeReductionEnabled && !InvokeBoolean(isInCinematicMode) &&
            !InvokeBoolean(isCinematicCamRendering) && InvokeBoolean(isGameplayCamShaking))
            ReduceShake();

        if (!enabled)
            return;

        const ULONGLONG now = GetTickCount64();
        if (now >= nextVehicleRefresh)
        {
            Invoke(invalidateCinematicVehicleIdleMode);
            nextVehicleRefresh = now + vehicleRefreshMs;
        }

        LASTINPUTINFO input = { sizeof(input) };
        if (!GetLastInputInfo(&input))
            return;

        const DWORD inactiveMs = GetTickCount() - input.dwTime;
        if (inactiveMs < inactiveThresholdMs)
            return;

        if (ControlPressed(cinematicCameraControl) || ControlPressed(cinematicCameraHoldControl))
            manualCameraAllowanceUntil = now + manualCameraAllowanceMs;

        if (now < manualCameraAllowanceUntil || InvokeBoolean(isInCinematicMode) ||
            InvokeBoolean(isCinematicCamRendering))
            return;

        Invoke(disableCinematicModeThisFrame);
    }
}
