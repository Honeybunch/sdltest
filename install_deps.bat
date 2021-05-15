@echo off

cd ./vcpkg

REM Install Windows Deps
call vcpkg install basisu:x64-windows
call vcpkg install cgltf:x64-windows
call vcpkg install imgui:x64-windows
call vcpkg install mimalloc:x64-windows
call vcpkg install sdl2[vulkan]:x64-windows
call vcpkg install sdl2-image:x64-windows
call vcpkg install volk:x64-windows
call vcpkg install vulkan-memory-allocator:x64-windows

REM Install Windows Static Deps
call vcpkg install basisu:x64-windows-static
call vcpkg install cgltf:x64-windows-static
call vcpkg install imgui:x64-windows-static
call vcpkg install mimalloc:x64-windows-static
call vcpkg install sdl2[vulkan]:x64-windows-static
call vcpkg install sdl2-image:x64-windows-static
call vcpkg install volk:x64-windows-static
call vcpkg install vulkan-memory-allocator:x64-windows-static

REM Return to starting directory
cd %~dp0