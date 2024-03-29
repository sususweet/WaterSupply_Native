#include <math.h>
#include "stdio.h"
#include "msp430f5438a.h"
#include "settings.h"
#include "intrinsics.h"
#include "in430.h"
#include "LCD12864.h"
#include "Keyboard.h"
#include "ADC.h"
#include "ThreePhaseSpwm.h"
#include "frequency_capture.h"
#include "Clock.h"
#include "PID.h"
#include "RS232.h"

/**
 * main.c
 */

#define KEY_WAIT 4    /*键盘扫描延迟周期*/
#define NONE_KEY_CODE 0xFF
#define NONE_KEY_NUM 0
#define LCD_TWINKLE_FREQ 40    /*LCD闪烁周期 400ms*/
#define LCD_PRESSURE_UPDATE 150    /*LCD闪烁周期 500ms*/
#define PID_CALCULATE_FREQ 200  /*PID计算周期 1.6s*/
//#define PID_WAITING_TIME 160

/*为减小占用空间，以正整数形式存储，使用时除10*/
#define MAX_STANDBY_PRESSURE 150
#define MAX_WORKING_PRESSURE Fc_to_Pressure(Max_Fc) * 10
#define MIN_WORKING_PRESSURE Fc_to_Pressure(Min_Fc) * 10
#define DEFAULT_STANDBY_PRESSURE 80
#define DEFAULT_WORKING_PRESSURE 100
/*注释结束*/

#define WATER_FLOW_THRESHOLD 3.0

extern pid PIDFreq;

unsigned char REC_BUF[11];       //接收缓存区
char datatype[5]={'P','F','W','S','M'};  //水压 流量 供水压力 待机压力 电机状态


enum setting_state {
    NORMAL, STANDBY1, STANDBY2, STANDBY3, WORKING1, WORKING2, WORKING3
};

enum motor_state {
    MOTOR_WORKING, MOTOR_STOPPED
};

/*unsigned int */
float pressureArray[300] =  {0};
unsigned int pressureArrayIndex = 0;
float frequencyArray[50] =  {0};
unsigned int frequencyArrayIndex = 0;
unsigned int FcArray[200] =  {0};
unsigned int FcArrayIndex = 0;

unsigned int freq_periodStart = 0;
unsigned int freq_pulseEnd = 0;
unsigned int freq_periodEnd = 0;
unsigned char freq_overflow = 0;             //防止溢出
unsigned char cap_flag = 0;
volatile float frequency;
float frequency_last;
volatile float Capture_voltage;

unsigned int Set_Fc = Fc_Default;
//unsigned int Capture_Fc = Fc_Default;
unsigned int Sent_Fc = Fc_Default;

/*为减小占用空间，以正整数形式存储，使用时除10*/
unsigned int standbyPressure = DEFAULT_STANDBY_PRESSURE;
unsigned int workingPressure = DEFAULT_WORKING_PRESSURE;
float Set_Pressure = 0;

unsigned char setting_stage = NORMAL;
unsigned char motor_stage = MOTOR_STOPPED;
unsigned char lcd_twinkle_cursor = 0;
unsigned char lcd_twinkle_num = 0;
unsigned char lcd_pressure_num = LCD_PRESSURE_UPDATE;
unsigned char pid_calculate_num = 0;
unsigned char com_sendtype = 0;
//unsigned char pid_waiting_flag = 1;

unsigned char water_flow_change_flag = 0;
unsigned char standbyPressureChangeFlag = 1;
unsigned char workingPressureChangeFlag = 1;
unsigned char FCChangeFlag = 1;

const unsigned char line1[16] = {"恒压变频供水系统"};
//const unsigned char line1[16] = {"FC  "};
const unsigned char line2[16] = {"待机压力："};
const unsigned char line3[16] = {"供水压力："};
const unsigned char line41[8] = {"水压"};
const unsigned char line42[8] = {"流量"};
unsigned char displayCache[9];

void LCD_Init_Show();

void LCD_Show_Update();

void LCD_Twinkle_Update();

void opr_key(unsigned char key_num);

void Change_Fc_PID();

