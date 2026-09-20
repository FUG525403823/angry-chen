export const WEAPON_LABELS: readonly string[] = Object.freeze(['手枪', '步枪', '霰弹枪']);

export function weaponLabelOf(slot: number): string {
  return WEAPON_LABELS[slot] ?? '手枪';
}

export interface WeaponPickerOptions {
  onChange(slot: number): void;
}

export interface WeaponPicker {
  readonly element: HTMLElement;
  readonly selected: number;
  setSelected(slot: number): void;
  dispose(): void;
}

export function createWeaponPicker(options: WeaponPickerOptions): WeaponPicker {
  const element = document.createElement('div');
  element.className = 'weapon-picker';
  const buttons: HTMLButtonElement[] = [];
  let selected = 0;

  function select(slot: number): void {
    selected = slot === 1 || slot === 2 ? slot : 0;
    for (let i = 0; i < buttons.length; i += 1) {
      buttons[i]?.classList.toggle('selected', i === selected);
    }
  }

  for (let slot = 0; slot < WEAPON_LABELS.length; slot += 1) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'weapon-button';
    button.textContent = WEAPON_LABELS[slot] ?? '';
    button.addEventListener('click', () => {
      select(slot);
      options.onChange(slot);
    });
    buttons.push(button);
    element.append(button);
  }
  select(0);

  return {
    element,
    get selected(): number {
      return selected;
    },
    setSelected(slot: number): void {
      select(slot);
    },
    dispose(): void {
      element.remove();
    },
  };
}
