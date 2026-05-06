#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include <cstdio>
#include "PerfTrace.h"
#include "DspDebugLog.h"

class ECHOTRAudioProcessor : public juce::AudioProcessor
{
public:
	ECHOTRAudioProcessor();
	~ECHOTRAudioProcessor() override;

	// Parameter IDs
	static constexpr const char* kParamTimeMs     = "time_ms";
	static constexpr const char* kParamTimeSync   = "time_sync";
	static constexpr const char* kParamFeedback   = "feedback";
	static constexpr const char* kParamJitter     = "jitter";
	static constexpr const char* kParamMode       = "mode";
	static constexpr const char* kParamMod        = "mod";
	static constexpr const char* kParamInput      = "input";
	static constexpr const char* kParamOutput     = "output";
	static constexpr const char* kParamMix        = "mix";
	static constexpr const char* kParamSync       = "sync";
	static constexpr const char* kParamMidi       = "midi";
	static constexpr const char* kParamAutoFbk    = "auto_fbk";
	static constexpr const char* kParamAutoFbkTau = "auto_fbk_tau";
	static constexpr const char* kParamAutoFbkAtt = "auto_fbk_att";
	static constexpr const char* kParamReverse      = "reverse";
	static constexpr const char* kParamReverseSmooth = "reverse_smooth";

	// Filter parameter IDs
	static constexpr const char* kParamFilterHpFreq  = "filter_hp_freq";
	static constexpr const char* kParamFilterLpFreq  = "filter_lp_freq";
	static constexpr const char* kParamFilterHpSlope = "filter_hp_slope";
	static constexpr const char* kParamFilterLpSlope = "filter_lp_slope";
	static constexpr const char* kParamFilterHpOn    = "filter_hp_on";
	static constexpr const char* kParamFilterLpOn    = "filter_lp_on";

	// Tilt parameter ID
	static constexpr const char* kParamTilt = "tilt";
	static constexpr const char* kParamPan  = "pan";

	// Engine parameter IDs
	static constexpr const char* kParamEngine     = "engine";
	static constexpr const char* kParamSat1Drive  = "sat1_drive";
	static constexpr const char* kParamSat1Grit   = "sat1_grit";
	static constexpr const char* kParamSat2Drive  = "sat2_drive";
	static constexpr const char* kParamSat2Grit   = "sat2_grit";

	// Chaos parameter IDs
	static constexpr const char* kParamChaos     = "chaos";      // filter chaos (CHS F)
	static constexpr const char* kParamChaosD    = "chaos_d";    // delay chaos  (CHS D)
	static constexpr const char* kParamChaosAmt  = "chaos_amt";
	static constexpr const char* kParamChaosSpd  = "chaos_spd";
	static constexpr const char* kParamChaosAmtFilter = "chaos_amt_filter";
	static constexpr const char* kParamChaosSpdFilter = "chaos_spd_filter";
	static constexpr const char* kParamDuck           = "duck";

	// Mode In / Mode Out / Sum Bus parameter IDs
	static constexpr const char* kParamModeIn  = "mode_in";
	static constexpr const char* kParamModeOut = "mode_out";
	static constexpr const char* kParamSumBus  = "sum_bus";

	// Invert Polarity / Invert Stereo
	static constexpr const char* kParamInvPol  = "inv_pol";
	static constexpr const char* kParamInvStr  = "inv_str";

	// Mix Mode (INSERT / SEND)
	static constexpr const char* kParamMixMode  = "mix_mode";
	static constexpr const char* kParamDryLevel = "dry_level";
	static constexpr const char* kParamWetLevel = "wet_level";

	// Filter position (PRE / POST saturation)
	static constexpr const char* kParamFilterPos = "filter_pos";

	// Limiter parameter IDs
	static constexpr const char* kParamLimThreshold = "lim_threshold";
	static constexpr const char* kParamLimMode      = "lim_mode";
	
	// UI state parameters (hidden from DAW automation)
	static constexpr const char* kParamUiWidth    = "ui_width";
	static constexpr const char* kParamUiHeight   = "ui_height";
	static constexpr const char* kParamUiPalette  = "ui_palette";
	static constexpr const char* kParamUiCrt      = "ui_fx_tail";  // string kept for preset compat
	static constexpr const char* kParamUiColor0   = "ui_color0";
	static constexpr const char* kParamUiColor1   = "ui_color1";

	// Parameter ranges and defaults
	static constexpr float kTimeMsMin = 0.0f;
	static constexpr float kTimeMsMax = 5000.0f;
	static constexpr float kTimeMsMaxSync = 30000.0f;
	static constexpr float kTimeMsDefault = 500.0f;

	static constexpr int kTimeSyncMin = 0;
	static constexpr int kTimeSyncMax = 28;
	static constexpr int kTimeSyncDefault = 10;

	static constexpr float kFeedbackMin = -1.0f;
	static constexpr float kFeedbackMax =  1.0f;
	static constexpr float kFeedbackDefault = 0.0f;

	static constexpr float kJitterMin = 0.0f;
	static constexpr float kJitterMax = 100.0f;
	static constexpr float kJitterDefault = 0.0f;

	static constexpr int kModeMin = 0;
	static constexpr int kModeMax = 4; // 0=MONO, 1=STEREO, 2=WIDE, 3=DUAL, 4=PING-PONG ("Style" in UI)
	static constexpr float kModeDefault = 1.0f;

	static constexpr float kModMin = 0.0f;
	static constexpr float kModMax = 1.0f;
	static constexpr float kModDefault = 0.5f;

	static constexpr float kGainFloorDb  = -144.0f;
	static constexpr float kGainMaxDb    =   24.0f;
	static constexpr float kGainDefaultDb =   0.0f;
	static constexpr float kGainSkew     = 4.4965561056f; // 0 dB at the fader midpoint

	static constexpr float kInputMin = kGainFloorDb;
	static constexpr float kInputMax = kGainMaxDb;
	static constexpr float kInputDefault = kGainDefaultDb;

	static constexpr float kOutputMin = kGainFloorDb;
	static constexpr float kOutputMax = kGainMaxDb;
	static constexpr float kOutputDefault = kGainDefaultDb;

	static constexpr float kMixMin = 0.0f;
	static constexpr float kMixMax = 1.0f;
	static constexpr float kMixDefault = 1.0f;

	static constexpr int   kMixModeDefault  = 0;   // 0=INSERT, 1=SEND
	static constexpr float kDryLevelDefault = 0.0f;
	static constexpr float kWetLevelDefault = 1.0f;

	static constexpr float kAutoFbkTauMin     = 0.0f;
	static constexpr float kAutoFbkTauMax     = 100.0f;
	static constexpr float kAutoFbkTauDefault = 50.0f;
	static constexpr float kAutoFbkAttMin     = 0.0f;
	static constexpr float kAutoFbkAttMax     = 100.0f;
	static constexpr float kAutoFbkAttDefault = 75.0f;

	static constexpr float kReverseSmoothMin     = -2.0f;
	static constexpr float kReverseSmoothMax     =  2.0f;
	static constexpr float kReverseSmoothDefault =  0.0f;

	// Filter ranges and defaults
	static constexpr float kFilterFreqMin       = 20.0f;
	static constexpr float kFilterFreqMax       = 20000.0f;
	static constexpr float kFilterHpFreqDefault = 250.0f;
	static constexpr float kFilterLpFreqDefault = 2000.0f;
	static constexpr int   kFilterSlopeMin      = 0;       // 6 dB/oct
	static constexpr int   kFilterSlopeMax      = 2;       // 24 dB/oct
	static constexpr int   kFilterSlopeDefault  = 1;       // 12 dB/oct

	// Tilt ranges and defaults
	static constexpr float kTiltMin     = -6.0f;
	static constexpr float kTiltMax     =  6.0f;
	static constexpr float kTiltDefault =  0.0f;

	static constexpr float kPanMin     = 0.0f;
	static constexpr float kPanMax     = 1.0f;
	static constexpr float kPanDefault = 0.5f;

	// Chaos ranges and defaults
	static constexpr float kChaosAmtMin     = 0.0f;
	static constexpr float kChaosAmtMax     = 100.0f;
	static constexpr float kChaosAmtDefault = 50.0f;
	static constexpr float kChaosSpdMin     = 0.01f;   // Hz
	static constexpr float kChaosSpdMax     = 100.0f;  // Hz
	static constexpr float kChaosSpdDefault = 5.0f;    // Hz

	// Model ranges and defaults
	static constexpr int kEngineMin     = 0;       // 0=CLEAN
	static constexpr int kEngineMax     = 2;       // 2=SAT2
	static constexpr int kEngineDefault = 0;       // CLEAN

	// Saturation drive / grit ranges and defaults
	static constexpr float kSatDriveMin     = 0.0f;
	static constexpr float kSatDriveMax     = 100.0f;
	static constexpr float kSatDriveDefault = 50.0f;
	static constexpr float kSatGritMin      = 0.0f;
	static constexpr float kSatGritMax      = 100.0f;
	static constexpr float kSatGritDefault  = 0.0f;

	// Duck ranges and defaults
	static constexpr float kDuckMin     = 0.0f;
	static constexpr float kDuckMax     = 100.0f;
	static constexpr float kDuckDefault = 0.0f;    // off by default

	// Mode In / Mode Out / Sum Bus defaults
	static constexpr int   kModeInOutDefault = 0;   // 0=L+R  1=MID  2=SIDE
	static constexpr int   kSumBusDefault    = 0;   // 0=ST   1=→M   2=→S
	static constexpr float kSqrt2Over2       = 0.707106781f;

	// Limiter ranges and defaults
	static constexpr float kLimThresholdMin     = -36.0f;
	static constexpr float kLimThresholdMax     = 0.0f;
	static constexpr float kLimThresholdDefault = 0.0f;
	static constexpr int   kLimModeDefault      = 0;   // 0=NONE  1=WET  2=GLOBAL

	// Invert Polarity / Invert Stereo defaults
	static constexpr int   kInvPolDefault       = 0;   // 0=NONE  1=WET  2=GLOBAL
	static constexpr int   kInvStrDefault       = 0;   // 0=NONE  1=WET  2=GLOBAL

	// Filter position default
	static constexpr int   kFilterPosDefault    = 0;   // 0=POST  1=PRE

	static juce::StringArray getTimeSyncChoices();
	static juce::String getTimeSyncName(int index);
	float tempoSyncToMs (int syncIndex, double bpm) const;
	
	static juce::String getMidiNoteName (int midiNote);
	float getCurrentDelayMs() const;
	juce::String getCurrentTimeDisplay() const;

	void prepareToPlay (double sampleRate, int samplesPerBlock) override;
	void releaseResources() override;

#if ! JucePlugin_PreferredChannelConfigurations
	bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
#endif

	void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
	
