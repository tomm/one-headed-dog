#include <Arduino.h>
#include "fabgl.h"
#include "fabglconf.h"
#include "fabutils.h"
#include <dirent.h>
#include "cerberus.h"
#include <string.h>

#define DEBUG 1

void debug_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
int load(String filename, unsigned int startAddr);

fabgl::PS2Controller PS2Controller;
fabgl::VGADirectController VGAController;
constexpr int scanlinesPerCallback = 2;
static TaskHandle_t mainTaskHandle = NULL;
#define SDCARD_MOUNT_PATH "/SD"

/** Next is the string in CAT's internal memory containing the edit line, **/
/** intialized in startup.                              **/
volatile char editLine[38];
volatile char previousEditLine[38];
volatile uint16_t bytesRead;

/** The above is self-explanatory: it allows for repeating previous command **/
volatile byte pos = 1;						/** Position in edit line currently occupied by cursor **/
volatile bool mode = false;	/** false = 6502 mode, true = Z80 mode**/
volatile bool cpurunning = false;			/** true = CPU is running, CAT should not use the buses **/
volatile bool interruptFlag = false;		/** true = Triggered by interrupt **/
volatile bool fast = true;	/** true = 8 MHz CPU clock, false = 4 MHz CPU clock **/
volatile bool expflag = false;
void(* resetFunc) (void) = 0;       		/** Software reset fuction at address 0 **/

/** Compilation defaults **/
#define	config_dev_mode	0			// Turn off various BIOS outputs to speed up development, specifically uploading code
#define config_silent 0				// Turn off the startup jingle
#define config_enable_nmi 1			// Turn on the 50hz NMI timer when CPU is running. If set to 0 will only trigger an NMI on keypress
#define config_outbox_flag 0x0200	// Outbox flag memory location (byte)
#define config_outbox_data 0x0201	// Outbox data memory location (byte)
#define config_inbox_flag 0x0202	// Inbox flag memory location (byte)
#define config_inbox_data 0x0203	// Inbox data memory location (word)
#define config_code_start 0x0205	// Start location of code
#define config_eeprom_address_mode    0 // First EEPROM location
#define config_eeprom_address_speed   1 // Second EEPROM location

/* Status constants */
#define STATUS_DEFAULT 0
#define STATUS_BOOT 1
#define STATUS_READY 2
#define STATUS_UNKNOWN_COMMAND 3
#define STATUS_NO_FILE 4
#define STATUS_CANNOT_OPEN 5  
#define STATUS_MISSING_OPERAND 6
#define STATUS_SCROLL_PROMPT 7
#define STATUS_FILE_EXISTS 8
#define STATUS_ADDRESS_ERROR 9
#define STATUS_POWER 10
#define STATUS_EOF 11

#define PS2_ESC 27
#define PS2_F12 (-fabgl::VK_F12)
#define PS2_ENTER 13
// yeah these really are the same in PS2Keyboard.h
#define PS2_DELETE 127
#define PS2_BACKSPACE 127
#define PS2_UPARROW (-fabgl::VK_UP)
#define PS2_LEFTARROW (-fabgl::VK_LEFT)
#define PS2_DOWNARROW (-fabgl::VK_DOWN)

