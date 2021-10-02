@echo off

cd ./vcpkg

REM Install Windows Host Tools
call vcpkg install tool-dxc:x64-windows

REM Install arm64 Android Deps
call vcpkg install cgltf:arm64-android
call vcpkg install imgui:arm64-android
call vcpkg install vulkan:arm64-android
call vcpkg install vulkan-headers:arm64-android
call vcpkg install ktx[vulkan]:arm64-android
call vcpkg install mimalloc:arm64-android
call vcpkg install sdl2[vulkan]:arm64-android
call vcpkg install sdl2-image:arm64-android
call vcpkg install volk:arm64-android
call vcpkg install vulkan-memory-allocator:arm64-android
call vcpkg install tracy:arm64-android

REM Install x64 Android Deps
call vcpkg install cgltf:x64-android
call vcpkg install imgui:x64-android
call vcpkg install vulkan:x64-android
call vcpkg install vulkan-headers:x64-android
call vcpkg install ktx[vulkan]:x64-android
call vcpkg install mimalloc:x64-android
call vcpkg install sdl2[vulkan]:x64-android
call vcpkg install sdl2-image:x64-android
call vcpkg install volk:x64-android
call vcpkg install vulkan-memory-allocator:x64-android
call vcpkg install tracy:x64-android

REM Return to starting directory
cd %~dp0