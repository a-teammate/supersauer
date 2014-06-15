// serverbrowser.cpp: eihrul's concurrent resolver, and server browser window management

#include "engine.h"
#include "SDL_thread.h"

struct resolverthread
{
    SDL_Thread *thread;
    const char *query;
    int starttime;
};

struct resolverresult
{
    const char *query;
    ENetAddress address;
};
XVARP(su_serverbrowser, 0, 1, 1); 
XVARP(su_serverbrowserfavs, 0, 1, 1);

vector<resolverthread> resolverthreads;
vector<const char *> resolverqueries;
vector<resolverresult> resolverresults;
SDL_mutex *resolvermutex;
SDL_cond *querycond, *resultcond;

#define RESOLVERTHREADS 2
#define RESOLVERLIMIT 3000

int resolverloop(void * data)
{
    resolverthread *rt = (resolverthread *)data;
    SDL_LockMutex(resolvermutex);
    SDL_Thread *thread = rt->thread;
    SDL_UnlockMutex(resolvermutex);
    if(!thread || SDL_GetThreadID(thread) != SDL_ThreadID())
        return 0;
    while(thread == rt->thread)
    {
        SDL_LockMutex(resolvermutex);
        while(resolverqueries.empty()) SDL_CondWait(querycond, resolvermutex);
        rt->query = resolverqueries.pop();
        rt->starttime = totalmillis;
        SDL_UnlockMutex(resolvermutex);

        ENetAddress address = { ENET_HOST_ANY, ENET_PORT_ANY };
        enet_address_set_host(&address, rt->query);

        SDL_LockMutex(resolvermutex);
        if(rt->query && thread == rt->thread)
        {
            resolverresult &rr = resolverresults.add();
            rr.query = rt->query;
            rr.address = address;
            rt->query = NULL;
            rt->starttime = 0;
            SDL_CondSignal(resultcond);
        }
        SDL_UnlockMutex(resolvermutex);
    }
    return 0;
}

void resolverinit()
{
    resolvermutex = SDL_CreateMutex();
    querycond = SDL_CreateCond();
    resultcond = SDL_CreateCond();

    SDL_LockMutex(resolvermutex);
    loopi(RESOLVERTHREADS)
    {
        resolverthread &rt = resolverthreads.add();
        rt.query = NULL;
        rt.starttime = 0;
        rt.thread = SDL_CreateThread(resolverloop, &rt);
    }
    SDL_UnlockMutex(resolvermutex);
}

void resolverstop(resolverthread &rt)
{
    SDL_LockMutex(resolvermutex);
    if(rt.query)
    {
#ifndef __APPLE__
        SDL_KillThread(rt.thread);
#endif
        rt.thread = SDL_CreateThread(resolverloop, &rt);
    }
    rt.query = NULL;
    rt.starttime = 0;
    SDL_UnlockMutex(resolvermutex);
} 

void resolverclear()
{
    if(resolverthreads.empty()) return;

    SDL_LockMutex(resolvermutex);
    resolverqueries.shrink(0);
    resolverresults.shrink(0);
    loopv(resolverthreads)
    {
        resolverthread &rt = resolverthreads[i];
        resolverstop(rt);
    }
    SDL_UnlockMutex(resolvermutex);
}

void resolverquery(const char *name)
{
    if(resolverthreads.empty()) resolverinit();

    SDL_LockMutex(resolvermutex);
    resolverqueries.add(name);
    SDL_CondSignal(querycond);
    SDL_UnlockMutex(resolvermutex);
}

bool resolvercheck(const char **name, ENetAddress *address)
{
    bool resolved = false;
    SDL_LockMutex(resolvermutex);
    if(!resolverresults.empty())
    {
        resolverresult &rr = resolverresults.pop();
        *name = rr.query;
        address->host = rr.address.host;
        resolved = true;
    }
    else loopv(resolverthreads)
    {
        resolverthread &rt = resolverthreads[i];
        if(rt.query && totalmillis - rt.starttime > RESOLVERLIMIT)        
        {
            resolverstop(rt);
            *name = rt.query;
            resolved = true;
        }    
    }
    SDL_UnlockMutex(resolvermutex);
    return resolved;
}

