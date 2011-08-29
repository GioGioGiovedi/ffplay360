#include "FileMan.h"
#include "Mount.h"
#include <vector>
#include <string>
#include "../SDL/SDL/include/SDL.h"
#include "..\SDL_Image\SDL_image.h"
#include "..\SDL_ttf360\SDL_ttf.h"
#include "xboxdefs.h"

using namespace std;


struct FileItem {
	string path;
	bool isDir;
};

bool atRoot;
string CurrentPath;
vector<string> drives;
int x = 50;
int y = 50;

TTF_Font *Font;
SDL_Surface* screen;
int activeItem;
bool driveset = false;
SDL_Color basicColor = {/*fgR,fgG,fgB,fgA*/200,200,200,0};
SDL_Color highlighColor = {/*fgR,fgG,fgB,fgA*/0,128,128,0};

vector<FileItem> FullList;


const char *getFileManager() {
	
	Sleep(1000);
	int flags, i; 
	flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_NOPARACHUTE;
#if !defined(__MINGW32__) && !defined(__APPLE__) && !defined(_WINDOWS) && !defined(CANT_THREAD_EVENTS)
    flags |= SDL_INIT_EVENTTHREAD; /* Not supported on Windows or Mac OS X */
#endif
    if (SDL_Init (flags)) {
        sprintf("Could not initialize SDL - %s\n", SDL_GetError());
        return NULL;
    }

        const SDL_VideoInfo *vi = SDL_GetVideoInfo();


	SDL_EventState(SDL_ACTIVEEVENT, SDL_IGNORE);
    SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
    SDL_EventState(SDL_USEREVENT, SDL_IGNORE);

	if (!driveset) {
		drives = cDrives::mountAll();
		driveset = true;
	}
	atRoot = true;
	flags = SDL_HWSURFACE|SDL_ASYNCBLIT|SDL_HWACCEL|SDL_DOUBLEBUF|SDL_FULLSCREEN;
    screen = SDL_SetVideoMode(1280, 720, 0, flags);
	if (TTF_Init() != -1) {
		Font = TTF_OpenFont("d:\\Images\\mFont.ttf", 48);
		if (Font == NULL)
			return NULL;
			//Font->height = 40;
	}

	activeItem = 0;
	CurrentPath = "Current Path :: Root";
	drawscreen();
	SDL_Event event;
	SDL_Joystick *joy = SDL_JoystickOpen(0);
	int olWaitCount = 0;
	for (;;) {
		
		SDL_Event fakeevent;
		// ****** XBOX ******
		//ANALOG STICK HERE

		int q = (SDL_JoystickGetAxis(joy, 0));
		int r = (SDL_JoystickGetAxis(joy, 1));
		int g = (SDL_JoystickGetAxis(joy, 2));
		int h = (SDL_JoystickGetAxis(joy, 3));
		SDL_Delay(10);
		if (((q < -9000) || (q > 9000) || (r < -9000) || (r > 9000)) && olWaitCount == 0)
		{
			/*if ((r <= -9000) && (q <= -9000))
			{
				fakeevent.type = SDL_KEYDOWN;
				fakeevent.key.type = SDL_KEYDOWN;
				fakeevent.key.keysym.sym = SDLK_KP7;
				SDL_PushEvent (&fakeevent);
				//return;
			}*/
			/*if ((r <= -9000) && (q >= 9000))
			{
				fakeevent.type = SDL_KEYDOWN;
				fakeevent.key.type = SDL_KEYDOWN;
				fakeevent.key.keysym.sym = SDLK_KP9;
				SDL_PushEvent (&fakeevent);
				//return;
			}*/
			/*if ((r >= 9000) && (q <= -9000))
			{
				fakeevent.type = SDL_KEYDOWN;
				fakeevent.key.type = SDL_KEYDOWN;
				fakeevent.key.keysym.sym = SDLK_KP1;
				SDL_PushEvent (&fakeevent);
				//return;
			}*/
			/*if ((r >= 9000) && (q >= 9000))
			{
				fakeevent.type = SDL_KEYDOWN;
				fakeevent.key.type = SDL_KEYDOWN;
				fakeevent.key.keysym.sym = SDLK_KP3;
				SDL_PushEvent (&fakeevent);
				//return;
			}*/
			if (r <= -9000)
			{
				olWaitCount = 15;
				fakeevent.type = SDL_KEYDOWN;
				fakeevent.key.type = SDL_KEYDOWN;
				fakeevent.key.keysym.sym = SDLK_KP8;
				SDL_PushEvent (&fakeevent);
				//return;
			}
			if (r >= 9000)
			{
				olWaitCount = 15;
				fakeevent.type = SDL_KEYDOWN;
				fakeevent.key.type = SDL_KEYDOWN;
				fakeevent.key.keysym.sym = SDLK_KP2;
				SDL_PushEvent (&fakeevent);
				//return;
			}
			// Left
			/*if (q <= -9000 && overlay)
			{
				olWaitCount = 25;
				fakeevent.type = SDL_KEYDOWN;
				fakeevent.key.type = SDL_KEYDOWN;
				fakeevent.key.keysym.sym = SDLK_KP4;
				SDL_PushEvent (&fakeevent);
				//return;
			}*/
			// Right
			/*if (q >= 9000 && overlay)
			{
				olWaitCount = 25;
				fakeevent.type = SDL_KEYDOWN;
				fakeevent.key.type = SDL_KEYDOWN;
				fakeevent.key.keysym.sym = SDLK_KP6;
				SDL_PushEvent (&fakeevent);
				//return;
			}*/
		} else if (olWaitCount > 0) {
			olWaitCount--;
		}
		SDL_WaitEvent(&event);
		if (event.type == SDL_JOYBUTTONDOWN)
		{
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_L)){
				activeItem++;
				if (atRoot && activeItem >= drives.size())
					activeItem = drives.size() -1;
				else if (!atRoot && activeItem >= FullList.size())
					activeItem = FullList.size()-1;
				drawscreen();
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_R)){
				activeItem--;
				if (activeItem < 0)
					activeItem = 0;	
				drawscreen();
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_TRIGL)){
				
			}
		    if (SDL_JoystickGetButton(joy, XBOX_BUTTON_TRIGR)){

			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_CLICK1)){
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_CLICK2)){
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_SELECT)){
				XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP,0);
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_START)){
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_X)){
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_Y)){
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_B)){
				if (!atRoot) {
					int pos = CurrentPath.rfind("\\");
					if (pos != string::npos) {
						CurrentPath = CurrentPath.substr(0, pos);
						GetFileList();
						activeItem = 0;
						drawscreen();
					} else {
						atRoot = true;
						activeItem = 0;
						drawscreen();
					}
				}
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_A)){
				if (atRoot) {
					CurrentPath = drives.at(activeItem);
				} else if (FullList.at(activeItem).isDir) {
					CurrentPath = CurrentPath + "\\" + FullList.at(activeItem).path;
				} else {
					CurrentPath = CurrentPath + "\\" + FullList.at(activeItem).path;
					SDL_Quit();
					Sleep(1000);
					return CurrentPath.c_str();
				}
				atRoot = false;
				GetFileList();
				activeItem = 0;
				drawscreen();				
			}
		}
		switch(event.type) {
        case SDL_KEYDOWN:
            switch(event.key.keysym.sym) {
			case SDLK_KP2:
				activeItem++;
				if (atRoot && activeItem >= drives.size()-1)
					activeItem = drives.size() -1;
				else if (!atRoot && activeItem >= FullList.size()-1)
					activeItem = FullList.size()-1;
				drawscreen();
				break;
			case SDLK_KP8:
				activeItem--;
				if (activeItem < 0)
					activeItem = 0;	
				drawscreen();
				break;
			}
		}

	}
	return NULL;

}


