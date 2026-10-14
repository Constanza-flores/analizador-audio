// ============================================================================
//  Analizador de audio en tiempo real  (tiempo + espectro + espectrograma)
//  Dependencia unica: SDL2  (captura de microfono + dibujo)
//
//  Compilar (Linux / macOS / MSYS2):
//    g++ -O2 -std=c++17 analizador_audio.cpp -o analizador $(sdl2-config --cflags --libs)
// 
//  # Windows (MSYS2 UCRT64)
// pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2
// g++ -O2 -std=c++17 analizador_audio.cpp -o analizador.exe $(sdl2-config --cflags --libs)
//
//  Uso:
//    ./analizador          -> microfono por defecto
//    ./analizador 2        -> usa el dispositivo de captura numero 2 (se listan al iniciar)
//  Teclas:  ESPACIO = pausa   |   J = juego de afinacion   |   S = captura PNG
//           ESC = volver del juego / salir
// ============================================================================
#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <random>
#include <string>
#include <vector>

// ------------------------------ Parametros ----------------------------------
static const int   FFT_N     = 4096;     // tamano de la FFT (potencia de 2)
static const int   TIME_N    = 4096;     // muestras mostradas en el osciloscopio
static const int   N_BANDS   = 110;      // barras del espectro
static const float F_MIN     = 30.0f;    // Hz
static const float F_MAX     = 16000.0f; // Hz
static const float DB_FLOOR  = -100.0f;
static const float DB_CEIL   = -20.0f;
static const int   SG_W      = 640;      // columnas de historia del espectrograma
static const int   SG_H      = 256;      // filas (frecuencia, escala log)
static const float PITCH_MIN = 70.0f;    // Hz, nota mas grave que se detecta
static const float PITCH_MAX = 2500.0f;  // Hz, nota mas aguda que se detecta
static const float GAME_SECS = 45.0f;    // duracion de una partida
static const float HOLD_SECS = 0.6f;     // tiempo que hay que sostener la nota
static const float GAME_TOL  = 0.4f;     // tolerancia en semitonos

// ------------------------------ Audio ---------------------------------------
struct Shared {
    std::vector<float> ring = std::vector<float>(1 << 16, 0.f);
    uint64_t written = 0;
    std::mutex m;
    int channels = 1;
};

static void audioCallback(void* ud, Uint8* stream, int len) {
    Shared* s = static_cast<Shared*>(ud);
    const float* f = reinterpret_cast<const float*>(stream);
    int frames = len / (int)sizeof(float) / s->channels;
    std::lock_guard<std::mutex> lk(s->m);
    const size_t mask = s->ring.size() - 1;
    for (int i = 0; i < frames; ++i)
        s->ring[(s->written++) & mask] = f[i * s->channels];
}

