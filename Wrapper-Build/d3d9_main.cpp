#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>
#include <d3d9.h>
#include <new>
#include <cstdio>
#include <cstdarg>

#include "frame_limiter.h"

static FrameLimiter* g_pFL = nullptr;

static FILE* g_logFile = nullptr;
static CRITICAL_SECTION g_logCS;
static bool g_debugLog = false;

static void LogInit() {
    char iniPath[MAX_PATH];
    GetModuleFileNameA(nullptr, iniPath, MAX_PATH);
    char* lastSlash = strrchr(iniPath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    const char* configNames[] = { "d3dx_config.ini" };
    char fullPath[MAX_PATH];
    for (int i = 0; i < 1; i++) {
        strcpy_s(fullPath, iniPath);
        strcat_s(fullPath, configNames[i]);
        if (GetFileAttributesA(fullPath) != INVALID_FILE_ATTRIBUTES) {
            char buf[16];
            if (GetPrivateProfileStringA("FrameLimit", "DebugLog", "0", buf, sizeof(buf), fullPath) > 0)
                g_debugLog = (atoi(buf) != 0);
            break;
        }
    }

    if (g_debugLog && !g_logFile) {
        wchar_t logPath[MAX_PATH];
        GetModuleFileNameW(nullptr, logPath, MAX_PATH);
        wchar_t* wlastSlash = wcsrchr(logPath, L'\\');
        if (wlastSlash) *(wlastSlash + 1) = L'\0';
        wcscat_s(logPath, L"d3d9_wrapper.log");
        _wfopen_s(&g_logFile, logPath, L"a");
    }
}

static void Log(const char* fmt, ...) {
    if (!g_logFile) return;
    EnterCriticalSection(&g_logCS);
    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    va_end(args);
    fflush(g_logFile);
    LeaveCriticalSection(&g_logCS);
}

static HMODULE g_hRealD3D9 = nullptr;

typedef IDirect3D9* (WINAPI* PFN_Direct3DCreate9_t)(UINT);
typedef HRESULT (WINAPI* PFN_Direct3DCreate9Ex_t)(UINT, IDirect3D9Ex**);

static PFN_Direct3DCreate9_t  pDirect3DCreate9  = nullptr;
static PFN_Direct3DCreate9Ex_t pDirect3DCreate9Ex = nullptr;

typedef int   (WINAPI* PFN_D3DPERF_BeginEvent_t)(D3DCOLOR, LPCWSTR);
typedef int   (WINAPI* PFN_D3DPERF_EndEvent_t)();
typedef void  (WINAPI* PFN_D3DPERF_SetMarker_t)(D3DCOLOR, LPCWSTR);
typedef void  (WINAPI* PFN_D3DPERF_SetRegion_t)(D3DCOLOR, LPCWSTR);
typedef BOOL  (WINAPI* PFN_D3DPERF_QueryRepeatFrame_t)();
typedef void  (WINAPI* PFN_D3DPERF_SetOptions_t)(DWORD);
typedef DWORD (WINAPI* PFN_D3DPERF_GetStatus_t)();

static PFN_D3DPERF_BeginEvent_t pD3DPERF_BeginEvent = nullptr;
static PFN_D3DPERF_EndEvent_t pD3DPERF_EndEvent = nullptr;
static PFN_D3DPERF_SetMarker_t pD3DPERF_SetMarker = nullptr;
static PFN_D3DPERF_SetRegion_t pD3DPERF_SetRegion = nullptr;
static PFN_D3DPERF_QueryRepeatFrame_t pD3DPERF_QueryRepeatFrame = nullptr;
static PFN_D3DPERF_SetOptions_t pD3DPERF_SetOptions = nullptr;
static PFN_D3DPERF_GetStatus_t pD3DPERF_GetStatus = nullptr;

typedef HRESULT (STDMETHODCALLTYPE *PFN_DevicePresent)(
    IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateDevice)(
    IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT (STDMETHODCALLTYPE *PFN_DeviceReset)(
    IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

static PFN_DevicePresent g_OrigPresent = nullptr;
static PFN_CreateDevice  g_OrigCreateDevice = nullptr;
static PFN_DeviceReset   g_OrigReset = nullptr;

static volatile LONG g_d3d9Hooked = 0;
static volatile LONG g_deviceHooked = 0;
static volatile LONG g_resetHooked = 0;

static bool PatchVtableEntry(void** vtable, UINT index, void* newFn, void** outOrig) {
    *outOrig = vtable[index];
    if (*outOrig == newFn) return false;

    DWORD oldProtect;
    if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        return false;
    }
    vtable[index] = newFn;
    DWORD dummy;
    VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &dummy);

    return true;
}

static HRESULT STDMETHODCALLTYPE Hook_Present(
    IDirect3DDevice9* pDevice,
    const RECT* pSourceRect, const RECT* pDestRect,
    HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) {

    g_pFL->WaitForFrame();

    HRESULT hr = g_OrigPresent(pDevice, pSourceRect, pDestRect,
                                hDestWindowOverride, pDirtyRegion);
    g_pFL->OnFramePresented();
    return hr;
}

static HRESULT STDMETHODCALLTYPE Hook_Reset(
    IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {

    if (pPresentationParameters && g_pFL->GetForceVSync()) {
        pPresentationParameters->PresentationInterval = D3DPRESENT_INTERVAL_ONE;
        pPresentationParameters->FullScreen_RefreshRateInHz = 0;
    }

    if (pPresentationParameters && pPresentationParameters->FullScreen_RefreshRateInHz > 0) {
        double refreshHz = (double)pPresentationParameters->FullScreen_RefreshRateInHz;
        g_pFL->SetRefreshRate(refreshHz);
    }

    HRESULT hr = g_OrigReset(pDevice, pPresentationParameters);

    if (SUCCEEDED(hr)) {
        // re-patch Present after reset only if not already hooked
        void** vtable = *reinterpret_cast<void***>(pDevice);
        if (vtable && reinterpret_cast<void*>(vtable[17]) != reinterpret_cast<void*>(Hook_Present)) {
            PatchVtableEntry(vtable, 17, reinterpret_cast<void*>(Hook_Present),
                             reinterpret_cast<void**>(&g_OrigPresent));
        }
    }
    return hr;
}

static void HookDeviceVtable(IDirect3DDevice9* pDevice) {
    if (InterlockedExchange(&g_deviceHooked, 1)) return;

    void** vtable = *reinterpret_cast<void***>(pDevice);
    if (!vtable) {
        InterlockedExchange(&g_deviceHooked, 0);
        return;
    }

    if (!PatchVtableEntry(vtable, 17, reinterpret_cast<void*>(Hook_Present),
                          reinterpret_cast<void**>(&g_OrigPresent))) {
        InterlockedExchange(&g_deviceHooked, 0);
        return;
    }

    PatchVtableEntry(vtable, 16, reinterpret_cast<void*>(Hook_Reset),
                     reinterpret_cast<void**>(&g_OrigReset));
}

static HRESULT STDMETHODCALLTYPE Hook_CreateDevice(
    IDirect3D9* pD3D9, UINT Adapter, D3DDEVTYPE DeviceType,
    HWND hFocusWindow, DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice9** ppReturnedDeviceInterface) {

    if (pPresentationParameters && g_pFL->GetForceVSync()) {
        pPresentationParameters->PresentationInterval = D3DPRESENT_INTERVAL_ONE;
        pPresentationParameters->FullScreen_RefreshRateInHz = 0;
    }

    double refreshHz = 0.0;
    if (pPresentationParameters && pPresentationParameters->FullScreen_RefreshRateInHz > 0) {
        refreshHz = (double)pPresentationParameters->FullScreen_RefreshRateInHz;
    }

    HRESULT hr = g_OrigCreateDevice(pD3D9, Adapter, DeviceType, hFocusWindow,
                                     BehaviorFlags, pPresentationParameters,
                                     ppReturnedDeviceInterface);

    if (SUCCEEDED(hr) && ppReturnedDeviceInterface && *ppReturnedDeviceInterface) {
        if (refreshHz > 0.0) {
            g_pFL->SetRefreshRate(refreshHz);
        }
        HookDeviceVtable(*ppReturnedDeviceInterface);
    }
    return hr;
}

static void HookD3D9Vtable(IDirect3D9* pD3D9) {
    if (InterlockedExchange(&g_d3d9Hooked, 1)) return;

    void** vtable = *reinterpret_cast<void***>(pD3D9);
    if (!vtable) {
        InterlockedExchange(&g_d3d9Hooked, 0);
        return;
    }

    PatchVtableEntry(vtable, 16, reinterpret_cast<void*>(Hook_CreateDevice),
                     reinterpret_cast<void**>(&g_OrigCreateDevice));
}

static void InitD3D9Pointers() {
    if (g_hRealD3D9) return;

    wchar_t sysPath[MAX_PATH];
    GetSystemDirectoryW(sysPath, MAX_PATH);
    wcscat_s(sysPath, L"\\d3d9.dll");

    // The system d3d9.dll has the same module name as our proxy.
    // LoadLibrary would return our own handle if called directly.
    // Copy it to a temp file with a different name to avoid collision.
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    wcscat_s(tempPath, L"d3d9_r.dll");

    if (!CopyFileW(sysPath, tempPath, FALSE)) {
        g_hRealD3D9 = LoadLibraryW(sysPath);
    } else {
        g_hRealD3D9 = LoadLibraryW(tempPath);
        DeleteFileW(tempPath);
    }

    if (!g_hRealD3D9) {
        return;
    }

    pDirect3DCreate9 = (PFN_Direct3DCreate9_t)
        GetProcAddress(g_hRealD3D9, "Direct3DCreate9");
    pDirect3DCreate9Ex = (PFN_Direct3DCreate9Ex_t)
        GetProcAddress(g_hRealD3D9, "Direct3DCreate9Ex");

    pD3DPERF_BeginEvent = (PFN_D3DPERF_BeginEvent_t)
        GetProcAddress(g_hRealD3D9, "D3DPERF_BeginEvent");
    pD3DPERF_EndEvent = (PFN_D3DPERF_EndEvent_t)
        GetProcAddress(g_hRealD3D9, "D3DPERF_EndEvent");
    pD3DPERF_SetMarker = (PFN_D3DPERF_SetMarker_t)
        GetProcAddress(g_hRealD3D9, "D3DPERF_SetMarker");
    pD3DPERF_SetRegion = (PFN_D3DPERF_SetRegion_t)
        GetProcAddress(g_hRealD3D9, "D3DPERF_SetRegion");
    pD3DPERF_QueryRepeatFrame = (PFN_D3DPERF_QueryRepeatFrame_t)
        GetProcAddress(g_hRealD3D9, "D3DPERF_QueryRepeatFrame");
    pD3DPERF_SetOptions = (PFN_D3DPERF_SetOptions_t)
        GetProcAddress(g_hRealD3D9, "D3DPERF_SetOptions");
    pD3DPERF_GetStatus = (PFN_D3DPERF_GetStatus_t)
        GetProcAddress(g_hRealD3D9, "D3DPERF_GetStatus");
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);

        // Write log to %TEMP% (game dir may be protected)
        wchar_t logPath[MAX_PATH];
        GetTempPathW(MAX_PATH, logPath);
        wcscat_s(logPath, L"d3d9_wrapper.log");
        _wfopen_s(&g_logFile, logPath, L"w");
        if (g_logFile) {
            fputs("DllMain: d3d9.dll attached\n", g_logFile);
            fflush(g_logFile);
        }

        OutputDebugStringA("d3d9_wrapper: DllMain attached\n");
        InitializeCriticalSection(&g_logCS);
        LogInit();
        Log("d3d9.dll loaded (DLL_PROCESS_ATTACH)\n");

        g_pFL = &FrameLimiter::Get();
        Log("FrameLimiter cached\n");
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        OutputDebugStringA("d3d9_wrapper: DllMain detached\n");
        if (g_logFile) {
            fputs("DllMain: d3d9.dll detached\n", g_logFile);
            fflush(g_logFile);
            fclose(g_logFile);
            g_logFile = nullptr;
        }
        DeleteCriticalSection(&g_logCS);
    }
    return TRUE;
}

