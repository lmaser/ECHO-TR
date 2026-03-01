// PluginEditor.cpp
#include "PluginEditor.h"
#include <functional>
#include <unordered_map>
#include <fstream>

//========================== Overflow helpers ==========================
// Helpers para medir texto y aplicar truncados según prioridad de formatos.

static std::unordered_map<std::string, int>& getStringWidthCache()
{
    static thread_local std::unordered_map<std::string, int> widthCache;
    return widthCache;
}

static int stringWidth (const juce::Font& font, const juce::String& s)
{
    auto& widthCache = getStringWidthCache();

    if (s.isEmpty())
        return 0;

    if (widthCache.size() > 2048)
        widthCache.clear();

    const int h100 = (int) std::round (font.getHeight() * 100.0f);
    std::string key;
    key.reserve (32 + (size_t) s.length());
    key += std::to_string (h100);
    key += "|";
    key += font.getTypefaceName().toStdString();
    key += font.isBold() ? "|b1" : "|b0";
    key += font.isItalic() ? "|i1" : "|i0";
    key += "|";
    key += s.toStdString();

    if (const auto it = widthCache.find (key); it != widthCache.end())
        return it->second;

    juce::GlyphArrangement ga;
    ga.addLineOfText (font, s, 0.0f, 0.0f);
    const int width = (int) std::ceil (ga.getBoundingBox (0, -1, true).getWidth());
    widthCache.emplace (std::move (key), width);
    return width;
}

struct GraphicsPromptLayout
{
    static constexpr int toggleBox = 34;
    static constexpr int toggleGap = 10;
    static constexpr int swatchSize = 40;
    static constexpr int swatchGap = 8;
    static constexpr int columnGap = 28;
    static constexpr int titleHeight = 24;
    static constexpr int titleToModeGap = 14;
    static constexpr int modeToSwatchesGap = 14;
};

namespace UiMetrics
{
    constexpr float tickBoxOuterScale = 2.0f;
    constexpr float tickBoxHorizontalBiasRatio = 0.1171875f;
    constexpr float tickBoxInnerInsetRatio = 0.25f;

    constexpr int tooltipMinWidth = 120;
    constexpr int tooltipMinHeight = 38;
    constexpr float tooltipHeightScale = 1.5f;
    constexpr float tooltipAnchorXRatio = 0.42f;
    constexpr float tooltipAnchorYRatio = 0.58f;
    constexpr float tooltipParentMarginRatio = 0.11f;
    constexpr float tooltipWidthPadFontRatio = 0.8f;
    constexpr float tooltipTextInsetXRatio = 0.21f;
    constexpr float tooltipTextInsetYRatio = 0.05f;

    constexpr float versionFontRatio = 0.42f;
    constexpr float versionHeightRatio = 0.62f;
    constexpr float versionDesiredWidthRatio = 1.9f;
}

namespace UiStateKeys
{
    constexpr const char* editorWidth = "uiEditorWidth";
    constexpr const char* editorHeight = "uiEditorHeight";
    constexpr const char* useCustomPalette = "uiUseCustomPalette";
    constexpr const char* fxTailEnabled = "uiFxTailEnabled";
    constexpr std::array<const char*, 4> customPalette {
        "uiCustomPalette0",
        "uiCustomPalette1",
        "uiCustomPalette2",
        "uiCustomPalette3"
    };
}

static void dismissEditorOwnedModalPrompts (juce::LookAndFeel& editorLookAndFeel);

static void dismissEditorOwnedModalPrompts (juce::LookAndFeel& editorLookAndFeel)
{
    for (int i = juce::Component::getNumCurrentlyModalComponents() - 1; i >= 0; --i)
    {
        auto* modal = juce::Component::getCurrentlyModalComponent (i);
        auto* alertWindow = dynamic_cast<juce::AlertWindow*> (modal);

        if (alertWindow == nullptr)
            continue;

        if (&alertWindow->getLookAndFeel() != &editorLookAndFeel)
            continue;

        alertWindow->exitModalState (0);
    }
}

static void bringPromptWindowToFront (juce::AlertWindow& aw)
{
    aw.setAlwaysOnTop (true);
    aw.toFront (true);
}

// Helper: embed an AlertWindow in the editor overlay and center it.
// Preserves previous behaviour; refactor to avoid duplication.
void embedAlertWindowInOverlay (ECHOTRAudioProcessorEditor* editor,
                                juce::AlertWindow* aw,
                                bool bringTooltip = false)
{
    if (editor == nullptr || aw == nullptr)
        return;

    editor->setPromptOverlayActive (true);
    editor->promptOverlay.addAndMakeVisible (*aw);
    const int bx = juce::jmax (0, (editor->getWidth() - aw->getWidth()) / 2);
    const int by = juce::jmax (0, (editor->getHeight() - aw->getHeight()) / 2);
    aw->setBounds (bx, by, aw->getWidth(), aw->getHeight());
    aw->toFront (false);
    if (bringTooltip && editor->tooltipWindow)
        editor->tooltipWindow->toFront (true);
    aw->repaint();
}

// Ensure an AlertWindow fits the editor width when embedded and optionally
// run a layout callback to reposition inner controls after a resize.
static void fitAlertWindowToEditor (juce::AlertWindow& aw,
                                    ECHOTRAudioProcessorEditor* editor,
                                    std::function<void(juce::AlertWindow&)> layoutCb = {})
{
    if (editor == nullptr)
        return;

    const int overlayPad = 12;
    const int availW = juce::jmax (120, editor->getWidth() - (overlayPad * 2));
    if (aw.getWidth() > availW)
    {
        aw.setSize (availW, juce::jmin (aw.getHeight(), editor->getHeight() - (overlayPad * 2)));
        if (layoutCb)
            layoutCb (aw);
    }
}

static void anchorEditorOwnedPromptWindows (ECHOTRAudioProcessorEditor& editor,
                                            juce::LookAndFeel& editorLookAndFeel)
{
    for (int i = juce::Component::getNumCurrentlyModalComponents() - 1; i >= 0; --i)
    {
        auto* modal = juce::Component::getCurrentlyModalComponent (i);
        auto* alertWindow = dynamic_cast<juce::AlertWindow*> (modal);

        if (alertWindow == nullptr)
            continue;

        if (&alertWindow->getLookAndFeel() != &editorLookAndFeel)
            continue;

        alertWindow->centreAroundComponent (&editor, alertWindow->getWidth(), alertWindow->getHeight());
        bringPromptWindowToFront (*alertWindow);
    }
}

static juce::Font makeOverlayDisplayFont()
{
    return juce::Font (juce::FontOptions (28.0f).withStyle ("Bold"));
}

static void drawOverlayPanel (juce::Graphics& g,
                              juce::Rectangle<int> bounds,
                              juce::Colour background,
                              juce::Colour outline)
{
    g.setColour (background);
    g.fillRect (bounds);

    g.setColour (outline);
    g.drawRect (bounds, 1);
}

static juce::Colour lerpColourStops (const std::array<juce::Colour, 2>& gradient, float t)
{
    return gradient[0].interpolatedWith (gradient[1], juce::jlimit (0.0f, 1.0f, t));
}

static bool isAbsoluteGradientEndpoint (const juce::Colour& c,
                                        const std::array<juce::Colour, 2>& gradient)
{
    const auto argb = c.getARGB();
    return argb == gradient[0].getARGB() || argb == gradient[1].getARGB();
}

static void parseTailTuning (const juce::String& tuning,
                             int& trimTailCount,
                             float& repeatScale)
{
    trimTailCount = 0;
    repeatScale = -1.0f;

    const auto t = tuning.trim();
    if (t.isEmpty())
        return;

    if (t.endsWithChar ('%'))
    {
        const auto number = t.dropLastCharacters (1).trim();
        const double pct = number.getDoubleValue();
        if (pct >= 0.0 && pct <= 100.0)
            repeatScale = (float) (pct / 100.0);
        return;
    }

    const int v = t.getIntValue();
    if (v < 0)
        trimTailCount = -v;
}

static float parseOptionalPercent01 (const juce::String& percentageText)
{
    const auto t = percentageText.trim();
    if (t.isEmpty())
        return -1.0f;

    juce::String number = t;
    if (number.endsWithChar ('%'))
        number = number.dropLastCharacters (1).trim();

    const double v = number.getDoubleValue();
    if (v < 0.0 || v > 100.0)
        return -1.0f;

    return (float) (v / 100.0);
}

static void drawTextWithRepeatedLastCharGradient (juce::Graphics& g,
                                                  const juce::Rectangle<int>& area,
                                                  const juce::String& sourceText,
                                                  int horizontalSpacePx,
                                                  const std::array<juce::Colour, 2>& gradient,
                                                  int noCollisionRightX = -1,
                                                  const juce::String& tailTuning = juce::String(),
                                                  const juce::String& shrinkPerCharPercent = juce::String(),
                                                  const juce::String& tailVerticalMode = juce::String(),
                                                  const juce::String& referenceCharIndex = juce::String(),
                                                  const juce::String& overlapPercent = juce::String())
{
    constexpr int kMaxTailCharsDrawn = 20;
    constexpr float kMinTailCharPx = 3.0f;

    if (area.getWidth() <= 0 || area.getHeight() <= 0)
        return;

    juce::String text = sourceText.toUpperCase().trim();

    int trimTailCount = 0;
    float repeatScale = -1.0f;
    parseTailTuning (tailTuning, trimTailCount, repeatScale);

    if (text.isEmpty())
        return;

    const auto font = g.getCurrentFont();
    int maxWidth = juce::jmin (area.getWidth(), juce::jmax (0, horizontalSpacePx));
    if (noCollisionRightX >= 0)
        maxWidth = juce::jmin (maxWidth, juce::jmax (0, noCollisionRightX - area.getX()));

    if (maxWidth <= 0)
        return;

    const int baseW = stringWidth (font, text);

    g.setColour (gradient[0]);
    g.drawText (text, area.getX(), area.getY(), juce::jmin (baseW, maxWidth), area.getHeight(), juce::Justification::left, false);

    if (baseW >= maxWidth)
        return;

    const juce::juce_wchar lastChar = text[text.length() - 1];
    juce::juce_wchar selectedChar = lastChar;
    const auto refIdxText = referenceCharIndex.trim();
    if (refIdxText.isNotEmpty())
    {
        const int idx = refIdxText.getIntValue();
        if (idx >= 0 && idx < text.length())
            selectedChar = text[idx];
    }

    juce::String tailChar;
    tailChar += selectedChar;
    const float shrinkStep01 = parseOptionalPercent01 (shrinkPerCharPercent);
    const bool useShrink = (shrinkStep01 >= 0.0f);
    const auto verticalMode = tailVerticalMode.trim().toLowerCase();
    const float overlap01 = parseOptionalPercent01 (overlapPercent);
    const float overlap = juce::jlimit (0.0f, 1.0f, overlap01 < 0.0f ? 0.0f : overlap01);
    const float advanceFactor = 1.0f - overlap;

    const float baseFontH = font.getHeight();
    auto scaleForIndex = [&] (int index1Based)
    {
        if (! useShrink)
            return 1.0f;

        const float s = 1.0f - (shrinkStep01 * (float) index1Based);
        return juce::jmax (0.1f, s);
    };

    const int availableTailW = juce::jmax (0, maxWidth - baseW);
    juce::Array<float> xPositions;
    juce::Array<int> widths;

    float cursorX = 0.0f;
    float maxRight = 0.0f;

    for (int i = 1; i <= kMaxTailCharsDrawn; ++i)
    {
        auto fi = font;
        fi.setHeight (baseFontH * scaleForIndex (i));
        const int wi = stringWidth (fi, tailChar);
        if (fi.getHeight() < kMinTailCharPx || wi < (int) std::ceil (kMinTailCharPx)
            || wi <= 0)
            break;

        const float x = cursorX;
        const float right = x + (float) wi;
        if (right > (float) availableTailW + 1.0f)
            break;

        xPositions.add (x);
        widths.add (wi);
        maxRight = juce::jmax (maxRight, right);
        cursorX += (float) wi * advanceFactor;
    }

    int repeatCount = xPositions.size();
    repeatCount = juce::jmin (repeatCount, kMaxTailCharsDrawn);

    if (repeatScale >= 0.0f)
        repeatCount = (int) std::floor ((double) repeatCount * (double) repeatScale);

    if (trimTailCount > 0)
        repeatCount = juce::jmax (0, repeatCount - trimTailCount);

    if (repeatCount <= 1)
        return;

    const int baseBaselineY = area.getY()
                            + (int) std::round ((area.getHeight() - font.getHeight()) * 0.5f)
                            + (int) std::round (font.getAscent());

    // Draw from the end to the beginning so early indices stay visually on top.
    int drawableCount = 0;
    for (int i = repeatCount - 1; i >= 0; --i)
    {
        auto fi = font;
        fi.setHeight (baseFontH * scaleForIndex (i + 1));
        const int wi = juce::jmax (1, widths.getUnchecked (i));
        if (fi.getHeight() < kMinTailCharPx || wi < (int) std::ceil (kMinTailCharPx))
            continue;

        const float t = (float) (i + 1) / (float) juce::jmax (1, repeatCount);
        const auto c = lerpColourStops (gradient, t);
        if (isAbsoluteGradientEndpoint (c, gradient))
            continue;

        ++drawableCount;
    }

    if (drawableCount <= 1)
        return;

    for (int i = repeatCount - 1; i >= 0; --i)
    {
        auto fi = font;
        fi.setHeight (baseFontH * scaleForIndex (i + 1));
        const int wi = juce::jmax (1, widths.getUnchecked (i));
        if (fi.getHeight() < kMinTailCharPx || wi < (int) std::ceil (kMinTailCharPx))
            continue;

        const int x = area.getX() + baseW + juce::roundToInt (xPositions.getUnchecked (i));

        const float t = (float) (i + 1) / (float) juce::jmax (1, repeatCount);
        const auto c = lerpColourStops (gradient, t);
        if (isAbsoluteGradientEndpoint (c, gradient))
            continue;

        g.setColour (c);

        g.setFont (fi);

        int baselineY = baseBaselineY;
        if (verticalMode == "pyramid")
        {
            baselineY = area.getY()
                      + (int) std::round ((area.getHeight() - fi.getHeight()) * 0.5f)
                      + (int) std::round (fi.getAscent());
        }
        else if (verticalMode == "baseline")
        {
            baselineY = baseBaselineY;
        }

        g.drawSingleLineText (tailChar, x, baselineY, juce::Justification::left);
    }

    g.setFont (font);
}

