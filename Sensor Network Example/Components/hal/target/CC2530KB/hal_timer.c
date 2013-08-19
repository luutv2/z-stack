/**************************************************************************************************
  Filename:       hal_timer.c
  Revised:        $Date: 2009-03-12 16:25:22 -0700 (Thu, 12 Mar 2009) $
  Revision:       $Revision: 19404 $

  Description:   This file contains the interface to the Timer Service.

  Copyright 2006-2007 Texas Instruments Incorporated. All rights reserved.

  Should you have any questions regarding your right to use this Software,
  contact Texas Instruments Incorporated at www.TI.com.
**************************************************************************************************/
/*********************************************************************
 NOTE: The following mapping is done between the logical timer
       names defined in HAL_TIMER.H and the physical HW timer.

       HAL_TIMER_0 --> HW Timer 3  (8-bits)
       HAL_TIMER_2 --> HW Timer 4  (8-bits)
       HAL_TIMER_3 --> HW Timer 1  (16-bits)

 NOTE: The timer code assumes only one channel, CHANNEL 0, is used
       for each timer.  There is currently no support for other
       channels.

 NOTE: Only Output Compare Mode is supported.  There is no provision
       to support Input Capture Mode.

 NOTE: There is no support to map the output of the timers to a
       physical I/O pin

*********************************************************************/
/*********************************************************************
 * INCLUDES
 */
#include  "hal_mcu.h"
#include  "hal_defs.h"
#include  "hal_types.h"
#include  "hal_timer.h"

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */
#define HW_TIMER_1        0x00
#define HW_TIMER_3        0x01
#define HW_TIMER_4        0x02
#define HW_TIMER_INVALID  0x03
#define HW_TIMER_MAX      0x03

#define IEN1_T1IE     0x02    /* Timer1 Interrupt Enable */
#define IEN1_T3IE     0x08    /* Timer3 Interrupt Enable */
#define IEN1_T4IE     0x10    /* Timer4 Interrupt Enable */

#define T1CTL_CH2IF   0x80
#define T1CTL_CH1IF   0x40
#define T1CTL_CH0IF   0x20
#define T1CTL_OVFIF   0x10

#define TIMIF_T1OVFIM 0x40
#define TIMIF_T4CH1IF 0x20
#define TIMIF_T4CH0IF 0x10
#define TIMIF_T4OVFIF 0x08
#define TIMIF_T3CH1IF 0x04
#define TIMIF_T3CH0IF 0x02
#define TIMIF_T3OVFIF 0x01

#define T34CTL_OVFIM  0x80

#define T134CCTL_IM         0x40    /* Interrupt Mask */
#define T134CCTL_CMP_BITS   0x38    /* Bits[5:3] == CMP[2:0] */
#define T134CCTL_MODE       0x04    /* Capture(0)/Compare(1) mode */
#define T134CCTL_CAP_BITS   0x03    /* Bits[1:0] == CAP[1:0] */

#define T134CCTL_CMP_OC     0x18    /* Set output on compare, clear at 0 */
#define T134CCTL_CAP_RE     0x01    /* Set input capture on rising edge */

/* Timer clock pre-scaler definitions for 16bit timer1 */
#define HAL_TIMER1_16_TC_DIV1     0x00  /* No clock pre-scaling */
#define HAL_TIMER1_16_TC_DIV8     0x04  /* Clock pre-scaled by 8 */
#define HAL_TIMER1_16_TC_DIV32    0x08  /* Clock pre-scaled by 32 */
#define HAL_TIMER1_16_TC_DIV128   0x0c  /* Clock pre-scaled by 128 */
#define HAL_TIMER1_16_TC_BITS     0x0c  /* Bits 3:2 */

