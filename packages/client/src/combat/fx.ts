import { BoxGeometry, Group, Mesh, MeshBasicMaterial, type Scene } from 'three';

export const TRACER_POOL_SIZE = 16;
export const TRACER_MS = 70;
export const IMPACT_POOL_SIZE = 12;
export const IMPACT_MS = 140;
export const DAMAGE_NUMBER_MS = 900;

export interface CombatFx {
  readonly root: Group;
  spawnTracer(
    fromX: number,
    fromY: number,
    fromZ: number,
    toX: number,
    toY: number,
    toZ: number,
    local: boolean,
  ): void;
  spawnImpact(x: number, y: number, z: number): void;
  showHitMarker(headshot: boolean): void;
  showDamageNumber(x: number, y: number, z: number, value: number, headshot: boolean): void;
  update(dtMs: number): void;
  dispose(): void;
}

export function createCombatFx(scene: Scene, host: HTMLElement): CombatFx {
  const root = new Group();
  scene.add(root);
  const localMaterial = new MeshBasicMaterial({ color: 0xfff0b8, transparent: true, opacity: 0.7 });
  const serverMaterial = new MeshBasicMaterial({
    color: 0xff9d5c,
    transparent: true,
    opacity: 0.85,
  });
  const impactMaterial = new MeshBasicMaterial({
    color: 0xffd0a0,
    transparent: true,
    opacity: 0.9,
  });
  const tracerGeometry = new BoxGeometry(0.012, 0.012, 1);
  const impactGeometry = new BoxGeometry(0.09, 0.09, 0.09);

  interface Timed {
    mesh: Mesh;
    leftMs: number;
    totalMs: number;
  }
  const tracers: Timed[] = [];
  for (let i = 0; i < TRACER_POOL_SIZE; i += 1) {
    const mesh = new Mesh(tracerGeometry, i % 2 === 0 ? localMaterial : serverMaterial);
    mesh.visible = false;
    root.add(mesh);
    tracers.push({ mesh, leftMs: 0, totalMs: TRACER_MS });
  }
  const impacts: Timed[] = [];
  for (let i = 0; i < IMPACT_POOL_SIZE; i += 1) {
    const mesh = new Mesh(impactGeometry, impactMaterial);
    mesh.visible = false;
    root.add(mesh);
    impacts.push({ mesh, leftMs: 0, totalMs: IMPACT_MS });
  }

  const marker = document.createElement('div');
  marker.className = 'hit-marker';
  marker.style.cssText =
    'position:absolute;left:50%;top:50%;width:18px;height:18px;margin:-9px 0 0 -9px;pointer-events:none;opacity:0;transition:opacity 120ms linear;';
  const feed = document.createElement('div');
  feed.style.cssText =
    'position:absolute;right:12px;top:96px;display:flex;flex-direction:column;gap:4px;align-items:flex-end;pointer-events:none;font:12px/1.4 monospace;color:#ffdca8;text-shadow:0 1px 2px #000;';
  host.append(marker, feed);

  interface Floating {
    element: HTMLDivElement;
    leftMs: number;
  }
  const numbers: Floating[] = [];
  let tracerCursor = 0;
  let impactCursor = 0;

  function formatDamage(value: number): string {
    return value >= 10 ? String(Math.round(value)) : value.toFixed(1);
  }

  return {
    root,
    spawnTracer(fromX, fromY, fromZ, toX, toY, toZ, local): void {
      const slot = tracers[tracerCursor % TRACER_POOL_SIZE];
      tracerCursor += 1;
      if (slot === undefined) return;
      const dx = toX - fromX;
      const dy = toY - fromY;
      const dz = toZ - fromZ;
      const length = Math.sqrt(dx * dx + dy * dy + dz * dz);
      if (length <= 0.001) return;
      slot.mesh.position.set((fromX + toX) / 2, (fromY + toY) / 2, (fromZ + toZ) / 2);
      slot.mesh.scale.set(1, 1, length);
      slot.mesh.lookAt(toX, toY, toZ);
      slot.mesh.material = local ? localMaterial : serverMaterial;
      slot.mesh.visible = true;
      slot.leftMs = local ? TRACER_MS / 2 : TRACER_MS;
      slot.totalMs = slot.leftMs;
    },
    spawnImpact(x, y, z): void {
      const slot = impacts[impactCursor % IMPACT_POOL_SIZE];
      impactCursor += 1;
      if (slot === undefined) return;
      slot.mesh.position.set(x, y, z);
      slot.mesh.visible = true;
      slot.leftMs = IMPACT_MS;
      slot.totalMs = IMPACT_MS;
    },
    showHitMarker(headshot): void {
      marker.textContent = headshot ? '✕' : '✳';
      marker.style.color = headshot ? '#ff6a4d' : '#ffe9b0';
      marker.style.font = '16px/18px monospace';
      marker.style.textAlign = 'center';
      marker.style.opacity = '1';
      window.setTimeout(() => {
        marker.style.opacity = '0';
      }, 90);
    },
    showDamageNumber(x, y, z, value, headshot): void {
      const element = document.createElement('div');
      element.textContent = (headshot ? '✕ ' : '') + formatDamage(value);
      element.style.cssText =
        'position:absolute;transform:translate(-50%,-50%);font:12px/1 monospace;color:' +
        (headshot ? '#ff7a5c' : '#ffe9b0') +
        ';text-shadow:0 1px 2px #000;pointer-events:none;';
      element.dataset['world'] = x.toFixed(3) + ' ' + y.toFixed(3) + ' ' + z.toFixed(3);
      host.append(element);
      numbers.push({ element, leftMs: DAMAGE_NUMBER_MS });
    },
    update(dtMs): void {
      for (const tracer of tracers) {
        if (tracer.leftMs <= 0) continue;
        tracer.leftMs -= dtMs;
        if (tracer.leftMs <= 0) {
          tracer.mesh.visible = false;
          continue;
        }
        const material = tracer.mesh.material;
        if (material instanceof MeshBasicMaterial) {
          material.opacity = 0.85 * (tracer.leftMs / tracer.totalMs);
        }
      }
      for (const impact of impacts) {
        if (impact.leftMs <= 0) continue;
        impact.leftMs -= dtMs;
        if (impact.leftMs <= 0) {
          impact.mesh.visible = false;
          continue;
        }
        const scale = 1 + (1 - impact.leftMs / impact.totalMs) * 1.6;
        impact.mesh.scale.setScalar(scale);
      }
      for (let i = numbers.length - 1; i >= 0; i -= 1) {
        const item = numbers[i];
        if (item === undefined) continue;
        item.leftMs -= dtMs;
        if (item.leftMs > 0) continue;
        item.element.remove();
        numbers.splice(i, 1);
      }
    },
    dispose(): void {
      for (const item of numbers) item.element.remove();
      numbers.length = 0;
      marker.remove();
      feed.remove();
      scene.remove(root);
      tracerGeometry.dispose();
      impactGeometry.dispose();
      localMaterial.dispose();
      serverMaterial.dispose();
      impactMaterial.dispose();
    },
  };
}
