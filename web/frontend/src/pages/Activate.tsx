import { t, useLocale, locale } from '../i18n'
import { FormEvent, useState } from 'react'
import { Navigate, useSearchParams } from 'react-router-dom'
import { api } from '../api'
import { useAuth } from '../auth'

export function Activate() {
  useLocale();
  const { user, loading } = useAuth(); const [search] = useSearchParams(); const [code,setCode]=useState(search.get('code') ?? ''); const [done,setDone]=useState(false); const [error,setError]=useState('')
  if (loading) return null
  if (!user) return <Navigate to={`/login?next=${encodeURIComponent(`/activate?code=${code}`)}`} replace />
  async function submit(event: FormEvent) { event.preventDefault(); setError(''); try { await api.approveDevice(code); setDone(true) } catch(e) { setError(e instanceof Error ? e.message : t("No pudimos activar el dispositivo.")) } }
  return <section className="auth-page"><form className="auth-card" onSubmit={submit}><p className="eyebrow">{t("ACTIVAR PULSO")}</p><h1>{done ? t("Dispositivo activado.") : t("Conecta este equipo.")}</h1>{!done && <><label>{t("Código mostrado por PULSO")}<input required pattern="[A-Za-z0-9-]{8,12}" value={code} onChange={e=>setCode(e.target.value.toUpperCase())} /></label>{error && <p className="form-error">{error}</p>}<button className="button">{t("Autorizar dispositivo")}</button></>}</form></section>
}