static bool fits (juce::Graphics& g, const juce::String& s, int w)
{
    if (w <= 0) return false;
    return stringWidth (g.getCurrentFont(), s) <= w;
}

// Variante “solo medir” (sin Graphics): para decidir enable/disable en resized()
static bool fitsWithOptionalShrink_NoG (juce::Font font,
                                       const juce::String& text,
                                       int width,
                                       float baseFontPx,
                                       float shrinkFloorPx)
{
    if (width <= 0) return false;

    font.setHeight (baseFontPx);
    if (stringWidth (font, text) <= width)
        return true;

    for (float h = baseFontPx - 1.0f; h >= shrinkFloorPx; h -= 1.0f)
    {
        font.setHeight (h);
        if (stringWidth (font, text) <= width)
            return true;
    }
    return false;
}

static bool drawIfFitsWithOptionalShrink (juce::Graphics& g,
                                         const juce::Rectangle<int>& area,
                                         const juce::String& text,
                                         float baseFontPx,
                                         float shrinkFloorPx)
{
    auto font = g.getCurrentFont();
    font.setHeight (baseFontPx);
    g.setFont (font);

    if (fits (g, text, area.getWidth()))
    {
        g.drawText (text, area, juce::Justification::left, false);
        return true;
    }

    // pequeño shrink “suave” para intentar salvar unidades antes de abreviar
    for (float h = baseFontPx - 1.0f; h >= shrinkFloorPx; h -= 1.0f)
    {
        font.setHeight (h);
        g.setFont (font);
        if (fits (g, text, area.getWidth()))
        {
            g.drawText (text, area, juce::Justification::left, false);
            return true;
        }
    }

    return false;
}

static void drawValueNoEllipsis (juce::Graphics& g,
                                 const juce::Rectangle<int>& area,
                                 const juce::String& fullText,
                                 const juce::String& noUnitText,
                                 const juce::String& intOnlyText,
                                 float baseFontPx,
                                 float minFontPx)
{
    if (area.getWidth() <= 2 || area.getHeight() <= 2)
        return;

    const auto full = fullText.toUpperCase();
    const auto noU  = noUnitText.toUpperCase();
    const auto intl = intOnlyText.toUpperCase();

    const float softShrinkFloor = minFontPx;

    // FULL (con shrink suave)
    if (drawIfFitsWithOptionalShrink (g, area, full, baseFontPx, softShrinkFloor))
        return;

    // NO-UNIT (con shrink suave)
    if (noU.isNotEmpty() && drawIfFitsWithOptionalShrink (g, area, noU, baseFontPx, softShrinkFloor))
        return;

    // INT (normal)
    {
        auto font = g.getCurrentFont();
        font.setHeight (baseFontPx);
        g.setFont (font);

        if (intl.isNotEmpty() && fits (g, intl, area.getWidth()))
        {
            g.drawText (intl, area, juce::Justification::left, false);
            return;
        }

        // shrink solo para el entero
        for (float h = baseFontPx; h >= minFontPx; h -= 1.0f)
        {
            font.setHeight (h);
            g.setFont (font);
            if (intl.isNotEmpty() && fits (g, intl, area.getWidth()))
            {
                g.drawText (intl, area, juce::Justification::left, false);
                return;
            }
        }
    }
}

static bool drawValueWithRightAlignedSuffix (juce::Graphics& g,
                                             const juce::Rectangle<int>& area,
                                             const juce::String& valueText,
                                             const juce::String& suffixText,
                                             bool enableAutoMargin,
                                             float baseFontPx,
                                             float minFontPx,
                                             const std::array<juce::Colour, 2>* tailGradient = nullptr,
                                             bool tailFromSuffixToLeft = false,
                                             bool lowercaseTailChars = false,
                                             const juce::String& tailTuning = juce::String())
{
    constexpr int kMaxTailCharsDrawn = 20;
    constexpr float kMinTailCharPx = 3.0f;
    constexpr int kAutoMarginThresholdPx = 24;
    constexpr int kSingleDigitTailBudgetChars = 8;
    constexpr float kDefaultReverseShrinkStep01 = 0.20f;
    constexpr float kSingleDigitReverseShrinkStep01 = 0.10f;
    constexpr float kMinTailScale = 0.1f;
    constexpr float kTailOverlap01 = 0.0f;
    constexpr int kTailTokenChars = 1;

    if (area.getWidth() <= 2 || area.getHeight() <= 2)
        return false;

    const auto value = valueText.toUpperCase();
    const auto suffix = suffixText.toUpperCase();

    auto font = g.getCurrentFont();

    for (float h = baseFontPx; h >= minFontPx; h -= 1.0f)
    {
        font.setHeight (h);
        g.setFont (font);

        const int suffixW = stringWidth (font, suffix);
        const int valueW = stringWidth (font, value);
        const int gapW = juce::jmax (2, stringWidth (font, " "));

        const int totalW = valueW + (suffix.isNotEmpty() ? gapW : 0) + suffixW;
        if (totalW > area.getWidth())
            continue;

        const int suffixX = area.getRight() - suffixW;
        const int valueRight = suffixX - (suffix.isNotEmpty() ? gapW : 0);
        const int fullValueAreaW = juce::jmax (1, valueRight - area.getX());
        const int freeSpace = juce::jmax (0, fullValueAreaW - valueW);

        int valueX = area.getX();
        if (enableAutoMargin && freeSpace > kAutoMarginThresholdPx)
            valueX += freeSpace / 2;

        const int valueAreaW = juce::jmax (1, valueRight - valueX);

        auto computeSingleDigitReverseLaneWidth = [&]() -> int
        {
            const juce::String tailToken = suffix.substring (0, 1);
            const int tailTokenW = juce::jmax (1, stringWidth (font, tailToken));
            const int tailBudgetW = juce::jmax (tailTokenW * kSingleDigitTailBudgetChars,
                                                stringWidth (font, "SSSS"));
            const int desiredLaneW = valueW + juce::jmax (gapW, tailBudgetW);
            const int minLaneW = valueW + gapW;

            if (valueAreaW <= minLaneW)
                return valueAreaW;

            return juce::jlimit (minLaneW, valueAreaW, desiredLaneW);
        };

        int valueDrawW = valueAreaW;
        if (tailGradient != nullptr && tailFromSuffixToLeft && suffix.isNotEmpty() && value.length() <= 1)
            valueDrawW = computeSingleDigitReverseLaneWidth();

        if (tailGradient != nullptr && ! tailFromSuffixToLeft)
        {
            const auto valueArea = juce::Rectangle<int> (valueX, area.getY(), valueDrawW, area.getHeight());
            drawTextWithRepeatedLastCharGradient (g, valueArea, value, valueDrawW, *tailGradient, valueX + valueDrawW,
                                                  tailTuning, "20%", "pyramid");
            g.setColour ((*tailGradient)[0]);
        }
        else
        {
            g.drawText (value, valueX, area.getY(), valueDrawW, area.getHeight(), juce::Justification::left, false);
        }

        g.drawText (suffix, suffixX, area.getY(), suffixW, area.getHeight(), juce::Justification::left, false);

        if (tailGradient != nullptr && tailFromSuffixToLeft && suffix.isNotEmpty())
        {
            int trimTailCount = 0;
            float repeatScale = -1.0f;
            parseTailTuning (tailTuning, trimTailCount, repeatScale);

            const float shrinkStep01 = (value.length() <= 1) ? kSingleDigitReverseShrinkStep01
                                                              : kDefaultReverseShrinkStep01;
            const bool useShrink = (shrinkStep01 >= 0.0f);
            const float advanceFactor = 1.0f - kTailOverlap01;

            juce::String tailChar = suffix.substring (0, juce::jmin (kTailTokenChars, suffix.length()));
            if (lowercaseTailChars)
                tailChar = tailChar.toLowerCase();

            const int tailCharW = stringWidth (font, tailChar);
            if (tailCharW > 0)
            {
                const int leftLimit = valueX + valueW;
                const int rightLimit = suffixX;
                const int fittingSlackPx = juce::jmax (2, tailCharW / 2);
                const int leftLimitForFit = leftLimit - fittingSlackPx;

                auto scaleForIndex = [&] (int index1Based)
                {
                    if (! useShrink)
                        return 1.0f;

                    const float s = 1.0f - (shrinkStep01 * (float) index1Based);
                    return juce::jmax (kMinTailScale, s);
                };

                int repeatCount = 0;
                float usedTailW = 0.0f;
                for (int i = 1; i <= kMaxTailCharsDrawn; ++i)
                {
                    auto fi = font;
                    fi.setHeight (font.getHeight() * scaleForIndex (i));
                    const int wi = stringWidth (fi, tailChar);
                    const int xCandidate = rightLimit - (int) std::floor (usedTailW + 1.0e-6f) - wi;
                    if (fi.getHeight() < kMinTailCharPx || wi < (int) std::ceil (kMinTailCharPx)
                        || wi <= 0 || xCandidate < leftLimitForFit)
                        break;

                    usedTailW += (float) wi * advanceFactor;
                    ++repeatCount;
                }

                if (repeatScale >= 0.0f)
                    repeatCount = (int) std::floor ((double) repeatCount * (double) repeatScale);

                if (trimTailCount > 0)
                    repeatCount = juce::jmax (0, repeatCount - trimTailCount);

                repeatCount = juce::jmin (repeatCount, kMaxTailCharsDrawn);

                if (repeatCount > 1)
                {
                    std::array<int, (size_t) kMaxTailCharsDrawn> drawXs {};
                    std::array<int, (size_t) kMaxTailCharsDrawn> drawBaselines {};
                    int draw_count = 0;

                    float consumedW = 0.0f;
                    for (int i = 0; i < repeatCount; ++i)
                    {
                        auto fi = font;
                        fi.setHeight (font.getHeight() * scaleForIndex (i + 1));
                        const int wi = juce::jmax (1, stringWidth (fi, tailChar));
                        if (fi.getHeight() < kMinTailCharPx || wi < (int) std::ceil (kMinTailCharPx))
                            break;
                        const int x = rightLimit - (int) std::floor (consumedW + 1.0e-6f) - wi;

                        const int baselineY = area.getY()
                                            + (int) std::round ((area.getHeight() - fi.getHeight()) * 0.5f)
                                            + (int) std::round (fi.getAscent());

                        if (draw_count >= kMaxTailCharsDrawn)
                            break;

                        drawXs[(size_t) draw_count] = x;
                        drawBaselines[(size_t) draw_count] = baselineY;
                        ++draw_count;
                        consumedW += (float) wi * advanceFactor;
                    }

                    if (draw_count <= 1)
                    {
                        g.setFont (font);
                        g.setColour ((*tailGradient)[0]);
                        return true;
                    }

                    int drawable_count = 0;
                    for (int i = draw_count - 1; i >= 0; --i)
                    {
                        const float t = (float) (i + 1) / (float) juce::jmax (1, draw_count);
                        const auto c = lerpColourStops (*tailGradient, t);
                        if (isAbsoluteGradientEndpoint (c, *tailGradient))
                            continue;
                        ++drawable_count;
                    }

                    if (drawable_count <= 1)
                    {
                        g.setFont (font);
                        g.setColour ((*tailGradient)[0]);
                        return true;
                    }

                    // Reversed stacking priority: draw darker/later first,
                    // then lighter/earlier on top.
                    for (int i = draw_count - 1; i >= 0; --i)
                    {
                        auto fi = font;
                        fi.setHeight (font.getHeight() * scaleForIndex (i + 1));

                        const float t = (float) (i + 1) / (float) juce::jmax (1, draw_count);
                        const auto c = lerpColourStops (*tailGradient, t);
                        if (isAbsoluteGradientEndpoint (c, *tailGradient))
                            continue;

                        g.setColour (c);
                        g.setFont (fi);
                        g.drawSingleLineText (tailChar, drawXs[(size_t) i], drawBaselines[(size_t) i], juce::Justification::left);
                    }

                    g.setFont (font);
                    g.setColour ((*tailGradient)[0]);
                }
            }
        }

        return true;
    }

    return false;
}

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
                                     std::round (local.getHeight() * 0.50f));

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
    const std::array<BarSlider*, 7> barSliders { &timeSlider, &feedbackSlider, &modeSlider, &modSlider, &inputSlider, &outputSlider, &mixSlider };

    useCustomPalette = audioProcessor.getUiUseCustomPalette();
    fxTailEnabled = audioProcessor.getUiFxTailEnabled();

    for (int i = 0; i < 4; ++i)
        customPalette[(size_t) i] = audioProcessor.getUiCustomPaletteColour (i);

    setOpaque (true);

    applyActivePalette();
    setLookAndFeel (&lnf);
    tooltipWindow = std::make_unique<juce::TooltipWindow> (nullptr, 250);
    tooltipWindow->setLookAndFeel (&lnf);
    tooltipWindow->setAlwaysOnTop (true);

    setResizable (true, true);

    // Para que el host/JUCE clipee de verdad
    setResizeLimits (kMinW, kMinH, kMaxW, kMaxH);

    resizeConstrainer.setMinimumSize (kMinW, kMinH);
    resizeConstrainer.setMaximumSize (kMaxW, kMaxH);

    resizerCorner = std::make_unique<juce::ResizableCornerComponent> (this, &resizeConstrainer);
    addAndMakeVisible (*resizerCorner);
    if (resizerCorner != nullptr)
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

    syncButton.setButtonText ("");
    autoFbkButton.setButtonText ("");
    midiButton.setButtonText ("");

    addAndMakeVisible (syncButton);
    addAndMakeVisible (autoFbkButton);
    addAndMakeVisible (midiButton);

    // Initialize MIDI port display
    const int savedPort = audioProcessor.getMidiPort();
    midiPortDisplay.setText (savedPort == 0 ? "---" : juce::String (savedPort), juce::dontSendNotification);
    midiPortDisplay.setJustificationType (juce::Justification::centred);
    midiPortDisplay.setInterceptsMouseClicks (false, false);
    midiPortDisplay.setBorderSize (juce::BorderSize<int> (0));  // No border, we draw it manually
    midiPortDisplay.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    midiPortDisplay.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (midiPortDisplay);
    midiPortDisplay.setVisible (false);  // Start hidden; resized() will show it if it fits

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

    // Disable numeric popup for MODE and MOD (slider-only operation)
    modeSlider.setAllowNumericPopup (false);
    modSlider.setAllowNumericPopup (false);

    auto bindButton = [&] (std::unique_ptr<ButtonAttachment>& attachment,
                           const char* paramId,
                           juce::Button& button)
    {
        attachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, paramId, button);
    };

    bindButton (syncAttachment, ECHOTRAudioProcessor::kParamSync, syncButton);
    bindButton (autoFbkAttachment, ECHOTRAudioProcessor::kParamAutoFbk, autoFbkButton);
    bindButton (midiAttachment, ECHOTRAudioProcessor::kParamMidi, midiButton);

    const std::array<const char*, 7> uiMirrorParamIds {
        ECHOTRAudioProcessor::kParamSync,        // Listen for sync mode changes
        ECHOTRAudioProcessor::kParamUiPalette,
        ECHOTRAudioProcessor::kParamUiFxTail,
        ECHOTRAudioProcessor::kParamUiColor0,
        ECHOTRAudioProcessor::kParamUiColor1,
        ECHOTRAudioProcessor::kParamUiColor2,
        ECHOTRAudioProcessor::kParamUiColor3
    };
    for (auto* paramId : uiMirrorParamIds)
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

    startTimerHz (10);

    refreshLegendTextCache();
}

