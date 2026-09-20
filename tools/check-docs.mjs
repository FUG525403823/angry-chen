#!/usr/bin/env node
import { readFileSync, readdirSync, existsSync, statSync } from 'node:fs';
import { dirname, join, resolve, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const PLANS_DIR = join(ROOT, 'docs', 'plans');

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

const REQUIRED_DOCS = [
  'README.md',
  'docs/README.md',
  'docs/00-共识/CONTEXT.md',
  'docs/00-共识/工程约定.md',
  'docs/00-共识/计划文档模板.md',
  'docs/00-共识/ADR/ADR-001-技术栈与仓库形态.md',
  'docs/00-共识/ADR/ADR-002-网络模型与协议.md',
  'docs/00-共识/ADR/ADR-003-美术素材与问界标识策略.md',
  'docs/00-共识/ADR/ADR-004-部署与持久化形态.md',
  'docs/01-技术选型.md',
  'docs/02-需求分析.md',
];

const errors = [];
const warnings = [];
const notes = [];

function fail(msg) {
  errors.push(msg);
}

function planFiles() {
  let entries;
  try {
    entries = readdirSync(PLANS_DIR);
  } catch {
    fail(`缺少目录 docs/plans/`);
    return [];
  }
  const plans = entries.filter((f) => /^P\d+.*\.md$/.test(f)).sort();
  const unexpected = entries.filter((f) => !/^P\d+.*\.md$/.test(f));
  if (unexpected.length > 0) {
    warnings.push(`docs/plans/ 下存在不符合 P<NN>-*.md 命名的文件：${unexpected.join(', ')}`);
  }
  return plans;
}

function checkRequiredDocs() {
  for (const rel of REQUIRED_DOCS) {
    if (!existsSync(join(ROOT, rel))) fail(`缺少必需文档：${rel}`);
  }
  const adrDir = join(ROOT, 'docs', '00-共识', 'ADR');
  if (existsSync(adrDir)) {
    const adrs = readdirSync(adrDir).filter((f) => f.endsWith('.md'));
    if (adrs.length < 4) warnings.push(`ADR 数量少于 4（当前 ${adrs.length}）`);
  }
}

function sectionOf(text, heading) {
  const i = text.indexOf(`\n${heading}`);
  if (i === -1) return null;
  const rest = text.slice(i + 1);
  const next = rest.indexOf('\n## ', heading.length);
  return next === -1 ? rest : rest.slice(0, next);
}

function checkPlans(plans) {
  const expected = Array.from({ length: 10 }, (_, i) => `P${String(i + 1).padStart(2, '0')}`);
  const found = plans.map((f) => f.slice(0, 3));
  for (const id of expected) {
    if (!found.includes(id)) fail(`缺少计划文档：docs/plans/${id}-*.md`);
  }
  for (const id of found) {
    if (!expected.includes(id)) fail(`计划文档编号越界：${id}`);
  }
  if (new Set(found).size !== found.length) fail('计划文档存在重复编号');

  const handoffOut = new Map();
  const handoffIn = new Map();

  for (const id of expected) {
    const file = plans.find((f) => f.startsWith(id));
    if (!file) continue;
    const rel = `docs/plans/${file}`;
    const text = readFileSync(join(PLANS_DIR, file), 'utf8');

    if (!/^> 里程碑：M\d/m.test(text)) fail(`${rel}：缺少 "> 里程碑：M<n>" 头行`);
    if (!text.includes('预估') && !text.includes('人日')) warnings.push(`${rel}：缺少预估工作量`);

    for (const sec of REQUIRED_SECTIONS) {
      if (!sectionOf(text, sec)) fail(`${rel}：缺少必需章节 "${sec}"`);
    }

    const n = Number(id.slice(1));
    const outId = `HANDOFF-H${String(n).padStart(2, '0')}`;
    const inId = `HANDOFF-H${String(n - 1).padStart(2, '0')}`;

    const sec9 = sectionOf(text, '## 9. 移交物') ?? '';
    const sec2 = sectionOf(text, '## 2. 入口条件') ?? '';

    const outMatch = sec9.match(/\*\*移交物 ID\*\*：\s*(HANDOFF-H\d{2})/);
    if (!outMatch) fail(`${rel}：§9 缺少 "**移交物 ID**：HANDOFF-H<NN>" 标记`);
    else {
      handoffOut.set(id, outMatch[1]);
      if (outMatch[1] !== outId) fail(`${rel}：移交物 ID 应为 ${outId}，实际 ${outMatch[1]}`);
    }

    const inMatch = sec2.match(/\*\*入口条件\*\*：\s*(HANDOFF-H\d{2})/);
    if (!inMatch) fail(`${rel}：§2 缺少 "**入口条件**：HANDOFF-H<NN>" 标记`);
    else {
      handoffIn.set(id, inMatch[1]);
      if (inMatch[1] !== inId) fail(`${rel}：入口条件应为 ${inId}，实际 ${inMatch[1]}`);
    }

    if (/\bTBD\b|待定/.test(text)) {
      const inOpenQuestions = /开放问题/.test(text);
      if (!inOpenQuestions) warnings.push(`${rel}：正文出现 TBD/待定，冻结契约不应留有未决项`);
    }
  }

  for (let i = 1; i < 10; i += 1) {
    const a = `P${String(i).padStart(2, '0')}`;
    const b = `P${String(i + 1).padStart(2, '0')}`;
    const out = handoffOut.get(a);
    const inn = handoffIn.get(b);
    if (out && inn && out !== inn) {
      fail(`线性链断裂：${a} 的移交物 ${out} 与 ${b} 的入口条件 ${inn} 不一致`);
    }
  }
  notes.push(`线性链：HANDOFF-H00 → ${[...handoffOut.values()].join(' → ')}`);
}

function checkLinks() {
  const walk = (dir) => {
    const out = [];
    for (const entry of readdirSync(dir)) {
      if (entry === 'node_modules' || entry.startsWith('.git')) continue;
      const full = join(dir, entry);
      if (statSync(full).isDirectory()) out.push(...walk(full));
      else if (entry.endsWith('.md')) out.push(full);
    }
    return out;
  };

  const docs = walk(join(ROOT, 'docs')).concat([join(ROOT, 'README.md')]);
  let checked = 0;
  for (const file of docs) {
    const text = readFileSync(file, 'utf8');
    const rel = relative(ROOT, file);
    const re = /\[[^\]]*\]\(([^)]+)\)/g;
    let m;
    while ((m = re.exec(text)) !== null) {
      const raw = m[1].trim();
      if (/^(https?:|mailto:)/.test(raw)) continue;
      const target = raw.split('#')[0];
      if (target === '') continue;
      const resolved = resolve(dirname(file), decodeURIComponent(target));
      checked += 1;
      if (!existsSync(resolved)) fail(`${rel}：链接指向不存在的路径 "${raw}"`);
    }
  }
  notes.push(`链接检查：扫描 ${docs.length} 个文档，解析相对链接 ${checked} 条`);
}

