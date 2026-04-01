#include <iostream>
#include <string>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/avutil.h>
}

struct InputOptions {
    std::string input_path;
    std::string output_path;
    int target_width;
    int target_height;
    std::string format;
};



int main(int argc, char **argv) {
    // 1. 解析参数

    // 2. 打开AVFormatContext

    // 3. 选择一个视频流和音频流

    // 4. 初始化解码器
    
    // 5. 初始化编码器

    // 6. 循环读取AVPacket，解码成AVFrame, 转成特定分辨率以及数据格式

    // 7. 送入编码器

    // 8. 循环读取AVFrame，编码成AVPacket，写入文件

    // 9. 释放资源
    return 0;
}