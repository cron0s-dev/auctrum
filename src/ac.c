#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <sndfile.h>
#include <kiss_fftr.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdbool.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#include "config.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    SNDFILE* file;
    sf_count_t pos;
    sf_count_t frames;
    int frameSize;
    SDL_AudioSpec spec;
    SDL_AudioStream* stream;
    SDL_AudioDeviceID deviceId;
    SDL_Mutex* mutex;
} AudioData;

static SDL_DialogFileFilter filters[] = {
    {
        "All supported audio files",
        "wav;aiff;aif;au;paf;svx;nist;voc;ircam;w64;mat;pvf;htk;sds;avr;sd2;flac;caf;wve;ogg;oga;opus;mp3"
    },
    { "WAV",   "wav"  },
    { "AIFF",  "aiff" },
    { "AIF",   "aif"  },
    { "AU",    "au"   },
    { "PAF",   "paf"  },
    { "SVX",   "svx"  },
    { "NIST",  "nist" },
    { "VOC",   "voc"  },
    { "IRCAM", "ircam"},
    { "W64",   "w64"  },
    { "MAT",   "mat"  },
    { "PVF",   "pvf"  },
    { "HTK",   "htk"  },
    { "SDS",   "sds"  },
    { "AVR",   "avr"  },
    { "SD2",   "sd2"  },
    { "FLAC",  "flac" },
    { "CAF",   "caf"  },
    { "WVE",   "wve"  },
    { "OGG",   "ogg"  },
    { "OGA",   "oga"  },
    { "OPUS",  "opus" },
    { "MP3",   "mp3"  },
};

static const size_t nFilters = sizeof(filters) / sizeof(filters[0]);

static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;
static AudioData audio = { 0 };
static SDL_AudioSpec visSpec;
static float barMag[BAR_NUMBER] = { 0 };
static SDL_Color barColor = BAR_COLOR;
static SDL_FRect* rects = NULL;
static float gain = MAX_GAIN;
static float speed = 1.0f;
static double dt;

static float ringBuffer[RING_BUFFER_SIZE];
static int ringPos = 0;
static uint64_t totalWritten = 0;

static bool isRunning = true;
static bool isRainbow = false;
static bool isPlaying = true;
static bool isLooping = false;
static bool isMuted = false;
static bool isFinished = false;
static bool isMirrored = false;
static bool showTimestamp = true;

static void file_dialog_callback(
    void* userdata,
    const char* const* fileList,
    int filter
);

static void audio_stream_callback(
    void* userdata,
    SDL_AudioStream* stream,
    int additional_amount,
    int total_amount
);

static bool open_audio_file(
    SDL_Window* window,
    const char* filePath,
    AudioData* audio
);

static void audio_change_spec(
    float** buf, size_t* len,
    SDL_AudioSpec* src_spec,
    SDL_AudioSpec* dst_spec
);

static float* create_hann_window(size_t n);

static void fft_audio(
    float* barMag,
    float* audioBuf,
    size_t fft_size
);

static void seek_audio(
    AudioData* audio,
    SDL_AudioStream* stream,
    double seconds
);

static void set_audio_speed(SDL_AudioStream* stream, float* speed);
static void should_audio_loop(AudioData* audio, bool loop);
static SDL_FRect* create_bars(SDL_Window* window, size_t bar_num);

static bool draw_bars(
    SDL_FRect* rects,
    size_t nBar,
    double dt
);

static void visualize_bars(int hopSize);
static void draw_timestamp(TTF_Font* font, AudioData audio);
static void set_color_rainbow(SDL_Color* color, float progress);
static void handle_key_events(SDL_Window* window, SDL_Event *event);
static void cleanup(void);

