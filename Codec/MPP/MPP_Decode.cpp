/**************************************************************************************
File：Mpp_Decode.cpp
Description：利用瑞芯微提供的硬件媒体处理平台MPP，实现硬件解码；
             利用FFmpeg打开RTSP数据流，并将H264数据交给MPP解码的接口，解码得到的图片利用OpenCV转为BGR格式；
**************************************************************************************/
#if defined(_WIN32)
#include "vld.h"
#endif

#define MODULE_TAG "mpi_dec_test"

#include <string.h>
#include "rk_mpi.h"

#include "mpp_log.h"
#include "mpp_mem.h"
#include "mpp_env.h"
#include "mpp_time.h"
#include "mpp_frame.h"
#include "mpp_common.h"
#include "mpp_frame_impl.h"
#include "mpp_buffer_impl.h"
#include "utils.h"
#include <sys/time.h>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
};

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>

using namespace std;
using namespace cv;

#define calTimeCost(begin,end)(end.tv_sec*1000.-begin.tv_sec*1000.+end.tv_usec/1000.-begin.tv_usec/1000.)
#define MPI_DEC_LOOP_COUNT          4
#define MPI_DEC_STREAM_SIZE         (SZ_4K)
#define MAX_FILE_NAME_LENGTH        256

typedef struct 
{
	MppCtx          ctx;
	MppApi          *mpi;
	RK_U32          eos;
	char            *buf;
	MppBufferGroup  frm_grp;
	MppBufferGroup  pkt_grp;
	MppPacket       packet;
	size_t          packet_size;
	MppFrame        frame;
	FILE            *fp_input;
	FILE            *fp_output;
	RK_S32          frame_count;
	RK_S32          frame_num;
	size_t          max_usage;
} MpiDecLoopData;

typedef struct 
{
	char            file_input[MAX_FILE_NAME_LENGTH];
	char            file_output[MAX_FILE_NAME_LENGTH];
	MppCodingType   type;
	MppFrameFormat  format;
	RK_U32          width;
	RK_U32          height;
	RK_U32          debug;
	RK_U32          have_input;
	RK_U32          have_output;
	RK_U32          simple;
	RK_S32          timeout;
	RK_S32          frame_num;
	size_t          max_usage;
} MpiDecTestCmd;

/**************************************************************************************
Function：mpp_frame_get_buf_size
Description：得到一帧数据buf的大小；
**************************************************************************************/
size_t mpp_frame_get_buf_size(const MppFrame s)
{
	check_is_mpp_frame((MppFrameImpl*)s);
	return ((MppFrameImpl*)s)->buf_size; 
}

