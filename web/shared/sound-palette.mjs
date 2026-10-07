// Versioned, deterministic audition palette. These patches are made by the
// PULSO Web Audio engine; no third-party samples or preset files are required.
export const SOUND_BANK_VERSION = 'local-1';

export const PATCHES = Object.freeze({
  sub_round: { label: 'Sub redondo', family: 'bass', wave: 'sine', second: 'triangle', detune: 0, attack: .012, decay: .16, sustain: .82, release: .12, cutoff: 150, motion: 0, level: .90, pan: 0, send: .01 },
  bass_analog: { label: 'Bajo analógico', family: 'bass', wave: 'sawtooth', second: 'triangle', detune: 3, attack: .006, decay: .21, sustain: .51, release: .13, cutoff: 620, motion: .19, level: .53, pan: 0, send: .025 },
  bass_reese: { label: 'Reese oscuro', family: 'bass', wave: 'sawtooth', second: 'sawtooth', detune: 12, attack: .025, decay: .29, sustain: .64, release: .24, cutoff: 780, motion: .24, level: .39, pan: 0, send: .04 },
  bass_acid: { label: 'Línea ácida', family: 'bass', wave: 'sawtooth', second: 'square', detune: 2, attack: .004, decay: .15, sustain: .33, release: .075, cutoff: 1500, motion: .56, level: .29, pan: 0, send: .045 },
  pad_velvet: { label: 'Pad aterciopelado', family: 'pad', wave: 'sawtooth', second: 'triangle', detune: 6, attack: .27, decay: .35, sustain: .65, release: 1.30, cutoff: 1150, motion: .11, level: .21, pan: -.14, send: .19 },
  pad_glass: { label: 'Pad de cristal', family: 'pad', wave: 'triangle', second: 'sine', detune: 8, attack: .40, decay: .45, sustain: .55, release: 1.70, cutoff: 3800, motion: .07, level: .20, pan: .14, send: .25 },
  pad_air: { label: 'Atmósfera aérea', family: 'pad', wave: 'triangle', second: 'sine', detune: 4, attack: .70, decay: .35, sustain: .69, release: 2.0, cutoff: 5400, motion: .09, level: .14, pan: .22, send: .33 },
  pad_drone: { label: 'Drone profundo', family: 'pad', wave: 'sawtooth', second: 'sine', detune: 3, attack: .60, decay: .50, sustain: .76, release: 2.3, cutoff: 510, motion: .10, level: .19, pan: -.18, send: .22 },
  chord_dub: { label: 'Acorde dub', family: 'keys', wave: 'sawtooth', second: 'triangle', detune: 4, attack: .005, decay: .30, sustain: .25, release: .35, cutoff: 980, motion: .28, level: .28, pan: -.10, send: .17 },
  chord_poly: { label: 'Polisinte cálido', family: 'keys', wave: 'sawtooth', second: 'triangle', detune: 5, attack: .040, decay: .36, sustain: .53, release: .54, cutoff: 1900, motion: .14, level: .23, pan: .08, send: .13 },
  keys_soft: { label: 'Teclas suaves', family: 'keys', wave: 'triangle', second: 'sine', detune: 0, attack: .003, decay: .65, sustain: .29, release: .32, cutoff: 4500, motion: 0, level: .33, pan: -.04, send: .12 },
  arp_pulse: { label: 'Pulso de arpegio', family: 'pluck', wave: 'square', second: 'sine', detune: 2, attack: .003, decay: .13, sustain: .24, release: .10, cutoff: 2200, motion: .31, level: .21, pan: .13, send: .12 },
  arp_glass: { label: 'Arpegio cristalino', family: 'pluck', wave: 'triangle', second: 'sine', detune: 0, attack: .002, decay: .30, sustain: .13, release: .20, cutoff: 6200, motion: .09, level: .28, pan: .17, send: .19 },
  pluck_deep: { label: 'Pluck profundo', family: 'pluck', wave: 'sawtooth', second: 'sine', detune: 0, attack: .002, decay: .22, sustain: .18, release: .18, cutoff: 1150, motion: .37, level: .26, pan: -.14, send: .13 },
  lead_round: { label: 'Lead redondo', family: 'lead', wave: 'triangle', second: 'sawtooth', detune: 4, attack: .025, decay: .18, sustain: .71, release: .17, cutoff: 2100, motion: .12, level: .34, pan: 0, send: .11 },
  lead_analog: { label: 'Lead analógico', family: 'lead', wave: 'sawtooth', second: 'square', detune: 5, attack: .013, decay: .23, sustain: .56, release: .19, cutoff: 2450, motion: .24, level: .27, pan: 0, send: .14 },
  lead_flute: { label: 'Lead de aire', family: 'lead', wave: 'sine', second: 'triangle', detune: 0, attack: .065, decay: .18, sustain: .76, release: .26, cutoff: 4400, motion: .06, level: .38, pan: .07, send: .15 },
  texture_noise: { label: 'Textura suave', family: 'synth', wave: 'triangle', second: 'sawtooth', detune: 15, attack: .32, decay: .41, sustain: .54, release: 1.8, cutoff: 1800, motion: .26, level: .14, pan: .26, send: .29 },
  fx_riser: { label: 'Transición aérea', family: 'synth', wave: 'sawtooth', second: 'triangle', detune: 10, attack: .32, decay: .30, sustain: .49, release: 1.2, cutoff: 3200, motion: .65, level: .13, pan: -.24, send: .31 },
  drums_electronic: { label: 'Kit electrónico', family: 'drums', level: .62, pan: 0, send: .07 },
  drums_808: { label: 'Kit 808 profundo', family: 'drums', level: .68, pan: 0, send: .05 },
  drums_909: { label: 'Kit 909 club', family: 'drums', level: .62, pan: 0, send: .07 },
  drums_soft: { label: 'Kit suave', family: 'drums', level: .46, pan: 0, send: .10 },
});

