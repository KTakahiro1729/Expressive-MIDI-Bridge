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
    startTimerHz(20);
}

ExpressiveMidiPlayerProcessor::~ExpressiveMidiPlayerProcessor()
{
    stopTimer();
}

void ExpressiveMidiPlayerProcessor::armCapture()
{
    captureState.store(captureIdle);
    captureFifo.reset();
    capturePairer = {};
    captureEvents.clear();
    capturedSoFarBeats = 0.0;
    captureLastBeat = 0.0;
    captureFirstBeat = -1.0;
    capturedNoteCount = 0;
    captureOverflow.store(false);
    captureState.store(captureArmed);
}

void ExpressiveMidiPlayerProcessor::cancelCapture()
{
    captureState.store(captureIdle);
    captureFifo.reset();
    capturePairer = {};
    captureEvents.clear();
    capturedNoteCount = 0;
}

void ExpressiveMidiPlayerProcessor::drainCapture()
{
    const auto scope = captureFifo.read(captureFifo.getNumReady());
    const auto consume = [this](int start, int count)
    {
        for (int i = start; i < start + count; ++i)
        {
            const auto& m = captureBuffer[static_cast<size_t>(i)];
            const auto status = m.bytes[0] & 0xf0;
            const int channel = (m.bytes[0] & 0x0f) + 1;
            captureLastBeat = std::max(captureLastBeat, m.beat);
            if (status == 0x90 && m.bytes[2] > 0)
            {
                capturePairer.noteOn(channel, m.bytes[1], m.bytes[2], m.beat);
                if (captureFirstBeat < 0) captureFirstBeat = m.beat;
                ++capturedNoteCount;
            }
            else if (status == 0x80 || status == 0x90)
                capturePairer.noteOff(channel, m.bytes[1], m.beat);
            else
            {
                expressive::ClipEvent e;
                e.beat = m.beat;
                e.size = m.size;
                std::copy(m.bytes, m.bytes + 3, e.bytes);
                captureEvents.push_back(e);
            }
        }
    };
    consume(scope.startIndex1, scope.blockSize1);
    consume(scope.startIndex2, scope.blockSize2);
    if (captureFirstBeat >= 0) capturedSoFarBeats = captureLastBeat - captureFirstBeat;
}

void ExpressiveMidiPlayerProcessor::finishCapture()
{
    drainCapture();
    captureState.store(captureIdle);
    expressive::Clip clip;
    clip.notes = capturePairer.finish(captureLastBeat);
    capturePairer = {};
    if (clip.notes.empty())
    {
        captureEvents.clear();
        return; // nothing played: stay idle, the editor shows a hint
    }
    clip.events = std::move(captureEvents);
    captureEvents.clear();
    clip.timeSigNumerator = captureSigNum.load();
    clip.timeSigDenominator = captureSigDen.load();
    clip.beatsPerBar = clip.timeSigNumerator * 4.0 / std::max(1, clip.timeSigDenominator);
    clip.originBeat = expressive::barStartBefore(clip.firstBeat(), clip.beatsPerBar, captureBarStart.load());
    clipSession.setCaptured(std::move(clip), captureBpm.load());
}

void ExpressiveMidiPlayerProcessor::timerCallback()
{
    const auto state = captureState.load();
    if (state == captureRecording) drainCapture();
    else if (state == captureStopping) finishCapture();
    clipSession.poll();
}

