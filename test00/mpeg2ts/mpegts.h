/*
 * MPEG2 transport stream defines
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

#ifndef AVFORMAT_MPEGTS_H
#define AVFORMAT_MPEGTS_H

extern int init_mpegtsenc(const double fps, void (*pCallback)(const BYTE*, const int), __int64 delay = 1000);
extern void set_sampling_time(void);
extern int feed_mpegtsenc(const void *data, const int size, const bool bKeyFrame, const int index);
extern int destroy_mpegtsenc(void);

extern BYTE g_TsBuffer[];
extern int g_TsBufferLen;

extern __int64 av_rescale(__int64 a, __int64 b, __int64 c);

//#include "avformat.h"

#endif /* AVFORMAT_MPEGTS_H */
