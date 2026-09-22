export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

export const TWO_PI = Math.PI * 2;

export function createVec3(x = 0, y = 0, z = 0): Vec3 {
  return { x, y, z };
}

export function setVec3(out: Vec3, x: number, y: number, z: number): Vec3 {
  out.x = x;
  out.y = y;
  out.z = z;
  return out;
}

export function copyVec3(out: Vec3, a: Vec3): Vec3 {
  return setVec3(out, a.x, a.y, a.z);
}

export function addScaled(out: Vec3, base: Vec3, dir: Vec3, scale: number): Vec3 {
  out.x = base.x + dir.x * scale;
  out.y = base.y + dir.y * scale;
  out.z = base.z + dir.z * scale;
  return out;
}

export function lengthSq(a: Vec3): number {
  return a.x * a.x + a.y * a.y + a.z * a.z;
}

export function length(a: Vec3): number {
  return Math.sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}

export function normalize(out: Vec3, a: Vec3): Vec3 {
  const len = Math.sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
  if (len === 0) return setVec3(out, 0, 0, 0);
  return setVec3(out, a.x / len, a.y / len, a.z / len);
}

export function clamp(value: number, lo: number, hi: number): number {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

export function clamp01(value: number): number {
  return clamp(value, 0, 1);
}

export function wrapAngle(angle: number): number {
  if (!Number.isFinite(angle)) return 0;
  let a = angle % TWO_PI;
  if (a > Math.PI) a -= TWO_PI;
  else if (a < -Math.PI) a += TWO_PI;
  return a;
}

export function forwardFromYaw(out: Vec3, yaw: number): Vec3 {
  return setVec3(out, Math.sin(yaw), 0, Math.cos(yaw));
}

/**
 * 注意方向约定：以 three.js 右手系看，本函数返回的是模型坐标系的**左手侧**
 * （cross(forwardFromYaw, rightFromYaw) = +Y，而右手系的 forward × right 应为 -Y）。
 * 也就是说 yaw 把模型 +Z 旋到前向，(forward, right) 构成镜像基。
 * 客户端视图侧用 `SIM_TO_VIEW_YAW_OFFSET` 把相机转到该前向，并把横移轴反号。
 * 改动这里会同时影响服务端射线、AI 与全部快照语义，请勿在未同步客户端的前提下翻转。
 */
export function rightFromYaw(out: Vec3, yaw: number): Vec3 {
  return setVec3(out, Math.cos(yaw), 0, -Math.sin(yaw));
}

export function yawPitchToDirection(out: Vec3, yaw: number, pitch: number): Vec3 {
  const cp = Math.cos(pitch);
  return setVec3(out, Math.sin(yaw) * cp, Math.sin(pitch), Math.cos(yaw) * cp);
}

export function approxEq(a: number, b: number, eps = 1e-9): boolean {
  return Math.abs(a - b) <= eps;
}
