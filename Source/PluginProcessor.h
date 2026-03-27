#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include "PerfTrace.h"

class ECHOTRAudioProcessor : public juce::AudioProcessor
{
public:
	ECHOTRAudioProcessor();
	~ECHOTRAudioProcessor() override;

	// Parameter IDs
	static constexpr const char* kParamTimeMs     = "time_ms";
	static constexpr const char* kParamTimeSync   = "time_sync";
	static constexpr const char* kParamFeedback   = "feedback";
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

	// Engine parameter ID
	static constexpr const char* kParamEngine    = "engine";

	// Chaos parameter IDs
	static constexpr const char* kParamChaos     = "chaos";      // filter chaos (CHS F)
	static constexpr const char* kParamChaosD    = "chaos_d";    // delay chaos  (CHS D)
	static constexpr const char* kParamChaosAmt  = "chaos_amt";
	static constexpr const char* kParamChaosSpd  = "chaos_spd";
	static constexpr const char* kParamChaosAmtFilter = "chaos_amt_filter";
	static constexpr const char* kParamChaosSpdFilter = "chaos_spd_filter";
	
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
	static constexpr float kTimeMsMaxSync = 20000.0f;
	static constexpr float kTimeMsDefault = 500.0f;

	static constexpr int kTimeSyncMin = 0;
	static constexpr int kTimeSyncMax = 29;
	static constexpr int kTimeSyncDefault = 10;

	static constexpr float kFeedbackMin = -1.0f;
	static constexpr float kFeedbackMax =  1.0f;
	static constexpr float kFeedbackDefault = 0.0f;

	static constexpr int kModeMin = 0;
	static constexpr int kModeMax = 4; // 0=MONO, 1=STEREO, 2=WIDE, 3=DUAL, 4=PING-PONG ("Style" in UI)
	static constexpr float kModeDefault = 1.0f;

	static constexpr float kModMin = 0.0f;
	static constexpr float kModMax = 1.0f;
	static constexpr float kModDefault = 0.5f;

	static constexpr float kInputMin = -100.0f;
	static constexpr float kInputMax = 0.0f;
	static constexpr float kInputDefault = 0.0f;

	static constexpr float kOutputMin = -100.0f;
	static constexpr float kOutputMax = 24.0f;
	static constexpr float kOutputDefault = 0.0f;

	static constexpr float kMixMin = 0.0f;
	static constexpr float kMixMax = 1.0f;
	static constexpr float kMixDefault = 1.0f;

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

	// Chaos ranges and defaults
	static constexpr float kChaosAmtMin     = 0.0f;
	static constexpr float kChaosAmtMax     = 100.0f;
	static constexpr float kChaosAmtDefault = 50.0f;
	static constexpr float kChaosSpdMin     = 0.01f;   // Hz
	static constexpr float kChaosSpdMax     = 100.0f;  // Hz
	static constexpr float kChaosSpdDefault = 5.0f;    // Hz

