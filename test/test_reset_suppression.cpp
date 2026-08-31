// ===================================================================
// เทสต์กันบั๊ก: กด "ล้างยอดรวม"/"คืนค่าทั้งหมด" แล้วค่าเด้งกลับเป็นค่าเก่า
//
// สาเหตุเดิม: ฟังก์ชัน SQL ตั้ง status.ok_total/ng_total = 0 ใน Supabase
// ทันทีตอนกดปุ่ม แต่ ESP32 ยังอัปโหลดค่าที่ Nano รายงานล่าสุด (ซึ่งยังเป็น
// ค่าเก่า เพราะ Nano ยังไม่ได้รับคำสั่งรีเซ็ต) ทับเข้าไปเป็นระยะ ก่อนจะ
// เด้งกลับมา 0 อีกทีตอน Nano ประมวลผลเสร็จจริง
// ===================================================================
#include "Arduino.h"
#include <iostream>
#include <vector>

SerialClass Serial;
unsigned long FAKE_MS = 100000;
unsigned long millis() { return FAKE_MS; }

bool forceUploadNow = false;
unsigned long lastUploadTime = 0;
const unsigned long UPLOAD_INTERVAL_MS = 2000;

#include "esp32_suppression.cpp"   // โค้ดจริงที่ตัดมาจาก .ino

int fails = 0;
void check(const char* name, bool cond)
{
  std::cout << (cond ? "  PASS  " : "  FAIL  ") << name << "\n";
  if (!cond) fails++;
}

void resetAll()
{
  FAKE_MS = 100000;
  suppressStatusUpload = false;
  suppressStatusUploadSince = 0;
  forceUploadNow = false;
  lastUploadTime = FAKE_MS;   // เพิ่งอัปโหลดไปหมาดๆ ไม่ใช่ตั้งแต่เวลา 0
}

int main()
{
  std::cout << "\n=== สถานการณ์ที่ 1: ปกติไม่มีรีเซ็ตค้าง -> อัปโหลดตามรอบปกติ ===\n";
  resetAll();
  check("ยังไม่ถึงรอบ -> ไม่อัปโหลด", !shouldUploadStatusNow(FAKE_MS));
  FAKE_MS += UPLOAD_INTERVAL_MS;
  check("ครบรอบ -> อัปโหลดได้", shouldUploadStatusNow(FAKE_MS));

  std::cout << "\n=== สถานการณ์ที่ 2: กดรีเซ็ต -> ต้องพักอัปโหลดจนกว่า Nano ยืนยัน (บั๊กเดิม) ===\n";
  resetAll();
  armResetSuppression();   // เทียบเท่า checkPendingCommands() ส่ง RESET_TOTAL ให้ Nano
  FAKE_MS += UPLOAD_INTERVAL_MS;   // ครบรอบอัปโหลดปกติแล้ว แต่ Nano ยังไม่ตอบ
  check("ค้างอัปโหลดยอดเก่าไม่ได้ (บั๊กเดิมจะ FAIL ตรงนี้)", !shouldUploadStatusNow(FAKE_MS));
  FAKE_MS += 500;
  check("ยังพักต่อ ถ้ายังไม่เห็น event ยืนยัน", !shouldUploadStatusNow(FAKE_MS));

  std::cout << "\n=== สถานการณ์ที่ 3: Nano ยืนยันกลับมา -> ปล่อยอัปโหลดค่า 0 ที่ถูกต้องทันที ===\n";
  bool confirmed = clearResetSuppressionIfConfirmed(String("total_reset"));
  check("จำ event ยืนยันได้ถูกตัว", confirmed);
  forceUploadNow = true;   // readFromNano() ตั้งค่านี้คู่กับการเช็ค event เสมอ
  check("อัปโหลดได้ทันทีหลังยืนยัน", shouldUploadStatusNow(FAKE_MS));

  std::cout << "\n=== สถานการณ์ที่ 4: event ระหว่างทางที่ไม่ใช่ตัวยืนยัน ไม่ปลดล็อกอัปโหลด ===\n";
  resetAll();
  armResetSuppression();
  bool wrongEvent = clearResetSuppressionIfConfirmed(String("ok"));
  check("event ok ไม่ใช่ตัวยืนยันรีเซ็ต", !wrongEvent);
  FAKE_MS += UPLOAD_INTERVAL_MS;
  check("ยังพักอัปโหลดต่อ เพราะยังไม่เห็นตัวยืนยันจริง", !shouldUploadStatusNow(FAKE_MS));

  std::cout << "\n=== สถานการณ์ที่ 5: Nano ไม่ตอบเลย -> ต้องมี timeout กันค้างถาวร ===\n";
  resetAll();
  armResetSuppression();
  FAKE_MS += SUPPRESS_TIMEOUT_MS - 1;
  check("ยังไม่ครบ timeout -> ยังพักอยู่", !shouldUploadStatusNow(FAKE_MS));
  FAKE_MS += 2;
  check("ครบ timeout -> เลิกพักเอง ไม่ค้างตลอดไป", shouldUploadStatusNow(FAKE_MS));
  check("flag ถูกปลดหลัง timeout", !suppressStatusUpload);

  std::cout << "\n=== สถานการณ์ที่ 6: จำลองไทม์ไลน์เต็ม เหมือนที่ผู้ใช้เจอจริง ===\n";
  resetAll();
  // T=0: ผู้ใช้กดปุ่มบนเว็บ -> SQL ตั้ง status.ok_total=0 ใน Supabase ทันที
  //      (ไม่ได้จำลองใน C++ เพราะเป็นฝั่ง SQL แยกต่างหาก)
  // T=0: checkPendingCommands() (รอบถัดไปของ ESP32) ส่ง RESET_TOTAL ให้ Nano
  armResetSuppression();
  // T=0..~1800ms: ESP32 ยังวนอัปโหลดสถานะเป็นระยะ แต่ Nano ยังไม่รู้เรื่องรีเซ็ต
  int blockedUploads = 0;
  for (int t = 0; t < 1800; t += 300)
  {
    FAKE_MS += 300;
    if (shouldUploadStatusNow(FAKE_MS)) blockedUploads++;
  }
  check("ตลอดช่วงรอ Nano ตอบ ไม่มีการอัปโหลดค่าเก่าทับเลยสักครั้ง (นี่คือบั๊กที่ผู้ใช้เจอ)",
        blockedUploads == 0);
  // T=~2000ms: Nano ประมวลผลเสร็จ ส่ง event total_reset กลับมา
  clearResetSuppressionIfConfirmed(String("total_reset"));
  forceUploadNow = true;
  check("พอ Nano ยืนยัน -> อัปโหลดค่า 0 ที่ถูกต้องได้ทันที ไม่ต้องรอรอบถัดไป",
        shouldUploadStatusNow(FAKE_MS));

  std::cout << (fails ? "\n>>> มีข้อที่ไม่ผ่าน: " : "\n>>> ผ่านทั้งหมด ") << fails << "\n\n";
  return fails ? 1 : 0;
}
