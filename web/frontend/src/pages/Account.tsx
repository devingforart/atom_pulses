import { useEffect, useState } from 'react'
import { Navigate } from 'react-router-dom'
import { api, Device, Release } from '../api'
import { useAuth } from '../auth'

export function Account() {
  const { user, loading, logout } = useAuth()
  const [release, setRelease] = useState<Release | null>(null)
  const [error, setError] = useState('')
  const [portalBusy, setPortalBusy] = useState(false)
  const [devices, setDevices] = useState<Device[]>([])
  useEffect(() => { api.release().then(setRelease).catch(()=>setRelease(null)) }, [])
  useEffect(() => { if (user) api.devices().then(result=>setDevices(result.devices)).catch(()=>setDevices([])) }, [user])
  if (loading) return <section className="account-page"><p>Abriendo tu cuenta…</p></section>
  if (!user) return <Navigate to="/login" replace />
  const entitled = user.studioOwned || user.subscription === 'active' || user.subscription === 'trialing'
  async function portal() {
    setPortalBusy(true); setError('')
    try { window.location.assign((await api.portal()).url) }
    catch (problem) { setError(problem instanceof Error ? problem.message : 'No pudimos abrir el portal.') }
    finally { setPortalBusy(false) }
  }
  return <section className="account-page"><div className="account-head"><div><p className="eyebrow">MI PULSO</p><h1>Hola, {user.displayName}.</h1></div><button className="text-button" onClick={()=>void logout()}>Cerrar sesión</button></div>
    <div className="account-grid"><article className="download-card"><p className="eyebrow">ÚLTIMA VERSIÓN</p><h2>{release?.name ?? 'PULSO'}</h2><p>{release?.available ? `Versión ${release.version} lista para Windows.` : 'La próxima versión aparecerá aquí al publicarse en GitHub.'}</p>
      {entitled ? <a className={`button ${!release?.available ? 'disabled' : ''}`} aria-disabled={!release?.available} href={release?.available ? '/api/downloads/latest/windows' : undefined}>Descargar instalador</a> : <a className="button" href="/pricing">Obtener PULSO</a>}
      {release?.sizeBytes && <small>{(release.sizeBytes / 1_048_576).toFixed(1)} MB · Windows x64</small>}</article>
      <article className="subscription-card"><p className="eyebrow">LICENCIA</p><span className={`status status-${user.subscription}`}>{user.studioOwned ? 'studio' : user.subscription.replace('_',' ')}</span><p>{user.studioOwned ? `Tu licencia perpetua está activa${user.studioUpdatesUntil ? ` · actualizaciones hasta ${new Date(user.studioUpdatesUntil*1000).toLocaleDateString()}` : ''}.` : entitled ? 'Tu acceso a PULSO Cloud está activo.' : 'Obtén Studio o Cloud para descargar PULSO.'}</p>{user.subscription !== 'none' && <button className="button button-ghost" disabled={portalBusy} onClick={portal}>{portalBusy ? 'Abriendo…' : 'Facturación y plan'}</button>}</article></div>
    {error && <p className="form-error" role="alert">{error}</p>}
    {!user.emailVerified && <section className="install-steps"><p className="eyebrow">EMAIL PENDIENTE</p><p>Verifica tu email para proteger activaciones y recuperaciones.</p><button className="button button-ghost" onClick={()=>void api.resendVerification()}>Reenviar verificación</button></section>}
    <section className="install-steps"><p className="eyebrow">DISPOSITIVOS ACTIVADOS · {devices.length}/2</p>{devices.length === 0 ? <p>Aún no activaste PULSO en un equipo.</p> : <ul className="device-list">{devices.map(device=><li key={device.id}><span><strong>{device.name}</strong><small>Último uso: {new Date(device.lastSeenAt*1000).toLocaleDateString()}</small></span><button className="text-button" onClick={async()=>{ await api.revokeDevice(device.id); setDevices(items=>items.filter(item=>item.id!==device.id)) }}>Revocar</button></li>)}</ul>}</section>
    <section className="install-steps"><p className="eyebrow">INSTALACIÓN</p><ol><li>Cierra Ableton Live.</li><li>Ejecuta el instalador <code>PULSO-windows-x64-setup.exe</code> y sigue el asistente.</li><li>Abre Live, activa <code>PulsoDeployRemote</code> y vuelve a buscar plugins.</li></ol></section>
  </section>
}