void Operate_motor();

void scan_key() {
    static unsigned char key_state = KEY_STATE_RELEASE;   /*状态机状态初始化，采用static保存状态*/
    static unsigned char key_code = NONE_KEY_CODE;
    static unsigned char key_num = NONE_KEY_NUM;
    unsigned char pressed = press_key(); /*press_key为检测是否有按键按下的函数*/
    static unsigned char scan_time = 0;
    switch (key_state) {
        case KEY_STATE_RELEASE: {   /*若原始状态为无按键按下RELEASE，同时又检测到按键按下，则状态转换到WAITING*/
            if (pressed == 1) {
                key_state = KEY_STATE_WAITING;
            }
            break;
        }
        case KEY_STATE_WAITING: {   /*原始状态为WAITING，对按键进行多次判断*/
            if (pressed) {
                scan_time++;
                if (scan_time >= KEY_WAIT) {   /*若按键按下的时间超过一定时间，则认为按键按下，读按键*/
                    key_state = KEY_STATE_PRESSED;
                    scan_time = 0;
                    key_code = read_key();  /*read_key为读按键的函数*/
                }
            } else {    /*若按键松开，则恢复到初始状态*/
                scan_time = 0;
                key_state = KEY_STATE_RELEASE;
            }
            break;
        }
        case KEY_STATE_PRESSED: {   /*若按键被确认按下，则等待按键松开再进行操作*/
            if (pressed == 0) {
                key_num = translate_key(key_code);
                opr_key(key_num);  /*opr_key为按键事件响应函数*/
                key_state = KEY_STATE_RELEASE;
                key_code = NONE_KEY_CODE;
                key_num = NONE_KEY_NUM;
            }
            break;
        }
        default:{
            key_state = KEY_STATE_RELEASE;
            break;
        }

    }
}


/*#pragma vector = TIMER0_A0_VECTOR
__interrupt void Timer_A(void) {              // 1s溢出中断
    //_NOP();
}*/

#pragma vector = TIMER0_A1_VECTOR
__interrupt void Timer_A1(void) {             // 10ms溢出中断
    /*0Eh Timer overflow TAxCTL TAIFG Lowest*/
    switch (TA0IV) {
        case 0x0E: {
            //__bis_SR_register(GIE);                    // Enable all interrupt.
            scan_key();

            lcd_twinkle_num++;
            lcd_pressure_num++;
            //pid_waiting_num++;

            if (lcd_twinkle_num >= LCD_TWINKLE_FREQ) {   //500MS
                 Capture_voltage = ADC();
                 lcd_twinkle_num = 0;
                 pressureArray[pressureArrayIndex] = Voltage_to_Pressure_Show(Capture_voltage);
                 pressureArrayIndex ++;
                 if (pressureArrayIndex >= 300) {
                     pressureArrayIndex = 0;
                     _NOP();
                 }
                 if (setting_stage == NORMAL) LCD_Show_Update();
                 else LCD_Twinkle_Update();
             }

            if (motor_stage == MOTOR_WORKING){
                pid_calculate_num++;
                //if (pid_waiting_num >= PID_WAITING_TIME){
                //    pid_waiting_flag = 0;
                //}
            }else{
                pid_calculate_num = 0;
                //pid_waiting_num = 0;
                //pid_waiting_flag = 1;
            }

            //if (pid_waiting_flag == 0) pid_calculate_num++;

            if (pid_calculate_num >= PID_CALCULATE_FREQ) {
                pid_calculate_num = 0;
                if (motor_stage == MOTOR_WORKING){
                    water_flow_change_flag = 0;
                    /*if (frequency / 7.5 < WATER_FLOW_THRESHOLD
                        && frequency_last / 7.5 >= WATER_FLOW_THRESHOLD){
                        water_flow_change_flag = 1;
                    } else if (frequency / 7.5 >= WATER_FLOW_THRESHOLD
                               && frequency_last / 7.5 < WATER_FLOW_THRESHOLD){
                        water_flow_change_flag = 2;
                    } else {
                        water_flow_change_flag = 0;
                    }*/

                    if (water_flow_change_flag == 1){
                        Set_Pressure = (float) (1.0 * standbyPressure / 10);
                        Sent_Fc = Set_Fc = Pressure_to_Fc(standbyPressure / 10);
                        water_flow_change_flag = 0;
                    }else if(water_flow_change_flag == 2){
                        Set_Pressure = (float) (1.0 * workingPressure / 10);
                        Sent_Fc = Set_Fc = Pressure_to_Fc(workingPressure / 10);
                        water_flow_change_flag = 0;
                    }

                    Change_Fc_PID();
                    FCChangeFlag = 1;
                    FcArray[FcArrayIndex] = Sent_Fc;
                    FcArrayIndex ++;
                    if (FcArrayIndex >= 200) FcArrayIndex = 0;
                    SPWM_Change_Freq(Sent_Fc);
                }

            }

            SPWM_FreqChangeCheck();

            break;
        }
        default:
            break;
    }
}

