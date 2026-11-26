# SUBMERGE

Ableton Move 上の Schwung で動く、完全オリジナルの Audio FX モジュールです。

入力音にリバーブやディレイをかけるのではなく、入力の輪郭・音色・アタックを消して、
**地鳴り・地下空間の残響・遠くの低周波・ゴロゴロした低域の塊・不規則にうねる暗いアンビエンス**
へ変換します。元音を残響化するのではなく、元音を「低域の環境現象」へ変換する FX です。

- 粒子・空気・高域テクスチャ（Clouds 系）とは逆方向：sub / rumble / earth / decay / dark low-frequency field
- 8 ノブ 1 ページ（ノブ 1 は必ず MIX）。トラックのスロットでも **Master FX** でも使える
- DSP は完全オリジナル実装、MIT License（作者 Hiro Ikezawa）

| ノブ | パラメータ | 表示 |
|---|---|---|
| 1 | MIX | 0–100 % |
| 2 | DEPTH | 0–100 % |
| 3 | TONE | 30–120 Hz（共振器バンクの最低モード） |
| 4 | DECAY | 0.3–25 秒 |
| 5 | MOTION | 0–100 % |
| 6 | BLUR | 0–100 % |
| 7 | FEEDBACK | 0–100 % |
| 8 | DARK | 0–100 % |

## 使い方

1. トラックのスロット（Signal Chain）の Audio FX、または Master FX（Shift+Vol+Menu）に **SUBMERGE** を入れる
2. まず MIX 50 %・DEPTH 70 % のデフォルトでキックやスネアを鳴らす
3. DEPTH を上げるほど元の楽器が分からなくなり、地鳴りだけが残る
4. FEEDBACK を 90 % 以上にすると場が長く自己維持する（100 % でも暴走しない）

画面は Schwung 標準のノブグリッド（他のモジュールと同じ見た目）です。モジュール独自の画面は持たず、
`ui_hierarchy` で 8 ノブを宣言しているので、トラックでも Master FX（Send / バスのインサートも）でも同じ画面が開きます。
ノブに触れるとヘッダに `TONE 49 HZ` / `DECAY 3.4 SEC` / `FDBK 96%` のように大きく値が出ます。

TONE は Hz、DECAY は秒をそのまま値として持っています（標準グリッドが単位付きで表示できるように）。
標準グリッドには対数カーブの指定がないため、ノブの回り方は Hz / 秒に対して直線です。

## ファイル構成

```text
schwung-submerge/
├── LICENSE                     MIT (Hiro Ikezawa)
├── README.md                   このファイル
├── THIRD_PARTY_LICENSES.md     Schwung API ヘッダ (MIT) の notice
├── module.json                 id submerge, audio_fx, chain_params 8 個
├── help.json                   本体ヘルプ (ASCII)
├── dsp/
│   ├── submerge_plugin.c       audio_fx_api_v2 アダプタ、ui_hierarchy、インスタンスプール、状態保存
│   ├── submerge_engine.c/.h    解析・励振・3 ステージの接続・出力段・スムージング
│   ├── submerge_resonator.c/.h 低域共振器バンク (8 モード)
│   ├── submerge_diffusion.c/.h BLUR: オールパス拡散 + マルチタップスミア + 高域ダンピング
│   ├── submerge_feedback.c/.h  暗いフィードバック網 (4 ライン FDN) + ガバナー
│   ├── submerge_params.h       パラメータ表 (module.json と一致をビルド時に検査)
│   └── submerge_dsp.h          一次フィルタ / biquad / ソフトクリップ / ノイズ
├── third_party/schwung/        Schwung の plugin API ヘッダ (MIT, 無改変)
├── scripts/
│   ├── build.sh                native / asan / arm64 ビルド、arm64 は dist/ に tarball
│   ├── test.sh                 全テスト (下記)
│   ├── render_demo.sh          オフライン試聴用 WAV + PNG 生成
│   ├── install.py              Schwung Manager 経由のインストール
│   ├── metadata.py             module.json → DSP 埋め込み + 整合チェック
│   ├── check_abi.py            ARM64 .so の ABI 検査
│   └── check_licenses.py       コピーレフト混入・著作権表示の検査
└── tests/
    ├── test_engine.c           エンジン / プラグイン API テスト
    ├── test_grid.mjs           実機と同じページエンジンで標準グリッドのテスト
    ├── dump_contract.py        ビルドした .so の応答をグリッドテストへ渡す
    ├── render_demo.c           デモレンダラ兼レベル/帯域プローブ
    ├── spectrogram.c           エンベロープ + 0–600 Hz スペクトログラムを PNG に
    ├── signals.h               キック / スネア / コード / ノイズ等の合成テスト信号
    └── bench_device.c          Move 実機用 CPU ベンチ (静的 aarch64 バイナリ)
```

