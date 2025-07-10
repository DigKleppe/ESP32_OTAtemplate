/*
 * clock.h
 *
 *  Created on: Apr 2, 2022
 *      Author: dig
 */

#ifndef GUI_CLOCKTASK_H_
#define GUI_CLOCKTASK_H_

#include <time.h>
#include <sys/time.h>

extern struct tm timeinfo;

void clockTask(void *pvParameter);
extern volatile bool clockSynced; 


#endif /* GUI_CLOCKTASK_H_ */