extern "C" {

IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion) {
    InitD3D9Pointers();
    if (!pDirect3DCreate9) return nullptr;
    IDirect3D9* pD3D9 = pDirect3DCreate9(SDKVersion);
    if (pD3D9) {
        HookD3D9Vtable(pD3D9);
    }
    return pD3D9;
}

HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppD3D9Ex) {
    InitD3D9Pointers();
    if (!pDirect3DCreate9Ex) return E_FAIL;
    HRESULT hr = pDirect3DCreate9Ex(SDKVersion, ppD3D9Ex);
    if (SUCCEEDED(hr) && ppD3D9Ex && *ppD3D9Ex) {
        HookD3D9Vtable(static_cast<IDirect3D9*>(*ppD3D9Ex));
    }
    return hr;
}

HRESULT WINAPI Direct3DShaderValidatorCreate9() {
    InitD3D9Pointers();
    auto fn = reinterpret_cast<HRESULT (WINAPI*)()>(
        GetProcAddress(g_hRealD3D9, "Direct3DShaderValidatorCreate9"));
    return fn ? fn() : E_FAIL;
}

int WINAPI D3DPERF_BeginEvent(D3DCOLOR col, LPCWSTR wszName) {
    InitD3D9Pointers();
    return pD3DPERF_BeginEvent ? pD3DPERF_BeginEvent(col, wszName) : -1;
}

