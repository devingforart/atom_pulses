import { cleanup, render, screen, waitFor } from '@testing-library/react'
import { MemoryRouter } from 'react-router-dom'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { Pricing } from './Pricing'

const mock = vi.hoisted(() => ({ plans: vi.fn(), user: { subscription: 'none', studioOwned: false } }))
vi.mock('../api', () => ({ api: mock }))
vi.mock('../auth', () => ({ useAuth: () => ({ user: mock.user, refresh: vi.fn() }) }))
vi.mock('../i18n', () => ({ t: (value: string) => value, useLocale: () => {}, locale: () => 'es-AR' }))
beforeEach(() => { mock.user.subscription = 'none' })
afterEach(cleanup)
const plans = { monthly: { amount: 1400, currency: 'USD', interval: 'month' },
  creditPlan: { amount: 1400, currency: 'USD' } }
it('offers a clearly identified test subscription only when its price exists', async () => {
  mock.plans.mockResolvedValue({ ...plans, testMode: true })
  render(<MemoryRouter><Pricing /></MemoryRouter>)
  expect(await screen.findByRole('button', { name: 'Probar suscripción' })).not.toBeDisabled()
  expect(screen.getByRole('status')).toHaveTextContent('no se realizan cargos reales')
})
it('keeps unavailable subscriptions disabled', async () => {
  mock.plans.mockResolvedValue({ ...plans, monthly: null })
  render(<MemoryRouter><Pricing /></MemoryRouter>)
  expect(await screen.findByRole('button', { name: 'Próximamente' })).toBeDisabled()
})
it('prevents buying another active subscription', async () => {
  mock.user.subscription = 'active'
  mock.plans.mockResolvedValue({ ...plans, testMode: false })
  render(<MemoryRouter><Pricing /></MemoryRouter>)
  await waitFor(() => expect(screen.getByRole('button', { name: 'Suscribirme' })).toBeDisabled())
})
