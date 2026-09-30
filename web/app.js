const $ = id => document.getElementById(id);
let context, controller, player, listening = false, muted = false, activeRequest = 0, program = 'french-house';
const touched = new Map();
function notice(text, error = false) { $('notice').textContent = text; $('notice').className = error ? 'error' : ''; }
async function command(commands, score = false) {
  try {
    const response = await fetch(score ? '/score' : '/control', {
      method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({commands})
    });
    const result = await response.json();
    if (!response.ok) throw Error(result.error);
    notice(score ? 'Transition queued for the next bar.' : result.message);
  } catch (error) { notice(error.message, true); }
}
function fader(id, prefix) {
  const input = $(id), output = $(`${id}-value`);
  let timer;
  input.oninput = () => {
    output.textContent = `${Math.round(input.value * 100)}%`;
    touched.set(id, performance.now());
    clearTimeout(timer);
    timer = setTimeout(() => command(`${prefix} ${input.value}`), 100);
  };
}
for (const layer of ['kick', 'clap', 'hats', 'bass', 'lead', 'pad']) {
  const div = document.createElement('div');
  div.className = 'fader';
  const name = layer === 'pad' ? 'Chords' : layer === 'lead' ? 'Hook' : layer[0].toUpperCase() + layer.slice(1);
  div.innerHTML = `<label for="${layer}">${name} <output id="${layer}-value">—</output></label><input id="${layer}" type="range" min="0" max="1" step=".01" value=".3">`;
  $('mixer').append(div);
  fader(layer, `mix ${layer}`);
}
fader('volume', 'volume');
$('mute').onclick = () => command(muted ? 'unmute' : 'mute');
$('next').onclick = () => command('next');
$('hold').onclick = () => command('cancel');
$('resume').onclick = () => command('radio on');
$('breakdown').onclick = () => command('ramp 0 2 mix kick 0\nramp 0 2 mix bass .1\nramp 0 2 mix clap 0\nramp 0 4 mix hats .06\nramp 0 4 mix pad .6\nramp 0 4 filter 1400\nramp 0 4 delay .45'
  + (program === 'french-house' ? '' : '\nat 0 melody 0 - 2 - 3 - 2 - 4 - 3 - 2 - 0 -'), true);
$('build').onclick = () => command(program === 'french-house'
  ? 'at 0 mix kick 0\nat 0 mix bass .2\nramp 0 8 filter 7500\nramp 0 8 mix pad .55\nramp 0 7 mix hats .3\nramp 0 7 mix clap .4\nat 7 mix hats 0\nat 7 mix clap 0\nat 8 mix kick .9\nat 8 mix bass .7\nat 8 mix clap .32\nat 8 mix hats .25\nat 8 delay .14'
  : 'at 0 mix kick 0\nat 0 mix bass .1\nat 0 melody 0 2 3 2 4 2 3 5 0 2 3 4 5 4 3 2\nat 0 bassline 0 1 1 1 0 1 1 1 0 1 1 1 0 1 1 1\nramp 0 8 filter 11000\nramp 0 8 mix lead .55\nramp 0 7 mix hats .4\nramp 0 7 mix clap .45\nat 7 mix hats 0\nat 7 mix clap 0\nat 8 mix kick .9\nat 8 mix bass .65\nat 8 mix clap .32\nat 8 mix hats .3\nat 8 delay .23', true);

