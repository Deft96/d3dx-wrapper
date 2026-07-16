#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>
#include <dxgi1_6.h>
#include <new>
#include <cstdio>
#include <cstdarg>

#include "frame_limiter.h"

static FrameLimiter* g_pFL = nullptr;

static FILE* g_logFile = nullptr;
static CRITICAL_SECTION g_logCS;
static bool g_debugLog = false;

static void LogInit() {
    InitializeCriticalSection(&g_logCS);

    // check config for debug log setting
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

    if (!g_debugLog) return;

    wchar_t logPath[MAX_PATH];
    GetTempPathW(MAX_PATH, logPath);
    wcscat_s(logPath, L"dxgi_wrapper.log");
    _wfopen_s(&g_logFile, logPath, L"w");
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

// ---- Real DXGI ----
static HMODULE g_hRealDXGI = nullptr;

typedef HRESULT (WINAPI* PFN_CreateDXGIFactory_t)(REFIID, void**);
typedef HRESULT (WINAPI* PFN_CreateDXGIFactory1_t)(REFIID, void**);
typedef HRESULT (WINAPI* PFN_CreateDXGIFactory2_t)(UINT, REFIID, void**);

static PFN_CreateDXGIFactory_t  pCreateDXGIFactory  = nullptr;
static PFN_CreateDXGIFactory1_t pCreateDXGIFactory1 = nullptr;
static PFN_CreateDXGIFactory2_t pCreateDXGIFactory2 = nullptr;

// ---- COM method hooks ----
typedef HRESULT (STDMETHODCALLTYPE *PFN_Present)(
    IDXGISwapChain*, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *PFN_Present1)(
    IDXGISwapChain*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateSwapChainForHwnd)(
    IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateSwapChain)(
    IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);

static PFN_Present  g_OrigPresent  = nullptr;
static PFN_Present1 g_OrigPresent1 = nullptr;
static PFN_CreateSwapChainForHwnd g_OrigCreateSwapChainForHwnd = nullptr;
static PFN_CreateSwapChain g_OrigCreateSwapChain = nullptr;

static volatile LONG g_factoryHooked = 0;
static volatile LONG g_swapChainHooked = 0;

// ---- Hooked Present ----

static HRESULT STDMETHODCALLTYPE Hook_Present(
    IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    g_pFL->WaitForFrame();

    UINT sync = SyncInterval;
    UINT flg = Flags;
    if (g_pFL->GetForceVSync()) {
        if (sync < 1) sync = 1;
        flg &= ~DXGI_PRESENT_ALLOW_TEARING;
    }

    HRESULT hr = g_OrigPresent(pSwapChain, sync, flg);
    g_pFL->OnFramePresented();
    return hr;
}

static HRESULT STDMETHODCALLTYPE Hook_Present1(
    IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT PresentFlags,
    const DXGI_PRESENT_PARAMETERS* pPresentParameters) {
    g_pFL->WaitForFrame();

    UINT sync = SyncInterval;
    UINT flg = PresentFlags;
    if (g_pFL->GetForceVSync()) {
        if (sync < 1) sync = 1;
        flg &= ~DXGI_PRESENT_ALLOW_TEARING;
    }

    HRESULT hr = g_OrigPresent1(pSwapChain, sync, flg, pPresentParameters);
    g_pFL->OnFramePresented();
    return hr;
}

// ---- Vtable entry patching ----
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

// ---- SwapChain vtable hook ----
// Present = vtable[8], Present1 = vtable[22]

static void HookSwapChainVtable(IDXGISwapChain* pSwapChain) {
    if (InterlockedExchange(&g_swapChainHooked, 1)) return;

    void** vtable = *reinterpret_cast<void***>(pSwapChain);
    if (!vtable) {
        InterlockedExchange(&g_swapChainHooked, 0);
        return;
    }

    if (!PatchVtableEntry(vtable, 8, reinterpret_cast<void*>(Hook_Present),
                          reinterpret_cast<void**>(&g_OrigPresent))) {
        InterlockedExchange(&g_swapChainHooked, 0);
        return;
    }

    void* pTmp = nullptr;
    if (SUCCEEDED(pSwapChain->QueryInterface(IID_IDXGISwapChain1, &pTmp))) {
        reinterpret_cast<IUnknown*>(pTmp)->Release();
        PatchVtableEntry(vtable, 22, reinterpret_cast<void*>(Hook_Present1),
                         reinterpret_cast<void**>(&g_OrigPresent1));
    }
}

// ---- Hooked CreateSwapChainForHwnd ----

static HRESULT STDMETHODCALLTYPE Hook_CreateSwapChainForHwnd(
    IDXGIFactory2* pFactory, IUnknown* pDevice, HWND hWnd,
    const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
    IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain) {

    double refreshHz = 0.0;
    if (pFullscreenDesc && pFullscreenDesc->RefreshRate.Denominator > 0) {
        refreshHz = (double)pFullscreenDesc->RefreshRate.Numerator /
            (double)pFullscreenDesc->RefreshRate.Denominator;
    }

    HRESULT hr = g_OrigCreateSwapChainForHwnd(
        pFactory, pDevice, hWnd, pDesc,
        pFullscreenDesc, pRestrictToOutput, ppSwapChain);

    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
        if (refreshHz > 0.0) {
            g_pFL->SetRefreshRate(refreshHz);
        }

        HookSwapChainVtable(*ppSwapChain);
    }
    return hr;
}

