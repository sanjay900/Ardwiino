/*
             LUFA Library
     Copyright (C) Dean Camera, 2010.

  dean [at] fourwalledcubicle [dot] com
      www.fourwalledcubicle.com
*/

/*
  Copyright 2010  Dean Camera (dean [at] fourwalledcubicle [dot] com)

  Permission to use, copy, modify, distribute, and sell this
  software and its documentation for any purpose is hereby granted
  without fee, provided that the above copyright notice appear in
  all copies and that both that the copyright notice and this
  permission notice and warranty disclaimer appear in supporting
  documentation, and that the name of the author not be used in
  advertising or publicity pertaining to distribution of the
  software without specific, written prior permission.

  The author disclaim all warranties with regard to this
  software, including all implied warranties of merchantability
  and fitness.  In no event shall the author be liable for any
  special, indirect or consequential damages or any damages
  whatsoever resulting from loss of use, data or profits, whether
  in an action of contract, negligence or other tortious action,
  arising out of or in connection with the use or performance of
  this software.
*/

/** \file
 *
 *  Main source file for the Arduino-usbserial project. This file contains the
 * main tasks of the project and is responsible for the initial application
 * hardware configuration.
 */

#include "main.h"
#include "../../shared/output/usb/API.h"

/** Circular buffer to hold data from the host before it is sent to the device
 * via the serial port. */
RingBuff_t USBtoUSART_Buffer;

/** Circular buffer to hold data from the serial port before it is sent to the
 * host. */
RingBuff_t USARTtoUSB_Buffer;

/** Pulse generation counters to keep track of the number of milliseconds
 * remaining for each pulse type */
volatile struct {
  uint8_t TxLEDPulse;       /**< Milliseconds remaining for data Tx LED pulse */
  uint8_t RxLEDPulse;       /**< Milliseconds remaining for data Rx LED pulse */
  uint8_t PingPongLEDPulse; /**< Milliseconds remaining for enumeration Tx/Rx
                               ping-pong LED pulse */
} PulseMSRemaining;

/** Contains the current baud rate and other settings of the first virtual
 * serial port. This must be retained as some operating systems will not open
 * the port unless the settings can be set successfully.
 */
static CDC_LineEncoding_t LineEncoding = {.BaudRateBPS = 0,
                                          .CharFormat =
                                              CDC_LINEENCODING_OneStopBit,
                                          .ParityType = CDC_PARITY_None,
                                          .DataBits = 8};
#define STATE_ARDWIINO 0
#define STATE_CONFIG 1
#define STATE_AVRDUDE 2
eeprom_config_t EEMEM config_mem = {.polling_rate = POLL_RATE,
                                    .device_type = DEVICE_TYPE,
                                    .id = ARDWIINO_DEVICE_TYPE};

eeprom_config_t config = {.id = ARDWIINO_DEVICE_TYPE};
bool entered_prog = false;
int state = STATE_ARDWIINO;
int lastCommand = 0;
int lastAddr = 0;
static void jump_atmel_bootloader(void) {
  USB_Disable();
  // disable interrupts
  cli();

  // Relocate the interrupt vector table to the bootloader section
  MCUCR = (1 << IVCE);
  MCUCR = (1 << IVSEL);

  // Jump to the bootloader section
  asm volatile("jmp 0x1000");
}

const char *mcu = MCU;
const char *freq = STR(F_CPU);
/** Main program entry point. This routine contains the overall program flow,
 * including initial setup of all components and the main program loop.
 */
