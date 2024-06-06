#include "stm8s.h"
#include <string.h>

#define LCD_CS          GPIOC, GPIO_PIN_5
#define LCD_DATA        GPIOC, GPIO_PIN_6
#define LCD_CLK         GPIOC, GPIO_PIN_7
#define DHT11_PIN       GPIOD, GPIO_PIN_3
#define CO2_CALIBRATION GPIOD, GPIO_PIN_4
#define HEAT_TIME_S 180

uint16_t co2;
uint8_t hum;
uint8_t temp;

// ==== TIMER ====
volatile uint32_t timer_s = 0;

INTERRUPT_HANDLER(TIM1_UPD_OVF_TRG_BRK_IRQHandler, 11)
{
  timer_s++;
  TIM1_ClearITPendingBit(TIM1_IT_UPDATE);
}

void InitTimer()
{
  // sec timer
  TIM1_TimeBaseInit(16000,TIM1_COUNTERMODE_UP, 1000, 0);
  TIM1_ITConfig(TIM1_IT_UPDATE, ENABLE);
  TIM1_Cmd(ENABLE);
  // us timer
  TIM2_TimeBaseInit(TIM2_PRESCALER_16, 0xFFFF);
  TIM2_Cmd(ENABLE);
}

// ==== UART ====

void InitUART()
{
  UART1_Init((uint32_t)9600, UART1_WORDLENGTH_8D, UART1_STOPBITS_1, UART1_PARITY_NO, UART1_SYNCMODE_CLOCK_DISABLE, UART1_MODE_TXRX_ENABLE);
  UART1_Cmd(ENABLE);
}

uint8_t TxUart(const uint8_t *buff, uint16_t size)
{
  uint32_t safeTimer = timer_s + 2;
  FlagStatus stat;
  
  while (size--)
  {
    UART1_SendData8(*buff++);  
    do
    {
      stat = UART1_GetFlagStatus(UART1_FLAG_TXE);
      if (timer_s == safeTimer) return 0;
    }
    while (stat == RESET );
  }
  
  do
  {
    stat = UART1_GetFlagStatus(UART1_FLAG_TC);
    if (timer_s == safeTimer) return 0;
  }
  while (stat == RESET ); 
  
  return 1;
}

uint8_t RxUart(uint8_t *buff, uint16_t size)
{
  uint32_t safeTimer = timer_s + 2;
  FlagStatus stat;
  
  while (size--)
  {
    do
    {
      stat = UART1_GetFlagStatus(UART1_FLAG_RXNE);
      if (timer_s == safeTimer) return 0;
    }
    while (stat == RESET );
    
    *buff++ = UART1_ReceiveData8();    
  }  
  return 1;
}

// ==== I2C ====
/*
void InitI2C()
{

  I2C_Init(100000, 1, I2C_DUTYCYCLE_2, I2C_ACK_NONE, I2C_ADDMODE_7BIT , 2 );
  I2C_Cmd(ENABLE);
  
  I2C_GenerateSTART(ENABLE);  
  while ( I2C_GetFlagStatus(I2C_FLAG_STARTDETECTION) != SET) ;
  
  I2C_Send7bitAddress(0xC0, I2C_DIRECTION_TX);  
  while ( I2C_GetFlagStatus(I2C_FLAG_ADDRESSSENTMATCHED) != SET) ;
  while ( I2C_GetFlagStatus(I2C_FLAG_TRANSMITTERRECEIVER) != SET) ;
}
*/
// ==== GPIO ====
void PinInit(void)
{
  // lcd
  GPIO_Init(LCD_CS,    GPIO_MODE_OUT_OD_HIZ_FAST);
  GPIO_Init(LCD_DATA,  GPIO_MODE_OUT_OD_HIZ_FAST);
  GPIO_Init(LCD_CLK,   GPIO_MODE_OUT_OD_HIZ_FAST);
  
  // calibration
  GPIO_Init(CO2_CALIBRATION, GPIO_MODE_IN_PU_NO_IT);
}


// ==== INIT ====
void Init(void)
{
  // ==== CLK ====
  
  // Configure the Fcpu to DIV1
  CLK_SYSCLKConfig(CLK_PRESCALER_CPUDIV1);    
  // Configure the HSI prescaler to the optimal value 
  CLK_SYSCLKConfig(CLK_PRESCALER_HSIDIV1);        
  // Configure the system clock to use HSI clock source and to run at 16Mhz
  CLK_ClockSwitchConfig(CLK_SWITCHMODE_AUTO, CLK_SOURCE_HSI, DISABLE, CLK_CURRENTCLOCKSTATE_DISABLE);
  
  enableInterrupts();
  InitTimer();
  InitUART();
  //InitI2C();
  PinInit();  
}

