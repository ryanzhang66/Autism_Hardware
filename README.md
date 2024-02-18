# 自闭症可穿戴

## 如何使用

- 安装DevTool插件到vscode
- 在源码`src\applications\sample\wifi-iot\app`下新创建一个文件夹,将项目复制进去。
- 设置app文件夹下的`BUILD.gn`
    ```
    lite_component("app") {
        features = [
            "Autism_Hardware:cloud_oc_manhole_cover"
        ]
    }   
    ``` 
- 点击`rebulid`按钮，然后烧录，查看串口

## 代码讲解

### 任务部分
- 创建了4个任务
  - 分别是连接华为云任务
    - `CloudMainTaskEntry`
  - 传感器上云任务两个（mpu6050，max30102）
    - `SensorTaskEntr`
    - `Max30102TaskEntry`
  - max30102采集数据任务
    - `heartrateTask_Init`
    ```c
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
    ``` 

### 传感器采集数据部分

#### MPU6050六轴传感器
    首先，这个传感器使用的是Hi386i芯片引出的硬件IIC1，IIC引脚为GPIO1和GPIO0，驱动代码在项目文件的src\E53_SC2.c
- 引脚说明
    ```
    I2C1_SDA -> GPIO0
    I2C1_SCL -> GPIO1
    ``` 
- 具体驱动代码不做过多说明
- 任务函数部分`SensorTaskEntr`
  - 创建局部变量
    ```c
    app_msg_t *app_msg;
    uint8_t ret;
    E53SC2Data data;
    int X = 0, Y = 0, Z = 0;
    ```  

  - 初始化mpu6050传感器
    ```c
    ret = E53SC2Init();
            if (ret != 0) {
                printf("E53_SC2 Init failed!\r\n");
                return;
            }
    ```  
  - 设置定时器
    ```c
    exec1 = 8U;
    id1 = osTimerNew(Timer1_Callback, osTimerOnce, &exec1, NULL);
    ```  
  - 定时器开始，传感器采集数据
    ```c
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
    ```  
  - 定时器结束，传感器停止采集数据
    ```c
    if (g_app_cb.flag_timer_1 == 1)
    ```  
  - 数据上云
    ```c
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
    ```  
#### MAX30102心率血氧传感器
    首先使用的是Hi3861的硬件IIC0，这里是利用了GPIO13和GPIO14，驱动代码在max30102/max30102.c
- 引脚说明  
    ```
    SDA -> GPIO13
    SCL -> GPIO14
    ```
- 任务函数
  - `heartrateTask_Init`：初始化max30102传感器，以及采集数据
    - 创建局部变量
        ```c
        //max30102变量
        uint32_t un_min, un_max, un_prev_data;  
        int i,j;
        int32_t n_brightness;
        float f_temp;
        uint8_t temp[6];

        //500个中断一次
        int number_500 = 0;
        ```  
    - 初始化IIC等引脚
        ```c
        IoTGpioInit(MAX_SDA_IO13);
        IoTGpioInit(MAX_SCL_IO14);
        hi_io_set_func(MAX_SDA_IO13, HI_IO_FUNC_GPIO_13_I2C0_SDA);
        hi_io_set_func(MAX_SCL_IO14, HI_IO_FUNC_GPIO_14_I2C0_SCL);
        hi_i2c_init(MAX_I2C_IDX, MAX_I2C_BAUDRATE);
        IoTGpioInit(9);
        IoTGpioSetDir(9,IOT_GPIO_DIR_OUT);
        ```  
    - 初始化max30102
        ```c
        max30102_init();
        ```  
    - 读取数据
        ```c
        maxim_max30102_read_fifo(&pun_red_led, &pun_ir_led);
        ```  
    - 算法实时处理数据
        ```c
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
        ```  
  - `Max30102TaskEntry`：将采集到的数据上传到云端，同时具有命令下发功能
    - 命令下发启动
        ```c
        if (g_app_cb.max30102_state == 1)
        ``` 
    - 上传数据
        ```c
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
        ``` 
