/*
 * main.c
 *
 * UDP Server - Template for Computer Networks assignment
 *
 * This file contains the boilerplate code for a UDP server
 * portable across Windows, Linux, and macOS.
 */

#if defined WIN32
#include <winsock.h>
#else
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#define closesocket close
#endif

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include "protocol.h"

#define NO_ERROR 0

void clearwinsock() {
#if defined WIN32
	WSACleanup();
#endif
}

void errorhandler(const char *errorMessage) {
    fprintf(stderr, "%s", errorMessage);
}

static float frand(float a, float b) {
    return a + ((float) rand() / RAND_MAX) * (b - a);
}

float get_temperature()
{ return frand(-10.0f, 40.0f); }
float get_humidity()
{ return frand( 20.0f,100.0f); }
float get_wind()
{ return frand(  0.0f,100.0f); }
float get_pressure()
{ return frand(950.0f,1050.0f); }

/* Lista città supportate*/
int is_valid_city(const char* c) {
    const char* list[] = {
        "bari","roma","milano","napoli","torino",
        "palermo","genova","bologna","firenze","venezia","reggio calabria"
    };

    char lower[CITY_MAX];
    strncpy(lower, c, CITY_MAX);
    lower[CITY_MAX - 1] = '\0';

    for (char *p = lower; *p; ++p)
        *p = (char)tolower((unsigned char)*p);

    for (int i = 0; i < 11; i++) {
        if (strcmp(lower, list[i]) == 0)
            return 1; // città trovata valida
    }

    return 0; // città non trovata
}

int is_valid_city_syntax(const char *c) {
    for (const char *p = c; *p; ++p) {
        unsigned char ch = (unsigned char)*p;

        if (ch == '\t') {
            return 0;  // tab vietato
        }

        if (ch == ' ' || ch == '\'') {
            continue;  // OK
        }

        if (!isalpha(ch)) {
            return 0;  // caratteri speciali vietati
        }
    }
    return 1;
}

int valid_type(char t) {
    return (t == TYPE_TEMP || t == TYPE_HUM || t == TYPE_WIND || t == TYPE_PRESS);
}


/* --- parsing porta da linea di comando --- */
int parse_port(int argc, char *argv[], int *port) {

    if (argc == 1) return 1;  /* uso porta di default */

    if (argc != 3) return 0;

    if (strcmp(argv[1], "-p") != 0) return 0;

    if (argv[2][0] == '-') return 0;

    int p = atoi(argv[2]);
    if (p <= 0 || p > 65535) return 0;

    *port = p;
    return 1;
}

void deserialize_request(const uint8_t buffer[REQ_BUFFER_SIZE], weather_request_t *req) {
    size_t offset = 0;

    req->type = (char)buffer[offset];
    offset += sizeof(char);

    memcpy(req->city, &buffer[offset], CITY_MAX);
    req->city[CITY_MAX - 1] = '\0'; /* sicurezza */
    offset += CITY_MAX;
}

void serialize_response(const weather_response_t *resp, uint8_t buffer[RESP_BUFFER_SIZE]) {
    size_t offset = 0;

    /* status */
    uint32_t net_status = htonl((uint32_t)resp->status);
    memcpy(&buffer[offset], &net_status, sizeof(net_status));
    offset += sizeof(net_status);

    /* type */
    memcpy(buffer + offset, &resp->type, sizeof(char));
    offset += sizeof(char);

    /* value (float) */
    uint32_t bits;
    memcpy(&bits, &resp->value, sizeof(bits));
    uint32_t net_bits = htonl(bits);
    memcpy(&buffer[offset], &net_bits, sizeof(net_bits));
    offset += sizeof(net_bits);
}