#pragma vector=TIMER1_A1_VECTOR
__interrupt void Timer_A1_Cap(void) {
    switch (TA1IV) {         //向量查询
        case 0x04: {         //捕获中断
            if (TA1CCTL2 & CM0) {//捕获到上升沿
                if (cap_flag == 0) {
                    TA1CCTL2 = (TA1CCTL2 & (~CM0)) | CM1; //更变设置为下降沿触发
                    freq_periodStart = TA1CCR2; //记录初始时间
                    cap_flag = 1; //开始一个周期的捕获
                } else {
                    freq_periodEnd = TA1CCR2;   //一个周期的时间
                    if (!freq_overflow) {
                        freq_periodEnd -= freq_periodStart;
                        if(freq_periodEnd>182) {
                            frequency_last = frequency;
                            frequency = 32768 / freq_periodEnd;
                            frequencyArray[frequencyArrayIndex] = frequency;
                            frequencyArrayIndex++;
                            if (frequencyArrayIndex >= 50) frequencyArrayIndex = 0;
                        }
                    } else {
                        freq_overflow = 0;
                    }
                    cap_flag = 0;
                    //TA1CCTL2 &= ~CCIE;   //关捕获使能
                    //TA1CTL &= ~TAIE;     //关中断使能
                }
            } else if (TA1CCTL2 & CM1) { //捕获到下降沿
                if (cap_flag == 1) {
                    freq_pulseEnd = TA1CCR2; //记录脉冲宽度结束时间
                    freq_pulseEnd-=freq_periodStart;
                    if(freq_pulseEnd>92){       //脉冲宽度在允许范围内
                        TA1CCTL2 = (TA1CCTL2 & (~CM1)) | CM0; //更变设置为上升沿触发
                    }
                }
            }
            break;
        }
        case 0X0E:{                                 // 溢出数加1
            freq_overflow++;
            break;
        }
        default:
            break;
    }
}


