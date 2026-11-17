#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <io/pad.h>
#include <tiny3d.h>
#include <libfont.h>
#include <ft2build.h>
#include FT_FREETYPE_H

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

static void TTFToBitmap(
    u8 chr, u8 *bitmap,
    short *w, short *h,
    short *yCorrection
) {
    if (!g_ftFace || !bitmap || !w || !h || !yCorrection) {
        if (w) *w = 0;
        return;
    }

    FT_Set_Pixel_Sizes(g_ftFace, (FT_UInt)*w, (FT_UInt)*h);

    if (FT_Load_Char(g_ftFace, (FT_ULong)chr, FT_LOAD_RENDER)) {
        *w = 0;
        return;
    }

    FT_GlyphSlot slot = g_ftFace->glyph;
    const int inputWidth = *w;
    const int inputHeight = *h;

    memset(bitmap, 0, (size_t)inputWidth * (size_t)inputHeight);

    *yCorrection = (short)(inputHeight - 1 - slot->bitmap_top);

    const int rows = (int)slot->bitmap.rows;
    const int cols = (int)slot->bitmap.width;
    const int pitch = (int)slot->bitmap.pitch;

    for (int row = 0; row < rows && row < inputHeight; ++row) {
        const unsigned char *src;

        if (pitch >= 0)
            src = slot->bitmap.buffer + row * pitch;
        else
            src = slot->bitmap.buffer + (rows - 1 - row) * (-pitch);

        for (int col = 0; col < cols && col < inputWidth; ++col)
            bitmap[row * inputWidth + col] = src[col];
    }

    *w = (short)(((slot->advance.x + 31) >> 6) +
        ((slot->bitmap_left < 0) ? -slot->bitmap_left : 0));

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

static void DrawRect(
    float x, float y,
    float width, float height,
    u32 color, float z
) {
    tiny3d_SetPolygon(TINY3D_QUADS);

    tiny3d_VertexPos(x, y, z);
    tiny3d_VertexColor(color);

    tiny3d_VertexPos(x + width, y, z);
    tiny3d_VertexPos(x + width, y + height, z);
    tiny3d_VertexPos(x, y + height, z);

    tiny3d_End();
}

/* Fonte gráfica simples: funciona antes do FreeType carregar. */
static const u8 pixelFont[26][7] = {
    {14,17,17,31,17,17,17}, // A
    {30,17,17,30,17,17,30}, // B
    {15,16,16,16,16,16,15}, // C
    {30,17,17,17,17,17,30}, // D
    {31,16,16,30,16,16,31}, // E
    {31,16,16,30,16,16,16}, // F
    {15,16,16,23,17,17,15}, // G
    {17,17,17,31,17,17,17}, // H
    {31,4,4,4,4,4,31},       // I
    {7,2,2,2,18,18,12},      // J
    {17,18,20,24,20,18,17},  // K
    {16,16,16,16,16,16,31},  // L
    {17,27,21,21,17,17,17},  // M
    {17,25,21,19,17,17,17},  // N
    {14,17,17,17,17,17,14},  // O
    {30,17,17,30,16,16,16},  // P
    {14,17,17,17,21,18,13},  // Q
    {30,17,17,30,20,18,17},  // R
    {15,16,16,14,1,1,30},    // S
    {31,4,4,4,4,4,4},        // T
    {17,17,17,17,17,17,14},  // U
    {17,17,17,17,17,10,4},   // V
    {17,17,17,21,21,21,10},  // W
    {17,17,10,4,10,17,17},   // X
    {17,17,10,4,4,4,4},       // Y
    {31,1,2,4,8,16,31}       // Z
};

static void DrawPixelText(
    const char *text,
    float x, float y,
    float scale,
    u32 color
) {
    if (!text) return;

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

static void Setup2D(void) {
    tiny3d_AlphaTest(1, 0x10, TINY3D_ALPHA_FUNC_GEQUAL);

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

/* Primeira imagem do aplicativo: não depende do FreeType. */
static void DrawBootSplash(void) {
    const u32 purple = 0x9560FFFF;
    const u32 white = 0xFFFFFFFF;
    const u32 dark = 0x090811FF;

    tiny3d_Clear(dark, TINY3D_CLEAR_ALL);
    Setup2D();

    DrawRect(0, 0, 847, 511, dark, 500.0f);
    DrawRect(0, 0, 847, 5, purple, 100.0f);
    DrawRect(0, 506, 847, 5, purple, 100.0f);

    /* Brilho e painel central */
    DrawRect(185, 65, 477, 380, 0x11101DFF, 300.0f);
    DrawRect(185, 65, 477, 2, 0x49336AFF, 200.0f);

    /* Xicara estilizada */
    DrawRect(359, 105, 128, 13, white, 100.0f);
    DrawRect(370, 118, 106, 66, white, 100.0f);
    DrawRect(380, 127, 86, 48, 0x171421FF, 90.0f);
    DrawRect(379, 179, 88, 7, purple, 80.0f);

    /* Alca */
    DrawRect(476, 130, 27, 12, purple, 100.0f);
    DrawRect(493, 140, 10, 29, purple, 100.0f);
    DrawRect(476, 166, 27, 10, purple, 100.0f);

    /* Vapor em forma de nota musical */
    DrawRect(397, 76, 5, 28, purple, 100.0f);
    DrawRect(397, 74, 24, 5, purple, 100.0f);
    DrawRect(416, 77, 5, 20, purple, 100.0f);
    DrawRect(411, 94, 13, 8, purple, 100.0f);
    DrawRect(390, 98, 13, 8, purple, 100.0f);

    /* Nome JEZZ */
    DrawPixelText("JEZZ", 321, 235, 10.0f, white);

    DrawRect(300, 330, 247, 2, 0x49336AFF, 100.0f);

    DrawPixelText("MUSICA EM TODO LUGAR", 330, 354, 2.0f, 0xC7C0D8FF);
    DrawPixelText("CARREGANDO", 365, 395, 2.0f, purple);

    tiny3d_Flip();
}

static void DrawText(
    float x, float y,
    int width, int height,
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

static void DrawScreen(int selected, int page) {
    const u32 purple = 0x9560FFFF;
    const u32 white = 0xFFFFFFFF;
    const u32 grey = 0xAAA5B8FF;

    DrawRect(0, 0, 847, 511, 0x090811FF, 65535.0f);
    DrawRect(0, 0, 847, 66, 0x13111EFF, 50000.0f);
    DrawRect(0, 66, 190, 445, 0x0E0C17FF, 50000.0f);

    DrawRect(24, 25, 5, 20, purple, 100.0f);
    DrawRect(34, 17, 5, 28, purple, 100.0f);
    DrawRect(44, 22, 5, 23, purple, 100.0f);
    DrawRect(54, 29, 5, 16, purple, 100.0f);

    DrawText(72, 13, 22, 34, white, "JEZZ");
    DrawText(170, 25, 10, 20, grey, "MUSIC PLAYER");

    DrawText(24, 84, 10, 20, grey, "MENU PRINCIPAL");

    for (int i = 0; i < MENU_COUNT; ++i) {
        const float y = 112.0f + (float)i * 51.0f;

        if (i == selected) {
            DrawRect(13, y, 163, 39, 0x35234FFF, 60.0f);
            DrawRect(13, y, 4, 39, purple, 40.0f);
        }

        DrawText(
            31, y + 8, 12, 22,
            (i == selected) ? white : grey,
            menuItems[i]
        );
    }

    DrawText(24, 459, 10, 18, purple, "JEZZ v0.2");
    DrawText(24, 480, 8, 14, grey, "PLAY YOUR WAY");

    if (page == 0) {
        DrawText(215, 82, 20, 34, white, "BEM-VINDO AO JEZZ");

        DrawRect(215, 130, 610, 155, 0x191425FF, 200.0f);
        DrawRect(215, 130, 6, 155, purple, 100.0f);

        DrawText(246, 157, 12, 22, purple, "SUA EXPERIENCIA MUSICAL");
        DrawText(246, 195, 20, 34, white, "Sua musica. Seu espaco.");
        DrawText(246, 239, 10, 18, grey, "O JEZZ esta sendo preparado.");

        DrawText(215, 312, 14, 25, white, "SUA BIBLIOTECA");

        DrawRect(215, 350, 190, 112, 0x171421FF, 200.0f);
        DrawRect(420, 350, 190, 112, 0x171421FF, 200.0f);
        DrawRect(625, 350, 200, 112, 0x171421FF, 200.0f);

        DrawText(234, 369, 11, 20, purple, "MUSICAS");
        DrawText(234, 407, 9, 16, grey, "Biblioteca vazia");

        DrawText(439, 369, 11, 20, purple, "ARTISTAS");
        DrawText(439, 407, 9, 16, grey, "Explore artistas");

        DrawText(644, 369, 11, 20, purple, "PLAYLISTS");
        DrawText(644, 407, 9, 16, grey, "Suas playlists");
    } else {
        DrawText(215, 82, 22, 36, white, pageTitles[page]);

        DrawRect(215, 145, 610, 280, 0x171421FF, 200.0f);
        DrawRect(215, 145, 6, 280, purple, 100.0f);

        if (page == 1) {
            DrawText(246, 185, 16, 28, purple, "BUSCAR MUSICAS");
            DrawText(246, 235, 11, 20, white,
                "A busca sera integrada nas proximas etapas.");
        } else if (page == 2) {
            DrawText(246, 185, 16, 28, purple, "SEUS ARTISTAS");
            DrawText(246, 235, 11, 20, white,
                "Seus artistas aparecerao aqui.");
        } else if (page == 3) {
            DrawText(246, 185, 16, 28, purple, "SEUS ALBUNS");
            DrawText(246, 235, 11, 20, white,
                "Sua colecao de albuns aparecera aqui.");
        } else if (page == 4) {
            DrawText(246, 185, 16, 28, purple, "SUAS PLAYLISTS");
            DrawText(246, 235, 11, 20, white,
                "Suas playlists aparecerao aqui.");
        } else if (page == 5) {
            DrawText(246, 185, 16, 28, purple, "CONFIGURACOES");
            DrawText(246, 235, 12, 22, white, "JEZZ - Versao 0.2");
            DrawText(246, 285, 10, 18, grey, "Cima/Baixo: navegar");
            DrawText(246, 318, 10, 18, grey, "X: selecionar");
            DrawText(246, 351, 10, 18, grey, "O: voltar ao inicio");
            DrawText(246, 384, 10, 18, grey, "Triangulo: sair");
        }
    }

    DrawText(215, 482, 8, 14, grey,
        "JEZZ  |  NAVEGUE COM O CONTROLE DO PS3");
}

int main(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;

    if (tiny3d_Init(1024 * 1024) != TINY3D_OK)
        return 1;

    /*
     * Mostra a primeira imagem ANTES de carregar a fonte.
     * O quadro fica na tela enquanto InitFont trabalha.
     */
    DrawBootSplash();

    ioPadInit(7);

    /* O carregamento demorado acontece com a abertura visivel. */
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

        tiny3d_Clear(0x090811FF, TINY3D_CLEAR_ALL);
        Setup2D();

        DrawScreen(selected, page);
        tiny3d_Flip();
    }

    ioPadEnd();
    return 0;
}