int WINAPI D3DPERF_EndEvent() {
    InitD3D9Pointers();
    return pD3DPERF_EndEvent ? pD3DPERF_EndEvent() : -1;
}

void WINAPI D3DPERF_SetMarker(D3DCOLOR col, LPCWSTR wszName) {
    InitD3D9Pointers();
    if (pD3DPERF_SetMarker) pD3DPERF_SetMarker(col, wszName);
}

void WINAPI D3DPERF_SetRegion(D3DCOLOR col, LPCWSTR wszName) {
    InitD3D9Pointers();
    if (pD3DPERF_SetRegion) pD3DPERF_SetRegion(col, wszName);
}

BOOL WINAPI D3DPERF_QueryRepeatFrame() {
    InitD3D9Pointers();
    return pD3DPERF_QueryRepeatFrame ? pD3DPERF_QueryRepeatFrame() : FALSE;
}

void WINAPI D3DPERF_SetOptions(DWORD dwOptions) {
    InitD3D9Pointers();
    if (pD3DPERF_SetOptions) pD3DPERF_SetOptions(dwOptions);
}

DWORD WINAPI D3DPERF_GetStatus() {
    InitD3D9Pointers();
    return pD3DPERF_GetStatus ? pD3DPERF_GetStatus() : 0;
}

} // extern "C"
