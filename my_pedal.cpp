// MultiSong Pedal — приложение поверх железа 125b (GuitarPedal125B).
// Пресет = тон под трек: нейромодель усилителя + IR-кабинет + ручки + эффекты.
//
// Режимы (нажатие энкодера открывает МЕНЮ):
//   PLAY   — основной: энкодер листает песни; футсвитчи +/-/оба=bypass; ручки правят тон.
//   MENU   — список: Tuner / Edit FX / Save / Exit. Энкодер: вращать=выбор, нажать=войти.
//   TUNER  — тюнер: автокорреляция входа -> нота + шкала центов. Нажать=назад.
//   EDIT   — правка эффектов: Gain/Tone/Reverb/Delay/Tremolo. Вращать=выбор, нажать=правка.
//
// Цепочка: вход -> [реле] -> Gain -> нейромодель -> Tone -> IR -> NoiseGate ->
//          Tremolo -> Delay -> Reverb -> выход.

#include <cstdio>
#include <cmath>
#include <cstring>
#include "daisy_seed.h"
#include "guitar_pedal_125b.h"
#include "amp_module.h"
#include "song_library.h"

using namespace daisy;
using namespace daisysp;
using namespace bkshepherd;

GuitarPedal125B hardware;
AmpModule amp;

// Ревизия прошивки — показывается мелким шрифтом под логотипом на заставке.
static const char *FW_REVISION = "rev 2.1";

// Эффекты после усилителя.
// delay-буфер вынесен в SDRAM (как лупер) -> DTCM свободен под стек, время до ~1 с.
static const float DELAY_MAX_MS = 900.0f;
struct SimpleReverb
{
    static const int C1 = 1557, C2 = 1617, C3 = 1491, C4 = 1422, A1 = 225, A2 = 556;
    float c1[C1], c2[C2], c3[C3], c4[C4], a1[A1], a2[A2];
    int i1, i2, i3, i4, ja1, ja2;
    float fb, lp, damp;
    void Init()
    {
        memset(c1, 0, sizeof(c1)); memset(c2, 0, sizeof(c2)); memset(c3, 0, sizeof(c3));
        memset(c4, 0, sizeof(c4)); memset(a1, 0, sizeof(a1)); memset(a2, 0, sizeof(a2));
        i1 = i2 = i3 = i4 = ja1 = ja2 = 0;
        fb = 0.84f; lp = 0.0f; damp = 0.35f;
    }
    static inline float comb(float *b, int n, int &i, float x, float fb)
    {
        float y = b[i]; b[i] = x + y * fb; if (++i >= n) i = 0; return y;
    }
    static inline float allpass(float *b, int n, int &i, float x, float g)
    {
        float bo = b[i]; float y = -x + bo; b[i] = x + bo * g; if (++i >= n) i = 0; return y;
    }
    float Process(float x)
    {
        float y = comb(c1, C1, i1, x, fb) + comb(c2, C2, i2, x, fb) + comb(c3, C3, i3, x, fb) + comb(c4, C4, i4, x, fb);
        y *= 0.25f;
        y = allpass(a1, A1, ja1, y, 0.5f);
        y = allpass(a2, A2, ja2, y, 0.5f);
        lp += damp * (y - lp);
        return lp;
    }
};
static SimpleReverb reverb;

// 3-полосный эквалайзер (Low/Mid/High) — разделение двумя однополюсными ФНЧ.
// При усилениях (1,1,1) сумма телескопически = вход (прозрачно).
struct EQ3
{
    float lp1, lp2, a1, a2, sr;
    void Init(float s) { sr = s; lp1 = lp2 = 0.0f; SetFreqs(250.0f, 2500.0f); }
    void SetFreqs(float f1, float f2)
    {
        a1 = 1.0f - expf(-2.0f * 3.14159265358979f * f1 / sr);
        a2 = 1.0f - expf(-2.0f * 3.14159265358979f * f2 / sr);
    }
    float Process(float x, float gLow, float gMid, float gHigh)
    {
        lp1 += a1 * (x - lp1);
        lp2 += a2 * (x - lp2);
        float low = lp1;
        float mid = lp2 - lp1;
        float high = x - lp2;
        return low * gLow + mid * gMid + high * gHigh;
    }
};
static EQ3 eq3;
static volatile float g_eqLow = 0.5f, g_eqMid = 0.5f, g_eqHigh = 0.5f; // 0.5 = плоско (усиление 1.0)
// Delay — ручной кольцевой буфер в SDRAM (как лупер, проверенный способ; без класса DelayLine).
static const int DELAY_BUF = 48000;               // 1 с @48к
static float DSY_SDRAM_BSS g_delayBuf[DELAY_BUF]; // буфер delay в SDRAM
static volatile int g_delaySamples = 1;           // текущее время delay в сэмплах
static int g_delayWrite = 0;                      // указатель записи
static float g_delayLp = 0.0f;                    // ФНЧ на повторах (тёмное эхо)
static Tremolo tremolo;
static volatile bool g_tremOn = false;

// Модуляционные эффекты (после усилителя, до delay/reverb).
// Буферы — в SDRAM (в DTCM не влезают; конструкторы пустые, Init() в main).
static Chorus DSY_SDRAM_BSS chorus;
static Phaser DSY_SDRAM_BSS phaser;
static Flanger DSY_SDRAM_BSS flanger;

// Параметры AmpModule: 0=Gain 1=Mix 2=Level 3=Tone 4=Model 5=IR 6=NeuralOn 7=IROn
static const int P_GAIN = 0, P_MIX = 1, P_LEVEL = 2, P_TONE = 3, P_MODEL = 4, P_IR = 5;

static const int SW_LEFT = 1, SW_RIGHT = 0; // поменяны местами под физическую раскладку

static int g_currentSong = 0;
static float g_sampleRate = 48000.0f;

// Настройки LCD (System). Контраст = electronic volume ST7565 (0..63), по умолч. 0x13.
// Яркость — пока демо (подсветка на PWM-пин заведём позже), 0..100.
static int g_contrast = 0x13;
static int g_brightness = 70;

// Режимы приложения.
enum AppMode { MODE_PLAY, MODE_MENU, MODE_TUNER, MODE_EDIT, MODE_LOOPER, MODE_SYSTEM, MODE_CUSTOM, MODE_TEMPO };
static volatile AppMode g_mode = MODE_PLAY;

// --- Лупер: 4 дорожки по 30 с в SDRAM (интерливленный буфер -> последовательный доступ) ---
static const int LOOP_TRACKS = 4;
static const int LOOP_LEN = 30 * 48000; // 30 с @ 48 кГц
static float DSY_SDRAM_BSS g_loop[LOOP_LEN][LOOP_TRACKS];
// Состояния дорожки: 0 пусто,1 запись,2 игра,3 mute,4 armed-rec,5 armed-dub,6 overdub
static volatile uint8_t g_loopState[LOOP_TRACKS] = {0, 0, 0, 0};
static volatile int g_loopLen = 0;    // длина мастер-петли (сэмплы; задаётся темпом)
static volatile int g_loopPos = 0;    // общий указатель воспроизведения
static volatile int g_loopActive = 0; // активная дорожка (управление)
static volatile float g_loopVol[LOOP_TRACKS] = {1.0f, 1.0f, 1.0f, 1.0f}; // громкости дорожек
static int g_loopTone[LOOP_TRACKS] = {0, 0, 0, 0}; // тон (индекс песни) на дорожке

// --- Метроном (независимый от петли, free-running) ---
static volatile int   g_bpm = 120;
static volatile int   g_beatsPerBar = 4;   // размер (доли в такте)
static volatile bool  g_metOn = false;     // клик по умолчанию ВЫКЛ
static volatile float g_clickVol = 0.5f;   // громкость клика
static volatile int   g_beatSamples = 24000; // сэмплов на долю (recomputeTempo)
static volatile int   g_metCounter = 0;    // счётчик сэмплов до следующей доли
static volatile bool  g_looperEngaged = false; // мы в режиме лупера
static volatile int   g_beatShow = 0;      // текущая доля 0..beatsPerBar-1
static volatile int   g_clickEnv = 0;      // остаток envelope клика (сэмплы)
static volatile int   g_clickLen = 2000;   // длина клика (сэмплы)
static volatile bool  g_clickAccent = false;
static volatile float g_clickPhase = 0.0f;
static volatile int   g_ledBeat = 0;       // остаток свечения LED на долю (сэмплы)
static volatile int   g_potMode = 0;       // 0=Tone, 1=Volume (что крутят поты в лупере)
static volatile bool  g_countInOn = true;  // отсчёт перед записью
static volatile int   g_countInLeft = 0;   // осталось долей отсчёта (>0 = идёт count-in)

// --- состояние ISR <-> главный цикл ---
static const int KNOBS = 6;
static volatile bool g_effectOn = false; // старт в BYPASS (включается футсвитчами)
static volatile int g_encDelta = 0;
static volatile int g_encPress = 0;

static volatile bool g_knobChanged[KNOBS];   // 1 = пот «взял» управление параметром
static volatile float g_knobCache[KNOBS];     // текущее значение пота (когда взят)
static volatile float g_knobRef[KNOBS];       // референс позиции при загрузке пресета/режима
static volatile bool g_knobRearm = false;     // пере-снять референсы (после смены пресета/режима)
static volatile bool g_knobsInit = false;
static const float KNOB_MOVE = 0.03f;         // насколько двинуть пот, чтобы он «взял» параметр

static volatile int g_muteCountdown = 0;
static int g_switchMuteSamples = 0;

// Нойз-гейт.
static float g_gateEnv = 0.0f, g_gate = 0.0f;
static int g_gateHold = 0, g_gateHoldSamples = 0;
static const float GATE_THRESH = 0.0012f;

// Эффекты (правятся из applySong / Edit FX).
static volatile float g_revMix = 0.0f, g_delayMix = 0.0f, g_delayFb = 0.0f, g_tremDepth = 0.0f;
static volatile float g_cpuLoad = 0.0f;