bool resolverwait(const char *name, ENetAddress *address)
{
    if(resolverthreads.empty()) resolverinit();

    defformatstring(text)("resolving %s... (esc to abort)", name);
    renderprogress(0, text);

    SDL_LockMutex(resolvermutex);
    resolverqueries.add(name);
    SDL_CondSignal(querycond);
    int starttime = SDL_GetTicks(), timeout = 0;
    bool resolved = false;
    for(;;) 
    {
        SDL_CondWaitTimeout(resultcond, resolvermutex, 250);
        loopv(resolverresults) if(resolverresults[i].query == name) 
        {
            address->host = resolverresults[i].address.host;
            resolverresults.remove(i);
            resolved = true;
            break;
        }
        if(resolved) break;
    
        timeout = SDL_GetTicks() - starttime;
        renderprogress(min(float(timeout)/RESOLVERLIMIT, 1.0f), text);
        if(interceptkey(SDLK_ESCAPE)) timeout = RESOLVERLIMIT + 1;
        if(timeout > RESOLVERLIMIT) break;    
    }
    if(!resolved && timeout > RESOLVERLIMIT)
    {
        loopv(resolverthreads)
        {
            resolverthread &rt = resolverthreads[i];
            if(rt.query == name) { resolverstop(rt); break; }
        }
    }
    SDL_UnlockMutex(resolvermutex);
    return resolved;
}

#define CONNLIMIT 20000

int connectwithtimeout(ENetSocket sock, const char *hostname, const ENetAddress &address)
{
    defformatstring(text)("connecting to %s... (esc to abort)", hostname);
    renderprogress(0, text);

    ENetSocketSet readset, writeset;
    if(!enet_socket_connect(sock, &address)) for(int starttime = SDL_GetTicks(), timeout = 0; timeout <= CONNLIMIT;)
    {
        ENET_SOCKETSET_EMPTY(readset);
        ENET_SOCKETSET_EMPTY(writeset);
        ENET_SOCKETSET_ADD(readset, sock);
        ENET_SOCKETSET_ADD(writeset, sock);
        int result = enet_socketset_select(sock, &readset, &writeset, 250);
        if(result < 0) break;
        else if(result > 0)
        {
            if(ENET_SOCKETSET_CHECK(readset, sock) || ENET_SOCKETSET_CHECK(writeset, sock))
            {
                int error = 0;
                if(enet_socket_get_option(sock, ENET_SOCKOPT_ERROR, &error) < 0 || error) break;
                return 0;
            }
        }
        timeout = SDL_GetTicks() - starttime;
        renderprogress(min(float(timeout)/CONNLIMIT, 1.0f), text);
        if(interceptkey(SDLK_ESCAPE)) break;
    }

    return -1;
}
 
enum { UNRESOLVED = 0, RESOLVING, RESOLVED };

vector<serverinfo *> servers;
ENetSocket pingsock = ENET_SOCKET_NULL;
int lastinfo = 0;

static serverinfo *newserver(const char *name, int port, uint ip = ENET_HOST_ANY)
{
    serverinfo *si = new serverinfo;
    si->address.host = ip;
    si->address.port = server::serverinfoport(port);
    if(ip!=ENET_HOST_ANY) si->resolved = RESOLVED;

    si->port = port;
    if(name) copystring(si->name, name);
    else if(ip==ENET_HOST_ANY || enet_address_get_host_ip(&si->address, si->name, sizeof(si->name)) < 0)
    {
        delete si;
        return NULL;

    }

    servers.add(si);

    return si;
}

void addserver(const char *name, int port, const char *password, bool keep)
{
    if(port <= 0) port = server::serverport();
    loopv(servers)
    {
        serverinfo *s = servers[i];
        if(strcmp(s->name, name) || s->port != port) continue;
        if(password && (!s->password || strcmp(s->password, password)))
        {
            DELETEA(s->password);
            s->password = newstring(password);
        }
        if(keep && !s->keep) s->keep = true;
        return;
    }
    serverinfo *s = newserver(name, port);
    if(!s) return;
    if(password) s->password = newstring(password);
    s->keep = keep;
}

