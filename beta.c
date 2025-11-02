// main.c - WebAssembly-ready version using emscripten_set_main_loop

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <emscripten.h>

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define PIPE_WIDTH 100
#define PIPE_GAP 250
#define MAX_PIPES 20

typedef struct {
    int x, height;
    int active;
    int scored;
} Pipe;

/* 
   Global game state
    */
static int running = 1;
static int gameOver = 0;
static int inMenu = 1;
static SDL_Event event;

static float birdY;
static float birdVelocity;
static const float gravity = 0.25f;
static const float flapStrength = -8.0f;
static const int pipeSpeed = 3;
static const int dashSpeed = 12;

static Pipe pipes[MAX_PIPES];
static int pipeTimer;
static int score;
static int normalPipeCounter;
static int threePipeCooldown;

static SDL_Rect birdRect;
static SDL_Rect restartButton;
static SDL_Rect startButton;
static SDL_Texture* currentBirdTexture = NULL;
static int dashChannel = -1;

/* SDL objects */
static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;
static SDL_Texture* bgTexture = NULL;
static SDL_Texture* birdTexture = NULL;
static SDL_Texture* birdDashTexture = NULL;
static SDL_Texture* pipeTopTexture = NULL;
static SDL_Texture* pipeBottomTexture = NULL;
static SDL_Texture* restartTexture = NULL;
static SDL_Texture* startTexture = NULL;

static Mix_Music* bgm = NULL;
static Mix_Chunk* jumpSfx = NULL;
static Mix_Chunk* dashSfx = NULL;
static Mix_Chunk* dedSfx = NULL;
static Mix_Chunk* crossSfx = NULL;

static TTF_Font* font = NULL;

/* -----------------------
   Utility functions
   ----------------------- */
int checkCollision(SDL_Rect a, SDL_Rect b) {
    return !(a.x + a.w < b.x ||
             a.x > b.x + b.w ||
             a.y + a.h < b.y ||
             a.y > b.y + b.h);
}

void resetGame() {
    birdY = WINDOW_HEIGHT / 2.0f;
    birdVelocity = 0.0f;
    for (int i = 0; i < MAX_PIPES; ++i) {
        pipes[i].active = 0;
        pipes[i].scored = 0;
    }
    pipeTimer = 0;
    score = 0;
    normalPipeCounter = 0;
    threePipeCooldown = 0;

    birdRect.x = 250;
    birdRect.y = (int)birdY;
    birdRect.w = 106;
    birdRect.h = 60;
}

/* Cleanup resources and stop the main loop */
void cleanup() {
    if (bgm) { Mix_FreeMusic(bgm); bgm = NULL; }
    if (jumpSfx) { Mix_FreeChunk(jumpSfx); jumpSfx = NULL; }
    if (dashSfx) { Mix_FreeChunk(dashSfx); dashSfx = NULL; }
    if (dedSfx) { Mix_FreeChunk(dedSfx); dedSfx = NULL; }
    if (crossSfx) { Mix_FreeChunk(crossSfx); crossSfx = NULL; }
    Mix_CloseAudio();

    if (font) { TTF_CloseFont(font); font = NULL; }
    TTF_Quit();
    IMG_Quit();

    if (birdTexture) { SDL_DestroyTexture(birdTexture); birdTexture = NULL; }
    if (birdDashTexture) { SDL_DestroyTexture(birdDashTexture); birdDashTexture = NULL; }
    if (bgTexture) { SDL_DestroyTexture(bgTexture); bgTexture = NULL; }
    if (pipeTopTexture) { SDL_DestroyTexture(pipeTopTexture); pipeTopTexture = NULL; }
    if (pipeBottomTexture) { SDL_DestroyTexture(pipeBottomTexture); pipeBottomTexture = NULL; }
    if (restartTexture) { SDL_DestroyTexture(restartTexture); restartTexture = NULL; }
    if (startTexture) { SDL_DestroyTexture(startTexture); startTexture = NULL; }

    if (renderer) { SDL_DestroyRenderer(renderer); renderer = NULL; }
    if (window) { SDL_DestroyWindow(window); window = NULL; }
    SDL_Quit();

    /* Stop the emscripten loop */
    emscripten_cancel_main_loop();
}