チェインホストは `audio_fx/<id>/<id>.so` を読むので、DSP は `submerge.so` です。

## DSP 信号経路

```text
Stereo In ─┬──────────────────────────────────────────────────────── Dry ───┐
           │                                                                 │
           └─ 20 Hz HPF (2次, 入力側の最低限のローカット)                     │
                 │                                                           │
                 ├─ mono ─ 解析 ─────────────────────────────┐               │
                 │    low band   = 160 Hz LPF                │               │
                 │    env fast   = 1 ms / 30 ms              │               │
                 │    env slow   = 12 ms / 280 ms            │ excitation    │
                 │    env swell  = (40-290 ms) / 600 ms      │ (DEPTH が配合 │
                 │    transient  = fast - 1.3 x slow (>=0)   │  を決める)    │
                 │    thump      = LPF(transient)            │               │
                 │    energy     = 低域ノイズ x swell         │               │
                 │    tnoise     = 500 Hz LPF ノイズ x transient              │
                 │    sub        = 低域のゼロ交差 1/2 分周 x env              │
                 │                                           v               │
                 │                       [ 低域共振器バンク x8 ]              │
                 │                        -> 非対称ソフトサチュレーション      │
                 │                                           │               │
                 └─ direct (暗い LPF) x 0.35(1-DEPTH)^1.5 -> +               │
                                                             v               │
                                    [ BLUR: 4段オールパス + 4タップスミア     │
                                      + 2段 1次 LPF (強い高域ダンピング) ]    │
                                                             │               │
                                  ┌──────────────────────────┤               │
                                  v                          v               │
                      [ 暗いフィードバック網 4ライン ]   early (拡散済みの場)  │
                        read(変調付き) -> LPF(DARK)           │               │
                        -> 2 Hz HPF -> 共鳴強調               │               │
                        -> gain(DECAY/FEEDBACK, ピーク正規化) │               │
                        -> Hadamard -> soft clip -> write     │               │
                                  │ tail                     │               │
                                  └─────────> wet = early + tail              │
                                                  │                          │
           出力 LPF (DARK, 1.6 kHz-260 Hz) -> 22 Hz HPF (subsonic/DC)         │
           -> M/S (Side だけ 85 Hz HPF = 低域はモノ) -> 安全ニー (-0.9 dBFS~)  │
                                                  │                          │
                                      equal-power MIX <──────────────────────┘
```

重要な点：

- **Wet 経路は `Input → Reverb` ではありません。** 入力波形がそのまま Wet に入る経路は
  「direct」だけで、DEPTH 0 % で 0.35、DEPTH 100 % で 0。しかもそれも拡散網を通るので
  時間的に崩れます。DEPTH が高いと、共振器を励振するのはエンベロープ・トランジェント・
  エネルギー（ノイズ × エンベロープ）だけになり、元の楽器の波形情報は Wet に残りません。
- 励振にノイズを使うので、同じ入力でも毎回少しずつ違う「ゴロゴロ」になります（雷鳴・地鳴りの不規則さ）。
- 共振器バンクはループの外、フィードバック網はループの中、という分担です。

### 低域共振器バンク (`submerge_resonator.c`)

