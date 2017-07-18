//
// Created by tangyq on 2017/7/18.
//

#ifndef LCD12864_H
#define LCD12864_H

/*******************************************************************/
/*                                                                 */
/*写指令到LCD                                                  */
/*RS=L，RW=L，E=高脉冲，D0-D7=指令码。                             */
/*                                                                 */
/*******************************************************************/
void LCD_WriteCommand(unsigned char Cmd);


/*******************************************************************/
/*                                                                 */
/*写显示数据到LCD                                                  */
/*RS=H，RW=L，E=高脉冲，D0-D7=数据。                               */
/*                                                                 */
/*******************************************************************/
void LCD_WriteData(unsigned char Dat);


/*******************************************************************/
/*                                                                 */
/*  LCD初始化设定                                                  */
/*                                                                 */
/*******************************************************************/
void LCD_Init();


//*********************************************************/
//*                                                       */
//* 设定汉字显示位置
//eg:LCD_Position(4,0); ----表示从第四行的第0个字符开始写*/
//--------------------------即从第四行的第0个字开始写
//eg:LCD_Position(4,1); ----表示从第四行的第2个字符开始写*/
//--------------------------即从第四行的第1个字开始写
/*                                                       */
/*********************************************************/
void LCD_Position(unsigned char X,unsigned char Y);


/*********************************************************
*                                                        *
* 闪烁函数                                               *
*                                                        *
*********************************************************/
void LCD_Flash();


/**********************************************************
; 显示字符表代码
**********************************************************/
void  LCD_Char_Display();


/*********************************************************
*                                                        *
* 图形显示                                               *
*                                                        *
*********************************************************/
void Photo_Display(const unsigned char *Bmp);


/*********************************************************
*                                                        *
* 清屏函数                                               *
*                                                        *
*********************************************************/
void LCD_Clear_Screen();


void LCD_GPIO_Init();

#endif //LCD12864_H
