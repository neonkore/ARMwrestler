//*******************************************************************
//Author: Yejoong Kim
//Description: Developed during PREv22E tape-out for verification
//*******************************************************************
#include "PREv22E.h"
#include "FLPv3S.h"
#include "FLPv3S_RF.h"
#include "SNTv5_RF.h"
#include "RDCv4_RF.h"
#include "MRRv11A_RF.h"
#include "SRRv8_RF.h"
#include "PMUv12_RF.h"
#include "mbus.h"

#define PRE_ADDR    0x1
#define FLP_ADDR    0x3
#define NODE_A_ADDR 0x4
#define SNT_ADDR    0x5
#define RDC_ADDR    0x6
#define MRR_ADDR    0x7
#define SRR_ADDR    0x8
#define PMU_ADDR    0x9

// FLPv3S Payloads
#define ERASE_PASS  0x4F

// Flag Idx
#define FLAG_ENUM           0
#define FLAG_GPIO_SUB_0     1
#define FLAG_GPIO_SUB_1     2
#define FLAG_XO_SUB_0       3
#define FLAG_XO_SUB_1       4
#define FLAG_XO_SUB_2       5
#define FLAG_SOFT_RESET     6
#define FLAG_GOCEP_SUB      7
#define FLAG_SNTWUP_SUB_0   8
#define FLAG_SNTWUP_SUB_1   9
#define FLAG_SNTWUP_RD_0    10
#define FLAG_SNTWUP_RD_1    11

//********************************************************************
// Global Variables
//********************************************************************
volatile uint32_t cyc_num;
volatile uint32_t irq_history;

volatile uint32_t mem_rsvd_0[10];
volatile uint32_t mem_rsvd_1[10];


volatile flpv3s_r0F_t FLPv3S_REG_IRQ     = FLPv3S_R0F_DEFAULT;
volatile flpv3s_r12_t FLPv3S_REG_PWR_CNF = FLPv3S_R12_DEFAULT;
volatile flpv3s_r09_t FLPv3S_REG_GO      = FLPv3S_R09_DEFAULT;

volatile pmuv12_r51_t PMUv12_REG_CONF = PMUv12_R51_DEFAULT;
volatile pmuv12_r52_t PMUv12_REG_IRQ  = PMUv12_R52_DEFAULT;

volatile sntv5_r00_t SNTv5_REG_LDO        = SNTv5_R00_DEFAULT;
volatile sntv5_r01_t SNTv5_REG_TSNS       = SNTv5_R01_DEFAULT;
volatile sntv5_r07_t SNTv5_REG_TSNS_IRQ   = SNTv5_R07_DEFAULT;
volatile sntv5_r08_t SNTv5_REG_TMR_CNF    = SNTv5_R08_DEFAULT;
volatile sntv5_r17_t SNTv5_REG_WUP_CNF    = SNTv5_R17_DEFAULT;
volatile sntv5_r18_t SNTv5_REG_WUP_PYLD   = SNTv5_R18_DEFAULT;
volatile sntv5_r19_t SNTv5_REG_WUP_THRESE = SNTv5_R19_DEFAULT;
volatile sntv5_r1A_t SNTv5_REG_WUP_THRES  = SNTv5_R1A_DEFAULT;


// Select Testing
#ifdef PREv22E
volatile uint32_t do_cycle0  = 1; // Wakeup through GPIO (only for PRE)
volatile uint32_t do_cycle1  = 1; // Wakeup through XO (only for PRE)
#else
volatile uint32_t do_cycle0  = 0;
volatile uint32_t do_cycle1  = 0;
#endif
volatile uint32_t do_cycle2  = 1; // Soft-Reset
volatile uint32_t do_cycle3  = 1; // GOC in Active
volatile uint32_t do_cycle4  = 1; // GOC in Sleep
volatile uint32_t do_cycle5  = 1; // GOC Write Access
volatile uint32_t do_cycle6  = 1; // GOC Read Access
volatile uint32_t do_cycle7  = 1; // SNTv5 Wakeup Timer
volatile uint32_t do_cycle8  = 1; // SNTv5 Wakeup Counter Read-Out
volatile uint32_t do_cycle9  = 0; // 
volatile uint32_t do_cycle10 = 0; // 
volatile uint32_t do_cycle11 = 1; // Watch-Dog Timer

