// PluginEditor.cpp
#include "PluginEditor.h"
#include "InfoContent.h"
#include <functional>

using namespace TR;

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace UiStateKeys
{
    constexpr const char* editorWidth = "uiEditorWidth";
    constexpr const char* editorHeight = "uiEditorHeight";
    constexpr const char* useCustomPalette = "uiUseCustomPalette";
    constexpr const char* crtEnabled = "uiFxTailEnabled";  // string kept for preset compat
    constexpr std::array<const char*, 2> customPalette {
        "uiCustomPalette0",
        "uiCustomPalette1"
    };
}

// ── Timer & display constants ──
static constexpr int   kCrtTimerHz   = 10;
static constexpr int   kIdleTimerHz  = 4;
static constexpr float kSilenceDb    = -80.0f;
static constexpr float kMultEpsilon  = 0.01f;

// ── Mod slider ↔ multiplier conversion ──
static constexpr double kModCenter  = 0.5;
static constexpr double kModScale   = 3.0;
static constexpr double kModMaxMult = 4.0;
static constexpr double kModMinMult = 0.25;

static double modSliderToMultiplier (double v)
{
    if (v < kModCenter)
        return 1.0 / (kModMaxMult - kModScale * (v / kModCenter));
    return 1.0 + kModScale * ((v - kModCenter) / kModCenter);
}

static double multiplierToModSlider (double mult)
{
    mult = juce::jlimit (kModMinMult, kModMaxMult, mult);
    if (mult < 1.0)
        return (kModMaxMult - 1.0 / mult) * kModCenter / kModScale;
    return kModCenter + (mult - 1.0) * kModCenter / kModScale;
}

// ── MIDI channel tooltip ──
static juce::String formatMidiChannelTooltip (int ch)
{
    return "CHANNEL " + juce::String (ch);
}

// ── Auto-feedback tooltip ──
static juce::String formatAutoFbkTooltip (float tauPct, float attPct)
{
    return juce::String (juce::roundToInt (tauPct)) + "% | "
         + juce::String (juce::roundToInt (attPct)) + "%";
}

static juce::String formatReverseSmoothTooltip (float smoothExp)
{
    const float mult = std::exp2 (smoothExp);
    return "x" + juce::String (mult, 2);
}

// ── Parameter listener IDs (shared by ctor + dtor) ──
static constexpr std::array<const char*, 5> kUiMirrorParamIds {
    ECHOTRAudioProcessor::kParamSync,
    ECHOTRAudioProcessor::kParamUiPalette,
    ECHOTRAudioProcessor::kParamUiCrt,
    ECHOTRAudioProcessor::kParamUiColor0,
    ECHOTRAudioProcessor::kParamUiColor1
};

//========================== LookAndFeel ==========================

void ECHOTRAudioProcessorEditor::MinimalLNF::drawLinearSlider (juce::Graphics& g,
                                                                  int x, int y, int width, int height,
                                                                  float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                                                                  const juce::Slider::SliderStyle /*style*/, juce::Slider& /*slider*/)
{
    const juce::Rectangle<float> r ((float) x, (float) y, (float) width, (float) height);

    g.setColour (scheme.outline);
    g.drawRect (r, 4.0f);

    const float pad = 7.0f;
    auto inner = r.reduced (pad);

    g.setColour (scheme.bg);
        g.fillRect (inner);

    const float fillW = juce::jlimit (0.0f, inner.getWidth(), sliderPos - inner.getX());
    auto fill = inner.withWidth (fillW);

    g.setColour (scheme.fg);
    g.fillRect (fill);
}

void ECHOTRAudioProcessorEditor::MinimalLNF::drawTickBox (juce::Graphics& g, juce::Component& button,
                                                            float x, float y, float w, float h,
                                                            bool ticked, bool /*isEnabled*/,
                                                            bool /*highlighted*/, bool /*down*/)
{
    juce::ignoreUnused (x, y, w, h);

    const auto local = button.getLocalBounds().toFloat().reduced (1.0f);
    const float side = juce::jlimit (14.0f,
                                     juce::jmax (14.0f, local.getHeight() - 2.0f),
                                     std::round (local.getHeight() * 0.65f));

    auto r = juce::Rectangle<float> (local.getX() + 2.0f,
                                     local.getCentreY() - (side * 0.5f),
                                     side,
                                     side).getIntersection (local);

    g.setColour (scheme.outline);
    g.drawRect (r, 4.0f);

    const float innerInset = juce::jlimit (1.0f, side * 0.45f, side * UiMetrics::tickBoxInnerInsetRatio);
    auto inner = r.reduced (innerInset);

    if (ticked)
    {
        g.setColour (scheme.fg);
        g.fillRect (inner);
    }
    else
    {
        g.setColour (scheme.bg);
        g.fillRect (inner);
    }
}

void ECHOTRAudioProcessorEditor::MinimalLNF::drawButtonBackground (juce::Graphics& g,
                                                                      juce::Button& button,
                                                                      const juce::Colour& backgroundColour,
                                                                      bool shouldDrawButtonAsHighlighted,
                                                                      bool shouldDrawButtonAsDown)
{
    auto r = button.getLocalBounds();

    auto fill = backgroundColour;
    if (shouldDrawButtonAsDown)
        fill = fill.brighter (0.12f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter (0.06f);

    g.setColour (fill);
    g.fillRect (r);

    g.setColour (scheme.outline);
    g.drawRect (r.reduced (1), 3);
}

void ECHOTRAudioProcessorEditor::MinimalLNF::drawAlertBox (juce::Graphics& g,
                                                              juce::AlertWindow& alert,
                                                              const juce::Rectangle<int>& textArea,
                                                              juce::TextLayout& textLayout)
{
    auto bounds = alert.getLocalBounds();

    g.setColour (scheme.bg);
    g.fillRect (bounds);

    g.setColour (scheme.outline);
    g.drawRect (bounds.reduced (1), 3);

    g.setColour (scheme.text);
    textLayout.draw (g, textArea.toFloat());
}

void ECHOTRAudioProcessorEditor::MinimalLNF::drawBubble (juce::Graphics& g,
                                                            juce::BubbleComponent&,
                                                            const juce::Point<float>&,
                                                            const juce::Rectangle<float>& body)
{
    drawOverlayPanel (g,
                      body.getSmallestIntegerContainer(),
                      findColour (juce::TooltipWindow::backgroundColourId),
                      findColour (juce::TooltipWindow::outlineColourId));
}

void ECHOTRAudioProcessorEditor::MinimalLNF::drawScrollbar (juce::Graphics& g,
                                                             juce::ScrollBar&,
                                                             int x, int y, int width, int height,
                                                             bool isScrollbarVertical,
                                                             int thumbStartPosition, int thumbSize,
                                                             bool isMouseOver, bool isMouseDown)
{
    juce::ignoreUnused (x, y, width, height);

    const auto thumbColour = scheme.text.withAlpha (isMouseDown ? 0.7f
                                                     : isMouseOver ? 0.5f
                                                                   : 0.3f);
    constexpr float barThickness = 7.0f;
    constexpr float cornerRadius = 3.5f;

    if (isScrollbarVertical)
    {
        const float bx = (float) (x + width) - barThickness - 1.0f;
        g.setColour (thumbColour);
        g.fillRoundedRectangle (bx, (float) thumbStartPosition,
                                barThickness, (float) thumbSize, cornerRadius);
    }
    else
    {
        const float by = (float) (y + height) - barThickness - 1.0f;
        g.setColour (thumbColour);
        g.fillRoundedRectangle ((float) thumbStartPosition, by,
                                (float) thumbSize, barThickness, cornerRadius);
    }
}

juce::Font ECHOTRAudioProcessorEditor::MinimalLNF::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    const float h = juce::jlimit (12.0f, 26.0f, buttonHeight * 0.48f);
    return juce::Font (juce::FontOptions (h).withStyle ("Bold"));
}

juce::Font ECHOTRAudioProcessorEditor::MinimalLNF::getAlertWindowMessageFont()
{
    auto f = juce::LookAndFeel_V4::getAlertWindowMessageFont();
    f.setBold (true);
    return f;
}

juce::Font ECHOTRAudioProcessorEditor::MinimalLNF::getLabelFont (juce::Label& label)
{
    auto f = label.getFont();
    if (f.getHeight() <= 0.0f)
    {
        const float h = juce::jlimit (12.0f, 40.0f, (float) juce::jmax (12, label.getHeight() - 6));
        f = juce::Font (juce::FontOptions (h).withStyle ("Bold"));
    }
    else
    {
        f.setBold (true);
    }

    return f;
}

juce::Font ECHOTRAudioProcessorEditor::MinimalLNF::getSliderPopupFont (juce::Slider&)
{
    return makeOverlayDisplayFont();
}

juce::Rectangle<int> ECHOTRAudioProcessorEditor::MinimalLNF::getTooltipBounds (const juce::String& tipText,
                                                                                   juce::Point<int> screenPos,
                                                                                   juce::Rectangle<int> parentArea)
{
    const auto f = makeOverlayDisplayFont();
    const int h = juce::jmax (UiMetrics::tooltipMinHeight,
                              (int) std::ceil (f.getHeight() * UiMetrics::tooltipHeightScale));

    const int anchorOffsetX = juce::jmax (8, (int) std::round ((double) h * UiMetrics::tooltipAnchorXRatio));
    const int anchorOffsetY = juce::jmax (10, (int) std::round ((double) h * UiMetrics::tooltipAnchorYRatio));
    const int parentMargin = juce::jmax (2, (int) std::round ((double) h * UiMetrics::tooltipParentMarginRatio));
    const int widthPad = juce::jmax (16, (int) std::round (f.getHeight() * UiMetrics::tooltipWidthPadFontRatio));

    const int w = juce::jmax (UiMetrics::tooltipMinWidth, stringWidth (f, tipText) + widthPad);
    auto r = juce::Rectangle<int> (screenPos.x + anchorOffsetX, screenPos.y + anchorOffsetY, w, h);
    return r.constrainedWithin (parentArea.reduced (parentMargin));
}

void ECHOTRAudioProcessorEditor::MinimalLNF::drawTooltip (juce::Graphics& g,
                                                              const juce::String& text,
                                                              int width,
                                                              int height)
{
    const auto f = makeOverlayDisplayFont();
    const int h = juce::jmax (UiMetrics::tooltipMinHeight,
                              (int) std::ceil (f.getHeight() * UiMetrics::tooltipHeightScale));
    const int textInsetX = juce::jmax (4, (int) std::round ((double) h * UiMetrics::tooltipTextInsetXRatio));
    const int textInsetY = juce::jmax (1, (int) std::round ((double) h * UiMetrics::tooltipTextInsetYRatio));

    drawOverlayPanel (g,
                      { 0, 0, width, height },
                      findColour (juce::TooltipWindow::backgroundColourId),
                      findColour (juce::TooltipWindow::outlineColourId));

    g.setColour (findColour (juce::TooltipWindow::textColourId));
    g.setFont (f);
    g.drawFittedText (text,
                      textInsetX,
                      textInsetY,
                      juce::jmax (1, width - (textInsetX * 2)),
                      juce::jmax (1, height - (textInsetY * 2)),
                      juce::Justification::centred,
                      1);
}

//========================== Editor ==========================

ECHOTRAudioProcessorEditor::ECHOTRAudioProcessorEditor (ECHOTRAudioProcessor& p)
: AudioProcessorEditor (&p), audioProcessor (p)
{
    const std::array<BarSlider*, 7> barSliders { &timeSlider, &modSlider, &feedbackSlider, &modeSlider, &inputSlider, &outputSlider, &mixSlider };

    useCustomPalette = audioProcessor.getUiUseCustomPalette();
    crtEnabled = audioProcessor.getUiCrtEnabled();
    ioSectionExpanded_ = audioProcessor.getUiIoExpanded();

    for (int i = 0; i < 2; ++i)
        customPalette[(size_t) i] = audioProcessor.getUiCustomPaletteColour (i);

    setOpaque (true);
    setBufferedToImage (true);   // keep cached frame visible during live resize

    applyActivePalette();
    setLookAndFeel (&lnf);
    tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 250);
    tooltipWindow->setLookAndFeel (&lnf);
    tooltipWindow->setAlwaysOnTop (true);
    tooltipWindow->setInterceptsMouseClicks (false, false);  // let mouse pass through (like desktop mode)

    setResizable (true, true);

    // Para que el host/JUCE clipee de verdad
    setResizeLimits (kMinW, kMinH, kMaxW, kMaxH);

    resizeConstrainer.setMinimumSize (kMinW, kMinH);
    resizeConstrainer.setMaximumSize (kMaxW, kMaxH);

    resizerCorner = std::make_unique<juce::ResizableCornerComponent> (this, &resizeConstrainer);
    addAndMakeVisible (*resizerCorner);
    resizerCorner->addMouseListener (this, true);

    addAndMakeVisible (promptOverlay);
    promptOverlay.setInterceptsMouseClicks (true, true);
    promptOverlay.setVisible (false);

    const int restoredW = juce::jlimit (kMinW, kMaxW, audioProcessor.getUiEditorWidth());
    const int restoredH = juce::jlimit (kMinH, kMaxH, audioProcessor.getUiEditorHeight());
    suppressSizePersistence = true;
    setSize (restoredW, restoredH);
    suppressSizePersistence = false;
    lastPersistedEditorW = restoredW;
    lastPersistedEditorH = restoredH;

    // ctor
    for (auto* slider : barSliders)
    {
        slider->setOwner (this);
        setupBar (*slider);
        addAndMakeVisible (*slider);
        slider->addListener (this);
    }

    timeSlider.setNumDecimalPlacesToDisplay (1);
    feedbackSlider.setNumDecimalPlacesToDisplay (1);
    modeSlider.setNumDecimalPlacesToDisplay (0);
    modSlider.setNumDecimalPlacesToDisplay (2);
    inputSlider.setNumDecimalPlacesToDisplay (1);
    outputSlider.setNumDecimalPlacesToDisplay (1);
    mixSlider.setNumDecimalPlacesToDisplay (1);

    // IO sliders start hidden (collapsible section, collapsed by default)
    inputSlider.setVisible (false);
    outputSlider.setVisible (false);
    mixSlider.setVisible (false);

    syncButton.setButtonText ("");
    autoFbkButton.setButtonText ("");
    midiButton.setButtonText ("");
    reverseButton.setButtonText ("");

    addAndMakeVisible (syncButton);
    addAndMakeVisible (autoFbkButton);
    addAndMakeVisible (midiButton);
    addAndMakeVisible (reverseButton);

    // MIDI channel tooltip overlay — invisible label positioned over the MIDI legend.
    // Provides tooltip on hover; clicks forwarded to editor via addMouseListener.
    const int savedChannel = audioProcessor.getMidiChannel();
    midiChannelDisplay.setText ("", juce::dontSendNotification);
    midiChannelDisplay.setInterceptsMouseClicks (true, false);
    midiChannelDisplay.addMouseListener (this, false);
    midiChannelDisplay.setTooltip (formatMidiChannelTooltip (savedChannel));
    midiChannelDisplay.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    midiChannelDisplay.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    midiChannelDisplay.setOpaque (false);
    addAndMakeVisible (midiChannelDisplay);

    // Auto-feedback tooltip overlay — invisible label positioned over the AUTO FBK legend.
    // Provides tooltip on hover; right-click forwarded to editor opens the config prompt.
    {
        const float savedTau = audioProcessor.apvts.getRawParameterValue (ECHOTRAudioProcessor::kParamAutoFbkTau)->load();
        const float savedAtt = audioProcessor.apvts.getRawParameterValue (ECHOTRAudioProcessor::kParamAutoFbkAtt)->load();
        autoFbkDisplay.setText ("", juce::dontSendNotification);
        autoFbkDisplay.setInterceptsMouseClicks (true, false);
        autoFbkDisplay.addMouseListener (this, false);
        autoFbkDisplay.setTooltip (formatAutoFbkTooltip (savedTau, savedAtt));
        autoFbkDisplay.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        autoFbkDisplay.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        autoFbkDisplay.setOpaque (false);
        addAndMakeVisible (autoFbkDisplay);
    }

    // Reverse smooth tooltip overlay — invisible label positioned over the REVERSE legend.
    // Provides tooltip on hover; right-click forwarded to editor opens the smooth prompt.
    {
        const float savedSmooth = audioProcessor.apvts.getRawParameterValue (ECHOTRAudioProcessor::kParamReverseSmooth)->load();
        reverseDisplay.setText ("", juce::dontSendNotification);
        reverseDisplay.setInterceptsMouseClicks (true, false);
        reverseDisplay.addMouseListener (this, false);
        reverseDisplay.setTooltip (formatReverseSmoothTooltip (savedSmooth));
        reverseDisplay.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        reverseDisplay.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        reverseDisplay.setOpaque (false);
        addAndMakeVisible (reverseDisplay);
    }

    auto bindSlider = [&] (std::unique_ptr<SliderAttachment>& attachment,
                           const char* paramId,
                           BarSlider& slider,
                           double defaultValue)
    {
        attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, paramId, slider);
        slider.setDoubleClickReturnValue (true, defaultValue);
    };

    // Create both time attachments but only one will be active at a time
    const bool syncEnabled = audioProcessor.apvts.getRawParameterValue (ECHOTRAudioProcessor::kParamSync)->load() > 0.5f;
    if (syncEnabled)
    {
        bindSlider (timeSyncAttachment, ECHOTRAudioProcessor::kParamTimeSync, timeSlider, (double) ECHOTRAudioProcessor::kTimeSyncDefault);
        timeSlider.setRange (0.0, 29.0, 1.0);  // 30 divisions
    }
    else
    {
        bindSlider (timeAttachment, ECHOTRAudioProcessor::kParamTimeMs, timeSlider, kDefaultTimeMs);
    }
    
    bindSlider (feedbackAttachment, ECHOTRAudioProcessor::kParamFeedback, feedbackSlider, kDefaultFeedback);
    bindSlider (modeAttachment, ECHOTRAudioProcessor::kParamMode, modeSlider, 0.0);
    bindSlider (modAttachment, ECHOTRAudioProcessor::kParamMod, modSlider, 0.5);
    bindSlider (inputAttachment, ECHOTRAudioProcessor::kParamInput, inputSlider, kDefaultInput);
    bindSlider (outputAttachment, ECHOTRAudioProcessor::kParamOutput, outputSlider, kDefaultOutput);
    bindSlider (mixAttachment, ECHOTRAudioProcessor::kParamMix, mixSlider, kDefaultMix);

    // Disable numeric popup for STYLE (slider-only operation)
    modeSlider.setAllowNumericPopup (false);

    auto bindButton = [&] (std::unique_ptr<ButtonAttachment>& attachment,
                           const char* paramId,
                           juce::Button& button)
    {
        attachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, paramId, button);
    };

    bindButton (syncAttachment, ECHOTRAudioProcessor::kParamSync, syncButton);
    bindButton (autoFbkAttachment, ECHOTRAudioProcessor::kParamAutoFbk, autoFbkButton);
    bindButton (midiAttachment, ECHOTRAudioProcessor::kParamMidi, midiButton);
    bindButton (reverseAttachment, ECHOTRAudioProcessor::kParamReverse, reverseButton);

    for (auto* paramId : kUiMirrorParamIds)
        audioProcessor.apvts.addParameterListener (paramId, this);

    juce::Component::SafePointer<ECHOTRAudioProcessorEditor> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]()
    {
        if (safeThis == nullptr)
            return;

        safeThis->applyPersistedUiStateFromProcessor (true, true);
    });

    // Re-apply persisted UI size after short delays to override late host resizes.
    juce::Timer::callAfterDelay (250, [safeThis]()
    {
        if (safeThis == nullptr)
            return;
        safeThis->applyPersistedUiStateFromProcessor (true, true);
    });

    juce::Timer::callAfterDelay (750, [safeThis]()
    {
        if (safeThis == nullptr)
            return;
        safeThis->applyPersistedUiStateFromProcessor (true, true);
    });

    // CRT post-process effect — only attach the ImageEffectFilter when
    // actually enabled.  When disabled, skipping setComponentEffect avoids a
    // redundant full-image blit (drawImageAt) on every repaint.
    applyCrtState (crtEnabled);

    refreshLegendTextCache();
    resized();  // Ensure correct initial layout (MIDI port visibility, etc.)
}

