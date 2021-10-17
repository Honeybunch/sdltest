cmake_minimum_required(VERSION 3.19)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake
  CACHE STRING "Vcpkg toolchain file")
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

project(sdltest
        VERSION 0.1.0
        DESCRIPTION "A personal game engine toybox project"
        LANGUAGES C CXX)

set(GAME_NAME "toolbox")
set(GAME_VERSION_MAJOR ${CMAKE_PROJECT_VERSION_MAJOR})
set(GAME_VERSION_MINOR ${CMAKE_PROJECT_VERSION_MINOR})
set(GAME_VERSION_PATCH ${CMAKE_PROJECT_VERSION_PATCH})

set(ENGINE_NAME "hbengine")
set(ENGINE_VERSION_MAJOR 0)
set(ENGINE_VERSION_MINOR 1)
set(ENGINE_VERSION_PATCH 0)

set(config $<CONFIG>)

# Get target CPU architecture
include(${CMAKE_CURRENT_LIST_DIR}/cmake/TargetArch.cmake)
target_architecture(arch)

# Platform detection
if(WIN32)
  set(platform "windows")
elseif(APPLE)
  if(IOS)
    set(platform "ios")
  else()
    set(platform "macos")
  endif()
elseif(ANDROID)
  set(platform "android")
elseif(UNIX)
  set(platform "linux")
elseif(SWITCH)
  set(platform "switch")
else()
  message(FATAL_ERROR "Unknown platform")
endif()

# Host detection
if(CMAKE_HOST_WIN32)
  set(host "windows")
elseif(CMAKE_HOST_UNIX)
  if(CMAKE_HOST_APPLE)
    set(host "macos")
  else()
    set(host "linux")
  endif()
else()
  message(FATAL_ERROR "Unknown host")
endif()

if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "AMD64")
  set(host_arch "x64")
elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(host_arch "x64")
elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
  set(host_arch "arm64")
else()
  message(FATAL_ERROR "Unknown host arch")
endif()

set(host_triple ${host_arch}-${host})

# Set install prefix if not already overridden
set(sdltest_install "${CMAKE_CURRENT_SOURCE_DIR}/install/${platform}")
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set (CMAKE_INSTALL_PREFIX ${sdltest_install} CACHE PATH "Default Install Path" FORCE )
else()
    set(sdltest_install ${CMAKE_INSTALL_PREFIX})
endif()