#if defined(_WIN32)
static void trim_memory() {
    HANDLE hHeap = GetProcessHeap();
    HeapCompact(hHeap, 0);
    SetProcessWorkingSetSize(GetCurrentProcess(), -1, -1);
}
#endif

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_LogCritical(SDL_LOG_CATEGORY_VIDEO, "%s", SDL_GetError());
        cleanup();
        return EXIT_FAILURE;
    }

    if (!TTF_Init()) {
        SDL_LogCritical(SDL_LOG_CATEGORY_VIDEO, "%s", SDL_GetError());
        cleanup();
        return EXIT_FAILURE;
    } 

    TTF_Font* font = NULL;
    const char* font_paths[] = {
#if defined(_WIN32)
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
#else // Linux, BSD, etc.
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
#endif
        NULL
    };

    for (int i = 0; font_paths[i] != NULL; i++) {
        font = TTF_OpenFont(font_paths[i], TIMESTAMP_FONT_SIZE);
        if (font) {
            break;
        }
    }

    if (!font) {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "Error",
            "Could not open any system font",
            NULL
        );
    }

    audio.mutex = SDL_CreateMutex();
    if (!audio.mutex) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "%s", SDL_GetError(), window);
        cleanup();
        return EXIT_FAILURE;
    }

    SDL_Surface* icon = IMG_Load("ac.ico");
    if (!icon) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "%s", SDL_GetError(), window);
        cleanup();
        return EXIT_FAILURE;
    }

    if (argv[1]) {
        if (!open_audio_file(window, argv[1], &audio)) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "%s", SDL_GetError(), window);
            cleanup();
            return EXIT_FAILURE;
        }

        audio.stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
            &audio.spec,
            audio_stream_callback,
            &audio
        );
        if (!audio.stream) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "%s", SDL_GetError(), window);
            cleanup();
            return EXIT_FAILURE;
        }


        if (!SDL_SetAudioStreamGain(audio.stream, gain)){
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "%s", SDL_GetError(), window);
        }

        set_audio_speed(audio.stream, &speed);

        audio.deviceId = SDL_GetAudioStreamDevice(audio.stream);
        if (isPlaying)
        {
            if (!SDL_ResumeAudioDevice(audio.deviceId))
            {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "%s", SDL_GetError(), window);
                cleanup();
                return EXIT_FAILURE;
            }
        }
    }

    const SDL_DisplayMode* display;
    const SDL_DisplayID *displayIds;
    int displayIdCount;
    
    displayIds = SDL_GetDisplays(&displayIdCount);
    if (displayIdCount <= 0) {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "Error",
            "Failed to get display IDs",
            window
        );
        cleanup();
        return EXIT_FAILURE;
    } 

    if (!(display = SDL_GetCurrentDisplayMode(displayIds[0]))) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "%s", SDL_GetError(), window);
        cleanup();
        return EXIT_FAILURE;
    }

    window = SDL_CreateWindow(
        WINDOW_TITLE,
        display->w / WINDOW_WIDTH_FACTOR,
        display->h / WINDOW_HEIGHT_FACTOR,
        WINDOW_FLAGS
    );
    if (!window) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "%s", SDL_GetError(), window);
        cleanup();
        return EXIT_FAILURE;
    }

    if (WINDOW_OPACITY >= 0.0f && WINDOW_OPACITY <= 1.0f) {
        SDL_SetWindowOpacity(window, WINDOW_OPACITY);
    }
    SDL_SetWindowIcon(window, icon);

    renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "%s", SDL_GetError(), window);
        cleanup();
        return EXIT_FAILURE;
    }

    rects = create_bars(window, BAR_NUMBER);
    SDL_Event event;
    Uint64 lastTick = SDL_GetTicks();

    while (isRunning) {
        Uint64 currentTick = SDL_GetTicks();
        double dt = (currentTick - lastTick) / 1000.0f;
        lastTick = currentTick;

        float speed = 2000.0f;
        float progress = (currentTick % (int)speed) / speed;

        handle_key_events(window, &event);

        if (audio.spec.freq) {
            visualize_bars(
                (FFT_HOP_SIZE == 0) ?
                audio.spec.freq / display->refresh_rate :
                FFT_HOP_SIZE
            );
        }

        if (isRainbow) {
            set_color_rainbow(&barColor, progress);
        } else {
            barColor = BAR_COLOR;
        }
        should_audio_loop(&audio, isLooping);

        SDL_SetRenderDrawColor(
            renderer,
            BACKGROUND_COLOR.r,
            BACKGROUND_COLOR.g,
            BACKGROUND_COLOR.b,
            BACKGROUND_COLOR.a
        );
        SDL_RenderClear(renderer);

        draw_bars(rects, BAR_NUMBER, dt);

        if (showTimestamp && font) {
            draw_timestamp(font, audio);
        }

        SDL_RenderPresent(renderer);
    }

    cleanup();
    return EXIT_SUCCESS;
}

