#include "app_control.h"
#include "bsp_esp32.h"
#include "stdio.h"

#define     PICKUP_P_MAX    12       // 检测拿起的角度范围 ±9°
#define     PICKUP_V_MIN    40      // 检测拿起的速度范围 ±    
#define     FALLING_PITCH_N -60     // 检测倒地的角度最小值
#define     FALLING_PITCH_P 60

#define   OUTPUT_MAX  100
#define   MED_ANGLE   5.85

PID_TYPE_DEF  Balance ; // 直立环
PID_TYPE_DEF  Velocity; // 速度环
PID_TYPE_DEF  Turn    ; // 转向环
PID_TYPE_DEF  Location; // 位置环
PID_TYPE_DEF  Dir     ; // 方向环
PID_TYPE_DEF  Follow  ; // 跟随环

void PID_Init(void)
{
  /* 直立环参数 */
  Balance.Kp =2.06;        // 1.86f      极性正
  Balance.Ki = 0;
  Balance.Kd = 2.26;       // 2.16f  极性正
  Balance.I_MAX = 0;
  Balance.outMAX = 140.0f;
  Balance.outMIN = -140.0f;
  
  /* 速度环参数 */
  Velocity.Kp = 1.30; //20
  Velocity.Ki = Velocity.Kp/200.0f;
  Velocity.Kd = 0;
  Velocity.I = 0;
  Velocity.I_MAX =  600.0f;
  Velocity.outMAX = 100.0f;
  Velocity.outMIN = -100.0f;
  
  /* 方向环参数 */
  Dir.Kp = 0.5;     // 0.5      极性正
  Dir.Ki = Dir.Kp/200.0f;
  Dir.Kd = -0.54;   // -0.54    极性负
  Dir.I = 0;
  Dir.I_MAX = 1000.0f;
  Dir.outMAX = 40.0f;   
  Dir.outMIN = -40.0f;
}

void PID_Control_Update(void)
{ 
    /* 计算速度环 */
    Velocity.target = 0;
    Velocity.out = Velocity_PID_Calcu(Velocity.target, sys.V0-sys.V1);

    /* 速度环限幅 */
    Velocity.out = (Velocity.out > Velocity.outMAX) ? (Velocity.outMAX): \
    ((Velocity.out < Velocity.outMIN) ? (Velocity.outMIN):Velocity.out);

    /* 计算直立环 */
    Balance.target = MED_ANGLE;
    Balance.out = Balance_PID_Calcu(Balance.target, sys.Pitch, sys.Gy);

    /* 直立环限幅 */
    Balance.out = (Balance.out > Balance.outMAX) ? (Balance.outMAX): \
      ((Balance.out < Balance.outMIN) ? (Balance.outMIN):Balance.out);

    /* 计算方向环 */
    Dir.target = 300;
    Dir.out = Dir_PID_Calcu(Dir.target, sys.Yaw, sys.Gz);
    
    /* 方向环限幅 */
    Dir.out = (Dir.out > Dir.outMAX) ? (Dir.outMAX): \
      ((Dir.out < Dir.outMIN) ? (Dir.outMIN):Dir.out);    
    
    sys.Set_V0 =  Balance.out - Velocity.out + Dir.out;
    sys.Set_V1 =  Balance.out - Velocity.out - Dir.out;
    
    //printf("%.1f,%.1f,%.1f,%.1f,%.1f\n", sys.V0, sys.V1, sys.Set_V0, sys.Set_V1,Velocity.I);
    //printf("%.1f,%.1f,%.1f\n",Velocity.out, Balance.out, sys.Set_V0);
    //printf("%.1f,%.1f,%.1f,%.1f\n",Dir.target,sys.Yaw, sys.Gz, sys.Set_V0); //方向环参数输出
    
    /* 输出到电机的扭矩 */
    Set_Motor_Torque(MOTOR0, sys.Set_V0);
    Set_Motor_Torque(MOTOR1, sys.Set_V1);
}

float Balance_PID_Calcu(float target_angle, float current_angle, float gyro)
{
  float PID_out = 0;
  
  PID_out = Balance.Kp*(target_angle - current_angle) + Balance.Kd*(gyro);
  
  return PID_out;
}

float Velocity_PID_Calcu(float target_v, float current_v)
{
  static float Err_Lowout_cur, Err_Lowout_pre;  // 低通滤波器当前输出与上轮输出
  float Error;
  float PID_out = 0;
  float a = 0.7;
   
  Error = target_v - current_v; // 期望速度 - 当前速度
  Err_Lowout_cur = (1-a)*Error + a*Err_Lowout_pre; // 低通滤波器，数据更平滑
  Err_Lowout_pre = Err_Lowout_cur;                 // 记录当前低通滤波的输出作为下一次数据
  
  Velocity.I += Err_Lowout_cur;  // 对误差进行积分
  
  /* 积分限幅 */
  Velocity.I = (Velocity.I > Velocity.I_MAX) ? (Velocity.I_MAX):  \
      ( (Velocity.I < -Velocity.I_MAX) ? (-Velocity.I_MAX) : (Velocity.I) );

  /* 计算PID输出 */
  PID_out = Velocity.Kp*Err_Lowout_cur + Velocity.Ki*Velocity.I;
  
  return PID_out;
}

float Dir_PID_Calcu(float target_yaw, float current_yaw, float gyro_z)
{
    static float Err_Lowout_cur = 0, Err_Lowout_pre = 0;    // 低通滤波后的误差值
    float a = 0.7;      // 低通滤波器系数
    float Error,PID_out = 0;
    
    Error = target_yaw - current_yaw;
    Err_Lowout_cur = (1-a)*Error + a*Err_Lowout_pre;
    Err_Lowout_pre = Err_Lowout_cur;
    
    Dir.I += Err_Lowout_cur;
    
    /* 积分限幅 */
    Dir.I = (Dir.I > Dir.I_MAX) ? (Dir.I_MAX):  \
      ( (Dir.I < -Dir.I_MAX) ? (-Dir.I_MAX) : (Dir.I) ); 
    
    /* 计算PID输出 */
    PID_out = Dir.Kp*Err_Lowout_cur + Dir.Ki*Dir.I + Dir.Kd*gyro_z;
    
    return PID_out;
}

void Pick_Up_Detect(float velocity, float pitch)
{
    if((velocity > PICKUP_V_MIN || velocity < -PICKUP_V_MIN) && pitch<PICKUP_P_MAX && pitch>-PICKUP_P_MAX ){
        sys.pick_up_flag = 1;
    }
}

void Falling_Detect(float pitch)
{
    if(pitch > FALLING_PITCH_P || pitch < FALLING_PITCH_N)
    {
        sys.falling_flag = 1;
    }
}


