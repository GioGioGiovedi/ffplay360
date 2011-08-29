/*
 * FFplay : Simple Media Player based on the FFmpeg libraries
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#ifndef _XBOX
#include <windows.h>
#else //if 0
//#include <xtl.h>
#undef memset
#undef memcpy

#define memset XMemSet
#define memcpy XMemCpy
#endif


#include <stdio.h>
#include <tchar.h>
#include <string.h>
#include "tools\ini.h"

//#if DEFINED(0) //def _MSC_VER
#include "libavformat\os_support.h"
#include "libavutil\libm.h"
//#endif
#include "Tools\LaunchData.h"
#include "Tools\FileMan.h"
//#include "Tools\xmv.h"
#include "SDL_Image\SDL_image.h"
#include "SDL_ttf360\SDL_ttf.h"
#include "config.h"
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include "libavutil/avstring.h"
#include "libavutil/pixdesc.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavcodec/audioconvert.h"
#include "libavcodec/colorspace.h"
#include "libavcodec/opt.h"
#include "libavcodec/avfft.h"
#include "libavcodec/avcodec.h"
#ifdef _MSC_VER
exit_is_forbidden(int code)
{
}
#endif

#if CONFIG_AVFILTER
# include "libavfilter/avfilter.h"
# include "libavfilter/avfiltergraph.h"
# include "libavfilter/graphparser.h"
#endif
#include "cmdutils.h"

#include <SDL/SDL/include/SDL.h>
#include <SDL/SDL/include/SDL_thread.h>
//#include <SDL/SDL/include/resize.h>

#include "xboxdefs.h"

#ifdef __MINGW32__
#undef main /* We don't want SDL to override our main() */
#endif

#ifndef _MSC_VER
#include <unistd.h>
#endif

#include <assert.h>

const char program_name[] = "FFplay";
const int program_birth_year = 2003;
const int caching_delay = 0;
//Joystick init
SDL_Joystick * joy;
int joystickcount = 1;

//#define DEBUG_SYNC

#define MAX_QUEUE_SIZE (16 * 65536) //(8 * 1000 * 1024) //(15 * 1024 * 1024)
#define MIN_AUDIOQ_SIZE (16*1024) //(1000 * 1024) //(20 * 16 * 1024)
#define MIN_FRAMES 4

/* SDL audio buffer size, in samples. Should be small to have precise
   A/V sync as SDL does not have hardware buffer fullness info. */
#define SDL_AUDIO_BUFFER_SIZE 1024

/* no AV sync correction is done if below the AV sync threshold */
#define AV_SYNC_THRESHOLD 0.01
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 90.0

#define FRAME_SKIP_FACTOR 0.04

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 90

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   10

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
#define SAMPLE_ARRAY_SIZE 16384 //1024 // (96 * 100 * 32 * 6) //(4*65536)

#if !CONFIG_AVFILTER
static int sws_flags = SWS_BICUBIC;
#endif

typedef enum {
	Play,
	Pause,
	Stop,
	Rewind,
	FForward,
	None
} Button;

typedef struct MediaControls {
	SDL_Surface *sActive;
	SDL_Surface *sInactive;
	SDL_Surface *sHighlighted;
	SDL_Surface *sInactivehighlighted;
} MediaControls;

typedef struct MediaManager {

	SDL_Surface *MediaBar;
	int Height;
	int Width;
	float scale;

	MediaControls Buttons[None];

	Button Active;
	Button Highlighted;

} MediaManager;


typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    int abort_request;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

#define VIDEO_PICTURE_QUEUE_SIZE 4
#define SUBPICTURE_QUEUE_SIZE 4

typedef struct VideoPicture {
    double pts;                                  ///<presentation time stamp for this picture
    double target_clock;                         ///<av_gettime() time at which this should be displayed ideally
    int64_t pos;                                 ///<byte position in file
    SDL_Overlay *bmp;
	//TEST SDL_Surface *bmp;
    int width, height; /* source height & width */
    int allocated;
    enum PixelFormat pix_fmt;

#if CONFIG_AVFILTER
    AVFilterPicRef *picref;
#endif
} VideoPicture;

typedef struct SubPicture {
    double pts; /* presentation time stamp for this picture */
    AVSubtitle sub;
} SubPicture;

enum {
    AV_SYNC_AUDIO_MASTER, /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

typedef struct VideoState {
    SDL_Thread *parse_tid;
    SDL_Thread *video_tid;
    SDL_Thread *refresh_tid;
    AVInputFormat *iformat;
    int no_background;
    int abort_request;
    int paused;
    int last_paused;
    int seek_req;
    int seek_flags;
    int64_t seek_pos;
    int64_t seek_rel;
    int read_pause_return;
    AVFormatContext *ic;
    int dtg_active_format;

    int audio_stream;

    int av_sync_type;
    double external_clock; /* external clock base */
    int64_t external_clock_time;

    double audio_clock;
    double audio_diff_cum; /* used for AV difference average computation */
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int audio_diff_avg_count;
    AVStream *audio_st;
    PacketQueue audioq;
    int audio_hw_buf_size;
    /* samples output by the codec. we reserve more space for avsync
       compensation */
    DECLARE_ALIGNED(16,uint8_t,audio_buf1)[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
    DECLARE_ALIGNED(16,uint8_t,audio_buf2)[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
    uint8_t *audio_buf;
    unsigned int audio_buf_size; /* in bytes */
    int audio_buf_index; /* in bytes */
    AVPacket audio_pkt_temp;
    AVPacket audio_pkt;
    enum SampleFormat audio_src_fmt;
    AVAudioConvert *reformat_ctx;
    int output_channels;

    int show_audio; /* if true, display audio samples */
    int16_t sample_array[SAMPLE_ARRAY_SIZE];
    int sample_array_index;
    int last_i_start;
    RDFTContext *rdft;
    int rdft_bits;
    int xpos;

    SDL_Thread *subtitle_tid;
    int subtitle_stream;
    int subtitle_stream_changed;
    AVStream *subtitle_st;
    PacketQueue subtitleq;
    SubPicture subpq[SUBPICTURE_QUEUE_SIZE];
    int subpq_size, subpq_rindex, subpq_windex;
    SDL_mutex *subpq_mutex;
    SDL_cond *subpq_cond;

    double frame_timer;
    double frame_last_pts;
    double frame_last_delay;
    double video_clock;                          ///<pts of last decoded frame / predicted pts of next decoded frame
    int video_stream;
    AVStream *video_st;
    PacketQueue videoq;
    double video_current_pts;                    ///<current displayed pts (different from video_clock if frame fifos are used)
    double video_current_pts_drift;              ///<video_current_pts - time (av_gettime) at which we updated video_current_pts - used to have running video pts
    int64_t video_current_pos;                   ///<current displayed file pos
    VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
    int pictq_size, pictq_rindex, pictq_windex;
    SDL_mutex *pictq_mutex;
    SDL_cond *pictq_cond;
#if !CONFIG_AVFILTER
    struct SwsContext *img_convert_ctx;
#endif

    //    QETimer *video_timer;
    char filename[1024];
    int width, height, xleft, ytop;

    int64_t faulty_pts;
    int64_t faulty_dts;
    int64_t last_dts_for_fault_detection;
    int64_t last_pts_for_fault_detection;

#if CONFIG_AVFILTER
    AVFilterContext *out_video_filter;          ///<the last filter in the video chain
#endif

    float skip_frames;
    float skip_frames_index;
    int refresh;
} VideoState;
static void show_help(void);
static int audio_write_get_buf_size(VideoState *is);
static void startNewVideo(const char *filename);
/* options specified by the user */
static AVInputFormat *file_iformat;
static const char *input_filename;
static const char *window_title;
static int overlay;
static int lowres = 0;
static int olWaitCount;
static int skipWaitCount;
static int skipTriggerWait;
static int fs_screen_width;
static int fs_screen_height;
static int screen_width = 0;
static int screen_height = 0;
static int frame_width = 0;
static int frame_height = 0;
static enum PixelFormat frame_pix_fmt = PIX_FMT_YUYV422; //PIX_FMT_YUV420P; //PIX_FMT_ARGB;//PIX_FMT_NONE;//PIX_FMT_RGB565BE
static int audio_disable;
static int video_disable;
#ifndef _MSC_VER
static int wanted_stream[AVMEDIA_TYPE_NB]={
    [AVMEDIA_TYPE_AUDIO]=-1,
    [AVMEDIA_TYPE_VIDEO]=-1,
    [AVMEDIA_TYPE_SUBTITLE]=-1,
};
#else
static int wanted_stream[AVMEDIA_TYPE_NB]={
    -1,
    -1,
	0,
    -1,
};
#endif
static int wanted_channel = -1;
static int seek_by_bytes=-1;
static int display_disable;
static int show_status = 1;
static int av_sync_type = AV_SYNC_VIDEO_MASTER; //AV_SYNC_VIDEO_MASTER; //AV_SYNC_AUDIO_MASTER; AV_SYNC_EXTERNAL_CLOCK; //
static int64_t start_time = AV_NOPTS_VALUE;
static int64_t duration = AV_NOPTS_VALUE;
#if !defined(NDEBUG)
static int debug = 1;
static int debug_mv = 1;
#else
static int debug = 0;
static int debug_mv = 0;
#endif
static int step = 0;
static int thread_count = 8;
static int thread_type = FF_THREAD_FRAME; //FF_THREAD_FRAME; //FF_THREAD_SLICE;
static int workaround_bugs = 1;
static int fast = 0;
static int genpts = 1;
static int low_res = 1;
static int sst = 2;
static int idct = FF_IDCT_AUTO; //FF_IDCT_SIMPLEALPHA; FF_IDCT_AUTO;
static int skipframe;
static int skipidct;
static int skiploop;
static enum AVDiscard skip_frame = AVDISCARD_DEFAULT; //AVDISCARD_NONREF
static enum AVDiscard skip_idct = AVDISCARD_DEFAULT; //AVDISCARD_BIDIR; //AVDISCARD_NONKEY
static enum AVDiscard skip_loop_filter = AVDISCARD_ALL; //AVDISCARD_NONKEY; //AVDISCARD_NONREF;
static int error_recognition = FF_ER_COMPLIANT; //FF_ER_COMPLIANT; FF_ER_CAREFUL
static int error_concealment = FF_EC_DEBLOCK;//FF_EC_GUESS_MVS; //FF_EC_DEBLOCK
static int decoder_reorder_pts = -1;
static int autoexit = 1;
static int loop = 0;
static int framedrop = 0; //TRUE;
static MediaManager MM;
static int Fonts = 0;
static TTF_Font *Font;
static const char* filename;
static char info[255];
static int SizeFlag;
static char *xexname;

static int rdftspeed=20;
#if CONFIG_AVFILTER
static char *vfilters = NULL;
#endif

/* current context */
static int is_full_screen = 1;
static VideoState *cur_stream;
static int64_t audio_callback_time;

static AVPacket flush_pkt;

#define FF_ALLOC_EVENT   (SDL_USEREVENT)
#define FF_REFRESH_EVENT (SDL_USEREVENT + 1)
#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)

static SDL_Surface *screen;

static int packet_queue_put(PacketQueue *q, AVPacket *pkt);

typedef struct
{
	int thread_count;
	int thread_type;
	int workaround_bugs;
	int fast;
	int genpts;
	int low_res;
	int idct;
	int skipframe;
	int skipidct;
	int skiploop;
	int error_recognition;
	int error_concealment;
	int decoder_reorder_pts;
	int autoexit;
	int loop;
	int framedrop;
	char* xexname;
} configuration;


/* packet queue handling */
static void packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
    packet_queue_put(q, &flush_pkt);
}

static void packet_queue_flush(PacketQueue *q)
{
    AVPacketList *pkt, *pkt1;

    SDL_LockMutex(q->mutex);
    for(pkt = q->first_pkt; pkt != NULL; pkt = pkt1) {
        pkt1 = pkt->next;
        av_free_packet(&pkt->pkt);
        av_freep(&pkt);
    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    SDL_UnlockMutex(q->mutex);
}

static void packet_queue_end(PacketQueue *q)
{
    packet_queue_flush(q);
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

static int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    AVPacketList *pkt1;

    /* duplicate the packet */
    if (pkt!=&flush_pkt && av_dup_packet(pkt) < 0)
        return -1;

    pkt1 = av_malloc(sizeof(AVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;


    SDL_LockMutex(q->mutex);

    if (!q->last_pkt)

        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size + sizeof(*pkt1);
    /* XXX: should duplicate packet data in DV case */
    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
    return 0;
}

static void packet_queue_abort(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);

    q->abort_request = 1;

    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
    AVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for(;;) {
        if (q->abort_request) {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size + sizeof(*pkt1);
            *pkt = pkt1->pkt;
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

static inline void fill_rectangle(SDL_Surface *screen,
                                  int x, int y, int w, int h, int color)
{
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    SDL_FillRect(screen, &rect, color);
}

#if 0
/* draw only the border of a rectangle */
void fill_border(VideoState *s, int x, int y, int w, int h, int color)
{
    int w1, w2, h1, h2;

    /* fill the background */
    w1 = x;
    if (w1 < 0)
        w1 = 0;
    w2 = s->width - (x + w);
    if (w2 < 0)
        w2 = 0;
    h1 = y;
    if (h1 < 0)
        h1 = 0;
    h2 = s->height - (y + h);
    if (h2 < 0)
        h2 = 0;
    fill_rectangle(screen,
                   s->xleft, s->ytop,
                   w1, s->height,
                   color);
    fill_rectangle(screen,
                   s->xleft + s->width - w2, s->ytop,
                   w2, s->height,
                   color);
    fill_rectangle(screen,
                   s->xleft + w1, s->ytop,
                   s->width - w1 - w2, h1,
                   color);
    fill_rectangle(screen,
                   s->xleft + w1, s->ytop + s->height - h2,
                   s->width - w1 - w2, h2,
                   color);
}
#endif

#define ALPHA_BLEND(a, oldp, newp, s)\
((((oldp << s) * (255 - (a))) + (newp * (a))) / (255 << s))

#define RGBA_IN(r, g, b, a, s)\
{\
    unsigned int v = ((const uint32_t *)(s))[0];\
    a = (v >> 24) & 0xff;\
    r = (v >> 16) & 0xff;\
    g = (v >> 8) & 0xff;\
    b = v & 0xff;\
}

#define YUVA_IN(y, u, v, a, s, pal)\
{\
    unsigned int val = ((const uint32_t *)(pal))[*(const uint8_t*)(s)];\
    a = (val >> 24) & 0xff;\
    y = (val >> 16) & 0xff;\
    u = (val >> 8) & 0xff;\
    v = val & 0xff;\
}
/*(a << 24) | (y << 16) | (u << 8) | v;\*/
#define YUVA_OUT(d, y, u, v, a)\
{\
    ((uint32_t *)(d))[0] = (y << 24) | (y << 16) | (u << 8) | v;\
}


#define BPP 1

static void blend_subrect(AVPicture *dst, const AVSubtitleRect *rect, int imgw, int imgh)
{
    int wrap, wrap3, width2, skip2;
    int y, u, v, a, u1, v1, a1, w, h;
    uint8_t *lum, *cb, *cr;
    const uint8_t *p;
    const uint32_t *pal;
    int dstx, dsty, dstw, dsth;

    dstw = av_clip(rect->w, 0, imgw);
    dsth = av_clip(rect->h, 0, imgh);
    dstx = av_clip(rect->x, 0, imgw - dstw);
    dsty = av_clip(rect->y, 0, imgh - dsth);
    lum = dst->data[0] + dsty * dst->linesize[0];
    cb = dst->data[1] + (dsty >> 1) * dst->linesize[1];
    cr = dst->data[2] + (dsty >> 1) * dst->linesize[2];

    width2 = ((dstw + 1) >> 1) + (dstx & ~dstw & 1);
    skip2 = dstx >> 1;
    wrap = dst->linesize[0];
    wrap3 = rect->pict.linesize[0];
    p = rect->pict.data[0];
    pal = (const uint32_t *)rect->pict.data[1];  /* Now in YCrCb! */

    if (dsty & 1) {
        lum += dstx;
        cb += skip2;
        cr += skip2;

        if (dstx & 1) {
            YUVA_IN(y, u, v, a, p, pal);
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);
            cb[0] = ALPHA_BLEND(a >> 2, cb[0], u, 0);
            cr[0] = ALPHA_BLEND(a >> 2, cr[0], v, 0);
            cb++;
            cr++;
            lum++;
            p += BPP;
        }
        for(w = dstw - (dstx & 1); w >= 2; w -= 2) {
            YUVA_IN(y, u, v, a, p, pal);
            u1 = u;
            v1 = v;
            a1 = a;
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);

            YUVA_IN(y, u, v, a, p + BPP, pal);
            u1 += u;
            v1 += v;
            a1 += a;
            lum[1] = ALPHA_BLEND(a, lum[1], y, 0);
            cb[0] = ALPHA_BLEND(a1 >> 2, cb[0], u1, 1);
            cr[0] = ALPHA_BLEND(a1 >> 2, cr[0], v1, 1);
            cb++;
            cr++;
            p += 2 * BPP;
            lum += 2;
        }
        if (w) {
            YUVA_IN(y, u, v, a, p, pal);
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);
            cb[0] = ALPHA_BLEND(a >> 2, cb[0], u, 0);
            cr[0] = ALPHA_BLEND(a >> 2, cr[0], v, 0);
            p++;
            lum++;
        }
        p += wrap3 - dstw * BPP;
        lum += wrap - dstw - dstx;
        cb += dst->linesize[1] - width2 - skip2;
        cr += dst->linesize[2] - width2 - skip2;
    }
    for(h = dsth - (dsty & 1); h >= 2; h -= 2) {
        lum += dstx;
        cb += skip2;
        cr += skip2;

        if (dstx & 1) {
            YUVA_IN(y, u, v, a, p, pal);
            u1 = u;
            v1 = v;
            a1 = a;
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);
            p += wrap3;
            lum += wrap;
            YUVA_IN(y, u, v, a, p, pal);
            u1 += u;
            v1 += v;
            a1 += a;
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);
            cb[0] = ALPHA_BLEND(a1 >> 2, cb[0], u1, 1);
            cr[0] = ALPHA_BLEND(a1 >> 2, cr[0], v1, 1);
            cb++;
            cr++;
            p += -wrap3 + BPP;
            lum += -wrap + 1;
        }
        for(w = dstw - (dstx & 1); w >= 2; w -= 2) {
            YUVA_IN(y, u, v, a, p, pal);
            u1 = u;
            v1 = v;
            a1 = a;
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);

            YUVA_IN(y, u, v, a, p + BPP, pal);
            u1 += u;
            v1 += v;
            a1 += a;
            lum[1] = ALPHA_BLEND(a, lum[1], y, 0);
            p += wrap3;
            lum += wrap;

            YUVA_IN(y, u, v, a, p, pal);
            u1 += u;
            v1 += v;
            a1 += a;
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);

            YUVA_IN(y, u, v, a, p + BPP, pal);
            u1 += u;
            v1 += v;
            a1 += a;
            lum[1] = ALPHA_BLEND(a, lum[1], y, 0);

            cb[0] = ALPHA_BLEND(a1 >> 2, cb[0], u1, 2);
            cr[0] = ALPHA_BLEND(a1 >> 2, cr[0], v1, 2);

            cb++;
            cr++;
            p += -wrap3 + 2 * BPP;
            lum += -wrap + 2;
        }
        if (w) {
            YUVA_IN(y, u, v, a, p, pal);
            u1 = u;
            v1 = v;
            a1 = a;
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);
            p += wrap3;
            lum += wrap;
            YUVA_IN(y, u, v, a, p, pal);
            u1 += u;
            v1 += v;
            a1 += a;
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);
            cb[0] = ALPHA_BLEND(a1 >> 2, cb[0], u1, 1);
            cr[0] = ALPHA_BLEND(a1 >> 2, cr[0], v1, 1);
            cb++;
            cr++;
            p += -wrap3 + BPP;
            lum += -wrap + 1;
        }
        p += wrap3 + (wrap3 - dstw * BPP);
        lum += wrap + (wrap - dstw - dstx);
        cb += dst->linesize[1] - width2 - skip2;
        cr += dst->linesize[2] - width2 - skip2;
    }
    /* handle odd height */
    if (h) {
        lum += dstx;
        cb += skip2;
        cr += skip2;

        if (dstx & 1) {
            YUVA_IN(y, u, v, a, p, pal);
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);
            cb[0] = ALPHA_BLEND(a >> 2, cb[0], u, 0);
            cr[0] = ALPHA_BLEND(a >> 2, cr[0], v, 0);
            cb++;
            cr++;
            lum++;
            p += BPP;
        }
        for(w = dstw - (dstx & 1); w >= 2; w -= 2) {
            YUVA_IN(y, u, v, a, p, pal);
            u1 = u;
            v1 = v;
            a1 = a;
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);

            YUVA_IN(y, u, v, a, p + BPP, pal);
            u1 += u;
            v1 += v;
            a1 += a;
            lum[1] = ALPHA_BLEND(a, lum[1], y, 0);
            cb[0] = ALPHA_BLEND(a1 >> 2, cb[0], u, 1);
            cr[0] = ALPHA_BLEND(a1 >> 2, cr[0], v, 1);
            cb++;
            cr++;
            p += 2 * BPP;
            lum += 2;
        }
        if (w) {
            YUVA_IN(y, u, v, a, p, pal);
            lum[0] = ALPHA_BLEND(a, lum[0], y, 0);
            cb[0] = ALPHA_BLEND(a >> 2, cb[0], u, 0);
            cr[0] = ALPHA_BLEND(a >> 2, cr[0], v, 0);
        }
    }
}

