import { describe, expect, it } from 'vitest';

import { createMaterials } from './materials.ts';
import {
  RELOAD_DIP_RAD,
  SWITCH_DIP_MS,
  createViewModelModel,
  reloadDipRad,
  switchDip,
  walkSway,
} from './viewmodelModel.ts';

describe('第一人称视模型', () => {
  it('三把武器共用一套材质且独立场景/相机', () => {
    const materials = createMaterials();
    const model = createViewModelModel(materials);
    try {
      expect(model.materialCount).toBeLessThanOrEqual(2);
      expect(model.scene.getObjectByName('viewmodel-weapon-0')?.visible).toBe(true);
      expect(model.scene.getObjectByName('viewmodel-weapon-1')?.visible).toBe(false);
      model.setWeapon(1);
      expect(model.scene.getObjectByName('viewmodel-weapon-0')?.visible).toBe(false);
      expect(model.scene.getObjectByName('viewmodel-weapon-1')?.visible).toBe(true);
      expect(model.camera.parent).toBeNull();
    } finally {
      model.dispose();
      materials.dispose();
    }
  });

  it('开火后坐与待机呼吸给出不同姿态', () => {
    const materials = createMaterials();
    const model = createViewModelModel(materials);
    try {
      model.update(16.7);
      const idlePitch = model.root.rotation.x;
      model.triggerFire();
      model.update(16.7);
      expect(model.root.rotation.x).toBeGreaterThan(idlePitch);
      expect(model.root.position.z).toBeGreaterThan(0);
      for (let i = 0; i < 120; i += 1) model.update(16.7);
      expect(Math.abs(model.root.rotation.x - idlePitch)).toBeLessThan(0.1);
    } finally {
      model.dispose();
      materials.dispose();
    }
  });

  it('换弹与切枪的关键帧曲线端点归零', () => {
    expect(reloadDipRad(0)).toBe(0);
    expect(reloadDipRad(1)).toBeCloseTo(0, 6);
    expect(reloadDipRad(0.5)).toBeCloseTo(RELOAD_DIP_RAD, 6);
    expect(switchDip(0)).toBe(0);
    expect(switchDip(SWITCH_DIP_MS)).toBeCloseTo(0, 6);
    expect(switchDip(SWITCH_DIP_MS / 2)).toBeCloseTo(1, 6);
    expect(walkSway(0, 1)).toBeCloseTo(0, 10);
    expect(Math.abs(walkSway(Math.PI / 2, 1))).toBeGreaterThan(0);
  });
});
