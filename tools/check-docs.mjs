#!/usr/bin/env node
// 文档门禁（v2）：校验必需文档、v2 双链（S/C 链）的机器可校验部分、docs 内相对链接与素材纪律。
// v1 的 P01–P10 计划链与 packages/** 已随重构移出仓库，历史实现见备份仓库 angry-chen-bak。
import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const errors = [];
const warnings = [];
const notes = [];
const fail = (msg) => errors.push(msg);

const REQUIRED_DOCS = [
  'docs/README.md',
  'docs/02-需求分析.md',
  'docs/03-技术选型-v2.md',
  'docs/00-共识/CONTEXT.md',
  'docs/00-共识/工程约定.md',
  'docs/00-共识/计划文档模板.md',
  'docs/00-共识/ADR/ADR-008-服务端语言与客户端引擎变更.md',
  'docs/00-共识/ADR/ADR-009-UDP传输与协议重构.md',
  'docs/00-共识/ADR/ADR-010-跨语言确定性与对拍.md',
  'docs/plans-v2/README.md',
];

const REQUIRED_SECTIONS = [
  '## 1. 目标',
  '## 2. 入口条件',
  '## 3. 交付物',
  '## 4. 任务清单',
  '## 5. 冻结契约',
  '## 6. 验证',
  '## 7. DoD（验收标准）',
  '## 8. 风险与回滚',
  '## 9. 移交物',
];

// 本地产物目录不参与链接与素材扫描
const SKIP_SCAN_DIRS = new Set([
  'node_modules',
  'Library',
  'Temp',
  'Obj',
  'Logs',
  'Build',
  'Builds',
  'UserSettings',
  'MemoryCaptures',
]);

function sectionOf(text, heading) {
  const i = text.indexOf('\n' + heading);
  if (i === -1) return null;
  const rest = text.slice(i + 1);
  const next = rest.indexOf('\n## ', 1);
  return next === -1 ? rest : rest.slice(0, next);
}

function walk(dir) {
  const out = [];
  for (const entry of readdirSync(dir)) {
    if (SKIP_SCAN_DIRS.has(entry) || entry.startsWith('.git')) continue;
    const full = join(dir, entry);
    if (statSync(full).isDirectory()) out.push(...walk(full));
    else out.push(full);
  }
  return out;
}

function checkRequiredDocs() {
  for (const rel of REQUIRED_DOCS) if (!existsSync(join(ROOT, rel))) fail('缺少必需文档：' + rel);
}

const PLANS_V2_DIR = join(ROOT, 'docs', 'plans-v2');
const INDEX = join(PLANS_V2_DIR, 'README.md');

function readV2Index() {
  if (!existsSync(INDEX)) {
    fail('缺少 docs/plans-v2/README.md');
    return { chains: [], order: [] };
  }
  const text = readFileSync(INDEX, 'utf8');
  const chains = [];
  const re = /<!--\s*chain:\s*(\S+)\s+([A-Z]+)\s+(\d+)\s*-->/g;
  let m;
  while ((m = re.exec(text)) !== null) chains.push({ dir: m[1], prefix: m[2], count: Number(m[3]) });
  const om = text.match(/<!--\s*order:\s*([^>]+?)\s*-->/);
  return { chains, order: om ? om[1].trim().split(/\s+/) : [] };
}

