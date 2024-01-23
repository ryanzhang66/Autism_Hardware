/*
    首先在华为云上点击命令下发按钮，触发deal_ cmd_ msg函数，再根据命令参数系统自动跳转到对应的参数函数，比如命令参数Max30102_ Begin,
    他就会到deal_ max30102_ cmd函数执行if (strcmp( CJSON_ GetStringValue(obj_ para), "ON") == 0)这个 if判断语句，然后定时器启动，
    传感器就会打开。
    max30102传感器的运行函数static int Max30102TaskEntry(void)因为g_ app_ cb .max30102_ state = 1而运行，采集定时器设定时间的数据，
    此时定时器停止工作，回调函数void Timer2_ Callback(void *arg)返回g_ app_ cb.flag_ timer_ 1 = 1这个定时器的停止标志位，定时器停止
    运行。
    同时，收集到的消息开始上报，我一开始没有加延迟上报，消息会有丢失，因此我加入了循环以及延迟函数来保证消息的全部上报，然后将
    g_ app_ cb.max30102_ state = 0，这样是防止再次进入传感器读取数据的循环，至此整个命令下发的流程到此结束
*/
//C的头文件，以及内核头文件
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cmsis_os2.h"
#include "ohos_init.h"

#include <dtls_al.h>
#include <mqtt_al.h>
#include <oc_mqtt_al.h>
#include <oc_mqtt_profile.h>
#include "E53_SC2.h"
#include "wifi_connect.h"

//ADC的头文件
#include "iot_adc.h"
#include "iot_errno.h"
#include <math.h>
#include "iot_gpio_ex.h"

//max30102的头文件
#include "iot_watchdog.h"
#include "hi_io.h"   //上拉、复用
#include "hi_gpio.h" //hi_gpio_set_dir()、hi_gpio_set(get)_output(input)_val()
#include "iot_gpio.h"//gpioInit
#include "hi_time.h"
#include "hi_i2c.h"
#include "max30102.h"
#include "./max30102/algorithm.h"

#define CONFIG_WIFI_SSID "yuizhu" // 修改为自己的WiFi 热点账号
#define CONFIG_WIFI_PWD "20040906" // 修改为自己的WiFi 热点密码
#define CONFIG_APP_SERVERIP "117.78.5.125"  //这是华为云平台标准版的IP地址
#define CONFIG_APP_SERVERPORT "1883"        //这是华为云开放MQTT端口，开放的还有8883端口
#define CONFIG_APP_DEVICEID "6553a1e616c4bf776318127e_20231115" // 替换为注册设备后生成的deviceid
#define CONFIG_APP_DEVICEPWD "zhangran20040906" // 替换为注册设备后生成的密钥

//以下3个参数（CLIENT_ID，USERNAME，PASSWORD）可以由HMACSHA256算法生成
//为硬件通过MQTT协议接入华为云IoT平台的鉴权依据，但是在本程序没有使用以下三个参数
#define CONFIG_APP_LIFETIME 60 // < seconds
#define CONFIG_QUEUE_TIMEOUT (5 * 1000)

#define MSGQUEUE_COUNT 16
#define MSGQUEUE_SIZE 10
#define CLOUD_TASK_STACK_SIZE (1024 * 10)
#define CLOUD_TASK_PRIO 24
#define SENSOR_TASK_STACK_SIZE (1024 * 4)
#define SENSOR_TASK_PRIO 25
#define TASK_DELAY 3
#define FLIP_THRESHOLD 100

//ADC的宏定义
#define ADC_CHANNEL_1 5
#define ADC_CHANNEL_2 4
#define ADC_GPIO 11
#define ADC_VREF_VOL 1.8
#define ADC_COEFFICIENT 4
#define ADC_RATIO 4096

//max30102的宏定义
#define MAX_SDA_IO0  0
#define MAX_SCL_IO1  1  
#define MAX_I2C_IDX  1  //hi3861 i2c-1
#define MAX_I2C_BAUDRATE 400*1000 //iic work frequency 400k (400*1000) 

//这里是max30102的IIC的寄存器地址
#define MAX30102_WR_address 0xAE
#define I2C_WRITE_ADDR 0xAE
#define I2C_READ_ADDR 0xAF

//这里是Max30102的寄存器的地址
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
#define REG_REV_ID 0xFE  //get max30102 infomation 
#define REG_PART_ID 0xFF

osMessageQueueId_t mid_MsgQueue;

