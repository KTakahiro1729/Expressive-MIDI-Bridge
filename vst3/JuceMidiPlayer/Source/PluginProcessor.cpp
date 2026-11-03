#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MidiScheduler.h"
#include <algorithm>
#include <atomic>
#include <cmath>

ExpressiveMidiPlayerProcessor::ExpressiveMidiPlayerProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    std::atomic_store(&clip, std::make_shared<const Clip>());
}

void ExpressiveMidiPlayerProcessor::prepareToPlay(double, int)
{
    wasPlaying = false;
    nextExpectedPpq = -1.0;
    lastClipRevision = clipRevision.load();
    previousMode = -1;
    previousHandMapping = -1;
    previousSplitNote = -1;
    wasLivePlaying = false;
    expectedLivePpq = -1;
    liveHumanizer.reset();
}

bool ExpressiveMidiPlayerProcessor::loadMidi(const juce::File& file, juce::String& error)
{
    if (!file.existsAsFile()) { error = "File does not exist"; return false; }
    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes)) { error = "Could not open MIDI"; return false; }
    if (!loadMidiBytes(bytes.getData(), bytes.getSize(), error)) return false;
    lastLoadedPath = file.getFullPathName();
    lastLoadedName = file.getFileName();
    return true;
}

bool ExpressiveMidiPlayerProcessor::reloadLastMidi(juce::String& error)
{
    if (lastLoadedPath.isEmpty())
    {
        error = "No MIDI file path available";
        return false;
    }
    return loadMidi(juce::File(lastLoadedPath), error);
}

bool ExpressiveMidiPlayerProcessor::loadMidiBytes(const void* bytes, size_t size, juce::String& error)
{
    if (!bytes || size == 0 || size > 32u * 1024u * 1024u)
    {
        error = "MIDI must be between 1 byte and 32 MB";
        return false;
    }
    juce::MemoryInputStream stream(bytes, size, false);
    juce::MidiFile midi;
    if (!midi.readFrom(stream)) { error = "Not a valid standard MIDI file"; return false; }
    const int ticksPerBeat = midi.getTimeFormat();
    if (ticksPerBeat <= 0) { error = "SMPTE timecode MIDI is not supported"; return false; }
    auto fresh = std::make_shared<Clip>();
    for (int track = 0; track < midi.getNumTracks(); ++track)
    {
        if (const auto* seq = midi.getTrack(track))
        {
            for (int i = 0; i < seq->getNumEvents(); ++i)
            {
                auto msg = seq->getEventPointer(i)->message;
                if (!msg.isNoteOnOrOff() && !msg.isController() &&
                    !msg.isPitchWheel() && !msg.isChannelPressure() &&
                    !msg.isAftertouch() && !msg.isProgramChange())
                    continue;
                fresh->events.push_back({ msg.getTimeStamp() / ticksPerBeat, msg });
            }
        }
    }
    std::stable_sort(fresh->events.begin(), fresh->events.end(),
        [](const ScheduledEvent& a, const ScheduledEvent& b)
        {
            if (a.ppq != b.ppq) return a.ppq < b.ppq;
            return a.message.isNoteOff() && !b.message.isNoteOff();
        });
    embeddedMidi = juce::MemoryBlock(bytes, size);
    std::atomic_store(&clip, std::shared_ptr<const Clip>(std::move(fresh)));
    clipRevision.fetch_add(1);
    error.clear();
    return true;
}

