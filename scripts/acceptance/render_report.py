import json,sys
from pathlib import Path
import pypdfium2 as pdfium
from PIL import Image,ImageDraw
m=json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"));log=Path(m["log_directory"])
doc=pdfium.PdfDocument(log/"small-generated.pdf")
images=[]
for index,page in enumerate(doc):
 im=page.render(scale=1.25).to_pil()
 path=log/f"report-page-{index+1:02}.png";im.save(path)
 im.thumbnail((330,465));images.append(im.copy())
for start in range(0,len(images),6):
 grid=Image.new("RGB",(1020,1000),"#ddd");draw=ImageDraw.Draw(grid)
 for j,im in enumerate(images[start:start+6]):
  x=(j%3)*340;y=(j//3)*500
  grid.paste(im,(x,y+25));draw.text((x+8,y+5),str(start+j+1),fill="black")
 grid.save(log/f"report-contact-{start//6+1}.png")
print("Rendered",len(doc),"pages")
