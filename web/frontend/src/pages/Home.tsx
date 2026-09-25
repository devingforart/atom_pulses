import { Link } from 'react-router-dom'
import { MidiStage } from '../components/MidiStage'

const workflow = [
  ['01', 'Describe una intención', 'Duración, clima, tensión y lenguaje musical. Sin aprender sintaxis.'],
  ['02', 'PULSO escribe la obra', 'Forma, armonía, voces, respiración y desarrollo nacen de una misma dirección.'],
  ['03', 'Termina en Live', 'Recibe pistas MIDI independientes, editables y listas para elegir sonido.'],
]

export function Home() {
  return <>
    <section className="hero section-grid">
      <div className="hero-copy"><p className="eyebrow">AI COMPOSITION INSTRUMENT · VST3</p>
        <h1>No generes más notas.<br/><em>Dirige una obra.</em></h1>
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
    <section className="statement"><p>Un loop repite.</p><h2>Una composición <em>recuerda, cambia<br/>y encuentra su destino.</em></h2></section>
    <section id="workflow" className="workflow"><div className="section-title"><p className="eyebrow">EL FLUJO</p><h2>De una frase a una arquitectura musical.</h2></div>
      <div className="workflow-grid">{workflow.map(([n,title,text]) => <article key={n}><span>{n}</span><h3>{title}</h3><p>{text}</p></article>)}</div>
    </section>
    <section className="feature-split"><div><p className="eyebrow">NO ES UN GENERADOR DE LOOPS</p><h2>Decisiones musicales antes que densidad.</h2><p>La IA compone el material. Los sistemas de auditoría protegen tonalidad, identidad, respiración y continuidad sin reemplazar sus decisiones creativas.</p><Link className="text-link" to="/product">Ver la filosofía de PULSO →</Link></div>
      <div className="score-card"><span>NARRATIVE ARC</span><div className="arc"><i/><i/><i/><i/><i/></div><div className="score-meta"><b>INTRO</b><b>ASCENT</b><b>PEAK</b><b>RETURN</b></div></div>
    </section>
    <section className="cta"><p className="eyebrow">LA PRÓXIMA IDEA NO TIENE QUE SER OTRO BOCETO</p><h2>Construye algo que llegue a algún lugar.</h2><Link className="button button-light" to="/pricing">Comenzar con PULSO</Link></section>
  </>
}
