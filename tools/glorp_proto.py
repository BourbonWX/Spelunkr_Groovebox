# SPDX-License-Identifier: GPL-3.0-only
# Phantom Tracker 0.9.8: the GLORP sketches (Ed, Nov 2026, picked F: "Glorpy F deff sounds way better"). numpy;
# writes glorp-A..F.wav here. A formant gloop, B bubble, C frog croak, D glorp, E envelope wah, F vowel wah (eng_glorp.c).
import numpy as np, wave
SR=44100
def save(name,x):
    x=x/ (np.max(np.abs(x))+1e-9)*0.89
    s=np.stack([x,x],1) if x.ndim==1 else x
    w=wave.open(name,'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((s*32767).astype('<i2').tobytes()); w.close()
def svf_bp(x,f,q):  # f array Hz, resonant band-pass (Chamberlin, per-sample coeffs)
    y=np.zeros_like(x); lo=bp=0.0
    F=2*np.sin(np.pi*np.minimum(f,SR/6)/SR); d=1.0/q
    for i in range(len(x)):
        hi=x[i]-lo-d*bp; bp+=F[i]*hi; lo+=F[i]*bp; y[i]=bp
    return y
def svf_lp(x,f,q):
    y=np.zeros_like(x); lo=bp=0.0
    F=2*np.sin(np.pi*np.minimum(f,SR/6)/SR); d=1.0/q
    for i in range(len(x)):
        hi=x[i]-lo-d*bp; bp+=F[i]*hi; lo+=F[i]*bp; y[i]=lo
    return y
def glott(f0):  # band-ish glottal pulse train from a phase
    ph=np.cumsum(f0/SR)%1.0
    return np.where(ph<0.4, np.sin(np.pi*ph/0.4)**2, 0.0)-0.25, ph
def saw(f0):
    ph=np.cumsum(f0/SR)%1.0; return 2*ph-1
V={'u':(300,870,2240),'o':(450,800,2830),'a':(730,1090,2440),'e':(530,1840,2480),'i':(270,2290,3010)}
def formants(src,traj,qs=(9,11,13),g=(1,.6,.25)):
    out=0
    for k in range(3):
        out=out+g[k]*svf_bp(src,traj[:,k],qs[k])
    return out
def vtraj(seq,n):  # seq of (pos0..1, vowel)
    pos=np.linspace(0,1,n); P=[p for p,_ in seq]
    return np.stack([np.interp(pos,P,[V[v][k] for _,v in seq]) for k in range(3)],1)
bpm=95; beat=60/bpm; step=beat/4
riff=[(0,36,2),(3,36,1),(4,43,2),(7,41,1),(8,39,3),(11,36,1),(12,46,2),(14,43,2)]  # (step,midi,len)
def hz(m): return 440*2**((m-69)/12)
def render(voice,bars=2):
    N=int(SR*step*16*bars); out=np.zeros(N)
    for b in range(bars):
        for st,m,ln in riff:
            n0=int(SR*(b*16+st)*step); n=int(SR*ln*step*1.15)
            seg=voice(hz(m+12),n)
            e=min(N,n0+n); out[n0:e]+=seg[:e-n0]
    return out
def env(n,a=0.004,r=0.06):
    t=np.arange(n)/SR; d=n/SR
    return np.clip(t/a,0,1)*np.clip((d-t)/r,0,1)
# A: formant glide "oo-wah-oo" with a pitch dip (gloop)
def A(f,n):
    t=np.arange(n)/SR
    f0=f*(1+0.5*np.exp(-t/0.03))*(1-0.06*np.minimum(t/0.2,1))
    src,_=glott(f0)
    tr=vtraj([(0,'u'),(0.35,'a'),(1,'o')],n)
    return np.tanh(2*formants(src,tr))*env(n)
# B: bubble/bloop: sine, fast upward pitch sweep + resonant low-pass ping
def B(f,n):
    t=np.arange(n)/SR
    f0=f*(0.5+1.5*(1-np.exp(-t/0.045)))
    ph=np.cumsum(f0/SR); x=np.sin(2*np.pi*ph)+0.4*np.sin(4*np.pi*ph)
    fc=f*(2+10*np.exp(-t/0.05))
    return np.tanh(1.5*svf_lp(x,fc,6))*env(n)
# C: frog croak: low glottal pulses gated in bursts (ribbit), low vowels, vibrato
def C(f,n):
    t=np.arange(n)/SR
    f0=f*0.5*(1+0.03*np.sin(2*np.pi*7*t))
    src,_=glott(f0)
    gate=(np.sin(2*np.pi*28*t)>-0.2).astype(float)
    gate=np.convolve(gate,np.ones(60)/60,'same')
    tr=vtraj([(0,'o'),(0.5,'u'),(1,'o')],n)
    return np.tanh(3*formants(src*gate,tr,qs=(12,14,14),g=(1,.8,.15)))*env(n,0.002,0.03)
# D: the glorp: A + B combined: pitch drops into the note, vowel opens on the envelope, wet resonant throat, drive
def D(f,n):
    t=np.arange(n)/SR
    f0=f*(1+1.0*np.exp(-t/0.02))
    s=saw(f0)*0.6+glott(f0)[0]
    o=np.exp(-t/0.09)  # vowel opens then closes
    tr=np.stack([V['u'][k]+(V['a'][k]-V['u'][k])*(1-o)*np.exp(-t/0.25)+0 for k in range(3)],1)
    wet=formants(s,tr,qs=(14,16,16))
    lp=svf_lp(wet, f*(3+12*o), 4)
    wob=1+0.25*np.sin(2*np.pi*(6+4*o)*t)
    return np.tanh(2.5*(0.6*wet+0.8*lp)*wob)*env(n)
for name,v in [('A-formant-gloop',A),('B-bubble-bloop',B),('C-frog-croak',C),('D-glorp',D)]:
    save(f'glorp-{name}.wav',render(v)); print(name)
# riff with velocities: soft, medium, hard mixed
vriff=[(0,36,2,40),(3,36,1,70),(4,43,2,127),(7,41,1,55),(8,39,3,100),(11,36,1,35),(12,46,2,127),(14,43,2,80)]
def renderv(voice,bars=2):
    N=int(SR*step*16*bars); out=np.zeros(N)
    for b in range(bars):
        for st,m,ln,v in vriff:
            n0=int(SR*(b*16+st)*step); n=int(SR*ln*step*1.15)
            seg=voice(hz(m+12),n,v/127); e=min(N,n0+n); out[n0:e]+=seg[:e-n0]
    return out
# E: envelope wah (Mu-Tron style): resonant filter sweeps up on the hit and back; velocity = how far + how fast
def E(f,n,v):
    t=np.arange(n)/SR
    src=saw(f)*0.7+glott(f)[0]*0.6
    sweep=(1-np.exp(-t/0.012))*np.exp(-t/(0.06+0.18*v))
    fc=f*1.5+ (250+3800*v**1.3)*sweep
    y=svf_bp(src,fc,7+5*v)*1.4+svf_lp(src,fc,3)*0.6
    return np.tanh(2*y)*env(n)*(0.5+0.5*v)
# F: vowel wah, the glorp: the vowel opens oo -> ah (harder = wider, to ee on the hardest), closes back, pitch drops in
def F(f,n,v):
    t=np.arange(n)/SR
    f0=f*(1+0.35*v*np.exp(-t/0.025))
    src=glott(f0)[0]+0.5*saw(f0)
    o=(1-np.exp(-t/0.015))*np.exp(-t/(0.07+0.2*v))
    tgt=np.array(V['a'])*(1-max(0,v-0.75)*2)+np.array(V['e'])*max(0,v-0.75)*2
    u=np.array(V['u'])
    tr=u[None,:]+(tgt-u)[None,:]*(np.minimum(1,v*1.2)*o)[:,None]
    y=formants(src,tr,qs=(10+6*v,12+6*v,14))
    return np.tanh(2.5*y)*env(n)*(0.5+0.5*v)
save('glorp-E-envelope-wah.wav',renderv(E)); save('glorp-F-vowel-wah.wav',renderv(F)); print('ok')