/**************************************************************************************
Function：decode_simple
Description：MPP的解码接口，送入一帧H264数据，解码数据通过OpenCV转换为BGR格式显示出来；
**************************************************************************************/
static int decode_simple(MpiDecLoopData *data, unsigned char *H264_buf, int buf_length)
{
	Mat yuvImg;
	Mat result;
	RK_U32 pkt_done = 0;
	RK_U32 pkt_eos  = 0;
	RK_U32 err_info = 0;
	MPP_RET ret = MPP_OK;
	MppCtx ctx  = data->ctx;
	MppApi *mpi = data->mpi;
	char   *buf = data->buf;
	MppPacket packet = data->packet;
	MppFrame  frame  = NULL;
	size_t read_size;

	memcpy(buf, H264_buf, buf_length);
	read_size = buf_length;
	mpp_packet_write(packet, 0, buf, read_size);
	mpp_packet_set_pos(packet, buf);
	mpp_packet_set_length(packet, read_size);
	if (pkt_eos)
	{
		mpp_packet_set_eos(packet);
	}
	
    do {
		if (!pkt_done) 
		{
			ret = mpi->decode_put_packet(ctx, packet);
			if (MPP_OK == ret)
			{
				pkt_done = 1;
			}
		}
		do {
			RK_S32 get_frm = 0;
			RK_U32 frm_eos = 0;
			
		try_again:
			ret = mpi->decode_get_frame(ctx, &frame);
			if (MPP_ERR_TIMEOUT == ret) 
			{
				mpp_err("decode_get_frame failed too much time\n");
			}
			if (MPP_OK != ret) 
			{
				mpp_err("decode_get_frame failed ret %d\n", ret);
				break;
			}
			if (frame) 
			{
				if (mpp_frame_get_info_change(frame)) 
				{
					RK_U32 width = mpp_frame_get_width(frame);
					RK_U32 height = mpp_frame_get_height(frame);
					RK_U32 hor_stride = mpp_frame_get_hor_stride(frame);
					RK_U32 ver_stride = mpp_frame_get_ver_stride(frame);
					RK_U32 buf_size = mpp_frame_get_buf_size(frame);
					mpp_log("decode_get_frame get info changed found\n");
					mpp_log("decoder require buffer w:h [%d:%d] stride [%d:%d] buf_size %d", width, height, hor_stride, ver_stride, buf_size);
					
					if (NULL == data->frm_grp) 
					{
						ret = mpp_buffer_group_get_internal(&data->frm_grp, MPP_BUFFER_TYPE_ION);
						ret = mpi->control(ctx, MPP_DEC_SET_EXT_BUF_GROUP, data->frm_grp);
					}
					else
					{
						ret = mpp_buffer_group_clear(data->frm_grp);
					}
					ret = mpp_buffer_group_limit_config(data->frm_grp, buf_size, 24);
					ret = mpi->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
				}
				else
				{
					err_info = mpp_frame_get_errinfo(frame) | mpp_frame_get_discard(frame);
					if (err_info) 
					{
						mpp_log("decoder_get_frame get err info:%d discard:%d.\n", mpp_frame_get_errinfo(frame), mpp_frame_get_discard(frame));
					}
					data->frame_count++;
					mpp_log("decode_get_frame get frame %d\n", data->frame_count);
					
					if (!err_info)
					{
						if(!frame)
						{
							mpp_log("frame is null ------------\n");
						}
						
						RK_U32 width    = mpp_frame_get_width(frame);
						RK_U32 height   = mpp_frame_get_height(frame);
						RK_U32 h_stride = mpp_frame_get_hor_stride(frame);
						RK_U32 v_stride = mpp_frame_get_buffer(frame);
	
						MppBuffer buffer    = mpp_frame_get_buffer(frame);
						RK_U8 *base = (RK_U8 *)mpp_buffer_get_ptr(buffer);
	
						RK_U32 buf_size = mpp_frame_get_buf_size(frame);
						size_t base_length = mpp_buffer_get_size(buffer);
						
						yuvImg.create(height * 3 / 2, width, CV_8UC1);
						memcpy(yuvImg.data, base, width * height * 3 / 2);
						cvtColor(yuvImg, result, CV_YUV2BGR_NV12);
						imshow("result", result);
						cvWaitKey(1);
					}
				}
				frm_eos = mpp_frame_get_eos(frame);
				mpp_frame_deinit(&frame);
				frame = NULL;
				get_frm = 1;
			}
			if (data->frm_grp) 
			{
				size_t usage = mpp_buffer_group_usage(data->frm_grp);
				if (usage > data->max_usage)
				{
					data->max_usage = usage;
				}
			}
			if (pkt_eos && pkt_done && !frm_eos) 
			{
				msleep(10);
				continue;
			}
			if (frm_eos) 
			{
				mpp_log("found last frame\n");
				break;
			}
			if (data->frame_num && data->frame_count >= data->frame_num) 
			{
				data->eos = 1;
				break;
			}
			if (get_frm)
			{
				continue;
			}
			break;
		}while (1);
		
		if (data->frame_num && data->frame_count >= data->frame_num) 
		{
			data->eos = 1;
			mpp_log("reach max frame number %d\n", data->frame_count);
			break;
		}
		if (pkt_done)
		{
			break;
		}
	} while (1);
	
	return ret;
}

