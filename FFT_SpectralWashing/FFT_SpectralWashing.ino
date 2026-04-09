// Spectral washing automat FFT hardware //

#include "timer.h"
#include "fft.h"

#define FFT_N 512
#define SAMPLE_RATE 44100
#define TIMER_RATE 1000000000/SAMPLE_RATE
#define PWM_RATE 1000000
#define AUDIO_PIN_L 6
#define AUDIO_PIN_R 8

#define MAX_PARTICLES 48

static int16_t audio_out[FFT_N];
volatile int sample_ptr = 0;

fft_data_t fft_out_data[FFT_N] __attribute__((aligned(64)));

typedef struct {

    float pos;
    float vel;

    float amp;
    float amp_vel;

    int life;

} particle_t;

particle_t particles[MAX_PARTICLES];

void spawn_particle(particle_t *p) {

    p->pos = rand() % (FFT_N/2);
    p->vel = ((rand()%2000)-1000) * 0.0002f;
    p->amp = 2000 + rand()%8000;
    p->amp_vel = ((rand()%2000)-1000) * 0.01f;
    p->life = 2000 + rand()%8000;

}

void update_particles() {

    for(int i=0;i<MAX_PARTICLES;i++) {

        particle_t *p = &particles[i];

        if(p->life <= 0){
            if(rand()%100 < 2)
                spawn_particle(p);

            continue;
        }

        p->pos += p->vel;
        p->amp += p->amp_vel;

        if(p->pos < 2) p->pos = 2;
        if(p->pos > FFT_N/2 - 2) p->pos = FFT_N/2 - 2;

        if(p->amp < 0) p->amp = 0;
        if(p->amp > 20000) p->amp = 20000;

        p->life--;
    }

}

void build_spectrum(fft_data_t *spec) {

    for(int i=0;i<FFT_N/2;i++) {
        spec[i].R1 = 0;
        spec[i].R2 = 0;
        spec[i].I1 = 0;
        spec[i].I2 = 0;
    }

    for(int i=0;i<MAX_PARTICLES;i++) {

        particle_t *p = &particles[i];

        if(p->life <= 0) continue;

        int bin = (int)p->pos;

        int a = p->amp;

        if(bin & 1)
            spec[bin/2].R2 += a;
        else
            spec[bin/2].R1 += a;

        int spread = a * 0.3f;

        if(bin > 2){
            if((bin-1)&1)
                spec[(bin-1)/2].R2 += spread;
            else
                spec[(bin-1)/2].R1 += spread;
        }

        if(bin < FFT_N/2-2) {
            if((bin+1)&1)
                spec[(bin+1)/2].R2 += spread;
            else
                spec[(bin+1)/2].R1 += spread;
        }
    }

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

    update_particles();

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

    for(int i=0;i<FFT_N/2;i++){
        audio_out[2*i]   = fft_out_data[i].R1;
        audio_out[2*i+1] = fft_out_data[i].R2;
    }

}