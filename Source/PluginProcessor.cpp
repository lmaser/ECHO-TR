#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
	// ---- Hermite 4-point cubic interpolation ----
	inline float hermite4pt (float ym1, float y0, float y1, float y2, float frac) noexcept
	{
		const float c0 = y0;
		const float c1 = 0.5f * (y1 - ym1);
		const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
		const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
		return ((c3 * frac + c2) * frac + c1) * frac + c0;
	}

	// ---- One-pole EMA coefficient from time constant ----
	inline float smoothCoeffFromTau (double sampleRate, float tauSeconds) noexcept
	{
		return std::exp (-1.0f / (static_cast<float>(sampleRate) * tauSeconds));
	}

	// 80 ms tau  ->  ~400 ms to settle (5*tau)
	// Enough inertia for smooth tape sweep, fast enough for musical response.
	constexpr float kDelaySmoothTauSeconds = 0.08f;

	// Gain / mix EMA coefficient: one-pole ~5 ms time constant at 44.1 kHz.
	// Shared by all delay modes (Stereo, Mono, PingPong, Reverse, bypass).
	constexpr float kGainSmoothCoeff = 0.9955f;

	// ---- Precomputed Tukey (raised-cosine) taper table for reverse mode ----
	// Avoids per-sample std::cos() in the inner loop.
	constexpr int kTaperTableSize = 129; // 128 samples + endpoint
	struct TaperTable
	{
		float data[kTaperTableSize];
		constexpr TaperTable() : data{}
		{
			// w(i) = 0.5 * (1 - cos(pi * i / 128))  for i in [0, 128]
			for (int i = 0; i < kTaperTableSize; ++i)
			{
				const double t = 3.14159265358979323846 * static_cast<double>(i) / 128.0;
				// Manual cosine via Taylor series (constexpr-safe, only need [0, pi])
				// cos(t) = 1 - t^2/2! + t^4/4! - t^6/6! + t^8/8! - t^10/10!
				const double t2 = t * t;
				const double t4 = t2 * t2;
				const double t6 = t4 * t2;
				const double t8 = t6 * t2;
				const double t10 = t8 * t2;
				const double cosVal = 1.0 - t2/2.0 + t4/24.0 - t6/720.0 + t8/40320.0 - t10/3628800.0;
				data[i] = static_cast<float>(0.5 * (1.0 - cosVal));
			}
		}
	};
	constexpr TaperTable kTaperTable {};

	// Look up taper weight. pos = position within taper zone [0, taperLen].
	// Uses linear interpolation between table entries.
	inline float taperWeight (float pos, float taperLen) noexcept
	{
		const float norm = pos * (128.0f / taperLen); // map to [0, 128]
		const int idx = static_cast<int>(norm);
		if (idx >= 128) return 1.0f;
		const float frac = norm - static_cast<float>(idx);
		return kTaperTable.data[idx] + frac * (kTaperTable.data[idx + 1] - kTaperTable.data[idx]);
	}

	inline float loadAtomicOrDefault (std::atomic<float>* p, float def) noexcept
	{
		return p != nullptr ? p->load (std::memory_order_relaxed) : def;
	}

	inline int loadIntParamOrDefault (std::atomic<float>* p, int def) noexcept
	{
		return (int) std::lround (loadAtomicOrDefault (p, (float) def));
	}

	inline bool loadBoolParamOrDefault (std::atomic<float>* p, bool def) noexcept
	{
		return loadAtomicOrDefault (p, def ? 1.0f : 0.0f) > 0.5f;
	}

	inline void setParameterPlainValue (juce::AudioProcessorValueTreeState& apvts,
										const char* paramId,
										float plainValue)
	{
		if (auto* param = apvts.getParameter (paramId))
		{
			const float norm = param->convertTo0to1 (plainValue);
			param->setValueNotifyingHost (norm);
		}
	}

	// ---- Fast dB-to-linear gain (avoids std::pow per block) ----
	// Uses the identity: 10^(dB/20) = 2^(dB * log2(10)/20)
	// log2(10)/20 = 0.16609640474
	// std::exp2 is typically a single x87/SSE instruction.
	inline float fastDecibelsToGain (float dB) noexcept
	{
		return (dB <= -100.0f) ? 0.0f : std::exp2 (dB * 0.16609640474f);
	}

	// ---- Feedback loop processing chain ----
	// Applied ONLY to the feedback component (never to input signal).
	// DC blocker only — maximally transparent.
	// DC blocker: one-pole HP at ~5 Hz prevents DC drift accumulation.

	constexpr float kFbkDcBlockHz = 5.0f;

	// DC blocker tick (one-pole HP)
	inline float dcBlockTick (float in, float& inState, float& outState, float r) noexcept
	{
		outState = r * (outState + in - inState);
		inState = in;
		return outState;
	}

	// ── Wet-signal biquad filter helpers ──

	using BQC = ECHOTRAudioProcessor::WetFilterBiquadCoeffs;

	constexpr float kBW4_Q1 = 0.54119610f;   // Butterworth 4th-order section 1
	constexpr float kBW4_Q2 = 1.30656296f;   // Butterworth 4th-order section 2
	constexpr float kBW2_Q  = 0.70710678f;   // Butterworth 2nd-order

	inline BQC calcOnePoleLP (float fc, float sr)
	{
		const float w = std::tan (juce::MathConstants<float>::pi * juce::jlimit (1.0f, sr * 0.499f, fc) / sr);
		BQC c;
		c.b0 = w / (1.0f + w);
		c.b1 = c.b0;
		c.b2 = 0.0f;
		c.a1 = (w - 1.0f) / (1.0f + w);
		c.a2 = 0.0f;
		return c;
	}

	inline BQC calcOnePoleHP (float fc, float sr)
	{
		const float w = std::tan (juce::MathConstants<float>::pi * juce::jlimit (1.0f, sr * 0.499f, fc) / sr);
		BQC c;
		c.b0 =  1.0f / (1.0f + w);
		c.b1 = -c.b0;
		c.b2 = 0.0f;
		c.a1 = (w - 1.0f) / (1.0f + w);
		c.a2 = 0.0f;
		return c;
	}

	inline BQC calcBiquadLP (float fc, float sr, float Q)
	{
		const float w0 = 2.0f * juce::MathConstants<float>::pi * juce::jlimit (1.0f, sr * 0.499f, fc) / sr;
		const float cosw = std::cos (w0);
		const float sinw = std::sin (w0);
		const float alpha = sinw / (2.0f * Q);
		const float a0inv = 1.0f / (1.0f + alpha);
		BQC c;
		c.b0 = ((1.0f - cosw) * 0.5f) * a0inv;
		c.b1 = ( 1.0f - cosw)         * a0inv;
		c.b2 = c.b0;
		c.a1 = (-2.0f * cosw)         * a0inv;
		c.a2 = ( 1.0f - alpha)        * a0inv;
		return c;
	}

	inline BQC calcBiquadHP (float fc, float sr, float Q)
	{
		const float w0 = 2.0f * juce::MathConstants<float>::pi * juce::jlimit (1.0f, sr * 0.499f, fc) / sr;
		const float cosw = std::cos (w0);
		const float sinw = std::sin (w0);
		const float alpha = sinw / (2.0f * Q);
		const float a0inv = 1.0f / (1.0f + alpha);
		BQC c;
		c.b0 = ((1.0f + cosw) * 0.5f) * a0inv;
		c.b1 = (-(1.0f + cosw))       * a0inv;
		c.b2 = c.b0;
		c.a1 = (-2.0f * cosw)         * a0inv;
		c.a2 = ( 1.0f - alpha)        * a0inv;
		return c;
	}

	inline float processBiquad (float in,
	                            const BQC& c,
	                            ECHOTRAudioProcessor::WetFilterBiquadState& s) noexcept
	{
		const float out = c.b0 * in + s.z1;
		s.z1 = c.b1 * in - c.a1 * out + s.z2;
		s.z2 = c.b2 * in - c.a2 * out;
		return out;
	}
}

//==============================================================================
ECHOTRAudioProcessor::ECHOTRAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
	: AudioProcessor (BusesProperties()
					 #if ! JucePlugin_IsMidiEffect
					  #if ! JucePlugin_IsSynth
					   .withInput  ("Input", juce::AudioChannelSet::stereo(), true)
					  #endif
					   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
					 #endif
					   )
#endif
	, apvts (*this, nullptr, "Parameters", createParameterLayout())
{
	// Bind parameter pointers
	timeMsParam = apvts.getRawParameterValue (kParamTimeMs);
	timeSyncParam = apvts.getRawParameterValue (kParamTimeSync);
	feedbackParam = apvts.getRawParameterValue (kParamFeedback);
	modeParam = apvts.getRawParameterValue (kParamMode);
	modParam = apvts.getRawParameterValue (kParamMod);
	inputParam = apvts.getRawParameterValue (kParamInput);
	outputParam = apvts.getRawParameterValue (kParamOutput);
	mixParam = apvts.getRawParameterValue (kParamMix);
	syncParam = apvts.getRawParameterValue (kParamSync);
	midiParam = apvts.getRawParameterValue (kParamMidi);
	autoFbkParam = apvts.getRawParameterValue (kParamAutoFbk);
	autoFbkTauParam = apvts.getRawParameterValue (kParamAutoFbkTau);
	autoFbkAttParam = apvts.getRawParameterValue (kParamAutoFbkAtt);
	reverseParam = apvts.getRawParameterValue (kParamReverse);
	reverseSmoothParam = apvts.getRawParameterValue (kParamReverseSmooth);

	filterHpFreqParam  = apvts.getRawParameterValue (kParamFilterHpFreq);
	filterLpFreqParam  = apvts.getRawParameterValue (kParamFilterLpFreq);
	filterHpSlopeParam = apvts.getRawParameterValue (kParamFilterHpSlope);
	filterLpSlopeParam = apvts.getRawParameterValue (kParamFilterLpSlope);
	filterHpOnParam    = apvts.getRawParameterValue (kParamFilterHpOn);
	filterLpOnParam    = apvts.getRawParameterValue (kParamFilterLpOn);

	tiltParam      = apvts.getRawParameterValue (kParamTilt);
	panParam       = apvts.getRawParameterValue (kParamPan);
	chaosParam     = apvts.getRawParameterValue (kParamChaos);
	chaosDelayParam= apvts.getRawParameterValue (kParamChaosD);
	chaosAmtParam  = apvts.getRawParameterValue (kParamChaosAmt);
	chaosSpdParam  = apvts.getRawParameterValue (kParamChaosSpd);
	chaosAmtFilterParam = apvts.getRawParameterValue (kParamChaosAmtFilter);
	chaosSpdFilterParam = apvts.getRawParameterValue (kParamChaosSpdFilter);
	engineParam    = apvts.getRawParameterValue (kParamEngine);
	duckParam      = apvts.getRawParameterValue (kParamDuck);
	modeInParam    = apvts.getRawParameterValue (kParamModeIn);
	modeOutParam   = apvts.getRawParameterValue (kParamModeOut);
	sumBusParam    = apvts.getRawParameterValue (kParamSumBus);
	
	uiWidthParam = apvts.getRawParameterValue (kParamUiWidth);
	uiHeightParam = apvts.getRawParameterValue (kParamUiHeight);
	uiPaletteParam = apvts.getRawParameterValue (kParamUiPalette);
	uiCrtParam = apvts.getRawParameterValue (kParamUiCrt);
	uiColorParams[0] = apvts.getRawParameterValue (kParamUiColor0);
	uiColorParams[1] = apvts.getRawParameterValue (kParamUiColor1);

	// Load UI state from parameters
	const int w = loadIntParamOrDefault (uiWidthParam, 360);
	const int h = loadIntParamOrDefault (uiHeightParam, 480);
	uiEditorWidth.store (w, std::memory_order_relaxed);
	uiEditorHeight.store (h, std::memory_order_relaxed);

	// Performance tracing: auto-dump to Desktop on plugin unload
	perfTrace.enableDesktopAutoDump();
}

ECHOTRAudioProcessor::~ECHOTRAudioProcessor()
{
}

//==============================================================================
const juce::String ECHOTRAudioProcessor::getName() const
{
	return JucePlugin_Name;
}

bool ECHOTRAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
	return true;
   #else
	return false;
   #endif
}

bool ECHOTRAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
	return true;
   #else
	return false;
   #endif
}

bool ECHOTRAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
	return true;
   #else
	return false;
   #endif
}

