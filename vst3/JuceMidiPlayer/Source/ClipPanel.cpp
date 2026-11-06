#include "ClipPanel.h"
#include <cmath>
#include <initializer_list>

using namespace ui;
using expressive::ClipSession;

// ============================================================== PianoRollPreview

void PianoRollPreview::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(colours::background);
    g.fillRoundedRectangle(area, 5.0f);
    g.setColour(colours::panelEdge);
    g.drawRoundedRectangle(area.reduced(0.5f), 5.0f, 1.0f);

    if (source == nullptr || source->empty())
    {
        g.setColour(colours::dim);
        g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText(emptyText, getLocalBounds().reduced(12), juce::Justification::centred, 3);
        return;
    }

    auto roll = area.reduced(6.0f);
    auto lane = roll.removeFromBottom(26.0f);
    roll.removeFromBottom(4.0f);

    int lo = 127, hi = 0;
    for (const auto& n : source->notes) { lo = std::min(lo, n.pitch); hi = std::max(hi, n.pitch); }
    lo = std::max(0, lo - 2);
    hi = std::min(127, hi + 2);
    const double t0 = source->originBeat;
    double t1 = std::max(source->lastBeat(), result != nullptr ? result->lastBeat() : 0.0);
    if (t1 <= t0) t1 = t0 + 4.0;
    const auto x = [&](double beat)
    { return roll.getX() + static_cast<float>((beat - t0) / (t1 - t0)) * roll.getWidth(); };
    const float rowH = roll.getHeight() / static_cast<float>(hi - lo + 1);
    const auto y = [&](int pitch) { return roll.getBottom() - static_cast<float>(pitch - lo + 1) * rowH; };

    // bar lines
    const double bar = source->beatsPerBar > 0 ? source->beatsPerBar : 4.0;
    const double barsVisible = (t1 - t0) / bar;
    const int labelEvery = barsVisible > 32 ? 8 : barsVisible > 12 ? 4 : 1;
    int barIndex = 0;
    g.setFont(juce::FontOptions(10.0f));
    for (double b = t0; b <= t1 + 1e-9; b += bar, ++barIndex)
    {
        const float bx = x(b);
        g.setColour(colours::panelEdge.withAlpha(0.7f));
        g.drawVerticalLine(juce::roundToInt(bx), roll.getY(), lane.getBottom());
        if (barIndex % labelEvery == 0)
        {
            g.setColour(colours::dim);
            g.drawText(juce::String(juce::roundToInt(b / bar) + 1), juce::Rectangle<float>(bx + 2, roll.getY(), 30, 12),
                       juce::Justification::topLeft);
        }
    }

    const float minW = 1.5f;
    for (const auto& n : source->notes)
    {
        const auto r = juce::Rectangle<float>(x(n.start), y(n.pitch), std::max(minW, x(n.end) - x(n.start)),
                                              std::max(1.5f, rowH - 0.5f));
        g.setColour(colours::dim.withAlpha(0.55f));
        g.drawRect(r, 1.0f);
    }
    if (result != nullptr)
        for (const auto& n : result->notes)
        {
            const auto r = juce::Rectangle<float>(x(n.start), y(n.pitch), std::max(minW, x(n.end) - x(n.start)),
                                                  std::max(1.5f, rowH - 0.5f));
            g.setColour(colours::accent.withAlpha(0.35f + 0.65f * static_cast<float>(n.velocity) / 127.0f));
            g.fillRect(r);
        }

    // velocity lane: grey = original, blue = result
    g.setColour(colours::panel);
    g.fillRect(lane);
    for (const auto& n : source->notes)
    {
        g.setColour(colours::dim.withAlpha(0.6f));
        const float h = lane.getHeight() * static_cast<float>(n.velocity) / 127.0f;
        g.fillRect(x(n.start) - 0.5f, lane.getBottom() - h, 1.0f, 1.5f);
    }
    if (result != nullptr)
        for (const auto& n : result->notes)
        {
            const float h = lane.getHeight() * static_cast<float>(n.velocity) / 127.0f;
            g.setColour(colours::accent);
            g.fillRect(x(n.start) - 0.5f, lane.getBottom() - h, 1.2f, h);
        }
}

// ============================================================== DragToDawHandle

void DragToDawHandle::setText(const juce::String& t, const juce::String& s)
{
    title = t;
    subtitle = s;
    repaint();
}

void DragToDawHandle::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    const auto c = !active ? colours::panelEdge : (hover || dragging) ? colours::ok : colours::accent;
    g.setColour(active ? c.withAlpha(hover ? 0.22f : 0.14f) : colours::panel);
    g.fillRoundedRectangle(r, 7.0f);
    drawDashedBox(g, r, c, 7.0f);

    // grip icon: three horizontal bars
    auto icon = r.removeFromLeft(54.0f).withSizeKeepingCentre(22.0f, 18.0f);
    g.setColour(active ? c : colours::dim);
    for (int i = 0; i < 3; ++i)
        g.fillRoundedRectangle(icon.getX(), icon.getY() + i * 7.0f, icon.getWidth(), 3.5f, 1.5f);

    g.setColour(active ? colours::text : colours::dim);
    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.drawText(title, r.removeFromTop(r.getHeight() * 0.55f).withTrimmedTop(4), juce::Justification::bottomLeft);
    g.setColour(colours::dim);
    g.setFont(juce::FontOptions(12.5f));
    g.drawText(subtitle, r, juce::Justification::topLeft);
}

