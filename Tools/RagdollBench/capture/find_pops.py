import csv,sys,math,collections
rows=list(csv.DictReader(open(sys.argv[1])))
F={}
for r in rows:
    f=int(r['frame']); F.setdefault(f,{'t':float(r['time']),'dt':float(r['dt']),'steps':int(r['steps']),'alpha':float(r['alpha']),'state':int(r['state']),'pow':int(r['powered']),'bal':int(r['balancing']),'blend':float(r['blend']),'d':{},'p':{}})
    b=int(r['body']); F[f]['d'][b]=(float(r['dx']),float(r['dy']),float(r['dz'])); F[f]['p'][b]=(float(r['px']),float(r['py']),float(r['pz']))
fl=sorted(F)
def dist(a,b): return math.sqrt(sum((a[i]-b[i])**2 for i in range(3)))
# jumps: drawn step of each body vs. neighbours; report frame-to-frame second difference in cm
ev=[]
for k in range(2,len(fl)):
    a,b,c=F[fl[k-2]],F[fl[k-1]],F[fl[k]]
    worst=0;wb=0
    for i in range(18):
        sd=math.sqrt(sum((c['d'][i][j]-2*b['d'][i][j]+a['d'][i][j])**2 for j in range(3)))*100
        if sd>worst: worst=sd;wb=i
    ev.append((c['t'],worst,wb,fl[k]))
print('second difference of drawn position, cm/frame^2: histogram per frame')
h=collections.Counter(min(int(math.log2(max(e[1],0.01))+7),14) for e in ev)
for k in sorted(h): print(f"  >={2**(k-7):8.3f} cm: {h[k]}")
big=[e for e in ev if e[1]>0.6]
print('frames with > 0.6 cm:',len(big),'of',len(ev))
# cluster
cl=[];
for e in big:
    if cl and e[0]-cl[-1][-1][0]<0.15: cl[-1].append(e)
    else: cl.append([e])
for c in cl[:60]:
    w=max(c,key=lambda e:e[1]); f=F[w[3]]
    print(f"t {c[0][0]:6.2f}-{c[-1][0]:6.2f} n {len(c):3} worst {w[1]:7.1f} cm body {w[2]:2} state {f['state']} pow {f['pow']} bal {f['bal']} blend {f['blend']:.2f} steps {f['steps']} alpha {f['alpha']:.2f}")
for T in sys.argv[2:]:
    T=float(T); print('around',T)
    for f in fl:
        if abs(F[f]['t']-T)<0.035:
            x=F[f]; print(f"  t {x['t']:.3f} st {x['state']} pow {x['pow']} bal {x['bal']} steps {x['steps']} a {x['alpha']:.2f} hipsD {x['d'][0][0]:.3f},{x['d'][0][1]:.3f},{x['d'][0][2]:.3f} hipsP {x['p'][0][0]:.3f},{x['p'][0][1]:.3f},{x['p'][0][2]:.3f}")
