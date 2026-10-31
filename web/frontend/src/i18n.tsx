import { useSyncExternalStore } from 'react'
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
    <select aria-label={label} title={label} value={lang} onChange={e => setLanguage(e.target.value as Language)}><option value="es">Español</option><option value="en">English</option><option value="pt-BR">Português (Brasil)</option></select>
    <select aria-label={lang === 'en' ? 'Appearance' : lang === 'pt-BR' ? 'Aparência' : 'Apariencia'} value={theme.preference} onChange={e => theme.setTheme(e.target.value as ThemePreference)}><option value="system">{lang === 'en' ? 'System theme' : lang === 'pt-BR' ? 'Tema do sistema' : 'Tema del sistema'}</option><option value="dark">{lang === 'en' ? 'Dark' : lang === 'pt-BR' ? 'Escuro' : 'Oscuro'}</option><option value="light">{lang === 'en' ? 'Light' : 'Claro'}</option></select>
  </div>
}
