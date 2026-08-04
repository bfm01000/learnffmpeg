# Native Sender 信令协议兼容说明

## 当前信令服务

当前 `server.js` 是一个极简 WebSocket signaling server，负责静态页面和房间消息转发。

支持的消息类型：

- `join`
- `leave`
- `offer`
- `answer`
- `candidate`
- `peer-joined`
- `peer-left`
- `joined`
- `hello`
- `error`

服务端不理解 SDP 内容，也不参与媒体传输，只做房间内转发。

## native sender 可复用的消息

native 端加入房间：

```json
{
  "type": "join",
  "room": "native-demo",
  "role": "native-sender"
}
```

服务端当前会忽略 `role` 字段，但保留它有助于日志和后续 UI 区分端类型。

native 发 offer：

```json
{
  "type": "offer",
  "room": "native-demo",
  "sdp": {
    "type": "offer",
    "sdp": "..."
  }
}
```

浏览器回 answer：

```json
{
  "type": "answer",
  "room": "native-demo",
  "target": "native-client-id",
  "sdp": {
    "type": "answer",
    "sdp": "..."
  }
}
```

ICE candidate：

```json
{
  "type": "candidate",
  "room": "native-demo",
  "candidate": {
    "candidate": "candidate:...",
    "sdpMid": "0",
    "sdpMLineIndex": 0
  }
}
```

## 浏览器端当前行为

当前浏览器端逻辑是：

1. 加入房间后等待其他 peer。
2. 收到 `peer-joined` 时主动创建 offer。
3. 收到 `offer` 时创建 answer。
4. 收到 `answer` 时设置 remote description。
5. 收到 `candidate` 时 addIceCandidate。

因此 native sender 有两种接入方式：

### 方案 A：native 后加入

浏览器先加入房间，native 后加入。浏览器收到 `peer-joined` 后会主动发 offer，native 作为 answerer。

优点：前端基本不用改。

缺点：native 第一版要支持 answerer 路径。

### 方案 B：native 主动 offer

native 加入房间后主动创建 offer，浏览器收到 offer 后 answer。

优点：native sender 更像发送端主动建连。

缺点：如果浏览器也因为 `peer-joined` 发 offer，可能出现 glare，需要增加角色判断或 polite peer 逻辑。

## 推荐第一版

推荐使用方案 A：浏览器先进入房间，native sender 后加入并作为 answerer。

这样改动最少：

- `server.js` 不需要改。
- `public/app.js` 暂时不需要改。
- native sender 只需要处理 offer、answer、candidate 和 joined。

## 后续增强

如果要让 native sender 主动 offer，可以给前端增加角色选择：

```text
browser-offerer
browser-answerer
native-sender
```

或者实现 perfect negotiation，避免双方同时 offer。
