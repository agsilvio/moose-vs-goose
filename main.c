#include "SDL3/SDL_error.h"
#include "SDL3/SDL_timer.h"
#define SDL_MAIN_USE_CALLBACKS

#include "SDL3_image/SDL_image.h"
#include "SDL3/SDL_main.h"
#include "SDL3/SDL_video.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_keycode.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_log.h"
#include "SDL3_ttf/SDL_ttf.h" 
#include "SDL3_mixer/SDL_mixer.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define DESIRED_FPS 60
#define EPSILON 0.00001f
#define PI 3.141592654f

#define SCREEN_WIDTH 1000
#define SCREEN_HEIGHT 600
#define WIN_SCORE 5
#define MAX_DOWN_LEFT_ANGLE 1.25f
#define MAX_DOWN_RIGHT_ANGLE 1.75f
#define MAX_UP_LEFT_ANGLE 0.75f
#define MAX_UP_RIGHT_ANGLE 0.25f
#define ANGLE_ADJ_SMALL 0.1f
#define ANGLE_ADJ_LARGE 0.2f
#define FACEOFF_TIME_SECONDS 3
#define SCORESTATE_TIME_SECONDS 3
#define AI_TIME_SECONDS 3

#define DEFAULT_PLAYER_WIDTH 50
#define DEFAULT_PLAYER_HEIGHT 150
#define PLAYER_EDGE_MARGIN 120

#define PLY_PERCENTAGE_MOVEMENT_PER_SECOND 0.90f

#define BALL_SIZE 34
#define EGG_CRACKING_SIZE 50

#if __EMSCRIPTEN__
  #define BALL_PERCENTAGE_MOVEMENT_PER_SECOND 1.40f
#else
  #define BALL_PERCENTAGE_MOVEMENT_PER_SECOND 1.30f
#endif

#define EGG_CRACKING_PERCENTAGE_MOVEMENT_PER_SECOND 0.6f

const int PLY_PERC_MOV_PER_SEC_IN_PIXELS = SCREEN_HEIGHT * PLY_PERCENTAGE_MOVEMENT_PER_SECOND;
const int BALL_PERC_MOV_PER_SEC_IN_PIXELS = SCREEN_HEIGHT * BALL_PERCENTAGE_MOVEMENT_PER_SECOND;
const int EGG_CRACKING_PERC_MOV_PER_SEC_IN_PIXELS = SCREEN_HEIGHT * EGG_CRACKING_PERCENTAGE_MOVEMENT_PER_SECOND;
const int BALL_COLOUR[3] = { 50, 200, 235 };
const int ENDZONE_WIDTH = (int)(BALL_SIZE * 1.5f);

const double AI_CHANCE_TO_BE_SLIPPIN = 0.35f;
const int AI_MILLIS_TO_CHECK_SLIPPIN = 500;
const int AI_DISTANCE_HIGHWATERMARK = 15;
const int AI_DISTANCE_LOWWATERMARK = 5;

typedef enum {
    GOING,
    WAITING
} AiMovingState;

typedef enum {
    INTRO,
    PLAYING,
    OVER
} GameState;

typedef enum {
    FACEOFF,
    INPLAY,
    SCORED
} PlayingState;

typedef enum {
    UPPER_TIP,
    UPPER_MID,
    MID,
    LOWER_MID,
    LOWER_TIP
} PaddleSection;

typedef enum {
    ON_POINT,
    SLIPPIN
} AiState;

typedef struct {
    double x;
    double y;
    SDL_Rect collisionBox;
    SDL_Rect presentationBox;
    Uint32 colour;
    int score;
} Player;

typedef struct {
    double x;
    double y;
    SDL_Rect collisionBox;
    SDL_Rect presentationBox;
    Uint32 colour;
    double anglePi;
    int presFrameCount;
    int presFrameWidth;
    int presFrameHeight;
    int scoredPresFrameCount;
    int scoredPresFrameWidth;
    int scoredPresFrameHeight;
} Ball;

typedef struct {
    Uint32 lastTime;
    SDL_Window * window;
    SDL_Renderer * renderer;
    TTF_Font * font;
    Mix_Music * music;

    //boring globals
    bool quit;
    //
    //SDL_Texture* gScreenSurface;
    SDL_Texture* titleScreen;
    SDL_Texture* background;
    SDL_Texture* mooseWinsScreen;
    SDL_Texture* gooseWinsScreen;
    SDL_Texture* moosePaddle;
    SDL_Texture* goosePaddle;
    SDL_Texture* ballTexture;
    SDL_Texture* eggCracking;
    SDL_Rect screenRect;
    double timeDeltaSeconds;
    float startingAngles[6];
    Mix_Chunk * bounceSounds[3];
    Mix_Chunk * mooseScoreSound;
    Mix_Chunk * gooseScoreSound;
    Mix_Chunk * mooseWinSound;
    Mix_Chunk * gooseWinSound;

    //game globals
    Player leftPlayer;
    Player rightPlayer;
    Ball ball;

    GameState gameState;
    PlayingState playingState;
    Uint32 faceOffEndTime;
    Uint32 scoredStateEndTime;
    bool faceOffCounterVisible;
    bool rightPlayerIsAi;
    Uint32 lastRightPlayerInputTime;
    AiState aiState;
    AiMovingState aiMovingState;
    Uint32 lastAiStateCheckTIme;
    bool firstCrackingFramePlayed;
    int crackingModuloOffset;
} GameContext;


