/**************************************************************************************
File：FFmpeg_Encode.cpp
Description：FFmpeg实现简单的编码器，OpenCV打开摄像头，并实时编码
**************************************************************************************/
#include <stdio.h>
#include <iostream>
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

using namespace cv;
using namespace std;

int main()
{
	//FFmpeg结构体初始化
	AVFormatContext *pFormatCtx = nullptr;
	AVOutputFormat *fmt = nullptr;
	AVStream *video_st = nullptr;
	AVCodecContext *pCodecCtx = nullptr;
	AVCodec *pCodec = nullptr;
	AVFrame *picture = nullptr;
	
	//一些参数的定义
	uint8_t *picture_buf = nullptr;
	int size;
	int in_w = 640, in_h = 480;
	const char* out_file = "result.h264";
	int yuv_bufLen = in_w * in_h * 3 / 2;

	//打开摄像头
	VideoCapture capture(0);
	Mat frame;

	avcodec_register_all();
	av_register_all();
	
	avformat_alloc_output_context2(&pFormatCtx, NULL, NULL, out_file);
	fmt = pFormatCtx->oformat;

	if (avio_open(&pFormatCtx->pb, out_file, AVIO_FLAG_READ_WRITE))
	{
		cout << "output file open fail!" << endl;
	}

	video_st = avformat_new_stream(pFormatCtx, 0);
	if (video_st == NULL)
	{
		printf("failed allocating output stram\n");
	}
	
	video_st->time_base.num = 1;
	video_st->time_base.den = 25;

	//参数设置
	pCodecCtx = video_st->codec;
	pCodecCtx->codec_id = fmt->video_codec;
	pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
	pCodecCtx->width = in_w;
	pCodecCtx->height = in_h;
	pCodecCtx->time_base.num = 1;
	pCodecCtx->time_base.den = 25;
	pCodecCtx->bit_rate = 4000000;
	pCodecCtx->gop_size = 12;

	if (pCodecCtx->codec_id == AV_CODEC_ID_H264)
	{
		pCodecCtx->qmin = 10;
		pCodecCtx->qmax = 51;
		pCodecCtx->qcompress = 0.6;
	}

	pCodec = avcodec_find_encoder(pCodecCtx->codec_id);
	if (avcodec_open2(pCodecCtx, pCodec, NULL)<0)
	{
		cout << "open encoder fail!" << endl;
	}

	av_dump_format(pFormatCtx, 0, out_file, 1);

	picture = av_frame_alloc();
	picture->width = pCodecCtx->width;
	picture->height = pCodecCtx->height;
	picture->format = pCodecCtx->pix_fmt;
	size = avpicture_get_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);
	picture_buf = (uint8_t*)av_malloc(size);
	avpicture_fill((AVPicture*)picture, picture_buf, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);

	//编码前先写好输出视频的头部信息
	avformat_write_header(pFormatCtx, NULL);

	AVPacket pkt; 
	int y_size = pCodecCtx->width*pCodecCtx->height;
	av_new_packet(&pkt, size * 3);

	int i = 0;
	while (1)
	{
		capture >> frame;

		Mat yuvImg;
		cvtColor(frame, yuvImg, CV_BGR2YUV_I420);
		memcpy(picture_buf, yuvImg.data, yuv_bufLen*sizeof(unsigned char));

		picture->data[0] = picture_buf; 
		picture->data[1] = picture_buf + y_size; 
		picture->data[2] = picture_buf + y_size * 5 / 4; 
		
		picture->pts = i;
		int got_picture = 0;

		int ret = avcodec_encode_video2(pCodecCtx, &pkt, picture, &got_picture);
		if (ret<0)
		{
			cout << "encoder fail!" << endl;
		}

		if (got_picture == 1)
		{	
			pkt.stream_index = video_st->index;
			pkt.pos = -1;
			pkt.pts = i;

			ret = av_interleaved_write_frame(pFormatCtx, &pkt);
			av_free_packet(&pkt);
		}
		i++;
	}

	//写入输出文件的尾部信息
	av_write_trailer(pFormatCtx);

	//FFmpeg资源释放
	avcodec_close(video_st->codec);
	av_free(picture);
	av_free(picture_buf);	
	avio_close(pFormatCtx->pb);
	avformat_free_context(pFormatCtx);

	return 0;
}
