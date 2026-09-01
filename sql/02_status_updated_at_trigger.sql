-- ===================================================================
-- Trigger: อัปเดต status.updated_at อัตโนมัติทุกครั้งที่แถวถูกแก้ไข
-- ===================================================================
--
-- ทำไมต้องมี: หน้าเว็บใช้ status.updated_at เป็นตัวบอกว่า ESP32
-- "เชื่อมต่ออยู่จริง" ครั้งล่าสุดเมื่อไหร่ (ดู applyStatus() ใน
-- web/src/template.html) แต่การอัปโหลดสถานะปกติจาก ESP32 เป็นแค่
-- REST upsert ธรรมดา (POST .../status?on_conflict=id) ที่ไม่ได้ส่ง
-- ฟิลด์ updated_at มาด้วย เดิมคอลัมน์นี้จึงถูกตั้งค่าเฉพาะตอนเรียก
-- reset_totals()/factory_reset() เท่านั้น ทำให้เว็บเข้าใจผิดว่า
-- เครื่อง "เชื่อมต่ออยู่" แม้บอร์ดจะออฟไลน์ไปนานแล้ว ตราบใดที่แถวเก่า
-- ยังอยู่ใน Supabase (เพราะเว็บใช้เวลาที่ query สำเร็จแทนเวลาที่ข้อมูล
-- ถูกเขียนจริง)
--
-- Trigger นี้ทำให้ "ทุก UPDATE บนตาราง status" (ไม่ว่าจะมาจาก ESP32
-- อัปโหลดปกติ หรือจากปุ่มรีเซ็ตบนเว็บ) ตั้ง updated_at = now() ให้เองที่
-- ฝั่งฐานข้อมูลเสมอ ไม่ต้องพึ่ง firmware ส่งฟิลด์นี้มา และไม่ต้องพึ่ง
-- นาฬิกาของ ESP32 (ซึ่งไม่มี NTP อยู่แล้ว)
--
-- วิธีติดตั้ง: รันไฟล์นี้ใน Supabase Dashboard -> SQL Editor
-- ===================================================================

create or replace function public.set_status_updated_at()
returns trigger
language plpgsql
as $$
begin
  new.updated_at = now();
  return new;
end;
$$;

drop trigger if exists trg_status_updated_at on public.status;

create trigger trg_status_updated_at
  before update on public.status
  for each row
  execute function public.set_status_updated_at();

comment on function public.set_status_updated_at() is
  'ตั้ง status.updated_at = now() อัตโนมัติทุกครั้งที่แถวถูก UPDATE เพื่อให้เว็บใช้เป็นตัวเช็คว่า ESP32 เชื่อมต่ออยู่จริงล่าสุดเมื่อไหร่';
