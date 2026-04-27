#include "config.h"

#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3/SDL.h>
#include <sndfile.h>
#include <kiss_fftr.h>

#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#endif

typedef struct {
  SNDFILE* file;
  sf_count_t pos;
  sf_count_t frames;
  int frameSize;
  SDL_AudioSpec spec;
  SDL_AudioDeviceID deviceId;
  SDL_AudioStream* stream;
  SDL_Mutex* mutex;
} AudioData;

typedef struct {
  SDL_Window* window;
  AudioData* audio;
} FileDialogData;

SDL_DialogFileFilter filters[] = {
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

const size_t nFilters = sizeof(filters) / sizeof(filters[0]);

static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;
static FileDialogData dialog;
static AudioData audio = { 0 };
static float barMag[BAR_NUMBER] = { 0 };
static SDL_Color barColor = BAR_COLOR;
static SDL_FRect* rects = NULL;
static float gain = MAX_GAIN;
static float speed = 1.f;
static double dt;

static float ring_buffer[FFT_SIZE];
static int ring_write_pos = 0;
static uint64_t total_written = 0;

bool isRunning = true;
bool isRainbow = false;
bool isPlaying = true;
bool isLooping = false;
bool isMuted = false;
bool isFinished = false;
bool isFileDialogOpen = false;
bool isMirrored = false;
bool showTimestamp = true;

bool open_audio_file(
  SDL_Window* window,
  const char* filePath,
  AudioData* audio
);

void file_dialog_callback(
  void* userdata,
  const char* const* fileList,
  int filter
);

void audio_stream_callback(
  void* userdata,
  SDL_AudioStream* stream,
  int additional_amount,
  int total_amount
);

float* create_hann_window(size_t n);

void fft_audio(
  float* barMag,
  float* audioBuf,
  size_t fft_size
);

void seek_audio(
  AudioData* audio,
  SDL_AudioStream* stream,
  double seconds
);

void set_audio_speed(SDL_AudioStream* stream, float* speed);
void should_audio_loop(AudioData* audio, bool loop);
SDL_FRect* create_bars(SDL_Window* window, size_t bar_num);

bool draw_bars(
  SDL_FRect* rects,
  size_t nBar,
  double dt
);

const char* basename(const char* path);
void cleanup(void);

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

void visualize_bars(int hop_size) {
  static uint64_t last_fft_sample = 0;

  while (total_written - last_fft_sample >= hop_size) {
    float window[FFT_SIZE];

    uint64_t end_sample = total_written;
    uint64_t start_sample = end_sample - FFT_SIZE;

    for (int i = 0; i < FFT_SIZE; ++i) {
      uint64_t sample_index = start_sample + i;
      uint64_t ring_index = sample_index % FFT_SIZE;
      window[i] = ring_buffer[ring_index];
    }

    fft_audio(barMag, window, FFT_SIZE);

    last_fft_sample += hop_size;
  }
}

void audio_change_spec(float** buf, size_t* len,
                       SDL_AudioSpec* src_spec,
                       SDL_AudioSpec* dst_spec)
{
    if (src_spec->format == dst_spec->format &&
        src_spec->channels == dst_spec->channels &&
        src_spec->freq == dst_spec->freq)
    {
        return;
    }

    SDL_AudioStream* stream = SDL_CreateAudioStream(src_spec, dst_spec);
    if (!stream) return;

    SDL_PutAudioStreamData(stream, *buf, (int)(*len));
    free(*buf);

    SDL_FlushAudioStream(stream);

    int out_bytes = SDL_GetAudioStreamAvailable(stream);
    if (out_bytes <= 0) {
        *buf = NULL;
        *len = 0;
        SDL_DestroyAudioStream(stream);
        return;
    }

    *buf = (float*)malloc(out_bytes);
    if (!*buf) {
        SDL_DestroyAudioStream(stream);
        *len = 0;
        return;
    }

    SDL_GetAudioStreamData(stream, *buf, out_bytes);
    *len = (size_t)out_bytes;

    *src_spec = *dst_spec;

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
                if (!isFileDialogOpen) {
                    isFileDialogOpen = true;
                    SDL_ShowOpenFileDialog(file_dialog_callback, &dialog, window,
                                           filters, nFilters, "./", false);
                }
                break;

            case SDLK_R:
                isRainbow = !isRainbow;
                break;
            
            case SDLK_T:
                showTimestamp = !showTimestamp;
                break;

            case SDLK_V:
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
                speed = 1.f;
                set_audio_speed(audio.stream, &speed);
                break;

            case SDLK_RIGHT:
                if (!audio.stream)
                    break;

                if (event->key.mod & SDL_KMOD_LCTRL) {
                case SDLK_MEDIA_NEXT_TRACK:
                    double fileEndSec =
                        (double)audio.frames / (double)audio.spec.freq;
                    seek_audio(&audio, audio.stream, fileEndSec);
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
                    double fileStartSec =
                        (double)audio.frames / (double)audio.spec.freq;
                    seek_audio(&audio, audio.stream, -fileStartSec);
                }

                if (isFinished)
                    isFinished = false;

                seek_audio(&audio, audio.stream, -SEEK_FORWARD);
                break;

            case SDLK_L:
                if (!audio.stream)
                    break;
                isLooping = !isLooping;
                break;

            case SDLK_M:
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

            case SDLK_MEDIA_PLAY_PAUSE:
            case SDLK_SPACE:
                if (!audio.stream)
                    break;

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

    if (!SDL_GetWindowSize(window, &width, &height))
    {
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

int main(int argc, char** argv) {
  if (argc > 2)
  {
    printf("Usage: %s <audio file>\nor\nUsage: %s", basename(argv[0]), basename(argv[0]));
    cleanup();
    return EXIT_FAILURE;
  }

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
  {
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

#ifdef _WIN32
  font = TTF_OpenFont("C:/Windows/Fonts/arial.ttf", 18);
#else
  font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 18);
#endif

  if(!font) {
    SDL_LogCritical(SDL_LOG_CATEGORY_VIDEO, "%s", SDL_GetError());
    cleanup();
    return EXIT_FAILURE;
  }

  audio.mutex = SDL_CreateMutex();
  if (!audio.mutex)
  {
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
    cleanup();
    return EXIT_FAILURE;
  }

  SDL_Surface* icon = IMG_Load("ac.ico");
  if (!icon)
  {
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
    cleanup();
    return EXIT_FAILURE;
  }

  if (argv[1])
  {
    if (!open_audio_file(window, argv[1], &audio))
    {
      SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Failed to load \"%s\"", basename(argv[1]));
      cleanup();
      return EXIT_FAILURE;
    }

    audio.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio.spec, audio_stream_callback, &audio);
    if (!audio.stream)
    {
      SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", SDL_GetError());
      cleanup();
      return EXIT_FAILURE;
    }


    if (!SDL_SetAudioStreamGain(audio.stream, gain))
      SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", SDL_GetError());

    set_audio_speed(audio.stream, &speed);

    audio.deviceId = SDL_GetAudioStreamDevice(audio.stream);
    if (isPlaying)
    {
      if (!SDL_ResumeAudioDevice(audio.deviceId))
      {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", SDL_GetError());
        cleanup();
        return EXIT_FAILURE;
      }
    }
  }

  const SDL_DisplayMode* display;
  if (!(display = SDL_GetCurrentDisplayMode(1)))
  {
    SDL_LogCritical(SDL_LOG_CATEGORY_VIDEO, "%s", SDL_GetError());
    cleanup();
    return EXIT_FAILURE;
  }

  const int WINDOW_WIDTH = display->w / WINDOW_WIDTH_FACTOR;
  const int WINDOW_HEIGHT = display->h / WINDOW_HEIGHT_FACTOR;
  const Uint64 FRAME_RATE = display->refresh_rate;

  window = SDL_CreateWindow(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_FLAGS);
  if (!window)
  {
    SDL_LogCritical(SDL_LOG_CATEGORY_VIDEO, "%s", SDL_GetError());
    cleanup();
    return EXIT_FAILURE;
  }
  SDL_SetWindowOpacity(window, WINDOW_OPACITY);
  SDL_SetWindowIcon(window, icon);

  renderer = SDL_CreateRenderer(window, NULL);
  if (!renderer)
  {
    SDL_LogCritical(SDL_LOG_CATEGORY_VIDEO, "%s", SDL_GetError());
    cleanup();
    return EXIT_FAILURE;
  }

  dialog.window = window;
  dialog.audio = &audio;

  rects = create_bars(window, BAR_NUMBER);
  SDL_Event event;
  Uint64 last_tick = SDL_GetTicks();

  while (isRunning)
  {
    Uint64 current_tick = SDL_GetTicks();
    double dt = (current_tick - last_tick) / 1000.0f;
    last_tick = current_tick;

    float speed = 2000.0f;
    float progress = (current_tick % ( int )speed) / speed;

    handle_key_events(window, &event);

    if (audio.spec.freq) {
      visualize_bars(audio.spec.freq / display->refresh_rate);
    }

    if (isRainbow)
      set_color_rainbow(&barColor, progress);
    else
      barColor = BAR_COLOR;
    should_audio_loop(&audio, isLooping);

    SDL_SetRenderDrawColor(renderer, BACKGROUND_COLOR.r, BACKGROUND_COLOR.g, BACKGROUND_COLOR.b, BACKGROUND_COLOR.a);
    SDL_RenderClear(renderer);

    draw_bars(rects, BAR_NUMBER, dt);

    if (showTimestamp) {
      draw_timestamp(font, audio);
    }

    SDL_RenderPresent(renderer);
  }

  cleanup();
  return EXIT_SUCCESS;
}

bool open_audio_file(SDL_Window* window, const char* filepath, AudioData* audio)
{
  SF_INFO sfinfo = { 0 };

#ifdef _WIN32
  int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath, -1, NULL, 0);
  wchar_t* wpath = malloc(wlen * sizeof(wchar_t));

  MultiByteToWideChar(CP_UTF8, 0, filepath, -1, wpath, wlen);
  audio->file = sf_wchar_open(wpath, SFM_READ, &sfinfo);
  free(wpath);
#else 
  audio->file = sf_open(filepath, SFM_READ, &sfinfo);
#endif

  if (!audio->file)
  {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", sf_strerror(NULL), window);
    return false;
  }

  if (!sf_format_check(&sfinfo))
  {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Unsupported format", window);
    sf_close(audio->file);
    audio->file = NULL;
    return false;
  }

  audio->pos = 0;
  audio->spec.format = SDL_AUDIO_F32;
  audio->spec.freq = sfinfo.samplerate;
  audio->spec.channels = sfinfo.channels;
  audio->frameSize = SDL_AUDIO_BITSIZE(audio->spec.format) / 8 * audio->spec.channels;
  audio->frames = sfinfo.frames;
  return true;
}

