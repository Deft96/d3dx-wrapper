#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>
#include <d3d12.h>
#include <cstdio>
#include <cstdarg>

static HMODULE g_hRealD3D12 = nullptr;

typedef HRESULT (WINAPI* PFN_D3D12CreateDevice_t)(
    IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
typedef HRESULT (WINAPI* PFN_D3D12GetDebugInterface_t)(REFIID, void**);
typedef HRESULT (WINAPI* PFN_D3D12GetInterface_t)(REFIID, REFIID, void**);
typedef HRESULT (WINAPI* PFN_D3D12CreateRootSignatureDeserializer_t)(
    LPCVOID, SIZE_T, REFIID, void**);
typedef HRESULT (WINAPI* PFN_D3D12SerializeRootSignature_t)(
    const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION,
    ID3DBlob**, ID3DBlob**);
typedef HRESULT (WINAPI* PFN_D3D12CreateVersionedRootSignatureDeserializer_t)(
    LPCVOID, SIZE_T, REFIID, void**);
typedef HRESULT (WINAPI* PFN_D3D12SerializeVersionedRootSignature_t)(
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*,
    ID3DBlob**, ID3DBlob**);
typedef HRESULT (WINAPI* PFN_D3D12EnableExperimentalFeatures_t)(
    UINT, const IID*, void*, UINT*);

static PFN_D3D12CreateDevice_t pCreateDevice = nullptr;
static PFN_D3D12GetDebugInterface_t pGetDebugInterface = nullptr;
static PFN_D3D12GetInterface_t pGetInterface = nullptr;
static PFN_D3D12CreateRootSignatureDeserializer_t pCreateRootSigDes = nullptr;
static PFN_D3D12SerializeRootSignature_t pSerializeRootSig = nullptr;
static PFN_D3D12CreateVersionedRootSignatureDeserializer_t pCreateVerRootSigDes = nullptr;
static PFN_D3D12SerializeVersionedRootSignature_t pSerializeVerRootSig = nullptr;
static PFN_D3D12EnableExperimentalFeatures_t pEnableExperimental = nullptr;

static void InitD3D12Pointers() {
    if (g_hRealD3D12) return;

    wchar_t sysPath[MAX_PATH];
    GetSystemDirectoryW(sysPath, MAX_PATH);
    wcscat_s(sysPath, L"\\d3d12.dll");

    g_hRealD3D12 = LoadLibraryW(sysPath);
    if (!g_hRealD3D12) return;

    pCreateDevice = (PFN_D3D12CreateDevice_t)
        GetProcAddress(g_hRealD3D12, "D3D12CreateDevice");
    pGetDebugInterface = (PFN_D3D12GetDebugInterface_t)
        GetProcAddress(g_hRealD3D12, "D3D12GetDebugInterface");
    pGetInterface = (PFN_D3D12GetInterface_t)
        GetProcAddress(g_hRealD3D12, "D3D12GetInterface");
    pCreateRootSigDes = (PFN_D3D12CreateRootSignatureDeserializer_t)
        GetProcAddress(g_hRealD3D12, "D3D12CreateRootSignatureDeserializer");
    pSerializeRootSig = (PFN_D3D12SerializeRootSignature_t)
        GetProcAddress(g_hRealD3D12, "D3D12SerializeRootSignature");
    pCreateVerRootSigDes = (PFN_D3D12CreateVersionedRootSignatureDeserializer_t)
        GetProcAddress(g_hRealD3D12, "D3D12CreateVersionedRootSignatureDeserializer");
    pSerializeVerRootSig = (PFN_D3D12SerializeVersionedRootSignature_t)
        GetProcAddress(g_hRealD3D12, "D3D12SerializeVersionedRootSignature");
    pEnableExperimental = (PFN_D3D12EnableExperimentalFeatures_t)
        GetProcAddress(g_hRealD3D12, "D3D12EnableExperimentalFeatures");
}

static FILE* g_logFile = nullptr;
static bool g_debugLog = false;

static void D3D12Log(const char* fmt, ...) {
    if (!g_debugLog) return;
    if (!g_logFile) {
        // check config for debug setting
        wchar_t iniPath[MAX_PATH];
        GetModuleFileNameW(nullptr, iniPath, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(iniPath, L'\\');
        if (lastSlash) *(lastSlash + 1) = L'\0';

        const wchar_t* configNames[] = { L"d3dx_config.ini" };
        wchar_t fullPath[MAX_PATH];
            for (int i = 0; i < 1; i++) {
                wcscpy_s(fullPath, iniPath);
                wcscat_s(fullPath, configNames[i]);
                if (GetFileAttributesW(fullPath) != INVALID_FILE_ATTRIBUTES) {
                    wchar_t buf[16];
                    if (GetPrivateProfileStringW(L"FrameLimit", L"DebugLog", L"0", buf, 16, fullPath) > 0)
                        g_debugLog = (_wtoi(buf) != 0);
                    break;
                }
            }
        if (!g_debugLog) return;

        wchar_t logPath[MAX_PATH];
        GetTempPathW(MAX_PATH, logPath);
        wcscat_s(logPath, L"d3d12_wrapper.log");
        _wfopen_s(&g_logFile, logPath, L"a");
    }
    if (!g_logFile) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    va_end(args);
    fflush(g_logFile);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        D3D12Log("d3d12.dll loaded\n");
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        D3D12Log("d3d12.dll unloading\n");
        if (g_logFile) fclose(g_logFile);
    }
    return TRUE;
}

extern "C" {

HRESULT WINAPI D3D12CreateDevice(
    IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel,
    REFIID riid, void** ppDevice) {
    InitD3D12Pointers();
    return pCreateDevice ? pCreateDevice(pAdapter, MinimumFeatureLevel, riid, ppDevice)
                         : DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
}

HRESULT WINAPI D3D12GetDebugInterface(REFIID riid, void** ppDebug) {
    InitD3D12Pointers();
    return pGetDebugInterface ? pGetDebugInterface(riid, ppDebug) : E_NOINTERFACE;
}

HRESULT WINAPI D3D12GetInterface(REFIID rclsid, REFIID riid, void** ppvDebug) {
    InitD3D12Pointers();
    return pGetInterface ? pGetInterface(rclsid, riid, ppvDebug) : E_NOINTERFACE;
}

HRESULT WINAPI D3D12CreateRootSignatureDeserializer(
    LPCVOID pSrcData, SIZE_T SrcDataSizeInBytes,
    REFIID pRootSignatureDeserializerInterface,
    void** ppRootSignatureDeserializer) {
    InitD3D12Pointers();
    return pCreateRootSigDes ? pCreateRootSigDes(
        pSrcData, SrcDataSizeInBytes,
        pRootSignatureDeserializerInterface, ppRootSignatureDeserializer)
        : E_NOINTERFACE;
}

HRESULT WINAPI D3D12SerializeRootSignature(
    const D3D12_ROOT_SIGNATURE_DESC* pRootSignature,
    D3D_ROOT_SIGNATURE_VERSION Version,
    ID3DBlob** ppBlob, ID3DBlob** ppErrorBlob) {
    InitD3D12Pointers();
    return pSerializeRootSig ? pSerializeRootSig(pRootSignature, Version, ppBlob, ppErrorBlob)
                             : E_OUTOFMEMORY;
}

HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(
    LPCVOID pSrcData, SIZE_T SrcDataSizeInBytes,
    REFIID pRootSignatureDeserializerInterface,
    void** ppRootSignatureDeserializer) {
    InitD3D12Pointers();
    return pCreateVerRootSigDes ? pCreateVerRootSigDes(
        pSrcData, SrcDataSizeInBytes,
        pRootSignatureDeserializerInterface, ppRootSignatureDeserializer)
        : E_NOINTERFACE;
}

HRESULT WINAPI D3D12SerializeVersionedRootSignature(
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pRootSignature,
    ID3DBlob** ppBlob, ID3DBlob** ppErrorBlob) {
    InitD3D12Pointers();
    return pSerializeVerRootSig ? pSerializeVerRootSig(pRootSignature, ppBlob, ppErrorBlob)
                                : E_OUTOFMEMORY;
}

HRESULT WINAPI D3D12EnableExperimentalFeatures(
    UINT NumFeatures, const IID* pIIDs, void* pConfigurationStructs,
    UINT* pConfigurationStructSizes) {
    InitD3D12Pointers();
    return pEnableExperimental ? pEnableExperimental(
        NumFeatures, pIIDs, pConfigurationStructs, pConfigurationStructSizes)
        : E_NOINTERFACE;
}

} // extern "C"