int main(void) {
  SetupHardware();
  eeprom_read_block(&config, &config_mem, sizeof(eeprom_config_t));
  if (config.id == ARDWIINO_DEVICE_TYPE) {
    polling_rate = config.polling_rate;
    device_type = config.device_type;
  }
  RingBuffer_InitBuffer(&USBtoUSART_Buffer, (RingBuff_Data_t *)0x100);
  RingBuffer_InitBuffer(&USARTtoUSB_Buffer, (RingBuff_Data_t *)0x200);
  sei();
  for (;;) {
    for (;;) {
      // USB Task
      uint8_t lastState = USB_DeviceState;
      Endpoint_SelectEndpoint(ENDPOINT_CONTROLEP);
      if (Endpoint_IsSETUPReceived()) USB_Device_ProcessControlRequest();

      // Compare last with new state
      uint8_t newState = USB_DeviceState;
      if (newState != DEVICE_STATE_Configured) {
        // Try to reconnect if communication is still broken
        if (lastState != DEVICE_STATE_Configured) continue;
        // Break and disable USART on connection lost
        else
          break;
      }

#define TX_RX_LED_PULSE_MS 5
      /* Only try to read in bytes from the CDC interface if the transmit buffer
       * is not full */
      if (!(RingBuffer_IsFull(&USBtoUSART_Buffer))) {
        Endpoint_SelectEndpoint(CDC_RX_EPADDR);
        /* Read bytes from the USB OUT endpoint into the USART transmit buffer
         */
        if (Endpoint_IsOUTReceived()) {
          if (Endpoint_BytesInEndpoint()) {
            uint8_t b = Endpoint_Read_8();
            RingBuffer_Insert(&USBtoUSART_Buffer, b);
            if (state == STATE_CONFIG) {
              if (lastCommand == COMMAND_READ_INFO) {
                const char *c = NULL;
                if (b == INFO_USB_MCU) {
                  c = mcu;
                } else if (b == INFO_USB_CPU_FREQ) {
                  c = freq;
                }
                if (c != NULL) {
                  while (*(c) != 0) {
                    RingBuffer_Insert(&USARTtoUSB_Buffer, *(c++));
                  }
                }
                lastCommand = 0;
              } else if (lastCommand == 0) {
                if (b == COMMAND_APPLY_CONFIG) {
                  USB_ResetInterface();
                  config.id = ARDWIINO_DEVICE_TYPE;
                  config.polling_rate = polling_rate;
                  config.device_type = device_type;
                  eeprom_update_block(&config, &config_mem,
                                      sizeof(eeprom_config_t));
                  state = STATE_ARDWIINO;
                }
                if (b == COMMAND_JUMP_BOOTLOADER) state = STATE_AVRDUDE;
                if (b == COMMAND_WRITE_CONFIG_VALUE) {
                  b = Endpoint_Read_8();
                  if (b == CONFIG_SUB_TYPE) {
                    device_type = Endpoint_Read_8();
                  } else if (b == CONFIG_POLL_RATE) {
                    polling_rate = Endpoint_Read_8();
                  }
                }
                if (b == COMMAND_READ_INFO) { lastCommand = b; }
              }
            } else if (state == STATE_ARDWIINO) {
              if (b == COMMAND_START_CONFIG) state = STATE_CONFIG;
              if (b == COMMAND_JUMP_BOOTLOADER) state = STATE_AVRDUDE;
              if (b == COMMAND_JUMP_BOOTLOADER_UNO) jump_atmel_bootloader();
            } else {
              if (b == COMMAND_STK_500_ENTER_PROG && !entered_prog) {
                entered_prog = true;
              }
            }
          }

          if (!(Endpoint_BytesInEndpoint())) Endpoint_ClearOUT();
        }
      }
      if (state == STATE_ARDWIINO) {
        uint8_t ReportSize;
        if (device_type <= XINPUT_ARCADE_PAD_SUBTYPE) {
          ReportSize = sizeof(USB_XInputReport_Data_t);
        } else if (device_type == KEYBOARD_SUBTYPE) {
          ReportSize = sizeof(USB_KeyboardReport_Data_t);
        } else {
          ReportSize = sizeof(USB_PS3Report_Data_t);
        }
        if (USARTtoUSB_Buffer.Count > ReportSize + 1 &&
            RingBuffer_Remove(&USARTtoUSB_Buffer) == 'm') {
          Endpoint_SelectEndpoint(HID_EPADDR_IN);
          if (Endpoint_IsReadWriteAllowed()) {
            if (Endpoint_IsINReady()) {
              while (ReportSize--) {
                Endpoint_Write_8(RingBuffer_Remove(&USARTtoUSB_Buffer));
              }
              Endpoint_ClearIN();
            }
          }
        }
      } else {

        /* Check if the UART receive buffer flush timer has expired or the
        buffer
         * is nearly full */
        RingBuff_Count_t BufferCount = RingBuffer_GetCount(&USARTtoUSB_Buffer);
        if ((TIFR0 & (1 << TOV0)) || (BufferCount > BUFFER_NEARLY_FULL)) {
          TIFR0 |= (1 << TOV0);

          if (USARTtoUSB_Buffer.Count) {
            LEDs_TurnOnLEDs(LEDMASK_TX);
            PulseMSRemaining.TxLEDPulse = TX_RX_LED_PULSE_MS;
          }

          Endpoint_SelectEndpoint(CDC_TX_EPADDR);
          // CDC device is ready for receiving bytes
          if (Endpoint_IsINReady()) {
            /* Read bytes from the USART receive buffer into the USB IN
            endpoint
             */
            while (BufferCount--)
              Endpoint_Write_8(RingBuffer_Remove(&USARTtoUSB_Buffer));
            Endpoint_ClearIN();
          }
          /* Turn off TX LED(s) once the TX pulse period has elapsed */
          if (PulseMSRemaining.TxLEDPulse && !(--PulseMSRemaining.TxLEDPulse))
            LEDs_TurnOffLEDs(LEDMASK_TX);

          /* Turn off RX LED(s) once the RX pulse period has elapsed */
          if (PulseMSRemaining.RxLEDPulse && !(--PulseMSRemaining.RxLEDPulse))
            LEDs_TurnOffLEDs(LEDMASK_RX);
        }
      }

      /* Load the next byte from the USART transmit buffer into the USART */
      if (!(RingBuffer_IsEmpty(&USBtoUSART_Buffer))) {
        Serial_SendByte(RingBuffer_Remove(&USBtoUSART_Buffer));

        LEDs_TurnOnLEDs(LEDMASK_RX);
        PulseMSRemaining.RxLEDPulse = TX_RX_LED_PULSE_MS;
      }
    }

    // Dont forget LEDs on if suddenly unconfigured.
    LEDs_TurnOffTXLED;
    LEDs_TurnOffRXLED;
  }
}

