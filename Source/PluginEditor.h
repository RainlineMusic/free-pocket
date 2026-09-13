/*
    Free Pocket - plugin editor.
    Copyright (c) 2026 Rainline Music. All Rights Reserved.

    Same look as Wide Pocket v0.4 / Phase Pocket v0.8: three themes, the same
    dial and button drawing, the same gear menu, the same power bypass with
    the frozen blurred snapshot.

    New here: the left hand display is a Pro-L2 style peak history. The input
    peak is drawn in one colour, the processed peak on top of it in another,
    so the recovered headroom is visible as the gap between the two.
*/

#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

enum class PocketTheme { Neon, SolidDark, SolidWhite };

class PocketLook final : public juce::LookAndFeel_V4
{
public:
    PocketTheme theme = PocketTheme::Neon;

    bool isDark() const { return theme != PocketTheme::SolidWhite; }
    bool isNeon() const { return theme == PocketTheme::Neon; }

    juce::Colour pick (juce::uint32 neon, juce::uint32 dark, juce::uint32 white) const;
    juce::Colour ink() const;
    juce::Colour muted() const;

    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawLinearSlider (juce::Graphics&, int, int, int, int, float, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
};

/** Rotary dial, identical drawing to v0.8: 200 px design size, 100 px compact. */
class ModernDial final : public juce::Slider
{
public:
    ModernDial (PocketLook&, juce::String title, juce::String subtitle, juce::String unit,
                juce::uint32 accent, bool compact = false, int decimals = 0);

    void paint (juce::Graphics&) override;

private:
    PocketLook& look;
    juce::String title, subtitle, unit;
    juce::uint32 accent;
    bool compact;
    int decimals;
};

class FreePocketAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit FreePocketAudioProcessorEditor (FreePocketAudioProcessor&);
    ~FreePocketAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void setTheme (PocketTheme, bool persist = true);
    void showSettingsMenu();
    void captureBlurSnapshot();
    void saveSize();

    void panel (juce::Graphics&, juce::Rectangle<float>);
    void peakHistory (juce::Graphics&, juce::Rectangle<float>);
    juce::Rectangle<int> scaled (float, float, float, float) const;

    FreePocketAudioProcessor& audioProcessor;
    PocketLook look;

    ModernDial freeDial { look, "Free", "Recovered level", "%", 0xff5987ff };
    ModernDial characterDial { look, "Character", "Rotation range", "%", 0xff32d4cb };
    ModernDial outputDial { look, "Output", "dB", "", 0xfff1e84b, true, 2 };

    juce::TextButton settingsButton { "settings" }, bypassButton { "power" };

    std::unique_ptr<SliderAttachment> freeAttach, characterAttach, outputAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach;

    std::unique_ptr<juce::PropertiesFile> preferences;

    PeakTrace latest {};

    // ~4 s of peak history at 400 frames a second.
    static constexpr int historySize = 1536;
    std::array<float, historySize> dryHistory {};
    std::array<float, historySize> wetHistory {};
    int historyCursor = 0, historyFilled = 0;

    bool ready = false, capturingBlur = false, bypassTarget = false;
    float bypassMix = 0.0f;
    double resizeStamp = 0.0;

    juce::Image blurredSnapshot;
    juce::Rectangle<int> blurArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreePocketAudioProcessorEditor)
};
