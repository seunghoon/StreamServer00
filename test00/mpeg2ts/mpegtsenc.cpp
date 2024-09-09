/*
 * MPEG2 transport stream (aka DVB) muxer
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

#include <windows.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define __STDC_LIMIT_MACROS
#include "stdint.h"
#include "bswap.h"
#include "crc.h"
//#include "libavcodec/mpegvideo.h"
//#include "avformat.h"
//#include "internal.h"
//#include "mpegts.h"

#define WRITE_FILE 0
#define FILE_NAME ("test.ts")
static FILE *fp_test = NULL;

#define MAX_BUFFER_SIZE (2000*1000*3)
BYTE g_TsBuffer[MAX_BUFFER_SIZE];
int g_TsBufferLen = 0;

static void (*s_pCallback)(const uint8_t *, const int);

#define INCLUDE_AUDIO 1		// MPEG-2 part 3
#define INCLUDE_ADTS_AAC 0	// MPEG-2 part 7
#define INCLUDE_LATM_AAC 0	// MPEG-4 part 3
#if (INCLUDE_ADTS_AAC)
#include "adts.h"
#endif

#define TS_FEC_PACKET_SIZE 204
#define TS_DVHS_PACKET_SIZE 192
#define TS_PACKET_SIZE 188
#define TS_MAX_PACKET_SIZE 204

#define NB_PID_MAX 8192
#define MAX_SECTION_SIZE 4096

/* pids */
#define PAT_PID                 0x0000
#define SDT_PID                 0x0011

/* table ids */
#define PAT_TID   0x00
#define PMT_TID   0x02
#define SDT_TID   0x42

#define STREAM_TYPE_VIDEO_MPEG1     0x01
#define STREAM_TYPE_VIDEO_MPEG2     0x02
#define STREAM_TYPE_AUDIO_MPEG1     0x03
#define STREAM_TYPE_AUDIO_MPEG2     0x04
#define STREAM_TYPE_PRIVATE_SECTION 0x05
#define STREAM_TYPE_PRIVATE_DATA    0x06
#define STREAM_TYPE_AUDIO_AAC       0x0f
#define STREAM_TYPE_VIDEO_MPEG4     0x10
#define STREAM_TYPE_VIDEO_H264      0x1b
#define STREAM_TYPE_VIDEO_VC1       0xea
#define STREAM_TYPE_VIDEO_DIRAC     0xd1

#define STREAM_TYPE_AUDIO_AC3       0x81
#define STREAM_TYPE_AUDIO_DTS       0x8a

#define MY_MAX_STREAMS	20
#define MY_MAX_SERVICES	20

#if 0
typedef struct MpegTSContext MpegTSContext;

MpegTSContext *ff_mpegts_parse_open(AVFormatContext *s);
int ff_mpegts_parse_packet(MpegTSContext *ts, AVPacket *pkt,
                           const uint8_t *buf, int len);
void ff_mpegts_parse_close(MpegTSContext *ts);
#endif

typedef struct AVStream {
	int is_video;
	int is_audio;
	int is_aac;
	double fps;		// added...
	bool bKeyFrame;	// added...
	void *priv_data;
} AVStream;

typedef struct AVPacket {
    int size;
	int stream_index;
	uint64_t dts, pts;
    uint8_t *data;
} AVPacket;

typedef struct AVFormatContext {
    void *priv_data;
    int nb_streams;
    AVStream *streams[MY_MAX_STREAMS];
	int64_t max_delay;
	int mux_rate;
} AVFormatContext;

#define av_mallocz(s) calloc(s, 1)
#define av_malloc(s) malloc(s)
#define av_free(p) free(p);
#define av_strdup(s) _strdup(s)

#define AV_NOPTS_VALUE 0x8000000000000000
#define AV_TIME_BASE 1000000

#ifndef AV_RB16
#   define AV_RB16(x)                           \
    ((((const uint8_t*)(x))[0] << 8) |          \
      ((const uint8_t*)(x))[1])
#endif
#ifndef AV_WB16
#   define AV_WB16(p, d) do {                   \
        ((uint8_t*)(p))[1] = (d);               \
        ((uint8_t*)(p))[0] = (d)>>8;            \
    } while(0)
#endif
#ifndef AV_RB32
#   define AV_RB32(x)                           \
    ((((const uint8_t*)(x))[0] << 24) |         \
     (((const uint8_t*)(x))[1] << 16) |         \
     (((const uint8_t*)(x))[2] <<  8) |         \
      ((const uint8_t*)(x))[3])
#endif
#ifndef AV_WB32
#   define AV_WB32(p, d) do {                   \
        ((uint8_t*)(p))[3] = (d);               \
        ((uint8_t*)(p))[2] = (d)>>8;            \
        ((uint8_t*)(p))[1] = (d)>>16;           \
        ((uint8_t*)(p))[0] = (d)>>24;           \
    } while(0)
#endif

static void av_freep(void *arg)
{
    void **ptr= (void**)arg;
    av_free(*ptr);
    *ptr = NULL;
}

static const uint8_t *ff_find_start_code(const uint8_t * __restrict p, const uint8_t *end, uint32_t * __restrict state){
    int i;

    //assert(p<=end);
    if(p>=end)
        return end;

    for(i=0; i<3; i++){
        uint32_t tmp= *state << 8;
        *state= tmp + *(p++);
        if(tmp == 0x100 || p==end)
            return p;
    }

    while(p<end){
        if     (p[-1] > 1      ) p+= 3;
        else if(p[-2]          ) p+= 2;
        else if(p[-3]|(p[-1]-1)) p++;
        else{
            p++;
            break;
        }
    }

    p= __min(p, end)-4;
    *state= AV_RB32(p);

    return p+4;
}

enum AVRounding {
    AV_ROUND_ZERO     = 0, ///< Round toward zero.
    AV_ROUND_INF      = 1, ///< Round away from zero.
    AV_ROUND_DOWN     = 2, ///< Round toward -infinity.
    AV_ROUND_UP       = 3, ///< Round toward +infinity.
    AV_ROUND_NEAR_INF = 5, ///< Round to nearest and halfway cases away from zero.
};

