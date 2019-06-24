/**************************************************************************************
File：Mpp_Encode.cpp
Description：利用瑞芯微提供的硬件媒体处理平台MPP，实现硬件编码；
             调用参考main函数，视频数据转为YUV格式后交给MyYuvtoH264，编码后的数据存储在encode_buf中；
**************************************************************************************/
#if defined(_WIN32)
#include "vld.h"
#endif

#define MODULE_TAG "mpi_enc_test"

#include <string.h>
#include <sys/time.h>

#include "utils.h"
#include "rk_mpi.h"
#include "mpp_env.h"
#include "mpp_mem.h"
#include "mpp_log.h"
#include "mpp_time.h"
#include "mpp_common.h"

#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

using namespace std;
using namespace cv;

#define MAX_FILE_NAME_LENGTH 256
#define calTimeCost(begin,end)(end.tv_sec*1000.-begin.tv_sec*1000.+end.tv_usec/1000.-begin.tv_usec/1000.)

typedef struct 
{
	MppCodingType   type;
	RK_U32          width;
	RK_U32          height;
	MppFrameFormat  format;
	RK_U32          num_frames;
}MpiEncTestCmd;

typedef struct 
{
	RK_U32 frm_eos;
	RK_U32 pkt_eos;
	RK_U32 frame_count;
	RK_U64 stream_size;
	FILE *fp_input;
	FILE *fp_output;
	MppBuffer frm_buf;
	MppEncSeiMode sei_mode;
	MppCtx ctx;
	MppApi *mpi;
	MppEncPrepCfg prep_cfg;
	MppEncRcCfg rc_cfg;
	MppEncCodecCfg codec_cfg;
	RK_U32 width;
	RK_U32 height;
	RK_U32 hor_stride;
	RK_U32 ver_stride;
	MppFrameFormat fmt;
	MppCodingType type;
	RK_U32 num_frames;
	size_t frame_size;
	size_t packet_size;
	RK_S32 gop;
	RK_S32 fps;
	RK_S32 bps;
}MpiEncTestData;

MpiEncTestData data;
MpiEncTestCmd cmd_ctx;
MpiEncTestData *ptr1;
MPP_RET ret = MPP_OK;
FILE *fp_out = fopen("result.h264", "w+b");

/**************************************************************************************
Function：test_ctx_init
Description：得到输入数据的参数，保存在结构体MpiEncTestData中；
**************************************************************************************/
MPP_RET test_ctx_init(MpiEncTestData **data, MpiEncTestCmd *cmd)
{
	MpiEncTestData *p = NULL;
	MPP_RET ret = MPP_OK;
	
	if (!data || !cmd) 
	{
		mpp_err_f("invalid input data %p cmd %p\n", data, cmd);
		return MPP_ERR_NULL_PTR;
	}
	p = mpp_calloc(MpiEncTestData, 1);
	if (!p) 
	{
		mpp_err_f("create MpiEncTestData failed\n");
		ret = MPP_ERR_MALLOC;
		goto RET;
	}
	
	//get paramter from cmd
	p->width        = cmd->width;
	p->height       = cmd->height;
	p->hor_stride   = MPP_ALIGN(cmd->width, 16);
	p->ver_stride   = MPP_ALIGN(cmd->height, 16);
	p->fmt          = cmd->format;
	p->type         = cmd->type;
	p->num_frames   = cmd->num_frames;
	p->frame_size = p->hor_stride * p->ver_stride * 3 / 2;
	p->packet_size  = p->width * p->height;

RET:
	*data = p;
	return ret;
}

