###########################################################

Welcome to our release of FFPlay.
From Cancerous and JQE


This is using FFmpeg libraries from http://ffmpeg.org/ as ported by Ced2911
This is using SDL as ported by Lantus

This should play allmost all 720p videos.
1080p IS UNSTABLE please don't report it we know about i.

###########################################################

INFO FOR DEVS:

##############################################################

We added launchdata. This means that any app can launch this with a few parameters and play a video.

The parameters are

struct LaunchInfo {
> char filename[260](260.md);
> char callingapp[260](260.md);
};

here is an example of how to use it.

LaunchInfo**data = (LaunchInfo**)malloc(sizeof(LaunchInfo));
memset(data->filename, 0, MAX\_PATH);
memcpy(data->filename, file.c\_str(), strlen(file.c\_str()));
memset(data->callingapp, 0, MAX\_PATH);
string expath = getExecutablePath();
memcpy(data->callingapp, expath.c\_str(), strlen(expath.c\_str()));
XSetLaunchData((PVOID)data, sizeof(LaunchInfo));
XLaunchNewImage("game:\\vid\\default.xex", 0);


CallingApp is the app you are running, use this to return to the app after launch.
filename is the name of the file you want to play.


INFO FOR USERS:

##############################################################3

A brings up a menu, rewind, Fast Forward, Stop, Play, Pause.

NOTE: Fast Forward, and Rewind might be unstable. It is know and we are working on it.



NOW we have our people to thank.

[cOz](cOz.md) helped with some coding and solved a issue with running it on JTAGS

Thanks to testers:
Blackwolf, JPizzle, Mattie, Razkar, and Trajik.

Thanks for direction and assistance:
MaesterRo, Anthony, And node21

Thanks for the libraries Ced2911 and Lantus