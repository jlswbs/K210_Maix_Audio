// Neural reconfigurable synthesizer //

#include "timer.h"

#define SAMPLE_RATE 44100
#define TIMER_RATE 1000000000 / SAMPLE_RATE
#define PWM_RATE 1000000
#define AUDIO_PIN_L 6
#define AUDIO_PIN_R 8

#define MODULES 20
#define MAX_CONNECTIONS (MODULES * MODULES * 2)
#define BUFFER_SIZE 256

enum ModuleType {
    MOD_SINE,
    MOD_SAW,
    MOD_NOISE,
    MOD_FILTER,
    MOD_VCA,
    MOD_DELAY,
    MOD_DISTORT,
    MOD_LFO,
    MOD_COUNT
};

const char* module_names[] = {
    "SINE", "SAW", "NOISE", "FILTER", "VCA", "DELAY", "DISTORT", "LFO"
};

struct Module {
    ModuleType type;
    float params[4];
    float state[8];
    float input;
    float output;
    float modulation;
};

struct Connection {
    int source_module;
    int target_module;
    int target_input;
    float weight;
};

Module modules[MODULES];
Connection connections[MAX_CONNECTIONS];
int num_connections = 0;

float audio_buffer[BUFFER_SIZE];
volatile int playback_ptr = 0;
static int mix_ptr = 0;

float randomf(float min, float max) { return min + (max - min) * ((float)rand() / (float)RAND_MAX); }

void process_sine(Module* m) {

    static float phase[MODULES] = {0};
    int idx = m - modules;
    
    float freq = powf(10.0f, (m->params[0] * 0.5f + 0.5f) * 4.3f);
    freq += m->modulation * 500.0f;
    freq += m->input * 200.0f;
    freq = fmaxf(0.1f, freq);
    
    float amp = fabsf(m->params[1]) * 1.0f;
    
    phase[idx] += 2.0f * M_PI * freq / SAMPLE_RATE;
    if(phase[idx] >= 2.0f * M_PI) phase[idx] -= 2.0f * M_PI;
    
    m->output = sinf(phase[idx] + m->params[2] * M_PI) * amp;

}

void process_saw(Module* m) {

    static float phase[MODULES] = {0};
    int idx = m - modules;
    
    float freq = powf(10.0f, (m->params[0] * 0.5f + 0.5f) * 4.3f);
    freq += m->modulation * 500.0f;
    freq += m->input * 200.0f;
    freq = fmaxf(0.1f, freq);
    
    float amp = fabsf(m->params[1]) * 1.0f;
    
    phase[idx] += 2.0f * M_PI * freq / SAMPLE_RATE;
    if(phase[idx] >= 2.0f * M_PI) phase[idx] -= 2.0f * M_PI;
    
    float t = phase[idx] / (2.0f * M_PI);
    float saw = (2.0f * t - 1.0f);
    m->output = saw * amp;

}

void process_noise(Module* m) {

    float amp = fabsf(m->params[0]) * 1.2f;
    float color = m->params[1];
    static float pink_state[MODULES] = {0};
    static float brown_state[MODULES] = {0};
    int idx = m - modules;
    
    float white = randomf(-1.0f, 1.0f);
    
    float input_mod = m->input * 0.5f;
    float modulated_color = color + input_mod;
    modulated_color = fminf(1.0f, fmaxf(-1.0f, modulated_color));
    
    float noise_out;
    if(modulated_color < -0.5f) {
        noise_out = white;
    } else if(modulated_color < 0.5f) {
        pink_state[idx] = pink_state[idx] * 0.9f + white * 0.1f;
        noise_out = pink_state[idx];
    } else {
        brown_state[idx] = brown_state[idx] * 0.95f + white * 0.05f;
        noise_out = brown_state[idx];
    }
    
    float input_gain = 1.0f - fabsf(m->input) * 0.5f;
    
    m->output = noise_out * amp * input_gain;

}

