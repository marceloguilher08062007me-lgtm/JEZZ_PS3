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
#include <sys/systime.h>
#include <net/net.h>
#include <net/netctl.h>
#include <sys/thread.h>
#include <curl/curl.h>
#include <lv2/system.h>
#include <audio/audio.h>
#include <math.h>

#define JEZZ_LOGO_PATH "/dev_hdd0/game/JEZZ00001/USRDIR/assets/JEZZ_LOGO.PNG"

enum JezzLanguage {
    LANG_PT = 0,
    LANG_EN = 1,
    LANG_ES = 2
};

static const int MENU_COUNT = 8;
static int g_language = LANG_PT;

static const char *LanguageText(const char *pt, const char *en, const char *es) {
    if (g_language == LANG_EN) return en;
    if (g_language == LANG_ES) return es;
    return pt;
}

static const char *MenuItem(int index) {
    switch (index) {
        case 0: return LanguageText("INICIO", "HOME", "INICIO");
        case 1: return LanguageText("BUSCAR", "SEARCH", "BUSCAR");
        case 2: return LanguageText("ARTISTAS", "ARTISTS", "ARTISTAS");
        case 3: return LanguageText("ÁLBUNS", "ALBUMS", "ÁLBUMES");
        case 4: return LanguageText("PLAYLISTS", "PLAYLISTS", "LISTAS");
        case 5: return LanguageText("CONFIGURAÇÕES", "SETTINGS", "AJUSTES");
        case 6: return LanguageText("CRÉDITOS", "CREDITS", "CRÉDITOS");
        default: return LanguageText("APOIE O PROJETO", "SUPPORT PROJECT", "APOYAR PROYECTO");
    }
}

#define JEZZ_SETTINGS_PATH "/dev_hdd0/game/JEZZ00001/USRDIR/jezz_settings.cfg"

static void LoadSettings(void) {
    FILE *file = fopen(JEZZ_SETTINGS_PATH, "r");
    if (!file) return;

    int savedLanguage = LANG_PT;
    if (fscanf(file, "%d", &savedLanguage) == 1 &&
        savedLanguage >= LANG_PT && savedLanguage <= LANG_ES) {
        g_language = savedLanguage;
    }
    fclose(file);
}

static void SaveSettings(void) {
    FILE *file = fopen(JEZZ_SETTINGS_PATH, "w");
    if (!file) return;
    fprintf(file, "%d\n", g_language);
    fclose(file);
}

enum JezzFocus {
    FOCUS_SIDEBAR = 0,
    FOCUS_CONTENT = 1,
    FOCUS_PLAYER = 2
};

static FT_Library g_ftLibrary;
static FT_Face g_ftFace;
static bool g_fontReady = false;
static bool g_boldFontReady = false;

static bool g_pngModuleLoaded = false;
static bool g_netModuleLoaded = false;
static bool g_netCtlModuleLoaded = false;
static bool g_netInitialized = false;
static bool g_netCtlInitialized = false;


// Teclado virtual e resultados da busca online.
static char g_searchText[128] = {0};
static bool g_keyboardOpen = false;
static int g_keyboardRow = 0;
static int g_keyboardCol = 0;
static bool g_searchRequested = false;
static bool g_previousStart = false;
static volatile bool g_searchBusy = false;
static bool g_searchThreadActive = false;
static sys_ppu_thread_t g_searchThread;
static char g_searchQuery[128] = {0};
static char g_searchStatus[96] = "";
static char g_resultTitle[6][128];
static char g_resultArtist[6][96];
static int g_resultCount = 0;

struct JezzHttpBuffer { char data[65536]; size_t used; };
static size_t JezzCurlWrite(char *ptr, size_t size, size_t nmemb, void *userdata) {
    JezzHttpBuffer *b = (JezzHttpBuffer *)userdata;
    size_t n = size * nmemb;
    if (!b || b->used + n >= sizeof(b->data)) return 0;
    memcpy(b->data + b->used, ptr, n);
    b->used += n;
    b->data[b->used] = 0;
    return n;
}

static const char *JsonSkipString(const char *p, const char *end) {
    if (!p || p >= end || *p != '"') return p;
    ++p;
    while (p < end) {
        if (*p == '\\') {
            ++p;
            if (p < end) ++p;
        } else if (*p == '"') {
            return p + 1;
        } else {
            ++p;
        }
    }
    return end;
}

static const char *JsonObjectEnd(const char *start, const char *limit) {
    if (!start || start >= limit || *start != '{') return NULL;
    int depth = 0;
    const char *p = start;
    while (p < limit) {
        if (*p == '"') {
            p = JsonSkipString(p, limit);
            continue;
        }
        if (*p == '{') ++depth;
        else if (*p == '}') {
            --depth;
            if (depth == 0) return p;
        }
        ++p;
    }
    return NULL;
}

static const char *JsonArrayEnd(const char *start, const char *limit) {
    if (!start || start >= limit || *start != '[') return NULL;
    int depth = 0;
    const char *p = start;
    while (p < limit) {
        if (*p == '"') {
            p = JsonSkipString(p, limit);
            continue;
        }
        if (*p == '[') ++depth;
        else if (*p == ']') {
            --depth;
            if (depth == 0) return p;
        }
        ++p;
    }
    return NULL;
}

