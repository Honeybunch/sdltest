vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Honeybunch/optick
    REF vcpkg-4
    SHA512 2e31ae506f95bdfc678aa249c9a48690b0dab1c2f738eac00f5f3fb17c2a2617ba2fec1fdf2dea8fa6393cc97cefaa4b2a87d8a412f81131e53a776671af014b
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