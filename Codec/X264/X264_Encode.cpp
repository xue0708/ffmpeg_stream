/***********************************************************************
File：X264_Encode.cpp
Description：OpenCV打开摄像头，X264实时编码
***********************************************************************/
#include <stdio.h>
#include <iostream>
#include "stdint.h"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

extern "C"
{
#include "x264.h"
#include "x264_config.h"
};

using namespace cv;
using namespace std;

int main(int argc, char* argv[])
{
	//打开摄像头
	VideoCapture capture(0);
	int w = capture.get(CV_CAP_PROP_FRAME_WIDTH);
	int h = capture.get(CV_CAP_PROP_FRAME_HEIGHT);
	
	Mat frame;
	int yuv_bufLen = w * h * 3 / 2;
	size_t yuv_size = w * h * 3 / 2;
	uint8_t *yuv_buffer = (uint8_t*)malloc(yuv_size);
	int64_t i_pts = 0;
	x264_nal_t *nals;
	int nnal;
	FILE *fp_out = fopen("test.h264", "wb");
	
	//编码器相关结构体定义
	x264_t *encoder;
	x264_param_t param;
	x264_picture_t pic_in;
	x264_picture_t pic_out;
	
	//参数设置
	x264_param_default_preset(&param, "veryfast", "zerolatency");
	param.i_width = w;            
	param.i_height = h;           
	x264_param_apply_profile(&param, "baseline");
	
	//编码器的初始化
	encoder = x264_encoder_open(&param);     
	
	//输入待编码的数据（YUV420）
	x264_picture_alloc(&pic_in, X264_CSP_I420, w, h);      
	pic_in.img.plane[0] = yuv_buffer;       
	pic_in.img.plane[1] = pic_in.img.plane[0] + w * h;
	pic_in.img.plane[2] = pic_in.img.plane[1] + w * h / 4;

	while (1)
	{
		capture >> frame;

		//格式转换（RGB to YUV420）
		Mat yuvImg;
		cvtColor(frame, yuvImg, CV_BGR2YUV_I420);   
		memcpy(yuv_buffer, yuvImg.data, yuv_bufLen * sizeof(unsigned char));    

		//开始编码
		pic_in.i_pts = i_pts++;
		x264_encoder_encode(encoder, &nals, &nnal, &pic_in, &pic_out);  
		x264_nal_t *nal;
		for (nal = nals; nal < nals + nnal; nal++)
		{
			//数据保存 nal->p_payload：编码后的数据；nal->i_payload：编码后数据的大小
			fwrite(nal->p_payload, 1, nal->i_payload, fp_out);     
		}
	}

	//释放资源
	x264_encoder_close(encoder);  
	free(yuv_buffer);
	
	return 0;
}