const uint8_t chardefs[] = {
    0x5a, 0x99, 0xe7, 0x5e, 0x5e, 0x24, 0x18, 0x66, 0xf0, 0xf0, 0xf0, 0xf0,
    0x00, 0x00, 0x00, 0x00, 0x0f, 0x0f, 0x0f, 0x0f, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0,
    0x0f, 0x0f, 0x0f, 0x0f, 0xf0, 0xf0, 0xf0, 0xf0, 0xff, 0xff, 0xff, 0xff,
    0xf0, 0xf0, 0xf0, 0xf0, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55,
    0x00, 0x00, 0x00, 0x00, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55,
    0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x18, 0x3c, 0x7e, 0xff,
    0x7e, 0x66, 0x66, 0x66, 0x00, 0x08, 0x0c, 0xff, 0xff, 0x0c, 0x08, 0x00,
    0x00, 0x10, 0x30, 0xff, 0xff, 0x30, 0x10, 0x00, 0x18, 0x18, 0x3c, 0x7e,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e, 0x3c, 0x18, 0x18,
    0x7e, 0x99, 0x99, 0xff, 0xff, 0x99, 0x99, 0x7e, 0x18, 0x24, 0x42, 0x99,
    0x99, 0x42, 0x24, 0x18, 0x00, 0x24, 0x24, 0x00, 0x81, 0x42, 0x3c, 0x00,
    0x00, 0x24, 0x24, 0x00, 0x3c, 0x42, 0x81, 0x00, 0x3c, 0x7e, 0x99, 0xff,
    0xe7, 0x7e, 0x3c, 0x66, 0x3c, 0x7e, 0x99, 0xdd, 0xff, 0xff, 0xff, 0xdb,
    0x3c, 0x42, 0x84, 0x88, 0x88, 0x84, 0x42, 0x3c, 0x42, 0x24, 0x7e, 0xdb,
    0xff, 0xbd, 0xa5, 0x18, 0x18, 0x3c, 0x3c, 0x7e, 0x7e, 0xff, 0x99, 0x18,
    0x00, 0x80, 0xe0, 0xbc, 0xff, 0x78, 0x60, 0x00, 0x00, 0x00, 0x24, 0x18,
    0x18, 0x24, 0x00, 0x00, 0x81, 0x42, 0x24, 0x00, 0x00, 0x24, 0x42, 0x81,
    0x00, 0x00, 0x3c, 0xc3, 0xc3, 0x3c, 0x00, 0x00, 0x18, 0x18, 0x24, 0x24,
    0x24, 0x24, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x00, 0x00, 0x24, 0x24, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x7e, 0x24, 0x24, 0x7e, 0x24, 0x00,
    0x00, 0x08, 0x3e, 0x28, 0x3e, 0x0a, 0x3e, 0x08, 0x00, 0x62, 0x64, 0x08,
    0x10, 0x26, 0x46, 0x00, 0x00, 0x10, 0x28, 0x10, 0x2a, 0x44, 0x3a, 0x00,
    0x00, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08, 0x08,
    0x08, 0x08, 0x04, 0x00, 0x00, 0x20, 0x10, 0x10, 0x10, 0x10, 0x20, 0x00,
    0x00, 0x00, 0x14, 0x08, 0x3e, 0x08, 0x14, 0x00, 0x00, 0x00, 0x08, 0x08,
    0x3e, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x10,
    0x00, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00,
    0x00, 0x3c, 0x46, 0x4a, 0x52, 0x62, 0x3c, 0x00, 0x00, 0x18, 0x28, 0x08,
    0x08, 0x08, 0x3e, 0x00, 0x00, 0x3c, 0x42, 0x02, 0x3c, 0x40, 0x7e, 0x00,
    0x00, 0x3c, 0x42, 0x0c, 0x02, 0x42, 0x3c, 0x00, 0x00, 0x08, 0x18, 0x28,
    0x48, 0x7e, 0x08, 0x00, 0x00, 0x7e, 0x40, 0x7c, 0x02, 0x42, 0x3c, 0x00,
    0x00, 0x3c, 0x40, 0x7c, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x7e, 0x02, 0x04,
    0x08, 0x10, 0x10, 0x00, 0x00, 0x3c, 0x42, 0x3c, 0x42, 0x42, 0x3c, 0x00,
    0x00, 0x3c, 0x42, 0x42, 0x3e, 0x02, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x10,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x10, 0x20,
    0x00, 0x00, 0x04, 0x08, 0x10, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x3e,
    0x00, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x10, 0x08, 0x04, 0x08, 0x10, 0x00,
    0x00, 0x3c, 0x42, 0x04, 0x08, 0x00, 0x08, 0x00, 0x00, 0x3c, 0x4a, 0x56,
    0x5e, 0x40, 0x3c, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x7e, 0x42, 0x42, 0x00,
    0x00, 0x7c, 0x42, 0x7c, 0x42, 0x42, 0x7c, 0x00, 0x00, 0x3c, 0x42, 0x40,
    0x40, 0x42, 0x3c, 0x00, 0x00, 0x78, 0x44, 0x42, 0x42, 0x44, 0x78, 0x00,
    0x00, 0x7e, 0x40, 0x7c, 0x40, 0x40, 0x7e, 0x00, 0x00, 0x7e, 0x40, 0x7c,
    0x40, 0x40, 0x40, 0x00, 0x00, 0x3c, 0x42, 0x40, 0x4e, 0x42, 0x3c, 0x00,
    0x00, 0x42, 0x42, 0x7e, 0x42, 0x42, 0x42, 0x00, 0x00, 0x3e, 0x08, 0x08,
    0x08, 0x08, 0x3e, 0x00, 0x00, 0x02, 0x02, 0x02, 0x42, 0x42, 0x3c, 0x00,
    0x00, 0x44, 0x48, 0x70, 0x48, 0x44, 0x42, 0x00, 0x00, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x7e, 0x00, 0x00, 0x42, 0x66, 0x5a, 0x42, 0x42, 0x42, 0x00,
    0x00, 0x42, 0x62, 0x52, 0x4a, 0x46, 0x42, 0x00, 0x00, 0x3c, 0x42, 0x42,
    0x42, 0x42, 0x3c, 0x00, 0x00, 0x7c, 0x42, 0x42, 0x7c, 0x40, 0x40, 0x00,
    0x00, 0x3c, 0x42, 0x42, 0x52, 0x4a, 0x3c, 0x00, 0x00, 0x7c, 0x42, 0x42,
    0x7c, 0x44, 0x42, 0x00, 0x00, 0x3c, 0x40, 0x3c, 0x02, 0x42, 0x3c, 0x00,
    0x00, 0xfe, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x42, 0x42, 0x42,
    0x42, 0x42, 0x3c, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x24, 0x18, 0x00,
    0x00, 0x42, 0x42, 0x42, 0x42, 0x5a, 0x24, 0x00, 0x00, 0x42, 0x24, 0x18,
    0x18, 0x24, 0x42, 0x00, 0x00, 0x82, 0x44, 0x28, 0x10, 0x10, 0x10, 0x00,
    0x00, 0x7e, 0x04, 0x08, 0x10, 0x20, 0x7e, 0x00, 0x00, 0x0e, 0x08, 0x08,
    0x08, 0x08, 0x0e, 0x00, 0x00, 0x00, 0x40, 0x20, 0x10, 0x08, 0x04, 0x00,
    0x00, 0x70, 0x10, 0x10, 0x10, 0x10, 0x70, 0x00, 0x00, 0x10, 0x38, 0x54,
    0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
    0x00, 0x10, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x04,
    0x3c, 0x44, 0x3c, 0x00, 0x00, 0x20, 0x20, 0x3c, 0x22, 0x22, 0x3c, 0x00,
    0x00, 0x00, 0x1c, 0x20, 0x20, 0x20, 0x1c, 0x00, 0x00, 0x04, 0x04, 0x3c,
    0x44, 0x44, 0x3c, 0x00, 0x00, 0x00, 0x38, 0x44, 0x78, 0x40, 0x3c, 0x00,
    0x00, 0x0c, 0x10, 0x18, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x3c, 0x44,
    0x44, 0x3c, 0x04, 0x38, 0x00, 0x40, 0x40, 0x78, 0x44, 0x44, 0x44, 0x00,
    0x00, 0x10, 0x00, 0x30, 0x10, 0x10, 0x38, 0x00, 0x00, 0x04, 0x00, 0x04,
    0x04, 0x04, 0x24, 0x18, 0x00, 0x20, 0x28, 0x30, 0x30, 0x28, 0x24, 0x00,
    0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0c, 0x00, 0x00, 0x00, 0x68, 0x54,
    0x54, 0x54, 0x54, 0x00, 0x00, 0x00, 0x78, 0x44, 0x44, 0x44, 0x44, 0x00,
    0x00, 0x00, 0x38, 0x44, 0x44, 0x44, 0x38, 0x00, 0x00, 0x00, 0x78, 0x44,
    0x44, 0x78, 0x40, 0x40, 0x00, 0x00, 0x3c, 0x44, 0x44, 0x3c, 0x04, 0x06,
    0x00, 0x00, 0x1c, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x38, 0x40,
    0x38, 0x04, 0x78, 0x00, 0x00, 0x10, 0x38, 0x10, 0x10, 0x10, 0x0c, 0x00,
    0x00, 0x00, 0x44, 0x44, 0x44, 0x44, 0x38, 0x00, 0x00, 0x00, 0x44, 0x44,
    0x28, 0x28, 0x10, 0x00, 0x00, 0x00, 0x44, 0x54, 0x54, 0x54, 0x28, 0x00,
    0x00, 0x00, 0x44, 0x28, 0x10, 0x28, 0x44, 0x00, 0x00, 0x00, 0x44, 0x44,
    0x44, 0x3c, 0x04, 0x38, 0x00, 0x00, 0x7c, 0x08, 0x10, 0x20, 0x7c, 0x00,
    0x00, 0x0e, 0x08, 0x30, 0x08, 0x08, 0x0e, 0x00, 0x00, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x00, 0x00, 0x70, 0x10, 0x0c, 0x10, 0x10, 0x70, 0x00,
    0x00, 0x14, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x42, 0x99, 0xa1,
    0xa1, 0x99, 0x42, 0x3c, 0xa5, 0x66, 0x18, 0xa1, 0xa1, 0xdb, 0xe7, 0x99,
    0x0f, 0x0f, 0x0f, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xf0, 0xf0, 0xf0,
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
    0x0f, 0x0f, 0x0f, 0x0f, 0xf0, 0xf0, 0xf0, 0xf0, 0x0f, 0x0f, 0x0f, 0x0f,
    0x00, 0x00, 0x00, 0x00, 0x0f, 0x0f, 0x0f, 0x0f, 0x55, 0xaa, 0x55, 0xaa,
    0x55, 0xaa, 0x55, 0xaa, 0xff, 0xff, 0xff, 0xff, 0x55, 0xaa, 0x55, 0xaa,
    0x55, 0xaa, 0x55, 0xaa, 0xff, 0xff, 0xff, 0xff, 0xe7, 0xe7, 0xe7, 0xe7,
    0xe7, 0xe7, 0xe7, 0xe7, 0xff, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0xff,
    0xe7, 0xc3, 0x81, 0x00, 0x81, 0x99, 0x99, 0x99, 0xff, 0xf7, 0xf3, 0x00,
    0x00, 0xf3, 0xf7, 0xff, 0xff, 0xef, 0xcf, 0x00, 0x00, 0xcf, 0xef, 0xff,
    0xe7, 0xe7, 0xc3, 0x81, 0xe7, 0xe7, 0xe7, 0xe7, 0xe7, 0xe7, 0xe7, 0xe7,
    0x81, 0xc3, 0xe7, 0xe7, 0x81, 0x66, 0x66, 0x00, 0x00, 0x66, 0x66, 0x81,
    0xe7, 0xdb, 0xbd, 0x66, 0x66, 0xbd, 0xdb, 0xe7, 0xff, 0xdb, 0xdb, 0xff,
    0x7e, 0xbd, 0xc3, 0xff, 0xff, 0xdb, 0xdb, 0xff, 0xc3, 0xbd, 0x7e, 0xff,
    0xc3, 0x81, 0x66, 0x00, 0x18, 0x81, 0xc3, 0x99, 0xc3, 0x81, 0x66, 0x22,
    0x00, 0x00, 0x00, 0x24, 0xc3, 0xbd, 0x7b, 0x77, 0x77, 0x7b, 0xbd, 0xc3,
    0xbd, 0xdb, 0x81, 0x24, 0x00, 0x42, 0x5a, 0xe7, 0xe7, 0xc3, 0xc3, 0x81,
    0x81, 0x00, 0x66, 0xe7, 0xff, 0x7f, 0x1f, 0x43, 0x00, 0x87, 0x9f, 0xff,
    0xff, 0xff, 0xdb, 0xe7, 0xe7, 0xdb, 0xff, 0xff, 0x7e, 0xbd, 0xdb, 0xff,
    0xff, 0xdb, 0xbd, 0x7e, 0xff, 0xff, 0xc3, 0x3c, 0x3c, 0xc3, 0xff, 0xff,
    0xe7, 0xe7, 0xdb, 0xdb, 0xdb, 0xdb, 0xe7, 0xe7, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0xef, 0xef, 0xef, 0xff, 0xef, 0xff,
    0xff, 0xdb, 0xdb, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xdb, 0x81, 0xdb,
    0xdb, 0x81, 0xdb, 0xff, 0xff, 0xf7, 0xc1, 0xd7, 0xc1, 0xf5, 0xc1, 0xf7,
    0xff, 0x9d, 0x9b, 0xf7, 0xef, 0xd9, 0xb9, 0xff, 0xff, 0xef, 0xd7, 0xef,
    0xd5, 0xbb, 0xc5, 0xff, 0xff, 0xf7, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xfb, 0xf7, 0xf7, 0xf7, 0xf7, 0xfb, 0xff, 0xff, 0xdf, 0xef, 0xef,
    0xef, 0xef, 0xdf, 0xff, 0xff, 0xff, 0xeb, 0xf7, 0xc1, 0xf7, 0xeb, 0xff,
    0xff, 0xff, 0xf7, 0xf7, 0xc1, 0xf7, 0xf7, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xf7, 0xf7, 0xef, 0xff, 0xff, 0xff, 0xff, 0xc1, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xe7, 0xe7, 0xff, 0xff, 0xff, 0xfd, 0xfb,
    0xf7, 0xef, 0xdf, 0xff, 0xff, 0xc3, 0xb9, 0xb5, 0xad, 0x9d, 0xc3, 0xff,
    0xff, 0xe7, 0xd7, 0xf7, 0xf7, 0xf7, 0xc1, 0xff, 0xff, 0xc3, 0xbd, 0xfd,
    0xc3, 0xbf, 0x81, 0xff, 0xff, 0xc3, 0xbd, 0xf3, 0xfd, 0xbd, 0xc3, 0xff,
    0xff, 0xf7, 0xe7, 0xd7, 0xb7, 0x81, 0xf7, 0xff, 0xff, 0x81, 0xbf, 0x83,
    0xfd, 0xbd, 0xc3, 0xff, 0xff, 0xc3, 0xbf, 0x83, 0xbd, 0xbd, 0xc3, 0xff,
    0xff, 0x81, 0xfd, 0xfb, 0xf7, 0xef, 0xef, 0xff, 0xff, 0xc3, 0xbd, 0xc3,
    0xbd, 0xbd, 0xc3, 0xff, 0xff, 0xc3, 0xbd, 0xbd, 0xc1, 0xfd, 0xc3, 0xff,
    0xff, 0xff, 0xff, 0xef, 0xff, 0xff, 0xef, 0xff, 0xff, 0xff, 0xef, 0xff,
    0xff, 0xef, 0xef, 0xdf, 0xff, 0xff, 0xfb, 0xf7, 0xef, 0xf7, 0xfb, 0xff,
    0xff, 0xff, 0xff, 0xc1, 0xff, 0xc1, 0xff, 0xff, 0xff, 0xff, 0xef, 0xf7,
    0xfb, 0xf7, 0xef, 0xff, 0xff, 0xc3, 0xbd, 0xfb, 0xf7, 0xff, 0xf7, 0xff,
    0xff, 0xc3, 0xb5, 0xa9, 0xa1, 0xbf, 0xc3, 0xff, 0xff, 0xc3, 0xbd, 0xbd,
    0x81, 0xbd, 0xbd, 0xff, 0xff, 0x83, 0xbd, 0x83, 0xbd, 0xbd, 0x83, 0xff,
    0xff, 0xc3, 0xbd, 0xbf, 0xbf, 0xbd, 0xc3, 0xff, 0xff, 0x87, 0xbb, 0xbd,
    0xbd, 0xbb, 0x87, 0xff, 0xff, 0x81, 0xbf, 0x83, 0xbf, 0xbf, 0x81, 0xff,
    0xff, 0x81, 0xbf, 0x83, 0xbf, 0xbf, 0xbf, 0xff, 0xff, 0xc3, 0xbd, 0xbf,
    0xb1, 0xbd, 0xc3, 0xff, 0xff, 0xbd, 0xbd, 0x81, 0xbd, 0xbd, 0xbd, 0xff,
    0xff, 0xc1, 0xf7, 0xf7, 0xf7, 0xf7, 0xc1, 0xff, 0xff, 0xfd, 0xfd, 0xfd,
    0xbd, 0xbd, 0xc3, 0xff, 0xff, 0xbb, 0xb7, 0x8f, 0xb7, 0xbb, 0xbd, 0xff,
    0xff, 0xbf, 0xbf, 0xbf, 0xbf, 0xbf, 0x81, 0xff, 0xff, 0xbd, 0x99, 0xa5,
    0xbd, 0xbd, 0xbd, 0xff, 0xff, 0xbd, 0x9d, 0xad, 0xb5, 0xb9, 0xbd, 0xff,
    0xff, 0xc3, 0xbd, 0xbd, 0xbd, 0xbd, 0xc3, 0xff, 0xff, 0x83, 0xbd, 0xbd,
    0x83, 0xbf, 0xbf, 0xff, 0xff, 0xc3, 0xbd, 0xbd, 0xad, 0xb5, 0xc3, 0xff,
    0xff, 0x83, 0xbd, 0xbd, 0x83, 0xbb, 0xbd, 0xff, 0xff, 0xc3, 0xbf, 0xc3,
    0xfd, 0xbd, 0xc3, 0xff, 0xff, 0x01, 0xef, 0xef, 0xef, 0xef, 0xef, 0xff,
    0xff, 0xbd, 0xbd, 0xbd, 0xbd, 0xbd, 0xc3, 0xff, 0xff, 0xbd, 0xbd, 0xbd,
    0xbd, 0xdb, 0xe7, 0xff, 0xff, 0xbd, 0xbd, 0xbd, 0xbd, 0xa5, 0xdb, 0xff,
    0xff, 0xbd, 0xdb, 0xe7, 0xe7, 0xdb, 0xbd, 0xff, 0xff, 0x7d, 0xbb, 0xd7,
    0xef, 0xef, 0xef, 0xff, 0xff, 0x81, 0xfb, 0xf7, 0xef, 0xdf, 0x81, 0xff,
    0xff, 0xf1, 0xf7, 0xf7, 0xf7, 0xf7, 0xf1, 0xff, 0xff, 0xff, 0xbf, 0xdf,
    0xef, 0xf7, 0xfb, 0xff, 0xff, 0x8f, 0xef, 0xef, 0xef, 0xef, 0x8f, 0xff,
    0xff, 0xef, 0xc7, 0xab, 0xef, 0xef, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0x00, 0xff, 0xef, 0xf7, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xc7, 0xfb, 0xc3, 0xbb, 0xc3, 0xff, 0xff, 0xdf, 0xdf, 0xc3,
    0xdd, 0xdd, 0xc3, 0xff, 0xff, 0xff, 0xe3, 0xdf, 0xdf, 0xdf, 0xe3, 0xff,
    0xff, 0xfb, 0xfb, 0xc3, 0xbb, 0xbb, 0xc3, 0xff, 0xff, 0xff, 0xc7, 0xbb,
    0x87, 0xbf, 0xc3, 0xff, 0xff, 0xf3, 0xef, 0xe7, 0xef, 0xef, 0xef, 0xff,
    0xff, 0xff, 0xc3, 0xbb, 0xbb, 0xc3, 0xfb, 0xc7, 0xff, 0xbf, 0xbf, 0x87,
    0xbb, 0xbb, 0xbb, 0xff, 0xff, 0xef, 0xff, 0xcf, 0xef, 0xef, 0xc7, 0xff,
    0xff, 0xfb, 0xff, 0xfb, 0xfb, 0xfb, 0xdb, 0xe7, 0xff, 0xdf, 0xd7, 0xcf,
    0xcf, 0xd7, 0xdb, 0xff, 0xff, 0xef, 0xef, 0xef, 0xef, 0xef, 0xf3, 0xff,
    0xff, 0xff, 0x97, 0xab, 0xab, 0xab, 0xab, 0xff, 0xff, 0xff, 0x87, 0xbb,
    0xbb, 0xbb, 0xbb, 0xff, 0xff, 0xff, 0xc7, 0xbb, 0xbb, 0xbb, 0xc7, 0xff,
    0xff, 0xff, 0x87, 0xbb, 0xbb, 0x87, 0xbf, 0xbf, 0xff, 0xff, 0xc3, 0xbb,
    0xbb, 0xc3, 0xfb, 0xf9, 0xff, 0xff, 0xe3, 0xdf, 0xdf, 0xdf, 0xdf, 0xff,
    0xff, 0xff, 0xc7, 0xbf, 0xc7, 0xfb, 0x87, 0xff, 0xff, 0xef, 0xc7, 0xef,
    0xef, 0xef, 0xf3, 0xff, 0xff, 0xff, 0xbb, 0xbb, 0xbb, 0xbb, 0xc7, 0xff,
    0xff, 0xff, 0xbb, 0xbb, 0xd7, 0xd7, 0xef, 0xff, 0xff, 0xff, 0xbb, 0xab,
    0xab, 0xab, 0xd7, 0xff, 0xff, 0xff, 0xbb, 0xd7, 0xef, 0xd7, 0xbb, 0xff,
    0xff, 0xff, 0xbb, 0xbb, 0xbb, 0xc3, 0xfb, 0xc7, 0xff, 0xff, 0x83, 0xf7,
    0xef, 0xdf, 0x83, 0xff, 0xff, 0xf1, 0xf7, 0xcf, 0xf7, 0xf7, 0xf1, 0xff,
    0xff, 0xf7, 0xf7, 0xf7, 0xf7, 0xf7, 0xf7, 0xff, 0xff, 0x8f, 0xef, 0xf3,
    0xef, 0xef, 0x8f, 0xff, 0xff, 0xeb, 0xd7, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xc3, 0xbd, 0x66, 0x5e, 0x5e, 0x66, 0xbd, 0xc3
};

