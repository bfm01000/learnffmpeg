#include <iostream>
#include <fstream>
#include <vector>

int get_nalu_type(unsigned char byte)
{
    return byte & 0x1F;
}

int main()
{
    std::ifstream file("./x5.h264", std::ios::binary);
    if (!file) {
        std::cerr << "open file failed" << std::endl;
        return -1;
    }

    const size_t BUF_SIZE = 4096;

    std::vector<unsigned char> buffer(BUF_SIZE + 4);
    size_t remain = 0;

    while (!file.eof())
    {
        file.read((char*)buffer.data() + remain, BUF_SIZE);
        size_t bytes_read = file.gcount();

        size_t total = remain + bytes_read;

        for (size_t i = 0; i + 3 < total; i++)
        {
            size_t start_code_len = 0;

            // 00 00 01
            if (buffer[i] == 0 &&
                buffer[i+1] == 0 &&
                buffer[i+2] == 1)
            {
                start_code_len = 3;
            }
            // 00 00 00 01
            else if (i + 4 < total &&
                     buffer[i] == 0 &&
                     buffer[i+1] == 0 &&
                     buffer[i+2] == 0 &&
                     buffer[i+3] == 1)
            {
                start_code_len = 4;
            }

            if (start_code_len > 0)
            {
                if (i + start_code_len >= total)
                    break;

                unsigned char header = buffer[i + start_code_len];
                int type = get_nalu_type(header);

                std::cout << "NALU type: " << type << " ";

                switch (type)
                {
                    case 7: std::cout << "(SPS)"; break;
                    case 8: std::cout << "(PPS)"; break;
                    case 5: std::cout << "(IDR Frame)"; break;
                    case 1: std::cout << "(P Frame)"; break;
                    case 6: std::cout << "(SEI)"; break;
                    default: std::cout << "(Other)"; break;
                }

                std::cout << std::endl;

                i += start_code_len - 1;
            }
        }

        // 保留最后3字节，防止start code跨buffer
        if (total >= 3)
        {
            buffer[0] = buffer[total-3];
            buffer[1] = buffer[total-2];
            buffer[2] = buffer[total-1];
            remain = 3;
        }
        else
        {
            remain = total;
        }
    }

    return 0;
}