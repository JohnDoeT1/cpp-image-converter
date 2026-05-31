#include "bmp_image.h"
#include "pack_defines.h"

#include <array>
#include <fstream>
#include <string_view>

using namespace std;

namespace img_lib {

// Заголовок файла BMP - ровно 14 байт
PACKED_STRUCT_BEGIN BitmapFileHeader {
    uint16_t bfType;          // сигнатура 'BM'
    uint32_t bfSize;          // размер файла
    uint16_t bfReserved1;     // зарезервировано (0)
    uint16_t bfReserved2;     // зарезервировано (0)
    uint32_t bfOffBits;       // смещение до данных изображения
}
PACKED_STRUCT_END

// Информационный заголовок BMP - ровно 40 байт
PACKED_STRUCT_BEGIN BitmapInfoHeader {
    uint32_t biSize;          // размер структуры (40)
    int32_t biWidth;          // ширина
    int32_t biHeight;         // высота
    uint16_t biPlanes;        // количество плоскостей (1)
    uint16_t biBitCount;      // бит на пиксель (24)
    uint32_t biCompression;   // тип сжатия (0 - без сжатия)
    uint32_t biSizeImage;     // размер данных изображения
    int32_t biXPelsPerMeter;  // горизонтальное разрешение
    int32_t biYPelsPerMeter;  // вертикальное разрешение
    uint32_t biClrUsed;       // количество используемых цветов
    uint32_t biClrImportant;  // количество важных цветов
}
PACKED_STRUCT_END

// функция вычисления отступа по ширине
static int GetBMPStride(int w) {
    return 4 * ((w * 3 + 3) / 4);
}

bool SaveBMP(const Path& file, const Image& image) {
    if (!image) {
        return false;
    }

    ofstream out(file, ios::binary);
    if (!out) {
        return false;
    }

    const int w = image.GetWidth();
    const int h = image.GetHeight();
    const int stride = GetBMPStride(w);
    const uint32_t data_size = static_cast<uint32_t>(stride * h);
    const uint32_t file_size = sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader) + data_size;

    // Заполняем заголовок файла
    BitmapFileHeader file_header;
    file_header.bfType = 0x4D42; // 'BM' в little-endian
    file_header.bfSize = file_size;
    file_header.bfReserved1 = 0;
    file_header.bfReserved2 = 0;
    file_header.bfOffBits = sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader);

    // Заполняем информационный заголовок
    BitmapInfoHeader info_header;
    info_header.biSize = sizeof(BitmapInfoHeader);
    info_header.biWidth = w;
    info_header.biHeight = h;  // положительная высота - строки снизу вверх
    info_header.biPlanes = 1;
    info_header.biBitCount = 24;
    info_header.biCompression = 0;
    info_header.biSizeImage = data_size;
    info_header.biXPelsPerMeter = 11811;  // 300 DPI
    info_header.biYPelsPerMeter = 11811;  // 300 DPI
    info_header.biClrUsed = 0;
    info_header.biClrImportant = 0x1000000;

    // Записываем заголовки
    out.write(reinterpret_cast<const char*>(&file_header), sizeof(file_header));
    if (!out.good()) {
        return false;
    }

    out.write(reinterpret_cast<const char*>(&info_header), sizeof(info_header));
    if (!out.good()) {
        return false;
    }

    // Подготавливаем буфер для строки с учетом padding
    vector<char> buff(stride);

    // В BMP строки записываются снизу вверх
    for (int y = h - 1; y >= 0; --y) {
        const Color* line = image.GetLine(y);
        
        // Заполняем буфер данными строки
        for (int x = 0; x < w; ++x) {
            // BMP хранит цвета в порядке BGR
            buff[x * 3 + 0] = static_cast<char>(line[x].b);
            buff[x * 3 + 1] = static_cast<char>(line[x].g);
            buff[x * 3 + 2] = static_cast<char>(line[x].r);
        }
        
        // Заполняем padding нулями если нужно
        for (int x = w * 3; x < stride; ++x) {
            buff[x] = 0;
        }
        
        out.write(buff.data(), stride);
        if (!out.good()) {
            return false;
        }
    }

    return out.good();
}

Image LoadBMP(const Path& file) {
    ifstream ifs(file, ios::binary);
    if (!ifs) {
        return {};
    }

    // Читаем заголовок файла
    BitmapFileHeader file_header;
    ifs.read(reinterpret_cast<char*>(&file_header), sizeof(file_header));
    if (!ifs.good() || ifs.gcount() != sizeof(file_header)) {
        return {};
    }

    // Проверяем сигнатуру 'BM'
    if (file_header.bfType != 0x4D42) {
        return {};
    }

    // Читаем информационный заголовок
    BitmapInfoHeader info_header;
    ifs.read(reinterpret_cast<char*>(&info_header), sizeof(info_header));
    if (!ifs.good() || ifs.gcount() != sizeof(info_header)) {
        return {};
    }

    // Проверяем корректность заголовка
    if (info_header.biSize != sizeof(BitmapInfoHeader) ||
        info_header.biPlanes != 1 ||
        info_header.biBitCount != 24 ||
        info_header.biCompression != 0) {
        return {};
    }

    const int w = info_header.biWidth;
    const int h = abs(info_header.biHeight);
    const bool top_down = info_header.biHeight < 0;
    
    if (w <= 0 || h <= 0) {
        return {};
    }

    // Перемещаемся к началу данных изображения
    ifs.seekg(file_header.bfOffBits, ios::beg);
    if (!ifs.good()) {
        return {};
    }

    Image result(w, h, Color::Black());
    const int stride = GetBMPStride(w);
    
    // Временный буфер для строки
    vector<char> buff(stride);

    for (int y = 0; y < h; ++y) {
        Color* line;
        
        // Определяем, в какую строку писать
        if (top_down) {
            line = result.GetLine(y);
        } else {
            line = result.GetLine(h - 1 - y);
        }

        ifs.read(buff.data(), stride);
        if (!ifs.good() || ifs.gcount() != stride) {
            return {};
        }

        for (int x = 0; x < w; ++x) {
            // BMP хранит цвета в порядке BGR
            line[x].b = static_cast<byte>(buff[x * 3 + 0]);
            line[x].g = static_cast<byte>(buff[x * 3 + 1]);
            line[x].r = static_cast<byte>(buff[x * 3 + 2]);
            line[x].a = byte{255};
        }
    }

    return result;
}

}  // namespace img_lib