export function encodeMp3(wav: Blob, onProgress: (progress: number) => void): Promise<Blob> {
  return new Promise((resolve,reject) => {
    const worker=new Worker(new URL('./mp3.worker.ts',import.meta.url),{type:'module'})
    worker.onmessage=event=>{
      if(event.data.error){worker.terminate();reject(new Error('No se pudo exportar el audio MP3.'))}
      else if(event.data.blob){worker.terminate();resolve(event.data.blob)}
      else if(typeof event.data.progress==='number')onProgress(event.data.progress)
    }
    worker.onerror=()=>{worker.terminate();reject(new Error('No se pudo exportar el audio MP3.'))}
    worker.postMessage({wav})
  })
}
