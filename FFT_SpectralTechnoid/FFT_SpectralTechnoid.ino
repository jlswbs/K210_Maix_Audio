// Spectral technoid FFT hardware //

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

fft_data_t fft_in_data[FFT_N] __attribute__((aligned(64)));
fft_data_t fft_out_data[FFT_N] __attribute__((aligned(64)));

static int16_t delay_buffer[FFT_N / 2] = {0};

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

    static uint32_t t = 0;
    static bool flip = false;
    static uint16_t cluster_states[16] = {0}; 
    t++;

    if (t % 16 == 0) flip = !flip;

    for (int i = 0; i < FFT_N / 2; i++) {
        fft_in_data[i].R1 = fft_in_data[i].I1 = 0;
        fft_in_data[i].R2 = fft_in_data[i].I2 = 0;
    }

    bool is_kick = (t % 16 == 0); 
    if (is_kick) {
        fft_in_data[0].R1 = 32767; 
        fft_in_data[1].I1 = 32767; 
    }

    bool drum_trigger = (t % 8 < 2); 
    if (drum_trigger) {
        for (int j = 1; j <= 32; j++) {
            int bin_idx = flip ? (33 - j) : j;
            int16_t amp = 20000 / j; 
            int target = bin_idx + 4;
            if (target < FFT_N / 2) {
                fft_in_data[target].R1 += amp;
            }
        }
    }

    for (int k = 0; k < 16; k++) {

        if ((t & 0x07) == 0) { 
            if ((rand() % 100) < 10) {
                cluster_states[k] = (rand() % 100 > 70) ? (1000 + (rand() % 3000)) : 0;
                cluster_states[k] ^= (t & 0x03FF);
            }
        }

        if (cluster_states[k] > 0) {
            int cluster_bin = 90 + (k * 3); 
            int16_t dynamic_amp = rand() % cluster_states[k];
            if ((t % 16) < 4) dynamic_amp |= 0x1000;

            fft_in_data[cluster_bin].R2 = dynamic_amp;
            fft_in_data[cluster_bin].I2 = (t % 2 == 0) ? (1000 ^ k) : (-1000 ^ k);
        }

    }

    fft_complex_uint16_dma(DMAC_CHANNEL1, DMAC_CHANNEL2, 0, FFT_DIR_BACKWARD, (uint64_t *)fft_in_data, FFT_N, (uint64_t *)fft_out_data);

    for (int i = 0; i < FFT_N / 2; i++) {

        int16_t mask_l = (i < 180) ? 0xFFFF : 0x0000;
        int16_t L = (drum_trigger || is_kick) ? (fft_out_data[i].R1 & mask_l) : 0;
        int16_t echo = (delay_buffer[i] >> 1) ^ (is_kick ? 0x00FF : 0); 
        
        L |= echo; 
        delay_buffer[i] = L;

        audio_out[2 * i]     = (L >> 4) << 4; 
        audio_out[2 * i + 1] = (fft_out_data[i].R2 >> 2) << 2;
    }

    delay(14); 

}