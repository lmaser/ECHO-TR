#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

namespace
{
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
	
	uiWidthParam = apvts.getRawParameterValue (kParamUiWidth);
	uiHeightParam = apvts.getRawParameterValue (kParamUiHeight);
	uiPaletteParam = apvts.getRawParameterValue (kParamUiPalette);
	uiFxTailParam = apvts.getRawParameterValue (kParamUiFxTail);
	uiColorParams[0] = apvts.getRawParameterValue (kParamUiColor0);
	uiColorParams[1] = apvts.getRawParameterValue (kParamUiColor1);
	uiColorParams[2] = apvts.getRawParameterValue (kParamUiColor2);
	uiColorParams[3] = apvts.getRawParameterValue (kParamUiColor3);

	// Load UI state from parameters
	const int w = loadIntParamOrDefault (uiWidthParam, 360);
	const int h = loadIntParamOrDefault (uiHeightParam, 480);
	uiEditorWidth.store (w, std::memory_order_relaxed);
	uiEditorHeight.store (h, std::memory_order_relaxed);
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
	
	if (feedback > 0.99f)
		return 30.0; // Cap at 30 seconds for very high feedback (supports long sync delays)
	
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
	
	// Allocate delay buffer: POWER OF 2 for efficient wrapping with bitwise AND
	// Use kTimeMsMaxSync (20s) to support long sync divisions like 8/1
	const int requestedSamples = (int) std::ceil (sampleRate * (kTimeMsMaxSync / 1000.0)) + 1024;
	// Round up to next power of 2
	int powerOf2 = 1;
	while (powerOf2 < requestedSamples)
		powerOf2 <<= 1;
	
	delayBufferLength = powerOf2;
	delayBuffer.setSize (2, delayBufferLength);
	delayBuffer.clear();
	delayBufferWritePos = 0;
	
	// Reset feedback state
	feedbackState[0] = 0.0f;
	feedbackState[1] = 0.0f;
	
	// Initialize tape-style delay time smoothing
	smoothedDelaySamples = 0.0f;
}

void ECHOTRAudioProcessor::releaseResources()
{
	delayBuffer.setSize (0, 0);
	delayBufferLength = 0;
	delayBufferWritePos = 0;
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
                                                float outputGain, float mix)
{
	auto* channelL = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
	auto* channelR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
	
	// Get raw pointers to delay buffer for maximum performance
	auto* delayL = delayBuffer.getWritePointer (0);
	auto* delayR = delayBuffer.getWritePointer (1);
	
	// Power-of-2 wrap mask for efficient modulo with bitwise AND
	const int wrapMask = delayBufferLength - 1;
	int writePos = delayBufferWritePos;
	
	// Exponential smoothing per-sample: smooth gradual changes without abrupt jumps
	// Coefficient 0.9997 = ~330ms time constant (smooth but responsive)
	const float smoothCoeff = 0.9997f;
	const float targetDelay = delaySamples;
	
	for (int i = 0; i < numSamples; ++i)
	{
		// Exponential smoothing: smooth_new = smooth_old * coeff + target * (1-coeff)
		smoothedDelaySamples = smoothedDelaySamples * smoothCoeff + targetDelay * (1.0f - smoothCoeff);
		
		// Calculate read position with smoothly varying delay
		float readPosF = (float) writePos - smoothedDelaySamples;
		if (readPosF < 0.0f)
			readPosF += (float) delayBufferLength;
		
		const int readPos0 = (int) readPosF;
		const int readPos1 = (readPos0 + 1) & wrapMask; // Bitwise AND = faster than modulo
		const float frac = readPosF - (float) readPos0;
		
		// Process left channel
		if (channelL != nullptr)
		{
			const float inputL = channelL[i];
			const float sample0 = delayL[readPos0];
			const float sample1 = delayL[readPos1];
			const float delayedL = sample0 + frac * (sample1 - sample0);
			
			// Clip to prevent spikes
			const float clippedDelayedL = juce::jlimit (-2.0f, 2.0f, delayedL);
			
			delayL[writePos] = inputL * inputGain + clippedDelayedL * feedback;
			channelL[i] = (inputL * (1.0f - mix) + clippedDelayedL * mix) * outputGain;
		}
		
		// Process right channel
		if (channelR != nullptr)
		{
			const float inputR = channelR[i];
			const float sample0 = delayR[readPos0];
			const float sample1 = delayR[readPos1];
			const float delayedR = sample0 + frac * (sample1 - sample0);
			
			// Clip to prevent spikes
			const float clippedDelayedR = juce::jlimit (-2.0f, 2.0f, delayedR);
			
			delayR[writePos] = inputR * inputGain + clippedDelayedR * feedback;
			channelR[i] = (inputR * (1.0f - mix) + clippedDelayedR * mix) * outputGain;
		}
		
		// Power-of-2 wrap: bitwise AND instead of conditional (faster)
		writePos = (writePos + 1) & wrapMask;
	}
	
	delayBufferWritePos = writePos;
}