ECHOTRAudioProcessorEditor::~ECHOTRAudioProcessorEditor()
{
    stopTimer();

    const std::array<const char*, 7> uiMirrorParamIds {
        ECHOTRAudioProcessor::kParamSync,
        ECHOTRAudioProcessor::kParamUiPalette,
        ECHOTRAudioProcessor::kParamUiFxTail,
        ECHOTRAudioProcessor::kParamUiColor0,
        ECHOTRAudioProcessor::kParamUiColor1,
        ECHOTRAudioProcessor::kParamUiColor2,
        ECHOTRAudioProcessor::kParamUiColor3
    };
    for (auto* paramId : uiMirrorParamIds)
        audioProcessor.apvts.removeParameterListener (paramId, this);

    audioProcessor.setUiUseCustomPalette (useCustomPalette);
    audioProcessor.setUiFxTailEnabled (fxTailEnabled);

    dismissEditorOwnedModalPrompts (lnf);
    setPromptOverlayActive (false);

    const std::array<BarSlider*, 7> barSliders { &timeSlider, &feedbackSlider, &modeSlider, &modSlider, &inputSlider, &outputSlider, &mixSlider };
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
    scheme.fxGradientStart = palette[2];
    scheme.fxGradientEnd = palette[3];

    for (auto& s : schemes)
        s = scheme;

    lnf.setScheme (schemes[(size_t) currentSchemeIndex]);
    
    // Apply text color to MIDI port display
    midiPortDisplay.setColour (juce::Label::textColourId, scheme.text);
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

    const int previousMode = labelVisibilityMode;
    const int previousValueColumnWidth = getTargetValueColumnWidth();
    const bool legendTextLengthChanged = refreshLegendTextCache();
    if (legendTextLengthChanged)
        updateLegendVisibility();
    const int currentValueColumnWidth = getTargetValueColumnWidth();

    if (labelVisibilityMode != previousMode || currentValueColumnWidth != previousValueColumnWidth || slider == nullptr)
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
    const std::array<juce::Component*, 10> interactiveControls {
        &timeSlider, &feedbackSlider, &modeSlider, &modSlider,
        &inputSlider, &outputSlider, &mixSlider,
        &syncButton, &autoFbkButton, &midiButton
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
                             || parameterID == ECHOTRAudioProcessor::kParamUiFxTail
                             || parameterID == ECHOTRAudioProcessor::kParamUiColor0
                             || parameterID == ECHOTRAudioProcessor::kParamUiColor1
                             || parameterID == ECHOTRAudioProcessor::kParamUiColor2
                             || parameterID == ECHOTRAudioProcessor::kParamUiColor3;

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
        for (int i = 0; i < 4; ++i)
        {
            const auto c = audioProcessor.getUiCustomPaletteColour (i);
            if (customPalette[(size_t) i].getARGB() != c.getARGB())
            {
                customPalette[(size_t) i] = c;
                paletteChanged = true;
            }
        }

        const bool targetUseCustomPalette = audioProcessor.getUiUseCustomPalette();
        const bool targetFxTailEnabled = audioProcessor.getUiFxTailEnabled();

        const bool paletteSwitchChanged = (useCustomPalette != targetUseCustomPalette);
        const bool fxChanged = (fxTailEnabled != targetFxTailEnabled);

        if (paletteSwitchChanged)
            useCustomPalette = targetUseCustomPalette;

        if (fxChanged)
            fxTailEnabled = targetFxTailEnabled;

        if (paletteChanged || paletteSwitchChanged)
            applyActivePalette();

        if (paletteChanged || paletteSwitchChanged || fxChanged)
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
    const auto oldTimeFullLen = cachedTimeTextFull.length();
    const auto oldTimeShortLen = cachedTimeTextShort.length();
    const auto oldFeedbackFullLen = cachedFeedbackTextFull.length();
    const auto oldFeedbackShortLen = cachedFeedbackTextShort.length();
    const auto oldModeFullLen = cachedModeTextFull.length();
    const auto oldModeShortLen = cachedModeTextShort.length();
    const auto oldModFullLen = cachedModTextFull.length();
    const auto oldModShortLen = cachedModTextShort.length();
    const auto oldInputFullLen = cachedInputTextFull.length();
    const auto oldInputShortLen = cachedInputTextShort.length();
    const auto oldOutputFullLen = cachedOutputTextFull.length();
    const auto oldOutputShortLen = cachedOutputTextShort.length();
    const auto oldMixFullLen = cachedMixTextFull.length();
    const auto oldMixShortLen = cachedMixTextShort.length();

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

    const bool lengthChanged = oldTimeFullLen != cachedTimeTextFull.length()
                            || oldTimeShortLen != cachedTimeTextShort.length()
                            || oldFeedbackFullLen != cachedFeedbackTextFull.length()
                            || oldFeedbackShortLen != cachedFeedbackTextShort.length()
                            || oldModeFullLen != cachedModeTextFull.length()
                            || oldModeShortLen != cachedModeTextShort.length()
                            || oldModFullLen != cachedModTextFull.length()
                            || oldModShortLen != cachedModTextShort.length()
                            || oldInputFullLen != cachedInputTextFull.length()
                            || oldInputShortLen != cachedInputTextShort.length()
                            || oldOutputFullLen != cachedOutputTextFull.length()
                            || oldOutputShortLen != cachedOutputTextShort.length()
                            || oldMixFullLen != cachedMixTextFull.length()
                            || oldMixShortLen != cachedMixTextShort.length();

    return lengthChanged;
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
    static double roundToDecimals (double value, int decimals)
    {
        const int safeDecimals = juce::jlimit (0, 9, decimals);
        const double scale = std::pow (10.0, (double) safeDecimals);
        return std::round (value * scale) / scale;
    }

    constexpr int kPromptWidth = 460;
    constexpr int kPromptHeight = 336;
    constexpr int kPromptInnerMargin = 24;
    constexpr int kPromptFooterBottomPad = 24;
    constexpr int kPromptFooterGap = 12;
    constexpr int kPromptBodyTopPad = 24;
    constexpr int kPromptBodyBottomPad = 18;
    constexpr const char* kPromptSuffixLabelId = "promptSuffixLabel";

    constexpr float kPromptEditorFontScale = 1.5f;
    constexpr float kPromptEditorHeightScale = 1.4f;
    constexpr int kPromptEditorHeightPadPx = 6;
    constexpr int kPromptEditorRaiseYPx = 8;
    constexpr int kPromptEditorMinTopPx = 6;
    constexpr int kPromptEditorMinWidthPx = 180;
    constexpr int kPromptEditorMaxWidthPx = 240;
    constexpr int kPromptEditorHostPadPx = 80;

    constexpr int kPromptInlineContentPadPx = 8;
    constexpr int kPromptSuffixVInsetPx = 1;
    constexpr int kPromptSuffixBaselineDefaultPx = 3;
    constexpr int kPromptSuffixBaselineShapePx = 4;

    constexpr int kTitleAreaExtraHeightPx = 4;
    constexpr int kTitleRightGapToInfoPx = 8;
    constexpr int kVersionGapPx = 8;
    constexpr int kToggleLegendCollisionPadPx = 6;

    void applyPromptShellSize (juce::AlertWindow& aw)
    {
        aw.setSize (kPromptWidth, kPromptHeight);
    }

    int getAlertButtonsTop (const juce::AlertWindow& aw)
    {
        int buttonsTop = aw.getHeight() - (kPromptFooterBottomPad + 36);
        for (int i = 0; i < aw.getNumButtons(); ++i)
            if (auto* btn = aw.getButton (i))
                buttonsTop = juce::jmin (buttonsTop, btn->getY());
        return buttonsTop;
    }

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

}

static void layoutAlertWindowButtons (juce::AlertWindow& aw)
{
    const int btnCount = aw.getNumButtons();
    if (btnCount <= 0)
        return;

    const int footerY = aw.getHeight() - kPromptFooterBottomPad;
    const int sideMargin = kPromptInnerMargin;
    const int buttonGap = kPromptFooterGap;

    if (btnCount == 1)
    {
        if (auto* btn = aw.getButton (0))
        {
            auto r = btn->getBounds();
            r.setWidth (juce::jmax (80, r.getWidth()));
            r.setX ((aw.getWidth() - r.getWidth()) / 2);
            r.setY (footerY - r.getHeight());
            btn->setBounds (r);
        }
        return;
    }

    const int totalW = aw.getWidth();
    const int totalGap = (btnCount - 1) * buttonGap;
    const int btnWidth = juce::jmax (20, (totalW - (2 * sideMargin) - totalGap) / btnCount);

    int x = sideMargin;
    for (int i = 0; i < btnCount; ++i)
    {
        if (auto* btn = aw.getButton (i))
        {
            auto r = btn->getBounds();
            r.setWidth (btnWidth);
            r.setY (footerY - r.getHeight());
            r.setX (x);
            btn->setBounds (r);
        }
        x += btnWidth + buttonGap;
    }
}

static void layoutInfoPopupContent (juce::AlertWindow& aw)
{
    layoutAlertWindowButtons (aw);

    const int contentTop = kPromptBodyTopPad;
    const int contentBottom = getAlertButtonsTop (aw) - kPromptBodyBottomPad;
    const int contentH = juce::jmax (0, contentBottom - contentTop);

    auto* infoLabel = dynamic_cast<juce::Label*> (aw.findChildWithID ("infoText"));
    auto* infoLink = dynamic_cast<juce::HyperlinkButton*> (aw.findChildWithID ("infoLink"));

    if (infoLabel != nullptr && infoLink != nullptr)
    {
        const int labelH = juce::jlimit (26, juce::jmax (26, contentH), (int) std::lround (contentH * 0.34));
        const int linkH = juce::jlimit (20, 34, (int) std::lround (contentH * 0.18));

        const int freeH = juce::jmax (0, contentH - labelH - linkH);
        const int gap = freeH / 3;
        const int labelY = contentTop + gap;
        const int linkY = labelY + labelH + gap;

        infoLabel->setBounds (kPromptInnerMargin,
                              labelY,
                              aw.getWidth() - (2 * kPromptInnerMargin),
                              labelH);

        infoLink->setBounds (kPromptInnerMargin,
                             linkY,
                             aw.getWidth() - (2 * kPromptInnerMargin),
                             linkH);
        return;
    }

    if (infoLabel != nullptr)
    {
        infoLabel->setBounds (kPromptInnerMargin,
                              contentTop,
                              aw.getWidth() - (2 * kPromptInnerMargin),
                              juce::jmax (20, contentH));
    }
}

static juce::String colourToHexRgb (juce::Colour c)
{
    auto h2 = [] (juce::uint8 v)
    {
        return juce::String::toHexString ((int) v).paddedLeft ('0', 2).toUpperCase();
    };

    return "#" + h2 (c.getRed()) + h2 (c.getGreen()) + h2 (c.getBlue());
}

static bool tryParseHexColour (juce::String text, juce::Colour& out)
{
    auto isHexDigitAscii = [] (juce::juce_wchar ch)
    {
        return (ch >= '0' && ch <= '9')
            || (ch >= 'A' && ch <= 'F')
            || (ch >= 'a' && ch <= 'f');
    };

    text = text.trim();
    if (text.startsWithChar ('#'))
        text = text.substring (1);

    if (text.length() != 6 && text.length() != 8)
        return false;

    for (int i = 0; i < text.length(); ++i)
        if (! isHexDigitAscii (text[i]))
            return false;

    if (text.length() == 6)
    {
        const auto r = (juce::uint8) text.substring (0, 2).getHexValue32();
        const auto g = (juce::uint8) text.substring (2, 4).getHexValue32();
        const auto b = (juce::uint8) text.substring (4, 6).getHexValue32();
        out = juce::Colour (r, g, b);
        return true;
    }

    const auto a = (juce::uint8) text.substring (0, 2).getHexValue32();
    const auto r = (juce::uint8) text.substring (2, 4).getHexValue32();
    const auto g = (juce::uint8) text.substring (4, 6).getHexValue32();
    const auto b = (juce::uint8) text.substring (6, 8).getHexValue32();
    out = juce::Colour (r, g, b).withAlpha ((float) a / 255.0f);
    return true;
}

static void setPaletteSwatchColour (juce::TextButton& b, juce::Colour colour)
{
    b.setButtonText ("");
    b.setColour (juce::TextButton::buttonColourId, colour);
    b.setColour (juce::TextButton::buttonOnColourId, colour);
}

static void stylePromptTextEditor (juce::TextEditor& te,
                                   juce::Colour bg,
                                   juce::Colour text,
                                   juce::Colour accent,
                                   juce::Font baseFont,
                                   int hostWidth,
                                   bool widenAndCenter)
{
    auto popupFont = baseFont;
    popupFont.setHeight (popupFont.getHeight() * kPromptEditorFontScale);
    te.setFont (popupFont);
    te.applyFontToAllText (popupFont);
    te.setJustification (juce::Justification::centred);
    te.setIndents (0, 0);

    te.setColour (juce::TextEditor::backgroundColourId,      bg);
    te.setColour (juce::TextEditor::textColourId,            text);
    te.setColour (juce::TextEditor::outlineColourId,         bg);
    te.setColour (juce::TextEditor::focusedOutlineColourId,  bg);
    te.setColour (juce::TextEditor::highlightColourId,       accent.withAlpha (0.35f));
    te.setColour (juce::TextEditor::highlightedTextColourId, text);

    auto r = te.getBounds();
    r.setHeight ((int) (popupFont.getHeight() * kPromptEditorHeightScale) + kPromptEditorHeightPadPx);
    r.setY (juce::jmax (kPromptEditorMinTopPx, r.getY() - kPromptEditorRaiseYPx));

    if (widenAndCenter)
    {
        const int editorW = juce::jlimit (kPromptEditorMinWidthPx,
                                          kPromptEditorMaxWidthPx,
                                          hostWidth - kPromptEditorHostPadPx);
        r.setWidth (editorW);
        r.setX ((hostWidth - r.getWidth()) / 2);
    }

    te.setBounds (r);
    te.selectAll();
}

static void centrePromptTextEditorVertically (juce::AlertWindow& aw,
                                              juce::TextEditor& te,
                                              int minTop = kPromptEditorMinTopPx)
{
    int buttonsTop = aw.getHeight();
    for (int i = 0; i < aw.getNumButtons(); ++i)
        if (auto* btn = aw.getButton (i))
            buttonsTop = juce::jmin (buttonsTop, btn->getY());

    auto r = te.getBounds();
    const int centeredY = (buttonsTop - r.getHeight()) / 2;
    r.setY (juce::jmax (minTop, centeredY));
    te.setBounds (r);
}

static void focusAndSelectPromptTextEditor (juce::AlertWindow& aw, const juce::String& editorId)
{
    juce::Component::SafePointer<juce::AlertWindow> safeAw (&aw);
    juce::MessageManager::callAsync ([safeAw, editorId]()
    {
        if (safeAw == nullptr)
            return;

        auto* te = safeAw->getTextEditor (editorId);
        if (te == nullptr)
            return;

        if (te->isShowing() && te->isEnabled() && te->getPeer() != nullptr)
            te->grabKeyboardFocus();

        te->selectAll();
    });
}

static void preparePromptTextEditor (juce::AlertWindow& aw,
                                     const juce::String& editorId,
                                     juce::Colour bg,
                                     juce::Colour text,
                                     juce::Colour accent,
                                     juce::Font baseFont,
                                     bool widenAndCenter,
                                     int minTop = 6)
{
    if (auto* te = aw.getTextEditor (editorId))
    {
        stylePromptTextEditor (*te,
                               bg,
                               text,
                               accent,
                               baseFont,
                               aw.getWidth(),
                               widenAndCenter);
        centrePromptTextEditorVertically (aw, *te, minTop);
        focusAndSelectPromptTextEditor (aw, editorId);
    }
}

static void syncGraphicsPopupState (juce::AlertWindow& aw,
                                    const std::array<juce::Colour, 4>& defaultPalette,
                                    const std::array<juce::Colour, 4>& customPalette,
                                    bool useCustomPalette)
{
    if (auto* t = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("paletteDefaultToggle")))
        t->setToggleState (! useCustomPalette, juce::dontSendNotification);
    if (auto* t = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("paletteCustomToggle")))
        t->setToggleState (useCustomPalette, juce::dontSendNotification);

    for (int i = 0; i < 4; ++i)
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

    const int buttonsTop = getAlertButtonsTop (aw);

    const int contentLeft = kPromptInnerMargin;
    const int contentTop = kPromptBodyTopPad;
    const int contentRight = aw.getWidth() - kPromptInnerMargin;
    const int contentBottom = buttonsTop - kPromptBodyBottomPad;
    const int contentW = juce::jmax (0, contentRight - contentLeft);
    const int contentH = juce::jmax (0, contentBottom - contentTop);

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
                                               (int) std::lround ((double) toggleBox * 0.50));

    const int swatchGroupSize = (2 * swatchSize) + swatchGap;
    const int swatchesH = swatchGroupSize;
    const int modeH = toggleBox;

    const int baseGap1 = GraphicsPromptLayout::titleToModeGap;
    const int baseGap2 = GraphicsPromptLayout::modeToSwatchesGap;
    const int stackHNoTopBottom = titleH + baseGap1 + modeH + baseGap2 + swatchesH;
    const int centeredYStart = snapEven (contentTop + juce::jmax (0, (contentH - stackHNoTopBottom) / 2));
    const int symmetricTopMargin = kPromptFooterBottomPad;
    const bool hasBodyTitle = (paletteTitle != nullptr);
    const int yStart = hasBodyTitle ? snapEven (symmetricTopMargin) : centeredYStart;

    const int titleY = yStart;
    const int modeY = snapEven (titleY + titleH + baseGap1);
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

        for (int i = 0; i < 4; ++i)
        {
            if (auto* b = dynamic_cast<juce::TextButton*> (aw.findChildWithID (prefix + juce::String (i))))
            {
                const int col = i % 2;
                const int row = i / 2;
                b->setBounds (startX + col * (swatchSize + swatchGap),
                              startY + row * (swatchSize + swatchGap),
                              swatchSize,
                              swatchSize);
            }
        }
    };

    placeSwatchGroup ("defaultSwatch", defaultSwatchStartX);
    placeSwatchGroup ("customSwatch", customSwatchStartX);

    if (auto* okButton = aw.getNumButtons() > 0 ? aw.getButton (0) : nullptr)
    {
        if (okButton != nullptr)
        {
            auto okR = okButton->getBounds();
            okR.setX (col1X);
            okButton->setBounds (okR);

                const int fxY = snapEven (okR.getY() + juce::jmax (0, (okR.getHeight() - toggleBox) / 2));
                const int fxX = col0X;
                if (fxToggle != nullptr) fxToggle->setBounds (fxX, fxY, toggleBox, toggleBox);
                if (fxLabel != nullptr)  fxLabel->setBounds (fxX + toggleLabelStartOffset, fxY, fxLabelW, toggleBox);
            }
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

    for (int i = 0; i < 4; ++i)
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

            for (int i = 0; i < 4; ++i)
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
    lnf.setScheme (schemes[(size_t) currentSchemeIndex]);

    // grab a local copy, we will use its raw colours below to bypass
    // any host/LNF oddities that might creep in
    const auto scheme = schemes[(size_t) currentSchemeIndex];

    // decide what suffix label should appear; we want *separate* text that
    // is not part of the editable field. use the full nomenclature from the
    // helpers (long form) rather than the previous abbreviations.
    juce::String suffix;
    const bool isTimeSyncMode = (&s == &timeSlider && syncButton.getToggleState());
    if (&s == &timeSlider)
    {
        if (isTimeSyncMode)
        {
            // No suffix in sync mode (divisions have no units)
            suffix = "";
        }
        else
            suffix = " MS";
    }
    else if (&s == &feedbackSlider)  suffix = " % FEEDBACK";
    else if (&s == &modeSlider)      suffix = " MODE";
    else if (&s == &modSlider)       suffix = " % MOD";
    else if (&s == &inputSlider)     suffix = " DB INPUT";
    else if (&s == &outputSlider)    suffix = " DB OUTPUT";
    else if (&s == &mixSlider)       suffix = " % MIX";
    const juce::String suffixText = suffix.trimStart();
    const bool isPercentPrompt = (&s == &feedbackSlider || &s == &modSlider || &s == &mixSlider);

    // Sin texto de prompt: solo input + OK/Cancel
    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);

    // enforce our custom look&feel; hosts often reset dialogs to their own LNF
    aw->setLookAndFeel (&lnf);

    const auto current = s.getTextFromValue (s.getValue());
    aw->addTextEditor ("val", current, juce::String()); // sin label

    // we will create a label just to the right of the editor showing the suffix
    juce::Label* suffixLabel = nullptr;

    // increase the font size for legibility rather than resizing the field
    // we already have a helper up near the top of this file that uses
    // GlyphArrangement to measure text. reuse that instead of TextLayout.
    // (stringWidth(font, text))

    // adaptable filter for numeric input: clamps length, number of decimals and
    // optionally a value range. the returned string is the permitted version of
    // whatever the user typed.
    struct NumericInputFilter  : juce::TextEditor::InputFilter
    {
        double minVal, maxVal;
        int maxLen, maxDecimals;
        bool isShape = false;

                NumericInputFilter (double minV, double maxV,
                                                        int maxLength, int maxDecs, bool isShapeValue = false)
            : minVal (minV), maxVal (maxV),
                            maxLen (maxLength), maxDecimals (maxDecs), isShape (isShapeValue) {}

        juce::String filterNewText (juce::TextEditor& editor,
                                    const juce::String& newText) override
        {
            bool seenDot = false;
            int decimals = 0;
            juce::String result;

            for (auto c : newText)
            {
                if (c == '.')
                {
                    if (seenDot || maxDecimals == 0)
                        continue;
                    seenDot = true;
                    result += c;
                }
                else if (juce::CharacterFunctions::isDigit (c))
                {
                    if (seenDot) ++decimals;
                    if (decimals > maxDecimals)
                        break;
                    result += c;
                }
                else if ((c == '+' || c == '-') && result.isEmpty())
                {
                    result += c;
                }

                if (maxLen > 0 && result.length() >= maxLen)
                    break;
            }

            // now check the numeric value if the new text were inserted
            juce::String proposed = editor.getText();
            int insertPos = editor.getCaretPosition();
            proposed = proposed.substring (0, insertPos) + result
                     + proposed.substring (insertPos + editor.getHighlightedText().length());

            double val = proposed.replaceCharacter(',', '.').getDoubleValue();
            if (val > maxVal)
                return juce::String(); // reject insertion that exceeds limit

            return result;
        }
    };
    
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
        auto f = lnf.getAlertWindowMessageFont();
        f.setHeight (f.getHeight() * 1.5f);
        te->setFont (f);
        te->applyFontToAllText (f);

        // ensure the editor is tall enough to contain the larger text
        auto r = te->getBounds();
        r.setHeight ((int) (f.getHeight() * 1.4f) + 6);
        r.setY (juce::jmax (6, r.getY() - 8));
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

        layoutValueAndSuffix = [aw, te, suffixLabel, editorBaseBounds, isPercentPrompt]()
        {
            int labelW = stringWidth (suffixLabel->getFont(), suffixLabel->getText()) + 2;
            auto er = te->getBounds();

            const auto txt = te->getText();
            const int textW = juce::jmax (1, stringWidth (te->getFont(), txt));
            const bool stickPercentToValue = suffixLabel->getText().containsChar ('%');
            const int spaceW = stickPercentToValue ? 0 : juce::jmax (2, stringWidth (te->getFont(), " "));
            const int minGapPx = juce::jmax (1, spaceW);

            constexpr int kEditorTextPadPx = 12;
            constexpr int kMinEditorWidthPx = 24;
            const int editorW = juce::jlimit (kMinEditorWidthPx,
                                              editorBaseBounds.getWidth(),
                                              textW + (kEditorTextPadPx * 2));
            er.setWidth (editorW);

            const int combinedW = textW + minGapPx + labelW;

            const int contentPad = kPromptInlineContentPadPx;
            const int contentLeft = contentPad;
            const int contentRight = (aw != nullptr ? aw->getWidth() - contentPad : editorBaseBounds.getRight());
            const int contentCenter = (contentLeft + contentRight) / 2;

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

            const int vInset = kPromptSuffixVInsetPx;
            const int baselineOffset = isPercentPrompt ? kPromptSuffixBaselineShapePx : kPromptSuffixBaselineDefaultPx;
            const int labelY = er.getY() + vInset + baselineOffset;
            const int labelH = juce::jmax (1, er.getHeight() - (vInset * 2) - baselineOffset);
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
                maxDecs = 2;
                maxLen = 8; // "10000.00"
            }
        }
        else if (&s == &feedbackSlider)
        {
            minVal = 0.0;
            maxVal = 100.0;    // user types percent
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
            maxVal = 100.0;    // user types percent
            maxDecs = 2;
            maxLen = 6; // "100.00"
        }
        else if (&s == &inputSlider)
        {
            minVal = -96.0;
            maxVal = 24.0;     // dB range
            maxDecs = 2;
            maxLen = 7; // "-96.00"
        }
        else if (&s == &outputSlider)
        {
            minVal = -96.0;
            maxVal = 24.0;     // dB range
            maxDecs = 2;
            maxLen = 7; // "-96.00"
        }
        else if (&s == &mixSlider)
        {
            minVal = 0.0;
            maxVal = 100.0;    // user types percent
            maxDecs = 2;
            maxLen = 6; // "100.00"
        }

        bool isPercent = (&s == &feedbackSlider || &s == &modSlider || &s == &mixSlider);
        
        // Use special filter for time slider in sync mode
        if (&s == &timeSlider && isTimeSyncMode)
            te->setInputFilter (new SyncDivisionInputFilter (maxLen), true);
        else
            te->setInputFilter (new NumericInputFilter (minVal, maxVal, maxLen, maxDecs, isPercent), true);

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

    preparePromptTextEditor (*aw,
                             "val",
                             scheme.bg,
                             scheme.text,
                             scheme.fg,
                             lnf.getAlertWindowMessageFont(),
                             false,
                             6);

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
    for (int i = 0; i < aw->getNumButtons(); ++i)
    {
        if (auto* btn = dynamic_cast<juce::TextButton*>(aw->getButton (i)))
        {
            btn->setColour (juce::TextButton::buttonColourId,   lnf.findColour (juce::TextButton::buttonColourId));
            btn->setColour (juce::TextButton::buttonOnColourId, lnf.findColour (juce::TextButton::buttonOnColourId));
            btn->setColour (juce::TextButton::textColourOffId,  lnf.findColour (juce::TextButton::textColourOffId));
            btn->setColour (juce::TextButton::textColourOnId,   lnf.findColour (juce::TextButton::textColourOnId));
            // font is provided by the look-and-feel already; avoid calling setFont
        }
    }

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
                                     lnf.getAlertWindowMessageFont(),
                                     false,
                                     6);
        });

        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
        aw->repaint();
    }

    // Apply larger font and final layout synchronously so the prompt is
    // fully laid out before being shown (avoids a small delayed re-layout).
    {
        auto bigFont = lnf.getAlertWindowMessageFont();
        bigFont.setHeight (bigFont.getHeight() * 1.5f);
        preparePromptTextEditor (*aw,
                                 "val",
                                 scheme.bg,
                                 scheme.text,
                                 scheme.fg,
                                 bigFont,
                                 false,
                                 6);
        if (auto* suffixLbl = dynamic_cast<juce::Label*> (aw->findChildWithID (kPromptSuffixLabelId)))
            suffixLbl->setFont (bigFont);

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

                // user typed percent for feedback/mod/mix; convert to slider's [0,1] range
                if (safeThis != nullptr && (sliderPtr == &safeThis->feedbackSlider
                                         || sliderPtr == &safeThis->modSlider
                                         || sliderPtr == &safeThis->mixSlider))
                    v *= 0.01;
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

void ECHOTRAudioProcessorEditor::openMidiPortPrompt()
{
    lnf.setScheme (schemes[(size_t) currentSchemeIndex]);
    const auto scheme = schemes[(size_t) currentSchemeIndex];

    const juce::String suffixText = "PORT";
    const int port = audioProcessor.getMidiPort();
    const juce::String currentValue = (port == 0) ? "---" : juce::String (port);
    
    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);
    aw->addTextEditor ("val", currentValue, juce::String()); // sin label, igual que sliders
    
    juce::Label* suffixLabel = nullptr;
    
    // Numeric input filter for MIDI port (1-127 or "---")
    struct MidiPortInputFilter : juce::TextEditor::InputFilter
    {
        juce::String filterNewText (juce::TextEditor& editor, const juce::String& newText) override
        {
            (void) editor; // Suppress unused parameter warning
            // Allow "---" or digits 1-127
            juce::String result;
            for (auto c : newText)
            {
                if (juce::CharacterFunctions::isDigit (c) || c == '-')
                    result += c;
                if (result.length() >= 3)
                    break;
            }
            return result;
        }
    };
    
    juce::Rectangle<int> editorBaseBounds;
    std::function<void()> layoutValueAndSuffix;
    
    if (auto* te = aw->getTextEditor ("val"))
    {
        auto f = lnf.getAlertWindowMessageFont();
        f.setHeight (f.getHeight() * 1.5f);
        te->setFont (f);
        te->applyFontToAllText (f);
        
        auto r = te->getBounds();
        r.setHeight ((int) (f.getHeight() * 1.4f) + 6);
        r.setY (juce::jmax (6, r.getY() - 8));
        editorBaseBounds = r;
        
        suffixLabel = new juce::Label ("suffix", suffixText);
        suffixLabel->setComponentID (kPromptSuffixLabelId);
        suffixLabel->setJustificationType (juce::Justification::centredLeft);
        applyLabelTextColour (*suffixLabel, scheme.text);
        suffixLabel->setBorderSize (juce::BorderSize<int> (0));
        suffixLabel->setFont (f);
        aw->addAndMakeVisible (suffixLabel);
        
        layoutValueAndSuffix = [aw, te, suffixLabel, editorBaseBounds]()
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
            
            const int combinedW = textW + minGapPx + labelW;
            
            const int contentPad = kPromptInlineContentPadPx;
            const int contentLeft = contentPad;
            const int contentRight = (aw != nullptr ? aw->getWidth() - contentPad : editorBaseBounds.getRight());
            const int contentCenter = (contentLeft + contentRight) / 2;
            
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
            
            const int vInset = kPromptSuffixVInsetPx;
            const int baselineOffset = kPromptSuffixBaselineDefaultPx;
            const int labelY = er.getY() + vInset + baselineOffset;
            const int labelH = juce::jmax (1, er.getHeight() - (vInset * 2) - baselineOffset);
            suffixLabel->setBounds (labelX, labelY, labelW, labelH);
        };
        
        te->setBounds (editorBaseBounds);
        int labelW0 = stringWidth (suffixLabel->getFont(), suffixText) + 2;
        suffixLabel->setBounds (r.getRight() + 2, r.getY() + 1, labelW0, juce::jmax (1, r.getHeight() - 2));
        
        if (layoutValueAndSuffix)
            layoutValueAndSuffix();
        
        te->setInputFilter (new MidiPortInputFilter(), true);
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
    
    preparePromptTextEditor (*aw,
                             "val",
                             scheme.bg,
                             scheme.text,
                             scheme.fg,
                             lnf.getAlertWindowMessageFont(),
                             false,
                             6);
    
    if (suffixLabel != nullptr && ! editorBaseBounds.isEmpty())
    {
        if (auto* te = aw->getTextEditor ("val"))
            suffixLabel->setFont (te->getFont());
        if (layoutValueAndSuffix)
            layoutValueAndSuffix();
    }
    
    // Style buttons
    for (int i = 0; i < aw->getNumButtons(); ++i)
    {
        if (auto* btn = dynamic_cast<juce::TextButton*>(aw->getButton (i)))
        {
            btn->setColour (juce::TextButton::buttonColourId,   lnf.findColour (juce::TextButton::buttonColourId));
            btn->setColour (juce::TextButton::buttonOnColourId, lnf.findColour (juce::TextButton::buttonOnColourId));
            btn->setColour (juce::TextButton::textColourOffId,  lnf.findColour (juce::TextButton::textColourOffId));
            btn->setColour (juce::TextButton::textColourOnId,   lnf.findColour (juce::TextButton::textColourOnId));
        }
    }
    
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
        auto bigFont = lnf.getAlertWindowMessageFont();
        bigFont.setHeight (bigFont.getHeight() * 1.5f);
        preparePromptTextEditor (*aw,
                                 "val",
                                 scheme.bg,
                                 scheme.text,
                                 scheme.fg,
                                 bigFont,
                                 false,
                                 6);
        if (auto* suffixLbl = dynamic_cast<juce::Label*> (aw->findChildWithID (kPromptSuffixLabelId)))
            suffixLbl->setFont (bigFont);
        
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
            
            if (txt == "---" || txt.isEmpty())
            {
                safeThis->audioProcessor.setMidiPort (0);
                safeThis->midiPortDisplay.setText ("---", juce::dontSendNotification);
                return;
            }
            
            int port = txt.getIntValue();
            if (port >= 1 && port <= 127)
            {
                safeThis->audioProcessor.setMidiPort (port);
                safeThis->midiPortDisplay.setText (juce::String (port), juce::dontSendNotification);
            }
        }),
        false);
}

