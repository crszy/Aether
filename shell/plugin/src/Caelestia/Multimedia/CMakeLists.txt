qml_module(caelestia-multimedia
    URI Caelestia.Multimedia
    SOURCES
        visualsource.cpp
        imagecache.cpp
        imagecacheworker.cpp
        ktxencoder.cpp
    QML_FILES
        Visual.qml
    LIBRARIES
        Qt::Quick
        Qt::Multimedia
        Qt::Concurrent
        Qt::Sql
        rgbcx
)

if(WIN32)
    target_compile_definitions(caelestia-multimedia PRIVATE CAE_BUILD_MULTIMEDIA)
endif()
