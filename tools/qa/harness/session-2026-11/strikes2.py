import json,sys,math
for v in sys.argv[1:]:
    L=open(v).read().splitlines()
    maps=[json.loads(l) for l in L if '"research_columns"' in l and '"map"' in l]
    st=[json.loads(l[l.index('{'):]) for l in L if '"barrage_strike_mesh"' in l]
    if len(maps)<2 or len(st)<3: print(v,'missing',len(maps),len(st)); continue
    a=[int(maps[0]['map'][2*i:2*i+2],16)>>4 for i in range(1024)]; b=[int(maps[-1]['map'][2*i:2*i+2],16)>>4 for i in range(1024)]
    out=[]
    for s in st[:3]:
        bx,by=s['board']; bx-=0.5; by-=0.5  # blast centre cell
        tot=hit=0; sides={'x-':[0,0],'x+':[0,0]}
        for dx in range(-3,4):
            for dy in range(-3,4):
                if dx*dx+dy*dy>9: continue
                x=int(round(bx+dx))%32; y=int(round(by+dy))%32
                tot+=1; h=b[x*32+y]<a[x*32+y]; hit+=h
                if dx<0: sides['x-'][0]+=1; sides['x-'][1]+=h
                if dx>0: sides['x+'][0]+=1; sides['x+'][1]+=h
        out.append(f"n{s['n']} at {s['board']} {hit}/{tot} (x- {sides['x-'][1]}/{sides['x-'][0]}, x+ {sides['x+'][1]}/{sides['x+'][0]})")
    print(v.split('/')[-2], maps[0]['cursor'], ' | '.join(out))