void file_dialog_callback(void* userdata, const char* const* filelist, int filter)
{
  if (!filelist || *filelist == NULL) {
    isFileDialogOpen = false;
    return;
  }

  FileDialogData* dialog = ( FileDialogData* )userdata;

  if (dialog->audio->file) {
    SDL_LockMutex(dialog->audio->mutex);
    sf_close(dialog->audio->file);
    SDL_UnlockMutex(dialog->audio->mutex);
    dialog->audio->file = NULL;
  }

  if (!open_audio_file(dialog->window, filelist[0], dialog->audio)) {
    isFileDialogOpen = false;
    return;
  }

  if (dialog->audio->stream) {
    SDL_LockMutex(dialog->audio->mutex);
    SDL_ClearAudioStream(dialog->audio->stream);
    SDL_SetAudioStreamFormat(dialog->audio->stream, &dialog->audio->spec, &dialog->audio->spec);
    SDL_UnlockMutex(dialog->audio->mutex);
  }
  else {
    SDL_LockMutex(dialog->audio->mutex);
    dialog->audio->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &dialog->audio->spec, audio_stream_callback, dialog->audio);
    SDL_UnlockMutex(dialog->audio->mutex);
    if (!dialog->audio->stream)
      SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", SDL_GetError());
  }

  SDL_LockMutex(dialog->audio->mutex);
  dialog->audio->deviceId = SDL_GetAudioStreamDevice(dialog->audio->stream);
  SDL_UnlockMutex(dialog->audio->mutex);

  if (!SDL_ResumeAudioDevice(dialog->audio->deviceId)) {
    SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", SDL_GetError());
  }

  if (!SDL_SetAudioStreamGain(dialog->audio->stream, pow(10, gain / 20))) {
    SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "%s", SDL_GetError());
  }

  set_audio_speed(dialog->audio->stream, &speed);
  isPlaying = true;
  isFinished = false;
  isFileDialogOpen = false;
  return;
}

