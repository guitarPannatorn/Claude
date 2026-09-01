/*
  ===================================================================
  Arduino Nano: ตรวจจับสีชิ้นงาน + นับจำนวน + ส่งข้อมูลไป ESP32
  ===================================================================

  การต่อสายกับ ESP32:
   Nano D7 (RX) <- ESP32 TX (GPIO17)
   Nano D8 (TX) -> ESP32 RX (GPIO16)
   Nano A0 <- ESP32 GPIO26 (สาย RFID reset แบบ active-LOW)
   ต้องต่อ GND ร่วมกัน, baud 9600

  *** สำคัญ: มี SoftwareSerial 2 ตัว (huskySerial, espSerial)
      ไลบรารีนี้ "ฟัง" ได้ทีละตัวเท่านั้น จึงต้องเรียก .listen()
      สลับก่อนใช้งานทุกครั้ง (ทำครบแล้วในโค้ดนี้)

  คำสั่งที่รับจาก ESP32 (ลงท้ายด้วย \n):
   - "RESET"         -> เหมือนกดปุ่ม A0 (เคลียร์ NG/LOCK)
   - "RESET_COUNT"   -> เหมือนกดปุ่ม A7 (เคลียร์ Counting + Full Counter)
   - "SET:<n>"       -> ตั้งค่า Setting เป็น n
   - "RESET_TOTAL"   -> ล้างยอดรวมสะสม OK/NG (ใน RAM และ EEPROM)
   - "FACTORY_RESET" -> คืนค่าทั้งหมดกลับค่าเริ่มต้น (ใช้ตอนทดสอบ)

  หมายเหตุเรื่องตัวนับ:
   - countingValue  = ตัวนับของรอบปัจจุบัน (รีเซ็ตได้ด้วย A7 / RESET_COUNT)
   - countOkTotal   = ยอด OK สะสมตลอดกาล
   - countNgTotal   = ยอด NG สะสมตลอดกาล
   - ยอดรวมทั้งสองถูกบันทึกลง EEPROM จึงนับต่อเนื่องข้ามการปิด/เปิดเครื่อง
     (ล้างได้ด้วยคำสั่ง RESET_TOTAL หรือ FACTORY_RESET จากเว็บเท่านั้น)
   - countingValue และ countOkTotal เพิ่มพร้อมกันที่ incrementCounting() จุดเดียว
  ===================================================================
*/

#include <SoftwareSerial.h>
#include "HUSKYLENS.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>


// ================================================================
// PIN CONFIGURATION
// ================================================================

#define PIN_RESET   A0
#define PIN_START   A1

#define LED_MONITOR 9
#define LED_CAM     10
#define LED_OK      11
#define LED_NG      12

#define PIN_SET_UP    A2
#define PIN_SET_DOWN  A3
#define PIN_COUNT_UP  A6
#define PIN_COUNT_RST A7

#define PIN_FULL_OUT        5
#define PIN_FULL_OUT_BLINK  6

#define PIN_ESP_RX  7
#define PIN_ESP_TX  8


// ================================================================
// HUSKYLENS / LCD / ESP LINK
// ================================================================

SoftwareSerial huskySerial(2, 3);
HUSKYLENS huskylens;
bool camConnected = false;

LiquidCrystal_I2C lcd(0x27, 16, 2);

SoftwareSerial espSerial(PIN_ESP_RX, PIN_ESP_TX);


// ================================================================
// COLOR ID
// ================================================================

const int COLOR_ID_1 = 1;
const int COLOR_ID_2 = 2;


// ================================================================
// START / RESET DEBOUNCE
// ================================================================

bool lastStartState = HIGH;
bool lastResetState = HIGH;
unsigned long lastStartDebounceTime = 0;
unsigned long lastResetDebounceTime = 0;
const unsigned long DEBOUNCE_MS = 40;
bool startPressed = false;
bool resetPressed = false;


// ================================================================
// OK / NG CONTROL
// ================================================================

bool ledOkOn = false;
bool okTimerActive = false;
unsigned long okOffTime = 0;
const unsigned long OK_HOLD_MS = 1000;

bool ledNgOn = false;
bool ngDelayActive = false;
unsigned long ngDelayStartTime = 0;
const unsigned long NG_DELAY_MS = 500;


// ================================================================
// CAMERA / MONITOR TIMING
// ================================================================

unsigned long lastCamCheckTime = 0;
const unsigned long CAM_CHECK_INTERVAL = 2000;

unsigned long lastMonitorCheckTime = 0;
const unsigned long MONITOR_CHECK_INTERVAL = 150;

// นับจำนวนครั้งที่อ่านกล้องไม่สำเร็จติดกัน
// ใช้แยกระหว่าง "กล้องหลุด" กับ "ไม่มีชิ้นงานหน้ากล้อง"
byte camFailStreak = 0;
const byte CAM_FAIL_LIMIT = 5;   // 5 เฟรม x 150ms = ~750ms


// ================================================================
// D9 HOLD LOGIC
// ================================================================

bool ledMonitorOn = false;
bool lastLedMonitorOn = false;
unsigned long lastColorFoundTime = 0;
const unsigned long COLOR_LOSS_TIMEOUT_MS = 400;


// ================================================================
// AUTO ASSESSMENT (5 วินาที)
// ================================================================

bool autoTimerActive = false;
unsigned long autoTimerStart = 0;
const unsigned long AUTO_TIMEOUT_MS = 5000;

bool autoFailLock = false;

