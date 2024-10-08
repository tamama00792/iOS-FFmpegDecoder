#include "accompany_decoder.h"

#define LOG_TAG "AccompanyDecoder"

/// 初始化解码器
AccompanyDecoder::AccompanyDecoder() {
    
	this->seek_seconds = 0.0f;
	this->seek_req = false;
	this->seek_resp = false;
	accompanyFilePath = NULL;
}

AccompanyDecoder::~AccompanyDecoder() {
	if(NULL != accompanyFilePath){
		delete[] accompanyFilePath;
		accompanyFilePath = NULL;
	}
}

/// 获取采样率和比特率
/// - Parameters:
///   - fileString: 文件路径
///   - metaData: 存储信息用的数组
int AccompanyDecoder::getMusicMeta(const char* fileString, int * metaData) {
    // 读取文件信息并初始化
	init(fileString);
    // 获取采样率
	int sampleRate = avCodecContext->sample_rate;
	LOGI("sampleRate is %d", sampleRate);
    // 获取比特率
	int bitRate = avCodecContext->bit_rate;
	LOGI("bitRate is %d", bitRate);
    // 销毁当前解码器
	destroy();
    // 存储采样率和比特率
	metaData[0] = sampleRate;
	metaData[1] = bitRate;
	return 0;
}

void AccompanyDecoder::init(const char* fileString, int packetBufferSizeParam){
	init(fileString);
	packetBufferSize = packetBufferSizeParam;
}

int AccompanyDecoder::init(const char* audioFile) {
	LOGI("enter AccompanyDecoder::init");
	audioBuffer = NULL;
	position = -1.0f;
	audioBufferCursor = 0;
	audioBufferSize = 0;
	swrContext = NULL;
	swrBuffer = NULL;
	swrBufferSize = 0;
	seek_success_read_frame_success = true;
	isNeedFirstFrameCorrectFlag = true;
	firstFrameCorrectionInSecs = 0.0f;

    // 注册编解码器
	avcodec_register_all();
    // 注册所有内容，包含编解码器、复用器、解复用器、协议处理器
	av_register_all();
    // 开辟内存，用于后续存储上下文
	avFormatContext = avformat_alloc_context();
	// 打开输入文件
	LOGI("open accompany file %s....", audioFile);

    // 如果文件路径为空，则从参数复制文件路径
	if(NULL == accompanyFilePath){
		int length = strlen(audioFile);
		accompanyFilePath = new char[length + 1];
		//由于最后一个是'\0' 所以memset的长度要设置为length+1
		memset(accompanyFilePath, 0, length + 1);
		memcpy(accompanyFilePath, audioFile, length + 1);
	}

    // 打开输入流，读取文件信息，但不打开编码器
	int result = avformat_open_input(&avFormatContext, audioFile, NULL, NULL);
	if (result != 0) {
		LOGI("can't open file %s result is %d", audioFile, result);
		return -1;
	} else {
		LOGI("open file %s success and result is %d", audioFile, result);
	}
    // 设置最大分析时间
	avFormatContext->max_analyze_duration = 50000;
	// 检查在文件中的流的信息
	result = avformat_find_stream_info(avFormatContext, NULL);
	if (result < 0) {
		LOGI("fail avformat_find_stream_info result is %d", result);
		return -1;
	} else {
		LOGI("sucess avformat_find_stream_info result is %d", result);
	}
    // 获取最合适的音频流
	stream_index = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	LOGI("stream_index is %d", stream_index);
	// 没有音频
	if (stream_index == -1) {
		LOGI("no audio stream");
		return -1;
	}
	// 获取音频流
	AVStream* audioStream = avFormatContext->streams[stream_index];
    // 计算时基的精确值
	if (audioStream->time_base.den && audioStream->time_base.num)
		timeBase = av_q2d(audioStream->time_base);
	else if (audioStream->codec->time_base.den && audioStream->codec->time_base.num)
		timeBase = av_q2d(audioStream->codec->time_base);
	// 获得音频流的解码器上下文
	avCodecContext = audioStream->codec;
	LOGI("avCodecContext->codec_id is %d AV_CODEC_ID_AAC is %d", avCodecContext->codec_id, AV_CODEC_ID_AAC);
    // 根据解码器上下文找到解码器
	AVCodec * avCodec = avcodec_find_decoder(avCodecContext->codec_id);
	if (avCodec == NULL) {
		LOGI("Unsupported codec ");
		return -1;
	}
	// 打开解码器
	result = avcodec_open2(avCodecContext, avCodec, NULL);
	if (result < 0) {
		LOGI("fail avformat_find_stream_info result is %d", result);
		return -1;
	} else {
		LOGI("sucess avformat_find_stream_info result is %d", result);
	}
	//4、判断是否需要resampler重采样
	if (!audioCodecIsSupported()) {
		LOGI("because of audio Codec Is Not Supported so we will init swresampler...");
		/**
		 * 初始化resampler
		 * @param s               Swr context, can be NULL
		 * @param out_ch_layout   output channel layout (AV_CH_LAYOUT_*)
		 * @param out_sample_fmt  output sample format (AV_SAMPLE_FMT_*).
		 * @param out_sample_rate output sample rate (frequency in Hz)
		 * @param in_ch_layout    input channel layout (AV_CH_LAYOUT_*)
		 * @param in_sample_fmt   input sample format (AV_SAMPLE_FMT_*).
		 * @param in_sample_rate  input sample rate (frequency in Hz)
		 * @param log_offset      logging level offset
		 * @param log_ctx         parent logging context, can be NULL
		 */
		 // 设置重采样参数并初始化
		swrContext = swr_alloc_set_opts(NULL, av_get_default_channel_layout(OUT_PUT_CHANNELS), AV_SAMPLE_FMT_S16, avCodecContext->sample_rate,
				av_get_default_channel_layout(avCodecContext->channels), avCodecContext->sample_fmt, avCodecContext->sample_rate, 0, NULL);
				// 如果初始化重采样上下文失败则释放资源返回
		if (!swrContext || swr_init(swrContext)) {
			if (swrContext)
				swr_free(&swrContext);
			avcodec_close(avCodecContext);
			LOGI("init resampler failed...");
			return -1;
		}
	}
	LOGI("channels is %d sampleRate is %d", avCodecContext->channels, avCodecContext->sample_rate);
	// 初始化一个avframe
	pAudioFrame = avcodec_alloc_frame();
//	LOGI("leave AccompanyDecoder::init");
	return 1;
}

