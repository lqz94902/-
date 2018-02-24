// 代码运行状态为接收模式
// 每秒钟发射的固定测试数据为： 0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x6d, 
//                              0x6d = (0x41 +0x42 +0x43 +0x44 +0x45 +0x46 +0x47 + 0x48 +0x49)
// MCU : R8C211B4
#include "sfr_r81b.h"

unsigned char rx_buf[10];

#define  	TX_DATA			p1_3	//MCU输出
#define  	TX_CS      		p1_7	//MCU输出
#define  	RX_CS      		p3_7	//MCU输出
#define  	RX_DATA       	p4_5	//MCU外部输入中断

#define  	LED_RX			p1_4
#define  	LED_TX			p1_5

// 两个前导码采样点间的宽度
#define MAX_WIDTH 0x634f           //14.7456M/0x634f=580hz
#define MIN_WIDTH  0x5b6d			//14.7456M/0x5b6d=630hz
typedef struct 
{	
    unsigned char reach_1s				: 1;
	unsigned char is_tx					: 1;
	unsigned char rxing					: 1;
	unsigned char width_caled   		: 1;
	unsigned char pulse_in_full       	: 1;
	unsigned char preamble_end			: 1;
	unsigned char preamble				: 1;	
	unsigned char preamble_changed		: 1;
}	FlagType;
FlagType	                Flag;

unsigned char count_1s;
unsigned char BitCounter;
unsigned char ByteCounter;
unsigned char DataReg;
_Bool  GotBit;

/**************cal width para***************/
unsigned char	pulse_counter;				// 已采样的脉冲数量
unsigned char	pulse_correct_counter; 		// 宽度符合数据率范围的脉冲数量
unsigned char	preamble_counter;			// 接收到“0”的数量
unsigned int   	width;						// 两个下降沿间的计时器差值
unsigned int	pulse_in_timer[6];			// 下降沿中断的计时器值
/**************cal width para**************/

void sysclk_cfg(void);
void port_init(void);
void ini0_init(void);
void timerx_init(void);
void timerz_init(void);
void timerc_init(void);
void cal_pulse_init(void);
void delay_us(unsigned int dx1us);
void delay_100us(unsigned char time);
void rx_data_init(void);
void rx_data_end(void);
void cal_preamble_width(void);
void pulse_to_rx(void);
void rece_preamble(void);
void rece_data(void);
void RF_rx_mode(void);

void main(void);

