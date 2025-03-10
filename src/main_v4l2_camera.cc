#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/core/ocl.hpp>
#include <sys/mman.h>
#include <cstdlib>
#include <stdio.h>
#include <getopt.h>
#include <sys/time.h>

#include "im2d.h"
#include "rga.h"
#include "RgaUtils.h"
#include "utils/dma_alloc.h"
#include "utils/camera_source.h"


using namespace std;
using namespace cv;

double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }
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

// typedef struct Cambufer_t {
//     void        *data;
//     size_t      length;
// } Cambufer;
// struct Camera {
//     Cambufer    fbuf[6];// frame buffers
// };

cv::Mat importDmaBufToMat(Camera *data, RK_S32 idx,int width, int height) {
    // 映射 DMA-BUF 到用户空间
    // size_t size = width * height * 3 / 2; // NV12 格式大小
    // void* data = mmap(nullptr, size, PROT_READ, MAP_SHARED, dmafd, 0);
    // if (data == MAP_FAILED) {
    //     perror("无法映射 DMA-BUF");
    //     return cv::Mat();
    // }

    struct timeval start_time, stop_time;
    // 创建 OpenCV Mat 对象
    cv::Mat nv12Mat(height + height / 2, width, CV_8UC1, data->fbuf[idx].data);
    // cv::UMat src_umat;
    // nv12Mat.copyTo(src_umat);
gettimeofday(&start_time, NULL);
    // 转换为 BGR 格式（可选）
    cv::Mat resized_img;
    cv::cvtColor(nv12Mat, resized_img, cv::COLOR_YUV2BGR_NV12);
    

    // rga_buffer_t src;
    // rga_buffer_t dst;
    // memset(&src, 0, sizeof(src));
    // memset(&dst, 0, sizeof(dst));

    // cv::Mat resized_img(height, width, CV_8UC3);

    // rga_buffer_handle_t src_handle, dst_handle;
    // int src_buf_size, dst_buf_size;
    // src_buf_size = width * height * get_bpp_from_format(RK_FORMAT_YCbCr_420_SP);
    // dst_buf_size = width * height * get_bpp_from_format(RK_FORMAT_BGR_888);

    // src_handle = importbuffer_virtualaddr((void *)data->fbuf[idx].data, src_buf_size);
    // dst_handle = importbuffer_virtualaddr((void *)resized_img.data, dst_buf_size);
    // if (src_handle == 0 || dst_handle == 0) {
    //     printf("importbuffer failed!\n");

    // }

    // im_rect src_rect;
    // im_rect dst_rect;
    // memset(&src_rect, 0, sizeof(src_rect));
    // memset(&dst_rect, 0, sizeof(dst_rect));

    // src = wrapbuffer_handle(src_handle, width, height, RK_FORMAT_YCbCr_420_SP);
    // dst = wrapbuffer_handle(dst_handle, width, height, RK_FORMAT_BGR_888);
    // int ret = imcheck(src, dst, src_rect, dst_rect);
    // if (IM_STATUS_NOERROR != ret)
    // {
    //     fprintf(stderr, "rga check error! %s", imStrError((IM_STATUS)ret));
    // }
    // IM_STATUS STATUS = imresize(src, dst);

    //  releasebuffer_handle(src_handle);
    //  releasebuffer_handle(dst_handle);
     
    gettimeofday(&stop_time, NULL);
    // 打印单次推理耗时
    printf("once run use %f ms\n", (__get_us(stop_time) - __get_us(start_time)) / 1000);

    return resized_img;
}