//*******************************************************************
// INTERRUPT HANDLERS
//*******************************************************************
void handler_ext_int_wakeup   (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_softreset(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_gocep    (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_timer32  (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_timer16  (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_mbustx   (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_mbusrx   (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_mbusfwd  (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_reg0     (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_reg1     (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_reg2     (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_reg3     (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_reg4     (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_reg5     (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_reg6     (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_reg7     (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_mbusmem  (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_gpio     (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_spi      (void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_xot      (void) __attribute__ ((interrupt ("IRQ")));

void handler_ext_int_timer32(void) { // TIMER32
    *NVIC_ICPR = (0x1 << IRQ_TIMER32); irq_history |= (0x1 << IRQ_TIMER32);
    *REG1 = *TIMER32_CNT;
    *REG2 = *TIMER32_STAT;
    *TIMER32_STAT = 0x0;
    arb_debug_reg(IRQ_TIMER32, 0x00000000);
    arb_debug_reg(IRQ_TIMER32, (0x10 << 24) | *REG1); // TIMER32_CNT
    arb_debug_reg(IRQ_TIMER32, (0x20 << 24) | *REG2); // TIMER32_STAT
    }
void handler_ext_int_timer16(void) { // TIMER16
    *NVIC_ICPR = (0x1 << IRQ_TIMER16); irq_history |= (0x1 << IRQ_TIMER16);
    *REG1 = *TIMER16_CNT;
    *REG2 = *TIMER16_STAT;
    *TIMER16_STAT = 0x0;
    arb_debug_reg(IRQ_TIMER16, 0x00000000);
    arb_debug_reg(IRQ_TIMER16, (0x10 << 24) | *REG1); // TIMER16_CNT
    arb_debug_reg(IRQ_TIMER16, (0x20 << 24) | *REG2); // TIMER16_STAT
    }
void handler_ext_int_reg0(void) { // REG0
    *NVIC_ICPR = (0x1 << IRQ_REG0); irq_history |= (0x1 << IRQ_REG0);
    arb_debug_reg(IRQ_REG0, 0x00000000);
}
void handler_ext_int_reg1(void) { // REG1
    *NVIC_ICPR = (0x1 << IRQ_REG1); irq_history |= (0x1 << IRQ_REG1);
    arb_debug_reg(IRQ_REG1, 0x00000000);
}
void handler_ext_int_reg2(void) { // REG2
    *NVIC_ICPR = (0x1 << IRQ_REG2); irq_history |= (0x1 << IRQ_REG2);
    arb_debug_reg(IRQ_REG2, 0x00000000);
}
void handler_ext_int_reg3(void) { // REG3
    *NVIC_ICPR = (0x1 << IRQ_REG3); irq_history |= (0x1 << IRQ_REG3);
    arb_debug_reg(IRQ_REG3, 0x00000000);
}
void handler_ext_int_reg4(void) { // REG4
    *NVIC_ICPR = (0x1 << IRQ_REG4); irq_history |= (0x1 << IRQ_REG4);
    arb_debug_reg(IRQ_REG4, 0x00000000);
}
void handler_ext_int_reg5(void) { // REG5
    *NVIC_ICPR = (0x1 << IRQ_REG5); irq_history |= (0x1 << IRQ_REG5);
    arb_debug_reg(IRQ_REG5, 0x00000000);
}
void handler_ext_int_reg6(void) { // REG6
    *NVIC_ICPR = (0x1 << IRQ_REG6); irq_history |= (0x1 << IRQ_REG6);
    arb_debug_reg(IRQ_REG6, 0x00000000);
}
void handler_ext_int_reg7(void) { // REG7
    *NVIC_ICPR = (0x1 << IRQ_REG7); irq_history |= (0x1 << IRQ_REG7);
    arb_debug_reg(IRQ_REG7, 0x00000000);
}
void handler_ext_int_mbusmem(void) { // MBUS_MEM_WR
    *NVIC_ICPR = (0x1 << IRQ_MBUS_MEM); irq_history |= (0x1 << IRQ_MBUS_MEM);
    arb_debug_reg(IRQ_MBUS_MEM, 0x00000000);
}
void handler_ext_int_mbusrx(void) { // MBUS_RX
    *NVIC_ICPR = (0x1 << IRQ_MBUS_RX); irq_history |= (0x1 << IRQ_MBUS_RX);
    arb_debug_reg(IRQ_MBUS_RX, 0x00000000);
}
void handler_ext_int_mbustx(void) { // MBUS_TX
    *NVIC_ICPR = (0x1 << IRQ_MBUS_TX); irq_history |= (0x1 << IRQ_MBUS_TX);
    arb_debug_reg(IRQ_MBUS_TX, 0x00000000);
}
void handler_ext_int_mbusfwd(void) { // MBUS_FWD
    *NVIC_ICPR = (0x1 << IRQ_MBUS_FWD); irq_history |= (0x1 << IRQ_MBUS_FWD);
    arb_debug_reg(IRQ_MBUS_FWD, 0x00000000);
}
void handler_ext_int_gocep(void) { // GOCEP
    *NVIC_ICPR = (0x1 << IRQ_GOCEP); irq_history |= (0x1 << IRQ_GOCEP);
    arb_debug_reg(IRQ_GOCEP, 0x00000000);
}
void handler_ext_int_softreset(void) { // SOFT_RESET
    *NVIC_ICPR = (0x1 << IRQ_SOFT_RESET); irq_history |= (0x1 << IRQ_SOFT_RESET);
    arb_debug_reg(IRQ_SOFT_RESET, 0x00000000);
}
void handler_ext_int_wakeup(void) { // WAKE-UP
    *NVIC_ICPR = (0x1 << IRQ_WAKEUP); irq_history |= (0x1 << IRQ_WAKEUP);
    arb_debug_reg(IRQ_WAKEUP, (0x10 << 24) | *SREG_WAKEUP_SOURCE);
    *SREG_WAKEUP_SOURCE = 0;
}
void handler_ext_int_spi(void) { // SPI
    *NVIC_ICPR = (0x1 << IRQ_SPI); irq_history |= (0x1 << IRQ_SPI);
    arb_debug_reg(IRQ_SPI, 0x00000000);
}
void handler_ext_int_gpio(void) { // GPIO
    *NVIC_ICPR = (0x1 << IRQ_GPIO); irq_history |= (0x1 << IRQ_GPIO);
    arb_debug_reg(IRQ_GPIO, 0x00000000);
}
void handler_ext_int_xot(void) { // XOT
    *NVIC_ICPR = (0x1 << IRQ_XOT); irq_history |= (0x1 << IRQ_XOT);
    mbus_write_message32(0xEE, 0xEEEEEEEE);
    arb_debug_reg(IRQ_XOT, 0x00000000);
}

//*******************************************************************
// USER FUNCTIONS
//*******************************************************************
void initialization (void) {

    set_flag(FLAG_ENUM, 1);
    cyc_num = 0;
    irq_history = 0;

    // Set Halt
    set_halt_until_mbus_trx();

    // Enumeration
    mbus_enumerate(FLP_ADDR);
    mbus_enumerate(NODE_A_ADDR);
    mbus_enumerate(SNT_ADDR);
    mbus_enumerate(RDC_ADDR);
    mbus_enumerate(MRR_ADDR);
    mbus_enumerate(SRR_ADDR);
    mbus_enumerate(PMU_ADDR);

    //Set Halt
    set_halt_until_mbus_tx();
}

void pass (uint32_t id, uint32_t data) {
    arb_debug_reg(0xE0, 0x0);
    arb_debug_reg(0xE1, id);
    arb_debug_reg(0xE2, data);
}

void fail (uint32_t id, uint32_t data) {
    arb_debug_reg(0xE8, 0x0);
    arb_debug_reg(0xE9, id);
    arb_debug_reg(0xEA, data);
    *REG_CHIP_ID = 0xFFFFFF; // This will stop the verilog sim.
}

void cycle0 (void) {
    if (do_cycle0 == 1) {
    #ifdef PREv22E
        arb_debug_reg(0x40, 0x00000000);

        //----------------------------------------------------------------------------------------------------------    
        // Posedge WIRQ
        if ((get_flag(FLAG_GPIO_SUB_1)==0) & (get_flag(FLAG_GPIO_SUB_0)==0)) {

            set_flag(FLAG_GPIO_SUB_1, 0);
            set_flag(FLAG_GPIO_SUB_0, 1);

            // Configure GPIO
            gpio_init(0xF0); // [7:4] Output, [3:0] Input
            config_gpio_posedge_wirq (0xF);
            config_gpio_negedge_wirq (0x0);
            freeze_gpio_out();

            arb_debug_reg(0x40, 0x00000001);
            mbus_sleep_all();
        }
        //----------------------------------------------------------------------------------------------------------    
        // Negedge WIRQ
        else if ((get_flag(FLAG_GPIO_SUB_1)==0) & (get_flag(FLAG_GPIO_SUB_0)==1)) {

            set_flag(FLAG_GPIO_SUB_1, 1);
            set_flag(FLAG_GPIO_SUB_0, 0);

            // Configure GPIO
            gpio_init(0xF0); // [7:4] Output, [3:0] Input
            config_gpio_posedge_wirq (0x0);
            config_gpio_negedge_wirq (0xF);
            freeze_gpio_out();

            arb_debug_reg(0x40, 0x00000002);
            mbus_sleep_all();
        }
        //----------------------------------------------------------------------------------------------------------    
        // No Edge Enabled
        else if ((get_flag(FLAG_GPIO_SUB_1)==1) & (get_flag(FLAG_GPIO_SUB_0)==0)) {

            set_flag(FLAG_GPIO_SUB_1, 1);
            set_flag(FLAG_GPIO_SUB_0, 1);

            // Configure GPIO
            gpio_init(0xF0); // [7:4] Output, [3:0] Input
            config_gpio_posedge_wirq (0x0);
            config_gpio_negedge_wirq (0x0);
            freeze_gpio_out();

            arb_debug_reg(0x40, 0x00000003);
            set_wakeup_timer(100, 1, 1);
            mbus_sleep_all();
        }
        //----------------------------------------------------------------------------------------------------------    
        // End of Testing
        else if ((get_flag(FLAG_GPIO_SUB_1)==1) & (get_flag(FLAG_GPIO_SUB_0)==1)) {
            arb_debug_reg(0x40, 0x00000004);
        }

    #endif
    }
}

void cycle1 (void) {
    if (do_cycle1 == 1) {
    #ifdef PREv22E
        arb_debug_reg(0x41, 0x00000000);

        //----------------------------------------------------------------------------------------------------------    
        // 0: XO Timer: Normal Use as a Sleep Timer
        if (!get_flag(FLAG_XO_SUB_2) && !get_flag(FLAG_XO_SUB_1) && !get_flag(FLAG_XO_SUB_0)) {

            set_flag(FLAG_XO_SUB_2, 0);
            set_flag(FLAG_XO_SUB_1, 0);
            set_flag(FLAG_XO_SUB_0, 1);

            // Configure XO
            // In reality, the XO driver must be properly configured and started
            arb_debug_reg(0x41, 0x00000001);
          //*REG_XO_CONF1 = 0x242123; // Default Value
            *REG_XO_CONF1 = 0x242133; // XO_RESETn = 1
            *REG_XO_CONF1 = 0x242132; // XO_ISOL_CLK_LP = 0

            set_wakeup_timer (100, 0, 1);
            set_xo_timer (0, 100, 1, 0);
            start_xo_cnt();
            mbus_sleep_all();
        }
        //----------------------------------------------------------------------------------------------------------    
        // 1: XO Timer: Free running XO CLK OUT in Active
        else if (!get_flag(FLAG_XO_SUB_2) && !get_flag(FLAG_XO_SUB_1) && get_flag(FLAG_XO_SUB_0)) {

            disable_xo_timer();

            set_flag(FLAG_XO_SUB_2, 0);
            set_flag(FLAG_XO_SUB_1, 1);
            set_flag(FLAG_XO_SUB_0, 0);

            // Output XO Clock
            arb_debug_reg(0x41, 0x00000002);
            enable_xo_timer();
            start_xo_cout();
            delay(1000);
            stop_xo_cout();

            // Go to Sleep
            set_xo_timer (0, 20, 1, 0);
            start_xo_cnt();
            mbus_sleep_all();
        }
        //----------------------------------------------------------------------------------------------------------    
        // 2: XO Timer: Free running XO CLK OUT in Active & Sleep - Part I
        else if (!get_flag(FLAG_XO_SUB_2) && get_flag(FLAG_XO_SUB_1) && !get_flag(FLAG_XO_SUB_0)) {

            disable_xo_timer();

            set_flag(FLAG_XO_SUB_2, 0);
            set_flag(FLAG_XO_SUB_1, 1);
            set_flag(FLAG_XO_SUB_0, 1);

            // Output XO Clock
            arb_debug_reg(0x41, 0x00000003);
            enable_xo_timer();
            start_xo_cout();

            // Go to Sleep
            set_xo_timer (0, 20, 1, 0);
            start_xo_cnt();
            mbus_sleep_all();
        }
        //----------------------------------------------------------------------------------------------------------    
        // 3: XO Timer: Free running XO CLK OUT in Active & Sleep - Part II
        else if (!get_flag(FLAG_XO_SUB_2) && get_flag(FLAG_XO_SUB_1) && get_flag(FLAG_XO_SUB_0)) {

            set_flag(FLAG_XO_SUB_2, 1);
            set_flag(FLAG_XO_SUB_1, 0);
            set_flag(FLAG_XO_SUB_0, 0);

            // Stop XO Clock
            arb_debug_reg(0x41, 0x00000004);
            stop_xo_cout();

            // Go to Sleep
            set_xo_timer (0, 20, 1, 0);
            start_xo_cnt();
            mbus_sleep_all();
        }
        //----------------------------------------------------------------------------------------------------------    
        // 4: XO Timer: Masking in Active
        else if (get_flag(FLAG_XO_SUB_2) && !get_flag(FLAG_XO_SUB_1) && !get_flag(FLAG_XO_SUB_0)) {

            disable_xo_timer();

            set_flag(FLAG_XO_SUB_2, 1);
            set_flag(FLAG_XO_SUB_1, 0);
            set_flag(FLAG_XO_SUB_0, 1);

            // Output XO Clock
            arb_debug_reg(0x41, 0x00000005);
            set_xo_timer (1, 9, 0, 1);
            arb_debug_reg(0x41, 0x00000006);
            start_xo_cout();
            delay(50);
            arb_debug_reg(0x41, 0x00000007);
            stop_xo_cout();
            WFI();
            delay(50);
            arb_debug_reg(0x41, 0x00000007);
            stop_xo_cout();
            WFI();
            delay(50);

            // Change the mode in order to get out of the Masking Mode
            arb_debug_reg(0x41, 0x00000008);
            set_xo_timer (0, 9, 0, 0);
            stop_xo_cout();

            // Go to Sleep
            set_xo_timer (0, 20, 1, 0);
            start_xo_cnt();
            mbus_sleep_all();
        }
        //----------------------------------------------------------------------------------------------------------    
        // 5: XO Timer: Masking with Sleep Mode
        else if (get_flag(FLAG_XO_SUB_2) && !get_flag(FLAG_XO_SUB_1) && get_flag(FLAG_XO_SUB_0)) {

            disable_xo_timer();

            set_flag(FLAG_XO_SUB_2, 1);
            set_flag(FLAG_XO_SUB_1, 1);
            set_flag(FLAG_XO_SUB_0, 0);

            // Send Mbus message, stop XO CLK output, go to sleep
            arb_debug_reg(0x41, 0x00000009);
            set_xo_timer (1, 49, 1, 1);
            start_xo_cout();
            delay(50);
            mbus_write_message32(0xDD, 0xDDDDDDDD);
            stop_xo_cout();
            mbus_sleep_all();
        }
        //----------------------------------------------------------------------------------------------------------    
        // 6: XO Timer: Waked up with the Masking
        else if (get_flag(FLAG_XO_SUB_2) && get_flag(FLAG_XO_SUB_1) && !get_flag(FLAG_XO_SUB_0)) {

            set_flag(FLAG_XO_SUB_2, 1);
            set_flag(FLAG_XO_SUB_1, 1);
            set_flag(FLAG_XO_SUB_0, 1);

            // Change the mode in order to get out of the Masking Mode
            arb_debug_reg(0x41, 0x00000008);
            set_xo_timer (0, 9, 0, 0);
            stop_xo_cout();

            // Go to Sleep
            set_xo_timer (0, 20, 1, 0);
            start_xo_cnt();
            mbus_sleep_all();
        }
        //----------------------------------------------------------------------------------------------------------    
        // 7: XO Timer: Use it as a normal timer (End of Testing)
        else {

            disable_xo_timer();

            arb_debug_reg(0x41, 0x0000000A);
            set_xo_timer (0, 100, 0, 1);

            start_xo_cnt();
            WFI();

            arb_debug_reg(0x41, 0x0000000B);
            start_xo_cnt();
            delay(20);

            reset_xo_cnt();
            WFI();

          //*REG_XO_CONF1 = 0x242123; // Power-gate and Isolate the XO driver (Default value)

            disable_xo_timer();

            arb_debug_reg(0x41, 0x0000000F);
        }

    #endif
    }
}

void cycle2 (void) {
    if (do_cycle2 == 1) {
        arb_debug_reg(0x42, 0x00000000);

        //----------------------------------------------------------------------------------------------------------    
        // Copy from Flash and triggers Soft Reset
        if (!get_flag(FLAG_SOFT_RESET)) {

            set_flag(FLAG_SOFT_RESET, 1);

            // Below value is the default value
            *REG_SYS_CONF =   (0 << 8)  // ENABLE_SOFT_RESET
                            | (1 << 7)  // PUF_CHIP_ID_SLEEP
                            | (1 << 6)  // PUF_CHIP_ID_ISOLATE
                            | (1 << 4)  // WAKEUP_ON_PEND_REQ (GIT)
                            | (1 << 3)  // WAKEUP_ON_PEND_REQ (GPIO)
                            | (1 << 2)  // WAKEUP_ON_PEND_REQ (XO TIMER)
                            | (1 << 1)  // WAKEUP_ON_PEND_REQ (WUP TIMER)
                            | (0 << 0); // WAKEUP_ON_PEND_REQ (GOC/EP)

            arb_debug_reg(0x42, 0x00000001);
            mbus_copy_mem_from_local_to_remote_bulk (FLP_ADDR, 0x0, 0x0, 15);

            set_halt_until_mbus_trx();
            arb_debug_reg(0x42, 0x00000002);
            mbus_copy_mem_from_remote_to_any_bulk (FLP_ADDR, 0x0, PRE_ADDR, (uint32_t *) 0x6C840000, 15);

            set_halt_until_mbus_tx();

            *REG_SYS_CONF =   (1 << 8)  // ENABLE_SOFT_RESET
                            | (1 << 7)  // PUF_CHIP_ID_SLEEP
                            | (1 << 6)  // PUF_CHIP_ID_ISOLATE
                            | (1 << 4)  // WAKEUP_ON_PEND_REQ (GIT)
                            | (1 << 3)  // WAKEUP_ON_PEND_REQ (GPIO)
                            | (1 << 2)  // WAKEUP_ON_PEND_REQ (XO TIMER)
                            | (1 << 1)  // WAKEUP_ON_PEND_REQ (WUP TIMER)
                            | (0 << 0); // WAKEUP_ON_PEND_REQ (GOC/EP)

            set_halt_until_mbus_trx();
            arb_debug_reg(0x42, 0x00000003);
            mbus_copy_mem_from_remote_to_any_bulk (FLP_ADDR, 0x0, PRE_ADDR, (uint32_t *) 0x6C840000, 15);

            set_halt_until_mbus_tx();
        }
        //----------------------------------------------------------------------------------------------------------    
        // End of Testing
        else {

            *REG_SYS_CONF =   (0 << 8)  // ENABLE_SOFT_RESET
                            | (1 << 7)  // PUF_CHIP_ID_SLEEP
                            | (1 << 6)  // PUF_CHIP_ID_ISOLATE
                            | (1 << 4)  // WAKEUP_ON_PEND_REQ (GIT)
                            | (1 << 3)  // WAKEUP_ON_PEND_REQ (GPIO)
                            | (1 << 2)  // WAKEUP_ON_PEND_REQ (XO TIMER)
                            | (1 << 1)  // WAKEUP_ON_PEND_REQ (WUP TIMER)
                            | (0 << 0); // WAKEUP_ON_PEND_REQ (GOC/EP)

            if (irq_history == (0x1 << IRQ_SOFT_RESET)) {
                   pass (0x0, irq_history); irq_history = 0; disable_all_irq(); }
            else { fail (0x0, irq_history); disable_all_irq(); }

            arb_debug_reg(0x42, 0x00000004);
        }
    }
}

void cycle3 (void) {
    if (do_cycle3 == 1) {
        arb_debug_reg(0x43, 0x00000000);
        *REG0 = 0;
        while (*REG0==0);
        disable_all_irq();
    }
}

void cycle4 (void) {
    if (do_cycle2 == 1) {
        arb_debug_reg(0x44, 0x00000000);

        //----------------------------------------------------------------------------------------------------------    
        // Go into indefinite sleep
        if (!get_flag(FLAG_GOCEP_SUB)) {

            set_flag(FLAG_GOCEP_SUB, 1);

            arb_debug_reg(0x44, 0x00000001);
            set_wakeup_timer(5, 0, 1);
            mbus_sleep_all();
        }
        //----------------------------------------------------------------------------------------------------------    
        // After wakeup by GOCEP
        else {
            if (irq_history == ((0x1 << IRQ_WAKEUP) | (0x1 << IRQ_GOCEP))) {
                   pass (0x1, irq_history); irq_history = 0; disable_all_irq(); }
            else { fail (0x1, irq_history); disable_all_irq(); }

            arb_debug_reg(0x44, 0x00000002);
        }
    }
}

void cycle5 (void) { 
    if (do_cycle5 == 1) { 
        arb_debug_reg(0x45, 0x00000000);

        if (*REG0 != 0xDEAD) {
            arb_debug_reg(0x45, 0x00000001);
            set_wakeup_timer(100, 0, 1);
            mbus_sleep_all();
        }
        else {
            *REG0 = 0x123456;
            arb_debug_reg(0x45, 0x00000002);
        }
    } 
}

void cycle6 (void) { 
    if (do_cycle6 == 1) { 
        arb_debug_reg(0x46, 0x00000000);

        if (*REG0 != 0xDEAD) {
            arb_debug_reg(0x46, 0x00000001);
            set_wakeup_timer(100, 0, 1);
            mbus_sleep_all();
        }
        else {
            *REG0 = 0;
            arb_debug_reg(0x46, 0x00000002);
        }
    } 
}

// SNTv5 Wakeup Timer
void cycle7 (void) { 
    if (do_cycle7 == 1) { 
        arb_debug_reg(0x47, 0x00000000);
        if ((!get_flag(FLAG_SNTWUP_SUB_1)) & (!get_flag(FLAG_SNTWUP_SUB_0))) {

            arb_debug_reg(0x47, 0x00000001);

            set_flag(FLAG_SNTWUP_SUB_1, 0);
            set_flag(FLAG_SNTWUP_SUB_0, 1);

            // Configure LDO
            SNTv5_REG_LDO.LDO_EN_IREF = 0x1;
            SNTv5_REG_LDO.LDO_EN_VREF = 0x1;
            mbus_remote_register_write(SNT_ADDR, 0x00, SNTv5_REG_LDO.as_int);
            
            SNTv5_REG_LDO.LDO_EN_LDO = 0x1;
            mbus_remote_register_write(SNT_ADDR, 0x00, SNTv5_REG_LDO.as_int);

            // Configure Temp Sensor
            SNTv5_REG_TSNS.TSNS_SEL_LDO = 0x1;         // Turn on Digital
            SNTv5_REG_TSNS.TSNS_EN_SENSOR_LDO = 0x1;   // Turn on Analog
            SNTv5_REG_TSNS.TSNS_EN_CLK_REF = 0x1;      // Enable Clock Reference
            mbus_remote_register_write(SNT_ADDR, 0x01, SNTv5_REG_TSNS.as_int);

            SNTv5_REG_TSNS.TSNS_ISOLATE = 0x0;     // Release Isolation
            mbus_remote_register_write(SNT_ADDR, 0x01, SNTv5_REG_TSNS.as_int);

            SNTv5_REG_TSNS.TSNS_RESETn = 0x1;     // Start Reference Clock
            mbus_remote_register_write(SNT_ADDR, 0x01, SNTv5_REG_TSNS.as_int);

            // Configure Wakeup Timer
            SNTv5_REG_WUP_CNF.WUP_INT_RPLY_REG_ADDR   = 0x07;
            SNTv5_REG_WUP_CNF.WUP_INT_RPLY_SHORT_ADDR = 0x10;
            SNTv5_REG_WUP_CNF.WUP_CLK_SEL = 0x1;    // Use the clock from the temperature sensor
            SNTv5_REG_WUP_CNF.WUP_AUTO_RESET = 0x1;
            SNTv5_REG_WUP_CNF.WUP_IRQ_EN = 0x1;
            SNTv5_REG_WUP_CNF.WUP_ENABLE = 0x1;
            mbus_remote_register_write(SNT_ADDR, 0x17, SNTv5_REG_WUP_CNF.as_int);

            SNTv5_REG_WUP_THRES.WUP_THRESHOLD = 1000;
            mbus_remote_register_write(SNT_ADDR, 0x1A, SNTv5_REG_WUP_THRES.as_int);

            arb_debug_reg(0x47, 0x000000FF);

            // Go to Sleep
            set_wakeup_timer(100, 0, 1);
            mbus_sleep_all();
        }
        else if ((!get_flag(FLAG_SNTWUP_SUB_1)) & get_flag(FLAG_SNTWUP_SUB_0)) {

            arb_debug_reg(0x47, 0x00000002);

            set_flag(FLAG_SNTWUP_SUB_1, 1);
            set_flag(FLAG_SNTWUP_SUB_0, 0);

            // Configure Wakeup Timer
            SNTv5_REG_WUP_THRES.WUP_THRESHOLD = 5000;
            mbus_remote_register_write(SNT_ADDR, 0x1A, SNTv5_REG_WUP_THRES.as_int);

            arb_debug_reg(0x47, 0x000000FF);

            // Go to Sleep
            set_wakeup_timer(100, 0, 1);
            mbus_sleep_all();
        }
        else if (get_flag(FLAG_SNTWUP_SUB_1) & (!get_flag(FLAG_SNTWUP_SUB_0))) {

            arb_debug_reg(0x47, 0x00000003);

            set_flag(FLAG_SNTWUP_SUB_1, 1);
            set_flag(FLAG_SNTWUP_SUB_0, 1);

            // Configure Wakeup Timer
            SNTv5_REG_WUP_THRES.WUP_THRESHOLD = 1000;
            mbus_remote_register_write(SNT_ADDR, 0x1A, SNTv5_REG_WUP_THRES.as_int);

            SNTv5_REG_WUP_CNF.WUP_AUTO_RESET = 0x0;
            SNTv5_REG_WUP_CNF.WUP_ENABLE = 0x0;
            mbus_remote_register_write(SNT_ADDR, 0x17, SNTv5_REG_WUP_CNF.as_int);

            SNTv5_REG_WUP_CNF.WUP_ENABLE = 0x1;
            mbus_remote_register_write(SNT_ADDR, 0x17, SNTv5_REG_WUP_CNF.as_int);

            arb_debug_reg(0x47, 0x000000FF);

            // Go to Sleep
            set_wakeup_timer(100, 0, 1);
            mbus_sleep_all();
        }
        else if (get_flag(FLAG_SNTWUP_SUB_1) & get_flag(FLAG_SNTWUP_SUB_0)) {

            arb_debug_reg(0x47, 0x00000004);

            SNTv5_REG_WUP_CNF.WUP_ENABLE = 0x0;
            SNTv5_REG_WUP_CNF.WUP_AUTO_RESET = 0x1;
            mbus_remote_register_write(SNT_ADDR, 0x17, SNTv5_REG_WUP_CNF.as_int);
        }
    } 
}

void cycle8 (void) { 
    if (do_cycle8 == 1) { 
        arb_debug_reg(0x48, 0x00000000);

        if (!get_flag(FLAG_SNTWUP_RD_1) & !get_flag(FLAG_SNTWUP_RD_0)) {

            arb_debug_reg(0x48, 0x00000001);
            
            set_flag(FLAG_SNTWUP_RD_1, 0);
            set_flag(FLAG_SNTWUP_RD_0, 1);

            // Configure the 5kHz Timer
            SNTv5_REG_TMR_CNF.TMR_SLEEP        = 0;
            mbus_remote_register_write(SNT_ADDR, 0x08, SNTv5_REG_TMR_CNF.as_int);

            SNTv5_REG_TMR_CNF.TMR_ISOLATE      = 0;
            mbus_remote_register_write(SNT_ADDR, 0x08, SNTv5_REG_TMR_CNF.as_int);

            SNTv5_REG_TMR_CNF.TMR_RESETB_DCDC  = 1;
            SNTv5_REG_TMR_CNF.TMR_RESETB_DIV   = 1;
            SNTv5_REG_TMR_CNF.TMR_RESETB       = 1;
            mbus_remote_register_write(SNT_ADDR, 0x08, SNTv5_REG_TMR_CNF.as_int);

            SNTv5_REG_TMR_CNF.TMR_EN_OSC       = 1;
            mbus_remote_register_write(SNT_ADDR, 0x08, SNTv5_REG_TMR_CNF.as_int);

            SNTv5_REG_TMR_CNF.TMR_EN_SELF_CLK  = 1;
            mbus_remote_register_write(SNT_ADDR, 0x08, SNTv5_REG_TMR_CNF.as_int);

            SNTv5_REG_TMR_CNF.TMR_EN_OSC       = 0;
            mbus_remote_register_write(SNT_ADDR, 0x08, SNTv5_REG_TMR_CNF.as_int);

            // Final Status
            // SNTv5_REG_TMR_CNF.TMR_EN_SELF_CLK  = 1;
            // SNTv5_REG_TMR_CNF.TMR_RESETB_DCDC  = 1;
            // SNTv5_REG_TMR_CNF.TMR_RESETB_DIV   = 1;
            // SNTv5_REG_TMR_CNF.TMR_EN_OSC       = 0;
            // SNTv5_REG_TMR_CNF.TMR_RESETB       = 1;
            // SNTv5_REG_TMR_CNF.TMR_ISOLATE      = 0;
            // SNTv5_REG_TMR_CNF.TMR_SLEEP        = 0;


            // Configure the Wakeup Timer
            // NOTE: No IRQ or Wakeup Request; Just free-running in Sleep Mode
            SNTv5_REG_WUP_THRESE.WUP_THRESHOLD_EXT = 0x0;
            mbus_remote_register_write(SNT_ADDR, 0x19, SNTv5_REG_WUP_THRESE.as_int);

            SNTv5_REG_WUP_THRES.WUP_THRESHOLD = 0x0;
            mbus_remote_register_write(SNT_ADDR, 0x1A, SNTv5_REG_WUP_THRES.as_int);

            SNTv5_REG_WUP_CNF.WUP_INT_RPLY_REG_ADDR   = 0x07;
            SNTv5_REG_WUP_CNF.WUP_INT_RPLY_SHORT_ADDR = 0x10;
            SNTv5_REG_WUP_CNF.WUP_ENABLE_CLK_SLP_OUT  = 0;
            SNTv5_REG_WUP_CNF.WUP_CLK_SEL             = 0;
            SNTv5_REG_WUP_CNF.WUP_AUTO_RESET          = 0;
            SNTv5_REG_WUP_CNF.WUP_IRQ_EN              = 0;
            SNTv5_REG_WUP_CNF.WUP_ENABLE              = 1;
            mbus_remote_register_write(SNT_ADDR, 0x17, SNTv5_REG_WUP_CNF.as_int);

            // Go to Sleep
            set_wakeup_timer(100, 0, 1);
            mbus_sleep_all();
            while(1);
        }
        else if (!get_flag(FLAG_SNTWUP_RD_1) & get_flag(FLAG_SNTWUP_RD_0)) {

            // If REG0 = 0, Read from the SNT Wakeup Counter and sends it to PRC
            if (*REG0 == 0) {
                arb_debug_reg(0x48, 0x00000002);

                // Read from WUP Counter
                set_halt_until_mbus_trx();
                mbus_copy_registers_from_remote_to_local (SNT_ADDR, 0x1B, 0x06, 1);
                set_halt_until_mbus_tx();

                // Check the result
                arb_debug_reg(0x4C, (*REG6 << 24) | *REG7);

                // Go to Sleep
                set_wakeup_timer(100, 0, 1);
                mbus_sleep_all();
                while(1);
            }
            // If REG0 = 1, Read from the SNT Wakeup Counter using the synchronous interface
            //  Read Type = 0 (See SNTv5_WUP_TIMER_SYNC.v)
            else if (*REG0 == 1) {
                *REG0 = 0;
                arb_debug_reg(0x48, 0x00000003);

                // Read from WUP Counter
                set_halt_until_mbus_trx();
                mbus_write_message32 ((SNT_ADDR << 4), 
                                          (0x14 << 24)  // MM Register ID
                                        | (0 << 16)     // Read Type
                                        | (0x10 << 8)   // MBus Target Address
                                        | (0x06 << 0)   // Destination Register
                                    );
                set_halt_until_mbus_tx();

                // Check the result
                arb_debug_reg(0x4D, (*REG6 << 24) | *REG7);

                // Go to Sleep
                set_wakeup_timer(100, 0, 1);
                mbus_sleep_all();
                while(1);
            }
            // If REG0 = 2, Read from the SNT Wakeup Counter using the synchronous interface
            //  Read Type = 1 (See SNTv5_WUP_TIMER_SYNC.v)
            else if (*REG0 == 2) {
                *REG0 = 0;
                arb_debug_reg(0x48, 0x00000004);

                // Read from WUP Counter
                set_halt_until_mbus_trx();
                mbus_write_message32 ((SNT_ADDR << 4), 
                                          (0x14 << 24)  // MM Register ID
                                        | (1 << 16)     // Read Type
                                        | (0x10 << 8)   // MBus Target Address
                                        | (0x06 << 0)   // Destination Register
                                    );
                set_halt_until_mbus_tx();

                // Check the result
                arb_debug_reg(0x4E, *REG6);

                // Go to Sleep
                set_wakeup_timer(100, 0, 1);
                mbus_sleep_all();
                while(1);
            }
            // If REG0 = 3, Read from the SNT Wakeup Counter using the synchronous interface
            //  Read Type = 2 (See SNTv5_WUP_TIMER_SYNC.v)
            else if (*REG0 == 3) {
                *REG0 = 0;
                arb_debug_reg(0x48, 0x00000005);

                // Read from WUP Counter
                set_halt_until_mbus_trx();
                mbus_write_message32 ((SNT_ADDR << 4), 
                                          (0x14 << 24)  // MM Register ID
                                        | (2 << 16)     // Read Type
                                        | (0x10 << 8)   // MBus Target Address
                                        | (0x06 << 0)   // Destination Register
                                    );
                set_halt_until_mbus_tx();

                // Check the result
                arb_debug_reg(0x4F, *REG6);

                // Go to Sleep
                set_wakeup_timer(100, 0, 1);
                mbus_sleep_all();
                while(1);
            }
            // Otherwise, terminate the cycle
            else {
                arb_debug_reg(0x48, 0x00000006);

                // Turn off the Timer
                SNTv5_REG_TMR_CNF.TMR_EN_SELF_CLK  = 0;
                mbus_remote_register_write(SNT_ADDR, 0x08, SNTv5_REG_TMR_CNF.as_int);

                SNTv5_REG_TMR_CNF.TMR_RESETB_DCDC  = 0;
                SNTv5_REG_TMR_CNF.TMR_RESETB_DIV   = 0;
                SNTv5_REG_TMR_CNF.TMR_RESETB       = 0;
                SNTv5_REG_TMR_CNF.TMR_ISOLATE      = 1;
                SNTv5_REG_TMR_CNF.TMR_SLEEP        = 1;
                mbus_remote_register_write(SNT_ADDR, 0x08, SNTv5_REG_TMR_CNF.as_int);

                // Disable Wakeup Counter
                SNTv5_REG_WUP_CNF.WUP_ENABLE = 0;
                mbus_remote_register_write(SNT_ADDR, 0x17, SNTv5_REG_WUP_CNF.as_int);
            }
        }
    } 
}
void cycle9 (void) { if (do_cycle9 == 1) { } }
void cycle10 (void) { if (do_cycle10 == 1) { } }

void cycle11 (void) {
    if (do_cycle11 == 1) {
        arb_debug_reg (0x4B, 0x00000000);
        set_halt_until_mbus_tx();

        // Watch-Dog Timer (You should see TIMERWD triggered)
        arb_debug_reg (0x4B, 0x00000001);
        config_timerwd(100);

        // Wait here until PMU resets everything
        while(1);
    }
}

//********************************************************************
// MAIN function starts here             
//********************************************************************

int main() {
    irq_history = 0;

    // Enable Wake-Up/Soft-Reset/GOCEP/XOT IRQs
    *NVIC_ISER = (0x1 << IRQ_WAKEUP) | (0x1 << IRQ_SOFT_RESET) | (0x1 << IRQ_GOCEP) | (0x1 << IRQ_XOT);

    // Initialization Sequence
    if (!get_flag(FLAG_ENUM)) { 
        initialization();
    }

    arb_debug_reg(0x20, cyc_num);

    // Testing Sequence
    if      (cyc_num == 0)  cycle0();
    else if (cyc_num == 1)  cycle1();
    else if (cyc_num == 2)  cycle2();
    else if (cyc_num == 3)  cycle3();
    else if (cyc_num == 4)  cycle4();
    else if (cyc_num == 5)  cycle5();
    else if (cyc_num == 6)  cycle6();
    else if (cyc_num == 7)  cycle7();
    else if (cyc_num == 8)  cycle8();
    else if (cyc_num == 9)  cycle9();
    else if (cyc_num == 10) cycle10();
    else if (cyc_num == 11) cycle11();
    else cyc_num = 999;

    arb_debug_reg(0x2F, cyc_num);

    // Sleep/Wakeup OR Terminate operation
    if (cyc_num == 999) *REG_CHIP_ID = 0xFFFFFF; // This will stop the verilog sim.
    else {
        cyc_num++;
        set_wakeup_timer(5, 1, 1);
        mbus_sleep_all();
    }

    while(1){  //Never Quit (should not come here.)
        arb_debug_reg(0xFF, 0xDEADBEEF);
        asm("nop;"); 
    }

    return 1;
}