// เวลาล่าสุดที่ยังเห็น "สีใดสีหนึ่ง" อยู่ในเฟรม
// ใช้ยกเลิกตัวจับเวลาเมื่อชิ้นงานออกจากเฟรมไปแล้ว
unsigned long lastAnyColorTime = 0;
const unsigned long EMPTY_CANCEL_MS = 600;   // ว่างติดกันเกินเท่านี้ = ไม่มีชิ้นงานจริง


// ================================================================
// SETTING / COUNTING
// ================================================================

const int SETTING_DEFAULT = 10;

int settingValue  = SETTING_DEFAULT;
int countingValue = 0;

const int SETTING_STEP = 5;
const int SETTING_MIN  = 0;
const int SETTING_MAX  = 100;

bool fullCounterFlag = false;

bool lastSetUpState   = HIGH;
bool lastSetDownState = HIGH;
unsigned long lastSetUpDebounce   = 0;
unsigned long lastSetDownDebounce = 0;
bool setUpPressed   = false;
bool setDownPressed = false;

const int ANALOG_PRESS_THRESHOLD = 512;

bool lastCountUpState  = false;
bool lastCountRstState = false;

unsigned long lastCountTriggerTime = 0;
const unsigned long COUNT_TRIGGER_LOCKOUT_MS = 500;

unsigned long lastCountRstDebounceTime = 0;
const unsigned long COUNT_RST_DEBOUNCE_MS = 40;

unsigned long lastBlinkTime = 0;
bool blinkVisible = true;
const unsigned long BLINK_INTERVAL_MS = 400;

unsigned long lastD6BlinkTime = 0;
bool d6BlinkState = false;
const unsigned long D6_BLINK_INTERVAL_MS = 300;

bool lcdNeedsRedraw = true;


// ================================================================
// สถิติสะสม OK / NG + อัตราการนับ
// ================================================================

unsigned long countOkTotal = 0;
unsigned long countNgTotal = 0;

#define RATE_HISTORY_SIZE 20
unsigned long countTimestamps[RATE_HISTORY_SIZE];
int countTimestampIndex = 0;
int countTimestampFilled = 0;

String lastEventMsg = "none";


// ================================================================
// EEPROM ADDRESS MAP
// ================================================================

#define EEPROM_MAGIC_ADDR    20   // byte
#define EEPROM_MAGIC_VALUE   0xB6
#define EEPROM_SETTING_ADDR  21   // int (21-22)
#define EEPROM_COUNTING_ADDR 23   // int (23-24)

// ยอดรวมสะสม - ใช้ magic แยกของตัวเอง เพื่อให้บอร์ดที่มีข้อมูลเดิมอยู่แล้ว
// ไม่โดนล้างค่า Setting/Counting ทิ้งตอนอัปเดตเฟิร์มแวร์
#define EEPROM_TOTAL_MAGIC_ADDR  25   // byte
#define EEPROM_TOTAL_MAGIC_VALUE 0xC3
#define EEPROM_OK_TOTAL_ADDR     26   // unsigned long (26-29)
#define EEPROM_NG_TOTAL_ADDR     30   // unsigned long (30-33)

// ค่าที่ถือว่าผิดปกติ (EEPROM เสีย) -> ตีเป็น 0
const unsigned long TOTAL_SANITY_MAX = 999999999UL;

// เขียนยอดรวมลง EEPROM แบบหน่วงเวลา เพื่อยืดอายุ EEPROM
// (เขียนจริงอย่างมากทุก 10 วินาที และเฉพาะตอนที่ค่าเปลี่ยนจริง)
bool totalsDirty = false;
unsigned long lastTotalsSaveTime = 0;
const unsigned long TOTALS_SAVE_INTERVAL_MS = 10000;


// ================================================================
// ESP LINK TIMING
// ================================================================

unsigned long lastEspSendTime = 0;
const unsigned long ESP_SEND_INTERVAL_MS = 500;

String espRxBuffer = "";


// ================================================================
// SETUP
// ================================================================

void setup()
{
  Serial.begin(115200);

  pinMode(PIN_RESET, INPUT_PULLUP);
  pinMode(PIN_START, INPUT_PULLUP);

  pinMode(PIN_SET_UP, INPUT_PULLUP);
  pinMode(PIN_SET_DOWN, INPUT_PULLUP);

  pinMode(LED_MONITOR, OUTPUT);
  pinMode(LED_CAM, OUTPUT);
  pinMode(LED_OK, OUTPUT);
  pinMode(LED_NG, OUTPUT);
  pinMode(PIN_FULL_OUT, OUTPUT);
  pinMode(PIN_FULL_OUT_BLINK, OUTPUT);

  digitalWrite(LED_MONITOR, LOW);
  digitalWrite(LED_CAM, LOW);
  digitalWrite(LED_OK, LOW);
  digitalWrite(LED_NG, LOW);
  digitalWrite(PIN_FULL_OUT, LOW);
  digitalWrite(PIN_FULL_OUT_BLINK, LOW);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  loadCounterFromEEPROM();
  loadTotalsFromEEPROM();

  if (countingValue >= settingValue)
  {
    fullCounterFlag = true;
    digitalWrite(PIN_FULL_OUT, HIGH);
  }

  huskySerial.begin(9600);
  espSerial.begin(9600);

  huskySerial.listen();

  Serial.println();
  Serial.println(F("=============================="));
  Serial.println(F("HUSKYLENS COLOR INSPECTION"));
  Serial.println(F("=============================="));

  Serial.print(F("ยอดรวมสะสมจาก EEPROM -> OK = "));
  Serial.print(countOkTotal);
  Serial.print(F(" | NG = "));
  Serial.println(countNgTotal);

  Serial.println(F("กำลังเชื่อมต่อ HuskyLens..."));

  camConnected = huskylens.begin(huskySerial);

  if (camConnected)
  {
    digitalWrite(LED_CAM, HIGH);
    Serial.println(F("เชื่อมต่อ HuskyLens สำเร็จ"));
  }
  else
  {
    digitalWrite(LED_CAM, LOW);
    Serial.println(F("เชื่อมต่อ HuskyLens ไม่สำเร็จ"));
  }

  drawLCDFull();
}


