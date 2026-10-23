cmake_minimum_required(VERSION 3.19)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/project.json" PROJECT_METADATA)
string(JSON APP_NAME GET "${PROJECT_METADATA}" identity name)
string(JSON APP_VERSION GET "${PROJECT_METADATA}" release version)
string(JSON APP_REPOSITORY GET "${PROJECT_METADATA}" project repository)
string(JSON APP_MINIMUM_QT GET "${PROJECT_METADATA}" platform minimum_qt)
string(JSON APP_CXX_STANDARD GET "${PROJECT_METADATA}" platform cpp_standard)
# Qt encodes API versions as 0xMMmmpp. A major.minor minimum implies patch zero.
string(REPLACE "." ";" APP_QT_VERSION_PARTS "${APP_MINIMUM_QT}")
list(APPEND APP_QT_VERSION_PARTS 0)
list(GET APP_QT_VERSION_PARTS 0 APP_QT_MAJOR)
list(GET APP_QT_VERSION_PARTS 1 APP_QT_MINOR)
list(GET APP_QT_VERSION_PARTS 2 APP_QT_PATCH)
math(EXPR APP_QT_STRICT_VERSION
    "(${APP_QT_MAJOR} << 16) | (${APP_QT_MINOR} << 8) | ${APP_QT_PATCH}"
    OUTPUT_FORMAT HEXADECIMAL)
