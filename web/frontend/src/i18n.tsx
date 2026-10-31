import { useSyncExternalStore, useRef, useEffect } from 'react'
import messages from './locales/messages.json'

export type Language = 'es' | 'en' | 'pt-BR'
type ThemePreference = 'system' | 'dark' | 'light'
const languageKey = 'pulso-language'
const themeKey = 'pulso-studio-theme'
const listeners = new Set<() => void>()
const notify = () => listeners.forEach(fn => fn())
const subscribe = (fn: () => void) => { listeners.add(fn); return () => { listeners.delete(fn) } }
const read = (key: string) => { try { return localStorage.getItem(key) } catch { return null } }
const save = (key: string, value: string) => { try { localStorage.setItem(key, value) } catch {} }
const validLanguage = (value: string | null): value is Language => ['es', 'en', 'pt-BR'].includes(value || '')

export function detectLanguage(languages: readonly string[], timeZone: string): Language {
  for (const code of languages) {
    if (/^es(?:-|$)/i.test(code)) return 'es'
    if (/^pt(?:-|$)/i.test(code)) return 'pt-BR'
    if (/^en(?:-|$)/i.test(code)) return 'en'
  }
  if (/^America\/(Sao_Paulo|Bahia|Belem|Fortaleza|Recife|Maceio|Araguaina|Cuiaba|Manaus|Porto_Velho|Rio_Branco|Campo_Grande|Boa_Vista|Noronha|Santarem|Eirunepe)$/.test(timeZone)) return 'pt-BR'
  if (/^Europe\/Madrid$|^Atlantic\/Canary$|^America\/(Argentina\/|Buenos_Aires|Cordoba|Mendoza|Santiago|Punta_Arenas|Lima|Bogota|Caracas|Guayaquil|La_Paz|Asuncion|Montevideo|Mexico_City|Cancun|Merida|Monterrey|Tijuana|Hermosillo|Mazatlan|Chihuahua|Ciudad_Juarez|Guatemala|Tegucigalpa|Managua|Costa_Rica|Panama|El_Salvador|Havana|Santo_Domingo|Puerto_Rico)/.test(timeZone)) return 'es'
  return 'en'
}
let timeZone = 'UTC'
try { timeZone = Intl.DateTimeFormat().resolvedOptions().timeZone } catch {}
let language: Language = validLanguage(read(languageKey)) ? read(languageKey) as Language : detectLanguage(navigator.languages || [navigator.language], timeZone)
const media = typeof matchMedia === 'function' ? matchMedia('(prefers-color-scheme: dark)') : null
let themePreference: ThemePreference = ['dark','light'].includes(read(themeKey) || '') ? read(themeKey) as ThemePreference : 'system'
function currentTheme() { return themePreference === 'system' ? media?.matches ? 'dark' : 'light' : themePreference }
function applyPreferences() {
  document.documentElement.lang = language
  document.documentElement.dataset.theme = currentTheme()
  document.documentElement.style.colorScheme = currentTheme()
  document.title = language === 'es' ? 'PULSO — composición con dirección' : language === 'pt-BR' ? 'PULSO — composição com direção' : 'PULSO — composition with direction'
}
applyPreferences()
media?.addEventListener?.('change', () => { if (themePreference === 'system') { applyPreferences(); notify() } })
window.addEventListener('storage', event => {
  if (event.key === languageKey && validLanguage(event.newValue)) language = event.newValue
  if (event.key === themeKey) themePreference = event.newValue === 'dark' || event.newValue === 'light' ? event.newValue : 'system'
  applyPreferences(); notify()
})
// Back/forward cache can restore an older document after another page changed
// the preference. Re-read persistent choices before displaying that document.
function restorePreferences() {
  const savedLanguage = read(languageKey)
  if (validLanguage(savedLanguage)) language = savedLanguage
  const savedTheme = read(themeKey)
  if (savedTheme === 'dark' || savedTheme === 'light' || savedTheme === 'system') themePreference = savedTheme
  applyPreferences(); notify()
}
window.addEventListener('pageshow', restorePreferences)
document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'visible') restorePreferences() })
export function locale(): Language { return language }
export function setLanguage(value: Language) { language = value; save(languageKey,value); applyPreferences(); notify() }
export function useLocale() { return useSyncExternalStore(subscribe, locale, () => 'es' as Language) }
export function useTheme() {
  useSyncExternalStore(subscribe, () => themePreference + ':' + currentTheme(), () => 'system:light')
  return { theme: currentTheme(), preference: themePreference,
    setTheme: setAppearance }
}
export function setAppearance(value: ThemePreference) { themePreference = value; save(themeKey,value); applyPreferences(); notify() }
export function t(source: string | undefined, params: Record<string, string | number> = {}): string {
  if (!source) return ''
  const entry = (messages as Record<string, {en: string; 'pt-BR': string}>)[source]
  const template = language === 'es' ? source : entry?.[language] ?? source
  return template.replace(/\{(\w+)\}/g, (whole, key: string) => Object.hasOwn(params,key) ? String(params[key]) : whole)
}
export function Preferences() {
  const lang = useLocale(); const theme = useTheme()
  const label = lang === 'en' ? 'Language' : lang === 'pt-BR' ? 'Idioma' : 'Idioma'
  return <div className="site-preferences">
    <LanguagePicker label={label} />
    <div className="theme-control">
      <button type="button" className="theme-switch" role="switch" aria-checked={theme.theme === 'dark'} aria-label={lang === 'en' ? 'Dark theme' : lang === 'pt-BR' ? 'Tema escuro' : 'Tema oscuro'} title={theme.theme === 'dark' ? t('Activar tema claro') : t('Activar tema oscuro')} onClick={() => theme.setTheme(theme.theme === 'dark' ? 'light' : 'dark')}>
        <span className="theme-switch-thumb" aria-hidden="true" />
        <svg className="theme-sun" viewBox="0 0 24 24" fill="none" aria-hidden="true"><circle cx="12" cy="12" r="3.5"/><path d="M12 2v2m0 16v2M2 12h2m16 0h2M5 5l1.5 1.5m11 11L19 19M5 19l1.5-1.5m11-11L19 5"/></svg>
        <svg className="theme-moon" viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M19.8 15A8.2 8.2 0 0 1 9 4.2 8.2 8.2 0 1 0 19.8 15Z"/></svg>
      </button>
      <button type="button" className="theme-system" aria-pressed={theme.preference === 'system'} aria-label={lang === 'en' ? 'Follow system theme' : lang === 'pt-BR' ? 'Seguir tema do sistema' : 'Seguir tema del sistema'} title={lang === 'en' ? 'Follow system theme' : lang === 'pt-BR' ? 'Seguir tema do sistema' : 'Seguir tema del sistema'} onClick={() => theme.setTheme('system')}><svg viewBox="0 0 24 24" fill="none" aria-hidden="true"><rect x="3" y="4" width="18" height="13" rx="2"/><path d="M8 21h8m-4-4v4"/></svg></button>
    </div>
  </div>
}

