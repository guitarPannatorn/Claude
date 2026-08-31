#!/usr/bin/env bash
# ===================================================================
# รันเทสต์ตรรกะ Auto Assessment บนเครื่อง PC (ไม่ต้องมีบอร์ดจริง)
#
#   bash test/run_tests.sh
#
# วิธีทำงาน: ใช้ stub header จำลองไลบรารี Arduino แล้วคอมไพล์ .ino
# ด้วย g++ ปกติ พร้อม millis() ปลอมที่เราควบคุมเวลาได้เอง
# จึงจำลองเหตุการณ์ 15 วินาทีได้ในเสี้ยววินาที
# ===================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT

cp "$ROOT"/test/stub/*.h "$BUILD/"
cp "$ROOT"/test/test_auto_assessment.cpp "$BUILD/"

# Arduino IDE สร้าง forward declaration ให้อัตโนมัติ แต่ g++ ไม่ทำให้
# จึงต้องแทรก prototype เองก่อนคอมไพล์
SKETCH="$ROOT/firmware/Nano_Pro/Nano_Pro.ino"
{
  sed -n '1,/^#include <EEPROM.h>/p' "$SKETCH"
  echo
  grep -E '^(void|int|float|bool|unsigned long|byte) +[a-zA-Z_][a-zA-Z0-9_]* *\(' "$SKETCH" | sed 's/ *$/;/'
  echo
  sed -n '/^#include <EEPROM.h>/,$p' "$SKETCH" | tail -n +2
} > "$BUILD/sketch.cpp"

echo "== ตรวจ syntax ทั้งไฟล์ =="
g++ -std=c++11 -I"$BUILD" -fsyntax-only -Wall -Wextra -Wno-unused-parameter \
    -include Arduino.h -include EEPROM.h -x c++ "$BUILD/sketch.cpp" 2>&1 || true

echo "== รันเทสต์ =="
g++ -std=c++11 -I"$BUILD" -o "$BUILD/test" "$BUILD/test_auto_assessment.cpp"
"$BUILD/test"
