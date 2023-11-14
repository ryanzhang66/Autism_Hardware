#ifndef __MAX30102_H
#define __MAX30102_H

#include <stdio.h>
#include <unistd.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "iot_errno.h" //==IOT_SUCCESS =0 

#include "hi_io.h"   //上拉、复用
#include "hi_gpio.h" //hi_gpio_set_dir()、hi_gpio_set(get)_output(input)_val()
#include "hi_time.h"
#include "hi_i2c.h"

#define MAX_SDA_IO13  13
#define MAX_SCL_IO14  14  
#define MAX_I2C_IDX  0  //hi3861 i2c-1
#define MAX_I2C_BAUDRATE 400*1000 //iic work frequency 400k (400*1000) 

#define MAX30102_WR_address 0xAE
#define I2C_WRITE_ADDR 0xAE
#define I2C_READ_ADDR 0xAF

//register addresses
#define REG_INTR_STATUS_1 0x00
#define REG_INTR_STATUS_2 0x01
#define REG_INTR_ENABLE_1 0x02
#define REG_INTR_ENABLE_2 0x03
#define REG_FIFO_WR_PTR 0x04
#define REG_OVF_COUNTER 0x05
#define REG_FIFO_RD_PTR 0x06
#define REG_FIFO_DATA 0x07
#define REG_FIFO_CONFIG 0x08
#define REG_MODE_CONFIG 0x09
#define REG_SPO2_CONFIG 0x0A
#define REG_LED1_PA 0x0C
#define REG_LED2_PA 0x0D
#define REG_PILOT_PA 0x10
#define REG_MULTI_LED_CTRL1 0x11
#define REG_MULTI_LED_CTRL2 0x12
#define REG_TEMP_INTR 0x1F
#define REG_TEMP_FRAC 0x20
#define REG_TEMP_CONFIG 0x21
#define REG_PROX_INT_THRESH 0x30
#define REG_REV_ID 0xFE //get max30102 infomation 
#define REG_PART_ID 0xFF


extern uint16_t fifo_red;
extern uint16_t fifo_ir;

uint8_t MAX_Cmd(uint8_t regAddr);
uint32_t MAX_Read_Data(uint8_t regAddr, uint8_t *data, unsigned int dataLen);
uint32_t MAX_Write_Data(uint8_t regAddr, uint8_t *data, unsigned int dataLen);
void max30102_init(void);  
void max30102_reset(void);
void maxim_max30102_read_fifo(uint32_t *pun_red_led, uint32_t *pun_ir_led);
int MAX30102_INTPin_Read();					
#endif