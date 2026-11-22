qt_add_library(quickshell-windows-crash STATIC
	handler.cpp
)

qs_pch(quickshell-windows-crash SET large)

target_link_libraries(quickshell-windows-crash PRIVATE quickshell-build Qt::Quick dbghelp)

target_link_libraries(quickshell PRIVATE quickshell-windows-crash)
