#include "PluginProcessor.h"
#include "PluginEditor.h"

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
	loopParam = apvts.getRawParameterValue (kParamLoop);
	
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

	// Allocate loop buffer (fixed 2s)
	loopBufferLength = (int) std::ceil (sampleRate * (kTimeMsMax / 1000.0)) + 1;
	loopBuffer.setSize (2, loopBufferLength);
	loopBuffer.clear();
	loopState = LoopState::Off;
	loopRecordedLength = 0;
	loopWritePos = 0;
	loopReadPos = 0.0f;
	smoothedLoopTimeSamples = 0.0f;
}

void ECHOTRAudioProcessor::releaseResources()
{
	delayBuffer.setSize (0, 0);
	delayBufferLength = 0;
	delayBufferWritePos = 0;

	loopBuffer.setSize (0, 0);
	loopBufferLength = 0;
	loopRecordedLength = 0;
	loopWritePos = 0;
	loopReadPos = 0.0f;
	loopState = LoopState::Off;
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
	
	const int wrapMask = delayBufferLength - 1;
	int writePos = delayBufferWritePos;
	
	const float smoothCoeff = 0.9997f;   // ~330ms time constant
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
		
		const int readPos0 = (int) readPosF;
		const int readPos1 = (readPos0 + 1) & wrapMask;
		const float frac = readPosF - (float) readPos0;
		
		if (channelL != nullptr)
		{
			const float inputL = channelL[i];
			const float delayedL = delayL[readPos0] + frac * (delayL[readPos1] - delayL[readPos0]);
			const float clippedL = juce::jlimit (-2.0f, 2.0f, delayedL);
			delayL[writePos] = inputL * smoothedInputGain + clippedL * feedback;
			channelL[i] = (inputL * (1.0f - smoothedMix) + clippedL * smoothedMix) * smoothedOutputGain;
		}
		
		if (channelR != nullptr)
		{
			const float inputR = channelR[i];
			const float delayedR = delayR[readPos0] + frac * (delayR[readPos1] - delayR[readPos0]);
			const float clippedR = juce::jlimit (-2.0f, 2.0f, delayedR);
			delayR[writePos] = inputR * smoothedInputGain + clippedR * feedback;
			channelR[i] = (inputR * (1.0f - smoothedMix) + clippedR * smoothedMix) * smoothedOutputGain;
		}
		
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
	
	const float smoothCoeff = 0.9997f;
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
		
		const int readPos0 = (int) readPosF;
		const int readPos1 = (readPos0 + 1) & wrapMask;
		const float frac = readPosF - (float) readPos0;
		
		const float sample0 = delayL[readPos0];
		const float sample1 = delayL[readPos1];
		const float delayed = sample0 + frac * (sample1 - sample0);
		const float clippedDelayed = juce::jlimit (-2.0f, 2.0f, delayed);
		
		const float inputL = channelL != nullptr ? channelL[i] : 0.0f;
		const float inputR = channelR != nullptr ? channelR[i] : inputL;
		const float inputMid = (inputL + inputR) * 0.5f;
		
		const float toWrite = inputMid * smoothedInputGain + clippedDelayed * feedback;
		delayL[writePos] = toWrite;
		delayR[writePos] = toWrite;
		
		const float output = (inputMid * (1.0f - smoothedMix) + clippedDelayed * smoothedMix) * smoothedOutputGain;
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
	
	const float smoothCoeff = 0.9997f;
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
		
		const int readPos0 = (int) readPosF;
		const int readPos1 = (readPos0 + 1) & wrapMask;
		const float frac = readPosF - (float) readPos0;
		
		const float sampleL0 = delayL[readPos0];
		const float sampleL1 = delayL[readPos1];
		const float delayedL = sampleL0 + frac * (sampleL1 - sampleL0);
		
		const float sampleR0 = delayR[readPos0];
		const float sampleR1 = delayR[readPos1];
		const float delayedR = sampleR0 + frac * (sampleR1 - sampleR0);
		
		const float clippedDelayedL = juce::jlimit (-2.0f, 2.0f, delayedL);
		const float clippedDelayedR = juce::jlimit (-2.0f, 2.0f, delayedR);
		
		const float inputL = channelL != nullptr ? channelL[i] : 0.0f;
		const float inputR = channelR != nullptr ? channelR[i] : 0.0f;
		const float inputMono = (inputL + inputR) * 0.5f;
		
		delayL[writePos] = inputMono * smoothedInputGain + clippedDelayedR * feedback;
		delayR[writePos] = clippedDelayedL * feedback;
		
		if (channelL != nullptr) channelL[i] = (inputL * (1.0f - smoothedMix) + clippedDelayedL * smoothedMix) * smoothedOutputGain;
		if (channelR != nullptr) channelR[i] = (inputR * (1.0f - smoothedMix) + clippedDelayedR * smoothedMix) * smoothedOutputGain;
		
		writePos = (writePos + 1) & wrapMask;
	}
	
	delayBufferWritePos = writePos;
}