//这里是定义枚举，在CloudMainTaskEntry线程函数中做switch的case选项
typedef enum {
    en_msg_cmd = 0,
    en_msg_mpu6050_report,
    en_msg_max30102_report,
    en_msg_conn,
    en_msg_disconn,
} en_msg_type_t;

//云平台的请求参数，在msg_rcv_callback函数中使用
typedef struct {
    char *request_id;
    char *payload;
} cmd_t;

//这里定义mpu6050消息上报的结构体
typedef struct {
    int temp;
    int acce_x;
    int acce_y;
    int acce_z;
    int gyro_x;
    int gyro_y;
    int gyro_z;
    char band_1[10000];
} report_mpu6050_t;

//这里定义max30102消息上报的结构体
typedef struct {
    int red;
    int ir;
}report_max30102_t;

//这里是定义消息打包的ID
typedef struct {
    en_msg_type_t msg_type;
    union {
        cmd_t cmd;
        report_mpu6050_t report_mpu6050;
        report_max30102_t report_max30102;
    } msg;
} app_msg_t;

//定义程序需要的参数变量
typedef struct {
    osMessageQueueId_t app_msg;
    int connected;
    int mpu6050;       //mpu6050的命令下发状态参数，类似于mpu6050.value = g_app_cb.mpu6050 ? "ON" : "OFF";
    int mpu6050_state;   //mpu6050传感器的状态参数
    int max30102;      //max30102的命令下发状态参数，类似于max30102.value = g_app_cb.max30102 ? "ON" : "OFF";
    int max30102_state;  //max30102传感器的状态参数

    int flag_timer_1;     //判断运动检测时间是否到达
    int flag_timer_2;     //判断心率检测时间是否到达
} app_cb_t;

static app_cb_t g_app_cb;
int g_coverStatus = 1;
int state = 0;
uint32_t *pun_red_led;  //max30102红光数据的指针参数
uint32_t *pun_ir_led;   //max30102红外光数据的指针参数

uint32_t aun_ir_buffer[500]; //IR LED sensor data   红外数据，用于计算血氧
int32_t n_ir_buffer_length=500;    //data length  
uint32_t aun_red_buffer[500];    //Red LED sensor data  红光数据，用于计算心率曲线以及计算心率

int32_t n_sp02; //SPO2 value
int8_t ch_spo2_valid;   //indicator to show if the SP02 calculation is valid
int32_t n_heart_rate;   //heart rate value
int8_t  ch_hr_valid;    //indicator to show if the heart rate calculation is valid
#define MAX_BRIGHTNESS 255

osTimerId_t ID1,ID2;  //定时器的ID

// 定时器的参数
osTimerId_t id1, id2, id3, id4;
uint32_t timerDelay;
osStatus_t status_1, status_2, status_3, status_4;
uint32_t exec1, exec2, exec3, exec4;

int ACCEL_x[100],ACCEL_y[100],ACCEL_z[100],GYRO_x[100],GYRO_y[100],GYRO_z[100];  //定义mpu6050参数数组，作为运动数据的缓存区
unsigned int RED[1000],IR[1000];    //定义max30102参数数组，作为运动数据的缓存区

/***** 定时器1 回调函数 *****/
void Timer1_Callback(void *arg)
{
    (void)arg;
    g_app_cb.flag_timer_1 = 1;
}

/***** 定时器2 回调函数 *****/
void Timer2_Callback(void *arg)
{
    (void)arg;
    g_app_cb.flag_timer_2 = 1;
}

void VCC(void)
{
    IoTGpioSetOutputVal(7,1);
    IoTGpioSetOutputVal(8,1);
}

/*
    函数名：max30102_report_msg
    参数：report_max30102_t *report_max30102
    作用：max30102的消息上报函数,定义了max30102的消息队列
*/
static void max30102_report_msg(report_max30102_t *report_max30102)
{
    oc_mqtt_profile_service_t service;
    oc_mqtt_profile_kv_t Red;
    oc_mqtt_profile_kv_t Ir;
    oc_mqtt_profile_kv_t max30102;

    if (g_app_cb.connected != 1) {
        return;
    }

    service.event_time = NULL;
    service.service_id = "Manhole_Cover";
    service.service_property = &Red;
    service.nxt = NULL;

    Red.key = "Red";
    Red.value = &report_max30102->red;
    Red.type = EN_OC_MQTT_PROFILE_VALUE_INT;
    Red.nxt = &Ir;

    Ir.key = "Ir";
    Ir.value = &report_max30102->ir;
    Ir.type = EN_OC_MQTT_PROFILE_VALUE_INT;
    Ir.nxt = &max30102;

    max30102.key = "MAX30102Status";
    max30102.value = g_app_cb.max30102 ? "ON" : "OFF";
    max30102.type = EN_OC_MQTT_PROFILE_VALUE_STRING;
    max30102.nxt = NULL;

    oc_mqtt_profile_propertyreport(NULL, &service);
    return;
}

