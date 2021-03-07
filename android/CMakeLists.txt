find_package(Java 1.8)
find_package(Java COMPONENTS Development)
include(UseJava)

get_filename_component(JAVA_HOME ${Java_JAVA_EXECUTABLE} DIRECTORY)
file(REAL_PATH "${JAVA_HOME}/../" JAVA_HOME)

set(ANDROID_VERSION 30)

set(dx_path ${CMAKE_ANDROID_NDK}/../../build-tools/29.0.3/dx.bat)
set(aapt_path ${CMAKE_ANDROID_NDK}/../../build-tools/29.0.3/aapt.exe)
set(zipalign_path ${CMAKE_ANDROID_NDK}/../../build-tools/29.0.3/zipalign.exe)
set(apksigner_path ${CMAKE_ANDROID_NDK}/../../build-tools/29.0.3/apksigner.bat)
set(android_path ${CMAKE_ANDROID_NDK}/../../platforms/android-${ANDROID_VERSION}/android.jar)

set(android_main ${CMAKE_CURRENT_LIST_DIR}/src/main)
set(resource_folder ${android_main}/res)
set(manifest_path ${android_main}/AndroidManifest.xml)
set(debug_keystore_path ${android_main}/debug.keystore)

set(keytool_path "${JAVA_HOME}/bin/keytool.exe")

file(GLOB sdl_java "${android_main}/java/org/libsdl/app/*.java")
add_jar(SDL ${sdl_java} INCLUDE_JARS ${android_path})

file(GLOB sdltest_java "${android_main}/java/com/honeybunch/sdltest/*.java")
add_jar(SDLTest ${sdltest_java} INCLUDE_JARS ${android_path} SDL)

# HACK - Copy SDL sos from vcpkg to our build directory manually
set(sdl_build_path ${CMAKE_CURRENT_BINARY_DIR}/../$<CONFIG>)
set(cfg_expr $<$<CONFIG:Debug>:$<LOWER_CASE:$<CONFIG>>>)
# hidapi relies on c++ 
set(libcppshared_path $ENV{ANDROID_NDK_HOME}/sources/cxx-stl/llvm-libc++/libs/${arch}/libc++_shared.so)
add_custom_target(copy_so ALL
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/../vcpkg/installed/arm64-android/${cfg_expr}/lib/libSDL2.so ${sdl_build_path}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/../vcpkg/installed/arm64-android/${cfg_expr}/lib/libhidapi.so ${sdl_build_path}
    COMMAND ${CMAKE_COMMAND} -E copy ${libcppshared_path} ${sdl_build_path})

install(FILES ${sdl_build_path}/libSDL2.so ${sdl_build_path}/libhidapi.so ${sdl_build_path}/libc++_shared.so
        DESTINATION $<CONFIG>/lib/${arch})

# Create signing keys
if(NOT EXISTS ${debug_keystore_path})
add_custom_command(
    OUTPUT ${debug_keystore_path}
    COMMAND ${CMAKE_COMMAND} -E env "JAVA_HOME=${JAVA_HOME}" ${keytool_path} -genkeypair -keystore ${debug_keystore_path} -storepass android -alias androiddebugkey -keypass android -keyalg RSA -validity 10000 -dname CN=,OU=,O=,L=,S=,C=)
endif()

set(packaging_working_dir ${sdltest_install}/$<CONFIG>)
set(unaligned_apk_path ${packaging_working_dir}/sdltest-unaligned.apk)
set(apk_path ${packaging_working_dir}/sdltest.apk)

# Package apk
add_custom_command(
    OUTPUT "sdltest-unaligned.apk"
    WORKING_DIRECTORY ${packaging_working_dir}
    COMMAND ${CMAKE_COMMAND} -E make_directory bin
    COMMAND ${CMAKE_COMMAND} -E env "JAVA_HOME=${JAVA_HOME}" ${dx_path} ARGS --dex --output=bin/classes.dex ${CMAKE_BINARY_DIR}/android/SDL.jar ${CMAKE_BINARY_DIR}/android/SDLTest.jar
    COMMAND ${aapt_path} ARGS package -v -f -S ${resource_folder} -M ${manifest_path} -I ${android_path} -F ${unaligned_apk_path} bin
    COMMAND ${aapt_path} ARGS add -v ${unaligned_apk_path} lib/${arch}/libhidapi.so
    COMMAND ${aapt_path} ARGS add -v ${unaligned_apk_path} lib/${arch}/libSDL2.so
    COMMAND ${aapt_path} ARGS add -v ${unaligned_apk_path} lib/${arch}/libc++_shared.so
    COMMAND ${aapt_path} ARGS add -v ${unaligned_apk_path} lib/${arch}/libsdltest.so)

# Zip align & sign
add_custom_command(
    OUTPUT "sdltest.apk"
    DEPENDS ${debug_keystore_path} "sdltest-unaligned.apk"
    COMMAND ${CMAKE_COMMAND} -E env "JAVA_HOME=${JAVA_HOME}" ${zipalign_path} -f 4 ${unaligned_apk_path} ${apk_path}
    COMMAND ${CMAKE_COMMAND} -E env "JAVA_HOME=${JAVA_HOME}" ${apksigner_path} sign -v --ks ${debug_keystore_path} --ks-pass pass:android --key-pass pass:android --ks-key-alias androiddebugkey ${apk_path}
    COMMAND ${CMAKE_COMMAND} -E env "JAVA_HOME=${JAVA_HOME}" ${apksigner_path} verify -v ${apk_path}
    COMMAND ${CMAKE_COMMAND} -E remove -f ${unaligned_apk_path})

add_custom_target(apk DEPENDS "sdltest.apk")