bool open_audio_file(SDL_Window* window, const char* filepath, AudioData* audio) {
    SF_INFO sfinfo = { 0 };

#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath, -1, NULL, 0);
    wchar_t* wpath = malloc(wlen * sizeof(wchar_t));

    MultiByteToWideChar(CP_UTF8, 0, filepath, -1, wpath, wlen);
    SDL_LockMutex(audio->mutex);
    audio->file = sf_wchar_open(wpath, SFM_READ, &sfinfo);
    SDL_UnlockMutex(audio->mutex);
    free(wpath);
#else 
    SDL_LockMutex(audio->mutex);
    audio->file = sf_open(filepath, SFM_READ, &sfinfo);
    SDL_UnlockMutex(audio->mutex);
#endif

    if (!audio->file) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", sf_strerror(NULL), window);
        return false;
    }

    if (!sf_format_check(&sfinfo)) {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "Error",
            "Unsupported audio format",
            window
        );
        sf_close(audio->file);
        audio->file = NULL;
        return false;
    }

    audio->pos = 0;
    SDL_LockMutex(audio->mutex);
    audio->spec.format = SDL_AUDIO_F32;
    audio->spec.freq = sfinfo.samplerate;
    audio->spec.channels = sfinfo.channels;
    SDL_UnlockMutex(audio->mutex);
    audio->frameSize = SDL_AUDIO_BITSIZE(audio->spec.format) / 8 * audio->spec.channels;
    audio->frames = sfinfo.frames;
    return true;
}

void file_dialog_callback(void* userdata, const char* const* filelist, int filter) {
    if (!filelist || *filelist == NULL) {
        return;
    }

    AudioData* audio = (AudioData*)userdata;

    if (audio->file) {
        SDL_LockMutex(audio->mutex);
        sf_close(audio->file);
        audio->file = NULL;
        SDL_UnlockMutex(audio->mutex);
    }

    if (!open_audio_file(window, filelist[0], audio)) {
        return;
    }

    if (audio->stream) {
        SDL_ClearAudioStream(audio->stream);
        SDL_SetAudioStreamFormat(audio->stream, &audio->spec, NULL);
    } else {
        audio->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio->spec, audio_stream_callback, audio);
        if (!audio->stream) {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", SDL_GetError());
        }
    }

    audio->deviceId = SDL_GetAudioStreamDevice(audio->stream);

    if (!SDL_ResumeAudioDevice(audio->deviceId)) {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", SDL_GetError());
    }

    if (!SDL_SetAudioStreamGain(audio->stream, pow(10, gain / 20))) {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", SDL_GetError());
    }

    set_audio_speed(audio->stream, &speed);
    isPlaying = true;
    isFinished = false;

#if defined(_WIN32)
    trim_memory();
#endif
}

