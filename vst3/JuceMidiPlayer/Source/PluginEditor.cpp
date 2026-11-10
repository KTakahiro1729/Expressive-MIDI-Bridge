#include "PluginEditor.h"
#include <cmath>
#include <initializer_list>

using namespace ui;
using expressive::AiRuntime;
using expressive::AiSession;

namespace
{
constexpr int editorWidth = 600;
constexpr int editorHeight = 600;
constexpr int margin = 16;
}

ExpressiveMidiPlayerEditor::ExpressiveMidiPlayerEditor(ExpressiveMidiPlayerProcessor& p)
    : AudioProcessorEditor(&p), processor(p), session(p.getSession())
{
    tr.japanese = juce::SystemStats::getUserLanguage().startsWithIgnoreCase("ja")
               || juce::SystemStats::getDisplayLanguage().startsWithIgnoreCase("ja");

    for (auto* l : { &setupTitle, &dropTitle, &strengthLabel })
    {
        addAndMakeVisible(*l);
        styleLabel(*l, 15.0f, colours::text, true);
    }
    for (auto* l : { &setupText, &setupStatus, &dropText, &status })
    {
        addAndMakeVisible(*l);
        styleLabel(*l, 13.0f, colours::dim);
        l->setJustificationType(juce::Justification::topLeft);
    }
    dropTitle.setJustificationType(juce::Justification::centred);
    dropText.setJustificationType(juce::Justification::centredTop);
    for (auto* l : { &dropTitle, &dropText })
        l->setInterceptsMouseClicks(false, false);

    addAndMakeVisible(setupButton);
    stylePrimary(setupButton);
    setupButton.onClick = [this]
    {
        auto& rt = AiRuntime::get();
        if (rt.isInstalling()) rt.cancel();
        else rt.install();
        refresh();
    };
    addAndMakeVisible(setupBar);
    setupBar.setPercentageDisplay(false);
    setupBar.setColour(juce::ProgressBar::foregroundColourId, colours::accent);
    setupBar.setColour(juce::ProgressBar::backgroundColourId, colours::background);

    addAndMakeVisible(strengthSlider);
    strengthSlider.setRange(0, 100, 1);
    strengthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    strengthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 24);
    strengthSlider.setTextValueSuffix(" %");
    strengthSlider.setDoubleClickReturnValue(true, 100);
    strengthSlider.setColour(juce::Slider::trackColourId, colours::accent);
    strengthSlider.setColour(juce::Slider::thumbColourId, colours::text);
    strengthSlider.setValue(session.getStrength() * 100.0, juce::dontSendNotification);
    strengthSlider.onValueChange = [this] { session.setStrength(strengthSlider.getValue() / 100.0); };

    addAndMakeVisible(preview);
    addAndMakeVisible(dragHandle);
    dragHandle.prepareFile = [this]
    {
        juce::String error;
        const auto f = session.exportResult(error);
        if (!f.existsAsFile()) { message = errorText(error); refresh(); }
        return f;
    };
    dragHandle.onClick = [this]
    {
        juce::String error;
        const auto f = session.exportResult(error);
        if (f.existsAsFile()) f.revealToUser();
    };

    session.addChangeListener(this);
    setSize(editorWidth, editorHeight);
    refresh();
    startTimerHz(10);
}

ExpressiveMidiPlayerEditor::~ExpressiveMidiPlayerEditor()
{
    session.removeChangeListener(this);
}

juce::String ExpressiveMidiPlayerEditor::errorText(const juce::String& key) const
{
    if (key == "read") return tr("The file could not be read.", "ファイルを読み込めませんでした。");
    if (key == "invalid") return tr("This is not a standard MIDI file.", "標準 MIDI ファイルではありません。");
    if (key == "smpte") return tr("SMPTE-timed MIDI is not supported.", "SMPTE 形式の MIDI には対応していません。");
    if (key == "toolarge") return tr("The file is larger than 32 MB.", "ファイルが 32 MB を超えています。");
    if (key == "empty") return tr("The file contains no notes.", "ファイルにノートがありません。");
    if (key == "write") return tr("Could not write a file.", "ファイルを書き込めませんでした。");
    if (key == "noresult") return tr("There is no result yet.", "まだ結果がありません。");
    return key;
}

