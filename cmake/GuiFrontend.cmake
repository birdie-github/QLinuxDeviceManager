# Included from the project root so source and install paths stay relative to it.
find_package(Qt6 ${APP_MINIMUM_QT} REQUIRED COMPONENTS Widgets)
# Optional read-only UDisks2 cache access. Native storage viewing still works without it.
option(UDISKS2 "Use optional QtDBus for cached UDisks2 storage metadata" ON)
option(BLUETOOTH "Use optional QtDBus for BlueZ Bluetooth metadata" ON)
option(ENABLE_DONATION_BUTTON "Include the toolbar donation button and its preference" ON)
if(UDISKS2 OR BLUETOOTH)
    find_package(Qt6 ${APP_MINIMUM_QT} QUIET COMPONENTS DBus)
endif()
pkg_check_modules(V4L2 REQUIRED IMPORTED_TARGET libv4l2)
option(AUDIO "Use optional libpulse for local audio session information" ON)
if(AUDIO)
    pkg_check_modules(PULSE QUIET IMPORTED_TARGET libpulse>=5.0)
endif()
# Headers only: session libraries are opened on demand with QLibrary (dlopen).
# Do not link PkgConfig targets for these backends into the executable.
pkg_check_modules(DISPLAY_X11 REQUIRED xcb-randr)
pkg_check_modules(DISPLAY_WAYLAND REQUIRED wayland-client>=1.20)
option(DISPLAY_INFO "Use optional libdisplay-info for monitor EDID decoding" ON)
if(DISPLAY_INFO)
    pkg_check_modules(DISPLAY_INFO QUIET IMPORTED_TARGET libdisplay-info>=0.1.1)
endif()
add_executable(qlinuxdevicemanager
    src/main.cpp
    src/ui/reporttable.h src/ui/reporttable.cpp
    src/devices/enumerator.h src/devices/enumerator.cpp
    src/devices/monitorstate.h src/devices/monitorstate.cpp
    src/devices/deviceproperties.h src/devices/deviceproperties.cpp
    src/devices/networkproperties.h src/devices/networkproperties.cpp
    src/devices/ethernetproperties.h src/devices/ethernetproperties.cpp
    src/devices/genericnetlink.h src/devices/wirelessproperties.h src/devices/wirelessproperties.cpp
    src/devices/displayproperties.h src/devices/displayproperties.cpp
    src/devices/monitorproperties.h src/devices/monitorproperties.cpp
    src/devices/cameraproperties.h src/devices/cameraproperties.cpp
    src/devices/bluetoothproperties.h src/devices/bluetoothproperties.cpp
    src/devices/audioproperties.h src/devices/audioproperties.cpp
    src/devices/displaybackend.h src/devices/displayx11.cpp src/devices/displaywayland.cpp
    src/devices/displayidentity.h src/devices/displayidentity.cpp
    src/devices/storageproperties.h src/devices/storageproperties.cpp
    src/devices/directoryscan.h src/devices/naturalsort.h
    src/devices/storagepresentation.h src/devices/storagepresentation.cpp



    src/devices/devicesearch.h src/devices/devicesearch.cpp
    src/devices/propertytext.h src/devices/propertytext.cpp
    src/ui/devicefilter.h src/ui/devicefilter.cpp
    src/ui/devicemodel.h src/ui/devicemodel.cpp
    src/ui/icontheme.h src/ui/icontheme.cpp
    src/ui/mainwindow.h src/ui/mainwindow.cpp
    src/ui/aboutdialog.h src/ui/aboutdialog.cpp
    src/ui/treeexport.h src/ui/treeexport.cpp src/ui/iconcredits.h
    src/devices/pcielinktext.h
    src/ui/gpulinktest.h src/ui/gpulinktest.cpp
    src/ui/propertiesdialog.h src/ui/propertiesdialog.cpp
    src/ui/propertyreport.h src/ui/propertyreport.cpp
    src/ui/devicepages/all.h
    src/ui/devicepages/detailspage.h src/ui/devicepages/detailspage.cpp
    src/ui/devicepages/propertyfieldspage.h src/ui/devicepages/propertyfieldspage.cpp
    src/ui/propertypage.h src/ui/propertypage.cpp
    src/ui/devicepages/bluetoothpage.h src/ui/devicepages/bluetoothpage.cpp
    src/ui/devicepages/audiopage.h src/ui/devicepages/audiopage.cpp
    src/ui/devicepages/networkpage.h src/ui/devicepages/networkpage.cpp
    src/ui/devicepages/camerapage.h src/ui/devicepages/camerapage.cpp
    src/ui/devicepages/displaypage.h src/ui/devicepages/displaypage.cpp
    src/ui/devicepages/usbpage.h src/ui/devicepages/usbpage.cpp
    src/ui/devicepages/batterypage.h src/ui/devicepages/batterypage.cpp
    src/ui/devicepages/pciepage.h src/ui/devicepages/pciepage.cpp
    src/ui/devicepages/healthpage.h src/ui/devicepages/healthpage.cpp
    src/ui/devicepages/volumespage.h src/ui/devicepages/volumespage.cpp
    src/ui/devicepages/storagepage.h src/ui/devicepages/storagepage.cpp
    src/ui/devicepages/eventspage.h src/ui/devicepages/eventspage.cpp
    src/ui/devicepages/resourcespage.h src/ui/devicepages/resourcespage.cpp
    src/ui/systempages/rampage.h src/ui/systempages/rampage.cpp
    src/ui/systempages/motherboardpage.h src/ui/systempages/motherboardpage.cpp
    src/ui/systempages/processorpage.h src/ui/systempages/processorpage.cpp
    src/ui/systempages/operatingsystempage.h src/ui/systempages/operatingsystempage.cpp
    src/ui/systemdialog.h src/ui/systemdialog.cpp)
