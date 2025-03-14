#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>

#define size 2048
extern int errno;

int port;

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("Trebuie sa folositi sintaxa: %s <adresa_server> <port>\n", argv[0]);
        return -1;
    }

    int socket_client;
    struct sockaddr_in server;

    char raspuns[size];
    char comanda[size];
    char buffer[size];

    port = atoi(argv[2]);

    if ((socket_client = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("[CLIENT]Eroare la socket().\n");
        return errno;
    }

    bzero(&server, sizeof(server));

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(argv[1]);
    server.sin_port = htons(port);

    if (connect(socket_client, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("[CLIENT]Eroare la connect().\n");
        return errno;
    }

    while (1)
    {
        memset(comanda, 0, sizeof(comanda));
        memset(raspuns, 0, sizeof(raspuns));

        printf("Introduceti comanda: ");
        fflush(stdout);
        
        read(0, comanda, sizeof(comanda));
        strtok(comanda, "\n");

        if (-1 == write(socket_client, comanda, strlen(comanda) + 1)) 
        {
            perror("[CLIENT]Eroare la write() spre server");
            return errno;
        }

        if (strcmp(comanda, "quit") == 0)
        {
            break;
        }
        
        if (-1 == read(socket_client, raspuns, sizeof(raspuns)))
        {
            perror("[CLIENT]Eroare la read() de la server");
            return errno;
        }
        else //daca raspunsul este valid
        {
            //printf("raspuns de la sv: %s", raspuns);
            if(strstr(raspuns, "sirLung")) // daca este lung
            {
                printf("Raspuns de la server: ");
                //printf("sir lung -- %s\n", raspuns);
                while(strcmp(raspuns, "END")) // ppana la final
                {
                    memset(raspuns, 0, sizeof(raspuns));
                    if (-1 == read(socket_client, raspuns, sizeof(raspuns)))
                    {
                        perror("[CLIENT]Eroare la read() de la server la sir lung");
                        return errno;
                    }
                    if (strcmp(raspuns, "END") == 0)
                        break;
                    printf("%s\n", raspuns);
                    fflush(stdout);
                }
                printf("\n");
                fflush(stdout);
            }
            else //daca este normal
            {
                //printf("sir normal\n");
                printf("Raspuns de la server: %s\n\n", raspuns);
            }
        }
    }
    close(socket_client);
    return 0;
}