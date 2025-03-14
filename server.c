#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <dirent.h>
#include <stdint.h>

#define MAX_LINE_L 1024
#define MAX_PATH_L 512
#define MAX_STATIONS 80
#define MAX_TOTAL_STATIONS 2000
#define MAX_TRAINS 1500
#define MAX_TRAINS_AUX 300
#define MAX_EXCEPTIONS 100
#define PORT 2024

extern int errno;

static void *treat(void *); /* functia executata de fiecare thread ce realizeaza comunicarea cu clientii */
//void raspunde(void *);

typedef struct {
    pthread_t idThread; //id-ul thread-ului
    int thCount; //nr de conexiuni servite
}Thread;

Thread *threadsPool; //un array de structuri Thread

int sd; //descriptorul de socket de ascultare
int nthreads;//numarul de threaduri
pthread_mutex_t mlock=PTHREAD_MUTEX_INITIALIZER;              // variabila mutex ce va fi partajata de threaduri
pthread_mutex_t mxml=PTHREAD_MUTEX_INITIALIZER;              // variabila mutex ce va fi partajata de threaduri

typedef struct {
    int socket; 
    int logged_in;
    int thread; 
    char username[50]; 
} ClientState;

typedef struct {
    char dataStart[11];
    char dataFinal[11];
} ExceptionInfo;

typedef struct {
    char nume[50];
    char oraSosire[10];
    char oraPlecare[10];
    int intarziere;
} Station;

typedef struct {
    char id[16];
    char statiePlecare[50];
    char statieSosire[50];
    char oraPlecare[10];
    char oraSosire[10];
    int intarzierePlecare;
    int intarziereSosire;
    Station statii[MAX_STATIONS];
    int nrStatii;
    ExceptionInfo exceptii[MAX_EXCEPTIONS];
    int nrExceptii;
    int circulaAzi;
} TrainInfo;

// pentru graf

Station statiiTotale[MAX_TOTAL_STATIONS];

typedef struct {
    int destinatie;
    int trenIndex;
} Arc;

typedef struct {
    Arc adiacenta[MAX_TOTAL_STATIONS][MAX_TOTAL_STATIONS];
    int nrArce[MAX_TOTAL_STATIONS]; 
} Graf;

TrainInfo trains[MAX_TRAINS];
TrainInfo trainsCrt[MAX_TRAINS_AUX];
ExceptionInfo exceptions[MAX_TRAINS];
ClientState *clients;
int trainCount = 0;
int trainCountAux = 0;
int exceptionCount = 0;
int nrTotalStatii = 0;
int vizitat[1500];
const char *trainsFile = "Trains/Trenuri.xml";
const char *exceptiiFile = "Trains/Exceptii.xml";

