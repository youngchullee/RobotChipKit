#include "WProgram.h"
#include "Wire.h" // used for I2C protocol (lib)

#include "config.h"
#include "def.h"
#include "types.h"
#include "PMultiWii.h"
#include "PSensors.h"

/*** I2C address ***/
#define MPU6050_ADDRESS     0x68 // address pin AD0 low (GND)
//#define MPU6050_ADDRESS     0x69 // address pin AD0 high (VCC)

#define ACC_1G             4096 // ACC_1G, depends on scale. For +/- 8g => 1g = 4096 => ACC_1G = 4096 
#define G_FORCE            9.81
#define PI                 3.14159265359

uint8_t rawADC[6];
uint8_t rawTemp[2];

  
// ************************************************************************************************************
// I2C general functions
// ************************************************************************************************************
void i2c_init() {

#if defined(TRACE)	
  Serial.println(">>>Start i2c_init");
#endif 

  Wire.begin(); // setup I2C
  
#if defined(TRACE)	
  Serial.println("<<<End   i2c_init");
#endif  
}


size_t i2c_read_reg_to_buf(uint8_t devAddr, uint8_t regAddr, void *buf, size_t size) {
  
  Wire.beginTransmission(devAddr);
  Wire.send(regAddr);
                
  uint8_t ret = Wire.endTransmission();
#if defined(TRACE)  
  if (ret != 0) {
      Serial.print("i2c_read_reg_to_buf ret: ");Serial.println((int)ret);
      Serial.print("devAddr: ");Serial.println((int)devAddr,HEX);
  }
#endif                          
  delay(1);
  
  Wire.requestFrom(devAddr, (uint8_t)size);
  size_t bytes_read = 0;
  uint8_t *b = (uint8_t*)buf;
  
  while (size--) {
    *b++ = Wire.receive();
    bytes_read++;
  }
  return bytes_read;	
}	


void i2c_getSixRawADC(uint8_t add, uint8_t reg) {
  i2c_read_reg_to_buf(add, reg, &rawADC, 6);
}

void i2c_getTemperature(uint8_t add, uint8_t reg) {
  i2c_read_reg_to_buf(add, reg, &rawTemp, 2);
}

uint8_t i2c_writeReg(uint8_t devAddr, uint8_t regAddr, uint8_t val) {
    Wire.beginTransmission(devAddr);
    Wire.send(regAddr); // send address
    Wire.send(val);     // send value
 
    uint8_t ret = Wire.endTransmission();
#if defined(TRACE)      
    if (ret != 0) {
        Serial.print("i2c_writeReg ret: ");Serial.println((int)ret);
        Serial.print("devAddr: ");Serial.println((int)devAddr,HEX);
     }   
#endif     
    return ret; 
}



// ************************************************************************************************************
// I2C Gyroscope and Accelerometer MPU6050
// ************************************************************************************************************

bool MPU_init() {
  uint8_t ret=0;
    
#if defined(TRACE)	
  Serial.println(">>>Start  MPU_init");
#endif    
  ret=i2c_writeReg(MPU6050_ADDRESS, 0x6B, 0x80); //PWR_MGMT_1    -- DEVICE_RESET 1
  if (ret> 0) return false;
  delay(5);
  ret=i2c_writeReg(MPU6050_ADDRESS, 0x6B, 0x03); //PWR_MGMT_1    -- SLEEP 0; CYCLE 0; TEMP_DIS 0; CLKSEL 3 (PLL with Z Gyro reference)
  if (ret> 0) return false;
  ret=i2c_writeReg(MPU6050_ADDRESS, 0x1A, 0x00); //CONFIG        -- EXT_SYNC_SET 0 (disable input pin for data sync) ; DLPF_CFG = 0 => ACC bandwidth = 260Hz  GYRO bandwidth = 256Hz)
  if (ret> 0) return false;

#if defined(TRACE)	
  Serial.println("<<<End OK MPU_init");
#endif 
  return true;
}


bool Gyro_init() {
  uint8_t ret=0;
#if defined(TRACE)	
  Serial.println(">>>Start  Gyro_init");
#endif 
  
  ret=i2c_writeReg(MPU6050_ADDRESS, 0x1B, 0x18); //GYRO_CONFIG   -- FS_SEL = 3: (Full scale set to +/- 2000 deg/sec)
  if (ret> 0) return false;

#if defined(TRACE)	
  Serial.println("<<<End OK Gyro_init");
#endif

  return true;
}


