#include "mtLED.h"

#include "i2cBusControllerNXP.h"

#include "wiring.h"

#include "usb_serial.h"

#include <stdint.h>
#include <string.h>

#ifndef _swap_int16_t
#define _swap_int16_t(a, b)                                                                        \
    {                                                                                              \
        int16_t t = a;                                                                             \
        a = b;                                                                                     \
        b = t;                                                                                     \
    }
#endif

const uint8_t gamma255[256] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
    1,   1,   1,   2,   2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   3,   3,   3,   4,
    4,   4,   4,   4,   5,   5,   5,   5,   6,   6,   6,   6,   7,   7,   7,   7,   8,   8,   8,
    9,   9,   9,   10,  10,  10,  11,  11,  11,  12,  12,  13,  13,  13,  14,  14,  15,  15,  16,
    16,  17,  17,  18,  18,  19,  19,  20,  20,  21,  21,  22,  22,  23,  24,  24,  25,  25,  26,
    27,  27,  28,  29,  29,  30,  31,  32,  32,  33,  34,  35,  35,  36,  37,  38,  39,  39,  40,
    41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  50,  51,  52,  54,  55,  56,  57,  58,  59,
    60,  61,  62,  63,  64,  66,  67,  68,  69,  70,  72,  73,  74,  75,  77,  78,  79,  81,  82,
    83,  85,  86,  87,  89,  90,  92,  93,  95,  96,  98,  99,  101, 102, 104, 105, 107, 109, 110,
    112, 114, 115, 117, 119, 120, 122, 124, 126, 127, 129, 131, 133, 135, 137, 138, 140, 142, 144,
    146, 148, 150, 152, 154, 156, 158, 160, 162, 164, 167, 169, 171, 173, 175, 177, 180, 182, 184,
    186, 189, 191, 193, 196, 198, 200, 203, 205, 208, 210, 213, 215, 218, 220, 223, 225, 228, 231,
    233, 236, 239, 241, 244, 247, 249, 252, 255

};

mtLED::mtLED()
{
    memset(ledState, 0, 144);
    memset(ledBits, 0, 18);
    memset(ledPWM, 0, 144);
}

void mtLED::begin(void (*resetFunc)(void), uint8_t addr)
{
    _i2caddr = addr;
    _frame = 0;
    resetTransmissionFunc = resetFunc;

    // shutdown
    writeRegister8(ISSI_BANK_FUNCTIONREG, ISSI_REG_SHUTDOWN, 0x00);

    delay(10);

    // out of shutdown
    writeRegister8(ISSI_BANK_FUNCTIONREG, ISSI_REG_SHUTDOWN, 0x01);

    // picture mode
    writeRegister8(ISSI_BANK_FUNCTIONREG, ISSI_REG_CONFIG, ISSI_REG_CONFIG_PICTUREMODE);

    displayFrame(_frame);

    // all LEDs on & 0 PWM
    setLEDOnOffBytes(ledBits, _frame);

    clear(); // set each led to 0 PWM

    audioSync(false);
}

uint8_t mtLED::update_all_leds()
{
    if (leds_need_update)
    {
        checkAndRepair();

        if (!i2cBusControllerNXP2.startTransmission(_i2caddr))
            return 1;

        i2cBusControllerNXP2.sendByte((uint8_t)ISSI_COMMANDREGISTER);
        i2cBusControllerNXP2.sendByte(_frame);
        I2cBusControllerImpl2::OperationResult status = i2cBusControllerNXP2.finishTransmission();
        if (status == I2cBusControllerImpl2::OperationResult::kError)
        {
            isDeadResetCheck();
            return 1;
        }

        i2cBusControllerNXP2.startTransmission(_i2caddr);
        i2cBusControllerNXP2.sendByte((uint8_t)0x00);

        for (uint8_t i = 0; i < 18; i++) // 0x11
        {
            i2cBusControllerNXP2.sendByte(ledBits[i]);
        }

        for (uint8_t i = 0; i < 18; i++) // 0x11
        {
            i2cBusControllerNXP2.sendByte(0);
        }

        for (uint8_t i = 0; i < 144; i++)
        {
            i2cBusControllerNXP2.sendByte((uint8_t)ledPWM[i]);
        }

        i2cBusControllerNXP2.sendTransmission();
        leds_need_update = 0;

        return 1;
    }

    return 0;
}

void mtLED::update_pads()
{
}

void mtLED::setLED(uint8_t num, uint8_t state, uint8_t gamma_pwm)
{

    if (gamma_pwm > 31)
        gamma_pwm = 31;

    num += 4 * (num / 12); // special for tracker 12 x 4 grid

    ledPWM[num] = gamma32[gamma_pwm];

    if (state)
        ledBits[num / 8] |= (1 << num % 8);
    else
        ledBits[num / 8] &= ~(1 << num % 8);
    leds_need_update = 1;
}

