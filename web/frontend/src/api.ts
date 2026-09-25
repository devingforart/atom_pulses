export type User = {
  id: string
  email: string
  displayName: string
  subscription: 'none' | 'trialing' | 'active' | 'past_due' | 'canceled'
}

export type Release = {
  version: string
  name: string
  publishedAt: string
  sizeBytes: number | null
  available: boolean
}

export type BillingPlans = {
  monthly: { amount: number; currency: string; interval: string }
  annual: { amount: number; currency: string; interval: string }
}

type ApiErrorBody = { error?: string }

export class ApiError extends Error {
  constructor(message: string, readonly status: number) {
    super(message)
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(path, {
    ...init,
    credentials: 'include',
    headers: { 'Content-Type': 'application/json', ...init?.headers },
  })
  if (!response.ok) {
    const body = (await response.json().catch(() => ({}))) as ApiErrorBody
    throw new ApiError(body.error ?? 'No pudimos completar la solicitud.', response.status)
  }
  if (response.status === 204) return undefined as T
  return response.json() as Promise<T>
}

export const api = {
  me: () => request<{ user: User }>('/api/auth/me'),
  register: (email: string, password: string, displayName: string) =>
    request<{ user: User }>('/api/auth/register', {
      method: 'POST', body: JSON.stringify({ email, password, displayName }),
    }),
  login: (email: string, password: string) =>
    request<{ user: User }>('/api/auth/login', {
      method: 'POST', body: JSON.stringify({ email, password }),
    }),
  logout: () => request<void>('/api/auth/logout', { method: 'POST' }),
  release: () => request<Release>('/api/releases/latest'),
  plans: () => request<BillingPlans>('/api/billing/plans'),
  checkout: (interval: 'monthly' | 'annual') =>
    request<{ url: string }>('/api/billing/checkout', {
      method: 'POST', body: JSON.stringify({ interval }),
    }),
  portal: () => request<{ url: string }>('/api/billing/portal', { method: 'POST' }),
}
