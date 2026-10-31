import './PulsoWordmark.css'

// Five pitch lanes, five custom glyphs. Each segment is a MIDI note:
// its onset and gate build the letter without a background grid or font.
const glyphs = [
  [[[0,30]], [[0,8],[26,8]], [[0,30]], [[0,8]], [[0,8]]],
  [[[0,8],[26,8]], [[0,8],[26,8]], [[0,8],[26,8]], [[0,8],[26,8]], [[5,24]]],
  [[[0,8]], [[0,8]], [[0,8]], [[0,8]], [[0,34]]],
  [[[5,29]], [[0,8]], [[5,24]], [[26,8]], [[0,29]]],
  [[[5,24]], [[0,8],[26,8]], [[0,8],[26,8]], [[0,8],[26,8]], [[5,24]]],
]
export function PulsoWordmark() {
  return <span className="pulso-brand-lockup">
    <svg className="pulso-brand-wave" viewBox="0 0 34 24" aria-hidden="true" focusable="false">
      <circle cx="3" cy="12" r="2.2" />
      <path d="M5.5 12h4l2.4-7 4.2 14 4.1-11 3.2 7h4l2.2-3H33" />
    </svg>
    <svg className="pulso-wordmark" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 218 52" role="img" aria-label="PULSO" focusable="false">
    {glyphs.map((rows, letter) => <g key={letter} transform={`translate(${letter * 46} 0)`}>
      {rows.map((notes, lane) => <g key={lane}>
        {notes.map(([x, width]) => <rect key={x} x={x} y={lane * 11} width={width} height="8" rx="1.3"
          className={letter === 3 && lane === 2 ? 'pulso-midi-accent' : undefined} />)}
      </g>)}
    </g>)}
  </svg></span>
}
