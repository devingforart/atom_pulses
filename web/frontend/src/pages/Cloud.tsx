import { t, useLocale, locale, useTheme, Preferences } from '../i18n'
import { useEffect, useMemo, useRef, useState, type CSSProperties, type FormEvent, type MouseEvent } from 'react'
import { Link, Navigate, useParams } from 'react-router-dom'
import { api, type CloudJob, type CloudTrack } from '../api'
import { useAuth } from '../auth'
import { Logo } from '../components/Logo'
import { PulsoSpinner } from '../components/PulsoSpinner'
import { parseMidi, downloadSingleTrackMidi, type MidiNote, type MidiScore } from '../../../shared/midi.mjs'
import { instrumentFor, type SuiteTrack } from '../../../shared/audio-engine.mjs'
import { ProductionAudio } from '../../../shared/production-audio.mjs'
import { PATCHES, SOUND_BANK_VERSION, patchForTrack, patchOptions } from '../../../shared/sound-palette.mjs'
import './CloudSuite.css'
import './CloudEditorial.css'

function SuiteDialog({ title, onClose, children }: { title: string; onClose: () => void; children: React.ReactNode }) {
  useLocale();
  const ref = useRef<HTMLDialogElement>(null)
  useEffect(() => { ref.current?.showModal() }, [])
  return <dialog ref={ref} className="cloud-studio-dialog" aria-label={t(title)} onCancel={onClose} onClose={onClose}>
    <div className="cloud-dialog-header"><span>{t(title)}</span><button type="button" onClick={onClose} aria-label={`Cerrar ${t(title)}`}>{t("×")}</button></div>{children}
  </dialog>
}

type AudibleTrack = SuiteTrack & { color: string }
type AudibleScore = { jobId: string; midi: MidiScore; tracks: AudibleTrack[] }

const stageText: Record<string, string> = {
  queued: 'Esperando turno', starting: 'Preparando la obra', blueprint: 'Diseñando la composición',
  writing: 'Escribiendo las pistas', recovery: 'Afinando pasajes', validation: 'Revisando la obra',
  rendering: 'Preparando los MIDI', ready: 'Lista para escuchar',
  interrupted: 'Interrumpida', generation: 'La composición no se completó',
}
const palette: Record<string, string> = { pad: '#dfb64e', bass: '#71a1d0', keys: '#75b57c', pluck: '#e37957', lead: '#e37957', drums: '#75b57c', synth: '#cfd0c7' }
const neutralLabels: Record<string, string> = { pad: 'Seno · armonía', bass: 'Seno · bajo', keys: 'Seno · teclas', pluck: 'Seno · arpegio', lead: 'Seno · melodía', drums: 'Seno · percusión', synth: 'Seno · pista' }
const fmt = (seconds: number) => { const value = Math.max(0, Math.floor(seconds || 0)); return `${String(Math.floor(value / 60)).padStart(2, '0')}:${String(value % 60).padStart(2, '0')}` }
const trackUrl = (jobId: string, filename: string, shareToken?: string) => shareToken
  ? `/api/exhibitions/${encodeURIComponent(shareToken)}/tracks/${encodeURIComponent(filename)}`
  : `/api/cloud/jobs/${encodeURIComponent(jobId)}/tracks/${encodeURIComponent(filename)}`