#pragma warning(push)
#pragma warning(disable:4018)
static int64_t av_rescale_rnd(const int64_t a, const int64_t b, const int64_t c, const enum AVRounding rnd){
    int64_t r=0;
    //assert(c > 0);
    //assert(b >=0);
    //assert(rnd >=0 && rnd<=5 && rnd!=4);

    if(a<0 && a != INT64_MIN) return -av_rescale_rnd(-a, b, c, (enum AVRounding)(rnd ^ ((rnd>>1)&1)));

    if(rnd==AV_ROUND_NEAR_INF) r= c/2;
    else if(rnd&1)             r= c-1;

    if(b<=INT_MAX && c<=INT_MAX){
        if(a<=INT_MAX)
            return (a * b + r)/c;
        else
            return a/c*b + (a%c*b + r)/c;
    }else{
        uint64_t a0= a&0xFFFFFFFF;
        uint64_t a1= a>>32;
        uint64_t b0= b&0xFFFFFFFF;
        uint64_t b1= b>>32;
        uint64_t t1= a0*b1 + a1*b0;
        uint64_t t1a= t1<<32;
        int i;

        a0 = a0*b0 + t1a;
        a1 = a1*b1 + (t1>>32) + (a0<t1a);
        a0 += r;
        a1 += a0<r;

        for(i=63; i>=0; i--){
//            int o= a1 & 0x8000000000000000ULL;
            a1+= a1 + ((a0>>i)&1);
            t1+=t1;
            if(/*o || */c <= a1){
                a1 -= c;
                t1++;
            }
        }
        return t1;
    }
}
#pragma warning(pop)

// a*b/c	=> convert c to b !!!
int64_t av_rescale(int64_t a, int64_t b, int64_t c)
{
    return av_rescale_rnd(a, b, c, AV_ROUND_NEAR_INF);
}

// timestamp in 90000Hz
static int64_t av_gettime(void)
{
#if 0
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
#else
	LARGE_INTEGER ticksPerSecond;	// frequency of tick
	LARGE_INTEGER ticks;
	QueryPerformanceFrequency(&ticksPerSecond);
	QueryPerformanceCounter(&ticks);
#if 1
	return av_rescale((int64_t)ticks.QuadPart, 90000, (int64_t)ticksPerSecond.QuadPart);
#else
	return ticks.QuadPart * 90000 / ticksPerSecond.QuadPart;
#endif
#endif
}

static void put_buffer(const uint8_t *data, const int size)
{
#if (WRITE_FILE)
	fwrite(data, 1, size, fp_test);
#else
	if (s_pCallback)
	{
		s_pCallback(data, size);
	}

	if (g_TsBufferLen + size < MAX_BUFFER_SIZE)
	{
		memcpy(&(g_TsBuffer[g_TsBufferLen]), data, size);
		g_TsBufferLen += size;
	}
#endif
}

static void put_flush_packet(void)
{
#if (WRITE_FILE)
	fflush(fp_test);
#else

#endif
}

/* write DVB SI sections */

/*********************************************/
/* mpegts section writer */

typedef struct MpegTSSection {
    int pid;
    int cc;
    void (*write_packet)(struct MpegTSSection *s, const uint8_t *packet);
    void *opaque;
} MpegTSSection;

typedef struct MpegTSService {
    MpegTSSection pmt; /* MPEG2 pmt table context */
    int sid;           /* service ID */
    char *name;
    char *provider_name;
    int pcr_pid;
    int pcr_packet_count;
    int pcr_packet_period;
} MpegTSService;

typedef struct MpegTSWrite {
    MpegTSSection pat; /* MPEG2 pat table */
    MpegTSSection sdt; /* MPEG2 sdt table context */
#if 0
    MpegTSService **services;
#else
	MpegTSService services[MY_MAX_SERVICES];
#endif
    int sdt_packet_count;
    int sdt_packet_period;
    int pat_packet_count;
    int pat_packet_period;
    int nb_services;
    int onid;
    int tsid;
    uint64_t cur_pcr;
    int mux_rate; ///< set to 1 when VBR
} MpegTSWrite;

/* NOTE: 4 bytes must be left at the end for the crc32 */
static void mpegts_write_section(MpegTSSection *s, uint8_t *buf, int len)
{
    MpegTSWrite *ts = (MpegTSWrite*)((AVFormatContext*)s->opaque)->priv_data;
    unsigned int crc;
    unsigned char packet[TS_PACKET_SIZE];
    const unsigned char *buf_ptr;
    unsigned char *q;
    int first, b, len1, left;

    crc = bswap_32(av_crc(av_crc_get_table(AV_CRC_32_IEEE), -1, buf, len - 4));
    buf[len - 4] = (crc >> 24) & 0xff;
    buf[len - 3] = (crc >> 16) & 0xff;
    buf[len - 2] = (crc >> 8) & 0xff;
    buf[len - 1] = (crc) & 0xff;

    /* send each packet */
    buf_ptr = buf;
    while (len > 0) {
        first = (buf == buf_ptr);
        q = packet;
        *q++ = 0x47;
        b = (s->pid >> 8);
        if (first)
            b |= 0x40;
        *q++ = b;
        *q++ = s->pid;
        s->cc = (s->cc + 1) & 0xf;
        *q++ = 0x10 | s->cc;
        if (first)
            *q++ = 0; /* 0 offset */
        len1 = TS_PACKET_SIZE - (q - packet);
        if (len1 > len)
            len1 = len;
        memcpy(q, buf_ptr, len1);
        q += len1;
        /* add known padding data */
        left = TS_PACKET_SIZE - (q - packet);
        if (left > 0)
            memset(q, 0xff, left);

        s->write_packet(s, packet);

        buf_ptr += len1;
        len -= len1;

        ts->cur_pcr += TS_PACKET_SIZE*8*90000LL/ts->mux_rate;
    }
}

static inline void put16(uint8_t **q_ptr, int val)
{
    uint8_t *q;
    q = *q_ptr;
    *q++ = val >> 8;
    *q++ = val;
    *q_ptr = q;
}

