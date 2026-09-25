import { useEffect, useState } from 'react'
import { Navigate } from 'react-router-dom'
import { api, Release } from '../api'
import { useAuth } from '../auth'

export function Account() {
  const { user, loading, logout } = useAuth()
  const [release, setRelease] = useState<Release | null>(null)
  const [error, setError] = useState('')
  const [portalBusy, setPortalBusy] = useState(false)
  useEffect(() => { api.release().then(setRelease).catch(()=>setRelease(null)) }, [])
  if (loading) return <section className="account-page"><p>Abriendo tu cuenta…</p></section>
  if (!user) return <Navigate to="/login" replace />
  const entitled = user.subscription === 'active' || user.subscription === 'trialing'
  async function portal() {
    setPortalBusy(true); setError('')
    try { window.location.assign((await api.portal()).url) }
    catch (problem) { setError(problem instanceof Error ? problem.message : 'No pudimos abrir el portal.') }
    finally { setPortalBusy(false) }
  }
  return <section className="account-page"><div className="account-head"><div><p className="eyebrow">MI PULSO</p><h1>Hola, {user.displayName}.</h1></div><button className="text-button" onClick={()=>void logout()}>Cerrar sesión</button></div>
    <div className="account-grid"><article className="download-card"><p className="eyebrow">ÚLTIMA VERSIÓN</p><h2>{release?.name ?? 'PULSO'}</h2><p>{release?.available ? `Versión ${release.version} lista para Windows.` : 'La próxima versión aparecerá aquí al publicarse en GitHub.'}</p>
      {entitled ? <a className={`button ${!release?.available ? 'disabled' : ''}`} aria-disabled={!release?.available} href={release?.available ? '/api/downloads/latest/windows' : undefined}>Descargar instalador</a> : <a className="button" href="/pricing">Activar suscripción</a>}
      {release?.sizeBytes && <small>{(release.sizeBytes / 1_048_576).toFixed(1)} MB · Windows x64</small>}</article>
      <article className="subscription-card"><p className="eyebrow">SUSCRIPCIÓN</p><span className={`status status-${user.subscription}`}>{user.subscription.replace('_',' ')}</span><p>{entitled ? 'Tu acceso a PULSO y sus actualizaciones está activo.' : 'Activa un plan para descargar PULSO.'}</p>{user.subscription !== 'none' && <button className="button button-ghost" disabled={portalBusy} onClick={portal}>{portalBusy ? 'Abriendo…' : 'Facturación y plan'}</button>}</article></div>
    {error && <p className="form-error" role="alert">{error}</p>}
    <section className="install-steps"><p className="eyebrow">INSTALACIÓN</p><ol><li>Cierra Ableton Live.</li><li>Descomprime el paquete y ejecuta <code>Install-PULSO.ps1</code>.</li><li>Abre Live, activa <code>PulsoDeployRemote</code> y vuelve a buscar plugins.</li></ol></section>
  </section>
}