void process_filter(Module* m) {

    static float prev[MODULES] = {0};
    static float band[MODULES] = {0};
    int idx = m - modules;
    
    float cutoff = powf(10.0f, (m->params[0] * 0.5f + 0.5f) * 4.0f);
    cutoff += m->modulation * 4000.0f;
    cutoff += m->input * 800.0f;
    cutoff = fminf(18000.0f, fmaxf(40.0f, cutoff));
    
    float resonance = fabsf(m->params[1]) * 0.92f;
    resonance += m->modulation * 0.4f;
    resonance = fminf(0.96f, fmaxf(0.0f, resonance));
    
    float q = 1.0f - resonance;
    float omega = 2.0f * M_PI * cutoff / SAMPLE_RATE;
    float alpha = sinf(omega) / (2.0f * q + 0.001f);
    float cos_omega = cosf(omega);
    
    float input = m->input;
    float output = (1.0f - cos_omega) / 2.0f * input +
                   cos_omega * prev[idx] -
                   (1.0f - alpha) * band[idx];
    
    band[idx] = alpha * (output - band[idx]) + cos_omega * band[idx];
    prev[idx] = output;
    
    m->output = fminf(0.98f, fmaxf(-0.98f, output));

}

void process_vca(Module* m) {

    float gain = fabsf(m->params[0]) * 2.0f;
    gain += m->modulation * 1.0f;
    gain = fminf(2.0f, fmaxf(0.0f, gain));
    gain = 1.0f - expf(-gain * 3.0f);
    m->output = m->input * gain;

}

void process_delay(Module* m) {

    static float buffer[MODULES][BUFFER_SIZE * 4] = {{0}};
    static int write_ptr[MODULES] = {0};
    int idx = m - modules;
    
    float time = fabsf(m->params[0]) * 0.5f;
    float feedback = fabsf(m->params[1]) * 0.88f;
    feedback += m->modulation * 0.4f;
    feedback = fminf(0.92f, fmaxf(0.0f, feedback));
    
    int delay_samples = (int)(time * SAMPLE_RATE);
    delay_samples = fminf(BUFFER_SIZE * 2 - 1, fmaxf(1, delay_samples));
    
    int read_ptr = write_ptr[idx] - delay_samples;
    if(read_ptr < 0) read_ptr += BUFFER_SIZE * 4;
    
    float delayed = buffer[idx][read_ptr];
    buffer[idx][write_ptr[idx]] = m->input + delayed * feedback;
    m->output = delayed * (0.3f + fabsf(m->params[2]) * 0.7f);
    
    write_ptr[idx]++;
    if(write_ptr[idx] >= BUFFER_SIZE * 4) write_ptr[idx] = 0;

}

void process_distort(Module* m) {

    float drive = fabsf(m->params[0]) * 8.0f;
    drive += m->modulation * 3.0f;
    drive = fminf(12.0f, fmaxf(0.1f, drive));
    
    float type = m->params[1];
    float input = m->input * drive;
    float output;
    
    if(type < -0.5f) {
        output = tanhf(input);
    } else if(type < 0.0f) {
        output = fminf(0.9f, fmaxf(-0.9f, input));
    } else if(type < 0.5f) {
        output = fmodf(input + 1.0f, 2.0f) - 1.0f;
    } else {
        float folded = fabsf(fmodf(input + 1.0f, 4.0f) - 2.0f) - 1.0f;
        output = folded;
    }
    
    m->output = fminf(1.0f, fmaxf(-1.0f, output));

}