void ExpressiveMidiPlayerProcessor::processBlock(juce::AudioBuffer<float>& audio,
                                                 juce::MidiBuffer& messages)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    const int selectedMode = mode.load();
    auto* transport = getPlayHead();
    const auto position = transport ? transport->getPosition() : decltype(transport->getPosition()){};
    const bool playing = position && position->getIsPlaying();
    const double bpm = position && position->getBpm() ? *position->getBpm() : 120.0;
    const double ppq = position && position->getPpqPosition() ? *position->getPpqPosition() : 0.0;
    const bool validClock = position && position->getPpqPosition() && position->getBpm()
                            && std::isfinite(ppq) && std::isfinite(bpm) && bpm > 0;
    const auto samples = audio.getNumSamples();
    const auto rate = getSampleRate();
    if (selectedMode != previousMode)
    {
        liveHumanizer.reset();
        if (previousMode >= 0)
            for (int channel = 1; channel <= 16; ++channel)
                messages.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
        wasPlaying = false;
        wasLivePlaying = false;
        previousMode = selectedMode;
    }
    if (selectedMode == 0)
    {
        const int currentMapping = handMapping.load();
        const int currentSplit = splitNote.load();
        const bool assignmentChanged = (previousHandMapping >= 0 && currentMapping != previousHandMapping)
            || (currentMapping == 0 && previousSplitNote >= 0 && currentSplit != previousSplitNote);
        previousHandMapping = currentMapping;
        previousSplitNote = currentSplit;
        const bool jumped = playing && wasLivePlaying && validClock
                            && std::abs(ppq - expectedLivePpq) > 0.05;
        if ((wasLivePlaying && !playing) || jumped || (wasLivePlaying && assignmentChanged))
        {
            liveHumanizer.reset();
            for (int channel = 1; channel <= 16; ++channel)
            {
                messages.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
                messages.addEvent(juce::MidiMessage::controllerEvent(channel, 64, 0), 0);
            }
        }
        juce::MidiBuffer incoming;
        incoming.swapWith(messages);
        for (const auto metadata : incoming)
        {
            const auto msg = metadata.getMessage();
            const auto at = metadata.samplePosition;
            const bool on = msg.isNoteOn();
            if (on || msg.isNoteOff())
            {
                const auto assignment = expressive::classifyHand(msg.getChannel(), msg.getNoteNumber(),
                    handMapping.load() == 1 ? expressive::HandMapping::midiChannels
                                            : expressive::HandMapping::pitchSplit,
                    splitNote.load());
                if (assignment == expressive::Hand::unchanged)
                {
                    messages.addEvent(msg, at);
                    continue;
                }
                const auto& settings = hands[assignment == expressive::Hand::left ? 1 : 0];
                const bool queued = liveHumanizer.add(at, msg.getChannel(), msg.getNoteNumber(),
                    on ? static_cast<int>(msg.getVelocity()) : 0, on,
                    settings.strength.load(), settings.timingMs.load(),
                    settings.velocityRange.load(), rate);
                if (!queued) messages.addEvent(msg, at); // full queue: preserve original note
            }
            else messages.addEvent(msg, at); // CC/pedals and other messages are unchanged
        }
        liveHumanizer.emitBlock(samples, [&messages](const expressive::LiveNote& note, int at)
        {
            if (note.on)
                messages.addEvent(juce::MidiMessage::noteOn(note.channel, note.pitch,
                    static_cast<juce::uint8>(note.velocity)), at);
            else
                messages.addEvent(juce::MidiMessage::noteOff(note.channel, note.pitch), at);
        });
        wasLivePlaying = playing;
        expectedLivePpq = validClock && rate > 0
            ? ppq + samples / rate * bpm / 60.0 : -1.0;
        return;
    }
    const auto current = std::atomic_load(&clip);
    if (!current || current->events.empty())
        return; // When no clip loaded, MIDI input passes through unchanged.

    messages.clear(); // Loaded clip replaces input stream.
    auto* playHead = getPlayHead();
    if (!playHead)
        return;
    const auto positionFile = playHead->getPosition();
    if (!positionFile || !positionFile->getPpqPosition() || !positionFile->getBpm())
        return;
    const auto filePpq = *positionFile->getPpqPosition();
    const auto fileBpm = *positionFile->getBpm();
    const auto filePlaying = positionFile->getIsPlaying();
    if (!std::isfinite(fileBpm) || fileBpm <= 0.0 || !std::isfinite(filePpq))
        return;
    const double sr = getSampleRate();
    if (sr <= 0.0 || samples <= 0)
        return;

    const double offsetBeats = startBeats.load();
    const double clipPpq = filePpq - offsetBeats;
    const auto generation = clipRevision.load();
    const bool jumped = wasPlaying && std::abs(filePpq - nextExpectedPpq) > 0.05;
    if (!filePlaying || jumped || generation != lastClipRevision)
    {
        if (wasPlaying)
            for (int channel = 1; channel <= 16; ++channel)
            {
                messages.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
                messages.addEvent(juce::MidiMessage::controllerEvent(channel, 64, 0), 0);
            }
        lastClipRevision = generation;
        if (!filePlaying)
        {
            wasPlaying = false;
            nextExpectedPpq = -1.0;
            return;
        }
    }
    const double ppqLength = (samples / sr) * (fileBpm / 60.0);
    const double endPpq = filePpq + ppqLength;
    expressive::scheduleBlock(current->events, clipPpq, fileBpm, sr, samples,
        [&messages](const ScheduledEvent& event, int offset)
        {
            messages.addEvent(event.message, offset);
        });
    nextExpectedPpq = endPpq;
    wasPlaying = true;
}