/*
   Main game loop (called by Emscripten)
   */
void gameLoop() {
    if (!running) {
        cleanup();
        return;
    }

    int dash = 0;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running = 0;
            return; // will cleanup on next frame
        }

        if (inMenu) {
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                int mx = event.button.x;
                int my = event.button.y;
                if (mx >= startButton.x && mx <= startButton.x + startButton.w &&
                    my >= startButton.y && my <= startButton.y + startButton.h) {
                    inMenu = 0;
                    resetGame();
                }
            }
        } else {
            if (event.type == SDL_KEYDOWN) {
                if (!gameOver) {
                    if (event.key.keysym.sym == SDLK_SPACE) {
                        birdVelocity = flapStrength;
                        if (jumpSfx) Mix_PlayChannel(-1, jumpSfx, 0);
                    }
                    if ((event.key.keysym.sym == SDLK_LSHIFT || event.key.keysym.sym == SDLK_RSHIFT) && dashChannel == -1) {
                        dash = 1;
                        if (dashSfx) dashChannel = Mix_PlayChannel(-1, dashSfx, -1);
                    }
                } else if (event.key.keysym.sym == SDLK_r) {
                    resetGame();
                    gameOver = 0;
                }
            }

            if (event.type == SDL_KEYUP) {
                if (event.key.keysym.sym == SDLK_LSHIFT || event.key.keysym.sym == SDLK_RSHIFT) {
                    if (dashChannel != -1) { Mix_HaltChannel(dashChannel); dashChannel = -1; }
                }
            }

            if (event.type == SDL_MOUSEBUTTONDOWN && gameOver) {
                int mx = event.button.x;
                int my = event.button.y;
                if (mx >= restartButton.x && mx <= restartButton.x + restartButton.w &&
                    my >= restartButton.y && my <= restartButton.y + restartButton.h) {
                    resetGame();
                    gameOver = 0;
                }
            }
        }
    }

    const Uint8* state = SDL_GetKeyboardState(NULL);
    int shiftHeld = state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT];

    if (!inMenu && !gameOver) {
        int currentPipeSpeed = pipeSpeed;
        if (shiftHeld) {
            birdVelocity = 0;
            currentPipeSpeed = dashSpeed;
            currentBirdTexture = birdDashTexture ? birdDashTexture : birdTexture;
        } else {
            currentBirdTexture = birdTexture;
        }

        if (!shiftHeld) birdVelocity += gravity;
        birdY += birdVelocity;
        birdRect.y = (int)birdY;

        if (birdY <= 0 || birdY + birdRect.h >= WINDOW_HEIGHT) {
            gameOver = 1;
            if (dedSfx) Mix_PlayChannel(-1, dedSfx, 0);
        }

        /* Pipe spawning */
        pipeTimer++;
        if (pipeTimer > 80) {
            pipeTimer = 0;
            if (threePipeCooldown > 0) {
                for (int i = 0; i < MAX_PIPES; i++) {
                    if (!pipes[i].active) {
                        pipes[i].active = 1;
                        pipes[i].x = WINDOW_WIDTH;
                        pipes[i].height = 50 + rand() % (WINDOW_HEIGHT - PIPE_GAP - 100);
                        pipes[i].scored = 0;
                        break;
                    }
                }
                threePipeCooldown--;
                normalPipeCounter++;
            } else {
                if (normalPipeCounter >= 3 && rand() % 5 == 0) {
                    int baseHeight = 50 + rand() % (WINDOW_HEIGHT - PIPE_GAP - 100);
                    for (int j = 0; j < 3; j++) {
                        for (int i = 0; i < MAX_PIPES; i++) {
                            if (!pipes[i].active) {
                                pipes[i].active = 1;
                                pipes[i].x = WINDOW_WIDTH + j * (PIPE_WIDTH + 10);
                                pipes[i].height = baseHeight;
                                pipes[i].scored = 0;
                                break;
                            }
                        }
                    }
                    normalPipeCounter = 0;
                    threePipeCooldown = 3;
                } else {
                    for (int i = 0; i < MAX_PIPES; i++) {
                        if (!pipes[i].active) {
                            pipes[i].active = 1;
                            pipes[i].x = WINDOW_WIDTH;
                            pipes[i].height = 50 + rand() % (WINDOW_HEIGHT - PIPE_GAP - 100);
                            pipes[i].scored = 0;
                            break;
                        }
                    }
                    normalPipeCounter++;
                }
            }
        }

        /* Move pipes & collisions */
        for (int i = 0; i < MAX_PIPES; i++) {
            if (pipes[i].active) {
                pipes[i].x -= currentPipeSpeed;

                SDL_Rect topPipe = {pipes[i].x, 0, PIPE_WIDTH, pipes[i].height};
                SDL_Rect bottomPipe = {pipes[i].x, pipes[i].height + PIPE_GAP, PIPE_WIDTH, WINDOW_HEIGHT - pipes[i].height - PIPE_GAP};
                SDL_Rect scoreZone = {pipes[i].x + PIPE_WIDTH / 2, 0, 1, WINDOW_HEIGHT};

                if (checkCollision(birdRect, topPipe) || checkCollision(birdRect, bottomPipe)) {
                    gameOver = 1;
                    if (dedSfx) Mix_PlayChannel(-1, dedSfx, 0);
                }

                if (!pipes[i].scored && checkCollision(birdRect, scoreZone)) {
                    score++;
                    pipes[i].scored = 1;
                    if (crossSfx) Mix_PlayChannel(-1, crossSfx, 0);
                }

                if (pipes[i].x + PIPE_WIDTH < 0) pipes[i].active = 0;
            }
        }
    }

    /* Rendering */
    SDL_RenderClear(renderer);
    if (bgTexture) SDL_RenderCopy(renderer, bgTexture, NULL, NULL);

    if (inMenu) {
        if (startTexture) SDL_RenderCopy(renderer, startTexture, NULL, &startButton);
        if (font) {
            SDL_Color white = {255, 255, 255, 255};
            SDL_Surface* creditSurf = TTF_RenderText_Solid(font, "Assets made by Wish Techawashira", white);
            if (creditSurf) {
                SDL_Texture* creditTex = SDL_CreateTextureFromSurface(renderer, creditSurf);
                SDL_Rect creditRect = {20, WINDOW_HEIGHT - creditSurf->h - 20, creditSurf->w, creditSurf->h};
                if (creditTex) SDL_RenderCopy(renderer, creditTex, NULL, &creditRect);
                SDL_FreeSurface(creditSurf);
                SDL_DestroyTexture(creditTex);
            }
        }
    } else {
        for (int i = 0; i < MAX_PIPES; i++) {
            if (pipes[i].active) {
                SDL_Rect top = {pipes[i].x, 0, PIPE_WIDTH, pipes[i].height};
                SDL_Rect bottom = {pipes[i].x, pipes[i].height + PIPE_GAP, PIPE_WIDTH, WINDOW_HEIGHT - pipes[i].height - PIPE_GAP};
                if (pipeTopTexture) SDL_RenderCopy(renderer, pipeTopTexture, NULL, &top);
                if (pipeBottomTexture) SDL_RenderCopy(renderer, pipeBottomTexture, NULL, &bottom);
            }
        }

        float angle = -birdVelocity * 3.0f;
        if (angle > 45.0f) angle = 45.0f;
        if (angle < -45.0f) angle = -45.0f;
        if (currentBirdTexture) SDL_RenderCopyEx(renderer, currentBirdTexture, NULL, &birdRect, angle, NULL, SDL_FLIP_NONE);

        if (font) {
            char scoreStr[16];
            sprintf(scoreStr, "Score: %d", score);
            SDL_Color white = {255, 255, 255, 255};
            SDL_Surface* scoreSurf = TTF_RenderText_Solid(font, scoreStr, white);
            if (scoreSurf) {
                SDL_Texture* scoreTex = SDL_CreateTextureFromSurface(renderer, scoreSurf);
                SDL_Rect scoreRect = {WINDOW_WIDTH/2 - scoreSurf->w/2, 20, scoreSurf->w, scoreSurf->h};
                if (scoreTex) SDL_RenderCopy(renderer, scoreTex, NULL, &scoreRect);
                SDL_FreeSurface(scoreSurf);
                SDL_DestroyTexture(scoreTex);
            }
        }

        if (gameOver && restartTexture) SDL_RenderCopy(renderer, restartTexture, NULL, &restartButton);
    }

    SDL_RenderPresent(renderer);
    /* no SDL_Delay — browser controls frame timing */
}

