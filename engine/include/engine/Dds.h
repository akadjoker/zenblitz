#ifndef ZENGL_DDS_H
#define ZENGL_DDS_H

// Leitor de DirectDraw Surface (.dds). O stb_image nao suporta este
// formato; o Pixmap usa isto para o tratar (ver pixmap.cpp:load_from_memory).
//
// Devolve sempre RGBA8 descomprimido, mesmo para os formatos de blocos.

#include <cstddef>
#include <cstdint>

namespace engine
{
namespace dds
{

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

enum class Format
{
    Unsupported,
    BC1,    // DXT1
    BC2,    // DXT3
    BC3,    // DXT5
    RGBA8,  // nao comprimido (qualquer ordem de canais, ja normalizada)
};

struct Image
{
    u8 *pixels = nullptr;   // RGBA8, width*height*4 bytes
    int width = 0;
    int height = 0;
    Format format = Format::Unsupported;  // o formato de origem
};

// Verifica a assinatura "DDS " sem descodificar nada.
bool isDDS(const unsigned char *data, std::size_t size);

// Descodifica o nivel 0 para RGBA8. Devolve false para formatos nao
// suportados (BC4/5/6H/7, cubemaps, volumes, float) em vez de adivinhar.
// Em caso de sucesso o chamador deve libertar com free().
bool load(const unsigned char *data, std::size_t size, Image &out);

void free(Image &image);

// Nome legivel do formato de origem, para diagnostico.
const char *formatName(Format format);

} // namespace dds
} // namespace engine

#endif // ZENGL_DDS_H
