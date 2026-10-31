# Club preferences shared with the local studio

Cloud accepts and persists `clubLayerPace` (`auto`, `8-16`, `16-32`, `32-64`) and
`clubVariety` (boolean). Existing jobs retain automatic pace and their previous
resource behavior. The composition dialog defaults free resource choice to on,
matching the local studio, and saves both settings in the user's draft.

The worker receives the same job-specific environment variables used locally:
`PULSO_LOCAL_CLUB_PACE` and `PULSO_LOCAL_CLUB_VARIETY`. They are reapplied on
checkpoint recovery and neutralized outside Club. The AI planning and ensemble
writing briefs receive the same instructions; these controls do not schedule,
copy, stretch, quantize, or insert MIDI notes.

Layer pace is a musical preference, not a mandatory interval. Free resource
choice removes the requirement for a continuous arpeggio and asks the AI to
choose the work's central tonal resource. Turning it off retains the existing
Club editorial direction. Neither option enables drums or percussion.

Deployment requires the database migration, API, frontend, and rebuilt composer
worker together. A static-only deployment cannot activate these options.