/*
    函数名：mpu6050_report_msg
    参数：report_mpu6050_t *report_mpu6050
    作用：mpu6050的消息上报函数，定义了mpu6050的消息队列
*/
static void mpu6050_report_msg(report_mpu6050_t *report_mpu6050)
{
    oc_mqtt_profile_service_t service;
    oc_mqtt_profile_kv_t temperature;
    oc_mqtt_profile_kv_t Accel_x;
    oc_mqtt_profile_kv_t Accel_y;
    oc_mqtt_profile_kv_t Accel_z;
    oc_mqtt_profile_kv_t Gyro_x;
    oc_mqtt_profile_kv_t Gyro_y;
    oc_mqtt_profile_kv_t Gyro_z;
    oc_mqtt_profile_kv_t voltage_1;
    oc_mqtt_profile_kv_t Red;
    oc_mqtt_profile_kv_t Ir;
    oc_mqtt_profile_kv_t mpu6050;
    oc_mqtt_profile_kv_t max30102;
    

    if (g_app_cb.connected != 1) {
        return;
    }

    service.event_time = NULL;
    service.service_id = "Manhole_Cover";
    service.service_property = &temperature;
    service.nxt = NULL;

    temperature.key = "Temperature";
    temperature.value = &report_mpu6050->temp;
    temperature.type = EN_OC_MQTT_PROFILE_VALUE_INT;
    temperature.nxt = &Accel_x;

    Accel_x.key = "Accel_x";
    Accel_x.value = &report_mpu6050->acce_x;
    Accel_x.type = EN_OC_MQTT_PROFILE_VALUE_INT;
    Accel_x.nxt = &Accel_y;

    Accel_y.key = "Accel_y";
    Accel_y.value = &report_mpu6050->acce_y;
    Accel_y.type = EN_OC_MQTT_PROFILE_VALUE_INT;
    Accel_y.nxt = &Accel_z;

    Accel_z.key = "Accel_z";
    Accel_z.value = &report_mpu6050->acce_z;
    Accel_z.type = EN_OC_MQTT_PROFILE_VALUE_INT;
    Accel_z.nxt = &Gyro_x;

    Gyro_x.key = "Gyro_x";
    Gyro_x.value = &report_mpu6050->gyro_x;
    Gyro_x.type = EN_OC_MQTT_PROFILE_VALUE_INT;
    Gyro_x.nxt = &Gyro_y;

    Gyro_y.key = "Gyro_y";
    Gyro_y.value = &report_mpu6050->gyro_y;
    Gyro_y.type = EN_OC_MQTT_PROFILE_VALUE_INT;
    Gyro_y.nxt = &Gyro_z;

    Gyro_z.key = "Gyro_z";
    Gyro_z.value = &report_mpu6050->gyro_z;
    Gyro_z.type = EN_OC_MQTT_PROFILE_VALUE_INT;
    Gyro_z.nxt = &voltage_1;

    voltage_1.key = "Angle_1";
    voltage_1.value = &report_mpu6050->band_1;
    voltage_1.type = EN_OC_MQTT_PROFILE_VALUE_STRING;
    voltage_1.nxt = &mpu6050;

    mpu6050.key = "MPU6050Status";
    mpu6050.value = g_app_cb.mpu6050 ? "ON" : "OFF";
    mpu6050.type = EN_OC_MQTT_PROFILE_VALUE_STRING;
    mpu6050.nxt = NULL;

    oc_mqtt_profile_propertyreport(NULL, &service);
    return;
}

