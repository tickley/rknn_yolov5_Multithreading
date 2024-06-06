#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <cstdlib>
#include <stdio.h>
#include <getopt.h>
#include <sys/time.h>
#include "rtsp_demo.h"
#include "utils/drawing.h"
#include "utils/camera_source.h"
#include "utils/mpi_encoder.h"

using namespace std;
using namespace cv;

rtsp_demo_handle g_rtsplive = NULL;
static rtsp_session_handle g_rtsp_session;
double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }

#define opencv 0


// NV12
char* get_camerasrc_nv12(int devIndex) {
    const int cam_width=1920;
    const int cam_height=1080;
    const int cam_frames=30;
    static char str[255]={'\0'};
	snprintf(str, sizeof(str)-1
		, "v4l2src device=/dev/video%d ! video/x-raw,format=NV12,width=%d,height=%d,framerate=%d/1 ! appsink"
		, devIndex
		, cam_width
		, cam_height
		, cam_frames
		);
	return str;
}

//------------------------------------------------------------------------------
//功能：将YUV420视频帧数据填充到MPP buffer
//说明：使用16字节对齐，MPP可以实现零拷贝，提高效率
//------------------------------------------------------------------------------
void read_yuv_buffer(RK_U8 *buf, Mat &yuvImg, RK_U32 width, RK_U32 height)
{
    RK_U8 *buf_y = buf;
    RK_U8 *buf_u = buf + width * height;
    RK_U8 *buf_v = buf_u + width * height / 4;
    //
    RK_U8 *yuvImg_y = yuvImg.data;
    RK_U8 *yuvImg_u = yuvImg_y + width * height;
    RK_U8 *yuvImg_v = yuvImg_u + width * height / 4;
    //
    memcpy(buf_y, yuvImg_y, width * height);
    memcpy(buf_u, yuvImg_u, width * height / 4);
    memcpy(buf_v, yuvImg_v, width * height / 4);
}

static char optstr[] = "?::w:h:I:";
static const struct option long_options[] = {
    {"width", required_argument, NULL, 'w'},
    {"height", required_argument, NULL, 'h'},
    {"camid", required_argument, NULL, 'I'},
    {"help", optional_argument, NULL, '?'},
    {NULL, 0, NULL, 0},
};

static void print_usage(const char *name) {
  printf("usage example:\n");
  printf("\t%s [-I 0]\n", name);
  printf("\t-w | --width: VI width, Default:1920\n");
  printf("\t-h | --heght: VI height, Default:1080\n");
  printf("\t-I | --camid: camera file, Default /dev/video11\n");
}


