import { useEffect, useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { api, BillingPlans } from '../api'
import { useAuth } from '../auth'

const features = ['Composiciones completas', 'Orquestación MIDI multicapa', 'Crear directamente en Live', 'Actualizaciones mientras la suscripción esté activa', 'Uso comercial de las composiciones']

export function Pricing() {
  const [annual, setAnnual] = useState(true)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  const [plans, setPlans] = useState<BillingPlans | null>(null)
  const { user } = useAuth()
  const navigate = useNavigate()
  useEffect(() => { api.plans().then(setPlans).catch(() => setError('No pudimos consultar los precios actuales.')) }, [])
  const selected = plans?.[annual ? 'annual' : 'monthly']
  const price = selected ? new Intl.NumberFormat('es', { style: 'currency', currency: selected.currency, maximumFractionDigits: selected.amount % 100 === 0 ? 0 : 2 }).format(selected.amount / 100) : '—'
  async function subscribe() {
    if (!user) { navigate('/register?next=/pricing'); return }
    setBusy(true); setError('')
    try { window.location.assign((await api.checkout(annual ? 'annual' : 'monthly')).url) }
    catch (problem) { setError(problem instanceof Error ? problem.message : 'No pudimos iniciar el pago.') }
    finally { setBusy(false) }
  }
  return <section className="pricing-page">
    <div className="page-hero compact"><p className="eyebrow">PLANES SIMPLES</p><h1>Una herramienta seria.<br/><em>Un precio claro.</em></h1><p>Prueba el flujo completo y administra tu suscripción directamente con Stripe.</p></div>
    <div className="billing-toggle" aria-label="Período de facturación"><button className={!annual ? 'active' : ''} onClick={()=>setAnnual(false)}>Mensual</button><button className={annual ? 'active' : ''} onClick={()=>setAnnual(true)}>Anual</button></div>
    <article className="price-card"><div><p className="eyebrow">PULSO COMPLETE</p><h2>{price}<small>/{annual ? 'año' : 'mes'}</small></h2><p>La suite completa de composición y despliegue en Live.</p></div><ul>{features.map(feature=><li key={feature}>✓ {feature}</li>)}</ul><button className="button" onClick={subscribe} disabled={busy || !plans}>{busy ? 'Conectando…' : 'Comenzar'}</button>{error && <p className="form-error" role="alert">{error}</p>}</article>
    <p className="pricing-note">El cobro, las facturas, los medios de pago y las cancelaciones son gestionados por Stripe. Los precios finales se configuran en tu catálogo de Stripe.</p>
  </section>
}