SDL_Texture * loadTexture(char * path, SDL_Renderer * renderer) {
    SDL_Texture * newTexture = IMG_LoadTexture(renderer, path);
    if (!newTexture) {
        printf( "Could not load image at '%s' could not be loaded! SDL Error: %s\n", path, SDL_GetError() );
        return NULL;
    }

    return newTexture;
}

Mix_Chunk * loadSound(char * path) {
    Mix_Chunk * sound = Mix_LoadWAV(path);
    if (!sound) {
        printf( "Could not load sound at '%s'. SDL Error: %s\n", path, SDL_GetError());
        return NULL;
    }

    return sound;
}

void rateLimitFps(Uint32 lastTime) {
    Uint32 frameTime = 1000 / DESIRED_FPS;
    Uint32 delay = frameTime - (SDL_GetTicks() - lastTime);
    if (delay > 0) {
        SDL_Delay(delay);
    }
}

void initGameObjectsPosition(GameContext * ctx) {
    ctx->leftPlayer.x = PLAYER_EDGE_MARGIN;
    ctx->rightPlayer.x = SCREEN_WIDTH - PLAYER_EDGE_MARGIN - DEFAULT_PLAYER_WIDTH;
    ctx->leftPlayer.y = ctx->rightPlayer.y = SCREEN_HEIGHT / 2.0f - DEFAULT_PLAYER_HEIGHT / 2.0f;
    ctx->ball.x = SCREEN_WIDTH / 2.0f - BALL_SIZE / 2.0f;
    ctx->ball.y = SCREEN_HEIGHT / 2.0f - BALL_SIZE / 2.0f;
    ctx->ball.anglePi = ctx->startingAngles[rand() % 6];
}

void resetScore(GameContext * ctx){
    ctx->leftPlayer.score = 0;
    ctx->rightPlayer.score = 0;
}

int compareDoubles(double a, double b) {
    if (fabs(a - b) < EPSILON) {
        return 0;
    } else if (a < b) {
        return -1;
    } else {
        return 1;
    }
}

void getDeltaXYComponentsFromRadiansAndHypotenuseLength(double anglePi,
                                                        double hypLength,
                                                        double * newX,
                                                        double * newY) {
    double rads = anglePi * PI;
    double cosTheta = cos(rads);
    double lengthAdj = cosTheta * hypLength;

    double sinTheta = sin(rads);
    double lengthOpp = sinTheta * hypLength;

    *newX = lengthAdj;

    //invert Y axis
    *newY = -lengthOpp;
}

PaddleSection getSectionHit(SDL_Rect intersection, SDL_Rect paddleRect) {
    double interBottomDelta = (paddleRect.y + paddleRect.h) - (intersection.y + intersection.h); 
    int lowerTipThreshold = (int)(paddleRect.h * 0.2f);
    int lowerMidThreshold = (int)(paddleRect.h * 0.4f);
    int midThreshold = (int)(paddleRect.h * 0.6f);
    int upperMidThreshold = (int)(paddleRect.h * 0.8f);
    int upperTipThreshold = (int)(paddleRect.h * 1.0f);

    if (interBottomDelta <= lowerTipThreshold){
        return LOWER_TIP;
    } else if (interBottomDelta <= lowerMidThreshold && interBottomDelta > lowerTipThreshold){
        return LOWER_MID;
    } else if (interBottomDelta <= midThreshold && interBottomDelta > lowerMidThreshold){
        return MID;
    } else if (interBottomDelta <= upperMidThreshold && interBottomDelta > midThreshold){
        return UPPER_MID;

    } else { //interBottomDelta <= upperTipThreshold && interBottomDelta > upperMidThreshold
        return UPPER_TIP;
    } 
}

void reflectX(double * rads) {
    *rads = 2 - *rads + 1;
    if (compareDoubles(*rads, 2.0f) > 0){
        *rads -= 2;
    }
}

void reflectY(double * rads) {
    *rads = 2 - *rads;
    if (compareDoubles(*rads, 2.0f) == 0) {
        *rads = 0;
    }
}

void doAngle(GameContext * ctx, bool isLeftPaddle, bool isBroadsideCollision, SDL_Rect intersection) {
    if (!isBroadsideCollision) {
        reflectY(&ctx->ball.anglePi);
    } else {
        bool isComingDown = ctx->ball.anglePi > 1.0f;
        SDL_Rect paddleRect = isLeftPaddle ? ctx->leftPlayer.collisionBox : ctx->rightPlayer.collisionBox;
        PaddleSection sectionHit = getSectionHit(intersection, paddleRect);

        reflectX(&ctx->ball.anglePi);
        if (isLeftPaddle) {
            switch (sectionHit) {
                case LOWER_TIP:
                    ctx->ball.anglePi -= ANGLE_ADJ_LARGE;
                    break;
                case LOWER_MID:
                    ctx->ball.anglePi -= ANGLE_ADJ_SMALL;
                    break;
                case MID:
                    reflectX(&ctx->ball.anglePi);
                    break;
                case UPPER_MID:
                    ctx->ball.anglePi += ANGLE_ADJ_SMALL;
                    break;
                case UPPER_TIP:
                    ctx->ball.anglePi += ANGLE_ADJ_LARGE;
                    break;
            }

            if (isComingDown) {
                if (ctx->ball.anglePi < MAX_DOWN_RIGHT_ANGLE) {
                    ctx->ball.anglePi = MAX_DOWN_RIGHT_ANGLE;
                }
            } else {
                if (ctx->ball.anglePi > MAX_UP_RIGHT_ANGLE) {
                    ctx->ball.anglePi = MAX_UP_RIGHT_ANGLE;
                }
            }
        } else { // is right paddle
            switch (sectionHit) {
                case LOWER_TIP:
                    ctx->ball.anglePi += ANGLE_ADJ_LARGE;
                    break;
                case LOWER_MID:
                    ctx->ball.anglePi += ANGLE_ADJ_SMALL;
                    break;
                case MID:
                    reflectX(&ctx->ball.anglePi);
                    break;
                case UPPER_MID:
                    ctx->ball.anglePi -= ANGLE_ADJ_SMALL;
                    break;
                case UPPER_TIP:
                    ctx->ball.anglePi -= ANGLE_ADJ_LARGE;
                    break;
            }

            if (isComingDown) {
                if (ctx->ball.anglePi > MAX_DOWN_LEFT_ANGLE) {
                    ctx->ball.anglePi = MAX_DOWN_LEFT_ANGLE;
                }
            } else {
                if (ctx->ball.anglePi < MAX_UP_LEFT_ANGLE) {
                    ctx->ball.anglePi = MAX_UP_LEFT_ANGLE;
                }
            }
        }
    }
}