/* Timer clock pre-scaler definitions for 8bit timer3 and timer4 */
#define HAL_TIMER34_8_TC_DIV1     0x00  /* No clock pre-scaling */
#define HAL_TIMER34_8_TC_DIV2     0x20  /* Clock pre-scaled by 2 */
#define HAL_TIMER34_8_TC_DIV4     0x40  /* Clock pre-scaled by 4 */
#define HAL_TIMER34_8_TC_DIV8     0x60  /* Clock pre-scaled by 8 */
#define HAL_TIMER34_8_TC_DIV16    0x80  /* Clock pre-scaled by 16 */
#define HAL_TIMER34_8_TC_DIV32    0xA0  /* Clock pre-scaled by 32 */
#define HAL_TIMER34_8_TC_DIV64    0xC0  /* Clock pre-scaled by 64 */
#define HAL_TIMER34_8_TC_DIV128   0xE0  /* Clock pre-scaled by 128 */
#define HAL_TIMER34_8_TC_BITS     0xE0  /* Bits 7:5 */

/* Operation Mode definitions */
#define HAL_TIMER1_OPMODE_STOP      0x00  /* Free Running Mode, Count from 0 to Max */
#define HAL_TIMER1_OPMODE_FREERUN   0x01  /* Free Running Mode, Count from 0 to Max */
#define HAL_TIMER1_OPMODE_MODULO    0x02  /* Modulo Mode, Count from 0 to CompareValue */
#define HAL_TIMER1_OPMODE_BITS      0x03  /* Bits 1:0 */

#define HAL_TIMER34_START           0x10  /* Timer3 and Timer4 have separate Start bit */
#define HAL_TIMER34_OPMODE_FREERUN  0x00  /* Free Running Mode, Count from 0 to Max */
#define HAL_TIMER34_OPMODE_MODULO   0x02  /* Modulo Mode, Count from 0 to CompareValue */
#define HAL_TIMER34_OPMODE_BITS     0x03  /* Bits 1:0 */

#define HAL_TIMER_MODE_STOP         0x03

/* Prescale settings */
#define HAL_TIMER1_16_PRESCALE      HAL_TIMER1_16_TC_DIV128
#define HAL_TIMER1_16_PRESCALE_VAL  128
#define HAL_TIMER3_8_PRESCALE       HAL_TIMER34_8_TC_DIV128
#define HAL_TIMER3_8_PRESCALE_VAL   128
#define HAL_TIMER4_8_PRESCALE       HAL_TIMER34_8_TC_DIV128
#define HAL_TIMER4_8_PRESCALE_VAL   128

/* Clock settings */
#define HAL_TIMER_16MHZ           16
#define HAL_TIMER_32MHZ           32

/* Default all timers to use channel 0 */
#define TCHN_T1CCTL   &(X_T1CCTL0)
#define TCHN_T1CCL    &(X_T1CC0L)
#define TCHN_T1CCH    &(X_T1CC0H)
#define TCNH_T1OVF    &(X_TIMIF)
#define TCHN_T1OVFBIT TIMIF_T1OVFIM
#define TCHN_T1INTBIT IEN1_T1IE

#define TCHN_T3CCTL   &(X_T3CCTL0)
#define TCHN_T3CCL    &(X_T3CC0)
#define TCHN_T3CCH    &(X_T3CC0)
#define TCNH_T3OVF    &(X_T3CTL)
#define TCHN_T3OVFBIT T34CTL_OVFIM
#define TCHN_T3INTBIT IEN1_T3IE

#define TCHN_T4CCTL   &(X_T4CCTL0)
#define TCHN_T4CCL    &(X_T4CC0)
#define TCHN_T4CCH    &(X_T4CC0)
#define TCNH_T4OVF    &(X_T4CTL)
#define TCHN_T4OVFBIT T34CTL_OVFIM
#define TCHN_T4INTBIT IEN1_T4IE

/*********************************************************************
 * TYPEDEFS
 */
typedef struct
{
  bool configured;
  bool intEnable;
  uint8 opMode;
  uint8 channel;
  uint8 channelMode;
  uint8 prescale;
  uint8 prescaleVal;
  uint8 clock;
  halTimerCBack_t callBackFunc;
} halTimerSettings_t;

