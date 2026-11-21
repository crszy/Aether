cmake_minimum_required(VERSION 3.20)
project(qs_windows_panel_animation_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Gui GuiPrivate Quick Qml Test)
qt_standard_project_setup(REQUIRES 6.11)

set(QS_WINDOWS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/..")

qt_add_executable(panel_animation_test
	panel_animation_test.cpp
	${QS_WINDOWS_SRC}/panel_animation.cpp
	${QS_WINDOWS_SRC}/panel_effects.cpp
	${QS_WINDOWS_SRC}/layer_rules.cpp
)

qt_add_resources(panel_animation_test "panel-effects"
	PREFIX "/Quickshell/Windows"
	BASE ${QS_WINDOWS_SRC}
	FILES ${QS_WINDOWS_SRC}/shaders/panel_effect.frag.qsb
)

target_include_directories(panel_animation_test PRIVATE ${QS_WINDOWS_SRC})
target_compile_definitions(panel_animation_test PRIVATE
	QS_WINDOWS_SHADER_SOURCE="${QS_WINDOWS_SRC}/shaders/panel_effect.frag"
	QS_WINDOWS_SHADER_QSB="${QS_WINDOWS_SRC}/shaders/panel_effect.frag.qsb"
)
target_link_libraries(panel_animation_test PRIVATE Qt6::Quick Qt6::Qml Qt6::Test Qt6::GuiPrivate)

qt_add_executable(screenshot_jobs_test
	screenshot_jobs_test.cpp
	${QS_WINDOWS_SRC}/system/screenshot_jobs.cpp
)

target_include_directories(screenshot_jobs_test PRIVATE ${QS_WINDOWS_SRC}/system)
target_link_libraries(screenshot_jobs_test PRIVATE Qt6::Gui Qt6::Test)

qt_add_executable(tiling_layout_test
	tiling_layout_test.cpp
	${QS_WINDOWS_SRC}/tiling_layout.cpp
)

target_include_directories(tiling_layout_test PRIVATE ${QS_WINDOWS_SRC})
target_link_libraries(tiling_layout_test PRIVATE Qt6::Core Qt6::Test)