// ------------------------------ FFT -----------------------------------------
using cpx = std::complex<float>;
static void fft(std::vector<cpx>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / (float)len;
        cpx wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cpx w(1.f, 0.f);
            for (size_t k = 0; k < len / 2; ++k) {
                cpx u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// ------------------------------ Colores -------------------------------------
struct RGB { Uint8 r, g, b; };
static RGB colormap(float t) {              // estilo "inferno"
    static const float st[8][3] = {
        {0, 0, 4}, {40, 11, 84}, {101, 21, 110}, {159, 42, 99},
        {212, 72, 66}, {245, 125, 21}, {250, 193, 39}, {252, 255, 164}};
    t = std::clamp(t, 0.f, 1.f) * 7.f;
    int i = std::min((int)t, 6);
    float f = t - i;
    return {(Uint8)(st[i][0] + (st[i + 1][0] - st[i][0]) * f),
            (Uint8)(st[i][1] + (st[i + 1][1] - st[i][1]) * f),
            (Uint8)(st[i][2] + (st[i + 1][2] - st[i][2]) * f)};
}

// ------------------------------ Fuente 3x5 ----------------------------------
struct Glyph { char c; const char* b; };
static const Glyph GLYPHS[] = {
    {'0', "111101101101111"}, {'1', "010110010010111"}, {'2', "111001111100111"},
    {'3', "111001111001111"}, {'4', "101101111001001"}, {'5', "111100111001111"},
    {'6', "111100111101111"}, {'7', "111001001001001"}, {'8', "111101111101111"},
    {'9', "111101111001111"}, {'H', "101101111101101"}, {'z', "111001010100111"},
    {'k', "101101110101101"}, {'T', "111010010010010"}, {'I', "111010010010111"},
    {'E', "111100111100111"}, {'M', "101111111101101"}, {'P', "111101111100100"},
    {'O', "111101101101111"}, {'S', "111100111001111"}, {'C', "111100100100111"},
    {'R', "110101110101101"}, {'G', "111100101101111"}, {'A', "010101111101101"},
    {'D', "110101101101110"}, {'B', "110101110101110"}, {'F', "111100110100100"},
    {'N', "110101101101101"}, {'U', "101101101101111"}, {'L', "100100100100111"},
    {'Z', "111001010100111"}, {'.', "000000000000010"}, {'-', "000000111000000"},
    {'J', "001001001101111"}, {'V', "101101101101010"}, {'Y', "101101010010010"},
    {'Q', "111101101111001"}, {'X', "101101010101101"}, {'W', "101101111111101"},
    {'K', "101101110101101"}, {'#', "010111010111010"}, {':', "000010000010000"},
    {'!', "010010010000010"}, {'+', "000010111010000"}, {'d', "001001111101111"},
};

static void drawText(SDL_Renderer* r, int x, int y, int scale, const std::string& s, RGB c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    for (char ch : s) {
        for (const Glyph& g : GLYPHS) {
            if (g.c != ch) continue;
            for (int i = 0; i < 15; ++i)
                if (g.b[i] == '1') {
                    SDL_Rect p{x + (i % 3) * scale, y + (i / 3) * scale, scale, scale};
                    SDL_RenderFillRect(r, &p);
                }
            break;
        }
        x += 4 * scale;
    }
}
static int textWidth(int scale, const std::string& s) { return (int)s.size() * 4 * scale - scale; }

static void fillRect(SDL_Renderer* r, SDL_Rect rc, RGB c, Uint8 a = 255) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, a);
    SDL_RenderFillRect(r, &rc);
}
static void panel(SDL_Renderer* r, SDL_Rect rc, const std::string& title, int sc) {
    fillRect(r, {rc.x - 2, rc.y - 2, rc.w + 4, rc.h + 4}, {38, 44, 66});
    fillRect(r, rc, {10, 12, 22});
    drawText(r, rc.x, rc.y - 6 - 5 * sc, sc, title, {150, 170, 230});
}

// ------------------------------ Notas ---------------------------------------
static const char* NOTE_NAMES[12] = {"DO", "DO#", "RE", "RE#", "MI", "FA",
                                     "FA#", "SOL", "SOL#", "LA", "LA#", "SI"};

// Frecuencia fundamental con el algoritmo YIN. Devuelve 0 si no hay un tono claro.
static float detectPitch(const std::vector<float>& x, float sr) {
    const int W = 2048;
    const int tauMax = std::min((int)(sr / PITCH_MIN), (int)x.size() - W - 1);
    const int tauMin = std::max(2, (int)(sr / PITCH_MAX));
    if (tauMax <= tauMin + 2) return 0.f;
    const float* s = x.data() + x.size() - W - tauMax;

    float energy = 0;
    for (int i = 0; i < W; ++i) energy += s[i] * s[i];
    if (energy / W < 1e-6f) return 0.f;      // silencio (menos de -60 dB)

    // diferencia normalizada por su media acumulada
    static std::vector<float> d;
    d.assign(tauMax + 1, 1.f);
    double run = 0;
    for (int tau = 1; tau <= tauMax; ++tau) {
        float acc = 0;
        for (int i = 0; i < W; ++i) {
            float df = s[i] - s[i + tau];
            acc += df * df;
        }
        run += acc;
        d[tau] = run > 0 ? (float)(acc * tau / run) : 1.f;
    }
    // primer minimo bajo el umbral
    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau) {
        if (d[tau] < 0.15f) {
            while (tau + 1 < tauMax && d[tau + 1] < d[tau]) ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0) return 0.f;
    // interpolacion parabolica para afinar el periodo
    float a = d[best - 1], b = d[best], c = d[best + 1];
    float den = a - 2 * b + c;
    float shift = den != 0 ? std::clamp(0.5f * (a - c) / den, -1.f, 1.f) : 0.f;
    return sr / (best + shift);
}