// Доп. параметры эффектов (правятся потами в Edit FX).
static volatile float g_tremRate = 5.0f;                 // Гц
static volatile float g_delayTimeMs = 300.0f;            // мс
static volatile float g_revFb = 0.84f, g_revDamp = 0.35f;
static volatile float g_chorusMix = 0.0f, g_chorusRate = 0.4f, g_chorusDepth = 0.5f;
static volatile float g_phaserMix = 0.0f, g_phaserRate = 0.3f, g_phaserDepth = 0.6f, g_phaserFb = 0.3f;
static volatile float g_flangerMix = 0.0f, g_flangerRate = 0.2f, g_flangerDepth = 0.5f, g_flangerFb = 0.3f;
static volatile float g_compAmt = 0.0f; // компрессор (0=выкл), до усилителя
static volatile float g_boost = 0.0f;   // буст/overdrive (0=выкл), до усилителя
static float g_compEnv = 0.0f;          // envelope-детектор компрессора

// Применить параметры модуляционных эффектов к объектам DaisySP.
static void applyModParams()
{
    chorus.SetLfoFreq(0.1f + g_chorusRate * 4.0f);
    chorus.SetLfoDepth(g_chorusDepth);
    chorus.SetFeedback(0.2f);
    phaser.SetLfoFreq(0.1f + g_phaserRate * 4.0f);
    phaser.SetLfoDepth(g_phaserDepth);
    phaser.SetFeedback(g_phaserFb);
    phaser.SetFreq(500.0f);
    flanger.SetLfoFreq(0.05f + g_flangerRate * 2.0f);
    flanger.SetLfoDepth(g_flangerDepth);
    flanger.SetFeedback(g_flangerFb);
}

// --- Тюнер ---
static const int TUNER_DECIM = 4;
static const int TUNER_N = 1024;
static const int TLAG_MIN = 16;  // ~750 Гц при 12 кГц
static const int TLAG_MAX = 165; // ~73 Гц
// Буферы тюнера — в RAM_D2_DMA (DTCMRAM занята буфером delay).
static float DMA_BUFFER_MEM_SECTION g_tunerBuf[TUNER_N];
static float DMA_BUFFER_MEM_SECTION g_acBuf[TUNER_N];
static float DMA_BUFFER_MEM_SECTION g_acCorr[TLAG_MAX + 2];
static volatile int g_tunerIdx = 0;
static int g_tunerDecimCnt = 0;
static float g_tunerLp = 0.0f;   // антиалиасинговый ФНЧ перед прореживанием
static float g_tunerFreq = 0.0f; // 0 = нет сигнала

// --- Сохранение в QSPI ---
static const int MAX_CUSTOM = 12; // максимум пользовательских пресетов

// Полный набор параметров пресета (для override фабричных и для custom).
struct StoredPreset
{
    int model, ir;
    float gain, mix, level, tone, revMix, delayMix, delayTimeMs, delayFb, tremDepth, tremRate;
    // Новые эффекты (сохраняются вместе с пресетом).
    float eqLow, eqMid, eqHigh;
    float revFb, revDamp;
    float chorusMix, chorusRate, chorusDepth;
    float phaserMix, phaserRate, phaserDepth, phaserFb;
    float flangerMix, flangerRate, flangerDepth, flangerFb;
    float compAmt, boost; // компрессор + буст
};
struct CustomPreset
{
    char name[12];
    StoredPreset p;
};

// ---- Банк пресетов: из файла в QSPI (0x90100000) или заводской song_library ----
static const int MAX_BANK = 32;

// Один пресет в файле (без указателей — имена фиксированной длины).
struct FilePreset
{
    char band[12];
    char song[12];
    int32_t model, ir;
    float gain, mix, level, tone, revMix, delayMix, delayTimeMs, delayFb, tremDepth, tremRate;
    float eqLow, eqMid, eqHigh, revFb, revDamp;
};
struct PresetFileHeader
{
    uint32_t magic;    // "VMG1"
    uint32_t version;  // 1
    uint32_t count;    // число пресетов
    uint32_t reserved; // FilePreset[count] следом
};
static const uint32_t PRESET_MAGIC = 0x31474D56;       // "VMG1" (LE)
static const uint32_t PRESET_FILE_ADDR = 0x90100000;   // QSPI, далеко от прошивки и хранилища
static_assert(sizeof(FilePreset) == 92, "FilePreset must match make_presets.py (<12s12s2i15f)");
static_assert(sizeof(PresetFileHeader) == 16, "PresetFileHeader must be 16 bytes (<4sIII)");

// Активный банк: указатель + количество (по умолчанию заводской song_library).
static const SongPreset *g_bank = kSongs;
static int g_bankCount = kSongCount;
static SongPreset g_fileSongs[MAX_BANK];
static char g_fileBand[MAX_BANK][12];
static char g_fileSong[MAX_BANK][12];

struct SaveData
{
    uint32_t version;
    uint8_t ovUsed[MAX_BANK];   // 1 = фабричный пресет переопределён сохранением
    StoredPreset ov[MAX_BANK];  // переопределения фабричных
    int customCount;              // сколько custom-пресетов добавлено
    CustomPreset custom[MAX_CUSTOM];
    bool operator!=(const SaveData &o) const { return memcmp(this, &o, sizeof(SaveData)) != 0; }
    bool operator==(const SaveData &o) const { return !(*this != o); }
};
static const uint32_t SAVE_VERSION = 7; // + compAmt/boost в StoredPreset
PersistentStorage<SaveData> storage(hardware.seed.qspi);

// Заводской «пустой» custom-тон (чистый Fender).
static const SongPreset kBlankCustom = {"Custom", "My Tone", "Clean", 1, 3, 0.40f, 1.0f, 0.55f, 0.55f, 0.20f, 0, 0, 0, 0, 0};

static StoredPreset presetToStored(const SongPreset &s)
{
    StoredPreset t;
    t.model = s.ampModelBin; t.ir = s.irBin;
    t.gain = s.gain; t.mix = s.mix; t.level = s.level; t.tone = s.tone;
    t.revMix = s.revMix; t.delayMix = s.delayMix; t.delayTimeMs = s.delayTimeMs;
    t.delayFb = s.delayFb; t.tremDepth = s.tremDepth; t.tremRate = s.tremRate;
    // Новые поля: 0 в song_library => «по умолчанию».
    t.eqLow = s.eqLow > 0.0f ? s.eqLow : 0.5f;
    t.eqMid = s.eqMid > 0.0f ? s.eqMid : 0.5f;
    t.eqHigh = s.eqHigh > 0.0f ? s.eqHigh : 0.5f;
    t.revFb = s.revFb > 0.0f ? s.revFb : 0.84f;
    t.revDamp = s.revDamp > 0.0f ? s.revDamp : 0.35f;
    // Хорус из пресета (0 mix = выкл); phaser/flanger выключены.
    t.chorusMix = s.chorusMix; t.chorusRate = s.chorusRate > 0.0f ? s.chorusRate : 0.4f; t.chorusDepth = s.chorusDepth > 0.0f ? s.chorusDepth : 0.5f;
    t.phaserMix = 0.0f; t.phaserRate = 0.3f; t.phaserDepth = 0.6f; t.phaserFb = 0.3f;
    t.flangerMix = 0.0f; t.flangerRate = 0.2f; t.flangerDepth = 0.5f; t.flangerFb = 0.3f;
    // Компрессор + буст из пресета.
    t.compAmt = s.comp; t.boost = s.boost;
    return t;
}
static int totalPresets() { return g_bankCount + storage.GetSettings().customCount; }

// Загрузить банк пресетов из файла в QSPI (если есть валидный), иначе оставить заводской.
static void loadPresetBank()
{
    const PresetFileHeader *h = reinterpret_cast<const PresetFileHeader *>(PRESET_FILE_ADDR);
    if (h->magic != PRESET_MAGIC || h->version != 1 || h->count == 0 || h->count > (uint32_t)MAX_BANK)
        return; // нет валидного файла -> заводской song_library
    const FilePreset *fp = reinterpret_cast<const FilePreset *>(PRESET_FILE_ADDR + sizeof(PresetFileHeader));
    int n = (int)h->count;
    for (int i = 0; i < n; i++)
    {
        memcpy(g_fileBand[i], fp[i].band, 12); g_fileBand[i][11] = 0;
        memcpy(g_fileSong[i], fp[i].song, 12); g_fileSong[i][11] = 0;
        SongPreset s = {};
        s.band = g_fileBand[i]; s.song = g_fileSong[i];
        s.ampModelBin = fp[i].model; s.irBin = fp[i].ir;
        s.gain = fp[i].gain; s.mix = fp[i].mix; s.level = fp[i].level; s.tone = fp[i].tone;
        s.revMix = fp[i].revMix; s.delayMix = fp[i].delayMix; s.delayTimeMs = fp[i].delayTimeMs;
        s.delayFb = fp[i].delayFb; s.tremDepth = fp[i].tremDepth; s.tremRate = fp[i].tremRate;
        s.eqLow = fp[i].eqLow; s.eqMid = fp[i].eqMid; s.eqHigh = fp[i].eqHigh;
        s.revFb = fp[i].revFb; s.revDamp = fp[i].revDamp;
        g_fileSongs[i] = s;
    }
    g_bank = g_fileSongs;
    g_bankCount = n;
}

// Эффективный пресет для индекса навигации (с учётом override/custom).
static void getEffective(int idx, StoredPreset &out, const char *&band, const char *&song)
{
    SaveData &sd = storage.GetSettings();
    if (idx < g_bankCount)
    {
        band = g_bank[idx].band; song = g_bank[idx].song;
        out = sd.ovUsed[idx] ? sd.ov[idx] : presetToStored(g_bank[idx]);
    }
    else
    {
        CustomPreset &c = sd.custom[idx - g_bankCount];
        band = "Custom"; song = c.name;
        out = c.p;
    }
}

// ---- Группировка тонов в песни ----------------------------------------------
// Пресеты одной песни идут ПОДРЯД (одинаковые band+song). Строим таблицу:
// для каждого плоского индекса — первый тон песни, число тонов и номер песни.
static const int MAX_PRESETS_TOTAL = MAX_BANK + MAX_CUSTOM;
static int g_toneFirst[MAX_PRESETS_TOTAL];      // плоский индекс 1-го тона песни
static int g_toneCount[MAX_PRESETS_TOTAL];      // тонов в песне
static int g_songNo[MAX_PRESETS_TOTAL];         // номер песни (1..g_songCount)
static int g_songFirstList[MAX_PRESETS_TOTAL];  // первый тон каждой песни
static int g_songCount = 0;