const uint8_t cerbicon_img[] = {
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x0d, 0x20, 0x16, 0x20, 0x17, 0x20, 0x18, 0x20, 0x19, 0x20,
    0x1a, 0x20, 0x1b, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x5f, 0x5f, 0x5f, 0x20,
    0x5f, 0x5f, 0x5f, 0x20, 0x5f, 0x5f, 0x5f, 0x20, 0x20, 0x5f, 0x5f, 0x5f,
    0x20, 0x5f, 0x5f, 0x5f, 0x20, 0x5f, 0x5f, 0x5f, 0x20, 0x5f, 0x20, 0x20,
    0x20, 0x5f, 0x20, 0x5f, 0x5f, 0x5f, 0x20, 0x20, 0x20, 0x2f, 0x20, 0x5f,
    0x5f, 0x7c, 0x20, 0x5f, 0x5f, 0x7c, 0x20, 0x5f, 0x20, 0x5c, 0x7c, 0x20,
    0x5f, 0x20, 0x29, 0x20, 0x5f, 0x5f, 0x7c, 0x20, 0x5f, 0x20, 0x5c, 0x20,
    0x7c, 0x20, 0x7c, 0x20, 0x2f, 0x20, 0x5f, 0x5f, 0x7c, 0x20, 0x20, 0x7c,
    0x28, 0x5f, 0x5f, 0x7c, 0x20, 0x5f, 0x7c, 0x7c, 0x20, 0x20, 0x20, 0x2f,
    0x7c, 0x20, 0x5f, 0x20, 0x5c, 0x20, 0x5f, 0x7c, 0x7c, 0x20, 0x20, 0x20,
    0x2f, 0x20, 0x7c, 0x5f, 0x7c, 0x20, 0x5c, 0x5f, 0x5f, 0x20, 0x5c, 0x20,
    0x20, 0x5c, 0x5f, 0x5f, 0x5f, 0x7c, 0x5f, 0x5f, 0x5f, 0x7c, 0x5f, 0x7c,
    0x5f, 0x5c, 0x7c, 0x5f, 0x5f, 0x5f, 0x2f, 0x5f, 0x5f, 0x5f, 0x7c, 0x5f,
    0x7c, 0x5f, 0x5c, 0x5c, 0x5f, 0x5f, 0x5f, 0x2f, 0x7c, 0x5f, 0x5f, 0x5f,
    0x7c, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f,
    0x5f, 0x5f, 0x5f, 0x20, 0x20, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f,
    0x5f, 0x5f, 0x5f, 0x5f, 0x20, 0x20, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f,
    0x5f, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x5c, 0x5f, 0x5f,
    0x5f, 0x5f, 0x5f, 0x20, 0x20, 0x5c, 0x2f, 0x5f, 0x20, 0x20, 0x20, 0x5c,
    0x20, 0x20, 0x20, 0x5f, 0x20, 0x20, 0x5c, 0x20, 0x5c, 0x20, 0x20, 0x20,
    0x5f, 0x20, 0x20, 0x5c, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x2f, 0x20, 0x20, 0x5f, 0x5f, 0x5f, 0x5f, 0x2f, 0x20, 0x7c, 0x20, 0x20,
    0x20, 0x2f, 0x20, 0x20, 0x2f, 0x20, 0x5c, 0x20, 0x20, 0x5c, 0x2f, 0x20,
    0x20, 0x2f, 0x20, 0x5c, 0x20, 0x20, 0x5c, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x2f, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x5c, 0x20, 0x7c,
    0x20, 0x20, 0x20, 0x5c, 0x20, 0x20, 0x5c, 0x5f, 0x2f, 0x20, 0x20, 0x20,
    0x5c, 0x20, 0x20, 0x5c, 0x5f, 0x2f, 0x20, 0x20, 0x20, 0x5c, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x5c, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x20,
    0x5c, 0x7c, 0x5f, 0x5f, 0x5f, 0x7c, 0x5c, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f,
    0x20, 0x20, 0x2f, 0x5c, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x20, 0x20, 0x2f,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x5c, 0x2f, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x5c, 0x2f, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x5c,
    0x2f, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x43, 0x72, 0x65, 0x61, 0x74, 0x65, 0x64, 0x20,
    0x61, 0x74, 0x20, 0x54, 0x68, 0x65, 0x20, 0x42, 0x79, 0x74, 0x65, 0x20,
    0x41, 0x74, 0x74, 0x69, 0x63, 0x21, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x54, 0x79, 0x70, 0x65, 0x20,
    0x62, 0x61, 0x73, 0x69, 0x63, 0x7a, 0x38, 0x30, 0x20, 0x66, 0x6f, 0x72,
    0x20, 0x5a, 0x38, 0x30, 0x20, 0x42, 0x41, 0x53, 0x49, 0x43, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x54, 0x79, 0x70, 0x65,
    0x20, 0x62, 0x61, 0x73, 0x69, 0x63, 0x36, 0x35, 0x30, 0x32, 0x20, 0x66,
    0x6f, 0x72, 0x20, 0x36, 0x35, 0x30, 0x32, 0x20, 0x42, 0x41, 0x53, 0x49,
    0x43, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x54, 0x79, 0x70,
    0x65, 0x20, 0x68, 0x65, 0x6c, 0x70, 0x20, 0x6f, 0x72, 0x20, 0x3f, 0x20,
    0x66, 0x6f, 0x72, 0x20, 0x42, 0x49, 0x4f, 0x53, 0x20, 0x63, 0x6f, 0x6d,
    0x6d, 0x61, 0x6e, 0x64, 0x73, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20
};

