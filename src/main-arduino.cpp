#include "cerberus.h"
#include "fabgl.h"
#include "fabglconf.h"
#include "fabutils.h"
#include <Arduino.h>
#include <dirent.h>
#include <string.h>

fabgl::PS2Controller PS2Controller;
fabgl::VGADirectController VGAController;
constexpr int scanlinesPerCallback = 8;

uint8_t cerb_color[8];
void setup_colours()
{
    cerb_color[0] = VGAController.createRawPixel(RGB222(0, 3, 0));
    cerb_color[1] = VGAController.createRawPixel(RGB222(3, 0, 0));
    cerb_color[2] = VGAController.createRawPixel(RGB222(0, 0, 3));
    cerb_color[3] = VGAController.createRawPixel(RGB222(3, 3, 0));
    cerb_color[4] = VGAController.createRawPixel(RGB222(0, 3, 3));
    cerb_color[5] = VGAController.createRawPixel(RGB222(3, 0, 3));
    cerb_color[6] = VGAController.createRawPixel(RGB222(0, 0, 0));
    cerb_color[7] = VGAController.createRawPixel(RGB222(3, 3, 3));
}

void IRAM_ATTR drawCerberusScanline(void* arg, uint8_t* dest, int scanLine)
{
    // vid ram at 0xf800 (40x30 bytes)
    // char ram at 0xf000 (2 KiB)
    const uint8_t bgcolor = cerb_color[6];
    const int char_line = 0xf800 + (scanLine >> 3) * 40;
    int tile_line = 0xf000;

    for (int _line = 0; _line < scanlinesPerCallback; _line++, tile_line++) {
        // Drawing 2 scanlines per call to drawScanline. Since cerberus
        // doubles the scanlines, we duplicate the pixel data onto the second scanline
        for (int col = 0; col < 40; col++) {
            // what tile?
            uint8_t tile_num = cerb_ram[char_line + col];
            // what line in the tile (0-7)
            uint8_t tile_dat = cerb_ram[tile_line + tile_num * 8];
            const uint8_t fgcolor = (tile_num >= 8 && tile_num < 32) ? cerb_color[(tile_num - 8) % 6] : cerb_color[7];

            uint8_t color = tile_dat & 0x80 ? fgcolor : bgcolor;
            int x = col * 8;
            VGA_PIXELINROW(dest, x) = color;
            x++;
            color = tile_dat & 0x40 ? fgcolor : bgcolor;
            VGA_PIXELINROW(dest, x) = color;
            x++;
            color = tile_dat & 0x20 ? fgcolor : bgcolor;
            VGA_PIXELINROW(dest, x) = color;
            x++;
            color = tile_dat & 0x10 ? fgcolor : bgcolor;
            VGA_PIXELINROW(dest, x) = color;
            x++;
            color = tile_dat & 0x8 ? fgcolor : bgcolor;
            VGA_PIXELINROW(dest, x) = color;
            x++;
            color = tile_dat & 0x4 ? fgcolor : bgcolor;
            VGA_PIXELINROW(dest, x) = color;
            x++;
            color = tile_dat & 0x2 ? fgcolor : bgcolor;
            VGA_PIXELINROW(dest, x) = color;
            x++;
            color = tile_dat & 0x1 ? fgcolor : bgcolor;
            VGA_PIXELINROW(dest, x) = color;
        }

        dest += 320;
    }
}

void errPrint(const char* msg)
{
    uint16_t addr = 0xf800;

    while (*msg) {
        cpoke(addr, *msg);
        addr++;
        msg++;
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.print("setup() running on core ");
    Serial.println(xPortGetCoreID());

    PS2Controller.begin(PS2Preset::KeyboardPort0_MousePort1, KbdMode::CreateVirtualKeysQueue);
    PS2Controller.keyboard()->setLayout(&fabgl::UKLayout);
    PS2Controller.keyboard()->setCodePage(fabgl::CodePages::get(1252));
    PS2Controller.keyboard()->enableVirtualKeys(true, true);

    VGAController.begin();
    VGAController.setScanlinesPerCallBack(scanlinesPerCallback);
    VGAController.setDrawScanlineCallback(drawCerberusScanline);
    VGAController.setResolution(QVGA_320x240_60Hz);
    setup_colours();

    const bool sd_mounted = FileBrowser::mountSDCard(false, SDCARD_MOUNT_PATH);

    cat_setup();

    if (!sd_mounted) {
        errPrint("Failed to mount SDCard");
    }
}

int readKey()
{
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
                    switch (key.vk) {
                    case fabgl::VK_F12:
                        return PS2_F12;
                    case fabgl::VK_UP:
                        return PS2_UPARROW;
                    case fabgl::VK_DOWN:
                        return PS2_DOWNARROW;
                    case fabgl::VK_LEFT:
                        return PS2_LEFTARROW;
                    case fabgl::VK_RIGHT:
                        return PS2_RIGHTARROW;
                    default:
                        return -key.vk;
                    }
                }
            }
        }
    }
    return 0;
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
        if (cpurunning) {
            debug_log("CPU clock %lld khz\r\n", (fast ? 8000 : 4000) * 20000 / (now - t));
        }
#endif /* DEBUG */
        t = now;
    }
}

void debug_log(const char* format, ...)
{
#ifdef DEBUG
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
#endif /* DEBUG */
}
