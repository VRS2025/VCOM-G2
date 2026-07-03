#!/usr/bin/env bash
# VCOM-G1 — сборка и прошивка. Запускать в Git Bash.
# Прописывает пути к тулчейну (arm-none-eabi-gcc), make и dfu-util,
# затем вызывает make с переданными аргументами.
#
# Использование:
#   ./build.sh                 # компиляция  -> build/VCOM_G.bin
#   ./build.sh clean           # очистить build/
#   ./build.sh program-dfu     # залить ПРОШИВКУ (плата в DFU: двойной RESET)
#   ./build.sh program-boot    # залить Daisy-бутлоадер (обычно один раз)
set -e

# GCC 10.3 (Daisy-стандарт) — НЕ GCC 14: свежий GCC ломает Eigen/RTNeural с -ffast-math.
export PATH="/c/Users/vinok/toolchains/gcc10/gcc-arm-none-eabi-10.3-2021.10/bin:/c/Users/vinok/.mplab/app-finder/apps/make/v4.4.1/windows:/c/Users/vinok/.espressif/tools/dfu-util/0.11/dfu-util-0.11-win64:$PATH"

cd "$(dirname "$0")"
make "$@"