# Clang & GNU only for now
if(NOT ${CMAKE_CXX_COMPILER_ID} MATCHES "Clang" AND
   NOT ${CMAKE_CXX_COMPILER_ID} MATCHES "AppleClang" AND
   NOT ${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
  message(FATAL_ERROR "${CMAKE_CXX_COMPILER_ID} Unsupported - Clang & GNU only")
endif()

if(${VCPKG_TARGET_TRIPLET} MATCHES static)
  set(STATIC ON)
endif()

find_package(SDL2 REQUIRED)
find_package(sdl2-image CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(mimalloc 1.6 CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(Tracy CONFIG REQUIRED)
if(UNIX)
  find_package(Threads REQUIRED)
  if(NOT ANDROID)
    add_library(zstd::zstd ALIAS zstd::libzstd_static)
  endif()
endif()

find_path(CGLTF_INCLUDE_DIRS "cgltf.h")

find_program(DXC dxc 
  PATHS ${CMAKE_CURRENT_LIST_DIR}/vcpkg/installed/${host_triple}/tools/dxc
  REQUIRED)
find_program(TOKTX toktx 
  PATHS ${CMAKE_CURRENT_LIST_DIR}/vcpkg/installed/${host_triple}/tools/ktx
  REQUIRED)

# Compile Shaders
set(shader_includes
    "${CMAKE_CURRENT_LIST_DIR}/src/common.hlsli"
    "${CMAKE_CURRENT_LIST_DIR}/src/fullscreenvert.hlsli"
    "${CMAKE_CURRENT_LIST_DIR}/src/healthbar.hlsli")

file(GLOB shaders "${CMAKE_CURRENT_LIST_DIR}/src/*.hlsl")

foreach(shader ${shaders})
  get_filename_component(filename ${shader} NAME_WLE)
  set(shader_out_path ${CMAKE_CFG_INTDIR}/shaders)

  set(vert_out_path "${shader_out_path}/${filename}_vert.h")
  set(frag_out_path "${shader_out_path}/${filename}_frag.h")
  set(out_paths "${vert_out_path};${frag_out_path}")

  add_custom_command(
      OUTPUT ${out_paths}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_out_path}
      COMMAND ${DXC} -T vs_6_0 -E vert -Vn ${filename}_vert $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.1 -spirv ${shader} -Fh ${vert_out_path}
      COMMAND ${DXC} -T ps_6_0 -E frag -Vn ${filename}_frag $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.1 -spirv ${shader} -Fh ${frag_out_path}
      MAIN_DEPENDENCY ${shader}
      DEPENDS ${shader_includes}
  )
  list(APPEND shader_headers ${out_paths})
endforeach()

file(GLOB rt_shaders "${CMAKE_CURRENT_LIST_DIR}/src/*.hlsl.rt")
foreach(shader ${rt_shaders})
  get_filename_component(filename ${shader} NAME_WLE)
  get_filename_component(filename ${filename} NAME_WLE)
  set(shader_out_path ${CMAKE_CFG_INTDIR}/shaders)

  set(raygen_out_path "${shader_out_path}/${filename}_raygen.h")
  set(miss_out_path "${shader_out_path}/${filename}_miss.h")
  set(closehit_out_path "${shader_out_path}/${filename}_closehit.h")
  set(out_paths "${raygen_out_path};${miss_out_path};${closehit_out_path}")

  add_custom_command(
      OUTPUT ${out_paths}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_out_path}
      COMMAND ${DXC} -T lib_6_4 -E raygen -Vn ${filename}_raygen $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.2 -spirv ${shader} -Fh ${raygen_out_path}
      COMMAND ${DXC} -T lib_6_4 -E miss -Vn ${filename}_miss $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.2 -spirv ${shader} -Fh ${miss_out_path}
      COMMAND ${DXC} -T lib_6_4 -E closehit -Vn ${filename}_closehit $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-target-env=vulkan1.2 -spirv ${shader} -Fh ${closehit_out_path}
      MAIN_DEPENDENCY ${shader}
      DEPENDS ${shader_includes}
  )
  list(APPEND shader_headers ${out_paths})
endforeach()

add_custom_target(shaders ALL DEPENDS ${shader_headers})

# Setup Main Executable
set(source "${CMAKE_CURRENT_LIST_DIR}/src/allocator.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/camera.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/cgltf.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/cimgui.cpp"
           "${CMAKE_CURRENT_LIST_DIR}/src/cube.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/demo.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/gpuresources.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/hosek.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/main.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/material.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/pattern.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/pipelines.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/plane.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/profiling.cpp"
           "${CMAKE_CURRENT_LIST_DIR}/src/scene.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/simd.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/skydome.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/vma.cpp"
           "${CMAKE_CURRENT_LIST_DIR}/src/vkdbg.c")
if(WIN32)
  add_executable(sdltest WIN32 ${source} "${CMAKE_CURRENT_LIST_DIR}/src/res/sdltest.rc")
  if(NOT ${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    target_link_options(sdltest PRIVATE -Wl,/subsystem:windows)
  endif()
elseif(ANDROID)
  add_library(sdltest SHARED ${source})
elseif(UNIX)
  if(APPLE)
    if(IOS)
    # iOS
    else()
    # macOS
    endif()
  else()
    add_executable(sdltest ${source} "${CMAKE_CURRENT_LIST_DIR}/linux/src/entry.c")
  endif()
endif()

add_dependencies(sdltest shaders)

if(isMultiConfig)
    get_filename_component(CMAKE_CFG_INTDIR_ABS "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>" ABSOLUTE)
else()
    get_filename_component(CMAKE_CFG_INTDIR_ABS "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}" ABSOLUTE)
endif()
#set_property(TARGET sdltest PROPERTY INTERPROCEDURAL_OPTIMIZATION True)

set(library_list "SDL2::SDL2main;SDL2::SDL2_image;volk::volk;volk::volk_headers;imgui::imgui;mimalloc;mimalloc-static;KTX::ktx;Tracy::TracyClient")

target_link_libraries(sdltest PRIVATE ${library_list})

# Get the latest abbreviated commit hash of the working branch
execute_process(COMMAND git log -1 --format=%h
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE GIT_COMMIT_HASH
                OUTPUT_STRIP_TRAILING_WHITESPACE)

# Generate config header
set(config_include_dir ${CMAKE_CURRENT_BINARY_DIR}/config)
configure_file(src/config.h.in ${config_include_dir}/config.h @ONLY)

target_include_directories(sdltest PRIVATE "src/" "${CMAKE_CFG_INTDIR_ABS}/shaders" "${CGLTF_INCLUDE_DIRS}" "${config_include_dir}")
target_compile_definitions(sdltest PRIVATE "-DHB_CONFIG=\"$<CONFIG>\"")

if(STATIC)
  target_link_libraries(sdltest PRIVATE SDL2::SDL2-static)
  set_property(TARGET sdltest PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  target_link_libraries(sdltest PRIVATE SDL2::SDL2)
endif()

target_compile_features(sdltest PRIVATE c_std_11)

set(assets_dest "assets")
if(ANDROID)
  set(assets_dest "$<CONFIG>/assets")
  install(TARGETS sdltest
          RUNTIME DESTINATION "$<CONFIG>/lib/${arch}"
          LIBRARY DESTINATION "$<CONFIG>/lib/${arch}")
else()
  install(TARGETS sdltest
          RUNTIME DESTINATION "."
          LIBRARY DESTINATION ".")
endif()

# Compress Textures
file(GLOB_RECURSE textures "${CMAKE_CURRENT_LIST_DIR}/assets/textures/*.png")

if(platform STREQUAL "android")
  set(resize "1024x1024")
else()
  set(resize "2048x2048")
endif()

set(UASTC_LEVEL $<$<CONFIG:Debug>:0>$<$<NOT:$<CONFIG:Debug>>:3>)
foreach(texture ${textures})
  file(RELATIVE_PATH relpath ${CMAKE_CURRENT_LIST_DIR}/assets ${texture})
  get_filename_component(relpath ${relpath} DIRECTORY)
  get_filename_component(filename ${texture} NAME_WE)
  set(ktx_texture ${CMAKE_CFG_INTDIR}/assets/textures/${filename}.ktx2)

  add_custom_command(
        OUTPUT ${ktx_texture}
        COMMAND ${CMAKE_COMMAND} -E make_directory assets/${relpath}
        COMMAND ${TOKTX} --t2 --genmipmap --resize ${resize} --uastc ${UASTC_LEVEL} assets/${relpath}/${filename}.ktx2 ${texture}
        MAIN_DEPENDENCY ${texture}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>
    )

  list(APPEND ktx_textures ${ktx_texture})
endforeach()
add_custom_target(textures ALL DEPENDS ${ktx_textures})

# Copy assets to build output dir
add_custom_command(TARGET sdltest POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/assets ${CMAKE_CFG_INTDIR}/assets)

install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/scenes
                   DESTINATION ${assets_dest})

install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/textures
                  DESTINATION ${assets_dest}
                  FILES_MATCHING PATTERN "*.ktx2")

# Install dlls on dynamic builds
if(NOT STATIC AND WIN32)
  install(FILES $<TARGET_FILE_DIR:sdltest>/SDL2$<$<CONFIG:Debug>:d>.dll
        DESTINATION .)
  install(FILES $<TARGET_FILE_DIR:sdltest>/libpng16$<$<CONFIG:Debug>:d>.dll
        DESTINATION .)
  install(FILES $<TARGET_FILE_DIR:sdltest>/mimalloc-redirect.dll
        DESTINATION .)
  install(FILES $<TARGET_FILE_DIR:sdltest>/zlib$<$<CONFIG:Debug>:d>1.dll
        DESTINATION .)
  install(FILES $<TARGET_FILE_DIR:sdltest>/zstd$<$<CONFIG:Debug>:d>.dll
        DESTINATION .)

  foreach(one_trg ${library_list})
    get_target_property(one_trg_type ${one_trg} TYPE)

    if (one_trg_type STREQUAL "SHARED_LIBRARY")
      get_target_property(one_trg_dll_location ${one_trg} IMPORTED_LOCATION_RELEASE)
      if( one_trg_dll_location MATCHES ".dll$")
          install(FILES ${one_trg_dll_location} DESTINATION . CONFIGURATIONS Release)
      endif()
      get_target_property(one_trg_dll_location ${one_trg} IMPORTED_LOCATION_RELWITHDEBINFO)
      if( one_trg_dll_location MATCHES ".dll$")
          install(FILES ${one_trg_dll_location} DESTINATION . CONFIGURATIONS RelWithDebInfo)
      endif()
      get_target_property(one_trg_dll_location ${one_trg} IMPORTED_LOCATION_DEBUG)
      if( one_trg_dll_location MATCHES ".dll$")
          install(FILES ${one_trg_dll_location} DESTINATION . CONFIGURATIONS Debug)
      endif()
    endif()
  endforeach()
endif()

if(ANDROID)
  # CPack has no support for APKs, so we do it manually
  add_subdirectory(android)
elseif(IOS)
  # TODO: custom iOS build steps
else()
  # CPack Packaging
  set(CPACK_PACKAGE_NAME ${CMAKE_PROJECT_NAME})
  set(CPACK_PACKAGE_DESCRIPTION ${CMAKE_PROJECT_DESCRIPTION})
  set(CPACK_PACKAGE_VERSION_MAJOR ${CMAKE_PROJECT_VERSION_MAJOR})
  set(CPACK_PACKAGE_VERSION_MINOR ${CMAKE_PROJECT_VERSION_MINOR})
  set(CPACK_PACKAGE_VERSION_PATCH ${CMAKE_PROJECT_VERSION_PATCH})
  set(CPACK_PACKAGE_VERSION ${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH})
  set(CPACK_PACKAGE_VENDOR "Honeybunch")
  set(CPACK_PACKAGE_CONTACT "amt3824@g.rit.edu")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Honeybunch/sdltest")
  set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${GIT_COMMIT_HASH}-${CMAKE_SYSTEM_NAME}")
  set(CPACK_PACKAGE_ICON ${CMAKE_CURRENT_LIST_DIR}/src/res/sdltest.ico) # TODO: Move to a better folder
  set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_LIST_DIR}/license.txt)
  set(CPACK_RESOURCE_FILE_README ${CMAKE_CURRENT_LIST_DIR}/readme.md)
  set(CPACK_PACKAGE_EXECUTABLES sdltest "SDLTest")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "sdltest")

  # Desktop OSes
  if(WIN32)
    find_package(WIX REQUIRED)

    set(CPACK_GENERATOR WIX)

    set(CPACK_WIX_PRODUCT_GUID "044b496e-773f-447d-90c9-2a0212e1ff2c")
    set(CPACK_WIX_UPGRADE_GUID "d100fdbf-2e03-4bd0-af73-755e632fbc7a")
    set(CPACK_WIX_LICENSE_RTF ${CMAKE_CURRENT_LIST_DIR}/license.rtf)
    set(CPACK_WIX_PRODUCT_ICON ${CMAKE_CURRENT_LIST_DIR}/src/res/sdltest.ico) # TODO: Move to a better folder
  elseif(APPLE)
    set(CPACK_GENERATOR Bundle)

    set(CPACK_BUNDLE_NAME ${CMAKE_PROJECT_NAME})
    set(CPACK_BUNDLE_PLIST ${CMAKE_CURRENT_LIST_DIR}/macos/Info.plist)
    set(CPACK_BUNDLE_ICON ${CMAKE_CURRENT_LIST_DIR}/macos/icon.ico)
    set(CPACK_BUNDLE_APPLE_CERT_APP "")
    set(CPACK_BUNDLE_APPLE_ENTITLEMENTS "")
  elseif(UNIX)
    # Unix systems are expected to provide the generator that suits them best
    # Don't want to try to detect which flavors of linux we're capable of creating
    # packages for.
    # TODO: Set various cpack arguments for DEB and RPM specific generation
  endif()

  include(CPack)
endif()