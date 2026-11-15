import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { DemoAccess } from './DemoAccess'
import { api, type DemoAccess as Access } from '../api'
vi.mock('../api',()=>({api:{demoFeedback:vi.fn(),snoozeDemoFeedback:vi.fn()}}))
const access:Access={windows:[{days:1,limit:3,completed:1,remaining:2,resetsAt:null},{days:7,limit:7,completed:1,remaining:6,resetsAt:null},{days:30,limit:12,completed:1,remaining:11,resetsAt:null}],canCompose:true,nextAvailableAt:null,surveyDue:true,completedWorks:1}
afterEach(cleanup)
beforeEach(()=>{
  vi.clearAllMocks()
  HTMLDialogElement.prototype.showModal=function(){this.setAttribute('open','')}
  HTMLDialogElement.prototype.close=function(){this.removeAttribute('open')}
})
it('shows all quotas and allows postponing without blocking the studio',async()=>{
  vi.mocked(api.snoozeDemoFeedback).mockResolvedValue()
  render(<DemoAccess access={access} canPrompt onRefresh={()=>{}} />)
  expect(screen.getByText('2/3')).toBeInTheDocument()
  expect(screen.getByText('6/7')).toBeInTheDocument()
  expect(screen.getByRole('dialog')).toBeInTheDocument()
  fireEvent.click(screen.getByText('Más tarde'))
  await waitFor(()=>expect(api.snoozeDemoFeedback).toHaveBeenCalledOnce())
  expect(screen.queryByRole('dialog')).not.toBeInTheDocument()
})
it('does not interrupt playback and saves ratings and both comments',async()=>{
  vi.mocked(api.demoFeedback).mockResolvedValue({saved:true})
  const view=render(<DemoAccess access={access} canPrompt={false} onRefresh={()=>{}} />)
  expect(screen.queryByRole('dialog')).not.toBeInTheDocument()
  view.rerender(<DemoAccess access={access} canPrompt onRefresh={()=>{}} />)
  for(const label of ['Experiencia general','Calidad musical','Facilidad de uso'])fireEvent.change(screen.getByLabelText(label),{target:{value:'4'}})
  fireEvent.change(screen.getByLabelText('¿Qué te gustó?'),{target:{value:'El MIDI editable'}})
  fireEvent.change(screen.getByLabelText('¿Qué falta o funciona mal?'),{target:{value:'Más contraste'}})
  fireEvent.click(screen.getByText('Guardar opinión'))
  await waitFor(()=>expect(api.demoFeedback).toHaveBeenCalledWith(expect.objectContaining({overall:4,musicality:4,easeOfUse:4,strengths:'El MIDI editable',improvements:'Más contraste',locale:'es'})))
  await screen.findByText('Gracias. Tu opinión quedó guardada.')
})