// ================================================================
// LOOP
// ================================================================

void loop()
{
  handleCameraStatus();
  handleResetButton();
  handleStartButton();
  handleOkAutoOff();
  handleNgDelay();
  handleColorMonitor();

  handleSettingButtons();
  handleCountingButtonManual();
  handleFullCounterOutput();
  handleFullCounterBlinkOutput();
  handleLCDUpdate();

  handleTotalsAutoSave();

  handleEspReceive();
  handleEspSend();

  // เขียนขา D9/D11/D12 ปิดท้ายทุกรอบ หลังทุกฟังก์ชันอัปเดตสถานะเสร็จแล้ว
  // จึงไม่มีทางที่ขาไหนจะค้างสวนกับสถานะจริงข้ามรอบ loop
  applyIndicatorOutputs();
}


// ================================================================
// CAMERA STATUS
// ================================================================

void handleCameraStatus()
{
  unsigned long now = millis();

  if (now - lastCamCheckTime >= CAM_CHECK_INTERVAL)
  {
    lastCamCheckTime = now;

    huskySerial.listen();
    bool ok = huskylens.request();
    camConnected = ok;

    digitalWrite(LED_CAM, camConnected ? HIGH : LOW);
  }
}


// ================================================================
// RESET BUTTON (A0) -> เคลียร์ NG/LOCK เท่านั้น
// ================================================================

void handleResetButton()
{
  bool reading = digitalRead(PIN_RESET);

  if (reading != lastResetState)
  {
    lastResetDebounceTime = millis();
  }

  if ((millis() - lastResetDebounceTime) > DEBOUNCE_MS)
  {
    bool pressedNow = (reading == LOW);

    if (pressedNow && !resetPressed)
    {
      doSystemReset();
    }

    resetPressed = pressedNow;
  }

  lastResetState = reading;
}

void doSystemReset()
{
  Serial.println();
  Serial.println(F("RESET (A0) กำลังทำงาน"));

  ledNgOn = false;
  ngDelayActive = false;

  ledOkOn = false;
  okTimerActive = false;

  autoFailLock = false;
  autoTimerActive = false;

  Serial.println(F("D12 OFF / ปลด Lock ทั้งระบบเก่าและ Auto"));

  // ปลด Full counter ด้วย เพื่อให้รับงานต่อได้โดยไม่ต้องล้าง Counting ทิ้ง
  // (ต่างจาก A7/RESET_COUNT ที่ล้าง Counting = 0 เพื่อเริ่มรอบใหม่)
  // หมายเหตุ: Counting ยังค้างที่เลขเดิม พอนับเพิ่มอีกชิ้นก็จะถึงเป้าและ
  // ขึ้น Full อีกครั้งทันที = กด RESET หนึ่งครั้งได้เพิ่มหนึ่งชิ้น
  if (fullCounterFlag)
  {
    fullCounterFlag = false;

    digitalWrite(PIN_FULL_OUT, LOW);
    digitalWrite(PIN_FULL_OUT_BLINK, LOW);

    Serial.println(F("ปลด Full counter -> รับงานต่อได้อีก (Counting ยังค้างที่เลขเดิม)"));
  }

  lastEventMsg = "reset_ng";

  // ข้างล่างมีหน่วง 700ms ที่ไม่ได้กลับเข้า loop จึงต้องเขียนขาให้ตรงสถานะเดี๋ยวนี้
  applyIndicatorOutputs();

  unsigned long resetTime = millis();
  while (millis() - resetTime < 700)
  {
    // รอ 700ms
  }

  Serial.println(F("RESET (A0) เสร็จสิ้น"));
}

void doCountingReset()
{
  countingValue = 0;
  fullCounterFlag = false;

  digitalWrite(PIN_FULL_OUT, LOW);
  digitalWrite(PIN_FULL_OUT_BLINK, LOW);
  d6BlinkState = false;

  Serial.println(F("RESET COUNT (A7) -> Counting = 0, ปลด Full Counter"));

  lastEventMsg = "count_reset";

  saveCounterToEEPROM();
  lcdNeedsRedraw = true;
}


// ================================================================
// RESET ยอดรวมสะสม / คืนค่าทั้งหมด (สั่งจากเว็บ)
// ================================================================

// ล้างเฉพาะยอดรวมสะสม OK/NG ไม่แตะ Setting และ Counting ของรอบปัจจุบัน
void doTotalReset()
{
  countOkTotal = 0;
  countNgTotal = 0;

  countTimestampIndex  = 0;
  countTimestampFilled = 0;

  lastEventMsg = "total_reset";

  saveTotalsToEEPROM();
  lcdNeedsRedraw = true;

  Serial.println(F("RESET TOTAL -> ล้างยอดรวมสะสม OK/NG แล้ว"));
}

