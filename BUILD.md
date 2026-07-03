# VCOM-G1 — сборка и прошивка

Самодостаточная копия проекта. Зависимости лежат внутри (`GuitarPedal/`), `VCOM-G` не используется.

## Установленный инструментарий (на этой машине)
| Инструмент | Путь |
|---|---|
| ARM GCC **10.3-2021.10** (обязательно, не 14!) | `C:\Users\vinok\toolchains\gcc10\gcc-arm-none-eabi-10.3-2021.10\bin` |
| GNU Make 4.4.1 | `C:\Users\vinok\.mplab\app-finder\apps\make\v4.4.1\windows` |
| dfu-util 0.11 | `C:\Users\vinok\.espressif\tools\dfu-util\0.11\dfu-util-0.11-win64` |

Скрипт `build.sh` сам добавляет эти пути — отдельно настраивать PATH не нужно.

## Сборка (в Git Bash)
```bash
cd /c/Users/vinok/Desktop/Retro/Fun/VCOM/VCOM-G/FW/VCOM-G1
./build.sh            # -> build/VCOM_G.bin (.elf, .hex)
./build.sh clean      # очистить
```
Библиотеки `libDaisy`/`DaisySP` уже собраны (`GuitarPedal/libDaisy/build/libdaisy.a`,
`GuitarPedal/DaisySP/build/libdaisysp.a`). Если их удалить — пересобрать:
```bash
export PATH="/c/Users/vinok/toolchains/arm-gnu/bin:/c/Users/vinok/.mplab/app-finder/apps/make/v4.4.1/windows:$PATH"
make -C GuitarPedal/libDaisy -j6
make -C GuitarPedal/DaisySP  -j6
```

## Прошивка
Приложение типа `BOOT_SRAM` — грузится в SRAM через Daisy-бутлоадер.

1. Подключи Daisy по USB.
2. Введи плату в DFU: **двойной тап RESET** (LED «дышит»).
3. Залей прошивку:
   ```bash
   ./build.sh program-dfu
   ```
4. Если на плате ещё нет Daisy-бутлоадера (или прошивка не стартует) — один раз:
   ```bash
   ./build.sh program-boot     # залить бутлоадер
   # затем снова двойной RESET и ./build.sh program-dfu
   ```

> Прошивку заливаешь ты — для этого нужна физически подключённая плата в режиме DFU.

## Что отличается от исходного VCOM-G
- **Delay** перенесён в SDRAM, до ~900 мс, с ФНЧ на повторах; пресеты delay подняты по миксу.
- **amp_module** восстановлен из апстрим-коммита `2f47ce6d` (**7 моделей + 4 кабинета** —
  данных на твои 9/10 не было). Кабинеты: Marsh / Proteus / US Deluxe / British.
- **Защита от OOB**: `SetParameterAsBinnedValue` клампит бин в диапазон; `SelectModel/SelectIR`
  клампят индекс. Это убирает «писк-от-выхода-за-границы» и делает выбор модели/кабинета
  детерминированным.
- **Адаптации под актуальные библиотеки**: свой однополюсный ФНЧ вместо удалённого из DaisySP
  `Tone`; добавлен геттер `GetParameterAsMagnitude`; вызов `hardware.Init(48, false)`.
