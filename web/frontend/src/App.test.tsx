import { render, screen } from '@testing-library/react'
import { describe, expect, it, vi } from 'vitest'
import { App } from './App'

vi.stubGlobal('fetch', vi.fn(async () => new Response('{}', { status: 401, headers: { 'Content-Type': 'application/json' } })))

describe('PULSO website', () => {
  it('renders the product promise and purchase path', async () => {
    render(<App />)
    expect(screen.getByRole('heading', { name: /Dirige una idea.*Habita una obra/i })).toBeInTheDocument()
    expect(screen.getByRole('link', { name: /Obtener PULSO/i })).toHaveAttribute('href', '/pricing')
  })
})
