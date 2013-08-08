/*
Raspberry Pi Bosch BMP085 communication code.
By:      John Burns (www.john.geek.nz)
Date:    13 February 2013
License: CC BY-SA v3.0 - http://creativecommons.org/licenses/by-sa/3.0/

This is a derivative work based on:
	BMP085 Extended Example Code
	by: Jim Lindblom
	SparkFun Electronics
	date: 1/18/11
	license: CC BY-SA v3.0 - http://creativecommons.org/licenses/by-sa/3.0/
	Source: http://www.sparkfun.com/tutorial/Barometric/BMP085_Example_Code.pde

Compile with: gcc -Wall -o testBMP085 ./smbus.c ./testBMP085.c


Circuit detail:
	Using a Spark Fun Barometric Pressure Sensor - BMP085 breakout board
	link: https://www.sparkfun.com/products/9694
	This comes with pull up resistors already on the i2c lines.
	BMP085 pins below are as marked on the Sparkfun BMP085 Breakout board

	SDA	- 	P1-03 / IC20-SDA
	SCL	- 	P1-05 / IC20_SCL
	XCLR	- 	Not Connected
	EOC	-	Not Connected
	GND	-	P1-06 / GND
	VCC	- 	P1-01 / 3.3V
	
	Note: Make sure you use P1-01 / 3.3V NOT the 5V pin.
*/

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include "smbus.h" 

#define BMP085_I2C_ADDRESS 0x77

const unsigned char BMP085_OVERSAMPLING_SETTING = 3;

// Calibration values - These are stored in the BMP085
short int ac1;
short int ac2; 
short int ac3; 
unsigned short int ac4;
unsigned short int ac5;
unsigned short int ac6;
short int b1; 
short int b2;
short int mb;
short int mc;
short int md;

int b5; 

unsigned int temperature, pressure;


// Open a connection to the bmp085
// Returns a file id
int bmp085_i2c_Begin()
{
	int fd;
	char *fileName = "/dev/i2c-0";
	
	// Open port for reading and writing
	if ((fd = open(fileName, O_RDWR)) < 0)
		exit(1);
	
	// Set the port options and set the address of the device
	if (ioctl(fd, I2C_SLAVE, BMP085_I2C_ADDRESS) < 0) {					
		close(fd);
		exit(1);
	}

	return fd;
}

// Read two words from the BMP085 and supply it as a 16 bit integer
__s32 bmp085_i2c_Read_Int(int fd, __u8 address)
{
	__s32 res = i2c_smbus_read_word_data(fd, address);
	if (res < 0) {
		close(fd);
		exit(1);
	}

	// Convert result to 16 bits and swap bytes
	res = ((res<<8) & 0xFF00) | ((res>>8) & 0xFF);

	return res;
}

//Write a byte to the BMP085
void bmp085_i2c_Write_Byte(int fd, __u8 address, __u8 value)
{
	if (i2c_smbus_write_byte_data(fd, address, value) < 0) {
		close(fd);
		exit(1);
	}
}

// Read a block of data BMP085
void bmp085_i2c_Read_Block(int fd, __u8 address, __u8 length, __u8 *values)
{
	if(i2c_smbus_read_i2c_block_data(fd, address,length,values)<0) {
		close(fd);
		exit(1);
	}
}


void bmp085_Calibration()
{
	int fd = bmp085_i2c_Begin();
	ac1 = bmp085_i2c_Read_Int(fd,0xAA);
	ac2 = bmp085_i2c_Read_Int(fd,0xAC);
	ac3 = bmp085_i2c_Read_Int(fd,0xAE);
	ac4 = bmp085_i2c_Read_Int(fd,0xB0);
	ac5 = bmp085_i2c_Read_Int(fd,0xB2);
	ac6 = bmp085_i2c_Read_Int(fd,0xB4);
	b1 = bmp085_i2c_Read_Int(fd,0xB6);
	b2 = bmp085_i2c_Read_Int(fd,0xB8);
	mb = bmp085_i2c_Read_Int(fd,0xBA);
	mc = bmp085_i2c_Read_Int(fd,0xBC);
	md = bmp085_i2c_Read_Int(fd,0xBE);
	close(fd);
}

// Read the uncompensated temperature value
unsigned int bmp085_ReadUT()
{
	unsigned int ut = 0;
	int fd = bmp085_i2c_Begin();

	// Write 0x2E into Register 0xF4
	// This requests a temperature reading
	bmp085_i2c_Write_Byte(fd,0xF4,0x2E);
	
	// Wait at least 4.5ms
	usleep(5000);

	// Read the two byte result from address 0xF6
	ut = bmp085_i2c_Read_Int(fd,0xF6);

	// Close the i2c file
	close (fd);
	
	return ut;
}

