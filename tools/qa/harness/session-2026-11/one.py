import sys,subprocess,os
from PIL import Image
v=sys.argv[1]; a=float(sys.argv[2]); b=float(sys.argv[3]); n=int(sys.argv[4]); out=sys.argv[5]
d=f'o-{out}'; os.makedirs(d,exist_ok=True)
ims=[]
for i in range(n):
    t=a+(b-a)*i/(n-1)
    subprocess.run(['ffmpeg','-loglevel','error','-y','-ss',str(t),'-i',f'cv-{v}/rec.mp4','-frames:v','1','-vf','crop=900:720:190:0,scale=360:288',f'{d}/{i:02d}.png'])
    ims.append(Image.open(f'{d}/{i:02d}.png'))
cols=min(n,6); rows=(n+cols-1)//cols
W=Image.new('RGB',(360*cols,288*rows))
for i,im in enumerate(ims): W.paste(im,((i%cols)*360,(i//cols)*288))
W.save(f'{out}.png')
