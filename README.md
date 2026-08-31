# Color Inspection Station

ระบบตรวจสอบสีชิ้นงานด้วย HuskyLens + นับจำนวน + ส่งข้อมูลขึ้น Supabase

```
HuskyLens ──I2C/Serial──> Arduino Nano ──Serial 9600──> ESP32 ──HTTPS──> Supabase ──> เว็บ
                              │                            │
                         LCD + LED + ปุ่ม              RFID-RC522
```

| โฟลเดอร์ | เนื้อหา |
|---|---|
| `firmware/Nano_Pro/` | เฟิร์มแวร์ Arduino Nano — ตรวจสี, นับ, LCD, EEPROM |
| `firmware/esp32_supabase_rfid/` | เฟิร์มแวร์ ESP32 — WiFi, Supabase, RFID |
| `sql/` | SQL function สำหรับปุ่มรีเซ็ตบนเว็บ |
| `web/` | หน้าเว็บควบคุมเครื่อง — Netlify deploy โฟลเดอร์นี้ขึ้นเว็บ |
| `test/` | เทสต์ตรรกะ Auto Assessment รันบน PC ได้โดยไม่ต้องมีบอร์ด |

---

## สิ่งที่แก้ในรอบนี้

### 1. บั๊ก: ขึ้น NG ทั้งที่สถานีว่าง ไม่มีชิ้นงาน  ← ตัวหลัก

`handleAutoAssessment()` ของเดิมยกเลิกตัวจับเวลา 5 วินาทีได้ทางเดียวคือ "เห็นครบ 2 สี"
ส่วนกรณี **ไม่เห็นสีเลย** ไม่มีโค้ดรองรับ ตัวจับเวลาจึงเดินต่อ

```
ชิ้นงานเข้าเฟรม เห็น ID1 ก่อน ID2 เสี้ยววินาที  ->  อาร์มตัวจับเวลา 5 วิ
หยิบชิ้นงานออก / ชิ้นงานเลื่อนผ่านไป            ->  ตัวจับเวลา "ยังเดินอยู่"
ครบ 5 วินาที                                    ->  autoFailLock = NG + ระบบ LOCK
```

**แก้แล้ว** — เพิ่มเงื่อนไข: ถ้าไม่เห็นสีใดเลยติดกันเกิน `EMPTY_CANCEL_MS` (600ms)
ถือว่าชิ้นงานออกจากเฟรมไปแล้ว ให้ยกเลิกตัวจับเวลา
ช่วง 600ms เป็น grace period กันภาพกระพริบหายแวบเดียวแล้วตัวจับเวลารีเซ็ตทิ้ง

พร้อมกันนั้นเพิ่มการแยกแยะ **"กล้องอ่านไม่ได้" ออกจาก "ไม่มีชิ้นงาน"** ใน `handleColorMonitor()`
ของเดิมถ้า `huskylens.request()` คืน false จะได้ `found1=found2=false` ซึ่งหน้าตาเหมือนเฟรมว่างเป๊ะ
ถ้าไม่แยก ของเสียที่ยังวางอยู่จะหลุดไปได้เมื่อกล้องสะดุด ตอนนี้จะข้ามเฟรมนั้นไปเฉยๆ
และถ้าอ่านไม่สำเร็จติดกัน 5 เฟรม (~750ms) จะดับ D9 + พักการประเมิน แต่ไม่ตัดสินเป็น NG

### 2. ยอดรวมสะสม OK/NG นับต่อเนื่องข้ามการปิด/เปิดเครื่อง

เดิม `countOkTotal` / `countNgTotal` อยู่ใน RAM ล้วน หายทุกครั้งที่ไฟดับ
แล้วค่า 0 ถูกอัปโหลดทับขึ้น Supabase ทำให้กราฟบนเว็บร่วงกลับศูนย์ทุกเช้า

ตอนนี้บันทึกลง EEPROM แล้ว ตอนบูตจะโหลดกลับมานับต่อจากของเดิม

**แผนที่ EEPROM**

| ตำแหน่ง | ขนาด | เก็บอะไร |
|---|---|---|
| 20 | 1 | magic 0xB6 (ของเดิม) |
| 21–22 | 2 | `settingValue` |
| 23–24 | 2 | `countingValue` |
| **25** | **1** | **magic 0xC3 (ใหม่)** |
| **26–29** | **4** | **`countOkTotal`** |
| **30–33** | **4** | **`countNgTotal`** |

ยอดรวมใช้ magic แยกของตัวเอง บอร์ดที่มีข้อมูลเดิมอยู่แล้วจึงอัปเดตเฟิร์มแวร์ได้
โดยค่า Setting/Counting เดิมไม่หาย (ยอดรวมจะเริ่มนับใหม่จาก 0 ครั้งเดียว)

