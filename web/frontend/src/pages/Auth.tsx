import { FormEvent, useState } from 'react'
import { Link, useNavigate, useSearchParams } from 'react-router-dom'
import { api } from '../api'
import { useAuth } from '../auth'

export function AuthPage({ mode }: { mode: 'login' | 'register' }) {
  const [email, setEmail] = useState('')
  const [password, setPassword] = useState('')
  const [displayName, setDisplayName] = useState('')
  const [error, setError] = useState('')
  const [busy, setBusy] = useState(false)
  const { refresh } = useAuth()
  const navigate = useNavigate()
  const [search] = useSearchParams()
  async function submit(event: FormEvent) {
    event.preventDefault(); setBusy(true); setError('')
    try {
      if (mode === 'register') await api.register(email, password, displayName)
      else await api.login(email, password)
      await refresh(); navigate(search.get('next') || '/account')
    } catch (problem) { setError(problem instanceof Error ? problem.message : 'No pudimos completar el acceso.') }
    finally { setBusy(false) }
  }
  return <section className="auth-page"><form onSubmit={submit} className="auth-card">
    <p className="eyebrow">{mode === 'login' ? 'BIENVENIDO DE NUEVO' : 'CREA TU CUENTA'}</p><h1>{mode === 'login' ? 'Volver a PULSO.' : 'Empieza a componer.'}</h1>
    {mode === 'register' && <label>Nombre<input required autoComplete="name" value={displayName} onChange={e=>setDisplayName(e.target.value)} /></label>}
    <label>Email<input required type="email" autoComplete="email" value={email} onChange={e=>setEmail(e.target.value)} /></label>
    <label>Contraseña<input required minLength={12} type="password" autoComplete={mode === 'login' ? 'current-password' : 'new-password'} value={password} onChange={e=>setPassword(e.target.value)} /><small>Mínimo 12 caracteres.</small></label>
    {error && <p className="form-error" role="alert">{error}</p>}
    <button className="button" disabled={busy}>{busy ? 'Procesando…' : mode === 'login' ? 'Ingresar' : 'Crear cuenta'}</button>
    <p>{mode === 'login' ? <>¿Todavía no tienes cuenta? <Link to="/register">Registrarte</Link><br /><Link to="/forgot-password">Olvidé mi contraseña</Link></> : <>¿Ya tienes cuenta? <Link to="/login">Ingresar</Link></>}</p>
  </form></section>
}
