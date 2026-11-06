#include "PluginEditor.h"
#include <cmath>
#include <initializer_list>

// NOTE: this file contains UTF-8 Japanese string literals. MSVC is built with /utf-8
// (see CMakeLists.txt); GCC/Clang read UTF-8 sources by default.

namespace
{
namespace colours
{
const juce::Colour background { 0xff1e2127 };
const juce::Colour panel      { 0xff282c34 };
const juce::Colour panelEdge  { 0xff3a404b };
const juce::Colour text       { 0xffe6e6e6 };
const juce::Colour dim        { 0xff9aa3b0 };
const juce::Colour accent     { 0xff4fa3ff };
const juce::Colour ok         { 0xff4cd98a };
const juce::Colour warning    { 0xffffc05c };
const juce::Colour error      { 0xffff6b6b };
const juce::Colour lampOff    { 0xff444a55 };
}

constexpr int editorWidth = 600;
constexpr int editorHeight = 520;
constexpr int margin = 14;
constexpr std::uint32_t lampHoldMs = 180;
constexpr std::uint32_t silenceWarningMs = 2500;

constexpr double defaultStrength = 65.0, defaultTiming = 16.0, defaultVelocity = 12.0;

juce::Colour toneColour(int tone)
{
    switch (tone)
    {
        case 1: return colours::ok;
        case 2: return colours::warning;
        case 3: return colours::error;
        default: return colours::dim;
    }
}

void styleLabel(juce::Label& label, float size, juce::Colour colour, bool bold = false)
{
    label.setFont(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
    label.setColour(juce::Label::textColourId, colour);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setMinimumHorizontalScale(1.0f);
}

juce::String formatNumber(double v, int decimals)
{
    return juce::String(v, decimals).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
}
}

const std::array<ExpressiveMidiPlayerEditor::Preset, 4> ExpressiveMidiPlayerEditor::presets {{
    { "Off (bypass)", "オフ（そのまま）", 0.0,   16.0, 12 },
    { "Subtle",       "控えめ",          35.0,   8.0,  6 },
    { "Natural",      "自然",            65.0,  16.0, 12 },
    { "Strong",       "強め",           100.0,  30.0, 20 },
}};

ExpressiveMidiPlayerEditor::ExpressiveMidiPlayerEditor(ExpressiveMidiPlayerProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    const auto language = processor.getUiLanguage();
    japanese = language == 1
            || (language < 0 && juce::SystemStats::getUserLanguage().startsWithIgnoreCase("ja"));

    for (auto* b : { &languageButton, &guideButton, &liveTab, &fileTab, &loadButton, &reloadButton })
        addAndMakeVisible(*b);

    languageButton.onClick = [this]
    {
        japanese = !japanese;
        processor.setUiLanguage(japanese ? 1 : 0);
        refreshTexts();
    };

    guideButton.setClickingTogglesState(true);
    guideButton.setColour(juce::TextButton::buttonOnColourId, colours::accent.darker(0.3f));
    guideButton.onClick = [this]
    {
        guideVisible = guideButton.getToggleState();
        refreshVisibility();
    };

    for (auto* tab : { &liveTab, &fileTab })
    {
        tab->setRadioGroupId(1);
        tab->setClickingTogglesState(true);
        tab->setColour(juce::TextButton::buttonOnColourId, colours::accent.darker(0.35f));
        tab->setColour(juce::TextButton::buttonColourId, colours::panel);
    }
    liveTab.onClick = [this] { if (liveTab.getToggleState()) selectMode(0); };
    fileTab.onClick = [this] { if (fileTab.getToggleState()) selectMode(1); };

    addAndMakeVisible(hintLabel);
    styleLabel(hintLabel, 14.0f, colours::dim);
    hintLabel.setJustificationType(juce::Justification::topLeft);

    // ---- Live mode controls ----
    addAndMakeVisible(presetLabel);
    styleLabel(presetLabel, 14.0f, colours::text, true);
    for (size_t i = 0; i < presetButtons.size(); ++i)
    {
        auto& button = presetButtons[i];
        addAndMakeVisible(button);
        button.setColour(juce::TextButton::buttonOnColourId, colours::accent.darker(0.35f));
        button.onClick = [this, i] { applyPreset(presets[i]); };
    }

    for (auto* l : { &strengthLabel, &timingLabel, &velocityLabel })
    {
        addAndMakeVisible(*l);
        styleLabel(*l, 14.0f, colours::text, true);
    }
    for (auto* l : { &strengthHelp, &timingHelp, &velocityHelp, &liveFooter })
    {
        addAndMakeVisible(*l);
        styleLabel(*l, 12.5f, colours::dim);
    }

    setupSlider(strengthSlider, 0, 100, 1, defaultStrength, " %");
    strengthSlider.setValue(processor.getStrength() * 100, juce::dontSendNotification);
    strengthSlider.onValueChange = [this]
    {
        processor.setStrength(strengthSlider.getValue() / 100.0);
        refreshDescriptions();
        refreshPresetHighlight();
    };
    setupSlider(timingSlider, 0, 40, 1, defaultTiming, " ms");
    timingSlider.setValue(processor.getTimingMs(), juce::dontSendNotification);
    timingSlider.onValueChange = [this]
    {
        processor.setTimingMs(timingSlider.getValue());
        refreshDescriptions();
        refreshPresetHighlight();
    };
    setupSlider(velocitySlider, 0, 32, 1, defaultVelocity, "");
    velocitySlider.setValue(processor.getVelocityRange(), juce::dontSendNotification);
    velocitySlider.onValueChange = [this]
    {
        processor.setVelocityRange(static_cast<int>(velocitySlider.getValue()));
        refreshDescriptions();
        refreshPresetHighlight();
    };

    // ---- MIDI-file mode controls ----
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>(tr("Choose a performance MIDI file",
                                                         "演奏MIDIファイルを選択"),
                                                      juce::File{}, "*.mid;*.midi");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& picker)
            {
                const auto file = picker.getResult();
                if (file.existsAsFile()) loadFile(file);
            });
    };
    reloadButton.onClick = [this]
    {
        juce::String error;
        if (processor.reloadLastMidi(error))
            showMessage(tr("Reloaded from disk: ", "ディスクから再読み込みしました: ")
                        + processor.loadedFileName(), Tone::ok);
        else
            showMessage(tr("Reload failed: ", "再読み込みに失敗: ") + error, Tone::error);
        refreshDescriptions();
    };
    for (auto* l : { &fileInfo, &offsetLabel, &offsetHelp, &dropHint })
        addAndMakeVisible(*l);
    styleLabel(fileInfo, 14.0f, colours::text);
    fileInfo.setJustificationType(juce::Justification::topLeft);
    styleLabel(offsetLabel, 14.0f, colours::text, true);
    styleLabel(offsetHelp, 12.5f, colours::dim);
    styleLabel(dropHint, 12.5f, colours::dim);
    dropHint.setJustificationType(juce::Justification::centred);

    setupSlider(offsetSlider, 0.0, 512.0, 0.25, 0.0, "");
    offsetSlider.setValue(processor.getStartBeats(), juce::dontSendNotification);
    offsetSlider.onValueChange = [this]
    {
        processor.setStartBeats(offsetSlider.getValue());
        refreshDescriptions();
    };

    // ---- Setup guide ----
    addChildComponent(guide);
    guide.setMultiLine(true, true);
    guide.setReadOnly(true);
    guide.setCaretVisible(false);
    guide.setScrollbarsShown(true);
    guide.setFont(juce::FontOptions(14.0f));
    guide.setColour(juce::TextEditor::backgroundColourId, colours::panel);
    guide.setColour(juce::TextEditor::outlineColourId, colours::panelEdge);
    guide.setColour(juce::TextEditor::textColourId, colours::text);

    liveTab.setToggleState(isLive(), juce::dontSendNotification);
    fileTab.setToggleState(!isLive(), juce::dontSendNotification);

    const auto activity = processor.getActivity();
    lastIn = activity.midiIn;
    lastOut = activity.midiOut;

    setSize(editorWidth, editorHeight);
    refreshTexts();
    startTimerHz(20);
}

