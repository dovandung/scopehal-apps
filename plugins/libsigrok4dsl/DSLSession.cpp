/***********************************************************************************************************************
*                                                                                                                      *
* libsigrok4DSL plugin for ngscopeclient                                                                               *
*                                                                                                                      *
* Licensed under the GNU GPL v3 or later (libsigrok4DSL is GPLv3).                                                     *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@brief Implementation of DSLSession

	How libsigrok4DSL delivers a capture: ds_start_collect() starts a library thread that sends
	SR_DF_HEADER (not every driver does), any number of SR_DF_LOGIC chunks in LA_CROSS_DATA
	layout, an optional SR_DF_TRIGGER between them, then SR_DF_END, and the thread ends.
	DSCope in DSO mode instead keeps one collect running and sends each frame as one SR_DF_DSO.
 */

#include "DSLSession.h"

#include <libsigrok.h>
#include "log/log.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GVariant helpers

namespace
{

template<typename T> bool FromVariant(GVariant* v, T& out);
template<typename T> GVariant* ToVariant(T v);

#define VARIANT_TYPE(CTYPE, GTYPE, GFUNC) \
	template<> inline bool FromVariant<CTYPE>(GVariant* v, CTYPE& out) \
	{ \
		if(!v) return false; \
		bool ok = g_variant_is_of_type(v, G_VARIANT_TYPE_##GTYPE); \
		if(ok) out = g_variant_get_##GFUNC(v); \
		g_variant_unref(v); \
		return ok; \
	} \
	template<> inline GVariant* ToVariant<CTYPE>(CTYPE v) \
	{ return g_variant_new_##GFUNC(v); }

VARIANT_TYPE(uint64_t, UINT64, uint64)
VARIANT_TYPE(uint32_t, UINT32, uint32)
VARIANT_TYPE(int16_t, INT16, int16)
VARIANT_TYPE(uint8_t, BYTE, byte)
VARIANT_TYPE(bool, BOOLEAN, boolean)
VARIANT_TYPE(double, DOUBLE, double)

template<> inline bool FromVariant<string>(GVariant* v, string& out)
{
	if(!v) return false;
	bool ok = g_variant_is_of_type(v, G_VARIANT_TYPE_STRING);
	if(ok) out = g_variant_get_string(v, nullptr);
	g_variant_unref(v);
	return ok;
}
template<> inline GVariant* ToVariant<string>(string v)
{ return g_variant_new_string(v.c_str()); }

template<typename T>
optional<T> GetConfig(void* ch, int key)
{
	GVariant* v = nullptr;
	if(ds_get_actived_device_config((const sr_channel*)ch, nullptr, key, &v) != SR_OK)
		return nullopt;
	T out;
	if(FromVariant<T>(v, out))
		return out;
	return nullopt;
}

template<typename T>
bool SetConfig(void* ch, int key, T value)
{
	GVariant* v = g_variant_ref_sink(ToVariant<T>(value));	//library sinks and drops its own ref
	int err = ds_set_actived_device_config((const sr_channel*)ch, nullptr, key, v);
	g_variant_unref(v);
	return err == SR_OK;
}

//List keys; some return a dictionary with the values under a name
template<typename T>
vector<T> GetConfigList(int key, const char* dictKey)
{
	vector<T> ret;
	GVariant* list = nullptr;
	if(ds_get_actived_device_config_list(nullptr, key, &list) != SR_OK || !list)
		return ret;

	GVariant* outer = nullptr;
	if(dictKey && g_variant_is_of_type(list, G_VARIANT_TYPE_DICTIONARY))
	{
		outer = list;
		list = g_variant_lookup_value(outer, dictKey, nullptr);
		if(!list)
		{
			g_variant_unref(outer);
			return ret;
		}
	}

	if(g_variant_is_container(list))
	{
		GVariantIter it;
		g_variant_iter_init(&it, list);
		GVariant* item;
		while((item = g_variant_iter_next_value(&it)) != nullptr)
		{
			T v;
			if(FromVariant<T>(item, v))
				ret.push_back(v);
		}
	}

	g_variant_unref(list);
	if(outer)
		g_variant_unref(outer);
	return ret;
}

string Lower(string s)
{
	transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return tolower(c); });
	return s;
}

bool IsNumber(const string& s)
{
	return !s.empty() && all_of(s.begin(), s.end(), [](unsigned char c){ return isdigit(c); });
}

//C trampolines
void EventTrampoline(int event)
{
	DSLSession::Get().OnEvent(event);
}

void DatafeedTrampoline(const sr_dev_inst* /*sdi*/, const sr_datafeed_packet* packet)
{
	DSLSession::Get().OnDatafeed(packet);
}

double NowSeconds()
{
	return chrono::duration<double>(chrono::steady_clock::now().time_since_epoch()).count();
}

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

DSLSession& DSLSession::Get()
{
	static DSLSession session;
	return session;
}

DSLSession::DSLSession()
{
}

DSLSession::~DSLSession()
{
	Close();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Library and device lifetime

/**
	@brief Initializes libsigrok4DSL once per process. Later calls do nothing.

	The library is never shut down: ds_lib_exit() followed by ds_lib_init() is not something
	DSView does, and the hotplug thread it starts is cheap.
 */
bool DSLSession::InitLibrary(const string& resourceDir, int logLevel)
{
	if(m_libInit)
		return true;

	string res = resourceDir;
	if(!res.empty() && res.back() != '/')
		res += '/';

	ds_log_level(logLevel);
	if(!res.empty())
		ds_set_firmware_resource_dir(res.c_str());
	ds_set_user_data_dir(g_get_tmp_dir());
	ds_set_event_callback(EventTrampoline);
	ds_set_datafeed_callback(DatafeedTrampoline);

	int err = ds_lib_init();
	if(err != SR_OK && err != SR_ERR_HAVE_DONE)
	{
		LogError("libsigrok4DSL: ds_lib_init failed (%d)\n", err);
		return false;
	}

	LogDebug("libsigrok4DSL %s initialized, firmware in %s\n", sr_get_lib_version_string(), res.c_str());
	m_libInit = true;
	return true;
}

vector<DSLSession::DeviceEntry> DSLSession::ListDevices()
{
	vector<DeviceEntry> ret;
	ds_device_base_info* list = nullptr;
	int count = 0;
	if(ds_get_device_list(&list, &count) != SR_OK || !list)
		return ret;
	for(int i=0; i<count; i++)
		ret.push_back({(size_t)i, list[i].name});
	g_free(list);
	return ret;
}

/**
	@brief Opens a device

	@param selector		Device index, part of the device name, or a driver name. Empty: the first
						hardware device, else the demo device.
	@param requestedMode	-1 to keep the device's mode, else LOGIC or DSO
	@param error		Filled in on failure
 */
bool DSLSession::Open(const string& selector, int requestedMode, string& error)
{
	lock_guard<recursive_mutex> lock(m_devMutex);

	if(m_open)
	{
		error = "a libsigrok4DSL device is already open in this process";
		return false;
	}

	ds_device_base_info* list = nullptr;
	int count = 0;
	if(ds_get_device_list(&list, &count) != SR_OK || !list || count == 0)
	{
		g_free(list);
		error = "libsigrok4DSL found no devices";
		return false;
	}
	vector<ds_device_base_info> devs(list, list + count);
	g_free(list);

	ds_device_full_info info;
	auto activate = [&](size_t i)
	{
		if(ds_active_device(devs[i].handle) != SR_OK)
			return false;
		if(ds_get_actived_device_info(&info) != SR_OK)
			return false;
		//ds_active_device() silently falls back to another device if opening fails
		return info.handle == devs[i].handle;
	};

	bool found = false;
	string want = Lower(selector);
	if(want.empty())
	{
		//First real device, demo as a last resort
		for(size_t i=0; !found && i<devs.size(); i++)
			found = activate(i) && info.dev_type == DEV_TYPE_USB;
		for(size_t i=0; !found && i<devs.size(); i++)
			found = activate(i);
	}
	else
	{
		if(IsNumber(want))
		{
			size_t idx = stoul(want);
			if(idx < devs.size())
				found = activate(idx);
		}
		for(size_t i=0; !found && i<devs.size(); i++)
		{
			if(Lower(devs[i].name).find(want) != string::npos)
				found = activate(i);
		}
		for(size_t i=0; !found && i<devs.size(); i++)
			found = activate(i) && Lower(info.driver_name) == want;
	}

	if(!found)
	{
		error = "no device matches \"" + selector + "\"; found:";
		for(auto& d : devs)
			error += string(" \"") + d.name + "\"";
		return false;
	}

	m_devName = info.name;
	m_driverName = info.driver_name;

	//Some drivers finish initializing in the background
	int status = 0;
	for(int i=0; i<200 && ds_get_actived_device_init_status(&status) == SR_OK && status == SR_ST_INITIALIZING; i++)
		g_usleep(10000);

	if(requestedMode >= 0)
	{
		if(!SetConfig<int16_t>(nullptr, SR_CONF_DEVICE_MODE, (int16_t)requestedMode))
			LogWarning("libsigrok4DSL: device refused mode %d\n", requestedMode);
	}

	int mode = ds_get_actived_device_mode();
	if(mode == LOGIC)
		m_isDSO = false;
	else if(mode == DSO)
		m_isDSO = true;
	else
	{
		error = "device mode " + to_string(mode) + " is not supported (logic analyzer and DSO only)";
		ds_release_actived_device();
		return false;
	}

	m_channels.clear();
	for(GSList* l = ds_get_actived_device_channels(); l; l = l->next)
		m_channels.push_back(l->data);

	auto hwdepth = GetConfig<uint64_t>(nullptr, SR_CONF_HW_DEPTH);
	if(hwdepth && *hwdepth > 0 && *hwdepth < m_maxDepth)
		m_maxDepth = *hwdepth;

	if(m_isDSO)
	{
		m_vdivOptions = GetConfigList<uint64_t>(SR_CONF_PROBE_VDIV, "vdivs");
		m_numDivs = DS_CONF_DSO_VDIVS;
		m_hwMin = GetConfig<uint32_t>(nullptr, SR_CONF_REF_MIN).value_or(0);
		m_hwMax = GetConfig<uint32_t>(nullptr, SR_CONF_REF_MAX).value_or(255);
		if(m_hwMax <= m_hwMin)
		{
			m_hwMin = 0;
			m_hwMax = 255;
		}
		m_hwRangeFactor = 255.f / (m_hwMax - m_hwMin);
	}
	else
	{
		//DSLogic needs buffer mode for a pre-trigger buffer; other drivers ignore the key
		if(m_driverName.rfind("DSLogic", 0) == 0)
			SetConfig<string>(nullptr, SR_CONF_OPERATION_MODE, "Buffer Mode");
		ds_trigger_set_mode(SIMPLE_TRIGGER);
	}

	m_rate = GetConfig<uint64_t>(nullptr, SR_CONF_SAMPLERATE).value_or(1000000);
	m_depth = GetConfig<uint64_t>(nullptr, SR_CONF_LIMIT_SAMPLES).value_or(10000);
	m_trigChannel = 0;
	m_trigEdge = EDGE_RISING;
	SetTriggerPosition(10);

	LogNotice("libsigrok4DSL: opened \"%s\" (driver %s, %zu channels, %s)\n",
		m_devName.c_str(), m_driverName.c_str(), m_channels.size(), m_isDSO ? "DSO" : "logic");

	m_open = true;
	m_quit = false;
	m_run = false;
	m_thread = thread(&DSLSession::AcquisitionThread, this);
	return true;
}

void DSLSession::Close()
{
	if(!m_open)
		return;

	m_run = false;
	m_quit = true;
	{
		lock_guard<recursive_mutex> lock(m_devMutex);
		AbortRunningCapture();
	}
	m_stateCv.notify_all();
	if(m_thread.joinable())
		m_thread.join();

	{
		lock_guard<recursive_mutex> lock(m_devMutex);
		ds_release_actived_device();
	}
	{
		lock_guard<mutex> lock(m_stateMutex);
		m_ready.clear();
	}
	m_channels.clear();
	m_open = false;
}

string DSLSession::GetChannelName(size_t i) const
{
	if(i >= m_channels.size())
		return "";
	auto ch = (sr_channel*)m_channels[i];
	if(ch->name && ch->name[0])
		return ch->name;
	return to_string(i);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timebase

vector<uint64_t> DSLSession::GetSampleRates()
{
	lock_guard<recursive_mutex> lock(m_devMutex);
	auto rates = GetConfigList<uint64_t>(SR_CONF_SAMPLERATE, "samplerates");
	sort(rates.begin(), rates.end());
	return rates;
}

uint64_t DSLSession::GetSampleRate()
{
	return m_rate;
}

uint64_t DSLSession::SetSampleRate(uint64_t rate)
{
	lock_guard<recursive_mutex> lock(m_devMutex);
	AbortRunningCapture();
	SetConfig<uint64_t>(nullptr, SR_CONF_SAMPLERATE, rate);
	//Drivers clamp to what the enabled channel count allows
	m_rate = GetConfig<uint64_t>(nullptr, SR_CONF_SAMPLERATE).value_or(rate);
	return m_rate;
}

vector<uint64_t> DSLSession::GetSampleDepths()
{
	//Drivers do not list legal depths: offer a 1-2-5 series up to the limit
	vector<uint64_t> ret;
	for(uint64_t d = 1000; d <= m_maxDepth; d *= 10)
	{
		for(uint64_t m : {1, 2, 5})
		{
			if(d * m <= m_maxDepth)
				ret.push_back(d * m);
		}
	}
	if(ret.empty() || ret.back() != m_maxDepth)
		ret.push_back(m_maxDepth);
	return ret;
}

uint64_t DSLSession::GetSampleDepth()
{
	return m_depth;
}

uint64_t DSLSession::SetSampleDepth(uint64_t depth)
{
	lock_guard<recursive_mutex> lock(m_devMutex);
	AbortRunningCapture();
	if(depth > m_maxDepth)
		depth = m_maxDepth;
	SetConfig<uint64_t>(nullptr, SR_CONF_LIMIT_SAMPLES, depth);
	m_depth = GetConfig<uint64_t>(nullptr, SR_CONF_LIMIT_SAMPLES).value_or(depth);
	return m_depth;
}

void DSLSession::SetMaxDepth(uint64_t depth)
{
	if(depth > 0)
		m_maxDepth = min(m_maxDepth, depth);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Channels

bool DSLSession::IsChannelEnabled(size_t i)
{
	if(i >= m_channels.size())
		return false;
	return ((sr_channel*)m_channels[i])->enabled;
}

bool DSLSession::SetChannelEnabled(size_t i, bool enabled)
{
	if(i >= m_channels.size())
		return false;
	if(IsChannelEnabled(i) == enabled)
		return true;

	size_t n = 0;
	for(size_t k=0; k<m_channels.size(); k++)
		n += IsChannelEnabled(k) ? 1 : 0;
	if(!enabled && n <= 1)
		return false;	//libsigrok4DSL refuses to capture with no channels

	lock_guard<recursive_mutex> lock(m_devMutex);
	AbortRunningCapture();

	bool ok;
	if(m_isDSO)
		ok = SetConfig<bool>(m_channels[i], SR_CONF_PROBE_EN, enabled);
	else
		ok = ds_enable_device_channel((sr_channel*)m_channels[i], enabled) == SR_OK;

	//The reachable sample rate depends on the channel count
	m_rate = GetConfig<uint64_t>(nullptr, SR_CONF_SAMPLERATE).value_or(m_rate);
	if(!m_isDSO)
		UpdateLogicTrigger();
	return ok;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Trigger

void DSLSession::SetTrigger(size_t channel, Edge edge, bool enabled)
{
	lock_guard<recursive_mutex> lock(m_devMutex);
	m_trigChannel = channel;
	m_trigEdge = edge;
	m_trigEnabled = enabled;

	if(m_isDSO)
	{
		SetConfig<uint8_t>(nullptr, SR_CONF_TRIGGER_SOURCE, channel == 0 ? DSO_TRIGGER_CH0 : DSO_TRIGGER_CH1);
		if(edge == EDGE_RISING)
			SetConfig<uint8_t>(nullptr, SR_CONF_TRIGGER_SLOPE, DSO_TRIGGER_RISING);
		else if(edge == EDGE_FALLING)
			SetConfig<uint8_t>(nullptr, SR_CONF_TRIGGER_SLOPE, DSO_TRIGGER_FALLING);
	}
	else
		UpdateLogicTrigger();
}

void DSLSession::UpdateLogicTrigger()
{
	bool any = false;
	for(size_t i=0; i<m_channels.size(); i++)
	{
		auto ch = (sr_channel*)m_channels[i];
		char dir = 'X';
		if(m_trigEnabled && i == m_trigChannel && ch->enabled)
		{
			if(m_trigEdge == EDGE_RISING)
				dir = 'R';
			else if(m_trigEdge == EDGE_FALLING)
				dir = 'F';
			else
				dir = 'C';
		}
		if(dir != 'X')
			any = true;
		ds_trigger_probe_set(ch->index, dir, 'X');
	}
	ds_trigger_set_en(any ? 1 : 0);
	ds_trigger_set_pos(m_trigPct);
}

void DSLSession::SetTriggerPosition(uint8_t percent)
{
	lock_guard<recursive_mutex> lock(m_devMutex);
	if(percent > 100)
		percent = 100;
	m_trigPct = percent;
	if(m_isDSO)
		SetConfig<uint8_t>(nullptr, SR_CONF_HORIZ_TRIGGERPOS, m_trigPct);
	else
		ds_trigger_set_pos(m_trigPct);
}

void DSLSession::SetTriggerLevel(double volts)
{
	if(!m_isDSO)
		return;

	lock_guard<recursive_mutex> lock(m_devMutex);
	for(size_t i=0; i<m_channels.size(); i++)
	{
		float scale, offset;
		ComputeScaleAndOffset(i, scale, offset);
		//volts = code*scale - offset  =>  code = (volts + offset) / scale
		float code = (volts + offset) / scale;
		code = max(0.f, min(255.f, code));
		SetConfig<uint8_t>(m_channels[i], SR_CONF_TRIGGER_VALUE, (uint8_t)code);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logic threshold

bool DSLSession::HasThreshold()
{
	lock_guard<recursive_mutex> lock(m_devMutex);
	return !m_isDSO && GetConfig<double>(nullptr, SR_CONF_VTH).has_value();
}

double DSLSession::GetThreshold()
{
	lock_guard<recursive_mutex> lock(m_devMutex);
	return GetConfig<double>(nullptr, SR_CONF_VTH).value_or(0);
}

bool DSLSession::SetThreshold(double volts)
{
	lock_guard<recursive_mutex> lock(m_devMutex);
	volts = max(0.0, min(5.0, volts));
	return SetConfig<double>(nullptr, SR_CONF_VTH, volts);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DSO channel settings

uint64_t DSLSession::GetVdiv(size_t ch)
{
	if(ch >= m_channels.size())
		return 0;
	lock_guard<recursive_mutex> lock(m_devMutex);
	return GetConfig<uint64_t>(m_channels[ch], SR_CONF_PROBE_VDIV).value_or(((sr_channel*)m_channels[ch])->vdiv);
}

bool DSLSession::SetVdiv(size_t ch, uint64_t mv)
{
	if(ch >= m_channels.size())
		return false;
	lock_guard<recursive_mutex> lock(m_devMutex);
	return SetConfig<uint64_t>(m_channels[ch], SR_CONF_PROBE_VDIV, mv);
}

bool DSLSession::SetCouplingAC(size_t ch, bool ac)
{
	if(ch >= m_channels.size())
		return false;
	lock_guard<recursive_mutex> lock(m_devMutex);
	return SetConfig<uint8_t>(m_channels[ch], SR_CONF_PROBE_COUPLING, ac ? SR_AC_COUPLING : SR_DC_COUPLING);
}

bool DSLSession::GetCouplingAC(size_t ch)
{
	if(ch >= m_channels.size())
		return false;
	return ((sr_channel*)m_channels[ch])->coupling == SR_AC_COUPLING;
}

void DSLSession::ComputeScaleAndOffset(size_t ch, float& scale, float& offset)
{
	ScaleForVdiv(GetVdiv(ch), scale, offset);
}

void DSLSession::ScaleForVdiv(uint64_t vdiv, float& scale, float& offset)
{
	float vdiv_mV = vdiv;
	float fullScale = vdiv_mV / 1000 * m_numDivs;
	scale = -1 * m_hwRangeFactor / 255.f * fullScale;	//ADC codes are upside down
	offset = 128 * scale;					//zero is code 128
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Acquisition control

void DSLSession::Start(bool oneShot)
{
	if(!m_open)
		return;
	m_oneShot = oneShot;
	m_run = true;
}

void DSLSession::ForceCapture()
{
	if(!m_open)
		return;
	{
		lock_guard<recursive_mutex> lock(m_devMutex);
		AbortRunningCapture();	//a capture waiting for its trigger would never finish
	}
	m_forceNext = true;
	Start(true);
}

void DSLSession::Stop()
{
	m_run = false;
	lock_guard<recursive_mutex> lock(m_devMutex);
	AbortRunningCapture();
}

shared_ptr<DSLCapture> DSLSession::PopCapture()
{
	lock_guard<mutex> lock(m_stateMutex);
	if(m_ready.empty())
		return nullptr;
	auto c = m_ready.front();
	m_ready.pop_front();
	return c;
}

bool DSLSession::WaitForCapture(int timeoutMs)
{
	unique_lock<mutex> lock(m_stateMutex);
	return m_stateCv.wait_for(lock, chrono::milliseconds(timeoutMs), [&]{ return !m_ready.empty(); });
}

double DSLSession::GetCaptureRate()
{
	lock_guard<mutex> lock(m_rateMutex);
	if(m_captureTimes.size() < 2)
		return 0;
	double span = m_captureTimes.back() - m_captureTimes.front();
	if(NowSeconds() - m_captureTimes.back() > 2)
		return 0;	//stale
	return span > 0 ? (m_captureTimes.size() - 1) / span : 0;
}

/**
	@brief Stops a running hardware capture so the device can be reconfigured.

	Called with m_devMutex held. Captures already queued or in flight were taken with the old
	settings and are thrown away. The acquisition thread starts the next capture on its own.
 */
void DSLSession::AbortRunningCapture()
{
	{
		lock_guard<mutex> lock(m_stateMutex);
		m_ready.clear();
		m_configGen++;
	}
	if(!ds_is_collecting())
		return;
	{
		lock_guard<mutex> lock(m_stateMutex);
		m_discard = true;
	}
	if(ds_stop_collect() == SR_OK)
	{
		lock_guard<mutex> lock(m_stateMutex);
		m_libJoined = true;
	}
}

void DSLSession::Publish(shared_ptr<DSLCapture> c)
{
	//Called with m_stateMutex held
	m_ready.push_back(c);
	while(m_ready.size() > MAX_QUEUED)
	{
		m_ready.pop_front();
		m_dropped++;
	}

	{
		lock_guard<mutex> lock(m_rateMutex);
		m_captureTimes.push_back(NowSeconds());
		while(m_captureTimes.size() > 32)
			m_captureTimes.pop_front();
	}
	m_stateCv.notify_all();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Acquisition thread

void DSLSession::InitCapture(DSLCapture& c)
{
	c.chans.clear();
	for(size_t i=0; i<m_channels.size(); i++)
	{
		if(IsChannelEnabled(i))
			c.chans.push_back(i);
	}
	c.data.assign(c.chans.size(), {});
	c.clipping.assign(c.chans.size(), false);
	c.samples = 0;
	c.triggered = false;
	c.rate = m_rate;
	c.dso = m_isDSO;
}

void DSLSession::AcquisitionThread()
{
#ifdef __linux__
	pthread_setname_np(pthread_self(), "DSLAcquisition");
#endif

	int failures = 0;
	bool forcing = false;
	while(!m_quit)
	{
		if(!m_run)
		{
			this_thread::sleep_for(chrono::milliseconds(1));
			continue;
		}

		{
			lock_guard<recursive_mutex> dev(m_devMutex);
			{
				lock_guard<mutex> lock(m_stateMutex);
				m_cur = make_shared<DSLCapture>();
				InitCapture(*m_cur);
				m_cur->configGen = m_configGen;
				m_collectEnded = false;
				m_collectThread = nullptr;
				m_discard = false;
				m_libJoined = false;
				m_stopAfterFrame = false;
			}
			if(m_forceNext && !m_isDSO)
			{
				ds_trigger_set_en(0);
				forcing = true;
			}
			m_forceNext = false;

			int err = ds_start_collect();
			if(err != SR_OK)
			{
				if(forcing)
				{
					UpdateLogicTrigger();
					forcing = false;
				}
				if(++failures % 50 == 1)
					LogError("libsigrok4DSL: ds_start_collect failed: %s\n", sr_error_str(err));
				this_thread::sleep_for(chrono::milliseconds(100));
				continue;
			}
		}

		//Wait for the capture to end. Stop requests end it via ds_stop_collect(), which
		//delivers the END event as well.
		shared_ptr<DSLCapture> done;
		void* thread;
		{
			unique_lock<mutex> lock(m_stateMutex);
			m_stateCv.wait(lock, [&]{ return m_collectEnded || m_stopAfterFrame; });
			if(m_stopAfterFrame && !m_collectEnded)
			{
				//DSO single shot: frame published, stop the device. ds_stop_collect() joins
				//the collect thread, so our lock must be free meanwhile.
				lock.unlock();
				m_run = false;
				{
					lock_guard<recursive_mutex> dev(m_devMutex);
					if(ds_stop_collect() == SR_OK)
					{
						lock_guard<mutex> l2(m_stateMutex);
						m_libJoined = true;
					}
				}
				lock.lock();
				m_stateCv.wait(lock, [&]{ return m_collectEnded; });
			}
			done = m_cur;
			m_cur.reset();
			thread = m_collectThread;
		}

		//libsigrok4DSL never joins its collect thread when a capture ends on its own; join it
		//here unless ds_stop_collect() already did
		bool discard;
		{
			lock_guard<recursive_mutex> dev(m_devMutex);
			lock_guard<mutex> lock(m_stateMutex);
			if(thread && !m_libJoined)
				g_thread_join((GThread*)thread);
			m_collectThread = nullptr;
			discard = m_discard || (done && done->configGen != m_configGen);
		}

		//Forced capture done: put the trigger back
		if(forcing)
		{
			lock_guard<recursive_mutex> dev(m_devMutex);
			UpdateLogicTrigger();
			forcing = false;
		}

		if(done && done->dso)
			continue;	//DSO frames are published from the datafeed callback

		if(discard || !done || !done->complete || done->error || done->samples == 0)
		{
			if(!discard && done)
			{
				LogDebug("libsigrok4DSL: capture dropped (complete=%d error=%d samples=%lu packets=%u)\n",
					done->complete, done->error, (unsigned long)done->samples, done->packets);
			}
			if(done && done->error)
			{
				if(++failures % 50 == 1)
					LogWarning("libsigrok4DSL: capture ended with an error\n");
				this_thread::sleep_for(chrono::milliseconds(20));
			}
			continue;
		}
		failures = 0;

		{
			lock_guard<mutex> lock(m_stateMutex);
			Publish(done);
		}

		if(m_oneShot)
			m_run = false;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Callbacks (run on the libsigrok4DSL collect thread)

void DSLSession::OnEvent(int event)
{
	lock_guard<mutex> lock(m_stateMutex);
	switch(event)
	{
		case DS_EV_COLLECT_TASK_START:
			//Remember the collect thread so it can be joined (see AcquisitionThread)
			m_collectThread = g_thread_self();
			break;

		case DS_EV_COLLECT_TASK_END:
		case DS_EV_COLLECT_TASK_END_BY_ERROR:
		case DS_EV_COLLECT_TASK_END_BY_DETACHED:
			if(event != DS_EV_COLLECT_TASK_END && m_cur)
				m_cur->error = true;
			if(event == DS_EV_COLLECT_TASK_END_BY_DETACHED)
				LogWarning("libsigrok4DSL: device detached\n");
			m_collectEnded = true;
			m_stateCv.notify_all();
			break;

		default:
			break;
	}
}

uint64_t DSLSession::TriggerSampleFromPacket(const DSLCapture& c, const void* p)
{
	auto pos = (const ds_trigger_pos*)p;

	//PXLogic sends SR_DF_TRIGGER without payload, at the trigger point
	if(!pos || !(pos->status & 1))
		return c.samples;

	//DSLogic reports in 16-bit units across the enabled channels
	if(m_driverName.rfind("DSLogic", 0) == 0)
		return (uint64_t)pos->real_pos * 16 / max<size_t>(1, c.chans.size());

	//capture_core based drivers (FX3, SLogic, hsdaoh, ...) report the sample index
	return pos->real_pos;
}

float DSLSession::InterpolateTriggerPhase(const vector<uint8_t>& buf, uint8_t trigvalue, size_t trigpos)
{
	for(int i=0; i<10; i++)
	{
		long at = (long)trigpos + ((i & 1) ? -(i + 1) / 2 : i / 2);
		if(at <= 0 || (size_t)at >= buf.size())
			continue;
		float pre = buf[at - 1];
		float aft = buf[at];
		float slope = aft - pre;
		if(slope == 0)
			continue;
		float phase = (trigvalue - pre) / slope;
		float val = -(1 - phase);
		if(val > -1 && val <= 0)
			return val + (at - (long)trigpos);
	}
	return 0;
}

void DSLSession::OnDatafeed(const void* p)
{
	auto packet = (const sr_datafeed_packet*)p;

	lock_guard<mutex> lock(m_stateMutex);
	if(!m_cur)
		return;
	DSLCapture& c = *m_cur;
	c.packets++;

	switch(packet->type)
	{
		case SR_DF_HEADER:
			//Not every driver sends one; the capture is initialized before collecting anyway
			if(c.samples == 0)
				InitCapture(c);
			break;

		case SR_DF_TRIGGER:
			c.triggered = true;
			c.trigSample = TriggerSampleFromPacket(c, packet->payload);
			break;

		case SR_DF_LOGIC:
			{
				auto logic = (const sr_datafeed_logic*)packet->payload;
				if(!logic || !logic->data || c.chans.empty())
					break;
				//data_error is ignored, as in DSView: some drivers leave it uninitialized
				if(logic->format != LA_CROSS_DATA)
				{
					LogWarning("libsigrok4DSL: unsupported logic format %d\n", logic->format);
					c.error = true;
					break;
				}

				//LA_CROSS_DATA: per 64-sample group, 8 bytes per enabled channel in order.
				//A trailing partial group cannot be split per channel and is dropped.
				const size_t n = c.chans.size();
				const size_t groupBytes = 8 * n;
				const size_t groups = logic->length / groupBytes;
				const uint8_t* src = (const uint8_t*)logic->data;
				for(size_t ch=0; ch<n; ch++)
					c.data[ch].reserve(c.data[ch].size() + groups * 8);
				for(size_t g=0; g<groups; g++)
				{
					for(size_t ch=0; ch<n; ch++, src += 8)
						c.data[ch].insert(c.data[ch].end(), src, src + 8);
				}
				c.samples += groups * 64;
			}
			break;

		case SR_DF_DSO:
			{
				//DSCope: one frame per packet, collect keeps running
				auto dso = (const sr_datafeed_dso*)packet->payload;
				if(!dso || !dso->data || c.chans.empty() || !m_run)
					break;
				if(c.configGen != m_configGen || m_stopAfterFrame)
					break;

				auto f = make_shared<DSLCapture>();
				f->dso = true;
				f->rate = c.rate;
				f->chans = c.chans;
				f->configGen = c.configGen;
				const size_t n = f->chans.size();
				f->data.assign(n, vector<uint8_t>(dso->num_samples));
				f->clipping.assign(n, false);
				auto src = (const uint8_t*)dso->data;
				for(int s=0; s<dso->num_samples; s++)
				{
					for(size_t ch=0; ch<n; ch++)
					{
						uint8_t d = *src++;
						if(d <= m_hwMin || d >= m_hwMax)
							f->clipping[ch] = true;
						f->data[ch][s] = d;
					}
				}
				f->samples = dso->num_samples;
				f->complete = true;

				//Scale/offset now, since V/div may change before the frame is consumed.
				//Read V/div from the channel struct: taking m_devMutex here could deadlock
				//against a reconfiguration that is joining this thread.
				f->scale.resize(n);
				f->offset.resize(n);
				for(size_t ch=0; ch<n; ch++)
					ScaleForVdiv(((sr_channel*)m_channels[f->chans[ch]])->vdiv, f->scale[ch], f->offset[ch]);

				//Sub-sample trigger position on the trigger channel
				auto it = find(f->chans.begin(), f->chans.end(), m_trigChannel);
				if(it != f->chans.end())
				{
					size_t ti = it - f->chans.begin();
					auto tch = (sr_channel*)m_channels[m_trigChannel];
					f->trigPhase = InterpolateTriggerPhase(f->data[ti], tch->trig_value, f->samples * m_trigPct / 100);
				}

				Publish(f);
				if(m_oneShot)
					m_stopAfterFrame = true;
			}
			break;

		case SR_DF_END:
			c.complete = true;
			break;

		default:
			break;
	}
}
