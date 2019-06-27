/**************************************************************************************
File：main.cpp
Description：利用HK_SDK同时采集多路RTSP网络流
**************************************************************************************/
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "HKCapture.h"

using namespace std;
using namespace cv;

Mat img[10];

/**************************************************************************************
Function：thread1
Description：线程一，采集第一个摄像机
**************************************************************************************/
void *thread1(void*)
{	
	HKCapture m_cam1;
	m_cam1.InitHKNetSDK();
	m_cam1.InitCamera("10.11.0.5","admin","hk888888",8000,0,"win1");
	while(1)
	{
		m_cam1.GetCamMat(img[0]);
	}
}

/**************************************************************************************
Function：thread2
Description：线程二，采集第二个摄像机
**************************************************************************************/
void *thread2(void*)
{
	HKCapture m_cam2;
	m_cam2.InitHKNetSDK();
	m_cam2.InitCamera("10.11.0.5","admin","hk888888",8000,0,"win2");
	while(1)
	{
		m_cam2.GetCamMat(img[1]);
	}
}

/**************************************************************************************
Function：thread3
Description：线程三，采集第三个摄像机
**************************************************************************************/
void *thread3(void*)
{
	HKCapture m_cam3;
	m_cam3.InitHKNetSDK();
	m_cam3.InitCamera("10.11.0.5","admin","hk888888",8000,0,"win3");
	while(1)
	{
		m_cam3.GetCamMat(img[2]);
	}
}

/**************************************************************************************
Function：thread4
Description：线程四，采集第四个摄像机
**************************************************************************************/
void *thread4(void*)
{
	HKCapture m_cam4;
	m_cam4.InitHKNetSDK();
	m_cam4.InitCamera("10.11.99.248","admin","jdh123456",8000,0,"win4");
	while(1)
	{
		m_cam4.GetCamMat(img[3]);
	}
}

/**************************************************************************************
Function：main
Description：主函数，开启四个线程采集
**************************************************************************************/
int main()
{
	namedWindow("Win-1",0);
	resizeWindow("Win-1",512,512);
	namedWindow("Win-2",0);
	resizeWindow("Win-2",512,512);
	namedWindow("Win-3",0);
	resizeWindow("Win-3",512,512);
	namedWindow("Win-4",0);
	resizeWindow("Win-4",512,512);

	pthread_t tidp1, tidp2, tidp3, tidp4;
	
	if((pthread_create(&tidp1,NULL,thread1,NULL))==-1)
	{
		printf("thread1 create error\n");
		return -1;
	}
	if((pthread_create(&tidp2,NULL,thread2,NULL))==-1)
	{
		printf("thread2 create error\n");
		return -2;
	}
	if((pthread_create(&tidp3,NULL,thread3,NULL))==-1)
	{
		printf("thread3 create error\n");
		return -1;
	}
	if((pthread_create(&tidp4,NULL,thread4,NULL))==-1)
	{
		printf("thread4 create error\n");
		return -1;
	}
	while(1)
	{
		if(!img[0].empty())
		{
			imshow("Win-1",img[0]);
			waitKey(1);
		}
		if(!img[1].empty())
		{
			imshow("Win-2",img[1]);
			waitKey(1);
		}
		if(!img[2].empty())
		{
			imshow("Win-3",img[2]);
			waitKey(1);
		}
		if(!img[3].empty())
		{
			imshow("Win-4",img[3]);
			waitKey(1);
		}
	}
	pthread_join(tidp1,NULL);
	pthread_join(tidp2,NULL);
	pthread_join(tidp3,NULL);
	pthread_join(tidp4,NULL);

	return 0;
}