bool ACC_init () {
  uint8_t ret=0;    
#if defined(TRACE)	
  Serial.println(">>>Start  ACC_init");
#endif     

  i2c_writeReg(MPU6050_ADDRESS, 0x1C, 0x10);     //ACCEL_CONFIG  -- AFS_SEL=2 (Full Scale = +/-8G)  ; ACCELL_HPF=0
  if (ret> 0) return false;

#if defined(TRACE)	
  Serial.println("<<<End OK ACC_init");
#endif 
 
  return true;
}


bool initSensors() {
#if defined(TRACE)	
  Serial.println(">>Start initSensors"); 
#endif 

  i2c_init();
  delay(100);
  
  if (!MPU_init())
  {   
#if defined(TRACE)	  
   Serial.println("<<End KO MPU_init");
#endif    
    return false;
  }
  if (!Gyro_init())
  {   
#if defined(TRACE)	  
   Serial.println("<<End KO Gyro_init");
#endif    
    return false;
  }    
  if (!ACC_init())
      {   
#if defined(TRACE)	  
   Serial.println("<<End KO ACC_init");
#endif    
    return false;
  }

  i2c_getTemperature(MPU6050_ADDRESS, 0x41);  
  imu.temperature  = (rawTemp[0] << 8) | rawTemp[1];
    
#if defined(TRACE)	  
  Serial.print("Temperature: "); Serial.println(((double)imu.temperature + 12412.0) / 340.0);
  Serial.println("<<End OK initSensors");
#endif 
  return true;
}