/* 
   Main initialization
    */
int main(int argc, char* argv[]) {
    srand((unsigned int)time(NULL));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        printf("IMG_Init failed: %s\n", IMG_GetError());
        SDL_Quit();
        return 1;
    }
    if (TTF_Init() == -1) {
        printf("TTF_Init failed: %s\n", TTF_GetError());
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    window = SDL_CreateWindow("Froppy Bird",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Surface* icon = IMG_Load("assets/sprites/icon.png");
    if (icon) {
        SDL_SetWindowIcon(window, icon);
        SDL_FreeSurface(icon);
    } else {
        printf("Failed to load icon: %s\n", IMG_GetError());
    }

    /* Load textures  */
    bgTexture = IMG_LoadTexture(renderer, "assets/sprites/bg.png");
    birdTexture = IMG_LoadTexture(renderer, "assets/sprites/Bird.png");
    birdDashTexture = IMG_LoadTexture(renderer, "assets/sprites/Bird_dash.png");
    pipeTopTexture = IMG_LoadTexture(renderer, "assets/sprites/pipe_top.png");
    pipeBottomTexture = IMG_LoadTexture(renderer, "assets/sprites/pipe_bottom.png");
    restartTexture = IMG_LoadTexture(renderer, "assets/sprites/restart.png");
    startTexture = IMG_LoadTexture(renderer, "assets/sprites/start.png");

    /* audio */
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) printf("Mix_OpenAudio failed: %s\n", Mix_GetError());
    bgm = Mix_LoadMUS("assets/audio/bgm.ogg");
    jumpSfx = Mix_LoadWAV("assets/audio/jump.ogg");
    dashSfx = Mix_LoadWAV("assets/audio/dash.ogg");
    dedSfx  = Mix_LoadWAV("assets/audio/ded.ogg");
    crossSfx = Mix_LoadWAV("assets/audio/cross.ogg");

    if (bgm) { Mix_VolumeMusic(4); Mix_PlayMusic(bgm, -1); }
    if (jumpSfx) Mix_VolumeChunk(jumpSfx, 40);
    if (dashSfx) Mix_VolumeChunk(dashSfx, 48);
    if (dedSfx)  Mix_VolumeChunk(dedSfx, 48);
    if (crossSfx) Mix_VolumeChunk(crossSfx, 40);

    font = TTF_OpenFont("assets/fonts/Fraktur.ttf", 48);
    if (!font) printf("Failed to load font: %s\n", TTF_GetError());

    /*variables */
    birdY = WINDOW_HEIGHT / 2.0f;
    birdVelocity = 0.0f;
    for (int i = 0; i < MAX_PIPES; i++) {
        pipes[i].active = 0;
        pipes[i].scored = 0;
    }
    pipeTimer = 0;
    score = 0;
    normalPipeCounter = 0;
    threePipeCooldown = 0;

    birdRect.x = 250;
    birdRect.y = (int)birdY;
    birdRect.w = 106;
    birdRect.h = 60;

    restartButton.x = WINDOW_WIDTH/2 - 150;
    restartButton.y = WINDOW_HEIGHT/2 - 50;
    restartButton.w = 300;
    restartButton.h = 100;

    startButton.x = WINDOW_WIDTH/2 - 400;
    startButton.y = WINDOW_HEIGHT/2 - 100;
    startButton.w = 800;
    startButton.h = 200;

    currentBirdTexture = birdTexture;
    dashChannel = -1;

    /* mainloop on browser*/
    emscripten_set_main_loop(gameLoop, 60, 1);

   
    return 0;
}
