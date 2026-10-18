/***********************************************************************************************************************
*                                                                                                                      *
* libsigrok4DSL plugin for ngscopeclient                                                                               *
*                                                                                                                      *
* Licensed under the GNU GPL v3 or later (libsigrok4DSL is GPLv3).                                                     *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@brief Declaration of LibSigrok4DSLOscilloscope
 */

#ifndef LibSigrok4DSLOscilloscope_h
#define LibSigrok4DSLOscilloscope_h

#include "scopehal.h"
#include "SCPIOscilloscope.h"
#include "EdgeTrigger.h"

class DSLSession;

/**
	@brief In-process driver for any device supported by libsigrok4DSL (DSView / ALL-LOGIC)

	Driver name "libsigrok4dsl", transport "null". The connection path selects the device and
	takes optional settings:

		<device>[,res=<firmware dir>][,mode=logic|dso][,maxdepth=<samples>]
				[,channels=<count>][,chmode=<mode id>]

	<device> is an index, part of the device name, or a libsigrok4DSL driver name ("slogic",
	"fx2lafw", "0", ...). Empty means the first USB device, else the demo device.

	libsigrok4DSL keeps its state in globals, so one instance per process.
 */
class LibSigrok4DSLOscilloscope : public virtual SCPIOscilloscope
{
public:
	LibSigrok4DSLOscilloscope(SCPITransport* transport);
	virtual ~LibSigrok4DSLOscilloscope();

	//not copyable or assignable
	LibSigrok4DSLOscilloscope(const LibSigrok4DSLOscilloscope& rhs) =delete;
	LibSigrok4DSLOscilloscope& operator=(const LibSigrok4DSLOscilloscope& rhs) =delete;

public:
	//Device information
	virtual std::string IDPing() override;
	virtual unsigned int GetInstrumentTypes() const override;
	virtual uint32_t GetInstrumentTypesForChannel(size_t i) const override;
	virtual void FlushConfigCache() override;

	//Channel configuration
	virtual bool IsChannelEnabled(size_t i) override;
	virtual void EnableChannel(size_t i) override;
	virtual void DisableChannel(size_t i) override;
	virtual bool CanEnableChannel(size_t i) override;
	virtual OscilloscopeChannel::CouplingType GetChannelCoupling(size_t i) override;
	virtual void SetChannelCoupling(size_t i, OscilloscopeChannel::CouplingType type) override;
	virtual std::vector<OscilloscopeChannel::CouplingType> GetAvailableCouplings(size_t i) override;
	virtual double GetChannelAttenuation(size_t i) override;
	virtual void SetChannelAttenuation(size_t i, double atten) override;
	virtual unsigned int GetChannelBandwidthLimit(size_t i) override;
	virtual void SetChannelBandwidthLimit(size_t i, unsigned int limit_mhz) override;
	virtual float GetChannelVoltageRange(size_t i, size_t stream) override;
	virtual void SetChannelVoltageRange(size_t i, size_t stream, float range) override;
	virtual OscilloscopeChannel* GetExternalTrigger() override;
	virtual float GetChannelOffset(size_t i, size_t stream) override;
	virtual void SetChannelOffset(size_t i, size_t stream, float offset) override;

	//Triggering
	virtual Oscilloscope::TriggerMode PollTrigger() override;
	virtual bool AcquireData() override;
	virtual void Start() override;
	virtual void StartSingleTrigger() override;
	virtual void Stop() override;
	virtual void ForceTrigger() override;
	virtual bool IsTriggerArmed() override;
	virtual void PushTrigger() override;
	virtual void PullTrigger() override;

	//Timebase
	virtual std::vector<uint64_t> GetSampleRatesNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleRatesInterleaved() override;
	virtual std::set<InterleaveConflict> GetInterleaveConflicts() override;
	virtual std::vector<uint64_t> GetSampleDepthsNonInterleaved() override;
	virtual std::vector<uint64_t> GetSampleDepthsInterleaved() override;
	virtual uint64_t GetSampleRate() override;
	virtual uint64_t GetSampleDepth() override;
	virtual void SetSampleDepth(uint64_t depth) override;
	virtual void SetSampleRate(uint64_t rate) override;
	virtual void SetTriggerOffset(int64_t offset) override;
	virtual int64_t GetTriggerOffset() override;
	virtual bool IsInterleaving() override;
	virtual bool SetInterleaving(bool combine) override;

	//Logic threshold
	virtual std::vector<DigitalBank> GetDigitalBanks() override;
	virtual DigitalBank GetDigitalBank(size_t channel) override;
	virtual bool IsDigitalHysteresisConfigurable() override;
	virtual bool IsDigitalThresholdConfigurable() override;
	virtual float GetDigitalHysteresis(size_t channel) override;
	virtual float GetDigitalThreshold(size_t channel) override;
	virtual void SetDigitalHysteresis(size_t channel, float level) override;
	virtual void SetDigitalThreshold(size_t channel, float level) override;

protected:
	void ParseConnectionString(const std::string& path);
	void PushEdgeTrigger(EdgeTrigger* trig);
	void ApplyTriggerPosition();
	bool IsDSO() const
	{ return m_dso; }

	///@brief The engine (singleton), valid if m_connected
	DSLSession& m_session;

	///@brief True if this instance opened the device
	bool m_connected;

	///@brief True for DSO (analog) devices, false for logic analyzers
	bool m_dso;

	//Connection options
	std::string m_selector;
	std::string m_resDir;
	int m_requestedMode;
	uint64_t m_maxDepth;
	int m_requestedChannels;
	int m_requestedChannelModeId;

	///@brief Femtoseconds from capture start to trigger
	int64_t m_triggerOffset;

	///@brief Per-channel probe attenuation (DSO)
	std::map<size_t, double> m_channelAttenuations;

	///@brief Cached logic threshold
	float m_threshold;
	bool m_hasThreshold;

public:
	static std::string GetDriverNameInternal();

	//cppcheck-suppress duplInheritedMember
	static std::vector<SCPIInstrumentModel> GetDriverSupportedModels()
	{
		return
		{
			{"libsigrok4DSL device", {{ SCPITransportType::TRANSPORT_NULL, "" }}}
		};
	}

	OSCILLOSCOPE_INITPROC(LibSigrok4DSLOscilloscope)
};

#endif