void get_current_date(char *buffer, size_t size) 
{
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    snprintf(buffer, size, "%02d.%02d.%04d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
}

int get_current_time()
{
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    int seconds_since_midnight = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;

    return seconds_since_midnight;
}

int dateToInt(const char *data)
{
    int zi, luna, an;
    sscanf(data, "%2d.%2d.%4d", &zi, &luna, &an);

    return an * 10000 + luna * 100 + zi; 
}

int oraToInt(const char *ora) 
{
    int ore, minute;

    if (sscanf(ora, "%d:%d", &ore, &minute) != 2) 
    {
        printf("Format invalid!\n");
        return -1;
    }

    return ore * 3600 + minute * 60;
}

void convertToTime(char *timeStr) 
{
    int seconds = atoi(timeStr);
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;

    sprintf(timeStr, "%02d:%02d", hours, minutes);
}

int isLogged(int cl)
{
    for(int i = 0; i < nthreads; i++)
        if(clients[i].socket == cl && clients[i].logged_in)
            return 1;

    return 0;
}

void addStation(Station statieToAdd)
{
    for(int i = 0; i < nrTotalStatii; i++)
    {
        if (strcmp(statieToAdd.nume, statiiTotale[i].nume) == 0)
        {
            return;
        }
    }

    //printf("statie adaugata: %s count : %d\n ", statieToAdd.nume, nrTotalStatii + 1);
    statiiTotale[nrTotalStatii] = statieToAdd;
    nrTotalStatii++;
}

int readXML(const char *filePath) 
{
    pthread_mutex_lock(&mxml);
    FILE *file = fopen(filePath, "r");
    if (!file) 
    {
        pthread_mutex_unlock(&mxml);
        perror("Error opening file");
        return -1;
    }

    char line[MAX_LINE_L];
    TrainInfo currentTrain;
    memset(&currentTrain, 0, sizeof(TrainInfo));
    int inTren = 0, inStatie = 0, inExceptie = 0;

    while (fgets(line, sizeof(line), file)) 
    {
        if (strstr(line, "<Tren>")) 
        {
            memset(&currentTrain, 0, sizeof(TrainInfo));
            inTren = 1;
            currentTrain.nrStatii = 0;
        } 
        else if (strstr(line, "</Tren>")) 
        {
            Station statieStart;
            statieStart.intarziere = currentTrain.intarzierePlecare;
            strcpy(statieStart.nume, currentTrain.statiePlecare);
            strcpy(statieStart.oraPlecare, currentTrain.oraPlecare);
            
            Station statieFinal;
            statieFinal.intarziere = currentTrain.intarziereSosire;
            strcpy(statieFinal.nume, currentTrain.statieSosire);
            strcpy(statieStart.oraSosire, currentTrain.oraSosire);

            addStation(statieStart);
            addStation(statieFinal);

            trains[trainCount] = currentTrain;
            trainCount++;
            inTren = 0;
        } 
        else if (strstr(line, "<ID>")) 
        {
            sscanf(line, " <ID>%15[^<]", currentTrain.id);
        } 
        else if (strstr(line, "<StatiePlecare>")) 
        {
            sscanf(line, " <StatiePlecare>%49[^<]", currentTrain.statiePlecare);
        } 
        else if (strstr(line, "<StatieSosire>")) 
        {
            sscanf(line, " <StatieSosire>%49[^<]", currentTrain.statieSosire);
        }
        else if (!inStatie && strstr(line, "<OraPlecare>")) 
        {
            sscanf(line, " <OraPlecare>%9[^<]", currentTrain.oraPlecare);
        } 
        else if (!inStatie && strstr(line, "<OraSosire>")) 
        {
            sscanf(line, " <OraSosire>%9[^<]", currentTrain.oraSosire);
        } 
        else if (!inStatie && strstr(line, "<IntarzierePlecare>"))
        {
            sscanf(line, " <IntarzierePlecare>%d", &currentTrain.intarzierePlecare);
        }
        else if (!inStatie && strstr(line, "<IntarziereSosire>"))
        {
            sscanf(line, " <IntarziereSosire>%d", &currentTrain.intarziereSosire);
        }
        else if (strstr(line, "<Statie>")) 
        {
            inStatie = 1;
        } 
        else if (strstr(line, "</Statie>")) 
        {
            inStatie = 0;
            addStation(currentTrain.statii[currentTrain.nrStatii]);
            currentTrain.nrStatii++;
        } 
        else if (inStatie && strstr(line, "<Nume>")) 
        {
            sscanf(line, " <Nume>%49[^<]", currentTrain.statii[currentTrain.nrStatii].nume);
        } 
        else if (inStatie && strstr(line, "<OraSosire>")) 
        {
            sscanf(line, " <OraSosire>%9[^<]", currentTrain.statii[currentTrain.nrStatii].oraSosire);
        }
        else if (inStatie && strstr(line, "<OraPlecare>")) 
        {
            sscanf(line, " <OraPlecare>%9[^<]", currentTrain.statii[currentTrain.nrStatii].oraPlecare);
        }
        else if (inStatie && strstr(line, "<Intarziere>")) 
        {
            sscanf(line, " <Intarziere>%d", &currentTrain.statii[currentTrain.nrStatii].intarziere);
        }
        else if (strstr(line, "<Exceptie>"))
        {
            inExceptie = 1;
        }
        else if (strstr(line, "</Exceptie>"))
        {
            inExceptie = 0;
            currentTrain.nrExceptii++; 
            exceptionCount++;
        }
        else if (inExceptie && strstr(line, "<DataInceput>")) 
        {
            sscanf(line, " <DataInceput>%11[^<]", currentTrain.exceptii[currentTrain.nrExceptii].dataStart);
        }
        else if (inExceptie && strstr(line, "<DataSfarsit>")) 
        {
            sscanf(line, " <DataSfarsit>%11[^<]", currentTrain.exceptii[currentTrain.nrExceptii].dataFinal);
        }
    }
    fclose(file);
    pthread_mutex_unlock(&mxml);
    return 0;
}

int saveXML(const char *filePath) 
{
    pthread_mutex_lock(&mxml);
    FILE *file = fopen(filePath, "w");
    if (!file) 
    {
        pthread_mutex_unlock(&mxml);
        perror("Error opening file for writing");
        return -1;
    }

    fprintf(file, "<Trenuri>\n");
    for (int i = 0; i < trainCount; i++) 
    {
        fprintf(file, "    <Tren>\n");
        fprintf(file, "        <ID>%s</ID>\n", trains[i].id);
        fprintf(file, "        <StatiePlecare>%s</StatiePlecare>\n", trains[i].statiePlecare);
        fprintf(file, "        <StatieSosire>%s</StatieSosire>\n", trains[i].statieSosire);
        fprintf(file, "        <OraPlecare>%s</OraPlecare>\n", trains[i].oraPlecare);
        fprintf(file, "        <OraSosire>%s</OraSosire>\n", trains[i].oraSosire);
        fprintf(file, "        <IntarzierePlecare>%d</IntarzierePlecare>\n", trains[i].intarzierePlecare);
        fprintf(file, "        <IntarziereSosire>%d</IntarziereSosire>\n", trains[i].intarziereSosire);

        if (trains[i].nrStatii > 0) 
        {
            fprintf(file, "        <Statii>\n");
            for (int j = 0; j < trains[i].nrStatii; j++) 
            {
                fprintf(file, "            <Statie>\n");
                fprintf(file, "                <Nume>%s</Nume>\n", trains[i].statii[j].nume);
                fprintf(file, "                <OraSosire>%s</OraSosire>\n", trains[i].statii[j].oraSosire);
                fprintf(file, "                <OraPlecare>%s</OraPlecare>\n", trains[i].statii[j].oraPlecare);
                fprintf(file, "                <Intarziere>%d</Intarziere>\n", trains[i].statii[j].intarziere);
                fprintf(file, "            </Statie>\n");
            }
            fprintf(file, "        </Statii>\n");
        }

        if(trains[i].nrExceptii > 0)
        {
            fprintf(file, "        <Exceptii>\n");
            for(int j = 0; j < trains[i].nrExceptii; j++)
            {
                fprintf(file, "            <Exceptie>\n");
                fprintf(file, "                <DataInceput>%s</DataInceput>\n", trains[i].exceptii[j].dataStart);
                fprintf(file, "                <DataSfarsit>%s</DataSfarsit>\n", trains[i].exceptii[j].dataFinal);
                fprintf(file, "            </Exceptie>\n");
            }
            fprintf(file, "        </Exceptii>\n");
        }

        fprintf(file, "    </Tren>\n");
    }
    fprintf(file, "</Trenuri>\n");
    fprintf(file, "am printat");
    fclose(file);
    pthread_mutex_unlock(&mxml);

    return 0;
}

void loadExceptii()
{
    char data[15]; get_current_date(data, sizeof(data));
    for(int i = 0; i < trainCount; i++)
    {
        trains[i].circulaAzi = 0;
        if(trains[i].nrExceptii > 0)
        {
            for(int j = 0; j < trains[i].nrExceptii; j++)
            {
                if(dateToInt(data) >= dateToInt(trains[i].exceptii[j].dataStart) && 
                dateToInt(data) <= dateToInt(trains[i].exceptii[j].dataFinal))
                {
                    trains[i].circulaAzi = 1;
                }
            }
        }
        
        char year[5];
        strncpy(year, data + 6, 4);
        year[4] = '\0';
        int year_int = atoi(year);
        if(year_int < 2024 || year_int > 2025)
            trains[i].circulaAzi = -1;
    }
}

void verificareLogin(const char *userName, const char *password, char *raspuns, size_t raspunsSize, int cl)
{
    for(int i = 0; i < nthreads; i++)
    {
        if(clients[i].socket == cl && clients[i].logged_in == 1)
        {
            strcpy(raspuns, "Sunteti deja logat");
            return;
        }
    }

    FILE *usersFile = fopen("users.txt", "r");
    if(usersFile == NULL)
    {
        perror("err la deschiderea fisierului / nu exista!");
        exit(5);
    }
    
    char line[128];
    while (fgets(line, sizeof(line), usersFile)) {
        line[strcspn(line, "\n")] = 0;

        char *file_user = strtok(line, ":");
        char *file_parola = strtok(NULL, ":");

        if (file_user && file_parola) 
        {
            if (strcmp(userName, file_user) == 0 && strcmp(password, file_parola) == 0) 
            {
                fclose(usersFile);
                for (int i = 0; i < nthreads; i++) {
                    if (clients[i].socket == -1 || clients[i].socket == cl) 
                    {
                        clients[i].socket = cl;
                        clients[i].logged_in = 1;
                        strncpy(clients[i].username, userName, sizeof(clients[i].username) - 1);
                        clients[i].username[sizeof(clients[i].username) - 1] = '\0'; // Asigurare terminator
                        snprintf(raspuns, raspunsSize, "%s, bine ati venit!", userName);
                        break;
                    }
                    else if (clients[i].logged_in == 1 && strcmp(clients[i].username, userName) == 0)
                    {
                        strcpy(raspuns, "Un admin cu acelasi nume este deja logat.");
                        break;
                    }
                }
                return; 
            }
            else
            {
                strcpy(raspuns, "Username-ul sau parola incorecte.");
            }
        }
    }
    
    fclose(usersFile);
    return;
}

void logoutComanda(char *raspuns, size_t raspunsSize, int cl)
{
    for(int i = 0; i < nthreads; i++)
    {
        if(clients[i].socket == cl)
        {
            clients[i].socket = -1;
            clients[i].logged_in = 0;
            snprintf(raspuns, raspunsSize, "%s, ati fost delogat.", clients[i].username);
            memset(clients[i].username, 0, sizeof(clients[i].username));
            return;
        }
    }

    strcpy(raspuns, "Nu sunteti logat.");
    return;
}

void setAuxTrains(int type, const char *numeOras, const char *data)
{
    memset(trainsCrt, 0, sizeof(trainsCrt));
    trainCountAux = 0;
    for(int i = 0; i < trainCount; i++)
    {
        trains[i].circulaAzi = 0;
        if(trains[i].nrExceptii > 0)
        {
            for(int j = 0; j < trains[i].nrExceptii; j++)
            {
                if(dateToInt(data) >= dateToInt(trains[i].exceptii[j].dataStart) && 
                dateToInt(data) <= dateToInt(trains[i].exceptii[j].dataFinal))
                {
                    trains[i].circulaAzi = 1;
                }
            }
        }
        
        char year[5];
        strncpy(year, data + 6, 4);
        year[4] = '\0';
        int year_int = atoi(year);
        if(year_int < 2024 || year_int > 2025)
            trains[i].circulaAzi = -1;

        if(type == 1)
        {
            if (strcmp(trains[i].statiePlecare, numeOras) == 0) 
            {
                trainsCrt[trainCountAux] = trains[i];
                trainCountAux++;
            }
            else
            {
                for(int j = 0; j < trains[i].nrStatii; j++)
                {
                    if(strcmp(trains[i].statii[j].nume, numeOras) == 0)
                    {    
                        trainsCrt[trainCountAux] = trains[i];
                        trainCountAux++;
                    }
                }
            }
        }
        else if(type == 2)
        {
            if (strcmp(trains[i].statieSosire, numeOras) == 0) 
            {
                trainsCrt[trainCountAux] = trains[i];
                trainCountAux++;
            }
            else
            {
                for(int j = 0; j < trains[i].nrStatii; j++)
                {
                    if(strcmp(trains[i].statii[j].nume, numeOras) == 0)
                    {    
                        trainsCrt[trainCountAux] = trains[i];
                        trainCountAux++;
                    }
                }
            }
        }
    }
}

void orderTrains(int type, const char *numeOras)
{
    //printf("Lungimea lui trainsCrt este %d\n", trainCountAux);

    if(type == 1)
    {
        for(int i = 0; i < trainCountAux - 1; i++)
        {
            int a = -1;
            if(strcmp(trainsCrt[i].statiePlecare, numeOras) == 0)
            {
                a = oraToInt(trainsCrt[i].oraPlecare);
                //printf("a = to_int(%s)\n", trainsCrt[i].oraPlecare);
            }
            else
            {
                for(int k = 0; k < trainsCrt[i].nrStatii; k++)
                {
                    if(strcmp(trainsCrt[i].statii[k].nume, numeOras) == 0)
                    {
                        a = oraToInt(trainsCrt[i].statii[k].oraPlecare);
                        //printf("a = to_int(%s)\n", trainsCrt[i].statii[k].oraPlecare);
                        break;
                    }
                }
            }
            for(int j = i + 1; j < trainCountAux; j++)
            {
                int b = -1;
                if(strcmp(trainsCrt[j].statiePlecare, numeOras) == 0)
                {
                    b = oraToInt(trainsCrt[j].oraPlecare);
                }
                else
                {
                    for(int k = 0; k < trainsCrt[j].nrStatii; k++)
                    {
                        if(strcmp(trainsCrt[j].statii[k].nume, numeOras) == 0)
                        {
                            b = oraToInt(trainsCrt[j].statii[k].oraPlecare);
                            //printf("b = to_int(%s)\n", trainsCrt[j].statii[k].oraPlecare);

                            break;
                        }
                    }
                }
                if(a > b && a != -1 && b != -1)
                {
                    //printf("AM SCHIMBAT trains[%s] cu trains[%s]\n cu orele %d si %d\n", trainsCrt[i].id, trainsCrt[j].id, a, b);
                    TrainInfo auxTrain;
                    memset(&auxTrain, 0, sizeof(TrainInfo));
                    auxTrain = trainsCrt[i];
                    trainsCrt[i] = trainsCrt[j];
                    trainsCrt[j] = auxTrain;
                    int aux = a;
                    a = b;
                    b = aux;
                    //printf("s-a schimbat %s cu %s\n\n", trainsCrt[i].id, trainsCrt[j].id);
                }
            }
        }
    }
    else if(type == 2)
    {
        for(int i = 0; i < trainCountAux - 1; i++)
        {
            int a = -1;
            if(strcmp(trainsCrt[i].statieSosire, numeOras) == 0)
            {
                a = oraToInt(trainsCrt[i].oraSosire);
                //printf("a = to_int(%s)\n", trainsCrt[i].oraPlecare);
            }
            else
            {
                for(int k = 0; k < trainsCrt[i].nrStatii; k++)
                {
                    if(strcmp(trainsCrt[i].statii[k].nume, numeOras) == 0)
                    {
                        a = oraToInt(trainsCrt[i].statii[k].oraSosire);
                        //printf("a = to_int(%s)\n", trainsCrt[i].statii[k].oraPlecare);
                        break;
                    }
                }
            }
            for(int j = i + 1; j < trainCountAux; j++)
            {
                int b = -1;
                if(strcmp(trainsCrt[j].statieSosire, numeOras) == 0)
                {
                    b = oraToInt(trainsCrt[j].oraSosire);
                }
                else
                {
                    for(int k = 0; k < trainsCrt[j].nrStatii; k++)
                    {
                        if(strcmp(trainsCrt[j].statii[k].nume, numeOras) == 0)
                        {
                            b = oraToInt(trainsCrt[j].statii[k].oraSosire);
                            //printf("b = to_int(%s)\n", trainsCrt[j].statii[k].oraPlecare);

                            break;
                        }
                    }
                }
                if(a > b && a != -1 && b != -1)
                {
                    //printf("AM SCHIMBAT trains[%s] cu trains[%s]\n cu orele %d si %d\n", trainsCrt[i].id, trainsCrt[j].id, a, b);
                    TrainInfo auxTrain;
                    memset(&auxTrain, 0, sizeof(TrainInfo));
                    auxTrain = trainsCrt[i];
                    trainsCrt[i] = trainsCrt[j];
                    trainsCrt[j] = auxTrain;
                    int aux = a;
                    a = b;
                    b = aux;
                    //printf("s-a schimbat %s cu %s\n\n", trainsCrt[i].id, trainsCrt[j].id);
                }
            }
        }
    }
    else
    {
        printf("Eroare la order");
    }
}

void getOraSosirePlecare(int type, const char *numeOras, int indice, char *oraSosire)
{   
    if(type == 1)
    {
        if(strcmp(trainsCrt[indice].statiePlecare, numeOras) == 0)
        {
            strcpy(oraSosire, trainsCrt[indice].oraPlecare);
            return;
        }
        for(int j = 0; j < trainsCrt[indice].nrStatii; j++)
        {
            if(strcmp(trainsCrt[indice].statii[j].nume, numeOras) == 0)
            {
                strcpy(oraSosire, trainsCrt[indice].statii[j].oraPlecare);
                return;
            } 
        }
        strcpy(oraSosire, "eroareOraPle");
    }
    else if(type == 2)
    {
        if(strcmp(trainsCrt[indice].statieSosire, numeOras) == 0)
        {
            strcpy(oraSosire, trainsCrt[indice].oraSosire);
            return;
        }
        for(int j = 0; j < trainsCrt[indice].nrStatii; j++)
        {
            if(strcmp(trainsCrt[indice].statii[j].nume, numeOras) == 0)
            {
                strcpy(oraSosire, trainsCrt[indice].statii[j].oraSosire);
                return;
            } 
        }
        strcpy(oraSosire, "eroareOraSos");
    }
}

int getIntarziere(const char *idTren, const char *statie)
{
    for (int i = 0; i < trainCountAux; i++)
    {
        if (strcmp(trainsCrt[i].id, idTren) == 0)
        {
            if (strcmp(statie, trainsCrt[i].statiePlecare) == 0)
                return trainsCrt[i].intarzierePlecare;
            else if (strcmp(statie, trainsCrt[i].statieSosire) == 0)
                return trainsCrt[i].intarziereSosire;
            else
            {
                for(int j = 0; j < trainsCrt[i].nrStatii; j++)
                {
                    if(strcmp(statie, trainsCrt[i].statii[j].nume) == 0)
                        return trainsCrt[i].statii[j].intarziere;
                }


                printf("NUUUUUUUUUUUUUUUUUUUUUUUUU");   
                return 121212; // eroareee
            }
        }
    }
}

int findStationIndex(const char *stationName) 
{
    for (int i = 0; i < nrTotalStatii; i++) {
        if (strcmp(statiiTotale[i].nume, stationName) == 0) {
            return i;
        }
    }
    return -1;
}

void constructGraph(Graf *graf, const char *data) //rip idee
{
    //setAuxTrains(1, numeOras, data);

    for (int i = 0; i < trainCount; i++) {
        //printf("--------------- tren %s-----------------\n", trains[i].id);
        if(vizitat[i])
            continue;
        vizitat[i] = 1;
        TrainInfo train = trains[i];
        int statieStart = findStationIndex(train.statiePlecare);
        Arc arcS;
        arcS.destinatie = findStationIndex(train.statii[0].nume);
        arcS.trenIndex = i;
        if(train.nrStatii == 0)
        {
            arcS.destinatie = findStationIndex(train.statieSosire);
            graf->adiacenta[statieStart][graf->nrArce[statieStart]++] = arcS;
            continue;
        }
        graf->adiacenta[statieStart][graf->nrArce[statieStart]++] = arcS;
        //printf("%s - %s cu trenul %s\n", train.statiePlecare, train.statii[0].nume, trains[i].id);
        for (int j = 0; j < train.nrStatii - 1; j++) {
            int src = findStationIndex(train.statii[j].nume);
            int dest = findStationIndex(train.statii[j + 1].nume);

            // Adaugam un arc in graf
            Arc arc;
            arc.destinatie = dest;
            arc.trenIndex = i;

            graf->adiacenta[src][graf->nrArce[src]++] = arc;
            //printf("%s - %s cu trenul %s\n", train.statii[j].nume, train.statii[j + 1].nume, trains[i].id);

            if(j == train.nrStatii - 2)
            {
                int statieFinala = findStationIndex(train.statieSosire);
                Arc arcF;
                arcF.destinatie = statieFinala;
                arcF.trenIndex = i;
                graf->adiacenta[dest][graf->nrArce[dest]++] = arcF;
                //printf("%s - %s cu trenul %s\n", train.statii[j + 1].nume, train.statieSosire, trains[i].id);

            }
        }
    }
}

void afisareMers(const char *numeOras, const char *data, char *raspuns, size_t raspunsSize, int cl)
{
    char dataAzi[11] = ""; get_current_date(dataAzi, sizeof(dataAzi));
    
    setAuxTrains(2, numeOras, data);
    orderTrains(2, numeOras);

    if(!trainCountAux)
    {
        memset(raspuns, 0 , sizeof(raspuns));
        snprintf(raspuns, raspunsSize, "Nu exista un istoric pentru %s in data de %s", numeOras, data);       
        write(cl, raspuns, strlen(raspuns));
        usleep(2000);
        memset(raspuns, 0 , sizeof(raspuns));
        strcpy(raspuns, "END");
        return;
    }

    snprintf(raspuns, raspunsSize, "\nMersul trenurilor din %s in data de %s\n\nSosiri:\n%-12s | %-23s | %-23s | %s\n",
            numeOras, data, "Numar Tren", "De la", "Pana la", "Sosire");
    strcat(raspuns, "--------------------------------------------------------------------------");
    write(cl, raspuns, strlen(raspuns));
    usleep(2000);
    for (int i = 0; i < trainCountAux; i++) 
    {
        char oraSosStatie[10] = "";
        getOraSosirePlecare(2, numeOras, i, oraSosStatie);
        snprintf(raspuns, raspunsSize, "%-12s | %-23s | %-23s | %-7s",
               trainsCrt[i].id,
               trainsCrt[i].statiePlecare,
               trainsCrt[i].statieSosire,
               oraSosStatie);

        if (!trainsCrt[i].circulaAzi)
        {
            strcat(raspuns, " - nu circula in aceasta data");
        }
        else if (trainsCrt[i].circulaAzi == -1)
        {
            strcat(raspuns, " - nu sunt informatii exacte");
        }
        else if (strcmp(data, dataAzi) == 0 && trainsCrt[i].circulaAzi == 1)
        {
            char intarziereText[50] = "";
            int intarziere = getIntarziere(trainsCrt[i].id, numeOras);
            if (intarziere == 0)
                strcat(intarziereText, " - ajunge la timp");
            else if (intarziere > 0)
                snprintf(intarziereText, sizeof(intarziereText), " - are o intarziere de %d minute", intarziere);
            else if (intarziere < 0)
                snprintf(intarziereText, sizeof(intarziereText), " - ajunge mai devreme cu %d minute", -intarziere);
            else
                strcpy(intarziereText, " a fost o eroare aici!!!!");

            strcat(raspuns, intarziereText);
        }
        write(cl, raspuns, strlen(raspuns));
        usleep(2000);

        memset(raspuns, 0, sizeof(raspuns));
    }

    setAuxTrains(1, numeOras, data);
    orderTrains(1, numeOras);

    snprintf(raspuns, raspunsSize, "\n\nPlecari:\n%-12s | %-23s | %-23s | %7s\n",
            "Numar Tren", "De la", "Pana la", "Plecare");
    strcat(raspuns, "---------------------------------------------------------------------------");    
    write(cl, raspuns, strlen(raspuns));
    usleep(2000);
    for (int i = 0; i < trainCountAux; i++) 
    {
        char oraPlStatie[10] = "";
        getOraSosirePlecare(1, numeOras, i, oraPlStatie);
        if(oraPlStatie)
        snprintf(raspuns, raspunsSize, "%-12s | %-23s | %-23s | %-7s",
               trainsCrt[i].id,
               trainsCrt[i].statiePlecare,
               trainsCrt[i].statieSosire,
               oraPlStatie);

        if (!trainsCrt[i].circulaAzi)
        {
            strcat(raspuns, " - nu circula in aceasta data");
        }
        else if (trainsCrt[i].circulaAzi == -1)
        {
            strcat(raspuns, " - nu sunt informatii exacte");
        }
        else if (strcmp(data, dataAzi) == 0 && trainsCrt[i].circulaAzi == 1)
        {
            char intarziereText[50] = "";
            int intarziere = getIntarziere(trainsCrt[i].id, numeOras);
            if (intarziere == 0)
                strcat(intarziereText, " - pleaca la timp");
            else if (intarziere > 0)
                snprintf(intarziereText, sizeof(intarziereText), " - are o intarziere de %d minute", intarziere);
            else if (intarziere < 0)
                snprintf(intarziereText, sizeof(intarziereText), " - pleaca mai devreme cu %d minute", -intarziere);
            else
                strcpy(intarziereText, " a fost o eroare aici!!!!");

            strcat(raspuns, intarziereText);
        }
        write(cl, raspuns, strlen(raspuns));
        usleep(2000);

        memset(raspuns, 0, sizeof(raspuns));
    }

    memset(raspuns, 0, sizeof(raspuns));
    strcpy(raspuns, "END");
}

void afisareSosiri(const char *numeOras, char *raspuns, size_t raspunsSize, int cl) {    
    int found = 0;
    char buffer[1024] = "";
    char data[30] = ""; get_current_date(data, sizeof(data));
    setAuxTrains(2, numeOras, data);
    orderTrains(2, numeOras);

    int valid = 0;
    for (int i = 0; i < trainCountAux; i++) 
    {
        char oraSosStatie[10] = "";
        getOraSosirePlecare(2, numeOras, i, oraSosStatie);
        int offset = oraToInt(oraSosStatie) - get_current_time();
        if(offset >= 0 && offset <= 3600 * 1)// 1 poate fi inlocuit e nr de ore
        {
            if(trainsCrt[i].circulaAzi == 1)
            {
                //printf("tren valid : %s circula azi = %d\n", trainsCrt[i].id, trainsCrt->circulaAzi);
                valid = 1;
                break;
            }
        }
    }
    //printf("train count : %d\n", trainCountAux);
    if(!valid)
    {
        memset(raspuns, 0 , sizeof(raspuns));
        snprintf(raspuns, raspunsSize, "Nu exista trenuri care ajung la %s in urmatoarea ora", numeOras);       
        write(cl, raspuns, strlen(raspuns));
        usleep(2000);
        memset(raspuns, 0 , sizeof(raspuns));
        strcpy(raspuns, "END");
        return;
    }
    snprintf(raspuns, raspunsSize, "\nSosiri la %s in urmatoarea ora:\n%-12s | %-23s | %-23s | %s\n",
            numeOras, "Numar Tren", "De la", "Pana la", "Sosire");
    strcat(raspuns, "--------------------------------------------------------------------------");
    write(cl, raspuns, strlen(raspuns));
    usleep(2000);
    for (int i = 0; i < trainCountAux; i++) 
    {
        char oraSosStatie[10] = "";
        getOraSosirePlecare(2, numeOras, i, oraSosStatie);
        int offset = oraToInt(oraSosStatie) - get_current_time();
        if(offset >= 0 && offset <= 3600 * 1 && trainsCrt[i].circulaAzi == 1)// 1 poate fi inlocuit e nr de ore
        {
            //printf(" oraasosire tren: %d - ora curenta: %d\n", oraToInt(oraSosStatie), get_current_time());
            snprintf(raspuns, raspunsSize, "%-12s | %-23s | %-23s | %-7s",
                trainsCrt[i].id,
                trainsCrt[i].statiePlecare,
                trainsCrt[i].statieSosire,
                oraSosStatie);

            char intarziereText[50] = "";
            int intarziere = getIntarziere(trainsCrt[i].id, numeOras);
            if (intarziere == 0)
                strcat(intarziereText, " - ajunge la timp");
            else if (intarziere > 0)
                snprintf(intarziereText, sizeof(intarziereText), " - are o intarziere de %d minute", intarziere);
            else if (intarziere < 0)
                snprintf(intarziereText, sizeof(intarziereText), " - ajunge mai devreme cu %d minute", -intarziere);
            else
                strcpy(intarziereText, " a fost o eroare aici!!!!");

            strcat(raspuns, intarziereText);
            write(cl, raspuns, strlen(raspuns));
            usleep(2000);
        }
        memset(raspuns, 0, sizeof(raspuns));
    }
    
    memset(raspuns, 0, sizeof(raspuns));
    strcpy(raspuns, "END");
}

void afisarePlecari(const char *numeOras, char *raspuns, size_t raspunsSize, int cl) {    
    int found = 0;
    char buffer[1024] = "";
    char data[30] = ""; get_current_date(data, sizeof(data));
    setAuxTrains(1, numeOras, data);
    orderTrains(1, numeOras);

    int valid = 0;
    for (int i = 0; i < trainCountAux; i++) 
    {
        char oraPleStatie[10] = "";
        getOraSosirePlecare(1, numeOras, i, oraPleStatie);
        int offset = oraToInt(oraPleStatie) - get_current_time();
        if(offset >= 0 && offset <= 3600 * 1)// 1 poate fi inlocuit e nr de ore
        {
            valid = 1;
            break;
        }
    }
    //printf("train count : %d\n", trainCountAux);
    if(!valid)
    {
        memset(raspuns, 0 , sizeof(raspuns));
        snprintf(raspuns, raspunsSize, "Nu exista trenuri care pleaca din %s in urmatoarea ora", numeOras);       
        write(cl, raspuns, strlen(raspuns));
        usleep(2000);
        memset(raspuns, 0 , sizeof(raspuns));
        strcpy(raspuns, "END");
        return;
    }
    snprintf(raspuns, raspunsSize, "\nPlecari din %s in urmatoarea ora:\n%-12s | %-23s | %-23s | %s\n",
            numeOras, "Numar Tren", "De la", "Pana la", "Plecare");
    strcat(raspuns, "--------------------------------------------------------------------------");
    write(cl, raspuns, strlen(raspuns));
    usleep(2000);
    for (int i = 0; i < trainCountAux; i++) 
    {
        char oraPleStatie[10] = "";
        getOraSosirePlecare(1, numeOras, i, oraPleStatie);
        int offset = oraToInt(oraPleStatie) - get_current_time();
        if(offset >= 0 && offset <= 3600 * 1 && trainsCrt[i].circulaAzi == 1)// 1 poate fi inlocuit e nr de ore
        {
            snprintf(raspuns, raspunsSize, "%-12s | %-23s | %-23s | %-7s",
                trainsCrt[i].id,
                trainsCrt[i].statiePlecare,
                trainsCrt[i].statieSosire,
                oraPleStatie);

            char intarziereText[50] = "";
            int intarziere = getIntarziere(trainsCrt[i].id, numeOras);
            if (intarziere == 0)
                strcat(intarziereText, " - pleaca la timp");
            else if (intarziere > 0)
                snprintf(intarziereText, sizeof(intarziereText), " - are o intarziere de %d minute", intarziere);
            else if (intarziere < 0)
                snprintf(intarziereText, sizeof(intarziereText), " - pleaca mai devreme cu %d minute", -intarziere);
            else
                strcpy(intarziereText, " a fost o eroare aici!!!!");

            strcat(raspuns, intarziereText);
            
            write(cl, raspuns, strlen(raspuns));
            usleep(2000);
        }
        memset(raspuns, 0, sizeof(raspuns));
    }
    
    memset(raspuns, 0, sizeof(raspuns));
    strcpy(raspuns, "END");
}

void afisareInfoTren(const char *idTren, char *raspuns, size_t raspunsSize, int cl)
{
    loadExceptii();
    int found = 0;
    TrainInfo tren;
    char data[30] = ""; get_current_date(data, sizeof(data));
    char intarziereText[50] = "";

    for(int i = 0; i < trainCount; i++)
    {
        
        if(strcmp(trains[i].id, idTren) == 0)
        {
            if (trains[i].circulaAzi <= 0)
            {
                memset(raspuns, 0 , sizeof(raspuns));
                snprintf(raspuns, raspunsSize, "\nATENTIE: Acest tren nu circula astazi %s. Se vor afisa doar ultimele informatii cunoscute.", data); 
                write(cl, raspuns, strlen(raspuns));     
            }

            if(trains[i].nrStatii == 0)
            {
                usleep(2000);
                memset(raspuns, 0, sizeof(raspuns));
                snprintf(raspuns, raspunsSize, "\n\n %s\n------------------------------------------------------------------\n> %-23s %-7s => %-23s %-7s", idTren,
                trains[i].statiePlecare, trains[i].oraPlecare, trains[i].statieSosire, trains[i].oraSosire); 
                write(cl, raspuns, strlen(raspuns));   
                memset(raspuns, 0, sizeof(raspuns));
                found = 1;
                break;
    
            }
            else
            {
                usleep(2000);
                memset(raspuns, 0, sizeof(raspuns));
                memset(intarziereText, 0, sizeof(intarziereText));
                snprintf(raspuns, raspunsSize, "\n\n %s\n------------------------------------------------------------------\n> %-23s %-7s => %-23s %-7s", idTren,
                trains[i].statiePlecare, trains[i].oraPlecare, trains[i].statii[0].nume, trains[i].statii[0].oraSosire); 

                if(trains[i].intarzierePlecare > 0)
                    snprintf(intarziereText, sizeof(intarziereText), " +%d minute (intarziere)", trains[i].intarzierePlecare);
                else if (trains[i].intarzierePlecare < 0)
                    snprintf(intarziereText, sizeof(intarziereText),  "%d minute (mai devreme)", trains[i].intarzierePlecare);
                else
                    strcat(intarziereText, " la timp");
                strcat(raspuns, intarziereText);

                write(cl, raspuns, strlen(raspuns));
                usleep(20000);
                for(int j = 0; j < trains[i].nrStatii - 1; j++)
                {
                    memset(raspuns, 0, sizeof(raspuns));
                    memset(intarziereText, 0, sizeof(intarziereText));

                    snprintf(raspuns, raspunsSize, "> %-23s %-7s => %-23s %-7s",
                    trains[i].statii[j].nume, trains[i].statii[j].oraPlecare, trains[i].statii[j+1].nume, trains[i].statii[j+1].oraSosire);

                    if(trains[i].statii[j].intarziere > 0)
                        snprintf(intarziereText, sizeof(intarziereText), " +%d minute (intarziere)", trains[i].statii[j].intarziere);
                    else if (trains[i].statii[j].intarziere < 0)
                        snprintf(intarziereText, sizeof(intarziereText), " %d minute (mai devreme)", trains[i].statii[j].intarziere);
                    else
                        strcat(intarziereText, " la timp"); 
                    strcat(raspuns, intarziereText);

                    write(cl, raspuns, strlen(raspuns));
                    usleep(2000);

                    memset(raspuns, 0, sizeof(raspuns));
                    memset(intarziereText, 0, sizeof(intarziereText));
                }
                snprintf(raspuns, raspunsSize, "> %-23s %-7s => %-23s %-7s",
                trains[i].statii[trains[i].nrStatii - 1].nume,  trains[i].statii[trains[i].nrStatii - 1].oraPlecare,
                trains[i].statieSosire,  trains[i].oraSosire);

                if(trains[i].intarziereSosire > 0)
                    snprintf(intarziereText, sizeof(intarziereText), " +%d minute (intarziere)", trains[i].intarziereSosire);
                else if (trains[i].intarziereSosire < 0)
                    snprintf(intarziereText, sizeof(intarziereText), " %d minute (mai devreme)", trains[i].intarziereSosire);
                else
                    strcat(intarziereText, " la timp");
                strcat(raspuns, intarziereText);

                write(cl, raspuns, strlen(raspuns));
                usleep(2000);
                memset(raspuns, 0, sizeof(raspuns));
                found = 1;      
                break;
            }
        }
    }
    //printf("found = %d", found);
    if(!found)
    {
        snprintf(raspuns, raspunsSize, "Nu exista un train cu id-ul %s", idTren);
        write(cl, raspuns, strlen(raspuns));
        usleep(2000);
        memset(raspuns, 0, sizeof(raspuns));
        strcpy(raspuns, "END");

    }
    else
    {
        usleep(2000);
        memset(raspuns, 0, sizeof(raspuns));
        strcpy(raspuns, "END");
        //++++++usleep(2000);
    }
}

void addIntarziere(const char *idTren, const char *statieAdd, int minute, char *raspuns, size_t raspunsSize, int cl)
{
    int found = 0, statieValida = 0;
    TrainInfo tren;
    int iStart = 0;
    for(int i = 0; i < trainCount; i++)
    {
        if(strcmp(idTren, trains[i].id) == 0)
        {
            if(trains[i].circulaAzi <= 0)
            {
                snprintf(raspuns, raspunsSize, "Trenul %s nu circula astazi.", idTren);
                return;
            }
            if(strcmp(trains[i].statieSosire, statieAdd))
            {
                if(strcmp(trains[i].statiePlecare, statieAdd) == 0)
                {
                    iStart = -1;
                    statieValida = 1;
                }
                else
                {
                    for(int k = 0; k < trains[i].nrStatii; k++)
                    {
                        if(strcmp(trains[i].statii[k].nume, statieAdd) == 0)
                        {
                            iStart = k;
                            statieValida = 1;
                            break;
                        }
                    }
                }
            }
            else
            {
                iStart = trains[i].nrStatii;
                statieValida = 1;
            }

            if(statieValida)
            {
                if(iStart == -1)
                {
                    trains[i].intarzierePlecare += minute;
                    int oraInit = oraToInt(trains[i].oraPlecare);
                    oraInit += minute * 60;
                    snprintf(trains[i].oraPlecare, sizeof(trains[i].oraPlecare),"%d", oraInit);
                    convertToTime(trains[i].oraPlecare);
                    iStart = 0;
                }

                for(int j = iStart; j < trains[i].nrStatii; j++)
                {
                    trains[i].statii[j].intarziere += minute;
                    int oraInit = oraToInt(trains[i].statii[j].oraSosire);
                    oraInit += minute * 60;
                    snprintf(trains[i].statii[j].oraSosire, sizeof(trains[i].statii[j].oraSosire), "%d", oraInit);
                    convertToTime(trains[i].statii[j].oraSosire);

                    oraInit = oraToInt(trains[i].statii[j].oraPlecare);
                    oraInit += minute * 60; 
                    snprintf(trains[i].statii[j].oraPlecare, sizeof(trains[i].statii[j].oraPlecare), "%d", oraInit);
                    convertToTime(trains[i].statii[j].oraPlecare);
                }
                
                trains[i].intarziereSosire += minute;
                int oraInit = oraToInt(trains[i].oraSosire);
                oraInit += minute * 60;
                snprintf(trains[i].oraSosire, sizeof(trains[i].oraSosire), "%d", oraInit);
                convertToTime(trains[i].oraSosire);
                //intarziere ult statie;
            }
            
            found = 1;
            break;
        }
    }

    if(!found)
    {
        snprintf(raspuns, raspunsSize, "Nu exista un train cu id-ul %s", idTren);
    }
    else
    {
        if(!statieValida)
        {
            snprintf(raspuns, raspunsSize, "Trenul %s nu opreste in statia %s sau aceasta nu exista.", idTren, statieAdd);
        }
        else
        {
            snprintf(raspuns, raspunsSize, "O intarziere de %d minute a fost adaugata trenului %s incepand cu statia %s", minute, idTren, statieAdd);
        }
    }
    if (saveXML(trainsFile) == 0)
        printf("[SERVER]Am scris %d trenuri in %s si %d exceptii.\n", trainCount, trainsFile, exceptionCount);
    else
        printf("[SERVER]Eroare la saveXML()\n");
}

void resetIntarzieri()
{
    for(int i = 0; i < trainCount; i++)
    {
        if(trains[i].intarzierePlecare != 0)
        {
            int oraInit = oraToInt(trains[i].oraPlecare);
            oraInit -= trains[i].intarzierePlecare * 60;
            snprintf(trains[i].oraPlecare, sizeof(trains[i].oraPlecare),"%d", oraInit);
            convertToTime(trains[i].oraPlecare);
            trains[i].intarzierePlecare = 0;
        }
        for(int j = 0; j < trains[i].nrStatii; j++)
        {
            if(trains[i].statii[j].intarziere != 0)
            {
                int oraInit = oraToInt(trains[i].statii[j].oraSosire);
                oraInit -= trains[i].statii[j].intarziere * 60;
                snprintf(trains[i].statii[j].oraSosire, sizeof(trains[i].statii[j].oraSosire), "%d", oraInit);
                convertToTime(trains[i].statii[j].oraSosire);

                oraInit = oraToInt(trains[i].statii[j].oraPlecare);
                oraInit -= trains[i].statii[j].intarziere * 60;
                snprintf(trains[i].statii[j].oraPlecare, sizeof(trains[i].statii[j].oraPlecare), "%d", oraInit);
                convertToTime(trains[i].statii[j].oraPlecare);  
                trains[i].statii[j].intarziere = 0;
            }
        }
        if(trains[i].intarziereSosire != 0)
        {
            int oraInit = oraToInt(trains[i].oraSosire);
            oraInit -= trains[i].intarziereSosire * 60;
            snprintf(trains[i].oraSosire, sizeof(trains[i].oraSosire),"%d", oraInit);
            convertToTime(trains[i].oraSosire);
            trains[i].intarziereSosire = 0;
        }
    }
    saveXML(trainsFile);
}

int main (int argc, char *argv[])
{
    struct sockaddr_in server;	
    void threadCreate(int);

    if(argc<2)
    {
        fprintf(stderr,"Eroare: Primul argument este numarul de fire de executie...");
        exit(1);
    }
    nthreads=atoi(argv[1]);
    if(nthreads <=0)
    {
        fprintf(stderr,"Eroare: Numar de fire invalid...");
        exit(1);
    }
    threadsPool = (Thread*)calloc(nthreads, sizeof(Thread));

    clients = (ClientState*)calloc(nthreads, sizeof(ClientState));
    for (int i = 0; i < nthreads; i++) 
    {
        clients[i].socket = -1;
        clients[i].logged_in = 0;
        memset(clients[i].username, 0, sizeof(clients[i].username));
    }
    
    /* crearea unui socket */
    if ((sd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror ("[SERVER]Eroare la socket().\n");
        return errno;
    }
    /* utilizarea optiunii SO_REUSEADDR */
    int on=1;
    setsockopt(sd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));

    /* pregatirea structurilor de date */
    bzero (&server, sizeof (server));

    /* umplem structura folosita de server */
    /* stabilirea familiei de socket-uri */
    server.sin_family = AF_INET;	
    /* acceptam orice adresa */
    server.sin_addr.s_addr = htonl (INADDR_ANY);
    /* utilizam un port utilizator */
    server.sin_port = htons (PORT);

    /* atasam socketul */
    if (bind (sd, (struct sockaddr *) &server, sizeof (struct sockaddr)) == -1)
    {
        perror ("[SERVER]Eroare la bind().\n");
        return errno;
    }

/* punem serverul sa asculte daca vin clienti sa se conecteze */
    if (listen (sd, 2) == -1)
    {
        perror ("[SERVER]Eroare la listen().\n");
        return errno;
    }
    

    printf("Nr threaduri %d \n", nthreads); fflush(stdout);
    int i;
    for(i = 0; i < nthreads; i++) 
    {
        threadCreate(i);
    }
    
    if (readXML(trainsFile) == 0) 
        printf("[SERVER]Am citit %d trenuri din %s si %d exceptii.\n", trainCount, trainsFile, exceptionCount);
    else
        printf("[SERVER]Eroare la readXML()\n");

    //resetIntarzieri();

    if (saveXML(trainsFile) == 0)
        printf("[SERVER]Am scris %d trenuri in %s si %d exceptii.\n", trainCount, trainsFile, exceptionCount);
    else
        printf("[SERVER]Eroare la saveXML()\n");

/* servim in mod concurent clientii...folosind thread-uri */
    for ( ; ; ) 
    {
        printf ("[SERVER]Asteptam la portul %d...\n",PORT);
        pause();				
    }
    return 0;
};

void response_manage(int cl, int idThread)
{
    char comanda[1024];
    char raspuns[1024]; 
    ssize_t bytes_read;

    while(1)
    {
        memset(comanda, 0, sizeof(comanda));
        memset(raspuns, 0, sizeof(raspuns));
        bytes_read = read(cl, comanda, sizeof(comanda) - 1);
        if (bytes_read <= 0) 
        {
            printf("[SERVER] Clientul de pe thread-ul %d a fost deconectat.\n", idThread);
            
            return;
        }
        comanda[bytes_read] = '\0'; 

        printf("[Thread %d] Comanda primită: %s\n", idThread, comanda);

        if (strncmp(comanda, "login", 5) == 0) {
            char user[50], parola[50];
            if (sscanf(comanda, "login %49s %49s", user, parola) == 2) 
            {
                verificareLogin(user, parola, raspuns, sizeof(raspuns), cl);
            } 
            else 
            {
                strcpy(raspuns, "Eroare: login necesita 2 parametri (user si parola).\nFormat: login <user> <parola>");
            }
        }
        else if (strncmp(comanda, "plecari", 7) == 0) 
        {
            
            char numeOras[100] = "";

            if (sscanf(comanda, "plecari %99s", numeOras) == 1) 
            {
                strcpy(raspuns, "sirLung");
                write(cl, raspuns, strlen(raspuns));
                afisarePlecari(numeOras, raspuns, sizeof(raspuns), cl);
            }
            else 
            {
                strcpy(raspuns, "Eroare: plecari necesita un parametru numeOras\nFormat: info <numeOras>");
            }
        }
        else if (strncmp(comanda, "sosiri", 6) == 0) 
        {
            char numeOras[100] = "";

            if (sscanf(comanda, "sosiri %99s", numeOras) == 1) 
            {
                strcpy(raspuns, "sirLung");
                write(cl, raspuns, strlen(raspuns));
                afisareSosiri(numeOras, raspuns, sizeof(raspuns), cl);
            }
            else 
            {
                strcpy(raspuns, "Eroare: sosiri necesita un parametru numeOras\nFormat: sosiri <numeOras>");
            }
        }
        else if (strncmp(comanda, "reset", 5) == 0) //reset la un tren specific / all ??
        {
            if (isLogged(cl)) 
            {
                resetIntarzieri();
                strcpy(raspuns, "Intarzierile trenurilor au fost resetate cu succes");
            }
            else 
            {
                strcpy(raspuns, "Eroare: trebuie sa fii logat pentru a folosi aceasta comanda.\nFoloseste comanda 'login <user> <parola>'");
            }
        }
        else if (strncmp(comanda, "trenuri", 7) == 0)
        {
            char *token;
            char numeOras[100] = "";
            char data[20] = "";

            token = strtok(comanda, " ");
            if (token == NULL || strcmp(token, "trenuri") != 0) 
            {
                strcpy(raspuns, "Eroare: comanda necunoscuta.");
                return;
            }

            token = strtok(NULL, " ");
            while (token != NULL) 
            {
                if (strlen(token) == 10 && token[2] == '.' && token[5] == '.') 
                {
                    strncpy(data, token, sizeof(data) - 1);
                    break;
                } 
                else 
                {
                    if (strlen(numeOras) > 0) 
                    {
                        strncat(numeOras, " ", sizeof(numeOras) - strlen(numeOras) - 1);
                    }
                    strncat(numeOras, token, sizeof(numeOras) - strlen(numeOras) - 1);
                }
                token = strtok(NULL, " ");
            }
            if (strlen(data) == 0) 
            {
                get_current_date(data, sizeof(data));
            }
            
            if (strlen(numeOras) == 0) 
            {
                strcpy(raspuns, "Eroare: sosiri necesita cel putin 1 parametru (numeOras).\nFormat: sosiri <numeOras> [data]");
            } 
            else 
            {
                strcpy(raspuns, "sirLung");
                write(cl, raspuns, strlen(raspuns));
                afisareMers(numeOras, data, raspuns, sizeof(raspuns), cl);
            }
        }
        else if (strncmp(comanda, "intarziere", 10) == 0) 
        {
            char nrTren[16];
            char numeStatie[30];
            int nrMinute;
            int offset = 0;

            if (sscanf(comanda, "intarziere %15s %n", nrTren, &offset) == 1 && isLogged(cl)) 
            {
                const char *rest = comanda + offset;

                char *minuteStart = strrchr(rest, ' ');
                if (minuteStart && sscanf(minuteStart + 1, "%d", &nrMinute) == 1) 
                {
                    size_t numeLen = minuteStart - rest;
                    if (numeLen >= sizeof(numeStatie)) 
                    {
                        numeLen = sizeof(numeStatie) - 1;
                    }
                    strncpy(numeStatie, rest, numeLen);
                    numeStatie[numeLen] = '\0';

                    int valid = 0;
                    for (size_t i = 0; i < numeLen; i++) 
                    {
                        if ((numeStatie[i] >= 'a' && numeStatie[i] <= 'z') || (numeStatie[i] >= 'A' && numeStatie[i] <= 'Z')) 
                        {
                            valid = 1;
                            break;
                        }
                    }

                    if (valid) 
                    {
                        addIntarziere(nrTren, numeStatie, nrMinute, raspuns, sizeof(raspuns), cl);
                    } 
                    else 
                    {
                        strcpy(raspuns, "Eroare: numele statiei trebuie sa contina cel putin o litera.\nFormat: intarziere <idTren> <numeStatieInceput> <nrMinute>");
                    }
                } 
                else 
                {
                    strcpy(raspuns, "Eroare: intarziere necesita un numar valid la final.\nFormat: intarziere <idTren> <numeStatieInceput> <nrMinute>");
                }
            } 
            else if (!isLogged(cl))
            {
                strcpy(raspuns, "Eroare: trebuie sa fii logat pentru a folosi aceasta comanda.\nFoloseste comanda 'login <user> <parola>'");
            }
            else 
            {
                strcpy(raspuns, "Eroare: intarziere necesita 3 parametrii\nFormat: intarziere <idTren> <numeStatieInceput> <nrMinute>");
            }
        }
        else if (strncmp(comanda, "info", 4) == 0)  
        {
            char idTren[16];

            if (sscanf(comanda, "info %15s", idTren) == 1) 
            {
                strcpy(raspuns, "sirLung");
                write(cl, raspuns, strlen(raspuns));
                afisareInfoTren(idTren, raspuns, sizeof(raspuns), cl);
            }
            else 
            {
                strcpy(raspuns, "Eroare: info necesita un parametru idTren\nFormat: info <idTren>");
            }
        }
        else if (strcmp(comanda, "logout") == 0) 
        {
            logoutComanda(raspuns, sizeof(raspuns), cl);
        }
        else if (strcmp(comanda, "help") == 0) 
        {
            strcpy(raspuns, "\nComenzi disponibile:\n");
            strcat(raspuns, ">login <user> <parola>                   | Conecteaza clientul.\n");
            strcat(raspuns, ">logout                                  | Delogheaza clientul.\n");
            strcat(raspuns, ">plecari <numeOras>                      | Afiseaza plecarile din <numeOras> din urmatoarea ora.\n");
            strcat(raspuns, ">sosiri <numeOras>                       | Afiseaza sosirile in <numeOras> din urmatoarea ora.\n");
            strcat(raspuns, ">trenuri <numeOras> [DD.MM.YYYY]         | Afiseaza mersurilor trenurilor din <numeOras> la data oferita(optional).\n");
            strcat(raspuns, ">info <idTren>                           | Ofera informatii despre trenul specificat.\n");
            strcat(raspuns, "*intarziere <idTren> <statie> <nrMinute> | Adauga o intarziere in minute trenului specificat.\n");
            strcat(raspuns, "*reset                                   | Reseteaza toate intarzierile trenurilor.\n");
            strcat(raspuns, ">quit                                    | Deconectare client.");
        }
        else 
        {
            strcpy(raspuns, "Eroare: Comanda necunoscuta. Scrie 'help' pentru lista de comenzi disponibile.");
        }

        printf("vrea sa se afiseze : %s\n", raspuns);
        write(cl, raspuns, strlen(raspuns));
    }
}

void *treat(void *arg)
{
    int client;
    struct sockaddr_in from;
    socklen_t length = sizeof(from);
    int thread_id = (int)(intptr_t)arg;

    //printf("[thread]- %d - pornit...\n", thread_id);

    while (1)
    {
        pthread_mutex_lock(&mlock);
        client = accept(sd, (struct sockaddr *)&from, &length);
        pthread_mutex_unlock(&mlock);

        if (client < 0)
        {
            perror("[thread] Eroare la accept().\n");
            continue;
        }

        threadsPool[thread_id].thCount++;
        response_manage(client, thread_id);
        close(client);
    }
    
    return NULL;
}

void threadCreate(int i)
{
    pthread_create(&threadsPool[i].idThread, NULL, &treat, (void *)(intptr_t)i);
}

