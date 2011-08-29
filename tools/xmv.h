/* Header file for XMV player */
#ifndef XMV_H
#define XMV_H



#ifdef __cplusplus
	extern "C" {
#endif

		extern void XMVMain(const char* filename);
		extern void SetD3DDevice(D3DDevice* device);

#ifdef __cplusplus
	}
#endif

#endif