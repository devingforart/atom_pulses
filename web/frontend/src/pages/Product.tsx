import { t, useLocale, locale } from '../i18n'
import { Link } from 'react-router-dom'
import { MidiStage } from '../components/MidiStage'

const capabilities = [
  ['Forma completa', 'Piensa en minutos, actos y retornos para desarrollar una obra con dirección.'],
  ['Orquestación editable', 'Entrega cada función musical en una pista MIDI independiente.'],
  ['Armonía consolidada', 'La tonalidad es un contrato audible, con tensión y resolución deliberadas.'],
  ['Voces con propósito', 'Protagonistas, respuestas, colchones y movimiento forman un diálogo musical claro.'],
  ['Respiración estructural', 'La respiración define dinámica, expectativa y espacio dentro de la música.'],
  ['Auditoría musical', 'Antes de publicar, PULSO verifica el resultado exacto que llegará a Live.'],
]

export function Product() {
  useLocale();
  return <>
    <section className="page-hero"><p className="eyebrow">{t("PULSO 0.58")}</p><h1>{t("Una sala de composición")}<br/><em>{t("dentro de tu sesión.")}</em></h1><p>{t("Conservas el control del timbre y la producción. PULSO se ocupa del pensamiento musical que conecta el principio con el final.")}</p></section>
    <section className="product-stage"><MidiStage /></section>
    <section className="capabilities"><div className="section-title"><p className="eyebrow">{t("CAPACIDADES")}</p><h2>{t("Diseñado para construir una identidad musical completa.")}</h2></div><div className="capability-grid">{capabilities.map(([title,text], index)=><article key={t(title)}><span>0{index+1}</span><h3>{t(title)}</h3><p>{t(text)}</p></article>)}</div></section>
    <section className="system-row"><div><p className="eyebrow">{t("TU SONIDO SIGUE SIENDO TUYO")}</p><h2>{t("MIDI limpio y un punto de partida abierto para tu producción.")}</h2></div><p>{t("Crea en Live con una audición transparente y lleva cada parte a tu colección de instrumentos, presets y decisiones de mezcla.")}</p></section>
    <section className="cta"><h2>{t("Empieza por la historia.")}<br/>{t("Termina con tu sonido.")}</h2><Link className="button button-light" to="/pricing">{t("Ver planes")}</Link></section>
  </>
}
