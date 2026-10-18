/***********************************************************************************************************************
*                                                                                                                      *
* libsigrok4DSL plugin for ngscopeclient                                                                               *
*                                                                                                                      *
* Licensed under the GNU GPL v3 or later (libsigrok4DSL is GPLv3).                                                     *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@brief Implementation of LibSigrok4DSLOscilloscope
 */

#include "LibSigrok4DSLOscilloscope.h"
#include "DSLSession.h"
#include "PackedBits.h"

#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

LibSigrok4DSLOscilloscope::LibSigrok4DSLOscilloscope(SCPITransport* transport)
	: SCPIDevice(transport, false)
	, SCPIInstrument(transport, false)
	, m_session(DSLSession::Get())
	, m_connected(false)
	, m_dso(false)
	, m_requestedMode(-1)
	, m_maxDepth(0)
	, m_triggerOffset(0)
	, m_threshold(0)
	, m_hasThreshold(false)
{
	m_vendor = "libsigrok4DSL";
	m_model = "(no device)";
	m_serial = "";

	ParseConnectionString(transport->GetConnectionString());

	if(!m_session.InitLibrary(m_resDir))
		return;

	string err;
	if(!m_session.Open(m_selector, m_requestedMode, err))
	{
		LogError("libsigrok4DSL: %s\n", err.c_str());
		return;
	}
	m_connected = true;
	m_dso = m_session.IsDSO();
	if(m_maxDepth)
		m_session.SetMaxDepth(m_maxDepth);

	m_model = m_session.GetDeviceName();
	m_serial = m_session.GetDriverName();

	//Create channels. Channel index = libsigrok4DSL channel index.
	for(size_t i=0; i<m_session.GetChannelCount(); i++)
	{
		string hwname = to_string(i);
		auto chan = new OscilloscopeChannel(
			this,
			hwname,
			GetDefaultChannelColor(i),
			Unit(Unit::UNIT_FS),
			m_dso ? Unit(Unit::UNIT_VOLTS) : Unit(Unit::UNIT_COUNTS),
			m_dso ? Stream::STREAM_TYPE_ANALOG : Stream::STREAM_TYPE_DIGITAL,
			i);
		m_channels.push_back(chan);

		//Device channel names are usually just numbers; prefix them so they read as channels
		string name = m_session.GetChannelName(i);
		bool numeric = !name.empty() && all_of(name.begin(), name.end(), [](unsigned char c){ return isdigit(c); });
		if(numeric)
			name = (m_dso ? "CH" : "D") + name;
		chan->SetDisplayName(name);

		m_channelAttenuations[i] = 1;
	}

	if(!m_dso)
	{
		m_hasThreshold = m_session.HasThreshold();
		if(m_hasThreshold)
			m_threshold = m_session.GetThreshold();
	}

	//Start with a modest capture so the first acquisition is quick
	auto depths = GetSampleDepthsNonInterleaved();
	uint64_t depth = 100000;
	if(!depths.empty() && depths.back() < depth)
		depth = depths.back();
	SetSampleDepth(depth);

	//Default trigger: rising edge on the first channel, 10% pre-trigger
	auto trig = new EdgeTrigger(this);
	trig->SetType(EdgeTrigger::EDGE_RISING);
	trig->SetLevel(0);
	trig->SetInput(0, StreamDescriptor(GetOscilloscopeChannel(0)));
	SetTrigger(trig);
	m_triggerOffset = GetSampleDepth() * (FS_PER_SECOND / max<uint64_t>(1, GetSampleRate())) / 10;
	PushTrigger();
}

LibSigrok4DSLOscilloscope::~LibSigrok4DSLOscilloscope()
{
	if(m_connected)
		m_session.Close();
}

/**
	@brief Parses "<device>[,res=DIR][,mode=logic|dso][,maxdepth=N]"
 */
