import { t, useLocale, locale } from './i18n'
import { BrowserRouter, Route, Routes } from 'react-router-dom'
import { AuthProvider } from './auth'
import { Layout } from './components/Layout'
import { Account } from './pages/Account'
import { AuthPage } from './pages/Auth'
import { Home } from './pages/Home'
import { Privacy, Terms } from './pages/Legal'
import { Pricing } from './pages/Pricing'
import { Product } from './pages/Product'
import { Activate } from './pages/Activate'
import { ForgotPassword, ResetPassword, VerifyEmail } from './pages/Recovery'
import { Cloud, SharedCloud } from './pages/Cloud'

export function App() {
  useLocale()
  return <BrowserRouter><AuthProvider><Routes><Route path="share/:token" element={<SharedCloud />} /><Route element={<Layout />}>
    <Route index element={<Home />} /><Route path="product" element={<Product />} /><Route path="pricing" element={<Pricing />} />
    <Route path="login" element={<AuthPage mode="login" />} /><Route path="register" element={<AuthPage mode="register" />} />
    <Route path="forgot-password" element={<ForgotPassword />} /><Route path="reset-password" element={<ResetPassword />} />
    <Route path="verify-email" element={<VerifyEmail />} /><Route path="activate" element={<Activate />} />
    <Route path="account" element={<Account />} /><Route path="privacy" element={<Privacy />} /><Route path="terms" element={<Terms />} />
    <Route path="cloud" element={<Cloud />} />
    <Route path="*" element={<section className="page-hero"><h1>404</h1><p>{t("Esta página no existe.")}</p></section>} />
  </Route></Routes></AuthProvider></BrowserRouter>
}