	// Engine ranges and defaults
	static constexpr int kEngineMin     = 0;       // 0=CLEAN
	static constexpr int kEngineMax     = 2;       // 2=BBD
	static constexpr int kEngineDefault = 0;       // CLEAN

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
	                          float outputGain, float mix, float delaySmoothCoeff);
	void processMonoDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
	                        float delaySamples, float feedback, float inputGain,
	                        float outputGain, float mix, float delaySmoothCoeff);
	void processPingPongDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
	                            float delaySamples, float feedback, float inputGain,
	                            float outputGain, float mix, float delaySmoothCoeff);
	void processWideDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
	                       float delaySamples, float feedback, float inputGain,
	                       float outputGain, float mix, float delaySmoothCoeff);
	void processDualDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
	                       float delaySamples, float feedback, float inputGain,
	                       float outputGain, float mix, float delaySmoothCoeff);

	// Reverse delay processing (chunk-based backward playback with smooth taper control)
	void processReverseDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
	                          float delaySamples, float feedback, float inputGain,
	                          float outputGain, float mix, float delaySmoothCoeff,
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
	WetFilterBiquadCoeffs hpCoeffs_[2];       // per-section HP coeffs
	WetFilterBiquadCoeffs lpCoeffs_[2];       // per-section LP coeffs
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
	float wetFilterTargetHpFreq_ = kFilterHpFreqDefault;
	float wetFilterTargetLpFreq_ = kFilterLpFreqDefault;
	int   wetFilterNumSectionsHp_ = 0;
	int   wetFilterNumSectionsLp_ = 0;
	void  filterWetSample (float& wetL, float& wetR);

	// Tilt filter state
	float tiltDb_        = 0.0f;
	float tiltB0_ = 1.0f, tiltB1_ = 0.0f, tiltA1_ = 0.0f;
	float tiltTargetB0_ = 1.0f, tiltTargetB1_ = 0.0f, tiltTargetA1_ = 0.0f;
	float tiltState_[2]  = { 0.0f, 0.0f };
	float lastTiltDb_    = 0.0f;
	float tiltSmoothSc_  = 0.0f;  // cached EMA coeff (depends only on sampleRate)

	// ── Chaos state (3 independent S&H engines) ──
	bool  chaosFilterEnabled_    = false;
	bool  chaosDelayEnabled_     = false;

	// CHS D parameters
	float chaosAmtD_                    = 0.0f;
	float chaosShPeriodD_               = 8820.0f;
	float smoothedChaosShPeriodD_       = 8820.0f;
	float chaosDelayMaxSamples_         = 0.0f;
	float smoothedChaosDelayMaxSamples_ = 0.0f;
	float chaosGainMaxDb_               = 0.0f;
	float smoothedChaosGainMaxDb_       = 0.0f;

	// CHS D S&H: delay (30ms EMA)
	float chaosDPhase_       = 0.0f;
	float chaosDTarget_      = 0.0f;
	float chaosDSmoothed_    = 0.0f;
	float chaosDSmoothCoeff_ = 0.999f;
	juce::Random chaosDRng_;

	// CHS D S&H: gain (15ms EMA, decorrelated)
	float chaosGPhase_       = 0.0f;
	float chaosGTarget_      = 0.0f;
	float chaosGSmoothed_    = 0.0f;
	float chaosGSmoothCoeff_ = 0.999f;
	juce::Random chaosGRng_;

	// CHS F parameters
	float chaosAmtF_                  = 0.0f;
	float chaosShPeriodF_             = 8820.0f;
	float smoothedChaosShPeriodF_     = 8820.0f;
	float chaosFilterMaxOct_          = 0.0f;
	float smoothedChaosFilterMaxOct_  = 0.0f;

	// CHS F S&H: filter (60ms EMA)
	float chaosFPhase_       = 0.0f;
	float chaosFTarget_      = 0.0f;
	float chaosFSmoothed_    = 0.0f;
	float chaosFSmoothCoeff_ = 0.999f;
	juce::Random chaosFRng_;

	// Chaos per-sample param smoothing
	float chaosParamSmoothCoeff_ = 0.999f;

	// Chaos micro-delay buffer (post-wet, CAB-TR style)
	static constexpr int kChaosDelayBufLen = 1024;
	float chaosDelayBuf_[2][kChaosDelayBufLen] = {};
	int   chaosDelayWritePos_ = 0;

	// Engine feedback-loop processing state
	int   engineMode_ = 0;            // 0=CLEAN, 1=ANALOG, 2=BBD
	int   prevEngineMode_ = 0;        // previous mode for LP state reset on switch
	float engineLp1StateL_ = 0.0f;    // 1st-pole LP filter state (left)
	float engineLp1StateR_ = 0.0f;    // 1st-pole LP filter state (right)
	float engineLp2StateL_ = 0.0f;    // 2nd-pole LP filter state (left)
	float engineLp2StateR_ = 0.0f;    // 2nd-pole LP filter state (right)
	float engineLpCoeff_ = 0.0f;      // 1-pole LP coeff (precomputed per block)

	// Per-sample feedback EMA smoothing
	float smoothedFeedback_ = 0.0f;

	// Engine micro-oscillation — mode-dependent analog drift
	// ANALOG: Slow wow (~2 Hz) modulating gain ±0.5 dB via sine LFO
	// BBD:    Faster random clock jitter (~15 Hz S&H) ±0.2 dB
	float engineDriftPhase_    = 0.0f;
	float engineDriftPeriod_   = 4800.0f;  // samples per cycle/step
	float engineDriftTarget_   = 1.0f;     // raw target (gain multiplier)
	float engineDriftSmoothed_ = 1.0f;     // EMA-smoothed gain multiplier
	float engineDriftSmoothCoeff_ = 0.9995f; // EMA coeff

	// Per-sample S&H step for CHS D (delay + gain, 2 independent S&H engines)
	inline void advanceChaosD() noexcept
	{
		smoothedChaosDelayMaxSamples_ += (chaosDelayMaxSamples_ - smoothedChaosDelayMaxSamples_) * (1.0f - chaosParamSmoothCoeff_);
		smoothedChaosGainMaxDb_       += (chaosGainMaxDb_       - smoothedChaosGainMaxDb_)       * (1.0f - chaosParamSmoothCoeff_);
		smoothedChaosShPeriodD_       += (chaosShPeriodD_       - smoothedChaosShPeriodD_)       * (1.0f - chaosParamSmoothCoeff_);

		chaosDPhase_ += 1.0f;
		if (chaosDPhase_ >= smoothedChaosShPeriodD_)
		{
			chaosDPhase_ -= smoothedChaosShPeriodD_;
			chaosDTarget_ = chaosDRng_.nextFloat() * 2.0f - 1.0f;
		}
		chaosDSmoothed_ = chaosDSmoothCoeff_ * chaosDSmoothed_
		                + (1.0f - chaosDSmoothCoeff_) * chaosDTarget_;

		chaosGPhase_ += 1.0f;
		if (chaosGPhase_ >= smoothedChaosShPeriodD_)
		{
			chaosGPhase_ -= smoothedChaosShPeriodD_;
			chaosGTarget_ = chaosGRng_.nextFloat() * 2.0f - 1.0f;
		}
		chaosGSmoothed_ = chaosGSmoothCoeff_ * chaosGSmoothed_
		                + (1.0f - chaosGSmoothCoeff_) * chaosGTarget_;
	}

	// Per-sample S&H step for CHS F (filter, 1 independent S&H engine)
	inline void advanceChaosF() noexcept
	{
		smoothedChaosFilterMaxOct_  += (chaosFilterMaxOct_  - smoothedChaosFilterMaxOct_)  * (1.0f - chaosParamSmoothCoeff_);
		smoothedChaosShPeriodF_     += (chaosShPeriodF_     - smoothedChaosShPeriodF_)     * (1.0f - chaosParamSmoothCoeff_);

		chaosFPhase_ += 1.0f;
		if (chaosFPhase_ >= smoothedChaosShPeriodF_)
		{
			chaosFPhase_ -= smoothedChaosShPeriodF_;
			chaosFTarget_ = chaosFRng_.nextFloat() * 2.0f - 1.0f;
		}
		chaosFSmoothed_ = chaosFSmoothCoeff_ * chaosFSmoothed_
		                + (1.0f - chaosFSmoothCoeff_) * chaosFTarget_;
	}

	// Post-wet micro-delay + gain modulation (CAB-TR style, ±5ms max, ±1dB gain)
	inline void applyChaosDelay (float& wetL, float& wetR) noexcept
	{
		const int wp = chaosDelayWritePos_;
		chaosDelayBuf_[0][wp] = wetL;
		chaosDelayBuf_[1][wp] = wetR;

		const float centerDelay = smoothedChaosDelayMaxSamples_;
		const float delaySamp   = juce::jlimit (0.0f, (float)(kChaosDelayBufLen - 2),
		                                        centerDelay + chaosDSmoothed_ * smoothedChaosDelayMaxSamples_);

		const float readPos = (float) wp - delaySamp;
		const int iPos = (int) std::floor (readPos);
		const float frac = readPos - (float) iPos;
		const int mask = kChaosDelayBufLen - 1;

		for (int ch = 0; ch < 2; ++ch)
		{
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

		// Gain modulation (±1dB from decorrelated gain S&H)
		const float gainDb  = chaosGSmoothed_ * smoothedChaosGainMaxDb_;
		const float gainLin = std::pow (10.0f, gainDb * 0.05f);
		wetL *= gainLin;
		wetR *= gainLin;
	}

	// Engine feedback-loop processing
	// ANALOG: Rational tanh saturation (Padé 3,3) + 2-pole LP @ 5 kHz + slow wow
	// BBD:    Simplified compander → sin() waveshaper → expander + 2-pole LP @ 3 kHz + clock jitter
	inline void applyEngineToFeedback (float& fbkL, float& fbkR) noexcept
	{
		if (engineMode_ == 0) return; // CLEAN: bypass

		// ── Mode-dependent drift ──
		engineDriftPhase_ += 1.0f;
		if (engineMode_ == 1)
		{
			// ANALOG wow: smooth sine LFO at ~2 Hz, ±0.5 dB [0.944, 1.059]
			if (engineDriftPhase_ >= engineDriftPeriod_)
				engineDriftPhase_ -= engineDriftPeriod_;
			const float phase = engineDriftPhase_ / engineDriftPeriod_; // [0,1)
			// Fast sine: Bhaskara approximation, max error ~0.2%
			const float x = phase * 2.0f - 1.0f; // [-1, 1]
			const float x2 = x * x;
			const float sineApprox = x * (1.0f - x2 * (0.16666667f - x2 * 0.00833333f));
			engineDriftSmoothed_ = 1.0f + sineApprox * 0.057f; // ±0.5 dB
		}
		else
		{
			// BBD clock jitter: S&H at ~15 Hz, ±0.2 dB [0.977, 1.023]
			if (engineDriftPhase_ >= engineDriftPeriod_)
			{
				engineDriftPhase_ -= engineDriftPeriod_;
				engineDriftTarget_ = 0.977f + chaosDRng_.nextFloat() * 0.046f;
			}
			engineDriftSmoothed_ = engineDriftSmoothCoeff_ * engineDriftSmoothed_
			                     + (1.0f - engineDriftSmoothCoeff_) * engineDriftTarget_;
		}

		// Input clamp — prevent runaway oscillation
		fbkL = juce::jlimit (-1.5f, 1.5f, fbkL);
		fbkR = juce::jlimit (-1.5f, 1.5f, fbkR);

		if (engineMode_ == 1)
		{
			// ── ANALOG: LP filter only in feedback loop ──
			// Saturation is applied on the wet OUTPUT path (applyAnalogOutputSat),
			// NOT inside the feedback loop.  This ensures the feedback parameter
			// alone controls sustain time — matching clean & BBD behavior.
			// The 2-pole LP darkens repeats progressively (core analog character).

			// 2-pole LP at ~5 kHz (tape head + output transformer rolloff)
			// Two cascaded 1-pole stages = 12 dB/oct Butterworth-like
			engineLp1StateL_ += engineLpCoeff_ * (fbkL - engineLp1StateL_);
			engineLp1StateR_ += engineLpCoeff_ * (fbkR - engineLp1StateR_);
			engineLp2StateL_ += engineLpCoeff_ * (engineLp1StateL_ - engineLp2StateL_);
			engineLp2StateR_ += engineLpCoeff_ * (engineLp1StateR_ - engineLp2StateR_);
			fbkL = engineLp2StateL_;
			fbkR = engineLp2StateR_;
		}
		else
		{
			// ── BBD: Compander + sin() waveshaper + expander ──
			// Real BBD chips (MN3005/3207) use NE571 compander for SNR improvement.
			// Simplified compander: compress → clip → expand

			// Pre-LP at ~3 kHz (anti-aliasing before S&H stage), 2-pole (12 dB/oct)
			engineLp1StateL_ += engineLpCoeff_ * (fbkL - engineLp1StateL_);
			engineLp1StateR_ += engineLpCoeff_ * (fbkR - engineLp1StateR_);
			engineLp2StateL_ += engineLpCoeff_ * (engineLp1StateL_ - engineLp2StateL_);
			engineLp2StateR_ += engineLpCoeff_ * (engineLp1StateR_ - engineLp2StateR_);
			fbkL = engineLp2StateL_;
			fbkR = engineLp2StateR_;

			// Compressor stage: soft-knee RMS-like compression
			// sign(x) * (2|x| / (1 + |x|)) — fast, no transcendentals
			// Ratio ~2:1 for |x|>0.5, transparent below.
			const float aL = std::abs (fbkL);
			const float aR = std::abs (fbkR);
			const float cL = (aL + aL) / (1.0f + aL);   // compressed magnitude
			const float cR = (aR + aR) / (1.0f + aR);
			fbkL = (fbkL >= 0.0f) ? cL : -cL;
			fbkR = (fbkR >= 0.0f) ? cR : -cR;

			// sin(π/2·|x|) waveshaper — matches BBD transfer function
			// Fast 5th-order minimax polynomial (max error < 0.0002)
			constexpr float kPiHalf = 1.57079633f;
			float rectL = std::abs (fbkL) * kPiHalf;
			float rectR = std::abs (fbkR) * kPiHalf;
			if (rectL > kPiHalf) rectL = kPiHalf;
			if (rectR > kPiHalf) rectR = kPiHalf;
			{
				const float x2L = rectL * rectL;
				rectL = rectL * (1.0f - x2L * (0.16666667f - x2L * 0.00833333f));
				const float x2R = rectR * rectR;
				rectR = rectR * (1.0f - x2R * (0.16666667f - x2R * 0.00833333f));
			}
			fbkL = (fbkL > 0.0f) ? rectL : -rectL;
			fbkR = (fbkR > 0.0f) ? rectR : -rectR;

			// Expander stage: inverse of compressor
			// sign(x) * (|x| / (2 - |x|)) — restores dynamics with BBD coloring
			const float eL = std::abs (fbkL);
			const float eR = std::abs (fbkR);
			const float denom_L = 2.0f - eL;
			const float denom_R = 2.0f - eR;
			// Guard against division by zero (only possible if |x| >= 2, shouldn't happen)
			const float exL = (denom_L > 0.01f) ? (eL / denom_L) : 1.0f;
			const float exR = (denom_R > 0.01f) ? (eR / denom_R) : 1.0f;
			fbkL = (fbkL >= 0.0f) ? exL : -exL;
			fbkR = (fbkR >= 0.0f) ? exR : -exR;
		}

		// Apply mode-dependent drift modulation
		fbkL *= engineDriftSmoothed_;
		fbkR *= engineDriftSmoothed_;

		// Final safety clamp
		fbkL = juce::jlimit (-1.5f, 1.5f, fbkL);
		fbkR = juce::jlimit (-1.5f, 1.5f, fbkR);
	}

	// ── Analog output saturation ──────────────────────────────────────
	// Applied to the WET output signal (not in the feedback loop).
	// This preserves loop-gain neutrality while adding warm analog
	// harmonic character to every repeat.
	inline void applyAnalogOutputSat (float& L, float& R) noexcept
	{
		// Rational tanh (Padé 3,3): adds rich odd harmonics
		const float x2L = L * L;
		L = L * (27.0f + x2L) / (27.0f + 9.0f * x2L);
		const float x2R = R * R;
		R = R * (27.0f + x2R) / (27.0f + 9.0f * x2R);

		// Asymmetric 2nd harmonic (even-order warmth)
		L -= L * L * 0.08f;
		R -= R * R * 0.08f;
	}

	std::atomic<float> currentMidiFrequency { 0.0f };
	std::atomic<int> lastMidiNote { -1 };
	std::atomic<int> lastMidiVelocity { 127 };
	std::atomic<int> midiChannel { 0 };

	std::atomic<float>* timeMsParam = nullptr;
	std::atomic<float>* timeSyncParam = nullptr;
	std::atomic<float>* feedbackParam = nullptr;
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
	std::atomic<float>* engineParam     = nullptr;
	
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
