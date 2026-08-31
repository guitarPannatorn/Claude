/*
  ===================================================================
  arduino_secrets.example.h  -  ไฟล์ตัวอย่างค่าความลับ
  ===================================================================

  วิธีใช้:
    1. ก๊อปไฟล์นี้ตั้งชื่อใหม่เป็น  arduino_secrets.h
       (วางไว้โฟลเดอร์เดียวกับ esp32_supabase_rfid.ino)
    2. ใส่ชื่อ WiFi และรหัสผ่านจริงของตัวเองลงไป
    3. ไฟล์ arduino_secrets.h ถูกใส่ไว้ใน .gitignore แล้ว
       จึงไม่ถูก commit ขึ้น GitHub

  ทำไมต้องแยกไฟล์:
    รหัส WiFi เป็นความลับจริง ถ้า commit ขึ้น repo สาธารณะ
    ใครก็ตามที่เห็นโค้ดจะเข้า WiFi ที่บ้าน/ที่โรงงานได้ทันที

    ส่วน Supabase anon key ไม่ใช่ความลับ มันถูกออกแบบมาให้ฝังใน
    เบราว์เซอร์อยู่แล้ว ความปลอดภัยจริงมาจาก RLS policy บน Supabase
    (ตอนนี้ตั้งไว้ว่า anon สั่งงานเครื่องไม่ได้ ต้องล็อกอินก่อน)
  ===================================================================
*/

#ifndef ARDUINO_SECRETS_H
#define ARDUINO_SECRETS_H

// ---------- WiFi (ใส่ค่าจริงของคุณ) ----------
#define SECRET_WIFI_SSID      "ชื่อ WiFi ของคุณ"
#define SECRET_WIFI_PASSWORD  "รหัสผ่าน WiFi ของคุณ"

// ---------- Supabase ----------
#define SECRET_SUPABASE_URL       "https://ufmwcstlzygrnmzkpgbs.supabase.co"
#define SECRET_SUPABASE_ANON_KEY  "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InVmbXdjc3Rsenlncm5temtwZ2JzIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODc3NzgxOTgsImV4cCI6MjEwMzM1NDE5OH0.geNcAdB1YL2N9XozB49uyu-__N5KyRg4gKuj0ftOD1g"

#endif