void ECHOTRAudioProcessor::processMonoDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
                                              float delaySamples, float feedback, float inputGain,
                                              float outputGain, float mix)
{
	auto* channelL = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
	auto* channelR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
	
	auto* delayL = delayBuffer.getWritePointer (0);
	auto* delayR = delayBuffer.getWritePointer (1);
	
	const int wrapMask = delayBufferLength - 1;
	int writePos = delayBufferWritePos;
	
	// Exponential smoothing per-sample: smooth gradual changes without abrupt jumps
	const float smoothCoeff = 0.9997f;
	const float targetDelay = delaySamples;
	
	for (int i = 0; i < numSamples; ++i)
	{
		smoothedDelaySamples = smoothedDelaySamples * smoothCoeff + targetDelay * (1.0f - smoothCoeff);
		float readPosF = (float) writePos - smoothedDelaySamples;
		if (readPosF < 0.0f)
			readPosF += (float) delayBufferLength;
		
		const int readPos0 = (int) readPosF;
		const int readPos1 = (readPos0 + 1) & wrapMask;
		const float frac = readPosF - (float) readPos0;
		
		const float sample0 = delayL[readPos0];
		const float sample1 = delayL[readPos1];
		const float delayed = sample0 + frac * (sample1 - sample0);
		
		// Clip to prevent spikes
		const float clippedDelayed = juce::jlimit (-2.0f, 2.0f, delayed);
		
		const float inputL = channelL != nullptr ? channelL[i] : 0.0f;
		const float inputR = channelR != nullptr ? channelR[i] : inputL;
		const float inputMid = (inputL + inputR) * 0.5f;
		
		const float toWrite = inputMid * inputGain + clippedDelayed * feedback;
		delayL[writePos] = toWrite;
		delayR[writePos] = toWrite;
		
		const float output = (inputMid * (1.0f - mix) + clippedDelayed * mix) * outputGain;
		if (channelL != nullptr) channelL[i] = output;
		if (channelR != nullptr) channelR[i] = output;
		
		writePos = (writePos + 1) & wrapMask;
	}
	
	delayBufferWritePos = writePos;
}

void ECHOTRAudioProcessor::processPingPongDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
                                                  float delaySamples, float feedback, float inputGain,
                                                  float outputGain, float mix)
{
	auto* channelL = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
	auto* channelR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
	
	auto* delayL = delayBuffer.getWritePointer (0);
	auto* delayR = delayBuffer.getWritePointer (1);
	
	const int wrapMask = delayBufferLength - 1;
	int writePos = delayBufferWritePos;
	
	// Exponential smoothing per-sample: smooth gradual changes without abrupt jumps
	const float smoothCoeff = 0.9997f;
	const float targetDelay = delaySamples;
	
	for (int i = 0; i < numSamples; ++i)
	{
		smoothedDelaySamples = smoothedDelaySamples * smoothCoeff + targetDelay * (1.0f - smoothCoeff);
		float readPosF = (float) writePos - smoothedDelaySamples;
		if (readPosF < 0.0f)
			readPosF += (float) delayBufferLength;
		
		const int readPos0 = (int) readPosF;
		const int readPos1 = (readPos0 + 1) & wrapMask;
		const float frac = readPosF - (float) readPos0;
		
		const float sampleL0 = delayL[readPos0];
		const float sampleL1 = delayL[readPos1];
		const float delayedL = sampleL0 + frac * (sampleL1 - sampleL0);
		
		const float sampleR0 = delayR[readPos0];
		const float sampleR1 = delayR[readPos1];
		const float delayedR = sampleR0 + frac * (sampleR1 - sampleR0);
		
		// Clip to prevent spikes
		const float clippedDelayedL = juce::jlimit (-2.0f, 2.0f, delayedL);
		const float clippedDelayedR = juce::jlimit (-2.0f, 2.0f, delayedR);
		
		const float inputL = channelL != nullptr ? channelL[i] : 0.0f;
		const float inputR = channelR != nullptr ? channelR[i] : 0.0f;
		const float inputMono = (inputL + inputR) * 0.5f;
		
		delayL[writePos] = inputMono * inputGain + clippedDelayedR * feedback;
		delayR[writePos] = clippedDelayedL * feedback;
		
		if (channelL != nullptr) channelL[i] = (inputL * (1.0f - mix) + clippedDelayedL * mix) * outputGain;
		if (channelR != nullptr) channelR[i] = (inputR * (1.0f - mix) + clippedDelayedR * mix) * outputGain;
		
		writePos = (writePos + 1) & wrapMask;
	}
	
	delayBufferWritePos = writePos;
}

void ECHOTRAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
	juce::ScopedNoDenormals noDenormals;

	const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
	const int numSamples = buffer.getNumSamples();
	
	// Process MIDI messages for note tracking
	const bool midiEnabled = loadBoolParamOrDefault (midiParam, false);
	const int selectedMidiPort = midiPort.load (std::memory_order_relaxed); // 0 = disabled, 1-16 = MIDI channel
	
	if (midiEnabled && selectedMidiPort > 0)
	{
		for (const auto metadata : midiMessages)
		{
			const auto msg = metadata.getMessage();
			
			// Filter by MIDI channel: msg.getChannel() returns 1-16
			if (msg.getChannel() != selectedMidiPort)
				continue; // Skip messages from other channels
			
			if (msg.isNoteOn())
			{
				// Store the note number and calculate its frequency
				const int noteNumber = msg.getNoteNumber();
				lastMidiNote.store (noteNumber, std::memory_order_relaxed);
				
				// MIDI note to frequency: freq = 440 * 2^((note - 69) / 12)
				const float frequency = 440.0f * std::pow (2.0f, (noteNumber - 69) / 12.0f);
				currentMidiFrequency.store (frequency, std::memory_order_relaxed);
			}
			else if (msg.isNoteOff())
			{
				// Clear MIDI note if it matches the one we're tracking
				if (msg.getNoteNumber() == lastMidiNote.load (std::memory_order_relaxed))
				{
					lastMidiNote.store (-1, std::memory_order_relaxed);
					currentMidiFrequency.store (0.0f, std::memory_order_relaxed);
				}
			}
		}
	}
	else
	{
		// MIDI disabled, clear tracking
		lastMidiNote.store (-1, std::memory_order_relaxed);
		currentMidiFrequency.store (0.0f, std::memory_order_relaxed);
	}

	// Clear extra output channels
	for (int i = numChannels; i < buffer.getNumChannels(); ++i)
		buffer.clear (i, 0, numSamples);

	// If delay buffer not ready, pass through
	if (delayBufferLength == 0 || currentSampleRate <= 0.0)
		return;

	// Read parameters ONCE per block
	const bool syncEnabled = loadBoolParamOrDefault (syncParam, false);
	const bool midiNoteActive = midiEnabled && (lastMidiNote.load (std::memory_order_relaxed) >= 0);
	const float timeMsValue = loadAtomicOrDefault (timeMsParam, kTimeMsDefault);
	const int timeSyncValue = loadIntParamOrDefault (timeSyncParam, kTimeSyncDefault);
	const int mode = loadIntParamOrDefault (modeParam, 1); // 0=MONO, 1=STEREO, 2=PING-PONG
	
	// Calculate target delay time
	float targetDelayMs = timeMsValue;
	
	// Priority: MIDI note > Sync > Manual time
	if (midiNoteActive)
	{
		// MIDI note is active: calculate delay time from frequency (period)
		const float frequency = currentMidiFrequency.load (std::memory_order_relaxed);
		if (frequency > 0.1f) // Sanity check
		{
			// Period in milliseconds = 1000 / frequency
			targetDelayMs = 1000.0f / frequency;
		}
	}
	else if (syncEnabled)
	{
		// Get tempo from host
		auto posInfo = getPlayHead();
		double bpm = 120.0;
		if (posInfo != nullptr)
		{
			auto pos = posInfo->getPosition();
			if (pos.hasValue() && pos->getBpm().hasValue())
				bpm = *pos->getBpm();
		}
		targetDelayMs = tempoSyncToMs (timeSyncValue, bpm);
	}
	
	// Clamp delay time to valid range (different limits for manual vs sync mode)
	const float maxAllowedDelayMs = syncEnabled ? kTimeMsMaxSync : kTimeMsMax;
	targetDelayMs = juce::jlimit (0.0f, maxAllowedDelayMs, targetDelayMs);
	
	// Read other parameters
	float targetFeedback = loadAtomicOrDefault (feedbackParam, kFeedbackDefault);
	const bool autoFbkEnabled = loadBoolParamOrDefault (autoFbkParam, false);
	const float inputGainDb = loadAtomicOrDefault (inputParam, kInputDefault);
	const float outputGainDb = loadAtomicOrDefault (outputParam, kOutputDefault);
	const float mixValue = loadAtomicOrDefault (mixParam, kMixDefault);
	const float modValue = loadAtomicOrDefault (modParam, kModDefault);
	
	// Convert dB to linear ONCE
	const float inputGain = juce::Decibels::decibelsToGain (inputGainDb);
	const float outputGain = juce::Decibels::decibelsToGain (outputGainDb);
	
	// Clamp feedback strictly (0 means NO feedback, no smoothing past 0)
	targetFeedback = juce::jlimit (0.0f, 0.99f, targetFeedback);
	
	// AUTO FEEDBACK: Apply aggressive exponential curve + multiplier for maximum control range
	// Problem: With x^2.5, only 0-21% is useful (reaches clamp too fast)
	// Solution: Use x^4.0 for more dramatic curve, extending useful range to ~40-50%
	if (autoFbkEnabled && targetFeedback > 0.001f && targetDelayMs > 1.0f)
	{
		// Step 1: Apply aggressive exponential curve (x^4.0) for maximum resolution at low values
		// This gives: 10% linear → 0.01% exponential, 50% linear → 6.25% exponential
		const float exponentialFeedback = std::pow (targetFeedback, 4.0f);
		
		// Step 2: Calculate auto feedback multiplier based on delay time
		const float referenceDelay = syncEnabled ? kTimeMsMaxSync : kTimeMsMax;
		const float octavesFromMax = std::log2 (referenceDelay / targetDelayMs);
		const float autoFbkMultiplier = std::pow (1.5f, octavesFromMax);
		
		// Step 3: Apply multiplier and clamp
		targetFeedback = juce::jmin (0.99f, exponentialFeedback * autoFbkMultiplier);
	}
	
	// Calculate frequency multiplier from MOD (pitch bend effect)
	// MOD = 0.0 → ÷4 (0.25x freq, longer delay, lower pitch)
	// MOD = 0.5 → ×1 (1.0x freq, normal)
	// MOD = 1.0 → ×4 (4.0x freq, shorter delay, higher pitch)
	float freqMultiplier;
	if (modValue < 0.5f)
	{
		// 0.0→0.5 maps to 0.25→1.0
		freqMultiplier = 0.25f + (modValue * 1.5f);
	}
	else
	{
		// 0.5→1.0 maps to 1.0→4.0
		freqMultiplier = 1.0f + ((modValue - 0.5f) * 6.0f);
	}
	
	// Calculate delay in samples ONCE (not per sample!)
	float delaySamples = juce::jmax (0.0f, (float) currentSampleRate * (targetDelayMs / 1000.0f));
	
	// Apply pitch bend (MOD): divide delay by frequency multiplier
	// Higher frequency = shorter delay = higher pitch
	// Lower frequency = longer delay = lower pitch
	delaySamples /= freqMultiplier;
	
	// Clamp to buffer size
	const float maxDelaySamples = (float) (delayBufferLength - 2);
	delaySamples = juce::jmin (delaySamples, maxDelaySamples);
	delaySamples = juce::jmax (0.0f, delaySamples);
	
	// TRUE BYPASS: only bypass if delay is essentially zero (< 1 sample)
	if (delaySamples < 1.0f)
	{
		// Pure dry signal (no delay, no feedback, no phasing)
		for (int ch = 0; ch < numChannels; ++ch)
		{
			auto* channelData = buffer.getWritePointer (ch);
			for (int i = 0; i < numSamples; ++i)
			{
				channelData[i] *= outputGain;
			}
		}
		// Reset smoothing for clean bypass
		smoothedDelaySamples = 0.0f;
		return;
	}
	
	// Process block efficiently (NO SMOOTHING for max performance)
	if (mode == 0) // MONO
	{
		processMonoDelay (buffer, numSamples, numChannels, delaySamples, 
		                  targetFeedback, inputGain, outputGain, mixValue);
	}
	else if (mode == 2) // PING-PONG
	{
		processPingPongDelay (buffer, numSamples, numChannels, delaySamples,
		                      targetFeedback, inputGain, outputGain, mixValue);
	}
	else // STEREO (default)
	{
		processStereoDelay (buffer, numSamples, numChannels, delaySamples,
		                    targetFeedback, inputGain, outputGain, mixValue);
	}
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
		if (xmlState->hasTagName (apvts.state.getType()))
			apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
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
// Tempo sync division names (order: triplet → normal → dotted for each base division)
juce::StringArray ECHOTRAudioProcessor::getTimeSyncChoices()
{
	return {
		// 1/64: triplet, normal, dotted
		"1/64T", "1/64", "1/64.",
		// 1/32: triplet, normal, dotted
		"1/32T", "1/32", "1/32.",
		// 1/16: triplet, normal, dotted
		"1/16T", "1/16", "1/16.",
		// 1/8: triplet, normal, dotted
		"1/8T", "1/8", "1/8.",
		// 1/4: triplet, normal, dotted
		"1/4T", "1/4", "1/4.",
		// 1/2: triplet, normal, dotted
		"1/2T", "1/2", "1/2.",
		// 1/1: triplet, normal, dotted
		"1/1T", "1/1", "1/1.",
		// 2/1: triplet, normal, dotted
		"2/1T", "2/1", "2/1.",
		// 4/1: triplet, normal, dotted
		"4/1T", "4/1", "4/1.",
		// 8/1: triplet, normal, dotted
		"8/1T", "8/1", "8/1."
	};
}

