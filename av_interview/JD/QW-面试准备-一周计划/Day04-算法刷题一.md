# Day 4 · 算法刷题一（数组 / 链表 / 双指针）

**目标**：内核岗笔试高频题型集中突破，累计本周第 4～9 题。  
**建议时长**：4～5 小时（今日算法占比提高到 50%）

---

## 刷题原则

1. **先自己想 15min**，再看题解；必须手写一遍通过。
2. 每题记录：**思路一句话 + 复杂度 + 一个坑**。
3. 不推荐死磕 Hard；中等题稳定更重要。

---

## 今日题单（6 题，选做 5 题也可）

| # | 题目 | 难度 | 考点 | 建议用时 |
|---|------|------|------|----------|
| 1 | [3. 无重复字符的最长子串](https://leetcode.cn/problems/longest-substring-without-repeating-characters/) | 中 | 滑动窗口 | 25min |
| 2 | [15. 三数之和](https://leetcode.cn/problems/3sum/) | 中 | 排序 + 双指针 | 30min |
| 3 | [142. 环形链表 II](https://leetcode.cn/problems/linked-list-cycle-ii/) | 中 | 快慢指针 | 25min |
| 4 | [238. 除自身以外数组的乘积](https://leetcode.cn/problems/product-of-array-except-self/) | 中 | 前缀积 | 25min |
| 5 | [56. 合并区间](https://leetcode.cn/problems/merge-intervals/) | 中 | 排序 + 区间 | 25min |
| 6 | [53. 最大子数组和](https://leetcode.cn/problems/maximum-subarray/) | 易 | DP / 贪心 | 15min |

**最低完成**：1、3、6 必做；其余选 2 道。

---

## C++ 实现注意（面试笔试）

```cpp
// 习惯写法，避免面试低级失误
class Solution {
public:
    vector<int> xxx(vector<int>& nums) {
        // reserve  when size known
        // 边界：empty, size==1
    }
};
```

- [ ] `vector` 传参：引用 + const
- [ ] 整数溢出：乘积题用 `long long`
- [ ] 链表题：dummy head 技巧

---

## 下午穿插（1.5h）· C++ 复习

不重学理论，只做：

- [ ] 回顾 Day1～2 复盘清单里的 **红灯项** 各 1 道自测口述
- [ ] 重做错题 1 道（若昨天有）

---

## 晚间（30min）

- [ ] 录音：轮换项目（4K / AJB / 剪辑 选一，压缩到 2 分钟）
- [ ] 在 `周复盘清单.md` 的「算法」区填今日题号 + 掌握度

---

## 今日产出物

- [ ] ≥5 题 AC，错题有文字复盘
- [ ] 滑动窗口 + 双指针模板能默写框架