8 基の 2 極バンドパス共振器（RBJ の「ピーク 0 dB」型。どの周波数でも |H| ≤ 1）。

- 周波数 = TONE の基音 × 比率 `1.00 1.13 1.27 1.43 1.61 1.80 2.03 2.27`（非整数比：コード感ではなく密度）
  に固定デチューン（±0.3–0.7 %）
- TONE 下端なら 30/34/38/43/48/54/61/68 Hz、上端なら 120…272 Hz
- 減衰 (T60) = (0.35 + 0.25 × DECAY ノブ) 秒 × モードごとの係数（上のモードほど短い）。
  共振器自体の Q は抑え、長い尾はフィードバック網に任せる（純粋なサイン波の集合にしないため）。
  v0.1.0 では DECAY 秒に比例させて最大 1.8 s にしていたため、DECAY を上げると帯域が狭まり
  演奏中の音量が −11 dB 下がって「音が消える」状態でした
- 出力の重みはモードごと（DARK で上のモードを弱く）、合計で正規化。定位はモードごとに少しずつ違い、
  低いモードほど中央

### BLUR (`submerge_diffusion.c`)

左右で長さの違う 4 段の Schroeder オールパス（L: 11.3/17.9/27.1/41.3 ms、R: 12.7/19.3/29.9/37.7 ms、
低域に効く長さ）→ 4 タップのスミア（19–109 ms、タップの和 × 1/√4。v0.1.0 は平均 × 1/4 で、打ち消し合いにより BLUR 最大で −6.7 dB 下がっていた）→ 2 段の 1 次 LPF（DARK で 1.1 kHz → 150 Hz）。
オールパスは |H| = 1、スミアは遅延コピーの凸結合、LPF は ≤ 1 なので、この網はゲインを足しません。
グレインは使わず、高域を作りません。

### 暗いフィードバック網 (`submerge_feedback.c`)

4 本の遅延線（83.1 / 109.7 / 137.3 / 173.9 ms、MOTION で ±3.5 % 揺れる）を直交 Hadamard 行列で混ぜる FDN。
ループ内の 1 パスごとに：

1. 分数遅延の読み出し（線形補間、長さはブロック内でランプ）
2. 1 次 LPF（DARK で 1.4 kHz → 220 Hz、ラインごとに 7 % ずつずらす）— 毎パス暗くなる
3. 1 次 HPF 2 Hz（DC 蓄積防止）
4. 共鳴強調 `c·x + (1−c)·BP(x)`（BP は共振器の第 3 モード付近、Q 2）。FEEDBACK で c が 1 → 0.97 に下がり、
   高 FEEDBACK では共鳴帯域以外が先に消えて「共鳴の場」が自己維持する（音色がだんだん変わる）
5. ゲイン g（下記）× ガバナー
6. Hadamard → ソフトクリップ → 書き戻し（＋拡散網からの新しい入力 ×0.5）

設計途中の経緯：最初はループ内の HPF を 18 Hz にし、FDN 出力の一部を共振器バンク経由で戻していましたが、
前者は 40 Hz で 1 パス 0.8 dB、後者は 1 パスあたり取り込んだ分の約半分を捨てるため、FEEDBACK 100 % でも
数秒で消えていました。測定して上記の構成に変えています。

## 各ノブの内部マッピング

TONE は Hz、DECAY は秒をそのまま、それ以外は 0..1 で保存し、エンジン内で写像します。