bool AccompanyDecoder::audioCodecIsSupported() {
// 判断是否为pcm格式
	if (avCodecContext->sample_fmt == AV_SAMPLE_FMT_S16) {
		return true;
	}
	return false;
}

AudioPacket* AccompanyDecoder::decodePacket(){
//	LOGI("MadDecoder::decodePacket packetBufferSize is %d", packetBufferSize);
	short* samples = new short[packetBufferSize];
//	LOGI("accompanyPacket buffer's addr is %x", samples);
	int stereoSampleSize = readSamples(samples, packetBufferSize);
	AudioPacket* samplePacket = new AudioPacket();
	if (stereoSampleSize > 0) {
		//构造成一个packet
		samplePacket->buffer = samples;
		samplePacket->size = stereoSampleSize;
		/** 这里由于每一个packet的大小不一样有可能是200ms 但是这样子position就有可能不准确了 **/
		samplePacket->position = position;
	} else {
		samplePacket->size = -1;
	}
	return samplePacket;
}

int AccompanyDecoder::readSamples(short* samples, int size) {
	if(seek_req){
        // 如果有seek请求，则将指针移动到当前已读取的size处，强制下一次执行去readFrame
		audioBufferCursor = audioBufferSize;
	}
	int sampleSize = size;
	while(size > 0){
        
		if(audioBufferCursor < audioBufferSize){
            // 如果存在读取的帧数据，走这个分支
            // 获取数据大小
			int audioBufferDataSize = audioBufferSize - audioBufferCursor;
            // 取剩余数据和请求的数据的最小值去拷贝
			int copySize = MIN(size, audioBufferDataSize);
            
			memcpy(samples + (sampleSize - size), audioBuffer + audioBufferCursor, copySize * 2);
			size -= copySize;
			audioBufferCursor += copySize;
		} else{
            // 如果没有未处理的帧数据，则走这个分支
			if(readFrame() < 0){
                // 如果读取失败则break
				break;
			}
		}
//		LOGI("size is %d", size);
	}
	int fillSize = sampleSize - size;
	if(fillSize == 0){
		return -1;
	}
	return fillSize;
}


void AccompanyDecoder::seek_frame(){
	LOGI("\n seek frame firstFrameCorrectionInSecs is %.6f, seek_seconds=%f, position=%f \n", firstFrameCorrectionInSecs, seek_seconds, position);
	float targetPosition = seek_seconds;
	float currentPosition = position;
	float frameDuration = duration;
	if(targetPosition < currentPosition){
        // 如果要往前退，则直接重新创建，从起始点往后找位置
		this->destroy();
		this->init(accompanyFilePath);
		//TODO:这里GT的测试样本会差距25ms 不会累加
		currentPosition = 0.0;
	}
	int readFrameCode = -1;
	while (true) {
        // 初始化音频包
		av_init_packet(&packet);
        // 读取帧
		readFrameCode = av_read_frame(avFormatContext, &packet);
        // 如果读取到了帧，则累加帧的持续时长，直到超过目标seek时间为止
		if (readFrameCode >= 0) {
			currentPosition += frameDuration;
			if (currentPosition >= targetPosition) {
				break;
			}
		}
//		LOGI("currentPosition is %.3f", currentPosition);
        // 释放音频包
		av_free_packet(&packet);
	}
	seek_resp = true;
	seek_req = false;
	seek_success_read_frame_success = false;
}