ExpressiveMidiPlayerEditor::~ExpressiveMidiPlayerEditor()
{
    stopTimer();
}

juce::String ExpressiveMidiPlayerEditor::tr(const char* en, const char* ja) const
{
    return japanese ? juce::String::fromUTF8(ja) : juce::String(en);
}

void ExpressiveMidiPlayerEditor::setupSlider(juce::Slider& slider, double min, double max, double step,
                                             double defaultValue, const juce::String& suffix)
{
    addAndMakeVisible(slider);
    slider.setRange(min, max, step);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 24);
    slider.setTextValueSuffix(suffix);
    slider.setDoubleClickReturnValue(true, defaultValue);
    slider.setColour(juce::Slider::trackColourId, colours::accent);
    slider.setColour(juce::Slider::thumbColourId, colours::text);
}

void ExpressiveMidiPlayerEditor::selectMode(int mode)
{
    processor.setMode(mode);
    message.clear();
    refreshTexts();
}

void ExpressiveMidiPlayerEditor::applyPreset(const Preset& preset)
{
    if (preset.strength > 0.0)
    {
        timingSlider.setValue(preset.timing, juce::sendNotificationSync);
        velocitySlider.setValue(preset.velocity, juce::sendNotificationSync);
    }
    // "Off" only zeroes the strength, so the timing/velocity values are kept for later.
    strengthSlider.setValue(preset.strength, juce::sendNotificationSync);
    refreshPresetHighlight();
}

