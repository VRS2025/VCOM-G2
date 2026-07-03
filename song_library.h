// Библиотека «песен» — MultiSong Pedal.
// Каждая песня = 1..3 ТОНА (Clean/Crunch/Lead...). Тоны одной песни идут ПОДРЯД
// (группируются по совпадению band+song). Навигация: энкодер=песня, футсвитчи=тон.
//
// Модели (bin): 1 Fender57 2 Matchless 3 Klon 4 Mesa iic 5 H&K Clean
//               6 Bassman 7 5150 8 Splawn 9 Klon2
// Кабинеты (IR, bin): 1 Rhythm 2 Lead 3 Clean 4 Marsh 5 Bogn
//                     6 Proteus 7 Rectify 8 Rhythm2 9 US Deluxe 10 British
// EQ: 0.5 = ровно, <0.5 срез, >0.5 подъём. Имена band/song/part <= 11 симв.

#pragma once

struct SongPreset
{
    const char *band;
    const char *song;
    const char *part; // имя тона (Clean/Crunch/Lead/Heavy/Funk/Driven)
    int ampModelBin;
    int irBin;
    float gain, mix, level, tone;
    float revMix, delayMix, delayTimeMs, delayFb, tremDepth, tremRate;
    float eqLow, eqMid, eqHigh, revFb, revDamp; // 0 = «по умолчанию» (см. presetToStored)
    float chorusMix, chorusRate, chorusDepth;   // хорус (0 mix = выкл)
    float comp, boost;                          // компрессор + буст (0 = выкл). Хвост можно опускать.
};

