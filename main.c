#include "rcon.h"
#include "config.h"
#include "srcrcon.h"
#include "sysconfig.h"
#include "memstream.h"

#include <glib.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>
#include <err.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <ncurses.h>

#define DIMOLDSTAT	8
#define MAXUSERS	64
#define DIMUSERSTAT	6
#define MAXLINLEN	10000
#define MAXSTATLINLEN	500
#define MAXUSERSP1	MAXUSERS+1

static char *host = NULL;
static char *password = NULL;
static char *port = NULL;
static char *config = NULL;
static char *server = NULL;
static bool debug = false;
static bool jstatus = false;
static bool nowait = false;
static bool minecraft = false;
FILE *debugdev = NULL;

static GByteArray *response = NULL;
static src_rcon_t *r = NULL;

int yMax,xMax;
int ms_delay = 0;

typedef struct userdata
{
	unsigned int		userid;
	char *			name;
	char *			uniqueid;
	char *			connected;
	unsigned int		ping;
	char *			adr;
} UserDataStructure;
int numoui = 0;

UserDataStructure * users[MAXUSERSP1];
UserDataStructure * userp;
UserDataStructure user;


/*
    A_NORMAL        Normal display (no highlight)
    A_STANDOUT      Best highlighting mode of the terminal.
    A_UNDERLINE     Underlining
    A_REVERSE       Reverse video
    A_BLINK         Blinking
    A_DIM           Half bright
    A_BOLD          Extra bright or bold
    A_PROTECT       Protected mode
    A_INVIS         Invisible or blank mode
    A_ALTCHARSET    Alternate character set
    A_CHARTEXT      Bit-mask to extract a character
    COLOR_PAIR(n)   Color-pair number n

		COLOR_PAIR(n)
        COLOR_BLACK   0
        COLOR_RED     1
        COLOR_GREEN   2
        COLOR_YELLOW  3
        COLOR_BLUE    4
        COLOR_MAGENTA 5
        COLOR_CYAN    6
        COLOR_WHITE   7
 */

int start_ncurses()
{
    initscr();
    cbreak(); // prevent line buffering, get chars as soon as typed
    noecho(); // don't print chars to screen when typed
    curs_set(FALSE); // hide cursor
    timeout(ms_delay); // wait delay milliseconds for input
    getmaxyx(stdscr, yMax, xMax); // use ncurses to get screen size
	if(!has_colors()){
		printw("Termional does not have colors");
		getch();
		return (-1);
	}
	start_color();
	return (0);
}

void freeuserdata(UserDataStructure ** userdata) {
   UserDataStructure * userq;
   userq = *userdata;
   if ( userq == NULL ) return;
   if ( userq->name      != NULL ) free(userq->name);
   if ( userq->uniqueid  != NULL ) free(userq->uniqueid);
   if ( userq->connected != NULL ) free(userq->connected);
   if ( userq->adr       != NULL ) free(userq->adr);
   free(userq);
   *userdata = NULL;
   return;
}

int lastnonblank(char * str, int maxlen) {
   int i;
   if ( maxlen <= 0 ) return (0);
   if ( str == NULL ) return (0);
   for (i=maxlen-1;i<0;i--) if ( *(str+i) != ' ' ) break;
   return(i);
}

int parseuser (char * userstring, UserDataStructure ** userdata) {
   UserDataStructure * userq;
   char *idstr;
   char *namestr;
   char *uniqueidstr;
   char *connectedstr;
   char *pingstr;
   char *adrstr;
   int i;

   if ( userstring == NULL ) return (1);
   if ( userdata == NULL ) return(2);
   freeuserdata(userdata);
   *userdata = (UserDataStructure *) malloc(sizeof(UserDataStructure));
   userq = *userdata;
   
//# userid name                uniqueid            connected ping  adr
//01234567890123456789012345678901234567890123456789012345678901234567890
//          1         1         1         1         1         1         1     

   idstr = userstring + 2;
   namestr = userstring + 9;
   uniqueidstr = userstring + 29;
   connectedstr = userstring + 49;
   pingstr = userstring + 59;
   adrstr = userstring + 65;
   sscanf(idstr,"%u",&(userq->userid));
   sscanf(pingstr,"%u",&(userq->ping));
   
   i = lastnonblank(namestr,20);
   userq->name = (char *) malloc((size_t) (i+1) );
   strncpy(userq->name,namestr,i);
   *(userq->name+i) = '\000';
   
   i = lastnonblank(uniqueidstr,20);
   userq->uniqueid = (char *) malloc((size_t) (i+1) );
   strncpy(userq->uniqueid,uniqueidstr,i);
   *(userq->uniqueid+i) = '\000';
   
   i = lastnonblank(connectedstr,20);
   userq->connected = (char *) malloc((size_t) (i+1) );
   strncpy(userq->connected,connectedstr,i);
   *(userq->connected+i) = '\000';
   
   i = lastnonblank(adrstr,20);
   userq->adr = (char *) malloc((size_t) (i+1) );
   strncpy(userq->adr,adrstr,i);
   *(userq->adr+i) = '\000';
   
   return(0);
   
}

