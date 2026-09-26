import { Link } from 'react-router-dom'
import { MidiStage } from '../components/MidiStage'

const workflow = [
  ['01', 'Describe una intención', 'Duración, clima, tensión y lenguaje musical. Habla de la música como la imaginas.'],
  ['02', 'PULSO escribe la obra', 'Forma, armonía, voces, respiración y desarrollo nacen de una misma dirección.'],
  ['03', 'Termina en Live', 'Recibe pistas MIDI independientes, editables y listas para producir con tu propio sonido.'],
]

export function Home() {
  return <>
    <section className="hero section-grid">
      <div className="hero-copy"><p className="eyebrow">AI COMPOSITION INSTRUMENT · VST3</p>
        <h1>Dirige una idea.<br/><em>Habita una obra.</em></h1>
        <p className="lead">PULSO transforma una intención en una canción completa: arco narrativo, lenguaje armónico y voces independientes para producir en Ableton Live.</p>
        <div className="actions"><Link className="button" to="/pricing">Obtener PULSO</Link><Link className="text-link" to="/product">Explorar el instrumento →</Link></div>
        <div className="trust-row"><span>VST3 · WINDOWS</span><span>ABLETON LIVE 12</span><span>MIDI EDITABLE</span></div>
      </div>
      <div className="hero-visual">
        <div className="visual-status"><span><i/>COMPOSITION READY</span><b>04 VOICES / 01 NARRATIVE</b></div>
        <MidiStage />
        <div className="visual-foot"><span>FORM</span><b>INTRO → ASCENT → PEAK → RETURN</b><span>MIDI / EDITABLE</span></div>
      </div>
    </section>
    <section className="statement"><p>COMPOSICIÓN CON MEMORIA</p><h2>Una composición <em>recuerda, cambia<br/>y encuentra su destino.</em></h2></section>
    <section id="workflow" className="workflow"><div className="section-title"><p className="eyebrow">EL FLUJO</p><h2>De una frase a una arquitectura musical.</h2></div>
      <div className="workflow-grid">{workflow.map(([n,title,text]) => <article key={n}><span>{n}</span><h3>{title}</h3><p>{text}</p></article>)}</div>
    </section>
    <section className="feature-split"><div><p className="eyebrow">COMPOSICIÓN CON DIRECCIÓN</p><h2>Una arquitectura que se siente viva.</h2><p>La IA compone el material y PULSO sostiene tonalidad, identidad, respiración y continuidad para que cada decisión forme parte de una misma historia.</p><Link className="text-link" to="/product">Descubrir PULSO →</Link></div>
      <div className="score-card"><span>NARRATIVE ARC</span><div className="arc"><i/><i/><i/><i/><i/></div><div className="score-meta"><b>INTRO</b><b>ASCENT</b><b>PEAK</b><b>RETURN</b></div></div>
    </section>
    <section className="cta"><p className="eyebrow">TU PRÓXIMA PRODUCCIÓN EMPIEZA AQUÍ</p><h2>Construye algo que llegue a algún lugar.</h2><Link className="button button-light" to="/pricing">Comenzar con PULSO</Link></section>
  </>
}
