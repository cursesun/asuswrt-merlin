/*  *********************************************************************
    *  Broadcom Common Firmware Environment (CFE)
    *  
    *  Board device initialization		File: pt1125_devs.c
    *  
    *  This is the "C" part of the board support package.  The
    *  routines to create and initialize the console, wire up 
    *  device drivers, and do other customization live here.
    *  
    *  Author:  Mitch Lichtenberg (mpl@broadcom.com)
    *
    *  modification history
    *  --------------------
    *  01a,01aug02,gtb  derived from pt1125_devs.c  
    *                           
    *                     
    *  
    *********************************************************************  
    *
    *  Copyright 2000,2001
    *  Broadcom Corporation. All rights reserved.
    *  
    *  This software is furnished under license and may be used and 
    *  copied only in accordance with the following terms and 
    *  conditions.  Subject to these conditions, you may download, 
    *  copy, install, use, modify and distribute modified or unmodified 
    *  copies of this software in source and/or binary form.  No title 
    *  or ownership is transferred hereby.
    *  
    *  1) Any source code used, modified or distributed must reproduce 
    *     and retain this copyright notice and list of conditions as 
    *     they appear in the source file.
    *  
    *  2) No right is granted to use any trade name, trademark, or 
    *     logo of Broadcom Corporation. Neither the "Broadcom 
    *     Corporation" name nor any trademark or logo of Broadcom 
    *     Corporation may be used to endorse or promote products 
    *     derived from this software without the prior written 
    *     permission of Broadcom Corporation.
    *  
    *  3) THIS SOFTWARE IS PROVIDED "AS-IS" AND ANY EXPRESS OR
    *     IMPLIED WARRANTIES, INCLUDING BUT NOT LIMITED TO, ANY IMPLIED 
    *     WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR 
    *     PURPOSE, OR NON-INFRINGEMENT ARE DISCLAIMED. IN NO EVENT 
    *     SHALL BROADCOM BE LIABLE FOR ANY DAMAGES WHATSOEVER, AND IN 
    *     PARTICULAR, BROADCOM SHALL NOT BE LIABLE FOR DIRECT, INDIRECT, 
    *     INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
    *     (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE 
    *     GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
    *     BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY 
    *     OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR 
    *     TORT (INCLUDING NEGLIGENCE OR OTHERWISE), EVEN IF ADVISED OF 
    *     THE POSSIBILITY OF SUCH DAMAGE.
    ********************************************************************* */



#include "sbmips.h"
#include "lib_types.h"
#include "lib_queue.h"
#include "lib_printf.h"
#include "lib_string.h"
#include "cfe_iocb.h"
#include "cfe_device.h"
#include "cfe_timer.h"
#include "dev_ptflash.h"
#include "env_subr.h"
#include "cfe.h"

#include "sb1250_defs.h"
#include "sb1250_regs.h"
#include "sb1250_pci.h"
#include "sb1250_ldt.h"
#include "sb1250_scd.h"
#include "sb1250_wid.h"
#include "sb1250_smbus.h"

#include "bsp_config.h"

#include "pt1125.h"
#include "dev_ide.h"

/*  *********************************************************************
    *  Devices we're importing
    ********************************************************************* */
extern cfe_driver_t ns16550_uart;		/* external UART on the I/O bus */
extern cfe_driver_t promice_uart;		/* promice serial port */
extern cfe_driver_t sb1250_uart;		/* SB1250 serial ports */
extern cfe_driver_t sb1250_ether;		/* SB1250 MACs */
extern cfe_driver_t sb1250_x1240eeprom;		/* Xicor SMBus NVRAM */
extern cfe_driver_t sb1250_24lc128eeprom;	/* Microchip EEPROM */
extern cfe_driver_t ptflashdrv;			/* AMD-style flash */
extern cfe_driver_t ptflashdrv_ro;		/* AMD-style flash */

extern cfe_driver_t m41t81_clock;		/* M41T81 SMBus RTC */


extern void ui_init_cpu1cmds(void);
extern void ui_init_swarmcmds(void);
extern int ui_init_corecmds(void);
extern int ui_init_soccmds(void);
extern int ui_init_testcmds(void);
extern int ui_init_resetcmds(void);
extern int ui_init_tempsensorcmds(void);
extern int ui_init_toyclockcmds(void);
extern void cs0_remap(void);
extern void cs1_remap(void);

extern void sb1250_show_cpu_type(void);

/*  *********************************************************************
    *  Forward declarations
    ********************************************************************* */
static void bootrom_add(unsigned int addr);
static void alt_bootrom_add(unsigned int addr);

/*  *********************************************************************
    *  Some board-specific parameters
    ********************************************************************* */

/*
 * Note!  Configure the PROMICE for burst mode zero (one byte per
 * access).
 */

#define PROMICE_BASE	(0x1FDFFC00)
#define PROMICE_WORDSIZE 1

/*  *********************************************************************
    *  SysConfig switch settings
    ********************************************************************* */

