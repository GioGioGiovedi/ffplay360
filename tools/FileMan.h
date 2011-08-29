/* Header file for LaunchData */
#pragma once
#ifndef LaunchData_H
#define LaunchData_H


/*	class FileManager {
	public:
		char *getFileManager();
		void GetFileList();
		void GetFolderList();
		void writeText(const char *text, int x, int y);
		void event_loop();
	};*/
typedef
	struct FileManager
		FileManager;

extern "C" {

		const char *getFileManager();
		void GetFileList();
		void writeText(const char *text, int x, int y, int isActive);
		void drawscreen();
}

#endif