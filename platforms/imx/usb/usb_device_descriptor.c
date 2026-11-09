/*
 * Copyright (c) 2015 - 2016, Freescale Semiconductor, Inc.
 * Copyright 2016 - 2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "usb.h"
#include "usb_device.h"
#include "usb_device_config.h"

#include "usb_device_class.h"

#include "usb_device_audio.h"
#include "usb_device_cdc_acm.h"

#include "composite.h"
#include "usb_device_descriptor.h"

extern usb_composite_state_t g_UsbState;

/*******************************************************************************
 * Variables
 ******************************************************************************/

/* midi information */

/* Define endpoint for MIDI class */
/* The midi class driver only supports one bulk OUT endpoint and one bulk IN endpoint */
usb_device_endpoint_struct_t g_MidiEndpoints[USB_MIDI_ENDPOINT_COUNT] = {
    {
      USB_MIDI_BULK_IN_ENDPOINT | (USB_IN << 7U),
      USB_ENDPOINT_BULK,
      HS_MIDI_BULK_IN_PACKET_SIZE,
      0U,
    },
    {
      USB_MIDI_BULK_OUT_ENDPOINT | (USB_OUT << 7U),
      USB_ENDPOINT_BULK,
      HS_MIDI_BULK_OUT_PACKET_SIZE,
      0U,
    }
};

/* Define interface for MIDI class */
usb_device_interface_struct_t g_MidiInterface[] = { { USB_MIDI_INTERFACE_ALTERNATE_0,
                                                      {
                                                        USB_MIDI_ENDPOINT_COUNT,
                                                        g_MidiEndpoints,
                                                      },
                                                      NULL } };

/* Define interfaces for midi */
usb_device_interfaces_struct_t g_MidiInterfaces[USB_MIDI_INTERFACE_COUNT] = { {
  USB_MIDI_CLASS,
  USB_MIDI_SUBCLASS,
  USB_MIDI_PROTOCOL,
  USB_MIDI_INTERFACE_INDEX,
  g_MidiInterface,
  sizeof(g_MidiInterface) / sizeof(usb_device_interface_struct_t),
} };
/* Define configurations for midi */
usb_device_interface_list_t g_MidiInterfaceList[USB_DEVICE_CONFIGURATION_COUNT] = {
    {
      USB_MIDI_INTERFACE_COUNT,
      g_MidiInterfaces,
    },
};

/* Define class information for midi */
usb_device_class_struct_t g_UsbDeviceMidiClass = {
    g_MidiInterfaceList,
    kUSB_DeviceClassTypeMidi,
    USB_DEVICE_CONFIGURATION_COUNT,
};

/* cdc virtual com information */
/* Define endpoint for communication class */
usb_device_endpoint_struct_t g_cdcVcomCicEndpoints[USB_CDC_VCOM_CIC_ENDPOINT_COUNT] = {
    {
      USB_CDC_VCOM_CIC_INTERRUPT_IN_ENDPOINT | (USB_IN << 7U),
      USB_ENDPOINT_INTERRUPT,
      FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE,
      FS_CDC_VCOM_INTERRUPT_IN_INTERVAL,
    },
};

/* Define endpoint for data class */
usb_device_endpoint_struct_t g_cdcVcomDicEndpoints[USB_CDC_VCOM_DIC_ENDPOINT_COUNT] = {
    {
      USB_CDC_VCOM_DIC_BULK_IN_ENDPOINT | (USB_IN << 7U),
      USB_ENDPOINT_BULK,
      FS_CDC_VCOM_BULK_IN_PACKET_SIZE,
      0U,
    },
    {
      USB_CDC_VCOM_DIC_BULK_OUT_ENDPOINT | (USB_OUT << 7U),
      USB_ENDPOINT_BULK,
      FS_CDC_VCOM_BULK_OUT_PACKET_SIZE,
      0U,
    },
};

/* Define interface for communication class */
usb_device_interface_struct_t g_cdcVcomCicInterface[] = { { USB_CDC_VCOM_CIC_INTERFACE_ALTERNATE_0,
                                                            {
                                                              USB_CDC_VCOM_CIC_ENDPOINT_COUNT,
                                                              g_cdcVcomCicEndpoints,
                                                            },
                                                            NULL } };

/* Define interface for data class */
usb_device_interface_struct_t g_cdcVcomDicInterface[] = { { USB_CDC_VCOM_DIC_INTERFACE_ALTERNATE_0,
                                                            {
                                                              USB_CDC_VCOM_DIC_ENDPOINT_COUNT,
                                                              g_cdcVcomDicEndpoints,
                                                            },
                                                            NULL } };

/* Define interfaces for virtual com */
usb_device_interfaces_struct_t g_cdcVcomInterfaces[USB_CDC_VCOM_INTERFACE_COUNT] = {
    { USB_CDC_VCOM_CIC_CLASS,
      USB_CDC_VCOM_CIC_SUBCLASS,
      USB_CDC_VCOM_CIC_PROTOCOL,
      0xFFU,
      g_cdcVcomCicInterface,
      sizeof(g_cdcVcomCicInterface) / sizeof(usb_device_interface_struct_t) },
    { USB_CDC_VCOM_DIC_CLASS,
      USB_CDC_VCOM_DIC_SUBCLASS,
      USB_CDC_VCOM_DIC_PROTOCOL,
      0xFFU,
      g_cdcVcomDicInterface,
      sizeof(g_cdcVcomDicInterface) / sizeof(usb_device_interface_struct_t) },
};

/* Define configurations for virtual com */
usb_device_interface_list_t g_UsbDeviceCdcVcomInterfaceList[USB_DEVICE_CONFIGURATION_COUNT] = {
    {
      USB_CDC_VCOM_INTERFACE_COUNT,
      g_cdcVcomInterfaces,
    },
};

/* Define class information for virtual com */
usb_device_class_struct_t g_UsbDeviceCdcVcomConfig = {
    g_UsbDeviceCdcVcomInterfaceList,
    kUSB_DeviceClassTypeCdc,
    USB_DEVICE_CONFIGURATION_COUNT,
};

/* Audio generator stream endpoint information */
usb_device_endpoint_struct_t
  g_UsbDeviceAudioRecorderEndpoints[USB_AUDIO_RECORDER_STREAM_ENDPOINT_COUNT] = {
      /* Audio generator ISO IN pipe */
      {
        USB_AUDIO_RECORDER_STREAM_ENDPOINT |
          (USB_IN << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT),
        USB_ENDPOINT_ISOCHRONOUS,
        HS_ISO_IN_ENDP_PACKET_SIZE,
        HS_ISO_IN_ENDP_INTERVAL,
      },
  };

usb_device_endpoint_struct_t
  g_UsbDeviceAudioPlayerEndpoints[USB_AUDIO_PLAYER_STREAM_ENDPOINT_COUNT] = {
      /* Audio player ISO OUT pipe */
      {
        USB_AUDIO_PLAYER_STREAM_ENDPOINT |
          (USB_OUT << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT),
        USB_ENDPOINT_ISOCHRONOUS,
        HS_ISO_OUT_ENDP_PACKET_SIZE,
        HS_ISO_OUT_ENDP_INTERVAL,
      },
      /* Audio feedback ISO IN pipe */
      {
        USB_AUDIO_PLAYER_FEEDBACK_ENDPOINT |
          (USB_IN << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT),
        USB_ENDPOINT_ISOCHRONOUS,
        HS_ISO_FEEDBACK_ENDP_PACKET_SIZE,
        FS_ISO_FEEDBACK_ENDP_INTERVAL,
      }
  };

/* Audio generator control endpoint information */
usb_device_endpoint_struct_t
  g_UsbDeviceAudioControlEndpoints[USB_AUDIO_CONTROL_ENDPOINT_COUNT] = {};

/* Audio generator entity struct */
usb_device_audio_entity_struct_t g_UsbDeviceAudioEntity[] = {
#if (USB_DEVICE_CONFIG_AUDIO_CLASS_2_0)
    {
      USB_AUDIO_CONTROL_CLOCK_SOURCE_ENTITY_ID,
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_CLOCK_SOURCE_UNIT,
      0U,
    },
#endif
    {
      USB_AUDIO_RECORDER_CONTROL_INPUT_TERMINAL_ID,
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_INPUT_TERMINAL,
      0U,
    },
    {
      USB_AUDIO_RECORDER_CONTROL_OUTPUT_TERMINAL_ID,
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_OUTPUT_TERMINAL,
      0U,
    },
    {
      USB_AUDIO_PLAYER_CONTROL_INPUT_TERMINAL_ID,
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_INPUT_TERMINAL,
      0U,
    },
    {
      USB_AUDIO_PLAYER_CONTROL_OUTPUT_TERMINAL_ID,
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_OUTPUT_TERMINAL,
      0U,
    },
};

/* Audio generator entity information */
usb_device_audio_entities_struct_t g_UsbDeviceAudioEntities = {
    g_UsbDeviceAudioEntity,
    sizeof(g_UsbDeviceAudioEntity) / sizeof(usb_device_audio_entity_struct_t),
};

/* Audio generator control interface information */
usb_device_interface_struct_t g_UsbDeviceAudioControInterface[] = { {
  USB_AUDIO_CONTROL_INTERFACE_ALTERNATE_0,
  {
    USB_AUDIO_CONTROL_ENDPOINT_COUNT,
    g_UsbDeviceAudioControlEndpoints,
  },
  &g_UsbDeviceAudioEntities,
} };

/* Audio generator stream interface information */
usb_device_interface_struct_t g_UsbDeviceAudioRecStreamInterface[] = {
    {
      USB_AUDIO_RECORDER_STREAM_INTERFACE_ALTERNATE_0,
      { 0U, NULL },
      NULL,
    },
    {
      USB_AUDIO_RECORDER_STREAM_INTERFACE_ALTERNATE_1,
      {
        USB_AUDIO_RECORDER_STREAM_ENDPOINT_COUNT,
        g_UsbDeviceAudioRecorderEndpoints,
      },
      NULL,
    },
};

/* Audio generator stream interface information */
usb_device_interface_struct_t g_UsbDeviceAudioPlayStreamInterface[] = {
    {
      USB_AUDIO_PLAYER_STREAM_INTERFACE_ALTERNATE_0,
      { 0U, NULL },
      NULL,
    },
    {
      USB_AUDIO_PLAYER_STREAM_INTERFACE_ALTERNATE_1,
      {
        USB_AUDIO_PLAYER_STREAM_ENDPOINT_COUNT,
        g_UsbDeviceAudioPlayerEndpoints,
      },
      NULL,
    },
};

/* Define interfaces for audio generator */
usb_device_interfaces_struct_t
  g_UsbDeviceAudioRecorderInterfaces[USB_AUDIO_RECORDER_INTERFACE_COUNT] = {
      {
        USB_AUDIO_CLASS,                 /* Audio class code */
        USB_SUBCLASS_AUDIOCONTROL,       /* Audio control subclass code */
        USB_AUDIO_PROTOCOL,              /* Audio protocol code */
        0xFFU,                           /* The interface number of the Audio control */
        g_UsbDeviceAudioControInterface, /* The handle of Audio control */
        sizeof(g_UsbDeviceAudioControInterface) / sizeof(usb_device_interface_struct_t),
      },
      {
        USB_AUDIO_CLASS,                    /* Audio class code */
        USB_SUBCLASS_AUDIOSTREAM,           /* Audio stream subclass code */
        USB_AUDIO_PROTOCOL,                 /* Audio protocol code */
        0xFFU,                              /* The interface number of the Audio control */
        g_UsbDeviceAudioRecStreamInterface, /* The handle of Audio stream */
        sizeof(g_UsbDeviceAudioRecStreamInterface) / sizeof(usb_device_interface_struct_t),
      },
  };

/* Define interfaces for audio generator */
usb_device_interfaces_struct_t
  g_UsbDeviceAudioPlayerInterfaces[USB_AUDIO_PLAYER_INTERFACE_COUNT] = {
      {
        USB_AUDIO_CLASS,                 /* Audio class code */
        USB_SUBCLASS_AUDIOCONTROL,       /* Audio control subclass code */
        USB_AUDIO_PROTOCOL,              /* Audio protocol code */
        0xFFU,                           /* The interface number of the Audio control */
        g_UsbDeviceAudioControInterface, /* The handle of Audio control */
        sizeof(g_UsbDeviceAudioControInterface) / sizeof(usb_device_interface_struct_t),
      },
      {
        USB_AUDIO_CLASS,                     /* Audio class code */
        USB_SUBCLASS_AUDIOSTREAM,            /* Audio stream subclass code */
        USB_AUDIO_PROTOCOL,                  /* Audio protocol code */
        0xFFU,                               /* The interface number of the Audio control */
        g_UsbDeviceAudioPlayStreamInterface, /* The handle of Audio stream */
        sizeof(g_UsbDeviceAudioPlayStreamInterface) / sizeof(usb_device_interface_struct_t),
      },
  };

usb_device_interface_list_t
  g_UsbDeviceAudioInterfaceListRecorder[USB_DEVICE_CONFIGURATION_COUNT] = {
      {
        USB_AUDIO_RECORDER_INTERFACE_COUNT,
        g_UsbDeviceAudioRecorderInterfaces,
      },
  };

usb_device_interface_list_t g_UsbDeviceAudioInterfaceListPlayer[USB_DEVICE_CONFIGURATION_COUNT] = {
    {
      USB_AUDIO_PLAYER_INTERFACE_COUNT,
      g_UsbDeviceAudioPlayerInterfaces,
    },
};

