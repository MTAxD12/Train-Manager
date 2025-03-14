#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BUFFER_SIZE 1024
#define MAX_LINE_L 256
#define MAX_PATH_L 512
#define MAX_STATIONS 80
#define MAX_TRAINS 1500
#define MAX_EXCEPTIONS 100

typedef struct {
    char dataStart[11];
    char dataFinal[11];
} ExceptionInfo;

typedef struct {
    char nume[50];
    char oraSosire[10];
    char oraPlecare[10];
} Station;

typedef struct {
    char id[16];
    char statiePlecare[50];
    char statieSosire[50];
    char oraPlecare[10];
    char oraSosire[10];
    Station statii[MAX_STATIONS];
    int nrStatii;
    ExceptionInfo exceptii[MAX_EXCEPTIONS];
    int nrExceptii;
} TrainInfo;

TrainInfo currentTrain;
TrainInfo trains[MAX_TRAINS];
int trainCount = 0;
int inTrain = 0;
int inTrainHeader = 0;
int inStatii = 0;
int inStatieHeader = 0;
int inExceptii = 0;
int inExceptiiHeader = 0;
char id[15]="";
char id_litere[5]="";
char id_numere[10]="";
char last_station[50]="";
char last_station_sosire[12]="";
char station_Name[50]="";
char station_Arrive[12]="";
char station_Leave[12]="";
char station_stay[12]="";
char exceptionStart[12]="";
char exceptionEnd[12] = "";

void reset_total()
{
    memset(&currentTrain, 0, sizeof(TrainInfo)); 
    strcpy(station_Arrive, "");
    strcpy(station_Leave, "");
    strcpy(station_Name, "");
    strcpy(station_stay, "");
}

void convertToTime(char *timeStr) 
{
    int seconds = atoi(timeStr);
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;

    sprintf(timeStr, "%02d:%02d", hours, minutes);
}

void extrageAtribut(char *linie, const char *atribut, char *rezultat) 
{
    char *start = strstr(linie, atribut);
    if (start) 
    {
        start += strlen(atribut) + 2; 
        char *end = strchr(start, '"');
        if (end) 
        {
            strncpy(rezultat, start, end - start);
            rezultat[end - start] = '\0';
        }
    }
}

