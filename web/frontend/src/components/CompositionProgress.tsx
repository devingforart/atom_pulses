import { t, useLocale } from '../i18n'

const details: Record<string,string> = {
  queued: 'Tu solicitud está en la cola de composición.',
  starting: 'Preparando el encargo y el contexto musical.',
  blueprint: 'Planificando la forma, la armonía y las relaciones entre voces.',
  writing: 'Escribiendo las notas y desarrollando las partes de la obra.',
  recovery: 'Revisando el material pendiente para continuar la composición.',
  validation: 'Comprobando la integridad del material MIDI.',
  rendering: 'Preparando los archivos MIDI para la escucha y la descarga.',
}
export function CompositionProgress({stage, completed, total, label}: {stage:string; completed:number; total:number; label:string}) {
  useLocale()
  const known=Number.isFinite(total)&&total>0
  const done=Math.max(0,Math.min(total,Number.isFinite(completed)?completed:0))
  return <div className="composition-progress">
    <div className="composition-progress-heading"><strong>{label}</strong>{known&&<small>{done} / {total}</small>}</div>
    <progress aria-label={t('Avance de la etapa actual')} max={known?total:1} value={known?done:undefined}/>
    <p>{t(details[stage] || 'La IA continúa trabajando en tu composición.')}</p>
    <small>{t('El avance corresponde a la etapa actual y puede reiniciarse al pasar a la siguiente.')}</small>
    <p className="composition-wait-note">{t('Algunas composiciones pueden tardar hasta 40 minutos en generarse. El tiempo varía según el encargo y las revisiones, y en ciertos casos puede ser mayor.')}</p>
    <small>{t('La composición continúa aunque cierres la página.')}</small>
  </div>
}
