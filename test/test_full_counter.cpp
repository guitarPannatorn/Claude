// ===================================================================
// เทสต์เงื่อนไข "ครบเป้า (Full counter) แล้วต้องกด RESET ก่อนถึงจะรับงานต่อ"
//
// พฤติกรรมที่ต้องการ:
//   - พอ Counting ถึง Setting -> ขึ้น Full counter แล้วหยุดรับงานทุกทาง
//     (ไม่นับเพิ่ม ไม่ตรวจสี ไม่ตัดสิน NG ไม่ส่ง event ขึ้นเว็บ)
//   - ปลดได้ 2 ทาง:
//       A7 / RESET_COUNT -> Counting = 0 เริ่มรอบใหม่
//       A0 / RESET       -> ปลด Full แต่ Counting ค้างที่เลขเดิม
//                           (กดหนึ่งครั้ง = รับเพิ่มได้หนึ่งชิ้นแล้วเต็มอีก)
// ===================================================================
#include "Arduino.h"
#include "EEPROM.h"
SerialClass Serial;
EEPROMClass EEPROM;
void pinMode(int,int){}
// จำระดับล่าสุดของทุกขา เพื่อเช็ค "ขาจริง" ไม่ใช่แค่ตัวแปรสถานะ
// ของเดิม stub ตัวนี้เป็นฟังก์ชันเปล่า เทสต์จึงมองไม่เห็นเลยว่ามีโค้ดเส้นไหน
// แอบจุดไฟ D9/D11/D12 ทับระหว่าง Full counter
int pinLevel[32];
void digitalWrite(int pin, int level){ if (pin >= 0 && pin < 32) pinLevel[pin] = level; }
int digitalRead(int){return 1;} int analogRead(int){return 1023;}
void delay(unsigned long){}
unsigned long FAKE_MS = 0;
// doSystemReset() มี busy-wait "while (millis() - resetTime < 700)" อยู่ข้างใน
// ถ้านาฬิกาปลอมไม่เดินเองจะวนไม่จบ จึงให้ขยับทีละ 1ms ทุกครั้งที่ถูกอ่าน
// (ยังกระโดดเวลาเองด้วย FAKE_MS += ... ได้เหมือนเดิม)
unsigned long millis(){ return ++FAKE_MS; }
#include "sketch.cpp"

#include <iostream>
int fails = 0;
void check(const char* name, bool cond) {
  std::cout << (cond ? "  PASS  " : "  FAIL  ") << name << "\n";
  if (!cond) fails++;
}

void resetState() {
  FAKE_MS = 100000;
  settingValue = 3;            // ตั้งเป้าน้อยๆ จะได้ถึง Full เร็ว
  countingValue = 0;
  fullCounterFlag = false;
  countOkTotal = 0;
  countNgTotal = 0;
  lastEventMsg = "none";
  ledNgOn = false;
  ngDelayActive = false;
  autoFailLock = false;
  autoTimerActive = false;
  ledMonitorOn = false;
  lastLedMonitorOn = false;
  ledOkOn = false;
  okTimerActive = false;
  lastCountTriggerTime = 0;
}

// นับหนึ่งชิ้นแบบข้าม lockout เวลา (ของจริงมี COUNT_TRIGGER_LOCKOUT_MS กันเด้ง)
void countOnePiece() {
  FAKE_MS += COUNT_TRIGGER_LOCKOUT_MS + 1;
  triggerCountFromD11();
}

