#include "ClipSession.h"

namespace expressive
{
namespace
{
constexpr int exportTicksPerQuarter = 960;

juce::String styleName(Style s)
{
    switch (s)
    {
        case Style::pop: return "pop";
        case Style::jazz: return "jazz";
        case Style::classical: default: return "classical";
    }
}

juce::String pedalArg(ClipSession::AiPedal p)
{
    switch (p)
    {
        case ClipSession::AiPedal::score: return "score";
        case ClipSession::AiPedal::none: return "none";
        case ClipSession::AiPedal::model: default: return "model";
    }
}

// Statistics against the source, pairing notes per pitch in time order (the same alignment
// the Python AI blender uses, so it also works for AI output whose note order differs).
RenderStats compareClips(const Clip& src, const Clip& out, double bpm)
{
    RenderStats st;
    st.notes = static_cast<int>(out.notes.size());
    const double msPerBeat = 60000.0 / (bpm > 0 ? bpm : 120.0);
    auto sortedIndices = [](const Clip& c)
    {
        std::vector<size_t> idx(c.notes.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
        std::stable_sort(idx.begin(), idx.end(), [&c](size_t a, size_t b)
        {
            return std::tie(c.notes[a].channel, c.notes[a].pitch, c.notes[a].start)
                 < std::tie(c.notes[b].channel, c.notes[b].pitch, c.notes[b].start);
        });
        return idx;
    };
    if (src.notes.size() != out.notes.size()) return st;
    const auto a = sortedIndices(src), b = sortedIndices(out);
    double shift = 0.0, vel = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const auto& s = src.notes[a[i]];
        const auto& o = out.notes[b[i]];
        const double d = std::abs(o.start - s.start) * msPerBeat;
        shift += d;
        st.maxAbsShiftMs = std::max(st.maxAbsShiftMs, d);
        vel += std::abs(o.velocity - s.velocity);
    }
    if (!a.empty())
    {
        st.meanAbsShiftMs = shift / static_cast<double>(a.size());
        st.meanAbsVelocityChange = vel / static_cast<double>(a.size());
    }
    return st;
}

Clip shifted(Clip c, double beats)
{
    for (auto& n : c.notes) { n.start += beats; n.end += beats; }
    for (auto& e : c.events) e.beat += beats;
    return c;
}
} // namespace

bool clipFromMidiBytes(const void* data, size_t size, Clip& clip, double& fileBpm, juce::String& error)
{
    if (data == nullptr || size == 0) { error = "read"; return false; }
    if (size > 32u * 1024u * 1024u) { error = "toolarge"; return false; }
    juce::MemoryInputStream stream(data, size, false);
    juce::MidiFile midi;
    if (!midi.readFrom(stream)) { error = "invalid"; return false; }
    const int tpq = midi.getTimeFormat();
    if (tpq <= 0) { error = "smpte"; return false; }

    Clip fresh;
    NotePairer pairer;
    double last = 0.0;
    bool tempoFound = false, signatureFound = false;
    fileBpm = 0.0;
    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        const auto* seq = midi.getTrack(t);
        if (seq == nullptr) continue;
        for (int i = 0; i < seq->getNumEvents(); ++i)
        {
            const auto& msg = seq->getEventPointer(i)->message;
            const double beat = msg.getTimeStamp() / tpq;
            last = std::max(last, beat);
            if (msg.isTempoMetaEvent())
            {
                if (!tempoFound && msg.getTempoSecondsPerQuarterNote() > 0.0)
                {
                    fileBpm = 60.0 / msg.getTempoSecondsPerQuarterNote();
                    tempoFound = true;
                }
            }
            else if (msg.isTimeSignatureMetaEvent())
            {
                if (!signatureFound)
                {
                    int num = 4, den = 4;
                    msg.getTimeSignatureInfo(num, den);
                    if (num > 0 && den > 0)
                    {
                        fresh.timeSigNumerator = num;
                        fresh.timeSigDenominator = den;
                        fresh.beatsPerBar = num * 4.0 / den;
                    }
                    signatureFound = true;
                }
            }
            else if (msg.isNoteOn())
                pairer.noteOn(msg.getChannel(), msg.getNoteNumber(), msg.getVelocity(), beat);
            else if (msg.isNoteOff())
                pairer.noteOff(msg.getChannel(), msg.getNoteNumber(), beat);
            else if (msg.isController() || msg.isPitchWheel() || msg.isProgramChange()
                     || msg.isChannelPressure() || msg.isAftertouch())
            {
                ClipEvent e;
                e.beat = beat;
                e.size = std::min(3, msg.getRawDataSize());
                for (int k = 0; k < e.size; ++k) e.bytes[k] = msg.getRawData()[k];
                fresh.events.push_back(e);
            }
        }
    }
    fresh.notes = pairer.finish(last);
    if (fresh.notes.empty()) { error = "empty"; return false; }
    std::stable_sort(fresh.events.begin(), fresh.events.end(),
                     [](const ClipEvent& a, const ClipEvent& b) { return a.beat < b.beat; });
    fresh.originBeat = 0.0;
    clip = std::move(fresh);
    error.clear();
    return true;
}

