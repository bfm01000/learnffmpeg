const elements = {
  roomInput: document.querySelector("#roomInput"),
  sourceSelect: document.querySelector("#sourceSelect"),
  resolutionSelect: document.querySelector("#resolutionSelect"),
  fpsSelect: document.querySelector("#fpsSelect"),
  maxBitrateInput: document.querySelector("#maxBitrateInput"),
  strategySelect: document.querySelector("#strategySelect"),
  applyEncodingButton: document.querySelector("#applyEncodingButton"),
  exportStatsButton: document.querySelector("#exportStatsButton"),
  joinButton: document.querySelector("#joinButton"),
  leaveButton: document.querySelector("#leaveButton"),
  localVideo: document.querySelector("#localVideo"),
  remoteVideo: document.querySelector("#remoteVideo"),
  connectionState: document.querySelector("#connectionState"),
  eventLog: document.querySelector("#eventLog"),
  rtt: document.querySelector("#rtt"),
  jitter: document.querySelector("#jitter"),
  packetLoss: document.querySelector("#packetLoss"),
  sendBitrate: document.querySelector("#sendBitrate"),
  recvBitrate: document.querySelector("#recvBitrate"),
  fps: document.querySelector("#fps"),
  resolution: document.querySelector("#resolution"),
  iceState: document.querySelector("#iceState"),
  dataRtt: document.querySelector("#dataRtt"),
  targetBitrate: document.querySelector("#targetBitrate"),
  abrState: document.querySelector("#abrState"),
  bitrateChart: document.querySelector("#bitrateChart"),
  latencyChart: document.querySelector("#latencyChart")
};

const rtcConfig = {
  iceServers: [{ urls: "stun:stun.l.google.com:19302" }]
};

let socket = null;
let peer = null;
let dataChannel = null;
let pingTimer = null;
let localStream = null;
let clientId = null;
let room = null;
let statsTimer = null;
let patternTimer = null;
let targetBitrateKbps = Number(elements.maxBitrateInput.value);
let lastAbrActionAt = 0;
let stableAbrSamples = 0;
let lastPacketLoss = 0;
let lastStats = {
  timestamp: 0,
  bytesSent: 0,
  bytesReceived: 0
};
let statsHistory = [];
let statsSamples = [];
let sessionStartedAt = null;
let latestDataRttMs = null;

function log(message) {
  const time = new Date().toLocaleTimeString();
  elements.eventLog.textContent = `[${time}] ${message}
${elements.eventLog.textContent}`;
}

function setState(state) {
  elements.connectionState.textContent = state;
}

function send(message) {
  if (!socket || socket.readyState !== WebSocket.OPEN) return;
  socket.send(JSON.stringify(message));
}

function getPeerId(peerInfo) {
  if (!peerInfo) return null;
  return typeof peerInfo === "string" ? peerInfo : peerInfo.id;
}

function getPeerRole(peerInfo) {
  if (!peerInfo || typeof peerInfo === "string") return "browser";
  return peerInfo.role || "browser";
}

function parseResolution(value) {
  const [width, height] = value.split("x").map(Number);
  return { width, height };
}

function stopPattern() {
  if (patternTimer) {
    window.clearInterval(patternTimer);
    patternTimer = null;
  }
}

