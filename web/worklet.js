// AudioWorklet side of the Catputer browser player.
// It instantiates the engine's WebAssembly module and pulls 32 kHz mono PCM
// from it, one render quantum at a time. Everything musical happens in the
// shared C++ engine; this file only moves samples and messages.
class CatputerProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const { wasm, seedHigh, seedLow, mood } = options.processorOptions;
    const module = new WebAssembly.Module(wasm);
    // A standalone build may still name a few WASI functions; none is needed.
    const imports = {};
    for (const entry of WebAssembly.Module.imports(module)) {
      if (entry.kind !== 'function') continue;
      (imports[entry.module] ||= {})[entry.name] = () => 0;
    }
    this.api = new WebAssembly.Instance(module, imports).exports;
    if (this.api._initialize) this.api._initialize();
    this.api.catputer_init(seedHigh, seedLow, mood);
    this.limit = this.api.catputer_buffer_frames();
    this.sinceStatus = 0;
    this.port.onmessage = ({ data }) => {
      if (data.type === 'mood') this.api.catputer_set_mood(data.mood);
      else if (data.type === 'next') this.api.catputer_next(data.seedHigh, data.seedLow);
      else if (data.type === 'pause') this.api.catputer_pause(data.paused ? 1 : 0);
    };
  }

  process(inputs, outputs) {
    const channels = outputs[0];
    const frames = Math.min(channels[0].length, this.limit);
    const rendered = this.api.catputer_render(frames);
    // Re-read the view each time: it is cheap and survives a memory change.
    const pcm = new Int16Array(this.api.memory.buffer, this.api.catputer_buffer(), rendered);
    const first = channels[0];
    for (let index = 0; index < rendered; ++index) first[index] = pcm[index] / 32768;
    for (let channel = 1; channel < channels.length; ++channel) channels[channel].set(first);

    this.sinceStatus += rendered;
    if (this.sinceStatus >= 3200) { // About ten updates a second.
      this.sinceStatus = 0;
      const meter = this.api.catputer_meter();
      const levels = new Uint8Array(this.api.memory.buffer, this.api.catputer_levels(), 7);
      this.port.postMessage({
        bpm: this.api.catputer_bpm(),
        bar: this.api.catputer_bar(),
        mood: this.api.catputer_mood(),
        meter: `${meter >> 8}/${meter & 255}`,
        seedHigh: this.api.catputer_seed_high() >>> 0,
        seedLow: this.api.catputer_seed_low() >>> 0,
        levels: Array.from(levels),
      });
    }
    return true;
  }
}

registerProcessor('catputer', CatputerProcessor);
