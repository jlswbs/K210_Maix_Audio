// Spectral data moshing FFT hardware //

#include "timer.h"
#include "fft.h"

#define FFT_N           512
#define SAMPLE_RATE     44100
#define TIMER_RATE      1000000000/SAMPLE_RATE
#define PWM_RATE        1000000
#define AUDIO_PIN_L     6
#define AUDIO_PIN_R     8

#define HISTORY_SIZE 8

static int16_t audio_out[FFT_N];
volatile int sample_ptr = 0;

fft_data_t fft_in_data[FFT_N] __attribute__((aligned(64)));
fft_data_t fft_out_data[FFT_N] __attribute__((aligned(64)));

fft_data_t spectrum_history[HISTORY_SIZE][FFT_N/2];
int history_index = 0;

float glitch_intensity = 0.3f;

int audio_callback(void *ctx) {

    int16_t s = audio_out[sample_ptr];
    float duty = (float)(s + 32768) / 65536.0f;

    pwm_set_frequency(PWM_DEVICE_1, PWM_CHANNEL_0, PWM_RATE, (double)duty);
    pwm_set_frequency(PWM_DEVICE_1, PWM_CHANNEL_1, PWM_RATE, (double)duty);

    sample_ptr++;
    if (sample_ptr >= FFT_N) sample_ptr = 0;
    return 0;

}

void save_to_history() {

    for (int i = 0; i < FFT_N/2; i++) {
        spectrum_history[history_index][i].R1 = fft_in_data[i].R1;
        spectrum_history[history_index][i].I1 = fft_in_data[i].I1;
        spectrum_history[history_index][i].R2 = fft_in_data[i].R2;
        spectrum_history[history_index][i].I2 = fft_in_data[i].I2;
    }
    history_index = (history_index + 1) % HISTORY_SIZE;

}

void load_from_history(int offset, int target_bin_start, int count) {

    int past_index = (history_index - offset - 1 + HISTORY_SIZE) % HISTORY_SIZE;
    
    for (int i = 0; i < count; i++) {
        int target = target_bin_start + i;
        int source = i;
        
        if (target < FFT_N/2 && source < FFT_N/2) {
            fft_in_data[target].R1 = spectrum_history[past_index][source].R1;
            fft_in_data[target].I1 = spectrum_history[past_index][source].I1;
            fft_in_data[target].R2 = spectrum_history[past_index][source].R2;
            fft_in_data[target].I2 = spectrum_history[past_index][source].I2;
        }
    }

}

void glitch_block_shift() {

    int block_size = 4 + (rand() % 8);
    
    for (int i = 0; i < FFT_N/2 - block_size; i += block_size) {
        if (rand() % 100 < glitch_intensity * 30) {
            int swap_with = rand() % (FFT_N/2 - block_size);
            
            for (int b = 0; b < block_size; b++) {
                if (i + b < FFT_N/2 && swap_with + b < FFT_N/2) {
                    fft_data_t temp;
                    temp.R1 = fft_in_data[i + b].R1;
                    temp.I1 = fft_in_data[i + b].I1;
                    temp.R2 = fft_in_data[i + b].R2;
                    temp.I2 = fft_in_data[i + b].I2;
                    
                    fft_in_data[i + b] = fft_in_data[swap_with + b];
                    fft_in_data[swap_with + b] = temp;
                }
            }
        }
    }

}

void glitch_compression_echo() {

    if (rand() % 100 < glitch_intensity * 40) {
        int echo_offset = 1 + (rand() % 3);
        int echo_start = rand() % (FFT_N/2 - 16);
        int echo_length = 8 + (rand() % 16);
        
        load_from_history(echo_offset, echo_start, echo_length);
    }

}

