//#define NOREADDELAY

#ifdef OLDBOARD
  #define LED_RED_PIN    23
  #define LED_BLUE_PIN   24
  #define LED_YELLOW_PIN 25
#else
  #define LED_YELLOW_PIN    23
  #define LED_BLUE_PIN   25
  #define LED_RED_PIN 24
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "nrf_drv_saadc.h"
#include "nrf_drv_ppi.h"
#include "nrf_drv_timer.h"
#include "nordic_common.h"
#include "app_error.h"
#include "app_uart.h"
#include "app_gpiote.h"
#include "ble_db_discovery.h"
#include "nrf_uart.h"                   
#include "app_timer.h"
#include "app_util.h"
#include "nrf_drv_timer.h"
#include "nrf_drv_lpcomp.h"
#include "ble.h"
#include "bsp.h"
#include "nrf_delay.h"
#include "ble_gap.h"
#include "ble_hci.h"
#include "nrf_drv_wdt.h"
#include "nrf_gpio.h"
#include "nrf_gpiote.h"
#include "nrfx_gpiote.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "ble_nus_c.h"
#include "nrf_drv_twi.h"
#include "nrf_ble_gatt.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_ble_scan.h"
#include "time.h"
#include "nrf_drv_spi.h"
#include "nrf_drv_rng.h"

#include "flashconst.h"

#include "nrf_crypto.h"
#include "nrf_crypto_error.h"
#include "mem_manager.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "spi.h"
#include "rc522.h"
#include "w25.h"

#include "commands.h"

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0') 
    
#define MASTER_TWI_INST     1
static const nrf_drv_twi_t m_twi_master = NRF_DRV_TWI_INSTANCE(MASTER_TWI_INST);
static uint32_t              m_adc_evt_counter;
#define SAMPLES_IN_BUFFER 5
static nrf_saadc_value_t     m_buffer_pool;

uint16_t conn_handle;
uint32_t currentMFRC522Delay = 0;
uint8_t addr0 = 0x00;

bool justSwitchOffLeds;
bool dataSessionBlinkingEnabled;

bool pleaseSwitchOffLedsAfterVibroAndStartDelayTimer = false;
bool isDoubleUID = false;
bool isOnCoilNow = false;

bool isUpdateStarted = false;

float battVolt_saved;

uint8_t AES_KEY[16] =  {0xA3, 0x81, 0x52, 0xC9, 0x03, 0x55, 0xCC, 0x63, 0xAC, 0x6E, 0x97, 0xA3, 0x99, 0x80, 0x7B, 0x59};

#ifdef CARD1
  uint8_t SL3_SECTOR_DATA[16] =  {'V', 'G', 'L', 'P', 'A', 'T', 'R', 'O', 'L', '4', '_', '0', '0', '0', '0', '1'}; // первый тип метки
#else 
  #ifdef CARD2
    uint8_t SL3_SECTOR_DATA[16] =  {'V', 'G', 'L', 'P', 'A', 'T', 'R', 'O', 'L', '4', '_', '0', '0', '0', '0', '2'}; // второй тип метки
  #endif
#endif

uint32_t currentFwBlock = 0;
uint32_t fwSize = 0;

uint8_t iv[16];
uint8_t CARD_NUM[7];

uint8_t device_secret[16] = {'0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0'};
uint8_t device_serial[6] = {'0', '0', '0', '0', '0', '0'};
uint8_t device_type = 1; // 0 - unknown, 1 - VGL4, 2 - VGL4G

char m_target_periph_name[9] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00}; //5,6 blocks of mifare ultralight

#define BATT_ADC_MULTIPLIER 2

#define SPI_INSTANCE  2                                                 /**< SPI instance index. */
const nrf_drv_spi_t spi = NRF_DRV_SPI_INSTANCE(SPI_INSTANCE);           /**< SPI instance. */

#define APP_BLE_CONN_CFG_TAG    1                                       /**< Tag that refers to the BLE stack configuration set with @ref sd_ble_cfg_set. The default tag is @ref BLE_CONN_CFG_TAG_DEFAULT. */
#define APP_BLE_OBSERVER_PRIO   3                                       /**< BLE observer priority of the application. There is no need to modify this value. */
#define UART_TX_BUF_SIZE        256                                     /**< UART TX buffer size. */
#define UART_RX_BUF_SIZE        256                                     /**< UART RX buffer size. */
#define NUS_SERVICE_UUID_TYPE   BLE_UUID_TYPE_VENDOR_BEGIN              /**< UUID type for the Nordic UART Service (vendor specific). */

#define SetBit(reg, bit)        reg |= (1<<bit)           
#define ClearBit(reg, bit)      reg &= (~(1<<bit))
#define InvBit(reg, bit)        reg ^= (1<<bit)
#define BitIsSet(reg, bit)      ((reg & (1<<bit)) != 0)
#define BitIsClear(reg, bit)    ((reg & (1<<bit)) == 0)

#define CLOCKADDR 0x68
#define EEPROMADDR 0x50

uint8_t i2c_array[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

bool bleWaitingForDeatach = false;
bool isActivated;
bool isPowerSavingModeEnabled = false;

APP_TIMER_DEF(longWD_timer);
APP_TIMER_DEF(mfrc522_timer);
APP_TIMER_DEF(motor_timer);
APP_TIMER_DEF(readDelay_timer);
APP_TIMER_DEF(dataBlinking_timer);
APP_TIMER_DEF(battVoltageUpdate_timer);

uint8_t lastSAK;
uint8_t lastAntiColl;

nrf_drv_wdt_channel_id m_channel_id;

extern uint8_t  m_tx_buf;
extern uint8_t  m_rx_buf;

BLE_NUS_C_DEF(m_ble_nus_c);                                             /**< BLE Nordic UART Service (NUS) client instance. */
NRF_BLE_GATT_DEF(m_gatt);                                               /**< GATT module instance. */
BLE_DB_DISCOVERY_DEF(m_db_disc);                                        /**< Database discovery module instance. */
NRF_BLE_SCAN_DEF(m_scan);                                               /**< Scanning Module instance. */

uint8_t		str[MFRC522_MAX_LEN];	
uint8_t		lastID[4];
uint8_t         value[16];

const mbedtls_cipher_info_t *cipher_info;
const uint8_t fwVersionMajor = 1;
const uint8_t fwVersionMinor = 5;

uint8_t buff[244];

uint16_t eventsToDownload;
uint16_t tmp_eventsToDownload;

uint16_t alarmsRWPointer;
uint16_t alarmsDownloadCounter;

uint8_t maxAlarmsEverCount = 0;

bool readDelayFlag;
bool isBatteryDischargedFlag;

static uint16_t m_ble_nus_max_data_len = BLE_GATT_ATT_MTU_DEFAULT - OPCODE_LENGTH - HANDLE_LENGTH; /**< Maximum length of data (in bytes) that can be transmitted to the peer by the Nordic UART service module. */

float readBattVolts_current();

//w25
void w25q128_spi_send(uint8_t data);
uint8_t w25q128_read();
void w25q128_reset();
uint8_t w25q128_read_status_1();
uint8_t w25q128_ident();
uint8_t w25q128_chip_erase();
uint8_t w25q128_read_buf(uint8_t* data, uint32_t addr, size_t len);
uint8_t w25q128_page_programm(uint8_t* data, uint32_t addr);
uint8_t w25q128_sector_programm(uint8_t* data, uint32_t addr);
uint8_t w25q128_checkFlashInit();
uint8_t w25q128_sector4K_erase(uint32_t addr);
uint8_t w25q128_sector64K_erase(uint32_t addr);

uint8_t W25Q_CheckSector ( uint32_t addr, uint8_t *apDest );
uint8_t W25Q_Write ( uint32_t aAdr, const void *apSource, uint32_t aSize );

uint8_t readEvent(int eventNumber, char* text);
uint8_t writeEvent(int eventNumber, uint8_t seconds, uint8_t minutes, uint8_t hours, uint8_t date, uint8_t months, uint8_t years, uint8_t cardNum);

uint8_t readAlarm(int alarmNumber, char* text);
uint8_t writeAlarm(int alarmNumber, uint8_t seconds, uint8_t minutes, uint8_t hours, uint8_t date, uint8_t months, uint8_t years);

uint8_t increaseEventCounter();
uint8_t decreaseEventCounter();

uint8_t resetEventCounter();

uint8_t increaseAlarmsPointer();

void disableShockSensor();
void enableShockSensor();

void doVibrate(uint16_t time);
void restartReaderTimer();
void doFirstBootInit();

ret_code_t readClock(uint8_t *secs, uint8_t *mins, uint8_t *hours, uint8_t *date, uint8_t *months, uint8_t *years); // 0 - OK 1 - ERROR READING 2 - OSCILLATOR FAILED
ret_code_t writeClock(uint8_t secs, uint8_t mins, uint8_t hours, uint8_t date, uint8_t months, uint8_t years); // 0 - OK 1 - ERROR
ret_code_t writeClockRegister(uint8_t reg, uint8_t value);

static void longWD_timer_handler(void * p_context);
static void mfrc522_timer_handler(void * p_context);
static void dataBlinking_timer_handler(void * p_context);
static void motor_timer_handler(void * p_context);
static void readDelay_timer_handler(void * p_context);
static void battVoltageUpdate_timer_handler(void * p_context);
static void input_pin_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action);

uint8_t settingsCommit();
uint8_t settingsRead();
uint8_t stateWrite();
uint8_t stateRead();

static void lpcomp_event_handler(nrf_lpcomp_event_t event)
{
  if (event == NRF_LPCOMP_EVENT_DOWN)
  {
    isPowerSavingModeEnabled = true;
    nrf_drv_lpcomp_disable();
  }
}

void wdt_event_handler(void)
{
  //NOTE: The max amount of time we can spend in WDT interrupt is two cycles of 32768[Hz] clock - after that, reset occurs
}

void doFirstBootInit()
{
  uint16_t marker = 0xBAAD;
  uint32_t tmp = FW_IMAGE_START_ADDR;
  
  w25q128_sector4K_erase(0x0);

  W25Q_Write(IS_FIRST_BOOT_MARKER_ADDR, &marker, IS_FIRST_BOOT_MARKER_SIZE);
  
  marker = 0x0000;
  
  W25Q_Write(IS_NEW_FIRMWARE_MARKER_ADDR, &marker, IS_NEW_FIRMWARE_MARKER_SIZE);
  //W25Q_Write(DEVICE_TYPE_ADDR, &device_type, DEVICE_TYPE_SIZE);
  W25Q_Write(IS_ACTIVATED_ADDR, &isActivated, IS_ACTIVATED_SIZE);
  
  W25Q_Write(FW_IMAGE_POINTER_ADDR, &tmp, FW_IMAGE_POINTER_SIZE);
}

bool checkIsFirstBoot()
{
  uint16_t marker = 0;
  
  w25q128_read_buf((uint8_t *)&marker, IS_FIRST_BOOT_MARKER_ADDR, IS_FIRST_BOOT_MARKER_SIZE); 
  
  if (marker == 0xBAAD)
  {
    return false;
  }
  
  return true;
}

void LedAllShort()
{
  nrf_gpio_pin_set(LED_RED_PIN);
  nrf_gpio_pin_set(LED_BLUE_PIN);
  nrf_gpio_pin_set(LED_YELLOW_PIN);
  nrf_delay_ms(20);
  nrf_gpio_pin_clear(LED_RED_PIN);
  nrf_gpio_pin_clear(LED_BLUE_PIN);
  nrf_gpio_pin_clear(LED_YELLOW_PIN);
  nrf_delay_ms(20);
}