juce::String ExpressiveMidiPlayerEditor::stageText() const
{
    const auto& rt = AiRuntime::get();
    const auto secs = juce::String(static_cast<int>(rt.elapsedSeconds()));
    switch (rt.getStage())
    {
        case AiRuntime::Stage::tool: return tr("1/5 Downloading the installer...", "1/5 インストーラーを取得中…");
        case AiRuntime::Stage::python: return tr("2/5 Installing Python...", "2/5 Python を準備中…");
        case AiRuntime::Stage::torch: return tr("3/5 Installing PyTorch (largest step)... ", "3/5 PyTorch を準備中（一番大きい手順）… ") + secs + tr(" s", " 秒");
        case AiRuntime::Stage::libraries: return tr("4/5 Installing libraries... ", "4/5 ライブラリを準備中… ") + secs + tr(" s", " 秒");
        case AiRuntime::Stage::model: return tr("5/5 Downloading the AI model... ", "5/5 AI モデルを取得中… ") + secs + tr(" s", " 秒");
        case AiRuntime::Stage::verify: return tr("Checking...", "確認中…");
        case AiRuntime::Stage::failed:
            return tr("Setup stopped: ", "準備が止まりました: ") + rt.getFailure()
                 + tr(" Check the internet connection and try again.", " インターネット接続を確認して、もう一度押してください。");
        case AiRuntime::Stage::cancelled: return tr("Cancelled.", "キャンセルしました。");
        case AiRuntime::Stage::notInstalled:
        case AiRuntime::Stage::ready:
        default: return {};
    }
}

void ExpressiveMidiPlayerEditor::refresh()
{
    auto& rt = AiRuntime::get();
    const bool ready = !rt.isInstalling() && rt.isReady();
    aiReady = ready;
    const bool installing = rt.isInstalling();

    // AI setup card
    for (auto* c : std::initializer_list<juce::Component*> { &setupTitle, &setupText, &setupStatus, &setupButton })
        c->setVisible(!aiReady);
    setupBar.setVisible(!aiReady && installing);
    setupTitle.setText(tr("Set up the AI (first time only)", "AI の準備（初回のみ）"), juce::dontSendNotification);
    setupText.setText(tr("Downloads Python, PyTorch and the Pianist Transformer model into a private folder on this "
                         "computer. Several hundred MB; takes a few minutes. Nothing else to install.",
                         "Python・PyTorch・Pianist Transformer のモデルを、このパソコン専用のフォルダにダウンロードします。"
                         "数百 MB・数分かかります。ほかに入れるものはありません。"),
                      juce::dontSendNotification);
    setupButton.setButtonText(installing ? tr("Cancel", "キャンセル")
                            : rt.getStage() == AiRuntime::Stage::failed ? tr("Try again", "もう一度")
                            : tr("Set up the AI", "AI を準備する"));
    setupStatus.setText(stageText(), juce::dontSendNotification);
    setupStatus.setColour(juce::Label::textColourId,
                          rt.getStage() == AiRuntime::Stage::failed ? colours::error : colours::dim);
    setupProgress = rt.getStage() == AiRuntime::Stage::tool ? rt.getProgress() : -1.0;

    // drop zone
    if (session.hasSource())
    {
        const auto& src = session.getSource();
        const double bars = std::ceil(src.lastBeat() / (src.beatsPerBar > 0 ? src.beatsPerBar : 4.0));
        dropTitle.setText(session.getSourceName() + ".mid", juce::dontSendNotification);
        dropText.setText(juce::String(static_cast<int>(src.notes.size())) + tr(" notes, about ", " ノート・約 ")
                         + juce::String(static_cast<int>(bars)) + tr(" bars  -  drop another .mid to replace",
                                                                      " 小節　（別の .mid をドロップで入れ替え）"),
                         juce::dontSendNotification);
    }
    else
    {
        dropTitle.setText(tr("Drop a .mid here (or click)", "ここに .mid をドロップ（クリックでも選べます）"),
                          juce::dontSendNotification);
        dropText.setText(tr("Fender Studio Pro: drag the piano part into the Browser and choose \"MIDI file\".",
                            "Fender Studio Pro では、ピアノのパートをブラウザへドラッグし「MIDI ファイル」を選ぶと .mid になります。"),
                         juce::dontSendNotification);
    }

    strengthLabel.setText(tr("Expression", "表情の強さ"), juce::dontSendNotification);
    strengthSlider.setTooltip(tr("100% = the AI performance; 0% = the original timing and dynamics.",
                                 "100% = AI の演奏そのまま、0% = 元の MIDI のまま。"));

    // preview + status
    const bool result = session.hasResult();
    preview.setClips(session.hasSource() ? &session.getSource() : nullptr, result ? &session.getResult() : nullptr);
    preview.setEmptyText(tr("The original (grey) and the AI performance (blue) appear here.",
                            "ここに元の MIDI（灰色）と AI の演奏（青）が表示されます。"));
    juce::String text;
    auto colour = colours::dim;
    switch (session.getStatus())
    {
        case AiSession::Status::noSource: break;
        case AiSession::Status::needsSetup:
            text = AiRuntime::get().isInstalling()
                 ? tr("The file will be processed automatically when the AI is ready.",
                      "AI の準備が終わると、自動でこのファイルを処理します。")
                 : tr("Press \"Set up the AI\" above; the file will then be processed automatically.",
                      "上の「AI を準備する」を押すと、準備のあと自動で処理します。");
            colour = colours::warning;
            break;
        case AiSession::Status::rendering:
            text = tr("The AI is performing the piece... ", "AI が演奏を考えています… ")
                 + juce::String(static_cast<int>(session.jobElapsedSeconds()))
                 + tr(" s (a few minutes on CPU)", " 秒（CPU だと数分かかることがあります）");
            break;
        case AiSession::Status::blending: text = tr("Finishing...", "仕上げ中…"); break;
        case AiSession::Status::ready:
            text = tr("Done. Drag the box below onto the piano track.", "できました。下の枠をピアノのトラックへドラッグしてください。");
            colour = colours::ok;
            break;
        case AiSession::Status::failed:
            text = tr("The AI could not process this file: ", "AI で処理できませんでした: ") + session.getFailure();
            colour = colours::error;
            break;
    }
    if (message.isNotEmpty()) { text = message; colour = colours::error; }
    status.setText(text, juce::dontSendNotification);
    status.setColour(juce::Label::textColourId, colour);

    dragHandle.setActive(result);
    dragHandle.setText(tr("Drag this onto the piano track", "ここをつかんで、ピアノのトラックへドラッグ"),
                       result ? tr("Place it where the original part starts and mute the original.  Click: show the file.",
                                   "元のパートと同じ位置に置き、元のパートはミュート。クリックでファイルの場所を開きます。")
                              : tr("Available when the AI has finished.", "AI の処理が終わると使えます。"));
    resized();
    repaint();
}

