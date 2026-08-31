/*
  ===================================================================
  ESP32: Supabase Uploader + RFID Key Card Reset (A0 เท่านั้น)
  ===================================================================

  หน้าที่:
   - ต่อ WiFi บ้าน แล้วส่งสถานะจาก Nano ขึ้น Supabase
   - รับคำสั่งควบคุมจากเว็บ ส่งต่อไป Nano
     (RESET / RESET_COUNT / SET / RESET_TOTAL / FACTORY_RESET)
   - อ่านบัตร RFID-RC522: สแกนบัตรใบไหนก็ได้ = สั่ง Reset A0
     (เคลียร์ NG/LOCK) โดยดึงขา output ลง LOW จำลองการกดปุ่มที่ Nano

  ===================================================================
  ค่าความลับ (WiFi / Supabase)
  -------------------------------------------------------------------
  อยู่ในไฟล์ arduino_secrets.h ซึ่งไม่ถูก commit ขึ้น git
  ครั้งแรกที่เปิดโปรเจกต์ ให้ก๊อป arduino_secrets.example.h
  เป็น arduino_secrets.h แล้วใส่ค่าจริงของตัวเอง

  ===================================================================
  การต่อสาย
  -------------------------------------------------------------------

  [1] ESP32 <-> Arduino Nano (Serial)
      ESP32 GPIO16 (RX2) <---- Nano D8 (TX)
      ESP32 GPIO17 (TX2) ----> Nano D7 (RX)
      GND ต่อร่วมกัน

  [2] ESP32 <-> RFID-RC522 (SPI)
      *** RC522 ใช้ไฟ 3.3V เท่านั้น ห้ามต่อ 5V จะพังทันที ***
      RC522 SDA (SS)  ----> ESP32 GPIO5
      RC522 SCK       ----> ESP32 GPIO18
      RC522 MOSI      ----> ESP32 GPIO23
      RC522 MISO      ----> ESP32 GPIO19
      RC522 RST       ----> ESP32 GPIO4
      RC522 3.3V      ----> ESP32 3V3
      RC522 GND       ----> ESP32 GND
      RC522 IRQ       ----> ไม่ต้องต่อ

  [3] ESP32 -> Nano (สายสั่ง Reset แบบไฟลบ / active-LOW)
      ESP32 GPIO26 ----> Nano A0  (เคลียร์ NG/LOCK)
      GND ต่อร่วมกัน (ใช้ GND เส้นเดียวกับข้อ [1] ได้)

      *** หมายเหตุสำคัญเรื่องขา ***
      เดิมต้องการใช้ D34/D35 แต่ GPIO34 และ GPIO35 ของ ESP32
      เป็นขา INPUT-ONLY ใช้เป็น output ไม่ได้ (ไม่มีวงจรขับกระแส)
      จึงเปลี่ยนมาใช้ GPIO26 แทน ซึ่งเป็นขา I/O ปกติ
      และไม่ชนกับขา strapping หรือขา SPI/UART ที่ใช้อยู่

      วิธีทำงานของขา output:
      - สภาวะปกติ: ตั้งเป็น INPUT (ปล่อยลอย) ให้ pull-up ภายในของ
        Nano ดึงขาเป็น HIGH เอง = เหมือนไม่ได้กดปุ่ม
      - ตอนสั่งงาน: เปลี่ยนเป็น OUTPUT LOW ค้างไว้ 300ms = เหมือนกดปุ่ม
        แล้วกลับเป็น INPUT เหมือนเดิม
      วิธีนี้ปลอดภัยกว่าการขับ HIGH ตรงๆ เพราะไม่มีไฟ 3.3V ชนกับ
      pull-up 5V ของ Nano

      หมายเหตุ: การเคลียร์ Counting (A7) ยังใช้ปุ่ม A7 จริงที่บอร์ด
      หรือปุ่มบนเว็บได้ตามปกติ ไม่ได้ผูกกับการสแกนบัตร

  ===================================================================
  ไลบรารีที่ต้องติดตั้ง (Library Manager):
   - ArduinoJson (เวอร์ชัน 6.x)
   - MFRC522 (โดย GithubCommunity)
  ===================================================================
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>

// ================================================================
// WIFI / SUPABASE
// ================================================================
//
// มี 2 ทางเลือก เลือกทางไหนก็ได้:
//
//   ทาง A (แนะนำ) - วางไฟล์ arduino_secrets.h ไว้โฟลเดอร์เดียวกับ .ino นี้
//                   ก๊อปมาจาก arduino_secrets.example.h แล้วใส่ค่าจริง
//                   ไฟล์นี้อยู่ใน .gitignore รหัส WiFi จึงไม่หลุดขึ้น GitHub
//
//   ทาง B         - ไม่ต้องมีไฟล์นั้นเลย แก้ค่าตรงบล็อกข้างล่างนี้ได้เลย
//                   (ถ้าจะ commit โค้ดขึ้น GitHub อย่าลืมลบรหัสออกก่อน)
//
// ถ้ามีไฟล์ arduino_secrets.h อยู่ ค่าจากไฟล์นั้นจะถูกใช้เสมอ
// ถ้าไม่มี ก็ยังคอมไพล์ผ่านโดยใช้ค่าที่กรอกไว้ข้างล่าง
// ================================================================

#if defined(__has_include)
  #if __has_include("arduino_secrets.h")
    #include "arduino_secrets.h"
  #endif
#endif

// ---------- แก้ 2 บรรทัดนี้ ถ้าไม่ได้ใช้ arduino_secrets.h ----------
#ifndef SECRET_WIFI_SSID
  #define SECRET_WIFI_SSID      "ใส่ชื่อ WiFi ที่นี่"
#endif

#ifndef SECRET_WIFI_PASSWORD
  #define SECRET_WIFI_PASSWORD  "ใส่รหัสผ่าน WiFi ที่นี่"
#endif
// -------------------------------------------------------------------

// Supabase anon key ไม่ใช่ความลับ ถูกออกแบบมาให้ฝังในเบราว์เซอร์อยู่แล้ว
// ความปลอดภัยจริงมาจาก RLS policy บน Supabase (anon สั่งงานเครื่องไม่ได้)
#ifndef SECRET_SUPABASE_URL
  #define SECRET_SUPABASE_URL "https://ufmwcstlzygrnmzkpgbs.supabase.co"
#endif

#ifndef SECRET_SUPABASE_ANON_KEY
  #define SECRET_SUPABASE_ANON_KEY "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InVmbXdjc3Rsenlncm5temtwZ2JzIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODc3NzgxOTgsImV4cCI6MjEwMzM1NDE5OH0.geNcAdB1YL2N9XozB49uyu-__N5KyRg4gKuj0ftOD1g"
#endif

const char* WIFI_SSID     = SECRET_WIFI_SSID;
const char* WIFI_PASSWORD = SECRET_WIFI_PASSWORD;

const char* SUPABASE_URL      = SECRET_SUPABASE_URL;
const char* SUPABASE_ANON_KEY = SECRET_SUPABASE_ANON_KEY;

// ยังไม่ได้ใส่ค่า WiFi -> เตือนทาง Serial Monitor ตอนบูต
// ไม่งั้นจะงงว่าทำไมต่อเน็ตไม่ติด ทั้งที่คอมไพล์ผ่านและอัปโหลดสำเร็จ
const bool WIFI_NOT_CONFIGURED = (strcmp(WIFI_SSID, "ใส่ชื่อ WiFi ที่นี่") == 0);


// ================================================================
// PIN CONFIGURATION
// ================================================================

// Serial ไป Nano
#define NANO_RX_PIN  16
#define NANO_TX_PIN  17

// RFID-RC522 (SPI)
#define RFID_SS_PIN   5
#define RFID_RST_PIN  4
#define RFID_SCK_PIN  18
#define RFID_MOSI_PIN 23
#define RFID_MISO_PIN 19

// สายสั่ง Reset ไป Nano (active-LOW)
// เดิมกำหนด D34 แต่เป็น input-only จึงเปลี่ยนเป็น GPIO26
#define PIN_CMD_RESET_NG  26   // -> Nano A0 (เคลียร์ NG/LOCK)

const unsigned long PULSE_DURATION_MS = 300;   // ระยะเวลากดปุ่มจำลอง


// ================================================================
// RFID
// ================================================================

MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

unsigned long lastCardScanTime = 0;
const unsigned long CARD_SCAN_LOCKOUT_MS = 3000;   // กันสแกนซ้ำถี่เกินไป


// ================================================================
// สถานะจาก Nano
// ================================================================

String latestJson = "";
String rxBuffer = "";
bool haveNewData = false;

// Nano ส่ง JSON ทุก 500ms แต่ ESP32 อัปโหลดทุก 2000ms
// ถ้ารอรอบปกติ event ที่เกิดใน 3 บรรทัดแรกจะถูกทับหายไป
// จึงบังคับอัปโหลดทันทีเมื่อบรรทัดที่รับมามี event ที่ไม่ใช่ "none"
bool forceUploadNow = false;

// ----------------------------------------------------------------
// คิว event ที่ยังไม่ได้บันทึกลง event_log
//
// ทำไมต้องมีคิว: readFromNano() อ่านทุกบรรทัดที่ค้างอยู่ใน buffer รวดเดียว
// แต่เก็บไว้ได้แค่บรรทัดล่าสุดบรรทัดเดียว (latestJson) ระหว่างที่ HTTP POST
// ทำงานอยู่ (บล็อกเป็นร้อย ms) Nano ยังส่งมาเรื่อยๆ หลายบรรทัดจึงมากอง
// รอพร้อมกัน พอวนอ่านครั้งถัดไปบรรทัดที่มี event จริงจะถูกบรรทัดหลังทับหมด
// และบรรทัดสุดท้ายมักเป็น event "none" เพราะ Nano ล้างค่าทุกครั้งหลังส่ง
// ผลคือชิ้นงานที่เทสไปหลายสิบชิ้นเหลือลง event_log แค่ไม่กี่แถว
//
// แก้โดยดึง event ออกจาก "ทุกบรรทัด" ที่อ่านได้ แล้วพักไว้ในคิว
// ค่อยยิงลง Supabase ให้ครบทุกตัวตอนอัปโหลดรอบถัดไป
// ----------------------------------------------------------------
#define EVENT_QUEUE_SIZE 16
String eventQueue[EVENT_QUEUE_SIZE];
uint8_t eventQueueHead  = 0;
uint8_t eventQueueCount = 0;


// ================================================================
// TIMING
// ================================================================

unsigned long lastUploadTime = 0;
const unsigned long UPLOAD_INTERVAL_MS = 2000;

unsigned long lastCommandCheckTime = 0;
const unsigned long COMMAND_CHECK_INTERVAL_MS = 2000;

unsigned long lastWifiRetryTime = 0;
const unsigned long WIFI_RETRY_INTERVAL_MS = 5000;


// ================================================================
// SETUP
// ================================================================

void setup()
{
  Serial.begin(115200);

  // ขยาย buffer ก่อน begin() เพราะ HTTP POST บล็อกได้หลายวินาที
  // ระหว่างนั้น Nano ยังส่ง JSON เข้ามาเรื่อยๆ buffer เดิม 256 ไบต์จะล้น
  // แล้วได้ JSON ที่ขาดกลาง แปลงไม่ผ่าน
  Serial2.setRxBufferSize(1024);
  Serial2.begin(9600, SERIAL_8N1, NANO_RX_PIN, NANO_TX_PIN);

  // ตั้งขาสั่ง Reset เป็น INPUT (ลอย) = สภาวะ "ไม่ได้กดปุ่ม"
  releaseCommandPin(PIN_CMD_RESET_NG);

  // เริ่มต้น SPI + RFID
  SPI.begin(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, RFID_SS_PIN);
  rfid.PCD_Init();

  Serial.println();
  Serial.println(F("=============================="));
  Serial.println(F("ESP32 Supabase + RFID Reset"));
  Serial.println(F("=============================="));

  // ตรวจสอบว่าเจอโมดูล RFID ไหม
  byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg);

  if (version == 0x00 || version == 0xFF)
  {
    Serial.println(F("*** ไม่พบโมดูล RFID-RC522 ตรวจสอบการต่อสาย/ไฟ 3.3V ***"));
  }
  else
  {
    Serial.print(F("พบโมดูล RFID-RC522 (version 0x"));
    Serial.print(version, HEX);
    Serial.println(F(")"));
  }

  connectWifi();
}

void connectWifi()
{
  if (WIFI_NOT_CONFIGURED)
  {
    Serial.println(F("!! ยังไม่ได้ใส่ชื่อ/รหัส WiFi"));
    Serial.println(F("!! แก้ที่ arduino_secrets.h หรือที่บล็อก SECRET_WIFI_* ด้านบนของไฟล์ .ino"));
  }

  Serial.print(F("กำลังเชื่อมต่อ WiFi: "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000)
  {
    delay(300);
    Serial.print(F("."));
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println(F("เชื่อมต่อ WiFi สำเร็จ"));
    Serial.print(F("IP Address: "));
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println(F("เชื่อมต่อ WiFi ไม่สำเร็จ จะลองใหม่เรื่อยๆ"));
  }
}


// ================================================================
// LOOP
// ================================================================

void loop()
{
  ensureWifiConnected();

  readFromNano();

  handleRfidScan();   // อ่านบัตร RFID

  unsigned long now = millis();

  if (forceUploadNow || (now - lastUploadTime >= UPLOAD_INTERVAL_MS))
  {
    lastUploadTime = now;
    forceUploadNow = false;

    if (haveNewData && WiFi.status() == WL_CONNECTED)
    {
      uploadStatusToSupabase();
    }
  }

  if (now - lastCommandCheckTime >= COMMAND_CHECK_INTERVAL_MS)
  {
    lastCommandCheckTime = now;

    if (WiFi.status() == WL_CONNECTED)
    {
      checkPendingCommands();
    }
  }
}


// ================================================================
// ขาสั่งงานแบบ active-LOW (จำลองการกดปุ่มที่ Nano)
// ================================================================

// สภาวะปกติ: ปล่อยขาลอย ให้ pull-up ของ Nano ดึงเป็น HIGH เอง
void releaseCommandPin(int pin)
{
  pinMode(pin, INPUT);
}

// สั่งงาน: ดึงขาลง LOW ค้างไว้ตามเวลาที่กำหนด แล้วปล่อยลอยเหมือนเดิม
void pulseCommandPin(int pin, const char* label)
{
  Serial.print(F("PULSE LOW -> "));
  Serial.println(label);

  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);

  delay(PULSE_DURATION_MS);

  releaseCommandPin(pin);
}


// ================================================================
// RFID: สแกนบัตรใบไหนก็ได้ = สั่ง Reset A0 (เคลียร์ NG/LOCK)
// ================================================================

void handleRfidScan()
{
  // กันสแกนซ้ำถี่เกินไป
  if (millis() - lastCardScanTime < CARD_SCAN_LOCKOUT_MS)
  {
    return;
  }

  if (!rfid.PICC_IsNewCardPresent())
  {
    return;
  }

  if (!rfid.PICC_ReadCardSerial())
  {
    return;
  }

  lastCardScanTime = millis();

  // อ่าน UID ของบัตร (ใช้บันทึก log เท่านั้น ไม่ได้ใช้ตรวจสอบสิทธิ์)
  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++)
  {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }

  uid.toUpperCase();

  Serial.println();
  Serial.print(F("สแกนบัตร UID: "));
  Serial.println(uid);
  Serial.println(F("อนุญาต -> สั่ง Reset A0 (เคลียร์ NG/LOCK)"));

  // สั่ง Reset เฉพาะ A0 เท่านั้น
  pulseCommandPin(PIN_CMD_RESET_NG, "A0 (เคลียร์ NG/LOCK)");

  // บันทึกลง Supabase ว่ามีการสแกนบัตร
  if (WiFi.status() == WL_CONNECTED)
  {
    logEventToSupabase(("card_reset:" + uid).c_str());
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}


// ================================================================
// WIFI RECONNECT
// ================================================================

void ensureWifiConnected()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    return;
  }

  unsigned long now = millis();

  if (now - lastWifiRetryTime >= WIFI_RETRY_INTERVAL_MS)
  {
    lastWifiRetryTime = now;

    Serial.println(F("WiFi หลุด กำลังลองเชื่อมต่อใหม่..."));
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}


// ================================================================
// อ่านข้อมูล JSON จาก Nano
// ================================================================

// เข้าคิว event ไว้รออัปโหลด
void pushEvent(const String& ev)
{
  if (eventQueueCount >= EVENT_QUEUE_SIZE)
  {
    // คิวเต็ม (เน็ตล่มนาน) ทิ้งตัวเก่าสุดเพื่อให้ของใหม่เข้ามาได้
    eventQueueHead = (eventQueueHead + 1) % EVENT_QUEUE_SIZE;
    eventQueueCount--;

    Serial.println(F("คิว event เต็ม - ทิ้งตัวเก่าสุด"));
  }

  eventQueue[(eventQueueHead + eventQueueCount) % EVENT_QUEUE_SIZE] = ev;
  eventQueueCount++;
}

// ดึงค่าของคีย์ "event" ออกจากบรรทัด JSON โดยไม่ต้อง parse ทั้งก้อน
// (ทำแบบเบาๆ เพราะเรียกทุกบรรทัดที่รับเข้ามา)
String extractEvent(const String& json)
{
  int k = json.indexOf("\"event\":\"");
  if (k < 0) return String("");

  int start = k + 9;                      // ความยาวของ "event":"
  int end   = json.indexOf('"', start);
  if (end < 0) return String("");

  return json.substring(start, end);
}

void readFromNano()
{
  while (Serial2.available())
  {
    char c = Serial2.read();

    if (c == '\n')
    {
      rxBuffer.trim();

      if (rxBuffer.length() > 0 && rxBuffer.startsWith("{"))
      {
        latestJson = rxBuffer;
        haveNewData = true;

        // เก็บ event ของ "ทุกบรรทัด" ไว้ในคิว ไม่ใช่แค่บรรทัดล่าสุด
        // (ok / ng_miss1 / auto_fail_miss2 / total_reset / factory_reset ฯลฯ)
        String ev = extractEvent(latestJson);

        if (ev.length() > 0 && ev != "none")
        {
          pushEvent(ev);
          forceUploadNow = true;   // มี event จริง ต้องรีบส่งขึ้นเว็บ
        }
      }

      rxBuffer = "";
    }
    else if (c != '\r')
    {
      rxBuffer += c;

      if (rxBuffer.length() > 300)
      {
        rxBuffer = "";
      }
    }
  }
}


// ================================================================
// อัปโหลดสถานะขึ้น Supabase
// ================================================================

void uploadStatusToSupabase()
{
  StaticJsonDocument<400> doc;
  DeserializationError err = deserializeJson(doc, latestJson);

  if (err)
  {
    Serial.print(F("แปลง JSON จาก Nano ไม่สำเร็จ: "));
    Serial.println(err.c_str());

    // ทิ้งข้อมูลที่เสียไปเลย ไม่งั้นจะวนพยายามส่งซ้ำกับบรรทัดเดิมตลอด
    haveNewData = false;
    return;
  }

  // ตรวจสอบว่ามีฟิลด์ครบก่อนส่ง
  const char* requiredFields[] = {
    "setting", "counting", "d9", "d11", "d12",
    "lockOld", "lockAuto", "full", "ok", "ng", "rate", "event"
  };

  for (int i = 0; i < 12; i++)
  {
    if (!doc.containsKey(requiredFields[i]))
    {
      Serial.print(F("JSON จาก Nano ขาดฟิลด์: "));
      Serial.println(requiredFields[i]);

      haveNewData = false;
      return;
    }
  }

  StaticJsonDocument<400> body;
  body["id"]           = 1;
  body["setting"]      = doc["setting"];
  body["counting"]     = doc["counting"];
  body["full_counter"] = doc["full"].as<int>() == 1;
  body["d9"]           = doc["d9"].as<int>() == 1;
  body["d11"]          = doc["d11"].as<int>() == 1;
  body["d12"]          = doc["d12"].as<int>() == 1;
  body["lock_old"]     = doc["lockOld"].as<int>() == 1;
  body["lock_auto"]    = doc["lockAuto"].as<int>() == 1;
  body["ok_total"]     = doc["ok"];
  body["ng_total"]     = doc["ng"];
  body["rate"]         = doc["rate"];

  // บรรทัดล่าสุดมัก event = "none" (Nano ล้างค่าทุกครั้งหลังส่ง)
  // ถ้ามี event จริงค้างคิวอยู่ ให้เอาตัวล่าสุดขึ้นไปแสดงบนแดชบอร์ดแทน
  if (eventQueueCount > 0)
  {
    body["event"] = eventQueue[(eventQueueHead + eventQueueCount - 1) % EVENT_QUEUE_SIZE];
  }
  else
  {
    body["event"] = doc["event"].as<const char*>();
  }

  String bodyStr;
  serializeJson(body, bodyStr);

  HTTPClient http;

  String url = String(SUPABASE_URL) + "/rest/v1/status?on_conflict=id";

  http.begin(url);
  http.addHeader("apikey", SUPABASE_ANON_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "resolution=merge-duplicates,return=minimal");

  int httpCode = http.POST(bodyStr);

  if (httpCode > 0 && httpCode < 300)
  {
    Serial.print(F("อัปโหลดสถานะสำเร็จ ("));
    Serial.print(httpCode);
    Serial.println(F(")"));
  }
  else
  {
    Serial.print(F("อัปโหลดสถานะล้มเหลว code="));
    Serial.println(httpCode);
    Serial.println(http.getString());
  }

  http.end();

  // บันทึกให้ครบทุก event ที่ค้างคิวอยู่ ไม่ใช่แค่ตัวที่ติดมากับบรรทัดล่าสุด
  drainEventQueue();

  haveNewData = false;
}


// บันทึก event ที่ค้างคิวลง event_log ให้ครบทุกตัว
// ตัวไหนส่งไม่สำเร็จ (เน็ตหลุด) ให้คาไว้ในคิวเพื่อลองใหม่รอบหน้า
void drainEventQueue()
{
  while (eventQueueCount > 0)
  {
    if (!logEventToSupabase(eventQueue[eventQueueHead].c_str()))
    {
      break;
    }

    eventQueueHead = (eventQueueHead + 1) % EVENT_QUEUE_SIZE;
    eventQueueCount--;
  }
}


// ================================================================
// บันทึก event ลง Supabase
// ================================================================

bool logEventToSupabase(const char* eventStr)
{
  StaticJsonDocument<128> body;
  body["event"] = eventStr;

  String bodyStr;
  serializeJson(body, bodyStr);

  HTTPClient http;

  String url = String(SUPABASE_URL) + "/rest/v1/event_log";

  http.begin(url);
  http.addHeader("apikey", SUPABASE_ANON_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  int httpCode = http.POST(bodyStr);
  bool ok = (httpCode > 0 && httpCode < 300);

  if (!ok)
  {
    Serial.print(F("บันทึก event_log ล้มเหลว code="));
    Serial.print(httpCode);
    Serial.print(F(" event="));
    Serial.println(eventStr);
  }

  http.end();

  return ok;
}


// ================================================================
// เช็คคำสั่งจากเว็บ
// ================================================================

void checkPendingCommands()
{
  HTTPClient http;

  String url = String(SUPABASE_URL) +
               "/rest/v1/commands?processed=eq.false&order=id.asc&limit=1";

  http.begin(url);
  http.addHeader("apikey", SUPABASE_ANON_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);

  int httpCode = http.GET();

  if (httpCode == 200)
  {
    String payload = http.getString();

    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, payload);

    if (!err && doc.is<JsonArray>() && doc.size() > 0)
    {
      JsonObject row = doc[0];

      long id = row["id"];
      const char* cmd = row["cmd"];

      http.end();

      if (cmd != nullptr)
      {
        if (strcmp(cmd, "RESET") == 0)
        {
          Serial2.print("RESET\n");
          Serial.println(F("-> Nano: RESET (เคลียร์ NG/LOCK จากเว็บ)"));
        }
        else if (strcmp(cmd, "RESET_COUNT") == 0)
        {
          Serial2.print("RESET_COUNT\n");
          Serial.println(F("-> Nano: RESET_COUNT (เคลียร์ Counting จากเว็บ)"));
        }
        else if (strcmp(cmd, "RESET_TOTAL") == 0)
        {
          Serial2.print("RESET_TOTAL\n");
          Serial.println(F("-> Nano: RESET_TOTAL (ล้างยอดรวมสะสม OK/NG)"));
        }
        else if (strcmp(cmd, "FACTORY_RESET") == 0)
        {
          Serial2.print("FACTORY_RESET\n");
          Serial.println(F("-> Nano: FACTORY_RESET (คืนค่าทั้งหมด)"));
        }
        else if (strcmp(cmd, "SET") == 0)
        {
          int value = row["value"] | -1;

          if (value >= 0)
          {
            Serial2.print("SET:");
            Serial2.print(value);
            Serial2.print("\n");

            Serial.print(F("-> Nano: SET:"));
            Serial.println(value);
          }
        }
      }

      markCommandProcessed(id);
      return;
    }
  }

  http.end();
}


// ================================================================
// mark คำสั่งว่าประมวลผลแล้ว
// ================================================================

void markCommandProcessed(long id)
{
  HTTPClient http;

  String url = String(SUPABASE_URL) + "/rest/v1/commands?id=eq." + String(id);

  http.begin(url);
  http.addHeader("apikey", SUPABASE_ANON_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  String body = "{\"processed\":true}";

  int httpCode = http.PATCH(body);

  if (httpCode <= 0 || httpCode >= 300)
  {
    Serial.print(F("mark processed ล้มเหลว code="));
    Serial.println(httpCode);
  }

  http.end();
}
