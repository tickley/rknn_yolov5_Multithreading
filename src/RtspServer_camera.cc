// RTSP Server

#include "xop/RtspServer.h"
#include "net/Timer.h"
#include <thread>
#include <memory>
#include <iostream>
#include <string>
#include <getopt.h>
#include <stdio.h>
#include <sys/time.h>
#include "camera_source.h"
#include "mpi_encoder.h"


void SendFrameThread(xop::RtspServer* rtsp_server, xop::MediaSessionId session_id, void *data);

double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }
std::string getLocalIp()
{
    int sockfd = 0;
    char buf[512] = { 0 };
    struct ifconf ifconf;
    struct ifreq  *ifreq;
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        close(sockfd);
        return "0.0.0.0";
    }

    ifconf.ifc_len = 512;
    ifconf.ifc_buf = buf;
    if (ioctl(sockfd, SIOCGIFCONF, &ifconf) < 0) {
        close(sockfd);
        return "0.0.0.0";
    }

    close(sockfd);

    ifreq = (struct ifreq*)ifconf.ifc_buf;
    for (int i = (ifconf.ifc_len / sizeof(struct ifreq)); i>0; i--) {
        if (ifreq->ifr_flags == AF_INET) {
            if (strcmp(ifreq->ifr_name, "lo") != 0) {
                return inet_ntoa(((struct sockaddr_in*)&(ifreq->ifr_addr))->sin_addr);
            }
            ifreq++;
        }
    }
    return "0.0.0.0";
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

int main(int argc, char **argv)
{	
	int clients = 0;
	std::string suffix = "live";
	std::string ip = "127.0.0.1";
	ip = getLocalIp();
	std::string port = "5543";
	std::string rtsp_url = "rtsp://" + ip + ":" + port + "/" + suffix;

    int width = 1920;
    int height = 1080;
    MppBuffer mpp_frame = NULL;
    void *mpp_frame_addr = NULL;
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

	MpiEncoderCtxInfo *ctxs = NULL;
    ctxs = mpp_calloc(MpiEncoderCtxInfo, 1);
    if (NULL == ctxs) {
        mpp_err("failed to alloc context for instances\n");
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
        printf("open %s failed!\n", camera_file);
        return -1;
    }
    init_encoder(ctxs);

    mpp_frame_addr =mpp_buffer_get_ptr(ctxs->frm_buf);

	std::shared_ptr<xop::EventLoop> event_loop(new xop::EventLoop());  
	std::shared_ptr<xop::RtspServer> server = xop::RtspServer::Create(event_loop.get());
	if (!server->Start("0.0.0.0", atoi(port.c_str()))) {
		printf("RTSP Server listen on %s failed.\n", port.c_str());
		return 0;
	}
	
#ifdef AUTH_CONFIG
	server->SetAuthConfig("-_-", "admin", "12345");
#endif
	 
	xop::MediaSession *session = xop::MediaSession::CreateNew("live"); // url: rtsp://ip/live
	session->AddSource(xop::channel_0, xop::H264Source::CreateNew()); 
	session->AddSource(xop::channel_1, xop::AACSource::CreateNew(44100,2));
	// session->StartMulticast(); /* 开启组播(ip,端口随机生成), 默认使用 RTP_OVER_UDP, RTP_OVER_RTSP */

	session->AddNotifyConnectedCallback([] (xop::MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port){
		printf("RTSP client connect, ip=%s, port=%hu \n", peer_ip.c_str(), peer_port);
	});
   
	session->AddNotifyDisconnectedCallback([](xop::MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port) {
		printf("RTSP client disconnect, ip=%s, port=%hu \n", peer_ip.c_str(), peer_port);
	});

	std::cout << "URL: " << rtsp_url << std::endl;
        
	xop::MediaSessionId session_id = server->AddSession(session); 
	//server->RemoveSession(session_id); /* 取消会话, 接口线程安全 */
         
	std::thread thread(SendFrameThread, server.get(), session_id, std::ref(ctxs));
	thread.detach();

	while(1) {
		xop::Timer::Sleep(100);
	}

	getchar();
	return 0;
}

