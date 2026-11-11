import sys, struct

def rd16(b,o): return (b[o]<<8)|b[o+1]

def decode_link(b):
    # FF 5A | len16 | ctrl seq ack session | hdrcksum | payload | paycksum
    assert b[0]==0xFF and b[1]==0x5A, "not a link packet"
    ln=rd16(b,2)
    print(f"link: len={ln} (actual {len(b)}) ctrl={b[4]:02X} seq={b[5]:02X} ack={b[6]:02X} session={b[7]:02X} hdrck={b[8]:02X}")
    payload=b[9:ln-1]
    pa[0] if False else None
    return payload

IDN = {
 0x0000:"Name",0x0001:"ModelId",0x0002:"Manufacturer",0x0003:"SerialNum",0x0004:"FirmwareVer",
 0x0005:"HardwareVer",0x0006:"MessagesSentByAccessory",0x0007:"MessagesReceivedFromDevice",
 0x0008:"PowerProvidingCapability",0x0009:"MaximumCurrentDrawn",0x000A:"SupportedExternalAccessoryProtocol",
 0x000B:"AppMatchTeamID",0x000C:"CurrentLanguage",0x000D:"SupportedLanguage",0x000E:"SerialTransportComponent",
 0x000F:"USBDeviceTransportComponent",0x0010:"USBHostTransportComponent",0x0011:"BluetoothTransportComponent",
 0x0012:"iAP2HIDComponent",0x0013:"USBHIDComponent",0x0014:"WirelessCarPlayTransportComponent",
 0x0015:"VehicleStatusComponent",0x0016:"VehicleInformationComponent",0x0017:"LocationInformationComponent",
 0x0018:"BluetoothHIDComponent",0x0019:"BluetoothTransportComponentDeprecated",0x001A:"RouteGuidanceDisplayComponent?",
 0x001D:"TelephonyComponent?",0x001E:"RouteGuidanceDisplayComponent",0x001F:"?1F",
}

def decode_params(payload, indent="  "):
    # control msg: 40 40 | msglen16 | msgid16 | params
    if payload[0]==0x40 and payload[1]==0x40:
        mlen=rd16(payload,2); mid=rd16(payload,4)
        print(f"{indent}ctrl-msg len={mlen} msgid=0x{mid:04X}")
        off=6
    else:
        off=0
    while off+4<=len(payload):
        l=rd16(payload,off); i=rd16(payload,off+2)
        if l<4 or off+l>len(payload):
            print(f"{indent}!! bad TLV at {off}: len={l}"); break
        val=payload[off+4:off+l]
        name=IDN.get(i,f"0x{i:04X}")
        # render value
        if i in (0x0006,0x0007):
            ids=[f"0x{rd16(val,k):04X}" for k in range(0,len(val)-1,2)]
            print(f"{indent}[{i:04X} {name}] len={l} ids({len(ids)})={','.join(ids)}")
        elif i==0x001E or (l>40 and val[:2]==b'\x00\x06'):
            print(f"{indent}[{i:04X} {name}] len={l}  -- nested component:")
            decode_component(val, indent+"    ")
        else:
            txt=''.join(chr(c) if 32<=c<127 else '.' for c in val)
            hexs=' '.join(f'{c:02X}' for c in val[:24])
            print(f"{indent}[{i:04X} {name}] len={l} val='{txt[:40]}' hex[{hexs}]")
        off+=l

def decode_component(val, indent):
    off=0
    while off+4<=len(val):
        l=rd16(val,off); i=rd16(val,off+2)
        if l<4 or off+l>len(val):
            print(f"{indent}!! bad inner TLV at {off}: len={l}"); break
        v=val[off+4:off+l]
        if len(v)==2:
            print(f"{indent}inner[0x{i:04X}] len={l} u16=0x{rd16(v,0):04X}")
        else:
            txt=''.join(chr(c) if 32<=c<127 else '.' for c in v)
            print(f"{indent}inner[0x{i:04X}] len={l} val='{txt}'")
        off+=l

for path in sys.argv[1:]:
    b=open(path,'rb').read()
    print(f"\n===== {path} ({len(b)} bytes) =====")
    if b[0]==0xFF and b[1]==0x5A:
        ln=rd16(b,2); payload=b[9:ln-1]
        decode_link(b)
        decode_params(payload)
    elif b[0]==0x40 and b[1]==0x40:
        decode_params(b)
    else:
        print("unknown framing; first bytes:", ' '.join(f'{c:02X}' for c in b[:16]))
