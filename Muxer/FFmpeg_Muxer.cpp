/**************************************************************************************
File：FFmpeg_Muxer.cpp
Description：FFmpeg采集音频和视频，实时封装，以mp4格式保存在本地
**************************************************************************************/
#include <stdio.h>
#include <conio.h>
#include <windows.h>
#include <iostream>

extern "C"
{
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/mathematics.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/audio_fifo.h"
};

using namespace std;

int flush_encoder(AVFormatContext *ifmt_ctx, AVFormatContext *ofmt_ctx, unsigned int stream_index, int framecnt);
int flush_encoder_a(AVFormatContext *ifmt_ctx_a, AVFormatContext *ofmt_ctx, unsigned int stream_index, int nb_samples);
int exit_thread = 0;

/**************************************************************************************
Function：dup_wchar_to_utf8
Description：编码格式的转换，换为UTF-8编码
**************************************************************************************/
char *dup_wchar_to_utf8(const wchar_t *w)
{
	char *s = NULL;
	int l = WideCharToMultiByte(CP_UTF8, 0, w, -1, 0, 0, 0, 0);
	s = (char *)av_malloc(l);
	if (s)
	{
		WideCharToMultiByte(CP_UTF8, 0, w, -1, s, l, 0, 0);
	}
	return s;
}

/**************************************************************************************
Function：MyThreadFunction
Description：线程函数，判断是否有回车键按下，按下停止保存，退出程序
**************************************************************************************/
DWORD WINAPI MyThreadFunction(LPVOID lpParam)
{
	while ((getchar()) != '\n')
	{

	}
	exit_thread = 1;
	return 0;
}