| ノブ | 内部で動くもの |
|---|---|
| MIX | 等パワー：dry = cos(πm/2)、wet = sin(πm/2)。両端は厳密に 1/0（MIX 0 % はビット単位で入力と同一）。ブロック単位 30 ms スムージング＋サンプル単位のランプ |
| DEPTH | 励振の配合を「波形」から「エンベロープ」へ：low band 0.22·(1−d)^1.3+0.02、thump 1.2+5d、energy 0.5+3.5d、tnoise 1+4d、sub 0.25d、direct 0.35·(1−d)^1.5。swell のアタック 40 → 290 ms（アタック抑制）、thump の LPF 70 → 35 Hz（丸く）、BLUR に +0.25d（スミア増）。pitch shift や EQ ではなく「何で励振するか」を変える |
| TONE | 共振器バンクの基音 = TONE の値（30–120 Hz、2 オクターブ）。log 領域で 0.3 s かけて追従。FDN の共鳴強調もこれに追従。v0.1.0 の 35–65 Hz では聴いて分からなかったため広げた |
| DECAY | DECAY の値（0.3–25 秒）。FDN の g_dec = 10^(−3·L/(fs·T))（L = ライン長）で、FEEDBACK 0 % なら「1 回の励振がこの時間で −60 dB」。尾の出力ゲイン 2.5 × (1 − 0.45·n)、n は DECAY の対数上の位置 0..1（長い DECAY ほど溜まる分を少し戻す）。共振器の T60 も n で決める。共振器の T60 は 0.35–0.6 s。DECAY を最小 → 最大にしても演奏中の音量は ±1 dB 程度、止めた後の尾は +18 dB。DECAY 単体では g < 1 なので自己発振しない |
| MOTION | 28 個の不規則モジュレータ（ランダム間隔 0.35–2.75 s の sample-and-hold を 2 段の 1 次平滑化）。共振器ごとに周波数 ±2 %、レベル（「ゴ…ゴロ…グゥ」）、減衰 ±30 %、定位。FDN ライン長 ±3.5 %、共鳴強調の中心、ラインごとのゲイン配分。速さ = 0.25 + 1.5·motion。全モジュレータが独立なので同じ方向に動かない |
| BLUR | オールパス係数 0.12 → 0.70、スミア量 0.8·blur²、共振器の定位の広がり |
| FEEDBACK | g = g_dec + (0.9995 − g_dec)·(1 − (1−fb)^1.3)、共鳴強調 c = 1 − 0.03·fb。90 % で非常に長く、98 % でほぼ無限、100 % でゆっくり自己維持（テスト：100 % で 1 分後も鳴っていて −18 dB） |
| DARK | 出力 LPF 1.6 kHz → 260 Hz、拡散のダンピング 1.1 kHz → 150 Hz、ループ LPF 1.4 kHz → 220 Hz、ループのクリップ駆動 1 → 1.5、共振器後段のサチュレーション駆動 1.15 → 1.75、上のモードの重み低下、tnoise（トランジェント励振）−60 %、thump の LPF 低下。下限を 260 Hz に止めているので DARK 100 % でも低域の存在感は残る |

スムージング時定数（ブロック単位）：MIX 30 ms（＋サンプル単位ランプ）、DEPTH 100 ms、TONE 300 ms（対数）、
DECAY 500 ms、MOTION 250 ms、BLUR 150 ms、FEEDBACK 200 ms、DARK 300 ms。FDN の遅延長はブロック内で
サンプル単位にランプします。

## ステレオ

- 共振器は低いモードほど中央、上のモードほど左右へ（MOTION で揺れる）
- 拡散網は左右で長さが違い、これが中低域〜中域の広がりを作る
- 出力段で M/S に分け、**Side だけ 85 Hz 2 次 HPF**：30–80 Hz はほぼモノ、80–200 Hz は適度、
  それ以上は拡散による広がり。モノラル互換性を優先

## CPU 使用上の工夫

- 共振器はモノラル 8 基（ステレオは重みだけ）、FDN は 4 ライン、拡散は各 ch 4 段 + 4 タップ
- フィルタ係数・モジュレータ・ゲイン正規化（40 点の周波数応答評価 × 4 ライン）は **128 サンプルに 1 回**の制御レートで計算
- 遅延線は 2 のべき乗長のリングバッファ（マスクで巡回）
- `-O2`、`-ffast-math` なし（NaN/Inf ガードを残すため）、`-ffp-contract=off`
- デスクトップ (x86-64) で 1 ブロック平均 22 µs（予算 2902 µs の 0.8 %）、最悪 115 µs

Move 実機（2026-10-20、v0.2.0、通常優先度・コア 0–2 に固定して実行）：

