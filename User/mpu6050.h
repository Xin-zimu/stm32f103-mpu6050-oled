#ifndef __MPU6050_H
#define __MPU6050_H

#include "stm32f10x.h"

#define MPU6050_STATUS_OFFLINE      0
#define MPU6050_STATUS_CALIBRATING  1
#define MPU6050_STATUS_READY        2

typedef struct
{
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
    int16_t temp10;
} MPU6050_RawData;

void MPU6050_Init(void);
void MPU6050_Task(void);
uint8_t MPU6050_IsOnline(void);
uint8_t MPU6050_GetStatus(void);
uint8_t MPU6050_GetWhoAmI(void);
int16_t MPU6050_GetRoll10(void);
int16_t MPU6050_GetPitch10(void);
int16_t MPU6050_GetYaw10(void);
int16_t MPU6050_GetTemp10(void);
void MPU6050_GetRawData(MPU6050_RawData *raw);

#endif
