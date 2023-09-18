#include <stdint.h>

extern uint32_t DAC_BASE;

#define MMIO32(addr)	(addr)

#define DAC1				DAC_BASE

/** DAC channel1 12-bit right-aligned data holding register (DAC_DHR12R1) */
#define DAC_DHR12R1(dac)		DAC_BASE



/** DAC channel2 12-bit right aligned data holding register (DAC_DHR12R2) */
#define DAC_DHR12R2(dac)		DAC_BASE