void DragToDawHandle::mouseDrag(const juce::MouseEvent& e)
{
    if (!active || dragging || e.getDistanceFromDragStart() < 6 || !prepareFile) return;
    const auto file = prepareFile();
    if (!file.existsAsFile()) return;
    dragging = true;
    repaint();
    juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.getFullPathName() }, false, this,
                                                               [this] { dragging = false; repaint(); });
}

// ============================================================== ClipPanel

ClipPanel::ClipPanel(ExpressiveMidiPlayerProcessor& p, Lang& l)
    : processor(p), session(p.getClipSession()), tr(l)
{
    for (auto* label : { &step1Title, &step2Title, &step3Title, &settingsTitle })
    {
        addAndMakeVisible(*label);
        styleLabel(*label, 15.0f, colours::text, true);
    }
    for (auto* label : { &sourceInfo, &resultInfo })
    {
        addAndMakeVisible(*label);
        styleLabel(*label, 13.5f, colours::text);
        label->setJustificationType(juce::Justification::topLeft);
    }
    for (auto* label : { &sourceHelp, &strengthHelp, &aiStatus, &placementHelp, &settingsHelp, &checkStatus })
    {
        addAndMakeVisible(*label);
        styleLabel(*label, 12.5f, colours::dim);
        label->setJustificationType(juce::Justification::topLeft);
    }
    for (auto* label : { &strengthLabel, &aiStrengthLabel, &pedalLabel, &repoLabel, &pythonLabel, &deviceLabel })
    {
        addAndMakeVisible(*label);
        styleLabel(*label, 13.5f, colours::text);
    }

    // ---- step 1
    for (auto* b : { &openButton, &captureButton, &changeSourceButton })
        addAndMakeVisible(*b);
    stylePrimary(openButton);
    openButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>(tr("Choose the MIDI to make expressive", "表情を付けるMIDIを選択"),
                                                      juce::File{}, "*.mid;*.midi");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& fc)
                             {
                                 if (fc.getResult().existsAsFile()) loadFile(fc.getResult());
                             });
    };
    captureButton.onClick = [this]
    {
        if (processor.getCaptureState() == ExpressiveMidiPlayerProcessor::captureIdle)
        {
            processor.armCapture();
            message.clear();
        }
        else
            processor.cancelCapture();
        refreshState();
    };
    changeSourceButton.onClick = [this]
    {
        session.clearSource();
        message.clear();
        settingsVisible = false;
        refreshState();
    };

    // ---- step 2
    addAndMakeVisible(ruleTab);
    addAndMakeVisible(aiTab);
    styleToggle(ruleTab, 11);
    styleToggle(aiTab, 11);
    ruleTab.onClick = [this] { if (ruleTab.getToggleState()) { session.setEngine(ClipSession::Engine::ruleBased); refreshState(); } };
    aiTab.onClick = [this] { if (aiTab.getToggleState()) { session.setEngine(ClipSession::Engine::ai); refreshState(); } };
    for (size_t i = 0; i < styleButtons.size(); ++i)
    {
        addAndMakeVisible(styleButtons[i]);
        styleToggle(styleButtons[i], 12);
        styleButtons[i].onClick = [this, i]
        {
            if (!styleButtons[i].getToggleState()) return;
            auto s = session.getRuleSettings();
            s.style = static_cast<expressive::Style>(i);
            session.setRuleSettings(s);
            refreshState();
        };
    }
    for (auto* s : { &strengthSlider, &aiStrengthSlider })
    {
        addAndMakeVisible(*s);
        s->setRange(0, 100, 1);
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
        s->setTextValueSuffix(" %");
        s->setColour(juce::Slider::trackColourId, colours::accent);
        s->setColour(juce::Slider::thumbColourId, colours::text);
    }
    strengthSlider.setDoubleClickReturnValue(true, 65);
    aiStrengthSlider.setDoubleClickReturnValue(true, 100);
    strengthSlider.onValueChange = [this] { pushRuleSettings(); };
    aiStrengthSlider.onValueChange = [this] { session.setAiStrength(aiStrengthSlider.getValue() / 100.0); };
    addAndMakeVisible(rerollButton);
    rerollButton.onClick = [this] { session.reroll(); refreshState(); };

    addAndMakeVisible(pedalBox);
    pedalBox.onChange = [this]
    { session.setAiPedal(static_cast<ClipSession::AiPedal>(juce::jmax(0, pedalBox.getSelectedId() - 1))); };
    addAndMakeVisible(runAiButton);
    stylePrimary(runAiButton);
    runAiButton.onClick = [this]
    {
        if (session.isAiRunning()) { session.cancelJob(); return; }
        juce::String error;
        if (!session.startAi(error))
        {
            showError(error);
            if (error == "norepo" || error == "nopython") { settingsVisible = true; refreshState(); }
        }
    };
    addAndMakeVisible(aiSettingsButton);
    aiSettingsButton.onClick = [this] { settingsVisible = true; refreshAiSettingsFields(); refreshState(); };

    // ---- AI settings
    for (auto* f : { &repoField, &pythonField })
    {
        addAndMakeVisible(*f);
        f->setFont(juce::FontOptions(13.0f));
        f->setColour(juce::TextEditor::backgroundColourId, colours::background);
        f->setColour(juce::TextEditor::outlineColourId, colours::panelEdge);
        f->onTextChange = [this]
        {
            session.aiSettings().repoFolder = repoField.getText().trim();
            session.aiSettings().pythonPath = pythonField.getText().trim();
            session.aiSettings().save();
        };
    }
    for (auto* b : { &repoBrowse, &pythonBrowse, &checkButton, &closeSettings })
        addAndMakeVisible(*b);
    repoBrowse.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("PianistTransformer", juce::File(repoField.getText()));
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                             [this](const juce::FileChooser& fc)
                             {
                                 if (fc.getResult().isDirectory())
                                     repoField.setText(fc.getResult().getFullPathName(), juce::sendNotification);
                             });
    };
    pythonBrowse.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("Python", juce::File(pythonField.getText()));
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& fc)
                             {
                                 if (fc.getResult().existsAsFile())
                                     pythonField.setText(fc.getResult().getFullPathName(), juce::sendNotification);
                             });
    };
    addAndMakeVisible(deviceBox);
    deviceBox.addItem("CPU", 1);
    deviceBox.addItem("CUDA (NVIDIA GPU)", 2);
    deviceBox.addItem("MPS (Apple Silicon)", 3);
    deviceBox.onChange = [this]
    {
        const char* devices[] = { "cpu", "cuda", "mps" };
        session.aiSettings().device = devices[juce::jlimit(0, 2, deviceBox.getSelectedId() - 1)];
        session.aiSettings().save();
    };
    checkButton.onClick = [this]
    {
        juce::String error;
        if (!session.startAiCheck(error)) showError(error);
    };
    stylePrimary(closeSettings);
    closeSettings.onClick = [this] { settingsVisible = false; refreshState(); };

    // ---- step 3
    addAndMakeVisible(preview);
    addAndMakeVisible(dragHandle);
    dragHandle.prepareFile = [this]
    {
        juce::String error;
        const auto f = session.exportResult(error);
        if (!f.existsAsFile()) showError(error);
        return f;
    };
    addAndMakeVisible(saveButton);
    addAndMakeVisible(revealButton);
    saveButton.onClick = [this]
    {
        juce::String error;
        const auto f = session.exportResult(error);
        if (!f.existsAsFile()) { showError(error); return; }
        chooser = std::make_unique<juce::FileChooser>(tr("Save expressive MIDI", "表情付きMIDIを保存"), f, "*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                             [this, f](const juce::FileChooser& fc)
                             {
                                 auto target = fc.getResult();
                                 if (target == juce::File()) return;
                                 if (!target.hasFileExtension("mid;midi")) target = target.withFileExtension("mid");
                                 if (f.copyFileTo(target))
                                 { message = tr("Saved: ", "保存しました: ") + target.getFileName(); messageIsError = false; }
                                 else showError("exportdir");
                                 refreshState();
                             });
    };
    revealButton.onClick = [this]
    {
        juce::String error;
        const auto f = session.exportResult(error);
        if (f.existsAsFile()) f.revealToUser();
        else showError(error);
    };

    session.addChangeListener(this);
    refreshAiSettingsFields();
    refreshTexts();
    startTimerHz(10);
}