void opr_key(unsigned char key_num) {
    switch (key_num) {
        case 8:{
            Change_Fc_PID();
            FCChangeFlag = 1;
            FcArray[FcArrayIndex] = Sent_Fc;
            FcArrayIndex ++;
            if (FcArrayIndex >= 200) FcArrayIndex = 0;
            SPWM_Change_Freq(Sent_Fc);
            break;
        }
        case 1: {
            LCD_Show_Update();
            lcd_twinkle_cursor = 0;
            switch (setting_stage) {
                case WORKING3: {
                    setting_stage = NORMAL;
                    break;
                }
                default: {
                    setting_stage++;
                    //_NOP();
                    break;
                }
            }
            break;
        }
        case 2: {
            switch (setting_stage) {
                case STANDBY1: {
                    standbyPressure += 100;
                    standbyPressureChangeFlag = 1;
                    break;
                }
                case STANDBY2: {
                    standbyPressure += 10;
                    standbyPressureChangeFlag = 1;
                    break;
                }
                case STANDBY3: {
                    standbyPressure += 1;
                    standbyPressureChangeFlag = 1;
                    break;
                }
                case WORKING1: {
                    workingPressure += 100;
                    workingPressureChangeFlag = 1;
                    break;
                }
                case WORKING2: {
                    workingPressure += 10;
                    workingPressureChangeFlag = 1;
                    break;
                }
                case WORKING3: {
                    workingPressure += 1;
                    workingPressureChangeFlag = 1;
                    break;
                }
                /*case NORMAL: {
                    Set_Fc += 250;
                    FCChangeFlag = 1;
                    Sent_Fc = Set_Fc;
                    SPWM_Change_Freq(Sent_Fc);
                    break;
                }*/
                default:
                    break;

            }
            if (setting_stage != NORMAL) {
                if (standbyPressure > MAX_STANDBY_PRESSURE) standbyPressure = MAX_STANDBY_PRESSURE;
                if (workingPressure > MAX_WORKING_PRESSURE) workingPressure = MAX_WORKING_PRESSURE;
            }
            break;
        }
        case 3: {
            switch (setting_stage) {
                case STANDBY1: {
                    if (standbyPressure / 100 != 0) {
                        standbyPressure -= 100;
                        standbyPressureChangeFlag = 1;
                    }
                    break;
                }
                case STANDBY2: {
                    if (standbyPressure / 100 != 0 || standbyPressure % 100 / 10 != 0) {
                        standbyPressure -= 10;
                        standbyPressureChangeFlag = 1;
                    }
                    break;
                }
                case STANDBY3: {
                    if (standbyPressure / 100 != 0 || standbyPressure % 100 / 10 != 0 ||
                        standbyPressure % 100 % 10 != 0){
                        standbyPressure -= 1;
                        standbyPressureChangeFlag = 1;
                    }

                    break;
                }
                case WORKING1: {
                    if (workingPressure / 100 != 0) {
                        workingPressure -= 100;
                        workingPressureChangeFlag = 1;
                    }
                    break;
                }
                case WORKING2: {
                    if (workingPressure / 100 != 0 || workingPressure % 100 / 10 != 0) {
                        workingPressure -= 10;
                        workingPressureChangeFlag = 1;
                    }
                    break;
                }
                case WORKING3: {
                    if (workingPressure / 100 != 0 || workingPressure % 100 / 10 != 0 ||
                        workingPressure % 100 % 10 != 0){
                        workingPressure -= 1;
                        workingPressureChangeFlag = 1;
                    }
                    break;
                }
                /*case NORMAL: {
                    Set_Fc -= 250;
                    Sent_Fc = Set_Fc;
                    SPWM_Change_Freq(Sent_Fc);
                    FCChangeFlag = 1;
                    break;
                }*/
                default:
                    break;
            }
            if (setting_stage != NORMAL) {
                //if (standbyPressure < MIN_STANDBY_PRESSURE) standbyPressure = MIN_STANDBY_PRESSURE;
                if (workingPressure < MIN_WORKING_PRESSURE) workingPressure = MIN_WORKING_PRESSURE;
            }
            break;
        }
        case 4: {                       /*抽水系统启动/停止*/
            Operate_motor();
            break;
        }
        case 5:
        case 6:
        case 7:
        case 9:
        case 10:
        case 11:
        case 13:
        case 14:
        case 15:
        case 16: {
            unsigned int num = 10;
            switch (key_num) {
                case 5:
                case 6:
                case 7: {
                    num = (unsigned int) (key_num - 4);
                    break;
                }
                case 9:
                case 10:
                case 11: {
                    num = (unsigned int) (key_num - 5);
                    break;
                }
                case 13:
                case 14:
                case 15: {
                    num = (unsigned int) (key_num - 6);
                    break;
                }
                case 16: {
                    num = 0;
                    break;
                }
                default:
                    break;
            }
            switch (setting_stage) {
                case STANDBY1: {
                    standbyPressure = standbyPressure % 100 + num * 100;
                    break;
                }
                case STANDBY2: {
                    standbyPressure = standbyPressure / 100 * 100 + standbyPressure % 100 % 10 + num * 10;
                    break;
                }
                case STANDBY3: {
                    standbyPressure = standbyPressure / 100 * 100 + standbyPressure % 100 / 10 * 10 + num;
                    if (standbyPressure > MAX_STANDBY_PRESSURE) standbyPressure = MAX_STANDBY_PRESSURE;
                    standbyPressureChangeFlag = 1;
                    break;
                }
                case WORKING1: {
                    workingPressure = workingPressure % 100 + num * 100;
                    break;
                }
                case WORKING2: {
                    workingPressure = workingPressure / 100 * 100 + workingPressure % 100 % 10 + num * 10;
                    break;
                }
                case WORKING3: {
                    workingPressure = workingPressure / 100 * 100 + workingPressure % 100 / 10 * 10 + num;
                    if (workingPressure > MAX_WORKING_PRESSURE) workingPressure = MAX_WORKING_PRESSURE;
                    workingPressureChangeFlag = 1;
                    break;
                }
                default:
                    break;
            }

            switch (setting_stage) {
                case WORKING3: {
                    setting_stage = NORMAL;
                    break;
                }
                default: {
                    if (setting_stage != NORMAL) setting_stage++;
                    break;
                }
            }
            LCD_Show_Update();
            lcd_twinkle_cursor = 0;
        }

        default:
            break;
    }
}