void ExpressiveMidiPlayerEditor::timerCallback()
{
    // Installation progress, elapsed seconds and readiness change continuously.
    refresh();
}

void ExpressiveMidiPlayerEditor::openChooser()
{
    chooser = std::make_unique<juce::FileChooser>(tr("Choose a piano MIDI file", "ピアノの MIDI ファイルを選択"),
                                                  juce::File{}, "*.mid;*.midi");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc)
                         {
                             if (fc.getResult().existsAsFile()) loadFile(fc.getResult());
                         });
}

void ExpressiveMidiPlayerEditor::loadFile(const juce::File& file)
{
    juce::String error;
    message = session.loadFile(file, processor.getHostBpm(), error) ? juce::String() : errorText(error);
    refresh();
}

void ExpressiveMidiPlayerEditor::mouseUp(const juce::MouseEvent& e)
{
    if (dropArea.contains(e.getPosition())) openChooser();
}

bool ExpressiveMidiPlayerEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi")) return true;
    return false;
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

void ExpressiveMidiPlayerEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);
    g.setColour(colours::text);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("Expressive MIDI Bridge", margin, 12, getWidth() - 2 * margin, 26, juce::Justification::centredLeft);

    if (!aiReady)
    {
        g.setColour(colours::panel);
        g.fillRoundedRectangle(setupArea.toFloat(), 7.0f);
        g.setColour(colours::accent.withAlpha(0.6f));
        g.drawRoundedRectangle(setupArea.toFloat().reduced(0.5f), 7.0f, 1.2f);
    }

    const auto drop = dropArea.toFloat();
    g.setColour(dragHover ? colours::accent.withAlpha(0.15f) : colours::panel);
    g.fillRoundedRectangle(drop, 7.0f);
    drawDashedBox(g, drop.reduced(1.0f), dragHover ? colours::accent : colours::panelEdge, 7.0f);
}

void ExpressiveMidiPlayerEditor::resized()
{
    auto area = getLocalBounds().reduced(margin, 0);
    area.removeFromTop(48);

    if (!aiReady)
    {
        const bool hasStatus = setupStatus.getText().isNotEmpty() || AiRuntime::get().isInstalling();
        setupArea = area.removeFromTop(hasStatus ? 132 : 98);
        auto c = setupArea.reduced(14, 10);
        auto top = c.removeFromTop(28);
        setupButton.setBounds(top.removeFromRight(150));
        setupTitle.setBounds(top);
        c.removeFromTop(4);
        setupText.setBounds(c.removeFromTop(36));
        c.removeFromTop(4);
        auto row = c.removeFromTop(36);
        if (setupBar.isVisible())
            setupBar.setBounds(row.removeFromRight(150).withSizeKeepingCentre(150, 14));
        setupStatus.setBounds(row.withTrimmedRight(10));
        area.removeFromTop(12);
    }
    else
        setupArea = {};

    dropArea = area.removeFromTop(session.hasSource() ? 64 : 84);
    auto d = dropArea.reduced(14, 8);
    dropTitle.setBounds(d.removeFromTop(session.hasSource() ? 24 : 34));
    dropText.setBounds(d);
    area.removeFromTop(12);

    auto row = area.removeFromTop(30);
    strengthLabel.setBounds(row.removeFromLeft(120));
    strengthSlider.setBounds(row);
    area.removeFromTop(12);

    area.removeFromBottom(margin);
    dragHandle.setBounds(area.removeFromBottom(62));
    area.removeFromBottom(8);
    status.setBounds(area.removeFromBottom(36));
    area.removeFromBottom(4);
    preview.setBounds(area);
}