int main(int argc, char *argv[]) {


#if defined WIN32
	// Initialize Winsock
	WSADATA wsa_data;
	int result = WSAStartup(MAKEWORD(2,2), &wsa_data);
	if (result != NO_ERROR) {
		printf("Error at WSAStartup()\n");
		return 0;
	}
#endif

    srand((unsigned)time(NULL));

    int port = SERVER_PORT;

    if (!parse_port(argc, argv, &port)) {
        printf("Uso corretto: %s [-p porta]\n", argv[0]);
        clearwinsock();
        return EXIT_FAILURE;
    }

    /* CREAZIONE SOCKET UDP */
    int my_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (my_socket < 0) {
        errorhandler("socket() failed\n");
        clearwinsock();
        return -1;
    }

    /* INDIRIZZO SERVER */
    struct sockaddr_in echoServAddr;
    memset(&echoServAddr, 0, sizeof(echoServAddr));
     echoServAddr.sin_family = AF_INET;
     echoServAddr.sin_port = htons(port);
     echoServAddr.sin_addr.s_addr = INADDR_ANY;

     /* BIND */
     if (bind(my_socket, (struct sockaddr*)&echoServAddr, sizeof(echoServAddr)) < 0) {
             errorhandler("bind() failed\n");
             closesocket(my_socket);
             clearwinsock();
             return EXIT_FAILURE;
         }

         printf("Server meteo UDP in ascolto sulla porta %d...\n", port);
	
         /* 4) LOOP PRINCIPALE (stateless) */
            while (1) {
                struct sockaddr_in client_addr;
        #if defined WIN32
                int client_len = sizeof(client_addr);
        #else
                socklen_t client_len = sizeof(client_addr);
        #endif

                uint8_t buffer_req[REQ_BUFFER_SIZE];
                int recvMsgSize = recvfrom(my_socket, (char*)buffer_req, REQ_BUFFER_SIZE, 0,(struct sockaddr*)&client_addr, &client_len);
                if (recvMsgSize < 0) {
                    errorhandler("recvfrom() failed\n");
                    /* continuiamo a servire altri client */
                    continue;
                }

                if (recvMsgSize != REQ_BUFFER_SIZE) {
                    fprintf(stderr, "Richiesta di dimensione non valida (%d byte)\n", recvMsgSize);
                    continue;
                }

                weather_request_t req;
                memset(&req, 0, sizeof(req));
                deserialize_request(buffer_req, &req);

                /* --- DNS reverse per log --- */
                struct in_addr addr = client_addr.sin_addr;
                char *ip_str = inet_ntoa(addr);

                struct hostent *host = gethostbyaddr((char*)&addr, 4, AF_INET);
                const char *client_name = (host != NULL) ? host->h_name : ip_str;

                printf("Richiesta ricevuta da %s (ip %s): type='%c', city='%s'\n",client_name, ip_str, req.type, req.city);

                /* Prepara risposta */
                weather_response_t resp;
                resp.status = STATUS_OK;
                resp.type   = req.type;
                resp.value  = 0.0f;

                /* VALIDAZIONE TIPO */
                if (!valid_type(req.type)) {
                    resp.status = STATUS_BAD_REQUEST;
                    resp.type   = '\0';
                    resp.value  = 0.0f;
                }
                /* VALIDAZIONE SINTATTICA CITY (tab, caratteri speciali) */
                else if (!is_valid_city_syntax(req.city)) {
                            resp.status = STATUS_BAD_REQUEST;
                            resp.type   = '\0';
                            resp.value  = 0.0f;
                }
                /* VALIDAZIONE LISTA CITY */
                else if (!is_valid_city(req.city)) {
                    resp.status = STATUS_CITY_UNKNOWN;
                    resp.type   = '\0';
                    resp.value  = 0.0f;
                }
                else {
                    /* TUTTO OK: genera valore meteo */
                    switch (req.type) {
                        case TYPE_TEMP:
                            resp.value = get_temperature();
                            break;
                        case TYPE_HUM:
                            resp.value = get_humidity();
                            break;
                        case TYPE_WIND:
                            resp.value = get_wind();
                            break;
                        case TYPE_PRESS:
                            resp.value = get_pressure();
                            break;
                        default:
                            resp.status = STATUS_BAD_REQUEST;
                            resp.type   = '\0';
                            resp.value  = 0.0f;
                            break;
                    }
                }

                /* SERIALIZZA E INVIA RISPOSTA */
                uint8_t buffer_resp[RESP_BUFFER_SIZE];
                serialize_response(&resp, buffer_resp);

                if (sendto(my_socket, (const char*)buffer_resp, RESP_BUFFER_SIZE, 0,(struct sockaddr*)&client_addr, client_len) != RESP_BUFFER_SIZE) {
                    errorhandler("sendto() failed (byte inviati diversi dal previsto)\n");
                    return -1;
                }
            }


	printf("Server terminated.\n");

	closesocket(my_socket);
	clearwinsock();
	return 0;
} // main end
