# D3Dx Wrapper

A set of proxy DLLs that intercept DirectX 9, 10, 11, and 12 games to provide:

- **Smooth frame pacing** — Fixed cadence, jitter-resistant, slightly more latency but much more consistent
- **Low-latency mode** — Faster present, minimal delay, may jitter, less consistent
- **Forced VSync** — Often smoother than in-game or driver VSync
- **Refresh-rate based divisor** — Virtually perfect display sync

Drop the DLLs next to any game EXE — no install, no overlays, no performance cost.
(It is recommended to avoid using it with online / anti-cheat enabled games)

## Supported APIs

| API | Files needed |
|---|---|
| DirectX 9 | `d3d9.dll` + `d3dx_config.ini` |
| DirectX 10 | `dxgi.dll` + `d3d10.dll` + `d3dx_config.ini` |
| DirectX 11 | `dxgi.dll` + `d3dx_config.ini` |
| DirectX 12 | `dxgi.dll` + `d3d12.dll` + `d3dx_config.ini` |

Use `d3dx_config.ini` as the universal config.

## Architecture

- **x86** — 32-bit (For DX9 games and mostly older titles)
- **x64** — 64-bit (For modern 64-bit games - Win 10 / Win 11)

## Configuration

Edit `d3dx_config.ini` before use:

- `Mode` — `0` = Fast/Low Latency, `1` = Smooth/console-like
- `ForceVSync` — `0` = Off, `1` = Forced ON
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
## License

MIT — see [LICENSE](LICENSE)