void main(void)
{
    sysclk_cfg();			// 外部14.7456M晶振
    port_init();			// 初始化I0口
    timerx_init();			// 初始化timerx
	
	timerz_init();			// 初始化数据采样时钟timerz
	cal_pulse_init();		// 初始化前导码检测参数
	ini0_init();			// 初始化外部触发中断
	
	count_1s=0;
	Flag.is_tx=0;
	asm("FSET I");
	while(1)
	{
		wdtr = 0;
		wdtr = 0xff;//看门狗

		if(!Flag.is_tx)
		{
			RX_CS=0;

			if(pulse_correct_counter==0)	// 没有检测到前导码
			 {
				 cal_preamble_width();		// 计算采样到脉冲宽度
				 pulse_to_rx();				// 转到接收模式
			 }
			 else
			 {	
				 RF_rx_mode();				
			 }
		}
	}
}
/********************init*******************/
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
    p3 = 0x7f;		// 初始值 RX_CS=0
    pd4 = 0x0f;		// RX_DATA输出
    p4 = 0x00;
    
    
    
    pur0 = 0xc8; 	// 设置管脚上拉
    pur1 = 0x00;
    drr = 0xff;		// p1_4,p1_5脚驱动LED
    u0mr=0x00;
    
    tzic = 0x00;	// 禁止timerz
    txic = 0x00;	// 禁止timerz
    tcic = 0x00;	// 禁止timerz
    adic = 0x00;	// 禁止AD
    s0tic = 0x00;	// 禁止UART0
    s0ric = 0x00;
    s1tic = 0x00;	// 禁止UART1
    s1ric = 0x00;   			
}
void ini0_init(void)
{
	pum=0x00;		// 下降沿触发
	inten=0x01;		// 单边沿触发
	int0f=0x01;		// f1滤波器
	int0ic=0x02;	// 使能int0
}
void timerx_init(void)
{
	txmr = 0x00;
	tcss = 0x01;		// f8
	prex = 0xff;
	tx = 0xf0;			// 1/8/(255+1)(239+1) * 14.7456M = 30Hz
		
	txic = 0x05;
	txs = 1;
}
void timerz_init(void)
{
	tzmr = 0x00;
	tcss = 0x01;
	pum = 0x00;
	prez =0xff;
	tzpr= 0x2f;		// 1/(255+1)(47+1) * 14.7456M = 1.2kHz
	
	tzs=1;			// 开始timerz计数

	tzic = 0x03;  // 使能timerc
}
void timerc_init(void)
{
	tcc00 = 0;   	// 停止timerc
	tcc0 = 0x18;	// 停止timerc,f1滤波器
	tcc1 = 0x00;  	// f1 f1滤波器，捕捉模式
	tcout = 0x00;  	// 禁止cmp
	
	
	tcc00 = 1;   	// 开始计数
	tcic = 0x04;  	// 使能timerc
}
void cal_pulse_init(void)
{
	pulse_counter = 0; 			// 初始化已采样的脉冲数量			 
	pulse_correct_counter = 0;	// 初始化宽度符合数据率范围的脉冲数量
	
	Flag.width_caled = 1;		
	Flag.pulse_in_full = 0;		// 初始化采样完成标志
	
	timerc_init();				//初始化脉冲宽度计数器timerc
}
/********************init*******************/
/************************delay***********************************************/
void delay_us(unsigned int dx1us) 	 // 延时 7.375us-7.500us
{
	unsigned int j;	
	for(j = 0; j<dx1us; j++)
		asm("NOP");
}
void delay_100us(unsigned char time)		// 延时 0.1145ms
{
	unsigned char i,k;
	for(k = 0; k< time; k++)
	{
		for(i = 0; i<92; i++)
		{
			asm("NOP");
		}	
	}		
}
/******************delay******************/
void rx_data_init(void)			// 初始化接收模式标志
{
	Flag.rxing=1;				// 接收模式标志
	Flag.preamble_end=0;		// 前导码接收完毕标志
	Flag.preamble=0;			// 当前接收到的前导码值
	Flag.preamble_changed=0;	// 接收到一位前导码
	preamble_counter=0;			// 同步字接收数量
}
void rx_data_end(void)
{		
	Flag.rxing=0;				// 停止接收模式
	Flag.preamble_end=0;		
	Flag.preamble=0;			
	Flag.preamble_changed=0;
}

void cal_preamble_width(void)			
{
	unsigned int width_del;
	if(pulse_counter>1)  		// 检测到两个下降沿
	{	
		if(Flag.width_caled ==0)
		{
			Flag.width_caled = 1;				
			//计算两个下降沿间的timerc计数值,并与标准值比较
			if(pulse_in_timer[pulse_counter-1] <pulse_in_timer[pulse_counter-2])   // 如果timerc溢出
			{
				width_del = 0xffff-pulse_in_timer[pulse_counter-2];
				width = width_del+ pulse_in_timer[pulse_counter-1]+1;		// 计算两个下降沿的宽度
			}	
			else									//一般情况下
			{
				width = pulse_in_timer[pulse_counter-1]-pulse_in_timer[pulse_counter-2];	// 计算两个下降沿的宽度
			}
			if((width > MAX_WIDTH) ||(width < MIN_WIDTH))
			{
				 	  cal_pulse_init();  		// 如果宽度不符合标准，重新接收
			}		 
		}		
	}	
}	

