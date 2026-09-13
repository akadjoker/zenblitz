// dds.cpp — leitor de DirectDraw Surface (.dds) para o Pixmap.
//
// O stb_image nao le DDS. Este ficheiro trata o formato e devolve sempre
// pixeis RGBA descomprimidos, para o Pixmap poder manipular/gravar como
// qualquer outra imagem.
//
// Suporta:
//   - BC1 / DXT1  (RGB + 1 bit de alpha)
//   - BC2 / DXT3  (alpha explicito de 4 bits)
//   - BC3 / DXT5  (alpha interpolado)
//   - nao comprimido: RGBA8, BGRA8, RGB8, BGR8
//   - DX10 header, para os mesmos formatos acima
//
// Nao suporta (devolve false, sem adivinhar): BC4/BC5/BC6H/BC7, cubemaps,
// volumes, e formatos de virgula flutuante. Mipmaps sao ignorados - so o
// nivel 0 e' lido.
//
// Os blocos BCn sao 4x4; imagens cujas dimensoes nao sejam multiplas de 4
// tem blocos parciais na borda, e so os texels dentro da imagem sao
// escritos.

#include "engine/Dds.h"

#include <cstring>

namespace engine
{
namespace dds
{

namespace
{

// --- cabecalhos, tal como definidos pela Microsoft ---

constexpr u32 kMagic = 0x20534444u; // "DDS "

constexpr u32 kFourCC_DXT1 = 0x31545844u; // "DXT1"
constexpr u32 kFourCC_DXT2 = 0x32545844u;
constexpr u32 kFourCC_DXT3 = 0x33545844u;
constexpr u32 kFourCC_DXT4 = 0x34545844u;
constexpr u32 kFourCC_DXT5 = 0x35545844u;
constexpr u32 kFourCC_DX10 = 0x30315844u; // "DX10"

// DDS_PIXELFORMAT.dwFlags
constexpr u32 kPfFourCC = 0x4u;
constexpr u32 kPfRGB    = 0x40u;
constexpr u32 kPfAlpha  = 0x1u;

// DXGI_FORMAT, so os que tratamos
constexpr u32 kDXGI_R8G8B8A8_UNORM      = 28u;
constexpr u32 kDXGI_R8G8B8A8_UNORM_SRGB = 29u;
constexpr u32 kDXGI_B8G8R8A8_UNORM      = 87u;
constexpr u32 kDXGI_B8G8R8A8_UNORM_SRGB = 91u;
constexpr u32 kDXGI_B8G8R8X8_UNORM      = 88u;
constexpr u32 kDXGI_BC1_UNORM           = 71u;
constexpr u32 kDXGI_BC1_UNORM_SRGB      = 72u;
constexpr u32 kDXGI_BC2_UNORM           = 74u;
constexpr u32 kDXGI_BC2_UNORM_SRGB      = 75u;
constexpr u32 kDXGI_BC3_UNORM           = 77u;
constexpr u32 kDXGI_BC3_UNORM_SRGB      = 78u;

struct PixelFormat
{
    u32 size, flags, fourCC;
    u32 rgbBitCount, rMask, gMask, bMask, aMask;
};

struct Header
{
    u32 size, flags, height, width, pitchOrLinearSize, depth, mipMapCount;
    u32 reserved1[11];
    PixelFormat pf;
    u32 caps, caps2, caps3, caps4, reserved2;
};

// Leitura little-endian explicita: o DDS e' sempre LE, independentemente
// da maquina.
inline u32 readU32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

void readHeader(const u8 *p, Header &h)
{
    h.size              = readU32(p + 0);
    h.flags             = readU32(p + 4);
    h.height            = readU32(p + 8);
    h.width             = readU32(p + 12);
    h.pitchOrLinearSize = readU32(p + 16);
    h.depth             = readU32(p + 20);
    h.mipMapCount       = readU32(p + 24);
    for (int i = 0; i < 11; i++)
        h.reserved1[i] = readU32(p + 28 + i * 4);
    const u8 *pf = p + 72;
    h.pf.size        = readU32(pf + 0);
    h.pf.flags       = readU32(pf + 4);
    h.pf.fourCC      = readU32(pf + 8);
    h.pf.rgbBitCount = readU32(pf + 12);
    h.pf.rMask       = readU32(pf + 16);
    h.pf.gMask       = readU32(pf + 20);
    h.pf.bMask       = readU32(pf + 24);
    h.pf.aMask       = readU32(pf + 28);
    h.caps  = readU32(p + 104);
    h.caps2 = readU32(p + 108);
}

// --- descompressao de blocos BCn ---
//
// Cada bloco cobre 4x4 texels. Os dois primeiros u16 sao cores de
// referencia em RGB565; os 32 bits seguintes sao 2 bits por texel a
// escolher entre as 4 cores da paleta (as duas de referencia e duas
// interpoladas).

inline void rgb565(u16 c, u8 &r, u8 &g, u8 &b)
{
    // Expandir para 8 bits replicando os bits altos, para o branco dar
    // mesmo 255 (e nao 248).
    const u8 r5 = (u8)((c >> 11) & 0x1F);
    const u8 g6 = (u8)((c >> 5) & 0x3F);
    const u8 b5 = (u8)(c & 0x1F);
    r = (u8)((r5 << 3) | (r5 >> 2));
    g = (u8)((g6 << 2) | (g6 >> 4));
    b = (u8)((b5 << 3) | (b5 >> 2));
}

// Escreve os 16 texels de um bloco de cor. `hasAlphaFromC0C1` distingue o
// modo do BC1: quando c0 <= c1 o bloco tem 3 cores + 1 transparente.
void decodeColorBlock(const u8 *block, u8 out[16][4], bool bc1Punchthrough)
{
    const u16 c0 = (u16)(block[0] | (block[1] << 8));
    const u16 c1 = (u16)(block[2] | (block[3] << 8));
    const u32 bits = readU32(block + 4);

    u8 palette[4][4];
    rgb565(c0, palette[0][0], palette[0][1], palette[0][2]);
    palette[0][3] = 255;
    rgb565(c1, palette[1][0], palette[1][1], palette[1][2]);
    palette[1][3] = 255;

    if (!bc1Punchthrough || c0 > c1)
    {
        // 4 cores opacas: 2/3 e 1/3 entre as de referencia.
        for (int i = 0; i < 3; i++)
        {
            palette[2][i] = (u8)((2 * palette[0][i] + palette[1][i]) / 3);
            palette[3][i] = (u8)((palette[0][i] + 2 * palette[1][i]) / 3);
        }
        palette[2][3] = 255;
        palette[3][3] = 255;
    }
    else
    {
        // 3 cores + transparente.
        for (int i = 0; i < 3; i++)
        {
            palette[2][i] = (u8)((palette[0][i] + palette[1][i]) / 2);
            palette[3][i] = 0;
        }
        palette[2][3] = 255;
        palette[3][3] = 0;
    }

    for (int i = 0; i < 16; i++)
    {
        const u32 index = (bits >> (i * 2)) & 0x3u;
        out[i][0] = palette[index][0];
        out[i][1] = palette[index][1];
        out[i][2] = palette[index][2];
        out[i][3] = palette[index][3];
    }
}

// BC2: 4 bits de alpha por texel, sem interpolacao.
void decodeAlphaBC2(const u8 *block, u8 out[16][4])
{
    for (int i = 0; i < 16; i++)
    {
        const u8 nibble = (i & 1) ? (u8)(block[i / 2] >> 4) : (u8)(block[i / 2] & 0x0F);
        out[i][3] = (u8)((nibble << 4) | nibble); // 0..15 -> 0..255
    }
}

// BC3: dois alphas de referencia + 3 bits por texel a indexar 8 valores.
void decodeAlphaBC3(const u8 *block, u8 out[16][4])
{
    const u8 a0 = block[0];
    const u8 a1 = block[1];

    u8 alpha[8];
    alpha[0] = a0;
    alpha[1] = a1;
    if (a0 > a1)
    {
        for (int i = 0; i < 6; i++)
            alpha[2 + i] = (u8)(((6 - i) * a0 + (1 + i) * a1) / 7);
    }
    else
    {
        for (int i = 0; i < 4; i++)
            alpha[2 + i] = (u8)(((4 - i) * a0 + (1 + i) * a1) / 5);
        alpha[6] = 0;
        alpha[7] = 255;
    }

    // 16 indices de 3 bits = 48 bits, guardados nos 6 bytes seguintes.
    u64 bits = 0;
    for (int i = 0; i < 6; i++)
        bits |= (u64)block[2 + i] << (i * 8);

    for (int i = 0; i < 16; i++)
        out[i][3] = alpha[(bits >> (i * 3)) & 0x7u];
}

// Copia um bloco 4x4 ja descodificado para a imagem, respeitando bordas.
void blitBlock(const u8 block[16][4], u8 *dst, int width, int height, int bx, int by)
{
    for (int row = 0; row < 4; row++)
    {
        const int y = by + row;
        if (y >= height)
            break;
        for (int col = 0; col < 4; col++)
        {
            const int x = bx + col;
            if (x >= width)
                continue;
            const u8 *src = block[row * 4 + col];
            u8 *out = dst + ((size_t)y * (size_t)width + (size_t)x) * 4;
            out[0] = src[0];
            out[1] = src[1];
            out[2] = src[2];
            out[3] = src[3];
        }
    }
}

bool decodeCompressed(const u8 *data, size_t size, int width, int height,
                       Format format, u8 *out)
{
    const int blocksX = (width + 3) / 4;
    const int blocksY = (height + 3) / 4;
    const size_t blockBytes = (format == Format::BC1) ? 8u : 16u;
    const size_t needed = (size_t)blocksX * (size_t)blocksY * blockBytes;
    if (size < needed)
        return false;

    for (int by = 0; by < blocksY; by++)
    {
        for (int bx = 0; bx < blocksX; bx++)
        {
            const u8 *block = data + ((size_t)by * blocksX + bx) * blockBytes;
            u8 texels[16][4];

            switch (format)
            {
            case Format::BC1:
                decodeColorBlock(block, texels, true);
                break;
            case Format::BC2:
                decodeColorBlock(block + 8, texels, false);
                decodeAlphaBC2(block, texels);
                break;
            case Format::BC3:
                decodeColorBlock(block + 8, texels, false);
                decodeAlphaBC3(block, texels);
                break;
            default:
                return false;
            }

            blitBlock(texels, out, width, height, bx * 4, by * 4);
        }
    }
    return true;
}

// Nao comprimido: reordenar canais conforme as mascaras do header.
bool decodeUncompressed(const u8 *data, size_t size, int width, int height,
                         const PixelFormat &pf, u8 *out)
{
    const int bpp = (int)(pf.rgbBitCount / 8);
    if (bpp != 3 && bpp != 4)
        return false;

    const size_t needed = (size_t)width * (size_t)height * (size_t)bpp;
    if (size < needed)
        return false;

    // Descobrir a posicao de cada canal a partir da mascara.
    auto shiftOf = [](u32 mask) -> int {
        if (mask == 0) return -1;
        int shift = 0;
        while (((mask >> shift) & 1u) == 0)
            shift++;
        return shift;
    };

    const int rs = shiftOf(pf.rMask);
    const int gs = shiftOf(pf.gMask);
    const int bs = shiftOf(pf.bMask);
    const int as = shiftOf(pf.aMask);

    for (int i = 0; i < width * height; i++)
    {
        const u8 *src = data + (size_t)i * (size_t)bpp;
        u32 texel = 0;
        for (int b = 0; b < bpp; b++)
            texel |= (u32)src[b] << (b * 8);

        u8 *dst = out + (size_t)i * 4;
        dst[0] = (rs >= 0) ? (u8)((texel & pf.rMask) >> rs) : 0;
        dst[1] = (gs >= 0) ? (u8)((texel & pf.gMask) >> gs) : 0;
        dst[2] = (bs >= 0) ? (u8)((texel & pf.bMask) >> bs) : 0;
        dst[3] = (as >= 0) ? (u8)((texel & pf.aMask) >> as) : 255;
    }
    return true;
}

} // namespace

bool isDDS(const unsigned char *data, size_t size)
{
    return data && size >= 4 && readU32(data) == kMagic;
}

const char *formatName(Format format)
{
    switch (format)
    {
    case Format::BC1: return "BC1/DXT1";
    case Format::BC2: return "BC2/DXT3";
    case Format::BC3: return "BC3/DXT5";
    case Format::RGBA8: return "RGBA8";
    default: return "unsupported";
    }
}

bool load(const unsigned char *data, size_t size, Image &out)
{
    out.pixels = nullptr;
    out.width = 0;
    out.height = 0;
    out.format = Format::Unsupported;

    // 4 (magic) + 124 (header)
    if (!isDDS(data, size) || size < 128)
        return false;

    Header header;
    readHeader(data + 4, header);
    if (header.size != 124)
        return false;

    const int width = (int)header.width;
    const int height = (int)header.height;
    if (width <= 0 || height <= 0)
        return false;

    size_t offset = 128;
    Format format = Format::Unsupported;
    bool compressed = false;

    if (header.pf.flags & kPfFourCC)
    {
        switch (header.pf.fourCC)
        {
        case kFourCC_DXT1:
            format = Format::BC1; compressed = true; break;
        case kFourCC_DXT2:
        case kFourCC_DXT3:
            format = Format::BC2; compressed = true; break;
        case kFourCC_DXT4:
        case kFourCC_DXT5:
            format = Format::BC3; compressed = true; break;
        case kFourCC_DX10:
        {
            // DDS_HEADER_DXT10 sao mais 20 bytes.
            if (size < 148)
                return false;
            const u32 dxgi = readU32(data + 128);
            offset = 148;
            switch (dxgi)
            {
            case kDXGI_BC1_UNORM: case kDXGI_BC1_UNORM_SRGB:
                format = Format::BC1; compressed = true; break;
            case kDXGI_BC2_UNORM: case kDXGI_BC2_UNORM_SRGB:
                format = Format::BC2; compressed = true; break;
            case kDXGI_BC3_UNORM: case kDXGI_BC3_UNORM_SRGB:
                format = Format::BC3; compressed = true; break;
            case kDXGI_R8G8B8A8_UNORM: case kDXGI_R8G8B8A8_UNORM_SRGB:
            case kDXGI_B8G8R8A8_UNORM: case kDXGI_B8G8R8A8_UNORM_SRGB:
            case kDXGI_B8G8R8X8_UNORM:
                format = Format::RGBA8; compressed = false; break;
            default:
                return false; // BC4/5/6H/7 e formatos float: nao tratados
            }
            // Para os nao comprimidos do DX10 as mascaras do header antigo
            // nao valem; montamos as do formato indicado.
            if (!compressed)
            {
                const bool bgr = (dxgi == kDXGI_B8G8R8A8_UNORM ||
                                   dxgi == kDXGI_B8G8R8A8_UNORM_SRGB ||
                                   dxgi == kDXGI_B8G8R8X8_UNORM);
                header.pf.rgbBitCount = 32;
                header.pf.rMask = bgr ? 0x00FF0000u : 0x000000FFu;
                header.pf.gMask = 0x0000FF00u;
                header.pf.bMask = bgr ? 0x000000FFu : 0x00FF0000u;
                header.pf.aMask = (dxgi == kDXGI_B8G8R8X8_UNORM) ? 0u : 0xFF000000u;
            }
            break;
        }
        default:
            return false;
        }
    }
    else if (header.pf.flags & (kPfRGB | kPfAlpha))
    {
        format = Format::RGBA8;
        compressed = false;
    }
    else
    {
        return false;
    }

    const size_t pixelCount = (size_t)width * (size_t)height;
    u8 *rgba = new u8[pixelCount * 4];

    const u8 *payload = data + offset;
    const size_t payloadSize = size - offset;

    const bool ok = compressed
        ? decodeCompressed(payload, payloadSize, width, height, format, rgba)
        : decodeUncompressed(payload, payloadSize, width, height, header.pf, rgba);

    if (!ok)
    {
        delete[] rgba;
        return false;
    }

    out.pixels = rgba;
    out.width = width;
    out.height = height;
    out.format = format;
    return true;
}

void free(Image &image)
{
    delete[] image.pixels;
    image.pixels = nullptr;
    image.width = 0;
    image.height = 0;
    image.format = Format::Unsupported;
}

} // namespace dds
} // namespace engine
