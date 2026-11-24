import sys,subprocess,os
from PIL import Image
k=sys.argv[1]; a=float(sys.argv[2]); b=float(sys.argv[3]); n=int(sys.argv[4])
rows=[]
for m in ['ours','orig']:
    d=f'z-{k}-{m}'; os.makedirs(d,exist_ok=True)
    for i in range(n):
        t=a+(b-a)*i/(n-1)
        subprocess.run(['ffmpeg','-loglevel','error','-y','-ss',str(t),'-i',f'cv-an-{k}-{m}/rec.mp4','-frames:v','1','-vf','crop=900:720:190:0,scale=360:288',f'{d}/{i:02d}.png'])
    rows.append([Image.open(f'{d}/{i:02d}.png') for i in range(n)])
W=Image.new('RGB',(360*n,576))
for j,r in enumerate(rows):
    for i,im in enumerate(r): W.paste(im,(i*360,j*288))
W.save(f'z-{k}.png')
