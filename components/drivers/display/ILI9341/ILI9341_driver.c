/*********************
 *      INCLUDES
 *********************/
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/param.h>

#include "ILI9341_driver.h"
#include "system_configuration.h"
#include "backlight_ctrl.h"
#include "LVGL/lvgl.h"

#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

/*********************
 *      DEFINES
 *********************/
#define ILI9341_SPI_QUEUE_SIZE 2
#define MADCTL_MY 0x80
#define MADCTL_MX 0x40
#define MADCTL_MV 0x20
#define MADCTL_ML 0x10
#define MADCTL_MH 0x04
#define TFT_RGB_BGR 0x08

#define TFT_CMD_SWRESET 0x01
#define TFT_CMD_SLEEP 0x10
#define TFT_CMD_DISPLAY_OFF 0x28

/**********************
*      VARIABLES
**********************/
static const char *TAG = "ILI9341_driver";

/**********************
*  STATIC PROTOTYPES
**********************/
static void ILI9341_send_cmd(ili9341_driver_t *driver, const ili9341_command_t *command);
static void ILI9341_config(ili9341_driver_t *driver);
static void ILI9341_pre_cb(spi_transaction_t *transaction);
static void ILI9341_queue_empty(ili9341_driver_t *driver);
static void ILI9341_multi_cmd(ili9341_driver_t *driver, const ili9341_command_t *sequence);


/**********************
 *   GLOBAL FUNCTIONS
 **********************/
