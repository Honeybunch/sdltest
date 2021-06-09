vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Honeybunch/optick
    REF vcpkg-5
    SHA512 0c5fa4c9f464c36eaa9e0bd5a8d364b1d7550d0d9a57dbb52f33fe0f91f01f9ec5279ae3ec0255d32d326805c000c38f0f6f0e231fa82180a43f2f97496eea26
    HEAD_REF master
)

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "static" ENABLE_STATIC)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        vulkan OPTICK_USE_VULKAN
        d3d12 OPTICK_USE_D3D12
)

vcpkg_configure_cmake(
    SOURCE_PATH ${SOURCE_PATH}
    PREFER_NINJA
    OPTIONS
        -DOPTICK_CORE_STATIC=${ENABLE_STATIC}
        ${FEATURE_OPTIONS}
)

vcpkg_install_cmake()

vcpkg_fixup_cmake_targets(CONFIG_PATH lib/cmake/Optick)
vcpkg_fixup_pkgconfig()

vcpkg_copy_pdbs()

# Need to manually copy headers
file(GLOB INCLUDES ${CURRENT_PACKAGES_DIR}/include/Optick/*)
file(COPY ${INCLUDES} DESTINATION ${CURRENT_PACKAGES_DIR}/include)

# Handle copyright
file(INSTALL ${SOURCE_PATH}/LICENSE DESTINATION ${CURRENT_PACKAGES_DIR}/share/${PORT} RENAME copyright)

# Cleanup debug headers
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")