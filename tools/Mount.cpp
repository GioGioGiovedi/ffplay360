#undef memset
#undef memcpy

#include "Mount.h"
#include <xtl.h>
#include <stdio.h>
#include <string>
#include <vector>

using namespace std;

int cDrives::Mount( string MountPoint )
{
	char MountConv[260];
	sprintf_s( MountConv,"\\??\\%s", MountPoint.c_str() );

	char * SysPath = NULL;
	if (strcmp(MountPoint.c_str(), "Memunit0:") ==0) {
		SysPath = "\\Device\\Mu0";
	}
	if (strcmp(MountPoint.c_str(), "Memunit1:") ==0) {
		SysPath = "\\Device\\Mu1";
	}
	if (strcmp(MountPoint.c_str(), "OnBoardMU:") ==0) {
		SysPath = "\\Device\\BuiltInMuSfc";
	}
	if (strcmp(MountPoint.c_str(), "Dvd:") ==0) {
		SysPath = "\\Device\\Cdrom0";
	}
	if (strcmp(MountPoint.c_str(), "Hdd1:") ==0) {
		SysPath = "\\Device\\Harddisk0\\Partition1";
	}
	if (strcmp(MountPoint.c_str(), "Usb0:") ==0) {
		SysPath = "\\Device\\Mass0";
	}
	if (strcmp(MountPoint.c_str(), "Usb1:") ==0) {
		SysPath = "\\Device\\Mass1";
	}
	if (strcmp(MountPoint.c_str(), "Usb2:") ==0) {
		SysPath = "\\Device\\Mass2";
	}
	if (strcmp(MountPoint.c_str(), "USBMU0:") ==0) {
		SysPath = "\\Device\\Mass0PartitionFile\\Storage";
	}
	if (strcmp(MountPoint.c_str(), "USBMU1:") ==0) {
		SysPath = "\\Device\\Mass0PartitionFile\\Storage";
	}
	if (strcmp(MountPoint.c_str(), "USBMU2:") ==0) {
		SysPath = "\\Device\\Mass0PartitionFile\\Storage";
	}

    STRING sSysPath = { (USHORT)strlen( SysPath ), (USHORT)strlen( SysPath ) + 1, SysPath };
    STRING sMountConv = { (USHORT)strlen( MountConv ), (USHORT)strlen( MountConv ) + 1, MountConv };
    int res = ObCreateSymbolicLink( &sMountConv, &sSysPath );

    if (res != 0)
            return 1;

    if ( DriveMounted(MountPoint))
		return 0;
	else return 1;
}

vector<string> cDrives::mountAll() {
	vector<string> drives;
	for (int x = DEVICE_FIXED_START; x < DEVICE_REMOVABLE_END; x++) {
		string MP ="";
		switch (x) {
			case DEVICE_CDROM0:
				MP = "Dvd:";
				if (Mount(MP) == 1)
					drives.push_back(MP);
				break;	
			case DEVICE_MEMORY_ONBOARD:
				MP = "OnBoardMU:";
				if (Mount(MP) == 1)
					drives.push_back(MP);
				break;
			case DEVICE_HARDISK0_PART1:
				MP = "Hdd1:";
				if (Mount(MP) == 1)
					drives.push_back(MP);
				break;
			case DEVICE_MEMORY_UNIT0:
				MP = "Memunit0:";
				if (Mount(MP) == 1)
					drives.push_back(MP);
				break;
			case DEVICE_MEMORY_UNIT1:
				MP = "Memunit1:";
				if (Mount(MP) == 1)
					drives.push_back(MP);
				break;
			case DEVICE_USB0:
				MP = "Usb0:";
				if (Mount(MP) == 1)
					drives.push_back(MP);
				break;
			case DEVICE_USB1:
				MP = "Usb1:";
				if (Mount(MP) == 1)
					drives.push_back(MP);
				break;
			case DEVICE_USB2:
				MP = "Usb2:";
				if (Mount(MP) == 1)
					drives.push_back(MP);
				break;
		}
		
	
	}
	return drives;
}

int cDrives::DriveMounted(string path)
{
        WIN32_FIND_DATA findFileData;
        memset(&findFileData,0,sizeof(WIN32_FIND_DATA));
		string searchcmd = path + "\\*.*";

        //debugLog(searchcmd.c_str());
        HANDLE hFind = FindFirstFile(searchcmd.c_str(), &findFileData);
        if (hFind == INVALID_HANDLE_VALUE)
        {
                return 1;
        }
        FindClose(hFind);

        return 0;
}