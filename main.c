/*
 * HC-SR04 Ultrasonic Sensor – Bare-Metal STM32 (PA0 = TRIG, PA1 = ECHO using TIM2_CH2)
 *
 * Step-by-Step System Overview:
 *
 * 1. RCC – Enable Peripheral Clocks
 *    - AHB1ENR → Enable GPIOA (for PA0 and PA1)
 *    - APB1ENR → Enable TIM2 (used for measuring ECHO pulse width)
 *
 * 2. GPIO Setup
 *    - PA0 (TRIG): Set as general-purpose output (MODER = 01), push-pull (OTYPER = 0)
 *    - PA1 (ECHO): Set to Alternate Function mode (MODER = 10), select AF1 (TIM2_CH2)
 *
 * 3. TIM2 Configuration (Input Capture Mode on CH2)
 *    - PSC: Set prescaler to divide 84 MHz → 1 µs tick (PSC = 83)
 *    - ARR: Set auto-reload to max (0xFFFF) so timer doesn't overflow prematurely
 *    - CCMR1: Configure CH2 as input, mapped to TI2 (PA1)
 *    - CCER:
 *        - Enable CH2 capture (CC2E = 1)
 *        - Start with rising edge (CC2P = 0), switch to falling later
 *    - CR1: Enable timer (CEN = 1)
 *
 * 4. Trigger the Sensor
 *    - Set PA0 HIGH for ~10 µs to start ultrasonic pulse
 *    - Then set PA0 LOW
 *
 * 5. Capture ECHO Pulse Duration
 *    - Wait for first capture event (rising edge), store startTime from CCR2
 *    - Switch to falling edge capture (set CC2P = 1)
 *    - Wait for second capture event (falling edge), store endTime
 *    - Calculate pulseDuration = endTime - startTime
 *
 * 6. Convert to Distance
 *    - Distance in cm = (pulseDuration * 0.0343) / 2
 *      (0.0343 cm/µs is the speed of sound; divide by 2 for round-trip)
 *
 * Notes:
 *    - PA1 input must be voltage-limited to 3.3V → use voltage divider if ECHO outputs 5V
 *    - This code uses polling (not interrupts) for simplicity
 *    - Can be extended for continuous reads or UART output
 */


/*
 * HC-SR04 Ultrasonic Sensor – Fixed STM32F4 Discovery Code
 * PA0 = TRIG, PA1 = ECHO using TIM2_CH2
 *
 * Key fixes:
 * - Removed floating point arithmetic (causes hard fault without FPU)
 * - Added proper delay function for trigger pulse
 * - Fixed prescaler for 168MHz system clock
 * - Added overflow handling
 * - Improved error checking
 */

#include <stdint.h>
#include <stdio.h>

// RCC Registers
#define RCC_AHB1ENR    (*(volatile uint32_t*) 0x40023830)
#define RCC_APB1ENR    (*(volatile uint32_t*) 0x40023840)

// GPIOA Registers
#define GPIOA_MODER    (*(volatile uint32_t*) 0x40020000)
#define GPIOA_OTYPER   (*(volatile uint32_t*) 0x40020004)
#define GPIOA_AFRL     (*(volatile uint32_t*) 0x40020020)
#define GPIOA_ODR      (*(volatile uint32_t*) 0x40020014)

// TIM2 Registers
#define TIM2_CR1       (*(volatile uint32_t*) 0x40000000)
#define TIM2_CCMR1     (*(volatile uint32_t*) 0x40000018)
#define TIM2_CCER      (*(volatile uint32_t*) 0x40000020)
#define TIM2_PSC       (*(volatile uint32_t*) 0x40000028)
#define TIM2_ARR       (*(volatile uint32_t*) 0x4000002C)
#define TIM2_SR        (*(volatile uint32_t*) 0x40000010)
#define TIM2_CCR2      (*(volatile uint32_t*) 0x40000038)
#define TIM2_CNT       (*(volatile uint32_t*) 0x40000024)

// Simple delay function (approximate)
void delay_us(uint32_t us) {
    // Rough delay for 168MHz system clock
    // Adjust this value based on actual timing measurements
    volatile uint32_t count = us * 42; // ~4 cycles per loop at 168MHz
    while(count--)
    {
        __asm__("nop");
    }
}

void delay_ms(uint32_t ms) {
    for(uint32_t i = 0; i < ms; i++) {
        delay_us(1000);
    }
}

