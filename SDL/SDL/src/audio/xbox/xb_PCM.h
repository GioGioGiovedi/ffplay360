/* Header file for LaunchData */
#ifndef xb_PCM_H
#define xb_PCM_H

#include <XAudio2.h>

typedef
		struct PCMFilter
			PCMFilter;

#ifdef __cplusplus
	extern "C" {
#endif

		WAVEFORMATEXTENSIBLE getPCMFilter(WAVEFORMATEXTENSIBLE waveformat);

#ifdef __cplusplus
	}
#endif

#endif