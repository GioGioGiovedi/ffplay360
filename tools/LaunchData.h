/* Header file for LaunchData */
#ifndef LaunchData_H
#define LaunchData_H

struct LaunchInfo {
	char filename[260];
	char callingapp[260];
};
/*
#ifdef __cplusplus
	class LaunchData {
	public:
		char* getLaunchData();
	};
#else*/
	typedef
		struct LaunchData
			LaunchData;
//#endif

#ifdef __cplusplus
	extern "C" {
#endif
		extern const char* ExLoadedImageName;
		extern const char* getLaunchData();
		extern const char* getReturnApp();

#ifdef __cplusplus
	}
#endif

#endif