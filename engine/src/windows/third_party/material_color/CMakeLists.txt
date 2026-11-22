# material-color-utilities' HCT, for system/terminal_colors (see README.md). Its own library so
# it stays out of quickshell-windows' precompiled header.
add_library(material-color STATIC
	cpp/cam/cam.cc
	cpp/cam/hct.cc
	cpp/cam/hct_solver.cc
	cpp/utils/utils.cc
)

# Upstream includes its headers as "cpp/cam/hct.h".
target_include_directories(material-color PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Plain C++, nothing for moc.
set_target_properties(material-color PROPERTIES AUTOMOC OFF)