void LedShort(uint8_t pin)
{
  nrf_gpio_pin_set(pin);
  nrf_delay_ms(20);
  nrf_gpio_pin_clear(pin);
  nrf_delay_ms(20);
}

void LedLong(uint8_t pin)
{
  nrf_gpio_pin_set(pin);
  nrf_delay_ms(1200);
  nrf_gpio_pin_clear(pin);
}

uint8_t w25q128_chip_erase()
{
  Status_reg_1_t r1;
  
  w25_cs_set();
  w25q128_spi_send(W25_WRITE_ENABLE);
  w25_cs_reset();
  
  r1.all = w25q128_read_status_1();
  
  if (r1.bit.write_enable != 1)
    return NRF_ERROR_INTERNAL;
  
  w25_cs_set();
  w25q128_spi_send(W25_CHIP_ERASE);
  w25_cs_reset();
  
  return NRF_SUCCESS;
}

uint8_t w25q128_read()
{
    m_rx_buf = 0;

    while(nrf_drv_spi_transfer(&spi, &m_tx_buf, 0, &m_rx_buf, 1) != NRF_SUCCESS)
    {
    }
    
    return m_rx_buf;
}

void w25q128_spi_send(uint8_t data)
{
    m_tx_buf = data;
    m_rx_buf = 0;

    while(nrf_drv_spi_transfer(&spi, &m_tx_buf, 1, &m_rx_buf, 0) != NRF_SUCCESS)
    {
    }
    
    return;
}

void w25q128_reset()
{
  Status_reg_1_t r1;
  
  w25_cs_set();
  w25q128_spi_send(W25_ENABLE_RESET);
  w25_cs_reset();
 
  w25_cs_set();
  w25q128_spi_send(W25_RESET);
  w25_cs_reset();
    
  do
  {
    r1.all = w25q128_read_status_1();
  }
  while(r1.bit.busy);
}

uint8_t w25q128_disable_QE2()
{
  uint8_t data = 0x00;
  
  w25_cs_set();
  w25q128_spi_send(W25_WRITE_ENABLE);
  w25_cs_reset();
  
  w25_cs_set();
  w25q128_spi_send(W25_WRITE_STATUS_1);
  w25q128_spi_send(data);
  w25q128_spi_send(data);
  w25_cs_reset();
    
  return NRF_SUCCESS;
}

uint8_t w25q128_disable_QE()
{
  uint8_t data = 0x00;
  volatile Status_reg_1_t r1;

  w25_cs_set();
  w25q128_spi_send(W25_WRITE_ENABLE);
  w25_cs_reset();
  
  w25_cs_set();
  w25q128_spi_send(W25_WRITE_STATUS_2);
  w25q128_spi_send(data);
  w25_cs_reset();
    
  return NRF_SUCCESS;
}

uint8_t w25q128_read_status_1()
{
    uint8_t resp;
    
    w25_cs_set();
    w25q128_spi_send(W25_READ_STATUS_1);
    resp = w25q128_read();
    w25_cs_reset();
     
    return resp;
}

uint8_t w25q128_read_status_2()
{
    uint8_t resp;
    
    w25_cs_set();
    w25q128_spi_send(W25_READ_STATUS_2);
    resp = w25q128_read();
    w25_cs_reset();
     
    return resp;
}

uint8_t w25q128_read_status_3()
{
    uint8_t resp;
    
    w25_cs_set();
    w25q128_spi_send(W25_READ_STATUS_3);
    resp = w25q128_read();
    w25_cs_reset();
     
    return resp;
}

uint8_t w25q128_uniqueid()
{
    const uint8_t get_id[] = {0x4B, 0, 0, 0, 0};
    uint8_t buff[8];
     
    w25_cs_set();
    w25q128_spi_send(get_id[0]);
    w25q128_spi_send(get_id[1]);
    w25q128_spi_send(get_id[2]);
    w25q128_spi_send(get_id[3]);
    w25q128_spi_send(get_id[4]);
    buff[0] = w25q128_read();
    buff[1] = w25q128_read();
    buff[2] = w25q128_read();
    buff[3] = w25q128_read();
    buff[4] = w25q128_read();
    buff[5] = w25q128_read();
    buff[6] = w25q128_read();
    buff[7] = w25q128_read();
    w25_cs_reset();
     
    return buff[0];
}

uint8_t w25q128_ident()
{
    const uint8_t get_id[] = {W25_GET_DEV_ID, 0, 0, 0};
    uint8_t buff[2], ret;
     
    w25_cs_set();
    w25q128_spi_send(get_id[0]);
    w25q128_spi_send(get_id[1]);
    w25q128_spi_send(get_id[2]);
    w25q128_spi_send(get_id[3]);
    buff[0] = w25q128_read();
    buff[1] = w25q128_read();
    w25_cs_reset();
     
    if (buff[0] == 0xef && buff[1] == 0x17)
    {
        ret = 1;
    } else {
        ret = 0;
    }
    
    return ret;
}

uint8_t w25q128_sector64K_erase(uint32_t addr)
{
  Status_reg_1_t r;
  
  w25_cs_set();
  w25q128_spi_send(W25_WRITE_ENABLE);
  w25_cs_reset();
  
  r.all = w25q128_read_status_1();
  
  if (r.bit.write_enable != 1)
    return NRF_ERROR_INTERNAL;
  
  w25_cs_set();
  w25q128_spi_send(W25_SECTOR64_ERASE);
  w25q128_spi_send((addr >> 16) & 0xFF);
  w25q128_spi_send((addr >> 8) & 0xFF);
  w25q128_spi_send(addr & 0xFF);

  w25_cs_reset();
  
  return NRF_SUCCESS;
}

uint8_t w25q128_sector4K_erase(uint32_t addr)
{
  Status_reg_1_t r;
  
  w25_cs_set();
  w25q128_spi_send(W25_WRITE_ENABLE);
  w25_cs_reset();
  
  r.all = w25q128_read_status_1();
  
  if (r.bit.write_enable != 1)
    return NRF_ERROR_INTERNAL;
  
  w25_cs_set();
  w25q128_spi_send(W25_SECTOR4_ERASE);
  w25q128_spi_send((addr >> 16) & 0xFF);
  w25q128_spi_send((addr >> 8) & 0xFF);
  w25q128_spi_send(addr & 0xFF);

  w25_cs_reset();

  do
  {
    r.all = w25q128_read_status_1();
  }
  while(r.bit.busy);  
  
  return NRF_SUCCESS;
}

uint8_t w25q128_sector_programm(uint8_t* data, uint32_t addr)
{
  for (uint8_t i = 0; i < W25Q_FLASH_SECTORSIZE / W25Q_FLASH_PAGESIZE; i++)
  {
    w25q128_page_programm(data + (i * W25Q_FLASH_PAGESIZE), addr + (i * W25Q_FLASH_PAGESIZE));
  }
  
  return NRF_SUCCESS;
}

uint8_t w25q128_page_programm(uint8_t* data, uint32_t addr)
{
  Status_reg_1_t r;
  
  uint16_t len = W25Q_FLASH_PAGESIZE;

  w25_cs_set();
  w25q128_spi_send(W25_WRITE_ENABLE);
  w25_cs_reset();

  r.all = w25q128_read_status_1();
  
  if (r.bit.write_enable != 1)
    return NRF_ERROR_INTERNAL;
  
  w25_cs_set();
  w25q128_spi_send(W25_PAGE_PROGRAMM);
  w25q128_spi_send((addr >> 16) & 0xFF);
  w25q128_spi_send((addr >> 8) & 0xFF);
  w25q128_spi_send(addr & 0xFF);
  
  while(len--)
  {
      w25q128_spi_send(*data++);
  }
  w25_cs_reset();
  
  do
  {
    r.all = w25q128_read_status_1();
  }
  while(r.bit.busy);
  
  return NRF_SUCCESS;
}

uint8_t w25q128_write_byte(const uint8_t data, uint32_t addr)
{
  w25_cs_set();
  w25q128_spi_send(W25_WRITE_ENABLE);
  w25_cs_reset();
     
  w25_cs_set();
  w25q128_spi_send(W25_PAGE_PROGRAMM);
  w25q128_spi_send((addr >> 16) & 0xFF);
  w25q128_spi_send((addr >> 8) & 0xFF);
  w25q128_spi_send(addr & 0xFF);

  w25q128_spi_send(data);

  w25_cs_reset();
  
  return 0;
}

uint8_t w25q128_read_byte(uint32_t addr)
{
    Status_reg_1_t r;
    uint8_t tmp = 0;

    w25_cs_set();
    w25q128_spi_send(W25_READ);
    w25q128_spi_send((addr >> 16) & 0xFF);
    w25q128_spi_send((addr >> 8) & 0xFF);
    w25q128_spi_send(addr & 0xFF);

    tmp = w25q128_read();

    w25_cs_reset();
    
    do
    {
      r.all = w25q128_read_status_1();
    }
    while(r.bit.busy);
    
    return tmp;
}

uint8_t w25q128_read_sector(uint8_t* data, uint32_t addr)
{
    Status_reg_1_t r;
    
    uint16_t len = W25Q_FLASH_SECTORSIZE;
    
    w25_cs_set();
    
    w25q128_spi_send(W25_READ);
    w25q128_spi_send((addr >> 16) & 0xFF);
    w25q128_spi_send((addr >> 8) & 0xFF);
    w25q128_spi_send(addr & 0xFF);
    
    while(len--)
    {
        *data++ = w25q128_read();
    }
    
    w25_cs_reset();
    
    do
    {
      r.all = w25q128_read_status_1();
    }
    while(r.bit.busy);
    
    return 0;
}

uint8_t w25q128_read_buf(uint8_t* data, uint32_t addr, size_t len)
{
    Status_reg_1_t r;
    
    w25_cs_set();
    
    w25q128_spi_send(W25_READ);
    w25q128_spi_send((addr >> 16) & 0xFF);
    w25q128_spi_send((addr >> 8) & 0xFF);
    w25q128_spi_send(addr & 0xFF);
    while(len--)
    {
        *data++ = w25q128_read();
    }
    
    w25_cs_reset();
    
    do
    {
      r.all = w25q128_read_status_1();
    }
    while(r.bit.busy);
    
    return 0;
}

ret_code_t writeClockRegister(uint8_t reg, uint8_t value)
{
  memset(i2c_array, 0x0, 2);
  
  i2c_array[0] = reg;
  i2c_array[1] = value;
  
  if (nrf_drv_twi_tx(&m_twi_master, CLOCKADDR, i2c_array, 2, false) == NRF_SUCCESS)
    return 0;
  else
    return 1;
}

