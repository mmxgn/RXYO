/*
Simple Raylib and Miniaudio XY Oscilloscope
Copyright 2022 mmxgn <eruyome@gmail.com>

This is a simple demo on how Miniaudio and Raylib can be used to:

1. Capture audio from a user's microphone.
2. Paint the captured audio on screen using pixels where left channel specifies the x
   position of a pixel and the right channel the right position of a pixel.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
and associated documentation files (the "Software"), to deal in the Software without restriction, 
including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING 
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER 
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE 
OR OTHER DEALINGS IN THE SOFTWARE.
*/

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "raylib.h"
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <math.h>

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 8192

#define DEFAULT_SCREEN_WIDTH 800
#define DEFAULT_SCREEN_HEIGHT 800
#define DEFAULT_FPS 60
#define BACKGROUND_COLOR WHITE 
#define FOREGROUND_COLOR BLACK

#define KEY_MENU KEY_M

#define LOG_LEVEL LOG_DEBUG

typedef struct
{
    float buf[2][BUF_SIZE];

    int ptr; // Pointer to buffer 1 or 2
    int idx; // Pointer to position in buffer
} buffer_store_t;

// We keep all those parameters as a struct in order to
// pass it to different functions in the game loop, such as
// handle_keyboard, and handle_draw.
typedef struct
{
    int screen_width;
    int screen_height;
    int fps;
    int default_device;

    ma_context context;
    ma_device device;
    ma_device_config config;
    buffer_store_t buffer_store;
    RenderTexture2D xytexture;

    int menu_shown;
    int should_exit;
    int error_code;
} opt_t;

/*
    Those functions are implemented at the bottom.
*/

// You probably only need to touch those functions if only you change the buffer_store_t above
void data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount);
void initialize_buffer_store(buffer_store_t *buffer_store);

// Those are the two main functions you might want to use.
void handle_keyboard(opt_t *opt);
void handle_draw(opt_t *opt, buffer_store_t *buffer_store, RenderTexture2D *xytexture);

// UI functions
void draw_menu(opt_t *opt);

// Utility functions
float clamp(float x, const float min_x, const float max_x);
int min(int x, int y);
float length(float x0, float y0, float x1, float y1);

int main(int argc, char const *argv[])
{
    static opt_t opt;

    // Set default options
    opt.menu_shown = TRUE;
    opt.screen_width = DEFAULT_SCREEN_WIDTH;
    opt.screen_height = DEFAULT_SCREEN_HEIGHT;
    opt.fps = DEFAULT_FPS;
    opt.default_device = 0;

    // Set up logging
    SetTraceLogLevel(LOG_LEVEL);

    // Initialize buffer store to 0s
    initialize_buffer_store(&opt.buffer_store);

    // Audio set up
    // Use a default capture device, use numbers 1->9 to choose an input device afterwards

    // We pass this to the opt_t struct since we will use it to change devices on the fly.
    if (ma_context_init(NULL, 0, NULL, &opt.context) != MA_SUCCESS)
    {
        return -1;
    }

    opt.config = ma_device_config_init(ma_device_type_capture);
    opt.config.capture.format = ma_format_f32; // Set to ma_format_unknown to use the device's native format.
    opt.config.capture.channels = 2;           // Set to 0 to use the device's native channel count.
    opt.config.sampleRate = 48000;             // Set to 0 to use the device's native sample rate.
    opt.config.dataCallback = data_callback;   // This function will be called when miniaudio needs more data.
    opt.config.pUserData = &opt.buffer_store;  // Can be accessed from the device object (device.pUserData).
    if (ma_device_init(NULL, &opt.config, &opt.device) != MA_SUCCESS)
    {
        return -1;
    }
    ma_device_start(&opt.device); // The device is sleeping by default so you'll need to start it manually.

    // Graphics set up
    InitWindow(opt.screen_width, opt.screen_height, "Simple XY");

    // Between frames, we draw on this texture, then we show it on screen all at once.
    opt.xytexture = LoadRenderTexture(opt.screen_width, opt.screen_height);

    // main loop
    SetTargetFPS(opt.fps);

    while (!WindowShouldClose())
    {
        if (opt.should_exit == TRUE)
            break;
        handle_keyboard(&opt);
        handle_draw(&opt, &opt.buffer_store, &opt.xytexture);
    }
    CloseWindow();
    ma_device_uninit(&opt.device);

    return 0;
}

