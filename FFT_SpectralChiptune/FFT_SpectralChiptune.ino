// Spectral chiptune FFT hardware //

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

    static uint32_t frame = 0;
    static uint8_t seq_step = 0;
    static uint16_t base_note = 12;
    frame++;

    if (frame % 6 == 0) { 
        seq_step++;
        if (seq_step % 16 == 0) base_note = 8 + (rand() % 12); 
    }

    for (int i = 0; i < FFT_N / 2; i++) {
        fft_in_data[i].R1 = fft_in_data[i].I1 = 0;
        fft_in_data[i].R2 = fft_in_data[i].I2 = 0;
    }

    bool trigger = (frame % 6 == 0); 

    if (trigger) {

        if (seq_step % 4 == 0) fft_in_data[0].R2 = 15000;

        int bass_target = (seq_step % 4 == 0) ? base_note : 2 + rand() % base_note;
        fft_in_data[bass_target-4].R1 = 15000;

        int lead_mult = (seq_step % 3 == 1) ? 3 : 6;
        int lead_bin = base_note * lead_mult;
        if (lead_bin < FFT_N / 2) {
            fft_in_data[lead_bin].R2 = 4000 + rand() % 6000;
            fft_in_data[lead_bin + 2].I2 = 20000;
        }

        if (seq_step % 3 == 1) {
            for (int n = 50; n < 150; n++) fft_in_data[n].R1 = rand() % 10000;
        }

    }

    fft_complex_uint16_dma(DMAC_CHANNEL1, DMAC_CHANNEL2, 0, FFT_DIR_BACKWARD, (uint64_t *)fft_in_data, FFT_N, (uint64_t *)fft_out_data);

    for (int i = 0; i < FFT_N / 2; i++) {

        int16_t mask = (i < 1024) ? 0xFFFF : 0x1F00; 

        audio_out[2 * i]     = (fft_out_data[i].R1 & mask); 
        audio_out[2 * i + 1] = (fft_out_data[i].R2 & mask);

    }

    delay(14);

}