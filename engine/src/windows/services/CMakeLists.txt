qt_add_library(quickshell-windows-services STATIC
	message_window.cpp
)

target_link_libraries(quickshell-windows-services PRIVATE Qt::Quick user32)

qs_pch(quickshell-windows-services SET large)

add_subdirectory(mpris)
add_subdirectory(pipewire)
add_subdirectory(upower)
add_subdirectory(notifications)
add_subdirectory(systray)
