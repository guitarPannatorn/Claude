#include "Arduino.h"
#include "EEPROM.h"
SerialClass Serial;
EEPROMClass EEPROM;
void pinMode(int,int){} void digitalWrite(int,int){}
int digitalRead(int){return 1;} int analogRead(int){return 1023;}
void delay(unsigned long){}
unsigned long FAKE_MS = 0;
unsigned long millis(){ return FAKE_MS; }
#include "sketch.cpp"

#include <iostream>
int fails = 0;
void check(const char* name, bool cond) {
  std::cout << (cond ? "  PASS  " : "  FAIL  ") << name << "\n";
  if (!cond) fails++;
}
void resetState() {
  autoTimerActive = false; autoFailLock = false;
  autoTimerStart = 0; lastAnyColorTime = 0; FAKE_MS = 100000;
}
// ป้อนผลตรวจจับทีละเฟรม เฟรมละ 150ms เหมือนของจริง
void feed(bool f1, bool f2, unsigned long durationMs) {
  for (unsigned long t = 0; t < durationMs; t += MONITOR_CHECK_INTERVAL) {
    FAKE_MS += MONITOR_CHECK_INTERVAL;
    handleAutoAssessment(f1, f2, FAKE_MS);
  }
}

int main() {
  std::cout << "\n=== สถานการณ์ที่ 1: เห็นสีเดียวแวบเดียว แล้วหยิบชิ้นงานออก (บั๊กเดิม) ===\n";
  resetState();
  feed(true, false, 900);      // เห็น ID1 อย่างเดียว 0.9 วิ -> อาร์มตัวจับเวลา
  check("ตัวจับเวลาถูกอาร์มแล้ว", autoTimerActive);
  feed(false, false, 15000);   // หยิบชิ้นงานออก สถานีว่าง 15 วิ
  check("สถานีว่าง -> ตัวจับเวลาถูกยกเลิก", !autoTimerActive);
  check("สถานีว่าง -> ไม่ล็อกเป็น NG (บั๊กเดิมจะ FAIL ตรงนี้)", !autoFailLock);
  check("ยอด NG ไม่ถูกบวกเพิ่ม", countNgTotal == 0);

  std::cout << "\n=== สถานการณ์ที่ 2: ของเสียจริง วางค้างไว้หน้ากล้อง ===\n";
  resetState(); countNgTotal = 0;
  feed(true, false, 6000);     // เห็นสีเดียวค้าง 6 วิ
  check("ยังจับของเสียได้ -> ล็อกเป็น NG", autoFailLock);
  check("ยอด NG ถูกบวก 1", countNgTotal == 1);

  std::cout << "\n=== สถานการณ์ที่ 3: ชิ้นงานดี เห็นครบ 2 สีทัน ===\n";
  resetState(); countNgTotal = 0;
  feed(true, false, 2000);
  check("อาร์มตัวจับเวลาไว้ก่อน", autoTimerActive);
  feed(true, true, 600);
  check("เห็นครบ 2 สี -> ยกเลิกตัวจับเวลา", !autoTimerActive);
  check("ไม่ล็อกเป็น NG", !autoFailLock);

  std::cout << "\n=== สถานการณ์ที่ 4: ของเสียค้างอยู่ แต่ภาพกระพริบหายแวบเดียว (< 600ms) ===\n";
  resetState(); countNgTotal = 0;
  feed(true, false, 2000);
  feed(false, false, 300);     // หายไป 0.3 วิ (สั้นกว่า EMPTY_CANCEL_MS)
  check("กระพริบสั้นๆ ไม่ยกเลิกตัวจับเวลา", autoTimerActive);
  feed(true, false, 4000);
  check("ยังล็อกเป็น NG ได้ตามเดิม", autoFailLock);

  std::cout << "\n=== สถานการณ์ที่ 5: ชิ้นงานทยอยเข้าเฟรม ID1 มาก่อน ID2 ===\n";
  resetState(); countNgTotal = 0;
  feed(true, false, 450);      // ID1 เข้ามาก่อน
  feed(true, true, 3000);      // ID2 ตามมา ครบ 2 สี
  feed(false, false, 8000);    // ชิ้นงานผ่านออกไป
  check("ไม่ล็อกเป็น NG ตลอดกระบวนการ", !autoFailLock);
  check("ยอด NG = 0", countNgTotal == 0);

  std::cout << (fails ? "\n>>> มีข้อที่ไม่ผ่าน: " : "\n>>> ผ่านทั้งหมด ") << fails << "\n\n";
  return fails ? 1 : 0;
}