void drawscreen() {
	
	int  color = SDL_MapRGB(screen->format, 0x00, 0x00, 0x00);
	SDL_FillRect(screen, NULL, color);
	string header = "Current Path :: " + CurrentPath;
	writeText(header.c_str(), x, 0, 0);
	if (atRoot) {
		for (unsigned int loop = 0; loop < drives.size(); loop++) {
			int active = 0;
			if (loop == activeItem)
				active = 1;
			writeText(drives.at(loop).c_str(), x, (y*(loop+1)), active);
		}
	} else {
		int loop = activeItem - 5;
		int dif = 0;
		if (loop < 0) {
			dif = loop;
			loop = 0;
		}
		int endloop = activeItem + (5-dif);
		if (FullList.size() < endloop)
			endloop = FullList.size();
		int count = 1;
		for (;loop < endloop; loop++) {
			int active = 0;
			if (loop == activeItem)
				active = 1;
			FileItem FI = FullList.at(loop);
			writeText(FI.path.c_str(), x, y*(count), active);
			count++;
		}
	}

	SDL_Flip(screen);
	SDL_Present();
}


void GetFileList() {
	FullList.clear();
	WIN32_FIND_DATA findFileData;
    memset(&findFileData,0,sizeof(WIN32_FIND_DATA));
	string searchcmd = CurrentPath + "\\*.*";

    //debugLog(searchcmd.c_str());
   HANDLE hFind = FindFirstFile(searchcmd.c_str(), &findFileData);
	if (hFind == INVALID_HANDLE_VALUE)
		return;
	do {
		FileItem FI;
		FI.path = findFileData.cFileName;
		FI.isDir = false;
		if(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			FI.isDir = true;
		}
		FullList.push_back(FI);
	} while (FindNextFile(hFind, &findFileData));
	FindClose(hFind);

    return;
}

void writeText(const char *text, int x, int y, int isActive)
{
	SDL_Color tmpfontcolor = basicColor;
	if (isActive == 1) {
		tmpfontcolor = highlighColor;
	}
	SDL_Surface *resulting_text;
	SDL_Rect src, dest;
	
	resulting_text = TTF_RenderText_Blended(Font, text, tmpfontcolor);//, tmpfontbgcolor);

	src.x = 0;
	src.y = 0;
	src.h = resulting_text->h;
	src.w = resulting_text->w;

	dest.x = x;
	dest.y = y;
	dest.h = resulting_text->h;
	dest.w = resulting_text->w;
	  
	SDL_BlitSurface(resulting_text, &src, screen, &dest);
	SDL_FreeSurface(resulting_text);
}