static const char *JsonFindKey(const char *start, const char *end, const char *key) {
    if (!start || !end || !key || start >= end) return NULL;
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    size_t keyLen = strlen(pattern);
    const char *p = start;
    while (p < end) {
        if (*p == '"') {
            const char *after = JsonSkipString(p, end);
            if ((size_t)(after - p) == keyLen &&
                memcmp(p, pattern, keyLen) == 0) {
                const char *v = after;
                while (v < end && (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n')) ++v;
                if (v < end && *v == ':') return v + 1;
            }
            p = after;
        } else {
            ++p;
        }
    }
    return NULL;
}

static bool JsonReadStringRange(const char *start, const char *end,
                                const char *key, char *out, size_t cap) {
    if (!out || cap < 2) return false;
    out[0] = 0;
    const char *p = JsonFindKey(start, end, key);
    if (!p) return false;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    if (p >= end || *p != '"') return false;
    ++p;
    size_t n = 0;
    while (p < end && *p != '"') {
        unsigned char ch = (unsigned char)*p++;
        if (ch == '\\') {
            if (p >= end) break;
            char esc = *p++;
            switch (esc) {
                case 'n': case 'r': case 't': ch = ' '; break;
                case 'b': ch = ' '; break;
                case 'f': ch = ' '; break;
                case '"': ch = '"'; break;
                case '\\': ch = '\\'; break;
                case '/': ch = '/'; break;
                case 'u':
                    // Mantem o parser seguro; caracteres Unicode escapados
                    // sao omitidos em vez de copiar sequencias JSON literais.
                    for (int i = 0; i < 4 && p < end; ++i) ++p;
                    ch = ' ';
                    break;
                default: ch = (unsigned char)esc; break;
            }
        }
        if (n + 1 < cap) out[n++] = (char)ch;
    }
    out[n] = 0;
    return true;
}

static void SearchAudius(const char *query) {
    char titles[6][128] = {{0}};
    char artists[6][96] = {{0}};
    int resultCount = 0;
    char statusText[96];

    snprintf(statusText, sizeof(statusText), "%s",
        LanguageText("AGUARDANDO REDE...", "WAITING FOR NETWORK...", "ESPERANDO RED..."));

    // Aguarda o PS3 obter um endereço IP.
    bool networkReady = false;
    for (int i = 0; i < 30; ++i) {
        s32 state = -1;
        if (netCtlGetState(&state) == 0 &&
            state == NET_CTL_STATE_IPObtained) {
            networkReady = true;
            break;
        }
        sysUsleep(1000000);
    }

    if (!networkReady) {
        snprintf(statusText, sizeof(statusText), "%s",
            LanguageText("REDE SEM IP. VERIFIQUE A INTERNET.",
                         "NETWORK HAS NO IP. CHECK INTERNET.",
                         "RED SIN IP. REVISA INTERNET."));
        goto publish;
    }

    {
        CURL *curl = curl_easy_init();
        if (!curl) {
            snprintf(statusText, sizeof(statusText), "%s",
                LanguageText("ERRO AO INICIAR A BUSCA.",
                             "SEARCH INITIALIZATION ERROR.",
                             "ERROR AL INICIAR LA BUSQUEDA."));
            goto publish;
        }

        char *escaped = curl_easy_escape(curl, query, 0);
        if (!escaped) {
            curl_easy_cleanup(curl);
            snprintf(statusText, sizeof(statusText), "%s",
                     "ERRO NO TEXTO DA BUSCA.");
            goto publish;
        }

        char url[512];
        snprintf(url, sizeof(url),
            "https://api.audius.co/v1/tracks/search?query=%s&limit=6",
            escaped);
        curl_free(escaped);

        JezzHttpBuffer response;
        memset(&response, 0, sizeof(response));

        char curlError[CURL_ERROR_SIZE] = {0};

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, JezzCurlWrite);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_CAINFO,
                         "/dev_hdd0/game/JEZZ00001/USRDIR/cacert.pem");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "JEZZ-PS3/0.8");

        CURLcode rc = curl_easy_perform(curl);
        long httpStatus = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);

        if (rc != CURLE_OK) {
            const char *detail = curlError[0]
                ? curlError : curl_easy_strerror(rc);
            snprintf(statusText, sizeof(statusText),
                     "CURL %d: %.65s", (int)rc, detail);
            curl_easy_cleanup(curl);
            goto publish;
        }

        curl_easy_cleanup(curl);

        if (httpStatus != 200) {
            snprintf(statusText, sizeof(statusText),
                     "AUDIUS HTTP: %ld", httpStatus);
            goto publish;
        }

        const char *dataKey = strstr(response.data, "\"data\"");
        const char *arrayStart = dataKey ? strchr(dataKey, '[') : NULL;

        if (!arrayStart) {
            snprintf(statusText, sizeof(statusText), "%s",
                LanguageText("RESPOSTA INVALIDA DA FONTE.",
                             "INVALID SOURCE RESPONSE.",
                             "RESPUESTA NO VALIDA DE LA FUENTE."));
            goto publish;
        }

        const char *arrayEnd =
            JsonArrayEnd(arrayStart, response.data + response.used);

        if (!arrayEnd) {
            snprintf(statusText, sizeof(statusText), "%s",
                LanguageText("RESPOSTA JSON INCOMPLETA.",
                             "INCOMPLETE JSON RESPONSE.",
                             "RESPUESTA JSON INCOMPLETA."));
            goto publish;
        }

        const char *p = arrayStart + 1;

        while (p < arrayEnd && resultCount < 6) {
            while (p < arrayEnd &&
                   (*p == ' ' || *p == '\t' ||
                    *p == '\r' || *p == '\n' || *p == ',')) {
                ++p;
            }

            if (p >= arrayEnd) break;
            if (*p != '{') {
                ++p;
                continue;
            }

            const char *objectEnd = JsonObjectEnd(p, arrayEnd);
            if (!objectEnd) break;

            char title[128] = "";
            char artist[96] = "Audius";

            if (JsonReadStringRange(
                    p, objectEnd + 1, "title", title, sizeof(title)) &&
                title[0]) {

                const char *userValue =
                    JsonFindKey(p, objectEnd + 1, "user");

                if (userValue) {
                    while (userValue < objectEnd &&
                           (*userValue == ' ' || *userValue == '\t' ||
                            *userValue == '\r' || *userValue == '\n')) {
                        ++userValue;
                    }

                    if (userValue < objectEnd && *userValue == '{') {
                        const char *userEnd =
                            JsonObjectEnd(userValue, objectEnd + 1);
                        if (userEnd) {
                            JsonReadStringRange(
                                userValue, userEnd + 1,
                                "name", artist, sizeof(artist));
                        }
                    }
                }

                strncpy(titles[resultCount], title,
                        sizeof(titles[0]) - 1);
                titles[resultCount][sizeof(titles[0]) - 1] = 0;

                strncpy(artists[resultCount], artist,
                        sizeof(artists[0]) - 1);
                artists[resultCount][sizeof(artists[0]) - 1] = 0;

                ++resultCount;
            }

            p = objectEnd + 1;
        }

        if (resultCount) {
            snprintf(statusText, sizeof(statusText), "%d %s",
                resultCount,
                LanguageText("RESULTADOS - AUDIUS",
                             "RESULTS - AUDIUS",
                             "RESULTADOS - AUDIUS"));
        } else {
            snprintf(statusText, sizeof(statusText), "%s",
                LanguageText("NENHUMA FAIXA ENCONTRADA.",
                             "NO TRACKS FOUND.",
                             "NO SE ENCONTRARON CANCIONES."));
        }
    }

publish:
    // Publica os resultados somente depois que a resposta estiver pronta.
    memset(g_resultTitle, 0, sizeof(g_resultTitle));
    memset(g_resultArtist, 0, sizeof(g_resultArtist));

    for (int i = 0; i < resultCount; ++i) {
        strncpy(g_resultTitle[i], titles[i],
                sizeof(g_resultTitle[i]) - 1);
        strncpy(g_resultArtist[i], artists[i],
                sizeof(g_resultArtist[i]) - 1);
    }

    g_resultCount = resultCount;
    snprintf(g_searchStatus, sizeof(g_searchStatus),
             "%s", statusText);
}

static void AudiusSearchThread(void *arg) {
    SearchAudius((const char *)arg);
    g_searchBusy = false;
}

static const char *g_keyboardKeys[4][10] = {
    {"A","B","C","D","E","F","G","H","I","J"},
    {"K","L","M","N","O","P","Q","R","S","T"},
    {"U","V","W","X","Y","Z","0","1","2","3"},
    {"4","5","6","7","8","9","ESP","DEL","LIM","OK"}
};

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

    int advance = (int)((slot->advance.x + 31) >> 6) +
                  ((slot->bitmap_left < 0) ? -slot->bitmap_left : 0);

    // Tracking leve para evitar que as letras fiquem visualmente grudadas.
    // O espaco recebe um avanco maior para separar claramente as palavras.
    advance += 2;
    if (chr == (u8)' ')
        advance += 10;

    *w = (short)advance;
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
        (u8 *)fontTexture, 32, 255, 64, 64, TTFToBitmap
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
            fontEnd, 32, 255, 64, 64, TTFToBitmap
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
        LanguageText("MUSICA EM TODO LUGAR", "MUSIC EVERYWHERE", "MUSICA EN TODAS PARTES"),
        330,
        365,
        2.0f,
        0xC7C0D8FF
    );

    DrawPixelText(
        LanguageText("INICIANDO", "STARTING", "INICIANDO"),
        365,
        405,
        2.0f,
        purple
    );

    tiny3d_Flip();
}


