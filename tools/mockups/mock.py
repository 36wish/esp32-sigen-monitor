from PIL import Image, ImageDraw, ImageFont
S=2
W,H=480,320
img=Image.new("RGB",(W*S,H*S),"#0b1220")
d=ImageDraw.Draw(img)
F="C:/Windows/Fonts/"
def f(px,b=False): return ImageFont.truetype(F+("segoeuib.ttf" if b else "segoeui.ttf"),px*S)
def sym(px): return ImageFont.truetype(F+"seguisym.ttf",px*S)
MUTED="#8fa3c8"
def rr(x0,y0,x1,y1,r,fill,outline=None):
    d.rounded_rectangle([x0*S,y0*S,x1*S,y1*S],r*S,fill=fill,outline=outline,width=S)
def text(cx,y,t,font,col):
    d.text((cx*S,y*S),t,font=font,fill=col,anchor="mt")
def title(cx,y,ic,t):
    w1=d.textlength(ic,font=sym(14))/S; w2=d.textlength(" "+t,font=f(14))/S
    x0=cx-(w1+w2)/2
    d.text((x0*S,y*S),ic,font=sym(14),fill=MUTED,anchor="lt")
    d.text(((x0+w1)*S,y*S)," "+t,font=f(14),fill=MUTED,anchor="lt")

# top bar
d.text((10*S,17*S),"SIGENERGY MONITOR",font=f(16,True),fill="#eaf1fb",anchor="lm")
d.text((434*S,17*S),"192.168.1.42",font=f(14),fill=MUTED,anchor="rm")
cx,cy=464,21
for r in (4,8,12):
    d.arc([(cx-r)*S,(cy-r)*S,(cx+r)*S,(cy+r)*S],225,315,fill="#2ecc71",width=2*S)
d.ellipse([(cx-1.5)*S,(cy-1.5)*S,(cx+1.5)*S,(cy+1.5)*S],fill="#2ecc71")

top=38; bot=318
x=12
cards=[(x,130),(x+138,190),(x+138+198,130)]
for cx0,w in cards:
    rr(cx0,top+6,cx0+w,bot-6,14,"#141d2e","#22304a")

# solar (content vertically centred, as LVGL flex does)
c=cards[0][0]+65
title(c,top+96,"\u26A1","SOLAR")
text(c,top+122,"4.2 kW",f(24,True),"#f5a623")
# house
c=cards[1][0]+95
text(c,top+60,"HOUSE LOAD",f(14),MUTED)
text(c,top+82,"+2.4 kW",f(40,True),"#eaf1fb")
d.line([(c-85)*S,(top+150)*S,(c+85)*S,(top+150)*S],fill="#22304a",width=S)
text(c,top+162,"Grid: -1.3 kW (export)",f(16),"#2ecc71")
# battery
c=cards[2][0]+65
title(c,top+22,"\u25AE","BATTERY")
text(c,top+44,"76%",f(24,True),"#2ecc71")
bx0,by0,bx1,by1=c-11,top+84,c+11,top+184
rr(bx0,by0,bx1,by1,6,"#0d1420")
fy=by1-(by1-by0)*0.76
d.rounded_rectangle([bx0*S,fy*S,bx1*S,by1*S],6*S,fill="#2ecc71")
text(c,top+200,"\u2191 0.5 kW charging",f(14),"#2ecc71")
img.save("ui-mockup.png")

# scanning overlay variant
img2=Image.alpha_composite(img.convert("RGBA"),Image.new("RGBA",img.size,(11,18,32,230)))
d2=ImageDraw.Draw(img2)
d2.multiline_text((W*S//2,H*S//2),"Searching for Sigenergy plant...\n192.168.1.0/24  (96/254)",font=f(16),fill="#eaf1fb",anchor="mm",align="center")
img2.convert("RGB").save("ui-mockup-scanning.png")