/**************************************************************************************
Function：main
Description：打开音频和视频设备，同时采集，保存为mp4文件
**************************************************************************************/
int main(int argc, char* argv[])
{
	AVFormatContext *ifmt_ctx = NULL;
	AVFormatContext *ifmt_ctx_a = NULL;
	AVFormatContext *ofmt_ctx;
	AVInputFormat* ifmt;
	AVStream* video_st;
	AVStream* audio_st;
	AVCodecContext* pCodecCtx;
	AVCodecContext* pCodecCtx_a;
	AVCodec* pCodec;
	AVCodec* pCodec_a;
	AVPacket *dec_pkt, enc_pkt;
	AVPacket *dec_pkt_a, enc_pkt_a;
	AVFrame *pframe, *pFrameYUV;
	struct SwsContext *img_convert_ctx;
	struct SwrContext *aud_convert_ctx;

	int framecnt = 0;
	int nb_samples = 0;
	int videoindex;
	int audioindex;
	int i;
	int ret;
	HANDLE hThread;

	const char* out_path = "result.mp4";
	int dec_got_frame, enc_got_frame;
	int dec_got_frame_a, enc_got_frame_a;

	int aud_next_pts = 0;
	int vid_next_pts = 0;
	int encode_video = 1, encode_audio = 1;

	AVRational time_base_q = {1, AV_TIME_BASE};

	av_register_all();
	avdevice_register_all();
	avformat_network_init();

	//指定电脑的视音频设备名称
	char *device_name_a = dup_wchar_to_utf8(L"audio=麦克风 (HD Pro Webcam C920)");
	char *device_name = "video=Logitech HD Pro Webcam C920";

	ifmt = av_find_input_format("dshow");

	AVDictionary *device_param = nullptr;
	av_dict_set_int(&device_param, "rtbufsize", 18432000, 0);

	if (avformat_open_input(&ifmt_ctx, device_name, ifmt, &device_param) != 0)
	{
		printf("Couldn't open input video stream.\n");
		return -1;
	}
	if (avformat_open_input(&ifmt_ctx_a, device_name_a, ifmt, &device_param) != 0)
	{
		printf("Couldn't open input audio stream.\n");
		return -1;
	}
	
	//找到视频流
	if (avformat_find_stream_info(ifmt_ctx, NULL) < 0)
	{
		printf("Couldn't find video stream information.\n");
		return -1;
	}
	videoindex = -1;
	for (i = 0; i < ifmt_ctx->nb_streams; i++)
	{
		if (ifmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoindex = i;
			break;
		}
	}
	if (videoindex == -1)
	{
		printf("Couldn't find a video stream.\n");
		return -1;
	}
	if (avcodec_open2(ifmt_ctx->streams[videoindex]->codec, avcodec_find_decoder(ifmt_ctx->streams[videoindex]->codec->codec_id), NULL) < 0)
	{
		printf("Could not open video codec.\n");
		return -1;
	}

	//找到音频流
	if (avformat_find_stream_info(ifmt_ctx_a, NULL) < 0)
	{
		printf("Couldn't find audio stream information.\n");
		return -1;
	}
	audioindex = -1;
	for (i = 0; i < ifmt_ctx_a->nb_streams; i++)
	{
		if (ifmt_ctx_a->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			audioindex = i;
			break;
		}
	}
	if (audioindex == -1)
	{
		printf("Couldn't find a audio stream.\n");
		return -1;
	}
	if (avcodec_open2(ifmt_ctx_a->streams[audioindex]->codec, avcodec_find_decoder(ifmt_ctx_a->streams[audioindex]->codec->codec_id), NULL) < 0)
	{
		printf("Could not open audio codec.\n");
		return -1;
	}

	//创建输出文件的上下文句柄
	avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", out_path);
	
	//设置视频编码器的参数
	pCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!pCodec)
	{
		printf("Can not find output video encoder!\n");
		return -1;
	}
	pCodecCtx = avcodec_alloc_context3(pCodec);
	pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
	pCodecCtx->width = ifmt_ctx->streams[videoindex]->codec->width;
	pCodecCtx->height = ifmt_ctx->streams[videoindex]->codec->height;
	pCodecCtx->time_base.num = 1;
	pCodecCtx->time_base.den = 25;
	pCodecCtx->bit_rate = 300000;
	pCodecCtx->gop_size = 250;
	if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
	{
		pCodecCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;
	}
	pCodecCtx->qmin = 10;
	pCodecCtx->qmax = 51;
	pCodecCtx->max_b_frames = 0;
	AVDictionary *param = 0;
	av_dict_set(&param, "preset", "fast", 0);
	av_dict_set(&param, "tune", "zerolatency", 0);
	if (avcodec_open2(pCodecCtx, pCodec, &param) < 0)
	{
		printf("Failed to open output video encoder!\n");
		return -1;
	}

	//设置音频编码器的参数
	pCodec_a = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (!pCodec_a)
	{
		printf("Can not find output audio encoder!\n");
		return -1;
	}
	pCodecCtx_a = avcodec_alloc_context3(pCodec_a);
	pCodecCtx_a->channels = 2;
	pCodecCtx_a->channel_layout = av_get_default_channel_layout(2);
	pCodecCtx_a->sample_rate = ifmt_ctx_a->streams[audioindex]->codec->sample_rate;
	pCodecCtx_a->sample_fmt = pCodec_a->sample_fmts[0];
	pCodecCtx_a->bit_rate = 32000;
	pCodecCtx_a->time_base.num = 1;
	pCodecCtx_a->time_base.den = pCodecCtx_a->sample_rate;
	pCodecCtx_a->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
	if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
	{
		pCodecCtx_a->flags |= CODEC_FLAG_GLOBAL_HEADER;
	}

	if (avcodec_open2(pCodecCtx_a, pCodec_a, NULL) < 0)
	{
		printf("Failed to open ouput audio encoder!\n");
		return -1;
	}

	//在写头部信息前，添加视频流和音频流
	video_st = avformat_new_stream(ofmt_ctx, pCodec);
	if (video_st == NULL)
	{
		return -1;
	}
	video_st->time_base.num = 1;
	video_st->time_base.den = 25;
	video_st->codec = pCodecCtx;
	audio_st = avformat_new_stream(ofmt_ctx, pCodec_a);
	if (audio_st == NULL)
	{
		return -1;
	}
	audio_st->time_base.num = 1;
	audio_st->time_base.den = pCodecCtx_a->sample_rate;
	audio_st->codec = pCodecCtx_a;
	if (avio_open(&ofmt_ctx->pb, out_path, AVIO_FLAG_READ_WRITE) < 0)
	{
		printf("Failed to open output file!\n");
		return -1;
	}
	av_dump_format(ofmt_ctx, 0, out_path, 1);
	
	//写入输出文件的头部信息
	avformat_write_header(ofmt_ctx, NULL);

	dec_pkt = (AVPacket *)av_malloc(sizeof(AVPacket));
	img_convert_ctx = sws_getContext(ifmt_ctx->streams[videoindex]->codec->width, ifmt_ctx->streams[videoindex]->codec->height, ifmt_ctx->streams[videoindex]->codec->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
	aud_convert_ctx = swr_alloc_set_opts(NULL, av_get_default_channel_layout(pCodecCtx_a->channels), pCodecCtx_a->sample_fmt, pCodecCtx_a->sample_rate, av_get_default_channel_layout(ifmt_ctx_a->streams[audioindex]->codec->channels), ifmt_ctx_a->streams[audioindex]->codec->sample_fmt, ifmt_ctx_a->streams[audioindex]->codec->sample_rate, 0, NULL);
	swr_init(aud_convert_ctx);

	pFrameYUV = av_frame_alloc();
	uint8_t *out_buffer = (uint8_t *)av_malloc(avpicture_get_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));
	avpicture_fill((AVPicture *)pFrameYUV, out_buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
	AVAudioFifo *fifo = NULL;
	fifo = av_audio_fifo_alloc(pCodecCtx_a->sample_fmt, pCodecCtx_a->channels, 1);
	uint8_t **converted_input_samples = NULL;

	if (!(converted_input_samples = (uint8_t**)calloc(pCodecCtx_a->channels, sizeof(**converted_input_samples)))) 
	{
		printf("Could not allocate converted input sample pointers\n");
		return AVERROR(ENOMEM);
	}
	printf("\n --------call started----------\n");
	printf("\nPress enter to stop...\n");

	hThread = CreateThread(NULL, 0, MyThreadFunction, NULL, 0, NULL);  
	int64_t start_time = av_gettime();
	while (encode_video || encode_audio)
	{
		//通过时间戳确定写入视频数据或者音频数据
		if (encode_video && (!encode_audio || av_compare_ts(vid_next_pts, time_base_q, aud_next_pts, time_base_q) <= 0))
		{
			if ((ret = av_read_frame(ifmt_ctx, dec_pkt)) >= 0)
			{
				if (exit_thread)
				{
					break;
				}	
				av_log(NULL, AV_LOG_DEBUG, "Going to reencode the frame\n");
				pframe = av_frame_alloc();
				if (!pframe) 
				{
					ret = AVERROR(ENOMEM);
					return ret;
				}

				ret = avcodec_decode_video2(ifmt_ctx->streams[dec_pkt->stream_index]->codec, pframe, &dec_got_frame, dec_pkt);
				if (ret < 0) 
				{
					av_frame_free(&pframe);
					av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
					break;
				}
				if (dec_got_frame)
				{
					sws_scale(img_convert_ctx, (const uint8_t* const*)pframe->data, pframe->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
					pFrameYUV->width = pframe->width;
					pFrameYUV->height = pframe->height;
					pFrameYUV->format = AV_PIX_FMT_YUV420P;

					enc_pkt.data = NULL;
					enc_pkt.size = 0;
					av_init_packet(&enc_pkt);
					ret = avcodec_encode_video2(pCodecCtx, &enc_pkt, pFrameYUV, &enc_got_frame);
					av_frame_free(&pframe);
					if (enc_got_frame == 1)
					{
						framecnt++;
						enc_pkt.stream_index = video_st->index;

						AVRational time_base = ofmt_ctx->streams[0]->time_base;
						AVRational r_framerate1 = ifmt_ctx->streams[videoindex]->r_frame_rate;						
						int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));	
									
						enc_pkt.pts = av_rescale_q(framecnt*calc_duration, time_base_q, time_base);
						enc_pkt.dts = enc_pkt.pts;
						enc_pkt.duration = av_rescale_q(calc_duration, time_base_q, time_base); 
						enc_pkt.pos = -1;		
						vid_next_pts = framecnt*calc_duration;

						int64_t pts_time = av_rescale_q(enc_pkt.pts, time_base, time_base_q);
						int64_t now_time = av_gettime() - start_time;
						if ((pts_time > now_time) && ((vid_next_pts + pts_time - now_time) < aud_next_pts))
						{
							av_usleep(pts_time - now_time);
						}
							
						ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
						av_free_packet(&enc_pkt);
					}
				}
				else
				{
					av_frame_free(&pframe);
				}
				av_free_packet(dec_pkt);
			}
			else
			{
				if (ret == AVERROR_EOF)
				{
					encode_video = 0;
				}					
				else
				{
					printf("Could not read video frame\n");
					return ret;
				}
			}			
		}
		else
		{			
			const int output_frame_size = pCodecCtx_a->frame_size;
			if (exit_thread)
			{
				break;
			}
				
			while (av_audio_fifo_size(fifo) < output_frame_size)
			{
				AVFrame *input_frame = av_frame_alloc();
				if (!input_frame)
				{
					ret = AVERROR(ENOMEM);
					return ret;
				}

				AVPacket input_packet;
				av_init_packet(&input_packet);
				input_packet.data = NULL;
				input_packet.size = 0;

				if ((ret = av_read_frame(ifmt_ctx_a, &input_packet)) < 0)
				{
					if (ret == AVERROR_EOF)
					{
						encode_audio = 0;
					}
					else
					{
						printf("Could not read audio frame\n");
						return ret;
					}
				}
				if ((ret = avcodec_decode_audio4(ifmt_ctx_a->streams[audioindex]->codec, input_frame, &dec_got_frame_a, &input_packet)) < 0)
				{
					printf("Could not decode audio frame\n");
					return ret;
				}
				av_packet_unref(&input_packet);

				if (dec_got_frame_a)
				{
					if ((ret = av_samples_alloc(converted_input_samples, NULL, pCodecCtx_a->channels, input_frame->nb_samples, pCodecCtx_a->sample_fmt, 0)) < 0) 
					{
						printf("Could not allocate converted input samples\n");
						av_freep(&(*converted_input_samples)[0]);
						free(*converted_input_samples);
						return ret;
					}
					if ((ret = swr_convert(aud_convert_ctx, converted_input_samples, input_frame->nb_samples, (const uint8_t**)input_frame->extended_data, input_frame->nb_samples)) < 0) 
					{
						printf("Could not convert input samples\n");
						return ret;
					}
					if ((ret = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + input_frame->nb_samples)) < 0) {
						printf("Could not reallocate FIFO\n");
						return ret;
					}
					if (av_audio_fifo_write(fifo, (void **)converted_input_samples, input_frame->nb_samples) < input_frame->nb_samples) 
					{
						printf("Could not write data to FIFO\n");
						return AVERROR_EXIT;
					}
				}
			}
			if (av_audio_fifo_size(fifo) >= output_frame_size)
			{
				AVFrame *output_frame = av_frame_alloc();
				if (!output_frame)
				{
					ret = AVERROR(ENOMEM);
					return ret;
				}

				const int frame_size = FFMIN(av_audio_fifo_size(fifo), pCodecCtx_a->frame_size);
				output_frame->nb_samples = frame_size;
				output_frame->channel_layout = pCodecCtx_a->channel_layout;
				output_frame->format = pCodecCtx_a->sample_fmt;
				output_frame->sample_rate = pCodecCtx_a->sample_rate;

				if ((ret = av_frame_get_buffer(output_frame, 0)) < 0)
				{
					printf("Could not allocate output frame samples\n");
					av_frame_free(&output_frame);
					return ret;
				}
				if (av_audio_fifo_read(fifo, (void **)output_frame->data, frame_size) < frame_size)
				{
					printf("Could not read data from FIFO\n");
					return AVERROR_EXIT;
				}

				AVPacket output_packet;
				av_init_packet(&output_packet);
				output_packet.data = NULL;
				output_packet.size = 0;

				if (output_frame)
				{
					nb_samples += output_frame->nb_samples;
				}

				if ((ret = avcodec_encode_audio2(pCodecCtx_a, &output_packet, output_frame, &enc_got_frame_a)) < 0)
				{
					printf("Could not encode frame\n");
					av_packet_unref(&output_packet);
					return ret;
				}
				if (enc_got_frame_a)
				{
					output_packet.stream_index = 1;

					AVRational time_base = ofmt_ctx->streams[1]->time_base;
					AVRational r_framerate1 = { ifmt_ctx_a->streams[audioindex]->codec->sample_rate, 1 };
					int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));

					output_packet.pts = av_rescale_q(nb_samples*calc_duration, time_base_q, time_base);
					output_packet.dts = output_packet.pts;
					output_packet.duration = output_frame->nb_samples;

					aud_next_pts = nb_samples*calc_duration;

					int64_t pts_time = av_rescale_q(output_packet.pts, time_base, time_base_q);
					int64_t now_time = av_gettime() - start_time;
					if ((pts_time > now_time) && ((aud_next_pts + pts_time - now_time) < vid_next_pts))
					{
						av_usleep(pts_time - now_time);
					}
					if ((ret = av_interleaved_write_frame(ofmt_ctx, &output_packet)) < 0)
					{
						printf("Could not write frame\n");
						av_packet_unref(&output_packet);
						return ret;
					}
					av_packet_unref(&output_packet);
				}
				av_frame_free(&output_frame);
			}
		}
	}

	ret = flush_encoder(ifmt_ctx, ofmt_ctx, 0, framecnt);
	if (ret < 0)
	{
		printf("Flushing encoder failed\n");
		return -1;
	}
	ret = flush_encoder_a(ifmt_ctx_a, ofmt_ctx, 1, nb_samples);
	if (ret < 0)
	{
		printf("Flushing encoder failed\n");
		return -1;
	}
	
	//写入输出文件的尾部信息
	av_write_trailer(ofmt_ctx);

