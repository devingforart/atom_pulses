import { t, useLocale, locale } from '../i18n'
import { FormEvent, useState } from 'react'
import { Link, useNavigate, useSearchParams } from 'react-router-dom'
import { api } from '../api'
import { useAuth } from '../auth'

export function AuthPage({ mode }: { mode: 'login' | 'register' }) {
  useLocale();
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
    } catch (problem) { setError(problem instanceof Error ? problem.message : t("No pudimos completar el acceso.")) }
    finally { setBusy(false) }
  }
  return <section className="auth-page"><form onSubmit={submit} className="auth-card">
    <p className="eyebrow">{mode === 'login' ? t("BIENVENIDO DE NUEVO") : t("CREA TU CUENTA")}</p><h1>{mode === 'login' ? t("Volver a PULSO.") : t("Empieza a componer.")}</h1>
    {mode === 'register' && <label>{t("Nombre")}<input required autoComplete="name" value={displayName} onChange={e=>setDisplayName(e.target.value)} /></label>}
    <label>{t("Email")}<input required type="email" autoComplete="email" value={email} onChange={e=>setEmail(e.target.value)} /></label>
    <label>{t("Contraseña")}<input required minLength={12} type="password" autoComplete={mode === 'login' ? 'current-password' : 'new-password'} value={password} onChange={e=>setPassword(e.target.value)} /><small>{t("Mínimo 12 caracteres.")}</small></label>
    {error && <p className="form-error" role="alert">{error}</p>}
    <button className="button" disabled={busy}>{busy ? t("Procesando…") : mode === 'login' ? t("Ingresar") : t("Crear cuenta")}</button>
    <p>{mode === 'login' ? <>{t("¿Todavía no tienes cuenta?")}{' '}<Link to="/register">{t("Registrarte")}</Link><br /><Link to="/forgot-password">{t("Olvidé mi contraseña")}</Link></> : <>{t("¿Ya tienes cuenta?")}{' '}<Link to="/login">{t("Ingresar")}</Link></>}</p>
  </form></section>
}