void mtLED::setLEDpwm(uint8_t num, uint8_t state, uint8_t pwm)
{
    ledPWM[num] = pwm;
    if (state)
        ledBits[num / 8] |= (1 << num % 8);
    else
        ledBits[num / 8] &= ~(1 << num % 8);
    leds_need_update = 1;
}

void mtLED::clearAllPadsLED()
{
    for (uint8_t led = 0; led < 64; led++)
    {
        ledPWM[medusaPadsLeds[led]] = 0;
        ledBits[medusaPadsLeds[led] / 8] &= ~(1 << medusaPadsLeds[led] % 8);
    }
    leds_need_update = 1;
}

void mtLED::clear(void)
{
    selectBank(_frame);

    for (uint8_t i = 0; i < 6; i++)
    {
        i2cBusControllerNXP2.startTransmission(_i2caddr);
        i2cBusControllerNXP2.sendByte((uint8_t)0x24 + i * 24);

        for (uint8_t j = 0; j < 18; j++)
        {
            i2cBusControllerNXP2.sendByte((uint8_t)0);
        }
        i2cBusControllerNXP2.finishTransmission();
    }
}

void mtLED::setLEDPWM(uint8_t lednum, uint8_t pwm, uint8_t bank)
{
    if (lednum >= 144)
        return;
    writeRegister8(bank, 0x24 + lednum, pwm);
}

void mtLED::fastSetLEDPWM(uint8_t lednum, uint8_t pwm, uint8_t bank)
{
    if (lednum >= 144)
        return;

    i2cBusControllerNXP2.startTransmission(_i2caddr);
    i2cBusControllerNXP2.sendByte((uint8_t)ISSI_COMMANDREGISTER);
    i2cBusControllerNXP2.sendByte(bank);
    i2cBusControllerNXP2.finishTransmission();

    i2cBusControllerNXP2.startTransmission(_i2caddr);
    i2cBusControllerNXP2.sendByte((uint8_t)0x24 + lednum);
    i2cBusControllerNXP2.sendByte((uint8_t)pwm);
    i2cBusControllerNXP2.finishTransmission();
}

void mtLED::setAllLEDPWM(uint8_t* arr, uint8_t bank)
{
    i2cBusControllerNXP2.startTransmission(_i2caddr);
    i2cBusControllerNXP2.sendByte((uint8_t)ISSI_COMMANDREGISTER);
    i2cBusControllerNXP2.sendByte((uint8_t)bank);
    i2cBusControllerNXP2.finishTransmission();

    i2cBusControllerNXP2.startTransmission(_i2caddr);
    i2cBusControllerNXP2.sendByte((uint8_t)0x24);
    for (uint8_t a = 0; a <= 143; a++)
    {
        i2cBusControllerNXP2.sendByte((uint8_t)arr[a]);
    }
    i2cBusControllerNXP2.finishTransmission();
}

void mtLED::setLEDOnOff2Bytes(uint16_t* arr, uint8_t bank)
{
    i2cBusControllerNXP2.startTransmission(_i2caddr);
    i2cBusControllerNXP2.sendByte((uint8_t)ISSI_COMMANDREGISTER);
    i2cBusControllerNXP2.sendByte((uint8_t)bank);
    i2cBusControllerNXP2.finishTransmission();

    i2cBusControllerNXP2.startTransmission(_i2caddr);
    i2cBusControllerNXP2.sendByte((uint8_t)0x00);
    for (uint8_t a = 0; a <= 9; a++)
    {
        i2cBusControllerNXP2.sendByte((uint8_t)arr[a]);
        i2cBusControllerNXP2.sendByte((uint8_t)(arr[a] >> 8));
    }

    i2cBusControllerNXP2.finishTransmission();
}

void mtLED::setLEDOnOffBytes(uint8_t* arr, uint8_t bank)
{
    i2cBusControllerNXP2.startTransmission(_i2caddr);
    i2cBusControllerNXP2.sendByte((uint8_t)ISSI_COMMANDREGISTER);
    i2cBusControllerNXP2.sendByte((uint8_t)bank);
    i2cBusControllerNXP2.finishTransmission();

    i2cBusControllerNXP2.startTransmission(_i2caddr);
    i2cBusControllerNXP2.sendByte((uint8_t)0x00);
    for (uint8_t i = 0; i < 18; i++) // 0x11
    {
        i2cBusControllerNXP2.sendByte(arr[i]);
    }
    i2cBusControllerNXP2.finishTransmission();
}
void mtLED::setFrame(uint8_t f)
{
    _frame = f;
}