ECHOTRAudioProcessorEditor::~ECHOTRAudioProcessorEditor()
{
    setComponentEffect (nullptr);
    stopTimer();

    for (auto* paramId : kUiMirrorParamIds)
        audioProcessor.apvts.removeParameterListener (paramId, this);

    audioProcessor.setUiUseCustomPalette (useCustomPalette);
    audioProcessor.setUiCrtEnabled (crtEnabled);

    dismissEditorOwnedModalPrompts (lnf);
    setPromptOverlayActive (false);

    const std::array<BarSlider*, 7> barSliders { &timeSlider, &modSlider, &feedbackSlider, &modeSlider, &inputSlider, &outputSlider, &mixSlider };
    for (auto* slider : barSliders)
        slider->removeListener (this);

    if (tooltipWindow != nullptr)
        tooltipWindow->setLookAndFeel (nullptr);

    setLookAndFeel (nullptr);
}

void ECHOTRAudioProcessorEditor::applyActivePalette()
{
    const auto& palette = useCustomPalette ? customPalette : defaultPalette;

    ECHOScheme scheme;
    scheme.bg = palette[1];
    scheme.fg = palette[0];
    scheme.outline = palette[0];
    scheme.text = palette[0];

    activeScheme = scheme;
    lnf.setScheme (activeScheme);
    
    // (midiChannelDisplay is now a transparent tooltip overlay — no text colour needed)
}

void ECHOTRAudioProcessorEditor::applyCrtState (bool enabled)
{
    crtEnabled = enabled;
    crtEffect.setEnabled (crtEnabled);
    setComponentEffect (crtEnabled ? &crtEffect : nullptr);
    stopTimer();
    startTimerHz (crtEnabled ? kCrtTimerHz : kIdleTimerHz);
}

void ECHOTRAudioProcessorEditor::applyLabelTextColour (juce::Label& label, juce::Colour colour)
{
    label.setColour (juce::Label::textColourId, colour);
}

void ECHOTRAudioProcessorEditor::sliderValueChanged (juce::Slider* slider)
{
    auto isBarSlider = [&] (const juce::Slider* s)
    {
        return s == &timeSlider || s == &feedbackSlider || s == &modeSlider || s == &modSlider
            || s == &inputSlider || s == &outputSlider || s == &mixSlider;
    };

    refreshLegendTextCache();

    if (slider == nullptr)
    {
        repaint();
        return;
    }

    if (isBarSlider (slider))
    {
        repaint (getRowRepaintBounds (*slider));
        return;
    }

    repaint();
}

void ECHOTRAudioProcessorEditor::setPromptOverlayActive (bool shouldBeActive)
{
    if (promptOverlayActive == shouldBeActive)
        return;

    promptOverlayActive = shouldBeActive;

    promptOverlay.setBounds (getLocalBounds());
    promptOverlay.setVisible (shouldBeActive);
    if (shouldBeActive)
        promptOverlay.toFront (false);

    const bool enableControls = ! shouldBeActive;
    const std::array<juce::Component*, 11> interactiveControls {
        &timeSlider, &feedbackSlider, &modeSlider, &modSlider,
        &inputSlider, &outputSlider, &mixSlider,
        &syncButton, &autoFbkButton, &reverseButton, &midiButton
    };
    for (auto* control : interactiveControls)
        control->setEnabled (enableControls);

    if (resizerCorner != nullptr)
        resizerCorner->setEnabled (enableControls);

    repaint();

    if (promptOverlayActive)
        promptOverlay.toFront (false);

    anchorEditorOwnedPromptWindows (*this, lnf);
}

void ECHOTRAudioProcessorEditor::moved()
{
    if (promptOverlayActive)
        promptOverlay.toFront (false);

    anchorEditorOwnedPromptWindows (*this, lnf);
}

void ECHOTRAudioProcessorEditor::parentHierarchyChanged()
{
   #if JUCE_WINDOWS
    // Set native window background brush to black so that Windows never
    // flashes white pixels in newly-exposed areas during live resize.
    if (auto* peer = getPeer())
    {
        if (auto nativeHandle = peer->getNativeHandle())
        {
            static HBRUSH blackBrush = CreateSolidBrush (RGB (0, 0, 0));
            SetClassLongPtr (static_cast<HWND> (nativeHandle),
                             GCLP_HBRBACKGROUND,
                             reinterpret_cast<LONG_PTR> (blackBrush));
        }
    }
   #endif
}

void ECHOTRAudioProcessorEditor::parameterChanged (const juce::String& parameterID, float newValue)
{
    // Handle SYNC mode changes
    if (parameterID == ECHOTRAudioProcessor::kParamSync)
    {
        const bool syncEnabled = newValue > 0.5f;
        juce::Component::SafePointer<ECHOTRAudioProcessorEditor> safeThis (this);
        juce::MessageManager::callAsync ([safeThis, syncEnabled]()
        {
            if (safeThis == nullptr)
                return;
            safeThis->updateTimeSliderForSyncMode (syncEnabled);
            safeThis->refreshLegendTextCache();
            safeThis->repaint();
        });
        return;
    }
    
    // Width/height should trigger applying size; other UI params should update palette/fx/colors.
    const bool isSizeParam = parameterID == ECHOTRAudioProcessor::kParamUiWidth
                         || parameterID == ECHOTRAudioProcessor::kParamUiHeight;

    const bool isUiVisualParam = parameterID == ECHOTRAudioProcessor::kParamUiPalette
                             || parameterID == ECHOTRAudioProcessor::kParamUiCrt
                             || parameterID == ECHOTRAudioProcessor::kParamUiColor0
                             || parameterID == ECHOTRAudioProcessor::kParamUiColor1;

    if (! isSizeParam && ! isUiVisualParam)
        return;

    juce::Component::SafePointer<ECHOTRAudioProcessorEditor> safeThis (this);
    juce::MessageManager::callAsync ([safeThis, isSizeParam]()
    {
        if (safeThis == nullptr)
            return;

        if (isSizeParam)
            safeThis->applyPersistedUiStateFromProcessor (true, false);
        else
            safeThis->applyPersistedUiStateFromProcessor (false, true);
    });
}

void ECHOTRAudioProcessorEditor::timerCallback()
{
    if (suppressSizePersistence)
        return;

    const auto newMidiDisplay = audioProcessor.getCurrentTimeDisplay();
    const bool timeSliderHeld = timeSlider.isMouseButtonDown();
    if (newMidiDisplay != cachedMidiDisplay || timeSliderHeld != cachedTimeSliderHeld)
    {
        cachedMidiDisplay = newMidiDisplay;
        cachedTimeSliderHeld = timeSliderHeld;
        refreshLegendTextCache();
        // Only repaint the time slider row, not the entire window
        repaint (getRowRepaintBounds (timeSlider));
    }

    const int w = getWidth();
    const int h = getHeight();

    const uint32_t last = lastUserInteractionMs.load (std::memory_order_relaxed);
    const uint32_t now = juce::Time::getMillisecondCounter();
    const bool userRecent = (now - last) <= (uint32_t) kUserInteractionPersistWindowMs;

    if ((w != lastPersistedEditorW || h != lastPersistedEditorH) && userRecent)
    {
        audioProcessor.setUiEditorSize (w, h);
        lastPersistedEditorW = w;
        lastPersistedEditorH = h;
    }

    // ── CRT animation (advance time for ImageEffectFilter shader) ──
    if (crtEnabled && w > 0 && h > 0)
    {
        crtTime += 0.1f;              // ~10 Hz tick
        crtEffect.setTime (crtTime);

        // Skip the timer-driven repaint while a slider is actively
        // dragged — sliderValueChanged already triggers a repaint that
        // will pick up the updated crtTime.  Avoids an expensive
        // duplicate full-image CRT pass per timer tick during drag.
        const bool anySliderDragging = timeSlider.isMouseButtonDown()
                                    || feedbackSlider.isMouseButtonDown()
                                    || modeSlider.isMouseButtonDown()
                                    || modSlider.isMouseButtonDown()
                                    || inputSlider.isMouseButtonDown()
                                    || outputSlider.isMouseButtonDown()
                                    || mixSlider.isMouseButtonDown();
        if (! anySliderDragging)
            repaint();
    }
}

void ECHOTRAudioProcessorEditor::applyPersistedUiStateFromProcessor (bool applySize, bool applyPaletteAndFx)
{
    if (applySize)
    {
        const int targetW = juce::jlimit (kMinW, kMaxW, audioProcessor.getUiEditorWidth());
        const int targetH = juce::jlimit (kMinH, kMaxH, audioProcessor.getUiEditorHeight());

        if (getWidth() != targetW || getHeight() != targetH)
        {
            suppressSizePersistence = true;
            setSize (targetW, targetH);
            suppressSizePersistence = false;
        }
    }

    if (applyPaletteAndFx)
    {
        bool paletteChanged = false;
        for (int i = 0; i < 2; ++i)
        {
            const auto c = audioProcessor.getUiCustomPaletteColour (i);
            if (customPalette[(size_t) i].getARGB() != c.getARGB())
            {
                customPalette[(size_t) i] = c;
                paletteChanged = true;
            }
        }

        const bool targetUseCustomPalette = audioProcessor.getUiUseCustomPalette();
        const bool targetCrtEnabled = audioProcessor.getUiCrtEnabled();

        const bool paletteSwitchChanged = (useCustomPalette != targetUseCustomPalette);
        const bool fxChanged = (crtEnabled != targetCrtEnabled);

        const bool targetIoExpanded = audioProcessor.getUiIoExpanded();
        const bool ioChanged = (ioSectionExpanded_ != targetIoExpanded);
        if (ioChanged)
        {
            ioSectionExpanded_ = targetIoExpanded;
            resized();
        }

        if (paletteSwitchChanged)
            useCustomPalette = targetUseCustomPalette;

        if (fxChanged)
            applyCrtState (targetCrtEnabled);

        if (paletteChanged || paletteSwitchChanged)
            applyActivePalette();

        if (paletteChanged || paletteSwitchChanged || fxChanged || ioChanged)
            repaint();
    }
}

void ECHOTRAudioProcessorEditor::updateTimeSliderForSyncMode (bool syncEnabled)
{
    // Get current BPM for conversion
    auto posInfo = audioProcessor.getPlayHead();
    double bpm = 120.0;
    if (posInfo != nullptr)
    {
        auto pos = posInfo->getPosition();
        if (pos.hasValue() && pos->getBpm().hasValue())
            bpm = *pos->getBpm();
    }
    
    if (syncEnabled)
    {
        // Switching from MS to SYNC: convert current MS to nearest sync division
        const float currentMs = static_cast<float> (timeSlider.getValue());
        
        // Find closest sync division
        int bestSyncIndex = ECHOTRAudioProcessor::kTimeSyncDefault;
        float bestDiff = std::abs (currentMs - audioProcessor.tempoSyncToMs (bestSyncIndex, bpm));
        
        for (int i = 0; i < 30; ++i)
        {
            const float syncMs = audioProcessor.tempoSyncToMs (i, bpm);
            const float diff = std::abs (currentMs - syncMs);
            if (diff < bestDiff)
            {
                bestDiff = diff;
                bestSyncIndex = i;
            }
        }
        
        // Destroy MS attachment and create SYNC attachment
        timeAttachment.reset();
        timeSyncAttachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, 
                                                                  ECHOTRAudioProcessor::kParamTimeSync, 
                                                                  timeSlider);
        timeSlider.setRange (0.0, 29.0, 1.0);  // 30 sync divisions
        timeSlider.setDoubleClickReturnValue (true, (double) ECHOTRAudioProcessor::kTimeSyncDefault);
        
        // Update parameter value
        if (auto* param = audioProcessor.apvts.getParameter (ECHOTRAudioProcessor::kParamTimeSync))
            param->setValueNotifyingHost (param->convertTo0to1 ((float) bestSyncIndex));
    }
    else
    {
        // Switching from SYNC to MS: convert current sync division to MS
        const int currentSyncIndex = (int) timeSlider.getValue();
        const float targetMs = audioProcessor.tempoSyncToMs (currentSyncIndex, bpm);
        
        // Destroy SYNC attachment and create MS attachment
        timeSyncAttachment.reset();
        timeAttachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, 
                                                              ECHOTRAudioProcessor::kParamTimeMs, 
                                                              timeSlider);
        timeSlider.setRange (ECHOTRAudioProcessor::kTimeMsMin, 
                            ECHOTRAudioProcessor::kTimeMsMax, 
                            0.0);
        timeSlider.setDoubleClickReturnValue (true, kDefaultTimeMs);
        
        // Update parameter value
        if (auto* param = audioProcessor.apvts.getParameter (ECHOTRAudioProcessor::kParamTimeMs))
            param->setValueNotifyingHost (param->convertTo0to1 (targetMs));
    }
}

bool ECHOTRAudioProcessorEditor::refreshLegendTextCache()
{
    const auto oldTimeFull      = cachedTimeTextFull;
    const auto oldTimeShort     = cachedTimeTextShort;
    const auto oldFeedbackFull  = cachedFeedbackTextFull;
    const auto oldFeedbackShort = cachedFeedbackTextShort;
    const auto oldModeFull      = cachedModeTextFull;
    const auto oldModeShort     = cachedModeTextShort;
    const auto oldModFull       = cachedModTextFull;
    const auto oldModShort      = cachedModTextShort;
    const auto oldInputFull     = cachedInputTextFull;
    const auto oldInputShort    = cachedInputTextShort;
    const auto oldOutputFull    = cachedOutputTextFull;
    const auto oldOutputShort   = cachedOutputTextShort;
    const auto oldMixFull       = cachedMixTextFull;
    const auto oldMixShort      = cachedMixTextShort;

    cachedTimeTextFull = getTimeText();
    cachedTimeTextShort = getTimeTextShort();
    cachedFeedbackTextFull = getFeedbackText();
    cachedFeedbackTextShort = getFeedbackTextShort();
    cachedModeTextFull = getModeText();
    cachedModeTextShort = getModeTextShort();
    cachedModTextFull = getModText();
    cachedModTextShort = getModTextShort();
    cachedInputTextFull = getInputText();
    cachedInputTextShort = getInputTextShort();
    cachedOutputTextFull = getOutputText();
    cachedOutputTextShort = getOutputTextShort();
    cachedMixTextFull = getMixText();
    cachedMixTextShort = getMixTextShort();

    const bool changed = oldTimeFull      != cachedTimeTextFull
                      || oldTimeShort     != cachedTimeTextShort
                      || oldFeedbackFull  != cachedFeedbackTextFull
                      || oldFeedbackShort != cachedFeedbackTextShort
                      || oldModeFull      != cachedModeTextFull
                      || oldModeShort     != cachedModeTextShort
                      || oldModFull       != cachedModTextFull
                      || oldModShort      != cachedModTextShort
                      || oldInputFull     != cachedInputTextFull
                      || oldInputShort    != cachedInputTextShort
                      || oldOutputFull    != cachedOutputTextFull
                      || oldOutputShort   != cachedOutputTextShort
                      || oldMixFull       != cachedMixTextFull
                      || oldMixShort      != cachedMixTextShort;

    return changed;
}

juce::Rectangle<int> ECHOTRAudioProcessorEditor::getRowRepaintBounds (const juce::Slider& s) const
{
    auto bounds = s.getBounds().getUnion (getValueAreaFor (s.getBounds()));
    return bounds.expanded (8, 8).getIntersection (getLocalBounds());
}

void ECHOTRAudioProcessorEditor::setupBar (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::LinearBar);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    // Disable tooltip/popup above the bar (we use our own numeric popup)
    s.setPopupDisplayEnabled (false, false, this);
    s.setTooltip (juce::String());

    // IMPORTANT: disable popup menu so right-click can be used for our numeric popup
    s.setPopupMenuEnabled (false);

    s.setColour (juce::Slider::trackColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::backgroundColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::thumbColourId, juce::Colours::transparentBlack);
}

//========================== Right-click numeric popup ==========================

namespace
{
    constexpr int kTitleAreaExtraHeightPx = 4;
    constexpr int kTitleRightGapToInfoPx = 8;
    constexpr int kVersionGapPx = 8;
    constexpr int kToggleLegendCollisionPadPx = 6;

    struct PopupSwatchButton final : public juce::TextButton
    {
        std::function<void()> onLeftClick;
        std::function<void()> onRightClick;

        void clicked() override
        {
            if (onLeftClick)
                onLeftClick();
            else
                juce::TextButton::clicked();
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
            {
                if (onRightClick)
                    onRightClick();
                return;
            }

            juce::TextButton::mouseUp (e);
        }
    };

    struct PopupClickableLabel final : public juce::Label
    {
        using juce::Label::Label;
        std::function<void()> onClick;

        void mouseUp (const juce::MouseEvent& e) override
        {
            juce::Label::mouseUp (e);
            if (! e.mods.isPopupMenu() && onClick)
                onClick();
        }
    };

    // Label that renders using TextLayout instead of the default
    // drawFittedText / GlyphArrangement path.  This guarantees that
    // the rendered line-wrapping matches the TextLayout-based height
    // measurement in layoutInfoPopupContent, preventing clipping.
    struct TextLayoutLabel final : public juce::Label
    {
        using juce::Label::Label;

        void paint (juce::Graphics& g) override
        {
            g.fillAll (findColour (backgroundColourId));

            if (isBeingEdited())
                return;

            const auto f     = getFont();
            const auto area  = getBorderSize().subtractedFrom (getLocalBounds()).toFloat();
            const auto alpha = isEnabled() ? 1.0f : 0.5f;

            juce::AttributedString as;
            as.append (getText(), f,
                       findColour (textColourId).withMultipliedAlpha (alpha));
            as.setJustification (getJustificationType());

            juce::TextLayout layout;
            layout.createLayout (as, area.getWidth());
            layout.draw (g, area);
        }
    };

}

