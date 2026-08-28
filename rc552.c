#include "rc522.h"

void MFRC522_WriteRegister(uint8_t addr, uint8_t val)
{
	addr = (addr << 1) & 0x7E;           // Address format: 0XXXXXX0
        SPI1_WriteReg(addr, val);
}

uint8_t MFRC522_ReadRegister(uint8_t addr)
{
	uint8_t val;

	addr = ((addr << 1) & 0x7E) | 0x80;
	val = SPI1_ReadReg(addr);
	return val;	
}

uint8_t MFRC522_Compare(uint8_t* CardID, uint8_t* CompareID) 
{
	uint8_t i;
        
	for (i = 0; i < 5; i++) 
        {
		if (CardID[i] != CompareID[i]) return MI_ERR;
	}
        
	return MI_OK;
}

void MFRC522_SetBitMask(uint8_t reg, uint8_t mask) 
{
	MFRC522_WriteRegister(reg, MFRC522_ReadRegister(reg) | mask);
}

void MFRC522_ClearBitMask(uint8_t reg, uint8_t mask)
{
	MFRC522_WriteRegister(reg, MFRC522_ReadRegister(reg) & (~mask));
}

uint8_t MFRC522_Request(uint8_t reqMode, uint8_t* TagType)
{
	uint8_t status;  
	uint16_t backBits;																			// The received data bits

	MFRC522_WriteRegister(MFRC522_REG_BIT_FRAMING, 0x07);		// TxLastBists = BitFramingReg[2..0]
	TagType[0] = reqMode;
	status = MFRC522_ToCard(PCD_TRANSCEIVE, TagType, 1, TagType, &backBits);
	if ((status != MI_OK) || (backBits != 0x10)) 
          status = MI_ERR;
	return status;
}

uint8_t MFRC522_ToCard(uint8_t command, uint8_t* sendData, uint8_t sendLen, uint8_t* backData, uint16_t* backLen) 
{
	uint8_t status = MI_ERR;
	uint8_t irqEn = 0x00;
	uint8_t waitIRq = 0x00;
	uint8_t lastBits;
	uint8_t n;
	uint16_t i;

	switch (command) 
        {
		case PCD_AUTHENT:
                  {
			irqEn = 0x12;
			waitIRq = 0x10;
			break;
                  }
		case PCD_TRANSCEIVE:
                  {
			irqEn = 0x77;
			waitIRq = 0x30;
			break;
                  }
		default:
		break;
	}

	MFRC522_WriteRegister(MFRC522_REG_COMM_IE_N, irqEn | 0x80);
	MFRC522_ClearBitMask(MFRC522_REG_COMM_IRQ, 0x80);
	MFRC522_SetBitMask(MFRC522_REG_FIFO_LEVEL, 0x80);
	MFRC522_WriteRegister(MFRC522_REG_COMMAND, PCD_IDLE);

	// Writing data to the FIFO
	for (i = 0; i < sendLen; i++) 
          MFRC522_WriteRegister(MFRC522_REG_FIFO_DATA, sendData[i]);

	// Execute the command
	MFRC522_WriteRegister(MFRC522_REG_COMMAND, command);
        
	if (command == PCD_TRANSCEIVE)
          MFRC522_SetBitMask(MFRC522_REG_BIT_FRAMING, 0x80);		// StartSend=1,transmission of data starts 

	// Waiting to receive data to complete
	i = 2000;	// i according to the clock frequency adjustment, the operator M1 card maximum waiting time 25ms
	do {
		// CommIrqReg[7..0]
		// Set1 TxIRq RxIRq IdleIRq HiAlerIRq LoAlertIRq ErrIRq TimerIRq
		n = MFRC522_ReadRegister(MFRC522_REG_COMM_IRQ);
		i--;
	} while ((i!=0) && !(n&0x01) && !(n&waitIRq));

	MFRC522_ClearBitMask(MFRC522_REG_BIT_FRAMING, 0x80);																// StartSend=0

	if (i != 0)  
        {
		if (!(MFRC522_ReadRegister(MFRC522_REG_ERROR) & 0x1B))
                {
			status = MI_OK;
			if (n & irqEn & 0x01)
                          status = MI_NOTAGERR;
			if (command == PCD_TRANSCEIVE) 
                        {
				n = MFRC522_ReadRegister(MFRC522_REG_FIFO_LEVEL);
				lastBits = MFRC522_ReadRegister(MFRC522_REG_CONTROL) & 0x07;
				if (lastBits) *backLen = (n-1)*8+lastBits; else *backLen = n*8;
				if (n == 0) n = 1;
				if (n > MFRC522_MAX_LEN) n = MFRC522_MAX_LEN;
				for (i = 0; i < n; i++) backData[i] = MFRC522_ReadRegister(MFRC522_REG_FIFO_DATA);		// Reading the received data in FIFO
			}
		} else 
                  status = MI_ERR;
	}
	return status;
}

