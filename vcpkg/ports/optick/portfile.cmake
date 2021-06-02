vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Honeybunch/optick
    REF capi
    SHA512 66470c9a1ce0a37ff58f5dc9eff8f0e71ab8a7398e85363bc17f9db0cdf0c5a8fa5897042987f494ecc0ff4938709a37cfb73c8763fb2ab5ff9dd838df0be470
    HEAD_REF master
)

vcpkg_configure_cmake(
    SOURCE_PATH ${SOURCE_PATH}
    PREFER_NINJA
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