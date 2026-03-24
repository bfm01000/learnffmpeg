# H.264 SPS/PPS 十六进制速查卡（面试版）

## 1) 先看 NALU 类型（第一步就能拿分）

H.264 NALU 常见起始码：
- `00 00 01` 或 `00 00 00 01`

起始码后第 1 个字节是 NAL header：
- `forbidden_zero_bit`：1 bit（必须为 0）
- `nal_ref_idc`：2 bits（参考重要性）
- `nal_unit_type`：5 bits（类型）

快速判断类型（`nal_unit_type`）：
- `7` = SPS
- `8` = PPS
- `6` = SEI
- `5` = IDR
- `1` = non-IDR slice

常见十六进制头字节：
- `0x67` 常见表示 SPS（`nal_unit_type=7`）
- `0x68` 常见表示 PPS（`nal_unit_type=8`）
- `0x06` 常见表示 SEI
- `0x65` 常见表示 IDR

> 注意：高 3 位会随 `nal_ref_idc` 变化，不是所有 SPS 都一定是 `0x67`，但面试中这样说通常足够。

---

## 2) 解析前必须做的一步：去 Emulation Prevention Byte

在 RBSP 里可能插入 `0x03` 防止误触发起始码：
- 模式常见为：`00 00 03 xx`

解析前要把该 `0x03` 去掉（在 `00 00 03` 场景下去），得到原始比特串再按语法读字段。

面试关键词：
- SODB -> RBSP -> EBSP 的关系
- 十六进制看起来和语法字段不一一对应，原因就是有防竞争字节

---

## 3) SPS 字段“手工识别顺序”（高频）

SPS（`nal_unit_type=7`）RBSP 常见主干顺序：
1. `profile_idc`
2. `constraint_set flags + reserved`
3. `level_idc`
4. `seq_parameter_set_id`（ue(v)）
5. （高 profile 才有）`chroma_format_idc`、`bit_depth_*`、`scaling_matrix...`
6. `log2_max_frame_num_minus4`
7. `pic_order_cnt_type`（及其分支字段）
8. `max_num_ref_frames`
9. `pic_width_in_mbs_minus1`
10. `pic_height_in_map_units_minus1`
11. `frame_mbs_only_flag`
12. `frame_cropping_flag + offsets`
13. `vui_parameters_present_flag`（有则继续读 VUI）

关键落地公式（面试常问分辨率怎么算）：
- `width = (pic_width_in_mbs_minus1 + 1) * 16 - crop_left*2 - crop_right*2`
- `height` 需结合 `frame_mbs_only_flag` 和 crop 计算  
  - progressive（`frame_mbs_only_flag=1`）常见近似：  
    `height = (pic_height_in_map_units_minus1 + 1) * 16 - crop_top*2 - crop_bottom*2`

> 严谨场景还要结合色度格式、场编码等 crop unit，不同 `chroma_format_idc` 的裁剪单位不同。

---

## 4) PPS 字段“手工识别顺序”

PPS（`nal_unit_type=8`）RBSP 常见主干顺序：
1. `pic_parameter_set_id`（ue(v)）
2. `seq_parameter_set_id`（ue(v)）-> 关联到哪个 SPS
3. `entropy_coding_mode_flag`（0=CAVLC, 1=CABAC）
4. `pic_order_present_flag`（旧文档也叫相关变体名）
5. `num_slice_groups_minus1`（FMO，少见）
6. `num_ref_idx_l0/l1_default_active_minus1`
7. `weighted_pred_flag / weighted_bipred_idc`
8. `pic_init_qp_minus26`
9. `chroma_qp_index_offset`
10. `deblocking_filter_control_present_flag`
11. `constrained_intra_pred_flag`
12. `redundant_pic_cnt_present_flag`
13. （高 profile 扩展）`transform_8x8_mode_flag`、`scaling matrix`、`second_chroma_qp_index_offset`

---

## 5) 面试可直接说的“手工识别流程”

“我先按起始码切 NALU，看 NAL header 的 `nal_unit_type`，先定位 SPS/PPS。  
然后把 payload 做 RBSP 还原（去 `00 00 03` 的防竞争字节），再按 Exp-Golomb 语法依次解字段。  
SPS 我重点看 profile/level、POC 类型、参考帧数、宽高和裁剪、VUI；  
PPS 我重点看它关联的 SPS、CABAC/CAVLC、默认参考索引和 QP/deblock 参数。  
如果要校验分辨率，我会用 `pic_width_in_mbs_minus1`、`pic_height_in_map_units_minus1` 和 crop 公式算最终显示尺寸。”

---

## 6) 高频误区（面试避坑）

- 误区1：只看 `0x67/0x68` 就断言 100% 是 SPS/PPS  
  - 正解：应看 `nal_unit_type`，`0x67/0x68` 只是常见值。

- 误区2：直接按字节位移读语法字段  
  - 正解：很多字段是 `ue(v)/se(v)` 可变长编码，必须按比特流语法解码。

- 误区3：忽略 `00 00 03`  
  - 正解：不去防竞争字节会导致后续字段全部错位。

- 误区4：分辨率只按宏块乘 16  
  - 正解：还要扣裁剪项，且 crop unit 受 chroma_format / 场帧模式影响。

---

## 7) 30 秒口播模板（SPS/PPS 手工识别）

“十六进制里我先按起始码切 NALU，再读 NAL header 的 `nal_unit_type`，7 是 SPS、8 是 PPS。解析前先把 `00 00 03` 防竞争字节去掉还原 RBSP。SPS 重点解析 profile/level、POC、参考帧、宽高和裁剪、VUI；PPS 重点解析关联的 SPS、CABAC/CAVLC、QP 和 deblock 相关参数。分辨率不能只看宏块数乘 16，还要结合 crop 和色度格式修正。”

