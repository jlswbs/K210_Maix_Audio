// Spectral drop grains FFT hardware //

#include "timer.h"
#include "fft.h"

#define FFT_N           512
#define SAMPLE_RATE     44100
#define TIMER_RATE      1000000000/SAMPLE_RATE
#define PWM_RATE        1000000
#define AUDIO_PIN_L     6
#define AUDIO_PIN_R     8

static int16_t audio_out[FFT_N];
volatile int sample_ptr = 0;

fft_data_t fft_out_data[FFT_N] __attribute__((aligned(64)));

typedef struct {
    int center;
    int width;
    int amp;
    int duration;
    int duration_left;
    float env;
    float env_step;
    uint8_t fx;
} grain_t;

#define MAX_GRAINS 128
grain_t grains[MAX_GRAINS];
int active_grains = 0;

void spawn_grain() {

    if(active_grains >= MAX_GRAINS) return;

    grain_t *g = &grains[active_grains++];

    g->center = rand() % (FFT_N/2 - 20) + 10;
    g->width = 1 + (rand() % 6);
    g->amp = 6000 + (rand() % 12000);

    g->duration = 20 + (rand() % 1000);
    g->duration_left = g->duration;

    g->env = 0;
    g->env_step = 2.0f / g->duration;

    g->fx = rand() & 3;

}

void remove_dead_grains(){

    int w = 0;

    for(int i=0;i<active_grains;i++) {

        if(grains[i].duration_left > 0 && grains[i].env > 0.01f)
        {
            if(i != w)
                grains[w] = grains[i];
            w++;
        }
    }

    active_grains = w;

}

void update_envelope(grain_t *g) {

    if(g->duration_left > g->duration/2)
        g->env += g->env_step;
    else
        g->env -= g->env_step;

    if(g->env < 0) g->env = 0;
    if(g->env > 1) g->env = 1;

    g->duration_left--;

}

void build_spectrum(fft_data_t *spec) {

    for(int i=0;i<FFT_N/2;i++) {
        spec[i].R1 = 0;
        spec[i].I1 = 0;
        spec[i].R2 = 0;
        spec[i].I2 = 0;
    }

    for(int g=0; g<active_grains; g++)
    {
        grain_t *gr = &grains[g];

        float env = gr->env;
        int center = gr->center;
        int width = gr->width;

        for(int w=-width; w<=width; w++)
        {
            int bin = center + w;
            if(bin < 0 || bin >= FFT_N/2) continue;

            float shape = 1.0f - (abs(w)/(float)width)*0.5f;
            int val = gr->amp * env * shape;

            if(bin & 1)
                spec[bin/2].R2 += val;
            else
                spec[bin/2].R1 += val;
        }

        update_envelope(gr);
    }

    remove_dead_grains();

}

int audio_callback(void *ctx) {

    int16_t s = audio_out[sample_ptr];
    float duty = (float)(s + 32768) / 65536.0f;

    pwm_set_frequency(PWM_DEVICE_1, PWM_CHANNEL_0, PWM_RATE, duty);
    pwm_set_frequency(PWM_DEVICE_1, PWM_CHANNEL_1, PWM_RATE, duty);

    sample_ptr++;
    if(sample_ptr >= FFT_N) sample_ptr = 0;

    return 0;

}

void setup() {

    srand(read_cycle());

    sysctl_clock_enable(SYSCTL_CLOCK_FFT);
    sysctl_reset(SYSCTL_RESET_FFT);

    dmac_init();

    fpioa_set_function(AUDIO_PIN_L, FUNC_TIMER1_TOGGLE1);
    fpioa_set_function(AUDIO_PIN_R, FUNC_TIMER1_TOGGLE2);

    plic_init();
    sysctl_enable_irq();

    timer_init(TIMER_DEVICE_0);
    timer_set_interval(TIMER_DEVICE_0, TIMER_CHANNEL_0, TIMER_RATE);
    timer_irq_register(TIMER_DEVICE_0, TIMER_CHANNEL_0, 0, 1, audio_callback, NULL);
    timer_set_enable(TIMER_DEVICE_0, TIMER_CHANNEL_0, 1);

    pwm_init(PWM_DEVICE_1);
    pwm_set_enable(PWM_DEVICE_1, PWM_CHANNEL_0, 1);
    pwm_set_enable(PWM_DEVICE_1, PWM_CHANNEL_1, 1);

}

void loop() {

    if((rand()%100) < 1) spawn_grain();

    fft_data_t spectrum[FFT_N/2];

    build_spectrum(spectrum);

    fft_complex_uint16_dma(
        DMAC_CHANNEL1,
        DMAC_CHANNEL2,
        0,
        FFT_DIR_BACKWARD,
        (uint64_t*)spectrum,
        FFT_N,
        (uint64_t*)fft_out_data
    );

    for(int i=0;i<FFT_N/2;i++) {
        audio_out[2*i]   = fft_out_data[i].R1;
        audio_out[2*i+1] = fft_out_data[i].R2;
    }

}