static void buildSongTable()
{
    int total = totalPresets();
    if (total > MAX_PRESETS_TOTAL) total = MAX_PRESETS_TOTAL;
    g_songCount = 0;
    int i = 0;
    while (i < total)
    {
        StoredPreset t; const char *b; const char *s;
        getEffective(i, t, b, s);
        int base = i;
        int j = i + 1;
        while (j < total)
        {
            StoredPreset t2; const char *b2; const char *s2;
            getEffective(j, t2, b2, s2);
            if (strcmp(b2, b) == 0 && strcmp(s2, s) == 0) j++;
            else break;
        }
        int cnt = j - base;
        g_songFirstList[g_songCount] = base;
        for (int k = base; k < j; k++)
        {
            g_toneFirst[k] = base;
            g_toneCount[k] = cnt;
            g_songNo[k] = g_songCount + 1;
        }
        g_songCount++;
        i = j;
    }
}

static bool isBinnedKnob(int i) { return i == 4 || i == 5; }

static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    uint32_t t0 = System::GetUs();

    hardware.ProcessAnalogControls();
    hardware.ProcessDigitalControls();

    for (int i = 0; i < KNOBS; i++)
    {
        float raw = hardware.GetKnobValue(i);
        if (!g_knobsInit) { g_knobChanged[i] = false; g_knobRef[i] = raw; g_knobCache[i] = raw; continue; }
        if (g_knobRearm) { g_knobChanged[i] = false; g_knobRef[i] = raw; } // новый пресет/режим — сброс референса
        if (!g_knobChanged[i])
        {
            // «взять» параметр только если пот заметно сдвинули от референса
            if (raw > g_knobRef[i] + KNOB_MOVE || raw < g_knobRef[i] - KNOB_MOVE) { g_knobChanged[i] = true; g_knobCache[i] = raw; }
        }
        else g_knobCache[i] = raw;
    }
    g_knobRearm = false;

    g_encDelta += hardware.encoders[0].Increment();
    if (hardware.encoders[0].RisingEdge()) g_encPress++;

    bool tuner = (g_mode == MODE_TUNER);

    for (size_t i = 0; i < size; i++)
    {
        // В режиме тюнера: собираем вход (с прореживанием) и пропускаем сухой звук,
        // НЕ гоняя усилитель -> освобождаем CPU под детектор питча.
        if (tuner)
        {
            // ФНЧ ~1.2 кГц перед прореживанием — убирает алиасинг гармоник.
            g_tunerLp += 0.16f * (in[0][i] - g_tunerLp);
            if (++g_tunerDecimCnt >= TUNER_DECIM)
            {
                g_tunerDecimCnt = 0;
                g_tunerBuf[g_tunerIdx] = g_tunerLp;
                if (++g_tunerIdx >= TUNER_N) g_tunerIdx = 0;
            }
            out[0][i] = in[0][i];
            out[1][i] = in[1][i];
            continue;
        }

        if (g_muteCountdown > 0) { g_muteCountdown--; out[0][i] = 0.0f; out[1][i] = 0.0f; continue; }

        if (g_effectOn)
        {
            float dry = in[0][i];
            float av = fabsf(dry);
            if (av > g_gateEnv) g_gateEnv = av; else g_gateEnv *= 0.99985f;
            if (g_gateEnv > GATE_THRESH) g_gateHold = g_gateHoldSamples;
            float target = (g_gateHold > 0) ? 1.0f : 0.0f;
            if (g_gateHold > 0) g_gateHold--;
            float coef = (target > g_gate) ? 0.03f : 0.00012f;
            g_gate += (target - g_gate) * coef;

            // Компрессор (до усилителя) — плотный funk/clean.
            if (g_compAmt > 0.001f)
            {
                float lvl = fabsf(dry);
                g_compEnv += (lvl - g_compEnv) * (lvl > g_compEnv ? 0.01f : 0.0008f);
                float thresh = 0.12f, cg = 1.0f;
                if (g_compEnv > thresh)
                {
                    float ratio = 1.0f + g_compAmt * 7.0f;
                    float outLvl = thresh + (g_compEnv - thresh) / ratio;
                    cg = outLvl / g_compEnv;
                }
                dry = dry * cg * (1.0f + g_compAmt * 1.5f); // + makeup
            }
            // Буст/overdrive (до усилителя) — для соло.
            if (g_boost > 0.001f)
            {
                dry *= 1.0f + g_boost * 4.0f;
                if (dry > 1.0f) dry = 1.0f; else if (dry < -1.0f) dry = -1.0f;
                dry -= (dry * dry * dry) * (0.15f * g_boost); // мягкое ограничение
            }

            amp.ProcessMono(dry);
            float a = amp.GetAudioLeft() * g_gate;

            // 3-полосный EQ (прозрачен при положениях 0.5/0.5/0.5).
            a = eq3.Process(a, g_eqLow * 2.0f, g_eqMid * 2.0f, g_eqHigh * 2.0f);

            if (g_tremOn) a = tremolo.Process(a);

            // Модуляция (только если включена по миксу) — после усилителя, до delay/reverb.
            if (g_chorusMix > 0.001f)  { float w = chorus.Process(a);  a = a * (1.0f - g_chorusMix)  + w * g_chorusMix; }
            if (g_phaserMix > 0.001f)  { float w = phaser.Process(a);  a = a * (1.0f - g_phaserMix)  + w * g_phaserMix; }
            if (g_flangerMix > 0.001f) { float w = flanger.Process(a); a = a * (1.0f - g_flangerMix) + w * g_flangerMix; }

            if (g_delayMix > 0.0001f || g_delayFb > 0.0001f)
            {
                int rp = g_delayWrite - g_delaySamples;
                if (rp < 0) rp += DELAY_BUF;
                float dl = g_delayBuf[rp];
                g_delayLp += 0.45f * (dl - g_delayLp); // тёмное «аналоговое» эхо
                g_delayBuf[g_delayWrite] = a + g_delayLp * g_delayFb;
                if (++g_delayWrite >= DELAY_BUF) g_delayWrite = 0;
                a = a + g_delayLp * g_delayMix;
            }
            if (g_revMix > 0.0001f)
            {
                float w = reverb.Process(a);
                a = a * (1.0f - g_revMix) + w * g_revMix;
            }
            if (a > 1.0f) a = 1.0f; else if (a < -1.0f) a = -1.0f;

            // ЛУПЕР (free-form) + независимый МЕТРОНОМ.
            if (g_looperEngaged)
            {
                // --- петля: запись/микс (запись СУХОГО a до микса и до клика) ---
                bool running = (g_loopLen > 0) || (g_loopState[g_loopActive] == 1);
                if (running)
                {
                    int pos = g_loopPos;
                    float mixv = a;
                    for (int t = 0; t < LOOP_TRACKS; t++)
                        if (g_loopState[t] == 2) mixv += g_loop[pos][t] * g_loopVol[t];
                    if (g_loopState[g_loopActive] == 1) g_loop[pos][g_loopActive] = a; // запись сухого
                    a = mixv * 0.7f;
                    int np = pos + 1;
                    if (g_loopLen > 0) { if (np >= g_loopLen) np = 0; }
                    else if (np >= LOOP_LEN) np = LOOP_LEN - 1;
                    g_loopPos = np;
                }
                // --- бит-грид (крутится всегда: нужен метроному И count-in) ---
                bool countingIn = (g_countInLeft > 0);
                bool beatTick = false;
                if (++g_metCounter >= g_beatSamples)
                {
                    g_metCounter = 0;
                    g_beatShow = (g_beatShow + 1) % g_beatsPerBar;
                    g_clickAccent = (g_beatShow == 0);
                    beatTick = true;
                }
                // count-in: на каждой доле уменьшаем; на нуле — старт записи
                if (countingIn && beatTick)
                {
                    if (--g_countInLeft == 0)
                    {
                        if (g_loopLen == 0) g_loopPos = 0;
                        g_loopState[g_loopActive] = 1; // старт записи ровно на долю
                    }
                }
                // клик + LED звучат когда метроном вкл ИЛИ идёт count-in
                if (beatTick && (g_metOn || countingIn)) { g_clickEnv = g_clickLen; g_ledBeat = g_beatSamples / 3; }
                if (g_clickEnv > 0)
                {
                    float env = (float)g_clickEnv / (float)g_clickLen;
                    float f = g_clickAccent ? 1600.0f : 1000.0f;
                    g_clickPhase += f / g_sampleRate;
                    if (g_clickPhase >= 1.0f) g_clickPhase -= 1.0f;
                    a += sinf(6.2831853f * g_clickPhase) * env * g_clickVol * (g_clickAccent ? 1.0f : 0.65f);
                    g_clickEnv--;
                }
                if (a > 1.0f) a = 1.0f; else if (a < -1.0f) a = -1.0f;
            }

            out[0][i] = a;
            out[1][i] = a;
        }
        else { out[0][i] = in[0][i]; out[1][i] = in[1][i]; }
    }

    if (g_ledBeat > 0) { g_ledBeat -= (int)size; if (g_ledBeat < 0) g_ledBeat = 0; }
    bool looperRec = false;
    if (g_looperEngaged)
        for (int t = 0; t < LOOP_TRACKS; t++) if (g_loopState[t] == 1) looperRec = true;
    if (looperRec)
        hardware.SetLed(0, 1.0f); // «запись» — сплошной свет
    else if (g_looperEngaged && g_ledBeat > 0)
        hardware.SetLed(0, g_clickAccent ? 1.0f : 0.45f); // мигание на долю (ярче на «раз»)
    else
        hardware.SetLed(0, g_effectOn ? 1.0f : 0.0f);
    hardware.SetLed(1, 0.0f); // LED меню выключен (был слишком яркий)
    hardware.UpdateLeds();

    g_cpuLoad = (System::GetUs() - t0) * 0.1f;
}