/**************************************************************************************
Function：test_mpp_setup
Description：设置编码器的参数；
**************************************************************************************/
MPP_RET test_mpp_setup(MpiEncTestData *p2)
{
	MPP_RET ret;
	MppApi *mpi;
	MppCtx ctx;
	MppEncCodecCfg *codec_cfg;
	MppEncPrepCfg *prep_cfg;
	MppEncRcCfg *rc_cfg;
	if (NULL == p2)
	{
		return MPP_ERR_NULL_PTR;
	}
	
	mpi = p2->mpi;
	ctx = p2->ctx;
	codec_cfg = &p2->codec_cfg;
	prep_cfg = &p2->prep_cfg;
	rc_cfg = &p2->rc_cfg;
	p2->fps = 25;
	p2->gop = 50;
	p2->bps = 4096 * 1024;
	
	prep_cfg->change        = MPP_ENC_PREP_CFG_CHANGE_INPUT | MPP_ENC_PREP_CFG_CHANGE_ROTATION | MPP_ENC_PREP_CFG_CHANGE_FORMAT;
	prep_cfg->width         = p2->width;
	prep_cfg->height        = p2->height;
	prep_cfg->hor_stride    = p2->hor_stride;
	prep_cfg->ver_stride    = p2->ver_stride;
	prep_cfg->format        = p2->fmt;
	prep_cfg->rotation      = MPP_ENC_ROT_0;
	ret = mpi->control(ctx, MPP_ENC_SET_PREP_CFG, prep_cfg);
	if (ret) 
	{
		mpp_err("mpi control enc set prep cfg failed ret %d\n", ret);
		goto RET;
	}
	
	rc_cfg->change  = MPP_ENC_RC_CFG_CHANGE_ALL;
	rc_cfg->rc_mode = MPP_ENC_RC_MODE_VBR;
	rc_cfg->quality = MPP_ENC_RC_QUALITY_CQP;
	if (rc_cfg->rc_mode == MPP_ENC_RC_MODE_CBR) 
	{
		rc_cfg->bps_target   = p2->bps;
		rc_cfg->bps_max      = p2->bps * 17 / 16;
		rc_cfg->bps_min      = p2->bps * 15 / 16;
	}
	else if (rc_cfg->rc_mode ==  MPP_ENC_RC_MODE_VBR) 
	{
		if (rc_cfg->quality == MPP_ENC_RC_QUALITY_CQP) 
		{
			rc_cfg->bps_target   = p2->bps;
			rc_cfg->bps_max      = p2->bps * 17 / 16;
			rc_cfg->bps_min      = p2->bps * 1 / 16;
		}
		else 
		{
			rc_cfg->bps_target   = p2->bps;
			rc_cfg->bps_max      = p2->bps * 17 / 16;
			rc_cfg->bps_min      = p2->bps * 1 / 16;
		}
	}
	
	rc_cfg->fps_in_flex      = 0;
	rc_cfg->fps_in_num       = p2->fps;
	rc_cfg->fps_in_denorm    = 1;
	rc_cfg->fps_out_flex     = 0;
	rc_cfg->fps_out_num      = p2->fps;
	rc_cfg->fps_out_denorm   = 1;
	rc_cfg->gop              = p2->gop;
	rc_cfg->skip_cnt         = 0;
	
	mpp_log("mpi_enc_test bps %d fps %d gop %d\n", rc_cfg->bps_target, rc_cfg->fps_out_num, rc_cfg->gop);
	ret = mpi->control(ctx, MPP_ENC_SET_RC_CFG, rc_cfg);
	if (ret)
	{
		mpp_err("mpi control enc set rc cfg failed ret %d\n", ret);
		goto RET;
		}
	
	codec_cfg->coding = p2->type;
	codec_cfg->h264.change = MPP_ENC_H264_CFG_CHANGE_PROFILE | MPP_ENC_H264_CFG_CHANGE_ENTROPY | MPP_ENC_H264_CFG_CHANGE_TRANS_8x8;
	//66  - Baseline profile
	//77  - Main profile
	//100 - High profile
	codec_cfg->h264.profile = 77; 
	codec_cfg->h264.level    = 30;
	codec_cfg->h264.entropy_coding_mode  = 1;
	codec_cfg->h264.cabac_init_idc  = 0;
	//codec_cfg->h264.qp_min = 0;
	//codec_cfg->h264.qp_max = 50;
	//codec_cfg->h264.transform8x8_mode = 0;
	ret = mpi->control(ctx, MPP_ENC_SET_CODEC_CFG, codec_cfg);
	if (ret) 
	{
		mpp_err("mpi control enc set codec cfg failed ret %d\n", ret);
		goto RET;
	}
	p2->sei_mode = MPP_ENC_SEI_MODE_ONE_FRAME;
	ret = mpi->control(ctx, MPP_ENC_SET_SEI_CFG, &p2->sei_mode);
	if (ret) 
	{
		mpp_err("mpi control enc set sei cfg failed ret %d\n", ret);
		goto RET;
	}

RET:
	return ret;
}

