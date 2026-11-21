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
option(GUI "Build the Qt Widgets desktop frontend" ON)
option(TUI "Build the optional FINAL CUT terminal frontend" OFF)
if(NOT GUI AND NOT TUI)
    message(FATAL_ERROR "Enable at least one frontend with -DGUI=ON or -DTUI=ON")
endif()

find_package(Qt6 ${APP_MINIMUM_QT} REQUIRED COMPONENTS Core Network)
include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
option(KMOD "Use optional libkmod for function descriptions and installed module properties" ON)
if(KMOD)
    pkg_check_modules(KMOD QUIET IMPORTED_TARGET libkmod>=30)
endif()
# Shared synchronous discovery and naming; neither frontend toolkit belongs here.
qt_add_resources(UEFI_SPELLING_RESOURCES resources/embedded/uefi_spellings.qrc)

add_library(qldm-core STATIC
    ${UEFI_SPELLING_RESOURCES}
    src/core/inventory.h src/core/inventory.cpp src/core/connectionparents.cpp
    src/core/typeprojection.h src/core/typeprojection.cpp
    src/core/commonproperties.h src/core/commonproperties.cpp src/core/commonpropertywording.h
    src/core/commonpropertyreader.h src/core/commonpropertyreader.cpp src/core/propertyreadhelpers.h
    src/core/systemreport.h src/core/systemreportplatform.cpp
    src/core/systemreportprocessor.cpp src/core/systemreportmemory.cpp
    src/devices/secureboot.h src/devices/secureboot.cpp
    src/devices/systemproperties.h src/devices/systemproperties.cpp src/devices/cputopology.h
    src/devices/device.h src/devices/device.cpp
    src/devices/deviceevents.h src/devices/deviceevents.cpp
    src/devices/cpumetadata.h src/devices/cpumetadata.cpp
    src/devices/functionnames.h src/devices/functionnames.cpp
    src/devices/acpivendors.h src/devices/acpivendors.cpp
    src/devices/devicelabel.h src/devices/devicelabel.cpp
    src/devices/efivariables.h src/devices/efivariables.cpp
    src/devices/modulenames.h src/devices/modulenames.cpp
    src/devices/devicepresentation.h src/devices/devicepresentation.cpp
    src/devices/deviceresources.h src/devices/deviceresources.cpp
    src/devices/usbproperties.h src/devices/usbproperties.cpp src/devices/usbformat.cpp
    src/devices/efivalue.h src/devices/efivalue.cpp
    src/devices/resourceformat.h src/devices/resourceformat.cpp
)
target_include_directories(qldm-core PUBLIC src/core src/devices)
target_link_libraries(qldm-core PUBLIC Qt6::Core PRIVATE Qt6::Network PkgConfig::UDEV)
target_compile_definitions(qldm-core PRIVATE
    QT_ENABLE_STRICT_MODE_UP_TO=${APP_QT_STRICT_VERSION})
if(KMOD AND TARGET PkgConfig::KMOD)
    target_link_libraries(qldm-core PRIVATE PkgConfig::KMOD)
    target_compile_definitions(qldm-core PRIVATE QLDM_HAVE_KMOD)
endif()
if(GUI)
    include(cmake/GuiFrontend.cmake)
endif()
install(FILES project.json DESTINATION ${CMAKE_INSTALL_DATADIR}/QLinuxDeviceManager)

# Some static FINAL CUT installations omit transitive dependencies from finalcut.pc.
# Keep these configurable: GPM and the terminal-capability provider vary by build.
set(TUI_FINALCUT_EXTRA_LIBRARIES "" CACHE STRING
    "Additional FINAL CUT link dependencies (semicolon-separated, e.g. gpm;tinfo)")