typedef struct
{
  uint8 volatile XDATA *TxCCTL;
  uint8 volatile XDATA *TxCCH;
  uint8 volatile XDATA *TxCCL;
  uint8 volatile XDATA *TxOVF;
  uint8 ovfbit;
  uint8 intbit;
} halTimerChannel_t;

/*********************************************************************
 * GLOBAL VARIABLES
 */
static halTimerSettings_t halTimerRecord[HW_TIMER_MAX];
static halTimerChannel_t  halTimerChannel[HW_TIMER_MAX];

/*********************************************************************
 * FUNCTIONS - External
 */

/*********************************************************************
 * FUNCTIONS - Local
 */
uint8 halTimerSetCount (uint8 cc2430id, uint32 timePerTick);
uint8 halTimerSetPrescale (uint8 cc2430id, uint8 prescale);
uint8 halTimerSetOpMode (uint8 cc2430id, uint8 opMode);
uint8 halTimerSetChannelMode (uint8 cc2430id, uint8 channelMode);
void halTimerSendCallBack (uint8 timerId, uint8 channel, uint8 channelMode);
uint8 halTimerRemap (uint8 timerId);
void halProcessTimer1 (void);
void halProcessTimer3 (void);
void halProcessTimer4 (void);

void HalTimerInit (void)
{
  T1CCTL0 = 0;    /* Make sure interrupts are disabled */
  T1CCTL1 = 0;    /* Make sure interrupts are disabled */
  T1CCTL2 = 0;    /* Make sure interrupts are disabled */
  T3CCTL0 = 0;    /* Make sure interrupts are disabled */
  T3CCTL1 = 0;    /* Make sure interrupts are disabled */
  T4CCTL0 = 0;    /* Make sure interrupts are disabled */
  T4CCTL1 = 0;    /* Make sure interrupts are disabled */

  /* Setup prescale & clock for timer0 */
  halTimerRecord[HW_TIMER_1].prescale    = HAL_TIMER1_16_PRESCALE;
  halTimerRecord[HW_TIMER_1].clock       = HAL_TIMER_32MHZ;
  halTimerRecord[HW_TIMER_1].prescaleVal = HAL_TIMER1_16_PRESCALE_VAL;

  /* Setup prescale & clock for timer2 */
  halTimerRecord[HW_TIMER_3].prescale    = HAL_TIMER3_8_PRESCALE;
  halTimerRecord[HW_TIMER_3].clock       = HAL_TIMER_32MHZ;
  halTimerRecord[HW_TIMER_3].prescaleVal = HAL_TIMER3_8_PRESCALE_VAL;

  /* Setup prescale & clock for timer3 */
  halTimerRecord[HW_TIMER_4].prescale    = HAL_TIMER4_8_PRESCALE;
  halTimerRecord[HW_TIMER_4].clock       = HAL_TIMER_32MHZ;
  halTimerRecord[HW_TIMER_4].prescaleVal = HAL_TIMER4_8_PRESCALE_VAL;

  /* Setup Timer1 Channel structure */
  halTimerChannel[HW_TIMER_1].TxCCTL =  TCHN_T1CCTL;
  halTimerChannel[HW_TIMER_1].TxCCL =   TCHN_T1CCL;
  halTimerChannel[HW_TIMER_1].TxCCH =   TCHN_T1CCH;
  halTimerChannel[HW_TIMER_1].TxOVF =   TCNH_T1OVF;
  halTimerChannel[HW_TIMER_1].ovfbit =  TCHN_T1OVFBIT;
  halTimerChannel[HW_TIMER_1].intbit =  TCHN_T1INTBIT;

  /* Setup Timer3 Channel structure */
  halTimerChannel[HW_TIMER_3].TxCCTL =  TCHN_T3CCTL;
  halTimerChannel[HW_TIMER_3].TxCCL =   TCHN_T3CCL;
  halTimerChannel[HW_TIMER_3].TxCCH =   TCHN_T3CCH;
  halTimerChannel[HW_TIMER_3].TxOVF =   TCNH_T3OVF;
  halTimerChannel[HW_TIMER_3].ovfbit =  TCHN_T3OVFBIT;
  halTimerChannel[HW_TIMER_3].intbit =  TCHN_T3INTBIT;

  /* Setup Timer4 Channel structure */
  halTimerChannel[HW_TIMER_4].TxCCTL =  TCHN_T4CCTL;
  halTimerChannel[HW_TIMER_4].TxCCL =   TCHN_T4CCL;
  halTimerChannel[HW_TIMER_4].TxCCH =   TCHN_T4CCH;
  halTimerChannel[HW_TIMER_4].TxOVF =   TCNH_T4OVF;
  halTimerChannel[HW_TIMER_4].ovfbit =  TCHN_T4OVFBIT;
  halTimerChannel[HW_TIMER_4].intbit =  TCHN_T4INTBIT;
}