void playRandomBounceSound(GameContext * ctx) {
    Mix_PlayChannel(-1, ctx->bounceSounds[rand() % 3], 0);
}

void playMooseScoreSound(GameContext * ctx) {
    Mix_PlayChannel(-1, ctx->mooseScoreSound, 0);
}

void playGooseScoreSound(GameContext * ctx) {
    Mix_PlayChannel(-1, ctx->gooseScoreSound, 0);
}

void playMooseWinSound(GameContext * ctx) {
    Mix_PlayChannel(-1, ctx->mooseWinSound, 0);
}

void playGooseWinSound(GameContext * ctx) {
    Mix_PlayChannel(-1, ctx->gooseWinSound, 0);
}

void enterPlayingStateFaceOff(GameContext * ctx){
    initGameObjectsPosition(ctx);
    ctx->faceOffEndTime = ctx->lastTime + (FACEOFF_TIME_SECONDS * 1000);
    ctx->faceOffCounterVisible = true;
    ctx->playingState = FACEOFF;
    ctx->gameState = PLAYING;
    ctx->firstCrackingFramePlayed = false;
    ctx->crackingModuloOffset = 0;
}

void enterPlayingStateInPlay(GameContext * ctx) {
    ctx->playingState = INPLAY;
    ctx->faceOffCounterVisible = false;
}

void enterPlayingStateScored(GameContext * ctx) {
    ctx->playingState = SCORED;
    ctx->scoredStateEndTime = ctx->lastTime + (SCORESTATE_TIME_SECONDS * 1000);
}

void updateIntro(GameContext * ctx) {
    const bool* keystates = SDL_GetKeyboardState(NULL);

    if (keystates[SDL_SCANCODE_SPACE]) {
        resetScore(ctx);
        enterPlayingStateFaceOff(ctx);
    }
}

void renderIntro(GameContext * ctx) {
    SDL_RenderClear(ctx->renderer);
    SDL_RenderTexture(ctx->renderer, ctx->titleScreen, NULL, NULL);
    SDL_RenderPresent( ctx->renderer );
}

void updateOver(GameContext * ctx) {
    const bool* keystates = SDL_GetKeyboardState(NULL);

    if (keystates[SDL_SCANCODE_SPACE]) {
        resetScore(ctx);
        enterPlayingStateFaceOff(ctx);
    }
}

void renderOver(GameContext * ctx) {
    SDL_RenderClear(ctx->renderer);

    if (ctx->leftPlayer.score >= WIN_SCORE) {
        SDL_RenderTexture(ctx->renderer, ctx->mooseWinsScreen, NULL, NULL);
    } else {
        SDL_RenderTexture(ctx->renderer, ctx->gooseWinsScreen, NULL, NULL);
    }

    SDL_RenderPresent( ctx->renderer );
}

