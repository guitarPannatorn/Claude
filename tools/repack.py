#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
===================================================================
repack.py — ประกอบ web/index.html ใหม่จากซอร์สที่แก้ไขได้
===================================================================

ทำไมต้องมีไฟล์นี้:
  web/index.html เป็นไฟล์ที่ถูก bundle มาจาก Claude Design Canvas
  ขนาด 560KB มีทั้ง React, supabase-js, ฟอนต์ woff2 ถูก gzip + base64
  ยัดรวมไว้ในไฟล์เดียว แก้ด้วยมือแทบไม่ได้เลย

  โค้ดแอปจริงอยู่ในบล็อก <script type="__bundler/template"> ซึ่งเป็น
  สตริง JSON ก้อนเดียว เราจึงแตกมันออกมาเก็บไว้เป็น web/src/template.html
  ให้แก้ไขได้ตามปกติ แล้วใช้สคริปต์นี้ยัดกลับเข้าไป

วิธีใช้:
    1. แก้ web/src/template.html  (HTML + โค้ด React ของหน้าเว็บ)
    2. python3 tools/repack.py
    3. git add -A && git commit && git push   -> Netlify deploy เอง

ตรวจสอบก่อนเขียนไฟล์:
    - โค้ด JS ในบล็อก text/x-dc ต้อง parse ผ่าน (ถ้ามี node จะเรียก node --check)
    - JSON ที่ประกอบใหม่ต้อง decode กลับมาได้ตรงกับต้นฉบับเป๊ะ
    - บล็อก __bundler/* ทุกอันต้องยัง parse ได้
===================================================================
"""
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUNDLE = os.path.join(ROOT, "web", "index.html")
TEMPLATE = os.path.join(ROOT, "web", "src", "template.html")

TPL_BLOCK = re.compile(r'(<script type="__bundler/template">\s*)(.*?)(\s*</script>)', re.S)
DC_SCRIPT = re.compile(r'<script type="text/x-dc"[^>]*>(.*?)</script>', re.S)


def fail(msg):
    print("ERROR: " + msg, file=sys.stderr)
    sys.exit(1)


def check_js(template_text):
    """ตรวจ syntax ของโค้ดแอปด้วย node ถ้ามีติดตั้งอยู่"""
    m = DC_SCRIPT.search(template_text)
    if not m:
        print("  เตือน: ไม่พบบล็อก text/x-dc ข้ามการตรวจ syntax")
        return
    if not shutil.which("node"):
        print("  ข้ามการตรวจ syntax (ไม่มี node)")
        return
    with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False, encoding="utf-8") as f:
        f.write(m.group(1))
        path = f.name
    try:
        r = subprocess.run(["node", "--check", path], capture_output=True, text=True)
        if r.returncode != 0:
            fail("JS syntax ไม่ผ่าน:\n" + r.stderr)
        print("  JS syntax ผ่าน (%s ตัวอักษร)" % format(len(m.group(1)), ","))
    finally:
        os.unlink(path)


def main():
    for p in (BUNDLE, TEMPLATE):
        if not os.path.exists(p):
            fail("ไม่พบไฟล์ %s" % p)

    bundle = io.open(BUNDLE, encoding="utf-8").read()
    template = io.open(TEMPLATE, encoding="utf-8").read()

    print("ตรวจซอร์ส:")
    check_js(template)

    m = TPL_BLOCK.search(bundle)
    if not m:
        fail("หาบล็อก __bundler/template ใน web/index.html ไม่เจอ")

    # escape "</" เป็น "</" แบบเดียวกับที่ bundler ต้นทางทำ
    # ไม่งั้น "</script>" ที่อยู่ในสตริงจะไปปิดแท็ก <script> ที่ห่อมันอยู่
    blob = json.dumps(template, ensure_ascii=False).replace("</", "<\\u002F")
    if json.loads(blob) != template:
        fail("round-trip JSON ไม่ตรงกับต้นฉบับ")
    print("  round-trip JSON ตรงกัน")

    out = bundle[:m.start(2)] + blob + bundle[m.end(2):]

    # ตรวจว่าไฟล์ผลลัพธ์ยังแกะทุกบล็อกได้
    for tag in ("manifest", "ext_resources", "page_order", "template"):
        mm = re.search(r'<script type="__bundler/%s">\s*(.*?)\s*</script>' % tag, out, re.S)
        if not mm:
            fail("บล็อก %s หายไปจากไฟล์ผลลัพธ์" % tag)
        try:
            json.loads(mm.group(1))
        except Exception as e:
            fail("บล็อก %s parse ไม่ได้: %s" % (tag, e))
    print("  บล็อก __bundler/* ทั้ง 4 อัน parse ผ่าน")

    io.open(BUNDLE, "w", encoding="utf-8").write(out)
    print("\nเขียน web/index.html แล้ว: %s -> %s ตัวอักษร"
          % (format(len(bundle), ","), format(len(out), ",")))


if __name__ == "__main__":
    main()