async function loadScore(job: CloudJob, signal: AbortSignal, shareToken?: string): Promise<AudibleScore> {
  const detail = shareToken ? job : await api.cloudJob(job.id)
  if (signal.aborted) throw new Error(t("Carga cancelada"))
  const manifest = detail.resultManifest
  if (!manifest?.tracks?.length) throw new Error(t("Esta obra no tiene pistas MIDI disponibles."))
  const parts: { track: CloudTrack; midi: MidiScore }[] = []
  for (let start = 0; start < manifest.tracks.length; start += 6) {
    const batch = await Promise.all(manifest.tracks.slice(start, start + 6).map(async track => {
      const response = await fetch(trackUrl(job.id, track.filename, shareToken), { credentials: shareToken ? 'omit' : 'include', cache: 'no-store', signal })
      if (!response.ok) throw new Error(t("No se pudo abrir {p0} (HTTP {p1}).", {p0: track.name, p1: response.status}))
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
  if (!tracks.length) throw new Error(t("La obra no contiene notas reproducibles."))
  return { jobId: job.id, midi, tracks }
}

function MidiLane({ notes, color, width, beatsPerBar }: { notes: MidiNote[]; color: string; width: number; beatsPerBar: number }) {
  useLocale();
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

export function SharedCloud() {
  useLocale();
  const { token } = useParams()
  return <Cloud shareToken={token || 'invalid'} />
}

export function Cloud({ shareToken }: { shareToken?: string } = {}) {
  useLocale();
  const shared = Boolean(shareToken)
  const { user, loading } = useAuth()
  const [jobs, setJobs] = useState<CloudJob[]>([])
  const [libraryLoading, setLibraryLoading] = useState(true)
  const [selectedId, setSelectedId] = useState<string | null>(null)
  const [historyOpen, setHistoryOpen] = useState(false)
  const [composeOpen, setComposeOpen] = useState(false)
  const [sessionOpen, setSessionOpen] = useState(false)
  const { theme } = useTheme()
  const [draftLoaded, setDraftLoaded] = useState(false)
  const [score, setScore] = useState<AudibleScore | null>(null)
  const [scoreError, setScoreError] = useState('')
  const [scoreLoading, setScoreLoading] = useState(false)
  const [playing, setPlaying] = useState(false)
  const [mixRevision, setMixRevision] = useState(0)
  const [listeningMode, setListeningMode] = useState<'piano' | 'neutral' | 'production'>('piano')
  const [renderingWav, setRenderingWav] = useState(false)
  const [renderingFullWav, setRenderingFullWav] = useState(false)
  const [fullWavProgress, setFullWavProgress] = useState(0)
  const [downloadingSingleMidi, setDownloadingSingleMidi] = useState(false)
  const [prompt, setPrompt] = useState('')
  const [allowChromatic, setAllowChromatic] = useState(false)
  const [club, setClub] = useState(false)
  const [sharing, setSharing] = useState(false)
  const [share, setShare] = useState<{ url: string; jobId: string } | null>(null)
  const [durationSeconds, setDurationSeconds] = useState(390)
  const [bpm, setBpm] = useState(120)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  const [cloudStatus, setCloudStatus] = useState<{ available: boolean; dailyJobLimit: number; unlimited: boolean } | null>(null)
  const timelineRef = useRef<HTMLDivElement>(null)
  const playheadRef = useRef<HTMLDivElement>(null)
  const timeRef = useRef<HTMLSpanElement>(null)
  const audioRef = useRef<ProductionAudio | null>(null)
  if (!audioRef.current) audioRef.current = new ProductionAudio(seconds => {
    const audio = audioRef.current
    if (!audio) return
    if (timeRef.current) timeRef.current.textContent = `${fmt(seconds)} / ${fmt(audio.duration)}`
    if (audio.midi && playheadRef.current) {
      const x = 222 + audio.midi.secondsToBeat(seconds) / audio.midi.beatsPerBar * 22
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

  useEffect(() => {
    if (!user || shared) return
    try {
      const draft = JSON.parse(localStorage.getItem(`pulso-cloud-draft:${user.id}`) || '{}')
      if (typeof draft.prompt === 'string') setPrompt(draft.prompt.slice(0, 600))
      if ([60,120,180,300,390,600,900].includes(draft.durationSeconds)) setDurationSeconds(draft.durationSeconds)
      if (Number.isFinite(draft.bpm) && draft.bpm >= 60 && draft.bpm <= 180) setBpm(draft.bpm)
      setClub(draft.club === true); setAllowChromatic(draft.allowChromatic === true)
    } catch { /* A malformed draft must not block the studio. */ }
    setDraftLoaded(true)
  }, [user?.id, shared])
  useEffect(() => {
    if (!user || shared || !draftLoaded) return
    try { localStorage.setItem(`pulso-cloud-draft:${user.id}`, JSON.stringify({ prompt, durationSeconds, bpm, club, allowChromatic })) } catch {}
  }, [user?.id, shared, draftLoaded, prompt, durationSeconds, bpm, club, allowChromatic])

  useEffect(() => { if (user && !shared) void api.cloudStatus().then(setCloudStatus).catch(() => setCloudStatus(null)) }, [user, shared])
  useEffect(() => {
    if (shareToken) {
      let active = true
      void api.sharedCloudJob(shareToken).then(job => { if (active) setJobs([job]) })
        .catch(problem => { if (active) setScoreError(problem instanceof Error ? problem.message : t("Exhibición no disponible.")) })
        .finally(() => { if (active) setLibraryLoading(false) })
      return () => { active = false }
    }
    if (!user) return
    let active = true
    const refresh = async () => {
      try { const result = await api.cloudJobs(); if (active) { setJobs(result); setError('') } }
      catch (problem) { if (active) setError(problem instanceof Error ? problem.message : t("No pudimos leer tus obras.")) }
      finally { if (active) setLibraryLoading(false) }
    }
    void refresh()
    const timer = window.setInterval(() => void refresh(), 5000)
    return () => { active = false; window.clearInterval(timer) }
  }, [user, shareToken])
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
    void loadScore(selected, controller.signal, shareToken).then(result => { if (!controller.signal.aborted) { setScore(result); setScoreLoading(false) } })
      .catch(problem => { if (!controller.signal.aborted) { setScoreError(problem instanceof Error ? problem.message : t("No se pudo leer el MIDI.")); setScoreLoading(false) } })
    return () => controller.abort()
    // The signature changes only when the selected MIDI changes, not on every status poll.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [selectedFile, shareToken])
  useEffect(() => { if (score && score.jobId === selectedId) { audio.setSong(score.midi, score.tracks); setMixRevision(0) } }, [score, selectedId, audio])
  useEffect(() => () => audio.stop(), [audio])

  if (loading && !shared) return <section className="cloud-suite-loading">{t("Abriendo PULSO Cloud…")}</section>
  if (!user && !shared) return <Navigate to="/login" replace />

  async function compose(event: FormEvent<HTMLFormElement>) {
    event.preventDefault(); setError(''); setBusy(true)
    try {
      const job = await api.createCloudJob({ prompt: prompt.trim(), durationSeconds,
        bpm, allowChromatic, club, idempotencyKey: crypto.randomUUID() })
      setJobs(previous => [job, ...previous.filter(item => item.id !== job.id)])
      setSelectedId(job.id); setComposeOpen(false)
    } catch (problem) { setError(problem instanceof Error ? problem.message : t("No pudimos comenzar la composición.")) }
    finally { setBusy(false) }
  }
  async function togglePlay() {
    if (!activeScore) return
    try { if (audio.playing) audio.pause(); else await audio.play(); setPlaying(audio.playing) }
    catch (problem) { setScoreError(problem instanceof Error ? problem.message : t("No se pudo iniciar el audio.")) }
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
    } catch (problem) { setScoreError(problem instanceof Error ? problem.message : t("No se pudo preparar el audio WAV.")) }
    finally { setRenderingWav(false) }
  }
  async function downloadFullWav(format: 'wav' | 'mp3' = 'wav') {
    if (!activeScore || renderingFullWav) return
    const jobId = activeScore.jobId
    setRenderingFullWav(true); setFullWavProgress(0); setScoreError('')
    try {
      const wav = await audio.renderFullWav(progress => setFullWavProgress(Math.round(progress * (format === 'mp3' ? 70 : 100))))
      const blob = format === 'mp3' ? await (await import('../mp3')).encodeMp3(wav, progress => setFullWavProgress(70 + Math.round(progress * 30))) : wav
      const url = URL.createObjectURL(blob)
      const link = document.createElement('a')
      link.href = url
      link.download = `pulso-${jobId.slice(0, 8)}-full-song.${format}`
      link.click()
      window.setTimeout(() => URL.revokeObjectURL(url), 60000)
    } catch (problem) { setScoreError(problem instanceof Error ? problem.message : t("No se pudo exportar el audio completo.")) }
    finally { setRenderingFullWav(false) }
  }
  async function downloadMergedMidi() {
    if (!activeScore || !selected?.resultManifest?.fullFile || downloadingSingleMidi) return
    setDownloadingSingleMidi(true); setScoreError('')
    try {
      await downloadSingleTrackMidi(trackUrl(activeScore.jobId, selected.resultManifest.fullFile),
        t("pulso-{p0}-todas-las-notas.mid", {p0: activeScore.jobId.slice(0, 8)}))
    } catch (problem) { setScoreError(problem instanceof Error ? problem.message : t("No se pudo crear el MIDI de una pista.")) }
    finally { setDownloadingSingleMidi(false) }
  }
  const status = selected?.status === 'completed' ? t('LISTA') : selected?.status === 'failed' ? t("NO COMPLETADA") :
    selected?.status === 'cancelled' ? t("CANCELADA") : selected ? t(stageText[selected.stage]) || t("COMPONIENDO") : t("SIN OBRAS")

  return <section data-theme={theme} className={`cloud-suite cloud-editorial${shared ? ' cloud-exhibition' : ''}`}>
    <header className="cloud-suite-bar">
      <div className="cloud-suite-brand"><a className="cloud-brand-home" href="https://pulso.cargo-core.com/" target="_blank" rel="noopener noreferrer" aria-label={t("Página principal de PULSO")}><Logo /></a><span className="cloud-suite-brand-divider" /><strong>{t("COMPOSITION STUDIO")}</strong></div>
      <div className="cloud-suite-global"><Preferences />{shared ? <span className="cloud-exhibition-badge">{t("EXHIBICIÓN COMPARTIDA · SOLO LECTURA")}</span> : <><span className="cloud-suite-online">{t("● EN TU CUENTA")}</span><Link to="/account">{t("Mi cuenta")}</Link><button type="button" disabled={selected?.status !== 'completed' || sharing} onClick={async () => { if (!selected) return; setSharing(true); try { const result = await api.shareCloudJob(selected.id); setShare({ ...result, jobId: selected.id }) } catch (problem) { setError(problem instanceof Error ? problem.message : t("No se pudo compartir.")) } finally { setSharing(false) } }}>{sharing ? t("Preparando…") : t("Share")}</button><button type="button" onClick={() => setHistoryOpen(value => !value)}>{t("Historial")}</button></>}</div>
    </header>
    <div className="cloud-statusbar"><div className="cloud-beta">{t("0.1.0 version Beta")}</div>
    {!shared && <button type="button" className="cloud-session-indicator" onClick={() => setSessionOpen(true)}>{status} · {selected?.resultManifest?.title || t("Seguimiento de la obra")} ↗</button>}
    </div>
    {error && !composeOpen && <p role="alert" className="cloud-suite-error cloud-global-error">{error}</p>}
    <div className="cloud-suite-layout">{!shared && <aside className="cloud-focus-rail" aria-label={t("Herramientas del estudio")}><button type="button" className="cloud-new-idea" title={t("Nueva composición")} aria-label={t("Nueva composición")} onClick={() => { setHistoryOpen(false); setComposeOpen(true) }}><svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="m5 19 1-5L16 4l4 4-10 10-5 1Zm9-13 4 4"/></svg></button><button type="button" title={t("Explorar biblioteca")} aria-label={t("Explorar biblioteca")} onClick={() => setHistoryOpen(value => !value)}><svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M5 5v14m5-14v14m5-14 4 14"/></svg></button><button type="button" title={t("Seguimiento de la obra")} aria-label={t("Seguimiento de la obra")} onClick={() => setSessionOpen(true)}><svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M4 18V9m5 9V5m5 13v-7m5 7V7"/></svg></button></aside>}
    {!shared && composeOpen && <SuiteDialog title={t("Nueva composición")} onClose={() => setComposeOpen(false)}>
      <div className="cloud-suite-compose">
      <div className="cloud-suite-kicker"><span>{t("01 / DIRECCIÓN MUSICAL")}</span></div><h1>{t("Tu próxima")}{' '}<em>{t("idea.")}</em></h1>
      <p className="cloud-suite-intro">{t("Imaginá la dirección. Explorá sus voces.")}</p>
      <form onSubmit={event => void compose(event)}><label htmlFor="cloud-prompt">{t("Idea musical")}{' '}<small>{prompt.length} / 600</small></label>
        <textarea id="cloud-prompt" value={prompt} maxLength={600} required rows={5} onChange={event => setPrompt(event.target.value)} placeholder={t("Una historia electrónica profunda, con armonías que evolucionan y un motivo que regresa transformado…")} />
        <label className="cloud-chromatic-option"><input type="checkbox" checked={allowChromatic} onChange={event => setAllowChromatic(event.target.checked)} />{' '}{t("Permitir cromatismos")}</label>
        <label className="cloud-chromatic-option" title={t("Células reconocibles y evolución de capas a largo plazo, sin cortes obligatorios cada 64 compases.")}><input type="checkbox" checked={club} onChange={event => setClub(event.target.checked)} />{' '}{t("Club · repetición y evolución")}</label>
        <p className="cloud-suite-cost">{t("Sin baterías ni percusiones. Arpegios, stabs y ritmos tonales siguen disponibles. Con cromatismos desactivados, las notas deben pertenecer a la escala declarada.")}</p>
        <div className="cloud-suite-field-pair"><div><label htmlFor="cloud-duration">{t("Duración")}</label><select id="cloud-duration" value={durationSeconds} onChange={event => setDurationSeconds(Number(event.target.value))}>{[[60, t("1 minuto")], [120, t("2 minutos")], [180, t("3 minutos")], [300, t("5 minutos")], [390, t("6 min 30 s")], [600, t("10 minutos")], [900, t("15 minutos")]].map(([value, label]) => <option key={value} value={value}>{label}</option>)}</select></div>
          <div><label htmlFor="cloud-bpm">{t("Tempo")}</label><div className="cloud-suite-unit"><input id="cloud-bpm" type="number" min="60" max="180" value={bpm} onChange={event => setBpm(Number(event.target.value))} /><span>{t("BPM")}</span></div></div></div>
        <button className="cloud-suite-compose-button" disabled={busy || !prompt.trim() || cloudStatus?.available !== true}>{busy ? t("Enviando…") : t("✦  Componer obra ↗")}</button>
        <p className="cloud-suite-cost">{cloudStatus?.available ? cloudStatus.unlimited ? t("Acceso ilimitado para esta cuenta. Solo puede haber una composición en curso.") : t("Hasta {p0} obras completadas cada 24 horas. Los intentos fallidos no cuentan.", {p0: cloudStatus.dailyJobLimit}) : t("Cloud no está habilitado para esta cuenta.")}{' '}{t("Escuchar obras guardadas no consume créditos. La composición continúa aunque cierres la página.")}</p>
        {error && <p className="cloud-suite-error" role="alert">{error}</p>}
      </form></div></SuiteDialog>}
      {!shared && sessionOpen && <SuiteDialog title={t("Seguimiento de la obra")} onClose={() => setSessionOpen(false)}>
      <div className="cloud-suite-session"><div className="cloud-suite-kicker"><span>{t("02 / SESIÓN")}</span><span>{jobs.length}{' '}{t("OBRAS")}</span></div>
        {selected ? <><h3>{selected.resultManifest?.title || t("Composición en curso")}</h3><strong className="cloud-suite-status">{status}</strong><p className="cloud-suite-session-prompt">{selected.prompt}</p>
          <small>{new Date(selected.createdAt * 1000).toLocaleString(locale())} · {fmt(selected.durationSeconds)} · {selected.bpm}{' '}{t("BPM")}</small>
          {(selected.status === 'running' || selected.status === 'queued') && <div className="cloud-suite-progress"><progress value={selected.completedSteps} max={Math.max(selected.totalSteps, 1)} /><span>{t(stageText[selected.stage]) || t("Componiendo")} · {selected.completedSteps}/{selected.totalSteps}</span><button type="button" onClick={async () => { try { await api.cancelCloudJob(selected.id); setJobs(await api.cloudJobs()) } catch (problem) { setError(problem instanceof Error ? problem.message : t("No se pudo cancelar.")) } }}>{t("Cancelar")}</button></div>}
          {selected.status === 'completed' && selected.resultManifest && <div className="cloud-suite-session-actions"><a href={trackUrl(selected.id, selected.resultManifest.fullFile)}>{t("↓ Obra completa MIDI")}</a><span>{selected.resultManifest.tracks.length}{' '}{t("pistas ·")}{' '}{selected.resultManifest.key}</span></div>}
          {selected.status === 'failed' && <p className="cloud-suite-error">{t("La composición no se completó. No se publicó una obra incompleta.")}</p>}</> : <p className="cloud-suite-empty-copy">{t("Tus obras aparecerán aquí cuando empieces a componer.")}</p>}</div>
    </SuiteDialog>}
      <div className="cloud-suite-arrangement"><div className="cloud-suite-transport"><div className="cloud-suite-player"><button type="button" onClick={() => audio.seek(0)} disabled={!activeScore} aria-label={t("Volver al inicio")}>⏮</button><button type="button" className="cloud-suite-play" onClick={() => void togglePlay()} disabled={!activeScore} aria-label={playing ? t("Pausar") : t("Reproducir")}>{playing ? 'Ⅱ' : '▶'}</button><button type="button" onClick={() => { audio.stop(); setPlaying(false) }} disabled={!activeScore} aria-label={t("Detener")}>■</button><span ref={timeRef} className="cloud-suite-time">00:00 / {fmt(duration)}</span></div><div className="cloud-suite-transport-meta"><label htmlFor="cloud-listen-mode">{t("Escucha")}</label><select id="cloud-listen-mode" aria-label={t("Modo de escucha")} value={listeningMode} onChange={event => { const mode = event.target.value as 'piano' | 'neutral'; void audio.setMode(mode).then(() => setListeningMode(mode)).catch(problem => setScoreError(problem instanceof Error ? problem.message : t("No se pudo cargar el piano."))) }}><option value="piano">{t("Piano MIDI")}</option><option value="neutral">{t("MIDI neutro")}</option></select><button type="button" className="cloud-suite-preview-wav" disabled={!activeScore || renderingWav} onClick={() => void downloadWav()} title={t("Descargar 30 segundos de audio WAV desde la posición actual, con el modo de escucha elegido. No consume API.")}>{renderingWav ? t("Renderizando…") : t("WAV 30 s")}</button><label htmlFor="cloud-volume">{t("Volumen")}</label><input id="cloud-volume" type="range" min="0" max="100" defaultValue="72" onChange={event => audio.setVolume(Number(event.target.value) / 100)} /></div></div>
        <div className="cloud-suite-score-head"><div><div className="cloud-suite-kicker">{t("02 / COMPOSICIÓN & EXPLORACIÓN")}</div><h2>{selected?.resultManifest?.title || t("Espacio para tu próxima idea.")}</h2><p className="cloud-score-facts">{activeScore ? <><strong>{selected?.resultManifest?.key || '—'}</strong><span>{bars} {t("compases")}</span><span>{fmt(duration)}</span><span>{selected?.resultManifest?.bpm || selected?.bpm || '—'} {t("BPM")}</span><span>{activeScore.tracks.length} {t("pistas")}</span></> : selected?.status === 'completed' ? t("Preparando las pistas MIDI…") : t("Elige una obra terminada para escuchar su partitura.")}</p></div>{!shared && activeScore && selected?.status === 'completed' && <details className="score-export-menu"><summary>{renderingFullWav ? t("Exportando audio {p0}%", {p0: fullWavProgress}) : t("Descargar obra")} <svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M12 3v12m-4-4 4 4 4-4M5 16v4h14v-4"/></svg></summary><div className="score-export-options"><div className="score-export-caption">{t("Tu composición, a tu manera.")}</div><button type="button" disabled={!activeScore || downloadingSingleMidi} onClick={() => void downloadMergedMidi()}><span className="export-format">MIDI</span><span><b>{t("1 pista MIDI")}</b><small>{t("Todas las notas con un instrumento")}</small></span><span>↓</span></button>{selected?.status === 'completed' && selected.resultManifest ? <a href={trackUrl(selected.id,selected.resultManifest.fullFile)}><span className="export-format">MIDI</span><span><b>{t("Full song MIDI")}</b><small>{t("Voces separadas · multipista")}</small></span><span>↓</span></a> : <button disabled><span className="export-format">MIDI</span><span><b>{t("Full song MIDI")}</b><small>{t("Voces separadas · multipista")}</small></span><span>↓</span></button>}<button type="button" disabled={!activeScore || renderingFullWav} onClick={() => void downloadFullWav('mp3')}><span className="export-format">MP3</span><span><b>{t("Audio MP3")}</b><small>{t("Obra completa · 192 kbps")}</small></span><span>↓</span></button><button type="button" disabled={!activeScore || renderingFullWav} onClick={() => void downloadFullWav()}><span className="export-format">WAV</span><span><b>{t("Audio WAV")}</b><small>{t("Obra completa · sin compresión")}</small></span><span>↓</span></button><p>{t("El audio usa el sonido y las pistas audibles actuales.")}</p></div></details>}</div>
        <div className="cloud-suite-timeline" ref={timelineRef}>{activeScore ? <div className="cloud-suite-content" style={{ width: 222 + timelineWidth }}><div className="cloud-suite-ruler"><div className="cloud-suite-ruler-label">{bars}{' '}{t("COMPASES")}</div><div className="cloud-suite-ruler-marks" style={{ width: timelineWidth }} onClick={seekAt}>{Array.from({ length: Math.floor(bars / (bars > 240 ? 16 : bars > 80 ? 8 : 4)) + 1 }, (_, i) => { const interval = bars > 240 ? 16 : bars > 80 ? 8 : 4; return <span key={i} style={{ left: i * interval * 22 }}>{i * interval + 1}</span> })}</div></div>
          {activeScore.tracks.map((track, index) => <div className="cloud-suite-track" key={`${track.filename}-${index}`}><div className="cloud-suite-track-label" style={{ '--suite-track-color': track.color } as CSSProperties}><div className="cloud-suite-track-name"><i /><b title={track.name}>{track.name}</b></div><div className="cloud-suite-track-controls"><button type="button" className={audio.muted.has(index) ? 'active' : ''} aria-label={t("Silenciar {p0}", {p0: track.name})} aria-pressed={audio.muted.has(index)} onClick={() => toggleMute(index)}>{t("M")}</button><button type="button" className={audio.soloed.has(index) ? 'active' : ''} aria-label={t("Escuchar solo {p0}", {p0: track.name})} aria-pressed={audio.soloed.has(index)} onClick={() => toggleSolo(index)}>{t("S")}</button>{listeningMode === 'production' ? <select className="cloud-suite-sound-select" aria-label={`Sonido de ${track.name}`} title={t("Cambiar el sonido de {p0} sin alterar su MIDI", {p0: track.name})} value={track.patchId} onChange={event => changePatch(index, event.target.value)}>{patchOptions(PATCHES[track.patchId || '']?.family || 'synth').map(id => <option value={id} key={id}>{PATCHES[id].label}</option>)}</select> : <small>{listeningMode === 'piano' ? t("Piano MIDI") : t(neutralLabels[track.instrument]) || t(neutralLabels.synth)}</small>}<a href={trackUrl(activeScore.jobId, track.filename)} aria-label={t("Descargar MIDI de {p0}", {p0: track.name})} title={t("Descargar MIDI de {p0}", {p0: track.name})}>↓</a></div></div><div className="cloud-suite-lane" style={{ width: timelineWidth }} onClick={seekAt}><MidiLane notes={track.notes} color={track.color} width={timelineWidth} beatsPerBar={activeScore.midi.beatsPerBar} /></div></div>)}<div className="cloud-suite-playhead" ref={playheadRef} style={{ left: 222 }} /></div> : <div className="cloud-suite-timeline-empty" aria-busy={libraryLoading || scoreLoading || selected?.status === 'running' || selected?.status === 'queued'}>{scoreError || error ? <><h3>{scoreError || error}</h3></> : libraryLoading || scoreLoading ? <PulsoSpinner label={t(scoreLoading ? "Leyendo las notas MIDI…" : "Abriendo tu biblioteca…")} /> : selected?.status === 'running' || selected?.status === 'queued' ? <PulsoSpinner label={t(stageText[selected.stage]) || t("COMPONIENDO")} /> : !jobs.length && !shared ? <><h3>{t("No has generado obras aún.")}</h3><p className="cloud-first-work">{t("Pulsá el")} <button type="button" className="cloud-inline-pencil" aria-label={t("Crear tu primera obra")} onClick={() => setComposeOpen(true)}><svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="m5 19 1-5L16 4l4 4-10 10-5 1Zm9-13 4 4"/></svg></button> {t("de la barra lateral para crear tu primera obra.")}</p></> : <><h3>{t(selected?.status === 'failed' ? "La composición no se completó" : selected?.status === 'cancelled' ? "CANCELADA" : "Elige una obra terminada para escuchar su partitura.")}</h3></>}</div>}</div>
        <div className="cloud-suite-footer"><span>{scoreError || (listeningMode === 'piano' ? t("Piano MIDI: todas las pistas suenan con el mismo piano. El MIDI no cambia.") : t("MIDI neutro: monitor senoidal sin color instrumental. El MIDI no cambia."))}</span><span>{activeScore?.tracks.length || 0}{' '}{t("pistas ·")}{' '}{fmt(duration)}</span></div>
      </div></div>
    {historyOpen && !shared && <aside className="cloud-suite-history" aria-label={t("Historial de composiciones")}>
      <div className="cloud-suite-history-head"><div><span className="cloud-suite-kicker">{t("BIBLIOTECA CLOUD")}</span><h2>{t("Tus obras")}</h2></div><button className="cloud-suite-history-close" type="button" onClick={() => setHistoryOpen(false)} aria-label={t("Cerrar historial")}>{t("×")}</button></div>
      <button className="cloud-suite-history-refresh" type="button" onClick={async () => { try { setJobs(await api.cloudJobs()) } catch (problem) { setError(problem instanceof Error ? problem.message : t("No se pudo actualizar la biblioteca.")) } }}>{t("↻ Actualizar")}</button>
      <div className="cloud-suite-history-list">{jobs.length ? jobs.map(job => <button type="button" key={job.id} className={`cloud-suite-history-job ${job.id === selectedId ? 'selected' : ''}`} onClick={() => { setSelectedId(job.id); setHistoryOpen(false) }}>
        <strong>{job.resultManifest?.title || job.prompt.slice(0, 72)}</strong><span className={`cloud-suite-history-status ${job.status}`}>{job.status === 'completed' ? t('LISTA') : job.status === 'failed' ? t("NO COMPLETADA") : job.status === 'cancelled' ? t("CANCELADA") : t(stageText[job.stage]) || job.status}</span>
        <span>{new Date(job.createdAt * 1000).toLocaleString(locale(), { dateStyle: 'short', timeStyle: 'short' })} · {fmt(job.durationSeconds)} · {job.resultManifest?.tracks?.length || 0}{' '}{t("pistas")}</span>
      </button>) : <p className="cloud-suite-empty-copy">{t("Todavía no hay obras guardadas.")}</p>}</div>
    </aside>}
    {share && <CloudShareDialog url={share.url} close={() => setShare(null)} revoke={async () => { await api.revokeCloudShare(share.jobId); setShare(null) }} />}
    <span hidden>{mixRevision}</span>
  </section>
}

function CloudShareDialog({ url, close, revoke }: { url: string; close: () => void; revoke: () => Promise<void> }) {
  useLocale();
  const ref = useRef<HTMLDialogElement>(null)
  const [feedback, setFeedback] = useState('')
  const [busy, setBusy] = useState(false)
  useEffect(() => { const dialog = ref.current; dialog?.showModal(); return () => { dialog?.close() } }, [])
  return <dialog ref={ref} className="cloud-share-dialog" onClose={close} aria-labelledby="cloud-share-title">
    <h2 id="cloud-share-title">{t("Comparte tu obra")}</h2><p>{t("Quien tenga este enlace podrá ver la partitura y escucharla sin registrarse. Tu prompt, biblioteca y registros siguen privados.")}</p>
    <label htmlFor="cloud-share-url">{t("Enlace de exhibición")}</label><input id="cloud-share-url" readOnly value={url} onFocus={event => event.target.select()} />
    <div className="cloud-share-actions"><button type="button" onClick={async () => { try { await navigator.clipboard.writeText(url); setFeedback(t("Enlace copiado.")) } catch { setFeedback(t("Selecciona el enlace y cópialo con Ctrl+C o ⌘C.")) } }}>{t("Copiar enlace")}</button><a href={url} target="_blank" rel="noopener noreferrer">{t("Abrir exhibición")}</a><button type="button" onClick={close}>{t("Cerrar")}</button></div>
    <p role="status">{feedback}</p><button type="button" disabled={busy} onClick={async () => { setBusy(true); try { await revoke() } catch { setFeedback(t("No se pudieron desactivar los enlaces. Intenta nuevamente.")); setBusy(false) } }}>{t("Desactivar todos los enlaces de esta obra")}</button>
  </dialog>
}
