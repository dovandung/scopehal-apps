/***********************************************************************************************************************
*                                                                                                                      *
* libsigrok4DSL plugin for ngscopeclient                                                                               *
*                                                                                                                      *
* Licensed under the GNU GPL v3 or later (libsigrok4DSL is GPLv3).                                                     *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@brief End-to-end test of the plugin inside libscopehal, the way ngscopeclient uses it

	Initializes scopehal (headless Vulkan), loads plugins from the executable's directory, creates
	the "libsigrok4dsl" driver on the null transport with the demo device, acquires and checks the
	waveforms. Exit code 77 means "skipped" (no Vulkan device).

	  libsigrok4dsl-plugin-smoke-test [path]      default path "demo"
 */

#include "scopehal.h"
#include "Oscilloscope.h"
#include "OscilloscopeChannel.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace std;

static int g_failures = 0;

#define CHECK(cond, ...) do { \
	if(cond) { printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } \
	else { printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); g_failures++; } \
	fflush(stdout); \
} while(0)

static bool AcquireOne(shared_ptr<Oscilloscope> scope, int timeoutMs)
{
	auto t0 = chrono::steady_clock::now();
	while(chrono::steady_clock::now() - t0 < chrono::milliseconds(timeoutMs))
	{
		//Same calls as ngscopeclient's InstrumentThread
		if(scope->PollTrigger() == Oscilloscope::TRIGGER_MODE_TRIGGERED)
		{
			if(scope->AcquireData())
				return true;
		}
	}
	return false;
}

int main(int argc, char* argv[])
{
	string path = (argc > 1) ? argv[1] : "demo";

	g_log_sinks.emplace(g_log_sinks.begin(), new ColoredSTDLogSink(Severity::NOTICE));

	if(!VulkanInit(true))
	{
		printf("SKIP: no Vulkan device\n");
		return 77;
	}
	TransportStaticInit();
	DriverStaticInit();
	InitializePlugins();

	vector<string> drivers;
	Oscilloscope::EnumDrivers(drivers);
	bool registered = find(drivers.begin(), drivers.end(), "libsigrok4dsl") != drivers.end();
	CHECK(registered, "plugin loaded, driver \"libsigrok4dsl\" registered (%zu drivers total)", drivers.size());
	if(!registered)
		return 1;

	auto transport = SCPITransport::CreateTransport("null", path);
	CHECK(transport != nullptr, "null transport created with path \"%s\"", path.c_str());
	auto scope = Oscilloscope::CreateOscilloscope("libsigrok4dsl", transport);
	CHECK(scope != nullptr, "driver instantiated");
	if(!scope)
		return 1;

	printf("  model \"%s\", vendor \"%s\", serial \"%s\", %zu channels\n",
		scope->GetName().c_str(), scope->GetVendor().c_str(), scope->GetSerial().c_str(), scope->GetChannelCount());
	CHECK(scope->GetChannelCount() > 0, "channels created");
	CHECK(scope->GetTransportConnectionString() == path, "connection string kept for session save/restore");

	auto chan0 = scope->GetOscilloscopeChannel(0);
	bool digital = chan0->GetType(0) == Stream::STREAM_TYPE_DIGITAL;
	printf("  channel 0 \"%s\" is %s\n", chan0->GetDisplayName().c_str(), digital ? "digital" : "analog");

	auto rates = scope->GetSampleRatesNonInterleaved();
	auto depths = scope->GetSampleDepthsNonInterleaved();
	CHECK(!rates.empty() && !depths.empty(), "%zu sample rates, %zu depths", rates.size(), depths.size());

	scope->SetSampleRate(10000000);
	scope->SetSampleDepth(100000);
	CHECK(scope->GetSampleRate() == 10000000 && scope->GetSampleDepth() == 100000, "rate 10 MS/s, depth 100k");

	int64_t capture = 100000 * (FS_PER_SECOND / 10000000);
	scope->SetTriggerOffset(capture / 2);
	CHECK(scope->GetTriggerOffset() == capture / 2, "trigger offset at mid capture");

	//Continuous acquisition
	scope->Start();
	CHECK(scope->IsTriggerArmed(), "armed after Start()");
	int got = 0;
	for(int i=0; i<5; i++)
		got += AcquireOne(scope, 5000) ? 1 : 0;
	scope->Stop();
	CHECK(got == 5, "5 acquisitions (%d)", got);
	CHECK(scope->GetPendingWaveformCount() >= 1, "%zu waveform sets pending", scope->GetPendingWaveformCount());

	//Pop the newest set and inspect it, as the UI would
	while(scope->GetPendingWaveformCount() > 1)
		scope->PopPendingWaveform();
	scope->PopPendingWaveform();

	bool shapes = true;
	size_t checked = 0;
	for(size_t i=0; i<scope->GetChannelCount(); i++)
	{
		auto chan = scope->GetOscilloscopeChannel(i);
		auto data = chan->GetData(0);
		if(!data)
			continue;
		checked++;
		if(digital)
		{
			auto w = dynamic_cast<SparseDigitalWaveform*>(data);
			if(!w || w->size() == 0)
			{
				shapes = false;
				continue;
			}
			w->PrepareForCpuAccess();
			int64_t span = w->m_offsets[w->size()-1] + w->m_durations[w->size()-1] - w->m_offsets[0];
			shapes &= (w->m_timescale == FS_PER_SECOND / 10000000);
			shapes &= (span > 90000 && span <= 100000);
			for(size_t k=1; k<w->size(); k++)
				shapes &= (w->m_offsets[k] == w->m_offsets[k-1] + w->m_durations[k-1]);
		}
		else
		{
			auto w = dynamic_cast<UniformAnalogWaveform*>(data);
			shapes &= (w && w->size() > 0);
		}
	}
	CHECK(checked == scope->GetChannelCount() && shapes,
		"%zu channel waveforms, contiguous, spanning the capture at 100 ns/sample", checked);

	//Single shot
	scope->StartSingleTrigger();
	bool single = AcquireOne(scope, 5000);
	this_thread::sleep_for(chrono::milliseconds(300));
	CHECK(single && !scope->IsTriggerArmed(), "single acquisition, then disarmed");

	//Channel enable/disable through the scope API
	scope->DisableChannel(1);
	CHECK(!scope->IsChannelEnabled(1), "channel 1 disabled");
	scope->EnableChannel(1);
	CHECK(scope->IsChannelEnabled(1), "channel 1 enabled again");

	scope = nullptr;
	ScopehalStaticCleanup();

	printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
	return g_failures ? 1 : 0;
}
