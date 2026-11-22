qt_add_library(quickshell-windows-notifications STATIC
	notification.cpp
	notify_send.cpp
	server.cpp
	toast_mirror.cpp
	toast_worker.cpp
	qml.cpp
)

qt_add_qml_module(quickshell-windows-notifications
	URI Quickshell.Services.Notifications
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-windows-notifications Quickshell)

install_qml_module(quickshell-windows-notifications)

target_link_libraries(quickshell-windows-notifications PRIVATE
	Qt::Quick Qt::Gui quickshell-core quickshell-windows windowsapp runtimeobject
)

qs_module_pch(quickshell-windows-notifications SET large)

target_link_libraries(quickshell PRIVATE quickshell-windows-notificationsplugin)
