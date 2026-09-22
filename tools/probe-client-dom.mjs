#!/usr/bin/env node
// O06 探针：用最小假 DOM 驱动真实 hud.ts，统计「每帧 DOM 写入次数」。
// 无浏览器可观测 GC/帧 p95，故本探针作为可复现的代理指标（详见 docs/evidence/client-frame-alloc.md）。
// 用法：node tools/probe-client-dom.mjs [--frames 600]
import { createCombatHud, createHud } from '../packages/client/src/ui/hud.ts';

function createFakeStyle() {
  const values = new Map();
  return {
    setProperty(name, value) {
      values.set(name, String(value));
    },
    getPropertyValue(name) {
      return values.get(name) ?? '';
    },
  };
}

function createFakeNode() {
  const classes = new Set();
  const node = {
    textContent: '',
    id: '',
    style: createFakeStyle(),
    children: [],
    parent: null,
    classList: {
      add: (...names) => {
        for (const name of names) classes.add(name);
      },
      remove: (...names) => {
        for (const name of names) classes.delete(name);
      },
      toggle: (name, force) => {
        const next = force ?? !classes.has(name);
        if (next) classes.add(name);
        else classes.delete(name);
        return next;
      },
      contains: (name) => classes.has(name),
    },
    append(...nodes) {
      for (const child of nodes) {
        child.parent = node;
        node.children.push(child);
      }
    },
    remove() {
      const parent = node.parent;
      if (parent === null) return;
      const index = parent.children.indexOf(node);
      if (index >= 0) parent.children.splice(index, 1);
      node.parent = null;
    },
    setAttribute() {},
  };
  Object.defineProperty(node, 'className', {
    configurable: true,
    get: () => [...classes].join(' '),
    set: (value) => {
      classes.clear();
      for (const name of String(value).split(' ')) if (name !== '') classes.add(name);
    },
  });
  return node;
}

function installDom() {
  globalThis.document = {
    createElement: () => createFakeNode(),
    body: createFakeNode(),
  };
}

function collect(node, className, out = []) {
  if (node.className.split(' ').includes(className)) out.push(node);
  for (const child of node.children) collect(child, className, out);
  return out;
}

function countStyle(node, property, stats, label) {
  let value = '';
  Object.defineProperty(node.style, property, {
    configurable: true,
    get: () => value,
    set: (next) => {
      stats.total += 1;
      stats.byLabel.set(label, (stats.byLabel.get(label) ?? 0) + 1);
      value = String(next);
    },
  });
}

function countText(node, stats, label) {
  let value = '';
  Object.defineProperty(node, 'textContent', {
    configurable: true,
    get: () => value,
    set: (next) => {
      stats.total += 1;
      stats.byLabel.set(label, (stats.byLabel.get(label) ?? 0) + 1);
      value = String(next);
    },
  });
}

function main() {
  const args = process.argv.slice(2);
  const framesArg = args.indexOf('--frames');
  const frames = framesArg >= 0 ? Number(args[framesArg + 1]) : 600;
  installDom();
  const root = document.createElement('div');
  const banner = document.createElement('div');
  const hud = createHud(root, banner);
  const combat = createCombatHud(root);
  const stats = { total: 0, byLabel: new Map() };
  countText(collect(root, 'hud-line')[0], stats, 'line.textContent');
  countText(collect(root, 'hud-stats')[0], stats, 'stats.textContent');
  countStyle(collect(root, 'hud-health-fill')[0], 'width', stats, 'healthFill.width');
  countStyle(collect(root, 'hud-health-after')[0], 'width', stats, 'healthAfter.width');
  countText(collect(root, 'hud-health-text')[0], stats, 'healthText');
  countStyle(collect(root, 'hud-armor-fill')[0], 'width', stats, 'armorFill.width');
  countStyle(collect(root, 'hud-armor-fill')[0], 'display', stats, 'armorFill.display');
  countText(collect(root, 'hud-ammo-text')[0], stats, 'ammoText');
  countText(collect(root, 'hud-ammo-reserve')[0], stats, 'ammoReserve');
  countStyle(collect(root, 'hud-rage-fill')[0], 'width', stats, 'rageFill.width');
  countText(collect(root, 'hud-rage-text')[0], stats, 'rageText');
  countStyle(collect(root, 'hud-reload-ring')[0], 'opacity', stats, 'reloadRing.opacity');
  const sample = {
    spreadDeg: 1,
    hpRatio: 1,
    armorRatio: 0.5,
    mag: 10,
    magSize: 10,
    reserve: 60,
    reloadRatio: 0,
    rage: 0,
    rageLeftMs: 0,
    downed: false,
    reviveRatio: undefined,
    weaponName: '手枪',
    wave: 1,
    bossHpRatio: undefined,
  };
  const update = {
    local: undefined,
    stats: { snapshotsPerSec: 19.9, inboundBytesPerSec: 100, rttMs: 40 },
    fps: 60,
    liveEntities: 12,
    localPos: { x: 1, y: 0, z: 2 },
  };
  for (let frame = 0; frame < frames; frame += 1) {
    sample.hpRatio = 0.4 + (0.2 * Math.floor(frame / 5)) / 5;
    sample.spreadDeg = 1 + (frame % 7) * 0.05;
    sample.mag = 10 - (frame % 11);
    sample.rage = (frame % 40) * 2.5;
    sample.reloadRatio = frame % 60 < 10 ? (frame % 10) / 10 : 0;
    sample.downed = false;
    hud.update(update);
    combat.set(sample);
  }
  console.log(
    'probe-client-dom：' +
      String(frames) +
      ' 帧（40 帧一个血量台阶、准星每帧微动、弹匣 11 帧一循环）',
  );
  console.log(
    '  DOM 写入合计：' +
      String(stats.total) +
      ' 次（' +
      (stats.total / frames).toFixed(3) +
      ' 次/帧）',
  );
  for (const [label, count] of [...stats.byLabel].sort((a, b) => b[1] - a[1])) {
    console.log('    ' + label.padEnd(22) + String(count));
  }
  hud.dispose();
  combat.dispose();
}

main();
