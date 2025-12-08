/*
 * main.c
 *
 * UDP Client - Template for Computer Networks assignment
 *
 * This file contains the boilerplate code for a UDP client
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
#include "protocol.h"

#define NO_ERROR 0

void clearwinsock() {
#if defined WIN32
	WSACleanup();
#endif
}

void errorhandler(const char *errorMessage){
    fprintf(stderr, "%s", errorMessage);
}

void print_usage(const char *progname) {
    printf("Uso corretto: %s [-s server] [-p port] -r \"type city\"\n", progname);
}

/* Trasforma una stringa in "Title Case" parola per parola */
void maiuscola(char *s) {
    int start = 1;
    for (int i = 0; s[i]; i++) {
        if (isspace((unsigned char)s[i])) {
            start = 1;
        } else {
            s[i] = start ? (char)toupper((unsigned char)s[i])
                         : (char)tolower((unsigned char)s[i]);
            start = 0;
        }
    }
}

/* --- SERIALIZZAZIONE / DESERIALIZZAZIONE --- */

void serialize_request(const weather_request_t *req, uint8_t buffer[REQ_BUFFER_SIZE]) {
    size_t offset = 0;

    buffer[offset] = (uint8_t)req->type;
    offset += sizeof(char);

    /* Copiamo sempre CITY_MAX byte (eventuale terminatore incluso) */
    memcpy(&buffer[offset], req->city, CITY_MAX);
    offset += CITY_MAX;
}

void deserialize_response(const uint8_t buffer[RESP_BUFFER_SIZE], weather_response_t *resp) {
    size_t offset = 0;

    /* status */
    uint32_t net_status;
    memcpy(&net_status, &buffer[offset], sizeof(net_status));
    resp->status = (unsigned int)ntohl(net_status);
    offset += sizeof(net_status);

    /* type */
    resp->type = (char)buffer[offset];
    offset += sizeof(char);

    /* value (float) */
    uint32_t net_bits;
    memcpy(&net_bits, &buffer[offset], sizeof(net_bits));
    uint32_t bits = ntohl(net_bits);
    memcpy(&resp->value, &bits, sizeof(resp->value));
    offset += sizeof(net_bits);
}

/* --- RISOLUZIONE DNS: gethostbyname / gethostbyaddr --- */
/* host_name può essere hostname (es. "localhost") o IP (es. "127.0.0.1") */
int resolve_server(const char *host_name,struct in_addr *out_addr,char *resolved_name, size_t resolved_name_len, char *resolved_ip,   size_t resolved_ip_len)
{
    struct hostent *remoteHost;
    struct in_addr addr;

    /* ============================================
       1) SE INIZIA CON LETTERA → È UN HOSTNAME
       ============================================ */
    if (isalpha((unsigned char)host_name[0])) {

        remoteHost = gethostbyname(host_name);

        if (remoteHost == NULL) {
            fprintf(stderr, "gethostbyname() failed for %s\n", host_name);
            return 0;
        }

        /* Ricava l’indirizzo IP dal risultato */
        struct in_addr *ina = (struct in_addr*)remoteHost->h_addr_list[0];
        *out_addr = *ina;

        /* IP stringa */
        strncpy(resolved_ip, inet_ntoa(*ina), resolved_ip_len - 1);
        resolved_ip[resolved_ip_len - 1] = '\0';

        /* Nome canonico */
        strncpy(resolved_name, remoteHost->h_name, resolved_name_len - 1);
        resolved_name[resolved_name_len - 1] = '\0';

        return 1;
    }

    /* ============================================
       2) ALTRIMENTI → PROVO A TRATTARLO COME IP
       ============================================ */
    addr.s_addr = inet_addr(host_name);

    if (addr.s_addr == INADDR_NONE) {
        fprintf(stderr, "Indirizzo IP non valido: %s\n", host_name);
        return 0;
    }

    /* Reverse DNS */
    remoteHost = gethostbyaddr((char*)&addr, 4, AF_INET);

    if (remoteHost != NULL) {
        strncpy(resolved_name, remoteHost->h_name, resolved_name_len - 1);
        resolved_name[resolved_name_len - 1] = '\0';
    } else {
        /* Se reverse DNS fallisce → usa l’IP come nome */
        strncpy(resolved_name, host_name, resolved_name_len - 1);
        resolved_name[resolved_name_len - 1] = '\0';
    }

    /* IP stringa = quello passato */
    strncpy(resolved_ip, host_name, resolved_ip_len - 1);
    resolved_ip[resolved_ip_len - 1] = '\0';

    *out_addr = addr;

    return 1;
}