#define PT1125_PROMICE_CONSOLE		0x00000001
#define PT1125_EXTERNAL_UART_CONSOLE	0x00000002
#define PT1125_INTERNAL_UART_CONSOLE	0x00000004
#define PT1125_PROMICE_BOOT		0x00000008

/*
* There are 8 possible configurations of CFE.  They are redundant -- so
* customize as you like.  The default configuration of '7' is set by the
* FPGA as of when the file was created.  The FPGA sets the six-bit config 
* field of the * system config register 0x39.  CFE uses the upper 3 bits,
* which are all set, giving array index 7.
*/
const unsigned int pt1125_startflags[8] = {
    PT1125_EXTERNAL_UART_CONSOLE,	/* 0 : External UART console */
    PT1125_PROMICE_CONSOLE,		/* 1 : PromICE console */
    PT1125_INTERNAL_UART_CONSOLE,	/* 2 : Internal UART console */
    PT1125_EXTERNAL_UART_CONSOLE,	/* 3 : External UART console */
    CFE_INIT_PCI  | CFE_LDT_SLAVE | PT1125_EXTERNAL_UART_CONSOLE, /* 4:ext UART,PCI.LDT slave*/
    CFE_INIT_PCI  | PT1125_EXTERNAL_UART_CONSOLE, /* 5:ext UART,PCI*/
    CFE_INIT_PCI  | PT1125_EXTERNAL_UART_CONSOLE | PT1125_PROMICE_BOOT, /* 6 */
    CFE_INIT_SAFE |  PT1125_EXTERNAL_UART_CONSOLE /* 7 : External UART console, safe mode */
};



/*  *********************************************************************
    *  board_console_init()
    *  
    *  Add the console device and set it to be the primary
    *  console.
    *  
    *  Input parameters: 
    *  	   nothing
    *  	   
    *  Return value:
    *  	   nothing
    ********************************************************************* */

void board_console_init(void)
{
    uint64_t syscfg;
    unsigned temp;
    int plldiv;

    syscfg = SBREADCSR(A_SCD_SYSTEM_CFG);

    /* 
    * External UART is device "uart0". On the PT1125 it's the connector
    * nearest the motherboard.
    */

    cfe_add_device(&ns16550_uart,UART_PHYS,0,0);

    /* Internal UART0 is device "uart1"  */

    cfe_add_device(&sb1250_uart,A_DUART,0,0);

    /* Add the PromICE UART, device "promice0", for good measure */

    cfe_add_device(&promice_uart,PROMICE_BASE,PROMICE_WORDSIZE,0);

    /*
     * Get the CFE startflags from the upper 3 bits of the "config" field
     * in the sysconfig register.  Only 3 bits are used, because that's
     * what the CSWARM does.
     */

    cfe_startflags = pt1125_startflags[(G_SYS_CONFIG(syscfg)/8) & 0x07];

    if (cfe_startflags & PT1125_PROMICE_CONSOLE) {
	cfe_set_console("promice0");
	}
    else if (cfe_startflags & PT1125_INTERNAL_UART_CONSOLE) {
	cfe_set_console("uart1");
        }
    else {
	cfe_set_console("uart0");
	}

    /*
     * Set variable that contains CPU speed, spit out config register
     */

    plldiv = G_SYS_PLL_DIV(syscfg);

    cfe_cpu_speed = 50000000 * plldiv;		

    /* 
     * NVRAM (environment variables
     */
  
    cfe_add_device(&sb1250_24lc128eeprom,BIGEEPROM_SMBUS_CHAN,BIGEEPROM_SMBUS_DEV,0);
    
    
    /*
     * Turn off the safe flag so the ethernet MAC addresses will get read
     * from NVRAM 
     */
    temp = cfe_startflags;
    cfe_startflags &= ~CFE_INIT_SAFE;

    cfe_set_envdevice("eeprom0");	/* Connect NVRAM subsystem to EEPROM */

    /* restore SAFE flag if it was set */
    cfe_startflags = temp;
}



/*  *********************************************************************
    *  bootrom_add()
    *  
    *  Add the physical flash device to the system
    *  
    *  Input parameters: 
    *  	   nothing
    *  	   
    *  Return value:
    *  	   nothing
    ********************************************************************* */

void bootrom_add(unsigned int addr) 
{
#ifndef OLD_FLASHDRV
    flash_probe_t flashdesc;
    memset(&flashdesc,0,sizeof(flash_probe_t));
    flashdesc.flash_phys = addr;
    flashdesc.flash_size = BOOTROM_SIZE*K64;
    flashdesc.flash_flags = FLASH_FLG_WIDE;
    flashdesc.nchips = BOOTROM_NCHIPS;
    flashdesc.chipsize = BOOTROM_CHIPSIZE;
    cfe_add_device(&ptflashdrv,NULL,NULL,&flashdesc);
    memset(&flashdesc,0,sizeof(flash_probe_t));
    flashdesc.flash_phys = addr;
    flashdesc.flash_size = BOOTROM_SIZE*K64;
    flashdesc.flash_flags = FLASH_FLG_WIDE | FLASH_FLG_MANUAL;
    flashdesc.flash_cmdset = 0x12345678;
    cfe_add_device(&ptflashdrv_ro,NULL,NULL,&flashdesc);
#else
    cfe_add_device(&ptflashdrv,BOOTROM_PHYS,BOOTROM_SIZE*K64,NULL);
#endif
}
/*  *********************************************************************
    *  alt_bootrom_add()
    *  
    *  Add the promice device to the system as flash
    *  
    *  Input parameters: 
    *  	   nothing
    *  	   
    *  Return value:
    *  	   nothing
    ********************************************************************* */

