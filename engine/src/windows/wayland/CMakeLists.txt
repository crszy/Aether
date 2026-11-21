qt_add_library(quickshell-windows-wayland STATIC
	wlr_layershell.cpp
	idle_inhibitor.cpp
	toplevel.cpp
	screencopy_view.cpp
	window_thumbnail.cpp
	background_effect.cpp
)

qt_add_qml_module(quickshell-windows-wayland
	URI Quickshell.Wayland
	VERSION 0.1
	DEPENDENCIES QtQuick
	QML_FILES
		qml/WlSessionLock.qml
		qml/WlSessionLockSurface.qml
		qml/IdleMonitor.qml
		qml/ShortcutInhibitor.qml
)

qs_add_module_deps_light(quickshell-windows-wayland Quickshell Quickshell.Windows)

install_qml_module(quickshell-windows-wayland)

target_link_libraries(quickshell-windows-wayland PRIVATE Qt::Quick quickshell-core quickshell-windows powrprof d3d11 dwmapi)

qs_module_pch(quickshell-windows-wayland SET large)

target_link_libraries(quickshell PRIVATE quickshell-windows-waylandplugin)
