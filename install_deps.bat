@echo off

cd ./vcpkg

REM For now, get DXC from Vulkan SDK, which is still kind of required
REM Install Windows Tools
REM call vcpkg install tool-dxc:x64-windows

REM Install Windows Deps
call vcpkg install cgltf:x64-windows
call vcpkg install imgui:x64-windows
call vcpkg install ktx[vulkan,tools]:x64-windows
call vcpkg install mimalloc:x64-windows
call vcpkg install sdl2[vulkan]:x64-windows
call vcpkg install sdl2-image:x64-windows
call vcpkg install volk:x64-windows
call vcpkg install vulkan-memory-allocator:x64-windows
call vcpkg install tracy:x64-windows

REM Install Windows Static Deps
call vcpkg install cgltf:x64-windows-static
call vcpkg install imgui:x64-windows-static
call vcpkg install ktx[vulkan,tools]:x64-windows-static
call vcpkg install mimalloc:x64-windows-static
call vcpkg install sdl2[vulkan]:x64-windows-static
call vcpkg install sdl2-image:x64-windows-static
call vcpkg install volk:x64-windows-static
call vcpkg install vulkan-memory-allocator:x64-windows-static
call vcpkg install tracy:x64-windows-static

REM Return to starting directory
cd %~dp0