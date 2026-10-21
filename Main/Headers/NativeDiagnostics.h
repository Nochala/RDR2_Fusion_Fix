#pragma once

namespace Main::NativeDiagnostics
{
    void Initialize(bool dumpNativeHashes, bool traceCameraNatives);
    void Update();
    bool IsCameraTraceActive();
}