function checkNoExternalAssets() {
  const banned = /\.(png|jpe?g|gif|webp|bmp|ico|mp3|wav|ogg|m4a|glb|gltf|fbx|obj|ktx2|hdr)$/i;
  const walk = (dir) => {
    const out = [];
    for (const entry of readdirSync(dir)) {
      if (entry === 'node_modules' || entry === '.git') continue;
      const full = join(dir, entry);
      if (statSync(full).isDirectory()) out.push(...walk(full));
      else out.push(full);
    }
    return out;
  };
  const offenders = walk(ROOT).filter((f) => banned.test(f));
  if (offenders.length > 0) {
    fail(`发现外部素材文件（违反 NFR-07）：${offenders.map((f) => relative(ROOT, f)).join(', ')}`);
  }
}

checkRequiredDocs();
const plans = planFiles();
checkPlans(plans);
checkLinks();
checkNoExternalAssets();

console.log('=== check-docs ===');
for (const n of notes) console.log(`  · ${n}`);
for (const w of warnings) console.log(`  ! ${w}`);
if (errors.length === 0) {
  console.log(`\nOK：${plans.length} 份计划文档 + ${REQUIRED_DOCS.length} 份前置文档，线性链与链接校验通过。`);
  process.exit(0);
}
console.log('');
for (const e of errors) console.log(`  ✗ ${e}`);
console.log(`\nFAIL：${errors.length} 处错误。`);
process.exit(1);
