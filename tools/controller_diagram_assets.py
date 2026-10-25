from pathlib import Path
from PIL import Image, ImageDraw
import base64, io, json, shutil

repo = Path(__file__).resolve().parents[1]
work = repo / 'public/controller-diagrams'
dest = repo / 'public/controller-diagrams'
dest.mkdir(parents=True, exist_ok=True)
models = {}

def uri(image):
    buf = io.BytesIO()
    image.save(buf, format='PNG', optimize=True)
    return 'data:image/png;base64,' + base64.b64encode(buf.getvalue()).decode('ascii')

def model(name, image_name):
    image = Image.open(work / image_name).convert('RGB')
    info = {'width': image.width, 'height': image.height, 'image': '/controller-diagrams/' + name + '.webp', 'controls': []}
    image.save(dest / (name + '.webp'), lossless=True, quality=100, method=6)
    models[name] = info
    return image, info

def control(image, info, code, label, box, index=None, back=None, aliases=None, stick=None, thumb=None, polygon=None, exclude=None):
    x, y, width, height = [round(value) for value in box]
    x = max(0, x); y = max(0, y)
    width = min(width, image.width-x); height = min(height, image.height-y)
    region = image.crop((x, y, x+width, y+height))
    alpha = region.convert('L').point(lambda value: min(255, max(0, (value-65)*2)))
    if polygon:
        allowed = Image.new('L', region.size, 0)
        ImageDraw.Draw(allowed).polygon([(round(px-x), round(py-y)) for px,py in polygon], fill=255)
        import PIL.ImageChops
        alpha = PIL.ImageChops.multiply(alpha, allowed)
    if exclude:
        draw = ImageDraw.Draw(alpha)
        for left, top, right, bottom in exclude:
            draw.rectangle((left-x, top-y, right-x, bottom-y), fill=0)
    mask = Image.new('RGBA', region.size, (255,255,255,0)); mask.putalpha(alpha)
    entry = {'code':code,'label':label,'box':[x,y,width,height],'mask':uri(mask)}
    if polygon:
        entry['hitClip'] = 'polygon(' + ','.join(f'{(px-x)/width*100:.3f}% {(py-y)/height*100:.3f}%' for px,py in polygon) + ')'
    if index is not None: entry['buttonIndex']=index
    if back is not None: entry['backIndex']=back
    if aliases: entry['aliases']=aliases
    if stick is not None:
        cx,cy,radius,travel = thumb
        cx,cy,radius = round(cx),round(cy),round(radius)
        cap = image.crop((cx-radius,cy-radius,cx+radius,cy+radius)).convert('RGBA')
        clip = Image.new('L',cap.size,0); ImageDraw.Draw(clip).ellipse((0,0,cap.width-1,cap.height-1),fill=255)
        cap.putalpha(clip)
        entry['stick']=stick
        entry['thumb']={'x':cx-radius,'y':cy-radius,'size':radius*2,'travel':travel,'image':uri(cap)}
    info['controls'].append(entry)

image,deck = model('steamdeck','steamdeck.webp')
sx,sy = image.width/2048,805/781
def deck_box(x,y,w,h): return (x*sx,y*sy-87,w*sx,h*sy)
def dc(code,label,box,index=None,back=None,aliases=None,thumb=None,exclude=None,polygon=None):
    transformed_thumb = None if thumb is None else (thumb[0]*sx,thumb[1]*sy-87,thumb[2]*sx,thumb[3]*sx)
    excluded = None if exclude is None else [(l*sx,t*sy-87,r*sx,b*sy-87) for l,t,r,b in exclude]
    transformed_polygon = None if polygon is None else [(x*sx,y*sy-87) for x,y in polygon]
    control(image,deck,code,label,deck_box(*box),index,back,aliases,0 if code=='leftThumb' else 1 if code=='rightThumb' else None,transformed_thumb,polygon=transformed_polygon,exclude=excluded)
for code,label,cx,cy,index in [('a','A',1884,344,0),('b','B',1943,302,1),('x','X',1820,302,2),('y','Y',1884,261,3)]:
    dc(code,label,(cx-31,cy-24,62,48),index)
for code,label,box,index in [('dpadUp','方向上',(124,251,52,39),12),('dpadDown','方向下',(124,320,52,39),13),('dpadLeft','方向左',(78,282,51,43),14),('dpadRight','方向右',(172,282,51,43),15),('back','Back / View',(225,229,71,25),8),('start','Start / Menu',(1740,229,71,25),9)]:
    dc(code,label,box,index)