static void AudioPump(void);

static void PlayBootAnimation(void) {

    const int frames = 45;

    for (int frame = 0; frame <= frames; ++frame) {

        float progress =
            (float)frame / (float)frames;

        AudioPump();
        DrawBootSplash(progress);
    }
}


/* =========================================================
   CONVERSAO UTF-8 PARA LATIN-1
   libfont trabalha com caracteres de 8 bits; esta conversao
   permite desenhar acentos portugueses e espanhóis.
   ========================================================= */

static void UTF8ToLatin1(const char *src, char *dst, size_t dstSize) {
    if (!dst || dstSize == 0)
        return;

    size_t out = 0;
    const unsigned char *p = (const unsigned char *)src;

    if (!p) {
        dst[0] = '\0';
        return;
    }

    while (*p && out + 1 < dstSize) {
        unsigned int codepoint = 0;

        if (*p < 0x80) {
            codepoint = *p++;
        } else if ((*p & 0xE0) == 0xC0 && p[1]) {
            codepoint = ((unsigned int)(p[0] & 0x1F) << 6) |
                        (unsigned int)(p[1] & 0x3F);
            p += 2;
        } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
            codepoint = ((unsigned int)(p[0] & 0x0F) << 12) |
                        ((unsigned int)(p[1] & 0x3F) << 6) |
                        (unsigned int)(p[2] & 0x3F);
            p += 3;
        } else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
            codepoint = ((unsigned int)(p[0] & 0x07) << 18) |
                        ((unsigned int)(p[1] & 0x3F) << 12) |
                        ((unsigned int)(p[2] & 0x3F) << 6) |
                        (unsigned int)(p[3] & 0x3F);
            p += 4;
        } else {
            ++p;
            codepoint = '?';
        }

        if (codepoint == 0xA0)
            codepoint = ' ';

        dst[out++] = (codepoint <= 0xFF)
            ? (char)codepoint
            : '?';
    }

    dst[out] = '\0';
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

    char latinText[512];
    UTF8ToLatin1(value, latinText, sizeof(latinText));

    SetCurrentFont(0);

    SetFontSize(width + 1, height);

    SetFontColor(color, 0x00000000);

    SetFontZ(0.0f);

    DrawString(x, y, latinText);
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

    char latinText[512];
    UTF8ToLatin1(value, latinText, sizeof(latinText));

    SetCurrentFont(g_boldFontReady ? 1 : 0);
    SetFontSize(width + 1, height);
    SetFontColor(color, 0x00000000);
    SetFontZ(0.0f);
    DrawString(x, y, latinText);
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


static void DrawFocusBorder(float x, float y, float width, float height, u32 color) {
    const float t = 2.0f;
    DrawRect(x, y, width, t, color, 70.0f);
    DrawRect(x, y + height - t, width, t, color, 70.0f);
    DrawRect(x, y, t, height, color, 70.0f);
    DrawRect(x + width - t, y, t, height, color, 70.0f);
}


static void DrawPlayerBar(
    u32 purple,
    u32 white,
    u32 grey,
    int focusArea,
    int playerIndex,
    int volume
) {
    DrawRect(0, 447, 848, 65, 0x11101AFF, 50000.0f);
    DrawRect(0, 447, 848, 2, 0x49336AFF, 100.0f);
    DrawRect(0, 448, 848, 1, 0x211A30FF, 90.0f);

    DrawCoverArt(18, 458, 43, 0);
    DrawTextBold(73, 458, 10, 18, white, LanguageText("NADA TOCANDO", "NOTHING PLAYING", "NADA SONANDO"));
    DrawText(73, 482, 8, 14, grey, LanguageText("ESCOLHA UMA FAIXA", "CHOOSE A TRACK", "ELIGE UNA CANCIÓN"));

    // Controles do player: esquerda, play/pause, proxima e volume.
    DrawText(365, 462, 10, 17, grey, "<<");
    DrawRect(425, 457, 34, 29, 0x302044FF, 120.0f);
    DrawTextBold(432, 463, 9, 16, white, ">");
    DrawText(480, 462, 10, 17, grey, ">>");

    if (focusArea == FOCUS_PLAYER) {
        if (playerIndex == 0)
            DrawFocusBorder(350, 454, 42, 34, 0xD8C4FFFF);
        else if (playerIndex == 1)
            DrawFocusBorder(421, 453, 42, 37, 0xD8C4FFFF);
        else if (playerIndex == 2)
            DrawFocusBorder(467, 454, 42, 34, 0xD8C4FFFF);
        else
            DrawFocusBorder(711, 453, 107, 43, 0xD8C4FFFF);
    }

    DrawRect(355, 495, 250, 3, 0x393344FF, 150.0f);
    DrawRect(355, 495, 3, 3, purple, 100.0f);

    DrawText(535, 459, 7, 12, grey, LanguageText("L1/R1: SEÇÕES", "L1/R1: SECTIONS", "L1/R1: SECCIONES"));
    DrawText(535, 480, 7, 12, grey, LanguageText("SELECT: PLAYER", "SELECT: PLAYER", "SELECT: REPRODUCTOR"));
    DrawText(610, 497, 7, 11, grey, LanguageText("O: SEGURE 5s", "HOLD O: 5s", "MANTENER O: 5s"));

    DrawText(720, 459, 8, 14, grey, LanguageText("VOLUME", "VOLUME", "VOLUMEN"));
    DrawRect(720, 482, 92, 4, 0x393344FF, 150.0f);
    int knobX = 720 + (volume * 92) / 100;
    DrawRect(720, 482, (float)(volume * 92) / 100.0f, 4, purple, 100.0f);
    DrawRect((float)knobX - 4.0f, 478, 8, 12, white, 90.0f);
}

static void DrawSidebar(int selected, int focusArea, u32 purple, u32 white, u32 grey) {
    DrawRect(0, 62, 176, 385, 0x0D0B15FF, 50000.0f);
    DrawRect(175, 62, 1, 385, 0x292337FF, 40000.0f);

    DrawText(22, 76, 9, 15, grey, LanguageText("MENU PRINCIPAL", "MAIN MENU", "MENU PRINCIPAL"));

    for (int i = 0; i < MENU_COUNT; ++i) {
        float y = 96.0f + (float)i * 40.0f;

        if (i == selected) {
            DrawRect(9, y, 158, 34, 0x302044FF, 180.0f);
            DrawRect(9, y, 3, 34, purple, 100.0f);
            DrawRect(164, y + 7, 2, 20, 0xB18AEAFF, 90.0f);
            if (focusArea == FOCUS_SIDEBAR)
                DrawFocusBorder(7, y - 1, 162, 36, 0xD8C4FFFF);
        }

        DrawTextBold(
            22,
            y + 8,
            9,
            17,
            (i == selected) ? white : grey,
            MenuItem(i)
        );
    }

    DrawRect(18, 419, 140, 1, 0x292337FF, 300.0f);
    DrawText(22, 423, 8, 14, purple, "JEZZ v0.6");
}