static void initMediaManager() {
	MM.Active = Play;
	MM.Highlighted = Play;

	MM.MediaBar = IMG_Load("d:\\Images\\mediaBar.png");

	//Play File
	MM.Buttons[Play].sActive = IMG_Load("d:\\Images\\playEn.png");
	MM.Buttons[Play].sInactive = IMG_Load("d:\\Images\\play.png");
	MM.Buttons[Play].sHighlighted = IMG_Load("d:\\Images\\playEnSel.png");
	MM.Buttons[Play].sInactivehighlighted = IMG_Load("d:\\Images\\playSel.png");
	
	//Pause File
	MM.Buttons[Pause].sActive = IMG_Load("d:\\Images\\pauseEn.png");
	MM.Buttons[Pause].sHighlighted = IMG_Load("d:\\Images\\pauseEnSel.png");
	MM.Buttons[Pause].sInactive = IMG_Load("d:\\Images\\pause.png");
	MM.Buttons[Pause].sInactivehighlighted = IMG_Load("d:\\Images\\pauseSel.png");

	//Stop File
	MM.Buttons[Stop].sActive = IMG_Load("d:\\Images\\stopEn.png");
	MM.Buttons[Stop].sHighlighted = IMG_Load("d:\\Images\\stopEnSel.png");
	MM.Buttons[Stop].sInactive = IMG_Load("d:\\Images\\stop.png");
	MM.Buttons[Stop].sInactivehighlighted = IMG_Load("d:\\Images\\stopSel.png");

	//FForward File
	MM.Buttons[FForward].sActive = IMG_Load("d:\\Images\\fforwardEn.png");
	MM.Buttons[FForward].sHighlighted = IMG_Load("d:\\Images\\fforwardEnSel.png");
	MM.Buttons[FForward].sInactive = IMG_Load("d:\\Images\\fforward.png");
	MM.Buttons[FForward].sInactivehighlighted = IMG_Load("d:\\Images\\fforwardSel.png");

	//rewind File
	MM.Buttons[Rewind].sActive = IMG_Load("d:\\Images\\rewindEn.png");
	MM.Buttons[Rewind].sHighlighted = IMG_Load("d:\\Images\\rewindEnSel.png");
	MM.Buttons[Rewind].sInactive = IMG_Load("d:\\Images\\rewind.png");
	MM.Buttons[Rewind].sInactivehighlighted = IMG_Load("d:\\Images\\rewindSel.png");

}

static void free_subpicture(SubPicture *sp)
{
    int i;

    for (i = 0; i < sp->sub.num_rects; i++)
    {
        av_freep(&sp->sub.rects[i]->pict.data[0]);
        av_freep(&sp->sub.rects[i]->pict.data[1]);
        av_freep(&sp->sub.rects[i]);
    }

    av_free(sp->sub.rects);

    memset(&sp->sub, 0, sizeof(AVSubtitle));
}

static SDL_Surface* getButtonPath(Button Btn) {
	if (Btn == MM.Active && Btn == MM.Highlighted)
		return MM.Buttons[Btn].sHighlighted;
	else if (Btn == MM.Active)
		return MM.Buttons[Btn].sActive;
	else if (Btn == MM.Highlighted)
		return MM.Buttons[Btn].sInactivehighlighted;
	else
		return MM.Buttons[Btn].sInactive;
}

static void drawtext(TTF_Font *fonttodraw/*, char fgR, char fgG, char fgB, char fgA, 
char bgR, char bgG, char bgB, char bgA*/, char text[])
{
	SDL_Color tmpfontcolor = {/*fgR,fgG,fgB,fgA*/255,255,255,0};
	SDL_Color tmpfontbgcolor = {/*bgR, bgG, bgB, bgA*/0,0,0,0};
	SDL_Surface *resulting_text;
	SDL_Rect src, dest;
	int displayX, displayY;
	int y = screen_height;
	int x = screen_width;
	resulting_text = SDL_GetVideoSurface();
	//fonttodraw->height = displayY/10;
	y = resulting_text->h;
	x = resulting_text->w;
	SDL_FreeSurface(resulting_text);
	resulting_text = TTF_RenderText_Blended(fonttodraw, text, tmpfontcolor);//, tmpfontbgcolor);

	src.x = 0;
	src.y = 0;
	src.h = resulting_text->h;
	src.w = resulting_text->w;

	displayX = (x/2) - (resulting_text->w /2);
	displayY = y - (resulting_text->h *1.5);
	
	dest.x = displayX;
	dest.y = displayY;
	dest.h = resulting_text->h;
	dest.w = resulting_text->w;
	  
	SDL_BlitSurface(resulting_text, &src, screen, &dest);
	SDL_FreeSurface(resulting_text);
}

static void displayOvImage(Button Btn, int Type) {
	SDL_Surface *image = NULL;
	int displayX = 0;
	int displayY = 0;
	int garbage = 0;
	int y = 0;
	int x = 0;
	int newx = 0;
	int newy = 0;
	float xfactor;
	float yfactor;
	float minfactor;
	SDL_Rect src, dest;
	x = cur_stream->width;
	y = cur_stream->height;
	if (Btn == None)
		image = SDL_DisplayFormat(MM.MediaBar);
	else if (Type == 0)
		image = SDL_DisplayFormat(MM.Buttons[Btn].sInactivehighlighted);
	else if (Type == 1)
		image = SDL_DisplayFormat(MM.Buttons[Btn].sActive);
	else if (Type == 2)
		image = SDL_DisplayFormat(MM.Buttons[Btn].sHighlighted);

	if (image == NULL) {
		printf("Failed to load image");
		return;
	}
	if (Btn == None) {
		MM.Height = image->h;
		MM.Width = image->w;
	}
	
	displayX = (x/2) - (MM.Width /2);
	displayY = y - (MM.Height *2);

	if (Btn != None) {
		garbage = (MM.Height - image->h)/2;
		displayY = displayY + garbage;

		garbage = (MM.Width - (image->w * None))/(None+1);
		displayX = displayX + garbage;
		garbage = (image->w*Btn) + (garbage*Btn);
		displayX = displayX + garbage;
	}
	
	src.x = 0;
	src.y = 0;
	src.w = image->w;
	src.h =  image->h;
	
	dest.x = displayX;
	dest.y = displayY;
	dest.w =  image->w;
	dest.h =  image->h;

	SDL_BlitSurface( image, &src, screen, &dest);
	SDL_FreeSurface(image);
	return;
}

static void displayMenu() {
	displayOvImage(None, 0);
	if (MM.Active == MM.Highlighted) {
		displayOvImage(MM.Active, 2);
	} else {
		displayOvImage(MM.Active, 1);
		displayOvImage(MM.Highlighted, 0);
	}
	if (Fonts) {
		drawtext(Font, info);
	}
}

static void video_image_display(VideoState *is)
{
    VideoPicture *vp;
    SubPicture *sp;
    AVPicture pict;
    float aspect_ratio;
    int width, height, x, y = 0;
    SDL_Rect rect, dest;
    int i;

    vp = &is->pictq[is->pictq_rindex];
    if (vp->bmp) {
#if CONFIG_AVFILTER
         if (vp->picref->pixel_aspect.num == 0)
             aspect_ratio = 0;
         else
             aspect_ratio = av_q2d(vp->picref->pixel_aspect);
#else

        /* XXX: use variable in the frame */
        if (is->video_st->sample_aspect_ratio.num)
            aspect_ratio = av_q2d(is->video_st->sample_aspect_ratio);
        else if (is->video_st->codec->sample_aspect_ratio.num)
            aspect_ratio = av_q2d(is->video_st->codec->sample_aspect_ratio);
        else
            aspect_ratio = 0;
#endif
        if (aspect_ratio <= 0.0)
            aspect_ratio = 1.0;
        aspect_ratio *= (float)vp->width / (float)vp->height;
        /* if an active format is indicated, then it overrides the
           mpeg format */
#if 0
        if (is->video_st->codec->dtg_active_format != is->dtg_active_format) {
            is->dtg_active_format = is->video_st->codec->dtg_active_format;
            printf("dtg_active_format=%d\n", is->dtg_active_format);
        }
#endif
#if 0
        switch(is->video_st->codec->dtg_active_format) {
        case FF_DTG_AFD_SAME:
        default:
            /* nothing to do */
            break;
        case FF_DTG_AFD_4_3:
            aspect_ratio = 4.0 / 3.0;
            break;
        case FF_DTG_AFD_16_9:
            aspect_ratio = 16.0 / 9.0;
            break;
        case FF_DTG_AFD_14_9:
            aspect_ratio = 14.0 / 9.0;
            break;
        case FF_DTG_AFD_4_3_SP_14_9:
            aspect_ratio = 14.0 / 9.0;
            break;
        case FF_DTG_AFD_16_9_SP_14_9:
            aspect_ratio = 14.0 / 9.0;
            break;
        case FF_DTG_AFD_SP_4_3:
            aspect_ratio = 4.0 / 3.0;
            break;
        }
#endif

        if (is->subtitle_st)
        {
            if (is->subpq_size > 0)
            {
                sp = &is->subpq[is->subpq_rindex];

                if (vp->pts >= sp->pts + ((float) sp->sub.start_display_time / 1000))
                {
                    SDL_LockYUVOverlay (vp->bmp);
					//TEST SDL_LockSurface(vp->bmp);

                    //TEST (void*)pict.data[0] = vp->bmp->pixels;
                    pict.data[0] = vp->bmp->pixels[0];
                    pict.data[1] = vp->bmp->pixels[2];
                    pict.data[2] = vp->bmp->pixels[1];

                    //TEST pict.linesize[0] = vp->bmp->pitch;
                    pict.linesize[0] = vp->bmp->pitches[0];
                    pict.linesize[1] = vp->bmp->pitches[2];
                    pict.linesize[2] = vp->bmp->pitches[1];

                    for (i = 0; i < sp->sub.num_rects; i++)
                        blend_subrect(&pict, sp->sub.rects[i],
                                      vp->bmp->w, vp->bmp->h);

                    SDL_UnlockYUVOverlay (vp->bmp);
					//TEST SDL_UnlockSurface(vp->bmp);
                }
            }
        }


        /* XXX: we suppose the screen has a 1.0 pixel ratio */
        height = is->height;
        width = ((int)rint(height * aspect_ratio)) & ~1;
        if (width > is->width) {
            width = is->width;
            height = ((int)rint(width / aspect_ratio)) & ~1;
        }
        x = (is->width - width) / 2;
        y = (is->height - height) / 2;
        if (!is->no_background) {
            /* fill the background */
            //            fill_border(is, x, y, width, height, QERGB(0x00, 0x00, 0x00));
        } else {
            is->no_background = 0;
        }
        rect.x = is->xleft + x;
        rect.y = is->ytop  + y;
        rect.w = width;
        rect.h = height;
		/*rect.x = 0;
		rect.y = 0;
		rect.w = width;
		rect.h = height;*/

		SDL_DisplayYUVOverlay(vp->bmp, &rect);
		//TEST SDL_BlitSurface(vp->bmp, NULL, screen, &rect);
		if (overlay)
			displayMenu();
		//SDL_Flip(screen);
		//SDL_Present();
    } else {
#if 0
        fill_rectangle(screen,
                       is->xleft, is->ytop, is->width, is->height,
                       QERGB(0x00, 0x00, 0x00));
#endif
    }
}

