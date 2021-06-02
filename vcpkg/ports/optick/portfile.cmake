vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Honeybunch/optick
    REF capi
    SHA512 74123d1dec0c33dc8fab201a7417099364098d4095d32c86300d747e232d7409acddfcebcfdbabf8d2f773a7499b6d8f99c4b66ee4a1828e64bff220cefa1d53
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