float GetPressure(float voltage){
    float target_pressure = 0;
    target_pressure = Voltage_to_Pressure_Show(voltage);
    if(target_pressure <= 1e-7){
        target_pressure = 0;
    }
    return target_pressure;
}

void LCD_Init_Show() {
    LCD_Show(1, 0, line1);
    LCD_Show(2, 0, line2);
    LCD_Show(3, 0, line3);
    LCD_Show(4, 0, line41);
    LCD_Show(4, 4, line42);
    LCD_Show_Update();
}


void LCD_Show_Get_Data(unsigned int variable) {
    displayCache[0] = (unsigned char) (variable / 100 + '0');
    displayCache[1] = (unsigned char) (variable % 100 / 10 + '0');
    displayCache[2] = '.';
    displayCache[3] = (unsigned char) (variable % 100 % 10 + '0');
    displayCache[4] = '\0';
}

void LCD_Twinkle_Update() {
    unsigned int waterFlow = 0;
    unsigned int waterPressure = 0;

    if (lcd_twinkle_cursor == 0) {
        lcd_twinkle_cursor = 1;
        switch (setting_stage) {
            case STANDBY1: {
                displayCache[0] = ' ';
                displayCache[1] = (unsigned char) (standbyPressure % 100 / 10 + '0');
                displayCache[2] = '.';
                displayCache[3] = (unsigned char) (standbyPressure % 100 % 10 + '0');
                displayCache[4] = '\0';
                LCD_Show(2, 5, displayCache);
                break;
            }
            case STANDBY2: {
                displayCache[0] = (unsigned char) (standbyPressure / 100 + '0');
                displayCache[1] = ' ';
                displayCache[2] = '.';
                displayCache[3] = (unsigned char) (standbyPressure % 100 % 10 + '0');
                displayCache[4] = '\0';
                LCD_Show(2, 5, displayCache);
                break;
            }
            case STANDBY3: {
                displayCache[0] = (unsigned char) (standbyPressure / 100 + '0');
                displayCache[1] = (unsigned char) (standbyPressure % 100 / 10 + '0');
                displayCache[2] = '.';
                displayCache[3] = ' ';
                displayCache[4] = '\0';
                LCD_Show(2, 5, displayCache);
                break;
            }
            case WORKING1: {
                displayCache[0] = ' ';
                displayCache[1] = (unsigned char) (workingPressure % 100 / 10 + '0');
                displayCache[2] = '.';
                displayCache[3] = (unsigned char) (workingPressure % 100 % 10 + '0');
                displayCache[4] = '\0';
                LCD_Show(3, 5, displayCache);
                break;
            }
            case WORKING2: {
                displayCache[0] = (unsigned char) (workingPressure / 100 + '0');
                displayCache[1] = ' ';
                displayCache[2] = '.';
                displayCache[3] = (unsigned char) (workingPressure % 100 % 10 + '0');
                displayCache[4] = '\0';
                LCD_Show(3, 5, displayCache);
                break;
            }
            case WORKING3: {
                displayCache[0] = (unsigned char) (workingPressure / 100 + '0');
                displayCache[1] = (unsigned char) (workingPressure % 100 / 10 + '0');
                displayCache[2] = '.';
                displayCache[3] = ' ';
                displayCache[4] = '\0';
                LCD_Show(3, 5, displayCache);
                break;
            }
            default:
                break;
        }

    } else {
        lcd_twinkle_cursor = 0;
        switch (setting_stage) {
            case STANDBY1:
            case STANDBY2:
            case STANDBY3: {
                LCD_Show_Get_Data(standbyPressure);
                LCD_Show(2, 5, displayCache);
                break;
            }
            case WORKING1:
            case WORKING2:
            case WORKING3: {
                LCD_Show_Get_Data(workingPressure);
                LCD_Show(3, 5, displayCache);
                break;
            }
            default:
                break;
        }
    }
#ifdef DEBUG
    if (FCChangeFlag == 1) {
        displayCache[0] = ' ';
        displayCache[1] = ' ';
        displayCache[2] = ' ';
        displayCache[3] = ' ';
        displayCache[4] = ' ';
        displayCache[5] = '\0';
        LCD_Show(1, 2, displayCache);
        sprintf(displayCache, "%04d", Sent_Fc);
        LCD_Show(1, 2, displayCache);
        FCChangeFlag = 0;
    }
#endif
    if (lcd_pressure_num >= LCD_PRESSURE_UPDATE) {   //1S
        lcd_pressure_num = 0;

        waterPressure = (unsigned int) (GetPressure(Capture_voltage) * 10);
        if (waterPressure < 200){
            LCD_Show_Get_Data(waterPressure);
            LCD_Show(4, 2, displayCache);
            if (com_sendtype == 0) RS232TX_SEND(datatype[0],displayCache);
        }

        waterFlow = (unsigned int) (frequency / 7.5 * 10);
        if (waterFlow < 300){
            LCD_Show_Get_Data(waterFlow);
            LCD_Show(4, 6, displayCache);
            if (com_sendtype == 1) RS232TX_SEND(datatype[1],displayCache);
        }

        if(com_sendtype == 2){
            if (motor_stage == MOTOR_STOPPED) displayCache[0] = '0';
            else if (motor_stage == MOTOR_WORKING) displayCache[0] = '1';
            displayCache[1] = '\0';
            RS232TX_SEND(datatype[4],displayCache);
        }

        if (com_sendtype == 0){
            com_sendtype = 1;
        } else if(com_sendtype == 1){
            com_sendtype = 2;
        }else if(com_sendtype == 2){
            com_sendtype = 0;
        }
#ifdef DEBUG
        //waterPressure = (unsigned int) (GetPressure(Capture_voltage) * 10);
        //LCD_Show_Get_Data(waterPressure);
        /*displayCache[0] = ' ';
        displayCache[1] = ' ';
        displayCache[2] = ' ';
        displayCache[3] = ' ';
        displayCache[4] = '\0';
        LCD_Show(4, 2, displayCache);
        LCD_Show(4, 6, displayCache);*/
        displayCache[0] = ' ';
        displayCache[1] = ' ';
        displayCache[2] = '\0';
        LCD_Show(4, 3, displayCache);
        LCD_Show(4, 7, displayCache);

        sprintf(displayCache,"%.1f", GetPressure(Capture_voltage));
        lcd_pressure_num = 0;
        LCD_Show(4, 2, displayCache);

        sprintf(displayCache,"%.1f", frequency / 7.5);
        //waterFlow = (unsigned int) (frequency / 7.5 * 10);
        //LCD_Show_Get_Data(waterFlow);
        LCD_Show(4, 6, displayCache);
#endif
    }

    /*waterPressure = (unsigned int) (Capture_voltage * 10);
    LCD_Show_Get_Data(waterPressure);*/
    //LCD_Show(4, 2, displayCache);

    /*  waterFlow = (unsigned int) (frequency / 7.5 * 10);
        LCD_Show_Get_Data(waterFlow);
        LCD_Show(4, 6, displayCache);*/

}

