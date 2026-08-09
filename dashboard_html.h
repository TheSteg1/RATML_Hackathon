#pragma once
// Dashboard HTML/JS lives here, NOT in the .ino, on purpose: Arduino's
// ctags-based auto-prototype generator does not understand C++ raw string
// literals. It scans .ino files as plain text looking for
// "identifier(args){" patterns at the start of a line and does not skip
// over raw string bodies -- so every "function xyz(){" inside the embedded
// JS below gets misdetected as a real top-level C++ function, causing
// bogus "'function' does not name a type" errors. Arduino's auto-prototyping
// only scans .ino files, not headers pulled in via #include, so moving this
// block here sidesteps the problem entirely instead of trying to dodge it
// with specific character choices inside the string.

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><title>Mesh radar</title>
<style>
:root{
  --grn:#39ff88; --grn-dim:#1c8a4d; --grn-glow:rgba(57,255,136,.55);
  --bg:#010503; --panel:#050d09; --panel-line:#123322;
}
*{box-sizing:border-box}
html,body{height:100%;margin:0}
body{font-family:'Courier New',ui-monospace,monospace;background:var(--bg);color:var(--grn);overflow:hidden}
#app{display:flex;height:100vh;width:100vw}
#sidebar{
  width:320px;min-width:320px;height:100%;overflow-y:auto;
  background:var(--panel);border-right:1px solid var(--panel-line);
  padding:14px 14px 20px;
}
#sidebar h3{
  margin:0 0 12px;font-size:15px;letter-spacing:2px;text-transform:uppercase;
  color:var(--grn);text-shadow:0 0 8px var(--grn-glow);border-bottom:1px solid var(--panel-line);padding-bottom:8px;
}
.sec{margin-bottom:14px;padding-bottom:12px;border-bottom:1px solid var(--panel-line)}
.sec:last-child{border-bottom:none}
.sec-label{font-size:10px;letter-spacing:1.5px;text-transform:uppercase;color:var(--grn-dim);margin-bottom:6px}
#status{font-size:11px;color:var(--grn-dim);margin-top:4px;line-height:1.5}
select,input,button{
  background:#03140b;color:var(--grn);border:1px solid var(--panel-line);
  padding:4px 6px;font-family:inherit;font-size:12px;border-radius:2px;
}
button{cursor:pointer;color:#00140a;background:var(--grn);border:1px solid var(--grn);font-weight:bold;letter-spacing:.5px}
button:hover{background:#7dffb8}
label{margin:0 6px 0 0;font-size:12px;display:inline-block}
.row{margin-bottom:6px}
.hint{color:var(--grn-dim);font-size:10.5px;line-height:1.5;margin-bottom:6px}
#calibPoints,#offsetList{margin-top:4px;color:var(--grn-dim);font-size:11px}

#radarWrap{flex:1;height:100%;display:flex;align-items:center;justify-content:center;background:radial-gradient(circle at center,#020e07 0%,#010603 60%,#000 100%);position:relative}
#radarStage{position:relative}
canvas{display:block;border-radius:50%;box-shadow:0 0 0 2px var(--panel-line),0 0 40px rgba(57,255,136,.18) inset,0 0 70px rgba(57,255,136,.12)}
#sweep{
  position:absolute;top:0;left:0;pointer-events:none;border-radius:50%;
  background:conic-gradient(from 0deg,var(--grn-glow) 0deg,rgba(57,255,136,0) 55deg,rgba(57,255,136,0) 360deg);
  mix-blend-mode:screen;animation:spin 4.5s linear infinite;opacity:.55;
}
@keyframes spin{from{transform:rotate(0deg)}to{transform:rotate(360deg)}}
@media (max-width:700px){
  #app{flex-direction:column}
  #sidebar{width:100%;min-width:0;height:42vh;border-right:none;border-bottom:1px solid var(--panel-line)}
  #radarWrap{height:58vh}
}
</style></head><body>
<div id="app">
<aside id="sidebar">
  <h3>&#9679; Mesh Radar</h3>

  <div class="sec">
    <div class="sec-label">View</div>
    <div class="row">
      <button id="zoomOutBtn" style="width:32px">&minus;</button>
      <input id="zoomSlider" type="range" min="0.3" max="6" step="0.1" value="1" style="width:130px;vertical-align:middle">
      <button id="zoomInBtn" style="width:32px">+</button>
    </div>
    <div class="row">
      <button id="zoomFitBtn">Fit to nodes</button>
      <span id="zoomLabel" style="color:var(--grn-dim);font-size:11px;margin-left:4px">1.0x</span>
    </div>
  </div>

  <div class="sec">
    <div class="sec-label">Orient using</div>
    <select id="selfPicker" style="width:100%"><option value="">(none)</option></select>
  </div>

  <div class="sec">
    <div class="sec-label">Distance model</div>
    <div class="row"><label>RSSI @1m</label><input id="rssiRef" type="number" value="-40" step="1" style="width:60px"></div>
    <div class="row"><label>Path-loss n</label><input id="pathLoss" type="number" value="2.5" step="0.1" style="width:60px"></div>
    <div class="row"><label><input id="showDist" type="checkbox" checked style="width:auto"> show distances (m)</label></div>
  </div>

  <div class="sec">
    <div class="sec-label">Calibrate from fixed pair</div>
    <div class="row">
      <select id="calibA" style="width:47%"></select> &harr; <select id="calibB" style="width:47%"></select>
    </div>
    <div class="row"><input id="calibDist" type="number" value="2" step="0.1" style="width:50px">m apart</div>
    <div class="row">
      <button id="calibAddBtn">Add point</button>
      <button id="calibFitBtn">Fit</button>
      <button id="calibClearBtn">Clear</button>
    </div>
    <div id="calibPoints">No points collected yet.</div>
  </div>

  <div class="sec" id="nodeCalib">
    <div class="sec-label">Per-board TX power calibration</div>
    <div class="hint">
      Boards can transmit at slightly different power, which skews distance readings for that
      board even after the curve above is fit. Hold two boards touching and measure one against
      the other to correct for it.
    </div>
    <div class="row">Reference (assumed 0dB)<br><select id="offsetRef" style="width:100%"></select></div>
    <div class="row">Board being calibrated<br><select id="offsetTarget" style="width:100%"></select></div>
    <div class="row"><input id="offsetDist" type="number" value="0.1" step="0.05" style="width:50px">m apart</div>
    <div class="row">
      <button id="offsetMeasureBtn">Measure offset</button>
      <button id="offsetClearBtn">Clear all</button>
    </div>
    <div id="offsetList">No per-board offsets set.</div>
  </div>

  <div class="sec">
    <div class="sec-label">Status</div>
    <div id="status">waiting for data...</div>
  </div>
</aside>

<main id="radarWrap">
  <div id="radarStage">
    <canvas id="c" width="380" height="380"></canvas>
    <div id="sweep"></div>
  </div>
</main>
</div>
<script>
let RSSI_REF = -40.0, PATH_LOSS_N = 2.5;
let lastState = null;
document.getElementById('rssiRef').addEventListener('input', e=>{ RSSI_REF = parseFloat(e.target.value); if(isNaN(RSSI_REF)) RSSI_REF=-40; });
document.getElementById('pathLoss').addEventListener('input', e=>{ PATH_LOSS_N = parseFloat(e.target.value); if(isNaN(PATH_LOSS_N)) PATH_LOSS_N=2.5; });
function rssiToDist(rssi){ return Math.pow(10, (RSSI_REF - rssi) / (10*PATH_LOSS_N)); }

// N-point calibration: move the fixed pair to a distance, click "Add
// point", move it again, "Add point" again, etc, then "Fit". Each point is
// (log10(dist), avg rssi over that pairs readings). Since
//   rssi = RSSI_REF - 10*n*log10(dist)
// is linear in x=log10(dist) with y=rssi, a=RSSI_REF, b=-10*n, 2+ points
// get an ordinary least-squares fit of both unknowns at once -- no need to
// hardcode two-point logic, a 3rd/4th boards point just joins the same fit.
// With only ONE point collected, that is 1 equation for 2 unknowns, so this
// instead holds PATH_LOSS_N at its current value and solves RSSI_REF
// exactly for that single point (tune PATH_LOSS_N by eye afterwards, or add
// a second point later to fit it properly).
let calibPoints = []; // {dist, rssi}

// Canvas fills whatever space #radarWrap has, staying square (capped so it
// never overflows the shorter of the wrap's width/height), and the sweep
// overlay is kept in lockstep so it always exactly covers the circle.
function resizeRadar(){
  const wrap = document.getElementById('radarWrap');
  const size = Math.floor(Math.min(wrap.clientWidth, wrap.clientHeight) - 24);
  const canvas = document.getElementById('c');
  canvas.width = size; canvas.height = size;
  const sweep = document.getElementById('sweep');
  sweep.style.width = size+'px'; sweep.style.height = size+'px';
}
window.addEventListener('resize', resizeRadar);
resizeRadar();

// Manual zoom, independent of the auto-fit-to-nodes scale computed each
// frame below: 1.0 = auto-fit (whatever's currently on screen fills the
// radius), >1 zooms in, <1 zooms out. Mouse wheel over the radar and the
// slider/buttons all just adjust this one number.
let zoomLevel = 1;
function setZoom(z){
  zoomLevel = Math.min(6, Math.max(0.3, z));
  document.getElementById('zoomSlider').value = zoomLevel.toFixed(1);
  document.getElementById('zoomLabel').textContent = zoomLevel.toFixed(1)+'x';
}
document.getElementById('zoomSlider').addEventListener('input', e=>setZoom(parseFloat(e.target.value)));
document.getElementById('zoomInBtn').addEventListener('click', ()=>setZoom(zoomLevel+0.2));
document.getElementById('zoomOutBtn').addEventListener('click', ()=>setZoom(zoomLevel-0.2));
document.getElementById('zoomFitBtn').addEventListener('click', ()=>setZoom(1));
document.getElementById('c').addEventListener('wheel', e=>{
  e.preventDefault();
  setZoom(zoomLevel * (e.deltaY < 0 ? 1.1 : 1/1.1));
}, {passive:false});
setZoom(1);

// Per-board TX power offset, keyed by the mac of the board doing the
// transmitting (added to any p.rssi where p.mac===that board, before it's
// turned into a distance). Corrects for one board simply shouting louder
// or softer than the rest -- a fixed dB error the shared RSSI_REF/PATH_LOSS_N
// curve above can't absorb because it's the same for every pair.
let nodeOffsets = {}; // mac -> dB

function renderCalibPoints(){
  const div = document.getElementById("calibPoints");
  div.textContent = calibPoints.length
    ? "Points: " + calibPoints.map(p=>`${p.dist}m@${p.rssi.toFixed(1)}dBm`).join(", ")
    : "No points collected yet.";
}

document.getElementById("calibAddBtn").addEventListener("click", ()=>{
  const a = document.getElementById("calibA").value, b = document.getElementById("calibB").value;
  const dist = parseFloat(document.getElementById("calibDist").value);
  const status = document.getElementById("status");
  if (!a || !b || a===b || !dist || !lastState){ status.textContent = "Pick two different nodes and a distance first"; return; }
  const readings = [];
  ((lastState.nodes[a]||{}).peers||[]).forEach(p=>{ if (p.mac===b) readings.push(p.rssi); });
  ((lastState.nodes[b]||{}).peers||[]).forEach(p=>{ if (p.mac===a) readings.push(p.rssi); });
  if (!readings.length){ status.textContent = `No direct RSSI between ${a} and ${b} yet -- wait a moment and retry`; return; }
  const avgRssi = readings.reduce((x,y)=>x+y,0)/readings.length;
  calibPoints.push({dist, rssi: avgRssi});
  renderCalibPoints();
  status.textContent = `Added point ${calibPoints.length}: ${dist}m @ ${avgRssi.toFixed(1)}dBm -- move the pair and add another, or click Fit`;
});

document.getElementById("calibClearBtn").addEventListener("click", ()=>{
  calibPoints = [];
  renderCalibPoints();
  document.getElementById("status").textContent = "Calibration points cleared";
});

document.getElementById("calibFitBtn").addEventListener("click", ()=>{
  const status = document.getElementById("status");
  if (!calibPoints.length){ status.textContent = "No calibration points yet -- add at least one"; return; }

  if (calibPoints.length === 1){
    const {dist, rssi} = calibPoints[0];
    RSSI_REF = rssi + 10*PATH_LOSS_N*Math.log10(dist);
    document.getElementById("rssiRef").value = RSSI_REF.toFixed(1);
    status.textContent = `Calibrated from 1 point: RSSI@1m = ${RSSI_REF.toFixed(1)} (path-loss n held at ${PATH_LOSS_N}) -- add a 2nd point at a different distance to fit n too`;
    return;
  }

  // Ordinary least squares on y = a + b*x, where x=log10(dist), y=rssi,
  // a=RSSI_REF, b=-10*PATH_LOSS_N.
  const n = calibPoints.length;
  const xs = calibPoints.map(p=>Math.log10(p.dist));
  const ys = calibPoints.map(p=>p.rssi);
  const xMean = xs.reduce((s,x)=>s+x,0)/n;
  const yMean = ys.reduce((s,y)=>s+y,0)/n;
  let num=0, den=0;
  for (let i=0;i<n;i++){ num += (xs[i]-xMean)*(ys[i]-yMean); den += (xs[i]-xMean)*(xs[i]-xMean); }
  if (Math.abs(den) < 1e-9){ status.textContent = "All points are at nearly the same distance -- cannot fit path-loss n, add a point at a different distance"; return; }
  const b = num/den;
  const a = yMean - b*xMean;
  RSSI_REF = a;
  PATH_LOSS_N = -b/10;
  document.getElementById("rssiRef").value = RSSI_REF.toFixed(1);
  document.getElementById("pathLoss").value = PATH_LOSS_N.toFixed(2);
  status.textContent = `Fit from ${n} points: RSSI@1m = ${RSSI_REF.toFixed(1)}, path-loss n = ${PATH_LOSS_N.toFixed(2)}`;
});

renderCalibPoints();

function renderOffsets(){
  const div = document.getElementById("offsetList");
  const keys = Object.keys(nodeOffsets);
  div.textContent = keys.length
    ? "Offsets: " + keys.map(k=>`${k.slice(-5)}: ${nodeOffsets[k]>=0?'+':''}${nodeOffsets[k].toFixed(1)}dB`).join(", ")
    : "No per-board offsets set.";
}

document.getElementById("offsetMeasureBtn").addEventListener("click", ()=>{
  const ref = document.getElementById("offsetRef").value;
  const target = document.getElementById("offsetTarget").value;
  const dist = parseFloat(document.getElementById("offsetDist").value);
  const status = document.getElementById("status");
  if (!ref || !target || ref===target || !dist || !lastState){ status.textContent = "Pick two different nodes and a distance first"; return; }
  // Isolate the target board's TX power specifically: only use readings where
  // target is the one transmitting, as heard by ref (ref's own peer list entry
  // for target). The reverse direction (target hearing ref) would instead be
  // measuring ref's TX power, which isn't what we're calibrating here.
  const readings = [];
  ((lastState.nodes[ref]||{}).peers||[]).forEach(p=>{ if (p.mac===target) readings.push(p.rssi); });
  if (!readings.length){ status.textContent = `No reading of ${target}'s signal at ${ref} yet -- wait a moment and retry`; return; }
  const measuredRssi = readings.reduce((a,b)=>a+b,0)/readings.length;
  const predictedRssi = RSSI_REF - 10*PATH_LOSS_N*Math.log10(dist);
  nodeOffsets[target] = predictedRssi - measuredRssi;
  renderOffsets();
  status.textContent = `${target.slice(-5)} offset: ${nodeOffsets[target]>=0?'+':''}${nodeOffsets[target].toFixed(1)}dB (measured ${measuredRssi.toFixed(1)}dBm at ${dist}m from ${ref.slice(-5)})`;
});

document.getElementById("offsetClearBtn").addEventListener("click", ()=>{
  nodeOffsets = {};
  renderOffsets();
  document.getElementById("status").textContent = "Per-board offsets cleared";
});

renderOffsets();

let selfMac = "";
let rotationAngle = 0;
let lastSelfPos = null;
let prevCoordsById = {}; // last frame's positions, keyed by node id -- used to keep the map from spinning

function jacobiEigen(A){
  const n = A.length;
  let a = A.map(r=>r.slice());
  let v = Array.from({length:n},(_,i)=>Array.from({length:n},(_,j)=>i===j?1:0));
  for (let sweep=0; sweep<100; sweep++){
    let off=0;
    for (let i=0;i<n;i++) for (let j=i+1;j<n;j++) off += a[i][j]*a[i][j];
    if (off < 1e-9) break;
    for (let p=0;p<n;p++) for (let q=p+1;q<n;q++){
      if (Math.abs(a[p][q]) < 1e-12) continue;
      const theta = (a[q][q]-a[p][p])/(2*a[p][q]);
      const t = Math.sign(theta||1)/(Math.abs(theta)+Math.sqrt(theta*theta+1));
      const c = 1/Math.sqrt(t*t+1), s = t*c;
      const app=a[p][p], aqq=a[q][q], apq=a[p][q];
      a[p][p] = c*c*app - 2*s*c*apq + s*s*aqq;
      a[q][q] = s*s*app + 2*s*c*apq + c*c*aqq;
      a[p][q] = a[q][p] = 0;
      for (let k=0;k<n;k++){
        if (k!==p && k!==q){
          const akp=a[k][p], akq=a[k][q];
          a[k][p]=a[p][k]=c*akp - s*akq;
          a[k][q]=a[q][k]=s*akp + c*akq;
        }
        const vkp=v[k][p], vkq=v[k][q];
        v[k][p] = c*vkp - s*vkq;
        v[k][q] = s*vkp + c*vkq;
      }
    }
  }
  const eigvals = a.map((row,i)=>row[i]);
  return {eigvals, eigvecs: v};
}

function classicalMDS(D){
  const n = D.length;
  const D2 = D.map(row=>row.map(d=>d*d));
  const rowMean = D2.map(row=>row.reduce((a,b)=>a+b,0)/n);
  const grandMean = rowMean.reduce((a,b)=>a+b,0)/n;
  const B = D2.map((row,i)=>row.map((d2,j)=> -0.5*(d2 - rowMean[i] - rowMean[j] + grandMean)));
  const {eigvals, eigvecs} = jacobiEigen(B);
  const idx = eigvals.map((v,i)=>i).sort((a,b)=>eigvals[b]-eigvals[a]);
  const [i1,i2] = idx;
  const l1 = Math.max(eigvals[i1],0), l2 = Math.max(eigvals[i2],0);
  const coords = [];
  for (let k=0;k<n;k++){
    coords.push([ eigvecs[k][i1]*Math.sqrt(l1), eigvecs[k][i2]*Math.sqrt(l2) ]);
  }
  return coords;
}

function buildMatrix(nodeIds, nodesData){
  const n = nodeIds.length;
  const D = Array.from({length:n},()=>Array(n).fill(null));
  for (let i=0;i<n;i++) D[i][i]=0;
  for (let i=0;i<n;i++){
    const peers = nodesData[nodeIds[i]].peers || [];
    peers.forEach(p=>{
      const j = nodeIds.indexOf(p.mac);
      if (j>=0){
        const d = rssiToDist(p.rssi + (nodeOffsets[p.mac]||0));
        if (D[i][j]===null) D[i][j]=d; else D[i][j]=(D[i][j]+d)/2;
        if (D[j][i]===null) D[j][i]=d; else D[j][i]=(D[j][i]+d)/2;
      }
    });
  }
  let known=[]; D.forEach(row=>row.forEach(v=>{if(v!==null)known.push(v);}));
  const avg = known.length ? known.reduce((a,b)=>a+b,0)/known.length : 2.0;
  for (let i=0;i<n;i++) for (let j=0;j<n;j++) if (D[i][j]===null) D[i][j]=avg;
  return D;
}

// Classical MDS only recovers shape up to an arbitrary rotation AND mirror
// reflection -- two solutions that produce identical pairwise distances can
// still look completely different on screen. Noisy RSSI readings mean each
// tick's raw solution can land on a different rotation/reflection than the
// last, which is what makes the map spin. This aligns the new frame to the
// previous one (best-fit rotation, tried both normal and mirrored) so the
// map only actually reflects genuine relative movement, not solver noise.
function complexAlign(curPts, prevPts){
  function tryOrientation(pts){
    let re=0, im=0;
    for (let i=0;i<pts.length;i++){
      const [cx,cy]=pts[i], [px,py]=prevPts[i];
      re += cx*px + cy*py;
      im += cx*py - cy*px;
    }
    const theta = Math.atan2(im, re);
    const c=Math.cos(theta), s=Math.sin(theta);
    let err=0;
    for (let i=0;i<pts.length;i++){
      const [cx,cy]=pts[i], [px,py]=prevPts[i];
      const rx = c*cx - s*cy, ry = s*cx + c*cy;
      err += (rx-px)*(rx-px) + (ry-py)*(ry-py);
    }
    return {theta, err};
  }
  const normal = tryOrientation(curPts);
  const mirrored = tryOrientation(curPts.map(([x,y])=>[-x,y]));
  return mirrored.err < normal.err ? {theta:mirrored.theta, reflect:true} : {theta:normal.theta, reflect:false};
}
function applyAlign(coords, align){
  const c=Math.cos(align.theta), s=Math.sin(align.theta);
  return coords.map(([x,y])=>{
    const px = align.reflect ? -x : x;
    return [c*px - s*y, s*px + c*y];
  });
}

async function tick(){
  const res = await fetch('/state.json'); const st = await res.json();
  lastState = st;
  const nodeIds = Object.keys(st.nodes);
  const picker = document.getElementById('selfPicker');
  const realIds = nodeIds.filter(id=>!st.nodes[id].is_anchor);
  if (picker.options.length-1 !== realIds.length){
    picker.innerHTML = '<option value="">(none)</option>';
    realIds.forEach(id=>{ const o=document.createElement('option'); o.value=id; o.textContent=id; picker.appendChild(o); });
    if (selfMac) picker.value = selfMac;
  }
  [document.getElementById('calibA'), document.getElementById('calibB'),
   document.getElementById('offsetRef'), document.getElementById('offsetTarget')].forEach(sel=>{
    if (sel.options.length !== realIds.length){
      const prev = sel.value;
      sel.innerHTML = '';
      realIds.forEach(id=>{ const o=document.createElement('option'); o.value=id; o.textContent=id; sel.appendChild(o); });
      if (realIds.includes(prev)) sel.value = prev;
    }
  });

  if (nodeIds.length < 3){
    document.getElementById('status').textContent = `Need 3+ nodes for positioning, have ${nodeIds.length}`;
    prevCoordsById = {};
    return;
  }

  const D = buildMatrix(nodeIds, st.nodes);
  const raw = classicalMDS(D);

  // stabilize against last frame
  const matchedCur = [], matchedPrev = [];
  nodeIds.forEach((id,i)=>{ if (prevCoordsById[id]) { matchedCur.push(raw[i]); matchedPrev.push(prevCoordsById[id]); } });
  const stabilized = matchedCur.length >= 2 ? applyAlign(raw, complexAlign(matchedCur, matchedPrev)) : raw;

  // light smoothing on top to soak up residual per-tick jitter
  const SMOOTH = 0.35;
  const smoothed = nodeIds.map((id,i)=>{
    const p = prevCoordsById[id];
    return p ? [p[0]+(stabilized[i][0]-p[0])*SMOOTH, p[1]+(stabilized[i][1]-p[1])*SMOOTH] : stabilized[i];
  });
  nodeIds.forEach((id,i)=>{ prevCoordsById[id] = smoothed[i]; });

  // separate from stabilization: optionally spin the whole (now-stable)
  // frame so the selected "self" node's recent movement points up -- a
  // heading cue for the person holding that board, not a jitter fix
  let coords = smoothed;
  const selfIdx = nodeIds.indexOf(selfMac);
  if (selfIdx >= 0){
    const pos = smoothed[selfIdx];
    if (lastSelfPos){
      const dx = pos[0]-lastSelfPos[0], dy = pos[1]-lastSelfPos[1];
      if (Math.hypot(dx,dy) > 0.15){
        const targetAngle = Math.atan2(dx,-dy);
        rotationAngle += (targetAngle-rotationAngle)*0.3;
      }
    }
    lastSelfPos = pos;
  }
  const cos_=Math.cos(rotationAngle), sin_=Math.sin(rotationAngle);
  coords = coords.map(([x,y])=>[x*cos_-y*sin_, x*sin_+y*cos_]);

  const canvasEl = document.getElementById('c');
  const ctx = canvasEl.getContext('2d');
  const size = canvasEl.width; // square, kept in sync by resizeRadar()
  ctx.clearRect(0,0,size,size);
  const CX=size/2, CY=size/2;
  const maxExtent = Math.max(1, ...coords.map(([x,y])=>Math.hypot(x,y)));
  const RADIUS = size/2 - 26; // leave room for edge distance labels
  const baseScale = RADIUS / maxExtent; // scale that fits every current node at zoom=1
  const SCALE = baseScale * zoomLevel;
  const DOTSCALE = size/380; // keep node/text sizes proportional at any canvas size

  // concentric range rings + crosshair, 80s-radar style. Ring pixel radii
  // are fixed fractions of RADIUS; their metre labels are derived from
  // SCALE so they stay correct as zoomLevel changes (zooming in shrinks
  // the real-world distance each ring represents, same as a real radar).
  const RINGS = 4;
  ctx.strokeStyle = 'rgba(57,255,136,0.22)';
  ctx.fillStyle = 'rgba(57,255,136,0.55)';
  ctx.font = `${Math.max(9,9*DOTSCALE)}px monospace`;
  ctx.textAlign = 'left'; ctx.textBaseline = 'alphabetic';
  for (let r=1; r<=RINGS; r++){
    const rad = RADIUS * r/RINGS;
    ctx.beginPath(); ctx.arc(CX,CY,rad,0,Math.PI*2); ctx.stroke();
    ctx.fillText((rad/SCALE).toFixed(1)+'m', CX+4, CY-rad+11);
  }
  ctx.beginPath();
  ctx.moveTo(CX-RADIUS,CY); ctx.lineTo(CX+RADIUS,CY);
  ctx.moveTo(CX,CY-RADIUS); ctx.lineTo(CX,CY+RADIUS);
  ctx.stroke();

  if (document.getElementById('showDist').checked){
    ctx.strokeStyle = 'rgba(57,255,136,0.28)'; ctx.fillStyle='rgba(57,255,136,0.65)';
    ctx.font = `${Math.max(8,9*DOTSCALE)}px monospace`;
    for (let i=0;i<coords.length;i++) for (let j=i+1;j<coords.length;j++){
      const [x1,y1]=coords[i], [x2,y2]=coords[j];
      const p1x=CX+x1*SCALE, p1y=CY-y1*SCALE, p2x=CX+x2*SCALE, p2y=CY-y2*SCALE;
      ctx.beginPath(); ctx.moveTo(p1x,p1y); ctx.lineTo(p2x,p2y); ctx.stroke();
      ctx.fillText(D[i][j].toFixed(1)+'m', (p1x+p2x)/2, (p1y+p2y)/2);
    }
  }

  coords.forEach(([x,y],i)=>{
    const px = CX + x*SCALE, py = CY - y*SCALE;
    const n = st.nodes[nodeIds[i]];
    const isSelf = nodeIds[i]===selfMac;
    const dotColor = n.is_anchor ? '#3f6b52' : (n.help_detected ? '#ff3b3b' : (isSelf ? '#00eaff' : '#39ff88'));
    const dotR = (n.is_anchor?6:8) * DOTSCALE;
    ctx.save();
    ctx.shadowColor = dotColor; ctx.shadowBlur = 10*DOTSCALE;
    ctx.fillStyle = dotColor;
    ctx.beginPath(); ctx.arc(px,py,dotR,0,Math.PI*2); ctx.fill();
    ctx.restore();
    ctx.fillStyle = dotColor; ctx.font = `${Math.max(9,10*DOTSCALE)}px monospace`;
    ctx.fillText((n.is_anchor?'AP ':'')+nodeIds[i].slice(-5), px-18*DOTSCALE, py-12*DOTSCALE);
  });
  document.getElementById('status').textContent = `${nodeIds.length} nodes, updated ${new Date().toLocaleTimeString()}`;
}

document.getElementById('selfPicker').addEventListener('change', e=>{
  selfMac = e.target.value; lastSelfPos = null;
});

setInterval(tick, 500); tick();
</script></body></html>
)rawliteral";