static void DrawVirtualKeyboard(void) {
    const u32 white = 0xFFFFFFFF;
    const u32 grey = 0xAAA5B8FF;
    DrawRect(211, 282, 598, 143, 0x0D0B15FF, 360.0f);
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 10; ++c) {
            float x = 219.0f + (float)c * 58.0f;
            float y = 289.0f + (float)r * 32.0f;
            bool selected = (r == g_keyboardRow && c == g_keyboardCol);
            DrawRect(x, y, 53, 27, selected ? 0x65449AFF : 0x242033FF, 180.0f);
            DrawTextBold(x + 7, y + 6, 8, 14, selected ? white : grey, g_keyboardKeys[r][c]);
            if (selected) DrawFocusBorder(x - 1, y - 1, 55, 29, 0xD8C4FFFF);
        }
    }
}

static void DrawScreen(int selected, int page, int focusArea, int contentRow, int contentIndex, int playerIndex, int volume) {
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
    DrawText(174, 25, 8, 15, grey, LanguageText("MÚSICA", "MUSIC", "MÚSICA"));

    DrawRect(696, 17, 130, 29, 0x1B1726FF, 200.0f);
    DrawRect(708, 28, 5, 5, 0x7DDBA4FF, 100.0f);
    DrawText(721, 25, 8, 14, grey, LanguageText("EDIÇÃO PS3", "PS3 EDITION", "EDICIÓN PS3"));

    DrawSidebar(selected, focusArea, purple, white, grey);

    // Conteudo principal.
    if (page == 0) {
        DrawText(196, 76, 9, 16, purple, LanguageText("BEM-VINDO DE VOLTA", "WELCOME BACK", "BIENVENIDO DE NUEVO"));
        DrawTextBold(196, 94, 19, 30, white, LanguageText("O QUE VAMOS OUVIR HOJE?", "WHAT SHALL WE LISTEN TO?", "QUE VAMOS A ESCUCHAR HOY?"));

        // Banner principal de destaque.
        DrawRect(194, 135, 632, 137, 0x171222FF, 320.0f);
        DrawRect(194, 135, 4, 137, purple, 100.0f);
        DrawRect(198, 136, 624, 1, 0x49336AFF, 180.0f);

        DrawText(218, 151, 9, 16, 0xC49BFFFF, LanguageText("DESTAQUE JEZZ", "JEZZ FEATURE", "DESTACADO JEZZ"));
        DrawTextBold(218, 177, 18, 29, white, LanguageText("DESCUBRA SEU PRÓXIMO SOM", "DISCOVER YOUR NEXT SOUND", "DESCUBRE TU PRÓXIMO SONIDO"));
        DrawText(218, 211, 8, 14, grey, LanguageText("Sua próxima descoberta começa aqui.", "Your next discovery starts here.", "Tu próximo descubrimiento empieza aquí."));
        DrawRect(218, 235, 139, 25, 0x65449AFF, 120.0f);
        DrawTextBold(231, 241, 8, 14, white, LanguageText("EXPLORAR AGORA", "EXPLORE NOW", "EXPLORAR AHORA"));

        // Arte abstrata principal.
        DrawRect(675, 145, 126, 116, 0x261A3BFF, 250.0f);
        DrawCoverArt(688, 151, 104, 0);
        DrawRect(668, 153, 2, 94, 0xC49BFFFF, 110.0f);
        if (focusArea == FOCUS_CONTENT && contentRow == 0)
            DrawFocusBorder(191, 132, 638, 143, 0xD8C4FFFF);

        // Pequenas barras de equalizador como detalhe.
        DrawRect(648, 226, 4, 22, 0x9560FFFF, 100.0f);
        DrawRect(655, 214, 4, 34, 0xB18AEAFF, 100.0f);
        DrawRect(662, 220, 4, 28, 0x65449AFF, 100.0f);

        // Secao de atalhos.
        DrawTextBold(196, 285, 13, 21, white, LanguageText("EXPLORE SUA MÚSICA", "EXPLORE YOUR MUSIC", "EXPLORA TU MÚSICA"));
        DrawText(754, 290, 7, 13, purple, LanguageText("VER MAIS", "SEE MORE", "VER MÁS"));

        // Card 1.
        DrawRect(194, 316, 202, 109, 0x15121FFF, 320.0f);
        DrawRect(194, 316, 202, 1, 0x302442FF, 180.0f);
        DrawCoverArt(207, 329, 82, 0);
        DrawTextBold(300, 342, 10, 18, white, LanguageText("MÚSICAS", "TRACKS", "CANCIONES"));
        DrawText(300, 366, 8, 14, grey, LanguageText("Sua coleção", "Your collection", "Tu colección"));
        DrawText(300, 394, 7, 12, purple, LanguageText("BIBLIOTECA", "LIBRARY", "BIBLIOTECA"));
        if (focusArea == FOCUS_CONTENT && contentRow == 1 && contentIndex == 0)
            DrawFocusBorder(191, 313, 208, 115, 0xD8C4FFFF);

        // Card 2.
        DrawRect(407, 316, 202, 109, 0x15121FFF, 320.0f);
        DrawRect(407, 316, 202, 1, 0x263D49FF, 180.0f);
        DrawCoverArt(420, 329, 82, 1);
        DrawTextBold(513, 342, 10, 18, white, LanguageText("ARTISTAS", "ARTISTS", "ARTISTAS"));
        DrawText(513, 366, 8, 14, grey, LanguageText("Seus favoritos", "Your favorites", "Tus favoritos"));
        DrawText(513, 394, 7, 12, 0x70C6D8FF, LanguageText("DESCUBRA", "DISCOVER", "DESCUBRIR"));
        if (focusArea == FOCUS_CONTENT && contentRow == 1 && contentIndex == 1)
            DrawFocusBorder(404, 313, 208, 115, 0xD8C4FFFF);

        // Card 3.
        DrawRect(620, 316, 206, 109, 0x15121FFF, 320.0f);
        DrawRect(620, 316, 206, 1, 0x493047FF, 180.0f);
        DrawCoverArt(633, 329, 82, 2);
        DrawTextBold(726, 342, 10, 18, white, LanguageText("PLAYLISTS", "PLAYLISTS", "LISTAS"));
        DrawText(726, 366, 8, 14, grey, LanguageText("Seus momentos", "Your moments", "Tus momentos"));
        DrawText(726, 394, 7, 12, 0xD58BAEFF, LanguageText("COLEÇÕES", "COLLECTIONS", "COLECCIONES"));
        if (focusArea == FOCUS_CONTENT && contentRow == 1 && contentIndex == 2)
            DrawFocusBorder(617, 313, 212, 115, 0xD8C4FFFF);

    } else {
        // Cabecalho das paginas internas.
        DrawText(196, 78, 9, 15, purple, LanguageText("SUA BIBLIOTECA", "YOUR LIBRARY", "TU BIBLIOTECA"));
        DrawTextBold(196, 98, 23, 37, white, MenuItem(page));

        DrawRect(194, 151, 632, 274, 0x15121FFF, 320.0f);
        DrawRect(194, 151, 4, 274, purple, 100.0f);
        DrawRect(199, 152, 626, 1, 0x302442FF, 180.0f);

        if (page == 1) {
            DrawTextBold(224, 178, 15, 25, white, LanguageText("ENCONTRE SUA PRÓXIMA MÚSICA", "FIND YOUR NEXT TRACK", "ENCUENTRA TU PRÓXIMA CANCIÓN"));
            DrawText(224, 210, 9, 15, grey, LanguageText("BUSQUE POR TÍTULO, ARTISTA OU ÁLBUM", "SEARCH BY TITLE, ARTIST OR ALBUM", "BUSCA POR TÍTULO, ARTISTA O ÁLBUM"));
            DrawRect(224, 245, 548, 32, 0x242033FF, 180.0f);
            DrawRect(224, 245, 3, 32, purple, 100.0f);
            DrawText(240, 253, 9, 16, g_searchText[0] ? white : grey,
                     g_searchText[0] ? g_searchText : LanguageText("SELECIONE PARA DIGITAR...", "SELECT TO TYPE...", "SELECCIONA PARA ESCRIBIR..."));
            // Cursor vertical piscante no fim do texto digitado.
            if (g_keyboardOpen) {
                static unsigned int cursorBlinkFrame = 0;
                ++cursorBlinkFrame;
                if ((cursorBlinkFrame / 30) % 2 == 0) {
                    int cursorX = 240 + (int)strlen(g_searchText) * 9;
                    if (cursorX > 766) cursorX = 766;
                    DrawRect(cursorX, 250, 2, 19, 0xFFFFFFFF, 120.0f);
                }
            }
            if (!g_keyboardOpen && focusArea == FOCUS_CONTENT)
                DrawFocusBorder(220, 241, 556, 40, 0xD8C4FFFF);
            if (g_keyboardOpen) {
                DrawText(475, 216, 6, 10, purple, LanguageText("D-PAD: MOVER  X: SELECIONAR  TRIÂNGULO: FECHAR", "D-PAD: MOVE  X: SELECT  TRIANGLE: CLOSE", "D-PAD: MOVER  X: ELEGIR  TRIÁNGULO: CERRAR"));
                DrawVirtualKeyboard();
            } else {
                DrawText(224, 322, 9, 15, purple, LanguageText("X: ABRIR TECLADO VIRTUAL", "X: OPEN VIRTUAL KEYBOARD", "X: ABRIR TECLADO VIRTUAL"));
                DrawText(224, 315, 8, 13, purple, g_searchStatus[0] ? g_searchStatus : LanguageText("DIGITE E PRESSIONE OK PARA PESQUISAR.", "TYPE AND PRESS OK TO SEARCH.", "ESCRIBE Y PULSA OK PARA BUSCAR."));
                for (int i = 0; i < g_resultCount && i < 5; ++i) {
                    char line[180];
                    snprintf(line, sizeof(line), "%d. %s - %s", i + 1, g_resultTitle[i], g_resultArtist[i]);
                    DrawText(224, 340 + i * 17, 7, 12, white, line);
                }
            }

        } else if (page == 2) {
            DrawTextBold(224, 180, 16, 27, white, LanguageText("SEUS ARTISTAS FAVORITOS", "YOUR FAVORITE ARTISTS", "TUS ARTISTAS FAVORITOS"));
            DrawText(224, 220, 10, 17, grey, LanguageText("Quando sua biblioteca estiver conectada, seus artistas", "When your library is connected, your artists", "Cuando conectes tu biblioteca, tus artistas"));
            DrawText(224, 243, 10, 17, grey, LanguageText("aparecerão organizados aqui.", "will appear here.", "aparecerán aquí."));
            DrawCoverArt(224, 293, 72, 1);
            DrawCoverArt(309, 293, 72, 2);
            DrawCoverArt(394, 293, 72, 3);
            DrawCoverArt(479, 293, 72, 4);
            if (focusArea == FOCUS_CONTENT) {
                const float xs[4] = {220.0f, 305.0f, 390.0f, 475.0f};
                DrawFocusBorder(xs[contentIndex % 4], 289, 80, 80, 0xD8C4FFFF);
            }

        } else if (page == 3) {
            DrawTextBold(224, 180, 16, 27, white, LanguageText("SUA COLEÇÃO DE ÁLBUNS", "YOUR ALBUM COLLECTION", "TU COLECCIÓN DE ÁLBUMES"));
            DrawText(224, 220, 10, 17, grey, LanguageText("Capas, lançamentos e álbuns salvos em um só lugar.", "Covers, releases and saved albums in one place.", "Portadas, lanzamientos y álbumes guardados en un solo lugar."));
            DrawCoverArt(224, 270, 82, 0);
            DrawCoverArt(321, 270, 82, 1);
            DrawCoverArt(418, 270, 82, 2);
            DrawCoverArt(515, 270, 82, 3);
            DrawCoverArt(612, 270, 82, 4);
            if (focusArea == FOCUS_CONTENT) {
                const float xs[5] = {220.0f, 317.0f, 414.0f, 511.0f, 608.0f};
                DrawFocusBorder(xs[contentIndex % 5], 266, 90, 90, 0xD8C4FFFF);
            }

        } else if (page == 4) {
            DrawTextBold(224, 180, 16, 27, white, LanguageText("PLAYLISTS PARA CADA MOMENTO", "PLAYLISTS FOR EVERY MOMENT", "LISTAS PARA CADA MOMENTO"));
            DrawText(224, 220, 10, 17, grey, LanguageText("Organize suas músicas do seu jeito.", "Organize your music your way.", "Organiza tu música a tu manera."));
            DrawCoverArt(224, 270, 82, 2);
            DrawCoverArt(321, 270, 82, 4);
            DrawCoverArt(418, 270, 82, 0);
            DrawCoverArt(515, 270, 82, 3);
            DrawCoverArt(612, 270, 82, 1);
            if (focusArea == FOCUS_CONTENT) {
                const float xs[5] = {220.0f, 317.0f, 414.0f, 511.0f, 608.0f};
                DrawFocusBorder(xs[contentIndex % 5], 266, 90, 90, 0xD8C4FFFF);
            }

        } else if (page == 5) {
            DrawTextBold(224, 180, 16, 27, white, LanguageText("CONFIGURAÇÕES", "SETTINGS", "AJUSTES"));
            DrawText(224, 218, 10, 17, grey, LanguageText("IDIOMA DO APLICATIVO", "APP LANGUAGE", "IDIOMA DE LA APLICACIÓN"));

            const char *languageNames[3] = { "PORTUGUÊS", "ENGLISH", "ESPAÑOL" };
            const float languageX[3] = { 224.0f, 420.0f, 616.0f };
            for (int i = 0; i < 3; ++i) {
                u32 boxColor = (g_language == i) ? 0x49336AFF : 0x242033FF;
                DrawRect(languageX[i], 255, 174, 54, boxColor, 180.0f);
                DrawTextBold(languageX[i] + 15, 274, 9, 16,
                             (g_language == i) ? white : grey, languageNames[i]);
                if (g_language == i)
                    DrawFocusBorder(languageX[i] - 2, 253, 178, 58, 0xD8C4FFFF);
            }

            DrawText(224, 342, 9, 15, purple, LanguageText("ESQUERDA / DIREITA: ALTERAR IDIOMA", "LEFT / RIGHT: CHANGE LANGUAGE", "IZQUIERDA / DERECHA: CAMBIAR IDIOMA"));
            DrawText(224, 371, 9, 15, grey, LanguageText("A ESCOLHA FICA SALVA NO CONSOLE.", "YOUR CHOICE IS SAVED ON THE CONSOLE.", "TU ELECCIÓN SE GUARDA EN LA CONSOLA."));
            DrawText(224, 402, 8, 13, grey, LanguageText("TRIÂNGULO: VOLTAR", "TRIANGLE: BACK", "TRIÁNGULO: VOLVER"));
            if (focusArea == FOCUS_CONTENT)
                DrawFocusBorder(190, 147, 640, 282, 0xD8C4FFFF);

        } else if (page == 6) {
            DrawText(224, 211, 11, 19, purple, LanguageText("DESENVOLVIDO POR:", "DEVELOPED BY:", "DESARROLLADO POR:"));
            DrawTextBold(224, 250, 19, 31, white, "MARCELO GUILHERME");
            DrawText(224, 334, 9, 15, grey, LanguageText("OBRIGADO POR CONHECER O JEZZ.", "THANK YOU FOR USING JEZZ.", "GRACIAS POR CONOCER JEZZ."));
            if (focusArea == FOCUS_CONTENT)
                DrawFocusBorder(190, 147, 640, 282, 0xD8C4FFFF);

        } else if (page == 7) {
            DrawText(224, 180, 9, 15, purple, LanguageText("CONTRIBUIÇÃO VOLUNTÁRIA", "VOLUNTARY SUPPORT", "APOYO VOLUNTARIO"));
            DrawText(224, 211, 10, 17, grey, LanguageText("A contribuição é opcional e não é obrigatória.", "Contributions are optional and never required.", "La contribución es opcional y no es obligatoria."));
            DrawText(224, 240, 10, 17, grey, LanguageText("O JEZZ será sempre gratuito para todos.", "JEZZ will always be free for everyone.", "JEZZ siempre será gratis para todos."));

            DrawRect(224, 278, 560, 1, 0x302442FF, 180.0f);
            DrawTextBold(224, 296, 11, 19, purple, "PIX:");

            DrawRect(224, 323, 460, 42, 0x242033FF, 180.0f);
            DrawRect(224, 323, 3, 42, 0x9560FFFF, 100.0f);
            DrawTextBold(240, 335, 10, 18, white, "Marceloguilher08062007me@gmail.com");

            DrawText(224, 391, 9, 15, grey, LanguageText("OBRIGADO POR APOIAR O PROJETO!", "THANK YOU FOR SUPPORTING THE PROJECT!", "GRACIAS POR APOYAR EL PROYECTO!"));
            if (focusArea == FOCUS_CONTENT)
                DrawFocusBorder(190, 147, 640, 282, 0xD8C4FFFF);
        }
    }

    DrawPlayerBar(purple, white, grey, focusArea, playerIndex, volume);
}


