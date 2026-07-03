# CLAUDE.md — VCOM-G firmware

Guitar multi-effect + 4-track looper on **Electro-Smith Daisy Seed** (STM32H750),
**Guitar Pedal 125B** hardware (bkshepherd's project, `namespace bkshepherd`).
Concept: a preset = the tone of a specific song (neural amp + IR cab + FX).

Owner is a hardware+firmware builder (guitar pedals). **Reply in Russian** (transliterated input is fine).
Default mode is **review/advise — do NOT edit files unless explicitly asked.** Verify from code/build
before claiming; say plainly when something is a hypothesis.

## Token-saving rules (read first)
- The whole app is **2 files**: `my_pedal.cpp` (~830 lines, all logic) and `song_library.h` (preset data).
  Read targeted line ranges, not whole files repeatedly. Facts below so you don't re-derive them.
- **`amp_module.cpp/.h`, libDaisy, DaisySP, RTNeural, eigen are NOT in this repo** — they live in a
  sibling `../GuitarPedal/` that is usually absent on this machine. Don't search the whole disk for them
  every session; if a question needs amp/IR internals, ask the user to copy `GuitarPedal` into `FW/`.
- To inspect what's actually linked without the source, grep `build/*.map` / `build/*.lst` / `build/*.d`
  (e.g. symbols `ir_data1..10`, `ImpulseResponse::Process`). Use Grep, not Bash `grep`.
- Don't re-explore settled facts; trust this file and update it when something changes.

## CRITICAL build facts (hard-won)
- **Toolchain MUST be GCC 10.3-2021.10** (`C:\Users\vinok\toolchains\gcc10\...`), NOT GCC 14. GCC 14 +
  `-ffast-math` silently miscompiles Eigen/RTNeural → neural amp outputs ~0 → NO distortion (clean tone).
  build.sh points at GCC 10.3. libDaisy/DaisySP rebuilt with 10.3.
- amp_module is the **verbatim f6a609c** version (9 models / 10 IRs) + OnePoleTone (DaisySP `Tone` removed)
  + base `GetParameterAsMagnitude` added. RTNeural pinned `29e41da0`, eigen `9836e8d0`, `-DRTNEURAL_USE_EIGEN=1`.
- Distortion = amp MODEL (knob5/Gain), IR (knob6) = cabinet EQ only (10 IRs ~400 taps each, not normalized →
  cabs differ in level; that's expected, not a bug).

## New effects + Edit FX (pot-controlled, pickup)
- Added FX after amp: **3-band EQ** (EQ3 struct, Low/Mid/High, transparent at 0.5/0.5/0.5), **Chorus,
  Phaser, Flanger** (DaisySP, placed `DSY_SDRAM_BSS` — they don't fit DTCM; empty ctors so SDRAM-safe).
- **Delay** = manual SDRAM circular buffer `g_delayBuf` (DelayLine-in-SDRAM was unreliable); default 300 ms
  time so Edit FX delay-mix is audible on any preset. SAVE_VERSION=4 (wipes old QSPI that masked delay).
- **Edit FX reworked**: encoder switches effect PAGE (`Amp·EQ·Reverb·Delay·Tremolo·Chorus·Phaser·Flanger`),
  the 6 pots edit that page's params with pickup (reuses g_knobChanged/g_knobCache; cleared on page
  switch/entry/exit). See `editParamCount/Name/Get/Apply` + `drawEditPage` + MODE_EDIT handler.
- Menu LED (LED1/index1) forced OFF (was too bright). LED0 = effect-on still works.

## Build / flash — SEE BUILD.md. VCOM-G1 is self-contained (deps in `GuitarPedal/`).
- Build in Git Bash: `./build.sh` (wraps PATH for arm-gcc 14.2 / make 4.4.1 / dfu-util 0.11). → `build/VCOM_G.bin`.
- Toolchain: `C:\Users\vinok\toolchains\arm-gnu\bin`; make: `…\.mplab\…\make\v4.4.1\windows`;
  dfu-util: `…\.espressif\tools\dfu-util\0.11\…`. libDaisy/DaisySP prebuilt under `GuitarPedal/*/build/*.a`.
- Flash (user only, needs board): `./build.sh program-dfu` (double-tap RESET → DFU). `program-boot` once for bootloader.
- `APP_TYPE = BOOT_SRAM`; `OPT=-O3 -ffast-math -ffinite-math-only` (required for real-time neural+IR).
- **CRITICAL: CPPFLAGS must include `-DRTNEURAL_USE_EIGEN=1`** (from f6a609c Makefile). Without it RTNeural
  uses a non-eigen backend, GRU weights load wrong → `model.forward`≈0 → NO distortion (clean/"acoustic"
  on every preset). This was the root cause of "no distortion". RTNeural pinned `29e41da0`, eigen `9836e8d0`.
- GP modules came from DaisySeedProjects commit `2f47ce6d`; deps cloned at latest (libDaisy needs its
  submodules: `git submodule update --init` HAL/CMSIS/USB). Latest DaisySP has NO `Tone` class.

## Compressor + Boost + preset chorus (rev 1.9)
- **Compressor** (`g_compAmt`, `g_compEnv`) and **Boost/overdrive** (`g_boost`) are custom inline DSP in
  AudioCallback, placed **before `amp.ProcessMono`** (comp = envelope follower + gain-reduction + makeup;
  boost = pre-gain ×(1+4·amt) + cubic soft-clip). Both single-knob (0..1). Edit FX pages `PG_COMP`/`PG_BOOST`
  (Amount). Stored in `StoredPreset.compAmt/boost` + `SongPreset.comp/boost` (trailing fields).
- **Chorus** now wired into factory presets: `SongPreset` gained trailing `chorusMix/chorusRate/chorusDepth`;
  `presetToStored` copies them (was hard-zeroed). Set on Nirvana Teen Spirit Clean + Sade Smooth Operator.
  Boost set on Lead tones (Scorpions ×3, Metallica Sandman, MJ Beat It, AC/DC Back Black).
- **`SAVE_VERSION` 6→7** (StoredPreset grew) → wiped on-device overrides/customs. Factory (song_library) unaffected.

## Hardware (confirmed from build/VCOM_G.map)
- Display: `OledDisplay<SSD130xDriver<128,64, SSD130x4WireSpiTransport>>` — 128×64 mono on **4-wire SPI**.
  → **I2C bus is free** for expanders / I2C peripherals.
- 6 pots (ADC), 1 encoder + push, 2 footswitches, 2 LEDs, true-bypass relay. Daisy Seed ≈31 GPIO,
  ~18–20 used, ~10–13 free. SDRAM/QSPI/audio-codec/USB are on dedicated pins (don't cost GPIO).
- Schematic net names for tapping audio: processed output = `AUDIO_OUT_BUFFER_LEFT/RIGHT`
  (Daisy `AUDIO_OUT_1/2`), analog gnd = `GND_A`. Mute/bypass control lines exist:
  `ENABLE_AUDIO_MUTE`, `DISABLE_AUDIO_BYPASS`.

## Audio engine facts (my_pedal.cpp)
- Mono DSP, 48 kHz, block size **48 = 1 ms**. `out[0]==out[1]` (L==R). CPU% = `GetUs()*0.1` (1 ms budget).
- Chain: in → [relay] → Gain → neural amp → Tone → IR → NoiseGate → Tremolo → Delay → Reverb → out.
- **Audio runs in `AudioCallback` (ISR/DMA). UI runs in the `main()` while-loop (`DelayMs(15)`).**
  Display/encoder/LED/I2C work in the main loop does NOT touch the audio CPU budget. RULE: never put
  blocking I2C / heavy work inside `AudioCallback` — read I2C peripherals in the main loop or via DMA/INT.
- AmpModule param indices: `0 Gain, 1 Mix, 2 Level, 3 Tone, 4 Model, 5 IR, 6 NeuralOn, 7 IROn`.
  Firmware sets 0–5 but **never touches 6/7**.
- Knob map: 1 Gain, 2 Mix, 3 Level, 4 Tone, 5 Model(binned), 6 IR(binned). `isBinnedKnob → i==4||i==5`.
  Knobs use "pickup" logic (g_knobCache/g_knobIdle) so presets keep stored values until a knob moves.
- Footswitch indices swapped: `SW_LEFT=1, SW_RIGHT=0`.
- **Multi-tone nav (rev 1.5+):** a "song" = consecutive presets in `song_library.h` with the same
  `band`+`song`; `SongPreset` has a `part` field (tone name Clean/Crunch/Lead…). `buildSongTable()`
  groups them into `g_toneFirst/g_toneCount/g_songNo/g_songFirstList/g_songCount` (rebuild after
  Add Custom / Default All). **Encoder = song (`gotoSong`), footswitches ± = tone within song
  (`gotoTone`, wraps), both = bypass.** Flat index `g_currentSong` is still the source of truth for
  save/override/custom; multi-tone is a nav layer on top. Bank = 14 songs / 27 tones. Custom slots =
  1-tone songs appended. Was flat (`changeSong`) before rev 1.5.
- Modes: `MODE_PLAY/MENU/TUNER/EDIT/LOOPER/SYSTEM` (`g_mode`).
- **System screen (rev 2.1+):** menu item "System" → `drawSystem`/`MODE_SYSTEM`. Contrast = REAL ST7565
  electronic-volume (cmd 0x81+val 0..63, `g_contrast` default 0x13). Brightness = demo slider only
  (`g_brightness` 0..100, no HW yet — backlight PWM pin TBD). Interaction: encoder scrolls items
  (Contrast/Brightness/Back), push = enter value-edit (or Back=exit), in edit encoder changes value,
  push = done. **Not persisted** (runtime only) — saving needs a SAVE_VERSION bump which wipes presets.
  Contrast plumbing: added `SetContrast(uint8_t)` to `st7565.h` driver + a passthrough in libDaisy
  `oled_display.h` (`VCOM-G1/.../hid/disp/oled_display.h`; template member, only instantiated when called
  so VCOM-G1's SSD130x build is unaffected).
- **Looper + independent metronome (rev "1.3" — user reset the version string; note it collides with the
  early 1.3, it's really the newest build):** 4 tracks × 30 s SDRAM, **free-form** recording (first track
  sets `g_loopLen`, `looperRightCycle`: empty→rec→play→mute, states 0/1/2/3 only). Recording stores DRY `a`
  (before loop mix + before click) so click/layers don't bleed into tracks. **Metronome is INDEPENDENT** of
  the loop (free-running `g_metCounter` at `g_beatSamples`, click added AFTER record), **default OFF**
  (`g_metOn=false`); `recomputeTempo` only sizes the click, does NOT set loop length. Accent on beat 1,
  LED0 pulse (`g_ledBeat`). Per-track volume `g_loopVol[]`, per-track tone `g_loopTone[]`.
  Controls: **right FS = rec→play→mute cycle, left FS = clear, both FS = exit** (unchanged). **Encoder rotate
  = adjust selected field, push = cycle field** (`loopField` 0..4: TRK track / MET metro on-off / BPM / VOL
  metVol / POT pot-mode). **Pots = live tone when POT=Tone; = track volumes (T1–4) when POT=Vol OR while the
  active track is recording** (`g_potMode`, auto-Vol during REC). Also `Menu → Tempo` (MODE_TEMPO) sets
  BPM/Sig/Click/Metro. `g_looperEngaged` gates the audio block.
- Reverb = Schroeder (4 comb fb=0.84 + 2 allpass + 1-pole LP). Delay = `DelayLine<float,48000>` in
  **SDRAM** (`DSY_SDRAM_BSS`), capped `DELAY_MAX_MS=900`, with a 1-pole LP on repeats (`g_delayLp`, dark
  echo). Was 7000/DTCM/140 ms — moved to SDRAM so longer delays don't crash the DTCM stack. Tuner =
  NSDF/McLeod pitch detect, buffers in `RAM_D2_DMA`.
- Save: `PersistentStorage<SaveData>` in QSPI, `SAVE_VERSION=3`; factory presets + up to 12 custom slots.

## amp_module / IR internals (VCOM-G1 — reconstructed & building)
The user's original 9-model/10-IR `amp_module.cpp` + data are LOST (not on disk, not online — they were a
generated custom fork). VCOM-G1 uses the upstream `2f47ce6d` `amp_module`: **7 models** (Fender57, Matchless,
Klon, Mesa iic, Bassman, 5150, Splawn) + **4 IRs** (Marsh, Proteus, US Deluxe, British). NAM-style
`ImpulseResponse` (`Init(std::vector)`, `Process(float)`, `mMaxLength=8192`), full convolution.
- Param system: `SetParameterAsMagnitude`(Binned)→`SetParameterAsBinnedValue`→`SetParameterRaw`→
  `ParameterChanged` (only if raw changed). Both cab knob and preset loads reload the IR. `IR_ON`/
  `NEURAL_MODEL` default 1 (on). ProcessMono: gain → GRU `forward+skip` → `tone` LP → wet/dry → `mIR.Process`.
- FIXES applied in VCOM-G1 (not in stock upstream):
  - `SetParameterAsBinnedValue` now CLAMPS to [1,binCount] (was reject) + `SelectModel/SelectIR` clamp the
    index → no OOB, deterministic model/cab for any preset bin. This is the "squeal-from-OOB" fix.
  - `OnePoleTone` (one-pole LP in amp_module.h) replaces DaisySP `Tone` (removed from latest DaisySP).
  - Added `BaseEffectModule::GetParameterAsMagnitude` (inverse of SetParameterAsMagnitude) — my_pedal needs it.
  - `hardware.Init(48,false)` (125B Init now takes blockSize,boost).
- song_library still references model bins up to 9 / IR bins up to 10 → they CLAMP (models 8,9→Splawn;
  IR 5–10→British). Cabs actually used: Proteus/US Deluxe/British (Marsh unused). OPTIONAL next step: remap
  preset bins to spread across the 7 models / 4 IRs for fuller cabinet variety. Residual squeal source:
  no oversampling on the GRU (aliasing on high gain) — could add 2× or a fixed HF LP.

## Design ideas discussed (not yet implemented)
- Bigger display: drop-in **SSD1309 2.42″/2.7″ 128×64 SPI** = zero code change. Higher-res needs
  parametrizing hardcoded 128/127 coords (writeCentered, drawBar, menus, tuner, looper).
- More controls: hybrid 4 pots + 2 encoders (Model/IR are already binned → ideal for detented encoders).
  Many encoders → use **I2C encoders with hardware counters** (DUPPA i2cEncoder / Adafruit seesaw),
  NOT a plain GPIO expander (misses quadrature steps). Addressable LEDs → APA102 (SPI, 2 pins) preferred
  over WS2812 (1 pin, tight 800 kHz timing). Headphone amp: tap `AUDIO_OUT_BUFFER_*`, TPA6130A2 (I2C vol)
  or TPA6132 (DirectPath); power from clean LDO 5 V, not the DC-DC.
- Power: U2 `PDS1-S5-S5-M` isolated DC-DC appears to have its output 0V tied to main GND (isolation
  defeated) — confirm intent before recommending a replacement.