static void layoutInfoPopupContent (juce::AlertWindow& aw)
{
    layoutAlertWindowButtons (aw);

    const int contentTop = kPromptBodyTopPad;
    const int contentBottom = getAlertButtonsTop (aw) - kPromptBodyBottomPad;
    const int contentH = juce::jmax (0, contentBottom - contentTop);
    const int bodyW = aw.getWidth() - (2 * kPromptInnerMargin);

    // Find the viewport that wraps all body content
    auto* viewport = dynamic_cast<juce::Viewport*> (aw.findChildWithID ("bodyViewport"));
    if (viewport == nullptr)
        return;

    viewport->setBounds (kPromptInnerMargin, contentTop, bodyW, contentH);

    auto* content = viewport->getViewedComponent();
    if (content == nullptr)
        return;

    // Layout children inside content component top-to-bottom
    constexpr int kItemGap = 10;
    int y = 0;
    const int innerW = bodyW - 10;  // leave room for scrollbar

    for (int i = 0; i < content->getNumChildComponents(); ++i)
    {
        auto* child = content->getChildComponent (i);
        if (child == nullptr || ! child->isVisible())
            continue;

        // Labels measure preferred height from font
        int itemH = 30;
        if (auto* label = dynamic_cast<juce::Label*> (child))
        {
            auto font = label->getFont();
            const auto text = label->getText();
            const auto border = label->getBorderSize();

            // Single-line labels (no newlines): use font height directly
            if (! text.containsChar ('\n'))
            {
                itemH = (int) std::ceil (font.getHeight()) + border.getTopAndBottom();
            }
            else
            {
                // Multi-line: measure with TextLayout
                juce::AttributedString as;
                as.append (text, font, label->findColour (juce::Label::textColourId));
                as.setJustification (label->getJustificationType());
                juce::TextLayout layout;
                const int textAreaW = innerW - border.getLeftAndRight();
                layout.createLayout (as, (float) juce::jmax (1, textAreaW));
                itemH = juce::jmax (20, (int) std::ceil (layout.getHeight()
                                                         + font.getDescent())
                                        + border.getTopAndBottom() + 4);
            }
        }
        else if (dynamic_cast<juce::HyperlinkButton*> (child) != nullptr)
        {
            itemH = 28;
        }

        child->setBounds (0, y, innerW, itemH);

        // ── Poem auto-fit: if text overflows with padding, shrink font ──
        if (auto* label = dynamic_cast<juce::Label*> (child))
        {
            const auto& props = label->getProperties();
            if (props.contains ("poemPadFraction"))
            {
                const float padFrac = (float) props["poemPadFraction"];
                const int padPx = juce::jmax (4, (int) std::round (innerW * padFrac));
                label->setBorderSize (juce::BorderSize<int> (0, padPx, 0, padPx));

                // Check if text fits; if not, shrink font until it does (min 65% scale)
                auto font = label->getFont();
                const int textAreaW = innerW - 2 * padPx;
                for (float scale = 1.0f; scale >= 0.65f; scale -= 0.025f)
                {
                    font.setHorizontalScale (scale);
                    juce::GlyphArrangement glyphs;
                    glyphs.addLineOfText (font, label->getText(), 0.0f, 0.0f);
                    if (static_cast<int> (std::ceil (glyphs.getBoundingBox (0, -1, false).getWidth())) <= textAreaW)
                        break;
                }
                label->setFont (font);
            }
        }

        y += itemH + kItemGap;
    }

    // Remove trailing gap
    if (y > kItemGap)
        y -= kItemGap;

    content->setSize (innerW, juce::jmax (contentH, y));
}

static void syncGraphicsPopupState (juce::AlertWindow& aw,
                                    const std::array<juce::Colour, 2>& defaultPalette,
                                    const std::array<juce::Colour, 2>& customPalette,
                                    bool useCustomPalette)
{
    if (auto* t = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("paletteDefaultToggle")))
        t->setToggleState (! useCustomPalette, juce::dontSendNotification);
    if (auto* t = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("paletteCustomToggle")))
        t->setToggleState (useCustomPalette, juce::dontSendNotification);

    for (int i = 0; i < 2; ++i)
    {
        if (auto* dflt = dynamic_cast<juce::TextButton*> (aw.findChildWithID ("defaultSwatch" + juce::String (i))))
            setPaletteSwatchColour (*dflt, defaultPalette[(size_t) i]);
        if (auto* custom = dynamic_cast<juce::TextButton*> (aw.findChildWithID ("customSwatch" + juce::String (i))))
        {
            setPaletteSwatchColour (*custom, customPalette[(size_t) i]);
            custom->setTooltip (colourToHexRgb (customPalette[(size_t) i]));
        }
    }

    // Helper for static context: safely apply text colour to a label pointer
    auto applyLabelTextColourTo = [] (juce::Label* lbl, juce::Colour col)
    {
        if (lbl != nullptr)
            lbl->setColour (juce::Label::textColourId, col);
    };

    // Ensure popup labels reflect the active palette text colour
    const juce::Colour activeText = useCustomPalette ? customPalette[0] : defaultPalette[0];
    applyLabelTextColourTo (dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteDefaultLabel")), activeText);
    applyLabelTextColourTo (dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteCustomLabel")), activeText);
    applyLabelTextColourTo (dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteTitle")), activeText);
    applyLabelTextColourTo (dynamic_cast<juce::Label*> (aw.findChildWithID ("fxLabel")), activeText);
}

static void layoutGraphicsPopupContent (juce::AlertWindow& aw)
{
    layoutAlertWindowButtons (aw);

    auto snapEven = [] (int v) { return v & ~1; };

    const int contentLeft = kPromptInnerMargin;
    const int contentRight = aw.getWidth() - kPromptInnerMargin;
    const int contentW = juce::jmax (0, contentRight - contentLeft);

    auto* dfltToggle = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("paletteDefaultToggle"));
    auto* dfltLabel  = dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteDefaultLabel"));
    auto* customToggle = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("paletteCustomToggle"));
    auto* customLabel  = dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteCustomLabel"));
    auto* paletteTitle = dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteTitle"));
    auto* fxToggle = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("fxToggle"));
    auto* fxLabel  = dynamic_cast<juce::Label*> (aw.findChildWithID ("fxLabel"));
    auto* okBtn = aw.getNumButtons() > 0 ? aw.getButton (0) : nullptr;

    constexpr int toggleBox = GraphicsPromptLayout::toggleBox;
    constexpr int toggleGap = 4;
    constexpr int toggleVisualInsetLeft = 2;
    constexpr int swatchSize = GraphicsPromptLayout::swatchSize;
    constexpr int swatchGap = GraphicsPromptLayout::swatchGap;
    constexpr int columnGap = GraphicsPromptLayout::columnGap;
    constexpr int titleH = GraphicsPromptLayout::titleHeight;

    const int toggleVisualSide = juce::jlimit (14,
                                               juce::jmax (14, toggleBox - 2),
                                               (int) std::lround ((double) toggleBox * 0.65));

    const int swatchW = swatchSize;
    const int swatchH = (2 * swatchSize) + swatchGap;  // vertical rectangle = same as old 2-row grid
    const int swatchGroupSize = (2 * swatchW) + swatchGap;
    const int swatchesH = swatchH;
    const int modeH = toggleBox;

    const int baseGap1 = GraphicsPromptLayout::titleToModeGap;
    const int baseGap2 = GraphicsPromptLayout::modeToSwatchesGap;

    // Title fixed at top, footer fixed at bottom
    const int titleY = snapEven (kPromptFooterBottomPad);
    const int footerY = getAlertButtonsTop (aw);

    // Body (toggles + swatches) centered between title bottom and footer top
    const int bodyH = modeH + baseGap2 + swatchesH;
    const int bodyZoneTop = titleY + titleH + baseGap1;
    const int bodyZoneBottom = footerY - baseGap1;
    const int bodyZoneH = juce::jmax (0, bodyZoneBottom - bodyZoneTop);
    const int bodyY = snapEven (bodyZoneTop + juce::jmax (0, (bodyZoneH - bodyH) / 2));

    const int modeY = bodyY;
    const int blocksY = snapEven (modeY + modeH + baseGap2);

    const int dfltLabelW = (dfltLabel != nullptr) ? juce::jmax (38, stringWidth (dfltLabel->getFont(), "DFLT") + 2) : 40;
    const int customLabelW = (customLabel != nullptr) ? juce::jmax (38, stringWidth (customLabel->getFont(), "CSTM") + 2) : 40;
    const int fxLabelW = (fxLabel != nullptr)
                       ? juce::jmax (90, stringWidth (fxLabel->getFont(), fxLabel->getText().toUpperCase()) + 2)
                       : 96;

    const int toggleLabelStartOffset = toggleVisualInsetLeft + toggleVisualSide + toggleGap;
    const int dfltRowW = toggleLabelStartOffset + dfltLabelW;
    const int customRowW = toggleLabelStartOffset + customLabelW;
    const int fxRowW = toggleLabelStartOffset + fxLabelW;
    const int okBtnW = (okBtn != nullptr) ? okBtn->getWidth() : 96;

    const int leftColumnW = juce::jmax (swatchGroupSize, juce::jmax (dfltRowW, fxRowW));
    const int rightColumnW = juce::jmax (swatchGroupSize, juce::jmax (customRowW, okBtnW));
    const int columnsRowW = leftColumnW + columnGap + rightColumnW;
    const int columnsX = snapEven (contentLeft + juce::jmax (0, (contentW - columnsRowW) / 2));
    const int col0X = columnsX;
    const int col1X = columnsX + leftColumnW + columnGap;

    const int dfltX = col0X;
    const int customX = col1X;

    const int defaultSwatchStartX = col0X;
    const int customSwatchStartX = col1X;

    if (paletteTitle != nullptr)
    {
        const int paletteW = juce::jmax (100, juce::jmin (leftColumnW, contentRight - col0X));
        paletteTitle->setBounds (col0X,
                                 titleY,
                                 paletteW,
                                 titleH);
    }

    if (dfltToggle != nullptr)   dfltToggle->setBounds (dfltX, modeY, toggleBox, toggleBox);
    if (dfltLabel != nullptr)    dfltLabel->setBounds (dfltX + toggleLabelStartOffset, modeY, dfltLabelW, toggleBox);
    if (customToggle != nullptr) customToggle->setBounds (customX, modeY, toggleBox, toggleBox);
    if (customLabel != nullptr)  customLabel->setBounds (customX + toggleLabelStartOffset, modeY, customLabelW, toggleBox);

    auto placeSwatchGroup = [&] (const juce::String& prefix, int startX)
    {
        const int startY = blocksY;

        for (int i = 0; i < 2; ++i)
        {
            if (auto* b = dynamic_cast<juce::TextButton*> (aw.findChildWithID (prefix + juce::String (i))))
            {
                b->setBounds (startX + i * (swatchW + swatchGap),
                              startY,
                              swatchW,
                              swatchH);
            }
        }
    };

    placeSwatchGroup ("defaultSwatch", defaultSwatchStartX);
    placeSwatchGroup ("customSwatch", customSwatchStartX);

    if (okBtn != nullptr)
    {
        auto okR = okBtn->getBounds();
        okR.setX (col1X);
        okR.setY (footerY);
        okBtn->setBounds (okR);

        const int fxY = snapEven (footerY + juce::jmax (0, (okR.getHeight() - toggleBox) / 2));
        const int fxX = col0X;
        if (fxToggle != nullptr) fxToggle->setBounds (fxX, fxY, toggleBox, toggleBox);
        if (fxLabel != nullptr)  fxLabel->setBounds (fxX + toggleLabelStartOffset, fxY, fxLabelW, toggleBox);
    }

    auto updateVisualBounds = [] (juce::Component* c, int& minX, int& maxR)
    {
        if (c == nullptr)
            return;

        const auto r = c->getBounds();
        minX = juce::jmin (minX, r.getX());
        maxR = juce::jmax (maxR, r.getRight());
    };

    int visualMinX = aw.getWidth();
    int visualMaxR = 0;

    updateVisualBounds (paletteTitle, visualMinX, visualMaxR);
    updateVisualBounds (dfltToggle, visualMinX, visualMaxR);
    updateVisualBounds (dfltLabel, visualMinX, visualMaxR);
    updateVisualBounds (customToggle, visualMinX, visualMaxR);
    updateVisualBounds (customLabel, visualMinX, visualMaxR);
    updateVisualBounds (fxToggle, visualMinX, visualMaxR);
    updateVisualBounds (fxLabel, visualMinX, visualMaxR);
    updateVisualBounds (okBtn, visualMinX, visualMaxR);

    for (int i = 0; i < 2; ++i)
    {
        updateVisualBounds (aw.findChildWithID ("defaultSwatch" + juce::String (i)), visualMinX, visualMaxR);
        updateVisualBounds (aw.findChildWithID ("customSwatch" + juce::String (i)), visualMinX, visualMaxR);
    }

    if (visualMaxR > visualMinX)
    {
        const int leftMarginToPrompt = visualMinX;
        const int rightMarginToPrompt = aw.getWidth() - visualMaxR;

        int dx = (rightMarginToPrompt - leftMarginToPrompt) / 2;

        const int minDx = contentLeft - visualMinX;
        const int maxDx = contentRight - visualMaxR;
        dx = juce::jlimit (minDx, maxDx, dx);

        if (dx != 0)
        {
            auto shiftX = [dx] (juce::Component* c)
            {
                if (c == nullptr)
                    return;

                auto r = c->getBounds();
                r.setX (r.getX() + dx);
                c->setBounds (r);
            };

            shiftX (paletteTitle);
            shiftX (dfltToggle);
            shiftX (dfltLabel);
            shiftX (customToggle);
            shiftX (customLabel);
            shiftX (fxToggle);
            shiftX (fxLabel);
            shiftX (okBtn);

            for (int i = 0; i < 2; ++i)
            {
                shiftX (aw.findChildWithID ("defaultSwatch" + juce::String (i)));
                shiftX (aw.findChildWithID ("customSwatch" + juce::String (i)));
            }
        }
    }

}