static inline int compute_mod(int a, int b)
{
    a = a % b;
    if (a >= 0)
        return a;
    else
        return a + b;
}

static void video_audio_display(VideoState *s)
{
    int i, i_start, x, y1, y, ys, delay, n, nb_display_channels = 0;
    int ch, channels, h, h2, bgcolor, fgcolor;
    int16_t time_diff;
    int rdft_bits, nb_freq;

    for(rdft_bits=1; (1<<rdft_bits)<2*s->height; rdft_bits++)
        ;
    nb_freq= 1<<(rdft_bits-1);

    /* compute display index : center on currently output samples */
    /*channels = s->audio_st->codec->channels;*/
	channels = s->output_channels;

    nb_display_channels = channels;
    if (!s->paused) {
        int data_used= s->show_audio==1 ? s->width : (2*nb_freq);
        n = 2 * channels;
        delay = audio_write_get_buf_size(s);
        delay /= n;

        /* to be more precise, we take into account the time spent since
           the last buffer computation */
        if (audio_callback_time) {
            time_diff = av_gettime() - audio_callback_time;
            delay -= (time_diff * s->audio_st->codec->sample_rate) / 1000000;
        }

        delay += 2*data_used;
        if (delay < data_used)
            delay = data_used;

        i_start= x = compute_mod(s->sample_array_index - delay * channels, SAMPLE_ARRAY_SIZE);
        if(s->show_audio==1){
            h= INT_MIN;
            for(i=0; i<1000; i+=channels){
                int idx= (SAMPLE_ARRAY_SIZE + x - i) % SAMPLE_ARRAY_SIZE;
                int a= s->sample_array[idx];
                int b= s->sample_array[(idx + 4*channels)%SAMPLE_ARRAY_SIZE];
                int c= s->sample_array[(idx + 5*channels)%SAMPLE_ARRAY_SIZE];
                int d= s->sample_array[(idx + 9*channels)%SAMPLE_ARRAY_SIZE];
                int score= a-d;
                if(h<score && (b^c)<0){
                    h= score;
                    i_start= idx;
                }
            }
        }

        s->last_i_start = i_start;
    } else {
        i_start = s->last_i_start;
    }

    bgcolor = SDL_MapRGB(screen->format, 0x00, 0x00, 0x00);
    if(s->show_audio==1){
        fill_rectangle(screen,
                       s->xleft, s->ytop, s->width, s->height,
                       bgcolor);

        fgcolor = SDL_MapRGB(screen->format, 0xff, 0xff, 0xff);

        /* total height for one channel */
        h = s->height / nb_display_channels;
        /* graph height / 2 */
        h2 = (h * 9) / 20;
        for(ch = 0;ch < nb_display_channels; ch++) {
            i = i_start + ch;
            y1 = s->ytop + ch * h + (h / 2); /* position of center line */
            for(x = 0; x < s->width; x++) {
                y = (s->sample_array[i] * h2) >> 15;
                if (y < 0) {
                    y = -y;
                    ys = y1 - y;
                } else {
                    ys = y1;
                }
                fill_rectangle(screen,
                               s->xleft + x, ys, 1, y,
                               fgcolor);
                i += channels;
                if (i >= SAMPLE_ARRAY_SIZE)
                    i -= SAMPLE_ARRAY_SIZE;
            }
        }

        fgcolor = SDL_MapRGB(screen->format, 0x00, 0x00, 0xff);

        for(ch = 1;ch < nb_display_channels; ch++) {
            y = s->ytop + ch * h;
            fill_rectangle(screen,
                           s->xleft, y, s->width, 1,
                           fgcolor);
        }
        SDL_UpdateRect(screen, s->xleft, s->ytop, s->width, s->height);
		//SDL_Flip(screen);
		//SDL_Present();
    }else{
        nb_display_channels= FFMIN(nb_display_channels, 2);
        if(rdft_bits != s->rdft_bits){
            av_rdft_end(s->rdft);
            s->rdft = av_rdft_init(rdft_bits, DFT_R2C);
            s->rdft_bits= rdft_bits;
        }
        {
 #ifndef _MSC_VER
            FFTSample data[2][2*nb_freq];
#else
			FFTSample *data[2];
			FFTSample *buffer = av_malloc_items(2*2*nb_freq, FFTSample);

			data[0] = buffer;
			data[1] = buffer + 2*nb_freq;
#endif
            for(ch = 0;ch < nb_display_channels; ch++) {
                i = i_start + ch;
                for(x = 0; x < 2*nb_freq; x++) {
                    double w= (x-nb_freq)*(1.0/nb_freq);
                    data[ch][x]= s->sample_array[i]*(1.0-w*w);
                    i += channels;
                    if (i >= SAMPLE_ARRAY_SIZE)
                        i -= SAMPLE_ARRAY_SIZE;
                }
                av_rdft_calc(s->rdft, data[ch]);
            }
            //least efficient way to do this, we should of course directly access it but its more than fast enough
            for(y=0; y<s->height; y++){
                double w= 1/sqrt(nb_freq);
                int a= sqrt(w*sqrt(data[0][2*y+0]*data[0][2*y+0] + data[0][2*y+1]*data[0][2*y+1]));
                int b= sqrt(w*sqrt(data[1][2*y+0]*data[1][2*y+0] + data[1][2*y+1]*data[1][2*y+1]));
                a= FFMIN(a,255);
                b= FFMIN(b,255);
                fgcolor = SDL_MapRGB(screen->format, a, b, (a+b)/2);

                fill_rectangle(screen,
                            s->xpos, s->height-y, 1, 1,
                            fgcolor);
            }

#ifdef _MSC_VER
			av_free(buffer);
#endif
        }
        SDL_UpdateRect(screen, s->xpos, s->ytop, 1, s->height);
		//SDL_Flip(screen);
		//SDL_Present();
        s->xpos++;
        if(s->xpos >= s->width)
            s->xpos= s->xleft;
    }
}

static int video_open(VideoState *is){
    int flags = SDL_HWSURFACE|SDL_ASYNCBLIT|SDL_HWACCEL|SDL_DOUBLEBUF;
    int w,h;

    if(is_full_screen)
		SizeFlag = SDL_FULLSCREEN;
    else              
		SizeFlag = SDL_RESIZABLE;
    if (is_full_screen && fs_screen_width) {
        w = fs_screen_width;
        h = fs_screen_height;
    } else if(!is_full_screen && screen_width){
        w = screen_width;
        h = screen_height;
#if CONFIG_AVFILTER
    }else if (is->out_video_filter && is->out_video_filter->inputs[0]){
        w = is->out_video_filter->inputs[0]->w;
        h = is->out_video_filter->inputs[0]->h;
#else
    }else if (is->video_st && is->video_st->codec->width){
        w = is->video_st->codec->width;
        h = is->video_st->codec->height;
#endif
    } else {
        w = 640;
        h = 480;
    }
	flags |= SizeFlag;
    if(screen && is->width == screen->w && screen->w == w
       && is->height== screen->h && screen->h == h)
        return 0;

#ifndef __APPLE__
    screen = SDL_SetVideoMode(w, h, 0, flags);
#else
    /* setting bits_per_pixel = 0 or 32 causes blank video on OS X */
    screen = SDL_SetVideoMode(w, h, 24, flags);
#endif
    if (!screen) {
        printf("SDL: could not set video mode %d x %d- exiting\n", w, h);
        return -1;
    }
    if (!window_title)
        window_title = input_filename;
    SDL_WM_SetCaption(window_title, window_title);

    is->width = screen->w;
    is->height = screen->h;

    return 0;
}

/* display the current picture, if any */
static void video_display(VideoState *is)
{
    if(!screen)
        video_open(cur_stream);
    if (is->audio_st && is->show_audio)
        video_audio_display(is);
    else if (is->video_st)
        video_image_display(is);
}

static int refresh_thread(void *opaque)
{
    VideoState *is= opaque;
    while(!is->abort_request){
    SDL_Event event;
    event.type = FF_REFRESH_EVENT;
    event.user.data1 = opaque;
        if(!is->refresh){
            is->refresh=1;
    SDL_PushEvent(&event);
        }
        usleep(is->audio_st && is->show_audio ? rdftspeed*1000 : 5000); //FIXME ideally we should wait the correct time but SDLs event passing is so slow it would be silly
    }
    return 0;
}

/* get the current audio clock value */
static double get_audio_clock(VideoState *is)
{
    double pts;
    int hw_buf_size, bytes_per_sec;
    pts = is->audio_clock;
    hw_buf_size = audio_write_get_buf_size(is);
    bytes_per_sec = 0;
    if (is->audio_st) {
        bytes_per_sec = is->audio_st->codec->sample_rate *
            2 * is->output_channels; /*2 * is->audio_st->codec->channels;*/
    }
    if (bytes_per_sec)
        pts -= (double)hw_buf_size / bytes_per_sec;
    return pts;
}

/* get the current video clock value */
static double get_video_clock(VideoState *is)
{
    if (is->paused) {
        return is->video_current_pts;
    } else {
        return is->video_current_pts_drift + av_gettime() / 1000000.0;
    }
}

/* get the current external clock value */
static double get_external_clock(VideoState *is)
{
    int64_t ti;
    ti = av_gettime();
    return is->external_clock + ((ti - is->external_clock_time) * 1e-6);
}

/* get the current master clock value */
static double get_master_clock(VideoState *is)
{
    double val;

    if (is->av_sync_type == AV_SYNC_VIDEO_MASTER) {
        if (is->video_st)
            val = get_video_clock(is);
        else
            val = get_audio_clock(is);
    } else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER) {
        if (is->audio_st)
            val = get_audio_clock(is);
        else
            val = get_video_clock(is);
    } else {
        val = get_external_clock(is);
    }
    return val;
}

/* seek in the stream */
static void stream_seek(VideoState *is, int64_t pos, int64_t rel, int seek_by_bytes)
{
    if (!is->seek_req) {
        is->seek_pos = pos;
        is->seek_rel = rel;
        is->seek_flags &= ~AVSEEK_FLAG_BYTE;
        if (seek_by_bytes)
            is->seek_flags |= AVSEEK_FLAG_BYTE;
        is->seek_req = 1;
    }
}

/* pause or resume the video */
static void stream_pause(VideoState *is)
{
    if (is->paused) {
        is->frame_timer += av_gettime() / 1000000.0 + is->video_current_pts_drift - is->video_current_pts;
        if(is->read_pause_return != AVERROR(ENOSYS)){
            is->video_current_pts = is->video_current_pts_drift + av_gettime() / 1000000.0;
        }
        is->video_current_pts_drift = is->video_current_pts - av_gettime() / 1000000.0;
    }
    is->paused = !is->paused;
}

static double compute_target_time(double frame_current_pts, VideoState *is)
{
    double delay, sync_threshold, diff;

    /* compute nominal delay */
    delay = frame_current_pts - is->frame_last_pts;
    if (delay <= 0 || delay >= 10.0) {
        /* if incorrect delay, use previous one */
        delay = is->frame_last_delay;
    } else {
        is->frame_last_delay = delay;
    }
    is->frame_last_pts = frame_current_pts;

    /* update delay to follow master synchronisation source */
    if (((is->av_sync_type == AV_SYNC_AUDIO_MASTER && is->audio_st) ||
         is->av_sync_type == AV_SYNC_EXTERNAL_CLOCK)) {
        /* if video is slave, we try to correct big delays by
           duplicating or deleting a frame */
        diff = get_video_clock(is) - get_master_clock(is);

        /* skip or repeat frame. We take into account the
           delay to compute the threshold. I still don't know
           if it is the best guess */
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD, delay);
        if (fabs(diff) < AV_NOSYNC_THRESHOLD) {
            if (diff <= -sync_threshold)
                delay = 0;
            else if (diff >= sync_threshold)
                delay = 2 * delay;
        }
    }
    is->frame_timer += delay;
#if defined(DEBUG_SYNC)
    printf("video: delay=%0.3f actual_delay=%0.3f pts=%0.3f A-V=%f\n",
            delay, actual_delay, frame_current_pts, -diff);
#endif

    return is->frame_timer;
}

/* called to display each frame */
static void video_refresh_timer(void *opaque)
{
    VideoState *is = opaque;
    VideoPicture *vp;

    SubPicture *sp, *sp2;

    if (is->video_st) {
retry:
        if (is->pictq_size == 0) {
            //nothing to do, no picture to display in the que
        } else {
            double time= av_gettime()/1000000.0;
            double next_target;
            /* dequeue the picture */
            vp = &is->pictq[is->pictq_rindex];

            if(time < vp->target_clock)
                return;
            /* update current video pts */
            is->video_current_pts = vp->pts;
            is->video_current_pts_drift = is->video_current_pts - time;
            is->video_current_pos = vp->pos;
            if(is->pictq_size > 1){
                VideoPicture *nextvp= &is->pictq[(is->pictq_rindex+1)%VIDEO_PICTURE_QUEUE_SIZE];
                assert(nextvp->target_clock >= vp->target_clock);
                next_target= nextvp->target_clock;
            }else{
                next_target= vp->target_clock + is->video_clock - vp->pts; //FIXME pass durations cleanly
            }
            if(framedrop && time > next_target){
                is->skip_frames *= 1.0 + FRAME_SKIP_FACTOR;
                if(is->pictq_size > 1 || time > next_target + 0.5){
                    /* update queue size and signal for next picture */
                    if (++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE)
                        is->pictq_rindex = 0;

                    SDL_LockMutex(is->pictq_mutex);
                    is->pictq_size--;
                    SDL_CondSignal(is->pictq_cond);
                    SDL_UnlockMutex(is->pictq_mutex);
                    goto retry;
                }
            }

            if(is->subtitle_st) {
                if (is->subtitle_stream_changed) {
                    SDL_LockMutex(is->subpq_mutex);

                    while (is->subpq_size) {
                        free_subpicture(&is->subpq[is->subpq_rindex]);

                        /* update queue size and signal for next picture */
                        if (++is->subpq_rindex == SUBPICTURE_QUEUE_SIZE)
                            is->subpq_rindex = 0;

                        is->subpq_size--;
                    }
                    is->subtitle_stream_changed = 0;

                    SDL_CondSignal(is->subpq_cond);
                    SDL_UnlockMutex(is->subpq_mutex);
                } else {
                    if (is->subpq_size > 0) {
                        sp = &is->subpq[is->subpq_rindex];

                        if (is->subpq_size > 1)
                            sp2 = &is->subpq[(is->subpq_rindex + 1) % SUBPICTURE_QUEUE_SIZE];
                        else
                            sp2 = NULL;

                        if ((is->video_current_pts > (sp->pts + ((float) sp->sub.end_display_time / 1000)))
                                || (sp2 && is->video_current_pts > (sp2->pts + ((float) sp2->sub.start_display_time / 1000))))
                        {
                            free_subpicture(sp);

                            /* update queue size and signal for next picture */
                            if (++is->subpq_rindex == SUBPICTURE_QUEUE_SIZE)
                                is->subpq_rindex = 0;

                            SDL_LockMutex(is->subpq_mutex);
                            is->subpq_size--;
                            SDL_CondSignal(is->subpq_cond);
                            SDL_UnlockMutex(is->subpq_mutex);
                        }
                    }
                }
            }

            /* display picture */
            video_display(is);

            /* update queue size and signal for next picture */
            if (++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE)
                is->pictq_rindex = 0;

            SDL_LockMutex(is->pictq_mutex);
            is->pictq_size--;
            SDL_CondSignal(is->pictq_cond);
            SDL_UnlockMutex(is->pictq_mutex);
        }
    } else if (is->audio_st) {
        /* draw the next audio frame */

        /* if only audio stream, then display the audio bars (better
           than nothing, just to test the implementation */

        /* display picture */
        video_display(is);
    }
    if (show_status) {
        static int64_t last_time;
        int64_t cur_time;
        int aqsize, vqsize, sqsize;
        double av_diff;

        cur_time = av_gettime();
        if (!last_time || (cur_time - last_time) >= 30000) {
            aqsize = 0;
            vqsize = 0;
            sqsize = 0;
            if (is->audio_st)
                aqsize = is->audioq.size;
            if (is->video_st)
                vqsize = is->videoq.size;
            if (is->subtitle_st)
                sqsize = is->subtitleq.size;
            av_diff = 0;
            if (is->audio_st && is->video_st)
                av_diff = get_audio_clock(is) - get_video_clock(is);
#if defined(PRINT_LOTS_OF_CRAP)
            printf("%7.2f A-V:%7.3f s:%3.1f aq=%5dKB vq=%5dKB sq=%5dB f=%"PRId64"/%"PRId64"   \r",
                   get_master_clock(is), av_diff, FFMAX(is->skip_frames-1, 0), aqsize / 1024, vqsize / 1024, sqsize, is->faulty_dts, is->faulty_pts);
#endif
            fflush(stdout);
            last_time = cur_time;

        }
    }
}

