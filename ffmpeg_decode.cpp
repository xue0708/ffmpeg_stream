/**************************************************************************************
File��FFmpeg_Decode.cpp
Description��FFmpegʵ�ּ򵥵Ľ���������ȡ����H264�ļ�������ΪYUV��ʽ�����ڱ���
**************************************************************************************/
#include <stdio.h>
#include <iostream>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
};

using namespace std;

int main()
{
	//FFmpeg�ṹ���ʼ��
	AVCodec *pCodec;
	AVCodecContext *pCodecCtx = NULL;
	AVCodecParserContext *pCodecParserCtx = NULL;
	AVFrame	*pFrame, *pFrameYUV;
	AVPacket packet;
	AVCodecID codec_id = AV_CODEC_ID_H264;
	struct SwsContext *img_convert_ctx;
	
	//һЩ�����Ķ���
	FILE *fp_in;
	FILE *fp_out;
	int frame_count;
	uint8_t *out_buffer;
	const int in_buffer_size = 4096;
	uint8_t in_buffer[in_buffer_size + FF_INPUT_BUFFER_PADDING_SIZE] = { 0 };
	uint8_t *cur_ptr;
	int cur_size;
	int ret, got_picture;
	int y_size;
	char filepath_in[] = "test.h264";
	char filepath_out[] = "test.yuv";
	int first_time = 1;

	avcodec_register_all();

	pCodec = avcodec_find_decoder(codec_id);
	if (!pCodec) 
	{
		printf("Codec not found\n");
		return -1;
	}
	pCodecCtx = avcodec_alloc_context3(pCodec);
	if (!pCodecCtx)
	{
		printf("Could not allocate video codec context\n");
		return -1;
	}
	pCodecParserCtx = av_parser_init(codec_id);
	if (!pCodecParserCtx)
	{
		printf("Could not allocate video parser context\n");
		return -1;
	}
	if (pCodec->capabilities&CODEC_CAP_TRUNCATED)
	{
		pCodecCtx->flags |= CODEC_FLAG_TRUNCATED; 
	}
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) 
	{
		printf("Could not open codec\n");
		return -1;
	}
	
	//�����������ļ�
	fp_in = fopen(filepath_in, "rb");
	if (!fp_in) 
	{
		printf("Could not open input stream\n");
		return -1;
	}
	fp_out = fopen(filepath_out, "wb");
	if (!fp_out) 
	{
		printf("Could not open output YUV file\n");
		return -1;
	}

	pFrame = av_frame_alloc();
	av_init_packet(&packet);

	while (1) 
	{	
		cur_size = fread(in_buffer, 1, in_buffer_size, fp_in);
		if (cur_size == 0)
		{
			break;
		}
		cur_ptr = in_buffer;

		while (cur_size>0)
		{
			int len = av_parser_parse2(pCodecParserCtx, pCodecCtx, &packet.data, &packet.size, cur_ptr, cur_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, AV_NOPTS_VALUE);
			cur_ptr += len;
			cur_size -= len;
			if (packet.size == 0)
			{
				continue;
			}

			//����
			ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, &packet);
			if (ret < 0) 
			{
				printf("Decode Error\n");
				return ret;
			}
			if (got_picture) 
			{
				if (first_time)
				{		
					img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
					pFrameYUV = av_frame_alloc();
					out_buffer = (uint8_t *)av_malloc(avpicture_get_size(PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));
					avpicture_fill((AVPicture *)pFrameYUV, out_buffer, PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
					y_size = pCodecCtx->width * pCodecCtx->height;
					first_time = 0;
				}
				sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);

				//���������ݱ����ڱ��أ�������ͨ��Y��U��V
				fwrite(pFrameYUV->data[0], 1, y_size, fp_out);   
				fwrite(pFrameYUV->data[1], 1, y_size / 4, fp_out);   
				fwrite(pFrameYUV->data[2], 1, y_size / 4, fp_out);   
			}
		}

	}

	//�������������ʣ�������û�б�����
	packet.data = NULL;
	packet.size = 0;
	while (1)
	{
		ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, &packet);
		if (ret < 0) {
			printf("Decode Error.(�������)\n");
			return ret;
		}
		if (!got_picture)
			break;
		if (got_picture) {
			printf("Flush Decoder: Succeed to decode 1 frame!\n");
			sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);

			fwrite(pFrameYUV->data[0], 1, y_size, fp_out);     
			fwrite(pFrameYUV->data[1], 1, y_size / 4, fp_out);  
			fwrite(pFrameYUV->data[2], 1, y_size / 4, fp_out);   
		}
	}

	//�ͷ�FFmpeg�������Դ
	fclose(fp_in);
	fclose(fp_out);

	sws_freeContext(img_convert_ctx);
	av_parser_close(pCodecParserCtx);

	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx);
	av_free(pCodecCtx);

	return 0;
}

