(() => {
  "use strict";

  const S = {
    meta: null,
    current: null,
    events: [],
    ws: null,
    wsGameId: null,
    replay: null,
    replayIndex: 0,
    replayTimer: null,
    lastAdvice: null,
  };

  const $ = (id) => document.getElementById(id);

  async function api(path, options = {}) {
    const res = await fetch(path, {
      headers: { "Content-Type": "application/json" },
      ...options,
    });
    if (!res.ok) {
      let detail = res.statusText;
      try {
        const body = await res.json();
        detail = body.detail || JSON.stringify(body);
      } catch (_) { /* ignore */ }
      throw new Error(detail);
    }
    return res.json();
  }

  function esc(s) {
    return String(s ?? "").replace(/[&<>"']/g, (c) => ({
      "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
    }[c]));
  }

  function tileHTML(code, cls = "") {
    const info = (S.meta && S.meta.tiles && S.meta.tiles[code]) || { name: code, color: "" };
    return `<span class="tile ${info.color || ""} ${cls}" title="${esc(code)}">${esc(info.name)}</span>`;
  }

  function backHTML(small = "") {
    return `<span class="tile-back ${small}" title="未公开">牌</span>`;
  }

  function playerHTML(p, st, opts = {}) {
    const isCurrent = p.seat === st.current_player;
    const reveal = opts.replay || $("revealAll").checked || p.seat === st.current_player;
    const hand = p.hand || [];
    let handHTML = "";
    if (reveal) {
      handHTML = hand.map((t) => tileHTML(t, "small")).join("");
    } else if (hand.length) {
      handHTML = Array.from({ length: Math.min(hand.length, 14) }, () => backHTML("small")).join("");
    } else {
      handHTML = `<span class="hint">未录入手牌</span>`;
    }
    const melds = (p.melds || []).map((m) => {
      const tiles = (m.tiles || []).map((t) => tileHTML(t, "small")).join("");
      return `<div class="meld" title="${esc(m.type)}">${tiles}</div>`;
    }).join("");
    const river = (p.discards || []).map((t) => tileHTML(t, "small")).join("");
    return `
      <div class="player p${p.seat} ${isCurrent ? "current" : ""}">
        <div class="player-head">
          <span class="player-name">${esc(p.name)}</span>
          <span class="player-wind">${esc(windName(p.wind))} · ${p.score ?? 0} 分</span>
        </div>
        <div class="hand">${handHTML}</div>
        ${melds ? `<div class="melds">${melds}</div>` : ""}
        <div class="river">${river}</div>
      </div>`;
  }

  function windName(w) {
    return ({ EA: "东", SO: "南", WE: "西", NO: "北" })[w] || w || "";
  }

  function boardHTML(st, opts = {}) {
    if (!st) return `<div class="empty-state">还没有牌局。</div>`;
    const players = st.players || [];
    const cur = players[st.current_player] || {};
    const last = st.last_discard;
    return `
      <div class="board-grid">
        ${players.map((p) => playerHTML(p, st, opts)).join("")}
        <div class="center">
          <div class="wall">${st.wall_remaining ?? "—"}</div>
          <div class="label">牌墙剩余</div>
          <div class="label">第 ${st.turn ?? 0} 巡 · 当前：${esc(cur.name || "—")}</div>
          <div class="last">
            <span class="label">最近出牌</span>
            ${last ? tileHTML(last.tile) : `<span class="label">—</span>`}
          </div>
          ${st.status === "finished" ? `<div class="badge warn">已结束 · 赢家 ${esc((players[st.winner] || {}).name || "")}</div>` : ""}
        </div>
      </div>`;
  }

  function render(payload) {
    if (!payload || !payload.state) return;
    S.current = payload;
    const st = payload.state;
    $("gameTitle").textContent = payload.name || "牌局";
    $("gameMeta").textContent =
      `第 ${st.turn ?? 0} 巡 · 牌墙 ${st.wall_remaining ?? "—"} · ` +
      `当前 ${((st.players || [])[st.current_player] || {}).name || "—"} · ` +
      `事件 ${payload.version ?? 0} 条`;
    $("board").innerHTML = boardHTML(st);
    renderActions(payload.legal_actions || []);
    updatePlayerOptions(st);
    renderGameListActive();
    renderTop3(st.last_recognition);
  }

  function renderTop3(rec) {
    const box = $("top3Box");
    if (!rec || !(rec.top3 || []).length) {
      box.innerHTML = `<span class="hint">识别后显示 top-3</span>`;
      return;
    }
    box.innerHTML = rec.top3.slice(0, 3).map((x, i) => {
      const pct = x.confidence == null ? "" : `${Math.round(Number(x.confidence) * 100)}%`;
      return `<span class="top3-item">${i + 1} ${tileHTML(x.tile, "small")} ${pct}</span>`;
    }).join("");
  }

  function updatePlayerOptions(st) {
    const sel = $("playerSelect");
    const players = st.players || [];
    const current = sel.value;
    const userPicked = sel.dataset.user === "1";
    sel.innerHTML = players.map((p) =>
      `<option value="${p.seat}">${p.seat} · ${esc(p.name)}</option>`).join("");
    sel.value = userPicked && current && players[Number(current)]
      ? current
      : String(st.current_player ?? 0);
  }

  function renderActions(actions) {
    const box = $("actionButtons");
    if (!actions.length) {
      box.innerHTML = `<p class="hint">等待其他玩家操作，或先录入手牌。</p>`;
      return;
    }
    const labels = { draw: "摸牌", discard: "出牌", peng: "碰", chi: "吃", gang: "杠", win: "胡", pass: "过" };
    box.innerHTML = actions.map((a) => {
      const label = a.label || labels[a.type] || a.type;
      return `<button class="btn" data-action="${esc(a.type)}">${esc(label)}</button>`;
    }).join("");
    box.querySelectorAll("button[data-action]").forEach((btn) => {
      btn.addEventListener("click", () => doAction(btn.dataset.action));
    });
  }

  async function doAction(type) {
    if (!S.current) return;
    const player = Number($("playerSelect").value || 0);
    const raw = $("tileInput").value.trim();
    const tile = raw.split(/[\s,，]+/)[0] || null;
    const body = { type, player, source: "ui" };
    if (type === "chi") {
      const last = S.current.state.last_discard;
      body.tile = last ? last.tile : tile;
      body.tiles = raw.split(/[\s,，]+/).filter(Boolean).slice(0, 2);
    } else if (type !== "pass") {
      body.tile = tile;
    }
    try {
      const payload = await api(`/api/games/${S.current.id}/events`, {
        method: "POST", body: JSON.stringify(body),
      });
      render(payload);
      await refreshEvents();
    } catch (err) {
      alert("操作失败：" + err.message);
    }
  }

  async function refreshEvents() {
    if (!S.current) return;
    try {
      const data = await api(`/api/games/${S.current.id}/events`);
      S.events = data.events || [];
      $("eventCount").textContent = `${data.count || 0} 条`;
      $("eventLog").innerHTML = S.events.slice(-30).reverse().map((e) => `
        <div class="event-item">
          <span class="seq">#${e.seq}</span>
          <span class="tag">${esc(e.source || "manual")}</span>
          <span>${esc(e.label)}</span>
        </div>`).join("") || `<p class="hint">还没有事件。</p>`;
    } catch (_) { /* ignore */ }
  }

  async function loadGames() {
    const games = await api("/api/games");
    const box = $("gameList");
    if (!games.length) {
      box.innerHTML = `<p class="hint">还没有牌局。点击“新建”或右上角“生成演示牌局”。</p>`;
      return;
    }
    box.innerHTML = games.map((g) => `
      <div class="game-item ${S.current && S.current.id === g.id ? "active" : ""}" data-id="${g.id}">
        <div class="name">${esc(g.name)}</div>
        <div class="sub">${g.status} · ${g.turn ?? 0} 巡 · 更新 ${esc((g.updated_at || "").slice(0, 19).replace("T", " "))}</div>
      </div>`).join("");
    box.querySelectorAll(".game-item").forEach((el) => {
      el.addEventListener("click", () => selectGame(el.dataset.id).catch((e) => alert(e.message)));
    });
    if (!S.current && games.length) selectGame(games[0].id).catch(() => {});
  }

  function renderGameListActive() {
    document.querySelectorAll(".game-item").forEach((el) => {
      el.classList.toggle("active", !!S.current && el.dataset.id === S.current.id);
    });
  }

  async function selectGame(id) {
    const payload = await api(`/api/games/${id}`);
    $("playerSelect").dataset.user = "0";
    render(payload);
    await refreshEvents();
    connectWS(id);
    $("handInput").value = (payload.state.players[Number($("playerSelect").value)] || {}).hand?.join(" ") || "";
  }

  function connectWS(gameId) {
    if (S.ws) {
      S.ws.onclose = null;
      S.ws.close();
    }
    S.wsGameId = gameId;
    const proto = location.protocol === "https:" ? "wss:" : "ws:";
    const ws = new WebSocket(`${proto}//${location.host}/ws/games/${gameId}`);
    S.ws = ws;
    ws.onmessage = (ev) => {
      let msg;
      try { msg = JSON.parse(ev.data); } catch (_) { return; }
      if (msg.type === "state" || msg.type === "recognition") {
        if (msg.data && S.current && msg.data.id === S.current.id) {
          render(msg.data);
          refreshEvents();
          if (msg.type === "recognition" && msg.data.advice) renderAdvice(msg.data.advice);
        }
      }
      if (msg.type === "deleted" && S.current && msg.game_id === S.current.id) {
        S.current = null;
        location.reload();
      }
    };
    ws.onclose = () => {
      if (S.wsGameId === gameId && S.current && S.current.id === gameId) {
        setTimeout(() => connectWS(gameId), 3000);
      }
    };
  }

  async function recordRecognition() {
    if (!S.current) return;
    const tile = $("tileInput").value.trim();
    if (!tile) { alert("请先输入识别到的牌"); return; }
    const player = Number($("playerSelect").value || 0);
    const confidence = Number($("confidenceInput").value || 0);
    try {
      const payload = await api(`/api/games/${S.current.id}/recognitions`, {
        method: "POST",
        body: JSON.stringify({ player, tile, confidence, top3: [{ tile, confidence }], source: "ui" }),
      });
      render(payload);
      if (payload.advice) renderAdvice(payload.advice);
      await refreshEvents();
    } catch (err) { alert("识别上报失败：" + err.message); }
  }

  async function saveHand() {
    if (!S.current) return;
    const tiles = $("handInput").value.split(/[\s,，]+/).filter(Boolean);
    if (!tiles.length) { alert("请先输入手牌"); return; }
    const player = Number($("playerSelect").value || 0);
    try {
      const payload = await api(`/api/games/${S.current.id}/hands/${player}`, {
        method: "POST", body: JSON.stringify({ tiles, source: "manual" }),
      });
      render(payload);
      await refreshEvents();
    } catch (err) { alert("保存手牌失败：" + err.message); }
  }

  async function getAdvice() {
    if (!S.current) return;
    const player = Number($("playerSelect").value || 0);
    $("adviceBox").innerHTML = `<p class="hint">正在分析…</p>`;
    try {
      const a = await api(`/api/games/${S.current.id}/advice`, {
        method: "POST", body: JSON.stringify({ player, use_llm: true }),
      });
      renderAdvice(a);
    } catch (err) {
      $("adviceBox").innerHTML = `<p class="hint">建议获取失败：${esc(err.message)}</p>`;
    }
  }

  function renderAdvice(a) {
    S.lastAdvice = a;
    const badge = $("adviceProvider");
    const isLlm = String(a.provider || "").startsWith("llm");
    badge.textContent = isLlm ? "大模型" : "本地规则";
    badge.className = "badge " + (isLlm ? "ok" : "warn");
    const actionText = {
      discard: "建议出牌", draw: "建议摸牌", peng: "建议碰", chi: "建议吃",
      gang: "建议杠", win: "可以胡", pass: "建议过", none: "需要更多信息",
    }[a.action] || "建议";
    const tile = a.tile ? tileHTML(a.tile) : "";
    const alt = (a.alternatives || []).map((x) =>
      `<li>${esc(x.reason || (x.tile ? x.tile : ""))}</li>`).join("");
    const meta = [];
    if (a.shanten !== null && a.shanten !== undefined) meta.push(`向听 ${a.shanten}`);
    if (a.ukeire) meta.push(`进张 ${a.ukeire} 张`);
    if (a.latency_ms) meta.push(`${a.latency_ms} ms`);
    $("adviceBox").innerHTML = `
      <div class="advice-action">${esc(actionText)} ${tile}</div>
      <p class="advice-reason">${esc(a.reason || "")}</p>
      ${meta.length ? `<div class="advice-meta">${meta.join(" · ")}</div>` : ""}
      ${alt ? `<ul class="advice-alt">${alt}</ul>` : ""}
      ${a.needs_hand ? `<p class="hint">提示：先在左侧“手动录入手牌”里输入 13 或 14 张牌。</p>` : ""}
      ${a.llm_error ? `<p class="hint">大模型暂不可用，已使用本地规则：${esc(a.llm_error)}</p>` : ""}
    `;
  }

  function speakAdvice() {
    if (!S.lastAdvice) { alert("还没有建议"); return; }
    const text = S.lastAdvice.reason || "";
    if (!text || !("speechSynthesis" in window)) return;
    window.speechSynthesis.cancel();
    const u = new SpeechSynthesisUtterance(text);
    u.lang = "zh-CN";
    u.rate = 0.95;
    window.speechSynthesis.speak(u);
  }

  async function undo() {
    if (!S.current) return;
    if (!confirm("撤销最后一条事件？")) return;
    try {
      const payload = await api(`/api/games/${S.current.id}/undo`, { method: "POST" });
      render(payload);
      await refreshEvents();
    } catch (err) { alert("撤销失败：" + err.message); }
  }

  async function createGame(ev) {
    ev.preventDefault();
    const players = [0, 1, 2, 3].map((i) => $(`p${i}`).value.trim() || `玩家${i}`);
    const deal = $("deal").checked;
    const body = {
      name: $("newName").value.trim() || null,
      players,
      dealer: Number($("dealer").value),
      deal,
      strict_hands: deal ? $("strictHands").checked : false,
      strict_turn: deal ? $("strictTurn").checked : false,
    };
    try {
      const payload = await api("/api/games", { method: "POST", body: JSON.stringify(body) });
      $("createForm").hidden = true;
      await loadGames();
      await selectGame(payload.id);
    } catch (err) { alert("创建失败：" + err.message); }
  }

  async function createDemo() {
    try {
      const payload = await api("/api/games/demo", {
        method: "POST", body: JSON.stringify({ seed: Math.floor(Math.random() * 100000), turns: 16 }),
      });
      await loadGames();
      await selectGame(payload.id);
    } catch (err) { alert("生成演示失败：" + err.message); }
  }

  async function openReplay() {
    if (!S.current) { alert("请先选择牌局"); return; }
    try {
      S.replay = await api(`/api/games/${S.current.id}/replay`);
      S.replayIndex = 0;
      $("replayModal").hidden = false;
      const slider = $("replaySlider");
      slider.max = String(S.replay.snapshots.length - 1);
      slider.value = "0";
      renderReplay(0);
    } catch (err) { alert("复盘加载失败：" + err.message); }
  }

  function renderReplay(idx) {
    if (!S.replay) return;
    const snaps = S.replay.snapshots;
    idx = Math.max(0, Math.min(idx, snaps.length - 1));
    S.replayIndex = idx;
    const snap = snaps[idx];
    $("replayBoard").innerHTML = boardHTML(snap.state, { replay: true });
    $("replaySlider").value = String(idx);
    $("replayLabel").textContent = `#${snap.seq} ${snap.label}`;
  }

  function stopReplay() {
    if (S.replayTimer) { clearInterval(S.replayTimer); S.replayTimer = null; }
    $("btnReplayPlay").textContent = "播放";
  }

  function toggleReplay() {
    if (S.replayTimer) { stopReplay(); return; }
    if (!S.replay) return;
    $("btnReplayPlay").textContent = "暂停";
    S.replayTimer = setInterval(() => {
      if (S.replayIndex >= S.replay.snapshots.length - 1) { stopReplay(); return; }
      renderReplay(S.replayIndex + 1);
    }, 1200);
  }

  async function init() {
    try {
      const [meta, health] = await Promise.all([api("/api/meta/tiles"), api("/api/health")]);
      S.meta = meta;
      const list = $("tileList");
      list.innerHTML = (meta.order || []).map((code) => {
        const t = meta.tiles[code];
        return `<option value="${esc(code)}">${esc(t.name)}（${esc(code)}）</option>`;
      }).join("");
      const badge = $("llmBadge");
      badge.textContent = health.llm ? `大模型已接入 · ${health.llm_model}` : "本地规则模式（未配置 LLM）";
      badge.className = "badge " + (health.llm ? "ok" : "warn");
    } catch (err) {
      console.error(err);
    }
    await loadGames().catch(() => {});
  }

  // 事件绑定
  $("btnDemo").addEventListener("click", createDemo);
  $("btnNew").addEventListener("click", () => { $("createForm").hidden = !$("createForm").hidden; });
  $("btnCancelNew").addEventListener("click", () => { $("createForm").hidden = true; });
  $("createForm").addEventListener("submit", createGame);
  $("btnScan").addEventListener("click", recordRecognition);
  $("btnDiscard").addEventListener("click", () => doAction("discard"));
  $("btnHand").addEventListener("click", saveHand);
  $("btnAdvice").addEventListener("click", getAdvice);
  $("btnSpeak").addEventListener("click", speakAdvice);
  $("btnUndo").addEventListener("click", undo);
  $("revealAll").addEventListener("change", () => { if (S.current) render(S.current); });
  $("btnReplay").addEventListener("click", openReplay);
  $("btnCloseReplay").addEventListener("click", () => { stopReplay(); $("replayModal").hidden = true; });
  $("replaySlider").addEventListener("input", (e) => { stopReplay(); renderReplay(Number(e.target.value)); });
  $("btnReplayPrev").addEventListener("click", () => { stopReplay(); renderReplay(S.replayIndex - 1); });
  $("btnReplayNext").addEventListener("click", () => { stopReplay(); renderReplay(S.replayIndex + 1); });
  $("btnReplayPlay").addEventListener("click", toggleReplay);
  $("playerSelect").addEventListener("change", () => {
    $("playerSelect").dataset.user = "1";
  });

  init();
})();