ret_code_t writeClock(uint8_t secs, uint8_t mins, uint8_t hours, uint8_t date, uint8_t months, uint8_t years)
{
  bool isSentFailed = false;
  
  // sec
  uint8_t secEd = secs % 10;
  uint8_t secDes = secs / 10 % 10;
  i2c_array[0] = (secDes << 4) | secEd;
  
  if (writeClockRegister(0x00, i2c_array[0]) != NRF_SUCCESS)
    isSentFailed = true;
  
  // min
  uint8_t minEd = mins % 10;
  uint8_t minDes = mins / 10 % 10;
  i2c_array[0] = (minDes << 4) | minEd;
  
  if (writeClockRegister(0x01, i2c_array[0]) != NRF_SUCCESS)
    isSentFailed = true;
  
  // hour
  uint8_t hoursEd = hours % 10;
  uint8_t hoursDes = hours / 10 % 10;
  i2c_array[0] = (hoursDes << 4) | hoursEd;
  
  if (writeClockRegister(0x02, i2c_array[0]) != NRF_SUCCESS)
    isSentFailed = true;
  
  // date
  uint8_t dateEd = date % 10;
  uint8_t dateDes = date / 10 % 10;
  i2c_array[0] = (dateDes << 4) | dateEd;
  
  if (writeClockRegister(0x04, i2c_array[0]) != NRF_SUCCESS)
    isSentFailed = true;
  
  // months
  uint8_t monthsEd = months % 10;
  uint8_t monthsDes = months / 10 % 10;
  i2c_array[0] = (monthsDes << 4) | monthsEd;
  
  if (writeClockRegister(0x05, i2c_array[0]) != NRF_SUCCESS)
    isSentFailed = true;
  
  // years
  uint8_t yearsEd = years % 10;
  uint8_t yearsDes = years / 10 % 10;
  i2c_array[0] = (yearsDes << 4) | yearsEd;
  
  if (writeClockRegister(0x06, i2c_array[0]) != NRF_SUCCESS)
    isSentFailed = true;
  
  if (isSentFailed == true)
    return 1;
  else
    return 0;
}

float readBattVolts_saved()
{
  return battVolt_saved;
}

float readBattVolts_current()
{
  float battVolt = 0;
  float tmp;
  
  /*
  memset(&m_buffer_pool, 0x0, 2);
  nrf_drv_saadc_sample_convert(0, &m_buffer_pool);
  battVolt = m_buffer_pool * (0.6) / (1024 * 1/6);
  battVolt *= BATT_ADC_MULTIPLIER;
  
  return battVolt;
  */
  
  for (int i = 0; i < 20; i++)
  {
    tmp = 0;
    memset(&m_buffer_pool, 0x0, 2);
    nrf_drv_saadc_sample_convert(0, &m_buffer_pool);
    tmp = m_buffer_pool * (0.6) / (1024 * 1/6);
    tmp *= BATT_ADC_MULTIPLIER;
    
    battVolt += tmp;
  }
    
  return battVolt / 20;
}

static void longWD_timer_handler(void * p_context)
{
  nrf_drv_wdt_channel_feed(m_channel_id);
  
  app_timer_start(longWD_timer, APP_TIMER_TICKS(55000), NULL);
}

static void battVoltageUpdate_timer_handler(void * p_context)
{
  battVolt_saved = readBattVolts_current();
}

ret_code_t readClock(uint8_t *secs, uint8_t *mins, uint8_t *hours, uint8_t *date, uint8_t *months, uint8_t *years)
{
  memset(i2c_array, 0x0, 9);
        
  if (nrf_drv_twi_tx(&m_twi_master, CLOCKADDR, &addr0, 1, true) == NRF_SUCCESS)
  {
    if (nrf_drv_twi_rx(&m_twi_master, CLOCKADDR, i2c_array, 7) == NRF_SUCCESS)
    {
      if (BitIsClear(i2c_array[1], 7))
      {
        //get sec
        uint8_t secEd = i2c_array[0] << 4;
        secEd = secEd >> 4;
        uint8_t secDes = i2c_array[0] << 1;
        secDes = secDes >> 5;
        *secs = secDes * 10 + secEd;
        
        // get min
        uint8_t minEd = i2c_array[1] << 4;
        minEd = minEd >> 4;
        uint8_t minDes = i2c_array[1] << 1;
        minDes = minDes >> 5;
        *mins = minDes * 10 + minEd;
        
        // get hour
        uint8_t hoursEd = i2c_array[2] << 4;
        hoursEd = hoursEd >> 4;
        uint8_t hoursDes = i2c_array[2] << 2;
        hoursDes = hoursDes >> 6;
        *hours = hoursDes * 10 + hoursEd;
        
        // get date
        uint8_t dateEd = i2c_array[4] << 4;
        dateEd = dateEd >> 4;
        uint8_t dateDes = i2c_array[4] << 2;
        dateDes = dateDes >> 6;
        *date = dateDes * 10 + dateEd;
        
        // get months
        uint8_t monthsEd = i2c_array[5] << 4;
        monthsEd = monthsEd >> 4;
        uint8_t monthsDes = i2c_array[5] << 3;
        monthsDes = monthsDes >> 7;
        *months = monthsDes * 10 + monthsEd;
        
        // get years
        uint8_t yearsEd = i2c_array[6] << 4;
        yearsEd = yearsEd >> 4;
        uint8_t yearsDes = i2c_array[6] >> 4;
        *years = yearsDes * 10 + yearsEd;
        
        return NRF_SUCCESS;
      } else
      {
        return 2;
      }
    }
    {
      return 1;
    }
  } else
  {
    return 1;
  }
}

void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
    app_error_handler(0xDEADBEEF, line_num, p_file_name);
}

static void scan_stop(void)
{
    nrf_ble_scan_stop();
}

static void scan_start(void)
{
    ret_code_t ret;

    ret = nrf_ble_scan_start(&m_scan);
    APP_ERROR_CHECK(ret);
}

static void scan_evt_handler(scan_evt_t const * p_scan_evt)
{
    ret_code_t err_code;

    switch(p_scan_evt->scan_evt_id)
    {
         case NRF_BLE_SCAN_EVT_CONNECTING_ERROR:
         {
              err_code = p_scan_evt->params.connecting_err.err_code;
              APP_ERROR_CHECK(err_code);
         } break;
         
         case NRF_BLE_SCAN_EVT_NOT_FOUND:
         {
         } break;
         
         case NRF_BLE_SCAN_EVT_CONNECTED:
         {
           
         } break;

         case NRF_BLE_SCAN_EVT_SCAN_TIMEOUT:
         {
           //if (bleWaitingForDeatach == false)
           //{
            //if (ableToBleConnect == true)
            //{
            //  scan_start();
            //  disableShockSensor();
            //}
           //}
         } break;

         default:
             break;
    }
}

static void scan_init(void)
{
    ret_code_t          err_code;
    nrf_ble_scan_init_t init_scan;
    
    memset(&init_scan, 0, sizeof(init_scan));

    init_scan.connect_if_match = true;
    init_scan.conn_cfg_tag     = APP_BLE_CONN_CFG_TAG;

    err_code = nrf_ble_scan_init(&m_scan, &init_scan, scan_evt_handler);
    APP_ERROR_CHECK(err_code);

    //err_code = nrf_ble_scan_filter_set(&m_scan, SCAN_UUID_FILTER, &m_nus_uuid);
    //APP_ERROR_CHECK(err_code);
    
    err_code = nrf_ble_scan_filter_set(&m_scan, SCAN_NAME_FILTER, m_target_periph_name);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_scan_filters_enable(&m_scan, NRF_BLE_SCAN_NAME_FILTER, false);
    APP_ERROR_CHECK(err_code);
}

static void db_disc_handler(ble_db_discovery_evt_t * p_evt)
{
    ble_nus_c_on_db_disc_evt(&m_ble_nus_c, p_evt);
}

