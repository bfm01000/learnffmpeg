# 作业 2：MP4/TS 解析程序

## 目标

编写一个简单程序，能够解析 MP4 或 TS 文件，并打印出关键结构的基本信息，以验证对 moov、trak、mdat（以及 TS 包结构）的理解。

---

## 要求

### 对 MP4 文件

程序应能解析 MP4 的**顶层 box**，并打印至少以下信息：

- **ftyp**（若存在）：box 大小、在文件中的偏移。
- **moov**：box 大小、在文件中的偏移；若可能，打印其子 box 中 **trak** 的个数及每个 trak 的 box 大小/偏移。
- **mdat**：box 大小、在文件中的偏移。

（不要求解析 trak 内部的 stbl、sample 等，能识别并列出 trak 即可。）

### 对 TS 文件

程序应能解析 TS 的**固定 188 字节包**，并打印至少以下信息：

- 总包数（或根据文件大小推算）。
- 同步字节（0x47）的检查：前若干包是否均为 0x47。
- 可选：统计不同 PID 的包数量（前 N 个包即可），或解析出 PAT/PMT 的 PID 并简单打印。

（不要求完整解析 PES 或音视频内容。）

### 通用

- 程序可通过命令行接受一个文件路径，根据扩展名（.mp4/.ts）或魔数自动判断格式并分支处理。
- 输出格式清晰（如表格或分节文本），便于阅读。

---

## 实现建议

- 语言不限（C/C++/Python/Go 等均可）。
- MP4：按 8 字节为单位读 box 头（size + type），再根据 size 递归或跳过；顶层只需扫描一层，对 moov 内可再扫一层统计 trak。
- TS：按 188 字节读包，检查首字节是否为 0x47；PID 可从包头第 2～3 字节解析（参见 TS 标准或 docs/02_ts_format.md）。

---

## 作答区

### 代码位置

- 代码目录/仓库路径：
- 编译与运行方式（含示例命令）：
- 依赖（如无则写“无”）：

### 示例输出

请贴出对**一个 MP4 文件**和**一个 TS 文件**的运行输出（可打码路径或文件名）。

**MP4 示例：**

```
（粘贴输出）
```

**TS 示例：**

```
（粘贴输出）
```

---

## 验证标准

- 对给定 MP4 能正确识别并打印 ftyp、moov、trak 个数、mdat 的大小与偏移。
- 对给定 TS 能正确按 188 字节解析并报告包数及同步字节检查结果。
- 代码可编译/可运行，说明清晰。

---

## 参考答案（含讲解）

> 以下给出一个 Python 示例实现思路与示例输出。你也可以用 C/C++/Go 实现，满足同样能力即可。

### 1. 参考实现（Python）

```python
#!/usr/bin/env python3
import os
import struct
import sys
from collections import Counter


def read_u32_be(b):
    return struct.unpack(">I", b)[0]


def parse_mp4(path):
    size = os.path.getsize(path)
    print("== MP4 ==")
    print(f"file: {path}")
    print(f"size: {size}")

    with open(path, "rb") as f:
        offset = 0
        while offset + 8 <= size:
            f.seek(offset)
            header = f.read(8)
            box_size = read_u32_be(header[:4])
            box_type = header[4:8].decode("ascii", errors="replace")

            if box_size == 0:
                box_size = size - offset
            elif box_size == 1:
                ext = f.read(8)
                box_size = struct.unpack(">Q", ext)[0]

            print(f"top-box type={box_type} size={box_size} offset={offset}")

            if box_type == "moov":
                # 扫描 moov 的一层子 box，统计 trak
                moov_end = offset + box_size
                sub_off = offset + 8
                trak_count = 0
                while sub_off + 8 <= moov_end:
                    f.seek(sub_off)
                    sh = f.read(8)
                    s_size = read_u32_be(sh[:4])
                    s_type = sh[4:8].decode("ascii", errors="replace")
                    if s_size < 8:
                        break
                    if s_type == "trak":
                        trak_count += 1
                        print(f"  trak size={s_size} offset={sub_off}")
                    sub_off += s_size
                print(f"  trak_count={trak_count}")

            if box_size < 8:
                break
            offset += box_size


def parse_ts(path, check_packets=20):
    size = os.path.getsize(path)
    pkt_size = 188
    total = size // pkt_size
    remain = size % pkt_size
    pid_counter = Counter()
    sync_ok = True

    print("== TS ==")
    print(f"file: {path}")
    print(f"size: {size}")
    print(f"packet_size: {pkt_size}, total_packets: {total}, remain_bytes: {remain}")

    with open(path, "rb") as f:
        for i in range(total):
            pkt = f.read(pkt_size)
            if len(pkt) < pkt_size:
                break
            if i < check_packets and pkt[0] != 0x47:
                sync_ok = False
            # PID: 13 bits = ((byte1 & 0x1F) << 8) | byte2
            pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
            pid_counter[pid] += 1

    print(f"sync_check_first_{check_packets}_packets: {'PASS' if sync_ok else 'FAIL'}")
    print("top_pid_counts:")
    for pid, cnt in pid_counter.most_common(8):
        print(f"  pid=0x{pid:04X} count={cnt}")


def main():
    if len(sys.argv) != 2:
        print("usage: python parser.py <input.mp4|input.ts>")
        return 1
    path = sys.argv[1]
    lower = path.lower()
    if lower.endswith(".mp4"):
        parse_mp4(path)
    elif lower.endswith(".ts"):
        parse_ts(path)
    else:
        print("unknown file type, only .mp4/.ts in this demo")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

### 2. 示例输出（示例）

**MP4 示例：**

```text
== MP4 ==
file: sample.mp4
size: 10485760
top-box type=ftyp size=32 offset=0
top-box type=moov size=4821 offset=32
  trak size=3011 offset=140
  trak size=1642 offset=3151
  trak_count=2
top-box type=mdat size=10480899 offset=4853
```

**TS 示例：**

```text
== TS ==
file: sample.ts
size: 564000
packet_size: 188, total_packets: 3000, remain_bytes: 0
sync_check_first_20_packets: PASS
top_pid_counts:
  pid=0x0100 count=1632
  pid=0x0101 count=1198
  pid=0x0000 count=27
  pid=0x1000 count=27
```

### 3. 讲解

- **MP4 部分如何达标：**
  - 通过读取 box 头（size + type）顺序扫描顶层 box，能定位 `ftyp`、`moov`、`mdat`。
  - 对 `moov` 内再做一层扫描统计 `trak`，说明你已经建立了“moov 包含多个轨道”的结构认识。

- **TS 部分如何达标：**
  - 以 188 字节切包并检查 `0x47`，验证你掌握了 TS 的基础包边界规则。
  - 提取 PID 并统计频次，说明你理解 TS 按 PID 复用多路流的机制。

- **为什么这个练习有效：**
  - 它同时覆盖“文件型容器（MP4）”和“传输型容器（TS）”两种思维方式；
  - 你输出的是“结构信息”而非解码结果，正好对应本阶段学习目标（封装层理解）。
