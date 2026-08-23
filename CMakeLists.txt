cmake_minimum_required(VERSION 3.20)

project(m8c LANGUAGES C VERSION 2.2.4)

set(APP_NAME m8c)

include(FetchContent)

option(USE_LIBSERIALPORT "Use libserialport as a backend" OFF)
option(USE_LIBUSB "Use libusb as a backend" OFF)
option(USE_RTMIDI "Use RtMidi as a backend" OFF)

# Enable USE_LIBSERIALPORT by default if neither USE_LIBUSB nor USE_RTMIDI are defined
if (NOT USE_LIBUSB AND NOT USE_RTMIDI)
    message(STATUS "Neither USE_LIBUSB nor USE_RTMIDI are enabled. Enabling USE_LIBSERIALPORT by default.")
    set(USE_LIBSERIALPORT ON)
endif ()


file(GLOB m8c_SRC "src/*.h" "src/*.c" "src/backends/*.h" "src/backends/*.c" "src/fonts/*.h" "src/fonts/*.c")

set(MACOS_CONTENTS "${CMAKE_CURRENT_SOURCE_DIR}/package/macos/m8c.app/Contents")
set(MACOS_ENTITLEMENTS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/package/macos/Entitlements.plist")

set(APP_ICON ${MACOS_CONTENTS}/Resources/m8c.icns)
set_source_files_properties(${APP_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")

if (APPLE)
    add_executable(${APP_NAME} MACOSX_BUNDLE ${APP_ICON} ${m8c_SRC})
elseif (WIN32)
    add_executable(${APP_NAME} WIN32 ${m8c_SRC})
else ()
    add_executable(${APP_NAME} ${m8c_SRC})
endif ()

find_package(PkgConfig QUIET)

find_package(SDL3 QUIET CONFIG COMPONENTS SDL3)
if (NOT SDL3_FOUND)
    if (WIN32 AND MSVC)
        FetchContent_Declare(
            SDL3_devel
            URL "https://github.com/libsdl-org/SDL/releases/download/release-3.4.14/SDL3-devel-3.4.14-VC.zip"
        )
        FetchContent_MakeAvailable(SDL3_devel)
        find_package(SDL3 REQUIRED CONFIG PATHS "${sdl3_devel_SOURCE_DIR}/SDL3-3.4.14" "${sdl3_devel_SOURCE_DIR}")
    elseif (PKG_CONFIG_FOUND)
        pkg_check_modules(SDL3 REQUIRED sdl3)
        target_link_options(${APP_NAME} PRIVATE ${SDL3_LDFLAGS})
        target_include_directories(${APP_NAME} PRIVATE ${SDL3_INCLUDE_DIRS})
        target_compile_options(${APP_NAME} PRIVATE ${SDL3_CFLAGS_OTHER})
        target_link_libraries(${APP_NAME} PRIVATE ${SDL3_LIBRARIES})
    else ()
        find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3)
    endif ()
endif ()

if (TARGET SDL3::SDL3)
    target_link_libraries(${APP_NAME} PRIVATE SDL3::SDL3)
elseif (TARGET SDL3::SDL3-shared)
    target_link_libraries(${APP_NAME} PRIVATE SDL3::SDL3-shared)
endif ()

if (USE_LIBSERIALPORT)
    set(LIBSERIALPORT_FOUND FALSE)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(LIBSERIALPORT libserialport)
    endif ()

    if (LIBSERIALPORT_FOUND)
        target_link_options(${APP_NAME} PRIVATE ${LIBSERIALPORT_LDFLAGS})
        target_include_directories(${APP_NAME} PRIVATE ${LIBSERIALPORT_INCLUDE_DIRS})
        target_compile_options(${APP_NAME} PRIVATE ${LIBSERIALPORT_CFLAGS_OTHER})
        target_link_libraries(${APP_NAME} PRIVATE ${LIBSERIALPORT_LIBRARIES})
    else ()
        FetchContent_Declare(
            libserialport
            GIT_REPOSITORY "https://github.com/sigrokproject/libserialport.git"
            GIT_TAG "master"
        )
        FetchContent_GetProperties(libserialport)
        if (NOT libserialport_POPULATED)
            FetchContent_Populate(libserialport)
            if (WIN32)
                set(LIBSERIALPORT_SRCS
                    "${libserialport_SOURCE_DIR}/serialport.c"
                    "${libserialport_SOURCE_DIR}/timing.c"
                    "${libserialport_SOURCE_DIR}/windows.c"
                )
                add_library(libserialport SHARED ${LIBSERIALPORT_SRCS})
                target_include_directories(libserialport PUBLIC "${libserialport_SOURCE_DIR}")
                target_compile_definitions(libserialport PRIVATE LIBSERIALPORT_MSBUILD)
                target_link_libraries(libserialport PRIVATE setupapi advapi32)
            endif ()
        endif ()
        target_link_libraries(${APP_NAME} PRIVATE libserialport)
    endif ()
    target_compile_definitions(${APP_NAME} PRIVATE USE_LIBSERIALPORT)
endif ()

if (USE_LIBUSB)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
        target_link_options(${APP_NAME} PRIVATE ${LIBUSB_LDFLAGS})
        target_include_directories(${APP_NAME} PRIVATE ${LIBUSB_INCLUDE_DIRS})
        target_compile_options(${APP_NAME} PRIVATE ${LIBUSB_CFLAGS_OTHER})
        target_link_libraries(${APP_NAME} PRIVATE ${LIBUSB_LIBRARIES})
    endif ()
    target_compile_definitions(${APP_NAME} PRIVATE USE_LIBUSB)
endif ()

if (USE_RTMIDI)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(RTMIDI REQUIRED rtmidi)
        target_link_options(${APP_NAME} PRIVATE ${RTMIDI_LDFLAGS})
        target_include_directories(${APP_NAME} PRIVATE ${RTMIDI_INCLUDE_DIRS})
        target_compile_options(${APP_NAME} PRIVATE ${RTMIDI_CFLAGS_OTHER})
        target_link_libraries(${APP_NAME} PRIVATE ${RTMIDI_LIBRARIES})
    endif ()
    target_compile_definitions(${APP_NAME} PRIVATE USE_RTMIDI)
endif ()

if (WIN32)
    target_link_libraries(${APP_NAME} PRIVATE ws2_32)

    add_custom_command(TARGET ${APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/gamecontrollerdb.txt"
            "$<TARGET_FILE_DIR:${APP_NAME}>"
    )
    if (TARGET SDL3::SDL3-shared)
        add_custom_command(TARGET ${APP_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3-shared>"
                "$<TARGET_FILE_DIR:${APP_NAME}>"
        )
    elseif (TARGET SDL3::SDL3)
        add_custom_command(TARGET ${APP_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3>"
                "$<TARGET_FILE_DIR:${APP_NAME}>"
        )
    endif ()
    if (TARGET libserialport)
        add_custom_command(TARGET ${APP_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:libserialport>"
                "$<TARGET_FILE_DIR:${APP_NAME}>"
        )
    endif ()
endif ()

if (APPLE)
    # Destination paths below are relative to ${CMAKE_INSTALL_PREFIX}
    install(TARGETS ${APP_NAME}
            BUNDLE DESTINATION . COMPONENT Runtime
            RUNTIME DESTINATION bin COMPONENT Runtime
    )

    set(CODESIGN_CERT_NAME "" CACHE STRING "The developer ID cert name for codesign")

    set_target_properties(${APP_NAME} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/package/macos/m8c.app/Contents/Info.plist"
            MACOSX_BUNDLE_BUNDLE_NAME "m8c"
            MACOSX_BUNDLE_COPYRIGHT "Copyright © 2025 laamaa. All rights reserved."
            MACOSX_BUNDLE_GUI_IDENTIFIER "fi.laamaa.m8c"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_ICON_FILE "m8c.icns"
    )

    set(APPS "\${CMAKE_INSTALL_PREFIX}/${APP_NAME}.app")

    if(CODESIGN_CERT_NAME)
        # Use the specified certificate
        install(CODE "include(BundleUtilities)
        fixup_bundle(\"${APPS}\" \"\" \"\")
        execute_process(COMMAND codesign --force --options runtime --deep --entitlements ../package/macos/Entitlements.plist --sign \"${CODESIGN_CERT_NAME}\" --timestamp  \${CMAKE_INSTALL_PREFIX}/${APP_NAME}.app)")
    else()
        # Use ad-hoc signing (self-signed) when no certificate is specified
        install(CODE "include(BundleUtilities)
        fixup_bundle(\"${APPS}\" \"\" \"\")
        execute_process(COMMAND codesign --force --options runtime --deep --entitlements ../package/macos/Entitlements.plist --sign - --timestamp  \${CMAKE_INSTALL_PREFIX}/${APP_NAME}.app)")
    endif()

    set(CPACK_PACKAGE_NAME "m8c")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_DIRECTORY "package-output")
    set(CPACK_GENERATOR "DragNDrop")
    include(CPack)
elseif (UNIX)
    install(TARGETS ${APP_NAME})
    install(DIRECTORY package/share/applications DESTINATION share/)
    install(DIRECTORY package/share/icons DESTINATION share/)
    install(FILES AUDIOGUIDE.md README.md LICENSE DESTINATION share/doc/${APP_NAME})
    install(FILES gamecontrollerdb.txt DESTINATION bin/)
endif ()

# Test suite
enable_testing()
add_executable(m8c_tests
    tests/test_ai_server.c
    src/ai_server.c
    src/ai_logger.c
    src/ai_screen.c
)
target_include_directories(m8c_tests PRIVATE src)
if (TARGET SDL3::SDL3)
    target_link_libraries(m8c_tests PRIVATE SDL3::SDL3)
elseif (TARGET SDL3::SDL3-shared)
    target_link_libraries(m8c_tests PRIVATE SDL3::SDL3-shared)
endif ()
if (WIN32)
    target_link_libraries(m8c_tests PRIVATE ws2_32)
    if (TARGET SDL3::SDL3-shared)
        add_custom_command(TARGET m8c_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3-shared>"
                "$<TARGET_FILE_DIR:m8c_tests>"
        )
    elseif (TARGET SDL3::SDL3)
        add_custom_command(TARGET m8c_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3>"
                "$<TARGET_FILE_DIR:m8c_tests>"
        )
    endif ()
endif ()
add_test(NAME m8c_tests COMMAND m8c_tests)