void audio_stream_callback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount)
{
  AudioData* audio = (AudioData*) userdata;
  sf_count_t frames = additional_amount / audio->frameSize;

  SDL_AudioSpec dst_spec = {
        .format = audio->spec.format,
        .channels = 1,
        .freq = audio->spec.freq 
  };

  float* buf = (float*) malloc(additional_amount);
  size_t buf_len = 0;

  SDL_LockMutex(audio->mutex);
  sf_count_t frames_read = sf_readf_float(audio->file, buf, frames);
  size_t bytes_read = frames_read * audio->frameSize;
  SDL_PutAudioStreamData(stream, buf, bytes_read);
  SDL_UnlockMutex(audio->mutex);

  SDL_LockMutex(audio->mutex);
  audio_change_spec(&buf, &bytes_read, &audio->spec, &dst_spec);
  SDL_UnlockMutex(audio->mutex);

  buf_len = bytes_read / sizeof(float);

  for (int i = 0; i < buf_len; ++i)
  {
    ring_buffer[ring_write_pos] = buf[i];
    ring_write_pos = (ring_write_pos + 1) % FFT_SIZE;

    total_written++;
  }

  free(buf);

  audio->pos += frames_read;

  if (audio->pos >= (audio->frames - (5 * audio->spec.freq))) {
    for (int i = 0; i < BAR_NUMBER; i++) {
      barMag[i] = 0;
    }
    isFinished = true;
  }
}

