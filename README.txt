--- IMPORTANT ---
- Open d3dx_config.ini to check and set your preferences before use.

--- HOW TO ---
- Pick the right architecture folder, drop ALL .dll files + d3dx_config.ini next to the game's .exe. 
  The correct DLL loads based on the game's API.

--- CHOOSING ARCHITECTURE ---
- x86 = 32-bit DLLs (For DX9 games and mostly older titles)
- x64 = 64-bit DLLs (For modern 64-bit games - Win 10 / Win 11)

--- DEBUG LOG ---
- Set DebugLog=1 in d3dx_config.ini to generate logs in %TEMP% (usually C:\Users\<name>\AppData\Local\Temp)
  d3d9_wrapper.log / d3d10_wrapper.log / dxgi_wrapper.log / d3d12_wrapper.log
  The log file will be named based on which API the game uses. (DX11 will use "dxgi")

ENJOY!