void ExpressiveMidiPlayerEditor::refreshPresetHighlight()
{
    const auto s = strengthSlider.getValue();
    for (size_t i = 0; i < presets.size(); ++i)
    {
        const auto& preset = presets[i];
        const bool match = juce::exactlyEqual(preset.strength, 0.0)
            ? juce::exactlyEqual(s, 0.0)
            : juce::exactlyEqual(s, preset.strength) && juce::exactlyEqual(timingSlider.getValue(), preset.timing)
              && static_cast<int>(velocitySlider.getValue()) == preset.velocity;
        presetButtons[i].setToggleState(match, juce::dontSendNotification);
    }
}

void ExpressiveMidiPlayerEditor::loadFile(const juce::File& file)
{
    if (isLive())
    {
        processor.setMode(1);
        refreshTexts();
    }
    juce::String error;
    if (processor.loadMidi(file, error))
        showMessage(tr("Loaded: ", "読み込みました: ") + file.getFileName(), Tone::ok);
    else
        showMessage(tr("Could not load ", "読み込めませんでした ") + file.getFileName() + ": " + error,
                    Tone::error);
    refreshDescriptions();
}

void ExpressiveMidiPlayerEditor::showMessage(const juce::String& text, Tone tone)
{
    message = text;
    messageTone = tone;
    messageUntilMs = juce::Time::getMillisecondCounter() + 5000;
    refreshStatus();
}

bool ExpressiveMidiPlayerEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi"))
            return true;
    return false;
}

void ExpressiveMidiPlayerEditor::fileDragEnter(const juce::StringArray&, int, int)
{
    dragHover = true;
    repaint();
}

void ExpressiveMidiPlayerEditor::fileDragExit(const juce::StringArray&)
{
    dragHover = false;
    repaint();
}

void ExpressiveMidiPlayerEditor::filesDropped(const juce::StringArray& files, int, int)
{
    dragHover = false;
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi"))
        {
            loadFile(juce::File(f));
            break;
        }
    repaint();
}

juce::String ExpressiveMidiPlayerEditor::fileSummary() const
{
    if (!processor.hasLoadedClip())
        return tr("No MIDI file loaded yet.", "MIDIファイルはまだ読み込まれていません。");
    const auto beats = processor.loadedLengthBeats();
    auto name = processor.loadedFileName();
    if (name.isEmpty()) name = tr("(embedded MIDI)", "（プロジェクト内に保存されたMIDI）");
    return name + "\n"
         + tr("Notes: ", "ノート数: ") + juce::String(processor.loadedNoteCount())
         + tr("   Length: ", "   長さ: ") + formatNumber(beats, 1) + tr(" beats", " 拍")
         + tr(" (about ", "（4/4 で約 ") + formatNumber(beats / 4.0, 1)
         + tr(" bars in 4/4)", " 小節）");
}

void ExpressiveMidiPlayerEditor::refreshDescriptions()
{
    const double s = strengthSlider.getValue() / 100.0;
    const double effectiveTiming = s * timingSlider.getValue();
    const int effectiveVelocity = static_cast<int>(std::lround(s * velocitySlider.getValue()));

    strengthHelp.setText(juce::exactlyEqual(s, 0.0)
        ? tr("0% = notes pass through unchanged (bypass).", "0% = ノートを一切変えずに通します（バイパス）。")
        : tr("Overall amount. Scales the two settings below.", "全体のかかり具合。下の2つの量に掛け算されます。"),
        juce::dontSendNotification);
    timingHelp.setText(tr("Delays notes randomly by up to ", "ノートをランダムに最大 ")
        + formatNumber(effectiveTiming, 1)
        + tr(" ms now (never earlier). Chords mostly stay together.",
             " ms 遅らせます（早めはしません）。和音はほぼ揃ったまま。"),
        juce::dontSendNotification);
    velocityHelp.setText(tr("Varies loudness by up to +/-", "強さ（ベロシティ）を最大 ±")
        + juce::String(effectiveVelocity)
        + tr(" now (MIDI velocity is 1-127).", " ばらつかせます（ベロシティは 1〜127）。"),
        juce::dontSendNotification);

    fileInfo.setText(fileSummary(), juce::dontSendNotification);
    const double offset = offsetSlider.getValue();
    offsetHelp.setText(tr("The file's beat 0 plays at DAW beat ", "ファイルの先頭を DAW の ")
        + formatNumber(offset, 2)
        + tr(" = bar ", " 拍目から再生 = 4/4 で ")
        + formatNumber(offset / 4.0 + 1.0, 2)
        + tr(" in 4/4. 0 = start of the song.", " 小節目。0 = 曲の頭。"),
        juce::dontSendNotification);
    reloadButton.setEnabled(processor.loadedFileName().isNotEmpty());
}

