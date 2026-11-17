#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <io/pad.h>
#include <tiny3d.h>
#include <libfont.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <pngdec/pngdec.h>
#include <sysmodule/sysmodule.h>

#define JEZZ_LOGO_PATH "/dev_hdd0/game/JEZZ00001/USRDIR/assets/JEZZ_LOGO.PNG"

static const int MENU_COUNT = 6;

static const char *menuItems[MENU_COUNT] = {
    "INICIO", "BUSCAR", "ARTISTAS",
    "ALBUNS", "PLAYLISTS", "AJUSTES"
};

static const char *pageTitles[MENU_COUNT] = {
    "INICIO", "BUSCAR", "ARTISTAS",
    "ALBUNS", "PLAYLISTS", "AJUSTES"
};

static FT_Library g_ftLibrary;
static FT_Face g_ftFace;
static bool g_fontReady = false;
static bool g_boldFontReady = false;

static bool g_pngModuleLoaded = false;

static u32 g_logoOffset = 0;
static u32 g_logoWidth = 0;
static u32 g_logoHeight = 0;
static u32 g_logoPitch = 0;


/* =========================================================
   CARREGAMENTO DA FONTE
   ========================================================= */

static void TTFToBitmap(
    u8 chr,
    u8 *bitmap,
    short *w,
    short *h,
    short *yCorrection
) {
    if (!g_ftFace || !bitmap || !w || !h || !yCorrection) {
        if (w) *w = 0;
        return;
    }

    FT_Set_Pixel_Sizes(
        g_ftFace,
        0,
        (FT_UInt)*h
    );

    if (FT_Load_Char(g_ftFace, (FT_ULong)chr, FT_LOAD_RENDER)) {
        *w = 0;
        return;
    }

    FT_GlyphSlot slot = g_ftFace->glyph;

    const int inputWidth = *w;
    const int inputHeight = *h;

    memset(
        bitmap,
        0,
        (size_t)inputWidth * (size_t)inputHeight
    );

    *yCorrection = (short)(
        inputHeight - 1 - slot->bitmap_top
    );

    const int rows = (int)slot->bitmap.rows;
    const int cols = (int)slot->bitmap.width;
    const int pitch = (int)slot->bitmap.pitch;

    for (int row = 0; row < rows && row < inputHeight; ++row) {

        const unsigned char *src;

        if (pitch >= 0)
            src = slot->bitmap.buffer + row * pitch;
        else
            src = slot->bitmap.buffer +
                (rows - 1 - row) * (-pitch);

        for (int col = 0; col < cols && col < inputWidth; ++col) {
            bitmap[row * inputWidth + col] = src[col];
        }
    }

    *w = (short)(
        ((slot->advance.x + 31) >> 6) +
        ((slot->bitmap_left < 0) ? -slot->bitmap_left : 0)
    );

    *h = (short)slot->bitmap.rows;
}


static bool InitFont(void) {
    if (FT_Init_FreeType(&g_ftLibrary))
        return false;

    void *fontTexture = tiny3d_AllocTexture(16 * 1024 * 1024);
    if (!fontTexture) {
        FT_Done_FreeType(g_ftLibrary);
        return false;
    }

    ResetFont();

    const char *regularPath =
        "/dev_hdd0/game/JEZZ00001/USRDIR/assets/JEZZ_FONT_REGULAR.ttf";

    if (FT_New_Face(g_ftLibrary, regularPath, 0, &g_ftFace)) {
        FT_Done_FreeType(g_ftLibrary);
        g_ftFace = 0;
        return false;
    }

    u8 *fontEnd = AddFontFromTTF(
        (u8 *)fontTexture, 32, 126, 64, 64, TTFToBitmap
    );

    FT_Done_Face(g_ftFace);
    g_ftFace = 0;

    if (!fontEnd) {
        FT_Done_FreeType(g_ftLibrary);
        return false;
    }

    g_fontReady = true;

    const char *boldPath =
        "/dev_hdd0/game/JEZZ00001/USRDIR/assets/JEZZ_FONT.ttf";

    if (!FT_New_Face(g_ftLibrary, boldPath, 0, &g_ftFace)) {
        fontEnd = AddFontFromTTF(
            fontEnd, 32, 126, 64, 64, TTFToBitmap
        );

        FT_Done_Face(g_ftFace);
        g_ftFace = 0;

        g_boldFontReady = (fontEnd != 0);
    }

    FT_Done_FreeType(g_ftLibrary);
    return g_fontReady;
}


