// Copyright (c) 2021 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*-------------------------------------------
                Includes
-------------------------------------------*/
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <opencv2/opencv.hpp>
#include "RgaUtils.h"
#include "postprocess.h"
#include "rknn_api.h"
#include "preprocess.h"
#include "rknnPool_rtsp.hpp"
#include "ThreadPool.hpp"
#include "rtsp_demo.h"
#include "utils/mpp_encoder.h"

using std::queue;
using std::time;
using std::time_t;
using std::vector;
using namespace cv;

INPUT_MODE INPUT;
std::string option = "opencv";

#define MPP_ALIGN(x, a)         (((x)+(a)-1)&~((a)-1))
rtsp_demo_handle g_rtsplive = NULL;
static rtsp_session_handle g_rtsp_session;

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

int main(int argc, char **argv)
{

  char *model_name = NULL;
  if (argc < 3) {
    printf("Usage: %s <rknn model> <jpg> \n", argv[0]);
    return -1;
  }
  model_name = (char *)argv[1]; // 参数二，模型所在路径 
  printf("模型名称:\t%s\n", model_name);
  
  if (argc >= 4)
    option = argv[3];

  cv::VideoCapture *capture=NULL;
	errno = 0;
	char *endptr;
	long int devIndex = strtol(argv[2], &endptr, 10); // 参数三, 视频/摄像头
	if (endptr == argv[2] || errno == ERANGE) {
		std::cout << "Input parameter: " << argv[2] << " " << option << '\n';
    capture = new cv::VideoCapture();
    capture->open(endptr);
    INPUT = VIDEO_MODE;
	} else {
    std::cout << "Input parameter: /dev/video" << devIndex << " " << option << '\n';
    capture = new VideoCapture(get_camerasrc_nv12(devIndex),cv::CAP_GSTREAMER);
    INPUT = CAMERA_MODE;
  }

  int width = 1280;
  int height = 720;
  void *mpp_frame = NULL;
  int mpp_frame_fd = 0;
  void *mpp_frame_addr = NULL;
  int enc_data_size;
  static int frame_index = 0;

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
    enc_params.hor_stride = MPP_ALIGN(width, 16);
    enc_params.ver_stride = MPP_ALIGN(height, 16);
    enc_params.fmt = MPP_FMT_YUV420SP;
    enc_params.type = MPP_VIDEO_CodingAVC; //H264
    mpp_encoder->Init(enc_params, NULL);
    mpp_frame = mpp_encoder->GetInputFrameBuffer();
    mpp_frame_fd = mpp_encoder->GetInputFrameBufferFd(mpp_frame);
    mpp_frame_addr = mpp_encoder->GetInputFrameBufferAddr(mpp_frame);

    int enc_buf_size = mpp_encoder->GetFrameSize();
    char *enc_data = (char *)malloc(enc_buf_size);

  cv::namedWindow("Camera FPS");
  if (!capture->isOpened()) {
        printf("Fail to open camera.\n");
        exit (1);
  }else{
        printf("open camera sucessful.\n");
  }

  // 设置线程数
  int n = 6, frames = 0;
  printf("线程数:\t%d\n", n);
  // 类似于多个rk模型的集合?
  vector<rknn_lite *> rkpool;
  // 线程池
  dpool::ThreadPool pool(n);
  // 线程队列
  queue<std::future<int>> futs;

  //初始化
  for (int i = 0; i < n; i++)
  {
    rknn_lite *ptr = new rknn_lite(model_name, i % 3);
    rkpool.push_back(ptr);
    *capture >> ptr->ori_img;
    futs.push(pool.submit(&rknn_lite::interf, &(*ptr)));
  }

  struct timeval time;
  gettimeofday(&time, nullptr);
  auto initTime = time.tv_sec * 1000 + time.tv_usec / 1000;

  gettimeofday(&time, nullptr);
  long tmpTime, lopTime = time.tv_sec * 1000 + time.tv_usec / 1000;
  Mat img;
  while (capture->isOpened())
  {
    if (futs.front().get() != 0)
      break;
    futs.pop();
    frame_index++;

    read_yuv_buffer((RK_U8*)mpp_frame_addr, rkpool[frames % n]->ori_img, width, height);
    // Encode to file
    // Write header on first frame
    if (frame_index == 1)
    {
        enc_data_size = mpp_encoder->GetHeader(enc_data, enc_buf_size);
        if (g_rtsplive && g_rtsp_session) {
            rtsp_tx_video(g_rtsp_session, (const uint8_t *)enc_data, enc_data_size,frame_index);
            rtsp_do_event(g_rtsplive);
        }
    }
    memset(enc_data, 0, enc_buf_size);
    enc_data_size = mpp_encoder->Encode(mpp_frame, enc_data, enc_buf_size);
    if (g_rtsplive && g_rtsp_session) {
        rtsp_tx_video(g_rtsp_session, (const uint8_t *)enc_data, enc_data_size,frame_index);
        rtsp_do_event(g_rtsplive);
    }
    cv::cvtColor(rkpool[frames % n]->ori_img, img, cv::COLOR_YUV2BGR_NV12);
    cv::imshow("Camera FPS", img);

    if (cv::waitKey(1) == 'q') // 延时1毫秒,按q键退出
      break;
    if(!capture->read(rkpool[frames % n]->ori_img))
      break;
    futs.push(pool.submit(&rknn_lite::interf, &(*rkpool[frames++ % n])));

    if(frames % 60 == 0){
        gettimeofday(&time, nullptr);
        tmpTime = time.tv_sec * 1000 + time.tv_usec / 1000;
        printf("60帧平均帧率:\t%f帧\n", 60000.0 / (float)(tmpTime - lopTime));
        lopTime = tmpTime;
    }
  }

  gettimeofday(&time, nullptr);
  printf("\n平均帧率:\t%f帧\n", float(frames) / (float)(time.tv_sec * 1000 + time.tv_usec / 1000 - initTime + 0.0001) * 1000.0);

  // 释放剩下的资源
  while (!futs.empty())
  {
    if (futs.front().get())
      break;
    futs.pop();
  }
  for (int i = 0; i < n; i++)
  delete rkpool[i];
  capture->release();
  delete capture;
  cv::destroyAllWindows();
  return 0;
}
