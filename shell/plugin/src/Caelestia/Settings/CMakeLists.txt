qml_module(caelestia-settings
    URI Caelestia.Settings
    INCLUDE_PREFIX settings
    SOURCES
        common.cpp
        schema.cpp
        codecs.cpp
        quarantine.cpp
        node.cpp
        objectnode.cpp
        listnode.cpp
        rootnode.cpp
        settingsfile.cpp
        changebatcher.cpp
    LIBRARIES
        caelestia-util
)

if(WIN32)
    target_compile_definitions(caelestia-settings PRIVATE CAE_BUILD_SETTINGS)
endif()