// คืนค่าทุกอย่างกลับค่าเริ่มต้น สำหรับตอนทดสอบเว็บหรือเครื่อง
void doFactoryReset()
{
  // ยอดรวมสะสม
  countOkTotal = 0;
  countNgTotal = 0;
  countTimestampIndex  = 0;
  countTimestampFilled = 0;

  // ตัวนับรอบปัจจุบัน + Setting
  countingValue = 0;
  settingValue  = SETTING_DEFAULT;

  fullCounterFlag = false;
  digitalWrite(PIN_FULL_OUT, LOW);
  digitalWrite(PIN_FULL_OUT_BLINK, LOW);
  d6BlinkState = false;

  // สถานะ NG / LOCK ทั้งหมด
  ledNgOn = false;
  ngDelayActive = false;
  autoFailLock = false;
  autoTimerActive = false;

  ledOkOn = false;
  okTimerActive = false;

  ledMonitorOn = false;
  lastLedMonitorOn = false;

  applyIndicatorOutputs();

  // ปลดล็อกการนับซ้ำ ไม่ให้ค้างจากรอบก่อนหน้า
  lastCountTriggerTime = millis() - COUNT_TRIGGER_LOCKOUT_MS;

  lastEventMsg = "factory_reset";

  saveCounterToEEPROM();
  saveTotalsToEEPROM();
  lcdNeedsRedraw = true;

  Serial.println(F("FACTORY RESET -> คืนค่าทั้งหมดเรียบร้อย"));
}


// ================================================================
// START BUTTON (A1)
// ================================================================

void handleStartButton()
{
  bool reading = digitalRead(PIN_START);

  if (reading != lastStartState)
  {
    lastStartDebounceTime = millis();
  }

  if ((millis() - lastStartDebounceTime) > DEBOUNCE_MS)
  {
    bool pressedNow = (reading == LOW);

    if (pressedNow && !startPressed)
    {
      Serial.println();
      Serial.println(F("START"));

      if (fullCounterFlag)
      {
        Serial.println(F("Full counter - รับงานต่อไม่ได้ ต้องกด RESET ก่อน"));
      }
      else if (ledNgOn)
      {
        Serial.println(F("ระบบ LOCK - กรุณากด RESET ก่อน"));
      }
      else if (ngDelayActive)
      {
        Serial.println(F("กำลังรอ NG Delay 500ms"));
      }
      else
      {
        ledOkOn = false;
        okTimerActive = false;

        runColorCheck();
      }
    }

    if (!pressedNow && startPressed)
    {
      if (ledOkOn)
      {
        Serial.println(F("START ปล่อย - เริ่มจับเวลา OK 1 วินาที"));

        okTimerActive = true;
        okOffTime = millis() + OK_HOLD_MS;
      }
    }

    startPressed = pressedNow;
  }

  lastStartState = reading;
}


// ================================================================
// COLOR CHECK
// ================================================================

void runColorCheck()
{
  bool found1 = false;
  bool found2 = false;

  Serial.println(F("กำลังตรวจสอบสี..."));

  huskySerial.listen();

  if (!huskylens.request())
  {
    Serial.println(F("HUSKYLENS ERROR"));
    setResultNG(false, false, false);
    return;
  }

  while (huskylens.available())
  {
    HUSKYLENSResult result = huskylens.read();

    Serial.print(F("ID = "));
    Serial.print(result.ID);
    Serial.print(F(" X = "));
    Serial.print(result.xCenter);
    Serial.print(F(" Y = "));
    Serial.println(result.yCenter);

    if (result.ID == COLOR_ID_1) found1 = true;
    if (result.ID == COLOR_ID_2) found2 = true;
  }

  if (found1 && found2)
  {
    setResultOK();
    Serial.println(F("RESULT = OK (พบ ID1 + ID2)"));
  }
  else
  {
    Serial.print(F("RESULT = NG | ID1 = "));
    Serial.print(found1);
    Serial.print(F(" | ID2 = "));
    Serial.println(found2);

    setResultNG(found1, found2, true);
  }
}


// ================================================================
// SET OK / SET NG
// ================================================================

void setResultOK()
{
  ngDelayActive = false;

  ledOkOn = true;

  ledNgOn = false;

  triggerCountFromD11();
}

// ประกอบชื่อ event ให้บอกด้วยว่าสี ID ไหนหายไป
// เว็บอ่านเลขที่ติดอยู่ในชื่อ event เพื่อไปแสดงว่า "สี ID x หาย"
// ต้องคั่นเลขแต่ละตัวไว้ ("miss1_2" ไม่ใช่ "miss12")
// ไม่งั้นเว็บจะอ่านได้เป็นเลข 12 แล้วมองว่าไม่ใช่ ID ที่รู้จัก
String ngEventName(const char* prefix, bool found1, bool found2)
{
  String ev = prefix;

  if (!found1 && !found2) ev += "_miss1_2";
  else if (!found1)       ev += "_miss1";
  else if (!found2)       ev += "_miss2";

  return ev;
}

// idsKnown = false ใช้กับกรณีที่อ่านกล้องไม่สำเร็จ ซึ่งไม่รู้ว่าสีไหนหาย
// จึงส่งแค่ "ng" เฉยๆ ไม่เดาว่าหายทั้งสองสี
void setResultNG(bool found1, bool found2, bool idsKnown)
{
  ledOkOn = false;
  okTimerActive = false;

  ngDelayActive = true;
  ngDelayStartTime = millis();

  countNgTotal++;
  totalsDirty = true;
  lastEventMsg = idsKnown ? ngEventName("ng", found1, found2) : String("ng");

  Serial.print(F("NG detected ("));
  Serial.print(lastEventMsg);
  Serial.println(F(") - รอ 500ms ก่อนเปิด D12"));
}


// ================================================================
// NG DELAY
// ================================================================

