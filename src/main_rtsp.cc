#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <cstdlib>
#include <stdio.h>
#include <sys/time.h>
#include "rtsp_demo.h"
#include "utils/mpp_decoder.h"
#include "utils/mpp_encoder.h"
#include "utils/drawing.h"
#include "utils/camera_source.h"

using namespace std;
using namespace cv;
#define MPP_ALIGN(x, a)         (((x)+(a)-1)&~((a)-1))
rtsp_demo_handle g_rtsplive = NULL;
static rtsp_session_handle g_rtsp_session;
double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }
// MJPEG
char* get_camerasrc_mjpeg(int devIndex) {
    // for T4, M4B
    const int cam_width=1280;
    const int cam_height=720;
    const int cam_frames=30;

    static char str[255]={'\0'};
	snprintf(str, sizeof(str)-1
		, "v4l2src device=/dev/video%d io-mode=4 ! image/jpeg,width=%d,height=%d,framerate=%d/1 ! jpegdec ! videoconvert ! video/x-raw,format=BGR ! appsink sync=false"
		, devIndex
		, cam_width
		, cam_height
		, cam_frames
		);
	return str;
}

// NV12
char* get_camerasrc_nv12(int devIndex) {
    const int cam_width=1280;
    const int cam_height=720;
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
    RK_U8 *buf_u = buf + MPP_ALIGN(width, 16) * MPP_ALIGN(height, 16);
    RK_U8 *buf_v = buf_u + MPP_ALIGN(width, 16) * MPP_ALIGN(height, 16) / 4;
    //
    RK_U8 *yuvImg_y = yuvImg.data;
    RK_U8 *yuvImg_u = yuvImg_y + width * height;
    RK_U8 *yuvImg_v = yuvImg_u + width * height / 4;
    //
    memcpy(buf_y, yuvImg_y, width * height);
    memcpy(buf_u, yuvImg_u, width * height / 4);
    memcpy(buf_v, yuvImg_v, width * height / 4);
}

