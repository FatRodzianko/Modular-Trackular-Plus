/*
 * Copyright (c) 2015 - 2016, Freescale Semiconductor, Inc.
 * Copyright 2016 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "usb_device_config.h"

#include "usb.h"
#include "usb_device.h"
#include "usb_device_class.h"

#if ((defined(USB_DEVICE_CONFIG_MIDI)) && (USB_DEVICE_CONFIG_MIDI > 0U))
#include "usb_device_midi.h"

/* MIDI device instance */

USB_GLOBAL USB_RAM_ADDRESS_ALIGNMENT(USB_DATA_ALIGN_SIZE) static usb_device_midi_struct_t
  g_midiHandle;
/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

static usb_status_t USB_DeviceMidiAllocateHandle(usb_device_midi_struct_t** handle)
{
    if (NULL == g_midiHandle.handle)
    {
        *handle = &g_midiHandle;
        return kStatus_USB_Success;
    }

    return kStatus_USB_Busy;
}

/*!
 * @brief Responds to the bulk in endpoint event.
 *
 * This function responds to the bulk in endpoint event.
 *
 * @param handle The device handle of the MIDI device.
 * @param message The pointer to the message of the endpoint callback.
 * @param callbackParam The pointer to the parameter of the callback.
 * @return A USB error code or kStatus_USB_Success.
 */
static usb_status_t USB_DeviceMidiBulkIn(usb_device_handle handle,
                                         usb_device_endpoint_callback_message_struct_t* message,
                                         void* callbackParam)
{
    usb_device_midi_struct_t* midiHandle;
    usb_status_t status = kStatus_USB_Error;
    midiHandle = (usb_device_midi_struct_t*)callbackParam;

    if (NULL == midiHandle)
    {
        return kStatus_USB_InvalidHandle;
    }

    midiHandle->bulkIn.isBusy = 0;

    if ((NULL != midiHandle->configStruct) && (NULL != midiHandle->configStruct->classCallback))
    {
        /*classCallback is initialized in classInit of s_UsbDeviceClassInterfaceMap,
        it is from the second parameter of classInit */
        status = midiHandle->configStruct->classCallback(
          (class_handle_t)midiHandle, kUSB_DeviceMidiEventSendResponse, message);
    }
    return status;
}

/*!
 * @brief Responds to the bulk out endpoint event.
 *
 * This function responds to the bulk out endpoint event.
 *
 * @param handle The device handle of the MIDI device.
 * @param message The pointer to the message of the endpoint callback.
 * @param callbackParam The pointer to the parameter of the callback.
 * @return A USB error code or kStatus_USB_Success.
 */
static usb_status_t USB_DeviceMidiBulkOut(usb_device_handle handle,
                                          usb_device_endpoint_callback_message_struct_t* message,
                                          void* callbackParam)
{
    usb_device_midi_struct_t* midiHandle;
    usb_status_t status = kStatus_USB_Error;
    midiHandle = (usb_device_midi_struct_t*)callbackParam;

    if (NULL == midiHandle)
    {
        return kStatus_USB_InvalidHandle;
    }

    midiHandle->bulkOut.isBusy = 0U;

    if ((NULL != midiHandle->configStruct) && (NULL != midiHandle->configStruct->classCallback))
    {
        /*classCallback is initialized in classInit of s_UsbDeviceClassInterfaceMap,
        it is from the second parameter of classInit */
        status = midiHandle->configStruct->classCallback(
          (class_handle_t)midiHandle, kUSB_DeviceMidiEventRecvResponse, message);
    }
    return status;
}

/*!
 * @brief Initializes the endpoints in MIDI class.
 *
 * This function initializes the endpoints in MIDI class.
 *
 * @param midiHandle The class handle of the MIDI class.
 * @return A USB error code or kStatus_USB_Success.
 */
