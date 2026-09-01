-- ===================================================================
-- แยก "เวลาที่แถวถูกแก้ไข" ออกจาก "เวลาที่ ESP32 เชื่อมต่อจริงล่าสุด"
-- ===================================================================
--
-- บั๊กที่เจอหลังติดตั้ง 02_status_updated_at_trigger.sql:
--   ปุ่ม "ล้างยอดรวม"/"คืนค่าทั้งหมด" บนเว็บ (reset_totals()/factory_reset())
--   ตั้งใจอัปเดตตาราง status ให้เห็นผลทันทีบนเว็บ "โดยไม่ต้องรอเครื่องตอบกลับ"
--   (ดูคอมเมนต์ในไฟล์ 01_reset_functions.sql) แม้ ESP32 จะออฟไลน์อยู่ก็ตาม
--   แต่ trigger ที่เพิ่งเพิ่มใน 02 จะ stamp updated_at = now() ให้ทุก UPDATE
--   บนตารางนี้แบบไม่แยกแยะที่มา ผลคือแค่กดปุ่มรีเซ็ตบนเว็บ (ซึ่งไม่ต้องมี
--   ESP32 อยู่จริงเลย) ก็ทำให้ updated_at ขยับ หน้าเว็บเลยเข้าใจผิดว่า
--   "เชื่อมต่อกลับมาแล้ว" ทั้งที่เครื่องยังไม่ได้เชื่อมต่อจริง
--
-- แก้โดยแยกเป็น 2 คอลัมน์ที่ความหมายต่างกันชัดเจน:
--   - updated_at    = แถวนี้ถูกแก้ไขล่าสุดเมื่อไหร่ (ไม่ว่าจะมาจากอะไร)
--   - device_seen_at = ESP32 อัปโหลดสถานะจริงล่าสุดเมื่อไหร่ (ใช้เช็คการเชื่อมต่อ)
--
-- reset_totals()/factory_reset() จะตั้งค่า session flag ไว้ก่อน UPDATE
-- เพื่อบอก trigger ว่า "นี่คือการอัปเดตแบบ optimistic จากเว็บ ไม่ใช่ ESP32
-- เขียนจริง" ทำให้ trigger ข้ามการ stamp device_seen_at ในกรณีนี้
--
-- วิธีติดตั้ง: รันไฟล์นี้ใน Supabase Dashboard -> SQL Editor
-- (รันได้แม้ยังไม่เคยรัน 02 มาก่อน เพราะไฟล์นี้ครอบคลุมการสร้าง trigger ใหม่ทั้งหมด)
-- ===================================================================

alter table public.status
  add column if not exists device_seen_at timestamptz;

-- ตั้งค่าเริ่มต้นให้แถวเก่า ไม่งั้นเว็บจะเห็น device_seen_at เป็น null
-- แล้วขึ้น "รอสัญญาณจากเครื่อง" ค้างจนกว่า ESP32 จะอัปโหลดรอบถัดไป
update public.status set device_seen_at = updated_at where device_seen_at is null;

create or replace function public.set_status_updated_at()
returns trigger
language plpgsql
as $$
begin
  new.updated_at = now();

  -- ข้ามการ stamp device_seen_at ถ้าเป็นการอัปเดตแบบ optimistic จากปุ่มบนเว็บ
  -- (reset_totals()/factory_reset() ตั้ง flag นี้ไว้ก่อนเรียก UPDATE)
  if coalesce(current_setting('app.manual_status_write', true), '') <> 'true' then
    new.device_seen_at = now();
  end if;

  return new;
end;
$$;

drop trigger if exists trg_status_updated_at on public.status;

create trigger trg_status_updated_at
  before update on public.status
  for each row
  execute function public.set_status_updated_at();


-- -------------------------------------------------------------------
-- reset_totals() / factory_reset(): ตั้ง flag ก่อน UPDATE เพื่อบอก trigger
-- ว่าไม่ใช่ ESP32 เขียนจริง (set_config ตัวที่ 3 = true คือ local ต่อ
-- transaction เดียว ไม่หลุดไปกระทบ session/connection อื่น)
-- -------------------------------------------------------------------

create or replace function public.reset_totals()
returns bigint
language plpgsql
security definer
set search_path = public
as $$
declare
  v_id bigint;
begin
  insert into public.commands (cmd, processed)
  values ('RESET_TOTAL', false)
  returning id into v_id;

  perform set_config('app.manual_status_write', 'true', true);

  update public.status
     set ok_total   = 0,
         ng_total   = 0,
         rate       = 0,
         event      = 'total_reset',
         updated_at = now()
   where id = 1;

  return v_id;
end;
$$;

create or replace function public.factory_reset(p_clear_log boolean default true)
returns bigint
language plpgsql
security definer
set search_path = public
as $$
declare
  v_id bigint;
  c_default_setting constant integer := 10;
begin
  insert into public.commands (cmd, processed)
  values ('FACTORY_RESET', false)
  returning id into v_id;

  update public.commands
     set processed = true
   where processed = false
     and id <> v_id;

  if p_clear_log then
    delete from public.event_log
     where id is not null;

    delete from public.commands
     where processed = true
       and id <> v_id;
  end if;

  perform set_config('app.manual_status_write', 'true', true);

  update public.status
     set setting      = c_default_setting,
         counting     = 0,
         full_counter = false,
         d9           = false,
         d11          = false,
         d12          = false,
         lock_old     = false,
         lock_auto    = false,
         ok_total     = 0,
         ng_total     = 0,
         rate         = 0,
         event        = 'factory_reset',
         updated_at   = now()
   where id = 1;

  return v_id;
end;
$$;

comment on function public.set_status_updated_at() is
  'ตั้ง status.updated_at เสมอ และตั้ง status.device_seen_at เฉพาะตอนที่ไม่ใช่การอัปเดตแบบ optimistic จากปุ่มบนเว็บ (ใช้แยก "แถวถูกแก้ไข" ออกจาก "ESP32 เชื่อมต่อจริง")';
