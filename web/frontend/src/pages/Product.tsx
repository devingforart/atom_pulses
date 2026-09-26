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
  return <>
    <section className="page-hero"><p className="eyebrow">PULSO 0.58</p><h1>Una sala de composición<br/><em>dentro de tu sesión.</em></h1><p>Conservas el control del timbre y la producción. PULSO se ocupa del pensamiento musical que conecta el principio con el final.</p></section>
    <section className="product-stage"><MidiStage /></section>
    <section className="capabilities"><div className="section-title"><p className="eyebrow">CAPACIDADES</p><h2>Diseñado para construir una identidad musical completa.</h2></div><div className="capability-grid">{capabilities.map(([title,text], index)=><article key={title}><span>0{index+1}</span><h3>{title}</h3><p>{text}</p></article>)}</div></section>
    <section className="system-row"><div><p className="eyebrow">TU SONIDO SIGUE SIENDO TUYO</p><h2>MIDI limpio y un punto de partida abierto para tu producción.</h2></div><p>Crea en Live con una audición transparente y lleva cada parte a tu colección de instrumentos, presets y decisiones de mezcla.</p></section>
    <section className="cta"><h2>Empieza por la historia.<br/>Termina con tu sonido.</h2><Link className="button button-light" to="/pricing">Ver planes</Link></section>
  </>
}
