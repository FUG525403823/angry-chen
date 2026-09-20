export const BUTTON = Object.freeze({
  fire: 1,
  sprint: 2,
  jump: 4,
  reload: 8,
  interact: 16,
  rage: 32,
  switchWeapon: 64,
} as const);

export const BUTTON_MASK_ALL =
  BUTTON.fire |
  BUTTON.sprint |
  BUTTON.jump |
  BUTTON.reload |
  BUTTON.interact |
  BUTTON.rage |
  BUTTON.switchWeapon;

export const INPUT = Object.freeze({
  moveAxisLimit: 1,
  pitchLimitRad: Math.PI / 2,
  buttonMask: BUTTON_MASK_ALL,
  buttons: BUTTON,
} as const);

export type InputConfig = typeof INPUT;