usb_device_class_struct_t g_UsbDeviceAudioClassRecorder = {
    g_UsbDeviceAudioInterfaceListRecorder,
    kUSB_DeviceClassTypeAudio,
    USB_DEVICE_CONFIGURATION_COUNT,
};

usb_device_class_struct_t g_UsbDeviceAudioClassPlayer = {
    g_UsbDeviceAudioInterfaceListPlayer,
    kUSB_DeviceClassTypeAudio,
    USB_DEVICE_CONFIGURATION_COUNT,
};

/* msc disk information */
/* Define endpoint for MSC class */
/* The msc class driver only supports one bulk OUT endpoint and one bulk IN endpoint */
usb_device_endpoint_struct_t g_mscDiskEndpoints[USB_MSC_DISK_ENDPOINT_COUNT] = {
    {
      USB_MSC_DISK_BULK_IN_ENDPOINT | (USB_IN << 7U),
      USB_ENDPOINT_BULK,
      FS_MSC_DISK_BULK_IN_PACKET_SIZE,
      0U,
    },
    {
      USB_MSC_DISK_BULK_OUT_ENDPOINT | (USB_OUT << 7U),
      USB_ENDPOINT_BULK,
      FS_MSC_DISK_BULK_OUT_PACKET_SIZE,
      0U,
    }
};

/* Define interface for MSC class */
usb_device_interface_struct_t g_mscDiskInterface[] = { { USB_MSC_DISK_INTERFACE_ALTERNATE_0,
                                                         {
                                                           USB_MSC_DISK_ENDPOINT_COUNT,
                                                           g_mscDiskEndpoints,
                                                         },
                                                         NULL } };

/* Define interfaces for MSC disk */
usb_device_interfaces_struct_t g_mscDiskInterfaces[USB_MSC_DISK_INTERFACE_COUNT] = { {
  USB_MSC_DISK_CLASS,
  USB_MSC_DISK_SUBCLASS,
  USB_MSC_DISK_PROTOCOL,
  0xFFU,
  g_mscDiskInterface,
  sizeof(g_mscDiskInterface) / sizeof(usb_device_interface_struct_t),
} };

/* Define configurations for MSC disk */
usb_device_interface_list_t g_mscDiskInterfaceList[USB_DEVICE_CONFIGURATION_COUNT] = {
    {
      USB_MSC_DISK_INTERFACE_COUNT,
      g_mscDiskInterfaces,
    },
};

/* Define class information for MSC disk */
usb_device_class_struct_t g_UsbDeviceMscClass = {
    g_mscDiskInterfaceList,
    kUSB_DeviceClassTypeMsc,
    USB_DEVICE_CONFIGURATION_COUNT,
};

