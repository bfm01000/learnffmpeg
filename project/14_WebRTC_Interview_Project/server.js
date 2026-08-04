const crypto = require("crypto");
const fs = require("fs");
const http = require("http");
const path = require("path");

const HOST = process.env.HOST || "0.0.0.0";
const PORT = Number(process.env.PORT || 3000);
const PUBLIC_DIR = path.join(__dirname, "public");

const clients = new Map();
const rooms = new Map();

function sendJson(client, payload) {
  if (client.socket.destroyed) return;
  const text = JSON.stringify(payload);
  const data = Buffer.from(text);
  const header = [];
  header.push(0x81);

  if (data.length < 126) {
    header.push(data.length);
  } else if (data.length < 65536) {
    header.push(126, (data.length >> 8) & 0xff, data.length & 0xff);
  } else {
    throw new Error("WebSocket payload too large for this signaling server");
  }

  client.socket.write(Buffer.concat([Buffer.from(header), data]));
}

function broadcast(roomId, payload, exceptClientId) {
  const room = rooms.get(roomId);
  if (!room) return;

  for (const clientId of room) {
    if (clientId === exceptClientId) continue;
    const client = clients.get(clientId);
    if (client) sendJson(client, payload);
  }
}

function leaveRoom(client) {
  if (!client.roomId) return;

  const room = rooms.get(client.roomId);
  if (room) {
    room.delete(client.id);
    if (room.size === 0) rooms.delete(client.roomId);
  }

  broadcast(client.roomId, { type: "peer-left", peerId: client.id }, client.id);
  client.roomId = null;
}

function handleMessage(client, payload) {
  let message;
  try {
    message = JSON.parse(payload);
  } catch {
    sendJson(client, { type: "error", message: "Invalid JSON message" });
    return;
  }

  if (message.type === "join") {
    const roomId = String(message.room || "default").trim() || "default";
    const role = String(message.role || "browser").trim() || "browser";
    leaveRoom(client);

    if (!rooms.has(roomId)) rooms.set(roomId, new Set());
    const room = rooms.get(roomId);
    const peers = [...room]
      .map((peerId) => clients.get(peerId))
      .filter(Boolean)
      .map((peer) => ({ id: peer.id, role: peer.role || "browser" }));
    room.add(client.id);
    client.roomId = roomId;
    client.role = role;

    sendJson(client, { type: "joined", room: roomId, clientId: client.id, peers });
    broadcast(roomId, { type: "peer-joined", peerId: client.id, role }, client.id);
    return;
  }

  if (message.type === "leave") {
    leaveRoom(client);
    sendJson(client, { type: "left" });
    return;
  }

  if (["offer", "answer", "candidate"].includes(message.type)) {
    if (!client.roomId) {
      sendJson(client, { type: "error", message: "Join a room before signaling" });
      return;
    }

    const relay = {
      ...message,
      from: client.id
    };

    if (message.target) {
      const target = clients.get(message.target);
      if (target && target.roomId === client.roomId) sendJson(target, relay);
      return;
    }

    broadcast(client.roomId, relay, client.id);
    return;
  }

  sendJson(client, { type: "error", message: `Unsupported message type: ${message.type}` });
}

function decodeFrame(buffer) {
  if (buffer.length < 2) return null;
  const opcode = buffer[0] & 0x0f;
  const masked = (buffer[1] & 0x80) !== 0;
  let length = buffer[1] & 0x7f;
  let offset = 2;

  if (length === 126) {
    if (buffer.length < 4) return null;
    length = buffer.readUInt16BE(2);
    offset = 4;
  } else if (length === 127) {
    return { opcode: 0x8, payload: "" };
  }

  if (!masked || buffer.length < offset + 4 + length) return null;
  const mask = buffer.subarray(offset, offset + 4);
  offset += 4;

  const payload = Buffer.alloc(length);
  for (let i = 0; i < length; i += 1) {
    payload[i] = buffer[offset + i] ^ mask[i % 4];
  }

  return { opcode, payload: payload.toString("utf8"), used: offset + length };
}

function serveStatic(req, res) {
  const urlPath = decodeURIComponent(new URL(req.url, `http://${req.headers.host}`).pathname);
  const relativePath = urlPath === "/" ? "index.html" : urlPath.replace(/^\/+/, "");
  const filePath = path.normalize(path.join(PUBLIC_DIR, relativePath));

  if (!filePath.startsWith(PUBLIC_DIR)) {
    res.writeHead(403);
    res.end("Forbidden");
    return;
  }

  fs.readFile(filePath, (error, data) => {
    if (error) {
      res.writeHead(404);
      res.end("Not found");
      return;
    }

    const ext = path.extname(filePath);
    const contentTypes = {
      ".html": "text/html; charset=utf-8",
      ".css": "text/css; charset=utf-8",
      ".js": "application/javascript; charset=utf-8"
    };

    res.writeHead(200, { "content-type": contentTypes[ext] || "application/octet-stream" });
    res.end(data);
  });
}

const server = http.createServer(serveStatic);

server.on("upgrade", (req, socket) => {
  if (req.headers.upgrade?.toLowerCase() !== "websocket") {
    socket.destroy();
    return;
  }

  const key = req.headers["sec-websocket-key"];
  const accept = crypto
    .createHash("sha1")
    .update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
    .digest("base64");

  socket.write(
    [
      "HTTP/1.1 101 Switching Protocols",
      "Upgrade: websocket",
      "Connection: Upgrade",
      `Sec-WebSocket-Accept: ${accept}`,
      "",
      ""
    ].join("\r\n")
  );

  const client = {
    id: crypto.randomUUID(),
    roomId: null,
    socket
  };
  clients.set(client.id, client);
  sendJson(client, { type: "hello", clientId: client.id });

  socket.on("data", (chunk) => {
    const frame = decodeFrame(chunk);
    if (!frame) return;
    if (frame.opcode === 0x8) {
      socket.end();
      return;
    }
    if (frame.opcode === 0x1) handleMessage(client, frame.payload);
  });

  socket.on("close", () => {
    leaveRoom(client);
    clients.delete(client.id);
  });

  socket.on("error", () => {
    leaveRoom(client);
    clients.delete(client.id);
  });
});

server.listen(PORT, HOST, () => {
  console.log(`LowLatency WebRTC Lab running at http://localhost:${PORT}`);
});