/* allocate a picture (needs to do that in main thread to avoid
   potential locking problems */
static void alloc_picture(void *opaque)
{
    VideoState *is = opaque;
    VideoPicture *vp;
	Uint32 amask = 0xff000000;
	Uint32 rmask = 0x00ff0000;
	Uint32 gmask = 0x0000ff00;
	Uint32 bmask = 0x000000ff;

    vp = &is->pictq[is->pictq_windex];

    if (vp->bmp) {
        SDL_FreeYUVOverlay(vp->bmp);
		//TEST SDL_FreeSurface(vp->bmp);
	}

#if CONFIG_AVFILTER
    if (vp->picref)
        avfilter_unref_pic(vp->picref);
    vp->picref = NULL;

    vp->width   = is->out_video_filter->inputs[0]->w;
    vp->height  = is->out_video_filter->inputs[0]->h;
    vp->pix_fmt = is->out_video_filter->inputs[0]->format;
#else
    vp->width   = is->video_st->codec->width;
    vp->height  = is->video_st->codec->height;
    vp->pix_fmt = is->video_st->codec->pix_fmt;
#endif

    vp->bmp = SDL_CreateYUVOverlay(vp->width, vp->height,
                                   SDL_YV12_OVERLAY,
                                   screen);

	//TEST vp->bmp = SDL_CreateRGBSurface(SDL_HWSURFACE, vp->width, vp->height, 32, rmask,gmask,bmask,amask);
    SDL_LockMutex(is->pictq_mutex);
    vp->allocated = 1;
    SDL_CondSignal(is->pictq_cond);
    SDL_UnlockMutex(is->pictq_mutex);
}

/**
 *
 * @param pts the dts of the pkt / pts of the frame and guessed if not known
 */
static int queue_picture(VideoState *is, AVFrame *src_frame, double pts, int64_t pos)
{
    VideoPicture *vp;
    int dst_pix_fmt;
#if CONFIG_AVFILTER
    AVPicture pict_src;
#endif
    /* wait until we have space to put a new picture */
    SDL_LockMutex(is->pictq_mutex);

    if(is->pictq_size>=VIDEO_PICTURE_QUEUE_SIZE && !is->refresh)
        is->skip_frames= FFMAX(1.0 - FRAME_SKIP_FACTOR, is->skip_frames * (1.0-FRAME_SKIP_FACTOR));

    while (is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE &&
           !is->videoq.abort_request) {
        SDL_CondWait(is->pictq_cond, is->pictq_mutex);
    }
    SDL_UnlockMutex(is->pictq_mutex);

    if (is->videoq.abort_request)
        return -1;

    vp = &is->pictq[is->pictq_windex];

    /* alloc or resize hardware picture buffer */
    if (!vp->bmp ||
#if CONFIG_AVFILTER
        vp->width  != is->out_video_filter->inputs[0]->w ||
        vp->height != is->out_video_filter->inputs[0]->h) {
#else
        vp->width != is->video_st->codec->width ||
        vp->height != is->video_st->codec->height) {
#endif
        SDL_Event event;

        vp->allocated = 0;

        /* the allocation must be done in the main thread to avoid
           locking problems */
        event.type = FF_ALLOC_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);

        /* wait until the picture is allocated */
        SDL_LockMutex(is->pictq_mutex);
        while (!vp->allocated && !is->videoq.abort_request) {
            SDL_CondWait(is->pictq_cond, is->pictq_mutex);
        }
        SDL_UnlockMutex(is->pictq_mutex);

        if (is->videoq.abort_request)
            return -1;
    }

    /* if the frame is not skipped, then display it */
    if (vp->bmp) {
        AVPicture pict;
#if CONFIG_AVFILTER
        if(vp->picref)
            avfilter_unref_pic(vp->picref);
        vp->picref = src_frame->opaque;
#endif

        /* get a pointer on the bitmap */
        SDL_LockYUVOverlay (vp->bmp);
		//TEST SDL_LockSurface(vp->bmp);

        dst_pix_fmt = PIX_FMT_YUYV422;// PIX_FMT_ARGB; //PIX_FMT_YUV420P;
        memset(&pict,0,sizeof(AVPicture));
        //(void*)pict.data[0] = vp->bmp->pixels/*[0]*/;
        pict.data[0] = vp->bmp->pixels[0];
        pict.data[1] = vp->bmp->pixels[2];
        pict.data[2] = vp->bmp->pixels[1];

        //TEST pict.linesize[0] = vp->bmp->pitch/*es[0]*/;
        pict.linesize[0] = vp->bmp->pitches[0];
        pict.linesize[1] = vp->bmp->pitches[2];
        pict.linesize[2] = vp->bmp->pitches[1];

#if CONFIG_AVFILTER
        pict_src.data[0] = src_frame->data[0];
        pict_src.data[1] = src_frame->data[1];
        pict_src.data[2] = src_frame->data[2];

        pict_src.linesize[0] = src_frame->linesize[0];
        pict_src.linesize[1] = src_frame->linesize[1];
        pict_src.linesize[2] = src_frame->linesize[2];

        //FIXME use direct rendering
        av_picture_copy(&pict, &pict_src,
                        vp->pix_fmt, vp->width, vp->height);
#else
        sws_flags = av_get_int(sws_opts, "sws_flags", NULL);
        is->img_convert_ctx = sws_getCachedContext(is->img_convert_ctx,
            vp->width, vp->height, vp->pix_fmt, vp->width, vp->height,
            dst_pix_fmt, sws_flags, NULL, NULL, NULL);
        if (is->img_convert_ctx == NULL) {
            sprintf("Cannot initialize the conversion context\n");
            return(-1);
        }
        sws_scale(is->img_convert_ctx, src_frame->data, src_frame->linesize,
                  0, vp->height, pict.data, pict.linesize);
#endif
        /* update the bitmap content */
        SDL_UnlockYUVOverlay(vp->bmp);
		//TEST SDL_UnlockSurface(vp->bmp);

		SDL_Flip(screen);
		SDL_Present();

        vp->pts = pts;
        vp->pos = pos;

        /* now we can update the picture count */
        if (++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE)
            is->pictq_windex = 0;
        SDL_LockMutex(is->pictq_mutex);
        vp->target_clock= compute_target_time(vp->pts, is);

        is->pictq_size++;
        SDL_UnlockMutex(is->pictq_mutex);
    }
    return 0;
}

/**
 * compute the exact PTS for the picture if it is omitted in the stream
 * @param pts1 the dts of the pkt / pts of the frame
 */
static int output_picture2(VideoState *is, AVFrame *src_frame, double pts1, int64_t pos)
{
    double frame_delay, pts;

    pts = pts1;

    if (pts != 0) {
        /* update video clock with pts, if present */
        is->video_clock = pts;
    } else {
        pts = is->video_clock;
    }
    /* update video clock for next frame */
    frame_delay = av_q2d(is->video_st->codec->time_base);
    /* for MPEG2, the frame can be repeated, so we update the
       clock accordingly */
    frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
    is->video_clock += frame_delay;

#if defined(DEBUG_SYNC) && 0
    printf("frame_type=%c clock=%0.3f pts=%0.3f\n",
           av_get_pict_type_char(src_frame->pict_type), pts, pts1);
#endif
    return queue_picture(is, src_frame, pts, pos);
}

static int get_video_frame(VideoState *is, AVFrame *frame, int64_t *pts, AVPacket *pkt)
{
    int len1, got_picture, i;

        if (packet_queue_get(&is->videoq, pkt, 1) < 0)
            return -1;

        if(pkt->data == flush_pkt.data){
            avcodec_flush_buffers(is->video_st->codec);

            SDL_LockMutex(is->pictq_mutex);
            //Make sure there are no long delay timers (ideally we should just flush the que but thats harder)
            for(i=0; i<VIDEO_PICTURE_QUEUE_SIZE; i++){
                is->pictq[i].target_clock= 0;
            }
            while (is->pictq_size && !is->videoq.abort_request) {
                SDL_CondWait(is->pictq_cond, is->pictq_mutex);
            }
            is->video_current_pos= -1;
            SDL_UnlockMutex(is->pictq_mutex);

            is->last_dts_for_fault_detection=
            is->last_pts_for_fault_detection= INT64_MIN;
            is->frame_last_pts= AV_NOPTS_VALUE;
            is->frame_last_delay = 0;
            is->frame_timer = (double)av_gettime() / 1000000.0;
            is->skip_frames= 1;
            is->skip_frames_index= 0;
            return 0;
        }

        /* NOTE: ipts is the PTS of the _first_ picture beginning in
           this packet, if any */
        is->video_st->codec->reordered_opaque= pkt->pts;
        len1 = avcodec_decode_video2(is->video_st->codec,
                                    frame, &got_picture,
                                    pkt);

        if (got_picture) {
            if(pkt->dts != AV_NOPTS_VALUE){
                is->faulty_dts += pkt->dts <= is->last_dts_for_fault_detection;
                is->last_dts_for_fault_detection= pkt->dts;
            }
            if(frame->reordered_opaque != AV_NOPTS_VALUE){
                is->faulty_pts += frame->reordered_opaque <= is->last_pts_for_fault_detection;
                is->last_pts_for_fault_detection= frame->reordered_opaque;
            }
        }

        if(   (   decoder_reorder_pts==1
               || (decoder_reorder_pts && is->faulty_pts<is->faulty_dts)
               || pkt->dts == AV_NOPTS_VALUE)
           && frame->reordered_opaque != AV_NOPTS_VALUE)
            *pts= frame->reordered_opaque;
        else if(pkt->dts != AV_NOPTS_VALUE)
            *pts= pkt->dts;
        else
            *pts= 0;

//            if (len1 < 0)
//                break;
    if (got_picture){
        is->skip_frames_index += 1;
        if(is->skip_frames_index >= is->skip_frames){
            is->skip_frames_index -= FFMAX(is->skip_frames, 1.0);
            return 1;
        }

    }
    return 0;
}

#if CONFIG_AVFILTER
typedef struct {
    VideoState *is;
    AVFrame *frame;
    int use_dr1;
} FilterPriv;

static int input_get_buffer(AVCodecContext *codec, AVFrame *pic)
{
    AVFilterContext *ctx = codec->opaque;
    AVFilterPicRef  *ref;
    int perms = AV_PERM_WRITE;
    int w, h, i, stride[4];
    unsigned edge;
	AVPixFmtDescriptor *av_pix_fmt_descriptors = get_av_pix_fmt_descriptors();

    if(pic->buffer_hints & FF_BUFFER_HINTS_VALID) {
        if(pic->buffer_hints & FF_BUFFER_HINTS_READABLE) perms |= AV_PERM_READ;
        if(pic->buffer_hints & FF_BUFFER_HINTS_PRESERVE) perms |= AV_PERM_PRESERVE;
        if(pic->buffer_hints & FF_BUFFER_HINTS_REUSABLE) perms |= AV_PERM_REUSE2;
    }
    if(pic->reference) perms |= AV_PERM_READ | AV_PERM_PRESERVE;

    w = codec->width;
    h = codec->height;
    avcodec_align_dimensions2(codec, &w, &h, stride);
    edge = codec->flags & CODEC_FLAG_EMU_EDGE ? 0 : avcodec_get_edge_width();
    w += edge << 1;
    h += edge << 1;

    if(!(ref = avfilter_get_video_buffer(ctx->outputs[0], perms, w, h)))
        return -1;

    ref->w = codec->width;
    ref->h = codec->height;
    for(i = 0; i < 3; i ++) {
        unsigned hshift = i == 0 ? 0 : av_pix_fmt_descriptors[ref->pic->format].log2_chroma_w;
        unsigned vshift = i == 0 ? 0 : av_pix_fmt_descriptors[ref->pic->format].log2_chroma_h;

        if (ref->data[i]) {
            ref->data[i]    += (edge >> hshift) + ((edge * ref->linesize[i]) >> vshift);
        }
        pic->data[i]     = ref->data[i];
        pic->linesize[i] = ref->linesize[i];
    }
    pic->opaque = ref;
    pic->age    = INT_MAX;
    pic->type   = FF_BUFFER_TYPE_USER;
    return 0;
}

static void input_release_buffer(AVCodecContext *codec, AVFrame *pic)
{
    memset(pic->data, 0, sizeof(pic->data));
    avfilter_unref_pic(pic->opaque);
}

static int input_init(AVFilterContext *ctx, const char *args, void *opaque)
{
    FilterPriv *priv = ctx->priv;
    AVCodecContext *codec;
    if(!opaque) return -1;

    priv->is = opaque;
    codec    = priv->is->video_st->codec;
    codec->opaque = ctx;
    if(codec->codec->capabilities & CODEC_CAP_DR1) {
        priv->use_dr1 = 1;
        codec->get_buffer     = input_get_buffer;
        codec->release_buffer = input_release_buffer;
    }

    priv->frame = avcodec_alloc_frame();

    return 0;
}

static void input_uninit(AVFilterContext *ctx)
{
    FilterPriv *priv = ctx->priv;
    av_free(priv->frame);
}

static int input_request_frame(AVFilterLink *link)
{
    FilterPriv *priv = link->src->priv;
    AVFilterPicRef *picref;
    int64_t pts = 0;
    AVPacket pkt;
    int ret;

    while (!(ret = get_video_frame(priv->is, priv->frame, &pts, &pkt)))
        av_free_packet(&pkt);
    if (ret < 0)
        return -1;

    if(priv->use_dr1) {
        picref = avfilter_ref_pic(priv->frame->opaque, ~0);
    } else {
        picref = avfilter_get_video_buffer(link, AV_PERM_WRITE, link->w, link->h);
        av_picture_copy((AVPicture *)&picref->data, (AVPicture *)priv->frame,
                        picref->pic->format, link->w, link->h);
    }
    av_free_packet(&pkt);

    picref->pts = pts;
    picref->pos = pkt.pos;
    picref->pixel_aspect = priv->is->video_st->codec->sample_aspect_ratio;
    avfilter_start_frame(link, picref);
    avfilter_draw_slice(link, 0, link->h, 1);
    avfilter_end_frame(link);

    return 0;
}

static int input_query_formats(AVFilterContext *ctx)
{
    FilterPriv *priv = ctx->priv;
    enum PixelFormat pix_fmts[] = {
        priv->is->video_st->codec->pix_fmt, PIX_FMT_NONE
    };

    avfilter_set_common_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

static int input_config_props(AVFilterLink *link)
{
    FilterPriv *priv  = link->src->priv;
    AVCodecContext *c = priv->is->video_st->codec;

    link->w = c->width;
    link->h = c->height;

    return 0;
}


AVFilterPad input_filter_inputs[] = {
	{0}
};

AVFilterPad input_filter_outputs[] = {
	{
		/*name*/ "default",
		/*type*/ AVMEDIA_TYPE_VIDEO,
		/*min_perms*/ 0,
		/*rej_perms*/ 0,
		/*start_frame*/ 0,
		/*get_video_buffer*/ 0,
		/*end_frame*/ 0,
		/*draw_slice*/ 0,
		/*poll_frame*/ 0,
		/*request_frame*/ input_request_frame,
		/*config_props*/ input_config_props
	},
	{0}
};

static AVFilter input_filter = {
#ifndef MSC_STRUCTS
    .name      = "ffplay_input",

    .priv_size = sizeof(FilterPriv),

    .init      = input_init,
    .uninit    = input_uninit,

    .query_formats = input_query_formats,

    .inputs    = (AVFilterPad[]) {{ .name = NULL }},
    .outputs   = (AVFilterPad[]) {{ .name = "default",
                                    .type = AVMEDIA_TYPE_VIDEO,
                                    .request_frame = input_request_frame,
                                    .config_props  = input_config_props, },
                                  { .name = NULL }},
};
#else
	/*name*/ "ffplay_input",
	/*priv_size*/ sizeof(FilterPriv),
	/*init*/ input_init,
	/*uninit*/ input_uninit,
	/*query_formats*/ input_query_formats,
	/*inputs*/ input_filter_inputs,
	/*outputs*/ input_filter_outputs,
	/*description*/ 0,
};
#endif

static void output_end_frame(AVFilterLink *link)
{
}