void ECHOTRAudioProcessorEditor::openNumericEntryPopupForSlider (juce::Slider& s)
{
    // All sliders use the same numeric input style
    // make sure the LNF is using the current scheme in case it changed
    lnf.setScheme (activeScheme);

    // grab a local copy, we will use its raw colours below to bypass
    // any host/LNF oddities that might creep in
    const auto scheme = activeScheme;

    // decide what suffix label should appear; we want *separate* text that
    // is not part of the editable field. provide both long and short forms;
    // the layout lambda will auto-switch to short when combined width overflows.
    juce::String suffix;
    juce::String suffixShort;
    const bool isTimeSyncMode = (&s == &timeSlider && syncButton.getToggleState());
    if (&s == &timeSlider)
    {
        if (isTimeSyncMode)
        {
            suffix = "";
            suffixShort = "";
        }
        else
        {
            suffix = " MS";
            suffixShort = " MS";
        }
    }
    else if (&s == &feedbackSlider)  { suffix = " % FEEDBACK"; suffixShort = " % FBK"; }
    else if (&s == &modeSlider)      { suffix = " MODE";       suffixShort = " MODE"; }
    else if (&s == &modSlider)       { suffix = " X MOD";      suffixShort = " X"; }
    else if (&s == &inputSlider)     { suffix = " DB INPUT";   suffixShort = " DB IN"; }
    else if (&s == &outputSlider)    { suffix = " DB OUTPUT";  suffixShort = " DB OUT"; }
    else if (&s == &mixSlider)       { suffix = " % MIX";      suffixShort = " % MIX"; }
    const juce::String suffixText = suffix.trimStart();
    const juce::String suffixTextShort = suffixShort.trimStart();
    const bool isPercentPrompt = (&s == &feedbackSlider || &s == &mixSlider);

    // Sin texto de prompt: solo input + OK/Cancel
    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);

    // enforce our custom look&feel; hosts often reset dialogs to their own LNF
    aw->setLookAndFeel (&lnf);

    // For MOD, show the multiplier value instead of raw slider %
    juce::String currentDisplay;
    if (&s == &modSlider)
    {
        currentDisplay = juce::String (modSliderToMultiplier (s.getValue()), 2);
    }
    else
    {
        currentDisplay = s.getTextFromValue (s.getValue());
    }
    aw->addTextEditor ("val", currentDisplay, juce::String()); // sin label

    // we will create a label just to the right of the editor showing the suffix
    juce::Label* suffixLabel = nullptr;

    // Special filter for sync mode: allows division names (e.g., "1/8T") or numeric indices
    struct SyncDivisionInputFilter : juce::TextEditor::InputFilter
    {
        int maxLen;
        
        SyncDivisionInputFilter (int maxLength) : maxLen (maxLength) {}
        
        juce::String filterNewText (juce::TextEditor& editor,
                                    const juce::String& newText) override
        {
            juce::ignoreUnused (editor);
            juce::String result;
            
            for (auto c : newText)
            {
                // Allow digits, '/', 'T', 't', '.'
                if (juce::CharacterFunctions::isDigit (c) || c == '/' || 
                    c == 'T' || c == 't' || c == '.')
                {
                    result += c;
                }
                
                if (maxLen > 0 && result.length() >= maxLen)
                    break;
            }
            
            return result;
        }
    };

    juce::Rectangle<int> editorBaseBounds;
    std::function<void()> layoutValueAndSuffix;

    if (auto* te = aw->getTextEditor ("val"))
    {
        const auto& f = kBoldFont40();
        te->setFont (f);
        te->applyFontToAllText (f);

        // ensure the editor is tall enough to contain the larger text
        auto r = te->getBounds();
        r.setHeight ((int) (f.getHeight() * kPromptEditorHeightScale) + kPromptEditorHeightPadPx);
        r.setY (juce::jmax (kPromptEditorMinTopPx, r.getY() - kPromptEditorRaiseYPx));
        editorBaseBounds = r;

        // create & position the suffix label; it's non-editable and won't
        // be selected when the user highlights the value.
        suffixLabel = new juce::Label ("suffix", suffixText);
        suffixLabel->setComponentID (kPromptSuffixLabelId);
        suffixLabel->setJustificationType (juce::Justification::centredLeft);
        applyLabelTextColour (*suffixLabel, scheme.text);
        suffixLabel->setBorderSize (juce::BorderSize<int> (0));
        suffixLabel->setFont (f);
        aw->addAndMakeVisible (suffixLabel);

        // Compute the worst-case text width for this slider so the suffix
        // full/short decision is STATIC (based on max possible input, not
        // the current value).  Use a representative widest string per slider.
        juce::String worstCaseText;
        if (&s == &timeSlider)
            worstCaseText = isTimeSyncMode ? "1/64T." : "10000.000";
        else if (&s == &feedbackSlider)
            worstCaseText = "100.00";
        else if (&s == &modeSlider)
            worstCaseText = "3";
        else if (&s == &modSlider)
            worstCaseText = "4.00";
        else if (&s == &inputSlider)
            worstCaseText = "-100.0";
        else if (&s == &outputSlider)
            worstCaseText = "-100.0";
        else if (&s == &mixSlider)
            worstCaseText = "100.00";
        else
            worstCaseText = "999.99";

        const int maxInputTextW = juce::jmax (1, stringWidth (f, worstCaseText));

        layoutValueAndSuffix = [aw, te, suffixLabel, editorBaseBounds, isPercentPrompt, suffixText, suffixTextShort, maxInputTextW]()
        {
            // Auto-switch to short suffix based on WORST-CASE input width
            // (static decision — doesn't flicker as user types)
            const int contentPad = kPromptInlineContentPadPx;
            const int contentLeft = contentPad;
            const int contentRight = (aw != nullptr ? aw->getWidth() - contentPad : editorBaseBounds.getRight());
            const int availableW = contentRight - contentLeft;
            const int contentCenter = (contentLeft + contentRight) / 2;

            const int fullLabelW = stringWidth (suffixLabel->getFont(), suffixText) + 2;
            const bool stickPercentFull = suffixText.containsChar ('%');
            const int spaceWFull = stickPercentFull ? 0 : juce::jmax (2, stringWidth (suffixLabel->getFont(), " "));
            const int worstCaseFullW = maxInputTextW + spaceWFull + fullLabelW;

            const bool useShort = (worstCaseFullW > availableW) && suffixTextShort != suffixText;
            const juce::String& activeSuffix = useShort ? suffixTextShort : suffixText;
            suffixLabel->setText (activeSuffix, juce::dontSendNotification);

            // Now lay out with current text width
            const auto txt = te->getText();
            const int textW = juce::jmax (1, stringWidth (te->getFont(), txt));
            int labelW = stringWidth (suffixLabel->getFont(), activeSuffix) + 2;
            auto er = te->getBounds();

            const bool stickPercentToValue = activeSuffix.containsChar ('%');
            const int spaceW = stickPercentToValue ? 0 : juce::jmax (2, stringWidth (te->getFont(), " "));
            const int minGapPx = juce::jmax (1, spaceW);

            constexpr int kEditorTextPadPx = 12;
            constexpr int kMinEditorWidthPx = 24;
            const int editorW = juce::jlimit (kMinEditorWidthPx,
                                              editorBaseBounds.getWidth(),
                                              textW + (kEditorTextPadPx * 2));
            er.setWidth (editorW);

            const int combinedW = textW + minGapPx + labelW;

            int blockLeft = contentCenter - (combinedW / 2);
            const int minBlockLeft = contentLeft;
            const int maxBlockLeft = juce::jmax (minBlockLeft, contentRight - combinedW);
            blockLeft = juce::jlimit (minBlockLeft, maxBlockLeft, blockLeft);

            int teX = blockLeft - ((editorW - textW) / 2);
            const int minTeX = contentLeft;
            const int maxTeX = juce::jmax (minTeX, contentRight - editorW);
            teX = juce::jlimit (minTeX, maxTeX, teX);

            er.setX (teX);
            te->setBounds (er);

            const int textLeftActual = er.getX() + (er.getWidth() - textW) / 2;
            int labelX = textLeftActual + textW + minGapPx;
            const int minLabelX = contentLeft;
            const int maxLabelX = juce::jmax (minLabelX, contentRight - labelW);
            labelX = juce::jlimit (minLabelX, maxLabelX, labelX);

            const int labelY = er.getY();
            const int labelH = juce::jmax (1, er.getHeight());
            suffixLabel->setBounds (labelX, labelY, labelW, labelH);
        };

        // our reposition function will place the label; keep the editor at its
        // original width (no further shrink needed here). we still set the
        // bounds now so that later reposition() can rely on r's coordinates.
        te->setBounds (editorBaseBounds);
        int labelW0 = stringWidth (suffixLabel->getFont(), suffixText) + 2;
        suffixLabel->setBounds (r.getRight() + 2, r.getY() + 1, labelW0, juce::jmax (1, r.getHeight() - 2));

        // initial placement (label anchored to right)
        if (layoutValueAndSuffix)
            layoutValueAndSuffix();

        // choose limits depending on the slider being edited
        double minVal = 0.0, maxVal = 1.0;
        int maxLen = 0, maxDecs = 4;

        if (&s == &timeSlider)
        {
            if (isTimeSyncMode)
            {
                // In sync mode: allow typing division names or indices (0-29)
                minVal = 0.0;
                maxVal = 29.0;
                maxDecs = 0;
                maxLen = 6; // "1/64T." or "29"
            }
            else
            {
                minVal = 0.0;
                maxVal = 10000.0;  // 10 seconds max
                maxDecs = 3;
                maxLen = 9; // "10000.000"
            }
        }
        else if (&s == &feedbackSlider)
        {
            minVal = 0.0;
            maxVal = 100.0;    // user types percent (0-100%)
            maxDecs = 2;
            maxLen = 6; // "100.00"
        }
        else if (&s == &modeSlider)
        {
            minVal = 0.0;
            maxVal = 3.0;      // 4 modes (0-3)
            maxDecs = 0;
            maxLen = 1; // "3"
        }
        else if (&s == &modSlider)
        {
            minVal = 0.0;
            maxVal = 4.0;      // user types multiplier (x0.25 to x4)
            maxDecs = 2;
            maxLen = 4; // "4.00"
        }
        else if (&s == &inputSlider)
        {
            minVal = -100.0;
            maxVal = 0.0;      // dB range: -inf to 0 dB
            maxDecs = 1;
            maxLen = 6; // "-100.0"
        }
        else if (&s == &outputSlider)
        {
            minVal = -100.0;
            maxVal = 24.0;     // dB range: -inf to +24 dB
            maxDecs = 1;
            maxLen = 6; // "-100.0"
        }
        else if (&s == &mixSlider)
        {
            minVal = 0.0;
            maxVal = 100.0;    // user types percent
            maxDecs = 2;
            maxLen = 6; // "100.00"
        }

        // Use special filter for time slider in sync mode
        if (&s == &timeSlider && isTimeSyncMode)
            te->setInputFilter (new SyncDivisionInputFilter (maxLen), true);
        else
            te->setInputFilter (new NumericInputFilter (minVal, maxVal, maxLen, maxDecs), true);

        // limit text to appropriate decimals and move the suffix when text changes
        te->onTextChange = [te, layoutValueAndSuffix, maxDecs]() mutable
        {
            auto txt = te->getText();
            int dot = txt.indexOfChar('.');
            if (dot >= 0)
            {
                int decimals = txt.length() - dot - 1;
                if (decimals > maxDecs)
                    te->setText (txt.substring (0, dot + 1 + maxDecs), juce::dontSendNotification);
            }
            if (layoutValueAndSuffix)
                layoutValueAndSuffix();
        };
    }

    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    applyPromptShellSize (*aw);
    layoutAlertWindowButtons (*aw);

    // We can't call lookAndFeelChanged() on AlertWindow (it's protected),
    // so just rely on calling setLookAndFeel() twice instead.

    const juce::Font& kPromptFont = kBoldFont40();

    preparePromptTextEditor (*aw,
                             "val",
                             scheme.bg,
                             scheme.text,
                             scheme.fg,
                             kPromptFont,
                             false);

    // Force initial suffix placement with final editor metrics so the first
    // frame does not show a vertical offset.
    if (suffixLabel != nullptr && ! editorBaseBounds.isEmpty())
    {
        if (auto* te = aw->getTextEditor ("val"))
            suffixLabel->setFont (te->getFont());
        if (layoutValueAndSuffix)
            layoutValueAndSuffix();
    }

    // style buttons as well – some hosts stomp them when the window is added
    styleAlertButtons (*aw, lnf);

    juce::Component::SafePointer<ECHOTRAudioProcessorEditor> safeThis (this);
    juce::Slider* sliderPtr = &s;

    setPromptOverlayActive (true);

    // re-assert our look&feel in case the host modified it when
    // adding the window to the desktop.
    aw->setLookAndFeel (&lnf);

    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [&] (juce::AlertWindow& a)
        {
            if (layoutValueAndSuffix)
                layoutValueAndSuffix();
            layoutAlertWindowButtons (a);
            preparePromptTextEditor (a,
                                     "val",
                                     scheme.bg,
                                     scheme.text,
                                     scheme.fg,
                                     kPromptFont,
                                     false);
        });

        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
        aw->repaint();
    }

    // Apply final layout synchronously so the prompt is
    // fully laid out before being shown (avoids a small delayed re-layout).
    {
        preparePromptTextEditor (*aw,
                                 "val",
                                 scheme.bg,
                                 scheme.text,
                                 scheme.fg,
                                 kPromptFont,
                                 false);
        if (auto* suffixLbl = dynamic_cast<juce::Label*> (aw->findChildWithID (kPromptSuffixLabelId)))
        {
            if (auto* te = aw->getTextEditor ("val"))
                suffixLbl->setFont (te->getFont());
        }

        if (layoutValueAndSuffix)
            layoutValueAndSuffix();

        // Keep a lightweight async fallback that only ensures the window is
        // on top and repainted — avoid re-running layout to prevent visible jumps.
        juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
        juce::Component::SafePointer<ECHOTRAudioProcessorEditor> safeThisPtr (this);
        juce::MessageManager::callAsync ([safeAw, safeThisPtr]()
        {
            if (safeAw == nullptr)
                return;
            bringPromptWindowToFront (*safeAw);
            safeAw->repaint();
        });
    }

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, sliderPtr, aw] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);

            if (safeThis != nullptr)
                safeThis->setPromptOverlayActive (false);

            if (safeThis == nullptr || sliderPtr == nullptr)
                return;

            if (result != 1)
                return;

            const auto txt = aw->getTextEditorContents ("val").trim();
            auto normalised = txt.replaceCharacter (',', '.');

            double v = 0.0;
            
            // Special handling for TIME slider in sync mode: parse division name or index
            if (safeThis != nullptr && sliderPtr == &safeThis->timeSlider 
                && safeThis->syncButton.getToggleState())
            {
                // Try to parse as division name first
                int foundIndex = -1;
                auto choices = safeThis->audioProcessor.getTimeSyncChoices();
                for (int i = 0; i < choices.size(); ++i)
                {
                    if (txt.equalsIgnoreCase (choices[i]) || 
                        txt.equalsIgnoreCase (choices[i].replace ("/", "")))
                    {
                        foundIndex = i;
                        break;
                    }
                }
                
                // If not found, try to parse as numeric index
                if (foundIndex < 0)
                {
                    juce::String t = normalised.trimStart();
                    while (t.startsWithChar ('+'))
                        t = t.substring (1).trimStart();
                    const juce::String numericToken = t.initialSectionContainingOnly ("0123456789");
                    foundIndex = numericToken.getIntValue();
                }
                
                // Clamp to valid range
                v = (double) juce::jlimit (0, 29, foundIndex);
            }
            else
            {
                // Standard numeric parsing for all other sliders
                juce::String t = normalised.trimStart();
                while (t.startsWithChar ('+'))
                    t = t.substring (1).trimStart();
                const juce::String numericToken = t.initialSectionContainingOnly ("0123456789.,-");
                v = numericToken.getDoubleValue();

                // user typed percent for feedback/mix; convert to slider's [0,1] range
                if (safeThis != nullptr && (sliderPtr == &safeThis->feedbackSlider
                                         || sliderPtr == &safeThis->mixSlider))
                    v *= 0.01;

                // user typed multiplier for MOD; convert to slider's [0,1] range
                if (safeThis != nullptr && sliderPtr == &safeThis->modSlider)
                    v = multiplierToModSlider (v);
            }

            const auto range = sliderPtr->getRange();
            double clamped = juce::jlimit (range.getStart(), range.getEnd(), v);

            if (safeThis != nullptr && sliderPtr == &safeThis->timeSlider 
                && !safeThis->syncButton.getToggleState())
            {
                clamped = roundToDecimals (clamped, 2);
            }

            sliderPtr->setValue (clamped, juce::sendNotificationSync);
        }));
}

void ECHOTRAudioProcessorEditor::openMidiChannelPrompt()
{
    lnf.setScheme (activeScheme);
    const auto scheme = activeScheme;

    const juce::String suffixText = "CHANNEL";
    const bool legendFirst = true;  // legend appears before input field
    const int channel = audioProcessor.getMidiChannel();
    const juce::String currentValue = juce::String (channel);
    
    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);
    aw->addTextEditor ("val", currentValue, juce::String()); // sin label, igual que sliders
    
    juce::Label* suffixLabel = nullptr;
    
    // Numeric input filter for MIDI channel (0-16)
    struct MidiChannelInputFilter : juce::TextEditor::InputFilter
    {
        juce::String filterNewText (juce::TextEditor& editor, const juce::String& newText) override
        {
            juce::String result;
            for (auto c : newText)
            {
                if (juce::CharacterFunctions::isDigit (c))
                    result += c;
                if (result.length() >= 2)
                    break;
            }

            // Reject if the proposed full value exceeds 16 or total length > 2
            juce::String proposed = editor.getText();
            int insertPos = editor.getCaretPosition();
            proposed = proposed.substring (0, insertPos) + result
                     + proposed.substring (insertPos + editor.getHighlightedText().length());
            if (proposed.length() > 2 || proposed.getIntValue() > 16)
                return juce::String();

            // Reject leading zeros ("01" etc. — "0" alone is valid for OMNI)
            if (proposed.length() > 1 && proposed[0] == '0')
                return juce::String();

            return result;
        }
    };
    
    juce::Rectangle<int> editorBaseBounds;
    std::function<void()> layoutValueAndSuffix;
    
    if (auto* te = aw->getTextEditor ("val"))
    {
        const auto& f = kBoldFont40();
        te->setFont (f);
        te->applyFontToAllText (f);
        
        auto r = te->getBounds();
        r.setHeight ((int) (f.getHeight() * kPromptEditorHeightScale) + kPromptEditorHeightPadPx);
        r.setY (juce::jmax (kPromptEditorMinTopPx, r.getY() - kPromptEditorRaiseYPx));
        editorBaseBounds = r;
        
        suffixLabel = new juce::Label ("suffix", suffixText);
        suffixLabel->setComponentID (kPromptSuffixLabelId);
        suffixLabel->setJustificationType (juce::Justification::centredLeft);
        applyLabelTextColour (*suffixLabel, scheme.text);
        suffixLabel->setBorderSize (juce::BorderSize<int> (0));
        suffixLabel->setFont (f);
        aw->addAndMakeVisible (suffixLabel);
        
        layoutValueAndSuffix = [aw, te, suffixLabel, editorBaseBounds, legendFirst]()
        {
            int labelW = stringWidth (suffixLabel->getFont(), suffixLabel->getText()) + 2;
            auto er = te->getBounds();
            
            const auto txt = te->getText();
            const int textW = juce::jmax (1, stringWidth (te->getFont(), txt));
            const int spaceW = juce::jmax (2, stringWidth (te->getFont(), " "));
            const int minGapPx = juce::jmax (1, spaceW);
            
            constexpr int kEditorTextPadPx = 12;
            constexpr int kMinEditorWidthPx = 24;
            const int editorW = juce::jlimit (kMinEditorWidthPx,
                                              editorBaseBounds.getWidth(),
                                              textW + (kEditorTextPadPx * 2));
            er.setWidth (editorW);
            
            const int combinedW = labelW + minGapPx + textW;
            
            const int contentPad = kPromptInlineContentPadPx;
            const int contentLeft = contentPad;
            const int contentRight = (aw != nullptr ? aw->getWidth() - contentPad : editorBaseBounds.getRight());
            const int contentCenter = (contentLeft + contentRight) / 2;
            
            int blockLeft = contentCenter - (combinedW / 2);
            const int minBlockLeft = contentLeft;
            const int maxBlockLeft = juce::jmax (minBlockLeft, contentRight - combinedW);
            blockLeft = juce::jlimit (minBlockLeft, maxBlockLeft, blockLeft);
            
            if (legendFirst)
            {
                // Label BEFORE input: [CHANNEL] [input]
                int labelX = blockLeft;
                const int minLabelX = contentLeft;
                const int maxLabelX = juce::jmax (minLabelX, contentRight - combinedW);
                labelX = juce::jlimit (minLabelX, maxLabelX, labelX);
                
                const int labelY = er.getY();
                const int labelH = juce::jmax (1, er.getHeight());
                suffixLabel->setBounds (labelX, labelY, labelW, labelH);
                
                int teX = labelX + labelW + minGapPx - ((editorW - textW) / 2);
                const int minTeX = contentLeft;
                const int maxTeX = juce::jmax (minTeX, contentRight - editorW);
                teX = juce::jlimit (minTeX, maxTeX, teX);
                er.setX (teX);
                te->setBounds (er);
            }
            else
            {
                // Input BEFORE label: [input] [SUFFIX]
                int teX = blockLeft - ((editorW - textW) / 2);
                const int minTeX = contentLeft;
                const int maxTeX = juce::jmax (minTeX, contentRight - editorW);
                teX = juce::jlimit (minTeX, maxTeX, teX);
                er.setX (teX);
                te->setBounds (er);
                
                const int textLeftActual = er.getX() + (er.getWidth() - textW) / 2;
                int labelX = textLeftActual + textW + minGapPx;
                const int minLabelX = contentLeft;
                const int maxLabelX = juce::jmax (minLabelX, contentRight - labelW);
                labelX = juce::jlimit (minLabelX, maxLabelX, labelX);
                
                const int labelY = er.getY();
                const int labelH = juce::jmax (1, er.getHeight());
                suffixLabel->setBounds (labelX, labelY, labelW, labelH);
            }
        };
        
        te->setBounds (editorBaseBounds);
        int labelW0 = stringWidth (suffixLabel->getFont(), suffixText) + 2;
        suffixLabel->setBounds (r.getRight() + 2, r.getY() + 1, labelW0, juce::jmax (1, r.getHeight() - 2));
        
        if (layoutValueAndSuffix)
            layoutValueAndSuffix();
        
        te->setInputFilter (new MidiChannelInputFilter(), true);
        te->onTextChange = [te, layoutValueAndSuffix]() mutable
        {
            if (layoutValueAndSuffix)
                layoutValueAndSuffix();
        };
    }
    
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    applyPromptShellSize (*aw);
    layoutAlertWindowButtons (*aw);
    
    const juce::Font& kMidiPromptFont = kBoldFont40();

    preparePromptTextEditor (*aw,
                             "val",
                             scheme.bg,
                             scheme.text,
                             scheme.fg,
                             kMidiPromptFont,
                             false);
    
    if (suffixLabel != nullptr && ! editorBaseBounds.isEmpty())
    {
        if (auto* te = aw->getTextEditor ("val"))
            suffixLabel->setFont (te->getFont());
        if (layoutValueAndSuffix)
            layoutValueAndSuffix();
    }
    
    // Style buttons
    styleAlertButtons (*aw, lnf);
    
    juce::Component::SafePointer<ECHOTRAudioProcessorEditor> safeThis (this);
    
    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [layoutValueAndSuffix] (juce::AlertWindow& a)
        {
            layoutAlertWindowButtons (a);
            if (layoutValueAndSuffix)
                layoutValueAndSuffix();
        });
        
        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
    }
    
    {
        preparePromptTextEditor (*aw,
                                 "val",
                                 scheme.bg,
                                 scheme.text,
                                 scheme.fg,
                                 kMidiPromptFont,
                                 false);
        if (auto* suffixLbl = dynamic_cast<juce::Label*> (aw->findChildWithID (kPromptSuffixLabelId)))
        {
            if (auto* te = aw->getTextEditor ("val"))
                suffixLbl->setFont (te->getFont());
        }
        
        if (layoutValueAndSuffix)
            layoutValueAndSuffix();
        
        juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
        juce::MessageManager::callAsync ([safeAw]()
        {
            if (safeAw == nullptr)
                return;
            bringPromptWindowToFront (*safeAw);
            safeAw->repaint();
        });
    }
    
    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, aw] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);
            
            if (safeThis != nullptr)
                safeThis->setPromptOverlayActive (false);
            
            if (safeThis == nullptr || result != 1)
                return;
            
            const auto txt = aw->getTextEditorContents ("val").trim();
            const int ch = juce::jlimit (0, 16, txt.isEmpty() ? 0 : txt.getIntValue());
            safeThis->audioProcessor.setMidiChannel (ch);
            safeThis->midiChannelDisplay.setTooltip (formatMidiChannelTooltip (ch));
        }),
        false);
}