/* =========================================================
   LOGO OFICIAL JEZZ - PNG
   ========================================================= */

static bool LoadLogoTexture(void) {

    pngData image = {};

    s32 result = pngLoadFromFile(
        JEZZ_LOGO_PATH,
        &image
    );

    if (
        result != PNGDEC_ERROR_OK ||
        !image.bmp_out ||
        image.width == 0 ||
        image.height == 0 ||
        image.pitch == 0
    ) {

        if (image.bmp_out)
            free(image.bmp_out);

        return false;
    }

    size_t imageBytes =
        (size_t)image.pitch * (size_t)image.height;

    size_t allocatedBytes =
        (imageBytes + 15) & ~(size_t)15;

    void *textureMemory = tiny3d_AllocTexture(
        (u32)allocatedBytes
    );

    if (!textureMemory) {

        free(image.bmp_out);

        return false;
    }

    memcpy(
        textureMemory,
        image.bmp_out,
        imageBytes
    );

    g_logoOffset = tiny3d_TextureOffset(textureMemory);

    g_logoWidth = image.width;
    g_logoHeight = image.height;
    g_logoPitch = image.pitch;

    free(image.bmp_out);

    return g_logoOffset != 0;
}


/* =========================================================
   DESENHO 2D
   ========================================================= */

static void DrawRect(
    float x,
    float y,
    float width,
    float height,
    u32 color,
    float z
) {

    tiny3d_SetPolygon(TINY3D_QUADS);

    tiny3d_VertexPos(x, y, z);
    tiny3d_VertexColor(color);

    tiny3d_VertexPos(x + width, y, z);
    tiny3d_VertexPos(x + width, y + height, z);
    tiny3d_VertexPos(x, y + height, z);

    tiny3d_End();
}


/* =========================================================
   TEXTO PIXELADO PARA A ABERTURA
   ========================================================= */

static const u8 pixelFont[26][7] = {

    {14,17,17,31,17,17,17},
    {30,17,17,30,17,17,30},
    {15,16,16,16,16,16,15},
    {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31},
    {31,16,16,30,16,16,16},
    {15,16,16,23,17,17,15},
    {17,17,17,31,17,17,17},
    {31,4,4,4,4,4,31},
    {7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17},
    {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},
    {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13},
    {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},
    {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10},
    {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},
    {31,1,2,4,8,16,31}
};


static void DrawPixelText(
    const char *text,
    float x,
    float y,
    float scale,
    u32 color
) {

    if (!text)
        return;

    float startX = x;

    while (*text) {

        char c = *text++;

        if (c == ' ') {
            startX += scale * 4.0f;
            continue;
        }

        if (c >= 'a' && c <= 'z')
            c -= 32;

        if (c >= 'A' && c <= 'Z') {

            const u8 *glyph = pixelFont[c - 'A'];

            for (int row = 0; row < 7; ++row) {

                for (int col = 0; col < 5; ++col) {

                    if (glyph[row] & (1 << (4 - col))) {

                        DrawRect(
                            startX + col * scale,
                            y + row * scale,
                            scale,
                            scale,
                            color,
                            20.0f
                        );
                    }
                }
            }
        }

        startX += scale * 6.0f;
    }
}


/* =========================================================
   CONFIGURACAO DE TRANSPARENCIA
   ========================================================= */