void ExpressiveMidiPlayerProcessor::prepareToPlay(double, int)
{
    wasPlaying = false;
    nextExpectedPpq = -1.0;
    lastClipRevision = clipRevision.load();
    previousMode = -1;
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
    int notes = 0;
    for (const auto& event : fresh->events)
        if (event.message.isNoteOn()) ++notes;
    clipNoteCount.store(notes);
    clipLengthBeats.store(fresh->events.empty() ? 0.0 : fresh->events.back().ppq);
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
    hostHasClock.store(validClock, std::memory_order_relaxed);
    hostPlaying.store(playing, std::memory_order_relaxed);
    hostBpm.store(validClock ? bpm : 0.0, std::memory_order_relaxed);
    hostPpq.store(validClock ? ppq : 0.0, std::memory_order_relaxed);
    const auto countNotes = [](const juce::MidiBuffer& buffer)
    {
        std::uint32_t n = 0;
        for (const auto metadata : buffer)
            if (metadata.getMessage().isNoteOn()) ++n;
        return n;
    };
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
    if (selectedMode == modeClip)
    {
        // MIDI passes through unchanged; optionally record it for the clip workflow.
        auto state = captureState.load();
        if (state == captureArmed && playing && validClock)
        {
            captureBpm.store(bpm);
            if (const auto sig = position->getTimeSignature())
            {
                captureSigNum.store(std::max(1, sig->numerator));
                captureSigDen.store(std::max(1, sig->denominator));
            }
            const auto barStart = position->getPpqPositionOfLastBarStart();
            captureBarStart.store(barStart ? *barStart : 0.0);
            captureExpectedPpq = ppq;
            captureState.store(state = captureRecording);
        }
        if (state == captureRecording)
        {
            const bool looped = validClock && captureExpectedPpq >= 0 && std::abs(ppq - captureExpectedPpq) > 0.05;
            if (!playing || !validClock || looped)
                captureState.store(captureStopping);
            else
            {
                for (const auto metadata : messages)
                {
                    const auto msg = metadata.getMessage();
                    if (!(msg.isNoteOnOrOff() || msg.isController() || msg.isPitchWheel()
                          || msg.isProgramChange() || msg.isChannelPressure() || msg.isAftertouch()))
                        continue;
                    const auto scope = captureFifo.write(1);
                    if (scope.blockSize1 + scope.blockSize2 == 0) { captureOverflow.store(true); continue; }
                    auto& slot = captureBuffer[static_cast<size_t>(scope.blockSize1 > 0 ? scope.startIndex1
                                                                                       : scope.startIndex2)];
                    slot.beat = ppq + (rate > 0 ? metadata.samplePosition / rate * bpm / 60.0 : 0.0);
                    slot.size = static_cast<std::uint8_t>(std::min(3, msg.getRawDataSize()));
                    for (int k = 0; k < 3; ++k)
                        slot.bytes[k] = k < slot.size ? msg.getRawData()[k] : 0;
                }
                captureExpectedPpq = ppq + (rate > 0 ? samples / rate * bpm / 60.0 : 0.0);
            }
        }
        if (const auto n = countNotes(messages))
        {
            midiInCounter.fetch_add(n, std::memory_order_relaxed);
            midiOutCounter.fetch_add(n, std::memory_order_relaxed);
        }
        return;
    }
    if (selectedMode == modeLive)
    {
        const bool jumped = playing && wasLivePlaying && validClock
                            && std::abs(ppq - expectedLivePpq) > 0.05;
        if ((wasLivePlaying && !playing) || jumped)
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
        if (const auto n = countNotes(incoming))
            midiInCounter.fetch_add(n, std::memory_order_relaxed);
        for (const auto metadata : incoming)
        {
            const auto msg = metadata.getMessage();
            const auto at = metadata.samplePosition;
            const bool on = msg.isNoteOn();
            if (on || msg.isNoteOff())
            {
                const bool queued = liveHumanizer.add(at, msg.getChannel(), msg.getNoteNumber(),
                    on ? static_cast<int>(msg.getVelocity()) : 0, on,
                    strength.load(), timingMs.load(), velocityRange.load(), rate);
                if (!queued) messages.addEvent(msg, at); // queue full: preserve original note
            }
            else messages.addEvent(msg, at); // CC/pedals and all other messages untouched
        }
        liveHumanizer.emitBlock(samples, [&messages](const expressive::LiveNote& note, int at)
        {
            if (note.on)
                messages.addEvent(juce::MidiMessage::noteOn(note.channel, note.pitch,
                    static_cast<juce::uint8>(note.velocity)), at);
            else
                messages.addEvent(juce::MidiMessage::noteOff(note.channel, note.pitch), at);
        });
        if (const auto n = countNotes(messages))
            midiOutCounter.fetch_add(n, std::memory_order_relaxed);
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
    if (const auto n = countNotes(messages))
        midiOutCounter.fetch_add(n, std::memory_order_relaxed);
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
    state.setAttribute("strength", strength.load());
    state.setAttribute("timingMs", timingMs.load());
    state.setAttribute("velocityRange", velocityRange.load());
    state.setAttribute("uiLanguage", uiLanguage.load());
    clipSession.saveState(state);
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
            setMode(xml->getIntAttribute("mode", encoded.isNotEmpty() ? modeFile : modeLive));
            strength.store(juce::jlimit(0.0, 1.0, xml->getDoubleAttribute("strength", 0.65)));
            timingMs.store(juce::jlimit(0.0, 40.0, xml->getDoubleAttribute("timingMs", 16.0)));
            velocityRange.store(juce::jlimit(0, 32, xml->getIntAttribute("velocityRange", 12)));
            setUiLanguage(xml->getIntAttribute("uiLanguage", -1));
            clipSession.loadState(*xml);
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