ClipPanel::~ClipPanel()
{
    session.removeChangeListener(this);
}

void ClipPanel::refreshAiSettingsFields()
{
    const auto& s = session.aiSettings();
    repoField.setText(s.repoFolder, juce::dontSendNotification);
    pythonField.setText(s.pythonPath, juce::dontSendNotification);
    deviceBox.setSelectedId(s.device == "cuda" ? 2 : s.device == "mps" ? 3 : 1, juce::dontSendNotification);
}

void ClipPanel::changeListenerCallback(juce::ChangeBroadcaster*) { refreshState(); }

void ClipPanel::timerCallback()
{
    // capture progress and AI elapsed time change continuously
    if (processor.getCaptureState() != ExpressiveMidiPlayerProcessor::captureIdle || session.isAiRunning()
        || session.isCheckRunning())
        refreshState();
}

void ClipPanel::loadFile(const juce::File& file)
{
    if (processor.getCaptureState() != ExpressiveMidiPlayerProcessor::captureIdle) processor.cancelCapture();
    juce::String error;
    const auto activity = processor.getActivity();
    if (session.loadFile(file, activity.hasClock ? activity.bpm : 0.0, error)) message.clear();
    else showError(error);
    refreshState();
}

void ClipPanel::showError(const juce::String& key)
{
    message = errorText(key);
    messageIsError = true;
    refreshState();
}