void ExpressiveMidiPlayerEditor::refreshTexts()
{
    languageButton.setButtonText(japanese ? "English" : juce::String::fromUTF8("日本語"));
    languageButton.setTooltip(tr("Switch the display language", "表示言語を切り替えます"));
    guideButton.setButtonText(tr("? Setup guide", "? 使い方"));
    guideButton.setTooltip(tr("Show step-by-step routing instructions for the DAW",
                              "DAW での接続手順を表示します"));

    liveTab.setButtonText(tr("Humanize this track's MIDI (recommended)",
                             "このトラックのMIDIを加工（おすすめ）"));
    liveTab.setTooltip(tr("Put your piano MIDI part on this track. It is reshaped in real time.",
                          "ピアノのMIDIパートをこのトラックに置くと、再生中にリアルタイムで加工します。"));
    fileTab.setButtonText(tr("Play a MIDI file (legacy)", "MIDIファイルを再生（旧方式）"));
    fileTab.setTooltip(tr("Play a .mid file rendered by the Python tool, in sync with the DAW.",
                          "Python ツールで作った .mid を DAW の再生位置に合わせて鳴らします。"));
    liveTab.setToggleState(isLive(), juce::dontSendNotification);
    fileTab.setToggleState(!isLive(), juce::dontSendNotification);

    presetLabel.setText(tr("Presets", "プリセット"), juce::dontSendNotification);
    for (size_t i = 0; i < presets.size(); ++i)
        presetButtons[i].setButtonText(tr(presets[i].en, presets[i].ja));

    strengthLabel.setText(tr("Amount", "かかり具合"), juce::dontSendNotification);
    timingLabel.setText(tr("Timing spread", "タイミングの揺れ"), juce::dontSendNotification);
    velocityLabel.setText(tr("Velocity spread", "強弱の揺れ"), juce::dontSendNotification);
    const auto resetTip = tr(" Double-click to reset.", " ダブルクリックで初期値に戻ります。");
    strengthSlider.setTooltip(tr("How strongly the humanizer acts. 0% = bypass.",
                                 "揺らぎ全体の強さ。0% でバイパス。") + resetTip);
    timingSlider.setTooltip(tr("Maximum delay at 100% amount.", "かかり具合 100% のときの最大の遅れ。")
                            + resetTip);
    velocitySlider.setTooltip(tr("Maximum velocity change at 100% amount.",
                                 "かかり具合 100% のときの最大のベロシティ変化。") + resetTip);
    liveFooter.setText(tr("Rule-based variation, not the Pianist Transformer AI. Note lengths and pedal are unchanged.",
                          "ルールベースの揺らぎです（Pianist Transformer AI ではありません）。音の長さとペダルは変えません。"),
                       juce::dontSendNotification);

    loadButton.setButtonText(tr("Load MIDI file...", "MIDIファイルを開く..."));
    loadButton.setTooltip(tr("You can also drag a .mid file onto this window.",
                             ".mid ファイルをこの画面にドラッグ＆ドロップしても読み込めます。"));
    reloadButton.setButtonText(tr("Reload", "再読み込み"));
    reloadButton.setTooltip(tr("Read the same file again after re-rendering it.",
                               "書き出し直した同じファイルを読み込み直します。"));
    offsetLabel.setText(tr("Start position (beats)", "開始位置（拍）"), juce::dontSendNotification);
    offsetSlider.setTooltip(tr("Where in the song the file starts. 4 beats = 1 bar in 4/4.",
                               "曲のどこからファイルを再生するか。4/4 では 4 拍 = 1 小節。") + resetTip);
    dropHint.setText(tr("Tip: drag & drop a .mid file anywhere on this window",
                        "ヒント: .mid ファイルをこの画面にドラッグ＆ドロップできます"),
                     juce::dontSendNotification);

    guide.setText(guideText(), false);
    refreshDescriptions();
    refreshPresetHighlight();
    refreshVisibility();
    refreshStatus();
}