export const PATCH_IDS = Object.keys(PATCHES);
export const patchOptions = family => PATCH_IDS.filter(id => PATCHES[id].family === family);
export const patchForTrack = track => {
  const meta = track.meta || track.instrumentMeta || {};
  const identity = `${meta.catalog_id || ''} ${meta.source_voice || ''}`.toLowerCase();
  const description = `${meta.live_preset_intent || ''} ${meta.role || ''} ${track.name || ''}`.toLowerCase();
  if (track.instrument === 'drums' || meta.department === 'rhythm' || /kick|snare|clap|hat|drum|percussion|shaker|cymbal/.test(identity)) return 'drums_electronic';
  if (/sub_synth|sub_bass/.test(identity)) return 'sub_round';
  if (/reese/.test(identity)) return 'bass_reese';
  if (/acid_line/.test(identity)) return 'bass_acid';
  if (/rolling_mid_bass|movement_bass|electric_bass/.test(identity)) return 'bass_analog';
  if (/noise_riser|shimmer_tail|transitions/.test(identity)) return 'fx_riser';
  if (/spectral_drone/.test(identity)) return 'pad_drone';
  if (/granular_pad|ambient_texture|atmosphere/.test(identity)) return 'pad_air';
  if (/analog_pad|string_ensemble|chamber_strings|choir/.test(identity)) return /bright|glass|shimmer|air/.test(description) ? 'pad_glass' : 'pad_velvet';
  if (/dub_chord|filtered_stab/.test(identity)) return 'chord_dub';
  if (/poly_synth|harmonic_foundation|harmonic_pulse/.test(identity)) return 'chord_poly';
  if (/piano|keys|celesta|vibraphone|mallets/.test(identity)) return 'keys_soft';
  if (/fm_sequence|hypnotic_arp/.test(identity)) return /fm|glass|bright/.test(identity + description) ? 'arp_glass' : 'arp_pulse';
  if (/deep_pluck|pluck|harp|marimba/.test(identity)) return 'pluck_deep';
  if (/flute|woodwind|oboe|clarinet/.test(identity)) return 'lead_flute';
  if (/lead_synth|lead|protagonist|countermelody/.test(identity)) return /dark|analog|saw/.test(description) ? 'lead_analog' : 'lead_round';
  if (/bass|subgrave/.test(description)) return 'bass_analog';
  if (/arp|arpeg|sequence/.test(description)) return 'arp_pulse';
  if (/pad|colch[oó]n|atmos/.test(description)) return 'pad_velvet';
  if (/lead|melod/.test(description)) return 'lead_round';
  return 'texture_noise';
};