void renderPlaying(GameContext * ctx) {
    SDL_RenderClear(ctx->renderer);

    SDL_RenderTexture(ctx->renderer, ctx->background, NULL, NULL);

    //update paddles x positions
    ctx->leftPlayer.collisionBox.y = ctx->leftPlayer.y;
    ctx->leftPlayer.presentationBox.y = ctx->leftPlayer.y;
    ctx->rightPlayer.collisionBox.y = ctx->rightPlayer.y;
    ctx->rightPlayer.presentationBox.y = ctx->rightPlayer.y;

    SDL_FRect lpPresBox;
    SDL_FRect rpPresBox;
    SDL_RectToFRect(&ctx->leftPlayer.presentationBox, &lpPresBox);
    SDL_RectToFRect(&ctx->rightPlayer.presentationBox, &rpPresBox);

    SDL_RenderTexture(ctx->renderer, ctx->moosePaddle, NULL, &lpPresBox);
    SDL_RenderTexture(ctx->renderer, ctx->goosePaddle, NULL, &rpPresBox);

    //render ball
    //get frame
    int ballFrameX;
    switch (ctx->playingState) {
        int frameIndex;
        int robustOffset;
        case SCORED:
            frameIndex = (ctx->lastTime / 350 % ctx->ball.scoredPresFrameCount);
            if (!ctx->firstCrackingFramePlayed) {
                ctx->crackingModuloOffset = frameIndex;
                ctx->firstCrackingFramePlayed = true;
            }
            robustOffset = frameIndex >= ctx->crackingModuloOffset ? frameIndex - ctx->crackingModuloOffset : ctx->ball.scoredPresFrameCount - ctx->crackingModuloOffset + frameIndex;
            ballFrameX = robustOffset * ctx->ball.scoredPresFrameWidth;
            break;
        case INPLAY:
        case FACEOFF:
            ballFrameX = (ctx->lastTime / 150 % ctx->ball.presFrameCount) * ctx->ball.presFrameWidth;
            break;
    }

    ctx->ball.collisionBox.x = ctx->ball.x;
    ctx->ball.presentationBox.x = ctx->ball.x;
    ctx->ball.collisionBox.y = ctx->ball.y;
    ctx->ball.presentationBox.y = ctx->ball.y;

    SDL_FRect ballPresBox;
    SDL_FRect ballPresSourceBox;
    switch (ctx->playingState) {
        int a;
        case SCORED:
            a = 0; //needed to retain C99
            SDL_FRect temp = { ballFrameX, 0, ctx->ball.scoredPresFrameWidth, ctx->ball.scoredPresFrameHeight };
            ballPresSourceBox = temp;
            break;
        case INPLAY:
        case FACEOFF:
            a = 0; //needed to retain C99
            SDL_FRect temp2 = { ballFrameX, 0, ctx->ball.presFrameWidth, ctx->ball.presFrameHeight };
            ballPresSourceBox = temp2;
            break;
    }

    SDL_RectToFRect(&ctx->ball.presentationBox, &ballPresBox);

    switch (ctx->playingState) {
        case SCORED:
            SDL_RenderTexture(ctx->renderer, ctx->eggCracking, &ballPresSourceBox, &ballPresBox);
            break;
        case INPLAY:
        case FACEOFF:
            SDL_RenderTexture(ctx->renderer, ctx->ballTexture, &ballPresSourceBox, &ballPresBox);
            break;
    }

    SDL_Color color = { 255, 255, 255 };

    //left player
    char screenString[50] = "Moose: ";
    char screenIntAsString[10];
    sprintf(screenIntAsString, "%d", ctx->leftPlayer.score);
    strcat(screenString, screenIntAsString);
    SDL_Surface * textSurface = TTF_RenderText_Solid(ctx->font, screenString, 0, color);
    SDL_Texture* textTexture = SDL_CreateTextureFromSurface( ctx->renderer, textSurface );


    SDL_Rect lDestRect = { 50, 50, textSurface->w, textSurface->h };
    SDL_FRect ldr;
    SDL_RectToFRect(&lDestRect, &ldr);
    SDL_RenderTexture(ctx->renderer, textTexture, NULL, &ldr);

    //right player oink
    if (ctx->rightPlayerIsAi) {
        strcpy(screenString, "CPU Goose: ");
    } else {
        strcpy(screenString, "Goose: ");
    }
    screenIntAsString[0] = '\0';
    sprintf(screenIntAsString, "%d", ctx->rightPlayer.score);
    strcat(screenString, screenIntAsString);
    textSurface = TTF_RenderText_Solid(ctx->font, screenString, 0, color);
    textTexture = SDL_CreateTextureFromSurface( ctx->renderer, textSurface );

    int rightMargin = 200;
    if (ctx->rightPlayerIsAi) {
        rightMargin = 290;
    }
    SDL_Rect rDestRect = { SCREEN_WIDTH - rightMargin, 50, textSurface->w, textSurface->h };
    SDL_FRect rdr;
    SDL_RectToFRect(&rDestRect, &rdr);
    SDL_RenderTexture(ctx->renderer, textTexture, NULL, &rdr);

    //counter
    if (ctx->faceOffCounterVisible) {
        strcpy(screenString, "Get Ready!");
        textSurface = TTF_RenderText_Solid(ctx->font, screenString, 0, color);
        textTexture = SDL_CreateTextureFromSurface( ctx->renderer, textSurface );
        SDL_Rect getReadyRect = { SCREEN_WIDTH / 2 - 300, SCREEN_HEIGHT / 2, textSurface->w, textSurface->h };
        SDL_FRect grdr;
        SDL_RectToFRect(&getReadyRect, &grdr);
        SDL_RenderTexture(ctx->renderer, textTexture, NULL, &grdr);

        strcpy(screenString, "");
        screenIntAsString[0] = '\0';
        int faceOffTime = ((ctx->faceOffEndTime - ctx->lastTime) / 1000) + 1;
        sprintf(screenIntAsString, "%d", faceOffTime);
        strcat(screenString, screenIntAsString);
        textSurface = TTF_RenderText_Solid(ctx->font, screenString, 0, color);
        textTexture = SDL_CreateTextureFromSurface( ctx->renderer, textSurface );

        SDL_Rect counterRect = { SCREEN_WIDTH / 2 + 200, SCREEN_HEIGHT / 2, textSurface->w, textSurface->h };
        SDL_FRect crdr;
        SDL_RectToFRect(&counterRect, &crdr);
        SDL_RenderTexture(ctx->renderer, textTexture, NULL, &crdr);
    }

    SDL_DestroySurface(textSurface);
    SDL_DestroyTexture(textTexture);
    SDL_RenderPresent( ctx->renderer );
}

int absDifference(int x, int y) {
    return x <= y ? y - x : x - y;
}