static int output_query_formats(AVFilterContext *ctx)
{
    enum PixelFormat pix_fmts[] = { PIX_FMT_YUYV422, PIX_FMT_YUV420P, PIX_FMT_RGB32, PIX_FMT_NONE };

    avfilter_set_common_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

static int get_filtered_video_frame(AVFilterContext *ctx, AVFrame *frame,
                                    int64_t *pts, int64_t *pos)
{
    AVFilterPicRef *pic;

    if(avfilter_request_frame(ctx->inputs[0]))
        return -1;
    if(!(pic = ctx->inputs[0]->cur_pic))
        return -1;
    ctx->inputs[0]->cur_pic = NULL;

    frame->opaque = pic;
    *pts          = pic->pts;
    *pos          = pic->pos;

    memcpy(frame->data,     pic->data,     sizeof(frame->data));
    memcpy(frame->linesize, pic->linesize, sizeof(frame->linesize));

    return 1;
}


AVFilterPad output_filter_inputs[] = {
	{
		/*name*/ "default",
		/*type*/ AVMEDIA_TYPE_VIDEO,
		/*min_perms*/ AV_PERM_READ,
		/*rej_perms*/ 0,
		/*start_frame*/ 0,
		/*get_video_buffer*/ 0,
		/*end_frame*/ output_end_frame,
		/*draw_slice*/ 0,
		/*poll_frame*/ 0,
		/*request_frame*/ 0,
		/*config_props*/ 0
	},
	{0}
};

AVFilterPad output_filter_outputs[] = {
	{0}
};

static AVFilter output_filter = {
#ifndef MSC_STRUCTS
    .name      = "ffplay_output",

    .query_formats = output_query_formats,

    .inputs    = (AVFilterPad[]) {{ .name          = "default",
                                    .type          = AVMEDIA_TYPE_VIDEO,
                                    .end_frame     = output_end_frame,
                                    .min_perms     = AV_PERM_READ, },
                                  { .name = NULL }},
    .outputs   = (AVFilterPad[]) {{ .name = NULL }},
};
#else
	/*name*/ "ffplay_output",
	/*priv_size*/ sizeof(FilterPriv),
	/*init*/ 0,
	/*uninit*/ 0,
	/*query_formats*/ output_query_formats,
	/*inputs*/ output_filter_inputs,
	/*outputs*/ output_filter_outputs,
	/*description*/ 0,
};
#endif
#endif  /* CONFIG_AVFILTER */

static int video_thread(void *arg)
{
    VideoState *is = arg;
    AVFrame *frame= avcodec_alloc_frame();
    int64_t pts_int;
    double pts;
    int ret;

#if CONFIG_AVFILTER
    int64_t pos;
    AVFilterContext *filt_src = NULL, *filt_out = NULL;
    AVFilterGraph *graph = av_mallocz(sizeof(AVFilterGraph));
    graph->scale_sws_opts = av_strdup("sws_flags=bilinear");

    if(!(filt_src = avfilter_open(&input_filter,  "src")))   goto the_end;
    if(!(filt_out = avfilter_open(&output_filter, "out")))   goto the_end;

    if(avfilter_init_filter(filt_src, NULL, is))             goto the_end;
    if(avfilter_init_filter(filt_out, NULL, frame))          goto the_end;


    if(vfilters) {
        AVFilterInOut *outputs = av_malloc(sizeof(AVFilterInOut));
        AVFilterInOut *inputs  = av_malloc(sizeof(AVFilterInOut));

        outputs->name    = av_strdup("in");
        outputs->filter  = filt_src;
        outputs->pad_idx = 0;
        outputs->next    = NULL;

        inputs->name    = av_strdup("out");
        inputs->filter  = filt_out;
        inputs->pad_idx = 0;
        inputs->next    = NULL;

        if (avfilter_graph_parse(graph, vfilters, inputs, outputs, NULL) < 0)
            goto the_end;
        av_freep(&vfilters);
    } else {
        if(avfilter_link(filt_src, 0, filt_out, 0) < 0)          goto the_end;
    }
    avfilter_graph_add_filter(graph, filt_src);
    avfilter_graph_add_filter(graph, filt_out);

    if(avfilter_graph_check_validity(graph, NULL))           goto the_end;
    if(avfilter_graph_config_formats(graph, NULL))           goto the_end;
    if(avfilter_graph_config_links(graph, NULL))             goto the_end;

    is->out_video_filter = filt_out;
#endif

    for(;;) {
#if !CONFIG_AVFILTER
        AVPacket pkt;
#endif
        while (is->paused && !is->videoq.abort_request)
            SDL_Delay(10);
#if CONFIG_AVFILTER
        ret = get_filtered_video_frame(filt_out, frame, &pts_int, &pos);
#else
        ret = get_video_frame(is, frame, &pts_int, &pkt);
#endif

        if (ret < 0) goto the_end;

        if (!ret)
            continue;

        pts = pts_int*av_q2d(is->video_st->time_base);

#if CONFIG_AVFILTER
        ret = output_picture2(is, frame, pts, pos);
#else
        ret = output_picture2(is, frame, pts,  pkt.pos);
        av_free_packet(&pkt);
#endif
        if (ret < 0)
            goto the_end;

        if (step)
            if (cur_stream)
                stream_pause(cur_stream);
    }
 the_end:
#if CONFIG_AVFILTER
    avfilter_graph_destroy(graph);
    av_freep(&graph);
#endif
    av_free(frame);
    return 0;
}

static int subtitle_thread(void *arg)
{
    VideoState *is = arg;
    SubPicture *sp;
    AVPacket pkt1, *pkt = &pkt1;
    int len1, got_subtitle;
    double pts;
    int i, j;
    int r, g, b, y, u, v, a;

    for(;;) {
        while (is->paused && !is->subtitleq.abort_request) {
            SDL_Delay(10);
        }
        if (packet_queue_get(&is->subtitleq, pkt, 1) < 0)
            break;

        if(pkt->data == flush_pkt.data){
            avcodec_flush_buffers(is->subtitle_st->codec);
            continue;
        }
        SDL_LockMutex(is->subpq_mutex);
        while (is->subpq_size >= SUBPICTURE_QUEUE_SIZE &&
               !is->subtitleq.abort_request) {
            SDL_CondWait(is->subpq_cond, is->subpq_mutex);
        }
        SDL_UnlockMutex(is->subpq_mutex);

        if (is->subtitleq.abort_request)
            goto the_end;

        sp = &is->subpq[is->subpq_windex];

       /* NOTE: ipts is the PTS of the _first_ picture beginning in
           this packet, if any */
        pts = 0;
        if (pkt->pts != AV_NOPTS_VALUE)
            pts = av_q2d(is->subtitle_st->time_base)*pkt->pts;

        len1 = avcodec_decode_subtitle2(is->subtitle_st->codec,
                                    &sp->sub, &got_subtitle,
                                    pkt);
//            if (len1 < 0)
//                break;
        if (got_subtitle && sp->sub.format == 0) {
            sp->pts = pts;

            for (i = 0; i < sp->sub.num_rects; i++)
            {
                for (j = 0; j < sp->sub.rects[i]->nb_colors; j++)
                {
                    RGBA_IN(r, g, b, a, (uint32_t*)sp->sub.rects[i]->pict.data[1] + j);
                    y = RGB_TO_Y_CCIR(r, g, b);
                    u = RGB_TO_U_CCIR(r, g, b, 0);
                    v = RGB_TO_V_CCIR(r, g, b, 0);
                    YUVA_OUT((uint32_t*)sp->sub.rects[i]->pict.data[1] + j, y, u, v, a);
                }
            }

            /* now we can update the picture count */
            if (++is->subpq_windex == SUBPICTURE_QUEUE_SIZE)
                is->subpq_windex = 0;
            SDL_LockMutex(is->subpq_mutex);
            is->subpq_size++;
            SDL_UnlockMutex(is->subpq_mutex);
        }
        av_free_packet(pkt);
//        if (step)
//            if (cur_stream)
//                stream_pause(cur_stream);
    }
 the_end:
    return 0;
}

/* copy samples for viewing in editor window */
static void update_sample_display(VideoState *is, short *samples, int samples_size)
{
    int size, len, channels;

    /*channels = is->audio_st->codec->channels;*/
	channels = is->output_channels;

    size = samples_size / sizeof(short);
    while (size > 0) {
        len = SAMPLE_ARRAY_SIZE - is->sample_array_index;
        if (len > size)
            len = size;
        memcpy(is->sample_array + is->sample_array_index, samples, len * sizeof(short));
        samples += len;
        is->sample_array_index += len;
        if (is->sample_array_index >= SAMPLE_ARRAY_SIZE)
            is->sample_array_index = 0;
        size -= len;
    }
}

/* return the new audio buffer size (samples can be added or deleted
   to get better sync if video or external master clock) */
static int synchronize_audio(VideoState *is, short *samples,
                             int samples_size1, double pts)
{
    int n, samples_size;
    double ref_clock;

    /*n = 2 * is->audio_st->codec->channels;*/
	n = 2 * is->output_channels;
    samples_size = samples_size1;

    /* if not master, then we try to remove or add samples to correct the clock */
    if (((is->av_sync_type == AV_SYNC_VIDEO_MASTER && is->video_st) ||
         is->av_sync_type == AV_SYNC_EXTERNAL_CLOCK)) {
        double diff, avg_diff;
        int wanted_size, min_size, max_size, nb_samples;

        ref_clock = get_master_clock(is);
        diff = get_audio_clock(is) - ref_clock;

        if (diff < AV_NOSYNC_THRESHOLD) {
            is->audio_diff_cum = diff + is->audio_diff_avg_coef * is->audio_diff_cum;
            if (is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
                /* not enough measures to have a correct estimate */
                is->audio_diff_avg_count++;
            } else {
                /* estimate the A-V difference */
                avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);

                if (fabs(avg_diff) >= is->audio_diff_threshold) {
                    wanted_size = samples_size + ((int)(diff * is->audio_st->codec->sample_rate) * n);
                    nb_samples = samples_size / n;

                    min_size = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX)) / 100) * n;
                    max_size = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX)) / 100) * n;
                    if (wanted_size < min_size)
                        wanted_size = min_size;
                    else if (wanted_size > max_size)
                        wanted_size = max_size;

                    /* add or remove samples to correction the synchro */
                    if (wanted_size < samples_size) {
                        /* remove samples */
                        samples_size = wanted_size;
                    } else if (wanted_size > samples_size) {
                        uint8_t *samples_end, *q;
                        int nb;

                        /* add samples */
                        nb = (samples_size - wanted_size);
                        samples_end = (uint8_t *)samples + samples_size - n;
                        q = samples_end + n;
                        while (nb > 0) {
                            memcpy(q, samples_end, n);
                            q += n;
                            nb -= n;
                        }
                        samples_size = wanted_size;
                    }
                }
#if 0
                printf("diff=%f adiff=%f sample_diff=%d apts=%0.3f vpts=%0.3f %f\n",
                       diff, avg_diff, samples_size - samples_size1,
                       is->audio_clock, is->video_clock, is->audio_diff_threshold);
#endif
            }
        } else {
            /* too big difference : may be initial PTS errors, so
               reset A-V filter */
            is->audio_diff_avg_count = 0;
            is->audio_diff_cum = 0;
        }
    }

    return samples_size;
}

/* decode one audio frame and returns its uncompressed size */
static int audio_decode_frame(VideoState *is, double *pts_ptr)
{
    AVPacket *pkt_temp = &is->audio_pkt_temp;
    AVPacket *pkt = &is->audio_pkt;
    AVCodecContext *dec= is->audio_st->codec;
	uint16_t *p;
    int n, len1, data_size;
    double pts;

	for(;;) {
        /* NOTE: the audio packet can contain several frames */
		while (pkt_temp->size > 0) {
            data_size = sizeof(is->audio_buf1);
            len1 = avcodec_decode_audio3(dec,
                                        (int16_t *)is->audio_buf1, &data_size,
                                        pkt_temp);
            if (len1 < 0) {
                /* if error, we skip the frame */
                pkt_temp->size = 0;
                break;
            }

            pkt_temp->data += len1;
            pkt_temp->size -= len1;
            if (data_size <= 0)
                continue;

            if (dec->sample_fmt != is->audio_src_fmt) {
                if (is->reformat_ctx)
                    av_audio_convert_free(is->reformat_ctx);
                is->reformat_ctx= av_audio_convert_alloc(SAMPLE_FMT_S16, 1,
                                                         dec->sample_fmt, 1, NULL, 0);
                if (!is->reformat_ctx) {
                    sprintf("Cannot convert %s sample format to %s sample format\n",
                        avcodec_get_sample_fmt_name(dec->sample_fmt),
                        avcodec_get_sample_fmt_name(SAMPLE_FMT_S16));
                        break;
                }
                is->audio_src_fmt= dec->sample_fmt;
            }

            if (is->reformat_ctx) {
                const void *ibuf[6]= {is->audio_buf1};
                void *obuf[6]= {is->audio_buf2};
                int istride[6]= {av_get_bits_per_sample_format(dec->sample_fmt)/8};
                int ostride[6]= {2};
                int len= data_size/istride[0];
                if (av_audio_convert(is->reformat_ctx, obuf, ostride, ibuf, istride, len)<0) {
                    printf("av_audio_convert() failed\n");
                    break;
                }
                is->audio_buf= is->audio_buf2;
                /* FIXME: existing code assume that data_size equals framesize*channels*2
                          remove this legacy cruft */
                data_size= len*2;
            }else{
                is->audio_buf= is->audio_buf1;
            }

			if (wanted_channel >= 0 && wanted_channel < dec->channels) {
                uint16_t *in_buf = (uint16_t *)is->audio_buf;
                uint16_t *out_buf = (uint16_t *)(is->reformat_ctx ? is->audio_buf1 : is->audio_buf2);
                int i = 0;
				for (p = (in_buf + wanted_channel); (p - in_buf) < data_size; p += (dec->channels))
				{
                    out_buf[i++] = *p;
				}
                is->audio_buf = (uint8_t *)out_buf;
                data_size /= dec->channels;
            }

            /* if no pts, then compute it */
            pts = is->audio_clock;
            *pts_ptr = pts;
            /* = 2 * dec->channels;*/
			n = 2 * is->output_channels;
            is->audio_clock += (double)data_size / (double)(n * dec->sample_rate);
#if defined(DEBUG_SYNC)
            {
                static double last_clock;
                printf("audio: delay=%0.3f clock=%0.3f pts=%0.3f\n",
                       is->audio_clock - last_clock,
                       is->audio_clock, pts);
                last_clock = is->audio_clock;
            }
#endif
            return data_size;
        }

        /* free the current packet */
        if (pkt->data)
            av_free_packet(pkt);

        if (is->paused || is->audioq.abort_request) {
            return -1;
        }

        /* read next packet */
        if (packet_queue_get(&is->audioq, pkt, 1) < 0)
            return -1;

		if(pkt->data == flush_pkt.data){
			avcodec_flush_buffers(dec);
			//continue;
		}

        pkt_temp->data = pkt->data;
        pkt_temp->size = pkt->size;

        /* if update the audio clock with the pts */
        if (pkt->pts != AV_NOPTS_VALUE) {
            is->audio_clock = av_q2d(is->audio_st->time_base)*pkt->pts;
        }
	}
}

/* get the current audio output buffer size, in samples. With SDL, we
   cannot have a precise information */
static int audio_write_get_buf_size(VideoState *is)
{
    return is->audio_buf_size - is->audio_buf_index;
}


/* prepare a new audio buffer */
static void sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
    VideoState *is = opaque;
    int audio_size, len1;
    double pts;

    audio_callback_time = av_gettime();

	while (len > 0) {
        if (is->audio_buf_index >= is->audio_buf_size) {
           audio_size = audio_decode_frame(is, &pts);
           if (audio_size < 0) {
                /* if error, just output silence */
               is->audio_buf = is->audio_buf1;
               is->audio_buf_size = 1024;
               memset(is->audio_buf, 0, is->audio_buf_size);
		   } else {
               if (is->show_audio)
                   update_sample_display(is, (int16_t *)is->audio_buf, audio_size);
               audio_size = synchronize_audio(is, (int16_t *)is->audio_buf, audio_size,
                                              pts);
               is->audio_buf_size = audio_size;
           }
           is->audio_buf_index = 0;
        }
        len1 = is->audio_buf_size - is->audio_buf_index;
        if (len1 > len)
            len1 = len;
        memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
}

