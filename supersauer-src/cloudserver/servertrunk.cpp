#include "stdafx.h"
#include "cloud.h"

#define LOGSTRLEN 512
#define MAXCLIENTS 256                 
#define MAXTRANS 5000                  // max amount of data to swallow in 1 go

static FILE *logfile = NULL;
int curtime = 0, totalmillis = 0;
uint totalsecs = 0;

void closelogfile()
{
    if(logfile)
    {
        fclose(logfile);
        logfile = NULL;
    }
}

FILE *getlogfile()
{
#ifdef WIN32
    return logfile;
#else
    return logfile ? logfile : stdout;
#endif
}

void setlogfile(const char *fname)
{
    closelogfile();
    if(fname && fname[0])
    {
        fname = findfile(fname, "wb");
        if(fname) logfile = fopen(fname, "wb");
    }
    FILE *f = getlogfile();
    if(f) setvbuf(f, NULL, _IOLBF, BUFSIZ);
	else exit(EXIT_FAILURE);
}

static void writelog(FILE *file, const char *buf)
{
    static uchar ubuf[512];
    int len = strlen(buf), carry = 0;
    while(carry < len)
    {
        int numu = encodeutf8(ubuf, sizeof(ubuf)-1, &((const uchar *)buf)[carry], len - carry, &carry);
        if(carry >= len) ubuf[numu++] = '\n';
        fwrite(ubuf, 1, numu, file);
		fwrite(ubuf, 1, numu, stderr);
    }
}

static void writelogv(FILE *file, const char *fmt, va_list args)
{
    static char buf[LOGSTRLEN];
    vformatstring(buf, fmt, args, sizeof(buf));
    writelog(file, buf);
}

void logoutfv(const char *fmt, va_list args)
{
    FILE *f = getlogfile();
    if(f) writelogv(f, fmt, args);
}

void logoutf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    logoutfv(fmt, args);
    va_end(args);
}

void conoutfv(const char *fmt, va_list args)
{
    logoutfv(fmt, args);
}

void conoutf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    conoutfv(fmt, args);
    va_end(args);
}

void fatal(const char *fmt, ...) 
{ 
    //void cleanupserver();
    //cleanupserver(); 
	defvformatstring(msg,fmt,fmt);
	if(logfile) logoutf("%s", msg);
    fprintf(stderr, "server error: %s\n", msg);
    closelogfile();
    exit(EXIT_FAILURE); 
}

enum { ST_EMPTY, ST_TCPIP };

struct client                   // server side version of "dynent" type
{
    int num, type;
    ENetPeer *peer;
    string hostname;
};

vector<client *> clients;
int connectedclients = 0;
client &addclient(int type)
{
    client *c = NULL;
    loopv(clients) if(clients[i]->type==ST_EMPTY)
    {
        c = clients[i];
        break;
    }
    if(!c)
    {
        c = new client;
        c->num = clients.length();
        clients.add(c);
    }
    c->type = type;
	if(type == ST_TCPIP) connectedclients++;
    return *c;
}

void delclient(client *c)
{
    if(!c) return;
    if(c->type == ST_TCPIP)
    {
        connectedclients--; 
		if(c->peer) c->peer->data = NULL;
    }
    c->type = ST_EMPTY;
    c->peer = NULL;
}

ENetHost *serverhost = NULL;
int laststatus = 0; 
ENetSocket serversocket = ENET_SOCKET_NULL;

void sendpacket(ENetPeer *receiver, int chan, ENetPacket *packet, int exclude)
{
	if(!receiver) return;
    enet_peer_send(receiver, chan, packet);
}