juce::String ClipPanel::errorText(const juce::String& key) const
{
    if (key == "read") return tr("The file could not be read.", "ファイルを読み込めませんでした。");
    if (key == "invalid") return tr("This is not a standard MIDI file.", "標準MIDIファイルではありません。");
    if (key == "smpte") return tr("SMPTE-timed MIDI files are not supported.", "SMPTE形式のMIDIファイルには対応していません。");
    if (key == "toolarge") return tr("The file is larger than 32 MB.", "ファイルが 32 MB を超えています。");
    if (key == "empty") return tr("The file contains no notes.", "ファイルにノートがありません。");
    if (key == "nosource") return tr("Bring in MIDI first (step 1).", "先に手順1でMIDIを用意してください。");
    if (key == "busy") return tr("A job is already running.", "別の処理を実行中です。");
    if (key == "norepo") return tr("Set the PianistTransformer folder in AI settings.",
                                   "AI の設定で PianistTransformer のフォルダを指定してください。");
    if (key == "nopython") return tr("Python was not found in <folder>/.venv. Run the setup script or set Python.",
                                     "<フォルダ>/.venv に Python が見つかりません。セットアップスクリプトを実行するか、Python を指定してください。");
    if (key == "tempdir" || key == "exportdir") return tr("Could not write the MIDI file.", "MIDIファイルを書き込めませんでした。");
    if (key == "noresult") return tr("There is no result yet.", "まだ結果がありません。");
    return key;
}

juce::String ClipPanel::barText(double beat) const
{
    const auto& src = session.getSource();
    const double bar = src.beatsPerBar > 0 ? src.beatsPerBar : 4.0;
    return formatNumber(std::floor(beat / bar + 1e-9) + 1.0, 0);
}

juce::String ClipPanel::sourceSummary() const
{
    const auto& src = session.getSource();
    const double bars = (src.lastBeat() - src.originBeat) / (src.beatsPerBar > 0 ? src.beatsPerBar : 4.0);
    juce::String name = session.getSourceKind() == ClipSession::SourceKind::captured
        ? tr("Captured from this track", "このトラックから取り込み")
        : session.getSourceName() + ".mid";
    return name + "\n"
         + juce::String(static_cast<int>(src.notes.size())) + tr(" notes", " ノート") + "  ·  "
         + tr("about ", "約 ") + formatNumber(std::ceil(bars), 0) + tr(" bars", " 小節") + "  ·  "
         + juce::String(src.timeSigNumerator) + "/" + juce::String(src.timeSigDenominator) + "  ·  "
         + formatNumber(session.getBpm(), 1) + " BPM";
}

juce::String ClipPanel::resultSummary() const
{
    if (!session.hasResult()) return {};
    const auto& st = session.getStats();
    return tr("Timing: avg ", "タイミングのずれ: 平均 ") + formatNumber(st.meanAbsShiftMs, 1)
         + tr(" ms (max ", " ms（最大 ") + formatNumber(st.maxAbsShiftMs, 1) + tr(" ms)", " ms）")
         + tr("   Velocity: avg +/-", "   強弱の変化: 平均 ±") + formatNumber(st.meanAbsVelocityChange, 1);
}