/* PARSING ARGOMENTI */

int parse(int argc, char *argv[], char *server_ip, int *port, char *type, char *city)
{
    int found_r = 0;

    for (int i = 1; i < argc; i++) {

        if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) return 0;
            strncpy(server_ip, argv[i + 1], 63);
            server_ip[63] = '\0';
            i++;
            continue;
        }

        if (strcmp(argv[i], "-p") == 0) {
            if (i + 1 >= argc) return 0;
            *port = atoi(argv[i + 1]);
            if (*port <= 0 || *port > 65535) return 0;
            i++;
            continue;
        }

        if (strcmp(argv[i], "-r") == 0) {
            if (i + 1 >= argc) return 0;

            /* -r deve essere l'ultimo parametro */
            if (i + 2 != argc) return 0;

            char *req_str = argv[i + 1];

            /* niente tabulazioni nella richiesta */
            if (strchr(req_str, '\t') != NULL) {
                fprintf(stderr, "Errore: la richiesta non può contenere tabulazioni.\n");
                return 0;
            }

            /* salta spazi iniziali */
            char *p = req_str;
            while (*p == ' ') p++;

            if (*p == '\0') {
                fprintf(stderr, "Errore: richiesta vuota.\n");
                return 0;
            }

            /* trova primo spazio dopo il token tipo */
            char *space = strchr(p, ' ');
            if (space == NULL) {
                fprintf(stderr, "Errore: formato richiesta non valido (manca la città).\n");
                return 0;
            }

            /* il primo token (type) deve essere lungo 1 */
            if (space - p != 1) {
                fprintf(stderr, "Errore: il primo token deve essere un singolo carattere.\n");
                return 0;
            }

            *type = p[0];

            /* city = tutto ciò che segue, saltando spazi multipli */
            char *city_start = space + 1;
            while (*city_start == ' ') city_start++;

            if (*city_start == '\0') {
                fprintf(stderr, "Errore: nome città mancante.\n");
                return 0;
            }

            /* controllo lunghezza città (max CITY_MAX-1) */
            if (strlen(city_start) >= CITY_MAX) {
                fprintf(stderr, "Errore: nome città troppo lungo (massimo %d caratteri).\n",
                        CITY_MAX - 1);
                return 0;
            }

            strncpy(city, city_start, CITY_MAX);
            city[CITY_MAX - 1] = '\0';

            found_r = 1;
            break;
        }

        /* opzione sconosciuta */
        return 0;
    }

    return found_r;
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

    char server_name[64];
    int port;
    char type = 0;
    char city[CITY_MAX];

    strncpy(server_name, DEFAULT_HOST , 63);
    server_name[63] = '\0';
    port = SERVER_PORT;        // 56700 di default

    if (!parse(argc, argv, server_name, &port, &type, city)) {
        print_usage(argv[0]);
        clearwinsock();
        return EXIT_FAILURE;
    }

    /* Risoluzione DNS del server */
    struct in_addr server_addr_in;
    char server_canonical_name[256];
    char server_ip_str[64];

    if (!resolve_server(server_name,&server_addr_in,server_canonical_name, sizeof(server_canonical_name), server_ip_str, sizeof(server_ip_str))) {
        clearwinsock();
        return EXIT_FAILURE;
    }

    /* CREAZIONE SOCKET */
    int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        errorhandler("Creazione della socket fallita.\n");
        clearwinsock();
        return EXIT_FAILURE;
    }

    struct sockaddr_in sad;
    memset(&sad, 0, sizeof(sad));
    sad.sin_family = AF_INET;
    sad.sin_port   = htons(port);
    sad.sin_addr   = server_addr_in;

    /* RICHIESTA */
    weather_request_t req;
    memset(&req, 0, sizeof(req));
    req.type = type;
    strncpy(req.city, city, CITY_MAX - 1);
    req.city[CITY_MAX - 1] = '\0';

    uint8_t buffer_req[REQ_BUFFER_SIZE];
    serialize_request(&req, buffer_req);

    /* Invia richiesta al server */
    if (sendto(sock, (const char*)buffer_req, REQ_BUFFER_SIZE, 0, (struct sockaddr*)&sad, sizeof(sad)) != REQ_BUFFER_SIZE) {
        errorhandler("sendto() fallita.\n");
        closesocket(sock);
        clearwinsock();
        return EXIT_FAILURE;
    }

    /* RISPOSTA SERVER */
    struct sockaddr_in fromAddr;
