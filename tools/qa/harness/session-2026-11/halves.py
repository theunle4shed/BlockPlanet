import json,sys
for v in sys.argv[1:]:
    L=open(v).read().splitlines()
    maps=[json.loads(l) for l in L if '"research_columns"' in l and '"map"' in l]
    st=[json.loads(l[l.index('{'):]) for l in L if '"barrage_strike_mesh"' in l]
    if len(maps)<2 or len(st)<3: print(v,'missing'); continue
    a=[int(maps[0]['map'][2*i:2*i+2],16)>>4 for i in range(1024)]; b=[int(maps[-1]['map'][2*i:2*i+2],16)>>4 for i in range(1024)]
    out=[]
    for s in st[:3]:
        bx,by=s['board']; bx-=0.5; by-=0.5
        R={'x-':[0,0],'x+':[0,0],'y-':[0,0],'y+':[0,0]}
        for dx in range(-4,5):
            for dy in range(-4,5):
                if dx*dx+dy*dy>16: continue
                x=int(round(bx+dx))%32; y=int(round(by+dy))%32; i=y*32+x; h=b[i]<a[i]
                if dx<0: R['x-'][0]+=1; R['x-'][1]+=h
                if dx>0: R['x+'][0]+=1; R['x+'][1]+=h
                if dy<0: R['y-'][0]+=1; R['y-'][1]+=h
                if dy>0: R['y+'][0]+=1; R['y+'][1]+=h
        out.append(f"n{s['n']}({bx:.0f},{by:.0f}) "+' '.join(f"{k} {h}/{t}" for k,(t,h) in R.items()))
    print(v.split('/')[-2].ljust(24),' | '.join(out))
