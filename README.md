# stm32-hcsr04-ultrasonic
Bare-metal STM32F4 project using the TIM2 input capture mode to measure distance via the HC-SR04 ultrasonic sensor.
Written in pure Embedded C with no HAL or CubeMX—direct register access only.

FEATURES:
Input capture on PA1 using TIM2 CH2

Distance calculation based on echo pulse width

Configurable system clock and prescaler for 1µs timer resolution

Clean, minimal codebase for educational purposes

HARDWARE: 
STM32F407 Discovery board

HC-SR04 ultrasonic sensor

STM32CubeIDE (used for flashing/debugging only)