/**************************************************************************************
Function：mpi_dec_test_decode
Description：打开RTSP地址，获取H264裸流，以4096的大小传给decode_simple函数；
**************************************************************************************/
int mpi_dec_test_decode(MpiDecTestCmd *cmd, string path)
{
	MPP_RET ret         = MPP_OK;
	size_t file_size    = 0;
	MppCtx ctx          = NULL;
	MppApi *mpi         = NULL;
	MppPacket packet    = NULL;
	MppFrame  frame     = NULL;
	MpiCmd mpi_cmd      = MPP_CMD_BASE;
	MppParam param      = NULL;
	RK_U32 need_split   = 1;
	MppPollType timeout = cmd->timeout;
	RK_U32 width        = cmd->width;
	RK_U32 height       = cmd->height;
	MppCodingType type  = cmd->type;
	
	char *buf           = NULL;
	size_t packet_size  = MPI_DEC_STREAM_SIZE;
	MppBuffer pkt_buf   = NULL;
	MppBuffer frm_buf   = NULL;
	
	MpiDecLoopData data;
	mpp_log("mpi_dec_test start\n");
	memset(&data, 0, sizeof(data));
	if (cmd->simple) 
	{
		buf = mpp_malloc(char, packet_size);
		if (NULL == buf) 
		{
			mpp_err("mpi_dec_test malloc input stream buffer failed\n");
		}
			ret = mpp_packet_init(&packet, buf, packet_size);
	}
	ret = mpp_create(&ctx, &mpi);
	mpi_cmd = MPP_DEC_SET_PARSER_SPLIT_MODE;
	param = &need_split;
	ret = mpi->control(ctx, mpi_cmd, param);
	if (timeout) 
	{
		param = &timeout;
		ret = mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, param);
	}
		
	ret = mpp_init(ctx, MPP_CTX_DEC, type);
	data.ctx            = ctx;
	data.mpi            = mpi;
	data.eos            = 0;
	data.buf            = buf;
	data.packet         = packet;
	data.packet_size    = packet_size;
	data.frame          = frame;
	data.frame_count    = 0;
	data.frame_num      = cmd->frame_num;

	AVFormatContext *pFormatCtx;
	AVCodecContext  *pCodecCtx;
	AVCodec         *pCodec;
	AVPacket        *ff_packet;
	int i, videoindex;
	struct SwsContext *img_convert_ctx;
	char *filepath = path.data();

	av_register_all();
	avformat_network_init();
	pFormatCtx = avformat_alloc_context();
	avformat_open_input(&pFormatCtx, filepath, NULL, NULL);
	avformat_find_stream_info(pFormatCtx, NULL);

	videoindex = -1;
	for (i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoindex = i;
			break;
		}
	}
		
	pCodecCtx = pFormatCtx->streams[videoindex]->codec;
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);

	avcodec_open2(pCodecCtx, pCodec, NULL);
	av_dump_format(pFormatCtx, 0, filepath, 0);
	ff_packet = (AVPacket *)av_malloc(sizeof(AVPacket));

	int present_H264_size = 0;
	int last_reserve_size = 0;
	unsigned char *last_reserve_buf = new unsigned char[MPI_DEC_STREAM_SIZE]; 
	memset(last_reserve_buf, '\0', MPI_DEC_STREAM_SIZE);
	unsigned char *present_H264_buf = NULL;
	int used_H264_size = 0;
	bool last_reserve_flag = false;
	unsigned char dataBuffer[MPI_DEC_STREAM_SIZE];

	int ffmpeg_count = 0;
	int decode_len = 0;
	int H264_buf_offset_len = 0;
	if (cmd->simple) 
	{
		while (1) 
		{
			if (av_read_frame(pFormatCtx, ff_packet) >= 0)
			{				
				present_H264_size = ff_packet->size;
				present_H264_buf = ff_packet->data;
				if(last_reserve_size > 0) 
				{
					if(last_reserve_size + present_H264_size > MPI_DEC_STREAM_SIZE)
					{
						memcpy(dataBuffer, last_reserve_buf, last_reserve_size);
						used_H264_size = MPI_DEC_STREAM_SIZE - last_reserve_size;
						memcpy(dataBuffer + last_reserve_size, present_H264_buf, used_H264_size);
						present_H264_size -= used_H264_size;
						decode_simple(&data, dataBuffer, 4096);
					}
					else
					{
						memcpy(last_reserve_buf + last_reserve_size, present_H264_buf, present_H264_size);
						last_reserve_size += present_H264_size;
						last_reserve_flag = true; 
					}
				}
				while(present_H264_size > MPI_DEC_STREAM_SIZE)
				{
					memcpy(dataBuffer, present_H264_buf + used_H264_size, MPI_DEC_STREAM_SIZE);
					used_H264_size += MPI_DEC_STREAM_SIZE;
					present_H264_size -= MPI_DEC_STREAM_SIZE;
					decode_simple(&data, dataBuffer, 4096);
				}
				if(!last_reserve_flag)
				{
					last_reserve_size = present_H264_size;
					memcpy(last_reserve_buf, present_H264_buf + used_H264_size, last_reserve_size);
					used_H264_size = 0;
				}
				last_reserve_flag = false; 
				//mpp_log("ff_packet->size = %d, ffmpeg_count = %d  , buf_length = %d\n", ff_packet->size, ffmpeg_count++, 4096);
				av_free_packet(ff_packet);	
			}
			else
			{
				mpp_log("av_read_frame < 0------------\n");
			}
		}
	}
	
	cmd->max_usage = data.max_usage;
	ret = mpi->reset(ctx);
	return ret;
}

/**************************************************************************************
Function：decode_start
Description：在调用MPP解码函数前配置cmd的相关参数，说明解码数据为H264，并且获取要打开的RTSP流地址
**************************************************************************************/
int decode_start(string path)
{
	RK_S32 ret = 0;
	MpiDecTestCmd  cmd_ctx;
	MpiDecTestCmd* cmd = &cmd_ctx;
	memset((void*)cmd, 0, sizeof(*cmd));

	cmd->type = 7;
	cmd->simple = (cmd->type != MPP_VIDEO_CodingMJPEG) ? (1) : (0);
	ret = mpi_dec_test_decode(cmd, path);
	
	return ret;
}

/**************************************************************************************
Function：main
Description：输入要打开的RTSP流地址，调用MPP的解码函数
**************************************************************************************/
int main()
{
	string RTSP_Path = "rtsp://admin:hk888888@10.11.0.5/h264/ch1/main/av_stream";
	decode_start(RTSP_Path);
	
	return 0;
}

