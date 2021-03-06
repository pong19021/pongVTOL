/*********************************************************************
 * adc.h
 ********************************************************************/

//***********************************************************
//* Externals
//***********************************************************

extern void Init_ADC(void);
extern void read_adc(uint8_t channel);

extern const int8_t ADCseqVert[8];
extern const int8_t ADCseqSide[8];
extern const int8_t ACC_RPY_Order[5][NUMBEROFAXIS];
extern const int8_t Gyro_RPY_Order[5][NUMBEROFAXIS];
