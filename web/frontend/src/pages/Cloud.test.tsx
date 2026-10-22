import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { Cloud } from './Cloud'

const mock = vi.hoisted(() => ({
  cloudJobs: vi.fn(), cloudJob: vi.fn(), cloudStatus: vi.fn(), createCloudJob: vi.fn(), cancelCloudJob: vi.fn(),
  shareCloudJob: vi.fn(), revokeCloudShare: vi.fn(), sharedCloudJob: vi.fn(),
  user: { id: 'user-1', email: 'test@example.com' } as { id: string; email: string } | null,
}))
vi.mock('../api', () => ({ api: mock }))
vi.mock('../auth', () => ({ useAuth: () => ({ user: mock.user, loading: false }) }))

const u16 = (value: number) => [(value >> 8) & 255, value & 255]
const u32 = (value: number) => [(value >>> 24) & 255, (value >>> 16) & 255, (value >>> 8) & 255, value & 255]
const midi = () => {
  const track = [0, 0x90, 60, 100, 0x83, 0x60, 0x80, 60, 0, 0, 0xff, 0x2f, 0]
  return Uint8Array.from([...new TextEncoder().encode('MThd'), ...u32(6), ...u16(0), ...u16(1), ...u16(480),
    ...new TextEncoder().encode('MTrk'), ...u32(track.length), ...track]).buffer
}

const job = {
  id: '6ce76fbb-bafd-440b-9d13-2e89c8910fac', prompt: 'Una melodía que responde a un acorde',
  durationSeconds: 60, bpm: 120, behavior: 'adaptive', aiSovereign: true, seed: '12345',
  status: 'completed', stage: 'ready', completedSteps: 1, totalSteps: 1, errorCode: null,
  createdAt: 1_800_000_000, updatedAt: 1_800_000_000,
  resultManifest: { title: 'Obra de prueba', key: 'C major', bpm: 120, bars: 1,
    fullFile: 'full-song.mid', tracks: [{ filename: 'track-01.mid', name: 'Protagonist lead', notes: 1,
      instrument: { catalog_id: 'lead_synth', source_voice: 'lead', department: 'melody' } }] },
}

beforeEach(() => {
  mock.user = { id: 'user-1', email: 'test@example.com' }
  mock.shareCloudJob.mockReset().mockResolvedValue({ url: 'https://pulso.cargo-core.com/share/private-token' })
  mock.revokeCloudShare.mockReset().mockResolvedValue(undefined)
  mock.sharedCloudJob.mockReset().mockResolvedValue({ ...job, prompt: '' })
  Object.defineProperty(HTMLDialogElement.prototype, 'showModal', { configurable: true, value: function (this: HTMLDialogElement) { this.setAttribute('open', '') } })
  Object.defineProperty(HTMLDialogElement.prototype, 'close', { configurable: true, value: function (this: HTMLDialogElement) { this.removeAttribute('open') } })
  mock.cloudJobs.mockReset().mockResolvedValue([job])
  mock.cloudJob.mockReset().mockResolvedValue(job)
  mock.cloudStatus.mockReset().mockResolvedValue({ available: true, dailyJobLimit: 2, unlimited: false })
  mock.createCloudJob.mockReset().mockResolvedValue({ ...job, id: 'new-job', status: 'queued', resultManifest: null })
  mock.cancelCloudJob.mockReset()
  vi.stubGlobal('fetch', vi.fn(async () => new Response(midi(), { status: 200, headers: { 'Content-Type': 'audio/midi' } })))
  localStorage.clear()
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue({ scale() {}, fillRect() {}, set fillStyle(_value: string) {}, set globalAlpha(_value: number) {} } as unknown as CanvasRenderingContext2D)
})
afterEach(() => { cleanup(); vi.restoreAllMocks() })