#if defined WIN32
    int fromSize = sizeof(fromAddr);
#else
    socklen_t fromSize = sizeof(fromAddr);
#endif

    uint8_t buffer_resp[RESP_BUFFER_SIZE];
    int respLen = recvfrom(sock, (char*)buffer_resp, RESP_BUFFER_SIZE, 0, (struct sockaddr*)&fromAddr, &fromSize);

    if (respLen < 0) {
        errorhandler("recvfrom() fallita.\n");
        closesocket(sock);
        clearwinsock();
        return EXIT_FAILURE;
    }

    if (respLen != RESP_BUFFER_SIZE) {
        fprintf(stderr, "Errore: dimensione risposta non valida (%d byte).\n", respLen);
        closesocket(sock);
        clearwinsock();
        return EXIT_FAILURE;
    }

    /* Opzionale: verifica che la risposta arrivi dallo stesso IP */
    if (fromAddr.sin_addr.s_addr != sad.sin_addr.s_addr) {
        fprintf(stderr, "Errore: ricevuto pacchetto da sorgente sconosciuta.\n");
        closesocket(sock);
        clearwinsock();
        return EXIT_FAILURE;
    }

    weather_response_t resp;
    deserialize_response(buffer_resp, &resp);

    /* Controllo status valido */
    if (resp.status != STATUS_OK &&
        resp.status != STATUS_CITY_UNKNOWN &&
        resp.status != STATUS_BAD_REQUEST) {
        printf("Errore: risposta non valida dal server.\n");
        closesocket(sock);
        clearwinsock();
        return EXIT_FAILURE;
    }

    /* Formatta città */
    maiuscola(city);

    /* COSTRUZIONE MESSAGGIO */
    printf("Ricevuto risultato dal server %s (ip %s). ",server_canonical_name, server_ip_str);

    if (resp.status == STATUS_OK) {
        switch (resp.type) {
            case TYPE_TEMP:
                printf("%s: Temperatura = %.1f°C\n", city, resp.value);
                break;
            case TYPE_HUM:
                printf("%s: Umidita' = %.1f%%\n", city, resp.value);
                break;
            case TYPE_WIND:
                printf("%s: Vento = %.1f km/h\n", city, resp.value);
                break;
            case TYPE_PRESS:
                printf("%s: Pressione = %.1f hPa\n", city, resp.value);
                break;
            default:
                printf("Richiesta non valida\n");
                break;
        }
    } else if (resp.status == STATUS_CITY_UNKNOWN) {
        printf("Città non disponibile\n");
    } else {
        printf("Richiesta non valida\n");
    }


    /* CHIUSURA CLIENT */
	printf("Client terminated.\n");
	closesocket(sock);
	clearwinsock();
	return 0;
}
