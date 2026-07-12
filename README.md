# D3Dx Wrapper

A set of proxy DLLs that intercept DirectX 9, 10, 11, and 12 games to provide:

- **Smooth frame pacing** — fixed cadence, jitter-resistant
- **Low-latency mode** — competitive, minimal delay
- **Forced VSync** — often smoother than in-game or driver VSync
- **Refresh-rate based divisor** — perfect display sync

Drop the DLLs next to any game EXE — no install, no overlays, no performance cost.

## Supported APIs

| API | Files needed |
|---|---|
| DirectX 9 | `d3d9.dll` + config |
| DirectX 10 | `dxgi.dll` + `d3d10.dll` + config |
| DirectX 11 | `dxgi.dll` + config |
| DirectX 12 | `dxgi.dll` + `d3d12.dll` + config |

Use `d3dx_config.ini` as the universal config.

## Architecture

- **x86** — 32-bit (Most DX9 and older DX10/11 games)
- **x64** — 64-bit (Modern DX10/11/12 games)

## Configuration

Edit `d3dx_config.ini` before use:

- `Mode` — `0` = Fast/Low Latency, `1` = Smooth/console-like
- `ForceVSync` — `0` = Off, `1` = Forced on
- `TargetFPS` — Direct FPS cap (e.g. `60`, `141.0`)
- `Divisor` — Auto-calculate FPS as RefreshRate / Divisor
- `DebugLog` — `1` to write logs to `%TEMP%`

## Building

Requires Visual Studio 2022 with C++ desktop workload.

```
cmake -S Wrapper-Build -B Wrapper-Build/build_x86 -A Win32
cmake --build Wrapper-Build/build_x86 --config Release
cmake -S Wrapper-Build -B Wrapper-Build/build_x64 -A x64
cmake --build Wrapper-Build/build_x64 --config Release
```

Or run `build_d3d9.bat` as administrator.

## License

MIT — see [LICENSE](LICENSE)
