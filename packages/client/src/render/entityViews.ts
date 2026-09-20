import * as THREE from 'three';

import type { ViewEntity } from '../net/state.ts';

export interface RenderPalette {
  readonly localPlayer: number;
  readonly remotePlayer: number;
  readonly sheep: number;
  readonly sheepRage: number;
  readonly emblem: number;
  readonly emblemCore: number;
  readonly projectile: number;
  readonly pickup: number;
}

export const DEFAULT_PALETTE: RenderPalette = Object.freeze({
  localPlayer: 0x4fc3f7,
  remotePlayer: 0xff8a65,
  sheep: 0xf1ece0,
  sheepRage: 0xd2402f,
  emblem: 0x14161c,
  emblemCore: 0xe8f4ff,
  projectile: 0xffe066,
  pickup: 0x9be36f,
});

export const COLORBLIND_PALETTE: RenderPalette = Object.freeze({
  localPlayer: 0x56b4e9,
  remotePlayer: 0xe69f00,
  sheep: 0xf0f0f0,
  sheepRage: 0x0072b2,
  emblem: 0x101216,
  emblemCore: 0xffffff,
  projectile: 0xf0e442,
  pickup: 0x009e73,
});

export interface EntityViews {
  sync(): void;
  readonly meshCount: number;
  readonly liveCount: number;
  setPalette(palette: RenderPalette): void;
  dispose(): void;
}

interface ViewNode {
  readonly root: THREE.Group;
  readonly body: THREE.Mesh;
  readonly bodyMaterial: THREE.MeshLambertMaterial;
  readonly head: THREE.Mesh | undefined;
  readonly mark: THREE.Mesh | undefined;
  kind: number;
  rage: boolean;
  seenFrame: number;
}

export function createEntityViews(
  scene: THREE.Scene,
  view: { forEachVisible(cb: (entity: ViewEntity) => void): void },
  localPlayerId: () => number,
  initialPalette: RenderPalette = DEFAULT_PALETTE,
): EntityViews {
  let palette = initialPalette;
  const pool = new Map<number, ViewNode>();
  let frame = 0;

  const bodyGeometry = new THREE.BoxGeometry(0.6, 1.4, 0.36);
  const headGeometry = new THREE.SphereGeometry(0.22, 12, 8);
  const sheepBodyGeometry = new THREE.BoxGeometry(1.1, 0.72, 0.72);
  const sheepHeadGeometry = new THREE.BoxGeometry(0.44, 0.44, 0.5);
  const markGeometry = new THREE.BoxGeometry(0.24, 0.24, 0.05);
  const coreGeometry = new THREE.BoxGeometry(0.1, 0.1, 0.07);
  const smallGeometry = new THREE.BoxGeometry(0.16, 0.16, 0.16);

  function createNode(entity: ViewEntity): ViewNode {
    const root = new THREE.Group();
    root.name = 'entity:' + String(entity.id);
    const bodyMaterial = new THREE.MeshLambertMaterial({ color: palette.remotePlayer });
    let head: THREE.Mesh | undefined;
    let mark: THREE.Mesh | undefined;

    if (entity.kind === 1) {
      const body = new THREE.Mesh(sheepBodyGeometry, bodyMaterial);
      body.position.y = 0.55;
      root.add(body);
      head = new THREE.Mesh(sheepHeadGeometry, bodyMaterial);
      head.position.set(0, 0.62, 0.62);
      root.add(head);
      mark = new THREE.Mesh(markGeometry, new THREE.MeshLambertMaterial({ color: palette.emblem }));
      mark.position.set(0, 0.02, 0.26);
      const core = new THREE.Mesh(
        coreGeometry,
        new THREE.MeshLambertMaterial({ color: palette.emblemCore, emissive: palette.emblemCore }),
      );
      core.position.z = 0.03;
      mark.add(core);
      head.add(mark);
    } else if (entity.kind === 2) {
      const body = new THREE.Mesh(smallGeometry, bodyMaterial);
      root.add(body);
    } else {
      const body = new THREE.Mesh(bodyGeometry, bodyMaterial);
      body.position.y = 0.7;
      root.add(body);
      head = new THREE.Mesh(headGeometry, bodyMaterial);
      head.position.y = 1.62;
      root.add(head);
    }

    scene.add(root);
    return {
      root,
      body: root.children[0] as THREE.Mesh,
      bodyMaterial,
      head,
      mark,
      kind: entity.kind,
      rage: false,
      seenFrame: 0,
    };
  }

  function colorFor(entity: ViewEntity, node: ViewNode): number {
    if (entity.kind === 1) return node.rage ? palette.sheepRage : palette.sheep;
    if (entity.kind === 2) return palette.projectile;
    if (entity.kind === 3) return palette.pickup;
    return entity.id === localPlayerId() ? palette.localPlayer : palette.remotePlayer;
  }

  return {
    sync(): void {
      frame += 1;
      view.forEachVisible((entity) => {
        const node = pool.get(entity.id) ?? createNode(entity);
        if (!pool.has(entity.id)) pool.set(entity.id, node);
        node.seenFrame = frame;
        node.root.position.set(entity.pos.x, entity.pos.y, entity.pos.z);
        node.root.rotation.y = entity.yaw;
        if (node.head !== undefined) node.head.rotation.x = entity.pitch;
        const rage = (entity.state & 1) === 1;
        if (rage !== node.rage || node.kind !== entity.kind) {
          node.rage = rage;
          node.kind = entity.kind;
        }
        const color = colorFor(entity, node);
        if (node.bodyMaterial.color.getHex() !== color) node.bodyMaterial.color.setHex(color);
        node.root.visible = !(entity.kind === 0 && entity.id === localPlayerId());
      });
      for (const node of pool.values()) {
        if (node.seenFrame !== frame) node.root.visible = false;
      }
    },
    get meshCount(): number {
      return pool.size;
    },
    get liveCount(): number {
      let live = 0;
      for (const node of pool.values()) if (node.seenFrame === frame) live += 1;
      return live;
    },
    setPalette(next: RenderPalette): void {
      palette = next;
      for (const node of pool.values()) {
        node.bodyMaterial.color.setHex(node.rage ? palette.sheepRage : palette.remotePlayer);
      }
    },
    dispose(): void {
      for (const node of pool.values()) scene.remove(node.root);
      pool.clear();
      bodyGeometry.dispose();
      headGeometry.dispose();
      sheepBodyGeometry.dispose();
      sheepHeadGeometry.dispose();
      markGeometry.dispose();
      coreGeometry.dispose();
      smallGeometry.dispose();
    },
  };
}
