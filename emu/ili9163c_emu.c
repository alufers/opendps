
/**
 * @file ili9163c.c
 * @author alufers
 * @brief ILI9163c display emulator using SDL
 */

#include "ili9163c.h"
#include "ili9163c_settings.h"
#include <SDL.h>
#include <SDL_image.h>
#include <event.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static SDL_Window *window = NULL;
SDL_Renderer *renderer;

static SDL_Texture *imageTexture = NULL;

static SDL_Surface *tftSurface = NULL;
static SDL_Texture *tftTexture = NULL;
static pthread_mutex_t tftSurfaceMutex = PTHREAD_MUTEX_INITIALIZER;
static SDL_Rect curr_rect = {0, 0, 0, 0};

#define BACKGROUND_SCALE (2)

#define TFT_POS_X (97 * BACKGROUND_SCALE)
#define TFT_POS_Y (36 * BACKGROUND_SCALE)
#define TFT_WIDTH (128 * BACKGROUND_SCALE)
#define TFT_HEIGHT (128 * BACKGROUND_SCALE)

typedef struct screen_button {
  bool present;
  event_t event;

  uint16_t x;
  uint16_t y;
  uint16_t w;
  uint16_t h;

  bool pressed;
  bool hovered;
} screen_button;

#define DEF_BUTTON(event_v, x_v, y_v, w_v, h_v)                                \
  {                                                                            \
    .present = true, .event = event_v, .x = x_v * BACKGROUND_SCALE,            \
    .y = y_v * BACKGROUND_SCALE, .w = w_v * BACKGROUND_SCALE,                  \
    .h = h_v * BACKGROUND_SCALE, .pressed = false, .hovered = false            \
  }

#define DEF_BUTTON_END()                                                       \
  { .present = false }

static screen_button buttons[] = {DEF_BUTTON(event_button_m1, 47, 47, 33, 23),
                                  DEF_BUTTON(event_button_sel, 47, 88, 33, 23),
                                  DEF_BUTTON(event_button_m2, 47, 129, 33, 23),
                                  DEF_BUTTON(event_rot_left, 273, 28, 18, 12),
                                  DEF_BUTTON(event_rot_right, 313, 28, 18, 12),
                                  DEF_BUTTON(event_rot_press, 273, 44, 58, 58),
                                  DEF_BUTTON(event_button_enable, 282, 121, 42, 35),
                                  DEF_BUTTON_END()

};

static void screen_emu_thread(void *arg) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
    exit(1);
    return;
  }

  if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
    printf("SDL_image could not initialize! SDL_image Error: %s\n",
           IMG_GetError());
    exit(1);
    return;
  }

  SDL_Surface *image =
      IMG_Load("background.png"); // Replace with your image's path
  if (image == NULL) {
    printf("Unable to background.png image! SDL_image Error: %s\n",
           IMG_GetError());
    exit(1);
    return;
  }

  window = SDL_CreateWindow("OpenDPS Emulator", SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED, image->w, image->h, 0);

  if (window == NULL) {
    printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
    exit(1);
    return;
  }

  renderer = SDL_CreateRenderer(window, 0, SDL_RENDERER_ACCELERATED);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  imageTexture = SDL_CreateTextureFromSurface(renderer, image);
  SDL_FreeSurface(image);

  tftSurface = SDL_CreateRGBSurface(0, _TFTWIDTH, _TFTHEIGHT, 16, 0, 0, 0, 0);

  SDL_Rect fillRect = {0, 0, 30, 30};
  SDL_FillRect(tftSurface, &fillRect,
               SDL_MapRGB(tftSurface->format, 255, 0, 0));

  // unlock mutex after ready
  pthread_mutex_unlock(&tftSurfaceMutex);

  bool quit = false;
  SDL_Event e;

  while (!quit) {
    while (SDL_PollEvent(&e) != 0) {
      if (e.type == SDL_QUIT) {
        quit = true;
      } else if (e.type == SDL_MOUSEMOTION) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        screen_button *btn = buttons;
        while (btn->present) {
          if (mx >= btn->x && mx < btn->x + btn->w && my >= btn->y &&
              my < btn->y + btn->h) {
            btn->hovered = true;
          } else {
            btn->hovered = false;
          }
          btn++;
        }
      } else if (e.type == SDL_MOUSEBUTTONDOWN) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        screen_button *btn = buttons;
        while (btn->present) {
          if (mx >= btn->x && mx < btn->x + btn->w && my >= btn->y &&
              my < btn->y + btn->h) {
            btn->pressed = true;
          }
          btn++;
        }
      } else if (e.type == SDL_MOUSEBUTTONUP) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        screen_button *btn = buttons;
        while (btn->present) {
          if (mx >= btn->x && mx < btn->x + btn->w && my >= btn->y &&
              my < btn->y + btn->h) {
            if (btn->pressed) {
              event_put(btn->event, 0);
            }
            btn->pressed = false;
          }
          btn++;
        }
      }
    }

    // clear
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    // first draw image
    SDL_RenderCopy(renderer, imageTexture, NULL, NULL);
    SDL_Rect dstRect = {TFT_POS_X, TFT_POS_Y, TFT_WIDTH, TFT_HEIGHT};
    {
      pthread_mutex_lock(&tftSurfaceMutex);
      tftTexture = SDL_CreateTextureFromSurface(renderer, tftSurface);
      SDL_RenderCopy(renderer, tftTexture, NULL, &dstRect);
      SDL_DestroyTexture(tftTexture);
    }
    pthread_mutex_unlock(&tftSurfaceMutex);
    screen_button *btn = buttons;
    while (btn->present) {
      if (btn->hovered) {
        SDL_Rect btnRect = {btn->x, btn->y, btn->w, btn->h};
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 200);
        SDL_RenderDrawRect(renderer, &btnRect);
      }
      if (btn->pressed) {
        SDL_Rect btnRect = {btn->x, btn->y, btn->w, btn->h};
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 70);
        SDL_RenderFillRect(renderer, &btnRect);
      }

      btn++;
    }
    SDL_RenderPresent(renderer);
  }

  SDL_FreeSurface(image);
  SDL_DestroyWindow(window);
  IMG_Quit();
  SDL_Quit();
  exit(0);
}

