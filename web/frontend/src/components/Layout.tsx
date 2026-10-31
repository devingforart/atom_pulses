import { t, useLocale, locale, Preferences } from '../i18n'
import { Link, NavLink, Outlet, useLocation } from 'react-router-dom'
import { useAuth } from '../auth'
import { Logo } from './Logo'

export function Layout() {
  useLocale();
  const { user } = useAuth()
  const { pathname } = useLocation()
  if (pathname === '/cloud') return <main id="content" className="studio-fullscreen"><Outlet /></main>
  return <div className="site-shell">
    <a className="skip-link" href="#content">{t("Saltar al contenido")}</a>
    <header className="nav-wrap">
      <Link to="/" className="brand-link"><Logo /><span className="brand-caption">{t("Estudio de composición")}</span></Link>
      <nav aria-label={t("Principal")}>
        <NavLink to="/product">{t("Producto")}</NavLink>
        <NavLink to="/pricing">{t("Planes")}</NavLink>
        <NavLink to="/cloud">{t("Cloud")}</NavLink>
        <a href="/#workflow">{t("Cómo funciona")}</a>
      </nav>
      <Preferences />
      <Link className="button button-small button-ghost" to={user ? '/account' : '/login'}>
        {user ? t("Mi cuenta") : t("Ingresar")}
      </Link>
    </header>
    <main id="content"><Outlet /></main>
    <footer>
      <div><Logo /><p>{t("Composición con dirección. MIDI con espacio para producir.")}</p></div>
      <div className="footer-links">
        <Link to="/product">{t("Producto")}</Link><Link to="/pricing">{t("Planes")}</Link>
        <a href="mailto:support@pulso.audio">{t("Soporte")}</a><Link to="/privacy">{t("Privacidad")}</Link><Link to="/terms">{t("Términos")}</Link>
      </div>
      <span>© {new Date().getFullYear()}{' '}{t("PULSO")}</span>
    </footer>
  </div>
}