juce::MemoryBlock clipToMidiBytes(const Clip& clip, double bpm, double originBeat, const juce::String& trackName)
{
    juce::MidiMessageSequence seq;
    const auto toTicks = [originBeat](double beat)
    { return std::max(0.0, (beat - originBeat) * exportTicksPerQuarter); };

    if (trackName.isNotEmpty())
    {
        auto name = juce::MidiMessage::textMetaEvent(3, trackName);
        name.setTimeStamp(0);
        seq.addEvent(name);
    }
    auto tempo = juce::MidiMessage::tempoMetaEvent(juce::roundToInt(60000000.0 / (bpm > 0 ? bpm : 120.0)));
    tempo.setTimeStamp(0);
    seq.addEvent(tempo);
    auto sig = juce::MidiMessage::timeSignatureMetaEvent(clip.timeSigNumerator, clip.timeSigDenominator);
    sig.setTimeStamp(0);
    seq.addEvent(sig);

    for (const auto& e : clip.events)
        if (e.size > 0)
            seq.addEvent(juce::MidiMessage(e.bytes, e.size, toTicks(e.beat)));
    for (const auto& n : clip.notes)
    {
        seq.addEvent(juce::MidiMessage::noteOn(n.channel, n.pitch, static_cast<juce::uint8>(n.velocity)),
                     toTicks(n.start));
        seq.addEvent(juce::MidiMessage::noteOff(n.channel, n.pitch), toTicks(n.end));
    }
    seq.sort();
    seq.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote(exportTicksPerQuarter);
    file.addTrack(seq);
    juce::MemoryOutputStream out;
    file.writeTo(out, 1);
    return out.getMemoryBlock();
}

// ---------------------------------------------------------------- AI settings

static juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "ExpressiveMidiBridge";
    o.filenameSuffix = ".settings";
    o.folderName = "ExpressiveMidiBridge";
    o.osxLibrarySubFolder = "Application Support";
    return o;
}

juce::File AiSettings::resolvedPython() const
{
    if (pythonPath.isNotEmpty()) return juce::File(pythonPath);
    if (repoFolder.isEmpty()) return {};
    const juce::File repo(repoFolder);
   #if JUCE_WINDOWS
    return repo.getChildFile(".venv").getChildFile("Scripts").getChildFile("python.exe");
   #else
    return repo.getChildFile(".venv").getChildFile("bin").getChildFile("python");
   #endif
}

AiSettings AiSettings::load()
{
    AiSettings s;
    juce::PropertiesFile file(settingsOptions());
    s.repoFolder = file.getValue("aiRepo");
    s.pythonPath = file.getValue("aiPython");
    s.device = file.getValue("aiDevice", "cpu");
    return s;
}

void AiSettings::save() const
{
    juce::PropertiesFile file(settingsOptions());
    file.setValue("aiRepo", repoFolder);
    file.setValue("aiPython", pythonPath);
    file.setValue("aiDevice", device);
    file.saveIfNeeded();
}

// ---------------------------------------------------------------- external job