double ECHOTRAudioProcessor::getTailLengthSeconds() const
{
	// Calculate tail length based on delay time and feedback
	// This ensures DAWs don't cut off the delay tail prematurely
	const bool syncEnabled = loadBoolParamOrDefault (syncParam, false);
	const float timeMsValue = loadAtomicOrDefault (timeMsParam, kTimeMsDefault);
	const int timeSyncValue = loadIntParamOrDefault (timeSyncParam, kTimeSyncDefault);
	float feedback = loadAtomicOrDefault (feedbackParam, kFeedbackDefault);
	feedback = juce::jlimit (kFeedbackMin, kFeedbackMax, feedback);
	{
		const float sign = feedback < 0.0f ? -1.0f : 1.0f;
		const float af   = std::abs (feedback);
		feedback         = sign * af * af * (3.0f - 2.0f * af);
	}
	
	// Get delay time
	float delayMs = timeMsValue;
	if (syncEnabled)
	{
		// Use last known BPM or default
		double bpm = 120.0;
		auto posInfo = getPlayHead();
		if (posInfo != nullptr)
		{
			auto pos = posInfo->getPosition();
			if (pos.hasValue() && pos->getBpm().hasValue())
				bpm = *pos->getBpm();
		}
		delayMs = tempoSyncToMs (timeSyncValue, bpm);
	}
	
	// Clamp to valid range (different limits for manual vs sync mode)
	const float maxAllowedDelayMs = syncEnabled ? kTimeMsMaxSync : kTimeMsMax;
	delayMs = juce::jlimit (0.0f, maxAllowedDelayMs, delayMs);
	
	// Calculate tail: time for signal to decay to -60dB (~0.001 amplitude)
	// Formula: tailTime = delayTime * log(0.001) / log(feedback)
	// But we need to handle edge cases
	const float absFeedback = std::abs (feedback);
	if (absFeedback < 0.01f || delayMs < 0.5f)
		return 0.5; // Minimum 0.5s tail for very low feedback
	
	if (absFeedback >= 1.0f)
		return 30.0; // feedback >= 100% → infinite sustain / self-oscillation
	
	const double delaySeconds = delayMs / 1000.0;
	const double numRepeatsTo60dB = std::log (0.001) / std::log ((double) absFeedback);
	const double tailSeconds = delaySeconds * numRepeatsTo60dB;
	
	// Clamp to reasonable range
	return juce::jlimit (0.5, 30.0, tailSeconds);
}

int ECHOTRAudioProcessor::getNumPrograms()
{
	return 1;
}

int ECHOTRAudioProcessor::getCurrentProgram()
{
	return 0;
}

void ECHOTRAudioProcessor::setCurrentProgram (int index)
{
	juce::ignoreUnused (index);
}

const juce::String ECHOTRAudioProcessor::getProgramName (int index)
{
	juce::ignoreUnused (index);
	return {};
}

void ECHOTRAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
	juce::ignoreUnused (index, newName);
}

//==============================================================================
void ECHOTRAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
	juce::ignoreUnused (samplesPerBlock);
	currentSampleRate = sampleRate;
	cachedDelaySmoothCoeff = smoothCoeffFromTau (sampleRate, kDelaySmoothTauSeconds);
	
	// Allocate delay buffer (power of 2 for bitwise wrap)
	const int requestedSamples = (int) std::ceil (sampleRate * (kTimeMsMaxSync / 1000.0)) + 1024;
	// Round up to next power of 2
	int powerOf2 = 1;
	while (powerOf2 < requestedSamples)
		powerOf2 <<= 1;
	
	delayBufferLength = powerOf2;
	delayBuffer.setSize (2, delayBufferLength);
	delayBuffer.clear();
	delayBufferWritePos = 0;
	feedbackState[0] = 0.0f;
	feedbackState[1] = 0.0f;
	smoothedDelaySamples = 0.0f;
	smoothedDelaySamplesR = 0.0f;
	smoothedInputGain = 1.0f;
	smoothedOutputGain = 1.0f;
	smoothedMix = 0.5f;

	// Reset reverse delay state
	reverseAnchor      = 0;
	reverseCounter     = 0.0f;
	reverseChunkLen    = 0.0f;
	revSmoothedDelay   = 0.0f;
	reverseNeedsInit   = true;

	// Reset auto-feedback envelope
	autoFbkEnvelope         = 1.0f;
	autoFbkLastDelaySamples = -1.0f;
	autoFbkCooldownLeft     = 0;

	// Reset feedback loop processing state
	fbkDcStateInL = fbkDcStateInR = 0.0f;
	fbkDcStateOutL = fbkDcStateOutR = 0.0f;

	// Reset wet-signal HP/LP filter state
	wetFilterState_[0].reset();
	wetFilterState_[1].reset();
	smoothedFilterHpFreq_ = loadAtomicOrDefault (filterHpFreqParam, kFilterHpFreqDefault);
	smoothedFilterLpFreq_ = loadAtomicOrDefault (filterLpFreqParam, kFilterLpFreqDefault);
	lastCalcHpFreq_ = -1.0f; lastCalcLpFreq_ = -1.0f;
	lastCalcHpSlope_ = -1;   lastCalcLpSlope_ = -1;
	filterCoeffCountdown_ = 0;
	updateFilterCoeffs (true, true);

	// Reset tilt state
	tiltDb_ = 0.0f;
	tiltB0_ = 1.0f; tiltB1_ = 0.0f; tiltA1_ = 0.0f;
	tiltTargetB0_ = 1.0f; tiltTargetB1_ = 0.0f; tiltTargetA1_ = 0.0f;
	tiltState_[0] = tiltState_[1] = 0.0f;
	lastTiltDb_ = 0.0f;
	tiltSmoothSc_ = 1.0f - std::exp (-1.0f / (static_cast<float> (currentSampleRate) * 0.03f));

	// Reset chaos state
	chaosFilterEnabled_ = false;
	chaosDelayEnabled_  = false;
	chaosAmtD_ = 0.0f; chaosAmtF_ = 0.0f;
	chaosShPeriodD_ = 8820.0f; smoothedChaosShPeriodD_ = 8820.0f;
	chaosShPeriodF_ = 8820.0f; smoothedChaosShPeriodF_ = 8820.0f;
	chaosDelayMaxSamples_ = 0.0f; smoothedChaosDelayMaxSamples_ = 0.0f;
	chaosGainMaxDb_ = 0.0f; smoothedChaosGainMaxDb_ = 0.0f;
	chaosFilterMaxOct_ = 0.0f; smoothedChaosFilterMaxOct_ = 0.0f;
	chaosDPhase_ = 0.0f; chaosDTarget_ = 0.0f; chaosDSmoothed_ = 0.0f;
	chaosGPhase_ = 0.0f; chaosGTarget_ = 0.0f; chaosGSmoothed_ = 0.0f;
	chaosFPhase_ = 0.0f; chaosFTarget_ = 0.0f; chaosFSmoothed_ = 0.0f;
	std::memset (chaosDelayBuf_, 0, sizeof (chaosDelayBuf_));
	chaosDelayWritePos_ = 0;

	// Reset engine state
	engineMode_ = 0;
	prevEngineMode_ = 0;
	engineLp1StateL_ = 0.0f; engineLp1StateR_ = 0.0f;
	engineLp2StateL_ = 0.0f; engineLp2StateR_ = 0.0f;
	engineLpCoeff_ = 0.0f;
	engineDriftPhase_ = 0.0f;
	engineDriftTarget_ = 1.0f;
	engineDriftSmoothed_ = 1.0f;
	smoothedFeedback_ = 0.0f;
	smoothedDuck_ = 0.0f;
	duckEnvelope_ = 0.0f;
	duckAmount_ = 0.0f;
	{
		const double sr = getSampleRate();
		duckAttackCoeff_  = static_cast<float> (1.0 - std::exp (-1.0 / (sr * 0.0005)));  // ~0.5 ms
		duckReleaseCoeff_ = static_cast<float> (1.0 - std::exp (-1.0 / (sr * 0.250)));   // ~250 ms
	}
	smoothedChaosFilterMaxOct_ = 0.0f;
	smoothedChaosDelayMaxSamples_ = 0.0f;
	chaosParamSmoothCoeff_ = 0.999f;
}

void ECHOTRAudioProcessor::releaseResources()
{
	delayBuffer.setSize (0, 0);
	delayBufferLength = 0;
	delayBufferWritePos = 0;
	reverseAnchor      = 0;
	reverseCounter     = 0.0f;
	reverseChunkLen    = 0.0f;
	revSmoothedDelay   = 0.0f;
	reverseNeedsInit   = true;
	autoFbkEnvelope         = 1.0f;
	autoFbkLastDelaySamples = -1.0f;
	autoFbkCooldownLeft     = 0;
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ECHOTRAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
	juce::ignoreUnused (layouts);
	return true;
  #else
	if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
	 && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
		return false;

   #if ! JucePlugin_IsSynth
	if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
		return false;
   #endif

	return true;
  #endif
}
#endif

//==============================================================================
// Wet-signal HP/LP filter coefficient update

void ECHOTRAudioProcessor::updateFilterCoeffs (bool forceHp, bool forceLp)
{
	const float sr = (float) currentSampleRate;
	const int hpSlope = juce::roundToInt (loadAtomicOrDefault (filterHpSlopeParam, (float) kFilterSlopeDefault));
	const int lpSlope = juce::roundToInt (loadAtomicOrDefault (filterLpSlopeParam, (float) kFilterSlopeDefault));

	if (forceHp || hpSlope != lastCalcHpSlope_ || std::abs (smoothedFilterHpFreq_ - lastCalcHpFreq_) > 0.01f)
	{
		lastCalcHpFreq_ = smoothedFilterHpFreq_;
		lastCalcHpSlope_ = hpSlope;
		if (hpSlope == 0)
		{
			hpCoeffs_[0] = calcOnePoleHP (smoothedFilterHpFreq_, sr);
			hpCoeffs_[1] = {};
		}
		else if (hpSlope == 1)
		{
			hpCoeffs_[0] = calcBiquadHP (smoothedFilterHpFreq_, sr, kBW2_Q);
			hpCoeffs_[1] = {};
		}
		else
		{
			hpCoeffs_[0] = calcBiquadHP (smoothedFilterHpFreq_, sr, kBW4_Q1);
			hpCoeffs_[1] = calcBiquadHP (smoothedFilterHpFreq_, sr, kBW4_Q2);
		}
	}

	if (forceLp || lpSlope != lastCalcLpSlope_ || std::abs (smoothedFilterLpFreq_ - lastCalcLpFreq_) > 0.01f)
	{
		lastCalcLpFreq_ = smoothedFilterLpFreq_;
		lastCalcLpSlope_ = lpSlope;
		if (lpSlope == 0)
		{
			lpCoeffs_[0] = calcOnePoleLP (smoothedFilterLpFreq_, sr);
			lpCoeffs_[1] = {};
		}
		else if (lpSlope == 1)
		{
			lpCoeffs_[0] = calcBiquadLP (smoothedFilterLpFreq_, sr, kBW2_Q);
			lpCoeffs_[1] = {};
		}
		else
		{
			lpCoeffs_[0] = calcBiquadLP (smoothedFilterLpFreq_, sr, kBW4_Q1);
			lpCoeffs_[1] = calcBiquadLP (smoothedFilterLpFreq_, sr, kBW4_Q2);
		}
	}
}

void ECHOTRAudioProcessor::filterWetSample (float& wetL, float& wetR)
{
	float hpTarget = wetFilterTargetHpFreq_;
	float lpTarget = wetFilterTargetLpFreq_;

	// ── CHAOS filter modulation (S&H already advanced in delay loop) ──
	if (chaosFilterEnabled_ && chaosAmtF_ > 0.01f)
	{
		const float octaveShift = chaosFSmoothed_ * smoothedChaosFilterMaxOct_;
		const float freqMult = std::exp2 (octaveShift);
		// When HP/LP knobs are off, chaos sweeps the full 20–20k range
		const float hpBase = wetFilterHpOn_ ? hpTarget : kFilterFreqMin;
		const float lpBase = wetFilterLpOn_ ? lpTarget : kFilterFreqMax;
		hpTarget = juce::jlimit (kFilterFreqMin, kFilterFreqMax, hpBase * freqMult);
		lpTarget = juce::jlimit (kFilterFreqMin, kFilterFreqMax, lpBase * freqMult);
	}

	// EMA frequency smoothing
	smoothedFilterHpFreq_ += (hpTarget - smoothedFilterHpFreq_) * (1.0f - kGainSmoothCoeff);
	smoothedFilterLpFreq_ += (lpTarget - smoothedFilterLpFreq_) * (1.0f - kGainSmoothCoeff);

	// Batched coefficient update
	if (--filterCoeffCountdown_ <= 0)
	{
		filterCoeffCountdown_ = kFilterCoeffUpdateInterval;
		updateFilterCoeffs (false, false);
	}

	// Apply HP cascade (also when chaos modulates frequency)
	const bool chaosFilterActive = chaosFilterEnabled_ && chaosAmtF_ > 0.01f;
	if (wetFilterHpOn_ || chaosFilterActive)
	{
		for (int s = 0; s < wetFilterNumSectionsHp_; ++s)
		{
			wetL = processBiquad (wetL, hpCoeffs_[s], wetFilterState_[0].hp[s]);
			wetR = processBiquad (wetR, hpCoeffs_[s], wetFilterState_[1].hp[s]);
		}
	}

	// Apply LP cascade (also when chaos modulates frequency)
	if (wetFilterLpOn_ || chaosFilterActive)
	{
		for (int s = 0; s < wetFilterNumSectionsLp_; ++s)
		{
			wetL = processBiquad (wetL, lpCoeffs_[s], wetFilterState_[0].lp[s]);
			wetR = processBiquad (wetR, lpCoeffs_[s], wetFilterState_[1].lp[s]);
		}
	}

	// ── TILT filter ──
	if (std::abs (tiltDb_) > 0.05f)
	{
		if (std::abs (tiltDb_ - lastTiltDb_) > 0.02f)
		{
			lastTiltDb_ = tiltDb_;
			const double pivot = 1000.0;
			const double octToNy = std::log2 ((currentSampleRate * 0.5) / pivot);
			const double gainNyDb = static_cast<double> (tiltDb_) * octToNy;
			const double gNy = std::pow (10.0, gainNyDb / 20.0);
			const double wc = 2.0 * currentSampleRate
			                * std::tan (juce::MathConstants<double>::pi * pivot / currentSampleRate);
			const double K = wc / (2.0 * currentSampleRate);
			const double g = std::sqrt (gNy);
			const double norm = 1.0 / (1.0 + K * g);
			tiltTargetB0_ = static_cast<float> ((g + K) * norm);
			tiltTargetB1_ = static_cast<float> ((K - g) * norm);
			tiltTargetA1_ = static_cast<float> ((K * g - 1.0) * norm);
		}

		const float sc = tiltSmoothSc_;
		tiltB0_ += (tiltTargetB0_ - tiltB0_) * sc;
		tiltB1_ += (tiltTargetB1_ - tiltB1_) * sc;
		tiltA1_ += (tiltTargetA1_ - tiltA1_) * sc;

		{
			const float x = wetL;
			const float y = tiltB0_ * x + tiltState_[0];
			tiltState_[0] = tiltB1_ * x - tiltA1_ * y;
			wetL = y;
		}
		{
			const float x = wetR;
			const float y = tiltB0_ * x + tiltState_[1];
			tiltState_[1] = tiltB1_ * x - tiltA1_ * y;
			wetR = y;
		}
	}
	else if (std::abs (lastTiltDb_) > 0.05f)
	{
		lastTiltDb_ = 0.0f;
		tiltB0_ = 1.0f; tiltB1_ = 0.0f; tiltA1_ = 0.0f;
		tiltTargetB0_ = 1.0f; tiltTargetB1_ = 0.0f; tiltTargetA1_ = 0.0f;
		tiltState_[0] = tiltState_[1] = 0.0f;
	}
}

