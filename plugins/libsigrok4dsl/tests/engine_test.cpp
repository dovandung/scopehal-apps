/***********************************************************************************************************************
*                                                                                                                      *
* libsigrok4DSL plugin for ngscopeclient                                                                               *
*                                                                                                                      *
* Licensed under the GNU GPL v3 or later (libsigrok4DSL is GPLv3).                                                     *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@brief Tests for the capture engine and the bit decoder, without ngscopeclient

	  libsigrok4dsl-engine-test [device] [--mode logic|dso] [--res DIR]

	Default device "demo" (libsigrok4DSL's virtual-demo, needs no hardware).
 */

#include "../DSLSession.h"
#include "../PackedBits.h"
#include "log/log.h"

#include <libsigrok.h>
#include <glib/gstdio.h>

#include <cstdio>
#include <cstring>
#include <chrono>
#include <random>
#include <thread>
#include <string>
#include <vector>

using namespace std;

static int g_failures = 0;

#define CHECK(cond, ...) do { \
	if(cond) { printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } \
	else { printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); g_failures++; } \
} while(0)

//Reference decoder: expand to one bool per sample
static vector<bool> Expand(const vector<uint8_t>& buf)
{
	vector<bool> out;
	for(size_t i=0; i<buf.size()*8; i++)
		out.push_back((buf[i/8] >> (i%8)) & 1);
	return out;
}

static void TestDecoder()
{
	printf("Packed bit decoder\n");
	mt19937 rng(1);

	for(int trial=0; trial<200; trial++)
	{
		//Mix of random bytes and long constant runs to hit both paths
		size_t n = 1 + rng() % 300;
		vector<uint8_t> buf(n);
		for(auto& b : buf)
		{
			int r = rng() % 4;
			b = (r == 0) ? 0x00 : (r == 1) ? 0xff : (uint8_t)rng();
		}
		int64_t shift = (int64_t)(rng() % 1000) - 500;

		vector<int64_t> off(n*8), dur(n*8);
		vector<bool> smp(n*8);
		size_t count = DecodePackedBits(buf.data(), n, shift, off, dur, smp);
		if(count != CountPackedBitRuns(buf.data(), n))
		{
			CHECK(false, "trial %d: CountPackedBitRuns %zu != decoded %zu", trial, CountPackedBitRuns(buf.data(), n), count);
			return;
		}

		//Rebuild from the sparse form and compare
		auto ref = Expand(buf);
		vector<bool> rebuilt;
		bool ok = (off[0] == shift);
		for(size_t k=0; k<count && ok; k++)
		{
			if(k > 0 && off[k] != off[k-1] + dur[k-1])
				ok = false;
			if(k > 0 && smp[k] == smp[k-1] && k != count-1)
				ok = false;	//adjacent equal entries only allowed for the forced final one
			for(int64_t d=0; d<dur[k]; d++)
				rebuilt.push_back(smp[k]);
		}
		ok = ok && (rebuilt == ref) && (off[count-1] - shift == (int64_t)ref.size() - 1) && dur[count-1] == 1;
		if(!ok)
		{
			CHECK(false, "trial %d (n=%zu) round-trips", trial, n);
			return;
		}
	}
	CHECK(true, "200 random buffers round-trip, runs merged, last sample separate, run count exact");

	vector<uint8_t> zeros(1000, 0);
	vector<int64_t> off(8000), dur(8000);
	vector<bool> smp(8000);
	size_t c = DecodePackedBits(zeros.data(), zeros.size(), 0, off, dur, smp);
	CHECK(c == 2 && dur[0] == 7999 && dur[1] == 1, "constant buffer -> 2 entries (got %zu)", c);
}

static bool WaitCapture(DSLSession& s, shared_ptr<DSLCapture>& c, int timeoutMs = 5000)
{
	if(!s.WaitForCapture(timeoutMs))
		return false;
	c = s.PopCapture();
	return c != nullptr;
}