ENetPacket *sendf(ENetPeer *receiver, int chan, const char *format, ...)
{
    int exclude = -1;
    bool reliable = false;
    if(*format=='r') { reliable = true; ++format; }
    packetbuf p(MAXTRANS, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    va_list args;
    va_start(args, format);
    while(*format) switch(*format++)
    {
        case 'x':
            exclude = va_arg(args, int);
            break;

        case 'v':
        {
            int n = va_arg(args, int);
            int *v = va_arg(args, int *);
            loopi(n) putint(p, v[i]);
            break;
        }

        case 'i': 
        {
            int n = isdigit(*format) ? *format++-'0' : 1;
            loopi(n) putint(p, va_arg(args, int));
            break;
        }
        case 'f':
        {
            int n = isdigit(*format) ? *format++-'0' : 1;
            loopi(n) putfloat(p, (float)va_arg(args, double));
            break;
        }
        case 's': sendstring(va_arg(args, const char *), p); break;
        case 'm':
        {
            int n = va_arg(args, int);
            p.put(va_arg(args, uchar *), n);
            break;
        }
    }
    va_end(args);
    ENetPacket *packet = p.finalize();
    sendpacket(receiver, chan, packet, exclude);
    return packet->referenceCount > 0 ? packet : NULL;
}

ENetPacket *sendfile(ENetPeer *receiver, int chan, stream *file, const char *format, ...)
{
    if(!receiver)
    {
		return NULL;
    }

    int len = (int)min(file->size(), stream::offset(INT_MAX));
    if(len <= 0 || len > 16<<20) return NULL;

    packetbuf p(MAXTRANS+len, ENET_PACKET_FLAG_RELIABLE);
    va_list args;
    va_start(args, format);
    while(*format) switch(*format++)
    {
        case 'i':
        {
            int n = isdigit(*format) ? *format++-'0' : 1;
            loopi(n) putint(p, va_arg(args, int));
            break;
        }
        case 's': sendstring(va_arg(args, const char *), p); break;
        case 'l': putint(p, len); break;
    }
    va_end(args);

    file->seek(0, SEEK_SET);
    file->read(p.subbuf(len).buf, len);

    ENetPacket *packet = p.finalize();
    sendpacket(receiver, chan, packet, -1);
    return packet->referenceCount > 0 ? packet : NULL;
}


void disconnect_client(int n)
{
    if(!clients.inrange(n)) return;
	cloud::disconnectuser(clients[n]->peer);
    enet_peer_disconnect(clients[n]->peer, 4);
    
    delclient(clients[n]);
}
void process(ENetPacket *packet, ENetPeer *sender, int n, int chan)   // sender may be -1
{
    packetbuf p(packet);
    cloud::parsepacket(sender, chan, p);
    if(p.overread()) { disconnect_client(n); return; }
}

void updatetime()
{
    static int lastsec = 0;
    if(totalmillis - lastsec >= 1000) 
    {
        int cursecs = (totalmillis - lastsec) / 1000;
        totalsecs += cursecs;
        lastsec += cursecs * 1000;
    }
}

void serverslice(uint timeout)   // main server update
{
    if(!serverhost) 
    {
        cloud::updatedb();
        cloud::sendpackets();
        return;
    }

    int millis = (int)enet_time_get(), elapsed = millis - totalmillis;
    static int timeerr = 0;
    int scaledtime = 100*elapsed + timeerr;
    curtime = scaledtime/100;
    timeerr = scaledtime%100;
    totalmillis = millis;
    updatetime();
 
    cloud::updatedb();
    
    if(totalmillis-laststatus>60*1000)   // display bandwidth stats, useful for server ops
    {
        laststatus = totalmillis;     
		logoutf("status: %d clients, %.1f send, %.1f rec (K/sec)", connectedclients, serverhost->totalSentData/60.0f/1024, serverhost->totalReceivedData/60.0f/1024);
        serverhost->totalSentData = serverhost->totalReceivedData = 0;
    }

    ENetEvent event;
    bool serviced = false;
    while(!serviced)
    {
        if(enet_host_check_events(serverhost, &event) <= 0)
        {
            if(enet_host_service(serverhost, &event, timeout) <= 0) break;
            serviced = true;
        }
        switch(event.type)
        {
            case ENET_EVENT_TYPE_CONNECT:
            {
                client &c = addclient(ST_TCPIP);
                c.peer = event.peer;
                c.peer->data = &c;
                char hn[1024];
                copystring(c.hostname, (enet_address_get_host_ip(&c.peer->address, hn, sizeof(hn))==0) ? hn : "unknown");
                logoutf("client connected (%s)", c.hostname);
                //int reason = cloud::clientconnect(c.num, c.peer);
               // if(reason) disconnect_client(c.num); 
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE:
            {
                client *c = (client *)event.peer->data;
				if(c) process(event.packet, c->peer, c->num, event.channelID);
                if(event.packet->referenceCount==0) enet_packet_destroy(event.packet);
				logoutf("received packet");
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: 
            {
                client *c = (client *)event.peer->data;
                if(!c) break;
                logoutf("disconnected client (%s)", c->hostname);
                cloud::disconnectuser(c->peer);
                delclient(c);
                break;
            }
            default:
                break;
        }
    }
    if(cloud::sendpackets()) enet_host_flush(serverhost);
}

void rundedicatedserver()
{
    logoutf("cloud server started, and ready...");
#ifdef WIN32
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	for(;;)
	{
	/*	MSG msg;
		while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if(msg.message == WM_QUIT) exit(EXIT_SUCCESS);
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} */
		serverslice(10);
	}
#else
    for(;;) serverslice(0);
#endif
}
int serverport = 0, serveruprate = 0, serverdownrate = 0;
#define DEFAULTPORT 13831
ENetSocket pingsocket = ENET_SOCKET_NULL;

bool setuplistenserver()
{
    ENetAddress address = { ENET_HOST_ANY, enet_uint16(serverport <= 0 ? DEFAULTPORT : serverport) };

    serverhost = enet_host_create(&address, MAXCLIENTS, 2, serverdownrate*128, serveruprate*128);
	if(!serverhost) { logoutf("could not create server host"); return false; }
    loopi(MAXCLIENTS) serverhost->peers[i].data = NULL;
	address.port++;
    serversocket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if(serversocket != ENET_SOCKET_NULL && (enet_socket_set_option(serversocket, ENET_SOCKOPT_REUSEADDR, 1) < 0 || enet_socket_bind(serversocket, &address) < 0))
    {
        enet_socket_destroy(serversocket);
        serversocket = ENET_SOCKET_NULL;
    }
	if(serversocket == ENET_SOCKET_NULL) { logoutf("could not create server socket"); return false; }
    else enet_socket_set_option(serversocket, ENET_SOCKOPT_NONBLOCK, 1);
    return true;
}

void initserver()
{   
    //execfile("server-init.cfg", false);
    setuplistenserver();
    cloud::serverinit();

    rundedicatedserver(); // never returns
}

#define MAXUSERS 128
#define DEFAULTDIR "files/"
string filedir, password;
int maxusers = MAXUSERS;

void conhelp()
{
	conoutf("\n");
	conoutf("====================================HELP MENU===================================");
	conoutf("\n");
	conoutf("available options:");
	conoutf("  -f<FILEDIRECTORY>:  Specifies a different path to the cloud space storage.");
	conoutf("                      (default: \"%s\")", DEFAULTDIR);
	conoutf("  -k<PASSWORD>:       Allow just clients that connect with this passkey.");
	conoutf("                      (default: \"\")");
	conoutf("  -p<PORT>:           Specifies a different port. (default: %d)", DEFAULTPORT);
	conoutf("  -u<UPLOADRATE>:     Limits the Uploading Connection to this bandwidth.");
	conoutf("                      In KB/s. (default: 0)");							
	conoutf("  -d<DOWNRATE>:       Limits the Downloading Connection to this bandwidth.");
	conoutf("                      In KB/s. (default: 0)");
	conoutf("  -m<MAXUSERS>:       How much Users will be able to store files in your cloud.");
	conoutf("                      (default: %d)", MAXUSERS);
	conoutf("\n");
	conoutf("--------------------------------------------------------------------------------");
	conoutf("\n");
	conoutf("example usage:");
	conoutf("\n");
	conoutf("./superserver ");
	conoutf("\nthis will start the server with default settings. (Recommended)");
	conoutf("\n");
	conoutf("./superserver -m200 -f../backup/files/ -p30000 -u6000 -d20000");
	conoutf("\nthis will start the server with a maximum of 200 users allowed, the files will be "
			"saved in \"../backup/files/\", the port to connect on is 30000 and the server uses a "
			"maximum bandwith of 6 MB/s for sending files and a maximum of 20 MB/s receiving them.");
	conoutf("\n");

	conoutf("HINT: You can use e.g. the screen-command to preserve the task from terminating when closing the terminal.");
	conoutf("\n================================================================================\n");
}
int main(int argc, char **argv)
{   
    setlogfile("superserverlog.cfg");
    if(enet_initialize()<0) fatal("Unable to initialise network module");
    atexit(enet_deinitialize);
    enet_time_set(0);
	strcpy(filedir, DEFAULTDIR);
	password[0] = '\0';
    for(int i = 1; i<argc; i++) {
		if(argv[i][0] != '-') continue;
		switch(argv[i][1])
		{
			case 'p': serverport = atoi(argv[i]+2); break;
			case 'h':	conhelp();		return EXIT_SUCCESS;
			case 'd': serverdownrate = atoi(argv[i]+2); break;
			case 'u': serveruprate = atoi(argv[i]+2); break;
			case 'f': strncpy(filedir, argv[i]+2, MAXSTRLEN); break;
			case 'm': maxusers = atoi(argv[i]+2); break;
			case 'k': strncpy(password, argv[i]+2, MAXSTRLEN); break;
		}
	}
    initserver();
    return EXIT_SUCCESS;
}