static void DrawExitCountdown(int heldFrames) {
    if (heldFrames <= 0)
        return;

    const int requiredFrames = 300; // 5 segundos a 60 quadros por segundo.
    int remaining = (requiredFrames - heldFrames + 59) / 60;
    if (remaining < 1)
        remaining = 1;

    DrawRect(605, 493, 105, 17, 0x211A30FF, 65.0f);
    DrawRect(605, 493, 105.0f * (float)heldFrames / (float)requiredFrames,
             2, 0x9560FFFF, 55.0f);

    char label[24];
    snprintf(label, sizeof(label), LanguageText("SAIR: %ds", "EXIT: %ds", "SALIR: %ds"), remaining);
    DrawText(615, 497, 7, 11, 0xFFFFFFFF, label);
}




// =========================================================
// ABERTURA SONORA SUAVE - JEZZ
// =========================================================
static bool g_audioInitialized = false;
static bool g_audioPortOpened = false;
static bool g_audioQueueCreated = false;
static bool g_audioQueueConnected = false;
static bool g_audioPortStarted = false;
static bool g_audioReady = false;

static u32 g_audioPort = 0;
static sys_event_queue_t g_audioQueue;
static sys_ipc_key_t g_audioQueueKey;
static audioPortConfig g_audioConfig;