void ECHOTRAudioProcessorEditor::openAutoFbkPrompt()
{
    lnf.setScheme (activeScheme);
    const auto scheme = activeScheme;

    const float currentTau = audioProcessor.apvts.getRawParameterValue (ECHOTRAudioProcessor::kParamAutoFbkTau)->load();
    const float currentAtt = audioProcessor.apvts.getRawParameterValue (ECHOTRAudioProcessor::kParamAutoFbkAtt)->load();

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);

    // Two text editors: tau and att
    aw->addTextEditor ("tau", juce::String (juce::roundToInt (currentTau)), juce::String());
    aw->addTextEditor ("att", juce::String (juce::roundToInt (currentAtt)), juce::String());

    // ── Inline bar component ──
    // Minimal horizontal bar: paints outline + bg + filled portion (same as main GUI).
    // Draggable to change value; double-click resets to default.
    // No numeric popup — we're already inside a prompt.
    struct PromptBar : public juce::Component
    {
        ECHOScheme colours;
        float value      = 0.5f;  // 0..1
        float defaultVal = 0.5f;  // 0..1
        std::function<void (float)> onValueChanged;

        PromptBar (const ECHOScheme& s, float initial01, float default01)
            : colours (s), value (initial01), defaultVal (default01) {}

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (colours.outline);
            g.drawRect (r, 4.0f);

            const float pad = 7.0f;
            auto inner = r.reduced (pad);

            g.setColour (colours.bg);
            g.fillRect (inner);

            const float fillW = juce::jlimit (0.0f, inner.getWidth(), inner.getWidth() * value);
            g.setColour (colours.fg);
            g.fillRect (inner.withWidth (fillW));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            updateFromMouse (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            updateFromMouse (e);
        }

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            setValue (defaultVal);
        }

        void setValue (float v01)
        {
            value = juce::jlimit (0.0f, 1.0f, v01);
            repaint();
            if (onValueChanged)
                onValueChanged (value);
        }

    private:
        void updateFromMouse (const juce::MouseEvent& e)
        {
            const float pad = 7.0f;
            const float innerX = pad;
            const float innerW = (float) getWidth() - pad * 2.0f;
            const float v = (innerW > 0.0f) ? ((float) e.x - innerX) / innerW : 0.0f;
            setValue (v);
        }
    };

    // Clickable suffix label — double-click resets the paired bar to default.
    struct ResetLabel : public juce::Label
    {
        PromptBar* pairedBar = nullptr;

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (pairedBar != nullptr)
                pairedBar->setValue (pairedBar->defaultVal);
        }
    };

    const auto& f = kBoldFont40();

    // Suffix labels (name) and unit labels (%)
    ResetLabel* tauSuffix = nullptr;
    ResetLabel* attSuffix = nullptr;
    juce::Label* tauPctLabel = nullptr;
    juce::Label* attPctLabel = nullptr;

    auto setupField = [&] (const char* editorId, const juce::String& suffixText,
                           ResetLabel*& suffixOut, juce::Label*& pctOut)
    {
        if (auto* te = aw->getTextEditor (editorId))
        {
            te->setFont (f);
            te->applyFontToAllText (f);
            te->setInputFilter (new PctInputFilter(), true);

            auto r = te->getBounds();
            r.setHeight ((int) (f.getHeight() * kPromptEditorHeightScale) + kPromptEditorHeightPadPx);
            te->setBounds (r);

            suffixOut = new ResetLabel();
            suffixOut->setText (suffixText, juce::dontSendNotification);
            suffixOut->setJustificationType (juce::Justification::centredLeft);
            applyLabelTextColour (*suffixOut, scheme.text);
            suffixOut->setBorderSize (juce::BorderSize<int> (0));
            suffixOut->setFont (f);
            aw->addAndMakeVisible (suffixOut);

            pctOut = new juce::Label ("", "%");
            pctOut->setJustificationType (juce::Justification::centredLeft);
            applyLabelTextColour (*pctOut, scheme.text);
            pctOut->setBorderSize (juce::BorderSize<int> (0));
            pctOut->setFont (f);
            aw->addAndMakeVisible (pctOut);
        }
    };

    setupField ("tau", "TAU", tauSuffix, tauPctLabel);
    setupField ("att", "ATT", attSuffix, attPctLabel);

    // Create bars
    auto* tauBar = new PromptBar (scheme, currentTau * 0.01f,
                                  ECHOTRAudioProcessor::kAutoFbkTauDefault * 0.01f);
    auto* attBar = new PromptBar (scheme, currentAtt * 0.01f,
                                  ECHOTRAudioProcessor::kAutoFbkAttDefault * 0.01f);
    aw->addAndMakeVisible (tauBar);
    aw->addAndMakeVisible (attBar);

    // Link suffix labels to bars for double-click reset
    if (tauSuffix != nullptr) tauSuffix->pairedBar = tauBar;
    if (attSuffix != nullptr) attSuffix->pairedBar = attBar;

    // ── Bidirectional sync: bar ↔ textEditor ──
    // Guard flag to avoid feedback loops during programmatic updates.
    auto syncing = std::make_shared<bool> (false);

    auto barToText = [aw, syncing] (const char* editorId, float v01)
    {
        if (*syncing) return;
        *syncing = true;
        if (auto* te = aw->getTextEditor (editorId))
        {
            te->setText (juce::String (juce::roundToInt (v01 * 100.0f)), juce::sendNotification);
            te->selectAll();
        }
        *syncing = false;
    };

    // Get APVTS parameter pointers for real-time DSP updates while prompt is open
    auto* tauApvts = audioProcessor.apvts.getParameter (ECHOTRAudioProcessor::kParamAutoFbkTau);
    auto* attApvts = audioProcessor.apvts.getParameter (ECHOTRAudioProcessor::kParamAutoFbkAtt);

    tauBar->onValueChanged = [barToText, tauApvts] (float v)
    {
        barToText ("tau", v);
        if (tauApvts != nullptr)
            tauApvts->setValueNotifyingHost (tauApvts->convertTo0to1 (v * 100.0f));
    };
    attBar->onValueChanged = [barToText, attApvts] (float v)
    {
        barToText ("att", v);
        if (attApvts != nullptr)
            attApvts->setValueNotifyingHost (attApvts->convertTo0to1 (v * 100.0f));
    };

    // Layout: vertically center both rows (text + bar) in the space above buttons.
    auto layoutRows = [aw, tauSuffix, attSuffix, tauPctLabel, attPctLabel, tauBar, attBar] ()
    {
        auto* tauTe = aw->getTextEditor ("tau");
        auto* attTe = aw->getTextEditor ("att");
        if (tauTe == nullptr || attTe == nullptr)
            return;

        const int buttonsTop = getAlertButtonsTop (*aw);
        const int rowH = tauTe->getHeight();
        const int barH = juce::jmax (10, rowH / 2);
        const int barGap = juce::jmax (2, rowH / 6);
        const int rowTotal = rowH + barGap + barH;          // text + gap + bar
        const int gap = juce::jmax (4, rowH / 3);           // between row blocks
        const int totalH = rowTotal * 2 + gap;
        const int startY = juce::jmax (kPromptEditorMinTopPx, (buttonsTop - totalH) / 2);

        const int contentPad = kPromptInlineContentPadPx;
        const int contentW = aw->getWidth() - contentPad * 2;
        const auto& font = tauTe->getFont();
        const int spaceW = juce::jmax (2, stringWidth (font, " "));
        const int pctW = stringWidth (font, "%") + 2;

        auto placeRow = [&] (juce::TextEditor* te, juce::Label* suffix, juce::Label* pctLabel, PromptBar* bar, int y)
        {
            if (te == nullptr || suffix == nullptr || bar == nullptr)
                return;

            const int labelW = stringWidth (suffix->getFont(), suffix->getText()) + 2;
            const auto txt = te->getText();
            const int textW = juce::jmax (1, stringWidth (font, txt));

            constexpr int kEditorTextPadPx = 12;
            constexpr int kMinEditorWidthPx = 24;
            const int editorW = juce::jlimit (kMinEditorWidthPx, 80, textW + kEditorTextPadPx * 2);

            // Centre using VISUAL widths: [LABEL] [text] [%]
            const int visualW = labelW + spaceW + textW + pctW;
            const int centerX = contentPad + contentW / 2;
            int blockLeft = centerX - visualW / 2;
            blockLeft = juce::jlimit (contentPad, juce::jmax (contentPad, contentPad + contentW - visualW), blockLeft);

            // Label BEFORE input: [TAU] [input] [%]
            suffix->setBounds (blockLeft, y, labelW, rowH);

            // Editor offset so its visible text aligns with the centred block
            int teX = blockLeft + labelW + spaceW - (editorW - textW) / 2;
            teX = juce::jlimit (contentPad, juce::jmax (contentPad, contentPad + contentW - editorW), teX);
            te->setBounds (teX, y, editorW, rowH);

            // % label right after the visible text
            if (pctLabel != nullptr)
            {
                const int textRightX = blockLeft + labelW + spaceW + textW;
                pctLabel->setBounds (textRightX, y, pctW, rowH);
            }

            // Bar below the text row, aligned with the footer buttons
            const int barX = kPromptInnerMargin;
            const int barW = juce::jmax (60, aw->getWidth() - kPromptInnerMargin * 2);
            bar->setBounds (barX, y + rowH + barGap, barW, barH);
        };

        placeRow (tauTe, tauSuffix, tauPctLabel, tauBar, startY);
        placeRow (attTe, attSuffix, attPctLabel, attBar, startY + rowTotal + gap);
    };

    // Wire text-change → bar update + re-layout
    auto textToBar = [syncing] (juce::TextEditor* te, PromptBar* bar,
                                juce::RangedAudioParameter* apvtsParam)
    {
        if (*syncing || te == nullptr || bar == nullptr) return;
        *syncing = true;
        const float v = juce::jlimit (0.0f, 100.0f, (float) te->getText().getIntValue());
        bar->value = v * 0.01f;
        bar->repaint();
        if (apvtsParam != nullptr)
            apvtsParam->setValueNotifyingHost (apvtsParam->convertTo0to1 (v));
        *syncing = false;
    };

    if (auto* tauTe = aw->getTextEditor ("tau"))
        tauTe->onTextChange = [layoutRows, tauTe, tauBar, textToBar, tauApvts] () mutable
        {
            textToBar (tauTe, tauBar, tauApvts);
            layoutRows();
        };
    if (auto* attTe = aw->getTextEditor ("att"))
        attTe->onTextChange = [layoutRows, attTe, attBar, textToBar, attApvts] () mutable
        {
            textToBar (attTe, attBar, attApvts);
            layoutRows();
        };

    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    applyPromptShellSize (*aw);
    layoutAlertWindowButtons (*aw);
    layoutRows();

    const juce::Font& kAutoFbkFont = kBoldFont40();

    preparePromptTextEditor (*aw, "tau", scheme.bg, scheme.text, scheme.fg, kAutoFbkFont, false);
    preparePromptTextEditor (*aw, "att", scheme.bg, scheme.text, scheme.fg, kAutoFbkFont, false);

    // Re-apply row layout after preparePromptTextEditor repositioned them
    layoutRows();

    styleAlertButtons (*aw, lnf);

    juce::Component::SafePointer<ECHOTRAudioProcessorEditor> safeThis (this);

    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [layoutRows] (juce::AlertWindow& a)
        {
            juce::ignoreUnused (a);
            layoutAlertWindowButtons (a);
            layoutRows();
        });

        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
    }

    // Final styling pass
    {
        preparePromptTextEditor (*aw, "tau", scheme.bg, scheme.text, scheme.fg, kAutoFbkFont, false);
        preparePromptTextEditor (*aw, "att", scheme.bg, scheme.text, scheme.fg, kAutoFbkFont, false);
        layoutRows();

        if (tauSuffix != nullptr)
        {
            if (auto* te = aw->getTextEditor ("tau"))
            {
                tauSuffix->setFont (te->getFont());
                if (tauPctLabel != nullptr) tauPctLabel->setFont (te->getFont());
            }
        }
        if (attSuffix != nullptr)
        {
            if (auto* te = aw->getTextEditor ("att"))
            {
                attSuffix->setFont (te->getFont());
                if (attPctLabel != nullptr) attPctLabel->setFont (te->getFont());
            }
        }

        layoutRows();

        juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
        juce::MessageManager::callAsync ([safeAw]()
        {
            if (safeAw == nullptr)
                return;
            bringPromptWindowToFront (*safeAw);
            safeAw->repaint();
        });
    }

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [safeThis, aw, tauBar, attBar,
             savedTau = currentTau, savedAtt = currentAtt] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);

            if (safeThis != nullptr)
                safeThis->setPromptOverlayActive (false);

            if (safeThis == nullptr)
                return;

            if (result != 1)
            {
                // CANCEL: revert to original values (real-time changes undone)
                if (auto* p = safeThis->audioProcessor.apvts.getParameter (ECHOTRAudioProcessor::kParamAutoFbkTau))
                    p->setValueNotifyingHost (p->convertTo0to1 (savedTau));
                if (auto* p = safeThis->audioProcessor.apvts.getParameter (ECHOTRAudioProcessor::kParamAutoFbkAtt))
                    p->setValueNotifyingHost (p->convertTo0to1 (savedAtt));
                return;
            }

            // OK: values already applied in real-time, just update tooltip
            const float newTau = juce::jlimit (0.0f, 100.0f, tauBar->value * 100.0f);
            const float newAtt = juce::jlimit (0.0f, 100.0f, attBar->value * 100.0f);
            safeThis->autoFbkDisplay.setTooltip (formatAutoFbkTooltip (newTau, newAtt));
        }),
        false);
}

void ECHOTRAudioProcessorEditor::openReverseSmoothPrompt()
{
    lnf.setScheme (activeScheme);
    const auto scheme = activeScheme;

    const float currentSmoothExp = audioProcessor.apvts.getRawParameterValue (ECHOTRAudioProcessor::kParamReverseSmooth)->load();
    const float currentMult = std::pow (2.0f, currentSmoothExp);

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);

    // Single text editor for smooth multiplier (0.25 to 4.00)
    aw->addTextEditor ("smooth", juce::String (currentMult, 2), juce::String());

    // Input filter: digits and decimal point, max 4 chars (e.g. "4.00")
    struct SmoothInputFilter : juce::TextEditor::InputFilter
    {
        juce::String filterNewText (juce::TextEditor& editor, const juce::String& newText) override
        {
            const bool existingHasDot = editor.getText().containsChar ('.');
            bool seenDot = false;
            juce::String result;

            for (auto c : newText)
            {
                if (c == '.')
                {
                    if (seenDot || existingHasDot)
                        continue;
                    seenDot = true;
                    result += c;
                }
                else if (juce::CharacterFunctions::isDigit (c))
                {
                    result += c;
                }

                if (result.length() >= 4)
                    break;
            }

            // Reject if the proposed multiplier exceeds 4.0 or total > 4 chars
            juce::String proposed = editor.getText();
            int insertPos = editor.getCaretPosition();
            proposed = proposed.substring (0, insertPos) + result
                     + proposed.substring (insertPos + editor.getHighlightedText().length());
            if (proposed.length() > 4 || proposed.getDoubleValue() > 4.0)
                return juce::String();

            // Reject leading zeros ("04" etc. — "0" or "0.x" are valid)
            juce::String stripped = proposed;
            if (stripped.length() > 1
                && stripped[0] == '0'
                && stripped[1] != '.')
                return juce::String();

            return result;
        }
    };

    // ── Inline bar component ──
    struct PromptBar : public juce::Component
    {
        ECHOScheme colours;
        float value      = 0.5f;   // 0..1 (maps to smooth exponent -2..+2)
        float defaultVal = 0.5f;
        std::function<void (float)> onValueChanged;

        PromptBar (const ECHOScheme& s, float initial01, float default01)
            : colours (s), value (initial01), defaultVal (default01) {}

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (colours.outline);
            g.drawRect (r, 4.0f);

            const float pad = 7.0f;
            auto inner = r.reduced (pad);

            g.setColour (colours.bg);
            g.fillRect (inner);

            const float fillW = juce::jlimit (0.0f, inner.getWidth(), inner.getWidth() * value);
            g.setColour (colours.fg);
            g.fillRect (inner.withWidth (fillW));
        }

        void mouseDown (const juce::MouseEvent& e) override   { updateFromMouse (e); }
        void mouseDrag (const juce::MouseEvent& e) override   { updateFromMouse (e); }
        void mouseDoubleClick (const juce::MouseEvent&) override { setValue (defaultVal); }

        void setValue (float v01)
        {
            value = juce::jlimit (0.0f, 1.0f, v01);
            repaint();
            if (onValueChanged)
                onValueChanged (value);
        }

    private:
        void updateFromMouse (const juce::MouseEvent& e)
        {
            const float pad = 7.0f;
            const float innerX = pad;
            const float innerW = (float) getWidth() - pad * 2.0f;
            const float v = (innerW > 0.0f) ? ((float) e.x - innerX) / innerW : 0.0f;
            setValue (v);
        }
    };

    struct ResetLabel : public juce::Label
    {
        PromptBar* pairedBar = nullptr;
        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (pairedBar != nullptr)
                pairedBar->setValue (pairedBar->defaultVal);
        }
    };

    const auto& f = kBoldFont40();

    // Helpers: map smooth exponent (-2..+2) ↔ 0..1 bar position
    auto expTo01 = [] (float exp) { return (exp - ECHOTRAudioProcessor::kReverseSmoothMin)
                                         / (ECHOTRAudioProcessor::kReverseSmoothMax - ECHOTRAudioProcessor::kReverseSmoothMin); };
    auto from01ToExp = [] (float v) { return ECHOTRAudioProcessor::kReverseSmoothMin
                                           + v * (ECHOTRAudioProcessor::kReverseSmoothMax - ECHOTRAudioProcessor::kReverseSmoothMin); };

    ResetLabel* smoothSuffix = nullptr;
    juce::Label* multLabel   = nullptr;

    if (auto* te = aw->getTextEditor ("smooth"))
    {
        te->setFont (f);
        te->applyFontToAllText (f);
        te->setInputFilter (new SmoothInputFilter(), true);

        auto r = te->getBounds();
        r.setHeight ((int) (f.getHeight() * kPromptEditorHeightScale) + kPromptEditorHeightPadPx);
        te->setBounds (r);

        smoothSuffix = new ResetLabel();
        smoothSuffix->setText ("SMOOTH", juce::dontSendNotification);
        smoothSuffix->setJustificationType (juce::Justification::centredLeft);
        applyLabelTextColour (*smoothSuffix, scheme.text);
        smoothSuffix->setBorderSize (juce::BorderSize<int> (0));
        smoothSuffix->setFont (f);
        aw->addAndMakeVisible (smoothSuffix);

        multLabel = new juce::Label ("", "x");
        multLabel->setJustificationType (juce::Justification::centredLeft);
        applyLabelTextColour (*multLabel, scheme.text);
        multLabel->setBorderSize (juce::BorderSize<int> (0));
        multLabel->setFont (f);
        aw->addAndMakeVisible (multLabel);
    }

    auto* smoothBar = new PromptBar (scheme, expTo01 (currentSmoothExp), expTo01 (ECHOTRAudioProcessor::kReverseSmoothDefault));
    aw->addAndMakeVisible (smoothBar);

    if (smoothSuffix != nullptr) smoothSuffix->pairedBar = smoothBar;

    // ── Bidirectional sync ──
    auto syncing = std::make_shared<bool> (false);

    auto barToText = [aw, syncing, from01ToExp] (float v01)
    {
        if (*syncing) return;
        *syncing = true;
        if (auto* te = aw->getTextEditor ("smooth"))
        {
            const float mult = std::pow (2.0f, from01ToExp (v01));
            te->setText (juce::String (mult, 2), juce::sendNotification);
            te->selectAll();
        }
        *syncing = false;
    };

    auto* smoothApvts = audioProcessor.apvts.getParameter (ECHOTRAudioProcessor::kParamReverseSmooth);

    smoothBar->onValueChanged = [barToText, smoothApvts, from01ToExp] (float v)
    {
        barToText (v);
        if (smoothApvts != nullptr)
        {
            const float exp = from01ToExp (v);
            smoothApvts->setValueNotifyingHost (smoothApvts->convertTo0to1 (exp));
        }
    };

    // Layout: vertically center the single row (text + bar) above buttons.
    auto layoutRows = [aw, smoothSuffix, multLabel, smoothBar] ()
    {
        auto* smoothTe = aw->getTextEditor ("smooth");
        if (smoothTe == nullptr)
            return;

        const int buttonsTop = getAlertButtonsTop (*aw);
        const int rowH = smoothTe->getHeight();
        const int barH = juce::jmax (10, rowH / 2);
        const int barGap = juce::jmax (2, rowH / 6);
        const int rowTotal = rowH + barGap + barH;
        const int startY = juce::jmax (kPromptEditorMinTopPx, (buttonsTop - rowTotal) / 2);

        const int contentPad = kPromptInlineContentPadPx;
        const int contentW = aw->getWidth() - contentPad * 2;
        const auto& font = smoothTe->getFont();
        const int spaceW = juce::jmax (2, stringWidth (font, " "));
        const int unitW = stringWidth (font, "x") + 2;

        if (smoothSuffix == nullptr || smoothBar == nullptr)
            return;

        const int labelW = stringWidth (smoothSuffix->getFont(), smoothSuffix->getText()) + 2;
        const auto txt = smoothTe->getText();
        const int textW = juce::jmax (1, stringWidth (font, txt));

        constexpr int kEditorTextPadPx = 12;
        constexpr int kMinEditorWidthPx = 24;
        const int editorW = juce::jlimit (kMinEditorWidthPx, 80, textW + kEditorTextPadPx * 2);

        const int visualW = labelW + spaceW + textW + unitW;
        const int centerX = contentPad + contentW / 2;
        int blockLeft = centerX - visualW / 2;
        blockLeft = juce::jlimit (contentPad, juce::jmax (contentPad, contentPad + contentW - visualW), blockLeft);

        smoothSuffix->setBounds (blockLeft, startY, labelW, rowH);

        int teX = blockLeft + labelW + spaceW - (editorW - textW) / 2;
        teX = juce::jlimit (contentPad, juce::jmax (contentPad, contentPad + contentW - editorW), teX);
        smoothTe->setBounds (teX, startY, editorW, rowH);

        if (multLabel != nullptr)
        {
            const int textRightX = blockLeft + labelW + spaceW + textW;
            multLabel->setBounds (textRightX, startY, unitW, rowH);
        }

        const int barX = kPromptInnerMargin;
        const int barW = juce::jmax (60, aw->getWidth() - kPromptInnerMargin * 2);
        smoothBar->setBounds (barX, startY + rowH + barGap, barW, barH);
    };

    // Wire text-change → bar + APVTS
    auto textToBar = [syncing, expTo01] (juce::TextEditor* te, PromptBar* bar,
                                         juce::RangedAudioParameter* apvtsParam)
    {
        if (*syncing || te == nullptr || bar == nullptr) return;
        *syncing = true;
        const float mult = juce::jlimit (0.25f, 4.0f, te->getText().getFloatValue());
        const float exp = std::log2 (mult);
        bar->value = expTo01 (exp);
        bar->repaint();
        if (apvtsParam != nullptr)
            apvtsParam->setValueNotifyingHost (apvtsParam->convertTo0to1 (exp));
        *syncing = false;
    };

    if (auto* smoothTe = aw->getTextEditor ("smooth"))
        smoothTe->onTextChange = [layoutRows, smoothTe, smoothBar, textToBar, smoothApvts] () mutable
        {
            textToBar (smoothTe, smoothBar, smoothApvts);
            layoutRows();
        };

    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    applyPromptShellSize (*aw);
    layoutAlertWindowButtons (*aw);
    layoutRows();

    const juce::Font& kReverseSmoothFont = kBoldFont40();
    preparePromptTextEditor (*aw, "smooth", scheme.bg, scheme.text, scheme.fg, kReverseSmoothFont, false);
    layoutRows();

    styleAlertButtons (*aw, lnf);

    juce::Component::SafePointer<ECHOTRAudioProcessorEditor> safeThis (this);

    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [layoutRows] (juce::AlertWindow& a)
        {
            juce::ignoreUnused (a);
            layoutAlertWindowButtons (a);
            layoutRows();
        });

        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
    }

    // Final styling pass
    {
        preparePromptTextEditor (*aw, "smooth", scheme.bg, scheme.text, scheme.fg, kReverseSmoothFont, false);
        layoutRows();

        if (smoothSuffix != nullptr)
        {
            if (auto* te = aw->getTextEditor ("smooth"))
            {
                smoothSuffix->setFont (te->getFont());
                if (multLabel != nullptr) multLabel->setFont (te->getFont());
            }
        }

        layoutRows();

        juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
        juce::MessageManager::callAsync ([safeAw]()
        {
            if (safeAw == nullptr)
                return;
            bringPromptWindowToFront (*safeAw);
            safeAw->repaint();
        });
    }

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [safeThis, aw, smoothBar, from01ToExp,
             savedSmoothExp = currentSmoothExp] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);

            if (safeThis != nullptr)
                safeThis->setPromptOverlayActive (false);

            if (safeThis == nullptr)
                return;

            if (result != 1)
            {
                // CANCEL: revert
                if (auto* p = safeThis->audioProcessor.apvts.getParameter (ECHOTRAudioProcessor::kParamReverseSmooth))
                    p->setValueNotifyingHost (p->convertTo0to1 (savedSmoothExp));
                return;
            }

            // OK: update tooltip
            const float newExp = from01ToExp (smoothBar->value);
            safeThis->reverseDisplay.setTooltip (formatReverseSmoothTooltip (newExp));
        }),
        false);
}