/*
    函数名：msg_rcv_callback
    参数：oc_mqtt_profile_msgrcv_t *msg
    作用：使用此函数将所有消息推送到缓冲区
*/
static int msg_rcv_callback(oc_mqtt_profile_msgrcv_t *msg)
{
    int ret = 0;
    char *buf;
    int buf_len;
    app_msg_t *app_msg;

    if ((msg == NULL) || (msg->request_id == NULL) || (msg->type != EN_OC_MQTT_PROFILE_MSG_TYPE_DOWN_COMMANDS)) {
        return ret;
    }

    buf_len = sizeof(app_msg_t) + strlen(msg->request_id) + 1 + msg->msg_len + 1;
    buf = malloc(buf_len);
    if (buf == NULL) {
        return ret;
    }
    app_msg = (app_msg_t *)buf;
    buf += sizeof(app_msg_t);

    app_msg->msg_type = en_msg_cmd;
    app_msg->msg.cmd.request_id = buf;
    buf_len = strlen(msg->request_id);
    buf += buf_len + 1;
    memcpy_s(app_msg->msg.cmd.request_id, buf_len, msg->request_id, buf_len);
    app_msg->msg.cmd.request_id[buf_len] = '\0';

    buf_len = msg->msg_len;
    app_msg->msg.cmd.payload = buf;
    memcpy_s(app_msg->msg.cmd.payload, buf_len, msg->msg, buf_len);
    app_msg->msg.cmd.payload[buf_len] = '\0';

    ret = osMessageQueuePut(g_app_cb.app_msg, &app_msg, 0U, CONFIG_QUEUE_TIMEOUT);
    if (ret != 0) {
        free(app_msg);
    }

    return ret;
}

/*
    函数名：oc_cmdresp
    参数：cmd_t *cmd,int cmdret
    作用：向华为云平台返回请求
*/
static void oc_cmdresp(cmd_t *cmd, int cmdret)
{
    oc_mqtt_profile_cmdresp_t cmdresp;
    ///< do the response
    cmdresp.paras = NULL;
    cmdresp.request_id = cmd->request_id;
    cmdresp.ret_code = cmdret;
    cmdresp.ret_name = NULL;
    (void)oc_mqtt_profile_cmdresp(NULL, &cmdresp);
}

///< COMMAND DEAL
#include <cJSON.h>

/*
    函数名：deal_mpu6050_cmd
    参数：cmd_t *cmd, cJSON *obj_root
    作用：用来开启或关闭mpu6050的传感器，本来的mpu6050传感器线程函数是开启的，只不过我用if+状态参数跳过了数据采集与上云
*/
static void deal_mpu6050_cmd(cmd_t *cmd, cJSON *obj_root)
{
    cJSON *obj_paras;
    cJSON *obj_para;
    int cmdret;

    obj_paras = cJSON_GetObjectItem(obj_root, "paras");
    if (obj_paras == NULL) {
        cJSON_Delete(obj_root);
    }
    obj_para = cJSON_GetObjectItem(obj_paras, "kneading_control");      //对应华为云IoTDA的产品——>模型的命令名称——>下发参数
    if (obj_paras == NULL) {
        cJSON_Delete(obj_root);
    }
    ///< operate the LED here
    if (strcmp(cJSON_GetStringValue(obj_para), "ON") == 0) {
        g_app_cb.mpu6050 = 1;           //对应消息队列里定义的ON
            g_app_cb.mpu6050_state = 1; //打开mpu6050传感器
            printf("mpu6050传感器 On!");
    } else {
        g_app_cb.mpu6050 = 0;           //对应消息队列里定义的OFF
            g_app_cb.mpu6050_state = 0; //打开mpu6050传感器
            printf("mpu6050传感器 Off!");
    }
    cmdret = 0;
    oc_cmdresp(cmd, cmdret);

    cJSON_Delete(obj_root);
    return;
}

/*
    函数名：deal_max30102_cmd
    参数：cmd_t *cmd, cJSON *obj_root
    作用：用来开启或关闭max30102的传感器，本来的max30102传感器线程函数是开启的，只不过我用if+状态参数跳过了数据采集与上云
*/
static void deal_max30102_cmd(cmd_t *cmd, cJSON *obj_root)
{
    cJSON *obj_paras;
    cJSON *obj_para;
    int cmdret;

    obj_paras = cJSON_GetObjectItem(obj_root, "Paras");
    if (obj_paras == NULL) {
        cJSON_Delete(obj_root);
    }
    obj_para = cJSON_GetObjectItem(obj_paras, "Max30102_Begin_control");    //对应华为云IoTDA的产品——>模型的命令名称——>下发参数
    if (obj_para == NULL) {
        cJSON_Delete(obj_root);
    }
    ///< operate the Motor here
    if (strcmp(cJSON_GetStringValue(obj_para), "ON") == 0) {
        g_app_cb.max30102 = 1;
            g_app_cb.max30102_state = 1;
            printf("max30102传感器 On!");
    } else {
        g_app_cb.max30102 = 0;
            g_app_cb.max30102_state = 0;
            printf("max30102传感器 Off!");
    }
    cmdret = 0;
    oc_cmdresp(cmd, cmdret);

    cJSON_Delete(obj_root);
    return;
}