void doAiMove(GameContext * ctx) {
    if ((ctx->lastTime - ctx->lastAiStateCheckTIme) > AI_MILLIS_TO_CHECK_SLIPPIN) {
        ctx->lastAiStateCheckTIme = ctx->lastTime;
        bool aiShouldSlip = (rand() % 11) < (int)(10.0f * AI_CHANCE_TO_BE_SLIPPIN);
        ctx->aiState = aiShouldSlip ? SLIPPIN : ON_POINT;
    }

    if (absDifference(ctx->ball.y, ctx->rightPlayer.y) < AI_DISTANCE_LOWWATERMARK) {
        ctx->aiMovingState = WAITING;
    }

    if (absDifference(ctx->ball.y, ctx->rightPlayer.y) > AI_DISTANCE_HIGHWATERMARK) {
        ctx->aiMovingState = GOING;
    }

    if (ctx->aiState == ON_POINT && ctx->aiMovingState == GOING) {
        double *rightY = &(ctx->rightPlayer.y); 
        if (ctx->ball.y > ctx->rightPlayer.y) {
            *rightY += ctx->timeDeltaSeconds * PLY_PERC_MOV_PER_SEC_IN_PIXELS;
            if (*rightY + DEFAULT_PLAYER_HEIGHT > SCREEN_HEIGHT) {
                *rightY = SCREEN_HEIGHT - DEFAULT_PLAYER_HEIGHT;
            }
        } else if (ctx->ball.y < ctx->rightPlayer.y) {
            *rightY -= ctx->timeDeltaSeconds * PLY_PERC_MOV_PER_SEC_IN_PIXELS;
            if (*rightY < 0) {
                *rightY = 0;
            }
        }
    }
}

void updatePlayingInPlay(GameContext * ctx) {

    const bool* keystates = SDL_GetKeyboardState(NULL);

    //left player movement
    if (keystates[SDL_SCANCODE_W]) {
        double *leftY = &(ctx->leftPlayer.y); 
        *leftY -= ctx->timeDeltaSeconds * PLY_PERC_MOV_PER_SEC_IN_PIXELS;
        if (*leftY < 0) {
            *leftY = 0;
        }
    }
    if (keystates[SDL_SCANCODE_S]) {
        double *leftY = &(ctx->leftPlayer.y); 
        *leftY += ctx->timeDeltaSeconds * PLY_PERC_MOV_PER_SEC_IN_PIXELS;
        if (*leftY + DEFAULT_PLAYER_HEIGHT > SCREEN_HEIGHT) {
            *leftY = SCREEN_HEIGHT - DEFAULT_PLAYER_HEIGHT;
        }
    }

    //right player movement
    if (keystates[SDL_SCANCODE_DOWN]) {
        if (ctx->rightPlayerIsAi) {
            ctx->rightPlayerIsAi = false;
        }
        double *rightY = &(ctx->rightPlayer.y); 
        *rightY += ctx->timeDeltaSeconds * PLY_PERC_MOV_PER_SEC_IN_PIXELS;
        if (*rightY + DEFAULT_PLAYER_HEIGHT > SCREEN_HEIGHT) {
            *rightY = SCREEN_HEIGHT - DEFAULT_PLAYER_HEIGHT;
        }
        ctx->lastRightPlayerInputTime = ctx->lastTime;
    }
    if (keystates[SDL_SCANCODE_UP]) {
        if (ctx->rightPlayerIsAi) {
            ctx->rightPlayerIsAi = false;
        }
        double *rightY = &(ctx->rightPlayer.y); 
        *rightY -= ctx->timeDeltaSeconds * PLY_PERC_MOV_PER_SEC_IN_PIXELS;
        if (*rightY < 0) {
            *rightY = 0;
        }
        ctx->lastRightPlayerInputTime = ctx->lastTime;
    }

    //do ai move
    if (ctx->rightPlayerIsAi) {
        doAiMove(ctx);
    }

    //move ball
    int movementDistance = ctx->timeDeltaSeconds * BALL_PERC_MOV_PER_SEC_IN_PIXELS;
    double deltaX, deltaY;
    getDeltaXYComponentsFromRadiansAndHypotenuseLength(ctx->ball.anglePi, 
                                                       movementDistance,
                                                       &deltaX,
                                                       &deltaY);
    ctx->ball.x += deltaX;
    ctx->ball.y += deltaY;

    //end zone check for ball
    if (ctx->ball.x + BALL_SIZE > SCREEN_WIDTH + ENDZONE_WIDTH) {
        ctx->ball.x = SCREEN_WIDTH + ENDZONE_WIDTH - EGG_CRACKING_SIZE;
        reflectX(&ctx->ball.anglePi);
        ctx->leftPlayer.score++;
        if (ctx->leftPlayer.score >= WIN_SCORE) {
            ctx->gameState = OVER;
            playMooseWinSound(ctx);
        } else {
            playMooseScoreSound(ctx);
            enterPlayingStateScored(ctx);
        }
    } else if (ctx->ball.x < 0 - ENDZONE_WIDTH) {
        ctx->ball.x = 0 - ENDZONE_WIDTH;
        reflectX(&ctx->ball.anglePi);
        ctx->rightPlayer.score++;
        if (ctx->rightPlayer.score >= WIN_SCORE) {
            ctx->gameState = OVER;
            playGooseWinSound(ctx);
        } else {
            playGooseScoreSound(ctx);
            enterPlayingStateScored(ctx);
        }
    }

    //upper and lower wall check for ball
    if (ctx->ball.y + BALL_SIZE > SCREEN_HEIGHT) {
        ctx->ball.y = SCREEN_HEIGHT - BALL_SIZE;
        reflectY(&ctx->ball.anglePi);
        playRandomBounceSound(ctx);
    } else if (ctx->ball.y < 0) {
        ctx->ball.y = 0;
        reflectY(&ctx->ball.anglePi);
        playRandomBounceSound(ctx);
    }

    //paddle check for ball
    SDL_Rect intersection;
    bool intersected; 
    //
    //left paddle check
    intersected = SDL_GetRectIntersection(&ctx->leftPlayer.collisionBox, 
                                          &ctx->ball.collisionBox, 
                                          &intersection);
    if (intersected) {
        //if intersection is higher than it is wide, the intersection happened
        //on the broad side of the paddle
        //also, don't want to reflect angle if the ball is coming from behind
        if (intersection.h > intersection.w &&
            //ball.x > leftPlayer.x + (BALL_SIZE / 2.0f) &&
            (ctx->ball.anglePi > 0.5f && ctx->ball.anglePi < 1.5f)) {
            ctx->ball.x = ctx->leftPlayer.x + ctx->leftPlayer.collisionBox.w;
            playRandomBounceSound(ctx);

            //everything but angle is handled. do that now.
            doAngle(ctx, true, true, intersection);
        }

        if (intersection.h < intersection.w &&
            (ctx->ball.anglePi > 0.5f && ctx->ball.anglePi < 1.5f)) {
            if (ctx->ball.y < ctx->leftPlayer.y + (ctx->leftPlayer.collisionBox.h / 2.0f)) {
                ctx->ball.y = ctx->leftPlayer.y - BALL_SIZE;
            } else {
                ctx->ball.y = ctx->leftPlayer.y + ctx->leftPlayer.collisionBox.h;
            }
            playRandomBounceSound(ctx);
            doAngle(ctx, true, false, intersection);
        }
    }

    //left paddle check
    intersected = SDL_GetRectIntersection(&ctx->rightPlayer.collisionBox, 
                                          &ctx->ball.collisionBox, 
                                          &intersection);
    if (intersected) {
        //if intersection is higher than it is wide, the intersection happened
        //on the broad side of the paddle
        //also, don't want to reflect angle if the ball is coming from behind
        if (intersection.h > intersection.w && 
            (ctx->ball.anglePi < 0.5f || ctx->ball.anglePi > 1.5f)) {
            ctx->ball.x = ctx->rightPlayer.x - ctx->rightPlayer.collisionBox.w;
            playRandomBounceSound(ctx);
            //everything but angle is handled. do that now.
            doAngle(ctx, false, true, intersection);
        }

        //for when ball hits top/bottom of panel
        if (intersection.h < intersection.w && 
            (ctx->ball.anglePi < 0.5f || ctx->ball.anglePi > 1.5f)) {
            if (ctx->ball.y < ctx->rightPlayer.y + (ctx->rightPlayer.collisionBox.h / 2.0f)) {
                ctx->ball.y = ctx->rightPlayer.y - BALL_SIZE;
            } else {
                ctx->ball.y = ctx->rightPlayer.y + ctx->rightPlayer.collisionBox.h;
            }
            playRandomBounceSound(ctx);
            doAngle(ctx, false, false, intersection);
        }
    }
}