/* since we have only one decoding thread, we can use a global
   variable instead of a thread local variable */
static VideoState *global_video_state;

static int decode_interrupt_cb(void)
{
    return (global_video_state && global_video_state->abort_request);
}

static void stream_component_close(VideoState *is, int stream_index)
{
    AVFormatContext *ic = is->ic;
    AVCodecContext *avctx;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return;
    avctx = ic->streams[stream_index]->codec;

    switch(avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        packet_queue_abort(&is->audioq);

        SDL_CloseAudio();

        packet_queue_end(&is->audioq);
        if (is->reformat_ctx)
            av_audio_convert_free(is->reformat_ctx);
        is->reformat_ctx = NULL;
        break;
    case AVMEDIA_TYPE_VIDEO:
        packet_queue_abort(&is->videoq);

        /* note: we also signal this mutex to make sure we deblock the
           video thread in all cases */
        SDL_LockMutex(is->pictq_mutex);
        SDL_CondSignal(is->pictq_cond);
        SDL_UnlockMutex(is->pictq_mutex);

        SDL_WaitThread(is->video_tid, NULL);

        packet_queue_end(&is->videoq);
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        packet_queue_abort(&is->subtitleq);

        /* note: we also signal this mutex to make sure we deblock the
           video thread in all cases */
        SDL_LockMutex(is->subpq_mutex);
        is->subtitle_stream_changed = 1;

        SDL_CondSignal(is->subpq_cond);
        SDL_UnlockMutex(is->subpq_mutex);

        SDL_WaitThread(is->subtitle_tid, NULL);

        packet_queue_end(&is->subtitleq);
        break;
    default:
        break;
    }

    ic->streams[stream_index]->discard = AVDISCARD_ALL;
    avcodec_close(avctx);
    switch(avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        is->audio_st = NULL;
        is->audio_stream = -1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->video_st = NULL;
        is->video_stream = -1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        is->subtitle_st = NULL;
        is->subtitle_stream = -1;
        break;
    default:
        break;
    }
}

/* this thread gets the stream from the disk or the network */
static int decode_thread(void *arg)
{
    int x;
	
    VideoState *is = arg;
    AVFormatContext *ic;
    int err, i, ret;
    int st_index[AVMEDIA_TYPE_NB];
    int st_count[AVMEDIA_TYPE_NB]={0};
    int st_best_packet_count[AVMEDIA_TYPE_NB];
    AVPacket pkt1, *pkt = &pkt1;
    AVFormatParameters params, *ap = &params;
    int eof=0;
    int pkt_in_play_range = 0;
	int64_t ts;
    int ns, mm, ss;
    int tns, tmm, tss;
	

    ic = avformat_alloc_context();

    memset(st_index, -1, sizeof(st_index));
    memset(st_best_packet_count, -1, sizeof(st_best_packet_count));
    is->video_stream = -1;
    is->audio_stream = -1;
    is->subtitle_stream = -1;

    global_video_state = is;
    url_set_interrupt_cb(decode_interrupt_cb);

    memset(ap, 0, sizeof(*ap));

    ap->prealloced_context = 1;
    ap->width = frame_width;
    ap->height= frame_height;
#ifndef _MSC_VER
    ap->time_base= (AVRational){1, 25};
#else
    ap->time_base= av_create_rational(1, 25);
#endif
    ap->pix_fmt = frame_pix_fmt;

    set_context_opts(ic, avformat_opts, AV_OPT_FLAG_DECODING_PARAM);

    err = av_open_input_file(&ic, is->filename, is->iformat, 0, ap);
    if (err < 0) {
        print_error(is->filename, err);
        ret = -1;
        goto fail;
    }
    is->ic = ic;

    if(genpts)
        ic->flags |= AVFMT_FLAG_GENPTS;

    err = av_find_stream_info(ic);
    if (err < 0) {
        sprintf("%s: could not find codec parameters\n", is->filename);
        ret = -1;
        goto fail;
    }
    if(ic->pb)
        ic->pb->eof_reached= 0; //FIXME hack, ffplay maybe should not use url_feof() to test for the end

    if(seek_by_bytes<0)
        seek_by_bytes= !!(ic->iformat->flags & AVFMT_TS_DISCONT);

    /* if seeking requested, we execute it */
    if (start_time != AV_NOPTS_VALUE) {
        int64_t timestamp;

        timestamp = start_time;
        /* add the stream start time */
        if (ic->start_time != AV_NOPTS_VALUE)
            timestamp += ic->start_time;
        ret = avformat_seek_file(ic, -1, INT64_MIN, timestamp, INT64_MAX, 0);
        if (ret < 0) {
            sprintf("%s: could not seek to position %0.3f\n",
                    is->filename, (double)timestamp / AV_TIME_BASE);
        }
    }

    for(i = 0; i < ic->nb_streams; i++) {
        AVStream *st= ic->streams[i];
        AVCodecContext *avctx = st->codec;
        ic->streams[i]->discard = AVDISCARD_ALL;
        if(avctx->codec_type >= (unsigned)AVMEDIA_TYPE_NB)
            continue;
        if(st_count[avctx->codec_type]++ != wanted_stream[avctx->codec_type] && wanted_stream[avctx->codec_type] >= 0)
            continue;

        if(st_best_packet_count[avctx->codec_type] >= st->codec_info_nb_frames)
            continue;
        st_best_packet_count[avctx->codec_type]= st->codec_info_nb_frames;

        switch(avctx->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            if (!audio_disable)
                st_index[AVMEDIA_TYPE_AUDIO] = i;
            break;
        case AVMEDIA_TYPE_VIDEO:
        case AVMEDIA_TYPE_SUBTITLE:
            if (!video_disable)
                st_index[avctx->codec_type] = i;
            break;
        default:
            break;
        }
    }
    if (show_status) {
        dump_format(ic, 0, is->filename, 0);
    }

    /* open the streams */
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
        stream_component_open(is, st_index[AVMEDIA_TYPE_AUDIO]);
    }

    ret=-1;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
        ret= stream_component_open(is, st_index[AVMEDIA_TYPE_VIDEO]);
    }
    is->refresh_tid = SDL_CreateThread(refresh_thread, is);
    if(ret<0) {
        if (!display_disable)
            is->show_audio = 2;
    }

    if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
        stream_component_open(is, st_index[AVMEDIA_TYPE_SUBTITLE]);
    }

    if (is->video_stream < 0 && is->audio_stream < 0) {
        sprintf("%s: could not open codecs\n", is->filename);
        ret = -1;
        goto fail;
    }

    for(;;) {
        if (is->abort_request)
            break;
        if (is->paused != is->last_paused) {
            is->last_paused = is->paused;
            if (is->paused)
                is->read_pause_return= av_read_pause(ic);
            else
                av_read_play(ic);
        }
#if CONFIG_RTSP_DEMUXER
        if (is->paused && !strcmp(ic->iformat->name, "rtsp")) {
            /* wait 10 ms to avoid trying to get another packet */
            /* XXX: horrible */
            SDL_Delay(10);
            continue;
        }
#endif
        if (is->seek_req) {
            int64_t seek_target= is->seek_pos;
            int64_t seek_min= is->seek_rel > 0 ? seek_target - is->seek_rel + 2: INT64_MIN;
            int64_t seek_max= is->seek_rel < 0 ? seek_target - is->seek_rel - 2: INT64_MAX;
//      of the seek_pos/seek_rel variables

            ret = avformat_seek_file(is->ic, -1, seek_min, seek_target, seek_max, is->seek_flags);
            if (ret < 0) {
                sprintf("%s: error while seeking\n", is->ic->filename);
            }else{
                if (is->audio_stream >= 0) {
                    packet_queue_flush(&is->audioq);
                    packet_queue_put(&is->audioq, &flush_pkt);
                }
                if (is->subtitle_stream >= 0) {
                    packet_queue_flush(&is->subtitleq);
                    packet_queue_put(&is->subtitleq, &flush_pkt);
                }
                if (is->video_stream >= 0) {
                    packet_queue_flush(&is->videoq);
                    packet_queue_put(&is->videoq, &flush_pkt);
                }
            }
            is->seek_req = 0;
            eof= 0;
        }

        /* if the queue are full, no need to read more */
        if (   is->audioq.size + is->videoq.size + is->subtitleq.size > MAX_QUEUE_SIZE
            || (   (is->audioq   .size  > MIN_AUDIOQ_SIZE || is->audio_stream<0)
                && (is->videoq   .nb_packets > MIN_FRAMES || is->video_stream<0)
                && (is->subtitleq.nb_packets > MIN_FRAMES || is->subtitle_stream<0))) {
            /* wait 10 ms */
            SDL_Delay(10);
            continue;
        }
        if(url_feof(ic->pb) || eof) {
            if(is->video_stream >= 0){
                av_init_packet(pkt);
                pkt->data=NULL;
                pkt->size=0;
                pkt->stream_index= is->video_stream;
                packet_queue_put(&is->videoq, pkt);
            }
            SDL_Delay(10);
            if(is->audioq.size + is->videoq.size + is->subtitleq.size ==0){
                if(loop==1){
                    stream_seek(cur_stream, start_time != AV_NOPTS_VALUE ? start_time : 0, 0, 0);
                }else if(autoexit){
                    ret=AVERROR_EOF;
                    goto fail;
                }
            }
            continue;
        }
        ret = av_read_frame(ic, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF)
                eof=1;
            if (url_ferror(ic->pb))
                break;
            SDL_Delay(10); /* wait for user event */
            continue;
        }
        /* check if packet is in play range specified by user, then queue, otherwise discard */
        pkt_in_play_range = duration == AV_NOPTS_VALUE ||
                (pkt->pts - ic->streams[pkt->stream_index]->start_time) *
                av_q2d(ic->streams[pkt->stream_index]->time_base) -
                (double)(start_time != AV_NOPTS_VALUE ? start_time : 0)/1000000
                <= ((double)duration/1000000);
        if (pkt->stream_index == is->audio_stream && pkt_in_play_range) {
            packet_queue_put(&is->audioq, pkt);
        } else if (pkt->stream_index == is->video_stream && pkt_in_play_range) {
            packet_queue_put(&is->videoq, pkt);
        } else if (pkt->stream_index == is->subtitle_stream && pkt_in_play_range) {
            packet_queue_put(&is->subtitleq, pkt);
        } else {
            av_free_packet(pkt);
        }
		
        tns = cur_stream->ic->duration/1000000LL;
        tmm = (tns)/60;
        tss = (tns%60);
        ns = get_master_clock(cur_stream);
        mm = (ns)/60;
        ss = (ns%60);
        sprintf_s(info, 255,"%s, %02d:%02d of %02d:%02d", filename, mm, ss, tmm, tss);
    }
    /* wait until the end */
    while (!is->abort_request) {
        SDL_Delay(10);
    }

    ret = 0;
 fail:
    /* disable interrupting */
    global_video_state = NULL;

    /* close each stream */
    if (is->audio_stream >= 0)
        stream_component_close(is, is->audio_stream);
    if (is->video_stream >= 0)
        stream_component_close(is, is->video_stream);
    if (is->subtitle_stream >= 0)
        stream_component_close(is, is->subtitle_stream);
    if (is->ic) {
        av_close_input_file(is->ic);
        is->ic = NULL; /* safety */
    }
    url_set_interrupt_cb(NULL);

    if (ret != 0) {
        SDL_Event event;

        event.type = FF_QUIT_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);
    }
    return 0;
}

static VideoState *stream_open(const char *filename, AVInputFormat *iformat)
{
    VideoState *is;

    is = av_mallocz(sizeof(VideoState));
    if (!is)
        return NULL;
    av_strlcpy(is->filename, filename, sizeof(is->filename));
    is->iformat = iformat;
    is->ytop = 0;
    is->xleft = 0;

    /* start video display */
    is->pictq_mutex = SDL_CreateMutex();
    is->pictq_cond = SDL_CreateCond();

    is->subpq_mutex = SDL_CreateMutex();
    is->subpq_cond = SDL_CreateCond();

    is->av_sync_type = av_sync_type;
    is->parse_tid = SDL_CreateThread(decode_thread, is);
    if (!is->parse_tid) {
        av_free(is);
        return NULL;
    }
    return is;
}

static void stream_close(VideoState *is)
{
    VideoPicture *vp;
    int i;
    /* XXX: use a special url_shutdown call to abort parse cleanly */
    is->abort_request = 1;
    SDL_WaitThread(is->parse_tid, NULL);
    SDL_WaitThread(is->refresh_tid, NULL);

    /* free all pictures */
    for(i=0;i<VIDEO_PICTURE_QUEUE_SIZE; i++) {
        vp = &is->pictq[i];
#if CONFIG_AVFILTER
        if (vp->picref) {
            avfilter_unref_pic(vp->picref);
            vp->picref = NULL;
        }
#endif
        if (vp->bmp) {
            SDL_FreeYUVOverlay(vp->bmp);
            vp->bmp = NULL;
        }
    }
    SDL_DestroyMutex(is->pictq_mutex);
    SDL_DestroyCond(is->pictq_cond);
    SDL_DestroyMutex(is->subpq_mutex);
    SDL_DestroyCond(is->subpq_cond);
#if !CONFIG_AVFILTER
    if (is->img_convert_ctx)
        sws_freeContext(is->img_convert_ctx);
#endif
    av_free(is);
}

static void do_exit(void)
{
    int i;
    SDL_Event event;
	const char *retApp;

	if (cur_stream) {
        stream_close(cur_stream);
        cur_stream = NULL;
    }
    for (i = 0; i < AVMEDIA_TYPE_NB; i++)
        //av_free(avcodec_opts[i]); //Hack?

    //av_free(avformat_opts);//Hack?
    //av_free(sws_opts);//Hack?
#if CONFIG_AVFILTER
    avfilter_uninit();//Hack?
#endif
    if (show_status)
        printf("\n");

        event.type = FF_QUIT_EVENT;
        //event.user.data1 = is;
        SDL_PushEvent(&event);

    SDL_Quit();
	
	retApp = getReturnApp();
	XLaunchNewImage(retApp, XLAUNCH_FLAG_CLEAR_LAUNCH_DATA);
	
    return;
}
/*
static void do_xmv(void)
{
    int i;
    global_video_state = NULL;
    if (cur_stream->audio_stream >= 0)
        stream_component_close(cur_stream, cur_stream->audio_stream);
    if (cur_stream->video_stream >= 0)
        stream_component_close(cur_stream, cur_stream->video_stream);
    if (cur_stream->subtitle_stream >= 0)
        stream_component_close(cur_stream, cur_stream->subtitle_stream);
    if (cur_stream->ic) {
        av_close_input_file(cur_stream->ic);
        cur_stream->ic = NULL; 
    }
	if (cur_stream) {
        //stream_close(cur_stream);
        cur_stream = NULL;
    }
    //for (i = 0; i < AVMEDIA_TYPE_NB; i++)
        //av_free(avcodec_opts[i]); //Hack?

    //av_free(avformat_opts);//Hack?
    //av_free(sws_opts);//Hack?
#if CONFIG_AVFILTER
    avfilter_uninit();//Hack?
#endif
    if (show_status)
        printf("\n");
    SDL_Quit();
	//printf("Passing to xmv player filename - %s\n", filename);
	XMVMain(filename);
	XLaunchNewImage("ffplay.xex",0);
	filename = getFileManager();
	startNewVideo(filename);
    return;
}*/
/* open a given stream. Return 0 if OK */
static int stream_component_open(VideoState *is, int stream_index)
{
    AVFormatContext *ic = is->ic;
    AVCodecContext *avctx;
    AVCodec *codec;
    SDL_AudioSpec wanted_spec, spec;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return -1;
    avctx = ic->streams[stream_index]->codec;

    /* downmix to stereo if codec can 
    if (avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        if (avctx->channels > 0) {
            //avctx->request_channels = FFMIN(2, avctx->channels);
			avctx->request_channel_layout = AV_CH_LAYOUT_STEREO_DOWNMIX;
            avctx->request_channels = 2;
        }
    }*/
    codec = avcodec_find_decoder(avctx->codec_id);
    avctx->debug_mv = debug_mv;
    avctx->debug = debug;
    avctx->workaround_bugs = workaround_bugs;
    if(lowres)
		avctx->flags |= CODEC_FLAG_EMU_EDGE;
	avctx->idct_algo= idct;
    if(fast)
		avctx->flags2 |= CODEC_FLAG2_FAST;
	avctx->skip_frame= skip_frame;
		/* set idct frame skip by codec id's */
	switch(avctx->codec_type){
		case AVMEDIA_TYPE_VIDEO:
			switch(avctx->codec_id){
				//case 18: //wmv1
				//case 19: //wmv2
				//case 74: //wmv3
					//do_xmv();
					//break;
				case 4: //h261
				case 5: //h263
				case 20: //H263P
				case 21: //H263I
				case 28: //h264
				case 101: //FFH264
				case 63: //xvid
					avctx->skip_idct = AVDISCARD_NONKEY; //AVDISCARD_ALL; //AVDISCARD_NONREF;
					break;
				default:
					avctx->skip_idct= skip_idct;
			}
			break;
		default:
			break;
	}
    avctx->skip_loop_filter= skip_loop_filter;
    avctx->error_recognition= error_recognition;
    avctx->error_concealment= error_concealment;

	avctx->thread_count = thread_count;
	//avctx->active_thread_type = thread_type;
	if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) { /*DIRTY HACK*/
		if (codec->capabilities & CODEC_CAP_FRAME_THREADS) {
			avctx->active_thread_type = FF_THREAD_FRAME;
		} else {
			avctx->active_thread_type = FF_THREAD_SLICE;
		}
	} else {/*DIRTY HACK*/
		avctx->active_thread_type = thread_type;
	}/*DIRTY HACK*/