project(${APP_NAME} VERSION ${APP_VERSION} LANGUAGES CXX)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS project.json)
configure_file(resources/embedded/projectmetadata.h.in generated/projectmetadata.h @ONLY)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "QLinuxDeviceManager supports Linux only")
endif()
set(CMAKE_CXX_STANDARD ${APP_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 ${APP_MINIMUM_QT} REQUIRED COMPONENTS Widgets Network)
# Optional read-only UDisks2 cache access. Native storage viewing still works without it.
option(UDISKS2 "Use optional QtDBus for cached UDisks2 storage metadata" ON)
if(UDISKS2)
    find_package(Qt6 ${APP_MINIMUM_QT} QUIET COMPONENTS DBus)
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(V4L2 REQUIRED IMPORTED_TARGET libv4l2)
# Headers only: session libraries are opened on demand with QLibrary (dlopen).
# Do not link PkgConfig targets for these backends into the executable.
pkg_check_modules(DISPLAY_X11 REQUIRED xcb-randr)
pkg_check_modules(DISPLAY_WAYLAND REQUIRED wayland-client>=1.20)
option(DISPLAY_INFO "Use optional libdisplay-info for monitor EDID decoding" ON)
if(DISPLAY_INFO)
    pkg_check_modules(DISPLAY_INFO QUIET IMPORTED_TARGET libdisplay-info>=0.1.1)
endif()
option(KMOD "Use optional libkmod for function descriptions and installed module properties" ON)
if(KMOD)
    pkg_check_modules(KMOD QUIET IMPORTED_TARGET libkmod>=30)
endif()
add_executable(qlinuxdevicemanager
    src/main.cpp src/devices/device.h src/devices/device.cpp
    src/devices/enumerator.h src/devices/enumerator.cpp
    src/devices/monitorstate.h src/devices/monitorstate.cpp
    src/devices/deviceevents.h src/devices/deviceevents.cpp
    src/devices/deviceproperties.h src/devices/deviceproperties.cpp
    src/devices/networkproperties.h src/devices/networkproperties.cpp
    src/devices/ethernetproperties.h src/devices/ethernetproperties.cpp
    src/devices/displayproperties.h src/devices/displayproperties.cpp
    src/devices/monitorproperties.h src/devices/monitorproperties.cpp
    src/devices/cameraproperties.h src/devices/cameraproperties.cpp
    src/devices/displaybackend.h src/devices/displayx11.cpp src/devices/displaywayland.cpp
    src/devices/displayidentity.h src/devices/displayidentity.cpp
    src/devices/storageproperties.h src/devices/storageproperties.cpp
    src/devices/directoryscan.h
    src/devices/storagepresentation.h src/devices/storagepresentation.cpp
    src/devices/deviceresources.h src/devices/deviceresources.cpp
    src/devices/resourceformat.h src/devices/resourceformat.cpp
    src/devices/cpumetadata.h src/devices/cpumetadata.cpp
    src/devices/functionnames.h src/devices/functionnames.cpp
    src/devices/devicelabel.h src/devices/devicelabel.cpp
    src/devices/efivariables.h src/devices/efivariables.cpp
    src/devices/modulenames.h src/devices/modulenames.cpp
    src/devices/devicepresentation.h src/devices/devicepresentation.cpp
    src/devices/devicesearch.h src/devices/devicesearch.cpp
    src/devices/propertytext.h src/devices/propertytext.cpp
    src/ui/devicefilter.h src/ui/devicefilter.cpp
    src/ui/devicemodel.h src/ui/devicemodel.cpp
    src/ui/mainwindow.h src/ui/mainwindow.cpp
    src/ui/gpulinktest.h src/ui/gpulinktest.cpp
    src/ui/propertiesdialog.h src/ui/propertiesdialog.cpp
    src/devices/secureboot.h src/devices/secureboot.cpp
    src/devices/systemproperties.h src/devices/systemproperties.cpp
    src/ui/systemdialog.h src/ui/systemdialog.cpp)
# Embed existing PNG assets so a build-tree executable has its own icon without
# an installed icon theme or Qt SVG image plugin. Keep every supplied raster size.
set(APP_ICON_SIZES 16 32 48 64 128 256 512)
set(APP_ICON_FILES)
foreach(icon_size IN LISTS APP_ICON_SIZES)
    list(APPEND APP_ICON_FILES
        resources/external/icons/${icon_size}x${icon_size}/QLinuxDeviceManager.png)
endforeach()
qt_add_resources(qlinuxdevicemanager app_icons
    PREFIX "/icons"
    BASE "resources/external/icons"
    FILES ${APP_ICON_FILES})

target_include_directories(qlinuxdevicemanager PRIVATE
    src/devices src/ui "${CMAKE_CURRENT_BINARY_DIR}/generated"
    ${DISPLAY_X11_INCLUDE_DIRS} ${DISPLAY_WAYLAND_INCLUDE_DIRS})
target_link_libraries(qlinuxdevicemanager PRIVATE Qt6::Widgets Qt6::Network PkgConfig::UDEV PkgConfig::V4L2)
if(DISPLAY_INFO AND TARGET PkgConfig::DISPLAY_INFO)
    target_link_libraries(qlinuxdevicemanager PRIVATE PkgConfig::DISPLAY_INFO)
    target_compile_definitions(qlinuxdevicemanager PRIVATE QLDM_HAVE_DISPLAY_INFO)
    message(STATUS "Monitor EDID decoding: enabled (libdisplay-info)")
else()
    message(STATUS "Monitor EDID decoding: disabled; kernel mode names remain available")
endif()
if(KMOD AND TARGET PkgConfig::KMOD)
    target_link_libraries(qlinuxdevicemanager PRIVATE PkgConfig::KMOD)
    target_compile_definitions(qlinuxdevicemanager PRIVATE QLDM_HAVE_KMOD)
    message(STATUS "Module description naming: enabled (libkmod)")
else()
    message(STATUS "Module description naming: disabled (libkmod >= 30 unavailable or disabled)")
endif()
if(UDISKS2 AND TARGET Qt6::DBus)
    target_link_libraries(qlinuxdevicemanager PRIVATE Qt6::DBus)
    target_compile_definitions(qlinuxdevicemanager PRIVATE QLDM_HAVE_DBUS)
    message(STATUS "Cached UDisks2 storage metadata: enabled (QtDBus)")
else()
    message(STATUS "Cached UDisks2 storage metadata: disabled; native fallback available")
endif()
include(cmake/GpuLinkTest.cmake)
target_compile_options(qlinuxdevicemanager PRIVATE -Wall -Wextra -Wpedantic)
include(GNUInstallDirs)
install(TARGETS qlinuxdevicemanager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/io.github.birdie-github.QLinuxDeviceManager.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
# Match the desktop entry's Icon key and Qt desktop file identity.
set(APP_DESKTOP_ID io.github.birdie-github.QLinuxDeviceManager)
foreach(icon_size IN LISTS APP_ICON_SIZES)
    install(FILES resources/external/icons/${icon_size}x${icon_size}/QLinuxDeviceManager.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${icon_size}x${icon_size}/apps
        RENAME ${APP_DESKTOP_ID}.png)
endforeach()
install(FILES resources/external/icons/QLinuxDeviceManager.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME ${APP_DESKTOP_ID}.svg)
install(FILES project.json DESTINATION ${CMAKE_INSTALL_DATADIR}/QLinuxDeviceManager)
# Optional tools: basic viewing never requires Linguist.
find_package(Qt6 ${APP_MINIMUM_QT} QUIET COMPONENTS LinguistTools)
if(TARGET Qt6::lupdate)
    add_custom_target(update-translations
        COMMAND Qt6::lupdate ${CMAKE_CURRENT_SOURCE_DIR}/src
        -ts ${CMAKE_CURRENT_SOURCE_DIR}/resources/external/translations/qlinuxdevicemanager_en.ts)
endif()
if(TARGET Qt6::lrelease)
    qt_add_translation(QM_FILES resources/external/translations/qlinuxdevicemanager_en.ts)
    add_custom_target(translations ALL DEPENDS ${QM_FILES})
    install(FILES ${QM_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/QLinuxDeviceManager/translations)
endif()
include(CTest)
if(BUILD_TESTING)
    add_executable(gpu-link-sample-test tests/gpu-link-sample.cpp src/devices/gpulinksample.h)
    target_include_directories(gpu-link-sample-test PRIVATE src/devices)
    target_link_libraries(gpu-link-sample-test PRIVATE Qt6::Core)
    add_test(NAME gpu-link-sample COMMAND gpu-link-sample-test)
    add_executable(display-identity-test tests/display-identity.cpp
        src/devices/displayidentity.cpp src/devices/displayidentity.h)
    target_include_directories(display-identity-test PRIVATE src/devices)
    target_link_libraries(display-identity-test PRIVATE Qt6::Core)
    add_test(NAME display-identity COMMAND display-identity-test)
    add_executable(directory-scan-test tests/directory-scan.cpp src/devices/directoryscan.h)
    target_include_directories(directory-scan-test PRIVATE src/devices)
    target_link_libraries(directory-scan-test PRIVATE Qt6::Core)
    add_test(NAME directory-scan COMMAND directory-scan-test)
    add_executable(monitor-state-test tests/monitor-state.cpp
        src/devices/monitorstate.cpp src/devices/monitorstate.h)
    target_include_directories(monitor-state-test PRIVATE src/devices)
    target_link_libraries(monitor-state-test PRIVATE Qt6::Core)
    add_test(NAME monitor-state COMMAND monitor-state-test)
    add_executable(device-events-test tests/device-events.cpp
        src/devices/deviceevents.cpp src/devices/deviceevents.h)
    target_include_directories(device-events-test PRIVATE src/devices)
    target_link_libraries(device-events-test PRIVATE Qt6::Core)
    add_test(NAME device-events COMMAND device-events-test)
    add_executable(device-rules-test tests/device-rules.cpp
        src/devices/device.cpp src/devices/device.h
        src/devices/cpumetadata.cpp src/devices/cpumetadata.h
        src/devices/functionnames.cpp src/devices/functionnames.h
        src/devices/devicelabel.cpp src/devices/devicelabel.h
        src/devices/efivariables.cpp src/devices/efivariables.h
        src/devices/devicepresentation.cpp src/devices/devicepresentation.h)
    target_include_directories(device-rules-test PRIVATE src/devices)
    target_link_libraries(device-rules-test PRIVATE Qt6::Widgets)
    add_test(NAME device-rules COMMAND device-rules-test)
    add_executable(device-resources-test tests/device-resources.cpp
        src/devices/deviceresources.cpp src/devices/deviceresources.h)
    target_include_directories(device-resources-test PRIVATE src/devices)
    target_link_libraries(device-resources-test PRIVATE Qt6::Core)
    add_test(NAME device-resources COMMAND device-resources-test)
    add_executable(device-search-test tests/device-search.cpp
        src/devices/devicesearch.cpp src/devices/devicesearch.h
        src/devices/deviceproperties.cpp src/devices/deviceproperties.h
        src/devices/networkproperties.cpp src/devices/networkproperties.h
        src/devices/ethernetproperties.cpp src/devices/ethernetproperties.h
        src/devices/displayproperties.cpp src/devices/displayproperties.h
        src/devices/monitorproperties.cpp src/devices/monitorproperties.h
        src/devices/cameraproperties.cpp src/devices/cameraproperties.h
        src/devices/displaybackend.h src/devices/displayx11.cpp src/devices/displaywayland.cpp
        src/devices/displayidentity.h src/devices/displayidentity.cpp
        src/devices/storageproperties.cpp src/devices/storageproperties.h
        src/devices/storagepresentation.cpp src/devices/storagepresentation.h
        src/devices/deviceresources.cpp src/devices/deviceresources.h
        src/devices/device.cpp src/devices/device.h
        src/devices/devicelabel.cpp src/devices/devicelabel.h
        src/devices/propertytext.cpp src/devices/propertytext.h
        src/devices/resourceformat.cpp src/devices/resourceformat.h
        src/devices/efivariables.cpp src/devices/efivariables.h
        src/devices/functionnames.cpp src/devices/functionnames.h
        src/ui/devicemodel.cpp src/ui/devicemodel.h
        src/ui/devicefilter.cpp src/ui/devicefilter.h)
    target_include_directories(device-search-test PRIVATE src/devices src/ui
        ${DISPLAY_X11_INCLUDE_DIRS} ${DISPLAY_WAYLAND_INCLUDE_DIRS})
    target_link_libraries(device-search-test PRIVATE Qt6::Widgets PkgConfig::UDEV PkgConfig::V4L2)
    if(DISPLAY_INFO AND TARGET PkgConfig::DISPLAY_INFO)
        target_link_libraries(device-search-test PRIVATE PkgConfig::DISPLAY_INFO)
        target_compile_definitions(device-search-test PRIVATE QLDM_HAVE_DISPLAY_INFO)
    endif()
    if(KMOD AND TARGET PkgConfig::KMOD)
        target_link_libraries(device-search-test PRIVATE PkgConfig::KMOD)
        target_compile_definitions(device-search-test PRIVATE QLDM_HAVE_KMOD)
    endif()
    if(UDISKS2 AND TARGET Qt6::DBus)
        target_link_libraries(device-search-test PRIVATE Qt6::DBus)
        target_compile_definitions(device-search-test PRIVATE QLDM_HAVE_DBUS)
    endif()
    add_test(NAME device-search COMMAND device-search-test)
    add_executable(system-properties-test tests/system-properties.cpp
        src/devices/systemproperties.cpp src/devices/systemproperties.h
        src/devices/secureboot.cpp src/devices/secureboot.h
        src/devices/cpumetadata.cpp src/devices/cpumetadata.h)
    target_include_directories(system-properties-test PRIVATE src/devices)
    target_link_libraries(system-properties-test PRIVATE Qt6::Core Qt6::Network PkgConfig::UDEV)
    add_test(NAME system-properties COMMAND system-properties-test)
endif()

# Apply the declared Qt baseline's API checks to project executables and fixture targets.
foreach(qldm_target IN ITEMS qlinuxdevicemanager monitor-state-test device-events-test
        device-rules-test device-resources-test device-search-test system-properties-test
        directory-scan-test display-identity-test gpu-link-sample-test)
    if(TARGET ${qldm_target})
        target_compile_definitions(${qldm_target} PRIVATE QT_ENABLE_STRICT_MODE_UP_TO=${APP_QT_STRICT_VERSION})
    endif()
endforeach()
