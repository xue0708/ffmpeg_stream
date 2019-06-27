/***********************************************************************
File：VPX_Encode.cpp
Description：OpenCV打开摄像头，VPX实时编码
***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "vpx/vpx_encoder.h"
#include "vpx/vp8cx.h"

#define interface (&vpx_codec_vp8_cx_algo)
#define fourcc 0x30385056
#define IVF_FILE_HDR_SZ (32)
#define IVF_FRAME_HDR_SZ (12)

using namespace std;
using namespace cv;

static void mem_put_le16(char *mem, unsigned int val)
{
	mem[0] = val;
	mem[1] = val >> 8;
}

static void mem_put_le32(char *mem, unsigned int val)
{
	mem[0] = val;
	mem[1] = val >> 8;
	mem[2] = val >> 16;
	mem[3] = val >> 24;
}

static void write_ivf_file_header(FILE *outfile, const vpx_codec_enc_cfg_t *cfg, int frame_cnt)
{
	char header[32];
	if (cfg->g_pass != VPX_RC_ONE_PASS && cfg->g_pass != VPX_RC_LAST_PASS)
	{
		return;
	}

	header[0] = 'D';
	header[1] = 'K';
	header[2] = 'I';
	header[3] = 'F';
	mem_put_le16(header + 4, 0);                  
	mem_put_le16(header + 6, 32);                  
	mem_put_le32(header + 8, fourcc);             
	mem_put_le16(header + 12, cfg->g_w);            
	mem_put_le16(header + 14, cfg->g_h);           
	mem_put_le32(header + 16, cfg->g_timebase.den); 
	mem_put_le32(header + 20, cfg->g_timebase.num); 
	mem_put_le32(header + 24, frame_cnt);           
	mem_put_le32(header + 28, 0);                  

	fwrite(header, 1, 32, outfile);
}

static void write_ivf_frame_header(FILE *outfile, const vpx_codec_cx_pkt_t *pkt)
{
	char header[12];
	vpx_codec_pts_t pts;

	if (pkt->kind != VPX_CODEC_CX_FRAME_PKT)
	{
		return;
	}
	pts = pkt->data.frame.pts;
	mem_put_le32(header, pkt->data.frame.sz);
	mem_put_le32(header + 4, pts & 0xFFFFFFFF);
	mem_put_le32(header + 8, pts >> 32);

	fwrite(header, 1, 12, outfile);
}

int main()
{
	//打开摄像头
	VideoCapture capture(0);
	Mat frame;
	int width = capture.get(CV_CAP_PROP_FRAME_WIDTH);
	int height = capture.get(CV_CAP_PROP_FRAME_HEIGHT);

	FILE *outfile = fopen("result.ivf", "wb");
	
	//相关结构体的定义
	vpx_codec_ctx_t codec;
	vpx_codec_enc_cfg_t cfg;
	vpx_image_t raw;
	vpx_codec_err_t ret;

	int frame_cnt = 0;
	unsigned char file_hdr[IVF_FILE_HDR_SZ];
	unsigned char frame_hdr[IVF_FRAME_HDR_SZ];
	int flags = 0;

	if (!vpx_img_alloc(&raw, VPX_IMG_FMT_I420, width, height, 1))
	{
		printf("Fail to allocate image\n");
		return -1;
	}
	printf("Using %s\n", vpx_codec_iface_name(interface));

	ret = vpx_codec_enc_config_default(interface, &cfg, 0);
	if (ret)
	{
		printf("Failed to get config: %s\n", vpx_codec_err_to_string(ret));
		return -1;
	}

	//参数设置
	cfg.rc_target_bitrate = 800;
	cfg.g_w = width;
	cfg.g_h = height;

	//写入ivf文件的头部信息
	write_ivf_file_header(outfile, &cfg, 0);
	if (vpx_codec_enc_init(&codec, interface, &cfg, 0))
	{
		printf("Failed to initialize encoder\n");
		return -1;
	}

	int frame_avail = 1;
	int got_data = 0;
	int y_size = cfg.g_w * cfg.g_h;
	int yuvLen = y_size * 3 / 2;
	while (frame_avail || got_data)
	{
		vpx_codec_iter_t iter = NULL;
		const vpx_codec_cx_pkt_t *pkt;

		//采集到的一帧画面，且格式转换
		capture >> frame;
		Mat yuvImg;
		cvtColor(frame, yuvImg, CV_BGR2YUV_I420);
		memcpy(raw.planes[0], yuvImg.data, yuvLen * sizeof(unsigned char));
		
		//开始编码
	    ret = vpx_codec_encode(&codec, &raw, frame_cnt, 1, flags, VPX_DL_REALTIME);
		if (ret)
		{
			printf("Failed to encode frame\n");
			return -1;
		}

		got_data = 0;
		while ((pkt = vpx_codec_get_cx_data(&codec, &iter)))
		{
			got_data = 1;
			//写入每一帧的头部
			write_ivf_frame_header(outfile, pkt);
			fwrite(pkt->data.frame.buf, 1, pkt->data.frame.sz, outfile);
		}
		printf("Succeed encode frame: %5d\n", frame_cnt);
		frame_cnt++;
	}

	//释放申请的资源
	vpx_codec_destroy(&codec);
	fclose(outfile);

	return 0;
}

