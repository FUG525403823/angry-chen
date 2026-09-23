import { describe, expect, it } from 'vitest';

import { SHEEP_HIT, SHEEP_ORDER } from '@ac/shared';

import {
  SHEEP_BODY_BOX,
  SHEEP_CROWN_BOX,
  SHEEP_FORM_SCALE,
  SHEEP_HEAD_BOX,
  WOOL_CLUSTER_RADIUS_M,
  WOOL_CLUSTER_SCALE_MAX,
  WOOL_SPREAD_X_M,
  WOOL_SPREAD_Y_HALF_M,
  WOOL_SPREAD_Z_M,
} from '../render/sheepModel.ts';

/**
 * 跨层一致性测试（放在 src/test：render 层禁止 import @ac/shared，见 eslint P04 §5.1）。
 *
 * 命中体口径必须跟着渲染模型走：SHEEP_HIT（@ac/shared）就是渲染盒体本身 —— 躯干盒 x/ z 取羊毛团轮廓
 * （WOOL_SPREAD_* + 团半径），y 取 [0, topM]；头盒是躯干前方那个独立盒子，羊王还要连冠盒一起盖上。
 * 任一侧单独改动都会让"看着在羊身上却打不中、描头没伤害"复现（真人试玩反馈）。
 */
describe('羊命中体与渲染模型对齐（SHEEP_HIT ↔ sheepModel）', () => {
  it('形体数量与羊种数量一致（数组顺序即 SheepKind 顺序）', () => {
    expect(SHEEP_FORM_SCALE.length).toBe(SHEEP_ORDER.length);
  });

  for (let index = 0; index < SHEEP_ORDER.length; index += 1) {
    const kind = SHEEP_ORDER[index];
    it(String(kind) + '：命中盒覆盖渲染的躯干盒 / 羊毛轮廓 / 头盒（含羊王冠）', () => {
      if (kind === undefined) throw new Error('kind missing');
      const scale = SHEEP_FORM_SCALE[index] ?? 1;
      const profile = SHEEP_HIT[kind];
      const isKing = kind === 'king';

      const woolRadiusM = WOOL_CLUSTER_RADIUS_M * WOOL_CLUSTER_SCALE_MAX;
      const bodyHalfWidthM =
        Math.max(SHEEP_BODY_BOX.sizeX / 2, WOOL_SPREAD_X_M + woolRadiusM) * scale;
      const bodyHalfDepthM =
        Math.max(SHEEP_BODY_BOX.sizeZ / 2, WOOL_SPREAD_Z_M + woolRadiusM) * scale;
      const bodyTopM = (SHEEP_BODY_BOX.y + SHEEP_BODY_BOX.sizeY / 2) * scale;
      const woolTopM = (SHEEP_BODY_BOX.y + WOOL_SPREAD_Y_HALF_M + woolRadiusM) * scale;
      const headHalfWidthM = (SHEEP_HEAD_BOX.sizeX / 2) * scale;
      const headTopM = (SHEEP_HEAD_BOX.y + SHEEP_HEAD_BOX.sizeY / 2) * scale;
      const headBottomM = (SHEEP_HEAD_BOX.y - SHEEP_HEAD_BOX.sizeY / 2) * scale;
      const headFrontM = (SHEEP_HEAD_BOX.z + SHEEP_HEAD_BOX.sizeZ / 2) * scale;
      const headBackM = (SHEEP_HEAD_BOX.z - SHEEP_HEAD_BOX.sizeZ / 2) * scale;
      const crownTopM = (SHEEP_CROWN_BOX.y + SHEEP_CROWN_BOX.sizeY / 2) * scale;

      // 躯干盒水平范围必须盖住"看得见的羊毛轮廓"，否则描羊毛边缘落空（体积太小的根因）
      expect(profile.halfWidthM).toBeGreaterThanOrEqual(bodyHalfWidthM);
      expect(profile.halfDepthM).toBeGreaterThanOrEqual(bodyHalfDepthM);
      // 盒顶：躯干、羊毛、头（羊王再加冠）都必须在里面，否则上半身整段落空
      expect(profile.topM).toBeGreaterThanOrEqual(bodyTopM);
      expect(profile.topM).toBeGreaterThanOrEqual(woolTopM);
      expect(profile.topM).toBeGreaterThanOrEqual(headTopM);
      if (isKing) expect(profile.topM).toBeGreaterThanOrEqual(crownTopM);

      // 头盒（含羊王冠的竖直范围）必须完整落在命中头的区间里
      expect(profile.headHalfWidthM).toBeGreaterThanOrEqual(headHalfWidthM);
      expect(profile.headMinYM).toBeLessThanOrEqual(headBottomM);
      expect(profile.headMaxYM).toBeGreaterThanOrEqual(headTopM);
      if (isKing) expect(profile.headMaxYM).toBeGreaterThanOrEqual(crownTopM);
      expect(profile.headMinZM).toBeLessThanOrEqual(headBackM);
      expect(profile.headMaxZM).toBeGreaterThanOrEqual(headFrontM);
      // 头盒要探出躯干盒之外（前伸段），这正是旧胶囊整发落空的那一段
      expect(profile.headMaxZM).toBeGreaterThan(profile.halfDepthM);

      // 高度阈值：头/躯干分界落在头盒下沿与头顶之间，躯干/四肢分界低于它
      expect(profile.headMinM).toBeGreaterThanOrEqual(headBottomM);
      expect(profile.headMinM).toBeLessThanOrEqual(headTopM);
      expect(profile.torsoMinM).toBeLessThan(profile.headMinM);
      expect(profile.torsoMinM).toBeGreaterThan(0);
    });
  }
});
