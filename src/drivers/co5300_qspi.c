/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT waveshare_co5300_qspi
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

/* CO5300's register preamble is 0x02 + a 24-bit command address. Pixel
 * payload uses 0x32/0x003c00 and four data lines while CS stays asserted.
 * The ESPressif SPI controller provides quad data but not quad commands. */
struct co5300_config {
    struct spi_dt_spec spi;
    struct gpio_dt_spec cs;
    struct gpio_dt_spec reset;
};
struct co5300_data { uint8_t brightness; bool blanked; };

static int transfer(const struct co5300_config *cfg, const void *buf,
                    size_t len, bool quad) {
    struct spi_config spi=cfg->spi.config;
    spi.operation=SPI_OP_MODE_MASTER|SPI_WORD_SET(8)|
                  (quad?SPI_LINES_QUAD:SPI_LINES_SINGLE);
    struct spi_buf segment={.buf=(void *)buf,.len=len};
    struct spi_buf_set set={.buffers=&segment,.count=1};
    return spi_write(cfg->spi.bus,&spi,&set);
}
static int command(const struct device *dev, uint8_t command,
                   const uint8_t *parameters, size_t count) {
    const struct co5300_config *cfg=dev->config;
    uint8_t prefix[4]={0x02,0x00,command,0x00};
    int err=gpio_pin_set_dt(&cfg->cs,1);
    if(err)return err;
    err=transfer(cfg,prefix,sizeof(prefix),false);
    if(!err&&count)err=transfer(cfg,parameters,count,false);
    int end=gpio_pin_set_dt(&cfg->cs,0);
    return err?err:end;
}
static int write_window(const struct device *dev,uint16_t x,uint16_t y,
                        uint16_t width,uint16_t height) {
    uint16_t xe=x+width-1,ye=y+height-1;
    uint8_t area[4]={(uint8_t)(x>>8),(uint8_t)x,
                     (uint8_t)(xe>>8),(uint8_t)xe};
    int err=command(dev,0x2a,area,4);
    if(err)return err;
    area[0]=(uint8_t)(y>>8);area[1]=(uint8_t)y;
    area[2]=(uint8_t)(ye>>8);area[3]=(uint8_t)ye;
    return command(dev,0x2b,area,4);
}
static int co5300_write(const struct device *dev,const uint16_t x,const uint16_t y,
                        const struct display_buffer_descriptor *desc,const void *buf) {
    const struct co5300_config *cfg=dev->config;
    if(!buf||!desc||!desc->width||!desc->height||
       desc->width>desc->pitch||x+desc->width>480||y+desc->height>480||
       desc->buf_size<(size_t)desc->pitch*desc->height*2u)return -EINVAL;
    int err=write_window(dev,x,y,desc->width,desc->height);
    if(err)return err;
    const uint8_t *pixels=buf;
    uint8_t prefix[4]={0x32,0x00,0x3c,0x00};
    err=gpio_pin_set_dt(&cfg->cs,1);
    if(err)return err;
    err=transfer(cfg,prefix,sizeof(prefix),false);
    for(unsigned row=0;!err&&row<desc->height;row++)
        err=transfer(cfg,pixels+(size_t)row*desc->pitch*2u,
                     (size_t)desc->width*2u,true);
    int end=gpio_pin_set_dt(&cfg->cs,0);
    return err?err:end;
}
static int co5300_blanking_on(const struct device *dev) {
    struct co5300_data *data=dev->data;
    int err=command(dev,0x28,NULL,0);
    if(!err)data->blanked=true;
    return err;
}
static int co5300_blanking_off(const struct device *dev) {
    struct co5300_data *data=dev->data;
    int err=command(dev,0x29,NULL,0);
    if(!err)data->blanked=false;
    return err;
}
static int co5300_set_brightness(const struct device *dev,uint8_t brightness) {
    struct co5300_data *data=dev->data;
    int err=command(dev,0x51,&brightness,1);
    if(!err)data->brightness=brightness;
    return err;
}
static void co5300_get_capabilities(const struct device *dev,
                                    struct display_capabilities *caps) {
    (void)dev;memset(caps,0,sizeof(*caps));
    caps->x_resolution=480;caps->y_resolution=480;
    caps->supported_pixel_formats=PIXEL_FORMAT_RGB_565;
    caps->current_pixel_format=PIXEL_FORMAT_RGB_565;
}
static int co5300_set_pixel_format(const struct device *dev,
                                    enum display_pixel_format format) {
    (void)dev;return format==PIXEL_FORMAT_RGB_565?0:-ENOTSUP;
}
static int co5300_init(const struct device *dev) {
    const struct co5300_config *cfg=dev->config;
    struct co5300_data *data=dev->data;
    if(!spi_is_ready_dt(&cfg->spi)||!gpio_is_ready_dt(&cfg->cs)||
       !gpio_is_ready_dt(&cfg->reset))return -ENODEV;
    int err=gpio_pin_configure_dt(&cfg->cs,GPIO_OUTPUT_INACTIVE);
    if(err)return err;
    err=gpio_pin_configure_dt(&cfg->reset,GPIO_OUTPUT_INACTIVE);
    if(err)return err;
    k_sleep(K_MSEC(10));
    gpio_pin_set_dt(&cfg->reset,1);
    k_sleep(K_MSEC(200));
    gpio_pin_set_dt(&cfg->reset,0);
    k_sleep(K_MSEC(200));
    err=command(dev,0x11,NULL,0);
    if(err)return err;
    k_sleep(K_MSEC(120));
    static const struct {uint8_t cmd,value;} init[]={
        {0xfe,0x00},{0xc4,0x80},{0x3a,0x55},{0x53,0x20},
        {0x63,0xff},{0x51,0xd0},{0x58,0x00}};
    for(unsigned i=0;i<sizeof(init)/sizeof(init[0]);i++) {
        err=command(dev,init[i].cmd,&init[i].value,1);
        if(err)return err;
    }
    err=command(dev,0x29,NULL,0);
    if(err)return err;
    data->brightness=0xd0;data->blanked=false;
    k_sleep(K_MSEC(10));
    return 0;
}
static DEVICE_API(display,co5300_api)={
    .blanking_on=co5300_blanking_on,
    .blanking_off=co5300_blanking_off,
    .write=co5300_write,
    .set_brightness=co5300_set_brightness,
    .get_capabilities=co5300_get_capabilities,
    .set_pixel_format=co5300_set_pixel_format,
};
#define CO5300_DEFINE(n) \
    static const struct co5300_config cfg_##n={ \
        .spi=SPI_DT_SPEC_INST_GET(n,SPI_WORD_SET(8),0), \
        .cs=GPIO_DT_SPEC_INST_GET(n,panel_cs_gpios), \
        .reset=GPIO_DT_SPEC_INST_GET(n,reset_gpios), \
    }; \
    static struct co5300_data data_##n; \
    DEVICE_DT_INST_DEFINE(n,co5300_init,NULL,&data_##n,&cfg_##n, \
                          POST_KERNEL,CONFIG_DISPLAY_INIT_PRIORITY,&co5300_api);
DT_INST_FOREACH_STATUS_OKAY(CO5300_DEFINE)
