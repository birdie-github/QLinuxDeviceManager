cmake_minimum_required(VERSION 3.19)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/project.json" PROJECT_METADATA)
string(JSON APP_NAME GET "${PROJECT_METADATA}" identity name)
string(JSON APP_VERSION GET "${PROJECT_METADATA}" release version)
string(JSON APP_REPOSITORY GET "${PROJECT_METADATA}" project repository)
project(${APP_NAME} VERSION ${APP_VERSION} LANGUAGES CXX)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS project.json)
configure_file(resources/embedded/projectmetadata.h.in generated/projectmetadata.h @ONLY)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "QLinuxDeviceManager supports Linux only")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
# Optional read-only UDisks2 cache access. Native storage viewing still works without it.
option(UDISKS2 "Use optional QtDBus for cached UDisks2 storage metadata" ON)
if(UDISKS2)
    find_package(Qt6 6.8 QUIET COMPONENTS DBus)
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
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
    src/ui/propertiesdialog.h src/ui/propertiesdialog.cpp
    src/devices/systemproperties.h src/devices/systemproperties.cpp
    src/ui/systemdialog.h src/ui/systemdialog.cpp)
target_include_directories(qlinuxdevicemanager PRIVATE
    src/devices src/ui "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(qlinuxdevicemanager PRIVATE Qt6::Widgets PkgConfig::UDEV)
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
target_compile_options(qlinuxdevicemanager PRIVATE -Wall -Wextra -Wpedantic)
include(GNUInstallDirs)
install(TARGETS qlinuxdevicemanager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/io.github.birdie-github.QLinuxDeviceManager.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES project.json DESTINATION ${CMAKE_INSTALL_DATADIR}/QLinuxDeviceManager)
# Optional tools: basic viewing never requires Linguist.
find_package(Qt6 6.8 QUIET COMPONENTS LinguistTools)
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
    target_include_directories(device-search-test PRIVATE src/devices src/ui)
    target_link_libraries(device-search-test PRIVATE Qt6::Widgets PkgConfig::UDEV)
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
        src/devices/cpumetadata.cpp src/devices/cpumetadata.h)
    target_include_directories(system-properties-test PRIVATE src/devices)
    target_link_libraries(system-properties-test PRIVATE Qt6::Core PkgConfig::UDEV)
    add_test(NAME system-properties COMMAND system-properties-test)
endif()

# Apply Qt's 6.8 API checks only to project executables, including fixture targets.
foreach(qldm_target IN ITEMS qlinuxdevicemanager monitor-state-test device-events-test
        device-rules-test device-resources-test device-search-test system-properties-test
        directory-scan-test)
    if(TARGET ${qldm_target})
        target_compile_definitions(${qldm_target} PRIVATE QT_ENABLE_STRICT_MODE_UP_TO=0x060800)
    endif()
endforeach()
