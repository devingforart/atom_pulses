import { describe, expect, it } from 'vitest'
import messages from './locales/messages.json'
import { detectLanguage, setLanguage, locale, t, setAppearance } from './i18n'

describe('website localization', () => {
  it('uses supported browser languages before ambiguous geographic information', () => {
    expect(detectLanguage(['en-US'], 'America/Argentina/Buenos_Aires')).toBe('en')
    expect(detectLanguage(['es-MX'], 'Europe/London')).toBe('es')
    expect(detectLanguage(['pt-BR'], 'Europe/Lisbon')).toBe('pt-BR')
    expect(detectLanguage(['fr-FR'], 'America/Sao_Paulo')).toBe('pt-BR')
    expect(detectLanguage([], 'America/Argentina/Buenos_Aires')).toBe('es')
    expect(detectLanguage([], 'Asia/Tokyo')).toBe('en')
  })
  it('updates language, document metadata and interpolations without altering user values', () => {
    setLanguage('en')
    expect(locale()).toBe('en')
    expect(document.documentElement.lang).toBe('en')
    expect(t('Silenciar {p0}', {p0:'Mi obra en español'})).toBe('Mute Mi obra en español')
    setLanguage('pt-BR')
    expect(t('Nueva composición')).toBe('Nova composição')
    expect(t(undefined)).toBe('')
  })
  it('has complete translations and preserves every interpolation', () => {
    for (const [source, entry] of Object.entries(messages)) {
      for (const lang of ['en','pt-BR'] as const) {
        expect(entry[lang].trim(), source).not.toBe('')
        expect(entry[lang].match(/\{p\d+\}/g)?.sort() || [], source).toEqual(source.match(/\{p\d+\}/g)?.sort() || [])
      }
    }
  })
  it('stores an explicit appearance and allows system preference again', () => {
    setAppearance('light'); expect(document.documentElement.dataset.theme).toBe('light')
    setAppearance('dark'); expect(document.documentElement.dataset.theme).toBe('dark')
    setAppearance('system'); expect(localStorage.getItem('pulso-studio-theme')).toBe('system')
  })
})
