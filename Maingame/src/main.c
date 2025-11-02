#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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

int checkCollision(SDL_Rect a, SDL_Rect b) {
    return !(a.x + a.w < b.x ||
             a.x > b.x + b.w ||
             a.y + a.h < b.y ||
             a.y > b.y + b.h);
}

void resetGame(float* birdY, float* birdVelocity, Pipe pipes[], int* pipeTimer, int* score, int* normalPipeCounter, int* threePipeCooldown) {
    *birdY = WINDOW_HEIGHT / 2;
    *birdVelocity = 0;
    for (int i = 0; i < MAX_PIPES; i++) pipes[i].active = 0;
    *pipeTimer = 0;
    *score = 0;
    *normalPipeCounter = 0;
    *threePipeCooldown = 0;
}

int main(int argc, char* argv[]) {
    srand((unsigned int)time(NULL));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { printf("SDL_Init failed: %s\n", SDL_GetError()); return 1; }
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) { printf("IMG_Init failed: %s\n", IMG_GetError()); SDL_Quit(); return 1; }
    if (TTF_Init() == -1) { printf("TTF_Init failed: %s\n", TTF_GetError()); SDL_Quit(); return 1; }

    SDL_Window* window = SDL_CreateWindow("Froppy Bird",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) { printf("SDL_CreateWindow failed: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) { printf("SDL_CreateRenderer failed: %s\n", SDL_GetError()); SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    SDL_Surface* icon = IMG_Load("assets/sprites/icon.png");
    if (icon) {
        SDL_SetWindowIcon(window, icon);
        SDL_FreeSurface(icon);
    } else {
        printf("Failed to load icon: %s\n", IMG_GetError());
    }


    // Load textures
    SDL_Texture* bgTexture = IMG_LoadTexture(renderer, "assets/sprites/bg.png");
    SDL_Texture* birdTexture = IMG_LoadTexture(renderer, "assets/sprites/Bird.png");
    SDL_Texture* birdDashTexture = IMG_LoadTexture(renderer, "assets/sprites/Bird_dash.png");
    SDL_Texture* pipeTopTexture = IMG_LoadTexture(renderer, "assets/sprites/pipe_top.png");
    SDL_Texture* pipeBottomTexture = IMG_LoadTexture(renderer, "assets/sprites/pipe_bottom.png");
    SDL_Texture* restartTexture = IMG_LoadTexture(renderer, "assets/sprites/restart.png");
    SDL_Texture* startTexture = IMG_LoadTexture(renderer, "assets/sprites/start.png");

    // Initialize audio
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) printf("Mix_OpenAudio failed: %s\n", Mix_GetError());
    Mix_Music* bgm = Mix_LoadMUS("assets/audio/bgm.mp3");
    Mix_Chunk* jumpSfx = Mix_LoadWAV("assets/audio/jump.mp3");
    Mix_Chunk* dashSfx = Mix_LoadWAV("assets/audio/dash.mp3");
    Mix_Chunk* dedSfx  = Mix_LoadWAV("assets/audio/ded.mp3");
    Mix_Chunk* crossSfx = Mix_LoadWAV("assets/audio/cross.mp3");

    if (bgm) { Mix_VolumeMusic(4); Mix_PlayMusic(bgm, -1); }
    if (jumpSfx) Mix_VolumeChunk(jumpSfx, 40);
    if (dashSfx) Mix_VolumeChunk(dashSfx, 48);
    if (dedSfx)  Mix_VolumeChunk(dedSfx, 48);
    if (crossSfx) Mix_VolumeChunk(crossSfx, 40);

    TTF_Font* font = TTF_OpenFont("assets/fonts/Fraktur.ttf", 48);
    if (!font) printf("Failed to load font: %s\n", TTF_GetError());

    int running = 1, gameOver = 0, inMenu = 1;
    SDL_Event event;

    float birdY = WINDOW_HEIGHT / 2, birdVelocity = 0;
    const float gravity = 0.25f, flapStrength = -8.0f;
    const int pipeSpeed = 3, dashSpeed = 12;
    Pipe pipes[MAX_PIPES] = {0};
    int pipeTimer = 0, score = 0, normalPipeCounter = 0, threePipeCooldown = 0;

    SDL_Rect birdRect = {250, (int)birdY, 106, 60};
    SDL_Rect restartButton = {WINDOW_WIDTH/2 - 150, WINDOW_HEIGHT/2 - 50, 300, 100};
    SDL_Rect startButton = {WINDOW_WIDTH/2 - 400, WINDOW_HEIGHT/2 - 100, 800, 200}; // Start button size
    SDL_Texture* currentBirdTexture = birdTexture;
    int dashChannel = -1;

    while (running) {
        int dash = 0;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = 0;

            if (inMenu) {
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    int mx = event.button.x;
                    int my = event.button.y;
                    if (mx >= startButton.x && mx <= startButton.x + startButton.w &&
                        my >= startButton.y && my <= startButton.y + startButton.h) {
                        inMenu = 0;
                        resetGame(&birdY, &birdVelocity, pipes, &pipeTimer, &score, &normalPipeCounter, &threePipeCooldown);
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
                        resetGame(&birdY, &birdVelocity, pipes, &pipeTimer, &score, &normalPipeCounter, &threePipeCooldown);
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
                        resetGame(&birdY, &birdVelocity, pipes, &pipeTimer, &score, &normalPipeCounter, &threePipeCooldown);
                        gameOver = 0;
                    }
                }
            }
        }

        const Uint8* state = SDL_GetKeyboardState(NULL);
        int shiftHeld = state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT];

        if (!inMenu && !gameOver) {
            int currentPipeSpeed = pipeSpeed;
            if (shiftHeld) { birdVelocity = 0; currentPipeSpeed = dashSpeed; currentBirdTexture = birdDashTexture ? birdDashTexture : birdTexture; }
            else { currentBirdTexture = birdTexture; }

            if (!shiftHeld) birdVelocity += gravity;
            birdY += birdVelocity;
            birdRect.y = (int)birdY;

            if (birdY <= 0 || birdY + birdRect.h >= WINDOW_HEIGHT) {
                gameOver = 1;
                if (dedSfx) Mix_PlayChannel(-1, dedSfx, 0);
            }

            // --- Pipe spawning ---
            pipeTimer++;
            if (pipeTimer > 80) {
                pipeTimer = 0;
                if (threePipeCooldown > 0) { // normal pipe
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
                    if (normalPipeCounter >= 3 && rand() % 5 == 0) { // 3-row pipes
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
                    } else { // normal pipe
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

            // Move pipes & check collisions
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

        // --- Rendering ---
        SDL_RenderClear(renderer);
        if (bgTexture) SDL_RenderCopy(renderer, bgTexture, NULL, NULL);

        if (inMenu) {
            if (startTexture) SDL_RenderCopy(renderer, startTexture, NULL, &startButton);
            // Tips bottom-right
            if (font) {
                SDL_Color white = {255, 255, 255, 255};

                SDL_Surface* creditSurf = TTF_RenderText_Solid(font, "Assets made by Wish Techawashira", white);
                SDL_Texture* creditTex = SDL_CreateTextureFromSurface(renderer, creditSurf);
                SDL_Rect creditRect = {20, WINDOW_HEIGHT - creditSurf->h - 20, creditSurf->w, creditSurf->h};
                SDL_RenderCopy(renderer, creditTex, NULL, &creditRect);
                SDL_FreeSurface(creditSurf);
                SDL_DestroyTexture(creditTex);
            }
        } else {
            // Draw pipes
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
            SDL_RenderCopyEx(renderer, currentBirdTexture, NULL, &birdRect, angle, NULL, SDL_FLIP_NONE);

            // Draw score
            if (font) {
                char scoreStr[16];
                sprintf(scoreStr, "Score: %d", score);
                SDL_Color white = {255, 255, 255, 255};
                SDL_Surface* scoreSurf = TTF_RenderText_Solid(font, scoreStr, white);
                SDL_Texture* scoreTex = SDL_CreateTextureFromSurface(renderer, scoreSurf);
                SDL_Rect scoreRect = {WINDOW_WIDTH/2 - scoreSurf->w/2, 20, scoreSurf->w, scoreSurf->h};
                SDL_RenderCopy(renderer, scoreTex, NULL, &scoreRect);
                SDL_FreeSurface(scoreSurf);
                SDL_DestroyTexture(scoreTex);
            }

            // Restart button if game over
            if (gameOver && restartTexture) SDL_RenderCopy(renderer, restartTexture, NULL, &restartButton);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    // Cleanup
    if (birdTexture) SDL_DestroyTexture(birdTexture);
    if (birdDashTexture) SDL_DestroyTexture(birdDashTexture);
    if (bgTexture) SDL_DestroyTexture(bgTexture);
    if (pipeTopTexture) SDL_DestroyTexture(pipeTopTexture);
    if (pipeBottomTexture) SDL_DestroyTexture(pipeBottomTexture);
    if (restartTexture) SDL_DestroyTexture(restartTexture);
    if (startTexture) SDL_DestroyTexture(startTexture);

    if (bgm) Mix_FreeMusic(bgm);
    if (jumpSfx) Mix_FreeChunk(jumpSfx);
    if (dashSfx) Mix_FreeChunk(dashSfx);
    if (dedSfx) Mix_FreeChunk(dedSfx);
    if (crossSfx) Mix_FreeChunk(crossSfx);
    Mix_CloseAudio();
    TTF_CloseFont(font);
    TTF_Quit();
    IMG_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