ExternalJob::ExternalJob(juce::StringArray args, juce::File, juce::File expectedOutput)
    : juce::Thread("Expressive MIDI external job"), arguments(std::move(args)), expected(std::move(expectedOutput))
{
}

ExternalJob::~ExternalJob()
{
    signalThreadShouldExit();
    stopThread(10000);
}

void ExternalJob::run()
{
    juce::ChildProcess process;
    if (!process.start(arguments, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        output = "start-failed";
        finished.store(true);
        return;
    }
    char buffer[4096];
    const auto deadline = juce::Time::getMillisecondCounter() + 60u * 60u * 1000u; // 1 hour
    while (process.isRunning())
    {
        if (threadShouldExit() || juce::Time::getMillisecondCounter() > deadline)
        {
            process.kill();
            output << "\n[cancelled]";
            finished.store(true);
            return;
        }
        const int n = process.readProcessOutput(buffer, sizeof(buffer));
        if (n > 0) output << juce::String::fromUTF8(buffer, n);
        else wait(50);
    }
    for (;;)
    {
        const int n = process.readProcessOutput(buffer, sizeof(buffer));
        if (n <= 0) break;
        output << juce::String::fromUTF8(buffer, n);
    }
    ok = process.getExitCode() == 0 && (expected == juce::File() || expected.existsAsFile());
    finished.store(true);
}

// ---------------------------------------------------------------- session

bool ClipSession::loadFile(const juce::File& file, double hostBpm, juce::String& error)
{
    juce::MemoryBlock bytes;
    if (!file.existsAsFile() || !file.loadFileAsData(bytes)) { error = "read"; return false; }
    Clip clip;
    double fileBpm = 0.0;
    if (!clipFromMidiBytes(bytes.getData(), bytes.getSize(), clip, fileBpm, error)) return false;
    source = std::move(clip);
    sourceKind = SourceKind::file;
    sourceName = file.getFileNameWithoutExtension();
    bpm = hostBpm > 0 ? hostBpm : (fileBpm > 0 ? fileBpm : 120.0);
    sourceChanged();
    return true;
}

void ClipSession::setCaptured(Clip clip, double captureBpm)
{
    source = std::move(clip);
    sourceKind = SourceKind::captured;
    sourceName = "Captured";
    bpm = captureBpm > 0 ? captureBpm : 120.0;
    sourceChanged();
}

void ClipSession::clearSource()
{
    cancelJob();
    source = {};
    ruleResult = {};
    aiResult = {};
    sourceKind = SourceKind::none;
    sourceName.clear();
    aiValid = false;
    aiResultStale = false;
    aiStatus.clear();
    sendChangeMessage();
}

void ClipSession::sourceChanged()
{
    if (isAiRunning()) cancelJob();
    aiValid = false;
    aiResult = {};
    aiResultStale = false;
    aiStatus.clear();
    render();
}

void ClipSession::setEngine(Engine e)
{
    if (e == engine) return;
    engine = e;
    sendChangeMessage();
}

void ClipSession::setRuleSettings(RenderSettings s)
{
    rule = s;
    render();
}

void ClipSession::reroll()
{
    rule.seed = rule.seed * 1103515245u + 12345u;
    render();
}

void ClipSession::setAiStrength(double v)
{
    aiStrength = juce::jlimit(0.0, 1.0, v);
    if (aiValid) aiResultStale = true;
    sendChangeMessage();
}

void ClipSession::setAiPedal(AiPedal p)
{
    aiPedal = p;
    if (aiValid) aiResultStale = true;
    sendChangeMessage();
}

void ClipSession::render()
{
    if (!source.empty())
        ruleResult = renderRuleBased(source, rule, bpm, &ruleStats);
    sendChangeMessage();
}

bool ClipSession::startAi(juce::String& error)
{
    if (source.empty()) { error = "nosource"; return false; }
    if (job != nullptr) { error = "busy"; return false; }
    const auto python = ai.resolvedPython();
    if (ai.repoFolder.isEmpty() || !juce::File(ai.repoFolder).isDirectory()) { error = "norepo"; return false; }
    if (!python.existsAsFile()) { error = "nopython"; return false; }

    const auto work = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("ExpressiveMidiBridge")
                          .getChildFile("ai-" + juce::String(juce::Time::currentTimeMillis()));
    if (!work.createDirectory()) { error = "tempdir"; return false; }
    const auto in = work.getChildFile("score.mid");
    const auto out = work.getChildFile("performance.mid");
    const auto bytes = clipToMidiBytes(source, bpm, source.originBeat, sourceName);
    if (!in.replaceWithData(bytes.getData(), bytes.getSize())) { error = "tempdir"; return false; }

    juce::StringArray args { python.getFullPathName(), "-m", "expressive_midi", "render",
                             in.getFullPathName(), out.getFullPathName(),
                             "--backend", "pianist-transformer",
                             "--repo", ai.repoFolder,
                             "--model-python", python.getFullPathName(),
                             "--daw-bpm", juce::String(bpm, 4),
                             "--ai-strength", juce::String(aiStrength, 3),
                             "--pedal", pedalArg(aiPedal),
                             "--device", ai.device };
    job = std::make_unique<ExternalJob>(args, juce::File(ai.repoFolder), out);
    jobIsCheck = false;
    aiStatus = "running";
    aiStatusError = false;
    job->start();
    sendChangeMessage();
    return true;
}

bool ClipSession::startAiCheck(juce::String& error)
{
    if (job != nullptr) { error = "busy"; return false; }
    const auto python = ai.resolvedPython();
    if (ai.repoFolder.isEmpty() || !juce::File(ai.repoFolder).isDirectory()) { error = "norepo"; return false; }
    if (!python.existsAsFile()) { error = "nopython"; return false; }
    job = std::make_unique<ExternalJob>(juce::StringArray { python.getFullPathName(), "-m", "expressive_midi",
                                                            "doctor", "--repo", ai.repoFolder,
                                                            "--model-python", python.getFullPathName() },
                                        juce::File(ai.repoFolder), juce::File());
    jobIsCheck = true;
    aiStatus = "checking";
    aiStatusError = false;
    job->start();
    sendChangeMessage();
    return true;
}

void ClipSession::cancelJob()
{
    if (job == nullptr) return;
    job.reset(); // destructor kills the child process
    aiStatus = "cancelled";
    aiStatusError = false;
    sendChangeMessage();
}

void ClipSession::poll()
{
    if (job == nullptr || !job->isFinished()) return;
    const auto finishedJob = std::move(job);
    const auto& log = finishedJob->log();
    if (jobIsCheck)
    {
        // `doctor` prints JSON and exits 0 only if every check passed.
        aiStatusError = !finishedJob->succeeded();
        aiStatus = aiStatusError ? "check-failed\n" + log.trim() : "check-ok";
        if (log.contains("No module named expressive_midi"))
            aiStatus = "no-module";
    }
    else if (finishedJob->succeeded())
    {
        juce::MemoryBlock bytes;
        Clip clip;
        double fileBpm = 0.0;
        juce::String error;
        if (finishedJob->outputFile().loadFileAsData(bytes)
            && clipFromMidiBytes(bytes.getData(), bytes.getSize(), clip, fileBpm, error))
        {
            clip = shifted(std::move(clip), source.originBeat);
            clip.originBeat = source.originBeat;
            clip.beatsPerBar = source.beatsPerBar;
            clip.timeSigNumerator = source.timeSigNumerator;
            clip.timeSigDenominator = source.timeSigDenominator;
            aiResult = std::move(clip);
            aiStats = compareClips(source, aiResult, bpm);
            aiValid = true;
            aiResultStale = false;
            aiStatus = "done";
            aiStatusError = false;
        }
        else
        {
            aiStatus = "failed\n" + error;
            aiStatusError = true;
        }
    }
    else
    {
        aiStatusError = true;
        aiStatus = log.contains("No module named expressive_midi") ? juce::String("no-module")
                 : log == "start-failed" ? juce::String("start-failed")
                 : "failed\n" + log.trim().getLastCharacters(1500);
    }
    sendChangeMessage();
}

juce::File ClipSession::exportFolder() const
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Expressive MIDI Bridge");
}