juce::String ClipPanel::aiStatusText() const
{
    const auto status = session.getAiStatus();
    const auto first = status.upToFirstOccurrenceOf("\n", false, false);
    const auto detail = status.fromFirstOccurrenceOf("\n", false, false).trim();
    if (session.isAiRunning())
        return tr("AI is rendering... ", "AI で加工中… ") + formatNumber(std::floor(session.jobElapsedSeconds()), 0)
             + tr(" s (on CPU this can take several minutes)", " 秒（CPU では数分かかることがあります）");
    if (session.isCheckRunning()) return tr("Checking the AI environment...", "AI の環境を確認中…");
    if (first == "done")
        return session.isAiResultStale()
            ? tr("Settings changed since the last AI render. Press \"Render with AI\" again.",
                 "前回の AI 加工のあとに設定が変わりました。もう一度「AI で加工」を押してください。")
            : tr("AI render finished.", "AI での加工が終わりました。");
    if (first == "cancelled") return tr("Cancelled.", "キャンセルしました。");
    if (first == "check-ok") return tr("AI environment OK.", "AI の環境は正常です。");
    if (first == "no-module")
        return tr("expressive_midi is not installed in that Python. Re-run the setup script.",
                  "その Python に expressive_midi が入っていません。セットアップスクリプトを実行し直してください。");
    if (first == "start-failed") return tr("Python could not be started.", "Python を起動できませんでした。");
    if (first == "check-failed")
    {
        // `doctor` prints a JSON object; turn the failed checks into plain language.
        const auto json = juce::JSON::parse(detail.fromFirstOccurrenceOf("{", true, false)
                                                  .upToLastOccurrenceOf("}", true, false));
        if (auto* obj = json.getDynamicObject())
        {
            juce::StringArray problems;
            const auto isFalse = [obj](const char* key) { return obj->hasProperty(key) && !static_cast<bool>(obj->getProperty(key)); };
            if (isFalse("repo_exists"))
                problems.add(tr("the folder is not a PianistTransformer checkout", "フォルダが PianistTransformer ではありません"));
            if (isFalse("model_config") || isFalse("model_weights") || isFalse("model_generation_config"))
                problems.add(tr("the model is not downloaded (models/sft)", "モデルがダウンロードされていません（models/sft）"));
            if (isFalse("imports_ok"))
                problems.add(tr("Python is missing libraries such as torch/transformers",
                                "Python に torch や transformers などのライブラリが入っていません"));
            if (!problems.isEmpty())
                return tr("Not ready: ", "まだ使えません: ") + problems.joinIntoString(tr("; ", "／"))
                     + tr(". Re-run scripts/setup_ai.", "。scripts/setup_ai を実行し直してください。");
        }
    }
    if (first == "failed" || first == "check-failed")
        return (first == "failed" ? tr("AI render failed: ", "AI での加工に失敗: ")
                                  : tr("Environment check failed: ", "環境チェックで問題: "))
             + detail.getLastCharacters(300).replace("\n", " ");
    if (session.aiSettings().repoFolder.isEmpty())
        return tr("Needs a one-time setup (AI settings). Rule-based styles work without it.",
                  "初回だけセットアップが必要です（AI の設定）。ルールベースはセットアップ不要です。");
    return tr("Rendering runs outside the DAW and takes a while.", "加工は DAW の外で実行され、少し時間がかかります。");
}

void ClipPanel::pushRuleSettings()
{
    auto s = session.getRuleSettings();
    s.strength = strengthSlider.getValue() / 100.0;
    session.setRuleSettings(s);
    refreshState();
}

void ClipPanel::refreshTexts()
{
    step1Title.setText(tr("Bring in the MIDI", "元のMIDIを用意する"), juce::dontSendNotification);
    step2Title.setText(tr("Choose how it is played", "弾き方を選ぶ"), juce::dontSendNotification);
    step3Title.setText(tr("Put it back into the DAW", "DAW に戻す"), juce::dontSendNotification);
    openButton.setButtonText(tr("Open or drop a .mid file", ".mid を開く／ドロップ"));
    openButton.setTooltip(tr("You can also drag a .mid file anywhere onto this window.",
                             ".mid ファイルはこの画面のどこにドラッグしても読み込めます。"));
    changeSourceButton.setButtonText(tr("Start over", "やり直す"));
    sourceHelp.setText(tr("Studio One-based DAWs cannot drag a part straight into a plugin: export it as a .mid "
                          "file first, or put the part on this track and capture it while playing.",
                          "DAW のパートはプラグインへ直接ドラッグできないことが多いので、.mid に書き出してから読み込むか、"
                          "パートをこのトラックに置いて再生しながら取り込みます。"),
                       juce::dontSendNotification);

    ruleTab.setButtonText(tr("Rule-based (instant)", "ルールベース（すぐ反映）"));
    aiTab.setButtonText(tr("AI: Pianist Transformer", "AI: Pianist Transformer"));
    const char* styleEn[] = { "Classical", "Pop", "Jazz" };
    const char* styleJa[] = { "クラシック", "ポップ", "ジャズ" };
    for (size_t i = 0; i < styleButtons.size(); ++i)
        styleButtons[i].setButtonText(tr(styleEn[i], styleJa[i]));
    strengthLabel.setText(tr("Expression", "表情の強さ"), juce::dontSendNotification);
    rerollButton.setButtonText(tr("Try another take", "別のテイク"));
    rerollButton.setTooltip(tr("Same style, different random variation.", "同じスタイルで、揺らぎの出方を変えます。"));
    aiStrengthLabel.setText(tr("AI amount", "AI の反映度"), juce::dontSendNotification);
    pedalLabel.setText(tr("Pedal", "ペダル"), juce::dontSendNotification);
    pedalBox.clear(juce::dontSendNotification);
    pedalBox.addItem(tr("AI decides", "AI に任せる"), 1);
    pedalBox.addItem(tr("Keep original", "元のまま"), 2);
    pedalBox.addItem(tr("No pedal", "ペダルなし"), 3);
    aiSettingsButton.setButtonText(tr("AI settings...", "AI の設定..."));

    settingsTitle.setText(tr("AI settings (this computer only)", "AI の設定（このパソコンだけに保存）"),
                          juce::dontSendNotification);
    repoLabel.setText(tr("PianistTransformer folder", "PianistTransformer フォルダ"), juce::dontSendNotification);
    pythonLabel.setText(tr("Python (optional)", "Python（任意）"), juce::dontSendNotification);
    deviceLabel.setText(tr("Device", "計算に使う装置"), juce::dontSendNotification);
    repoBrowse.setButtonText(tr("Browse...", "参照..."));
    pythonBrowse.setButtonText(tr("Browse...", "参照..."));
    checkButton.setButtonText(tr("Check environment", "環境をチェック"));
    closeSettings.setButtonText(tr("Done", "閉じる"));
    settingsHelp.setText(tr("One-time setup: run scripts/setup_ai (.sh or .ps1) from this project. It downloads the "
                            "model and installs everything into <folder>/.venv. Leave Python empty to use that.",
                            "初回だけ、このプロジェクトの scripts/setup_ai（.sh または .ps1）を実行します。モデルの"
                            "ダウンロードと、<フォルダ>/.venv へのインストールを行います。Python 欄は空欄のままで構いません。"),
                         juce::dontSendNotification);

    saveButton.setButtonText(tr("Save as...", "名前を付けて保存..."));
    revealButton.setButtonText(tr("Show file", "ファイルの場所を開く"));
    preview.setEmptyText(tr("The before/after preview appears here.", "ここに加工前と加工後の比較が表示されます。"));
    refreshState();
}