/**************************************************************************************
Function：read_yuv_buffer
Description：输入的YUV数据送到MPP可以使用的buffer中；
**************************************************************************************/
MPP_RET read_yuv_buffer(RK_U8 *buf, Mat &yuvImg, RK_U32 width, RK_U32 height)
{
	MPP_RET ret = MPP_OK;
	RK_U32 read_size;
	RK_U32 row = 0;
	RK_U8 *buf_y = buf;
	RK_U8 *buf_u = buf_y + width * height; 
	RK_U8 *buf_v = buf_u + width * height / 4; 
	int yuv_size = width * height *3/2;
	memcpy(buf, yuvImg.data, yuv_size);

err:
	return ret;
}

/**************************************************************************************
Function：test_mpp_run_yuv
Description：编码函数，编码后的数据保存在H264_buf中；
**************************************************************************************/
MPP_RET test_mpp_run_yuv(Mat yuvImg, MppApi *mpi1, MppCtx &ctx1, unsigned char * &H264_buf, int &length)
{
	MpiEncTestData *p3 = ptr1;
	MPP_RET ret;
	MppFrame frame = NULL;
	MppPacket packet = NULL;
	void *buf = mpp_buffer_get_ptr(p3->frm_buf);
	ret = read_yuv_buffer(buf, yuvImg, p3->width, p3->height);
	ret = mpp_frame_init(&frame);
	if (ret) 
	{
		mpp_err_f("mpp_frame_init failed\n");
		goto RET;
	}
	
	mpp_frame_set_width(frame, p3->width);
	mpp_frame_set_height(frame, p3->height);
	mpp_frame_set_hor_stride(frame, p3->hor_stride);
	mpp_frame_set_ver_stride(frame, p3->ver_stride);
	mpp_frame_set_fmt(frame, p3->fmt);
	mpp_frame_set_buffer(frame, p3->frm_buf);
	mpp_frame_set_eos(frame, p3->frm_eos);
	
	ret = mpi1->encode_put_frame(ctx1, frame);
	if (ret) 
	{
		mpp_err("mpp encode put frame failed\n");
		goto RET;
	}
	
	ret = mpi1->encode_get_packet(ctx1, &packet);
	if (ret) 
	{
		mpp_err("mpp encode get packet failed\n");
		goto RET;
	}
	if (packet) 
	{
		void *ptr   = mpp_packet_get_pos(packet);
		size_t len  = mpp_packet_get_length(packet);
		p3->pkt_eos = mpp_packet_get_eos(packet);
		H264_buf = new unsigned char[len];
		memcpy(H264_buf, ptr, len);
		length = len;
		mpp_packet_deinit(&packet);
		p3->stream_size += len;
		p3->frame_count++;
		
		if (p3->pkt_eos) 
		{
			mpp_log("found last packet\n");
			mpp_assert(p3->frm_eos);
		}
	}
RET:
	return ret;
}

