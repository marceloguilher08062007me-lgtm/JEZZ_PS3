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
        (FT_UInt)*w,
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
        "/dev_flash/data/font/SCE-PS3-VR-R-LATIN2.TTF";

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
        16,
        32,
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

    DrawRect(
        0, 0, 847, 511,
        0x090811FF,
        65535.0f
    );

    DrawRect(
        0, 0, 847, 66,
        0x13111EFF,
        50000.0f
    );

    DrawRect(
        0, 66, 190, 445,
        0x0E0C17FF,
        50000.0f
    );

    DrawRect(24, 25, 5, 20, purple, 100.0f);
    DrawRect(34, 17, 5, 28, purple, 100.0f);
    DrawRect(44, 22, 5, 23, purple, 100.0f);
    DrawRect(54, 29, 5, 16, purple, 100.0f);

    DrawText(72, 13, 22, 34, white, "JEZZ");

    DrawText(
        170, 25, 10, 20,
        grey,
        "MUSIC PLAYER"
    );

    DrawText(
        24, 84, 10, 20,
        grey,
        "MENU PRINCIPAL"
    );

    for (int i = 0; i < MENU_COUNT; ++i) {

        const float y = 112.0f + (float)i * 51.0f;

        if (i == selected) {

            DrawRect(
                13, y, 163, 39,
                0x35234FFF,
                60.0f
            );

            DrawRect(
                13, y, 4, 39,
                purple,
                40.0f
            );
        }

        DrawText(
            31,
            y + 8,
            12,
            22,
            (i == selected) ? white : grey,
            menuItems[i]
        );
    }

    DrawText(
        24, 459, 10, 18,
        purple,
        "JEZZ v0.2"
    );

    DrawText(
        24, 480, 8, 14,
        grey,
        "PLAY YOUR WAY"
    );


    /* PAGINA INICIAL */

    if (page == 0) {

        DrawText(
            215, 82, 20, 34,
            white,
            "BEM-VINDO AO JEZZ"
        );

        DrawRect(
            215, 130, 610, 155,
            0x191425FF,
            200.0f
        );

        DrawRect(
            215, 130, 6, 155,
            purple,
            100.0f
        );

        DrawText(
            246, 157, 12, 22,
            purple,
            "SUA EXPERIENCIA MUSICAL"
        );

        DrawText(
            246, 195, 20, 34,
            white,
            "Sua musica. Seu espaco."
        );

        DrawText(
            246, 239, 10, 18,
            grey,
            "O JEZZ esta sendo preparado."
        );

        DrawText(
            215, 312, 14, 25,
            white,
            "SUA BIBLIOTECA"
        );

        DrawRect(
            215, 350, 190, 112,
            0x171421FF,
            200.0f
        );

        DrawRect(
            420, 350, 190, 112,
            0x171421FF,
            200.0f
        );

        DrawRect(
            625, 350, 200, 112,
            0x171421FF,
            200.0f
        );

        DrawText(
            234, 369, 11, 20,
            purple,
            "MUSICAS"
        );

        DrawText(
            234, 407, 9, 16,
            grey,
            "Biblioteca vazia"
        );

        DrawText(
            439, 369, 11, 20,
            purple,
            "ARTISTAS"
        );

        DrawText(
            439, 407, 9, 16,
            grey,
            "Explore artistas"
        );

        DrawText(
            644, 369, 11, 20,
            purple,
            "PLAYLISTS"
        );

        DrawText(
            644, 407, 9, 16,
            grey,
            "Suas playlists"
        );

        /* CREDITO DO CRIADOR */

        DrawText(
            215, 465, 8, 14,
            0xC7C0D8FF,
            "DESENVOLVIDO POR: MARCELO GUILHERME"
        );

    } else {

        DrawText(
            215, 82, 22, 36,
            white,
            pageTitles[page]
        );

        DrawRect(
            215, 145, 610, 280,
            0x171421FF,
            200.0f
        );

        DrawRect(
            215, 145, 6, 280,
            purple,
            100.0f
        );

        if (page == 1) {

            DrawText(
                246, 185, 16, 28,
                purple,
                "BUSCAR MUSICAS"
            );

            DrawText(
                246, 235, 11, 20,
                white,
                "A busca sera integrada nas proximas etapas."
            );

        } else if (page == 2) {

            DrawText(
                246, 185, 16, 28,
                purple,
                "SEUS ARTISTAS"
            );

            DrawText(
                246, 235, 11, 20,
                white,
                "Seus artistas aparecerao aqui."
            );

        } else if (page == 3) {

            DrawText(
                246, 185, 16, 28,
                purple,
                "SEUS ALBUNS"
            );

            DrawText(
                246, 235, 11, 20,
                white,
                "Sua colecao de albuns aparecera aqui."
            );

        } else if (page == 4) {

            DrawText(
                246, 185, 16, 28,
                purple,
                "SUAS PLAYLISTS"
            );

            DrawText(
                246, 235, 11, 20,
                white,
                "Suas playlists aparecerao aqui."
            );

        } else if (page == 5) {

            DrawText(
                246, 185, 16, 28,
                purple,
                "CONFIGURACOES"
            );

            DrawText(
                246, 235, 12, 22,
                white,
                "JEZZ - Versao 0.2"
            );

            DrawText(
                246, 285, 10, 18,
                grey,
                "Cima/Baixo: navegar"
            );

            DrawText(
                246, 318, 10, 18,
                grey,
                "X: selecionar"
            );

            DrawText(
                246, 351, 10, 18,
                grey,
                "O: voltar ao inicio"
            );

            DrawText(
                246, 384, 10, 18,
                grey,
                "Triangulo: sair"
            );
        }
    }

    DrawText(
        215, 482, 8, 14,
        grey,
        "JEZZ  |  NAVEGUE COM O CONTROLE DO PS3"
    );
}


/* =========================================================
   PRINCIPAL
   ========================================================= */

int main(int argc, const char *argv[]) {

    (void)argc;
    (void)argv;

    if (tiny3d_Init(1024 * 1024) != TINY3D_OK)
        return 1;

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