static void Setup2D(void) {

    tiny3d_AlphaTest(
        1,
        0x10,
        TINY3D_ALPHA_FUNC_GEQUAL
    );

    tiny3d_BlendFunc(
        1,
        static_cast<blend_src_func>(
            TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA |
            TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA
        ),
        static_cast<blend_dst_func>(
            TINY3D_BLEND_FUNC_DST_RGB_ONE_MINUS_SRC_ALPHA |
            TINY3D_BLEND_FUNC_DST_ALPHA_ZERO
        ),
        static_cast<blend_func>(
            TINY3D_BLEND_RGB_FUNC_ADD |
            TINY3D_BLEND_ALPHA_FUNC_ADD
        )
    );

    tiny3d_Project2D();
}


/* =========================================================
   DESENHO DA LOGO PNG
   ========================================================= */

static void DrawLogoTexture(
    float x,
    float y,
    float width,
    float height,
    u32 color
) {

    if (!g_logoOffset)
        return;

    tiny3d_SetTextureWrap(
        0,
        g_logoOffset,
        g_logoWidth,
        g_logoHeight,
        g_logoPitch,
        TINY3D_TEX_FORMAT_A8R8G8B8,
        TEXTWRAP_CLAMP,
        TEXTWRAP_CLAMP,
        TEXTURE_LINEAR
    );

    tiny3d_SetPolygon(TINY3D_QUADS);

    tiny3d_VertexPos(x, y, 100.0f);
    tiny3d_VertexColor(color);
    tiny3d_VertexTexture(0.0f, 0.0f);

    tiny3d_VertexPos(x + width, y, 100.0f);
    tiny3d_VertexTexture(1.0f, 0.0f);

    tiny3d_VertexPos(x + width, y + height, 100.0f);
    tiny3d_VertexTexture(1.0f, 1.0f);

    tiny3d_VertexPos(x, y + height, 100.0f);
    tiny3d_VertexTexture(0.0f, 1.0f);

    tiny3d_End();
}


/* =========================================================
   ABERTURA ANIMADA
   ========================================================= */

static void DrawBootSplash(float progress) {

    const u32 purple = 0x9560FFFF;
    const u32 white = 0xFFFFFFFF;
    const u32 dark = 0x090811FF;

    tiny3d_Clear(dark, TINY3D_CLEAR_ALL);

    Setup2D();

    DrawRect(
        0, 0, 847, 511,
        dark,
        500.0f
    );

    DrawRect(
        0, 0, 847, 5,
        purple,
        100.0f
    );

    DrawRect(
        0, 506, 847, 5,
        purple,
        100.0f
    );

    DrawRect(
        185, 55, 477, 400,
        0x11101DFF,
        300.0f
    );

    DrawRect(
        185, 55, 477, 2,
        0x49336AFF,
        200.0f
    );

    if (g_logoOffset) {

        float scale = 0.78f + (0.22f * progress);

        float logoW = 360.0f * scale;

        float logoH =
            logoW * (float)g_logoHeight / (float)g_logoWidth;

        float logoX = (848.0f - logoW) / 2.0f;
        float logoY = 225.0f - (logoH / 2.0f);

        DrawRect(
            logoX - 15,
            logoY - 15,
            logoW + 30,
            logoH + 30,
            0x49336A40,
            120.0f
        );

        u32 alpha = (u32)(progress * 255.0f);

        DrawLogoTexture(
            logoX,
            logoY,
            logoW,
            logoH,
            0xFFFFFF00 | alpha
        );

    } else {

        DrawPixelText(
            "JEZZ",
            321,
            205,
            10.0f,
            white
        );
    }

    DrawRect(
        300, 345, 247, 2,
        0x49336AFF,
        100.0f
    );

    DrawPixelText(
        "MUSICA EM TODO LUGAR",
        330,
        365,
        2.0f,
        0xC7C0D8FF
    );

    DrawPixelText(
        "INICIANDO",
        365,
        405,
        2.0f,
        purple
    );

    tiny3d_Flip();
}


static void PlayBootAnimation(void) {

    const int frames = 45;

    for (int frame = 0; frame <= frames; ++frame) {

        float progress =
            (float)frame / (float)frames;

        DrawBootSplash(progress);
    }
}


/* =========================================================
   TEXTO PRINCIPAL
   ========================================================= */

