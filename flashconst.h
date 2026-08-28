#ifndef _FLASHCONST_
#define _FLASHCONST_

/*
typedef __packed struct _BreakEvent
{
  uint8_t dateTime[14];
} TBreakEvent;

typedef __packed struct _NormalEvent
{
  uint8_t number[7];
  uint8_t dateTime[14];
} TNormalEvent;

typedef __packed struct _Settings
{
  uint16_t eventsCount;
  uint16_t breaksCount;
  uint8_t secret_word[16];
  uint8_t device_serial[6];
  uint8_t deviceType;
} TSettings;
*/
#define FWBLKSIZE                                                               120
#define W25Q_FLASH_WR_ATTEMPTS						        3
#define W25Q_FLASH_SECTORSIZE						        4096
#define W25Q_FLASH_PAGESIZE                                                     256

#define MAX_EVENTS_COUNT        					        15000
#define MAX_ALARMS_COUNT        					        100

#define EVENT_RECORD_SIZE       					        21
#define ALARM_RECORD_SIZE       					        14

#define IS_FIRST_BOOT_MARKER_SIZE      	                                        2
#define IS_FIRST_BOOT_MARKER_ADDR      	                                        0x0

#define IS_NEW_FIRMWARE_MARKER_SIZE      	                                2
#define IS_NEW_FIRMWARE_MARKER_ADDR      	                                IS_FIRST_BOOT_MARKER_ADDR + IS_FIRST_BOOT_MARKER_SIZE //0xDA01

#define IS_ACTIVATED_SIZE       					        1
#define IS_ACTIVATED_ADDR       					        IS_NEW_FIRMWARE_MARKER_ADDR + IS_NEW_FIRMWARE_MARKER_SIZE

#define DEVICE_SECRET_SIZE        					        16
#define DEVICE_SECRET_ADDR							IS_ACTIVATED_ADDR + IS_ACTIVATED_SIZE

#define DEVICE_SERIAL_SIZE      					        6
#define DEVICE_SERIAL_ADDR      					        DEVICE_SECRET_ADDR + DEVICE_SECRET_SIZE

#define DEVICE_TYPE_SIZE          					        1
#define DEVICE_TYPE_ADDR          					        DEVICE_SERIAL_ADDR + DEVICE_SERIAL_SIZE

#define CURRENT_EVENTS_COUNT_RECORD_SIZE  	                                4
#define CURRENT_EVENTS_COUNT_RECORD_ADDR  	                                DEVICE_TYPE_ADDR + DEVICE_TYPE_SIZE

#define CURRENT_ALARMS_COUNT_RECORD_SIZE  	                                4
#define CURRENT_ALARMS_COUNT_RECORD_ADDR 	                                CURRENT_EVENTS_COUNT_RECORD_ADDR + CURRENT_EVENTS_COUNT_RECORD_SIZE

#define EVENT_RECORDS_ADDR       					        CURRENT_ALARMS_COUNT_RECORD_ADDR + CURRENT_ALARMS_COUNT_RECORD_SIZE
#define ALARM_RECORDS_ADDR       					        EVENT_RECORDS_ADDR + (EVENT_RECORD_SIZE * MAX_EVENTS_COUNT)

// Адрес начала образа обновления в SPI флешке
#define FW_IMAGE_POINTER_ADDR							ALARM_RECORDS_ADDR + (ALARM_RECORD_SIZE * MAX_ALARMS_COUNT)
#define FW_IMAGE_POINTER_SIZE							sizeof(uint32_t)

#define FW_IMAGE_START_ADDR							FW_IMAGE_POINTER_ADDR + FW_IMAGE_POINTER_SIZE
#define FW_IMAGE_START_SIZE                                                     sizeof(uint32_t)

// Размер образа прошивки в блоках по x байт (в образ не входит размер и сумма, они пишутся отдельными командами)
#define FW_IMAGE_SIZE_ADDR                                                      FW_IMAGE_START_ADDR + FW_IMAGE_START_SIZE
#define FW_IMAGE_SIZE_SIZE                                                      sizeof(uint32_t)

// Контрольная сумма прошивки
#define FW_IMAGE_SUMM_ADDR                                                      FW_IMAGE_SIZE_ADDR + FW_IMAGE_SIZE_SIZE
#define FW_IMAGE_SUMM_SIZE                                                      sizeof(uint32_t)

#endif //_FLASHCONST_