void SendFrameThread(xop::RtspServer* rtsp_server, xop::MediaSessionId session_id, void *data)
{       
	MpiEncoderCtxInfo *ctxs = (MpiEncoderCtxInfo *)data;
	static int frame_index = 0;
	RK_S32 cam_frm_idx = -1;
	int enc_buf_size = ctxs->frame_size;
    char *enc_data = (char *)malloc(enc_buf_size);
	RK_U32 cap_num = 0;
    MppBuffer cam_buf = NULL;
	int enc_data_size;
    struct timeval start_time, stop_time;
	
    time_t start, end;
	int fps_counter = 0;
	double sec,fps;
    time(&start);
    
	while(1) {
        
        //获取一帧 H264, 打包
		cam_frm_idx = camera_source_get_frame(ctxs->cam_ctx);

        /* skip unstable frames */
        if (cap_num++ < 50) {
            camera_source_put_frame(ctxs->cam_ctx, cam_frm_idx);
            continue;
        }

        cam_buf = camera_frame_to_buf(ctxs->cam_ctx, cam_frm_idx);

        // Encode to file
        if (frame_index == 1) {
            enc_data_size = GetHeader(ctxs,enc_data);
				
			xop::AVFrame videoFrame = {0};
			videoFrame.type = 0; // 建议确定帧类型。I帧(xop::VIDEO_FRAME_I) P帧(xop::VIDEO_FRAME_P)
			videoFrame.size = enc_data_size;  // 视频帧大小 
			videoFrame.timestamp = xop::H264Source::GetTimestamp(); // 时间戳, 建议使用编码器提供的时间戳
			videoFrame.buffer.reset(new uint8_t[videoFrame.size]);                    
			memcpy(videoFrame.buffer.get(), enc_data, videoFrame.size);					
			rtsp_server->PushFrame(session_id, xop::channel_0, videoFrame); //送到服务器进行转发, 接口线程安全
        }
	
        //gettimeofday(&start_time, NULL);  
        memset(enc_data, 0, enc_buf_size);
        enc_data_size =  test_mpp_run(ctxs,cam_buf, enc_data);

		xop::AVFrame videoFrame = {0};
		videoFrame.type = 0; // 建议确定帧类型。I帧(xop::VIDEO_FRAME_I) P帧(xop::VIDEO_FRAME_P)
		videoFrame.size = enc_data_size;  // 视频帧大小 
		videoFrame.timestamp = xop::H264Source::GetTimestamp(); // 时间戳, 建议使用编码器提供的时间戳
		videoFrame.buffer.reset(new uint8_t[videoFrame.size]);                    
		memcpy(videoFrame.buffer.get(), enc_data, videoFrame.size);					
		rtsp_server->PushFrame(session_id, xop::channel_0, videoFrame); //送到服务器进行转发, 接口线程安全

        // gettimeofday(&stop_time, NULL);	
        // printf("once run use %f ms\n", (__get_us(stop_time) - __get_us(start_time)) / 1000);			
        if (cam_frm_idx >= 0)
            camera_source_put_frame(ctxs->cam_ctx, cam_frm_idx);    

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
                    
        {				
            /*
            //获取一帧 AAC, 打包
            xop::AVFrame audioFrame = {0};
            audioFrame.type = xop::AUDIO_FRAME;
            audioFrame.size = audio frame size;  /* 音频帧大小 
            audioFrame.timestamp = xop::AACSource::GetTimestamp(44100); // 时间戳
            audioFrame.buffer.reset(new uint8_t[audioFrame.size]);                    
            memcpy(audioFrame.buffer.get(), audio frame data, audioFrame.size);

            rtsp_server->PushFrame(session_id, xop::channel_1, audioFrame); // 送到服务器进行转发, 接口线程安全
            */
        }		
	

		//xop::Timer::Sleep(1); /* 实际使用需要根据帧率计算延时! */
	}
}
