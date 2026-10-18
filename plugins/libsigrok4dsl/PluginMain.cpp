/***********************************************************************************************************************
*                                                                                                                      *
* libsigrok4DSL plugin for ngscopeclient                                                                               *
*                                                                                                                      *
* Licensed under the GNU GPL v3 or later (libsigrok4DSL is GPLv3).                                                     *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@brief Plugin entry point: scopehal's InitializePlugins() calls PluginInit() after loading the library
 */

#include "LibSigrok4DSLOscilloscope.h"

#ifdef _WIN32
#define PLUGIN_EXPORT __declspec(dllexport)
#else
#define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

extern "C" PLUGIN_EXPORT void PluginInit();

void PluginInit()
{
	AddDriverClass(LibSigrok4DSLOscilloscope);
	LogDebug("libsigrok4DSL plugin: registered driver \"%s\"\n",
		LibSigrok4DSLOscilloscope::GetDriverNameInternal().c_str());
}