lt_poly=[(55,202),(62,153),(100,111),(149,93),(176,96),(224,124),(184,133),(120,155),(73,180)]
lb_poly=[(55,202),(72,180),(121,154),(184,133),(227,124),(264,133),(330,134),(342,142),(341,162),(329,176),(157,178),(108,186),(68,207),(55,210)]
dc('lt','LT',(54,90,173,115),6,polygon=lt_poly)
dc('rt','RT',(1809,90,173,115),7,polygon=[(2036-x,y) for x,y in lt_poly])
dc('lb','LB',(54,122,292,91),4,polygon=lb_poly)
dc('rb','RB',(1690,122,292,91),5,polygon=[(2036-x,y) for x,y in lb_poly])
dc('leftThumb','左摇杆 / L3',(249,262,182,150),10,thumb=(340,376,28,23))
dc('rightThumb','右摇杆 / R3',(1607,262,182,150),11,thumb=(1695,376,28,23))
dc('steam','Steam',(317,604,117,40),16,aliases=['steam','STEAM','guide'])
dc('quick','快捷菜单',(1602,604,117,40),aliases=['quick','quickAccess','qs'])
dc('L4','L4 左上背键',(449,449,139,101),back=1,aliases=['L4','l4'],exclude=[(513,447,521,551)])
dc('R4','R4 右上背键',(1453,449,139,101),back=2,aliases=['R4','r4'],exclude=[(1513,447,1522,551)])
dc('L5','L5 左下背键',(449,557,139,101),back=3,aliases=['L5','l5'],exclude=[(513,555,521,659)])
dc('R5','R5 右下背键',(1453,557,139,101),back=4,aliases=['R5','r5'],exclude=[(1513,555,1522,659)])
dc('leftTouchpad','左触摸板',(208,423,226,163),aliases=['leftTouchpad'])
dc('rightTouchpad','右触摸板',(1603,423,226,163),aliases=['rightTouchpad'])

image,ps = model('ps5','ps5.webp')
def pc(code,label,box,index=None,aliases=None,thumb=None):
    transformed_thumb = None if thumb is None else (thumb[0],thumb[1]-42,thumb[2],thumb[3])
    control(image,ps,code,label,(box[0],box[1]-42,box[2],box[3]),index,aliases=aliases,stick=0 if code=='leftThumb' else 1 if code=='rightThumb' else None,thumb=transformed_thumb)
for code,label,cx,cy,index in [('a','×',569,203,0),('b','○',612,160,1),('x','□',526,160,2),('y','△',569,116,3)]:
    pc(code,label,(cx-23,cy-23,46,46),index)
for code,label,box,index in [('dpadUp','方向上',(140,100,45,51),12),('dpadDown','方向下',(140,164,45,51),13),('dpadLeft','方向左',(104,137,52,43),14),('dpadRight','方向右',(168,137,51,43),15),('back','创建 / Back',(201,69,23,37),8),('start','选项 / Start',(500,69,23,37),9),('lb','L1',(122,48,96,18),4),('rb','R1',(517,48,88,18),5),('lt','L2',(123,42,84,8),6),('rt','R2',(520,42,82,8),7)]:
    pc(code,label,box,index)
pc('leftThumb','左摇杆 / L3',(213,201,93,94),10,thumb=(258,248,24,13))
pc('rightThumb','右摇杆 / R3',(423,201,93,94),11,thumb=(470,248,24,13))
pc('guide','PS',(340,226,50,52),16,aliases=['ps','PS','guide'])
pc('touchpad','触摸板',(234,48,260,136),17,aliases=['touchpad'])
pc('microphone','麦克风',(346,280,36,12),aliases=['microphone','mic'])

image,xbox=model('xbox','xbox.webp')
def xc(code,label,box,index=None,thumb=None,back=None):
    scale_x,scale_y = image.width/420,image.height/282
    x,y,w,h = box
    transformed_thumb = None if thumb is None else (thumb[0]*scale_x,(thumb[1]-48)*scale_y,thumb[2]*scale_x,thumb[3]*scale_x)
    control(image,xbox,code,label,(x*scale_x,(y-48)*scale_y,w*scale_x,h*scale_y),index,back,stick=0 if code=='leftThumb' else 1 if code=='rightThumb' else None,thumb=transformed_thumb)
for code,label,cx,cy,index in [('a','A',293,168,0),('b','B',313,148,1),('x','X',273,148,2),('y','Y',293,130,3)]: xc(code,label,(cx-12,cy-12,24,24),index)
for code,label,box,index in [('back','Back',(176,138,18,18),8),('start','Start',(222,138,18,18),9),('lb','LB',(89,77,73,31),4),('rb','RB',(258,77,73,31),5),('lt','LT',(109,62,36,22),6),('rt','RT',(275,62,36,22),7),('dpadUp','方向上',(160,174,20,18),12),('dpadDown','方向下',(160,202,20,21),13),('dpadLeft','方向左',(144,186,21,21),14),('dpadRight','方向右',(175,186,21,21),15)]: xc(code,label,box,index)
xc('leftThumb','左摇杆 / L3',(98,114,47,47),10,thumb=(121,138,14,8))
xc('rightThumb','右摇杆 / R3',(226,172,47,47),11,thumb=(249,195,14,8))
xc('guide','Xbox',(194,101,31,32),16)
for code,label,box,back in [('M1','背部1',(69,214,47,31),1),('M2','背部2',(306,214,47,31),2),('M3','背部3',(60,248,46,32),3),('M4','背部4',(316,248,46,32),4)]: xc(code,label,box,back=back)

target=repo/'src/bridge/controllerDiagramData.json'
target.write_text(json.dumps(models,separators=(',',':')),encoding='utf8')
print(json.dumps({key:len(value['controls']) for key,value in models.items()}))
print(target, target.stat().st_size)