int main() {
  std::cout << "\n=== สถานการณ์ที่ 1: นับจนครบเป้า -> ขึ้น Full counter ===\n";
  resetState();
  countOnePiece(); countOnePiece();
  check("ยังไม่ครบเป้า -> ยังไม่ Full", !fullCounterFlag);
  check("นับได้ตามจริง 2 ชิ้น", countingValue == 2);
  countOnePiece();
  check("ครบเป้า -> ขึ้น Full counter", fullCounterFlag);
  check("Counting = Setting พอดี", countingValue == 3);
  check("event แจ้ง full_counter", lastEventMsg == String("full_counter"));

  std::cout << "\n=== สถานการณ์ที่ 2: Full แล้วต้องไม่รับงานเพิ่มอีกเลย ===\n";
  int okBefore = countOkTotal;
  countOnePiece(); countOnePiece(); countOnePiece();
  check("นับเพิ่มไม่ได้ Counting ค้างที่เป้า", countingValue == 3);
  check("ยอด OK สะสมไม่ขยับ", countOkTotal == okBefore);

  std::cout << "\n=== สถานการณ์ที่ 3: Full แล้ว handleColorMonitor ต้องหยุดทำงานทั้งหมด ===\n";
  // จำลองสภาพค้างจากก่อนหน้า: D9 ติดอยู่ + ตัวจับเวลา Auto กำลังเดิน
  ledMonitorOn = true; lastLedMonitorOn = true; autoTimerActive = true;
  countNgTotal = 0;
  handleColorMonitor();
  check("D9 ถูกดับ", !ledMonitorOn);
  check("ตัวจับเวลา Auto ถูกยกเลิก", !autoTimerActive);
  check("ไม่ตัดสินเป็น NG ระหว่าง Full", countNgTotal == 0);
  check("ล้าง lastLedMonitorOn กัน falling edge ค้าง", !lastLedMonitorOn);

  std::cout << "\n=== สถานการณ์ที่ 4: กด A0/RESET -> ปลด Full แต่ Counting ค้างเลขเดิม ===\n";
  doSystemReset();
  check("Full ถูกปลดแล้ว", !fullCounterFlag);
  check("Counting ยังค้างที่เลขเดิม ไม่ถูกล้าง", countingValue == 3);
  countOnePiece();
  check("รับเพิ่มได้อีก 1 ชิ้น", countingValue == 4);
  check("เกินเป้าแล้ว -> กลับมา Full ทันที", fullCounterFlag);

  std::cout << "\n=== สถานการณ์ที่ 5: ปลด Full แล้วต้องไม่มีการนับผีจาก falling edge ค้าง ===\n";
  resetState();
  countOnePiece(); countOnePiece(); countOnePiece();
  check("ถึง Full ก่อน", fullCounterFlag);
  ledMonitorOn = true; lastLedMonitorOn = true;   // D9 ค้างติดตอนเข้า Full
  handleColorMonitor();                            // โดนบล็อก + ล้างสถานะ
  doSystemReset();                                 // ปลด Full
  int countingAfterReset = countingValue;
  handleColorMonitor();                            // รอบแรกหลังปลด
  check("ไม่มีการนับผีเพิ่มเองหลังปลด Full", countingValue == countingAfterReset);

  std::cout << "\n=== สถานการณ์ที่ 6: Full แล้วต้องไม่แสดงผลที่ D9 / D11 / D12 ===\n";
  resetState();
  countOnePiece(); countOnePiece(); countOnePiece();
  check("ถึง Full ก่อน", fullCounterFlag);
  // จำลอง NG/LOCK ที่ค้างมาก่อนหน้า - ต้องไม่โผล่ออกที่ D12 ระหว่าง Full
  ledNgOn = true; autoFailLock = true; ledMonitorOn = true; ledOkOn = true;
  check("D12 ต้องดับระหว่าง Full แม้มี NG/LOCK ค้าง", !outputD12Active());
  check("D9 ต้องดับระหว่าง Full", !outputD9Active());
  check("D11 ต้องดับระหว่าง Full แม้ไฟ OK ค้างจากชิ้นที่ทำให้ครบเป้า", !outputD11Active());
  // ค่าที่ส่งขึ้นเว็บใช้ตัวเดียวกัน ไฟหน้าเครื่องกับหน้าเว็บจึงตรงกันเสมอ
  doSystemReset();
  check("ปลด Full แล้ว NG/LOCK ถูกเคลียร์ไปด้วย -> D12 ยังดับ", !outputD12Active());

  // ชิ้นที่ทำให้ครบเป้าจุด D11 ก่อนแล้วค่อย Full ในจังหวะเดียวกัน
  // ไฟ OK ต้องไม่ค้างสว่างหลังจากนั้น
  resetState();
  countOnePiece(); countOnePiece();
  ledOkOn = true;                 // เหมือน setResultOK() จุดไฟ OK ให้ชิ้นสุดท้าย
  countOnePiece();                // ชิ้นนี้ทำให้ครบเป้า
  check("ชิ้นที่ทำให้ครบเป้า -> Full ขึ้น", fullCounterFlag);
  check("ไฟ OK ไม่ค้างสว่างตอน Full", !outputD11Active());

  // ผลตรวจที่เกิดตอนไม่ Full ต้องยังโชว์ตามปกติ
  resetState();
  ledNgOn = true;
  check("ไม่ Full + มี NG -> D12 ติดตามปกติ", outputD12Active());
  ledNgOn = false; autoFailLock = true;
  check("ไม่ Full + auto lock -> D12 ติดตามปกติ", outputD12Active());
  autoFailLock = false; ledMonitorOn = true;
  check("ไม่ Full + เห็นสีครบ -> D9 ติดตามปกติ", outputD9Active());
  ledMonitorOn = false; ledOkOn = true;
  check("ไม่ Full + ผลเป็น OK -> D11 ติดตามปกติ", outputD11Active());

  std::cout << "\n=== สถานการณ์ที่ 7: A7/RESET_COUNT ยังล้าง Counting เริ่มรอบใหม่ได้ตามเดิม ===\n";
  resetState();
  countOnePiece(); countOnePiece(); countOnePiece();
  check("ถึง Full ก่อน", fullCounterFlag);
  doCountingReset();
  check("Full ถูกปลด", !fullCounterFlag);
  check("Counting กลับเป็น 0 เริ่มรอบใหม่", countingValue == 0);
  countOnePiece();
  check("รับงานรอบใหม่ได้ปกติ", countingValue == 1);
  check("ยังไม่ Full เพราะเพิ่งเริ่มรอบ", !fullCounterFlag);

  std::cout << "\n=== สถานการณ์ที่ 8: ขาจริง D9/D11/D12 ต้องไม่มีสัญญาณตอน D5/D6 ทำงาน ===\n";
  resetState();
  countOnePiece(); countOnePiece(); countOnePiece();
  check("ถึง Full ก่อน", fullCounterFlag);

  // สภาพที่หนักที่สุด: ค้างทั้ง OK, NG, LOCK และ D9 มาจากก่อนเข้า Full
  ledOkOn = true; ledNgOn = true; autoFailLock = true; ledMonitorOn = true;

  // เรียกเส้นทางที่เคย digitalWrite ขาสามขานี้เองก่อน แล้วค่อยจบด้วยตัวเขียนขา
  // เหมือนลำดับใน loop() จริง
  handleColorMonitor();
  handleNgDelay();
  handleOkAutoOff();
  handleFullCounterOutput();
  handleFullCounterBlinkOutput();
  applyIndicatorOutputs();

  check("ขา D5 (Full) มีสัญญาณออก", pinLevel[PIN_FULL_OUT] == HIGH);
  check("ขา D9 ไม่มีสัญญาณออก", pinLevel[LED_MONITOR] == LOW);
  check("ขา D11 ไม่มีสัญญาณออก", pinLevel[LED_OK] == LOW);
  check("ขา D12 ไม่มีสัญญาณออก", pinLevel[LED_NG] == LOW);

  // จังหวะที่ D6 กะพริบเป็น HIGH พอดี ขาสามขานั้นก็ยังต้องเงียบ
  FAKE_MS += D6_BLINK_INTERVAL_MS + 1;
  handleFullCounterBlinkOutput();
  if (pinLevel[PIN_FULL_OUT_BLINK] == LOW)
  {
    FAKE_MS += D6_BLINK_INTERVAL_MS + 1;
    handleFullCounterBlinkOutput();
  }
  applyIndicatorOutputs();
  check("จับจังหวะ D6 = HIGH ได้จริง", pinLevel[PIN_FULL_OUT_BLINK] == HIGH);
  check("D6 HIGH -> D9 ยังเงียบ", pinLevel[LED_MONITOR] == LOW);
  check("D6 HIGH -> D12 ยังเงียบ", pinLevel[LED_NG] == LOW);

  // เส้นทางตัดสินผลที่เคยจุดไฟ OK ที่ขาโดยตรง ก็ต้องไม่ทะลุออกมาระหว่าง Full
  setResultOK();
  check("setResultOK ระหว่าง Full -> ขา D11 ยังเงียบ", pinLevel[LED_OK] == LOW);
  countNgTotal = 0;
  setResultNG(true, false, true);
  applyIndicatorOutputs();
  check("setResultNG ระหว่าง Full -> ขา D12 ยังเงียบ", pinLevel[LED_NG] == LOW);

  // ปลด Full แล้วขาต้องกลับมาทำงานตามผลตรวจจริงเหมือนเดิม
  resetState();
  ledNgOn = true; ledMonitorOn = true; ledOkOn = true;
  applyIndicatorOutputs();
  check("ไม่ Full -> ขา D12 ทำงานตามปกติ", pinLevel[LED_NG] == HIGH);
  check("ไม่ Full -> ขา D9 ทำงานตามปกติ", pinLevel[LED_MONITOR] == HIGH);
  check("ไม่ Full -> ขา D11 ทำงานตามปกติ", pinLevel[LED_OK] == HIGH);

  std::cout << (fails ? "\n>>> มีข้อที่ไม่ผ่าน: " : "\n>>> ผ่านทั้งหมด ") << fails << "\n\n";
  return fails ? 1 : 0;
}