static int mpegts_write_section1(MpegTSSection *s, int tid, int id,
                          int version, int sec_num, int last_sec_num,
                          uint8_t *buf, int len)
{
    uint8_t section[1024], *q;
    unsigned int tot_len;

    tot_len = 3 + 5 + len + 4;
    /* check if not too big */
    if (tot_len > 1024)
        return -1;

    q = section;
    *q++ = tid;
    put16(&q, 0xb000 | (len + 5 + 4)); /* 5 byte header + 4 byte CRC */
    put16(&q, id);
    *q++ = 0xc1 | (version << 1); /* current_next_indicator = 1 */
    *q++ = sec_num;
    *q++ = last_sec_num;
    memcpy(q, buf, len);

    mpegts_write_section(s, section, tot_len);
    return 0;
}

/*********************************************/
/* mpegts writer */

#define DEFAULT_PMT_START_PID   0x1000
#define DEFAULT_START_PID       0x0100
#define DEFAULT_PROVIDER_NAME   "KT"
#define DEFAULT_SERVICE_NAME    "Game Streaming"

/* default network id, transport stream and service identifiers */
#define DEFAULT_ONID            0x0001
#define DEFAULT_TSID            0x0001
#define DEFAULT_SID             0x0001

/* a PES packet header is generated every DEFAULT_PES_HEADER_FREQ packets */
#define DEFAULT_PES_HEADER_FREQ 16
#define DEFAULT_PES_PAYLOAD_SIZE ((DEFAULT_PES_HEADER_FREQ - 1) * 184 + 170)

/* we retransmit the SI info at this rate */
#define SDT_RETRANS_TIME 500
#define PAT_RETRANS_TIME 100
#define PCR_RETRANS_TIME 20

typedef struct MpegTSWriteStream {
    struct MpegTSService *service;
    int pid; /* stream associated pid */
    int cc;
    int payload_index;
    int first_pts_check; ///< first pts check needed
    int64_t payload_pts;
    int64_t payload_dts;
    uint8_t payload[DEFAULT_PES_PAYLOAD_SIZE];
#if (INCLUDE_ADTS_AAC)
    ADTSContext *adts;
#endif
} MpegTSWriteStream;

static void mpegts_write_pat(AVFormatContext *s)
{
    MpegTSWrite *ts = (MpegTSWrite*)s->priv_data;
    MpegTSService *service;
    uint8_t data[1012], *q;
    int i;

    q = data;
    for(i = 0; i < ts->nb_services; i++) {
#if 0
        service = ts->services[i];
#else
		service = &(ts->services[i]);
#endif
        put16(&q, service->sid);
        put16(&q, 0xe000 | service->pmt.pid);
    }
    mpegts_write_section1(&ts->pat, PAT_TID, ts->tsid, 0, 0, 0,
                          data, q - data);
}

static void mpegts_write_pmt(AVFormatContext *s, MpegTSService *service)
{
    //    MpegTSWrite *ts = s->priv_data;
    uint8_t data[1012], *q, *desc_length_ptr, *program_info_length_ptr;
    int val, stream_type, i;

    q = data;
    put16(&q, 0xe000 | service->pcr_pid);

    program_info_length_ptr = q;
    q += 2; /* patched after */

    /* put program info here */

    val = 0xf000 | (q - program_info_length_ptr - 2);
    program_info_length_ptr[0] = val >> 8;
    program_info_length_ptr[1] = val;

    for(i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MpegTSWriteStream *ts_st = (MpegTSWriteStream*)st->priv_data;
        //AVMetadataTag *lang = av_metadata_get(st->metadata, "language", NULL,0);
#if 0
        switch(st->codec->codec_id) {
        case CODEC_ID_MPEG1VIDEO:
        case CODEC_ID_MPEG2VIDEO:
            stream_type = STREAM_TYPE_VIDEO_MPEG2;
            break;
        case CODEC_ID_MPEG4:
            stream_type = STREAM_TYPE_VIDEO_MPEG4;
            break;
        case CODEC_ID_H264:
            stream_type = STREAM_TYPE_VIDEO_H264;
            break;
        case CODEC_ID_DIRAC:
            stream_type = STREAM_TYPE_VIDEO_DIRAC;
            break;
        case CODEC_ID_MP2:
        case CODEC_ID_MP3:
            stream_type = STREAM_TYPE_AUDIO_MPEG1;
            break;
        case CODEC_ID_AAC:
            stream_type = STREAM_TYPE_AUDIO_AAC;
            break;
        case CODEC_ID_AC3:
            stream_type = STREAM_TYPE_AUDIO_AC3;
            break;
        default:
            stream_type = STREAM_TYPE_PRIVATE_DATA;
            break;
        }
#else
		if (st->is_video) stream_type = STREAM_TYPE_VIDEO_H264;
		else if (st->is_audio) stream_type = STREAM_TYPE_AUDIO_AAC;
#endif
        *q++ = stream_type;
        put16(&q, 0xe000 | ts_st->pid);
        desc_length_ptr = q;
        q += 2; /* patched after */

#if 0
		/* write optional descriptors here: SPEC 64 */
        switch(st->codec->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            if (lang && strlen(lang->value) == 3) {
                *q++ = 0x0a; /* ISO 639 language descriptor */
                *q++ = 4;
                *q++ = lang->value[0];
                *q++ = lang->value[1];
                *q++ = lang->value[2];
                *q++ = 0; /* undefined type */
            }
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            {
                const char *language;
                language = lang && strlen(lang->value)==3 ? lang->value : "eng";
                *q++ = 0x59;
                *q++ = 8;
                *q++ = language[0];
                *q++ = language[1];
                *q++ = language[2];
                *q++ = 0x10; /* normal subtitles (0x20 = if hearing pb) */
                put16(&q, 1); /* page id */
                put16(&q, 1); /* ancillary page id */
            }
            break;
        case AVMEDIA_TYPE_VIDEO:
            if (stream_type == STREAM_TYPE_VIDEO_DIRAC) {
                *q++ = 0x05; /*MPEG-2 registration descriptor*/
                *q++ = 4;
                *q++ = 'd';
                *q++ = 'r';
                *q++ = 'a';
                *q++ = 'c';
            }
            break;
        }
#elif 0
		if (st->is_video) {

			// video_stream_descriptor()
			if (st->fps == 23.976) val = 1;
			else if (st->fps == 24.0) val = 2;
			else if (st->fps == 25.0) val = 3;
			else if (st->fps == 29.97) val = 4;
			else if (st->fps == 30.0) val = 5;
			else if (st->fps == 50.0) val = 6;
			else if (st->fps == 59.94) val = 7;
			else if (st->fps == 60.0) val = 8;
			else val = 0;
			if (val != 0) {
				unsigned bits = 0;
				*q++ = 2;	// tag
				*q++ = 3;	// length
				bits = val;
				bits <<= 3;
				*q++ = 0xff && bits;
				*q++ = 0;
				*q++ = 0;
			}
		}
#if (INCLUDE_AUDIO)
		else if (st->is_audio) {

		}
#endif
#endif

        val = 0xf000 | (q - desc_length_ptr - 2);
        desc_length_ptr[0] = val >> 8;
        desc_length_ptr[1] = val;
    }
    mpegts_write_section1(&service->pmt, PMT_TID, service->sid, 0, 0, 0,
                          data, q - data);
}