cleanup:
	if (video_st)
	{
		avcodec_close(video_st->codec);
	}
	if (audio_st)
	{
		avcodec_close(audio_st->codec);
	}
	av_free(out_buffer);
	if (converted_input_samples)
	{
		av_freep(&converted_input_samples[0]);
	}
	if (fifo)
	{
		av_audio_fifo_free(fifo);
	}
	avio_close(ofmt_ctx->pb);
	avformat_free_context(ifmt_ctx);
	avformat_free_context(ifmt_ctx_a);
	avformat_free_context(ofmt_ctx);
	CloseHandle(hThread);
	
	return 0;
}

/**************************************************************************************
Function：flush_encoder
Description：编码编码器内剩余的视频帧
**************************************************************************************/
int flush_encoder(AVFormatContext *ifmt_ctx, AVFormatContext *ofmt_ctx, unsigned int stream_index, int framecnt)
{
	int ret;
	int got_frame;
	AVPacket enc_pkt;
	if (!(ofmt_ctx->streams[stream_index]->codec->codec->capabilities & CODEC_CAP_DELAY))
	{
		return 0;
	}
	while (1) 
	{
		enc_pkt.data = NULL;
		enc_pkt.size = 0;
		av_init_packet(&enc_pkt);
		ret = avcodec_encode_video2(ofmt_ctx->streams[stream_index]->codec, &enc_pkt, NULL, &got_frame);
		av_frame_free(NULL);
		if (ret < 0)
		{
			break;
		}
		if (!got_frame)
		{
			ret = 0;
			break;
		}
		printf("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n", enc_pkt.size);
		framecnt++;

		AVRational time_base = ofmt_ctx->streams[stream_index]->time_base;
		AVRational r_framerate1 = ifmt_ctx->streams[0]->r_frame_rate;
		AVRational time_base_q = { 1, AV_TIME_BASE };
		int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));	
		
		enc_pkt.pts = av_rescale_q(framecnt*calc_duration, time_base_q, time_base);
		enc_pkt.dts = enc_pkt.pts;
		enc_pkt.duration = av_rescale_q(calc_duration, time_base_q, time_base);
		enc_pkt.pos = -1;

		ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
		if (ret < 0)
		{
			break;
		}			
	}
	return ret;
}