uint8_t MFRC522_Anticoll(uint8_t* serNum)
{
	uint8_t status;
	uint8_t i;
	uint8_t serNumCheck = 0;
	uint16_t unLen;

	MFRC522_WriteRegister(MFRC522_REG_BIT_FRAMING, 0x00);												// TxLastBists = BitFramingReg[2..0]
	serNum[0] = PICC_ANTICOLL;
	serNum[1] = 0x20;
	status = MFRC522_ToCard(PCD_TRANSCEIVE, serNum, 2, serNum, &unLen);
	if (status == MI_OK) 
        {
		// Check card serial number
		for (i = 0; i < 4; i++)
                  serNumCheck ^= serNum[i];
		if (serNumCheck != serNum[i]) 
                  status = MI_ERR;
	}
        
        lastAntiColl = serNum[0];

	return status;
} 

uint8_t MFRC522_Anticoll2(uint8_t* serNum)
{
	uint8_t status;
	uint8_t i;
	uint8_t serNumCheck = 0;
	uint16_t unLen;

	MFRC522_WriteRegister(MFRC522_REG_BIT_FRAMING, 0x00);												// TxLastBists = BitFramingReg[2..0]
	serNum[0] = PICC_ANTICOLL2;
	serNum[1] = 0x20;
	status = MFRC522_ToCard(PCD_TRANSCEIVE, serNum, 2, serNum, &unLen);
	if (status == MI_OK) 
        {
		// Check card serial number
		for (i = 0; i < 4; i++)
                  serNumCheck ^= serNum[i];
		if (serNumCheck != serNum[i]) 
                  status = MI_ERR;
	}
        
        lastAntiColl = serNum[0];

	return status;
} 

void MFRC522_CalculateCRC(uint8_t*  pIndata, uint8_t len, uint8_t* pOutData)
{
	uint8_t i, n;

	MFRC522_ClearBitMask(MFRC522_REG_DIV_IRQ, 0x04);													// CRCIrq = 0
	MFRC522_SetBitMask(MFRC522_REG_FIFO_LEVEL, 0x80);													// Clear the FIFO pointer
	// Write_MFRC522(CommandReg, PCD_IDLE);

	// Writing data to the FIFO	
	for (i = 0; i < len; i++) 
          MFRC522_WriteRegister(MFRC522_REG_FIFO_DATA, *(pIndata+i));
	MFRC522_WriteRegister(MFRC522_REG_COMMAND, PCD_CALCCRC);

	// Wait CRC calculation is complete
	i = 0xFF;
	do {
		n = MFRC522_ReadRegister(MFRC522_REG_DIV_IRQ);
		i--;
	} while ((i!=0) && !(n&0x04));						// CRCIrq = 1

	// Read CRC calculation result
	pOutData[0] = MFRC522_ReadRegister(MFRC522_REG_CRC_RESULT_L);
	pOutData[1] = MFRC522_ReadRegister(MFRC522_REG_CRC_RESULT_M);
}

uint8_t MFRC522_WritePersoSECTOR(uint16_t addr) 
{
	uint8_t status;
	uint16_t recvBits;
	uint8_t buffer[21]; 
        
	buffer[0] = 0xA8;
        
        buffer[1] = (uint8_t)(addr & 0xFF); /* LSB */
        buffer[2] = (uint8_t)(addr >> 8);   /* MSB */
        
        memcpy(&buffer[3], AES_KEY, 16);
        
        MFRC522_CalculateCRC(buffer, 19, &buffer[19]);
        
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buffer, 21, buffer, &recvBits);
        
	if (status == MI_OK)
          if (buffer[0] == 0x0A)
            return 1;
        
	return 0;
}