uint8_t cerb_ram[65536];

unsigned int cpeekW(unsigned int address) {
  return (cpeek(address) | (cpeek(address+1) << 8));
}
void cpokeW(unsigned int address, unsigned int data) {
	cpoke(address, data & 0xFF);
	cpoke(address + 1, (data >> 8) & 0xFF);
}
void cpokeL(unsigned int address, unsigned long data) {
	cpoke(address, data & 0xFF);
	cpoke(address + 1, (data >> 8) & 0xFF);
	cpoke(address + 2, (data >> 16) & 0xFF);
	cpoke(address + 3, (data >> 24) & 0xFF);
}

boolean cpokeStr(unsigned int address, String text) {
	unsigned int i;
	for(i = 0; i < text.length(); i++) {
		cpoke(address + i, text[i]);
	}
	cpoke(address + i, 0);
	return true;
}

boolean cpeekStr(unsigned int address, volatile char * dest, int max) {
	unsigned int i;
	byte c;
	for(i = 0; i < max; i++) {
		c = cpeek(address + i);
		dest[i] = c;
		if(c == 0) return true;
	}
	return false;
}
#define tone(a,b,c)

void cprintChar(byte x, byte y, byte token) {
  	/** First, calculate address **/
  	unsigned int address = 0xF800 + ((y - 1) * 40) + (x - 1); /** Video memory addresses start at 0XF800 **/
  	cpoke(address, token);
}

void clearLine(byte y) {
  	unsigned int x;
  	for (x = 2; x <= 39; x++) {
    	cprintChar(x, y, 32);
	}
}

void cls() {
  	/** This clears the screen only WITHIN the main frame **/
  	unsigned int y;
  	for (y = 2; y <= 25; y++) {
    	clearLine(y);
	}
}

void ccls() {
  	/** This clears the entire screen **/
  	unsigned int x;
  	for (x = 0; x < 1200; x++) {
	    cpoke(0xF800 + x, 32);        /** Video memory addresses start at 0XF800 **/
	}
}

void cprintFrames() {
  unsigned int x;
  unsigned int y;
  /** First print horizontal bars **/
  for (x = 2; x <= 39; x++) {
    cprintChar(x, 1, 3);
    cprintChar(x, 30, 131);
    cprintChar(x, 26, 3);
  }
  /** Now print vertical bars **/
  for (y = 1; y <= 30; y++) {
    cprintChar(1, y, 160);
    cprintChar(40, y, 160);
  }
}

void cprintString(byte x, byte y, String text) {
  	unsigned int i;
  	for (i = 0; i < text.length(); i++) {
	    if (((x + i) > 1) && ((x + i) < 40)) {
			cprintChar(x + i, y, text[i]);
		}
  	}
}

void center(String text) {
  	clearLine(27);
  	cprintString(2+(38-text.length())/2, 27, text);
}

void cprintStatus(byte status) {
  	/** REMEMBER: The macro "F()" simply tells the compiler to put the string in code memory, so to save dynamic memory **/
  	switch( status ) {
    	case STATUS_BOOT:
      		center(F("Here we go! Hang on..."));
      		break;
    	case STATUS_READY:
      		center(F("Alright, done!"));
      		break;
    	case STATUS_UNKNOWN_COMMAND:
      		center(F("Darn, unrecognized command"));
      		tone(SOUND, 50, 150);
      		break;
    	case STATUS_NO_FILE:
      		center(F("Oops, file doesn't seem to exist"));
      		tone(SOUND, 50, 150);
      		break;
    	case STATUS_CANNOT_OPEN:
      		center(F("Oops, couldn't open the file"));
      		tone(SOUND, 50, 150);
      		break;
    	case STATUS_MISSING_OPERAND:
      		center(F("Oops, missing an operand!!"));
      		tone(SOUND, 50, 150);
      		break;
    	case STATUS_SCROLL_PROMPT:
      		center(F("Press a key to scroll, ESC to stop"));
      		break;
    	case STATUS_FILE_EXISTS:
      		center(F("The file already exists!"));
      	break;
    		case STATUS_ADDRESS_ERROR:
      	center(F("Oops, invalid address range!"));
      		break;
    	case STATUS_POWER:
      		center(F("Feel the power of Dutch design!!"));
      		break;
    	default:
      		cprintString(2, 27, F("      CERBERUS 2100: "));
      		if (mode) cprintString(23, 27, F(" Z80, "));
      		else cprintString(23, 27, F("6502, "));
      		if (fast) cprintString(29, 27, F("8 MHz"));
      		else cprintString(29, 27, F("4 MHz"));
      		cprintString(34, 27, F("     "));
  	}
}


void cprintBanner() {
	/** Load the CERBERUS icon image on the screen ************/
    const uint8_t *dataFile2 = cerbicon_img;
    for (uint8_t y = 3; y <= 25; y++) {
        for (uint8_t x = 2; x <= 39; x++) {
            int inChar = *dataFile2;
            dataFile2++;
            
            cprintChar(x, y, inChar);
        }
    }
}

void cprintEditLine () {
  	byte i;
  	for (i = 0; i < 38; i++) cprintChar(i + 2, 29, editLine[i]);
}