int main() 
{
    FILE *input = fopen("baza.xml", "r");

    if (!input) 
    {
        return 1;
    }

    char linie[BUFFER_SIZE];

    printf("<Trenuri>\n");
    memset(&currentTrain, 0, sizeof(TrainInfo));
    while (fgets(linie, BUFFER_SIZE, input)) 
    {
        if (strstr(linie, "<Tren") && !inTrainHeader) \
        {
            printf("    <Tren>\n");
            inTrain = 1;
            inTrainHeader = 1;
            currentTrain.nrStatii = 0;
            reset_total();

        }
        else if (strstr(linie, "</Tren>")) 
        {
            trains[trainCount] = currentTrain;
            trainCount++;
            inTrain = 0;
            printf("    </Tren>\n Am gasit : %d statii", trains[trainCount].nrStatii);
            reset_total();
        }
        else if (strstr(linie, "<Trase>"))
        {
            printf("        <Statii>\n");
            inStatii = 1;
        }
        else if (strstr(linie, "</Trase>"))
        {
            printf("        </Statii>\n");
            inStatii = 0;
            strcpy(currentTrain.statieSosire, last_station);
            convertToTime(last_station_sosire);
            strcpy(currentTrain.oraSosire, last_station_sosire);
            strcpy(last_station, "");
            strcpy(last_station_sosire, "");
            printf("am pus ultima statie\n");
            printf("statie sosire : %s\n", currentTrain.statieSosire);
            printf("ora: %s\n", currentTrain.oraSosire);
        }
        else if (strstr(linie, "<RestrictiiTren>"))
        {
            printf("        <Exceptii>\n");
            inExceptii = 1;
        }
        else if (strstr(linie, "</RestrictiiTren>"))
        {
            printf("        </Exceptii>\n");
            inExceptii = 0;
        }

        if (inTrainHeader) 
        {
            extrageAtribut(linie, "CategorieTren", id_litere);
            extrageAtribut(linie, "Numar", id_numere);

            if (id_litere[0] != '\0' && id_numere[0] != '\0') 
            {
                snprintf(id, sizeof(id), "%s%s", id_litere, id_numere);
                printf("        <ID>%s</ID>\n", id);
                strcpy(currentTrain.id, id);
                inTrainHeader = 0; 
            }
        }

        if ((strstr(linie, "<CalendarTren")) && inExceptii)
        {
            inExceptiiHeader = 1;
            extrageAtribut(linie, "DeLa", exceptionStart);
            extrageAtribut(linie, "PinaLa", exceptionEnd);

            if(exceptionStart[0] != '\0' && exceptionEnd[0] != '\0')
            {
                char startAux[12] = "";
                char endAux[12] = "";
                snprintf(startAux, sizeof(startAux), "%c%c.%c%c.%c%c%c%c",
                    exceptionStart[6], exceptionStart[7],
                    exceptionStart[4], exceptionStart[5],
                    exceptionStart[0], exceptionStart[1], exceptionStart[2], exceptionStart[3]);
                
                snprintf(endAux, sizeof(endAux), "%c%c.%c%c.%c%c%c%c",
                    exceptionEnd[6], exceptionEnd[7],
                    exceptionEnd[4], exceptionEnd[5],
                    exceptionEnd[0], exceptionEnd[1], exceptionEnd[2], exceptionEnd[3]);
                    
                //printf("am extras: exceptie de la %s pana la %s", startAux, exceptionEnd);

                strcpy(currentTrain.exceptii[currentTrain.nrExceptii].dataStart, startAux);
                strcpy(currentTrain.exceptii[currentTrain.nrExceptii].dataFinal, endAux);
                currentTrain.nrExceptii++;
                inExceptiiHeader = 0;

                strcpy(exceptionStart, "");
                strcpy(exceptionEnd, "");
            }

        }

        if ((strstr(linie, "<ElementTrasa") || inStatieHeader) && inStatii) 
        {
            inStatieHeader = 1;
            extrageAtribut(linie, "DenStaOrigine", station_Name);
            extrageAtribut(linie, "OraP", station_Leave);
            extrageAtribut(linie, "StationareSecunde", station_stay);

            if (station_Name[0] != '\0' && station_Leave[0] != '\0' && station_stay[0] != '\0') 
            {
                //printf("am extras: %s, %s, %s si count statii crt: %d\n", station_Name, station_Leave, station_stay, currentTrain.nrStatii);
                strcpy(last_station, station_Name);
                strcpy(last_station_sosire, station_Leave);
                if(currentTrain.nrStatii == 0)
                {
                    convertToTime(station_Leave);
                    strcpy(currentTrain.statiePlecare, station_Name);
                    strcpy(currentTrain.oraPlecare, station_Leave);
                    inStatieHeader = 0;
                    //printf("am pus prima statie\n");
                    //printf("statie plecare : %s", currentTrain.statiePlecare);
                    currentTrain.nrStatii++;
                }
                else if(atoi(station_stay) > 0 && currentTrain.nrStatii > 0)
                {   
                    int oraP = atoi(station_Leave), stationare = atoi(station_stay);
                    int oraS = oraP - stationare;
                    //printf("oraS:%d\n", oraS);
                    //printf("stationare: %d\n", stationare);
                    //printf("oraP:%d\n", oraP);
                    snprintf(station_Arrive, sizeof(station_Arrive), "%d", oraS);

                    convertToTime(station_Leave);
                    convertToTime(station_Arrive);


                    printf("            <Statie> -- traincount: %d, nrstatii: %d\n", trainCount, currentTrain.nrStatii);
                    printf("                <Nume>%s</Nume>\n", station_Name);
                    printf("                <OraSosire>%s</OraSosire>\n", station_Arrive);
                    printf("                <OraPlecare>%s</OraPlecare>\n", station_Leave);
                    printf("            </Statie>\n");

                    inStatieHeader = 0;
                    strcpy(currentTrain.statii[currentTrain.nrStatii].nume, station_Name);
                    strcpy(currentTrain.statii[currentTrain.nrStatii].oraSosire, station_Arrive);
                    strcpy(currentTrain.statii[currentTrain.nrStatii].oraPlecare, station_Leave);
                    currentTrain.nrStatii++;
                    strcpy(currentTrain.statieSosire, station_Name); // in caz de 
                    strcpy(currentTrain.oraSosire, station_Arrive); // are timp de stationare ultima statie
                    strcpy(station_Arrive, "");
                    strcpy(station_Leave, "");
                    strcpy(station_Name, "");
                    strcpy(station_stay, "");
                }
            }
            
        }
    }

    printf("</Trenuri>\n");
    printf("Numar total de trenuri: %d\n -------------------------------\n", trainCount);

    FILE *file = fopen("tren_output.xml", "w");
    if (!file) 
    {
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
        fprintf(file, "        <IntarzierePlecare>%d</IntarzierePlecare>\n", 0);
        fprintf(file, "        <IntarziereSosire>%d</IntarziereSosire>\n", 0);


        if (trains[i].nrStatii > 0) 
        {
            fprintf(file, "        <Statii>\n");
            for (int j = 1; j < trains[i].nrStatii; j++) 
            {
                fprintf(file, "            <Statie>\n");
                fprintf(file, "                <Nume>%s</Nume>\n", trains[i].statii[j].nume);
                fprintf(file, "                <OraSosire>%s</OraSosire>\n", trains[i].statii[j].oraSosire);
                fprintf(file, "                <OraPlecare>%s</OraPlecare>\n", trains[i].statii[j].oraPlecare);
                fprintf(file, "                <Intarziere>%d</Intarziere>\n", 0);
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
    fprintf(file, "am printa2t");
    fclose(file);
    return 0;
}
