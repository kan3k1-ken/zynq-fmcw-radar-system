#ifndef SRC_BB_DRIVER_H_
#define SRC_BB_DRIVER_H_
#include "bkbase.h"
#ifdef Linux
#include "Types.h"
#else
#include "xil_types.h"
#endif

/***************************************-----------------BKF fwj table----------------*********************************************/
/********System Part*****/

#define BK_status0 0
#define BK_status1 1
#define BK_status2 2
#define BK_status3 3
#define BK_status4 4
#define BK_status5 5
#define BK_status6 6
#define BK_status7 7
#define BK_status8 8
#define BK_status9 9
#define BK_status10 10
#define BK_status11 11
#define BK_status12 12
#define BK_status13 13
#define BK_status14 14
#define BK_status15 15
#define BK_status16 16
#define BK_status17 17
#define BK_status18 18
#define BK_status19 19
#define BK_status20 20
#define BK_status21 21
#define BK_status22 22
#define BK_status23 23
#define BK_status24 24
#define BK_status25 25
#define BK_status26 26
#define BK_status27 27
#define BK_status28 28
#define BK_status29 29
#define BK_status30 30
#define BK_status31 31


#define upper_32_bits(n) ((u32)(((n) >> 16) >> 16))
#define lower_32_bits(n) ((u32)((n) & 0xffffffff))
#define get_u32_bit(n,index) ((u8) ((n>>index) & 0x01))



	/*********************************b2extb  bk_base:400*********************************************/
	//  ascripiton: shell
	//  type: RTL
    //  function : switch 1 of N bk_status also you can expand more status
	//  update:  2025.10.13
	//  0          bit0          DESR             //extb1_enable
	//  1          bit3-0        led              //4 leds
	//  2          bit23-0       smg              //6 smg  4bit every 1 smg  value can be  0-9
	//  3          bit31-0       beep_duty        //beep  duty cycle
	//  4          bit31-0       beep_freq        //beep  freq  min is 1hz  max is 30khz
	//  5          bit0          use_lcdex        //use_lcdex  if this be set to 1  lcd will desplay example image.
    //             bk_status                      //bit 4-0 btn value

#define b2extb 400

enum{
	b2extb_DESR,
	led,
	smg,
	beep_duty,
	beep_freq,
	use_lcdex
};

void b2extb_init(u32 base_index);

	/*********************************bk_status_sw bk_base:500*********************************************/
	 // ascription: fwj
	 // type: RTL
	 // function : switch 1 of N bk_status
	 // update: 2025.3.11
	 //  0        bit3-0          DESR
     //  1        bit0            sw              	          0 is status 0 , 1 is status 1
#define bk_status_sw_index  500
#define Version_Status 			   BK_status0
#define bk_ad_collect_status   	   BK_status2
enum
{
	status_sw_DESR,
	status_sw,
};
void subsys_init(u32 base_index);
void SubSystemChoose(u32 subsys);




	/*********************************bk_uart bk_base:700*********************************************/
	 // ascription: shell
	 // type: RTL
	 // function: PL UART TX/RX via BK bus, echo/loopback test
	 // update: 2026.07.09
	 //  0          bit0            DESR                     Module enable (1=ENABLE)
	 //  1          bit31-0         BAUD_DIV                 Baud rate divisor = clk_freq / baud_rate
	 //  2          bit7-0          TX_DATA                  Transmit data byte (write auto-triggers TX)
	 //  3          bit7-0          RX_DATA                  mode1: read received byte
	 /*** bk_status ***/
	 // mode0: bit[9] tx_busy, bit[8] rx_valid, bit[7:0] rx_data

#define BK_UART_BASE            1000
#define BK_UART_DESR            1000    // offset 0: bit0=ENABLE
#define BK_UART_BAUD_DIV        1001    // offset 1: clock divider
#define BK_UART_TX_DATA         1002    // offset 2: TX byte (write triggers send)
#define BK_UART_RX_DATA         1003    // offset 3: reserved (was RX_DATA)
#define BK_UART_RX_FIFO         1004    // offset 4: RX FIFO read (mode1)
#define BK_UART_RX_CLEAN        1006    // offset 6: rec_clean

#define BK_UART_ENABLE          0x01    // bit0: module enable
#define BK_UART_LOOPBACK        0x02    // bit1: internal loopback

/* bk_status bit definitions (mode0) */
#define BK_UART_RX_MASK         0x00FF    // bits[7:0]  : rx_data byte (oldest FIFO)
#define BK_UART_RX_VALID        0x0100    // bit[8]     : FIFO not empty
#define BK_UART_TX_PENDING      0x0200    // bit[9]     : TX data held, will send
#define BK_UART_TX_BUSY         0x0400    // bit[10]    : TX engine active
#define BK_UART_RX_OVERFLOW     0x0800    // bit[11]    : RX FIFO overflow
#define BK_UART_FRAME_ERR       0x1000    // bit[12]    : stop bit error
#define BK_UART_RX_COUNT_MASK   0x1FE000  // bits[20:13] : bytes in RX FIFO
#define BK_UART_RX_COUNT_SHIFT  13

#define BK_UART_SUBSYS          1       // bk_status1_i channel

enum {
	bk_uart_DESR,
	bk_uart_BAUD_DIV,
	bk_uart_TX_DATA,
	bk_uart_RX_DATA
};

void bk_uart_init(void);
void bk_uart_init_ex(u32 base_index, u32 BandRate);
void bk_uart_send_byte(u8 data);
u8   bk_uart_recv_byte(void);
int  bk_uart_is_rx_ready(void);
int  bk_uart_read(u32 base_index, u8 rec_data[]);
int  bk_uart_is_tx_busy(void);
u32  bk_uart_get_rx_total(void);

void BB_init();
void BB_release();

int board_init();



#endif