void audio_stream_callback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount) {
    AudioData* audio = (AudioData*)userdata;
    sf_count_t frames = additionalAmount / audio->frameSize;

    visSpec.format = audio->spec.format;
    visSpec.channels = 1;
    visSpec.freq = 
        (FFT_DESIRED_SAMPLERATE == 0) ?
        audio->spec.freq :
        FFT_DESIRED_SAMPLERATE; 

    float* buf = (float*)malloc(frames * audio->frameSize);
    size_t lenBuf = 0;

    SDL_LockMutex(audio->mutex);
    sf_count_t framesRead = sf_readf_float(audio->file, buf, frames);
    SDL_UnlockMutex(audio->mutex);

    size_t bytesRead = framesRead * audio->frameSize;
    SDL_PutAudioStreamData(stream, buf, bytesRead);

    audio_change_spec(&buf, &bytesRead, &audio->spec, &visSpec);

    lenBuf = bytesRead / sizeof(float);

    for (int i = 0; i < lenBuf; ++i) {
        ringBuffer[ringPos] = buf[i];
        ringPos = (ringPos + 1) % RING_BUFFER_SIZE;

        totalWritten++;
    }

    free(buf);

    audio->pos += framesRead;

    if (audio->pos >= (audio->frames - (5 * audio->spec.freq))) {
        for (int i = 0; i < BAR_NUMBER; i++) {
            barMag[i] = 0;
        }
        isFinished = true;
    }
}

float* create_hann_window(size_t n) {
    float* window = malloc(n * sizeof(float));
    if (!window) return NULL;

    const float TWO_PI = 2.0f * M_PI;
    const float DENOM = (float)(n - 1);
    for (size_t i = 0; i < n; i++) {
        window[i] = 0.5f * (1.0f - cosf(TWO_PI * i / DENOM));
    }

    return window;
}

void fft_audio(float* barMag, float* audioBuf, size_t fftSize) {
    const int hfftSize = fftSize / 2;
    const float windowGain = 2.0f / 3.0f;

    static kiss_fftr_cfg cfg = NULL;
    static float* hann = NULL;
    static kiss_fft_cpx* fout = NULL;
    static float* fftMag = NULL;

    float barSum[BAR_NUMBER] = { 0 };
    float count[BAR_NUMBER] = { 0 };

    if (!cfg) {
        cfg = kiss_fftr_alloc(fftSize, 0, NULL, NULL);
    }

    if (!fout) {
        fout = (kiss_fft_cpx*)malloc((hfftSize+ 1) * sizeof(kiss_fft_cpx));
    }

    if (!fftMag) {
        fftMag = (float*)malloc((hfftSize + 1) * sizeof(float));
    }

    if (!hann) {
        hann = create_hann_window(fftSize);
    }

    for (size_t i = 0; i < fftSize; ++i) {
        audioBuf[i] *= hann[i];
    }

    kiss_fftr(cfg, audioBuf, fout);

    for (size_t k = 0; k <= hfftSize; k++) {
        float r = fout[k].r;
        float i = fout[k].i;
        float amp = sqrtf(r * r + i * i);
        float scale = (k == 0 || k == hfftSize) ? 1.0f : 2.0f;

        fftMag[k] = scale * amp * windowGain;
    }

    float binFreq = (float) visSpec.freq * speed / fftSize;
    float logMin = log2f(MIN_FREQ);
    float logMax = log2f(MAX_FREQ);

    float bandEdge[BAR_NUMBER + 1];
    for (int i = 0; i <= BAR_NUMBER; ++i) {
        float t = ( float )i / BAR_NUMBER;
        float logFreq = logMin + t * (logMax - logMin);
        bandEdge[i] = powf(2.0f, logFreq);
    }

    for (int bar = 0; bar < BAR_NUMBER; ++bar) {
        float f1 = bandEdge[bar];
        float f2 = bandEdge[bar + 1];

        int b1 = (int)(f1 / binFreq);
        int b2 = (int)(f2 / binFreq);

        if (b1 < 0) b1 = 0;
        if (b2 >= hfftSize) b2 = hfftSize - 1;
        if (b2 < b1) b2 = b1;

        float sumSq = 0.f;
        int cnt = 0;

        for (int k = b1; k <= b2; ++k) {
            float v = fftMag[k];
            sumSq += v * v;
            cnt++;
        }

        float mag = (cnt > 0) ? sqrtf(sumSq / cnt) : 0.f;
        mag = fmaxf(mag, 1e-10f);

        float centerFreq = sqrtf(f1 * f2);

        float db = 20.f * log10f(mag / fftSize);

        const float bassFc   = 200.0f;
        const float bassRamp = 150.0f;
        const float bassBoost = BASS_BOOST;

        float lowT = (bassFc - centerFreq) / bassRamp;
        if (lowT < 0.0f) lowT = 0.0f;
        if (lowT > 1.0f) lowT = 1.0f;

        lowT = lowT * lowT * (3.0f - 2.0f * lowT);

        db += bassBoost * lowT;

        const float trebleFc = 2000.0f;
        const float trebleRamp = 2000.0f;
        const float trebleBoost = TREBLE_BOOST;

        float highT = (centerFreq - trebleFc) / trebleRamp;

        if (highT < 0.0f) highT = 0.0f;
        if (highT > 1.0f) highT = 1.0f;

        highT = highT * highT * (3.0f - 2.0f * highT);

        db += trebleBoost * highT;

        float norm = (db - MIN_DB) / (MAX_DB - MIN_DB);

        const float pivot = 0.18f;
        const float contrast = VISUAL_CONTRAST;

        norm = pivot + (norm - pivot) * contrast;

        norm = fmaxf(0.f, fminf(1.f, norm));

        norm = powf(norm, 1.6f);

        barSum[bar] += norm;
        count[bar]++;
    }

    for (int bar = 0; bar < BAR_NUMBER; ++bar) {
        if (count[bar] > 0) {
          barMag[bar] = barSum[bar] / count[bar];
        }
        else {
          barMag[bar] = 0.0f;
        }
    }
}