float* create_hann_window(size_t n)
{
  float* win = malloc(n * sizeof(float));
  if (!win) return NULL;

  const float two_pi = 2.0f * M_PI;
  const float denom = ( float )(n - 1);
  for (size_t i = 0; i < n; i++) {
    win[i] = 0.5f * (1.0f - cosf(two_pi * i / denom));
  }

  return win;
}

float aweight(float f) {
  float f2 = f * f;
  float ra = (12200 * 12200 * f2 * f2) / ((f2 + 20.6 * 20.6) * (f2 + 12200 * 12200) * sqrtf((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)));
  return 20 * log10f(ra) + 2.f; // +2dB offset
}

void fft_audio(float* bar_mag, float* audio_buf, size_t fft_size)
{
  const int half_fft_size = fft_size / 2;
  const float window_gain = 2.0f / 3.0f;

  static kiss_fftr_cfg cfg = NULL;
  static float* hann = NULL;
  static kiss_fft_cpx* fout = NULL;
  static float* fft_mag = NULL;

  float bar_sum[BAR_NUMBER] = { 0 };
  float count[BAR_NUMBER] = { 0 };

  if (!cfg)
    cfg = kiss_fftr_alloc(fft_size, 0, NULL, NULL);

  if (!fout)
    fout = ( kiss_fft_cpx* )malloc((half_fft_size + 1) * sizeof(kiss_fft_cpx));

  if (!fft_mag)
    fft_mag = ( float* )malloc((half_fft_size + 1) * sizeof(float));

  if (!hann)
    hann = create_hann_window(fft_size);

  for (size_t i = 0; i < fft_size; ++i)
    audio_buf[i] *= hann[i];

  kiss_fftr(cfg, audio_buf, fout);

  for (size_t k = 0; k <= half_fft_size; k++)
  {
    float r = fout[k].r;
    float i = fout[k].i;
    float amp = sqrtf(r * r + i * i);
    float scale = (k == 0 || k == half_fft_size) ? 1.0f : 2.0f;

    fft_mag[k] = scale * amp * window_gain;
  }

  float bin_freq = ( float )audio.spec.freq * speed / fft_size;
  float log_min = log2f(MIN_FREQ);
  float log_max = log2f(MAX_FREQ);

  float band_edge[BAR_NUMBER + 1];
  for (int i = 0; i <= BAR_NUMBER; ++i)
  {
    float t = ( float )i / BAR_NUMBER;
    float log_f = log_min + t * (log_max - log_min);
    band_edge[i] = powf(2.0f, log_f);
  }

  for (int bar = 0; bar < BAR_NUMBER; ++bar)
  {
    float f1 = band_edge[bar];
    float f2 = band_edge[bar + 1];

    int b1 = ( int )(f1 / bin_freq);
    int b2 = ( int )(f2 / bin_freq);

    if (b1 < 0) b1 = 0;
    if (b2 >= half_fft_size) b2 = half_fft_size - 1;
    if (b2 < b1) b2 = b1;

    float sum_sq = 0.f;
    int cnt = 0;

    for (int k = b1; k <= b2; ++k)
    {
      float v = fft_mag[k];
      sum_sq += v * v;
      cnt++;
    }

    float mag = (cnt > 0) ? sqrtf(sum_sq / cnt) : 0.f;
    mag = fmaxf(mag, 1e-10f);

    float center_freq = sqrtf(f1 * f2);

    float db = 20.f * log10f(mag / fft_size);
    db += aweight(center_freq);

    float b = 1.0f - fminf(center_freq / 200.0f, 1.0f);
    b = powf(b, 1.5);
    db += 26.0f * b;

    if (center_freq > 2000.f)
    {
      float t = (center_freq - 2000.f) / 4000.f;
      t = powf(t, 1.5f);
      db += 26.0f * t;
    }

    float norm = (db - MIN_DB) / (MAX_DB - MIN_DB);

    const float pivot = 0.18f;
    const float contrast = 2.1f;

    norm = pivot + (norm - pivot) * contrast;

    norm = fmaxf(0.f, fminf(1.f, norm));

    bar_sum[bar] += norm;
    count[bar]++;
  }

  for (int bar = 0; bar < BAR_NUMBER; ++bar)
  {
    if (count[bar] > 0)
      bar_mag[bar] = bar_sum[bar] / count[bar];
    else
      bar_mag[bar] = 0.0f;
  }
}