**เรื่องอายุ EEPROM** — EEPROM ของ ATmega328P เขียนได้ประมาณ 100,000 ครั้งต่อไบต์
ยอดรวมจึงเขียนแบบหน่วงเวลา (`TOTALS_SAVE_INTERVAL_MS = 10 วินาที`) แทนที่จะเขียนทุกชิ้น
ยกเว้นตอนครบ Full Counter และตอนรีเซ็ต ที่บันทึกทันที
ผลข้างเคียง: ถ้าไฟดับกะทันหัน อาจเสียยอดล่าสุดไม่เกิน 10 วินาที

### 3. ปุ่มบนเว็บ: Reset ยอดรวม / คืนค่าทั้งหมด

คำสั่งใหม่ที่ Nano รับได้:

| คำสั่ง | ผลลัพธ์ |
|---|---|
| `RESET_TOTAL` | ล้าง `countOkTotal` / `countNgTotal` + rate history ทั้งใน RAM และ EEPROM<br>**ไม่แตะ** Setting และ Counting ของรอบปัจจุบัน |
| `FACTORY_RESET` | ยอดรวม = 0, Counting = 0, Setting = 10, ปลด NG/LOCK/Full ทั้งหมด |

(ของเดิมมี `RESET`, `RESET_COUNT`, `SET:<n>` อยู่แล้ว)

---

## การติดตั้ง

### 1) เฟิร์มแวร์ Nano
เปิด `firmware/Nano_Pro/Nano_Pro.ino` แล้วอัปโหลดตามปกติ
ต้องมีไลบรารี: `HUSKYLENS`, `LiquidCrystal_I2C`

### 2) เฟิร์มแวร์ ESP32
```bash
cd firmware/esp32_supabase_rfid
cp arduino_secrets.example.h arduino_secrets.h
# แก้ SECRET_WIFI_SSID / SECRET_WIFI_PASSWORD ให้เป็นค่าจริง
```
ต้องมีไลบรารี: `ArduinoJson` (6.x), `MFRC522`

> รหัส WiFi ถูกย้ายออกจากไฟล์ `.ino` มาไว้ใน `arduino_secrets.h` ซึ่งอยู่ใน `.gitignore`
> เพราะของเดิมเขียนรหัสไว้ในโค้ดตรงๆ ถ้า repo เป็น public ใครก็เข้า WiFi ได้
> **แนะนำให้เปลี่ยนรหัส WiFi ใหม่** เนื่องจากรหัสเดิมเคยอยู่ในไฟล์ที่แชร์ออกไปแล้ว
>
> ส่วน Supabase anon key ไม่ใช่ความลับ มันถูกออกแบบมาให้ฝังในเบราว์เซอร์
> ความปลอดภัยจริงมาจาก RLS policy ซึ่งตั้งไว้ถูกต้องแล้ว (anon สั่งงานเครื่องไม่ได้)

### 3) SQL บน Supabase
ติดตั้งไปแล้วบนโปรเจกต์ `ufmwcstlzygrnmzkpgbs`
ถ้าต้องติดตั้งใหม่หรือย้ายโปรเจกต์ ให้รัน `sql/01_reset_functions.sql` ใน SQL Editor

สร้าง 2 ฟังก์ชัน เรียกได้เฉพาะผู้ใช้ที่ล็อกอินแล้ว (`authenticated`) — `anon` เรียกไม่ได้

| ฟังก์ชัน | ทำอะไร |
|---|---|
| `reset_totals()` | เข้าคิวคำสั่ง `RESET_TOTAL` + ล้าง `ok_total`/`ng_total` ในตาราง `status` |
| `factory_reset(p_clear_log)` | เข้าคิว `FACTORY_RESET` + ยกเลิกคำสั่งเก่าที่ค้างคิว + ล้าง `event_log` + รีเซ็ตแถว `status` |

> ต้องใช้ RPC เพราะ RLS ปัจจุบันไม่มีใครมีสิทธิ์ `DELETE` บน `event_log` เลย
> การเปิดสิทธิ์ DELETE ทั้งตารางให้เว็บเสี่ยงเกินไป จึงห่อไว้ใน function
> แบบ `SECURITY DEFINER` ที่ทำได้เฉพาะงานที่กำหนด

### 4) หน้าเว็บ

มี 2 ทางเลือก

**ทาง A — ใช้หน้าใหม่ทั้งหน้า** `web/index.html` คือหน้าที่ Netlify เสิร์ฟอยู่ตอนนี้
แก้ไฟล์นี้แล้ว push เว็บจะอัปเดตเอง

