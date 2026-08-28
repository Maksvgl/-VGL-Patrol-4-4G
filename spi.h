#ifndef SPI
#define SPI
#include "stdint.h"
#include "nrf_error.h"
#include "nrf_drv_spi.h"
#include "nrf_gpio.h"

#define rc522_cs_reset() nrf_gpio_pin_clear(27); 
#define rc522_cs_set()   nrf_gpio_pin_set(27);
#define w25_cs_reset()   nrf_gpio_pin_set(22);
#define w25_cs_set()     nrf_gpio_pin_clear(22); 

extern const nrf_drv_spi_t spi;

static uint8_t       m_tx_buf;
static uint8_t       m_rx_buf;

uint8_t SPI1SendByte(uint8_t data);
void SPI1_WriteReg(uint8_t address, uint8_t value);
uint8_t SPI1_ReadReg(uint8_t address);
#endif