juce::String ECHOTRAudioProcessor::getTimeSyncName (int index)
{
	auto choices = getTimeSyncChoices();
	if (index >= 0 && index < choices.size())
		return choices[index];
	return "1/8";
}

juce::String ECHOTRAudioProcessor::getTimeSyncNameShort (int index)
{
	// Return the full division name (no shortening)
	return getTimeSyncName (index);
}

//==============================================================================
float ECHOTRAudioProcessor::tempoSyncToMs (int syncIndex, double bpm) const
{
	if (bpm <= 0.0)
		bpm = 120.0; // Fallback BPM if host doesn't provide tempo
	
	// Clamp index to valid range
	syncIndex = juce::jlimit (0, 29, syncIndex);
	
	// Base divisions: 1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1/1, 2/1, 4/1, 8/1
	const float divisions[] = { 64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f };
	
	// New order: every 3 indices = triplet, normal, dotted
	const int baseIndex = syncIndex / 3;  // Which base division (0-9)
	const int modifier = syncIndex % 3;   // 0=triplet, 1=normal, 2=dotted
	
	// Calculate straight quarter note duration in ms
	const float quarterNoteMs = (float) (60000.0 / bpm);
	
	// Calculate base duration (relative to quarter note)
	float durationMs = quarterNoteMs * (4.0f / divisions[baseIndex]);
	
	// Apply modifiers
	if (modifier == 0)      // Triplet
		durationMs *= (2.0f / 3.0f);
	else if (modifier == 2) // Dotted
		durationMs *= 1.5f;
	// modifier == 1 (normal) stays as is
	
	return durationMs;
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ECHOTRAudioProcessor::createParameterLayout()
{
	std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

	// Time (ms mode)
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamTimeMs, "Time",
		juce::NormalisableRange<float> (kTimeMsMin, kTimeMsMax, 0.0f, 0.35f), kTimeMsDefault));

	// Time (sync mode)
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamTimeSync, "Time Sync", getTimeSyncChoices(), kTimeSyncDefault));

	// Feedback
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFeedback, "Feedback",
		juce::NormalisableRange<float> (kFeedbackMin, kFeedbackMax, 0.0f, 1.0f), kFeedbackDefault));

	// Mode: 0=MONO, 1=STEREO, 2=PING-PONG
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamMode, "Mode",
		juce::NormalisableRange<float> ((float)kModeMin, (float)kModeMax, 1.0f, 1.0f), kModeDefault));

	// Mod (pitch bend: 0.0=÷4, 0.5=x1, 1.0=x4)
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamMod, "Mod",
		juce::NormalisableRange<float> (kModMin, kModMax, 0.0f, 1.0f), kModDefault));

	// Input gain
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamInput, "Input",
		juce::NormalisableRange<float> (kInputMin, kInputMax, 0.0f, 2.5f), kInputDefault));

	// Output gain
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamOutput, "Output",
		juce::NormalisableRange<float> (kOutputMin, kOutputMax, 0.0f, 2.5f), kOutputDefault));

	// Mix
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamMix, "Mix",
		juce::NormalisableRange<float> (kMixMin, kMixMax, 0.0f, 1.0f), kMixDefault));

	// Sync button
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamSync, "Sync", false));

	// MIDI button
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamMidi, "MIDI", false));

	// Auto Feedback button
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamAutoFbk, "Auto Feedback", false));

	// UI state parameters (hidden from automation)
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiWidth, "UI Width", 360, 1600, 360));
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiHeight, "UI Height", 240, 1200, 480));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamUiPalette, "UI Palette", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamUiFxTail, "UI FX Tail", true));
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiColor0, "UI Color 0", 0, 0xFFFFFF, 0xFFFFFF));
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiColor1, "UI Color 1", 0, 0xFFFFFF, 0x000000));
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiColor2, "UI Color 2", 0, 0xFFFFFF, 0xFFFFFF));
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiColor3, "UI Color 3", 0, 0xFFFFFF, 0x000000));

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