// ------------------------------ Captura PNG ---------------------------------
static void put32(std::vector<Uint8>& v, Uint32 x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back((Uint8)(x >> s));
}
static Uint32 crc32(const std::vector<Uint8>& v) {
    static Uint32 table[256];
    if (!table[1])
        for (Uint32 n = 0; n < 256; ++n) {
            Uint32 c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
    Uint32 c = 0xFFFFFFFFu;
    for (Uint8 b : v) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return ~c;
}
// chunk = tipo (4 letras) seguido de los datos
static void writeChunk(FILE* f, const std::vector<Uint8>& chunk) {
    std::vector<Uint8> head, tail;
    put32(head, (Uint32)chunk.size() - 4);
    put32(tail, crc32(chunk));
    std::fwrite(head.data(), 1, 4, f);
    std::fwrite(chunk.data(), 1, chunk.size(), f);
    std::fwrite(tail.data(), 1, 4, f);
}
// PNG sin compresion (bloques "stored"), para no depender de zlib
static bool savePNG(const std::string& path, const std::vector<Uint8>& rgb, int w, int h) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    static const Uint8 sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::fwrite(sig, 1, 8, f);

    std::vector<Uint8> ihdr = {'I', 'H', 'D', 'R'};
    put32(ihdr, (Uint32)w);
    put32(ihdr, (Uint32)h);
    for (Uint8 b : {8, 2, 0, 0, 0}) ihdr.push_back(b);   // 8 bits, RGB
    writeChunk(f, ihdr);

    std::vector<Uint8> raw;                              // filas con byte de filtro 0
    raw.reserve((size_t)h * (w * 3 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + (size_t)y * w * 3, rgb.begin() + (size_t)(y + 1) * w * 3);
    }
    std::vector<Uint8> idat = {'I', 'D', 'A', 'T', 0x78, 0x01};
    idat.reserve(raw.size() + raw.size() / 65535 * 5 + 32);
    Uint32 a = 1, b = 0;                                 // adler32
    for (size_t off = 0; off < raw.size(); off += 65535) {
        size_t n = std::min<size_t>(65535, raw.size() - off);
        idat.push_back(off + n == raw.size() ? 1 : 0);
        idat.push_back((Uint8)(n & 0xFF));
        idat.push_back((Uint8)(n >> 8));
        idat.push_back((Uint8)(~n & 0xFF));
        idat.push_back((Uint8)((~n >> 8) & 0xFF));
        idat.insert(idat.end(), raw.begin() + off, raw.begin() + off + n);
        for (size_t i = off; i < off + n; ++i) {
            a = (a + raw[i]) % 65521;
            b = (b + a) % 65521;
        }
    }
    put32(idat, (b << 16) | a);
    writeChunk(f, idat);
    writeChunk(f, {'I', 'E', 'N', 'D'});
    return std::fclose(f) == 0;
}

// Guarda lo que hay dibujado en capturas/captura_FECHA_HORA.png. Devuelve la ruta o "".
static std::string saveCapture(SDL_Renderer* r, int w, int h) {
    std::vector<Uint8> px((size_t)w * h * 3);
    if (SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGB24, px.data(), w * 3) != 0) return "";
    std::error_code ec;
    std::filesystem::create_directories("capturas", ec);
    char name[64];
    std::time_t t = std::time(nullptr);
    std::strftime(name, sizeof name, "capturas/captura_%Y%m%d_%H%M%S.png", std::localtime(&t));
    return savePNG(name, px, w, h) ? name : "";
}

// ------------------------------ Record --------------------------------------
static int loadRecord() {
    int r = 0;
    if (FILE* f = std::fopen("record.txt", "r")) {
        if (std::fscanf(f, "%d", &r) != 1) r = 0;
        std::fclose(f);
    }
    return std::max(r, 0);
}
static void saveRecord(int r) {
    if (FILE* f = std::fopen("record.txt", "w")) {
        std::fprintf(f, "%d\n", r);
        std::fclose(f);
    }
}