uint8 HalTimerConfig (uint8 timerId, uint8 opMode, uint8 channel, uint8 channelMode,
                      bool intEnable, halTimerCBack_t cBack)
{
  uint8 hwtimerid;

  hwtimerid = halTimerRemap (timerId);

  if ((opMode & HAL_TIMER_MODE_MASK) && (timerId < HAL_TIMER_MAX) &&
      (channelMode & HAL_TIMER_CHANNEL_MASK) && (channel & HAL_TIMER_CHANNEL_MASK))
  {
    halTimerRecord[hwtimerid].configured    = TRUE;
    halTimerRecord[hwtimerid].opMode        = opMode;
    halTimerRecord[hwtimerid].channel       = channel;
    halTimerRecord[hwtimerid].channelMode   = channelMode;
    halTimerRecord[hwtimerid].intEnable     = intEnable;
    halTimerRecord[hwtimerid].callBackFunc  = cBack;
  }
  else
  {
    return HAL_TIMER_PARAMS_ERROR;
  }
  return HAL_TIMER_OK;
}

uint8 HalTimerStart (uint8 timerId, uint32 timePerTick)
{
  uint8 hwtimerid;

  hwtimerid = halTimerRemap (timerId);

  if (halTimerRecord[hwtimerid].configured)
  {
    halTimerSetCount (hwtimerid, timePerTick);
    halTimerSetPrescale (hwtimerid, halTimerRecord[hwtimerid].prescale);
    halTimerSetOpMode (hwtimerid, halTimerRecord[hwtimerid].opMode);
    halTimerSetChannelMode (hwtimerid, halTimerRecord[hwtimerid].channelMode);

    if (hwtimerid == HW_TIMER_3)
    {
      T3CTL |= HAL_TIMER34_START;
    }
    if (hwtimerid == HW_TIMER_4)
    {
      T4CTL |= HAL_TIMER34_START;
    }
    HalTimerInterruptEnable (hwtimerid, halTimerRecord[hwtimerid].channelMode,
                             halTimerRecord[hwtimerid].intEnable);
  }
  else
  {
    return HAL_TIMER_NOT_CONFIGURED;
  }
  return HAL_TIMER_OK;
}

void HalTimerTick (void)
{
  if (!halTimerRecord[HW_TIMER_1].intEnable)
  {
    halProcessTimer1 ();
  }

  if (!halTimerRecord[HW_TIMER_3].intEnable)
  {
    halProcessTimer3 ();
  }

  if (!halTimerRecord[HW_TIMER_4].intEnable)
  {
    halProcessTimer4 ();
  }
}

uint8 HalTimerStop (uint8 timerId)
{
  uint8 hwtimerid;

  hwtimerid = halTimerRemap (timerId);

  switch (hwtimerid)
  {
    case HW_TIMER_1:
      halTimerSetOpMode(HW_TIMER_1, HAL_TIMER_MODE_STOP);
      break;
    case HW_TIMER_3:
      T3CTL &= ~(HAL_TIMER34_START);
      break;
    case HW_TIMER_4:
      T4CTL &= ~(HAL_TIMER34_START);
      break;
    default:
      return HAL_TIMER_INVALID_ID;
  }
  return HAL_TIMER_OK;
}

