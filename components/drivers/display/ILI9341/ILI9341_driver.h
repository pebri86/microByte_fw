#pragma once
#include "driver/spi_master.h"
#include "LVGL/lvgl.h"

/*******************************
 *      ILI9341 Commands
 * *****************************/
// All ILI9341 specific commands
#define ILI9341_NOP     	0x00
#define ILI9341_SWRESET 	0x01
#define ILI9341_RDDID   	0x04
#define ILI9341_RDDST   	0x09

#define ILI9341_SLPIN   	0x10
#define ILI9341_SLPOUT  	0x11
#define ILI9341_PTLON   	0x12
#define ILI9341_NORON   	0x13

#define ILI9341_RDMODE  	0x0A
#define ILI9341_RDMADCTL  	0x0B
#define ILI9341_RDPIXFMT  	0x0C
#define ILI9341_RDIMGFMT  	0x0A
#define ILI9341_RDSELFDIAG  0x0F

#define ILI9341_INVOFF  	0x20
#define ILI9341_INVON   	0x21
#define ILI9341_GAMMASET 	0x26
#define ILI9341_DISPOFF 	0x28
#define ILI9341_DISPON  	0x29

#define ILI9341_CASET   	0x2A
#define ILI9341_PASET   	0x2B
#define ILI9341_RAMWR   	0x2C
#define ILI9341_RAMRD   	0x2E

#define ILI9341_PTLAR   	0x30
#define ILI9341_VSCRDEF 	0x33
#define ILI9341_MADCTL  	0x36
#define ILI9341_VSCRSADD 	0x37
#define ILI9341_PIXFMT  	0x3A

#define ILI9341_WRDISBV  	0x51
#define ILI9341_RDDISBV  	0x52
#define ILI9341_WRCTRLD  	0x53

#define ILI9341_FRMCTR1 	0xB1
#define ILI9341_FRMCTR2 	0xB2
#define ILI9341_FRMCTR3 	0xB3
#define ILI9341_INVCTR  	0xB4
#define ILI9341_DFUNCTR 	0xB6

#define ILI9341_PWCTR1  	0xC0
#define ILI9341_PWCTR2  	0xC1
#define ILI9341_PWCTR3  	0xC2
#define ILI9341_PWCTR4  	0xC3
#define ILI9341_PWCTR5  	0xC4
#define ILI9341_VMCTR1  	0xC5
#define ILI9341_VMCTR2  	0xC7

#define ILI9341_RDID4   	0xD3
#define ILI9341_RDINDEX 	0xD9
#define ILI9341_RDID1   	0xDA
#define ILI9341_RDID2   	0xDB
#define ILI9341_RDID3   	0xDC
#define ILI9341_RDIDX   	0xDD // TBC

#define ILI9341_GMCTRP1 	0xE0
#define ILI9341_GMCTRN1 	0xE1

#define ILI9341_MADCTL_MY  	0x80
#define ILI9341_MADCTL_MX  	0x40
#define ILI9341_MADCTL_MV  	0x20
#define ILI9341_MADCTL_ML  	0x10
#define ILI9341_MADCTL_RGB 	0x00
#define ILI9341_MADCTL_BGR 	0x08
#define ILI9341_MADCTL_MH  	0x04

#define ILI9341_CMDLIST_END	0xff // End command (used for command list)

/*******************************
 *      TYPEDEF
 * *****************************/
struct ili9341_driver;

typedef struct ili9341_transaction_data {
	struct ili9341_driver *driver;
	bool data;
} ili9341_transaction_data_t;

typedef uint16_t ili9341_color_t;

typedef struct ili9341_driver {
	int pin_reset;
	int pin_dc;
	int pin_mosi;
	int pin_sclk;
	int spi_host;
	int dma_chan;
	uint8_t queue_fill;
	uint16_t display_width;
	uint16_t display_height;
	spi_device_handle_t spi;
	size_t buffer_size;
	ili9341_transaction_data_t data;
	ili9341_transaction_data_t command;
	ili9341_color_t *buffer;
	ili9341_color_t *buffer_primary;
	ili9341_color_t *buffer_secondary;
	ili9341_color_t *current_buffer;
	spi_transaction_t trans_a;
	spi_transaction_t trans_b;
} ili9341_driver_t;

