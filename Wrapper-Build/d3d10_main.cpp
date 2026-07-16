#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>
#include <d3d10.h>
#include <d3d10shader.h>
#include <cstdio>
#include <cstdarg>

static HMODULE g_hRealD3D10 = nullptr;

typedef HRESULT (WINAPI* PFN_D3D10CreateDevice_t)(
    IDXGIAdapter*, D3D10_DRIVER_TYPE, HMODULE, UINT, UINT, ID3D10Device**);
typedef HRESULT (WINAPI* PFN_D3D10CreateDeviceAndSwapChain_t)(
    IDXGIAdapter*, D3D10_DRIVER_TYPE, HMODULE, UINT, UINT,
    DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D10Device**);
typedef HRESULT (WINAPI* PFN_D3D10CreateBlob_t)(SIZE_T, ID3D10Blob**);
typedef HRESULT (WINAPI* PFN_D3D10CreateEffectFromMemory_t)(
    void*, SIZE_T, UINT, ID3D10Device*, ID3D10EffectPool*, ID3D10Effect**);
typedef HRESULT (WINAPI* PFN_D3D10CreateEffectPoolFromMemory_t)(
    void*, SIZE_T, UINT, ID3D10Device*, ID3D10EffectPool**);
typedef HRESULT (WINAPI* PFN_D3D10CreateStateBlock_t)(
    ID3D10Device*, D3D10_STATE_BLOCK_MASK*, ID3D10StateBlock**);
typedef HRESULT (WINAPI* PFN_D3D10DisassembleEffect_t)(
    ID3D10Effect*, BOOL, ID3D10Blob**);
typedef HRESULT (WINAPI* PFN_D3D10ReflectShader_t)(
    const void*, SIZE_T, ID3D10ShaderReflection**);
typedef HRESULT (WINAPI* PFN_D3D10PreprocessShader_t)(
    LPCSTR, SIZE_T, LPCSTR, LPD3D10_SHADER_MACRO, LPD3D10INCLUDE,
    ID3D10Blob**, ID3D10Blob**);
typedef LPCSTR (WINAPI* PFN_D3D10GetVertexShaderProfile_t)(ID3D10Device*);
typedef LPCSTR (WINAPI* PFN_D3D10GetGeometryShaderProfile_t)(ID3D10Device*);
typedef LPCSTR (WINAPI* PFN_D3D10GetPixelShaderProfile_t)(ID3D10Device*);
typedef HRESULT (WINAPI* PFN_D3D10CompileShader_t)(
    LPCSTR, SIZE_T, LPCSTR, LPD3D10_SHADER_MACRO, LPD3D10INCLUDE,
    LPCSTR, LPCSTR, UINT, UINT, ID3D10Blob**, ID3D10Blob**);
typedef HRESULT (WINAPI* PFN_D3D10DisassembleShader_t)(
    const void*, SIZE_T, BOOL, LPCSTR, ID3D10Blob**);
typedef HRESULT (WINAPI* PFN_D3D10StateBlockMaskUnion_t)(
    D3D10_STATE_BLOCK_MASK*, D3D10_STATE_BLOCK_MASK*, D3D10_STATE_BLOCK_MASK*);

static PFN_D3D10CreateDevice_t pCreateDevice = nullptr;
static PFN_D3D10CreateDeviceAndSwapChain_t pCreateDeviceAndSwapChain = nullptr;
static PFN_D3D10CreateBlob_t pCreateBlob = nullptr;
static PFN_D3D10CreateEffectFromMemory_t pCreateEffectFromMemory = nullptr;
static PFN_D3D10CreateEffectPoolFromMemory_t pCreateEffectPoolFromMemory = nullptr;
static PFN_D3D10CreateStateBlock_t pCreateStateBlock = nullptr;
static PFN_D3D10DisassembleEffect_t pDisassembleEffect = nullptr;
static PFN_D3D10ReflectShader_t pReflectShader = nullptr;
static PFN_D3D10PreprocessShader_t pPreprocessShader = nullptr;
static PFN_D3D10GetVertexShaderProfile_t pGetVertexShaderProfile = nullptr;
static PFN_D3D10GetGeometryShaderProfile_t pGetGeometryShaderProfile = nullptr;
static PFN_D3D10GetPixelShaderProfile_t pGetPixelShaderProfile = nullptr;
static PFN_D3D10CompileShader_t pCompileShader = nullptr;
static PFN_D3D10DisassembleShader_t pDisassembleShader = nullptr;
static PFN_D3D10StateBlockMaskUnion_t pStateBlockMaskUnion = nullptr;

