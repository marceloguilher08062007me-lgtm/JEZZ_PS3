
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>

#include <ppu-types.h>
#include <ppu-lv2.h>
#include <sysutil/video.h>
#include <rsx/gcm_sys.h>
#include <rsx/rsx.h>
#include <io/pad.h>

#define MAX_BUFFERS 2
#define HOST_SIZE (32 * 1024 * 1024)
#define CB_SIZE 0x100000

#define BLACK   0x00000000
#define PANEL   0x00151220
#define PANEL2  0x0020192B
#define PURPLE  0x008A42E8
#define PURPLE2 0x00B46CFF
#define WHITE   0x00FFFFFF
#define GREY    0x009A96A5

typedef struct {
    u32 *ptr;
    u32 offset;
    u16 width;
    u16 height;
    int id;
} ScreenBuffer;

static const unsigned char font[36][7] = {
    {14,17,17,31,17,17,17},
    {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31},
    {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},
    {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},
    {7,2,2,2,2,18,12},
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
    {31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14},
    {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},
    {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},
    {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}
};

static int glyph_index(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return 26 + c - '0';
    return -1;
}

static void pixel(ScreenBuffer *b, int x, int y, u32 color)
{
    if (x < 0 || y < 0 || x >= b->width || y >= b->height)
        return;

    b->ptr[y * b->width + x] = color;
}

static void rectangle(ScreenBuffer *b, int x, int y,
                      int w, int h, u32 color)
{
    int xx, yy;

    for (yy = y; yy < y + h; yy++)
        for (xx = x; xx < x + w; xx++)
            pixel(b, xx, yy, color);
}

static void text(ScreenBuffer *b, int x, int y,
                 const char *str, int scale, u32 color)
{
    int i, row, col;

    for (i = 0; str[i]; i++) {
        int index = glyph_index(str[i]);

        if (index >= 0) {
            for (row = 0; row < 7; row++) {
                for (col = 0; col < 5; col++) {
                    if (font[index][row] & (1 << (4 - col))) {
                        rectangle(b,
                                  x + col * scale,
                                  y + row * scale,
                                  scale, scale, color);
                    }
                }
            }
        }

        x += 6 * scale;
    }
}

static void wait_rsx_idle(gcmContextData *context)
{
    u32 value = 1;

    rsxSetWriteBackendLabel(context, 255, value);
    rsxSetWaitLabel(context, 255, value);

    value++;

    rsxSetWriteBackendLabel(context, 255, value);
    rsxFlushBuffer(context);

    while (*(volatile u32 *)gcmGetLabelAddress(255) != value)
        usleep(30);
}

static int init_screen(gcmContextData **context,
                       void *host_addr,
                       u16 *width,
                       u16 *height)
{
    videoState state;
    videoResolution resolution;
    videoConfiguration config;

    if (rsxInit(context, CB_SIZE, HOST_SIZE, host_addr) != 0)
        return 0;

    if (*context == NULL)
        return 0;

    if (videoGetState(0, 0, &state) != 0)
        return 0;

    if (state.state != 0)
        return 0;

    if (videoGetResolution(state.displayMode.resolution,
                           &resolution) != 0)
        return 0;

    *width = resolution.width;
    *height = resolution.height;

    memset(&config, 0, sizeof(config));

    config.resolution = state.displayMode.resolution;
    config.format = VIDEO_BUFFER_FORMAT_XRGB;
    config.pitch = resolution.width * sizeof(u32);
    config.aspect = state.displayMode.aspect;

    wait_rsx_idle(*context);

    if (videoConfigure(0, &config, NULL, 0) != 0)
        return 0;

    gcmSetFlipMode(GCM_FLIP_VSYNC);
    gcmResetFlipStatus();

    return 1;
}

static int create_buffer(ScreenBuffer *b,
                         u16 width, u16 height, int id)
{
    u32 size = width * height * sizeof(u32);
    u32 pitch = width * sizeof(u32);

    b->ptr = (u32 *)rsxMemalign(64, size);

    if (!b->ptr)
        return 0;

    if (rsxAddressToOffset(b->ptr, &b->offset) != 0) {
        rsxFree(b->ptr);
        return 0;
    }

    if (gcmSetDisplayBuffer(id, b->offset,
                            pitch, width, height) != 0) {
        rsxFree(b->ptr);
        return 0;
    }

    b->width = width;
    b->height = height;
    b->id = id;

    memset(b->ptr, 0, size);

    return 1;
}

static void wait_flip(void)
{
    while (gcmGetFlipStatus() != 0)
        usleep(200);

    gcmResetFlipStatus();
}

static void show_buffer(gcmContextData *context, int id)
{
    gcmSetFlip(context, id);
    rsxFlushBuffer(context);
    gcmSetWaitFlip(context);
}

static const char *menu_items[] = {
    "HOME",
    "SEARCH",
    "ARTISTS",
    "ALBUMS",
    "PLAYLISTS",
    "SETTINGS"
};

