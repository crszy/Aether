qml_module(caelestia-images
    URI Caelestia.Images
    SOURCES
        imageanalyser.cpp
    LIBRARIES
        Qt::Gui
        Qt::Quick
        Qt::Concurrent
)

if(WIN32)
    target_compile_definitions(caelestia-images PRIVATE CAE_BUILD_IMAGES)
endif()