uint8 halTimerSetCount (uint8 hwtimerid, uint32 timePerTick)
{
  uint16  count;
  uint8   high, low;

  /* Load count = ((sec/tick) x clock) / prescale */
  count = (uint16)((timePerTick * halTimerRecord[hwtimerid].clock) / halTimerRecord[hwtimerid].prescaleVal);
  high = (uint8) (count >> 8);
  low = (uint8) count;

  *(halTimerChannel[hwtimerid].TxCCH) = high;
  *(halTimerChannel[hwtimerid].TxCCL) = low;

  return HAL_TIMER_OK;
}

uint8 halTimerSetPrescale (uint8 hwtimerid, uint8 prescale)
{
  switch (hwtimerid)
  {
    case HW_TIMER_1:
      T1CTL &= ~(HAL_TIMER1_16_TC_BITS);
      T1CTL |= prescale;
      break;
    case HW_TIMER_3:
      T3CTL &= ~(HAL_TIMER34_8_TC_BITS);
      T3CTL |= prescale;
      break;
    case HW_TIMER_4:
      T4CTL &= ~(HAL_TIMER34_8_TC_BITS);
      T4CTL |= prescale;
      break;
    default:
      return HAL_TIMER_INVALID_ID;
  }
  return HAL_TIMER_OK;
}

uint8 halTimerSetOpMode (uint8 hwtimerid, uint8 opMode)
{
  /* Load Waveform Generation Mode */
  switch (opMode)
  {
    case HAL_TIMER_MODE_NORMAL:
      switch (hwtimerid)
      {
        case HW_TIMER_1:
          T1CTL &= ~(HAL_TIMER1_OPMODE_BITS);
          T1CTL |= HAL_TIMER1_OPMODE_FREERUN;
          break;
        case HW_TIMER_3:
          T3CTL &= ~(HAL_TIMER34_OPMODE_BITS);
          T3CTL |= HAL_TIMER34_OPMODE_FREERUN;
          break;
        case HW_TIMER_4:
          T4CTL &= ~(HAL_TIMER34_OPMODE_BITS);
          T4CTL |= HAL_TIMER34_OPMODE_FREERUN;
          break;
        default:
          return HAL_TIMER_INVALID_ID;
      }
      break;

    case HAL_TIMER_MODE_CTC:
      switch (hwtimerid)
      {
        case HW_TIMER_1:
          T1CTL &= ~(HAL_TIMER1_OPMODE_BITS);
          T1CTL |= HAL_TIMER1_OPMODE_MODULO;
          break;
        case HW_TIMER_3:
          T3CTL &= ~(HAL_TIMER34_OPMODE_BITS);
          T3CTL |= HAL_TIMER34_OPMODE_MODULO;
          break;
        case HW_TIMER_4:
          T4CTL &= ~(HAL_TIMER34_OPMODE_BITS);
          T4CTL |= HAL_TIMER34_OPMODE_MODULO;
          break;
        default:
          return HAL_TIMER_INVALID_ID;
      }
      break;

    case HAL_TIMER_MODE_STOP:
      if (hwtimerid == HW_TIMER_1)
      {
        T1CTL &= ~(HAL_TIMER1_OPMODE_BITS);
        T1CTL |= HAL_TIMER1_OPMODE_STOP;
      }
      break;

    default:
      return HAL_TIMER_INVALID_OP_MODE;
  }
  return HAL_TIMER_OK;
}

