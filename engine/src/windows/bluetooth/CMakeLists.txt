qt_add_library(quickshell-windows-bluetooth STATIC
	bluetooth.cpp
	adapter.cpp
	device.cpp
	bt_backend.cpp
	bt_worker.cpp
	ks_audio.cpp
	device_class.cpp
)

qt_add_qml_module(quickshell-windows-bluetooth
	URI Quickshell.Bluetooth
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-windows-bluetooth Quickshell)

install_qml_module(quickshell-windows-bluetooth)

target_include_directories(quickshell-windows-bluetooth PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(quickshell-windows-bluetooth PRIVATE
	Qt::Qml windowsapp runtimeobject ole32 uuid
)

qs_module_pch(quickshell-windows-bluetooth SET large)

target_link_libraries(quickshell PRIVATE quickshell-windows-bluetoothplugin)
