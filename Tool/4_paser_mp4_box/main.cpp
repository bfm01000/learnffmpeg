#include <iostream>
#include <fstream>
#include <string>
#include <arpa/inet.h>
#include <vector>

// 简单的 64 位大端转主机字节序函数（如果环境没有 be64toh）
uint64_t ntohll_custom(uint64_t val) {
    return ((uint64_t)ntohl(val & 0xFFFFFFFF) << 32) | ntohl(val >> 32);
}

struct Box {
    uint64_t size; // 改为 64 位以兼容 largesize
    std::string type;
    uint64_t header_size; // 记录头部占用了多少字节 (8 或 16)
};

Box parser_box(std::ifstream& file) {
    Box box;
    uint32_t size32 = 0;
    
    // 1. 读取 4 字节 size
    if (!file.read(reinterpret_cast<char*>(&size32), 4)) return {0, "EOF", 0};
    box.size = ntohl(size32);
    box.header_size = 8;

    // 2. 读取 4 字节 type
    char type[5] = {0};
    file.read(type, 4);
    box.type = std::string(type, 4);

    // 3. 处理 size == 1 的情况 (largesize)
    if (box.size == 1) {
        uint64_t size64 = 0;
        file.read(reinterpret_cast<char*>(&size64), 8);
        box.size = ntohll_custom(size64);
        box.header_size = 16;
        std::cout << "Large size box found: " << box.size << std::endl;
    } 
    // 处理 size == 0 的情况（通常是文件末尾的 mdat）
    else if (box.size == 0) {
        // 实际逻辑应计算到文件末尾，这里暂时简化处理
        std::cout << "mdat box found" << std::endl;
    }

    return box;
}

int main() {
    std::ifstream file("../source/cat.mp4", std::ios::binary);
    if (!file) {
        std::cerr << "Open file failed!" << std::endl;
        return -1;
    }

    while (file.peek() != EOF) {
        Box box = parser_box(file);
        if (box.type == "EOF") break;

        std::cout << "Found Box: [" << box.type << "] Size: " << box.size << std::endl;

        // 关键逻辑：如果是容器类 Box (如 moov, trak)，我们不跳过，继续向下读
        if (box.type == "moov" || box.type == "trak" || box.type == "mdia" || box.type == "minf" || box.type == "stbl") {
            std::cout << "Entering container box: " << box.type << std::endl;
            // 保持当前文件指针，进入下一层循环解析子 Box
            continue; 
        } else {
            // 如果不是容器（如 ftyp, mdat, mvhd），则跳过其数据部分
            if (box.size >= box.header_size) {
                file.seekg(box.size - box.header_size, std::ios::cur);
            }
        }
    }

    return 0;
}