static void ble_nus_chars_received_uart_print(uint8_t * p_data, uint16_t data_len, uint16_t max_data_len)
{
  if (!dataSessionBlinkingEnabled)
  {
    app_timer_start(dataBlinking_timer, APP_TIMER_TICKS(100), NULL);
    battVolt_saved = readBattVolts_current();
    app_timer_start(battVoltageUpdate_timer, APP_TIMER_TICKS(1800000), NULL);
    
    dataSessionBlinkingEnabled = true;
  }
    
    ret_code_t ret_val;
    
    if (memcmp(p_data, str_EVDONE, sizeof(str_EVDONE)) == 0) // read done
    {
        memset(buff, 0x0, 24);
        
        if (resetEventCounter() == NRF_SUCCESS)
        {
          sprintf((char *)buff, "OK\n");
        } else {
          sprintf((char *)buff, "ERR\n");
        }
      
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_SNDTST, sizeof(str_SNDTST)) == 0)
    {
        memset(buff, 'S', 244);
        buff[243] = '\n';
        
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 244);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_HM, sizeof(str_HM)) == 0) // how much records?
    {
        memset(buff, 0x0, 24);
        
        sprintf((char *)buff, "%d,%d\n", eventsToDownload, maxAlarmsEverCount);
        
        tmp_eventsToDownload = eventsToDownload;
        alarmsDownloadCounter = maxAlarmsEverCount;
      
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_S0, sizeof(str_S0)) == 0) // serial 1
    {
        memset(buff, 0x0, 24);
        
        sprintf((char *)buff, "%x\n", NRF_FICR->DEVICEADDR[0]);
      
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_S1, sizeof(str_S1)) == 0) // serial 2
    {
        memset(buff, 0x0, 24);
        
        sprintf((char *)buff, "%x\n", NRF_FICR->DEVICEADDR[1]);
      
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_RCREG, sizeof(str_RCREG)) == 0) // mfrc reg write
    {
        ret_code_t ret_val = 0;
        uint32_t regNum = 0;
        uint32_t regValue = 0;
        
        memset(buff, 0x0, 24);
        
        ret_val = sscanf((char *)&p_data[5], "%2x%2x\n", &regNum, &regValue);
        
        if (ret_val != 2)
          sprintf((char *)buff, "ERR\n");
        else
        {
          MFRC522_WriteRegister(regNum, regValue);
          sprintf((char *)buff, "OK\n");
        }
      
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_RCRDREG, sizeof(str_RCRDREG)) == 0) // mfrc reg read
    {
        ret_code_t ret_val = 0;
        uint32_t regNum = 0;
        uint8_t regValue = 0;
        
        memset(buff, 0x0, 24);
        
        ret_val = sscanf((char *)&p_data[7], "%2x\n", &regNum);
        
        if (ret_val != 1)
          sprintf((char *)buff, "ERR\n");
        else
        {
          regValue = MFRC522_ReadRegister(regNum);
          
          sprintf((char *)buff, "%X\n", regValue);
        }
      
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_IA, sizeof(str_IA)) == 0) // is activated
    {
        memset(buff, 0x0, 24);
        
        sprintf((char *)buff, "%d\n", isActivated);
      
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_CE, sizeof(str_CE)) == 0) // is power coil enabled
    {
        memset(buff, 0x0, 24);
        
        sprintf((char *)buff, "%d\n", isOnCoilNow);
      
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_GV, sizeof(str_GV)) == 0) // get dongle version
    {
        memset(buff, 0x0, 24);
        
        sprintf((char *)buff, "%d.%d\n", fwVersionMajor, fwVersionMinor);
      
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_DA, sizeof(str_DA)) == 0) // do activate
    {
        int i = 0;
        bool secretErr = true;
        bool serialErr = true;
        
        memset(buff, 0, 16);
        
        for (i = 0; i < 16; i++)
        {
          if (device_secret[i] != '0')
          {
            secretErr = false;
          }
        }
        
        for (i = 0; i < 6; i++)
        {
          if (device_serial[i] != '0')
          {
            serialErr = false;
          }
        }
        
        if (secretErr == false && serialErr == false)
        {
          isActivated = 1;
        
          if (settingsCommit() == NRF_SUCCESS);
          {
            sprintf((char *)buff, "OK\n");
          }
        } else
        {
          sprintf((char *)buff, "ERR\n");
        }
        
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_GB, sizeof(str_GB)) == 0) // BATT
    {
        memset(buff, 0x0, 24);
        
        sprintf((char *)buff, "%.2f\n", readBattVolts_current());
      
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_CLRBREAK, sizeof(str_CLRBREAK)) == 0) // clear all alarms
    {
        memset(buff, 0x0, 16);
        
        maxAlarmsEverCount = 0;
        alarmsRWPointer = 0;
        alarmsDownloadCounter = 0;
        
        sprintf((char *)buff, "OK\n");
        
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_RMBREAK, sizeof(str_RMBREAK)) == 0) // clear prev. alarm
    {
        memset(buff, 0x0, 16);
        
//        if (alarmsDownloadCounter > 0)
//        {
          if (alarmsRWPointer > 0)
            alarmsRWPointer--;
          else
            alarmsRWPointer = maxAlarmsEverCount - 1;
          
          writeAlarm(alarmsRWPointer, 0, 0, 0, 0, 0, 0);
          
          sprintf((char *)buff, "OK\n");
//        } else
//          sprintf((char *)buff, "ERR\n");
        
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_RSER, sizeof(str_RSER)) == 0) // device serial
    {
        memset(buff, 0x0, 24);
        
        //sprintf((char *)buff, "%s\0\n", device_serial);
        sprintf((char *)buff, "%c%c%c%c%c%c\n", device_serial[0],
                                                device_serial[1],
                                                device_serial[2],
                                                device_serial[3],
                                                device_serial[4],
                                                device_serial[5]);
      
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } /* else if (memcmp(p_data, str_GSECRET, sizeof(str_GSECRET)) == 0) // device secret
    {
        memset(buff, 0x0, 24);
        
        sprintf((char *)buff, "%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c\n", device_secret[0],
                                                                    device_secret[1],
                                                                    device_secret[2],
                                                                    device_secret[3],
                                                                    device_secret[4],
                                                                    device_secret[5],
                                                                    device_secret[6],
                                                                    device_secret[7],
                                                                    device_secret[8],
                                                                    device_secret[9],
                                                                    device_secret[10],
                                                                    device_secret[11],
                                                                    device_secret[12],
                                                                    device_secret[13],
                                                                    device_secret[14],
                                                                    device_secret[15]);
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 17);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } */ else if (memcmp(p_data, str_BB, sizeof(str_BB)) == 0) // BYE
    {
        app_timer_stop(dataBlinking_timer);
        app_timer_stop(battVoltageUpdate_timer);
        dataSessionBlinkingEnabled = false;
        //isCardReadingBlocked = false;
        
        if (isOnCoilNow == true)
        {
          bleWaitingForDeatach = true;
        }
        
        ret_val = sd_ble_gap_disconnect(conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        APP_ERROR_CHECK(ret_val);
    
    } else if (memcmp(p_data, str_GTIME, sizeof(str_GTIME)) == 0) // get time
    {
        uint8_t seconds = 0;
        uint8_t minutes = 0;
        uint8_t hours = 0;
        uint8_t date = 0;
        uint8_t months = 0;
        uint8_t years = 0;
        
        memset(buff, 0x0, 24);
        
        ret_val = readClock(&seconds, &minutes, &hours, &date, &months, &years);
        
        switch (ret_val)
        {
          case 0:
            sprintf((char *)buff, "%02d%02d%02d%02d%02d20%02d\n", hours, minutes, seconds, date, months, years);
            break;
            
          case 2:
            sprintf((char *)buff, "OSCFAIL\n");
            break;
            
          case 1:
          default:
            sprintf((char *)buff, "ERR\n");
            break;
        }
        
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 24);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_STIME, sizeof(str_STIME)) == 0) // set time
    {
        int seconds = 45;
        int minutes = 59;
        int hours   = 23;
        int date    = 30;
        int months  = 11;
        int years   = 99;
        
        memset(buff, 0x0, 24);
        
        ret_val = sscanf((char *)&p_data[5], "%02d%02d%02d%02d%02d20%02d\n", &hours, &minutes, &seconds, &date, &months, &years);
        
        if (ret_val != 6)
          sprintf((char *)buff, "ERR\n");
        else
        {
          ret_val = writeClock(seconds, minutes, hours, date, months, years);
          
          if (ret_val == 0)
            sprintf((char *)buff, "OK\n");
          else
            sprintf((char *)buff, "ERR\n");
        }
        
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 24);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_GT, sizeof(str_GT)) == 0) // get dongle type
    {
        memset(buff, 0x0, 24);
        
        sprintf((char *)buff, "%d\n", device_type);
      
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_WSER, sizeof(str_WSER)) == 0) // write serial
    {
        memset(buff, 0, 16);
        
        memcpy((void *)device_serial, &p_data[4], 6);
        
        if (settingsCommit() == NRF_SUCCESS)
        {
          sprintf((char *)buff, "OK\n");
        }
        
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    }  else if (memcmp(p_data, str_WSEC, sizeof(str_WSEC)) == 0) // write secret
    {
        memset(buff, 0, 16);
        
        memcpy((void *)device_secret, &p_data[4], 16);
        
        if (settingsCommit() == NRF_SUCCESS)
        {
          sprintf((char *)buff, "OK\n");
        } else
        {
          sprintf((char *)buff, "ERR\n");
        }
        
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_DISA, sizeof(str_DISA)) == 0) // disactivate
    {
        memset(buff, 0, 16);
        sprintf((char *)buff, "ERR\n");
        
        if (memcmp(&p_data[4], &device_secret[0], 16) == 0)
        {
          isActivated = 0;
          
          if (settingsCommit() == NRF_SUCCESS)
          {
            memset(buff, 0, 16);
            sprintf((char *)buff, "OK\n");
          }
        }
        
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_GCARD, sizeof(str_GCARD)) == 0) // read last card
    {
        int i = 0;
        memset(buff, 0, 244);
        
        if (tmp_eventsToDownload > 0)
        {
          for (i = 0; i < 244 - EVENT_RECORD_SIZE - 1; i += EVENT_RECORD_SIZE)
          {
            if (tmp_eventsToDownload == 0)
              break;
            
            if (readEvent(tmp_eventsToDownload - 1, (char *)&buff[i]) != NRF_SUCCESS)
            {
              sprintf((char *)buff, "ERR\n");
              break;
            }
            decreaseEventCounter();
          }
          
          buff[i] = '\n';
          
          i++;
          
//          if (readEvent(tmp_eventsToDownload - 1, (char *)buff) != NRF_SUCCESS)
//          {
//            sprintf((char *)buff, "ERR\n");
//          }
//          else if (decreaseEventCounter() != NRF_SUCCESS)
//          {
//            sprintf((char *)buff, "ERR\n");
//          } else
//            buff[21] = '\n';
        } else
        {
          i = 4;
          sprintf((char *)buff, "END\n");
        }
        
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, i);
            //ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, i + EVENT_RECORD_SIZE + 1);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_GALARM, sizeof(str_GALARM)) == 0) // read last alarm
    {
        memset(buff, 0, 24);
        
        if (alarmsDownloadCounter > 0)
        {
          if (readAlarm(alarmsDownloadCounter - 1, (char *)buff) != NRF_SUCCESS)
          {
            sprintf((char *)buff, "ERR\n");
          }
          else
          {
            alarmsDownloadCounter--;
            buff[14] = '\n';
          }
        } else
          sprintf((char *)buff, "END\n");
        
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 24);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_DUSTART, sizeof(str_DUSTART)) == 0) // dongle update start
    {
        memset(buff, 0, 16);
        
        isUpdateStarted = true;
        currentFwBlock = 0;
        
        sprintf((char *)buff, "OK\n");
        
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_DUSIZE, sizeof(str_DUSIZE)) == 0) // dongle update size
    {
        uint8_t writeRes = 0xFA;
        uint32_t tmpSize = 0;
        
        memset(buff, 0, 16);
        
        ret_val = sscanf((char *)&p_data[6], "%d\n", &tmpSize);
        
        if (tmpSize > 0)
          writeRes = W25Q_Write(FW_IMAGE_SIZE_ADDR,  (uint8_t *)&tmpSize,     FW_IMAGE_SIZE_SIZE);
        
        if ((tmpSize <= 0) || (ret_val != 1) || (!isUpdateStarted) || (writeRes != NRF_SUCCESS))
        {
          isUpdateStarted = false;
          sprintf((char *)buff, "ERR\n");
        } else
        {
          fwSize = tmpSize;
          sprintf((char *)buff, "OK\n");
        }
        
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_DUSUMM, sizeof(str_DUSUMM)) == 0) // dongle update summ
    {
        uint8_t writeRes = 0xFA;
        uint32_t tmpSumm = 0;
        
        memset(buff, 0, 16);
        
        ret_val = sscanf((char *)&p_data[6], "%d\n", &tmpSumm);
        
        if (ret_val == 1)
          writeRes = W25Q_Write(FW_IMAGE_SUMM_ADDR,  (uint8_t *)&tmpSumm,     FW_IMAGE_SUMM_SIZE);
        
        if ((ret_val != 1) || (!isUpdateStarted) || (fwSize <= 0) || (writeRes != NRF_SUCCESS))
        {
          isUpdateStarted = false;
          sprintf((char *)buff, "ERR\n");
        } else
        {
          sprintf((char *)buff, "OK\n");
        }
        
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_DUB, sizeof(str_DUB)) == 0) // dongle update block
    {
        uint8_t writeRes = 0xFA;
        
        memset(buff, 0, 16);
        
        char lbuff[FWBLKSIZE * 2];
        char minBuff[3];
        
        minBuff[2] = 0;
        
        //nrf_gpio_pin_set(LED_BLUE_PIN);
        //nrf_gpio_pin_set(LED_RED_PIN);
        
        memcpy(lbuff, (char *)&p_data[3], FWBLKSIZE * 2);
        
        for (int i = 0; i < FWBLKSIZE * 2; i++)
        {
          if (i % 2 == 0)
          {
            minBuff[0] = lbuff[i];
            minBuff[1] = lbuff[i+1];
            
            lbuff[i / 2] = strtol(minBuff, NULL, 16);
          }
        }
        
        //nrf_gpio_pin_clear(LED_RED_PIN);

        writeRes = W25Q_Write(FW_IMAGE_START_ADDR + 4 + 4 + 4 + (FWBLKSIZE * currentFwBlock), lbuff, FWBLKSIZE);

        //nrf_gpio_pin_clear(LED_BLUE_PIN);

        if ((lbuff == 0) || (!isUpdateStarted) || (fwSize <= 0) || (writeRes != NRF_SUCCESS))
        {
          isUpdateStarted = false;
          sprintf((char *)buff, "ERR\n");
        } else
        {
          if (currentFwBlock < fwSize - 1)
          {
            currentFwBlock++;
            sprintf((char *)buff, "NEXT\n");
          } else
          {
            uint16_t marker = 0xDA01;
            
            writeRes = W25Q_Write(IS_NEW_FIRMWARE_MARKER_ADDR,   (uint8_t *)&marker,      IS_NEW_FIRMWARE_MARKER_SIZE);
            
            if (writeRes == NRF_SUCCESS)
            {
              sprintf((char *)buff, "OK\n");
            }
            else
            {
              isUpdateStarted = false;
              sprintf((char *)buff, "ERR\n");
            }
          }
        }
        
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
    } else if (memcmp(p_data, str_DRST, sizeof(str_DRST)) == 0) // reset
    {
        memset(buff, 0x0, 16);
        
        sprintf((char *)buff, "OK\n");
        
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, buff, 16);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
                APP_ERROR_CHECK(ret_val);
        } while (ret_val == NRF_ERROR_BUSY);
        
        nrf_delay_ms(500);
        
        ret_val = sd_ble_gap_disconnect(conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        APP_ERROR_CHECK(ret_val);
        
        NVIC_SystemReset();
    }
    
    /*
    if (0)
    {
        // Send data back to the peripheral.
        do
        {
            ret_val = ble_nus_c_string_send(&m_ble_nus_c, p_data, data_len);
            //if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
            //{
                //NRF_LOG_ERROR("Failed sending NUS message. Error 0x%x. ", ret_val);
                //APP_ERROR_CHECK(ret_val);
            //}
        } while (ret_val == NRF_ERROR_BUSY);
    }
    */
}