static void TestLogic(DSLSession& s)
{
	printf("Logic acquisition on \"%s\" (%zu channels)\n", s.GetDeviceName().c_str(), s.GetChannelCount());

	auto rates = s.GetSampleRates();
	CHECK(!rates.empty(), "%zu sample rates, %lu .. %lu Hz", rates.size(),
		rates.empty() ? 0UL : (unsigned long)rates.front(), rates.empty() ? 0UL : (unsigned long)rates.back());
	auto depths = s.GetSampleDepths();
	CHECK(!depths.empty() && depths.back() == s.GetMaxDepth(), "%zu depths up to %lu", depths.size(), (unsigned long)s.GetMaxDepth());

	uint64_t r = s.SetSampleRate(10000000);
	uint64_t d = s.SetSampleDepth(100000);
	CHECK(r == 10000000 && d == 100000, "rate %lu, depth %lu", (unsigned long)r, (unsigned long)d);
	s.SetTrigger(0, DSLSession::EDGE_RISING);
	s.SetTriggerPosition(50);

	s.Start(false);
	shared_ptr<DSLCapture> c;
	int got = 0;
	bool shapeOk = true;
	for(int i=0; i<10; i++)
	{
		if(!WaitCapture(s, c))
			break;
		got++;
		shapeOk &= !c->dso && c->chans.size() == s.GetChannelCount() && c->rate == 10000000;
		for(auto& v : c->data)
			shapeOk &= (v.size() * 8 == c->samples);
		shapeOk &= c->samples >= 99000 && c->samples <= 100000;
	}
	CHECK(got == 10, "10 continuous captures received (%d)", got);
	CHECK(shapeOk, "each has all channels, %lu samples, packed data sized to match", c ? (unsigned long)c->samples : 0UL);

	//Real signal content: demo random pattern has edges
	size_t edges = 0;
	if(c)
	{
		auto bits = Expand(c->data[0]);
		for(size_t i=1; i<bits.size(); i++)
			edges += bits[i] != bits[i-1];
	}
	CHECK(edges > 0, "channel 0 has %zu edges", edges);

	//Reconfigure while running: everything after must use the new settings
	s.SetChannelEnabled(3, false);
	s.SetChannelEnabled(7, false);
	s.SetSampleRate(50000000);
	s.SetSampleDepth(20000);
	bool reconfOk = true;
	got = 0;
	for(int i=0; i<5; i++)
	{
		if(!WaitCapture(s, c))
			break;
		got++;
		reconfOk &= c->chans.size() == s.GetChannelCount() - 2 && c->rate == 50000000 && c->samples <= 20000;
		for(auto ch : c->chans)
			reconfOk &= (ch != 3 && ch != 7);
	}
	CHECK(got == 5 && reconfOk, "after reconfiguring: no stale captures, %zu channels at 50 MHz", c ? c->chans.size() : 0);

	CHECK(!s.SetChannelEnabled(99, true), "out-of-range channel rejected");

	//Stop, then single shot
	s.Stop();
	CHECK(!s.IsArmed(), "stopped");
	s.SetChannelEnabled(3, true);
	s.SetChannelEnabled(7, true);
	while(s.PopCapture()) {}
	s.Start(true);
	bool single = WaitCapture(s, c);
	CHECK(single && c->chans.size() == s.GetChannelCount(), "single capture with all channels back");
	this_thread::sleep_for(chrono::milliseconds(300));
	CHECK(!s.IsArmed() && s.PopCapture() == nullptr, "disarmed after single, nothing more queued");

	//Force: one capture with the trigger off, then the trigger is back
	s.SetTrigger(0, DSLSession::EDGE_FALLING);
	CHECK(ds_trigger_get_en() == 1, "edge trigger enabled before force");
	while(s.PopCapture()) {}
	s.ForceCapture();
	bool forced = WaitCapture(s, c);
	this_thread::sleep_for(chrono::milliseconds(300));
	CHECK(forced && !s.IsArmed() && ds_trigger_get_en() == 1, "forced capture taken, disarmed, trigger re-enabled");

	//Throughput and thread hygiene
	s.SetSampleDepth(10000);
	s.Start(false);
	auto t0 = chrono::steady_clock::now();
	got = 0;
	while(chrono::steady_clock::now() - t0 < chrono::seconds(3))
	{
		if(WaitCapture(s, c, 1000))
			got++;
	}
	double rate = s.GetCaptureRate();
	s.Stop();
	CHECK(got > 20, "%d captures in 3 s, device rate %.1f/s, %lu dropped by slow consumer", got, rate,
		(unsigned long)s.GetDroppedCount());

	int threads = 0;
	FILE* f = fopen("/proc/self/status", "r");
	char line[256];
	while(f && fgets(line, sizeof(line), f))
	{
		if(!strncmp(line, "Threads:", 8))
			threads = atoi(line + 8);
	}
	if(f)
		fclose(f);
	CHECK(threads <= 5, "%d threads after %d captures (collect threads are joined)", threads, got);
}

