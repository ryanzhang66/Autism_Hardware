#include "max30102.h"
#include <stdio.h>
#include <unistd.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "iot_errno.h" //==IOT_SUCCESS =0 
#include <stddef.h>

#include "hi_io.h"   //上拉、复用
#include "hi_gpio.h" //hi_gpio_set_dir()、hi_gpio_set(get)_output(input)_val()
#include "hi_time.h"
#include "hi_i2c.h" 

uint8_t MAX_Cmd(uint8_t regAddr)
{ 
	hi_i2c_idx id = MAX_I2C_IDX;
    uint8_t buffer[] = {regAddr};
    hi_i2c_data i2cData;
	i2cData.receive_buf = NULL;
    i2cData.receive_len = 0;
    i2cData.send_buf = buffer;
    i2cData.send_len = sizeof(buffer)/sizeof(buffer[0]);
    return hi_i2c_write((hi_i2c_idx)id, MAX30102_WR_address, &i2cData); //==发送器件地址+写命令 + 寄存器regAddr 
}

uint32_t MAX_Write_Data(uint8_t regAddr, uint8_t *data, unsigned int dataLen)
{
    hi_i2c_idx id = MAX_I2C_IDX;
    uint8_t buffer[] = {regAddr, data};
    hi_i2c_data i2cData;
	i2cData.receive_buf = NULL;
    i2cData.receive_len = 0;
    i2cData.send_buf = buffer;
    i2cData.send_len = sizeof(buffer)/sizeof(buffer[0]);
    return hi_i2c_write((hi_i2c_idx)id, I2C_WRITE_ADDR, &i2cData);  //==发送器件地址+写命令+ reg + data
}

uint32_t MAX_Read_Data(uint8_t regAddr, uint8_t *data, unsigned int dataLen)
{
    hi_i2c_idx id = MAX_I2C_IDX;
    hi_i2c_data i2cData;
    i2cData.send_buf = NULL;
    i2cData.send_len = 0;
    i2cData.receive_buf = data;
    i2cData.receive_len = dataLen;
	MAX_Cmd(regAddr); // write device add 0xAE + reg_ADD [目标寄存器]
    return hi_i2c_read((hi_i2c_idx)id, I2C_READ_ADDR, &i2cData);
}

void max30102_reset(void)
{
	MAX_Write_Data(REG_MODE_CONFIG, 0x40, 1);
}

void max30102_init(void)
{
	uint8_t max30102_info = 0;
	printf("\r\n come in MAX30102 init\r\n");
	max30102_reset();
	MAX_Read_Data(REG_PART_ID,&max30102_info,1);
	printf("REG_PART_ID = %d \n",max30102_info); //测试是否输出ID，看I2C是否正常
	if(max30102_info==0) //ID 不确定是多少 测试
	{
		printf("\r\n MAX30102 init Faild \r\n");
		return ;
	}
	printf("\r\n MAX30102 init Successful \r\n"); 



	MAX_Write_Data(REG_INTR_ENABLE_1, 0xc0,1); // INTR setting
	MAX_Write_Data(REG_INTR_ENABLE_2, 0x00,1);
	MAX_Write_Data(REG_FIFO_WR_PTR, 0x00,1); //FIFO_WR_PTR[4:0]
	MAX_Write_Data(REG_OVF_COUNTER, 0x00,1); //OVF_COUNTER[4:0]
	MAX_Write_Data(REG_FIFO_RD_PTR, 0x00,1); //FIFO_RD_PTR[4:0]
	MAX_Write_Data(REG_FIFO_CONFIG, 0x0f,1); //sample avg = 1, fifo rollover=false, fifo almost full = 17
	MAX_Write_Data(REG_MODE_CONFIG, 0x03,1); //0x02 for Red only, 0x03 for SpO2 mode 0x07 multimode LED
	MAX_Write_Data(REG_SPO2_CONFIG, 0x27,1); // SPO2_ADC range = 4096nA, SPO2 sample rate (100 Hz), LED pulseWidth (400uS)
	MAX_Write_Data(REG_LED1_PA, 0x24,1);	 //Choose value for ~ 7mA for LED1
	MAX_Write_Data(REG_LED2_PA, 0x24,1);	 // Choose value for ~ 7mA for LED2
	MAX_Write_Data(REG_PILOT_PA, 0x7f,1);	 // Choose value for ~ 25mA for Pilot LED 
}

void maxim_max30102_read_fifo(uint32_t *pun_red_led, uint32_t *pun_ir_led)
{
	uint32_t un_temp;
	unsigned char uch_temp;
	char ach_i2c_data[6];
	*pun_red_led = 0;
	*pun_ir_led = 0;

	//read and clear status register
	MAX_Read_Data(REG_INTR_STATUS_1, &uch_temp,1);
	MAX_Read_Data(REG_INTR_STATUS_2, &uch_temp,1);

	MAX_Read_Data(REG_FIFO_DATA, (uint8_t *)ach_i2c_data, 6);

	un_temp = (unsigned char)ach_i2c_data[0];
	un_temp <<= 16;
	*pun_red_led += un_temp;
	un_temp = (unsigned char)ach_i2c_data[1];
	un_temp <<= 8;
	*pun_red_led += un_temp;
	un_temp = (unsigned char)ach_i2c_data[2];
	*pun_red_led += un_temp;

	un_temp = (unsigned char)ach_i2c_data[3];
	un_temp <<= 16;
	*pun_ir_led += un_temp;
	un_temp = (unsigned char)ach_i2c_data[4];
	un_temp <<= 8;
	*pun_ir_led += un_temp;
	un_temp = (unsigned char)ach_i2c_data[5];
	*pun_ir_led += un_temp;
	*pun_red_led &= 0x03FFFF; //Mask MSB [23:18]
	*pun_ir_led &= 0x03FFFF;  //Mask MSB [23:18]
}

int MAX30102_INTPin_Read()
{
	hi_gpio_value gpioval;
	hi_gpio_get_output_val (3, &gpioval);
	//printf("%d",gpioval);
	return gpioval;
}