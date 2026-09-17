"""Read-only device power telemetry, separate from firmware boot receipts."""
import threading,time

def validate_power(p):
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
    return power

class PowerTelemetry:
    def __init__(self):self.lock=threading.Lock();self.value=None
    def record(self, payload):
        power=validate_power(payload.get("power"))
        with self.lock:self.value={"power":power,"received_at":time.time()}
    def snapshot(self):
        with self.lock:return dict(self.value) if self.value else None