void ECHOTRAudioProcessorEditor::openInfoPopup()
{
    lnf.setScheme (activeScheme);

    setPromptOverlayActive (true);

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
    juce::Component::SafePointer<ECHOTRAudioProcessorEditor> safeThis (this);
    aw->setLookAndFeel (&lnf);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("GRAPHICS", 2);

    applyPromptShellSize (*aw);

    // ── Body content: parsed from InfoContent.h XML ──
    auto* bodyContent = new juce::Component();
    bodyContent->setComponentID ("bodyContent");

    auto infoFont = lnf.getAlertWindowMessageFont();
    infoFont.setHeight (infoFont.getHeight() * 1.45f);

    auto headingFont = infoFont;
    headingFont.setBold (true);
    headingFont.setHeight (infoFont.getHeight() * 1.25f);

    auto linkFont = infoFont;
    linkFont.setHeight (infoFont.getHeight() * 1.08f);

    auto poemFont = infoFont;
    poemFont.setItalic (true);

    // Parse the XML content from InfoContent.h
    auto xmlDoc = juce::XmlDocument::parse (InfoContent::xml);
    auto* contentNode = xmlDoc != nullptr ? xmlDoc->getChildByName ("content") : nullptr;

    if (contentNode != nullptr)
    {
        int elemIdx = 0;
        for (auto* node : contentNode->getChildIterator())
        {
            const auto tag  = node->getTagName();
            const auto text = node->getAllSubText().trim();
            const auto id   = tag + juce::String (elemIdx++);

            if (tag == "heading")
            {
                auto* l = new juce::Label (id, text);
                l->setComponentID (id);
                l->setJustificationType (juce::Justification::centred);
                applyLabelTextColour (*l, activeScheme.text);
                l->setFont (headingFont);
                bodyContent->addAndMakeVisible (l);
            }
            else if (tag == "text" || tag == "separator")
            {
                auto* l = new juce::Label (id, text);
                l->setComponentID (id);
                l->setJustificationType (juce::Justification::centred);
                applyLabelTextColour (*l, activeScheme.text);
                l->setFont (infoFont);
                l->setBorderSize (juce::BorderSize<int> (0));
                bodyContent->addAndMakeVisible (l);
            }
            else if (tag == "link")
            {
                const auto url = node->getStringAttribute ("url");
                auto* lnk = new juce::HyperlinkButton (text, juce::URL (url));
                lnk->setComponentID (id);
                lnk->setJustificationType (juce::Justification::centred);
                lnk->setColour (juce::HyperlinkButton::textColourId, activeScheme.text);
                lnk->setFont (linkFont, false, juce::Justification::centred);
                lnk->setTooltip ("");
                bodyContent->addAndMakeVisible (lnk);
            }
            else if (tag == "poem")
            {
                auto* l = new juce::Label (id, text);
                l->setComponentID (id);
                l->setJustificationType (juce::Justification::centred);
                applyLabelTextColour (*l, activeScheme.text);
                l->setFont (poemFont);
                // Horizontal padding: 12% of available width per side.
                // Gives the poem visual breathing room without hardcoding pixels.
                l->setBorderSize (juce::BorderSize<int> (0, 0, 0, 0));  // vertical=0; horizontal set in layout
                l->getProperties().set ("poemPadFraction", 0.12f);
                bodyContent->addAndMakeVisible (l);
            }
            else if (tag == "spacer")
            {
                auto* l = new juce::Label (id, "");
                l->setComponentID (id);
                l->setFont (infoFont);
                l->setBorderSize (juce::BorderSize<int> (0));
                bodyContent->addAndMakeVisible (l);
            }
        }
    }

    auto* viewport = new juce::Viewport();
    viewport->setComponentID ("bodyViewport");
    viewport->setViewedComponent (bodyContent, true);  // viewport owns bodyContent
    viewport->setScrollBarsShown (true, false);         // vertical only
    viewport->setScrollBarThickness (8);
    viewport->setLookAndFeel (&lnf);
    aw->addAndMakeVisible (viewport);

    layoutInfoPopupContent (*aw);

    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [] (juce::AlertWindow& a)
        {
            layoutInfoPopupContent (a);
        });

        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
        aw->repaint();
    }

    juce::MessageManager::callAsync ([safeAw, safeThis]()
    {
        if (safeAw == nullptr || safeThis == nullptr)
            return;

        bringPromptWindowToFront (*safeAw);
        safeAw->repaint();
    });

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis = juce::Component::SafePointer<ECHOTRAudioProcessorEditor> (this), aw] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);

            if (safeThis == nullptr)
                return;

            if (result == 2)
            {
                safeThis->openGraphicsPopup();
                return;
            }

            safeThis->setPromptOverlayActive (false);
        }));
}

void ECHOTRAudioProcessorEditor::openGraphicsPopup()
{
    lnf.setScheme (activeScheme);

    useCustomPalette = audioProcessor.getUiUseCustomPalette();
    crtEnabled = audioProcessor.getUiCrtEnabled();
    crtEffect.setEnabled (crtEnabled);
    applyActivePalette();

    setPromptOverlayActive (true);

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    juce::Component::SafePointer<ECHOTRAudioProcessorEditor> safeThis (this);
    juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
    aw->setLookAndFeel (&lnf);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));

    auto labelFont = lnf.getAlertWindowMessageFont();
    labelFont.setHeight (labelFont.getHeight() * 1.20f);

    auto addPopupLabel = [this, aw] (const juce::String& id,
                                     const juce::String& text,
                                     juce::Font font,
                                     juce::Justification justification = juce::Justification::centredLeft)
    {
        auto* label = new PopupClickableLabel (id, text);
        label->setComponentID (id);
        label->setJustificationType (justification);
        applyLabelTextColour (*label, activeScheme.text);
        label->setBorderSize (juce::BorderSize<int> (0));
        label->setFont (font);
        label->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        aw->addAndMakeVisible (label);
        return label;
    };

    auto* defaultToggle = new juce::ToggleButton ("");
    defaultToggle->setComponentID ("paletteDefaultToggle");
    aw->addAndMakeVisible (defaultToggle);

    auto* defaultLabel = addPopupLabel ("paletteDefaultLabel", "DFLT", labelFont);

    auto* customToggle = new juce::ToggleButton ("");
    customToggle->setComponentID ("paletteCustomToggle");
    aw->addAndMakeVisible (customToggle);

    auto* customLabel = addPopupLabel ("paletteCustomLabel", "CSTM", labelFont);

    auto paletteTitleFont = labelFont;
    paletteTitleFont.setHeight (paletteTitleFont.getHeight() * 1.30f);
    addPopupLabel ("paletteTitle", "PALETTE", paletteTitleFont, juce::Justification::centredLeft);

    for (int i = 0; i < 2; ++i)
    {
        auto* dflt = new juce::TextButton();
        dflt->setComponentID ("defaultSwatch" + juce::String (i));
        dflt->setTooltip ("Default palette colour " + juce::String (i + 1));
        aw->addAndMakeVisible (dflt);

        auto* custom = new PopupSwatchButton();
        custom->setComponentID ("customSwatch" + juce::String (i));
        custom->setTooltip (colourToHexRgb (customPalette[(size_t) i]));
        aw->addAndMakeVisible (custom);
    }

    auto* fxToggle = new juce::ToggleButton ("");
    fxToggle->setComponentID ("fxToggle");
    fxToggle->setToggleState (crtEnabled, juce::dontSendNotification);
    fxToggle->onClick = [safeThis, fxToggle]()
    {
        if (safeThis == nullptr || fxToggle == nullptr)
            return;

        safeThis->applyCrtState (fxToggle->getToggleState());
        safeThis->audioProcessor.setUiCrtEnabled (safeThis->crtEnabled);
        safeThis->repaint();
    };
    aw->addAndMakeVisible (fxToggle);

    auto* fxLabel = addPopupLabel ("fxLabel", "GRAPHIC FX", labelFont);

    auto syncAndRepaintPopup = [safeThis, safeAw]()
    {
        if (safeThis == nullptr || safeAw == nullptr)
            return;

        syncGraphicsPopupState (*safeAw, safeThis->defaultPalette, safeThis->customPalette, safeThis->useCustomPalette);
        layoutGraphicsPopupContent (*safeAw);
        safeAw->repaint();
    };

    auto applyPaletteAndRepaint = [safeThis]()
    {
        if (safeThis == nullptr)
            return;

        safeThis->applyActivePalette();
        safeThis->repaint();
    };

    defaultToggle->onClick = [safeThis, defaultToggle, customToggle, applyPaletteAndRepaint, syncAndRepaintPopup]() mutable
    {
        if (safeThis == nullptr || defaultToggle == nullptr || customToggle == nullptr)
            return;

        safeThis->useCustomPalette = false;
        safeThis->audioProcessor.setUiUseCustomPalette (safeThis->useCustomPalette);
        defaultToggle->setToggleState (true, juce::dontSendNotification);
        customToggle->setToggleState (false, juce::dontSendNotification);
        applyPaletteAndRepaint();
        syncAndRepaintPopup();
    };

    customToggle->onClick = [safeThis, defaultToggle, customToggle, applyPaletteAndRepaint, syncAndRepaintPopup]() mutable
    {
        if (safeThis == nullptr || defaultToggle == nullptr || customToggle == nullptr)
            return;

        safeThis->useCustomPalette = true;
        safeThis->audioProcessor.setUiUseCustomPalette (safeThis->useCustomPalette);
        defaultToggle->setToggleState (false, juce::dontSendNotification);
        customToggle->setToggleState (true, juce::dontSendNotification);
        applyPaletteAndRepaint();
        syncAndRepaintPopup();
    };

    if (defaultLabel != nullptr && defaultToggle != nullptr)
        defaultLabel->onClick = [defaultToggle]() { defaultToggle->triggerClick(); };

    if (customLabel != nullptr && customToggle != nullptr)
        customLabel->onClick = [customToggle]() { customToggle->triggerClick(); };

    if (fxLabel != nullptr && fxToggle != nullptr)
        fxLabel->onClick = [fxToggle]() { fxToggle->triggerClick(); };

    for (int i = 0; i < 2; ++i)
    {
        if (auto* customSwatch = dynamic_cast<PopupSwatchButton*> (aw->findChildWithID ("customSwatch" + juce::String (i))))
        {
            customSwatch->onLeftClick = [safeThis, safeAw, i]()
            {
                if (safeThis == nullptr)
                    return;

                auto& rng = juce::Random::getSystemRandom();
                const auto randomColour = juce::Colour::fromRGB ((juce::uint8) rng.nextInt (256),
                                                                 (juce::uint8) rng.nextInt (256),
                                                                 (juce::uint8) rng.nextInt (256));

                safeThis->customPalette[(size_t) i] = randomColour;
                safeThis->audioProcessor.setUiCustomPaletteColour (i, randomColour);
                if (safeThis->useCustomPalette)
                {
                    safeThis->applyActivePalette();
                    safeThis->repaint();
                }

                if (safeAw != nullptr)
                {
                    syncGraphicsPopupState (*safeAw, safeThis->defaultPalette, safeThis->customPalette, safeThis->useCustomPalette);
                    layoutGraphicsPopupContent (*safeAw);
                    safeAw->repaint();
                }
            };

            customSwatch->onRightClick = [safeThis, safeAw, i]()
            {
                if (safeThis == nullptr)
                    return;

                const auto scheme = safeThis->activeScheme;

                auto* colorAw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
                colorAw->setLookAndFeel (&safeThis->lnf);
                colorAw->addTextEditor ("hex", colourToHexRgb (safeThis->customPalette[(size_t) i]), juce::String());

                if (auto* te = colorAw->getTextEditor ("hex"))
                    te->setInputFilter (new HexInputFilter(), true);

                colorAw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                colorAw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

                styleAlertButtons (*colorAw, safeThis->lnf);

                applyPromptShellSize (*colorAw);
                layoutAlertWindowButtons (*colorAw);

                const juce::Font& kHexPromptFont = kBoldFont40();

                preparePromptTextEditor (*colorAw,
                                         "hex",
                                         scheme.bg,
                                         scheme.text,
                                         scheme.fg,
                                         kHexPromptFont,
                                         true,
                                         6);

                if (safeThis != nullptr)
                {
                    fitAlertWindowToEditor (*colorAw, safeThis.getComponent(), [&] (juce::AlertWindow& a)
                    {
                        layoutAlertWindowButtons (a);
                        preparePromptTextEditor (a,
                                                 "hex",
                                                 scheme.bg,
                                                 scheme.text,
                                                 scheme.fg,
                                                 kHexPromptFont,
                                                 true,
                                                 6);
                    });

                    embedAlertWindowInOverlay (safeThis.getComponent(), colorAw, true);
                }
                else
                {
                    colorAw->centreAroundComponent (safeThis.getComponent(), colorAw->getWidth(), colorAw->getHeight());
                    bringPromptWindowToFront (*colorAw);
                    if (safeThis != nullptr && safeThis->tooltipWindow)
                        safeThis->tooltipWindow->toFront (true);
                    colorAw->repaint();
                }

                // Synchronously ensure prompt text editor styling is applied so
                // the prompt appears correctly before being shown.
                preparePromptTextEditor (*colorAw,
                                         "hex",
                                         scheme.bg,
                                         scheme.text,
                                         scheme.fg,
                                         kHexPromptFont,
                                         true,
                                         6);

                // Lightweight async: ensure window is on top and repainted,
                // but avoid re-applying layout synchronously to prevent jumps.
                juce::Component::SafePointer<juce::AlertWindow> safeColorAw (colorAw);
                juce::MessageManager::callAsync ([safeColorAw]()
                {
                    if (safeColorAw == nullptr)
                        return;
                    bringPromptWindowToFront (*safeColorAw);
                    safeColorAw->repaint();
                });

                colorAw->enterModalState (true,
                    juce::ModalCallbackFunction::create ([safeThis, safeAw, colorAw, i] (int result) mutable
                    {
                        std::unique_ptr<juce::AlertWindow> killer (colorAw);
                        if (safeThis == nullptr)
                            return;

                        if (result != 1)
                            return;

                        juce::Colour parsed;
                        if (! tryParseHexColour (killer->getTextEditorContents ("hex"), parsed))
                            return;

                        safeThis->customPalette[(size_t) i] = parsed;
                        safeThis->audioProcessor.setUiCustomPaletteColour (i, parsed);
                        if (safeThis->useCustomPalette)
                        {
                            safeThis->applyActivePalette();
                            safeThis->repaint();
                        }

                        if (safeAw != nullptr)
                        {
                            syncGraphicsPopupState (*safeAw, safeThis->defaultPalette, safeThis->customPalette, safeThis->useCustomPalette);
                            layoutGraphicsPopupContent (*safeAw);
                            safeAw->repaint();
                        }
                    }));
            };
        }
    }

    applyPromptShellSize (*aw);
    syncGraphicsPopupState (*aw, defaultPalette, customPalette, useCustomPalette);
    layoutGraphicsPopupContent (*aw);

    // If we're embedding the prompt inside the editor and the editor is
    // narrower than the default prompt width, shrink the prompt to fit and
    // re-run the layout so nothing is positioned outside the visible area.
    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [&] (juce::AlertWindow& a)
        {
            syncGraphicsPopupState (a, defaultPalette, customPalette, useCustomPalette);
            layoutGraphicsPopupContent (a);
        });
    }
    if (safeThis != nullptr)
    {
        embedAlertWindowInOverlay (safeThis.getComponent(), aw);

        juce::MessageManager::callAsync ([safeAw, safeThis]()
        {
            if (safeAw == nullptr || safeThis == nullptr)
                return;

            safeAw->toFront (false);
            safeAw->repaint();
        });
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
        aw->repaint();
    }

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, aw] (int) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);
            if (safeThis != nullptr)
                safeThis->setPromptOverlayActive (false);
        }));
}

//========================== Text helpers ==========================

juce::String ECHOTRAudioProcessorEditor::getTimeText() const
{
    // When user is interacting with the time slider, always show the manual
    // time value (not the MIDI note) so they can see what they're adjusting.
    if (cachedMidiDisplay.isNotEmpty() && ! timeSlider.isMouseButtonDown())
        return cachedMidiDisplay;

    const bool isSyncOn = syncButton.getToggleState();
    if (isSyncOn)
    {
        const int idx = (int) timeSlider.getValue();
        return audioProcessor.getTimeSyncName (idx);
    }
    
    const float ms = (float) timeSlider.getValue();
    if (ms >= 1000.0f)
        return juce::String (ms / 1000.0f, 3) + " s TIME";
    return juce::String ((int) std::lround (ms)) + " ms TIME";
}