VARP(searchlan, 0, 0, 1);
VARP(servpingrate, 1000, 5000, 60000);
VARP(servpingdecay, 1000, 15000, 60000);
VARP(maxservpings, 0, 10, 1000);

void pingservers()
{
    if(pingsock == ENET_SOCKET_NULL) 
    {
        pingsock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        if(pingsock == ENET_SOCKET_NULL)
        {
            lastinfo = totalmillis;
            return;
        }
        enet_socket_set_option(pingsock, ENET_SOCKOPT_NONBLOCK, 1);
        enet_socket_set_option(pingsock, ENET_SOCKOPT_BROADCAST, 1);
    }
    ENetBuffer buf;
    uchar ping[MAXTRANS];
    ucharbuf p(ping, sizeof(ping));
    putint(p, totalmillis ? totalmillis : 1);

    static int lastping = 0;
    if(lastping >= servers.length()) lastping = 0;
    loopi(maxservpings ? min(servers.length(), maxservpings) : servers.length())
    {
        serverinfo &si = *servers[lastping];
        if(++lastping >= servers.length()) lastping = 0;
        if(si.address.host == ENET_HOST_ANY) continue;
        buf.data = ping;
        buf.dataLength = p.length();
        enet_socket_send(pingsock, &si.address, &buf, 1);
        if(su_serverbrowser)  preview::extinforequest(pingsock, si.address, &si); // 
        
        si.checkdecay(servpingdecay);
    }
    if(searchlan)
    {
        ENetAddress address;
        address.host = ENET_HOST_BROADCAST;
        address.port = server::laninfoport();
        buf.data = ping;
        buf.dataLength = p.length();
        enet_socket_send(pingsock, &address, &buf, 1);
    }
    lastinfo = totalmillis;
}
  
void checkresolver()
{
    int resolving = 0;
    loopv(servers)
    {
        serverinfo &si = *servers[i];
        if(si.resolved == RESOLVED) continue;
        if(si.address.host == ENET_HOST_ANY)
        {
            if(si.resolved == UNRESOLVED) { si.resolved = RESOLVING; resolverquery(si.name); }
            resolving++;
        }
    }
    if(!resolving) return;

    const char *name = NULL;
    for(;;)
    {
        ENetAddress addr = { ENET_HOST_ANY, ENET_PORT_ANY };
        if(!resolvercheck(&name, &addr)) break;
        loopv(servers)
        {
            serverinfo &si = *servers[i];
            if(name == si.name)
            {
                si.resolved = RESOLVED; 
                si.address.host = addr.host;
                break;
            }
        }
    }
}

static int lastreset = 0;

void checkpings()
{
    if(pingsock==ENET_SOCKET_NULL) return;
    enet_uint32 events = ENET_SOCKET_WAIT_RECEIVE;
    ENetBuffer buf;
    ENetAddress addr;
    uchar ping[MAXTRANS];
    char text[MAXTRANS];
    buf.data = ping; 
    buf.dataLength = sizeof(ping);
    while(enet_socket_wait(pingsock, &events, 0) >= 0 && events)
    {
        int len = enet_socket_receive(pingsock, &addr, &buf, 1);
        if(len <= 0) return;  
        serverinfo *si = NULL;
        loopv(servers) if(addr.host == servers[i]->address.host && addr.port == servers[i]->address.port) { si = servers[i]; break; }
        if(!si && searchlan) si = newserver(NULL, server::serverport(addr.port), addr.host); 
        if(!si /*|| !si->limitpong()*/) continue;
        ucharbuf p(ping, len);
        if(su_serverbrowser) { 
            if(preview::extinfoparse(p,si)) continue;
            p.len = 0;
        }
        int millis = getint(p), rtt = clamp(totalmillis - millis, 0, min(servpingdecay, totalmillis));
        if(millis >= lastreset && rtt < servpingdecay) si->addping(rtt, millis);
        si->numplayers = getint(p);
        int numattr = getint(p);
        si->attr.setsize(0);
        loopj(numattr) { int attr = getint(p); if(p.overread()) break; si->attr.add(attr); }
        getstring(text, p);
        filtertext(si->map, text, false);
        getstring(text, p);
        filtertext(si->sdesc, text);
    }
}