int main(int argc,char* argv[]) 
{ 
    int width = 1920;
    int height = 1080;
    void *mpp_frame_addr = nullptr;
    static int frame_index = 0;
    int enc_data_size;
    RK_S32 cam_frm_idx = -1;
    RK_U32 cap_num = 0;
 
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

//  // 检查系统是否支持OpenCL
//     if (!cv::ocl::haveOpenCL()) {
//         std::cout << "系统不支持OpenCL。" << std::endl;
//         return -1;
//     }

//     // 输出OpenCL设备信息
//     cv::ocl::Context context;
//     if (!context.create(cv::ocl::Device::TYPE_GPU)) {
//         std::cout << "未找到可用的GPU设备，使用CPU执行。" << std::endl;
//     } else {
//         cv::ocl::Device device = cv::ocl::Device::getDefault();
//         std::cout << "使用的OpenCL设备：" << device.name() << std::endl;
//     }
//     cv::ocl::setUseOpenCL(true);



    CamSource *cam_ctx = camera_source_init(camera_file, 4, width, height, MPP_FMT_YUV420SP);
    printf("open camera device %s\n",camera_file);
    if (cam_ctx == NULL){
        printf("open %s failed !\n", camera_file);
        return -1;
    }

    int dst_dma_fd;
    rga_buffer_t src;
    rga_buffer_t dst;
    im_rect src_rect;
    im_rect dst_rect;
    cv::Mat resized_img(height, width, CV_8UC3);
    int src_buf_size, dst_buf_size;
    src_buf_size = width * height * get_bpp_from_format(RK_FORMAT_YCbCr_420_SP);
    dst_buf_size = width * height * get_bpp_from_format(RK_FORMAT_BGR_888);
    rga_buffer_handle_t src_handle[4], dst_handle;
    
    // Camera *data;
    // data = (Camera *)calloc(1,sizeof(Camera));
    for (int i = 0; i < 4; ++i) {
        // size_t size = width * height * 3 / 2; // NV12 格式大小
        // data->fbuf[i].data = mmap(nullptr, size, PROT_READ, MAP_SHARED, camera_frame_to_fd(cam_ctx, i), 0);
        // if (data == MAP_FAILED) {
        //     perror("无法映射 DMA-BUF");
        // }
        src_handle[i] = importbuffer_fd(camera_frame_to_fd(cam_ctx, i), src_buf_size);
        //src_handle[i] = importbuffer_virtualaddr(data->fbuf[i].data, src_buf_size);
        if (src_handle[i] == 0) {
            printf("importbuffer failed!\n");
        }
    }

    int ret = dma_buf_alloc(DMA_HEAP_DMA32_PATH, dst_buf_size, &dst_dma_fd, (void **)&resized_img.data);
    if (ret < 0) {
        printf("alloc dst dma_heap buffer failed!\n");
        return -1;
    }

    //dst_handle = importbuffer_virtualaddr((void *)resized_img.data, dst_buf_size);
    dst_handle = importbuffer_fd(dst_dma_fd, dst_buf_size);
    if ( dst_handle == 0) {
        printf("importbuffer failed!\n");

    }


    struct timeval start_time, stop_time;

	// fps counter end
    struct timeval time;
    gettimeofday(&time, NULL);
    auto initTime = time.tv_sec * 1000 + time.tv_usec / 1000;

    gettimeofday(&time, NULL);
    long tmpTime, lopTime = time.tv_sec * 1000 + time.tv_usec / 1000;

	while (1) { 
		frame_index++;

        cam_frm_idx = camera_source_get_frame(cam_ctx);

        /* skip unstable frames */
        if (cap_num++ < 50) {
            camera_source_put_frame(cam_ctx, cam_frm_idx);
            continue;
        }

        // cv::Mat image = importDmaBufToMat(data,cam_frm_idx, width, height);
        // if (image.empty()) {
        //     std::cerr << "无法导入 DMA-BUF！" << std::endl;
        //     return -1;
        // }

       gettimeofday(&start_time, NULL); 
        
        // cv::Mat nv12Mat(height + height / 2, width, CV_8UC1,data->fbuf[cam_frm_idx].data);
        // cv::cvtColor(nv12Mat, resized_img, cv::COLOR_YUV2BGR_NV12);

        memset(&src, 0, sizeof(src));
        memset(&dst, 0, sizeof(dst));
        memset(&src_rect, 0, sizeof(src_rect));
        memset(&dst_rect, 0, sizeof(dst_rect));

        src = wrapbuffer_handle(src_handle[cam_frm_idx], width, height, RK_FORMAT_YCbCr_420_SP);
        dst = wrapbuffer_handle(dst_handle, width, height, RK_FORMAT_BGR_888);
        int ret = imcheck(src, dst, src_rect, dst_rect);
        if (IM_STATUS_NOERROR != ret) {
            fprintf(stderr, "rga check error! %s", imStrError((IM_STATUS)ret));
        }

        IM_STATUS STATUS = imresize(src, dst);
    
    // 打印单次推理耗时

        gettimeofday(&stop_time, NULL);
        printf("once run use %f ms\n", (__get_us(stop_time) - __get_us(start_time)) / 1000);

        imshow("Camera FPS", resized_img);
        if (cv::waitKey(1) == 'q') // 延时1毫秒,按q键退出
            break;


        if (cam_frm_idx >= 0)
            camera_source_put_frame(cam_ctx, cam_frm_idx);

        // fps counter begin
        if(frame_index % 60 == 0){
            gettimeofday(&time, NULL);
            tmpTime = time.tv_sec * 1000 + time.tv_usec / 1000;
            printf("60帧平均帧率:\t%f帧\n", 60000.0 / (float)(tmpTime - lopTime));
            lopTime = tmpTime;
        }

	}
    for (int i = 0; i < 4; ++i) {
        releasebuffer_handle(src_handle[i]);
    }
    releasebuffer_handle(dst_handle);
    dma_buf_free(dst_buf_size, &dst_dma_fd, resized_img.data);
    // free(data);
    camera_source_deinit(cam_ctx);
    cv::destroyAllWindows();
	return 0; 
}
