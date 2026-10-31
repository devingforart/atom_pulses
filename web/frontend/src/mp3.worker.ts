import { Mp3Encoder } from '@breezystack/lamejs'

self.onmessage = async (event: MessageEvent<{ wav: Blob }>) => {
  try {
    const wav = event.data.wav
    const header = new DataView(await wav.slice(0,44).arrayBuffer())
    if (header.getUint16(20,true) !== 1 || header.getUint16(22,true) !== 2 || header.getUint16(34,true) !== 16)
      throw new Error('Unsupported PCM format')
    const encoder = new Mp3Encoder(2, header.getUint32(24,true), 192)
    const parts: ArrayBuffer[] = []
    const blockBytes = 1152 * 4 * 128
    for (let offset=44; offset<wav.size; offset+=blockBytes) {
      const view = new DataView(await wav.slice(offset,Math.min(wav.size,offset+blockBytes)).arrayBuffer())
      for (let pos=0; pos<view.byteLength; pos+=1152*4) {
        const frames = Math.floor(Math.min(1152*4,view.byteLength-pos)/4)
        const left = new Int16Array(frames), right = new Int16Array(frames)
        for(let i=0;i<frames;i++){left[i]=view.getInt16(pos+i*4,true);right[i]=view.getInt16(pos+i*4+2,true)}
        const bytes=encoder.encodeBuffer(left,right)
        if(bytes.length) parts.push(new Uint8Array(bytes).buffer)
      }
      self.postMessage({progress:Math.min(1,(offset+view.byteLength)/wav.size)})
    }
    const final=encoder.flush(); if(final.length)parts.push(new Uint8Array(final).buffer)
    self.postMessage({blob:new Blob(parts,{type:'audio/mpeg'})})
  } catch { self.postMessage({error:true}) }
}