/* NOTE: str == NULL is accepted for an empty string */
static void putstr8(uint8_t **q_ptr, const char *str)
{
    uint8_t *q;
    int len;

    q = *q_ptr;
    if (!str)
        len = 0;
    else
        len = strlen(str);
    *q++ = len;
    memcpy(q, str, len);
    q += len;
    *q_ptr = q;
}

static void mpegts_write_sdt(AVFormatContext *s)
{
    MpegTSWrite *ts = (MpegTSWrite*)s->priv_data;
    MpegTSService *service;
    uint8_t data[1012], *q, *desc_list_len_ptr, *desc_len_ptr;
    int i, running_status, free_ca_mode, val;

    q = data;
    put16(&q, ts->onid);
    *q++ = 0xff;
    for(i = 0; i < ts->nb_services; i++) {
#if 0
        service = ts->services[i];
#else
		service = &(ts->services[i]);
#endif
        put16(&q, service->sid);
        *q++ = 0xfc | 0x00; /* currently no EIT info */
        desc_list_len_ptr = q;
        q += 2;
        running_status = 4; /* running */
        free_ca_mode = 0;

        /* write only one descriptor for the service name and provider */
        *q++ = 0x48;
        desc_len_ptr = q;
        q++;
        *q++ = 0x01; /* digital television service */
        putstr8(&q, service->provider_name);
        putstr8(&q, service->name);
        desc_len_ptr[0] = q - desc_len_ptr - 1;

        /* fill descriptor length */
        val = (running_status << 13) | (free_ca_mode << 12) |
            (q - desc_list_len_ptr - 2);
        desc_list_len_ptr[0] = val >> 8;
        desc_list_len_ptr[1] = val;
    }
    mpegts_write_section1(&ts->sdt, SDT_TID, ts->tsid, 0, 0, 0,
                          data, q - data);
}

static MpegTSService *mpegts_add_service(MpegTSWrite *ts,
                                         int sid,
                                         const char *provider_name,
                                         const char *name)
{
	MpegTSService *service = NULL;
#if 0
    service = av_mallocz(sizeof(MpegTSService));
#else
	if (ts->nb_services < MY_MAX_SERVICES) service = &(ts->services[ts->nb_services]);
#endif
    if (!service)
        return NULL;
    service->pmt.pid = DEFAULT_PMT_START_PID + ts->nb_services - 1;
    service->sid = sid;
    service->provider_name = av_strdup(provider_name);
    service->name = av_strdup(name);
    service->pcr_pid = 0x1fff;
#if 0
    dynarray_add(&ts->services, &ts->nb_services, service);
#else
	ts->nb_services++;
#endif
    return service;
}

static void section_write_packet(MpegTSSection *s, const uint8_t *packet)
{
    AVFormatContext *ctx = (AVFormatContext*)s->opaque;
#if 0
    put_buffer(ctx->pb, packet, TS_PACKET_SIZE);
#else
	put_buffer(packet, TS_PACKET_SIZE);
#endif
}

