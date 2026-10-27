#!/usr/bin/env node
// Screenshot the dashboard with headless Chrome over the DevTools protocol, waiting in real
// time so the page collects enough samples for its rate counters (plain --screenshot either
// fast-forwards the clock or captures a backgrounded page that never polls).
//
// Usage: node tools/screenshot_dashboard.mjs <url> <out.png> [width] [height] [lang] [mobile]
//   e.g. node tools/screenshot_dashboard.mjs http://192.168.100.23:8095/ docs/images/dashboard.png 1200 1350 en
// Needs Node 22+ (global WebSocket) and Chrome. Set CHROME to override the binary path.
import { spawn } from 'node:child_process';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const [url, out, w = '1200', h = '1350', lang = 'en', mobile = ''] = process.argv.slice(2);
const scale = +(process.env.SCALE || 2);   // SCALE=1 for an exact-size image (the 512x512 tile icon)
if (!url || !out) {
  console.error('usage: screenshot_dashboard.mjs <url> <out.png> [width] [height] [lang] [mobile]');
  process.exit(1);
}
const chromePath = process.env.CHROME || 'C:/Program Files/Google/Chrome/Application/chrome.exe';
const port = 9333;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const chrome = spawn(chromePath, [
  '--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run',
  `--remote-debugging-port=${port}`, `--user-data-dir=${mkdtempSync(join(tmpdir(), 'omnipad-shot-'))}`,
  'about:blank',
], { stdio: 'ignore' });

try {
  let targets;
  for (let i = 0; i < 50 && !targets; i++) {
    try { targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json(); } catch { await sleep(200); }
  }
  const page = targets.find((t) => t.type === 'page');
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((r) => ws.addEventListener('open', r, { once: true }));

  let nextId = 0;
  const pending = new Map();
  ws.addEventListener('message', (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.id && pending.has(msg.id)) { pending.get(msg.id)(msg); pending.delete(msg.id); }
  });
  const send = (method, params = {}) => new Promise((resolve, reject) => {
    const id = ++nextId;
    pending.set(id, (m) => (m.error ? reject(new Error(`${method}: ${m.error.message}`)) : resolve(m.result)));
    ws.send(JSON.stringify({ id, method, params }));
  });

  const isMobile = mobile === 'mobile';
  await send('Emulation.setDeviceMetricsOverride', { width: +w, height: +h, deviceScaleFactor: scale, mobile: isMobile });
  await send('Emulation.setFocusEmulationEnabled', { enabled: true });
  await send('Emulation.setLocaleOverride', { locale: lang === 'pt' ? 'pt-BR' : 'en-US' });
  await send('Page.enable');
  await send('Page.navigate', { url });
  await sleep(1500);
  // the page keeps its language in localStorage; set it explicitly and reload once
  await send('Runtime.evaluate', { expression: `localStorage.setItem('omnipad.lang', '${lang === 'pt' ? 'pt' : 'en'}'); localStorage.setItem('omnipad.slot', '0'); location.reload();` });
  await sleep(6000);
  const shot = await send('Page.captureScreenshot', { format: 'png' });
  writeFileSync(out, Buffer.from(shot.data, 'base64'));
  console.log(`saved ${out}`);
  ws.close();
} finally {
  chrome.kill();
}
