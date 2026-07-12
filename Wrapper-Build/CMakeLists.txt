cmake_minimum_required(VERSION 3.20)
project(DX12Wrapper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(COMMON_SOURCES frame_limiter.cpp)

add_library(d3d9 SHARED d3d9_main.cpp d3d9.def ${COMMON_SOURCES})
add_library(d3d10 SHARED d3d10_main.cpp d3d10.def ${COMMON_SOURCES})
add_library(d3d12 SHARED d3d12_main.cpp d3d12.def ${COMMON_SOURCES})
add_library(dxgi SHARED dxgi_main.cpp dxgi.def ${COMMON_SOURCES})

set_target_properties(d3d9 PROPERTIES PREFIX "" SUFFIX ".dll")
set_target_properties(d3d10 PROPERTIES PREFIX "" SUFFIX ".dll")
set_target_properties(d3d12 PROPERTIES PREFIX "" SUFFIX ".dll")
set_target_properties(dxgi PROPERTIES PREFIX "" SUFFIX ".dll")

target_link_libraries(d3d9 PRIVATE winmm)
target_link_libraries(d3d10 PRIVATE dxguid winmm)
target_link_libraries(d3d12 PRIVATE dxguid winmm)
target_link_libraries(dxgi PRIVATE dxguid winmm)

target_compile_options(d3d9 PRIVATE /MT /O2 /W3)
target_compile_options(d3d10 PRIVATE /MT /O2 /W3)
target_compile_options(d3d12 PRIVATE /MT /O2 /W3)
target_compile_options(dxgi PRIVATE /MT /O2 /W3)