void process_lfo(Module* m) {

    static float phase[MODULES] = {0};
    int idx = m - modules;
    
    float freq = powf(10.0f, (m->params[0] * 0.5f + 0.5f) * 3.3f);
    freq += m->modulation * 5.0f;
    freq += m->input * 2.0f;
    freq = fmaxf(0.005f, freq);
    
    float amp = fabsf(m->params[1]) * 1.5f;
    float waveform = m->params[2];
    
    phase[idx] += 2.0f * M_PI * freq / SAMPLE_RATE;
    if(phase[idx] >= 2.0f * M_PI) phase[idx] -= 2.0f * M_PI;
    
    float t = phase[idx] / (2.0f * M_PI);
    float output = 0.0f;
    
    if(waveform < -0.5f) {
        output = sinf(phase[idx]);
    } else if(waveform < 0.5f) {
        output = 2.0f * fabsf(2.0f * t - 1.0f) - 1.0f;
    } else {
        output = 2.0f * t - 1.0f;
    }
    
    m->output = output * amp;

}

void create_unique_universe() {

    memset(modules, 0, sizeof(modules));
    num_connections = 0;

    int num_osc   = MODULES * 0.28f;
    int num_lfo   = MODULES * 0.22f;
    int num_filter = MODULES * 0.18f;
    int num_vca   = MODULES * 0.12f;
    int num_noise = MODULES * 0.08f;
    int num_delay = MODULES * 0.07f;
    int num_dist  = MODULES * 0.05f;

    int idx = 0;

    for(int i = 0; i < num_osc && idx < MODULES; i++) {
        modules[idx++].type = (rand() % 2 == 0) ? MOD_SINE : MOD_SAW;
    }

    for(int i = 0; i < num_noise && idx < MODULES; i++) modules[idx++].type = MOD_NOISE;
    for(int i = 0; i < num_lfo && idx < MODULES; i++) modules[idx++].type = MOD_LFO;
    for(int i = 0; i < num_filter && idx < MODULES; i++) modules[idx++].type = MOD_FILTER;
    for(int i = 0; i < num_vca && idx < MODULES; i++) modules[idx++].type = MOD_VCA;
    for(int i = 0; i < num_delay && idx < MODULES; i++) modules[idx++].type = MOD_DELAY;
    for(int i = 0; i < num_dist && idx < MODULES; i++) modules[idx++].type = MOD_DISTORT;

    while(idx < MODULES) {
        modules[idx++].type = (ModuleType)(rand() % MOD_COUNT);
    }

    for(int i = 0; i < MODULES; i++) {
        Module* m = &modules[i];
        
        for(int p = 0; p < 4; p++) {
            m->params[p] = randomf(-1.0f, 1.0f);
        }

        switch(m->type) {
            case MOD_SINE:
            case MOD_SAW:
                m->params[0] = randomf(-0.4f, 0.8f);
                m->params[1] = randomf(0.65f, 1.0f);
                break;
                
            case MOD_LFO:
                m->params[0] = randomf(-0.8f, 0.6f);
                m->params[1] = randomf(0.7f, 1.3f);
                break;
                
            case MOD_FILTER:
                m->params[0] = randomf(0.2f, 0.9f);
                m->params[1] = randomf(0.1f, 0.75f);
                break;
                
            case MOD_DELAY:
                m->params[0] = randomf(0.1f, 0.7f);
                m->params[1] = randomf(0.25f, 0.75f);
                break;
                
            case MOD_VCA:
                m->params[0] = randomf(0.4f, 1.0f);
                break;
                
            case MOD_DISTORT:
                m->params[0] = randomf(0.2f, 0.9f);
                break;
                
            default:
                break;
        }
    }

    for(int i = 0; i < MODULES * 0.35f; i++) {
        int src = rand() % MODULES;

        if(modules[src].type == MOD_SINE || 
           modules[src].type == MOD_SAW || 
           modules[src].type == MOD_NOISE) {

            int dst = rand() % MODULES;
            add_connection(src, dst, 0, randomf(0.6f, 1.4f));
        }
    }

    for(int i = 0; i < MODULES * 0.65f; i++) {
        int lfo = rand() % MODULES;

        if(modules[lfo].type != MOD_LFO) continue;
        
        int target = rand() % MODULES;
        if(target == lfo) continue;
        
        int target_in = (rand() % 4 == 0) ? 4 : (1 + rand() % 3);

        add_connection(lfo, target, target_in, randomf(0.4f, 1.3f));
    }

    for(int i = 0; i < MODULES * 1.8f; i++) {
        int src = rand() % MODULES;
        int dst = rand() % MODULES;

        if(src == dst) continue;
        
        int tin = rand() % 5;
        float weight = randomf(-1.3f, 1.3f);
        
        if((modules[src].type == MOD_SINE || modules[src].type == MOD_SAW) && tin == 0) {
            weight *= 1.7f;
        }

        add_connection(src, dst, tin, weight);
    }

}