static int mpegts_write_header(AVFormatContext *s)
{
    MpegTSWrite *ts = (MpegTSWrite*)s->priv_data;
    MpegTSWriteStream *ts_st;
    MpegTSService *service;
    AVStream *st, *pcr_st = NULL;
    //AVMetadataTag *title;
    int i;
    const char *service_name;

    ts->tsid = DEFAULT_TSID;
    ts->onid = DEFAULT_ONID;
    /* allocate a single DVB service */
    //title = av_metadata_get(s->metadata, "title", NULL, 0);
#if 0
    service_name = title ? title->value : DEFAULT_SERVICE_NAME;
#else
	service_name = DEFAULT_SERVICE_NAME;
#endif
    service = mpegts_add_service(ts, DEFAULT_SID,
                                 DEFAULT_PROVIDER_NAME, service_name);
    service->pmt.write_packet = section_write_packet;
    service->pmt.opaque = s;
    service->pmt.cc = 15;	// countinuity_counter

    ts->pat.pid = PAT_PID;
    ts->pat.cc = 15; // Initialize at 15 so that it wraps and be equal to 0 for the first packet we write
    ts->pat.write_packet = section_write_packet;
    ts->pat.opaque = s;

    ts->sdt.pid = SDT_PID;
    ts->sdt.cc = 15;
    ts->sdt.write_packet = section_write_packet;
    ts->sdt.opaque = s;

    /* assign pids to each stream */
    for(i = 0;i < s->nb_streams; i++) {
        st = s->streams[i];
        ts_st = (MpegTSWriteStream*)av_mallocz(sizeof(MpegTSWriteStream));
        if (!ts_st)
            goto fail;
        st->priv_data = ts_st;
        ts_st->service = service;
        ts_st->pid = DEFAULT_START_PID + i;
        ts_st->payload_pts = AV_NOPTS_VALUE;
        ts_st->payload_dts = AV_NOPTS_VALUE;
        ts_st->first_pts_check = 1;
        ts_st->cc = 15;	// countinuity_counter
        /* update PCR pid by using the first video stream */
#if 0
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
#else
		if (st->is_video &&
#endif
            service->pcr_pid == 0x1fff) {
            service->pcr_pid = ts_st->pid;
            pcr_st = st;
        }
#if (INCLUDE_ADTS_AAC)
        if (st->is_audio && st->is_aac &&
            st->codec->extradata_size > 0) {
            ts_st->adts = av_mallocz(sizeof(*ts_st->adts));
            if (!ts_st->adts)
                return AVERROR(ENOMEM);
            if (ff_adts_decode_extradata(s, ts_st->adts, st->codec->extradata,
                                         st->codec->extradata_size) < 0)
                return -1;
        }
#endif
    }

    /* if no video stream, use the first stream as PCR */
    if (service->pcr_pid == 0x1fff && s->nb_streams > 0) {
        pcr_st = s->streams[0];
        ts_st = (MpegTSWriteStream*)pcr_st->priv_data;
        service->pcr_pid = ts_st->pid;
    }

    ts->mux_rate = s->mux_rate ? s->mux_rate : 1;

    if (ts->mux_rate > 1) {
        service->pcr_packet_period = (ts->mux_rate * PCR_RETRANS_TIME) /
            (TS_PACKET_SIZE * 8 * 1000);
        ts->sdt_packet_period      = (ts->mux_rate * SDT_RETRANS_TIME) /
            (TS_PACKET_SIZE * 8 * 1000);
        ts->pat_packet_period      = (ts->mux_rate * PAT_RETRANS_TIME) /
            (TS_PACKET_SIZE * 8 * 1000);
#if 1
        ts->cur_pcr = av_rescale(s->max_delay, 90000, AV_TIME_BASE);
#else
		ts->cur_pcr = 0;	// no delay
#endif
    } else {
        /* Arbitrary values, PAT/PMT could be written on key frames. */
        ts->sdt_packet_period = 200;
        ts->pat_packet_period = 40;
		service->pcr_packet_period = 3;
#if 0
        if (pcr_st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (!pcr_st->codec->frame_size) {
#if 0
                av_log(s, AV_LOG_WARNING, "frame size not set\n");
#endif
                service->pcr_packet_period =
                    pcr_st->codec->sample_rate/(10*512);
            } else {
                service->pcr_packet_period =
                    pcr_st->codec->sample_rate/(10*pcr_st->codec->frame_size);
            }
        } else 
		{
            // max delta PCR 0.1s
            service->pcr_packet_period =
                pcr_st->codec->time_base.den/(10*pcr_st->codec->time_base.num);
        }
#endif
    }

    // output a PCR as soon as possible
    service->pcr_packet_count = service->pcr_packet_period;
    ts->pat_packet_count = ts->pat_packet_period-1;
    ts->sdt_packet_count = ts->sdt_packet_period-1;

#if 0
    av_log(s, AV_LOG_INFO,
           "muxrate %d bps, pcr every %d pkts, "
           "sdt every %d, pat/pmt every %d pkts\n",
           ts->mux_rate, service->pcr_packet_period,
           ts->sdt_packet_period, ts->pat_packet_period);
#endif

#if 0
    put_flush_packet(s->pb);
#else
	put_flush_packet();
#endif

    return 0;

 fail:
    for(i = 0;i < s->nb_streams; i++) {
        st = s->streams[i];
        av_free(st->priv_data);
    }
    return -1;
}