//==============================================================================
// Optimized delay processing functions

void ECHOTRAudioProcessor::processStereoDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
                                                float delaySamples, float feedback, float inputGain, 
                                                float outputGain, float mix, float delaySmoothCoeff)
{
	auto* channelL = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
	auto* channelR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
	
	// Get raw pointers to delay buffer for maximum performance
	auto* delayL = delayBuffer.getWritePointer (0);
	auto* delayR = delayBuffer.getWritePointer (1);
	
	const int wrapMask = delayBufferLength - 1;
	int writePos = delayBufferWritePos;
	
	const float smoothCoeff = delaySmoothCoeff;
	const float targetDelay = delaySamples;
	
	for (int i = 0; i < numSamples; ++i)
	{
		smoothedDelaySamples = smoothedDelaySamples * smoothCoeff + targetDelay * (1.0f - smoothCoeff);
		if (chaosDelayEnabled_) advanceChaosD();
		if (chaosFilterEnabled_) advanceChaosF();
		smoothedInputGain  = smoothedInputGain  * kGainSmoothCoeff + inputGain  * (1.0f - kGainSmoothCoeff);
		smoothedOutputGain = smoothedOutputGain * kGainSmoothCoeff + outputGain * (1.0f - kGainSmoothCoeff);
		smoothedMix        = smoothedMix        * kGainSmoothCoeff + mix        * (1.0f - kGainSmoothCoeff);
		smoothedFeedback_  = smoothedFeedback_  * kGainSmoothCoeff + feedback   * (1.0f - kGainSmoothCoeff);
		
		float readPosF = (float) writePos - smoothedDelaySamples;
		if (readPosF < 0.0f)
			readPosF += (float) delayBufferLength;
		
		const int idx0  = static_cast<int>(readPosF) & wrapMask;
		const int idxM1 = (idx0 + wrapMask) & wrapMask;
		const int idx1  = (idx0 + 1) & wrapMask;
		const int idx2  = (idx0 + 2) & wrapMask;
		const float frac = readPosF - static_cast<float>(static_cast<int>(readPosF));
		
		if (channelL != nullptr)
		{
			const float inputL = channelL[i];
			const float delayedL = hermite4pt (delayL[idxM1], delayL[idx0], delayL[idx1], delayL[idx2], frac);

			if (channelR != nullptr)
			{
				const float inputR = channelR[i];
				const float delayedR = hermite4pt (delayR[idxM1], delayR[idx0], delayR[idx1], delayR[idx2], frac);
				const float duckGain = advanceDuck (inputL, inputR);
				float fbkL = delayedL * smoothedFeedback_;
				float fbkR = delayedR * smoothedFeedback_;

				// DC blocker (transparent feedback path)
				fbkL = dcBlockTick (fbkL, fbkDcStateInL, fbkDcStateOutL, fbkDcCoeff);
				fbkR = dcBlockTick (fbkR, fbkDcStateInR, fbkDcStateOutR, fbkDcCoeff);

				applyEngineToFeedback (fbkL, fbkR);
				fbkL *= duckGain;
				fbkR *= duckGain;

				delayL[writePos] = inputL * smoothedInputGain + fbkL;
				delayR[writePos] = inputR * smoothedInputGain + fbkR;

				float wetL = delayedL, wetR = delayedR;
				if (engineMode_ == 1) applyAnalogOutputSat (wetL, wetR);
				filterWetSample (wetL, wetR);
				if (chaosDelayEnabled_) applyChaosDelay (wetL, wetR);
				channelL[i] = inputL * (1.0f - smoothedMix) + wetL * smoothedMix * smoothedOutputGain * duckGain;
				channelR[i] = inputR * (1.0f - smoothedMix) + wetR * smoothedMix * smoothedOutputGain * duckGain;
			}
			else
			{
				const float duckGain = advanceDuck (inputL, inputL);
				float fbkL = delayedL * smoothedFeedback_;
				// DC blocker (transparent feedback path)
				fbkL = dcBlockTick (fbkL, fbkDcStateInL, fbkDcStateOutL, fbkDcCoeff);
				{ float fbkR = fbkL; applyEngineToFeedback (fbkL, fbkR); }
				fbkL *= duckGain;
				delayL[writePos] = inputL * smoothedInputGain + fbkL;
				float wetL = delayedL, wetR = delayedL;
				if (engineMode_ == 1) applyAnalogOutputSat (wetL, wetR);
				filterWetSample (wetL, wetR);
				if (chaosDelayEnabled_) applyChaosDelay (wetL, wetR);
				channelL[i] = inputL * (1.0f - smoothedMix) + wetL * smoothedMix * smoothedOutputGain * duckGain;
			}
		}
		
		writePos = (writePos + 1) & wrapMask;
	}
	
	delayBufferWritePos = writePos;
}

void ECHOTRAudioProcessor::processMonoDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
                                              float delaySamples, float feedback, float inputGain,
                                              float outputGain, float mix, float delaySmoothCoeff)
{
	auto* channelL = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
	auto* channelR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
	
	auto* delayL = delayBuffer.getWritePointer (0);
	auto* delayR = delayBuffer.getWritePointer (1);
	
	const int wrapMask = delayBufferLength - 1;
	int writePos = delayBufferWritePos;
	
	const float smoothCoeff = delaySmoothCoeff;
	const float targetDelay = delaySamples;
	
	for (int i = 0; i < numSamples; ++i)
	{
		smoothedDelaySamples = smoothedDelaySamples * smoothCoeff + targetDelay * (1.0f - smoothCoeff);
		if (chaosDelayEnabled_) advanceChaosD();
		if (chaosFilterEnabled_) advanceChaosF();
		smoothedInputGain  = smoothedInputGain  * kGainSmoothCoeff + inputGain  * (1.0f - kGainSmoothCoeff);
		smoothedOutputGain = smoothedOutputGain * kGainSmoothCoeff + outputGain * (1.0f - kGainSmoothCoeff);
		smoothedMix        = smoothedMix        * kGainSmoothCoeff + mix        * (1.0f - kGainSmoothCoeff);
		smoothedFeedback_  = smoothedFeedback_  * kGainSmoothCoeff + feedback   * (1.0f - kGainSmoothCoeff);
		float readPosF = (float) writePos - smoothedDelaySamples;
		if (readPosF < 0.0f)
			readPosF += (float) delayBufferLength;
		
		const int idx0  = static_cast<int>(readPosF) & wrapMask;
		const int idxM1 = (idx0 + wrapMask) & wrapMask;
		const int idx1  = (idx0 + 1) & wrapMask;
		const int idx2  = (idx0 + 2) & wrapMask;
		const float frac = readPosF - static_cast<float>(static_cast<int>(readPosF));
		
		const float delayed = hermite4pt (delayL[idxM1], delayL[idx0], delayL[idx1], delayL[idx2], frac);
		
		const float inputL = channelL != nullptr ? channelL[i] : 0.0f;
		const float inputR = channelR != nullptr ? channelR[i] : inputL;
		const float inputMid = (inputL + inputR) * 0.5f;
		const float duckGain = advanceDuck (inputL, inputR);
		
		float fbkMono = delayed * smoothedFeedback_;

		// DC blocker (transparent feedback path)
		fbkMono = dcBlockTick (fbkMono, fbkDcStateInL, fbkDcStateOutL, fbkDcCoeff);
		{ float fbkR = fbkMono; applyEngineToFeedback (fbkMono, fbkR); }
		fbkMono *= duckGain;

		const float toWrite = inputMid * smoothedInputGain + fbkMono;
		delayL[writePos] = toWrite;
		delayR[writePos] = toWrite;

		float wetL = delayed, wetR = delayed;
		filterWetSample (wetL, wetR);
		if (chaosDelayEnabled_) applyChaosDelay (wetL, wetR);
		if (channelL != nullptr) channelL[i] = inputL * (1.0f - smoothedMix) + wetL * smoothedMix * smoothedOutputGain * duckGain;
		if (channelR != nullptr) channelR[i] = inputR * (1.0f - smoothedMix) + wetR * smoothedMix * smoothedOutputGain * duckGain;
		
		writePos = (writePos + 1) & wrapMask;
	}
	
	delayBufferWritePos = writePos;
}

void ECHOTRAudioProcessor::processPingPongDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
                                                  float delaySamples, float feedback, float inputGain,
                                                  float outputGain, float mix, float delaySmoothCoeff)
{
	auto* channelL = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
	auto* channelR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
	
	auto* delayL = delayBuffer.getWritePointer (0);
	auto* delayR = delayBuffer.getWritePointer (1);
	
	const int wrapMask = delayBufferLength - 1;
	int writePos = delayBufferWritePos;
	
	const float smoothCoeff = delaySmoothCoeff;
	const float targetDelay = delaySamples;
	
	for (int i = 0; i < numSamples; ++i)
	{
		smoothedDelaySamples = smoothedDelaySamples * smoothCoeff + targetDelay * (1.0f - smoothCoeff);
		if (chaosDelayEnabled_) advanceChaosD();
		if (chaosFilterEnabled_) advanceChaosF();
		smoothedInputGain  = smoothedInputGain  * kGainSmoothCoeff + inputGain  * (1.0f - kGainSmoothCoeff);
		smoothedOutputGain = smoothedOutputGain * kGainSmoothCoeff + outputGain * (1.0f - kGainSmoothCoeff);
		smoothedMix        = smoothedMix        * kGainSmoothCoeff + mix        * (1.0f - kGainSmoothCoeff);
		smoothedFeedback_  = smoothedFeedback_  * kGainSmoothCoeff + feedback   * (1.0f - kGainSmoothCoeff);
		float readPosF = (float) writePos - smoothedDelaySamples;
		if (readPosF < 0.0f)
			readPosF += (float) delayBufferLength;
		
		const int idx0  = static_cast<int>(readPosF) & wrapMask;
		const int idxM1 = (idx0 + wrapMask) & wrapMask;
		const int idx1  = (idx0 + 1) & wrapMask;
		const int idx2  = (idx0 + 2) & wrapMask;
		const float frac = readPosF - static_cast<float>(static_cast<int>(readPosF));
		
		const float delayedL = hermite4pt (delayL[idxM1], delayL[idx0], delayL[idx1], delayL[idx2], frac);
		const float delayedR = hermite4pt (delayR[idxM1], delayR[idx0], delayR[idx1], delayR[idx2], frac);
		
		const float inputL = channelL != nullptr ? channelL[i] : 0.0f;
		const float inputR = channelR != nullptr ? channelR[i] : 0.0f;
		const float inputMono = (inputL + inputR) * 0.5f;
		const float duckGain = advanceDuck (inputL, inputR);
		
		float fbkPpL = delayedR * smoothedFeedback_;
		float fbkPpR = delayedL * smoothedFeedback_;

		// DC blocker (transparent feedback path)
		fbkPpL = dcBlockTick (fbkPpL, fbkDcStateInL, fbkDcStateOutL, fbkDcCoeff);
		fbkPpR = dcBlockTick (fbkPpR, fbkDcStateInR, fbkDcStateOutR, fbkDcCoeff);

		applyEngineToFeedback (fbkPpL, fbkPpR);
		fbkPpL *= duckGain;
		fbkPpR *= duckGain;

		delayL[writePos] = inputMono * smoothedInputGain + fbkPpL;
		delayR[writePos] = fbkPpR;

		float wetL = delayedL, wetR = delayedR;
		if (engineMode_ == 1) applyAnalogOutputSat (wetL, wetR);
		filterWetSample (wetL, wetR);
		if (chaosDelayEnabled_) applyChaosDelay (wetL, wetR);
		if (channelL != nullptr) channelL[i] = inputL * (1.0f - smoothedMix) + wetL * smoothedMix * smoothedOutputGain * duckGain;
		if (channelR != nullptr) channelR[i] = inputR * (1.0f - smoothedMix) + wetR * smoothedMix * smoothedOutputGain * duckGain;
		
		writePos = (writePos + 1) & wrapMask;
	}
	
	delayBufferWritePos = writePos;
}

