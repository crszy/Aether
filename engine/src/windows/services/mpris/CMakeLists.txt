qt_add_library(quickshell-windows-mpris STATIC
	player.cpp
	mpris.cpp
	gsmtc_backend.cpp
	gsmtc_worker.cpp
	art_cache.cpp
)

qt_add_qml_module(quickshell-windows-mpris
	URI Quickshell.Services.Mpris
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-windows-mpris Quickshell)

install_qml_module(quickshell-windows-mpris)

target_link_libraries(quickshell-windows-mpris PRIVATE Qt::Quick Qt::Gui windowsapp runtimeobject)

qs_module_pch(quickshell-windows-mpris SET large)

target_link_libraries(quickshell PRIVATE quickshell-windows-mprisplugin)