int AccompanyDecoder::readFrame() {
//	LOGI("enter AccompanyDecoder::readFrame");
	if(seek_req){
		this->seek_frame();
	}
	int ret = 1;
    // 初始化一个数据包
	av_init_packet(&packet);
    // 用于记录是否获取到了帧数据
	int gotframe = 0;
    // 用于记录读取帧是否发生了错误
	int readFrameCode = -1;
	while (true) {
        // 从上下文读取一帧数据存入到数据包里
		readFrameCode = av_read_frame(avFormatContext, &packet);
//		LOGI(" av_read_frame ...", duration);
		if (readFrameCode >= 0) {
            // 走到这个分支说明读取帧数据成功
			if (packet.stream_index == stream_index) {
                // 走到这个分支说明数据包的流id和期望读取的流id一致
                // 对packet进行解码，将解码后的存入pAudioFrame中，并返回解码的数据长度
				int len = avcodec_decode_audio4(avCodecContext, pAudioFrame,
						&gotframe, &packet);
//				LOGI(" avcodec_decode_audio4 ...", duration);
				if (len < 0) {
					LOGI("decode audio error, skip packet");
				}
				if (gotframe) {
                    // 走到这个分支说明获取到了帧
					int numChannels = OUT_PUT_CHANNELS;
					int numFrames = 0;
					void * audioData;
					if (swrContext) {
                        // 走到这个分支说明需要重采样
						const int ratio = 2;
                        // 根据通道、采用格式、采用频率来获取数据的大小
						const int bufSize = av_samples_get_buffer_size(NULL,
								numChannels, pAudioFrame->nb_samples * ratio,
								AV_SAMPLE_FMT_S16, 1);
                        // 第一次执行到这里时会设置重采样的大小以及开辟空间
						if (!swrBuffer || swrBufferSize < bufSize) {
							swrBufferSize = bufSize;
							swrBuffer = realloc(swrBuffer, swrBufferSize);
						}
                        // 开辟空间
						byte *outbuf[2] = { (byte*) swrBuffer, NULL };
                        // 进行重采样转换，将转换后的结果记录在outbuf的空间里，并返回帧的数量
						numFrames = swr_convert(swrContext, outbuf,
								pAudioFrame->nb_samples * ratio,
								(const uint8_t **) pAudioFrame->data,
								pAudioFrame->nb_samples);
						if (numFrames < 0) {
							LOGI("fail resample audio");
							ret = -1;
							break;
						}
                        // 记录重采样的数据
						audioData = swrBuffer;
					} else {
                        // 走到这个分支说明不需要重采样
						if (avCodecContext->sample_fmt != AV_SAMPLE_FMT_S16) {
							LOGI("bucheck, audio format is invalid");
							ret = -1;
							break;
						}
                        // 直接获取音频数据和帧数量
						audioData = pAudioFrame->data[0];
						numFrames = pAudioFrame->nb_samples;
					}
					if(isNeedFirstFrameCorrectFlag && position >= 0){
                        // 如果需要修正第一帧且位置大于等于0会走这个分支（position默认是-1，第一次不会走这里）
                        // 计算期望的位置
						float expectedPosition = position + duration;
                        // 获取帧的最可能所在的时间戳，并乘以时基，计算出最有可能在的位置
						float actualPosition = av_frame_get_best_effort_timestamp(pAudioFrame) * timeBase;
                        // 计算差值
						firstFrameCorrectionInSecs = actualPosition - expectedPosition;
                        // 去除首帧修复的标记
						isNeedFirstFrameCorrectFlag = false;
					}
                    // 获取当前帧的持续时间
					duration = av_frame_get_pkt_duration(pAudioFrame) * timeBase;
                    // 获取帧最可能所在的恶时间戳，并修正误差
					position = av_frame_get_best_effort_timestamp(pAudioFrame) * timeBase - firstFrameCorrectionInSecs;
					if (!seek_success_read_frame_success) {
                        // 如果没标记seek后readFrame成功，则标记一下，并记录实际seek的位置
						LOGI("position is %.6f", position);
						actualSeekPosition = position;
						seek_success_read_frame_success = true;
					}
                    // 获取数据大小
					audioBufferSize = numFrames * numChannels;
//					LOGI(" \n duration is %.6f position is %.6f audioBufferSize is %d\n", duration, position, audioBufferSize);
                    // 获取数据
					audioBuffer = (short*) audioData;
                    // 重置指针
					audioBufferCursor = 0;
					break;
				}
			}
		} else {
            // 走到这个分支说明读取帧数据失败，记录结果为失败并退出循环
			ret = -1;
			break;
		}
	}
    // 释放数据包空间
	av_free_packet(&packet);
    // 返回执行结果
	return ret;
}

/// 销毁
void AccompanyDecoder::destroy() {
//	LOGI("start destroy!!!");
	if (NULL != swrBuffer) {
		free(swrBuffer);
		swrBuffer = NULL;
		swrBufferSize = 0;
	}
	if (NULL != swrContext) {
		swr_free(&swrContext);
		swrContext = NULL;
	}
	if (NULL != pAudioFrame) {
		av_free (pAudioFrame);
		pAudioFrame = NULL;
	}
	if (NULL != avCodecContext) {
		avcodec_close(avCodecContext);
		avCodecContext = NULL;
	}
	if (NULL != avFormatContext) {
		LOGI("leave LiveReceiver::destory");
		avformat_close_input(&avFormatContext);
		avFormatContext = NULL;
	}
//	LOGI("end destroy!!!");
}
