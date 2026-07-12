// TEST B2: Hook_Present: just QueryPerformanceCounter
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdarg>
#include "frame_limiter.h"
static FILE* g_logFile = nullptr;
static CRITICAL_SECTION g_logCS;
static void Log(const char* fmt, ...) {
    if (!g_logFile) return;
    EnterCriticalSection(&g_logCS);
    va_list args; va_start(args, fmt); vfprintf(g_logFile, fmt, args); va_end(args);
    fflush(g_logFile); LeaveCriticalSection(&g_logCS);
}
static HMODULE g_hRealD3D9 = nullptr;
typedef IDirect3D9* (WINAPI* PFN_D3DCreate9)(UINT);
typedef HRESULT (WINAPI* PFN_D3DCreate9Ex)(UINT, IDirect3D9Ex**);
typedef int (WINAPI* PFN_PE_B)(D3DCOLOR, LPCWSTR);
typedef int (WINAPI* PFN_PE_E)();
typedef void (WINAPI* PFN_PE_SM)(D3DCOLOR, LPCWSTR);
typedef void (WINAPI* PFN_PE_SR)(D3DCOLOR, LPCWSTR);
typedef BOOL (WINAPI* PFN_PE_QR)();
typedef void (WINAPI* PFN_PE_SO)(DWORD);
typedef DWORD (WINAPI* PFN_PE_GS)();
static PFN_D3DCreate9 pD3DC9=nullptr;
static PFN_D3DCreate9Ex pD3DC9Ex=nullptr;
static PFN_PE_B pB=nullptr; static PFN_PE_E pE=nullptr; static PFN_PE_SM pSM=nullptr;
static PFN_PE_SR pSR=nullptr; static PFN_PE_QR pQR=nullptr; static PFN_PE_SO pSO=nullptr; static PFN_PE_GS pGS=nullptr;
typedef HRESULT (STDMETHODCALLTYPE *PFN_Pres)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CDev)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
static PFN_Pres g_OrigPresent=nullptr; static PFN_CDev g_OrigCreateDevice=nullptr;
static volatile LONG g_d3d9H=0, g_devH=0;
static bool PatchVtableEntry(void** vt,UINT i,void* nf,void** orig){
    *orig=vt[i]; if(*orig==nf)return false;
    DWORD op; if(!VirtualProtect(&vt[i],sizeof(void*),PAGE_READWRITE,&op))return false;
    vt[i]=nf; VirtualProtect(&vt[i],sizeof(void*),op,&op); return true;
}
static HRESULT STDMETHODCALLTYPE Hook_Present(IDirect3DDevice9* d,const RECT* sr,const RECT* dr,HWND hw,const RGNDATA* rg){
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    return g_OrigPresent(d,sr,dr,hw,rg);
}
static void HookDeviceVtable(IDirect3DDevice9* d){
    if(InterlockedExchange(&g_devH,1))return;
    void** vt=*reinterpret_cast<void***>(d); if(!vt){InterlockedExchange(&g_devH,0);return;}
    PatchVtableEntry(vt,17,(void*)Hook_Present,(void**)&g_OrigPresent);
}
static HRESULT STDMETHODCALLTYPE Hook_CreateDevice(IDirect3D9* d9,UINT a,D3DDEVTYPE t,HWND hw,DWORD fl,D3DPRESENT_PARAMETERS* pp,IDirect3DDevice9** out){
    HRESULT hr=g_OrigCreateDevice(d9,a,t,hw,fl,pp,out);
    if(SUCCEEDED(hr)&&out&&*out) HookDeviceVtable(*out);
    return hr;
}
static void HookD3D9Vtable(IDirect3D9* d9){
    if(InterlockedExchange(&g_d3d9H,1))return;
    void** vt=*reinterpret_cast<void***>(d9); if(!vt){InterlockedExchange(&g_d3d9H,0);return;}
    PatchVtableEntry(vt,16,(void*)Hook_CreateDevice,(void**)&g_OrigCreateDevice);
}
static void InitD3D9(){
    if(g_hRealD3D9)return;
    wchar_t s[MAX_PATH];GetSystemDirectoryW(s,MAX_PATH);wcscat_s(s,L"\\d3d9.dll");
    wchar_t t[MAX_PATH];GetTempPathW(MAX_PATH,t);wcscat_s(t,L"d3d9_r.dll");
    if(!CopyFileW(s,t,FALSE)){g_hRealD3D9=LoadLibraryW(s);}else{g_hRealD3D9=LoadLibraryW(t);DeleteFileW(t);}
    if(!g_hRealD3D9)return;
    pD3DC9=(PFN_D3DCreate9)GetProcAddress(g_hRealD3D9,"Direct3DCreate9");
    pD3DC9Ex=(PFN_D3DCreate9Ex)GetProcAddress(g_hRealD3D9,"Direct3DCreate9Ex");
    pB=(PFN_PE_B)GetProcAddress(g_hRealD3D9,"D3DPERF_BeginEvent");
    pE=(PFN_PE_E)GetProcAddress(g_hRealD3D9,"D3DPERF_EndEvent");
    pSM=(PFN_PE_SM)GetProcAddress(g_hRealD3D9,"D3DPERF_SetMarker");
    pSR=(PFN_PE_SR)GetProcAddress(g_hRealD3D9,"D3DPERF_SetRegion");
    pQR=(PFN_PE_QR)GetProcAddress(g_hRealD3D9,"D3DPERF_QueryRepeatFrame");
    pSO=(PFN_PE_SO)GetProcAddress(g_hRealD3D9,"D3DPERF_SetOptions");
    pGS=(PFN_PE_GS)GetProcAddress(g_hRealD3D9,"D3DPERF_GetStatus");
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD r,LPVOID){
    if(r==DLL_PROCESS_ATTACH){
        DisableThreadLibraryCalls(h);
        wchar_t lp[MAX_PATH]; GetTempPathW(MAX_PATH,lp); wcscat_s(lp,L"d3d9_wrapper.log");
        _wfopen_s(&g_logFile,lp,L"w"); if(g_logFile){fputs("TestB2\n",g_logFile);fflush(g_logFile);}
        InitializeCriticalSection(&g_logCS);
        FrameLimiter::Get();
    }else if(r==DLL_PROCESS_DETACH){if(g_logFile){fclose(g_logFile);g_logFile=nullptr;}DeleteCriticalSection(&g_logCS);}
    return TRUE;
}
extern "C" {
IDirect3D9* WINAPI Direct3DCreate9(UINT v){InitD3D9();if(!pD3DC9)return 0;IDirect3D9*d=pD3DC9(v);if(d)HookD3D9Vtable(d);return d;}
HRESULT WINAPI Direct3DCreate9Ex(UINT v,IDirect3D9Ex**e){InitD3D9();if(!pD3DC9Ex)return E_FAIL;HRESULT hr=pD3DC9Ex(v,e);if(SUCCEEDED(hr)&&e&&*e)HookD3D9Vtable((IDirect3D9*)*e);return hr;}
HRESULT WINAPI Direct3DShaderValidatorCreate9(){InitD3D9();auto fn=(HRESULT(WINAPI*)())GetProcAddress(g_hRealD3D9,"Direct3DShaderValidatorCreate9");return fn?fn():E_FAIL;}
int WINAPI D3DPERF_BeginEvent(D3DCOLOR c,LPCWSTR w){InitD3D9();return pB?pB(c,w):-1;}
int WINAPI D3DPERF_EndEvent(){InitD3D9();return pE?pE():-1;}
void WINAPI D3DPERF_SetMarker(D3DCOLOR c,LPCWSTR w){InitD3D9();if(pSM)pSM(c,w);}
void WINAPI D3DPERF_SetRegion(D3DCOLOR c,LPCWSTR w){InitD3D9();if(pSR)pSR(c,w);}
BOOL WINAPI D3DPERF_QueryRepeatFrame(){InitD3D9();return pQR?pQR():FALSE;}
void WINAPI D3DPERF_SetOptions(DWORD o){InitD3D9();if(pSO)pSO(o);}
DWORD WINAPI D3DPERF_GetStatus(){InitD3D9();return pGS?pGS():0;}
}
