import { useEffect, useMemo, useRef, useState, type CSSProperties, type FormEvent, type MouseEvent } from 'react'
import { Navigate } from 'react-router-dom'
import { api, type CloudJob, type CloudTrack } from '../api'
import { useAuth } from '../auth'
import { Logo } from '../components/Logo'
import { parseMidi, type MidiNote, type MidiScore } from '../../../shared/midi.mjs'
import { instrumentFor, type SuiteTrack } from '../../../shared/audio-engine.mjs'
import { ProductionAudio } from '../../../shared/production-audio.mjs'
import { PATCHES, SOUND_BANK_VERSION, patchForTrack, patchOptions } from '../../../shared/sound-palette.mjs'
import './CloudSuite.css'

type AudibleTrack = SuiteTrack & { color: string }
type AudibleScore = { jobId: string; midi: MidiScore; tracks: AudibleTrack[] }

const stageText: Record<string, string> = {
  queued: 'Esperando turno', starting: 'Preparando la obra', blueprint: 'Diseñando la composición',
  writing: 'Escribiendo las pistas', recovery: 'Afinando pasajes', validation: 'Revisando la obra',
  rendering: 'Preparando los MIDI', ready: 'Lista para escuchar',
  interrupted: 'Interrumpida', generation: 'La composición no se completó',
}
const palette: Record<string, string> = { pad: '#dfb64e', bass: '#71a1d0', keys: '#8b9dca', pluck: '#cc8b6e', lead: '#d84f2b', drums: '#75b57c', synth: '#a899c7' }
const neutralLabels: Record<string, string> = { pad: 'Seno · armonía', bass: 'Seno · bajo', keys: 'Seno · teclas', pluck: 'Seno · arpegio', lead: 'Seno · melodía', drums: 'Seno · percusión', synth: 'Seno · pista' }
const fmt = (seconds: number) => { const value = Math.max(0, Math.floor(seconds || 0)); return `${String(Math.floor(value / 60)).padStart(2, '0')}:${String(value % 60).padStart(2, '0')}` }
const trackUrl = (jobId: string, filename: string) => `/api/cloud/jobs/${encodeURIComponent(jobId)}/tracks/${encodeURIComponent(filename)}`

async function loadScore(job: CloudJob, signal: AbortSignal): Promise<AudibleScore> {
  const detail = await api.cloudJob(job.id)
  if (signal.aborted) throw new Error('Carga cancelada')
  const manifest = detail.resultManifest
  if (!manifest?.tracks?.length) throw new Error('Esta obra no tiene pistas MIDI disponibles.')
  const parts: { track: CloudTrack; midi: MidiScore }[] = []
  for (let start = 0; start < manifest.tracks.length; start += 6) {
    const batch = await Promise.all(manifest.tracks.slice(start, start + 6).map(async track => {
      const response = await fetch(trackUrl(job.id, track.filename), { credentials: 'include', cache: 'no-store', signal })
      if (!response.ok) throw new Error(`No se pudo abrir ${track.name} (HTTP ${response.status}).`)
      return { track, midi: parseMidi(await response.arrayBuffer()) }
    }))
    parts.push(...batch)
  }
  const first = parts[0].midi
  const midi: MidiScore = { ...first, lengthBeats: Math.max(manifest.bars * first.beatsPerBar, ...parts.map(part => part.midi.lengthBeats)) }
  const tracks = parts.map(({ track, midi: partMidi }) => {
    const notes = partMidi.tracks.flatMap(item => item.notes).sort((a, b) => a.startBeat - b.startBeat)
    const kind = instrumentFor(track.instrument || {}, track.name)
    const item: AudibleTrack = { name: track.name, filename: track.filename, notes, instrument: kind, meta: track.instrument || {}, color: palette[kind] || palette.synth }
    item.patchId = patchForTrack(item)
    try {
      const saved = localStorage.getItem(`pulso:audition:${SOUND_BANK_VERSION}:${job.id}:${track.filename}`)
      if (saved && PATCHES[saved]?.family === PATCHES[item.patchId]?.family) item.patchId = saved
    } catch { /* Private browsing may disable storage. */ }
    return item
  }).filter(track => track.notes.length)
  if (!tracks.length) throw new Error('La obra no contiene notas reproducibles.')
  return { jobId: job.id, midi, tracks }
}