	// Optimized delay processing functions
	void processStereoDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
	                          float delaySamples, float feedback, float inputGain, 
	                          float outputGain, float /*mix*/, float delaySmoothCoeff);
	void processMonoDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
	                        float delaySamples, float feedback, float inputGain,
	                        float outputGain, float /*mix*/, float delaySmoothCoeff);
	void processPingPongDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
	                            float delaySamples, float feedback, float inputGain,
	                            float outputGain, float /*mix*/, float delaySmoothCoeff);
	void processWideDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
	                       float delaySamples, float feedback, float inputGain,
	                       float outputGain, float /*mix*/, float delaySmoothCoeff);
	void processDualDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
	                       float delaySamples, float feedback, float inputGain,
	                       float outputGain, float /*mix*/, float delaySmoothCoeff);

	// Reverse delay processing (chunk-based backward playback with smooth taper control)
	void processReverseDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
	                          float delaySamples, float feedback, float inputGain,
	                          float outputGain, float /*mix*/, float delaySmoothCoeff,
	                          float smoothMult, int mode);

	juce::AudioProcessorEditor* createEditor() override;
	bool hasEditor() const override;

	const juce::String getName() const override;

	bool acceptsMidi() const override;
	bool producesMidi() const override;
	bool isMidiEffect() const override;
	double getTailLengthSeconds() const override;

	int getNumPrograms() override;
	int getCurrentProgram() override;
	void setCurrentProgram (int index) override;
	const juce::String getProgramName (int index) override;
	void changeProgramName (int index, const juce::String& newName) override;

	void getStateInformation (juce::MemoryBlock& destData) override;
	void setStateInformation (const void* data, int sizeInBytes) override;
	void getCurrentProgramStateInformation (juce::MemoryBlock& destData) override;
	void setCurrentProgramStateInformation (const void* data, int sizeInBytes) override;

	// UI state management (same as DISP-TR)
	void setUiEditorSize (int width, int height);
	int getUiEditorWidth() const noexcept;
	int getUiEditorHeight() const noexcept;

	void setUiUseCustomPalette (bool shouldUseCustomPalette);
	bool getUiUseCustomPalette() const noexcept;

	void setUiCrtEnabled (bool enabled);
	bool getUiCrtEnabled() const noexcept;

	void setMidiChannel (int channel);
	int getMidiChannel() const noexcept;

	void setUiIoExpanded (bool expanded);
	bool getUiIoExpanded() const noexcept;

	void setUiCustomPaletteColour (int index, juce::Colour colour);
	juce::Colour getUiCustomPaletteColour (int index) const noexcept;

	juce::AudioProcessorValueTreeState apvts;
	static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

	// Wet-signal HP/LP filter biquad structs
	struct WetFilterBiquadCoeffs { float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; };
	struct WetFilterBiquadState  { float z1 = 0.0f, z2 = 0.0f; };

	// Performance tracing (enable with ECHOTR_PERF_TRACE=1)
	PerfTrace perfTrace;

	// DSP debug logger (enable with ECHOTR_DSP_DEBUG_LOG=1)
	// Auto-dumps to Desktop/echotr_dsp_debug.csv on plugin destruction.
	DspDebugLog dspLog;