void LCD_Show_Update() {
    unsigned int waterFlow = 0;
    unsigned int waterPressure = 0;

   /* //sprintf(displayCache,"%04d",Set_Fc);
    LCD_Show(1, 2, displayCache);*/
#ifdef DEBUG
    if (FCChangeFlag == 1){
        displayCache[0] = ' ';
        displayCache[1] = ' ';
        displayCache[2] = ' ';
        displayCache[3] = ' ';
        displayCache[4] = ' ';
        displayCache[5] = '\0';
        LCD_Show(1, 2, displayCache);
        sprintf(displayCache,"%04d",Sent_Fc);
        LCD_Show(1, 2, displayCache);
        FCChangeFlag = 0;
    }
#endif

    if (standbyPressureChangeFlag == 1){
        LCD_Show_Get_Data(standbyPressure);
        LCD_Show(2, 5, displayCache);
        standbyPressureChangeFlag = 0;
    }

    if (workingPressureChangeFlag == 1){
        LCD_Show_Get_Data(workingPressure);
        LCD_Show(3, 5, displayCache);
        workingPressureChangeFlag = 0;
    }

    if (lcd_pressure_num >= LCD_PRESSURE_UPDATE) {   //1S
        lcd_pressure_num = 0;

        waterPressure = (unsigned int) (GetPressure(Capture_voltage) * 10);
        if (waterPressure < 200){
            LCD_Show_Get_Data(waterPressure);
            LCD_Show(4, 2, displayCache);
            if (com_sendtype == 0) RS232TX_SEND(datatype[0],displayCache);
        }

        waterFlow = (unsigned int) (frequency / 7.5 * 10);
        if (waterFlow < 300){
            LCD_Show_Get_Data(waterFlow);
            LCD_Show(4, 6, displayCache);
            if (com_sendtype == 1) RS232TX_SEND(datatype[1],displayCache);
        }

        if(com_sendtype == 2){
            if (motor_stage == MOTOR_STOPPED) displayCache[0] = '0';
            else if (motor_stage == MOTOR_WORKING) displayCache[0] = '1';
            displayCache[1] = '\0';
            RS232TX_SEND(datatype[4],displayCache);
        }

        if (com_sendtype == 0){
            com_sendtype = 1;
        } else if(com_sendtype == 1){
            com_sendtype = 2;
        }else if(com_sendtype == 2){
            com_sendtype = 0;
        }


        //LCD_Show_Get_Data(waterPressure);
        /*displayCache[0] = ' ';
        displayCache[1] = ' ';
        displayCache[2] = '\0';
        LCD_Show(4, 3, displayCache);
        LCD_Show(4, 7, displayCache);*/


        //sprintf(displayCache,"%.1f", GetPressure(Capture_voltage));


        /*sprintf(displayCache,"%.1f",frequency / 7.5);
        //waterFlow = (unsigned int) (frequency / 7.5 * 10);
        //LCD_Show_Get_Data(waterFlow);
        LCD_Show(4, 6, displayCache);*/
    }
    /*sprintf(displayCache,"%4.2f",Voltage_to_Pressure_Show(Capture_voltage));
   // sprintf(displayCache,"%6.5f",Capture_voltage);
    //waterPressure = (unsigned int) (Capture_voltage * 10);
    //LCD_Show_Get_Data(waterPressure);
    LCD_Show(4, 2, displayCache);*/

    /*waterFlow = (unsigned int) (frequency / 7.5 * 10);
    LCD_Show_Get_Data(waterFlow);
    LCD_Show(4, 6, displayCache);*/
}