static void InitD3D10Pointers() {
    if (g_hRealD3D10) return;

    wchar_t sysPath[MAX_PATH];
    GetSystemDirectoryW(sysPath, MAX_PATH);
    wcscat_s(sysPath, L"\\d3d10.dll");

    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    wcscat_s(tempPath, L"d3d10_r.dll");

    if (!CopyFileW(sysPath, tempPath, FALSE)) {
        g_hRealD3D10 = LoadLibraryW(sysPath);
    } else {
        g_hRealD3D10 = LoadLibraryW(tempPath);
        DeleteFileW(tempPath);
    }

    if (!g_hRealD3D10) return;

    pCreateDevice = (PFN_D3D10CreateDevice_t)
        GetProcAddress(g_hRealD3D10, "D3D10CreateDevice");
    pCreateDeviceAndSwapChain = (PFN_D3D10CreateDeviceAndSwapChain_t)
        GetProcAddress(g_hRealD3D10, "D3D10CreateDeviceAndSwapChain");
    pCreateBlob = (PFN_D3D10CreateBlob_t)
        GetProcAddress(g_hRealD3D10, "D3D10CreateBlob");
    pCreateEffectFromMemory = (PFN_D3D10CreateEffectFromMemory_t)
        GetProcAddress(g_hRealD3D10, "D3D10CreateEffectFromMemory");
    pCreateEffectPoolFromMemory = (PFN_D3D10CreateEffectPoolFromMemory_t)
        GetProcAddress(g_hRealD3D10, "D3D10CreateEffectPoolFromMemory");
    pCreateStateBlock = (PFN_D3D10CreateStateBlock_t)
        GetProcAddress(g_hRealD3D10, "D3D10CreateStateBlock");
    pDisassembleEffect = (PFN_D3D10DisassembleEffect_t)
        GetProcAddress(g_hRealD3D10, "D3D10DisassembleEffect");
    pReflectShader = (PFN_D3D10ReflectShader_t)
        GetProcAddress(g_hRealD3D10, "D3D10ReflectShader");
    pPreprocessShader = (PFN_D3D10PreprocessShader_t)
        GetProcAddress(g_hRealD3D10, "D3D10PreprocessShader");
    pGetVertexShaderProfile = (PFN_D3D10GetVertexShaderProfile_t)
        GetProcAddress(g_hRealD3D10, "D3D10GetVertexShaderProfile");
    pGetGeometryShaderProfile = (PFN_D3D10GetGeometryShaderProfile_t)
        GetProcAddress(g_hRealD3D10, "D3D10GetGeometryShaderProfile");
    pGetPixelShaderProfile = (PFN_D3D10GetPixelShaderProfile_t)
        GetProcAddress(g_hRealD3D10, "D3D10GetPixelShaderProfile");
    pCompileShader = (PFN_D3D10CompileShader_t)
        GetProcAddress(g_hRealD3D10, "D3D10CompileShader");
    pDisassembleShader = (PFN_D3D10DisassembleShader_t)
        GetProcAddress(g_hRealD3D10, "D3D10DisassembleShader");
    pStateBlockMaskUnion = (PFN_D3D10StateBlockMaskUnion_t)
        GetProcAddress(g_hRealD3D10, "D3D10StateBlockMaskUnion");
}

static FILE* g_logFile = nullptr;
static bool g_debugLog = false;

static void D3D10Log(const char* fmt, ...) {
    if (!g_debugLog) return;
    if (!g_logFile) {
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
        wcscat_s(logPath, L"d3d10_wrapper.log");
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
        D3D10Log("d3d10.dll loaded\n");
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        D3D10Log("d3d10.dll unloading\n");
        if (g_logFile) fclose(g_logFile);
    }
    return TRUE;
}

