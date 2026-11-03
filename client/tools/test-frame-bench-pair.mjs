import assert from 'node:assert/strict';
import { judgePair } from './frame-bench-pair.mjs';
const budget = { frameP95Ms: 20, frameP99Ms: 33, managedAllocBytesPerFrame: 0, gc0Delta: 0, drawCalls: 120, triangles: 180000, particles: 256, materials: 24, stageP95Ms: { input: .5, sync: 1.5, predict: 1, fx: 4, audio: 1, draw: 10, overlay: 1, hud: 1 } };
function fixture(role) {
 const buildKind = role === 'release' ? 'release' : 'development';
 const sample = { machine: 'reference', cpu: 'cpu', gpu: 'gpu', driver: 'driver', unityVersion: '2022.3.62t16', resolution: '1920x1080', qualityTier: '2', scene: 'bench-4p60sheep', scriptingBackend: 'mono', evidenceRole: role, buildKind, mechanism: 'player', sampleFrames: '600', warmupFrames: '120', verdict: 'PASS', frameP95Ms: 4, frameP99Ms: 6, managedAllocBytesPerFrame: role === 'release' ? -1 : 0, gc0Delta: 0, drawCalls: 60, triangles: 40000, particles: 256, materials: 8, budget, stageP95: Object.fromEntries(Object.keys(budget.stageP95Ms).map(k => [k, .01])), graphics: { completedSamples: 600, missingSamples: 0, pixelCoverage: .5 }, alloc: { source: role === 'release' ? 'unmeasured' : 'engine-counter:Memory/GC Allocated In Frame', unmeasurableWindows: role === 'release' ? 600 : 0 } };
 return { evidenceRole: role, buildKind, machine: 'reference', cpu: 'cpu', gpu: 'gpu', driver: 'driver', unityVersion: '2022.3.62t16', resolution: '1920x1080', qualityTier: '2', scene: 'bench-4p60sheep', scriptingBackend: 'mono', sourceFingerprint: 'a'.repeat(64), playerSha256: (role === 'release' ? 'b' : 'c').repeat(64), runs: 3, warmupFrames: 120, sampleFrames: 600, samples: [structuredClone(sample), structuredClone(sample), structuredClone(sample)], budget, emptyControl: { belowBudget: true }, verdict: 'PASS' };
}
const pair = () => [fixture('release'), fixture('allocation')];
assert.equal(judgePair(...pair()).verdict, 'PASS');
for (const mutate of [
 ([r,d]) => { d.sourceFingerprint = 'd'.repeat(64); },
 ([r,d]) => { d.buildKind = 'release'; },
 ([r,d]) => { r.samples[1].graphics.completedSamples = 599; },
 ([r,d]) => { d.samples[0].managedAllocBytesPerFrame = -1; },
 ([r,d]) => { d.samples[0].alloc.source = 'gc-heap'; },
 ([r,d]) => { delete d.samples[0].stageP95.hud; },
 ([r,d]) => { r.samples.pop(); },
 ([r,d]) => { r.emptyControl.belowBudget = false; },
 ([r,d]) => { r.samples[0].verdict = 'UNVERIFIED'; },
 ([r,d]) => { r.budget = structuredClone(r.budget); r.budget.drawCalls = 999; },
]) { const p = pair(); mutate(p); assert.notEqual(judgePair(...p).verdict, 'PASS'); }
for (const mutate of [
 ([r,d]) => { r.samples[0].drawCalls = 121; },
 ([r,d]) => { d.samples[0].managedAllocBytesPerFrame = 1; },
 ([r,d]) => { r.samples[0].gc0Delta = 1; },
 ([r,d]) => { d.samples[0].stageP95.draw = 10.1; },
 ([r,d]) => { r.samples[0].frameP95Ms = 21; d.samples[0].managedAllocBytesPerFrame = -1; },
]) { const p = pair(); mutate(p); assert.equal(judgePair(...p).verdict, 'FAIL'); }
console.log('FRAME-BENCH PAIR CONTRACT OK: positive + 15 negative fixtures');
