Expressive MIDI Bridge
======================

ピアノの MIDI に、AI（Pianist Transformer）で人間らしい表情を付ける VST3 プラグインです。
音は加工しません。結果は普通の MIDI として、いつものピアノのトラックに戻します。

■ インストール（1回だけ）
1. この zip の中の「Expressive MIDI Bridge.vst3」フォルダを、フォルダごと次の場所にコピーします。
     Windows: C:\Program Files\Common Files\VST3
     macOS  : /Library/Audio/Plug-Ins/VST3
2. Fender Studio Pro を起動します。一覧に出ない時は
     Options（macOS は Preferences）→ Locations → VST Plug-ins →「Reset Blocklist」を押して再起動。
3. ピアノのトラックのインサートに「Expressive MIDI Bridge」を挿して開き、
   「AI を準備する」を押します。初回だけ、数百 MB をダウンロードします（数分）。

■ 使い方
1. ピアノのパートをブラウザへドラッグし、ポップアップで「MIDI ファイル」を選びます
   （初期値は Musicloop なので切り替えてください）。
2. できた .mid をプラグインの画面にドロップします。AI が自動で演奏を作ります。
3. 「表情の強さ」を調整し、下の枠をつかんでピアノのトラックへドラッグします。
   元のパートと同じ位置に置き、元のパートはミュートします。

■ AI のファイル
- 保存先  Windows: %LOCALAPPDATA%\ExpressiveMidiBridge\ai
          macOS  : ~/Library/Application Support/ExpressiveMidiBridge/ai
- 中身    uv（Astral）、Python 3.11、PyTorch（CPU 版）、transformers、miditoolkit、
          Pianist Transformer のモデル（yhj137/pianist-transformer-rendering, Apache-2.0）
- 消す時はこのフォルダを削除するだけです。プラグインは次に開いた時にもう一度準備できます。
- ドラッグした MIDI は「ドキュメント/Expressive MIDI Bridge」にも残ります。

■ macOS の注意
このプラグインは署名されていません。読み込まれない時はターミナルで次を実行してください。
  xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/Expressive MIDI Bridge.vst3"

--------------------------------------------------------------------------------
English
1. Copy "Expressive MIDI Bridge.vst3" to C:\Program Files\Common Files\VST3 (Windows) or
   /Library/Audio/Plug-Ins/VST3 (macOS) and restart the DAW.
2. Insert "Expressive MIDI Bridge" on the piano track, open it, press "Set up the AI" once.
3. Export the piano part as a .mid (Fender Studio Pro: drag the part into the Browser and
   choose "MIDI file"), drop it on the plugin, then drag the result back onto the track.
The AI files live in the folder listed above; delete it to uninstall the AI.
Project: https://github.com/KTakahiro1729/Expressive-MIDI-Bridge