function checkPlansV2() {
  const { chains, order } = readV2Index();
  if (chains.length === 0) fail('docs/plans-v2/README.md 缺少 chain 声明注释');
  if (order.length === 0) fail('docs/plans-v2/README.md 缺少 order 执行顺序注释');
  const allIds = [];
  for (const chain of chains) {
    const dir = join(PLANS_V2_DIR, chain.dir);
    if (!existsSync(dir)) {
      fail('缺少 v2 计划目录：docs/plans-v2/' + chain.dir + '/');
      continue;
    }
    const named = new RegExp('^' + chain.prefix + '\\d{2}-.*\\.md$');
    const entries = readdirSync(dir);
    const files = entries.filter((f) => named.test(f)).sort();
    const odd = entries.filter((f) => !named.test(f) && !f.startsWith('.'));
    if (odd.length > 0) warnings.push('docs/plans-v2/' + chain.dir + '/ 命名不合规范：' + odd.join(', '));
    if (files.length !== chain.count) {
      fail('docs/plans-v2/' + chain.dir + '/：索引声明 ' + chain.count + ' 份，实际 ' + files.length + ' 份');
    }
    for (let i = 1; i <= chain.count; i += 1) {
      const id = chain.prefix + String(i).padStart(2, '0');
      allIds.push(id);
      const file = files.find((f) => f.startsWith(id));
      if (!file) {
        fail('缺少 v2 计划文档：docs/plans-v2/' + chain.dir + '/' + id + '-*.md');
        continue;
      }
      const rel = 'docs/plans-v2/' + chain.dir + '/' + file;
      const text = readFileSync(join(dir, file), 'utf8');
      if (!new RegExp('^> 里程碑：M' + id + '\\b', 'm').test(text)) {
        fail(rel + '：头行缺少里程碑标记 M' + id);
      }
      for (const sec of REQUIRED_SECTIONS) {
        if (!sectionOf(text, sec)) fail(rel + '：缺少必需章节 ' + sec);
      }
      const sec2 = sectionOf(text, '## 2. 入口条件') ?? '';
      const sec9 = sectionOf(text, '## 9. 移交物') ?? '';
      const expectOut = 'HANDOFF-' + id;
      const expectIn = 'HANDOFF-' + chain.prefix + String(i - 1).padStart(2, '0');
      const outMatch = sec9.match(/\*\*移交物 ID\*\*：\s*(HANDOFF-[SC]\d{2})/);
      if (!outMatch) fail(rel + '：§9 缺少移交物 ID 标记');
      else if (outMatch[1] !== expectOut) fail(rel + '：移交物 ID 应为 ' + expectOut + '，实际 ' + outMatch[1]);
      const inMatch = sec2.match(/\*\*入口条件\*\*：\s*(HANDOFF-[SC]\d{2})/);
      if (!inMatch) fail(rel + '：§2 缺少入口条件标记');
      else if (inMatch[1] !== expectIn) fail(rel + '：入口条件应为 ' + expectIn + '，实际 ' + inMatch[1]);
      if (/\bTBD\b|待定/.test(text)) warnings.push(rel + '：正文出现 TBD/待定');
    }
  }
  const dup = order.filter((id, i) => order.indexOf(id) !== i);
  if (dup.length > 0) fail('order 顺序表存在重复：' + [...new Set(dup)].join(', '));
  for (const id of allIds) if (!order.includes(id)) fail('order 顺序表缺少 ' + id);
  for (const id of order) if (!allIds.includes(id)) fail('order 顺序表含未知编号 ' + id);
  if (order.length !== allIds.length) {
    fail('order 顺序表长度 ' + order.length + ' 与 v2 计划总数 ' + allIds.length + ' 不一致');
  }
  for (const id of allIds) {
    const chain = chains.find((c) => id.startsWith(c.prefix));
    if (!chain) continue;
    const file = readdirSync(join(PLANS_V2_DIR, chain.dir)).find((f) => f.startsWith(id));
    if (!file) continue;
    const rel = 'docs/plans-v2/' + chain.dir + '/' + file;
    const text = readFileSync(join(PLANS_V2_DIR, chain.dir, file), 'utf8');
    const sec2 = (sectionOf(text, '## 2. 入口条件') ?? '').replace(/HANDOFF-[SC]\d{2}/g, '');
    const me = order.indexOf(id);
    for (const ref of new Set(sec2.match(/\b[SC]\d{2}\b/g) ?? [])) {
      if (!allIds.includes(ref)) {
        fail(rel + '：§2 引用了未知编号 ' + ref);
        continue;
      }
      if (order.indexOf(ref) >= me) fail(rel + '：§2 引用了顺序表中不早于自己的计划 ' + ref + '（前向依赖）');
    }
  }
  notes.push(
    'v2 双链：' +
      chains.map((c) => c.prefix + '×' + c.count).join(' + ') +
      '；执行顺序 ' +
      order.join(' → '),
  );
  return { count: allIds.length, chains, order };
}

function checkLinks() {
  const mds = walk(join(ROOT, 'docs')).filter((f) => f.endsWith('.md'));
  let total = 0;
  for (const file of mds) {
    const text = readFileSync(file, 'utf8');
    const rel = file.slice(ROOT.length + 1).replace(/\\/g, '/');
    for (const m of text.matchAll(/\]\(([^)\s]+)\)/g)) {
      const raw = m[1];
      if (/^(https?:|mailto:|#)/.test(raw)) continue;
      const target = raw.split('#')[0];
      if (!target) continue;
      total += 1;
      if (!existsSync(resolve(dirname(file), decodeURIComponent(target)))) {
        fail(rel + '：链接指向不存在的路径 "' + raw + '"');
      }
    }
  }
  notes.push('链接检查：扫描 ' + mds.length + ' 个文档，解析相对链接 ' + total + ' 条');
}

checkRequiredDocs();
const v2 = checkPlansV2();
checkLinks();

console.log('=== check-docs ===');
for (const n of notes) console.log('  · ' + n);
for (const w of warnings) console.log('  ! ' + w);
for (const e of errors) console.log('  ✗ ' + e);
if (errors.length === 0) {
  console.log('');
  console.log(
    'OK：v2 ' +
      v2.count +
      ' 份计划（' +
      v2.chains.map((c) => c.prefix).join('/') +
      ' 链） + ' +
      REQUIRED_DOCS.length +
      ' 份前置文档，线性链与链接校验通过。',
  );
  process.exit(0);
}
console.log('');
console.log('FAIL：' + errors.length + ' 处错误。');
process.exit(1);