//////////super serverbrowser/////////
enum {	SPING = 1 ,	SPLAYER, SMODE, SMAP, STIME, SMASTER, SHOST, SPORT, SDESC	}; 
namespace game {
    int sortbythat = 0;
    bool upsidedown = false;
}
static bool sicompare(serverinfo *a, serverinfo *b)
{
    bool ac = server::servercompatible(a->name, a->sdesc, a->map, a->ping, a->attr, a->numplayers),
        bc = server::servercompatible(b->name, b->sdesc, b->map, b->ping, b->attr, b->numplayers);
    if(ac > bc) return true;
    if(bc > ac) return false;
    //    if(a->keep > b->keep) return true;
    //    if(a->keep < b->keep) return false;
    switch(game::sortbythat) 
    {
    case SPING:
        if(a->ping > b->ping) return game::upsidedown ? true : false;
        if(a->ping < b->ping) return game::upsidedown ? false : true;
        break;
    case SPLAYER:
        if(a->numplayers < b->numplayers) return game::upsidedown ? true : false;
        if(a->numplayers > b->numplayers) return game::upsidedown ? false : true;
        break;
    case SMODE:
        if(a->attr.length()<5) return game::upsidedown ? false :true;
        if(b->attr.length()<5) return game::upsidedown ? true :false;
        if(a->attr[1] < b->attr[1]) return game::upsidedown ? false :true;
        if(a->attr[1] > b->attr[1]) return game::upsidedown ? true :false;
        break;
    case SMAP:	{
        int cmp = strcmp(a->map, b->map);
        if(cmp != 0) return !game::upsidedown ? (cmp<0) : (cmp>0);
        break;
                }
    case STIME:
        if(!ac && !bc) break;
        if(a->attr.length()<3) return game::upsidedown ? false : true;
        if(a->attr[2] < 0) return game::upsidedown ? false : true;
        if(b->attr.length()<3) return game::upsidedown ? true : false;
        if(b->attr[2] < 0) return game::upsidedown ? true : false;
        if(a->attr[2] < b->attr[2]) return game::upsidedown ? true : false;
        if(a->attr[2] > b->attr[2]) return game::upsidedown ? false : true;
        break;
    case SMASTER:
        if(a->attr.length()<5) return game::upsidedown ? false :true;
        if(b->attr.length()<5) return game::upsidedown ? true :false;
        if(a->attr[4] < b->attr[4]) return game::upsidedown ? false :true;
        if(a->attr[4] > b->attr[4]) return game::upsidedown ? true :false;
        break;
    case SHOST: {
        int cmp = strcmp(a->name, b->name);
        if(cmp != 0) return !game::upsidedown ? (cmp<0) : (cmp>0);
        break;
                }
    case SPORT:
        if(a->port < b->port) return game::upsidedown ? false : true;
        if(a->port > b->port) return game::upsidedown ? true : false;
        break;
    case SDESC: {
        int cmp = strcmp(a->sdesc, b->sdesc);
        if(cmp != 0) return !game::upsidedown ? (cmp<0) : (cmp>0);
        break;
                }
    }
    if(a->numplayers < b->numplayers) return false;
    if(a->numplayers > b->numplayers) return true;
    int cmp = strcmp(a->name, b->name);
    if(cmp != 0) return (cmp<0);
    if(a->ping > b->ping) return false;
    if(a->ping < b->ping) return true;
    if(a->port < b->port) return true;
    if(a->port > b->port) return false;
    return false;

}
void sortservers()
{
    servers.sort(sicompare);
}
COMMAND(sortservers, "");

VARP(autosortservers, 0, 1, 1);
VARP(autoupdateservers, 0, 1, 1);

