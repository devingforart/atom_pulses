import { render, cleanup, screen } from '@testing-library/react'
import { afterEach, expect, it } from 'vitest'
import { PulsoWordmark } from './PulsoWordmark'

afterEach(cleanup)
it('combines the original waveform with five custom MIDI glyphs', () => {
  const { container } = render(<PulsoWordmark />)
  expect(screen.getByRole('img', { name: 'PULSO' })).toBeInTheDocument()
  expect(container.querySelector('.pulso-brand-wave path')).toHaveAttribute('d', 'M5.5 12h4l2.4-7 4.2 14 4.1-11 3.2 7h4l2.2-3H33')
  expect(container.querySelectorAll('.pulso-wordmark rect')).toHaveLength(33)
  expect(container.querySelectorAll('.pulso-midi-accent')).toHaveLength(1)
})
