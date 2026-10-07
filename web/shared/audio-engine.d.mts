import type { MidiNote, MidiScore } from './midi.mjs'

export type SuiteTrack = {
  name: string
  filename: string
  notes: MidiNote[]
  instrument: string
  patchId?: string
  meta?: {
    catalog_id?: string
    department?: string
    source_voice?: string
    role?: string
    orchestral_function?: string
    articulation?: string
    live_preset_intent?: string
  }
}

export function instrumentFor(part?: {
  catalog_id?: string
  source_voice?: string
  role?: string
  department?: string
}, name?: string): string

export class SuiteAudio {
  constructor(onPosition?: (seconds: number) => void, onStop?: () => void)
  midi?: MidiScore
  tracks: SuiteTrack[]
  muted: Set<number>
  soloed: Set<number>
  position: number
  playing: boolean
  duration: number
  currentPosition(): number
  setSong(midi: MidiScore, tracks: SuiteTrack[]): void
  setVolume(value: number): void
  setMute(index: number, value: boolean): void
  setSolo(index: number, value: boolean): void
  seek(seconds: number): void
  play(): Promise<void>
  pause(): void
  stop(): void
}
