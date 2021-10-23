#include <stdio.h>
#include "pico/stdlib.h"   // stdlib 
#include "hardware/dma.h"  // dma
#include "hardware/irq.h"  // interrupts
#include "hardware/pwm.h"  // pwm 
#include "hardware/sync.h" // wait for interrupt 

#define AUDIO_PIN 28

/*
We need two buffers and two DMA channels. One copies two bytes at a time
from buffer 1 to the PWM register and the other copies buffer 2 to buffer 1
when buffer 1 is used up. As soon as buffer 2 is copied we fire an interrupt
so the next sample can be copied to (and duplicated) in buffer 2. Hopefully
before buffer 2 gets copied to buffer 1 again.
*/
volatile uint16_t buffer1[4]; // where the PWM values are read from
volatile uint16_t buffer2[4]; // where the calculated samples are put
int pwm_dma_chan;
int buffer_dma_chan;

volatile uint16_t sample = 0;

void dma_irh() {
    dma_channel_acknowledge_irq0(buffer_dma_chan);

    // just a sawtooth wave at arbitrary frequency for now
    if (sample < 1023) {
        ++sample;
    }
    else {
        sample = 0;
    }

    // 4 copies because 4 pulses per sample
    for (int i=0; i<4; ++i) {
        buffer2[i] = sample;
    }

    // @fhdmdev
    dma_channel_set_read_addr(buffer_dma_chan, buffer2, false);
}

int main() {
    stdio_init_all();

    // Putting some fake values in the buffer to see if the PWM pulls values off
    // the buffer and it runw through it once, then gets stuck on the last one
    // even though buffer_dma_chan copies buffer2 over to buffer1 and the
    // interrupt then fires that then puts new values into buffer2.
    // I can see the 128 on my oscilloscope repeated forever. The PWM frequency
    // is 125KHz as expected.
    buffer1[0] = 512;
    buffer1[1] = 256;
    buffer1[2] = 512;
    buffer1[3] = 128;

    /*
    128MHz VCO = 768MHz FBDIV = 64 PD1 = 6 PD2 = 1

    This should allow us to do 10-bit samples, 4 duplicate samples for each one.

    So an audio rate of 31250Hz.

    Therefore the pulse rate will be 125KHz which should be well beyond the
    filter cutoff. (Because 10 bits = 1024 divisions per pulse. 128MHz/1024 =
    125KHz)

    PWM register is 16-bit, so 2 bytes per sample duplicated 4 times is an 8
    byte buffer if we want to trigger the interrupt at audio rate and calculate
    one sample at a time. (Which is easiest.)

    But that gives us just 4096 clock cycles per interrupt. We could have a
    larger buffer, but the only saving really is in the interrupt overhead -
    we'd then still have to calculate multiple samples per interrupt so we won't
    get that much more than 4096 clock cycles per sample anyway. Will see how
    this plays out first.
    */
    set_sys_clock_pll(768000000, 6, 1);

    sleep_ms(5000);

    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);

    int audio_pin_slice = pwm_gpio_to_slice_num(AUDIO_PIN);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&config, 1);
    pwm_config_set_wrap(&config, 1023); // 10-bit samples
    pwm_init(audio_pin_slice, &config, true);

    pwm_dma_chan = dma_claim_unused_channel(true); // for copying buffer1 to pwm
    buffer_dma_chan = dma_claim_unused_channel(true); // for copying buffer2 to buffer1

    dma_channel_config pwm_dma_chan_config = dma_channel_get_default_config(pwm_dma_chan);
    channel_config_set_transfer_data_size(&pwm_dma_chan_config, DMA_SIZE_16);
    channel_config_set_read_increment(&pwm_dma_chan_config, true);
    channel_config_set_write_increment(&pwm_dma_chan_config, false); // keep writing to the same address
    channel_config_set_chain_to(&pwm_dma_chan_config, buffer_dma_chan); // when this one completes, copy from the double buffer
    channel_config_set_dreq(&pwm_dma_chan_config, DREQ_PWM_WRAP0 + audio_pin_slice); // Transfer on PWM cycle end
    
    // Clearly this ring buffer isn't wrapping. Do I have to do something extra to make that happen? Is this even right?
    channel_config_set_ring(&pwm_dma_chan_config, false, 3); // false=read ring buffer. 4 copies of 2 bytes == 8 bytes which is 2^^3

    dma_channel_configure(
        pwm_dma_chan,
        &pwm_dma_chan_config,
        &pwm_hw->slice[audio_pin_slice].cc, // Write to PWM slice CC register
        buffer1, // Read from buffer1
        4, // Should this be 1 or 4? I want to copy 1 at a time but the buffer has 4 in total?
        false // @fhdmdev
    );

    dma_channel_config buffer_dma_chan_config = dma_channel_get_default_config(buffer_dma_chan);
    channel_config_set_transfer_data_size(&buffer_dma_chan_config, DMA_SIZE_16);
    channel_config_set_read_increment(&buffer_dma_chan_config, true); 
    channel_config_set_write_increment(&buffer_dma_chan_config, true);

    // @fhdmdev
    channel_config_set_chain_to(&buffer_dma_chan_config, pwm_dma_chan); // when this one completes, copy from the double buffer
    
    // I don't think these ring buffers are working either

    // @fhdmdev
    // channel_config_set_ring(&buffer_dma_chan_config, false, 3); // read ring buffer. 4 copies of 2 bytes == 8 bytes which is 2^^3
    channel_config_set_ring(&buffer_dma_chan_config, true, 3); // write ring buffer. 4 copies of 2 bytes == 8 bytes which is 2^^3

    dma_channel_configure(
        buffer_dma_chan,
        &buffer_dma_chan_config,
        buffer1, // Write to the buffer the PWM samples are read from
        buffer2, // Read from the double buffer
        4, // because each sample becomes 4 pulses and we want to copy the whole buffer
        false // Don't start yet
    );

    // Fire interrupt when buffer DMA channel is done so the next sample can be put in the buffer
    dma_channel_set_irq0_enabled(buffer_dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irh);
    irq_set_enabled(DMA_IRQ_0, true);

    // @fhdmdev
    dma_channel_start(buffer_dma_chan);

    while (true) {
        printf("Hello, world!\n");
        sleep_ms(1000);
    }
    return 0;
}


