import { useEffect, useState } from 'react'
import { Navigate } from 'react-router-dom'
import { api, type CloudJob } from '../api'
import { useAuth } from '../auth'

const stageText: Record<string, string> = {
  queued: 'Esperando turno', starting: 'Preparando la obra', blueprint: 'Diseñando la composición',
  writing: 'Escribiendo las pistas', recovery: 'Afinando pasajes', validation: 'Revisando la obra',
  rendering: 'Preparando los MIDI', ready: 'Lista para descargar',
  interrupted: 'Interrumpida', generation: 'La composición no se completó',
}

export function Cloud() {
  const { user, loading } = useAuth()
  const [jobs, setJobs] = useState<CloudJob[]>([])
  const [prompt, setPrompt] = useState('')
  const [minutes, setMinutes] = useState(4)
  const [bpm, setBpm] = useState(120)
  const [behavior, setBehavior] = useState<CloudJob['behavior']>('adaptive')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  const [cloudStatus, setCloudStatus] = useState<{ available: boolean; dailyJobLimit: number } | null>(null)
  useEffect(() => { if (user) void api.cloudStatus().then(setCloudStatus).catch(() => setCloudStatus(null)) }, [user])
  useEffect(() => {
    if (!user) return
    let active = true
    const refresh = async () => {
      try { const result = await api.cloudJobs(); if (active) setJobs(result) }
      catch (problem) { if (active) setError(problem instanceof Error ? problem.message : 'No pudimos leer tus obras.') }
    }
    void refresh()
    const timer = window.setInterval(() => void refresh(), 5000)
    return () => { active = false; window.clearInterval(timer) }
  }, [user])
  if (loading) return <section className="cloud-page"><p>Abriendo PULSO Cloud...</p></section>
  if (!user) return <Navigate to="/login" replace />

  async function compose(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault(); setError(''); setBusy(true)
    try {
      const job = await api.createCloudJob({ prompt: prompt.trim(), durationSeconds: minutes * 60,
        bpm, behavior, idempotencyKey: crypto.randomUUID() })
      setJobs(previous => [job, ...previous]); setPrompt('')
    } catch (problem) {
      setError(problem instanceof Error ? problem.message : 'No pudimos comenzar la composición.')
    } finally { setBusy(false) }
  }

  return <section className="cloud-page">
    <div className="page-hero compact"><p className="eyebrow">PULSO CLOUD · BETA</p>
      <h1>Tus obras, <em>siempre contigo.</em></h1>
      <p>Compón en segundo plano y descarga cada pista MIDI al terminar. Puedes cerrar esta página y volver más tarde.</p></div>
    <div className="cloud-grid"><form className="cloud-form" onSubmit={event => void compose(event)}>
      <h2>Nueva obra</h2><label htmlFor="cloud-prompt">Tu idea musical</label>
      <textarea id="cloud-prompt" value={prompt} maxLength={600} required rows={5}
        onChange={event => setPrompt(event.target.value)} placeholder="Describe la historia, el carácter, la instrumentación y el movimiento que buscas." />
      <div className="cloud-fields"><label>Duración en minutos<input type="number" min={1} max={15} value={minutes}
        onChange={event => setMinutes(Number(event.target.value))} /></label>
        <label>Tempo (BPM)<input type="number" min={60} max={180} value={bpm}
          onChange={event => setBpm(Number(event.target.value))} /></label>
        <label>Desarrollo<select value={behavior} onChange={event => setBehavior(event.target.value as CloudJob['behavior'])}>
          <option value="adaptive">Adaptativo</option><option value="hypnotic">Hipnótico</option>
          <option value="narrative">Narrativo</option></select></label></div>
      <button className="button" disabled={busy || !prompt.trim() || cloudStatus?.available !== true}>{busy ? 'Enviando...' : 'Componer en Cloud'}</button>
      <p className="cloud-caption">{cloudStatus?.available ? `Beta: ${cloudStatus.dailyJobLimit} obras cada 24 horas por cuenta.` : 'La beta Cloud todavía no está habilitada para esta cuenta.'} La generación puede tardar varios minutos; el trabajo continúa aunque cierres el navegador.</p>
      {error && <p className="form-error" role="alert">{error}</p>}
    </form><div className="cloud-library"><h2>Mis obras</h2>
      {jobs.length === 0 ? <p>Aquí aparecerán tus composiciones Cloud.</p> : jobs.map(job =>
        <article className="cloud-job" key={job.id}><div className="cloud-job-head"><strong>{job.resultManifest?.title || job.prompt}</strong>
          <span>{job.status === 'completed' ? 'Lista' : job.status === 'failed' ? 'No completada' :
            job.status === 'cancelled' ? 'Cancelada' : stageText[job.stage] || 'Componiendo'}</span></div>
          <small>{new Date(job.createdAt * 1000).toLocaleString()} · {job.durationSeconds / 60} min · {job.bpm} BPM</small>
          {(job.status === 'queued' || job.status === 'running') && <div className="cloud-job-progress">
            <progress value={job.completedSteps} max={Math.max(job.totalSteps, 1)} />
            <button type="button" className="text-button" onClick={async () => {
              await api.cancelCloudJob(job.id); setJobs(await api.cloudJobs())
            }}>Cancelar</button></div>}
          {job.status === 'completed' && job.resultManifest && <div className="cloud-track-list">
            <p>{job.resultManifest.key} · {job.resultManifest.tracks.length} pistas MIDI</p>
            <a className="cloud-full-download" href={`/api/cloud/jobs/${job.id}/tracks/${encodeURIComponent(job.resultManifest.fullFile)}`}>
              Canción completa <span>Descargar MIDI multipista</span></a>
            {job.resultManifest.tracks.map(track => <a key={track.filename}
              href={`/api/cloud/jobs/${job.id}/tracks/${encodeURIComponent(track.filename)}`}>
              {track.name} <span>Descargar MIDI · {track.notes} notas</span></a>)}</div>}
          {job.status === 'failed' && <p>Conservamos el registro técnico para investigar el fallo. No se publicó una obra incompleta.</p>}
        </article>)}</div></div>
  </section>
}