// ------------------------------ Main ----------------------------------------
int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    int nDev = SDL_GetNumAudioDevices(1);
    std::printf("Dispositivos de captura:\n");
    for (int i = 0; i < nDev; ++i) std::printf("  [%d] %s\n", i, SDL_GetAudioDeviceName(i, 1));
    const char* devName = nullptr;
    if (argc > 1) {
        int idx = std::atoi(argv[1]);
        if (idx >= 0 && idx < nDev) devName = SDL_GetAudioDeviceName(idx, 1);
    }

    Shared shared;
    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = audioCallback;
    want.userdata = &shared;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(devName, 1, &want, &have,
                                                SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
                                                SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    if (!dev) {
        std::fprintf(stderr, "No se pudo abrir el microfono: %s\n", SDL_GetError());
        return 1;
    }
    if (have.format != AUDIO_F32SYS) {
        std::fprintf(stderr, "Formato de audio no soportado\n");
        return 1;
    }
    shared.channels = have.channels;
    const float sr = (float)have.freq;
    std::printf("Capturando a %d Hz, %d canal(es)\n", have.freq, have.channels);
    SDL_PauseAudioDevice(dev, 0);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Window* win = SDL_CreateWindow("Analizador de audio", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, 1100, 800,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    SDL_Texture* sgTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, SG_W, SG_H);
    std::vector<Uint32> blank(SG_W * SG_H, 0xFF000004u);
    SDL_UpdateTexture(sgTex, nullptr, blank.data(), SG_W * 4);
    int sgX = 0;

    // Ventana Hann y buffers
    std::vector<float> win_(FFT_N);
    float wsum = 0;
    for (int i = 0; i < FFT_N; ++i) {
        win_[i] = 0.5f - 0.5f * std::cos(2.f * (float)M_PI * i / (FFT_N - 1));
        wsum += win_[i];
    }
    std::vector<cpx> buf(FFT_N);
    std::vector<float> dbs(FFT_N / 2 + 1, DB_FLOOR);
    std::vector<float> samples(std::max(FFT_N, TIME_N), 0.f);
    std::vector<float> bars(N_BANDS, DB_FLOOR), peaks(N_BANDS, DB_FLOOR);
    float peakFreq = 0, peakDb = DB_FLOOR;

    auto dbAt = [&](float bin) {
        bin = std::clamp(bin, 0.f, (float)(FFT_N / 2));
        int i = (int)bin;
        int j = std::min(i + 1, FFT_N / 2);
        float f = bin - i;
        return dbs[i] * (1 - f) + dbs[j] * f;
    };
    auto freqOf = [&](float t) { return F_MIN * std::pow(F_MAX / F_MIN, t); };
    auto norm = [&](float db) { return std::clamp((db - DB_FLOOR) / (DB_CEIL - DB_FLOOR), 0.f, 1.f); };

    // Nota detectada
    float midi = 69.f, noteHz = 0.f;   // ultima nota valida
    float noteAge = 1e9f;              // segundos desde que se detecto

    // Juego de afinacion: cantar o silbar la nota pedida (en cualquier octava)
    enum class Game { Off, Playing, Over };
    Game game = Game::Off;
    std::mt19937 rng((unsigned)std::time(nullptr));
    int score = 0, record = loadRecord(), targetPc = -1;
    bool newRecord = false;
    float timeLeft = 0, hold = 0, targetAge = 0, flash = 0, marker = 0;
    auto newTarget = [&]() {
        static const int naturals[7] = {0, 2, 4, 5, 7, 9, 11};
        int p;
        do p = naturals[rng() % 7]; while (p == targetPc);
        targetPc = p;
        hold = 0;
        targetAge = 0;
    };

    bool wantShot = false;
    float toast = 0;                   // segundos que queda visible el aviso de captura
    Uint32 lastTicks = SDL_GetTicks();

    bool paused = false, running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                SDL_Keycode key = e.key.keysym.sym;
                if (key == SDLK_ESCAPE) {
                    if (game != Game::Off) game = Game::Off;
                    else running = false;
                }
                if (key == SDLK_SPACE) paused = !paused;
                if (key == SDLK_s) wantShot = true;
                if (key == SDLK_j) {
                    game = Game::Playing;
                    score = 0;
                    timeLeft = GAME_SECS;
                    newRecord = false;
                    flash = 0;
                    newTarget();
                }
            }
        }
        Uint32 ticks = SDL_GetTicks();
        const float dt = std::min((ticks - lastTicks) / 1000.f, 0.1f);
        lastTicks = ticks;
        toast = std::max(0.f, toast - dt);

        // ---------- Procesamiento ----------
        if (!paused) {
            {
                std::lock_guard<std::mutex> lk(shared.m);
                const size_t mask = shared.ring.size() - 1;
                int need = (int)samples.size();
                for (int i = 0; i < need; ++i)
                    samples[i] = shared.ring[(shared.written - need + i) & mask];
            }
            int off = (int)samples.size() - FFT_N;
            for (int i = 0; i < FFT_N; ++i) buf[i] = cpx(samples[off + i] * win_[i], 0.f);
            fft(buf);
            peakDb = DB_FLOOR; peakFreq = 0;
            for (int k = 0; k <= FFT_N / 2; ++k) {
                float mag = std::abs(buf[k]) * 2.f / wsum;
                dbs[k] = std::max(20.f * std::log10(mag + 1e-9f), DB_FLOOR);
                float fk = k * sr / FFT_N;
                if (fk > 50.f && dbs[k] > peakDb) { peakDb = dbs[k]; peakFreq = fk; }
            }
            // barras
            for (int i = 0; i < N_BANDS; ++i) {
                float f0 = freqOf((float)i / N_BANDS), f1 = freqOf((float)(i + 1) / N_BANDS);
                int lo = (int)(f0 * FFT_N / sr), hi = (int)(f1 * FFT_N / sr);
                hi = std::min(hi, FFT_N / 2);
                float v = dbAt(0.5f * (f0 + f1) * FFT_N / sr);
                for (int k = lo; k <= hi; ++k) v = std::max(v, dbs[k]);
                bars[i] = v > bars[i] ? v : bars[i] * 0.85f + v * 0.15f;
                peaks[i] = std::max(v, peaks[i] - 0.5f);
            }
            // columna del espectrograma
            std::vector<Uint32> col(SG_H);
            for (int y = 0; y < SG_H; ++y) {
                float t = 1.f - (float)y / (SG_H - 1);
                float f = freqOf(t);
                RGB c = colormap(norm(dbAt(f * FFT_N / sr)));
                col[y] = 0xFF000000u | (c.r << 16) | (c.g << 8) | c.b;
            }
            SDL_Rect cr{sgX, 0, 1, SG_H};
            SDL_UpdateTexture(sgTex, &cr, col.data(), 4);
            sgX = (sgX + 1) % SG_W;

            // nota
            float hz = detectPitch(samples, sr);
            if (hz > 0) {
                noteHz = hz;
                midi = 69.f + 12.f * std::log2(hz / 440.f);
                noteAge = 0;
            } else {
                noteAge += dt;
            }
        }
        const bool voiced = noteAge < 0.15f;
        // distancia a la nota pedida en semitonos, sin importar la octava: [-6, 6)
        float diff = std::fmod(midi - (float)targetPc, 12.f);
        if (diff < 0) diff += 12.f;
        if (diff >= 6.f) diff -= 12.f;

        // ---------- Juego ----------
        if (!paused && game == Game::Playing) {
            timeLeft -= dt;
            targetAge += dt;
            flash = std::max(0.f, flash - dt);
            if (voiced) marker = std::fabs(diff - marker) > 6.f ? diff : marker + (diff - marker) * 0.4f;
            if (voiced && std::fabs(diff) <= GAME_TOL) hold += dt;
            else hold = std::max(0.f, hold - 2.f * dt);
            if (hold >= HOLD_SECS) {             // nota lograda: mas puntos si fue rapido
                score += 100 + std::max(0, (int)(100.f - 20.f * targetAge));
                flash = 0.4f;
                newTarget();
            }
            if (timeLeft <= 0) {
                game = Game::Over;
                if (score > record) {
                    record = score;
                    newRecord = true;
                    saveRecord(record);
                }
            }
        }

        // ---------- Dibujo ----------
        int W, H;
        SDL_GetRendererOutputSize(ren, &W, &H);
        const int sc = std::max(2, H / 260);        // escala del texto
        const int mg = 28 * sc / 2;                 // margen
        fillRect(ren, {0, 0, W, H}, {18, 20, 34});

        // cabecera
        drawText(ren, mg, mg / 2, sc * 2, "ANALIZADOR DE AUDIO", {235, 240, 255});
        const RGB cDim{150, 170, 230}, cWhite{235, 240, 255}, cGold{250, 193, 39};
        const RGB cGreen{80, 220, 140}, cRed{240, 90, 90};
        {   // nota detectada: nombre en grande y frecuencia al lado
            std::string name, hzTxt = "NOTA --";
            if (noteAge < 0.3f) {
                int n = (int)std::lround(midi);
                name = NOTE_NAMES[((n % 12) + 12) % 12] + std::to_string(n / 12 - 1);
                hzTxt = std::to_string((int)std::lround(noteHz)) + " Hz";
            }
            int xHz = W - mg - textWidth(sc * 2, hzTxt);
            drawText(ren, xHz, mg / 2 + 5 * sc, sc * 2, hzTxt, cDim);
            drawText(ren, xHz - 4 * sc - textWidth(sc * 3, name), mg / 2, sc * 3, name, cGold);
        }

        const bool inGame = game != Game::Off;
        int top = mg / 2 + 14 * sc + 10 * sc;
        int avail = H - top - mg;
        int gap = 16 * sc;
        int hTime = (int)((avail - 2 * gap) * (inGame ? 0.42f : 0.22f));
        int hSpec = (int)((avail - 2 * gap) * (inGame ? 0.24f : 0.34f));
        int hSg = avail - 2 * gap - hTime - hSpec;
        int pw = W - 2 * mg;

        // --- Tiempo (o el juego, que ocupa su lugar) ---
        SDL_Rect rT{mg, top, pw, hTime};
        if (!inGame) {
            panel(ren, rT, "TIEMPO", sc);
            fillRect(ren, {rT.x, rT.y + rT.h / 2, rT.w, 1}, {45, 52, 80});
            int base = (int)samples.size() - TIME_N;
            SDL_SetRenderDrawColor(ren, 80, 220, 190, 255);
            for (int x = 0; x < rT.w; ++x) {
                int a = base + (int)((int64_t)x * TIME_N / rT.w);
                int b = base + (int)((int64_t)(x + 1) * TIME_N / rT.w);
                float mn = 1e9f, mx = -1e9f;
                for (int i = a; i <= std::min(b, (int)samples.size() - 1); ++i) {
                    mn = std::min(mn, samples[i]); mx = std::max(mx, samples[i]);
                }
                mn = std::clamp(mn, -1.f, 1.f); mx = std::clamp(mx, -1.f, 1.f);
                int y1 = rT.y + (int)((1.f - mx) * 0.5f * rT.h);
                int y2 = rT.y + (int)((1.f - mn) * 0.5f * rT.h);
                SDL_RenderDrawLine(ren, rT.x + x, y1, rT.x + x, std::max(y2, y1 + 1));
            }
        } else if (game == Game::Playing) {
            panel(ren, rT, "JUEGO DE AFINACION", sc);
            if (flash > 0) fillRect(ren, rT, cGreen, (Uint8)(140 * flash / 0.4f));
            const int pad = 4 * sc;

            // izquierda: nota pedida
            int cw = rT.w * 22 / 100;
            int bs = std::max(sc, std::min((rT.h - 3 * pad - 5 * sc) / 5, cw / 12));
            drawText(ren, rT.x + pad, rT.y + pad, sc, "CANTA O SILBA", cDim);
            drawText(ren, rT.x + pad, rT.y + 2 * pad + 5 * sc, bs, NOTE_NAMES[targetPc], cGold);

            // derecha: marcador
            int rx = rT.x + rT.w - rT.w * 18 / 100;
            auto stat = [&](int row, const char* label, int value, RGB c) {
                int y = rT.y + pad + row * (rT.h - pad) / 3;
                drawText(ren, rx, y, sc, label, cDim);
                drawText(ren, rx, y + 7 * sc, sc * 2, std::to_string(value), c);
            };
            stat(0, "PUNTOS", score, cWhite);
            stat(1, "TIEMPO", (int)std::ceil(std::max(timeLeft, 0.f)), timeLeft < 10 ? cRed : cWhite);
            stat(2, "RECORD", record, cGold);

            // centro: que tan lejos esta la voz de la nota (cada marca es un semitono)
            const bool onNote = voiced && std::fabs(diff) <= GAME_TOL;
            int gx = rT.x + cw + pad, gw = rx - 2 * pad - gx;
            int gy = rT.y + rT.h / 2 - 3 * sc, gh = 6 * sc;
            fillRect(ren, {gx, gy, gw, gh}, {30, 36, 58});
            int zw = (int)(gw * GAME_TOL / 12.f);
            fillRect(ren, {gx + gw / 2 - zw, gy, 2 * zw, gh}, cGreen, onNote ? 255 : 110);
            for (int k = 0; k <= 12; ++k)
                fillRect(ren, {gx + (int)((int64_t)(gw - 1) * k / 12), gy + gh, 1, sc * 2}, cDim);
            drawText(ren, gx, gy + gh + 4 * sc, sc, "GRAVE", cDim);
            drawText(ren, gx + gw - textWidth(sc, "AGUDO"), gy + gh + 4 * sc, sc, "AGUDO", cDim);
            if (noteAge < 0.3f) {
                int mx = gx + (int)((marker + 6.f) / 12.f * gw);
                fillRect(ren, {mx - sc, gy - 2 * sc, 2 * sc, gh + 4 * sc}, onNote ? cGreen : cWhite);
            }
            std::string hint = "CANTA O SILBA";
            RGB hc = cDim;
            if (noteAge < 0.3f) {
                hint = onNote ? "MANTEN!" : diff < 0 ? "SUBE" : "BAJA";
                hc = onNote ? cGreen : cWhite;
            }
            drawText(ren, gx + (gw - textWidth(sc * 2, hint)) / 2, gy - 14 * sc, sc * 2, hint, hc);
            // barra de "sostener la nota"
            int hw = gw / 2, hx = gx + gw / 4, hy = gy + gh + 12 * sc;
            fillRect(ren, {hx, hy, hw, 2 * sc}, {30, 36, 58});
            fillRect(ren, {hx, hy, (int)(hw * std::min(hold / HOLD_SECS, 1.f)), 2 * sc}, cGreen);
        } else {
            panel(ren, rT, "JUEGO DE AFINACION", sc);
            auto centered = [&](float fy, int s, const std::string& t, RGB c) {
                drawText(ren, rT.x + (rT.w - textWidth(s, t)) / 2, rT.y + (int)(rT.h * fy), s, t, c);
            };
            centered(0.10f, sc * 2, "FIN DEL JUEGO", cDim);
            centered(0.30f, sc * 4, std::to_string(score) + " PUNTOS", cWhite);
            if (newRecord) centered(0.64f, sc * 2, "NUEVO RECORD!", cGold);
            else centered(0.64f, sc * 2, "RECORD " + std::to_string(record), cDim);
            centered(0.86f, sc, "J: JUGAR DE NUEVO", cDim);
        }

        // --- Espectro ---
        SDL_Rect rS{mg, rT.y + rT.h + gap, pw, hSpec};
        panel(ren, rS, "ESPECTRO", sc);
        for (int d = 1; d <= 3; ++d) {   // lineas de dB
            int y = rS.y + rS.h - (int)(rS.h * d * 0.25f);
            fillRect(ren, {rS.x, y, rS.w, 1}, {35, 42, 68});
        }
        {
            float bw = (float)rS.w / N_BANDS;
            for (int i = 0; i < N_BANDS; ++i) {
                float n = norm(bars[i]);
                int h = (int)(n * rS.h);
                int x = rS.x + (int)(i * bw);
                int w = std::max(1, (int)(bw) - 1);
                RGB c = colormap(0.15f + 0.85f * n);
                fillRect(ren, {x, rS.y + rS.h - h, w, h}, c);
                int py = rS.y + rS.h - (int)(norm(peaks[i]) * rS.h);
                fillRect(ren, {x, py, w, std::max(1, sc / 2)}, {240, 240, 255});
            }
        }
        // etiquetas de frecuencia (compartidas)
        auto freqLabels = [&](SDL_Rect r) {
            const std::pair<float, const char*> ticks[] = {
                {50, "50"}, {100, "100"}, {500, "500"}, {1000, "1k"}, {5000, "5k"}, {10000, "10k"}};
            for (auto& t : ticks) {
                float x = std::log(t.first / F_MIN) / std::log(F_MAX / F_MIN);
                int px = r.x + (int)(x * r.w);
                fillRect(ren, {px, r.y + r.h, 1, sc * 2}, {150, 170, 230});
                drawText(ren, px - textWidth(sc, t.second) / 2, r.y + r.h + sc * 3, sc, t.second, {150, 170, 230});
            }
            drawText(ren, r.x + r.w - textWidth(sc, "Hz"), r.y - 6 - 5 * sc, sc, "Hz", {150, 170, 230});
        };
        freqLabels(rS);
        drawText(ren, rS.x + 4, rS.y + 3, sc, "PICO " + std::to_string((int)peakFreq) + " Hz", {120, 130, 170});
        drawText(ren, rS.x + rS.w - textWidth(sc, "-20 dB") - 4, rS.y + 3, sc, "-20 dB", {120, 130, 170});
        drawText(ren, rS.x + rS.w - textWidth(sc, "-100 dB") - 4, rS.y + rS.h - 5 * sc - 3, sc, "-100 dB", {120, 130, 170});

        // --- Espectrograma (frecuencia en vertical, tiempo hacia la derecha) ---
        SDL_Rect rG{mg, rS.y + rS.h + gap + 8 * sc, pw, hSg - 8 * sc};
        panel(ren, rG, "ESPECTROGRAMA", sc);
        {
            int leftCols = SG_W - sgX;
            int wL = (int)((int64_t)rG.w * leftCols / SG_W);
            SDL_Rect sL{sgX, 0, leftCols, SG_H}, dL{rG.x, rG.y, wL, rG.h};
            SDL_RenderCopy(ren, sgTex, &sL, &dL);
            if (sgX > 0) {
                SDL_Rect sR{0, 0, sgX, SG_H}, dR{rG.x + wL, rG.y, rG.w - wL, rG.h};
                SDL_RenderCopy(ren, sgTex, &sR, &dR);
            }
            // marcador de "ahora"
            int nowX = rG.x + wL;
            fillRect(ren, {nowX, rG.y, 1, rG.h}, {255, 255, 255}, 90);
        }
        // etiquetas de frecuencia en el eje vertical del espectrograma
        {
            const std::pair<float, const char*> ticks[] = {{100, "100"}, {1000, "1k"}, {10000, "10k"}};
            for (auto& t : ticks) {
                float y = std::log(t.first / F_MIN) / std::log(F_MAX / F_MIN);
                int py = rG.y + rG.h - (int)(y * rG.h);
                fillRect(ren, {rG.x, py, sc * 2, 1}, {255, 255, 255}, 180);
                drawText(ren, rG.x + sc * 3, py - sc * 2, sc, t.second, {255, 255, 255});
            }
        }

        // pie: teclas, aviso de captura y pausa
        {
            int fy = H - mg + (mg - 5 * sc) / 2;
            if (toast > 0)
                drawText(ren, mg, fy, sc, "CAPTURA GUARDADA EN LA CARPETA CAPTURAS", cGreen);
            else
                drawText(ren, mg, fy, sc,
                         inGame ? "J: REINICIAR   S: CAPTURA   ESPACIO: PAUSA   ESC: VOLVER"
                                : "J: JUEGO   S: CAPTURA   ESPACIO: PAUSA   ESC: SALIR", cDim);
            if (paused)
                drawText(ren, W - mg - textWidth(sc * 2, "PAUSA"), H - mg + 2 * sc, sc * 2, "PAUSA", cRed);
        }

        if (wantShot) {
            wantShot = false;
            std::string path = saveCapture(ren, W, H);
            if (!path.empty()) {
                toast = 2.5f;
                std::printf("Captura guardada: %s\n", path.c_str());
            } else {
                std::fprintf(stderr, "No se pudo guardar la captura\n");
            }
            std::fflush(stdout);
        }

        SDL_RenderPresent(ren);
    }

    SDL_CloseAudioDevice(dev);
    SDL_DestroyTexture(sgTex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}