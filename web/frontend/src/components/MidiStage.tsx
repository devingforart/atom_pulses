const tracks = [
  { name: 'HARMONIC BED', notes: [[2,24],[30,18],[54,30],[90,22]] },
  { name: 'MOTION', notes: [[9,8],[22,6],[38,12],[58,7],[73,10],[96,12]] },
  { name: 'PROTAGONIST', notes: [[16,5],[24,8],[44,4],[50,9],[80,7],[91,5]] },
  { name: 'ATMOSPHERE', notes: [[0,36],[42,30],[78,38]] },
]

export function MidiStage() {
  return <div className="midi-stage" aria-label="Vista conceptual de una composición en PULSO">
    <div className="device-bar"><span><i/>PULSO / ARRANGEMENT</span><span>SCORE 01</span></div>
    <div className="stage-head"><span>F♯ MINOR</span><span>06:30</span><span>128 BPM</span></div>
    {tracks.map((track, index) => <div className="midi-track" key={track.name}>
      <b><span>{track.name}</span><small>0{index + 1}</small></b><div className="note-lane">
        {track.notes.map(([left,width], note) => <i key={note} style={{ left: `${left}%`, width: `${width}%`, top: `${(note * 7 + index * 9) % 70}%` }} />)}
      </div>
    </div>)}
    <div className="playhead" />
  </div>
}
