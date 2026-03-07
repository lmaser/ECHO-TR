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
	reversePitchParam = apvts.getRawParameterValue (kParamReversePitch);
	
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
	const float feedback = loadAtomicOrDefault (feedbackParam, kFeedbackDefault);
	
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
	if (feedback < 0.01f || delayMs < 0.5f)
		return 0.5; // Minimum 0.5s tail for very low feedback
	
	if (feedback >= 1.0f)
		return 30.0; // feedback >= 100% → infinite sustain / self-oscillation
	
	const double delaySeconds = delayMs / 1000.0;
	const double numRepeatsTo60dB = std::log (0.001) / std::log (feedback);
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
	smoothedInputGain = 1.0f;
	smoothedOutputGain = 1.0f;
	smoothedMix = 0.5f;

	// Reset reverse delay state
	reverseAnchor     = 0;
	reverseCounter    = 0.0f;
	reverseChunkLen   = 0.0f;
	revSmoothedDelay  = 0.0f;
	reverseNeedsInit  = true;

	// Reset auto-feedback envelope
	autoFbkEnvelope         = 1.0f;
	autoFbkLastDelaySamples = -1.0f;
	autoFbkCooldownLeft     = 0;

	// Reset feedback loop processing state
	fbkDcStateInL = fbkDcStateInR = 0.0f;
	fbkDcStateOutL = fbkDcStateOutR = 0.0f;
}

