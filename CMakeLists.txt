cmake_minimum_required(VERSION 3.19)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake
  CACHE STRING "Vcpkg toolchain file")

get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

project(sdltest)

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
else()
    message(FATAL_ERROR "Unknown platform")
endif()

# Set install prefix if not already overridden
set(sdltest_install "${CMAKE_CURRENT_SOURCE_DIR}/install/${platform}")
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set (CMAKE_INSTALL_PREFIX ${sdltest_install} CACHE PATH "Default Install Path" FORCE )
else()
    set(sdltest_install ${CMAKE_INSTALL_PREFIX})
endif()

# Clang only for now
if(NOT ${CMAKE_CXX_COMPILER_ID} MATCHES "Clang" AND
   NOT ${CMAKE_CXX_COMPILER_ID} MATCHES "AppleClang")
  message(FATAL_ERROR "${CMAKE_CXX_COMPILER_ID} Unsupported - Clang only")
endif()

if(${VCPKG_TARGET_TRIPLET} MATCHES static)
  set(STATIC ON)
endif()

find_package(SDL2 REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(unofficial-vulkan-memory-allocator CONFIG REQUIRED)
find_program(DXC dxc CONFIG REQUIRED)

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
      COMMAND ${DXC} -T vs_6_0 -E vert -Vn ${filename}_vert $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-reflect -spirv ${shader} -Fh ${vert_out_path}
      COMMAND ${DXC} -T ps_6_0 -E frag -Vn ${filename}_frag $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-reflect -spirv ${shader} -Fh ${frag_out_path}
      MAIN_DEPENDENCY ${shader}
      DEPENDS ${shader_includes}
  )
  list(APPEND shader_headers ${out_paths})
endforeach()
add_custom_target(shaders ALL DEPENDS ${shader_headers})

# Setup Main Executable
set(source "${CMAKE_CURRENT_LIST_DIR}/src/camera.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/fractal.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/main.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/mesh.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/simd.c"
           "${CMAKE_CURRENT_LIST_DIR}/src/vma.cpp")
if(WIN32)
  add_executable(sdltest WIN32 ${source} "${CMAKE_CURRENT_LIST_DIR}/src/res/sdltest.rc")
  target_link_options(sdltest PRIVATE -Wl,/subsystem:windows)
elseif(ANDROID)
  add_library(sdltest SHARED ${source})
endif()

add_dependencies(sdltest shaders)

if(isMultiConfig)
    get_filename_component(CMAKE_CFG_INTDIR_ABS "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>" ABSOLUTE)
else()
    get_filename_component(CMAKE_CFG_INTDIR_ABS "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}" ABSOLUTE)
endif()
#set_property(TARGET sdltest PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
target_include_directories(sdltest PRIVATE "src/" "${CMAKE_CFG_INTDIR_ABS}/shaders")
target_link_libraries(sdltest PRIVATE
  SDL2::SDL2main 
  volk::volk 
  volk::volk_headers 
  unofficial::vulkan-memory-allocator::vulkan-memory-allocator)
if(STATIC)
  target_link_libraries(sdltest PRIVATE SDL2::SDL2-static)
else()
  target_link_libraries(sdltest PRIVATE SDL2::SDL2)
endif()

if(ANDROID)
  install(TARGETS sdltest
          RUNTIME DESTINATION "$<CONFIG>/lib/${arch}"
          LIBRARY DESTINATION "$<CONFIG>/lib/${arch}")
else()
  install(TARGETS sdltest
          RUNTIME DESTINATION "${arch}/bin/$<CONFIG>"
          LIBRARY DESTINATION "${arch}/lib/$<CONFIG>")
endif()

# Install dlls on dynamic builds
if(NOT STATIC AND WIN32)
  install(FILES $<TARGET_FILE_DIR:sdltest>/SDL2$<$<CONFIG:Debug>:d>.dll
        DESTINATION "$<CONFIG>")
endif()

if(ANDROID)
  add_subdirectory(android)
endif()