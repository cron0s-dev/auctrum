#if !defined(config_h_)
#define config_h_

// WINDOW CONSTANTS
#define WINDOW_TITLE "Auctrum"
#define WINDOW_WIDTH_FACTOR 3.5f
#define WINDOW_HEIGHT_FACTOR 6.5f
#define WINDOW_FLAGS SDL_WINDOW_RESIZABLE
#define WINDOW_OPACITY 0.7f

// VISUALIZER CONSTANTS
#define ATTACK 100.0f 
#define DECAY 100.0f 
#define MAX_FREQ 12000.0f
#define MIN_FREQ 20.0f
#define MAX_DB 0.0f // dBFS
#define MIN_DB -80.0f // dBFS
#define BAR_NUMBER 50
#define BAR_MIN_HEIGHT 0.f
#define BAR_SEPARATION 0.5f 
#define BASS_BOOST 0.0f // dB offset
#define TREBLE_BOOST 20.0f // dB offset
/*
 * Adds contrast to small variations
 * in dB values. This makes the
 * visualiser more reactive.
 */
#define VISUAL_CONTRAST 1.2f
#define TIMESTAMP_FONT_SIZE 18

// FFT CONSTANTS
#define FFT_SIZE 8192 
#define RING_BUFFER_SIZE (FFT_SIZE * 4)
/*
 * Provide a value or leave
 * as 0 for it to be determined
 * from your monitor's refresh rate.
 */
#define FFT_HOP_SIZE 0 
/*
 * Audio buffer will get resampled
 * to this value before visualization
 * to preserve frequency resolution.
 * Set to 0 if you want to disable this
 * feature.
 */
#define FFT_DESIRED_SAMPLERATE 44100 

// MUSIC PLAYER CONSTANTS
#define MAX_GAIN 0.0f // dBFS
#define MIN_GAIN -100.0f // dBFS

/* For a linear step of x out of 100
 * GAIN_STEP = 10^(((x / 100) * MIN_GAIN) / 20)
 */
#define GAIN_STEP 1.58f // dBFS

#define SEEK_FORWARD 5.0f // seconds
#define SEEK_BACKWARD 5.0f // seconds
#define MAX_SPEED 2.0f 
#define MIN_SPEED 0.5f

const SDL_Color BACKGROUND_COLOR = { 0, 0, 0, 255 };
const SDL_Color BAR_COLOR = { 255, 255, 255, 255 };

#endif