function createTestPatternStream(width, height, frameRate) {
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext("2d");
  let frame = 0;

  stopPattern();
  patternTimer = window.setInterval(() => {
    const now = new Date();
    const t = frame / Math.max(frameRate, 1);
    const gradient = context.createLinearGradient(0, 0, width, height);
    gradient.addColorStop(0, `hsl(${(frame * 3) % 360}, 70%, 42%)`);
    gradient.addColorStop(1, `hsl(${(frame * 3 + 120) % 360}, 72%, 36%)`);
    context.fillStyle = gradient;
    context.fillRect(0, 0, width, height);

    const barWidth = Math.max(40, width / 8);
    context.fillStyle = "rgba(255, 255, 255, 0.22)";
    context.fillRect((frame * 8) % (width + barWidth) - barWidth, 0, barWidth, height);

    context.fillStyle = "rgba(0, 0, 0, 0.48)";
    context.fillRect(24, 24, Math.min(width - 48, 440), 124);
    context.fillStyle = "#ffffff";
    context.font = "700 28px system-ui, sans-serif";
    context.fillText("WebRTC Test Pattern", 44, 68);
    context.font = "18px system-ui, sans-serif";
    context.fillText(`${width}x${height} @ ${frameRate}fps`, 44, 102);
    context.fillText(now.toLocaleTimeString(), 44, 132);

    context.fillStyle = "#ffffff";
    context.beginPath();
    context.arc(width - 58, 58, 22 + Math.sin(t * 4) * 8, 0, Math.PI * 2);
    context.fill();
    frame += 1;
  }, 1000 / Math.max(frameRate, 1));

  return canvas.captureStream(frameRate);
}

async function openLocalMedia() {
  const { width, height } = parseResolution(elements.resolutionSelect.value);
  const frameRate = Number(elements.fpsSelect.value);

  if (elements.sourceSelect.value === "pattern") {
    localStream = createTestPatternStream(width, height, frameRate);
    log("Using generated test pattern stream");
  } else {
    try {
      const devices = await navigator.mediaDevices.enumerateDevices();
      const cameras = devices.filter((device) => device.kind === "videoinput");
      log(`Browser camera devices: ${cameras.length}`);

      localStream = await navigator.mediaDevices.getUserMedia({
        audio: false,
        video: {
          width: { ideal: width },
          height: { ideal: height },
          frameRate: { ideal: frameRate, max: frameRate }
        }
      });
      stopPattern();
      log("Using browser camera stream");
    } catch (error) {
      log(`Camera unavailable: ${error.name || "Error"} ${error.message || ""}`.trim());
      log("Fallback to generated test pattern stream");
      elements.sourceSelect.value = "pattern";
      localStream = createTestPatternStream(width, height, frameRate);
    }
  }

  elements.localVideo.srcObject = localStream;
}

function getVideoSender() {
  if (!peer) return null;
  return peer.getSenders().find((sender) => sender.track?.kind === "video") || null;
}

async function applyEncodingParameters(reason = "manual") {
  const sender = getVideoSender();
  if (!sender) {
    log("No video sender yet; join a room first");
    return;
  }

  const bitrateKbps = Math.max(80, Number(elements.maxBitrateInput.value) || 800);
  const maxFramerate = Number(elements.fpsSelect.value) || 30;
  const params = sender.getParameters();
  if (!params.encodings || params.encodings.length === 0) {
    params.encodings = [{}];
  }

  params.encodings[0].maxBitrate = bitrateKbps * 1000;
  params.encodings[0].maxFramerate = maxFramerate;
  await sender.setParameters(params);

  targetBitrateKbps = bitrateKbps;
  elements.targetBitrate.textContent = `${bitrateKbps} kbps`;
  log(`Applied encoding: maxBitrate=${bitrateKbps}kbps, maxFramerate=${maxFramerate}, reason=${reason}`);
}


function updateAbrState(text) {
  elements.abrState.textContent = text;
}

async function setTargetBitrate(bitrateKbps, reason) {
  const next = Math.max(80, Math.min(8000, Math.round(bitrateKbps)));
  if (next === targetBitrateKbps) return;
  elements.maxBitrateInput.value = String(next);
  await applyEncodingParameters(reason);
}