void ECHOTRAudioProcessor::processWideDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
                                              float delaySamples, float feedback, float inputGain,
                                              float outputGain, float mix, float delaySmoothCoeff)
{
	auto* channelL = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
	auto* channelR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

	auto* delayL = delayBuffer.getWritePointer (0);
	auto* delayR = delayBuffer.getWritePointer (1);

	const int wrapMask = delayBufferLength - 1;
	int writePos = delayBufferWritePos;

	const float smoothCoeff = delaySmoothCoeff;

	// Cross-feedback round trip = T_L + T_R.  For the comb resonance to match
	// self-feedback pitch (period T), we need T_L + T_R = T.
	// Ratio 2:1 (octave between channels) ⇒ T_L = 2T/3, T_R = T/3.
	const float targetDelayL = delaySamples * (2.0f / 3.0f);
	const float targetDelayR = delaySamples * (1.0f / 3.0f);

	for (int i = 0; i < numSamples; ++i)
	{
		smoothedDelaySamples  = smoothedDelaySamples  * smoothCoeff + targetDelayL * (1.0f - smoothCoeff);
		smoothedDelaySamplesR = smoothedDelaySamplesR * smoothCoeff + targetDelayR * (1.0f - smoothCoeff);
		if (chaosDelayEnabled_) advanceChaosD();
		if (chaosFilterEnabled_) advanceChaosF();
		smoothedInputGain  = smoothedInputGain  * kGainSmoothCoeff + inputGain  * (1.0f - kGainSmoothCoeff);
		smoothedOutputGain = smoothedOutputGain * kGainSmoothCoeff + outputGain * (1.0f - kGainSmoothCoeff);
		smoothedMix        = smoothedMix        * kGainSmoothCoeff + mix        * (1.0f - kGainSmoothCoeff);
		smoothedFeedback_  = smoothedFeedback_  * kGainSmoothCoeff + feedback   * (1.0f - kGainSmoothCoeff);

		// L channel read position
		float readPosL = (float) writePos - smoothedDelaySamples;
		if (readPosL < 0.0f)
			readPosL += (float) delayBufferLength;

		const int idxL0  = static_cast<int>(readPosL) & wrapMask;
		const int idxLM1 = (idxL0 + wrapMask) & wrapMask;
		const int idxL1  = (idxL0 + 1) & wrapMask;
		const int idxL2  = (idxL0 + 2) & wrapMask;
		const float fracL = readPosL - static_cast<float>(static_cast<int>(readPosL));

		// R channel read position (golden ratio offset)
		float readPosR = (float) writePos - smoothedDelaySamplesR;
		if (readPosR < 0.0f)
			readPosR += (float) delayBufferLength;

		const int idxR0  = static_cast<int>(readPosR) & wrapMask;
		const int idxRM1 = (idxR0 + wrapMask) & wrapMask;
		const int idxR1  = (idxR0 + 1) & wrapMask;
		const int idxR2  = (idxR0 + 2) & wrapMask;
		const float fracR = readPosR - static_cast<float>(static_cast<int>(readPosR));

		const float delayedL = hermite4pt (delayL[idxLM1], delayL[idxL0], delayL[idxL1], delayL[idxL2], fracL);
		const float delayedR = hermite4pt (delayR[idxRM1], delayR[idxR0], delayR[idxR1], delayR[idxR2], fracR);

		const float inputL = channelL != nullptr ? channelL[i] : 0.0f;
		const float inputR = channelR != nullptr ? channelR[i] : 0.0f;
		const float duckGain = advanceDuck (inputL, inputR);

		// Cross-feedback: L reads from R delay, R reads from L delay
		// Both channels receive their own stereo input
		float fbkL = delayedR * smoothedFeedback_;
		float fbkR = delayedL * smoothedFeedback_;

		fbkL = dcBlockTick (fbkL, fbkDcStateInL, fbkDcStateOutL, fbkDcCoeff);
		fbkR = dcBlockTick (fbkR, fbkDcStateInR, fbkDcStateOutR, fbkDcCoeff);

		applyEngineToFeedback (fbkL, fbkR);
		fbkL *= duckGain;
		fbkR *= duckGain;

		delayL[writePos] = inputL * smoothedInputGain + fbkL;
		delayR[writePos] = inputR * smoothedInputGain + fbkR;

		float wetL = delayedL, wetR = delayedR;
		if (engineMode_ == 1) applyAnalogOutputSat (wetL, wetR);
		filterWetSample (wetL, wetR);
		if (chaosDelayEnabled_) applyChaosDelay (wetL, wetR);
		if (channelL != nullptr) channelL[i] = inputL * (1.0f - smoothedMix) + wetL * smoothedMix * smoothedOutputGain * duckGain;
		if (channelR != nullptr) channelR[i] = inputR * (1.0f - smoothedMix) + wetR * smoothedMix * smoothedOutputGain * duckGain;

		writePos = (writePos + 1) & wrapMask;
	}

	delayBufferWritePos = writePos;
}

void ECHOTRAudioProcessor::processDualDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
                                              float delaySamples, float feedback, float inputGain,
                                              float outputGain, float mix, float delaySmoothCoeff)
{
	auto* channelL = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
	auto* channelR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

	auto* delayL = delayBuffer.getWritePointer (0);
	auto* delayR = delayBuffer.getWritePointer (1);

	const int wrapMask = delayBufferLength - 1;
	int writePos = delayBufferWritePos;

	const float smoothCoeff = delaySmoothCoeff;
	const float targetDelayL = delaySamples;
	const float targetDelayR = delaySamples * 0.5f; // Half-time offset for R channel

	for (int i = 0; i < numSamples; ++i)
	{
		smoothedDelaySamples  = smoothedDelaySamples  * smoothCoeff + targetDelayL * (1.0f - smoothCoeff);
		smoothedDelaySamplesR = smoothedDelaySamplesR * smoothCoeff + targetDelayR * (1.0f - smoothCoeff);
		if (chaosDelayEnabled_) advanceChaosD();
		if (chaosFilterEnabled_) advanceChaosF();
		smoothedInputGain  = smoothedInputGain  * kGainSmoothCoeff + inputGain  * (1.0f - kGainSmoothCoeff);
		smoothedOutputGain = smoothedOutputGain * kGainSmoothCoeff + outputGain * (1.0f - kGainSmoothCoeff);
		smoothedMix        = smoothedMix        * kGainSmoothCoeff + mix        * (1.0f - kGainSmoothCoeff);
		smoothedFeedback_  = smoothedFeedback_  * kGainSmoothCoeff + feedback   * (1.0f - kGainSmoothCoeff);

		// L channel read position
		float readPosL = (float) writePos - smoothedDelaySamples;
		if (readPosL < 0.0f)
			readPosL += (float) delayBufferLength;

		const int idxL0  = static_cast<int>(readPosL) & wrapMask;
		const int idxLM1 = (idxL0 + wrapMask) & wrapMask;
		const int idxL1  = (idxL0 + 1) & wrapMask;
		const int idxL2  = (idxL0 + 2) & wrapMask;
		const float fracL = readPosL - static_cast<float>(static_cast<int>(readPosL));

		// R channel read position (half-time offset)
		float readPosR = (float) writePos - smoothedDelaySamplesR;
		if (readPosR < 0.0f)
			readPosR += (float) delayBufferLength;

		const int idxR0  = static_cast<int>(readPosR) & wrapMask;
		const int idxRM1 = (idxR0 + wrapMask) & wrapMask;
		const int idxR1  = (idxR0 + 1) & wrapMask;
		const int idxR2  = (idxR0 + 2) & wrapMask;
		const float fracR = readPosR - static_cast<float>(static_cast<int>(readPosR));

		const float delayedL = hermite4pt (delayL[idxLM1], delayL[idxL0], delayL[idxL1], delayL[idxL2], fracL);
		const float delayedR = hermite4pt (delayR[idxRM1], delayR[idxR0], delayR[idxR1], delayR[idxR2], fracR);

		const float inputL = channelL != nullptr ? channelL[i] : 0.0f;
		const float inputR = channelR != nullptr ? channelR[i] : 0.0f;
		const float duckGain = advanceDuck (inputL, inputR);

		// Independent feedback: no cross-feedback between L and R
		float fbkL = delayedL * smoothedFeedback_;
		float fbkR = delayedR * smoothedFeedback_;

		fbkL = dcBlockTick (fbkL, fbkDcStateInL, fbkDcStateOutL, fbkDcCoeff);
		fbkR = dcBlockTick (fbkR, fbkDcStateInR, fbkDcStateOutR, fbkDcCoeff);

		applyEngineToFeedback (fbkL, fbkR);
		fbkL *= duckGain;
		fbkR *= duckGain;

		delayL[writePos] = inputL * smoothedInputGain + fbkL;
		delayR[writePos] = inputR * smoothedInputGain + fbkR;

		float wetL = delayedL, wetR = delayedR;
		if (engineMode_ == 1) applyAnalogOutputSat (wetL, wetR);
		filterWetSample (wetL, wetR);
		if (chaosDelayEnabled_) applyChaosDelay (wetL, wetR);
		if (channelL != nullptr) channelL[i] = inputL * (1.0f - smoothedMix) + wetL * smoothedMix * smoothedOutputGain * duckGain;
		if (channelR != nullptr) channelR[i] = inputR * (1.0f - smoothedMix) + wetR * smoothedMix * smoothedOutputGain * duckGain;

		writePos = (writePos + 1) & wrapMask;
	}

	delayBufferWritePos = writePos;
}