static void cleanup(void)
{
    config_free();

    src_rcon_free(r);

    free(host);
    free(password);
    free(port);
    free(config);
    free(server);

    if (response) {
        g_byte_array_free(response, TRUE);
    }
}

static void usage(void)
{
    puts("");
    puts("Usage:");
    puts(" rcon [options] command");
    puts("");
    puts("Options:");
    puts(" -c, --config     Alternate configuration file");
    puts(" -d, --debug      Debug output");
    puts(" -h, --help       This bogus");
    puts(" -H, --host       Host name or IP");
    puts(" -m, --minecraft  Minecraft mode");
    puts(" -n, --nowait     Don't wait for reply from server for commands.");
    puts(" -P, --password   RCON Password");
    puts(" -p, --port       Port or service");
    puts(" -s, --server     Use this server from config file");
    puts(" -1, --1packet    Unused, backward compability");
    puts(" -j, --status     Continuously report status to stdout");
}

static int parse_args(int ac, char **av)
{
    static struct option opts[] = {
        { "config", required_argument, 0, 'c' },
        { "debug", no_argument, 0, 'd' },
        { "help", no_argument, 0, 'h' },
        { "host", required_argument, 0, 'H' },
        { "minecraft", no_argument, 0, 'm' },
        { "nowait", no_argument, 0, 'n' },
        { "password", required_argument, 0, 'P' },
        { "port", required_argument, 0, 'p' },
        { "server", required_argument, 0, 's' },
        { "1packet", no_argument, 0, '1' },
        { "status", no_argument, 0, 'j' },
        { NULL, 0, 0, 0 }
    };

    static char const *optstr = "c:dH:hmnP:p:s:1j";

    int c = 0;

    while ((c = getopt_long(ac, av, optstr, opts, NULL)) != -1) {
        switch (c)
        {
        case 'c': free(config); config = strdup(optarg); break;
        case 'd': debug = true; break;
        case 'H': free(host); host = strdup(optarg); break;
        case 'm': minecraft = true; break;
        case 'p': free(port); port = strdup(optarg); break;
        case 'P': free(password); password = strdup(optarg); break;
        case 's': free(server); server = strdup(optarg); break;
        case 'n': nowait = true; break;
        case '1': /* backward compability */ break;
        case 'h': usage(); exit(0); break;
        case 'j': jstatus = true; break;
        default: /* intentional */
        case '?': usage(); exit(1); break;
        }
    }

    return 0;
}

static void debug_dump(bool in, uint8_t const *data, size_t sz)
{
    size_t i = 0;

    if (!debug) {
        return;
    }

    printf("%s ", (in ? ">>" : "<<"));

    for (i = 0; i < sz; i++) {
        if (isprint((int)data[i])) {
            fputc((int)data[i], stdout);
        } else {
            printf("0x%.2X", (int)data[i]);
        }
        if (i < sz-1) {
            printf(",");
        }
    }

    printf("\n");
}

static int send_message(int sock, src_rcon_message_t *msg)
{
    uint8_t *data = NULL;
    uint8_t *p = NULL;
    size_t size = 0;
    int ret = 0;

    if (src_rcon_serialize(r, msg, &data, &size)) {
        return -1;
    }

    debug_dump(false, data, size);

    p = data;
    do {
        ret = write(sock, p, size);
        if (ret == 0 || ret < 0) {
            free(data);
            fprintf(stderr, "Failed to communicate: %s\n", strerror(errno));
            return -2;
        }

        p += ret;
        size -= ret;
    } while (size > 0);

    free(data);

    return 0;
}

