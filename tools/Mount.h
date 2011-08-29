#include <xtl.h>
#include <stdio.h>
#include <string>
#include <vector>

using namespace std;

typedef enum 
{
	DEVICE_FIXED_START,
    DEVICE_NAND_FLASH, 
	DEVICE_MEMORY_ONBOARD, 
    DEVICE_CDROM0, 
	DEVICE_HARDISK0_PART1, 
	DEVICE_HARDISK0_PART0,
	DEVICE_HARDISK0_SYSPART, 
	DEVICE_HARDISK0_SYSEXT,
	DEVICE_FIXED_END,
	DEVICE_REMOVABLE_START,	// Place removable devices below this point
	DEVICE_MEMORY_UNIT0, 
	DEVICE_MEMORY_UNIT1,  
	DEVICE_USB0,
    DEVICE_USB1,  
	DEVICE_USB2, 
	DEVICE_HDDVD_PLAYER,         
    DEVICE_HDDVD_STORGE,
    DEVICE_TRANSFER_CABLE,
    DEVICE_TRANSFER_CABLE_SYSPART,
	DEVICE_TEST,
	DEVICE_USBMEMORY_UNIT0,
	DEVICE_USBMEMORY_UNIT1,
	DEVICE_USBMEMORY_UNIT2,
	DEVICE_USBMEMORY_Cache0,
	DEVICE_USBMEMORY_Cache1,
	DEVICE_USBMEMORY_Cache2,
	DEVICE_REMOVABLE_END
} DriveType;

typedef struct _STRING {
    USHORT Length;
    USHORT MaximumLength;
    PCHAR Buffer;
} STRING;

extern "C" int __stdcall ObCreateSymbolicLink( STRING*, STRING*);
extern "C" int __stdcall ObDeleteSymbolicLink( STRING* );

class cDrives {
public:
	static int Mount( string MountPoint );
	static int DriveMounted( string path);
	static vector<string> mountAll();
};