void glitch_bit_errors() {

    for (int i = 0; i < FFT_N/2; i++) {
        if (rand() % 100 < glitch_intensity * 15) {
            if (rand() % 2) {
                fft_in_data[i].R1 = (int16_t)(fft_in_data[i].R1 * 0.5f + (rand() % 10000 - 5000));
            } else {
                fft_in_data[i].I1 = (int16_t)(fft_in_data[i].I1 * 0.5f + (rand() % 10000 - 5000));
            }
            
            if (rand() % 100 < 20) {
                int16_t temp = fft_in_data[i].R2;
                fft_in_data[i].R2 = fft_in_data[i].R1;
                fft_in_data[i].R1 = temp;
            }
        }
    }

}

void glitch_frequency_hold() {

    if (rand() % 100 < glitch_intensity * 25) {
        int hold_bin = rand() % (FFT_N/2 - 10);
        int hold_length = 3 + (rand() % 6);
        
        for (int i = 0; i < hold_length; i++) {
            if (hold_bin + i < FFT_N/2) {
                fft_in_data[hold_bin + i] = fft_in_data[hold_bin];
            }
        }
    }

}

void spectral_limiter() {

    float max_amp = 30000;
    
    for (int i = 0; i < FFT_N/2; i++) {
        float amp1 = sqrt(fft_in_data[i].R1 * fft_in_data[i].R1 + 
                         fft_in_data[i].I1 * fft_in_data[i].I1);
        float amp2 = sqrt(fft_in_data[i].R2 * fft_in_data[i].R2 + 
                         fft_in_data[i].I2 * fft_in_data[i].I2);
        
        if (amp1 > max_amp) {
            float scale = max_amp / amp1;
            fft_in_data[i].R1 *= scale;
            fft_in_data[i].I1 *= scale;
        }
        
        if (amp2 > max_amp) {
            float scale = max_amp / amp2;
            fft_in_data[i].R2 *= scale;
            fft_in_data[i].I2 *= scale;
        }
    }
    
    for (int i = FFT_N/4; i < FFT_N/2; i++) {
        fft_in_data[i].R1 *= 0.95f;
        fft_in_data[i].I1 *= 0.95f;
        fft_in_data[i].R2 *= 0.95f;
        fft_in_data[i].I2 *= 0.95f;
    }

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
    
    for (int h = 0; h < HISTORY_SIZE; h++) {
        for (int i = 0; i < FFT_N/2; i++) {
            spectrum_history[h][i].R1 = 0;
            spectrum_history[h][i].I1 = 0;
            spectrum_history[h][i].R2 = 0;
            spectrum_history[h][i].I2 = 0;
        }
    }

}

void loop() {

    for (int i = 0; i < FFT_N/2; i++) {
        fft_in_data[i].R1 = 0;
        fft_in_data[i].I1 = 0;
        fft_in_data[i].R2 = 0;
        fft_in_data[i].I2 = 0;
    }
    
    for (int h = 1; h <= 8; h++) {
        int bin = h * 4;
        float amp = 8000 / h;
        
        if (bin < FFT_N/2) {
            if (bin % 2 == 0) {
                fft_in_data[bin/2].R1 = (int16_t)amp;
                fft_in_data[bin/2].I1 = 0;
            } else {
                fft_in_data[bin/2].R2 = (int16_t)amp;
                fft_in_data[bin/2].I2 = 0;
            }
        }
    }
    
    save_to_history();
    
    if (rand() % 100 < 5) {
        glitch_intensity = 0.2f + (rand() % 80) / 100.0f;
    }
    
    glitch_block_shift();
    glitch_compression_echo();
    glitch_bit_errors();
    glitch_frequency_hold();

    spectral_limiter();
    
    fft_complex_uint16_dma(DMAC_CHANNEL1, DMAC_CHANNEL2, 0, 
                           FFT_DIR_BACKWARD, (uint64_t *)fft_in_data, 
                           FFT_N, (uint64_t *)fft_out_data);
    
    for (int i = 0; i < FFT_N / 2; i++) {
        audio_out[2 * i]     = fft_out_data[i].R1;
        audio_out[2 * i + 1] = fft_out_data[i].R2;
    }
    
    delay(30);

}