void ECHOTRAudioProcessor::setUiFxTailEnabled (bool shouldEnableFxTail)
{
	uiFxTailEnabled.store (shouldEnableFxTail ? 1 : 0, std::memory_order_relaxed);
	apvts.state.setProperty (UiStateKeys::fxTailEnabled, shouldEnableFxTail, nullptr);
	setParameterPlainValue (apvts, kParamUiFxTail, shouldEnableFxTail ? 1.0f : 0.0f);
	updateHostDisplay();
}

bool ECHOTRAudioProcessor::getUiFxTailEnabled() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::fxTailEnabled);
	if (! fromState.isVoid())
		return (bool) fromState;
	if (uiFxTailParam != nullptr)
		return uiFxTailParam->load (std::memory_order_relaxed) > 0.5f;
	return uiFxTailEnabled.load (std::memory_order_relaxed) != 0;
}

void ECHOTRAudioProcessor::setMidiPort (int portNumber)
{
	const int port = juce::jlimit (0, 127, portNumber);
	midiPort.store (port, std::memory_order_relaxed);
	apvts.state.setProperty (UiStateKeys::midiPort, port, nullptr);
}

int ECHOTRAudioProcessor::getMidiPort() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::midiPort);
	if (! fromState.isVoid())
		return (int) fromState;
	
	return midiPort.load (std::memory_order_relaxed);
}

void ECHOTRAudioProcessor::setUiCustomPaletteColour (int index, juce::Colour colour)
{
	if (index >= 0 && index < 4)
	{
		uiCustomPalette[(size_t) index].store (colour.getARGB(), std::memory_order_relaxed);
		const juce::String key = UiStateKeys::customPalette[(size_t) index];
		apvts.state.setProperty (key, (int) colour.getARGB(), nullptr);
		if (uiColorParams[(size_t) index] != nullptr)
			setParameterPlainValue (apvts, (index == 0 ? kParamUiColor0 :
											 index == 1 ? kParamUiColor1 :
											 index == 2 ? kParamUiColor2 : kParamUiColor3),
									(float) (int) colour.getARGB());
		updateHostDisplay();
	}
}

juce::Colour ECHOTRAudioProcessor::getUiCustomPaletteColour (int index) const noexcept
{
	if (index < 0 || index >= 4)
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
