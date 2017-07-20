/*
 * ThreePhaseSpwm.h
 *
 *  Created on: 2017年7月19日
 *      Author: tangyq
 */

#ifndef THREEPHASESPWM_H_
#define THREEPHASESPWM_H_

#define M 450                                             // 载波比1000
#define PI 3.14159
#define DeadTime 20                                         // 1us
#define a_m 0.2                                                // a_m:调制度
#define Rad 2.0944                                          // 2PI/3
#define Fc_Default 15000									// Default 10khz

void SPWM_Init();
void SPWM_FreqChangeCheck();
void SPWM_Stop();
void SPWM_Change_Freq(unsigned int freq);

#endif /* THREEPHASESPWM_H_ */