static void TestDSO(DSLSession& s)
{
	printf("DSO acquisition on \"%s\" (%zu channels)\n", s.GetDeviceName().c_str(), s.GetChannelCount());

	auto vdivs = s.GetVdivOptions();
	CHECK(!vdivs.empty(), "%zu V/div settings", vdivs.size());

	s.SetSampleRate(100000000);
	s.SetTrigger(0, DSLSession::EDGE_RISING);
	s.SetTriggerPosition(50);
	s.Start(false);

	shared_ptr<DSLCapture> c;
	int got = 0;
	bool ok = true;
	for(int i=0; i<5; i++)
	{
		if(!WaitCapture(s, c))
			break;
		got++;
		ok &= c->dso && c->chans.size() == s.GetChannelCount() && c->scale.size() == c->chans.size();
		for(auto& v : c->data)
			ok &= v.size() == c->samples;
	}
	CHECK(got == 5 && ok, "5 frames, %lu samples each", c ? (unsigned long)c->samples : 0UL);

	if(c)
	{
		float vmin = 1e9, vmax = -1e9;
		for(auto code : c->data[0])
		{
			float v = code * c->scale[0] - c->offset[0];
			vmin = min(vmin, v);
			vmax = max(vmax, v);
		}
		float range = s.GetVdiv(0) / 1000.0 * s.GetNumDivs();
		CHECK(vmax > vmin && vmax - vmin <= range * 1.01, "ch0 %.3f .. %.3f V within the %.2f V screen", vmin, vmax, range);
	}

	//V/div change takes effect in scaling
	if(vdivs.size() >= 2)
	{
		s.SetVdiv(0, vdivs[0]);
		while(s.PopCapture()) {}
		WaitCapture(s, c);
		float sc1 = c->scale[0];
		s.SetVdiv(0, vdivs[1]);
		this_thread::sleep_for(chrono::milliseconds(100));
		while(s.PopCapture()) {}
		WaitCapture(s, c);
		CHECK(c->scale[0] != sc1, "scale follows V/div (%g -> %g)", sc1, c->scale[0]);
	}

	s.Stop();
	while(s.PopCapture()) {}
	s.Start(true);
	bool single = WaitCapture(s, c);
	this_thread::sleep_for(chrono::milliseconds(300));
	CHECK(single && !s.IsArmed() && s.PopCapture() == nullptr, "single frame, then disarmed");
}

static void TestChannelModes(DSLSession& s, const string& selector, int mode)
{
	printf("Channel modes on \"%s\"\n", selector.c_str());
	string err;

	//List
	CHECK(s.Open(selector, mode, err), "open");
	auto modes = s.GetChannelModes();
	for(auto& m : modes)
		printf("       %d: %s  -> %d channels\n", m.id, m.name.c_str(), m.channels);
	bool parsed = !modes.empty();
	for(auto& m : modes)
		parsed &= m.channels > 0;
	CHECK(parsed, "%zu modes, channel count parsed from every name", modes.size());
	size_t defaultChannels = s.GetChannelCount();
	s.Close();
	if(modes.size() < 2)
		return;

	//channels=N picks the mode with that many channels, before the channel list is read
	auto& small = modes.back();
	DSLSession::ChannelModeRequest req;
	req.channels = small.channels;
	CHECK(s.Open(selector, mode, err, req), "open with channels=%d", small.channels);
	CHECK(s.GetChannelMode() == small.id && s.GetChannelCount() == (size_t)small.channels,
		"mode \"%s\", %zu channels", s.GetChannelModeName().c_str(), s.GetChannelCount());
	s.SetSampleDepth(10000);
	s.Start(true);
	shared_ptr<DSLCapture> c;
	bool got = WaitCapture(s, c);
	CHECK(got && c->chans.size() == (size_t)small.channels, "capture has %zu channels", got ? c->chans.size() : 0);
	s.Close();

	//A count the device does not offer: warning, still opens in a valid mode
	req.channels = 99;
	bool opened = s.Open(selector, mode, err, req);
	bool valid = false;
	for(auto& m : modes)
		valid |= opened && m.id == s.GetChannelMode() && (size_t)m.channels == s.GetChannelCount();
	CHECK(valid, "channels=99 rejected with a warning, device still opens (\"%s\")", s.GetChannelModeName().c_str());
	s.Close();

	//chmode=ID selects by raw id: back to the first mode
	DSLSession::ChannelModeRequest byId;
	byId.id = modes.front().id;
	CHECK(s.Open(selector, mode, err, byId), "open with chmode=%d", byId.id);
	CHECK(s.GetChannelMode() == modes.front().id && s.GetChannelCount() == defaultChannels,
		"mode \"%s\", %zu channels", s.GetChannelModeName().c_str(), s.GetChannelCount());
	CHECK(!s.IsReconnectNeeded(), "no reconnect needed (demo switches in place)");
	s.Close();
}