void refreshservers()
{
    static int lastrefresh = 0;
    if(lastrefresh==totalmillis) return;
    if(totalmillis - lastrefresh > 1000) 
    {
        loopv(servers) servers[i]->reset();
        lastreset = totalmillis;
    }
    lastrefresh = totalmillis;

    checkresolver();
    checkpings();
    if(totalmillis - lastinfo >= servpingrate/(maxservpings ? max(1, (servers.length() + maxservpings - 1) / maxservpings) : 1)) pingservers();
    if(autosortservers) sortservers();
}

serverinfo *selectedserver = NULL;

int allplayersonline = 0;
int lastallup = 0;
ICOMMAND(su_getplayers, "", (), {//total players online counter
    if(lastallup + 1000 < totalmillis )
    {
        allplayersonline = 0;
        loop(servs, servers.length())	allplayersonline += servers[servs]->numplayers;
        lastallup = totalmillis;
    }
    intret(allplayersonline);
}); 

struct favserver {
    char *name;
    char *address;
    char *realaddress;
    int port;
	~favserver()
	{
		DELETEA(name)
		DELETEA(address)
		DELETEA( realaddress)
	}
};
struct favservergroup {
    char *headline;
    vector<favserver *> server;
	~favservergroup()
	{
		DELETEA(headline)
		server.shrink(0);
	}
}; 
vector<favservergroup *> favs;

favserver *getfavserver(int group, int server)
{
	if(!favs.inrange(group) || !favs[group] ) return NULL;
	favservergroup *g = favs[group];
	if(!g->server.inrange(server)) return NULL;
	return g->server[server];
}

void favgroup(const char *headline)
{
	if(!headline || !headline[0]) return;
    favservergroup *fsg = new favservergroup;
	fsg->headline = new char[21];
    strncpy(fsg->headline, headline, 21);	fsg->headline[20] = '\0';
    favs.add(fsg);
}
ICOMMAND(favgroup, "s" ,(const char *headline), favgroup(headline));
void favserv(const char *name, const char *address, int port/*, const char *realaddress*/)
{
    if(!favs.length()) return;
    favservergroup *fsg = favs.last();
	if(!name || !address || !port || !name[0] || !address[0]) return;
    favserver *fs = new favserver;
	fs->name = new char[21];
    strncpy(fs->name, name, 21); fs->name[20] = '\0';
	fs->address = new char[16]; 
    strncpy(fs->address, address, 16); fs->address[15] = '\0';
    fs->port = port;
    fsg->server.add(fs);
    addserver(address, port);
}
ICOMMAND(favserv, "ssi", (const char *name, const char *address, int *port), favserv(name, address, *port));
void clearfavs()
{
	loopv(favs) DELETEP( favs[i])
    favs.setsize(0);
}
COMMAND(clearfavs, "");
ICOMMAND(numfavgroups, "", (), { intret(favs.length()); });
ICOMMAND(numfavserver, "i", (int *group), {
	if(favs.inrange(*group) && favs[*group])	intret(favs[*group]->server.length()); 
});

const char *getfavservername(int group, int server)
{
	favserver *s = getfavserver(group, server);
	if(!s) return NULL;
	return s->name;
}
ICOMMAND(getfavservername, "ii", (int *group, int *server), result(getfavservername(*group, *server)));

const char *getfavserveraddress(int group, int server)
{
	favserver *s = getfavserver(group, server);
	if(!s) return NULL;
	return s->address;
}
ICOMMAND(getfavserveraddress, "ii", (int *group, int *server), result(getfavserveraddress(*group, *server)));

void getfavserverport(int group, int server)
{
	favserver *s = getfavserver(group, server);
	if(!s) return;
	intret(s->port);
}
ICOMMAND(getfavserverport, "ii", (int *group, int *server), getfavserverport(*group, *server));

void getfavgroupname(int group)
{
	if(!favs.inrange(group) || !favs[group]) return;
	if(favs[group]->headline) result(favs[group]->headline);
}
ICOMMAND(getfavgroupname, "i", (int *group), getfavgroupname(*group));

