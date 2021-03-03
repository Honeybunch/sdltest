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

set(resource_folder ${CMAKE_CURRENT_LIST_DIR}/src/main/res)
set(res_path ${resource_folder}/res)
set(manifest_path ${resource_folder}/AndroidManifest.xml)
set(debug_keystore_path ${resource_folder}/debug.keystore)

set(keytool_path "${JAVA_HOME}/bin/keytool.exe")

file(GLOB sdl_java "${CMAKE_CURRENT_LIST_DIR}/src/main/java/org/libsdl/app/*.java")
add_jar(SDL ${sdl_java} INCLUDE_JARS ${android_path})

file(GLOB sdltest_java "${CMAKE_CURRENT_LIST_DIR}/src/main/java/com/honeybunch/sdltest/*.java")
add_jar(SDLTest ${sdltest_java} INCLUDE_JARS ${android_path} SDL)

# Create signing keys
if(NOT EXISTS ${debug_keystore_path})
add_custom_command(
    OUTPUT ${debug_keystore_path}
    COMMAND ${CMAKE_COMMAND} -E env "JAVA_HOME=${JAVA_HOME}" ${keytool_path} -genkeypair -keystore ${debug_keystore_path} -storepass android -alias androiddebugkey -keypass android -keyalg RSA -validity 10000 -dname CN=,OU=,O=,L=,S=,C=)
endif()