import { FormEvent, useEffect, useState } from 'react'
import { Link, useSearchParams } from 'react-router-dom'
import { api } from '../api'

export function ForgotPassword() {
  const [email, setEmail] = useState(''); const [done, setDone] = useState(false); const [error, setError] = useState('')
  async function submit(event: FormEvent) { event.preventDefault(); setError(''); try { await api.forgotPassword(email); setDone(true) } catch (e) { setError(e instanceof Error ? e.message : 'No pudimos continuar.') } }
  return <section className="auth-page"><form className="auth-card" onSubmit={submit}><p className="eyebrow">RECUPERAR ACCESO</p><h1>Volver a PULSO.</h1>{done ? <p>Si la cuenta existe, enviamos un enlace válido durante 30 minutos.</p> : <><label>Email<input required type="email" autoComplete="email" value={email} onChange={e=>setEmail(e.target.value)} /></label>{error && <p className="form-error">{error}</p>}<button className="button">Enviar enlace</button></>}<p><Link to="/login">Volver al ingreso</Link></p></form></section>
}

export function ResetPassword() {
  const [search] = useSearchParams(); const [password,setPassword]=useState(''); const [done,setDone]=useState(false); const [error,setError]=useState('')
  async function submit(event: FormEvent) { event.preventDefault(); try { await api.resetPassword(search.get('token') ?? '', password); setDone(true) } catch(e) { setError(e instanceof Error ? e.message : 'No pudimos continuar.') } }
  return <section className="auth-page"><form className="auth-card" onSubmit={submit}><p className="eyebrow">NUEVA CONTRASEÑA</p><h1>Protege tu música.</h1>{done ? <p>Contraseña actualizada. <Link to="/login">Ingresa nuevamente.</Link></p> : <><label>Contraseña<input required minLength={12} type="password" autoComplete="new-password" value={password} onChange={e=>setPassword(e.target.value)} /><small>Mínimo 12 caracteres.</small></label>{error && <p className="form-error">{error}</p>}<button className="button">Guardar contraseña</button></>}</form></section>
}

export function VerifyEmail() {
  const [search] = useSearchParams(); const [status,setStatus]=useState('Verificando tu email…')
  useEffect(()=>{ api.verifyEmail(search.get('token') ?? '').then(()=>setStatus('Email verificado. Ya puedes volver a tu cuenta.')).catch(e=>setStatus(e instanceof Error ? e.message : 'No pudimos verificar el enlace.')) },[search])
  return <section className="auth-page"><div className="auth-card"><p className="eyebrow">IDENTIDAD</p><h1>{status}</h1><Link className="button" to="/account">Ir a mi cuenta</Link></div></section>
}

