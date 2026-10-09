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
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
option(QLDM_WITH_KMOD "Use optional libkmod for unresolved device function descriptions" ON)
if(QLDM_WITH_KMOD)
    pkg_check_modules(KMOD QUIET IMPORTED_TARGET libkmod>=30)
endif()
add_executable(qlinuxdevicemanager
    src/main.cpp src/devices/device.h src/devices/device.cpp
    src/devices/enumerator.h src/devices/enumerator.cpp
    src/devices/cpumetadata.h src/devices/cpumetadata.cpp
    src/devices/functionnames.h src/devices/functionnames.cpp
    src/devices/devicelabel.h src/devices/devicelabel.cpp
    src/devices/efivariables.h src/devices/efivariables.cpp
    src/devices/modulenames.h src/devices/modulenames.cpp
    src/devices/devicepresentation.h src/devices/devicepresentation.cpp
    src/ui/devicemodel.h src/ui/devicemodel.cpp
    src/ui/mainwindow.h src/ui/mainwindow.cpp)
target_include_directories(qlinuxdevicemanager PRIVATE
    src/devices src/ui "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(qlinuxdevicemanager PRIVATE Qt6::Widgets PkgConfig::UDEV)
if(QLDM_WITH_KMOD AND TARGET PkgConfig::KMOD)
    target_link_libraries(qlinuxdevicemanager PRIVATE PkgConfig::KMOD)
    target_compile_definitions(qlinuxdevicemanager PRIVATE QLDM_HAVE_KMOD)
    message(STATUS "Module description naming: enabled (libkmod)")
else()
    message(STATUS "Module description naming: disabled (libkmod >= 30 unavailable or disabled)")
endif()
target_compile_options(qlinuxdevicemanager PRIVATE -Wall -Wextra -Wpedantic)
include(GNUInstallDirs)
install(TARGETS qlinuxdevicemanager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/io.github.birdie-github.QLinuxDeviceManager.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES project.json DESTINATION ${CMAKE_INSTALL_DATADIR}/QLinuxDeviceManager)
# Optional tools: basic viewing never requires Linguist.
find_package(Qt6 6.2 QUIET COMPONENTS LinguistTools)
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
endif()
