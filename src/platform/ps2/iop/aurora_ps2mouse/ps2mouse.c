/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
*/

/**
 * @file
 * USB Mouse Driver for PS2
 */

#include "types.h"
#include "iomanX.h"
#include "loadcore.h"
#include "stdio.h"
#include "sifcmd.h"
#include "sifrpc.h"
#include "sysclib.h"
#include "sysmem.h"
#include "usbd.h"
#include "usbd_macro.h"
#include "thbase.h"
#include "thevent.h"
#include "thsemap.h"

#include "ps2mouse.h"

#define MODNAME "Aurora composite USB mouse driver"

IRX_ID(MODNAME, 1, 2);

#define PS2MOUSE_VERSION 0x100

#define USB_SUBCLASS_BOOT 1
#define USB_HIDPROTO_MOUSE 2

#define PS2MOUSE_MAXDEV 2
#define PS2MOUSE_MAXBUTTONS 3

/* AURORA_COMPOSITE_MOUSE_V1
 * Private diagnostics RPC; stock commands/ABI stay unchanged. */
#define PS2MOUSE_AURORA_STATUS 0x21
#define AURORA_MOUSE_STATUS_DRIVER     0x01U
#define AURORA_MOUSE_STATUS_HID_SEEN   0x02U
#define AURORA_MOUSE_STATUS_BOOT_SEEN  0x04U
#define AURORA_MOUSE_STATUS_CONNECTED  0x08U
#define AURORA_MOUSE_STATUS_BOOT_PROTO 0x10U

#define PS2MOUSE_DEFDBLCLICK 500

#define PS2MOUSE_DEFACCEL (1 << 16)
#define PS2MOUSE_DEFTHRES 65536;

static SifRpcDataQueue_t ps2mouse_queue __attribute__((aligned(16)));
static SifRpcServerData_t ps2mouse_server __attribute__((aligned(16)));
static int _rpc_buffer[512]  __attribute__((__aligned__(4)));

#define ABS(x) (x < 0 ? -x : x)

void rpcMainThread(void* param);
void *rpcCommandHandler(u32 command, void *buffer, int size);

int ps2mouse_init();
void ps2mouse_config_set(int resultCode, int bytes, void *arg);
void ps2mouse_protocol_set(int resultCode, int bytes, void *arg);
void ps2mouse_data_recv(int resultCode, int bytes, void *arg);
/* AURORA_COMPOSITE_MOUSE_V1
 * Stock PS2SDK assumes the first configuration interface is the mouse.
 * 2.4 GHz receivers are frequently composite. Walk the whole configuration
 * descriptor and select an actual HID Boot Mouse interrupt-IN interface. */
