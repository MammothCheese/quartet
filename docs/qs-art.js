/* QUARTET quick start: the keyboards, the beat charts and the line animations, shared by index.html and start.html.
 * Each scene is built once (build) and ticks while on screen (start / stop, below). */
(() => {
  'use strict';
  const NS = 'http://www.w3.org/2000/svg';
  const COL = { lead: '#C86E00', pad: '#7A46DA', bass: '#0080BE', drums: '#188C32', ink: '#1A1A1E', sub: '#5C5C64', dim: '#A8A69F', paper: '#F0EEE9', card: '#FAF9F6', cap: '#E6E4DE', mute: '#D63026' };
  const TRK = ['lead', 'pad', 'bass', 'drums'];
  const RM = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const $ = (id) => document.getElementById(id);
  function el(parent, tag, a, text) {                        // an SVG element; "draw" lines get their length set
    const e = document.createElementNS(NS, tag);
    for (const k in a) e.setAttribute(k, a[k]);
    if (text != null) e.textContent = text;
    parent.appendChild(e);
    return e;
  }
  function lengths(svg) { svg.querySelectorAll('.draw').forEach((p) => { const L = p.getTotalLength ? Math.ceil(p.getTotalLength()) + 2 : 2000; p.style.setProperty('--len', L); }); }
  const rr = (x, y, w, h, r) => `M${x + r},${y}h${w - 2 * r}a${r},${r} 0 0 1 ${r},${r}v${h - 2 * r}a${r},${r} 0 0 1 -${r},${r}h${2 * r - w}a${r},${r} 0 0 1 -${r},-${r}v${2 * r - h}a${r},${r} 0 0 1 ${r},-${r}z`;
  const cap = (id, b, rest) => { const n = $(id); if (n) n.innerHTML = '<b>' + b + '</b>' + (rest ? ' · ' + rest : ''); };

  // ---- the FM-1's keyboard (F3 to G5), for the recipe: data-kb="3=A,5=C,7=E" (white key 1..16 = its label)
  const WN = ['F', 'G', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'A', 'B', 'C', 'D', 'E', 'F', 'G'];
  const WK = ['Q', 'W', 'E', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', ',', '.', '/', 'R', 'T', 'Y'];
  const BLK = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14];              // a black key after these white keys (0-based)
  document.querySelectorAll('svg.kb').forEach((svg) => {
    const c = getComputedStyle(svg).getPropertyValue('--c').trim() || COL.ink, col = c.startsWith('var') ? COL.ink : c;
    const hot = {}; svg.dataset.kb.split(',').forEach((p) => { const [k, l] = p.split('='); hot[+k - 1] = l; });
    const W = 19, H = 58; svg.setAttribute('viewBox', `-1 -1 ${16 * W + 2} ${H + 24}`);
    for (let i = 0; i < 16; i++) {
      const on = i in hot;
      el(svg, 'rect', { x: i * W, y: 0, width: W, height: H, rx: 3, fill: on ? 'currentColor' : COL.card, stroke: COL.ink, 'stroke-width': 1, style: on ? `color:${col}` : '' });
      if (on) {
        hot[i].split(' ').reverse().forEach((l, j) => el(svg, 'text', { x: i * W + W / 2, y: H - 7 - j * 10, 'text-anchor': 'middle', style: 'font:700 9px var(--mono);fill:#fff' }, l));
        el(svg, 'text', { x: i * W + W / 2, y: H + 12, 'text-anchor': 'middle', style: 'font:700 9.5px var(--mono);fill:' + COL.ink }, WN[i]);
        el(svg, 'text', { x: i * W + W / 2, y: H + 22, 'text-anchor': 'middle', style: 'font:600 7.5px var(--mono);fill:' + COL.dim }, WK[i]);
      }
    }
    BLK.forEach((w) => el(svg, 'rect', { x: (w + 1) * W - 5.5, y: 0, width: 11, height: H * 0.58, rx: 2, fill: COL.ink }));
  });
  // ---- the beat, as a chart: data-beat="KICK:1:1,7,9|..." (the drum, its black key, its steps); data-new: steps to mark new
  document.querySelectorAll('svg.beat').forEach((svg) => {
    const rows = svg.dataset.beat.split('|').map((r) => { const [n, b, s] = r.split(':'); return { n, b, s: s.split(',').map(Number) }; });
    const nw = svg.dataset.new ? svg.dataset.new.split(':')[1].split(',').map(Number) : [], nr = svg.dataset.new ? +svg.dataset.new.split(':')[0] : -1;
    const X = 92, C = 23, R = 24;
    svg.setAttribute('viewBox', `0 0 ${X + 16 * C} ${18 + rows.length * R + (nw.length ? 28 : 14)}`);
    for (let s = 0; s < 16; s++) el(svg, 'text', { x: X + s * C + C / 2 - 1, y: 11, 'text-anchor': 'middle', style: 'font:700 9px var(--mono);fill:' + (s % 4 ? COL.dim : COL.sub) }, s + 1);
    rows.forEach((r, i) => {
      const y = 18 + i * R;
      el(svg, 'text', { x: 0, y: y + 12, style: 'font:700 10px var(--mono);fill:' + COL.drums }, r.n);
      el(svg, 'text', { x: 0, y: y + 21, style: 'font:600 7.5px var(--mono);fill:' + COL.dim }, 'BLACK KEY ' + r.b);
      for (let s = 0; s < 16; s++) {
        const on = r.s.includes(s + 1), fresh = i === nr && nw.includes(s + 1);
        el(svg, 'rect', { x: X + s * C, y, width: C - 4, height: R - 5, rx: 4, fill: on ? (fresh ? COL.pad : COL.drums) : COL.card, stroke: on ? 'none' : (s % 4 ? COL.line || '#DAD8D2' : COL.dim), 'stroke-width': 1 });
      }
    });
    for (let s = 0; s < 16; s++) el(svg, 'text', { x: X + s * C + C / 2 - 1, y: 18 + rows.length * R + 10, 'text-anchor': 'middle', style: 'font:600 8px var(--mono);fill:' + COL.dim }, WK[s]);
    el(svg, 'text', { x: 0, y: 18 + rows.length * R + 10, style: 'font:600 7.5px var(--mono);fill:' + COL.dim }, 'COMPUTER KEY');
    if (nw.length) el(svg, 'text', { x: X, y: 18 + rows.length * R + 24, style: 'font:600 8px var(--mono);fill:' + COL.pad }, 'NEW IN PATTERN B');
  });

  // each scene: build() once, then tick(t) a few times a second while it's on screen
  const scenes = [];
  function scene(svgId, build, tick, every) { if ($(svgId)) scenes.push({ svg: $(svgId), build, tick, every: every || 250, on: false, t: 0, timer: 0 }); }

  // ---- the FM-1, drawn line by line; on its screen the four players and their pedals
  scene('hero', (g) => {
    el(g, 'path', { class: 'ln draw', d: rr(20, 20, 560, 320, 30) });
    [[70, 70], [130, 70], [70, 130], [130, 130]].forEach(([x, y]) => el(g, 'circle', { class: 'ln draw', cx: x, cy: y, r: 13 }));
    el(g, 'path', { class: 'ln draw thin', d: rr(52, 168, 96, 26, 7) });
    el(g, 'path', { class: 'ln draw', d: rr(178, 38, 150, 150, 10) });
    for (let i = 0; i < 4; i++) el(g, 'circle', { class: 'ln draw', cx: 372 + i * 54, cy: 62, r: 12 });
    el(g, 'path', { class: 'ln draw thin', d: rr(352, 104, 206, 84, 10) });
    for (let r = 0; r < 2; r++) for (let c = 0; c < 6; c++) el(g, 'path', { class: 'ln draw thin', d: rr(362 + c * 32.5, 113 + r * 37, 25, 28, 6) });
    el(g, 'path', { class: 'ln draw thin', d: rr(40, 214, 520, 110, 16) });
    for (let i = 0; i < 16; i++) el(g, 'path', { class: 'ln draw thin', d: rr(52 + i * 31.5, 272, 24, 44, 12) });
    [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14].forEach((w) => el(g, 'path', { class: 'ln draw thin', d: rr(52 + w * 31.5 + 19, 222, 22, 42, 11) }));
    const sc = el(g, 'g', { class: 'fade' });
    TRK.forEach((t, i) => {
      el(sc, 'path', { d: rr(188, 52 + i * 26, 130, 21, 5), fill: COL.card, stroke: COL[t], 'stroke-width': 1.4, id: 'hrow' + i });
      el(sc, 'text', { x: 196, y: 66 + i * 26, fill: COL[t], style: 'font-size:9px;font-weight:700' }, t.toUpperCase());
      for (let s = 0; s < 8; s++) el(sc, 'rect', { x: 246 + s * 8.5, y: 58 + i * 26, width: 6, height: 9, rx: 1.5, fill: COL[t], opacity: .22, id: 'hs' + i + s });
    });
    for (let p = 0; p < 4; p++) el(sc, 'path', { d: rr(188 + p * 33, 160, 30, 16, 8), fill: COL.card, stroke: COL.dim, 'stroke-width': 1, id: 'hp' + p });
  }, (t) => {
    const s = t % 8, who = Math.floor(t / 16) % 4;
    for (let i = 0; i < 4; i++) for (let k = 0; k < 8; k++) $('hs' + i + k).setAttribute('opacity', k === s ? 1 : ((i + k) % 3 === 0 ? .5 : .18));
    for (let i = 0; i < 4; i++) $('hrow' + i).setAttribute('stroke-width', i === who ? 3 : 1.2);
    for (let p = 0; p < 4; p++) $('hp' + p).setAttribute('stroke', p === Math.floor(t / 8) % 4 ? COL[TRK[who]] : COL.dim);
    cap('heroCap', TRK[who].toUpperCase(), ['PIANO', 'STRINGS', 'MOOG BASS', '808'][who]);
  }, 180);

  // ---- 1: the beat: the grid fills drum by drum (kick, snare, hats), then plays
  const DR = ['KICK', 'SNARE', 'HAT', 'OPEN', 'CLAP', 'PERC'];
  const HITS = [[0, 0], [0, 6], [0, 8], [1, 4], [1, 12], [2, 0], [2, 2], [2, 4], [2, 6], [2, 8], [2, 10], [2, 12], [2, 14]];
  scene('grid', (g) => {
    DR.forEach((d, r) => {
      el(g, 'text', { x: 0, y: 42 + r * 44, fill: r < 3 ? COL.drums : COL.dim, style: 'font-weight:700' }, d);
      for (let s = 0; s < 16; s++) el(g, 'path', { class: 'ln draw thin' + (s % 4 ? ' soft' : ''), d: rr(70 + s * 32.5, 22 + r * 44, 26, 28, 5), id: `g${r}_${s}` });
    });
    el(g, 'path', { id: 'gph', d: 'M0,14V286', stroke: COL.ink, 'stroke-width': 1.6, opacity: 0 });
  }, (t) => {
    const T = t % 61, placed = Math.min(HITS.length, T);
    for (let r = 0; r < 6; r++) for (let s = 0; s < 16; s++) $(`g${r}_${s}`).setAttribute('fill', 'none');
    HITS.slice(0, placed).forEach(([r, s]) => $(`g${r}_${s}`).setAttribute('fill', COL.drums));
    if (T < HITS.length) {
      $('gph').setAttribute('opacity', 0);
      const [r, s] = HITS[T]; cap('gridCap', DR[r], 'STEP ' + String(s + 1).padStart(2, '0'));
    } else {
      const s = (T - HITS.length) % 16;
      $('gph').setAttribute('opacity', 1); $('gph').setAttribute('transform', `translate(${70 + s * 32.5 + 13},0)`);
      HITS.forEach(([r, k]) => { if (k === s) $(`g${r}_${k}`).setAttribute('fill', COL.ink); });
      cap('gridCap', 'PLAYING', 'STEP ' + String(s + 1).padStart(2, '0') + ' · 90 BPM');
    }
  }, 160);

  // ---- 2-4: a piano roll, four bars: notes written one by one (the step recorder), then played
  // notes: [step 0..63, length in steps, rows (0 = F3 .. 12 = F4)]
  const BLACKF = [1, 3, 5, 8, 10];                          // (the black keys, counting from F)
  const CH = ['Am', 'F', 'C', 'G'];
  function roll(id, colour, notes, name) {
    const X0 = 50, SW = 540 / 64, RH = 20, top = 30;
    scene(id, (g) => {
      for (let r = 0; r < 13; r++) {
        el(g, 'path', { class: 'ln draw thin', d: rr(0, top + (12 - r) * RH, 40, RH - 2, 3), fill: BLACKF.includes(r % 12) ? COL.ink : 'none' });
        el(g, 'path', { class: 'ln draw thin soft', d: `M48,${top + (12 - r) * RH + 9}H590` });
      }
      for (let b = 0; b <= 4; b++) el(g, 'path', { class: 'ln draw thin' + (b % 4 ? ' soft' : ''), d: `M${X0 + b * 16 * SW},${top - 8}V${top + 13 * RH}` });
      for (let b = 0; b < 4; b++) el(g, 'text', { x: X0 + b * 16 * SW + 6, y: top - 12, style: 'font-weight:700', fill: COL.sub }, 'BAR ' + (b + 1) + ' · ' + CH[b]);
      notes.forEach(([s, l, rows], i) => rows.forEach((r, k) => el(g, 'rect', { id: `${id}n${i}_${k}`, x: X0 + s * SW + 1.5, y: top + (12 - r) * RH + 2, width: l * SW - 3, height: RH - 6, rx: 4, fill: colour, opacity: 0 })));
      el(g, 'path', { id: id + 'ph', d: `M0,${top - 6}V${top + 13 * RH + 4}`, stroke: COL.ink, 'stroke-width': 1.6 });
    }, (t) => {
      const T = t % 140, write = T < notes.length * 3;          // writing: a note every three ticks; then two rounds played
      const shown = write ? Math.floor(T / 3) + 1 : notes.length, s = write ? (notes[Math.min(shown, notes.length) - 1][0]) : ((T - notes.length * 3) % 64);
      $(id + 'ph').setAttribute('transform', `translate(${X0 + s * SW},0)`);
      let now = -1;
      notes.forEach(([k, l, rows], i) => {
        const vis = i < shown, hit = write ? i === shown - 1 : s >= k && s < k + l;
        if (hit) now = i;
        rows.forEach((r, j) => $(`${id}n${i}_${j}`).setAttribute('opacity', vis ? (hit ? 1 : .55) : 0));
      });
      const bar = Math.floor(s / 16);
      cap(id + 'Cap', (write ? 'WRITING · ' : '') + 'BAR ' + (bar + 1), now >= 0 ? name(notes[now]) : CH[bar]);
    }, 85);
  }
  const NN = ['F', 'F#', 'G', 'G#', 'A', 'A#', 'B', 'C', 'C#', 'D', 'D#', 'E', 'F'];
  roll('rollB', COL.bass, [[0, 8, [4]], [8, 8, [4]], [16, 8, [0]], [24, 8, [0]], [32, 8, [7]], [40, 8, [7]], [48, 8, [2]], [56, 8, [2]]], (n) => NN[n[2][0]]);
  roll('rollP', COL.pad, [[0, 16, [4, 7, 11]], [16, 16, [4, 7, 12]], [32, 16, [2, 7, 11]], [48, 16, [2, 6, 9]]], (n) => CH[n[0] / 16] + ' · ' + n[2].map((r) => NN[r]).join(' '));
  const ARPS = [[4, 7, 11, 7], [0, 4, 7, 4], [2, 7, 11, 7], [2, 6, 9, 6]];
  roll('rollL', COL.lead, ARPS.flatMap((a, b) => a.map((r, k) => [b * 16 + k * 4, 4, [r]])), (n) => NN[n[2][0]]);

  // ---- 5: the lead through its four pedals: reverb, then delay, switched on; each changes the wave after it
  const PEDALS = ['REVERB', 'DELAY', 'DIST', 'CHORUS'];
  scene('chain', (g) => {
    el(g, 'path', { class: 'ln draw', d: rr(0, 130, 64, 70, 10) }); el(g, 'text', { x: 32, y: 170, 'text-anchor': 'middle', fill: COL.lead, style: 'font-weight:700' }, 'LEAD');
    PEDALS.forEach((p, i) => {
      const x = 100 + i * 112;
      el(g, 'path', { class: 'ln draw', d: rr(x, 110, 76, 110, 10), id: 'pb' + i });
      el(g, 'text', { x: x + 38, y: 134, 'text-anchor': 'middle', style: 'font-weight:700' }, p);
      for (let k = 0; k < 3; k++) el(g, 'circle', { class: 'ln draw thin', cx: x + 16 + k * 22, cy: 158, r: 7 });
      el(g, 'circle', { cx: x + 38, cy: 198, r: 9, class: 'ln draw thin' });
      el(g, 'circle', { cx: x + 64, cy: 122, r: 3.5, fill: COL.dim, id: 'pl' + i });
      el(g, 'text', { x: x + 38, y: 246, 'text-anchor': 'middle' }, ['FX', 'SEL', 'ENV', 'LFO'][i]);
    });
    el(g, 'path', { class: 'ln draw', d: 'M560,140l18,-14v78l-18,-14h-10v-50z' });
    for (let i = 0; i < 5; i++) el(g, 'path', { id: 'w' + i, fill: 'none', stroke: COL.lead, 'stroke-width': 1.8, 'stroke-linecap': 'round' });
  }, (t) => {
    const T = t % 100, on = [T >= 14 && T < 86, T >= 34 && T < 86, false, false];
    PEDALS.forEach((p, i) => {
      $('pb' + i).setAttribute('fill', on[i] ? '#F8EBDA' : 'none');
      $('pl' + i).setAttribute('fill', on[i] ? '#FF4632' : COL.dim);
    });
    const ph = t * 0.35;
    for (let i = 0; i < 5; i++) {
      const x0 = i === 0 ? 64 : 100 + (i - 1) * 112 + 76, x1 = i === 4 ? 550 : 100 + i * 112;
      const rev = i >= 1 && on[0], del = i >= 2 && on[1];
      let d = '';
      for (let x = x0; x <= x1; x += 1.5) {
        const u = (x - x0) / 9 - ph;                 // (minus: the signal travels left to right, into the pedals)
        const pluck = Math.pow(0.5 + 0.5 * Math.cos(u / 4), 3);   // (piano notes: each one fades)
        let v = Math.sin(u) * pluck;
        if (rev) v = v * 0.85 + 0.22 * Math.sin(u * 2.7 + ph) * (0.6 + 0.4 * Math.sin(u / 7));
        if (del) v = v + 0.5 * Math.sin(u - 6.3) * Math.pow(0.5 + 0.5 * Math.cos((u - 6.3) / 4), 3);
        d += (d ? 'L' : 'M') + x.toFixed(1) + ',' + (165 - v * 16).toFixed(1);
      }
      $('w' + i).setAttribute('d', d);
    }
    cap('chainCap', on[0] || on[1] ? PEDALS.filter((p, i) => on[i]).join(' + ') : 'ALL OFF', on[0] || on[1] ? 'ON' : 'CLEAN');
  }, 60);

  // ---- 6: SUPER SEQ: the drum row A A A B, a box a bar; the others one 4-bar A, round and round
  const SONG = [['A', '', '', ''], ['A', '', '', ''], ['A', '', '', ''], ['A', '', '', 'B']];
  scene('sseq', (g) => {
    TRK.forEach((t, i) => {
      el(g, 'text', { x: 0, y: 64 + i * 64, fill: COL[t], style: 'font-weight:700' }, ['LD', 'PD', 'BS', 'DR'][i]);
      SONG[i].forEach((p, b) => {
        el(g, 'path', { class: 'ln draw thin', d: rr(44 + b * 132, 38 + i * 64, 120, 44, 8), id: `sb${i}_${b}`, stroke: COL[t] });
        el(g, 'text', { x: 104 + b * 132, y: 66 + i * 64, 'text-anchor': 'middle', class: 'big', id: `st${i}_${b}`, fill: COL[t] }, p || '·');
      });
    });
    el(g, 'text', { x: 300, y: 318, 'text-anchor': 'middle', id: 'sbar' }, '');
  }, (t) => {
    const bar = t % 16, dr = bar % 4, rest = Math.floor(bar / 4);   // (16 bars: the 4-bar A four times; the drums' box each bar)
    for (let i = 0; i < 4; i++) for (let k = 0; k < 4; k++) {
      const on = i === 3 ? k === dr : k === rest;
      $(`sb${i}_${k}`).setAttribute('fill', on ? COL[TRK[i]] : COL.card);
      $(`st${i}_${k}`).setAttribute('fill', on ? '#fff' : COL[TRK[i]]);
      $(`st${i}_${k}`).textContent = SONG[i][k] || (on ? 'A' : '·');
    }
    $('sbar').textContent = 'BAR ' + (bar % 4 + 1) + ' OF THE CHORDS · ' + CH[bar % 4] + (dr === 3 ? ' · DRUM FILL' : '');
    cap('sseqCap', 'BAR ' + (bar + 1), dr === 3 ? 'DRUMS B · FILL' : 'DRUMS A');
  }, 700);

  // ---- 7: SAVE: four slots; KNOB 1 to slot 2, REC, REC, and it fills with the song
  scene('save', (g) => {
    for (let i = 0; i < 4; i++) {
      const x = 30 + i * 140;
      el(g, 'path', { class: 'ln draw', d: rr(x, 40, 120, 150, 14), id: 'sv' + i });
      el(g, 'text', { x: x + 60, y: 68, 'text-anchor': 'middle', class: 'big' }, 'SLOT ' + (i + 1));
      el(g, 'text', { x: x + 60, y: 120, 'text-anchor': 'middle', id: 'sve' + i }, 'EMPTY');
      TRK.forEach((t, k) => el(g, 'rect', { x: x + 18, y: 88 + k * 20, width: 84, height: 12, rx: 4, fill: COL[t], opacity: 0, id: `svb${i}_${k}` }));
    }
    el(g, 'circle', { class: 'ln draw', cx: 222, cy: 262, r: 30 });
    el(g, 'path', { d: 'M222,262V238', stroke: COL.ink, 'stroke-width': 3, 'stroke-linecap': 'round', id: 'svk' });
    el(g, 'text', { x: 222, y: 312, 'text-anchor': 'middle' }, 'KNOB 1');
    el(g, 'path', { class: 'ln draw', d: rr(330, 238, 96, 48, 10), id: 'svr' });
    el(g, 'circle', { cx: 410, cy: 250, r: 4, fill: COL.dim, id: 'svl' });
    el(g, 'text', { x: 378, y: 267, 'text-anchor': 'middle', class: 'big' }, 'REC');
    el(g, 'text', { x: 378, y: 312, 'text-anchor': 'middle', id: 'svt' }, '');
  }, (t) => {
    const T = t % 24, slot = T < 4 ? 0 : 1, press = T === 8 || T === 11, saved = T >= 12;
    for (let i = 0; i < 4; i++) {
      $('sv' + i).setAttribute('stroke-width', i === slot ? 3.2 : 1.6);
      const full = i === 1 && saved;
      $('sve' + i).textContent = full ? '' : 'EMPTY';
      TRK.forEach((tr, k) => $(`svb${i}_${k}`).setAttribute('opacity', full && T - 12 > k ? .9 : 0));
    }
    $('svk').setAttribute('transform', `rotate(${slot * 40 - 20} 222 262)`);
    $('svr').setAttribute('fill', press ? COL.cap : 'none');
    $('svl').setAttribute('fill', T >= 8 && T < 12 ? (T % 2 ? COL.mute : COL.dim) : saved && T < 16 ? COL.mute : COL.dim);
    $('svt').textContent = T >= 8 && T < 11 ? 'REC AGAIN?' : T >= 11 && T < 16 ? 'SAVED' : '';
    cap('saveCap', 'SLOT ' + (slot + 1), saved ? 'SAVED' : T >= 8 ? 'REC AGAIN TO SAVE' : 'EMPTY');
  }, 300);

  // ---- the end: the song you made, as four lanes with a playhead (one bar of it)
  const PAT = [[0, 4, 8, 12], [0], [0, 8], [0, 2, 4, 6, 8, 10, 12, 14]];
  scene('seq', (g) => {
    TRK.forEach((t, i) => {
      const y = 40 + i * 66;
      el(g, 'text', { x: 0, y: y + 22, fill: COL[t], style: 'font-weight:700' }, t.toUpperCase());
      el(g, 'path', { class: 'ln draw thin soft', d: `M70,${y + 18}H590` });
      for (let s = 0; s < 16; s++) {
        const x = 78 + s * 32.4;
        el(g, 'path', { class: 'ln draw thin soft', d: `M${x - 4},${y + 14}v8` });
        if (PAT[i].includes(s) && i !== 1) el(g, 'circle', { cx: x + 10, cy: y + 18, r: 7, fill: 'none', stroke: COL[t], 'stroke-width': 1.8, class: 'fade', id: `n${i}_${s}` });
      }
      if (i === 1) el(g, 'path', { d: `M80,${y + 18}H588`, stroke: COL.pad, 'stroke-width': 7, 'stroke-linecap': 'round', class: 'fade', opacity: .35 });
    });
    el(g, 'path', { id: 'ph', d: 'M0,24V300', stroke: COL.ink, 'stroke-width': 1.6 });
  }, (t) => {
    const s = t % 16, x = 78 + s * 32.4 + 10;
    $('ph').setAttribute('transform', `translate(${x},0)`);
    PAT.forEach((p, i) => p.forEach((k) => { const n = $(`n${i}_${k}`); if (n) { n.setAttribute('fill', k === s ? COL[TRK[i]] : 'none'); n.setAttribute('r', k === s ? 9 : 7); } }));
    cap('seqCap', 'STEP ' + String(s + 1).padStart(2, '0'), '16 · 90 BPM');
  }, 167);

  scenes.forEach((S) => { S.build(S.svg); lengths(S.svg); });
  function start(S) { if (S.on) return; S.on = true; S.tick(S.t); if (!RM) S.timer = setInterval(() => S.tick(++S.t), S.every); }
  function stop(S) { if (!S.on) return; S.on = false; clearInterval(S.timer); }
  window.QSArt = { scenes, start, stop, $ };
})();
