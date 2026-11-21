qml_module(caelestia-state
    URI Caelestia.State
    INCLUDE_PREFIX state
    SOURCES
        common.cpp
        rootnodes.cpp
    DEPENDENCIES
        Caelestia.Settings
)
target_link_libraries(caelestia-state PUBLIC caelestia-settings)

if(WIN32)
    target_compile_definitions(caelestia-state PRIVATE CAE_BUILD_STATE)
endif()