if(ENABLE_DONATION_BUTTON)
    target_sources(qlinuxdevicemanager PRIVATE src/ui/donatebutton.h src/ui/donatebutton.cpp)
    target_compile_definitions(qlinuxdevicemanager PRIVATE QLDM_ENABLE_DONATION_BUTTON)
endif()
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
if(AUDIO AND TARGET PkgConfig::PULSE)
    target_link_libraries(qlinuxdevicemanager PRIVATE PkgConfig::PULSE)
    target_compile_definitions(qlinuxdevicemanager PRIVATE QLDM_HAVE_PULSE)
    message(STATUS "Audio session information: enabled (libpulse)")
else()
    message(STATUS "Audio session information: unavailable (libpulse disabled or not found)")
endif()
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
if(BLUETOOTH AND TARGET Qt6::DBus)
    target_link_libraries(qlinuxdevicemanager PRIVATE Qt6::DBus)
    target_compile_definitions(qlinuxdevicemanager PRIVATE QLDM_HAVE_BLUEZ)
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
# External application icon sets; no assets are embedded into the GPL executable.
target_compile_definitions(qlinuxdevicemanager PRIVATE QLDM_HAVE_ICON_PATHS)
set(QLDM_ICON_THEME_DIR "${CMAKE_INSTALL_FULL_DATADIR}/${APP_NAME}/icons")
file(RELATIVE_PATH QLDM_RELATIVE_ICON_THEME_DIR "${CMAKE_INSTALL_FULL_BINDIR}" "${QLDM_ICON_THEME_DIR}")
configure_file(resources/embedded/iconpaths.h.in generated/iconpaths.h @ONLY)
option(ICONS8_ICONS "Install the separately licensed Icons8 icon set" ON)
install(DIRECTORY resources/external/icon-themes/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/${APP_NAME}/icons
    PATTERN "Icons8 Windows 11 Color" EXCLUDE
    PATTERN "README.md" EXCLUDE)
if(ICONS8_ICONS)
    install(DIRECTORY "resources/external/icon-themes/Icons8 Windows 11 Color"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${APP_NAME}/icons)
endif()
set(ACPI_NAME_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/external/device-names/acpi-pnp.ids")
# Keep local builds usable without installation; the data stays outside the binary.
configure_file("${ACPI_NAME_FILE}" "${CMAKE_CURRENT_BINARY_DIR}/acpi-pnp.ids" COPYONLY)
install(FILES "${ACPI_NAME_FILE}" DESTINATION ${CMAKE_INSTALL_DATADIR}/${APP_NAME})
set(BLUETOOTH_SERVICE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/external/device-names/bluetooth-services.ids")
configure_file("${BLUETOOTH_SERVICE_FILE}" "${CMAKE_CURRENT_BINARY_DIR}/bluetooth-services.ids" COPYONLY)
install(FILES "${BLUETOOTH_SERVICE_FILE}" DESTINATION ${CMAKE_INSTALL_DATADIR}/${APP_NAME})
target_compile_definitions(qlinuxdevicemanager PRIVATE
    QLDM_INSTALLED_BLUETOOTH_SERVICES="${CMAKE_INSTALL_FULL_DATADIR}/${APP_NAME}/bluetooth-services.ids")
target_compile_definitions(qlinuxdevicemanager PRIVATE
    QLDM_INSTALLED_ACPI_IDS="${CMAKE_INSTALL_FULL_DATADIR}/${APP_NAME}/acpi-pnp.ids")
target_link_libraries(qlinuxdevicemanager PRIVATE qldm-core)

# Optional tools: basic viewing never requires Linguist.
find_package(Qt6 ${APP_MINIMUM_QT} QUIET COMPONENTS LinguistTools)
if(TARGET Qt6::lupdate)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_custom_target(update-translations
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/device-name-data.py"
                translations --lupdate $<TARGET_FILE:Qt6::lupdate>)
    else()
        message(STATUS "Python3 is needed only for the update-translations maintenance target")
    endif()
endif()
if(TARGET Qt6::lrelease)
    qt_add_translation(QM_FILES resources/external/translations/qlinuxdevicemanager_en.ts)
    add_custom_target(translations ALL DEPENDS ${QM_FILES})
    install(FILES ${QM_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/QLinuxDeviceManager/translations)
endif()
