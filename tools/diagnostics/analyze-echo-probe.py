from pathlib import Path
import numpy as np,json
out=[]
for path in sorted(Path('artifacts').glob('echo-probe-*-source.f32'),key=lambda p:int(p.name.split('-')[2])):
    a=np.fromfile(path,dtype=np.float32).astype(float);b=np.fromfile(str(path).replace('-source','-reference'),dtype=np.float32).astype(float)
    if len(a)<1000 or len(b)<1000 or max(abs(b),default=0)<.005:continue
    n=1000;ac=np.r_[0,np.cumsum(a*a)];peak=int(np.argmax(ac[n:]-ac[:-n]));query=a[peak:peak+n];query-=query.mean()
    width=1 << (len(b)+n-2).bit_length()
    cross=np.fft.irfft(np.fft.rfft(b,width)*np.fft.rfft(query[::-1],width),width)[n-1:len(b)]
    sums=np.r_[0,np.cumsum(b)];squares=np.r_[0,np.cumsum(b*b)];e=squares[n:]-squares[:-n]-(sums[n:]-sums[:-n])**2/n
    scores=cross*cross/np.maximum(1e-20,np.sum(query*query)*e);start=int(np.argmax(scores));lag=start-peak
    blocks=[]
    for pos in range(0,len(a),500):
        q=a[pos:pos+500];energy=np.sum(q*q)
        if energy<max(ac[n:]-ac[:-n])*.025 or len(q)<100:continue
        t=b[max(0,pos+lag):max(0,pos+lag)+len(q)];score=float(np.corrcoef(q,t)[0,1]**2) if pos+lag>=0 and len(t)==len(q) and np.std(t)>1e-10 else 0
        blocks.append([pos,round(score,3),round(energy,5)])
    out.append(dict(probe=path.name,seconds=len(a)/2000,score=float(scores[start]),lag=lag,blocks=blocks))
print(json.dumps(out,indent=2))
Path('artifacts/echo-probe-analysis.json').write_text(json.dumps(out,indent=2)+'\n')