void seek_audio(AudioData* audio, SDL_AudioStream* stream, double seconds)
{
  if (!stream || !audio->file) {
    return;
  }

  sf_count_t frames = ( sf_count_t )(seconds * ( double )audio->spec.freq);

  sf_count_t new_pos = audio->pos + frames;
  SDL_LockMutex(audio->mutex);
  sf_count_t file_len = sf_seek(audio->file, 0, SF_SEEK_END);
  SDL_UnlockMutex(audio->mutex);
  if (new_pos > file_len) {
    new_pos = file_len;
  }
  else if (new_pos < 0) {
    new_pos = 0;
  }

  SDL_LockMutex(audio->mutex);
  sf_count_t result = sf_seek(audio->file, new_pos, SF_SEEK_SET);
  SDL_UnlockMutex(audio->mutex);
  if (result == -1) {
    fprintf(stderr, "Failed to seek backward\n");
    return;
  }
  audio->pos = result;

  SDL_ClearAudioStream(stream);
}

void set_audio_speed(SDL_AudioStream* stream, float* speed)
{

  if (*speed > MAX_SPEED)
    *speed = MAX_SPEED;
  if (*speed < MIN_SPEED)
    *speed = MIN_SPEED;

  SDL_SetAudioStreamFrequencyRatio(stream, *speed);
}