function MidiLane({ notes, color, width, beatsPerBar }: { notes: MidiNote[]; color: string; width: number; beatsPerBar: number }) {
  const canvas = useRef<HTMLCanvasElement>(null)
  useEffect(() => {
    const element = canvas.current
    if (!element) return
    const dpr = Math.min(2, window.devicePixelRatio || 1)
    element.width = Math.ceil(width * dpr)
    element.height = Math.ceil(74 * dpr)
    const context = element.getContext('2d')
    if (!context) return
    context.scale(dpr, dpr)
    let min = 127, max = 0
    for (const note of notes) { min = Math.min(min, note.pitch); max = Math.max(max, note.pitch) }
    const span = Math.max(12, max - min + 1)
    context.fillStyle = color
    for (const note of notes) {
      context.globalAlpha = 0.38 + 0.55 * note.velocity / 127
      context.fillRect(note.startBeat / beatsPerBar * 22, 62 - (note.pitch - min) / span * 48,
        Math.max(2, note.durationBeats / beatsPerBar * 22), 5)
    }
    context.globalAlpha = 1
  }, [notes, color, width, beatsPerBar])
  return <canvas ref={canvas} className="cloud-suite-notes" style={{ width, height: 74 }} aria-hidden="true" />
}

export function Cloud() {
  const { user, loading } = useAuth()
  const [jobs, setJobs] = useState<CloudJob[]>([])
  const [selectedId, setSelectedId] = useState<string | null>(null)
  const [historyOpen, setHistoryOpen] = useState(false)
  const [score, setScore] = useState<AudibleScore | null>(null)
  const [scoreError, setScoreError] = useState('')
  const [scoreLoading, setScoreLoading] = useState(false)
  const [playing, setPlaying] = useState(false)
  const [mixRevision, setMixRevision] = useState(0)
  const [listeningMode, setListeningMode] = useState<'production' | 'neutral'>('production')
  const [renderingWav, setRenderingWav] = useState(false)
  const [prompt, setPrompt] = useState('')
  const [durationSeconds, setDurationSeconds] = useState(390)
  const [bpm, setBpm] = useState(120)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  const [cloudStatus, setCloudStatus] = useState<{ available: boolean; dailyJobLimit: number } | null>(null)
  const timelineRef = useRef<HTMLDivElement>(null)
  const playheadRef = useRef<HTMLDivElement>(null)
  const timeRef = useRef<HTMLSpanElement>(null)
  const audioRef = useRef<ProductionAudio | null>(null)
  if (!audioRef.current) audioRef.current = new ProductionAudio(seconds => {
    const audio = audioRef.current
    if (!audio) return
    if (timeRef.current) timeRef.current.textContent = `${fmt(seconds)} / ${fmt(audio.duration)}`
    if (audio.midi && playheadRef.current) {
      const x = 205 + audio.midi.secondsToBeat(seconds) / audio.midi.beatsPerBar * 22
      playheadRef.current.style.left = `${x}px`
      const timeline = timelineRef.current
      if (timeline && audio.playing && x - timeline.scrollLeft > timeline.clientWidth - 70)
        timeline.scrollLeft = Math.max(0, x - Math.max(280, timeline.clientWidth * 0.55))
    }
  }, () => setPlaying(false))
  const audio = audioRef.current
  const selected = useMemo(() => jobs.find(job => job.id === selectedId) || null, [jobs, selectedId])
  const selectedFile = selected?.status === 'completed' && selected.resultManifest?.tracks?.length
    ? `${selected.id}:${selected.resultManifest.fullFile}:${selected.resultManifest.tracks.length}` : null
  const activeScore = score && selected?.status === 'completed' && score.jobId === selected.id ? score : null
  const duration = activeScore ? activeScore.midi.beatToSeconds(activeScore.midi.lengthBeats) : 0
  const bars = activeScore ? Math.ceil(activeScore.midi.lengthBeats / activeScore.midi.beatsPerBar) : 0
  const timelineWidth = Math.max(620, bars * 22)

  useEffect(() => { if (user) void api.cloudStatus().then(setCloudStatus).catch(() => setCloudStatus(null)) }, [user])
  useEffect(() => {
    if (!user) return
    let active = true
    const refresh = async () => {
      try { const result = await api.cloudJobs(); if (active) { setJobs(result); setError('') } }
      catch (problem) { if (active) setError(problem instanceof Error ? problem.message : 'No pudimos leer tus obras.') }
    }
    void refresh()
    const timer = window.setInterval(() => void refresh(), 5000)
    return () => { active = false; window.clearInterval(timer) }
  }, [user])
  useEffect(() => {
    if (!jobs.length) { setSelectedId(null); return }
    if (!selectedId || !jobs.some(job => job.id === selectedId))
      setSelectedId((jobs.find(job => job.status === 'running') || jobs.find(job => job.status === 'completed' && job.resultManifest?.tracks?.length) || jobs[0]).id)
  }, [jobs, selectedId])
  useEffect(() => {
    audio.stop(); setPlaying(false); setScore(null); setScoreError(''); setScoreLoading(false)
    if (!selectedFile || !selected) return
    const controller = new AbortController()
    setScoreLoading(true)
    void loadScore(selected, controller.signal).then(result => { if (!controller.signal.aborted) { setScore(result); setScoreLoading(false) } })
      .catch(problem => { if (!controller.signal.aborted) { setScoreError(problem instanceof Error ? problem.message : 'No se pudo leer el MIDI.'); setScoreLoading(false) } })
    return () => controller.abort()
    // The signature changes only when the selected MIDI changes, not on every status poll.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [selectedFile])
  useEffect(() => { if (score && score.jobId === selectedId) { audio.setSong(score.midi, score.tracks); setMixRevision(0) } }, [score, selectedId, audio])
  useEffect(() => () => audio.stop(), [audio])

  if (loading) return <section className="cloud-suite-loading">Abriendo PULSO Cloud…</section>
  if (!user) return <Navigate to="/login" replace />

  async function compose(event: FormEvent<HTMLFormElement>) {
    event.preventDefault(); setError(''); setBusy(true)
    try {
      const job = await api.createCloudJob({ prompt: prompt.trim(), durationSeconds,
        bpm, idempotencyKey: crypto.randomUUID() })
      setJobs(previous => [job, ...previous.filter(item => item.id !== job.id)])
      setSelectedId(job.id); setPrompt('')
    } catch (problem) { setError(problem instanceof Error ? problem.message : 'No pudimos comenzar la composición.') }
    finally { setBusy(false) }
  }
  async function togglePlay() {
    if (!activeScore) return
    try { if (audio.playing) audio.pause(); else await audio.play(); setPlaying(audio.playing) }
    catch (problem) { setScoreError(problem instanceof Error ? problem.message : 'No se pudo iniciar el audio.') }
  }
  function seekAt(event: MouseEvent<HTMLElement>) {
    if (!activeScore) return
    const x = event.clientX - event.currentTarget.getBoundingClientRect().left
    audio.seek(activeScore.midi.beatToSeconds(x / 22 * activeScore.midi.beatsPerBar))
  }
  function toggleMute(index: number) { audio.setMute(index, !audio.muted.has(index)); setMixRevision(value => value + 1) }
  function toggleSolo(index: number) { audio.setSolo(index, !audio.soloed.has(index)); setMixRevision(value => value + 1) }
  function changePatch(index: number, patchId: string) {
    if (!activeScore || !audio.setPatch(index, patchId)) return
    try { localStorage.setItem(`pulso:audition:${SOUND_BANK_VERSION}:${activeScore.jobId}:${activeScore.tracks[index].filename}`, patchId) }
    catch { /* The session still works without storage. */ }
    setMixRevision(value => value + 1)
  }
  async function downloadWav() {
    if (!activeScore) return
    setRenderingWav(true); setScoreError('')
    try {
      const start = audio.currentPosition()
      const blob = await audio.renderPreview(start, 30)
      const url = URL.createObjectURL(blob)
      const link = document.createElement('a')
      link.href = url
      link.download = `pulso-${activeScore.jobId.slice(0, 8)}-${Math.floor(start)}s-preview.wav`
      link.click()
      window.setTimeout(() => URL.revokeObjectURL(url), 60000)
    } catch (problem) { setScoreError(problem instanceof Error ? problem.message : 'No se pudo preparar el audio WAV.') }
    finally { setRenderingWav(false) }
  }
  const status = selected?.status === 'completed' ? 'LISTA' : selected?.status === 'failed' ? 'NO COMPLETADA' :
    selected?.status === 'cancelled' ? 'CANCELADA' : selected ? stageText[selected.stage] || 'COMPONIENDO' : 'SIN OBRAS'

  return <section className="cloud-suite">
    <header className="cloud-suite-bar">
      <div className="cloud-suite-brand"><Logo /><span className="cloud-suite-brand-divider" /><strong>SUITE / CLOUD</strong></div>
      <div className="cloud-suite-global"><span className="cloud-suite-online">● EN TU CUENTA</span><button type="button" onClick={() => setHistoryOpen(value => !value)}>Historial</button>
        {selected?.status === 'completed' && selected.resultManifest ? <a className="cloud-suite-export" href={trackUrl(selected.id, selected.resultManifest.fullFile)}>↓ Exportar obra MIDI</a> : <span className="cloud-suite-export disabled">↓ Exportar obra MIDI</span>}</div>
    </header>
    <div className="cloud-suite-layout"><aside className="cloud-suite-sidebar"><div className="cloud-suite-side-scroll"><div className="cloud-suite-compose">
      <div className="cloud-suite-kicker"><span>01 / DIRECCIÓN</span><span>CREACIÓN</span></div><h1>Tu próxima<br /><em>obra.</em></h1>
      <p className="cloud-suite-intro">Describe el carácter, el movimiento y la instrumentación. PULSO escribe la obra y la trae aquí para escucharla pista por pista.</p>
      <form onSubmit={event => void compose(event)}><label htmlFor="cloud-prompt">Idea musical <small>{prompt.length} / 600</small></label>
        <textarea id="cloud-prompt" value={prompt} maxLength={600} required rows={5} onChange={event => setPrompt(event.target.value)} placeholder="Una historia electrónica profunda, con armonías que evolucionan y un motivo que regresa transformado…" />
        <div className="cloud-suite-field-pair"><div><label htmlFor="cloud-duration">Duración</label><select id="cloud-duration" value={durationSeconds} onChange={event => setDurationSeconds(Number(event.target.value))}>{[[60, '1 minuto'], [120, '2 minutos'], [180, '3 minutos'], [300, '5 minutos'], [390, '6 min 30 s'], [600, '10 minutos'], [900, '15 minutos']].map(([value, label]) => <option key={value} value={value}>{label}</option>)}</select></div>
          <div><label htmlFor="cloud-bpm">Tempo</label><div className="cloud-suite-unit"><input id="cloud-bpm" type="number" min="60" max="180" value={bpm} onChange={event => setBpm(Number(event.target.value))} /><span>BPM</span></div></div></div>
        <button className="cloud-suite-compose-button" disabled={busy || !prompt.trim() || cloudStatus?.available !== true}>{busy ? 'Enviando…' : '✦  Componer obra ↗'}</button>
        <p className="cloud-suite-cost">{cloudStatus?.available ? `Beta: ${cloudStatus.dailyJobLimit} obras cada 24 horas por cuenta.` : 'Cloud no está habilitado para esta cuenta.'} Escuchar obras guardadas no consume créditos. La composición continúa aunque cierres la página.</p>
        {error && <p className="cloud-suite-error" role="alert">{error}</p>}
      </form></div>
      <div className="cloud-suite-session"><div className="cloud-suite-kicker"><span>02 / SESIÓN</span><span>{jobs.length} OBRAS</span></div>
        {selected ? <><h3>{selected.resultManifest?.title || 'Composición en curso'}</h3><strong className="cloud-suite-status">{status}</strong><p className="cloud-suite-session-prompt">{selected.prompt}</p>
          <small>{new Date(selected.createdAt * 1000).toLocaleString()} · {fmt(selected.durationSeconds)} · {selected.bpm} BPM</small>
          {(selected.status === 'running' || selected.status === 'queued') && <div className="cloud-suite-progress"><progress value={selected.completedSteps} max={Math.max(selected.totalSteps, 1)} /><span>{stageText[selected.stage] || 'Componiendo'} · {selected.completedSteps}/{selected.totalSteps}</span><button type="button" onClick={async () => { try { await api.cancelCloudJob(selected.id); setJobs(await api.cloudJobs()) } catch (problem) { setError(problem instanceof Error ? problem.message : 'No se pudo cancelar.') } }}>Cancelar</button></div>}
          {selected.status === 'completed' && selected.resultManifest && <div className="cloud-suite-session-actions"><a href={trackUrl(selected.id, selected.resultManifest.fullFile)}>↓ Obra completa MIDI</a><span>{selected.resultManifest.tracks.length} pistas · {selected.resultManifest.key}</span></div>}
          {selected.status === 'failed' && <p className="cloud-suite-error">La composición no se completó. No se publicó una obra incompleta.</p>}</> : <p className="cloud-suite-empty-copy">Tus obras aparecerán aquí cuando empieces a componer.</p>}</div>
    </div></aside>
      <div className="cloud-suite-arrangement"><div className="cloud-suite-transport"><div className="cloud-suite-player"><button type="button" onClick={() => audio.seek(0)} disabled={!activeScore} aria-label="Volver al inicio">⏮</button><button type="button" className="cloud-suite-play" onClick={() => void togglePlay()} disabled={!activeScore} aria-label={playing ? 'Pausar' : 'Reproducir'}>{playing ? 'Ⅱ' : '▶'}</button><button type="button" onClick={() => { audio.stop(); setPlaying(false) }} disabled={!activeScore} aria-label="Detener">■</button><span ref={timeRef} className="cloud-suite-time">00:00 / {fmt(duration)}</span></div><div className="cloud-suite-transport-meta"><label htmlFor="cloud-listen-mode">Escucha</label><select id="cloud-listen-mode" aria-label="Modo de escucha" value={listeningMode} onChange={event => { const mode = event.target.value as 'production' | 'neutral'; audio.setMode(mode); setListeningMode(mode) }}><option value="production">Producción</option><option value="neutral">MIDI neutro</option></select><button type="button" className="cloud-suite-preview-wav" disabled={!activeScore || renderingWav} onClick={() => void downloadWav()} title="Descargar 30 segundos de audio WAV desde la posición actual, con los sonidos elegidos. No consume API.">{renderingWav ? 'Renderizando…' : 'WAV 30 s'}</button><span>{selected?.resultManifest?.bpm || selected?.bpm || '—'} BPM</span><span>{selected?.resultManifest?.key || '—'}</span><label htmlFor="cloud-volume">Volumen</label><input id="cloud-volume" type="range" min="0" max="100" defaultValue="72" onChange={event => audio.setVolume(Number(event.target.value) / 100)} /></div></div>
        <div className="cloud-suite-score-head"><div><div className="cloud-suite-kicker">03 / PARTITURA</div><h2>{selected?.resultManifest?.title || 'El estudio está listo'}</h2><p>{activeScore ? `${selected?.resultManifest?.key || '—'} · ${bars} compases · ${activeScore.tracks.length} pistas con notas` : selected?.status === 'completed' ? 'Preparando las pistas MIDI…' : 'Elige una obra terminada para escuchar su partitura.'}</p></div><span>{activeScore ? `${activeScore.tracks.reduce((total, track) => total + track.notes.length, 0).toLocaleString('es-AR')} NOTAS · ${activeScore.tracks.length} PISTAS` : 'MIDI REAL · ESCUCHA PULSO'}</span></div>
        <div className="cloud-suite-timeline" ref={timelineRef}>{activeScore ? <div className="cloud-suite-content" style={{ width: 205 + timelineWidth }}><div className="cloud-suite-ruler"><div className="cloud-suite-ruler-label">{bars} COMPASES</div><div className="cloud-suite-ruler-marks" style={{ width: timelineWidth }} onClick={seekAt}>{Array.from({ length: Math.floor(bars / (bars > 240 ? 16 : bars > 80 ? 8 : 4)) + 1 }, (_, i) => { const interval = bars > 240 ? 16 : bars > 80 ? 8 : 4; return <span key={i} style={{ left: i * interval * 22 }}>{i * interval + 1}</span> })}</div></div>
          {activeScore.tracks.map((track, index) => <div className="cloud-suite-track" key={`${track.filename}-${index}`}><div className="cloud-suite-track-label" style={{ '--suite-track-color': track.color } as CSSProperties}><div className="cloud-suite-track-name"><i /><b title={track.name}>{track.name}</b></div><div className="cloud-suite-track-controls"><button type="button" className={audio.muted.has(index) ? 'active' : ''} aria-label={`Silenciar ${track.name}`} aria-pressed={audio.muted.has(index)} onClick={() => toggleMute(index)}>M</button><button type="button" className={audio.soloed.has(index) ? 'active' : ''} aria-label={`Escuchar solo ${track.name}`} aria-pressed={audio.soloed.has(index)} onClick={() => toggleSolo(index)}>S</button>{listeningMode === 'production' ? <select className="cloud-suite-sound-select" aria-label={`Sonido de ${track.name}`} title={`Cambiar el sonido de ${track.name} sin alterar su MIDI`} value={track.patchId} onChange={event => changePatch(index, event.target.value)}>{patchOptions(PATCHES[track.patchId || '']?.family || 'synth').map(id => <option value={id} key={id}>{PATCHES[id].label}</option>)}</select> : <small>{neutralLabels[track.instrument] || neutralLabels.synth}</small>}<a href={trackUrl(activeScore.jobId, track.filename)} aria-label={`Descargar MIDI de ${track.name}`} title={`Descargar MIDI de ${track.name}`}>↓</a></div></div><div className="cloud-suite-lane" style={{ width: timelineWidth }} onClick={seekAt}><MidiLane notes={track.notes} color={track.color} width={timelineWidth} beatsPerBar={activeScore.midi.beatsPerBar} /></div></div>)}<div className="cloud-suite-playhead" ref={playheadRef} style={{ left: 205 }} /></div> : <div className="cloud-suite-timeline-empty"><span>◫</span><h3>{scoreLoading ? 'Leyendo las notas MIDI…' : scoreError || 'Una vista para escuchar la composición.'}</h3><p>{scoreLoading ? 'La obra se carga desde tus archivos Cloud, sin generar una nueva composición.' : 'Las notas y silencios aparecerán aquí tal como están en los MIDI exportados.'}</p></div>}</div>
        <div className="cloud-suite-footer"><span>{scoreError || (listeningMode === 'production' ? 'Sonidos PULSO elegidos por función musical. Cambiá a MIDI neutro para auditar la partitura; las notas no cambian.' : 'Monitor senoidal: todas las notas tonales usan el mismo sonido.')}</span><span>{activeScore?.tracks.length || 0} pistas · {fmt(duration)}</span></div>
      </div></div>
    {historyOpen && <aside className="cloud-suite-history" aria-label="Historial de composiciones"><div><h2>Tus obras</h2><button type="button" onClick={() => setHistoryOpen(false)} aria-label="Cerrar historial">×</button></div>{jobs.length ? jobs.map(job => <button type="button" key={job.id} className={job.id === selectedId ? 'selected' : ''} onClick={() => { setSelectedId(job.id); setHistoryOpen(false) }}><strong>{job.resultManifest?.title || job.prompt.slice(0, 72)}</strong><span>{job.status === 'completed' ? 'LISTA' : job.status === 'failed' ? 'NO COMPLETADA' : stageText[job.stage] || job.status} · {new Date(job.createdAt * 1000).toLocaleString()}</span></button>) : <p>Aquí aparecerán tus composiciones Cloud.</p>}</aside>}
    <span hidden>{mixRevision}</span>
  </section>
}
