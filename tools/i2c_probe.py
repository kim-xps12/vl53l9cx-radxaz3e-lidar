# SPDX-License-Identifier: MIT
import fcntl, ctypes, os, sys
I2C_RDWR=0x0707; I2C_M_RD=1
class msg(ctypes.Structure): _fields_=[("addr",ctypes.c_uint16),("flags",ctypes.c_uint16),("len",ctypes.c_uint16),("buf",ctypes.POINTER(ctypes.c_uint8))]
class rdwr(ctypes.Structure): _fields_=[("msgs",ctypes.POINTER(msg)),("nmsgs",ctypes.c_uint32)]
fd=os.open(sys.argv[1] if len(sys.argv)>1 else "/dev/i2c-2", os.O_RDWR)
def rd16(addr, reg, n):
    w=(ctypes.c_uint8*2)(reg>>8, reg&0xff); r=(ctypes.c_uint8*n)()
    m=(msg*2)(msg(addr,0,2,w), msg(addr,I2C_M_RD,n,r))
    fcntl.ioctl(fd, I2C_RDWR, rdwr(m,2)); return bytes(r)
def ack(a):
    r=(ctypes.c_uint8*1)(); m=(msg*1)(msg(a,I2C_M_RD,1,r))
    try: fcntl.ioctl(fd, I2C_RDWR, rdwr(m,1)); return True
    except OSError: return False
print("responding:", [hex(a) for a in range(0x08,0x78) if ack(a)])
try:
    print("MODEL_ID(0x0000..3):", rd16(0x29,0x0000,4).hex(" "), " ROM_REV(0x000A):", rd16(0x29,0x000A,2).hex(" "))
    f=rd16(0x29,0x008C,1)[0]
    print("FSM(0x008C) =", f, {0:"NONE",1:"READY_TO_BOOT",2:"STANDBY",3:"STREAMING"}.get(f,"?"))
except OSError as e:
    print("0x29 read failed:", e)
