#pragma once

#include <cstdint>
#include <atomic>
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "CrtEffect.h"
#include "TRSharedUI.h"

class ECHOTRAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                     private juce::Slider::Listener,
                                     private juce::AudioProcessorValueTreeState::Listener,
                                     private juce::Timer
{
public:
    explicit ECHOTRAudioProcessorEditor (ECHOTRAudioProcessor&);
    ~ECHOTRAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void moved() override;
    void parentHierarchyChanged() override;


private:
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;

    void openNumericEntryPopupForSlider (juce::Slider& s);
    void openMidiChannelPrompt();
    void openAutoFbkPrompt();
    void openReverseSmoothPrompt();
    void openFilterPrompt();
    void openChaosConfigPrompt (const char* amtParamId, const char* spdParamId, const juce::String& title);
    void openChaosFilterPrompt();
    void openChaosDelayPrompt();
    void openInfoPopup();
    void openGraphicsPopup();
    void setPromptOverlayActive (bool shouldBeActive);

    ECHOTRAudioProcessor& audioProcessor;

    class BarSlider : public juce::Slider
    {
    public:
        using juce::Slider::Slider;

        void setOwner (ECHOTRAudioProcessorEditor* o) { owner = o; }
        
        void setAllowNumericPopup (bool allow) { allowNumericPopup = allow; }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && allowNumericPopup)
            {
                if (owner != nullptr)
                    owner->openNumericEntryPopupForSlider (*this);
                return;
            }

            juce::Slider::mouseDown (e);
        }

        juce::String getTextFromValue (double v) override
        {
            // For feedback and mix (0-1 range)
            if (owner != nullptr && (this == &owner->feedbackSlider || this == &owner->mixSlider))
            {
                double percent = v * 100.0;
                juce::String t (percent, 4);
                if (t.containsChar ('.'))
                {
                    while (t.endsWithChar ('0'))
                        t = t.dropLastCharacters (1);
                    if (t.endsWithChar ('.'))
                        t = t.dropLastCharacters (1);
                }
                return t;
            }

            // For time_ms (0-2000 ms)
            if (owner != nullptr && this == &owner->timeSlider)
            {
                const double rounded2 = std::round (v * 100.0) / 100.0;
                return juce::String (rounded2, 2);
            }

            // For input/output gain
            if (owner != nullptr && (this == &owner->inputSlider || this == &owner->outputSlider))
            {
                if (this == &owner->inputSlider && v <= -80.0)
                    return "-INF";
                const double rounded1 = std::round (v * 10.0) / 10.0;
                return juce::String (rounded1, 1);
            }

            juce::String t = juce::Slider::getTextFromValue (v);
            int dot = t.indexOfChar ('.');
            if (dot >= 0)
                t = t.substring (0, dot + 1 + 4);
            return t;
        }

    private:
        ECHOTRAudioProcessorEditor* owner = nullptr;
        bool allowNumericPopup = true;
    };

    BarSlider timeSlider;
    BarSlider feedbackSlider;
    BarSlider modeSlider;
    BarSlider modSlider;
    BarSlider engineSlider;
    BarSlider inputSlider;
    BarSlider outputSlider;
    BarSlider tiltSlider;
    BarSlider mixSlider;

    juce::ToggleButton syncButton;
    juce::ToggleButton midiButton;
    juce::ToggleButton autoFbkButton;
    juce::ToggleButton reverseButton;
    juce::ToggleButton chaosFilterButton;
    juce::ToggleButton chaosDelayButton;

    juce::Label midiChannelDisplay;
    juce::Label autoFbkDisplay;
    juce::Label reverseDisplay;
    juce::Label chaosFilterDisplay;
    juce::Label chaosDelayDisplay;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> timeAttachment;
    std::unique_ptr<SliderAttachment> timeSyncAttachment;  // For tempo sync mode
    std::unique_ptr<SliderAttachment> feedbackAttachment;
    std::unique_ptr<SliderAttachment> modeAttachment;
    std::unique_ptr<SliderAttachment> modAttachment;
    std::unique_ptr<SliderAttachment> engineAttachment;
    std::unique_ptr<SliderAttachment> inputAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::unique_ptr<SliderAttachment> tiltAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;

    std::unique_ptr<ButtonAttachment> syncAttachment;
    std::unique_ptr<ButtonAttachment> midiAttachment;
    std::unique_ptr<ButtonAttachment> autoFbkAttachment;
    std::unique_ptr<ButtonAttachment> reverseAttachment;
    std::unique_ptr<ButtonAttachment> chaosFilterAttachment;
    std::unique_ptr<ButtonAttachment> chaosDelayAttachment;

    juce::ComponentBoundsConstrainer resizeConstrainer;
    std::unique_ptr<juce::ResizableCornerComponent> resizerCorner;

    using ECHOScheme = TR::TRScheme;

    ECHOScheme activeScheme;

    struct HorizontalLayoutMetrics
    {
        int barW = 0;
        int valuePad = 0;
        int valueW = 0;
        int contentW = 0;
        int leftX = 0;
    };

    struct VerticalLayoutMetrics
    {
        int rhythm = 0;
        int titleH = 0;
        int titleAreaH = 0;
        int titleTopPad = 0;
        int topMargin = 0;
        int betweenSlidersAndButtons = 0;
        int bottomMargin = 0;
        int box = 0;
        int btnRow1Y = 0;
        int btnRow2Y = 0;
        int btnRowGap = 0;
        int availableForSliders = 0;
        int barH = 0;
        int gapY = 0;
        int topY = 0;
        int toggleBarH = 0;
        int toggleBarY = 0;
    };

    static HorizontalLayoutMetrics buildHorizontalLayout (int editorW, int valueColW);
    static VerticalLayoutMetrics buildVerticalLayout (int editorH, int biasY, bool ioExpanded);
    void updateCachedLayout();

    class MinimalLNF : public juce::LookAndFeel_V4
    {
    public:
        void setScheme (const ECHOScheme& s)
        {
            scheme = s;
            TR::applySchemeToLookAndFeel (*this, scheme);
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               const juce::Slider::SliderStyle style, juce::Slider& slider) override;

        void drawTickBox (juce::Graphics& g, juce::Component&,
                          float x, float y, float w, float h,
                          bool ticked, bool isEnabled,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

        void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

        void drawButtonBackground (juce::Graphics& g,
                       juce::Button& button,
                       const juce::Colour& backgroundColour,
                       bool shouldDrawButtonAsHighlighted,
                       bool shouldDrawButtonAsDown) override;

        void drawAlertBox (juce::Graphics& g,
                   juce::AlertWindow& alert,
                   const juce::Rectangle<int>& textArea,
                   juce::TextLayout& textLayout) override;

        void drawBubble (juce::Graphics&,
                 juce::BubbleComponent&,
                 const juce::Point<float>& tip,
                 const juce::Rectangle<float>& body) override;

        void drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar,
                    int x, int y, int width, int height,
                    bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                    bool isMouseOver, bool isMouseDown) override;

        int getMinimumScrollbarThumbSize (juce::ScrollBar&) override { return 16; }
        int getScrollbarButtonSize (juce::ScrollBar&) override      { return 0; }  // no arrow buttons

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        juce::Font getAlertWindowMessageFont() override;
        juce::Font getLabelFont (juce::Label& label) override;
        juce::Font getSliderPopupFont (juce::Slider&) override;
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText,
                               juce::Point<int> screenPos,
                               juce::Rectangle<int> parentArea) override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    private:
        ECHOScheme scheme {
            juce::Colours::black,
            juce::Colours::white,
            juce::Colours::white,
            juce::Colours::white
        };
    };

    // ── Wet-signal filter frequency bar component ──
    class FilterBarComponent : public juce::Component,
                               public juce::SettableTooltipClient
    {
    public:
        FilterBarComponent() = default;
        void setOwner (ECHOTRAudioProcessorEditor* o) { owner = o; }
        void setScheme (const ECHOScheme& s) { scheme = s; repaint(); }

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;

        void updateFromProcessor();

        float getHpFreq() const { return hpFreq_; }
        float getLpFreq() const { return lpFreq_; }
        bool  isHpOn()    const { return hpOn_; }
        bool  isLpOn()    const { return lpOn_; }

    private:
        ECHOTRAudioProcessorEditor* owner = nullptr;
        ECHOScheme scheme {};

        float hpFreq_ = 250.0f;
        float lpFreq_ = 2000.0f;
        bool  hpOn_   = false;
        bool  lpOn_   = false;

        enum DragTarget { None, HP, LP };
        DragTarget currentDrag_ = None;

        static constexpr float kMinFreq = 20.0f;
        static constexpr float kMaxFreq = 20000.0f;
        static constexpr float kPad     = 7.0f;
        static constexpr int   kMarkerHitPx = 10;

        juce::Rectangle<float> getInnerArea() const;
        float freqToNormX (float freq) const;
        float normXToFreq (float normX) const;
        float getMarkerScreenX (float freq) const;
        DragTarget hitTestMarker (juce::Point<float> p) const;
        void  setFreqFromMouseX (float mouseX, DragTarget target);
    };

    FilterBarComponent filterBar_;

    using PromptOverlay = TR::PromptOverlay;

    MinimalLNF lnf;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    PromptOverlay promptOverlay;

    void setupBar (juce::Slider& s);

    juce::String getTimeText() const;
    juce::String getTimeTextShort() const;
    
    juce::String getFeedbackText() const;
    juce::String getFeedbackTextShort() const;

    juce::String getModeText() const;
    juce::String getModeTextShort() const;

    juce::String getEngineText() const;
    juce::String getEngineTextShort() const;

    juce::String getModText() const;
    juce::String getModTextShort() const;

    juce::String getInputText() const;
    juce::String getInputTextShort() const;

    juce::String getOutputText() const;
    juce::String getOutputTextShort() const;

    juce::String getMixText() const;
    juce::String getMixTextShort() const;

    juce::String getTiltText() const;
    juce::String getTiltTextShort() const;

    int getTargetValueColumnWidth() const;

    void sliderValueChanged (juce::Slider* slider) override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;

    void applyPersistedUiStateFromProcessor (bool applySize, bool applyPaletteAndFx);
    void applyLabelTextColour (juce::Label& label, juce::Colour colour);
    
    void updateTimeSliderForSyncMode (bool syncEnabled);

    template <typename T>
    friend void TR::embedAlertWindowInOverlay (T*, juce::AlertWindow*, bool);

    juce::Rectangle<int> getValueAreaFor (const juce::Rectangle<int>& barBounds) const;
    juce::Slider* getSliderForValueAreaPoint (juce::Point<int> p);
    juce::Rectangle<int> getSyncLabelArea() const;
    juce::Rectangle<int> getAutoFbkLabelArea() const;
    juce::Rectangle<int> getReverseLabelArea() const;
    juce::Rectangle<int> getMidiLabelArea() const;
    juce::Rectangle<int> getChaosLabelArea() const;
    juce::Rectangle<int> getInfoIconArea() const;
    void updateInfoIconCache();
    bool refreshLegendTextCache();
    juce::Rectangle<int> getRowRepaintBounds (const juce::Slider& s) const;
    void applyActivePalette();
    void applyCrtState (bool enabled);

    juce::Path cachedInfoGearPath;
    juce::Rectangle<float> cachedInfoGearHole;
    
    juce::String cachedTimeTextFull;
    juce::String cachedTimeTextShort;
    juce::String cachedFeedbackTextFull;
    juce::String cachedFeedbackTextShort;
    juce::String cachedModeTextFull;
    juce::String cachedModeTextShort;
    juce::String cachedEngineTextFull;
    juce::String cachedEngineTextShort;
    juce::String cachedModTextFull;
    juce::String cachedModTextShort;
    juce::String cachedInputTextFull;
    juce::String cachedInputTextShort;
    juce::String cachedOutputTextFull;
    juce::String cachedOutputTextShort;
    juce::String cachedMixTextFull;
    juce::String cachedMixTextShort;
    juce::String cachedTiltTextFull;
    juce::String cachedTiltTextShort;

    juce::String cachedTimeIntOnly;
    juce::String cachedFeedbackIntOnly;
    juce::String cachedModeIntOnly;
    juce::String cachedEngineIntOnly;
    juce::String cachedModIntOnly;
    juce::String cachedInputIntOnly;
    juce::String cachedOutputIntOnly;
    juce::String cachedMixIntOnly;
    juce::String cachedTiltIntOnly;

    juce::String cachedFilterTextFull;
    juce::String cachedFilterTextShort;
    
    juce::String cachedMidiDisplay;
    bool cachedTimeSliderHeld = false;
    
    mutable std::uint64_t cachedValueColumnWidthKey = 0;
    mutable int cachedValueColumnWidth = 90;

    HorizontalLayoutMetrics cachedHLayout_;
    VerticalLayoutMetrics cachedVLayout_;
    std::array<juce::Rectangle<int>, 9> cachedValueAreas_;
    juce::Rectangle<int> cachedFilterValueArea_;
    juce::Rectangle<int> cachedTiltValueArea_;
    juce::Rectangle<int> cachedToggleBarArea_;
    juce::Rectangle<int> cachedChaosArea_;
    bool ioSectionExpanded_ = false;

    static constexpr double kDefaultTimeMs = (double) ECHOTRAudioProcessor::kTimeMsDefault;
    static constexpr double kDefaultFeedback = (double) ECHOTRAudioProcessor::kFeedbackDefault;
    static constexpr double kDefaultMix = (double) ECHOTRAudioProcessor::kMixDefault;
    static constexpr double kDefaultInput = (double) ECHOTRAudioProcessor::kInputDefault;
    static constexpr double kDefaultOutput = (double) ECHOTRAudioProcessor::kOutputDefault;
    static constexpr double kDefaultTilt = (double) ECHOTRAudioProcessor::kTiltDefault;

    static constexpr int kMinW = 360;
    static constexpr int kMinH = 540;
    static constexpr int kMaxW = 800;
    static constexpr int kMaxH = 640;

    static constexpr int kLayoutVerticalBiasPx = 10;

    bool promptOverlayActive = false;
    bool suppressSizePersistence = false;
    int lastPersistedEditorW = -1;
    int lastPersistedEditorH = -1;
    std::atomic<uint32_t> lastUserInteractionMs { 0 };
    static constexpr uint32_t kUserInteractionPersistWindowMs = 5000;
    bool crtEnabled = false;
    bool useCustomPalette = false;

    // CRT post-process effect (Retro-Windows-Terminal shader on CPU)
    CrtEffect crtEffect;
    float     crtTime = 0.0f;

    std::array<juce::Colour, 2> defaultPalette {
        juce::Colours::white,
        juce::Colours::black
    };
    std::array<juce::Colour, 2> customPalette {
        juce::Colours::white,
        juce::Colours::black
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ECHOTRAudioProcessorEditor)
};
