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
    const auto current = std::atomic_load(&clip);
    if (!current || current->events.empty())
        return; // When no clip loaded, MIDI input passes through unchanged.

    messages.clear(); // Loaded clip replaces input stream.
    auto* playHead = getPlayHead();
    if (!playHead)
        return;
    const auto position = playHead->getPosition();
    if (!position || !position->getPpqPosition() || !position->getBpm())
        return;
    const auto ppq = *position->getPpqPosition();
    const auto bpm = *position->getBpm();
    const auto playing = position->getIsPlaying();
    if (!std::isfinite(bpm) || bpm <= 0.0 || !std::isfinite(ppq))
        return;
    const int samples = audio.getNumSamples();
    const double sr = getSampleRate();
    if (sr <= 0.0 || samples <= 0)
        return;

    const double offsetBeats = startBeats.load();
    const double clipPpq = ppq - offsetBeats;
    const auto generation = clipRevision.load();
    const bool jumped = wasPlaying && std::abs(ppq - nextExpectedPpq) > 0.05;
    if (!playing || jumped || generation != lastClipRevision)
    {
        if (wasPlaying)
            for (int channel = 1; channel <= 16; ++channel)
            {
                messages.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
                messages.addEvent(juce::MidiMessage::controllerEvent(channel, 64, 0), 0);
            }
        lastClipRevision = generation;
        if (!playing)
        {
            wasPlaying = false;
            nextExpectedPpq = -1.0;
            return;
        }
    }
    const double ppqLength = (samples / sr) * (bpm / 60.0);
    const double endPpq = ppq + ppqLength;
    expressive::scheduleBlock(current->events, clipPpq, bpm, sr, samples,
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
