/*
 * Copyright 2015 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __MPI_ENCODER_H__
#define __MPI_ENCODER_H__

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "rockchip/rk_mpi.h"
#include "rockchip/mpp_log.h"
#include "rockchip/mpp_mem.h"
#include "utils/camera_source.h"

#define MPP_ALIGN(x, a)         (((x)+(a)-1)&~((a)-1))
#define SZ_4K 4096

typedef void (*MpiEncoderFrameCallback)(void* userdata, const char* data, int size);

typedef struct {
    char                *file_output;
    /* -v q runtime log disable flag */
    RK_U32              quiet;
    /* -qc */
    RK_S32              qp_init;
    RK_S32              qp_min;
    RK_S32              qp_max;
    RK_S32              qp_min_i;
    RK_S32              qp_max_i;

    /* -fqc */
    RK_S32              fqp_min_i;
    RK_S32              fqp_min_p;
    RK_S32              fqp_max_i;
    RK_S32              fqp_max_p;

    // base flow context
    MppCtx ctx;
    MppApi *mpi;
    RK_S32 chn;

    // global flow control flag
    RK_U32 frm_eos;
    RK_U32 pkt_eos;
    RK_U32 frm_pkt_cnt;
    RK_S32 frame_num;
    RK_S32 frame_count;
    RK_U64 stream_size;
    /* end of encoding flag when set quit the loop */
    volatile RK_U32 loop_end;

    /* encoder config set */
    MppEncCfg       cfg;
    MppEncPrepCfg   prep_cfg;
    MppEncRcCfg     rc_cfg;
    MppEncCodecCfg  codec_cfg;
    MppEncSliceSplit split_cfg;
    MppEncOSDPltCfg osd_plt_cfg;
    MppEncOSDPlt    osd_plt;
    MppEncOSDData   osd_data;
    MppEncROICfg    roi_cfg;

    // input / output
    MppBufferGroup buf_grp;
    MppBuffer frm_buf;
    MppBuffer pkt_buf;
    MppBuffer md_info;
    MppEncSeiMode sei_mode;
    MppEncHeaderMode header_mode;

    // paramter for resource malloc
    RK_U32 width;
    RK_U32 height;
    RK_U32 hor_stride;
    RK_U32 ver_stride;
    MppFrameFormat fmt;
    MppCodingType type;
    RK_S32 loop_times;
    CamSource *cam_ctx;
    MpiEncoderFrameCallback callback;

    // resources
    size_t header_size;
    size_t frame_size;
    size_t mdinfo_size;
    /* NOTE: packet buffer may overflow */
    size_t packet_size;

    RK_U32 osd_enable;
    RK_U32 osd_mode;
    RK_U32 split_mode;
    RK_U32 split_arg;
    RK_U32 split_out;

    RK_U32 user_data_enable;
    RK_U32 roi_enable;

    // rate control runtime parameter
    RK_S32 fps_in_flex;
    RK_S32 fps_in_den;
    RK_S32 fps_in_num;
    RK_S32 fps_out_flex;
    RK_S32 fps_out_den;
    RK_S32 fps_out_num;
    RK_S32 bps;
    RK_S32 bps_max;
    RK_S32 bps_min;
    RK_S32 rc_mode;
    RK_S32 gop_mode;
    RK_S32 gop_len;
    RK_S32 vi_len;
    RK_S32 scene_mode;

    RK_S64 first_frm;
    RK_S64 first_pkt;
} MpiEncoderCtxInfo;

#define mpp_log_q(quiet, fmt, ...) \
    do { \
        if (!quiet) mpp_log(fmt, ## __VA_ARGS__); \
    } while (0);


#ifdef __cplusplus
extern "C" {
#endif

void *init_encoder(void *arg);
MPP_RET test_mpp_run(MpiEncoderCtxInfo *info,void* mpp_buf, char* enc_buf);
void _oshow_enc_fps(RK_S64 total_time, RK_S64 total_count, RK_S64 last_time, RK_S64 last_count);
int GetHeader(MpiEncoderCtxInfo *info,char* enc_buf);
MPP_RET test_ctx_deinit(MpiEncoderCtxInfo *info);

#ifdef __cplusplus
}
#endif

#endif /*__MPI_ENCODER_H__*/
