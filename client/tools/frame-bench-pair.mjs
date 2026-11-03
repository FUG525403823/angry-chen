import fs from 'node:fs';
import { pathToFileURL } from 'node:url';

// Full P0 evidence is a pair, never a relabelled development player.
export function judgePair(release, allocation) {
  const failures = [], missing = [];
  const require = (ok, why) => { if (!ok) missing.push(why); };
  const hex = x => /^[a-f0-9]{64}$/.test(x ?? '');
  require(release.evidenceRole === 'release' && release.buildKind === 'release', 'release role/build');
  require(allocation.evidenceRole === 'allocation' && allocation.buildKind === 'development', 'allocation role/build');
  require(hex(release.sourceFingerprint) && release.sourceFingerprint === allocation.sourceFingerprint, 'same build-time source fingerprint');
  require(hex(release.playerSha256) && hex(allocation.playerSha256), 'recorded player launcher hashes (Mono launchers may be identical)');
  for (const k of ['machine', 'cpu', 'gpu', 'driver', 'unityVersion', 'resolution', 'qualityTier', 'scene', 'scriptingBackend'])
    require(release[k] != null && release[k] === allocation[k], `same ${k}`);
  require(release.resolution === '1920x1080' && String(release.qualityTier) === '2', 'frozen high quality 1920x1080');
  const frozen = { frameP95Ms: 20, frameP99Ms: 33, managedAllocBytesPerFrame: 0, gc0Delta: 0, drawCalls: 120, triangles: 180000, particles: 256, materials: 24 };
  const stageBudget = { input: 0.5, sync: 1.5, predict: 1, fx: 4, audio: 1, draw: 10, overlay: 1, hud: 1 };
  for (const summary of [release, allocation]) {
    for (const [k, v] of Object.entries(frozen)) require(summary.budget?.[k] === v, `frozen budget ${k}`);
    for (const [k, v] of Object.entries(stageBudget)) require(summary.budget?.stageP95Ms?.[k] === v, `frozen stage budget ${k}`);
  }
  const metrics = { release: ['frameP95Ms', 'frameP99Ms', 'gc0Delta', 'drawCalls', 'triangles', 'particles', 'materials'], allocation: ['managedAllocBytesPerFrame', 'gc0Delta'] };
  for (const [role, summary] of [['release', release], ['allocation', allocation]]) {
    require(Number(summary.runs) === 3 && Number(summary.warmupFrames) === 120 && Number(summary.sampleFrames) === 600, `${role} 3x120+600`);
    require(summary.samples?.length === 3, `${role} all raw runs`);
    if (summary.verdict === 'FAIL') failures.push(`${role} summary FAIL`);
    require(summary.emptyControl?.belowBudget === true, `${role} empty floor`);
    for (const [i, s] of (summary.samples ?? []).entries()) {
      const prefix = `${role}[${i + 1}]`;
      require(s.evidenceRole === role && s.buildKind === summary.buildKind && s.mechanism === 'player', `${prefix} runtime identity`);
      for (const k of ['machine', 'cpu', 'gpu', 'driver', 'unityVersion', 'resolution', 'qualityTier', 'scene', 'scriptingBackend'])
        require(s[k] === summary[k], `${prefix} stable ${k}`);
      require(Number(s.sampleFrames) === 600 && Number(s.warmupFrames) === 120, `${prefix} sample window`);
      if (s.verdict === 'FAIL') failures.push(`${prefix} FAIL`);
      require(s.verdict === 'PASS', `${prefix} scoped verdict`);
      if (role === 'release') {
        require(s.graphics?.completedSamples === 600 && s.graphics?.missingSamples === 0, `${prefix} completed graphics coverage`);
        require(s.graphics?.pixelCoverage > 0, `${prefix} raster`);
      } else {
        require(s.alloc?.source?.startsWith('engine-counter:') && s.alloc?.unmeasurableWindows === 0, `${prefix} exact main-window allocation`);
      }
      for (const k of metrics[role]) {
        require(Number.isFinite(s[k]) && s[k] >= 0 && Number.isFinite(s.budget?.[k]), `${prefix} measured ${k}`);
        require(s.budget?.[k] === summary.budget?.[k], `${prefix} stable budget ${k}`);
        if (s[k] > s.budget?.[k]) failures.push(`${prefix} ${k}=${s[k]} > ${s.budget[k]}`);
      }
      if (role === 'allocation') {
        const stages = Object.entries(s.budget?.stageP95Ms ?? {});
        require(stages.length === 8, `${prefix} eight stages`);
        for (const [k, b] of stages) {
          require(Number.isFinite(s.stageP95?.[k]) && s.stageP95[k] > 0 && b === summary.budget?.stageP95Ms?.[k], `${prefix} measured stable stage ${k}`);
          if (s.stageP95?.[k] > b) failures.push(`${prefix} stage ${k}=${s.stageP95[k]} > ${b}`);
        }
      }
    }
  }
  const verdict = failures.length ? 'FAIL' : missing.length ? 'UNVERIFIED' : 'PASS';
  return { verdict, exitCode: verdict === 'PASS' ? 0 : verdict === 'FAIL' ? 1 : 2, failures, missing,
    scope: 'release: frame/graphics/sampling-interval GC; same-source development: exact synthetic-input + GameLoop.Frame allocation and eight stages',
    doesNotProve: ['release exact allocation', 'whole-engine-frame zero allocation'],
    sourceFingerprint: release.sourceFingerprint, releasePlayerSha256: release.playerSha256, developmentPlayerSha256: allocation.playerSha256 };
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const [r, d, out] = process.argv.slice(2);
  if (!r || !d || !out) throw new Error('Usage: node client/tools/frame-bench-pair.mjs release-summary.json development-summary.json paired.json');
  const result = judgePair(JSON.parse(fs.readFileSync(r, 'utf8').replace(/^\uFEFF/, '')), JSON.parse(fs.readFileSync(d, 'utf8').replace(/^\uFEFF/, '')));
  fs.writeFileSync(out, JSON.stringify(result, null, 2) + '\n');
  console.log(`FRAME-BENCH PAIRED ${result.verdict}`);
  process.exitCode = result.exitCode;
}
