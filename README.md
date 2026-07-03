# VCOM_G

Guitar multi-effect pedal for the **Electro-Smith Daisy Seed** (125B / Hothouse-style hardware).

The core idea: a preset = the *tone of a specific song*. Each preset loads a neural amp model
(RTNeural) + impulse-response cabinet + effects, dialed in to evoke a particular track/band.
On top of that: a tuner, a 4-track looper, fully editable & savable presets, and user
"Custom" slots.

Boot screen shows **VCOM-G** for ~3.5 s, then the main screen.

---

## Hardware (125B)

| Control | Count | Use |
|---|---|---|
| Knobs (pots) | 6 | live tone editing |
| Encoder (with push) | 1 | navigation / menu |
| Footswitches | 2 | presets / bypass / looper |
| LEDs | 2 | status |
| OLED 128×64 | 1 | UI |
| Audio | stereo | true-bypass relay |

Top status bar (PLAY): `preset#/total   CPU%` on the left, `ON`/`BYP` on the right.
**LED1** = effect engaged. **LED2** = you are inside a menu/sub-mode.

---

## PLAY mode (default)

| Action | Result |
|---|---|
| **Encoder turn** | change preset (song) |
| **Encoder press** | open MENU |
| **Right footswitch** | next preset (+) |
| **Left footswitch** | previous preset (−) |
| **Both footswitches together** | effect on/off (true-bypass relay) |
| **Knob 1** | Gain |
| **Knob 2** | Mix (amp wet/dry) |
| **Knob 3** | Level |
| **Knob 4** | Tone |
| **Knob 5** | Amp model |
| **Knob 6** | Cabinet (IR) |

Knobs use **"pickup"**: a knob only takes effect once you move it, so presets keep their
stored values until you actually touch a control. Turning a knob shows a large edit screen
(parameter name + bar) for ~1.5 s.

---

## MENU (Encoder press in PLAY)

Turn the encoder to move, press to select.

```
Tuner
Edit FX
Looper
Save Preset
Add Custom
Default All
Exit
```

- **Save Preset** — store the current tweaks (knobs + Edit FX) into the current preset slot
  (flash / QSPI). Survives power-off. Shows `SAVED`.
- **Add Custom** — clone the current sound into a new **Custom** slot (appended to the end of
  the preset list, up to 12 slots). Jumps to the new slot. Shows `CUSTOM+`.
- **Default All** — factory reset **everything**: clears all saved tweaks and removes all
  Custom presets. Shows `DEFAULT`.
- **Exit** — back to PLAY.

---

## Tuner

Menu → **Tuner**.

- Play any string → shows the **note + octave** (large) with a **cents scale**: the needle is
  centred when in tune, left = flat (−), right = sharp (+). Frequency in Hz below.
- It **holds the last detected note** (does not blank between notes) and keeps updating as you
  play. The amp is muted in tuner mode for a cleaner read.
- **Encoder press** → back to menu.

Standard tuning reference: E2 (82 Hz) · A2 (110) · D3 (147) · G3 (196) · B3 (247) · E4 (330).

---

## Edit FX

Menu → **Edit FX**. Deep per-preset effect editing.

```
Gain  ·  Tone  ·  Reverb  ·  Delay  ·  Tremolo  ·  Default  ·  Back
```

- **Encoder turn** — move between items.
- **Encoder press** on a parameter — enter adjust mode (a `*` appears); turn to change the
  value (bar + %), press again to leave adjust.
- **Default** — reset *this* preset back to its factory tone.
- **Back** — return to menu.

To make changes permanent, use **Save Preset** in the menu.

---

## Looper (4 tracks)

Menu → **Looper**. Four independent tracks, all synced to a master loop length (set by the
first track you record). Buffer lives in SDRAM (~30 s per track).

| Control | Action |
|---|---|
| **Encoder turn** | select active track (T1–T4) |
| **Right footswitch** | active track: REC → PLAY → mute → PLAY … |
| **Left footswitch** | clear the active track (→ empty) |
| **Encoder press** | exit looper (back to menu) |

Workflow ("jam in 4 parts"): record **T1** (tap = REC, play your part, tap = PLAY → it loops
and sets the loop length). Turn the encoder to **T2**, tap right footswitch to record over it,
and so on for T3/T4. All four parts loop in sync; you can still play live and switch presets on
top. The track list shows each state (`empty` / `REC` / `PLAY` / `mute`) with the active track
highlighted, plus a loop-progress bar.

---

## Presets (factory bank)

| Band | Songs |
|---|---|
| Scorpions | You and Me · Wind of Change · Still Loving You · Rock You Like a Hurricane · No One Like You · Big City Nights |
| Metallica | Nothing Else Matters · Enter Sandman · Master of Puppets |
| Rammstein | Sonne |
| Michael Jackson | Beat It |
| Chris Isaak | Wicked Game |
| Pink Floyd | Comfortably Numb |
| AC/DC | Back in Black |
| RHCP | Californication · Can't Stop · By the Way |
| Sade | Smooth Operator · No Ordinary Love |
| Custom | My Tone (+ any Custom slots you add) |

Presets are data in [`song_library.h`](song_library.h) — easy to edit, add, or retune.

> Note: the 9 built-in neural amp models are generic captures — they get you *in the ballpark*
> of each tone, not a 1:1 clone. True per-track accuracy would require custom neural amp
> captures (a separate, offline task).

---

## Build & flash

From this folder:

```bash
make                 # build  -> build/VCOM_G.bin
make program-dfu     # flash via USB DFU
```

The app is built with `-O3 -ffast-math` (this is what makes the neural amp + IR + effects fit
in real time — dropping back to `-Os` causes audio dropouts/crackle).

Because the binary is large (RTNeural + eigen) it runs from SRAM, so it needs the **Daisy
bootloader** (flashed once with `make program-boot`). To load the app, put the board into the
bootloader's DFU window (**double-tap RESET**, LED breathes) and run `make program-dfu`.

Dependencies (libDaisy, DaisySP, RTNeural, eigen, amp/IR modules) are reused from the sibling
`../GuitarPedal/` folder — referenced by the Makefile, no separate copy needed.

---

## Files

| File | Purpose |
|---|---|
| `my_pedal.cpp` | all firmware: audio engine, effects, gate, menu, tuner, looper, save |
| `song_library.h` | preset bank (data) |
| `Makefile` | build config (`-O3`, paths) |
