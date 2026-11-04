#!/usr/bin/env python3
"""Works out, for maps that are too hard to win, how much to cut the health of their worst waves so that Hard asks for at most
a given share of the ideal damage (see check_maps.py). Usage: propose_rebalance.py <data folder> <map> [<map> ...]"""
import sys,os,re,math
args=sys.argv[2:]
sys.argv=sys.argv[:2]
src=open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'check_maps.py')).read().split("maps = [l.strip()")[0]
exec(src)
def run(name,target):
    mapdir=os.path.join(DATA,'maps','%s.xml'%name)
    start,lives,tile,buildable,towers,waves0=analyse(mapdir)
    sc=[1.0]*len(waves0)
    def scaled(): return [(sp,[(max(1,int(h*sc[k])),m,v,c) for h,m,v,c in es]) for k,(sp,es) in enumerate(waves0)]
    def ratios(d):
        gold=start; out=[]
        for k,(sp,es) in enumerate(scaled()):
            n=sum(e[3] for e in es); slow=min(e[2] for e in es)
            rho,nm,lv,rad,cost=best(towers,tile,slow*sp/1000.0,k)
            dur=(n-1)*sp/1000.0+2*rad/slow
            hp=sum(e[0]*e[3] for e in es)*d
            out.append(hp/(rho*gold*dur)); gold+=sum(e[1]*e[3] for e in es)
        return out
    for it in range(300):
        nd=needed(start,lives,towers,tile,scaled(),3)
        if nd is not None and nd<=target: break
        r=ratios(3); k=r.index(max(r)); sc[k]*=0.94
    print(name,'scales',[round(x,2) for x in sc],flush=True)
    for d in (1,2,3):
        a=needed(start,lives,towers,tile,scaled(),d)
        print('  difficulty',d,'needs',('%.0f%%'%(a*100)) if a else 'more than 100%',flush=True)
    for k in range(len(sc)):
        print('   wave %d x%.2f: %s'%(k+1,sc[k],[(h,max(1,int(h*sc[k]))) for h,m,v,c in waves0[k][1]]),flush=True)
for name in args: run(name,0.85)