void data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
    /*
     * This function simply writes to the buffer pointed by ptr and updates the buffer
       index and nothing else.
     */
    buffer_store_t *buffer_store = (buffer_store_t *)pDevice->pUserData;
    float *x = (float *)pInput;

    for (int i = 0; i < 2 * frameCount; i++)
    {
        buffer_store->buf[buffer_store->ptr][i] = x[i];

        // Those are circular buffers, when the index goes to BUF_SIZE
        // it starts pointing back to the beginning of the buffer.
        // If we were dealing with an audio effect we should treat this as an issue
        // but in this application it is not critical (might lead to some graphical
        // glitches which might also be desirable ).
        buffer_store->idx = (buffer_store->idx + 1) % BUF_SIZE;
    }
}

void initialize_buffer_store(buffer_store_t *buffer_store)
{
    for (size_t i = 0; i < BUF_SIZE; i++)
    {
        buffer_store->buf[0][i] = 0.0;
        buffer_store->buf[1][i] = 0.0;
    }

    buffer_store->idx = 0;
    buffer_store->ptr = 0;
}

void handle_keyboard(opt_t *opt)
{
    int key_pressed = GetKeyPressed();
    if (key_pressed != 0)
    {

        TraceLog(LOG_DEBUG, "Pressed key: %d", key_pressed);

        if (key_pressed == KEY_MENU)
        {
            if (opt->menu_shown == TRUE)
            {
                opt->menu_shown = FALSE;
            }
            else
            {
                opt->menu_shown = TRUE;
            }
        }
        else if (48 <= key_pressed  && key_pressed <= 57) // 0->9 numerical keys
        {

            ma_device_info *pPlaybackInfos;
            ma_uint32 playbackCount;
            ma_device_info *pCaptureInfos;
            ma_uint32 captureCount;
            ma_device_uninit(&opt->device);
            if (ma_context_get_devices(&opt->context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) != MA_SUCCESS)
            {
                opt->error_code = 1;
                opt->should_exit = TRUE;
                return;
            }
            // Convert a key press to a number between 0 and 9
            int device_idx = key_pressed - 48;

            if (device_idx < captureCount)
            {
                opt->config.capture.pDeviceID = &pCaptureInfos[device_idx].id;
                if (ma_device_init(NULL, &opt->config, &opt->device) != MA_SUCCESS)
                {
                    opt->error_code = -1;
                    opt->should_exit = TRUE;
                    return;
                }
                ma_device_start(&opt->device);
            }
        }
    }
}