void ECHOTRAudioProcessor::releaseResources()
{
	delayBuffer.setSize (0, 0);
	delayBufferLength = 0;
	delayBufferWritePos = 0;
	reverseAnchor     = 0;
	reverseCounter    = 0.0f;
	reverseChunkLen   = 0.0f;
	revSmoothedDelay  = 0.0f;
	reverseNeedsInit  = true;
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
	const float gainSmoothCoeff = 0.9955f; // ~5ms time constant
	
	for (int i = 0; i < numSamples; ++i)
	{
		smoothedDelaySamples = smoothedDelaySamples * smoothCoeff + targetDelay * (1.0f - smoothCoeff);
		smoothedInputGain  = smoothedInputGain  * gainSmoothCoeff + inputGain  * (1.0f - gainSmoothCoeff);
		smoothedOutputGain = smoothedOutputGain * gainSmoothCoeff + outputGain * (1.0f - gainSmoothCoeff);
		smoothedMix        = smoothedMix        * gainSmoothCoeff + mix        * (1.0f - gainSmoothCoeff);
		
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
				float fbkL = delayedL * feedback;
				float fbkR = delayedR * feedback;

				// DC blocker (transparent feedback path)
				fbkL = dcBlockTick (fbkL, fbkDcStateInL, fbkDcStateOutL, fbkDcCoeff);
				fbkR = dcBlockTick (fbkR, fbkDcStateInR, fbkDcStateOutR, fbkDcCoeff);

				delayL[writePos] = inputL * smoothedInputGain + fbkL;
				delayR[writePos] = inputR * smoothedInputGain + fbkR;
				channelL[i] = inputL * (1.0f - smoothedMix) + delayedL * smoothedMix * smoothedOutputGain;
				channelR[i] = inputR * (1.0f - smoothedMix) + delayedR * smoothedMix * smoothedOutputGain;
			}
			else
			{
				float fbkL = delayedL * feedback;
				// DC blocker (transparent feedback path)
				fbkL = dcBlockTick (fbkL, fbkDcStateInL, fbkDcStateOutL, fbkDcCoeff);
				delayL[writePos] = inputL * smoothedInputGain + fbkL;
				channelL[i] = inputL * (1.0f - smoothedMix) + delayedL * smoothedMix * smoothedOutputGain;
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
	const float gainSmoothCoeff = 0.9955f;
	
	for (int i = 0; i < numSamples; ++i)
	{
		smoothedDelaySamples = smoothedDelaySamples * smoothCoeff + targetDelay * (1.0f - smoothCoeff);
		smoothedInputGain  = smoothedInputGain  * gainSmoothCoeff + inputGain  * (1.0f - gainSmoothCoeff);
		smoothedOutputGain = smoothedOutputGain * gainSmoothCoeff + outputGain * (1.0f - gainSmoothCoeff);
		smoothedMix        = smoothedMix        * gainSmoothCoeff + mix        * (1.0f - gainSmoothCoeff);
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
		
		float fbkMono = delayed * feedback;

		// DC blocker (transparent feedback path)
		fbkMono = dcBlockTick (fbkMono, fbkDcStateInL, fbkDcStateOutL, fbkDcCoeff);

		const float toWrite = inputMid * smoothedInputGain + fbkMono;
		delayL[writePos] = toWrite;
		delayR[writePos] = toWrite;
		
		if (channelL != nullptr) channelL[i] = inputL * (1.0f - smoothedMix) + delayed * smoothedMix * smoothedOutputGain;
		if (channelR != nullptr) channelR[i] = inputR * (1.0f - smoothedMix) + delayed * smoothedMix * smoothedOutputGain;
		
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
	const float gainSmoothCoeff = 0.9955f;
	
	for (int i = 0; i < numSamples; ++i)
	{
		smoothedDelaySamples = smoothedDelaySamples * smoothCoeff + targetDelay * (1.0f - smoothCoeff);
		smoothedInputGain  = smoothedInputGain  * gainSmoothCoeff + inputGain  * (1.0f - gainSmoothCoeff);
		smoothedOutputGain = smoothedOutputGain * gainSmoothCoeff + outputGain * (1.0f - gainSmoothCoeff);
		smoothedMix        = smoothedMix        * gainSmoothCoeff + mix        * (1.0f - gainSmoothCoeff);
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
		
		float fbkPpL = delayedR * feedback;
		float fbkPpR = delayedL * feedback;

		// DC blocker (transparent feedback path)
		fbkPpL = dcBlockTick (fbkPpL, fbkDcStateInL, fbkDcStateOutL, fbkDcCoeff);
		fbkPpR = dcBlockTick (fbkPpR, fbkDcStateInR, fbkDcStateOutR, fbkDcCoeff);

		delayL[writePos] = inputMono * smoothedInputGain + fbkPpL;
		delayR[writePos] = fbkPpR;
		
		if (channelL != nullptr) channelL[i] = inputL * (1.0f - smoothedMix) + delayedL * smoothedMix * smoothedOutputGain;
		if (channelR != nullptr) channelR[i] = inputR * (1.0f - smoothedMix) + delayedR * smoothedMix * smoothedOutputGain;
		
		writePos = (writePos + 1) & wrapMask;
	}
	
	delayBufferWritePos = writePos;
}

void ECHOTRAudioProcessor::processReverseDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
                                                 float delaySamples, float feedback, float inputGain,
                                                 float outputGain, float mix, float delaySmoothCoeff,
                                                 float pitchRate)
{
	// Single-buffer reverse delay.
	// ONE read head reads BACKWARDS through the main circular delay buffer.
	// Chunk length is LOCKED at chunk start from the EMA-smoothed delay.
	// This prevents mid-chunk length changes that cause clicks when time
	// drops quickly.  The EMA still runs every sample so the NEXT chunk
	// picks up the new target smoothly.
	// Tukey cosine taper at chunk edges prevents clicks at boundaries.

	auto* channelL = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
	auto* channelR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
	auto* delayL = delayBuffer.getWritePointer (0);
	auto* delayR = delayBuffer.getWritePointer (1);

	const int wrapMask = delayBufferLength - 1;
	int writePos = delayBufferWritePos;

	const float smoothCoeff = delaySmoothCoeff;
	const float targetDelay = delaySamples;
	const float gainSmoothCoeff = 0.9955f;

	// Tukey taper: cosine fade over this many samples at chunk start/end
	constexpr int kTaperSamples = 128;
	// Minimum chunk length — must be small enough that reverse mode covers
	// the same MIDI note range as forward mode.  4 samples ≈ 12 kHz @ 48 kHz
	// (MIDI note ≈ 120), well above any practical musical pitch.
	constexpr float kMinChunkLen = 4.0f;

	for (int i = 0; i < numSamples; ++i)
	{
		// EMA runs every sample — always tracks target, even mid-chunk
		revSmoothedDelay = revSmoothedDelay * smoothCoeff + targetDelay * (1.0f - smoothCoeff);
		smoothedInputGain  = smoothedInputGain  * gainSmoothCoeff + inputGain  * (1.0f - gainSmoothCoeff);
		smoothedOutputGain = smoothedOutputGain * gainSmoothCoeff + outputGain * (1.0f - gainSmoothCoeff);
		smoothedMix        = smoothedMix        * gainSmoothCoeff + mix        * (1.0f - gainSmoothCoeff);

		// ── Chunk init / boundary ──
		if (reverseNeedsInit)
		{
			reverseAnchor    = writePos;
			reverseCounter   = 0.0f;
			float candidateLen = juce::jmax (kMinChunkLen, revSmoothedDelay);

			// Safety: with pitchRate < 1 the chunk takes chunkLen/pitchRate
			// real-time samples to complete.  If that exceeds half the
			// circular buffer the write head would overwrite data we're
			// still reading.  Clamp to keep it safe.
			if (pitchRate > 0.0f && pitchRate < 1.0f)
			{
				const float maxSafe = (float) (delayBufferLength / 2) * pitchRate;
				candidateLen = juce::jmin (candidateLen, maxSafe);
			}
			reverseChunkLen  = candidateLen;
			reverseNeedsInit = false;
		}

		const float chunkLen = reverseChunkLen;  // locked for this chunk

		const float inputL = channelL != nullptr ? channelL[i] : 0.0f;
		const float inputR = channelR != nullptr ? channelR[i] : 0.0f;

		// Read backwards from anchor
		float readPosF = (float) reverseAnchor - reverseCounter;
		if (readPosF < 0.0f) readPosF += (float) delayBufferLength;

		const int idx0  = static_cast<int>(readPosF) & wrapMask;
		const int idxM1 = (idx0 + wrapMask) & wrapMask;
		const int idx1  = (idx0 + 1) & wrapMask;
		const int idx2  = (idx0 + 2) & wrapMask;
		const float frac = readPosF - static_cast<float>(static_cast<int>(readPosF));

		float revL = hermite4pt (delayL[idxM1], delayL[idx0], delayL[idx1], delayL[idx2], frac);
		float revR = hermite4pt (delayR[idxM1], delayR[idx0], delayR[idx1], delayR[idx2], frac);

		// Tukey cosine taper at chunk edges
		// Taper length adapts to chunk size: 25 % per edge, capped so both
		// fade-in and fade-out fit inside the chunk (≤ 50 % total).
		const int halfChunk = static_cast<int>(chunkLen * 0.5f);
		const int taperLen = juce::jmin (halfChunk, juce::jmax (1, juce::jmin (kTaperSamples, static_cast<int>(chunkLen * 0.25f))));
		{
			const float pos = reverseCounter;
			const float remaining = chunkLen - pos;
			const float taperF = static_cast<float>(taperLen);
			if (pos < taperF)
			{
				const float w = taperWeight (pos, taperF);
				revL *= w;
				revR *= w;
			}
			else if (remaining < taperF)
			{
				const float w = taperWeight (remaining, taperF);
				revL *= w;
				revR *= w;
			}
		}

		// Write to delay buffer: input + reversed signal × feedback
		float fbkRevL = revL * feedback;
		float fbkRevR = revR * feedback;

		// DC blocker (transparent feedback path)
		fbkRevL = dcBlockTick (fbkRevL, fbkDcStateInL, fbkDcStateOutL, fbkDcCoeff);
		fbkRevR = dcBlockTick (fbkRevR, fbkDcStateInR, fbkDcStateOutR, fbkDcCoeff);

		delayL[writePos] = inputL * smoothedInputGain + fbkRevL;
		delayR[writePos] = inputR * smoothedInputGain + fbkRevR;

		// Output: dry/wet mix
		if (channelL != nullptr) channelL[i] = inputL * (1.0f - smoothedMix) + revL * smoothedMix * smoothedOutputGain;
		if (channelR != nullptr) channelR[i] = inputR * (1.0f - smoothedMix) + revR * smoothedMix * smoothedOutputGain;

		// Advance write position
		writePos = (writePos + 1) & wrapMask;

		// Advance reverse counter; start new chunk when done.
		// When MIDI glide is active the smoothed delay can diverge
		// significantly from the locked chunk length.  To make the
		// pitch glide feel as continuous as in forward mode, force an
		// early chunk boundary once the EMA has moved far enough from
		// the current chunk length (> 20 % ratio change).  The taper
		// fade-out at chunk end ensures this stays click-free.
		reverseCounter += pitchRate;
		const float smoothedNow = juce::jmax (kMinChunkLen, revSmoothedDelay);
		const float ratio = (smoothedNow > chunkLen)
		                   ? (smoothedNow / chunkLen)
		                   : (chunkLen / smoothedNow);
		const bool chunkDone   = reverseCounter >= chunkLen;
		const bool glideCutoff = (ratio > 1.08f) && (reverseCounter >= kMinChunkLen);
		if (chunkDone || glideCutoff)
		{
			reverseCounter  = 0.0f;
			reverseAnchor   = writePos;
			// Lock NEW chunk length from current smoothed delay
			float newLen = smoothedNow;
			if (pitchRate > 0.0f && pitchRate < 1.0f)
			{
				const float maxSafe = (float) (delayBufferLength / 2) * pitchRate;
				newLen = juce::jmin (newLen, maxSafe);
			}
			reverseChunkLen = newLen;
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
	
	// Fast dB→linear (std::exp2 instead of std::pow via Decibels::decibelsToGain)
	const float inputGain  = fastDecibelsToGain (inputGainDb);
	const float outputGain = fastDecibelsToGain (outputGainDb);
	targetFeedback = juce::jlimit (0.0f, kFeedbackMax, targetFeedback);

	// MOD frequency multiplier (pure arithmetic, no transcendentals)
	float freqMultiplier;
	if (modValue < 0.5f)
		freqMultiplier = 0.25f + (modValue * 1.5f);
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
		//   att 10 % → attCurved ≈ 0.001 (barely perceptible)
		//   att 25 % → attCurved ≈ 0.016 (subtle)
		//   att 50 % → attCurved ≈ 0.125 (clearly audible)
		//   att 75 % → attCurved ≈ 0.422 (dramatic)
		//   att 100 % → multiplier = envelope (full modulation)
		const float attCurved = attPct * attPct * attPct;
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
		const float gainSmoothCoeff = 0.9955f;
		for (int ch = 0; ch < numChannels; ++ch)
		{
			auto* channelData = buffer.getWritePointer (ch);
			for (int i = 0; i < numSamples; ++i)
			{
				smoothedOutputGain = smoothedOutputGain * gainSmoothCoeff + outputGain * (1.0f - gainSmoothCoeff);
				channelData[i] *= smoothedOutputGain;
			}
		}
		smoothedDelaySamples = 0.0f;
		PERF_BLOCK_END(perfTrace, numSamples, currentSampleRate);
		return;
	}
	
	// Delay smoothing coefficient (cached; only recompute for MIDI glide)
	float delaySmoothCoeff = cachedDelaySmoothCoeff;
	if (midiNoteActive)
	{
		const float vel = (float) lastMidiVelocity.load (std::memory_order_relaxed);
		const float tLin = juce::jlimit (0.0f, 1.0f, (vel - 1.0f) / 126.0f);
		// Fifth-root curve: compresses the fast zone aggressively so the vast
		// majority of the velocity range (≈60-127) feels nearly instant, while
		// only very low velocities produce audible portamento.
		//   tau_max = 300 ms (vel 1)  → 95 % reached in ~900 ms
		//   tau_min = 0.4 ms (vel 127) → essentially instant
		constexpr float kTauMax = 0.30f;   // 300 ms — long portamento at low vel
		constexpr float kTauMin = 0.0004f; // 0.4 ms
		const float t = std::pow (tLin, 0.2f);  // fifth root
		const float tau = kTauMax - t * (kTauMax - kTauMin);
		delaySmoothCoeff = std::exp (-1.0f / ((float) currentSampleRate * tau));
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
	
	// Reverse mode: chunk-based backward playback (works with any mode routing)
	if (reverseEnabled)
	{
		const float revPitchSt = loadAtomicOrDefault (reversePitchParam, kReversePitchDefault);
		const float pitchRate  = std::pow (2.0f, revPitchSt / 12.0f);
		processReverseDelay (buffer, numSamples, numChannels, delaySamples,
		                     targetFeedback, inputGain, outputGain, mixValue, delaySmoothCoeff,
		                     pitchRate);
		PERF_BLOCK_END(perfTrace, numSamples, currentSampleRate);
		return;
	}

	if (mode == 0)
	{
		processMonoDelay (buffer, numSamples, numChannels, delaySamples, 
		                  targetFeedback, inputGain, outputGain, mixValue, delaySmoothCoeff);
	}
	else if (mode == 2) // PING-PONG
	{
		processPingPongDelay (buffer, numSamples, numChannels, delaySamples,
		                      targetFeedback, inputGain, outputGain, mixValue, delaySmoothCoeff);
	}
	else // STEREO (default)
	{
		processStereoDelay (buffer, numSamples, numChannels, delaySamples,
		                    targetFeedback, inputGain, outputGain, mixValue, delaySmoothCoeff);
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
		kParamMode, "Mode",
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
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamAutoFbk, "Auto Fbk", false));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamAutoFbkTau, "Auto Fbk Tau",
		juce::NormalisableRange<float> (kAutoFbkTauMin, kAutoFbkTauMax, 0.01f, 1.0f), kAutoFbkTauDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamAutoFbkAtt, "Auto Fbk Att",
		juce::NormalisableRange<float> (kAutoFbkAttMin, kAutoFbkAttMax, 0.01f, 1.0f), kAutoFbkAttDefault));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamReverse, "Reverse", false));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamReversePitch, "Reverse Pitch",
		juce::NormalisableRange<float> (kReversePitchMin, kReversePitchMax, 0.01f, 1.0f), kReversePitchDefault));

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
