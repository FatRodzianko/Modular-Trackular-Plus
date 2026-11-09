#include <UsbImpl.h>

extern "C" void USB_Init();
extern "C" void USB_Task();
extern "C" void USB_DeviceReset();
extern "C" uint8_t USB_DeviceVcomClear();

void UsbImpl::init()
{
    USB_Init();
}

void UsbImpl::reset(void)
{
    g_UsbState.isResetRequested = true;
}

void UsbImpl::update(void)
{
    USB_Task();

    if (g_UsbState.isResetRequested)
    {
        // we want VCOM to be disconnected
        if (USB_DeviceVcomClear() != 0)
        {
            g_UsbState.isResetRequested = false;
            USB_DeviceReset();
        }
    }
}

void UsbImpl::setClassState(Usb::ClassType type, bool enable)
{
    switch (type)
    {
        case Usb::ClassType::kMidi:
            g_UsbState.isMidiEnabled = enable;
            break;
        case Usb::ClassType::kAudioIn:
            // Tracker audio input = USB OUT endpoint (Host -> Device, 2ch)
            g_UsbState.isAudioOutEnabled = enable;
            break;
        case Usb::ClassType::kAudioOut:
            // Tracker audio output = USB IN endpoint (Device -> Host, 28ch)
            g_UsbState.isAudioInEnabled = enable;
            break;
        case Usb::ClassType::kStorage:
            g_UsbState.isStorageEnabled = enable;
            break;
        case Usb::ClassType::kSerial:
            g_UsbState.isSerialEnabled = enable;
            break;
    }
}

bool UsbImpl::getClassState(Usb::ClassType type)
{
    bool ret = false;

    switch (type)
    {
        case Usb::ClassType::kMidi:
            ret = g_UsbState.isMidiEnabled;
            break;
        case Usb::ClassType::kAudioIn:
            ret = g_UsbState.isAudioOutEnabled;
            break;
        case Usb::ClassType::kAudioOut:
            ret = g_UsbState.isAudioInEnabled;
            break;
        case Usb::ClassType::kStorage:
            ret = g_UsbState.isStorageEnabled;
            break;
        case Usb::ClassType::kSerial:
            ret = g_UsbState.isSerialEnabled;
            break;
    }

    return ret;
}

extern "C" usb_device_composite_struct_t g_composite;

bool UsbImpl::isAttached()
{
    return g_composite.attach;
}
