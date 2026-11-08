cmake_minimum_required(VERSION 3.21)

project(OneTakeLink VERSION 0.3.0 LANGUAGES C CXX)

# Must match the scrcpy-server bundled for Android (see build.yml).
set(OTL_SCRCPY_VERSION "5.0.1")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network Widgets OpenGL OpenGLWidgets)

# ---------------------------------------------------------------- FFmpeg
# Windows: point FFMPEG_ROOT at a "shared" FFmpeg build that has include/,
# lib/ (*.lib import libraries) and bin/ (*.dll) — e.g. the gyan.dev
# "full_build-shared" package (see .github/workflows/build.yml).
# Linux/macOS: found through pkg-config.
set(FFMPEG_ROOT "" CACHE PATH "FFmpeg install prefix (include/, lib/, bin/)")
set(FFMPEG_LIBS avformat avcodec avutil swscale)

add_library(ffmpeg INTERFACE)
if(FFMPEG_ROOT)
    target_include_directories(ffmpeg INTERFACE "${FFMPEG_ROOT}/include")
    foreach(lib ${FFMPEG_LIBS})
        find_library(FFMPEG_${lib}_LIBRARY NAMES ${lib} PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
        target_link_libraries(ffmpeg INTERFACE "${FFMPEG_${lib}_LIBRARY}")
    endforeach()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG_PC REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
    target_link_libraries(ffmpeg INTERFACE PkgConfig::FFMPEG_PC)
endif()

configure_file(src/core/Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/core/Version.h" @ONLY)

# ------------------------------------------------- LensLink protocol (C)
# Ported from LensLink (GPL-2.0-or-later); no libobs, no Qt.
add_library(lenslink_proto STATIC
    src/camera/lenslink/proto/handshake.c
    src/camera/lenslink/proto/json-reader.c
    src/camera/lenslink/proto/mdns.c
    src/camera/lenslink/proto/usbmux.c
)
target_include_directories(lenslink_proto PUBLIC src/camera/lenslink/proto)
if(WIN32)
    target_compile_definitions(lenslink_proto PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WINSOCK_DEPRECATED_NO_WARNINGS _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(lenslink_proto PUBLIC ws2_32)
endif()

# ------------------------------------------------------ core (no UI)
# Camera/Input, Recording and Library layers: shared by the app and otl-cli.
add_library(otl_core STATIC
    src/core/AnnexB.cpp
    src/core/AppSettings.cpp
    src/core/Log.cpp
    src/core/VideoTypes.cpp
    src/camera/CameraSource.cpp
    src/camera/CameraSource.h
    src/camera/DeviceDiscovery.cpp
    src/camera/DeviceDiscovery.h
    src/camera/android/Adb.cpp
    src/camera/android/AndroidSource.cpp
    src/camera/android/AndroidSource.h
    src/camera/lenslink/LensLinkDiscovery.cpp
    src/camera/lenslink/LensLinkDiscovery.h
    src/camera/lenslink/LensLinkSource.cpp
    src/camera/lenslink/LensLinkSource.h
    src/camera/StreamHandler.cpp
    src/camera/VideoDecoder.cpp
    src/recording/Mp4Writer.cpp
    src/recording/Recorder.cpp
    src/recording/Recorder.h
    src/recording/TakeNamer.cpp
    src/library/MediaProbe.cpp
    src/library/Thumbnailer.cpp
    src/library/VideoPlayer.cpp
    src/library/VideoPlayer.h
)
target_include_directories(otl_core PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(otl_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network lenslink_proto ffmpeg)

# ------------------------------------------------------------ app
set(APP_SOURCES
    src/main.cpp
    src/preview/PreviewWidget.cpp
    src/preview/PreviewWidget.h
    src/ui/LibraryPage.cpp
    src/ui/LibraryPage.h
    src/ui/PlayerPanel.cpp
    src/ui/PlayerPanel.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/ShootPage.cpp
    src/ui/ShootPage.h
    src/ui/Style.cpp
)
if(WIN32)
    list(APPEND APP_SOURCES packaging/windows/onetakelink.rc)
endif()

qt_add_executable(OneTakeLink WIN32 ${APP_SOURCES})
qt_add_resources(OneTakeLink "icons"
    PREFIX "/icons"
    BASE assets/icon
    FILES
        assets/icon/onetakelink-16.png
        assets/icon/onetakelink-32.png
        assets/icon/onetakelink-64.png
        assets/icon/onetakelink-128.png
        assets/icon/onetakelink-256.png
        assets/icon/check.png
        assets/icon/check@2x.png
)
# English UI. Source strings are Russian; regenerate with tools/update_translations.sh.
qt_add_resources(OneTakeLink "i18n"
    PREFIX "/i18n"
    BASE translations
    FILES translations/onetakelink_en.qm
)
target_link_libraries(OneTakeLink PRIVATE
    otl_core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)

# Console diagnostics tool (stage 1: real FPS / drops / long-run checks).
qt_add_executable(otl-cli tools/otl_cli.cpp)
target_link_libraries(otl-cli PRIVATE otl_core)

if(MSVC)
    foreach(t otl_core OneTakeLink otl-cli)
        target_compile_options(${t} PRIVATE /W4 /utf-8 /permissive- /wd4100)
    endforeach()
    target_compile_options(lenslink_proto PRIVATE /utf-8)
else()
    foreach(t otl_core OneTakeLink otl-cli)
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
    endforeach()
endif()

# ------------------------------------------------------------ tests
option(OTL_BUILD_TESTS "Build unit tests" ON)
if(OTL_BUILD_TESTS)
    enable_testing()
    add_executable(otl_tests
        tests/test_main.cpp
        src/core/AnnexB.cpp
        src/recording/TakeNamer.cpp
    )
    target_include_directories(otl_tests PRIVATE src)
    target_link_libraries(otl_tests PRIVATE Qt6::Core lenslink_proto)
    add_test(NAME otl_tests COMMAND otl_tests)
endif()

# ------------------------------------------------------------ install
install(TARGETS OneTakeLink otl-cli RUNTIME DESTINATION .)
install(FILES LICENSE README.md THIRD_PARTY_NOTICES.md DESTINATION .)