// do we need this? 
//    avcodec_thread_init(avctx, thread_count); //Don't need this.

    set_context_opts(avctx, avcodec_opts[avctx->codec_type], 0);

	if (!codec || avcodec_open(avctx, codec) < 0)
		return -1;

	if(low_res && (avctx->codec->max_lowres >= 1))
		avctx->lowres = avctx->codec->max_lowres;

    /* prepare audio output */
	if (avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
		if (wanted_channel >= 0 && wanted_channel < avctx->channels){
			avctx->request_channel_layout = AV_CH_LAYOUT_STEREO_DOWNMIX;
            is->output_channels = avctx->request_channels = 2;
		} else
            is->output_channels = avctx->request_channels = avctx->channels;

		wanted_spec.freq = avctx->sample_rate;
        wanted_spec.format = AUDIO_S16SYS;
		wanted_spec.channels = is->output_channels; //avctx->request_channels;
        wanted_spec.silence = 0;
        wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
        wanted_spec.callback = sdl_audio_callback;
        wanted_spec.userdata = is;
        if (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
            sprintf("SDL_OpenAudio: %s\n", SDL_GetError());
            return -1;
        }
        is->audio_hw_buf_size = spec.size;
        is->audio_src_fmt= SAMPLE_FMT_S16;
    }

    ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
	switch(avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        is->audio_stream = stream_index;
        is->audio_st = ic->streams[stream_index];
        is->audio_buf_size = 0;
        is->audio_buf_index = 0;

        /* init averaging filter */
        is->audio_diff_avg_coef = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
        is->audio_diff_avg_count = 0;
        /* since we do not have a precise anough audio fifo fullness,
           we correct audio sync only if larger than this threshold */
        is->audio_diff_threshold = 2.0 * SDL_AUDIO_BUFFER_SIZE / avctx->sample_rate;

        memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
        packet_queue_init(&is->audioq);
        SDL_PauseAudio(0);
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->video_stream = stream_index;
        is->video_st = ic->streams[stream_index];

        //is->video_current_pts_time = av_gettime(); //hack

        packet_queue_init(&is->videoq);
        is->video_tid = SDL_CreateThread(video_thread, is);
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        is->subtitle_stream = stream_index;
        is->subtitle_st = ic->streams[stream_index];
        packet_queue_init(&is->subtitleq);

        is->subtitle_tid = SDL_CreateThread(subtitle_thread, is);
        break;
    default:
        break;
    }
    return 0;
}

static void stream_cycle_channel(VideoState *is, int codec_type)
{
    AVFormatContext *ic = is->ic;
    int start_index, stream_index;
    AVStream *st;

    if (codec_type == AVMEDIA_TYPE_VIDEO)
        start_index = is->video_stream;
    else if (codec_type == AVMEDIA_TYPE_AUDIO)
        start_index = is->audio_stream;
    else
        start_index = is->subtitle_stream;
    if (start_index < (codec_type == AVMEDIA_TYPE_SUBTITLE ? -1 : 0))
        return;
    stream_index = start_index;
    for(;;) {
        if (++stream_index >= is->ic->nb_streams)
        {
            if (codec_type == AVMEDIA_TYPE_SUBTITLE)
            {
                stream_index = -1;
                goto the_end;
            } else
                stream_index = 0;
        }
        if (stream_index == start_index)
            return;
        st = ic->streams[stream_index];
        if (st->codec->codec_type == codec_type) {
            /* check that parameters are OK */
            switch(codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                if (st->codec->sample_rate != 0 &&
                    st->codec->channels != 0)
                    goto the_end;
                break;
            case AVMEDIA_TYPE_VIDEO:
            case AVMEDIA_TYPE_SUBTITLE:
                goto the_end;
            default:
                break;
            }
        }
    }
 the_end:
    stream_component_close(is, start_index);
    stream_component_open(is, stream_index);
}

static void stream_cycle_audio_channel(VideoState *is)
{
    int stream_index = is->audio_stream;
    if (stream_index < 0)
        return;
    wanted_channel = FFMAX(wanted_channel, -1);
    if (++wanted_channel >= is->ic->streams[stream_index]->codec->channels)
        wanted_channel = -1;
    stream_component_close(is, stream_index);
    stream_component_open(is, stream_index);
}

static void toggle_full_screen(void)
{
    is_full_screen = !is_full_screen;
    if (!fs_screen_width) {
        /* use default SDL method */
        SDL_WM_ToggleFullScreen(screen);
    }
    video_open(cur_stream);
}

static void toggle_pause(void)
{
    if (cur_stream)
        stream_pause(cur_stream);
    step = 0;
}

static void step_to_next_frame(void)
{
    if (cur_stream) {
        /* if the stream is paused unpause it, then step */
        if (cur_stream->paused)
            stream_pause(cur_stream);
    }
    step = 1;
}

static void toggle_audio_display(void)
{
    if (cur_stream) {
        int bgcolor = SDL_MapRGB(screen->format, 0x00, 0x00, 0x00);
        cur_stream->show_audio = (cur_stream->show_audio + 1) % 3;
        fill_rectangle(screen,
                    cur_stream->xleft, cur_stream->ytop, cur_stream->width, cur_stream->height,
                    bgcolor);
        SDL_UpdateRect(screen, cur_stream->xleft, cur_stream->ytop, cur_stream->width, cur_stream->height);
    }
}
static void new_seek(double syncr)
{
	double curpos;
    if (cur_stream) {
        if (seek_by_bytes) {
            if (cur_stream->video_stream >= 0 && cur_stream->video_current_pos>=0){
                curpos= cur_stream->video_current_pos;
            }else if(cur_stream->audio_stream >= 0 && cur_stream->audio_pkt.pos>=0){
                curpos= cur_stream->audio_pkt.pos;
            }else
                curpos = url_ftell(cur_stream->ic->pb);
            if (cur_stream->ic->bit_rate)
                syncr *= cur_stream->ic->bit_rate / 8.0;
            else
                syncr *= 180000.0;
            curpos += syncr;
            stream_seek(cur_stream, curpos, syncr, 1);
        } else {
            curpos = get_master_clock(cur_stream);
            curpos += syncr;
            stream_seek(cur_stream, (int64_t)(curpos * AV_TIME_BASE), (int64_t)(syncr * AV_TIME_BASE), 0);
        }
    }
}

static void btnPlay() {
	if (MM.Active == Play)
		toggle_pause();
	else if (MM.Active == Stop)
		toggle_pause();
	else if (MM.Active == Pause)
		toggle_pause();
	MM.Active = Play;
	XEnableScreenSaver(TRUE);
}

static void btnPause() {
	if (MM.Active == Pause) {
		XEnableScreenSaver(FALSE);
		toggle_pause();
		MM.Active = Play;
	} else if (MM.Active == Play) {
		XEnableScreenSaver(TRUE);
		toggle_pause();
		MM.Active = Pause;
	} else if (MM.Active == FForward) {
		toggle_pause();
		MM.Active = Pause;
	} else if (MM.Active == Rewind) {
		toggle_pause();
		MM.Active = Rewind;
	}
}

static void btnRewind() {
	if (MM.Active == Rewind) {
		MM.Active = Play;
		skipWaitCount = 0;
	} else {
		MM.Active = Rewind;
	}
}

static void btnFForward() {
	if (MM.Active == FForward) {
		MM.Active = Play;
		skipWaitCount = 0;
	} else {
		MM.Active = FForward;
	}
}

static void btnStop() {
	if (MM.Active == Play) {
		XEnableScreenSaver(TRUE);
		MM.Active = Stop;
		new_seek(-get_master_clock(cur_stream));
		toggle_pause();
	} else if (MM.Active == FForward) {
		XEnableScreenSaver(TRUE);
		MM.Active = Stop;
		new_seek(-get_master_clock(cur_stream));
		toggle_pause();
	} else if (MM.Active == Rewind) {
		XEnableScreenSaver(TRUE);
		MM.Active = Stop;
		new_seek(-get_master_clock(cur_stream));
		toggle_pause();
	}
}

/* handle an event sent by the GUI */
static void event_loop(void)
{
    SDL_Event event;
    double incr, pos, frac;
	double x = 0;
	overlay = 0;
	olWaitCount = 0;
	skipWaitCount = 0;
	skipTriggerWait = 0;
	joy = SDL_JoystickOpen(0);
	if (caching_delay >= 1) {
		toggle_pause();
		Sleep(caching_delay);
		toggle_pause();
	}
    for(;;) {
		// *** XBOX *****
		SDL_Event fakeevent;
		// ****** XBOX ******
		//ANALOG STICK HERE

		int q = (SDL_JoystickGetAxis(joy, 0));
		int r = (SDL_JoystickGetAxis(joy, 1));
		int g = (SDL_JoystickGetAxis(joy, 2));
		int h = (SDL_JoystickGetAxis(joy, 3));
		SDL_Delay(10); //keep this
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
			/*if (r <= -9000)
			{
				fakeevent.type = SDL_KEYDOWN;
				fakeevent.key.type = SDL_KEYDOWN;
				fakeevent.key.keysym.sym = SDLK_KP8;
				SDL_PushEvent (&fakeevent);
				//return;
			}*/
			/*if (r >= 9000)
			{
				fakeevent.type = SDL_KEYDOWN;
				fakeevent.key.type = SDL_KEYDOWN;
				fakeevent.key.keysym.sym = SDLK_KP2;
				SDL_PushEvent (&fakeevent);
				//return;
			}*/
			// Left
			if (q <= -9000 && overlay)
			{
				olWaitCount = 25;
				fakeevent.type = SDL_KEYDOWN;
				fakeevent.key.type = SDL_KEYDOWN;
				fakeevent.key.keysym.sym = SDLK_KP4;
				SDL_PushEvent (&fakeevent);
				//return;
			}
			// Right
			if (q >= 9000 && overlay)
			{
				olWaitCount = 25;
				fakeevent.type = SDL_KEYDOWN;
				fakeevent.key.type = SDL_KEYDOWN;
				fakeevent.key.keysym.sym = SDLK_KP6;
				SDL_PushEvent (&fakeevent);
				//return;
			}
		} else if (olWaitCount > 0) {
			olWaitCount--;
		}
		if (skipWaitCount >0) {
			skipWaitCount--;
		}
		if (MM.Active == FForward && skipWaitCount == 0) {
			skipWaitCount = 150;
			new_seek(30.0);
		} else if (MM.Active == Rewind && skipWaitCount == 0) {
			skipWaitCount = 150;
			new_seek(-30.0);
		}
        SDL_WaitEvent(&event);
		if (event.type == SDL_JOYBUTTONDOWN)
		{
			/*if (SDL_JoystickGetButton(joy, XBOX_BUTTON_L)){
				new_seek(-10.0);
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_R)){
				new_seek(10.0);
			}*/
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_TRIGL)){
				if (skipWaitCount == 0 &&
					MM.Active != Rewind &&
					MM.Active != FForward) {
					new_seek(-60.0);
					skipWaitCount = 150;
				}
			}
		    if (SDL_JoystickGetButton(joy, XBOX_BUTTON_TRIGR)){
				if (skipWaitCount == 0 &&
					MM.Active != Rewind &&
					MM.Active != FForward) {
					new_seek(60.0);
					skipWaitCount = 150;
				}
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_CLICK1)){
				if (cur_stream)
                    stream_cycle_audio_channel(cur_stream);
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_CLICK2)){
				if (cur_stream){
					printf("toggled subs\n");
                    stream_cycle_channel(cur_stream, AVMEDIA_TYPE_SUBTITLE);
				}
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_SELECT)){
				do_exit();
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_START)){
				toggle_pause();
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_X)) {
				if (SizeFlag & SDL_FULLSCREEN) {
					SizeFlag = SDL_RESIZABLE;
				} else if (SizeFlag & SDL_RESIZABLE) {
					SizeFlag = SDL_NoSize;
				} else {
					SizeFlag = SDL_FULLSCREEN;
				}
				
				fakeevent.type = SDL_VIDEORESIZE;
				fakeevent.resize.h = SizeFlag;
				SDL_PushEvent (&fakeevent);
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_Y)){
				toggle_audio_display();
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_B)){
				overlay = 0;
			}
			if (SDL_JoystickGetButton(joy, XBOX_BUTTON_A)){
				//toggle_pause();
				if (!overlay)
					overlay = 1;
				else {
					switch (MM.Highlighted) {
					case Play:
						btnPlay();
						break;
					case Pause:
						btnPause();
						break;
					case Stop:
						btnStop();
						break;
					case FForward:
						btnFForward();
						break;
					case Rewind:
						btnRewind();
						break;
					default:
						overlay = 0;
					}
				}
			}
		}
		if (event.type == SDL_JOYBUTTONUP)
		{
		}
        switch(event.type) {
        case SDL_KEYDOWN:
            switch(event.key.keysym.sym) {
			case SDLK_KP6:
				MM.Highlighted++;
				if (MM.Highlighted >= None)
					MM.Highlighted = None -1;
				if (cur_stream->paused) {
					displayMenu();
					SDL_Flip(screen);
					SDL_Present();
				}
				break;
			case SDLK_KP4:
				MM.Highlighted--;
				if (MM.Highlighted < 0)
					MM.Highlighted = Play;
				if (cur_stream->paused) {
					displayMenu();
					SDL_Flip(screen);
					SDL_Present();
				}
				break;
        
            case SDLK_ESCAPE:
            case SDLK_q:
                do_exit();
                break;
            case SDLK_f:
                toggle_full_screen();
                break;
            case SDLK_p:
            case SDLK_SPACE:
                toggle_pause();
                break;
            case SDLK_s: //S: Step to next frame
                step_to_next_frame();
                break;
            case SDLK_a:
                if (cur_stream)
                    stream_cycle_channel(cur_stream, AVMEDIA_TYPE_AUDIO);
                break;
            case SDLK_v:
                if (cur_stream)
                    stream_cycle_channel(cur_stream, AVMEDIA_TYPE_VIDEO);
                break;
            case SDLK_t:
                if (cur_stream)
                    stream_cycle_channel(cur_stream, AVMEDIA_TYPE_SUBTITLE);
                break;
            case SDLK_w:
                toggle_audio_display();
                break;
            case SDLK_UP:
                incr = -10.0;
                goto do_seek;
            case SDLK_DOWN:
                incr = 10.0;
                goto do_seek;
            case SDLK_LEFT:
                incr = 60.0;
                goto do_seek;
            case SDLK_RIGHT:
                incr = -60.0;
            do_seek:
                if (cur_stream) {
                    if (seek_by_bytes) {
                        if (cur_stream->video_stream >= 0 && cur_stream->video_current_pos>=0){
                            pos= cur_stream->video_current_pos;
                        }else if(cur_stream->audio_stream >= 0 && cur_stream->audio_pkt.pos>=0){
                            pos= cur_stream->audio_pkt.pos;
                        }else
                            pos = url_ftell(cur_stream->ic->pb);
                        if (cur_stream->ic->bit_rate)
                            incr *= cur_stream->ic->bit_rate / 8.0;
                        else
                            incr *= 180000.0;
                        pos += incr;
                        stream_seek(cur_stream, pos, incr, 1);
                    } else {
                        pos = get_master_clock(cur_stream);
                        pos += incr;
                        stream_seek(cur_stream, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
                    }
                }
                break;
            default:
                break;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEMOTION:
            if(event.type ==SDL_MOUSEBUTTONDOWN){
                x= event.button.x;
            }else{
                if(event.motion.state != SDL_PRESSED)
                    break;
                x= event.motion.x;
            }
            if (cur_stream) {
                if(seek_by_bytes || cur_stream->ic->duration<=0){
                    uint64_t size=  url_fsize(cur_stream->ic->pb);
                    stream_seek(cur_stream, size*x/cur_stream->width, 0, 1);
                }else{
                    int64_t ts;
                    int ns, hh, mm, ss;
                    int tns, thh, tmm, tss;
                    tns = cur_stream->ic->duration/1000000LL;
                    thh = tns/3600;
                    tmm = (tns%3600)/60;
                    tss = (tns%60);
                    frac = x/cur_stream->width;
                    ns = frac*tns;
                    hh = ns/3600;
                    mm = (ns%3600)/60;
                    ss = (ns%60);
                    printf("Seek to (%2d:%02d:%02d) of total duration (%2d:%02d:%02d) \n", hh, mm, ss, thh, tmm, tss);
                    ts = frac*cur_stream->ic->duration;
                    if (cur_stream->ic->start_time != AV_NOPTS_VALUE)
                        ts += cur_stream->ic->start_time;
                    stream_seek(cur_stream, ts, 0, 0);
                }
            }
            break;
        case SDL_VIDEORESIZE:
            if (cur_stream) {
				screen = SDL_SetVideoMode(screen->w, screen->h, 0,
					SizeFlag|SDL_HWSURFACE|SDL_ASYNCBLIT|SDL_HWACCEL|SDL_DOUBLEBUF);
                screen_width = cur_stream->width; //= event.resize.w;
                screen_height= cur_stream->height; //= event.resize.h;
            }
            break;
        case SDL_QUIT:
        case FF_QUIT_EVENT:
            do_exit();
            break;
        case FF_ALLOC_EVENT:
            video_open((VideoState*)event.user.data1);
            alloc_picture(event.user.data1);
            break;
        case FF_REFRESH_EVENT:
            video_refresh_timer(event.user.data1);
            cur_stream->refresh=0;
            break;
        default:
            break;
        }
    }
}