uint8 halTimerSetChannelMode (uint8 hwtimerid, uint8 channelMode)
{
  switch (channelMode)
  {
    case HAL_TIMER_CH_MODE_OUTPUT_COMPARE:
      *(halTimerChannel[hwtimerid].TxCCTL) &= ~(T134CCTL_CMP_BITS);
      *(halTimerChannel[hwtimerid].TxCCTL) |= (T134CCTL_CMP_OC | T134CCTL_MODE);
      break;

    case HAL_TIMER_CH_MODE_INPUT_CAPTURE:       /* Not Supported */
/*
      *(halTimerChannel[hwtimerid].TxCCTL) &= ~(T134CCTL_CAP_BITS | T134CCTL_MODE);
      *(halTimerChannel[hwtimerid].TxCCTL) |= T134CCTL_CAP_RE;
*/
      break;

    default:
      return HAL_TIMER_INVALID_CH_MODE;
  }
  return HAL_TIMER_OK;
}

uint8 HalTimerInterruptEnable (uint8 hwtimerid, uint8 channelMode, bool enable)
{
  switch (channelMode)
  {
    case HAL_TIMER_CH_MODE_OVERFLOW:

      if (enable)
      {
        *(halTimerChannel[hwtimerid].TxOVF) |= halTimerChannel[hwtimerid].ovfbit;
      }
      else
      {
        *(halTimerChannel[hwtimerid].TxOVF) &= ((halTimerChannel[hwtimerid].ovfbit) ^ 0xFF);
      }
      break;

    case HAL_TIMER_CH_MODE_OUTPUT_COMPARE:
    case HAL_TIMER_CH_MODE_INPUT_CAPTURE:

      if (enable)
      {
        *(halTimerChannel[hwtimerid].TxCCTL) |= T134CCTL_IM;
      }
      else
      {
        *(halTimerChannel[hwtimerid].TxCCTL) &= ~(T134CCTL_IM);
      }
      break;

    default:
      return HAL_TIMER_INVALID_CH_MODE;
  }

  if (halTimerRecord[hwtimerid].intEnable)
  {
    IEN1 |= halTimerChannel[hwtimerid].intbit;
  }
  else
  {
    IEN1 &= ((halTimerChannel[hwtimerid].intbit) ^ 0xFF);
  }
  return HAL_TIMER_OK;
}

void halTimerSendCallBack (uint8 timerId, uint8 channel, uint8 channelMode)
{
  uint8 hwtimerid;

  hwtimerid = halTimerRemap (timerId);

  if (halTimerRecord[hwtimerid].callBackFunc)
    (halTimerRecord[hwtimerid].callBackFunc) (timerId, channel, channelMode);
}

uint8 halTimerRemap (uint8 timerId)
{
  switch (timerId)
  {
    case HAL_TIMER_0:
      return HW_TIMER_3;
    case HAL_TIMER_2:
      return HW_TIMER_4;
    case HAL_TIMER_3:
      return HW_TIMER_1;
    default:
      return HW_TIMER_INVALID;
  }
}

void halProcessTimer1 (void)
{
  if (halTimerRecord[halTimerRemap(HAL_TIMER_3)].channelMode == HAL_TIMER_CH_MODE_OUTPUT_COMPARE)
  {
    if (T1CTL & T1CTL_CH0IF)
    {
      T1CTL &= ~(T1CTL_CH0IF);
      halTimerSendCallBack (HAL_TIMER_3, HAL_TIMER_CHANNEL_A, HAL_TIMER_CH_MODE_OUTPUT_COMPARE);
    }
    if (T1CTL & T1CTL_CH1IF)
    {
      T1CTL &= ~(T1CTL_CH1IF);
      halTimerSendCallBack (HAL_TIMER_3, HAL_TIMER_CHANNEL_B, HAL_TIMER_CH_MODE_OUTPUT_COMPARE);
    }
    if (T1CTL & T1CTL_CH2IF)
    {
      T1CTL &= ~(T1CTL_CH2IF);
      halTimerSendCallBack (HAL_TIMER_3, HAL_TIMER_CHANNEL_C, HAL_TIMER_CH_MODE_OUTPUT_COMPARE);
    }
  }
  else if (halTimerRecord[halTimerRemap(HAL_TIMER_3)].channelMode == HAL_TIMER_CH_MODE_OVERFLOW)
  {
    if (T1CTL & T1CTL_OVFIF)
    {
      T1CTL &= ~(T1CTL_OVFIF);
      halTimerSendCallBack (HAL_TIMER_3, HAL_TIMER_CHANNEL_SINGLE, HAL_TIMER_CH_MODE_OVERFLOW);
    }
  }
}

