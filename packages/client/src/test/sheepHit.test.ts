import { describe, expect, it } from 'vitest';

import { SHEEP_HIT, SHEEP_ORDER } from '@ac/shared';

import { SHEEP_BODY_BOX, SHEEP_FORM_SCALE, SHEEP_HEAD_BOX } from '../render/sheepModel.ts';

/**
 * 跨层一致性测试（放在 src/test：render 层禁止 import @ac/shared，见 eslint P04 §5.1）。
 *
 * 命中体口径必须跟着渲染模型走：SHEEP_HIT（@ac/shared）是服务端射线判定用的胶囊半径 + 高度阈值，
 * 渲染体是 render/sheepModel.ts 的躯干盒 / 头盒 × SHEEP_FORM_SCALE。
 * 任一侧单独改动都会让"描头打不出伤害"或"看得见却打不中"复现（真人试玩反馈）。
 */
describe('羊命中体与渲染模型对齐（SHEEP_HIT ↔ sheepModel）', () => {
  it('形体数量与羊种数量一致（数组顺序即 SheepKind 顺序）', () => {
    expect(SHEEP_FORM_SCALE.length).toBe(SHEEP_ORDER.length);
  });

  for (let index = 0; index < SHEEP_ORDER.length; index += 1) {
    const kind = SHEEP_ORDER[index];
    it(String(kind) + '：命中胶囊覆盖渲染体的头盒与躯干盒', () => {
      if (kind === undefined) throw new Error('kind missing');
      const scale = SHEEP_FORM_SCALE[index] ?? 1;
      const profile = SHEEP_HIT[kind];

      const headTopM = (SHEEP_HEAD_BOX.y + SHEEP_HEAD_BOX.sizeY / 2) * scale;
      const headBottomM = (SHEEP_HEAD_BOX.y - SHEEP_HEAD_BOX.sizeY / 2) * scale;
      const bodyHalfWidthM = (SHEEP_BODY_BOX.sizeX / 2) * scale;
      const bodyTopM = (SHEEP_BODY_BOX.y + SHEEP_BODY_BOX.sizeY / 2) * scale;
      const bodyBottomM = (SHEEP_BODY_BOX.y - SHEEP_BODY_BOX.sizeY / 2) * scale;

      // 头顶必须在命中胶囊内，否则"瞄头顶"整段落空（旧口径通用 0.9m 的根因）
      expect(profile.topM).toBeGreaterThanOrEqual(headTopM);
      expect(profile.topM).toBeGreaterThanOrEqual(bodyTopM);
      // 躯干不能比胶囊宽，否则正面描身体侧面反而落空
      expect(profile.radiusM).toBeGreaterThanOrEqual(bodyHalfWidthM);
      // 胶囊不能退化：topM ≤ 2r 时轴段首尾颠倒，命中的是"两个球叠出的怪形状"（旧口径 0.9/0.5 就是）
      expect(profile.topM).toBeGreaterThan(2 * profile.radiusM);
      // 头部判定区间落在头盒里：不低于头盒底（避免上背算头），不高于头盒顶（避免头打不到）
      expect(profile.headMinM).toBeGreaterThanOrEqual(headBottomM);
      expect(profile.headMinM).toBeLessThanOrEqual(headTopM);
      // 躯干/四肢分界落在腿与躯干之间
      expect(profile.torsoMinM).toBeLessThan(profile.headMinM);
      expect(profile.torsoMinM).toBeGreaterThan(bodyBottomM - 0.15);
    });
  }
});
