if(NOT _VCPKG_LINUX_TOOLCHAIN)
set(_VCPKG_LINUX_TOOLCHAIN 1)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR "aarch64")
set(CMAKE_CROSSCOMPILING ON CACHE BOOL "")

set(DEVKITPRO $ENV{DEVKITPRO})

set(TOOL_PREFIX ${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(CMAKE_ASM_COMPILER ${TOOL_PREFIX}gcc.exe    CACHE PATH "")
    set(CMAKE_C_COMPILER   ${TOOL_PREFIX}gcc.exe    CACHE PATH "")
    set(CMAKE_CXX_COMPILER ${TOOL_PREFIX}g++.exe    CACHE PATH "")
    set(CMAKE_LINKER       ${TOOL_PREFIX}g++.exe    CACHE PATH "")
    set(CMAKE_AR           ${TOOL_PREFIX}ar.exe     CACHE PATH "")
    set(CMAKE_RANLIB       ${TOOL_PREFIX}ranlib.exe CACHE PATH "")
    set(CMAKE_STRIP        ${TOOL_PREFIX}strip.exe  CACHE PATH "")
else()
    set(CMAKE_ASM_COMPILER ${TOOL_PREFIX}gcc    CACHE PATH "")
    set(CMAKE_C_COMPILER   ${TOOL_PREFIX}gcc    CACHE PATH "")
    set(CMAKE_CXX_COMPILER ${TOOL_PREFIX}g++    CACHE PATH "")
    set(CMAKE_LINKER       ${TOOL_PREFIX}g++    CACHE PATH "")
    set(CMAKE_AR           ${TOOL_PREFIX}ar     CACHE PATH "")
    set(CMAKE_RANLIB       ${TOOL_PREFIX}ranlib CACHE PATH "")
    set(CMAKE_STRIP        ${TOOL_PREFIX}strip  CACHE PATH "")
endif()

set(CMAKE_LIBRARY_ARCHITECTURE aarch64-none-elf CACHE INTERNAL "abi")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "Shared libs not available" )

set(DKA_SWITCH_C_FLAGS "-D__SWITCH__ -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -ftls-model=local-exec -ffunction-sections -fdata-sections")
set(VCPKG_C_FLAGS   "${DKA_SWITCH_C_FLAGS}" CACHE STRING "")
set(VCPKG_CXX_FLAGS "${DKA_SWITCH_C_FLAGS}" CACHE STRING "")
set(VCPKG_ASM_FLAGS "${DKA_SWITCH_C_FLAGS}" CACHE STRING "")

set(CMAKE_EXE_LINKER_FLAGS_INIT "-fPIE -specs=${DEVKITPRO}/libnx/switch.specs")

set(CMAKE_FIND_ROOT_PATH
  ${DEVKITPRO}/devkitA64
  ${DEVKITPRO}/devkitA64/aarch64-none-elf
  ${DEVKITPRO}/tools
  ${DEVKITPRO}/portlibs/switch
  ${DEVKITPRO}/libnx
)

set(CMAKE_PREFIX_PATH
  ${DEVKITPRO}/libnx
)

set(SWITCH TRUE CACHE BOOL "")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(NX_ROOT ${DEVKITPRO}/libnx)

set(NX_STANDARD_LIBRARIES "${NX_ROOT}/lib/libnx.a")
set(CMAKE_C_STANDARD_LIBRARIES "${NX_STANDARD_LIBRARIES}" CACHE STRING "")
set(CMAKE_CXX_STANDARD_LIBRARIES "${NX_STANDARD_LIBRARIES}" CACHE STRING "")
set(CMAKE_ASM_STANDARD_LIBRARIES "${NX_STANDARD_LIBRARIES}" CACHE STRING "")

#for some reason cmake (3.14.3) doesn't appreciate having \" here
set(NX_STANDARD_INCLUDE_DIRECTORIES "${NX_ROOT}/include")
set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES "${NX_STANDARD_INCLUDE_DIRECTORIES}" CACHE STRING "")
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES "${NX_STANDARD_INCLUDE_DIRECTORIES}" CACHE STRING "")
set(CMAKE_ASM_STANDARD_INCLUDE_DIRECTORIES "${NX_STANDARD_INCLUDE_DIRECTORIES}" CACHE STRING "")

link_directories( ${DEVKITPRO}/libnx/lib ${DEVKITPRO}/portlibs/switch/lib )
endif()