void ECHOTRAudioProcessorEditor::openInfoPopup()
{
    lnf.setScheme (schemes[(size_t) currentSchemeIndex]);

    setPromptOverlayActive (true);

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
    juce::Component::SafePointer<ECHOTRAudioProcessorEditor> safeThis (this);
    aw->setLookAndFeel (&lnf);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("GRAPHICS", 2);

    applyPromptShellSize (*aw);

    auto* infoLabel = new juce::Label ("infoText", "NMSTR -> INFO SOON");
    infoLabel->setComponentID ("infoText");
    infoLabel->setJustificationType (juce::Justification::centred);
    applyLabelTextColour (*infoLabel, schemes[(size_t) currentSchemeIndex].text);
    auto infoFont = lnf.getAlertWindowMessageFont();
    infoFont.setHeight (infoFont.getHeight() * 1.45f);
    infoLabel->setFont (infoFont);
    aw->addAndMakeVisible (infoLabel);

    auto* infoLink = new juce::HyperlinkButton ("GitHub Repository",
                                                juce::URL ("https://github.com/lmaser/ECHO-TR"));
    infoLink->setComponentID ("infoLink");
    infoLink->setJustificationType (juce::Justification::centred);
    infoLink->setColour (juce::HyperlinkButton::textColourId,
                         schemes[(size_t) currentSchemeIndex].text);
    auto linkFont = infoFont;
    linkFont.setHeight (infoFont.getHeight() * 0.72f);
    infoLink->setFont (linkFont, false, juce::Justification::centred);
    aw->addAndMakeVisible (infoLink);
    
    // Capture infoLink to delete explicitly before AlertWindow destruction
    juce::Component::SafePointer<juce::HyperlinkButton> safeInfoLink (infoLink);

    layoutInfoPopupContent (*aw);

    if (safeThis != nullptr)
    {
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

        safeAw->centreAroundComponent (safeThis.getComponent(), safeAw->getWidth(), safeAw->getHeight());
        bringPromptWindowToFront (*safeAw);

        layoutInfoPopupContent (*safeAw);

        safeAw->repaint();
    });

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis = juce::Component::SafePointer<ECHOTRAudioProcessorEditor> (this), aw, safeInfoLink] (int result) mutable
        {
            // Explicitly delete the HyperlinkButton before destroying the AlertWindow
            if (safeInfoLink != nullptr)
                delete safeInfoLink.getComponent();
            
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
    lnf.setScheme (schemes[(size_t) currentSchemeIndex]);

    useCustomPalette = audioProcessor.getUiUseCustomPalette();
    fxTailEnabled = audioProcessor.getUiFxTailEnabled();
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
        applyLabelTextColour (*label, schemes[(size_t) currentSchemeIndex].text);
        label->setBorderSize (juce::BorderSize<int> (0));
        label->setFont (font);
        label->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        aw->addAndMakeVisible (label);
        return label;
    };

    auto stylePromptButtons = [this] (juce::AlertWindow& alert)
    {
        for (int bi = 0; bi < alert.getNumButtons(); ++bi)
        {
            if (auto* btn = dynamic_cast<juce::TextButton*> (alert.getButton (bi)))
            {
                btn->setColour (juce::TextButton::buttonColourId,   lnf.findColour (juce::TextButton::buttonColourId));
                btn->setColour (juce::TextButton::buttonOnColourId, lnf.findColour (juce::TextButton::buttonOnColourId));
                btn->setColour (juce::TextButton::textColourOffId,  lnf.findColour (juce::TextButton::textColourOffId));
                btn->setColour (juce::TextButton::textColourOnId,   lnf.findColour (juce::TextButton::textColourOnId));
            }
        }
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

    for (int i = 0; i < 4; ++i)
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
    fxToggle->setToggleState (fxTailEnabled, juce::dontSendNotification);
    fxToggle->onClick = [safeThis, fxToggle]()
    {
        if (safeThis == nullptr || fxToggle == nullptr)
            return;

        safeThis->fxTailEnabled = fxToggle->getToggleState();
        safeThis->audioProcessor.setUiFxTailEnabled (safeThis->fxTailEnabled);
        safeThis->repaint();
    };
    aw->addAndMakeVisible (fxToggle);

    auto* fxLabel = addPopupLabel ("fxLabel", "TEXT FX", labelFont);

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

    for (int i = 0; i < 4; ++i)
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

            customSwatch->onRightClick = [safeThis, safeAw, i, stylePromptButtons]()
            {
                if (safeThis == nullptr)
                    return;

                const auto scheme = safeThis->schemes[(size_t) safeThis->currentSchemeIndex];

                auto* colorAw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
                colorAw->setLookAndFeel (&safeThis->lnf);
                colorAw->addTextEditor ("hex", colourToHexRgb (safeThis->customPalette[(size_t) i]), juce::String());
                colorAw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                colorAw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

                stylePromptButtons (*colorAw);

                applyPromptShellSize (*colorAw);
                layoutAlertWindowButtons (*colorAw);

                preparePromptTextEditor (*colorAw,
                                         "hex",
                                         scheme.bg,
                                         scheme.text,
                                         scheme.fg,
                                         safeThis->lnf.getAlertWindowMessageFont(),
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
                                                 safeThis->lnf.getAlertWindowMessageFont(),
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
                                         safeThis->lnf.getAlertWindowMessageFont(),
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

            syncGraphicsPopupState (*safeAw, safeThis->defaultPalette, safeThis->customPalette, safeThis->useCustomPalette);
            layoutGraphicsPopupContent (*safeAw);
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
    const bool isSyncOn = syncButton.getToggleState();
    if (isSyncOn)
    {
        const int idx = (int) timeSlider.getValue();
        return audioProcessor.getTimeSyncName (idx);
    }
    
    const float ms = (float) timeSlider.getValue();
    if (ms >= 1000.0f)
        return juce::String (ms / 1000.0f, 3) + " S TIME";
    return juce::String ((int)ms) + " MS TIME";
}

juce::String ECHOTRAudioProcessorEditor::getTimeTextShort() const
{
    const bool isSyncOn = syncButton.getToggleState();
    if (isSyncOn)
    {
        const int idx = (int) timeSlider.getValue();
        return audioProcessor.getTimeSyncNameShort (idx);
    }
    
    const float ms = (float) timeSlider.getValue();
    if (ms >= 1000.0f)
        return juce::String (ms / 1000.0f, 3) + " S";
    return juce::String ((int)ms) + " MS";
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
        case 0: return "MONO MODE";
        case 1: return "STEREO MODE";
        case 2: return "PING-PONG MODE";
        default: return "STEREO MODE";
    }
}

juce::String ECHOTRAudioProcessorEditor::getModeTextShort() const
{
    const int mode = (int) modeSlider.getValue();
    switch (mode)
    {
        case 0: return "MONO";
        case 1: return "STR";
        case 2: return "PP";
        default: return "STR";
    }
}

juce::String ECHOTRAudioProcessorEditor::getModText() const
{
    const float val = (float) modSlider.getValue();
    
    if (val < 0.5f)
    {
        // 0.0 = /4, 0.5 = x1
        float divisor = 4.0f - 3.0f * (val / 0.5f);
        if (std::abs(divisor - 1.0f) < 0.01f)
            return "X1 MOD";
        return "/" + juce::String(divisor, 2) + " MOD";
    }
    else
    {
        // 0.5 = x1, 1.0 = x4
        float multiplier = 1.0f + 3.0f * ((val - 0.5f) / 0.5f);
        if (std::abs(multiplier - 1.0f) < 0.01f)
            return "X1 MOD";
        return "X" + juce::String(multiplier, 2) + " MOD";
    }
}

juce::String ECHOTRAudioProcessorEditor::getModTextShort() const
{
    const float val = (float) modSlider.getValue();
    
    if (val < 0.5f)
    {
        float divisor = 4.0f - 3.0f * (val / 0.5f);
        if (std::abs(divisor - 1.0f) < 0.01f)
            return "X1 MOD";
        return "/" + juce::String(divisor, 1) + " MOD";
    }
    else
    {
        float multiplier = 1.0f + 3.0f * ((val - 0.5f) / 0.5f);
        if (std::abs(multiplier - 1.0f) < 0.01f)
            return "X1 MOD";
        return "X" + juce::String(multiplier, 1) + " MOD";
    }
}

juce::String ECHOTRAudioProcessorEditor::getInputText() const
{
    const float db = (float) inputSlider.getValue();
    if (db <= -80.0f)
        return "-INF DB INPUT";
    return juce::String (db, 1) + " DB INPUT";
}

juce::String ECHOTRAudioProcessorEditor::getInputTextShort() const
{
    const float db = (float) inputSlider.getValue();
    if (db <= -80.0f)
        return "-INF IN";
    return juce::String (db, 1) + " IN";
}

juce::String ECHOTRAudioProcessorEditor::getOutputText() const
{
    const float db = (float) outputSlider.getValue();
    if (db <= -80.0f)
        return "-INF DB OUTPUT";
    return juce::String (db, 1) + " DB OUTPUT";
}

juce::String ECHOTRAudioProcessorEditor::getOutputTextShort() const
{
    const float db = (float) outputSlider.getValue();
    if (db <= -80.0f)
        return "-INF OUT";
    return juce::String (db, 1) + " OUT";
}

juce::String ECHOTRAudioProcessorEditor::getMixText() const
{
    const int pct = (int) std::lround (mixSlider.getValue() * 100.0);
    return juce::String (pct) + "% MIX";
}

juce::String ECHOTRAudioProcessorEditor::getMixTextShort() const
{
    const int pct = (int) std::lround (mixSlider.getValue() * 100.0);
    return juce::String (pct) + "% MX";
}

namespace
{
    constexpr const char* kTimeLegendFull  = "2000 MS TIME";
    constexpr const char* kTimeLegendShort = "2000 MS";  // Use MS format (wider than "2.000 S")
    constexpr const char* kTimeLegendInt   = "2000";

    constexpr const char* kFeedbackLegendFull  = "100% FEEDBACK";
    constexpr const char* kFeedbackLegendShort = "100% FBK";
    constexpr const char* kFeedbackLegendInt   = "100";

    constexpr const char* kModeLegendFull  = "STEREO MODE";
    constexpr const char* kModeLegendShort = "STR MODE";
    constexpr const char* kModeLegendInt   = "STR";

    constexpr const char* kModLegendFull  = "X4.00 MOD";
    constexpr const char* kModLegendShort = "X4.0 MOD";
    constexpr const char* kModLegendInt   = "X4";

    constexpr const char* kInputLegendFull  = "-100.0 DB INPUT";
    constexpr const char* kInputLegendShort = "-100.0 IN";
    constexpr const char* kInputLegendInt   = "-100";

    constexpr const char* kOutputLegendFull  = "-100.0 DB OUTPUT";
    constexpr const char* kOutputLegendShort = "-100.0 OUT";
    constexpr const char* kOutputLegendInt   = "-100";

    constexpr const char* kMixLegendFull  = "100% MIX";
    constexpr const char* kMixLegendShort = "100% MX";
    constexpr const char* kMixLegendInt   = "100";

    constexpr int kValueAreaHeightPx = 44;
    constexpr int kValueAreaRightMarginPx = 24;
    constexpr int kToggleLabelGapPx = 4;
    constexpr int kToggleLabelRightPadPx = 10;
    constexpr int kResizerCornerPx = 22;
    constexpr int kToggleBoxPx = 72;
    constexpr int kMinToggleBlocksGapPx = 10;
    constexpr int kMinSliderGapPx = 4;
    constexpr int kMidiPortSelectorWidthPx = 46;
    constexpr int kMidiPortGapPx = 3;

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
        int btnY = 0;
        int availableForSliders = 0;
        int barH = 0;
        int gapY = 0;
        int topY = 0;
    };

    HorizontalLayoutMetrics makeHorizontalLayoutMetrics (int editorW, int valueW)
    {
        HorizontalLayoutMetrics m;
        m.barW = (int) std::round (editorW * 0.455);
        m.valuePad = (int) std::round (editorW * 0.02);
        m.valueW = valueW;
        m.contentW = m.barW + m.valuePad + m.valueW;
        m.leftX = juce::jmax (6, (editorW - m.contentW) / 2);
        return m;
    }

    VerticalLayoutMetrics makeVerticalLayoutMetrics (int editorH, int layoutVerticalBiasPx)
    {
        VerticalLayoutMetrics m;
        m.rhythm = juce::jlimit (6, 16, (int) std::round (editorH * 0.018));
        const int nominalBarH = juce::jlimit (14, 120, m.rhythm * 6);
        const int nominalGapY = juce::jmax (4, m.rhythm * 4);

        m.titleH = juce::jlimit (24, 56, m.rhythm * 4);
        m.titleAreaH = m.titleH + 4;
        const int computedTitleTopPad = 6 + layoutVerticalBiasPx;
        m.titleTopPad = (computedTitleTopPad > 8) ? computedTitleTopPad : 8;
        const int titleGap = m.titleTopPad;
        m.topMargin = m.titleTopPad + m.titleAreaH + titleGap;
        m.betweenSlidersAndButtons = juce::jmax (8, m.rhythm * 2);
        m.bottomMargin = m.titleTopPad;

        m.box = kToggleBoxPx;
        m.btnY = editorH - m.bottomMargin - m.box;
        m.availableForSliders = juce::jmax (40, m.btnY - m.betweenSlidersAndButtons - m.topMargin);

        const int nominalStack = 7 * nominalBarH + 6 * nominalGapY;
        const double stackScale = nominalStack > 0 ? juce::jmin (1.0, (double) m.availableForSliders / (double) nominalStack)
                                                   : 1.0;

        m.barH = juce::jmax (14, (int) std::round (nominalBarH * stackScale));
        m.gapY = juce::jmax (4,  (int) std::round (nominalGapY * stackScale));

        auto stackHeight = [&]() { return 7 * m.barH + 6 * m.gapY; };

        while (stackHeight() > m.availableForSliders && m.gapY > 4)
            --m.gapY;

        while (stackHeight() > m.availableForSliders && m.barH > 14)
            --m.barH;

        m.topY = m.topMargin;
        return m;
    }
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

    constexpr float baseFontPx = 40.0f;
    juce::Font font (juce::FontOptions (baseFontPx).withStyle ("Bold"));

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
    const auto layout = makeHorizontalLayoutMetrics (getWidth(), getTargetValueColumnWidth());

    const int valueX = barBounds.getRight() + layout.valuePad;
    const int maxW = juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx);
    const int valueW = juce::jmin (layout.valueW, maxW);

    const int y = barBounds.getCentreY() - (kValueAreaHeightPx / 2);
    return { valueX, y, juce::jmax (0, valueW), kValueAreaHeightPx };
}

