import { t, useLocale, locale } from '../i18n'
import { FormEvent, useEffect, useState } from 'react'
import { Link, useSearchParams } from 'react-router-dom'
import { api } from '../api'

export function ForgotPassword() {
  useLocale();
  const [email, setEmail] = useState(''); const [done, setDone] = useState(false); const [error, setError] = useState('')
  async function submit(event: FormEvent) { event.preventDefault(); setError(''); try { await api.forgotPassword(email); setDone(true) } catch (e) { setError(e instanceof Error ? e.message : t("No pudimos continuar.")) } }
  return <section className="auth-page"><form className="auth-card" onSubmit={submit}><p className="eyebrow">{t("RECUPERAR ACCESO")}</p><h1>{t("Volver a PULSO.")}</h1>{done ? <p>{t("Si la cuenta existe, enviamos un enlace válido durante 30 minutos.")}</p> : <><label>{t("Email")}<input required type="email" autoComplete="email" value={email} onChange={e=>setEmail(e.target.value)} /></label>{error && <p className="form-error">{error}</p>}<button className="button">{t("Enviar enlace")}</button></>}<p><Link to="/login">{t("Volver al ingreso")}</Link></p></form></section>
}

export function ResetPassword() {
  useLocale();
  const [search] = useSearchParams(); const [password,setPassword]=useState(''); const [done,setDone]=useState(false); const [error,setError]=useState('')
  async function submit(event: FormEvent) { event.preventDefault(); try { await api.resetPassword(search.get('token') ?? '', password); setDone(true) } catch(e) { setError(e instanceof Error ? e.message : t("No pudimos continuar.")) } }
  return <section className="auth-page"><form className="auth-card" onSubmit={submit}><p className="eyebrow">{t("NUEVA CONTRASEÑA")}</p><h1>{t("Protege tu música.")}</h1>{done ? <p>{t("Contraseña actualizada.")}{' '}<Link to="/login">{t("Ingresa nuevamente.")}</Link></p> : <><label>{t("Contraseña")}<input required minLength={12} type="password" autoComplete="new-password" value={password} onChange={e=>setPassword(e.target.value)} /><small>{t("Mínimo 12 caracteres.")}</small></label>{error && <p className="form-error">{error}</p>}<button className="button">{t("Guardar contraseña")}</button></>}</form></section>
}

export function VerifyEmail() {
  useLocale();
  const [search] = useSearchParams(); const [status,setStatus]=useState(t("Verificando tu email…"))
  useEffect(()=>{ api.verifyEmail(search.get('token') ?? '').then(()=>setStatus(t("Email verificado. Ya puedes volver a tu cuenta."))).catch(e=>setStatus(e instanceof Error ? e.message : t("No pudimos verificar el enlace."))) },[search])
  return <section className="auth-page"><div className="auth-card"><p className="eyebrow">{t("IDENTIDAD")}</p><h1>{status}</h1><Link className="button" to="/account">{t("Ir a mi cuenta")}</Link></div></section>
}