/**
	@brief fx3,channels=N is written where the FX3 driver reads it at scan time
 */
static void TestFx3Preset()
{
	printf("FX3 channel mode preset\n");
	gchar* ini = g_build_filename(g_get_user_config_dir(), "AllLogic", "cypress-fx3.ini", NULL);
	gchar* saved = NULL;
	gsize savedLen = 0;
	bool had = g_file_get_contents(ini, &saved, &savedLen, NULL);

	auto modeInFile = [&]()
	{
		GKeyFile* kf = g_key_file_new();
		int v = -1;
		if(g_key_file_load_from_file(kf, ini, G_KEY_FILE_NONE, NULL))
		{
			GError* e = NULL;
			v = g_key_file_get_integer(kf, "cypress-fx3", "channel_mode", &e);
			if(e)
			{
				v = -1;
				g_clear_error(&e);
			}
		}
		g_key_file_free(kf);
		return v;
	};

	DSLSession::ChannelModeRequest r;
	r.channels = 16;
	DSLSession::PresetChannelMode("fx3", r);
	CHECK(modeInFile() == 1, "fx3,channels=16 -> channel_mode=1");
	r.channels = 8;
	DSLSession::PresetChannelMode("Cypress-FX3", r);
	CHECK(modeInFile() == 0, "cypress-fx3,channels=8 -> channel_mode=0");
	r.channels = 16;
	DSLSession::PresetChannelMode("slogic", r);
	CHECK(modeInFile() == 0, "other devices leave it alone");
	r.channels = -1;
	r.id = 1;
	DSLSession::PresetChannelMode("fx3", r);
	CHECK(modeInFile() == 1, "fx3,chmode=1 -> channel_mode=1");

	//Leave the user's file as it was
	if(had)
		g_file_set_contents(ini, saved, savedLen, NULL);
	else
		g_remove(ini);
	g_free(saved);
	g_free(ini);
}

int main(int argc, char* argv[])
{
	string selector = "demo";
	string res = "/usr/local/share/DSView/res";
	int mode = -1;
	for(int i=1; i<argc; i++)
	{
		string a = argv[i];
		if(a == "--mode" && i+1 < argc)
			mode = string(argv[++i]) == "dso" ? DSO : LOGIC;
		else if(a == "--res" && i+1 < argc)
			res = argv[++i];
		else
			selector = a;
	}

	g_log_sinks.emplace(g_log_sinks.begin(), new ColoredSTDLogSink(Severity::NOTICE));

	TestDecoder();
	TestFx3Preset();

	auto& s = DSLSession::Get();
	if(!s.InitLibrary(res, 1))
		return 1;

	printf("Devices:\n");
	for(auto& d : s.ListDevices())
		printf("  %zu: %s\n", d.index, d.name.c_str());

	string err;
	if(!s.Open(selector, mode, err))
	{
		printf("open failed: %s\n", err.c_str());
		return 1;
	}

	if(s.IsDSO())
		TestDSO(s);
	else
		TestLogic(s);

	s.Close();
	string err2;
	CHECK(s.Open(selector, mode, err2), "reopen after close");
	bool dso = s.IsDSO();
	s.Close();

	if(!dso)
		TestChannelModes(s, selector, mode);

	//op=buffer / op=stream: applied when the device has the setting, a warning otherwise
	if(!dso)
	{
		printf("Operation mode request\n");
		s.RequestOperationMode(DSLSession::OP_BUFFER);
		string err3;
		CHECK(s.Open(selector, mode, err3), "open with op=buffer");
		s.Close();
		CHECK(s.Open(selector, mode, err3), "request is not carried over to the next open");
		s.Close();
	}

	printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
	return g_failures ? 1 : 0;
}