// ************************************************************************
// GYRO common part
// adjust imu.gyroADC according gyroZero and 
// limit the variation between two consecutive readings (anti gyro glitch)
// ************************************************************************
void GYRO_Common() {
  static int32_t g[3];
  uint8_t axis;
  double e_roll, e_pitch;
  double i_roll, i_pitch;
  double eax, eay, eaz;
  double evx, evy, evz;
  static double prev_c_roll  = 0.0;
  static double prev_c_pitch = 0.0;
  double a = 0.98;
  uint32_t currentTime;
  static uint32_t previousTime = 0;
  uint32_t dt;

  if (calibratingG>0) {
    /* Calbrating phase */
    for (axis = 0; axis < 3; axis++) {
      // Reset g[axis] at start of calibration
      if (calibratingG == 512) {
        g[axis]=0;
      }
      // Sum up 512 readings
      g[axis] +=imu.gyroADC[axis];
      // Clear global variables for next reading
      imu.gyroADC[axis]=0;
      gyroZero[axis]=0;
      
      // Calculate average on 512 readings   
      if (calibratingG == 1) {
        gyroZero[axis]=g[axis]>>9;
#if defined(TRACE)  
    Serial.print("gyroZero[");Serial.print((int)axis);Serial.print("]:");Serial.println((int)gyroZero[axis]);
#endif        
      }
    }
    calibratingG--;  
  }
  else
  {
    /* Flying phase */
    for (axis = 0; axis < 3; axis++) {
      imu.gyroADC[axis] -= gyroZero[axis];  
    }
    
    //Convert raw value to real value based an selectivity defined at init: +/- 2000 deg/sec
    imu.dgyroADC[ROLL]  = imu.gyroADC[ROLL]  * 4000.0 / 65536; 
    imu.dgyroADC[PITCH] = imu.gyroADC[PITCH] * 4000.0 / 65536; 
    imu.dgyroADC[YAW]   = imu.gyroADC[YAW]   * 4000.0 / 65536; 
#if defined(TRACE6)  
    Serial.print(">>>GYRO_Common: imu.dgyroADC[ROLL]:");Serial.print(imu.dgyroADC[ROLL]);Serial.print(" *** ");
    Serial.print("imu.dgyroADC[PITCH]:");Serial.print(imu.dgyroADC[PITCH]);Serial.print(" *** ");
    Serial.print("imu.dgyroADC[YAW]:");Serial.println(imu.dgyroADC[YAW]);
#endif 

    // compute Euler angles between [-PI;+PI], re-add gravity1G
	e_roll   = atan2(imu.daccADC[ROLL],   sqrt(pow(imu.daccADC[YAW]+1, 2.0) + pow(imu.daccADC[PITCH], 2.0)));
	e_pitch  = atan2(imu.daccADC[PITCH],  sqrt(pow(imu.daccADC[YAW]+1, 2.0) + pow(imu.daccADC[ROLL],  2.0)));

#if defined(TRACE6)  
    Serial.print(">>>GYRO_Common: e_roll:");Serial.print(e_roll);Serial.print(" *** ");
    Serial.print("e_pitch:");Serial.println(e_pitch);   
#endif   
    
    // compute delta time DT in millis for gyro integration
    currentTime = millis();
    if (previousTime > 0) {
        dt = currentTime-previousTime;
  
        // integrate the gyros angular velocity in deg/sec to determine angles in radians
        i_roll =          imu.dgyroADC[0] * PI/180.0 * (double)dt/1000.0;
        i_pitch = -1.00 * imu.dgyroADC[1] * PI/180.0 * (double)dt/1000.0;
#if defined(TRACE6)  
        Serial.print(">>>GYRO_Common: i_roll:");Serial.print(i_roll);Serial.print(" *** ");
        Serial.print("i_pitch:");Serial.print(i_pitch);Serial.print(" *** ");
        Serial.print("dt:");Serial.println(dt);
        Serial.print(">>>GYRO_Common: prev_c_roll:");Serial.print(prev_c_roll);Serial.print(" *** ");
        Serial.print("prev_c_pitch:");Serial.println(prev_c_pitch);
#endif   
        // adjust angles Roll & Pitch using complementary filter between [-PI;+PI]
        c_angle[0]  = a * (prev_c_roll  + i_roll)  + (1 - a) * e_roll;
	    prev_c_roll = c_angle[0];
	    	    
	    c_angle[1] = a * (prev_c_pitch + i_pitch) + (1 - a) * e_pitch;
	    prev_c_pitch = c_angle[1];

#if defined(TRACE6)  
        Serial.print(">>>GYRO_Common: c_angle[0] in�:");Serial.print(c_angle[0]*180.0/PI);Serial.print(" *** ");
        Serial.print("c_angle[1] in�:");Serial.println(c_angle[1]*180.0/PI);
#endif  

	    // Convert the acceleration to earth coordinates
	    eax = imu.daccADC[PITCH] * cos(c_angle[0]);
	    eay = imu.daccADC[ROLL]  * cos(c_angle[1]);
	    eaz = imu.daccADC[YAW]   * cos(c_angle[0]) * cos(c_angle[1]);

#if defined(TRACE6)  
        Serial.print(">>>GYRO_Common: eax:");Serial.print(eax);Serial.print(" *** ");
        Serial.print("eay:");Serial.print(eay);Serial.print(" *** ");
        Serial.print("eaz:");Serial.println(eaz);
#endif
	    // Integrate acceleration to speed and convert in earth's X and Y axes meters per second
	    evx += eax * G_FORCE * (double)dt/1000.0;
	    evy += eay * G_FORCE * (double)dt/1000.0;
	    evz += eaz * G_FORCE * (double)dt/1000.0;
#if defined(TRACE6)  
        Serial.print(">>>GYRO_Common: evx:");Serial.print(evx);Serial.print(" *** ");
        Serial.print("evy:");Serial.print(evy);Serial.print(" *** ");
        Serial.print("evz:");Serial.println(evz);
#endif
    }
    else
    {
        c_angle[0] = 0;
        c_angle[1] = 0;
    }    
    previousTime = currentTime;
  
  }

}


/*******************************************************************************/
/*                    Gyro_getADC                                              */
/*  - call i2c_getSixRawADC to get raw values for:                             */
/*                            imu.gyroADC[axis]                                */
/*  - call GYRO_Common to adjust imu.gyroADC[axis] with gyroZero and           */
/*    limit the variation between two consecutive readings (anti gyro glitch)  */
/*    interval [-8192;+8192]                                                   */
/*******************************************************************************/