juce::Slider* ECHOTRAudioProcessorEditor::getSliderForValueAreaPoint (juce::Point<int> p)
{
    if (getValueAreaFor (timeSlider.getBounds()).contains (p))
        return &timeSlider;

    if (getValueAreaFor (feedbackSlider.getBounds()).contains (p))
        return &feedbackSlider;

    if (getValueAreaFor (modeSlider.getBounds()).contains (p))
        return &modeSlider;

    if (getValueAreaFor (modSlider.getBounds()).contains (p))
        return &modSlider;

    if (getValueAreaFor (inputSlider.getBounds()).contains (p))
        return &inputSlider;

    if (getValueAreaFor (outputSlider.getBounds()).contains (p))
        return &outputSlider;

    if (getValueAreaFor (mixSlider.getBounds()).contains (p))
        return &mixSlider;

    return nullptr;
}

namespace
{
    int getToggleVisualBoxSidePx (const juce::Component& button)
    {
        const int h = button.getHeight();
        return juce::jlimit (14, juce::jmax (14, h - 2), (int) std::lround ((double) h * 0.50));
    }

    int getToggleVisualBoxLeftPx (const juce::Component& button)
    {
        return button.getX() + 2;
    }

    juce::Rectangle<int> makeToggleLabelArea (const juce::Component& button,
                                              int editorWidth,
                                              const juce::String& labelText,
                                              const juce::String& shortLabelText)
    {
        const auto b = button.getBounds();
        const int visualRight = getToggleVisualBoxLeftPx (button) + getToggleVisualBoxSidePx (button);
        const int x = visualRight + kToggleLabelGapPx;

        juce::Font labelFont (juce::FontOptions (40.0f).withStyle ("Bold"));
        const int desiredFullW = stringWidth (labelFont, labelText) + 2;
        const int desiredShortW = stringWidth (labelFont, shortLabelText) + 2;
        const int maxW = juce::jmax (0, editorWidth - x - kToggleLabelRightPadPx);
        
        // Choose short label if full doesn't fit
        const int w = (desiredFullW <= maxW) ? desiredFullW : juce::jmin (desiredShortW, maxW);

        return { x, b.getY(), w, b.getHeight() };
    }
}

