#include "spi.h"

uint8_t SPI1SendByte(uint8_t data) 
{
    m_tx_buf = data;
    m_rx_buf = 0;

    while(nrf_drv_spi_transfer(&spi, &m_tx_buf, 1, &m_rx_buf, 1) != NRF_SUCCESS)
    {
    }
        
    return m_rx_buf;
}

void SPI1_WriteReg(uint8_t address, uint8_t value) 
{
    rc522_cs_reset();
    SPI1SendByte(address);
    SPI1SendByte(value);
    rc522_cs_set();
}

uint8_t SPI1_ReadReg(uint8_t address)
{
  uint8_t val;

  rc522_cs_reset();
  SPI1SendByte(address);
  val = SPI1SendByte(0x00);
  rc522_cs_set();

  return val;
}