void handleNgDelay()
{
  if (!ngDelayActive) return;

  if (millis() - ngDelayStartTime >= NG_DELAY_MS)
  {
    ngDelayActive = false;

    ledNgOn = true;

    ledOkOn = false;
    okTimerActive = false;

    Serial.println(F("ครบ 500ms -> D12 = ON, ระบบ LOCK"));
  }
}


// ================================================================
// OK AUTO OFF
// ================================================================

void handleOkAutoOff()
{
  if (!okTimerActive) return;

  if (millis() >= okOffTime)
  {
    ledOkOn = false;
    okTimerActive = false;

    Serial.println(F("D11 OFF"));
  }
}


// ================================================================
// D9 / D11 / D12 OUTPUT รวมสถานะ
// ================================================================

// ครบเป้าแล้วต้องไม่โชว์ผลตรวจที่ D9/D11/D12 เลย ทั้งขาออกจริงและค่าที่ส่งขึ้นเว็บ
// เหลือแค่ D5/D6 ที่แสดงสถานะ Full อย่างเดียว
// (สถานะ OK/NG/LOCK ข้างในยังจำไว้เหมือนเดิม พอกด RESET ปลด Full ถึงกลับมาแสดงตามจริง)
// ใช้ตัวเดียวกันทั้ง 2 ที่ เพื่อไม่ให้ไฟที่หน้าเครื่องกับที่หน้าเว็บขัดกัน
bool outputD9Active()
{
  return !fullCounterFlag && ledMonitorOn;
}

bool outputD11Active()
{
  return !fullCounterFlag && ledOkOn;
}

bool outputD12Active()
{
  return !fullCounterFlag && (ledNgOn || autoFailLock);
}

// ประตูเดียวที่เขียนขา D9/D11/D12 ได้
// ห้ามฟังก์ชันอื่น digitalWrite() ขาสามขานี้เองเด็ดขาด ให้แก้แค่ตัวแปรสถานะ
// (ledMonitorOn / ledOkOn / ledNgOn / autoFailLock) แล้วปล่อยให้ตรงนี้เขียนขา
// ของเดิมกระจายอยู่หลายที่ พอครบเป้าแล้วบางเส้นทางยังจุดไฟทับได้ในรอบเดียวกัน
// ทำให้ D9/D12 ยังมีสัญญาณออกทั้งที่ D6 กำลังกะพริบอยู่
void applyIndicatorOutputs()
{
  digitalWrite(LED_MONITOR, outputD9Active()  ? HIGH : LOW);
  digitalWrite(LED_OK,      outputD11Active() ? HIGH : LOW);
  digitalWrite(LED_NG,      outputD12Active() ? HIGH : LOW);
}


// ================================================================
// สแกนสี + D9 hold + Auto Assessment
// ================================================================

void handleColorMonitor()
{
  // ครบเป้าแล้ว = หยุดรับงานทุกทาง ต้องกด RESET ก่อนถึงจะตรวจต่อได้
  // บล็อกตรงนี้จุดเดียวครอบคลุมทั้งการนับอัตโนมัติจาก D9 falling edge
  // และ Auto Assessment ที่จะตัดสินเป็น NG เมื่อเห็นสีเดียวครบ 5 วินาที
  if (fullCounterFlag)
  {
    // ดับสถานะ D9 ทุกรอบ ไม่ใช่ดับครั้งเดียวตอนเข้า Full
    // (ขาจริงถูกเขียนที่ applyIndicatorOutputs() ปลายทาง)
    ledMonitorOn = false;

    // ต้องล้างด้วย ไม่งั้นตอนกด RESET ปลด Full ปุ๊บ checkD9FallingEdge()
    // จะเห็นเป็น falling edge ค้างจากก่อนหน้า แล้วนับเพิ่มให้เองทันที 1 ชิ้น
    lastLedMonitorOn = false;
    autoTimerActive = false;

    return;
  }

  unsigned long now = millis();

  if (now - lastMonitorCheckTime < MONITOR_CHECK_INTERVAL)
  {
    return;
  }

  lastMonitorCheckTime = now;

  huskySerial.listen();

  // อ่านกล้องไม่สำเร็จ ห้ามตีความว่า "ไม่พบสี" เด็ดขาด
  // เพราะจะทำให้ Auto Assessment ยกเลิกตัวจับเวลาทั้งที่ชิ้นงานยังอยู่
  // (ของเสียจะหลุดไปได้) -> ข้ามเฟรมนี้ไปเฉยๆ
  if (!huskylens.request())
  {
    if (camFailStreak < 255) camFailStreak++;

    if (camFailStreak == CAM_FAIL_LIMIT)
    {
      // กล้องหลุดจริง -> ดับ D9 และหยุดจับเวลา แต่ไม่ตัดสินผลเป็น NG
      ledMonitorOn = false;
      lastLedMonitorOn = false;   // กันไม่ให้เกิด falling edge หลอกๆ แล้วนับเพิ่ม
      autoTimerActive = false;

      Serial.println(F("MONITOR: อ่านกล้องไม่สำเร็จติดกัน -> พักการประเมินผล"));
    }

    return;
  }

  camFailStreak = 0;

  bool found1 = false;
  bool found2 = false;

  while (huskylens.available())
  {
    HUSKYLENSResult result = huskylens.read();

    if (result.ID == COLOR_ID_1) found1 = true;
    if (result.ID == COLOR_ID_2) found2 = true;
  }

  if (!ledNgOn && !ngDelayActive && !autoFailLock)
  {
    updateD9Hold(found1, found2, now);
  }
  else
  {
    ledMonitorOn = false;
  }

  checkD9FallingEdge();

  handleAutoAssessment(found1, found2, now);
}


