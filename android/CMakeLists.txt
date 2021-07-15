find_package(Java 1.8)
find_package(Java COMPONENTS Development)
include(UseJava)

set(android_main ${CMAKE_CURRENT_LIST_DIR}/src/main)

set(ANDROID_VERSION 29)
set(android_path ${CMAKE_ANDROID_NDK}/../../platforms/android-${ANDROID_VERSION}/android.jar)

file(GLOB sdl_java "${android_main}/java/org/libsdl/app/*.java")
add_jar(SDL ${sdl_java} INCLUDE_JARS ${android_path})
install_jar(SDL DESTINATION jar/)

file(GLOB sdltest_java "${android_main}/java/com/honeybunch/sdltest/*.java")
add_jar(SDLTest ${sdltest_java} INCLUDE_JARS ${android_path} SDL)
install_jar(SDLTest DESTINATION jar/)

# HACK - Copy SDL sos from vcpkg to our build directory manually
set(sdl_build_path ${CMAKE_CURRENT_BINARY_DIR}/../$<CONFIG>)
set(cfg_expr $<$<CONFIG:Debug>:$<LOWER_CASE:$<CONFIG>>>)
# hidapi relies on c++ 
set(libcppshared_path $ENV{ANDROID_NDK_HOME}/sources/cxx-stl/llvm-libc++/libs/${arch}/libc++_shared.so)
add_custom_target(copy_so ALL
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/../vcpkg/installed/arm64-android/${cfg_expr}/lib/libhidapi.so ${sdl_build_path}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/../vcpkg/installed/arm64-android/${cfg_expr}/lib/libktx.so ${sdl_build_path}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/../vcpkg/installed/arm64-android/${cfg_expr}/lib/libmimalloc$<$<CONFIG:Debug>:-$<LOWER_CASE:$<CONFIG>>>.so ${sdl_build_path}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/../vcpkg/installed/arm64-android/${cfg_expr}/lib/libSDL2.so ${sdl_build_path}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/../vcpkg/installed/arm64-android/${cfg_expr}/lib/libSDL2_image.so ${sdl_build_path}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/../vcpkg/installed/arm64-android/${cfg_expr}/lib/libOptickCore$<$<CONFIG:Debug>:d>.so ${sdl_build_path}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/../vcpkg/installed/arm64-android/${cfg_expr}/lib/libpng.so ${sdl_build_path}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/../vcpkg/installed/arm64-android/${cfg_expr}/lib/libpng16$<$<CONFIG:Debug>:d>.so ${sdl_build_path}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/../vcpkg/installed/arm64-android/${cfg_expr}/lib/libz.so ${sdl_build_path}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/../vcpkg/installed/arm64-android/${cfg_expr}/lib/libzstd$<$<CONFIG:Debug>:d>.so ${sdl_build_path}
    COMMAND ${CMAKE_COMMAND} -E copy ${libcppshared_path} ${sdl_build_path})
# Bring along the validation layers
if(NOT FINAL)
    file(GLOB vk_validation_layers "$ENV{ANDROID_NDK_HOME}/sources/third_party/vulkan/src/build-android/jniLibs/${arch}/*.so")
    foreach(layer ${vk_validation_layers})
        get_filename_component(layer_name ${layer} NAME_WE)
        list(APPEND layer_names ${layer_name})
        add_custom_target(copy_${layer_name} ALL 
            COMMAND ${CMAKE_COMMAND} -E copy ${layer} ${sdl_build_path})

        install(FILES ${sdl_build_path}/${layer_name}.so
                DESTINATION lib/${arch})
    endforeach()
endif()

install(FILES 
        ${sdl_build_path}/libhidapi.so
        ${sdl_build_path}/libktx.so
        ${sdl_build_path}/libmimalloc$<$<CONFIG:Debug>:-$<LOWER_CASE:$<CONFIG>>>.so 
        ${sdl_build_path}/libSDL2.so 
        ${sdl_build_path}/libSDL2_image.so 
        ${sdl_build_path}/libOptickCore$<$<CONFIG:Debug>:d>.so
        ${sdl_build_path}/libpng.so
        ${sdl_build_path}/libpng16$<$<CONFIG:Debug>:d>.so
        ${sdl_build_path}/libz.so 
        ${sdl_build_path}/libzstd$<$<CONFIG:Debug>:d>.so 
        ${sdl_build_path}/libc++_shared.so
        DESTINATION lib/${arch})

install(FILES ${android_main}/AndroidManifest.xml DESTINATION .)
install(DIRECTORY ${android_main}/res/ DESTINATION res/)

set(CPACK_GENERATOR APK PARENT_SCOPE)
set(CPACK_APK_NDK_PATH ${CMAKE_ANDROID_NDK} PARENT_SCOPE)
set(CPACK_ANDROID_VERSION ${ANDROID_VERSION} PARENT_SCOPE)
set(CPACK_APK_MANIFEST_PATH ./AndroidManifest.xml PARENT_SCOPE)
set(CPACK_APK_KEYSTORE_PATH ./debug.keystore PARENT_SCOPE)
set(CPACK_APK_PACKAGE_LIST ${CPACK_APK_PACKAGE_LIST} PARENT_SCOPE)
set(CPACK_APK_PACKAGE_ARCH_LIST "${arch}" PARENT_SCOPE)