void ExpressiveMidiPlayerEditor::refreshVisibility()
{
    const bool live = isLive();
    const bool showLive = live && !guideVisible;
    const bool showFile = !live && !guideVisible;
    presetLabel.setVisible(showLive);
    for (auto& b : presetButtons) b.setVisible(showLive);
    for (auto* c : std::initializer_list<juce::Component*> { &strengthLabel, &timingLabel, &velocityLabel,
                                &strengthHelp, &timingHelp, &velocityHelp, &liveFooter,
                                &strengthSlider, &timingSlider, &velocitySlider })
        c->setVisible(showLive);
    for (auto* c : std::initializer_list<juce::Component*> { &loadButton, &reloadButton, &fileInfo,
                                &offsetLabel, &offsetHelp, &dropHint, &offsetSlider })
        c->setVisible(showFile);
    guide.setText(guideText(), false);
    guide.setVisible(guideVisible);
    guideButton.setToggleState(guideVisible, juce::dontSendNotification);
    repaint();
}

juce::String ExpressiveMidiPlayerEditor::guideText() const
{
    if (isLive())
        return tr(
            "HOW TO CONNECT (live mode)\n\n"
            "1. Put your piano MIDI part on THIS track (the one with Expressive MIDI Player).\n"
            "2. Create a second instrument track and load your piano sound on it.\n"
            "3. On the piano track (Inspector, F4), set the instrument input to "
            "\"Expressive MIDI Player\" and turn input monitoring on.\n"
            "4. If the piano track also has the same MIDI part, mute that part to avoid double notes.\n"
            "5. Press Play. Both lamps (MIDI IN and MIDI OUT) above should blink.\n\n"
            "TROUBLESHOOTING\n"
            "- MIDI IN never lights: the MIDI part is not on this track, or the part/track is muted.\n"
            "- MIDI OUT lights but you hear nothing: check the piano track's input and monitoring.\n"
            "- Notes sound twice: the original MIDI part is still playing on the piano track.\n"
            "- Your DAW does not offer this plugin as an input: use the Python tool to render a .mid "
            "file and import it directly onto the piano track instead.\n\n"
            "This plugin never makes sound by itself.",
            "接続のしかた（トラックのMIDIを加工）\n\n"
            "1. ピアノのMIDIパートを「このトラック」（Expressive MIDI Player を入れたトラック）に置きます。\n"
            "2. 別のインストゥルメントトラックを作り、ピアノ音源を入れます。\n"
            "3. ピアノトラックのインスペクター（F4）で、入力を「Expressive MIDI Player」にし、"
            "モニターをオンにします。\n"
            "4. ピアノトラック側にも同じMIDIパートがある場合は、二重に鳴らないようミュートします。\n"
            "5. 再生します。上の「MIDI IN」「MIDI OUT」ランプが両方点滅すれば成功です。\n\n"
            "うまくいかない時\n"
            "・MIDI IN が点かない → MIDIパートがこのトラックにない、またはミュートされています。\n"
            "・MIDI OUT は点くのに音が出ない → ピアノトラックの入力とモニター設定を確認してください。\n"
            "・音が二重に鳴る → ピアノトラックの元のMIDIパートが鳴っています。ミュートしてください。\n"
            "・DAW の入力一覧にこのプラグインが出ない → Python ツールで .mid を書き出し、"
            "ピアノトラックに直接読み込む方法を使ってください。\n\n"
            "このプラグイン自体は音を出しません。");
    return tr(
        "HOW TO CONNECT (MIDI file mode)\n\n"
        "1. Render a performance .mid with the Python tool (use --daw-bpm with your song tempo).\n"
        "2. Click \"Load MIDI file...\" or drag the .mid onto this window.\n"
        "3. Create a second instrument track with your piano sound. Set its input to "
        "\"Expressive MIDI Player\" and turn input monitoring on.\n"
        "4. Set the start position (0 = beginning of the song) and press Play.\n"
        "5. After re-rendering the same file, press Reload.\n\n"
        "TROUBLESHOOTING\n"
        "- MIDI OUT never lights: the playhead is before the start position or after the end of the file.\n"
        "- Timing drifts: the file contains tempo changes. Render it at a fixed tempo (--daw-bpm).\n"
        "- Starting in the middle of a held note does not re-trigger it.\n\n"
        "The loaded file is stored inside the DAW project.",
        "接続のしかた（MIDIファイルを再生）\n\n"
        "1. Python ツールで演奏 .mid を書き出します（--daw-bpm に曲のテンポを指定）。\n"
        "2. 「MIDIファイルを開く...」を押すか、.mid をこの画面にドラッグします。\n"
        "3. ピアノ音源を入れた別のインストゥルメントトラックを作り、入力を"
        "「Expressive MIDI Player」に、モニターをオンにします。\n"
        "4. 開始位置を決めて（0 = 曲の頭）再生します。\n"
        "5. 同じファイルを書き出し直したら「再読み込み」を押します。\n\n"
        "うまくいかない時\n"
        "・MIDI OUT が点かない → 再生位置が開始位置より前、またはファイルの終わりより後です。\n"
        "・タイミングがずれる → ファイルにテンポ変化があります。固定テンポ（--daw-bpm）で書き出してください。\n"
        "・伸ばしている音の途中から再生すると、その音は鳴りません。\n\n"
        "読み込んだファイルは DAW のプロジェクト内に保存されます。");
}

void ExpressiveMidiPlayerEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    const auto activity = processor.getActivity();
    if (activity.midiIn != lastIn) { lastIn = activity.midiIn; lastInMs = now; everIn = true; }
    if (activity.midiOut != lastOut) { lastOut = activity.midiOut; lastOutMs = now; everOut = true; }
    const bool newInLit = everIn && now - lastInMs < lampHoldMs;
    const bool newOutLit = everOut && now - lastOutMs < lampHoldMs;
    if (newInLit != inLit || newOutLit != outLit)
    {
        inLit = newInLit;
        outLit = newOutLit;
        repaint(flowArea);
    }
    refreshStatus();
}

void ExpressiveMidiPlayerEditor::refreshStatus()
{
    const auto now = juce::Time::getMillisecondCounter();
    const auto a = processor.getActivity();

    juce::String transport;
    if (!a.hasClock)
        transport = tr("No transport information from the host yet", "DAW から再生情報がまだ届いていません");
    else
        transport = (a.playing ? tr("Playing", "再生中") : tr("Stopped", "停止中"))
                  + "   " + formatNumber(a.bpm, 2) + " BPM   "
                  + tr("beat ", "拍 ") + formatNumber(std::floor(a.ppq * 4.0) / 4.0, 2);

    juce::String hint;
    Tone tone = Tone::neutral;
    const bool recentIn = everIn && now - lastInMs < silenceWarningMs;
    const bool recentOut = everOut && now - lastOutMs < silenceWarningMs;

    if (message.isNotEmpty() && now < messageUntilMs)
    {
        hint = message;
        tone = messageTone;
    }
    else if (isLive())
    {
        if (!a.playing)
            hint = tr("Press Play in the DAW. Put the piano MIDI part on THIS track first.",
                      "DAW で再生してください。先にピアノのMIDIパートを「このトラック」に置きます。");
        else if (!recentIn)
        {
            hint = tr("No notes are arriving. Is the MIDI part on this track (not on the piano track) and unmuted?",
                      "ノートが届いていません。MIDIパートはピアノ側ではなく「このトラック」にありますか？ミュートされていませんか？");
            tone = Tone::warning;
        }
        else if (juce::exactlyEqual(strengthSlider.getValue(), 0.0))
        {
            hint = tr("Notes are passing through unchanged (amount is 0%).",
                      "ノートをそのまま通しています（かかり具合 0%）。");
            tone = Tone::ok;
        }
        else
        {
            hint = tr("MIDI is flowing. No sound? On the piano track set input = Expressive MIDI Player and monitoring on.",
                      "MIDIは流れています。音が出ない時は、ピアノトラックの入力を Expressive MIDI Player に、モニターをオンに。");
            tone = Tone::ok;
        }
    }
    else
    {
        const double start = processor.getStartBeats();
        const double end = start + processor.loadedLengthBeats();
        if (!processor.hasLoadedClip())
        {
            hint = tr("Load a .mid file with the button below, or drag it onto this window.",
                      "下のボタンか、ドラッグ＆ドロップで .mid ファイルを読み込んでください。");
            tone = Tone::warning;
        }
        else if (!a.playing)
            hint = tr("Ready. Press Play in the DAW.", "準備完了。DAW で再生してください。");
        else if (a.ppq < start)
            hint = tr("Waiting: the file starts at beat ", "待機中: ファイルは ")
                 + formatNumber(start, 2) + tr(" (bar ", " 拍目（4/4 で ")
                 + formatNumber(start / 4.0 + 1.0, 2) + tr(" in 4/4).", " 小節目）から始まります。");
        else if (a.ppq > end + 0.5)
            hint = tr("The playhead is past the end of the file.", "再生位置がファイルの終わりを過ぎています。");
        else
        {
            hint = recentOut
                ? tr("Playing the file. No sound? On the piano track set input = Expressive MIDI Player and monitoring on.",
                     "ファイルを再生中。音が出ない時は、ピアノトラックの入力を Expressive MIDI Player に、モニターをオンに。")
                : tr("Playing the file (no notes in this section).", "ファイルを再生中（この区間にはノートがありません）。");
            tone = Tone::ok;
        }
    }

    if (transport != transportText)
    {
        transportText = transport;
        repaint(statusArea);
    }
    if (hint != hintText || tone != hintTone)
    {
        hintText = hint;
        hintTone = tone;
        hintLabel.setText(hint, juce::dontSendNotification);
        hintLabel.setColour(juce::Label::textColourId,
                            tone == Tone::neutral ? colours::text : toneColour(static_cast<int>(tone)));
        repaint(statusArea);
    }
}

void ExpressiveMidiPlayerEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);

    // Header
    g.setColour(colours::text);
    g.setFont(juce::FontOptions(19.0f, juce::Font::bold));
    g.drawText("Expressive MIDI Player", margin, 10, 360, 26, juce::Justification::centredLeft);
    g.setColour(colours::warning);
    g.setFont(juce::FontOptions(13.0f));
    g.drawText(tr("Makes no sound: it sends MIDI to a separate piano track.",
                  "音は出しません。MIDIを加工して、別のピアノトラックへ送ります。"),
               margin, 36, getWidth() - 2 * margin, 20, juce::Justification::centredLeft);

    // Status panel
    g.setColour(colours::panel);
    g.fillRoundedRectangle(statusArea.toFloat(), 6.0f);
    g.setColour(colours::panelEdge);
    g.drawRoundedRectangle(statusArea.toFloat().reduced(0.5f), 6.0f, 1.0f);

    const auto a = processor.getActivity();
    auto icon = juce::Rectangle<float>(static_cast<float>(statusArea.getX() + 12),
                                       static_cast<float>(statusArea.getY() + 11), 11.0f, 11.0f);
    if (a.hasClock && a.playing)
    {
        juce::Path triangle;
        triangle.addTriangle(icon.getTopLeft(), icon.getBottomLeft(),
                             { icon.getRight(), icon.getCentreY() });
        g.setColour(colours::ok);
        g.fillPath(triangle);
    }
    else
    {
        g.setColour(colours::dim);
        g.fillRect(icon.reduced(1.0f));
    }
    g.setColour(colours::text);
    g.setFont(juce::FontOptions(13.5f));
    g.drawText(transportText, statusArea.getX() + 30, statusArea.getY() + 6,
               statusArea.getWidth() - 40, 20, juce::Justification::centredLeft);

    // Signal flow: [source + IN lamp] -> [this plugin] -> [piano track + OUT lamp]
    const bool live = isLive();
    const auto boxW = (flowArea.getWidth() - 2 * 34) / 3;
    const juce::String names[3] = {
        live ? tr("MIDI on this track", "このトラックのMIDI") : tr("Loaded .mid file", "読み込んだ .mid"),
        live ? tr("Humanize", "揺らぎを加える") : tr("Play at DAW position", "再生位置に合わせて再生"),
        tr("Your piano track", "ピアノのトラック") };
    for (int i = 0; i < 3; ++i)
    {
        auto box = juce::Rectangle<int>(flowArea.getX() + i * (boxW + 34), flowArea.getY(),
                                        boxW, flowArea.getHeight()).toFloat();
        g.setColour(i == 1 ? colours::accent.withAlpha(0.18f) : colours::background);
        g.fillRoundedRectangle(box, 5.0f);
        g.setColour(i == 1 ? colours::accent : colours::panelEdge);
        g.drawRoundedRectangle(box.reduced(0.5f), 5.0f, 1.0f);

        g.setColour(colours::text);
        g.setFont(juce::FontOptions(14.0f));
        g.drawFittedText(names[i], box.toNearestInt().reduced(6, 3).withTrimmedBottom(i == 1 ? 0 : 18),
                         juce::Justification::centred, 2);

        if (i != 1)
        {
            const bool lit = i == 0 ? inLit : outLit;
            const bool shown = i == 0 ? live : true;
            if (shown)
            {
                const auto lamp = juce::Rectangle<float>(box.getCentreX() - 34.0f, box.getBottom() - 17.0f,
                                                         10.0f, 10.0f);
                g.setColour(lit ? colours::ok : colours::lampOff);
                g.fillEllipse(lamp);
                if (lit)
                {
                    g.setColour(colours::ok.withAlpha(0.35f));
                    g.fillEllipse(lamp.expanded(3.0f));
                }
                g.setColour(colours::dim);
                g.setFont(juce::FontOptions(11.5f));
                g.drawText(i == 0 ? "MIDI IN" : "MIDI OUT", juce::Rectangle<float>(lamp.getRight() + 5.0f,
                           lamp.getY() - 3.0f, 70.0f, 16.0f), juce::Justification::centredLeft);
            }
        }
        if (i < 2)
        {
            const float y = box.getCentreY();
            const float x0 = box.getRight() + 6.0f, x1 = box.getRight() + 28.0f;
            g.setColour(colours::dim);
            g.drawArrow(juce::Line<float>(x0, y, x1, y), 2.0f, 9.0f, 9.0f);
        }
    }

    // Left accent bar next to the hint, coloured by severity.
    const auto hintBounds = hintLabel.getBounds();
    g.setColour(hintTone == Tone::neutral ? colours::panelEdge : toneColour(static_cast<int>(hintTone)));
    g.fillRect(hintBounds.getX() - 8, hintBounds.getY() + 2, 3, hintBounds.getHeight() - 4);

    if (!live && !guideVisible)
    {
        auto drop = dropHint.getBounds().toFloat();
        g.setColour(dragHover ? colours::accent : colours::panelEdge);
        const float dashes[] = { 5.0f, 4.0f };
        juce::Path outline;
        outline.addRoundedRectangle(drop, 6.0f);
        juce::Path dashed;
        juce::PathStrokeType(1.2f).createDashedStroke(dashed, outline, dashes, 2);
        g.fillPath(dashed);
    }
    if (dragHover)
    {
        g.setColour(colours::accent.withAlpha(0.12f));
        g.fillRect(getLocalBounds());
        g.setColour(colours::accent);
        g.drawRect(getLocalBounds(), 3);
    }
}

