/***********************************************************************************************************************
*                                                                                                                      *
* libsigrok4DSL plugin for ngscopeclient                                                                               *
*                                                                                                                      *
* Licensed under the GNU GPL v3 or later (libsigrok4DSL is GPLv3).                                                     *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@brief Capture engine on top of the libsigrok4DSL ds_* API (DSView >= 1.3 / ALL-LOGIC)

	Deliberately free of scopehal types so it can be tested on its own. The scopehal driver
	(LibSigrok4DSLOscilloscope) turns the Capture objects produced here into waveforms.
 */

#ifndef DSLSession_h
#define DSLSession_h

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

/**
	@brief One complete acquisition from the device
 */
struct DSLCapture
{
	///@brief True for DSO (8-bit analog) data, false for logic data
	bool dso = false;

	///@brief Sample rate the capture was taken at
	uint64_t rate = 0;

	///@brief Device channel indexes present, in data order
	std::vector<size_t> chans;

	/**
		@brief Per-channel sample data

		Logic: 8 samples per byte, least significant bit first (byte k bit b = sample 8k+b).
		DSO: one unsigned ADC code per sample.
	 */
	std::vector<std::vector<uint8_t>> data;

	///@brief Samples per channel
	uint64_t samples = 0;

	///@brief True if the device reported where it triggered
	bool triggered = false;

	///@brief Sample index of the trigger (valid if triggered)
	uint64_t trigSample = 0;

	///@brief Per-channel clipping flag (DSO)
	std::vector<bool> clipping;

	///@brief Per-channel conversion volts = code*scale - offset (DSO)
	std::vector<float> scale;
	std::vector<float> offset;

	///@brief Sub-sample trigger interpolation in samples (DSO)
	float trigPhase = 0;

	//Internal bookkeeping
	bool complete = false;
	bool error = false;
	unsigned packets = 0;
	uint64_t configGen = 0;
};

/**
	@brief Device and acquisition manager

	libsigrok4DSL keeps all of its state in globals, so only one device can be open per process.
	DSLSession is therefore a singleton.
 */
class DSLSession
{
public:
	static DSLSession& Get();

	enum Edge
	{
		EDGE_RISING,
		EDGE_FALLING,
		EDGE_ANY
	};

	struct DeviceEntry
	{
		size_t index;
		std::string name;
	};

	//Library / device lifetime
	bool InitLibrary(const std::string& resourceDir, int logLevel = 1);
	std::vector<DeviceEntry> ListDevices();
	bool Open(const std::string& selector, int requestedMode, std::string& error);
	void Close();
	bool IsOpen() const
	{ return m_open; }

	//Device information
	std::string GetDeviceName() const
	{ return m_devName; }
	std::string GetDriverName() const
	{ return m_driverName; }
	bool IsDSO() const
	{ return m_isDSO; }
	size_t GetChannelCount() const
	{ return m_channels.size(); }
	std::string GetChannelName(size_t i) const;

	//Timebase
	std::vector<uint64_t> GetSampleRates();
	uint64_t GetSampleRate();
	uint64_t SetSampleRate(uint64_t rate);
	std::vector<uint64_t> GetSampleDepths();
	uint64_t GetSampleDepth();
	uint64_t SetSampleDepth(uint64_t depth);
	uint64_t GetMaxDepth() const
	{ return m_maxDepth; }
	void SetMaxDepth(uint64_t depth);

	//Channels
	bool IsChannelEnabled(size_t i);
	bool SetChannelEnabled(size_t i, bool enabled);

	//Trigger
	void SetTrigger(size_t channel, Edge edge, bool enabled = true);
	void SetTriggerPosition(uint8_t percent);
	uint8_t GetTriggerPosition() const
	{ return m_trigPct; }
	void SetTriggerLevel(double volts);	//DSO only

	//Logic threshold (devices with SR_CONF_VTH)
	bool HasThreshold();
	double GetThreshold();
	bool SetThreshold(double volts);

