import sys,subprocess,os
from PIL import Image
v=sys.argv[1]; a=float(sys.argv[2]); b=float(sys.argv[3]); n=int(sys.argv[4]); out=sys.argv[5]
crop=sys.argv[6] if len(sys.argv)>6 else '640:480:200:80'
ims=[]
for i in range(n):
    t=a+(b-a)*i/(n-1)
    f=f'/tmp/zz{i}.png'
    subprocess.run(['ffmpeg','-loglevel','error','-y','-ss',str(t),'-i',f'cv-{v}/rec.mp4','-frames:v','1','-vf',f'crop={crop},scale=480:360',f])
    ims.append(Image.open(f))
cols=min(n,4); rows=(n+cols-1)//cols
W=Image.new('RGB',(480*cols,360*rows))
for i,im in enumerate(ims): W.paste(im,((i%cols)*480,(i//cols)*360))
W.save(f'{out}.png')