void seek_audio(AudioData* audio, SDL_AudioStream* stream, double seconds) {
    if (!stream || !audio->file) {
        return;
    }

    sf_count_t frames = (sf_count_t) (seconds * ( double )audio->spec.freq);

    sf_count_t newPos = audio->pos + frames;
    SDL_LockMutex(audio->mutex);
    sf_count_t lenFile = sf_seek(audio->file, 0, SF_SEEK_END);
    SDL_UnlockMutex(audio->mutex);
    if (newPos > lenFile) {
        newPos = lenFile;
    } else if (newPos < 0) {
        newPos = 0;
    }

    SDL_LockMutex(audio->mutex);
    sf_count_t result = sf_seek(audio->file, newPos, SF_SEEK_SET);
    SDL_UnlockMutex(audio->mutex);
    if (result == -1) {
        fprintf(stderr, "Failed to seek backward\n");
        return;
    }
    audio->pos = result;

    SDL_ClearAudioStream(stream);
}

void set_audio_speed(SDL_AudioStream* stream, float* speed) {
    if (*speed > MAX_SPEED) {
        *speed = MAX_SPEED;
    }
    if (*speed < MIN_SPEED) {
        *speed = MIN_SPEED;
    }

    SDL_SetAudioStreamFrequencyRatio(stream, *speed);
}

void should_audio_loop(AudioData* audio, bool loop) {
    if (!loop) {
        return;
    }

    if (audio->pos == audio->frames) {
        double secondsFromStart = (double) audio->frames / (double) audio->spec.freq;
        seek_audio(audio, audio->stream, -secondsFromStart);
        isFinished = false;
    }
}

