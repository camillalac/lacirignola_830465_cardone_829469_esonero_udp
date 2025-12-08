/*
 * protocol.h
 *
 * Shared header file for UDP client and server
 * Contains protocol definitions, data structures, constants and function prototypes
 */

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include <stdint.h>

/*
 * ============================================================================
 * PROTOCOL CONSTANTS
 * ============================================================================
 */

#define SERVER_PORT 56700
#define DEFAULT_HOST "localhost"
#define CITY_MAX 64


#define TYPE_TEMP  't'
#define TYPE_HUM   'h'
#define TYPE_WIND  'w'
#define TYPE_PRESS 'p'


#define STATUS_OK            0
#define STATUS_CITY_UNKNOWN  1
#define STATUS_BAD_REQUEST   2


#define REQ_BUFFER_SIZE (1 + CITY_MAX)
#define RESP_BUFFER_SIZE (sizeof(uint32_t) + sizeof(char) + sizeof(float))

/*
 * ============================================================================
 * PROTOCOL DATA STRUCTURES
 * ============================================================================
 */

//Richiesta client -> server
typedef struct {
    char type;                   // 't','h','w','p'
    char city[CITY_MAX];         // stringa città null-terminated
} weather_request_t;

//Risposta server -> client
typedef struct {
    unsigned int status;         // 0,1,2
    char type;                   // eco del tipo richiesto
    float value;                 // valore meteo
} weather_response_t;

/*
 * ============================================================================
 * FUNCTION PROTOTYPES
 * ============================================================================
 */

float get_temperature(void);
float get_humidity(void);
float get_wind(void);
float get_pressure(void);

#endif /* PROTOCOL_H_ */