/* Define device descriptor */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceDescriptor[] = {
    /* Size of this descriptor in bytes */
    USB_DESCRIPTOR_LENGTH_DEVICE,
    /* DEVICE Descriptor Type */
    USB_DESCRIPTOR_TYPE_DEVICE,
    /* USB Specification Release Number in Binary-Coded Decimal (i.e., 2.10 is 210H). */
    USB_SHORT_GET_LOW(USB_DEVICE_SPECIFIC_BCD_VERSION),
    USB_SHORT_GET_HIGH(USB_DEVICE_SPECIFIC_BCD_VERSION),
    /* Class code (assigned by the USB-IF). */
    USB_DEVICE_CLASS,
    /* Subclass code (assigned by the USB-IF). */
    USB_DEVICE_SUBCLASS,
    /* Protocol code (assigned by the USB-IF). */
    USB_DEVICE_PROTOCOL,
    /* Maximum packet size for endpoint zero (only 8, 16, 32, or 64 are valid) */
    USB_CONTROL_MAX_PACKET_SIZE,
    USB_SHORT_GET_LOW(USB_DEVICE_VID),
    USB_SHORT_GET_HIGH(USB_DEVICE_VID), /* Vendor ID (assigned by the USB-IF) */
    USB_SHORT_GET_LOW(USB_DEVICE_PID),
    USB_SHORT_GET_HIGH(USB_DEVICE_PID), /* Product ID (assigned by the manufacturer) */
    /* Device release number in binary-coded decimal */
    USB_SHORT_GET_LOW(USB_DEVICE_DEMO_BCD_VERSION),
    USB_SHORT_GET_HIGH(USB_DEVICE_DEMO_BCD_VERSION),
    /* Index of string descriptor describing manufacturer */
    0x01,
    /* Index of string descriptor describing product */
    0x02,
    /* Index of string descriptor describing the device's serial number */
    0x00,
    /* Number of possible configurations */
    USB_DEVICE_CONFIGURATION_COUNT,
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
usb_midi_interface_descriptor_t g_UsbMidiInterfaceDescriptor = {
    /* MIDI Interface Descriptor */
    .iface = {
      USB_DESCRIPTOR_LENGTH_INTERFACE,
      USB_DESCRIPTOR_TYPE_INTERFACE,
      USB_MIDI_INTERFACE_INDEX,
      USB_MIDI_INTERFACE_ALTERNATE_0,
      USB_MIDI_ENDPOINT_COUNT,
      USB_MIDI_CLASS,
      USB_MIDI_SUBCLASS,
      USB_MIDI_PROTOCOL,
      0x04, /* Interface Description String Index */
    },
    .cs_iface = {
      /* MIDI CS Interface Descriptor */
      USB_DESCRIPTOR_LENGTH_MS_INTERFACE,
      USB_DESCRIPTOR_TYPE_CS_INTERFACE,
      USB_DESCRIPTOR_SUBTYPE_MS_HEADER,
      USB_SHORT_GET_LOW(USB_MIDI_RELEASE),
      USB_SHORT_GET_HIGH(USB_MIDI_RELEASE),
      USB_SHORT_GET_LOW(USB_DESCRIPTOR_LENGTH_MS_INTERFACE +
                        (USB_DESCRIPTOR_LENGTH_MIDI_IN_JACK + USB_DESCRIPTOR_LENGTH_MIDI_IN_JACK +
                         USB_DESCRIPTOR_LENGTH_MIDI_OUT_JACK +
                         USB_DESCRIPTOR_LENGTH_MIDI_OUT_JACK) *
                          USB_MIDI_CABLES),
      USB_SHORT_GET_HIGH(USB_DESCRIPTOR_LENGTH_MS_INTERFACE +
                         (USB_DESCRIPTOR_LENGTH_MIDI_IN_JACK + USB_DESCRIPTOR_LENGTH_MIDI_IN_JACK +
                          USB_DESCRIPTOR_LENGTH_MIDI_OUT_JACK +
                          USB_DESCRIPTOR_LENGTH_MIDI_OUT_JACK) *
                           USB_MIDI_CABLES),
    },
	.jack0 = {
      /* MIDI IN Jack Descriptor */
      USB_DESCRIPTOR_LENGTH_MIDI_IN_JACK,
      USB_DESCRIPTOR_TYPE_CS_INTERFACE,
      USB_DESCRIPTOR_SUBTYPE_MIDI_IN_JACK,
      USB_DESCRIPTOR_JACK_TYPE_EMBEDDED,
      USB_MIDI_CABLES,
      0,
    },
	.jack1 = {
      /* MIDI IN Jack Descriptor */
      USB_DESCRIPTOR_LENGTH_MIDI_IN_JACK,
      USB_DESCRIPTOR_TYPE_CS_INTERFACE,
      USB_DESCRIPTOR_SUBTYPE_MIDI_IN_JACK,
      USB_DESCRIPTOR_JACK_TYPE_EXTERNAL,
      USB_MIDI_CABLES + 1,
      0,
    },
	.jack2 = {
      /* MIDI OUT Jack Descriptor */
      USB_DESCRIPTOR_LENGTH_MIDI_OUT_JACK,
      USB_DESCRIPTOR_TYPE_CS_INTERFACE,
      USB_DESCRIPTOR_SUBTYPE_MIDI_OUT_JACK,
      USB_DESCRIPTOR_JACK_TYPE_EMBEDDED,
      USB_MIDI_CABLES + 2,
      1,
      2,
      1,
      0,
    },
	.jack3 = {
      /* MIDI OUT Jack Descriptor */
      USB_DESCRIPTOR_LENGTH_MIDI_OUT_JACK,
      USB_DESCRIPTOR_TYPE_CS_INTERFACE,
      USB_DESCRIPTOR_SUBTYPE_MIDI_OUT_JACK,
      USB_DESCRIPTOR_JACK_TYPE_EXTERNAL,
      USB_MIDI_CABLES + 3,
      1,
      1,
      1,
      0,
    },
	.ep0 = {
      /*MIDI Out Endpoint */
      USB_DESCRIPTOR_LENGTH_MIDI_ENDPOINT,
      USB_DESCRIPTOR_TYPE_ENDPOINT,
      USB_MIDI_BULK_OUT_ENDPOINT | (USB_OUT << 7U),
      USB_ENDPOINT_BULK,
      USB_SHORT_GET_LOW(HS_MIDI_BULK_OUT_PACKET_SIZE),
      USB_SHORT_GET_HIGH(HS_MIDI_BULK_OUT_PACKET_SIZE),
      0x00,
      0x00,
      0x00,
    },
	.cs_ep0 = {
      4 + USB_MIDI_CABLES,
      USB_DESCRIPTOR_TYPE_CS_ENDPOINT,
      USB_DESCRIPTOR_SUBTYPE_MS_HEADER,
      USB_MIDI_CABLES,
      1,
    },
	.ep1 = {
      /*MIDI In Endpoint */
      USB_DESCRIPTOR_LENGTH_MIDI_ENDPOINT,
      USB_DESCRIPTOR_TYPE_ENDPOINT,
      USB_MIDI_BULK_IN_ENDPOINT | (USB_IN << 7U),
      USB_ENDPOINT_BULK,
      USB_SHORT_GET_LOW(HS_MIDI_BULK_IN_PACKET_SIZE),
      USB_SHORT_GET_HIGH(HS_MIDI_BULK_IN_PACKET_SIZE),
      0x00,
      0x00,
      0x00,
    },
	.cs_ep1 = {
      4 + USB_MIDI_CABLES,
      USB_DESCRIPTOR_TYPE_CS_ENDPOINT,
      USB_DESCRIPTOR_SUBTYPE_MS_HEADER,
      USB_MIDI_CABLES,
      3,
    }
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
usb_audio_in_out_interface_descriptor_t g_UsbAudioInterfaceDescriptor = {
    /* Interface Association Descriptor */
    /* Size of this descriptor in bytes */
    .iad = {
      USB_IAD_DESC_SIZE,
      /* INTERFACE_ASSOCIATION Descriptor Type  */
      USB_DESCRIPTOR_TYPE_INTERFACE_ASSOCIATION,
      /* The first interface number associated with this function */
      USB_AUDIO_CONTROL_INTERFACE_INDEX,
      /* The number of contiguous interfaces associated with this function */
      USB_AUDIO_INTERFACE_COUNT,
      /* The function belongs to the Communication Device/Interface Class  */
      USB_AUDIO_CLASS,
      0x00,
      /* The function uses the No class specific protocol required Protocol  */
      USB_AUDIO_PROTOCOL,
      /* The Function string descriptor index */
      0x05,
    },
    .ac_iface = {
      /* AUDIO */
      USB_DESCRIPTOR_LENGTH_INTERFACE, /* Size of this descriptor in bytes */
      USB_DESCRIPTOR_TYPE_INTERFACE,   /* INTERFACE Descriptor Type */

      USB_AUDIO_CONTROL_INTERFACE_INDEX,       /* Number of this interface. */
      USB_AUDIO_CONTROL_INTERFACE_ALTERNATE_0, /* Value used to select this alternate setting
                                            for the interface identified in the prior field */
      0x00U,                                   /* Number of endpoints used by this
                                                  interface (excluding endpoint zero). */
      USB_AUDIO_CLASS,           /*The interface implements the Audio Interface class  */
      USB_SUBCLASS_AUDIOCONTROL, /*The interface implements the AUDIOCONTROL Subclass  */
      USB_AUDIO_PROTOCOL,        /*The interface doesn't use any class-specific protocols  */
      0x00U, /* The device doesn't have a string descriptor describing this iInterface  */
    },
    .cs_ac_iface = {
      /* Audio Class Specific type of INTERFACE Descriptor */
      USB_AUDIO_CONTROL_INTERFACE_HEADER_LENGTH,   /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,      /* CS_INTERFACE Descriptor Type   */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_HEADER, /* HEADER descriptor subtype   */
      0x00U,
      0x02U, /* Audio Device compliant to the USB Audio specification version 2.00  */
      0x0AU, /* Pro audio */
      USB_AUDIO_CONTROL_INTERFACE_HEADER_LENGTH + USB_AUDIO_CLOCK_SOURCE_LENGTH +
        USB_AUDIO_INPUT_TERMINAL_ONLY_DESC_SIZE + USB_AUDIO_OUTPUT_TERMINAL_ONLY_DESC_SIZE +
        USB_AUDIO_INPUT_TERMINAL_ONLY_DESC_SIZE + USB_AUDIO_OUTPUT_TERMINAL_ONLY_DESC_SIZE,
      0x00U, /* Total number of bytes returned for the class-specific AudioControl interface
                descriptor. Includes the combined length of this descriptor header and all Unit and
                Terminal descriptors. */
      0x00U,
    },
	.clock_source = {
      USB_AUDIO_CLOCK_SOURCE_LENGTH,          /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE, /* CS_INTERFACE Descriptor Type  */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_CLOCK_SOURCE_UNIT, /* CLOCK_SOURCE
                                                                 descriptor
                                                                 subtype  */
      USB_AUDIO_CONTROL_CLOCK_SOURCE_ENTITY_ID,               /* Constant uniquely identifying
                                                               * the Clock Source Entity within
                                                               * the audio funcion
                                                               */
      0x01U,                                                  /* D1..0: 01: Internal Fixed Clock
                                                                 D2: 0 Clock is not synchronized to SOF
                                                                 D7..3: Reserved, should set to 0   */
      0x07U, /* D1..0: Clock Frequency Control is present and Host programmable
                D3..2: Clock Validity Control is present but read-only
                D7..4: Reserved, should set to 0 */
      0x00U, /* This Clock Source has no association   */
      0x00U, /* Index of a string descriptor, describing the Clock Source Entity */
             /* Audio Class Specific type of Input Terminal*/
    },
	.input_terminal0 = {
      USB_AUDIO_INPUT_TERMINAL_ONLY_DESC_SIZE, /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,  /* CS_INTERFACE Descriptor Type   */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_INPUT_TERMINAL,
      /* INPUT_TERMINAL descriptor subtype  */
      USB_AUDIO_RECORDER_CONTROL_INPUT_TERMINAL_ID, /* Constant uniquely identifying the Terminal
                   within the audio function. This value is used in all 	requests to address this
                   Terminal.  */
      0x13U,
      0x07,  /* Synthesizer  */
      0x00U, /* This Input Terminal has no association  */
      USB_AUDIO_CONTROL_CLOCK_SOURCE_ENTITY_ID,
      AUDIO_IN_FORMAT_CHANNELS, /* 4 channels  */
      0x00U,
      0x00U,
      0x00U,
      0x00U,
      0x00U, /* Index of a string descriptor, describing the name of the first logical channel.   */
      0x00,
      0x00,
      0x00U, /* Index of a string descriptor, describing the Input Terminal.   */
    },
	.output_terminal0 = {
      /* Audio Class Specific type of  Output Terminal */
      USB_AUDIO_OUTPUT_TERMINAL_ONLY_DESC_SIZE, /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,   /* CS_INTERFACE Descriptor Type   */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_OUTPUT_TERMINAL,
      /* OUTPUT_TERMINAL descriptor subtype  */
      USB_AUDIO_RECORDER_CONTROL_OUTPUT_TERMINAL_ID, /* Constant uniquely identifying the Terminal
                                                        within the audio function*/
      0x01U,
      0x01U, /* A Terminal dealing with a signal carried over an endpoint in an AudioStreaming
                interface */
      0x00U, /*  This Output Terminal has no association   */
      USB_AUDIO_RECORDER_CONTROL_INPUT_TERMINAL_ID, /* ID of the Unit or Terminal to which this
                                                     Terminal is connected.   */
      USB_AUDIO_CONTROL_CLOCK_SOURCE_ENTITY_ID,
      0x00U,
      0x00U,
      0x00U,
    },
	.input_terminal1 = {
      USB_AUDIO_INPUT_TERMINAL_ONLY_DESC_SIZE, /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,  /* CS_INTERFACE Descriptor Type   */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_INPUT_TERMINAL,
      /* INPUT_TERMINAL descriptor subtype  */
      USB_AUDIO_PLAYER_CONTROL_INPUT_TERMINAL_ID, /* Constant uniquely identifying the Terminal
                   within the audio function. This value is used in all 	requests to address this
                   Terminal.  */
      0x01U,
      0x01,  /* USB  */
      0x00U, /* This Input Terminal has no association  */
      USB_AUDIO_CONTROL_CLOCK_SOURCE_ENTITY_ID,
      AUDIO_OUT_FORMAT_CHANNELS, /* 2 channels  */
      0x00U,
      0x00U,
      0x00U,
      0x00U,
      0x00U, /* Index of a string descriptor, describing the name of the first logical channel.   */
      0x00,
      0x00,
      0x00U, /* Index of a string descriptor, describing the Input Terminal.   */
    },
	.output_terminal1 = {
      /* Audio Class Specific type of  Output Terminal */
      USB_AUDIO_OUTPUT_TERMINAL_ONLY_DESC_SIZE, /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,   /* CS_INTERFACE Descriptor Type   */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_OUTPUT_TERMINAL,
      /* OUTPUT_TERMINAL descriptor subtype  */
      USB_AUDIO_PLAYER_CONTROL_OUTPUT_TERMINAL_ID, /* Constant uniquely identifying the Terminal
                                                        within the audio function*/
      0x13U,
      0x07U, /* A Terminal dealing with a signal carried over an endpoint in an AudioStreaming
                interface */
      0x00U, /*  This Output Terminal has no association   */
      USB_AUDIO_PLAYER_CONTROL_INPUT_TERMINAL_ID, /* ID of the Unit or Terminal to which this
                                                     Terminal is connected.   */
      USB_AUDIO_CONTROL_CLOCK_SOURCE_ENTITY_ID,
      0x00U,
      0x00U,
      0x00U,
    },
    .as_iface0_0 = {
      /* Audio Class Specific INTERFACE Descriptor, alternative interface 0  */
      USB_DESCRIPTOR_LENGTH_INTERFACE,                 /* Descriptor size is 9 bytes  */
      USB_DESCRIPTOR_TYPE_INTERFACE,                   /* INTERFACE Descriptor Type   */
      USB_AUDIO_RECORDER_STREAM_INTERFACE_INDEX,       /* The number of this interface is 1.  */
      USB_AUDIO_RECORDER_STREAM_INTERFACE_ALTERNATE_0, /* The value used to select the alternate
                                                          setting for this interface is 0   */
      0x00U, /* The number of endpoints used by this interface is 0 (excluding endpoint zero)   */
      USB_AUDIO_CLASS,          /* The interface implements the Audio Interface class   */
      USB_SUBCLASS_AUDIOSTREAM, /* The interface implements the AUDIOSTREAMING Subclass   */
      USB_AUDIO_PROTOCOL,       /* The interface doesn't use any class-specific protocols   */
      0x00U, /* The device doesn't have a string descriptor describing this iInterface  */
    },
    .as_iface0_1 = {
      /* Audio Class Specific INTERFACE Descriptor, alternative interface 1 */
      USB_DESCRIPTOR_LENGTH_INTERFACE,                 /* Descriptor size is 9 bytes  */
      USB_DESCRIPTOR_TYPE_INTERFACE,                   /* INTERFACE Descriptor Type  */
      USB_AUDIO_RECORDER_STREAM_INTERFACE_INDEX,       /*The number of this interface is 1.  */
      USB_AUDIO_RECORDER_STREAM_INTERFACE_ALTERNATE_1, /* The value used to select the alternate
                                                          setting for this interface is 1  */
      0x01U, /* The number of endpoints used by this interface is 1 (excluding endpoint zero)    */
      USB_AUDIO_CLASS,          /* The interface implements the Audio Interface class   */
      USB_SUBCLASS_AUDIOSTREAM, /* The interface implements the AUDIOSTREAMING Subclass   */
      USB_AUDIO_PROTOCOL,       /* The interface doesn't use any class-specific protocols  */
      0x00U, /* The device doesn't have a string descriptor describing this iInterface  */
    },
    .cs_as_iface0 = {
      /* Audio Class Specific CS INTERFACE Descriptor*/
      USB_AUDIO_STREAMING_IFACE_DESC_SIZE,               /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,            /* CS_INTERFACE Descriptor Type  */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_STREAMING_AS_GENERAL, /* AS_GENERAL descriptor subtype  */
      USB_AUDIO_RECORDER_CONTROL_OUTPUT_TERMINAL_ID, /* The Terminal ID of the Terminal to which the
                                                        endpoint of this interface is connected. */
      0x00U, /* Delay introduced by the data path. Expressed in number of frames.  */
      AUDIO_IN_FORMAT,
      0x01,
      0x00,
      0x00,
      0x00,
      AUDIO_IN_FORMAT_CHANNELS,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
    },
	.format0 = {
      /* Audio Class Specific type I format INTERFACE Descriptor */
      USB_AUDIO_STREAMING_TYPE_I_DESC_SIZE,   /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE, /* CS_INTERFACE Descriptor Type   */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_STREAMING_FORMAT_TYPE,
      /* FORMAT_TYPE descriptor subtype  */
      USB_AUDIO_FORMAT_TYPE_I, /* FORMAT_TYPE_I  */
      AUDIO_IN_FORMAT_SIZE,    /* The number of bytes occupied by one audio subframe. Can be 1, 2, 3
                                  or 4.   */
      AUDIO_IN_FORMAT_BITS,    /* The number of effectively used bits from the available bits in an
                                  audio    subframe.*/
    },
	.as_ep0 = {
      /* ENDPOINT Descriptor */
      USB_ENDPOINT_AUDIO_DESCRIPTOR_LENGTH, /* Descriptor size is 9 bytes  */
      USB_DESCRIPTOR_TYPE_ENDPOINT,         /* ENDPOINT Descriptor Type   */
      USB_AUDIO_RECORDER_STREAM_ENDPOINT |
        (USB_IN << 7), /* This is an IN endpoint with endpoint number 2   */
      USB_ENDPOINT_ISOCHRONOUS | 0x01 << 2 | 0x02 << 4, /*   Types - Transfer: ISOCHRONOUS
                                                              Sync: Async
                                                              Implicit feedback */
      USB_SHORT_GET_LOW(HS_ISO_IN_ENDP_PACKET_SIZE),
      USB_SHORT_GET_HIGH(
        HS_ISO_IN_ENDP_PACKET_SIZE), /* Maximum packet size for this endpoint is 8 Bytes.  */
      HS_ISO_IN_ENDP_INTERVAL, /* The polling interval value is every 1 Frames. If Hi-Speed, every
                                      1 uFrames   */
    },
	.cs_as_ep0 = {
      /* Audio Class Specific ENDPOINT Descriptor  */
      USB_AUDIO_STREAMING_ENDP_DESC_SIZE,      /*  Size of the descriptor, in bytes  */
      USB_AUDIO_STREAM_ENDPOINT_DESCRIPTOR,    /* CS_ENDPOINT Descriptor Type  */
      USB_AUDIO_EP_GENERAL_DESCRIPTOR_SUBTYPE, /* AUDIO_EP_GENERAL descriptor subtype  */
      0x00U,                                   /* Bit 0: Sampling Frequency 0
                                                  Bit 1: Pitch 0
                                                  Bit 7: MaxPacketsOnly 0   */
      0x00U, /* Indicates the units used for the wLockDelay field: 0: Undefined  */
      0x00U,
      0x00U, /* Indicates the time it takes this endpoint to reliably lock its internal clock
              recovery circuitry */
      0x00U,
    },
    .as_iface1_0 = {
      /* Audio Class Specific INTERFACE Descriptor, alternative interface 0  */
      USB_DESCRIPTOR_LENGTH_INTERFACE,               /* Descriptor size is 9 bytes  */
      USB_DESCRIPTOR_TYPE_INTERFACE,                 /* INTERFACE Descriptor Type   */
      USB_AUDIO_PLAYER_STREAM_INTERFACE_INDEX,       /* The number of this interface is 1.  */
      USB_AUDIO_PLAYER_STREAM_INTERFACE_ALTERNATE_0, /* The value used to select the alternate
                                                          setting for this interface is 0   */
      0x00U, /* The number of endpoints used by this interface is 0 (excluding endpoint zero)   */
      USB_AUDIO_CLASS,          /* The interface implements the Audio Interface class   */
      USB_SUBCLASS_AUDIOSTREAM, /* The interface implements the AUDIOSTREAMING Subclass   */
      USB_AUDIO_PROTOCOL,       /* The interface doesn't use any class-specific protocols   */
      0x00U, /* The device doesn't have a string descriptor describing this iInterface  */
    },
    .as_iface1_1 = {
      /* Audio Class Specific INTERFACE Descriptor, alternative interface 1 */
      USB_DESCRIPTOR_LENGTH_INTERFACE,               /* Descriptor size is 9 bytes  */
      USB_DESCRIPTOR_TYPE_INTERFACE,                 /* INTERFACE Descriptor Type  */
      USB_AUDIO_PLAYER_STREAM_INTERFACE_INDEX,       /*The number of this interface is 1.  */
      USB_AUDIO_PLAYER_STREAM_INTERFACE_ALTERNATE_1, /* The value used to select the alternate
                                                          setting for this interface is 1  */
      USB_AUDIO_PLAYER_STREAM_ENDPOINT_COUNT, /* The number of endpoints used by this interface is 1
                                                 (excluding endpoint zero)    */
      USB_AUDIO_CLASS,          /* The interface implements the Audio Interface class   */
      USB_SUBCLASS_AUDIOSTREAM, /* The interface implements the AUDIOSTREAMING Subclass   */
      USB_AUDIO_PROTOCOL,       /* The interface doesn't use any class-specific protocols  */
      0x00U, /* The device doesn't have a string descriptor describing this iInterface  */
    },
    .cs_as_iface1 = {
      /* Audio Class Specific CS INTERFACE Descriptor*/
      USB_AUDIO_STREAMING_IFACE_DESC_SIZE,               /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,            /* CS_INTERFACE Descriptor Type  */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_STREAMING_AS_GENERAL, /* AS_GENERAL descriptor subtype  */
      USB_AUDIO_PLAYER_CONTROL_INPUT_TERMINAL_ID, /* The Terminal ID of the Terminal to which the
                                                        endpoint of this interface is connected. */
      0x00U, /* Delay introduced by the data path. Expressed in number of frames.  */
      AUDIO_OUT_FORMAT,
      0x01,
      0x00,
      0x00,
      0x00,
      AUDIO_OUT_FORMAT_CHANNELS,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
    },
	.format1 = {
      /* Audio Class Specific type I format INTERFACE Descriptor */
      USB_AUDIO_STREAMING_TYPE_I_DESC_SIZE,   /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE, /* CS_INTERFACE Descriptor Type   */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_STREAMING_FORMAT_TYPE,
      /* FORMAT_TYPE descriptor subtype  */
      USB_AUDIO_FORMAT_TYPE_I, /* FORMAT_TYPE_I  */
      AUDIO_OUT_FORMAT_SIZE,   /* The number of bytes occupied by one audio subframe. Can be 1, 2, 3
                                 or 4.   */
      AUDIO_OUT_FORMAT_BITS,   /* The number of effectively used bits from the available bits in an
                                 audio   subframe.*/
    },
	.as_ep1 = {
      /* ENDPOINT Descriptor */
      USB_ENDPOINT_AUDIO_DESCRIPTOR_LENGTH, /* Descriptor size is 9 bytes  */
      USB_DESCRIPTOR_TYPE_ENDPOINT,         /* ENDPOINT Descriptor Type   */
      USB_AUDIO_PLAYER_STREAM_ENDPOINT |
        (USB_OUT << 7),                     /* This is an IN endpoint with endpoint number 2   */
      USB_ENDPOINT_ISOCHRONOUS | 0x01 << 2, /*   Types - Transfer: ISOCHRONOUS
                                                                  Sync: Async */
      USB_SHORT_GET_LOW(HS_ISO_OUT_ENDP_PACKET_SIZE),
      USB_SHORT_GET_HIGH(
        HS_ISO_OUT_ENDP_PACKET_SIZE), /* Maximum packet size for this endpoint is 8 Bytes.  */
      HS_ISO_OUT_ENDP_INTERVAL, /* The polling interval value is every 1 Frames. If Hi-Speed, every
                                      1 uFrames   */
    },
	.cs_as_ep1 = {
      /* Audio Class Specific ENDPOINT Descriptor  */
      USB_AUDIO_STREAMING_ENDP_DESC_SIZE,      /*  Size of the descriptor, in bytes  */
      USB_AUDIO_STREAM_ENDPOINT_DESCRIPTOR,    /* CS_ENDPOINT Descriptor Type  */
      USB_AUDIO_EP_GENERAL_DESCRIPTOR_SUBTYPE, /* AUDIO_EP_GENERAL descriptor subtype  */
      0x00U,                                   /* Bit 0: Sampling Frequency 0
                                                  Bit 1: Pitch 0
                                                  Bit 7: MaxPacketsOnly 0   */
      0x00U, /* Indicates the units used for the wLockDelay field: 0: Undefined  */
      0x00U,
      0x00U, /* Indicates the time it takes this endpoint to reliably lock its internal clock
              recovery circuitry */
      0x00U,
    },
	.as_fdb_ep = {
      /* Endpoint 3 Feedback ENDPOINT */
      USB_AUDIO_STANDARD_AS_ISO_FEEDBACK_ENDPOINT_LENGTH, /* Descriptor size is 7
                                                             bytes */
      USB_DESCRIPTOR_TYPE_ENDPOINT,                       /* bDescriptorType */
      USB_AUDIO_PLAYER_FEEDBACK_ENDPOINT |
        (USB_IN << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT), /* This is an IN
                                                                        endpoint with
                                                                        endpoint number
                                                                        3 */
      USB_ENDPOINT_ISOCHRONOUS | 0x01 << 4,                          /*  Types -
                                                                    Transfer: ISOCHRONOUS
                                                                    Sync: Async
                                                                    Usage: Feedback EP   */
      0x04,
      0x00,                          /* wMaxPacketSize */
      FS_ISO_FEEDBACK_ENDP_INTERVAL, /* interval polling(2^x ms) */
    }
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
usb_audio_in_interface_descriptor_t g_UsbAudioInInterfaceDescriptor = {
    /* Interface Association Descriptor */
    /* Size of this descriptor in bytes */
    .iad = {
      USB_IAD_DESC_SIZE,
      /* INTERFACE_ASSOCIATION Descriptor Type  */
      USB_DESCRIPTOR_TYPE_INTERFACE_ASSOCIATION,
      /* The first interface number associated with this function */
      USB_AUDIO_CONTROL_INTERFACE_INDEX,
      /* The number of contiguous interfaces associated with this function */
      USB_AUDIO_INTERFACE_COUNT - 1,
      /* The function belongs to the Communication Device/Interface Class  */
      USB_AUDIO_CLASS,
      0x00,
      /* The function uses the No class specific protocol required Protocol  */
      USB_AUDIO_PROTOCOL,
      /* The Function string descriptor index */
      0x05,
    },
    .ac_iface = {
      /* AUDIO */
      USB_DESCRIPTOR_LENGTH_INTERFACE, /* Size of this descriptor in bytes */
      USB_DESCRIPTOR_TYPE_INTERFACE,   /* INTERFACE Descriptor Type */

      USB_AUDIO_CONTROL_INTERFACE_INDEX,       /* Number of this interface. */
      USB_AUDIO_CONTROL_INTERFACE_ALTERNATE_0, /* Value used to select this alternate setting
                                            for the interface identified in the prior field */
      0x00U,                                   /* Number of endpoints used by this
                                                  interface (excluding endpoint zero). */
      USB_AUDIO_CLASS,           /*The interface implements the Audio Interface class  */
      USB_SUBCLASS_AUDIOCONTROL, /*The interface implements the AUDIOCONTROL Subclass  */
      USB_AUDIO_PROTOCOL,        /*The interface doesn't use any class-specific protocols  */
      0x00U, /* The device doesn't have a string descriptor describing this iInterface  */
    },
    .cs_ac_iface = {
      /* Audio Class Specific type of INTERFACE Descriptor */
      USB_AUDIO_CONTROL_INTERFACE_HEADER_LENGTH,   /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,      /* CS_INTERFACE Descriptor Type   */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_HEADER, /* HEADER descriptor subtype   */
      0x00U,
      0x02U, /* Audio Device compliant to the USB Audio specification version 2.00  */
      0x0AU, /* Pro audio */
      USB_AUDIO_CONTROL_INTERFACE_HEADER_LENGTH + USB_AUDIO_CLOCK_SOURCE_LENGTH +
        USB_AUDIO_INPUT_TERMINAL_ONLY_DESC_SIZE + USB_AUDIO_OUTPUT_TERMINAL_ONLY_DESC_SIZE,
      0x00U, /* Total number of bytes returned for the class-specific AudioControl interface
                descriptor. Includes the combined length of this descriptor header and all Unit and
                Terminal descriptors. */
      0x00U,
    },
	.clock_source = {
      USB_AUDIO_CLOCK_SOURCE_LENGTH,          /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE, /* CS_INTERFACE Descriptor Type  */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_CLOCK_SOURCE_UNIT, /* CLOCK_SOURCE
                                                                 descriptor
                                                                 subtype  */
      USB_AUDIO_CONTROL_CLOCK_SOURCE_ENTITY_ID,               /* Constant uniquely identifying
                                                               * the Clock Source Entity within
                                                               * the audio funcion
                                                               */
      0x01U,                                                  /* D1..0: 01: Internal Fixed Clock
                                                                 D2: 0 Clock is not synchronized to SOF
                                                                 D7..3: Reserved, should set to 0   */
      0x07U, /* D1..0: Clock Frequency Control is present and Host programmable
                D3..2: Clock Validity Control is present but read-only
                D7..4: Reserved, should set to 0 */
      0x00U, /* This Clock Source has no association   */
      0x00U, /* Index of a string descriptor, describing the Clock Source Entity */
             /* Audio Class Specific type of Input Terminal*/
    },
	.input_terminal = {
      USB_AUDIO_INPUT_TERMINAL_ONLY_DESC_SIZE, /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,  /* CS_INTERFACE Descriptor Type   */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_INPUT_TERMINAL,
      /* INPUT_TERMINAL descriptor subtype  */
      USB_AUDIO_RECORDER_CONTROL_INPUT_TERMINAL_ID, /* Constant uniquely identifying the Terminal
                   within the audio function. This value is used in all 	requests to address this
                   Terminal.  */
      0x13U,
      0x07,  /* Synthesizer  */
      0x00U, /* This Input Terminal has no association  */
      USB_AUDIO_CONTROL_CLOCK_SOURCE_ENTITY_ID,
      AUDIO_IN_FORMAT_CHANNELS, /* 4 channels  */
      0x00U,
      0x00U,
      0x00U,
      0x00U,
      0x00U, /* Index of a string descriptor, describing the name of the first logical channel.   */
      0x00,
      0x00,
      0x00U, /* Index of a string descriptor, describing the Input Terminal.   */
    },
	.output_terminal = {
      /* Audio Class Specific type of  Output Terminal */
      USB_AUDIO_OUTPUT_TERMINAL_ONLY_DESC_SIZE, /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,   /* CS_INTERFACE Descriptor Type   */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_OUTPUT_TERMINAL,
      /* OUTPUT_TERMINAL descriptor subtype  */
      USB_AUDIO_RECORDER_CONTROL_OUTPUT_TERMINAL_ID, /* Constant uniquely identifying the Terminal
                                                        within the audio function*/
      0x01U,
      0x01U, /* A Terminal dealing with a signal carried over an endpoint in an AudioStreaming
                interface */
      0x00U, /*  This Output Terminal has no association   */
      USB_AUDIO_RECORDER_CONTROL_INPUT_TERMINAL_ID, /* ID of the Unit or Terminal to which this
                                                     Terminal is connected.   */
      USB_AUDIO_CONTROL_CLOCK_SOURCE_ENTITY_ID,
      0x00U,
      0x00U,
      0x00U,
    },
    .as_iface_0 = {
      /* Audio Class Specific INTERFACE Descriptor, alternative interface 0  */
      USB_DESCRIPTOR_LENGTH_INTERFACE,                 /* Descriptor size is 9 bytes  */
      USB_DESCRIPTOR_TYPE_INTERFACE,                   /* INTERFACE Descriptor Type   */
      USB_AUDIO_CONTROL_INTERFACE_INDEX + 1,           /* The number of this interface is 2.  */
      USB_AUDIO_RECORDER_STREAM_INTERFACE_ALTERNATE_0, /* The value used to select the alternate
                                                          setting for this interface is 0   */
      0x00U, /* The number of endpoints used by this interface is 0 (excluding endpoint zero)   */
      USB_AUDIO_CLASS,          /* The interface implements the Audio Interface class   */
      USB_SUBCLASS_AUDIOSTREAM, /* The interface implements the AUDIOSTREAMING Subclass   */
      USB_AUDIO_PROTOCOL,       /* The interface doesn't use any class-specific protocols   */
      0x00U, /* The device doesn't have a string descriptor describing this iInterface  */
    },
    .as_iface_1 = {
      /* Audio Class Specific INTERFACE Descriptor, alternative interface 1 */
      USB_DESCRIPTOR_LENGTH_INTERFACE,                 /* Descriptor size is 9 bytes  */
      USB_DESCRIPTOR_TYPE_INTERFACE,                   /* INTERFACE Descriptor Type  */
      USB_AUDIO_CONTROL_INTERFACE_INDEX + 1,           /*The number of this interface is 2.  */
      USB_AUDIO_RECORDER_STREAM_INTERFACE_ALTERNATE_1, /* The value used to select the alternate
                                                          setting for this interface is 1  */
      0x01U, /* The number of endpoints used by this interface is 1 (excluding endpoint zero)    */
      USB_AUDIO_CLASS,          /* The interface implements the Audio Interface class   */
      USB_SUBCLASS_AUDIOSTREAM, /* The interface implements the AUDIOSTREAMING Subclass   */
      USB_AUDIO_PROTOCOL,       /* The interface doesn't use any class-specific protocols  */
      0x00U, /* The device doesn't have a string descriptor describing this iInterface  */
    },
    .cs_as_iface = {
      /* Audio Class Specific CS INTERFACE Descriptor*/
      USB_AUDIO_STREAMING_IFACE_DESC_SIZE,               /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,            /* CS_INTERFACE Descriptor Type  */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_STREAMING_AS_GENERAL, /* AS_GENERAL descriptor subtype  */
      USB_AUDIO_RECORDER_CONTROL_OUTPUT_TERMINAL_ID, /* The Terminal ID of the Terminal to which the
                                                        endpoint of this interface is connected. */
      0x00U, /* Delay introduced by the data path. Expressed in number of frames.  */
      AUDIO_IN_FORMAT,
      0x01,
      0x00,
      0x00,
      0x00,
      AUDIO_IN_FORMAT_CHANNELS,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
    },
	.format = {
      /* Audio Class Specific type I format INTERFACE Descriptor */
      USB_AUDIO_STREAMING_TYPE_I_DESC_SIZE,   /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE, /* CS_INTERFACE Descriptor Type   */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_STREAMING_FORMAT_TYPE,
      /* FORMAT_TYPE descriptor subtype  */
      USB_AUDIO_FORMAT_TYPE_I, /* FORMAT_TYPE_I  */
      AUDIO_IN_FORMAT_SIZE,    /* The number of bytes occupied by one audio subframe. Can be 1, 2, 3
                                  or 4.   */
      AUDIO_IN_FORMAT_BITS,    /* The number of effectively used bits from the available bits in an
                                  audio    subframe.*/
    },
	.as_ep = {
      /* ENDPOINT Descriptor */
      USB_ENDPOINT_AUDIO_DESCRIPTOR_LENGTH, /* Descriptor size is 9 bytes  */
      USB_DESCRIPTOR_TYPE_ENDPOINT,         /* ENDPOINT Descriptor Type   */
      USB_AUDIO_RECORDER_STREAM_ENDPOINT |
        (USB_IN << 7), /* This is an IN endpoint with endpoint number 2   */
      USB_ENDPOINT_ISOCHRONOUS | 0x01 << 2 | 0x02 << 4, /*   Types - Transfer: ISOCHRONOUS
                                                              Sync: Async
                                                              Implicit feedback */
      USB_SHORT_GET_LOW(HS_ISO_IN_ENDP_PACKET_SIZE),
      USB_SHORT_GET_HIGH(
        HS_ISO_IN_ENDP_PACKET_SIZE), /* Maximum packet size for this endpoint is 8 Bytes.  */
      HS_ISO_IN_ENDP_INTERVAL, /* The polling interval value is every 1 Frames. If Hi-Speed, every
                                      1 uFrames   */
    },
	.cs_as_ep = {
      /* Audio Class Specific ENDPOINT Descriptor  */
      USB_AUDIO_STREAMING_ENDP_DESC_SIZE,      /*  Size of the descriptor, in bytes  */
      USB_AUDIO_STREAM_ENDPOINT_DESCRIPTOR,    /* CS_ENDPOINT Descriptor Type  */
      USB_AUDIO_EP_GENERAL_DESCRIPTOR_SUBTYPE, /* AUDIO_EP_GENERAL descriptor subtype  */
      0x00U,                                   /* Bit 0: Sampling Frequency 0
                                                  Bit 1: Pitch 0
                                                  Bit 7: MaxPacketsOnly 0   */
      0x00U, /* Indicates the units used for the wLockDelay field: 0: Undefined  */
      0x00U,
      0x00U, /* Indicates the time it takes this endpoint to reliably lock its internal clock
              recovery circuitry */
      0x00U,
    }
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
usb_audio_out_interface_descriptor_t g_UsbAudioOutInterfaceDescriptor = {
    /* Interface Association Descriptor */
    /* Size of this descriptor in bytes */
    .iad = {
      USB_IAD_DESC_SIZE,
      /* INTERFACE_ASSOCIATION Descriptor Type  */
      USB_DESCRIPTOR_TYPE_INTERFACE_ASSOCIATION,
      /* The first interface number associated with this function */
      USB_AUDIO_CONTROL_INTERFACE_INDEX,
      /* The number of contiguous interfaces associated with this function */
      USB_AUDIO_INTERFACE_COUNT - 1,
      /* The function belongs to the Communication Device/Interface Class  */
      USB_AUDIO_CLASS,
      0x00,
      /* The function uses the No class specific protocol required Protocol  */
      USB_AUDIO_PROTOCOL,
      /* The Function string descriptor index */
      0x05,
    },
    .ac_iface = {
      /* AUDIO */
      USB_DESCRIPTOR_LENGTH_INTERFACE, /* Size of this descriptor in bytes */
      USB_DESCRIPTOR_TYPE_INTERFACE,   /* INTERFACE Descriptor Type */

      USB_AUDIO_CONTROL_INTERFACE_INDEX,       /* Number of this interface. */
      USB_AUDIO_CONTROL_INTERFACE_ALTERNATE_0, /* Value used to select this alternate setting
                                            for the interface identified in the prior field */
      0x00U,                                   /* Number of endpoints used by this
                                                  interface (excluding endpoint zero). */
      USB_AUDIO_CLASS,           /*The interface implements the Audio Interface class  */
      USB_SUBCLASS_AUDIOCONTROL, /*The interface implements the AUDIOCONTROL Subclass  */
      USB_AUDIO_PROTOCOL,        /*The interface doesn't use any class-specific protocols  */
      0x00U, /* The device doesn't have a string descriptor describing this iInterface  */
    },
    .cs_ac_iface = {
      /* Audio Class Specific type of INTERFACE Descriptor */
      USB_AUDIO_CONTROL_INTERFACE_HEADER_LENGTH,   /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,      /* CS_INTERFACE Descriptor Type   */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_HEADER, /* HEADER descriptor subtype   */
      0x00U,
      0x02U, /* Audio Device compliant to the USB Audio specification version 2.00  */
      0x0AU, /* Pro audio */
      USB_AUDIO_CONTROL_INTERFACE_HEADER_LENGTH + USB_AUDIO_CLOCK_SOURCE_LENGTH +
        USB_AUDIO_INPUT_TERMINAL_ONLY_DESC_SIZE + USB_AUDIO_OUTPUT_TERMINAL_ONLY_DESC_SIZE,
      0x00U, /* Total number of bytes returned for the class-specific AudioControl interface
                descriptor. Includes the combined length of this descriptor header and all Unit and
                Terminal descriptors. */
      0x00U,
    },
	.clock_source = {
      USB_AUDIO_CLOCK_SOURCE_LENGTH,          /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE, /* CS_INTERFACE Descriptor Type  */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_CLOCK_SOURCE_UNIT, /* CLOCK_SOURCE
                                                                 descriptor
                                                                 subtype  */
      USB_AUDIO_CONTROL_CLOCK_SOURCE_ENTITY_ID,               /* Constant uniquely identifying
                                                               * the Clock Source Entity within
                                                               * the audio funcion
                                                               */
      0x01U,                                                  /* D1..0: 01: Internal Fixed Clock
                                                                 D2: 0 Clock is not synchronized to SOF
                                                                 D7..3: Reserved, should set to 0   */
      0x07U, /* D1..0: Clock Frequency Control is present and Host programmable
                D3..2: Clock Validity Control is present but read-only
                D7..4: Reserved, should set to 0 */
      0x00U, /* This Clock Source has no association   */
      0x00U, /* Index of a string descriptor, describing the Clock Source Entity */
    },
	.input_terminal = {
      USB_AUDIO_INPUT_TERMINAL_ONLY_DESC_SIZE, /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,  /* CS_INTERFACE Descriptor Type   */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_INPUT_TERMINAL,
      /* INPUT_TERMINAL descriptor subtype  */
      USB_AUDIO_PLAYER_CONTROL_INPUT_TERMINAL_ID, /* Constant uniquely identifying the Terminal
                   within the audio function. This value is used in all 	requests to address this
                   Terminal.  */
      0x01U,
      0x01,  /* USB  */
      0x00U, /* This Input Terminal has no association  */
      USB_AUDIO_CONTROL_CLOCK_SOURCE_ENTITY_ID,
      AUDIO_OUT_FORMAT_CHANNELS, /* 2 channels  */
      0x00U,
      0x00U,
      0x00U,
      0x00U,
      0x00U, /* Index of a string descriptor, describing the name of the first logical channel.   */
      0x00,
      0x00,
      0x00U, /* Index of a string descriptor, describing the Input Terminal.   */
    },
	.output_terminal = {
      /* Audio Class Specific type of  Output Terminal */
      USB_AUDIO_OUTPUT_TERMINAL_ONLY_DESC_SIZE, /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,   /* CS_INTERFACE Descriptor Type   */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_CONTROL_OUTPUT_TERMINAL,
      /* OUTPUT_TERMINAL descriptor subtype  */
      USB_AUDIO_PLAYER_CONTROL_OUTPUT_TERMINAL_ID, /* Constant uniquely identifying the Terminal
                                                        within the audio function*/
      0x13U,
      0x07U, /* A Terminal dealing with a signal carried over an endpoint in an AudioStreaming
                interface */
      0x00U, /*  This Output Terminal has no association   */
      USB_AUDIO_PLAYER_CONTROL_INPUT_TERMINAL_ID, /* ID of the Unit or Terminal to which this
                                                     Terminal is connected.   */
      USB_AUDIO_CONTROL_CLOCK_SOURCE_ENTITY_ID,
      0x00U,
      0x00U,
      0x00U,
    },
    .as_iface_0 = {
      /* Audio Class Specific INTERFACE Descriptor, alternative interface 0  */
      USB_DESCRIPTOR_LENGTH_INTERFACE,               /* Descriptor size is 9 bytes  */
      USB_DESCRIPTOR_TYPE_INTERFACE,                 /* INTERFACE Descriptor Type   */
      USB_AUDIO_CONTROL_INTERFACE_INDEX + 1,         /* The number of this interface is 2.  */
      USB_AUDIO_PLAYER_STREAM_INTERFACE_ALTERNATE_0, /* The value used to select the alternate
                                                          setting for this interface is 0   */
      0x00U, /* The number of endpoints used by this interface is 0 (excluding endpoint zero)   */
      USB_AUDIO_CLASS,          /* The interface implements the Audio Interface class   */
      USB_SUBCLASS_AUDIOSTREAM, /* The interface implements the AUDIOSTREAMING Subclass   */
      USB_AUDIO_PROTOCOL,       /* The interface doesn't use any class-specific protocols   */
      0x00U, /* The device doesn't have a string descriptor describing this iInterface  */
    },
    .as_iface_1 = {
      /* Audio Class Specific INTERFACE Descriptor, alternative interface 1 */
      USB_DESCRIPTOR_LENGTH_INTERFACE,               /* Descriptor size is 9 bytes  */
      USB_DESCRIPTOR_TYPE_INTERFACE,                 /* INTERFACE Descriptor Type  */
      USB_AUDIO_CONTROL_INTERFACE_INDEX + 1,         /*The number of this interface is 2.  */
      USB_AUDIO_PLAYER_STREAM_INTERFACE_ALTERNATE_1, /* The value used to select the alternate
                                                          setting for this interface is 1  */
      USB_AUDIO_PLAYER_STREAM_ENDPOINT_COUNT, /* The number of endpoints used by this interface is 1
                                                 (excluding endpoint zero)    */
      USB_AUDIO_CLASS,          /* The interface implements the Audio Interface class   */
      USB_SUBCLASS_AUDIOSTREAM, /* The interface implements the AUDIOSTREAMING Subclass   */
      USB_AUDIO_PROTOCOL,       /* The interface doesn't use any class-specific protocols  */
      0x00U, /* The device doesn't have a string descriptor describing this iInterface  */
    },
    .cs_as_iface = {
      /* Audio Class Specific CS INTERFACE Descriptor*/
      USB_AUDIO_STREAMING_IFACE_DESC_SIZE,               /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE,            /* CS_INTERFACE Descriptor Type  */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_STREAMING_AS_GENERAL, /* AS_GENERAL descriptor subtype  */
      USB_AUDIO_PLAYER_CONTROL_INPUT_TERMINAL_ID, /* The Terminal ID of the Terminal to which the
                                                        endpoint of this interface is connected. */
      0x00U, /* Delay introduced by the data path. Expressed in number of frames.  */
      AUDIO_OUT_FORMAT,
      0x01,
      0x00,
      0x00,
      0x00,
      AUDIO_OUT_FORMAT_CHANNELS,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
    },
	.format = {
      /* Audio Class Specific type I format INTERFACE Descriptor */
      USB_AUDIO_STREAMING_TYPE_I_DESC_SIZE,   /* Size of the descriptor, in bytes  */
      USB_DESCRIPTOR_TYPE_AUDIO_CS_INTERFACE, /* CS_INTERFACE Descriptor Type   */
      USB_DESCRIPTOR_SUBTYPE_AUDIO_STREAMING_FORMAT_TYPE,
      /* FORMAT_TYPE descriptor subtype  */
      USB_AUDIO_FORMAT_TYPE_I, /* FORMAT_TYPE_I  */
      AUDIO_OUT_FORMAT_SIZE,   /* The number of bytes occupied by one audio subframe. Can be 1, 2, 3
                                 or 4.   */
      AUDIO_OUT_FORMAT_BITS,   /* The number of effectively used bits from the available bits in an
                                 audio   subframe.*/
    },
	.as_ep = {
      /* ENDPOINT Descriptor */
      USB_ENDPOINT_AUDIO_DESCRIPTOR_LENGTH, /* Descriptor size is 9 bytes  */
      USB_DESCRIPTOR_TYPE_ENDPOINT,         /* ENDPOINT Descriptor Type   */
      USB_AUDIO_PLAYER_STREAM_ENDPOINT |
        (USB_OUT << 7),                     /* This is an IN endpoint with endpoint number 2   */
      USB_ENDPOINT_ISOCHRONOUS | 0x01 << 2, /*   Types - Transfer: ISOCHRONOUS
                                                                  Sync: Async */
      USB_SHORT_GET_LOW(HS_ISO_OUT_ENDP_PACKET_SIZE),
      USB_SHORT_GET_HIGH(
        HS_ISO_OUT_ENDP_PACKET_SIZE), /* Maximum packet size for this endpoint is 8 Bytes.  */
      HS_ISO_OUT_ENDP_INTERVAL, /* The polling interval value is every 1 Frames. If Hi-Speed, every
                                      1 uFrames   */
    },
	.cs_as_ep = {
      /* Audio Class Specific ENDPOINT Descriptor  */
      USB_AUDIO_STREAMING_ENDP_DESC_SIZE,      /*  Size of the descriptor, in bytes  */
      USB_AUDIO_STREAM_ENDPOINT_DESCRIPTOR,    /* CS_ENDPOINT Descriptor Type  */
      USB_AUDIO_EP_GENERAL_DESCRIPTOR_SUBTYPE, /* AUDIO_EP_GENERAL descriptor subtype  */
      0x00U,                                   /* Bit 0: Sampling Frequency 0
                                                  Bit 1: Pitch 0
                                                  Bit 7: MaxPacketsOnly 0   */
      0x00U, /* Indicates the units used for the wLockDelay field: 0: Undefined  */
      0x00U,
      0x00U, /* Indicates the time it takes this endpoint to reliably lock its internal clock
              recovery circuitry */
      0x00U,
    },
	.as_fdb_ep = {
      /* Endpoint 3 Feedback ENDPOINT */
      USB_AUDIO_STANDARD_AS_ISO_FEEDBACK_ENDPOINT_LENGTH, /* Descriptor size is 7
                                                             bytes */
      USB_DESCRIPTOR_TYPE_ENDPOINT,                       /* bDescriptorType */
      USB_AUDIO_PLAYER_FEEDBACK_ENDPOINT |
        (USB_IN << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT), /* This is an IN
                                                                        endpoint with
                                                                        endpoint number
                                                                        3 */
      USB_ENDPOINT_ISOCHRONOUS | 0x01 << 4,                          /*  Types -
                                                                    Transfer: ISOCHRONOUS
                                                                    Sync: Async
                                                                    Usage: Feedback EP   */
      0x04,
      0x00,                          /* wMaxPacketSize */
      FS_ISO_FEEDBACK_ENDP_INTERVAL, /* interval polling(2^x ms) */
    }
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
usb_msc_interface_descriptor_t g_UsbStorageInterfaceDescriptor = {
    /* MSC Interface Descriptor */
    .iface = {
      USB_DESCRIPTOR_LENGTH_INTERFACE,
      USB_DESCRIPTOR_TYPE_INTERFACE,
      USB_MSC_DISK_INTERFACE_INDEX,
      USB_MSC_DISK_INTERFACE_ALTERNATE_0,
      USB_MSC_DISK_ENDPOINT_COUNT,
      USB_MSC_DISK_CLASS,
      USB_MSC_DISK_SUBCLASS,
      USB_MSC_DISK_PROTOCOL,
      0x00, /* Interface Description String Index*/
    },
    .ep0 = {
      /*Bulk IN Endpoint descriptor */
      USB_DESCRIPTOR_LENGTH_ENDPOINT,
      USB_DESCRIPTOR_TYPE_ENDPOINT,
      USB_MSC_DISK_BULK_IN_ENDPOINT | (USB_IN << 7U),
      USB_ENDPOINT_BULK,
      USB_SHORT_GET_LOW(HS_MSC_DISK_BULK_IN_PACKET_SIZE),
      USB_SHORT_GET_HIGH(HS_MSC_DISK_BULK_IN_PACKET_SIZE),
      0x00, /* The polling interval value is every 0 Frames */
    },
	.ep1 = {
      /*Bulk OUT Endpoint descriptor */
      USB_DESCRIPTOR_LENGTH_ENDPOINT,
      USB_DESCRIPTOR_TYPE_ENDPOINT,
      USB_MSC_DISK_BULK_OUT_ENDPOINT | (USB_OUT << 7U),
      USB_ENDPOINT_BULK,
      USB_SHORT_GET_LOW(HS_MSC_DISK_BULK_OUT_PACKET_SIZE),
      USB_SHORT_GET_HIGH(HS_MSC_DISK_BULK_OUT_PACKET_SIZE),
      0x00, /* The polling interval value is every 0 Frames */
    }
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
usb_cdc_interface_descriptor_t g_UsbSerialInterfaceDescriptor = {
    /* Interface Association Descriptor */
    /* Size of this descriptor in bytes */
    .iad = {
      USB_IAD_DESC_SIZE,
      /* INTERFACE_ASSOCIATION Descriptor Type  */
      USB_DESCRIPTOR_TYPE_INTERFACE_ASSOCIATION,
      /* The first interface number associated with this function */
      USB_CDC_VCOM_CIC_INTERFACE_INDEX,
      /* The number of contiguous interfaces associated with this function */
      USB_CDC_VCOM_INTERFACE_COUNT,
      /* The function belongs to the Communication Device/Interface Class  */
      USB_CDC_VCOM_CIC_CLASS,
      USB_CDC_VCOM_CIC_SUBCLASS,
      /* The function uses the No class specific protocol required Protocol  */
      USB_CDC_VCOM_CIC_PROTOCOL,
      /* The Function string descriptor index */
      0x06,
    },
    /* Interface Descriptor */
    .iface0 = {
      USB_DESCRIPTOR_LENGTH_INTERFACE,
      USB_DESCRIPTOR_TYPE_INTERFACE,
      USB_CDC_VCOM_CIC_INTERFACE_INDEX,
      USB_CDC_VCOM_CIC_INTERFACE_ALTERNATE_0,
      USB_CDC_VCOM_CIC_ENDPOINT_COUNT,
      USB_CDC_VCOM_CIC_CLASS,
      USB_CDC_VCOM_CIC_SUBCLASS,
      USB_CDC_VCOM_CIC_PROTOCOL,
      0x00,
    },
    .func0 = {
      /* CDC Class-Specific descriptor */
      USB_DESCRIPTOR_LENGTH_CDC_HEADER_FUNC, /* Size of this descriptor in bytes */
      USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE,  /* CS_INTERFACE Descriptor Type */
      USB_CDC_HEADER_FUNC_DESC,
      0x10,
      0x01, /* USB Class Definitions for Communications the Communication specification version 1.10*/
    },
	.func1 = {
      USB_DESCRIPTOR_LENGTH_CDC_CALL_MANAG, /* Size of this descriptor in bytes */
      USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE, /* CS_INTERFACE Descriptor Type */
      USB_CDC_CALL_MANAGEMENT_FUNC_DESC,
      0x01, /*Bit 0: Whether device handle call management itself 1, Bit 1: Whether device can
               send/receive call management information over a Data Class Interface 0 */
      0x01, /* Indicates multiplexed commands are handled via data interface */
    },
	.func2 = {
      USB_DESCRIPTOR_LENGTH_CDC_ABSTRACT,   /* Size of this descriptor in bytes */
      USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE, /* CS_INTERFACE Descriptor Type */
      USB_CDC_ABSTRACT_CONTROL_FUNC_DESC,
      0x06, /* Bit 0: Whether device supports the request combination of Set_Comm_Feature,
               Clear_Comm_Feature, and Get_Comm_Feature 0, Bit 1: Whether device supports the
               request combination of Set_Line_Coding, Set_Control_Line_State, Get_Line_Coding, and
               the notification Serial_State 1, Bit ...  */
    },
	.func3 = {
      USB_DESCRIPTOR_LENGTH_CDC_UNION_FUNC, /* Size of this descriptor in bytes */
      USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE, /* CS_INTERFACE Descriptor Type */
      USB_CDC_UNION_FUNC_DESC,
      USB_CDC_VCOM_CIC_INTERFACE_INDEX, /* The interface number of the Communications or Data Class
                                           interface  */
      USB_CDC_VCOM_DIC_INTERFACE_INDEX, /* Interface number of subordinate interface in the Union */
    },
	.ep0 = {
      /*Notification Endpoint descriptor */
      USB_DESCRIPTOR_LENGTH_ENDPOINT,
      USB_DESCRIPTOR_TYPE_ENDPOINT,
      USB_CDC_VCOM_CIC_INTERRUPT_IN_ENDPOINT | (USB_IN << 7U),
      USB_ENDPOINT_INTERRUPT,
      USB_SHORT_GET_LOW(FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE),
      USB_SHORT_GET_HIGH(FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE),
      FS_CDC_VCOM_INTERRUPT_IN_INTERVAL,
    },
    .iface1 = {
      /* Data Interface Descriptor */
      USB_DESCRIPTOR_LENGTH_INTERFACE,
      USB_DESCRIPTOR_TYPE_INTERFACE,
      USB_CDC_VCOM_DIC_INTERFACE_INDEX,
      USB_CDC_VCOM_DIC_INTERFACE_ALTERNATE_0,
      USB_CDC_VCOM_DIC_ENDPOINT_COUNT,
      USB_CDC_VCOM_DIC_CLASS,
      USB_CDC_VCOM_DIC_SUBCLASS,
      USB_CDC_VCOM_DIC_PROTOCOL,
      0x00, /* Interface Description String Index*/
    },
	.ep1_0 = {
    /*Bulk IN Endpoint descriptor */
    USB_DESCRIPTOR_LENGTH_ENDPOINT, USB_DESCRIPTOR_TYPE_ENDPOINT,
      USB_CDC_VCOM_DIC_BULK_IN_ENDPOINT | (USB_IN << 7U), USB_ENDPOINT_BULK,
      USB_SHORT_GET_LOW(FS_CDC_VCOM_BULK_IN_PACKET_SIZE),
      USB_SHORT_GET_HIGH(FS_CDC_VCOM_BULK_IN_PACKET_SIZE),
      0x00, /* The polling interval value is every 0 Frames */
    },
	.ep1_1 = {
      /*Bulk OUT Endpoint descriptor */
      USB_DESCRIPTOR_LENGTH_ENDPOINT, USB_DESCRIPTOR_TYPE_ENDPOINT,
      USB_CDC_VCOM_DIC_BULK_OUT_ENDPOINT | (USB_OUT << 7U), USB_ENDPOINT_BULK,
      USB_SHORT_GET_LOW(FS_CDC_VCOM_BULK_OUT_PACKET_SIZE),
      USB_SHORT_GET_HIGH(FS_CDC_VCOM_BULK_OUT_PACKET_SIZE),
      0x00, /* The polling interval value is every 0 Frames */
    }
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
usb_configuration_descriptor_t g_UsbDeviceConfigurationDescriptor;

/*******************************************************************************
 * Variables
 ******************************************************************************/

#if (defined(USB_DEVICE_CONFIG_CV_TEST) && (USB_DEVICE_CONFIG_CV_TEST > 0U))
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceQualifierDescriptor[] = {
    USB_DESCRIPTOR_LENGTH_DEVICE_QUALITIER, /* Size of this descriptor in bytes */
    USB_DESCRIPTOR_TYPE_DEVICE_QUALITIER,   /* DEVICE Descriptor Type */
    USB_SHORT_GET_LOW(USB_DEVICE_SPECIFIC_BCD_VERSION),
    USB_SHORT_GET_HIGH(
      USB_DEVICE_SPECIFIC_BCD_VERSION), /* USB Specification Release Number in
                                           Binary-Coded Decimal (i.e., 2.10 is 210H). */
    USB_DEVICE_CLASS,                   /* Class code (assigned by the USB-IF). */
    USB_DEVICE_SUBCLASS,                /* Subclass code (assigned by the USB-IF). */
    USB_DEVICE_PROTOCOL,                /* Protocol code (assigned by the USB-IF). */
    USB_CONTROL_MAX_PACKET_SIZE,        /* Maximum packet size for endpoint zero
                                           (only 8, 16, 32, or 64 are valid) */
    0x00U,                              /* Number of Other-speed Configurations */
    0x00U,                              /* Reserved for future use, must be zero */
};
#endif
/* Define string descriptor */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString0[] = {
    2U + 2U,
    USB_DESCRIPTOR_TYPE_STRING,
    0x09U,
    0x04U,
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString1[] = {
    2U + 2U * 7U, USB_DESCRIPTOR_TYPE_STRING,
    'P',          0x00U,
    'o',          0x00U,
    'l',          0x00U,
    'y',          0x00U,
    'e',          0x00U,
    'n',          0x00U,
    'd',          0x00U,
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString2[] = {
#ifdef TRACKER_MINI
    2U + 2U * 12U, USB_DESCRIPTOR_TYPE_STRING,
    'T',           0x00U,
    'r',           0x00U,
    'a',           0x00U,
    'c',           0x00U,
    'k',           0x00U,
    'e',           0x00U,
    'r',           0x00U,
    ' ',           0x00U,
    'M',           0x00U,
    'i',           0x00U,
    'n',           0x00U,
    'i',           0x00U,
#else
    2U + 2U * 12U, USB_DESCRIPTOR_TYPE_STRING,
    'T',           0x00U,
    'r',           0x00U,
    'a',           0x00U,
    'c',           0x00U,
    'k',           0x00U,
    'e',           0x00U,
    'r',           0x00U,
    ' ',           0x00U,
    'P',           0x00U,
    'l',           0x00U,
    'u',           0x00U,
    's',           0x00U,
#endif
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString3[] = { 2U + 2U * 16U, USB_DESCRIPTOR_TYPE_STRING,
                                 '0',           0x00U,
                                 '1',           0x00U,
                                 '2',           0x00U,
                                 '3',           0x00U,
                                 '4',           0x00U,
                                 '5',           0x00U,
                                 '6',           0x00U,
                                 '7',           0x00U,
                                 '8',           0x00U,
                                 '9',           0x00U,
                                 'A',           0x00U,
                                 'B',           0x00U,
                                 'C',           0x00U,
                                 'D',           0x00U,
                                 'E',           0x00U,
                                 'F',           0x00U };

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString4[] = {
#ifdef TRACKER_MINI
    2U + 2U * 17U, USB_DESCRIPTOR_TYPE_STRING,
    'T',           0x00U,
    'r',           0x00U,
    'a',           0x00U,
    'c',           0x00U,
    'k',           0x00U,
    'e',           0x00U,
    'r',           0x00U,
    ' ',           0x00U,
    'M',           0x00U,
    'i',           0x00U,
    'n',           0x00U,
    'i',           0x00U,
    ' ',           0x00U,
    'M',           0x00U,
    'I',           0x00U,
    'D',           0x00U,
    'I',           0x00U,
#else
    2U + 2U * 17U, USB_DESCRIPTOR_TYPE_STRING,
    'T',           0x00U,
    'r',           0x00U,
    'a',           0x00U,
    'c',           0x00U,
    'k',           0x00U,
    'e',           0x00U,
    'r',           0x00U,
    ' ',           0x00U,
    'P',           0x00U,
    'l',           0x00U,
    'u',           0x00U,
    's',           0x00U,
    ' ',           0x00U,
    'M',           0x00U,
    'I',           0x00U,
    'D',           0x00U,
    'I',           0x00U,
#endif
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString5[] = {
#ifdef TRACKER_MINI
    2U + 2U * 18U, USB_DESCRIPTOR_TYPE_STRING,
    'T',           0x00U,
    'r',           0x00U,
    'a',           0x00U,
    'c',           0x00U,
    'k',           0x00U,
    'e',           0x00U,
    'r',           0x00U,
    ' ',           0x00U,
    'M',           0x00U,
    'i',           0x00U,
    'n',           0x00U,
    'i',           0x00U,
    ' ',           0x00U,
    'A',           0x00U,
    'U',           0x00U,
    'D',           0x00U,
    'I',           0x00U,
    'O',           0x00U,
#else
    2U + 2U * 18U, USB_DESCRIPTOR_TYPE_STRING,
    'T',           0x00U,
    'r',           0x00U,
    'a',           0x00U,
    'c',           0x00U,
    'k',           0x00U,
    'e',           0x00U,
    'r',           0x00U,
    ' ',           0x00U,
    'P',           0x00U,
    'l',           0x00U,
    'u',           0x00U,
    's',           0x00U,
    ' ',           0x00U,
    'A',           0x00U,
    'U',           0x00U,
    'D',           0x00U,
    'I',           0x00U,
    'O',           0x00U,
#endif
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString6[] = {
#ifdef TRACKER_MINI
    2U + 2U * 16U, USB_DESCRIPTOR_TYPE_STRING,
    'T',           0x00U,
    'r',           0x00U,
    'a',           0x00U,
    'c',           0x00U,
    'k',           0x00U,
    'e',           0x00U,
    'r',           0x00U,
    ' ',           0x00U,
    'M',           0x00U,
    'i',           0x00U,
    'n',           0x00U,
    'i',           0x00U,
    ' ',           0x00U,
    'C',           0x00U,
    'O',           0x00U,
    'M',           0x00U
#else
    2U + 2U * 16U, USB_DESCRIPTOR_TYPE_STRING,
    'T',           0x00U,
    'r',           0x00U,
    'a',           0x00U,
    'c',           0x00U,
    'k',           0x00U,
    'e',           0x00U,
    'r',           0x00U,
    ' ',           0x00U,
    'P',           0x00U,
    'l',           0x00U,
    'u',           0x00U,
    's',           0x00U,
    ' ',           0x00U,
    'C',           0x00U,
    'O',           0x00U,
    'M',           0x00U
#endif
};

/* Define string descriptor size */
uint32_t g_UsbDeviceStringDescriptorLength[USB_DEVICE_STRING_COUNT] = {
    sizeof(g_UsbDeviceString0), sizeof(g_UsbDeviceString1), sizeof(g_UsbDeviceString2),
    sizeof(g_UsbDeviceString3), sizeof(g_UsbDeviceString4), sizeof(g_UsbDeviceString5),
    sizeof(g_UsbDeviceString6)
};

uint8_t* g_UsbDeviceStringDescriptorArray[USB_DEVICE_STRING_COUNT] = {
    g_UsbDeviceString0, g_UsbDeviceString1, g_UsbDeviceString2, g_UsbDeviceString3,
    g_UsbDeviceString4, g_UsbDeviceString5, g_UsbDeviceString6
};

usb_language_t g_UsbDeviceLanguage[USB_DEVICE_LANGUAGE_COUNT] = { {
  g_UsbDeviceStringDescriptorArray,
  g_UsbDeviceStringDescriptorLength,
  (uint16_t)0x0409,
} };

usb_language_list_t g_UsbDeviceLanguageList = {
    g_UsbDeviceString0,
    sizeof(g_UsbDeviceString0),
    g_UsbDeviceLanguage,
    USB_DEVICE_LANGUAGE_COUNT,
};

/*******************************************************************************
 * Code
 ******************************************************************************/
/*!
 * @brief USB device get device descriptor function.
 *
 * This function gets the device descriptor of the USB device.
 *
 * @param handle The USB device handle.
 * @param deviceDescriptor The pointer to the device descriptor structure.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceGetDeviceDescriptor(
  usb_device_handle handle,
  usb_device_get_device_descriptor_struct_t* deviceDescriptor)
{
    deviceDescriptor->buffer = g_UsbDeviceDescriptor;
    deviceDescriptor->length = USB_DESCRIPTOR_LENGTH_DEVICE;
    return kStatus_USB_Success;
}
#if (defined(USB_DEVICE_CONFIG_CV_TEST) && (USB_DEVICE_CONFIG_CV_TEST > 0U))
/* Get device qualifier descriptor request */
usb_status_t USB_DeviceGetDeviceQualifierDescriptor(
  usb_device_handle handle,
  usb_device_get_device_qualifier_descriptor_struct_t* deviceQualifierDescriptor)
{
    deviceQualifierDescriptor->buffer = g_UsbDeviceQualifierDescriptor;
    deviceQualifierDescriptor->length = USB_DESCRIPTOR_LENGTH_DEVICE_QUALITIER;
    return kStatus_USB_Success;
}
#endif
/*!
 * @brief USB device get configuration descriptor function.
 *
 * This function gets the configuration descriptor of the USB device.
 *
 * @param handle The USB device handle.
 * @param configurationDescriptor The pointer to the configuration descriptor structure.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceGetConfigurationDescriptor(
  usb_device_handle handle,
  usb_device_get_configuration_descriptor_struct_t* configurationDescriptor)
{
    if (USB_COMPOSITE_CONFIGURE_INDEX > configurationDescriptor->configuration)
    {
        uint16_t wTotalLength = USB_DESCRIPTOR_LENGTH_CONFIGURE;
        uint8_t* descriptor = (uint8_t*)&g_UsbDeviceConfigurationDescriptor;
        uint8_t interfaceNum = 0;

        g_UsbDeviceConfigurationDescriptor.bLength = USB_DESCRIPTOR_LENGTH_CONFIGURE;
        g_UsbDeviceConfigurationDescriptor.bDescriptorType = USB_DESCRIPTOR_TYPE_CONFIGURE;
        g_UsbDeviceConfigurationDescriptor.wTotalLength = 0;   // to be set appropriatelly
        g_UsbDeviceConfigurationDescriptor.bNumInterfaces = 0; // to be set appropriatelly
        g_UsbDeviceConfigurationDescriptor.bConfigurationValue = USB_COMPOSITE_CONFIGURE_INDEX;
        g_UsbDeviceConfigurationDescriptor.iConfiguration = 0;
        /* Configuration characteristics D7: Reserved (set to one) D6: Self-powered D5: Remote
           Wakeup D4...0: Reserved (reset to zero) */
        g_UsbDeviceConfigurationDescriptor.bmAttributes =
          (USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_D7_MASK) |
          (USB_DEVICE_CONFIG_SELF_POWER << USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_SELF_POWERED_SHIFT) |
          (USB_DEVICE_CONFIG_REMOTE_WAKEUP
           << USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_REMOTE_WAKEUP_SHIFT);
        /* Maximum power consumption of the USB * device from the bus in this specific *
           configuration when the device is fully * operational. Expressed in 2 mA units *  (i.e.,
           50 = 100 mA).  */
        g_UsbDeviceConfigurationDescriptor.bMaxPower = USB_DEVICE_MAX_POWER;

        g_cdcVcomInterfaces[0].interfaceNumber = 0xFF;
        g_cdcVcomInterfaces[1].interfaceNumber = 0xFF;
        g_mscDiskInterfaces[0].interfaceNumber = 0xFF;
        g_UsbDeviceAudioPlayerInterfaces[0].interfaceNumber = 0xFF;
        g_UsbDeviceAudioPlayerInterfaces[1].interfaceNumber = 0xFF;
        g_UsbDeviceAudioRecorderInterfaces[0].interfaceNumber = 0xFF;
        g_UsbDeviceAudioRecorderInterfaces[1].interfaceNumber = 0xFF;
        g_MidiInterfaces[0].interfaceNumber = 0xFF;

        if (g_UsbState.isSerialEnabled)
        {
            g_cdcVcomInterfaces[0].interfaceNumber = interfaceNum;
            g_UsbSerialInterfaceDescriptor.iad.bFirstInterface = interfaceNum;
            g_UsbSerialInterfaceDescriptor.func3.bControlInterface = interfaceNum;
            g_UsbSerialInterfaceDescriptor.iface0.bInterfaceNumber = interfaceNum++;
            g_cdcVcomInterfaces[1].interfaceNumber = interfaceNum;
            g_UsbSerialInterfaceDescriptor.func1.bDataInterface = interfaceNum;
            g_UsbSerialInterfaceDescriptor.func3.bSubordinateInterface0 = interfaceNum;
            g_UsbSerialInterfaceDescriptor.iface1.bInterfaceNumber = interfaceNum++;
            memcpy(&descriptor[wTotalLength],
                   &g_UsbSerialInterfaceDescriptor,
                   sizeof(g_UsbSerialInterfaceDescriptor));
            wTotalLength += sizeof(g_UsbSerialInterfaceDescriptor);
        }

        if (g_UsbState.isStorageEnabled)
        {
            g_mscDiskInterfaces[0].interfaceNumber = interfaceNum;
            g_UsbStorageInterfaceDescriptor.iface.bInterfaceNumber = interfaceNum++;
            memcpy(&descriptor[wTotalLength],
                   &g_UsbStorageInterfaceDescriptor,
                   sizeof(g_UsbStorageInterfaceDescriptor));
            wTotalLength += sizeof(g_UsbStorageInterfaceDescriptor);
        }

        if (g_UsbState.isAudioInEnabled)
        {
            if (g_UsbState.isAudioOutEnabled)
            {
                g_UsbDeviceAudioRecorderInterfaces[0].interfaceNumber = interfaceNum;
                g_UsbDeviceAudioPlayerInterfaces[0].interfaceNumber = interfaceNum;
                g_UsbAudioInterfaceDescriptor.iad.bFirstInterface = interfaceNum;
                g_UsbAudioInterfaceDescriptor.ac_iface.bInterfaceNumber = interfaceNum++;
                g_UsbDeviceAudioRecorderInterfaces[1].interfaceNumber = interfaceNum;
                g_UsbAudioInterfaceDescriptor.as_iface0_0.bInterfaceNumber = interfaceNum;
                g_UsbAudioInterfaceDescriptor.as_iface0_1.bInterfaceNumber = interfaceNum++;
                g_UsbDeviceAudioPlayerInterfaces[1].interfaceNumber = interfaceNum;
                g_UsbAudioInterfaceDescriptor.as_iface1_0.bInterfaceNumber = interfaceNum;
                g_UsbAudioInterfaceDescriptor.as_iface1_1.bInterfaceNumber = interfaceNum++;
                memcpy(&descriptor[wTotalLength],
                       &g_UsbAudioInterfaceDescriptor,
                       sizeof(g_UsbAudioInterfaceDescriptor));
                wTotalLength += sizeof(g_UsbAudioInterfaceDescriptor);
            }
            else
            {
                g_UsbDeviceAudioRecorderInterfaces[0].interfaceNumber = interfaceNum;
                g_UsbAudioInInterfaceDescriptor.iad.bFirstInterface = interfaceNum;
                g_UsbAudioInInterfaceDescriptor.ac_iface.bInterfaceNumber = interfaceNum++;
                g_UsbDeviceAudioRecorderInterfaces[1].interfaceNumber = interfaceNum;
                g_UsbAudioInInterfaceDescriptor.as_iface_0.bInterfaceNumber = interfaceNum;
                g_UsbAudioInInterfaceDescriptor.as_iface_1.bInterfaceNumber = interfaceNum++;
                memcpy(&descriptor[wTotalLength],
                       (uint8_t*)&g_UsbAudioInInterfaceDescriptor,
                       sizeof(g_UsbAudioInInterfaceDescriptor));
                wTotalLength += sizeof(g_UsbAudioInInterfaceDescriptor);
            }
        }
        else
        {
            if (g_UsbState.isAudioOutEnabled)
            {
                g_UsbDeviceAudioPlayerInterfaces[0].interfaceNumber = interfaceNum;
                g_UsbAudioOutInterfaceDescriptor.iad.bFirstInterface = interfaceNum;
                g_UsbAudioOutInterfaceDescriptor.ac_iface.bInterfaceNumber = interfaceNum++;
                g_UsbDeviceAudioPlayerInterfaces[1].interfaceNumber = interfaceNum;
                g_UsbAudioOutInterfaceDescriptor.as_iface_0.bInterfaceNumber = interfaceNum;
                g_UsbAudioOutInterfaceDescriptor.as_iface_1.bInterfaceNumber = interfaceNum++;
                memcpy(&descriptor[wTotalLength],
                       &g_UsbAudioOutInterfaceDescriptor,
                       sizeof(g_UsbAudioOutInterfaceDescriptor));
                wTotalLength += sizeof(g_UsbAudioOutInterfaceDescriptor);
            }
        }

        if (g_UsbState.isMidiEnabled)
        {
            g_MidiInterfaces[0].interfaceNumber = interfaceNum;
            g_UsbMidiInterfaceDescriptor.iface.bInterfaceNumber = interfaceNum++;
            memcpy(&descriptor[wTotalLength],
                   &g_UsbMidiInterfaceDescriptor,
                   sizeof(g_UsbMidiInterfaceDescriptor));
            wTotalLength += sizeof(g_UsbMidiInterfaceDescriptor);
        }

        g_UsbDeviceConfigurationDescriptor.bNumInterfaces = interfaceNum;
        g_UsbDeviceConfigurationDescriptor.wTotalLength = wTotalLength;

        configurationDescriptor->buffer = descriptor;
        configurationDescriptor->length = wTotalLength;

        return kStatus_USB_Success;
    }
    return kStatus_USB_InvalidRequest;
}

/*!
 * @brief USB device get string descriptor function.
 *
 * This function gets the string descriptor of the USB device.
 *
 * @param handle The USB device handle.
 * @param stringDescriptor Pointer to the string descriptor structure.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceGetStringDescriptor(
  usb_device_handle handle,
  usb_device_get_string_descriptor_struct_t* stringDescriptor)
{
    if (stringDescriptor->stringIndex == 0)
    {
        stringDescriptor->buffer = (uint8_t*)g_UsbDeviceLanguageList.languageString;
        stringDescriptor->length = g_UsbDeviceLanguageList.stringLength;
    }
    else
    {
        uint8_t langId = 0;
        uint8_t langIndex = USB_DEVICE_STRING_COUNT;

        for (; langId < USB_DEVICE_LANGUAGE_COUNT; langId++)
        {
            if (stringDescriptor->languageId ==
                g_UsbDeviceLanguageList.languageList[langId].languageId)
            {
                if (stringDescriptor->stringIndex < USB_DEVICE_STRING_COUNT)
                {
                    langIndex = stringDescriptor->stringIndex;
                }
                break;
            }
        }

        if (USB_DEVICE_STRING_COUNT == langIndex)
        {
            return kStatus_USB_InvalidRequest;
        }
        stringDescriptor->buffer =
          (uint8_t*)g_UsbDeviceLanguageList.languageList[langId].string[langIndex];
        stringDescriptor->length = g_UsbDeviceLanguageList.languageList[langId].length[langIndex];
    }
    return kStatus_USB_Success;
}

/*!
 * @brief USB device set speed function.
 *
 * This function sets the speed of the USB device.
 *
 * Due to the difference of HS and FS descriptors, the device descriptors and configurations need to
 * be updated to match current speed. As the default, the device descriptors and configurations are
 * configured by using FS parameters for both EHCI and KHCI. When the EHCI is enabled, the
 * application needs to call this function to update device by using current speed. The updated
 * information includes endpoint max packet size, endpoint interval, etc.
 *
 * @param handle The USB device handle.
 * @param speed Speed type. USB_SPEED_HIGH/USB_SPEED_FULL/USB_SPEED_LOW.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceSetSpeed(usb_device_handle handle, uint8_t speed)
{
    usb_descriptor_union_t* ptr1;
    usb_descriptor_union_t* ptr2;

    ptr1 = (usb_descriptor_union_t*)(&g_UsbDeviceConfigurationDescriptor);
    ptr2 = (usb_descriptor_union_t*)((uint32_t)&g_UsbDeviceConfigurationDescriptor +
                                     g_UsbDeviceConfigurationDescriptor.wTotalLength);

    while (ptr1 < ptr2)
    {
        if (ptr1->common.bDescriptorType == USB_DESCRIPTOR_TYPE_ENDPOINT)
        {
            if (USB_SPEED_HIGH == speed)
            {
                if ((USB_CDC_VCOM_CIC_INTERRUPT_IN_ENDPOINT ==
                     (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                    ((ptr1->endpoint.bEndpointAddress &
                      USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                     USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_IN))
                {
                    ptr1->endpoint.bInterval = HS_CDC_VCOM_INTERRUPT_IN_INTERVAL;
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else if ((USB_CDC_VCOM_DIC_BULK_IN_ENDPOINT ==
                          (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                         ((ptr1->endpoint.bEndpointAddress &
                           USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                          USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_IN))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_CDC_VCOM_BULK_IN_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else if ((USB_CDC_VCOM_DIC_BULK_OUT_ENDPOINT ==
                          (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                         ((ptr1->endpoint.bEndpointAddress &
                           USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                          USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_OUT))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_CDC_VCOM_BULK_OUT_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else if ((USB_MSC_DISK_BULK_IN_ENDPOINT ==
                          (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                         ((ptr1->endpoint.bEndpointAddress &
                           USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                          USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_IN))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_MSC_DISK_BULK_IN_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else if ((USB_MSC_DISK_BULK_OUT_ENDPOINT ==
                          (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                         ((ptr1->endpoint.bEndpointAddress &
                           USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                          USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_OUT))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_MSC_DISK_BULK_OUT_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else if ((USB_MIDI_BULK_IN_ENDPOINT ==
                          (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                         ((ptr1->endpoint.bEndpointAddress &
                           USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                          USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_IN))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_MIDI_BULK_IN_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else if ((USB_MIDI_BULK_OUT_ENDPOINT ==
                          (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                         ((ptr1->endpoint.bEndpointAddress &
                           USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                          USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_OUT))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_MIDI_BULK_OUT_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else
                {
                }
            }
            else
            {
                if ((USB_CDC_VCOM_CIC_INTERRUPT_IN_ENDPOINT ==
                     (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                    ((ptr1->endpoint.bEndpointAddress &
                      USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                     USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_IN))
                {
                    ptr1->endpoint.bInterval = FS_CDC_VCOM_INTERRUPT_IN_INTERVAL;
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else if ((USB_CDC_VCOM_DIC_BULK_IN_ENDPOINT ==
                          (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                         ((ptr1->endpoint.bEndpointAddress &
                           USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                          USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_IN))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_CDC_VCOM_BULK_IN_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else if ((USB_CDC_VCOM_DIC_BULK_OUT_ENDPOINT ==
                          (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                         ((ptr1->endpoint.bEndpointAddress &
                           USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                          USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_OUT))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_CDC_VCOM_BULK_OUT_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else if ((USB_MSC_DISK_BULK_IN_ENDPOINT ==
                          (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                         ((ptr1->endpoint.bEndpointAddress &
                           USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                          USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_IN))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_MSC_DISK_BULK_IN_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else if ((USB_MSC_DISK_BULK_OUT_ENDPOINT ==
                          (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                         ((ptr1->endpoint.bEndpointAddress &
                           USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                          USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_OUT))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_MSC_DISK_BULK_OUT_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else if ((USB_MIDI_BULK_IN_ENDPOINT ==
                          (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                         ((ptr1->endpoint.bEndpointAddress &
                           USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                          USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_IN))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_MIDI_BULK_IN_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else if ((USB_MIDI_BULK_OUT_ENDPOINT ==
                          (ptr1->endpoint.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) &&
                         ((ptr1->endpoint.bEndpointAddress &
                           USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                          USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_OUT))
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_MIDI_BULK_OUT_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else
                {
                }
            }
        }
        ptr1 = (usb_descriptor_union_t*)((uint8_t*)ptr1 + ptr1->common.bLength);
    }

    if (USB_SPEED_HIGH == speed)
    {
        USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_MIDI_BULK_OUT_PACKET_SIZE,
                                           g_UsbMidiInterfaceDescriptor.ep0.ep.wMaxPacketSize);
        USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_MIDI_BULK_IN_PACKET_SIZE,
                                           g_UsbMidiInterfaceDescriptor.ep1.ep.wMaxPacketSize);

        g_UsbSerialInterfaceDescriptor.ep0.bInterval = HS_CDC_VCOM_INTERRUPT_IN_INTERVAL;
        USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE,
                                           g_UsbSerialInterfaceDescriptor.ep0.wMaxPacketSize);
        USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_CDC_VCOM_BULK_IN_PACKET_SIZE,
                                           g_UsbSerialInterfaceDescriptor.ep1_0.wMaxPacketSize);
        USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_CDC_VCOM_BULK_OUT_PACKET_SIZE,
                                           g_UsbSerialInterfaceDescriptor.ep1_1.wMaxPacketSize);

        USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_MSC_DISK_BULK_IN_PACKET_SIZE,
                                           g_UsbStorageInterfaceDescriptor.ep0.wMaxPacketSize);
        USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_MSC_DISK_BULK_OUT_PACKET_SIZE,
                                           g_UsbStorageInterfaceDescriptor.ep1.wMaxPacketSize);
    }
    else
    {
        USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_MIDI_BULK_OUT_PACKET_SIZE,
                                           g_UsbMidiInterfaceDescriptor.ep0.ep.wMaxPacketSize);
        USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_MIDI_BULK_IN_PACKET_SIZE,
                                           g_UsbMidiInterfaceDescriptor.ep1.ep.wMaxPacketSize);

        g_UsbSerialInterfaceDescriptor.ep0.bInterval = FS_CDC_VCOM_INTERRUPT_IN_INTERVAL;
        USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE,
                                           g_UsbSerialInterfaceDescriptor.ep0.wMaxPacketSize);
        USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_CDC_VCOM_BULK_IN_PACKET_SIZE,
                                           g_UsbSerialInterfaceDescriptor.ep1_0.wMaxPacketSize);
        USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_CDC_VCOM_BULK_OUT_PACKET_SIZE,
                                           g_UsbSerialInterfaceDescriptor.ep1_1.wMaxPacketSize);

        USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_MSC_DISK_BULK_IN_PACKET_SIZE,
                                           g_UsbStorageInterfaceDescriptor.ep0.wMaxPacketSize);
        USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_MSC_DISK_BULK_OUT_PACKET_SIZE,
                                           g_UsbStorageInterfaceDescriptor.ep1.wMaxPacketSize);
    }

    for (int i = 0; i < USB_MIDI_ENDPOINT_COUNT; i++)
    {
        if (USB_SPEED_HIGH == speed)
        {
            if (g_MidiEndpoints[i].endpointAddress &
                USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK)
            {
                g_MidiEndpoints[i].maxPacketSize = HS_MIDI_BULK_IN_PACKET_SIZE;
            }
            else
            {
                g_MidiEndpoints[i].maxPacketSize = HS_MIDI_BULK_OUT_PACKET_SIZE;
            }
        }
        else
        {
            if (g_MidiEndpoints[i].endpointAddress &
                USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK)
            {
                g_MidiEndpoints[i].maxPacketSize = FS_MIDI_BULK_IN_PACKET_SIZE;
            }
            else
            {
                g_MidiEndpoints[i].maxPacketSize = FS_MIDI_BULK_OUT_PACKET_SIZE;
            }
        }
    }

    for (int i = 0; i < USB_CDC_VCOM_CIC_ENDPOINT_COUNT; i++)
    {
        if (USB_SPEED_HIGH == speed)
        {
            g_cdcVcomCicEndpoints[i].maxPacketSize = HS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE;
            g_cdcVcomCicEndpoints[i].interval = HS_CDC_VCOM_INTERRUPT_IN_INTERVAL;
        }
        else
        {
            g_cdcVcomCicEndpoints[i].maxPacketSize = FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE;
            g_cdcVcomCicEndpoints[i].interval = FS_CDC_VCOM_INTERRUPT_IN_INTERVAL;
        }
    }

    for (int i = 0; i < USB_CDC_VCOM_DIC_ENDPOINT_COUNT; i++)
    {
        if (USB_SPEED_HIGH == speed)
        {
            if (g_cdcVcomDicEndpoints[i].endpointAddress &
                USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK)
            {
                g_cdcVcomDicEndpoints[i].maxPacketSize = HS_CDC_VCOM_BULK_IN_PACKET_SIZE;
            }
            else
            {
                g_cdcVcomDicEndpoints[i].maxPacketSize = HS_CDC_VCOM_BULK_OUT_PACKET_SIZE;
            }
        }
        else
        {
            if (g_cdcVcomDicEndpoints[i].endpointAddress &
                USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK)
            {
                g_cdcVcomDicEndpoints[i].maxPacketSize = FS_CDC_VCOM_BULK_IN_PACKET_SIZE;
            }
            else
            {
                g_cdcVcomDicEndpoints[i].maxPacketSize = FS_CDC_VCOM_BULK_OUT_PACKET_SIZE;
            }
        }
    }

    for (int i = 0; i < USB_MSC_DISK_ENDPOINT_COUNT; i++)
    {
        if (USB_SPEED_HIGH == speed)
        {
            if (g_mscDiskEndpoints[i].endpointAddress &
                USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK)
            {
                g_mscDiskEndpoints[i].maxPacketSize = HS_MSC_DISK_BULK_IN_PACKET_SIZE;
            }
            else
            {
                g_mscDiskEndpoints[i].maxPacketSize = HS_MSC_DISK_BULK_OUT_PACKET_SIZE;
            }
        }
        else
        {
            if (g_mscDiskEndpoints[i].endpointAddress &
                USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK)
            {
                g_mscDiskEndpoints[i].maxPacketSize = FS_MSC_DISK_BULK_IN_PACKET_SIZE;
            }
            else
            {
                g_mscDiskEndpoints[i].maxPacketSize = FS_MSC_DISK_BULK_OUT_PACKET_SIZE;
            }
        }
    }

    return kStatus_USB_Success;
}
