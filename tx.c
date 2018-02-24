// 代码运行状态为接收模式
// 每秒钟发射的固定测试数据为： 0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x6d, 
//                              0x6d = (0x41 +0x42 +0x43 +0x44 +0x45 +0x46 +0x47 + 0x48 +0x49)
// MCU : R8C211B4
#include "sfr_r81b.h"

const unsigned char tx_buf[15]={0x55,0x55,0x55,0x55,0x00,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x6d};
//4个字节前导码0x55,1个字节同步字0x00,10个字节数据


#define  	TX_DATA			p1_3	//MCU输出
#define  	TX_CS      		p1_7	//MCU输出
#define  	RX_CS      		p3_7	//MCU输出
#define  	RX_DATA       	p4_5	//MCU外部触发中断

#define  	LED_RX			p1_4
#define  	LED_TX			p1_5

typedef struct 
{
	unsigned char is_tx					: 1;
    unsigned char reach_1s				: 1;
}	FlagType;
FlagType	                Flag;

unsigned char count_1s;
unsigned char BitCounter;
unsigned char ByteCounter;
unsigned char DataReg;
_Bool  GotBit;

void sysclk_cfg(void);
void port_init(void);
void timerx_init(void);
void timerz_init(void);
void RF_tx_mode(void);


void main(void)
{
    sysclk_cfg();		// 外部14.7456M晶振
    port_init();		// 初始化管脚
    timerx_init();		// 初始化timerx
	timerz_init();		// 初始化发射时钟
	
	count_1s=0;
	asm("FSET I");				// 使能总中断
	while(1)
	{
		wdtr = 0;
		wdtr = 0xff;		//喂看门狗
		if(Flag.reach_1s)
		{
			Flag.reach_1s=0;
			RF_tx_mode();		// 每一分钟发射一个数据包		
		}
	}
}
/*************init**************************/
void sysclk_cfg(void)
{   	
 	prc0 = 1;		// 电压保护寄存器
    prc1 = 1;
    prc3 = 1;
    
    cm02 = 0;		// 系统时钟控制
    cm05 = 0;
    cm06 = 0;

    cm13 = 1;
    cm14 = 1;
    cm15 = 1;
    cm16 = 0;
    cm17 = 0;
    cm10 = 0;	
    
    ocd0 = 0;		// 使能外部14.7456M晶振
    ocd1 = 0;
    ocd2 = 0;
    ocd3 = 0;
    hra01 = 0;
    hra00 = 0;
    prc0 = 0;

    wdc = 0x9f;		// 使能看门狗
    cspr = 0;	
    pm12 = 1;
    wdtr = 0;
    wdtr = 0xff;
    wdts = 0;
}
void port_init(void)
{   
    pd1 = 0xf8;		// TX_DATA,TX_CS输出
    p1 = 0x80;		// 初始值 TX_DATA=0,TX_CS=0
    pd3 = 0xff;		// RX_CS输出
    p3 = 0xff;		// 初始值 RX_CS=1
    pd4 = 0x0f;		// RX_DATA输出
    p4 = 0x00;
    
    
    
    pur0 = 0xc8; 	// 设置管脚上拉
    pur1 = 0x00;
    drr = 0xff;		// p1_4,p1_5脚驱动LED
    u0mr=0x00;
    
    tzic = 0x00;	// 禁止timerz
    txic = 0x00;	// 禁止timerx
    tcic = 0x00;	// 禁止timerc
    adic = 0x00;	// 禁止AD
    s0tic = 0x00;	// 禁止UART0
    s0ric = 0x00;
    s1tic = 0x00;	// 禁止UART1
    s1ric = 0x00;   		
}
void timerx_init(void)	
{
	txmr = 0x00;
	tcss = 0x01;		// f8
	prex = 0xff;
	tx = 0xEF;			// 1/8/(255+1)(239+1) * 14.7456M = 30Hz
		
	txic = 0x05;
	txs = 1;
}
void timerz_init(void)
{
	tzmr = 0x00;	
	tcss = 0x01;
	pum = 0x00;
	prez =0xff;	
	tzpr= 0x2f;			// 1/(255+1)(47+1) * 14.7456M = 1.2kHz
	
	tzs=1;				// 计数开始

	tzic = 0x03;  		// 使能timerz
}
/*************init**************************/

void RF_tx_mode(void)
{
	Flag.is_tx = 1;
	RX_CS= 1;		// 禁止接收模块
	TX_CS= 0 ;		// 使能发送模块

	for (ByteCounter=0;ByteCounter<15;ByteCounter++)
	{
		BitCounter=8;
		DataReg=tx_buf[ByteCounter];		// 发送一个字节数据
		while(BitCounter>0)			// 等待这个字节发送完成
		{
			wdtr = 0;
			wdtr = 0xff;			// 看门狗
		}				
	}
	LED_TX^=1;		// 发送完毕，改变发射LED
	Flag.is_tx=0;
}   	                                                           
// timer X			(software int 22)
#pragma interrupt	_timer_x(vect=22)
void _timer_x(void);
void _timer_x(void)
{
	count_1s++;
	if(count_1s>30)		// 1秒定时器
	{
		count_1s=0;
		Flag.reach_1s=1;
	}
}
// timer Z			(software int 24)
#pragma interrupt	_timer_z(vect=24)
void _timer_z(void);
void _timer_z(void)
{
	if(Flag.is_tx)
	{
		if(DataReg&0x80) 	// 发送一个字节数据的最高位 
			TX_DATA=1;
		else
			TX_DATA=0;
		DataReg<<=1;		// 发送下一位
		BitCounter--;		
	}
}