void add_connection(int src, int dst, int target_in, float weight) {

    if(num_connections >= MAX_CONNECTIONS) return;

    connections[num_connections].source_module = src;
    connections[num_connections].target_module = dst;
    connections[num_connections].target_input = target_in;
    connections[num_connections].weight = weight;

    num_connections++;

}

void process_modules() {

    for(int i = 0; i < MODULES; i++) {
        modules[i].input = 0.0f;
        modules[i].modulation = 0.0f;
    }
    
    for(int c = 0; c < num_connections; c++) {
        Connection* conn = &connections[c];
        float signal = modules[conn->source_module].output;
        
        switch(conn->target_input) {
            case 0:
                modules[conn->target_module].input += signal * conn->weight;
                break;

            case 1:
                modules[conn->target_module].params[0] += signal * conn->weight * 0.5f;
                break;

            case 2:
                modules[conn->target_module].params[1] += signal * conn->weight * 0.5f;
                break;

            case 3:
                modules[conn->target_module].params[2] += signal * conn->weight * 0.5f;
                break;

            case 4:
                modules[conn->target_module].modulation += signal * conn->weight * 0.4f;
                break;
        }
    }
    
    for(int i = 0; i < MODULES; i++) {
        for(int p = 0; p < 4; p++) {
            modules[i].params[p] = fminf(2.5f, fmaxf(-2.5f, modules[i].params[p]));
        }

        modules[i].modulation = fminf(2.0f, fmaxf(-2.0f, modules[i].modulation));
    }
    
    for(int i = 0; i < MODULES; i++) {
        switch(modules[i].type) {
            case MOD_SINE:
                process_sine(&modules[i]);
                break;

            case MOD_SAW:
                process_saw(&modules[i]);
                break;

            case MOD_NOISE:
                process_noise(&modules[i]);
                break;

            case MOD_FILTER:
                process_filter(&modules[i]);
                break;

            case MOD_VCA:
                process_vca(&modules[i]);
                break;

            case MOD_DELAY:
                process_delay(&modules[i]);
                break;

            case MOD_DISTORT:
                process_distort(&modules[i]);
                break;

            case MOD_LFO:
                process_lfo(&modules[i]);
                break;

            default:
                break;
        }
    }

    float sum = 0.0f;
    for(int i = 0; i < MODULES; i++) { sum += modules[i].output; }
    float sample = tanhf(sum * (1.0f / MODULES));
    
    audio_buffer[mix_ptr] = sample;

    mix_ptr = (mix_ptr + 1) % BUFFER_SIZE;

}

int audio_callback(void *ctx) {

    process_modules();
    
    float sample = audio_buffer[playback_ptr];
    float duty = 0.5f * (sample + 1.0f);
    
    pwm_set_frequency(PWM_DEVICE_1, PWM_CHANNEL_0, PWM_RATE, duty);
    pwm_set_frequency(PWM_DEVICE_1, PWM_CHANNEL_1, PWM_RATE, duty);
    
    playback_ptr = (playback_ptr + 1) % BUFFER_SIZE;
    
    return 0;

}

void setup() {

    srand(read_cycle());
    
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
    
    create_unique_universe();

}

void loop() {

}