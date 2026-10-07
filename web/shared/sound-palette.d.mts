import type { SuiteTrack } from './audio-engine.mjs'

export type Patch = { label: string; family: string; level: number }
export const SOUND_BANK_VERSION: string
export const PATCHES: Readonly<Record<string, Patch>>
export const PATCH_IDS: string[]
export function patchOptions(family: string): string[]
export function patchForTrack(track: Partial<SuiteTrack>): string