void should_audio_loop(AudioData* audio, bool loop)
{
  if (!loop)
    return;

  if (audio->pos == audio->frames) {
    double backsec = ( double )audio->frames / ( double )audio->spec.freq;
    seek_audio(audio, audio->stream, -backsec);
  }
}

SDL_FRect* create_bars(SDL_Window* window, size_t bar_num)
{
  int width = 0, height = 0;

  if (!SDL_GetWindowSize(window, &width, &height))
  {
    printf("ERROR: %s", SDL_GetError());
    exit(EXIT_FAILURE);
  }

  SDL_FRect* rects = malloc(sizeof(SDL_FRect) * bar_num);
  if (!rects)
    return NULL;

  double k = BAR_SEPARATION;

  double ideal_bar = ( double )width / (bar_num + (bar_num - 1) * k);
  int bar_width = ( int )ideal_bar;

  int gap_count = bar_num - 1;
  int remaining = width - bar_width * bar_num;

  int separation = gap_count > 0 ? remaining / gap_count : 0;
  int extra = gap_count > 0 ? remaining % gap_count : 0;

  int x = 0;

  for (size_t i = 0; i < bar_num; i++)
  {
    rects[i].x = x;
    rects[i].y = height;
    rects[i].w = bar_width;
    rects[i].h = 0;

    x += bar_width;

    if (i < gap_count)
    {
      x += separation;

      if (extra > 0)
      {
        x += 1;
        extra--;
      }
    }
  }

  return rects;
}

bool draw_bars(SDL_FRect* rects, size_t bar_num, double dt)
{
    int width, height;
    SDL_GetWindowSize(window, &width, &height);

    for (size_t i = 0; i < bar_num; ++i)
    {
        float target = barMag[i] * height;
        if (!isPlaying) target = 0;

        float scale = isMirrored ? 0.5f : 0.75f;
        rects[i].h += (scale * target - rects[i].h) * ALPHA * (float)dt;

        if (rects[i].h < BAR_MIN_HEIGHT) {
            rects[i].h = BAR_MIN_HEIGHT;
        }
    }

    SDL_SetRenderDrawColor(renderer, barColor.r, barColor.g, barColor.b, barColor.a);

    if (isMirrored) {
        float centerY = (float)height / 2.0f;

        for (size_t i = 0; i < bar_num; i++) {
            rects[i].y = centerY - rects[i].h;
        }
        SDL_RenderFillRects(renderer, rects, (int)bar_num);

        for (size_t i = 0; i < bar_num; i++) {
            rects[i].y = centerY;
        }
        SDL_RenderFillRects(renderer, rects, (int)bar_num);

    } else {
        for (size_t i = 0; i < bar_num; i++) {
            rects[i].y = (float)height - rects[i].h;
        }
        SDL_RenderFillRects(renderer, rects, (int)bar_num);
    }

    return true;
}

const char* basename(const char* path)
{
  const char* p = strrchr(path, '/');
  if (!p)
    p = strrchr(path, '\\');
  return p ? p + 1 : path;
}

void cleanup(void)
{
  if (window)
    SDL_DestroyWindow(window);
  if (renderer)
    SDL_DestroyRenderer(renderer);

  if (audio.stream)
  {
    SDL_FlushAudioStream(audio.stream);
    SDL_DestroyAudioStream(audio.stream);
  }

  if (audio.file)
    sf_close(audio.file);

  if (audio.mutex)
    SDL_DestroyMutex(audio.mutex);

  if (audio.deviceId)
    SDL_CloseAudioDevice(audio.deviceId);

  SDL_Quit();
  TTF_Quit();
}