void clearEditLine() {
  	/** Resets the contents of edit line and reprints it **/
  	byte i;
  	editLine[0] = 62;
  	editLine[1] = 0;
  	for (i = 2; i < 38; i++) editLine[i] = 32;
  	pos = 1;
  	cprintEditLine();
}

uint8_t cerb_color[8];
void setup_colours() {
    cerb_color[0] = VGAController.createRawPixel(RGB222(0, 3, 0));
    cerb_color[1] = VGAController.createRawPixel(RGB222(3, 0, 0));
    cerb_color[2] = VGAController.createRawPixel(RGB222(0, 0, 3));
    cerb_color[3] = VGAController.createRawPixel(RGB222(3, 3, 0));
    cerb_color[4] = VGAController.createRawPixel(RGB222(0, 3, 3));
    cerb_color[5] = VGAController.createRawPixel(RGB222(3, 0, 3));
    cerb_color[6] = VGAController.createRawPixel(RGB222(0, 0, 0));
    cerb_color[7] = VGAController.createRawPixel(RGB222(3, 3, 3));
}

void IRAM_ATTR drawCerberusScanline(void * arg, uint8_t * dest, int scanLine)
{
    // vid ram at 0xf800 (40x30 bytes)
    // char ram at 0xf000 (2 KiB)
    const uint8_t bgcolor = cerb_color[6];

    auto width  = VGAController.getScreenWidth();
    auto height = VGAController.getScreenHeight();

    // Drawing 2 scanlines per call to drawScanline. Since cerberus
    // doubles the scanlines, we duplicate the pixel data onto the second scanline
    for (int col=0; col<40; col ++) {
        // what tile?
        uint8_t tile_num = cerb_ram[0xf800 + (scanLine / 16)*40 + col];
        // what line in the tile (0-7)
        int tile_line = (scanLine & 0xf) >> 1;
        uint8_t tile_dat = cerb_ram[0xf000 + tile_num*8 + tile_line];
        uint8_t fgcolor = cerb_color[7];
        if (tile_num >= 8 && tile_num < 32) {
            fgcolor = cerb_color[(tile_num-8)%6];
        }

        for (int p=0; p<8; p++) {
            const auto color = tile_dat & (0x80>>p) ? fgcolor : bgcolor;
            const int x = col*16 + 2*p;
            VGA_PIXELINROW(dest, x) = color;
            VGA_PIXELINROW(dest, x + 1) = color;
            VGA_PIXELINROW((dest+width), x) = color;
            VGA_PIXELINROW((dest+width), x + 1) = color;
        }
    }

    scanLine += 2;

    if (scanLine == height) {
        // signal end of screen
        vTaskNotifyGiveFromISR(mainTaskHandle, NULL);
    }
}

void errPrint(const char *msg) {
    uint16_t addr = 0xf800;

    while (*msg) {
        cpoke(addr, *msg);
        addr++;
        msg++;
    }
}

void load_chardefs() {
    memcpy(&cerb_ram[0xf000], chardefs, sizeof chardefs);
}

extern void init_cpus(); // in emu_cpu.cpp
extern void cpu_clockcycles(int num_clocks);

void setup()
{
    Serial.begin(115200);
    mainTaskHandle = xTaskGetCurrentTaskHandle();

    // init cerberus ram
    memset(cerb_ram, 0, sizeof cerb_ram);
    load_chardefs();

    init_cpus();

    PS2Controller.begin(PS2Preset::KeyboardPort0_MousePort1, KbdMode::CreateVirtualKeysQueue);
    PS2Controller.keyboard()->setLayout(&fabgl::UKLayout);
    PS2Controller.keyboard()->setCodePage(fabgl::CodePages::get(1252));
    PS2Controller.keyboard()->enableVirtualKeys(true, true);

    VGAController.begin();
    VGAController.setScanlinesPerCallBack(scanlinesPerCallback);
    VGAController.setDrawScanlineCallback(drawCerberusScanline);
    VGAController.setResolution(VGA_640x480_60Hz);
    setup_colours();
    
    const bool sd_mounted = FileBrowser::mountSDCard(false, SDCARD_MOUNT_PATH);

    // compiled-in chardefs are already loaded, so sdcard ones are optional
    // (this is not true on the real cerberus)
  	if (load("chardefs.bin", 0xf000) != STATUS_READY) {
		//tone(SOUND, 50, 150);
    }
    ccls();
    cprintFrames();
    cprintBanner();
    cprintStatus(STATUS_BOOT);
  	//playJingle();
    delay(1000);
    cprintStatus(STATUS_DEFAULT);
    clearEditLine();

    if (!sd_mounted) {
        errPrint("Failed to mount SDCard");
    }
}

int readKey() {
    auto keyboard = PS2Controller.keyboard();
    // maybe read from debug serial
    if (keyboard) {
        if (keyboard->virtualKeyAvailable()) {
            fabgl::VirtualKeyItem key;
            bool success = keyboard->getNextVirtualKey(&key, 0);
            if (success && key.down) {
                if (key.ASCII == 8) {
                    return PS2_BACKSPACE;
                } else if (key.ASCII) {
                    return key.ASCII;
                } else {
                    return -key.vk;
                }
            }
        }
    }
    return 0;
}

String getNextWord(bool fromTheBeginning) {
  /** A very simple parser that returns the next word in the edit line **/
  static byte initialPosition;    /** Start parsing from this point in the edit line **/
  byte i, j, k;                   /** General-purpose indices **/
  if (fromTheBeginning) initialPosition = 1; /** Starting from the beginning of the edit line **/
  i = initialPosition;            /** Otherwise, continuing on from where we left off in previous call **/
  while ((editLine[i] == 32) || (editLine[i] == 44)) i++; /** Ignore leading spaces or commas **/
  j = i + 1;                      /** Now start indexing the next word proper **/
  /** Find the end of the word, marked either by a space, a comma or the cursor **/
  while ((editLine[j] != 32) && (editLine[j] != 44) && (editLine[j] != 0)) j++;
  char nextWord[j - i + 1];       /** Create a buffer (the +1 is to make space for null-termination) **/
  for (k = i; k < j; k++) nextWord[k - i] = editLine[k]; /** Transfer the word to the buffer **/
  nextWord[j - i] = 0;            /** Null-termination **/
  initialPosition = j;            /** Next time round, start from here, unless... **/
  return (nextWord);              /** Return the contents of the buffer **/
}

void binMove(String startAddr, String endAddr, String destAddr) {
  unsigned int start, finish, destination;                /** Memory addresses **/
  unsigned int i;                                         /** Address counter **/
  if (startAddr == "") cprintStatus(STATUS_MISSING_OPERAND);                   /** Missing the file's name **/
  else {
    start = strtol(startAddr.c_str(), NULL, 16);          /** Convert hexadecimal address string to unsigned int **/
    if (endAddr == "") cprintStatus(STATUS_MISSING_OPERAND);                   /** Missing the file's name **/
    else {
      finish = strtol(endAddr.c_str(), NULL, 16);         /** Convert hexadecimal address string to unsigned int **/
      if (destAddr == "") cprintStatus(STATUS_MISSING_OPERAND);                /** Missing the file's name **/
      else {
        destination = strtol(destAddr.c_str(), NULL, 16); /** Convert hexadecimal address string to unsigned int **/
        if (finish < start) cprintStatus(STATUS_ADDRESS_ERROR);              /** Invalid address range **/
        else if ((destination <= finish) && (destination >= start)) cprintStatus(STATUS_ADDRESS_ERROR); /** Destination cannot be within original range **/  
        else {
          for (i = start; i <= finish; i++) {
            cpoke(destination, cpeek(i));
            destination++;
          }
          cprintStatus(STATUS_READY);
        }
      }
    }
  }
}

void list(String address) {
  /** Lists the contents of memory from the given address, in a compact format **/
  byte i, j;                      /** Just counters **/
  unsigned int addr;              /** Memory address **/
  if (address == "") addr = 0;
  else addr = strtol(address.c_str(), NULL, 16); /** Convert hexadecimal address string to unsigned int **/
  for (i = 2; i < 25; i++) {
    cprintString(3, i, "0x");
    cprintString(5, i, String(addr, HEX));
    for (j = 0; j < 8; j++) {
      cprintString(12+(j*3), i, String(cpeek(addr++), HEX)); /** Print bytes in HEX **/
    }
  }
}

unsigned int addressTranslate (unsigned int virtualAddress) {
  	byte numberVirtualRows;
  	numberVirtualRows = (virtualAddress - 0xF800) / 38;
  	return((virtualAddress + 43) + (2 * (numberVirtualRows - 1)));
}

void testMem() {
  	/** Tests that all four memories are accessible for reading and writing **/
  	unsigned int x;
  	byte i = 0;
    for (x = 0; x < 874; x++) {
    	cpoke(x, i);                                           /** Write to low memory **/
    	cpoke(0x8000 + x, cpeek(x));                           /** Read from low memory and write to high memory **/
    	cpoke(addressTranslate(0xF800 + x), cpeek(0x8000 + x));/** Read from high mem, write to VMEM, read from character mem **/
    	if (i < 255) i++;
    	else i = 0;
  	}
}

void storePreviousLine() {
	for (byte i = 0; i < 38; i++) previousEditLine[i] = editLine[i]; /** Store edit line just executed **/
}

