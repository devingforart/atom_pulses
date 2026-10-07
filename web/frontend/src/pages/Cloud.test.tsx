import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { Cloud } from './Cloud'

const mock = vi.hoisted(() => ({
  cloudJobs: vi.fn(), cloudJob: vi.fn(), cloudStatus: vi.fn(), createCloudJob: vi.fn(), cancelCloudJob: vi.fn(),
}))
vi.mock('../api', () => ({ api: mock }))
vi.mock('../auth', () => ({ useAuth: () => ({ user: { id: 'user-1', email: 'test@example.com' }, loading: false }) }))

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
  mock.cloudJobs.mockReset().mockResolvedValue([job])
  mock.cloudJob.mockReset().mockResolvedValue(job)
  mock.cloudStatus.mockReset().mockResolvedValue({ available: true, dailyJobLimit: 3 })
  mock.createCloudJob.mockReset().mockResolvedValue({ ...job, id: 'new-job', status: 'queued', resultManifest: null })
  mock.cancelCloudJob.mockReset()
  vi.stubGlobal('fetch', vi.fn(async () => new Response(midi(), { status: 200, headers: { 'Content-Type': 'audio/midi' } })))
  localStorage.clear()
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue({ scale() {}, fillRect() {}, set fillStyle(_value: string) {}, set globalAlpha(_value: number) {} } as unknown as CanvasRenderingContext2D)
})
afterEach(() => { cleanup(); vi.restoreAllMocks() })

describe('Cloud Suite', () => {
  it('reads the authenticated MIDI and exposes real transport, solo and download controls', async () => {
    render(<Cloud />)
    await waitFor(() => expect(screen.getByRole('button', { name: 'Reproducir' })).not.toBeDisabled())
    expect(screen.getByText('Protagonist lead')).toBeInTheDocument()
    expect(screen.getByLabelText('Sonido de Protagonist lead')).toHaveValue('lead_round')
    expect(screen.getByLabelText('Modo de escucha')).toHaveValue('production')
    expect(screen.getByRole('button', { name: 'WAV 30 s' })).not.toBeDisabled()
    fireEvent.change(screen.getByLabelText('Sonido de Protagonist lead'), { target: { value: 'lead_analog' } })
    expect(screen.getByLabelText('Sonido de Protagonist lead')).toHaveValue('lead_analog')
    expect(localStorage.getItem(`pulso:audition:local-1:${job.id}:track-01.mid`)).toBe('lead_analog')
    expect(screen.getByRole('link', { name: '↓ Exportar obra MIDI' })).toHaveAttribute('href', `/api/cloud/jobs/${job.id}/tracks/full-song.mid`)
    expect(screen.getByRole('button', { name: 'Escuchar solo Protagonist lead' })).toHaveAttribute('aria-pressed', 'false')
    fireEvent.change(screen.getByLabelText('Modo de escucha'), { target: { value: 'neutral' } })
    expect(screen.getByText('Seno · melodía')).toBeInTheDocument()
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
    expect(request).toHaveProperty('idempotencyKey')
    expect(request).not.toHaveProperty('behavior')
    expect(request).not.toHaveProperty('aiSovereign')
    expect(request).not.toHaveProperty('seed')
  })
})