void uart_event_handle(app_uart_evt_t * p_event)
{
    static uint8_t data_array[BLE_NUS_MAX_DATA_LEN];
    static uint16_t index = 0;
    uint32_t ret_val;

    switch (p_event->evt_type)
    {
        case APP_UART_DATA_READY:
            UNUSED_VARIABLE(app_uart_get(&data_array[index]));
            index++;

            if ((data_array[index - 1] == '\n') || (index >= (m_ble_nus_max_data_len)))
            {
                do
                {
                    ret_val = ble_nus_c_string_send(&m_ble_nus_c, data_array, index);
                    if ( (ret_val != NRF_ERROR_INVALID_STATE) && (ret_val != NRF_ERROR_RESOURCES) )
                    {
                        APP_ERROR_CHECK(ret_val);
                    }
                } while (ret_val == NRF_ERROR_RESOURCES);

                index = 0;
            }
            break;

        case APP_UART_COMMUNICATION_ERROR:
            APP_ERROR_HANDLER(p_event->data.error_communication);
            break;

        case APP_UART_FIFO_ERROR:
            APP_ERROR_HANDLER(p_event->data.error_code);
            break;

        default:
            break;
    }
}

static void ble_nus_c_evt_handler(ble_nus_c_t * p_ble_nus_c, ble_nus_c_evt_t const * p_ble_nus_evt)
{
    ret_code_t err_code;

    switch (p_ble_nus_evt->evt_type)
    {
        case BLE_NUS_C_EVT_DISCOVERY_COMPLETE:
            err_code = ble_nus_c_handles_assign(p_ble_nus_c, p_ble_nus_evt->conn_handle, &p_ble_nus_evt->handles);
            APP_ERROR_CHECK(err_code);

            err_code = ble_nus_c_tx_notif_enable(p_ble_nus_c);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_NUS_C_EVT_NUS_TX_EVT:
            ble_nus_chars_received_uart_print(p_ble_nus_evt->p_data, p_ble_nus_evt->data_len, p_ble_nus_evt->max_data_len);
            break;

        case BLE_NUS_C_EVT_DISCONNECTED:
            //scan_start();
            break;
    }
}


static bool shutdown_handler(nrf_pwr_mgmt_evt_t event)
{
    //ret_code_t err_code;

    //err_code = bsp_indication_set(BSP_INDICATE_IDLE);
    //APP_ERROR_CHECK(err_code);

    switch (event)
    {
        case NRF_PWR_MGMT_EVT_PREPARE_WAKEUP:
            // Prepare wakeup buttons.
            //err_code = bsp_btn_ble_sleep_mode_prepare();
            //APP_ERROR_CHECK(err_code);
            break;

        default:
            break;
    }

    return true;
}

NRF_PWR_MGMT_HANDLER_REGISTER(shutdown_handler, APP_SHUTDOWN_HANDLER_PRIORITY);

static void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context)
{
    ret_code_t            err_code;
    ble_gap_evt_t const * p_gap_evt = &p_ble_evt->evt.gap_evt;

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            
            //nrf_gpio_pin_set(LED_BLUE_PIN);
            
            //isWasSessionEstablished = false;
            
            // Отключаем считывание всех меток при подключении по BLE
            app_timer_stop(mfrc522_timer);
            err_code = ble_nus_c_handles_assign(&m_ble_nus_c, p_ble_evt->evt.gap_evt.conn_handle, NULL);
            APP_ERROR_CHECK(err_code);

            err_code = ble_db_discovery_start(&m_db_disc, p_ble_evt->evt.gap_evt.conn_handle);
            APP_ERROR_CHECK(err_code);
            
            
            nrf_gpio_pin_set(LED_BLUE_PIN);
            nrf_delay_ms(150);
            nrf_gpio_pin_clear(LED_BLUE_PIN);
            nrf_gpio_pin_set(LED_YELLOW_PIN);
            nrf_delay_ms(150);
            nrf_gpio_pin_clear(LED_YELLOW_PIN);
            
            doVibrate(APP_TIMER_TICKS(250));
            
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            scan_stop();
            enableShockSensor();
            
            nrf_gpio_pin_clear(LED_BLUE_PIN);
            nrf_gpio_pin_clear(LED_YELLOW_PIN);            
            app_timer_stop(dataBlinking_timer);
            app_timer_stop(battVoltageUpdate_timer);
            dataSessionBlinkingEnabled = false;
            
            // Включаем считывание всех меток при отключении BLE
            restartReaderTimer();

            break;

        case BLE_GAP_EVT_TIMEOUT:
            //if (p_gap_evt->params.timeout.src == BLE_GAP_TIMEOUT_SRC_CONN)
            //{
            //  NRF_LOG_INFO("Connection Request timed out.");
            //}
            break;

        case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
            err_code = sd_ble_gap_sec_params_reply(p_ble_evt->evt.gap_evt.conn_handle, BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST:
            err_code = sd_ble_gap_conn_param_update(p_gap_evt->conn_handle,
                                                    &p_gap_evt->params.conn_param_update_request.conn_params);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GAP_EVT_PHY_UPDATE_REQUEST:
        {
            ble_gap_phys_t const phys =
            {
                .rx_phys = BLE_GAP_PHY_AUTO,
                .tx_phys = BLE_GAP_PHY_AUTO,
            };
            err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
            APP_ERROR_CHECK(err_code);
        } break;

        case BLE_GATTC_EVT_TIMEOUT:
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GATTS_EVT_TIMEOUT:
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;

        default:
            break;
    }
}

static void ble_stack_init(void)
{
    ret_code_t err_code;

    err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    // Configure the BLE stack using the default settings.
    // Fetch the start address of the application RAM.
    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    APP_ERROR_CHECK(err_code);

    // Enable BLE stack.
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);

    // Register a handler for BLE events.
    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}

void gatt_evt_handler(nrf_ble_gatt_t * p_gatt, nrf_ble_gatt_evt_t const * p_evt)
{
    if (p_evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED)
    {
        m_ble_nus_max_data_len = p_evt->params.att_mtu_effective - OPCODE_LENGTH - HANDLE_LENGTH;
    }
}

void gatt_init(void)
{
    ret_code_t err_code;

    err_code = nrf_ble_gatt_init(&m_gatt, gatt_evt_handler);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_gatt_att_mtu_central_set(&m_gatt, NRF_SDH_BLE_GATT_MAX_MTU_SIZE);
    APP_ERROR_CHECK(err_code);
}

/*
static void uart_init(void)
{
    ret_code_t err_code;

    app_uart_comm_params_t const comm_params =
    {
        .rx_pin_no    = RX_PIN_NUMBER,
        .tx_pin_no    = TX_PIN_NUMBER,
        .rts_pin_no   = RTS_PIN_NUMBER,
        .cts_pin_no   = CTS_PIN_NUMBER,
        .flow_control = APP_UART_FLOW_CONTROL_DISABLED,
        .use_parity   = false,
        .baud_rate    = UART_BAUDRATE_BAUDRATE_Baud9600
    };

    APP_UART_FIFO_INIT(&comm_params,
                       UART_RX_BUF_SIZE,
                       UART_TX_BUF_SIZE,
                       uart_event_handle,
                       APP_IRQ_PRIORITY_LOWEST,
                       err_code);

    APP_ERROR_CHECK(err_code);
}

*/
static void nus_c_init(void)
{
    ret_code_t       err_code;
    ble_nus_c_init_t init;

    init.evt_handler = ble_nus_c_evt_handler;

    err_code = ble_nus_c_init(&m_ble_nus_c, &init);
    APP_ERROR_CHECK(err_code);
}

static void timer_init(void)
{
    ret_code_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);
    
    err_code = app_timer_create(&mfrc522_timer, APP_TIMER_MODE_SINGLE_SHOT, mfrc522_timer_handler);
    APP_ERROR_CHECK(err_code);
    
    err_code = app_timer_create(&readDelay_timer, APP_TIMER_MODE_SINGLE_SHOT, readDelay_timer_handler);
    APP_ERROR_CHECK(err_code);
    
    err_code = app_timer_create(&motor_timer, APP_TIMER_MODE_SINGLE_SHOT, motor_timer_handler);
    APP_ERROR_CHECK(err_code);
    
    err_code = app_timer_create(&dataBlinking_timer, APP_TIMER_MODE_REPEATED, dataBlinking_timer_handler);
    APP_ERROR_CHECK(err_code);
    
    err_code = app_timer_create(&longWD_timer, APP_TIMER_MODE_SINGLE_SHOT, longWD_timer_handler);
    APP_ERROR_CHECK(err_code);
 
    err_code = app_timer_create(&battVoltageUpdate_timer, APP_TIMER_MODE_REPEATED, battVoltageUpdate_timer_handler);
    APP_ERROR_CHECK(err_code);
}

static void power_management_init(void)
{
    ret_code_t err_code;
    err_code = nrf_pwr_mgmt_init();
    APP_ERROR_CHECK(err_code);
}

static void db_discovery_init(void)
{
    ret_code_t err_code = ble_db_discovery_init(db_disc_handler);
    APP_ERROR_CHECK(err_code);
}

static void idle_state_handle(void)
{
  nrf_pwr_mgmt_run();
}

void saadc_callback(nrf_drv_saadc_evt_t const * p_event)
{
    if (p_event->type == NRF_DRV_SAADC_EVT_DONE)
    {
        ret_code_t err_code;

        err_code = nrf_drv_saadc_buffer_convert(p_event->data.done.p_buffer, SAMPLES_IN_BUFFER);
        APP_ERROR_CHECK(err_code);

        m_adc_evt_counter++;
    }
}

void saadc_init(void)
{
    ret_code_t err_code;
    nrf_saadc_channel_config_t channel_config = NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NRF_SAADC_INPUT_AIN0);
    channel_config.acq_time = NRF_SAADC_ACQTIME_40US;

    err_code = nrf_drv_saadc_init(NULL, saadc_callback);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_saadc_channel_init(0, &channel_config);
    APP_ERROR_CHECK(err_code);
}

static ret_code_t twi_master_init(void)
{
    ret_code_t ret;
    const nrf_drv_twi_config_t config =
    {
       .scl                = 6,
       .sda                = 7,
       .frequency          = NRF_DRV_TWI_FREQ_400K,
       .interrupt_priority = APP_IRQ_PRIORITY_HIGH,
       .clear_bus_init     = true
    };
    
    ret = nrf_drv_twi_init(&m_twi_master, &config, NULL, NULL);

    if (NRF_SUCCESS == ret)
    {
        nrf_drv_twi_enable(&m_twi_master);
        //nrf_gpio_pin_write(23, 1);
    }

    return ret;
}

void doVibrate(uint16_t time)
{
  if (!justSwitchOffLeds)
  {
    nrf_gpio_pin_set(10);
  }
  app_timer_start(motor_timer, time, NULL);
}

