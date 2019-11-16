#include "output_handler.h"
#include "avr/wdt.h"
#include "bootloader/bootloader.h"
#include "output_keyboard.h"
#include "output_ps3.h"
#include "output_serial.h"
#include "output_xinput.h"
#include "usb/Descriptors.h"

event_pointers events;
USB_ClassInfo_HID_Device_t interface = {
  Config : {
    InterfaceNumber : INTERFACE_ID_HID,
    ReportINEndpoint : {
      Address : HID_EPADDR_IN,
      Size : HID_EPSIZE,
      Type : EP_TYPE_CONTROL,
      Banks : 1,
    },
    PrevReportINBuffer : NULL,
    PrevReportINBufferSize : sizeof(output_report_size_t),
  },
};
USB_HID_Descriptor_HID_t hid_descriptor = {
  Header : {Size : sizeof(USB_HID_Descriptor_HID_t), Type : HID_DTYPE_HID},

  HIDSpec : VERSION_BCD(1, 1, 1),
  CountryCode : 0x00,
  TotalReportDescriptors : 1,
  HIDReportType : HID_DTYPE_Report,
  HIDReportLength : 0
};
void hid_init(void) {
  memmove(&ConfigurationDescriptor.Controller.HID.Endpoints,
          &ConfigurationDescriptor.Controller.XInput.Endpoints,
          sizeof(ConfigurationDescriptor.Controller.XInput.Endpoints));
  // And now adjust the total size as the HID layout is actually smaller
  ConfigurationDescriptor.Config.TotalConfigurationSize -=
      sizeof(USB_HID_XBOX_Descriptor_HID_t) - sizeof(USB_HID_Descriptor_HID_t);

  hid_descriptor.HIDReportLength = hid_report_size;
  ConfigurationDescriptor.Controller.HID.HIDDescriptor = hid_descriptor;
  // Report that we have an HID device
  ConfigurationDescriptor.Interface0.Class = HID_CSCP_HIDClass;
  ConfigurationDescriptor.Interface0.SubClass = HID_CSCP_NonBootSubclass;
  ConfigurationDescriptor.Interface0.Protocol = HID_CSCP_NonBootProtocol;
}
void output_init(void) {
  ConfigurationDescriptor.Controller.XInput.Endpoints.DataInEndpoint0
      .PollingIntervalMS = config.main.poll_rate;
  if (config.main.sub_type >= KEYBOARD_SUBTYPE) {
    if (config.main.sub_type == KEYBOARD_SUBTYPE) {
      keyboard_init(&events);
    } else {
      ps3_init(&events);
    }
    hid_init();
  } else {
    xinput_init(&events);
  }

  USB_Init();
  sei();
}

void output_tick() {
  wdt_reset();
  HID_Device_USBTask(&interface);
}
void EVENT_USB_Device_ConfigurationChanged(void) {
  HID_Device_ConfigureEndpoints(&interface);
  USB_Device_EnableSOFEvents();
  serial_configuration_changed();
}
void EVENT_USB_Device_ControlRequest(void) {
  if (events.control_request) {
    events.control_request();
  } else {
    HID_Device_ProcessControlRequest(&interface);
  }
  serial_control_request();
}
void EVENT_USB_Device_StartOfFrame(void) {
  HID_Device_MillisecondElapsed(&interface);
}

bool CALLBACK_HID_Device_CreateHIDReport(
    USB_ClassInfo_HID_Device_t *const HIDInterfaceInfo, uint8_t *const ReportID,
    const uint8_t ReportType, void *ReportData, uint16_t *const ReportSize) {
  events.create_hid_report(HIDInterfaceInfo, ReportID, ReportType, ReportData,
                           ReportSize);
  return true;
}

void CALLBACK_HID_Device_ProcessHIDReport(
    USB_ClassInfo_HID_Device_t *const HIDInterfaceInfo, const uint8_t ReportID,
    const uint8_t ReportType, const void *ReportData,
    const uint16_t ReportSize) {}

static char *FW = ARDWIINO_BOARD;
void process_serial(USB_ClassInfo_CDC_Device_t *VirtualSerial_CDC_Interface,
                    bool is_dfu) {
  int16_t b = CDC_Device_ReceiveByte(VirtualSerial_CDC_Interface);
  void* buffer;
  uint16_t size = 0;
  bool w = false;
  switch (b) {
  case MAIN_CMD_R:
    buffer = &config.main;
    size = sizeof(main_config_t);
    break;
  case MAIN_CMD_W:
    buffer = &config.main;
    size = sizeof(main_config_t);
    w = true;
    break;
  case PIN_CMD_R:
    buffer = &config.pins;
    size = sizeof(pins_t);
    break;
  case PIN_CMD_W:
    buffer = &config.pins;
    size = sizeof(pins_t);
    w = true;
    break;
  case AXIS_CMD_R:
    buffer = &config.axis;
    size = sizeof(axis_config_t);
    break;
  case AXIS_CMD_W:
    buffer = &config.axis;
    size = sizeof(axis_config_t);
    w = true;
    break;
  case KEY_CMD_R:
    buffer = &config.keys;
    size = sizeof(keys_t);
    break;
  case KEY_CMD_W:
    buffer = &config.keys;
    size = sizeof(keys_t);
    w = true;
    break;
  case CONTROLLER_CMD_R:
    buffer = &controller;
    size = sizeof(controller_t);
    break;
  case FW_CMD_R:
    CDC_Device_SendString(VirtualSerial_CDC_Interface, FW);
    break;
  case DFU_CMD_R:
    CDC_Device_SendByte(VirtualSerial_CDC_Interface, is_dfu);
    break;
  case REBOOT_CMD:
    reboot();
    break;
  }
  if (size > 0) {
    if (w) {
      uint8_t *data = (uint8_t *)&config_pointer;
      size_t i = 0;
      uint16_t ptr = (size_t)buffer - (size_t)&config;
      while (i < size) {
        eeprom_write_byte(data + i + ptr,
                          CDC_Device_ReceiveByte(VirtualSerial_CDC_Interface));
        i++;
      }
      reboot();
    } else {
      CDC_Device_SendData(VirtualSerial_CDC_Interface, buffer, size);
    }
  }
  CDC_Device_USBTask(VirtualSerial_CDC_Interface);
  USB_USBTask();
}