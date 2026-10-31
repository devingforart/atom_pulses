import '@testing-library/jest-dom/vitest'
import { beforeEach } from 'vitest'
import { setLanguage, setAppearance } from '../i18n'
beforeEach(() => { setLanguage('es'); setAppearance('dark') })