void updateD9Hold(bool found1, bool found2, unsigned long now)
{
  if (found1 && found2)
  {
    ledMonitorOn = true;
    lastColorFoundTime = now;
  }
  else
  {
    if (ledMonitorOn)
    {
      if (now - lastColorFoundTime > COLOR_LOSS_TIMEOUT_MS)
      {
        ledMonitorOn = false;
        Serial.println(F("D9 OFF (สีหายเกิน 400ms)"));
      }
    }
  }
}


void checkD9FallingEdge()
{
  if (lastLedMonitorOn && !ledMonitorOn)
  {
    Serial.println(F("D9 falling edge -> trigger count"));
    triggerCountFromD11();
  }

  lastLedMonitorOn = ledMonitorOn;
}


void handleAutoAssessment(bool found1, bool found2, unsigned long now)
{
  if (autoFailLock)
  {
    return;
  }

  // จำเวลาล่าสุดที่ยังเห็นสีอยู่ในเฟรม
  if (found1 || found2)
  {
    lastAnyColorTime = now;
  }

  // พบครบ 2 สี = ชิ้นงานถูกต้อง -> ยกเลิกตัวจับเวลา
  if (found1 && found2)
  {
    if (autoTimerActive)
    {
      Serial.println(F("AUTO: ผ่าน ภายใน 5 วินาที"));
    }

    autoTimerActive = false;
    return;
  }

  bool exactlyOne = (found1 != found2);

  // ---------------------------------------------------------------
  // จุดที่แก้บั๊ก: ไม่พบสีใดเลย = ไม่มีชิ้นงานอยู่หน้ากล้อง
  // ของเดิมไม่มีเงื่อนไขนี้ ตัวจับเวลาจึงเดินต่อทั้งที่หยิบชิ้นงาน
  // ออกไปแล้ว พอครบ 5 วินาทีก็ล็อกเป็น NG ทั้งที่สถานีว่างเปล่า
  // ---------------------------------------------------------------
  if (!exactlyOne)
  {
    if (autoTimerActive && (now - lastAnyColorTime >= EMPTY_CANCEL_MS))
    {
      autoTimerActive = false;
      Serial.println(F("AUTO: ชิ้นงานออกจากเฟรม -> ยกเลิกจับเวลา"));
    }

    return;
  }

  // เหลือกรณีเดียว: พบสีเดียว
  if (!autoTimerActive)
  {
    autoTimerActive = true;
    autoTimerStart = now;

    Serial.println(F("AUTO: เริ่มจับเวลา 5 วินาที (พบ 1 สี)"));
    return;
  }

  if (now - autoTimerStart >= AUTO_TIMEOUT_MS)
  {
    autoFailLock = true;
    autoTimerActive = false;

    ledMonitorOn = false;

    countNgTotal++;
    totalsDirty = true;
    lastEventMsg = ngEventName("auto_fail", found1, found2);

    Serial.println(F("AUTO: FAIL เกิน 5 วินาที - ระบบ LOCK ต้องกด A0"));
  }
}


// ================================================================
// SETTING BUTTONS: A2 (+5) / A3 (-5)
// ================================================================

void handleSettingButtons()
{
  bool readingUp = digitalRead(PIN_SET_UP);

  if (readingUp != lastSetUpState)
  {
    lastSetUpDebounce = millis();
  }

  if ((millis() - lastSetUpDebounce) > DEBOUNCE_MS)
  {
    bool pressedNow = (readingUp == LOW);

    if (pressedNow && !setUpPressed)
    {
      applySettingChange(settingValue + SETTING_STEP);
    }

    setUpPressed = pressedNow;
  }

  lastSetUpState = readingUp;

  bool readingDown = digitalRead(PIN_SET_DOWN);

  if (readingDown != lastSetDownState)
  {
    lastSetDownDebounce = millis();
  }

  if ((millis() - lastSetDownDebounce) > DEBOUNCE_MS)
  {
    bool pressedNow = (readingDown == LOW);

    if (pressedNow && !setDownPressed)
    {
      applySettingChange(settingValue - SETTING_STEP);
    }

    setDownPressed = pressedNow;
  }

  lastSetDownState = readingDown;
}

void applySettingChange(int requestedValue)
{
  int nextValue = requestedValue;

  if (nextValue < countingValue)
  {
    nextValue = ((countingValue + SETTING_STEP - 1) / SETTING_STEP) * SETTING_STEP;
    Serial.println(F("ลด Setting ไม่ได้ ต่ำกว่าค่า Counting ปัจจุบัน"));
  }

  if (nextValue > SETTING_MAX) nextValue = SETTING_MAX;
  if (nextValue < SETTING_MIN) nextValue = SETTING_MIN;

  settingValue = nextValue;

  Serial.print(F("Setting = "));
  Serial.println(settingValue);

  saveCounterToEEPROM();
  lcdNeedsRedraw = true;
}


// ================================================================
// A6 (นับมือ) / A7 (reset counting)
// ================================================================

void handleCountingButtonManual()
{
  int rawUp = analogRead(PIN_COUNT_UP);
  bool pressedUpNow = (rawUp < ANALOG_PRESS_THRESHOLD);

  if (pressedUpNow && !lastCountUpState)
  {
    triggerCountFromD11();
  }

  lastCountUpState = pressedUpNow;

  int rawRst = analogRead(PIN_COUNT_RST);
  bool pressedRstNow = (rawRst < ANALOG_PRESS_THRESHOLD);

  if (pressedRstNow != lastCountRstState)
  {
    lastCountRstDebounceTime = millis();
  }

  if ((millis() - lastCountRstDebounceTime) > COUNT_RST_DEBOUNCE_MS)
  {
    static bool rstEdgeLatched = false;

    if (pressedRstNow && !rstEdgeLatched)
    {
      doCountingReset();
      rstEdgeLatched = true;
    }
    else if (!pressedRstNow)
    {
      rstEdgeLatched = false;
    }
  }

  lastCountRstState = pressedRstNow;
}


