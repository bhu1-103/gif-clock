#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <gif_lib.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

typedef struct {
    SDL_Texture** frames;
    int* delays;
    int frame_count;
    int current_frame;
    Uint32 last_update;
    int width;
    int height;
} GIFAnimation;

GIFAnimation* load_gif(SDL_Renderer* renderer, const char* path) {
    int error = 0;
    GifFileType* gif = DGifOpenFileName(path, &error);
    if (!gif) {
        printf("GIF open error: %d\n", error);
        return NULL;
    }

    if (DGifSlurp(gif) != GIF_OK) {
        DGifCloseFile(gif, &error);
        return NULL;
    }

    GIFAnimation* animation = malloc(sizeof(GIFAnimation));
    animation->frame_count = gif->ImageCount;
    animation->current_frame = 0;
    animation->last_update = SDL_GetTicks();
    animation->width = gif->SWidth;
    animation->height = gif->SHeight;
    animation->frames = malloc(sizeof(SDL_Texture*) * gif->ImageCount);
    animation->delays = malloc(sizeof(int) * gif->ImageCount);

    // Create base surface with correct format
    SDL_Surface* base_surface = SDL_CreateRGBSurfaceWithFormat(
        0, animation->width, animation->height, 32, SDL_PIXELFORMAT_RGBA32);

    for (int i = 0; i < gif->ImageCount; i++) {
        SavedImage* frame = &gif->SavedImages[i];
        GraphicsControlBlock gcb;
        DGifSavedExtensionToGCB(gif, i, &gcb);
        animation->delays[i] = gcb.DelayTime > 0 ? gcb.DelayTime * 10 : 100;

        ColorMapObject* color_map = gif->SColorMap ? gif->SColorMap : frame->ImageDesc.ColorMap;
        if (!color_map) {
            printf("No color map for frame %d\n", i);
            continue;
        }

        // Create frame surface
        SDL_Surface* frame_surface = SDL_CreateRGBSurfaceWithFormat(
            0, animation->width, animation->height, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_FillRect(frame_surface, NULL, SDL_MapRGBA(frame_surface->format, 0, 0, 0, 0));

        // Copy previous frame if disposal method requires it
        if (i > 0 && gcb.DisposalMode == DISPOSE_DO_NOT) {
            SDL_BlitSurface(base_surface, NULL, frame_surface, NULL);
        }

        // Draw current frame
        for (int y = 0; y < frame->ImageDesc.Height; y++) {
            for (int x = 0; x < frame->ImageDesc.Width; x++) {
                int color_index = frame->RasterBits[y * frame->ImageDesc.Width + x];
                if (color_index == gcb.TransparentColor) continue;

                GifColorType color = color_map->Colors[color_index];
                int tx = frame->ImageDesc.Left + x;
                int ty = frame->ImageDesc.Top + y;
                if (tx < frame_surface->w && ty < frame_surface->h) {
                    Uint32 pixel = SDL_MapRGBA(frame_surface->format, 
                        color.Red, color.Green, color.Blue, 0xFF);
                    ((Uint32*)frame_surface->pixels)[ty * frame_surface->pitch/4 + tx] = pixel;
                }
            }
        }

        // Update base surface for next frame
        if (gcb.DisposalMode == DISPOSE_DO_NOT) {
            SDL_BlitSurface(frame_surface, NULL, base_surface, NULL);
        }

        // Create texture
        animation->frames[i] = SDL_CreateTextureFromSurface(renderer, frame_surface);
        SDL_SetTextureBlendMode(animation->frames[i], SDL_BLENDMODE_BLEND);
        SDL_FreeSurface(frame_surface);
    }

    SDL_FreeSurface(base_surface);
    DGifCloseFile(gif, &error);
    return animation;
}

void update_gif(GIFAnimation* animation) {
    Uint32 now = SDL_GetTicks();
    Uint32 elapsed = now - animation->last_update;
    
    while (elapsed >= animation->delays[animation->current_frame]) {
        elapsed -= animation->delays[animation->current_frame];
        animation->current_frame = (animation->current_frame + 1) % animation->frame_count;
        animation->last_update = now - elapsed;
    }
}

void render_gif(SDL_Renderer* renderer, GIFAnimation* animation) {
    SDL_RenderCopy(renderer, animation->frames[animation->current_frame], NULL, NULL);
}

void render_text(SDL_Renderer* renderer, TTF_Font* font, const char* text, 
                SDL_Color color, int x, int y) {
    SDL_Surface* surface = TTF_RenderText_Blended(font, text, color);
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_Rect rect = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &rect);
    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

int main(int argc, char* argv[]) {
    if (argc != 6) {
        printf("Usage: %s <font.ttf> <x> <y> <font_size> <date_fmt>\n", argv[0]);
        printf("Date formats: dd-mm or mm-dd\n");
        return 1;
    }

    const char* font_path = argv[1];
    int pos_x = atoi(argv[2]);
    int pos_y = atoi(argv[3]);
    int font_size = atoi(argv[4]);
    const char* date_fmt = argv[5];
    
    if (strcmp(date_fmt, "dd-mm") != 0 && strcmp(date_fmt, "mm-dd") != 0) {
        printf("Invalid date format! Use dd-mm or mm-dd\n");
        return 1;
    }

    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    setlocale(LC_ALL, "");

    SDL_Window* window = SDL_CreateWindow("Animated Clock",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_SHOWN);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    GIFAnimation* bg_animation = load_gif(renderer, "bg.gif");
    if (!bg_animation) {
        printf("Failed to load bg.gif\n");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    TTF_Font* font = TTF_OpenFont(font_path, font_size);
    if (!font) {
        printf("Failed to load font: %s\n", TTF_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    TTF_SetFontHinting(font, TTF_HINTING_LIGHT);

    SDL_Color textColor = {255, 0, 255, 255};
    int quit = 0;
    SDL_Event event;
    const int padding = 10;

    while (!quit) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) quit = 1;
        }

        update_gif(bg_animation);

        // Get current time
        time_t now = time(NULL);
        struct tm* timeinfo = localtime(&now);

        // Format strings
        char timestr[20], datestr[20], daystr[20];
        strftime(timestr, sizeof(timestr), "%H:%M:%S", timeinfo);
        strftime(daystr, sizeof(daystr), " %a", timeinfo);
        strftime(datestr, sizeof(datestr),
                strcmp(date_fmt, "dd-mm") == 0 ? "%d-%m" : "%m-%d", timeinfo);

        // Single render pass
        SDL_RenderClear(renderer);
        render_gif(renderer, bg_animation);
        
        // Render text elements directly
        render_text(renderer, font, timestr, textColor, pos_x, pos_y);
        render_text(renderer, font, datestr, textColor,
                   pos_x, pos_y + font_size + padding);
        render_text(renderer, font, daystr, textColor,
                   pos_x + (strlen(datestr) * font_size * 0.6),
                   pos_y + font_size + padding);

        SDL_RenderPresent(renderer);
    }

    // Cleanup
    for (int i = 0; i < bg_animation->frame_count; i++) {
        SDL_DestroyTexture(bg_animation->frames[i]);
    }
    free(bg_animation->frames);
    free(bg_animation->delays);
    free(bg_animation);

    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    return 0;
}