/*
    函数名：deal_cmd_msg
    参数：cmd_t *cmd
    作用：定义命令下发的命令名称，同时可以调用对应的命令执行函数
*/
static void deal_cmd_msg(cmd_t *cmd)
{
    cJSON *obj_root;
    cJSON *obj_cmdname;

    int cmdret = 1;
    obj_root = cJSON_Parse(cmd->payload);
    if (obj_root == NULL) {
        oc_cmdresp(cmd, cmdret);
    }
    obj_cmdname = cJSON_GetObjectItem(obj_root, "command_name");
    if (obj_cmdname == NULL) {
        cJSON_Delete(obj_root);
    }
    if (strcmp(cJSON_GetStringValue(obj_cmdname), "Kneading") == 0) {       //对应华为云IoTDA的产品——>模型的命令名称
        deal_mpu6050_cmd(cmd, obj_root);
    } else if (strcmp(cJSON_GetStringValue(obj_cmdname), "Max30102_Begin") == 0) {  //对应华为云IoTDA的产品——>模型的命令名称
        deal_max30102_cmd(cmd, obj_root);
    }
    return;
} 

/*
    函数名：CloudMainTaskEntry
    参数：空
    作用：这是一个上云的线程函数，包含初次的WIFI的连接，检查与华为云MQTT服务器的连通性，同时实时检测华为云平台命令下发
*/
static int CloudMainTaskEntry(void)
{
    app_msg_t *app_msg;
    uint32_t ret;

    WifiConnect(CONFIG_WIFI_SSID, CONFIG_WIFI_PWD);
    dtls_al_init();
    mqtt_al_init();
    oc_mqtt_init();

    g_app_cb.app_msg = osMessageQueueNew(MSGQUEUE_COUNT, MSGQUEUE_SIZE, NULL);
    if (g_app_cb.app_msg == NULL) {
        printf("Create receive msg queue failed");
    }
    oc_mqtt_profile_connect_t connect_para;
    (void)memset_s(&connect_para, sizeof(connect_para), 0, sizeof(connect_para));

    connect_para.boostrap = 0;
    connect_para.device_id = CONFIG_APP_DEVICEID;
    connect_para.device_passwd = CONFIG_APP_DEVICEPWD;
    connect_para.server_addr = CONFIG_APP_SERVERIP;
    connect_para.server_port = CONFIG_APP_SERVERPORT;
    connect_para.life_time = CONFIG_APP_LIFETIME;
    connect_para.rcvfunc = msg_rcv_callback;
    connect_para.security.type = EN_DTLS_AL_SECURITY_TYPE_NONE;
    ret = oc_mqtt_profile_connect(&connect_para);
    if ((ret == (int)en_oc_mqtt_err_ok)) {
        g_app_cb.connected = 1;
        printf("oc_mqtt_profile_connect succed!\r\n");
    } else {
        printf("oc_mqtt_profile_connect faild!\r\n");
    }

    //检测命令下发
    while (1) {
        app_msg = NULL;
        (void)osMessageQueueGet(g_app_cb.app_msg, (void **)&app_msg, NULL, 0xFFFFFFFF);
        if (app_msg != NULL) {
            switch (app_msg->msg_type) {
                case en_msg_cmd:
                    deal_cmd_msg(&app_msg->msg.cmd);  //主的命令名称
                    break;
                case en_msg_mpu6050_report:
                    mpu6050_report_msg(&app_msg->msg.report_mpu6050);  //副的下发参数
                    break;
                case en_msg_max30102_report:
                    max30102_report_msg(&app_msg->msg.report_max30102); //副的下发参数
                    break;
                default:
                    break;
            }
            free(app_msg); //释放堆空间
        }
    }
    return 0;
}

