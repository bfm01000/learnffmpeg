# 006 Stats JSON Export

## 本次目标

把页面上的实时 stats 变成可以沉淀的数据文件。这样后续做弱网模拟、码率控制或端到端延迟实验时，不只能看当场 UI，还可以导出 JSON 做离线对比。

## 做了什么

修改 `public/index.html`：

1. 在控制区新增 `Export Stats` 按钮。
2. 更新趋势图区说明，明确趋势图展示最近 60 秒，导出文件保存本次会话全部采样。

修改 `public/app.js`：

1. 新增 `statsSamples`，保存本次 join 后的完整采样序列。
2. 每次 `getStats()` 后生成结构化 sample，包含时间戳、房间、client id、source、connection state、ICE state、send / recv kbps、target kbps、RTT、jitter、Data RTT、packet loss、fps、resolution、bytes sent / received。
3. DataChannel pong 返回时记录最近一次应用层 RTT，并写入后续 stats sample。
4. 新增 `exportStats()`，把本次会话采样打包为 JSON Blob 并触发浏览器下载。
5. 新会话开始时清空旧采样；离开房间后保留本次采样，方便先停止通话再导出。

修改 `public/styles.css`：

1. 调整控制区网格，让新增按钮在桌面和移动端都能自然排布。

修改 `README.md`：

1. 把 stats JSON 导出加入当前能力。
2. 清理已经完成的下一步条目，留下真正还没做的方向。

## 技术取舍

导出逻辑放在浏览器端完成，没有引入后端存储。这样仍然保持 MVP 轻量：服务端只做静态文件和信令，实验数据由浏览器在本地生成并下载。

当前导出的 Data RTT 是最近一次 DataChannel ping-pong 结果，和每秒 stats sample 对齐保存。它不是视频端到端延迟，但能帮助分析应用层控制消息延迟和 WebRTC 传输状态之间的关系。

## 后续可扩展

1. 增加 JSON 离线分析脚本，输出平均值、P95、码率变化点和异常区间。
2. 增加画面时间戳，把 Test Pattern 扩展为视频 E2E latency 测量源。
3. 增加弱网实验标签，把每次导出的 JSON 和实验条件关联起来。
