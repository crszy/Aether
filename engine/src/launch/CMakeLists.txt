find_package(CLI11 CONFIG QUIET)

if (NOT TARGET CLI11::CLI11)
	set(CLI11_INCLUDE_DIR "" CACHE PATH "Directory containing CLI/CLI.hpp (header-only CLI11)")

	if (CLI11_INCLUDE_DIR)
		add_library(CLI11::CLI11 INTERFACE IMPORTED)
		target_include_directories(CLI11::CLI11 INTERFACE "${CLI11_INCLUDE_DIR}")
	else()
		find_package(CLI11 CONFIG REQUIRED)
	endif()
endif()

qt_add_library(quickshell-launch STATIC
	parsecommand.cpp
	command.cpp
	launch.cpp
	main.cpp
)

if (WIN32)
	target_sources(quickshell-launch PRIVATE daemon_win.cpp)
endif()

target_link_libraries(quickshell-launch PRIVATE
	Qt::Quick Qt::Widgets CLI11::CLI11 quickshell-build
)

qs_add_pchset(launch
	DEPENDENCIES Qt::Core CLI11::CLI11
	HEADERS
		<CLI/App.hpp>
		<qcoreapplication.h>
)

qs_pch(quickshell-launch SET launch)

target_link_libraries(quickshell PRIVATE quickshell-launch)