/*
   ADC获取电压（弯曲传感器 1）
*/
static float GetVoltage1(void)
{
    unsigned int ret;
    unsigned short data;

    ret=IoTAdcRead(ADC_CHANNEL_1,&data,IOT_ADC_EQU_MODEL_8,IOT_ADC_CUR_BAIS_DEFAULT,0xff);
    if (ret != IOT_SUCCESS){
        printf("Failed to read adc!\r\n");
    }
    return (float)data * ADC_VREF_VOL * ADC_COEFFICIENT / ADC_RATIO;
}

/*
    函数名：map
    参数：float val, float I_Min, float I_Max, float O_Min, float O_Max
    作用：映射数据，用比例完成映射区间
*/
float map(float val, float I_Min, float I_Max, float O_Min, float O_Max)
	{
		return(((val-I_Min)*((O_Max-O_Min)/(I_Max-I_Min)))+O_Min);
    }

//max30102获取红光和红外光数据
/*
    函数名：heartrateTask_Init
    参数：空
    作用：初始化max30102传感器
*/
static void *heartrateTask_Init(const char *arg)
{
    //max30102变量
	uint32_t un_min, un_max, un_prev_data;  
	int i,j;
	int32_t n_brightness;
	float f_temp;
	uint8_t temp[6];

    //500个中断一次
    int number_500 = 0;

    IoTGpioInit(MAX_SDA_IO13);
	IoTGpioInit(MAX_SCL_IO14);
    hi_io_set_func(MAX_SDA_IO13, HI_IO_FUNC_GPIO_13_I2C0_SDA);
    hi_io_set_func(MAX_SCL_IO14, HI_IO_FUNC_GPIO_14_I2C0_SCL);
    hi_i2c_init(MAX_I2C_IDX, MAX_I2C_BAUDRATE);
	IoTGpioInit(9);
    IoTGpioSetDir(9,IOT_GPIO_DIR_OUT);
    max30102_init();
    while(1)
    {
        maxim_max30102_read_fifo(&pun_red_led, &pun_ir_led);
        // printf("--------------------------\r\n");
        // printf("red:%d\r\n",pun_red_led);
        aun_red_buffer[number_500] = pun_red_led;
        // printf("ir:%d\r\n",pun_ir_led);
        aun_ir_buffer[number_500] = pun_ir_led;
        // printf("--------------------------");
        number_500 ++;
        osDelay(10U);

        un_min=0x3FFFF;
	    un_max=0;
        n_ir_buffer_length=200; //缓冲区长度100存储5秒的运行在100sps的样本
	    //读取前500个样本，确定信号范围
        if (number_500 % 200 == 0)
        {
            for(i=0;i<n_ir_buffer_length;i++)
            {
                // aun_red_buffer[i] =  pun_red_led;  //  将值合并得到实际数字
                // aun_ir_buffer[i] =   pun_ir_led; //  将值合并得到实际数字
                if(un_min>aun_red_buffer[i])
                        un_min=aun_red_buffer[i];//更新计算最小值
                if(un_max<aun_red_buffer[i])
                        un_max=aun_red_buffer[i];//更新计算最大值
            }
            
            //在计算心率前取100组样本，取的数据放在400-500缓存数组中
            for(i=100;i<200;i++){
                un_prev_data=aun_red_buffer[i-1];//临时记录上一次读取数据
                if(aun_red_buffer[i]>un_prev_data){//用新获取的一个数值与上一个数值对比
                    f_temp=aun_red_buffer[i]-un_prev_data;
                    f_temp /=(un_max-un_min);
                    f_temp*=MAX_BRIGHTNESS;//公式（心率曲线）=（新数值-旧数值）/（最大值-最小值）*255
                    n_brightness-=(int)f_temp;
                    if(n_brightness<0)
                        n_brightness=0;
                }
                else{
                    f_temp=un_prev_data-aun_red_buffer[i];
                    f_temp/=(un_max-un_min);
                    f_temp*=MAX_BRIGHTNESS;//公式（心率曲线）=（旧数值-新数值）/（最大值-最小值）*255
                    n_brightness+=(int)f_temp;
                    if(n_brightness>MAX_BRIGHTNESS)
                        n_brightness=MAX_BRIGHTNESS;
                }
						
            //un_prev_data=aun_red_buffer[i];
            //获取数据最后一个数值，没有用，主程序中未马上使用被替换
            //计算前500个样本(前5秒样本)后的心率和SpO2
            maxim_heart_rate_and_oxygen_saturation(aun_ir_buffer, n_ir_buffer_length, aun_red_buffer, &n_sp02, &ch_spo2_valid, &n_heart_rate, &ch_hr_valid); 
            }
            printf("%d\n",n_heart_rate);
            printf("%d\n",(int)n_sp02);
            number_500 = 0;
        }
    }
}