void help() {
  cls();
  cprintString(3, 2,  F("The Byte Attic's CERBERUS 2100 (tm)"));
  cprintString(3, 3,  F("        AVAILABLE COMMANDS:"));
  cprintString(3, 4,  F(" (All numbers must be hexadecimal)"));
  cprintString(3, 6,  F("0xADDR BYTE: Writes BYTE at ADDR"));
  cprintString(3, 7,  F("list ADDR: Lists memory from ADDR"));
  cprintString(3, 8,  F("cls: Clears the screen"));
  cprintString(3, 9,  F("testmem: Reads/writes to memories"));
  cprintString(3, 10, F("6502: Switches to 6502 CPU mode"));
  cprintString(3, 11, F("z80: Switches to Z80 CPU mode"));
  cprintString(3, 12, F("fast: Switches to 8MHz mode"));
  cprintString(3, 13, F("slow: Switches to 4MHz mode"));
  cprintString(3, 14, F("reset: Resets the system"));
  cprintString(3, 15, F("dir: Lists files on uSD card"));
  cprintString(3, 16, F("del FILE: Deletes FILE"));
  cprintString(3, 17, F("load FILE ADDR: Loads FILE at ADDR"));
  cprintString(3, 18, F("save ADDR1 ADDR2 FILE: Saves memory"));
  cprintString(5, 19, F("from ADDR1 to ADDR2 to FILE"));
  cprintString(3, 20, F("run: Executes code in memory"));
  cprintString(3, 21, F("move ADDR1 ADDR2 ADDR3: Moves bytes"));
  cprintString(5, 22, F("between ADDR1 & ADDR2 to ADDR3 on"));
  cprintString(3, 23, F("help / ?: Shows this help screen"));
  cprintString(3, 24, F("F12 key: Quits CPU program"));
}

/**
 * Warning: returns a static buffer
 */
const char *filenameToSdAbsPath(const char *filename) {
    static char buf[256];
    snprintf(buf, sizeof buf, "%s/%s", SDCARD_MOUNT_PATH, filename);
    return buf;
}

FILE *SD_open(String &filename, const char *mode) {
    return fopen(filenameToSdAbsPath(filename.c_str()), mode);
}

void SD_close(FILE *f) {
    fclose(f);
}

bool SD_exists(String &filename) {
    FILE *f = SD_open(filename, "r");
    bool exists = !!f;
    SD_close(f);
    return exists;
}

int delFile(String filename) {
	int status = STATUS_DEFAULT;
  	/** Deletes a file from the uSD card **/
  	if (!SD_exists(filename)) {
		status = STATUS_NO_FILE;		/** The file doesn't exist, so stop with error **/
	}
  	else {
        remove(filenameToSdAbsPath(filename.c_str()));
	    //SD.remove(filename);          /** Delete the file **/
	    status = STATUS_READY;
  	}
	return status;
}

void catDelFile(String filename) {
	cprintStatus(delFile(filename));
}

int get_file_size(const char *filename)
{
    FILE *f = fopen(filenameToSdAbsPath(filename), "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        int len = ftell(f);
        fclose(f);
        return len;
    } else {
        return 0;
    }
}

void dir() {
  /** Lists the files in the root directory of uSD card, if available **/
  byte y = 2;                     /** Screen line **/
  byte x = 0;                     /** Screen column **/
  //File root;                      /** Root directory of uSD card **/
  //File entry;                     /** A file on the uSD card **/
  cls();
  DIR *root = opendir(SDCARD_MOUNT_PATH);
  //root = SD.open("/");            /** Go to the root directory of uSD card **/
  while (true) {
    struct dirent *entry = readdir(root);
    //entry = root.openNextFile();  /** Open next file **/
    if (!entry) {                 /** No more files on the uSD card **/
      closedir(root);
      //root.close();               /** Close root directory **/
      cprintStatus(STATUS_READY);            /** Announce completion **/
      break;                      /** Get out of this otherwise infinite while() loop **/
    }
    cprintString(3, y, entry->d_name);
    cprintString(20, y, String(get_file_size(entry->d_name), DEC));
    //entry.close();                /** Close file as soon as it is no longer needed **/
    if (y < 24) y++;              /** Go to the next screen line **/
    else {
      cprintStatus(STATUS_SCROLL_PROMPT);            /** End of screen has been reached, needs to scrow down **/
      for (x = 2; x < 40; x++) cprintChar(x, 29, ' '); /** Hide editline while waiting for key press **/
      int key;
      while (!(key = readKey()));
      //while (!keyboard.available());/** Wait for a key to be pressed **/
      if (key == PS2_ESC) { /** If the user pressed ESC, break and exit **/
        tone(SOUND, 750, 5);      /** Clicking sound for auditive feedback to key press **/
        closedir(root);
        //root.close();             /** Close the directory before exiting **/
        cprintStatus(STATUS_READY);
        break;
      } else {
        tone(SOUND, 750, 5);      /** Clicking sound for auditive feedback to key press **/
        cls();                    /** Clear the screen and... **/
        y = 2;                    /** ...go back tot he top of the screen **/
      }
    }
  }
}

int save(String filename, unsigned int startAddress, unsigned int endAddress) {
  	/** Saves contents of a region of memory to a file on uSD card **/
	int status = STATUS_DEFAULT;
  	unsigned int i;                                     /** Memory address counter **/
  	byte data;                                          /** Data from memory **/
  	FILE *dataFile;                                      /** File to be created and written to **/
	if (endAddress < startAddress) {
		status = STATUS_ADDRESS_ERROR;            		/** Invalid address range **/
	}
	else {
		if (filename == "") {
			status = STATUS_MISSING_OPERAND;          	/** Missing the file's name **/
		}
		else {
			if (SD_exists(filename)) {
				status = STATUS_FILE_EXISTS;   				/** The file already exists, so stop with error **/
			}
			else {
				dataFile = SD_open(filename, "wb"); /** Try to create the file **/
				if (!dataFile) {
					status = STATUS_CANNOT_OPEN;           /** Cannot create the file **/
				}
				else {                                    /** Now we can finally write into the created file **/
					for(i = startAddress; i <= endAddress; i++) {
						data = cpeek(i);
                        fwrite(&data, 1, 1, dataFile);
					}
                    SD_close(dataFile);
					status = STATUS_READY;
				}
			}
		}
	}
	return status;
}

void catSave(String filename, String startAddress, String endAddress) {
	unsigned int startAddr;
	unsigned int endAddr;
	int status = STATUS_DEFAULT;
   	if (startAddress == "") {
		status = STATUS_MISSING_OPERAND;               /** Missing operand **/
	}
	else {
		startAddr = strtol(startAddress.c_str(), NULL, 16);
		if(endAddress == "") {
			status = STATUS_MISSING_OPERAND;
		}
		else {
			endAddr = strtol(endAddress.c_str(), NULL, 16);
			status = save(filename, startAddr, endAddr);
		}
	}
	cprintStatus(status);
}

