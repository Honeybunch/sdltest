vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Honeybunch/optick
    REF vcpkg-2
    SHA512 13cdfcfe2ed65edfb896c84a2064bb1c2b4660b957ab1d954fc2218c3d3a00f18c9f059bc3b28c65a3dcb8a2a67ee538b01a657146b9769125642fe7caac756d
    HEAD_REF master
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        vulkan OPTICK_USE_VULKAN
        d3d12 OPTICK_USE_D3D12
)

vcpkg_configure_cmake(
    SOURCE_PATH ${SOURCE_PATH}
    PREFER_NINJA
    OPTIONS
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