int main(int argc,char* argv[]) 
{ 
	// check args
    if(argc < 2) {
        printf("Please provide the number of cameras, it must be 1,2 or 3.\n");
        exit(0);
    }
	// errno = 0;
	// char *endptr;
	// long int devIndex = strtol(argv[1], &endptr, 10);
	// if (endptr == argv[1] || errno == ERANGE) {
	// 	std::cerr << "Invalid parameter: " << argv[1] << '\n';
	// 	exit (1);
	// }


    int width = 1920;
    int height = 1080;
    MppBuffer mpp_frame = NULL;
    int mpp_frame_fd = 0;
    void *mpp_frame_addr = NULL;
    static int frame_index = 0;
    int enc_data_size;
    RK_S32 cam_frm_idx = -1;
    RK_U32 cap_num = 0;

    MppFrameFormat fmt = MPP_FMT_YUV420SP;

    CamSource *cam_ctx = NULL;
    if (!strncmp(argv[1], "/dev/video", 10)) {
        printf("open camera device\n");
        cam_ctx = camera_source_init(argv[1], 4, width, height, fmt);
        printf("new framecap ok\n");
        if (cam_ctx == NULL)
            printf("open %s fail\n", argv[1]);
    }
	VideoCapture *pCapture=NULL;
	// printf("open video%d\r\n",devIndex);

    // init rtsp
    g_rtsplive = create_rtsp_demo(554);
    g_rtsp_session = rtsp_new_session(g_rtsplive, "/live/main_stream");
    rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
    rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());

    // init encoder
    MppEncoder *mpp_encoder = new MppEncoder();
    MppEncoderParams enc_params;
    memset(&enc_params, 0, sizeof(MppEncoderParams));
    enc_params.width = width;
    enc_params.height = height;
    enc_params.hor_stride = width;  //对齐有问题去掉就好了
    enc_params.ver_stride = height;
    enc_params.fmt = MPP_FMT_YUV420SP;
    enc_params.type = MPP_VIDEO_CodingAVC; //H264
    mpp_encoder->Init(enc_params, NULL);
    mpp_frame = mpp_encoder->GetInputFrameBuffer();
    mpp_frame_fd = mpp_encoder->GetInputFrameBufferFd(mpp_frame);
    mpp_frame_addr = mpp_encoder->GetInputFrameBufferAddr(mpp_frame);

    int enc_buf_size = mpp_encoder->GetFrameSize();
    char *enc_data = (char *)malloc(enc_buf_size);

	// mjpg
	//pCapture = new VideoCapture(get_camerasrc_mjpeg(devIndex),cv::CAP_GSTREAMER);
			// 这里主要是通过gstream调取摄像头
	// pCapture = new VideoCapture(get_camerasrc_nv12(devIndex),cv::CAP_GSTREAMER);
	// if (!pCapture->isOpened()) {
	// 	printf("Fail to open camera.\n");
	// 	exit (1);
	// }else{
	// 	printf("open camera sucessful.\n");
	// }

	// fps counter begin
	time_t start, end;
    struct timeval start_time, stop_time;
	int fps_counter = 0;
	double sec,fps;
    time(&start);
	// fps counter end
	Mat frame,img;
    FILE *fp_output = fopen("out.h264","w+b");
     if (NULL == fp_output) {
            printf("failed to open output file \n");

        }
	while (1) { 
		frame_index++;
		// *pCapture >> frame;
		// if (frame.empty()) {
		// 	printf("Fail to read frame.\n");
		// 	break;
		// }

        cam_frm_idx = camera_source_get_frame(cam_ctx);

        /* skip unstable frames */
        if (cap_num++ < 50) {
            camera_source_put_frame(cam_ctx, cam_frm_idx);
            continue;
        }

        mpp_frame = camera_frame_to_buf(cam_ctx, cam_frm_idx);

		// //cv::cvtColor(frame, img, COLOR_YUV2GRAY_NV12);
        // gettimeofday(&start_time, NULL);
        // read_yuv_buffer((RK_U8*)mpp_frame_addr, frame, width, height);
        // gettimeofday(&stop_time, NULL);
        // // 打印单次推理耗时
        // printf("once run use %f ms\n", (__get_us(stop_time) - __get_us(start_time)) / 1000);
        // Encode to file
        // Write header on first frame
        if (frame_index == 1)
        {
            enc_data_size = mpp_encoder->GetHeader(enc_data, enc_buf_size);
            if (g_rtsplive && g_rtsp_session) {
                rtsp_tx_video(g_rtsp_session, (const uint8_t *)enc_data, enc_data_size,frame_index);
                rtsp_do_event(g_rtsplive);
            }
            fwrite(enc_data, 1, enc_data_size, fp_output);
        }
        memset(enc_data, 0, enc_buf_size);
        
        enc_data_size = mpp_encoder->Encode(mpp_frame, enc_data, enc_buf_size);
        if (g_rtsplive && g_rtsp_session) {
            rtsp_tx_video(g_rtsp_session, (const uint8_t *)enc_data, enc_data_size,frame_index);
            rtsp_do_event(g_rtsplive);
        }
         fwrite(enc_data, 1, enc_data_size, fp_output);

        if (cam_frm_idx >= 0)
            camera_source_put_frame(cam_ctx, cam_frm_idx);

		//imshow("gs_camera",frame);
		
		// fps counter begin
		time(&end);
		fps_counter++;
		sec = difftime(end, start);
		if (sec > 1) {
			fps = fps_counter/sec;
			printf("%.2f fps\n", fps);
			fps_counter=0;
			time(&start);
						}
		// fps counter end
		if (waitKey(30) & 0xFF == 'q') 
			break;
	} 
	
	// release
      if (g_rtsplive)
    rtsp_del_demo(g_rtsplive);
    camera_source_deinit(cam_ctx);
    cam_ctx = NULL;
	pCapture->release();
	delete pCapture;
	return 0; 
}
