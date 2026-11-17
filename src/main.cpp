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

    const char *fontPath =
        "/dev_hdd0/game/JEZZ00001/USRDIR/assets/JEZZ_FONT.ttf";

    if (FT_New_Face(g_ftLibrary, fontPath, 0, &g_ftFace)) {

        FT_Done_FreeType(g_ftLibrary);
        g_ftFace = 0;

        return false;
    }

    void *fontTexture = tiny3d_AllocTexture(16 * 1024 * 1024);

    if (!fontTexture) {

        FT_Done_Face(g_ftFace);
        FT_Done_FreeType(g_ftLibrary);

        g_ftFace = 0;

        return false;
    }

    ResetFont();

    u8 *fontEnd = AddFontFromTTF(
        (u8 *)fontTexture,
        32,
        126,
        64,
        64,
        TTFToBitmap
    );

    FT_Done_Face(g_ftFace);
    FT_Done_FreeType(g_ftLibrary);

    g_ftFace = 0;

    g_fontReady = (fontEnd != 0);

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


/* =========================================================
   INTERFACE PRINCIPAL
   ========================================================= */


static void DrawScreen(int selected, int page) {
    const u32 purple = 0x9560FFFF;
    const u32 white = 0xFFFFFFFF;
    const u32 grey = 0xAAA5B8FF;
    const u32 background = 0x08070DFF;

    // Fundo e estrutura principal
    DrawRect(0, 0, 848, 512, background, 65535.0f);
    DrawRect(0, 0, 848, 64, 0x12101BFF, 50000.0f);
    DrawRect(0, 64, 184, 383, 0x0D0B15FF, 50000.0f);
    DrawRect(183, 64, 1, 383, 0x292337FF, 40000.0f);

    // Identidade JEZZ
    DrawRect(24, 20, 4, 25, purple, 100.0f);
    DrawRect(33, 13, 4, 32, purple, 100.0f);
    DrawRect(42, 18, 4, 27, purple, 100.0f);
    DrawRect(51, 25, 4, 20, purple, 100.0f);

    DrawText(68, 12, 26, 40, white, "JEZZ");
    DrawText(112, 25, 9, 16, grey, "MUSIC PLAYER");
    DrawText(730, 25, 9, 16, grey, "PS3 EDITION");

    // Menu lateral
    DrawText(24, 83, 9, 16, grey, "MENU PRINCIPAL");

    for (int i = 0; i < MENU_COUNT; ++i) {
        float y = 111.0f + (float)i * 47.0f;

        if (i == selected) {
            DrawRect(13, y, 157, 38, 0x302044FF, 100.0f);
            DrawRect(13, y, 4, 38, purple, 60.0f);
        }

        DrawText(
            31,
            y + 8,
            12,
            21,
            (i == selected) ? white : grey,
            menuItems[i]
        );
    }

    DrawText(24, 399, 9, 16, purple, "JEZZ v0.3");
    DrawText(24, 421, 8, 14, grey, "MUSIC EXPERIENCE");

    // Conteudo principal
    if (page == 0) {
        DrawText(210, 80, 10, 18, grey, "BEM-VINDO AO JEZZ");
        DrawText(210, 103, 23, 38, white, "O QUE VAMOS OUVIR?");

        // Painel de destaque
        DrawRect(210, 153, 610, 132, 0x1B142AFF, 300.0f);
        DrawRect(210, 153, 5, 132, purple, 100.0f);

        DrawText(239, 169, 10, 18, purple, "SUA EXPERIENCIA MUSICAL");
        DrawText(239, 199, 19, 31, white, "SUA MUSICA. SEU MOMENTO.");
        DrawText(239, 245, 9, 16, grey, "Explore sua biblioteca e descubra novos sons.");

        // Equalizador decorativo
        const float bars[9] = {
            22, 38, 53, 31, 65, 43, 56, 28, 46
        };

        for (int i = 0; i < 9; ++i) {
            float x = 690.0f + (float)i * 13.0f;
            float h = bars[i];
            DrawRect(
                x,
                258.0f - h,
                7,
                h,
                (i % 2 == 0) ? purple : 0x65449AFF,
                120.0f
            );
        }

        DrawText(210, 302, 13, 23, white, "SUA BIBLIOTECA");

        // Card de musicas
        DrawRect(210, 337, 194, 83, 0x171421FF, 300.0f);
        DrawRect(224, 348, 60, 60, 0x39245DFF, 180.0f);
        DrawRect(224, 348, 60, 8, 0xA477DFFF, 120.0f);
        DrawRect(234, 366, 9, 31, 0xC8A5FFFF, 100.0f);
        DrawRect(248, 357, 9, 40, 0x9560FFFF, 100.0f);
        DrawRect(262, 373, 9, 24, 0xC8A5FFFF, 100.0f);
        DrawText(295, 356, 10, 18, white, "MUSICAS");
        DrawText(295, 382, 8, 14, grey, "Sua biblioteca");

        // Card de artistas
        DrawRect(412, 337, 194, 83, 0x171421FF, 300.0f);
        DrawRect(426, 348, 60, 60, 0x20394AFF, 180.0f);
        DrawRect(436, 358, 40, 40, 0x39718AFF, 120.0f);
        DrawRect(446, 368, 20, 20, 0x8AC5D9FF, 100.0f);
        DrawText(497, 356, 10, 18, white, "ARTISTAS");
        DrawText(497, 382, 8, 14, grey, "Explore artistas");

        // Card de playlists
        DrawRect(614, 337, 206, 83, 0x171421FF, 300.0f);
        DrawRect(628, 348, 60, 60, 0x4A283BFF, 180.0f);
        DrawRect(638, 358, 40, 40, 0xA44F75FF, 120.0f);
        DrawRect(648, 368, 20, 20, 0xE2A0BDFF, 100.0f);
        DrawText(699, 356, 10, 18, white, "PLAYLISTS");
        DrawText(699, 382, 8, 14, grey, "Suas colecoes");

        DrawText(
            210, 426, 8, 14,
            0xC7C0D8FF,
            "DESENVOLVIDO POR: MARCELO GUILHERME"
        );

    } else {
        DrawText(210, 86, 23, 38, white, pageTitles[page]);

        DrawRect(210, 145, 610, 276, 0x171421FF, 300.0f);
        DrawRect(210, 145, 5, 276, purple, 100.0f);

        if (page == 1) {
            DrawText(241, 177, 15, 26, purple, "BUSCAR MUSICAS");
            DrawRect(241, 224, 548, 48, 0x242033FF, 180.0f);
            DrawText(259, 237, 10, 18, grey, "BUSQUE POR MUSICA OU ARTISTA");
            DrawText(241, 302, 10, 18, white, "A busca sera integrada nas proximas etapas.");

        } else if (page == 2) {
            DrawText(241, 185, 16, 28, purple, "SEUS ARTISTAS");
            DrawText(241, 237, 11, 20, white, "Seus artistas aparecerao aqui.");
            DrawText(241, 273, 9, 16, grey, "Sua colecao ficara organizada nesta tela.");

        } else if (page == 3) {
            DrawText(241, 185, 16, 28, purple, "SEUS ALBUNS");
            DrawText(241, 237, 11, 20, white, "Sua colecao de albuns aparecera aqui.");

        } else if (page == 4) {
            DrawText(241, 185, 16, 28, purple, "SUAS PLAYLISTS");
            DrawText(241, 237, 11, 20, white, "Suas playlists aparecerao aqui.");

        } else if (page == 5) {
            DrawText(241, 185, 16, 28, purple, "CONFIGURACOES");
            DrawText(241, 237, 11, 20, white, "JEZZ - Versao 0.3");
            DrawText(241, 285, 10, 18, grey, "Cima/Baixo: navegar");
            DrawText(241, 318, 10, 18, grey, "X: selecionar");
            DrawText(241, 351, 10, 18, grey, "O: voltar ao inicio");
            DrawText(241, 384, 10, 18, grey, "Triangulo: sair");
        }
    }

    // Player inferior
    DrawRect(0, 447, 848, 65, 0x12101BFF, 50000.0f);
    DrawRect(0, 447, 848, 2, 0x49336AFF, 100.0f);

    DrawText(22, 458, 10, 18, white, "NADA TOCANDO");
    DrawText(22, 483, 8, 14, grey, "Sua proxima musica aparecera aqui");

    DrawText(390, 462, 10, 18, grey, "ANTERIOR");
    DrawText(492, 462, 11, 19, white, "PLAY");
    DrawText(568, 462, 10, 18, grey, "PROXIMA");

    DrawRect(385, 493, 260, 3, 0x393344FF, 150.0f);
    DrawRect(385, 493, 2, 3, purple, 100.0f);

    DrawText(754, 462, 8, 14, grey, "VOLUME");
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
