cmake_minimum_required(VERSION 3.19)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake
  CACHE STRING "Vcpkg toolchain file")

get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

project(sdltest)

if(${VCPKG_TARGET_TRIPLET} MATCHES static)
  set(STATIC ON)
endif()

find_package(SDL2 CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_program(DXC dxc CONFIG REQUIRED)

# Compile Shaders
set(shader_includes 
    "${CMAKE_CURRENT_LIST_DIR}/src/common.hlsli"
    "${CMAKE_CURRENT_LIST_DIR}/src/fractal.hlsli"
    "${CMAKE_CURRENT_LIST_DIR}/src/healthbar.hlsli")
set(shader "${CMAKE_CURRENT_LIST_DIR}/src/shader.hlsl")
set(shader_out_path ${CMAKE_CFG_INTDIR}/shaders)
set(vert_out_path "${shader_out_path}/vert.h")
set(frag_out_path "${shader_out_path}/frag.h")
set(out_paths "${vert_out_path};${frag_out_path}")
add_custom_command(
    OUTPUT ${out_paths}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_out_path}
    COMMAND ${DXC} -T vs_6_0 -E vert -Vn vert $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-reflect -spirv ${shader} -Fh ${vert_out_path}
    COMMAND ${DXC} -T ps_6_0 -E frag -Vn frag $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Debug>:-Zi> $<$<CONFIG:Debug>:-Qembed_debug> -fspv-reflect -spirv ${shader} -Fh ${frag_out_path}
    MAIN_DEPENDENCY ${shader}
    DEPENDS ${shader_includes}
)
add_custom_target(shaders ALL DEPENDS ${out_paths})

# Setup Main Executable
set(source "src/main.c")
if(WIN32)
  add_executable(sdltest WIN32 ${source} "src/res/sdltest.rc")
  target_link_options(sdltest PRIVATE -Wl,/subsystem:windows)
else()
endif()

add_dependencies(sdltest shaders)

if(isMultiConfig)
    get_filename_component(CMAKE_CFG_INTDIR_ABS "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>" ABSOLUTE)
else()
    get_filename_component(CMAKE_CFG_INTDIR_ABS "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}" ABSOLUTE)
endif()
target_include_directories(sdltest PRIVATE "src/" "${CMAKE_CFG_INTDIR_ABS}/shaders")
target_link_libraries(sdltest PRIVATE SDL2::SDL2main volk::volk volk::volk_headers)
if(STATIC)
  target_link_libraries(sdltest PRIVATE SDL2::SDL2-static)
else()
  target_link_libraries(sdltest PRIVATE SDL2::SDL2)
endif()

install(TARGETS sdltest
        RUNTIME DESTINATION "$<CONFIG>"
        LIBRARY DESTINATION "$<CONFIG>")

# Install dlls on dynamic builds
if(NOT STATIC)
  install(FILES $<TARGET_FILE_DIR:sdltest>/SDL2$<$<CONFIG:Debug>:d>.dll
        DESTINATION "$<CONFIG>")
endif()