void alt_bootrom_add(unsigned int addr)
{
#ifndef OLD_FLASHDRV
    flash_probe_t flashdesc;
    /* When CS0 is jumped to PromICE, this will be flash */
    memset(&flashdesc,0,sizeof(flash_probe_t));
    flashdesc.flash_phys = addr;
    flashdesc.flash_size = ALT_BOOTROM_SIZE*K64;
    flashdesc.flash_flags = FLASH_FLG_WIDE;
    flashdesc.nchips = ALT_BOOTROM_NCHIPS;
    flashdesc.chipsize = ALT_BOOTROM_CHIPSIZE;
    cfe_add_device(&ptflashdrv,NULL,NULL,&flashdesc);
#else
    cfe_add_device(&ptflashdrv,ALT_BOOTROM_PHYS,ALT_BOOTROM_SIZE*K64,NULL);
#endif
}


/*  *********************************************************************
    *  board_device_init()
    *  
    *  Initialize and add other devices.  Add everything you need
    *  for bootstrap here, like disk drives, flash memory, UARTs,
    *  network controllers, etc.
    *  
    *  Input parameters: 
    *  	   nothing
    *  	   
    *  Return value:
    *  	   nothing
    ********************************************************************* */

void board_device_init(void)
{
    uint64_t syscfg;
    int promice_boot;

    /* 
     * Boot ROM 
     */

    syscfg = SBREADCSR(A_SCD_SYSTEM_CFG);
    promice_boot = pt1125_startflags[(G_SYS_CONFIG(syscfg)/8) & 0x07] & \
        PT1125_PROMICE_BOOT;

    /*
    *  Don't remap chip select 0 when using PromICE, since
    *  PromICE only emulates 2 Meg and we set ALT_BOOTROM
    *  to 2 Meg
    */
    if (!promice_boot)
        {
        cs0_remap();   /* Expand CS0 -- this is bootrom and flash */
        bootrom_add(BOOTROM_PHYS);
        alt_bootrom_add(ALT_BOOTROM_PHYS);
        }
    else
        {
        cs1_remap();   /* Expand CS1 -- it's all flash */
        alt_bootrom_add(BOOTROM_PHYS);
        bootrom_add(ALT_BOOTROM_PHYS);
        }
 
    cfe_add_device(&m41t81_clock,M41T81_SMBUS_CHAN,M41T81_SMBUS_DEV,0);
    
    /* 
     * MACs - must init after environment, since the hw address is stored there 
     */
    cfe_add_device(&sb1250_ether,A_MAC_BASE_0,0,env_getenv("ETH0_HWADDR"));
#ifndef _PT1125_DIAG_CFG_
    cfe_add_device(&sb1250_ether,A_MAC_BASE_1,1,env_getenv("ETH1_HWADDR"));
#endif



    /*
     * Set variable that contains CPU speed, spit out config register
     */

    syscfg = SBREADCSR(A_SCD_SYSTEM_CFG);
    printf("Config switch: %d\n",G_SYS_CONFIG(syscfg));

    /*
     * Display CPU status
     */

    sb1250_show_cpu_type();

}



/*  *********************************************************************
    *  board_device_reset()
    *  
    *  Reset devices.  This call is done when the firmware is restarted,
    *  as might happen when an operating system exits, just before the
    *  "reset" command is applied to the installed devices.   You can
    *  do whatever board-specific things are here to keep the system
    *  stable, like stopping DMA sources, interrupts, etc.
    *  
    *  Input parameters: 
    *  	   nothing
    *  	   
    *  Return value:
    *  	   nothing
    ********************************************************************* */

void board_device_reset(void)
{

}


/*  *********************************************************************
    *  board_final_init()
    *  
    *  Do any final initialization, such as adding commands to the
    *  user interface.
    *
    *  If you don't want a user interface, put the startup code here.  
    *  This routine is called just before CFE starts its user interface.
    *  
    *  Input parameters: 
    *  	   nothing
    *  	   
    *  Return value:
    *  	   nothing
    ********************************************************************* */

void board_final_init(void)
{
    ui_init_cpu1cmds();
    ui_init_swarmcmds();
    ui_init_corecmds();
    ui_init_soccmds();
    ui_init_testcmds();
    ui_init_resetcmds();
    ui_init_tempsensorcmds();
    ui_init_toyclockcmds();
}