async function stopListening() {
  listening = false; activeRequest++;
  controller?.abort(); controller = null;
  player?.disconnect(); player = null;
  const old = context; context = null;
  if (old) await old.close();
  $('listen').textContent = '▶ Listen';
  $('audio-status').textContent = 'Listening paused. The station keeps moving.';
}
$('listen').onclick = async () => {
  if (listening) { await stopListening(); return; }
  $('listen').disabled = true;
  const request = ++activeRequest;
  try {
    context = new AudioContext({sampleRate: 48000, latencyHint: 'playback'});
    await context.resume();
    await context.audioWorklet.addModule('/worklet.js');
    player = new AudioWorkletNode(context, 'live-player', {outputChannelCount: [2]});
    player.connect(context.destination);
    player.port.onmessage = ({data}) => {
      $('audio-status').textContent = `Listening live · ${Math.round(data.buffer)} ms buffer · ${data.underruns} playback gaps`;
    };
    controller = new AbortController();
    const response = await fetch('/audio', {signal: controller.signal});
    if (!response.ok) throw Error('Could not join the live stream');
    listening = true;
    $('listen').textContent = 'Ⅱ Pause';
    $('audio-status').textContent = 'Joining the live sound…';
    $('listen').disabled = false;
    const reader = response.body.getReader();
    let carry = new Uint8Array();
    while (request === activeRequest) {
      const {value, done} = await reader.read();
      if (done) break;
      const bytes = new Uint8Array(carry.length + value.length);
      bytes.set(carry); bytes.set(value, carry.length);
      const length = bytes.length - bytes.length % 4;
      const view = new DataView(bytes.buffer, 0, length);
      const samples = new Float32Array(length / 2);
      for (let i = 0; i < samples.length; i++) samples[i] = view.getInt16(i * 2, true) / 32768;
      carry = bytes.slice(length);
      if (request !== activeRequest) break;
      player.port.postMessage(samples, [samples.buffer]);
    }
    if (request === activeRequest) { await stopListening(); notice('Stream ended. Press Listen to reconnect.', true); }
  } catch (error) {
    if (request === activeRequest) { await stopListening(); if (error.name !== 'AbortError') notice(error.message, true); }
  } finally { $('listen').disabled = false; }
};

async function poll() {
  try {
    const response = await fetch('/status');
    if (!response.ok) throw Error('Station unavailable');
    const status = await response.json();
    program = status.program || 'trance';
    $('connection').textContent = status.running ? 'Live · local' : 'Station stopped';
    const waiting = status.bar < status.score_start_bar;
    $('chapter').textContent = waiting ? `Up next: ${status.chapter}` : status.chapter;
    if ($('description')) $('description').textContent = status.program === 'french-house'
      ? 'Funk in the bass. Rhythm in the chords. An original house groove, generated live.'
      : 'Progressive trance. A steady pulse, an evolving journey.';
    $('bpm').textContent = Math.round(status.bpm || 0);
    $('bar').textContent = Math.floor(status.bar || 1);
    const position = Math.max(0, (status.bar || 1) - (status.score_start_bar || 1));
    const phase = position < 16 ? 0 : position < 32 ? 1 : position < 40 ? 2 : position < 48 ? 3 : 4;
    $('section').textContent = status.custom && status.cues_remaining ? 'Your transition' : !status.radio ? 'Holding the groove' : waiting ? 'Next chapter queued' : status.custom ? 'Your transition' : ['The theme', 'Developing the theme', 'Breakdown', 'Building tension', 'Release'][phase];
    const length = status.custom ? Math.max(1, status.score_end_bar - status.score_start_bar) : 64;
    $('progress').style.width = `${Math.min(100, position / length * 100)}%`;
    document.querySelector('.sections').style.visibility = status.custom || !status.radio ? 'hidden' : 'visible';
    document.querySelectorAll('.sections span').forEach((node, index) => node.classList.toggle('active', phase === index));
    $('autopilot').textContent = status.radio ? 'Auto DJ on' : 'Manual';
    muted = !!status.muted;
    $('mute').textContent = muted ? 'Unmute' : 'Mute';
    $('mute').setAttribute('aria-pressed', String(muted));
    for (const key of ['volume', 'kick', 'clap', 'hats', 'bass', 'lead', 'pad']) {
      if (status[key] === undefined || performance.now() - (touched.get(key) || -Infinity) < 1000 || document.activeElement === $(key)) continue;
      $(key).value = status[key]; $(`${key}-value`).textContent = `${Math.round(status[key] * 100)}%`;
    }
    $('health').textContent = `${Math.floor((status.seconds || 0) / 60)} min on air · ${status.underruns || 0} engine gaps · ${status.stream_dropped || 0} stream drops`;
    if (status.error) notice(status.error, true);
  } catch (_) { $('connection').textContent = 'Disconnected'; }
  setTimeout(poll, 500);
}
poll();