/* send SDT, PAT and PMT tables regulary */
static void retransmit_si_info(AVFormatContext *s, AVStream *st)
{
    MpegTSWrite *ts = (MpegTSWrite*)s->priv_data;
    int i;

    if (++ts->sdt_packet_count == ts->sdt_packet_period) {
        ts->sdt_packet_count = 0;
#if 0
        mpegts_write_sdt(s);
#endif
    }
#if 1
    if (++ts->pat_packet_count == ts->pat_packet_period) {
		ts->pat_packet_count = 0;
#else
	if (st->bKeyFrame == true) {
#endif
        mpegts_write_pat(s);
        for(i = 0; i < ts->nb_services; i++) {
            mpegts_write_pmt(s, &(ts->services[i]));
        }
    }
}

/* Write a single null transport stream packet */
static void mpegts_insert_null_packet(AVFormatContext *s)
{
    MpegTSWrite *ts = (MpegTSWrite*)s->priv_data;
    uint8_t *q;
    uint8_t buf[TS_PACKET_SIZE];

    q = buf;
    *q++ = 0x47;
    *q++ = 0x00 | 0x1f;
    *q++ = 0xff;
    *q++ = 0x10;
    memset(q, 0x0FF, TS_PACKET_SIZE - (q - buf));
#if 0
    put_buffer(s->pb, buf, TS_PACKET_SIZE);
#else
	put_buffer(buf, TS_PACKET_SIZE);
#endif
    ts->cur_pcr += TS_PACKET_SIZE*8*90000LL/ts->mux_rate;
}

/* Write a single transport stream packet with a PCR and no payload */
static void mpegts_insert_pcr_only(AVFormatContext *s, AVStream *st)
{
    MpegTSWrite *ts = (MpegTSWrite*)s->priv_data;
    MpegTSWriteStream *ts_st = (MpegTSWriteStream*)st->priv_data;
    uint8_t *q;
    uint64_t pcr = ts->cur_pcr;
    uint8_t buf[TS_PACKET_SIZE];

    q = buf;
    *q++ = 0x47;
    *q++ = ts_st->pid >> 8;
    *q++ = ts_st->pid;
    *q++ = 0x20 | ts_st->cc;   /* Adaptation only */
    /* Continuity Count field does not increment (see 13818-1 section 2.4.3.3) */
    *q++ = TS_PACKET_SIZE - 5; /* Adaptation Field Length */
    *q++ = 0x10;               /* Adaptation flags: PCR present */

    /* PCR coded into 6 bytes */
    *q++ = (uint8_t)(pcr >> 25);
    *q++ = (uint8_t)(pcr >> 17);
    *q++ = (uint8_t)(pcr >> 9);
    *q++ = (uint8_t)(pcr >> 1);
    *q++ = (uint8_t)((pcr & 1) << 7);
    *q++ = 0;

    /* stuffing bytes */
    memset(q, 0xFF, TS_PACKET_SIZE - (q - buf));
#if 0
    put_buffer(s->pb, buf, TS_PACKET_SIZE);
#else
	put_buffer(buf, TS_PACKET_SIZE);
#endif
    ts->cur_pcr += TS_PACKET_SIZE*8*90000LL/ts->mux_rate;
}

static void write_pts(uint8_t *q, int fourbits, int64_t pts)
{
    int val;

    val = fourbits << 4 | (((pts >> 30) & 0x07) << 1) | 1;
    *q++ = val;
    val = (((pts >> 15) & 0x7fff) << 1) | 1;
    *q++ = val >> 8;
    *q++ = val;
    val = (((pts) & 0x7fff) << 1) | 1;
    *q++ = val >> 8;
    *q++ = val;
}

/* Add a pes header to the front of payload, and segment into an integer number of
 * ts packets. The final ts packet is padded using an over-sized adaptation header
 * to exactly fill the last ts packet.
 * NOTE: 'payload' contains a complete PES payload.
 */
static void mpegts_write_pes(AVFormatContext *s, AVStream *st,
                             const uint8_t *payload, int payload_size,
                             int64_t pts, int64_t dts)
{
    MpegTSWriteStream *ts_st = (MpegTSWriteStream*)st->priv_data;
    MpegTSWrite *ts = (MpegTSWrite*)s->priv_data;
    uint8_t buf[TS_PACKET_SIZE];
    uint8_t *q;
    int val, is_start, len, header_len, write_pcr, private_code, flags;
    int afc_len, stuffing_len;
    int64_t pcr = -1; /* avoid warning */
#if 1
    int64_t delay = av_rescale(s->max_delay, 90000, AV_TIME_BASE);
#else
	int64_t delay = 10;	// no delay!!
#endif

    is_start = 1;
    while (payload_size > 0) {
        retransmit_si_info(s, st);

        write_pcr = 0;
        if (ts_st->pid == ts_st->service->pcr_pid) {
            if (ts->mux_rate > 1 || is_start) // VBR pcr period is based on frames
                ts_st->service->pcr_packet_count++;
            if (ts_st->service->pcr_packet_count >=
                ts_st->service->pcr_packet_period) {
                ts_st->service->pcr_packet_count = 0;
                write_pcr = 1;
            }
        }

#if 1	// It's rate-control for constant-rate muxer...
        if (ts->mux_rate > 1 && dts != AV_NOPTS_VALUE &&
            (dts - (int64_t)ts->cur_pcr) > delay) {
            /* pcr insert gets priority over null packet insert */
            if (write_pcr)
                mpegts_insert_pcr_only(s, st);
            else
                mpegts_insert_null_packet(s);
            continue; /* recalculate write_pcr and possibly retransmit si_info */
        }
#endif

        /* prepare packet header */
        q = buf;
        *q++ = 0x47;
        val = (ts_st->pid >> 8);
        if (is_start)
            val |= 0x40;
        *q++ = val;
        *q++ = ts_st->pid;
        ts_st->cc = (ts_st->cc + 1) & 0xf;
        *q++ = 0x10 | ts_st->cc | (write_pcr ? 0x20 : 0);
        if (write_pcr) {
            // add 11, pcr references the last byte of program clock reference base
            if (ts->mux_rate > 1)
                pcr = ts->cur_pcr + (4+7)*8*90000LL / ts->mux_rate;
            else
                pcr = dts - delay;	// pcs, dts, delay, all in AV_TIME_BASE unit. why???

#if 0
            if (dts != AV_NOPTS_VALUE && dts < pcr)
                av_log(s, AV_LOG_WARNING, "dts < pcr, TS is invalid\n");
#endif
            *q++ = 7; /* AFC length */
            *q++ = 0x10; /* flags: PCR present */
			/* pcr is in AV_TIME_BASE unit, not 27MHz. why??? */
            *q++ = (uint8_t)(pcr >> 25);
            *q++ = (uint8_t)(pcr >> 17);
            *q++ = (uint8_t)(pcr >> 9);
            *q++ = (uint8_t)(pcr >> 1);
            *q++ = (uint8_t)((pcr & 1) << 7);
            *q++ = 0;
        }
        if (is_start) {
            int pes_extension = 0;
            /* write PES header */
            *q++ = 0x00;
            *q++ = 0x00;
            *q++ = 0x01;
            private_code = 0;
#if 0
            if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
                if (st->codec->codec_id == CODEC_ID_DIRAC) {
                    *q++ = 0xfd;
                } else
                    *q++ = 0xe0;
            } else if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO &&
                       (st->codec->codec_id == CODEC_ID_MP2 ||
                        st->codec->codec_id == CODEC_ID_MP3)) {
                *q++ = 0xc0;
            } else {
                *q++ = 0xbd;
                if (st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                    private_code = 0x20;
                }
            }
#else
			if (st->is_video) {
				*q++ = 0xe0;
			} else {
				*q++ = 0xbd;
			}
#endif
            header_len = 0;
            flags = 0;
            if (pts != AV_NOPTS_VALUE) {
                header_len += 5;
                flags |= 0x80;
            }
            if (dts != AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE && dts != pts) {
                header_len += 5;
                flags |= 0x40;
            }
#if 0
            if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
                st->codec->codec_id == CODEC_ID_DIRAC) {
                /* set PES_extension_flag */
                pes_extension = 1;
                flags |= 0x01;

                /*
                * One byte for PES2 extension flag +
                * one byte for extension length +
                * one byte for extension id
                */
                header_len += 3;
            }
#endif
            len = payload_size + header_len + 3;
            if (private_code != 0)
                len++;
            if (len > 0xffff)
                len = 0;
            *q++ = len >> 8;
            *q++ = len;
            val = 0x80;
#if 0
            /* data alignment indicator is required for subtitle data */
            if (st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE)
                val |= 0x04;
#endif
            *q++ = val;
            *q++ = flags;
            *q++ = header_len;
            if (pts != AV_NOPTS_VALUE) {
                write_pts(q, flags >> 6, pts);
                q += 5;
            }
            if (dts != AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE && dts != pts) {
                write_pts(q, 1, dts);
                q += 5;
            }
#if 0
            if (pes_extension && st->codec->codec_id == CODEC_ID_DIRAC) {
                flags = 0x01;  /* set PES_extension_flag_2 */
                *q++ = flags;
                *q++ = 0x80 | 0x01;  /* marker bit + extension length */
                /*
                * Set the stream id extension flag bit to 0 and
                * write the extended stream id
                */
                *q++ = 0x00 | 0x60;
            }
#endif
            if (private_code != 0)
                *q++ = private_code;
            is_start = 0;
        }
        /* header size */
        header_len = q - buf;
        /* data len */
        len = TS_PACKET_SIZE - header_len;
        if (len > payload_size)
            len = payload_size;
        stuffing_len = TS_PACKET_SIZE - header_len - len;
        if (stuffing_len > 0) {
            /* add stuffing with AFC */
            if (buf[3] & 0x20) {
                /* stuffing already present: increase its size */
                afc_len = buf[4] + 1;
                memmove(buf + 4 + afc_len + stuffing_len,
                        buf + 4 + afc_len,
                        header_len - (4 + afc_len));
                buf[4] += stuffing_len;
                memset(buf + 4 + afc_len, 0xff, stuffing_len);
            } else {
                /* add stuffing */
                memmove(buf + 4 + stuffing_len, buf + 4, header_len - 4);
                buf[3] |= 0x20;
                buf[4] = stuffing_len - 1;
                if (stuffing_len >= 2) {
                    buf[5] = 0x00;
                    memset(buf + 6, 0xff, stuffing_len - 2);
                }
            }
        }
        memcpy(buf + TS_PACKET_SIZE - len, payload, len);
        payload += len;
        payload_size -= len;
