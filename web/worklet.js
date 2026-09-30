class LivePlayer extends AudioWorkletProcessor {
  constructor() {
    super();
    this.audio = new Float32Array(48000 * 2 * 2);
    this.read = 0; this.write = 0; this.size = 0; this.started = false;
    this.underruns = 0; this.blocks = 0;
    this.port.onmessage = ({data}) => {
      if (data.length > this.audio.length) return;
      if (this.size + data.length > this.audio.length) {
        this.read = this.write; this.size = 0; this.started = false;
      }
      for (const value of data) {
        this.audio[this.write] = value;
        this.write = (this.write + 1) % this.audio.length;
      }
      this.size += data.length;
    };
  }
  process(_, outputs) {
    const [left, right] = outputs[0];
    if (!this.started && this.size >= 48000 * 2 * .18) this.started = true;
    if (this.started && this.size < left.length * 2) {
      this.started = false; this.underruns++;
    }
    if (this.started) {
      for (let i = 0; i < left.length; i++) {
        left[i] = this.audio[this.read]; this.read = (this.read + 1) % this.audio.length;
        right[i] = this.audio[this.read]; this.read = (this.read + 1) % this.audio.length;
      }
      this.size -= left.length * 2;
    }
    if (++this.blocks % 180 === 0)
      this.port.postMessage({buffer: this.size / 96, underruns: this.underruns});
    return true;
  }
}
registerProcessor('live-player', LivePlayer);