SDL_FRect* create_bars(SDL_Window* window, size_t barNum) {
    int width = 0, height = 0;

    if (!SDL_GetWindowSize(window, &width, &height)) {
        printf("ERROR: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    SDL_FRect* rects = malloc(sizeof(SDL_FRect) * barNum);
    if (!rects) {
        return NULL;
    }

    double k = BAR_SEPARATION;

    double idealBar = (double)width / (barNum + (barNum - 1) * k);
    int barWidth = (int)idealBar;

    int gapCount = barNum - 1;
    int remaining = width - barWidth * barNum;

    int separation = gapCount > 0 ? remaining / gapCount : 0;
    int extra = gapCount > 0 ? remaining % gapCount : 0;

    int x = 0;

    for (size_t i = 0; i < barNum; i++) {
        rects[i].x = x;
        rects[i].y = height;
        rects[i].w = barWidth;
        rects[i].h = 0;

        x += barWidth;

        if (i < gapCount) {
            x += separation;

            if (extra > 0) {
                x += 1;
                extra--;
            }
        }
    }

    return rects;
}

bool draw_bars(SDL_FRect* rects, size_t barNum, double dt) {
    int width, height;
    SDL_GetWindowSize(window, &width, &height);

    for (size_t i = 0; i < barNum; ++i) {
        float target = barMag[i] * height;
        if (!isPlaying) {
            target = 0;
        }

        float scale = isMirrored ? 0.375f : 0.75f;
        target *= scale;

        float k = (target > rects[i].h) ?
            1.0f - expf(-ATTACK * dt) :
            1.0f - expf(-DECAY * dt);

        rects[i].h += (target - rects[i].h) * k;

        if (rects[i].h < BAR_MIN_HEIGHT) {
            rects[i].h = BAR_MIN_HEIGHT;
        }
    }

    SDL_SetRenderDrawColor(renderer, barColor.r, barColor.g, barColor.b, barColor.a);

    if (isMirrored) {
        float centerY = (float)height / 2.0f;

        for (size_t i = 0; i < barNum; i++) {
            rects[i].y = centerY - rects[i].h;
        }
        SDL_RenderFillRects(renderer, rects, (int)barNum);

        for (size_t i = 0; i < barNum; i++) {
            rects[i].y = centerY;
        }
        SDL_RenderFillRects(renderer, rects, (int)barNum);

    } else {
        for (size_t i = 0; i < barNum; i++) {
            rects[i].y = (float)height - rects[i].h;
        }
        SDL_RenderFillRects(renderer, rects, (int)barNum);
    }

    return true;
}

void cleanup(void) {
    if (window) {
        SDL_DestroyWindow(window);
    }

    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }

    if (audio.stream) {
        SDL_FlushAudioStream(audio.stream);
        SDL_DestroyAudioStream(audio.stream);
    }

    if (audio.file) {
        sf_close(audio.file);
    }

    if (audio.mutex) {
        SDL_DestroyMutex(audio.mutex);
    }

    if (audio.deviceId) {
        SDL_CloseAudioDevice(audio.deviceId);
    }

    SDL_Quit();
    TTF_Quit();
}

void set_color_rainbow(SDL_Color* color, float progress) {
    progress = fmodf(progress, 1.0f);
    if (progress < 0) progress += 1.0f;

    float h = progress * 6.0f;
    float x = 1.0f - fabsf(fmodf(h, 2.0f) - 1.0f);

    float r = 0, g = 0, b = 0;

    if (h < 1) { r = 1; g = x; b = 0; }
    else if (h < 2) { r = x; g = 1; b = 0; }
    else if (h < 3) { r = 0; g = 1; b = x; }
    else if (h < 4) { r = 0; g = x; b = 1; }
    else if (h < 5) { r = x; g = 0; b = 1; }
    else { r = 1; g = 0; b = x; }

    color->r = (Uint8) (r * 255);
    color->g = (Uint8) (g * 255);
    color->b = (Uint8) (b * 255);
    color->a = 255;
}

void visualize_bars(int hopSize) {
    static uint64_t lastSample = 0;

    while (totalWritten - lastSample >= hopSize) {
        float window[FFT_SIZE];

        uint64_t endSample = totalWritten;
        uint64_t startSample = endSample - FFT_SIZE;

        for (int i = 0; i < FFT_SIZE; ++i) {
            uint64_t iSample = startSample + i;
            uint64_t iRing = iSample % RING_BUFFER_SIZE;
            window[i] = ringBuffer[iRing];
        }

        fft_audio(barMag, window, FFT_SIZE);

        lastSample += hopSize;
    }
}

void audio_change_spec(float** buf, size_t* len, SDL_AudioSpec* srcSpec, SDL_AudioSpec* dstSpec) {
    if (srcSpec->format == dstSpec->format &&
        srcSpec->channels == dstSpec->channels &&
        srcSpec->freq == dstSpec->freq) {
        return;
    }

    SDL_AudioStream* stream = SDL_CreateAudioStream(srcSpec, dstSpec);
    if (!stream) return;

    SDL_PutAudioStreamData(stream, *buf, (int)(*len));
    free(*buf);

    SDL_FlushAudioStream(stream);

    int outBytes = SDL_GetAudioStreamAvailable(stream);
    if (outBytes <= 0) {
        *buf = NULL;
        *len = 0;
        SDL_DestroyAudioStream(stream);
        return;
    }

    *buf = (float*)malloc(outBytes);
    if (!*buf) {
        SDL_DestroyAudioStream(stream);
        *len = 0;
        return;
    }

    SDL_GetAudioStreamData(stream, *buf, outBytes);
    *len = (size_t)outBytes;

    SDL_DestroyAudioStream(stream);
}

void handle_key_events(SDL_Window* window, SDL_Event *event) {
    while (SDL_PollEvent(event)) {
        switch (event->type) {

        case SDL_EVENT_QUIT:
            isRunning = false;
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            if (rects) {
                free(rects);
            }
            rects = create_bars(window, BAR_NUMBER);
            break;

        case SDL_EVENT_KEY_DOWN:
            switch (event->key.key) {

            case SDLK_Q:
                isRunning = false;
                break;

            case SDLK_E:
                if (event->key.repeat != 0) {
                    break;
                }
                SDL_ShowOpenFileDialog(
                    file_dialog_callback,
                    &audio,
                    window,
                    filters,
                    nFilters,
                    "./",
                    false
                );
                break;

            case SDLK_R:
                if (event->key.repeat != 0) {
                    break;
                }
                isRainbow = !isRainbow;
                break;
            
            case SDLK_T:
                if (event->key.repeat != 0) {
                    break;
                }

                showTimestamp = !showTimestamp;
                break;

            case SDLK_V:
                if (event->key.repeat != 0) {
                    break;
                }
                isMirrored = !isMirrored;
                break;

            case SDLK_A:
                if (!audio.stream) {
                    break;
                }
                speed -= 0.05f;
                set_audio_speed(audio.stream, &speed);
                break;

            case SDLK_D:
                if (!audio.stream) {
                    break;
                }
                speed += 0.05f;
                set_audio_speed(audio.stream, &speed);
                break;

            case SDLK_S:
                if (!audio.stream) {
                    break;
                }
                speed = 1.0f;
                set_audio_speed(audio.stream, &speed);
                break;

            case SDLK_RIGHT:
                if (!audio.stream)
                    break;

                if (event->key.mod & SDL_KMOD_LCTRL) {
                case SDLK_MEDIA_NEXT_TRACK:
                    double secondsToEnd =
                        (double)audio.frames / (double)audio.spec.freq;
                    seek_audio(&audio, audio.stream, secondsToEnd);
                }

                seek_audio(&audio, audio.stream, SEEK_FORWARD);
                break;

            case SDLK_LEFT:
                if (!audio.stream)
                    break;

                if (event->key.mod & SDL_KMOD_LCTRL) {
                case SDLK_MEDIA_PREVIOUS_TRACK:
                    if (isFinished)
                        isFinished = false;
                    double secondsFromStart =
                        (double)audio.frames / (double)audio.spec.freq;
                    seek_audio(&audio, audio.stream, -secondsFromStart);
                }

                if (isFinished)
                    isFinished = false;

                seek_audio(&audio, audio.stream, -SEEK_FORWARD);
                break;

            case SDLK_L:
                if (event->key.repeat != 0) {
                    break;
                }
                if (!audio.stream)
                    break;
                isLooping = !isLooping;
                break;

            case SDLK_M:
                if (event->key.repeat != 0) {
                    break;
                }

                if (!audio.stream)
                    break;

                isMuted = !isMuted;

                if (isMuted) {
                    if (!SDL_SetAudioDeviceGain(audio.deviceId, 0.0f)) {
                        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", SDL_GetError());
                    }
                } else {
                    if (!SDL_SetAudioDeviceGain(
                            audio.deviceId, pow(10, (gain / 20)))) {
                        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", SDL_GetError());
                    }
                }
                break;

            case SDLK_UP:
                if (!audio.stream || isMuted)
                    break;

                gain += 20.f * log10f(GAIN_STEP);
                if (gain > MAX_GAIN)
                    gain = MAX_GAIN;

                if (!SDL_SetAudioStreamGain(audio.stream, pow(10, gain / 20))) {
                    SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", SDL_GetError());
                }
                break;

            case SDLK_DOWN:
                if (!audio.stream || isMuted)
                    break;

                gain -= 20.f * log10f(GAIN_STEP);
                if (gain < MIN_GAIN)
                    gain = MIN_GAIN;

                if (!SDL_SetAudioStreamGain(audio.stream, pow(10, gain / 20))) {
                    SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", SDL_GetError());
                }
                break;

            case SDLK_SPACE:
            case SDLK_MEDIA_PLAY_PAUSE:
                if (event->key.repeat != 0) {
                    break;
                }

                if (!audio.stream) {
                    break;
                }

                if (isFinished) {
                    double backsec =
                        (double)audio.frames / (double)audio.spec.freq;
                    seek_audio(&audio, audio.stream, -backsec);
                    isFinished = false;
                    break;
                }

                isPlaying = !isPlaying;

                if (isPlaying && SDL_AudioDevicePaused(audio.deviceId)) {
                    if (!SDL_ResumeAudioDevice(audio.deviceId)) {
                        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", SDL_GetError());
                        cleanup();
                        return;
                    }
                }

                if (!isPlaying && !SDL_AudioDevicePaused(audio.deviceId)) {
                    if (!SDL_PauseAudioDevice(audio.deviceId)) {
                        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", SDL_GetError());
                        cleanup();
                        return;
                    }
                }
                break;

            default:
                break;
            }
            break;

        default:
            break;
        }
    }
}

void draw_timestamp(TTF_Font* font, AudioData audio) {
    if (!audio.file) {
        return;
    }

    int width = 0, height = 0;

    if (!SDL_GetWindowSize(window, &width, &height)) {
        printf("ERROR: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    char timeStamp[32];

    int currentMinute = (audio.pos / audio.spec.freq) / 60;
    int currentSecond = (audio.pos / audio.spec.freq) % 60;

    int totalMinute = (audio.frames / audio.spec.freq) / 60;
    int totalSecond = (audio.frames / audio.spec.freq) % 60;

    snprintf(timeStamp, sizeof(timeStamp), "%d:%02d / %d:%02d", currentMinute, currentSecond, totalMinute, totalSecond);

    SDL_Color white = {255, 255, 255, 200};

    SDL_Surface *surface = TTF_RenderText_Blended(font, timeStamp, strlen(timeStamp), white);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);

    float w, h;
    SDL_GetTextureSize(texture, &w, &h);

    SDL_FRect dst;
    dst.x = width - w - 10;
    dst.y = 10;
    dst.w = w;
    dst.h = h;

    SDL_RenderTexture(renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);

    float progress = (float)audio.pos / audio.frames;
    float barWidth = width * progress;

    SDL_FRect bar = {0, 0, barWidth, 2};
    SDL_SetRenderDrawColor(renderer, 255,255,255,120);
    SDL_RenderFillRect(renderer, &bar);
}