void ExpressiveMidiPlayerProcessor::getStateInformation(juce::MemoryBlock& data)
{
    juce::XmlElement state("ExpressiveMidiPlayer");
    state.setAttribute("path", lastLoadedPath);
    state.setAttribute("name", lastLoadedName);
    state.setAttribute("startBeats", startBeats.load());
    state.setAttribute("mode", mode.load());
    state.setAttribute("handMapping", handMapping.load());
    state.setAttribute("splitNote", splitNote.load());
    state.setAttribute("rhStrength", hands[0].strength.load());
    state.setAttribute("rhTimingMs", hands[0].timingMs.load());
    state.setAttribute("rhVelocityRange", hands[0].velocityRange.load());
    state.setAttribute("lhStrength", hands[1].strength.load());
    state.setAttribute("lhTimingMs", hands[1].timingMs.load());
    state.setAttribute("lhVelocityRange", hands[1].velocityRange.load());
    if (embeddedMidi.getSize() != 0)
        state.setAttribute("midiBase64", embeddedMidi.toBase64Encoding());
    copyXmlToBinary(state, data);
}

void ExpressiveMidiPlayerProcessor::setStateInformation(const void* data, int size)
{
    if (const auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName("ExpressiveMidiPlayer"))
        {
            startBeats.store(xml->getDoubleAttribute("startBeats", 0.0));
            const auto encoded = xml->getStringAttribute("midiBase64");
            mode.store(xml->getIntAttribute("mode", encoded.isNotEmpty() ? 1 : 0));
            handMapping.store(xml->getIntAttribute("handMapping", 0) == 1 ? 1 : 0);
            splitNote.store(juce::jlimit(21, 108, xml->getIntAttribute("splitNote", 60)));
            // Backward compatibility: v1.1's shared settings initialize both hands.
            for (int h = 0; h < 2; ++h)
            {
                const bool right = h == 0;
                const auto prefix = right ? "rh" : "lh";
                hands[h].strength.store(juce::jlimit(0.0, 1.0,
                    xml->getDoubleAttribute(juce::String(prefix) + "Strength",
                                            xml->getDoubleAttribute("strength", 0.65))));
                hands[h].timingMs.store(juce::jlimit(0.0, 40.0,
                    xml->getDoubleAttribute(juce::String(prefix) + "TimingMs",
                                            xml->getDoubleAttribute("timingMs", 16.0))));
                hands[h].velocityRange.store(juce::jlimit(0, 32,
                    xml->getIntAttribute(juce::String(prefix) + "VelocityRange",
                                         xml->getIntAttribute("velocityRange", 12))));
            }
            juce::String error;
            if (encoded.isNotEmpty())
            {
                juce::MemoryBlock bytes;
                if (bytes.fromBase64Encoding(encoded) &&
                    loadMidiBytes(bytes.getData(), bytes.getSize(), error))
                {
                    lastLoadedName = xml->getStringAttribute("name", "Embedded MIDI");
                    lastLoadedPath = xml->getStringAttribute("path");
                    return;
                }
            }
            const auto file = juce::File(xml->getStringAttribute("path"));
            if (file.existsAsFile()) loadMidi(file, error);
        }
}

juce::AudioProcessorEditor* ExpressiveMidiPlayerProcessor::createEditor()
{
    return new ExpressiveMidiPlayerEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ExpressiveMidiPlayerProcessor();
}