void ECHOTRAudioProcessor::processReverseDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
                                                 float delaySamples, float feedback, float inputGain,
                                                 float outputGain, float mix, float delaySmoothCoeff,
                                                 float smoothMult, int mode)
{
	// ══════════════════════════════════════════════════════════════════
	// STYLE-AWARE REVERSE DELAY — forward feedback, proportional taper
	// ══════════════════════════════════════════════════════════════════
	//
	// Feedback path mirrors the forward-mode routing for each STYLE
	// (cross-feedback for WIDE/PING-PONG, independent for others, etc.).
	// WIDE/DUAL use per-channel forward-read delays for feedback but
	// share a single reverse chunk based on the user's nominal delay T.
	//
	// OUTPUT reads BACKWARD (single voice, chunk = EMA-smoothed delay).
	// Taper is PROPORTIONAL to chunk length (1/16th, scaled by SMOOTH).

	auto* channelL = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
	auto* channelR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
	auto* delayL = delayBuffer.getWritePointer (0);
	auto* delayR = delayBuffer.getWritePointer (1);

	const int wrapMask = delayBufferLength - 1;
	int writePos = delayBufferWritePos;

	const float smoothCoeff = delaySmoothCoeff;

	// Per-style feedback delay ratios (applied to the smoothed T)
	float fbkRatioL = 1.0f, fbkRatioR = 1.0f;
	if (mode == 2)      { fbkRatioL = 2.0f / 3.0f; fbkRatioR = 1.0f / 3.0f; } // WIDE
	else if (mode == 3) { fbkRatioR = 0.5f; } // DUAL

	const bool crossFeedback = (mode == 2 || mode == 4); // WIDE, PING-PONG
	const bool monoInput     = (mode == 0);              // MONO
	const bool pingPongInput = (mode == 4);              // PING-PONG

	const float kTaperFraction = (1.0f / 16.0f) * smoothMult;

	constexpr float kMinChunkLen = 4.0f;
	const float maxSafe = static_cast<float>(delayBufferLength >> 1);

	for (int i = 0; i < numSamples; ++i)
	{
		// EMA smoothing — always tracks the user's nominal delay T
		revSmoothedDelay   = revSmoothedDelay * smoothCoeff + delaySamples * (1.0f - smoothCoeff);
		if (chaosDelayEnabled_) advanceChaosD();
		if (chaosFilterEnabled_) advanceChaosF();
		smoothedInputGain  = smoothedInputGain  * kGainSmoothCoeff + inputGain  * (1.0f - kGainSmoothCoeff);
		smoothedOutputGain = smoothedOutputGain * kGainSmoothCoeff + outputGain * (1.0f - kGainSmoothCoeff);
		smoothedMix        = smoothedMix        * kGainSmoothCoeff + mix        * (1.0f - kGainSmoothCoeff);
		smoothedFeedback_  = smoothedFeedback_  * kGainSmoothCoeff + feedback   * (1.0f - kGainSmoothCoeff);

		// ── Chunk init / boundary ──
		if (reverseNeedsInit)
		{
			reverseAnchor    = writePos;
			reverseCounter   = 0.0f;
			const float candidateLen = juce::jmax (kMinChunkLen, revSmoothedDelay);
			reverseChunkLen  = juce::jmin (candidateLen, maxSafe);
			reverseNeedsInit = false;
		}

		const float chunkLen = reverseChunkLen;

		const float inputL = channelL != nullptr ? channelL[i] : 0.0f;
		const float inputR = channelR != nullptr ? channelR[i] : 0.0f;
		const float duckGain = advanceDuck (inputL, inputR);

		// ════════════════════════════════════════════════════════════
		// FEEDBACK PATH: forward read (style-aware per-channel delays)
		// ════════════════════════════════════════════════════════════

		// L channel forward read (at style-specific delay)
		float fwdReadPosL = (float) writePos - revSmoothedDelay * fbkRatioL;
		if (fwdReadPosL < 0.0f) fwdReadPosL += (float) delayBufferLength;

		const int fLIdx0  = static_cast<int>(fwdReadPosL) & wrapMask;
		const int fLIdxM1 = (fLIdx0 + wrapMask) & wrapMask;
		const int fLIdx1  = (fLIdx0 + 1) & wrapMask;
		const int fLIdx2  = (fLIdx0 + 2) & wrapMask;
		const float fLFrac = fwdReadPosL - static_cast<float>(static_cast<int>(fwdReadPosL));

		const float fwdDelayedL = hermite4pt (delayL[fLIdxM1], delayL[fLIdx0], delayL[fLIdx1], delayL[fLIdx2], fLFrac);

		// R channel forward read (at style-specific delay)
		float fwdReadPosR = (float) writePos - revSmoothedDelay * fbkRatioR;
		if (fwdReadPosR < 0.0f) fwdReadPosR += (float) delayBufferLength;

		const int fRIdx0  = static_cast<int>(fwdReadPosR) & wrapMask;
		const int fRIdxM1 = (fRIdx0 + wrapMask) & wrapMask;
		const int fRIdx1  = (fRIdx0 + 1) & wrapMask;
		const int fRIdx2  = (fRIdx0 + 2) & wrapMask;
		const float fRFrac = fwdReadPosR - static_cast<float>(static_cast<int>(fwdReadPosR));

		const float fwdDelayedR = hermite4pt (delayR[fRIdxM1], delayR[fRIdx0], delayR[fRIdx1], delayR[fRIdx2], fRFrac);

		// Feedback routing (mirrors forward-mode functions)
		float fbkL, fbkR;
		if (crossFeedback)
		{
			fbkL = fwdDelayedR * smoothedFeedback_;
			fbkR = fwdDelayedL * smoothedFeedback_;
		}
		else
		{
			fbkL = fwdDelayedL * smoothedFeedback_;
			fbkR = fwdDelayedR * smoothedFeedback_;
		}

		fbkL = dcBlockTick (fbkL, fbkDcStateInL, fbkDcStateOutL, fbkDcCoeff);
		fbkR = dcBlockTick (fbkR, fbkDcStateInR, fbkDcStateOutR, fbkDcCoeff);

		applyEngineToFeedback (fbkL, fbkR);
		fbkL *= duckGain;
		fbkR *= duckGain;

		// Write to buffer (style-aware input routing)
		if (monoInput)
		{
			const float monoIn = (inputL + inputR) * 0.5f * smoothedInputGain;
			delayL[writePos] = monoIn + fbkL;
			delayR[writePos] = monoIn + fbkR;
		}
		else if (pingPongInput)
		{
			const float monoIn = (inputL + inputR) * 0.5f * smoothedInputGain;
			delayL[writePos] = monoIn + fbkL;
			delayR[writePos] = fbkR;
		}
		else
		{
			delayL[writePos] = inputL * smoothedInputGain + fbkL;
			delayR[writePos] = inputR * smoothedInputGain + fbkR;
		}

		// ════════════════════════════════════════════════════════════
		// OUTPUT PATH: backward read (shared chunk, proportional taper)
		// ════════════════════════════════════════════════════════════

		float revReadPos = (float) reverseAnchor - reverseCounter;
		if (revReadPos < 0.0f) revReadPos += (float) delayBufferLength;

		const int rIdx0  = static_cast<int>(revReadPos) & wrapMask;
		const int rIdxM1 = (rIdx0 + wrapMask) & wrapMask;
		const int rIdx1  = (rIdx0 + 1) & wrapMask;
		const int rIdx2  = (rIdx0 + 2) & wrapMask;
		const float rFrac = revReadPos - static_cast<float>(static_cast<int>(revReadPos));

		const float rawRevL = hermite4pt (delayL[rIdxM1], delayL[rIdx0], delayL[rIdx1], delayL[rIdx2], rFrac);
		const float rawRevR = hermite4pt (delayR[rIdxM1], delayR[rIdx0], delayR[rIdx1], delayR[rIdx2], rFrac);

		// Proportional taper at chunk edges
		const float pos = reverseCounter;
		const float remaining = chunkLen - pos;
		const int outTaperLen = juce::jlimit (1, static_cast<int>(chunkLen * 0.5f),
		                                      static_cast<int>(chunkLen * kTaperFraction));
		float outTaper = 1.0f;
		{
			const float taperF = static_cast<float>(outTaperLen);
			if (pos < taperF)
				outTaper = taperWeight (pos, taperF);
			else if (remaining < taperF)
				outTaper = taperWeight (remaining, taperF);
		}

		const float outRevL = rawRevL * outTaper;
		const float outRevR = rawRevR * outTaper;

		float wetL = outRevL, wetR = outRevR;
		filterWetSample (wetL, wetR);
		if (chaosDelayEnabled_) applyChaosDelay (wetL, wetR);

		if (monoInput)
		{
			if (channelL != nullptr) channelL[i] = inputL * (1.0f - smoothedMix) + wetL * smoothedMix * smoothedOutputGain * duckGain;
			if (channelR != nullptr) channelR[i] = inputR * (1.0f - smoothedMix) + wetR * smoothedMix * smoothedOutputGain * duckGain;
		}
		else
		{
			if (channelL != nullptr) channelL[i] = inputL * (1.0f - smoothedMix) + wetL * smoothedMix * smoothedOutputGain * duckGain;
			if (channelR != nullptr) channelR[i] = inputR * (1.0f - smoothedMix) + wetR * smoothedMix * smoothedOutputGain * duckGain;
		}

		// Advance write position
		writePos = (writePos + 1) & wrapMask;

		// Advance reverse counter
		reverseCounter += 1.0f;
		if (reverseCounter >= chunkLen)
		{
			reverseCounter  = 0.0f;
			reverseAnchor   = writePos;
			const float newLen = juce::jmax (kMinChunkLen, revSmoothedDelay);
			reverseChunkLen = juce::jmin (newLen, maxSafe);
		}
	}

	delayBufferWritePos = writePos;
}

void ECHOTRAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
	juce::ScopedNoDenormals noDenormals;
	PERF_BLOCK_BEGIN();

	const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
	const int numSamples = buffer.getNumSamples();
	
	// ── MIDI note tracking (skip iteration entirely when MIDI disabled) ──
	const bool midiEnabled = loadBoolParamOrDefault (midiParam, false);
	
	if (midiEnabled && ! midiMessages.isEmpty())
	{
		const int selectedMidiChannel = midiChannel.load (std::memory_order_relaxed);
		for (const auto metadata : midiMessages)
		{
			const auto msg = metadata.getMessage();
			
			if (selectedMidiChannel > 0 && msg.getChannel() != selectedMidiChannel)
				continue;
			
			if (msg.isNoteOn())
			{
				const int noteNumber = msg.getNoteNumber();
				lastMidiNote.store (noteNumber, std::memory_order_relaxed);
				lastMidiVelocity.store (msg.getVelocity(), std::memory_order_relaxed);
				// MIDI note → frequency via std::exp2 (avoids std::pow per noteOn)
				const float frequency = 440.0f * std::exp2 ((noteNumber - 69) * (1.0f / 12.0f));
				currentMidiFrequency.store (frequency, std::memory_order_relaxed);
			}
			else if (msg.isNoteOff())
			{
				if (msg.getNoteNumber() == lastMidiNote.load (std::memory_order_relaxed))
				{
					lastMidiNote.store (-1, std::memory_order_relaxed);
					currentMidiFrequency.store (0.0f, std::memory_order_relaxed);
					
					const float fallbackMs = loadAtomicOrDefault (timeMsParam, kTimeMsDefault);
					const float fallbackSamples = (float) currentSampleRate * (fallbackMs / 1000.0f);
					smoothedDelaySamples = juce::jlimit (0.0f, (float) (delayBufferLength - 2), fallbackSamples);
				}
			}
		}
	}
	else if (! midiEnabled)
	{
		// Only reset if MIDI was just disabled (avoids 2 atomic stores per block)
		if (lastMidiNote.load (std::memory_order_relaxed) >= 0)
		{
			lastMidiNote.store (-1, std::memory_order_relaxed);
			currentMidiFrequency.store (0.0f, std::memory_order_relaxed);
		}
	}

	for (int i = numChannels; i < buffer.getNumChannels(); ++i)
		buffer.clear (i, 0, numSamples);

	if (delayBufferLength == 0 || currentSampleRate <= 0.0)
	{
		PERF_BLOCK_END(perfTrace, numSamples, currentSampleRate);
		return;
	}

	// ── Read parameters (grouped loads) ──
	const bool reverseEnabled = loadBoolParamOrDefault (reverseParam, false);
	const bool syncEnabled    = loadBoolParamOrDefault (syncParam, false);
	const int  midiNote       = lastMidiNote.load (std::memory_order_relaxed);
	const bool midiNoteActive = midiEnabled && (midiNote >= 0);
	const float timeMsValue   = loadAtomicOrDefault (timeMsParam, kTimeMsDefault);
	const int   mode          = loadIntParamOrDefault (modeParam, 1);
	
	float targetDelayMs = timeMsValue;
	
	// Priority: MIDI note > Sync > Manual time
	if (midiNoteActive)
	{
		const float frequency = currentMidiFrequency.load (std::memory_order_relaxed);
		if (frequency > 0.1f)
			targetDelayMs = 1000.0f / frequency;
	}
	else if (syncEnabled)
	{
		// getPlayHead() only called when sync is actually enabled
		const int timeSyncValue = loadIntParamOrDefault (timeSyncParam, kTimeSyncDefault);
		double bpm = 120.0;
		auto posInfo = getPlayHead();
		if (posInfo != nullptr)
		{
			auto pos = posInfo->getPosition();
			if (pos.hasValue() && pos->getBpm().hasValue())
				bpm = *pos->getBpm();
		}
		targetDelayMs = tempoSyncToMs (timeSyncValue, bpm);
	}
	
	const float maxAllowedDelayMs = syncEnabled ? kTimeMsMaxSync : kTimeMsMax;
	targetDelayMs = juce::jlimit (0.0f, maxAllowedDelayMs, targetDelayMs);
	
	float targetFeedback    = loadAtomicOrDefault (feedbackParam, kFeedbackDefault);
	const float inputGainDb = loadAtomicOrDefault (inputParam, kInputDefault);
	const float outputGainDb= loadAtomicOrDefault (outputParam, kOutputDefault);
	const float mixValue    = loadAtomicOrDefault (mixParam, kMixDefault);
	const float modValue    = loadAtomicOrDefault (modParam, kModDefault);
	duckAmount_             = loadAtomicOrDefault (duckParam, kDuckDefault) * 0.01f;  // 0-100 → 0-1
	
	// Fast dB→linear (std::exp2 instead of std::pow via Decibels::decibelsToGain)
	const float inputGain  = fastDecibelsToGain (inputGainDb);
	const float outputGain = fastDecibelsToGain (outputGainDb);
	targetFeedback = juce::jlimit (kFeedbackMin, kFeedbackMax, targetFeedback);

	// Smoothstep mapping on |feedback|, sign preserved
	// Positive = additive comb (all harmonics, sawtooth), negative = subtractive comb (odd harmonics, square)
	{
		const float sign = targetFeedback < 0.0f ? -1.0f : 1.0f;
		const float af   = std::abs (targetFeedback);
		targetFeedback   = sign * af * af * (3.0f - 2.0f * af);
	}

	// MOD frequency multiplier (hyperbolic below centre, linear above)
	float freqMultiplier;
	if (modValue < 0.5f)
		freqMultiplier = 1.0f / (4.0f - 6.0f * modValue);
	else
		freqMultiplier = 1.0f + ((modValue - 0.5f) * 6.0f);
	
	float delaySamples = juce::jmax (0.0f, (float) currentSampleRate * (targetDelayMs / 1000.0f));
	delaySamples /= freqMultiplier;
	delaySamples = juce::jlimit (0.0f, (float) (delayBufferLength - 2), delaySamples);

	// Auto feedback: envelope resets to 0 on note/time/MOD change, then ramps
	// back to 1.0.  Feedback is multiplied by the envelope so the delay
	// "clears" between changes with a smooth fade-in of the feedback tail.
	// Detection runs on the FINAL delaySamples (post-MOD) so MOD tweaks also
	// trigger the reset.
	const bool autoFbkEnabled = loadBoolParamOrDefault (autoFbkParam, false);

	// When ENABLING auto-feedback: launch the envelope (reset to 0) so
	// it fades in — same effect as hitting a new MIDI note.  Do NOT
	// clear the delay buffer so existing feedback tail is preserved.
	// When DISABLING: do nothing (tail continues naturally).
	if (autoFbkEnabled != prevAutoFbkEnabled)
	{
		prevAutoFbkEnabled = autoFbkEnabled;
		if (autoFbkEnabled)
		{
			autoFbkEnvelope = 0.0f;
			autoFbkLastDelaySamples = -1.0f;
		}
	}

	if (autoFbkEnabled && targetFeedback > 0.001f)
	{
		// ── Detection: reset envelope when delay changes significantly ──
		// Threshold 2 % — industry standard tolerance for pitch-change
		// detection (cf. Melodyne/Auto-Tune use 1-5 %).  Prevents
		// spurious triggers from EMA micro-fluctuations.
		constexpr float kChangeThreshold = 0.02f;

		// Cooldown: minimum samples between resets.  Prevents LFO
		// automation from permanently suppressing feedback.  ~43 ms @ 48 kHz.
		constexpr int kCooldownSamples = 2048;

		const float delayDelta = std::abs (delaySamples - autoFbkLastDelaySamples);
		const bool delayChanged = (autoFbkLastDelaySamples < 0.0f)
		                        ? false  // first call after init → don't reset
		                        : (delayDelta > autoFbkLastDelaySamples * kChangeThreshold);

		autoFbkCooldownLeft = juce::jmax (0, autoFbkCooldownLeft - numSamples);

		if (delayChanged && autoFbkCooldownLeft <= 0)
		{
			autoFbkEnvelope     = 0.0f;
			autoFbkCooldownLeft = kCooldownSamples;
		}

		autoFbkLastDelaySamples = delaySamples;

		// ── Envelope ramp 0 → 1 ──
		// TAU (0-100 %): recovery speed.  0 % = fast (30 ms), 100 % = slow (3 s).
		// ATT (0-100 %): modulation depth only.  0 % = bypass, 100 % = full.
		//
		// Pitch-scaling is automatic and fixed: shorter delays recover
		// faster via sqrt(delayMs / kRefMs).  This is internal — the user
		// doesn't control it and ATT only sets depth.
		// Reference: FabFilter Timeless 3, Soundtoys EchoBoy Swell.
		constexpr float kTauFloor = 0.030f;     // 30 ms — fast but audible
		constexpr float kTauCeil  = 3.000f;     // 3 s  — dramatic swell
		constexpr float kRefMs    = 1000.0f;    // delay at which pitch-scaling = 1.0

		const float tauPct = loadAtomicOrDefault (autoFbkTauParam, kAutoFbkTauDefault) * 0.01f;
		const float attPct = loadAtomicOrDefault (autoFbkAttParam, kAutoFbkAttDefault) * 0.01f;

		// Base tau from TAU slider (30 ms – 3 s)
		const float tauBase = kTauFloor + (kTauCeil - kTauFloor) * tauPct;

		// Automatic pitch-scaling: sqrt(delay/ref).  Gentle and fixed.
		//   1000 ms → ×1.00,  500 ms → ×0.71,  100 ms → ×0.32,  50 ms → ×0.22
		const float delayMs = (delaySamples / (float) currentSampleRate) * 1000.0f;
		const float pitchScale = std::sqrt (juce::jlimit (0.001f, 1.0f, delayMs / kRefMs));
		const float tau = kTauFloor + (tauBase - kTauFloor) * pitchScale;

		const float envCoeff = std::exp (-1.0f / ((float) currentSampleRate * tau));
		for (int i = 0; i < numSamples; ++i)
			autoFbkEnvelope = envCoeff * autoFbkEnvelope + (1.0f - envCoeff) * 1.0f;

		autoFbkEnvelope = juce::jlimit (0.0f, 1.0f, autoFbkEnvelope);

		// ATT = modulation depth only (cubic curve for gradual onset).
		// UI 100 % → internal 75 % to keep the full slider range useful.
		//   att 25 % → scaled 18.75 % → attCurved ≈ 0.007 (subtle)
		//   att 50 % → scaled 37.5 %  → attCurved ≈ 0.053 (moderate)
		//   att 75 % → scaled 56.25 % → attCurved ≈ 0.178 (clear)
		//   att 100 % → scaled 75 %   → attCurved ≈ 0.422 (dramatic max)
		const float attScaled = attPct * 0.75f;
		const float attCurved = attScaled * attScaled * attScaled;
		const float envMix = 1.0f - attCurved * (1.0f - autoFbkEnvelope);
		targetFeedback *= envMix;
	}
	else
	{
		// When auto-feedback is off, keep tracking delay so enabling it
		// mid-session doesn't trigger a false reset.
		autoFbkLastDelaySamples = delaySamples;
		autoFbkEnvelope         = 1.0f;
		autoFbkCooldownLeft     = 0;
	}

	// Bypass if delay < 1 sample
	if (delaySamples < 1.0f)
	{
		for (int ch = 0; ch < numChannels; ++ch)
		{
			auto* channelData = buffer.getWritePointer (ch);
			for (int i = 0; i < numSamples; ++i)
			{
				smoothedOutputGain = smoothedOutputGain * kGainSmoothCoeff + outputGain * (1.0f - kGainSmoothCoeff);
				channelData[i] *= smoothedOutputGain;
			}
		}
		smoothedDelaySamples = 0.0f;
		smoothedDelaySamplesR = 0.0f;
		PERF_BLOCK_END(perfTrace, numSamples, currentSampleRate);
		return;
	}
	
	// Delay smoothing coefficients (cached; only recompute for MIDI glide).
	// Direct and reverse use DIFFERENT velocity→tau curves because their
	// architectures make glide perceptible in very different ways:
	//   • Direct: continuous per-sample EMA → glide is very audible.
	//   • Reverse: chunk-quantised → glide only manifests at chunk edges.
	// Both share kTauMax/kTauMin and velocity mapping, but exponents differ
	// so the PERCEIVED glide is identical across modes.
	float delaySmoothCoeff    = cachedDelaySmoothCoeff;
	float revDelaySmoothCoeff = cachedDelaySmoothCoeff;
	if (midiNoteActive)
	{
		const float vel  = (float) lastMidiVelocity.load (std::memory_order_relaxed);
		const float tLin = juce::jlimit (0.0f, 1.0f, (vel - 1.0f) / 126.0f);

		constexpr float kTauMax = 0.200f;   // 200 ms — full portamento at pianissimo
		constexpr float kTauMin = 0.0002f;  // 0.2 ms — imperceptible at max velocity

		// ── Direct mode: very steep curve (exponent 0.05) ──
		// Continuous EMA makes glide highly perceptible, so the curve must
		// compress most of the velocity range into nearly-instant territory.
		//   vel 127 → tau ≈ 0.2 ms  (instant)
		//   vel 100 → tau ≈ 2.6 ms  (instant)
		//   vel  60 → tau ≈ 7.5 ms  (subtle)
		//   vel  30 → tau ≈ 14 ms   (gentle glide)
		//   vel   1 → tau ≈ 200 ms  (full portamento)
		{
			const float t   = std::pow (tLin, 0.05f);
			const float tau = kTauMax - t * (kTauMax - kTauMin);
			delaySmoothCoeff = std::exp (-1.0f / ((float) currentSampleRate * tau));
		}

		// ── Reverse mode: gentler curve (exponent 0.12) ──
		// Chunk-based playback naturally quantises glide to chunk boundaries,
		// reducing perceived pitch-slide.  A wider tau range compensates so
		// the musical result feels identical to direct mode.
		//   vel 127 → tau ≈ 0.2 ms  (instant)
		//   vel 100 → tau ≈ 6 ms    (barely noticeable — hidden by chunks)
		//   vel  60 → tau ≈ 18 ms   (subtle)
		//   vel  30 → tau ≈ 40 ms   (gentle glide)
		//   vel   1 → tau ≈ 200 ms  (full portamento)
		{
			const float t   = std::pow (tLin, 0.12f);
			const float tau = kTauMax - t * (kTauMax - kTauMin);
			revDelaySmoothCoeff = std::exp (-1.0f / ((float) currentSampleRate * tau));
		}
	}
	
	// Snap gain/mix smoothers to target when close enough (avoids useless EMA in steady state)
	constexpr float kSnapEpsilon = 1e-5f;
	if (std::abs (smoothedInputGain  - inputGain)  < kSnapEpsilon) smoothedInputGain  = inputGain;
	if (std::abs (smoothedOutputGain - outputGain) < kSnapEpsilon) smoothedOutputGain = outputGain;
	if (std::abs (smoothedMix        - mixValue)   < kSnapEpsilon) smoothedMix        = mixValue;

	// ── Feedback loop coefficients (used by all delay modes) ──
	// DC blocker only — maximally transparent path.
	{
		const float sr = (float) currentSampleRate;
		fbkDcCoeff  = 1.0f - (6.2831853f * kFbkDcBlockHz / sr);  // ≈0.9993 @ 48kHz
	}

	// ── Flush denormals in feedback-path filter states (per-block, near-zero cost) ──
	{
		constexpr float kDnr = 1e-20f;
		if (std::abs (fbkDcStateInL)   < kDnr) fbkDcStateInL   = 0.0f;
		if (std::abs (fbkDcStateOutL)  < kDnr) fbkDcStateOutL  = 0.0f;
		if (std::abs (fbkDcStateInR)   < kDnr) fbkDcStateInR   = 0.0f;
		if (std::abs (fbkDcStateOutR)  < kDnr) fbkDcStateOutR  = 0.0f;
		if (std::abs (engineLp1StateL_) < kDnr) engineLp1StateL_ = 0.0f;
		if (std::abs (engineLp1StateR_) < kDnr) engineLp1StateR_ = 0.0f;
		if (std::abs (engineLp2StateL_) < kDnr) engineLp2StateL_ = 0.0f;
		if (std::abs (engineLp2StateR_) < kDnr) engineLp2StateR_ = 0.0f;
		if (std::abs (tiltState_[0])   < kDnr) tiltState_[0]   = 0.0f;
		if (std::abs (tiltState_[1])   < kDnr) tiltState_[1]   = 0.0f;
		if (std::abs (chaosDSmoothed_) < kDnr) chaosDSmoothed_ = 0.0f;
		if (std::abs (chaosGSmoothed_) < kDnr) chaosGSmoothed_ = 0.0f;
		if (std::abs (chaosFSmoothed_) < kDnr) chaosFSmoothed_ = 0.0f;
	}

	// ── Load wet-signal filter targets for process*Delay functions ──
	wetFilterHpOn_ = loadBoolParamOrDefault (filterHpOnParam, false);
	wetFilterLpOn_ = loadBoolParamOrDefault (filterLpOnParam, false);
	wetFilterTargetHpFreq_ = loadAtomicOrDefault (filterHpFreqParam, kFilterHpFreqDefault);
	wetFilterTargetLpFreq_ = loadAtomicOrDefault (filterLpFreqParam, kFilterLpFreqDefault);
	{
		const int hpSlope = juce::roundToInt (loadAtomicOrDefault (filterHpSlopeParam, (float) kFilterSlopeDefault));
		const int lpSlope = juce::roundToInt (loadAtomicOrDefault (filterLpSlopeParam, (float) kFilterSlopeDefault));
		wetFilterNumSectionsHp_ = (hpSlope == 0) ? 1 : (hpSlope == 1) ? 1 : 2;
		wetFilterNumSectionsLp_ = (lpSlope == 0) ? 1 : (lpSlope == 1) ? 1 : 2;
	}

	// ── Load tilt parameter ──
	tiltDb_ = loadAtomicOrDefault (tiltParam, kTiltDefault);

	// ── Load chaos parameters and precompute per-block constants ──
	chaosFilterEnabled_ = loadBoolParamOrDefault (chaosParam, false);
	chaosDelayEnabled_  = loadBoolParamOrDefault (chaosDelayParam, false);
	const bool anyChaos = chaosFilterEnabled_ || chaosDelayEnabled_;
	if (anyChaos)
	{
		if (chaosDelayEnabled_)
		{
			const float rawAmtD = loadAtomicOrDefault (chaosAmtParam, kChaosAmtDefault);
			const float rawSpdD = loadAtomicOrDefault (chaosSpdParam, kChaosSpdDefault);
			chaosAmtD_       = rawAmtD;
			chaosShPeriodD_  = (float) currentSampleRate / rawSpdD;
			const float amtNormD = rawAmtD * 0.01f;
			chaosDelayMaxSamples_ = amtNormD * 0.005f * (float) currentSampleRate;  // ±5ms at 100%
			chaosGainMaxDb_       = amtNormD * 1.0f;                                // ±1dB at 100%
			constexpr float kChaosDTau = 0.030f;
			chaosDSmoothCoeff_ = std::exp (-1.0f / ((float) currentSampleRate * kChaosDTau));
			constexpr float kChaosGTau = 0.015f;
			chaosGSmoothCoeff_ = std::exp (-1.0f / ((float) currentSampleRate * kChaosGTau));
		}
		else
		{
			chaosDelayMaxSamples_ = 0.0f;
			chaosGainMaxDb_ = 0.0f;
		}

		if (chaosFilterEnabled_)
		{
			const float rawAmtF = loadAtomicOrDefault (chaosAmtFilterParam, kChaosAmtDefault);
			const float rawSpdF = loadAtomicOrDefault (chaosSpdFilterParam, kChaosSpdDefault);
			chaosAmtF_       = rawAmtF;
			chaosShPeriodF_  = (float) currentSampleRate / rawSpdF;
			const float amtNormF = rawAmtF * 0.01f;
			chaosFilterMaxOct_ = amtNormF * 2.0f;  // ±2 oct at 100%
			constexpr float kChaosFTau = 0.060f;
			chaosFSmoothCoeff_ = std::exp (-1.0f / ((float) currentSampleRate * kChaosFTau));
		}
		else
		{
			chaosFilterMaxOct_ = 0.0f;
		}

		constexpr float kChaosParamTau = 0.010f;
		chaosParamSmoothCoeff_ = std::exp (-1.0f / ((float) currentSampleRate * kChaosParamTau));
	}
	else
	{
		chaosAmtD_ = 0.0f; chaosAmtF_ = 0.0f;
		chaosDelayMaxSamples_ = 0.0f;
		chaosGainMaxDb_ = 0.0f;
		chaosFilterMaxOct_ = 0.0f;
	}

	// ── Load engine mode and precompute feedback-loop LP coefficient ──
	engineMode_ = loadIntParamOrDefault (engineParam, kEngineDefault);
	if (engineMode_ != prevEngineMode_)
	{
		// Reset LP filter state on mode change to prevent transient bursts
		engineLp1StateL_ = 0.0f; engineLp1StateR_ = 0.0f;
		engineLp2StateL_ = 0.0f; engineLp2StateR_ = 0.0f;
		prevEngineMode_ = engineMode_;
	}
	if (engineMode_ > 0)
	{
		// ANALOG: 5 kHz (tape head rolloff), BBD: 3 kHz (anti-alias before S&H)
		const float cutoff = (engineMode_ == 1) ? 5000.0f : 3000.0f;
		engineLpCoeff_ = 1.0f - std::exp (-6.2831853f * cutoff / (float) currentSampleRate);
		if (engineMode_ == 1)
		{
			// ANALOG wow: sine LFO at ~2 Hz
			engineDriftPeriod_ = (float) currentSampleRate / 2.0f;
			// Not used for sine mode, but keep smooth for safety
			engineDriftSmoothCoeff_ = 0.0f;
		}
		else
		{
			// BBD clock jitter: S&H at ~15 Hz
			engineDriftPeriod_ = (float) currentSampleRate / 15.0f;
			// EMA τ ≈ 15ms — fast enough for jitter, smooth enough to avoid clicks
			engineDriftSmoothCoeff_ = std::exp (-1.0f / ((float) currentSampleRate * 0.015f));
		}
	}
	
	// ── Mode In / Mode Out / Sum Bus ──
	const int modeInVal  = loadIntParamOrDefault (modeInParam,  kModeInOutDefault);
	const int modeOutVal = loadIntParamOrDefault (modeOutParam, kModeInOutDefault);
	const int sumBusVal  = loadIntParamOrDefault (sumBusParam,  kSumBusDefault);

	// Save original dry signal for Sum Bus reconstruction
	juce::AudioBuffer<float> dryBuffer;
	if (sumBusVal != 0 && numChannels >= 2)
	{
		dryBuffer.makeCopyOf (buffer);
	}

	// Mode In: M/S encode input buffer before delay processing
	if (numChannels >= 2 && modeInVal != 0)
	{
		auto* chL = buffer.getWritePointer (0);
		auto* chR = buffer.getWritePointer (1);
		for (int i = 0; i < numSamples; ++i)
		{
			const float l = chL[i], r = chR[i];
			if (modeInVal == 1)      { const float mid = (l + r) * kSqrt2Over2; chL[i] = chR[i] = mid; }
			else /* modeInVal==2 */   { const float side = (l - r) * kSqrt2Over2; chL[i] = chR[i] = side; }
		}
	}

	// Reverse mode: chunk-based backward playback (works with any mode routing)
	if (reverseEnabled)
	{
		const float smoothVal  = loadAtomicOrDefault (reverseSmoothParam, kReverseSmoothDefault);
		const float smoothMult = std::exp2 (smoothVal);
		processReverseDelay (buffer, numSamples, numChannels, delaySamples,
		                     targetFeedback, inputGain, outputGain, mixValue, revDelaySmoothCoeff,
		                     smoothMult, mode);
	}
	else if (mode == 0)
	{
		processMonoDelay (buffer, numSamples, numChannels, delaySamples, 
		                  targetFeedback, inputGain, outputGain, mixValue, delaySmoothCoeff);
	}
	else if (mode == 2) // WIDE
	{
		processWideDelay (buffer, numSamples, numChannels, delaySamples,
		                  targetFeedback, inputGain, outputGain, mixValue, delaySmoothCoeff);
	}
	else if (mode == 3) // DUAL
	{
		processDualDelay (buffer, numSamples, numChannels, delaySamples,
		                  targetFeedback, inputGain, outputGain, mixValue, delaySmoothCoeff);
	}
	else if (mode == 4) // PING-PONG
	{
		processPingPongDelay (buffer, numSamples, numChannels, delaySamples,
		                      targetFeedback, inputGain, outputGain, mixValue, delaySmoothCoeff);
	}
	else // STEREO (default)
	{
		processStereoDelay (buffer, numSamples, numChannels, delaySamples,
		                    targetFeedback, inputGain, outputGain, mixValue, delaySmoothCoeff);
	}

	// Mode Out: M/S encode output after delay processing
	if (numChannels >= 2 && modeOutVal != 0)
	{
		auto* chL = buffer.getWritePointer (0);
		auto* chR = buffer.getWritePointer (1);
		for (int i = 0; i < numSamples; ++i)
		{
			const float l = chL[i], r = chR[i];
			if (modeOutVal == 1)      { const float mid = (l + r) * kSqrt2Over2; chL[i] = chR[i] = mid; }
			else /* modeOutVal==2 */   { const float side = (l - r) * kSqrt2Over2; chL[i] = chR[i] = side; }
		}
	}

	// Sum Bus: separate dry/wet, route only wet through M/S bus, preserve dry stereo image
	if (numChannels >= 2 && sumBusVal != 0)
	{
		auto* chL = buffer.getWritePointer (0);
		auto* chR = buffer.getWritePointer (1);
		const auto* dL = dryBuffer.getReadPointer (0);
		const auto* dR = dryBuffer.getReadPointer (1);

		for (int i = 0; i < numSamples; ++i)
		{
			// Recompute Mode-In-encoded dry to accurately extract wet portion
			float encL = dL[i], encR = dR[i];
			if (modeInVal == 1)      { const float m = (encL + encR) * kSqrt2Over2; encL = encR = m; }
			else if (modeInVal == 2) { const float s = (encL - encR) * kSqrt2Over2; encL = encR = s; }

			const float dryContribL = encL * (1.0f - mixValue);
			const float dryContribR = encR * (1.0f - mixValue);
			const float wetL = chL[i] - dryContribL;
			const float wetR = chR[i] - dryContribR;

			// Original dry pass-through (pre-Mode-In, preserves stereo image)
			const float origDryL = dL[i] * (1.0f - mixValue);
			const float origDryR = dR[i] * (1.0f - mixValue);

			if (sumBusVal == 1) // →M: wet collapsed to mono mid
			{
				const float midBus = (wetL + wetR) * 0.5f;
				chL[i] = origDryL + midBus;
				chR[i] = origDryR + midBus;
			}
			else // →S: wet to side
			{
				const float sideBus = (wetL - wetR) * 0.5f;
				chL[i] = origDryL + sideBus;
				chR[i] = origDryR - sideBus;
			}
		}
	}

	// ── Pan (equal-power, stereo only) ──
	if (numChannels >= 2)
	{
		const float pan = panParam->load();
		if (std::abs (pan - lastPan_) > 0.001f)
		{
			lastPan_ = pan;
			const float angle = pan * 1.5707963f; // π/2
			lastPanLeft_  = std::cos (angle);
			lastPanRight_ = std::sin (angle);
		}
		if (std::abs (lastPan_ - 0.5f) > 0.001f)
		{
			juce::FloatVectorOperations::multiply (buffer.getWritePointer (0), lastPanLeft_,  numSamples);
			juce::FloatVectorOperations::multiply (buffer.getWritePointer (1), lastPanRight_, numSamples);
		}
	}

	// Safety hard-limiter: prevent catastrophic output only (NaN/Inf runaway).
	// Set very high (+48 dBFS) so it never engages during normal operation.
	{
		constexpr float kSafetyLimit = 251.19f; // +48 dBFS — only catches runaways
		for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
		{
			auto* data = buffer.getWritePointer (ch);
			juce::FloatVectorOperations::clip (data, data, -kSafetyLimit, kSafetyLimit, numSamples);
		}
	}

	PERF_BLOCK_END(perfTrace, numSamples, currentSampleRate);
}