int load(String filename, unsigned int startAddr) {
  /** Loads a binary file from the uSD card into memory **/
  FILE *dataFile;                                /** File for reading from on SD Card, if present **/
  uint16_t addr = startAddr;                /** Address where to load the file into memory **/
  int status = STATUS_DEFAULT;
  if (filename == "") {
	  status = STATUS_MISSING_OPERAND;
  }
  else {
#if 0
    if (!SD.exists(filename)) {
		status = STATUS_NO_FILE;				/** The file does not exist, so stop with error **/
	} 
    else {
#endif /* 0 */
      	dataFile = SD_open(filename, "rb");           /** Open the binary file **/
      	if (!dataFile) {
			status = STATUS_CANNOT_OPEN; 		/** Cannot open the file **/
	  	}
      	else {
			bytesRead = 0;
            int dataByte;
            while ((dataByte = fgetc(dataFile)) != EOF) {
          	bytesRead++;
          	cpoke(addr++, dataByte);
          	if (addr == 0) {                    /** Break if address wraps around to the start of memory **/
                fclose(dataFile);
            	break;
          	}
        }
        fclose(dataFile);
		status = STATUS_READY;
      }
    //}
  }
  return status;
}

void catLoad(String filename, String startAddress, bool silent) {
	unsigned int startAddr;
	int status = STATUS_DEFAULT;
	if (startAddress == "") {
		startAddr = config_code_start;	/** If not otherwise specified, load file into start of code area **/
	}
	else {
		startAddr = strtol(startAddress.c_str(), NULL, 16);	/** Convert address string to hexadecimal number **/
	}
	status = load(filename, startAddr);
	if(!silent) {
		cprintStatus(status);
	}
}

void runCode() {
  byte runL = config_code_start & 0xFF;
  byte runH = config_code_start >> 8;

  ccls();
  /** REMEMBER:                           **/
  /** Byte at config_outbox_flag is the new mail flag **/
  /** Byte at config_outbox_data is the mail box      **/
  cpoke(config_outbox_flag, 0x00);	/** Reset outbox mail flag	**/
  cpoke(config_outbox_data, 0x00);	/** Reset outbox mail data	**/
  cpoke(config_inbox_flag, 0x00);	/** Reset inbox mail flag	**/
  if (!mode) {            /** We are in 6502 mode **/
    /** Non-maskable interrupt vector points to 0xFCB0, just after video area **/
    cpoke(0xFFFA, 0xB0);
    cpoke(0xFFFB, 0xFC);
    /** The interrupt service routine simply returns **/
    // FCB0        RTI             40
    cpoke(0xFCB0, 0x40);
    /** Set reset vector to config_code_start, the beginning of the code area **/
    cpoke(0xFFFC, runL);
    cpoke(0xFFFD, runH);
  } else {                /** We are in Z80 mode **/
    /** The NMI service routine of the Z80 is at 0x0066 **/
    /** It simply returns **/
    // 0066   ED 45                  RETN 
    cpoke(0x0066, 0xED);
    cpoke(0x0067, 0x45);
    /** The Z80 fetches the first instruction from 0x0000, so put a jump to the code area there **/
    // 0000   C3 ll hh               JP   config_code_start
	#if config_code_start != 0x0000
    cpoke(0x0000, 0xC3);
    cpoke(0x0001, runL);
    cpoke(0x0002, runH);
	#endif
  }
  cpu_reset();
  cpurunning = true;
  //digitalWrite(CPURST, HIGH); /** Reset the CPU **/
  //digitalWrite(CPUGO, HIGH);  /** Enable CPU buses and clock **/
  //delay(50);
  //digitalWrite(CPURST, LOW);  /** CPU should now initialize and then go to its reset vector **/
  #if config_enable_nmi == 1
  //Timer1.initialize(20000);
  //Timer1.attachInterrupt(cpuInterrupt); /** Interrupt every 0.02 seconds - 50Hz **/
  #endif
}
/************************************************************************************************/
void enter() {  /** Called when the user presses ENTER, unless a CPU program is being executed **/
/************************************************************************************************/
  unsigned int addr;                /** Memory addresses **/
  byte data;                        /** A byte to be stored in memory **/
  byte i;                           /** Just a counter **/
  String nextWord, nextNextWord, nextNextNextWord; /** General-purpose strings **/
  nextWord = getNextWord(true);     /** Get the first word in the edit line **/
  nextWord.toLowerCase();           /** Ignore capitals **/
  if( nextWord.length() == 0 ) {    /** Ignore empty line **/
    //Serial.println(F("OK"));
    return;
  }
  /** MANUAL ENTRY OF OPCODES AND DATA INTO MEMORY *******************************************/
  if ((nextWord.charAt(0) == '0') && (nextWord.charAt(1) == 'x')) { /** The user is entering data into memory **/
    nextWord.remove(0,2);                       /** Removes the "0x" at the beginning of the string to keep only a HEX number **/
    addr = strtol(nextWord.c_str(), NULL, 16);  /** Converts to HEX number type **/
    nextNextWord = getNextWord(false);          /** Get next byte **/
    byte chkA = 1;
    byte chkB = 0;
    while (nextNextWord != "") {                /** For as long as user has typed in a byte, store it **/
      if(nextNextWord.charAt(0) != '#') {
        data = strtol(nextNextWord.c_str(), NULL, 16);/** Converts to HEX number type **/
        while( cpeek(addr) != data ) {          /** Serial comms may cause writes to be missed?? **/
          cpoke(addr, data);
        }
        chkA += data;
        chkB += chkA;
        addr++;
      }
      else {
        nextNextWord.remove(0,1);
        addr = strtol(nextNextWord.c_str(), NULL, 16);
        if( addr != ((chkA << 8) | chkB) ) {
          cprintString(28, 26, nextWord);
          tone(SOUND, 50, 50);
        }
      }
      nextNextWord = getNextWord(false);  /** Get next byte **/
    }
    #if config_dev_mode == 0
    cprintStatus(STATUS_READY);
    cprintString(28, 27, nextWord);
    #endif
#if 0
    Serial.print(nextWord);
    Serial.print(' ');
    Serial.println((uint16_t)((chkA << 8) | chkB), HEX);
#endif /* 0 */
    
  /** LIST ***********************************************************************************/
  } else if (nextWord == F("list")) {     /** Lists contents of memory in compact format **/
    cls();
    nextWord = getNextWord(false);        /** Get address **/
    list(nextWord);
    cprintStatus(STATUS_READY);
  /** CLS ************************************************************************************/
  } else if (nextWord == F("cls")) {      /** Clear the main window **/
    cls();
    cprintStatus(STATUS_READY);
  /** TESTMEM ********************************************************************************/
  } else if (nextWord == F("testmem")) {  /** Checks whether all four memories can be written to and read from **/
    cls();
    testMem();
    cprintStatus(STATUS_READY);
  /** 6502 ***********************************************************************************/
  } else if (nextWord == F("6502")) {     /** Switches to 6502 mode **/
    mode = false;
    //EEPROM.write(config_eeprom_address_mode,0);
    //digitalWrite(CPUSLC, LOW);            /** Tell CAT of the new mode **/
    cprintStatus(STATUS_READY);
  /** Z80 ***********************************************************************************/
  } else if (nextWord == F("z80")) {      /** Switches to Z80 mode **/
    mode = true;
    //EEPROM.write(config_eeprom_address_mode,1);
    //digitalWrite(CPUSLC, HIGH);           /** Tell CAT of the new mode **/
    cprintStatus(STATUS_READY);
  /** RESET *********************************************************************************/
  } else if (nextWord == F("reset")) {
    //pinMode(SOUND, INPUT);  /** Avoids annoying beep upon software reset **/
    resetFunc();						  /** This resets CAT and, therefore, the CPUs too **/
  /** FAST **********************************************************************************/
  } else if (nextWord == F("fast")) {     /** Sets CPU clock at 8 MHz **/
    //digitalWrite(CPUSPD, HIGH);
    fast = true;
    //EEPROM.write(config_eeprom_address_speed,1);
    cprintStatus(STATUS_READY);
  /** SLOW **********************************************************************************/
  } else if (nextWord == F("slow")) {     /** Sets CPU clock at 4 MHz **/
    //digitalWrite(CPUSPD, LOW);
    fast = false;
    //EEPROM.write(config_eeprom_address_speed,0);
    cprintStatus(STATUS_READY);
  /** DIR ***********************************************************************************/
  } else if (nextWord == F("dir")) {      /** Lists files on uSD card **/
    dir();
  /** DEL ***********************************************************************************/
  } else if (nextWord == F("del")) {      /** Deletes a file on uSD card **/
    nextWord = getNextWord(false);
    catDelFile(nextWord);
  /** LOAD **********************************************************************************/
  } else if (nextWord == F("load")) {     /** Loads a binary file into memory, at specified location **/
    nextWord = getNextWord(false);        /** Get the file name from the edit line **/
    nextNextWord = getNextWord(false);    /** Get memory address **/
    catLoad(nextWord, nextNextWord, false);
  /** RUN ***********************************************************************************/
  } else if (nextWord == F("run")) {      /** Runs the code in memory **/
    for (i = 0; i < 38; i++) previousEditLine[i] = editLine[i]; /** Store edit line just executed **/
    runCode();
  /** SAVE **********************************************************************************/
  } else if (nextWord == F("basic6502")) {
    mode = false;
    //digitalWrite(CPUSLC, LOW);
    catLoad("basic65.bin","", false); 
    runCode();
  } else if (nextWord == F("basicz80")) {
    mode = true;
    //digitalWrite(CPUSLC, HIGH);
    catLoad("basicz80.bin","", false); 
    runCode();
  } else if (nextWord == F("save")) {
    nextWord = getNextWord(false);						/** Start start address **/
    nextNextWord = getNextWord(false);					/** End address **/
    nextNextNextWord = getNextWord(false);				/** Filename **/
    catSave(nextNextNextWord, nextWord, nextNextWord);
  /** MOVE **********************************************************************************/
  } else if (nextWord == F("move")) {
    nextWord = getNextWord(false);
    nextNextWord = getNextWord(false);
    nextNextNextWord = getNextWord(false);
    binMove(nextWord, nextNextWord, nextNextNextWord);
  /** HELP **********************************************************************************/
  } else if ((nextWord == F("help")) || (nextWord == F("?"))) {
    help();
    cprintStatus(STATUS_POWER);
  /** ALL OTHER CASES ***********************************************************************/
  } else cprintStatus(STATUS_UNKNOWN_COMMAND);
  if (!cpurunning) {
    storePreviousLine();
    clearEditLine();                   /** Reset edit line **/
  }
}

void stopCode() {
    cpurunning = false;         /** Reset this flag **/
#if 0
    Timer1.detachInterrupt();
    digitalWrite(CPURST, HIGH); /** Reset the CPU to bring its output signals back to original states **/ 
    digitalWrite(CPUGO, LOW);   /** Tristate its buses to high-Z **/
#endif /* 0 */
    delay(50);                   /** Give it some time **/
#if 0
    digitalWrite(CPURST, LOW);  /** Finish reset cycle **/
#endif /* 0 */

    load("chardefs.bin", 0xf000);/** Reset the character definitions in case the CPU changed them **/
    ccls();                     /** Clear screen completely **/
    cprintFrames();             /** Reprint the wire frame in case the CPU code messed with it **/
    cprintBanner();
    cprintStatus(STATUS_DEFAULT);            /** Update status bar **/
    clearEditLine();            /** Clear and display the edit line **/
}

// CPU Interrupt Routine (50hz)
//
void cpuInterrupt(void) {
  	if(cpurunning) {							// Only run this code if cpu is running 
	   	//digitalWrite(CPUIRQ, HIGH);		 		// Trigger an NMI interrupt
	   	//digitalWrite(CPUIRQ, LOW);
        if (mode) { cpu_z80_nmi(); } else { cpu_6502_nmi(); }
  	}
	interruptFlag = true;
}

// Handle LOAD command from BASIC
//
int cmdLoad(unsigned int address) {
  int result;
  unsigned int startAddr = cpeekW(address);
  cpeekStr(address + 4, editLine, 38);
  result = load((char *)editLine, startAddr);
  cpokeW(address+2, bytesRead);
  
  return result;
}

// Handle SAVE command from BASIC
//
int cmdSave(unsigned int address) {
	unsigned int startAddr = cpeekW(address);
	unsigned int length = cpeekW(address + 2);
	cpeekStr(address + 4, editLine, 38);
	return save((char *)editLine, startAddr, startAddr + length - 1);
}

DIR *cd;
// Handle CAT command from BASIC
//
int cmdCatOpen(unsigned int address) {
	cd = opendir(SDCARD_MOUNT_PATH);
	return STATUS_READY;
}

int cmdCatEntry(unsigned int address) {		// Subsequent calls to this will read the directory entries
	struct dirent *entry;
	entry = readdir(cd);				// Open the next file
	if(!entry) {							// If we've read past the last file in the directory
		closedir(cd);
		return STATUS_EOF;					// And return end of file
	}
    int filesize = get_file_size(entry->d_name);
	cpokeL(address, filesize);			// First four bytes are the length
	cpokeStr(address + 4, entry->d_name);	// Followed by the filename, zero terminated
	//entry.close();							// Close the directory entry
	return STATUS_READY;					// Return READY
}

// Handle ERASE command from BASIC
//
int cmdDelFile(unsigned int address) {
	cpeekStr(address, editLine, 38);
	return delFile((char *)editLine);	
}

// Inbox message handler
//
void messageHandler(void) {
  	int	flag, status;
  	byte retVal = 0x00;							// Return status; default is OK
  	unsigned int address;						// Pointer for data

 	if(cpurunning) {							// Only run this code if cpu is running 
	 	cpurunning = false;						// Just to prevent interrupts from happening
		//digitalWrite(CPUGO, LOW); 				// Pause the CPU and tristate its buses to high-Z
		flag = cpeek(config_inbox_flag);		// Fetch the inbox flag 
                                                //
        if(flag > 0 && flag < 0x80) {
#ifdef DEBUG
            debug_log("msg 0x%x\r\n", flag);
#endif /* DEBUG */

			address = cpeekW(config_inbox_data);
			switch(flag) {
				case 0x01:
					//cmdSound(address);
					break;
				case 0x02: 
					status = cmdLoad(address);
					if(status != STATUS_READY) {
						retVal = (byte)(status + 0x80);
					}
					break;
				case 0x03:
					status = cmdSave(address);
					if(status != STATUS_READY) {
						retVal = (byte)(status + 0x80);
					}
					break;
				case 0x04:
					status = cmdDelFile(address);
					if(status != STATUS_READY) {
						retVal = (byte)(status + 0x80);
					}
					break;
				case 0x05:
					status = cmdCatOpen(address);
					if(status != STATUS_READY) {
						retVal = (byte)(status + 0x80);
					}
					break;
				case 0x06:
					status = cmdCatEntry(address);
					if(status != STATUS_READY) {
						retVal = (byte)(status + 0x80);
					}
					break;
#if 0
                case 0x7E:
                  cmdSoundNb(address);
                  status = STATUS_READY;
                  break;
#endif /* 0 */
                        case 0x7F:
                            resetFunc();
                            break;
			}
			cpoke(config_inbox_flag, retVal);	// Flag we're done - values >= 0x80 are error codes
		}
		//digitalWrite(CPUGO, HIGH);   			// Restart the CPU 
		cpurunning = true;
 	}
}

void cat_loop()
{
    // wait vblank
    //ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	int ascii = readKey();	/** Stores ascii value of key pressed **/
	byte i;     			/** Just a counter **/

  	/** Wait for a key to be pressed, then take it from there... **/
	if(ascii != 0) {
    	if(cpurunning) {
    		if (ascii == PS2_F12) {  /** This happens if F1 has been pressed... and so on... **/
	    		stopCode();
    		}
    		//else {
            else if (ascii > 0) {
				cpurunning = false;						/** Just stops interrupts from happening **/
        		//digitalWrite(CPUGO, LOW);   			/** Pause the CPU and tristate its buses to high-Z **/
      			byte mode = cpeek(config_outbox_flag);
      			cpoke(config_outbox_data, ascii);       /** Put token code of pressed key in the CPU's mailbox, at config_outbox_data **/
	         	cpoke(config_outbox_flag, 0x01);		/** Flag that there is new mail for the CPU waiting at the mailbox **/
      			//digitalWrite(CPUGO, HIGH);  			/** Let the CPU go **/
				cpurunning = true;
      			#if config_enable_nmi == 0
      			//digitalWrite(CPUIRQ, HIGH); /** Trigger an interrupt **/
      			//digitalWrite(CPUIRQ, LOW);
      			#endif
    		}
    	}
    	else {
	 	   switch(ascii) {
	    		case PS2_ENTER:
		    		enter();
	    			break;
	    		case PS2_UPARROW:
		     	   	for (i = 0; i < 38; i++) editLine[i] = previousEditLine[i];
	        		i = 0;
        			while (editLine[i] != 0) i++;
        			pos = i;
        			cprintEditLine();
        			break;
        		case PS2_DOWNARROW:
		        	clearEditLine();
        			break;
        		case PS2_DELETE:
        		case PS2_LEFTARROW:
			        editLine[pos] = 32; /** Put an empty space in current cursor position **/
        			if (pos > 1) pos--; /** Update cursor position, unless reached left-most position already **/
        			editLine[pos] = 0;  /** Put cursor on updated position **/
        			cprintEditLine();   /** Print the updated edit line **/
        			break;
        		default:
                    if (ascii < 0) break;
		        	editLine[pos] = ascii;  /** Put new character in current cursor position **/
        			if (pos < 37) pos++;    /** Update cursor position **/
        			editLine[pos] = 0;      /** Place cursor to the right of new character **/
    				#if config_dev_mode == 0        	
		       		cprintEditLine();       /** Print the updated edit line **/
	       			#endif
        			break;        
			}
		}    
	}
	if(interruptFlag) {						/** If the interrupt flag is set then **/
		interruptFlag = false;
		messageHandler();					/** Run the inbox message handler **/
	}
#if 0
	// Now we deal with bus access requests from the expansion card
  if (digitalRead(XBUSREQ) == LOW) { // The expansion card is requesting bus access... 
    if (cpurunning) { // If a CPU is running (the internal buses being therefore not tristated)...
      digitalWrite(CPUGO, LOW); // ...first pause the CPU and tristate the buses...
      digitalWrite(XBUSACK, LOW); // ...then acknowledge request; buses are now under the control of the expansion card
      while (digitalRead(XBUSREQ) == LOW); // Wait until the expansion card is done...
      digitalWrite(XBUSACK, HIGH); // ...then let the expansion card know that the buses are no longer available to it
      digitalWrite(CPUGO, HIGH); // Finally, let the CPU run again
    } else { // If a CPU is NOT running...
      digitalWrite(XBUSACK, LOW); // Acknowledge request; buses are now under the control of the expansion card
      while (digitalRead(XBUSREQ) == LOW); // Wait until the expansion card is done...
      digitalWrite(XBUSACK, HIGH); // ...then let the expansion card know that the buses are no longer available to it
    }
  }
  // And finally, deal with the expansion flag (which will be 'true' if there has been an XIRQ strobe from an expansion card)
  if (expflag) {
    if (cpurunning) {
      digitalWrite(CPUGO, LOW); // Pause the CPU and tristate its buses **/
      cpoke(0xEFFF, 0x01); // Flag that there is data from the expansion card waiting for the CPU in memory
      digitalWrite(CPUGO, HIGH); // Let the CPU go
      digitalWrite(CPUIRQ, HIGH); // Trigger an interrupt so the CPU knows there's data waiting for it in memory
      digitalWrite(CPUIRQ, LOW);
    }
    expflag = false; // Reset the flag
  }
#endif /* 0 */
}

void debug_log(const char *format, ...) {
	#if DEBUG == 1
	va_list ap;
	va_start(ap, format);
	auto size = vsnprintf(nullptr, 0, format, ap) + 1;
	if (size > 0) {
		va_end(ap);
		va_start(ap, format);
		char buf[size + 1];
		vsnprintf(buf, size, format, ap);
		Serial.print(buf);
	}
	va_end(ap);
	#endif
}

void loop()
{
    char buf[64];
    int64_t t = esp_timer_get_time();
    for (;;) {
        cpuInterrupt();
        cat_loop();
        cpu_clockcycles(fast ? 160000 : 80000); // 8 mhz cycles in 0.02 seconds

        // 50Hz timer (every 0.02 seconds)
        int64_t now;
        do {
            now = esp_timer_get_time();
        } while (now - t < 20000);

#ifdef DEBUG
        debug_log("50Hz real elapsed: %lld\r\n", now - t);
#endif /* DEBUG */
        t = now;
    }
}
