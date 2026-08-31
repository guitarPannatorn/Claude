-- ===================================================================
-- ฟังก์ชันสำหรับปุ่ม "Reset ยอดรวม" และ "คืนค่าทั้งหมด" บนเว็บ
-- ===================================================================
--
-- ทำไมต้องใช้ RPC function ไม่ยิง REST ตรงๆ:
--   RLS policy ปัจจุบันไม่มีใครมีสิทธิ์ DELETE บนตาราง event_log เลย
--   (มีแค่ anon insert ได้ / authenticated อ่านได้)
--   การเปิดสิทธิ์ DELETE ให้ทั้งตารางเสี่ยงเกินไป จึงห่อไว้ใน function
--   แบบ SECURITY DEFINER ที่ทำได้เฉพาะงานที่กำหนดไว้เท่านั้น
--
-- สิทธิ์: เรียกได้เฉพาะผู้ใช้ที่ล็อกอินแล้ว (authenticated)
--         anon ซึ่งเป็น key ที่ฝังอยู่ใน ESP32 และหน้าเว็บ เรียกไม่ได้
--
-- วิธีติดตั้ง: รันไฟล์นี้ใน Supabase Dashboard -> SQL Editor
-- ===================================================================


-- -------------------------------------------------------------------
-- 1) reset_totals()
--    ล้างเฉพาะยอดรวมสะสม OK/NG
--    ไม่แตะ Setting และ Counting ของรอบปัจจุบัน
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
  -- เข้าคิวคำสั่งให้ ESP32 มาดึงไปสั่ง Nano ล้างค่าใน EEPROM
  insert into public.commands (cmd, processed)
  values ('RESET_TOTAL', false)
  returning id into v_id;

  -- อัปเดตหน้าเว็บให้เห็นผลทันที ไม่ต้องรอเครื่องตอบกลับ
  -- (ถ้าเครื่องออฟไลน์ ค่าจะกลับมาตอนเครื่องออนไลน์แล้วส่งค่าจริงขึ้นมา)
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


-- -------------------------------------------------------------------
-- 2) factory_reset(p_clear_log)
--    คืนค่าทั้งหมดกลับค่าเริ่มต้น สำหรับตอนทดสอบเว็บหรือเครื่อง
--    - ยอดรวมสะสม OK/NG = 0
--    - Counting = 0, Setting = 10
--    - ปลดสถานะ NG / LOCK / Full counter
--    - ล้างประวัติ event_log (ถ้า p_clear_log = true)
--    - ยกเลิกคำสั่งเก่าที่ยังค้างคิว
-- -------------------------------------------------------------------
create or replace function public.factory_reset(p_clear_log boolean default true)
returns bigint
language plpgsql
security definer
set search_path = public
as $$
declare
  v_id bigint;
  -- ต้องตรงกับ SETTING_DEFAULT ใน firmware/Nano_Pro/Nano_Pro.ino
  c_default_setting constant integer := 10;
begin
  insert into public.commands (cmd, processed)
  values ('FACTORY_RESET', false)
  returning id into v_id;

  -- คำสั่งเก่าที่ยังไม่ถูกประมวลผล (เช่นตอนเครื่องออฟไลน์อยู่)
  -- ต้องยกเลิกทิ้ง ไม่งั้นจะเด้งมาทำงานทีหลังแล้วทับค่าที่เพิ่งรีเซ็ต
  update public.commands
     set processed = true
   where processed = false
     and id <> v_id;

  if p_clear_log then
    -- ต้องมี WHERE เสมอ แม้จะตั้งใจลบทั้งตาราง
    -- Supabase preload ส่วนขยาย safeupdate ให้ role authenticated/anon ไว้
    -- (session_preload_libraries = 'safeupdate') ซึ่งบล็อก DELETE/UPDATE
    -- ที่ไม่มี WHERE เพื่อกันอุบัติเหตุลบข้อมูลทั้งตาราง
    -- SECURITY DEFINER เปลี่ยนแค่สิทธิ์ ไม่ได้ปิดการ์ดตัวนี้
    -- id เป็น bigint identity จึงไม่มีทางเป็น null -> เงื่อนไขนี้ครอบคลุมทุกแถว
    delete from public.event_log
     where id is not null;

    -- เก็บกวาดคำสั่งเก่าที่ประมวลผลไปแล้วด้วย ไม่ให้ตารางบวมเรื่อยๆ
    delete from public.commands
     where processed = true
       and id <> v_id;
  end if;

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


-- -------------------------------------------------------------------
-- สิทธิ์การเรียกใช้
-- -------------------------------------------------------------------
revoke all on function public.reset_totals()                  from public, anon;
revoke all on function public.factory_reset(boolean)          from public, anon;

grant execute on function public.reset_totals()               to authenticated;
grant execute on function public.factory_reset(boolean)       to authenticated;


comment on function public.reset_totals() is
  'ล้างยอดรวมสะสม OK/NG ทั้งบนเว็บและใน EEPROM ของ Nano (ผ่านคำสั่ง RESET_TOTAL)';

comment on function public.factory_reset(boolean) is
  'คืนค่าทั้งหมดกลับค่าเริ่มต้นสำหรับการทดสอบ ส่งคำสั่ง FACTORY_RESET ไปที่เครื่องด้วย';