void updatePlayingScored(GameContext * ctx) {

    const bool* keystates = SDL_GetKeyboardState(NULL);
    //right player movement
    if (keystates[SDL_SCANCODE_DOWN]) {
        if (ctx->rightPlayerIsAi) {
            ctx->rightPlayerIsAi = false;
        }
        ctx->lastRightPlayerInputTime = ctx->lastTime;
    }
    if (keystates[SDL_SCANCODE_UP]) {
        if (ctx->rightPlayerIsAi) {
            ctx->rightPlayerIsAi = false;
        }
        ctx->lastRightPlayerInputTime = ctx->lastTime;
    }

    //move ball
    int movementDistance = ctx->timeDeltaSeconds * EGG_CRACKING_PERC_MOV_PER_SEC_IN_PIXELS;
    double deltaX, deltaY;
    getDeltaXYComponentsFromRadiansAndHypotenuseLength(ctx->ball.anglePi, 
                                                       movementDistance,
                                                       &deltaX,
                                                       &deltaY);
    ctx->ball.x += deltaX;
    ctx->ball.y += deltaY;


    if (ctx->ball.y + EGG_CRACKING_SIZE > SCREEN_HEIGHT) {
        ctx->ball.y = SCREEN_HEIGHT - EGG_CRACKING_SIZE;
        reflectY(&ctx->ball.anglePi);
        //playRandomBounceSound();
    } else if (ctx->ball.y < 0) {
        ctx->ball.y = 0;
        reflectY(&ctx->ball.anglePi);
        //playRandomBounceSound();
    }

    if (ctx->lastTime > ctx->scoredStateEndTime) {
        enterPlayingStateFaceOff(ctx);
    }
}