juce::Rectangle<int> ECHOTRAudioProcessorEditor::getSyncLabelArea() const
{
    return makeToggleLabelArea (syncButton, getWidth(), "SYNC", "SNC");
}

juce::Rectangle<int> ECHOTRAudioProcessorEditor::getAutoFbkLabelArea() const
{
    return makeToggleLabelArea (autoFbkButton, getWidth(), "AUTO", "AT");
}

juce::Rectangle<int> ECHOTRAudioProcessorEditor::getMidiLabelArea() const
{
    const auto b = midiButton.getBounds();
    const int visualRight = getToggleVisualBoxLeftPx (midiButton) + getToggleVisualBoxSidePx (midiButton);
    const int x = visualRight + kToggleLabelGapPx;
    
    // Use same logic as paint() getButtonLabel for consistency
    // Collision is with MIDI port display, not the value column
    const int collisionRight = midiPortDisplay.getX() - kToggleLegendCollisionPadPx;
    
    juce::Font labelFont (juce::FontOptions (40.0f).withStyle ("Bold"));
    const int fullW = stringWidth (labelFont, "MIDI") + 2;
    const int shortW = stringWidth (labelFont, "MD") + 2;
    const int maxW = juce::jmax (0, collisionRight - x);
    
    const int w = (fullW <= maxW) ? fullW : shortW;
    
    return { x, b.getY(), w, b.getHeight() };
}