/**
 * Change global variable: Sent_Fc
 */
void Change_Fc_PID(){
    PID_realize();
    //Sent_Fc = Pressure_to_Fc(PIDFreq.output);
    Sent_Fc = PIDFreq.output;
    if(Sent_Fc > Max_Fc){
        Sent_Fc = Max_Fc;
    }
    else if (Sent_Fc < Min_Fc){
        Sent_Fc = Min_Fc;
    }
}

void Translate_Com(unsigned char *str) {
    switch (*str){
        case('W'):{
            str++;
            workingPressure = 0;
            while (*str != '\0'){
                workingPressure = (*str) - '0' + workingPressure * 10;
                str++;
            }
            workingPressureChangeFlag = 1;
            break;
        }
        case('S'):{
            str++;
            standbyPressure = 0;
            while (*str != '\0'){
                standbyPressure = (*str) - '0' + standbyPressure * 10;
                str++;
            }
            standbyPressureChangeFlag = 1;
            break;
        }
        case('M'):{
            Operate_motor();
            break;
        }
        default:break;
    }
}

//RS232/485接收中断服务程序
//_nop()只是测试用来看是不是进了对应的位置用的 可以删掉
#pragma vector=USCI_A3_VECTOR
__interrupt void USCI_A3_ISR(void) {
  static unsigned int datacount=0;
  unsigned char end;
  if(datacount==0) {
    REC_BUF[datacount] = UCA3RXBUF;   //访问接收缓冲寄存器
    datacount++;
    switch(REC_BUF[0]){         //judge data type
      case('P'):
      case('F'):
      case('W'):
      case('S'):
      case('M'):{
          _nop();
          break;
      }
      default:{
        datacount=0;
        break;
      }
    }
  } else if(datacount<11){
    end=UCA3RXBUF;
    /*   假读\0 不存  现在没这个情况了
    if(datacount==1){

        REC_BUF[1]=REC_BUF[2];
    }
    */
    if(end=='E'){
        REC_BUF[datacount] = '\0';
        datacount=0;
        Translate_Com(REC_BUF);
    }else{
        REC_BUF[datacount]= end;
        datacount++;
    }
  } 
}
//unsigned char datatest[9]={"19.2"};
//RS232TX_SEND(datatype[0],datatest);