	//DSO channel settings
	std::vector<uint64_t> GetVdivOptions() const
	{ return m_vdivOptions; }
	uint64_t GetVdiv(size_t ch);
	bool SetVdiv(size_t ch, uint64_t mv);
	int GetNumDivs() const
	{ return m_numDivs; }
	bool SetCouplingAC(size_t ch, bool ac);
	bool GetCouplingAC(size_t ch);
	void ComputeScaleAndOffset(size_t ch, float& scale, float& offset);
	void ScaleForVdiv(uint64_t vdiv_mV, float& scale, float& offset);

	//Acquisition
	void Start(bool oneShot);
	void Stop();

	/**
		@brief Takes one capture now, ignoring the trigger (logic analyzers)

		The trigger is disabled for that capture only and restored afterwards.
	 */
	void ForceCapture();
	bool IsArmed() const
	{ return m_run; }

	/**
		@brief Pops the oldest finished capture, or null if none is waiting
	 */
	std::shared_ptr<DSLCapture> PopCapture();

	/**
		@brief Waits up to timeoutMs for a finished capture
	 */
	bool WaitForCapture(int timeoutMs);

	///@brief Hardware capture rate (captures per second, averaged)
	double GetCaptureRate();

	///@brief Captures thrown away because the consumer was too slow
	uint64_t GetDroppedCount() const
	{ return m_dropped; }

	//libsigrok4DSL callbacks (public so the C trampolines can reach them)
	void OnEvent(int event);
	void OnDatafeed(const void* packet);

protected:
	DSLSession();
	~DSLSession();

	void AcquisitionThread();
	void AbortRunningCapture();
	void UpdateLogicTrigger();
	void InitCapture(DSLCapture& c);
	void Publish(std::shared_ptr<DSLCapture> c);
	uint64_t TriggerSampleFromPacket(const DSLCapture& c, const void* pos);
	float InterpolateTriggerPhase(const std::vector<uint8_t>& buf, uint8_t trigvalue, size_t trigpos);

	bool m_libInit = false;
	std::atomic<bool> m_open{false};

	std::string m_devName;
	std::string m_driverName;
	bool m_isDSO = false;
	std::vector<void*> m_channels;		//struct sr_channel*
	std::vector<uint64_t> m_vdivOptions;
	int m_numDivs = 10;
	uint32_t m_hwMin = 0;
	uint32_t m_hwMax = 255;
	float m_hwRangeFactor = 1;

	uint64_t m_rate = 0;
	uint64_t m_depth = 0;
	uint64_t m_maxDepth = 100000000;
	uint8_t m_trigPct = 0;
	size_t m_trigChannel = 0;
	Edge m_trigEdge = EDGE_RISING;
	bool m_trigEnabled = true;

	//Serializes ds_start_collect/ds_stop_collect and device configuration
	std::recursive_mutex m_devMutex;

	//Capture state shared with the collect thread
	std::mutex m_stateMutex;
	std::condition_variable m_stateCv;
	std::shared_ptr<DSLCapture> m_cur;
	bool m_collectEnded = false;
	void* m_collectThread = nullptr;	//GThread*
	bool m_discard = false;
	bool m_libJoined = false;
	bool m_stopAfterFrame = false;
	uint64_t m_configGen = 0;

	//Finished captures waiting for the consumer
	std::deque<std::shared_ptr<DSLCapture>> m_ready;
	static const size_t MAX_QUEUED = 2;
	std::atomic<uint64_t> m_dropped{0};

	//Acquisition thread
	std::thread m_thread;
	std::atomic<bool> m_quit{false};
	std::atomic<bool> m_run{false};
	std::atomic<bool> m_oneShot{false};
	std::atomic<bool> m_forceNext{false};

	//Capture rate measurement
	std::mutex m_rateMutex;
	std::deque<double> m_captureTimes;
};

#endif