void ClipPanel::refreshState()
{
    const auto capture = processor.getCaptureState();
    const bool capturing = capture != ExpressiveMidiPlayerProcessor::captureIdle;
    const bool hasSource = session.hasSource();
    const bool ai = session.getEngine() == ClipSession::Engine::ai;

    // step 1
    openButton.setVisible(!hasSource && !capturing);
    captureButton.setVisible(!hasSource);
    changeSourceButton.setVisible(hasSource);
    captureButton.setButtonText(capturing ? tr("Cancel capture", "取り込みをキャンセル")
                                          : tr("Capture from this track", "このトラックから取り込む"));
    juce::String info;
    if (hasSource) info = sourceSummary();
    else if (capture == ExpressiveMidiPlayerProcessor::captureArmed)
        info = tr("Waiting: press Play in the DAW. Capture ends when you press Stop (or at a loop jump).",
                  "待機中: DAW で再生してください。停止（またはループで戻った時点）で取り込みが終わります。");
    else if (capture == ExpressiveMidiPlayerProcessor::captureRecording
             || capture == ExpressiveMidiPlayerProcessor::captureStopping)
        info = tr("Capturing... ", "取り込み中… ") + juce::String(processor.getCapturedNotes())
             + tr(" notes", " ノート") + "  ·  " + formatNumber(processor.getCaptureBeats(), 1) + tr(" beats", " 拍");
    sourceInfo.setText(info, juce::dontSendNotification);
    sourceInfo.setVisible(hasSource || capturing);
    sourceHelp.setVisible(!hasSource && !capturing);

    // step 2
    ruleTab.setToggleState(!ai, juce::dontSendNotification);
    aiTab.setToggleState(ai, juce::dontSendNotification);
    const auto rule = session.getRuleSettings();
    for (size_t i = 0; i < styleButtons.size(); ++i)
        styleButtons[i].setToggleState(static_cast<size_t>(rule.style) == i, juce::dontSendNotification);
    strengthSlider.setValue(rule.strength * 100.0, juce::dontSendNotification);
    aiStrengthSlider.setValue(session.getAiStrength() * 100.0, juce::dontSendNotification);
    pedalBox.setSelectedId(static_cast<int>(session.getAiPedal()) + 1, juce::dontSendNotification);
    strengthHelp.setText(rule.strength <= 0.0
        ? tr("0% = the MIDI is unchanged.", "0% = MIDI をそのまま出力します。")
        : tr("Chords move together; accents follow the bar and a 4-bar phrase arc. Pedal is unchanged.",
             "和音はまとめて動きます。強弱は拍子と4小節のフレーズに沿います。ペダルは変えません。"),
        juce::dontSendNotification);
    runAiButton.setButtonText(session.isAiRunning() ? tr("Cancel", "キャンセル") : tr("Render with AI", "AI で加工"));
    runAiButton.setEnabled(hasSource || session.isAiRunning());
    aiStatus.setText(aiStatusText(), juce::dontSendNotification);
    aiStatus.setColour(juce::Label::textColourId, session.aiStatusIsError() ? colours::error
                                                 : session.isAiResultStale() ? colours::warning : colours::dim);

    // step 3
    const bool result = session.hasResult();
    preview.setClips(hasSource ? &session.getSource() : nullptr, result ? &session.getResult() : nullptr);
    juce::String resultText = message.isNotEmpty() ? message : resultSummary();
    resultInfo.setText(resultText, juce::dontSendNotification);
    resultInfo.setColour(juce::Label::textColourId, message.isNotEmpty() && messageIsError ? colours::error
                                                  : message.isNotEmpty() ? colours::ok : colours::text);
    dragHandle.setActive(result);
    if (!hasSource)
        dragHandle.setText(tr("Drag the result to your piano track", "結果をピアノのトラックへドラッグ"),
                           tr("Available after steps 1 and 2.", "手順1と2のあとで使えます。"));
    else if (!result)
        dragHandle.setText(tr("Drag the result to your piano track", "結果をピアノのトラックへドラッグ"),
                           tr("Press \"Render with AI\" first.", "先に「AI で加工」を押してください。"));
    else
    {
        const auto& src = session.getSource();
        const bool captured = session.getSourceKind() == ClipSession::SourceKind::captured;
        dragHandle.setText(tr("Drag this onto your piano track", "ここをつかんで、ピアノのトラックへドラッグ"),
                           captured ? tr("Drop it at the start of bar ", "") + barText(src.originBeat)
                                          + tr(".", " 小節目の頭に置いてください。")
                                    : tr("Drop it where the original part starts.", "元のパートと同じ位置に置いてください。"));
    }
    placementHelp.setText(tr("Then mute the original part. To change the result, adjust step 2 and drag again.",
                             "置いたら元のパートはミュートします。直したい時は手順2を変えて、もう一度ドラッグします。"),
                          juce::dontSendNotification);
    saveButton.setEnabled(result);
    revealButton.setEnabled(result);

    // AI settings
    juce::String check;
    const auto first = session.getAiStatus().upToFirstOccurrenceOf("\n", false, false);
    if (session.isCheckRunning()) check = aiStatusText();
    else if (first == "check-ok" || first == "check-failed" || first == "no-module" || first == "start-failed")
        check = aiStatusText();
    const auto python = session.aiSettings().resolvedPython();
    if (check.isEmpty() && session.aiSettings().repoFolder.isNotEmpty())
        check = (python.existsAsFile() ? tr("Python found: ", "Python: ") : tr("Python not found: ", "Python が見つかりません: "))
              + python.getFullPathName();
    checkStatus.setText(check, juce::dontSendNotification);
    checkStatus.setColour(juce::Label::textColourId, session.aiStatusIsError() || (!python.existsAsFile()
                                                         && session.aiSettings().repoFolder.isNotEmpty())
                                                       ? colours::warning : colours::dim);

    // visibility of the step 2/3 widgets vs. the AI settings sheet
    const bool main = !settingsVisible;
    for (auto* c : std::initializer_list<juce::Component*> { &step2Title, &ruleTab, &aiTab, &step3Title, &preview,
                                                             &resultInfo, &dragHandle, &placementHelp, &saveButton,
                                                             &revealButton })
        c->setVisible(main);
    for (auto& b : styleButtons) b.setVisible(main && !ai);
    for (auto* c : std::initializer_list<juce::Component*> { &strengthLabel, &strengthSlider, &rerollButton, &strengthHelp })
        c->setVisible(main && !ai);
    for (auto* c : std::initializer_list<juce::Component*> { &aiStrengthLabel, &aiStrengthSlider, &pedalLabel, &pedalBox,
                                                             &runAiButton, &aiSettingsButton, &aiStatus })
        c->setVisible(main && ai);
    for (auto* c : std::initializer_list<juce::Component*> { &settingsTitle, &repoLabel, &repoField, &repoBrowse,
                                                             &pythonLabel, &pythonField, &pythonBrowse, &deviceLabel,
                                                             &deviceBox, &checkButton, &closeSettings, &settingsHelp,
                                                             &checkStatus })
        c->setVisible(settingsVisible);
    resized();
    repaint();
}