void pulse_to_rx(void)
{
	if(Flag.pulse_in_full)  // 连续得到六个正确的脉冲
	{
		pulse_correct_counter=1;//前导码检测正确	
		delay_100us(3);		//延时0.33ms
		rx_data_init();	
	}

}
void rece_preamble(void)
{
	Flag.preamble_changed=0;
	if(GotBit)											// 采样到前导码值为“1”
	{
		if((!Flag.preamble)&&(preamble_counter==0))		// 如果上一位前导码的值是“0”
		{
			Flag.preamble=1;							// 这一位的值是“1”
		}
		else
		{	
			Flag.rxing=0;								// 前导码错误
			rx_data_end();
			cal_pulse_init();
		}
	}
	else												// 采样到前导码值为“0"
	{
		if(!Flag.preamble)								// 如果上一位也是"0", 表示正在接收同步字
		{
			preamble_counter++;							
			if(preamble_counter>6)						// 接收完8位同步字
			{
				preamble_counter=0;
				Flag.preamble_end=1;
			}
		}
		else											// 上一位是”1“	
		{
			Flag.preamble=0;							
		}
	}
}
void rece_data(void)
{
	unsigned char i,chksum;
	DataReg=0;
	BitCounter=0;
	ByteCounter=0;
	while(Flag.rxing)
	{
		wdtr = 0;
		wdtr = 0xff;//看门狗
		if(BitCounter>7)						// 接收完一个字节
		{
			BitCounter=0;						// 接收下一个字节
			rx_buf[ByteCounter]=DataReg;
			DataReg=0;
			ByteCounter++;
		}
		if(ByteCounter>9)						// 接收完所有数据
		{
			rx_data_end();						// 设置接收完成标志		
			chksum = 0;
			for(i=0;i<9;i++)							//	计算校验字
			{		
				chksum += rx_buf[i];					
			}          	 		
			if(( chksum == rx_buf[9] )&&( rx_buf[0] == 0x41 ))
			{
				LED_RX=LED_RX^1;						// 数据正确，改变接收LED
			}
			cal_pulse_init();							// 重新接收
		}
	}	
}
void RF_rx_mode(void)
{
	if(Flag.preamble_changed)
	{
		rece_preamble();								// 接收前导码和同步字
	}
	if(Flag.preamble_end)
	{	
		rece_data();									// 接收数据
	}	
}                                                                                                                                                                                     
// timer X			(software int 22)
#pragma interrupt	_timer_x(vect=22)
void _timer_x(void);
void _timer_x(void)
{
	count_1s++;
	if(count_1s>30)			// 1秒定时器
	{
		count_1s=0;
		Flag.reach_1s=1;
	}
}

// int0				(software int 29)
#pragma interrupt	_int0(vect=29)
void _int0(void);
void _int0(void)
{
	if(Flag.pulse_in_full==0) 
	{
		pulse_in_timer[pulse_counter] = tc; 	// 记录触发中断时的计数值		
		pulse_counter++;						
		Flag.width_caled=0;

		if(pulse_counter == 0x05)				// 得到六个脉冲
		{	
			pulse_counter=0;
			Flag.pulse_in_full = 1;
		}
	}
	
}
// timer Z			(software int 24)
#pragma interrupt	_timer_z(vect=24)
void _timer_z(void);
void _timer_z(void)
{
	if(Flag.rxing)
	{
		//p1_5=!p1_5;							// 用一个IO口来观测采样点是否在每个数据脉冲的中间
		Flag.preamble_changed=1;				// 接收到前导码的一位
		if(Flag.preamble_end)					// 接收数据
		{
			DataReg<<=1;
			if(RX_DATA)
			DataReg|=0x01;
			else
			DataReg&=0xfe;
			BitCounter++;
		}
		else									// 接收前导码
		{
			GotBit=RX_DATA;
		}
	}
}
// timer C			(software int 27)
#pragma interrupt	_timer_c(vect=27)
void _timer_c(void);
void _timer_c(void)
{
	tc=0x0000;									// timerc溢出，重新装载计数器初值
}
