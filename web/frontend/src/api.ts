export type User = {
  id: string
  email: string
  displayName: string
  subscription: 'none' | 'trialing' | 'active' | 'past_due' | 'canceled'
  emailVerified: boolean
  studioOwned: boolean
  studioUpdatesUntil: number | null
}

export type Release = {
  version: string
  name: string
  publishedAt: string
  sizeBytes: number | null
  available: boolean
}

export type BillingPlans = {
  studio: { amount: number; currency: string; interval: string }
  monthly: { amount: number; currency: string; interval: string } | null
  annual: { amount: number; currency: string; interval: string } | null
}

export type Device = { id: string; name: string; createdAt: number; lastSeenAt: number }

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
  forgotPassword: (email: string) => request<void>('/api/auth/forgot-password', { method: 'POST', body: JSON.stringify({ email }) }),
  resetPassword: (token: string, password: string) => request<void>('/api/auth/reset-password', { method: 'POST', body: JSON.stringify({ token, password }) }),
  verifyEmail: (token: string) => request<void>('/api/auth/verify-email', { method: 'POST', body: JSON.stringify({ token }) }),
  resendVerification: () => request<void>('/api/auth/resend-verification', { method: 'POST' }),
  approveDevice: (code: string) => request<{ approved: boolean }>('/api/licenses/device-approve', { method: 'POST', body: JSON.stringify({ code }) }),
  devices: () => request<{ devices: Device[]; limit: number }>('/api/licenses/devices'),
  revokeDevice: (id: string) => request<void>(`/api/licenses/devices/${id}`, { method: 'DELETE' }),
  release: () => request<Release>('/api/releases/latest'),
  plans: () => request<BillingPlans>('/api/billing/plans'),
  checkout: (plan: 'studio' | 'cloud_monthly' | 'cloud_annual', promotionCode?: string) =>
    request<{ url: string }>('/api/billing/checkout', {
      method: 'POST', body: JSON.stringify({ plan, promotionCode }),
    }),
  redeemPromotion: (code: string) =>
    request<{ granted: boolean; alreadyRedeemed: boolean; updatesUntil: number }>('/api/billing/redeem-promo', {
      method: 'POST', body: JSON.stringify({ code }),
    }),
  portal: () => request<{ url: string }>('/api/billing/portal', { method: 'POST' }),
}