static void DrawText(
    float x,
    float y,
    int width,
    int height,
    u32 color,
    const char *value
) {

    if (!g_fontReady || !value)
        return;

    SetCurrentFont(0);

    SetFontSize(width, height);

    SetFontColor(color, 0x00000000);

    SetFontZ(0.0f);

    DrawString(x, y, (char *)value);
}

static void DrawTextBold(
    float x,
    float y,
    int width,
    int height,
    u32 color,
    const char *value
) {
    if (!g_fontReady || !value)
        return;

    SetCurrentFont(g_boldFontReady ? 1 : 0);
    SetFontSize(width, height);
    SetFontColor(color, 0x00000000);
    SetFontZ(0.0f);
    DrawString(x, y, (char *)value);
}


/* =========================================================
   INTERFACE PRINCIPAL
   ========================================================= */



/* =========================================================
   CAPAS ILUSTRADAS - IDENTIDADE VISUAL JEZZ
   ========================================================= */

static void DrawCoverArt(float x, float y, float size, int variant) {
    u32 base = 0x25183DFF;
    u32 mid = 0x65449AFF;
    u32 light = 0xC49BFFFF;
    u32 accent = 0x9560FFFF;

    if (variant == 1) {
        base = 0x102A35FF;
        mid = 0x286477FF;
        light = 0x8BD2DFFF;
        accent = 0x48A6BFFF;
    } else if (variant == 2) {
        base = 0x351C32FF;
        mid = 0x87466EFF;
        light = 0xE5A2C2FF;
        accent = 0xC66A9AFF;
    } else if (variant == 3) {
        base = 0x302719FF;
        mid = 0x806039FF;
        light = 0xE9C98AFF;
        accent = 0xC18B45FF;
    } else if (variant == 4) {
        base = 0x19263AFF;
        mid = 0x3D5D91FF;
        light = 0xA9C8FFFF;
        accent = 0x658BD0FF;
    }

    DrawRect(x, y, size, size, base, 280.0f);
    DrawRect(x + size * 0.08f, y + size * 0.08f,
             size * 0.84f, size * 0.84f, mid, 220.0f);

    // Blocos geometricos criam uma capa diferente sem depender
    // de imagens externas ou recursos ainda nao existentes.
    DrawRect(x + size * 0.15f, y + size * 0.15f,
             size * 0.70f, size * 0.12f, accent, 180.0f);
    DrawRect(x + size * 0.15f, y + size * 0.31f,
             size * 0.22f, size * 0.53f, base, 170.0f);
    DrawRect(x + size * 0.43f, y + size * 0.31f,
             size * 0.42f, size * 0.10f, light, 160.0f);
    DrawRect(x + size * 0.43f, y + size * 0.48f,
             size * 0.42f, size * 0.10f, accent, 160.0f);
    DrawRect(x + size * 0.43f, y + size * 0.65f,
             size * 0.28f, size * 0.10f, light, 160.0f);

    // Pequena assinatura visual no canto inferior.
    DrawRect(x + size * 0.15f, y + size * 0.83f,
             size * 0.70f, size * 0.035f, light, 140.0f);
}


static void DrawPlayerBar(u32 purple, u32 white, u32 grey) {
    DrawRect(0, 447, 848, 65, 0x11101AFF, 50000.0f);
    DrawRect(0, 447, 848, 2, 0x49336AFF, 100.0f);
    DrawRect(0, 448, 848, 1, 0x211A30FF, 90.0f);

    DrawCoverArt(18, 458, 43, 0);

    DrawTextBold(73, 458, 10, 18, white, "NADA TOCANDO");
    DrawText(73, 482, 8, 14, grey, "ESCOLHA UMA FAIXA");

    // Controles centrais do player.
    DrawText(365, 462, 10, 17, grey, "<<");
    DrawRect(425, 457, 34, 29, 0x302044FF, 120.0f);
    DrawTextBold(432, 463, 9, 16, white, ">");
    DrawText(480, 462, 10, 17, grey, ">>");

    DrawRect(355, 495, 250, 3, 0x393344FF, 150.0f);
    DrawRect(355, 495, 3, 3, purple, 100.0f);

    DrawText(720, 459, 8, 14, grey, "VOLUME");
    DrawRect(720, 482, 92, 4, 0x393344FF, 150.0f);
    DrawRect(720, 482, 34, 4, purple, 100.0f);
    DrawRect(751, 478, 8, 12, white, 90.0f);
}


