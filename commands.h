#ifndef COMMANDS
#define COMMANDS

uint8_t str_DUSTART[]   =  {'D', 'U', 'S', 'T', 'A', 'R', 'T'};                 // dongle update start
uint8_t str_DUSIZE[]    =  {'D', 'U', 'S', 'I', 'Z', 'E'};                      // dongle update size
uint8_t str_DUSUMM[]    =  {'D', 'U', 'S', 'U', 'M', 'M'};                      // dongle update summ
uint8_t str_DUB[]       =  {'D', 'U', 'B'};                                     // dongle update block

uint8_t str_SNDTST[]   =  {'S', 'N', 'D', 'T', 'S', 'T'};                       // sendtest
uint8_t str_DRST[]     =  {'D', 'R', 'S', 'T'};                                 // dongle reset
uint8_t str_RCREG[]    =  {'R', 'C', 'R', 'E', 'G'};                            // MFRC register write
uint8_t str_RCRDREG[]  =  {'R', 'C', 'R', 'D', 'R', 'E', 'G'};                  // MFRC register read
uint8_t str_HM[]       =  {'H', 'O', 'W', 'M', 'U', 'C', 'H'};
uint8_t str_S0[]       =  {'S', '0'};
uint8_t str_S1[]       =  {'S', '1'};

uint8_t str_IA[]       =  {'I', 'A'};                                           // is activated?
uint8_t str_DA[]       =  {'D', 'A'};                                           // do activate
uint8_t str_DISA[]     =  {'D', 'I', 'S', 'A'};                                 // disactivate

uint8_t str_EVDONE[]   =  {'E', 'V', 'D', 'O', 'N', 'E'};                       // read done

uint8_t str_GTIME[]    =  {'G', 'T', 'I', 'M', 'E'};                            // get time
uint8_t str_STIME[]    =  {'S', 'T', 'I', 'M', 'E'};                            // set time

uint8_t str_GB[]       =  {'G', 'B'};
uint8_t str_GCARD[]    =  {'G', 'C', 'A', 'R', 'D'};
uint8_t str_GALARM[]   =  {'G', 'A', 'L', 'A', 'R', 'M'};
uint8_t str_CLRBREAK[] =  {'C', 'L', 'R', 'B', 'R', 'E', 'A', 'K'};
uint8_t str_RMBREAK[]  =  {'R', 'M', 'B', 'R', 'E', 'A', 'K'};
uint8_t str_RSER[]     =  {'R', 'S', 'E', 'R'};                                 // read serial
uint8_t str_WSER[]     =  {'W', 'S', 'E', 'R'};                                 // write serial
uint8_t str_WSEC[]     =  {'W', 'S', 'E', 'C'};                                 // write secret
uint8_t str_RFILE[]    =  {'R', 'F', 'I', 'L', 'E'};
uint8_t str_BB[]       =  {'B', 'B'};                                           // bye-bye (disconnect)
uint8_t str_GV[]       =  {'G', 'V'};                                           // get dongle fw version
uint8_t str_GT[]       =  {'G', 'T'};                                           // get dongle type
uint8_t str_CE[]       =  {'C', 'E'};                                           // is power coil enabled?

#endif