uint8_t MFRC522_WritePersoCOMMIT() 
{
	uint8_t status;
	uint16_t recvBits;
	uint8_t buffer[3]; 
        
	buffer[0] = 0xAA;
        
        MFRC522_CalculateCRC(buffer, 1, &buffer[1]);
        
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buffer, 3, buffer, &recvBits);
        
	if (status == MI_OK)
          if (buffer[0] == 0x0A)
            return 1;
        
	return 0;
}

uint8_t MFRC522_RATS() 
{
	uint8_t status;
	uint16_t recvBits;
	uint8_t buffer[4]; 
        uint8_t recv_buffer[14];
        
	buffer[0] = PICC_RATS;
	buffer[1] = 0x50;
        
        MFRC522_CalculateCRC(buffer, 2, &buffer[2]);
        
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buffer, 4, recv_buffer, &recvBits);
        
	if (status == MI_OK)
          if (buffer[0] == 0x0A)
            return 1;
        
        //lastSAK = recvBits;
        
	return 0;
}

uint8_t MFRC522_FIRSTAUTH_1() 
{
    ret_code_t ret;
    size_t      len_in;
    len_in = 16;
    size_t      len_out;
    len_out = 16;
  
    uint8_t status;
    uint16_t recvBits;
    uint8_t buffer[36]; 
    uint8_t recv_buffer[36];
    uint8_t tmp[48];
        
    uint8_t rnbB[16];
    uint8_t rnbA[16];
       
    memset(buffer, 0x0, 36);
    memset(tmp, 0x0, 48);
    memset(recv_buffer, 0x0, 36);
        
    memset(rnbB, 0x0, 16);
    memset(rnbA, 0x0, 16);
        
    //buffer[0] = 0x02; //02
    buffer[0] = 0x76; //firstauth
    buffer[1] = 0x03; // keynum1
    buffer[2] = 0x90; // keynum2
        
    MFRC522_CalculateCRC(buffer, 3, &buffer[3]);
        
    status = MFRC522_ToCard(PCD_TRANSCEIVE, buffer, 5, recv_buffer, &recvBits);
        
	if (status == MI_OK)
          if (recv_buffer[0] == 0x02)
            if (recv_buffer[1] == 0x90)
            {
                  memcpy(&tmp[0], &recv_buffer[2], 16);
                  
                  // Generating rndA
                  random_vector_generate(rnbA, 16);
                  
                  memset(iv, 0, sizeof(iv));
                  
                  // Unencrypt rndB
                  ret = nrf_crypto_aes_crypt(&cbc_decr_128_ctx, &g_nrf_crypto_aes_cbc_128_info, NRF_CRYPTO_DECRYPT, AES_KEY, iv, tmp, len_in, recv_buffer, &len_out);
                  APP_ERROR_CHECK(ret);

                  // Rotate left rndB
                  memcpy(&rnbB[0], &recv_buffer[1], 15);
                  rnbB[15] = recv_buffer[0];
                  
                  memset(buffer, 0x0, 36);
                  memset(recv_buffer, 0x0, 36);
                  
                  //buffer[0] = 0x03; // 03
                  buffer[0] = 0x72; // firstauth 2
                  
                  memcpy(&tmp[0],  &rnbA, 16);
                  
                  //memset(iv, 0, sizeof(iv));
                  
                  // Encrypt rndA
                  ret = nrf_crypto_aes_crypt(&cbc_decr_128_ctx, &g_nrf_crypto_aes_cbc_128_info, NRF_CRYPTO_ENCRYPT, AES_KEY, iv, tmp, len_in, &buffer[1], &len_out);
                  APP_ERROR_CHECK(ret);
                  
                  memcpy(&tmp[0], &rnbB, 16);
                  
                  //memset(iv, 0, sizeof(iv));
                  
                  // Encrypt rndB
                  ret = nrf_crypto_aes_crypt(&cbc_decr_128_ctx, &g_nrf_crypto_aes_cbc_128_info, NRF_CRYPTO_ENCRYPT, AES_KEY, iv, tmp, len_in, &buffer[17], &len_out);
                  APP_ERROR_CHECK(ret);
                  
                  MFRC522_CalculateCRC(buffer, 33, &buffer[33]);
                  
                  status = MFRC522_ToCard(PCD_TRANSCEIVE, buffer, 35, recv_buffer, &recvBits);
                  
                  if (status == MI_OK)
                    if (recv_buffer[1] != 0x06)
                    {
                      return 1;
                    }
                  
                  return 0;
            }
        
	return 0;
}