void updatePlayingFaceOff(GameContext * ctx) {
    const bool* keystates = SDL_GetKeyboardState(NULL);

    //left player movement
    if (keystates[SDL_SCANCODE_W]) {
        double *leftY = &(ctx->leftPlayer.y); 
        *leftY -= ctx->timeDeltaSeconds * PLY_PERC_MOV_PER_SEC_IN_PIXELS;
        if (*leftY < 0) {
            *leftY = 0;
        }
    }
    if (keystates[SDL_SCANCODE_S]) {
        double *leftY = &(ctx->leftPlayer.y); 
        *leftY += ctx->timeDeltaSeconds * PLY_PERC_MOV_PER_SEC_IN_PIXELS;
        if (*leftY + DEFAULT_PLAYER_HEIGHT > SCREEN_HEIGHT) {
            *leftY = SCREEN_HEIGHT - DEFAULT_PLAYER_HEIGHT;
        }
    }
    //
    //right player movement
    if (keystates[SDL_SCANCODE_DOWN]) {
        if (ctx->rightPlayerIsAi) {
            ctx->rightPlayerIsAi = false;
        }
        double *rightY = &(ctx->rightPlayer.y); 
        *rightY += ctx->timeDeltaSeconds * PLY_PERC_MOV_PER_SEC_IN_PIXELS;
        if (*rightY + DEFAULT_PLAYER_HEIGHT > SCREEN_HEIGHT) {
            *rightY = SCREEN_HEIGHT - DEFAULT_PLAYER_HEIGHT;
        }
        ctx->lastRightPlayerInputTime = ctx->lastTime;
    }
    if (keystates[SDL_SCANCODE_UP]) {
        if (ctx->rightPlayerIsAi) {
            ctx->rightPlayerIsAi = false;
        }
        double *rightY = &(ctx->rightPlayer.y); 
        *rightY -= ctx->timeDeltaSeconds * PLY_PERC_MOV_PER_SEC_IN_PIXELS;
        if (*rightY < 0) {
            *rightY = 0;
        }
        ctx->lastRightPlayerInputTime = ctx->lastTime;
    }

    if (ctx->lastTime > ctx->faceOffEndTime) {
        enterPlayingStateInPlay(ctx);
    }
}

void initGameObjects(GameContext * ctx) {
    //player images
    SDL_Rect leftPlayerRect = {ctx->leftPlayer.x,
        0, //will get populated later, so don't calculate here
        DEFAULT_PLAYER_WIDTH,
        DEFAULT_PLAYER_HEIGHT};
    SDL_Rect rightPlayerRect = {ctx->rightPlayer.x,
        0, //will get populated later, so don't calculate here
        DEFAULT_PLAYER_WIDTH,
        DEFAULT_PLAYER_HEIGHT};

    //ball image
    SDL_Rect ballRect = {ctx->ball.x,
        ctx->ball.y, //will get populated later, so don't calculate here
        BALL_SIZE,
        BALL_SIZE};

    ctx->leftPlayer.collisionBox = leftPlayerRect;
    ctx->leftPlayer.presentationBox = leftPlayerRect;
    ctx->rightPlayer.collisionBox = rightPlayerRect;
    ctx->rightPlayer.presentationBox = rightPlayerRect;
    ctx->ball.collisionBox = ballRect;
    ctx->ball.presentationBox = ballRect;
}

void initScreen(GameContext * ctx){
    ctx->screenRect.x = 0;
    ctx->screenRect.y = 0;
    ctx->screenRect.w = SCREEN_WIDTH;
    ctx->screenRect.h = SCREEN_HEIGHT;
}

void initMusic(GameContext * ctx){
    Mix_VolumeMusic(30);
    Mix_PlayMusic(ctx->music, -1);
}

