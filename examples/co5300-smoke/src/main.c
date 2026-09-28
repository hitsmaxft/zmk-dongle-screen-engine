/* SPDX-License-Identifier: MIT */
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#define W 480
#define STRIP 16
static uint16_t pixels[W*STRIP] __aligned(4);

int main(void) {
    const struct device *display=DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if(!device_is_ready(display))return -1;
    struct display_capabilities caps;
    display_get_capabilities(display,&caps);
    if(caps.x_resolution!=W||caps.y_resolution!=W)return -2;
    display_set_brightness(display,120);
    for(int y=0;y<W;y+=STRIP) {
        for(int yy=0;yy<STRIP;yy++)for(int x=0;x<W;x++) {
            uint16_t rgb=(uint16_t)(((x*31/W)<<11)|
                       (((y+yy)*63/W)<<5)|((x+y+yy)*31/(2*W)));
            pixels[yy*W+x]=sys_cpu_to_be16(rgb);
        }
        struct display_buffer_descriptor desc={.width=W,.height=STRIP,
            .pitch=W,.buf_size=sizeof(pixels)};
        if(display_write(display,0,y,&desc,pixels))return -3;
    }
    return 0;
}