// ================================================================
// ทริกนับ - จุดเดียวที่เพิ่มทั้ง countingValue และ countOkTotal
// ================================================================

void triggerCountFromD11()
{
  if (millis() - lastCountTriggerTime < COUNT_TRIGGER_LOCKOUT_MS) return;

  lastCountTriggerTime = millis();
  incrementCounting();
}

void incrementCounting()
{
  // กันชั้นสุดท้าย - ครอบคลุมปุ่มนับมือ A6 ที่ไม่ได้ผ่าน handleColorMonitor()
  if (fullCounterFlag)
  {
    Serial.println(F("Full counter - ไม่นับเพิ่ม ต้องกด RESET ก่อน"));
    return;
  }

  countingValue++;

  countOkTotal++;
  totalsDirty = true;
  lastEventMsg = "ok";

  Serial.print(F("Counting = "));
  Serial.print(countingValue);
  Serial.print(F(" | OK รวม = "));
  Serial.println(countOkTotal);

  countTimestamps[countTimestampIndex] = millis();
  countTimestampIndex = (countTimestampIndex + 1) % RATE_HISTORY_SIZE;
  if (countTimestampFilled < RATE_HISTORY_SIZE) countTimestampFilled++;

  if (countingValue >= settingValue)
  {
    fullCounterFlag = true;
    lastEventMsg = "full_counter";
    Serial.println(F("FULL COUNTER"));

    // ครบเป้าปุ๊บ ดับ D9/D11/D12 ทันทีในจังหวะเดียวกับที่ D5/D6 ติด
    // ไม่ต้องรอจนจบรอบ loop เผื่อเส้นทางที่เรียกมามีงานยาวคั่นอยู่
    applyIndicatorOutputs();

    // ครบเป้าแล้วเป็นจุดสำคัญ บันทึกยอดรวมทันทีไม่ต้องรอรอบหน่วงเวลา
    saveTotalsToEEPROM();
  }

  saveCounterToEEPROM();
  lcdNeedsRedraw = true;
}

float calcCountRatePerMinute()
{
  if (countTimestampFilled < 2) return 0.0;

  unsigned long now = millis();

  int oldestIndex;
  if (countTimestampFilled < RATE_HISTORY_SIZE)
  {
    oldestIndex = 0;
  }
  else
  {
    oldestIndex = countTimestampIndex;
  }

  unsigned long oldestTime = countTimestamps[oldestIndex];

  if (now <= oldestTime) return 0.0;

  float elapsedMinutes = (now - oldestTime) / 60000.0;

  if (elapsedMinutes <= 0.0) return 0.0;

  return (countTimestampFilled - 1) / elapsedMinutes;
}


// ================================================================
// D5 / D6 OUTPUT
// ================================================================

void handleFullCounterOutput()
{
  digitalWrite(PIN_FULL_OUT, fullCounterFlag ? HIGH : LOW);
}

void handleFullCounterBlinkOutput()
{
  if (!fullCounterFlag)
  {
    digitalWrite(PIN_FULL_OUT_BLINK, LOW);
    d6BlinkState = false;
    return;
  }

  unsigned long now = millis();

  if (now - lastD6BlinkTime >= D6_BLINK_INTERVAL_MS)
  {
    lastD6BlinkTime = now;
    d6BlinkState = !d6BlinkState;
    digitalWrite(PIN_FULL_OUT_BLINK, d6BlinkState ? HIGH : LOW);
  }
}


// ================================================================
// LCD UPDATE
// ================================================================

void handleLCDUpdate()
{
  if (fullCounterFlag)
  {
    unsigned long now = millis();

    if (now - lastBlinkTime >= BLINK_INTERVAL_MS)
    {
      lastBlinkTime = now;
      blinkVisible = !blinkVisible;

      lcd.setCursor(0, 1);

      if (blinkVisible)
      {
        lcd.print(F("  Full counter  "));
      }
      else
      {
        lcd.print(F("                "));
      }
    }

    if (lcdNeedsRedraw)
    {
      drawSettingLine();
      lcdNeedsRedraw = false;
    }
  }
  else
  {
    if (lcdNeedsRedraw)
    {
      drawLCDFull();
      lcdNeedsRedraw = false;
    }
  }
}

void drawSettingLine()
{
  char buf[17];
  snprintf(buf, sizeof(buf), "Setting  %3d pcs", settingValue);
  lcd.setCursor(0, 0);
  lcd.print(buf);
}

void drawCountingLine()
{
  char buf[17];
  snprintf(buf, sizeof(buf), "Counting %3d pcs", countingValue);
  lcd.setCursor(0, 1);
  lcd.print(buf);
}

void drawLCDFull()
{
  drawSettingLine();
  drawCountingLine();
}


// ================================================================
// EEPROM SAVE / LOAD - Setting + Counting ของรอบปัจจุบัน
// ================================================================

void saveCounterToEEPROM()
{
  EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
  EEPROM.put(EEPROM_SETTING_ADDR, settingValue);
  EEPROM.put(EEPROM_COUNTING_ADDR, countingValue);
}