static void draw_interface(ScreenBuffer *b, int selected)
{
    int w = b->width;
    int h = b->height;
    int scale = 3;
    int i;
    int sidebar_x = w / 20;
    int sidebar_y = h / 4;
    int sidebar_w = w / 4;
    int item_h = h / 13;

    rectangle(b, 0, 0, w, h, BLACK);

    /* Top bar */
    rectangle(b, 0, 0, w, h / 7, PANEL);
    rectangle(b, 0, h / 7 - 4, w, 4, PURPLE);

    text(b, sidebar_x, h / 25, "JEZZ", 5, WHITE);
    text(b, sidebar_x + 150, h / 16, "MUSIC", 2, PURPLE2);

    /* Sidebar */
    rectangle(b, sidebar_x, sidebar_y,
              sidebar_w, h * 3 / 5, PANEL);

    text(b, sidebar_x + 24, sidebar_y + 20,
         "LIBRARY", 2, GREY);

    for (i = 0; i < 6; i++) {
        int y = sidebar_y + 65 + i * item_h;

        if (i == selected) {
            rectangle(b, sidebar_x + 10, y - 12,
                      sidebar_w - 20, item_h - 4, PURPLE);
        }

        text(b, sidebar_x + 30, y,
             menu_items[i], scale,
             i == selected ? WHITE : GREY);
    }

    /* Main content panel */
    {
        int x = sidebar_x + sidebar_w + 35;
        int y = sidebar_y;
        int panel_w = w - x - sidebar_x;
        int panel_h = h * 3 / 5;

        rectangle(b, x, y, panel_w, panel_h, PANEL);

        text(b, x + 35, y + 35,
             menu_items[selected], 4, WHITE);

        rectangle(b, x + 35, y + 95,
                  panel_w - 70, 3, PURPLE);

        if (selected == 0) {
            text(b, x + 35, y + 145,
                 "WELCOME TO JEZZ", 3, PURPLE2);

            text(b, x + 35, y + 205,
                 "YOUR MUSIC STARTS HERE", 2, GREY);

            text(b, x + 35, y + 260,
                 "SEARCH AND DISCOVER MUSIC", 2, GREY);
        } else if (selected == 1) {
            text(b, x + 35, y + 155,
                 "ONLINE MUSIC SEARCH", 2, GREY);

            text(b, x + 35, y + 210,
                 "SEARCH FEATURE COMING SOON", 2, PURPLE2);
        } else if (selected == 2) {
            text(b, x + 35, y + 155,
                 "YOUR ARTISTS", 3, GREY);

            text(b, x + 35, y + 210,
                 "ARTIST LIBRARY COMING SOON", 2, PURPLE2);
        } else if (selected == 3) {
            text(b, x + 35, y + 155,
                 "YOUR ALBUMS", 3, GREY);

            text(b, x + 35, y + 210,
                 "ALBUM LIBRARY COMING SOON", 2, PURPLE2);
        } else if (selected == 4) {
            text(b, x + 35, y + 155,
                 "YOUR PLAYLISTS", 3, GREY);

            text(b, x + 35, y + 210,
                 "PLAYLISTS COMING SOON", 2, PURPLE2);
        } else {
            text(b, x + 35, y + 155,
                 "JEZZ SETTINGS", 3, GREY);

            text(b, x + 35, y + 210,
                 "SETTINGS COMING SOON", 2, PURPLE2);
        }
    }

    /* Footer */
    text(b, sidebar_x, h - 55,
         "UP DOWN MOVE", 2, GREY);

    text(b, sidebar_x + 250, h - 55,
         "X SELECT", 2, PURPLE2);

    text(b, w - 260, h - 55,
         "JEZZ PREVIEW", 2, GREY);
}

int main(void)
{
    gcmContextData *context = NULL;
    void *host_addr;
    ScreenBuffer buffers[MAX_BUFFERS];

    padInfo padinfo;
    padData paddata;

    u16 width = 0;
    u16 height = 0;

    int current_buffer = 0;
    int selected = 0;
    int previous_up = 0;
    int previous_down = 0;
    int previous_cross = 0;
    int previous_circle = 0;
    int i;

    memset(buffers, 0, sizeof(buffers));

    host_addr = memalign(1024 * 1024, HOST_SIZE);

    if (!host_addr)
        return 1;

    if (!init_screen(&context, host_addr, &width, &height)) {
        free(host_addr);
        return 1;
    }

    for (i = 0; i < MAX_BUFFERS; i++) {
        if (!create_buffer(&buffers[i], width, height, i)) {
            rsxFinish(context, 1);
            free(host_addr);
            return 1;
        }
    }

    ioPadInit(7);

    draw_interface(&buffers[0], selected);
    draw_interface(&buffers[1], selected);

    show_buffer(context, 0);
    current_buffer = 1;

    while (1) {
        int up = 0;
        int down = 0;
        int cross = 0;
        int circle = 0;

        memset(&padinfo, 0, sizeof(padinfo));
        memset(&paddata, 0, sizeof(paddata));

        ioPadGetInfo(&padinfo);

        if (padinfo.status[0]) {
            if (ioPadGetData(0, &paddata) == 0) {
                up = paddata.BTN_UP;
                down = paddata.BTN_DOWN;
                cross = paddata.BTN_CROSS;
                circle = paddata.BTN_CIRCLE;

                if (paddata.BTN_START)
                    break;
            }
        }

        if (up && !previous_up) {
            selected--;
            if (selected < 0)
                selected = 5;
        }

        if (down && !previous_down) {
            selected++;
            if (selected > 5)
                selected = 0;
        }

        if (circle && !previous_circle)
            selected = 0;

        if (cross && !previous_cross) {
            /* Opens the selected section.
               Content will be added in future builds. */
        }

        previous_up = up;
        previous_down = down;
        previous_cross = cross;
        previous_circle = circle;

        wait_flip();

        draw_interface(&buffers[current_buffer], selected);
        show_buffer(context, buffers[current_buffer].id);

        current_buffer++;
        if (current_buffer >= MAX_BUFFERS)
            current_buffer = 0;

        usleep(10000);
    }

    wait_flip();

    ioPadEnd();

    for (i = 0; i < MAX_BUFFERS; i++)
        rsxFree(buffers[i].ptr);

    rsxFinish(context, 1);
    free(host_addr);

    return 0;
}