// Применить пресет (с учётом сохранённой правки / custom).
static void applySong(int idx)
{
    StoredPreset t; const char *b; const char *s;
    getEffective(idx, t, b, s);
    g_muteCountdown = g_switchMuteSamples;

    amp.SetParameterAsBinnedValue(P_MODEL, t.model);
    amp.SetParameterAsBinnedValue(P_IR, t.ir);
    amp.SetParameterAsMagnitude(P_GAIN, t.gain);
    amp.SetParameterAsMagnitude(P_MIX, t.mix);
    amp.SetParameterAsMagnitude(P_LEVEL, t.level);
    amp.SetParameterAsMagnitude(P_TONE, t.tone);
    g_revMix = t.revMix; g_delayMix = t.delayMix; g_delayFb = t.delayFb; g_tremDepth = t.tremDepth;

    float dms = t.delayTimeMs; if (dms > DELAY_MAX_MS) dms = DELAY_MAX_MS;
    // Если время delay в пресете не задано — дефолт 300 мс, чтобы Edit FX delay (правит микс)
    // давал слышимое эхо на любом пресете, а не «гребёнку» в 1 сэмпл.
    g_delayTimeMs = (dms > 0.0f) ? dms : 300.0f;
    int ds = (int)(g_delayTimeMs * 0.001f * g_sampleRate);
    if (ds < 1) ds = 1;
    if (ds >= DELAY_BUF) ds = DELAY_BUF - 1;
    g_delaySamples = ds;

    g_tremRate = t.tremRate > 0.0f ? t.tremRate : 5.0f;
    tremolo.SetDepth(g_tremDepth);
    tremolo.SetFreq(g_tremRate);
    g_tremOn = g_tremDepth > 0.001f;

    // Реверб decay/damp из пресета.
    g_revFb = t.revFb; g_revDamp = t.revDamp; reverb.fb = g_revFb; reverb.damp = g_revDamp;

    // EQ из пресета.
    g_eqLow = t.eqLow; g_eqMid = t.eqMid; g_eqHigh = t.eqHigh;

    // Модуляция из пресета.
    g_chorusMix = t.chorusMix; g_chorusRate = t.chorusRate; g_chorusDepth = t.chorusDepth;
    g_phaserMix = t.phaserMix; g_phaserRate = t.phaserRate; g_phaserDepth = t.phaserDepth; g_phaserFb = t.phaserFb;
    g_flangerMix = t.flangerMix; g_flangerRate = t.flangerRate; g_flangerDepth = t.flangerDepth; g_flangerFb = t.flangerFb;
    g_compAmt = t.compAmt; g_boost = t.boost;
    applyModParams();

    g_knobRearm = true;
}

// Переключить ТОН внутри текущей песни (футсвитчи +/-, с заворотом).
static void gotoTone(int delta)
{
    if (g_currentSong < 0 || g_currentSong >= MAX_PRESETS_TOTAL) return;
    int base = g_toneFirst[g_currentSong];
    int cnt = g_toneCount[g_currentSong];
    if (cnt < 1) cnt = 1;
    int ti = g_currentSong - base + delta;
    while (ti < 0) ti += cnt;
    while (ti >= cnt) ti -= cnt;
    g_currentSong = base + ti;
    applySong(g_currentSong);
}

// Переключить ПЕСНЮ (энкодер), встаём на её первый тон.
static void gotoSong(int delta)
{
    if (g_songCount <= 0) return;
    int no = g_songNo[g_currentSong] - 1 + delta;
    while (no < 0) no += g_songCount;
    while (no >= g_songCount) no -= g_songCount;
    g_currentSong = g_songFirstList[no];
    applySong(g_currentSong);
}

// Снять текущие живые параметры в StoredPreset.
static StoredPreset captureCurrent()
{
    StoredPreset t;
    t.model = amp.GetParameterAsBinnedValue(P_MODEL);
    t.ir = amp.GetParameterAsBinnedValue(P_IR);
    t.gain = amp.GetParameterAsMagnitude(P_GAIN);
    t.mix = amp.GetParameterAsMagnitude(P_MIX);
    t.level = amp.GetParameterAsMagnitude(P_LEVEL);
    t.tone = amp.GetParameterAsMagnitude(P_TONE);
    t.revMix = g_revMix; t.delayMix = g_delayMix; t.delayFb = g_delayFb; t.tremDepth = g_tremDepth;
    t.delayTimeMs = g_delayTimeMs; t.tremRate = g_tremRate; // теперь правятся в Edit FX
    // Новые эффекты — снимаем живые значения.
    t.eqLow = g_eqLow; t.eqMid = g_eqMid; t.eqHigh = g_eqHigh;
    t.revFb = g_revFb; t.revDamp = g_revDamp;
    t.chorusMix = g_chorusMix; t.chorusRate = g_chorusRate; t.chorusDepth = g_chorusDepth;
    t.phaserMix = g_phaserMix; t.phaserRate = g_phaserRate; t.phaserDepth = g_phaserDepth; t.phaserFb = g_phaserFb;
    t.flangerMix = g_flangerMix; t.flangerRate = g_flangerRate; t.flangerDepth = g_flangerDepth; t.flangerFb = g_flangerFb;
    t.compAmt = g_compAmt; t.boost = g_boost;
    return t;
}

// Сохранить текущий пресет (фабричный override или custom-слот).
static void saveCurrentSong()
{
    SaveData &sd = storage.GetSettings();
    StoredPreset t = captureCurrent();
    if (g_currentSong < g_bankCount) { sd.ov[g_currentSong] = t; sd.ovUsed[g_currentSong] = 1; }
    else { sd.custom[g_currentSong - g_bankCount].p = t; }
    storage.Save();
}

// Сброс ТЕКУЩЕГО пресета к заводскому (Edit FX -> Default).
static void resetCurrentPreset()
{
    SaveData &sd = storage.GetSettings();
    if (g_currentSong < g_bankCount) sd.ovUsed[g_currentSong] = 0;
    else sd.custom[g_currentSong - g_bankCount].p = presetToStored(kBlankCustom);
    storage.Save();
    applySong(g_currentSong);
}

// Добавить новый custom-пресет (клон текущего звука как старт).
static void addCustomPreset()
{
    SaveData &sd = storage.GetSettings();
    if (sd.customCount >= MAX_CUSTOM) return;
    CustomPreset &c = sd.custom[sd.customCount];
    snprintf(c.name, sizeof(c.name), "Slot %d", sd.customCount + 1);
    c.p = captureCurrent();
    sd.customCount++;
    storage.Save();
    buildSongTable();
    g_currentSong = g_bankCount + sd.customCount - 1;
    applySong(g_currentSong);
}

// Удалить ТЕКУЩИЙ custom-пресет (если сейчас выбран custom-слот). Возвращает true.
static bool deleteCustomPreset()
{
    SaveData &sd = storage.GetSettings();
    if (g_currentSong < g_bankCount) return false; // выбран фабричный — удалять нечего
    int ci = g_currentSong - g_bankCount;
    if (ci < 0 || ci >= sd.customCount) return false;
    for (int i = ci; i < sd.customCount - 1; i++) sd.custom[i] = sd.custom[i + 1]; // сдвиг
    sd.customCount--;
    storage.Save();
    buildSongTable();
    if (g_currentSong >= totalPresets()) g_currentSong = totalPresets() - 1;
    if (g_currentSong < 0) g_currentSong = 0;
    applySong(g_currentSong);
    return true;
}

// Фабричный сброс ВСЕГО (главное меню -> Default): убрать все правки и custom.
static void factoryResetAll()
{
    SaveData &sd = storage.GetSettings();
    for (int i = 0; i < g_bankCount; i++) sd.ovUsed[i] = 0;
    sd.customCount = 0;
    storage.Save();
    buildSongTable();
    if (g_currentSong >= totalPresets()) g_currentSong = 0;
    applySong(g_currentSong);
}

// ---- Лупер / метроном: управление ----
// Пересчёт размеров клика из BPM (метроном независимый; длину петли НЕ трогаем).
static void recomputeTempo()
{
    if (g_bpm < 40) g_bpm = 40; if (g_bpm > 300) g_bpm = 300;
    if (g_beatsPerBar < 1) g_beatsPerBar = 1; if (g_beatsPerBar > 8) g_beatsPerBar = 8;
    float spb = (60.0f / (float)g_bpm) * g_sampleRate;
    int bs = (int)(spb + 0.5f); if (bs < 1) bs = 1;
    g_beatSamples = bs;
    g_clickLen = bs / 10; if (g_clickLen < 240) g_clickLen = 240; if (g_clickLen > 4000) g_clickLen = 4000;
}
// Правый ФС: цикл как раньше (free-form) — пусто→[count-in]→запись→игра→mute→игра.
static void looperRightCycle()
{
    int a = g_loopActive;
    if (g_countInLeft > 0) { g_countInLeft = 0; return; } // повторное нажатие — отмена отсчёта
    uint8_t st = g_loopState[a];
    if (st == 0)
    {
        g_loopTone[a] = g_currentSong;
        if (g_countInOn && g_metOn) // count-in только если метроном включён
        {
            g_metCounter = 0;
            g_beatShow = g_beatsPerBar - 1; // следующая доля будет «раз»
            g_countInLeft = g_beatsPerBar;  // такт отсчёта, потом старт записи (в аудио-потоке)
        }
        else { if (g_loopLen == 0) g_loopPos = 0; g_loopState[a] = 1; } // сразу запись
    }
    else if (st == 1) { if (g_loopLen == 0) { g_loopLen = g_loopPos; g_loopPos = 0; } g_loopState[a] = 2; } // запись -> игра
    else if (st == 2) g_loopState[a] = 3;  // игра -> mute
    else g_loopState[a] = 2;               // mute -> игра
}
static void looperClearActive()
{
    g_loopState[g_loopActive] = 0;
    bool any = false;
    for (int t = 0; t < LOOP_TRACKS; t++) if (g_loopState[t] != 0) any = true;
    if (!any) { g_loopLen = 0; g_loopPos = 0; } // все пусты -> сброс длины
}

