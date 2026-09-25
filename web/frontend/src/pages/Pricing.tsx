import { useEffect, useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { api, BillingPlans } from '../api'
import { useAuth } from '../auth'

const money = (amount: number, currency: string) => new Intl.NumberFormat('es', { style:'currency', currency, maximumFractionDigits:amount%100===0?0:2 }).format(amount/100)

export function Pricing() {
  const [plans,setPlans]=useState<BillingPlans|null>(null); const [busy,setBusy]=useState(''); const [error,setError]=useState(''); const { user }=useAuth(); const navigate=useNavigate()
  useEffect(()=>{ api.plans().then(setPlans).catch(()=>setError('No pudimos consultar los precios actuales.')) },[])
  async function buy(plan:'studio'|'cloud_monthly'|'cloud_annual') { if(!user){navigate('/register?next=/pricing');return} setBusy(plan);setError('');try{window.location.assign((await api.checkout(plan)).url)}catch(e){setError(e instanceof Error?e.message:'No pudimos iniciar el pago.')}finally{setBusy('')} }
  return <section className="pricing-page"><div className="page-hero compact"><p className="eyebrow">DOS FORMAS DE CREAR</p><h1>Tu herramienta.<br/><em>Tu elección.</em></h1><p>Compra PULSO Studio una vez y usa tu propia clave, o suma Cloud cuando quieras consumo administrado.</p></div>
    <div className="pricing-grid">
      <article className="price-card"><div><p className="eyebrow">PULSO STUDIO</p><h2>{plans?money(plans.studio.amount,plans.studio.currency):'—'}<small> pago único</small></h2><p>Licencia perpetua para dos equipos y 12 meses de actualizaciones.</p></div><ul><li>✓ Motor completo de composición</li><li>✓ VST3 + aplicación</li><li>✓ Crear en Ableton Live</li><li>✓ BYOK: costos de IA bajo tu control</li></ul><button className="button" disabled={!plans||!!busy||user?.studioOwned} onClick={()=>void buy('studio')}>{user?.studioOwned?'Ya es tuyo':busy==='studio'?'Conectando…':'Comprar Studio'}</button></article>
      <article className="price-card"><div><p className="eyebrow">PULSO CLOUD · PRÓXIMAMENTE</p><h2>{plans?.monthly?money(plans.monthly.amount,plans.monthly.currency):'En desarrollo'}{plans?.monthly&&<small>/mes</small>}</h2><p>Servicio opcional de créditos, modelos administrados y sincronización. Se habilitará cuando el gateway de IA esté auditado.</p></div><ul><li>✓ Todo el flujo de Studio</li><li>✓ Consumo de IA administrado</li><li>✓ Límites de costo visibles</li><li>✓ Facturación centralizada</li></ul><button className="button button-ghost" disabled>Próximamente</button></article>
    </div>{error&&<p className="form-error pricing-note">{error}</p>}<p className="pricing-note">Stripe procesa pagos, impuestos, facturas y medios de pago. PULSO no almacena datos de tarjeta.</p></section>
}