static void input_pin_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
  ret_code_t ret_val = 0;
  uint8_t tmp = 0;
    
  // Удар
  if (pin == 9)
  {
    disableShockSensor();
      
    // TODO: Убрать колхоз и написать отдельную функцию
    uint8_t seconds = 0;
    uint8_t minutes = 0;
    uint8_t hours = 0;
    uint8_t date = 0;
    uint8_t months = 0;
    uint8_t years = 0;
    
    ret_val = readClock(&seconds, &minutes, &hours, &date, &months, &years);
    
    if (ret_val != NRF_SUCCESS)
    {
      seconds = 0;
      minutes = 0;
      hours = 0;
      date = 1;
      months = 1;
      years = 10;
    }
    
    nrf_delay_ms(500);
    
    nrf_gpio_pin_set(LED_YELLOW_PIN);
    nrf_delay_ms(1500);
    nrf_gpio_pin_clear(LED_YELLOW_PIN);
    
    writeAlarm(alarmsRWPointer, seconds, minutes, hours, date, months, years);
    increaseAlarmsPointer();
    
    enableShockSensor();
  // Питание от катушки
  } else if (pin == 3)
  {
      tmp = nrf_gpio_pin_read(3);
      
      // Появилась питание
      if (tmp == 0)
      {
        isOnCoilNow = true;
        bleWaitingForDeatach = false;
        isPowerSavingModeEnabled = false;
        
        currentMFRC522Delay = APP_TIMER_TICKS(400);

        restartReaderTimer();
        
        // Вырубаем смотрелку за нижним порогом напряжения
        nrf_drv_lpcomp_disable();
        
        isBatteryDischargedFlag = false;
      }
      // Пропало питание
      else if (tmp == 1)
      {
        isOnCoilNow = false;
        
        //currentMFRC522Delay = APP_TIMER_TICKS(1500);
        currentMFRC522Delay = APP_TIMER_TICKS(400);
        
        bleWaitingForDeatach = false;
        
        if (dataSessionBlinkingEnabled == false)
        {
          sd_ble_gap_disconnect(conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        }
        
        // Врубаем смотрелку за нижним порогом напряжения
        nrf_drv_lpcomp_enable();
      }
  // Зарядка АКБ
  } else if (pin == 4)
  {
    tmp = nrf_gpio_pin_read(4);
    
    if (tmp == 1)
    {
      nrf_gpio_pin_clear(LED_RED_PIN);
    } else 
    {
      nrf_gpio_pin_set(LED_RED_PIN);
    }
  } 
  
  /*else if ( pin == 31 )
  {
    tmp = nrf_gpio_pin_read(31);
  }*/
}

static void dataBlinking_timer_handler(void * p_context)
{
  if (readBattVolts_saved() >= 3.7)
  {
    if (isBatteryDischargedFlag)
    {
      isBatteryDischargedFlag = false;
      nrf_gpio_pin_clear(LED_YELLOW_PIN);
      nrf_gpio_pin_clear(LED_BLUE_PIN);
    }
    
    nrf_gpio_pin_toggle(LED_BLUE_PIN);
  }
  else
  {
    if (!isBatteryDischargedFlag)
    {
      isBatteryDischargedFlag = true;
      nrf_gpio_pin_clear(LED_YELLOW_PIN);
      nrf_gpio_pin_clear(LED_BLUE_PIN);
    }
    
    nrf_gpio_pin_toggle(LED_YELLOW_PIN);
  }
}

static void readDelay_timer_handler(void * p_context)
{
  readDelayFlag = false;
  restartReaderTimer();
}

static void motor_timer_handler(void * p_context)
{
  if (justSwitchOffLeds)
  {
    justSwitchOffLeds = false;
    nrf_gpio_pin_clear(23);
    nrf_gpio_pin_clear(24);
    nrf_gpio_pin_clear(25);
    
    return;
  }
  
  if (pleaseSwitchOffLedsAfterVibroAndStartDelayTimer == true)
  {
    nrf_gpio_pin_clear(23);
    nrf_gpio_pin_clear(24);
    nrf_gpio_pin_clear(25);
  }
  
  //OFF MOTOR
  nrf_gpio_pin_write(10, 0);
  
  if (pleaseSwitchOffLedsAfterVibroAndStartDelayTimer == true)
  {
    pleaseSwitchOffLedsAfterVibroAndStartDelayTimer = false;
#ifdef NOREADDELAY
    app_timer_start(readDelay_timer, 100, NULL);
#else
    app_timer_start(readDelay_timer, 100000, NULL);
#endif
  }
}

void enableShockSensor()
{
  nrf_drv_gpiote_in_event_enable(9, true);
}

void disableShockSensor()
{
  nrf_drv_gpiote_in_event_disable(9);
}