/** Configures the board hardware and chip peripherals for the demo's
 * functionality. */
void SetupHardware(void) {
  /* Disable watchdog if enabled by bootloader/fuses */
  MCUSR &= ~(1 << WDRF);
  wdt_disable();

  /* Hardware Initialization */
  Serial_Init(115200, true);
  UCSR1B = ((1 << RXCIE1) | (1 << TXEN1) | (1 << RXEN1));
  LEDs_Init();
  USB_Init();

  /* Start the flush timer so that overflows occur rapidly to push received
   * bytes to the USB interface */
  TCCR0B = (1 << CS02);

  /* Pull target /RESET line high */
  AVR_RESET_LINE_PORT |= AVR_RESET_LINE_MASK;
  AVR_RESET_LINE_DDR |= AVR_RESET_LINE_MASK;
}

/** Event handler for the library USB Configuration Changed event. */
void EVENT_USB_Device_ConfigurationChanged(void) {

  /* Setup CDC Notification, Rx and Tx Endpoints */
  Endpoint_ConfigureEndpoint(CDC_NOTIFICATION_EPADDR, EP_TYPE_INTERRUPT,
                             CDC_NOTIFICATION_EPSIZE, 1);

  Endpoint_ConfigureEndpoint(CDC_TX_EPADDR, EP_TYPE_BULK, CDC_TX_EPSIZE,
                             CDC_TX_BANK_SIZE);

  Endpoint_ConfigureEndpoint(CDC_RX_EPADDR, EP_TYPE_BULK, CDC_RX_EPSIZE,
                             CDC_RX_BANK_SIZE);
  Endpoint_ConfigureEndpoint(HID_EPADDR_IN, EP_TYPE_INTERRUPT, HID_EPSIZE, 1);
}