describe('Cloud Suite', () => {
  it('sends Club independently of chromatic permission', async () => {
    mock.createCloudJob.mockReset().mockResolvedValue({ ...job, behavior: 'club' })
    render(<Cloud />)
    await waitFor(() => expect(screen.getByText(/hasta 2 obras completadas cada 24 horas/i)).toBeInTheDocument())
    fireEvent.change(screen.getByRole('textbox', { name: /Idea musical/i }), { target: { value: 'Progressive house oscuro' } })
    fireEvent.click(screen.getByRole('checkbox', { name: /Club/i }))
    fireEvent.click(screen.getByRole('button', { name: /Componer obra/i }))
    await waitFor(() => expect(mock.createCloudJob).toHaveBeenCalledWith(expect.objectContaining({
      club: true, allowChromatic: false, prompt: 'Progressive house oscuro',
    })))
  })
  it('opens a public exhibition without authentication, library access or composition requests', async () => {
    mock.user = null
    render(<Cloud shareToken="private-token" />)
    await waitFor(() => expect(screen.getByRole('button', { name: 'Reproducir' })).not.toBeDisabled())
    expect(screen.getByText(/EXHIBICIÓN COMPARTIDA/)).toBeInTheDocument()
    expect(mock.sharedCloudJob).toHaveBeenCalledWith('private-token')
    expect(mock.cloudJobs).not.toHaveBeenCalled()
    expect(mock.cloudStatus).not.toHaveBeenCalled()
    expect(mock.cloudJob).not.toHaveBeenCalled()
    expect(mock.createCloudJob).not.toHaveBeenCalled()
    expect(screen.queryByRole('button', { name: 'Historial' })).not.toBeInTheDocument()
    expect(screen.queryByLabelText(/Idea musical/)).not.toBeInTheDocument()
    expect(fetch).toHaveBeenCalledWith(expect.stringContaining('/api/exhibitions/private-token/tracks/'), expect.objectContaining({ credentials: 'omit' }))
  })
  it('creates and revokes an owner-authorized exhibition link', async () => {
    render(<Cloud />)
    const button = await screen.findByRole('button', { name: 'Share' })
    await waitFor(() => expect(button).not.toBeDisabled())
    fireEvent.click(button)
    expect(await screen.findByRole('dialog')).toBeInTheDocument()
    expect(mock.shareCloudJob).toHaveBeenCalledWith(job.id)
    expect(screen.getByLabelText('Enlace de exhibición')).toHaveValue('https://pulso.cargo-core.com/share/private-token')
    fireEvent.click(screen.getByRole('button', { name: 'Desactivar todos los enlaces de esta obra' }))
    await waitFor(() => expect(mock.revokeCloudShare).toHaveBeenCalledWith(job.id))
    await waitFor(() => expect(screen.queryByRole('dialog')).not.toBeInTheDocument())
  })
  it('shows the correct daily policy for regular and unlimited accounts', async () => {
    const regular = render(<Cloud />)
    await waitFor(() => expect(screen.getByText(/hasta 2 obras completadas cada 24 horas/i)).toBeInTheDocument())
    regular.unmount()
    mock.cloudStatus.mockResolvedValue({ available: true, dailyJobLimit: 2, unlimited: true })
    render(<Cloud />)
    await waitFor(() => expect(screen.getByText(/Acceso ilimitado para esta cuenta/i)).toBeInTheDocument())
  })
  it('shows the same useful library details as the local studio', async () => {
    render(<Cloud />)
    await waitFor(() => expect(screen.getByRole('button', { name: 'Historial' })).toBeInTheDocument())
    fireEvent.click(screen.getByRole('button', { name: 'Historial' }))
    const history = screen.getByLabelText('Historial de composiciones')
    expect(history).toHaveTextContent('BIBLIOTECA CLOUD')
    expect(history).toHaveTextContent('Obra de prueba')
    expect(history).toHaveTextContent('LISTA')
    expect(history).toHaveTextContent('01:00 · 1 pistas')
    expect(screen.getByRole('button', { name: 'Cerrar historial' })).toHaveClass('cloud-suite-history-close')
    expect(screen.getByRole('button', { name: /Actualizar/ })).toHaveClass('cloud-suite-history-refresh')
    const callsBeforeRefresh = mock.cloudJobs.mock.calls.length
    fireEvent.click(screen.getByRole('button', { name: /Actualizar/ }))
    await waitFor(() => expect(mock.cloudJobs.mock.calls.length).toBeGreaterThan(callsBeforeRefresh))
  })

  it('reads the authenticated MIDI and exposes real transport, solo and download controls', async () => {
    render(<Cloud />)
    await waitFor(() => expect(screen.getByRole('button', { name: 'Reproducir' })).not.toBeDisabled())
    expect(screen.getByText('Protagonist lead')).toBeInTheDocument()
    expect(screen.getByLabelText('Modo de escucha')).toHaveValue('piano')
    expect(screen.getAllByText('Piano MIDI').length).toBeGreaterThan(1)
    expect(screen.getByRole('button', { name: 'WAV 30 s' })).not.toBeDisabled()
    expect(screen.getByRole('button', { name: /Exportar audio completo · WAV/ })).not.toBeDisabled()
    expect(screen.getByRole('button', { name: /Todas las notas · 1 pista MIDI/ })).not.toBeDisabled()
    expect(screen.queryByLabelText('Sonido de Protagonist lead')).not.toBeInTheDocument()
    expect(screen.getByRole('link', { name: '↓ Multipista' })).toHaveAttribute('href', `/api/cloud/jobs/${job.id}/tracks/full-song.mid`)
    expect(screen.getByRole('button', { name: '↓ 1 pista MIDI' })).not.toBeDisabled()
    expect(screen.getByRole('button', { name: 'Escuchar solo Protagonist lead' })).toHaveAttribute('aria-pressed', 'false')
    fireEvent.change(screen.getByLabelText('Modo de escucha'), { target: { value: 'neutral' } })
    await waitFor(() => expect(screen.getByText('Seno · melodía')).toBeInTheDocument())
    expect(screen.queryByLabelText('Sonido de Protagonist lead')).not.toBeInTheDocument()
  })

  it('sends only the musician-facing parameters with a new Cloud request', async () => {
    render(<Cloud />)
    fireEvent.change(screen.getByLabelText(/Idea musical/), { target: { value: 'Una obra nueva' } })
    await waitFor(() => expect(screen.getByRole('button', { name: /Componer obra/ })).not.toBeDisabled())
    expect(screen.queryByText(/Dirección avanzada/)).not.toBeInTheDocument()
    expect(screen.queryByLabelText(/Autoría del MIDI/)).not.toBeInTheDocument()
    expect(screen.queryByLabelText(/Semilla/)).not.toBeInTheDocument()
    fireEvent.click(screen.getByRole('button', { name: /Componer obra/ }))
    await waitFor(() => expect(mock.createCloudJob).toHaveBeenCalled())
    const request = mock.createCloudJob.mock.calls[0][0]
    expect(request).toMatchObject({ prompt: 'Una obra nueva', durationSeconds: 390, bpm: 120 })
    expect(request.allowChromatic).toBe(false)
    expect(request).toHaveProperty('idempotencyKey')
    expect(request).not.toHaveProperty('behavior')
    expect(request).not.toHaveProperty('aiSovereign')
    expect(request).not.toHaveProperty('seed')
  })
})