// ---- Edit FX: страницы эффектов; 6 потов правят параметры текущей страницы ----
enum EditPage { PG_AMP, PG_EQ, PG_REVERB, PG_DELAY, PG_TREMOLO, PG_CHORUS, PG_PHASER, PG_FLANGER, PG_COMP, PG_BOOST, PG_COUNT };
static const char *kPageNames[PG_COUNT] = {"Amp", "EQ", "Reverb", "Delay", "Tremolo", "Chorus", "Phaser", "Flanger", "Comp", "Boost"};

static int editParamCount(int pg)
{
    switch (pg)
    {
    case PG_AMP: return 2;
    case PG_EQ: return 3;
    case PG_REVERB: return 3;
    case PG_DELAY: return 3;
    case PG_TREMOLO: return 2;
    case PG_CHORUS: return 3;
    case PG_PHASER: return 4;
    case PG_FLANGER: return 4;
    case PG_COMP: return 1;
    case PG_BOOST: return 1;
    }
    return 0;
}
static const char *editParamName(int pg, int idx)
{
    static const char *amp_[] = {"Gain", "Tone"};
    static const char *eq_[] = {"Low", "Mid", "High"};
    static const char *rev_[] = {"Mix", "Decay", "Damp"};
    static const char *dly_[] = {"Mix", "Time", "Fdbk"};
    static const char *trm_[] = {"Depth", "Rate"};
    static const char *cho_[] = {"Mix", "Rate", "Depth"};
    static const char *pha_[] = {"Mix", "Rate", "Depth", "Fdbk"};
    static const char *fla_[] = {"Mix", "Rate", "Depth", "Fdbk"};
    static const char *cmp_[] = {"Amount"};
    static const char *bst_[] = {"Amount"};
    switch (pg)
    {
    case PG_COMP: return cmp_[idx];
    case PG_BOOST: return bst_[idx];
    case PG_AMP: return amp_[idx];
    case PG_EQ: return eq_[idx];
    case PG_REVERB: return rev_[idx];
    case PG_DELAY: return dly_[idx];
    case PG_TREMOLO: return trm_[idx];
    case PG_CHORUS: return cho_[idx];
    case PG_PHASER: return pha_[idx];
    case PG_FLANGER: return fla_[idx];
    }
    return "";
}
// Значение параметра как 0..1 (для отрисовки полоски).
static float editParamGet(int pg, int idx)
{
    switch (pg)
    {
    case PG_AMP: return idx == 0 ? amp.GetParameterAsMagnitude(P_GAIN) : amp.GetParameterAsMagnitude(P_TONE);
    case PG_EQ: return idx == 0 ? g_eqLow : idx == 1 ? g_eqMid : g_eqHigh;
    case PG_REVERB: return idx == 0 ? g_revMix : idx == 1 ? (g_revFb - 0.6f) / 0.39f : (g_revDamp - 0.1f) / 0.5f;
    case PG_DELAY: return idx == 0 ? g_delayMix : idx == 1 ? (g_delayTimeMs - 20.0f) / 880.0f : (g_delayFb / 0.9f);
    case PG_TREMOLO: return idx == 0 ? g_tremDepth : (g_tremRate - 1.0f) / 9.0f;
    case PG_CHORUS: return idx == 0 ? g_chorusMix : idx == 1 ? g_chorusRate : g_chorusDepth;
    case PG_PHASER: return idx == 0 ? g_phaserMix : idx == 1 ? g_phaserRate : idx == 2 ? g_phaserDepth : g_phaserFb;
    case PG_FLANGER: return idx == 0 ? g_flangerMix : idx == 1 ? g_flangerRate : idx == 2 ? g_flangerDepth : g_flangerFb;
    case PG_COMP: return g_compAmt;
    case PG_BOOST: return g_boost;
    }
    return 0.0f;
}
// Применить значение пота 0..1 к параметру страницы.
static void editParamApply(int pg, int idx, float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    switch (pg)
    {
    case PG_AMP:
        if (idx == 0) amp.SetParameterAsMagnitude(P_GAIN, v); else amp.SetParameterAsMagnitude(P_TONE, v);
        break;
    case PG_EQ:
        if (idx == 0) g_eqLow = v; else if (idx == 1) g_eqMid = v; else g_eqHigh = v;
        break;
    case PG_REVERB:
        if (idx == 0) g_revMix = v;
        else if (idx == 1) { g_revFb = 0.6f + v * 0.39f; reverb.fb = g_revFb; }
        else { g_revDamp = 0.1f + v * 0.5f; reverb.damp = g_revDamp; }
        break;
    case PG_DELAY:
        if (idx == 0) g_delayMix = v;
        else if (idx == 1) { g_delayTimeMs = 20.0f + v * 880.0f; int ds = (int)(g_delayTimeMs * 0.001f * g_sampleRate); if (ds < 1) ds = 1; if (ds >= DELAY_BUF) ds = DELAY_BUF - 1; g_delaySamples = ds; }
        else g_delayFb = v * 0.9f;
        break;
    case PG_TREMOLO:
        if (idx == 0) { g_tremDepth = v; tremolo.SetDepth(v); g_tremOn = v > 0.001f; }
        else { g_tremRate = 1.0f + v * 9.0f; tremolo.SetFreq(g_tremRate); }
        break;
    case PG_CHORUS:
        if (idx == 0) g_chorusMix = v;
        else if (idx == 1) { g_chorusRate = v; chorus.SetLfoFreq(0.1f + v * 4.0f); }
        else { g_chorusDepth = v; chorus.SetLfoDepth(v); }
        break;
    case PG_PHASER:
        if (idx == 0) g_phaserMix = v;
        else if (idx == 1) { g_phaserRate = v; phaser.SetLfoFreq(0.1f + v * 4.0f); }
        else if (idx == 2) { g_phaserDepth = v; phaser.SetLfoDepth(v); }
        else { g_phaserFb = v; phaser.SetFeedback(v); }
        break;
    case PG_FLANGER:
        if (idx == 0) g_flangerMix = v;
        else if (idx == 1) { g_flangerRate = v; flanger.SetLfoFreq(0.05f + v * 2.0f); }
        else if (idx == 2) { g_flangerDepth = v; flanger.SetLfoDepth(v); }
        else { g_flangerFb = v; flanger.SetFeedback(v); }
        break;
    case PG_COMP: g_compAmt = v; break;
    case PG_BOOST: g_boost = v; break;
    }
}

// ---- Отрисовка ----
static void writeCentered(const char *s, int y, FontDef font, bool on)
{
    int w = (int)strlen(s) * font.FontWidth;
    int x = (128 - w) / 2; if (x < 0) x = 0;
    hardware.display.SetCursor(x, y);
    hardware.display.WriteString(s, font, on);
}
static void drawBar(int y, float v)
{
    if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;
    int bx1 = 6, bx2 = 122;
    hardware.display.DrawRect(bx1, y, bx2, y + 12, true, false);
    int w = (int)((bx2 - bx1 - 2) * v);
    if (w > 0) hardware.display.DrawRect(bx1 + 1, y + 1, bx1 + 1 + w, y + 11, true, true);
}

// Общий стиль экранов: инверсная плашка с заголовком по центру (без рамки по периметру).
static void drawHeader(const char *title)
{
    hardware.display.DrawRect(0, 0, 127, 11, true, true); // плашка
    int w = (int)strlen(title) * 7;
    hardware.display.SetCursor((128 - w) / 2, 1);
    hardware.display.WriteString(title, Font_7x10, false); // тёмный текст на светлой плашке
}

// Имя тона (Clean/Crunch/Lead...) для плоского индекса; "" у custom-слотов.
static const char *getPartName(int idx)
{
    if (idx >= 0 && idx < g_bankCount) return g_bank[idx].part ? g_bank[idx].part : "";
    return "";
}

static void drawPlay()
{
    char line[40];
    StoredPreset t; const char *band; const char *song;
    getEffective(g_currentSong, t, band, song);

    hardware.display.Fill(false);

    // Инверсная шапка: № песни / CPU / ON (рамки по периметру нет).
    hardware.display.DrawRect(0, 0, 127, 11, true, true);
    int songNo = (g_currentSong >= 0 && g_currentSong < MAX_PRESETS_TOTAL) ? g_songNo[g_currentSong] : 1;
    snprintf(line, sizeof(line), "%d/%d", songNo, g_songCount);
    hardware.display.SetCursor(3, 1);
    hardware.display.WriteString(line, Font_7x10, false);
    snprintf(line, sizeof(line), "%d%%", (int)g_cpuLoad);
    hardware.display.SetCursor((128 - (int)strlen(line) * 7) / 2, 1);
    hardware.display.WriteString(line, Font_7x10, false);
    const char *st = g_effectOn ? "ON" : "BYP";
    hardware.display.SetCursor(124 - (int)strlen(st) * 7, 1);
    hardware.display.WriteString(st, Font_7x10, false);

    // Исполнитель — крупно, под ним — песня.
    writeCentered(band, 14, Font_11x18, true);
    writeCentered(song, 35, Font_7x10, true);

    // Позиция тона в песне.
    int base = (g_currentSong >= 0 && g_currentSong < MAX_PRESETS_TOTAL) ? g_toneFirst[g_currentSong] : g_currentSong;
    int tcnt = (g_currentSong >= 0 && g_currentSong < MAX_PRESETS_TOTAL) ? g_toneCount[g_currentSong] : 1;
    if (tcnt < 1) tcnt = 1;
    int tno = g_currentSong - base + 1;

    // Табы тонов снизу: имя тона (Font_6x8), активный залит.
    {
        const int bx = 1, bw = 126, by0 = 50, by1 = 62, gap = 2;
        int segW = (bw - (tcnt - 1) * gap) / tcnt;
        if (segW < 6) segW = 6;
        for (int k = 0; k < tcnt; k++)
        {
            int sx = bx + k * (segW + gap);
            bool act = (k == tno - 1);
            hardware.display.DrawRect(sx, by0, sx + segW, by1, true, act);
            char tb[12];
            const char *lbl = getPartName(base + k);
            if (!lbl[0]) { snprintf(tb, sizeof(tb), "%d", k + 1); lbl = tb; }
            int lw = (int)strlen(lbl) * 6;
            int tx = sx + (segW - lw) / 2 + 1;
            if (tx < sx + 1) tx = sx + 1;
            hardware.display.SetCursor(tx, 53);
            hardware.display.WriteString(lbl, Font_6x8, !act);
        }
    }

    hardware.display.Update();
}