void saveserverbrowsercfg()
{
    stream *f = openutf8file(path("data/serverbrowser.cfg"), "w");
    if(!f) { conoutf("not able to save your serverbrowser.cfg"); return; }
	f->putline("//favgroup is the headline, favserver adds a new server to the group. Important: the IP should not be the domain. Best is to edit with the menu provided.");
	f->putline("clearfavs");
	loopv(favs)
	{
		f->printf("\nfavgroup %s\n", favs[i]->headline);
		loopvk(favs[i]->server) {
			f->printf("favserv \"%s\" %s %d\n", favs[i]->server[k]->name, favs[i]->server[k]->address, favs[i]->server[k]->port);
		}
	}
	f->putline("\n//clantags \n//effects: clansearch in the serverbrowser, cut clantags automatically when saying sorry or in automatic duelscreenshots and for the scoreboard instead of good/evil .. ");
	f->putline("clearclans");
	loopv(game::clantags) if(game::clantags[i]) f->printf("addclan %s\n", game::clantags[i]->name);
	delete f;
}
COMMAND(saveserverbrowsercfg, "");


serverinfo *showfavs(g3d_gui *cgui) {
    serverinfo *sc = NULL;
    cgui->pushlist();
    loop(num, favs.length())
    {
        cgui->pushlist();
        cgui->text(favs[num]->headline, 0xCC44CC, NULL);

        loop(serv, favs[num]->server.length())
        {
            favserver *fs = getfavserver(num, serv);
			if(!fs) continue;
            loopvk(servers)
            {
                serverinfo &so = *servers[k];
                if(strstr(so.name, fs->address) && so.port == fs->port)
                {
                    //	if(so.attr.inrange(3))  
                    {
                        string plpl;
                        if(so.attr.inrange(3)) formatstring(plpl) ("%d/%d", so.numplayers, so.attr[3]);
                        else formatstring(plpl) ("--/--");
                        if(cgui->buttonf("%s %s(%s)", 0xCCCCCC, "menu", NULL,fs->name, 
							so.numplayers && so.attr.inrange(3) ? (so.numplayers >= so.attr[3] ? "\f3": "\f2") : "\f4", 
                            plpl)&G3D_UP)
                            selectedserver = sc = &so;
                    }
                    //	else cgui->buttonf("%s\f4 (--/--)", 0xAAAAAA, "arrow_fw", favs[num].names[serv]);

                }
            }
        }
        cgui->poplist();
        cgui->space(5);
    }
    cgui->poplist();
    cgui->separator();
    return sc;
}

bool waitforfreeslot = false;
static bool comparefoundpl(preview::foundplayer *a, preview::foundplayer *b)
{
	int cmp = strcmp(a->clan, b->clan);
	if(cmp != 0) return cmp < 0;
	cmp = strcmp(a->name, b->name);
	if(cmp != 0) return cmp < 0;
	if(a->frags < b->frags) return true;
	if(b->frags < a->frags) return false;
	if(a->cn < b->cn) return false;
	if(b->cn <= a->cn) return true;
	return false;
}
char *showplayers(g3d_gui *cgui)
{
    waitforfreeslot = false;
    refreshservers();
    serverinfo *sc = NULL;
    if(su_serverbrowserfavs) {
        sc = showfavs(cgui);
    }
    preview::clearfoundplayers();
    loopv(servers) preview::playerfilter(servers[i]);
	preview::foundplayers.sort(comparefoundpl);

    for(int start = 0; start < preview::foundplayers.length();)
    {
        if(start > 0) cgui->tab();
        int end = preview::foundplayers.length();
        cgui->pushlist();
        loopi(7)
        {
            if(!game::playerinfostartcolumn(cgui, i)) break;

            for(int j = start; j < end; j++)
            {
                preview::foundplayer &fp = *preview::foundplayers[j];
                if(!i && cgui->shouldtab()) { end = j; break; } //new tab when too many search results.
                const char *sdesc = fp.si->sdesc;
                if(fp.si->address.host == ENET_HOST_ANY || fp.si->ping == INT_MAX) continue;
                if(game::playerinfoentry(cgui, i, fp.name, fp.frags, fp.si->name, fp.si->port, fp.si->sdesc, fp.si->map, fp.si->attr, sdesc == fp.si->sdesc ? fp.si->ping : -1, fp.si->numplayers))
                    selectedserver = sc = fp.si;	
            }
            game::serverinfoendcolumn(cgui, i);
        }
        cgui->poplist();
        start = end;
    }

    if(!sc) return NULL;
    if(su_serverbrowser) return newstring("showgui serverextinfo");
    string command;
    formatstring(command)("connect %s %d", sc->name, sc->port);
    return newstring(command);
}