juce::String ClipSession::suggestedFileName() const
{
    auto base = juce::File::createLegalFileName(sourceName.isEmpty() ? juce::String("clip") : sourceName);
    if (engine == Engine::ai)
        base << "_ai" << juce::roundToInt(aiStrength * 100);
    else
        base << "_" << styleName(rule.style) << juce::roundToInt(rule.strength * 100);
    return base + ".mid";
}

juce::File ClipSession::exportResult(juce::String& error) const
{
    if (!hasResult()) { error = "noresult"; return {}; }
    const auto& result = getResult();
    const auto folder = exportFolder();
    if (!folder.createDirectory()) { error = "exportdir"; return {}; }
    const auto file = folder.getChildFile(suggestedFileName());
    const auto bytes = clipToMidiBytes(result, bpm, result.originBeat, sourceName + " (expressive)");
    if (!file.replaceWithData(bytes.getData(), bytes.getSize())) { error = "exportdir"; return {}; }
    return file;
}

void ClipSession::saveState(juce::XmlElement& parent) const
{
    auto* x = parent.createNewChildElement("Clip");
    x->setAttribute("engine", static_cast<int>(engine));
    x->setAttribute("strength", rule.strength);
    x->setAttribute("style", static_cast<int>(rule.style));
    x->setAttribute("seed", static_cast<int>(rule.seed));
    x->setAttribute("aiStrength", aiStrength);
    x->setAttribute("aiPedal", static_cast<int>(aiPedal));
    if (source.empty()) return;
    x->setAttribute("kind", static_cast<int>(sourceKind));
    x->setAttribute("name", sourceName);
    x->setAttribute("bpm", bpm);
    x->setAttribute("origin", source.originBeat);
    // Positions are stored absolute (origin 0) so that captured clips keep their song position.
    x->setAttribute("source", clipToMidiBytes(source, bpm, 0.0, {}).toBase64Encoding());
    if (aiValid)
    {
        x->setAttribute("aiResult", clipToMidiBytes(aiResult, bpm, 0.0, {}).toBase64Encoding());
        x->setAttribute("aiStale", aiResultStale);
    }
}

