/**************************************************************************************
File：HKCapture.h
Description：海康SDK采集RTSP流，头文件定义了类HKCapture
**************************************************************************************/
#include <string.h>
#include <vector>
#include <iostream>
#include <fstream>
#include "HCNetSDK.h"
#include "PlayM4.h"
#include "LinuxPlayM4.h"
#include "opencv2/opencv.hpp"

using namespace cv;  
using namespace std;
                                                    
typedef long CamHandle;                                       

/**************************************************************************************
Function：类HKCapture
Description：InitHKNetSDK()：SDK的初始化；
             InitCamera()：相机采集时输入的参数；
			 ReleaseCamera：释放申请的资源；
			 GetCamMat()：获取解码后的一帧图像
**************************************************************************************/
class HKCapture
{
public:
    HKCapture();
    ~HKCapture();

    void InitHKNetSDK();
    CamHandle InitCamera(const char *sIP, const char *UsrName,const char *PsW,const int Port,const int streamType, string winname);
    int ReleaseCamera(void);
	int GetCamMat(Mat &Img);
	
	string m_UserName,m_sIP,m_Psw, m_strStreamType;
	DWORD  m_streamType;
	LONG lRealPlayHandle;
    LONG lUserID;
	string m_name;
	DWORD cam_id;
};

