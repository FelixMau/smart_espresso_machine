#ifndef DASHBOARD_H
#define DASHBOARD_H

// ============================================================================
// WEB DASHBOARD PAGE (served from PROGMEM at "/")
// ============================================================================
// Single-page dashboard: live tiles, start/stop, PID tuning sliders, goal
// weight / offset / pressure profile editors, live Chart.js plots and shot
// history. Chart.js is loaded from CDN (plan decision) - the page needs
// internet access on the client; the ESP itself never fetches it.
// Polls GET /state every 500 ms; history via /shots and /shot?id=N.

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Espresso Dashboard</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.9/dist/chart.umd.min.js"></script>
<style>
  :root {
    --page: #f9f9f7; --surface: #fcfcfb; --ink: #0b0b0b; --ink2: #52514e;
    --muted: #898781; --grid: #e1e0d9; --border: rgba(11,11,11,0.10);
    --accent: #2a78d6; --good: #0ca30c; --critical: #d03b3b;
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --page: #0d0d0d; --surface: #1a1a19; --ink: #ffffff; --ink2: #c3c2b7;
      --muted: #898781; --grid: #2c2c2a; --border: rgba(255,255,255,0.10);
      --accent: #3987e5;
    }
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; background: var(--page); color: var(--ink);
    font-family: system-ui, -apple-system, "Segoe UI", sans-serif;
  }
  header {
    display: flex; align-items: baseline; gap: 12px;
    padding: 14px 20px; border-bottom: 1px solid var(--border);
  }
  header h1 { font-size: 18px; margin: 0; }
  #conn { font-size: 13px; color: var(--muted); }
  #conn.ok { color: var(--good); }
  #conn.bad { color: var(--critical); }
  main { max-width: 1100px; margin: 0 auto; padding: 16px; }
  .cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 10px; }
  .card {
    background: var(--surface); border: 1px solid var(--border);
    border-radius: 10px; padding: 12px 14px;
  }
  .card .label { font-size: 12px; color: var(--muted); margin-bottom: 4px; }
  .card .value { font-size: 26px; font-weight: 600; }
  .card .sub { font-size: 12px; color: var(--ink2); margin-top: 2px; }
  section { margin-top: 20px; }
  section h2 { font-size: 14px; color: var(--ink2); margin: 0 0 10px; text-transform: uppercase; letter-spacing: 0.04em; }
  .panel { background: var(--surface); border: 1px solid var(--border); border-radius: 10px; padding: 14px; }
  .row { display: flex; flex-wrap: wrap; gap: 14px; align-items: center; }
  button {
    font: inherit; border: 1px solid var(--border); border-radius: 8px;
    padding: 10px 18px; cursor: pointer; background: var(--surface); color: var(--ink);
  }
  button.primary { background: var(--accent); border-color: var(--accent); color: #fff; }
  button.danger { background: var(--critical); border-color: var(--critical); color: #fff; }
  button:active { opacity: 0.8; }
  button:disabled { opacity: 0.4; cursor: not-allowed; }
  .value.ok { color: var(--good); }
  .value.bad { color: var(--critical); }
  input[type=number], input[type=text] {
    font: inherit; padding: 8px; border: 1px solid var(--border); border-radius: 8px;
    background: var(--page); color: var(--ink); width: 110px;
  }
  input.wide { width: 220px; }
  input[type=range] { width: 180px; accent-color: var(--accent); }
  .field { display: flex; flex-direction: column; gap: 4px; }
  .field label { font-size: 12px; color: var(--muted); }
  .slider-val { font-size: 13px; color: var(--ink2); min-width: 48px; display: inline-block; }
  .chartbox { position: relative; height: 220px; }
  table { border-collapse: collapse; width: 100%; font-size: 14px; }
  th, td { text-align: left; padding: 6px 10px; border-bottom: 1px solid var(--grid); }
  th { color: var(--muted); font-weight: 500; font-size: 12px; }
  td { color: var(--ink2); font-variant-numeric: tabular-nums; }
  .swatch { display: inline-block; width: 10px; height: 10px; border-radius: 3px; margin-right: 6px; }
  #histEmpty { color: var(--muted); font-size: 13px; }
</style>
</head>
<body>
<header>
  <h1>Espresso Dashboard</h1>
  <span id="conn">connecting…</span>
</header>
<main>

<div class="cards">
  <div class="card"><div class="label">State</div><div class="value" id="tState">–</div></div>
  <div class="card"><div class="label">Scale</div><div class="value" id="tScale">–</div></div>
  <div class="card"><div class="label">Shot time</div><div class="value" id="tTime">–</div><div class="sub">expected end <span id="tEnd">–</span> s</div></div>
  <div class="card"><div class="label">Weight</div><div class="value" id="tWeight">–</div><div class="sub">goal <span id="tGoalW">–</span> g − <span id="tOffset">–</span> g offset</div></div>
  <div class="card"><div class="label">Pressure</div><div class="value" id="tPressure">–</div><div class="sub">goal <span id="tGoalP">–</span> bar</div></div>
  <div class="card"><div class="label">Pump power</div><div class="value" id="tPump">–</div><div class="sub">PWM <span id="tPwm">–</span>/255</div></div>
  <div class="card"><div class="label">PID</div><div class="value" id="tPidOut">–</div><div class="sub">P <span id="tP">–</span> · I <span id="tI">–</span> · D <span id="tD">–</span></div></div>
</div>

<section>
  <h2>Shot control</h2>
  <div class="panel row">
    <button class="primary" id="startBtn" onclick="fetch('/start_shot')">Start shot</button>
    <button class="danger" onclick="fetch('/stop_shot')">Stop shot</button>
    <button onclick="fetch('/reset_shot')" title="Stops the shot on ESP and scale only - does not press the machine button">Reset (ESP only)</button>
    <div class="field">
      <label>Goal weight (g)</label>
      <div class="row" style="gap:6px">
        <input type="number" id="goalW" min="10" max="200" step="0.5">
        <button onclick="setGoalWeight()">Set</button>
      </div>
    </div>
    <div class="field">
      <label>Weight offset (g): <span class="slider-val" id="offsetVal">–</span></label>
      <input type="range" id="offset" min="0" max="5" step="0.1"
             oninput="offsetVal.textContent=this.value"
             onchange="fetch('/set_weight_offset?value='+this.value)">
    </div>
  </div>
</section>

<section>
  <h2>Cleaning cycle (detergent backflush)</h2>
  <div class="panel">
    <div class="row" style="margin-bottom:10px">
      <button class="primary" id="cleanStartBtn" onclick="fetch('/start_cleaning')"
              title="Put detergent in the blind basket and lock in the portafilter first">Start cleaning</button>
      <button id="cleanContinueBtn" onclick="fetch('/continue_cleaning')" disabled
              title="Confirm after rinsing the blind basket - starts the water flushes">Confirm rinse</button>
      <button class="danger" onclick="fetch('/stop_cleaning')">Abort</button>
      <span id="cleanStatus" style="font-size:14px;color:var(--ink2)">Idle. Add detergent to the blind basket, lock in, then start.</span>
    </div>
    <div class="row">
      <div class="field">
        <label>Max pressure (bar)</label>
        <input type="number" id="clMaxP" min="4" max="12" step="0.5">
      </div>
      <div class="field">
        <label>Flushes per phase</label>
        <input type="number" id="clCycles" min="1" max="10" step="1">
      </div>
      <div class="field">
        <label>Hold at max (s)</label>
        <input type="number" id="clHold" min="0" max="30" step="1">
      </div>
      <div class="field">
        <label>Pause between (s)</label>
        <input type="number" id="clPause" min="2" max="60" step="1">
      </div>
      <div class="field">
        <label>Soak (s)</label>
        <input type="number" id="clSoak" min="0" max="600" step="10">
      </div>
      <button onclick="setCleaning()">Set</button>
    </div>
  </div>
</section>

<section>
  <h2>PID tuning (live)</h2>
  <div class="panel row">
    <div class="field">
      <label>Kp: <span class="slider-val" id="kpVal">–</span></label>
      <input type="range" id="kp" min="0" max="100" step="0.5" oninput="pidChanged()">
    </div>
    <div class="field">
      <label>Ki: <span class="slider-val" id="kiVal">–</span></label>
      <input type="range" id="ki" min="0" max="5" step="0.05" oninput="pidChanged()">
    </div>
    <div class="field">
      <label>Kd: <span class="slider-val" id="kdVal">–</span></label>
      <input type="range" id="kd" min="0" max="50" step="0.5" oninput="pidChanged()">
    </div>
  </div>
</section>

<section>
  <h2>Pressure profile</h2>
  <div class="panel row">
    <div class="field">
      <label>Times (s, comma separated; negative = seconds before expected end)</label>
      <input type="text" class="wide" id="profTimes">
    </div>
    <div class="field">
      <label>Pressures (bar, comma separated)</label>
      <input type="text" class="wide" id="profPressures">
    </div>
    <button onclick="setProfile()">Set profile</button>
  </div>
</section>

<section>
  <h2>Live shot</h2>
  <div class="panel">
    <div class="chartbox"><canvas id="weightChart"></canvas></div>
    <div class="chartbox" style="margin-top:10px"><canvas id="pressureChart"></canvas></div>
  </div>
</section>

<section>
  <h2>Shot history (last 5, lost on reboot)</h2>
  <div class="panel">
    <div id="histEmpty">No shots recorded yet.</div>
    <table id="histTable" style="display:none">
      <thead><tr><th>Shot</th><th>Duration (s)</th><th>Final weight (g)</th><th>Peak pressure (bar)</th><th>Ended by</th></tr></thead>
      <tbody id="histBody"></tbody>
    </table>
    <div class="chartbox" style="margin-top:10px"><canvas id="histChart"></canvas></div>
  </div>
</section>

</main>
<script>
'use strict';
const dark = matchMedia('(prefers-color-scheme: dark)').matches;
// Validated categorical palette (dataviz reference instance), stepped per mode
const SERIES = dark
  ? ['#3987e5', '#199e70', '#c98500', '#008300', '#9085e9']
  : ['#2a78d6', '#1baf7a', '#eda100', '#008300', '#4a3aa7'];
const INK = { muted: '#898781', grid: dark ? '#2c2c2a' : '#e1e0d9',
              weight: dark ? '#3987e5' : '#2a78d6', pressure: '#008300' };

Chart.defaults.color = INK.muted;
Chart.defaults.borderColor = INK.grid;
Chart.defaults.font.family = 'system-ui, -apple-system, "Segoe UI", sans-serif';

const liveW = [], liveP = [], livePG = [];

function lineOpts(yTitle) {
  return {
    responsive: true, maintainAspectRatio: false, animation: false,
    interaction: { mode: 'nearest', intersect: false },
    scales: {
      x: { type: 'linear', title: { display: true, text: 'time (s)' }, grid: { color: INK.grid } },
      y: { title: { display: true, text: yTitle }, grid: { color: INK.grid }, beginAtZero: true }
    },
    plugins: { legend: { display: true, labels: { boxWidth: 12, boxHeight: 12 } } }
  };
}

const weightChart = new Chart(document.getElementById('weightChart'), {
  type: 'line',
  data: { datasets: [
    { label: 'Weight (g)', data: liveW, borderColor: INK.weight, borderWidth: 2,
      pointRadius: 0, tension: 0.2 }
  ]},
  options: (() => { const o = lineOpts('g'); o.plugins.legend.display = false; return o; })()
});

const pressureChart = new Chart(document.getElementById('pressureChart'), {
  type: 'line',
  data: { datasets: [
    { label: 'Pressure (bar)', data: liveP, borderColor: INK.pressure, borderWidth: 2,
      pointRadius: 0, tension: 0.2 },
    { label: 'Goal (bar)', data: livePG, borderColor: INK.muted, borderWidth: 2,
      borderDash: [6, 4], pointRadius: 0, stepped: true }
  ]},
  options: lineOpts('bar')
});

const histChart = new Chart(document.getElementById('histChart'), {
  type: 'line', data: { datasets: [] }, options: lineOpts('g')
});

// --- PID sliders -----------------------------------------------------------
let pidTimer = null, initialized = false;
function pidChanged() {
  kpVal.textContent = kp.value; kiVal.textContent = ki.value; kdVal.textContent = kd.value;
  clearTimeout(pidTimer);
  pidTimer = setTimeout(() =>
    fetch(`/set_pid?kp=${kp.value}&ki=${ki.value}&kd=${kd.value}`), 200);
}

function setGoalWeight() { fetch('/set_goal_weight?value=' + goalW.value); }
function setCleaning() {
  fetch(`/set_cleaning?max_pressure=${clMaxP.value}&cycles=${clCycles.value}` +
        `&hold_s=${clHold.value}&pause_s=${clPause.value}&soak_s=${clSoak.value}`);
}
function cleanStatusText(c) {
  if (!c.active) {
    return c.phase === 'done' ? 'Cleaning complete.'
         : 'Idle. Add detergent to the blind basket, lock in, then start.';
  }
  if (c.phase === 'soak') {
    return `Soaking detergent… ${Math.max(0, c.soakS - c.elapsed).toFixed(0)} s left`;
  }
  if (c.phase === 'await_rinse') {
    return 'Remove portafilter, rinse blind basket and group, lock back in, then press Confirm rinse.';
  }
  const ph = c.phase === 'detergent' ? 'Detergent' : 'Rinse';
  let txt = `${ph} flush ${c.cycle}/${c.cycles}: `;
  if (c.state === 'pressurize') txt += 'pressurizing…';
  else if (c.state === 'hold') txt += `holding at ${c.maxPressure.toFixed(1)} bar`;
  else txt += 'released - venting through valve';
  if (c.lastFillPeak > 0) {
    txt += ` (last peak ${c.lastFillPeak.toFixed(1)} bar${c.lastFillReachedMax ? '' : ' ⚠ max never reached - blind basket inserted?'})`;
  }
  return txt;
}
function setProfile() {
  fetch('/set_pressure_profile?times=' + encodeURIComponent(profTimes.value)
      + '&pressures=' + encodeURIComponent(profPressures.value));
}

// --- Shot history ----------------------------------------------------------
async function loadHistory() {
  try {
    const data = await (await fetch('/shots')).json();
    const shots = data.shots || [];
    histEmpty.style.display = shots.length ? 'none' : '';
    histTable.style.display = shots.length ? '' : 'none';
    histBody.innerHTML = '';
    const datasets = [];
    for (const s of shots) {
      const color = SERIES[(s.id - 1) % SERIES.length]; // color follows the shot, not its rank
      histBody.insertAdjacentHTML('beforeend',
        `<tr><td><span class="swatch" style="background:${color}"></span>#${s.id}</td>` +
        `<td>${s.duration.toFixed(1)}</td><td>${s.finalWeight.toFixed(1)}</td>` +
        `<td>${s.peakPressure.toFixed(1)}</td><td>${s.endReason}</td></tr>`);
      const traj = await (await fetch('/shot?id=' + s.id)).json();
      datasets.push({
        label: 'Shot #' + s.id,
        data: traj.t.map((t, i) => ({ x: t, y: traj.w[i] })),
        borderColor: color, borderWidth: 2, pointRadius: 0, tension: 0.2
      });
    }
    histChart.data.datasets = datasets;
    histChart.update();
  } catch (e) { /* keep old view on transient errors */ }
}

// --- State polling ---------------------------------------------------------
let brewing = false;
const txt = (id, v) => document.getElementById(id).textContent = v;

async function poll() {
  try {
    const s = await (await fetch('/state')).json();
    document.getElementById('conn').textContent = 'connected';
    document.getElementById('conn').className = 'ok';

    const c = s.cleaning || {};
    txt('tState', c.active ? 'Cleaning' : (s.brewing ? 'Brewing' : 'Idle'));
    cleanStartBtn.disabled = !!c.active || s.brewing;
    cleanContinueBtn.disabled = c.phase !== 'await_rinse';
    document.getElementById('cleanStatus').textContent = cleanStatusText(c);
    txt('tScale', s.scaleConnected ? 'Connected' : 'Offline');
    document.getElementById('tScale').className = 'value ' + (s.scaleConnected ? 'ok' : 'bad');
    startBtn.disabled = !s.scaleConnected;  // no shots without a scale
    txt('tTime', s.shotTimer.toFixed(1) + ' s');
    txt('tEnd', s.expectedEnd.toFixed(1));
    txt('tWeight', s.weight.toFixed(1) + ' g');
    txt('tGoalW', s.goalWeight.toFixed(1));
    txt('tOffset', s.weightOffset.toFixed(1));
    txt('tPressure', s.pressure.toFixed(1) + ' bar');
    txt('tGoalP', s.goalPressure.toFixed(1));
    txt('tPump', Math.round(s.pumpPwm / 255 * 100) + '%');
    txt('tPwm', s.pumpPwm);
    if (s.pid) {
      txt('tPidOut', s.pid.out);
      txt('tP', s.pid.p.toFixed(1)); txt('tI', s.pid.i.toFixed(1)); txt('tD', s.pid.d.toFixed(1));
    }

    // Populate editors once, so polling doesn't fight the user's edits
    if (!initialized) {
      initialized = true;
      goalW.value = s.goalWeight;
      offset.value = s.weightOffset; offsetVal.textContent = s.weightOffset.toFixed(1);
      if (s.pid) {
        kp.value = s.pid.kp; ki.value = s.pid.ki; kd.value = s.pid.kd;
        kpVal.textContent = s.pid.kp; kiVal.textContent = s.pid.ki; kdVal.textContent = s.pid.kd;
      }
      profTimes.value = (s.profileTimes || []).join(',');
      profPressures.value = (s.profilePressures || []).join(',');
      clMaxP.value = c.maxPressure; clCycles.value = c.cycles;
      clHold.value = c.holdS; clPause.value = c.pauseS; clSoak.value = c.soakS;
      loadHistory();
    }

    if (s.brewing && !brewing) {          // shot started: clear live plots
      liveW.length = liveP.length = livePG.length = 0;
    }
    if (!s.brewing && brewing) {          // shot ended: refresh history
      setTimeout(loadHistory, 1000);
    }
    brewing = s.brewing;

    if (brewing) {
      liveW.push({ x: s.shotTimer, y: s.weight });
      liveP.push({ x: s.shotTimer, y: s.pressure });
      livePG.push({ x: s.shotTimer, y: s.goalPressure });
      weightChart.update('none');
      pressureChart.update('none');
    }
  } catch (e) {
    document.getElementById('conn').textContent = 'disconnected';
    document.getElementById('conn').className = 'bad';
  }
  setTimeout(poll, 500);
}
poll();
</script>
</body>
</html>
)rawliteral";

#endif // DASHBOARD_H
