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

#define _BASETSD_H

#include "RgaUtils.h"
#include "postprocess.h"
#include "rknn_api.h"
#include "preprocess.h"
#include "rknnPool.hpp"
#include "ThreadPool.hpp"

using std::queue;
using std::time;
using std::time_t;
using std::vector;
using namespace cv;

INPUT_MODE INPUT;
std::string option = "opencv";

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

  while (capture->isOpened())
  {
    if (futs.front().get() != 0)
      break;
    futs.pop();
    cv::imshow("Camera FPS", rkpool[frames % n]->ori_img);
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