static const int MENU_COUNT = 10;
static const char *kMenuItems[MENU_COUNT] = {"Tuner", "Edit FX", "Looper", "Tempo", "Save Preset", "Reset Preset", "Default All", "Custom Preset", "System", "Exit"};
// Плашка-заголовок без рамки по периметру (для MENU / подменю).
static void drawTitleBar(const char *title)
{
    hardware.display.DrawRect(0, 0, 127, 11, true, true);
    int w = (int)strlen(title) * 7;
    hardware.display.SetCursor((128 - w) / 2, 1);
    hardware.display.WriteString(title, Font_7x10, false);
}

static void drawMenu(int sel)
{
    hardware.display.Fill(false);
    drawTitleBar("MENU"); // без рамки по периметру
    const int VIS = 5; // видимых строк под шапкой (окно прокрутки)
    int top = sel - VIS / 2;
    if (top > MENU_COUNT - VIS) top = MENU_COUNT - VIS;
    if (top < 0) top = 0;
    for (int r = 0; r < VIS && top + r < MENU_COUNT; r++)
    {
        int i = top + r;
        int y = 14 + r * 10;
        bool s = (i == sel);
        if (s) hardware.display.DrawRect(3, y - 1, 117, y + 9, true, true);
        hardware.display.SetCursor(7, y);
        hardware.display.WriteString(kMenuItems[i], Font_7x10, !s);
    }
    // Скроллбар справа.
    int trackTop = 13, trackH = 61 - trackTop;
    int thumbH = trackH * VIS / MENU_COUNT;
    int thumbY = trackTop + trackH * top / MENU_COUNT;
    hardware.display.DrawRect(120, thumbY, 124, thumbY + thumbH, true, true);
    hardware.display.Update();
}

// Экран System: настройки LCD (Contrast — реальный, Brightness — демо) + Back.
// sel: 0=Contrast, 1=Brightness, 2=Back. edit: правим значение выбранного.
static void drawSystem(int sel, bool edit)
{
    hardware.display.Fill(false);
    drawHeader("SYSTEM");
    const char *names[2] = {"Contrast", "Brightness"};
    float vals[2] = {g_contrast / 63.0f, g_brightness / 100.0f};
    for (int i = 0; i < 2; i++)
    {
        int by = 15 + i * 13;
        bool s = (sel == i);
        if (s) hardware.display.DrawRect(3, by - 1, 67, by + 8, true, edit); // залито если правим
        hardware.display.SetCursor(6, by);
        hardware.display.WriteString(names[i], Font_6x8, !(s && edit));
        int bx1 = 70, bx2 = 122, bh = 9;
        hardware.display.DrawRect(bx1, by - 1, bx2, by - 1 + bh, true, false);
        int w = (int)((bx2 - bx1 - 2) * vals[i]);
        if (w > 0) hardware.display.DrawRect(bx1 + 1, by, bx1 + 1 + w, by - 1 + bh - 1, true, true);
    }
    // Back
    int by = 15 + 2 * 13;
    if (sel == 2) hardware.display.DrawRect(3, by - 1, 124, by + 8, true, false);
    hardware.display.SetCursor(6, by);
    hardware.display.WriteString("Back", Font_6x8, true);
    hardware.display.Update();
}

// Подменю Custom Preset: Add / Delete Preset / Back.
static const int CUST_COUNT = 3;
static const char *kCustItems[CUST_COUNT] = {"Add", "Delete Preset", "Back"};
static void drawCustomMenu(int sel)
{
    hardware.display.Fill(false);
    drawTitleBar("CUSTOM PRESET");
    for (int i = 0; i < CUST_COUNT; i++)
    {
        int y = 16 + i * 12;
        bool s = (i == sel);
        if (s) hardware.display.DrawRect(3, y - 1, 124, y + 9, true, true);
        hardware.display.SetCursor(7, y);
        hardware.display.WriteString(kCustItems[i], Font_7x10, !s);
    }
    hardware.display.Update();
}

// Экран Tempo: BPM / Sig / Bars / Click / Metro / Back. sel + edit как в System.
static const int TEMPO_ITEMS = 6;
static void drawTempo(int sel, bool edit)
{
    char v0[16], v1[16], v2[16], v3[16], v4[16];
    snprintf(v0, sizeof(v0), "BPM     %d", g_bpm);
    snprintf(v1, sizeof(v1), "Sig     %d/4", g_beatsPerBar);
    snprintf(v2, sizeof(v2), "Click   %d%%", (int)(g_clickVol * 100.0f + 0.5f));
    snprintf(v3, sizeof(v3), "Metro   %s", g_metOn ? "ON" : "OFF");
    snprintf(v4, sizeof(v4), "CountIn %s", g_countInOn ? "ON" : "OFF");
    const char *rows[TEMPO_ITEMS] = {v0, v1, v2, v3, v4, "Back"};
    hardware.display.Fill(false);
    drawTitleBar("TEMPO");
    for (int i = 0; i < TEMPO_ITEMS; i++)
    {
        int y = 14 + i * 8;
        bool s = (i == sel);
        if (s) hardware.display.DrawRect(3, y - 1, 124, y + 7, true, edit);
        hardware.display.SetCursor(6, y);
        hardware.display.WriteString(rows[i], Font_6x8, !(s && edit));
    }
    hardware.display.Update();
}

static void drawTuner()
{
    hardware.display.Fill(false);
    drawHeader("TUNER");
    static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    if (g_tunerFreq < 1.0f)
    {
        writeCentered("- play a string -", 30, Font_7x10, true);
        hardware.display.Update();
        return;
    }

    float midi = 69.0f + 12.0f * log2f(g_tunerFreq / 440.0f);
    int n = (int)(midi + 0.5f);
    float cents = (midi - n) * 100.0f;
    int note = ((n % 12) + 12) % 12;
    int oct = n / 12 - 1;

    char nm[8];
    snprintf(nm, sizeof(nm), "%s%d", names[note], oct);
    writeCentered(nm, 12, Font_16x26, true);

    // Шкала центов: рамка, центральная метка, «игла».
    int bx1 = 8, bx2 = 119, by = 40, bh = 9;
    hardware.display.DrawRect(bx1, by, bx2, by + bh, true, false);
    int cx = (bx1 + bx2) / 2;
    hardware.display.DrawLine(cx, by - 2, cx, by + bh + 2, true); // центр (строй)
    float c = cents; if (c < -50) c = -50; if (c > 50) c = 50;
    int nx = cx + (int)((bx2 - bx1 - 4) * 0.5f * (c / 50.0f));
    hardware.display.DrawRect(nx - 2, by + 1, nx + 2, by + bh - 1, true, true);

    char ln[24];
    snprintf(ln, sizeof(ln), "%+dc  %dHz", (int)cents, (int)g_tunerFreq);
    writeCentered(ln, 52, Font_7x10, true);
    hardware.display.Update();
}

// Страница Edit FX: имя эффекта + параметры (имя ручки + полоска значения).
static void drawEditPage(int pg)
{
    char line[24];
    hardware.display.Fill(false);
    snprintf(line, sizeof(line), "%s  %d/%d", kPageNames[pg], pg + 1, PG_COUNT);
    drawHeader(line);

    int n = editParamCount(pg);
    for (int i = 0; i < n; i++)
    {
        int y = 15 + i * 12;
        float v = editParamGet(pg, i);
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        snprintf(line, sizeof(line), "K%d %s", i + 1, editParamName(pg, i));
        hardware.display.SetCursor(4, y);
        hardware.display.WriteString(line, Font_7x10, true);
        int bx1 = 54, bx2 = 94;
        hardware.display.DrawRect(bx1, y, bx2, y + 8, true, false);
        int w = (int)((bx2 - bx1 - 2) * v);
        if (w > 0) hardware.display.DrawRect(bx1 + 1, y + 1, bx1 + 1 + w, y + 7, true, true);
        snprintf(line, sizeof(line), "%d", (int)(v * 100.0f + 0.5f));
        hardware.display.SetCursor(98, y);
        hardware.display.WriteString(line, Font_7x10, true);
    }
    hardware.display.Update();
}

