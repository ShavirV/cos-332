/*
 * Telnet Appointment Server
 * uses fork() for multi-user
 * single shared binary database, stored in appointments.db
 * ANSI UI
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#define PORT    5555
#define DB_FILE "appointments.db"

//main method of storage
typedef struct {
    int  id;
    char date[11];  //YYYY-MM-DD
    char time[6];   //HH:MM
    char with[50];
    char note[80];
} Appointment;

//ANSI escape sequences for styling
#define RST  "\033[0m"
#define BOLD "\033[1m"
#define RED  "\033[31m"
#define GRN  "\033[32m"
#define YLW  "\033[33m"
#define CYN  "\033[36m"
#define WHT  "\033[37m"

//write a string to socket fd (file descriptor, our client connection)
static void writeString(int fd, const char *s) {
    write(fd, s, strlen(s));
}

//clear screen and move cursor home
static void clearScreen(int fd) {
    writeString(fd, "\033[2J\033[H");
}

//read one line from the socket into buf of max size sz, supports backspace
static void readLine(int fd, char *buf, size_t sz) {
    size_t i = 0;
    char c;
    memset(buf, 0, sz); //make sure no garbage data

    //continue reading byte by byte
    while (read(fd, &c, 1) > 0) {
        if (c == '\r') { //\r = carriage return, user pressed enter
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd, &fds);
            struct timeval tv = {0, 5000}; //short timeout to catch the \n that follows
            //telnet sends \r\n for enter — drain the \n so it doesn't bleed into the next read
            if (select(fd+1, &fds, NULL, NULL, &tv) > 0) {
                char discard;
                read(fd, &discard, 1);
            }
            write(fd, "\r\n", 2); //echo newline
            break;
        }
        if (c == '\n') { //bare newline fallback
            write(fd, "\r\n", 2);
            break;
        }
        if ((c == 127 || c == 8) && i > 0) { //backspace was pressed
            i--;
            write(fd, "\b \b", 3);
            continue;
        }
        if (i < sz - 1) { //normal character, echo it back
            buf[i++] = c;
            write(fd, &c, 1);
        }
    }
    buf[i] = '\0';
}

//helpers to do general UI things
static void prompt(int fd, const char *label) {
    writeString(fd, YLW "  > " WHT);
    writeString(fd, label);
    writeString(fd, RST);
}

static void waitEnter(int fd) {
    char tmp[4];
    writeString(fd, "\r\n  " CYN "[enter to continue]" RST " ");
    readLine(fd, tmp, sizeof(tmp));
}

//database functionality

//count records by dividing file size by the size of one Appointment struct
static int countAppointments(void) {
    FILE *f = fopen(DB_FILE, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    int count = (int)(ftell(f) / (long)sizeof(Appointment));
    fclose(f);
    return count;
}

//write a new appointment to the end of the database file
static void addAppointment(Appointment *a) {
    FILE *f = fopen(DB_FILE, "ab"); //ab = append binary
    if (!f) return;
    fwrite(a, sizeof(*a), 1, f);
    fclose(f);
}

//read and print every appointment as a plain table
static void listAppointments(int fd) {
    FILE *f = fopen(DB_FILE, "rb"); //rb = read binary
    if (!f) { writeString(fd, "  No appointments.\r\n"); return; }

    int count = 0;
    Appointment a;

    writeString(fd, "  ID   Date        Time   With                  Note\r\n");
    writeString(fd, "  ---- ----------  -----  --------------------  ----------\r\n");

    //read one record at a time and print it
    while (fread(&a, sizeof(a), 1, f) == 1) {
        char line[160];
        //%-4d = left-aligned integer width 4, %-10s = left-aligned string width 10...
        snprintf(line, sizeof(line), "  %-4d %-10s  %-5s  %-20s  %s\r\n",
                 a.id, a.date, a.time, a.with, a.note);
        writeString(fd, line);
        count++;
    }
    fclose(f);

    writeString(fd, "\r\n");
    if (count == 0) {
        writeString(fd, "  No appointments yet.\r\n");
    } else {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "  %d appointment(s).\r\n", count);
        writeString(fd, tmp);
    }
}

//search appointments — print any record whose 'with' field contains the query
static void searchAppointments(int fd, const char *query) {
    FILE *f = fopen(DB_FILE, "rb");
    if (!f) { writeString(fd, "  No appointments.\r\n"); return; }

    int found = 0;
    Appointment a;

    writeString(fd, "  ID   Date        Time   With                  Note\r\n");
    writeString(fd, "  ---- ----------  -----  --------------------  ----------\r\n");

    while (fread(&a, sizeof(a), 1, f) == 1) {
        //strstr checks if query appears anywhere inside the 'with' field
        if (strstr(a.with, query) != NULL) {
            char line[160];
            snprintf(line, sizeof(line), "  %-4d %-10s  %-5s  %-20s  %s\r\n",
                     a.id, a.date, a.time, a.with, a.note);
            writeString(fd, line);
            found++;
        }
    }
    fclose(f);

    writeString(fd, "\r\n");
    if (found == 0) {
        writeString(fd, "  No matches found.\r\n");
    } else {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "  %d match(es).\r\n", found);
        writeString(fd, tmp);
    }
}

//does not modify the file directly
//copies every record except the target into a temp file, then renames it over the original
static void deleteAppointment(int target) {
    FILE *f = fopen(DB_FILE, "rb");
    if (!f) return;

    FILE *t = fopen("appointments.tmp", "wb");
    if (!t) { fclose(f); return; }

    Appointment a;
    while (fread(&a, sizeof(a), 1, f) == 1)
        if (a.id != target) fwrite(&a, sizeof(a), 1, t); //if not target, copy it across

    fclose(f);
    fclose(t);
    rename("appointments.tmp", DB_FILE); //atomic swap
}

//menus

//prompt the user to fill in appointment fields and save to the file
static void menuAdd(int fd) {
    Appointment a;
    memset(&a, 0, sizeof(a)); //zero out garbage
    a.id = countAppointments() + 1; //next ID = current count + 1

    clearScreen(fd);
    writeString(fd, "\r\n  " CYN BOLD "-- Add Appointment --\r\n\r\n" RST);
    prompt(fd, "Date (YYYY-MM-DD): "); readLine(fd, a.date, sizeof(a.date));
    prompt(fd, "Time (HH:MM):      "); readLine(fd, a.time, sizeof(a.time));
    prompt(fd, "With:              "); readLine(fd, a.with, sizeof(a.with));
    prompt(fd, "Note (optional):   "); readLine(fd, a.note, sizeof(a.note));

    addAppointment(&a);
    writeString(fd, "\r\n  " GRN "Saved." RST);
    waitEnter(fd);
}

static void menuList(int fd) {
    clearScreen(fd);
    writeString(fd, "\r\n  " CYN BOLD "-- Appointments --\r\n\r\n" RST);
    listAppointments(fd);
    waitEnter(fd);
}

static void menuSearch(int fd) {
    clearScreen(fd);
    writeString(fd, "\r\n  " CYN BOLD "-- Search --\r\n\r\n" RST);
    char query[50];
    prompt(fd, "Search 'with' field: ");
    readLine(fd, query, sizeof(query));
    writeString(fd, "\r\n");
    searchAppointments(fd, query);
    waitEnter(fd);
}

static void menuDelete(int fd) {
    clearScreen(fd);
    writeString(fd, "\r\n  " CYN BOLD "-- Delete Appointment --\r\n\r\n" RST);
    listAppointments(fd);

    char buf[16];
    writeString(fd, "\r\n");
    prompt(fd, "ID to delete (0 = cancel): ");
    readLine(fd, buf, sizeof(buf));

    int id = atoi(buf);
    if (id <= 0) {
        writeString(fd, "  " YLW "Cancelled.\r\n" RST);
    } else {
        deleteAppointment(id);
        writeString(fd, "  " GRN "Deleted.\r\n" RST);
    }
    waitEnter(fd);
}

//client session runs in a forked child process, one per connected user
static void handleClient(int fd) {
    char ch[4]; //menu choice buffer

    while (1) {
        clearScreen(fd);

        writeString(fd, CYN BOLD
            "\r\n"
            "    +----------------------------------+\r\n"
            "    |      APPOINTMENT MANAGER         |\r\n"
            "    +----------------------------------+\r\n\r\n"
            RST);

        writeString(fd,
            "     " GRN BOLD "1" RST WHT "  Add appointment\r\n"
            "     " GRN BOLD "2" RST WHT "  List appointments\r\n"
            "     " GRN BOLD "3" RST WHT "  Search appointments\r\n"
            "     " RED BOLD "4" RST WHT "  Delete appointment\r\n"
            "     " RED BOLD "5" RST WHT "  Quit\r\n\r\n" RST);

        prompt(fd, "Command: ");
        readLine(fd, ch, sizeof(ch));

        if      (ch[0] == '1') menuAdd(fd);
        else if (ch[0] == '2') menuList(fd);
        else if (ch[0] == '3') menuSearch(fd);
        else if (ch[0] == '4') menuDelete(fd);
        else if (ch[0] == '5') break;
    }

    clearScreen(fd);
    writeString(fd, "\r\n  " CYN "Goodbye.\r\n\r\n" RST);
    close(fd);
    exit(0); //exit the child process only, parent keeps running
}

int main(void) {
    int sfd, cfd;
    struct sockaddr_in addr;
    int opt = 1;

    //create a TCP socket
    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return 1; }

    //allow reuse of the port immediately after a restart, mark as unused
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    //bind to all interfaces on PORT
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(sfd, 10) < 0) { perror("listen"); return 1; }

    printf("Listening on port %d\n", PORT);
    fflush(stdout);

    while (1) {
        cfd = accept(sfd, NULL, NULL); //block until a client connects
        if (cfd < 0) continue;
        if (fork() == 0) {
            //child, close the listening socket and handle this client
            close(sfd);
            handleClient(cfd);
        }
        //parent, close our copy of the client socket and loop back to accept
        close(cfd);
    }
}