uint8_t MFRC522_FIRSTAUTH_3(uint16_t addr, bool onlyAuth, uint8_t* cardNum) 
{
    ret_code_t ret;
    size_t      len_in;
    len_in = 16;
    size_t      len_out;
    len_out = 16;
  
    uint8_t status;
    uint16_t recvBits;
    uint8_t buffer[36]; 
    uint8_t recv_buffer[36];
    uint8_t tmp[48];
        
    uint8_t rnbB[16];
    uint8_t rnbB_shifted[16];
    uint8_t rnbB_shifted_coded[16];
    uint8_t rnbA[16];
    uint8_t rnbA_coded[16];
    uint8_t authPart2Response[32];
    uint8_t TI[4];
    uint8_t F[5], G[5], H[5], I[5], J[5];
    uint8_t KMAC[16];
    
    memset(buffer, 0x0, 36);
    memset(tmp, 0x0, 48);
    memset(TI, 0x0, 4);
    memset(recv_buffer, 0x0, 36);

    memset(rnbB, 0x0, 16);
    memset(rnbB_shifted, 0x0, 16);
    memset(rnbB_shifted_coded, 0x0, 16);
    memset(rnbA, 0x0, 16);

    buffer[0] = 0x02; //02
    buffer[1] = 0x70; //firstauth
    buffer[2] = (uint8_t)(addr & 0xFF); /* LSB */
    buffer[3] = (uint8_t)(addr >> 8);   /* MSB */
    buffer[4] = 0x00; // len cap (0)
    
    *cardNum = 0;

    MFRC522_CalculateCRC(buffer, 5, &buffer[5]);

    status = MFRC522_ToCard(PCD_TRANSCEIVE, buffer, 7, recv_buffer, &recvBits);

    if (status == MI_OK)
      if (recv_buffer[0] == 0x02)
        if (recv_buffer[1] == 0x90)
        {
          memcpy(tmp, &recv_buffer[2], 16);
          memset(iv, 0, sizeof(iv));
                  
          // Generating rndA
          random_vector_generate(rnbA, 16);
                  
          // Unencrypt rndB
          ret = nrf_crypto_aes_crypt(&cbc_decr_128_ctx, &g_nrf_crypto_aes_cbc_128_info, NRF_CRYPTO_DECRYPT, AES_KEY, iv, tmp, len_in, rnbB, &len_out);
          APP_ERROR_CHECK(ret);

          // Rotate left rndB
          memcpy(&rnbB_shifted[0], &rnbB[1], 15);
          rnbB_shifted[15] = rnbB[0];
                  
          memset(buffer, 0x0, 36);
          memset(recv_buffer, 0x0, 36);
                  
          buffer[0] = 0x03; // 03
          buffer[1] = 0x72; // firstauth 2
                  
          // Encrypt rndA
          ret = nrf_crypto_aes_crypt(&cbc_decr_128_ctx, &g_nrf_crypto_aes_cbc_128_info, NRF_CRYPTO_ENCRYPT, AES_KEY, iv, rnbA, len_in, rnbA_coded, &len_out);
          APP_ERROR_CHECK(ret);
                  
          // Encrypt rndB'
          ret = nrf_crypto_aes_crypt(&cbc_decr_128_ctx, &g_nrf_crypto_aes_cbc_128_info, NRF_CRYPTO_ENCRYPT, AES_KEY, rnbA_coded, rnbB_shifted, len_in, rnbB_shifted_coded, &len_out);
          APP_ERROR_CHECK(ret);
          
          memcpy(&buffer[2], &rnbA_coded, 16);
          memcpy(&buffer[18], &rnbB_shifted_coded, 16);
                  
          MFRC522_CalculateCRC(buffer, 34, &buffer[34]);
                  
          memset(recv_buffer, 0x0, 36);
          status = MFRC522_ToCard(PCD_TRANSCEIVE, &buffer[0], 36, recv_buffer, &recvBits);
                  
          if (status == MI_OK)
          {
            if (recv_buffer[0] == 0x03)
            {
              if (recv_buffer[1] == 0x90)
              {
                if (onlyAuth)
                  return NRF_SUCCESS;
                    
                len_in = 32;
                len_out = 32;
                
                // Unencrypt second response
                ret = nrf_crypto_aes_crypt(&cbc_decr_128_ctx, &g_nrf_crypto_aes_cbc_128_info, NRF_CRYPTO_DECRYPT, AES_KEY, iv, &recv_buffer[2], len_in, authPart2Response, &len_out);
                APP_ERROR_CHECK(ret);
                
                if (ret == NRF_SUCCESS)
                {
                  // Checking rndA'
                  if (rnbA[0] == authPart2Response[19])
                  {
                    // Setting TI
                    memcpy(TI, authPart2Response, 4);
                    
                    // Calculating MAC base
                    memcpy(F, &rnbA[7], 5);
                    memcpy(G, &rnbB[7], 5);
                    memcpy(H, &rnbA[0], 5);
                    memcpy(I, &rnbB[0], 5);
                    
                    J[0] = H[0] ^ I[0];
                    J[1] = H[1] ^ I[1];
                    J[2] = H[2] ^ I[2];
                    J[3] = H[3] ^ I[3];
                    J[4] = H[4] ^ I[4];
                    
                    memcpy(&KMAC[0], F, 5);
                    memcpy(&KMAC[5], G, 5);
                    memcpy(&KMAC[10], J, 5);
                    KMAC[15] = 0x22;
                    
                    len_in = 16;
                    len_out = 16;
                    
                    // Encrypt KMAC
                    ret = nrf_crypto_aes_crypt(&cbc_decr_128_ctx, &g_nrf_crypto_aes_cbc_128_info, NRF_CRYPTO_ENCRYPT, AES_KEY, iv, KMAC, len_in, KMAC, &len_out);
                    APP_ERROR_CHECK(ret);
                    
                    uint8_t payload[25];
                    
                    memset(payload, 0x0, 25);
                    
                    // WRITE
#ifdef AUTH3_WRITE
                    buffer[0] = 0x02; // 02 -4 lev block num
                    buffer[1] = 0xA3; // cmd
                    buffer[2] = 0x09; // block num
                    buffer[3] = 0x00; // block num
                    
                    memcpy(&buffer[4], SL3_SECTOR_DATA, 16);
                    
                    payload[0] = 0xA3;
                    payload[1] = 0x00;
                    payload[2] = 0x00;
                    memcpy(&payload[3], TI, 4);
                    payload[7] = 0x09;
                    payload[8] = 0x00;
                    memcpy(&payload[9], &buffer[4], 16);

                    calculate_MAC(payload, 25, KMAC, &buffer[20]);
                    
                    MFRC522_CalculateCRC(buffer, 28, &buffer[28]);
                    
                    memset(recv_buffer, 0x0, 36);
                    
                    status = MFRC522_ToCard(PCD_TRANSCEIVE, buffer, 30, recv_buffer, &recvBits);
                    
                    if (status == MI_OK)
                      if (recv_buffer[0] == 0x02)
                        if (recv_buffer[1] == 0x90)
                        {
                          return NRF_SUCCESS;
                        }
#endif

                    

                    //READ
#ifdef AUTH3_READ
                    buffer[0] = 0x02; // 02 -4 lev block num
                    buffer[1] = 0x33; // cmd
                    buffer[2] = 0x09; // block num
                    buffer[3] = 0x00; // block num
                    buffer[4] = 0x01;
                    
                    //MAC PAYLOAD = CC + R_CTR + TI + BNr + Ext
                    payload[0] = 0x33;
                    payload[1] = 0x00;
                    payload[2] = 0x00;
                    memcpy(&payload[3], TI, 4);
                    payload[7] = 0x09; // block num
                    payload[8] = 0x00; // block num
                    payload[9] = 0x01; // block num
                    
                    calculate_MAC(payload, 10, KMAC, &buffer[5]);
                    
                    memset(recv_buffer, 0x0, 36);
                    
                    MFRC522_CalculateCRC(buffer, 13, &buffer[13]);
                    
                    status = MFRC522_ToCard(PCD_TRANSCEIVE, buffer, 15, recv_buffer, &recvBits);
                    
                    if (status == MI_OK)
                    {
                      if (recv_buffer[0] == 0x02)
                      {
                        if (recv_buffer[1] == 0x90)
                        {
                          if (memcmp(SL3_SECTOR_DATA, &recv_buffer[2], 15) == 0)
                          {
                            *cardNum = recv_buffer[17];
                            return NRF_SUCCESS;
                          }
                        }
                      }
                    }
#endif
                  }
                }
              }
            }
          }
        }
        
	return NRF_ERROR_INTERNAL;
}

