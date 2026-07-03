# Пресеты из файла (без перекомпиляции)

Прошивка читает банк пресетов из QSPI по адресу `0x90100000`. Если там нет валидного
файла — берутся заводские из `song_library.h`. Значит пресеты можно менять **файлом**,
не пересобирая прошивку.

## Как пользоваться
1. Правишь **`presets.csv`** (Excel/блокнот). Колонки:
   `band,song,model,ir,gain,mix,level,tone,revMix,delayMix,delayMs,delayFb,tremDepth,tremRate,eqLow,eqMid,eqHigh,revFb,revDamp`
   - Обязательны: `band,song,model,ir`. Остальное можно опускать (берётся дефолт).
   - `eqLow/eqMid/eqHigh`=0 → EQ плоский; `revFb`=0 → 0.84; `revDamp`=0 → 0.35.
   - Имена ≤ 11 символов. До **32** пресетов.
2. Вводишь плату в **DFU** (двойной тап RESET, LED дышит).
3. Запускаешь в Git Bash:
   ```bash
   ./load_presets.sh
   ```
   Он сам конвертит `presets.csv` → `presets.bin` и зальёт в QSPI. **Компиляции нет.**
4. Плата перезагрузится с новым банком.

Вернуть заводские: ничего не заливать — или стереть область
(`dfu-util -a 0 -s 0x90100000:leave -D /dev/null` не сработает; проще залить пустой/
короткий `presets.csv`).

## Номера моделей/кабинетов
- **model**: 1 Fender57 · 2 Matchless · 3 Klon · 4 Mesa iic · 5 H&K Clean · 6 Bassman · 7 5150 · 8 Splawn · 9 Klon2
- **ir**: 1 Rhythm · 2 Lead · 3 Clean · 4 Marsh · 5 Bogn · 6 Proteus · 7 Rectify · 8 Rhythm2 · 9 US Deluxe · 10 British

## Файлы
- `make_presets.py` — конвертер CSV → BIN (формат совпадает с `FilePreset` в прошивке).
- `presets.csv` — твой банк (правишь его).
- `load_presets.sh` — конвертит + заливает через DFU.

> Сохранение прямо на педали (Menu → Save Preset / Add Custom) тоже работает и живёт
> поверх банка. «Default All» сбрасывает правки. Банк из файла — это «заводская» основа.
