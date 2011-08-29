#undef memset
#undef memcpy




#include "LaunchData.h"
#include "Mount.h"
#include <xtl.h>
#include <string>

using namespace std;

string retApp;

const char* getLaunchData() {
	
	DWORD dwLaunchDataSize = 0;    
    DWORD dwStatus = XGetLaunchDataSize( &dwLaunchDataSize );
    if( dwStatus == ERROR_SUCCESS )
    {
		LaunchInfo* pLaunchData = new LaunchInfo [ dwLaunchDataSize ];
        dwStatus = XGetLaunchData( pLaunchData, dwLaunchDataSize );
		if (dwStatus == ERROR_SUCCESS) {
			string tempStr = pLaunchData->filename;
			retApp = pLaunchData->callingapp;
			string mountPoint = tempStr.substr(0, tempStr.find("\\"));
			if (cDrives::Mount(mountPoint))
				return (const char*)pLaunchData->filename;
			else return NULL;
		}
    }
	return NULL;
}

const char* getReturnApp() {
	printf("Calling app %s", retApp.c_str());
	if (strcmp(retApp.c_str(), "") != 0) {
		return retApp.c_str();
	} else {
		string current = ExLoadedImageName;
		retApp = current.substr(current.find_last_of("\\") + 1);
		return retApp.c_str();
	}

	return NULL;
}