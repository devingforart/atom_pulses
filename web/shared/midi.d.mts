export type MidiNote = {
  startBeat: number
  durationBeats: number
  pitch: number
  velocity: number
  channel: number
  unterminated?: boolean
}

export type MidiScore = {
  format: number
  division: number
  tracks: { name: string; notes: MidiNote[] }[]
  beatsPerBar: number
  lengthBeats: number
  beatToSeconds(beat: number): number
  secondsToBeat(seconds: number): number
  tempos: { tick: number; microseconds: number; seconds: number }[]
}

export function parseMidi(input: ArrayBuffer | Uint8Array): MidiScore
