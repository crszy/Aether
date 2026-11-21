find_package(Qt6 REQUIRED COMPONENTS ShaderTools Core Qml Gui Quick QuickControls2 Concurrent Sql Network DBus Multimedia)
if(WIN32)
    # Windows build: libqalculate / PipeWire / aubio / cava / lm_sensors are replaced by Windows code
    foreach(t PkgConfig::Qalculate PkgConfig::Pipewire PkgConfig::Aubio PkgConfig::Cava Sensors::Sensors)
        add_library(${t} INTERFACE IMPORTED)
    endforeach()
    target_link_libraries(PkgConfig::Pipewire INTERFACE ole32)
    target_link_libraries(PkgConfig::Aubio INTERFACE powrprof)
    target_link_libraries(Sensors::Sensors INTERFACE gdi32)
else()
find_package(PkgConfig REQUIRED)
pkg_check_modules(Qalculate IMPORTED_TARGET libqalculate REQUIRED)
pkg_check_modules(Pipewire IMPORTED_TARGET libpipewire-0.3 REQUIRED)
pkg_check_modules(Aubio IMPORTED_TARGET aubio REQUIRED)
pkg_check_modules(Cava IMPORTED_TARGET libcava QUIET)
if(NOT Cava_FOUND)
    pkg_check_modules(Cava IMPORTED_TARGET cava REQUIRED)
endif()
include(cmake/sensorslib.cmake)
endif()

set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
qt_standard_project_setup(REQUIRES 6.9)

include(cmake/pch.cmake)
include(cmake/qml-module.cmake)

# Header only helpers
add_library(caelestia-util INTERFACE)
target_include_directories(caelestia-util INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_subdirectory(3rdparty/rgbcx)
add_subdirectory(src/Caelestia)
