/*
    Free Pocket - plugin processor.
    Copyright (c) 2026 Rainline Music. All Rights Reserved.
*/

#pragma once

#include <JuceHeader.h>

#include "dsp/FreePocketEngine.h"

/** One meter frame: the input peak and the output peak of the same moment,
    already latency aligned by the engine, plus the gain that was free. */
struct PeakTrace
{
    float dryPeak = 0.0f;
    float wetPeak = 0.0f;
    float gainDb = 0.0f;
    float headroomDb = 0.0f;
    int curve = 0;
};

class FreePocketAudioProcessor final : public juce::AudioProcessor
{
public:
    FreePocketAudioProcessor();

    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout&) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    juce::AudioProcessorParameter* getBypassParameter() const override { return parameters.getParameter ("bypass"); }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return (double) getLatencySamples() / juce::jmax (1.0, getSampleRate()); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    /** Pops one meter frame. Returns false when the queue is empty. */
    bool popTrace (PeakTrace&);

    juce::AudioProcessorValueTreeState parameters;

    std::atomic<bool> editorOpen { false };
    std::atomic<bool> displayBypass { false };
    std::atomic<int> editorWidth { 0 };
    std::atomic<float> freeGainDb { 0.0f };

private:
    void processAudio (juce::AudioBuffer<float>&, bool hostBypass);
    void pushParameters (bool bypassed);

    fp::FreePocketEngine engine;

    std::atomic<float>* headroom = nullptr;
    std::atomic<float>* character = nullptr;
    std::atomic<float>* outputGain = nullptr;
    std::atomic<float>* bypass = nullptr;

    // Dry path used while bypassed, so switching is click free and the
    // reported latency stays valid either way.
    juce::AudioBuffer<float> bypassDelayBuffer;
    juce::AudioBuffer<float> bypassWarmBuffer;
    int bypassWritePosition = 0;

    static constexpr int fifoSize = 2048;
    juce::AbstractFifo fifo { fifoSize };
    std::array<PeakTrace, (std::size_t) fifoSize> traces {};
    int chunkSize = 128;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreePocketAudioProcessor)
};