#if 0
        put_buffer(s->pb, buf, TS_PACKET_SIZE);
#else
		put_buffer(buf, TS_PACKET_SIZE);
#endif
        ts->cur_pcr += TS_PACKET_SIZE*8*90000LL/ts->mux_rate;
    }
#if 0
    put_flush_packet(s->pb);
#else
	put_flush_packet();
#endif
}

#define DATA_SIZE (2048 * 1024 * 4)
static int mpegts_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st = s->streams[pkt->stream_index];
    int size = pkt->size;
    uint8_t *buf= pkt->data;
#if 0
    uint8_t *data= NULL;
#else
	static uint8_t data[DATA_SIZE];
#endif
    MpegTSWriteStream *ts_st = (MpegTSWriteStream*)st->priv_data;
#if 1
    const uint64_t delay = av_rescale(s->max_delay, 90000, AV_TIME_BASE)*2;	// why x2?
#else
	const uint64_t delay = 0;	// no delay!!
#endif
    int64_t dts = AV_NOPTS_VALUE, pts = AV_NOPTS_VALUE;

#if 1
    if (pkt->pts != AV_NOPTS_VALUE)
        pts = pkt->pts + delay;
    if (pkt->dts != AV_NOPTS_VALUE)
        dts = pkt->dts + delay;
#endif

    if (ts_st->first_pts_check && pts == AV_NOPTS_VALUE) {
#if 0
        av_log(s, AV_LOG_ERROR, "first pts value must set\n");
#endif
        return -1;
    }
    ts_st->first_pts_check = 0;

