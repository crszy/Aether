qt_add_library(quickshell-windows-systray STATIC
	hook.cpp
	seed.cpp
	toolbar.cpp
	host.cpp
	item.cpp
	qml.cpp
)

qt_add_qml_module(quickshell-windows-systray
	URI Quickshell.Services.SystemTray
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-windows-systray Quickshell)

install_qml_module(quickshell-windows-systray)

target_link_libraries(quickshell-windows-systray PRIVATE
	Qt::Quick Qt::Gui quickshell-core quickshell-windows user32 shell32 ole32 version
)

qs_module_pch(quickshell-windows-systray SET large)

target_link_libraries(quickshell PRIVATE quickshell-windows-systrayplugin)
