import './PulsoWordmark.css'

// Original user-provided geometry, cropped to its lettering and transparent
// so the same artwork belongs naturally to either site theme.
const rows = [
  [[98,85],[228,25],[318,25],[358,25],[488,55],[618,55]],
  [[98,25],[188,25],[228,25],[318,25],[358,25],[458,25],[548,25],[588,25],[678,25]],
  [[98,85],[228,25],[318,25],[358,25],[488,55],[588,25],[678,25]],
  [[98,25],[228,25],[318,25],[358,25],[518,55],[588,25],[678,25]],
  [[98,25],[228,25],[318,25],[358,25],[458,25],[548,25],[588,25],[678,25]],
  [[98,25],[258,55],[358,85],[488,55],[618,55]],
]
export function PulsoWordmark() {
  return <svg className="pulso-wordmark" xmlns="http://www.w3.org/2000/svg" viewBox="98 115 605 120" role="img" aria-label="PULSO" focusable="false">
    {rows.map((rectangles, row) => <g key={row} fill={row < 2 ? '#E2785F' : row < 4 ? '#EAC15F' : '#8CB1D6'}>
      {rectangles.map(([x, width]) => <rect key={x} x={x} y={115 + row * 22} width={width} height="10" />)}
    </g>)}
  </svg>
}