//==============================================================================
bool ECHOTRAudioProcessor::hasEditor() const
{
	return true;
}

juce::AudioProcessorEditor* ECHOTRAudioProcessor::createEditor()
{
	return new ECHOTRAudioProcessorEditor (*this);
}

//==============================================================================
void ECHOTRAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
	auto state = apvts.copyState();
	std::unique_ptr<juce::XmlElement> xml (state.createXml());
	copyXmlToBinary (*xml, destData);
}

void ECHOTRAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
	std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

	if (xmlState.get() != nullptr)
	{
		if (xmlState->hasTagName (apvts.state.getType()))
		{
			apvts.replaceState (juce::ValueTree::fromXml (*xmlState));

			// Sync midiChannel atomic from restored state so processBlock sees the correct value
			const auto restoredChannel = apvts.state.getProperty (UiStateKeys::midiPort);
			if (! restoredChannel.isVoid())
				midiChannel.store ((int) restoredChannel, std::memory_order_relaxed);
		}
	}
}

void ECHOTRAudioProcessor::getCurrentProgramStateInformation (juce::MemoryBlock& destData)
{
	getStateInformation (destData);
}

void ECHOTRAudioProcessor::setCurrentProgramStateInformation (const void* data, int sizeInBytes)
{
	setStateInformation (data, sizeInBytes);
}

//==============================================================================
// Tempo sync divisions: each group = triplet, normal, dotted
juce::StringArray ECHOTRAudioProcessor::getTimeSyncChoices()
{
	return {
		"1/64T", "1/64", "1/64.",
		"1/32T", "1/32", "1/32.",
		"1/16T", "1/16", "1/16.",
		"1/8T",  "1/8",  "1/8.",
		"1/4T",  "1/4",  "1/4.",
		"1/2T",  "1/2",  "1/2.",
		"1/1T",  "1/1",  "1/1.",
		"2/1T",  "2/1",  "2/1.",
		"4/1T",  "4/1",  "4/1.",
		"8/1T",  "8/1",  "8/1."
	};
}

juce::String ECHOTRAudioProcessor::getTimeSyncName (int index)
{
	auto choices = getTimeSyncChoices();
	if (index >= 0 && index < choices.size())
		return choices[index];
	return "1/8";
}

