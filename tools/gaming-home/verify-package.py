"""Read-only MSIX payload/CMS-binding verification; never install the package."""
import base64,hashlib,json,pathlib,sys,zipfile,xml.etree.ElementTree as ET
from PIL import Image
from io import BytesIO
sha=lambda b:hashlib.sha256(b).hexdigest()
def verify(package, binding, source):
    checks=0
    def check(v,msg):
        nonlocal checks
        checks+=1
        if not v:raise ValueError(msg)
    with zipfile.ZipFile(package) as z:
        names=z.namelist()
        check(len(names)==len(set(names)), 'duplicate ZIP entry')
        check(all('..' not in pathlib.PurePosixPath(n).parts and not n.startswith('/') for n in names),'unsafe ZIP path')
        check(z.testzip() is None,'ZIP CRC failure')
        content=base64.b64decode(binding['signedContentBase64'])
        start=content.find(b'APPXAXPC')
        check(start>=0,'signed APPX digest structure missing')
        records=content[start+4:]
        check(len(records)==5*36,'signed APPX digest structure length')
        digests={records[i:i+4].decode('ascii'):records[i+4:i+36].hex() for i in range(0,len(records),36)}
        for tag,name in [('AXBM','AppxBlockMap.xml'),('AXCT','[Content_Types].xml'),('AXCI','AppxMetadata/CodeIntegrity.cat')]:
            check(digests[tag]==sha(z.read(name)),f'signed digest mismatch: {name}')
        blockmap=ET.fromstring(z.read('AppxBlockMap.xml'));ns={'b':'http://schemas.microsoft.com/appx/2010/blockmap'}
        payload=set()
        for row in blockmap.findall('b:File',ns):
            name=row.attrib['Name'].replace('\\','/');payload.add(name);data=z.read(name)
            check(len(data)==int(row.attrib['Size']),f'blockmap size mismatch {name}')
            blocks=row.findall('b:Block',ns)
            check(len(blocks)==max(1,(len(data)+65535)//65536),f'block count {name}')
            for i,b in enumerate(blocks):
                check(base64.b64decode(b.attrib['Hash']).hex()==sha(data[i*65536:(i+1)*65536]),f'blockmap hash mismatch {name}')
        expected={'YeManGamingHome.exe','AppxManifest.xml','CustomCapability.SCCD','Public/home.txt','Assets/Square150x150Logo.png','Assets/Square44x44Logo.png','Assets/StoreLogo.png'}
        check(payload==expected,'unexpected/missing payload file')
        check(set(names)==expected|{'AppxBlockMap.xml','[Content_Types].xml','AppxMetadata/CodeIntegrity.cat','AppxSignature.p7x'},'unbound extra ZIP file')
        for name in expected-{'YeManGamingHome.exe','AppxManifest.xml'}:
            check(z.read(name)==(source/name).read_bytes(),f'source bytes differ: {name}')
        for name,size in [('Assets/Square150x150Logo.png',150),('Assets/Square44x44Logo.png',44),('Assets/StoreLogo.png',50)]:
            im=Image.open(BytesIO(z.read(name)));check(im.size==(size,size),'logo dimensions')
        manifest=ET.fromstring(z.read('AppxManifest.xml'));n={'f':'http://schemas.microsoft.com/appx/manifest/foundation/windows10','u':'http://schemas.microsoft.com/appx/manifest/uap/windows10','u3':'http://schemas.microsoft.com/appx/manifest/uap/windows10/3','u4':'http://schemas.microsoft.com/appx/manifest/uap/windows10/4'}
        identity=manifest.find('f:Identity',n)
        check(identity.attrib['Name']=='YeManCC.GamingHome','package identity')
        check(identity.attrib['Publisher']==binding['subject']=='CN=YeManCC Gaming Home','publisher/signing subject')
        app=manifest.find('f:Applications/f:Application',n)
        check(app.attrib['Id']=='Home' and app.attrib['Executable']=='YeManGamingHome.exe','home entry')
        check(app.attrib['EntryPoint']=='Windows.FullTrustApplication','full trust application')
        ext=app.find('f:Extensions/u3:Extension/u3:AppExtension',n)
        check(ext.attrib['Name']=='windows.gamingApp' and ext.attrib['PublicFolder']=='Public','gaming extension')
        cap=manifest.find('f:Capabilities/u4:CustomCapability',n)
        check(cap.attrib['Name']=='Microsoft.appCategory.gamingHome_8wekyb3d8bbwe','gaming capability')
        sccd=ET.fromstring(z.read('CustomCapability.SCCD'))
        check(sccd.find('{http://schemas.microsoft.com/appx/2018/sccd}AuthorizedEntities').attrib['AllowAny']=='true','development SCCD')
        return {'status':'PASS','checks':checks,'msixSha256':sha(pathlib.Path(package).read_bytes()),'cmsSignatureCryptographic':True,'signedBlockMapBinding':True,'certificateTrustedByThisMachine':'not-required-or-changed','hardwareValidated':False,'packageInstalled':False,'applicationStarted':False}
if __name__=='__main__':
    package,binding,source,out=map(pathlib.Path,sys.argv[1:5]);result=verify(package,json.loads(binding.read_text(encoding='utf-8-sig')),source)
    pathlib.Path(out).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))