static void mfrc522_timer_handler(void * p_context)
{
  lastSAK = 0;
  lastAntiColl = 0;
  uint8_t cardNum = 0;
  
  nrf_drv_wdt_channel_feed(m_channel_id);
  
  MFRC522_Wakeup();
  
  //if (readDelayFlag == false)
  //{
    //led3Short();
    
    MFRC522_AntennaOn();
    
    nrf_delay_ms(10);
    
    memset(str, 0x00, 7);
    
    if (!MFRC522_Request(PICC_REQIDL, str))
    {
      if (!MFRC522_Anticoll(str))
      {
        if (lastAntiColl == 0x88)
        {
          MFRC522_SelectTag(str);
          memcpy(CARD_NUM, &str[1], 3);
          if (!MFRC522_Anticoll2(str))
          {
            if (lastAntiColl != 0x88)
            {
              MFRC522_SelectTag2(str);
              memcpy(&CARD_NUM[3], str, 4);
              
              if (lastSAK == 0x18) //
              {
                //MFRC522_RATS();
                //MFRC522_WritePersoMASTER();
                //MFRC522_WritePersoCC();
                //MFRC522_WritePersoL2SW();
                //MFRC522_WritePersoL3SW();
                //MFRC522_WritePersoCOMMIT();
                //led2Short();
              } else if (lastSAK == 0x20) //
              {
                //2. Чтобы устройство не считывало метки на зарядке.
                if (isOnCoilNow == false)
                {
                  MFRC522_RATS();
                  
                  if (MFRC522_FIRSTAUTH_3(0x4004, false, &cardNum) == NRF_SUCCESS)
                  {
                    ret_code_t ret_val = 0;
                    
                    // TODO: Убрать колхоз и написать отдельную функцию
                    uint8_t seconds = 0;
                    uint8_t minutes = 0;
                    uint8_t hours = 0;
                    uint8_t date = 0;
                    uint8_t months = 0;
                    uint8_t years = 0;
                    
                    // а не дофига ли событий?
                    if (eventsToDownload >= MAX_EVENTS_COUNT)
                    {
                      justSwitchOffLeds = true;
                      nrf_gpio_pin_set(LED_YELLOW_PIN);
                      doVibrate(2000);
                              
                      for (uint8_t i = 0; i < 3; i++)
                      {  
                        nrf_gpio_pin_set(10);
                        nrf_delay_ms(550);
                        nrf_gpio_pin_clear(10);
                        nrf_delay_ms(100);
                      }

                      restartReaderTimer();
                        
                      return;
                    }
                    
                    // а не умерло ли время?
                    ret_val = readClock(&seconds, &minutes, &hours, &date, &months, &years);
                    if(ret_val == 2)
                    {
                      uint8_t cntr = 4;
                      
                      while (cntr)
                      {
                        nrf_gpio_pin_set(LED_BLUE_PIN);
                        nrf_delay_ms(200);
                        nrf_gpio_pin_clear(LED_BLUE_PIN);
                        nrf_gpio_pin_set(LED_YELLOW_PIN);
                        nrf_delay_ms(200);
                        nrf_gpio_pin_clear(LED_YELLOW_PIN);
                        cntr--;
                      }
                      
                      restartReaderTimer();
                      
                      return;
                    }
                    
                    writeEvent(eventsToDownload, seconds, minutes, hours, date, months, years, cardNum);
                    increaseEventCounter();
                    
                    pleaseSwitchOffLedsAfterVibroAndStartDelayTimer = true;
                    readDelayFlag = true;
                    
                    if ((isBatteryDischargedFlag == false) && (readBattVolts_current() >= 3.7))
                    {
                      nrf_gpio_pin_set(LED_BLUE_PIN);
                      doVibrate(18000);
                    }
                    else
                    {
                      if (isBatteryDischargedFlag == false)
                      {
                        isBatteryDischargedFlag = true;
                      }
                      
                      nrf_gpio_pin_set(LED_YELLOW_PIN);
                      doVibrate(9000);
                    }
                  }
                  
                  /*
                  MFRC522_WritePersoSECTOR(0x9000);
                  MFRC522_WritePersoSECTOR(0x9001);
                  MFRC522_WritePersoSECTOR(0x9003);
                  MFRC522_WritePersoSECTOR(0x9004);
                  
                  MFRC522_WritePersoSECTOR(0x4000);
                  MFRC522_WritePersoSECTOR(0x4001);
                  MFRC522_WritePersoSECTOR(0x4002);
                  MFRC522_WritePersoSECTOR(0x4003);
                  MFRC522_WritePersoSECTOR(0x4004);
                  MFRC522_WritePersoSECTOR(0x4005);
                  MFRC522_WritePersoSECTOR(0x4006);
                  MFRC522_WritePersoSECTOR(0x4007);
                  MFRC522_WritePersoSECTOR(0x4008);
                  MFRC522_WritePersoSECTOR(0x4009);
                  MFRC522_WritePersoSECTOR(0x400A);
                  MFRC522_WritePersoSECTOR(0x400B);
                  MFRC522_WritePersoSECTOR(0x400C);
                  MFRC522_WritePersoSECTOR(0x400D);
                  MFRC522_WritePersoSECTOR(0x400E);
                  MFRC522_WritePersoSECTOR(0x400F);
                  MFRC522_WritePersoSECTOR(0x4010);
                  MFRC522_WritePersoSECTOR(0x4011);
                  MFRC522_WritePersoSECTOR(0x4012);
                  MFRC522_WritePersoSECTOR(0x4013);
                  MFRC522_WritePersoSECTOR(0x4014);
                  MFRC522_WritePersoSECTOR(0x4015);
                  MFRC522_WritePersoSECTOR(0x4016);
                  MFRC522_WritePersoSECTOR(0x4017);
                  MFRC522_WritePersoSECTOR(0x4018);
                  MFRC522_WritePersoSECTOR(0x4019);
                  MFRC522_WritePersoSECTOR(0x401A);
                  MFRC522_WritePersoSECTOR(0x401B);
                  MFRC522_WritePersoSECTOR(0x401C);
                  MFRC522_WritePersoSECTOR(0x401D);
                  MFRC522_WritePersoSECTOR(0x401E);
                  MFRC522_WritePersoSECTOR(0x401F);
                  
                  MFRC522_WritePersoCOMMIT();
                  */
                }
                
              } else if (lastSAK == 0x08) //
              {
                MFRC522_RATS();
                if (MFRC522_FIRSTAUTH_3(0x9003, true, &cardNum))
                {
                  LedShort(LED_BLUE_PIN);
                  LedShort(LED_BLUE_PIN);
                  LedShort(LED_BLUE_PIN);
                  //doVibrate(6000);
                }
                else
                {
                  LedShort(LED_YELLOW_PIN);
                  LedShort(LED_YELLOW_PIN);
                  LedShort(LED_YELLOW_PIN);
                  //MFRC522_FOLLOWAUTH();
                  //led3Short();
                }
              }  else if (lastSAK == 0x00) //
              {
                // BAZA
                if (bleWaitingForDeatach == false)
                {
                  MFRC522_Read(4, value);
                
                  if (value[0] == 'B' && value[1] == 'A' && value[2] == 'Z' && value[3] == 'A')
                  {
                    MFRC522_Read(15, value);
                    if (value[0] == 'B' && value[1] == 'A' && value[2] == 'Z' && value[3] == 'A')
                    {
                      //led1Short();
                      
                      nrf_ble_scan_all_filter_remove(&m_scan);
                      //APP_ERROR_CHECK(err_code);
                      
                      //nrf_ble_scan_filter_set(&m_scan, SCAN_UUID_FILTER, &m_nus_uuid);
                      
                      MFRC522_Read(5, value);
                      memcpy((void *)&m_target_periph_name, (void *)&value, 4);
                      
                      MFRC522_Read(6, value);
                      memcpy((void *)&m_target_periph_name[4], (void *)&value, 4);
                      
                      nrf_ble_scan_filter_set(&m_scan, SCAN_NAME_FILTER, m_target_periph_name);
                      //APP_ERROR_CHECK(err_code);
                      
                      // Set filter on target name.
                      //nrf_ble_scan_filter_set(&m_scan, SCAN_NAME_FILTER, m_target_periph_name);
                      //APP_ERROR_CHECK(err_code);
                      
                      //nrf_ble_scan_filters_enable(&m_scan, NRF_BLE_SCAN_UUID_FILTER, false);

                      scan_start();
                      disableShockSensor();
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
        
    MFRC522_AntennaOff();
  
  if (readDelayFlag == false)
  {
    restartReaderTimer();
  }
}

uint8_t readAlarm(int alarmNumber, char* text)
{
  return w25q128_read_buf((uint8_t *)text, ALARM_RECORDS_ADDR + (ALARM_RECORD_SIZE * alarmNumber), ALARM_RECORD_SIZE);
}

uint8_t writeAlarm(int alarmNumber, uint8_t seconds, uint8_t minutes, uint8_t hours, uint8_t date, uint8_t months, uint8_t years)
{
  char bufak[ALARM_RECORD_SIZE+1] = {'1', '2', '5', '9', '5', '9', '2', '2', '0', '5', '1', '9', '9', '0'};
  
  sprintf(bufak, "%02d%02d%02d%02d%02d20%02d", hours, minutes, seconds, date, months, years);
  
  return W25Q_Write(ALARM_RECORDS_ADDR + (ALARM_RECORD_SIZE * alarmNumber), bufak, ALARM_RECORD_SIZE);
}

uint8_t writeEvent(int eventNumber, uint8_t seconds, uint8_t minutes, uint8_t hours, uint8_t date, uint8_t months, uint8_t years, uint8_t cardNum)
{
  char bufak[EVENT_RECORD_SIZE+1] = {'7', '7', '7', '7', '7', '7', '7', '1', '2', '5', '9', '5', '9', '2', '2', '0', '5', '1', '9', '9', '0'};
  
  //memset(buff, 0x0, EVENT_RECORD_SIZE);
  sprintf(bufak, "7777777%02d%02d%02d%02d%02d%02d%02d", hours, minutes, seconds, date, months, cardNum, years);
  memcpy(bufak, CARD_NUM, 7);
  
  return W25Q_Write(EVENT_RECORDS_ADDR + (EVENT_RECORD_SIZE * eventNumber), bufak, EVENT_RECORD_SIZE);
}

uint8_t readEvent(int eventNumber, char* text)
{
  return w25q128_read_buf((uint8_t *)text, EVENT_RECORDS_ADDR + (EVENT_RECORD_SIZE * eventNumber), EVENT_RECORD_SIZE);
}

uint8_t increaseAlarmsPointer()
{
  if (alarmsRWPointer == maxAlarmsEverCount)
    if (maxAlarmsEverCount < MAX_ALARMS_COUNT)
      maxAlarmsEverCount++;
  
  if (alarmsRWPointer < maxAlarmsEverCount)
    alarmsRWPointer++;
  else
    alarmsRWPointer = 0;
  
  return stateWrite();
}

uint8_t increaseEventCounter()
{
  if (eventsToDownload < MAX_EVENTS_COUNT)
  {
    eventsToDownload++;
    
    return stateWrite();
  }
  
  return NRF_ERROR_INTERNAL;
}

uint8_t resetEventCounter()
{
  eventsToDownload = 0;
  tmp_eventsToDownload = 0;

  return stateWrite();
}

uint8_t decreaseEventCounter()
{
  if (tmp_eventsToDownload > 0)
    tmp_eventsToDownload--;
  else
    tmp_eventsToDownload = 0;

  return NRF_SUCCESS;
}

uint8_t M24_WRITE(uint8_t addr, uint8_t value)
{
  uint8_t tmp[2] = {0, 0};
  
  tmp[0] = addr;
  tmp[1] = value;
  
  if (nrf_drv_twi_tx(&m_twi_master, EEPROMADDR, tmp, 2, false) == NRF_SUCCESS)
  {
    nrf_delay_ms(5);
    return NRF_SUCCESS;
  }
  
  return 1;
}

uint8_t M24_READ(uint8_t addr, bool *result)
{
  uint8_t tmp = 0;
  
  if (nrf_drv_twi_tx(&m_twi_master, EEPROMADDR, &addr, 1, true) == NRF_SUCCESS)
  {
    if (nrf_drv_twi_rx(&m_twi_master, EEPROMADDR, &tmp, 1) == NRF_SUCCESS)
    {
      *result = true;
      return tmp;
    }
  }
  
  *result = false;
  return 0;
}

void M24_OPEN()
{
  nrf_gpio_pin_clear(12);
}

void M24_CLOSE()
{
  nrf_gpio_pin_set(12);
}

uint8_t M24_TEST ()
{
  bool result = false;
  uint8_t tmp, tmp2 = 0xFF;
  uint8_t testVal = 0x12;
  
  M24_OPEN();
  
  tmp = M24_READ (0, &result);
  
  if (result != true)
  {
    M24_CLOSE();
    
    return 1;
  }
  
  if (M24_WRITE(0, testVal) == NRF_SUCCESS)
  {
    tmp2 = M24_READ(0, &result);
    
    if (result && (tmp2 == testVal))
    {
      if (M24_WRITE(0, tmp) == NRF_SUCCESS)
      {
        M24_CLOSE();
        
        return NRF_SUCCESS;
      }
    }
  }
  
  M24_CLOSE();
  
  return 1;
}

uint8_t W25Q_CheckSector ( uint32_t addr, uint8_t *apDest )
{
  Status_reg_1_t r;
    
  w25_cs_set();
  
  w25q128_spi_send(W25_READ);
  w25q128_spi_send((addr >> 16) & 0xFF);
  w25q128_spi_send((addr >> 8) & 0xFF);
  w25q128_spi_send(addr & 0xFF);
  
  for (uint32_t i = 0; i < W25Q_FLASH_SECTORSIZE; i++)
  {
      if (apDest[i] != w25q128_read())
        return NRF_ERROR_INVALID_DATA;
  }
  
  w25_cs_reset();
  
  do
  {
    r.all = w25q128_read_status_1();
  }
  while(r.bit.busy);
  
  return NRF_SUCCESS;
}

uint8_t W25Q_Write ( uint32_t aAdr, const void *apSource, uint32_t aSize )
{
  uint32_t  idxSource = 0,
        page_limit;
  div_t  tSegAdr;
  uint8_t  *pSrc = ( uint8_t * ) apSource;
  uint8_t    *pW25QBuf;
  uint8_t   Attempt;

  if ( !aSize )
  {
    return NRF_ERROR_INVALID_LENGTH;
  }

  tSegAdr = div ( aAdr, W25Q_FLASH_SECTORSIZE );
  // Выделяем память под сектор
  pW25QBuf = malloc ( W25Q_FLASH_SECTORSIZE );

  if ( pW25QBuf == NULL )
  {
    return NRF_ERROR_NO_MEM;
  }

  do
  {
    Attempt = 0;
    page_limit = aSize - idxSource;

    if ( page_limit > W25Q_FLASH_SECTORSIZE - tSegAdr.rem )
    {
      page_limit = W25Q_FLASH_SECTORSIZE - tSegAdr.rem;
    }

    // Если записываем меньше страницы (первая/последняя или единственная)
    if ( page_limit != W25Q_FLASH_SECTORSIZE )
    {
      if ( w25q128_read_sector(pW25QBuf, tSegAdr.quot * W25Q_FLASH_SECTORSIZE) )
      {
        // Освобождаем память в случае ошибки
        free ( pW25QBuf );
        return NRF_ERROR_NO_MEM;
      };
    };

    // Обновляем содержимое буфера
    memcpy ( &pW25QBuf[tSegAdr.rem], &pSrc[idxSource], page_limit );

    do
    {
      // Стираем сектор (25 мс)
      if ( w25q128_sector4K_erase(tSegAdr.quot * W25Q_FLASH_SECTORSIZE) )
      {
        // Освобождаем память в случае ошибки
        free ( pW25QBuf );
        return NRF_ERROR_NO_MEM;
      };

      // Записываем сектор
      if ( w25q128_sector_programm(pW25QBuf, tSegAdr.quot * W25Q_FLASH_SECTORSIZE) )
      {
        // Освобождаем память в случае ошибки
        free ( pW25QBuf );
        return NRF_ERROR_NO_MEM;
      };

      // Проверим записанный сектор
      if ( W25Q_CheckSector ( tSegAdr.quot * W25Q_FLASH_SECTORSIZE, pW25QBuf ) )
      {
        // Инкрементируем счетчик попыток записи
        Attempt ++;

        if ( Attempt >= W25Q_FLASH_WR_ATTEMPTS )
        {
          // Освобождаем память в случае ошибки
          free ( pW25QBuf );
          return NRF_ERROR_NO_MEM;
        };
      }
      else
      {
        Attempt = 0;
      };
    }
    while ( Attempt );

    // Увеличиваем номер сектора, обнуляем смещение в странице
    idxSource += page_limit;
    tSegAdr.quot++;
    tSegAdr.rem = 0;
  }
  while ( idxSource < aSize );

  // Освобождаем память
  free ( pW25QBuf );
  return NRF_SUCCESS;
}

uint8_t settingsRead()
{
  w25q128_read_buf((uint8_t *)&isActivated,   IS_ACTIVATED_ADDR,  IS_ACTIVATED_SIZE);
  w25q128_read_buf((uint8_t *)&device_secret,   DEVICE_SECRET_ADDR,   DEVICE_SECRET_SIZE);
  w25q128_read_buf((uint8_t *)&device_serial, DEVICE_SERIAL_ADDR, DEVICE_SERIAL_SIZE);
  //w25q128_read_buf((uint8_t *)&device_type,   DEVICE_TYPE_ADDR,   DEVICE_TYPE_SIZE);
 
  return NRF_SUCCESS;
}

uint8_t settingsCommit()
{
  W25Q_Write(IS_ACTIVATED_ADDR,  (uint8_t *)&isActivated,     IS_ACTIVATED_SIZE);
  W25Q_Write(DEVICE_SECRET_ADDR,   (uint8_t *)&device_secret,      DEVICE_SECRET_SIZE);
  W25Q_Write(DEVICE_SERIAL_ADDR, (uint8_t *)&device_serial,  DEVICE_SERIAL_SIZE);
  //W25Q_Write(DEVICE_TYPE_ADDR,   (uint8_t *)&device_type,      DEVICE_TYPE_SIZE);
  
  return NRF_SUCCESS;
}

uint8_t w25q128_checkFlashID()
{
  Status_reg_1_t r1;
  uint8_t ret = 0;
  
  ret = w25q128_ident();
  do
  {
    r1.all = w25q128_read_status_1();
  }
  while(r1.bit.busy);
  
  if (ret != 1)
  {
    return NRF_ERROR_INTERNAL;
  }
    
  return NRF_SUCCESS;
}

void restartReaderTimer()
{
  MFRC522_Sleep();
  
  // Если нужно будет работать
  if (isOnCoilNow == true || (isActivated == true && isPowerSavingModeEnabled == false))
  {
    app_timer_start(mfrc522_timer, currentMFRC522Delay, NULL);
    
    return;
  }
}

uint8_t stateWrite()
{
  uint8_t buf[2];
  
  M24_OPEN();
  
  //eventsToDownload
  memcpy(buf, &eventsToDownload, 2);
  if (M24_WRITE(0, buf[0]))
  {
    M24_CLOSE();
    
    return 1;
  }
  if (M24_WRITE(1, buf[1]))
  {
    M24_CLOSE();
    
    return 1;
  }
  
  //maxAlarmsEverCount
  if (M24_WRITE(2, maxAlarmsEverCount))
  {
    M24_CLOSE();
    
    return 1;
  }
  
  //alarmsRWPointer;
  memcpy(buf, &alarmsRWPointer, 2);
  if (M24_WRITE(3, buf[0]))
  {
    M24_CLOSE();
    
    return 1;
  }
  if (M24_WRITE(4, buf[1]))
  {
    M24_CLOSE();
    
    return 1;
  }
  
  //alarmsDownloadCounter;
  memcpy(buf, &alarmsDownloadCounter, 2);
  if (M24_WRITE(5, buf[0]))
  {
    M24_CLOSE();
    
    return 1;
  }
  if (M24_WRITE(6, buf[1]))
  {
    M24_CLOSE();
    
    return 1;
  }
  
  M24_CLOSE();
  return NRF_SUCCESS;
}

uint8_t stateRead()
{
  bool result = false;
  uint8_t buf[2];
  
  //eventsToDownload
  buf[0] = M24_READ(0, &result);
  if (result == false)
  {
    return 1;
  }
  buf[1] = M24_READ(1, &result);
  if (result == false)
  {
    return 1;
  }
  memcpy(&eventsToDownload, buf, 2);
  
  //maxAlarmsEverCount
  maxAlarmsEverCount = M24_READ(2, &result);
  if (result == false)
  {
    return 1;
  }
  
  //alarmsRWPointer;
  buf[0] = M24_READ(3, &result);
  if (result == false)
  {
    return 1;
  }
  buf[1] = M24_READ(4, &result);
  if (result == false)
  {
    return 1;
  }
  memcpy(&alarmsRWPointer, buf, 2);
  
  //alarmsDownloadCounter;
  buf[0] = M24_READ(5, &result);
  if (result == false)
  {
    return 1;
  }
  buf[1] = M24_READ(6, &result);
  if (result == false)
  {
    return 1;
  }
  memcpy(&alarmsDownloadCounter, buf, 2);
  
  return NRF_SUCCESS;
}

void blinkError(uint8_t code)
{
  int i;
  uint8_t dec;
  uint8_t num;
  
  num = code % 10;
  dec = code / 10 % 10;
  
  while (1)
  {
    for (i = 0; i < dec; i++)
    {
      nrf_gpio_pin_set(LED_YELLOW_PIN);
      nrf_delay_ms(800);
      nrf_gpio_pin_clear(LED_YELLOW_PIN);
      nrf_delay_ms(500);
    }
    
    for (i = 0; i < num; i++)
    {
      nrf_gpio_pin_set(LED_YELLOW_PIN);
      nrf_delay_ms(200);
      nrf_gpio_pin_clear(LED_YELLOW_PIN);
      nrf_delay_ms(500);
    }
    
    nrf_delay_ms(3000);
  }
}

/**
 * @brief Initialize LPCOMP driver.
 */
static void lpcomp_init(void)
{
    uint32_t                err_code;

    nrf_drv_lpcomp_config_t config = NRF_DRV_LPCOMP_DEFAULT_CONFIG;
    // initialize LPCOMP driver, from this point LPCOMP will be active and provided
    // event handler will be executed when defined action is detected
    err_code = nrf_drv_lpcomp_init(&config, lpcomp_event_handler);
    APP_ERROR_CHECK(err_code);
    nrf_drv_lpcomp_enable();
}

int main(void)
{
  dataSessionBlinkingEnabled = false;
  justSwitchOffLeds = false;
  isBatteryDischargedFlag = false;
  readDelayFlag = false;
  volatile Status_reg_1_t r1;
  ret_code_t ret;
  
  nrf_drv_gpiote_in_config_t inputPinConfigPullUp = GPIOTE_CONFIG_IN_SENSE_TOGGLE(true);
  inputPinConfigPullUp.pull = NRF_GPIO_PIN_PULLUP;
  
  nrf_drv_gpiote_in_config_t inputPinConfigPullDown = GPIOTE_CONFIG_IN_SENSE_TOGGLE(true);
  inputPinConfigPullDown.pull = NRF_GPIO_PIN_PULLDOWN;
  
  nrf_drv_spi_config_t spi_config =   NRF_DRV_SPI_DEFAULT_CONFIG;
  spi_config.ss_pin   =               NRF_DRV_SPI_PIN_NOT_USED;
  spi_config.miso_pin =               SPI_MISO_PIN;
  spi_config.mosi_pin =               SPI_MOSI_PIN;
  spi_config.sck_pin  =               SPI_SCK_PIN;
  spi_config.frequency =              NRF_DRV_SPI_FREQ_8M;
  spi_config.bit_order =              NRF_DRV_SPI_BIT_ORDER_MSB_FIRST;
  spi_config.mode =                   NRF_DRV_SPI_MODE_0;

  isActivated = 0;
  
  eventsToDownload = 0;
  alarmsRWPointer = 0;
  
  // Настраиваем ватчдог
  nrf_drv_wdt_config_t config = NRF_DRV_WDT_DEAFULT_CONFIG;
  ret = nrf_drv_wdt_init(&config, wdt_event_handler);
  APP_ERROR_CHECK(ret);
  ret = nrf_drv_wdt_channel_alloc(&m_channel_id);
  APP_ERROR_CHECK(ret);
  // Включаем ватчдог
  //nrf_drv_wdt_enable();
  //app_timer_start(longWD_timer, APP_TIMER_TICKS(9000), NULL);
  
  // 12 PIN - EEPROM CS
  nrf_gpio_cfg_output(12);
  nrf_gpio_pin_set(12);
  // 22 PIN - FLASH CS
  nrf_gpio_cfg_output(22);
  // 27 PIN - MFRC522 CS
  nrf_gpio_cfg_output(27);   
  // 28 PIN - MFRC522 RESET
  nrf_gpio_cfg_output(28);   
  
  // disable flash
  nrf_gpio_pin_set(22);
  // disable mfrc522 spi
  nrf_gpio_pin_set(27);
  // enable mfrc522 reset
  nrf_gpio_pin_set(28);
  
  // 10 PIN - MOTOR
  nrf_gpio_cfg(10, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT, NRF_GPIO_PIN_PULLDOWN, NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
  
  // LED RED
  nrf_gpio_cfg_output(LED_RED_PIN);
  // LED BLUE
  nrf_gpio_cfg_output(LED_BLUE_PIN); 
  // LED YELLOW
  nrf_gpio_cfg_output(LED_YELLOW_PIN); 
  
  nrf_gpio_pin_clear(LED_RED_PIN);
  nrf_gpio_pin_clear(LED_BLUE_PIN);
  nrf_gpio_pin_clear(LED_YELLOW_PIN);
  
  // HW INIT
  timer_init();
  db_discovery_init();
  power_management_init();
  ble_stack_init();
  gatt_init();
  nus_c_init();
  scan_init();
  saadc_init();
  twi_master_init();
  nrf_drv_spi_init(&spi, &spi_config, NULL, NULL);
  lpcomp_init();

  // Включаем DC-DC преобразователь
  ret = sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE );
  if (ret != NRF_SUCCESS)
  {
    blinkError(11);
  }

  if(!nrf_drv_gpiote_is_init())
  {
      ret = nrf_drv_gpiote_init();
  }

  // 9 PIN - BREAK SENSOR
  ret = nrf_drv_gpiote_in_init(9, &inputPinConfigPullUp, input_pin_handler);
  if (ret != NRF_SUCCESS)
  {
      blinkError(12);
  }
  
  // 31 PIN - IRQ MFRC522
  ret = nrf_drv_gpiote_in_init(31, &inputPinConfigPullDown, input_pin_handler);
  if (ret != NRF_SUCCESS)
  {
      blinkError(13);
  }
  
  // 3 PIN - CHG SIGNAL BQ51013
  ret = nrf_drv_gpiote_in_init(3, &inputPinConfigPullUp, input_pin_handler);
  if (ret != NRF_SUCCESS)
  {
      blinkError(14);
  }
  
  // 4 PIN - CHG SIGNAL BQ24074RGTR
  ret = nrf_drv_gpiote_in_init(4, &inputPinConfigPullUp, input_pin_handler);
  if (ret != NRF_SUCCESS)
  {
      blinkError(15);
  }
  
  nrf_gpio_pin_set(LED_RED_PIN);

  MFRC522_Init();
  
  // CRYPTO INIT
  ret = nrf_drv_rng_init(NULL);
  if (ret != NRF_SUCCESS)
  {
    blinkError(16);
  }
    
  ret = nrf_crypto_init();
  if (ret != NRF_SUCCESS)
  {
    blinkError(17);
  }
  
  #if NRF_CRYPTO_BACKEND_MBEDTLS_ENABLED
    ret = nrf_mem_init();
    if (ret != NRF_SUCCESS)
    {
      blinkError(18);
    }
    
    cipher_info = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
  #endif
  
  // MFRC522 CHECK
  ret =  MFRC522_ReadRegister(MFRC522_REG_VERSION);
  if (ret != 0x92)
  {
    blinkError(19);
  }
  
  // FLASH RESET
  w25q128_reset();
  
  // FLASH ID CHECK
  if (w25q128_checkFlashID() != NRF_SUCCESS)
  {
    blinkError(21);
  }
  
  // Проверка EEPROM
  if (M24_TEST() != NRF_SUCCESS)
  {
    blinkError(22);
  }

  // Проверка первого включения
  if (checkIsFirstBoot())
  {
    nrf_gpio_pin_set(LED_YELLOW_PIN);
    
    doFirstBootInit();
    
    settingsCommit();
    
    if (stateWrite() != NRF_SUCCESS)
    {
      blinkError(23);
    }
    
    nrf_gpio_pin_clear(LED_YELLOW_PIN);
  } else
  {
      settingsRead();
      
      if (stateRead() != NRF_SUCCESS)
      {
        blinkError(24);
      }
  }

  nrf_gpio_pin_clear(LED_RED_PIN);
  
  // Включаем событие на датчик удара
  enableShockSensor();
  
  // Включаем событие обнаружения катушки питания
  nrf_drv_gpiote_in_event_enable(3, true);
  
  // Включаем событие обнаружения зарядки
  nrf_drv_gpiote_in_event_enable(4, true);
  
  //nrf_drv_gpiote_in_event_enable(31, true);
  
  restartReaderTimer();
  
  for (;;)
  {
    idle_state_handle();
  }
}
