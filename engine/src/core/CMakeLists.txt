if (NOT WIN32)
	pkg_check_modules(libdrm REQUIRED IMPORTED_TARGET libdrm)
endif()

qt_add_library(quickshell-core STATIC
	plugin.cpp
	shell.cpp
	variants.cpp
	rootwrapper.cpp
	reload.cpp
	rootwrapper.cpp
	qmlglobal.cpp
	qmlscreen.cpp
	region.cpp
	persistentprops.cpp
	singleton.cpp
	generation.cpp
	scan.cpp
	scanenv.cpp
	qsintercept.cpp
	qmlcache.cpp
	incubator.cpp
	lazyloader.cpp
	easingcurve.cpp
	iconimageprovider.cpp
	imageprovider.cpp
	transformwatcher.cpp
	boundcomponent.cpp
	model.cpp
	elapsedtimer.cpp
	desktopentry.cpp
	desktopentrymonitor.cpp
	platformmenu.cpp
	qsmenu.cpp
	retainable.cpp
	popupanchor.cpp
	types.cpp
	qsmenuanchor.cpp
	clock.cpp
	logging.cpp
	paths.cpp
	instanceinfo.cpp
	common.cpp
	iconprovider.cpp
	scriptmodel.cpp
	colorquantizer.cpp
	backgroundpool.cpp
	toolsupport.cpp
	streamreader.cpp
	debuginfo.cpp
)

if (WIN32)
	target_sources(quickshell-core PRIVATE winfilelock.cpp)
endif()

qt_add_qml_module(quickshell-core
	URI Quickshell
	VERSION 0.1
	DEPENDENCIES QtQuick
	OPTIONAL_IMPORTS Quickshell._Window
	DEFAULT_IMPORTS Quickshell._Window
)

install_qml_module(quickshell-core)

target_link_libraries(quickshell-core PRIVATE Qt::Quick Qt::QuickPrivate Qt::Widgets quickshell-build)

if (WIN32)
	target_link_libraries(quickshell-core PRIVATE user32)
else()
	target_link_libraries(quickshell-core PRIVATE PkgConfig::libdrm)
endif()

qs_module_pch(quickshell-core SET large)

target_link_libraries(quickshell PRIVATE quickshell-coreplugin)

if (BUILD_TESTING)
	add_subdirectory(test)
endif()
