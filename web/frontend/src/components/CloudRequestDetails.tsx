import { Fragment } from 'react'
import type { CloudJob } from '../api'
import { t, useLocale } from '../i18n'
import './CloudRequestDetails.css'

export function CloudRequestDetails({ job }: { job: CloudJob }) {
  useLocale()
  const flag = (value: boolean | undefined) => t(typeof value !== 'boolean' ? 'No registrado' : value ? 'Activado' : 'Desactivado')
  const approaches = { adaptive: 'Adaptativo', hypnotic: 'Hipnótico', narrative: 'Narrativo', club: 'Club' }
  const club = job.behavior === 'club'
  const pace = job.clubLayerPace
  const horizon = pace && ['8-16', '16-32', '32-64'].includes(pace)
    ? pace.replace('-', '–') + ' ' + t('compases')
    : t(pace === 'auto' ? 'Automática' : 'No registrado')
  const rows = [
    [t('Idea musical'), job.prompt],
    [t('Duración solicitada'), Math.floor(job.durationSeconds / 60) + ':' + String(job.durationSeconds % 60).padStart(2, '0')],
    [t('Tempo'), job.bpm + ' ' + t('BPM')],
    [t('Semilla'), job.seed],
    [t('Enfoque solicitado'), t(approaches[job.behavior] || job.behavior)],
    [t('Club: evolución de capas'), club ? horizon : t('No aplica · Club desactivado')],
    [t('Club: recurso principal libre'), club ? flag(job.clubVariety) : t('No aplica · Club desactivado')],
    [t('Permitir cromatismos'), flag(job.allowChromatic)],
    [t('Autoría solicitada'), t(job.aiSovereign ? 'IA soberana' : 'Motor anterior · referencia A/B')],
  ]
  return <details className="cloud-request-details">
    <summary>{t('Solicitud enviada · ajustes guardados')}</summary>
    <p>{t('Parámetros guardados al enviar esta obra, no una medición del resultado musical. La evolución de capas es una preferencia, no un calendario obligatorio.')}</p>
    <dl>{rows.map(([label, value]) => <Fragment key={label}><dt>{label}</dt><dd>{value}</dd></Fragment>)}</dl>
  </details>
}