private:
	struct UiStateKeys
	{
		static constexpr const char* editorWidth = "uiEditorWidth";
		static constexpr const char* editorHeight = "uiEditorHeight";
		static constexpr const char* useCustomPalette = "uiUseCustomPalette";
		static constexpr const char* crtEnabled = "uiFxTailEnabled";  // string kept for preset compat
		static constexpr const char* midiPort = "midiPort";
		static constexpr const char* ioExpanded = "uiIoExpanded";
		static constexpr std::array<const char*, 2> customPalette {
			"uiCustomPalette0", "uiCustomPalette1"
		};
	};

	double currentSampleRate = 44100.0;
	float cachedDelaySmoothCoeff = 0.0f;   // precomputed EMA coeff at current SR
	
	juce::AudioBuffer<float> delayBuffer;
	int delayBufferWritePos = 0;
	int delayBufferLength = 0;
	float smoothedDelaySamples = 0.0f;
	float smoothedDelaySamplesR = 0.0f; // Independent R delay for WIDE/DUAL modes
	float smoothedInputGain = 1.0f;
	float smoothedOutputGain = 1.0f;
	float smoothedMix = 0.5f;
	float smoothedDryLevel = 1.0f;
	float smoothedWetLevel = 1.0f;
	float dryGainTarget_ = 0.0f;
	float wetGainTarget_ = 1.0f;
	std::array<float, 2> feedbackState { 0.0f, 0.0f };

	// Single-voice reverse delay state.
	// One read head reads BACKWARDS through the main delay buffer.
	// Proportional Tukey taper at chunk edges prevents clicks.
	// Feedback reads FORWARD (like direct mode) for coherent tails.
	int   reverseAnchor     = 0;      // writePos snapshot at chunk start
	float reverseCounter    = 0.0f;   // position within chunk (0 → chunkLen)
	float reverseChunkLen   = 0.0f;   // locked chunk length (set at chunk start)
	float revSmoothedDelay  = 0.0f;   // EMA-smoothed delay for reverse (independent)
	bool  reverseNeedsInit  = true;   // first-call initialisation flag

	// Auto-feedback envelope: ramps 0→1 after each note/time/MOD change.
	// Multiplied with the user's feedback value so it "resets" on change.
	float autoFbkEnvelope          = 1.0f;  // current envelope level (0 = silent, 1 = full)
	float autoFbkLastDelaySamples  = -1.0f; // previous final delay in samples; -1 = uninitialised
	bool  prevAutoFbkEnabled       = false;  // previous state for edge-detect buffer clear
	int   autoFbkCooldownLeft      = 0;     // samples remaining before next reset allowed

	// ── Feedback loop processing state ──
	// DC blocker only (maximally transparent feedback path).
	float fbkDcStateInL = 0.0f;  // DC blocker input state (left)
	float fbkDcStateInR = 0.0f;  // DC blocker input state (right)
	float fbkDcStateOutL = 0.0f; // DC blocker output state (left)
	float fbkDcStateOutR = 0.0f; // DC blocker output state (right)
	float wetDcStateInL = 0.0f;  // wet pre-limiter DC blocker input state (left)
	float wetDcStateInR = 0.0f;  // wet pre-limiter DC blocker input state (right)
	float wetDcStateOutL = 0.0f; // wet pre-limiter DC blocker output state (left)
	float wetDcStateOutR = 0.0f; // wet pre-limiter DC blocker output state (right)
	float outDcStateInL = 0.0f;  // final output DC blocker input state (left)
	float outDcStateInR = 0.0f;  // final output DC blocker input state (right)
	float outDcStateOutL = 0.0f; // final output DC blocker output state (left)
	float outDcStateOutR = 0.0f; // final output DC blocker output state (right)
	// Per-block precomputed coefficient
	float fbkDcCoeff  = 0.999f;  // DC blocker R coefficient

	// ── Wet-signal HP/LP filter state ──
	struct WetFilterChannelState
	{
		WetFilterBiquadState hp[2];   // up to 2 cascaded HP sections
		WetFilterBiquadState lp[2];   // up to 2 cascaded LP sections
		void reset() { hp[0] = hp[1] = lp[0] = lp[1] = {}; }
	};
	WetFilterChannelState wetFilterState_[2];
	WetFilterBiquadCoeffs hpCoeffs_[2];       // per-section HP coeffs (L / mono)
	WetFilterBiquadCoeffs lpCoeffs_[2];       // per-section LP coeffs (L / mono)
	WetFilterBiquadCoeffs hpCoeffsR_[2];      // per-section HP coeffs (R, stereo chaos)
	WetFilterBiquadCoeffs lpCoeffsR_[2];      // per-section LP coeffs (R, stereo chaos)
	float smoothedFilterHpFreq_ = kFilterHpFreqDefault;
	float smoothedFilterLpFreq_ = kFilterLpFreqDefault;
	float lastCalcHpFreq_ = -1.0f, lastCalcLpFreq_ = -1.0f;
	int   lastCalcHpSlope_ = -1,   lastCalcLpSlope_ = -1;
	int   filterCoeffCountdown_ = 0;
	static constexpr int kFilterCoeffUpdateInterval = 32;
	void updateFilterCoeffs (bool forceHp, bool forceLp);

	// Runtime filter targets (loaded in processBlock, used in process*Delay)
	bool  wetFilterHpOn_   = false;
	bool  wetFilterLpOn_   = false;
	bool  filterPre_       = false;  // true = filter BEFORE saturation
	bool  tiltPre_         = false;  // true = tilt   BEFORE saturation
	float wetFilterTargetHpFreq_ = kFilterHpFreqDefault;
	float wetFilterTargetLpFreq_ = kFilterLpFreqDefault;
	int   wetFilterNumSectionsHp_ = 0;
	int   wetFilterNumSectionsLp_ = 0;
	void  filterWetSample (float& wetL, float& wetR);
	void  tiltWetSample   (float& wetL, float& wetR);

	// Tilt filter state
	float tiltDb_        = 0.0f;
	float tiltB0_ = 1.0f, tiltB1_ = 0.0f, tiltA1_ = 0.0f;
	float tiltTargetB0_ = 1.0f, tiltTargetB1_ = 0.0f, tiltTargetA1_ = 0.0f;
	float tiltState_[2]  = { 0.0f, 0.0f };
	float lastTiltDb_    = 0.0f;
	float tiltSmoothSc_  = 0.0f;  // cached EMA coeff (depends only on sampleRate)

	// ── Chaos state ──
	bool  chaosFilterEnabled_    = false;
	bool  chaosDelayEnabled_     = false;
	bool  chaosStereo_           = false;   // true when mode >= 1 (per-channel D/G)

	// CHS D parameters
	float chaosAmtD_                    = 0.0f;
	float chaosAmtNormD_                = 0.0f;   // cached amtD * 0.01
	float chaosShPeriodD_               = 8820.0f;
	float smoothedChaosShPeriodD_       = 8820.0f;
	float chaosDelayMaxSamples_         = 0.0f;
	float smoothedChaosDelayMaxSamples_ = 0.0f;
	float chaosGainMaxDb_               = 0.0f;
	float smoothedChaosGainMaxDb_       = 0.0f;
	float chaosDelaySmoothedSamples_[2] = {};
	bool  chaosDelaySmoothReady_[2]     = {};
	float chaosDriveAmtSmoothed_        = 0.0f;
	float chaosDriveSpdSmoothed_        = 0.0f;
	bool  chaosDriveParamSmoothReady_   = false;

	// CHS D smooth S&H + Drift: delay (per-channel for stereo styles)
	float chaosDPrev_[2]         = {};
	float chaosDCurr_[2]         = {};
	float chaosDNext_[2]         = {};
	float chaosDPhase_[2]        = {};
	float chaosDDriftPhase_[2]   = {};
	float chaosDDriftFreqHz_[2]  = {};
	float chaosDOut_[2]          = {};
	juce::Random chaosDRng_[2];

	// CHS D smooth S&H + Drift: gain (per-channel, decorrelated)
	float chaosGPrev_[2]         = {};
	float chaosGCurr_[2]         = {};
	float chaosGNext_[2]         = {};
	float chaosGPhase_[2]        = {};
	float chaosGDriftPhase_[2]   = {};
	float chaosGDriftFreqHz_[2]  = {};
	float chaosGOut_[2]          = {};
	juce::Random chaosGRng_[2];

	// CHS F parameters
	float chaosAmtF_                  = 0.0f;
	float chaosShPeriodF_             = 8820.0f;
	float smoothedChaosShPeriodF_     = 8820.0f;
	float chaosFilterMaxOct_          = 0.0f;
	float smoothedChaosFilterMaxOct_  = 0.0f;
	float chaosFilterAmtSmoothed_     = 0.0f;
	float chaosFilterSpdSmoothed_     = 0.0f;
	bool  chaosFilterParamSmoothReady_ = false;

	// CHS F smooth S&H + Drift: filter (mono S&H + quadrature drift)
	float chaosFPrev_            = 0.0f;
	float chaosFCurr_            = 0.0f;
	float chaosFNext_            = 0.0f;
	float chaosFPhase_           = 0.0f;
	float chaosFDriftPhase_      = 0.0f;   // single phase; R = +90° offset
	float chaosFDriftFreqHz_     = 0.0f;
	float chaosFOut_[2]          = {};     // [0]=L, [1]=R (quadrature when stereo)
	juce::Random chaosFRng_;

	// Chaos per-sample param smoothing
	float chaosParamSmoothCoeff_ = 0.999f;

	// Precomputed sampleRate-dependent smooth coefficients (set in prepareToPlay)
	float cachedChaosParamSmoothCoeff_   = 0.999f;
	float chaosDelaySmoothStep_          = 0.001f;

	// JIT: tape-style timing and feedback instability, independent from CHS F/D.
	float jitterTargetNorm_              = 0.0f;
	float jitterAmountSmoothed_          = 0.0f;
	float jitterParamSmoothCoeff_        = 0.999f;
	bool  jitterParamSmoothReady_        = false;
	bool  jitterActive_                  = false;

	float jitterDelayPrev_[2]            = {};
	float jitterDelayCurr_[2]            = {};
	float jitterDelayNext_[2]            = {};
	float jitterDelayPhase_[2]           = {};
	float jitterDelayDriftPhase_[2]      = {};
	float jitterDelayDriftFreqHz_[2]     = {};
	float jitterDelayFastPrev_[2]        = {};
	float jitterDelayFastCurr_[2]        = {};
	float jitterDelayFastNext_[2]        = {};
	float jitterDelayFastPhase_[2]       = {};
	float jitterDelayFastDriftPhase_[2]  = {};
	float jitterDelayFastDriftFreqHz_[2] = {};
	float jitterTonePhase_[2]            = {};
	float jitterToneRateHz_[2]           = {};
	float jitterDelayOut_[2]             = {};
	float jitterDelayDepthOct_[2]        = {};
	float jitterDelayOffsetSamples_[2]   = {};
	bool  jitterDelayOffsetReady_[2]     = {};
	juce::Random jitterDelayRng_[2];
	juce::Random jitterDelayFastRng_[2];

	float jitterFeedbackPrev_            = 0.0f;
	float jitterFeedbackCurr_            = 0.0f;
	float jitterFeedbackNext_            = 0.0f;
	float jitterFeedbackPhase_           = 0.0f;
	float jitterFeedbackDriftPhase_      = 0.0f;
	float jitterFeedbackDriftFreqHz_     = 0.0f;
	float jitterFeedbackFastPrev_        = 0.0f;
	float jitterFeedbackFastCurr_        = 0.0f;
	float jitterFeedbackFastNext_        = 0.0f;
	float jitterFeedbackFastPhase_       = 0.0f;
	float jitterFeedbackFastDriftPhase_  = 0.0f;
	float jitterFeedbackFastDriftFreqHz_ = 0.0f;
	float jitterFeedbackOut_             = 0.0f;
	float jitterFeedbackDepth_           = 0.0f;
	juce::Random jitterFeedbackRng_;
	juce::Random jitterFeedbackFastRng_;
	void resetJitterState() noexcept;

	// Chaos micro-delay buffer (post-wet, CAB-TR style)
	static constexpr int kChaosDelayBufLen = 1024;
	float chaosDelayBuf_[2][kChaosDelayBufLen] = {};
	int   chaosDelayWritePos_ = 0;

	static constexpr float kChaosDriftAmp = 0.3f;
	static constexpr float kTwoPi = 6.283185307f;

	// Dual-stage transparent limiter state (stereo-linked)
	static constexpr float kLimFloor = 1.0e-12f;
	float limEnv1_[2] = { kLimFloor, kLimFloor };
	float limEnv2_[2] = { kLimFloor, kLimFloor };
	float limAtt1_ = 0.0f;     // Stage 1 attack coefficient (~2 ms)
	float limRel1_ = 0.0f;     // Stage 1 release coefficient (~10 ms)
	float limRel2_ = 0.0f;     // Stage 2 release coefficient (~100 ms)
	bool  wetLimiterActive_ = false;  // true when limMode == WET (set per-block)
	float limThreshLin_     = 1.0f;   // current smoothed linear threshold
	float limThreshTargetLin_ = 1.0f;
	float limThreshStep_    = 0.0f;

	inline float nextLimiterThreshold() noexcept
	{
		const float current = limThreshLin_;
		limThreshLin_ += limThreshStep_;
		return current;
	}

	static inline float dcBlockSample (float in, float& inState, float& outState, float r) noexcept
	{
		outState = r * (outState + in - inState);
		inState = in;
		if (! (outState > -1.0e15f && outState < 1.0e15f))
		{
			outState = 0.0f;
			inState = 0.0f;
		}
		return outState;
	}

	inline void applyFeedbackDcBlock (float& sampleL, float& sampleR) noexcept
	{
		sampleL = dcBlockSample (sampleL, fbkDcStateInL, fbkDcStateOutL, fbkDcCoeff);
		sampleR = dcBlockSample (sampleR, fbkDcStateInR, fbkDcStateOutR, fbkDcCoeff);
	}

	inline void applyWetDcBlock (float& sampleL, float& sampleR) noexcept
	{
		sampleL = dcBlockSample (sampleL, wetDcStateInL, wetDcStateOutL, fbkDcCoeff);
		sampleR = dcBlockSample (sampleR, wetDcStateInR, wetDcStateOutR, fbkDcCoeff);
	}

	inline void applyOutputDcBlock (float& sampleL, float& sampleR) noexcept
	{
		sampleL = dcBlockSample (sampleL, outDcStateInL, outDcStateOutL, fbkDcCoeff);
		sampleR = dcBlockSample (sampleR, outDcStateInR, outDcStateOutR, fbkDcCoeff);
	}

	// Engine feedback-loop processing state
	int   engineMode_ = 0;            // 0=CLEAN, 1=SAT1, 2=SAT2
	int   prevEngineMode_ = 0;        // previous mode for LP state reset on switch
	int   engineXfadePos_ = 0;        // crossfade position (samples elapsed)
	int   engineXfadeLen_ = 0;        // crossfade length (0 = not active)
	float smoothedSatDrive_ = 0.5f;   // normalised 0..1 (50% default)
	float smoothedSatGrit_  = 0.0f;   // normalised 0..1 (0% default)
	float engineLp1StateL_ = 0.0f;    // 1st-pole LP filter state (left)
	float engineLp1StateR_ = 0.0f;    // 1st-pole LP filter state (right)
	float engineLp2StateL_ = 0.0f;    // 2nd-pole LP filter state (left)
	float engineLp2StateR_ = 0.0f;    // 2nd-pole LP filter state (right)
	float engineLpCoeff_ = 0.0f;      // 1-pole LP coeff (precomputed per block)
	float engineNoteHoldSmoothed_ = 0.0f; // opens in-loop LP for high-feedback short-delay notes
	float engineFeedbackMakeup_ = 1.0f;   // near-max feedback compensation for lossy analog modes

	// G-domain feedback smoothing (rate-invariant)
	// Instead of smoothing the per-pass feedback coefficient (which at short
	// delays compounds hundreds of times per second killing the tail), we
	// Delay-time compensated feedback: normalize the per-pass coefficient
	// so the audible decay rate (dB/s) is constant regardless of delay time.
	// smoothedFeedback_ is per-sample EMA'd toward the compensated target.
	float smoothedFeedback_ = 0.0f;

	// ── Temporary feedback diagnostic dump ──
	FILE* fbkDumpFile_ = nullptr;
	double fbkDumpT0_ = 0.0;           // start time (ms)
	int    fbkDumpBlockCount_ = 0;     // block counter

	// Duck envelope follower (Valhalla-style 1-knob ducking)
	float smoothedDuck_     = 0.0f;   // EMA-smoothed duck amount (0-1)
	float duckEnvelope_     = 0.0f;   // peak envelope of input signal
	float duckAttackCoeff_  = 0.0f;   // ~0.5 ms attack
	float duckReleaseCoeff_ = 0.0f;   // ~250 ms release
	float duckAmount_       = 0.0f;   // target duck depth (0-1, set per block)

	// Engine micro-oscillation — mode-dependent drift
	// SAT1: Slow wow (~2 Hz) modulating gain ±0.5 dB via sine LFO
	// SAT2: Faster random clock jitter (~15 Hz S&H) ±0.2 dB
	float engineDriftPhase_    = 0.0f;
	float engineDriftPeriod_   = 4800.0f;  // samples per cycle/step
	float engineDriftTarget_   = 1.0f;     // raw target (gain multiplier)
	float engineDriftSmoothed_ = 1.0f;     // EMA-smoothed gain multiplier
	float engineDriftSmoothCoeff_ = 0.9995f; // EMA coeff

	// Progressive AC-coupling HP in feedback (both SAT1 & SAT2)
	// Simulates coupling caps in the preamp / record-driver stages.
	// Each recirculation attenuates LF, making tails progressively thinner.
	float engineHpStateL_ = 0.0f;
	float engineHpStateR_ = 0.0f;
	float engineHpCoeff_  = 0.0f;  // 1-pole IIR coeff (precomputed per block)

	// Commercial repeat low-cut inside the feedback loop.
	// Unlike the subsonic AC-coupling stage above, this is a partial high-pass
	// blend in the 250-700 Hz region. It thins analog repeats like tape/BBD
	// delay plugins while preserving near-unity loop gain above the cutoff.
	float engineToneHpStateL_ = 0.0f;
	float engineToneHpStateR_ = 0.0f;
	float engineToneHpCoeff_  = 0.0f;
	float engineToneHpMix_    = 0.0f;

	// SAT1 head bump (peaking biquad at ~80 Hz, +2 dB, Q≈0.8)
	// Tape head proximity resonance — adds low-end warmth per repeat
	float engineHbB0_ = 1.0f, engineHbB1_ = 0.0f, engineHbB2_ = 0.0f;
	float engineHbA1_ = 0.0f, engineHbA2_ = 0.0f;
	float engineHbZL_[2] = {0.0f, 0.0f};  // Transposed DF2 state L
	float engineHbZR_[2] = {0.0f, 0.0f};  // Transposed DF2 state R

	// SAT1 secondary flutter LFO (~0.4 Hz slow drift)
	// Complements the primary 2 Hz wow with a slower tape-wander component
	float engineFlutter2Phase_  = 0.0f;
	float engineFlutter2Period_ = 110250.0f;  // SR / 0.4

	// SAT1 pre-saturation anti-aliasing LP (~10 kHz) — reduces HF before
	// the in-loop tube waveshaper to minimise aliasing without oversampling.
	float enginePreSatLpL_ = 0.0f;
	float enginePreSatLpR_ = 0.0f;
	float enginePreSatLpCoeff_ = 0.0f;

	// Output-stage anti-aliasing LP — band-limits signal before output
	// waveshaper (SAT1 ~16 kHz, SAT2 ~10 kHz).
	float engineOutAaLpL_ = 0.0f;
	float engineOutAaLpR_ = 0.0f;
	float engineOutAaLpCoeff_ = 0.0f;

	// Post-waveshaper 2-pole Butterworth LP (catches harmonics generated
	// by waveshapers that can't use ADAA). SAT1 ~16 kHz, SAT2 ~12 kHz.
	float enginePostWsB0_ = 1.0f, enginePostWsB1_ = 0.0f, enginePostWsB2_ = 0.0f;
	float enginePostWsA1_ = 0.0f, enginePostWsA2_ = 0.0f;
	float enginePostWsZL_[2] = {0.0f, 0.0f};  // TDF2 state L
	float enginePostWsZR_[2] = {0.0f, 0.0f};  // TDF2 state R

	// ADAA (1st-order antiderivative anti-aliasing) state for tanh grit
	// ADAA1: y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
	// where F(x) = (1/k)·ln(cosh(k·x)) for tanh(k·x)
	float adaaTanhPrevL_ = 0.0f;
	float adaaTanhPrevR_ = 0.0f;
	float adaaTanhAd1PrevL_ = 0.0f;  // F(x[n-1]) left
	float adaaTanhAd1PrevR_ = 0.0f;  // F(x[n-1]) right

	// ADAA state for SAT2 feedback-loop sin(π/2·|x|) waveshaper
	float adaaSinFbkPrevL_ = 0.0f;
	float adaaSinFbkPrevR_ = 0.0f;
	float adaaSinFbkAd1PrevL_ = 0.0f;
	float adaaSinFbkAd1PrevR_ = 0.0f;

	// ADAA state for SAT2 grit wavefolding sin(π·x)
	float adaaFoldPrevL_ = 0.0f;
	float adaaFoldPrevR_ = 0.0f;
	float adaaFoldAd1PrevL_ = 0.0f;
	float adaaFoldAd1PrevR_ = 0.0f;

	// Saturation drive modulation — S&H with EMA smoothing
	// Varies the saturation intensity per-sample for analog "alive" feel.
	// SAT1: ~1.5 Hz tape bias drift, SAT2: ~3 Hz BBD clock instability.
	// Range ±2.5% around unity [0.975, 1.025].
	float engineSatModPhase_     = 0.0f;
	float engineSatModPeriod_    = 32000.0f;  // samples per S&H step
	float engineSatModTarget_    = 1.0f;
	float engineSatModSmoothed_  = 1.0f;
	float engineSatModSmoothCoeff_ = 0.999f;

	// Gain jitter — S&H with EMA smoothing, amplitude scales with drive²
	// SAT1: ~1.5 Hz tape-bias voltage drift, ±1.5 dB max
	// SAT2: ~8 Hz BBD clock instability, ±1.0 dB max
	float engineGainJitterPhase_     = 0.0f;
	float engineGainJitterPeriod_    = 29400.0f;  // samples per S&H step (~1.5 Hz @ 44.1k)
	float engineGainJitterTarget_    = 1.0f;
	float engineGainJitterSmoothed_  = 1.0f;
	float engineGainJitterSmoothCoeff_ = 0.999f;  // ~50 ms EMA

	// SAT2 compander envelope (stereo-linked, attack/release timing)
	// Real NE571 compander has ~1 ms attack, ~10 ms release
	float engineCompEnv_     = 0.0f;
	float engineCompAttack_  = 0.0f;  // attack coeff (~1 ms)
	float engineCompRelease_ = 0.0f;  // release coeff (~10 ms)

	// SAT2 noise RNG (BBD thermal + clock noise)
	juce::Random engineNoiseRng_;
	float engineNoiseScale_ = 1.0f;  // raw |feedback| for noise level scaling

	// Diagnostic: last-sample values for dump
	float dbgCompEnv_       = 0.0f;  // compander envelope
	float dbgPostEnginePk_  = 0.0f;  // peak after applyEngineToFeedback (pre-fbkMag)
	float dbgPostFbkMagPk_  = 0.0f;  // peak after fbkMag multiplication
	float dbgFbkMag_        = 0.0f;  // actual fbkMag applied
	float dbgDelayBufPk_    = 0.0f;  // peak written to delay buffer

	// Per-sample dual-stage transparent limiter (stereo-linked)
	// Stage 1: Leveler (2ms att, 10ms rel) — gradual gain reduction
	// Stage 2: Brickwall (instant att, 100ms rel) — catches transients
	inline void applyLimiter (float& sampleL, float& sampleR, float threshLin) noexcept
	{
		const float peakL = std::abs (sampleL);
		const float peakR = std::abs (sampleR);

		// Stage 1 — leveler (2 ms attack, 10 ms release)
		for (int ch = 0; ch < 2; ++ch)
		{
			const float p = (ch == 0) ? peakL : peakR;
			if (p > limEnv1_[ch])
				limEnv1_[ch] = limAtt1_ * limEnv1_[ch] + (1.0f - limAtt1_) * p;
			else
				limEnv1_[ch] = limRel1_ * limEnv1_[ch] + (1.0f - limRel1_) * p;
			if (limEnv1_[ch] < kLimFloor) limEnv1_[ch] = kLimFloor;
		}

		// Stage 2 — brickwall (instant attack, 100 ms release)
		for (int ch = 0; ch < 2; ++ch)
		{
			const float p = (ch == 0) ? peakL : peakR;
			if (p > limEnv2_[ch])
				limEnv2_[ch] = p;
			else
				limEnv2_[ch] = limRel2_ * limEnv2_[ch] + (1.0f - limRel2_) * p;
			if (limEnv2_[ch] < kLimFloor) limEnv2_[ch] = kLimFloor;
		}

		// Stereo-linked gain reduction
		float gr = 1.0f;
		const float maxEnv1 = juce::jmax (limEnv1_[0], limEnv1_[1]);
		const float maxEnv2 = juce::jmax (limEnv2_[0], limEnv2_[1]);
		if (maxEnv1 > threshLin)
			gr = juce::jmin (gr, threshLin / maxEnv1);
		if (maxEnv2 > threshLin)
			gr = juce::jmin (gr, threshLin / maxEnv2);

		sampleL *= gr;
		sampleR *= gr;
	}

	// Per-sample duck envelope follower — returns duck gain [0,1]
	static constexpr float kDuckSmoothCoeff_ = 0.9955f; // same as kGainSmoothCoeff (~5 ms @ 44.1 kHz)
	inline float advanceDuck (float inL, float inR) noexcept
	{
		smoothedDuck_ = smoothedDuck_ * kDuckSmoothCoeff_ + duckAmount_ * (1.0f - kDuckSmoothCoeff_);
		const float peakIn = juce::jmax (std::abs (inL), std::abs (inR));
		const float dCoeff = (peakIn > duckEnvelope_) ? duckAttackCoeff_ : duckReleaseCoeff_;
		duckEnvelope_ += dCoeff * (peakIn - duckEnvelope_);
		return juce::jmax (0.0f, 1.0f - smoothedDuck_ * duckEnvelope_);
	}

	// Generic smooth S&H + Drift chaos engine (per-sample advance)
	inline void advanceChaosEngine (
		float& prev, float& curr, float& next, float& phase,
		float& driftPhase, float& driftFreqHz, float& output,
		juce::Random& rng, float period, float amtNorm, float sr) noexcept
	{
		const float safePeriod = juce::jmax (1.0f, period);
		phase += 1.0f / safePeriod;
		if (phase >= 1.0f)
		{
			phase -= std::floor (phase);
			prev = curr;
			curr = next;
			next = rng.nextFloat() * 2.0f - 1.0f;
			const float driftBase = sr / safePeriod * 0.37f;
			driftFreqHz = driftBase * (0.88f + rng.nextFloat() * 0.24f);
		}
		const float t = juce::jlimit (0.0f, 1.0f, phase);
		const float t2 = t * t;
		const float t3 = t2 * t;
		const float u = t3 * (t * (t * 6.0f - 15.0f) + 10.0f);
		const float shValue = curr + (next - curr) * u;

		driftPhase += driftFreqHz / sr;
		if (driftPhase > 1e6f) driftPhase -= 1e6f;
		const float driftValue = std::sin (driftPhase * kTwoPi) * kChaosDriftAmp;

		const float shWeight = juce::jlimit (0.0f, 1.0f, amtNorm * 1.5f - 0.15f);
		output = driftValue + shValue * shWeight;
	}

	static float smoothStep01 (float x) noexcept
	{
		const float t = juce::jlimit (0.0f, 1.0f, x);
		return t * t * (3.0f - 2.0f * t);
	}

	struct JitterMetrics
	{
		float amountMapped = 0.0f;
		float delayMs = 1.0f;
		float shortness = 0.0f;
		float longness = 0.0f;
		float driftRateHz = 0.1f;
		float flutterRateHz = 4.0f;
		float toneRateHz = 0.0f;
		float driftWeight = 0.4f;
		float flutterWeight = 0.5f;
		float toneWeight = 0.0f;
		float delayDepthOct = 0.0f;
		float feedbackDepth = 0.0f;
	};

	inline JitterMetrics makeJitterMetrics (float baseDelaySamples, float amount, float sr, int channel) const noexcept
	{
		JitterMetrics m;
		const float a = juce::jlimit (0.0f, 1.0f, amount);
		m.amountMapped = a;

		m.delayMs = juce::jmax (0.05f, juce::jmax (2.0f, baseDelaySamples) * 1000.0f / sr);
		const float delaySeconds = m.delayMs * 0.001f;
		const float delayHz = 1.0f / delaySeconds;

		constexpr float kShortRefMs = 8.0f;
		constexpr float kMidRefMs = 500.0f;
		constexpr float kLongRefMs = 4000.0f;
		m.shortness = juce::jlimit (0.0f, 1.0f,
			std::log2 (kMidRefMs / m.delayMs) / std::log2 (kMidRefMs / kShortRefMs));
		m.longness = juce::jlimit (0.0f, 1.0f,
			std::log2 (m.delayMs / 250.0f) / std::log2 (kLongRefMs / 250.0f));

		const float high = smoothStep01 ((m.amountMapped - 0.55f) / 0.45f);
		const float extreme = smoothStep01 ((m.amountMapped - 0.82f) / 0.18f);

		m.driftRateHz = (0.08f + (1.20f - 0.08f) * m.amountMapped)
		              * (1.0f - 0.65f * m.longness)
		              * (1.0f + 0.10f * m.shortness);
		m.driftRateHz = juce::jlimit (0.03f, 2.0f, m.driftRateHz);

		m.flutterRateHz = (4.0f + (130.0f - 4.0f) * m.amountMapped)
		                * std::pow (250.0f / m.delayMs, 0.90f);
		m.flutterRateHz = juce::jlimit (2.0f, 7000.0f, m.flutterRateHz);

		const float toneAmount = smoothStep01 ((m.amountMapped - 0.18f) / 0.82f);
		const float toneLift = 4.0f + 190.0f * m.amountMapped + high * 60.0f + extreme * 90.0f;
		const float toneShort = std::pow (m.shortness, 0.70f);
		const float harmonic = (channel == 0) ? 1.0f : 1.618f;
		const float toneCeilHz = juce::jmin (12000.0f, sr * 0.22f);
		m.toneRateHz = juce::jlimit (0.0f, toneCeilHz, delayHz * toneLift * toneShort * harmonic);

		m.driftWeight = juce::jlimit (0.18f, 0.72f, 0.42f + 0.30f * m.longness - 0.14f * m.shortness);
		m.flutterWeight = juce::jlimit (0.35f, 0.95f, 0.45f + 0.38f * m.shortness + 0.12f * high);
		m.toneWeight = toneAmount * std::pow (m.shortness, 0.55f) * (0.35f + 0.55f * m.amountMapped);
		m.toneWeight = juce::jlimit (0.0f, 0.78f, m.toneWeight);

		const float targetDepthRatio = 0.055f * std::pow (m.amountMapped, 1.05f);
		const float maxDepthSeconds = delaySeconds * 0.12f;
		const float depthSeconds = juce::jlimit (1.0e-7f, maxDepthSeconds, delaySeconds * targetDepthRatio);
		m.delayDepthOct = std::log2 ((delaySeconds + depthSeconds) / delaySeconds);

		m.feedbackDepth = (0.010f + 0.055f * m.amountMapped) * m.amountMapped
		                * (1.0f + 0.20f * m.shortness);
		return m;
	}

	inline void advanceJitter (float baseDelaySamplesL, float baseDelaySamplesR) noexcept
	{
		const float sr = juce::jmax (1.0f, (float) currentSampleRate);
		const float smoothStep = 1.0f - jitterParamSmoothCoeff_;
		const float target = juce::jlimit (0.0f, 1.0f, jitterTargetNorm_);

		if (! jitterParamSmoothReady_)
		{
			jitterParamSmoothReady_ = true;
		}
		else
		{
			jitterAmountSmoothed_ += (target - jitterAmountSmoothed_) * smoothStep;
		}

		if (target <= 0.000001f && jitterAmountSmoothed_ <= 0.000001f)
		{
			jitterAmountSmoothed_ = 0.0f;
			jitterParamSmoothReady_ = false;
			jitterActive_ = false;
			jitterDelayOut_[0] = jitterDelayOut_[1] = 0.0f;
			jitterDelayDepthOct_[0] = jitterDelayDepthOct_[1] = 0.0f;
			jitterDelayOffsetSamples_[0] = jitterDelayOffsetSamples_[1] = 0.0f;
			jitterDelayOffsetReady_[0] = jitterDelayOffsetReady_[1] = false;
			jitterToneRateHz_[0] = jitterToneRateHz_[1] = 0.0f;
			jitterFeedbackOut_ = 0.0f;
			jitterFeedbackDepth_ = 0.0f;
			return;
		}

		const float amt = juce::jlimit (0.0f, 1.0f, jitterAmountSmoothed_);
		const int nCh = chaosStereo_ ? 2 : 1;
		const float baseDelaySamples[2] = { baseDelaySamplesL, baseDelaySamplesR };
		const float toneRateSmooth = std::exp (-1.0f / (sr * 0.006f));

		for (int ch = 0; ch < nCh; ++ch)
		{
			float slowOut = 0.0f;
			float fastOut = 0.0f;
			const JitterMetrics metrics = makeJitterMetrics (baseDelaySamples[ch], amt, sr, ch);
			const float slowPeriodForDelay = sr / juce::jmax (0.01f, metrics.driftRateHz);
			const float fastPeriod = sr / juce::jmax (0.01f, metrics.flutterRateHz);

			advanceChaosEngine (jitterDelayPrev_[ch], jitterDelayCurr_[ch], jitterDelayNext_[ch],
			                    jitterDelayPhase_[ch], jitterDelayDriftPhase_[ch],
			                    jitterDelayDriftFreqHz_[ch], slowOut, jitterDelayRng_[ch],
			                    slowPeriodForDelay, metrics.amountMapped, sr);
			advanceChaosEngine (jitterDelayFastPrev_[ch], jitterDelayFastCurr_[ch], jitterDelayFastNext_[ch],
			                    jitterDelayFastPhase_[ch], jitterDelayFastDriftPhase_[ch],
			                    jitterDelayFastDriftFreqHz_[ch], fastOut, jitterDelayFastRng_[ch],
			                    fastPeriod, metrics.amountMapped, sr);

			float toneOut = 0.0f;
			if (metrics.toneWeight > 0.000001f && metrics.toneRateHz > 0.0f)
			{
				if (jitterToneRateHz_[ch] <= 0.0f)
					jitterToneRateHz_[ch] = metrics.toneRateHz;
				else
					jitterToneRateHz_[ch] = jitterToneRateHz_[ch] * toneRateSmooth
					                      + metrics.toneRateHz * (1.0f - toneRateSmooth);

				jitterTonePhase_[ch] += jitterToneRateHz_[ch] / sr;
				jitterTonePhase_[ch] -= std::floor (jitterTonePhase_[ch]);

				const float phase = jitterTonePhase_[ch] * kTwoPi;
				toneOut = std::sin (phase) * 0.72f
				        + std::sin (phase * 2.0f + (ch == 0 ? 0.73f : 1.37f)) * 0.20f
				        + std::sin (phase * 3.0f + (ch == 0 ? 1.91f : 2.47f)) * 0.08f;
			}
			else
			{
				jitterToneRateHz_[ch] = 0.0f;
			}

			const float combined = slowOut * metrics.driftWeight
			                     + fastOut * metrics.flutterWeight
			                     + toneOut * metrics.toneWeight;
			jitterDelayOut_[ch] = juce::jlimit (-1.25f, 1.25f, combined);
			jitterDelayDepthOct_[ch] = metrics.delayDepthOct;
		}

		if (! chaosStereo_)
		{
			jitterDelayOut_[1] = jitterDelayOut_[0];
			jitterDelayDepthOct_[1] = jitterDelayDepthOct_[0];
		}

		float feedbackSlow = 0.0f;
		float feedbackFast = 0.0f;
		const float feedbackDelaySamples = juce::jmax (2.0f, (baseDelaySamplesL + baseDelaySamplesR) * 0.5f);
		const JitterMetrics feedbackMetrics = makeJitterMetrics (feedbackDelaySamples, amt, sr, 0);
		advanceChaosEngine (jitterFeedbackPrev_, jitterFeedbackCurr_, jitterFeedbackNext_,
		                    jitterFeedbackPhase_, jitterFeedbackDriftPhase_, jitterFeedbackDriftFreqHz_,
		                    feedbackSlow, jitterFeedbackRng_,
		                    sr / juce::jmax (0.01f, feedbackMetrics.driftRateHz * 0.80f),
		                    feedbackMetrics.amountMapped, sr);
		advanceChaosEngine (jitterFeedbackFastPrev_, jitterFeedbackFastCurr_, jitterFeedbackFastNext_,
		                    jitterFeedbackFastPhase_, jitterFeedbackFastDriftPhase_, jitterFeedbackFastDriftFreqHz_,
		                    feedbackFast, jitterFeedbackFastRng_,
		                    sr / juce::jmax (0.01f, feedbackMetrics.flutterRateHz * 0.55f),
		                    feedbackMetrics.amountMapped, sr);
		jitterFeedbackOut_ = juce::jlimit (-1.0f, 1.0f,
			feedbackSlow * 0.62f + feedbackFast * (0.24f + feedbackMetrics.shortness * 0.28f));
		jitterFeedbackDepth_ = feedbackMetrics.feedbackDepth * 0.60f;
	}

	inline float getJitterDelayMultiplier (int channel, float baseDelaySamples) const noexcept
	{
		const float amt = juce::jlimit (0.0f, 1.0f, jitterAmountSmoothed_);
		if (! jitterActive_ || amt <= 0.000001f)
			return 1.0f;

		const int lane = juce::jlimit (0, 1, channel);
		(void) baseDelaySamples;
		return std::exp2 (jitterDelayOut_[lane] * jitterDelayDepthOct_[lane]);
	}

	inline float getJitteredReadDelaySamples (int channel, float baseDelaySamples) noexcept
	{
		const int lane = juce::jlimit (0, 1, channel);
		const float baseDelay = juce::jmax (2.0f, baseDelaySamples);
		const float amt = juce::jlimit (0.0f, 1.0f, jitterAmountSmoothed_);

		if (! jitterActive_ || amt <= 0.000001f)
		{
			jitterDelayOffsetSamples_[0] = jitterDelayOffsetSamples_[1] = 0.0f;
			jitterDelayOffsetReady_[0] = jitterDelayOffsetReady_[1] = false;
			return baseDelay;
		}

		const float targetOffset = baseDelay * (getJitterDelayMultiplier (lane, baseDelay) - 1.0f);
		if (! jitterDelayOffsetReady_[lane])
		{
			jitterDelayOffsetSamples_[lane] = targetOffset;
			jitterDelayOffsetReady_[lane] = true;
		}
		else
		{
			const float maxOffsetStep = 0.12f + 0.78f * amt;
			const float delta = targetOffset - jitterDelayOffsetSamples_[lane];
			jitterDelayOffsetSamples_[lane] += maxOffsetStep * std::tanh (delta / maxOffsetStep);
		}

		return juce::jmax (2.0f, baseDelay + jitterDelayOffsetSamples_[lane]);
	}

	inline float applyJitterToFeedbackMagnitude (float feedbackMagnitude) const noexcept
	{
		const float amt = juce::jlimit (0.0f, 1.0f, jitterAmountSmoothed_);
		if (! jitterActive_ || amt <= 0.000001f || feedbackMagnitude <= 0.0f)
			return feedbackMagnitude;

		return juce::jlimit (0.0f, 1.0f, feedbackMagnitude * (1.0f + jitterFeedbackOut_ * jitterFeedbackDepth_));
	}

	inline void advanceChaosD() noexcept
	{
		const float sr = (float) currentSampleRate;
		const float smoothStep = 1.0f - chaosParamSmoothCoeff_;
		const float targetAmt = juce::jlimit (kChaosAmtMin, kChaosAmtMax, chaosAmtD_);
		const float targetSpd = juce::jlimit (kChaosSpdMin, kChaosSpdMax, sr / juce::jmax (1.0f, chaosShPeriodD_));

		if (! chaosDriveParamSmoothReady_)
		{
			chaosDriveParamSmoothReady_ = true;
			if (chaosDriveSpdSmoothed_ <= 0.0f)
				chaosDriveSpdSmoothed_ = targetSpd;
		}

		chaosDriveAmtSmoothed_ += (targetAmt - chaosDriveAmtSmoothed_) * smoothStep;
		const float spdLog = std::log (juce::jmax (kChaosSpdMin, chaosDriveSpdSmoothed_));
		const float targetSpdLog = std::log (targetSpd);
		chaosDriveSpdSmoothed_ = std::exp (spdLog + (targetSpdLog - spdLog) * smoothStep);

		chaosAmtNormD_ = chaosDriveAmtSmoothed_ * 0.01f;
		smoothedChaosDelayMaxSamples_ = chaosAmtNormD_ * 0.005f * sr;
		smoothedChaosGainMaxDb_ = chaosAmtNormD_ * 1.0f;
		smoothedChaosShPeriodD_ = sr / juce::jmax (kChaosSpdMin, chaosDriveSpdSmoothed_);

		const float period = smoothedChaosShPeriodD_;
		const int nCh = chaosStereo_ ? 2 : 1;

		for (int c = 0; c < nCh; ++c)
		{
			advanceChaosEngine (chaosDPrev_[c], chaosDCurr_[c], chaosDNext_[c], chaosDPhase_[c],
				chaosDDriftPhase_[c], chaosDDriftFreqHz_[c], chaosDOut_[c],
				chaosDRng_[c], period, chaosAmtNormD_, sr);

			advanceChaosEngine (chaosGPrev_[c], chaosGCurr_[c], chaosGNext_[c], chaosGPhase_[c],
				chaosGDriftPhase_[c], chaosGDriftFreqHz_[c], chaosGOut_[c],
				chaosGRng_[c], period, chaosAmtNormD_, sr);
		}

		// Mono chaos: copy ch0 to ch1.
		if (! chaosStereo_)
		{
			chaosDOut_[1] = chaosDOut_[0];
			chaosGOut_[1] = chaosGOut_[0];
		}
	}

	inline void advanceChaosF() noexcept
	{
		const float sr       = (float) currentSampleRate;
		const float smoothStep = 1.0f - chaosParamSmoothCoeff_;
		const float targetAmt = juce::jlimit (kChaosAmtMin, kChaosAmtMax, chaosAmtF_);
		const float targetSpd = juce::jlimit (kChaosSpdMin, kChaosSpdMax, sr / juce::jmax (1.0f, chaosShPeriodF_));

		if (! chaosFilterParamSmoothReady_)
		{
			chaosFilterParamSmoothReady_ = true;
			if (chaosFilterSpdSmoothed_ <= 0.0f)
				chaosFilterSpdSmoothed_ = targetSpd;
		}

		chaosFilterAmtSmoothed_ += (targetAmt - chaosFilterAmtSmoothed_) * smoothStep;
		const float spdLog = std::log (juce::jmax (kChaosSpdMin, chaosFilterSpdSmoothed_));
		const float targetSpdLog = std::log (targetSpd);
		chaosFilterSpdSmoothed_ = std::exp (spdLog + (targetSpdLog - spdLog) * smoothStep);

		const float amtNormF = chaosFilterAmtSmoothed_ * 0.01f;
		smoothedChaosFilterMaxOct_ = amtNormF * 2.0f;
		smoothedChaosShPeriodF_ = sr / juce::jmax (kChaosSpdMin, chaosFilterSpdSmoothed_);
		const float period = smoothedChaosShPeriodF_;

		const float safePeriod = juce::jmax (1.0f, period);
		chaosFPhase_ += 1.0f / safePeriod;
		if (chaosFPhase_ >= 1.0f)
		{
			chaosFPhase_ -= std::floor (chaosFPhase_);
			chaosFPrev_ = chaosFCurr_;
			chaosFCurr_ = chaosFNext_;
			chaosFNext_ = chaosFRng_.nextFloat() * 2.0f - 1.0f;
			const float driftBase = sr / safePeriod * 0.37f;
			chaosFDriftFreqHz_ = driftBase * (0.88f + chaosFRng_.nextFloat() * 0.24f);
		}

		const float t = juce::jlimit (0.0f, 1.0f, chaosFPhase_);
		const float t2 = t * t;
		const float t3 = t2 * t;
		const float u = t3 * (t * (t * 6.0f - 15.0f) + 10.0f);
		const float shValue = chaosFCurr_ + (chaosFNext_ - chaosFCurr_) * u;

		chaosFDriftPhase_ += chaosFDriftFreqHz_ / sr;
		if (chaosFDriftPhase_ > 1e6f) chaosFDriftPhase_ -= 1e6f;
		const float driftL = std::sin (chaosFDriftPhase_ * kTwoPi) * kChaosDriftAmp;

		const float shWeight = juce::jlimit (0.0f, 1.0f, amtNormF * 1.5f - 0.15f);
		chaosFOut_[0] = driftL + shValue * shWeight;

		if (chaosStereo_)
		{
			const float driftR = std::sin (chaosFDriftPhase_ * kTwoPi + kTwoPi * 0.25f) * kChaosDriftAmp;
			chaosFOut_[1] = driftR + shValue * shWeight;
		}
		else
		{
			chaosFOut_[1] = chaosFOut_[0];
		}
	}

	inline void applyChaosDelay (float& wetL, float& wetR) noexcept
	{
		const int wp = chaosDelayWritePos_;
		chaosDelayBuf_[0][wp] = wetL;
		chaosDelayBuf_[1][wp] = wetR;

		const float centerDelay = smoothedChaosDelayMaxSamples_;
		const int mask = kChaosDelayBufLen - 1;

		for (int ch = 0; ch < 2; ++ch)
		{
			const float targetDelaySamp = juce::jlimit (0.0f, (float)(kChaosDelayBufLen - 2),
			                                      centerDelay + chaosDOut_[ch] * smoothedChaosDelayMaxSamples_);
			float& delaySamp = chaosDelaySmoothedSamples_[ch];
			if (! chaosDelaySmoothReady_[ch])
			{
				delaySamp = targetDelaySamp;
				chaosDelaySmoothReady_[ch] = true;
			}
			else
			{
				delaySamp += (targetDelaySamp - delaySamp) * chaosDelaySmoothStep_;
			}

			const float readPos = (float) wp - delaySamp;
			const int iPos = (int) std::floor (readPos);
			const float frac = readPos - (float) iPos;

			const float p0 = chaosDelayBuf_[ch][(iPos - 1) & mask];
			const float p1 = chaosDelayBuf_[ch][(iPos    ) & mask];
			const float p2 = chaosDelayBuf_[ch][(iPos + 1) & mask];
			const float p3 = chaosDelayBuf_[ch][(iPos + 2) & mask];
			const float c0 = p1;
			const float c1 = p2 - (1.0f / 3.0f) * p0 - 0.5f * p1 - (1.0f / 6.0f) * p3;
			const float c2 = 0.5f * (p0 + p2) - p1;
			const float c3 = (1.0f / 6.0f) * (p3 - p0) + 0.5f * (p1 - p2);
			float& wet = (ch == 0) ? wetL : wetR;
			wet = ((c3 * frac + c2) * frac + c1) * frac + c0;
		}

		chaosDelayWritePos_ = (wp + 1) & mask;

		// Per-channel gain modulation
		for (int ch = 0; ch < 2; ++ch)
		{
			const float gainDb  = chaosGOut_[ch] * smoothedChaosGainMaxDb_;
			const float ex = gainDb * 0.16609640474f;
			const float exln2 = ex * 0.6931472f;
			const float gainLin = 1.0f + exln2 * (1.0f + exln2 * 0.5f);
			float& wet = (ch == 0) ? wetL : wetR;
			wet *= gainLin;
		}
	}

	// Soft clamp: linear below ±knee, smooth hyperbolic compression above.
	// Approaches ±limit asymptotically — no hard harmonics from clipping.
	// At the join point the derivative is exactly 1.0 (C¹ continuous).
	// Used at the end of the feedback loop to replace the hard ±1.5 clamp
	// which caused abrupt harmonic dropout when head bump self-oscillation
	// crossed the feedback sustain threshold.
	static inline float softClamp (float x) noexcept
	{
		constexpr float knee  = 1.0f;
		constexpr float range = 0.5f;   // limit (1.5) − knee
		if (x > knee)
		{
			const float excess = x - knee;
			return knee + range * excess / (excess + range);
		}
		if (x < -knee)
		{
			const float excess = -x - knee;
			return -(knee + range * excess / (excess + range));
		}
		return x;
	}

	// Engine feedback-loop processing
	// SAT1: AC-coupling HP + head bump + mild saturation + 2-pole LP + dual flutter
	// SAT2: AC-coupling HP + LP + NE571 compander w/ timing + sin() waveshaper + noise + expander
	inline void applyEngineToFeedback (float& fbkL, float& fbkR) noexcept
	{
		if (engineMode_ == 0) return; // CLEAN: bypass

		// ── Mode-dependent drift ──
		engineDriftPhase_ += 1.0f;
		if (engineMode_ == 1)
		{
			// SAT1 primary wow: sine LFO at ~0.8 Hz, ±0.2 dB
			if (engineDriftPhase_ >= engineDriftPeriod_)
				engineDriftPhase_ -= engineDriftPeriod_;
			const float phase = engineDriftPhase_ / engineDriftPeriod_;
			const float x = phase * 2.0f - 1.0f;
			const float x2 = x * x;
			const float sineApprox = x * (1.0f - x2 * (0.16666667f - x2 * 0.00833333f));

			// SAT1 secondary flutter: ~0.15 Hz slow tape-wander, ±0.1 dB
			engineFlutter2Phase_ += 1.0f;
			if (engineFlutter2Phase_ >= engineFlutter2Period_)
				engineFlutter2Phase_ -= engineFlutter2Period_;
			const float ph2 = engineFlutter2Phase_ / engineFlutter2Period_;
			const float x2b = ph2 * 2.0f - 1.0f;
			const float x2b2 = x2b * x2b;
			const float sine2 = x2b * (1.0f - x2b2 * (0.16666667f - x2b2 * 0.00833333f));

			engineDriftSmoothed_ = 1.0f + sineApprox * 0.023f + sine2 * 0.012f;
		}
		else
		{
			// SAT2 clock jitter: S&H at ~4 Hz, ±0.1 dB [0.988, 1.012]
			if (engineDriftPhase_ >= engineDriftPeriod_)
			{
				engineDriftPhase_ -= engineDriftPeriod_;
				engineDriftTarget_ = 0.988f + chaosDRng_[0].nextFloat() * 0.024f;
			}
			engineDriftSmoothed_ = engineDriftSmoothCoeff_ * engineDriftSmoothed_
			                     + (1.0f - engineDriftSmoothCoeff_) * engineDriftTarget_;
		}

		// Input clamp — prevent runaway oscillation
		fbkL = juce::jlimit (-1.5f, 1.5f, fbkL);
		fbkR = juce::jlimit (-1.5f, 1.5f, fbkR);

		// Save dry reference for mode-switch crossfade
		const float xfDryL = fbkL;
		const float xfDryR = fbkR;

		// ── AC-coupling HP — progressive bass loss (coupling caps) ──
		// 1-pole IIR LP computes the DC/LF component; subtracting it gives HP.
		// SAT1 ~30 Hz (tape preamp caps), SAT2 ~20 Hz (BBD larger caps).
		// Accumulates with each recirculation → tails become thinner.
		engineHpStateL_ = engineHpStateL_ * (1.0f - engineHpCoeff_) + fbkL * engineHpCoeff_;
		engineHpStateR_ = engineHpStateR_ * (1.0f - engineHpCoeff_) + fbkR * engineHpCoeff_;
		fbkL -= engineHpStateL_;
		fbkR -= engineHpStateR_;

		// Tonal feedback HP: commercial repeat low-cut.
		// Partial blend, not a full replacement: lows thin out over repeats
		// while mids/highs keep near-unity loop gain at 100% feedback.
		if (engineToneHpMix_ > 0.0001f)
		{
			engineToneHpStateL_ = engineToneHpStateL_ * (1.0f - engineToneHpCoeff_) + fbkL * engineToneHpCoeff_;
			engineToneHpStateR_ = engineToneHpStateR_ * (1.0f - engineToneHpCoeff_) + fbkR * engineToneHpCoeff_;
			const float hpL = fbkL - engineToneHpStateL_;
			const float hpR = fbkR - engineToneHpStateR_;
			fbkL += (hpL - fbkL) * engineToneHpMix_;
			fbkR += (hpR - fbkR) * engineToneHpMix_;
		}

		if (engineMode_ == 1)
		{
			// ── SAT1: Analog tape feedback chain ──

			// Head bump (~80 Hz peaking EQ, +0.15 dB, Q≈0.8)
			// Tape head proximity resonance — adds subtle low-end warmth per repeat.
			// Kept mild (+0.15 dB) because it accumulates inside the feedback loop.
			// AC-coupling HP removes ~0.57 dB/pass at 80 Hz, so net is < 0,
			// preventing self-oscillation at any feedback setting.
			{
				float outL = engineHbB0_ * fbkL + engineHbZL_[0];
				engineHbZL_[0] = engineHbB1_ * fbkL - engineHbA1_ * outL + engineHbZL_[1];
				engineHbZL_[1] = engineHbB2_ * fbkL - engineHbA2_ * outL;
				fbkL = outL;

				float outR = engineHbB0_ * fbkR + engineHbZR_[0];
				engineHbZR_[0] = engineHbB1_ * fbkR - engineHbA1_ * outR + engineHbZR_[1];
				engineHbZR_[1] = engineHbB2_ * fbkR - engineHbA2_ * outR;
				fbkR = outR;
			}

			// Progressive tape-loss LP (~10 kHz, 1-pole)
			// Simulates magnetic-spacing loss: HF is attenuated each pass
			// through the record/playback heads.  At 10 kHz the per-pass
			// loss at 1 kHz is only ~0.04 dB (transparent mid-range), but
			// at 5 kHz it's ~0.8 dB — repeats darken progressively like
			// real tape.  Higher cutoff than SAT2's 5 kHz because tape
			// head losses are gentler than BBD clock-aliasing filters, and
			// SAT1 has no compander to compensate mid-range drain.
			engineLp1StateL_ += engineLpCoeff_ * (fbkL - engineLp1StateL_);
			engineLp1StateR_ += engineLpCoeff_ * (fbkR - engineLp1StateR_);
			fbkL = engineLp1StateL_;
			fbkR = engineLp1StateR_;
		}
		else
		{
			// ── SAT2: BBD feedback chain ──

			// Pre-LP at ~5 kHz (anti-aliasing before S&H stage), 1-pole
			// Reduced from 2-pole to halve per-pass HF loss.  The 2-pole
			// removed ~0.35 dB/pass at 1 kHz — more than the compander
			// could compensate when feedback dropped below 100%, causing
			// abrupt tail collapse.  1-pole (~0.18 dB/pass at 1 kHz)
			// lets the compander maintain smooth decay at all feedback.
			engineLp1StateL_ += engineLpCoeff_ * (fbkL - engineLp1StateL_);
			engineLp1StateR_ += engineLpCoeff_ * (fbkR - engineLp1StateR_);
			fbkL = engineLp1StateL_;
			fbkR = engineLp1StateR_;

			// NE571-style compander with envelope timing.
			// Stereo-linked peak envelope with ~1 ms attack, ~10 ms release.
			// Creates characteristic BBD "pumping" on transients.
			const float peak = juce::jmax (std::abs (fbkL), std::abs (fbkR));
			const float eCoeff = (peak > engineCompEnv_) ? engineCompAttack_ : engineCompRelease_;
			engineCompEnv_ = engineCompEnv_ * eCoeff + peak * (1.0f - eCoeff);
			const float env = juce::jmax (engineCompEnv_, 0.001f);

			// Compress: gain = 2/(1+env) — envelope-driven 2:1 soft ratio
			const float compGain = 2.0f / (1.0f + env);
			fbkL *= compGain;
			fbkR *= compGain;

			// Clamp compressed signal to [-1, 1] before waveshaper.
			fbkL = juce::jlimit (-1.0f, 1.0f, fbkL);
			fbkR = juce::jlimit (-1.0f, 1.0f, fbkR);

			// Save compressed linear values for drive blend
			const float compLinL = fbkL;
			const float compLinR = fbkR;

			// sin(π/2·|x|) waveshaper with ADAA (BBD transfer nonlinearity)
			constexpr float kPiHalf = 1.57079633f;
			fbkL = processAdaaSinHalf (fbkL, adaaSinFbkPrevL_, adaaSinFbkAd1PrevL_);
			fbkR = processAdaaSinHalf (fbkR, adaaSinFbkPrevR_, adaaSinFbkAd1PrevR_);

			// Blend waveshaper with linear by DRIVE:
			// At 0% drive → pure linear (no harmonics from waveshaper)
			// At 100% drive → full sin() waveshaper (original BBD behavior)
			fbkL = compLinL + smoothedSatDrive_ * (fbkL - compLinL);
			fbkR = compLinR + smoothedSatDrive_ * (fbkR - compLinR);

			// Subtle noise floor (BBD thermal + clock noise, ~-74 dBFS)
			// Added after waveshaper, before expander: the expander will
			// reduce noise during quiet passages (correct NE571 behavior)
			// and noise is masked during loud passages.
			// Scale by raw feedback so noise doesn't accumulate at low
			// feedback with delay-time compensation (phantom feedback).
			const float noiseAmp = 2.0e-4f * engineNoiseScale_;
			fbkL += engineNoiseRng_.nextFloat() * 2.0f * noiseAmp - noiseAmp;
			fbkR += engineNoiseRng_.nextFloat() * 2.0f * noiseAmp - noiseAmp;

			// Gain-corrected expander.
			// The waveshaper sin(π/2·|x|) has gain > 1 for |x| < 1:
			//   wsGain(x) = sin(π/2·x)/x → π/2 as x→0.
			// Without correction, compress × ws × expand > 1 → runaway.
			// Compute wsGain at the clamped compressed peak and divide
			// the expander by it so the compander is gain-neutral.
			// When env > 1, compPk is clamped to 1.0 (matching the
			// compressor output clamp) → wsGain = 1.0 → expGain < env
			// → natural limiting that pushes the signal below 0 dBFS.
			const float compPk = juce::jmin (2.0f * env / (1.0f + env), 1.0f);
			const float wsArg = juce::jmin (compPk * kPiHalf, kPiHalf);
			const float wsA2 = wsArg * wsArg;
			const float wsSin = wsArg * (1.0f - wsA2 * (0.16666667f - wsA2 * 0.00833333f));
			const float wsGain = (compPk > 0.001f) ? (wsSin / compPk) : kPiHalf;
			// Blend gain correction with linear (wsGain=1) by drive amount
			const float blendedWsGain = 1.0f + smoothedSatDrive_ * (wsGain - 1.0f);
			const float expGain = (1.0f + env) * 0.5f / blendedWsGain;
			fbkL *= expGain;
			fbkR *= expGain;
		}

		// Apply mode-dependent drift modulation
		// SAT1 drift applied in output stage (applyAnalogOutputSat) to
		// preserve loop-gain neutrality at short delay times.
		// SAT2 drift applied here — mild enough to not erode loop gain.
		if (engineMode_ != 1)
		{
			fbkL *= engineDriftSmoothed_;
			fbkR *= engineDriftSmoothed_;
		}

		fbkL *= engineFeedbackMakeup_;
		fbkR *= engineFeedbackMakeup_;

		// Engine mode crossfade — 30 ms linear ramp from dry to
		// fully-processed when switching to a new SAT mode.
		if (engineXfadePos_ < engineXfadeLen_)
		{
			const float t = (float) ++engineXfadePos_ / (float) engineXfadeLen_;
			fbkL = xfDryL + (fbkL - xfDryL) * t;
			fbkR = xfDryR + (fbkR - xfDryR) * t;
		}

		// Soft safety clamp — smooth asymptotic curve prevents the
		// abrupt harmonic dropout that hard clipping causes when
		// head bump self-oscillation crosses the feedback threshold.
		fbkL = softClamp (fbkL);
		fbkR = softClamp (fbkR);
	}

	// ── Engine output saturation ────────────────────────────────────
	// Applied to the WET output signal (not in the feedback loop).
	// Stage 2 of the dual-saturation architecture.
	// ── ADAA helpers (inline, no class overhead) ──

	// ADAA1 for tanh(k·x): F(x) = (1/k)·ln(cosh(k·x))
	static inline float adaaTanhAD1 (float x, float k) noexcept
	{
		const float kx = k * x;
		// ln(cosh(kx)) = |kx| + ln(1 + exp(-2|kx|)) - ln(2)
		// numerically stable form avoids overflow of cosh() for large x
		const float akx = std::abs (kx);
		return (akx + std::log1p (std::exp (-2.0f * akx)) - 0.6931472f) / k;
	}

	// Process one sample through ADAA1 tanh(k·x)
	inline float processAdaaTanh (float x, float k, float& prev, float& ad1Prev) noexcept
	{
		constexpr float kTol = 1.0e-5f;
		const float ad1 = adaaTanhAD1 (x, k);
		const float dx = x - prev;
		const float y = (std::abs (dx) < kTol)
		              ? std::tanh (k * 0.5f * (x + prev))
		              : (ad1 - ad1Prev) / dx;
		prev = x;
		ad1Prev = ad1;
		return y;
	}

	// ADAA1 for sgn(x)·sin(π/2·|x|): F(x) = (2/π)·(1 - cos(π/2·|x|))
	static inline float adaaSinHalfAD1 (float x) noexcept
	{
		constexpr float kPiH = 1.57079633f;
		constexpr float k2oPI = 0.63661977f;  // 2/π
		return k2oPI * (1.0f - std::cos (kPiH * std::abs (x)));
	}

	// Process one sample through ADAA1 sgn(x)·sin(π/2·|x|) (clamped [-1,1])
	inline float processAdaaSinHalf (float x, float& prev, float& ad1Prev) noexcept
	{
		constexpr float kTol = 1.0e-5f;
		constexpr float kPiH = 1.57079633f;
		x = juce::jlimit (-1.0f, 1.0f, x);
		const float ad1 = adaaSinHalfAD1 (x);
		const float dx = x - prev;
		float y;
		if (std::abs (dx) < kTol)
		{
			// fallback: evaluate function at midpoint
			const float mid = 0.5f * (x + prev);
			const float amid = std::abs (mid) * kPiH;
			y = (mid >= 0.0f ? 1.0f : -1.0f) * std::sin (juce::jmin (amid, kPiH));
		}
		else
		{
			y = (ad1 - ad1Prev) / dx;
		}
		prev = x;
		ad1Prev = ad1;
		return y;
	}

	// ADAA1 for sin(π·x) wavefolder (SAT2 grit): F(x) = -(1/π)·cos(π·x)
	static inline float adaaFoldAD1 (float x) noexcept
	{
		constexpr float kInvPi = 0.31830989f;
		constexpr float kPi    = 3.14159265f;
		return -kInvPi * std::cos (kPi * x);
	}

	inline float processAdaaFold (float x, float& prev, float& ad1Prev) noexcept
	{
		constexpr float kTol = 1.0e-5f;
		constexpr float kPi  = 3.14159265f;
		const float ad1 = adaaFoldAD1 (x);
		const float dx = x - prev;
		const float y = (std::abs (dx) < kTol)
		              ? std::sin (kPi * 0.5f * (x + prev))
		              : (ad1 - ad1Prev) / dx;
		prev = x;
		ad1Prev = ad1;
		return y;
	}

	// 2-pole TDF2 biquad (inline, for post-waveshaper LP)
	static inline float biquadTdf2 (float x, float b0, float b1, float b2,
	                                 float a1, float a2, float z[2]) noexcept
	{
		const float y = b0 * x + z[0];
		z[0] = b1 * x - a1 * y + z[1];
		z[1] = b2 * x - a2 * y;
		return y;
	}

	// SAT1: output waveshaper combining mojo + tube + grit
	// SAT2: output waveshaper combining cubic fold + grit
	// Both include: pre-WS AA LP, post-WS 2-pole LP, ADAA grit, gain jitter.
	inline void applyAnalogOutputSat (float& L, float& R) noexcept
	{
		// Anti-aliasing LP before waveshaper (band-limits signal)
		engineOutAaLpL_ += engineOutAaLpCoeff_ * (L - engineOutAaLpL_);
		engineOutAaLpR_ += engineOutAaLpCoeff_ * (R - engineOutAaLpR_);
		L = engineOutAaLpL_;
		R = engineOutAaLpR_;

		// ── Saturation drive modulation (S&H → EMA) ──
		engineSatModPhase_ += 1.0f;
		if (engineSatModPhase_ >= engineSatModPeriod_)
		{
			engineSatModPhase_ -= engineSatModPeriod_;
			engineSatModTarget_ = 0.975f + chaosDRng_[0].nextFloat() * 0.05f;
		}
		engineSatModSmoothed_ = engineSatModSmoothCoeff_ * engineSatModSmoothed_
		                      + (1.0f - engineSatModSmoothCoeff_) * engineSatModTarget_;

		// ── Drive-dependent gain jitter (S&H → EMA) ──
		engineGainJitterPhase_ += 1.0f;
		if (engineGainJitterPhase_ >= engineGainJitterPeriod_)
		{
			engineGainJitterPhase_ -= engineGainJitterPeriod_;
			const float halfRange = (engineMode_ == 1) ? 0.159f : 0.109f;
			engineGainJitterTarget_ = 1.0f + (chaosDRng_[0].nextFloat() * 2.0f - 1.0f) * halfRange;
		}
		engineGainJitterSmoothed_ = engineGainJitterSmoothCoeff_ * engineGainJitterSmoothed_
		                          + (1.0f - engineGainJitterSmoothCoeff_) * engineGainJitterTarget_;

		const float driveAmt = smoothedSatDrive_;
		const float driveAmt2 = driveAmt * driveAmt;
		const float gritAmt = smoothedSatGrit_;

		if (engineMode_ == 1)
		{
			// SAT1 wow/flutter drift applied to output
			L *= engineDriftSmoothed_;
			R *= engineDriftSmoothed_;

			const float cleanL = L, cleanR = R;

			// ── SAT1: ToTape6 "mojo" + tube asymmetry ──
			// Drive scales the input gain into the waveshaper for more aggression
			constexpr float kPiH = 1.57079633f;
			const float driveVar = engineSatModSmoothed_ * (1.0f + driveAmt * 1.5f);

			float absL = std::abs (L);
			if (absL > 0.0001f)
			{
				float mojoL = std::sqrt (std::sqrt (absL));
				float argL  = juce::jlimit (-kPiH, kPiH, L * mojoL * driveVar * kPiH);
				const float aL2 = argL * argL;
				L = argL * (1.0f - aL2 * (0.16666667f - aL2 * 0.00833333f)) / mojoL;
			}

			float absR = std::abs (R);
			if (absR > 0.0001f)
			{
				float mojoR = std::sqrt (std::sqrt (absR));
				float argR  = juce::jlimit (-kPiH, kPiH, R * mojoR * driveVar * kPiH);
				const float aR2 = argR * argR;
				R = argR * (1.0f - aR2 * (0.16666667f - aR2 * 0.00833333f)) / mojoR;
			}

			// Tube asymmetric 2nd harmonic
			const float tubeK = 0.12f * driveVar;
			L -= L * L * tubeK;
			R -= R * R * tubeK;

			// Dry/wet blend by DRIVE
			L = cleanL + driveAmt * (L - cleanL);
			R = cleanR + driveAmt * (R - cleanR);

			// Post-main-WS biquad LP (catches mojo harmonics)
			L = biquadTdf2 (L, enginePostWsB0_, enginePostWsB1_, enginePostWsB2_,
			                enginePostWsA1_, enginePostWsA2_, enginePostWsZL_);
			R = biquadTdf2 (R, enginePostWsB0_, enginePostWsB1_, enginePostWsB2_,
			                enginePostWsA1_, enginePostWsA2_, enginePostWsZR_);

			// ── SAT1 Grit: "Valve Crunch" — cascaded asymmetric tanh with ADAA ──
			// Two-stage half-wave tanh soft-clipper → duty-cycle modulation
			// like real tube amps (Carvin/SimulAnalog technique).
			// k scales 2→5 with grit for increasing aggression.
			if (gritAmt > 0.001f)
			{
				const float k = 2.0f + gritAmt * 3.0f;  // 2..5 steepness

				// Stage 1: positive half tanh with ADAA
				float gL = processAdaaTanh (L, k, adaaTanhPrevL_, adaaTanhAd1PrevL_);
				float gR = processAdaaTanh (R, k, adaaTanhPrevR_, adaaTanhAd1PrevR_);

				// Stage 2: invert, clip negative half, invert back
				// → asymmetric duty-cycle modulation (even+odd harmonics)
				gL = -std::tanh (k * juce::jmax (0.0f, -gL));
				gR = -std::tanh (k * juce::jmax (0.0f, -gR));

				// Blend grit in
				L += gritAmt * (gL - L);
				R += gritAmt * (gR - R);
			}
		}
		else
		{
			const float cleanL = L, cleanR = R;

			// ── SAT2: Drive-style cubic fold ──
			// Drive scales g for more aggressive fold at high drive
			const float g = 0.35f * engineSatModSmoothed_ * (1.0f + driveAmt * 1.5f);

			float sL = juce::jlimit (-1.0f, 1.0f, L);
			float sR = juce::jlimit (-1.0f, 1.0f, R);

			float aL = std::abs (sL);
			sL -= sL * aL * g * aL * g;
			sL *= (1.0f + g);

			float aR = std::abs (sR);
			sR -= sR * aR * g * aR * g;
			sR *= (1.0f + g);

			// Mild 2nd-harmonic asymmetry (NE571 transistor mismatch)
			const float bbdAsym = 0.06f * engineSatModSmoothed_;
			sL -= sL * sL * bbdAsym;
			sR -= sR * sR * bbdAsym;

			L = sL;
			R = sR;

			// Dry/wet blend by DRIVE
			L = cleanL + driveAmt * (L - cleanL);
			R = cleanR + driveAmt * (R - cleanR);

			// Post-main-WS biquad LP (catches cubic-fold harmonics)
			L = biquadTdf2 (L, enginePostWsB0_, enginePostWsB1_, enginePostWsB2_,
			                enginePostWsA1_, enginePostWsA2_, enginePostWsZL_);
			R = biquadTdf2 (R, enginePostWsB0_, enginePostWsB1_, enginePostWsB2_,
			                enginePostWsA1_, enginePostWsA2_, enginePostWsZR_);

			// ── SAT2 Grit: "Digital Wreck" — wavefold sin(πx) with ADAA ──
			// + self-ring-mod (signal × sign-flipped delayed self)
			// Produces intermodulation / metallic inharmonics typical of
			// lo-fi digital / trap distortion.
			if (gritAmt > 0.001f)
			{
				// Wavefold: sin(π·x) with ADAA — fold threshold lowers with grit
				const float foldInput = L * (1.0f + gritAmt * 2.0f);
				const float foldInputR = R * (1.0f + gritAmt * 2.0f);
				float gL = processAdaaFold (foldInput, adaaFoldPrevL_, adaaFoldAd1PrevL_);
				float gR = processAdaaFold (foldInputR, adaaFoldPrevR_, adaaFoldAd1PrevR_);

				// Self-ring-mod: multiply by sign-inverted opposite channel
				// Creates sum/difference frequencies — metallic intermodulation
				const float ringAmt = gritAmt * 0.3f;  // max 30% ring mix
				gL += ringAmt * gL * (gR >= 0.0f ? -1.0f : 1.0f);
				gR += ringAmt * gR * (gL >= 0.0f ? -1.0f : 1.0f);

				// Blend grit in
				L += gritAmt * (gL - L);
				R += gritAmt * (gR - R);
			}
		}

		// ── Apply gain jitter (post-everything, scales with drive²) ──
		const float jitterGain = 1.0f + driveAmt2 * (engineGainJitterSmoothed_ - 1.0f);
		L *= jitterGain;
		R *= jitterGain;
	}

	std::atomic<float> currentMidiFrequency { 0.0f };
	std::atomic<int> lastMidiNote { -1 };
	std::atomic<int> lastMidiVelocity { 127 };
	std::atomic<int> midiChannel { 0 };

	std::atomic<float>* timeMsParam = nullptr;
	std::atomic<float>* timeSyncParam = nullptr;
	std::atomic<float>* feedbackParam = nullptr;
	std::atomic<float>* jitterParam = nullptr;
	std::atomic<float>* modeParam = nullptr;
	std::atomic<float>* modParam = nullptr;
	std::atomic<float>* inputParam = nullptr;
	std::atomic<float>* outputParam = nullptr;
	std::atomic<float>* mixParam = nullptr;
	std::atomic<float>* syncParam = nullptr;
	std::atomic<float>* midiParam = nullptr;
	std::atomic<float>* autoFbkParam = nullptr;
	std::atomic<float>* autoFbkTauParam = nullptr;
	std::atomic<float>* autoFbkAttParam = nullptr;
	std::atomic<float>* reverseParam = nullptr;
	std::atomic<float>* reverseSmoothParam = nullptr;

	std::atomic<float>* filterHpFreqParam  = nullptr;
	std::atomic<float>* filterLpFreqParam  = nullptr;
	std::atomic<float>* filterHpSlopeParam = nullptr;
	std::atomic<float>* filterLpSlopeParam = nullptr;
	std::atomic<float>* filterHpOnParam    = nullptr;
	std::atomic<float>* filterLpOnParam    = nullptr;

	std::atomic<float>* tiltParam     = nullptr;
	std::atomic<float>* chaosParam      = nullptr;
	std::atomic<float>* chaosDelayParam  = nullptr;
	std::atomic<float>* chaosAmtParam    = nullptr;
	std::atomic<float>* chaosSpdParam    = nullptr;
	std::atomic<float>* chaosAmtFilterParam = nullptr;
	std::atomic<float>* chaosSpdFilterParam = nullptr;
	std::atomic<float>* engineParam      = nullptr;
	std::atomic<float>* sat1DriveParam   = nullptr;
	std::atomic<float>* sat1GritParam    = nullptr;
	std::atomic<float>* sat2DriveParam   = nullptr;
	std::atomic<float>* sat2GritParam    = nullptr;
	std::atomic<float>* duckParam       = nullptr;

	std::atomic<float>* modeInParam   = nullptr;
	std::atomic<float>* modeOutParam  = nullptr;
	std::atomic<float>* sumBusParam   = nullptr;

	std::atomic<float>* limThresholdParam = nullptr;
	std::atomic<float>* limModeParam      = nullptr;
	std::atomic<float>* invPolParam       = nullptr;
	std::atomic<float>* invStrParam       = nullptr;
	std::atomic<float>* mixModeParam      = nullptr;
	std::atomic<float>* dryLevelParam     = nullptr;
	std::atomic<float>* wetLevelParam     = nullptr;

	std::atomic<float>* panParam       = nullptr;
	std::atomic<float>* filterPosParam = nullptr;
	float lastPan_      = -1.0f;
	float lastPanLeft_  = 1.0f;
	float lastPanRight_ = 1.0f;
	
	std::atomic<float>* uiWidthParam = nullptr;
	std::atomic<float>* uiHeightParam = nullptr;
	std::atomic<float>* uiPaletteParam = nullptr;
	std::atomic<float>* uiCrtParam = nullptr;
	std::array<std::atomic<float>*, 2> uiColorParams { nullptr, nullptr };

	// UI state atomics
	std::atomic<int> uiEditorWidth { 360 };
	std::atomic<int> uiEditorHeight { 480 };
	std::atomic<int> uiUseCustomPalette { 0 };
	std::atomic<int> uiCrtEnabled { 0 };
	std::array<std::atomic<juce::uint32>, 2> uiCustomPalette {
		std::atomic<juce::uint32> { juce::Colours::white.getARGB() },
		std::atomic<juce::uint32> { juce::Colours::black.getARGB() }
	};

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ECHOTRAudioProcessor)
};