void LibSigrok4DSLOscilloscope::ParseConnectionString(const string& path)
{
	vector<string> parts;
	string cur;
	for(char c : path)
	{
		if(c == ',')
		{
			parts.push_back(cur);
			cur.clear();
		}
		else
			cur += c;
	}
	parts.push_back(cur);

	auto trim = [](string s)
	{
		while(!s.empty() && isspace((unsigned char)s.front()))
			s.erase(s.begin());
		while(!s.empty() && isspace((unsigned char)s.back()))
			s.pop_back();
		return s;
	};

	m_selector = trim(parts[0]);
	for(size_t i=1; i<parts.size(); i++)
	{
		string p = trim(parts[i]);
		auto eq = p.find('=');
		if(eq == string::npos)
			continue;
		string key = p.substr(0, eq);
		string val = p.substr(eq + 1);
		if(key == "res")
			m_resDir = val;
		else if(key == "mode")
			m_requestedMode = (val == "dso") ? 1 : (val == "logic") ? 0 : -1;	//libsigrok4DSL LOGIC=0, DSO=1
		else if(key == "maxdepth")
			m_maxDepth = stoull(val);
		else
			LogWarning("libsigrok4DSL: unknown option \"%s\"\n", key.c_str());
	}

	//Firmware / bitstream directory: explicit, then environment, then the usual install locations
	if(m_resDir.empty())
	{
		vector<string> candidates;
		if(auto env = getenv("DSVIEW_RES_DIR"))
			candidates.push_back(env);
#ifdef _WIN32
		//Windows packages ship the firmware as "res" next to ngscopeclient.exe
		char exe[MAX_PATH];
		DWORD len = GetModuleFileNameA(nullptr, exe, sizeof(exe));
		if(len > 0 && len < sizeof(exe))
		{
			string dir(exe, len);
			auto slash = dir.find_last_of("\\/");
			if(slash != string::npos)
				candidates.push_back(dir.substr(0, slash) + "\\res");
		}
		if(auto appdata = getenv("LOCALAPPDATA"))
			candidates.push_back(string(appdata) + "\\DSView\\res");
#endif
		if(auto home = getenv("HOME"))
			candidates.push_back(string(home) + "/.local/share/DSView/res");
		candidates.push_back("/usr/local/share/DSView/res");
		candidates.push_back("/usr/share/DSView/res");
		candidates.push_back("/usr/local/share/ALL-LOGIC/res");
		candidates.push_back("/usr/share/ALL-LOGIC/res");
#ifdef DSL_SOURCE_RES_DIR
		candidates.push_back(DSL_SOURCE_RES_DIR);
#endif
		for(auto& c : candidates)
		{
			struct stat st;
			if(stat(c.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
			{
				m_resDir = c;
				break;
			}
		}
		if(m_resDir.empty())
			LogWarning("libsigrok4DSL: no firmware directory found; devices that need firmware will fail. "
				"Add \",res=<dir>\" to the path or set DSVIEW_RES_DIR.\n");
	}
}

string LibSigrok4DSLOscilloscope::GetDriverNameInternal()
{
	return "libsigrok4dsl";
}

string LibSigrok4DSLOscilloscope::IDPing()
{
	return "";
}

unsigned int LibSigrok4DSLOscilloscope::GetInstrumentTypes() const
{
	return Instrument::INST_OSCILLOSCOPE;
}

uint32_t LibSigrok4DSLOscilloscope::GetInstrumentTypesForChannel(size_t /*i*/) const
{
	return Instrument::INST_OSCILLOSCOPE;
}

void LibSigrok4DSLOscilloscope::FlushConfigCache()
{
	//Nothing cached that the device could change behind our back
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channel configuration

bool LibSigrok4DSLOscilloscope::IsChannelEnabled(size_t i)
{
	return m_connected && m_session.IsChannelEnabled(i);
}

void LibSigrok4DSLOscilloscope::EnableChannel(size_t i)
{
	if(m_connected)
		m_session.SetChannelEnabled(i, true);
}

void LibSigrok4DSLOscilloscope::DisableChannel(size_t i)
{
	if(m_connected && !m_session.SetChannelEnabled(i, false))
		LogDebug("libsigrok4DSL: channel %zu stays enabled (last enabled channel)\n", i);
}

bool LibSigrok4DSLOscilloscope::CanEnableChannel(size_t i)
{
	return m_connected && i < m_channels.size();
}

OscilloscopeChannel::CouplingType LibSigrok4DSLOscilloscope::GetChannelCoupling(size_t i)
{
	if(!m_dso)
		return OscilloscopeChannel::COUPLE_SYNTHETIC;
	return m_session.GetCouplingAC(i) ? OscilloscopeChannel::COUPLE_AC_1M : OscilloscopeChannel::COUPLE_DC_1M;
}

void LibSigrok4DSLOscilloscope::SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type)
{
	if(!m_dso)
		return;
	if(type == OscilloscopeChannel::COUPLE_AC_1M)
		m_session.SetCouplingAC(i, true);
	else if(type == OscilloscopeChannel::COUPLE_DC_1M)
		m_session.SetCouplingAC(i, false);
}

vector<OscilloscopeChannel::CouplingType> LibSigrok4DSLOscilloscope::GetAvailableCouplings(size_t /*i*/)
{
	vector<OscilloscopeChannel::CouplingType> ret;
	if(m_dso)
	{
		ret.push_back(OscilloscopeChannel::COUPLE_DC_1M);
		ret.push_back(OscilloscopeChannel::COUPLE_AC_1M);
	}
	return ret;
}

double LibSigrok4DSLOscilloscope::GetChannelAttenuation(size_t i)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	return m_channelAttenuations[i];
}

void LibSigrok4DSLOscilloscope::SetChannelAttenuation(size_t i, double atten)
{
	lock_guard<recursive_mutex> lock(m_cacheMutex);
	if(atten > 0)
		m_channelAttenuations[i] = atten;
}

unsigned int LibSigrok4DSLOscilloscope::GetChannelBandwidthLimit(size_t /*i*/)
{
	return 0;
}

void LibSigrok4DSLOscilloscope::SetChannelBandwidthLimit(size_t /*i*/, unsigned int /*limit_mhz*/)
{
}

float LibSigrok4DSLOscilloscope::GetChannelVoltageRange(size_t i, size_t /*stream*/)
{
	if(!m_dso)
		return 1;
	return m_session.GetVdiv(i) / 1000.0 * m_session.GetNumDivs() * GetChannelAttenuation(i);
}

void LibSigrok4DSLOscilloscope::SetChannelVoltageRange(size_t i, size_t /*stream*/, float range)
{
	if(!m_dso)
		return;

	//Smallest V/div whose full screen covers the requested range
	auto opts = m_session.GetVdivOptions();
	if(opts.empty())
		return;
	double want_mV = range / GetChannelAttenuation(i) / m_session.GetNumDivs() * 1000;
	uint64_t pick = *max_element(opts.begin(), opts.end());
	for(auto v : opts)
	{
		if(v >= want_mV && v < pick)
			pick = v;
	}
	m_session.SetVdiv(i, pick);
}

OscilloscopeChannel* LibSigrok4DSLOscilloscope::GetExternalTrigger()
{
	return nullptr;
}

float LibSigrok4DSLOscilloscope::GetChannelOffset(size_t /*i*/, size_t /*stream*/)
{
	return 0;
}

void LibSigrok4DSLOscilloscope::SetChannelOffset(size_t /*i*/, size_t /*stream*/, float /*offset*/)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Triggering

/**
	@brief Reports TRIGGERED when a capture is waiting

	ngscopeclient polls this in a loop while armed, so wait briefly for a capture instead of
	spinning.
 */
Oscilloscope::TriggerMode LibSigrok4DSLOscilloscope::PollTrigger()
{
	if(!m_connected)
		return TRIGGER_MODE_STOP;

	if(m_session.WaitForCapture(m_session.IsArmed() ? 20 : 0))
		return TRIGGER_MODE_TRIGGERED;

	return m_session.IsArmed() ? TRIGGER_MODE_RUN : TRIGGER_MODE_STOP;
}

bool LibSigrok4DSLOscilloscope::AcquireData()
{
	auto cap = m_session.PopCapture();
	if(!cap)
		return false;

	ChannelsDownloadStarted();

	int64_t fs_per_sample = FS_PER_SECOND / max<uint64_t>(1, cap->rate);

	double now = GetTime();
	time_t start = now;
	int64_t startFs = (now - floor(now)) * FS_PER_SECOND;

	SequenceSet s;

	if(!cap->dso)
	{
		//Shift samples so the reported trigger lands on the configured trigger offset. Drivers
		//that send no trigger position already put it at the configured percentage.
		int64_t shift = 0;
		if(cap->triggered)
			shift = m_triggerOffset / fs_per_sample - (int64_t)cap->trigSample;

		for(size_t i=0; i<cap->chans.size(); i++)
		{
			size_t chnum = cap->chans[i];
			auto chan = GetOscilloscopeChannel(chnum);
			ChannelsDownloadStatusUpdate(chnum, InstrumentChannel::DownloadState::DOWNLOAD_IN_PROGRESS, 0.0);

			auto& buf = cap->data[i];
			size_t runs = CountPackedBitRuns(buf.data(), buf.size());

			auto wfm = AllocateDigitalWaveform(m_nickname + "." + chan->GetHwname());
			wfm->m_timescale = fs_per_sample;
			wfm->m_triggerPhase = 0;
			wfm->m_startTimestamp = start;
			wfm->m_startFemtoseconds = startFs;
			wfm->Resize(runs);
			wfm->PrepareForCpuAccess();
			int64_t* offs = wfm->m_offsets.GetCpuPointer();
			int64_t* durs = wfm->m_durations.GetCpuPointer();
			bool* smps = wfm->m_samples.GetCpuPointer();
			size_t n = DecodePackedBits(buf.data(), buf.size(), shift, offs, durs, smps);
			if(n != runs)
				wfm->Resize(n);
			wfm->MarkModifiedFromCpu();

			s[chan] = wfm;
			ChannelsDownloadStatusUpdate(chnum, InstrumentChannel::DownloadState::DOWNLOAD_FINISHED, 1.0);
		}
	}
	else
	{
		for(size_t i=0; i<cap->chans.size(); i++)
		{
			size_t chnum = cap->chans[i];
			auto chan = GetOscilloscopeChannel(chnum);
			double atten = GetChannelAttenuation(chnum);

			auto wfm = AllocateAnalogWaveform(m_nickname + "." + chan->GetHwname());
			wfm->m_timescale = fs_per_sample;
			wfm->m_triggerPhase = -cap->trigPhase * fs_per_sample;
			wfm->m_startTimestamp = start;
			wfm->m_startFemtoseconds = startFs;
			wfm->m_flags = cap->clipping[i] ? WaveformBase::WAVEFORM_CLIPPING : 0;
			wfm->Resize(cap->samples);
			wfm->PrepareForCpuAccess();
			ConvertUnsigned8BitSamples(
				wfm->m_samples.GetCpuPointer(),
				cap->data[i].data(),
				cap->scale[i] * atten,
				cap->offset[i] * atten,
				cap->samples);
			wfm->MarkModifiedFromCpu();

			s[chan] = wfm;
		}
	}

	m_pendingWaveformsMutex.lock();
	m_pendingWaveforms.push_back(s);
	m_pendingWaveformsMutex.unlock();

	ChannelsDownloadFinished();

	m_triggerArmed = m_session.IsArmed();
	return true;
}

void LibSigrok4DSLOscilloscope::Start()
{
	if(!m_connected)
		return;
	m_session.Start(false);
	m_triggerArmed = true;
	m_triggerOneShot = false;
}

void LibSigrok4DSLOscilloscope::StartSingleTrigger()
{
	if(!m_connected)
		return;
	m_session.Start(true);
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

void LibSigrok4DSLOscilloscope::Stop()
{
	if(m_connected)
		m_session.Stop();
	m_triggerArmed = false;
}

void LibSigrok4DSLOscilloscope::ForceTrigger()
{
	//libsigrok4DSL has no software trigger: take one capture with the trigger disabled
	if(!m_connected)
		return;
	m_session.ForceCapture();
	m_triggerArmed = true;
	m_triggerOneShot = true;
}

bool LibSigrok4DSLOscilloscope::IsTriggerArmed()
{
	//A finished single capture still waiting to be read counts as armed so it gets picked up
	return m_connected && (m_session.IsArmed() || m_session.WaitForCapture(0));
}

void LibSigrok4DSLOscilloscope::PushTrigger()
{
	auto et = dynamic_cast<EdgeTrigger*>(m_trigger);
	if(et)
		PushEdgeTrigger(et);
	else if(m_trigger)
		LogWarning("libsigrok4DSL: only edge triggers are supported\n");
	ApplyTriggerPosition();
	ClearPendingWaveforms();
}

void LibSigrok4DSLOscilloscope::PushEdgeTrigger(EdgeTrigger* trig)
{
	if(!m_connected)
		return;

	auto chan = dynamic_cast<OscilloscopeChannel*>(trig->GetInput(0).m_channel);
	if(!chan || chan->GetScope() != this)
		return;

	DSLSession::Edge edge = DSLSession::EDGE_RISING;
	switch(trig->GetType())
	{
		case EdgeTrigger::EDGE_FALLING:
			edge = DSLSession::EDGE_FALLING;
			break;
		case EdgeTrigger::EDGE_ANY:
			edge = DSLSession::EDGE_ANY;
			break;
		default:
			break;
	}
	m_session.SetTrigger(chan->GetIndex(), edge);

	if(m_dso)
		m_session.SetTriggerLevel(trig->GetLevel() / GetChannelAttenuation(chan->GetIndex()));
}

void LibSigrok4DSLOscilloscope::PullTrigger()
{
	//The trigger only lives here; nothing to read back from the device
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timebase

vector<uint64_t> LibSigrok4DSLOscilloscope::GetSampleRatesNonInterleaved()
{
	if(!m_connected)
		return {};
	return m_session.GetSampleRates();
}

vector<uint64_t> LibSigrok4DSLOscilloscope::GetSampleRatesInterleaved()
{
	return GetSampleRatesNonInterleaved();
}

set<Oscilloscope::InterleaveConflict> LibSigrok4DSLOscilloscope::GetInterleaveConflicts()
{
	return {};
}

vector<uint64_t> LibSigrok4DSLOscilloscope::GetSampleDepthsNonInterleaved()
{
	if(!m_connected)
		return {};
	return m_session.GetSampleDepths();
}

vector<uint64_t> LibSigrok4DSLOscilloscope::GetSampleDepthsInterleaved()
{
	return GetSampleDepthsNonInterleaved();
}

uint64_t LibSigrok4DSLOscilloscope::GetSampleRate()
{
	return m_connected ? m_session.GetSampleRate() : 1;
}

uint64_t LibSigrok4DSLOscilloscope::GetSampleDepth()
{
	return m_connected ? m_session.GetSampleDepth() : 1;
}

void LibSigrok4DSLOscilloscope::SetSampleDepth(uint64_t depth)
{
	if(!m_connected)
		return;
	m_session.SetSampleDepth(depth);
	ApplyTriggerPosition();
	ClearPendingWaveforms();
}

void LibSigrok4DSLOscilloscope::SetSampleRate(uint64_t rate)
{
	if(!m_connected)
		return;
	m_session.SetSampleRate(rate);
	ApplyTriggerPosition();
	ClearPendingWaveforms();
}

void LibSigrok4DSLOscilloscope::SetTriggerOffset(int64_t offset)
{
	m_triggerOffset = max<int64_t>(0, offset);
	ApplyTriggerPosition();
}

int64_t LibSigrok4DSLOscilloscope::GetTriggerOffset()
{
	return m_triggerOffset;
}

/**
	@brief Converts the trigger offset (time from capture start) to libsigrok4DSL's pre-trigger
	percentage, and snaps the offset to what the device can actually do
 */
void LibSigrok4DSLOscilloscope::ApplyTriggerPosition()
{
	if(!m_connected)
		return;

	int64_t fs_per_sample = FS_PER_SECOND / max<uint64_t>(1, GetSampleRate());
	int64_t capture = (int64_t)GetSampleDepth() * fs_per_sample;
	if(capture <= 0)
		return;

	int64_t pct = m_triggerOffset * 100 / capture;
	pct = max<int64_t>(0, min<int64_t>(100, pct));
	m_session.SetTriggerPosition((uint8_t)pct);
	m_triggerOffset = capture * pct / 100;
}

bool LibSigrok4DSLOscilloscope::IsInterleaving()
{
	return false;
}

bool LibSigrok4DSLOscilloscope::SetInterleaving(bool /*combine*/)
{
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logic threshold: one setting for the whole device

vector<Oscilloscope::DigitalBank> LibSigrok4DSLOscilloscope::GetDigitalBanks()
{
	vector<DigitalBank> banks;
	if(!m_dso && !m_channels.empty())
		banks.push_back(GetDigitalBank(0));
	return banks;
}

Oscilloscope::DigitalBank LibSigrok4DSLOscilloscope::GetDigitalBank(size_t /*channel*/)
{
	DigitalBank bank;
	if(!m_dso)
	{
		for(size_t i=0; i<m_channels.size(); i++)
			bank.push_back(GetOscilloscopeChannel(i));
	}
	return bank;
}

bool LibSigrok4DSLOscilloscope::IsDigitalHysteresisConfigurable()
{
	return false;
}

bool LibSigrok4DSLOscilloscope::IsDigitalThresholdConfigurable()
{
	return m_hasThreshold;
}

float LibSigrok4DSLOscilloscope::GetDigitalHysteresis(size_t /*channel*/)
{
	return 0;
}

float LibSigrok4DSLOscilloscope::GetDigitalThreshold(size_t /*channel*/)
{
	return m_threshold;
}

void LibSigrok4DSLOscilloscope::SetDigitalHysteresis(size_t /*channel*/, float /*level*/)
{
}

void LibSigrok4DSLOscilloscope::SetDigitalThreshold(size_t /*channel*/, float level)
{
	if(!m_hasThreshold)
		return;
	if(m_session.SetThreshold(level))
		m_threshold = m_session.GetThreshold();
}