if(TUI)
    pkg_check_modules(FINALCUT QUIET IMPORTED_TARGET finalcut>=0.9.0)
    if(TARGET PkgConfig::FINALCUT)
        add_executable(qlinuxdevicemanager-tui src/tui/main.cpp
            src/tui/browser.h src/tui/browser.cpp
            src/tui/browsermenu.h src/tui/browsermenu.cpp
            src/tui/modalcombobox.h src/tui/modalcombobox.cpp
            src/tui/systemdialog.h src/tui/systemdialog.cpp
            src/tui/systemsnapshot.h src/tui/systemsnapshot.cpp
            src/tui/systempages/pages.h
            src/tui/systempages/report.h src/tui/systempages/report.cpp
            src/tui/systempages/operatingsystempage.cpp
            src/tui/systempages/motherboardpage.cpp
            src/tui/systempages/processorpage.cpp
            src/tui/systempages/rampage.cpp
            src/tui/treeprojection.h src/tui/treeprojection.cpp
            src/tui/inventoryfilter.h src/tui/inventoryfilter.cpp
            src/tui/terminaltext.h src/tui/terminaltext.cpp
            src/tui/propertiesdialog.h src/tui/propertiesdialog.cpp
            src/tui/propertypagecontrol.h
            src/tui/propertiesstatus.h src/tui/propertiesstatus.cpp
            src/tui/propertypages.h src/tui/propertypages.cpp
            src/tui/devicepages/resourcespage.h src/tui/devicepages/resourcespage.cpp
            src/tui/devicepages/usbpage.h src/tui/devicepages/usbpage.cpp
            src/tui/devicetree.h src/tui/devicetree.cpp)
        target_include_directories(qlinuxdevicemanager-tui PRIVATE src/tui)
        target_link_libraries(qlinuxdevicemanager-tui PRIVATE
            qldm-core PkgConfig::FINALCUT ${TUI_FINALCUT_EXTRA_LIBRARIES})
        target_compile_options(qlinuxdevicemanager-tui PRIVATE -Wall -Wextra -Wpedantic)
        install(TARGETS qlinuxdevicemanager-tui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    elseif(GUI)
        message(WARNING "FINAL CUT >= 0.9.0 not found; skipping terminal frontend")
    else()
        message(FATAL_ERROR "FINAL CUT >= 0.9.0 is required for a TUI-only build")
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    if(GUI)
        add_executable(tree-export-test tests/tree-export.cpp src/ui/treeexport.h src/ui/treeexport.cpp)
        target_include_directories(tree-export-test PRIVATE src/ui)
        target_link_libraries(tree-export-test PRIVATE Qt6::Widgets)
        add_test(NAME tree-export COMMAND tree-export-test)
    endif()
    add_executable(pcie-link-text-test tests/pcie-link-text.cpp src/devices/pcielinktext.h)
    target_include_directories(pcie-link-text-test PRIVATE src/devices)
    target_link_libraries(pcie-link-text-test PRIVATE Qt6::Core)
    add_test(NAME pcie-link-text COMMAND pcie-link-text-test)
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
    if(GUI)
        add_executable(device-rules-test tests/device-rules.cpp
            ${UEFI_SPELLING_RESOURCES}
            src/devices/device.cpp src/devices/device.h
            src/devices/cpumetadata.cpp src/devices/cpumetadata.h
            src/devices/functionnames.cpp src/devices/functionnames.h
            src/devices/acpivendors.cpp src/devices/acpivendors.h
            src/devices/devicelabel.cpp src/devices/devicelabel.h
            src/devices/efivariables.cpp src/devices/efivariables.h
            src/devices/devicepresentation.cpp src/devices/devicepresentation.h)
        target_include_directories(device-rules-test PRIVATE src/devices)
        target_compile_definitions(device-rules-test PRIVATE QLDM_TEST_ACPI_IDS="${ACPI_NAME_FILE}")
        target_link_libraries(device-rules-test PRIVATE Qt6::Widgets)
        add_test(NAME device-rules COMMAND device-rules-test)
    endif()
    add_executable(device-resources-test tests/device-resources.cpp
        src/devices/deviceresources.cpp src/devices/deviceresources.h)
    target_include_directories(device-resources-test PRIVATE src/devices)
    target_link_libraries(device-resources-test PRIVATE Qt6::Core)
    add_test(NAME device-resources COMMAND device-resources-test)
    if(GUI)
        add_executable(device-search-test tests/device-search.cpp
            src/devices/devicesearch.cpp src/devices/devicesearch.h
            src/devices/deviceproperties.cpp src/devices/deviceproperties.h
            src/devices/usbproperties.h src/devices/usbproperties.cpp src/devices/usbformat.cpp
            src/devices/efivalue.h src/devices/efivalue.cpp
            src/devices/networkproperties.cpp src/devices/networkproperties.h
            src/devices/ethernetproperties.cpp src/devices/ethernetproperties.h
            src/devices/genericnetlink.h src/devices/wirelessproperties.cpp src/devices/wirelessproperties.h
            src/devices/displayproperties.cpp src/devices/displayproperties.h
            src/devices/monitorproperties.cpp src/devices/monitorproperties.h
            src/devices/cameraproperties.cpp src/devices/cameraproperties.h
            src/devices/bluetoothproperties.cpp src/devices/bluetoothproperties.h
            src/devices/audioproperties.h src/devices/audioproperties.cpp
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
            src/devices/acpivendors.cpp src/devices/acpivendors.h
            src/ui/devicemodel.cpp src/ui/devicemodel.h
            src/core/connectionparents.cpp src/core/inventory.h
            src/core/typeprojection.h src/core/typeprojection.cpp
            src/core/commonproperties.h src/core/commonproperties.cpp src/core/commonpropertywording.h
            src/core/commonpropertyreader.h src/core/commonpropertyreader.cpp src/core/propertyreadhelpers.h
            src/ui/icontheme.cpp src/ui/icontheme.h
            src/ui/devicefilter.cpp src/ui/devicefilter.h)
        target_include_directories(device-search-test PRIVATE src/core src/devices src/ui
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
        if(BLUETOOTH AND TARGET Qt6::DBus)
            target_link_libraries(device-search-test PRIVATE Qt6::DBus)
            target_compile_definitions(device-search-test PRIVATE QLDM_HAVE_BLUEZ)
        endif()
        if(UDISKS2 AND TARGET Qt6::DBus)
            target_link_libraries(device-search-test PRIVATE Qt6::DBus)
            target_compile_definitions(device-search-test PRIVATE QLDM_HAVE_DBUS)
        endif()
        add_test(NAME device-search COMMAND device-search-test)
    endif()
    add_executable(system-properties-test tests/system-properties.cpp
        src/devices/systemproperties.cpp src/devices/systemproperties.h src/devices/cputopology.h
        src/devices/secureboot.cpp src/devices/secureboot.h
        src/devices/cpumetadata.cpp src/devices/cpumetadata.h)
    target_include_directories(system-properties-test PRIVATE src/devices)
    target_link_libraries(system-properties-test PRIVATE Qt6::Core Qt6::Network PkgConfig::UDEV)
    add_test(NAME system-properties COMMAND system-properties-test)
    add_executable(type-projection-test tests/type-projection.cpp)
    target_link_libraries(type-projection-test PRIVATE qldm-core)
    add_test(NAME type-projection COMMAND type-projection-test)
    add_executable(system-report-test tests/system-report.cpp)
    target_link_libraries(system-report-test PRIVATE qldm-core)
    add_test(NAME system-report COMMAND system-report-test)
endif()

# Apply the declared Qt baseline's API checks to project executables and fixture targets.
foreach(qldm_target IN ITEMS qlinuxdevicemanager monitor-state-test device-events-test
        device-rules-test device-resources-test device-search-test system-properties-test system-report-test
        directory-scan-test display-identity-test gpu-link-sample-test pcie-link-text-test tree-export-test)
    if(TARGET ${qldm_target})
        target_compile_definitions(${qldm_target} PRIVATE QT_ENABLE_STRICT_MODE_UP_TO=${APP_QT_STRICT_VERSION})
    endif()
endforeach()