// Read the uncompensated pressure value
unsigned int bmp085_ReadUP()
{
	unsigned int up = 0;
	int fd = bmp085_i2c_Begin();

	// Write 0x34+(BMP085_OVERSAMPLING_SETTING<<6) into register 0xF4
	// Request a pressure reading w/ oversampling setting
	bmp085_i2c_Write_Byte(fd,0xF4,0x34 + (BMP085_OVERSAMPLING_SETTING<<6));

	// Wait for conversion, delay time dependent on oversampling setting
	usleep((2 + (3<<BMP085_OVERSAMPLING_SETTING)) * 1000);

	// Read the three byte result from 0xF6
	// 0xF6 = MSB, 0xF7 = LSB and 0xF8 = XLSB
	__u8 values[3];
	bmp085_i2c_Read_Block(fd, 0xF6, 3, values);

	up = (((unsigned int) values[0] << 16) | ((unsigned int) values[1] << 8) | (unsigned int) values[2]) >> (8-BMP085_OVERSAMPLING_SETTING);

	// Close the i2c file
	close (fd);
	
	return up;
}

// Calculate pressure given uncalibrated pressure
// Value returned will be in units of Pa
unsigned int bmp085_GetPressure(unsigned int up)
{
	int x1, x2, x3, b3, b6, p;
	unsigned int b4, b7;
  
	b6 = b5 - 4000;
	// Calculate B3
	x1 = (b2 * (b6 * b6)>>12)>>11;
	x2 = (ac2 * b6)>>11;
	x3 = x1 + x2;
	b3 = (((((int)ac1)*4 + x3)<<BMP085_OVERSAMPLING_SETTING) + 2)>>2;
  
	// Calculate B4
	x1 = (ac3 * b6)>>13;
	x2 = (b1 * ((b6 * b6)>>12))>>16;
	x3 = ((x1 + x2) + 2)>>2;
	b4 = (ac4 * (unsigned int)(x3 + 32768))>>15;
  
	b7 = ((unsigned int)(up - b3) * (50000>>BMP085_OVERSAMPLING_SETTING));
	if (b7 < 0x80000000)
		p = (b7<<1)/b4;
	else
		p = (b7/b4)<<1;
	
	x1 = (p>>8) * (p>>8);
	x1 = (x1 * 3038)>>16;
	x2 = (-7357 * p)>>16;
	p += (x1 + x2 + 3791)>>4;
  
	return p;
}

// Calculate temperature given uncalibrated temperature
// Value returned will be in units of 0.1 deg C
unsigned int bmp085_GetTemperature(unsigned int ut)
{
	int x1, x2;
  
	x1 = (((int)ut - (int)ac6)*(int)ac5) >> 15;
	x2 = ((int)mc << 11)/(x1 + md);
	b5 = x1 + x2;

	unsigned int result = ((b5 + 8)>>4);  

	return result;
}

int main(int argc, char **argv)
{
//	double alt,alt_anal;
	unsigned int depl;
	unsigned int vect[2][500];
//	double pr;
	bmp085_Calibration();
	int x;
//	temperature = bmp085_GetTemperature(bmp085_ReadUT());
	pressure = bmp085_GetPressure(bmp085_ReadUP());
	depl=(int)(pressure/100)*100-300;
	vect[1][x]=0;
//	pr=(double)pressure/100-0.3;
//	alt = 44330 * (1-pow((double)(pressure/100+6)/1011.25, 0.190294957));
//	alt_anal = 44330 * (1-pow((double)pressure/101325, 0.190294957));
//	printf("Temperature         \t%0.4f C\n", ((double)temperature)/10);
//	printf("Pressure            \t%0.4f hPa\n", ((double)pressure)/100);
//	printf("Pr2\t%0.4f hPa\n", (double)pr);
//	printf("Altitude recalib    \t%0.4f m\n", alt);
//	printf("Altitude analitical \t%0.4f m\n", alt_anal);
	printf("Deplasament > %d <\n",depl);
	for (x=1;x<500;x++)
	{
	vect[1][x]=bmp085_GetPressure(bmp085_ReadUP())-depl;
	pressure = vect[1][x];
	printf("%d,\t %d, %f, %0.2f,\n",x, pressure, ((double)vect[1][x-1])/(double)(vect[1][x]),(double)abs((1-((double)vect[1][x-1])/(double)(vect[1][x]))*100));
//	printf("P \t %d %f\n", pressure, ((double)vect[1][x-1])/(double)(vect[1][x]));
	usleep(2000);
//        print("\t 0.4%f \n",((double)&vect[1][x])/100);
	
	}

	return 0;
}