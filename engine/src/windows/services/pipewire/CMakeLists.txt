qt_add_library(quickshell-windows-pipewire STATIC
	policy_config.cpp
	pw_types.cpp
	pw_node.cpp
	pw_link.cpp
	pw_object_tracker.cpp
	pw_peak.cpp
	pw_backend.cpp
	pipewire.cpp
)

qt_add_qml_module(quickshell-windows-pipewire
	URI Quickshell.Services.Pipewire
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-windows-pipewire Quickshell)

install_qml_module(quickshell-windows-pipewire)

target_link_libraries(quickshell-windows-pipewire PRIVATE Qt::Quick ole32)

qs_module_pch(quickshell-windows-pipewire SET large)

target_link_libraries(quickshell PRIVATE quickshell-windows-pipewireplugin)