extern "C" {

HRESULT WINAPI D3D10CreateDevice(
    IDXGIAdapter* pAdapter, D3D10_DRIVER_TYPE DriverType,
    HMODULE Software, UINT Flags, UINT SDKVersion,
    ID3D10Device** ppDevice) {
    InitD3D10Pointers();
    return pCreateDevice ? pCreateDevice(pAdapter, DriverType, Software, Flags, SDKVersion, ppDevice)
                         : E_FAIL;
}

HRESULT WINAPI D3D10CreateDeviceAndSwapChain(
    IDXGIAdapter* pAdapter, D3D10_DRIVER_TYPE DriverType,
    HMODULE Software, UINT Flags, UINT SDKVersion,
    DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain** ppSwapChain, ID3D10Device** ppDevice) {
    InitD3D10Pointers();
    return pCreateDeviceAndSwapChain ? pCreateDeviceAndSwapChain(
        pAdapter, DriverType, Software, Flags, SDKVersion,
        pSwapChainDesc, ppSwapChain, ppDevice) : E_FAIL;
}

HRESULT WINAPI D3D10CreateBlob(SIZE_T Size, ID3D10Blob** ppBlob) {
    InitD3D10Pointers();
    return pCreateBlob ? pCreateBlob(Size, ppBlob) : E_OUTOFMEMORY;
}

HRESULT WINAPI D3D10CreateEffectFromMemory(
    void* pData, SIZE_T DataLength, UINT FXFlags,
    ID3D10Device* pDevice, ID3D10EffectPool* pEffectPool,
    ID3D10Effect** ppEffect) {
    InitD3D10Pointers();
    return pCreateEffectFromMemory ? pCreateEffectFromMemory(
        pData, DataLength, FXFlags, pDevice, pEffectPool, ppEffect) : E_FAIL;
}

HRESULT WINAPI D3D10CreateEffectPoolFromMemory(
    void* pData, SIZE_T DataLength, UINT FXFlags,
    ID3D10Device* pDevice, ID3D10EffectPool** ppEffectPool) {
    InitD3D10Pointers();
    return pCreateEffectPoolFromMemory ? pCreateEffectPoolFromMemory(
        pData, DataLength, FXFlags, pDevice, ppEffectPool) : E_FAIL;
}

HRESULT WINAPI D3D10CreateStateBlock(
    ID3D10Device* pDevice, D3D10_STATE_BLOCK_MASK* pStateBlockMask,
    ID3D10StateBlock** ppStateBlock) {
    InitD3D10Pointers();
    return pCreateStateBlock ? pCreateStateBlock(pDevice, pStateBlockMask, ppStateBlock) : E_FAIL;
}

HRESULT WINAPI D3D10DisassembleEffect(
    ID3D10Effect* pEffect, BOOL EnableColorCode, ID3D10Blob** ppDisassembly) {
    InitD3D10Pointers();
    return pDisassembleEffect ? pDisassembleEffect(pEffect, EnableColorCode, ppDisassembly) : E_FAIL;
}

HRESULT WINAPI D3D10ReflectShader(
    const void* pShaderBytecode, SIZE_T BytecodeLength,
    ID3D10ShaderReflection** ppReflector) {
    InitD3D10Pointers();
    return pReflectShader ? pReflectShader(pShaderBytecode, BytecodeLength, ppReflector) : E_FAIL;
}

} // extern "C"

HRESULT WINAPI D3D10PreprocessShader(
    LPCSTR pShaderSource, SIZE_T ShaderSourceLen, LPCSTR pShaderName,
    LPD3D10_SHADER_MACRO pDefines, LPD3D10INCLUDE pInclude,
    ID3D10Blob** ppShaderText, ID3D10Blob** ppErrorMsgs) {
    InitD3D10Pointers();
    return pPreprocessShader ? pPreprocessShader(
        pShaderSource, ShaderSourceLen, pShaderName,
        pDefines, pInclude, ppShaderText, ppErrorMsgs) : E_FAIL;
}

LPCSTR WINAPI D3D10GetVertexShaderProfile(ID3D10Device* pDevice) {
    InitD3D10Pointers();
    return pGetVertexShaderProfile ? pGetVertexShaderProfile(pDevice) : "";
}

LPCSTR WINAPI D3D10GetGeometryShaderProfile(ID3D10Device* pDevice) {
    InitD3D10Pointers();
    return pGetGeometryShaderProfile ? pGetGeometryShaderProfile(pDevice) : "";
}

LPCSTR WINAPI D3D10GetPixelShaderProfile(ID3D10Device* pDevice) {
    InitD3D10Pointers();
    return pGetPixelShaderProfile ? pGetPixelShaderProfile(pDevice) : "";
}

HRESULT WINAPI D3D10CompileShader(
    LPCSTR pShaderSource, SIZE_T ShaderSourceLen, LPCSTR pShaderName,
    LPD3D10_SHADER_MACRO pDefines, LPD3D10INCLUDE pInclude,
    LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2,
    ID3D10Blob** ppShader, ID3D10Blob** ppErrorMsgs) {
    InitD3D10Pointers();
    return pCompileShader ? pCompileShader(
        pShaderSource, ShaderSourceLen, pShaderName,
        pDefines, pInclude, pEntrypoint, pTarget,
        Flags1, Flags2, ppShader, ppErrorMsgs) : E_FAIL;
}

HRESULT WINAPI D3D10DisassembleShader(
    const void* pShader, SIZE_T BytecodeLength, BOOL EnableColorCode,
    LPCSTR pComments, ID3D10Blob** ppDisassembly) {
    InitD3D10Pointers();
    return pDisassembleShader ? pDisassembleShader(
        pShader, BytecodeLength, EnableColorCode, pComments, ppDisassembly) : E_FAIL;
}

HRESULT WINAPI D3D10StateBlockMaskUnion(
    D3D10_STATE_BLOCK_MASK* pA, D3D10_STATE_BLOCK_MASK* pB,
    D3D10_STATE_BLOCK_MASK* pResult) {
    InitD3D10Pointers();
    return pStateBlockMaskUnion ? pStateBlockMaskUnion(pA, pB, pResult) : E_FAIL;
}