static void drawLooper(int field)
{
    static const char *ftag[5] = {"TRK", "MET", "BPM", "VOL", "POT"}; // что крутит энкодер
    char line[40];
    hardware.display.Fill(false);
    // Экран отсчёта перед записью (count-in).
    if (g_countInLeft > 0)
    {
        writeCentered("REC IN", 8, Font_11x18, true);
        char b[8]; snprintf(b, sizeof(b), "%d", g_countInLeft);
        writeCentered(b, 32, Font_16x26, true);
        hardware.display.Update();
        return;
    }
    // шапка: LOOP + beat-точки (если метроном вкл) + поле энкодера
    hardware.display.DrawRect(0, 0, 127, 11, true, true);
    hardware.display.SetCursor(3, 1);
    hardware.display.WriteString("LOOP", Font_7x10, false);
    if (g_metOn)
        for (int b = 0; b < g_beatsPerBar && b < 8; b++)
        {
            int bx = 40 + b * 7;
            hardware.display.DrawRect(bx, 3, bx + 5, 8, false, (b == g_beatShow));
        }
    hardware.display.SetCursor(124 - 3 * 7, 1);
    hardware.display.WriteString(ftag[field], Font_7x10, false);

    // строка: метроном + режим потов
    if (g_metOn) snprintf(line, sizeof(line), "Met %d v%d%%", g_bpm, (int)(g_clickVol * 100.0f + 0.5f));
    else snprintf(line, sizeof(line), "Met OFF");
    hardware.display.SetCursor(3, 13);
    hardware.display.WriteString(line, Font_6x8, true);
    bool potsVol = (g_potMode == 1) || (g_loopState[g_loopActive] == 1);
    hardware.display.SetCursor(104, 13);
    hardware.display.WriteString(potsVol ? "Vol" : "Ton", Font_6x8, true);

    // 4 дорожки. Транспорт-бар: заливка=запись, бегущий маркер=игра, "mute"=стоп.
    float prog = (g_loopLen > 0) ? (float)g_loopPos / (float)g_loopLen
                                 : (float)g_loopPos / (float)LOOP_LEN; // общая позиция круга
    if (prog < 0) prog = 0; if (prog > 1) prog = 1;
    for (int t = 0; t < LOOP_TRACKS; t++)
    {
        int y = 23 + t * 10;
        uint8_t st = g_loopState[t]; if (st > 3) st = 2;
        bool act = (t == g_loopActive);
        bool col = act ? false : true; // цвет графики: тёмный на подсвеченной строке
        if (act) hardware.display.DrawRect(1, y - 1, 126, y + 8, true, true);
        const char *tn = (st == 0) ? "--" : getPartName(g_loopTone[t]);
        if (!tn[0]) tn = "cust";
        snprintf(line, sizeof(line), "T%d %s", t + 1, tn);
        hardware.display.SetCursor(4, y);
        hardware.display.WriteString(line, Font_6x8, !act);

        int bx1 = 58, bx2 = 124, bh = 7, inW = bx2 - bx1 - 2;
        if (st == 3) // mute — надпись, без бара (явно «не играет»)
        {
            hardware.display.SetCursor(bx2 - 4 * 6, y);
            hardware.display.WriteString("mute", Font_6x8, !act);
        }
        else if (st == 1) // запись — сплошная заливка до головки
        {
            hardware.display.DrawRect(bx1, y, bx2, y + bh, col, false);
            int w = (int)(inW * prog);
            if (w > 0) hardware.display.DrawRect(bx1 + 1, y + 1, bx1 + 1 + w, y + bh - 1, col, true);
        }
        else if (st == 2) // игра — рамка + бегущий маркер (головка едет по кругу)
        {
            hardware.display.DrawRect(bx1, y, bx2, y + bh, col, false);
            int mx = bx1 + 1 + (int)(inW * prog);
            hardware.display.DrawRect(mx, y + 1, mx + 2, y + bh - 1, col, true);
        }
    }
    hardware.display.Update();
}

// Автокорреляционный детектор питча (вызывается из главного цикла в режиме тюнера).
// Детектор питча McLeod (NSDF): нормализованная функция + выбор ПЕРВОГО сильного пика
// -> устойчив к октавным ошибкам.
static void detectPitch()
{
    int start = g_tunerIdx;
    float mean = 0.0f, energy = 0.0f;
    for (int i = 0; i < TUNER_N; i++)
    {
        float v = g_tunerBuf[(start + i) % TUNER_N];
        g_acBuf[i] = v; mean += v;
    }
    mean /= TUNER_N;
    for (int i = 0; i < TUNER_N; i++) { g_acBuf[i] -= mean; energy += g_acBuf[i] * g_acBuf[i]; }
    if (energy < 0.002f) return; // тишина -> держим последнюю ноту

    int win = TUNER_N - TLAG_MAX;
    float maxPeak = 0.0f;
    for (int lag = TLAG_MIN; lag <= TLAG_MAX; lag++)
    {
        float ac = 0.0f, m = 0.0f;
        for (int i = 0; i < win; i++)
        {
            float a = g_acBuf[i], b = g_acBuf[i + lag];
            ac += a * b;
            m += a * a + b * b;
        }
        float v = (m > 1e-9f) ? (2.0f * ac / m) : 0.0f; // NSDF в [-1,1]
        g_acCorr[lag] = v;
        if (v > maxPeak) maxPeak = v;
    }
    if (maxPeak < 0.3f) return; // слабая периодичность -> держим последнюю ноту

    // Первый локальный пик выше 0.75*макс — это основной тон (а не октава/гармоника).
    float thresh = 0.75f * maxPeak;
    int bestLag = -1;
    for (int lag = TLAG_MIN + 1; lag < TLAG_MAX; lag++)
    {
        if (g_acCorr[lag] > thresh && g_acCorr[lag] >= g_acCorr[lag - 1] && g_acCorr[lag] > g_acCorr[lag + 1])
        {
            bestLag = lag;
            break;
        }
    }
    if (bestLag < 0) return; // нет пика -> держим последнюю ноту

    float c0 = g_acCorr[bestLag - 1], c1 = g_acCorr[bestLag], c2 = g_acCorr[bestLag + 1];
    float den = (c0 - 2.0f * c1 + c2);
    float period = bestLag;
    if (fabsf(den) > 1e-6f) period = bestLag + 0.5f * (c0 - c2) / den;

    float decimSR = g_sampleRate / TUNER_DECIM;
    g_tunerFreq = decimSR / period;
}

// Бесшумное переключение байпаса: аппаратный mute -> переключить реле -> unmute.
// Вызывается из главного цикла (UI), не из аудио-ISR. Глушит клик реле/CPC1018N.
static void setBypassMuted(bool bypass)
{
    hardware.SetAudioMute(true);   // заглушить (CPC1018N)
    hardware.DelayMs(4);
    hardware.SetAudioBypass(bypass); // переключить true-bypass реле
    hardware.DelayMs(8);           // дать реле устаканиться
    hardware.SetAudioMute(false);  // снять mute
}