| 設定 | 平均 / ブロック | 予算 2902 µs 比 | 最悪 |
|---|---|---|---|
| デフォルト | 171 µs | 5.9 % | 1.0 ms |
| 全ノブ最大 | 151 µs | 5.2 % | 1.0 ms |

最悪値はベンチが通常優先度のプロセスで、Move 本体のスレッドに割り込まれた時間を含みます
（処理量はブロックごとにほぼ一定の設計です）。再測定は：

```bash
scp build/bench-device-arm64 ableton@move.local:/data/UserData/
```

```bash
ssh ableton@move.local "taskset 0x7 /data/UserData/bench-device-arm64"
```

## リアルタイム安全性

- audio callback 側（`process_block` / `set_param` / `get_param`。すべて SPI コールバック上で走る）で
  malloc/free・new/delete・mutex・ファイル I/O・syscall・ブロッキング・ログを一切行いません
- インスタンスは静的プール（8 個、1 個約 460 KB、全バッファを構造体に内蔵）から原子的 CAS で確保するので、
  `create_instance` も何も確保しません
- `check_abi.py` が ARM64 の .so に malloc/calloc/free/new/delete/pthread_mutex の参照が無いことを検査

## 安全対策

| 対策 | 場所 |
|---|---|
| 入力 20 Hz HPF（2 次）— 極端な DC・超低域をループに入れない（80 Hz のような高い位置では切らない） | エンジン入口 |
| ループ内 2 Hz HPF — DC 蓄積防止 | FDN 各ライン |
| ループゲインの構造的上限：各ラインのフィルタ連鎖（LPF × HPF × 共鳴強調）のピークゲインを毎ブロック数値評価し、最もよく通る周波数でちょうど g（≤ 0.9995）になるよう正規化。Hadamard は直交、ソフトクリップは傾き ≤ 1 → どの設定でもループは増幅できない | FDN |
| ソフトクリップ（有理関数、傾き ≤ 1、±1 で飽和）をループ内に常設 | FDN、共振器後段 |
| ガバナー：ループ RMS が −9 dBFS を超えたときだけゲインを下げる（80 ms で下げ 2 s で戻す）。通常素材では作動しない | FDN |
| 出力 22 Hz HPF（2 次）— subsonic / DC | 出力段 |
| NaN/Inf ガード：出力の合計と主要な状態を毎ブロック検査、非有限なら内部状態を無音から再構築し、そのブロックは dry を出す（`nan_resets` で回数を読める） | 出力段 |
| 壊滅的ピーク保護：−0.9 dBFS を超えた分だけソフトニーで ±1.0 に収める（普段の音には触れない、ブリックウォールではない） | 出力段 |
| デノーマル対策：ブロックごとにフィルタ状態の 1e-15 未満を 0 に | 全ステージ |
| パラメータ文字列：非有限値は無視、範囲外はクランプ。状態 JSON は全キーが揃って妥当なときだけ適用 | プラグイン |

## 状態保存

`state` に MIX DEPTH TONE DECAY MOTION BLUR FEEDBACK DARK を JSON で保存します
（例：`{"version":2,"mix":0.31,"depth":0.12,"tone":97,"decay":12.5,...,"dark":0.2}`）。遅延線・共振器の状態は保存しません。
0.1.x の状態（`"version":1`、TONE/DECAY が 0..1 のノブ位置、`sink` あり）も読み込めます：
TONE/DECAY は Hz / 秒に換算し、`sink` は無視します。
ロード後は無音状態から内部を再構築し、ノブはスムージングなしで保存値に合わせます。
キーが欠けている・壊れている状態は丸ごと拒否します。

## ARM64 ビルド結果 / ABI 依存

```bash
./scripts/build.sh arm64
```

`dist/submerge/` と `dist/submerge-module.tar.gz` ができます。