static usb_status_t USB_DeviceMidiEndpointsInit(usb_device_midi_struct_t* midiHandle)
{
    usb_device_interface_list_t* interfaceList;
    usb_device_interface_struct_t* interface = NULL;
    usb_device_endpoint_callback_struct_t epCallback;
    usb_status_t error = kStatus_USB_Error;
    uint32_t count;
    uint32_t index;

    if (NULL == midiHandle)
    {
        return error;
    }
    midiHandle->interfaceNumber = 0xFFU;
    midiHandle->interfaceHandle = NULL;
    epCallback.callbackFn = (usb_device_endpoint_callback_t)NULL;
    /* return error when configuration is invalid (0 or more than the
     * configuration number) */
    if ((midiHandle->configuration == 0U) ||
        (midiHandle->configuration > midiHandle->configStruct->classInfomation->configurations))
    {
        return error;
    }

    interfaceList =
      &midiHandle->configStruct->classInfomation->interfaceList[midiHandle->configuration - 1U];

    for (count = 0; count < interfaceList->count; count++)
    {
        if (0xFFU == interfaceList->interfaces[count].interfaceNumber)
        {
            continue;
        }
        if ((USB_MIDI_CLASS == interfaceList->interfaces[count].classCode) &&
            (USB_MIDI_SUBCLASS == interfaceList->interfaces[count].subclassCode))
        {
            for (index = 0; index < interfaceList->interfaces[count].count; index++)
            {
                if (interfaceList->interfaces[count].interface[index].alternateSetting ==
                    midiHandle->alternate)
                {
                    interface = &interfaceList->interfaces[count].interface[index];
                    break;
                }
            }
            midiHandle->interfaceNumber = interfaceList->interfaces[count].interfaceNumber;
            break;
        }
    }
    if (NULL == interface)
    {
        return error;
    }

    midiHandle->interfaceHandle = interface;

    for (count = 0; count < interface->endpointList.count; count++)
    {
        usb_device_endpoint_init_struct_t epInitStruct;
        epInitStruct.zlt = 0;
        epInitStruct.interval = interface->endpointList.endpoint[count].interval;
        epInitStruct.endpointAddress = interface->endpointList.endpoint[count].endpointAddress;
        epInitStruct.maxPacketSize = interface->endpointList.endpoint[count].maxPacketSize;
        epInitStruct.transferType = interface->endpointList.endpoint[count].transferType;

        if ((USB_IN ==
             ((epInitStruct.endpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) >>
              USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT)) &&
            (USB_ENDPOINT_BULK == epInitStruct.transferType))
        {
            midiHandle->bulkIn.ep =
              (epInitStruct.endpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_NUMBER_MASK);
            midiHandle->bulkIn.isBusy = 0;
            midiHandle->bulkIn.pipeDataBuffer = (uint8_t*)USB_INVALID_TRANSFER_BUFFER;
            midiHandle->bulkIn.pipeStall = 0U;
            midiHandle->bulkIn.pipeDataLen = 0U;
            epCallback.callbackFn = USB_DeviceMidiBulkIn;
        }
        else if ((USB_OUT == ((epInitStruct.endpointAddress &
                               USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) >>
                              USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT)) &&
                 (USB_ENDPOINT_BULK == epInitStruct.transferType))
        {
            midiHandle->bulkOut.ep =
              (epInitStruct.endpointAddress & USB_DESCRIPTOR_ENDPOINT_ADDRESS_NUMBER_MASK);
            midiHandle->bulkOut.isBusy = 0;
            midiHandle->bulkOut.pipeDataBuffer = (uint8_t*)USB_INVALID_TRANSFER_BUFFER;
            midiHandle->bulkOut.pipeStall = 0U;
            midiHandle->bulkOut.pipeDataLen = 0U;
            epCallback.callbackFn = USB_DeviceMidiBulkOut;
        }
        else
        {
            /*no action*/
        }
        epCallback.callbackParam = midiHandle;
        epCallback.runInIrq = 1;

        error = USB_DeviceInitEndpoint(midiHandle->handle, &epInitStruct, &epCallback);
    }
    return error;
}

/*!
 * @brief De-initializes the endpoints in MIDI class.
 *
 * This function de-initializes the endpoints in MIDI class.
 *
 * @param midiHandle The class handle of the MIDI class.
 * @return A USB error code or kStatus_USB_Success.
 */
static usb_status_t USB_DeviceMidiEndpointsDeinit(usb_device_midi_struct_t* midiHandle)
{
    usb_status_t status = kStatus_USB_Error;
    uint32_t count;

    if (NULL == midiHandle->interfaceHandle)
    {
        return status;
    }
    for (count = 0; count < midiHandle->interfaceHandle->endpointList.count; count++)
    {
        status = USB_DeviceDeinitEndpoint(
          midiHandle->handle,
          midiHandle->interfaceHandle->endpointList.endpoint[count].endpointAddress);
    }
    midiHandle->interfaceHandle = NULL;

    return status;
}

