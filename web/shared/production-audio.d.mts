import type { MidiScore } from './midi.mjs'
import type { SuiteTrack } from './audio-engine.mjs'

export class ProductionAudio {
  constructor(onPosition?: (seconds: number) => void, onStop?: () => void)
  midi?: MidiScore
  tracks: SuiteTrack[]
  muted: Set<number>
  soloed: Set<number>
  position: number
  playing: boolean
  mode: 'production' | 'neutral'
  duration: number
  setSong(midi: MidiScore, tracks: SuiteTrack[]): void
  setVolume(value: number): void
  setMode(mode: 'production' | 'neutral'): void
  setPatch(index: number, patchId: string): boolean
  setMute(index: number, value: boolean): void
  setSolo(index: number, value: boolean): void
  currentPosition(): number
  seek(seconds: number): void
  play(): Promise<void>
  pause(): void
  stop(): void
  renderPreview(startSeconds?: number, maxSeconds?: number): Promise<Blob>
}