juce::String ECHOTRAudioProcessorEditor::getTimeTextShort() const
{
    if (cachedMidiDisplay.isNotEmpty() && ! timeSlider.isMouseButtonDown())
        return cachedMidiDisplay;

    const bool isSyncOn = syncButton.getToggleState();
    if (isSyncOn)
    {
        const int idx = (int) timeSlider.getValue();
        return audioProcessor.getTimeSyncName (idx);
    }
    
    const float ms = (float) timeSlider.getValue();
    if (ms >= 1000.0f)
        return juce::String (ms / 1000.0f, 3) + "s";
    return juce::String ((int) std::lround (ms)) + "ms";
}

juce::String ECHOTRAudioProcessorEditor::getFeedbackText() const
{
    const int pct = (int) std::lround (feedbackSlider.getValue() * 100.0);
    return juce::String (pct) + "% FEEDBACK";
}

juce::String ECHOTRAudioProcessorEditor::getFeedbackTextShort() const
{
    const int pct = (int) std::lround (feedbackSlider.getValue() * 100.0);
    return juce::String (pct) + "% FBK";
}

juce::String ECHOTRAudioProcessorEditor::getModeText() const
{
    const int mode = (int) modeSlider.getValue();
    switch (mode)
    {
        case 0: return "MONO STYLE";
        case 1: return "STEREO STYLE";
        case 2: return "WIDE STYLE";
        case 3: return "PING-PONG STYLE";
        default: return "STEREO STYLE";
    }
}

juce::String ECHOTRAudioProcessorEditor::getModeTextShort() const
{
    const int mode = (int) modeSlider.getValue();
    switch (mode)
    {
        case 0: return "MONO";
        case 1: return "STEREO";
        case 2: return "WIDE";
        case 3: return "PING-PONG";
        default: return "STEREO";
    }
}

juce::String ECHOTRAudioProcessorEditor::getModText() const
{
    const float mult = (float) modSliderToMultiplier (modSlider.getValue());
    if (std::abs (mult - 1.0f) < kMultEpsilon)
        return "X1 MOD";
    return "X" + juce::String (mult, 2) + " MOD";
}

juce::String ECHOTRAudioProcessorEditor::getModTextShort() const
{
    const float mult = (float) modSliderToMultiplier (modSlider.getValue());
    if (std::abs (mult - 1.0f) < kMultEpsilon)
        return "X1";
    return "X" + juce::String (mult, 2);
}

juce::String ECHOTRAudioProcessorEditor::getInputText() const
{
    const float db = (float) inputSlider.getValue();
    if (db <= kSilenceDb)
        return "-INF dB INPUT";
    if (std::abs (db) < 0.05f)
        return "0 dB INPUT";
    return juce::String (db, 1) + " dB INPUT";
}

juce::String ECHOTRAudioProcessorEditor::getInputTextShort() const
{
    const float db = (float) inputSlider.getValue();
    if (db <= kSilenceDb)
        return "-INF dB IN";
    if (std::abs (db) < 0.05f)
        return "0 dB IN";
    return juce::String (db, 1) + " dB IN";
}

juce::String ECHOTRAudioProcessorEditor::getOutputText() const
{
    const float db = (float) outputSlider.getValue();
    if (db <= kSilenceDb)
        return "-INF dB OUTPUT";
    if (std::abs (db) < 0.05f)
        return "0 dB OUTPUT";
    return juce::String (db, 1) + " dB OUTPUT";
}

juce::String ECHOTRAudioProcessorEditor::getOutputTextShort() const
{
    const float db = (float) outputSlider.getValue();
    if (db <= kSilenceDb)
        return "-INF dB OUT";
    if (std::abs (db) < 0.05f)
        return "0 dB OUT";
    return juce::String (db, 1) + " dB OUT";
}

juce::String ECHOTRAudioProcessorEditor::getMixText() const
{
    const int pct = (int) std::lround (mixSlider.getValue() * 100.0);
    return juce::String (pct) + "% MIX";
}

juce::String ECHOTRAudioProcessorEditor::getMixTextShort() const
{
    const int pct = (int) std::lround (mixSlider.getValue() * 100.0);
    return juce::String (pct) + "%";
}

namespace
{
    constexpr const char* kTimeLegendFull  = "5000 ms TIME";
    constexpr const char* kTimeLegendShort = "5000ms";  // Glued value+unit (wider than "5.000s")
    constexpr const char* kTimeLegendInt   = "5000";

    constexpr const char* kFeedbackLegendFull  = "100% FEEDBACK";
    constexpr const char* kFeedbackLegendShort = "100% FBK";
    constexpr const char* kFeedbackLegendInt   = "100%";

    constexpr const char* kModeLegendFull  = "PING-PONG STYLE";
    constexpr const char* kModeLegendShort = "PING-PONG";  // Value-only (worst-case width)
    constexpr const char* kModeLegendInt   = "P-P";

    constexpr const char* kModLegendFull  = "X4.00 MOD";
    constexpr const char* kModLegendShort = "X4.00";
    constexpr const char* kModLegendInt   = "X4";

    constexpr const char* kInputLegendFull  = "-100.0 dB INPUT";
    constexpr const char* kInputLegendShort = "-100.0 dB IN";
    constexpr const char* kInputLegendInt   = "-100.0dB";

    constexpr const char* kOutputLegendFull  = "-100.0 dB OUTPUT";
    constexpr const char* kOutputLegendShort = "-100.0 dB OUT";
    constexpr const char* kOutputLegendInt   = "-100.0dB";

    constexpr const char* kMixLegendFull  = "100% MIX";
    constexpr const char* kMixLegendShort = "100%";
    constexpr const char* kMixLegendInt   = "100%";

    constexpr int kValueAreaHeightPx = 44;
    constexpr int kValueAreaRightMarginPx = 24;
    constexpr int kToggleLabelGapPx = 4;
    constexpr int kResizerCornerPx = 22;
    constexpr int kToggleBoxPx = 72;
    constexpr int kMinToggleBlocksGapPx = 10;
    constexpr int kMinSliderGapPx = 4;
}

ECHOTRAudioProcessorEditor::HorizontalLayoutMetrics
ECHOTRAudioProcessorEditor::buildHorizontalLayout (int editorW, int valueColW)
{
    HorizontalLayoutMetrics m;
    m.barW = (int) std::round (editorW * 0.455);
    m.valuePad = (int) std::round (editorW * 0.02);
    m.valueW = valueColW;
    m.contentW = m.barW + m.valuePad + m.valueW;
    m.leftX = juce::jmax (6, (editorW - m.contentW) / 2);
    return m;
}

ECHOTRAudioProcessorEditor::VerticalLayoutMetrics
ECHOTRAudioProcessorEditor::buildVerticalLayout (int editorH, int biasY, bool ioExpanded)
{
    VerticalLayoutMetrics m;
    m.rhythm = juce::jlimit (6, 16, (int) std::round (editorH * 0.018));
    const int nominalBarH = juce::jlimit (14, 120, m.rhythm * 6);
    const int nominalGapY = juce::jmax (4, m.rhythm * 4);

    m.titleH = juce::jlimit (24, 56, m.rhythm * 4);
    m.titleAreaH = m.titleH + 4;
    const int computedTitleTopPad = 6 + biasY;
    m.titleTopPad = (computedTitleTopPad > 8) ? computedTitleTopPad : 8;
    const int titleGap = m.titleTopPad;
    m.topMargin = m.titleTopPad + m.titleAreaH + titleGap;
    m.betweenSlidersAndButtons = juce::jmax (8, m.rhythm * 2);
    m.bottomMargin = m.titleTopPad;

    m.box = juce::jlimit (40, kToggleBoxPx, (int) std::round (editorH * 0.085));
    m.btnRowGap = juce::jlimit (4, 14, (int) std::round (editorH * 0.008));
    m.btnRow2Y = editorH - m.bottomMargin - m.box;
    m.btnRow1Y = m.btnRow2Y - m.btnRowGap - m.box;
    m.availableForSliders = juce::jmax (40, m.btnRow1Y - m.betweenSlidersAndButtons - m.topMargin);

    // Expanded: 7 bars + 8 gaps (6 inter-slider + 2 toggle padding).
    // Collapsed: 4 bars + 4 gaps (3 inter-slider + 1 toggle-to-slider).
    //            The toggle bar (20px fixed) is subtracted from available space
    //            BEFORE scaling so bars/gaps don't overflow.
    const int numSliders = ioExpanded ? 7 : 4;
    const int numGaps    = ioExpanded ? 8 : 4;

    m.toggleBarH = 20;  // fixed visual height for click area
    const int spaceForScale = ioExpanded ? m.availableForSliders
                                         : juce::jmax (40, m.availableForSliders - m.toggleBarH);

    const int nominalStack = numSliders * nominalBarH + numGaps * nominalGapY;
    const double stackScale = nominalStack > 0 ? juce::jmin (1.0, (double) spaceForScale / (double) nominalStack)
                                               : 1.0;

    m.barH = juce::jmax (14, (int) std::round (nominalBarH * stackScale));
    m.gapY = juce::jmax (4,  (int) std::round (nominalGapY * stackScale));

    auto stackHeight = [&]() { return numSliders * m.barH + numGaps * m.gapY; };

    while (stackHeight() > spaceForScale && m.gapY > 4)
        --m.gapY;

    while (stackHeight() > spaceForScale && m.barH > 14)
        --m.barH;

    m.topY = m.topMargin;

    if (ioExpanded)
        m.toggleBarY = m.topY + 3 * m.barH + 3 * m.gapY;
    else
        m.toggleBarY = m.topY;  // collapsed: toggle bar flush with content top

    return m;
}

void ECHOTRAudioProcessorEditor::updateCachedLayout()
{
    cachedHLayout_ = buildHorizontalLayout (getWidth(), getTargetValueColumnWidth());
    cachedVLayout_ = buildVerticalLayout (getHeight(), kLayoutVerticalBiasPx, ioSectionExpanded_);

    const juce::Slider* sliders[7] = { &timeSlider, &modSlider, &feedbackSlider, &modeSlider,
                                        &inputSlider, &outputSlider, &mixSlider };

    for (int i = 0; i < 7; ++i)
    {
        if (! sliders[i]->isVisible())
        {
            cachedValueAreas_[(size_t) i] = {};
            continue;
        }

        const auto& bb = sliders[i]->getBounds();
        const int valueX = bb.getRight() + cachedHLayout_.valuePad;
        const int maxW = juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx);
        const int vw   = juce::jmin (cachedHLayout_.valueW, maxW);
        const int y    = bb.getCentreY() - (kValueAreaHeightPx / 2);
        cachedValueAreas_[(size_t) i] = { valueX, y, juce::jmax (0, vw), kValueAreaHeightPx };
    }

    // Cache toggle bar area
    cachedToggleBarArea_ = { cachedHLayout_.leftX, cachedVLayout_.toggleBarY,
                             cachedHLayout_.contentW, cachedVLayout_.toggleBarH };
}

int ECHOTRAudioProcessorEditor::getTargetValueColumnWidth() const
{
    std::uint64_t key = 1469598103934665603ull;
    auto mix = [&] (std::uint64_t v)
    {
        key ^= v;
        key *= 1099511628211ull;
    };

    mix ((std::uint64_t) getWidth());

    if (key == cachedValueColumnWidthKey)
        return cachedValueColumnWidth;

    const auto& font = kBoldFont40();

    const int timeMaxW = juce::jmax (stringWidth (font, kTimeLegendFull),
                                     juce::jmax (stringWidth (font, kTimeLegendShort),
                                                 stringWidth (font, kTimeLegendInt)));

    const int feedbackMaxW = juce::jmax (stringWidth (font, kFeedbackLegendFull),
                                         juce::jmax (stringWidth (font, kFeedbackLegendShort),
                                                     stringWidth (font, kFeedbackLegendInt)));

    const int modeMaxW = juce::jmax (stringWidth (font, kModeLegendFull),
                                     juce::jmax (stringWidth (font, kModeLegendShort),
                                                 stringWidth (font, kModeLegendInt)));

    const int modMaxW = juce::jmax (stringWidth (font, kModLegendFull),
                                    juce::jmax (stringWidth (font, kModLegendShort),
                                                stringWidth (font, kModLegendInt)));

    const int inputMaxW = juce::jmax (stringWidth (font, kInputLegendFull),
                                      juce::jmax (stringWidth (font, kInputLegendShort),
                                                  stringWidth (font, kInputLegendInt)));

    const int outputMaxW = juce::jmax (stringWidth (font, kOutputLegendFull),
                                       juce::jmax (stringWidth (font, kOutputLegendShort),
                                                   stringWidth (font, kOutputLegendInt)));

    const int mixMaxW = juce::jmax (stringWidth (font, kMixLegendFull),
                                    juce::jmax (stringWidth (font, kMixLegendShort),
                                                stringWidth (font, kMixLegendInt)));

    const int maxW = juce::jmax (juce::jmax (juce::jmax (timeMaxW, feedbackMaxW), juce::jmax (modeMaxW, modMaxW)),
                                 juce::jmax (juce::jmax (inputMaxW, outputMaxW), mixMaxW));

    const int desired = maxW + 16;
    const int minW = 90;
    // Allow up to 40% of editor width for longer legends (INPUT/OUTPUT with dB units)
    const int maxAllowed = juce::jmax (minW, (int) std::round (getWidth() * 0.40));
    cachedValueColumnWidth = juce::jlimit (minW, maxAllowed, desired);
    cachedValueColumnWidthKey = key;
    return cachedValueColumnWidth;
}

//========================== Hit areas ==========================

juce::Rectangle<int> ECHOTRAudioProcessorEditor::getValueAreaFor (const juce::Rectangle<int>& barBounds) const
{
    const int valueX = barBounds.getRight() + cachedHLayout_.valuePad;
    const int maxW = juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx);
    const int valueW = juce::jmin (cachedHLayout_.valueW, maxW);

    const int y = barBounds.getCentreY() - (kValueAreaHeightPx / 2);
    return { valueX, y, juce::jmax (0, valueW), kValueAreaHeightPx };
}

juce::Slider* ECHOTRAudioProcessorEditor::getSliderForValueAreaPoint (juce::Point<int> p)
{
    juce::Slider* sliders[7] = { &timeSlider, &modSlider, &feedbackSlider, &modeSlider,
                                  &inputSlider, &outputSlider, &mixSlider };

    for (int i = 0; i < 7; ++i)
        if (cachedValueAreas_[(size_t) i].contains (p))
            return sliders[i];

    return nullptr;
}

namespace
{
    int getToggleVisualBoxSidePx (const juce::Component& button)
    {
        const int h = button.getHeight();
        return juce::jlimit (14, juce::jmax (14, h - 2), (int) std::lround ((double) h * 0.65));
    }

    int getToggleVisualBoxLeftPx (const juce::Component& button)
    {
        return button.getX() + 2;
    }

    juce::Rectangle<int> makeToggleLabelArea (const juce::Component& button,
                                              int collisionRight,
                                              const juce::String& fullLabel,
                                              const juce::String& shortLabel)
    {
        const auto b = button.getBounds();
        const int visualRight = getToggleVisualBoxLeftPx (button) + getToggleVisualBoxSidePx (button);
        const int x = visualRight + kToggleLabelGapPx;

        const auto& labelFont = kBoldFont40();
        const int fullW  = stringWidth (labelFont, fullLabel) + 2;
        const int shortW = stringWidth (labelFont, shortLabel) + 2;
        const int maxW   = juce::jmax (0, collisionRight - x);

        const int w = (fullW <= maxW) ? fullW : juce::jmin (shortW, maxW);
        return { x, b.getY(), w, b.getHeight() };
    }

    juce::String chooseToggleLabel (const juce::Component& button,
                                   int collisionRight,
                                   const juce::String& fullLabel,
                                   const juce::String& shortLabel)
    {
        const int visualRight = getToggleVisualBoxLeftPx (button) + getToggleVisualBoxSidePx (button);
        const int x = visualRight + kToggleLabelGapPx;
        const auto& labelFont = kBoldFont40();
        const int fullW = stringWidth (labelFont, fullLabel) + 2;
        return (fullW <= juce::jmax (0, collisionRight - x)) ? fullLabel : shortLabel;
    }
}

juce::Rectangle<int> ECHOTRAudioProcessorEditor::getSyncLabelArea() const
{
    return makeToggleLabelArea (syncButton, autoFbkButton.getX() - kToggleLegendCollisionPadPx, "SYNC", "SYN");
}

juce::Rectangle<int> ECHOTRAudioProcessorEditor::getAutoFbkLabelArea() const
{
    return makeToggleLabelArea (autoFbkButton, getWidth() - kToggleLegendCollisionPadPx, "AUTO FBK", "AUTO");
}

juce::Rectangle<int> ECHOTRAudioProcessorEditor::getReverseLabelArea() const
{
    return makeToggleLabelArea (reverseButton, midiButton.getX() - kToggleLegendCollisionPadPx, "REVERSE", "RVS");
}

juce::Rectangle<int> ECHOTRAudioProcessorEditor::getMidiLabelArea() const
{
    return makeToggleLabelArea (midiButton, getWidth() - kToggleLegendCollisionPadPx, "MIDI", "MD");
}

juce::Rectangle<int> ECHOTRAudioProcessorEditor::getInfoIconArea() const
{
    int contentRight = 0;
    for (size_t i = 0; i < cachedValueAreas_.size(); ++i)
    {
        if (! cachedValueAreas_[i].isEmpty())
        {
            contentRight = cachedValueAreas_[i].getRight();
            break;
        }
    }
    if (contentRight <= 0)
        contentRight = getWidth() - 8;

    const int titleH = cachedVLayout_.titleH;
    const int titleY = cachedVLayout_.titleTopPad;
    const int titleAreaH = cachedVLayout_.titleAreaH;
    const int size = juce::jlimit (20, 36, titleH);

    const int x = contentRight - size;
    const int y = titleY + juce::jmax (0, (titleAreaH - size) / 2);
    return { x, y, size, size };
}

void ECHOTRAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    lastUserInteractionMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    const auto p = e.getEventRelativeTo (this).getPosition();

    // Toggle IO section expand/collapse
    if (cachedToggleBarArea_.contains (p))
    {
        ioSectionExpanded_ = ! ioSectionExpanded_;
        audioProcessor.setUiIoExpanded (ioSectionExpanded_);
        resized();
        repaint();
        return;
    }

    // (MIDI channel prompt is now handled via right-click on the MIDI label area below)

    if (e.mods.isPopupMenu())
    {
        if (auto* slider = getSliderForValueAreaPoint (p))
        {
            openNumericEntryPopupForSlider (*slider);
            return;
        }
    }

    {
        auto infoArea = getInfoIconArea();
        if (crtEnabled)
            infoArea = infoArea.expanded (4, 0);  // small pad for CRT distortion
        if (infoArea.contains (p))
        {
            openInfoPopup();
            return;
        }
    }

    if (getSyncLabelArea().contains (p))
    {
        syncButton.setToggleState (! syncButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    if (getAutoFbkLabelArea().contains (p) || autoFbkDisplay.getBounds().contains (p))
    {
        if (e.mods.isPopupMenu())
            openAutoFbkPrompt();
        else
            autoFbkButton.setToggleState (! autoFbkButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    if (getReverseLabelArea().contains (p) || reverseDisplay.getBounds().contains (p))
    {
        if (e.mods.isPopupMenu())
            openReverseSmoothPrompt();
        else
            reverseButton.setToggleState (! reverseButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    if (getMidiLabelArea().contains (p) || midiChannelDisplay.getBounds().contains (p))
    {
        if (e.mods.isPopupMenu())
            openMidiChannelPrompt();
        else
            midiButton.setToggleState (! midiButton.getToggleState(), juce::sendNotificationSync);
        return;
    }
}

void ECHOTRAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    juce::ignoreUnused (e);
    lastUserInteractionMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
}

void ECHOTRAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();

    if (auto* slider = getSliderForValueAreaPoint (p))
    {
        if (slider == &timeSlider)            slider->setValue (kDefaultTimeMs, juce::sendNotificationSync);
        else if (slider == &feedbackSlider)   slider->setValue (kDefaultFeedback, juce::sendNotificationSync);
        else if (slider == &modeSlider)       slider->setValue (0.0, juce::sendNotificationSync);
        else if (slider == &modSlider)        slider->setValue (1.0, juce::sendNotificationSync);
        else if (slider == &inputSlider)      slider->setValue (kDefaultInput, juce::sendNotificationSync);
        else if (slider == &outputSlider)     slider->setValue (kDefaultOutput, juce::sendNotificationSync);
        else if (slider == &mixSlider)        slider->setValue (kDefaultMix, juce::sendNotificationSync);
        return;
    }
}

//==============================================================================

void ECHOTRAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int W = getWidth();
    const auto& horizontalLayout = cachedHLayout_;
    const auto& verticalLayout   = cachedVLayout_;

    const auto scheme = activeScheme;

    g.fillAll (scheme.bg);
    g.setColour (scheme.text);

    constexpr float baseFontPx = 40.0f;
    constexpr float minFontPx  = 18.0f;
    constexpr float fullShrinkFloor = baseFontPx * 0.75f;
    g.setFont (kBoldFont40());

    // Per-label cascading: tries to draw the legend at a given shrink floor.
    // Returns true if drawn, false if it didn't fit.
    auto tryDrawLegend = [&] (const juce::Rectangle<int>& area,
                              const juce::String& text,
                              float shrinkFloor) -> bool
    {
        auto t = text.trim();
        if (t.isEmpty() || area.getWidth() <= 2 || area.getHeight() <= 2)
            return false;

        const int split = t.lastIndexOfChar (' ');
        if (split <= 0 || split >= t.length() - 1)
        {
            g.setFont (kBoldFont40());
            return drawIfFitsWithOptionalShrink (g, area, t, baseFontPx, shrinkFloor);
        }

        const auto value  = t.substring (0, split).trimEnd();
        const auto suffix = t.substring (split + 1).trimStart();

        g.setFont (kBoldFont40());
        if (drawValueWithRightAlignedSuffix (g, area, value, suffix, false,
                                              baseFontPx, shrinkFloor))
        {
            g.setColour (scheme.text);
            return true;
        }
        return false;
    };

    // Cascade: full (generous floor) → short (aggressive floor) → int-only.
    auto drawLegendForMode = [&] (const juce::Rectangle<int>& area,
                                  const juce::String& fullLegend,
                                  const juce::String& shortLegend,
                                  const juce::String& intOnlyLegend)
    {
        if (tryDrawLegend (area, fullLegend, fullShrinkFloor))
            return;

        if (tryDrawLegend (area, shortLegend, minFontPx))
            return;

        g.setFont (kBoldFont40());
        drawValueNoEllipsis (g, area, intOnlyLegend, juce::String(), intOnlyLegend, baseFontPx, minFontPx);
        g.setColour (scheme.text);
    };

    {
        const int titleH = verticalLayout.titleH;

        const int barW = horizontalLayout.barW;
        const int contentW = horizontalLayout.contentW;
        const int leftX = horizontalLayout.leftX;

        const int titleX = juce::jlimit (0, juce::jmax (0, W - 1), leftX);
        const int titleW = juce::jmax (0, juce::jmin (contentW, W - titleX));
        const int titleY = verticalLayout.titleTopPad;

        auto titleFont = g.getCurrentFont();
        titleFont.setHeight ((float) titleH);
        g.setFont (titleFont);

        const auto titleArea = juce::Rectangle<int> (titleX, titleY, titleW, titleH + kTitleAreaExtraHeightPx);
        const juce::String titleText ("ECHO-TR");

        g.drawText (titleText, titleArea.getX(), titleArea.getY(), titleArea.getWidth(), titleArea.getHeight(), juce::Justification::left, false);

        // If horizontal space is too tight, fix only the base title text by overdrawing a fitted version.
        const auto infoIconArea = getInfoIconArea();
        const int titleRightLimit = infoIconArea.getX() - kTitleRightGapToInfoPx;
        const int titleMaxW = juce::jmax (0, titleRightLimit - titleArea.getX());
        const int titleBaseW = stringWidth (titleFont, titleText);
        const int originalTitleLimitW = juce::jmax (0, juce::jmin (titleW, barW));
        const bool originalWouldClipTitle = titleBaseW > originalTitleLimitW;

        if (titleMaxW > 0 && (originalWouldClipTitle || titleBaseW > titleMaxW))
        {
            auto fittedTitleFont = titleFont;
            fittedTitleFont.setHorizontalScale (1.0f);
            const float titleMinScale = juce::jlimit (0.4f, 1.0f, 12.0f / (float) titleH);
            for (float s = 1.0f; s >= titleMinScale; s -= 0.025f)
            {
                fittedTitleFont.setHorizontalScale (s);
                if (stringWidth (fittedTitleFont, titleText) <= titleMaxW)
                    break;
            }

            g.setColour (scheme.text);
            g.setFont (fittedTitleFont);
            g.drawText (titleText, titleArea.getX(), titleArea.getY(), titleMaxW, titleArea.getHeight(), juce::Justification::left, false);
        }

        g.setColour (scheme.text);

        auto versionFont = juce::Font (juce::FontOptions (juce::jmax (10.0f, (float) titleH * UiMetrics::versionFontRatio)).withStyle ("Bold"));
        g.setFont (versionFont);

        const int versionH = juce::jlimit (10, infoIconArea.getHeight(), (int) std::round ((double) infoIconArea.getHeight() * UiMetrics::versionHeightRatio));
        const int versionY = infoIconArea.getBottom() - versionH;

        const int desiredVersionW = juce::jlimit (28, 64, (int) std::round ((double) infoIconArea.getWidth() * UiMetrics::versionDesiredWidthRatio));
        const int versionRight = infoIconArea.getX() - kVersionGapPx;
        const int versionLeftLimit = titleArea.getX();
        const int versionX = juce::jmax (versionLeftLimit, versionRight - desiredVersionW);
        const int versionW = juce::jmax (0, versionRight - versionX);

        if (versionW > 0)
            g.drawText (juce::String ("v") + InfoContent::version,
                        versionX, versionY, versionW, versionH,
                        juce::Justification::bottomRight, false);

        g.setFont (kBoldFont40());
    }

    // ── Toggle bar (triangle + rounded horizontal bar) ──
    {
        if (! cachedToggleBarArea_.isEmpty())
        {
            const float barRadius = (float) cachedToggleBarArea_.getHeight() * 0.3f;
            g.setColour (scheme.fg.withAlpha (0.25f));
            g.fillRoundedRectangle (cachedToggleBarArea_.toFloat(), barRadius);

            // Triangle indicator — centered, palette-coloured
            const float triH = (float) cachedToggleBarArea_.getHeight() * 0.8f;
            const float triW = triH * 1.125f;
            const float cx = (float) cachedToggleBarArea_.getCentreX();
            const float cy = (float) cachedToggleBarArea_.getCentreY();

            juce::Path tri;
            if (ioSectionExpanded_)
            {
                // ▲ pointing up (collapse)
                tri.addTriangle (cx - triW * 0.5f, cy + triH * 0.35f,
                                 cx + triW * 0.5f, cy + triH * 0.35f,
                                 cx,               cy - triH * 0.35f);
            }
            else
            {
                // ▼ pointing down (expand)
                tri.addTriangle (cx - triW * 0.5f, cy - triH * 0.35f,
                                 cx + triW * 0.5f, cy - triH * 0.35f,
                                 cx,               cy + triH * 0.35f);
            }
            g.setColour (scheme.text);
            g.fillPath (tri);
        }
    }

    g.setColour (scheme.text);

    {
        const juce::String* fullTexts[7]  = { &cachedTimeTextFull, &cachedModTextFull, &cachedFeedbackTextFull,
                                               &cachedModeTextFull, &cachedInputTextFull, &cachedOutputTextFull, &cachedMixTextFull };
        const juce::String* shortTexts[7] = { &cachedTimeTextShort, &cachedModTextShort, &cachedFeedbackTextShort,
                                               &cachedModeTextShort, &cachedInputTextShort, &cachedOutputTextShort, &cachedMixTextShort };
        const juce::String intTexts[7] = {
            juce::String ((int) timeSlider.getValue()),
            juce::String ((int) modSlider.getValue()),
            juce::String ((int) std::lround (feedbackSlider.getValue() * 100.0)) + "%",
            juce::String ((int) modeSlider.getValue()),
            juce::String ((int) inputSlider.getValue()) + "dB",
            juce::String ((int) outputSlider.getValue()) + "dB",
            juce::String ((int) std::lround (mixSlider.getValue() * 100.0)) + "%"
        };

        for (int i = 0; i < 7; ++i)
            drawLegendForMode (cachedValueAreas_[(size_t) i], *fullTexts[i], *shortTexts[i], intTexts[i]);
    }

    {
        // Determine which labels to use based on available space INCLUDING collision bounds
        const auto& labelFont = kBoldFont40();

        // Restore the correct font on g — previous drawLegendForMode calls may have
        // compressed it via drawIfFitsWithOptionalShrink, leaving a residual scale.
        // All area/position calculations (resized, getLabelArea) use this same 40px Bold.
        g.setFont (labelFont);
        
        const int syncCR  = autoFbkButton.getX() - kToggleLegendCollisionPadPx;
        const int autoCR  = W - kToggleLegendCollisionPadPx;
        const int revCR  = midiButton.getX() - kToggleLegendCollisionPadPx;
        const int midiCR  = W - kToggleLegendCollisionPadPx;

        const juce::String syncLabel = chooseToggleLabel (syncButton,    syncCR, "SYNC",     "SYN");
        const juce::String autoLabel = chooseToggleLabel (autoFbkButton, autoCR, "AUTO FBK", "AUTO");
        const juce::String revLabel = chooseToggleLabel (reverseButton,    revCR, "REVERSE",     "RVS");
        const juce::String midiLabel = chooseToggleLabel (midiButton,    midiCR, "MIDI",     "MD");
        
        auto drawToggleLegend = [&] (const juce::Rectangle<int>& labelArea,
                                     const juce::String& labelText,
                                     int noCollisionRight)
        {
            const int safeW = juce::jmax (0, noCollisionRight - labelArea.getX());
            // snap to integer/even coordinates to avoid sub-pixel artefacts on resize
            auto snapEven = [] (int v) { return v & ~1; };
            const int ax = snapEven (labelArea.getX());
            const int ay = snapEven (labelArea.getY());
            const int aw = snapEven (safeW);
            const int ah = labelArea.getHeight();
            const auto drawArea = juce::Rectangle<int> (ax, ay, aw, ah);

            g.drawText (labelText, drawArea.getX(), drawArea.getY(), drawArea.getWidth(), drawArea.getHeight(), juce::Justification::left, true);
        };

        // Row 1: SYNC + AUTO FBK
        drawToggleLegend (getSyncLabelArea(),    syncLabel, syncCR);
        drawToggleLegend (getAutoFbkLabelArea(), autoLabel, autoCR);

        // Row 2: REV + MIDI
        drawToggleLegend (getReverseLabelArea(), revLabel, revCR);
        drawToggleLegend (getMidiLabelArea(), midiLabel, midiCR);
    }
    
    g.setColour (scheme.text);

    {
        if (cachedInfoGearPath.isEmpty())
            updateInfoIconCache();

        // filled white gear + center cutout
        g.setColour (scheme.text);
        g.fillPath (cachedInfoGearPath);
        g.strokePath (cachedInfoGearPath, juce::PathStrokeType (1.0f));

        g.setColour (scheme.bg);
        g.fillEllipse (cachedInfoGearHole);
    }



}

// ── CRT / VHS overlay — now handled entirely by CrtEffect (ImageEffectFilter).
// paintOverChildren is intentionally empty so the source image captured by
// the effect filter contains only the clean GUI render.
void ECHOTRAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    juce::ignoreUnused (g);
}

// ── CRT overlay generation (called from resized) ──────────────────────
void ECHOTRAudioProcessorEditor::updateInfoIconCache()
{
    const auto iconArea = getInfoIconArea();
    const auto iconF = iconArea.toFloat();
    const auto center = iconF.getCentre();
    const float toothTipR = (float) iconArea.getWidth() * 0.47f;
    const float toothRootR = toothTipR * 0.78f;
    const float holeR = toothTipR * 0.40f;
    constexpr int teeth = 8;

    cachedInfoGearPath.clear();
    for (int i = 0; i < teeth * 2; ++i)
    {
        const float a = -juce::MathConstants<float>::halfPi
                      + (juce::MathConstants<float>::pi * (float) i / (float) teeth);
        const float r = (i % 2 == 0) ? toothTipR : toothRootR;
        const float x = center.x + std::cos (a) * r;
        const float y = center.y + std::sin (a) * r;

        if (i == 0)
            cachedInfoGearPath.startNewSubPath (x, y);
        else
            cachedInfoGearPath.lineTo (x, y);
    }
    cachedInfoGearPath.closeSubPath();
    cachedInfoGearHole = { center.x - holeR, center.y - holeR, holeR * 2.0f, holeR * 2.0f };
}

void ECHOTRAudioProcessorEditor::resized()
{
    refreshLegendTextCache();

    // If the user is actively dragging/resizing (mouse down), treat this
    // as a recent user interaction so size persistence will occur immediately.
    if (! suppressSizePersistence)
    {
        if (juce::ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown()
            || juce::Desktop::getInstance().getMainMouseSource().isDragging())
        {
            lastUserInteractionMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
        }
    }

    const int W = getWidth();
    const int H = getHeight();

    if (! suppressSizePersistence)
    {
        const uint32_t last = lastUserInteractionMs.load (std::memory_order_relaxed);
        const uint32_t now = juce::Time::getMillisecondCounter();
        const bool userRecent = (now - last) <= (uint32_t) kUserInteractionPersistWindowMs;
        if ((W != lastPersistedEditorW || H != lastPersistedEditorH) && userRecent)
        {
            audioProcessor.setUiEditorSize (W, H);
            lastPersistedEditorW = W;
            lastPersistedEditorH = H;
        }
    }

    const auto horizontalLayout = buildHorizontalLayout (W, getTargetValueColumnWidth());
    const auto verticalLayout = buildVerticalLayout (H, kLayoutVerticalBiasPx, ioSectionExpanded_);

    // Position sliders based on IO section expanded/collapsed state
    if (ioSectionExpanded_)
    {
        // Expanded: INPUT, OUTPUT, MIX → [toggle bar] → TIME, MOD, FEEDBACK, STYLE
        int y = verticalLayout.topY;
        inputSlider.setBounds  (horizontalLayout.leftX, y, horizontalLayout.barW, verticalLayout.barH);  y += verticalLayout.barH + verticalLayout.gapY;
        outputSlider.setBounds (horizontalLayout.leftX, y, horizontalLayout.barW, verticalLayout.barH);  y += verticalLayout.barH + verticalLayout.gapY;
        mixSlider.setBounds    (horizontalLayout.leftX, y, horizontalLayout.barW, verticalLayout.barH);  y += verticalLayout.barH;
        // padding + toggle bar + padding
        y += verticalLayout.gapY;  // padding above
        y += verticalLayout.gapY;  // toggle bar
        y += verticalLayout.gapY;  // padding below
        timeSlider.setBounds     (horizontalLayout.leftX, y, horizontalLayout.barW, verticalLayout.barH);  y += verticalLayout.barH + verticalLayout.gapY;
        modSlider.setBounds      (horizontalLayout.leftX, y, horizontalLayout.barW, verticalLayout.barH);  y += verticalLayout.barH + verticalLayout.gapY;
        feedbackSlider.setBounds (horizontalLayout.leftX, y, horizontalLayout.barW, verticalLayout.barH);  y += verticalLayout.barH + verticalLayout.gapY;
        modeSlider.setBounds     (horizontalLayout.leftX, y, horizontalLayout.barW, verticalLayout.barH);

        inputSlider.setVisible (true);
        outputSlider.setVisible (true);
        mixSlider.setVisible (true);
    }
    else
    {
        // Collapsed: sliders start after toggle bar + gap
        int y = verticalLayout.toggleBarY + verticalLayout.toggleBarH + verticalLayout.gapY;
        timeSlider.setBounds     (horizontalLayout.leftX, y, horizontalLayout.barW, verticalLayout.barH);  y += verticalLayout.barH + verticalLayout.gapY;
        modSlider.setBounds      (horizontalLayout.leftX, y, horizontalLayout.barW, verticalLayout.barH);  y += verticalLayout.barH + verticalLayout.gapY;
        feedbackSlider.setBounds (horizontalLayout.leftX, y, horizontalLayout.barW, verticalLayout.barH);  y += verticalLayout.barH + verticalLayout.gapY;
        modeSlider.setBounds     (horizontalLayout.leftX, y, horizontalLayout.barW, verticalLayout.barH);

        inputSlider.setBounds (0, 0, 0, 0);
        outputSlider.setBounds (0, 0, 0, 0);
        mixSlider.setBounds (0, 0, 0, 0);

        inputSlider.setVisible (false);
        outputSlider.setVisible (false);
        mixSlider.setVisible (false);
    }

    // Button area: 2x2 grid — Row 1: SYNC + AUTO FBK, Row 2: LOOP + MIDI
    const int buttonAreaX = horizontalLayout.leftX;

    const int toggleVisualSide = juce::jlimit (14,
                                               juce::jmax (14, verticalLayout.box - 2),
                                               (int) std::lround ((double) verticalLayout.box * 0.65));
    const int toggleHitW = toggleVisualSide + 6;

    // Each row has 2 buttons: left-anchored + right-anchored
    // Row 1: SYNC (left) + AUTO FBK (right)
    // Row 2: REV (left) + MIDI (right)
    const int leftBlockX = buttonAreaX;
    const int rightBlockX = horizontalLayout.leftX + horizontalLayout.barW + horizontalLayout.valuePad;

    syncButton.setBounds    (leftBlockX,  verticalLayout.btnRow1Y, toggleHitW, verticalLayout.box);
    autoFbkButton.setBounds (rightBlockX, verticalLayout.btnRow1Y, toggleHitW, verticalLayout.box);
    reverseButton.setBounds    (leftBlockX,  verticalLayout.btnRow2Y, toggleHitW, verticalLayout.box);
    midiButton.setBounds    (rightBlockX, verticalLayout.btnRow2Y, toggleHitW, verticalLayout.box);
    
    // Position invisible tooltip overlay on the MIDI label area
    {
        const auto midiLabelRect = getMidiLabelArea();
        midiChannelDisplay.setBounds (midiLabelRect);
    }

    // Position invisible tooltip overlay on the AUTO FBK label area
    {
        const auto autoFbkLabelRect = getAutoFbkLabelArea();
        autoFbkDisplay.setBounds (autoFbkLabelRect);
    }

    // Position invisible tooltip overlay on the REVERSE label area
    {
        const auto reverseLabelRect = getReverseLabelArea();
        reverseDisplay.setBounds (reverseLabelRect);
    }

    if (resizerCorner != nullptr)
        resizerCorner->setBounds (W - kResizerCornerPx, H - kResizerCornerPx, kResizerCornerPx, kResizerCornerPx);

    promptOverlay.setBounds (getLocalBounds());
    if (promptOverlayActive)
        promptOverlay.toFront (false);

    // Cache layout metrics + value areas AFTER sliders are positioned
    updateCachedLayout();

    updateInfoIconCache();
    crtEffect.setResolution (static_cast<float> (W), static_cast<float> (H));

    // Don't modify the constrainer here to avoid reentrancy issues.
}

