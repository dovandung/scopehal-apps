/***********************************************************************************************************************
*                                                                                                                      *
* libsigrok4DSL plugin for ngscopeclient                                                                               *
*                                                                                                                      *
* Licensed under the GNU GPL v3 or later (libsigrok4DSL is GPLv3).                                                     *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@brief Plugin entry point: scopehal's InitializePlugins() calls PluginInit() after dlopen()
 */

#include "LibSigrok4DSLOscilloscope.h"

extern "C" __attribute__((visibility("default"))) void PluginInit();

void PluginInit()
{
	AddDriverClass(LibSigrok4DSLOscilloscope);
	LogDebug("libsigrok4DSL plugin: registered driver \"%s\"\n",
		LibSigrok4DSLOscilloscope::GetDriverNameInternal().c_str());
}