/** Event handler for the library USB Unhandled Control Request event. */
void EVENT_USB_Device_ControlRequest(void) {
  controller_control_request();
  /* Ignore any requests that aren't directed to the CDC interface */
  if ((USB_ControlRequest.bmRequestType &
       (CONTROL_REQTYPE_TYPE | CONTROL_REQTYPE_RECIPIENT)) !=
      (REQTYPE_CLASS | REQREC_INTERFACE)) {
    return;
  }
  /* Process CDC specific control requests */
  uint8_t bRequest = USB_ControlRequest.bRequest;
  if (bRequest == CDC_REQ_GetLineEncoding) {
    if (USB_ControlRequest.bmRequestType ==
        (REQDIR_DEVICETOHOST | REQTYPE_CLASS | REQREC_INTERFACE)) {
      Endpoint_ClearSETUP();

      /* Write the line coding data to the control endpoint */
      // this one is not inline because its already used somewhere in the usb
      // core, so it will dupe code
      Endpoint_Write_Control_Stream_LE(&LineEncoding,
                                       sizeof(CDC_LineEncoding_t));
      Endpoint_ClearOUT();
    }
  } else if (bRequest == CDC_REQ_SetLineEncoding) {
    if (USB_ControlRequest.bmRequestType ==
        (REQDIR_HOSTTODEVICE | REQTYPE_CLASS | REQREC_INTERFACE)) {
      Endpoint_ClearSETUP();
      // Read the line coding data in from the host into the global struct (made
      // inline)
      // Endpoint_Read_Control_Stream_LE(&LineEncoding,
      // sizeof(CDC_LineEncoding_t));

      uint8_t Length = sizeof(CDC_LineEncoding_t);
      uint8_t *DataStream = (uint8_t *)&LineEncoding;

      bool skip = false;
      while (Length) {
        uint8_t USB_DeviceState_LCL = USB_DeviceState;

        if ((USB_DeviceState_LCL == DEVICE_STATE_Unattached) ||
            (USB_DeviceState_LCL == DEVICE_STATE_Suspended) ||
            (Endpoint_IsSETUPReceived())) {
          skip = true;
          break;
        }

        if (Endpoint_IsOUTReceived()) {
          while (Length && Endpoint_BytesInEndpoint()) {
            *DataStream = Endpoint_Read_8();
            DataStream++;
            Length--;
          }

          Endpoint_ClearOUT();
        }
      }

      if (!skip) {
        do {
          uint8_t USB_DeviceState_LCL = USB_DeviceState;

          if ((USB_DeviceState_LCL == DEVICE_STATE_Unattached) ||
              (USB_DeviceState_LCL == DEVICE_STATE_Suspended))
            break;
        } while (!(Endpoint_IsINReady()));
      }

      // end of inline Endpoint_Read_Control_Stream_LE

      Endpoint_ClearIN();
    }
  } else if (bRequest == CDC_REQ_SetControlLineState) {
    if (USB_ControlRequest.bmRequestType ==
        (REQDIR_HOSTTODEVICE | REQTYPE_CLASS | REQREC_INTERFACE)) {
      Endpoint_ClearSETUP();
      Endpoint_ClearStatusStage();
      Board_Reset(USB_ControlRequest.wValue & CDC_CONTROL_LINE_OUT_DTR);
      // The next dtr after programming will reset the device.
      if (entered_prog) {
        entered_prog = false;
        state = STATE_CONFIG;
      }
    }
  }
}

/** ISR to manage the reception of data from the serial port, placing received
 * bytes into a circular buffer for later transmission to the host.
 */
ISR(USART1_RX_vect, ISR_BLOCK) {
  uint8_t ReceivedByte = UDR1;

  if (USB_DeviceState == DEVICE_STATE_Configured)
    RingBuffer_Insert(&USARTtoUSB_Buffer, ReceivedByte);
}