void handle_draw(opt_t *opt, buffer_store_t *buffer_store, RenderTexture2D *xytexture)
{

    BeginDrawing();
    ClearBackground(BACKGROUND_COLOR);

    // We update  xytexture on the bottom of this function
    DrawTextureEx(xytexture->texture, (Vector2){0, 0}, 0.0, 1.0, WHITE);

    // UI has to be drawn last since we overlay it over the xy curve
    if (opt->menu_shown == TRUE)
    {
        draw_menu(opt);
    }

    EndDrawing();

    // When this function is called do the following:
    // -> Set buffer_store->ptr to point to the other buffer
    //    so the audio callback doesn't accidentally overwrite
    //    the buffer we want to draw from.
    // -> Draw the contents of buffer_store->buf to the screen as xy coordinates.

    // Save the buffer we are going to read from
    float *buffer = opt->buffer_store.buf[opt->buffer_store.ptr];

    // Number of frames written to the buffer thus far (samples written / 2)
    int frameCount = opt->buffer_store.idx / 2;


    // Move buffer store pointer to the next buffer, will circle back to the buffer.
    opt->buffer_store.ptr = (opt->buffer_store.ptr + 1) % 2;
    opt->buffer_store.idx = 0;

    // Draw the points on buffer.
    BeginTextureMode(*xytexture);

    int x0 = 0, x1 = 0, x2 = 0, x3 = 0; // x positions at times n, n-1, and n-2
    int y0 = 0, y1 = 0, y2 = 0, y3 = 0; // y positions at times n, n-1, and n-2

    // "Refresh" the texture by painting BACKGROUND_COLOR over it. Only do that when
    // there are more than 0 frames to paint pixels/lines on. If we don't check for
    // frameCount, `handle_draw` will not paint anything at this frame and this will
    // look like "flickering" (try disabling the if statement below and see what happens)
    if (frameCount > 0)
        DrawRectangle(
            0,
            0,
            opt->screen_width,
            opt->screen_height,
            BACKGROUND_COLOR);

    for (int i = 0; i < frameCount; i++)
    {
        x3 = x2;
        y3 = y2;

        x2 = x1;
        y2 = y1;

        x1 = x0;
        y1 = y0;

        if (buffer[2 * i] > 0.0 || buffer[2 * i + 1] > 0.0 ||
            buffer[2 * i] < 0.0 || buffer[2 * i + 1] < 0.0)
        {
            x0 = (int)((buffer[2 * i] + 1.0) / 2.0 * (double)opt->screen_width);
            y0 = (int)((buffer[2 * i + 1] + 1.0) / 2.0 * (double)opt->screen_height);
        }

        x0 = clamp(x0, 0, opt->screen_width);
        y0 = clamp(y0, 0, opt->screen_height);

        // TODO: Creatively check intensity and color
        float len = length(x1, y1, x0, y0) / length(0, 0, opt->screen_width, opt->screen_height);
        float intensity = 1.0f / len / len;//- clamp(opt->buffer_store.ptr,0,1);
        Color color = (Color){FOREGROUND_COLOR.r, FOREGROUND_COLOR.g * intensity,  FOREGROUND_COLOR.b * intensity, (int)255.0 * intensity};
        // TraceLog(LOG_DEBUG, "len: %d", len);
        float thickness = fminf(opt->screen_width, opt->screen_height) / min(opt->screen_height, opt->screen_width);

    DrawLineBezierCubic((Vector2){x3, y3}, (Vector2){x0, y0}, (Vector2){x1, y1}, (Vector2){x2, y2}, thickness, color);    }

    EndTextureMode();
}

// User interface
void draw_menu(opt_t *opt)
{
    int x = (10 * opt->screen_width) / 100;
    int y = (10 * opt->screen_height) / 100;
    int w = (80 * opt->screen_width) / 100;
    int last_text_y = 0;

    char inp[256];


    DrawText("Shortcuts (Press m to toggle)", x + 10, y + 10, 10, FOREGROUND_COLOR);
    DrawLine(x, y + 30, x + w, y + 30, FOREGROUND_COLOR);
    DrawText("Select input", x + 10, y + 40, 10, FOREGROUND_COLOR);
    DrawLine(x + 10, y + 55, x + w - 10, y + 55, FOREGROUND_COLOR);

    ma_device_info *pPlaybackInfos;
    ma_uint32 playbackCount;
    ma_device_info *pCaptureInfos;
    ma_uint32 captureCount;

    if (ma_context_get_devices(&opt->context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) != MA_SUCCESS)
    {
        opt->should_exit = TRUE;
        return;
    }

    for (ma_uint32 iDevice = 0; iDevice < captureCount; iDevice += 1)
    {
        sprintf(inp, "%d - %s\n", iDevice, pCaptureInfos[iDevice].name);
        DrawText(inp, x + 10, y + 65 + iDevice * 15, 9, FOREGROUND_COLOR);
        last_text_y = y + 65 + iDevice * 15;
    }

    DrawLine(x + 10, last_text_y + 20, x + w - 10, last_text_y + 20, FOREGROUND_COLOR);
    DrawText("Esc - Exit", x + 10, last_text_y + 25, 10, FOREGROUND_COLOR);

    int h = last_text_y + 40 - y;

    /* Draw a rectangle from 0.1->0.9 of screen */
    DrawRectangleLines(x, y, w, h, FOREGROUND_COLOR);
}


// Utility functions
float clamp(float x, const float min_x, const float max_x)
{
    /*
    Makes sure x is between min_x and max_x
    */
    if (x > max_x)
        return max_x;
    else if (x < min_x)
        return min_x;
    else
        return x;
}

int min(int x, int y)
{
    return x > y ? x : y;
}

float length(float x0, float y0, float x1, float y1)
{
  return sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
}