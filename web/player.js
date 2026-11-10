// Page side of the Catputer browser player: one button to start, mood and
// next-session controls, and a small readout fed by the worklet.
(() => {
  const root = document.querySelector('#live-player');
  if (!root) return;
  const base = root.dataset.base;
  const play = root.querySelector('[data-live="play"]');
  const next = root.querySelector('[data-live="next"]');
  const status = root.querySelector('[data-live="status"]');
  const moods = Array.from(root.querySelectorAll('[data-mood]'));
  const bars = Array.from(root.querySelectorAll('[data-live="level"]'));
  const moodNames = ['Cozy', 'Rainy', 'Night', 'Sunny'];
  let context = null, node = null, playing = false, mood = 0;
  // The engine reports conservative activity levels; about 48 of 255 is a loud part.
  const kFullLevel = 48;

  const randomSeed = () => {
    const words = new Uint32Array(2);
    crypto.getRandomValues(words);
    return { seedHigh: words[0], seedLow: words[1] };
  };
  const hex = value => value.toString(16).padStart(8, '0');
  const showMood = () => moods.forEach(button =>
    button.setAttribute('aria-pressed', String(Number(button.dataset.mood) === mood)));
  const showPlaying = () => {
    play.textContent = playing ? 'Pause' : 'Play';
    next.disabled = !node;
  };

  async function start() {
    if (!window.AudioWorkletNode || !window.WebAssembly) {
      throw new Error('This browser has no AudioWorklet or WebAssembly support.');
    }
    const response = await fetch(`${base}catputer.wasm`);
    if (!response.ok) throw new Error('The player could not be downloaded.');
    const wasm = await response.arrayBuffer();
    // The engine renders at 32 kHz, so ask for a context at that rate.
    context = new AudioContext({ sampleRate: 32000 });
    await context.audioWorklet.addModule(`${base}worklet.js`);
    node = new AudioWorkletNode(context, 'catputer', {
      numberOfInputs: 0,
      outputChannelCount: [2],
      processorOptions: { wasm, mood, ...randomSeed() },
    });
    node.port.onmessage = ({ data }) => {
      status.textContent = `${moodNames[data.mood]} · ${data.bpm} BPM · ${data.meter} · bar ${data.bar + 1} · seed ${hex(data.seedHigh)}${hex(data.seedLow)}`;
      bars.forEach((bar, index) => { bar.style.transform = `scaleY(${Math.min(1, Math.max(0.04, data.levels[index] / kFullLevel))})`; });
    };
    node.connect(context.destination);
  }

  play.addEventListener('click', async () => {
    try {
      if (!node) {
        play.disabled = true;
        status.textContent = 'Starting…';
        await start();
        playing = true;
      } else {
        playing = !playing;
        node.port.postMessage({ type: 'pause', paused: !playing });
        if (playing) await context.resume();
      }
    } catch (error) {
      status.textContent = `${error.message} The recorded demos below still work.`;
      node = null;
    } finally {
      play.disabled = false;
      showPlaying();
    }
  });

  next.addEventListener('click', () => {
    if (node) node.port.postMessage({ type: 'next', ...randomSeed() });
  });

  moods.forEach(button => button.addEventListener('click', () => {
    mood = Number(button.dataset.mood);
    showMood();
    if (node) node.port.postMessage({ type: 'mood', mood });
  }));

  showMood();
  showPlaying();
})();