static int wait_auth(int sock, src_rcon_message_t *auth)
{
    uint8_t tmp[512];
    int ret = 0;
    rcon_error_t status;
    size_t off = 0;

    do {
        ret = read(sock, tmp, sizeof(tmp));
        if (ret < 0) {
            fprintf(stderr, "Failed to receive data: %s\n", strerror(errno));
            return -1;
        }

        debug_dump(true, tmp, ret);

        g_byte_array_append(response, tmp, ret);

        status = src_rcon_auth_wait(r, auth, &off,
                                    response->data, response->len
            );
        if (status != rcon_error_moredata) {
            g_byte_array_remove_range(response, 0, off);
            return (int)status;
        }
    } while (true);

    return 1;
}

static int send_command(int sock, char const *cmd, int typeout, char * reply)
{
    src_rcon_message_t *command = NULL, *end = NULL;
    src_rcon_message_t **commandanswers = NULL;
    src_rcon_message_t **p = NULL;
    uint8_t tmp[512];
    int ret = 0;
    rcon_error_t status;
    size_t off = 0;
    int ec = -1;
    bool done = false;

    /* Send command
     */
    command = src_rcon_command(r, cmd);
    if (command == NULL) {
        goto cleanup;
    }

    if (send_message(sock, command)) {
        goto cleanup;
    }

    if (nowait == true) {
        goto cleanup;
    }

    if (!minecraft) {
        /* minecraft does not like the empty command at the end.
         * it will abort the connection if it finds an empty command
         * and we get no answer back.
         */
        end = src_rcon_command(r, "");
        if (end == NULL) {
            goto cleanup;
        }
        if (send_message(sock, end)) {
            goto cleanup;
        }
    }

    do {
        ret = read(sock, tmp, sizeof(tmp));
        if (ret < 0) {
            fprintf(stderr, "Failed to receive data: %s\n", strerror(errno));
            return -1;
        }

        if (ret == 0) {
            fprintf(stderr, "Peer: connection closed\n");
            done = true;
        }

        g_byte_array_append(response, tmp, ret);
        status = src_rcon_command_wait(r, command, &commandanswers, &off,
                                       response->data, response->len
            );
        if (status != rcon_error_moredata) {
            g_byte_array_remove_range(response, 0, off);
        }

        if (status == rcon_error_success) {
            if (commandanswers != NULL) {
                for (p = commandanswers; *p != NULL; p++) {
                    if (!minecraft && (*p)->id == end->id) {
                        done = true;
                    } else {
                        size_t bodylen = strlen((char const*)(*p)->body);
			if (!typeout) {

                        	fprintf(stdout, "%s", (char const*)(*p)->body);

	                        if (bodylen > 0 && (*p)->body[bodylen-1] != '\n') {
        	                    fprintf(stdout, "\n");
                	        }
			} else {
                                sprintf(reply, "%s", (char const*)(*p)->body);

                                if (bodylen > 0 && (*p)->body[bodylen-1] != '\n') {
                                    sprintf(reply, "\n");
                                }
			}

                        /* in minecraft mode we are done after the first message
                         */
                        if (minecraft) {
                            done = true;
                        }
                    }
                }
            }
        }

        src_rcon_message_freev(commandanswers);
        commandanswers = NULL;
    } while (!done);

    ec = 0;

cleanup:

    src_rcon_message_free(command);
    src_rcon_message_free(end);
    src_rcon_message_freev(commandanswers);

    return ec;
}

static int handle_arguments(int sock, int ac, char **av)
{
    char *c = NULL;
    size_t size = 0;
    FILE *cmd = NULL;
    int i = 0;

    cmd = open_memstream(&c, &size);
    if (cmd == NULL) {
        return -1;
    }

    for (i = 0; i < ac; i++) {
        if (i > 0) {
            fputc(' ', cmd);
        }
        fprintf(cmd, "%s", av[i]);
    }
    fclose(cmd);

    if (send_command(sock, c, 0, NULL)) {
        free(c);
        return -1;
    }

    free(c);

    return 0;
}