```text
PASS: AArch64, only move_audio_fx_init_v2 exported,
      NEEDED ['libm.so.6', 'libc.so.6', 'ld-linux-aarch64.so.1'] max GLIBC 2.27
      - no malloc/new/mutex imported
submerge.so: ELF 64-bit LSB shared object, ARM aarch64, 73 KB (text 26 KB, bss 3.6 MB = プール 8 個)
```

- ABI：Schwung **Audio FX API v2**（`move_audio_fx_init_v2` → `audio_fx_api_v2_t`）。`host_api_v1_t` からは
  `sample_rate` だけを読む（44100 / 48000 を受け付け、それ以外は NULL を返して読み込みを断る）
- 依存：libc / libm のみ。GLIBC 2.27 以下
- `ui_hierarchy` で 8 ノブ 1 ページを宣言し、モジュール独自の画面（`ui_chain.js`）は持ちません。
  Schwung 1.7.3 は Master FX / Send / バスの位置では `ui_hierarchy` の無いモジュールの画面を開かないため、これが Master FX 対応の条件です
- 診断用の読み取りキー：`version` `nan_resets` `governor`

## インストール

```bash
python3 scripts/install.py http://move.local:7700
```

Schwung Manager の `install-custom` 経由でアップロードします。インストール後は Move を再起動してください。

## テスト

```bash
./scripts/test.sh
```

| テスト | 内容 | 結果 |
|---|---|---|
| Dry | MIX 0 % で出力 = 入力（int16 でビット単位一致）。保存状態からの復元直後も、MIX を 0 へ回した 1 秒後も | 一致（差 0 サンプル） |
| Wet | MIX 100 % で、出力に含まれる入力そのものの成分（250 Hz 以上、遅延 0–5 ms の最小二乗ゲイン） | DEPTH 0: ノイズ 0.0002 / ドラム 0.017、DEPTH 100: 0.00005 / 0.002（基準 < 0.03） |
| Stability | 全ノブ最大で 10 分（ドラム・サイン・ピンク・フルスケール白色雑音・無音を交互） | NaN リセット 0、ガバナー有限、その後の無音で減衰 |
| Silence | 新規インスタンスに無音 60 s（全 100 %）→ 出力完全 0。バースト後の無音で 5 秒窓が前より大きくならない | 出力 0、窓ごとの変化は最大 −0.5 dB（減る一方）、1 分後もまだ鳴っていて −18 dB |
| Impulse | デフォルトでインパルス → 尾がある・減衰する・200 Hz 以下が 98 % | OK |
| Sine | 40 / 60 / 100 / 440 Hz を全 100 % で 40 s → 定常（10 s 窓の差 < 2 dB）、止めると減衰 | 差 −0.7〜+0.2 dB、停止後 −20 dB |
| Noise | ピンク / 白色雑音 60 s を全 100 % → 暴走なし | 最後の 15 s の変化 −0.2 / +0.06 dB |
| レベル維持 | DECAY 0.3–25 秒 / BLUR 0–100 % で演奏中の音量差 < 4 dB（v0.1.0 の不具合の再発防止） | 0.9 dB / 1.5 dB |
| ジッパー | 8 ノブそれぞれ最小 ↔ 最大を瞬時に動かしても、最大サンプル差分が定常時の 3 倍未満 | 1.0〜1.4 倍 |
| 状態 | 全ノブの往復、0.1.x 形式の読み込み（TONE/DECAY の換算）、不完全な状態の拒否、NaN・範囲外の扱い、インスタンスプール | OK |
| NaN ガード | ループに NaN を注入 → 1 回リセット、出力は有限のまま | OK |
| ASan / UBSan | 上記すべてをサニタイザ付きで | OK |
| グリッド | 実機と同じ Schwung のページエンジンで：1 ページ 8 ノブの順、各ノブが自分のキーを範囲内で書く、表示（`49 Hz` / `3.4 sec` / `70%`）、独自画面と SINK が無いこと | OK |
| ABI / ライセンス | ARM64 .so の検査、コピーレフト混入なし、全自作ファイルに著作権表示 | OK |

グリッドテストは実機の `shared/` のコピー（`build/device-shared/`、無ければ `../schwung`）のページエンジンを使います。