void loadCounterFromEEPROM()
{
  byte magic = EEPROM.read(EEPROM_MAGIC_ADDR);

  if (magic == EEPROM_MAGIC_VALUE)
  {
    EEPROM.get(EEPROM_SETTING_ADDR, settingValue);
    EEPROM.get(EEPROM_COUNTING_ADDR, countingValue);

    if (settingValue < SETTING_MIN || settingValue > SETTING_MAX) settingValue = SETTING_DEFAULT;
    if (countingValue < 0 || countingValue > 9999) countingValue = 0;

    Serial.println(F("โหลดค่า Setting/Counting จาก EEPROM แล้ว"));
  }
  else
  {
    settingValue = SETTING_DEFAULT;
    countingValue = 0;
    saveCounterToEEPROM();
    Serial.println(F("EEPROM ว่าง -> ตั้งค่าเริ่มต้น Setting=10, Counting=0"));
  }
}


// ================================================================
// EEPROM SAVE / LOAD - ยอดรวมสะสม OK / NG
// ================================================================

void saveTotalsToEEPROM()
{
  EEPROM.update(EEPROM_TOTAL_MAGIC_ADDR, EEPROM_TOTAL_MAGIC_VALUE);
  EEPROM.put(EEPROM_OK_TOTAL_ADDR, countOkTotal);
  EEPROM.put(EEPROM_NG_TOTAL_ADDR, countNgTotal);

  totalsDirty = false;
  lastTotalsSaveTime = millis();
}

void loadTotalsFromEEPROM()
{
  byte magic = EEPROM.read(EEPROM_TOTAL_MAGIC_ADDR);

  if (magic == EEPROM_TOTAL_MAGIC_VALUE)
  {
    EEPROM.get(EEPROM_OK_TOTAL_ADDR, countOkTotal);
    EEPROM.get(EEPROM_NG_TOTAL_ADDR, countNgTotal);

    if (countOkTotal > TOTAL_SANITY_MAX) countOkTotal = 0;
    if (countNgTotal > TOTAL_SANITY_MAX) countNgTotal = 0;

    Serial.println(F("โหลดยอดรวมสะสม OK/NG จาก EEPROM แล้ว"));
  }
  else
  {
    countOkTotal = 0;
    countNgTotal = 0;
    saveTotalsToEEPROM();
    Serial.println(F("ยังไม่เคยเก็บยอดรวม -> เริ่มนับใหม่จาก 0"));
  }
}

// เขียนยอดรวมลง EEPROM แบบหน่วงเวลา
// เขียนจริงอย่างมากทุก TOTALS_SAVE_INTERVAL_MS และเฉพาะตอนที่ค่าเปลี่ยนจริง
void handleTotalsAutoSave()
{
  if (!totalsDirty) return;

  if (millis() - lastTotalsSaveTime < TOTALS_SAVE_INTERVAL_MS) return;

  saveTotalsToEEPROM();
}


// ================================================================
// ESP32 LINK: ส่งข้อมูล (JSON) ทุก 500ms
// ================================================================

void handleEspSend()
{
  unsigned long now = millis();

  if (now - lastEspSendTime < ESP_SEND_INTERVAL_MS)
  {
    return;
  }

  lastEspSendTime = now;

  float rate = calcCountRatePerMinute();

  espSerial.print(F("{"));

  espSerial.print(F("\"setting\":"));
  espSerial.print(settingValue);

  espSerial.print(F(",\"counting\":"));
  espSerial.print(countingValue);

  espSerial.print(F(",\"d9\":"));
  espSerial.print(outputD9Active() ? 1 : 0);

  espSerial.print(F(",\"d11\":"));
  espSerial.print(outputD11Active() ? 1 : 0);

  espSerial.print(F(",\"d12\":"));
  espSerial.print(outputD12Active() ? 1 : 0);

  espSerial.print(F(",\"lockOld\":"));
  espSerial.print(ledNgOn ? 1 : 0);

  espSerial.print(F(",\"lockAuto\":"));
  espSerial.print(autoFailLock ? 1 : 0);

  espSerial.print(F(",\"full\":"));
  espSerial.print(fullCounterFlag ? 1 : 0);

  espSerial.print(F(",\"ok\":"));
  espSerial.print(countOkTotal);

  espSerial.print(F(",\"ng\":"));
  espSerial.print(countNgTotal);

  espSerial.print(F(",\"rate\":"));
  espSerial.print(rate, 1);

  espSerial.print(F(",\"event\":\""));
  espSerial.print(lastEventMsg);
  espSerial.print(F("\""));

  espSerial.println(F("}"));

  lastEventMsg = "none";
}


// ================================================================
// ESP32 LINK: รับคำสั่งควบคุมจากเว็บ
// ================================================================

void handleEspReceive()
{
  espSerial.listen();

  while (espSerial.available())
  {
    char c = espSerial.read();

    if (c == '\n')
    {
      espRxBuffer.trim();

      if (espRxBuffer.length() > 0)
      {
        processEspCommand(espRxBuffer);
      }

      espRxBuffer = "";
    }
    else if (c != '\r')
    {
      espRxBuffer += c;

      if (espRxBuffer.length() > 40)
      {
        espRxBuffer = "";
      }
    }
  }
}

void processEspCommand(String cmd)
{
  Serial.print(F("ESP CMD: "));
  Serial.println(cmd);

  if (cmd == "RESET")
  {
    doSystemReset();
  }
  else if (cmd == "RESET_COUNT")
  {
    doCountingReset();
  }
  else if (cmd == "RESET_TOTAL")
  {
    doTotalReset();
  }
  else if (cmd == "FACTORY_RESET")
  {
    doFactoryReset();
  }
  else if (cmd.startsWith("SET:"))
  {
    int value = cmd.substring(4).toInt();
    applySettingChange(value);
  }
}