/**************************************************************************************
Function：test_mpp_run_yuv_init
Description：调用MPP的编码接口，第一帧编码时要取出SPS、PPS等信息，保存在SPS_buf中；
**************************************************************************************/
MppApi *mpi;
MppCtx ctx;
MpiEncTestData *test_mpp_run_yuv_init(MpiEncTestData *p, MpiEncTestCmd *cmd, int width , int height, unsigned char * &SPS_buf, int &SPS_length)
{
	cmd->width = width;
	cmd->height = height;
	cmd->type = MPP_VIDEO_CodingAVC;
	cmd->format = MPP_FMT_YUV420P;
	cmd->num_frames = 0;
	
	ret = test_ctx_init(&p, cmd);
	if (ret) 
	{
		mpp_err_f("test data init failed ret %d\n", ret);
		goto MPP_TEST_OUT;
	}
	mpp_log("p->frame_size = %d----------------\n", p->frame_size);
	ret = mpp_buffer_get(NULL, &p->frm_buf, p->frame_size);
	if (ret) 
	{
		mpp_err_f("failed to get buffer for input frame ret %d\n", ret);
		goto MPP_TEST_OUT;
	}
	mpp_log("mpi_enc_test encoder test start w %d h %d type %d\n",p->width, p->height, p->type);
	ret = mpp_create(&p->ctx, &p->mpi);
	if (ret) 
	{
		mpp_err("mpp_create failed ret %d\n", ret);
		goto MPP_TEST_OUT;
	}
	ret = mpp_init(p->ctx, MPP_CTX_ENC, p->type);
	if (ret) 
	{
		mpp_err("mpp_init failed ret %d\n", ret);
		goto MPP_TEST_OUT;
	}
	ret = test_mpp_setup(p);
	if (ret) 
	{
		mpp_err_f("test mpp setup failed ret %d\n", ret);
		goto MPP_TEST_OUT;	
	}
	mpi = p->mpi;
	ctx = p->ctx;
	if (p->type == MPP_VIDEO_CodingAVC) 
	{
		MppPacket packet = NULL;
		
		ret = mpi->control(ctx, MPP_ENC_GET_EXTRA_INFO, &packet);
		if (ret) 
		{
			mpp_err("mpi control enc get extra info failed\n");
		}		
		if (packet) 
		{
			void *ptr	= mpp_packet_get_pos(packet);
			size_t len	= mpp_packet_get_length(packet);
			SPS_buf = new unsigned char[len];
			memcpy(SPS_buf, ptr, len);
			SPS_length = len;
			packet = NULL;
		}
	}

	return p;
MPP_TEST_OUT:
	return p;
}

MpiEncTestData *p = &data;
MpiEncTestCmd *cmd = &cmd_ctx;

unsigned char *H264_buf = NULL;
int length = 0;
unsigned char *SPS_buf = NULL;
int SPS_length = 0;
int frame_num = 1;

/**************************************************************************************
Function：MyYuvtoH264
Description：调用MPP的编码接口，第一帧编码时要取出SPS、PPS等信息；
**************************************************************************************/
int MyYuvtoH264(int width, int height, Mat yuv_frame, unsigned char* (&encode_buf), int &encode_length)
{
	if(frame_num == 1)
	{	
		ptr1 = test_mpp_run_yuv_init(p, cmd, width, height, SPS_buf, SPS_length);	
		test_mpp_run_yuv(yuv_frame, mpi, ctx, H264_buf, length);
	
		encode_buf = new unsigned char[SPS_length + length];
		memcpy(encode_buf, SPS_buf, SPS_length);
		if(H264_buf)
		{
			memcpy(encode_buf + SPS_length, H264_buf, length);
		}
		encode_length = length + SPS_length;		
		frame_num++;
		
		delete H264_buf;
		delete SPS_buf;
	}
	else
	{
		test_mpp_run_yuv(yuv_frame, mpi, ctx, H264_buf, length);
		encode_buf = new unsigned char[length];
		if(H264_buf)
		{
			memcpy(encode_buf, H264_buf, length);
			delete H264_buf;
		}
		encode_length = length;		
	}	
	return 0;
}

/**************************************************************************************
Function：main
Description：OpenCV打开USB-Camera，将采集到的画面利用MPP编码为H264数据，保存在本地；
             编码器的输入是YUV，因此需要调用cvtColor；
**************************************************************************************/
int main()
{
	VideoCapture cap(0);
	Mat frame;

	unsigned char *encode_buf = NULL;
	int encode_length = 0;

	while(1)
	{
		cap >> frame;
		
		Mat yuvImg;
		cvtColor(frame, yuvImg, CV_BGR2YUV_I420);
		
		MyYuvtoH264(frame.cols, frame.rows, yuvImg, encode_buf, encode_length);
		fwrite(encode_buf, 1, encode_length, fp_out);
		
		delete encode_buf;
	}
	return 0;	
}