static int SensorTaskEntry(void)
{
    app_msg_t *app_msg;
    uint8_t ret;
    E53SC2Data data;
    int X = 0, Y = 0, Z = 0;
    ret = E53SC2Init();
            if (ret != 0) {
                printf("E53_SC2 Init failed!\r\n");
                return;
            }
    // 定时器1：用于运动检测，目前设置时间为8秒触发
    exec1 = 8U;
    id1 = osTimerNew(Timer1_Callback, osTimerOnce, &exec1, NULL);


    while (1) {
        
        if (g_app_cb.mpu6050_state == 1)
        {
            int i=0,j=0;
            timerDelay = 500U;
            status_1 = osTimerStart(id1, timerDelay); // 定时器开始
            while (1)
            {
                ret = E53SC2ReadData(&data);
                if (ret != 0) {
                    printf("E53_SC2 Read Data!\r\n");
                    return;
                }
                printf("\r\n**************Temperature      is  %d\r\n", (int)data.Temperature);
                ACCEL_x[i]=data.Accel[ACCEL_X_AXIS];
                printf("\r\n**************Accel[0]         is  %lf\r\n", data.Accel[ACCEL_X_AXIS]);
                ACCEL_y[i]=data.Accel[ACCEL_Y_AXIS];
                printf("\r\n**************Accel[1]         is  %lf\r\n", data.Accel[ACCEL_Y_AXIS]);
                ACCEL_z[i]=data.Accel[ACCEL_Z_AXIS];
                printf("\r\n**************Accel[2]         is  %lf\r\n", data.Accel[ACCEL_Z_AXIS]);
                GYRO_x[i]=data.Gyro[ACCEL_X_AXIS];
                printf("\r\n**************Gyro[0]         is  %lf\r\n", data.Gyro[ACCEL_X_AXIS]);
                GYRO_y[i]=data.Gyro[ACCEL_Y_AXIS];
                printf("\r\n**************Gyro[1]         is  %lf\r\n", data.Gyro[ACCEL_Y_AXIS]);
                GYRO_z[i]=data.Gyro[ACCEL_Z_AXIS];
                printf("\r\n**************Gyro[2]         is  %lf\r\n", data.Gyro[ACCEL_Z_AXIS]);
                i=i+1;
                float Voltage_1,Voltage_2;
                Voltage_1 = GetVoltage1();
                if (Voltage_1 > 1010)
                {
                    Voltage_1 = 1010;
                }
                else if (Voltage_2 <450)
                {
                    Voltage_1 = 450;
                }
                printf("vlt:%.3f\n",Voltage_1);
                float flex_angle_1 = map(Voltage_1,1010,450,0,90);
                printf("Angle_1:%.2f\n",flex_angle_1);
                usleep(250000);

                if (X == 0 && Y == 0 && Z == 0) {
                    X = (int)data.Accel[ACCEL_X_AXIS];
                    Y = (int)data.Accel[ACCEL_Y_AXIS];
                    Z = (int)data.Accel[ACCEL_Z_AXIS];
                } else {
                    if (X + FLIP_THRESHOLD < data.Accel[ACCEL_X_AXIS] || X - FLIP_THRESHOLD > data.Accel[ACCEL_X_AXIS] ||
                        Y + FLIP_THRESHOLD < data.Accel[ACCEL_Y_AXIS] || Y - FLIP_THRESHOLD > data.Accel[ACCEL_Y_AXIS] ||
                        Z + FLIP_THRESHOLD < data.Accel[ACCEL_Z_AXIS] || Z - FLIP_THRESHOLD > data.Accel[ACCEL_Z_AXIS]) {
                        LedD1StatusSet(OFF);
                        LedD2StatusSet(ON);
                        g_coverStatus = 1;
                    } else {
                        LedD1StatusSet(ON);
                        LedD2StatusSet(OFF);
                        g_coverStatus = 0;
                    }
                }
                printf("__________________________________________________%d_______________________________________________________________",g_app_cb.flag_timer_1);
                if (g_app_cb.flag_timer_1 == 1)
                {
                    app_msg = malloc(sizeof(app_msg_t));
                    if (app_msg != NULL) {
                        for (j=0; j<i; j++)
                        {
                            printf("\nj:%d",j);
                            printf("\nAccel_x:%d",ACCEL_x[j]);
                            app_msg->msg_type = en_msg_mpu6050_report;
                        app_msg->msg.report_mpu6050.temp = (int)data.Temperature;
                        app_msg->msg.report_mpu6050.acce_x = ACCEL_x[j];
                        app_msg->msg.report_mpu6050.acce_y = ACCEL_y[j];
                        app_msg->msg.report_mpu6050.acce_z = ACCEL_z[j];
                        app_msg->msg.report_mpu6050.gyro_x = GYRO_x[j];
                        app_msg->msg.report_mpu6050.gyro_y = GYRO_y[j];
                        app_msg->msg.report_mpu6050.gyro_z = GYRO_z[j];
                        sprintf(app_msg->msg.report_mpu6050.band_1,"%.2f",flex_angle_1);
                        if (osMessageQueuePut(g_app_cb.app_msg, &app_msg, 0U, CONFIG_QUEUE_TIMEOUT) != 0) {
                            free(app_msg);
                        }
                        osDelay(50U);
                        }
                        
                        
                    }
                    g_app_cb.flag_timer_1 = 0;
                    g_app_cb.mpu6050_state = 0;
                    status_1 = osTimerStop(id1);
                    i=0;
                    j=0;
                    break;
                }
                

                if (g_app_cb.mpu6050_state == 0)
                {
                    status_1 = osTimerStop(id1);
                    break;
                    i=0;
                    j=0;
                }
            }
        }
        
        sleep(TASK_DELAY);   
    }
    return 0;
}