static const SongPreset kSongs[] = {
    // band          song           part      am ir  gain  mix  lvl   tone | rev  dMix dMs   dFb  tDep tRate| eqLo eqMid eqHi revFb revDmp
    // --- Scorpions — Still Loving You ---
    {"Scorpions", "Still Lovin", "Clean",  2, 3, 0.84f, 1.0f, 1.0f, 0.52f, 0.28f, 0,0,0,0,0, 0.50f,0.55f,0.55f,0,0},
    {"Scorpions", "Still Lovin", "Crunch", 8, 4, 0.55f, 1.0f, 0.52f, 0.55f, 0.10f, 0,0,0,0,0, 0.45f,0.68f,0.60f,0,0},
    {"Scorpions", "Still Lovin", "Lead",   8, 2, 0.80f, 1.0f, 0.52f, 0.60f, 0.22f, 0.20f,350.0f,0.25f,0,0, 0.45f,0.72f,0.62f,0,0, 0,0,0, 0,0.35f}, // +буст
    // --- Scorpions — Rock You Like a Hurricane ---
    {"Scorpions", "Rock You H.", "Crunch", 8, 4, 0.60f, 1.0f, 0.52f, 0.56f, 0.08f, 0,0,0,0,0, 0.45f,0.68f,0.62f,0,0},
    {"Scorpions", "Rock You H.", "Lead",   8, 2, 0.82f, 1.0f, 0.52f, 0.60f, 0.18f, 0.18f,300.0f,0.22f,0,0, 0.45f,0.72f,0.64f,0,0, 0,0,0, 0,0.35f}, // +буст
    // --- Nirvana — Smells Like Teen Spirit ---
    {"Nirvana", "Teen Spirit", "Clean", 1, 3, 0.84f, 1.0f, 1.0f, 0.55f, 0.10f, 0,0,0,0,0, 0.52f,0.50f,0.55f,0,0, 0.45f,0.25f,0.50f, 0,0}, // хорус Small Clone
    {"Nirvana", "Teen Spirit", "Heavy", 8, 4, 0.75f, 1.0f, 0.50f, 0.55f, 0.05f, 0,0,0,0,0, 0.55f,0.52f,0.55f,0,0},
    // --- Metallica — Enter Sandman ---
    {"Metallica", "Sandman", "Clean", 5, 3, 0.80f, 1.0f, 1.0f, 0.50f, 0.20f, 0,0,0,0,0, 0.52f,0.45f,0.55f,0,0},
    {"Metallica", "Sandman", "Heavy", 4, 7, 0.80f, 1.0f, 0.50f, 0.58f, 0,     0,0,0,0,0, 0.62f,0.38f,0.62f,0,0},
    {"Metallica", "Sandman", "Lead",  4, 2, 0.82f, 1.0f, 0.50f, 0.60f, 0.12f, 0.12f,300.0f,0.20f,0,0, 0.55f,0.50f,0.62f,0,0, 0,0,0, 0,0.35f}, // +буст
    // --- Rammstein — Sonne ---
    {"Rammstein", "Sonne", "Heavy", 4, 7, 0.76f, 1.0f, 0.50f, 0.62f, 0, 0,0,0,0,0, 0.68f,0.40f,0.66f,0,0},
    // --- Michael Jackson — Beat It ---
    {"M. Jackson", "Beat It", "Crunch", 8, 4, 0.50f, 1.0f, 0.52f, 0.56f, 0.08f, 0,0,0,0,0, 0.50f,0.60f,0.58f,0,0},
    {"M. Jackson", "Beat It", "Lead",   8, 2, 0.78f, 1.0f, 0.52f, 0.60f, 0.15f, 0.15f,280.0f,0.25f,0,0, 0.48f,0.65f,0.62f,0,0, 0,0,0, 0,0.35f}, // +буст
    // --- AC/DC — Back in Black ---
    {"AC/DC", "Back Black", "Crunch", 8, 4, 0.45f, 1.0f, 0.54f, 0.58f, 0.05f, 0,0,0,0,0, 0.50f,0.62f,0.60f,0,0},
    {"AC/DC", "Back Black", "Lead",   8, 4, 0.58f, 1.0f, 0.54f, 0.60f, 0.10f, 0.10f,250.0f,0.15f,0,0, 0.50f,0.65f,0.62f,0,0, 0,0,0, 0,0.30f}, // +буст
    // --- RHCP — Californication ---
    {"RHCP", "Californ.", "Clean",  1, 3, 0.90f, 1.0f, 1.0f, 0.62f, 0.15f, 0,0,0,0,0, 0.50f,0.50f,0.60f,0,0},
    {"RHCP", "Californ.", "Driven", 3, 9, 0.45f, 1.0f, 0.53f, 0.60f, 0.12f, 0,0,0,0,0, 0.50f,0.58f,0.60f,0,0},
    // --- RHCP — Can't Stop ---
    {"RHCP", "Can't Stop", "Funk",   1, 3, 0.36f, 1.0f, 0.72f, 0.64f, 0.08f, 0,0,0,0,0, 0.50f,0.55f,0.62f,0,0, 0,0,0, 0.60f,0}, // компрессор
    {"RHCP", "Can't Stop", "Driven", 3, 9, 0.48f, 1.0f, 0.53f, 0.60f, 0.10f, 0,0,0,0,0, 0.50f,0.58f,0.60f,0,0},
    // --- RHCP — By the Way ---
    {"RHCP", "By the Way", "Clean", 1, 3, 0.90f, 1.0f, 1.0f, 0.63f, 0.10f, 0,0,0,0,0, 0.50f,0.52f,0.62f,0,0},
    {"RHCP", "By the Way", "Heavy", 8, 4, 0.70f, 1.0f, 0.50f, 0.56f, 0.05f, 0,0,0,0,0, 0.55f,0.55f,0.58f,0,0},
    // --- Chris Isaak — Wicked Game ---
    {"Chris Isaak", "Wicked Game", "Clean", 1, 3, 0.76f, 1.0f, 1.0f, 0.55f, 0.42f, 0.22f,350.0f,0.20f,0.50f,3.5f, 0.57f,0.38f,0.60f,0.80f,0.42f},
    // --- Sade — Smooth Operator ---
    {"Sade", "Smooth Oper", "Clean", 1, 3, 0.76f, 1.0f, 1.0f, 0.40f, 0.25f, 0,0,0,0,0, 0.55f,0.42f,0.35f,0,0, 0.38f,0.20f,0.45f, 0,0}, // хорус
    // --- Custom ---
    {"Custom", "My Tone", "Clean", 1, 3, 0.40f, 1.0f, 0.55f, 0.55f, 0.20f, 0,0,0,0,0, 0,0,0,0,0},
};

static const int kSongCount = (int)(sizeof(kSongs) / sizeof(kSongs[0]));
