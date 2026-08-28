#ifndef _W25_
#define _W25_

typedef enum {
    W25_WRITE_DISABLE = 0x04,
    W25_WRITE_ENABLE = 0x06,
    W25_REGISTER_WRITE_ENABLE = 0x50,
     
    W25_READ_STATUS_1 = 0x05,
    W25_READ_STATUS_2 = 0x35,
    W25_READ_STATUS_3 = 0x15,
     
    W25_WRITE_STATUS_1 = 0x01,
    W25_WRITE_STATUS_2 = 0x31,
    W25_WRITE_STATUS_3 = 0x11,
     
    W25_CHIP_ERASE = 0xc7, //0x60
     
    W25_GET_DEV_ID = 0x90,
    W25_GET_JEDEC_ID = 0x9f,
     
    W25_ENABLE_RESET = 0x66,
    W25_RESET = 0x99,
     
    W25_SECTOR4_ERASE = 0x20,
    W25_SECTOR64_ERASE = 0xD8,
    
    W25_PAGE_PROGRAMM = 0x02,
    W25_READ = 0x03,
    
    W25_EXIT_QPI = 0xFF,
} W25_Command_t;

typedef struct {
    uint8_t busy : 1;
    uint8_t write_enable : 1;
    uint8_t block_protect : 3;
    uint8_t top_bot_ptotect : 1;
    uint8_t sector_protect : 1;
    uint8_t status_reg_protect0 : 1;
} STATUS_REG1_STRUCT_t;

typedef struct {
    uint8_t SRP1 : 1;
    uint8_t QE : 1;
    uint8_t R : 1;
    uint8_t LOCKBITS : 3;
    uint8_t CMP: 1;
    uint8_t SUS: 1;
} STATUS_REG2_STRUCT_t;

typedef struct {
    uint8_t Reserved0 : 1;
    uint8_t Reserved1 : 1;
    uint8_t WPS : 1;
    uint8_t Reserved2 : 1;
    uint8_t Reserved3 : 1;
    uint8_t DRV0 : 1;
    uint8_t DRV1: 1;
    uint8_t HOLD_or_RST: 1;
} STATUS_REG3_STRUCT_t;
 
typedef union {
    uint8_t all;
    STATUS_REG1_STRUCT_t bit;
} Status_reg_1_t;

typedef union {
    uint8_t all;
    STATUS_REG2_STRUCT_t bit;
} Status_reg_2_t;

typedef union {
    uint8_t all;
    STATUS_REG3_STRUCT_t bit;
} Status_reg_3_t;

#endif