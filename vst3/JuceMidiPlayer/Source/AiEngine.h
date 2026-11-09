#pragma once
#include <JuceHeader.h>
#include "ClipModel.h"
#include <atomic>
#include <memory>
#include <mutex>

// Everything AI-related, on the message thread or on background threads (never audio):
//
//  AiRuntime  - a private Python environment with Pianist Transformer, installed by one
//               click into the user's local app-data folder (no Python/Git needed):
//               uv (pinned, checksum-verified) -> Python 3.11 -> torch (CPU) + libraries
//               -> embedded Python code -> model weights from Hugging Face.
//  AiSession  - the per-plugin-instance workflow: dropped .mid -> AI render (once, slow)
//               -> blend with the score at the chosen strength (fast) -> .mid to drag.
namespace expressive
{
bool clipFromMidiBytes(const void* data, size_t size, Clip& clip, double& fileBpm, juce::String& error);

// Runs one external command on a background thread and keeps its output.
class ExternalJob final : private juce::Thread
{
public:
    explicit ExternalJob(juce::StringArray args);
    ~ExternalJob() override;
    void start() { startThread(); }
    bool isFinished() const { return finished.load(); }
    bool succeeded() const { return ok; }
    juce::String log() const;
    juce::String errorLine() const;   // last "ERROR ..." line, or the last non-empty line
    double elapsedSeconds() const { return (juce::Time::getMillisecondCounterHiRes() - startedMs) / 1000.0; }
private:
    void run() override;
    juce::StringArray arguments;
    mutable std::mutex lock;
    juce::String output;
    bool ok = false;
    std::atomic<bool> finished { false };
    double startedMs = juce::Time::getMillisecondCounterHiRes();
};

class AiRuntime final : private juce::Thread
{
public:
    enum class Stage { notInstalled, tool, python, torch, libraries, model, verify, ready, failed, cancelled };

    static AiRuntime& get();   // one runtime shared by all plugin instances in the process
    ~AiRuntime() override;

    bool isReady();
    void install();             // starts the background installation (no-op if running)
    void cancel();
    bool isInstalling() const { return isThreadRunning(); }
    Stage getStage() const { return stage.load(); }
    double getProgress() const { return progress.load(); }  // 0..1 for downloads, -1 if unknown
    juce::String getFailure() const;
    double elapsedSeconds() const;

    juce::File rootFolder() const;
    juce::File pythonExecutable() const;      // inside the private venv
    juce::File bridgeScript() const;          // <root>/app/bridge.py
    juce::File modelFolder() const;

private:
    AiRuntime();
    void run() override;
    bool ensureApp();                       // extracts the embedded Python code if missing/outdated
    bool step(Stage, const juce::StringArray& args);
    bool downloadTool(juce::File& uvExecutable);
    void fail(const juce::String& message);

    std::atomic<Stage> stage { Stage::notInstalled };
    std::atomic<bool> verified { false };
    std::atomic<double> progress { -1.0 };
    mutable std::mutex lock;
    juce::String failure;
    double startedMs = 0.0;
    std::unique_ptr<juce::ChildProcess> current;
};

class AiSession final : public juce::ChangeBroadcaster
{
public:
    enum class Status { noSource, needsSetup, rendering, blending, ready, failed };

    AiSession();
    ~AiSession() override;

    bool loadFile(const juce::File&, double hostBpm, juce::String& error);
    void setStrength(double);              // 0..1; re-blends without running the model again
    double getStrength() const { return strength; }

    Status getStatus() const { return status; }
    juce::String getFailure() const { return failure; }
    double jobElapsedSeconds() const { return job ? job->elapsedSeconds() : 0.0; }
    bool hasSource() const { return !source.empty(); }
    const Clip& getSource() const { return source; }
    bool hasResult() const { return !result.empty(); }
    const Clip& getResult() const { return result; }
    const juce::String& getSourceName() const { return sourceName; }

    juce::File exportResult(juce::String& error) const;   // stable copy in Documents for drag & drop
    void poll();                                          // call regularly on the message thread

    void saveState(juce::XmlElement&) const;
    void loadState(const juce::XmlElement&);

private:
    void startRender();
    void startBlend();
    void setFailure(const juce::String&);
    juce::File work(const juce::String& name) const { return workFolder.getChildFile(name); }

    juce::File workFolder;
    Clip source, result;
    juce::String sourceName;
    double bpm = 120.0;
    double strength = 1.0;
    Status status = Status::noSource;
    juce::String failure;
    bool hasRaw = false;
    bool blendPending = false;
    double blendRequestedMs = 0.0;
    std::unique_ptr<ExternalJob> job;
    enum class JobKind { none, render, blend } jobKind = JobKind::none;
};
} // namespace expressive