static u64 g_audioSamplesRemaining = 96000;
static u64 g_audioSamplesPlayed = 0;
static u32 g_audioNextBlock = 0;
static double g_audioPhase = 0.0;

static void AudioStop(void) {
    g_audioReady = false;

    if (g_audioPortStarted) {
        audioPortStop(g_audioPort);
        g_audioPortStarted = false;
    }

    if (g_audioQueueConnected) {
        audioRemoveNotifyEventQueue(g_audioQueueKey);
        g_audioQueueConnected = false;
    }

    if (g_audioPortOpened) {
        audioPortClose(g_audioPort);
        g_audioPortOpened = false;
    }

    if (g_audioQueueCreated) {
        sysEventQueueDestroy(g_audioQueue, 0);
        g_audioQueueCreated = false;
    }

    if (g_audioInitialized) {
        audioQuit();
        g_audioInitialized = false;
    }
}

static bool AudioStart(void) {
    audioPortParam param;
    memset(&param, 0, sizeof(param));

    param.numChannels = AUDIO_PORT_2CH;
    param.numBlocks = AUDIO_BLOCK_8;
    param.attrib = AUDIO_PORT_INITLEVEL;
    param.level = 1.0f;

    if (audioInit() != 0)
        goto fail;
    g_audioInitialized = true;

    if (audioPortOpen(&param, &g_audioPort) != 0)
        goto fail;
    g_audioPortOpened = true;

    if (audioGetPortConfig(g_audioPort, &g_audioConfig) != 0)
        goto fail;

    if (audioCreateNotifyEventQueue(&g_audioQueue, &g_audioQueueKey) != 0)
        goto fail;
    g_audioQueueCreated = true;

    if (audioSetNotifyEventQueue(g_audioQueueKey) != 0)
        goto fail;
    g_audioQueueConnected = true;

    sysEventQueueDrain(g_audioQueue);

    if (audioPortStart(g_audioPort) != 0)
        goto fail;
    g_audioPortStarted = true;

    g_audioSamplesRemaining = 96000;
    g_audioSamplesPlayed = 0;
    g_audioNextBlock = 0;
    g_audioPhase = 0.0;
    g_audioReady = true;
    return true;

fail:
    AudioStop();
    return false;
}