void ClipPanel::paint(juce::Graphics& g)
{
    const auto drawCard = [&g](juce::Rectangle<int> r, int number, bool done)
    {
        if (r.isEmpty()) return;
        g.setColour(colours::panel);
        g.fillRoundedRectangle(r.toFloat(), 7.0f);
        g.setColour(colours::panelEdge);
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 7.0f, 1.0f);
        if (number > 0)
        {
            auto badge = juce::Rectangle<float>(static_cast<float>(r.getX() + 12), static_cast<float>(r.getY() + 11),
                                                22.0f, 22.0f);
            g.setColour(done ? colours::ok : colours::accent);
            g.fillEllipse(badge);
            g.setColour(colours::background);
            g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
            g.drawText(juce::String(number), badge, juce::Justification::centred);
        }
    };
    const bool capturing = processor.getCaptureState() != ExpressiveMidiPlayerProcessor::captureIdle;
    drawCard(card1, 1, session.hasSource());
    if (settingsVisible)
        drawCard(card2.getUnion(card3), 0, false);
    else
    {
        drawCard(card2, 2, session.hasResult());
        drawCard(card3, 3, false);
    }
    if (capturing)
    {
        // pulsing record dot next to the capture info
        const bool rec = processor.getCaptureState() != ExpressiveMidiPlayerProcessor::captureArmed;
        const auto phase = (juce::Time::getMillisecondCounter() / 400) % 2 == 0;
        g.setColour(rec ? colours::record.withAlpha(phase ? 1.0f : 0.45f) : colours::warning);
        const auto r = sourceInfo.getBounds();
        g.fillEllipse(static_cast<float>(r.getX() - 16), static_cast<float>(r.getY() + 4), 10.0f, 10.0f);
    }
}

