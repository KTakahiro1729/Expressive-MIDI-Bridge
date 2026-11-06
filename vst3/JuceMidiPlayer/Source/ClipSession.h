#pragma once
#include <JuceHeader.h>
#include "ClipModel.h"
#include <memory>

// Message-thread state of the clip workflow:
//   1. source MIDI (dropped/opened .mid file, or captured from this track while playing)
//   2. how it should be played (rule-based styles, or the external Pianist Transformer AI)
//   3. the rendered result, exported as a .mid file that the user drags onto a track.
// Nothing here runs on the audio thread.
namespace expressive
{
// MIDI file <-> Clip conversion. Positions are quarter-note beats (tempo-independent).
bool clipFromMidiBytes(const void* data, size_t size, Clip& clip, double& fileBpm, juce::String& error);
juce::MemoryBlock clipToMidiBytes(const Clip& clip, double bpm, double originBeat, const juce::String& trackName);

// Machine-specific settings for the optional AI engine (stored per user, not per project).
struct AiSettings
{
    juce::String repoFolder;     // PianistTransformer checkout
    juce::String pythonPath;     // optional override; default = <repo>/.venv python
    juce::String device { "cpu" };

    juce::File resolvedPython() const;
    static AiSettings load();
    void save() const;
};

// Runs one external command on a background thread (AI render or environment check).
class ExternalJob final : private juce::Thread
{
public:
    ExternalJob(juce::StringArray args, juce::File workingDirectory, juce::File expectedOutput);
    ~ExternalJob() override;
    void start() { startThread(); }
    void cancel() { signalThreadShouldExit(); }
    bool isFinished() const { return finished.load(); }
    bool succeeded() const { return ok; }               // valid once finished
    const juce::String& log() const { return output; }  // valid once finished
    const juce::File& outputFile() const { return expected; }
    double elapsedSeconds() const { return (juce::Time::getMillisecondCounterHiRes() - startedMs) / 1000.0; }
private:
    void run() override;
    juce::StringArray arguments;
    juce::File directory, expected;
    juce::String output;
    bool ok = false;
    std::atomic<bool> finished { false };
    double startedMs = juce::Time::getMillisecondCounterHiRes();
};

class ClipSession final : public juce::ChangeBroadcaster
{
public:
    enum class Engine { ruleBased = 0, ai = 1 };
    enum class SourceKind { none = 0, file = 1, captured = 2 };
    enum class AiPedal { model = 0, score = 1, none = 2 };

    // --- step 1: source ---
    bool loadFile(const juce::File&, double hostBpm, juce::String& error);
    void setCaptured(Clip clip, double bpm);
    void clearSource();
    bool hasSource() const { return !source.empty(); }
    const Clip& getSource() const { return source; }
    SourceKind getSourceKind() const { return sourceKind; }
    const juce::String& getSourceName() const { return sourceName; }
    double getBpm() const { return bpm; }

    // --- step 2: how to play ---
    Engine getEngine() const { return engine; }
    void setEngine(Engine);
    RenderSettings getRuleSettings() const { return rule; }
    void setRuleSettings(RenderSettings);
    void reroll();
    double getAiStrength() const { return aiStrength; }
    void setAiStrength(double);
    AiPedal getAiPedal() const { return aiPedal; }
    void setAiPedal(AiPedal);

    AiSettings& aiSettings() { return ai; }
    bool startAi(juce::String& error);
    bool startAiCheck(juce::String& error);
    void cancelJob();
    bool isAiRunning() const { return job != nullptr && !jobIsCheck; }
    bool isCheckRunning() const { return job != nullptr && jobIsCheck; }
    double jobElapsedSeconds() const { return job ? job->elapsedSeconds() : 0.0; }
    const juce::String& getAiStatus() const { return aiStatus; }
    bool aiStatusIsError() const { return aiStatusError; }
    bool isAiResultStale() const { return engine == Engine::ai && aiValid && aiResultStale; }

    // --- step 3: result ---
    bool hasResult() const { return engine == Engine::ai ? aiValid : !source.empty(); }
    const Clip& getResult() const { return engine == Engine::ai ? aiResult : ruleResult; }
    const RenderStats& getStats() const { return engine == Engine::ai ? aiStats : ruleStats; }
    juce::File exportResult(juce::String& error) const;   // writes the .mid used for drag & drop
    juce::String suggestedFileName() const;
    juce::File exportFolder() const;

    // Called regularly on the message thread (collects finished background jobs).
    void poll();

    void saveState(juce::XmlElement&) const;
    void loadState(const juce::XmlElement&);

private:
    void render();
    void sourceChanged();

    Clip source, ruleResult, aiResult;
    SourceKind sourceKind = SourceKind::none;
    juce::String sourceName;
    double bpm = 120.0;
    Engine engine = Engine::ruleBased;
    RenderSettings rule;
    double aiStrength = 1.0;
    AiPedal aiPedal = AiPedal::model;
    RenderStats ruleStats, aiStats;
    bool aiValid = false;
    bool aiResultStale = false;

    AiSettings ai = AiSettings::load();
    std::unique_ptr<ExternalJob> job;
    bool jobIsCheck = false;
    juce::String aiStatus;
    bool aiStatusError = false;
};
} // namespace expressive
