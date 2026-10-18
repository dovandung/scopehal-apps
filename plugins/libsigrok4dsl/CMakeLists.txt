# ngscopeclient plugin: native driver for libsigrok4DSL devices (DSView / ALL-LOGIC)
#
# Built from the scopehal-apps tree when ALLLOGIC_DIR points at an ALL-LOGIC or DSView
# source checkout:
#
#   cmake -B build -DALLLOGIC_DIR=/path/to/ALL-LOGIC
#
# The plugin is written next to the ngscopeclient binary, where scopehal looks for plugins,
# and installs to <prefix>/lib/scopehal/plugins.

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/libsigrok4DSL.cmake)
find_package(Threads REQUIRED)

# Capture engine: libsigrok4DSL + logtools only, no scopehal types
add_library(dslsession STATIC DSLSession.cpp)
set_target_properties(dslsession PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
	CXX_VISIBILITY_PRESET hidden)
# "log/log.h" from scopehal-apps/lib: libsigrok4DSL has a log.h of its own.
# logtools is deliberately not linked here: inside ngscopeclient the Log* functions must be the
# host's (libscopehal's), or the plugin would log into its own empty sink list.
target_include_directories(dslsession PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../../lib)
target_link_libraries(dslsession PUBLIC sigrok4DSL)
if(WIN32)
	target_compile_definitions(dslsession PUBLIC NOMINMAX)
endif()

# The plugin itself
if(TARGET scopehal)
	add_library(scopehal-libsigrok4dsl MODULE
		LibSigrok4DSLOscilloscope.cpp
		PluginMain.cpp)
	set_target_properties(scopehal-libsigrok4dsl PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_VISIBILITY_PRESET hidden
		PREFIX "lib")
	# log after dslsession: on Windows it is a DLL and MinGW's linker scans libraries once, in order
	target_link_libraries(scopehal-libsigrok4dsl PRIVATE scopehal dslsession log)

	# Last-resort firmware directory: the source tree's DSView/res (for uninstalled ALL-LOGIC)
	if(EXISTS "${ALLLOGIC_DIR}/DSView/res")
		target_compile_definitions(scopehal-libsigrok4dsl PRIVATE
			DSL_SOURCE_RES_DIR="${ALLLOGIC_DIR}/DSView/res")
	endif()

	if(WIN32)
		# scopehal loads <exe dir>\plugins\*.dll. Only PluginInit is exported (__declspec).
		set_target_properties(scopehal-libsigrok4dsl PROPERTIES
			LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/ngscopeclient/plugins")
		install(TARGETS scopehal-libsigrok4dsl LIBRARY DESTINATION bin/plugins RUNTIME DESTINATION bin/plugins)
		install(DIRECTORY "${ALLLOGIC_DIR}/DSView/res" DESTINATION bin)
	else()
		# scopehal loads plugins from the executable's directory, among others
		set_target_properties(scopehal-libsigrok4dsl PROPERTIES
			LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/ngscopeclient")
		target_link_options(scopehal-libsigrok4dsl PRIVATE
			"-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/plugin.map")
		set_property(TARGET scopehal-libsigrok4dsl APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/plugin.map)
		# scopehal searches <prefix>/lib/scopehal/plugins literally (not lib64 or a multiarch dir)
		install(TARGETS scopehal-libsigrok4dsl LIBRARY DESTINATION lib/scopehal/plugins)
	endif()

	# Windows packages (-DWIXPATH=...): add the plugin, its DLL dependencies and the firmware to
	# dist/ngscopeclient_windows_x64 before the portable zip and MSI are made from it
	if(WIN32 AND TARGET ngscopeclient_dist_windows_x64)
		set(DIST "${CMAKE_BINARY_DIR}/dist/ngscopeclient_windows_x64")
		get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
		add_custom_target(libsigrok4dsl_dist_windows_x64
			COMMENT "Adding libsigrok4DSL plugin, its DLLs and firmware to dist/ngscopeclient_windows_x64..."
			DEPENDS ngscopeclient_dist_windows_x64 scopehal-libsigrok4dsl
			COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/windows/bundle-plugin.sh"
				"${DIST}"
				"$<TARGET_FILE:scopehal-libsigrok4dsl>"
				"${MINGW_BIN_DIR}"
				"${CMAKE_BINARY_DIR}"
				"${ALLLOGIC_DIR}/DSView/res"
			VERBATIM)
		foreach(t ngscopeclient_portable_zip_windows_x64 msi_windows_x64_ng)
			if(TARGET ${t})
				add_dependencies(${t} libsigrok4dsl_dist_windows_x64)
			endif()
		endforeach()
	endif()
endif()

# Engine tests, runnable without hardware or a GPU (uses the libsigrok4DSL demo device)
option(LIBSIGROK4DSL_PLUGIN_TESTS "Build the libsigrok4DSL plugin engine test" OFF)
if(LIBSIGROK4DSL_PLUGIN_TESTS)
	add_executable(libsigrok4dsl-engine-test tests/engine_test.cpp)
	set_target_properties(libsigrok4dsl-engine-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
	if(TARGET ngscopeclient)
		# Next to ngscopeclient, where liblog.dll & co are copied on Windows
		set_target_properties(libsigrok4dsl-engine-test PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/ngscopeclient")
	endif()
	target_link_libraries(libsigrok4dsl-engine-test PRIVATE dslsession log Threads::Threads)

	# Plugin inside libscopehal, the way ngscopeclient uses it (needs a Vulkan device, e.g. lavapipe).
	# Placed next to ngscopeclient so InitializePlugins() finds the plugin where it really lives.
	if(TARGET scopehal)
		add_executable(libsigrok4dsl-plugin-smoke-test tests/plugin_smoke_test.cpp)
		set_target_properties(libsigrok4dsl-plugin-smoke-test PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/ngscopeclient")
		target_link_libraries(libsigrok4dsl-plugin-smoke-test PRIVATE scopehal)
		add_dependencies(libsigrok4dsl-plugin-smoke-test scopehal-libsigrok4dsl)
	endif()
endif()
