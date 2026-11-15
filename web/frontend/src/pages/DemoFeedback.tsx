import { useEffect, useState } from 'react'
import { Navigate } from 'react-router-dom'
import { useAuth } from '../auth'
import { api, type DemoFeedback as Feedback } from '../api'
import '../components/DemoAccess.css'
export function DemoFeedback(){
  const {user,loading}=useAuth();const [items,setItems]=useState<Feedback[]>([]),[total,setTotal]=useState(0),[offset,setOffset]=useState(0),[error,setError]=useState('')
  const owner=user?.email.toLowerCase()==='devingforart@gmail.com' && user.emailVerified
  useEffect(()=>{if(owner)void api.reviewDemoFeedback(offset).then(data=>{setItems(data.items);setTotal(data.total);setError('')}).catch(e=>setError(e.message))},[owner,offset])
  if(loading)return <p>Cargando…</p>
  if(!owner)return <Navigate to="/account" replace />
  return <section className="page-hero"><h1>Opiniones de la demo</h1><p>{total} respuestas privadas · página {Math.floor(offset/50)+1}</p>
    <button onClick={()=>{const url=URL.createObjectURL(new Blob([JSON.stringify(items,null,2)],{type:'application/json'}));const a=document.createElement('a');a.href=url;a.download=`pulso-demo-feedback-${offset}.json`;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000)}}>Descargar esta página (JSON)</button>
    {error && <p role="alert">{error}</p>}
    <div className="demo-feedback-list">{items.map(f=><article key={f.id}><h2>{f.displayName}</h2><small>{f.email} · {new Date(f.createdAt*1000).toLocaleString()} · {f.completedWorks} obras</small><p>General: {f.overall}/5 · Musicalidad: {f.musicality}/5 · Uso: {f.easeOfUse}/5</p><h3>Lo positivo</h3><p>{f.strengths}</p><h3>Por mejorar</h3><p>{f.improvements}</p></article>)}</div>
    <button disabled={!offset} onClick={()=>setOffset(n=>Math.max(0,n-50))}>Anterior</button><button disabled={offset+50>=total} onClick={()=>setOffset(n=>n+50)}>Siguiente</button>
  </section>
}
