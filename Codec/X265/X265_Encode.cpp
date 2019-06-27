/***********************************************************************
File：X265_Encode.cpp
Description：OpenCV打开摄像头，X265实时编码
***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

extern "C"
{
#include "x265.h"
#include "x265_config.h"
};

using namespace cv;
using namespace std;

int main()
{
	//打开摄像头
	VideoCapture capture(0);

	Mat frame;
	int width = capture.get(CV_CAP_PROP_FRAME_WIDTH);
	int height = capture.get(CV_CAP_PROP_FRAME_HEIGHT);

	char *buff = NULL;
	int ret;
	FILE *fp_dst = fopen("result.h265", "wb");
	int yuvLen = width * height * 3 / 2;
	uint32_t iNal = 0;

	//相关结构体的定义
	x265_nal *pNals = NULL;
	x265_param* pParam = NULL;
	x265_encoder* pHandle = NULL;
	x265_picture* pPic_in = NULL;

	//参数设置
	pParam = x265_param_alloc();
	x265_param_default(pParam);
	pParam->bRepeatHeaders = 1;
	pParam->internalCsp = X265_CSP_I420;
	pParam->sourceWidth = width;
	pParam->sourceHeight = height;
	pParam->fpsNum = 25;
	pParam->fpsDenom = 1;
	
	pHandle = x265_encoder_open(pParam);
	if(pHandle==NULL)
	{
		printf("x265_encoder_open error!\n");
		return 0;
	}

	int y_size = pParam->sourceWidth * pParam->sourceHeight;
	buff = (char *)malloc(y_size * 3 / 2);

	pPic_in = x265_picture_alloc();
	x265_picture_init(pParam, pPic_in);
	pPic_in->planes[0] = buff;
	pPic_in->planes[1] = buff + y_size;
	pPic_in->planes[2] = buff + y_size * 5 / 4;
	pPic_in->stride[0] = width;
	pPic_in->stride[1] = width / 2;
	pPic_in->stride[2] = width / 2;
	
	while(1)
	{
		//摄像头采集到的画面
		capture >> frame;
		
		//格式转换
		Mat yuvImg;
		cvtColor(frame, yuvImg, CV_BGR2YUV_I420);
		memcpy(buff, yuvImg.data, yuvLen * sizeof(unsigned char));
		
		//开始编码
		ret = x265_encoder_encode(pHandle, &pNals, &iNal, pPic_in, NULL);	
		for(int j = 0; j < iNal; j++)
		{
			fwrite(pNals[j].payload, 1, pNals[j].sizeBytes, fp_dst);
		}	
	}
	
	//释放申请的资源
	x265_encoder_close(pHandle);
	x265_picture_free(pPic_in);
	x265_param_free(pParam);
	free(buff);
	fclose(fp_dst);
	
	return 0;
}
