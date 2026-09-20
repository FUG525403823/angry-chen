export interface FakePannerRecord {
  panningModel: string;
  distanceModel: string;
  refDistance: number;
  maxDistance: number;
  rolloffFactor: number;
  x: number;
  y: number;
  z: number;
}

export interface FakeAudioStats {
  contexts: number;
  gains: number;
  filters: number;
  oscillators: number;
  noiseSources: number;
  shapers: number;
  scheduled: number;
  stopped: number;
  closed: number;
  readonly panners: FakePannerRecord[];
}

export interface FakeAudioParam {
  value: number;
  setValueAtTime(value: number, when: number): void;
  linearRampToValueAtTime(value: number, when: number): void;
  exponentialRampToValueAtTime(value: number, when: number): void;
  setTargetAtTime(value: number, startTime: number, timeConstant: number): void;
  cancelScheduledValues(startTime: number): void;
}

interface FakeNode {
  connect(target: unknown): void;
  disconnect(): void;
}

interface FakeSourceNode extends FakeNode {
  start(when?: number, offset?: number, duration?: number): void;
  stop(when?: number): void;
}

function makeParam(initial: number, onChange?: (value: number) => void): FakeAudioParam {
  let current = initial;
  function assign(value: number): void {
    current = value;
    if (onChange !== undefined) onChange(value);
  }
  return {
    get value(): number {
      return current;
    },
    set value(next: number) {
      assign(next);
    },
    setValueAtTime(value: number): void {
      assign(value);
    },
    linearRampToValueAtTime(value: number): void {
      assign(value);
    },
    exponentialRampToValueAtTime(value: number): void {
      assign(value);
    },
    setTargetAtTime(value: number): void {
      assign(value);
    },
    cancelScheduledValues(): void {
      return;
    },
  };
}

function makeSinkNode(): FakeNode {
  return {
    connect(): void {
      return;
    },
    disconnect(): void {
      return;
    },
  };
}

export function createFakeAudioContext(stats: FakeAudioStats): unknown {
  stats.contexts += 1;

  function gainNode(): unknown {
    stats.gains += 1;
    return { ...makeSinkNode(), gain: makeParam(1) };
  }

  function filterNode(): unknown {
    stats.filters += 1;
    return {
      ...makeSinkNode(),
      type: 'lowpass',
      frequency: makeParam(350),
      Q: makeParam(1),
    };
  }

  function oscillatorNode(): unknown {
    stats.oscillators += 1;
    const node: FakeSourceNode & {
      type: string;
      frequency: FakeAudioParam;
      detune: FakeAudioParam;
      onended: unknown;
    } = {
      ...makeSinkNode(),
      type: 'sine',
      frequency: makeParam(440),
      detune: makeParam(0),
      onended: null,
      start(): void {
        stats.scheduled += 1;
      },
      stop(): void {
        stats.stopped += 1;
      },
    };
    return node;
  }

  function bufferSourceNode(): unknown {
    stats.noiseSources += 1;
    const node: FakeSourceNode & { buffer: unknown; loop: boolean } = {
      ...makeSinkNode(),
      buffer: null,
      loop: false,
      start(): void {
        stats.scheduled += 1;
      },
      stop(): void {
        stats.stopped += 1;
      },
    };
    return node;
  }

  function pannerNode(): unknown {
    const record: FakePannerRecord = {
      panningModel: 'equalpower',
      distanceModel: 'inverse',
      refDistance: 1,
      maxDistance: 10000,
      rolloffFactor: 1,
      x: 0,
      y: 0,
      z: 0,
    };
    stats.panners.push(record);
    return {
      ...makeSinkNode(),
      positionX: makeParam(0, (value: number) => {
        record.x = value;
      }),
      positionY: makeParam(0, (value: number) => {
        record.y = value;
      }),
      positionZ: makeParam(0, (value: number) => {
        record.z = value;
      }),
      get panningModel(): string {
        return record.panningModel;
      },
      set panningModel(value: string) {
        record.panningModel = value;
      },
      get distanceModel(): string {
        return record.distanceModel;
      },
      set distanceModel(value: string) {
        record.distanceModel = value;
      },
      get refDistance(): number {
        return record.refDistance;
      },
      set refDistance(value: number) {
        record.refDistance = value;
      },
      get maxDistance(): number {
        return record.maxDistance;
      },
      set maxDistance(value: number) {
        record.maxDistance = value;
      },
      get rolloffFactor(): number {
        return record.rolloffFactor;
      },
      set rolloffFactor(value: number) {
        record.rolloffFactor = value;
      },
    };
  }

  return {
    currentTime: 1,
    sampleRate: 48000,
    state: 'running',
    destination: makeSinkNode(),
    listener: {
      positionX: makeParam(0),
      positionY: makeParam(0),
      positionZ: makeParam(0),
      forwardX: makeParam(0),
      forwardY: makeParam(0),
      forwardZ: makeParam(-1),
      upX: makeParam(0),
      upY: makeParam(1),
      upZ: makeParam(0),
    },
    createGain(): unknown {
      return gainNode();
    },
    createBiquadFilter(): unknown {
      return filterNode();
    },
    createOscillator(): unknown {
      return oscillatorNode();
    },
    createBufferSource(): unknown {
      return bufferSourceNode();
    },
    createWaveShaper(): unknown {
      stats.shapers += 1;
      return { ...makeSinkNode(), curve: null, oversample: 'none' };
    },
    createPanner(): unknown {
      return pannerNode();
    },
    createBuffer(channels: number, length: number, sampleRate: number): unknown {
      const data: Float32Array[] = [];
      for (let i = 0; i < channels; i += 1) data.push(new Float32Array(length));
      return {
        numberOfChannels: channels,
        length,
        sampleRate,
        getChannelData(index: number): Float32Array {
          return data[index] ?? new Float32Array(length);
        },
      };
    },
    resume(): Promise<void> {
      return Promise.resolve();
    },
    close(): Promise<void> {
      stats.closed += 1;
      return Promise.resolve();
    },
  };
}

export interface FakeAudioGlobal {
  readonly stats: FakeAudioStats;
  uninstall(): void;
}

export function installFakeAudioContext(): FakeAudioGlobal {
  const stats: FakeAudioStats = {
    contexts: 0,
    gains: 0,
    filters: 0,
    oscillators: 0,
    noiseSources: 0,
    shapers: 0,
    scheduled: 0,
    stopped: 0,
    closed: 0,
    panners: [],
  };
  function FakeAudioContext(): unknown {
    return createFakeAudioContext(stats);
  }
  Object.defineProperty(globalThis, 'AudioContext', {
    configurable: true,
    writable: true,
    value: FakeAudioContext,
  });
  return {
    stats,
    uninstall(): void {
      Reflect.deleteProperty(globalThis, 'AudioContext');
    },
  };
}
