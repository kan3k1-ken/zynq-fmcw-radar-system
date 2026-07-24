#include "bb_driver.h"

#include <stdio.h>
#include "bkbase.h"

#ifdef Linux
#include <pthread.h>
#include "Types.h"
#include <unistd.h>
#include<sys/mman.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<time.h>
pthread_mutex_t mutex_fpga = PTHREAD_MUTEX_INITIALIZER;
#else
#include "xil_types.h"
#include <sleep.h>
#endif

/*********************************bk_status_sw bk_base:500*********************************************/
void subsys_init(u32 base_index)
{
    /* BKF 4.0: 子系统选择已集成到框架内部，无需外部初始化 */
    /* 保留此函数以兼容 bb_driver.h 的声明 */
}

/*********************************b2extb  bk_base:400*********************************************/

void b2extb_init(u32 base_index)
{

	u8 smg0,smg1,smg2,smg3,smg4,smg5;
	u64  beep_D;

	smg0=0;
	smg1=1;
	smg2=2;
	smg3=3;
	smg4=4;
    smg5=5;

    beep_D = 5629499534213;   // 2^ v
	bk_send_data(BK_AXI_BASE_ADDR,base_index+status_sw_DESR,0x01);
}

void SubSystemChoose(u32 subsys)
{
	/* BKF 4.0: subsystem select via global index 1 (built into bkt_forwarder) */
	bk_send_data(BK_AXI_BASE_ADDR, 1, subsys);
}


int board_init()
{
	u32 version;


	#ifdef Linux
    if(bk_mem_init()==0)
    {
        printf("bk mem  init finished\r\n");
    }
    else
    {
        printf("bk mem  init failed\r\n");
    }
    printf("This is a Linux elf\r\n");
#else
    printf("This is a StandAlone elf\r\n");
#endif


    subsys_init(bk_status_sw_index);


    SubSystemChoose(Version_Status);

	version = get_bk_status();

	if(version == 0)
	{
		printf("board init error, bk framework doesn't work! \r\n");
		return -1;
	}
	else
	{
		printf("Cur version is v1 \r\n");
	}




	return 0;
}




void BB_init()
{



}

/*********************************bk_uart bk_base:700*********************************************/

void bk_uart_init(void)
{
    /* Reset then enable module (reference pattern: DESR 0→1) */
    bk_send_data(BK_AXI_BASE_ADDR, BK_UART_DESR, 0x00);
    bk_send_data(BK_AXI_BASE_ADDR, BK_UART_DESR, BK_UART_ENABLE);

    /* Configure baud rate: 100MHz / 115200 = 868 */
    bk_send_data(BK_AXI_BASE_ADDR, BK_UART_BAUD_DIV, 868);

    /* Clear TX buffer and start */
    bk_send_data(BK_AXI_BASE_ADDR, BK_UART_TX_DATA, 0);

    /* Clear RX buffer */
    bk_send_data(BK_AXI_BASE_ADDR, BK_UART_DESR, 0x00);
    bk_send_data(BK_AXI_BASE_ADDR, BK_UART_DESR, BK_UART_ENABLE);

    /* Select BK-UART subsystem (bk_status1_i channel) */
    SubSystemChoose(BK_UART_SUBSYS);

    /* Set mode0: bk_status returns real-time status */
    set_bk_mode(0);
}

void bk_uart_send_byte(u8 data)
{
    /* Wait until TX engine is free */
    while (bk_uart_is_tx_busy());

    /* Write TX_DATA — auto-triggers UART transmission */
    bk_send_data(BK_AXI_BASE_ADDR, BK_UART_TX_DATA, (u32)data);
}

u8 bk_uart_recv_byte(void)
{
    /* Wait for new RX data */
    while (!bk_uart_is_rx_ready());

    /* Read bk_status and extract rx_data byte */
    u32 status = get_bk_status();
    return (u8)(status & BK_UART_RX_MASK);
}

int bk_uart_is_rx_ready(void)
{
    return (get_bk_status() & BK_UART_RX_VALID) ? 1 : 0;
}

int bk_uart_is_tx_busy(void)
{
    return (get_bk_status() & BK_UART_TX_BUSY) ? 1 : 0;
}

void bk_uart_init_ex(u32 base_index, u32 BandRate)
{
    (void)base_index;
    u32 div = 100000000 / BandRate;
    bk_send_data(BK_AXI_BASE_ADDR, BK_UART_DESR, 0x00);
    bk_send_data(BK_AXI_BASE_ADDR, BK_UART_DESR, BK_UART_ENABLE);
    bk_send_data(BK_AXI_BASE_ADDR, BK_UART_BAUD_DIV, div);
    bk_send_data(BK_AXI_BASE_ADDR, BK_UART_DESR, 0x00);
    bk_send_data(BK_AXI_BASE_ADDR, BK_UART_DESR, BK_UART_ENABLE);
    SubSystemChoose(BK_UART_SUBSYS);
    set_bk_mode(0);
}

int bk_uart_read(u32 base_index, u8 rec_data[])
{
    int i, n;
    u32 s;

    (void)base_index;

    set_bk_mode(0);
    get_bk_status();
    s = get_bk_status();
    n = (int)((s & BK_UART_RX_COUNT_MASK) >> BK_UART_RX_COUNT_SHIFT);
    if (n == 0)
        return 0;
    if (n > 8) n = 8;

    set_bk_mode(1);
    for (i = 0; i < n; i++) {
        bk_send_data(BK_AXI_BASE_ADDR, BK_UART_BASE + 4, 0x00);
        get_bk_status();
        s = get_bk_status();
        rec_data[i] = (u8)(s & 0xFF);
    }

    return n;
}

u32 bk_uart_get_rx_total(void)
{
    u32 s;

    set_bk_mode(2);
    get_bk_status();
    s = get_bk_status();
    return s;
}



#ifdef Linux
void BB_release()
{

}
#endif