async function evaluateSimpleAbr(sample) {
  if (elements.strategySelect.value !== "simple-abr") {
    updateAbrState("Manual");
    lastPacketLoss = sample.packetLoss;
    stableAbrSamples = 0;
    return;
  }

  const now = performance.now();
  const lossDelta = Math.max(0, sample.packetLoss - lastPacketLoss);
  lastPacketLoss = sample.packetLoss;

  const badNetwork = lossDelta >= 3 || sample.rttMs >= 350 || sample.jitterMs >= 80;
  const stableNetwork = lossDelta === 0 && sample.rttMs > 0 && sample.rttMs < 150 && sample.jitterMs < 30;

  if (badNetwork && now - lastAbrActionAt > 3000) {
    stableAbrSamples = 0;
    lastAbrActionAt = now;
    updateAbrState("降码率");
    await setTargetBitrate(Math.max(150, targetBitrateKbps * 0.7), `abr-down loss=${lossDelta} rtt=${Math.round(sample.rttMs)} jitter=${Math.round(sample.jitterMs)}`);
    return;
  }

  if (stableNetwork) {
    stableAbrSamples += 1;
  } else {
    stableAbrSamples = 0;
  }

  if (stableAbrSamples >= 8 && now - lastAbrActionAt > 5000) {
    stableAbrSamples = 0;
    lastAbrActionAt = now;
    updateAbrState("升码率");
    await setTargetBitrate(Math.min(2500, targetBitrateKbps * 1.15), "abr-up stable-network");
    return;
  }

  updateAbrState(stableAbrSamples > 0 ? `稳定观察 ${stableAbrSamples}/8` : "观察中");
}
function setupDataChannel(channel) {
  dataChannel = channel;
  dataChannel.onopen = () => {
    log("DataChannel open");
    startPingLoop();
  };
  dataChannel.onclose = () => {
    log("DataChannel closed");
    stopPingLoop();
  };
  dataChannel.onmessage = (event) => {
    let message;
    try {
      message = JSON.parse(event.data);
    } catch {
      return;
    }

    if (message.type === "ping" && dataChannel.readyState === "open") {
      dataChannel.send(JSON.stringify({ type: "pong", t: message.t }));
      return;
    }

    if (message.type === "pong") {
      const rtt = performance.now() - message.t;
      latestDataRttMs = rtt;
      elements.dataRtt.textContent = `${Math.round(rtt)} ms`;
    }
  };
}

function startPingLoop() {
  stopPingLoop();
  pingTimer = window.setInterval(() => {
    if (dataChannel?.readyState === "open") {
      dataChannel.send(JSON.stringify({ type: "ping", t: performance.now() }));
    }
  }, 1000);
}

function stopPingLoop() {
  if (pingTimer) {
    window.clearInterval(pingTimer);
    pingTimer = null;
  }
}

function createPeerConnection() {
  peer = new RTCPeerConnection(rtcConfig);

  localStream.getTracks().forEach((track) => {
    peer.addTrack(track, localStream);
  });

  peer.ondatachannel = (event) => {
    log("DataChannel received");
    setupDataChannel(event.channel);
  };

  peer.onicecandidate = (event) => {
    if (event.candidate) {
      send({ type: "candidate", room, candidate: event.candidate });
    }
  };

  peer.ontrack = (event) => {
    const [remoteStream] = event.streams;
    elements.remoteVideo.srcObject = remoteStream;
    log("Remote stream attached");
  };

  peer.onconnectionstatechange = () => {
    setState(peer.connectionState);
    log(`PeerConnection state: ${peer.connectionState}`);
  };

  peer.oniceconnectionstatechange = () => {
    elements.iceState.textContent = peer.iceConnectionState;
  };
}

async function makeOffer(targetPeerId = null) {
  if (!dataChannel) {
    setupDataChannel(peer.createDataChannel("latency"));
  }
  await applyEncodingParameters();
  const offer = await peer.createOffer();
  await peer.setLocalDescription(offer);
  send({ type: "offer", room, sdp: peer.localDescription, target: targetPeerId });
  log(targetPeerId ? `Offer sent to ${targetPeerId}` : "Offer sent");
}

async function handleOffer(message) {
  if (!peer) createPeerConnection();
  await peer.setRemoteDescription(message.sdp);
  await applyEncodingParameters();

  const answer = await peer.createAnswer();
  await peer.setLocalDescription(answer);
  send({ type: "answer", room, sdp: peer.localDescription, target: message.from });
  log("Answer sent");
}

async function handleAnswer(message) {
  await peer.setRemoteDescription(message.sdp);
  log("Answer applied");
}