static uint32_t RGB565_to_SDLColor(const SDL_PixelFormat *format,
                                   uint16_t color) {
  SDL_Color sdlColor;

  // Extract individual channels from RGB565 format
  uint8_t r5 = (color & 0xF800) >> 11;
  uint8_t g6 = (color & 0x07E0) >> 5;
  uint8_t b5 = (color & 0x001F);

  // Scale 5-bit and 6-bit channels to 8-bit channels
  sdlColor.r = (r5 << 3) | (r5 >> 2);
  sdlColor.g = (g6 << 2) | (g6 >> 4);
  sdlColor.b = (b5 << 3) | (b5 >> 2);
  sdlColor.a = 255; // Full alpha by default, since RGB565 doesn't include alpha

  return SDL_MapRGBA(format, sdlColor.r, sdlColor.g, sdlColor.b, sdlColor.a);
}

void ili9163c_init(void) {
  pthread_mutexattr_init(&tftSurfaceMutex);
  pthread_mutex_lock(&tftSurfaceMutex);
  pthread_t th;
  pthread_create(&th, NULL, screen_emu_thread, NULL);
}

void ili9163c_get_geometry(uint16_t *width, uint16_t *height) {
  *width = _TFTWIDTH;
  *height = _TFTHEIGHT;
}

void ili9163c_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  curr_rect.x = x0;
  curr_rect.y = y0;
  curr_rect.w = x1 - x0 + 1;
  curr_rect.h = y1 - y0 + 1;
}

void ili9163c_push_color(uint16_t color) {}

void ili9163c_fill_screen(uint16_t color) {
  pthread_mutex_lock(&tftSurfaceMutex);
  SDL_Rect fillRect = {0, 0, _TFTWIDTH, _TFTHEIGHT};
  SDL_FillRect(tftSurface, &fillRect,
               RGB565_to_SDLColor(tftSurface->format, color));
  pthread_mutex_unlock(&tftSurfaceMutex);
}

void ili9163c_draw_pixel(int16_t x, int16_t y, uint16_t color) {}

void ili9163c_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                        uint16_t color) {}

void ili9163c_set_rotation(uint8_t r) {}

void ili9163c_invert_display(bool i) {}

void ili9163c_display(bool on) {}

bool ili9163c_boundary_check(int16_t x, int16_t y) { return true; }

void ili9163c_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color) {
  pthread_mutex_lock(&tftSurfaceMutex);

  SDL_Rect vlineRect = {x, y, 1, h};
  SDL_FillRect(tftSurface, &vlineRect,
               RGB565_to_SDLColor(tftSurface->format, color));

  pthread_mutex_unlock(&tftSurfaceMutex);
}

void ili9163c_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
  pthread_mutex_lock(&tftSurfaceMutex);

  SDL_Rect hlineRect = {x, y, w, 1};
  SDL_FillRect(tftSurface, &hlineRect,
               RGB565_to_SDLColor(tftSurface->format, color));

  pthread_mutex_unlock(&tftSurfaceMutex);
}

/**
 * @brief Emulate SPI transaction, for image data coming to the display
 *
 * @param tx_buf
 * @param tx_len
 * @param rx_buf
 * @param rx_len
 * @return true
 * @return false
 */
bool spi_dma_transceive(uint8_t *tx_buf, uint32_t tx_len, uint8_t *rx_buf,
                        uint32_t rx_len) {

  pthread_mutex_lock(&tftSurfaceMutex);

  uint16_t *tx_buf16 = (uint16_t *)tx_buf;
  for (size_t x = 0; x < curr_rect.w; x++) {
    for (size_t y = 0; y < curr_rect.h; y++) {
      if (y * curr_rect.w + x >= tx_len / 2) {
        // printf("y * curr_rect.w + x >= tx_len / 2\n");
        continue;
        ;
      }
      uint16_t color = tx_buf16[y * curr_rect.w + x];

      SDL_Rect pixRect = {curr_rect.x + x, curr_rect.y + y, 1, 1};
      SDL_FillRect(tftSurface, &pixRect,
                   RGB565_to_SDLColor(tftSurface->format, color));
      // draw t
    }
  }
  pthread_mutex_unlock(&tftSurfaceMutex);
  return false;
}