static void AudioPump(void) {
    if (!g_audioReady)
        return;

    for (int eventIndex = 0; eventIndex < AUDIO_BLOCK_8; ++eventIndex) {
        sys_event_t event;

        if (sysEventQueueReceive(g_audioQueue, &event, 1) != 0)
            break;

        const u32 block = g_audioNextBlock;
        g_audioNextBlock = (g_audioNextBlock + 1) % g_audioConfig.numBlocks;

        float *buffer = (float *)(u64)(
            g_audioConfig.audioDataStart +
            block * g_audioConfig.channelCount *
            AUDIO_BLOCK_SAMPLES * sizeof(float)
        );

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
            float sample = 0.0f;

            if (g_audioSamplesRemaining > 0) {
                const double elapsed = (double)g_audioSamplesPlayed / 48000.0;
                const double remaining = (double)g_audioSamplesRemaining / 48000.0;

                double envelope = elapsed / 0.55;
                if (envelope > 1.0) envelope = 1.0;
                double fadeOut = remaining / 0.65;
                if (fadeOut < envelope) envelope = fadeOut;
                if (envelope < 0.0) envelope = 0.0;

                const double chord =
                    sin(g_audioPhase) +
                    0.65 * sin(g_audioPhase * 1.25) +
                    0.45 * sin(g_audioPhase * 1.5);

                sample = (float)(0.022 * envelope * chord);
                g_audioPhase += 6.283185307179586 * 220.0 / 48000.0;

                if (g_audioPhase >= 6.283185307179586)
                    g_audioPhase -= 6.283185307179586;

                ++g_audioSamplesPlayed;
                --g_audioSamplesRemaining;
            }

            buffer[i * 2] = sample;
            buffer[i * 2 + 1] = sample;
        }
    }
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
    AudioStart();
    LoadSettings();
    // A pilha de rede do sistema deve estar ativa para o libcurl.
    // Inicializa a pilha de rede do PS3.
    if (sysModuleLoad(SYSMODULE_NET) == 0) {
        g_netModuleLoaded = true;
        if (sysModuleLoad(SYSMODULE_NETCTL) == 0) {
            g_netCtlModuleLoaded = true;
            if (netInitialize() == 0) {
                g_netInitialized = true;
                if (netCtlInit() == 0)
                    g_netCtlInitialized = true;
            }
        }
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Diagnóstico temporário da fonte de aleatoriedade do PS3.
    {
        unsigned char randomBytes[16] = {0};
        u32 randomResult = sysGetRandomNumber(randomBytes, sizeof(randomBytes));

        FILE *randomLog = fopen(
            "/dev_hdd0/game/JEZZ00001/USRDIR/JEZZ_RANDOM_TEST.txt", "w"
        );

        if (randomLog) {
            fprintf(randomLog, "sysGetRandomNumber retorno: %u (0x%08X)\n",
                    randomResult, randomResult);

            fprintf(randomLog, "Bytes: ");
            for (int i = 0; i < 16; i++)
                fprintf(randomLog, "%02X", randomBytes[i]);

            fprintf(randomLog, "\n");
            fclose(randomLog);
        }
    }


    if (sysModuleLoad(SYSMODULE_PNGDEC) == 0) {
        g_pngModuleLoaded = true;
        LoadLogoTexture();
    }

    PlayBootAnimation();
    InitFont();

    int selected = 0;
    int page = 0;
    int focusArea = FOCUS_SIDEBAR;
    int contentRow = 0;
    int contentIndex = 0;
    int playerIndex = 1;
    int volume = 65;
    int exitHoldFrames = 0;

    bool previousUp = false;
    bool previousDown = false;
    bool previousLeft = false;
    bool previousRight = false;
    bool previousCross = false;
    bool previousTriangle = false;
    bool previousL1 = false;
    bool previousR1 = false;
    bool previousSelect = false;
    bool previousL2 = false;
    bool previousR2 = false;

    padInfo padinfo;
    padData pad;

    while (1) {

        AudioPump();
        memset(&padinfo, 0, sizeof(padinfo));
        memset(&pad, 0, sizeof(pad));

        ioPadGetInfo(&padinfo);
        if (padinfo.status[0])
            ioPadGetData(0, &pad);

        const bool up = pad.BTN_UP != 0;
        const bool down = pad.BTN_DOWN != 0;
        const bool left = pad.BTN_LEFT != 0;
        const bool right = pad.BTN_RIGHT != 0;
        const bool cross = pad.BTN_CROSS != 0;
        const bool circle = pad.BTN_CIRCLE != 0;
        const bool triangle = pad.BTN_TRIANGLE != 0;
        const bool l1 = pad.BTN_L1 != 0;
        const bool r1 = pad.BTN_R1 != 0;
        const bool select = pad.BTN_SELECT != 0;
        const bool l2 = pad.BTN_L2 != 0;
        const bool r2 = pad.BTN_R2 != 0;

        // A busca roda em uma thread separada para nao congelar a interface.
        if (g_searchThreadActive && !g_searchBusy) {
            u64 threadResult = 0;
            sysThreadJoin(g_searchThread, &threadResult);
            g_searchThreadActive = false;
        }
        if (g_searchRequested && !g_searchBusy && !g_searchThreadActive) {
            g_searchRequested = false;
            strncpy(g_searchQuery, g_searchText, sizeof(g_searchQuery) - 1);
            g_searchQuery[sizeof(g_searchQuery) - 1] = 0;
            g_searchBusy = true;
            if (sysThreadCreate(&g_searchThread, AudiusSearchThread, g_searchQuery,
                                1001, 0x40000, THREAD_JOINABLE, (char *)"JEZZ_AUDIUS") == 0) {
                g_searchThreadActive = true;
            } else {
                g_searchBusy = false;
                snprintf(g_searchStatus, sizeof(g_searchStatus), "%s", LanguageText("NAO FOI POSSIVEL INICIAR A BUSCA.", "COULD NOT START SEARCH.", "NO SE PUDO INICIAR LA BUSQUEDA."));
            }
        }

        if (g_keyboardOpen) {
            if (pad.BTN_START && !g_previousStart && g_searchText[0] != '\0') {
                g_searchRequested = true;
                g_keyboardOpen = false;
            }

            if (up && !previousUp) g_keyboardRow = (g_keyboardRow + 3) % 4;
            if (down && !previousDown) g_keyboardRow = (g_keyboardRow + 1) % 4;
            if (left && !previousLeft) g_keyboardCol = (g_keyboardCol + 9) % 10;
            if (right && !previousRight) g_keyboardCol = (g_keyboardCol + 1) % 10;
            if (cross && !previousCross) {
                const char *key = g_keyboardKeys[g_keyboardRow][g_keyboardCol];
                size_t n = strlen(g_searchText);
                if (g_keyboardRow == 3 && g_keyboardCol == 9) {
                    g_searchRequested = g_searchText[0] != '\0';
                    g_keyboardOpen = false;
                } else if (g_keyboardRow == 3 && g_keyboardCol == 8) {
                    g_searchText[0] = '\0';
                    g_searchRequested = false;
                } else if (g_keyboardRow == 3 && g_keyboardCol == 7) {
                    if (n > 0) g_searchText[n - 1] = '\0';
                } else if (g_keyboardRow == 3 && g_keyboardCol == 6) {
                    if (n + 1 < sizeof(g_searchText)) { g_searchText[n] = ' '; g_searchText[n + 1] = '\0'; }
                } else if (n + 1 < sizeof(g_searchText)) {
                    g_searchText[n] = key[0];
                    g_searchText[n + 1] = '\0';
                }
            }
            if (triangle && !previousTriangle) g_keyboardOpen = false;
        } else {
        // L1/R1 percorrem as seis secoes do JEZZ.
        if (l1 && !previousL1) {
            selected = (selected + MENU_COUNT - 1) % MENU_COUNT;
            page = selected;
            g_keyboardOpen = false;
            focusArea = FOCUS_CONTENT;
            contentRow = 0;
            contentIndex = 0;
        }

        if (r1 && !previousR1) {
            selected = (selected + 1) % MENU_COUNT;
            page = selected;
            g_keyboardOpen = false;
            focusArea = FOCUS_CONTENT;
            contentRow = 0;
            contentIndex = 0;
        }

        // SELECT leva diretamente ao player inferior e volta ao conteudo.
        if (select && !previousSelect) {
            if (focusArea == FOCUS_PLAYER)
                focusArea = FOCUS_CONTENT;
            else
                focusArea = FOCUS_PLAYER;
        }

        if (focusArea == FOCUS_SIDEBAR) {
            if (up && !previousUp) {
                selected = (selected + MENU_COUNT - 1) % MENU_COUNT;
            }
            if (down && !previousDown) {
                selected = (selected + 1) % MENU_COUNT;
            }
            if (right && !previousRight) {
                page = selected;
                focusArea = FOCUS_CONTENT;
                contentRow = 0;
                contentIndex = 0;
            }
            if (cross && !previousCross) {
                page = selected;
                focusArea = FOCUS_CONTENT;
                contentRow = 0;
                contentIndex = 0;
            }
        }
        else if (focusArea == FOCUS_CONTENT) {
            if (page == 0) {
                if (up && !previousUp) {
                    if (contentRow == 1) {
                        contentRow = 0;
                        contentIndex = 0;
                    } else {
                        focusArea = FOCUS_SIDEBAR;
                    }
                }
                if (down && !previousDown) {
                    if (contentRow == 0) {
                        contentRow = 1;
                        contentIndex = 0;
                    } else {
                        focusArea = FOCUS_PLAYER;
                        playerIndex = 1;
                    }
                }
                if (left && !previousLeft) {
                    if (contentRow == 1 && contentIndex > 0)
                        --contentIndex;
                    else
                        focusArea = FOCUS_SIDEBAR;
                }
                if (right && !previousRight) {
                    if (contentRow == 0) {
                        contentRow = 1;
                        contentIndex = 0;
                    } else if (contentIndex < 2) {
                        ++contentIndex;
                    }
                }
                if (cross && !previousCross) {
                    if (contentRow == 0) {
                        selected = 1;
                        page = 1;
                        contentRow = 0;
                        contentIndex = 0;
                    } else if (contentIndex == 0) {
                        selected = 3;
                        page = 3;
                        contentIndex = 0;
                    } else if (contentIndex == 1) {
                        selected = 2;
                        page = 2;
                        contentIndex = 0;
                    } else {
                        selected = 4;
                        page = 4;
                        contentIndex = 0;
                    }
                }
            } else if (page == 1) {
                if (cross && !previousCross) {
                    g_keyboardOpen = true;
                    g_keyboardRow = 0;
                    g_keyboardCol = 0;
                    g_searchRequested = false;
                }
                if (left && !previousLeft) focusArea = FOCUS_SIDEBAR;
                if (down && !previousDown) focusArea = FOCUS_PLAYER;
                if (up && !previousUp) focusArea = FOCUS_SIDEBAR;
            } else if (page == 2 || page == 3 || page == 4) {
                int maxIndex = (page == 2) ? 3 : 4;
                if (left && !previousLeft) {
                    if (contentIndex > 0)
                        --contentIndex;
                    else
                        focusArea = FOCUS_SIDEBAR;
                }
                if (right && !previousRight && contentIndex < maxIndex)
                    ++contentIndex;
                if (down && !previousDown)
                    focusArea = FOCUS_PLAYER;
                if (up && !previousUp)
                    focusArea = FOCUS_SIDEBAR;
            } else if (page == 5) {
                bool languageChanged = false;
                if (left && !previousLeft) {
                    g_language = (g_language + 2) % 3;
                    languageChanged = true;
                }
                if (right && !previousRight) {
                    g_language = (g_language + 1) % 3;
                    languageChanged = true;
                }
                if (cross && !previousCross) {
                    g_language = (g_language + 1) % 3;
                    languageChanged = true;
                }
                if (languageChanged)
                    SaveSettings();
                if (down && !previousDown)
                    focusArea = FOCUS_PLAYER;
                if (up && !previousUp)
                    focusArea = FOCUS_SIDEBAR;

            } else {
                if (left && !previousLeft)
                    focusArea = FOCUS_SIDEBAR;
                if (down && !previousDown)
                    focusArea = FOCUS_PLAYER;
                if (up && !previousUp)
                    focusArea = FOCUS_SIDEBAR;
            }
        }
        else if (focusArea == FOCUS_PLAYER) {
            if (left && !previousLeft)
                playerIndex = (playerIndex + 3) % 4;
            if (right && !previousRight)
                playerIndex = (playerIndex + 1) % 4;
            if (up && !previousUp) {
                if (playerIndex == 3)
                    volume = (volume + 5 > 100) ? 100 : volume + 5;
                else
                    focusArea = FOCUS_CONTENT;
            }
            if (down && !previousDown) {
                if (playerIndex == 3)
                    volume = (volume - 5 < 0) ? 0 : volume - 5;
            }
            // O player ainda nao tem motor de audio: X apenas seleciona o controle.
            if (cross && !previousCross) {
                if (playerIndex == 1)
                    playerIndex = 1;
            }
        }

        // L2/R2 ajustam o volume sem precisar entrar no controle deslizante.
        if (l2 && !previousL2)
            volume = (volume - 5 < 0) ? 0 : volume - 5;
        if (r2 && !previousR2)
            volume = (volume + 5 > 100) ? 100 : volume + 5;

        // TRIANGULO sempre retorna diretamente para a tela inicial.
        if (triangle && !previousTriangle) {
            selected = 0;
            page = 0;
            g_keyboardOpen = false;
            focusArea = FOCUS_SIDEBAR;
            contentRow = 0;
            contentIndex = 0;
        }

        // A saida exige manter CIRCULO pressionado por cinco segundos.
        // Soltar o botao antes do fim cancela a contagem.
        if (circle) {
            if (exitHoldFrames < 300)
                ++exitHoldFrames;
            if (exitHoldFrames >= 300)
                break;
        } else {
            exitHoldFrames = 0;
        }

        } // fim do controle normal quando o teclado virtual esta fechado

        previousUp = up;
        previousDown = down;
        previousLeft = left;
        previousRight = right;
        g_previousStart = pad.BTN_START != 0;
        previousCross = cross;
        previousTriangle = triangle;
        previousL1 = l1;
        previousR1 = r1;
        previousSelect = select;
        previousL2 = l2;
        previousR2 = r2;

        tiny3d_Clear(0x090811FF, TINY3D_CLEAR_ALL);
        Setup2D();
        DrawScreen(
            selected,
            page,
            focusArea,
            contentRow,
            contentIndex,
            playerIndex,
            volume
        );
        DrawExitCountdown(exitHoldFrames);
        tiny3d_Flip();
    }

    AudioStop();
    ioPadEnd();

    if (g_searchThreadActive) {
        u64 threadResult = 0;
        sysThreadJoin(g_searchThread, &threadResult);
        g_searchThreadActive = false;
    }
    curl_global_cleanup();

    if (g_netCtlInitialized)
        netCtlTerm();

    if (g_netInitialized)
        netDeinitialize();

    if (g_netCtlModuleLoaded)
        sysModuleUnload(SYSMODULE_NETCTL);

    if (g_netModuleLoaded)
        sysModuleUnload(SYSMODULE_NET);

    if (g_pngModuleLoaded)
        sysModuleUnload(SYSMODULE_PNGDEC);

    return 0;
}
