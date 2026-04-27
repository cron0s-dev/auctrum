#ifndef config_h_
#define config_h_

#include <SDL3/SDL.h>

// WINDOW CONSTANTS
#define WINDOW_TITLE "Auctrum"
#define WINDOW_WIDTH_FACTOR 3.5f
#define WINDOW_HEIGHT_FACTOR 6.5f
#define WINDOW_FLAGS SDL_WINDOW_RESIZABLE
#define WINDOW_OPACITY 0.7f

// VISUALIZER CONSTANTS
#define ALPHA 30
#define MAX_FREQ 5000.f
#define MIN_FREQ 20.f
#define MAX_DB 0.f
#define MIN_DB -70.f
#define BAR_NUMBER 50
#define BAR_MIN_HEIGHT 0.f
#define BAR_SEPARATION 0.5f 

// FFT CONSTANTS
#define FFT_SIZE 8192 

// MUSIC PLAYER CONSTANTS
#define MAX_GAIN 0.f
#define MIN_GAIN -100.f

/* For a linear step of x out of 100
 * GAIN_STEP = 10^(((x / 100) * MIN_GAIN) / 20)
 */
#define GAIN_STEP 1.58f 

#define SEEK_FORWARD 5.0f
#define SEEK_BACKWARD 5.0f
#define MAX_SPEED 2.f 
#define MIN_SPEED 0.5f

const SDL_Color BACKGROUND_COLOR = { 0, 0, 0, 255 };
const SDL_Color BAR_COLOR = { 255, 255, 255, 255 };

#endif