int main(int argc,char* argv[]) 
{ 
    int width = 1920;
    int height = 1080;
    void *mpp_frame_addr = NULL;
    static int frame_index = 0;
    int enc_data_size;
    RK_S32 cam_frm_idx = -1;
    RK_U32 cap_num = 0;
    MppBuffer cam_buf = NULL;
    FILE *fp_output;

    VideoCapture *pCapture=NULL;

    char *camera_file = (char*)"/dev/video11";
    int c;

    while ((c = getopt_long(argc, argv, optstr, long_options, NULL)) != -1) {
        const char *tmp_optarg = optarg;
        switch (c) {
        case 'I':
            if (!optarg && NULL != argv[optind] && '-' != argv[optind][0]) {
                tmp_optarg = argv[optind++];
            }
            if (tmp_optarg) {
                camera_file = (char *)tmp_optarg;
            }
            break;
        case 'w':
            width = atoi(optarg);
            break;
        case 'h':
            height = atoi(optarg);
            break;
        case '?':
        default:
            print_usage(argv[0]);
            return 0;
        }
    }

#if opencv
    
	errno = 0;
	char *endptr;
	long int devIndex = strtol(argv[1], &endptr, 10);
	if (endptr == argv[1] || errno == ERANGE) {
		std::cerr << "Invalid parameter: " << argv[1] << '\n';
		exit (1);
	}
    printf("open video%d\r\n",devIndex);

    	// 这里主要是通过gstream调取摄像头
    pCapture = new VideoCapture(get_camerasrc_nv12(devIndex),cv::CAP_GSTREAMER);
    if (!pCapture->isOpened()) {
    	printf("Fail to open camera.\n");
    	exit (1);
    }else{
    	printf("open camera sucessful.\n");
    }
#endif
	
    // init rtsp
    g_rtsplive = create_rtsp_demo(554);
    g_rtsp_session = rtsp_new_session(g_rtsplive, "/live/main_stream");
    rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
    rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());

    MpiEncoderCtxInfo *ctxs = NULL;
    ctxs = mpp_calloc(MpiEncoderCtxInfo, 1);
    if (NULL == ctxs) {
        printf("failed to alloc context for instances\n");
        return -1;
    }
    

    fp_output = fopen("out.h264", "w+b");
    if (NULL == fp_output) {
        printf("failed to open output file \n");
        return -1;
    }

    ctxs->width =width;
    ctxs->height = height;
    ctxs->hor_stride = width;
    ctxs->ver_stride = height;
    ctxs->fmt = MPP_FMT_YUV420SP;
    ctxs->type = MPP_VIDEO_CodingAVC;

    ctxs->cam_ctx = camera_source_init(camera_file, 4, ctxs->width, ctxs->height, ctxs->fmt);
    printf("open camera device %s\n",camera_file);
    if (ctxs->cam_ctx == NULL){
        printf("open %s failed !\n", camera_file);
        return -1;
    }

    init_encoder(ctxs);

    mpp_frame_addr =mpp_buffer_get_ptr(ctxs->frm_buf);

    int enc_buf_size = ctxs->frame_size;
    char *enc_data = (char *)malloc(enc_buf_size);

	// fps counter end
	Mat frame,img;
    struct timeval time;
    gettimeofday(&time, NULL);
    auto initTime = time.tv_sec * 1000 + time.tv_usec / 1000;

    gettimeofday(&time, NULL);
    long tmpTime, lopTime = time.tv_sec * 1000 + time.tv_usec / 1000;

	while (1) { 
		frame_index++;

#if opencv
        *pCapture >> frame;
        if (frame.empty()) {
        	printf("Fail to read frame.\n");
        	break;
        }
        //cv::cvtColor(frame, img, COLOR_YUV2GRAY_NV12);
        read_yuv_buffer((RK_U8*)mpp_frame_addr, frame, width, height);
        enc_data_size =  test_mpp_run(ctxs,ctxs->ctx.frm_buf, enc_data, enc_buf_size);
#endif

        cam_frm_idx = camera_source_get_frame(ctxs->cam_ctx);

        /* skip unstable frames */
        if (cap_num++ < 50) {
            camera_source_put_frame(ctxs->cam_ctx, cam_frm_idx);
            continue;
        }

        cam_buf = camera_frame_to_buf(ctxs->cam_ctx, cam_frm_idx);

        // Encode to file
        if (frame_index == 1)
        {
            enc_data_size = GetHeader(ctxs,enc_data);

	        fwrite(enc_data, 1, enc_data_size,  fp_output);
            if (g_rtsplive && g_rtsp_session) {
                rtsp_tx_video(g_rtsp_session, (const uint8_t *)enc_data, enc_data_size,frame_index);
                rtsp_do_event(g_rtsplive);
            }
        }
	
        memset(enc_data, 0, enc_buf_size);
        enc_data_size =  test_mpp_run(ctxs,cam_buf, enc_data);
        if (g_rtsplive && g_rtsp_session) {
            rtsp_tx_video(g_rtsp_session, (const uint8_t *)enc_data, enc_data_size,frame_index);
            rtsp_do_event(g_rtsplive);
        }
	    fwrite(enc_data, 1, enc_data_size, fp_output);

        if (cam_frm_idx >= 0)
            camera_source_put_frame(ctxs->cam_ctx, cam_frm_idx);

        // fps counter begin
        if(frame_index % 60 == 0){
            gettimeofday(&time, NULL);
            tmpTime = time.tv_sec * 1000 + time.tv_usec / 1000;
            printf("60帧平均帧率:\t%f帧\n", 60000.0 / (float)(tmpTime - lopTime));
            lopTime = tmpTime;
        }

	} 
	
	// release
    if (g_rtsplive)
    rtsp_del_demo(g_rtsplive);
    test_ctx_deinit(ctxs);
    fclose(fp_output);
	pCapture->release();
	delete pCapture;
	return 0; 
}
