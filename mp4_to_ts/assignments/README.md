# 作业说明

本目录包含五项作业，用于验证「封装格式」「FFmpeg 解析流程」「命令行转码」与「C++ API 转换实战」的学习效果。建议按学习计划中的 Day 1～Day 6 顺序完成。

---

## 作业列表

| 作业 | 文件 | 对应目标 | 建议完成时间 |
|------|------|----------|--------------|
| 作业 1 | [assignment_1.md](./assignment_1.md) | 理解 av_read_frame() 如何读取数据包 | Day 3 学习后 |
| 作业 2 | [assignment_2.md](./assignment_2.md) | 编写 MP4/TS 解析程序，打印 moov/trak/mdat 等信息 | Day 1～2 学习后，Day 4 收尾 |
| 作业 3 | [assignment_3.md](./assignment_3.md) | 对比 MP4 与 TS 的不同点及适用场景 | Day 2 学习后 |
| 作业 4 | [assignment_4.md](./assignment_4.md) | 完成 MP4 转 TS 的封装转换与重编码实践 | Day 5 学习后 |
| 作业 5 | [assignment_5.md](./assignment_5.md) | 用 C++ 调 FFmpeg API 完成 MP4 转 TS | Day 6 学习后 |

---

## 提交与验证

- **作业 1、3**：在对应 `assignment_*.md` 中直接填写答案（文字/列表/表格均可）。
- **作业 2**：在项目根目录或 `assignments/` 下新建目录（如 `parser/`），放置源码与 README；在 `assignment_2.md` 中写明代码路径、编译/运行方式及示例输出。
- **自检**：
  - 作业 1：能清晰描述从 `av_read_frame()` 到 demuxer、到得到 AVPacket 的流程。
  - 作业 2：对至少一个 MP4 和一个 TS 运行程序，能正确打印 moov/trak/mdat（MP4）及包/流基本信息（TS）。
  - 作业 3：能列出至少 3 点结构/用途差异，并说明典型应用场景。
  - 作业 4：能跑通 MP4→TS 命令，使用 `ffprobe` 验证输出，并解释关键参数含义。
  - 作业 5：能编译运行 C++ 程序生成 TS，能解释 `av_read_frame`/时间戳重映射/写包流程。

---

## 参考资料

- 讲解文档：`../docs/01_mp4_format.md`、`02_ts_format.md`、`03_ffmpeg_demux.md`、`04_mp4_to_ts_transcode.md`、`05_cpp_mp4_to_ts.md`
- FFmpeg 源码：`libavformat/utils.c`（av_read_frame）、`mov.c`、`mpegts.c`