void Operate_motor(){
    switch (motor_stage) {
        case MOTOR_STOPPED: {
            SPWM_GPIO_INIT();
            SPWM_CLOCK_INIT();
            Set_Pressure = (float) (1.0 * workingPressure / 10);
            Sent_Fc = Set_Fc = Pressure_to_Fc(workingPressure / 10);

            PID_init();
            pid_calculate_num = 0;
            water_flow_change_flag = 1;
            //pid_waiting_num = 0;
            //pid_waiting_flag = 1;

            /*Change_Fc_PID();
            FCChangeFlag = 1;
            FcArray[FcArrayIndex] = Sent_Fc;
            FcArrayIndex ++;
            if (FcArrayIndex >= 50) FcArrayIndex = 0;*/
            //Set_Fc = Sent_Fc = Pressure_to_Fc(workingPressure / 10);

            SPWM_Change_Freq(Sent_Fc);
            motor_stage = MOTOR_WORKING;
            break;
        }
        case MOTOR_WORKING: {
            SPWM_GPIO_OFF();
            motor_stage = MOTOR_STOPPED;
            break;
        }
        default:
            break;
    }
}

int main(void) {
    WDTCTL = WDTPW | WDTHOLD;       // stop watchdog timer

    initClock();
	Init_RSUART();
	
    ADS1118_GPIO_Init();           //initialize the GPIO
    ADS1118_SPI_Init();
    Capture_init();
    Key_GPIO_Init();
    LCD_GPIO_Init();
    LCD_Init();
    LCD_Init_Show();

    PID_init();

    _NOP();
    SPWM_Init();
    //SPWM_GPIO_INIT();
#ifdef DEBUG
    SPWM_GPIO_INIT();
    SPWM_CLOCK_INIT();
    Set_Pressure = (float) (1.0 * workingPressure / 10);
    Sent_Fc = Set_Fc = Pressure_to_Fc(workingPressure / 10);
#endif
    _NOP();

    initTimerA0();

    //SPWM_GPIO_INIT();
    //SPWM_CLOCK_INIT();

    while (1) {
        /*CS_L;
        Value = Write_SIP(0x858b);           //AD数值     Conversion Register
        ConfigRegister = Write_SIP(0x858b);  //配置寄存器 Config Register
        CS_H;
        _NOP(); //断点*/
    }
    return 0;

}


