#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

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
	static constexpr const char* kParamReverse    = "reverse";
	
	// UI state parameters (hidden from DAW automation)
	static constexpr const char* kParamUiWidth    = "ui_width";
	static constexpr const char* kParamUiHeight   = "ui_height";
	static constexpr const char* kParamUiPalette  = "ui_palette";
	static constexpr const char* kParamUiFxTail   = "ui_fx_tail";
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

	static constexpr float kFeedbackMin = 0.0f;
	static constexpr float kFeedbackMax = 1.0f;
	static constexpr float kFeedbackDefault = 0.35f;

	static constexpr int kModeMin = 0;
	static constexpr int kModeMax = 2; // 0=MONO, 1=STEREO, 2=PING-PONG
	static constexpr float kModeDefault = 1.0f;

	static constexpr float kModMin = 0.0f;
	static constexpr float kModMax = 1.0f;
	static constexpr float kModDefault = 0.5f;

	static constexpr float kInputMin = -100.0f;
	static constexpr float kInputMax = 12.0f;
	static constexpr float kInputDefault = 0.0f;

	static constexpr float kOutputMin = -100.0f;
	static constexpr float kOutputMax = 12.0f;
	static constexpr float kOutputDefault = 0.0f;

	static constexpr float kMixMin = 0.0f;
	static constexpr float kMixMax = 1.0f;
	static constexpr float kMixDefault = 1.0f;

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

	// Reverse delay processing (chunk-based dual read with crossfade)
	void processReverseDelay (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels,
	                          float delaySamples, float feedback, float inputGain,
	                          float outputGain, float mix, float delaySmoothCoeff);

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

	void setUiFxTailEnabled (bool shouldEnableFxTail);
	bool getUiFxTailEnabled() const noexcept;

	void setMidiChannel (int channel);
	int getMidiChannel() const noexcept;

	void setUiCustomPaletteColour (int index, juce::Colour colour);
	juce::Colour getUiCustomPaletteColour (int index) const noexcept;

	juce::AudioProcessorValueTreeState apvts;
	static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
	struct UiStateKeys
	{
		static constexpr const char* editorWidth = "uiEditorWidth";
		static constexpr const char* editorHeight = "uiEditorHeight";
		static constexpr const char* useCustomPalette = "uiUseCustomPalette";
		static constexpr const char* fxTailEnabled = "uiFxTailEnabled";
		static constexpr const char* midiPort = "midiPort";
		static constexpr std::array<const char*, 2> customPalette {
			"uiCustomPalette0", "uiCustomPalette1"
		};
	};

	double currentSampleRate = 44100.0;
	
	juce::AudioBuffer<float> delayBuffer;
	int delayBufferWritePos = 0;
	int delayBufferLength = 0;
	float smoothedDelaySamples = 0.0f;
	float smoothedInputGain = 1.0f;
	float smoothedOutputGain = 1.0f;
	float smoothedMix = 0.5f;
	std::array<float, 2> feedbackState { 0.0f, 0.0f };

	// Single-buffer reverse delay state.
	// One read head reads BACKWARDS through the main delay buffer.
	// A Tukey cosine taper at chunk edges prevents clicks.
	// EMA-smoothed delay determines chunk length → when delay changes,
	// chunks end sooner/later → tape-speed pitch shift, same as forward.
	int   reverseAnchor     = 0;      // writePos snapshot at chunk start
	float reverseCounter    = 0.0f;   // position within chunk (0 → chunkLen)
	float revSmoothedDelay  = 0.0f;   // EMA-smoothed delay for reverse (independent)
	bool  reverseNeedsInit  = true;   // first-call initialisation flag


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
	std::atomic<float>* reverseParam = nullptr;
	
	std::atomic<float>* uiWidthParam = nullptr;
	std::atomic<float>* uiHeightParam = nullptr;
	std::atomic<float>* uiPaletteParam = nullptr;
	std::atomic<float>* uiFxTailParam = nullptr;
	std::array<std::atomic<float>*, 2> uiColorParams { nullptr, nullptr };

	// UI state atomics
	std::atomic<int> uiEditorWidth { 360 };
	std::atomic<int> uiEditorHeight { 480 };
	std::atomic<int> uiUseCustomPalette { 0 };
	std::atomic<int> uiFxTailEnabled { 0 };
	std::array<std::atomic<juce::uint32>, 2> uiCustomPalette {
		std::atomic<juce::uint32> { juce::Colours::white.getARGB() },
		std::atomic<juce::uint32> { juce::Colours::black.getARGB() }
	};

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ECHOTRAudioProcessor)
};
