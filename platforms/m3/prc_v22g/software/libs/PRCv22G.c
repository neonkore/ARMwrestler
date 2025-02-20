//*******************************************************************
//Author: Yejoong Kim
//Description: PRCv22G lib file
//-------------------------------------------------------------------
//  <Update History>
//  Jul 13 2020 - PRCv21 Family
//                  - Updated set_wakeup_timer according to the new register bitmap.
//                      Old: uint32_t regval = timestamp;
//                           if( irq_en ) regval |= 0xC00000;
//                           else         regval &= 0x3FFFFF;
//                      New: uint32_t regval = timestamp | 0x800000;
//                           if( irq_en ) regval |= 0xC00000;
//                           else         regval &= 0xBFFFFF;
//  Apr 28 2021 - PRCv22 Family
//*******************************************************************

#include "PRCv22G.h"
#include "mbus.h"

//*******************************************************************
// OTHER FUNCTIONS
//*******************************************************************

void delay(unsigned ticks){
  unsigned i;
  for (i=0; i < ticks; i++)
    asm("nop;");
}

void WFI(){
  asm("wfi;");
}

void config_timer16(uint32_t cmp0, uint32_t cmp1, uint8_t irq_en, uint32_t cnt, uint32_t status){
	*TIMER16_GO    = 0x0;
	*TIMER16_CMP0  = cmp0;
	*TIMER16_CMP1  = cmp1;
	*TIMER16_IRQEN = irq_en;
	*TIMER16_CNT   = cnt;
	*TIMER16_STAT  = status;
	*TIMER16_GO    = 0x1;
}

void config_timer32(uint32_t cmp, uint8_t roi, uint32_t cnt, uint32_t status){
	*TIMER32_GO   = 0x0;
	*TIMER32_CMP  = cmp;
	*TIMER32_ROI  = roi;
	*TIMER32_CNT  = cnt;
	*TIMER32_STAT = status;
	*TIMER32_GO   = 0x1;
}

void config_timerwd(uint32_t cnt){
	*TIMERWD_GO  = 0x0;
	*TIMERWD_CNT = cnt;
	*TIMERWD_GO  = 0x1;
}

void disable_timerwd(){
	*TIMERWD_GO  = 0x0;
}

void set_wakeup_timer( uint32_t timestamp, uint8_t irq_en, uint8_t reset ){
    uint32_t regval = timestamp | 0x800000; // WUP Timer Enable
    if( irq_en ) regval |= 0xC00000;
    else         regval &= 0xBFFFFF;
    *REG_WUPT_CONFIG = regval;

	if( reset ) *WUPT_RESET = 0x01;
}


//**************************************************
// M0 IRQ SETTING
//**************************************************
void enable_all_irq() { *NVIC_ICPR = 0xFFFFFFFF; *NVIC_ISER = 0xFFFFFFFF; }
void disable_all_irq() { *NVIC_ICPR = 0xFFFFFFFF; *NVIC_ICER = 0xFFFFFFFF; }
void clear_all_pend_irq() { *NVIC_ICPR = 0xFFFFFFFF; }

//**************************************************
// PRC/PRE FLAGS Register
//**************************************************
uint32_t set_flag ( uint32_t bit_idx, uint32_t value ) {
    uint32_t reg_val = (*REG_FLAGS & (~(0x1 << bit_idx))) | (value << bit_idx);
    *REG_FLAGS = reg_val;
    return reg_val;
}
    
uint8_t get_flag ( uint32_t bit_idx ) {
    uint8_t reg_val = (*REG_FLAGS & (0x1 << bit_idx)) >> bit_idx;
    return reg_val;
}
    
uint32_t set_mb_flag ( uint32_t lsb_idx, uint32_t num_bits, uint32_t value ) {
    uint32_t pattern = (((0xFFFFFFFF << (32 - (lsb_idx + num_bits))) >> (32 - (lsb_idx + num_bits))) >> lsb_idx) << lsb_idx;
    uint32_t reg_val = (*REG_FLAGS & (~pattern)) | (value << lsb_idx);
    *REG_FLAGS = reg_val;
    return reg_val;
}
    
uint32_t get_mb_flag ( uint32_t lsb_idx, uint32_t num_bits ) {
    uint32_t pattern = (((0xFFFFFFFF << (32 - (lsb_idx + num_bits))) >> (32 - (lsb_idx + num_bits))) >> lsb_idx) << lsb_idx;
    uint32_t reg_val = (*REG_FLAGS & pattern) >> lsb_idx;
    return reg_val;
}
    

//**************************************************
// MBUS IRQ SETTING
//**************************************************
void set_halt_until_reg(uint32_t reg_id) { *SREG_CONF_HALT = reg_id; }
void set_halt_until_mem_wr(void) { *SREG_CONF_HALT = HALT_UNTIL_MEM_WR; }
void set_halt_until_mbus_rx(void) { *SREG_CONF_HALT = HALT_UNTIL_MBUS_RX; }
void set_halt_until_mbus_tx(void) { *SREG_CONF_HALT = HALT_UNTIL_MBUS_TX; }
void set_halt_until_mbus_trx(void) { *SREG_CONF_HALT = HALT_UNTIL_MBUS_TRX; }
void set_halt_until_mbus_fwd(void) { *SREG_CONF_HALT = HALT_UNTIL_MBUS_FWD; }
void set_halt_disable(void) { *SREG_CONF_HALT = HALT_DISABLE; }
void set_halt_config(uint8_t new_config) { *SREG_CONF_HALT = new_config; }
uint8_t get_current_halt_config(void) { return (uint8_t) *SREG_CONF_HALT; }
void halt_cpu (void) { *SCTR_REG_HALT_ADDR = SCTR_CMD_HALT_CPU; }

//**************************************************
// AES
//**************************************************
void set_aes_key (uint32_t key[]) {
    *AES_KEY_0 = key[0];
    *AES_KEY_1 = key[1];
    *AES_KEY_2 = key[2];
    *AES_KEY_3 = key[3];
}

void set_aes_pt (uint32_t pt[]) {
    *AES_TEXT_0 = pt[0];
    *AES_TEXT_1 = pt[1];
    *AES_TEXT_2 = pt[2];
    *AES_TEXT_3 = pt[3];
}

//*******************************************************************
// VERIOLG SIM DEBUG PURPOSE ONLY!!
//*******************************************************************
void arb_debug_reg (uint8_t id, uint32_t code) { 
    uint32_t temp_addr = 0xBFFF0000 | (id << 2);
    *((volatile uint32_t *) temp_addr) = code;
}