int main(void)
{
    hardware.Init(48, false); // blockSize=48, boost=false (API GuitarPedal125B)
    hardware.SetAudioBlockSize(48);
    g_sampleRate = hardware.AudioSampleRate();

    __set_FPSCR(__get_FPSCR() | (1u << 24));
    FPU->FPDSCR |= (1u << 24);

    // Заставка при загрузке: VCOM-G2 крупно по центру, ревизия мелко в углу.
    hardware.display.Fill(false);
    writeCentered("VCOM-G2", 19, Font_16x26, true);
    hardware.display.SetCursor(126 - (int)strlen(FW_REVISION) * 4, 57);
    hardware.display.WriteString(FW_REVISION, Font_4x6, true);
    hardware.display.Update();
    hardware.DelayMs(3500);

    amp.Init(g_sampleRate);
    reverb.Init();
    for (int i = 0; i < DELAY_BUF; i++) g_delayBuf[i] = 0.0f; // обнулить SDRAM-буфер delay
    g_delayWrite = 0;
    tremolo.Init(g_sampleRate);
    chorus.Init(g_sampleRate);
    phaser.Init(g_sampleRate);
    flanger.Init(g_sampleRate);
    eq3.Init(g_sampleRate);
    applyModParams();

    // Банк пресетов: из файла в QSPI (если залит) или заводской song_library.
    loadPresetBank();

    // Хранилище правок в QSPI.
    SaveData def;
    memset(&def, 0, sizeof(def)); // ovUsed=0, customCount=0
    def.version = SAVE_VERSION;
    storage.Init(def);
    if (storage.GetSettings().version != SAVE_VERSION) storage.RestoreDefaults();

    buildSongTable(); // сгруппировать тоны в песни (для навигации)

    amp.SetParameterAsBinnedValue(P_MODEL, 2);
    amp.SetParameterAsBinnedValue(P_MODEL, 1);
    amp.SetParameterAsBinnedValue(P_IR, 2);
    amp.SetParameterAsBinnedValue(P_IR, 1);

    g_switchMuteSamples = hardware.GetNumberOfSamplesForTime(0.06f);
    g_gateHoldSamples = hardware.GetNumberOfSamplesForTime(0.35f);

    applySong(g_currentSong);

    hardware.SetAudioMute(true);      // замьютить на время старта аудио (убрать хлопок)
    hardware.StartAdc();
    hardware.StartAudio(AudioCallback);
    g_effectOn = false;               // старт в BYPASS: без щелчков/шумов, юзер включит сам
    hardware.SetAudioBypass(true);    // реле — прямой сигнал гитары
    hardware.DelayMs(80);             // дать кодеку/реле устаканиться
    hardware.SetAudioMute(false);     // снять mute

    uint32_t startMs = System::GetNow();
    bool rPrev = false, lPrev = false, comboLatch = false;
    int editKnob = -1; uint32_t editUntilMs = 0;

    int menuSel = 0;     // выбор в меню
    int editPage = 0;    // текущая страница эффекта в Edit FX
    int sysSel = 0;      // System: 0=Contrast 1=Brightness 2=Back
    bool sysEdit = false; // System: правим значение выбранного
    int custSel = 0;     // Custom Preset: 0=Add 1=Delete 2=Back
    int tempoSel = 0;    // Tempo: 0..4 поля, 5=Back
    bool tempoEdit = false;
    int loopField = 0;   // Looper: 0=Дорожка 1=Тон 2=МетГромкость
    float prevKnob[KNOBS] = {0}; // для детекта реального движения пота (оверлей правки)
    uint32_t savedMsgUntil = 0;
    const char *flashMsg = "SAVED";

    while (1)
    {
        uint32_t now = System::GetNow();
        if (!g_knobsInit && (now - startMs) > 1000) g_knobsInit = true;

        int d = g_encDelta; g_encDelta = 0;
        int p = g_encPress; g_encPress = 0;

        // ---- Футсвитчи: активны только в PLAY ----
        bool rNow = hardware.switches[SW_RIGHT].Pressed();
        bool lNow = hardware.switches[SW_LEFT].Pressed();
        if (g_mode == MODE_PLAY)
        {
            if (rNow && lNow && !comboLatch) { comboLatch = true; g_effectOn = !g_effectOn; setBypassMuted(!g_effectOn); }
            if (rPrev && !rNow && !comboLatch && !lNow) gotoTone(+1); // правый: следующий тон
            if (lPrev && !lNow && !comboLatch && !rNow) gotoTone(-1); // левый: предыдущий тон
            if (!rNow && !lNow) comboLatch = false;
        }
        else if (g_mode == MODE_LOOPER)
        {
            if (rNow && lNow && !comboLatch) { comboLatch = true; g_countInLeft = 0; g_looperEngaged = false; g_mode = MODE_MENU; } // оба = выход
            else
            {
                if (rPrev && !rNow && !comboLatch && !lNow) looperRightCycle();   // правый: запись->игра->mute
                if (lPrev && !lNow && !comboLatch && !rNow) looperClearActive();  // левый: стереть дорожку
            }
            if (!rNow && !lNow) comboLatch = false;
        }
        rPrev = rNow; lPrev = lNow;

        // ---- Логика по режимам ----
        if (g_mode == MODE_PLAY)
        {
            if (g_knobsInit)
            {
                for (int i = 0; i < KNOBS; i++)
                {
                    if (g_knobChanged[i])
                    {
                        int pid = amp.GetMappedParameterIDForKnob(i);
                        if (pid != -1)
                        {
                            if (isBinnedKnob(i))
                            {
                                int before = amp.GetParameterAsBinnedValue(pid);
                                amp.SetParameterAsMagnitude(pid, g_knobCache[i]);
                                if (amp.GetParameterAsBinnedValue(pid) != before) g_muteCountdown = g_switchMuteSamples;
                            }
                            else amp.SetParameterAsMagnitude(pid, g_knobCache[i]);
                        }
                        // экран правки — только при реальном движении пота (не постоянно)
                        if (g_knobCache[i] > prevKnob[i] + 0.002f || g_knobCache[i] < prevKnob[i] - 0.002f)
                        { editKnob = i; editUntilMs = now + 1500; }
                    }
                    prevKnob[i] = g_knobCache[i];
                }
            }
            if (d != 0) gotoSong(d); // энкодер: выбор песни
            if (p > 0) { g_mode = MODE_MENU; menuSel = 0; }

            if (editKnob >= 0 && now < editUntilMs)
            {
                // экран правки ручки
                char line[24];
                int pid = amp.GetMappedParameterIDForKnob(editKnob);
                const char *pname = amp.GetParameterName(pid);
                hardware.display.Fill(false);
                writeCentered(pname, 2, Font_16x26, true);
                if (isBinnedKnob(editKnob))
                {
                    const char **names = amp.GetParameterBinNames(pid);
                    int idx = amp.GetParameterAsBinnedValue(pid) - 1;
                    writeCentered(names[idx], 40, Font_11x18, true);
                }
                else
                {
                    drawBar(36, g_knobCache[editKnob]);
                    snprintf(line, sizeof(line), "%d%%", (int)(g_knobCache[editKnob] * 100.0f + 0.5f));
                    writeCentered(line, 52, Font_7x10, true);
                }
                hardware.display.Update();
            }
            else if (now < savedMsgUntil)
            {
                hardware.display.Fill(false);
                writeCentered(flashMsg, 18, Font_16x26, true);
                hardware.display.Update();
            }
            else drawPlay();
        }
        else if (g_mode == MODE_MENU)
        {
            if (d != 0) { menuSel += d; while (menuSel < 0) menuSel += MENU_COUNT; while (menuSel >= MENU_COUNT) menuSel -= MENU_COUNT; }
            if (p > 0)
            {
                if (menuSel == 0) { g_mode = MODE_TUNER; g_tunerFreq = 0.0f; setBypassMuted(false); } // вход в Daisy
                else if (menuSel == 1) { g_mode = MODE_EDIT; editPage = 0; g_knobRearm = true; }
                else if (menuSel == 2) { g_mode = MODE_LOOPER; loopField = 0; recomputeTempo(); g_metCounter = 0; g_beatShow = g_beatsPerBar - 1; g_looperEngaged = true; g_knobRearm = true; }
                else if (menuSel == 3) { g_mode = MODE_TEMPO; tempoSel = 0; tempoEdit = false; }
                else if (menuSel == 4) { saveCurrentSong(); flashMsg = "SAVED"; g_mode = MODE_PLAY; savedMsgUntil = now + 900; }
                else if (menuSel == 5) { resetCurrentPreset(); flashMsg = "RESET"; g_mode = MODE_PLAY; savedMsgUntil = now + 900; }   // только этот пресет
                else if (menuSel == 6) { factoryResetAll(); flashMsg = "DEFAULT"; g_mode = MODE_PLAY; savedMsgUntil = now + 900; }
                else if (menuSel == 7) { g_mode = MODE_CUSTOM; custSel = 0; }
                else if (menuSel == 8) { g_mode = MODE_SYSTEM; sysSel = 0; sysEdit = false; }
                else g_mode = MODE_PLAY; // Exit
            }
            drawMenu(menuSel);
        }
        else if (g_mode == MODE_TUNER)
        {
            detectPitch();
            if (p > 0) { g_mode = MODE_MENU; setBypassMuted(!g_effectOn); } // вернуть реле
            drawTuner();
        }
        else if (g_mode == MODE_LOOPER)
        {
            // нажатие: TRK -> MET(вкл/выкл) -> BPM -> VOL(клик) -> POT(Tone/Vol)
            if (p > 0) { loopField = (loopField + 1) % 5; g_knobRearm = true; }
            if (d != 0)
            {
                if (loopField == 0) { g_loopActive += d; while (g_loopActive < 0) g_loopActive += LOOP_TRACKS; while (g_loopActive >= LOOP_TRACKS) g_loopActive -= LOOP_TRACKS; }
                else if (loopField == 1) { g_metOn = !g_metOn; if (g_metOn) { g_metCounter = 0; g_beatShow = g_beatsPerBar - 1; } }
                else if (loopField == 2) { g_bpm += d; recomputeTempo(); }
                else if (loopField == 3) { g_clickVol += d * 0.05f; if (g_clickVol < 0) g_clickVol = 0; if (g_clickVol > 1) g_clickVol = 1; }
                else { g_potMode ^= 1; g_knobRearm = true; }
            }
            // поты: Volume если выбран режим Volume ИЛИ активная дорожка пишется; иначе живой тон
            if (g_knobsInit)
            {
                bool potsVol = (g_potMode == 1) || (g_loopState[g_loopActive] == 1);
                if (potsVol)
                {
                    for (int i = 0; i < LOOP_TRACKS && i < KNOBS; i++) if (g_knobChanged[i]) g_loopVol[i] = g_knobCache[i];
                }
                else
                {
                    for (int i = 0; i < KNOBS; i++) if (g_knobChanged[i])
                    {
                        int pid = amp.GetMappedParameterIDForKnob(i);
                        if (pid != -1)
                        {
                            if (isBinnedKnob(i)) { int before = amp.GetParameterAsBinnedValue(pid); amp.SetParameterAsMagnitude(pid, g_knobCache[i]); if (amp.GetParameterAsBinnedValue(pid) != before) g_muteCountdown = g_switchMuteSamples; }
                            else amp.SetParameterAsMagnitude(pid, g_knobCache[i]);
                        }
                    }
                }
            }
            drawLooper(loopField);
        }
        else if (g_mode == MODE_TEMPO)
        {
            if (!tempoEdit)
            {
                if (d != 0) { tempoSel += d; while (tempoSel < 0) tempoSel += TEMPO_ITEMS; while (tempoSel >= TEMPO_ITEMS) tempoSel -= TEMPO_ITEMS; }
                if (p > 0) { if (tempoSel == 5) g_mode = MODE_MENU; else tempoEdit = true; }
            }
            else
            {
                if (d != 0)
                {
                    if (tempoSel == 0) { g_bpm += d; recomputeTempo(); }
                    else if (tempoSel == 1) { g_beatsPerBar += d; recomputeTempo(); }
                    else if (tempoSel == 2) { g_clickVol += d * 0.05f; if (g_clickVol < 0) g_clickVol = 0; if (g_clickVol > 1) g_clickVol = 1; }
                    else if (tempoSel == 3) { g_metOn = !g_metOn; }
                    else if (tempoSel == 4) { g_countInOn = !g_countInOn; }
                }
                if (p > 0) tempoEdit = false;
            }
            drawTempo(tempoSel, tempoEdit);
        }
        else if (g_mode == MODE_SYSTEM)
        {
            if (!sysEdit)
            {
                if (d != 0) { sysSel += d; while (sysSel < 0) sysSel += 3; while (sysSel >= 3) sysSel -= 3; }
                if (p > 0) { if (sysSel == 2) g_mode = MODE_MENU; else sysEdit = true; }
            }
            else
            {
                if (d != 0)
                {
                    if (sysSel == 0)
                    {
                        g_contrast += d;
                        if (g_contrast < 0) g_contrast = 0;
                        if (g_contrast > 63) g_contrast = 63;
                        hardware.display.SetContrast((uint8_t)g_contrast); // реальная регулировка
                    }
                    else // Brightness — пока демо (подсветка на PWM позже)
                    {
                        g_brightness += d * 5;
                        if (g_brightness < 0) g_brightness = 0;
                        if (g_brightness > 100) g_brightness = 100;
                    }
                }
                if (p > 0) sysEdit = false;
            }
            drawSystem(sysSel, sysEdit);
        }
        else if (g_mode == MODE_CUSTOM)
        {
            if (d != 0) { custSel += d; while (custSel < 0) custSel += CUST_COUNT; while (custSel >= CUST_COUNT) custSel -= CUST_COUNT; }
            if (p > 0)
            {
                if (custSel == 0) { addCustomPreset(); flashMsg = "CUSTOM+"; g_mode = MODE_PLAY; savedMsgUntil = now + 900; }
                else if (custSel == 1) { bool ok = deleteCustomPreset(); flashMsg = ok ? "DELETED" : "SKIP"; g_mode = MODE_PLAY; savedMsgUntil = now + 900; }
                else g_mode = MODE_MENU; // Back
            }
            drawCustomMenu(custSel);
        }
        else // MODE_EDIT
        {
            // Энкодер листает страницы эффектов (при смене — сброс подхвата потов).
            if (d != 0)
            {
                editPage += d;
                while (editPage < 0) editPage += PG_COUNT;
                while (editPage >= PG_COUNT) editPage -= PG_COUNT;
                g_knobRearm = true;
            }
            // Нажатие энкодера — выход в меню (сброс подхвата, чтобы поты не дёрнули усилитель).
            if (p > 0) { g_knobRearm = true; g_mode = MODE_MENU; }
            // 6 потов с подхватом правят параметры текущей страницы (только после движения ручки).
            if (g_knobsInit)
            {
                int n = editParamCount(editPage);
                for (int i = 0; i < n && i < KNOBS; i++)
                    if (g_knobChanged[i]) editParamApply(editPage, i, g_knobCache[i]);
            }
            drawEditPage(editPage);
        }

        hardware.DelayMs(15);
    }
}