static void DrawSidebar(int selected, u32 purple, u32 white, u32 grey) {
    DrawRect(0, 62, 176, 385, 0x0D0B15FF, 50000.0f);
    DrawRect(175, 62, 1, 385, 0x292337FF, 40000.0f);

    DrawText(22, 83, 9, 15, grey, "SUA BIBLIOTECA");

    for (int i = 0; i < MENU_COUNT; ++i) {
        float y = 111.0f + (float)i * 46.0f;

        if (i == selected) {
            DrawRect(12, y, 151, 37, 0x302044FF, 180.0f);
            DrawRect(12, y, 3, 37, purple, 100.0f);
            DrawRect(157, y + 8, 2, 21, 0xB18AEAFF, 90.0f);
        }

        DrawTextBold(
            29,
            y + 9,
            10,
            18,
            (i == selected) ? white : grey,
            menuItems[i]
        );
    }

    DrawRect(20, 393, 135, 1, 0x292337FF, 300.0f);
    DrawText(22, 405, 8, 14, purple, "JEZZ v0.4");
    DrawText(22, 425, 7, 12, grey, "PREMIUM EDITION");
}


static void DrawScreen(int selected, int page) {
    const u32 purple = 0x9560FFFF;
    const u32 white = 0xFFFFFFFF;
    const u32 grey = 0xAAA5B8FF;
    const u32 background = 0x08070DFF;

    // Fundo, topo e identidade visual.
    DrawRect(0, 0, 848, 512, background, 65535.0f);
    DrawRect(0, 0, 848, 62, 0x11101AFF, 50000.0f);
    DrawRect(0, 61, 848, 1, 0x292337FF, 40000.0f);

    // Marca JEZZ no topo.
    DrawRect(23, 20, 4, 24, purple, 100.0f);
    DrawRect(32, 13, 4, 31, purple, 100.0f);
    DrawRect(41, 18, 4, 26, purple, 100.0f);
    DrawRect(50, 25, 4, 19, purple, 100.0f);
    DrawTextBold(67, 11, 26, 40, white, "JEZZ");
    DrawText(174, 25, 8, 15, grey, "MUSIC");

    DrawRect(696, 17, 130, 29, 0x1B1726FF, 200.0f);
    DrawRect(708, 28, 5, 5, 0x7DDBA4FF, 100.0f);
    DrawText(721, 25, 8, 14, grey, "PS3 EDITION");

    DrawSidebar(selected, purple, white, grey);

    // Conteudo principal.
    if (page == 0) {
        DrawText(196, 76, 9, 16, purple, "BEM-VINDO DE VOLTA");
        DrawTextBold(196, 94, 19, 30, white, "O QUE VAMOS OUVIR HOJE?");

        // Banner principal de destaque.
        DrawRect(194, 135, 632, 137, 0x171222FF, 320.0f);
        DrawRect(194, 135, 4, 137, purple, 100.0f);
        DrawRect(198, 136, 624, 1, 0x49336AFF, 180.0f);

        DrawText(218, 151, 9, 16, 0xC49BFFFF, "DESTAQUE JEZZ");
        DrawTextBold(218, 177, 18, 29, white, "DESCUBRA SEU PROXIMO SOM");
        DrawText(218, 211, 8, 14, grey, "Sua proxima descoberta comeca aqui.");
        DrawRect(218, 235, 139, 25, 0x65449AFF, 120.0f);
        DrawTextBold(231, 241, 8, 14, white, "EXPLORAR AGORA");

        // Arte abstrata principal.
        DrawRect(675, 145, 126, 116, 0x261A3BFF, 250.0f);
        DrawCoverArt(688, 151, 104, 0);
        DrawRect(668, 153, 2, 94, 0xC49BFFFF, 110.0f);

        // Pequenas barras de equalizador como detalhe.
        DrawRect(648, 226, 4, 22, 0x9560FFFF, 100.0f);
        DrawRect(655, 214, 4, 34, 0xB18AEAFF, 100.0f);
        DrawRect(662, 220, 4, 28, 0x65449AFF, 100.0f);

        // Secao de atalhos.
        DrawTextBold(196, 285, 13, 21, white, "EXPLORE SUA MUSICA");
        DrawText(754, 290, 7, 13, purple, "VER MAIS");

        // Card 1.
        DrawRect(194, 316, 202, 109, 0x15121FFF, 320.0f);
        DrawRect(194, 316, 202, 1, 0x302442FF, 180.0f);
        DrawCoverArt(207, 329, 82, 0);
        DrawTextBold(300, 342, 10, 18, white, "MUSICAS");
        DrawText(300, 366, 8, 14, grey, "Sua colecao");
        DrawText(300, 394, 7, 12, purple, "BIBLIOTECA");

        // Card 2.
        DrawRect(407, 316, 202, 109, 0x15121FFF, 320.0f);
        DrawRect(407, 316, 202, 1, 0x263D49FF, 180.0f);
        DrawCoverArt(420, 329, 82, 1);
        DrawTextBold(513, 342, 10, 18, white, "ARTISTAS");
        DrawText(513, 366, 8, 14, grey, "Seus favoritos");
        DrawText(513, 394, 7, 12, 0x70C6D8FF, "DESCUBRA");

        // Card 3.
        DrawRect(620, 316, 206, 109, 0x15121FFF, 320.0f);
        DrawRect(620, 316, 206, 1, 0x493047FF, 180.0f);
        DrawCoverArt(633, 329, 82, 2);
        DrawTextBold(726, 342, 10, 18, white, "PLAYLISTS");
        DrawText(726, 366, 8, 14, grey, "Seus momentos");
        DrawText(726, 394, 7, 12, 0xD58BAEFF, "COLECOES");

        DrawText(196, 431, 7, 12, 0x827B91FF,
                 "DESENVOLVIDO POR MARCELO GUILHERME");

    } else {
        // Cabecalho das paginas internas.
        DrawText(196, 78, 9, 15, purple, "SUA BIBLIOTECA");
        DrawTextBold(196, 98, 23, 37, white, pageTitles[page]);

        DrawRect(194, 151, 632, 274, 0x15121FFF, 320.0f);
        DrawRect(194, 151, 4, 274, purple, 100.0f);
        DrawRect(199, 152, 626, 1, 0x302442FF, 180.0f);

        if (page == 1) {
            DrawTextBold(224, 178, 15, 25, white, "ENCONTRE SUA PROXIMA MUSICA");
            DrawText(224, 210, 9, 15, grey, "BUSQUE POR TITULO, ARTISTA OU ALBUM");
            DrawRect(224, 245, 548, 49, 0x242033FF, 180.0f);
            DrawRect(224, 245, 3, 49, purple, 100.0f);
            DrawText(244, 260, 10, 18, grey, "DIGITE O NOME DE UMA MUSICA OU ARTISTA");
            DrawText(224, 322, 9, 15, purple, "BUSCA DE MUSICA SERA INTEGRADA NAS PROXIMAS ETAPAS.");

        } else if (page == 2) {
            DrawTextBold(224, 180, 16, 27, white, "SEUS ARTISTAS FAVORITOS");
            DrawText(224, 220, 10, 17, grey, "Quando sua biblioteca estiver conectada, seus artistas");
            DrawText(224, 243, 10, 17, grey, "aparecerao organizados aqui.");
            DrawCoverArt(224, 293, 72, 1);
            DrawCoverArt(309, 293, 72, 2);
            DrawCoverArt(394, 293, 72, 3);
            DrawCoverArt(479, 293, 72, 4);

        } else if (page == 3) {
            DrawTextBold(224, 180, 16, 27, white, "SUA COLECAO DE ALBUNS");
            DrawText(224, 220, 10, 17, grey, "Capas, lancamentos e albuns salvos em um so lugar.");
            DrawCoverArt(224, 270, 82, 0);
            DrawCoverArt(321, 270, 82, 1);
            DrawCoverArt(418, 270, 82, 2);
            DrawCoverArt(515, 270, 82, 3);
            DrawCoverArt(612, 270, 82, 4);

        } else if (page == 4) {
            DrawTextBold(224, 180, 16, 27, white, "PLAYLISTS PARA CADA MOMENTO");
            DrawText(224, 220, 10, 17, grey, "Organize suas musicas do seu jeito.");
            DrawCoverArt(224, 270, 82, 2);
            DrawCoverArt(321, 270, 82, 4);
            DrawCoverArt(418, 270, 82, 0);
            DrawCoverArt(515, 270, 82, 3);
            DrawCoverArt(612, 270, 82, 1);

        } else if (page == 5) {
            DrawTextBold(224, 180, 16, 27, white, "PERSONALIZE SUA EXPERIENCIA");
            DrawText(224, 225, 10, 17, white, "JEZZ - PREMIUM EDITION");
            DrawText(224, 265, 10, 17, grey, "CIMA / BAIXO: NAVEGAR PELO MENU");
            DrawText(224, 296, 10, 17, grey, "X: SELECIONAR");
            DrawText(224, 327, 10, 17, grey, "O: VOLTAR PARA INICIO");
            DrawText(224, 358, 10, 17, grey, "TRIANGULO: SAIR DO APLICATIVO");
        }
    }

    DrawPlayerBar(purple, white, grey);
}