/*!
 * @brief Handle the event passed to the midi class.
 *
 * This function handles the event passed to the midi class.
 *
 * @param handle          The audio class handle, got from the
 * usb_device_class_config_struct_t::classHandle.
 * @param event           The event codes. Please refer to the enumeration
 * usb_device_class_event_t.
 * @param param           The param type is determined by the event code.
 *
 * @return A USB error code or kStatus_USB_Success.
 * @retval kStatus_USB_Success              Free device handle successfully.
 * @retval kStatus_USB_InvalidParameter     The device handle not be found.
 * @retval kStatus_USB_InvalidRequest       The request is invalid, and the
 * control pipe will be stalled by the caller.
 */
usb_status_t USB_DeviceMidiEvent(void* handle, uint32_t event, void* param)
{
    usb_status_t error = kStatus_USB_Error;
    usb_device_midi_struct_t* midiHandle;
    uint32_t count;
    uint16_t interfaceAlternate;
    uint8_t* temp8;
    uint8_t alternate;
    usb_device_class_event_t eventCode = (usb_device_class_event_t)event;
    if ((NULL == param) || (NULL == handle))
    {
        return kStatus_USB_InvalidHandle;
    }

    midiHandle = (usb_device_midi_struct_t*)handle;

    switch (eventCode)
    {
        case kUSB_DeviceClassEventDeviceReset:
            /* Bus reset, clear the configuration. */
            midiHandle->configuration = 0;
            midiHandle->interfaceNumber = 0xFFU;
            midiHandle->interfaceHandle = NULL;
            midiHandle->bulkIn.isBusy = 0U;
            midiHandle->bulkOut.isBusy = 0U;
            error = kStatus_USB_Success;
            break;
        case kUSB_DeviceClassEventSetConfiguration:
            temp8 = ((uint8_t*)param);
            if (NULL == midiHandle->configStruct)
            {
                break;
            }
            if (*temp8 == midiHandle->configuration)
            {
                error = kStatus_USB_Success;
                break;
            }

            error = USB_DeviceMidiEndpointsDeinit(midiHandle);
            midiHandle->configuration = *temp8;
            midiHandle->alternate = 0U;
            error = USB_DeviceMidiEndpointsInit(midiHandle);
            if (kStatus_USB_Success != error)
            {
#if 0
            	(void)usb_echo("kUSB_DeviceClassEventSetConfiguration, USB_DeviceInitEndpoint fail\r\n");
#endif
            }
            break;
        case kUSB_DeviceClassEventSetInterface:
            if (NULL == midiHandle->configStruct)
            {
                break;
            }

            interfaceAlternate = *((uint16_t*)param);
            alternate = (uint8_t)(interfaceAlternate & 0xFFU);

            if (midiHandle->interfaceNumber != ((uint8_t)(interfaceAlternate >> 8U)))
            {
                break;
            }
            if (alternate == midiHandle->alternate)
            {
                error = kStatus_USB_Success;
                break;
            }
            error = USB_DeviceMidiEndpointsDeinit(midiHandle);
            midiHandle->alternate = alternate;
            error = USB_DeviceMidiEndpointsInit(midiHandle);
            if (kStatus_USB_Success != error)
            {
#if 0
            	(void)usb_echo("kUSB_DeviceClassEventSetInterface, USB_DeviceInitEndpoint fail\r\n");
#endif
            }
            break;
        case kUSB_DeviceClassEventSetEndpointHalt:
            if ((NULL == midiHandle->configStruct) || (NULL == midiHandle->interfaceHandle))
            {
                break;
            }
            temp8 = ((uint8_t*)param);
            for (count = 0; count < midiHandle->interfaceHandle->endpointList.count; count++)
            {
                if (*temp8 ==
                    midiHandle->interfaceHandle->endpointList.endpoint[count].endpointAddress)
                {
                    if (USB_IN == (((*temp8) & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) >>
                                   USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT))
                    {
                        midiHandle->bulkIn.pipeStall = 1U;
                    }
                    else
                    {
                        midiHandle->bulkOut.pipeStall = 1U;
                    }
                    error = USB_DeviceStallEndpoint(midiHandle->handle, *temp8);
                }
            }
            break;
        case kUSB_DeviceClassEventClearEndpointHalt:
            if ((NULL == midiHandle->configStruct) || (NULL == midiHandle->interfaceHandle))
            {
                break;
            }
            temp8 = ((uint8_t*)param);
            for (count = 0; count < midiHandle->interfaceHandle->endpointList.count; count++)
            {
                if (*temp8 ==
                    midiHandle->interfaceHandle->endpointList.endpoint[count].endpointAddress)
                {
                    error = USB_DeviceUnstallEndpoint(midiHandle->handle, *temp8);
                    if (USB_IN == (((*temp8) & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) >>
                                   USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT))
                    {
                        if (0U != midiHandle->bulkIn.pipeStall)
                        {
                            midiHandle->bulkIn.pipeStall = 0U;
                            if ((uint8_t*)USB_INVALID_TRANSFER_BUFFER !=
                                midiHandle->bulkIn.pipeDataBuffer)
                            {
                                error = USB_DeviceSendRequest(midiHandle->handle,
                                                              (midiHandle->bulkIn.ep),
                                                              midiHandle->bulkIn.pipeDataBuffer,
                                                              midiHandle->bulkIn.pipeDataLen);
                                if (kStatus_USB_Success != error)
                                {
                                    usb_device_endpoint_callback_message_struct_t
                                      endpointCallbackMessage;
                                    endpointCallbackMessage.buffer =
                                      midiHandle->bulkIn.pipeDataBuffer;
                                    endpointCallbackMessage.length = midiHandle->bulkIn.pipeDataLen;
                                    endpointCallbackMessage.isSetup = 0U;
#if (defined(USB_DEVICE_CONFIG_RETURN_VALUE_CHECK) && (USB_DEVICE_CONFIG_RETURN_VALUE_CHECK > 0U))
                                    if (kStatus_USB_Success !=
                                        USB_DeviceMidiBulkIn(midiHandle->handle,
                                                             (void*)&endpointCallbackMessage,
                                                             handle))
                                    {
                                        return kStatus_USB_Error;
                                    }
#else
                                    (void)USB_DeviceMidiBulkIn(
                                      midiHandle->handle, (void*)&endpointCallbackMessage, handle);
#endif
                                }
                                midiHandle->bulkIn.pipeDataBuffer =
                                  (uint8_t*)USB_INVALID_TRANSFER_BUFFER;
                                midiHandle->bulkIn.pipeDataLen = 0U;
                            }
                        }
                    }
                    else
                    {
                        if (0U != midiHandle->bulkOut.pipeStall)
                        {
                            midiHandle->bulkOut.pipeStall = 0U;
                            if ((uint8_t*)USB_INVALID_TRANSFER_BUFFER !=
                                midiHandle->bulkOut.pipeDataBuffer)
                            {
                                error = USB_DeviceRecvRequest(midiHandle->handle,
                                                              (midiHandle->bulkOut.ep),
                                                              midiHandle->bulkOut.pipeDataBuffer,
                                                              midiHandle->bulkOut.pipeDataLen);
                                if (kStatus_USB_Success != error)
                                {
                                    usb_device_endpoint_callback_message_struct_t
                                      endpointCallbackMessage;
                                    endpointCallbackMessage.buffer =
                                      midiHandle->bulkOut.pipeDataBuffer;
                                    endpointCallbackMessage.length =
                                      midiHandle->bulkOut.pipeDataLen;
                                    endpointCallbackMessage.isSetup = 0U;
#if (defined(USB_DEVICE_CONFIG_RETURN_VALUE_CHECK) && (USB_DEVICE_CONFIG_RETURN_VALUE_CHECK > 0U))
                                    if (kStatus_USB_Success !=
                                        USB_DeviceMidiBulkOut(midiHandle->handle,
                                                              (void*)&endpointCallbackMessage,
                                                              handle))
                                    {
                                        return kStatus_USB_Error;
                                    }
#else
                                    (void)USB_DeviceMidiBulkOut(
                                      midiHandle->handle, (void*)&endpointCallbackMessage, handle);
#endif
                                }
                                midiHandle->bulkOut.pipeDataBuffer =
                                  (uint8_t*)USB_INVALID_TRANSFER_BUFFER;
                                midiHandle->bulkOut.pipeDataLen = 0U;
                            }
                        }
                    }
                }
            }
            break;
        case kUSB_DeviceClassEventClassRequest:
        {
        }
        break;
        default:
            /*no action*/
            break;
    }
    return error;
}

