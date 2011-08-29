#undef memset
#undef memcpy

#include <xtl.h>
#include <string>
#include <xaudio2.h>
#include "xb_PCM.h"

using namespace std;

WAVEFORMATEXTENSIBLE getPCMFilter(WAVEFORMATEXTENSIBLE waveformat) {
	
	waveformat.SubFormat                   = KSDATAFORMAT_SUBTYPE_PCM;
	return waveformat;
}