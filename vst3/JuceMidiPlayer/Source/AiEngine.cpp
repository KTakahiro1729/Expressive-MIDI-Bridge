#include "AiEngine.h"
#include "EmbeddedPython.h"

namespace expressive
{
namespace
{
// uv 0.11.32 (2026-07-23), https://github.com/astral-sh/uv/releases — SHA-256 of each archive.
struct ToolArchive { const char* name; const char* sha256; };
ToolArchive toolArchive()
{
   #if JUCE_WINDOWS
    return { "uv-x86_64-pc-windows-msvc.zip", "acfde570451cfdb8689fa159a138ee805ba4e241c466432750302c86254b0984" };
   #elif JUCE_MAC && JUCE_ARM
    return { "uv-aarch64-apple-darwin.tar.gz", "ed336d0ba49db8ef89b2b41fffa372ce63bd032f22a56f001c265891aec32829" };
   #elif JUCE_MAC
    return { "uv-x86_64-apple-darwin.tar.gz", "77f5ca26c0de20e992a3677a174fe1121ee25c36f9b1434a863f75bf077a05eb" };
   #else
    return { "uv-x86_64-unknown-linux-gnu.tar.gz", "aab924fd522efd06f1c5f3b93a243864fc453132c94b2dc49f1371b528a4b967" };
   #endif
}
constexpr const char* toolVersion = "0.11.32";
constexpr const char* runtimeVersion = "1"; // bump to force a reinstall after changing the steps below

juce::String tail(const juce::String& text, int chars = 4000)
{
    return text.length() > chars ? text.getLastCharacters(chars) : text;
}

juce::String lastMeaningfulLine(const juce::String& log)
{
    juce::StringArray lines;
    lines.addLines(log);
    lines.removeEmptyStrings();
    for (int i = lines.size(); --i >= 0;)
        if (lines[i].startsWith("ERROR ")) return lines[i].substring(6).trim();
    for (const auto& line : lines)   // uv: "error: ..." followed by "Caused by: ..." details
        if (line.trimStart().startsWithIgnoreCase("error:")) return line.trim().substring(6).trim();
    for (int i = lines.size(); --i >= 0;)
        if (!lines[i].startsWith("STAGE ") && !lines[i].trim().startsWith("{")) return lines[i].trim();
    return {};
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
    clip = std::move(fresh);
    error.clear();
    return true;
}

// ================================================================= ExternalJob

ExternalJob::ExternalJob(juce::StringArray args)
    : juce::Thread("Expressive MIDI AI job"), arguments(std::move(args))
{
}

ExternalJob::~ExternalJob()
{
    signalThreadShouldExit();
    stopThread(10000);
}

juce::String ExternalJob::log() const
{
    std::lock_guard<std::mutex> g(lock);
    return output;
}

juce::String ExternalJob::errorLine() const
{
    return lastMeaningfulLine(log());
}

void ExternalJob::run()
{
    juce::ChildProcess process;
    if (!process.start(arguments, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        { std::lock_guard<std::mutex> g(lock); output = "ERROR could not start " + arguments[0]; }
        finished.store(true);
        return;
    }
    char buffer[4096];
    for (;;)
    {
        if (threadShouldExit()) { process.kill(); break; }
        const int n = process.readProcessOutput(buffer, sizeof(buffer));
        if (n > 0)
        {
            std::lock_guard<std::mutex> g(lock);
            output = tail(output + juce::String::fromUTF8(buffer, n), 20000);
        }
        else if (!process.isRunning())
            break;
        else
            wait(50);
    }
    ok = !threadShouldExit() && process.getExitCode() == 0;
    finished.store(true);
}

// ================================================================= AiRuntime

AiRuntime& AiRuntime::get()
{
    static AiRuntime instance;
    return instance;
}

AiRuntime::AiRuntime() : juce::Thread("Expressive MIDI AI setup") {}

AiRuntime::~AiRuntime()
{
    cancel();
}

juce::File AiRuntime::rootFolder() const
{
   #if JUCE_WINDOWS
    const auto base = juce::File::getSpecialLocation(juce::File::windowsLocalAppData);
   #elif JUCE_MAC
    const auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("Application Support");
   #else
    const auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #endif
    return base.getChildFile("ExpressiveMidiBridge").getChildFile("ai");
}

juce::File AiRuntime::pythonExecutable() const
{
   #if JUCE_WINDOWS
    return rootFolder().getChildFile("venv").getChildFile("Scripts").getChildFile("python.exe");
   #else
    return rootFolder().getChildFile("venv").getChildFile("bin").getChildFile("python");
   #endif
}

juce::File AiRuntime::bridgeScript() const { return rootFolder().getChildFile("app").getChildFile("bridge.py"); }
juce::File AiRuntime::modelFolder() const { return rootFolder().getChildFile("model"); }

bool AiRuntime::isReady()
{
    if (isInstalling()) return false;
    if (verified.load()) return true;
    const auto marker = rootFolder().getChildFile("ready.txt");
    const bool ok = marker.existsAsFile() && marker.loadFileAsString().trim() == runtimeVersion
                 && pythonExecutable().existsAsFile()
                 && modelFolder().getChildFile("model.safetensors").existsAsFile()
                 && ensureApp();
    if (ok) { stage.store(Stage::ready); verified.store(true); }
    return ok;
}

bool AiRuntime::ensureApp()
{
    const auto app = rootFolder().getChildFile("app");
    const auto stampFile = app.getChildFile(".stamp");
    const auto stamp = juce::SHA256(EmbeddedPython::python_app_zip,
                                    static_cast<size_t>(EmbeddedPython::python_app_zipSize)).toHexString();
    if (stampFile.existsAsFile() && stampFile.loadFileAsString() == stamp && bridgeScript().existsAsFile())
        return true;
    app.deleteRecursively();
    if (!app.createDirectory()) return false;
    juce::MemoryInputStream zipData(EmbeddedPython::python_app_zip,
                                    static_cast<size_t>(EmbeddedPython::python_app_zipSize), false);
    juce::ZipFile zip(zipData);
    if (zip.getNumEntries() == 0 || zip.uncompressTo(app).failed()) return false;
    return bridgeScript().replaceWithText("from expressive_midi.ai_bridge import main\nraise SystemExit(main())\n")
        && stampFile.replaceWithText(stamp);
}

void AiRuntime::install()
{
    if (isInstalling()) return;
    {
        std::lock_guard<std::mutex> g(lock);
        failure.clear();
    }
    startedMs = juce::Time::getMillisecondCounterHiRes();
    verified.store(false);
    stage.store(Stage::tool);
    startThread();
}

void AiRuntime::cancel()
{
    signalThreadShouldExit();
    {
        std::lock_guard<std::mutex> g(lock);
        if (current) current->kill();
    }
    stopThread(10000);
}

juce::String AiRuntime::getFailure() const
{
    std::lock_guard<std::mutex> g(lock);
    return failure;
}

double AiRuntime::elapsedSeconds() const
{
    return startedMs > 0 ? (juce::Time::getMillisecondCounterHiRes() - startedMs) / 1000.0 : 0.0;
}

void AiRuntime::fail(const juce::String& message)
{
    {
        std::lock_guard<std::mutex> g(lock);
        failure = message;
    }
    stage.store(threadShouldExit() ? Stage::cancelled : Stage::failed);
}

bool AiRuntime::step(Stage s, const juce::StringArray& args)
{
    stage.store(s);
    progress.store(-1.0);
    auto process = std::make_unique<juce::ChildProcess>();
    if (!process->start(args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        fail("could not start " + args[0]);
        return false;
    }
    {
        std::lock_guard<std::mutex> g(lock);
        current = std::move(process);
    }
    juce::String out;
    char buffer[4096];
    for (;;)
    {
        juce::ChildProcess* p = nullptr;
        {
            std::lock_guard<std::mutex> g(lock);
            p = current.get();
        }
        const int n = p->readProcessOutput(buffer, sizeof(buffer));
        if (n > 0) out = tail(out + juce::String::fromUTF8(buffer, n));
        else if (!p->isRunning()) break;
        else if (threadShouldExit()) { p->kill(); break; }
        else wait(100);
    }
    std::unique_ptr<juce::ChildProcess> done;
    {
        std::lock_guard<std::mutex> g(lock);
        done = std::move(current);
    }
    if (threadShouldExit()) { fail("cancelled"); return false; }
    if (done->getExitCode() != 0)
    {
        fail(lastMeaningfulLine(out));
        return false;
    }
    return true;
}

bool AiRuntime::downloadTool(juce::File& uvExecutable)
{
    stage.store(Stage::tool);
    const auto archive = toolArchive();
    const auto folder = rootFolder().getChildFile("uv");
    const auto file = rootFolder().getChildFile(archive.name);
    folder.deleteRecursively();
    folder.createDirectory();

    const juce::URL url(juce::String("https://github.com/astral-sh/uv/releases/download/") + toolVersion + "/" + archive.name);
    int status = 0;
    auto in = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                        .withConnectionTimeoutMs(30000)
                                        .withNumRedirectsToFollow(8)
                                        .withStatusCode(&status));
    if (in == nullptr || status >= 400) { fail("download failed (" + url.toString(false) + ")"); return false; }
    juce::MemoryBlock data;
    const auto total = in->getTotalLength();
    char buffer[65536];
    for (;;)
    {
        if (threadShouldExit()) { fail("cancelled"); return false; }
        const int n = in->read(buffer, sizeof(buffer));
        if (n <= 0) break;
        data.append(buffer, static_cast<size_t>(n));
        if (total > 0) progress.store(static_cast<double>(data.getSize()) / static_cast<double>(total));
    }
    if (juce::SHA256(data).toHexString() != archive.sha256) { fail("checksum mismatch for " + juce::String(archive.name)); return false; }
    if (!file.replaceWithData(data.getData(), data.getSize())) { fail("cannot write " + file.getFullPathName()); return false; }

    if (file.hasFileExtension("zip"))
    {
        juce::ZipFile zip(file);
        if (zip.uncompressTo(folder).failed()) { fail("cannot extract " + file.getFileName()); return false; }
    }
    else
    {
        juce::ChildProcess tar;
        if (!tar.start(juce::StringArray { "/usr/bin/tar", "-xzf", file.getFullPathName(), "-C", folder.getFullPathName() })
            || !tar.waitForProcessToFinish(120000) || tar.getExitCode() != 0)
        { fail("cannot extract " + file.getFileName()); return false; }
    }
    file.deleteFile();
   #if JUCE_WINDOWS
    const auto found = folder.findChildFiles(juce::File::findFiles, true, "uv.exe");
   #else
    const auto found = folder.findChildFiles(juce::File::findFiles, true, "uv");
   #endif
    if (found.isEmpty()) { fail("uv not found in archive"); return false; }
    uvExecutable = found.getFirst();
    return true;
}

void AiRuntime::run()
{
    const auto root = rootFolder();
    if (!root.createDirectory()) { fail("cannot create " + root.getFullPathName()); return; }
    root.getChildFile("ready.txt").deleteFile();
    const auto cache = root.getChildFile("cache").getFullPathName();

    juce::File uv;
    if (!downloadTool(uv)) return;

    // Python 3.11 (a standalone build managed by uv, kept inside our folder) and a venv.
    const auto pythons = root.getChildFile("python");
    if (!step(Stage::python, { uv.getFullPathName(), "python", "install", "3.11",
                               "--install-dir", pythons.getFullPathName(), "--cache-dir", cache })) return;
   #if JUCE_WINDOWS
    const auto candidates = pythons.findChildFiles(juce::File::findFiles, true, "python.exe");
   #else
    const auto candidates = pythons.findChildFiles(juce::File::findFiles, true, "python3.11");
   #endif
    if (candidates.isEmpty()) { fail("Python 3.11 was not installed"); return; }
    if (!step(Stage::python, { uv.getFullPathName(), "venv", root.getChildFile("venv").getFullPathName(),
                               "--python", candidates.getFirst().getFullPathName(), "--clear", "--cache-dir", cache })) return;
    const auto py = pythonExecutable().getFullPathName();

    // PyTorch: CPU build (the PyPI build on macOS). Versions match the upstream README.
    juce::StringArray torch { uv.getFullPathName(), "pip", "install", "--python", py, "--cache-dir", cache, "torch==2.7.1" };
   #if ! JUCE_MAC
    torch.addArray({ "--index-url", "https://download.pytorch.org/whl/cpu" });
   #endif
    if (!step(Stage::torch, torch)) return;
    if (!step(Stage::libraries, { uv.getFullPathName(), "pip", "install", "--python", py, "--cache-dir", cache,
                                  "transformers==4.54.0", "miditoolkit==1.0.1", "mido>=1.3,<2",
                                  "huggingface_hub", "numpy", "tqdm" })) return;

    if (!ensureApp()) { fail("cannot write the AI scripts"); return; }
    if (!modelFolder().getChildFile("model.safetensors").existsAsFile())
        if (!step(Stage::model, { py, bridgeScript().getFullPathName(), "download-model",
                                  "--dest", modelFolder().getFullPathName() })) return;
    if (!step(Stage::verify, { py, bridgeScript().getFullPathName(), "check",
                               "--model", modelFolder().getFullPathName() })) return;

    juce::File(cache).deleteRecursively();
    root.getChildFile("ready.txt").replaceWithText(runtimeVersion);
    stage.store(Stage::ready);
}

// ================================================================= AiSession

AiSession::AiSession()
{
    workFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getChildFile("ExpressiveMidiBridge")
                     .getChildFile("session-" + juce::Uuid().toDashedString());
    workFolder.createDirectory();
}

AiSession::~AiSession()
{
    job.reset();
    workFolder.deleteRecursively();
}

bool AiSession::loadFile(const juce::File& file, double hostBpm, juce::String& error)
{
    juce::MemoryBlock bytes;
    if (!file.existsAsFile() || !file.loadFileAsData(bytes)) { error = "read"; return false; }
    Clip clip;
    double fileBpm = 0.0;
    if (!clipFromMidiBytes(bytes.getData(), bytes.getSize(), clip, fileBpm, error)) return false;
    if (!work("score.mid").replaceWithData(bytes.getData(), bytes.getSize())) { error = "write"; return false; }
    job.reset();
    jobKind = JobKind::none;
    source = std::move(clip);
    result = {};
    sourceName = file.getFileNameWithoutExtension();
    bpm = hostBpm > 0 ? hostBpm : (fileBpm > 0 ? fileBpm : 120.0);
    hasRaw = false;
    blendPending = false;
    failure.clear();
    work("raw.mid").deleteFile();
    work("out.mid").deleteFile();
    if (AiRuntime::get().isReady()) startRender();
    else status = Status::needsSetup;
    sendChangeMessage();
    return true;
}

void AiSession::setStrength(double v)
{
    strength = juce::jlimit(0.0, 1.0, v);
    if (hasRaw)
    {
        blendPending = true;
        blendRequestedMs = juce::Time::getMillisecondCounterHiRes();
    }
}

void AiSession::startRender()
{
    auto& rt = AiRuntime::get();
    job = std::make_unique<ExternalJob>(juce::StringArray {
        rt.pythonExecutable().getFullPathName(), rt.bridgeScript().getFullPathName(), "render",
        "--score", work("score.mid").getFullPathName(), "--raw", work("raw.mid").getFullPathName(),
        "--model", rt.modelFolder().getFullPathName() });
    jobKind = JobKind::render;
    status = Status::rendering;
    failure.clear();
    job->start();
    sendChangeMessage();
}

void AiSession::startBlend()
{
    auto& rt = AiRuntime::get();
    blendPending = false;
    job = std::make_unique<ExternalJob>(juce::StringArray {
        rt.pythonExecutable().getFullPathName(), rt.bridgeScript().getFullPathName(), "blend",
        "--score", work("score.mid").getFullPathName(), "--raw", work("raw.mid").getFullPathName(),
        "--out", work("out.mid").getFullPathName(), "--bpm", juce::String(bpm, 4),
        "--strength", juce::String(strength, 3) });
    jobKind = JobKind::blend;
    if (result.empty()) status = Status::blending;
    job->start();
    sendChangeMessage();
}

void AiSession::setFailure(const juce::String& message)
{
    failure = message;
    status = Status::failed;
    sendChangeMessage();
}

void AiSession::poll()
{
    if (job != nullptr && job->isFinished())
    {
        const auto finished = std::move(job);
        const auto kind = jobKind;
        jobKind = JobKind::none;
        if (!finished->succeeded())
            setFailure(finished->errorLine());
        else if (kind == JobKind::render)
        {
            hasRaw = true;
            startBlend();
        }
        else
        {
            juce::MemoryBlock bytes;
            Clip clip;
            double fileBpm = 0.0;
            juce::String error;
            if (work("out.mid").loadFileAsData(bytes)
                && clipFromMidiBytes(bytes.getData(), bytes.getSize(), clip, fileBpm, error))
            {
                result = std::move(clip);
                status = Status::ready;
                sendChangeMessage();
            }
            else
                setFailure("could not read the AI result");
        }
    }
    if (job == nullptr)
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (blendPending && now - blendRequestedMs > 250.0)
            startBlend();
        else if (status == Status::needsSetup && hasSource() && AiRuntime::get().isReady())
            startRender();
    }
}

juce::File AiSession::exportResult(juce::String& error) const
{
    if (!work("out.mid").existsAsFile()) { error = "noresult"; return {}; }
    const auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                            .getChildFile("Expressive MIDI Bridge");
    if (!folder.createDirectory()) { error = "write"; return {}; }
    const auto target = folder.getChildFile(juce::File::createLegalFileName(
        (sourceName.isEmpty() ? juce::String("piano") : sourceName) + " (AI " + juce::String(juce::roundToInt(strength * 100)) + "%).mid"));
    if (!work("out.mid").copyFileTo(target)) { error = "write"; return {}; }
    return target;
}

void AiSession::saveState(juce::XmlElement& parent) const
{
    auto* x = parent.createNewChildElement("Ai");
    x->setAttribute("strength", strength);
    if (!hasSource()) return;
    x->setAttribute("name", sourceName);
    x->setAttribute("bpm", bpm);
    const auto store = [x](const char* key, const juce::File& f)
    {
        juce::MemoryBlock bytes;
        if (f.existsAsFile() && f.loadFileAsData(bytes)) x->setAttribute(key, bytes.toBase64Encoding());
    };
    store("score", work("score.mid"));
    if (hasRaw) store("raw", work("raw.mid"));
    if (hasResult()) store("out", work("out.mid"));
}

void AiSession::loadState(const juce::XmlElement& parent)
{
    const auto* x = parent.getChildByName("Ai");
    if (x == nullptr) return;
    strength = juce::jlimit(0.0, 1.0, x->getDoubleAttribute("strength", 1.0));
    const auto restore = [x, this](const char* key, const juce::String& file, Clip* clip)
    {
        juce::MemoryBlock bytes;
        if (!bytes.fromBase64Encoding(x->getStringAttribute(key)) || bytes.isEmpty()) return false;
        if (!work(file).replaceWithData(bytes.getData(), bytes.getSize())) return false;
        if (clip == nullptr) return true;
        double fileBpm = 0.0;
        juce::String error;
        return clipFromMidiBytes(bytes.getData(), bytes.getSize(), *clip, fileBpm, error);
    };
    if (!x->hasAttribute("score") || !restore("score", "score.mid", &source)) return;
    sourceName = x->getStringAttribute("name");
    bpm = x->getDoubleAttribute("bpm", 120.0);
    hasRaw = x->hasAttribute("raw") && restore("raw", "raw.mid", nullptr);
    if (x->hasAttribute("out") && restore("out", "out.mid", &result)) status = Status::ready;
    else if (hasRaw) { blendPending = true; status = Status::blending; }
    else status = Status::needsSetup; // poll() starts rendering once the runtime is ready
    sendChangeMessage();
}
} // namespace expressive
