import { useEffect, useState } from 'react'
import { locale, useLocale } from '../i18n'

type Wallet = { enabled: boolean; blocked: boolean; available: number; reserved: number; perWork: number;
  basicPlan: { monthlyCredits: number; amountCents: number; currency: string };
  packs: { id: string; credits: number; amount: number; currency: string }[];
  history: { kind: string; available: number; reserved: number; at: string }[] }
const words = {
  es: { title:'Créditos Cloud', balance:'Disponibles', held:'Reservados', buy:'Comprar', cost:'Esta obra reserva', units:'créditos', rule:'Solo se consumen al completar la obra. Si falla o se cancela, se devuelven. Escuchar y descargar no consume créditos.', history:'Movimientos', unavailable:'No hay paquetes a la venta todavía.', error:'No se pudo consultar el saldo.', waiting:'Esperando confirmación del pago. El saldo se actualiza al recibirla.', purchase:'Compra', reserve:'Reserva', consume:'Obra completada', release:'Devolución', adjustment:'Ajuste', subscription_grant:'Créditos mensuales' },
  en: { title:'Cloud credits', balance:'Available', held:'Reserved', buy:'Buy', cost:'This work reserves', units:'credits', rule:'Credits are consumed only on completion. Failed or cancelled works are refunded. Listening and downloading are free.', history:'Activity', unavailable:'No packs are available yet.', error:'Could not load your balance.', waiting:'Waiting for payment confirmation. Your balance updates when it arrives.', purchase:'Purchase', reserve:'Reservation', consume:'Completed work', release:'Released', adjustment:'Adjustment', subscription_grant:'Monthly credits' },
  pt: { title:'Créditos Cloud', balance:'Disponíveis', held:'Reservados', buy:'Comprar', cost:'Esta obra reserva', units:'créditos', rule:'Só são consumidos ao concluir a obra. Em caso de falha ou cancelamento, são devolvidos. Ouvir e baixar não consome créditos.', history:'Movimentações', unavailable:'Ainda não há pacotes à venda.', error:'Não foi possível consultar o saldo.', waiting:'Aguardando confirmação do pagamento. O saldo será atualizado.', purchase:'Compra', reserve:'Reserva', consume:'Obra concluída', release:'Devolução', adjustment:'Ajuste', subscription_grant:'Créditos mensais' },
}
export function CreditWallet({ duration }: { duration?: number }) {
  useLocale()
  const language=locale().startsWith('pt')?'pt':locale().startsWith('en')?'en':'es'
  const w=words[language]
  const [wallet,setWallet]=useState<Wallet|null>(null)
  const [error,setError]=useState('')
  const [busy,setBusy]=useState(false)
  const [keys]=useState<Record<string,string>>({})
  useEffect(()=>{
    let alive=true
    const refresh=async()=>{try {
      const r=await fetch('/api/billing/credits',{credentials:'same-origin',cache:'no-store'})
      if(!r.ok) throw new Error(w.error)
      const value=await r.json() as Wallet
      if(alive){setWallet(value);setError('')}
    }catch(e){if(alive)setError((e as Error).message)}}
    void refresh(); const timer=setInterval(()=>void refresh(),10000)
    return()=>{alive=false;clearInterval(timer)}
  },[w.error])
  async function buy(packId:string) {
    setBusy(true);setError('')
    keys[packId] ||= crypto.randomUUID()
    try {
      const r=await fetch('/api/billing/credits/checkout',{method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/json'},body:JSON.stringify({packId,requestKey:keys[packId]})})
      const body=await r.json()
      if(!r.ok)throw new Error(body.error||w.error)
      const url=new URL(body.url)
      if(url.protocol!=='https:'||url.hostname!=='checkout.stripe.com')throw new Error(w.error)
      location.assign(url.href)
    }catch(e){setError((e as Error).message);setBusy(false)}
  }
  if(wallet && !wallet.enabled) return null
  if(!wallet) return error ? <p role="alert">{error}</p> : null
  return <section className="install-steps" aria-label={w.title}>
    <h3>{w.title}</h3><p><strong>{wallet.available}</strong> {w.balance} · {wallet.reserved} {w.held}</p>
    {wallet.blocked && <p role="alert">{language==='en'?'Balance under payment review. Contact support.':language==='pt'?'Saldo em revisão de pagamento. Entre em contato com o suporte.':'Saldo en revisión de pago. Contacta con soporte.'}</p>}
    <p>{language==='en'?'Basic plan':language==='pt'?'Plano básico':'Plan básico'}: <strong>{wallet.basicPlan.monthlyCredits} {w.units} / {language==='en'?'month':language==='pt'?'mês':'mes'}</strong> · {new Intl.NumberFormat(locale(),{style:'currency',currency:wallet.basicPlan.currency}).format(wallet.basicPlan.amountCents/100)} / {language==='en'?'month':language==='pt'?'mês':'mes'}.</p>
    {duration!==undefined && <p>{w.cost} <strong>{wallet.perWork} {w.units}</strong>.</p>}
    <p>{w.rule}</p>
    {new URLSearchParams(location.search).get('credits')==='processing' && <p role="status">{w.waiting}</p>}
    {wallet.packs.map(pack=><button key={pack.id} type="button" className="button button-ghost" disabled={busy||wallet.blocked} onClick={()=>void buy(pack.id)}>{w.buy} {pack.credits} {w.units} · {new Intl.NumberFormat(locale(),{style:'currency',currency:pack.currency}).format(pack.amount/100)}</button>)}
    {!wallet.packs.length && <p>{language==='en'?'Additional credit packs are not available yet.':language==='pt'?'Pacotes adicionais ainda não estão disponíveis.':'Los paquetes adicionales todavía no están disponibles.'}</p>}
    <p>{language==='en'?'Unused credits carry forward; a Cloud entitlement is required to spend them.':language==='pt'?'Os créditos não usados acumulam; é preciso acesso Cloud para utilizá-los.':'Los créditos no utilizados se acumulan; necesitas acceso Cloud para usarlos.'}</p>
    {error && <p role="alert">{error}</p>}
    {duration===undefined && <details><summary>{w.history}</summary><ul>{wallet.history.map((item,i)=><li key={i}>{w[item.kind as keyof typeof w]||item.kind} · {item.available>0?'+':''}{item.available} · {new Date(item.at).toLocaleString(locale())}</li>)}</ul></details>}
  </section>
}
