import { t, useLocale, locale } from '../i18n'
import { Link } from 'react-router-dom'
import { MidiStage } from '../components/MidiStage'
import { PulsoSpinner } from '../components/PulsoSpinner'
import './HomeWorkflow.css'

const workflow = [
  ['01', 'Describe una intención', 'Duración, clima, tensión y lenguaje musical. Habla de la música como la imaginas.'],
  ['02', 'PULSO escribe la obra', 'Forma, armonía, voces, respiración y desarrollo nacen de una misma dirección.'],
  ['03', 'Termina en tu DAW', 'Recibe pistas MIDI independientes, editables y listas para producir con tu propio sonido.'],
]

export function Home() {
  useLocale();
  return <>
    <section className="hero section-grid">
      <div className="hero-copy"><p className="eyebrow">{t("AI COMPOSITION INSTRUMENT · VST3")}</p>
        <h1>{t("Dirige una idea.")}<br/><em>{t("Habita una obra.")}</em></h1>
        <p className="lead">{t("PULSO transforma una intención en una canción completa: arco narrativo, lenguaje armónico y voces independientes para producir en Ableton Live.")}</p>
        <div className="actions"><Link className="button" to="/pricing">{t("Obtener PULSO")}</Link><Link className="text-link" to="/product">{t("Explorar el instrumento →")}</Link></div>
        <div className="trust-row"><span>{t("VST3 · WINDOWS")}</span><span>{t("ABLETON LIVE 12")}</span><span>{t("MIDI EDITABLE")}</span></div>
      </div>
      <div className="hero-visual">
        <div className="visual-status"><span><i/>{t("COMPOSITION READY")}</span><b>{t("04 VOICES / 01 NARRATIVE")}</b></div>
        <MidiStage />
        <div className="visual-foot"><span>{t("FORM")}</span><b>{t("INTRO → ASCENT → PEAK → RETURN")}</b><span>{t("MIDI / EDITABLE")}</span></div>
      </div>
    </section>
    <section className="statement"><p>{t("CON INTELIGENCIA ARTIFICIAL")}</p><h2>{t("Una composición")}{' '}<em>{t("recuerda, cambia")}<br/>{t("y encuentra su destino.")}</em></h2></section>
    <section id="workflow" className="workflow"><div className="section-title"><p className="eyebrow">{t("EL FLUJO")}</p><h2>{t("De una frase a una arquitectura musical.")}</h2></div>
      <div className="workflow-grid">{workflow.map(([n,title,text]) => <article key={n}><span>{n}</span><h3>{t(title)}</h3><p>{t(text)}</p>
        <div className={`workflow-media workflow-media-${n}`}>
          {n === '02' ? <PulsoSpinner label={t("PULSO escribe la obra")} /> :
            <img src={n === '01' ? '/images/pulso-prompt.png' : '/images/pulso-midi.png'}
              alt={t(n === '01' ? "Entrada de una idea musical en PULSO" : "Pistas MIDI independientes en el estudio PULSO")}
              loading="lazy" width="1000" height="600" />}
        </div>
      </article>)}</div>
    </section>
    <section className="feature-split"><div><p className="eyebrow">{t("COMPOSICIÓN CON DIRECCIÓN")}</p><h2>{t("Una arquitectura que se siente viva.")}</h2><p>{t("La IA compone el material y PULSO sostiene tonalidad, identidad, respiración y continuidad para que cada decisión forme parte de una misma historia.")}</p><Link className="text-link" to="/product">{t("Descubrir PULSO →")}</Link></div>
      <div className="score-card"><span>{t("NARRATIVE ARC")}</span><div className="arc"><i/><i/><i/><i/><i/></div><div className="score-meta"><b>{t("INTRO")}</b><b>{t("ASCENT")}</b><b>{t("PEAK")}</b><b>{t("RETURN")}</b></div></div>
    </section>
    <section className="cta"><p className="eyebrow">{t("TU PRÓXIMA PRODUCCIÓN EMPIEZA AQUÍ")}</p><h2>{t("Construye algo que llegue a algún lugar.")}</h2><Link className="button button-light" to="/pricing">{t("Comenzar con PULSO")}</Link></section>
  </>
}