const char *showservers(g3d_gui *cgui, uint *header, int pagemin, int pagemax)
{
    waitforfreeslot = false;
    refreshservers();
    if(servers.empty())
    {
        if(header) execute(header);
        return NULL;
    }
    serverinfo *sc = NULL;
    if(su_serverbrowserfavs) {
        selectedserver = sc = showfavs(cgui);
    }

    for(int start = 0; start < servers.length();)
    {
        if(start > 0) cgui->tab();
        if(header) execute(header);
        int end = servers.length();
        cgui->pushlist();
        loopi(10)
        {
            if(!game::serverinfostartcolumn(cgui, i)) break;
            for(int j = start; j < end; j++)
            {
                if(!i && j+1 - start >= pagemin && (j+1 - start >= pagemax || cgui->shouldtab())) { end = j; break; }
                serverinfo &si = *servers[j];
                const char *sdesc = si.sdesc;
                if(si.address.host == ENET_HOST_ANY) sdesc = "[unknown host]";
                else if(si.ping == serverinfo::WAITING) sdesc = "[waiting for response]";
                if(preview::serverfilter(&si) && game::serverinfoentry(cgui, i, si.name, si.port, sdesc, si.map, sdesc == si.sdesc ? si.ping : -1, si.attr, si.numplayers) && (si.attr.length() && si.attr[0]==259 )) //todo protocol version
                    selectedserver = sc = &si;
            }
            game::serverinfoendcolumn(cgui, i);
        }
        cgui->poplist();
        start = end;
    }
    if(!sc) return NULL;
    if(su_serverbrowser) return newstring("showgui serverextinfo");
    return newstring("connectselected");
}

char *showserverinfo(g3d_gui * cgui)
{
    if(!selectedserver) return NULL; 
    refreshservers();
    
    char *out = preview::serverextinfo(cgui,selectedserver);
    cgui->separator();
    cgui->pushlist(); //-1
    cgui->spring(-1);
    if(cgui->button("Cancel",0xFFDDDD,"exit")&G3D_UP) {
        cgui->poplist();
        selectedserver = NULL;
        return newstring("cleargui 1");
    }
    cgui->space(40);

    bool hasfreeslot = true; //its default true because we dont want to ignore connect-request to servers which arent sending attributes.
    if(selectedserver->attr.length()>=4) 
        if(selectedserver->numplayers >= selectedserver->attr[3]) hasfreeslot = false;

    if(!waitforfreeslot) 
    {
        if(cgui->button("Connect",0xAAAAFF,"arrow_fw")&G3D_UP) waitforfreeslot = true;
    }
    else {
        if(cgui->button("", 0x000000, "exit")&G3D_UP) waitforfreeslot = false;
        cgui->text("Waiting for a free Slot", 0xAAAAAA);
    }
    if(waitforfreeslot && hasfreeslot) //when connectbutton is pressed, we are waiting till a free slot is available
    {
        cgui->poplist();
        waitforfreeslot = false;
        return newstring("connectselected");
    }
    cgui->poplist();
    return out;
}

void connectselected()
{
    if(!selectedserver) return;
    connectserv(selectedserver->name, selectedserver->port, selectedserver->password);
    selectedserver = NULL;
}

COMMAND(connectselected, "");

void clearservers(bool full = false)
{
    resolverclear();
    if(full) servers.deletecontents();
    else loopvrev(servers) if(!servers[i]->keep) delete servers.remove(i);
    selectedserver = NULL;
}

