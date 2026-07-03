#!/usr/bin/env bash
# Конвертит presets.csv -> presets.bin и заливает в QSPI (0x90100000) через DFU.
# БЕЗ компиляции прошивки. Запускать в Git Bash из папки tools/.
#
# Перед запуском: введи плату в DFU (двойной тап RESET, LED дышит).
set -e
cd "$(dirname "$0")"

# CSV можно передать аргументом, иначе берётся ../presets/factory.csv.
CSV="${1:-../presets/factory.csv}"
echo "1) Конвертирую $CSV -> presets.bin"
python make_presets.py "$CSV" presets.bin

export PATH="/c/Users/vinok/.espressif/tools/dfu-util/0.11/dfu-util-0.11-win64:$PATH"

echo "2) Жду плату в DFU (двойной тап RESET, держи 'дыхание')..."
ok=0
for i in $(seq 1 200); do
  if dfu-util -l 2>/dev/null | grep -qi "0483:df11"; then
    echo "   Плата в DFU — заливаю presets.bin в 0x90100000"
    out=$(dfu-util -a 0 -s 0x90100000:leave -D presets.bin -d ,0483:df11 2>&1)
    if echo "$out" | grep -qiE "File downloaded successfully"; then echo "=== ПРЕСЕТЫ ЗАЛИТЫ ==="; ok=1; break; fi
    echo "$out" | grep -iE "No DFU|error" | tail -2
  fi
  sleep 0.4
done
[ $ok -eq 0 ] && echo "=== не залил за ~80с — повтори двойной RESET и запусти снова ==="
exit 0
