import { useEffect, useRef, useState, type FormEvent } from 'react'
import { api, type DemoAccess as Access } from '../api'
import { useLocale } from '../i18n'
import './DemoAccess.css'
const words = {
  es: { demo:'Acceso demo', daily:'24 horas', week:'7 días', month:'30 días', left:'disponibles', failures:'Las obras fallidas no consumen cupo. Los límites son móviles y se aplican juntos.', next:'Próximo cupo', feedback:'Compartir opinión', title:'¿Cómo te está resultando PULSO?', intro:'Encuesta opcional. Tus respuestas se guardan de forma privada, vinculadas a tu cuenta, para mejorar PULSO.', overall:'Experiencia general', music:'Calidad musical', ease:'Facilidad de uso', good:'¿Qué te gustó?', bad:'¿Qué falta o funciona mal?', save:'Guardar opinión', later:'Más tarde', saving:'Guardando…', thanks:'Gracias. Tu opinión quedó guardada.', choose:'Elegir', low:'Muy mala', high:'Excelente' },
  en: { demo:'Demo access', daily:'24 hours', week:'7 days', month:'30 days', left:'available', failures:'Failed works do not use your allowance. Rolling limits apply together.', next:'Next availability', feedback:'Share feedback', title:'How is PULSO working for you?', intro:'Optional survey. Responses are stored privately, linked to your account, to improve PULSO.', overall:'Overall experience', music:'Musical quality', ease:'Ease of use', good:'What did you like?', bad:'What is missing or not working?', save:'Save feedback', later:'Later', saving:'Saving…', thanks:'Thank you. Your feedback was saved.', choose:'Choose', low:'Very poor', high:'Excellent' },
  'pt-BR': { demo:'Acesso demo', daily:'24 horas', week:'7 dias', month:'30 dias', left:'disponíveis', failures:'Obras com falha não consomem sua cota. Os limites móveis se aplicam juntos.', next:'Próxima disponibilidade', feedback:'Compartilhar opinião', title:'Como está sendo usar o PULSO?', intro:'Pesquisa opcional. As respostas ficam privadas, vinculadas à sua conta, para melhorar o PULSO.', overall:'Experiência geral', music:'Qualidade musical', ease:'Facilidade de uso', good:'Do que você gostou?', bad:'O que falta ou não funciona?', save:'Salvar opinião', later:'Mais tarde', saving:'Salvando…', thanks:'Obrigado. Sua opinião foi salva.', choose:'Escolher', low:'Muito ruim', high:'Excelente' }
}
export function DemoAccess({access,canPrompt,onRefresh}:{access:Access;canPrompt:boolean;onRefresh:()=>void}) {
  const lang=useLocale(), copy=words[lang]
  const [open,setOpen]=useState(false), [dismissed,setDismissed]=useState(false), [busy,setBusy]=useState(false)
  const [error,setError]=useState(''), [saved,setSaved]=useState(false)
  const [ratings,setRatings]=useState([0,0,0]), [strengths,setStrengths]=useState(''), [improvements,setImprovements]=useState('')
  const [requestKey,setRequestKey]=useState(()=>crypto.randomUUID())
  const dialog=useRef<HTMLDialogElement>(null)
  useEffect(()=>{if(access.surveyDue && canPrompt && !dismissed && !saved) setOpen(true)},[access.surveyDue,canPrompt,dismissed,saved])
  useEffect(()=>{if(open) dialog.current?.showModal(); else dialog.current?.close()},[open])
  async function postpone(){
    setDismissed(true);setOpen(false)
    try {await api.snoozeDemoFeedback();onRefresh()} catch(problem){setError(problem instanceof Error?problem.message:'Error')}
  }
  async function submit(event:FormEvent){
    event.preventDefault();setBusy(true);setError('')
    try {await api.demoFeedback({requestKey,overall:ratings[0],musicality:ratings[1],easeOfUse:ratings[2],strengths,improvements,locale:lang});setSaved(true);setDismissed(true);setOpen(false);onRefresh()}
    catch(problem){setError(problem instanceof Error?problem.message:'Error')}
    finally{setBusy(false)}
  }
  return <aside className="demo-access" aria-label={copy.demo}>
    <strong>{copy.demo}</strong><div className="demo-counters">{access.windows.map(w=><span key={w.days}><b>{w.remaining}/{w.limit}</b> {copy.left} · {w.days===1?copy.daily:w.days===7?copy.week:copy.month}</span>)}</div>
    <small>{copy.failures}</small>
    {access.nextAvailableAt && <small>{copy.next}: {new Date(access.nextAvailableAt*1000).toLocaleString(lang)}</small>}
    {access.completedWorks>0 && <button type="button" onClick={()=>{setSaved(false);setError('');setRequestKey(crypto.randomUUID());setOpen(true)}}>{copy.feedback}</button>}
    {saved && <p role="status">{copy.thanks}</p>}{!open && error && <p role="alert">{error}</p>}
    <dialog ref={dialog} className="demo-survey" aria-labelledby="demo-survey-title" onCancel={event=>{event.preventDefault();if(!busy)void postpone()}}>
      <form onSubmit={submit}>
        <h2 id="demo-survey-title">{copy.title}</h2><p>{copy.intro}</p>
        {[copy.overall,copy.music,copy.ease].map((label,i)=><label key={i}>{label}<select required value={ratings[i]} onChange={e=>setRatings(values=>values.map((n,j)=>i===j?Number(e.target.value):n))}><option value={0} disabled>{copy.choose}</option>{[1,2,3,4,5].map(n=><option key={n} value={n}>{n}{n===1?` · ${copy.low}`:n===5?` · ${copy.high}`:''}</option>)}</select></label>)}
        <label>{copy.good}<textarea required maxLength={2000} value={strengths} onChange={e=>setStrengths(e.target.value)} /></label>
        <label>{copy.bad}<textarea required maxLength={2000} value={improvements} onChange={e=>setImprovements(e.target.value)} /></label>
        {error && <p role="alert">{error}</p>}
        <footer><button type="button" disabled={busy} onClick={()=>void postpone()}>{copy.later}</button><button type="submit" disabled={busy || ratings.some(n=>!n) || !strengths.trim() || !improvements.trim()}>{busy?copy.saving:copy.save}</button></footer>
      </form>
    </dialog>
  </aside>
}
