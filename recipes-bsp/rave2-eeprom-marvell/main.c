// Copyright © Advanced Micro Devices, Inc., or its affiliates. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: KC
// Maintainer: KC

#include <Amethyst.h>

#define PORT_STATUS_REG         0x00
#define PORT_STATUS_CMODE_MASK  0x000F
#define PORT_STATUS_CMODE_SHIFT 0

int main(void)
{
    unsigned short v;

    /* Port 9 -> C_Mode 0xD */
    v = ReadReg((unsigned char)9, (unsigned char)PORT_STATUS_REG);
    v = (unsigned short)((v & (unsigned short)~PORT_STATUS_CMODE_MASK) |
                         (unsigned short)((0x0D << PORT_STATUS_CMODE_SHIFT) & PORT_STATUS_CMODE_MASK));
    WriteReg((unsigned char)9, (unsigned char)PORT_STATUS_REG, v);

    /* Port 10 -> C_Mode 0xE */
    v = ReadReg((unsigned char)10, (unsigned char)PORT_STATUS_REG);
    v = (unsigned short)((v & (unsigned short)~PORT_STATUS_CMODE_MASK) |
                         (unsigned short)((0x0E << PORT_STATUS_CMODE_SHIFT) & PORT_STATUS_CMODE_MASK));
    WriteReg((unsigned char)10, (unsigned char)PORT_STATUS_REG, v);

    return 0;
}