/*
uint8_t MFRC522_FOLLOWAUTH() 
{
	uint8_t status;
	uint16_t recvBits;
	uint8_t buffer[6]; 
        uint8_t recv_buffer[19];
        
        memset(buffer, 0x0, 6);
        memset(recv_buffer, 0x0, 19);
        
        buffer[0] = 0x02; //02
	buffer[1] = 0x76; //follow auth
	buffer[2] = 0x00; // keynum1
        buffer[3] = 0x00; // keynum2
        
        MFRC522_CalculateCRC(buffer, 4, &buffer[4]);
        
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buffer, 6, recv_buffer, &recvBits);
        
	if (status == MI_OK)
        {
          recv_buffer[17] = 0;
          recv_buffer[18] = 0;
          
          MFRC522_CalculateCRC(recv_buffer, 17, &recv_buffer[17]);
        }
        
	return 0;
}
*/

uint8_t MFRC522_SelectTag(uint8_t* serNum) 
{
	uint8_t i;
	uint8_t status;
	uint8_t size;
	uint16_t recvBits;
	uint8_t buffer[9]; 
        
        isDoubleUID = false;

	buffer[0] = PICC_SElECTTAG;
	buffer[1] = 0x70;
	for (i = 0; i < 5; i++) buffer[i+2] = *(serNum+i);
	MFRC522_CalculateCRC(buffer, 7, &buffer[7]);		//??
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buffer, 9, buffer, &recvBits);
        
	if ((status == MI_OK) && (recvBits == 0x18))
          size = buffer[0]; else size = 0;
        
        lastSAK = buffer[0];
        
	return size;
}