/*!
 * @brief Initialize the audio class.
 *
 * This function is used to initialize the audio class.
 *
 * @param controllerId   The controller id of the USB IP. Please refer to the
 * enumeration usb_controller_index_t.
 * @param config          The class configuration information.
 * @param handle          It is out parameter, is used to return pointer of the
 * audio class handle to the caller.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceMidiInit(uint8_t controllerId,
                                usb_device_class_config_struct_t* config,
                                class_handle_t* handle)
{
    usb_status_t error = kStatus_USB_Error;
    usb_device_midi_struct_t* midiHandle;

    error = USB_DeviceMidiAllocateHandle(&midiHandle);

    if (kStatus_USB_Success != error)
    {
        return error;
    }

    error = USB_DeviceClassGetDeviceHandle(controllerId, &midiHandle->handle);

    if (kStatus_USB_Success != error)
    {
        return error;
    }

    if (NULL == midiHandle->handle)
    {
        return kStatus_USB_InvalidHandle;
    }

    midiHandle->configStruct = config;
    midiHandle->configuration = 0;
    midiHandle->alternate = 0xFF;
    midiHandle->interfaceNumber = 0xFFU;
    midiHandle->interfaceHandle = NULL;

    midiHandle->bulkIn.mutex = (osa_mutex_handle_t)&midiHandle->bulkIn.mutexBuffer[0];
    if (KOSA_StatusSuccess != OSA_MutexCreate((midiHandle->bulkIn.mutex)))
    {
    }
    midiHandle->bulkOut.mutex = (osa_mutex_handle_t)&midiHandle->bulkOut.mutexBuffer[0];
    if (KOSA_StatusSuccess != OSA_MutexCreate((midiHandle->bulkOut.mutex)))
    {
    }
    *handle = (class_handle_t)midiHandle;

    return error;
}

/*!
 * @brief De-initialize the device midi class.
 *
 * The function de-initializes the device midi class.
 *
 * @param handle The ccid class handle got from
 * usb_device_class_config_struct_t::classHandle.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceMidiDeinit(class_handle_t handle)
{
    usb_status_t error = kStatus_USB_Error;

    return error;
}

/*!
 * @brief Prime the endpoint to send packet to host.
 *
 * This function checks whether the endpoint is sending packet, then it primes
 * the endpoint with the buffer address and the buffer length if the pipe is not
 * busy. Otherwise, it ignores this transfer by returning an error code.
 *
 * @param handle The class handle of the MIDI class.
 * @param ep The endpoint number of the transfer.
 * @param buffer The pointer to the buffer to be transferred.
 * @param length The length of the buffer to be transferred.
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceMidiSend(class_handle_t handle, uint8_t ep, uint8_t* buffer, uint32_t length)
{
    usb_device_midi_struct_t* midiHandle;
    usb_status_t status = kStatus_USB_Error;
    usb_device_midi_pipe_t* midiPipe = NULL;

    if (NULL == handle)
    {
        return kStatus_USB_InvalidHandle;
    }
    midiHandle = (usb_device_midi_struct_t*)handle;

    if (midiHandle->bulkIn.ep == ep)
    {
        midiPipe = &(midiHandle->bulkIn);
    }
    else
    {
        /*no action*/
    }

    if (NULL != midiPipe)
    {
        if (1U == midiPipe->isBusy)
        {
            return kStatus_USB_Busy;
        }
        midiPipe->isBusy = 1U;

        if (0u != midiPipe->pipeStall)
        {
            midiPipe->pipeDataBuffer = buffer;
            midiPipe->pipeDataLen = length;
            return kStatus_USB_Success;
        }

        status = USB_DeviceSendRequest(midiHandle->handle, ep, buffer, length);
        if (kStatus_USB_Success != status)
        {
            midiPipe->isBusy = 0U;
        }
    }
    return status;
}

