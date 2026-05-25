# Day 5 · 算法刷题二（哈希 / 树 / 栈队列）

**目标**：累计本周 12～15 题；树与哈希是大厂笔试最常见类型。  
**建议时长**：4～5 小时

---

## 今日题单（6 题）

| # | 题目 | 难度 | 考点 |
|---|------|------|------|
| 1 | [94. 二叉树的中序遍历](https://leetcode.cn/problems/binary-tree-inorder-traversal/) | 易 | 递归 / 栈 |
| 2 | [102. 二叉树的层序遍历](https://leetcode.cn/problems/binary-tree-level-order-traversal/) | 中 | BFS |
| 3 | [236. 二叉树的最近公共祖先](https://leetcode.cn/problems/lowest-common-ancestor-of-a-binary-tree/) | 中 | 递归 |
| 4 | [347. 前 K 个高频元素](https://leetcode.cn/problems/top-k-frequent-elements/) | 中 | 堆 / 桶 |
| 5 | [739. 每日温度](https://leetcode.cn/problems/daily-temperatures/) | 中 | 单调栈 |
| 6 | [46. 全排列](https://leetcode.cn/problems/permutations/) | 中 | 回溯（了解） |

**最低完成**：1、2、4、5 必做。

---

## 模板背诵（15min）

### 二叉树递归

```cpp
void dfs(TreeNode* root) {
    if (!root) return;
    dfs(root->left);
    dfs(root->right);
}
```

### 层序 BFS

```cpp
queue<TreeNode*> q;
q.push(root);
while (!q.empty()) {
    int sz = q.size();
    for (int i = 0; i < sz; ++i) { /* ... */ }
}
```

---

## 下午穿插（1.5h）· 系统设计口述

准备一道**非代码**题（内核岗爱问）：

> 你如何设计一个**向后兼容**的 SDK 特性开关体系？

用你 4K 项目的答案骨架：

1. Java 层 `RecordParam` 新增 bool 开关，默认 `false`
2. JNI 透传到底层策略分发
3. 旧业务零改动，新业务按需开启
4. 灰度与回滚路径

写下来，控制在 **2 分钟口述**。

---

## 晚间（30min）

- [ ] 算法错题二刷 1 道
- [ ] 复盘清单：本周算法累计题号列表

---

## 今日产出物

- [ ] ≥4 题 AC（全周累计 ≥12）
- [ ] SDK 兼容性设计口述稿 1 份