uint8_t MFRC522_SelectTag2(uint8_t* serNum) 
{
	uint8_t i;
	uint8_t status;
	uint8_t size;
	uint16_t recvBits;
	uint8_t buffer[9]; 
        
        isDoubleUID = true;

	buffer[0] = PICC_SElECTTAG2;
	buffer[1] = 0x70;
	for (i = 0; i < 5; i++) buffer[i+2] = *(serNum+i);
	MFRC522_CalculateCRC(buffer, 7, &buffer[7]);		//??
        
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buffer, 9, buffer, &recvBits);
	if ((status == MI_OK) && (recvBits == 0x18)) 
          size = buffer[0]; 
        else size = 0;
        
        lastSAK = buffer[0];
        
	return size;
}

uint8_t MFRC522_Auth(uint8_t authMode, uint8_t BlockAddr, uint8_t* Sectorkey, uint8_t* serNum)
{
	uint8_t status;
	uint16_t recvBits;
	uint8_t i;
	uint8_t buff[12]; 

	// Verify the command block address + sector + password + card serial number
	buff[0] = authMode;
	buff[1] = BlockAddr;
	for (i = 0; i < 6; i++) buff[i+2] = *(Sectorkey+i);
	for (i=0; i<4; i++) buff[i+8] = *(serNum+i);
	status = MFRC522_ToCard(PCD_AUTHENT, buff, 12, buff, &recvBits);
	if ((status != MI_OK) || (!(MFRC522_ReadRegister(MFRC522_REG_STATUS2) & 0x08))) status = MI_ERR;
	return status;
}

uint8_t MFRC522_Read(uint8_t blockAddr, uint8_t* recvData) 
{
	uint8_t status;
	uint16_t unLen;

	recvData[0] = PICC_READ;
	recvData[1] = blockAddr;
	MFRC522_CalculateCRC(recvData,2, &recvData[2]);
	status = MFRC522_ToCard(PCD_TRANSCEIVE, recvData, 4, recvData, &unLen);
	if ((status != MI_OK) || (unLen != 0x90))
          status = MI_ERR;
	return status;
}

uint8_t MFRC522_Write(uint8_t blockAddr, uint8_t* writeData)
{
	uint8_t status;
	uint16_t recvBits;
	uint8_t i;
	uint8_t buff[18]; 

	buff[0] = PICC_WRITE;
	buff[1] = blockAddr;
	MFRC522_CalculateCRC(buff, 2, &buff[2]);
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buff, 4, buff, &recvBits);
	if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A)) status = MI_ERR;
	if (status == MI_OK) {
		// Data to the FIFO write 16Byte
		for (i = 0; i < 16; i++) buff[i] = *(writeData+i);
		MFRC522_CalculateCRC(buff, 16, &buff[16]);
		status = MFRC522_ToCard(PCD_TRANSCEIVE, buff, 18, buff, &recvBits);
		if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A)) status = MI_ERR;
	}
	return status;
}