void ClipPanel::resized()
{
    auto area = getLocalBounds();
    const bool compactSource = session.hasSource();
    card1 = area.removeFromTop(compactSource ? 96 : 124);
    area.removeFromTop(10);
    card2 = area.removeFromTop(158);
    area.removeFromTop(10);
    card3 = area;

    const auto titleRow = [](juce::Rectangle<int>& c) { return c.removeFromTop(44).withTrimmedLeft(44).withTrimmedTop(4); };

    // step 1
    {
        auto c = card1.reduced(12, 0);
        auto title = titleRow(c);
        changeSourceButton.setBounds(title.removeFromRight(110).withSizeKeepingCentre(110, 26));
        step1Title.setBounds(title);
        c.removeFromBottom(10);
        auto row = c.removeFromTop(32);
        const int w = (row.getWidth() - 10) / 2;
        openButton.setBounds(row.removeFromLeft(w));
        row.removeFromLeft(10);
        captureButton.setBounds(row);
        sourceHelp.setBounds(c.withTrimmedTop(6));
        auto info = card1.reduced(12, 0).withTrimmedTop(44).withTrimmedBottom(8).withTrimmedLeft(32);
        if (processor.getCaptureState() != ExpressiveMidiPlayerProcessor::captureIdle && !session.hasSource())
            info = info.withTrimmedTop(40);
        sourceInfo.setBounds(info);
    }
    // step 2
    {
        auto c = card2.reduced(12, 0);
        auto title = titleRow(c);
        const int tabW = 190;
        aiTab.setBounds(title.removeFromRight(tabW).withSizeKeepingCentre(tabW, 26));
        title.removeFromRight(6);
        ruleTab.setBounds(title.removeFromRight(tabW).withSizeKeepingCentre(tabW, 26));
        step2Title.setBounds(title);

        auto rowA = c.removeFromTop(30);
        auto rowA2 = rowA;
        rerollButton.setBounds(rowA.removeFromRight(130));
        rowA.removeFromRight(16);
        const int sw = (rowA.getWidth() - 12) / 3;
        for (auto& b : styleButtons) { b.setBounds(rowA.removeFromLeft(sw)); rowA.removeFromLeft(6); }
        aiStrengthLabel.setBounds(rowA2.removeFromLeft(110));
        aiStrengthSlider.setBounds(rowA2);

        c.removeFromTop(8);
        auto rowB = c.removeFromTop(28);
        auto rowB2 = rowB;
        strengthLabel.setBounds(rowB.removeFromLeft(110));
        strengthSlider.setBounds(rowB);
        pedalLabel.setBounds(rowB2.removeFromLeft(110));
        pedalBox.setBounds(rowB2.removeFromLeft(150));
        aiSettingsButton.setBounds(rowB2.removeFromRight(130));
        rowB2.removeFromRight(8);
        runAiButton.setBounds(rowB2.removeFromRight(150));

        c.removeFromTop(6);
        strengthHelp.setBounds(c.withTrimmedLeft(110));
        aiStatus.setBounds(c);
    }
    // step 3
    {
        auto c = card3.reduced(12, 0);
        step3Title.setBounds(titleRow(c));
        c.removeFromBottom(10);
        auto bottom = c.removeFromBottom(28);
        revealButton.setBounds(bottom.removeFromRight(150));
        bottom.removeFromRight(8);
        saveButton.setBounds(bottom.removeFromRight(150));
        placementHelp.setBounds(bottom.withTrimmedRight(8));
        c.removeFromBottom(8);
        dragHandle.setBounds(c.removeFromBottom(58));
        c.removeFromBottom(6);
        resultInfo.setBounds(c.removeFromBottom(20));
        c.removeFromBottom(4);
        preview.setBounds(c);
    }
    // AI settings sheet over steps 2+3
    {
        auto c = card2.getUnion(card3).reduced(16, 12);
        auto title = c.removeFromTop(28);
        closeSettings.setBounds(title.removeFromRight(110));
        settingsTitle.setBounds(title);
        c.removeFromTop(6);
        settingsHelp.setBounds(c.removeFromTop(54));
        c.removeFromTop(8);
        const auto fieldRow = [&c](juce::Label& l, juce::Component& field, juce::Component* browse)
        {
            auto r = c.removeFromTop(28);
            l.setBounds(r.removeFromLeft(200));
            if (browse != nullptr) { browse->setBounds(r.removeFromRight(90)); r.removeFromRight(8); }
            field.setBounds(r);
            c.removeFromTop(10);
        };
        fieldRow(repoLabel, repoField, &repoBrowse);
        fieldRow(pythonLabel, pythonField, &pythonBrowse);
        fieldRow(deviceLabel, deviceBox, nullptr);
        auto r = c.removeFromTop(30);
        checkButton.setBounds(r.removeFromLeft(200));
        c.removeFromTop(8);
        checkStatus.setBounds(c);
    }
}