**ทาง B — แปะปุ่มเข้าเว็บเดิม** ก๊อป `web/snippet-reset-buttons.html` ทั้งไฟล์
ไปวางใน `index.html` ของเว็บเดิม ตรงก่อนปิด `</body>`
บล็อกนี้ห่อไว้ใน IIFE ไม่ประกาศตัวแปร global และ id/class ขึ้นต้นด้วย `ci-` ทั้งหมด
จึงไม่ชนกับโค้ดเดิม ถ้าหน้าเว็บโหลด supabase-js ไว้แล้วจะใช้ตัวเดิม ไม่โหลดซ้ำ

ทั้งสองทางต้องล็อกอินด้วยบัญชี Supabase Auth ก่อน เพราะ RLS ให้เฉพาะ `authenticated`
เขียนตาราง `commands` ได้ (ดูหัวข้อ "ข้อควรรู้เรื่องสิทธิ์" ด้านล่าง)

ถ้าจะเขียนเองก็ใช้แค่นี้:

```js
// ปุ่ม "Reset ยอดรวม"
const { error } = await supabase.rpc('reset_totals');

// ปุ่ม "คืนค่าทั้งหมด"
const { error } = await supabase.rpc('factory_reset', { p_clear_log: true });

// ปุ่มสั่งงานทั่วไป (ของเดิม)
await supabase.from('commands').insert({ cmd: 'RESET' });
await supabase.from('commands').insert({ cmd: 'RESET_COUNT' });
await supabase.from('commands').insert({ cmd: 'SET', value: 20 });
```

---

## ข้อควรรู้เรื่องสิทธิ์ (RLS)

สิทธิ์ปัจจุบันบน Supabase เป็นแบบนี้

| ตาราง | `anon` (คีย์ที่ฝังใน ESP32 และหน้าเว็บ) | `authenticated` (ล็อกอินแล้ว) |
|---|---|---|
| `status` | insert / select / update | select |
| `event_log` | insert | select |
| `commands` | select / update | **insert** / select |

จุดสำคัญ: **`anon` สั่งงานเครื่องไม่ได้** เพราะ insert ตาราง `commands` ไม่ได้

- หน้าเว็บที่ใช้แค่ anon key **แสดงสถานะได้** (anon select `status` ได้)
- แต่ **กดปุ่มสั่งงานไม่ได้** จะได้ error `new row violates row-level security policy`
- ส่วน `event_log` anon อ่านไม่ได้เลย ตารางประวัติจะว่างถ้าไม่ล็อกอิน

ถ้าเจอปุ่มกดแล้วไม่มีอะไรเกิดขึ้น ให้เปิด DevTools Console ดูก่อน มักเป็นเรื่องนี้

ทางเลือกถ้าไม่อยากล็อกอิน (เช่นจอแสดงผลหน้างานที่ให้ทุกคนกดได้):
เพิ่ม policy ให้ `anon` insert `commands` ได้ แต่ต้องเข้าใจว่า anon key ฝังอยู่ในหน้าเว็บสาธารณะ
= **ใครก็ตามบนอินเทอร์เน็ตที่เปิด view-source จะสั่งงานเครื่องได้** ไม่แนะนำ

---

## การ deploy บน Netlify

เว็บ `stunning-speculoos-0a330b.netlify.app` ต่อกับ repo นี้แล้ว

| รายการ | ค่า |
|---|---|
| Repo | `guitarPannatorn/Claude` |
| Branch ที่ deploy เป็น production | `claude/data-retrieval-question-e517au` |
| Publish directory | `web/` (ตั้งใน `netlify.toml`) |
| Build command | ไม่มี — เป็น static HTML ล้วน |

**push ขึ้น branch นี้ = เว็บอัปเดตอัตโนมัติ** ไม่ต้องลากไฟล์เข้า Netlify อีกแล้ว

`netlify.toml` ตั้ง `publish = "web"` ไว้ เพื่อให้เอาขึ้นเว็บเฉพาะโฟลเดอร์ `web/`
ถ้าไม่ตั้ง Netlify จะเอา **ทั้ง repo** ขึ้นเว็บ ทำให้ซอร์สเฟิร์มแวร์และไฟล์ SQL
ถูกเปิดให้ดาวน์โหลดจากอินเทอร์เน็ตได้ และที่สำคัญคือไม่มี `index.html` ที่ root
เว็บหลักจะขึ้น 404

### กู้หน้าเว็บเดิมกลับมา

หน้า `index.html` เดิมที่เคย drop ไว้ ยังอยู่ในประวัติ deploy ของ Netlify
(deploy id `6a93de38af6a039d8798dab2` วันที่ 30 ส.ค. 07:39)
เข้า Netlify → Deploys → เลือก deploy นั้น → **Preview** เพื่อดู
หรือ **Publish deploy** เพื่อย้อนกลับไปใช้เวอร์ชันนั้น

ถ้าอยากเอาเนื้อหาบางส่วนของหน้าเดิมมารวมกับหน้าใหม่ ให้ก๊อป HTML จากหน้า preview
มาใส่ `web/index.html` แล้ว push