void ExpressiveMidiPlayerEditor::resized()
{
    auto area = getLocalBounds().reduced(margin, 0);
    languageButton.setBounds(getWidth() - margin - 84, 12, 84, 24);
    guideButton.setBounds(languageButton.getX() - 8 - 104, 12, 104, 24);

    area.removeFromTop(64);
    auto tabs = area.removeFromTop(36);
    liveTab.setBounds(tabs.removeFromLeft(tabs.getWidth() * 3 / 5).reduced(0, 0).withTrimmedRight(4));
    fileTab.setBounds(tabs.withTrimmedLeft(4));

    area.removeFromTop(10);
    statusArea = area.removeFromTop(150);
    auto inner = statusArea.reduced(12, 0);
    inner.removeFromTop(30);
    flowArea = inner.removeFromTop(66);
    inner.removeFromTop(8);
    hintLabel.setBounds(inner.removeFromTop(42).withTrimmedLeft(8));

    area.removeFromTop(14);
    auto content = area.withTrimmedBottom(12);
    guide.setBounds(content);

    // Live mode
    {
        auto c = content;
        auto row = c.removeFromTop(28);
        presetLabel.setBounds(row.removeFromLeft(110));
        const int w = (row.getWidth() - 3 * 6) / 4;
        for (auto& b : presetButtons)
        {
            b.setBounds(row.removeFromLeft(w));
            row.removeFromLeft(6);
        }
        c.removeFromTop(12);
        const auto layoutRow = [&c](juce::Label& name, juce::Slider& slider, juce::Label& help)
        {
            auto r = c.removeFromTop(28);
            name.setBounds(r.removeFromLeft(110));
            slider.setBounds(r);
            help.setBounds(c.removeFromTop(20).withTrimmedLeft(110));
            c.removeFromTop(10);
        };
        layoutRow(strengthLabel, strengthSlider, strengthHelp);
        layoutRow(timingLabel, timingSlider, timingHelp);
        layoutRow(velocityLabel, velocitySlider, velocityHelp);
        liveFooter.setBounds(c.removeFromBottom(20));
    }
    // MIDI-file mode
    {
        auto c = content;
        auto row = c.removeFromTop(32);
        reloadButton.setBounds(row.removeFromRight(110));
        row.removeFromRight(8);
        loadButton.setBounds(row);
        c.removeFromTop(8);
        fileInfo.setBounds(c.removeFromTop(40));
        c.removeFromTop(8);
        auto r = c.removeFromTop(28);
        offsetLabel.setBounds(r.removeFromLeft(170));
        offsetSlider.setBounds(r);
        offsetHelp.setBounds(c.removeFromTop(20).withTrimmedLeft(170));
        c.removeFromTop(12);
        dropHint.setBounds(c.removeFromTop(40));
    }
}