function LanguageFlag({ language }: { language: Language }) {
  return <svg className="language-flag" viewBox="0 0 30 20" aria-hidden="true">
    {language === 'es' ? <><path fill="#b92435" d="M0 0h30v20H0z"/><path fill="#f5ca52" d="M0 5h30v10H0z"/><path fill="#b92435" d="M8 8h3v5H8z"/></> : language === 'pt-BR' ? <><path fill="#208747" d="M0 0h30v20H0z"/><path fill="#f8d64d" d="m15 2 12 8-12 8L3 10Z"/><circle cx="15" cy="10" r="5" fill="#26428a"/><path d="M10.3 8.5q5-1 9.4 3" stroke="#fff" strokeWidth="1.1" fill="none"/></> : <><path fill="#223c73" d="M0 0h30v20H0z"/><path d="m0 0 30 20M30 0 0 20" stroke="#fff" strokeWidth="5"/><path d="m0 0 30 20M30 0 0 20" stroke="#c43644" strokeWidth="2"/><path d="M15 0v20M0 10h30" stroke="#fff" strokeWidth="8"/><path d="M15 0v20M0 10h30" stroke="#c43644" strokeWidth="4"/></>}
  </svg>
}
function LanguagePicker({label}: {label: string}) {
  const lang = useLocale(); const ref = useRef<HTMLDetailsElement>(null)
  const choices: [Language,string][] = [['es','Español'],['en','English'],['pt-BR','Português (Brasil)']]
  useEffect(() => {
    const close = (event: PointerEvent) => { if (event.target instanceof Node && !ref.current?.contains(event.target)) ref.current?.removeAttribute('open') }
    document.addEventListener('pointerdown', close)
    return () => document.removeEventListener('pointerdown', close)
  }, [])
  return <details ref={ref} className="language-picker" onKeyDown={event => { if (event.key === 'Escape') { ref.current?.removeAttribute('open'); ref.current?.querySelector('summary')?.focus() } }}>
    <summary aria-label={`${label}: ${choices.find(c => c[0] === lang)?.[1]}`} title={label}><LanguageFlag language={lang}/><svg className="language-chevron" viewBox="0 0 12 12" aria-hidden="true"><path d="m3 4.5 3 3 3-3"/></svg></summary>
    <div className="language-options" role="group" aria-label={label}>{choices.map(([code,name]) => <button key={code} type="button" lang={code} aria-pressed={lang === code} onClick={() => { setLanguage(code); ref.current?.removeAttribute('open'); ref.current?.querySelector('summary')?.focus() }}><LanguageFlag language={code}/><span>{name}</span><span aria-hidden="true">{lang === code ? '✓' : ''}</span></button>)}</div>
  </details>
}