async function handleCandidate(message) {
  if (!peer || !message.candidate) return;
  await peer.addIceCandidate(message.candidate);
}

function connectSignaling() {
  const protocol = window.location.protocol === "https:" ? "wss" : "ws";
  socket = new WebSocket(`${protocol}://${window.location.host}`);

  socket.onopen = () => {
    send({ type: "join", room, role: "browser" });
    log(`Joined signaling room: ${room}`);
  };

  socket.onmessage = async (event) => {
    const message = JSON.parse(event.data);

    if (message.type === "hello") {
      clientId = message.clientId;
      log(`Client id: ${clientId}`);
      return;
    }

    if (message.type === "joined") {
      log(`Room ready, peers: ${message.peers.length}`);
      const nativePeer = message.peers.find((peerInfo) => getPeerRole(peerInfo) === "native-sender");
      const nativePeerId = getPeerId(nativePeer);
      if (nativePeerId) await makeOffer(nativePeerId);
      return;
    }

    if (message.type === "peer-joined") {
      log(`Peer joined: ${message.peerId}${message.role ? ` (${message.role})` : ""}`);
      await makeOffer(message.peerId);
      return;
    }

    if (message.type === "offer") {
      await handleOffer(message);
      return;
    }

    if (message.type === "answer") {
      await handleAnswer(message);
      return;
    }

    if (message.type === "candidate") {
      await handleCandidate(message);
      return;
    }

    if (message.type === "peer-left") {
      elements.remoteVideo.srcObject = null;
      log(`Peer left: ${message.peerId}`);
      return;
    }

    if (message.type === "error") {
      log(`Server error: ${message.message}`);
    }
  };

  socket.onclose = () => log("Signaling socket closed");
}

function formatBitrate(bitsPerSecond) {
  if (!Number.isFinite(bitsPerSecond)) return "-";
  if (bitsPerSecond > 1_000_000) return `${(bitsPerSecond / 1_000_000).toFixed(2)} Mbps`;
  return `${Math.round(bitsPerSecond / 1000)} kbps`;
}

function drawChart(canvas, series, options) {
  if (!canvas) return;
  const context = canvas.getContext("2d");
  const { width, height } = canvas;
  context.clearRect(0, 0, width, height);
  context.fillStyle = "#fbfcfc";
  context.fillRect(0, 0, width, height);

  context.strokeStyle = "#d8e2e5";
  context.lineWidth = 1;
  for (let i = 1; i < 4; i += 1) {
    const y = (height / 4) * i;
    context.beginPath();
    context.moveTo(0, y);
    context.lineTo(width, y);
    context.stroke();
  }

  const values = series.flatMap((item) => item.values).filter((value) => Number.isFinite(value));
  const max = Math.max(options.minMax || 1, ...values);
  const left = 42;
  const right = 12;
  const top = 16;
  const bottom = 28;
  const plotWidth = width - left - right;
  const plotHeight = height - top - bottom;

  context.fillStyle = "#587079";
  context.font = "12px system-ui, sans-serif";
  context.fillText(options.label, 10, 16);
  context.fillText(options.format(max), 10, top + 8);
  context.fillText("0", 10, height - bottom + 4);

  series.forEach((item) => {
    context.strokeStyle = item.color;
    context.lineWidth = 2;
    context.beginPath();
    item.values.forEach((value, index) => {
      const x = left + (plotWidth * index) / Math.max(item.values.length - 1, 1);
      const y = height - bottom - (Math.max(value, 0) / max) * plotHeight;
      if (index === 0) context.moveTo(x, y);
      else context.lineTo(x, y);
    });
    context.stroke();
  });

  let legendX = left;
  series.forEach((item) => {
    context.fillStyle = item.color;
    context.fillRect(legendX, height - 18, 10, 10);
    context.fillStyle = "#40535a";
    context.fillText(item.name, legendX + 14, height - 9);
    legendX += 92;
  });
}

