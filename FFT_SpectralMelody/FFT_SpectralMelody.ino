// Spectral melody FFT hardware //

#include <stdio.h>
#include <timer.h>
#include <pwm.h>
#include <plic.h>
#include <sysctl.h>
#include <fpioa.h>
#include <fft.h>

#define FFT_N           512
#define SAMPLE_RATE     44100
#define TIMER_RATE      1000000000/SAMPLE_RATE
#define PWM_RATE        1000000
#define AUDIO_PIN_L     6
#define AUDIO_PIN_R     8

#define BPM             120
#define NUM_VOICES      8
#define BIN_MIN         1
#define BIN_MAX         59
#define DECAY_RATE      0.35f

typedef struct {
    int bin;
    float amp;
    float target_amp;
} Voice;

Voice voices[NUM_VOICES];

static int16_t audio_out[FFT_N];
volatile int sample_ptr = 0;

fft_data_t fft_in_data[FFT_N] __attribute__((aligned(64)));
fft_data_t fft_out_data[FFT_N] __attribute__((aligned(64)));

int audio_callback(void *ctx) {

    int16_t s = audio_out[sample_ptr];
    float duty = (float)(s + 32768) / 65536.0f;

    pwm_set_frequency(PWM_DEVICE_1, PWM_CHANNEL_0, PWM_RATE, (double)duty);
    pwm_set_frequency(PWM_DEVICE_1, PWM_CHANNEL_1, PWM_RATE, (double)duty);

    sample_ptr++;
    if (sample_ptr >= FFT_N) sample_ptr = 0;
    return 0;

}

void setup() {

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

    for (int i = 0; i < FFT_N / 2; i++) {
        fft_in_data[i].R1 = (int16_t)(fft_in_data[i].R1 * DECAY_RATE);
        fft_in_data[i].I1 = (int16_t)(fft_in_data[i].I1 * DECAY_RATE);
        fft_in_data[i].R2 = (int16_t)(fft_in_data[i].R2 * DECAY_RATE);
        fft_in_data[i].I2 = (int16_t)(fft_in_data[i].I2 * DECAY_RATE);
    }

    for (int v = 0; v < NUM_VOICES; v++) {

        if (voices[v].amp < 10.0f) {
            voices[v].bin = BIN_MIN + (rand() % (BIN_MAX - BIN_MIN));
            voices[v].target_amp = 500 + (rand() % 2000);
            voices[v].amp = voices[v].target_amp;
        }

        int b = voices[v].bin;
        int index = b / 2;
        int16_t val = (int16_t)voices[v].amp;

        if (b % 2 == 0) {
            fft_in_data[index].R1 += val;
        } else {
            fft_in_data[index].R2 += val;
        }

        voices[v].amp *= 0.05f; 
    }

    fft_complex_uint16_dma(DMAC_CHANNEL1, DMAC_CHANNEL2, 0, FFT_DIR_BACKWARD, 
                           (uint64_t *)fft_in_data, FFT_N, (uint64_t *)fft_out_data);

    for (int i = 0; i < FFT_N / 2; i++) {
        audio_out[2 * i]     = fft_out_data[i].R1;
        audio_out[2 * i + 1] = fft_out_data[i].R2;
    }

    int tempo = 60000 / BPM;
    delay(tempo / 4);

}