void Delay(uint16_t us)
{
  TIM2->CNTRL = 0;
  TIM2->CNTRH = 0;
  while( TIM2_GetCounter() < us);
}

// === LCD ===
#define LCD_CMD_LCD_OFF       0x804 // 0b 100 0000 0010 0
#define LCD_CMD_BIAS_13_4_COM 0x852 // 0b 100 0010 1001 0
#define LCD_CMD_RC_256K       0x830 // 0b 100 0001 1000 0
#define LCD_CMD_SYS_DIS       0x800 // 0b 100 0000 0000 0
#define LCD_CMD_SYS_EN        0x802 // 0b 100 0000 0001 0
#define LCD_CMD_LCD_ON        0x806 // 0b 100 0000 0011 0
#define LCD_CMD_WDTDIS1       0x80A // 0b 100 0000 0011 0
#define DL 10

#define DELAY(US) do {TIM2->CNTRL = 0; while (TIM2->CNTRL < US);}while(0)// до 255 мкс

const uint8_t lcd_def[10] = 
{
  0x00,  
  0x80,
  0,
  0,
  0,  
  0x80,
  0,
  0xC3,
  0x80,
  0
};

const uint8_t lcd_digit[] = {0x5F, 0x50, 0x6B, 0x79, 0x74, 0x3D, 0x3F, 0x58, 0x7F, 0x7D, 0x76, 0x2F, 0x0F, 0xFE, 0x07 }; // 0 1 2 3 4 5 6 7 8 9 H E C A L
// Отправка нескольких бит (команда или код команды. Или байт). Только data и clk
void LCD_SendWord(uint16_t word, uint8_t bits)
{  
  while(bits--)
  {
    GPIO_WriteLow(LCD_CLK);
    
    if(word & (1<<bits))     
      GPIO_WriteHigh(LCD_DATA);
    else
      GPIO_WriteLow(LCD_DATA);
    DELAY(DL);    
    GPIO_WriteHigh(LCD_CLK);
    DELAY(DL);
  }
 
}

void LCD_SendCmd(uint16_t cmd)
{    
  GPIO_WriteLow(LCD_CS); 
  DELAY(DL);
  LCD_SendWord(cmd, 12);
  DELAY(DL);
  GPIO_WriteHigh(LCD_CS);
  DELAY(DL);
  GPIO_WriteHigh(LCD_DATA);
  DELAY(DL);
}


// данных 32х4 бита = 16 байт
void LCD_SendData(uint8_t data[10])
{  
  const uint16_t write = 0x140; // 0b101 000000
  GPIO_WriteLow(LCD_CS);
  DELAY(DL);
  LCD_SendWord(write, 9);
  DELAY(DL);
  for (uint8_t i=0; i<10; i++)
  {
    LCD_SendWord(data[i], 8);
    DELAY(DL);
  }
  GPIO_WriteHigh(LCD_CS);
  DELAY(DL);
  GPIO_WriteHigh(LCD_DATA);
  DELAY(DL);
}

void valToLCD(uint16_t val, uint8_t *out) // Перевод числа в вывод LCD - 4 digs
{  
  if (val > 9999) val %= 10000;
  *(out++) = val / 1000;
  val = val % 1000;
  *(out++) = val / 100;
  val = val % 100;
  *(out++) = val / 10;
  *out = val % 10; 
}

