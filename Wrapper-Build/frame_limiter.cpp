#include "frame_limiter.h"
#include <timeapi.h>
#include <cstdio>
#include <cmath>

FrameLimiter& FrameLimiter::Get() {
    static FrameLimiter instance;
    return instance;
}

FrameLimiter::FrameLimiter()
    : m_refreshRate(0.0)
    , m_divisor(0.0)
    , m_targetFPS(60.0)
    , m_frameInterval(1.0 / 60.0)
    , m_frameIntervalTicks(0)
    , m_mode(MODE_SMOOTH)
    , m_forceVSync(false)
    , m_targetFrameNum(0)
    , m_firstFrame(true)
    , m_frameCount(0)
    , m_actualFPS(0.0)
    , m_fpsAccum(0.0) {
    InitializeCriticalSection(&m_cs);
    QueryPerformanceFrequency(&m_qpcFreq);
    m_frameIntervalTicks = static_cast<LONGLONG>(m_frameInterval * m_qpcFreq.QuadPart);
    QueryPerformanceCounter(&m_masterClock);
    m_lastPresentTime = m_masterClock;
    m_fpsLastSample = m_masterClock;

    timeBeginPeriod(1);
    LoadConfig();
}

void FrameLimiter::LoadConfig() {
    char iniPath[MAX_PATH];
    GetModuleFileNameA(nullptr, iniPath, MAX_PATH);
    char* lastSlash = strrchr(iniPath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    char fullPath[MAX_PATH];
    const char* configNames[] = { "d3dx_config.ini" };
    const char* foundConfig = nullptr;

    for (int i = 0; i < 1; i++) {
        strcpy_s(fullPath, iniPath);
        strcat_s(fullPath, configNames[i]);
        if (GetFileAttributesA(fullPath) != INVALID_FILE_ATTRIBUTES) {
            foundConfig = configNames[i];
            break;
        }
    }

    if (!foundConfig) {
        strcpy_s(fullPath, iniPath);
        strcat_s(fullPath, "d3dx_config.ini");
    } else {
        strcpy_s(fullPath, iniPath);
        strcat_s(fullPath, foundConfig);
    }

    char buf[64];

    // ---- Mode ----
    int mode = MODE_SMOOTH;
    if (GetPrivateProfileStringA("FrameLimit", "Mode", "1", buf, sizeof(buf), fullPath) > 0)
        mode = atoi(buf);
    if (mode < 0 || mode > 1) mode = MODE_SMOOTH;
    m_mode = static_cast<Mode>(mode);

    // ---- ForceVSync ----
    int forceVSync = 0;
    if (GetPrivateProfileStringA("FrameLimit", "ForceVSync", "0", buf, sizeof(buf), fullPath) > 0)
        forceVSync = atoi(buf);
    m_forceVSync = (forceVSync != 0);

    // ---- RefreshRate ----
    double refresh = 0.0;
    if (GetPrivateProfileStringA("FrameLimit", "RefreshRate", "0", buf, sizeof(buf), fullPath) > 0)
        refresh = atof(buf);
    if (refresh < 0.0) refresh = 0.0;
    m_refreshRate = refresh;

    // ---- Divisor ----
    double divisor = 0.0;
    if (GetPrivateProfileStringA("FrameLimit", "Divisor", "0", buf, sizeof(buf), fullPath) > 0)
        divisor = atof(buf);
    if (divisor < 0.0) divisor = 0.0;
    m_divisor = divisor;

    // ---- TargetFPS ----
    double fps = 60.0;
    if (GetPrivateProfileStringA("FrameLimit", "TargetFPS", "0", buf, sizeof(buf), fullPath) > 0)
        fps = atof(buf);

    if (fps < 0.0) fps = 0.0;

    RecalculateFPS();

    if (m_divisor <= 0.0 && fps > 0.0) {
        SetTargetFPS(fps);
    }
}

void FrameLimiter::SetRefreshRate(double hz) {
    EnterCriticalSection(&m_cs);
    if (hz > 0.0 && m_refreshRate <= 0.0) {
        m_refreshRate = hz;
        RecalculateFPS();
    }
    LeaveCriticalSection(&m_cs);
}

void FrameLimiter::RecalculateFPS() {
    if (m_divisor > 0.0 && m_refreshRate > 0.0) {
        double fps = m_refreshRate / m_divisor;
        SetTargetFPS(fps);
    }
}

void FrameLimiter::SetTargetFPS(double fps) {
    m_targetFPS = fps;
    m_frameInterval = (fps > 0.0) ? (1.0 / fps) : 0.0;
    m_frameIntervalTicks = static_cast<LONGLONG>(m_frameInterval * m_qpcFreq.QuadPart);
    m_firstFrame = true;
    m_targetFrameNum = 0;
}

void FrameLimiter::WaitUntil(LONGLONG targetTicks) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (now.QuadPart >= targetTicks) return;

    double remainingSec = static_cast<double>(targetTicks - now.QuadPart) / m_qpcFreq.QuadPart;
    if (remainingSec > 0.002) {
        DWORD sleepMs = static_cast<DWORD>((remainingSec - 0.001) * 1000.0);
        if (sleepMs > 0) Sleep(sleepMs);
    }

    do {
        QueryPerformanceCounter(&now);
    } while (now.QuadPart < targetTicks);
}