static int handle_stdin(int sock)
{
    char *line = NULL;
    size_t sz = 0;
    int read = 0;
    int ec = 0;

    while ((read = getline(&line, &sz, stdin)) != -1) {
        char *cmd = line;

        /* Strip away \n
         */
        line[read-1] = '\0';

        while (*cmd != '\0' && isspace(*cmd)) {
            ++cmd;
        }

        /* Comment or empty line
         */
        if (cmd[0] == '\0' || cmd[0] == '#') {
            continue;
        }

       /* line containing only "q"
         */
        if (strcmp(cmd,"q") == 0) {
	    ec = 1;
	    break;
        }

        if (send_command(sock, cmd, 0, NULL)) {
            ec = -1;
            break;
        }
    }

    free(line);

    return ec;
}

static int num_common(char * str1, char * str2) {
	int i;
	int len1,len2;
	len1 = strlen(str1);
	len2 = strlen(str2);
	if (len2 < len1) len1 = len2;
	for (i=0;i<len1;i++) if ( *(str1+i) != *(str2+i) ) return (i);
	return(len1);
}

static int handle_status(int sock)
{
    enum STAT2STATE {
	INITIAL,
	WAITING,
	SELUSER,
        KICKBAN,
	CHANGELEVEL
    } stat2state = WAITING;
    enum STAT2STATE oldstat2state = INITIAL;
    char linestatus[] = "status";
    char *oldstatlines[DIMOLDSTAT];
    char *oldmaps[DIMOLDSTAT];
    int lenoldstatusline = 0;
    char oldstatusline[MAXSTATLINLEN];
    char *oldusers[MAXUSERSP1];
    int ec = 0;
    time_t now;
    int i,j;
    char line[MAXLINLEN];
    char templine[MAXLINLEN];
    char statusline[MAXSTATLINLEN];
    char * pch;
    char * pch2;
    int icount;
    int nummaps = 0;
    int maxmapnamelen = 0;
    int iuser;
    int c;
    int highlight = 0;

    char *cmd = linestatus;
    if(start_ncurses()) return -1;
    init_pair(1,COLOR_RED, COLOR_WHITE);
    attr_t emphasis = A_REVERSE | COLOR_PAIR(1);
    for (i=0;i<xMax;i++) statusline[i] = ' ';
    statusline[xMax] = '\000';
    oldstatusline[0] = '\000';
    for (i=0;i<DIMOLDSTAT;i++){
	oldstatlines[i] = (char *) calloc(1,1);
	if ( oldstatlines[i] == NULL ) {
		fprintf(stderr,"calloc failed\n");
		exit(2);
	}
	oldmaps[i] = (char *) calloc(1,1);
	if ( oldmaps[i] == NULL ) {
		fprintf(stderr,"calloc failed\n");
		exit(3);
	}
    }
    for (i=0;i<MAXUSERS;i++) oldusers[i] = (char *) calloc(1,1);
//    for (i=0;i<MAXUSERS;i++) for(j=0;j<DIMUSERSTAT;j++) olduserdata[i][j] = (char *) calloc(1,1);
    WINDOW * statwin = newwin(1,xMax,0,0);
    wattron(statwin,emphasis);
    WINDOW * statwin2 = newwin(1,xMax,yMax-1,0);
    wattron(statwin2,emphasis);
    WINDOW * inputwin = newwin(yMax-3, xMax,1, 1);
    keypad(inputwin,true);
    wtimeout(inputwin,0);
    refresh();
    int oldcount = 0;
    while (1) {
	/* initialize status lines */
	if (!lenoldstatusline) {
		for (i=0;i<xMax;i++) statusline[i] = ' ';
		statusline[i+1] = '\000';
		mvwprintw(statwin,0,0,statusline);
		wnoutrefresh(statwin);
	}
	if (stat2state != oldstat2state) {
//		for (i=0;i<xMax;i++) statusline[i] = ' ';
//		statusline[i+1] = '\000';
//		mvwprintw(statwin2,0,0,statusline);
		mvwprintw(statwin2,0,0,"%*s",xMax," ");
		wnoutrefresh(statwin2);
		switch(stat2state)
		{
		case WAITING:
			mvwprintw(statwin2,0,0,"%s"," F1 -> Select User, F2 -> Change Level, q -> End Program");
			break;
		case SELUSER:
			mvwprintw(statwin2,0,0,"%s"," ^v Arrows -> Highlight User, Space -> Select User, <ESC> -> Return");
			break;
		case KICKBAN:
			mvwprintw(statwin2,0,0,"%s"," F3 -> Kick User, F4 -> Permantly Ban User, <ESC> -> Return");
			break;
		case CHANGELEVEL:
			mvwprintw(statwin2,0,0,"%s"," ^v Arrows -> Highlight Level, Space -> Select Level, <ESC> -> Return");
			break;
		default:
			break;
		}
		wnoutrefresh(statwin2);
		oldstat2state = stat2state;
	}
	/* print time hack */
    	/* Obtain current time */
    	time(&now);
    	/* Convert to local time format and print to status line */
	sprintf(statusline," %s", ctime(&now));
	i = strlen(statusline);
	statusline[i-1] = '\000';
	j = num_common(statusline, oldstatusline);
	mvwprintw(statwin,0,j,statusline+j);
	wnoutrefresh(statwin);
	lenoldstatusline = i;
	strcpy( oldstatusline , statusline);
//	attroff(emphasis);
//        WINDOW * inputwin = newwin(yMax-2, xMax-2,1, 1);
//      box(inputwin,0,0);
//      wrefresh(inputwin);
//        idcok(inputwin,true);
//        scrollok(inputwin,true);
//        scroll(inputwin);
//        wrefresh(inputwin);
	clearok(inputwin,false);

        if (send_command(sock, cmd, 1 , line)) {
            ec = -1;
            break;
        }
	if (debug) {
//	    rewind(debugdev);
	    if (fwrite((const void*)line,(size_t)1,(size_t)(strlen(line)+1),debugdev) != (size_t)(strlen(line)+1) ) {
		fprintf(stderr,"debug write to file failed!\n");
		exit(1);
	    }
	}
	pch = strtok (line,"\n");
//	wmove(inputwin,1,0);
	icount = 0;
	iuser = 0;
	while (pch != NULL)
	{
		if (strcmp(pch,oldstatlines[icount])) mvwprintw(inputwin,icount,0,"%s",pch);
		if (strlen(pch) != strlen(oldstatlines[icount]) ) {
			if ( oldstatlines[icount] != NULL) free(oldstatlines[icount]);
			oldstatlines[icount] = (char *) malloc(strlen(pch)+1);
		        if ( oldstatlines[icount] == NULL ) {
         			fprintf(stderr,"malloc failed\n");
		                exit(2);
		        }
		}
		strcpy(oldstatlines[icount],pch); 
		pch = strtok (NULL, "\n");
		if (icount++ > 6) break;
	}	
	while (pch != NULL) {
		strcpy(templine,pch);
		strcpy(templine+64,templine+75);
		if ( icount > 7 ) {
			pch2=strrchr(templine,':');
			if ( pch2 != NULL ) *pch2 = '\000';
		}
		if( strlen(templine) > xMax ) *(templine+xMax) = '\000';
		if ( strcmp(templine,oldusers[iuser]) ) {
			mvwprintw(inputwin,icount,0,"%*s",xMax-1," ");
			mvwprintw(inputwin,icount,0,"%s",templine);	
		}
//		mvwprintw(inputwin,icount,0,"%s\n",pch);
                if (strlen(templine) != strlen(oldusers[iuser]) ) {
                        if ( oldusers[iuser] != NULL) free(oldusers[iuser]);
                        oldusers[iuser] = (char *) malloc(strlen(templine)+1);
                        if ( oldusers[iuser] == NULL ) {
                                fprintf(stderr,"malloc failed\n");
                                exit(2);
                        }
                }
                strcpy(oldusers[iuser],templine);
		parseuser (oldusers[iuser], &(users[iuser]));
		iuser++;
		pch = strtok (NULL, "\n");
		icount++;
	}
	if ( oldcount > icount ) for(i=icount;i<oldcount;i++) mvwprintw(inputwin,i,0,"%*s",xMax-1," ");
	oldcount = icount;
//	wrefresh(inputwin);
	wnoutrefresh(inputwin);
	doupdate();
//	refresh();
	c = wgetch(inputwin);
	if ( c == 'q' && stat2state == WAITING ) break;
	if ( c == KEY_F(1) && stat2state == WAITING && iuser ) stat2state = SELUSER;
	if ( c == KEY_F(2) && stat2state == WAITING ) stat2state = CHANGELEVEL;
	if ( c == KEY_F(3) && stat2state == KICKBAN && highlight ) {
		sprintf(templine,"kickid %u",(*(users+highlight))->userid);
		if (send_command(sock, templine, 1 , line)) {
	            ec = -1;
        	    break;
	        }

		stat2state = WAITING;
	}
	if ( stat2state == CHANGELEVEL ) {
                sprintf(templine,"maps *");
                if (send_command(sock, templine, 1 , line)) {
                    ec = -1;
                    break;
                }
	        if (debug) {
        	    rewind(debugdev);
	            if (fwrite((const void*)line,(size_t)1,(size_t)(strlen(line)+1),debugdev) != (size_t)(strlen(line)+1) ) {
        	        fprintf(stderr,"debug write to file failed!\n");
	                exit(1);
        	    }
	        }
	        pch = strtok (line,"\n");
	        nummaps = 0;
		maxmapnamelen = 0;
		i = 0;
	        while (pch != NULL)
	        {
			i++;
			if (i != 1) {
		                if (strlen(pch+16) != strlen(oldmaps[nummaps]) ) {
        		                if ( oldmaps[nummaps] != NULL) free(oldmaps[nummaps]);
                		        oldmaps[nummaps] = (char *) malloc(strlen(pch+16)+1);
	                	        if ( oldmaps[nummaps] == NULL ) {
        	                	        fprintf(stderr,"malloc failed\n");
	                	                exit(2);
		                        }
        		        }
		                strcpy(oldmaps[nummaps],pch+16);
				if (strlen(oldmaps[nummaps]) > maxmapnamelen) maxmapnamelen = strlen(oldmaps[nummaps]);
	        	        nummaps++;
			}
        	        pch = strtok (NULL, "\n");
	        }
		WINDOW * changelevwin = newwin(nummaps+1,maxmapnamelen+2,6,10);
		keypad(changelevwin,true);
		wtimeout(changelevwin,0);
	        wnoutrefresh(changelevwin);
	        doupdate();
		box(changelevwin,0,0);
                highlight = 1;
                keypad(changelevwin,true);
                while(1)
                {
                        for(i=1;i<nummaps;i++)
                        {
                                if(i==highlight) wattron(changelevwin, A_REVERSE);
                                mvwprintw(changelevwin,i,1,oldmaps[i]);
                                wattroff(changelevwin, A_REVERSE);
                                wrefresh(changelevwin);
                        }
                        c = wgetch(changelevwin);
                        switch(c)
                        {
                        case KEY_UP:
                                highlight--;
                                if(highlight == -1) highlight = 0;
                                break;
                        case KEY_DOWN:
                                highlight++;
                                if(highlight == nummaps) highlight = nummaps - 1;
                                break;
                        default:
                                break;
                        }
                        if(c == 10) break;
                }
                sprintf(templine,"changelevel %s",oldmaps[highlight]);
                if (send_command(sock, templine, 1 , line)) {
                    ec = -1;
                    break;
                }
		stat2state = WAITING;
		wnoutrefresh(changelevwin);
		doupdate();
		highlight = 0;
		touchwin(inputwin);
	}
	if ( stat2state == SELUSER ) {
		highlight = 1;
		keypad(inputwin,true);
		while(1)
		{
			for(i=1;i<=iuser;i++)
			{
				if(i==highlight) wattron(inputwin, A_REVERSE);
				mvwprintw(inputwin,i+8,0,oldusers[i]);
				wattroff(inputwin, A_REVERSE);
				wrefresh(inputwin);
			}
			c = wgetch(inputwin);
			switch(c)
			{
			case KEY_UP:
				highlight--;
				if(highlight == -1) highlight = 0;
				break;
			case KEY_DOWN:
				highlight++;
				if(highlight == iuser) highlight = iuser - 1;
				break;
			default:
				break;
			}
			if(c == 10) break;
		}
		stat2state = KICKBAN;
	}
	if ( stat2state == WAITING ) sleep(3);
    }

    clear();
    endwin();
//   todo : free all of your mallocs

    return ec;
}