static void opt_frame_size(const char *arg)
{
    if (av_parse_video_frame_size(&frame_width, &frame_height, arg) < 0) {
        printf("Incorrect frame size %d x %d\n",frame_width,frame_height);
        return;
    }
    if ((frame_width % 2) != 0 || (frame_height % 2) != 0) {
        printf("Frame size must be a multiple of 2 %d x %d\n",frame_width,frame_height);
        return;
    }
}

static int opt_width(const char *opt, const char *arg)
{
    screen_width = parse_number_or_die(opt, arg, OPT_INT64, 1, INT_MAX);
    return 0;
}

static int opt_height(const char *opt, const char *arg)
{
    screen_height = parse_number_or_die(opt, arg, OPT_INT64, 1, INT_MAX);
    return 0;
}

static void opt_format(const char *arg)
{
    file_iformat = av_find_input_format(arg);
    if (!file_iformat) {
        sprintf("Unknown input format: %s\n", arg);
        return;
    }
}

static void opt_frame_pix_fmt(const char *arg)
{
    frame_pix_fmt = av_get_pix_fmt(arg);
}

static int opt_sync(const char *opt, const char *arg)
{
    if (!strcmp(arg, "audio"))
        av_sync_type = AV_SYNC_AUDIO_MASTER;
    else if (!strcmp(arg, "video"))
        av_sync_type = AV_SYNC_VIDEO_MASTER;
    else if (!strcmp(arg, "ext"))
        av_sync_type = AV_SYNC_EXTERNAL_CLOCK;
    else {
        sprintf("Unknown value for %s: %s\n", opt, arg);
        return(-1);
    }
    return 0;
}

static int opt_seek(const char *opt, const char *arg)
{
    start_time = parse_time_or_die(opt, arg, 1);
    return 0;
}

static int opt_duration(const char *opt, const char *arg)
{
    duration = parse_time_or_die(opt, arg, 1);
    return 0;
}

static int opt_debug(const char *opt, const char *arg)
{
    av_log_set_level(99);
    debug = parse_number_or_die(opt, arg, OPT_INT64, 0, INT_MAX);
    return 0;
}

static int opt_vismv(const char *opt, const char *arg)
{
    debug_mv = parse_number_or_die(opt, arg, OPT_INT64, INT_MIN, INT_MAX);
    return 0;
}

static int opt_thread_count(const char *opt, const char *arg)
{
    thread_count= parse_number_or_die(opt, arg, OPT_INT64, 0, INT_MAX);
#if !HAVE_THREADS
    sprintf("Warning: not compiled with thread support, using thread emulation\n");
#endif
    return 0;
}

static const OptionDef options[] = {
#include "cmdutils_common_opts.h"
    { "x", HAS_ARG | OPT_FUNC2, {(void*)opt_width}, "force displayed width", "width" },
    { "y", HAS_ARG | OPT_FUNC2, {(void*)opt_height}, "force displayed height", "height" },
    { "s", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_size}, "set frame size (WxH or abbreviation)", "size" },
    { "fs", OPT_BOOL, {(void*)&is_full_screen}, "force full screen" },
    { "an", OPT_BOOL, {(void*)&audio_disable}, "disable audio" },
    { "vn", OPT_BOOL, {(void*)&video_disable}, "disable video" },
    { "ast", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&wanted_stream[AVMEDIA_TYPE_AUDIO]}, "select desired audio stream", "stream_number" },
    { "vst", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&wanted_stream[AVMEDIA_TYPE_VIDEO]}, "select desired video stream", "stream_number" },
    { "sst", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&wanted_stream[AVMEDIA_TYPE_SUBTITLE]}, "select desired subtitle stream", "stream_number" },
    { "ss", HAS_ARG | OPT_FUNC2, {(void*)&opt_seek}, "seek to a given position in seconds", "pos" },
    { "t", HAS_ARG | OPT_FUNC2, {(void*)&opt_duration}, "play  \"duration\" seconds of audio/video", "duration" },
    { "bytes", OPT_INT | HAS_ARG, {(void*)&seek_by_bytes}, "seek by bytes 0=off 1=on -1=auto", "val" },
    { "nodisp", OPT_BOOL, {(void*)&display_disable}, "disable graphical display" },
    { "f", HAS_ARG, {(void*)opt_format}, "force format", "fmt" },
    { "pix_fmt", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_frame_pix_fmt}, "set pixel format", "format" },
    { "stats", OPT_BOOL | OPT_EXPERT, {(void*)&show_status}, "show status", "" },
    { "debug", HAS_ARG | OPT_FUNC2 | OPT_EXPERT, {(void*)opt_debug}, "print specific debug info", "" },
    { "bug", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&workaround_bugs}, "workaround bugs", "" },
    { "vismv", HAS_ARG | OPT_FUNC2 | OPT_EXPERT, {(void*)opt_vismv}, "visualize motion vectors", "" },
    { "fast", OPT_BOOL | OPT_EXPERT, {(void*)&fast}, "non spec compliant optimizations", "" },
    { "genpts", OPT_BOOL | OPT_EXPERT, {(void*)&genpts}, "generate pts", "" },
    { "drp", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&decoder_reorder_pts}, "let decoder reorder pts 0=off 1=on -1=auto", ""},
    { "lowres", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&lowres}, "", "" },
    { "skiploop", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&skip_loop_filter}, "", "" },
    { "skipframe", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&skip_frame}, "", "" },
    { "skipidct", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&skip_idct}, "", "" },
    { "idct", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&idct}, "set idct algo",  "algo" },
    { "er", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&error_recognition}, "set error detection threshold (0-4)",  "threshold" },
    { "ec", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&error_concealment}, "set error concealment options",  "bit_mask" },
    { "sync", HAS_ARG | OPT_FUNC2 | OPT_EXPERT, {(void*)opt_sync}, "set audio-video sync. type (type=audio/video/ext)", "type" },
    { "threads", HAS_ARG | OPT_FUNC2 | OPT_EXPERT, {(void*)opt_thread_count}, "thread count", "count" },
    { "autoexit", OPT_BOOL | OPT_EXPERT, {(void*)&autoexit}, "exit at the end", "" },
    { "loop", OPT_INT | HAS_ARG | OPT_EXPERT, {(void*)&loop}, "set number of times the playback shall be looped", "loop count" },
    { "framedrop", OPT_BOOL | OPT_EXPERT, {(void*)&framedrop}, "drop frames when cpu is too slow", "" },
    { "window_title", OPT_STRING | HAS_ARG, {(void*)&window_title}, "set window title", "window title" },
#if CONFIG_AVFILTER
    { "vfilters", OPT_STRING | HAS_ARG, {(void*)&vfilters}, "video filters", "filter list" },
#endif
    { "rdftspeed", OPT_INT | HAS_ARG| OPT_AUDIO | OPT_EXPERT, {(void*)&rdftspeed}, "rdft speed", "msecs" },
    { "default", OPT_FUNC2 | HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, {(void*)opt_default}, "generic catch all option", "" },
    { NULL, },
};

static void show_usage(void)
{
    printf("Simple media player\n");
    printf("usage: ffplay [options] input_file\n");
    printf("\n");
}

static void show_help(void)
{
    show_usage();
    show_help_options(options, "Main options:\n",
                      OPT_EXPERT, 0);
    show_help_options(options, "\nAdvanced options:\n",
                      OPT_EXPERT, OPT_EXPERT);
    printf("\nWhile playing:\n"
           "q, ESC              quit\n"
           "f                   toggle full screen\n"
           "p, SPC              pause\n"
           "a                   cycle audio channel\n"
           "v                   cycle video channel\n"
           "t                   cycle subtitle channel\n"
           "w                   show audio waves\n"
           "s                   activate frame-step mode\n"
           "left/right          seek backward/forward 10 seconds\n"
           "down/up             seek backward/forward 1 minute\n"
           "mouse click         seek to percentage in file corresponding to fraction of width\n"
           );
}

static void opt_input_file(const char *filename)
{
    if (input_filename) {
        sprintf("Argument '%s' provided as input filename, but '%s' was already specified.\n",
                filename, input_filename);
        return;
    }
    if (!strcmp(filename, "-"))
        filename = "pipe:";
	input_filename = filename;
}

static void startNewVideo(const char *filename) {
	
	int flags, i; 
	flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_NOPARACHUTE;
#if !defined(__MINGW32__) && !defined(__APPLE__) && !defined(_WINDOWS) && !defined(CANT_THREAD_EVENTS)
    flags |= SDL_INIT_EVENTTHREAD; /* Not supported on Windows or Mac OS X */
#endif
    if (SDL_Init (flags)) {
        sprintf("Could not initialize SDL - %s\n", SDL_GetError());
        return;
    }	

if (!display_disable) {
#if (1) //HAVE_SDL_VIDEO_SIZE
        const SDL_VideoInfo *vi = SDL_GetVideoInfo();
        //fs_screen_width = 1280; //vi->current_w;
        //fs_screen_height = 720; //vi->current_h;
#endif
    }
	SDL_EventState(SDL_ACTIVEEVENT, SDL_IGNORE);
    SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
    SDL_EventState(SDL_USEREVENT, SDL_IGNORE);

	cur_stream = stream_open(filename, file_iformat);

	
	initMediaManager();
	XEnableScreenSaver(FALSE);

	if (TTF_Init() != -1) {
		Font = TTF_OpenFont("d:\\Images\\mFont.ttf", 32);
		if (Font != NULL)
			Fonts = 1;
			//Font->height = screen_height/10;
	}
	event_loop();
}

static int handler(void* user, const char* section, const char* name,
                   const char* value)
{
    configuration* pconfig = (configuration*)user;

    #define MATCH(s, n) stricmp(section, s) == 0 && stricmp(name, n) == 0
    if (MATCH("useroptions", "thread_count")) {
        pconfig->thread_count = atoi(value);
			thread_count = pconfig->thread_count;
    } else if (MATCH("useroptions", "thread_type")) {
        pconfig->thread_type = atoi(value);
			thread_type = pconfig->thread_type;
    } else if (MATCH("useroptions", "workaround_bugs")) {
        pconfig->workaround_bugs = atoi(value);
			workaround_bugs = pconfig->workaround_bugs;
    } else if (MATCH("useroptions", "fast")) {
        pconfig->fast = atoi(value);
			fast = pconfig->fast;
    } else if (MATCH("useroptions", "genpts")) {
        pconfig->genpts = atoi(value);
			genpts = pconfig->genpts;
    } else if (MATCH("useroptions", "low_res")) {
        pconfig->low_res = atoi(value);
			low_res = pconfig->low_res;
    } else if (MATCH("useroptions", "idct")) {
        pconfig->idct = atoi(value);
			idct = pconfig->idct;
    } else if (MATCH("useroptions", "skipframe")) {
        pconfig->skipframe = atoi(value);
			skipframe = pconfig->skipframe;
    } else if (MATCH("useroptions", "skipidct")) {
        pconfig->skipidct = atoi(value);
			skipidct = pconfig->skipidct;
    } else if (MATCH("useroptions", "skiploop")) {
        pconfig->skiploop = atoi(value);
			skiploop = pconfig->skiploop;
    } else if (MATCH("useroptions", "autoexit")) {
        pconfig->autoexit = atoi(value);
			autoexit = pconfig->autoexit;
    } else if (MATCH("useroptions", "error_recognition")) {
        pconfig->error_recognition = atoi(value);
			error_recognition = pconfig->error_recognition;
    } else if (MATCH("useroptions", "error_concealment")) {
        pconfig->error_concealment = atoi(value);
			error_concealment = pconfig->error_concealment;
    } else if (MATCH("useroptions", "decoder_reorder_pts")) {
        pconfig->decoder_reorder_pts = atoi(value);
			decoder_reorder_pts = pconfig->decoder_reorder_pts;
    } else if (MATCH("useroptions", "loop")) {
        pconfig->loop = atoi(value);
			loop = pconfig->loop;
    } else if (MATCH("useroptions", "framedrop")) {
        pconfig->framedrop = atoi(value);
			framedrop = pconfig->framedrop;
	} else if (MATCH("useroptions", "xexname")) {
		pconfig->xexname = value;
			xexname = pconfig->xexname;
			printf("Xexname is %s", xexname);
	}
	return 0;
}

int inimain(void)
{
    configuration config;

	if (ini_parse("d:\\player.ini", handler, &config) < 0) {
		printf("Can't load 'player.ini'\n");
        return 1;
    }
    /*printf("Config loaded from 'test.ini': version=%d, name=%s, email=%s\n",
        config.version, config.name, config.email);*/
    return 0;
}

/* Called from the main */
int main(int argc, char **argv)
{
    int flags, i;
	/*ini file stuff*/
	
	xexname = "default.xex";
	inimain();
	skip_frame = skipframe;
	skip_idct = skipidct;
	skip_loop_filter = skiploop;

	/*//joystick*/
	SDL_JoystickEventState(SDL_ENABLE);
	
	av_get_pix_fmt("PIX_FMT_YUYV422"/*"PIX_FMT_ARGB"*/); //PIX_FMT_RGB565BE"); //R5G6B5");//PIX_FMT_YUV420P //PIX_FMT_ARGB
    /* register all codecs, demux and protocols */
    avcodec_register_all();
#if (0) //CONFIG_AVDEVICE
    avdevice_register_all();
#endif
#if CONFIG_AVFILTER
    avfilter_register_all();
#endif
    av_register_all();

    for(i=0; i<AVMEDIA_TYPE_NB; i++){
        avcodec_opts[i]= avcodec_alloc_context2(i);
    }
    avformat_opts = avformat_alloc_context();
#if !CONFIG_AVFILTER
    sws_opts = sws_getContext(16,16,0, 16,16,0, sws_flags, NULL,NULL,NULL);
#endif

    //show_banner();
	//parse_options(argc, argv, options, filename);
	//parse_options(1, filename, options, input_filename);

//	if (!input_filename) {
//        show_usage();
//        sprintf("An input file must be specified\n");
//        sprintf("Use -h to get full help or, even better, run 'man ffplay'\n");
//        return(-1);
//    }

    if (display_disable) {
        video_disable = 1;
    }
    
    
	av_init_packet(&flush_pkt);
    flush_pkt.data= "FLUSH";

	filename = getLaunchData();
	if (filename == NULL )
		filename = getFileManager();
		//filename = "d:\\test.mp4";

    startNewVideo(filename);
	/* never returns */

    return 0;
}