extern u32 aurora_status;
static int ps2mouse_find_boot_mouse(UsbConfigDescriptor *conf,
                                    UsbInterfaceDescriptor **ppIntf,
                                    UsbEndpointDescriptor **ppEndp)
{
  u8 *p;
  u8 *end;

  if(!conf || conf->bLength < 2 || conf->wTotalLength < conf->bLength)
    return 0;

  p = (u8 *)conf + conf->bLength;
  end = (u8 *)conf + conf->wTotalLength;

  while((p + 2) <= end)
    {
      u8 len = p[0];
      u8 type = p[1];

      if(len < 2 || (p + len) > end)
        break;

      if(type == USB_DT_INTERFACE && len >= sizeof(UsbInterfaceDescriptor))
        {
          UsbInterfaceDescriptor *intf = (UsbInterfaceDescriptor *)p;

          if(intf->bInterfaceClass == USB_CLASS_HID)
            {
              aurora_status |= AURORA_MOUSE_STATUS_HID_SEEN;
              printf("AURORA MOUSE: HID if=%u alt=%u sub=%u proto=%u eps=%u\n",
                     intf->bInterfaceNumber, intf->bAlternateSetting,
                     intf->bInterfaceSubClass, intf->bInterfaceProtocol,
                     intf->bNumEndpoints);
            }

          if(intf->bAlternateSetting == 0 &&
             intf->bInterfaceClass == USB_CLASS_HID &&
             intf->bInterfaceSubClass == USB_SUBCLASS_BOOT &&
             intf->bInterfaceProtocol == USB_HIDPROTO_MOUSE &&
             intf->bNumEndpoints >= 1)
            {
              u8 *q = p + len;
              aurora_status |= AURORA_MOUSE_STATUS_BOOT_SEEN;

              while((q + 2) <= end)
                {
                  u8 qlen = q[0];
                  u8 qtype = q[1];

                  if(qlen < 2 || (q + qlen) > end)
                    break;
                  if(qtype == USB_DT_INTERFACE)
                    break;

                  if(qtype == USB_DT_ENDPOINT &&
                     qlen >= sizeof(UsbEndpointDescriptor))
                    {
                      UsbEndpointDescriptor *ep = (UsbEndpointDescriptor *)q;
                      if(((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
                          USB_ENDPOINT_XFER_INT) &&
                         ((ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK) ==
                          USB_DIR_IN))
                        {
                          *ppIntf = intf;
                          *ppEndp = ep;
                          return 1;
                        }
                    }
                  q += qlen;
                }
            }
        }

      p += len;
    }

  return 0;
}

int ps2mouse_probe(int devId);
int ps2mouse_connect(int devId);
int ps2mouse_disconnect(int devId);
void usb_getstring(int endp, int index, char *desc);

typedef struct _mouse_data_recv

{
  unsigned char buttons;
  char x, y, wheel;
} mouse_data_recv;

typedef struct _mouse_dev

{
  int configEndp;
  int dataEndp;
  int packetSize;
  int devId;
  int interfaceNumber;
  /** Holds the data for the transfers */
  mouse_data_recv data;
  /** Array to hold timers for double click */
  u32 timer[PS2MOUSE_MAXBUTTONS];
} mouse_dev;

/* Global Variables */

int mouse_readmode;
int mousex_min;
int mousex_max;
int mousey_min;
int mousey_max;
int mouse_thres;
int mouse_accel;
int mouse_dblclicktime;
int mouse_sema;
/** Holds the current mouse information */
mouse_data mouse;
/** Holds a list of current devices */
mouse_dev *devices[PS2MOUSE_MAXDEV];
int dev_count;
u32 aurora_status;
sceUsbdLddOps mouse_driver = { NULL, NULL, "PS2Mouse", ps2mouse_probe, ps2mouse_connect, ps2mouse_disconnect, 0, 0, 0, 0, 0, NULL };

int _start (int argc, char *argv[])
{
  iop_thread_t param;
  int th;

  (void)argc;
  (void)argv;

  ps2mouse_init();

  param.attr         = TH_C;
  param.thread     = rpcMainThread;
  param.priority 	 = 40;
  param.stacksize    = 0x800;
  param.option      = 0;

  th = CreateThread(&param);
  if (th > 0) {
	StartThread(th, NULL);
	return MODULE_RESIDENT_END;
  }
  else return MODULE_NO_RESIDENT_END;
}

void rpcMainThread(void* param)
{
  int tid;

  (void)param;

  sceSifInitRpc(0);

  //printf("PS2MOUSE - RPC Initialise\n");
  printf("PS2MOUSE - USB Mouse Library\n");

  tid = GetThreadId();
  sceSifSetRpcQueue(&ps2mouse_queue, tid);
  sceSifRegisterRpc(&ps2mouse_server, PS2MOUSE_BIND_RPC_ID, (void *) rpcCommandHandler, (u8 *) &_rpc_buffer, 0, 0, &ps2mouse_queue);
  sceSifRpcLoop(&ps2mouse_queue);
}

int ps2mouse_probe(int devId)
{
  UsbDeviceDescriptor *dev;
  UsbConfigDescriptor *conf;
  UsbInterfaceDescriptor *intf = NULL;
  UsbEndpointDescriptor *endp = NULL;

  if(dev_count >= PS2MOUSE_MAXDEV)
    return 0;

  dev = sceUsbdScanStaticDescriptor(devId, NULL, USB_DT_DEVICE);
  if(!dev || dev->bNumConfigurations < 1)
    return 0;

  conf = sceUsbdScanStaticDescriptor(devId, dev, USB_DT_CONFIG);
  if(!conf)
    return 0;

  if(!ps2mouse_find_boot_mouse(conf, &intf, &endp))
    return 0;

  printf("AURORA MOUSE: Boot Mouse found on interface %u endpoint 0x%02x\n",
         intf->bInterfaceNumber, endp->bEndpointAddress);
  return 1;
}

int ps2mouse_connect(int devId)
{
  UsbDeviceDescriptor *dev;
  UsbConfigDescriptor *conf;
  UsbInterfaceDescriptor *intf = NULL;
  UsbEndpointDescriptor *endp = NULL;
  mouse_dev *currDev;
  int devLoop;

  dev = sceUsbdScanStaticDescriptor(devId, NULL, USB_DT_DEVICE);
  if(!dev)
    return 1;

  conf = sceUsbdScanStaticDescriptor(devId, dev, USB_DT_CONFIG);
  if(!conf || !ps2mouse_find_boot_mouse(conf, &intf, &endp))
    return 1;

  currDev = (mouse_dev *)AllocSysMemory(0, sizeof(mouse_dev), NULL);
  if(!currDev)
    return 1;
  memset(currDev, 0, sizeof(mouse_dev));

  currDev->configEndp = sceUsbdOpenPipe(devId, NULL);
  currDev->dataEndp = sceUsbdOpenPipe(devId, endp);
  if(currDev->configEndp < 0 || currDev->dataEndp < 0)
    {
      FreeSysMemory(currDev);
      return 1;
    }

  currDev->packetSize =
      endp->wMaxPacketSizeLB | ((int)endp->wMaxPacketSizeHB << 8);
  if(currDev->packetSize <= 0)
    {
      FreeSysMemory(currDev);
      return 1;
    }
  if((unsigned int)currDev->packetSize > sizeof(mouse_data_recv))
    currDev->packetSize = sizeof(mouse_data_recv);

  currDev->devId = devId;
  currDev->interfaceNumber = intf->bInterfaceNumber;

  if(dev->iManufacturer)
    usb_getstring(currDev->configEndp, dev->iManufacturer, "Mouse Manufacturer");
  if(dev->iProduct)
    usb_getstring(currDev->configEndp, dev->iProduct, "Mouse Product");

  for(devLoop = 0; devLoop < PS2MOUSE_MAXDEV; devLoop++)
    {
      if(devices[devLoop] == NULL)
        {
          devices[devLoop] = currDev;
          break;
        }
    }

  if(devLoop == PS2MOUSE_MAXDEV)
    {
      FreeSysMemory(currDev);
      return 1;
    }

  sceUsbdSetPrivateData(devId, currDev);

  if(sceUsbdSetConfiguration(currDev->configEndp,
                             conf->bConfigurationValue,
                             ps2mouse_config_set, currDev) != USB_RC_OK)
    {
      devices[devLoop] = NULL;
      FreeSysMemory(currDev);
      return 1;
    }

  dev_count++;
  aurora_status |= AURORA_MOUSE_STATUS_CONNECTED;
  printf("AURORA MOUSE: Connected interface %u, packet=%d\n",
         currDev->interfaceNumber, currDev->packetSize);
  return 0;
}

int ps2mouse_disconnect(int devId)

{
  int devLoop;
  //printf("PS2Mouse_disconnect devId %d\n", devId);

  for(devLoop = 0; devLoop < PS2MOUSE_MAXDEV; devLoop++)
    {
      if((devices[devLoop]) && (devices[devLoop]->devId == devId))
	{
	  dev_count--;
	  if(dev_count <= 0)
	    aurora_status &= ~AURORA_MOUSE_STATUS_CONNECTED;
	  FreeSysMemory(devices[devLoop]);
	  devices[devLoop] = NULL;
	  printf("PS2MOUSE: Disconnected device\n");
	  break;
	}
    }

  return 0;
}


typedef struct _string_descriptor

{
  u8 buf[200];
  char *desc;
} string_descriptor;

void ps2mouse_getstring_set(int resultCode, int bytes, void *arg)

{
  UsbStringDescriptor *str = (UsbStringDescriptor *) arg;
  string_descriptor *strBuf = (string_descriptor *) arg;

/*   printf("=========getstring=========\n"); */

/*   printf("PS2MOUSE: GET_DESCRIPTOR res %d, bytes %d, arg %p\n", resultCode, bytes, arg); */

  if(resultCode == USB_RC_OK)
    {
      char string[50];
      int strLoop;

      memset(string, 0, 50);
      for(strLoop = 0; strLoop < ((bytes - 2) / 2); strLoop++)
	{
	  string[strLoop] = str->wData[strLoop] & 0xFF;
	}
      printf("%s: %s\n", strBuf->desc, string);
    }

  FreeSysMemory(arg);
}

void usb_getstring(int endp, int index, char *desc)

{
  u8 *data;
  string_descriptor *str;

  data = (u8 *) AllocSysMemory(0, sizeof(string_descriptor), NULL);
  str = (string_descriptor *) data;

  if(data != NULL)
    {
      int ret;

      str->desc = desc;
      ret = sceUsbdControlTransfer(endp, 0x80, USB_REQ_GET_DESCRIPTOR, (USB_DT_STRING << 8) | index,
			       0, sizeof(string_descriptor) - 4, data, ps2mouse_getstring_set, data);
      if(ret != USB_RC_OK)
	{
	  printf("PS2MOUSE: Error sending string descriptor request\n");
	  FreeSysMemory(data);
	}
    }
}

void ps2mouse_protocol_set(int resultCode, int bytes, void *arg)
{
  mouse_dev *dev = (mouse_dev *)arg;
  (void)bytes;

  if(dev == NULL)
    return;

  if(resultCode == USB_RC_OK)
    {
      aurora_status |= AURORA_MOUSE_STATUS_BOOT_PROTO;
      printf("AURORA MOUSE: HID Boot Protocol active on interface %u\n",
             dev->interfaceNumber);
    }
  else
    {
      /* Fail-soft: many Boot Mouse receivers already emit the short packet. */
      printf("AURORA MOUSE: SET_PROTOCOL failed (%d), trying input anyway\n",
             resultCode);
    }

  sceUsbdInterruptTransfer(dev->dataEndp, &dev->data, dev->packetSize,
                           ps2mouse_data_recv, dev);
}

void ps2mouse_config_set(int resultCode, int bytes, void *arg)
{
  mouse_dev *dev = (mouse_dev *)arg;
  int ret;
  (void)bytes;

  if(resultCode != USB_RC_OK || dev == NULL)
    {
      printf("AURORA MOUSE: configuration failed (%d)\n", resultCode);
      return;
    }

  /* HID SET_PROTOCOL(BOOT), addressed to the selected mouse interface. */
  ret = sceUsbdControlTransfer(
      dev->configEndp,
      USB_TYPE_CLASS | USB_RECIP_INTERFACE,
      USB_REQ_SET_PROTOCOL,
      0,
      dev->interfaceNumber,
      0,
      NULL,
      ps2mouse_protocol_set,
      dev);

  if(ret != USB_RC_OK)
    ps2mouse_protocol_set(ret, 0, dev);
}

void ps2mouse_data_recv(int resultCode, int bytes, void *arg)

{
  mouse_dev *dev;

  if((resultCode != USB_RC_OK) && (resultCode != USB_RC_DATAOVER))
    {
      printf("PS2MOUSE: Data Recv set res %d, bytes %d, arg %p\n", resultCode, bytes, arg);
      return;
    }

  //printf("PS2MOUSE: Data Recv set res %d, bytes %d, arg %p\n", resultCode, bytes, arg);

  dev = (mouse_dev *) arg;
  if(dev != NULL)
    {
      int buttonLoop;
      int buttonData;
      int mx, my;

      WaitSema(mouse_sema);

      mx = dev->data.x;
      my = dev->data.y;

      if(ABS(mx) >= mouse_thres)
	{
	  mx = (mx * mouse_accel) >> 16;
	}

      if(ABS(my) >= mouse_thres)
	{
	  my = (my * mouse_accel) >> 16;
	}

/*       if(mx > mouse_thres) mx = mouse_thres; */
/*       else if(mx < (-mouse_thres)) mx = -mouse_thres; */

/*       if(my > mouse_thres) my = mouse_thres; */
/*       else if(my < (-mouse_thres)) my = -mouse_thres; */

      mouse.x += mx;
      mouse.y += my;
      mouse.wheel += dev->data.wheel;
      buttonData = 0;
      for(buttonLoop = 0; buttonLoop < PS2MOUSE_MAXBUTTONS; buttonLoop++)
	{
	  int currButton = 1 << buttonLoop;

	  if( ((mouse.buttons & currButton) == 0) && (dev->data.buttons & currButton))
	    {
	      iop_sys_clock_t t;
	      int usec, sec;
	      int msec;

	      GetSystemTime(&t);
	      SysClock2USec(&t, (u32 *)&sec, (u32 *)&usec);
	      msec = (sec * 1000) + (usec / 1000);
	      //printf("%d %d %d\n", msec, sec, usec);

	      if(dev->timer[buttonLoop])
		{
		  if((msec - dev->timer[buttonLoop]) < (u32)mouse_dblclicktime)
		    {
		      //printf("Double click\n");
		      buttonData |= (1 << (buttonLoop + 8));
		      dev->timer[buttonLoop] = 0;
		    }
		  else
		    {
		      dev->timer[buttonLoop] = msec;
		    }
		}
	      else /* If not set a timer */
		{
		  dev->timer[buttonLoop] = msec;
		}
	    }
	}
      mouse.buttons = dev->data.buttons | buttonData;
      if(mouse_readmode == PS2MOUSE_READMODE_ABS)
	{
	  if(mouse.x < mousex_min) mouse.x = mousex_min;
	  if(mouse.x > mousex_max) mouse.x = mousex_max;
	  if(mouse.y < mousey_min) mouse.y = mousey_min;
	  if(mouse.y > mousey_max) mouse.y = mousey_max;
	}

      SignalSema(mouse_sema);
      //printf("X = %d, Y = %d, Wheel = %d, Buttons = %x\n", mouse.x, mouse.y, mouse.wheel, mouse.buttons);

      sceUsbdInterruptTransfer(dev->dataEndp, &dev->data, dev->packetSize, ps2mouse_data_recv, arg);
    }
}

int ps2mouse_init()

{
  iop_sema_t s;

  aurora_status = 0;

  s.initial = 1;
  s.max = 1;
  s.option = 0;
  s.attr = 0;
  mouse_sema = CreateSema(&s);
  if(mouse_sema <= 0)
    {
      printf("ERROR: Couldn't create ps2mouse sema\n");
      return 1;
    }

  if(sceUsbdRegisterLdd(&mouse_driver) >= 0)
    {
      memset(&mouse, 0, sizeof(mouse_data));
      memset(devices, 0, sizeof(mouse_dev *) * PS2MOUSE_MAXDEV);
      dev_count = 0;
      aurora_status = AURORA_MOUSE_STATUS_DRIVER;
      mouse_readmode = PS2MOUSE_READMODE_DIFF;
      mousex_min = -1000000;
      mousex_max =  1000000;
      mousey_min = -1000000;
      mousey_max =  1000000;
      mouse_dblclicktime = PS2MOUSE_DEFDBLCLICK;
      mouse_accel = PS2MOUSE_DEFACCEL;
      mouse_thres = PS2MOUSE_DEFTHRES;

      return 0;
  } else {
	printf("ERROR: Couldn't register ps2mouse driver\n");
	return 1;
  }
}

/* RPC Handlers */

void do_ps2mouse_read(u8 *data, int size)

{
  (void)size;

  //printf("PS2MOUSE read\n");
  memcpy(data, &mouse, sizeof(mouse_data));

  //printf("%d %d %d %d\n", mouse.x, mouse.y, mouse.buttons, mouse.wheel);
  if(mouse_readmode == PS2MOUSE_READMODE_DIFF)
    {
      mouse.x = 0;
      mouse.y = 0;
      mouse.wheel = 0;
      mouse.buttons &= 0xFF; /* Clear the double click flags */
    }
  else
    {
      mouse.wheel = 0;
      mouse.buttons &= 0xFF;
    }
}

void do_ps2mouse_setreadmode(const u32 *data, int size)

{
  (void)size;

  //printf("PS2MOUSE setreadmode mode %d\n", data[0]);
  if(data[0] == (u32)mouse_readmode)
    {
      return;
    }

  if((data[0] != PS2MOUSE_READMODE_DIFF) && (data[0] != PS2MOUSE_READMODE_ABS))
    {
      printf("ERROR: Invalid readmode\n");
      return;
    }

  memset(&mouse, 0, sizeof(mouse_data));
  mouse_readmode = data[0];
  if(mouse_readmode == PS2MOUSE_READMODE_ABS)
    {
      mouse.x = mousex_min;
      mouse.y = mousey_min;
    }
}

void do_ps2mouse_getreadmode(u32 *data, int size)

{
  (void)size;

  //printf("PS2MOUSE getreadmode\n");
  data[0] = mouse_readmode;
}

void do_ps2mouse_setthres(const u32 *data, int size)

{
  (void)size;

  //printf("PS2MOUSE setthres %d\n", data[0]);
  mouse_thres = data[0];
}

void do_ps2mouse_getthres(u32 *data, int size)

{
  (void)size;

  //printf("PS2MOUSE getthres\n");
  data[0] = mouse_thres;
}

void do_ps2mouse_setaccel(const u32 *data, int size)

{
  (void)size;

  //printf("PS2MOUSE setsense %d\n", data[0]);
  mouse_accel = data[0];
}

void do_ps2mouse_getaccel(u32 *data, int size)

{
  (void)size;

  //printf("PS2MOUSE getsense\n");
  data[0] = mouse_accel;
}

void do_ps2mouse_setboundary(const s32 *data, int size)

{
  (void)size;

  //printf("PS2MOUSE setboundry %d %d %d %d\n", data[0], data[1], data[2], data[3]);
  if(data[0] < data[1])
    {
      mousex_min = data[0];
      mousex_max = data[1];
    }

  if(data[2] < data[3])
    {
      mousey_min = data[2];
      mousey_max = data[3];
    }
  if(mouse_readmode == PS2MOUSE_READMODE_ABS)
    {

      mouse.x = mousex_min;
      mouse.y = mousey_min;
    }
}

void do_ps2mouse_getboundary(s32 *data, int size)

{
  (void)size;

  //printf("PS2MOUSE getboundry\n");
  data[0] = mousex_min;
  data[1] = mousex_max;
  data[2] = mousey_min;
  data[3] = mousey_max;
}

void do_ps2mouse_setposition(const s32 *data, int size)

{
  (void)size;

  //printf("PS2MOUSE setposition %d %d\n", data[0], data[1]);

  WaitSema(mouse_sema);

  if(data[0] < mousex_min)
    {
      mouse.x = mousex_min;
    }
  else if(data[0] > mousex_max)
    {
      mouse.x = mousex_max;
    }
  else
    {
      mouse.x = data[0];
    }

  if(data[1] < mousey_min)
    {
      mouse.y = mousey_min;
    }
  else if(data[1] > mousey_max)
    {
      mouse.y = mousey_max;
    }
  else
    {
      mouse.y = data[1];
    }
  SignalSema(mouse_sema);
}

void do_ps2mouse_reset()

{
  //printf("PS2MOUSE reset\n");
  memset(&mouse, 0, sizeof(mouse_data));
  if(mouse_readmode == PS2MOUSE_READMODE_ABS) /* If ABS mode then set to lowest boundry */
    {
      mouse.x = mousex_min;
      mouse.y = mousey_min;
    }
}

void do_ps2mouse_enum(u32 *data, int size)

{
  (void)size;

  //printf("PS2MOUSE enum\n");
  data[0] = dev_count;
}

void do_ps2mouse_setdblclicktime(const u32 *data, int size)

{
  (void)size;

  //printf("PS2MOUSE setdblclicktime %d\n", data[0]);
  mouse_dblclicktime = data[0];
}

void do_ps2mouse_getdblclicktime(u32 *data, int size)

{
  (void)size;

  //printf("PS2MOUSE getdblclicktime\n");
  data[0] = mouse_dblclicktime;
}

void do_ps2mouse_aurora_status(u32 *data, int size)
{
  (void)size;
  data[0] = aurora_status |
            (dev_count > 0 ? AURORA_MOUSE_STATUS_CONNECTED : 0);
}

void do_ps2mouse_getversion(u32 *data, int size)

{
  (void)size;

  //printf("PS2MOUSE getversion\n");
  data[0] = PS2MOUSE_VERSION;
}

void *rpcCommandHandler(u32 command, void *buffer, int size)

{
  switch(command)
    {
    case PS2MOUSE_READ: do_ps2mouse_read((u8 *) buffer, size);
      break;
    case PS2MOUSE_SETREADMODE: do_ps2mouse_setreadmode((u32 *) buffer, size);
      break;
    case PS2MOUSE_GETREADMODE: do_ps2mouse_getreadmode((u32 *) buffer, size);
      break;
    case PS2MOUSE_SETTHRES: do_ps2mouse_setthres((u32 *) buffer, size);
      break;
    case PS2MOUSE_GETTHRES: do_ps2mouse_getthres((u32 *) buffer, size);
      break;
    case PS2MOUSE_SETACCEL: do_ps2mouse_setaccel((u32 *) buffer, size);
      break;
    case PS2MOUSE_GETACCEL: do_ps2mouse_getaccel((u32 *) buffer, size);
      break;
    case PS2MOUSE_SETBOUNDARY: do_ps2mouse_setboundary((s32 *) buffer, size);
      break;
    case PS2MOUSE_GETBOUNDARY: do_ps2mouse_getboundary((s32 *) buffer, size);
      break;
    case PS2MOUSE_SETPOSITION: do_ps2mouse_setposition((s32 *) buffer, size);
      break;
    case PS2MOUSE_RESET: do_ps2mouse_reset();
      break;
    case PS2MOUSE_ENUM: do_ps2mouse_enum((u32 *) buffer, size);
      break;
    case PS2MOUSE_SETDBLCLICKTIME: do_ps2mouse_setdblclicktime((u32 *) buffer, size);
      break;
    case PS2MOUSE_GETDBLCLICKTIME: do_ps2mouse_getdblclicktime((u32 *) buffer, size);
      break;
    case PS2MOUSE_AURORA_STATUS: do_ps2mouse_aurora_status((u32 *) buffer, size);
      break;
    case PS2MOUSE_GETVERSION: do_ps2mouse_getversion((u32 *) buffer, size);
      break;
    default : printf("Unknown PS2MOUSE command %ld\n", command);
      break;
    }

  return buffer;
}