int do_config(void)
{
    if (server == NULL) {
        return 0;
    }

    if (config == NULL) {
        char const *home = getenv("HOME");
        size_t sz = 0;

        if (home == NULL) {
            fprintf(stderr, "Neither config file nor $HOME is set\n");
            return 4;
        }

        sz = strlen(home) + 10;
        config = calloc(1, sz);
        if (config == NULL) {
            return 4;
        }

        g_strlcpy(config, getenv("HOME"), sz);
        g_strlcat(config, "/.rconrc", sz);
    }

    if (config_load(config)) {
        return 2;
    }

    free(host);
    free(port);
    free(password);

    if (config_host_data(server, &host, &port, &password, &minecraft)) {
        fprintf(stderr, "Server %s not found in configuration\n", server);
        return 2;
    }

    return 0;
}

int main(int ac, char **av)
{
    struct addrinfo *info = NULL, *ai = NULL, hint = {0};
    struct sockaddr_in *addr;
    src_rcon_message_t *auth = NULL;
    src_rcon_message_t **authanswers = NULL;
    int sock = 0;
    int ret = 0;
    int ec = 3;
    int i;

    for (i=0;i<MAXUSERSP1;i++) users[MAXUSERSP1] = NULL;

#ifdef HAVE_PLEDGE
    /* stdio = standard IO and send/recv
     * rpath = config file
     * inet = dns = :-)
     */
    if (pledge("stdio rpath inet dns", NULL) == -1) {
        err(1, "pledge");
    }
#endif

    atexit(cleanup);

    parse_args(ac, av);
    if (do_config()) {
        return 2;
    }
    /* Now parse arguments *again*. This allows for overrides on the command
     * line.
     */
    optind = 1;
    parse_args(ac, av);


    ac -= optind;
    av += optind;

    if (host == NULL) {
        fprintf(stderr, "No host specified\n");
        return 1;
    }

    if (port == NULL) {
        fprintf(stderr, "No port specified\n");
        return 1;
    }
   
    if (debug) {
	debugdev = fopen("debug.txt","wb");
	if(debugdev == NULL)
	{
	   fprintf(stderr,"Unable to open debug output file!\n");   
	   exit(1);             
	}
    }

    memset(&hint, 0, sizeof(hint));
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_family = AF_INET;
    hint.ai_flags = AI_PASSIVE;

    if ((ret = getaddrinfo(host, port, &hint, &info))) {
        fprintf(stderr, "Failed to resolve host: %s: %s\n",
                host, gai_strerror(ret)
            );
        goto cleanup;
    }

    for (ai = info; ai != NULL; ai = ai->ai_next ) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0) {
	    printf ("Error opening socket: %s\n",strerror(errno));
            continue;
        }

	addr = (struct sockaddr_in *)ai->ai_addr; 
	printf("Connecting to: %s\n",inet_ntoa((struct in_addr)addr->sin_addr));

        if (connect(sock, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        } else {
	    printf ("Error connecting to socket: %s\n",strerror(errno));
	}

        close(sock);
        sock = -1;
    }

    if (sock < 0) {
        fprintf(stderr, "Failed to connect to the given host/port\n");
        goto cleanup;
    }

#ifdef HAVE_PLEDGE
    /* Drop privileges further, since we are done socket()ing.
     */
    if (pledge("stdio", NULL) == -1) {
        err(1, "pledge");
    }
#endif

    response = g_byte_array_new();
    r = src_rcon_new();

    /* Do we have a password?
     */
    if (password != NULL && strlen(password) > 0) {
        /* Send auth request first
         */
        auth = src_rcon_auth(r, password);

        if (send_message(sock, auth)) {
            goto cleanup;
        }

        if (wait_auth(sock, auth)) {
            fprintf(stderr, "Invalid auth reply, valid password?\n");
            goto cleanup;
        }
    }

    printf ("Authorization successful...\n");

    if (ac > 0) {
        if (handle_arguments(sock, ac, av)) {
            goto cleanup;
        }
    } else if (jstatus) {
        if (handle_status(sock)) {
            goto cleanup;
        }
    } else {
        if (handle_stdin(sock)) {
            goto cleanup;
        }
    }

    ec = 0;

cleanup:

    src_rcon_message_free(auth);
    src_rcon_message_freev(authanswers);

    if (sock > -1) {
        close(sock);
    }

    if (info) {
        freeaddrinfo(info);
    }

    if (debug) fclose(debugdev);

    return ec;
}
