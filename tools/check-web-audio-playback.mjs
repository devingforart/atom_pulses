// Optional real-Chrome smoke test; no Cloud accounts or paid API requests.
// Uses the existing local browser-tool installation, not production dependencies.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';
import { basename } from 'node:path';
import { chromium } from '../build/pulso-promo-tools/node_modules/playwright-core/index.mjs';

const html = `<!doctype html><button id="play">Play</button><script type="module">
import {ProductionAudio} from '/shared/production-audio.mjs';
const player=new ProductionAudio();
player.setSong({lengthBeats:8,beatToSeconds:b=>b*.5},[{name:'Lead',instrument:'keys',notes:[{pitch:60,velocity:100,startBeat:0,durationBeats:4}]}]);
window.player=player;
document.querySelector('#play').onclick=async()=>{
  try {
    await player.setMode(window.testMode);
    await player.play();
    const analyser=player.context.createAnalyser();
    player.limiter.disconnect(player.context.destination);
    player.limiter.connect(analyser);
    analyser.connect(player.context.destination);
    await new Promise(r=>setTimeout(r,180));
    const samples=new Float32Array(analyser.fftSize);
    analyser.getFloatTimeDomainData(samples);
    window.result={state:player.context.state,time:player.context.currentTime,voices:player.voices.size,rms:Math.sqrt(samples.reduce((n,x)=>n+x*x,0)/samples.length)};
  } catch(e) {window.result={error:e.message};}
};
window.ready=true;
</script>`;
const server = createServer((req,res) => {
  const url=new URL(req.url,'http://localhost');
  if(url.pathname==='/') {res.setHeader('content-type','text/html');res.end(html);return;}
  const name=basename(url.pathname);
  const shared=url.pathname.startsWith('/shared/')&&['audio-engine.mjs','production-audio.mjs','sound-palette.mjs'].includes(name);
  const piano=url.pathname.startsWith('/piano/')&&/^INSPiano_Vel03_\w+_01\.(mp3|ogg)$/.test(name);
  if(!shared&&!piano) {res.writeHead(404);res.end();return;}
  try {
    const file=new URL(shared?`../web/shared/${name}`:`../web/frontend/public/piano/${name}`,import.meta.url);
    res.setHeader('content-type',shared?'text/javascript':name.endsWith('.mp3')?'audio/mpeg':'audio/ogg');
    res.end(readFileSync(file));
  } catch {res.writeHead(404);res.end();}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;
try {
  browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true,args:['--autoplay-policy=user-gesture-required']});
  for(const mode of ['neutral','piano']) {
    const page=await browser.newPage();
    // Headless Windows has no speaker device. Render the actual browser graph
    // to Chrome's silent sink and measure its PCM; this is not a hearing test.
    await page.addInitScript(()=>{
      const NativeContext=window.AudioContext;
      window.AudioContext=class extends NativeContext {
        constructor(options) { super({...options,sinkId:{type:'none'}}); }
      };
    });
    let mp3Requests=0;
    page.on('request',request=>{if(request.url().endsWith('.mp3'))mp3Requests++;});
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.waitForFunction(()=>window.ready);
    await page.evaluate(mode=>{
      window.testMode=mode;
      if(mode==='piano') HTMLMediaElement.prototype.canPlayType=()=>'';
    },mode);
    await page.click('#play');
    await page.waitForFunction(()=>window.result);
    const result=await page.evaluate(()=>window.result);
    console.log(JSON.stringify({mode,...result,mp3Requests}));
    assert.equal(result.error,undefined);
    assert.equal(result.state,'running');
    assert.ok(result.rms>.0001,`${mode} produced a silent audio graph`);
    if(mode==='piano') assert.equal(mp3Requests,16);
    await page.close();
  }
} finally {await browser?.close();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
