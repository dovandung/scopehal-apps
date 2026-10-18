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

# The plugin itself
if(TARGET scopehal)
	add_library(scopehal-libsigrok4dsl MODULE
		LibSigrok4DSLOscilloscope.cpp
		PluginMain.cpp)
	set_target_properties(scopehal-libsigrok4dsl PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON
		CXX_VISIBILITY_PRESET hidden
		PREFIX "lib"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/ngscopeclient")
	target_link_libraries(scopehal-libsigrok4dsl PRIVATE scopehal dslsession)

	# Last-resort firmware directory: the source tree's DSView/res (for uninstalled ALL-LOGIC)
	if(EXISTS "${ALLLOGIC_DIR}/DSView/res")
		target_compile_definitions(scopehal-libsigrok4dsl PRIVATE
			DSL_SOURCE_RES_DIR="${ALLLOGIC_DIR}/DSView/res")
	endif()
	target_link_options(scopehal-libsigrok4dsl PRIVATE
		"-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/plugin.map")
	set_property(TARGET scopehal-libsigrok4dsl APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/plugin.map)

	# scopehal searches <prefix>/lib/scopehal/plugins literally (not lib64 or a multiarch dir)
	install(TARGETS scopehal-libsigrok4dsl LIBRARY DESTINATION lib/scopehal/plugins)
endif()

# Engine tests, runnable without hardware or a GPU (uses the libsigrok4DSL demo device)
option(LIBSIGROK4DSL_PLUGIN_TESTS "Build the libsigrok4DSL plugin engine test" OFF)
if(LIBSIGROK4DSL_PLUGIN_TESTS)
	add_executable(libsigrok4dsl-engine-test tests/engine_test.cpp)
	set_target_properties(libsigrok4dsl-engine-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
	target_link_libraries(libsigrok4dsl-engine-test PRIVATE dslsession log pthread)
endif()