/**************************************************************************************
Function：flush_encoder_a
Description：编码编码器内剩余的音频帧
**************************************************************************************/
int flush_encoder_a(AVFormatContext *ifmt_ctx_a, AVFormatContext *ofmt_ctx, unsigned int stream_index, int nb_samples)
{
	int ret;
	int got_frame;
	AVPacket enc_pkt;
	if (!(ofmt_ctx->streams[stream_index]->codec->codec->capabilities & CODEC_CAP_DELAY))
	{
		return 0;
	}
	while (1)
	{
		enc_pkt.data = NULL;
		enc_pkt.size = 0;
		av_init_packet(&enc_pkt);
		ret = avcodec_encode_audio2(ofmt_ctx->streams[stream_index]->codec, &enc_pkt, NULL, &got_frame);
		av_frame_free(NULL);
		if (ret < 0)
		{
			break;
		}
		if (!got_frame)
		{
			ret = 0;
			break;
		}
		printf("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n", enc_pkt.size);
		nb_samples += 1024;

		AVRational time_base = ofmt_ctx->streams[stream_index]->time_base;
		AVRational r_framerate1 = { ifmt_ctx_a->streams[0]->codec->sample_rate, 1 };
		AVRational time_base_q = { 1, AV_TIME_BASE };
		int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));	
		
		enc_pkt.pts = av_rescale_q(nb_samples*calc_duration, time_base_q, time_base);
		enc_pkt.dts = enc_pkt.pts;
		enc_pkt.duration = 1024;
		enc_pkt.pos = -1;

		ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
		if (ret < 0)
		{
			break;
		}
	}
	return ret;
}