void LCD_Update(uint8_t isCalibration)
{
  uint8_t LCD_data[10];
  memcpy(LCD_data, lcd_def, 10);
  uint8_t tmp[4];
  // co2
  if (isCalibration)
  {
    LCD_data[4] |= lcd_digit[0xC];
    LCD_data[3] |= lcd_digit[0xD];
    LCD_data[2] |= lcd_digit[0xE];
    LCD_data[1] |= lcd_digit[0x8];      
  }
  else
  {  
    valToLCD(co2 & 0x3FFF, tmp);  
    
    LCD_data[4] |= (tmp[0] > 0) ? lcd_digit[tmp[0]] : 0;
    LCD_data[3] |= lcd_digit[tmp[1]];
    LCD_data[2] |= lcd_digit[tmp[2]];
    LCD_data[1] |= lcd_digit[tmp[3]];      
    
    if (co2 & 0x8000) LCD_data[4] = lcd_digit[11];  
    if (co2 & 0x4000) 
    {
      LCD_data[4] = lcd_digit[10];  
      co2 *= 10;
    }
    co2 &= 0x3FFF;
    
    LCD_data[0] |= 1<<3;
    if (co2 >= 500)  LCD_data[0] |= 1<<2;
    if (co2 >= 1000) LCD_data[0] |= 1<<1;
    if (co2 >= 1500) LCD_data[0] |= 1<<0;
  }  
  // temp
  valToLCD(temp & 0x7F, tmp);    
  LCD_data[9] |= lcd_digit[tmp[2]];
  LCD_data[8] |= lcd_digit[tmp[3]];      
  if (temp & 0x80) LCD_data[9] = lcd_digit[11];
    
  // hum
  valToLCD(hum & 0x7F, tmp);    
  LCD_data[6] |= lcd_digit[tmp[2]];
  LCD_data[5] |= lcd_digit[tmp[3]];      
  if (hum & 0x80) LCD_data[6] = lcd_digit[11];
  
  LCD_SendData(LCD_data);

}
// ===========================================
void CO2_Update(uint8_t isCalibration)
{
   const uint8_t TxBuff_meas[9] =  {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
   const uint8_t TxBuff_calib[9] = {0xFF, 0x01, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78};
   uint8_t RxBuff[9];
   
   if (isCalibration)
   {
     TxUart(TxBuff_calib, sizeof(TxBuff_calib));
     return;
   }
   
   TxUart(TxBuff_meas, sizeof(TxBuff_meas));
   uint8_t res = RxUart(RxBuff, sizeof(RxBuff));
   if (res == 0) 
   {
     co2 = 0x8000 + 0; // E0 no answer
     return;
   }
   uint8_t cs = 0;
   for(char i=1; i<=7; i++)
     cs += RxBuff[i];
   cs = (0xFF - cs) + 1;
   if (cs != RxBuff[8])
     co2 = 0x8000 + 1; // E1 - checksumm
   else
    co2 = (RxBuff[2]<<8) + RxBuff[3];   
}

// ===================================

uint8_t DHT11_GetPulse() // 0, 1 or 0xFF-err
{  
  uint8_t ans;  
  // wait low
  TIM2->CNTRL = 0;
  while(GPIO_ReadInputPin(DHT11_PIN) != RESET) 
    if(TIM2->CNTRL > 0xF0) 
      return 0xFF;
  ans = (TIM2->CNTRL < 50) ? 0 : 1;  
  
  // wait hi
  while(GPIO_ReadInputPin(DHT11_PIN) == RESET) 
    if(TIM2->CNTRL > 0xF0) 
      return 0xFF;
  
  return ans;  
}

void DHT11_Update()
{
  static uint8_t prediv = 0;
  uint8_t res[5] = {0};
  uint8_t bitN = 7;
  uint8_t byteN = 0;
  uint8_t cs;  
 
  if (prediv++ & 0x7)
    return;
  
  // out
  GPIO_Init(DHT11_PIN, GPIO_MODE_OUT_OD_LOW_FAST);
  Delay(20000);
  GPIO_Init(DHT11_PIN, GPIO_MODE_IN_FL_NO_IT); 
  // ack
  if ( DHT11_GetPulse() == 0xFF) goto error;
  if ( DHT11_GetPulse() == 0xFF) goto error;
  // read data 
    //wait down
  for (char i=0; i<40 ;i++)
  {
    res[byteN] |= DHT11_GetPulse() << bitN;
    if (bitN > 0) 
      bitN--; 
    else 
    {
      bitN = 7;
      byteN++;
    }    
  }
  
  cs = res[0] + res[1] + res[2] + res[3];
  if (res[4] != cs)
  {
    temp = 0x81;
    hum = 0x81;  
  }
  else
  {
    temp = res[2];
    hum = res[0];  
  }
  
  return;
  
  error:
    temp = 0x80;
    hum = 0x80;
     
  
}

// ========================

int main( void )
{
  Init();  
  uint32_t currentTim = timer_s;
  uint8_t calibrationFlag = 0;
  while (currentTim == timer_s) ;
  LCD_SendCmd(LCD_CMD_BIAS_13_4_COM);
  LCD_SendCmd(LCD_CMD_RC_256K);  
  LCD_SendCmd(LCD_CMD_SYS_DIS);  
  LCD_SendCmd(LCD_CMD_WDTDIS1);
  LCD_SendCmd(LCD_CMD_SYS_EN);  
  LCD_SendCmd(LCD_CMD_LCD_ON);  
   
  while(1)
  {  
    currentTim = timer_s;
    while (currentTim == timer_s)
      while (GPIO_ReadInputPin(CO2_CALIBRATION) == RESET)
        calibrationFlag = 1;
      
    if ( timer_s < HEAT_TIME_S)
    {
      co2 = HEAT_TIME_S - timer_s;
      co2 += 0x4000; // флаг установки символа H
    }
    else
    {      
        CO2_Update(calibrationFlag);     
        
    }
    DHT11_Update();
    
    LCD_Update(calibrationFlag);  
    calibrationFlag = 0;
  }
  
}