typedef struct ili9341_command {
	uint8_t command;
	uint8_t wait_ms;
	uint8_t data_size;
	const uint8_t *data;
} ili9341_command_t;


/*********************
 *      FUNCTIONS
 *********************/

/*
 * Function:  ILI9341_init 
 * --------------------
 * 
 * Initialize the SPI peripheral and send the initialization sequence.
 * 
 * Arguments:
 * 	-driver: Screen driver structure.
 * 
 * Returns: True if the initialization suceed otherwise false.
 * 
 */
bool ILI9341_init(ili9341_driver_t *driver);

/*
 * Function:  ILI9341_reset
 * --------------------
 * 
 * Reset the display
 * 
 * Arguments:
 * 	-driver: Screen driver structure.
 * 
 * Returns: Nothing.
 * 
 */
void ILI9341_reset(ili9341_driver_t *driver);

/*
 * Function:  ILI9341_fill_area
 * --------------------
 * 
 * Fill a area of the display with a selected color
 * 
 * Arguments:
 * 	-driver: Screen driver structure.
 * 	-color: 16 Bit hexadecimal color to fill the area.
 * 	-start_x: Start point on the X axis.
 * 	-start_y: Start point on the Y axis.
 * 	-width: Width of the area to be fill.
 * 	-height: Height of the area to be fill.
 * 
 * Returns: Nothing.
 * 
 */
void ILI9341_fill_area(ili9341_driver_t *driver, ili9341_color_t color, uint16_t start_x, uint16_t start_y, uint16_t width, uint16_t height);

/*
 * Function:  ILI9341_write_pixels 
 * --------------------
 * 
 * WIP
 * 
 * Arguments:
 * 	-driver: Screen driver structure.
 * 
 * Returns: Nothing.
 * 
 */
void ILI9341_write_pixels(ili9341_driver_t *driver, ili9341_color_t *pixels, size_t length);

/*
 * Function:  ILI9341_write_lines 
 * --------------------
 * 
 * WIP
 * 
 * Arguments:
 * 	-driver: Screen driver structure.
 * 
 * Returns: Nothing.
 * 
 */
void ILI9341_write_lines(ili9341_driver_t *driver, int ypos, int xpos, int width, uint16_t *linedata, int lineCount);

/*
 * Function:  ILI9341_swap_buffers 
 * --------------------
 * 
 * The driver has two buffer, to allow send and render the image at the same type. This function
 * send the data of the actived buffer and change the pointer of current buffer to the next one.
 * 
 * Arguments:
 * 	-driver: Screen driver structure.
 * 
 * Returns: Nothing.
 * 
 */
void ILI9341_swap_buffers(ili9341_driver_t *driver);

/*
 * Function:  ILI9341_set_window
 * --------------------
 * 
 * This screen allows partial update of the screen, so we can specified which part of the windows is going to change.
 * 
 * Arguments:
 * 	-driver: Screen driver structure.
 * 	-start_x: X axis start point of the refresh zone.
 * 	-start_y: Y axis start point of the refresh zone. 
 *	-end_x: X axis end point of the refresh zone. 
 *	-end_y: Y axis end point of the refresh zone. 

 * Returns: Nothing.
 * 
 */
void ILI9341_set_window(ili9341_driver_t *driver, uint16_t start_x, uint16_t start_y, uint16_t end_x, uint16_t end_y);

/*
 * Function:  ILI9341_set_endian 
 * --------------------
 * 
 * Depper explanation on the display_HAL.h file, but this function change the screen configuration from,
 * little endian message to big endian message.
 * 
 * Arguments:
 * 	-driver: Screen driver structure.
 * 
 * Returns: Nothing.
 * 
 */

void ILI9341_set_endian(ili9341_driver_t *driver);