// ===================================================================
// เทสต์คิว event ของ ESP32 (ตัดโค้ดจริงจาก esp32_supabase_rfid.ino มาคอมไพล์)
//
// บั๊กที่กันไว้: ระหว่างที่ HTTP POST บล็อกอยู่ Nano ยังส่ง JSON มาเรื่อยๆ
// หลายบรรทัดจึงมากองใน buffer พร้อมกัน ของเดิมเก็บไว้แค่บรรทัดล่าสุด
// บรรทัดที่มี event จริงเลยถูกทับหาย เหลือลง event_log แค่ไม่กี่แถว
// ===================================================================
#include "Arduino.h"
#include <iostream>
#include <vector>
#include <string>

SerialClass Serial;

// ---- สิ่งที่โค้ดจริงต้องใช้ แต่อยู่คนละส่วนของ .ino ----
std::vector<std::string> uploaded;   // แถวที่ถูกยิงเข้า event_log สำเร็จ
bool networkUp = true;               // จำลองเน็ตหลุด

bool logEventToSupabase(const char* eventStr)
{
  if (!networkUp) return false;
  uploaded.push_back(std::string(eventStr));
  return true;
}

#include "esp32_queue.cpp"           // โค้ดจริงที่ตัดมาจาก .ino

int fails = 0;
void check(const char* name, bool cond)
{
  std::cout << (cond ? "  PASS  " : "  FAIL  ") << name << "\n";
  if (!cond) fails++;
}

// จำลองบรรทัด JSON ที่ Nano ส่งมา
String nanoLine(const char* ev)
{
  return String("{\"setting\":10,\"counting\":3,\"d9\":1,\"ok\":42,\"ng\":7,"
                "\"rate\":2.5,\"event\":\"") + String(ev) + String("\"}");
}

// จำลอง readFromNano() ตอนที่มีหลายบรรทัดกองรออยู่พร้อมกัน
void feedLines(const std::vector<const char*>& lines)
{
  for (size_t i = 0; i < lines.size(); i++)
  {
    String ev = extractEvent(nanoLine(lines[i]));
    if (ev.length() > 0 && ev != "none") pushEvent(ev);
  }
}

void resetAll()
{
  eventQueueHead = 0;
  eventQueueCount = 0;
  uploaded.clear();
  networkUp = true;
}

int main()
{
  std::cout << "\n=== สถานการณ์ที่ 1: แกะ event ออกจากบรรทัด JSON ===\n";
  check("อ่าน event ธรรมดาได้", extractEvent(nanoLine("ok")) == String("ok"));
  check("อ่าน event ที่มี ID หายได้", extractEvent(nanoLine("ng_miss1_2")) == String("ng_miss1_2"));
  check("บรรทัดที่ไม่มีคีย์ event -> คืนค่าว่าง", extractEvent(String("{\"ok\":1}")) == String(""));

  std::cout << "\n=== สถานการณ์ที่ 2: หลายบรรทัดมากองพร้อมกัน (บั๊กเดิม) ===\n";
  // ของเดิมเก็บแค่บรรทัดสุดท้าย ซึ่งคือ "none" -> event_log ไม่ได้อะไรเลย
  resetAll();
  feedLines({ "ok", "none", "ng_miss2", "none", "none" });
  check("เก็บครบทั้ง 2 event ที่เกิดจริง", eventQueueCount == 2);
  drainEventQueue();
  check("ยิงลง event_log ครบ 2 แถว (บั๊กเดิมได้ 0)", uploaded.size() == 2);
  check("แถวแรกคือ ok", uploaded.size() > 0 && uploaded[0] == "ok");
  check("แถวสองคือ ng_miss2", uploaded.size() > 1 && uploaded[1] == "ng_miss2");
  check("ล้างคิวหมดแล้ว", eventQueueCount == 0);

  std::cout << "\n=== สถานการณ์ที่ 3: เทส 60 ชิ้นรวด ต้องได้ครบ 60 แถว ===\n";
  resetAll();
  int sent = 0;
  for (int i = 0; i < 60; i++)
  {
    // สลับ ok / ng เหมือนเทสจริง แล้วมี none คั่นระหว่างรอบส่ง
    feedLines({ (i % 4 == 0) ? "ng_miss1" : "ok", "none" });
    // อัปโหลดทุกๆ 5 ชิ้น (จำลองรอบ upload ที่ช้ากว่า Nano ส่ง)
    if (i % 5 == 4) { drainEventQueue(); sent = (int)uploaded.size(); }
  }
  drainEventQueue();
  check("ได้ครบ 60 แถว ไม่ตกหล่น", uploaded.size() == 60);
  check("อัปโหลดเป็นระยะระหว่างทาง ไม่ค้างจนคิวล้น", sent > 0);

  std::cout << "\n=== สถานการณ์ที่ 4: เน็ตหลุด -> ต้องคาไว้ในคิว ไม่ทิ้ง ===\n";
  resetAll();
  feedLines({ "ok", "ng_miss2" });
  networkUp = false;
  drainEventQueue();
  check("ส่งไม่ได้ -> ยังไม่มีแถวขึ้น Supabase", uploaded.size() == 0);
  check("event ยังคาอยู่ในคิวครบ", eventQueueCount == 2);
  networkUp = true;
  drainEventQueue();
  check("เน็ตกลับมา -> ส่งครบทั้ง 2 แถว", uploaded.size() == 2);

  std::cout << "\n=== สถานการณ์ที่ 5: เน็ตหลุดยาว คิวเต็ม -> ทิ้งตัวเก่าสุด ===\n";
  resetAll();
  networkUp = false;
  for (int i = 0; i < EVENT_QUEUE_SIZE + 5; i++) feedLines({ "ok" });
  check("คิวไม่ล้นเกินขนาดที่กันไว้", eventQueueCount == EVENT_QUEUE_SIZE);
  networkUp = true;
  drainEventQueue();
  check("ส่งได้เท่าที่คิวเก็บไว้", uploaded.size() == EVENT_QUEUE_SIZE);

  std::cout << (fails ? "\n>>> มีข้อที่ไม่ผ่าน: " : "\n>>> ผ่านทั้งหมด ") << fails << "\n\n";
  return fails ? 1 : 0;
}
