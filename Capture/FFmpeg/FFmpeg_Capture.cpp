/**************************************************************************************
File：FFmpeg_Capture.cpp
Description：FFmpeg打开RTSP网络流，数据流保存在本地
**************************************************************************************/
#include <stdio.h>
#include <string>
#include <memory>
#include <thread>
#include <iostream>

extern "C"
{
	#include "libavutil/opt.h"
	#include "libavutil/channel_layout.h"
	#include "libavutil/common.h"
	#include "libavutil/imgutils.h"
	#include "libavutil/mathematics.h"
	#include "libavutil/samplefmt.h"
	#include "libavutil/time.h"
	#include "libavutil/fifo.h"
	#include "libavcodec/avcodec.h"
	#include "libavformat/avformat.h"
	#include "libavformat/avio.h"
	#include "libavfilter/avfiltergraph.h"
	#include "libavfilter/avfilter.h"
	#include "libavfilter/buffersink.h"
	#include "libavfilter/buffersrc.h"
	#include "libswscale/swscale.h"
	#include "libswresample/swresample.h"
}

using namespace std;

//结构体：保存输入和输出上下文信息
AVFormatContext *inputContext = nullptr;
AVFormatContext *outputContext;

/**************************************************************************************
Function：Init
Description：初始化
**************************************************************************************/
void Init()
{
	av_register_all();
	avfilter_register_all();
	avformat_network_init();
	av_log_set_level(AV_LOG_ERROR);
}

/**************************************************************************************
Function：OpenInput
Description：打开输入的网络流地址
**************************************************************************************/
int OpenInput(string inputUrl)
{
	inputContext = avformat_alloc_context();	
	int ret = avformat_open_input(&inputContext, inputUrl.c_str(), nullptr, nullptr);
	if(ret < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "Input file open input failed\n");
		return  ret;
	}
	ret = avformat_find_stream_info(inputContext, nullptr);
	if(ret < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "Find input file stream inform failed\n");
	}
	else
	{
		av_log(NULL, AV_LOG_FATAL, "Open input file  %s success\n",inputUrl.c_str());
	}
	return ret;
}

/**************************************************************************************
Function：OpenOutput
Description：输出文件的初始化，在保存数据之前要先写入头部信息
**************************************************************************************/
int OpenOutput(string outUrl)
{
	int ret = avformat_alloc_output_context2(&outputContext, nullptr, "mpegts", outUrl.c_str());
	if (ret < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "open output context failed\n");
		goto Error;
	}
	ret = avio_open2(&outputContext->pb, outUrl.c_str(), AVIO_FLAG_WRITE, nullptr, nullptr);
	if (ret < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "open avio failed");
		goto Error;
	}
	for (int i = 0; i < inputContext->nb_streams; i++)
	{
		AVStream *stream = avformat_new_stream(outputContext, inputContext->streams[i]->codec->codec);
		ret = avcodec_copy_context(stream->codec, inputContext->streams[i]->codec);
		if (ret < 0)
		{
			av_log(NULL, AV_LOG_ERROR, "copy coddec context failed");
			goto Error;
		}
	}
	ret = avformat_write_header(outputContext, nullptr);
	if (ret < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "format write header failed");
		goto Error;
	}

	av_log(NULL, AV_LOG_FATAL, " Open output file success %s\n", outUrl.c_str());
	return ret;
	
Error:
	if (outputContext)
	{
		for (int i = 0; i < outputContext->nb_streams; i++)
		{
			avcodec_close(outputContext->streams[i]->codec);
		}
		avformat_close_input(&outputContext);
	}
	return ret;
}

/**************************************************************************************
Function：ReadPacketFromSource
Description：读取完整的一个packet
**************************************************************************************/
shared_ptr<AVPacket> ReadPacketFromSource()
{
	shared_ptr<AVPacket> packet(static_cast<AVPacket*>(av_malloc(sizeof(AVPacket))), [&](AVPacket *p) {av_packet_free(&p); av_freep(&p); });
	av_init_packet(packet.get());
	int ret = av_read_frame(inputContext, packet.get());
	if (ret >= 0)
	{
		return packet;
	}
	else
	{
		return nullptr;
	}
}

/**************************************************************************************
Function：av_packet_rescale_ts
Description：时间戳的校正
**************************************************************************************/
void av_packet_rescale_ts(AVPacket *pkt, AVRational src_tb, AVRational dst_tb)
{
	if (pkt->pts != AV_NOPTS_VALUE)
	{
		pkt->pts = av_rescale_q(pkt->pts, src_tb, dst_tb);
	}
	if (pkt->dts != AV_NOPTS_VALUE)
	{
		pkt->dts = av_rescale_q(pkt->dts, src_tb, dst_tb);
	}
	if (pkt->duration > 0)
	{
		pkt->duration = av_rescale_q(pkt->duration, src_tb, dst_tb);
	}	
}

/**************************************************************************************
Function：WritePacket
Description：将完整的一个packet保存在本地
**************************************************************************************/
int WritePacket(shared_ptr<AVPacket> packet)
{
	auto inputStream = inputContext->streams[packet->stream_index];
	auto outputStream = outputContext->streams[packet->stream_index];
	av_packet_rescale_ts(packet.get(), inputStream->time_base, outputStream->time_base);
	return av_interleaved_write_frame(outputContext, packet.get());
}

/**************************************************************************************
Function：CloseInput
Description：关闭输入文件句柄
**************************************************************************************/
void CloseInput()
{
	if(inputContext != nullptr)
	{
		avformat_close_input(&inputContext);
	}
}

/**************************************************************************************
Function：CloseOutput
Description：关闭输出文件句柄
**************************************************************************************/
void CloseOutput()
{
	if(outputContext != nullptr)
	{
		for(int i = 0 ; i < outputContext->nb_streams; i++)
		{
			AVCodecContext *codecContext = outputContext->streams[i]->codec;
			avcodec_close(codecContext);
		}
		avformat_close_input(&outputContext);
	}
}

/**************************************************************************************
Function：main
Description：主函数，提供RTSP流地址
             rtsp://user:password@ip/h264/ch1/main(sub)/av_stream
**************************************************************************************/
int main()
{
	Init();
	int ret = OpenInput("rtsp://admin:hk888888@10.11.0.5/h264/ch1/sub/av_stream");
	if(ret >= 0)
	{
		ret = OpenOutput("test.264"); 
	}
	if(ret < 0)
	{
		goto Error;
	}

	while(true)
	{
		auto packet = ReadPacketFromSource();
		if(packet)
		{
			ret = WritePacket(packet);
			if(ret >= 0)
			{
				cout<<"WritePacket Success!"<<endl;
			}
			else
			{
				cout<<"WritePacket failed!"<<endl;
			}
		}
		else
		{
			break;
		}
	}
	
Error:
	CloseInput();
	CloseOutput();
	while(true)
	{
		this_thread::sleep_for(chrono::seconds(100));
	}
	return 0;
}

