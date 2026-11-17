#include <io/pad.h>
#include <NoRSX.h>
#include <string.h>

static const int MENU_COUNT = 6;

static const char *menuItems[MENU_COUNT] = {
    "INICIO",
    "BUSCAR",
    "ARTISTAS",
    "ALBUNS",
    "PLAYLISTS",
    "AJUSTES"
};

static const char *pageTitles[MENU_COUNT] = {
    "INICIO",
    "BUSCAR",
    "ARTISTAS",
    "ALBUNS",
    "PLAYLISTS",
    "AJUSTES"
};

static void drawScreen(
    NoRSX *gfx,
    Background *background,
    Object *objects,
    Font *font,
    int selected,
    int page
) {
    const u32 purple = 0x9560FF;
    const u32 white = 0xFFFFFF;
    const u32 grey = 0xAAA5B8;

    background->Mono(0x090811);

    // Barra superior
    objects->Rectangle(0, 0, 1280, 96, 0x13111E);

    // Simbolo visual JEZZ
    objects->Rectangle(38, 34, 6, 25, purple);
    objects->Rectangle(49, 24, 6, 35, purple);
    objects->Rectangle(60, 30, 6, 29, purple);
    objects->Rectangle(71, 39, 6, 20, purple);

    font->Printf(91, 23, white, 48, "JEZZ");
    font->Printf(250, 39, grey, 20, "MUSIC PLAYER");

    // Barra lateral
    objects->Rectangle(0, 96, 270, 624, 0x0E0C17);

    font->Printf(35, 125, grey, 17, "MENU PRINCIPAL");

    // Navegacao
    for (int i = 0; i < MENU_COUNT; i++) {
        u32 color = grey;

        if (i == selected) {
            objects->Rectangle(
                20,
                151 + i * 72,
                225,
                53,
                0x35234F
            );

            objects->Rectangle(
                20,
                151 + i * 72,
                5,
                53,
                purple
            );

            color = white;
        }

        font->Printf(
            48,
            165 + i * 72,
            color,
            24,
            "%s",
            menuItems[i]
        );
    }

    font->Printf(35, 652, purple, 18, "JEZZ v0.1");
    font->Printf(35, 678, grey, 14, "PLAY YOUR WAY");

    // Conteudo principal
    if (page == 0) {
        font->Printf(320, 145, white, 40, "BEM-VINDO AO JEZZ");

        objects->Rectangle(
            320, 210, 910, 205, 0x191425
        );

        objects->Rectangle(
            320, 210, 8, 205, purple
        );

        font->Printf(
            365, 260, purple, 21, "SUA EXPERIENCIA MUSICAL"
        );

        font->Printf(
            365, 305, white, 38, "Sua musica. Seu espaco."
        );

        font->Printf(
            365, 354, grey, 21, "O JEZZ esta sendo preparado."
        );

        font->Printf(
            320, 460, white, 25, "SUA BIBLIOTECA"
        );

        objects->Rectangle(
            320, 505, 280, 135, 0x171421
        );

        objects->Rectangle(
            620, 505, 280, 135, 0x171421
        );

        objects->Rectangle(
            920, 505, 310, 135, 0x171421
        );

        font->Printf(
            345, 535, purple, 19, "MUSICAS"
        );

        font->Printf(
            345, 575, grey, 18, "Biblioteca vazia"
        );

        font->Printf(
            645, 535, purple, 19, "ARTISTAS"
        );

        font->Printf(
            645, 575, grey, 18, "Explore artistas"
        );

        font->Printf(
            945, 535, purple, 19, "PLAYLISTS"
        );

        font->Printf(
            945, 575, grey, 18, "Suas playlists"
        );
    } else {
        font->Printf(
            320, 145, white, 42, "%s", pageTitles[page]
        );

        objects->Rectangle(
            320, 220, 910, 310, 0x171421
        );

        objects->Rectangle(
            320, 220, 8, 310, purple
        );

        if (page == 1) {
            font->Printf(
                365, 285, purple, 27, "BUSCAR MUSICAS"
            );

            font->Printf(
                365, 345, white, 25, "A busca sera integrada nas proximas etapas."
            );
        } else if (page == 2) {
            font->Printf(
                365, 285, purple, 27, "SEUS ARTISTAS"
            );

            font->Printf(
                365, 345, white, 25, "Seus artistas aparecerao aqui."
            );
        } else if (page == 3) {
            font->Printf(
                365, 285, purple, 27, "SEUS ALBUNS"
            );

            font->Printf(
                365, 345, white, 25, "Sua colecao de albuns aparecera aqui."
            );
        } else if (page == 4) {
            font->Printf(
                365, 285, purple, 27, "SUAS PLAYLISTS"
            );

            font->Printf(
                365, 345, white, 25, "Suas playlists aparecerao aqui."
            );
        } else if (page == 5) {
            font->Printf(
                365, 285, purple, 27, "CONFIGURACOES"
            );

            font->Printf(
                365, 345, white, 23, "JEZZ - Versao 0.1"
            );

            font->Printf(
                365, 390, grey, 20, "Cima/Baixo: navegar"
            );

            font->Printf(
                365, 425, grey, 20, "X: selecionar"
            );

            font->Printf(
                365, 460, grey, 20, "O: voltar ao inicio"
            );

            font->Printf(
                365, 495, grey, 20, "Triangulo: sair"
            );
        }
    }

    font->Printf(
        320, 690, grey, 15,
        "JEZZ  |  NAVEGUE COM O CONTROLE DO PS3"
    );

    (void)gfx;
}

int main(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;

    ioPadInit(7);

    NoRSX GFX(RESOLUTION_AUTO, RESOLUTION_1280x720);

    Background background(&GFX);
    Object objects(&GFX);
    Font font(JPN, &GFX);

    padInfo padinfo;
    padData pad;

    memset(&padinfo, 0, sizeof(padinfo));
    memset(&pad, 0, sizeof(pad));

    int selected = 0;
    int page = 0;

    bool previousUp = false;
    bool previousDown = false;
    bool previousCross = false;
    bool previousCircle = false;
    bool previousExit = false;

    GFX.AppStart();

    while (GFX.GetAppStatus() && !GFX.ExitSignalStatus()) {
        memset(&padinfo, 0, sizeof(padinfo));
        memset(&pad, 0, sizeof(pad));

        ioPadGetInfo(&padinfo);

        if (padinfo.status[0]) {
            ioPadGetData(0, &pad);
        }

        bool up = pad.BTN_UP != 0;
        bool down = pad.BTN_DOWN != 0;
        bool cross = pad.BTN_CROSS != 0;
        bool circle = pad.BTN_CIRCLE != 0;
        bool exitButton =
            pad.BTN_TRIANGLE != 0 || pad.BTN_START != 0;

        if (up && !previousUp) {
            selected--;

            if (selected < 0) {
                selected = MENU_COUNT - 1;
            }
        }

        if (down && !previousDown) {
            selected++;

            if (selected >= MENU_COUNT) {
                selected = 0;
            }
        }

        if (cross && !previousCross) {
            page = selected;
        }

        if (circle && !previousCircle) {
            selected = 0;
            page = 0;
        }

        if (exitButton && !previousExit) {
            GFX.AppExit();
        }

        previousUp = up;
        previousDown = down;
        previousCross = cross;
        previousCircle = circle;
        previousExit = exitButton;

        drawScreen(
            &GFX,
            &background,
            &objects,
            &font,
            selected,
            page
        );

        GFX.Flip();
    }

    ioPadEnd();

    return 0;
}