uint32_t measure_distance_cm(void) {
    uint32_t start_time, end_time, pulse_width;
    uint32_t timeout;

    // Clear any pending capture flags
    TIM2_SR &= ~(1 << 2); // Clear CC2IF

    // Reset timer counter
    TIM2_CNT = 0;

    // Set capture for rising edge
    TIM2_CCER &= ~(1 << 5); // CC2P = 0 (rising edge)

    // Generate trigger pulse (10us minimum)
    GPIOA_ODR |= (1 << 0);   // Set PA0 high
    delay_us(100);            // Wait 15us (more than minimum 10us)
    GPIOA_ODR &= ~(1 << 0);  // Set PA0 low

    // Wait for rising edge with timeout
    timeout = 50000; // Adjust as needed
    while (!(TIM2_SR & (1 << 2)) && --timeout)
    {
        // Check for timer overflow
        if (TIM2_CNT > 60000)
        { // ~60ms timeout
            return 0xFFFF; // Error: timeout
        }
    }
    if (timeout == 0) return 0xFFFF; // Timeout error

    // Capture start time and clear flag
    start_time = TIM2_CCR2;
    TIM2_SR &= ~(1 << 2);

    // Switch to falling edge
    TIM2_CCER |= (1 << 5); // CC2P = 1 (falling edge)

    // Wait for falling edge with timeout
    timeout = 50000;
    while (!(TIM2_SR & (1 << 2)) && --timeout)
    {
        // Check for timer overflow (max range ~400cm = ~24ms)
        if (TIM2_CNT > 30000)
        { // ~30ms timeout
            return 0xFFFF; // Error: timeout or out of range
        }
    }
    if (timeout == 0) return 0xFFFF; // Timeout error

    // Capture end time and clear flag
    end_time = TIM2_CCR2;
    TIM2_SR &= ~(1 << 2);

    // Calculate pulse width (handle potential overflow)
    if (end_time >= start_time)
    {
        pulse_width = end_time - start_time;
    } else
    {
        // Timer overflowed between captures
        pulse_width = (0xFFFF - start_time) + end_time + 1;
    }

    printf("Pulse width: %lu us\r\n", pulse_width);


    // Convert to distance in cm using integer math
    // Distance = (pulse_width_us * speed_of_sound_cm_per_us) / 2
    // Speed of sound ≈ 0.0343 cm/µs
    // So: distance_cm = (pulse_width * 343) / 2000
    uint32_t distance_cm = (pulse_width * 343) / 4000;
    printf("Distance: %lu cm\n", distance_cm);



    return distance_cm;
}

int main(void)
{
    // Enable GPIOA clock
    RCC_AHB1ENR |= (1 << 0);

    // Configure PA0 as output (TRIG)
    GPIOA_MODER &= ~(0x3 << 0); // Clear PA0 mode bits
    GPIOA_MODER |=  (0x1 << 0); // PA0 = General purpose output
    GPIOA_OTYPER &= ~(1 << 0);  // PA0 = Push-pull output

    // Configure PA1 as alternate function (ECHO - TIM2_CH2)
    GPIOA_MODER &= ~(0x3 << 2); // Clear PA1 mode bits
    GPIOA_MODER |=  (0x2 << 2); // PA1 = Alternate function
    GPIOA_AFRL  &= ~(0xF << 4); // Clear PA1 AF bits
    GPIOA_AFRL  |=  (0x1 << 4); // PA1 = AF1 (TIM2_CH2)

    // Enable TIM2 clock
    RCC_APB1ENR |= (1 << 0);

    // Configure TIM2 for input capture
    // For 168MHz system clock, TIM2 runs at 84MHz (APB1 * 2)
    // To get 1µs ticks: 84MHz / 84 = 1MHz = 1µs per tick
    TIM2_PSC = 83;              // Prescaler: 84MHz / 84 = 1MHz (1µs ticks)
    TIM2_ARR = 0xFFFF;          // Maximum auto-reload value

    // Configure CH2 as input capture
    TIM2_CCMR1 &= ~(0xFF << 8); // Clear CC2S and input filter bits
    TIM2_CCMR1 |= (1 << 8);     // CC2S = 01: IC2 mapped to TI2 (PA1)

    // Enable CH2 capture, start with rising edge
    TIM2_CCER &= ~(1 << 5);     // CC2P = 0: rising edge
    TIM2_CCER |=  (1 << 4);     // CC2E = 1: enable capture on CH2

    // Start timer
    TIM2_CR1 |= (1 << 0);       // CEN = 1: enable counter

    // Wait for timer to stabilize
    delay_ms(100);

    // Main loop

       uint32_t distance = 0;
       while (1) {
           distance = measure_distance_cm();

           if (distance == 0xFFFF) {
               printf("Distance Error or Timeout\r\n");
           } else {
               printf("Distance: %lu cm\r\n", distance);
           }

           delay_ms(100); // Wait 100ms between measurements
       }

    return 0;
}
