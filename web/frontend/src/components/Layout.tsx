import { Link, NavLink, Outlet } from 'react-router-dom'
import { useAuth } from '../auth'
import { Logo } from './Logo'

export function Layout() {
  const { user } = useAuth()
  return <div className="site-shell">
    <a className="skip-link" href="#content">Saltar al contenido</a>
    <header className="nav-wrap">
      <Link to="/" className="brand-link"><Logo /><span className="brand-caption">Composition system</span></Link>
      <nav aria-label="Principal">
        <NavLink to="/product">Producto</NavLink>
        <NavLink to="/pricing">Planes</NavLink>
        <a href="/#workflow">Cómo funciona</a>
      </nav>
      <Link className="button button-small button-ghost" to={user ? '/account' : '/login'}>
        {user ? 'Mi cuenta' : 'Ingresar'}
      </Link>
    </header>
    <main id="content"><Outlet /></main>
    <footer>
      <div><Logo /><p>Composición con dirección. MIDI con espacio para producir.</p></div>
      <div className="footer-links">
        <Link to="/product">Producto</Link><Link to="/pricing">Planes</Link>
        <a href="mailto:support@pulso.audio">Soporte</a><Link to="/privacy">Privacidad</Link><Link to="/terms">Términos</Link>
      </div>
      <span>© {new Date().getFullYear()} PULSO</span>
    </footer>
  </div>
}