void mtLED::displayFrame(uint8_t f)
{
    if (f > 7)
        f = 0;
    writeRegister8(ISSI_BANK_FUNCTIONREG, ISSI_REG_PICTUREFRAME, f);
}

void mtLED::selectBank(uint8_t b)
{

    i2cBusControllerNXP2.startTransmission(_i2caddr);
    i2cBusControllerNXP2.sendByte((uint8_t)ISSI_COMMANDREGISTER);
    i2cBusControllerNXP2.sendByte((uint8_t)b);
    i2cBusControllerNXP2.finishTransmission();
}

void mtLED::audioSync(bool sync)
{
    if (sync)
    {
        writeRegister8(ISSI_BANK_FUNCTIONREG, ISSI_REG_AUDIOSYNC, 0x1);
    }
    else
    {
        writeRegister8(ISSI_BANK_FUNCTIONREG, ISSI_REG_AUDIOSYNC, 0x0);
    }
}

void mtLED::writeRegister8(uint8_t b, uint8_t reg, uint8_t data)
{

    if (!i2cBusControllerNXP2.startTransmission(_i2caddr))
        return;
    i2cBusControllerNXP2.sendByte((uint8_t)ISSI_COMMANDREGISTER);
    i2cBusControllerNXP2.sendByte((uint8_t)b);
    i2cBusControllerNXP2.finishTransmission();

    i2cBusControllerNXP2.startTransmission(_i2caddr);
    i2cBusControllerNXP2.sendByte((uint8_t)reg);
    i2cBusControllerNXP2.sendByte((uint8_t)data);
    i2cBusControllerNXP2.finishTransmission();

    if (b == ISSI_BANK_FUNCTIONREG)
    {
        functionRegBuff[reg] = data;
    }
}

uint8_t mtLED::readRegister8(uint8_t bank, uint8_t reg)
{
    uint8_t x;
    x = 0;

    if (!i2cBusControllerNXP2.startTransmission(_i2caddr))
        return 0;
    i2cBusControllerNXP2.sendByte((uint8_t)ISSI_COMMANDREGISTER);
    i2cBusControllerNXP2.sendByte((uint8_t)bank);
    i2cBusControllerNXP2.finishTransmission();

    i2cBusControllerNXP2.startTransmission(_i2caddr);
    i2cBusControllerNXP2.sendByte((uint8_t)reg);
    i2cBusControllerNXP2.finishTransmission();

    i2cBusControllerNXP2.request(_i2caddr, uint8_t(1));

    while (i2cBusControllerNXP2.isDataToReceive())
    {
        x = i2cBusControllerNXP2.read();
    }
    i2cBusControllerNXP2.finishTransmission();

    return x;
}

void mtLED::print_i2c_status(void)
{
}

void mtLED::debug(void)
{
    _debug = !_debug;
}

uint8_t mtLED::get_g32(uint8_t g)
{
    if (g <= 31)
    {
        return gamma32[g];
    }
    if (g > 31)
    {
        return gamma32[31];
    }
    else
    {
        return 0;
    }
}

bool mtLED::isDeadResetCheck(void)
{
    bool x;
    if (readRegister8(ISSI_BANK_FUNCTIONREG, ISSI_REG_SHUTDOWN) == 1)
    {
        x = 0;
    }
    else
    {
        if (resetTransmissionFunc != nullptr)
            resetTransmissionFunc();
        if (readRegister8(ISSI_BANK_FUNCTIONREG, ISSI_REG_CONFIG) != ISSI_REG_CONFIG_PICTUREMODE)
        {
            begin(resetTransmissionFunc, _i2caddr);
        }
    }

    return x;
}

void mtLED::dropRegInfo(void)
{
    for (uint8_t reg = 0x00; reg <= 0x0C; reg++)
    {
        uint8_t tempData = readRegister8(ISSI_BANK_FUNCTIONREG, reg);

        Serial.print("reg: ");
        Serial.println(reg, HEX);
        Serial.println(tempData);

        if (tempData == functionRegBuff[reg])
            Serial.println("match ok");
        else
        {
            Serial.println("missmatch, trying repair...");
            checkAndRepairReg(reg);
        }
        Serial.println();
    }
}

bool mtLED::checkAndRepairReg(uint8_t reg)
{
    if (readRegister8(ISSI_BANK_FUNCTIONREG, reg) != functionRegBuff[reg])
    {
        writeRegister8(ISSI_BANK_FUNCTIONREG, reg, functionRegBuff[reg]);
        return true;
    }

    return false;
}

void mtLED::checkAndRepair(void)
{

    for (uint8_t reg = 0; reg <= 0x0D; reg++)
    {
        if (reg == 0x07)
            continue;

        checkAndRepairReg(reg);
    }
}