bool initSystem(GameContext * ctx) {
    //open window and renderer
    if (!SDL_CreateWindowAndRenderer("Moose Vs. Goose", SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_BORDERLESS, &ctx->window, &ctx->renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create window and renderer: %s", SDL_GetError());
        return false;
    }

    SDL_SetRenderVSync(ctx->renderer, -1);

    if (!Mix_OpenAudio( 0, NULL)) {
        printf( "SDL Mixer could not be initialized! SDL Error: %s\n", SDL_GetError() );
        return false;
    }

    for (int i = 0; i < 3; i++) {
        char bounceFileName[50];
        sprintf(bounceFileName, "assets/bounce%d.wav", (i+1));
        ctx->bounceSounds[i] = loadSound(bounceFileName);
        if (!ctx->bounceSounds[i]) {
            return false;
        }
    }

    ctx->gooseScoreSound = loadSound("assets/gooseScoreWithEgg.wav");
    ctx->mooseScoreSound = loadSound("assets/mooseScoreWithEgg.wav");
    ctx->mooseWinSound = loadSound("assets/mooseWin.wav");
    ctx->gooseWinSound = loadSound("assets/gooseWin.wav");
    if (!ctx->gooseScoreSound ||
        !ctx->mooseScoreSound ||
        !ctx->mooseWinSound ||
        !ctx->gooseWinSound) {
        return false;
    }

    //load music
    ctx->music = Mix_LoadMUS("assets/music.wav");
    if (!ctx->music) {
        printf( "Could not load music. %s", SDL_GetError() );
        return false;
    }

    Mix_VolumeMusic(30);

    if (!TTF_Init()) {
        printf( "Could not load font. %s", SDL_GetError() );
        return false;
    }

    ctx->font = TTF_OpenFont("assets/power_up.ttf", 48);
    if (!ctx->font) {
        printf( "Font could not be loaded! SDL Error: %s\n", SDL_GetError() );
        return false;
    }

    ctx->titleScreen = loadTexture("assets/titleScreen.png", ctx->renderer);
    ctx->background = loadTexture("assets/background.png", ctx->renderer);
    ctx->mooseWinsScreen = loadTexture("assets/mooseWinsScreen.png", ctx->renderer);
    ctx->gooseWinsScreen = loadTexture("assets/gooseWinsScreen.png", ctx->renderer);
    ctx->moosePaddle = loadTexture("assets/moosePaddle.png", ctx->renderer);
    ctx->goosePaddle = loadTexture("assets/goosePaddle.png", ctx->renderer);
    ctx->eggCracking = loadTexture("assets/eggcracking.png", ctx->renderer);
    ctx->ballTexture = loadTexture("assets/ball-34x34x8.png", ctx->renderer);
    if (!ctx->titleScreen ||
        !ctx->background ||
        !ctx->mooseWinsScreen ||
        !ctx->gooseWinsScreen ||
        !ctx->moosePaddle ||
        !ctx->goosePaddle ||
        !ctx->ballTexture) {
        return false;
    }

    return true;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    printf("SDL_AppInit\n");

    int result = SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS);
    if(result < 0) {
        printf("SDL_InitSubSystem failed with code %d.", result);
        return SDL_APP_FAILURE;
    }

    //initialize game context
    GameContext * ctx = (GameContext *)malloc(sizeof(GameContext));
    GameContext ctxInit = {
        .leftPlayer = { 0.0f, 0.0f, {0, 0, 0, 0}, {0, 0, 0, 0}, 0, 0 },
        .rightPlayer = { 0.0f, 0.0f, {0, 0, 0, 0}, {0, 0, 0, 0}, 0, 0 },
        .startingAngles = { 0.0f, 1.95f, 0.05f, 1.0f, 1.05f, 0.95f },
        .gameState = INTRO,
        .playingState = FACEOFF,
        .faceOffEndTime = 0,
        .scoredStateEndTime = 0,
        .faceOffCounterVisible = false,
        .rightPlayerIsAi = true,
        .lastRightPlayerInputTime = 0,
        .aiState = ON_POINT,
        .aiMovingState = WAITING,
        .firstCrackingFramePlayed = false,
        .crackingModuloOffset = 0,
        .ball = { 0.0f, 0.0f, {0, 0, 0, 0}, {0, 0, 0, 0}, 0, 0.25f, 8, BALL_SIZE, BALL_SIZE, 19, EGG_CRACKING_SIZE, EGG_CRACKING_SIZE}
    };

    *ctx = ctxInit;
    *appstate = ctx;

    //Start up SDL and create window
    if( !initSystem(ctx)) {
        printf( "Failed to initialize!\n" );
        return SDL_APP_FAILURE;
    }

    initScreen(ctx);

    initGameObjectsPosition(ctx);

    initGameObjects(ctx);

    initMusic(ctx);

    ctx->lastTime = SDL_GetTicks();

    bool quit = false;

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate){
    GameContext * ctx = (GameContext *)appstate;


    Uint32 currentTime = SDL_GetTicks();
    ctx->timeDeltaSeconds = (currentTime - ctx->lastTime) / 1000.0f;
    ctx->lastTime = currentTime;

    //rateLimitFps(ctx->lastTime);

    //check right player ai status
    if ((ctx->lastTime - ctx->lastRightPlayerInputTime) / 1000 > AI_TIME_SECONDS) {
        ctx->rightPlayerIsAi = true;
    }

    switch (ctx->gameState) {
        case INTRO:
            updateIntro(ctx);
            renderIntro(ctx);
            break;
        case PLAYING:
            switch (ctx->playingState) {
                case FACEOFF:
                    updatePlayingFaceOff(ctx);
                    break;
                case INPLAY:
                    updatePlayingInPlay(ctx);
                    break;
                case SCORED:
                    updatePlayingScored(ctx);
                    break;
            }
            renderPlaying(ctx);
            break;
        case OVER:
            updateOver(ctx);
            renderOver(ctx);
            break;
    }


    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event){

    if( event->type == SDL_EVENT_QUIT ) {
        return SDL_APP_SUCCESS;
    }

    else if( event->type == SDL_EVENT_KEY_DOWN ) {
        switch( event->key.key ) {
            case SDLK_Q:
                return SDL_APP_SUCCESS;
                break;
            case SDLK_M:
                if (Mix_PausedMusic() == 1) {
                    Mix_ResumeMusic();
                } else {
                    Mix_PauseMusic();
                }
                break;
        }
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    GameContext * ctx = (GameContext *)appstate;

    TTF_CloseFont(ctx->font);

    Mix_HaltMusic();
    Mix_FreeChunk(ctx->mooseScoreSound);
    Mix_FreeChunk(ctx->gooseScoreSound);
    Mix_FreeChunk(ctx->mooseWinSound);
    Mix_FreeChunk(ctx->gooseWinSound);
    for (int i = 0; i < 3; i++) {
        Mix_FreeChunk(ctx->bounceSounds[i]);
    }
    Mix_FreeMusic(ctx->music);
    SDL_DestroyTexture(ctx->titleScreen);
    SDL_DestroyTexture(ctx->background);
    SDL_DestroyTexture(ctx->mooseWinsScreen);
    SDL_DestroyTexture(ctx->gooseWinsScreen);
    SDL_DestroyTexture(ctx->moosePaddle);
    SDL_DestroyTexture(ctx->goosePaddle);
    SDL_DestroyTexture(ctx->ballTexture);

    SDL_DestroyRenderer(ctx->renderer);
    SDL_DestroyWindow(ctx->window);

    TTF_Quit();
    Mix_Quit();
    SDL_Quit();
}