void MFRC522_Init(void)
{
	MFRC522_Reset();
	MFRC522_WriteRegister(MFRC522_REG_T_MODE, 0x8D);
	MFRC522_WriteRegister(MFRC522_REG_T_PRESCALER, 0x3E);
	MFRC522_WriteRegister(MFRC522_REG_T_RELOAD_L, 30);           
	MFRC522_WriteRegister(MFRC522_REG_T_RELOAD_H, 0);
	MFRC522_WriteRegister(MFRC522_REG_RF_CFG, 0x50);				// 38dB gain	
	MFRC522_WriteRegister(MFRC522_REG_TX_AUTO, 0x40);
	MFRC522_WriteRegister(MFRC522_REG_MODE, 0x3D);
	//MFRC522_AntennaOn();								// Open the antenna
        
        MFRC522_WriteRegister(MFRC522_REG_COMM_IRQ,  0x80);
        MFRC522_WriteRegister(MFRC522_REG_COMM_IE_N, 0x7F);
        MFRC522_WriteRegister(MFRC522_REG_DIV1_EN,   0x14);
}

void MFRC522_Reset(void)
{
	MFRC522_WriteRegister(MFRC522_REG_COMMAND, PCD_RESETPHASE);
}

void MFRC522_Sleep(void)
{
  MFRC522_WriteRegister(MFRC522_REG_COMMAND, 0x10);
}

void MFRC522_Wakeup(void)
{
  MFRC522_WriteRegister(MFRC522_REG_COMMAND, 0x0);
}

void MFRC522_AntennaOn(void)
{
	uint8_t temp;

	temp = MFRC522_ReadRegister(MFRC522_REG_TX_CONTROL);
	if (!(temp & 0x03)) MFRC522_SetBitMask(MFRC522_REG_TX_CONTROL, 0x03);
}

void MFRC522_AntennaOff(void)
{
	MFRC522_ClearBitMask(MFRC522_REG_TX_CONTROL, 0x03);
}

void MFRC522_Halt(void)
{
	uint16_t unLen;
	uint8_t buff[4]; 

	buff[0] = PICC_HALT;
	buff[1] = 0;
	MFRC522_CalculateCRC(buff, 2, &buff[2]);
	MFRC522_ToCard(PCD_TRANSCEIVE, buff, 4, buff, &unLen);
}

static uint8_t random_vector_generate(uint8_t * p_buff, uint8_t size)
{
    uint32_t err_code;
    uint8_t  available;

    nrf_drv_rng_bytes_available(&available);
    uint8_t length = MIN(size, available);

    err_code = nrf_drv_rng_rand(p_buff, length);
    APP_ERROR_CHECK(err_code);

    return length;
}

uint8_t calculate_MAC(uint8_t *data, uint8_t dataSize, uint8_t *KEY, uint8_t *MAC)
{
  ret_code_t ret;
  uint8_t temp[16];
  
  mbedtls_cipher_init(&m_ctx);
  
  ret = mbedtls_cipher_setup( &m_ctx, cipher_info );
  if (ret != NRF_SUCCESS)
    return ret;
  
  ret = mbedtls_cipher_cmac_starts(&m_ctx, KEY, 128);
  if (ret != NRF_SUCCESS)
    return ret;
  
  ret = mbedtls_cipher_cmac_update(&m_ctx, data, dataSize);
  if (ret != NRF_SUCCESS)
    return ret;
  
  ret = mbedtls_cipher_cmac_finish(&m_ctx, temp);
  if (ret != NRF_SUCCESS)
    return ret;
  
  MAC[0] = temp[1];
  MAC[1] = temp[3];
  MAC[2] = temp[5];
  MAC[3] = temp[7];
  MAC[4] = temp[9];
  MAC[5] = temp[11];
  MAC[6] = temp[13];
  MAC[7] = temp[15];
  
  mbedtls_cipher_free(&m_ctx);
    
  return NRF_SUCCESS;
}