juce::Rectangle<int> ECHOTRAudioProcessorEditor::getInfoIconArea() const
{
    const auto timeValueArea = getValueAreaFor (timeSlider.getBounds());
    const int contentRight = timeValueArea.getRight();
    const auto verticalLayout = makeVerticalLayoutMetrics (getHeight(), kLayoutVerticalBiasPx);
    const int titleH = verticalLayout.titleH;
    const int titleY = verticalLayout.titleTopPad;
    const int titleAreaH = verticalLayout.titleAreaH;
    const int size = juce::jlimit (20, 36, titleH);

    const int x = contentRight - size;
    const int y = titleY + juce::jmax (0, (titleAreaH - size) / 2);
    return { x, y, size, size };
}

void ECHOTRAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    lastUserInteractionMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    const auto p = e.getPosition();

    // Check if click is on MIDI port display (only if visible)
    if (midiPortDisplay.isVisible() && midiPortDisplay.getBounds().contains (p))
    {
        openMidiPortPrompt();
        return;
    }

    if (e.mods.isPopupMenu())
    {
        if (auto* slider = getSliderForValueAreaPoint (p))
        {
            openNumericEntryPopupForSlider (*slider);
            return;
        }
    }

    if (getInfoIconArea().contains (p))
    {
        openInfoPopup();
        return;
    }

    if (getSyncLabelArea().contains (p))
    {
        syncButton.setToggleState (! syncButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    if (getAutoFbkLabelArea().contains (p))
    {
        autoFbkButton.setToggleState (! autoFbkButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    if (getMidiLabelArea().contains (p))
    {
        midiButton.setToggleState (! midiButton.getToggleState(), juce::sendNotificationSync);
        return;
    }
}

void ECHOTRAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    (void) e;
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
    const auto horizontalLayout = makeHorizontalLayoutMetrics (W, getTargetValueColumnWidth());
    const auto verticalLayout = makeVerticalLayoutMetrics (getHeight(), kLayoutVerticalBiasPx);
    const auto timeValueArea = getValueAreaFor (timeSlider.getBounds());
    const auto feedbackValueArea = getValueAreaFor (feedbackSlider.getBounds());
    const auto modeValueArea = getValueAreaFor (modeSlider.getBounds());
    const auto modValueArea = getValueAreaFor (modSlider.getBounds());
    const auto inputValueArea = getValueAreaFor (inputSlider.getBounds());
    const auto outputValueArea = getValueAreaFor (outputSlider.getBounds());
    const auto mixValueArea = getValueAreaFor (mixSlider.getBounds());

    const auto scheme = schemes[(size_t) currentSchemeIndex];
    const bool useShortLabels = (labelVisibilityMode == 1);
    const bool shouldHideUnitLabels = (labelVisibilityMode == 2);

    g.fillAll (scheme.bg);
    g.setColour (scheme.text);

    constexpr float baseFontPx = 40.0f;
    constexpr float minFontPx  = 18.0f;
    const juce::String barTailTuning; // "" = sin modificación; ejemplos: "80%", "-1"

    g.setFont (juce::Font (juce::FontOptions (baseFontPx).withStyle ("Bold")));

    auto drawAlignedLegend = [&] (const juce::Rectangle<int>& area,
                                  const juce::String& text,
                                  bool useAutoMargin,
                                  bool useTailEffect,
                                  bool tailFromSuffixToLeft,
                                  bool lowercaseTailChars,
                                  const juce::String& tailTuning)
    {
        auto t = text.toUpperCase().trim();
        const int split = t.lastIndexOfChar (' ');
        if (split <= 0 || split >= t.length() - 1)
            return drawValueNoEllipsis (g, area, t, juce::String(), t, baseFontPx, minFontPx), void();

        const auto value = t.substring (0, split).trimEnd();
        const auto suffix = t.substring (split + 1).trimStart();
        const auto* tailGradient = (useTailEffect && fxTailEnabled) ? &lnf.getTrailingTextGradient() : nullptr;

        if (! drawValueWithRightAlignedSuffix (g, area, value, suffix, useAutoMargin, baseFontPx, minFontPx,
                                               tailGradient, tailFromSuffixToLeft, lowercaseTailChars, tailTuning))
            drawValueNoEllipsis (g, area, t, juce::String(), value, baseFontPx, minFontPx);

        g.setColour (scheme.text);
    };

    auto drawLegendForMode = [&] (const juce::Rectangle<int>& area,
                                  const juce::String& fullLegend,
                                  const juce::String& shortLegend,
                                  const juce::String& intOnlyLegend,
                                  const juce::String& tailTuning)
    {
        if (shouldHideUnitLabels)
        {
            drawValueNoEllipsis (g, area,
                                 intOnlyLegend,
                                 juce::String(),
                                 intOnlyLegend,
                                 baseFontPx, minFontPx);
            return;
        }

        drawAlignedLegend (area,
                           useShortLabels ? shortLegend : fullLegend,
                           false,
                           true,
                           true,
                           true,
                           tailTuning);
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

        // Keep tail behaviour exactly as original (unchanged params / unchanged font).
        if (fxTailEnabled)
        {
            drawTextWithRepeatedLastCharGradient (g, titleArea, titleText, barW, lnf.getTrailingTextGradient(), titleX + barW,
                          juce::String(), "20%", "pyramid");
        }
        else
        {
            g.drawText (titleText, titleArea.getX(), titleArea.getY(), titleArea.getWidth(), titleArea.getHeight(), juce::Justification::left, false);
        }

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
            for (float h = (float) titleH; h >= 12.0f; h -= 1.0f)
            {
                fittedTitleFont.setHeight (h);
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
            g.drawText ("v1.0b", versionX, versionY, versionW, versionH,
                juce::Justification::bottomRight, false);

        g.setFont (juce::Font (juce::FontOptions (baseFontPx).withStyle ("Bold")));
    }

    {
        drawLegendForMode (timeValueArea,
                           cachedTimeTextFull,
                           cachedTimeTextShort,
                           juce::String (( int) timeSlider.getValue()),
                           barTailTuning);
    }

    {
        drawLegendForMode (feedbackValueArea,
                           cachedFeedbackTextFull,
                           cachedFeedbackTextShort,
                           juce::String ((int) std::lround (feedbackSlider.getValue() * 100.0)),
                           barTailTuning);
    }

    {
        drawLegendForMode (modeValueArea,
                           cachedModeTextFull,
                           cachedModeTextShort,
                           juce::String ((int) modeSlider.getValue()),
                           barTailTuning);
    }

    {
        drawLegendForMode (modValueArea,
                           cachedModTextFull,
                           cachedModTextShort,
                           juce::String ((int) modSlider.getValue()),
                           barTailTuning);
    }

    {
        drawLegendForMode (inputValueArea,
                           cachedInputTextFull,
                           cachedInputTextShort,
                           juce::String ((int) inputSlider.getValue()),
                           barTailTuning);
    }

    {
        drawLegendForMode (outputValueArea,
                           cachedOutputTextFull,
                           cachedOutputTextShort,
                           juce::String ((int) outputSlider.getValue()),
                           barTailTuning);
    }

    {
        drawLegendForMode (mixValueArea,
                           cachedMixTextFull,
                           cachedMixTextShort,
                           juce::String ((int) std::lround (mixSlider.getValue() * 100.0)),
                           barTailTuning);
    }

    {
        // Determine which labels to use based on available space INCLUDING collision bounds
        juce::Font labelFont (juce::FontOptions (40.0f).withStyle ("Bold"));
        
        auto getButtonLabel = [&] (const juce::Component& button, const juce::String& fullText, const juce::String& shortText, int collisionRight) -> juce::String
        {
            const int visualRight = getToggleVisualBoxLeftPx (button) + getToggleVisualBoxSidePx (button);
            const int x = visualRight + kToggleLabelGapPx;
            const int maxW = juce::jmax (0, collisionRight - x);
            const int fullW = stringWidth (labelFont, fullText) + 2;
            return (fullW <= maxW) ? fullText : shortText;
        };
        
        const juce::String syncLabel = getButtonLabel (syncButton, "SYNC", "SNC", autoFbkButton.getX() - kToggleLegendCollisionPadPx);
        const juce::String autoLabel = getButtonLabel (autoFbkButton, "AUTO", "AT", midiButton.getX() - kToggleLegendCollisionPadPx);
        const juce::String midiLabel = getButtonLabel (midiButton, "MIDI", "MD", midiPortDisplay.getX() - kToggleLegendCollisionPadPx);
        
        auto drawToggleLegend = [&] (const juce::Rectangle<int>& labelArea,
                                     const juce::String& labelText,
                                     int noCollisionRight,
                                     const juce::String& tailTuning)
        {
            const int safeW = juce::jmax (0, noCollisionRight - labelArea.getX());
            // snap to integer/even coordinates to avoid sub-pixel artefacts on resize
            auto snapEven = [] (int v) { return v & ~1; };
            const int ax = snapEven (labelArea.getX());
            const int ay = snapEven (labelArea.getY());
            const int aw = snapEven (safeW);
            const int ah = labelArea.getHeight();
            const auto drawArea = juce::Rectangle<int> (ax, ay, aw, ah);

            if (fxTailEnabled)
                drawTextWithRepeatedLastCharGradient (g, drawArea, labelText, getWidth(), lnf.getTrailingTextGradient(), noCollisionRight,
                                      tailTuning, "20%", "pyramid");
            else
                g.drawText (labelText, drawArea.getX(), drawArea.getY(), drawArea.getWidth(), drawArea.getHeight(), juce::Justification::left, true);
        };

        drawToggleLegend (getSyncLabelArea(), syncLabel, autoFbkButton.getX() - kToggleLegendCollisionPadPx, "-3");
        drawToggleLegend (getAutoFbkLabelArea(), autoLabel, midiButton.getX() - kToggleLegendCollisionPadPx, "-3");
        
        // Adjust MIDI label collision boundary based on port visibility
        const int midiLabelRightBound = midiPortDisplay.isVisible() 
            ? midiPortDisplay.getX() - kToggleLegendCollisionPadPx
            : W - kToggleLegendCollisionPadPx;
        drawToggleLegend (getMidiLabelArea(), midiLabel, midiLabelRightBound, "-2");
    }
    
    // Draw MIDI port display border (matching checkbox style) - only if visible
    if (midiPortDisplay.isVisible())
    {
        auto portBounds = midiPortDisplay.getBounds().toFloat();
        
        // Draw background (same as checkbox)
        g.setColour (scheme.bg);
        g.fillRect (portBounds);
        
        // Draw border with same width as checkbox
        g.setColour (scheme.outline);
        g.drawRect (portBounds, 4.0f);
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

    // Draw white border around MIDI port display (like checkboxes) - only if visible
    if (midiPortDisplay.isVisible())
    {
        g.setColour (scheme.outline);
        auto portBounds = midiPortDisplay.getBounds();
        g.drawRect (portBounds, 1);
    }

}



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

void ECHOTRAudioProcessorEditor::updateLegendVisibility()
{
    constexpr float baseFontPx = 40.0f;
    constexpr float minFontPx  = 18.0f;
    const float softShrinkFloorFull  = juce::jmax (minFontPx, baseFontPx * 0.88f);
    const float softShrinkFloorShort = minFontPx;

    juce::Font measureFont (juce::FontOptions (baseFontPx).withStyle ("Bold"));

    auto areaTime = getValueAreaFor (timeSlider.getBounds());
    auto areaFeedback = getValueAreaFor (feedbackSlider.getBounds());
    auto areaMode = getValueAreaFor (modeSlider.getBounds());
    auto areaMod = getValueAreaFor (modSlider.getBounds());
    auto areaInput = getValueAreaFor (inputSlider.getBounds());
    auto areaOutput = getValueAreaFor (outputSlider.getBounds());
    auto areaMix = getValueAreaFor (mixSlider.getBounds());

    // Check FULL versions using fixed worst-case templates (stable, value-independent)
    const juce::String timeFull = kTimeLegendFull;
    const juce::String feedbackFull = kFeedbackLegendFull;
    const juce::String modeFull = kModeLegendFull;
    const juce::String modFull = kModLegendFull;
    const juce::String inputFull = kInputLegendFull;
    const juce::String outputFull = kOutputLegendFull;
    const juce::String mixFull = kMixLegendFull;

    const bool timeFullFits = fitsWithOptionalShrink_NoG (measureFont, timeFull, areaTime.getWidth(), baseFontPx, softShrinkFloorFull);
    const bool feedbackFullFits = fitsWithOptionalShrink_NoG (measureFont, feedbackFull, areaFeedback.getWidth(), baseFontPx, softShrinkFloorFull);
    const bool modeFullFits = fitsWithOptionalShrink_NoG (measureFont, modeFull, areaMode.getWidth(), baseFontPx, softShrinkFloorFull);
    const bool modFullFits = fitsWithOptionalShrink_NoG (measureFont, modFull, areaMod.getWidth(), baseFontPx, softShrinkFloorFull);
    const bool inputFullFits = fitsWithOptionalShrink_NoG (measureFont, inputFull, areaInput.getWidth(), baseFontPx, softShrinkFloorFull);
    const bool outputFullFits = fitsWithOptionalShrink_NoG (measureFont, outputFull, areaOutput.getWidth(), baseFontPx, softShrinkFloorFull);
    const bool mixFullFits = fitsWithOptionalShrink_NoG (measureFont, mixFull, areaMix.getWidth(), baseFontPx, softShrinkFloorFull);

    // Check SHORT versions using fixed worst-case templates (stable, value-independent)
    const juce::String timeShort = kTimeLegendShort;
    const juce::String feedbackShort = kFeedbackLegendShort;
    const juce::String modeShort = kModeLegendShort;
    const juce::String modShort = kModLegendShort;
    const juce::String inputShort = kInputLegendShort;
    const juce::String outputShort = kOutputLegendShort;
    const juce::String mixShort = kMixLegendShort;

    const bool timeShortFits = fitsWithOptionalShrink_NoG (measureFont, timeShort, areaTime.getWidth(), baseFontPx, softShrinkFloorShort);
    const bool feedbackShortFits = fitsWithOptionalShrink_NoG (measureFont, feedbackShort, areaFeedback.getWidth(), baseFontPx, softShrinkFloorShort);
    const bool modeShortFits = fitsWithOptionalShrink_NoG (measureFont, modeShort, areaMode.getWidth(), baseFontPx, softShrinkFloorShort);
    const bool modShortFits = fitsWithOptionalShrink_NoG (measureFont, modShort, areaMod.getWidth(), baseFontPx, softShrinkFloorShort);
    const bool inputShortFits = fitsWithOptionalShrink_NoG (measureFont, inputShort, areaInput.getWidth(), baseFontPx, softShrinkFloorShort);
    const bool outputShortFits = fitsWithOptionalShrink_NoG (measureFont, outputShort, areaOutput.getWidth(), baseFontPx, softShrinkFloorShort);
    const bool mixShortFits = fitsWithOptionalShrink_NoG (measureFont, mixShort, areaMix.getWidth(), baseFontPx, softShrinkFloorShort);

    // Determine global mode: 0=Full, 1=Short, 2=None
    const bool anyFullFailed = (!timeFullFits || !feedbackFullFits || !modeFullFits || !modFullFits 
                             || !inputFullFits || !outputFullFits || !mixFullFits);
    const bool anyShortFailed = (!timeShortFits || !feedbackShortFits || !modeShortFits || !modShortFits 
                              || !inputShortFits || !outputShortFits || !mixShortFits);

    if (anyShortFailed)
        labelVisibilityMode = 2;  // None
    else if (anyFullFailed)
        labelVisibilityMode = 1;  // Short
    else
        labelVisibilityMode = 0;  // Full
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

    const auto horizontalLayout = makeHorizontalLayoutMetrics (W, getTargetValueColumnWidth());
    const auto verticalLayout = makeVerticalLayoutMetrics (H, kLayoutVerticalBiasPx);

    // Position 7 sliders in 7 separate rows
    timeSlider.setBounds     (horizontalLayout.leftX, verticalLayout.topY + 0 * (verticalLayout.barH + verticalLayout.gapY), horizontalLayout.barW, verticalLayout.barH);
    feedbackSlider.setBounds (horizontalLayout.leftX, verticalLayout.topY + 1 * (verticalLayout.barH + verticalLayout.gapY), horizontalLayout.barW, verticalLayout.barH);
    modeSlider.setBounds     (horizontalLayout.leftX, verticalLayout.topY + 2 * (verticalLayout.barH + verticalLayout.gapY), horizontalLayout.barW, verticalLayout.barH);
    modSlider.setBounds      (horizontalLayout.leftX, verticalLayout.topY + 3 * (verticalLayout.barH + verticalLayout.gapY), horizontalLayout.barW, verticalLayout.barH);
    inputSlider.setBounds    (horizontalLayout.leftX, verticalLayout.topY + 4 * (verticalLayout.barH + verticalLayout.gapY), horizontalLayout.barW, verticalLayout.barH);
    outputSlider.setBounds   (horizontalLayout.leftX, verticalLayout.topY + 5 * (verticalLayout.barH + verticalLayout.gapY), horizontalLayout.barW, verticalLayout.barH);
    mixSlider.setBounds      (horizontalLayout.leftX, verticalLayout.topY + 6 * (verticalLayout.barH + verticalLayout.gapY), horizontalLayout.barW, verticalLayout.barH);

    // Button area: SNC aligns with bars (leftX), MIDI aligns with value legends in justified mode
    const int buttonAreaX = horizontalLayout.leftX;
    const int buttonAreaW = horizontalLayout.contentW;
    
    // Calculate value area X position for MIDI block alignment
    const int valueAreaX = horizontalLayout.leftX + horizontalLayout.barW + horizontalLayout.valuePad;
    const int valueAreaW = horizontalLayout.valueW;

    juce::Font labelFont (juce::FontOptions (40.0f).withStyle ("Bold"));
    const int labelGap = kToggleLabelGapPx;

    const int toggleVisualSide = juce::jlimit (14,
                                               juce::jmax (14, verticalLayout.box - 2),
                                               (int) std::lround ((double) verticalLayout.box * 0.50));
    const int toggleHitW = toggleVisualSide + 6;

    // Calculate label widths for both full and short versions
    const int syncFullW = stringWidth (labelFont, "SYNC") + 2;
    const int syncShortW = stringWidth (labelFont, "SNC") + 2;
    const int autoFullW = stringWidth (labelFont, "AUTO") + 2;
    const int autoShortW = stringWidth (labelFont, "AT") + 2;
    const int midiFullW = stringWidth (labelFont, "MIDI") + 2;
    const int midiShortW = stringWidth (labelFont, "MD") + 2;

    // Determine if we need to use short labels based on available space
    const int minBlockW = toggleHitW;
    const int midiPortSide = toggleVisualSide;  // MIDI port uses same size as checkbox
    const int syncFullBlockW = minBlockW + labelGap + syncFullW;
    const int autoFullBlockW = minBlockW + labelGap + autoFullW;
    const int midiFullBlockW = minBlockW + labelGap + midiFullW + kMidiPortGapPx + midiPortSide;
    const int totalFullW = syncFullBlockW + autoFullBlockW + midiFullBlockW + 2 * kMinToggleBlocksGapPx;

    const bool useShortLabels = (totalFullW > buttonAreaW);

    const int syncLabelW = useShortLabels ? syncShortW : syncFullW;
    const int autoLabelW = useShortLabels ? autoShortW : autoFullW;
    const int midiLabelW = useShortLabels ? midiShortW : midiFullW;
    
    // Recalculate block widths with selected labels
    const int syncBlockW = juce::jmax (toggleHitW, toggleHitW + labelGap + syncLabelW);
    const int autoBlockW = juce::jmax (toggleHitW, toggleHitW + labelGap + autoLabelW);
    const int midiLabelBlockW = juce::jmax (toggleHitW, toggleHitW + labelGap + midiLabelW);
    const int midiBlockW = midiLabelBlockW + kMidiPortGapPx + midiPortSide;
    
    // Choose layout strategy based on available space
    int syncBlockX, autoBlockX, midiBlockX;
    
    if (useShortLabels)
    {
        // LEFT-PACKED LAYOUT: When space is tight, pack all elements to the left with uniform small gaps
        // This creates a compact, visually balanced layout where MD stays adjacent to its port
        constexpr int kCompactGapPx = 5;  // Small uniform gap between button blocks
        
        syncBlockX = buttonAreaX;
        autoBlockX = syncBlockX + syncBlockW + kCompactGapPx;
        midiBlockX = autoBlockX + autoBlockW + kCompactGapPx;
        
        // Ensure we don't exceed available width (compress if absolutely necessary)
        const int totalNeededW = syncBlockW + kCompactGapPx + autoBlockW + kCompactGapPx + midiBlockW;
        if (totalNeededW > buttonAreaW)
        {
            // Extreme compression: calculate minimum gaps
            const int remainingSpace = buttonAreaW - (syncBlockW + autoBlockW + midiBlockW);
            const int minGap = juce::jmax (1, remainingSpace / 2);
            autoBlockX = syncBlockX + syncBlockW + minGap;
            midiBlockX = autoBlockX + autoBlockW + minGap;
        }
    }
    else
    {
        // JUSTIFIED LAYOUT: When space is available, use justified distribution
        // SYNC block aligns with bars (left edge)        // MIDI block aligns with value legends (right edge)
        // AUTO centered between them
        syncBlockX = buttonAreaX;
        const int midiBlockRightX = valueAreaX + valueAreaW;  // Right edge of value legends
        midiBlockX = midiBlockRightX - midiBlockW;
        
        // Calculate available space between SYNC and MIDI for AUTO
        const int spaceAfterSync = midiBlockX - (syncBlockX + syncBlockW);
        
        // Center AUTO in the available space with equal gaps on both sides
        autoBlockX = syncBlockX + syncBlockW + (spaceAfterSync - autoBlockW) / 2;
        
        // Ensure minimum gaps are respected
        const int minAutoX = syncBlockX + syncBlockW + kMinToggleBlocksGapPx;
        const int maxAutoX = midiBlockX - autoBlockW - kMinToggleBlocksGapPx;
        
        if (minAutoX <= maxAutoX)
            autoBlockX = juce::jlimit (minAutoX, maxAutoX, autoBlockX);
        else
            autoBlockX = minAutoX;  // Fallback to minimum spacing
    }

    syncButton.setBounds   (syncBlockX, verticalLayout.btnY, toggleHitW, verticalLayout.box);
    autoFbkButton.setBounds (autoBlockX, verticalLayout.btnY, toggleHitW, verticalLayout.box);
    midiButton.setBounds   (midiBlockX, verticalLayout.btnY, toggleHitW, verticalLayout.box);
    
    // Position MIDI port display to the right of MIDI label (using midiPortSide from above)
    const int midiPortX = midiBlockX + midiLabelBlockW + kMidiPortGapPx;
    const int midiPortY = verticalLayout.btnY + (verticalLayout.box - midiPortSide) / 2;
    midiPortDisplay.setBounds (midiPortX, midiPortY, midiPortSide, midiPortSide);
    
    // Hide MIDI port if it would overflow the right edge (adaptive visibility)
    constexpr int kMidiPortRightMargin = 6;  // Minimum margin from right edge
    const bool midiPortFits = (midiPortX + midiPortSide + kMidiPortRightMargin) <= W;
    midiPortDisplay.setVisible (midiPortFits);

    if (resizerCorner != nullptr)
        resizerCorner->setBounds (W - kResizerCornerPx, H - kResizerCornerPx, kResizerCornerPx, kResizerCornerPx);

    promptOverlay.setBounds (getLocalBounds());
    if (promptOverlayActive)
        promptOverlay.toFront (false);

    updateInfoIconCache();

    // Update legend visibility globally: if ANY slider cannot fit its labels, ALL are disabled
    updateLegendVisibility();

    // Don't modify the constrainer here to avoid reentrancy issues.
}