// ---- Hooked CreateSwapChain (old D3D11 path) ----

static HRESULT STDMETHODCALLTYPE Hook_CreateSwapChain(
    IDXGIFactory* pFactory, IUnknown* pDevice,
    DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain) {

    HRESULT hr = g_OrigCreateSwapChain(pFactory, pDevice, pDesc, ppSwapChain);

    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
        HookSwapChainVtable(*ppSwapChain);
    }
    return hr;
}

// ---- Factory vtable hook (in-place) ----
// CreateSwapChain = vtable[10], CreateSwapChainForHwnd = vtable[15]

static void HookFactoryVtable(IDXGIFactory2* pFactory) {
    if (InterlockedExchange(&g_factoryHooked, 1)) return;

    void** vtable = *reinterpret_cast<void***>(pFactory);
    if (!vtable) {
        InterlockedExchange(&g_factoryHooked, 0);
        return;
    }

    PatchVtableEntry(vtable, 10,
                     reinterpret_cast<void*>(Hook_CreateSwapChain),
                     reinterpret_cast<void**>(&g_OrigCreateSwapChain));

    PatchVtableEntry(vtable, 15,
                     reinterpret_cast<void*>(Hook_CreateSwapChainForHwnd),
                     reinterpret_cast<void**>(&g_OrigCreateSwapChainForHwnd));
}

// ---- Init ----

static void InitDXGIPointers() {
    if (g_hRealDXGI) return;

    wchar_t sysPath[MAX_PATH];
    GetSystemDirectoryW(sysPath, MAX_PATH);
    wcscat_s(sysPath, L"\\dxgi.dll");

    Log("Loading real dxgi from: %ls\n", sysPath);
    g_hRealDXGI = LoadLibraryW(sysPath);
    if (!g_hRealDXGI) {
        Log("ERROR: LoadLibrary failed, err=%u\n", GetLastError());
        return;
    }

    pCreateDXGIFactory = (PFN_CreateDXGIFactory_t)
        GetProcAddress(g_hRealDXGI, "CreateDXGIFactory");
    pCreateDXGIFactory1 = (PFN_CreateDXGIFactory1_t)
        GetProcAddress(g_hRealDXGI, "CreateDXGIFactory1");
    pCreateDXGIFactory2 = (PFN_CreateDXGIFactory2_t)
        GetProcAddress(g_hRealDXGI, "CreateDXGIFactory2");
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        LogInit();
        Log("dxgi.dll loaded (DLL_PROCESS_ATTACH)\n");
        g_pFL = &FrameLimiter::Get();
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        Log("dxgi.dll unloading\n");
        if (g_logFile) fclose(g_logFile);
        DeleteCriticalSection(&g_logCS);
    }
    return TRUE;
}

extern "C" {

HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** ppFactory) {
    InitDXGIPointers();
    if (!pCreateDXGIFactory) return E_FAIL;
    return pCreateDXGIFactory(riid, ppFactory);
}

HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    InitDXGIPointers();
    if (!pCreateDXGIFactory1) return E_FAIL;
    HRESULT hr = pCreateDXGIFactory1(riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory) {
        IDXGIFactory2* pF2 = nullptr;
        if (SUCCEEDED(reinterpret_cast<IUnknown*>(*ppFactory)->QueryInterface(
                IID_IDXGIFactory2, reinterpret_cast<void**>(&pF2)))) {
            HookFactoryVtable(pF2);
            pF2->Release();
        }
    }
    return hr;
}

HRESULT WINAPI CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    InitDXGIPointers();
    if (!pCreateDXGIFactory2) return E_FAIL;
    HRESULT hr = pCreateDXGIFactory2(Flags, riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory) {
        IDXGIFactory2* pF2 = nullptr;
        if (SUCCEEDED(reinterpret_cast<IUnknown*>(*ppFactory)->QueryInterface(
                IID_IDXGIFactory2, reinterpret_cast<void**>(&pF2)))) {
            HookFactoryVtable(pF2);
            pF2->Release();
        }
    }
    return hr;
}

HRESULT WINAPI DXGIDeclareAdapterRemovalSupport() {
    InitDXGIPointers();
    auto fn = reinterpret_cast<HRESULT (WINAPI*)()>(
        GetProcAddress(g_hRealDXGI, "DXGIDeclareAdapterRemovalSupport"));
    return fn ? fn() : E_FAIL;
}

HRESULT WINAPI DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** ppDebug) {
    InitDXGIPointers();
    auto fn = reinterpret_cast<HRESULT (WINAPI*)(UINT, REFIID, void**)>(
        GetProcAddress(g_hRealDXGI, "DXGIGetDebugInterface1"));
    return fn ? fn(Flags, riid, ppDebug) : E_NOINTERFACE;
}

} // extern "C"