#define RETRIEVELIMIT 20000

void retrieveservers(vector<char> &data)
{
    ENetSocket sock = connectmaster(true);
    if(sock == ENET_SOCKET_NULL) return;

    extern char *mastername;
    defformatstring(text)("retrieving servers from %s... (esc to abort)", mastername);
    renderprogress(0, text);

    int starttime = SDL_GetTicks(), timeout = 0;
    const char *req = "list\n";
    int reqlen = strlen(req);
    ENetBuffer buf;
    while(reqlen > 0)
    {
        enet_uint32 events = ENET_SOCKET_WAIT_SEND;
        if(enet_socket_wait(sock, &events, 250) >= 0 && events) 
        {
            buf.data = (void *)req;
            buf.dataLength = reqlen;
            int sent = enet_socket_send(sock, NULL, &buf, 1);
            if(sent < 0) break;
            req += sent;
            reqlen -= sent;
            if(reqlen <= 0) break;
        }
        timeout = SDL_GetTicks() - starttime;
        renderprogress(min(float(timeout)/RETRIEVELIMIT, 1.0f), text);
        if(interceptkey(SDLK_ESCAPE)) timeout = RETRIEVELIMIT + 1;
        if(timeout > RETRIEVELIMIT) break;
    }

    if(reqlen <= 0) for(;;)
    {
        enet_uint32 events = ENET_SOCKET_WAIT_RECEIVE;
        if(enet_socket_wait(sock, &events, 250) >= 0 && events)
        {
            if(data.length() >= data.capacity()) data.reserve(4096);
            buf.data = data.getbuf() + data.length();
            buf.dataLength = data.capacity() - data.length();
            int recv = enet_socket_receive(sock, NULL, &buf, 1);
            if(recv <= 0) break;
            data.advance(recv);
        }
        timeout = SDL_GetTicks() - starttime;
        renderprogress(min(float(timeout)/RETRIEVELIMIT, 1.0f), text);
        if(interceptkey(SDLK_ESCAPE)) timeout = RETRIEVELIMIT + 1;
        if(timeout > RETRIEVELIMIT) break;
    }

    if(data.length()) data.add('\0');
    enet_socket_destroy(sock);
}

bool updatedservers = false;

void updatefrommaster()
{
    vector<char> data;
    retrieveservers(data);
    if(data.empty()) conoutf("master server not replying");
    else
    {
        clearservers();
        execute(data.getbuf());
    }
    refreshservers();
    updatedservers = true;
}

void initservers()
{
    selectedserver = NULL;
    if(autoupdateservers && !updatedservers) updatefrommaster();
}

ICOMMAND(addserver, "sis", (const char *name, int *port, const char *password), addserver(name, *port, password[0] ? password : NULL));
ICOMMAND(keepserver, "sis", (const char *name, int *port, const char *password), addserver(name, *port, password[0] ? password : NULL, true));
ICOMMAND(clearservers, "i", (int *full), clearservers(*full!=0));
COMMAND(updatefrommaster, "");
COMMAND(initservers, "");

void writeservercfg()
{
    if(!game::savedservers()) return;
    stream *f = openutf8file(path(game::savedservers(), true), "w");
    if(!f) return;
    int kept = 0;
    loopv(servers)
    {
        serverinfo *s = servers[i];
        if(s->keep)
        {
            if(!kept) f->printf("// servers that should never be cleared from the server list\n\n");
            if(s->password) f->printf("keepserver %s %d %s\n", escapeid(s->name), s->port, escapestring(s->password));
            else f->printf("keepserver %s %d\n", escapeid(s->name), s->port);
            kept++;
        }
    }
    if(kept) f->printf("\n");
    f->printf("// servers connected to are added here automatically\n\n");
    loopv(servers) 
    {
        serverinfo *s = servers[i];
        if(!s->keep) 
        {
            if(s->password) f->printf("addserver %s %d %s\n", escapeid(s->name), s->port, escapestring(s->password));
            else f->printf("addserver %s %d\n", escapeid(s->name), s->port);
        }
    }
    delete f;
}

