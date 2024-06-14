#include "accompany_decoder_controller.h"

#define LOG_TAG "AccompanyDecoderController"

/// 初始化解码控制器
AccompanyDecoderController::AccompanyDecoderController() {
    // 解码器置空
	accompanyDecoder = NULL;
    // 解码文件置空
	pcmFile = NULL;
}

AccompanyDecoderController::~AccompanyDecoderController() {
}

/// 初始化
/// - Parameters:
///   - accompanyPath: 源文件目录
///   - pcmFilePath: 解码文件目录
void AccompanyDecoderController::Init(const char* accompanyPath, const char* pcmFilePath) {
	// 初始化临时的解码器
	AccompanyDecoder* tempDecoder = new AccompanyDecoder();
    // 声明存放采样率和比特率的数组
	int accompanyMetaData[2];
    // 获取采样率和比特率
	tempDecoder->getMusicMeta(accompanyPath, accompanyMetaData);
	// 销毁临时解码器
	delete tempDecoder;
	//初始化伴奏的采样率
	accompanySampleRate = accompanyMetaData[0];
	// 计算每秒字节数= 采样率*通道数*每个通道的位数/每个字节的位数
	int accompanyByteCountPerSec = accompanySampleRate * CHANNEL_PER_FRAME * BITS_PER_CHANNEL / BITS_PER_BYTE;
	accompanyPacketBufferSize = (int) ((accompanyByteCountPerSec / 2) * 0.2);
	// 初始化解码器
	accompanyDecoder = new AccompanyDecoder();
	// 通过文件初始化解码器并设置包大小
	accompanyDecoder->init(accompanyPath, accompanyPacketBufferSize);
	// 打开pcm文件地址，准备写入
	pcmFile = fopen(pcmFilePath, "wb+");
}

void AccompanyDecoderController::Decode() {
	while(true) {
	// 解码器中读取一个音频包
		AudioPacket* accompanyPacket = accompanyDecoder->decodePacket();
		// 如果是无效的包则结束任务
		if(-1 == accompanyPacket->size) {
			break;
		}
		// 将包内容写入pcm文件
		fwrite(accompanyPacket->buffer, sizeof(short), accompanyPacket->size, pcmFile);
	}
}

void AccompanyDecoderController::Destroy() {
// 销毁解码器
	if (NULL != accompanyDecoder) {
		accompanyDecoder->destroy();
		delete accompanyDecoder;
		accompanyDecoder = NULL;
	}
	// 关闭文件
	if(NULL != pcmFile) {
		fclose(pcmFile);
		pcmFile = NULL;
	}
}
