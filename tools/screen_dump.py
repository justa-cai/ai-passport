#!/usr/bin/env python3
"""screen_dump.py — 从设备串口截取屏幕并解码为 PNG。

用法：python3 tools/screen_dump.py [串口] [输出.png]
默认串口自动探测（Espressif USB JTAG），输出 tmp/screenshot.png。

协议见 main/screen_dump.h：发送 `shot`，收 SHOT:BEGIN/…/SHOT:END，
base64 → RLE(count,b1,b2) → RGB565 → PNG（RGB565 字节序与面板一致，
LVGL 快照为小端字节对）。
"""
import base64
import re
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def find_port():
    from serial.tools import list_ports
    for p in list_ports.comports():
        if "303A:1001" in p.hwid:  # Espressif USB Serial/JTAG
            return p.device
    return None


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else REPO / "tmp" / "screenshot.png"
    if port is None:
        print("未发现 Espressif USB JTAG 设备")
        return 1

    import serial

    ser = serial.Serial(port, 115200, timeout=1)
    # 复位设备后采集（快照在长时间运行/多次绘制后会退化为全黑，
    # 复位后立即截屏稳定——与渲染缓存状态有关，绕过）
    ser.setDTR(False)
    ser.setRTS(True)
    time.sleep(0.1)
    ser.setRTS(False)
    time.sleep(3.5)
    ser.reset_input_buffer()
    ser.write(b"shot\n")
    ser.flush()

    b64 = []
    meta = {}
    deadline = time.time() + 30
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if line.startswith("SHOT:BEGIN"):
            meta = dict(re.findall(r"(\w+)=(\d+)", line))
        elif line.startswith("SHOT:") and not line.startswith(("SHOT:END", "SHOT:ERR")):
            b64.append(line[5:])
        elif line.startswith("SHOT:END"):
            break
        elif line.startswith("SHOT:ERR"):
            print("设备端错误:", line)
            ser.close()
            return 1
    ser.close()

    if not b64:
        print("未收到截图数据")
        return 1

    import base64 as b64mod

    joined = "".join(b64)
    # 容错：容忍日志行混入造成的截断，按字符集过滤 + 补齐 padding
    joined = "".join(c for c in joined if c in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=")
    if len(joined) % 4:
        joined += "=" * (4 - len(joined) % 4)
    try:
        rle = b64mod.b64decode(joined)
    except Exception as exc:
        print(f"base64 解码失败（{exc}），收集行数 {len(b64)}")
        return 1
    w, h = int(meta.get("w", 240)), int(meta.get("h", 320))
    px = bytearray()
    i = 0
    while i + 3 <= len(rle) and len(px) < w * h * 2:
        run, b1, b2 = rle[i], rle[i + 1], rle[i + 2]
        px += bytes((b1, b2)) * run
        i += 3

    import numpy as np
    from PIL import Image

    arr = np.frombuffer(bytes(px[: w * h * 2]), dtype="<u2").reshape(h, w)
    # 先加宽再乘：uint8*255 会按值基提升保持 uint8，30*255=7650 回绕成 226，
    # 整屏 RGB 被压成 (7,3,7) 黑——务必在宽类型里做刻度换算
    def scale(v: "np.ndarray", max_val: int) -> "np.ndarray":
        return (v.astype(np.uint32) * 255 // max_val).astype(np.uint8)

    r = scale((arr >> 11) & 0x1F, 31)
    g = scale((arr >> 5) & 0x3F, 63)
    b = scale(arr & 0x1F, 31)
    img = Image.merge("RGB", (Image.fromarray(r), Image.fromarray(g), Image.fromarray(b)))
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)
    print(f"已保存 {out} ({w}x{h}, rle {len(rle)} B)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