void halProcessTimer3 (void)
{
  if (halTimerRecord[halTimerRemap(HAL_TIMER_0)].channelMode == HAL_TIMER_CH_MODE_OUTPUT_COMPARE)
  {
    if (TIMIF & TIMIF_T3CH0IF)
    {
      TIMIF &= ~(TIMIF_T3CH0IF);
      halTimerSendCallBack (HAL_TIMER_0, HAL_TIMER_CHANNEL_A, HAL_TIMER_CH_MODE_OUTPUT_COMPARE);
    }
    if (TIMIF & TIMIF_T3CH1IF)
    {
      TIMIF &= ~(TIMIF_T3CH1IF);
      halTimerSendCallBack (HAL_TIMER_0, HAL_TIMER_CHANNEL_B, HAL_TIMER_CH_MODE_OUTPUT_COMPARE);
    }
  }
  else if (halTimerRecord[halTimerRemap(HAL_TIMER_0)].channelMode == HAL_TIMER_CH_MODE_OVERFLOW)
  {
    if (TIMIF & TIMIF_T3OVFIF)
    {
      TIMIF &= ~(TIMIF_T3OVFIF);
      halTimerSendCallBack (HAL_TIMER_0, HAL_TIMER_CHANNEL_SINGLE, HAL_TIMER_CH_MODE_OVERFLOW);
    }
  }
}

void halProcessTimer4 (void)
{
  if (halTimerRecord[halTimerRemap(HAL_TIMER_2)].channelMode == HAL_TIMER_CH_MODE_OUTPUT_COMPARE)
  {
    if (TIMIF & TIMIF_T4CH0IF)
    {
      TIMIF &= ~(TIMIF_T4CH0IF);
      halTimerSendCallBack (HAL_TIMER_2, HAL_TIMER_CHANNEL_A, HAL_TIMER_CH_MODE_OUTPUT_COMPARE);
    }
    if (TIMIF & TIMIF_T4CH1IF)
    {
      TIMIF &= ~(TIMIF_T4CH1IF);
      halTimerSendCallBack (HAL_TIMER_2, HAL_TIMER_CHANNEL_B, HAL_TIMER_CH_MODE_OUTPUT_COMPARE);
    }
  }
  else if (halTimerRecord[halTimerRemap(HAL_TIMER_2)].channelMode == HAL_TIMER_CH_MODE_OVERFLOW)
  if (TIMIF & TIMIF_T4OVFIF)
  {
    TIMIF &= ~(TIMIF_T4OVFIF);
    halTimerSendCallBack (HAL_TIMER_2, HAL_TIMER_CHANNEL_SINGLE, HAL_TIMER_CH_MODE_OVERFLOW);
  }
}

HAL_ISR_FUNCTION( halTimer1Isr, T1_VECTOR )
{
  halProcessTimer1 ();
}

HAL_ISR_FUNCTION( halTimer3Isr, T3_VECTOR )
{
  halProcessTimer3 ();
}

HAL_ISR_FUNCTION( halTimer4Isr, T4_VECTOR )
{
  halProcessTimer4 ();
}

void halMcuWaitUs(uint16 usec)
{
    usec >>= 1;
    while (usec--)
    {
        asm("NOP"); asm("NOP"); asm("NOP");
        asm("NOP"); asm("NOP"); asm("NOP");
        asm("NOP"); asm("NOP"); asm("NOP");
        asm("NOP"); asm("NOP"); asm("NOP");
        asm("NOP"); asm("NOP"); asm("NOP");
        asm("NOP");
    }
}

void halMcuWaitMs(uint16 msec)
{
    while (msec--)
    {
        halMcuWaitUs(1000);
    }
}