#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>

class FrameLimiter {
public:
    enum Mode {
        MODE_LOW_LATENCY = 0,
        MODE_SMOOTH      = 1,
    };

    static FrameLimiter& Get();

    void SetTargetFPS(double fps);
    double GetTargetFPS() const { return m_targetFPS; }
    void SetMode(Mode mode) { m_mode = mode; m_firstFrame = true; }
    Mode GetMode() const { return m_mode; }

    void SetRefreshRate(double hz);
    double GetRefreshRate() const { return m_refreshRate; }
    double GetDivisor() const { return m_divisor; }
    bool GetForceVSync() const { return m_forceVSync; }

    HRESULT WaitForFrame();
    void OnFramePresented();

    double GetActualFPS() const;
    unsigned long long GetFrameCount() const { return m_frameCount; }
    unsigned long long GetSkipFrames() const { return m_skipFrames; }

private:
    FrameLimiter();
    ~FrameLimiter() = default;
    FrameLimiter(const FrameLimiter&) = delete;
    FrameLimiter& operator=(const FrameLimiter&) = delete;

    void LoadConfig();
    void RecalculateFPS();
    void WaitUntil(LONGLONG targetTicks);

    double m_refreshRate;
    double m_divisor;

    double m_targetFPS;
    double m_frameInterval;
    LONGLONG m_frameIntervalTicks;
    LARGE_INTEGER m_qpcFreq;

    Mode m_mode;
    bool m_forceVSync;

    LARGE_INTEGER m_lastPresentTime;
    LARGE_INTEGER m_masterClock;
    LONGLONG m_targetFrameNum;

    bool m_firstFrame;
    unsigned long long m_skipFrames;
    unsigned long long m_frameCount;
    double m_actualFPS;
    double m_fpsAccum;
    LARGE_INTEGER m_fpsLastSample;

    CRITICAL_SECTION m_cs;
};
