/** Brand loader: a revolving orbit around the exact PULSO mark. */
export function PulsoSpinner({ label, children }: { label: string; children?: React.ReactNode }) {
  return <div className="pulso-loader" role="status" aria-live="polite">
    <div className="pulso-loader-symbol" aria-hidden="true">
      <svg className="pulso-loader-orbit" viewBox="0 0 80 80"><circle className="pulso-loader-track" cx="40" cy="40" r="35"/><circle className="pulso-loader-arc" cx="40" cy="40" r="35"/></svg>
      <svg className="pulso-loader-mark" viewBox="0 0 34 24"><circle cx="3" cy="12" r="2.2"/><path d="M5.5 12h4l2.4-7 4.2 14 4.1-11 3.2 7h4l2.2-3H33"/></svg>
    </div><span>{label}</span>{children}
  </div>
}