int main(int argc, const char *argv[]) {

    (void)argc;
    (void)argv;

    if (tiny3d_Init(1024 * 1024) != TINY3D_OK)
        return 1;
    // Ajuste da area virtual para a tela da TV.
    tiny3d_UserViewport(
        1, 0.0f, 0.0f,
        (float)Video_Resolution.width / 848.0f,
        (float)Video_Resolution.height / 512.0f,
        (float)Video_Resolution.width / 1920.0f,
        (float)Video_Resolution.height / 1080.0f
    );

    ioPadInit(7);

    /* Carrega o decodificador PNG do sistema. */

    if (sysModuleLoad(SYSMODULE_PNGDEC) == 0) {

        g_pngModuleLoaded = true;

        LoadLogoTexture();
    }

    /* Abertura animada com a logo oficial. */

    PlayBootAnimation();

    /* A fonte é carregada depois da abertura. */

    InitFont();

    int selected = 0;
    int page = 0;

    bool previousUp = false;
    bool previousDown = false;
    bool previousCross = false;
    bool previousCircle = false;
    bool previousExit = false;

    padInfo padinfo;
    padData pad;

    while (1) {

        memset(&padinfo, 0, sizeof(padinfo));
        memset(&pad, 0, sizeof(pad));

        ioPadGetInfo(&padinfo);

        if (padinfo.status[0])
            ioPadGetData(0, &pad);

        const bool up = pad.BTN_UP != 0;
        const bool down = pad.BTN_DOWN != 0;
        const bool cross = pad.BTN_CROSS != 0;
        const bool circle = pad.BTN_CIRCLE != 0;

        const bool exitButton =
            (pad.BTN_TRIANGLE != 0) ||
            (pad.BTN_START != 0);

        if (up && !previousUp) {

            --selected;

            if (selected < 0)
                selected = MENU_COUNT - 1;
        }

        if (down && !previousDown) {

            ++selected;

            if (selected >= MENU_COUNT)
                selected = 0;
        }

        if (cross && !previousCross)
            page = selected;

        if (circle && !previousCircle) {

            selected = 0;
            page = 0;
        }

        if (exitButton && !previousExit)
            break;

        previousUp = up;
        previousDown = down;
        previousCross = cross;
        previousCircle = circle;
        previousExit = exitButton;

        tiny3d_Clear(
            0x090811FF,
            TINY3D_CLEAR_ALL
        );

        Setup2D();

        DrawScreen(selected, page);

        tiny3d_Flip();
    }

    ioPadEnd();

    if (g_pngModuleLoaded)
        sysModuleUnload(SYSMODULE_PNGDEC);

    return 0;
}
