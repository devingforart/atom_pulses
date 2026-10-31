import { t, useLocale, locale } from '../i18n'
import { useEffect, useState } from 'react'
import { Link, Navigate } from 'react-router-dom'
import { api, Device, Release } from '../api'
import { useAuth } from '../auth'

export function Account() {
  useLocale();
  const { user, loading, logout } = useAuth()
  const [release, setRelease] = useState<Release | null>(null)
  const [error, setError] = useState('')
  const [portalBusy, setPortalBusy] = useState(false)
  const [devices, setDevices] = useState<Device[]>([])
  useEffect(() => { api.release().then(setRelease).catch(()=>setRelease(null)) }, [])
  useEffect(() => { if (user) api.devices().then(result=>setDevices(result.devices)).catch(()=>setDevices([])) }, [user])
  if (loading) return <section className="account-page"><p>{t("Abriendo tu cuenta…")}</p></section>
  if (!user) return <Navigate to="/login" replace />
  const entitled = user.studioOwned || user.subscription === 'active' || user.subscription === 'trialing'
  async function portal() {
    setPortalBusy(true); setError('')
    try { window.location.assign((await api.portal()).url) }
    catch (problem) { setError(problem instanceof Error ? problem.message : t("No pudimos abrir el portal.")) }
    finally { setPortalBusy(false) }
  }
  return <section className="account-page"><div className="account-head"><div><p className="eyebrow">{t("MI PULSO")}</p><h1>{t("Hola,")}{' '}{user.displayName}.</h1><p className="account-email">{user.email}</p></div><button className="text-button account-signout" onClick={()=>void logout()}>{t("Cerrar sesión")}</button></div>
    <article className="account-studio-entry"><div><p className="eyebrow">PULSO CLOUD</p><h2>{t("Tu espacio para crear.")}</h2><p>{t("Volvé a tus composiciones o dale forma a una nueva idea.")}</p><Link className="button" to="/cloud">{t("Abrir estudio")}</Link></div><div className="account-score-art" aria-hidden="true">{Array.from({length:12},(_,i)=><i key={i} style={{left:`${6+(i%4)*23}%`,top:`${18+Math.floor(i/4)*24}%`,width:`${[17,12,20,14][i%4]}%`}}/>)}</div></article>
    <div className="account-grid"><article className="download-card"><p className="eyebrow">{t("ÚLTIMA VERSIÓN")}</p><h2>{release?.name ?? t("PULSO")}</h2><p>{release?.available ? t("Versión {p0} lista para Windows.", {p0: release.version}) : t("La próxima versión aparecerá aquí al publicarse en GitHub.")}</p>
      {entitled ? <a className={`button ${!release?.available ? 'disabled' : ''}`} aria-disabled={!release?.available} href={release?.available ? '/api/downloads/latest/windows' : undefined}>{t("Descargar instalador")}</a> : <a className="button" href="/pricing">{t("Obtener PULSO")}</a>}
      {release?.sizeBytes && <small>{(release.sizeBytes / 1_048_576).toFixed(1)}{' '}{t("MB · Windows x64")}</small>}</article>
      <article className="subscription-card"><p className="eyebrow">{t("LICENCIA")}</p><span className={`status status-${user.subscription}`}>{user.studioOwned ? 'studio' : t(user.subscription)}</span><p>{user.studioOwned ? t("Tu licencia perpetua está activa{p0}.", {p0: user.studioUpdatesUntil ? t(" · actualizaciones hasta {p0}", {p0: new Date(user.studioUpdatesUntil*1000).toLocaleDateString(locale())}) : ''}) : entitled ? t("Tu acceso a PULSO Cloud está activo.") : t("Obtén Studio o Cloud para descargar PULSO.")}</p>{(user.studioOwned || user.subscription !== 'none') && <button className="button button-ghost" disabled={portalBusy} onClick={portal}>{portalBusy ? t("Abriendo…") : t("Facturación y comprobantes")}</button>}</article></div>
    {error && <p className="form-error" role="alert">{error}</p>}
    {!user.emailVerified && <section className="install-steps"><p className="eyebrow">{t("EMAIL PENDIENTE")}</p><p>{t("Verifica tu email para proteger activaciones y recuperaciones.")}</p><button className="button button-ghost" onClick={()=>void api.resendVerification()}>{t("Reenviar verificación")}</button></section>}
    <div className="account-lower-grid"><section className="install-steps"><p className="eyebrow">{t("DISPOSITIVOS ACTIVADOS ·")}{' '}{devices.length}/2</p>{devices.length === 0 ? <p>{t("Aún no activaste PULSO en un equipo.")}</p> : <ul className="device-list">{devices.map(device=><li key={device.id}><span><strong>{device.name}</strong><small>{t("Último uso:")}{' '}{new Date(device.lastSeenAt*1000).toLocaleDateString(locale())}</small></span><button className="text-button" onClick={async()=>{ await api.revokeDevice(device.id); setDevices(items=>items.filter(item=>item.id!==device.id)) }}>{t("Revocar")}</button></li>)}</ul>}</section>
    <section className="install-steps"><p className="eyebrow">{t("INSTALACIÓN")}</p><ol><li>{t("Cierra Ableton Live.")}</li><li>{t("Ejecuta el instalador")}{' '}<code>{t("PULSO-windows-x64-setup.exe")}</code>{' '}{t("y sigue el asistente.")}</li><li>{t("Abre Live, activa")}{' '}<code>{t("PulsoDeployRemote")}</code>{' '}{t("y vuelve a buscar plugins.")}</li></ol></section>
    </div>
  </section>
}