function updateCharts(sample) {
  statsHistory.push(sample);
  statsHistory = statsHistory.slice(-60);

  drawChart(
    elements.bitrateChart,
    [
      { name: "send", color: "#176b5d", values: statsHistory.map((item) => item.sendKbps) },
      { name: "recv", color: "#b54b3a", values: statsHistory.map((item) => item.recvKbps) },
      { name: "target", color: "#6f5aa8", values: statsHistory.map((item) => item.targetKbps) }
    ],
    { label: "kbps", minMax: 100, format: (value) => `${Math.round(value)}k` }
  );

  drawChart(
    elements.latencyChart,
    [
      { name: "rtt", color: "#255f9c", values: statsHistory.map((item) => item.rttMs) },
      { name: "jitter", color: "#8a5a16", values: statsHistory.map((item) => item.jitterMs) }
    ],
    { label: "ms", minMax: 10, format: (value) => `${Math.round(value)}ms` }
  );
}

async function updateStats() {
  if (!peer) return;

  const report = await peer.getStats();
  let rtt = null;
  let jitter = null;
  let packetLoss = null;
  let bytesSent = 0;
  let bytesReceived = 0;
  let fps = null;
  let resolution = null;

  report.forEach((stat) => {
    if (stat.type === "candidate-pair" && stat.state === "succeeded" && stat.nominated) {
      rtt = stat.currentRoundTripTime;
    }

    if (stat.type === "inbound-rtp" && stat.kind === "video") {
      jitter = stat.jitter;
      packetLoss = stat.packetsLost;
      bytesReceived = stat.bytesReceived || bytesReceived;
      fps = stat.framesPerSecond || stat.framesDecoded || fps;
      if (stat.frameWidth && stat.frameHeight) resolution = `${stat.frameWidth}x${stat.frameHeight}`;
    }

    if (stat.type === "outbound-rtp" && stat.kind === "video") {
      bytesSent = stat.bytesSent || bytesSent;
      fps = stat.framesPerSecond || stat.framesEncoded || fps;
      if (stat.frameWidth && stat.frameHeight) resolution = `${stat.frameWidth}x${stat.frameHeight}`;
    }
  });

  const now = performance.now();
  const seconds = lastStats.timestamp ? (now - lastStats.timestamp) / 1000 : 0;
  const sendBitrate = seconds > 0 ? ((bytesSent - lastStats.bytesSent) * 8) / seconds : NaN;
  const recvBitrate = seconds > 0 ? ((bytesReceived - lastStats.bytesReceived) * 8) / seconds : NaN;

  lastStats = { timestamp: now, bytesSent, bytesReceived };

  elements.rtt.textContent = rtt == null ? "-" : `${Math.round(rtt * 1000)} ms`;
  elements.jitter.textContent = jitter == null ? "-" : `${Math.round(jitter * 1000)} ms`;
  elements.packetLoss.textContent = packetLoss == null ? "-" : String(packetLoss);
  elements.sendBitrate.textContent = formatBitrate(sendBitrate);
  elements.recvBitrate.textContent = formatBitrate(recvBitrate);
  elements.fps.textContent = fps == null ? "-" : String(Math.round(fps));
  elements.resolution.textContent = resolution || "-";

  const sample = {
    timestamp: new Date().toISOString(),
    elapsedMs: sessionStartedAt == null ? 0 : Math.round(now - sessionStartedAt),
    room,
    clientId,
    source: elements.sourceSelect.value,
    connectionState: peer.connectionState,
    iceState: peer.iceConnectionState,
    sendKbps: Number.isFinite(sendBitrate) ? sendBitrate / 1000 : 0,
    recvKbps: Number.isFinite(recvBitrate) ? recvBitrate / 1000 : 0,
    targetKbps: targetBitrateKbps,
    rttMs: rtt == null ? 0 : rtt * 1000,
    jitterMs: jitter == null ? 0 : jitter * 1000,
    dataRttMs: latestDataRttMs == null ? null : Math.round(latestDataRttMs),
    packetLoss: packetLoss == null ? null : packetLoss,
    fps: fps == null ? null : Math.round(fps),
    resolution,
    bytesSent,
    bytesReceived
  };

  statsSamples.push(sample);
  elements.exportStatsButton.disabled = statsSamples.length === 0;
  updateCharts(sample);
  await evaluateSimpleAbr(sample);
}