/*!
 * @brief Prime the endpoint to receive packet from host.
 *
 * This function checks whether the endpoint is receiving packet, then it primes
 * the endpoint with the buffer address and the buffer length if the pipe is not
 * busy. Otherwise, it ignores this transfer by returning an error code.
 *
 * @param handle The class handle of the MIDI class.
 * @param ep The endpoint number of the transfer.
 * @param buffer The pointer to the buffer to be transferred.
 * @param length The length of the buffer to be transferred.
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceMidiRecv(class_handle_t handle, uint8_t ep, uint8_t* buffer, uint32_t length)
{
    usb_device_midi_struct_t* midiHandle;
    usb_status_t status;

    if (NULL == handle)
    {
        return kStatus_USB_InvalidHandle;
    }
    midiHandle = (usb_device_midi_struct_t*)handle;

    if (1U == midiHandle->bulkOut.isBusy)
    {
        return kStatus_USB_Busy;
    }
    midiHandle->bulkOut.isBusy = 1U;

    if (0U != midiHandle->bulkOut.pipeStall)
    {
        midiHandle->bulkOut.pipeDataBuffer = buffer;
        midiHandle->bulkOut.pipeDataLen = length;
        return kStatus_USB_Success;
    }

    status = USB_DeviceRecvRequest(midiHandle->handle, ep, buffer, length);
    if (kStatus_USB_Success != status)
    {
        midiHandle->bulkOut.isBusy = 0U;
    }
    return status;
}

#endif