void Gyro_getADC () {
   
  i2c_getSixRawADC(MPU6050_ADDRESS, 0x43); 
  
  imu.gyroADC[ROLL]  = (rawADC[0] << 8) | rawADC[1];
  imu.gyroADC[PITCH] = (rawADC[2] << 8) | rawADC[3];
  imu.gyroADC[YAW]   = (rawADC[4] << 8) | rawADC[5]; 
#if defined(TRACE6)  
    Serial.print(">>>Gyro_getADC(1): imu.gyroADC[ROLL]:");Serial.print(imu.gyroADC[ROLL]);Serial.print(" *** ");
    Serial.print("imu.gyroADC[PITCH]:");Serial.print(imu.gyroADC[PITCH]);Serial.print(" *** ");
    Serial.print("imu.gyroADC[YAW]:");Serial.println(imu.gyroADC[YAW]);
#endif 

  GYRO_Common();
}

// ************************************************************************
// ACC common part
// adjust imu.accADC according accZero
// ************************************************************************
void ACC_Common() {
  static int32_t a[3];
  uint8_t axis;
  
  if (calibratingA>0) { /* Calibrating phase */
    for (axis = 0; axis < 3; axis++) {
      // Reset a[axis] at start of calibration
      if (calibratingA == 512) a[axis]=0;
      // Sum up 512 readings
      a[axis] +=imu.accADC[axis];
      // Clear global variables for next reading
      imu.accADC[axis]=0;
      accZero[axis]=0;
    }
    // Calculate average on 512 readings
    if (calibratingA == 1) {
      accZero[ROLL]  = a[ROLL]>>9;
      accZero[PITCH] = a[PITCH]>>9;
      accZero[YAW]   = a[YAW]>>9; 
#if defined(TRACE)  
      Serial.print("accZero[ROLL]:");Serial.println(accZero[ROLL]);
      Serial.print("accZero[PITCH]:");Serial.println(accZero[PITCH]);
      Serial.print("accZero[YAW]:");Serial.println(accZero[YAW]);      
#endif
    }
    calibratingA--;   
  }
  
  else
  
  {                     /* Flying phase */
    for (axis = 0; axis < 3; axis++) {
      imu.accADC[axis]  -= accZero[axis];
    }
#if defined(TRACE6)  
    Serial.print(">>>ACC_Common: imu.accADC[ROLL]:");Serial.print(imu.accADC[ROLL]);Serial.print(" *** ");
    Serial.print("imu.accADC[PITCH]:");Serial.print(imu.accADC[PITCH]);Serial.print(" *** ");
    Serial.print("imu.accADC[YAW]:");Serial.println(imu.accADC[YAW]);
#endif
  
    //Convert raw value to real value based an selectivity defined at init: +/- 8g
    imu.daccADC[ROLL]  = imu.accADC[ROLL]  * 16.0 / 65536;
    imu.daccADC[PITCH] = imu.accADC[PITCH] * 16.0 / 65536;
    imu.daccADC[YAW]   = imu.accADC[YAW]   * 16.0 / 65536;
#if defined(TRACE6)  
    Serial.print(">>>ACC_Common: imu.daccADC[ROLL]:");Serial.print(imu.daccADC[ROLL]);Serial.print(" *** ");
    Serial.print("imu.daccADC[PITCH]:");Serial.print(imu.daccADC[PITCH]);Serial.print(" *** ");
    Serial.print("imu.daccADC[YAW]:");Serial.println(imu.daccADC[YAW]);
#endif    
  }
}


/****************************************************************/
/*                    ACC_getADC                                */
/*  - call i2c_getSixRawADC to get raw values for:              */
/*                            imu.accADC[axis]                  */
/*  - call ACC_Common to adjust imu.accADC[axis] with accZero   */
/*    interval [-4096;+4096]                                    */
/*  - call ComputeEulerAngles to get the Euler angles in radians*/
/****************************************************************/

void ACC_getADC () {
  
  i2c_getSixRawADC(MPU6050_ADDRESS, 0x3B);
    
  imu.accADC[ROLL]  = (rawADC[0] << 8) | rawADC[1];
  imu.accADC[PITCH] = (rawADC[2] << 8) | rawADC[3];
  imu.accADC[YAW]   = (rawADC[4] << 8) | rawADC[5]; 
#if defined(TRACE6)  
    Serial.print(">>>ACC_getADC(1): imu.accADC[ROLL]:");Serial.print(imu.accADC[ROLL]);Serial.print(" *** ");
    Serial.print("imu.accADC[PITCH]:");Serial.print(imu.accADC[PITCH]);Serial.print(" *** ");
    Serial.print("imu.accADC[YAW]:");Serial.println(imu.accADC[YAW]);
#endif

  ACC_Common();
  
}