function startStatsLoop() {
  stopStatsLoop();
  statsTimer = window.setInterval(updateStats, 1000);
}

function stopStatsLoop() {
  if (statsTimer) {
    window.clearInterval(statsTimer);
    statsTimer = null;
  }
}

async function join() {
  room = elements.roomInput.value.trim() || "lab";
  sessionStartedAt = performance.now();
  statsSamples = [];
  latestDataRttMs = null;
  elements.joinButton.disabled = true;
  elements.leaveButton.disabled = false;
  elements.applyEncodingButton.disabled = false;
  elements.exportStatsButton.disabled = true;
  elements.targetBitrate.textContent = `${targetBitrateKbps} kbps`;
  updateAbrState(elements.strategySelect.value === "simple-abr" ? "观察中" : "Manual");

  await openLocalMedia();
  createPeerConnection();
  await applyEncodingParameters();
  connectSignaling();
  startStatsLoop();
  setState("joining");
}

function leave() {
  send({ type: "leave", room });
  stopStatsLoop();
  stopPingLoop();

  if (dataChannel) {
    dataChannel.close();
    dataChannel = null;
  }

  if (peer) {
    peer.close();
    peer = null;
  }

  if (socket) {
    socket.close();
    socket = null;
  }

  if (localStream) {
    localStream.getTracks().forEach((track) => track.stop());
    localStream = null;
  }
  stopPattern();

  statsHistory = [];
  lastStats = { timestamp: 0, bytesSent: 0, bytesReceived: 0 };
  updateCharts({ sendKbps: 0, recvKbps: 0, targetKbps: 0, rttMs: 0, jitterMs: 0 });

  elements.localVideo.srcObject = null;
  elements.remoteVideo.srcObject = null;
  elements.joinButton.disabled = false;
  elements.leaveButton.disabled = true;
  elements.applyEncodingButton.disabled = true;
  elements.exportStatsButton.disabled = statsSamples.length === 0;
  elements.iceState.textContent = "-";
  latestDataRttMs = null;
  elements.dataRtt.textContent = "-";
  elements.targetBitrate.textContent = "-";
  updateAbrState("-");
  setState("idle");
  log("Left room");
}

elements.joinButton.addEventListener("click", () => {
  join().catch((error) => {
    log(error.message);
    leave();
  });
});

elements.leaveButton.addEventListener("click", leave);

function exportStats() {
  if (statsSamples.length === 0) {
    log("No stats samples to export yet");
    return;
  }

  const payload = {
    exportedAt: new Date().toISOString(),
    room,
    clientId,
    source: elements.sourceSelect.value,
    resolutionSetting: elements.resolutionSelect.value,
    fpsSetting: Number(elements.fpsSelect.value),
    targetBitrateKbps,
    sampleIntervalMs: 1000,
    samples: statsSamples
  };
  const blob = new Blob([JSON.stringify(payload, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  const safeRoom = String(room || "lab").replace(/[^a-z0-9_-]+/gi, "-").replace(/^-|-$/g, "") || "lab";
  link.href = url;
  link.download = `webrtc-stats-${safeRoom}-${Date.now()}.json`;
  document.body.appendChild(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(url);
  log(`Exported ${statsSamples.length} stats samples`);
}

elements.exportStatsButton.addEventListener("click", exportStats);
elements.applyEncodingButton.addEventListener("click", () => {
  applyEncodingParameters("manual-button").catch((error) => log(`Encoding apply failed: ${error.message}`));
});

elements.strategySelect.addEventListener("change", () => {
  stableAbrSamples = 0;
  lastAbrActionAt = 0;
  lastPacketLoss = 0;
  updateAbrState(elements.strategySelect.value === "simple-abr" ? "观察中" : "Manual");
  log(`Strategy changed: ${elements.strategySelect.value}`);
});