HRESULT FrameLimiter::WaitForFrame() {
    if (m_frameInterval <= 0.0) {
        EnterCriticalSection(&m_cs);
        m_firstFrame = false;
        LeaveCriticalSection(&m_cs);
        return S_OK;
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    if (m_firstFrame) {
        EnterCriticalSection(&m_cs);
        m_masterClock = now;
        m_lastPresentTime = now;
        m_targetFrameNum = 0;
        m_firstFrame = false;
        LeaveCriticalSection(&m_cs);
        return S_OK;
    }

    if (m_mode == MODE_LOW_LATENCY) {
        LONGLONG target = now.QuadPart;
        {
            EnterCriticalSection(&m_cs);
            target = m_lastPresentTime.QuadPart + m_frameIntervalTicks;
            if (now.QuadPart > target + m_frameIntervalTicks * 3) {
                target = now.QuadPart;
            }
            m_lastPresentTime.QuadPart = target;
            LeaveCriticalSection(&m_cs);
        }
        WaitUntil(target);
    } else {
        LONGLONG ideal;
        {
            EnterCriticalSection(&m_cs);
            LONGLONG interval = m_frameIntervalTicks;
            m_targetFrameNum++;
            ideal = m_masterClock.QuadPart + m_targetFrameNum * interval;

            LONGLONG lateBy = now.QuadPart - ideal;
            if (lateBy >= interval * 3) {
                m_masterClock = now;
                m_targetFrameNum = 0;
                ideal = now.QuadPart;
                m_masterClock.QuadPart -= m_masterClock.QuadPart % interval;
                ideal = m_masterClock.QuadPart;
            } else if (lateBy >= interval) {
                ideal = now.QuadPart;
            }
            LeaveCriticalSection(&m_cs);
        }
        WaitUntil(ideal);
    }

    return S_OK;
}

void FrameLimiter::OnFramePresented() {
    EnterCriticalSection(&m_cs);
    m_frameCount++;
    m_fpsAccum += 1.0;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double elapsed = static_cast<double>(now.QuadPart - m_fpsLastSample.QuadPart) / m_qpcFreq.QuadPart;
    if (elapsed >= 1.0) {
        m_actualFPS = m_fpsAccum / elapsed;
        m_fpsAccum = 0.0;
        m_fpsLastSample = now;
    }
    LeaveCriticalSection(&m_cs);
}

double FrameLimiter::GetActualFPS() const {
    return m_actualFPS;
}