bool ILI9341_init(ili9341_driver_t *driver){
    backlight_init();
    backlight_set(100);
    //Allocate the buffer memory
    driver->buffer = (ili9341_color_t *)heap_caps_malloc(driver->buffer_size * 2 * sizeof(ili9341_color_t), MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    if(driver->buffer == NULL){
        ESP_LOGE(TAG, "Display buffer allocation fail");
        return false;
    }

    ESP_LOGI(TAG,"Display buffer allocated with a size of: %i",driver->buffer_size * 2 * sizeof(ili9341_color_t));

    // Why set buffer, primary and secondary instead,just primary and secondary??
    //Set-up the display buffers
    driver->buffer_primary =  driver->buffer;
    driver->buffer_secondary = driver->buffer + driver->buffer_size;
    driver->current_buffer = driver->buffer_primary;
    driver->queue_fill = 0;

    driver->data.driver = driver;
	driver->data.data = true;
	driver->command.driver = driver;
	driver->command.data = false;

    // DC PIN
    
    //PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[HSPI_DC], PIN_FUNC_GPIO);
    gpio_pad_select_gpio(HSPI_DC);
    gpio_set_direction(HSPI_DC, GPIO_MODE_OUTPUT);

    ESP_LOGI(TAG,"Set RST pin: %i \n Set DC pin: %i",HSPI_RST,HSPI_DC);

    // Set-Up SPI BUS
    spi_bus_config_t buscfg = {
		.mosi_io_num    = HSPI_MOSI,
		.miso_io_num    = -1,
		.sclk_io_num    = HSPI_CLK,
		.quadwp_io_num  = -1,
		.quadhd_io_num  = -1,
		.max_transfer_sz= driver->buffer_size * 2 * sizeof(ili9341_color_t), // 2 buffers with 2 bytes for pixel
		.flags          = SPICOMMON_BUSFLAG_NATIVE_PINS
	};

    // Configure SPI BUS
    spi_device_interface_config_t devcfg = {
		.clock_speed_hz = HSPI_CLK_SPEED,
		.mode           = 3,
		.spics_io_num   = 5,
		.queue_size     = ILI9341_SPI_QUEUE_SIZE,
		.pre_cb         = ILI9341_pre_cb,
	};

    if(spi_bus_initialize(VSPI_HOST, &buscfg, 1) != ESP_OK){
        ESP_LOGE(TAG,"SPI Bus initialization failed.");
        return false;
    }

    if(spi_bus_add_device(VSPI_HOST, &devcfg,&driver->spi) != ESP_OK){
        ESP_LOGE(TAG,"SPI Bus add device failed.");
        return false;
    }

    ESP_LOGI(TAG,"SPI Bus configured correctly.");

    // Set the screen configuration
    ILI9341_reset(driver);
    ILI9341_config(driver);

    ESP_LOGI(TAG,"Display configured and ready to work.");

    
    return true;
}


void ILI9341_reset(ili9341_driver_t *driver) {
	const ili9341_command_t sequence[] = {
		{ILI9341_SWRESET, 120, 0, NULL},
		{ILI9341_CMDLIST_END, 0, 0, NULL},
	};

	ILI9341_multi_cmd(driver, sequence);
}


void ILI9341_fill_area(ili9341_driver_t *driver, ili9341_color_t color, uint16_t start_x, uint16_t start_y, uint16_t width, uint16_t height){
    // Fill the buffer with the selected color
	for (size_t i = 0; i < driver->buffer_size * 2; ++i) {
		driver->buffer[i] = color;
	}

    // Set the working area on the screen
	ILI9341_set_window(driver, start_x, start_y, start_x + width - 1, start_y + height - 1);

	size_t bytes_to_write = width * height * 2;
	size_t transfer_size = driver->buffer_size * 2 * sizeof(ili9341_color_t);

	spi_transaction_t trans;
    spi_transaction_t *rtrans;

	memset(&trans, 0, sizeof(trans));
	trans.tx_buffer = driver->buffer;
	trans.user = &driver->data;
	trans.length = transfer_size * 8;
	trans.rxlength = 0;

	
	while (bytes_to_write > 0) {
		if (driver->queue_fill >= ILI9341_SPI_QUEUE_SIZE) {
			spi_device_get_trans_result(driver->spi, &rtrans, portMAX_DELAY);
			driver->queue_fill--;
		}
		if (bytes_to_write < transfer_size) {
			transfer_size = bytes_to_write;
		}
		spi_device_queue_trans(driver->spi, &trans, portMAX_DELAY);
		driver->queue_fill++;
		bytes_to_write -= transfer_size;
	}

	ILI9341_queue_empty(driver);
}

void ILI9341_write_pixels(ili9341_driver_t *driver, ili9341_color_t *pixels, size_t length){
	ILI9341_queue_empty(driver);

	spi_transaction_t *trans = driver->current_buffer == driver->buffer_primary ? &driver->trans_a : &driver->trans_b;
	memset(trans, 0, sizeof(&trans));
	trans->tx_buffer = driver->current_buffer;
	trans->user = &driver->data;
	trans->length = length * sizeof(ili9341_color_t) * 8;
	trans->rxlength = 0;

	spi_device_queue_trans(driver->spi, trans, portMAX_DELAY);
	driver->queue_fill++;
}

void ILI9341_write_lines(ili9341_driver_t *driver, int ypos, int xpos, int width, uint16_t *linedata, int lineCount){
   // ILI9341_set_window(driver,xpos,ypos,240,ypos +20);
    int size = width * 2 * 8 * lineCount;

    //driver->buffer_secondary = linedata;
    //driver->current_buffer = driver->buffer_secondary;


    //ILI9341_write_pixels(driver, driver->buffer_primary, size);
    driver->buffer_size = 240*20; 
    ILI9341_set_window(driver, xpos , ypos, width - 1, ypos + lineCount);
   	// ILI9341_write_pixels(driver, driver->current_buffer, driver->buffer_size);
   	ILI9341_swap_buffers(driver);
}

void ILI9341_swap_buffers(ili9341_driver_t *driver){
	ILI9341_write_pixels(driver, driver->current_buffer, driver->buffer_size);
	driver->current_buffer = driver->current_buffer == driver->buffer_primary ? driver->buffer_secondary : driver->buffer_primary;
}

void ILI9341_set_window(ili9341_driver_t *driver, uint16_t start_x, uint16_t start_y, uint16_t end_x, uint16_t end_y){
	uint8_t caset[4];
	uint8_t raset[4];
    
	caset[0] = (uint8_t)((start_x + 40) >> 8) & 0xFF;
	caset[1] = (uint8_t)((start_x + 40) & 0xff);
	caset[2] = (uint8_t)((end_x + 40) >> 8) & 0xFF;
	caset[3] = (uint8_t)((end_x + 40) & 0xff) ;
	raset[0] = (uint8_t)(start_y >> 8) & 0xFF;
	raset[1] = (uint8_t)(start_y & 0xff);
	raset[2] = (uint8_t)(end_y >> 8) & 0xFF;
	raset[3] = (uint8_t)(end_y & 0xff);

	ili9341_command_t sequence[] = {
		{ILI9341_CASET, 0, 4, caset},
		{ILI9341_PASET, 0, 4, raset},
		{ILI9341_RAMWR, 0, 0, NULL},
		{ILI9341_CMDLIST_END, 0, 0, NULL},
	};

	ILI9341_multi_cmd(driver, sequence);
}

void ILI9341_set_endian(ili9341_driver_t *driver){
	// do nothing, ili9341 doesn't support change endian
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void ILI9341_pre_cb(spi_transaction_t *transaction) {
	const ili9341_transaction_data_t *data = (ili9341_transaction_data_t *)transaction->user;
	gpio_set_level(HSPI_DC, data->data);
}

static void ILI9341_config(ili9341_driver_t *driver){
	const ili9341_command_t init_sequence[] = {
		{ILI9341_SWRESET, 120, 0, NULL},
        {0xEF, 0, 3, (const uint8_t *) "\x03\x80\x02"},      
		{0xCF, 0, 3, (const uint8_t *) "\x00\x83\x30"},
		{0xED, 0, 4, (const uint8_t *) "\x64\x03\x12\x81"},
		{0xE8, 0, 3, (const uint8_t *) "\x85\x01\x79"},
		{0xCB, 0, 5, (const uint8_t *) "\x39\x2c\x00\x34\x02"},
		{0xF7, 0, 1, (const uint8_t *) "\x20"},
		{0xEA, 0, 2, (const uint8_t *) "\x00\x00"},
		{ILI9341_PWCTR1, 0, 1, (const uint8_t *) "\x26"},											  /*Power control*/
		{ILI9341_PWCTR2, 0, 1, (const uint8_t *) "\x11"},											  /*Power control */
		{ILI9341_VMCTR1, 0, 2, (const uint8_t *) "\x35\x3e"},									  /*VCOM control*/
		{ILI9341_VMCTR2, 0, 1, (const uint8_t *) "\xbe"},											  /*VCOM control*/
		{ILI9341_MADCTL, 0, 1, (const uint8_t *) "\x28"}, /*Memory Access Control*/
		{ILI9341_PIXFMT, 0, 1, (const uint8_t *) "\x55"},											  /*Pixel Format Set*/
		{ILI9341_FRMCTR1, 0, 2, (const uint8_t *) "\x00\x1b"},
		{ILI9341_DFUNCTR, 0, 3, (const uint8_t *) "\x08\x82\x27"},
		{0xF2, 0, 1, (const uint8_t *) "\x08"},
		{ILI9341_GAMMASET, 0, 1, (const uint8_t *) "\x01"},
		{ILI9341_GMCTRP1, 0, 15, (const uint8_t *) "\x1f\x1a\x18\x0a\x0f\x06\x45\x87\x32\x0a\x07\x02\x07\x05\x00"},
		{ILI9341_GMCTRN1, 0, 15, (const uint8_t *) "\x00\x25\x27\x05\x10\x09\x3a\x78\x4d\x05\x18\x0d\x38\x3a\x1f"},
		{0x2A, 0, 4, (const uint8_t *) "\x00\x00\x00\xEF"},
		{0x2B, 0, 4, (const uint8_t *) "\x00\x00\x01\x3f"},
		{0x2C, 0, 0, NULL},
		{0xB7, 0, 1, (const uint8_t *) "\x07"},
		{0xB6, 0, 4, (const uint8_t *) "\x0A\x82\x27\x00"},
		{0x11, 120, 0, NULL},
		{0x29, 10, 0, NULL},
		{ILI9341_CMDLIST_END, 0, 0, NULL},                   // End of commands		
	};

	ILI9341_multi_cmd(driver, init_sequence);
	ILI9341_fill_area(driver, 0x0000, 0, 0, driver->display_width, driver->display_height);
}

static void ILI9341_send_cmd(ili9341_driver_t *driver, const ili9341_command_t *command){
    spi_transaction_t *return_trans;
	spi_transaction_t data_trans;
    
    // Check if the SPI queue is empty
    ILI9341_queue_empty(driver);

    // Send the command
	memset(&data_trans, 0, sizeof(data_trans));
	data_trans.length = 8; // 8 bits
	data_trans.tx_buffer = &command->command;
	data_trans.user = &driver->command;

	spi_device_queue_trans(driver->spi, &data_trans, portMAX_DELAY);
	spi_device_get_trans_result(driver->spi, &return_trans, portMAX_DELAY);

    // Send the data if the command has.
	if (command->data_size > 0) {
		memset(&data_trans, 0, sizeof(data_trans));
		data_trans.length = command->data_size * 8;
		data_trans.tx_buffer = command->data;
		data_trans.user = &driver->data;

		spi_device_queue_trans(driver->spi, &data_trans, portMAX_DELAY);
		spi_device_get_trans_result(driver->spi, &return_trans, portMAX_DELAY);
	}

    // Wait the required time
	if (command->wait_ms > 0) {
		vTaskDelay(command->wait_ms / portTICK_PERIOD_MS);
	}
}

static void ILI9341_multi_cmd(ili9341_driver_t *driver, const ili9341_command_t *sequence){
    while (sequence->command != ILI9341_CMDLIST_END) {
		ILI9341_send_cmd(driver, sequence);
		sequence++;
	}
}

static void ILI9341_queue_empty(ili9341_driver_t *driver){
	spi_transaction_t *return_trans;

	while (driver->queue_fill > 0) {
		spi_device_get_trans_result(driver->spi, &return_trans, portMAX_DELAY);
		driver->queue_fill--;
	}
}

