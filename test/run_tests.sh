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

echo "== รันเทสต์ Nano: Auto Assessment + ชื่อ event =="
g++ -std=c++11 -I"$BUILD" -o "$BUILD/test" "$BUILD/test_auto_assessment.cpp"
"$BUILD/test"

# ---------------------------------------------------------------
# เทสต์คิว event ของ ESP32
# ตัดเฉพาะส่วนคิวออกมาจาก .ino จริง (ตั้งแต่ #define EVENT_QUEUE_SIZE
# ถึงท้าย drainEventQueue) แล้วคอมไพล์กับ stub เพื่อไม่ต้องมีไลบรารี ESP32
# ---------------------------------------------------------------
ESP_SKETCH="$ROOT/firmware/esp32_supabase_rfid/esp32_supabase_rfid.ino"
cp "$ROOT"/test/test_event_queue.cpp "$BUILD/"

{
  sed -n '/^#define EVENT_QUEUE_SIZE/,/^uint8_t eventQueueCount/p' "$ESP_SKETCH"
  echo
  sed -n '/^void pushEvent(const String& ev)$/,/^}$/p' "$ESP_SKETCH"
  echo
  sed -n '/^String extractEvent(const String& json)$/,/^}$/p' "$ESP_SKETCH"
  echo
  sed -n '/^void drainEventQueue()$/,/^}$/p' "$ESP_SKETCH"
} > "$BUILD/esp32_queue.cpp"

for fn in pushEvent extractEvent drainEventQueue; do
  grep -q "$fn" "$BUILD/esp32_queue.cpp" || { echo "ตัดโค้ด $fn จาก .ino ไม่สำเร็จ"; exit 1; }
done

echo "== รันเทสต์ ESP32: คิว event =="
g++ -std=c++11 -I"$BUILD" -o "$BUILD/test_queue" "$BUILD/test_event_queue.cpp"
"$BUILD/test_queue"

# ---------------------------------------------------------------
# เทสต์ตัวพักอัปโหลดตอนรีเซ็ตยอดรวม (กันบั๊กค่าเด้งกลับเป็นค่าเก่า)
# ตัดเฉพาะส่วนนี้ออกมาจาก .ino จริงเหมือนกับคิว event ข้างบน
# ---------------------------------------------------------------
cp "$ROOT"/test/test_reset_suppression.cpp "$BUILD/"

{
  sed -n '/^bool suppressStatusUpload/,/^const unsigned long SUPPRESS_TIMEOUT_MS/p' "$ESP_SKETCH"
  echo
  sed -n '/^void armResetSuppression()$/,/^}$/p' "$ESP_SKETCH"
  echo
  sed -n '/^bool clearResetSuppressionIfConfirmed(const String& ev)$/,/^}$/p' "$ESP_SKETCH"
  echo
  sed -n '/^bool shouldUploadStatusNow(unsigned long now)$/,/^}$/p' "$ESP_SKETCH"
} > "$BUILD/esp32_suppression.cpp"

for fn in armResetSuppression clearResetSuppressionIfConfirmed shouldUploadStatusNow; do
  grep -q "$fn" "$BUILD/esp32_suppression.cpp" || { echo "ตัดโค้ด $fn จาก .ino ไม่สำเร็จ"; exit 1; }
done

echo "== รันเทสต์ ESP32: กันค่ายอดรวมเด้งกลับหลังรีเซ็ต =="
g++ -std=c++11 -I"$BUILD" -o "$BUILD/test_suppress" "$BUILD/test_reset_suppression.cpp"
"$BUILD/test_suppress"
