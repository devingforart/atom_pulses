import { cleanup, render, screen } from '@testing-library/react'
import { afterEach, expect, it } from 'vitest'
import type { CloudJob } from '../api'
import { CloudRequestDetails } from './CloudRequestDetails'

afterEach(cleanup)
const job: CloudJob = {
  id: 'example', prompt: 'Progressive house oscuro', durationSeconds: 180, bpm: 122,
  seed: '1176982797102497781', behavior: 'club', clubLayerPace: '8-16', clubVariety: false,
  allowChromatic: false, aiSovereign: true, status: 'running', stage: 'writing',
  completedSteps: 1, totalSteps: 3, resultManifest: null, errorCode: null, createdAt: 1, updatedAt: 1,
}
it('shows saved request values, not a fixed horizon or current form draft', () => {
  render(<CloudRequestDetails job={job} />)
  expect(screen.getByText('8–16 compases')).toBeInTheDocument()
  expect(screen.getByText(job.seed)).toBeInTheDocument()
  expect(screen.getByText('Progressive house oscuro')).toBeInTheDocument()
  expect(screen.queryByText('64 compases')).not.toBeInTheDocument()
})
it('keeps the disclosure open when progress updates', () => {
  const view = render(<CloudRequestDetails job={job} />)
  const details = view.container.querySelector('details')!
  details.open = true
  view.rerender(<CloudRequestDetails job={{ ...job, completedSteps: 2, updatedAt: 2 }} />)
  expect(view.container.querySelector('details')).toBe(details)
  expect(details.open).toBe(true)
})
it('does not invent options for legacy jobs or non-Club requests', () => {
  const view = render(<CloudRequestDetails job={{ ...job, clubLayerPace: undefined, clubVariety: undefined }} />)
  expect(screen.getAllByText('No registrado')).toHaveLength(2)
  view.rerender(<CloudRequestDetails job={{ ...job, behavior: 'adaptive' }} />)
  expect(screen.getAllByText('No aplica · Club desactivado')).toHaveLength(2)
})