## オフライン試聴

```bash
./scripts/render_demo.sh
```

`default` / `deep` / `shallow` の 3 プリセットを書き出します。任意の設定は
`./scripts/render_demo.sh mine depth=1 dark=0.9 feedback=0.95` のように指定できます。

`build/renders/<preset>/` に、キック・スネア・ギター風コード・ノイズバーストそれぞれの
dry / 処理後 WAV（44.1 kHz 16 bit、14 秒、MIX 100 %）と、レベルエンベロープ＋0–600 Hz スペクトログラムの PNG を書きます。
すべて合成信号です（サンプル素材は使っていません）。

デフォルト設定での測定（MIX 100 %）：

| 素材 | 入力 RMS | Wet RMS | Wet の 200 Hz 以下 | Wet の 1 kHz 以上 |
|---|---|---|---|---|
| キック | 0.148 | 0.111 | 99 % | 0.0 % |
| スネア（入力は 54 % が 1 kHz 以上） | 0.042 | 0.028 | 99 % | 0.0 % |
| コード（入力は 49 % が 1 kHz 以上） | 0.057 | 0.038 | 99 % | 0.0 % |
| ノイズバースト（入力は 95 % が 1 kHz 以上） | 0.074 | 0.034 | 99 % | 0.0 % |

スペクトログラムでは、キックの個々のアタックが消えて連続した低域の場になり、
エンベロープは不規則に揺れます。deep（DEPTH 100 / FEEDBACK 80 / DARK 70）ではコードのストロークが
消え、40–60 Hz の暗い場が 8 秒以上うねりながら残ります。

## 既知の問題・未確認事項

- Master FX での音はまだ実機で聴いていません（ミックス全体に深くかけると低域が非常に大きくなるので、まず MIX を低めに）
- TONE / DECAY のノブは Hz / 秒に対して直線（標準グリッドに対数カーブの指定が無いため）。DECAY は短い側が狭く感じます
- Move 上の CPU は平均 5〜6 %（ベンチ）。SPI スレッド内での実負荷は Schwung の CPU ページで確認してください
- FEEDBACK 100 % 付近で長く放置すると、場が共鳴帯域（第 3 モード付近）に収束し、MOTION が低いと
  低いハム寄りの持続音になります。MOTION を上げると揺らぎます
- DEPTH 0 % でキックを入れると Wet のピークが入力より大きくなることがあります（ピーク 0.92、安全ニーの手前）
- 全ノブ 100 % にフルスケールの白色雑音・サインを長時間入れると、出力が安全ニーに当たり 0 dBFS 付近に張り付きます
  （クリップ・NaN にはならない）
- `ld-linux-aarch64.so.1` が NEEDED に出ます（ツールチェーン由来。既存モジュールでも同じ）
- 励振のノイズは毎回同じ乱数の種から始まるので、ロード直後の最初の数打は毎回同じ揺らぎになります

## 変更履歴

- **0.2.0** SINK を削除し、モジュール独自の画面（`ui_chain.js` / `ui_core.mjs`）をやめて標準グリッドに。`ui_hierarchy` を返すので Master FX / Send / バスでも画面が開く。TONE は Hz、DECAY は秒を値として持つように（0.1.x の状態は換算して読む）
- **0.1.1**（実機での指摘を受けて）DECAY / BLUR を上げると音が消える問題を修正（共振器 T60 を DECAY から切り離し、尾の音量を上げ、スミアを 1/√N に）。TONE を 30–120 Hz に拡大。SINK はまずループを保持してから入力を絞るように（押すと無音になっていた）
- **0.1.0** 初版

## ライセンス

MIT License — Copyright (c) 2026 Hiro Ikezawa。`LICENSE` を参照してください。
同梱する外部コードは Schwung の plugin API ヘッダ（MIT, Charles Vestal）だけで、
その notice は `THIRD_PARTY_LICENSES.md` と `third_party/schwung/LICENSE` にあります。
GPL / LGPL / AGPL のコードはリンクもコピーもしていません。