void ClipSession::loadState(const juce::XmlElement& parent)
{
    const auto* x = parent.getChildByName("Clip");
    if (x == nullptr) return;
    engine = x->getIntAttribute("engine") == 1 ? Engine::ai : Engine::ruleBased;
    rule.strength = juce::jlimit(0.0, 1.0, x->getDoubleAttribute("strength", 0.65));
    rule.style = static_cast<Style>(juce::jlimit(0, 2, x->getIntAttribute("style", 0)));
    rule.seed = static_cast<std::uint32_t>(x->getIntAttribute("seed", 23));
    aiStrength = juce::jlimit(0.0, 1.0, x->getDoubleAttribute("aiStrength", 1.0));
    aiPedal = static_cast<AiPedal>(juce::jlimit(0, 2, x->getIntAttribute("aiPedal", 0)));

    const auto load = [this](const juce::String& base64, Clip& clip)
    {
        juce::MemoryBlock bytes;
        double fileBpm = 0.0;
        juce::String error;
        return base64.isNotEmpty() && bytes.fromBase64Encoding(base64)
            && clipFromMidiBytes(bytes.getData(), bytes.getSize(), clip, fileBpm, error);
    };
    Clip clip;
    if (!load(x->getStringAttribute("source"), clip)) { render(); return; }
    clip.originBeat = x->getDoubleAttribute("origin", 0.0);
    source = std::move(clip);
    sourceKind = static_cast<SourceKind>(juce::jlimit(0, 2, x->getIntAttribute("kind", 1)));
    sourceName = x->getStringAttribute("name");
    bpm = x->getDoubleAttribute("bpm", 120.0);
    aiValid = false;
    Clip aiClip;
    if (load(x->getStringAttribute("aiResult"), aiClip))
    {
        aiClip.originBeat = source.originBeat;
        aiResult = std::move(aiClip);
        aiStats = compareClips(source, aiResult, bpm);
        aiValid = true;
        aiResultStale = x->getBoolAttribute("aiStale");
    }
    render();
}
} // namespace expressive