//==============================================================================
float ECHOTRAudioProcessor::tempoSyncToMs (int syncIndex, double bpm) const
{
	if (bpm <= 0.0)
		bpm = 120.0;
	
	syncIndex = juce::jlimit (0, 29, syncIndex);
	
	const float divisions[] = { 64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f };
	const int baseIndex = syncIndex / 3;
	const int modifier = syncIndex % 3; // 0=triplet, 1=normal, 2=dotted
	
	const float quarterNoteMs = (float) (60000.0 / bpm);
	float durationMs = quarterNoteMs * (4.0f / divisions[baseIndex]);
	
	if (modifier == 0)
		durationMs *= (2.0f / 3.0f);
	else if (modifier == 2)
		durationMs *= 1.5f;
	
	return durationMs;
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ECHOTRAudioProcessor::createParameterLayout()
{
	std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamTimeMs, "Time",
		juce::NormalisableRange<float> (kTimeMsMin, kTimeMsMax, 0.0f, 0.35f), kTimeMsDefault));

	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamTimeSync, "Time Sync", getTimeSyncChoices(), kTimeSyncDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFeedback, "Feedback",
		juce::NormalisableRange<float> (kFeedbackMin, kFeedbackMax, 0.0f, 1.0f), kFeedbackDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamMode, "Style",
		juce::NormalisableRange<float> ((float)kModeMin, (float)kModeMax, 1.0f, 1.0f), kModeDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamMod, "Mod",
		juce::NormalisableRange<float> (kModMin, kModMax, 0.0f, 1.0f), kModDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamInput, "Input",
		juce::NormalisableRange<float> (kInputMin, kInputMax, 0.0f, 2.5f), kInputDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamOutput, "Output",
		juce::NormalisableRange<float> (kOutputMin, kOutputMax, 0.0f, 3.23f), kOutputDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamMix, "Mix",
		juce::NormalisableRange<float> (kMixMin, kMixMax, 0.0f, 1.0f), kMixDefault));

	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamSync, "Sync", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamMidi, "MIDI", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamAutoFbk, "Env Fbk", false));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamAutoFbkTau, "Env Fbk Tau",
		juce::NormalisableRange<float> (kAutoFbkTauMin, kAutoFbkTauMax, 0.01f, 1.0f), kAutoFbkTauDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamAutoFbkAtt, "Env Fbk Att",
		juce::NormalisableRange<float> (kAutoFbkAttMin, kAutoFbkAttMax, 0.01f, 1.0f), kAutoFbkAttDefault));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamReverse, "Reverse", false));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamReverseSmooth, "Reverse Smooth",
		juce::NormalisableRange<float> (kReverseSmoothMin, kReverseSmoothMax, 0.01f, 1.0f), kReverseSmoothDefault));

	// HP/LP wet-signal filter
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFilterHpFreq, "Filter HP Freq",
		juce::NormalisableRange<float> (kFilterFreqMin, kFilterFreqMax, 0.01f, 0.35f), kFilterHpFreqDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFilterLpFreq, "Filter LP Freq",
		juce::NormalisableRange<float> (kFilterFreqMin, kFilterFreqMax, 0.01f, 0.35f), kFilterLpFreqDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFilterHpSlope, "Filter HP Slope",
		juce::NormalisableRange<float> ((float) kFilterSlopeMin, (float) kFilterSlopeMax, 1.0f), (float) kFilterSlopeDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFilterLpSlope, "Filter LP Slope",
		juce::NormalisableRange<float> ((float) kFilterSlopeMin, (float) kFilterSlopeMax, 1.0f), (float) kFilterSlopeDefault));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamFilterHpOn, "Filter HP On", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamFilterLpOn, "Filter LP On", false));

	// Tilt EQ
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamTilt, "Tilt",
		juce::NormalisableRange<float> (kTiltMin, kTiltMax, 0.01f), kTiltDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamPan, "Pan",
		juce::NormalisableRange<float> (kPanMin, kPanMax, 0.01f), kPanDefault));

	// Chaos
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamChaos, "Chaos Filter", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamChaosD, "Chaos Delay", false));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamChaosAmt, "Chaos Amount",
		juce::NormalisableRange<float> (kChaosAmtMin, kChaosAmtMax, 0.1f), kChaosAmtDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamChaosSpd, "Chaos Speed",
		juce::NormalisableRange<float> (kChaosSpdMin, kChaosSpdMax, 0.01f, 0.3f), kChaosSpdDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamChaosAmtFilter, "Chaos Filter Amount",
		juce::NormalisableRange<float> (kChaosAmtMin, kChaosAmtMax, 0.1f), kChaosAmtDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamChaosSpdFilter, "Chaos Filter Speed",
		juce::NormalisableRange<float> (kChaosSpdMin, kChaosSpdMax, 0.01f, 0.3f), kChaosSpdDefault));

	// Engine (feedback-loop character: CLEAN / ANALOG / BBD)
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamEngine, "Engine",
		juce::NormalisableRange<float> ((float) kEngineMin, (float) kEngineMax, 1.0f, 1.0f), (float) kEngineDefault));

	// Duck (Valhalla-style 1-knob ducking: 0-100%)
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamDuck, "Duck",
		juce::NormalisableRange<float> (kDuckMin, kDuckMax, 1.0f), kDuckDefault));

	// Mode In / Mode Out / Sum Bus
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamModeIn, "Mode In", juce::StringArray { "L+R", "MID", "SIDE" }, kModeInOutDefault));
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamModeOut, "Mode Out", juce::StringArray { "L+R", "MID", "SIDE" }, kModeInOutDefault));
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamSumBus, "Sum Bus", juce::StringArray { "ST", u8"\u2192M", u8"\u2192S" }, kSumBusDefault));

	// UI state (hidden from automation)
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiWidth, "UI Width", 360, 1600, 360));
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiHeight, "UI Height", 240, 1200, 480));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamUiPalette, "UI Palette", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamUiCrt, "UI CRT", false));
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiColor0, "UI Color 0", 0, 0xFFFFFF, 0xFFFFFF));
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiColor1, "UI Color 1", 0, 0xFFFFFF, 0x000000));
	// Legacy params kept for backward compatibility with saved presets
	params.push_back (std::make_unique<juce::AudioParameterInt> ("ui_color2", "UI Color 2", 0, 0xFFFFFF, 0xFFFFFF));
	params.push_back (std::make_unique<juce::AudioParameterInt> ("ui_color3", "UI Color 3", 0, 0xFFFFFF, 0x000000));

	return { params.begin(), params.end() };
}

//==============================================================================
// UI state management
void ECHOTRAudioProcessor::setUiEditorSize (int width, int height)
{
	const int w = juce::jlimit (360, 1600, width);
	const int h = juce::jlimit (240, 1200, height);
	uiEditorWidth.store (w, std::memory_order_relaxed);
	uiEditorHeight.store (h, std::memory_order_relaxed);
	apvts.state.setProperty (UiStateKeys::editorWidth, w, nullptr);
	apvts.state.setProperty (UiStateKeys::editorHeight, h, nullptr);
	setParameterPlainValue (apvts, kParamUiWidth, (float) w);
	setParameterPlainValue (apvts, kParamUiHeight, (float) h);
	updateHostDisplay();
}

int ECHOTRAudioProcessor::getUiEditorWidth() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::editorWidth);
	if (! fromState.isVoid())
		return (int) fromState;
	if (uiWidthParam != nullptr)
		return (int) std::lround (uiWidthParam->load (std::memory_order_relaxed));
	return uiEditorWidth.load (std::memory_order_relaxed);
}

int ECHOTRAudioProcessor::getUiEditorHeight() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::editorHeight);
	if (! fromState.isVoid())
		return (int) fromState;
	if (uiHeightParam != nullptr)
		return (int) std::lround (uiHeightParam->load (std::memory_order_relaxed));
	return uiEditorHeight.load (std::memory_order_relaxed);
}

void ECHOTRAudioProcessor::setUiUseCustomPalette (bool shouldUseCustomPalette)
{
	uiUseCustomPalette.store (shouldUseCustomPalette ? 1 : 0, std::memory_order_relaxed);
	apvts.state.setProperty (UiStateKeys::useCustomPalette, shouldUseCustomPalette, nullptr);
	setParameterPlainValue (apvts, kParamUiPalette, shouldUseCustomPalette ? 1.0f : 0.0f);
	updateHostDisplay();
}

bool ECHOTRAudioProcessor::getUiUseCustomPalette() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::useCustomPalette);
	if (! fromState.isVoid())
		return (bool) fromState;
	if (uiPaletteParam != nullptr)
		return uiPaletteParam->load (std::memory_order_relaxed) > 0.5f;
	return uiUseCustomPalette.load (std::memory_order_relaxed) != 0;
}

void ECHOTRAudioProcessor::setUiCrtEnabled (bool enabled)
{
	uiCrtEnabled.store (enabled ? 1 : 0, std::memory_order_relaxed);
	apvts.state.setProperty (UiStateKeys::crtEnabled, enabled, nullptr);
	setParameterPlainValue (apvts, kParamUiCrt, enabled ? 1.0f : 0.0f);
	updateHostDisplay();
}

bool ECHOTRAudioProcessor::getUiCrtEnabled() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::crtEnabled);
	if (! fromState.isVoid())
		return (bool) fromState;
	if (uiCrtParam != nullptr)
		return uiCrtParam->load (std::memory_order_relaxed) > 0.5f;
	return uiCrtEnabled.load (std::memory_order_relaxed) != 0;
}

void ECHOTRAudioProcessor::setUiIoExpanded (bool expanded)
{
	apvts.state.setProperty (UiStateKeys::ioExpanded, expanded, nullptr);
}

bool ECHOTRAudioProcessor::getUiIoExpanded() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::ioExpanded);
	if (! fromState.isVoid()) return (bool) fromState;
	return false;
}

void ECHOTRAudioProcessor::setMidiChannel (int channel)
{
	const int ch = juce::jlimit (0, 16, channel);
	midiChannel.store (ch, std::memory_order_relaxed);
	apvts.state.setProperty (UiStateKeys::midiPort, ch, nullptr); // key kept for preset compat
}

int ECHOTRAudioProcessor::getMidiChannel() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::midiPort);
	if (! fromState.isVoid())
		return juce::jlimit (0, 16, (int) fromState);
	
	return midiChannel.load (std::memory_order_relaxed);
}

void ECHOTRAudioProcessor::setUiCustomPaletteColour (int index, juce::Colour colour)
{
	if (index >= 0 && index < 2)
	{
		uiCustomPalette[(size_t) index].store (colour.getARGB(), std::memory_order_relaxed);
		const juce::String key = UiStateKeys::customPalette[(size_t) index];
		apvts.state.setProperty (key, (int) colour.getARGB(), nullptr);
		if (uiColorParams[(size_t) index] != nullptr)
			setParameterPlainValue (apvts, (index == 0 ? kParamUiColor0 : kParamUiColor1),
									(float) (int) colour.getARGB());
		updateHostDisplay();
	}
}

juce::Colour ECHOTRAudioProcessor::getUiCustomPaletteColour (int index) const noexcept
{
	if (index < 0 || index >= 2)
		return juce::Colours::white;

	const juce::String key = UiStateKeys::customPalette[(size_t) index];
	const auto fromState = apvts.state.getProperty (key);
	if (! fromState.isVoid())
		return juce::Colour ((juce::uint32) (int) fromState);

	if (uiColorParams[(size_t) index] != nullptr)
	{
		const int rgb = juce::jlimit (0, 0xFFFFFF,
									  (int) std::lround (uiColorParams[(size_t) index]->load (std::memory_order_relaxed)));
		const juce::uint8 r = (juce::uint8) ((rgb >> 16) & 0xFF);
		const juce::uint8 g = (juce::uint8) ((rgb >> 8) & 0xFF);
		const juce::uint8 b = (juce::uint8) (rgb & 0xFF);
		return juce::Colour::fromRGB (r, g, b);
	}

	return juce::Colour (uiCustomPalette[(size_t) index].load (std::memory_order_relaxed));
}

//==============================================================================
// MIDI helpers

juce::String ECHOTRAudioProcessor::getMidiNoteName (int midiNote)
{
	if (midiNote < 0 || midiNote > 127)
		return "";
	
	const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
	const int octave = (midiNote / 12) - 1;
	const int noteIndex = midiNote % 12;
	
	return juce::String (noteNames[noteIndex]) + juce::String (octave);
}

float ECHOTRAudioProcessor::getCurrentDelayMs() const
{
	const bool midiEnabled = loadBoolParamOrDefault (midiParam, false);
	const int midiNote = lastMidiNote.load (std::memory_order_relaxed);
	const bool midiNoteActive = midiEnabled && (midiNote >= 0);
	
	// Priority: MIDI note > Sync > Manual time
	if (midiNoteActive)
	{
		const float frequency = currentMidiFrequency.load (std::memory_order_relaxed);
		if (frequency > 0.1f)
			return 1000.0f / frequency;
	}
	
	const bool syncEnabled = loadBoolParamOrDefault (syncParam, false);
	if (syncEnabled)
	{
		const int timeSyncValue = loadIntParamOrDefault (timeSyncParam, kTimeSyncDefault);
		double bpm = 120.0;
		auto posInfo = getPlayHead();
		if (posInfo != nullptr)
		{
			auto pos = posInfo->getPosition();
			if (pos.hasValue() && pos->getBpm().hasValue())
				bpm = *pos->getBpm();
		}
		return tempoSyncToMs (timeSyncValue, bpm);
	}
	
	return loadAtomicOrDefault (timeMsParam, kTimeMsDefault);
}

juce::String ECHOTRAudioProcessor::getCurrentTimeDisplay() const
{
	const bool midiEnabled = loadBoolParamOrDefault (midiParam, false);
	const int midiNote = lastMidiNote.load (std::memory_order_relaxed);
	const bool midiNoteActive = midiEnabled && (midiNote >= 0);
	
	if (midiNoteActive)
		return getMidiNoteName (midiNote);
	
	// Return empty string to let editor show normal time display
	return "";
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new ECHOTRAudioProcessor();
}