static int Max30102TaskEntry(void)
{
    

    app_msg_t *app_msg;
    uint8_t ret;
    E53SC2Data data;

    while (1) {
        int i,j;

        if (g_app_cb.max30102_state == 1)
        {

            while (1)
            {
                    app_msg = malloc(sizeof(app_msg_t));
                    if (app_msg != NULL) {
                            printf("wwww");
                            app_msg->msg_type = en_msg_max30102_report;
                            app_msg->msg.report_max30102.red = (int)n_heart_rate;
                            app_msg->msg.report_max30102.ir = (int)n_sp02;
                            if (osMessageQueuePut(g_app_cb.app_msg, &app_msg, 0U, CONFIG_QUEUE_TIMEOUT) != 0) {
                            free(app_msg);
                            }
                            osDelay(30U);

                    }
                    g_app_cb.flag_timer_1 = 0;
                    g_app_cb.max30102_state = 0;
                    status_1 = osTimerStop(id1);
                    i=0;
                    j=0;
                    break;
                
                if (g_app_cb.max30102_state == 0)
                {
                    status_1 = osTimerStop(id1);
                    i=0;
                    j=0;
                    break;
                }
            }
        }
        
        sleep(TASK_DELAY);
        
    }
    return 0;
}

static void IotMainTaskEntry(void)
{
    
    osThreadAttr_t attr;
    attr.name = "CloudMainTaskEntry";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = CLOUD_TASK_STACK_SIZE;
    attr.priority = CLOUD_TASK_PRIO;

    if (osThreadNew((osThreadFunc_t)CloudMainTaskEntry, NULL, &attr) == NULL) {
        printf("Failed to create CloudMainTaskEntry!\n");
    }
    
    attr.stack_size = SENSOR_TASK_STACK_SIZE;
    attr.priority = SENSOR_TASK_PRIO;
    attr.name = "SensorTaskEntry";
    if (osThreadNew((osThreadFunc_t)SensorTaskEntry, NULL, &attr) == NULL) {
        printf("Failed to create SensorTaskEntry!\n");
    }
    
    attr.stack_size = SENSOR_TASK_STACK_SIZE;
    attr.priority = SENSOR_TASK_PRIO;
    attr.name = "Max30102TaskEntry";
    if (osThreadNew((osThreadFunc_t)Max30102TaskEntry, NULL, &attr) == NULL) {
        printf("Failed to create Max30102TaskEntry!\n");
    }

    attr.stack_size = SENSOR_TASK_STACK_SIZE;
    attr.priority = 26;
    attr.name = "heartrateTask_Init";
    if (osThreadNew((osThreadFunc_t)heartrateTask_Init, NULL, &attr) == NULL) {
        printf("Failed to create heartrateTask_Init!\n");
    }
}
APP_FEATURE_INIT(IotMainTaskEntry);