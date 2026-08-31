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
#include <string>

SerialClass Serial;
unsigned long FAKE_MS = 100000;
unsigned long millis() { return FAKE_MS; }

bool forceUploadNow = false;
unsigned long lastUploadTime = 0;
const unsigned long UPLOAD_INTERVAL_MS = 2000;

// จำลอง Serial2 (สาย ESP32 <-> Nano) เก็บทุกครั้งที่ armResetSuppression()/
// retryPendingResetIfNeeded() ส่งคำสั่งออกไป เพื่อนับว่าส่งซ้ำจริงกี่ครั้ง
struct FakeSerial2 {
  std::vector<std::string> sent;
  void print(const char* s) { sent.push_back(s); }
  void print(const String& s) { sent.push_back(s.c_str()); }
};
FakeSerial2 Serial2;

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
  pendingResetCmd = "";
  lastResetRetrySentTime = 0;
  Serial2.sent.clear();
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
  armResetSuppression("RESET_TOTAL\n");   // เทียบเท่า checkPendingCommands() ส่ง RESET_TOTAL ให้ Nano
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
  armResetSuppression("RESET_TOTAL\n");
  bool wrongEvent = clearResetSuppressionIfConfirmed(String("ok"));
  check("event ok ไม่ใช่ตัวยืนยันรีเซ็ต", !wrongEvent);
  FAKE_MS += UPLOAD_INTERVAL_MS;
  check("ยังพักอัปโหลดต่อ เพราะยังไม่เห็นตัวยืนยันจริง", !shouldUploadStatusNow(FAKE_MS));

  std::cout << "\n=== สถานการณ์ที่ 5: Nano ไม่ตอบเลย -> ต้องมี timeout กันค้างถาวร ===\n";
  resetAll();
  armResetSuppression("RESET_TOTAL\n");
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
  armResetSuppression("RESET_TOTAL\n");
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

  std::cout << "\n=== สถานการณ์ที่ 7: ส่งครั้งแรกหายไปเฉยๆ (SoftwareSerial ชนกับกล้อง) -> ต้องมีการส่งซ้ำ ===\n";
  resetAll();
  armResetSuppression("RESET_TOTAL\n");
  check("ส่งครั้งแรกทันทีตอน arm", Serial2.sent.size() == 1);
  // จำลองว่า Nano ไม่เคยได้รับเลย (กำลังฟังฝั่งกล้องอยู่พอดีตอนส่งครั้งแรก)
  // ปล่อยเวลาผ่านไปหลายรอบ retry โดยไม่มี event ยืนยันกลับมาเลย
  for (int i = 0; i < 5; i++)
  {
    FAKE_MS += RESET_RETRY_INTERVAL_MS;
    retryPendingResetIfNeeded(FAKE_MS);
  }
  check("ส่งซ้ำครบ 5 ครั้งตามที่ปล่อยเวลาไป (รวมครั้งแรกเป็น 6)", Serial2.sent.size() == 6);
  check("ทุกครั้งที่ส่งเป็นคำสั่งเดียวกัน", Serial2.sent.back() == "RESET_TOTAL\n");

  std::cout << "\n=== สถานการณ์ที่ 8: พอ Nano ยืนยัน (ไม่ว่าจะรอบไหน) ต้องหยุดส่งซ้ำทันที ===\n";
  clearResetSuppressionIfConfirmed(String("total_reset"));
  size_t sentBeforeStop = Serial2.sent.size();
  for (int i = 0; i < 5; i++)
  {
    FAKE_MS += RESET_RETRY_INTERVAL_MS;
    retryPendingResetIfNeeded(FAKE_MS);
  }
  check("เลิกส่งซ้ำทันทีที่ยืนยันแล้ว ไม่ยิงเพิ่มอีกเลย", Serial2.sent.size() == sentBeforeStop);
  check("pendingResetCmd ถูกล้างแล้ว", pendingResetCmd.length() == 0);

  std::cout << "\n=== สถานการณ์ที่ 9: ยังไม่ครบจังหวะ retry -> ไม่ส่งซ้ำก่อนเวลา ===\n";
  resetAll();
  armResetSuppression("RESET_TOTAL\n");
  size_t afterFirst = Serial2.sent.size();
  FAKE_MS += RESET_RETRY_INTERVAL_MS - 1;   // ยังไม่ครบช่วงเวลา
  retryPendingResetIfNeeded(FAKE_MS);
  check("ยังไม่ครบจังหวะ -> ยังไม่ส่งซ้ำ", Serial2.sent.size() == afterFirst);

  std::cout << (fails ? "\n>>> มีข้อที่ไม่ผ่าน: " : "\n>>> ผ่านทั้งหมด ") << fails << "\n\n";
  return fails ? 1 : 0;
}