void ECHOTRAudioProcessor::processLoop (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
                                         float loopTimeSamples, float inputGain, float outputGain, float mix)
{
	if (loopBufferLength <= 0)
		return;

	auto* channelL = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
	auto* channelR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
	auto* loopL = loopBuffer.getWritePointer (0);
	auto* loopR = loopBuffer.getWritePointer (1);

	const float smoothCoeff = 0.9997f;
	const float gainSmoothCoeff = 0.9955f;

	for (int i = 0; i < numSamples; ++i)
	{
		smoothedInputGain  = smoothedInputGain  * gainSmoothCoeff + inputGain  * (1.0f - gainSmoothCoeff);
		smoothedOutputGain = smoothedOutputGain * gainSmoothCoeff + outputGain * (1.0f - gainSmoothCoeff);
		smoothedMix        = smoothedMix        * gainSmoothCoeff + mix        * (1.0f - gainSmoothCoeff);

		const float inputL = channelL != nullptr ? channelL[i] : 0.0f;
		const float inputR = channelR != nullptr ? channelR[i] : 0.0f;

		if (loopState == LoopState::Recording)
		{
			loopL[loopWritePos] = inputL * smoothedInputGain;
			loopR[loopWritePos] = inputR * smoothedInputGain;
			++loopWritePos;

			if (loopWritePos >= loopBufferLength)
			{
				// Buffer full (2 seconds captured) — switch to playing
				loopRecordedLength = loopBufferLength;
				loopState = LoopState::Playing;
				loopReadPos = 0.0f;
				smoothedLoopTimeSamples = loopTimeSamples;
			}

			if (channelL != nullptr) channelL[i] = inputL * smoothedOutputGain;
			if (channelR != nullptr) channelR[i] = inputR * smoothedOutputGain;
		}
		else if (loopState == LoopState::Playing)
		{
			smoothedLoopTimeSamples = smoothedLoopTimeSamples * smoothCoeff + loopTimeSamples * (1.0f - smoothCoeff);
			const float effectiveLoopLen = juce::jlimit (1.0f, (float) loopRecordedLength, smoothedLoopTimeSamples);

			// Linear interpolation read
			const float readF = loopReadPos;
			const int r0 = (int) readF;
			const int r1 = (r0 + 1 < loopRecordedLength) ? r0 + 1 : 0;
			const float frac = readF - (float) r0;

			float outL = loopL[r0] + frac * (loopL[r1] - loopL[r0]);
			float outR = loopR[r0] + frac * (loopR[r1] - loopR[r0]);

			// Crossfade at wrap point
			const float distToEnd = effectiveLoopLen - loopReadPos;
			if (distToEnd < (float) kLoopCrossfadeSamples && effectiveLoopLen > (float) (kLoopCrossfadeSamples * 2))
			{
				const float fade = distToEnd / (float) kLoopCrossfadeSamples;
				const float wrapReadF = (float) kLoopCrossfadeSamples - distToEnd;
				const int wr0 = (int) wrapReadF;
				const int wr1 = (wr0 + 1 < loopRecordedLength) ? wr0 + 1 : 0;
				const float wfrac = wrapReadF - (float) wr0;
				const float wrapL = loopL[wr0] + wfrac * (loopL[wr1] - loopL[wr0]);
				const float wrapR = loopR[wr0] + wfrac * (loopR[wr1] - loopR[wr0]);

				outL = outL * fade + wrapL * (1.0f - fade);
				outR = outR * fade + wrapR * (1.0f - fade);
			}

			outL = juce::jlimit (-2.0f, 2.0f, outL);
			outR = juce::jlimit (-2.0f, 2.0f, outR);

			if (channelL != nullptr) channelL[i] = (inputL * (1.0f - smoothedMix) + outL * smoothedMix) * smoothedOutputGain;
			if (channelR != nullptr) channelR[i] = (inputR * (1.0f - smoothedMix) + outR * smoothedMix) * smoothedOutputGain;

			loopReadPos += 1.0f;
			if (loopReadPos >= effectiveLoopLen)
				loopReadPos -= effectiveLoopLen;
		}
	}
}

void ECHOTRAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
	juce::ScopedNoDenormals noDenormals;

	const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
	const int numSamples = buffer.getNumSamples();
	
	// Process MIDI messages for note tracking
	const bool midiEnabled = loadBoolParamOrDefault (midiParam, false);
	const int selectedMidiPort = midiPort.load (std::memory_order_relaxed);
	
	if (midiEnabled)
	{
		for (const auto metadata : midiMessages)
		{
			const auto msg = metadata.getMessage();
			
			// Filter by MIDI channel (0 = omni)
			if (selectedMidiPort > 0 && msg.getChannel() != selectedMidiPort)
				continue;
			
			if (msg.isNoteOn())
			{
				const int noteNumber = msg.getNoteNumber();
				lastMidiNote.store (noteNumber, std::memory_order_relaxed);
				const float frequency = 440.0f * std::pow (2.0f, (noteNumber - 69) / 12.0f);
				currentMidiFrequency.store (frequency, std::memory_order_relaxed);
			}
			else if (msg.isNoteOff())
			{
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
		lastMidiNote.store (-1, std::memory_order_relaxed);
		currentMidiFrequency.store (0.0f, std::memory_order_relaxed);
	}

	for (int i = numChannels; i < buffer.getNumChannels(); ++i)
		buffer.clear (i, 0, numSamples);

	if (delayBufferLength == 0 || currentSampleRate <= 0.0)
		return;

	const bool loopEnabled = loadBoolParamOrDefault (loopParam, false);
	const bool syncEnabled = loadBoolParamOrDefault (syncParam, false);
	const bool midiNoteActive = midiEnabled && (lastMidiNote.load (std::memory_order_relaxed) >= 0);
	const float timeMsValue = loadAtomicOrDefault (timeMsParam, kTimeMsDefault);
	const int timeSyncValue = loadIntParamOrDefault (timeSyncParam, kTimeSyncDefault);
	const int mode = loadIntParamOrDefault (modeParam, 1);

	if (loopEnabled && loopState == LoopState::Off)
	{
		loopBuffer.clear();
		loopWritePos = 0;
		loopRecordedLength = 0;
		loopReadPos = 0.0f;
		loopState = LoopState::Recording;
	}
	else if (! loopEnabled && loopState != LoopState::Off)
	{
		loopState = LoopState::Off;
		loopBuffer.clear();
		loopRecordedLength = 0;
		loopWritePos = 0;
		loopReadPos = 0.0f;
		smoothedLoopTimeSamples = 0.0f;
	}
	
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
	
	float targetFeedback = loadAtomicOrDefault (feedbackParam, kFeedbackDefault);
	const bool autoFbkEnabled = loadBoolParamOrDefault (autoFbkParam, false);
	const float inputGainDb = loadAtomicOrDefault (inputParam, kInputDefault);
	const float outputGainDb = loadAtomicOrDefault (outputParam, kOutputDefault);
	const float mixValue = loadAtomicOrDefault (mixParam, kMixDefault);
	const float modValue = loadAtomicOrDefault (modParam, kModDefault);
	
	const float inputGain = juce::Decibels::decibelsToGain (inputGainDb);
	const float outputGain = juce::Decibels::decibelsToGain (outputGainDb);
	targetFeedback = juce::jlimit (0.0f, 0.99f, targetFeedback);
	
	// Auto feedback: exponential curve (x^4) + octave-based multiplier
	if (autoFbkEnabled && targetFeedback > 0.001f && targetDelayMs > 1.0f)
	{
		const float exponentialFeedback = std::pow (targetFeedback, 4.0f);
		const float referenceDelay = syncEnabled ? kTimeMsMaxSync : kTimeMsMax;
		const float octavesFromMax = std::log2 (referenceDelay / targetDelayMs);
		const float autoFbkMultiplier = std::pow (1.5f, octavesFromMax);
		targetFeedback = juce::jmin (0.99f, exponentialFeedback * autoFbkMultiplier);
	}
	
	// Calculate frequency multiplier from MOD (pitch bend effect)
	// MOD: frequency multiplier (0=÷4, 0.5=×1, 1.0=×4)
	float freqMultiplier;
	if (modValue < 0.5f)
		freqMultiplier = 0.25f + (modValue * 1.5f);
	else
		freqMultiplier = 1.0f + ((modValue - 0.5f) * 6.0f);
	
	float delaySamples = juce::jmax (0.0f, (float) currentSampleRate * (targetDelayMs / 1000.0f));
	delaySamples /= freqMultiplier;
	delaySamples = juce::jlimit (0.0f, (float) (delayBufferLength - 2), delaySamples);
	
	// Loop mode
	if (loopState != LoopState::Off)
	{
		// Calculate loop time in samples (same time source as delay, with MOD)
		float loopTimeSamples = juce::jmax (1.0f, (float) currentSampleRate * (targetDelayMs / 1000.0f));
		loopTimeSamples /= freqMultiplier;
		loopTimeSamples = juce::jlimit (1.0f, (float) loopBufferLength, loopTimeSamples);

		processLoop (buffer, numSamples, numChannels, loopTimeSamples, inputGain, outputGain, mixValue);
		return;
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
		return;
	}
	
	if (mode == 0)
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
	{
		if (xmlState->hasTagName (apvts.state.getType()))
		{
			apvts.replaceState (juce::ValueTree::fromXml (*xmlState));

			// Sync midiPort atomic from restored state so processBlock sees the correct value
			const auto restoredPort = apvts.state.getProperty (UiStateKeys::midiPort);
			if (! restoredPort.isVoid())
				midiPort.store ((int) restoredPort, std::memory_order_relaxed);
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
		juce::NormalisableRange<float> (kOutputMin, kOutputMax, 0.0f, 2.5f), kOutputDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamMix, "Mix",
		juce::NormalisableRange<float> (kMixMin, kMixMax, 0.0f, 1.0f), kMixDefault));

	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamSync, "Sync", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamMidi, "MIDI", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamAutoFbk, "Auto Fbk", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamLoop, "Loop", false));

	// UI state (hidden from automation)
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