#if 0
    if (st->codec->codec_id == CODEC_ID_H264) {
#else
	if (st->is_video) {
#endif
        const uint8_t *p = buf, *buf_end = p+size;
        uint32_t state = -1;

        if (pkt->size < 5 || AV_RB32(pkt->data) != 0x0000001) {
#if 0
            av_log(s, AV_LOG_ERROR, "h264 bitstream malformated, "
                   "no startcode found, use -vbsf h264_mp4toannexb\n");
#endif
            return -1;
        }

        do {
            p = ff_find_start_code(p, buf_end, &state);
            //av_log(s, AV_LOG_INFO, "nal %d\n", state & 0x1f);
        } while (p < buf_end && (state & 0x1f) != 9 &&
                 (state & 0x1f) != 5 && (state & 0x1f) != 1);

        if ((state & 0x1f) != 9) { // AUD NAL
#if 0
            data = (uint8_t*)av_malloc(pkt->size+6);
            if (!data)
                return -1;
#else
			if (DATA_SIZE < pkt->size+6) {
				return -1;
			}
#endif
            memcpy(data+6, pkt->data, pkt->size);

			// AVC_video_descriptor() ??? => No. Part of ES, I guess.
			// What is it???
            AV_WB32(data, 0x00000001);
            data[4] = 0x09;
            data[5] = 0xe0; // any slice type
            buf  = data;
            size = pkt->size+6;
        }
#if 0
    } else if (st->codec->codec_id == CODEC_ID_AAC) {
#else
	} else if (st->is_audio && st->is_aac) {
#endif
#if (INCLUDE_ADTS_AAC)
        if (pkt->size < 2)
            return -1;
        if ((AV_RB16(pkt->data) & 0xfff0) != 0xfff0) {
            ADTSContext *adts = ts_st->adts;
            int new_size;
            if (!adts) {
#if 0
                av_log(s, AV_LOG_ERROR, "aac bitstream not in adts format "
                       "and extradata missing\n");
#endif
                return -1;
            }
            new_size = ADTS_HEADER_SIZE+adts->pce_size+pkt->size;
            if ((unsigned)new_size >= INT_MAX)
                return -1;
#if 0
            data = av_malloc(new_size);
            if (!data)
                return AVERROR(ENOMEM);
#else
			if (DATA_SIZE < new_size) return -1;
#endif
            ff_adts_write_frame_header(adts, data, pkt->size, adts->pce_size);
            if (adts->pce_size) {
                memcpy(data+ADTS_HEADER_SIZE, adts->pce_data, adts->pce_size);
                adts->pce_size = 0;
            }
            memcpy(data+ADTS_HEADER_SIZE+adts->pce_size, pkt->data, pkt->size);
            buf = data;
            size = new_size;
        }
#endif
    }

#if 0
    if (st->codec->codec_type != AVMEDIA_TYPE_AUDIO) {
#else
	if (!(st->is_audio)) {
#endif
        // for video and subtitle, write a single pes packet
        mpegts_write_pes(s, st, buf, size, pts, dts);
#if 0
        av_free(data);
#endif
        return 0;
    }

    if (ts_st->payload_index + size > DEFAULT_PES_PAYLOAD_SIZE) {
        mpegts_write_pes(s, st, ts_st->payload, ts_st->payload_index,
                         ts_st->payload_pts, ts_st->payload_dts);
        ts_st->payload_index = 0;
    }

    if (!ts_st->payload_index) {
        ts_st->payload_pts = pts;
        ts_st->payload_dts = dts;
    }

    memcpy(ts_st->payload + ts_st->payload_index, buf, size);
    ts_st->payload_index += size;
#if 0
    av_free(data);
#endif
    return 0;
}

static int mpegts_write_end(AVFormatContext *s)
{
    MpegTSWrite *ts = (MpegTSWrite*)s->priv_data;
    MpegTSWriteStream *ts_st;
    MpegTSService *service;
    AVStream *st;
    int i;

    /* flush current packets */
    for(i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        ts_st = (MpegTSWriteStream*)st->priv_data;
        if (ts_st->payload_index > 0) {
            mpegts_write_pes(s, st, ts_st->payload, ts_st->payload_index,
                             ts_st->payload_pts, ts_st->payload_dts);
        }
#if (INCLUDE_ADTS_AAC)
        av_freep(&ts_st->adts);
#endif
    }
#if 0
    put_flush_packet(s->pb);
#else
	put_flush_packet();
#endif

    for(i = 0; i < ts->nb_services; i++) {
#if 0
        service = ts->services[i];
#else
		service = &(ts->services[i]);
#endif
        av_freep(&service->provider_name);
        av_freep(&service->name);
#if 0
        av_free(service);
#endif
    }
#if 0
    av_free(ts->services);
#endif

    return 0;
}

#if 0
AVOutputFormat mpegts_muxer = {
    "mpegts",
    NULL_IF_CONFIG_SMALL("MPEG-2 transport stream format"),
    "video/x-mpegts",
    "ts,m2t",
    sizeof(MpegTSWrite),
    CODEC_ID_MP2,
    CODEC_ID_MPEG2VIDEO,
    mpegts_write_header,
    mpegts_write_packet,
    mpegts_write_end,
};
#endif

static AVFormatContext s_ctx;
static AVStream s_videoStream;
static AVStream s_audioStream;
static MpegTSWrite s_ts;
static AVPacket s_pkt;
static uint64_t s_reference_time = 0;
static uint64_t s_sampling_time = 0;
static bool s_bInitialized = false;
static bool s_bIsFirst = false;

int init_mpegtsenc(const double fps, void (*pCallback)(const uint8_t *, const int), __int64 delay)
{
	if (s_bInitialized == true) return 0;

#if (WRITE_FILE)
	if ((fp_test = fopen(FILE_NAME, ("wb"))) == NULL) {
		return -1;
	}
#endif

	s_pCallback = pCallback;

	s_reference_time = av_gettime();
	s_sampling_time = 0;

	s_ts.mux_rate = 1;
	s_ts.nb_services = 0;

	s_ctx.priv_data = &s_ts;
	s_ctx.mux_rate = 0;
	s_ctx.max_delay = delay;	// 1MHz
	s_ctx.nb_streams = 1;
	s_videoStream.priv_data = NULL;
	s_audioStream.priv_data = NULL;
	s_ctx.streams[0] = &s_videoStream;
	s_ctx.streams[1] = &s_audioStream;
	s_ctx.streams[0]->priv_data = NULL;
	s_ctx.streams[0]->is_video = 1;
	s_ctx.streams[0]->is_audio = 0;
	s_ctx.streams[0]->is_aac = 0;
	s_ctx.streams[0]->fps = fps;
	s_ctx.streams[0]->bKeyFrame = false;

	s_bInitialized = true;
	s_bIsFirst = true;

	return mpegts_write_header(&s_ctx);
}

void set_sampling_time(void)
{
	s_sampling_time = av_gettime();
}

int feed_mpegtsenc(const void *data, const int size, const bool bKeyFrame, const int index)
{
	s_pkt.data = (uint8_t*)data;
	s_pkt.size = size;
	if (s_sampling_time == 0) {
		s_pkt.dts = AV_NOPTS_VALUE;
		s_pkt.pts = AV_NOPTS_VALUE;
	} else {
		// Unlike normal ffmpeg usage, these timestamps are in AV_TIME_BASE unit.	=> why???
		uint64_t timestamp = s_sampling_time - s_reference_time;
		s_pkt.dts = timestamp;
		s_pkt.pts = timestamp;
	}
	s_pkt.stream_index = index;
	if (index == 0) {
		s_videoStream.bKeyFrame = bKeyFrame;
	}

	g_TsBufferLen = 0;

	return mpegts_write_packet(&s_ctx, &s_pkt);
}

int destroy_mpegtsenc(void)
{
	int ret = 0;
	
	if (s_bInitialized == true) {
		ret = mpegts_write_end(&s_ctx);
		if (s_videoStream.priv_data) {
			av_free(s_videoStream.priv_data);
			s_videoStream.priv_data = NULL;
		}
		if (s_audioStream.priv_data) {
			av_free(s_audioStream.priv_data);
			s_audioStream.priv_data = NULL;
		}
	}

	if (fp_test) {
		fclose(fp_test);
		fp_test = NULL;
	}

	s_bInitialized = false;
	s_bIsFirst = false;

	return ret;
}
