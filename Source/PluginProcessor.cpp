/*
    Free Pocket - plugin processor.
    Copyright (c) 2026 Rainline Music. All Rights Reserved.
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

FreePocketAudioProcessor::FreePocketAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", layout())
{
    headroom = parameters.getRawParameterValue ("headroom");
    character = parameters.getRawParameterValue ("character");
    outputGain = parameters.getRawParameterValue ("output");
    bypass = parameters.getRawParameterValue ("bypass");
}

juce::AudioProcessorValueTreeState::ParameterLayout FreePocketAudioProcessor::layout()
{
    using Float = juce::AudioParameterFloat;
    using Bool = juce::AudioParameterBool;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // Free: how much of the peak room recovered by the phase rotation is handed
    // back as level. At 0 % the plugin is a bit exact bypass.
    p.push_back (std::make_unique<Float> (juce::ParameterID { "headroom", 1 }, "Free",
                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f));
    // Character: how far up the spectrum the rotation is allowed to reach.
    p.push_back (std::make_unique<Float> (juce::ParameterID { "character", 1 }, "Character",
                                         juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f));
    p.push_back (std::make_unique<Float> (juce::ParameterID { "output", 1 }, "Output",
                                         juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<Bool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));

    return { p.begin(), p.end() };
}

bool FreePocketAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void FreePocketAudioProcessor::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    engine.prepare (sampleRate, maximumBlockSize);
    pushParameters (bypass->load() > 0.5f);
    setLatencySamples (engine.latencySamples());

    bypassDelayBuffer.setSize (2, juce::jmax (1, engine.latencySamples() + 1), false, true, true);
    bypassDelayBuffer.clear();
    bypassWarmBuffer.setSize (2, juce::jmax (1, maximumBlockSize), false, true, true);
    bypassWarmBuffer.clear();
    bypassWritePosition = 0;

    // One meter frame every ~2.5 ms: dense enough for a smooth scrolling
    // history, cheap enough never to stress the queue.
    chunkSize = juce::jlimit (16, 2048, (int) std::lround (sampleRate / 400.0));
}

void FreePocketAudioProcessor::reset()
{
    engine.reset();
    bypassDelayBuffer.clear();
    bypassWarmBuffer.clear();
    bypassWritePosition = 0;
}

void FreePocketAudioProcessor::pushParameters (bool bypassed)
{
    fp::Parameters p;
    p.headroom = bypassed ? 0.0f : headroom->load();
    p.character = character->load();
    p.outputDb = bypassed ? 0.0f : outputGain->load();
    engine.setParameters (p);
}

void FreePocketAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processAudio (buffer, false);
}

void FreePocketAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processAudio (buffer, true);
}

void FreePocketAudioProcessor::processAudio (juce::AudioBuffer<float>& buffer, bool hostBypass)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numOutputChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numOutputChannels <= 0)
        return;

    const bool bypassed = hostBypass || bypass->load() > 0.5f;
    displayBypass.store (bypassed, std::memory_order_relaxed);

    if (bypassed)
    {
        // Keep the allpass states warm so releasing bypass cannot click.
        if (bypassWarmBuffer.getNumSamples() >= numSamples)
        {
            bypassWarmBuffer.copyFrom (0, 0, buffer, 0, 0, numSamples);
            bypassWarmBuffer.copyFrom (1, 0, buffer, numOutputChannels > 1 ? 1 : 0, 0, numSamples);
            pushParameters (true);
            engine.process (bypassWarmBuffer.getWritePointer (0), bypassWarmBuffer.getWritePointer (1), numSamples);
        }

        const int delayLength = bypassDelayBuffer.getNumSamples();
        const int latency = engine.latencySamples();

        if (delayLength > 0 && latency > 0)
        {
            for (int n = 0; n < numSamples; ++n)
            {
                const int readPosition = (bypassWritePosition + delayLength - latency) % delayLength;

                for (int channel = 0; channel < juce::jmin (2, numOutputChannels); ++channel)
                {
                    const float dry = buffer.getSample (channel, n);
                    buffer.setSample (channel, n, bypassDelayBuffer.getSample (channel, readPosition));
                    bypassDelayBuffer.setSample (channel, bypassWritePosition, dry);
                }

                bypassWritePosition = (bypassWritePosition + 1) % delayLength;
            }
        }

        freeGainDb.store (0.0f);
        return;
    }

    pushParameters (false);

    // Prime the bypass delay with the dry input, never with processed output.
    const int delayLength = bypassDelayBuffer.getNumSamples();
    if (delayLength > 0)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            bypassDelayBuffer.setSample (0, bypassWritePosition, buffer.getSample (0, n));
            bypassDelayBuffer.setSample (1, bypassWritePosition,
                                         buffer.getSample (numOutputChannels > 1 ? 1 : 0, n));
            bypassWritePosition = (bypassWritePosition + 1) % delayLength;
        }
    }

    auto* left = buffer.getWritePointer (0);
    auto* right = numOutputChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    // Processed in chunks so the meter receives an evenly spaced stream of
    // before / after peak pairs instead of a single point per host block.
    for (int offset = 0; offset < numSamples; offset += chunkSize)
    {
        const int count = juce::jmin (chunkSize, numSamples - offset);
        engine.process (left + offset, right != nullptr ? right + offset : nullptr, count);

        fp::EngineFrame frame;
        if (! engine.popFrame (frame))
            continue;

        freeGainDb.store (frame.gainDb);

        if (! editorOpen.load (std::memory_order_relaxed))
            continue;

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToWrite (1, start1, size1, start2, size2);

        if (size1 <= 0)
            continue;

        PeakTrace trace;
        trace.dryPeak = frame.dryPeak;
        trace.wetPeak = frame.wetPeak;
        trace.gainDb = frame.gainDb;
        trace.headroomDb = frame.headroomDb;
        trace.curve = frame.curve;

        traces[(std::size_t) start1] = trace;
        fifo.finishedWrite (1);
    }
}

bool FreePocketAudioProcessor::popTrace (PeakTrace& value)
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead (1, start1, size1, start2, size2);

    if (size1 <= 0)
        return false;

    value = traces[(std::size_t) start1];
    fifo.finishedRead (1);
    return true;
}

void FreePocketAudioProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    state.setProperty ("uiWidth", editorWidth.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void FreePocketAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            editorWidth.store ((int) state.getProperty ("uiWidth", 0));
            parameters.replaceState (state);
        }
    }
}

juce::AudioProcessorEditor* FreePocketAudioProcessor::createEditor()
{
    return new FreePocketAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FreePocketAudioProcessor();
}
