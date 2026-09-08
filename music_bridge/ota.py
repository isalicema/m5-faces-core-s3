"""Authenticated Faces pull OTA, adapted from StopWatch's immutable catalog."""
import hashlib,json,os,re,struct,tempfile,threading,time
from dataclasses import dataclass
from pathlib import Path

TARGET='faces-suite-cores3-v1'
MAX_SIZE=0x640000
MARKER=b'FACES_SUITE_OTA_V1'
SHA=re.compile(r'^[0-9a-f]{64}$')
MAC=re.compile(r'^[0-9a-f]{2}(?::[0-9a-f]{2}){5}$',re.I)

def device_mac(value):
    if not isinstance(value,str) or not MAC.fullmatch(value):raise ValueError('Expected a colon-separated device MAC address')
    return value.lower()

def validate_image(data):
    if not 288<=len(data)<=MAX_SIZE:raise ValueError('Firmware does not fit Faces OTA slot')
    if data[0]!=0xe9 or struct.unpack_from('<H',data,12)[0]!=9:raise ValueError('Expected ESP32-S3 application')
    if struct.unpack_from('<I',data,32)[0]!=0xabcd5432:raise ValueError('Expected app image, not bootloader or merged flash')
    if MARKER not in data:raise ValueError('Expected OTA-capable Faces Suite firmware')

def atomic_write(path,data):
    fd,name=tempfile.mkstemp(prefix=path.name+'.',dir=path.parent)
    try:
        with os.fdopen(fd,'wb') as out:out.write(data);out.flush();os.fsync(out.fileno())
        os.replace(name,path)
    finally:
        if os.path.exists(name):os.unlink(name)

def publish(firmware,directory,mac):
    data=Path(firmware).read_bytes();validate_image(data)
    digest=hashlib.sha256(data).hexdigest();directory=Path(directory);mac=device_mac(mac)
    directory.mkdir(parents=True,exist_ok=True)
    name='firmware-'+digest+'.bin';dest=directory/name
    if not dest.is_file() or dest.read_bytes()!=data:atomic_write(dest,data)
    manifest={'schema':1,'target':TARGET,'device_mac':mac,'release_id':digest,'sha256':digest,'size':len(data),'filename':name,'elf_sha256':data[176:208].hex()}
    atomic_write(directory/'current.json',(json.dumps(manifest,indent=2)+'\n').encode())
    return manifest

@dataclass(frozen=True)
class Release:
    sha256:str
    data:bytes
    device_mac:str
    def manifest(self):
        return {'schema':1,'target':TARGET,'device_mac':self.device_mac,'release_id':self.sha256,'sha256':self.sha256,'size':len(self.data),'download_path':'/api/ota/firmware/'+self.sha256,'elf_sha256':self.data[176:208].hex()}

class FirmwareCatalog:
    def __init__(self,directory):
        self.directory=Path(directory);self.lock=threading.Lock();self.key=None;self.release=None;self.report=None
    def current(self):
        with self.lock:
            try:
                m=json.loads((self.directory/'current.json').read_text())
                digest=m.get('sha256','');size=m.get('size');mac=device_mac(m.get('device_mac'))
                if m.get('schema')!=1 or m.get('target')!=TARGET:return None
                if not isinstance(digest,str) or not SHA.fullmatch(digest) or m.get('release_id')!=digest:return None
                if type(size)!=int or not 288<=size<=MAX_SIZE or m.get('filename')!='firmware-'+digest+'.bin':return None
                path=self.directory/m['filename'];st=path.stat()
                if not path.is_file() or st.st_size!=size:return None
                key=(digest,mac,st.st_size,st.st_mtime_ns,st.st_ctime_ns,st.st_ino)
                if key!=self.key:
                    data=path.read_bytes();validate_image(data)
                    if len(data)!=size or hashlib.sha256(data).hexdigest()!=digest:return None
                    self.release=Release(digest,data,mac);self.key=key
                return self.release
            except (OSError,ValueError,TypeError,AttributeError,struct.error):return None
    def resolve(self,digest):
        current=self.current()
        return current if current and current.sha256==digest else None

    def record_report(self,value):
        release=self.current()
        if not isinstance(value,dict) or not release or value.get('target')!=TARGET or device_mac(value.get('device_mac'))!=release.device_mac:raise ValueError('Unexpected device')
        sha=value.get('elf_sha256','')
        if not isinstance(sha,str) or not SHA.fullmatch(sha) or value.get('partition') not in (0x10000,0x650000):raise ValueError('Invalid firmware report')
        if value.get('event') not in ('ready','installed','failed'):raise ValueError('Invalid report event')
        report={k:value[k] for k in ('target','device_mac','elf_sha256','partition','event')}
        if 'power' in value:
            p=value['power']
            if not isinstance(p,dict) or type(p.get('valid')) is not bool:raise ValueError('Invalid power report')
            power={'valid':p['valid']}
            for key in ('battery_present','usb_present','charging'):
                v=p.get(key)
                if v is not None and type(v) is not bool:raise ValueError('Invalid power state')
                power[key]=v
            for key,low,high in (('percent',0,100),('battery_mv',2500,4500),('vbus_mv',0,6000)):
                v=p.get(key)
                if v is not None and (type(v) is not int or not low<=v<=high):raise ValueError('Invalid power measurement')
                power[key]=v
            if not power['valid']:
                power.update({key:None for key in power if key!='valid'})
            elif not power['battery_present']:
                power.update(percent=None,battery_mv=None,charging=False)
            report['power']=power
        report['received_at']=time.time()
        with self.lock:self.report=report
    def status(self):
        with self.lock:return dict(self.report) if self.report else None
