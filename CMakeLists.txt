cmake_minimum_required(VERSION 3.4.1)

project(rknn_yolov5_demo)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-pthread")

# skip 3rd-party lib dependencies
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--allow-shlib-undefined")

# install target and libraries
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install/rknn_yolov5_demo_${CMAKE_SYSTEM_NAME})

set(CMAKE_SKIP_INSTALL_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

if(CMAKE_C_COMPILER MATCHES "aarch64")
  set(LIB_ARCH aarch64)
else()
  set(LIB_ARCH armhf)
endif()

if(TARGET_SOC STREQUAL "RK3588")
  add_definitions(-DRK3588)
endif()

include_directories(${CMAKE_SOURCE_DIR})

# rknn api
set(RKNN_API_PATH ${CMAKE_SOURCE_DIR}/include/runtime//${CMAKE_SYSTEM_NAME}/librknn_api)
set(RKNN_RT_LIB ${RKNN_API_PATH}/${LIB_ARCH}/librknnrt.so)
include_directories(${RKNN_API_PATH}/include)
include_directories(${CMAKE_SOURCE_DIR}/include/3rdparty)

# opencv
if(LIB_ARCH STREQUAL "armhf")
  set(OpenCV_DIR ${CMAKE_SOURCE_DIR}/include/3rdparty/opencv/opencv-linux-armhf/share/OpenCV)
else()
  if(TARGET_SOC STREQUAL "RK3566_RK3568")
    set(OpenCV_DIR ${CMAKE_SOURCE_DIR}/include/3rdparty/opencv/opencv-linux-aarch64/lib/cmake/opencv4)
  endif()
endif()

find_package(OpenCV REQUIRED)

# rga
# comes from https://github.com/airockchip/librga
set(RGA_PATH ${CMAKE_SOURCE_DIR}/include/3rdparty/rga/)
if(CMAKE_C_COMPILER MATCHES "aarch64")
  set(LIB_ARCH aarch64)
else()
  set(LIB_ARCH armhf)
endif()
set(RGA_LIB ${RGA_PATH}/libs//gcc-${LIB_ARCH}/librga.so)
include_directories( ${RGA_PATH}/include)

# mpp
set(MPP_PATH ${CMAKE_CURRENT_SOURCE_DIR}/include/3rdparty/mpp)
set(MPP_LIBS ${MPP_PATH}/${CMAKE_SYSTEM_NAME}/${LIB_ARCH}/librockchip_mpp.so)
include_directories(${MPP_PATH}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/mpp)

# zlmediakit rtsp
set(ZLMEDIAKIT_PATH ${CMAKE_SOURCE_DIR}/include/3rdparty/zlmediakit)
include_directories(${ZLMEDIAKIT_PATH}/include)
set(ZLMEDIAKIT_LIBS ${ZLMEDIAKIT_PATH}/${LIB_ARCH}/libmk_api.so ${ZLMEDIAKIT_PATH}/${LIB_ARCH}/librtsp.a)

if(ZLMEDIAKIT_LIBS)
  add_definitions(-DBUILD_VIDEO_RTSP)
endif()

set(CMAKE_INSTALL_RPATH "lib")

# rknn_yolov5_demo
include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(camera_rtsp_demo 
  src/main_rtsp.cc 
  utils/postprocess.cc 
  utils/preprocess.cc
  utils/mpp_decoder.cpp 
  utils/mpp_encoder.cpp 
  utils/drawing.cpp
  utils/camera_source.c
)

target_link_libraries(camera_rtsp_demo 
  ${RKNN_RT_LIB} 
  ${RGA_LIB} 
  ${OpenCV_LIBS} 
  ${ZLMEDIAKIT_LIBS}
  ${MPP_LIBS}
)

add_executable(mpi_enc_test
  src/main_mpi.cc
  utils/mpi_encoder.c
  utils/camera_source.c
)
target_link_libraries(mpi_enc_test
  ${RKNN_RT_LIB} 
  ${RGA_LIB} 
  ${OpenCV_LIBS} 
  ${ZLMEDIAKIT_LIBS}
  ${MPP_LIBS}
)

# 定义一个函数用于设置可执行文件和链接库
function(setup_executable target_name source_files)

    add_executable(${target_name} ${source_files} 
      utils/postprocess.cc 
      utils/preprocess.cc
      utils/mpp_decoder.cpp 
      utils/mpp_encoder.cpp 
      utils/drawing.cpp
    )

    target_link_libraries(${target_name} 
      ${RKNN_RT_LIB} 
      ${RGA_LIB} 
      ${OpenCV_LIBS} 
      ${ZLMEDIAKIT_LIBS}
      ${MPP_LIBS}
    )
endfunction()

# 调用函数设置两个可执行文件
setup_executable(rknn_yolov5_demo src/main.cc)
setup_executable(rknn_H264_rtsp_demo src/main_h264_rtsp.cc)
setup_executable(rknn_yolov5_video_demo src/main_video.cc)
#setup_executable(camera_rtsp_demo src/main_rtsp.cc)



# install target and libraries
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install/rknn_yolov5_demo_${CMAKE_SYSTEM_NAME})
install(TARGETS mpi_enc_test DESTINATION ./)
install(TARGETS rknn_yolov5_demo DESTINATION ./)
install(TARGETS rknn_H264_rtsp_demo DESTINATION ./)
install(TARGETS rknn_yolov5_video_demo DESTINATION ./)
install(TARGETS camera_rtsp_demo DESTINATION ./)
install(PROGRAMS ${RKNN_RT_LIB} DESTINATION lib)
install(PROGRAMS ${RGA_LIB} DESTINATION lib)
install(PROGRAMS ${MPP_LIBS} DESTINATION lib)
install(PROGRAMS ${ZLMEDIAKIT_LIBS} DESTINATION lib)
install(DIRECTORY model/${TARGET_SOC} DESTINATION ./model)
file(GLOB IMAGE_FILES "model/*.jpg")
file(GLOB LABEL_FILE "model/*.txt")
install(FILES ${IMAGE_FILES} DESTINATION ./model/)
install(FILES ${LABEL_FILE} DESTINATION ./model/)
