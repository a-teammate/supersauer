// the interface the engine uses to run the gameplay module

namespace entities
{
    extern void editent(int i, bool local);
    extern const char *entnameinfo(entity &e);
    extern const char *entname(int i);
    extern int extraentinfosize();
    extern void writeent(entity &e, char *buf);
    extern void readent(entity &e, char *buf, int ver);
    extern float dropheight(entity &e);
    extern void fixentity(extentity &e);
    extern void entradius(extentity &e, bool color);
    extern bool mayattach(extentity &e);
    extern bool attachent(extentity &e, extentity &a);
    extern bool printent(extentity &e, char *buf);
    extern extentity *newentity();
    extern void deleteentity(extentity *e);
    extern void clearents();
    extern vector<extentity *> &getents();
    extern const char *entmodel(const entity &e);
    extern void animatemapmodel(const extentity &e, int &anim, int &basetime);
}

namespace game
{
    extern void parseoptions(vector<const char *> &args);

    extern void gamedisconnect(bool cleanup);
    extern void parsepacketclient(int chan, packetbuf &p);
    extern void connectattempt(const char *name, const char *password, const ENetAddress &address);
    extern void connectfail();
    extern void gameconnect(bool _remote);
    extern bool allowedittoggle();
    extern void edittoggled(bool on);
    extern void writeclientinfo(stream *f);
    extern void toserver(char *text);
	extern void sayteam (char *text);
    extern void changemap(const char *name);
    extern void forceedit(const char *name);
    extern bool ispaused();
    extern int scaletime(int t);
    extern bool allowmouselook();

    extern const char *gameident();
    extern const char *savedconfig();
    extern const char *restoreconfig();
    extern const char *defaultconfig();
    extern const char *autoexec();
    extern const char *savedservers();
    extern void loadconfigs();


	enum { STATS_SMILED, STATS_FRAGS, STATS_CHAINFRAGS, 
		STATS_TKS, STATS_DEATHS, STATS_SUIS, STATS_GOT_TK, 
		STATS_SHOTS, STATS_JUMPS, STATS_FLAGS, STATS_DAMAGE, 
		STATS_QUAD, STATS_BOOST, STATS_AMMO, STATS_HEALTH, 
		STATS_WEEKS, STATS_DAYS, STATS_HOURS, STATS_MINUTES, STATS_SECONDS,
		STATS_CLANWARS, STATS_DUELS, STATS_NUM };
	extern int statslog[STATS_NUM]; 	
	extern void dotime();
	extern void writestats();
	extern void loadstats();
	    
	struct namestruct {
	    char name[16];
    };

    extern vector<namestruct *> friends;
    extern vector<namestruct *> clantags;
    extern string battleheadline; //either clanxy vs clanxx or playerxy vs playerxx
    extern bool isclanwar(bool allowspec = false, int colors = 0);
	extern const char *ownteamstatus();
	extern const char *demospath(char *n);

    extern void updateworld();
    extern void initclient();
    extern void physicstrigger(physent *d, bool local, int floorlevel, int waterlevel, int material = 0);
    extern void bounced(physent *d, const vec &surface);
    extern void edittrigger(const selinfo &sel, int op, int arg1 = 0, int arg2 = 0, int arg3 = 0);
    extern void vartrigger(ident *id);
    extern void dynentcollide(physent *d, physent *o, const vec &dir);
    extern const char *getclientmap();
    extern const char *getmapinfo();
    extern void resetgamestate();
    extern void suicide(physent *d);
    extern void newmap(int size);
    extern void startmap(const char *name);
    extern void preload();
    extern float abovegameplayhud(int w, int h);
    extern void gameplayhud(int w, int h);
    extern bool canjump();
    extern bool allowmove(physent *d);
    extern void doattack(bool on);
    extern dynent *iterdynents(int i);
    extern int numdynents();
    extern void rendergame(bool mainpass);
    extern void renderavatar();
    extern void renderplayerpreview(int model, int team, int weap);
    extern void writegamedata(vector<char> &extras);
    extern void readgamedata(vector<char> &extras);
    extern int clipconsole(int w, int h);
    extern void g3d_gamemenus();
    extern const char *defaultcrosshair(int index);
	extern int selectcrosshair(float &r, float &g, float &b, int &w, int &h);
	//extern int selectcrosshair(float &r, float &g, float &b);
    extern void lighteffects(dynent *d, vec &color, vec &dir);
    extern void setupcamera();
    extern bool detachcamera();
    extern bool collidecamera();
    extern void adddynlights();
    extern void particletrack(physent *owner, vec &o, vec &d);
    extern void dynlighttrack(physent *owner, vec &o, vec &hud);
    extern bool serverinfostartcolumn(g3d_gui *g, int i);
    extern void serverinfoendcolumn(g3d_gui *g, int i);
    extern bool serverinfoentry(g3d_gui *g, int i, const char *name, int port, const char *desc, const char *map, int ping, const vector<int> &attr, int np);
    extern bool needminimap();
	//////////////////////////////////extended serverbrowser ///////////////////////
	
	extern bool playerinfostartcolumn(g3d_gui *g, int i);
	extern bool playerinfoentry(g3d_gui *g, int i, const char *name, int frags, const char *host, int port, const char *sdesc, const char *map, const vector<int> &attr, int ping, int np);
	extern char *showserverdemos(g3d_gui *g);
} 
 
namespace server
{
    extern void *newclientinfo();
    extern void deleteclientinfo(void *ci);
    extern void serverinit();
    extern int reserveclients();
    extern int numchannels();
    extern void clientdisconnect(int n);
    extern int clientconnect(int n, uint ip);
    extern void localdisconnect(int n);
    extern void localconnect(int n);
    extern bool allowbroadcast(int n);
    extern void recordpacket(int chan, void *data, int len);
    extern void parsepacket(int sender, int chan, packetbuf &p);
    extern void sendservmsg(const char *s);
    extern bool sendpackets(bool force = false);
    extern void serverinforeply(ucharbuf &req, ucharbuf &p);
    extern void serverupdate();
    extern bool servercompatible(char *name, char *sdec, char *map, int ping, const vector<int> &attr, int np);
    extern int laninfoport();
    extern int serverinfoport(int servport = -1);
    extern int serverport(int infoport = -1);
    extern const char *defaultmaster();
    extern int masterport();
    extern void processmasterinput(const char *cmd, int cmdlen, const char *args);
    extern void masterconnected();
    extern void masterdisconnected();
    extern bool ispaused();
    extern int scaletime(int t);
}
///////////////////////extended demopreview && serverpreview//////////////////////
namespace preview  {
	struct extplayer {
		string name, team;
		int cn, ping, frags, flags, deaths, teamkills, accuracy, privilege, gunselect, state, playermodel;
		string ip;
		string country;
		extplayer() : cn(-1),ping(0),frags(0),flags(0),deaths(0),teamkills(0),accuracy(0),privilege(0), gunselect(4), state(0), playermodel(0)
		{
			name[0] = team[0] = ip[0] = country[0] = '\0';// uk for clans
		}
		~extplayer() 
		{
			cn = -1;
			ping = 0;
			name[0] = team[0] = ip[0] = country[0] = '\0';
		}
		static bool compare(extplayer * a, extplayer * b) 
		{
			if(a->flags > b->flags) return true;
			if(a->flags < b->flags) return false;
			if(a->frags > b->frags) return true;
			if(a->frags < b->frags) return false;
			return a->name[0] && b->name[0] ? strcmp(a->name, b->name) > 0 : false;
		}
	};
	struct extteam {
		string name;
		int score, frags;
		int numbases;
		vector<extplayer *> players;
		extteam() : score(0), numbases(-1), frags(0) {
			name[0] = 0;
		}
		extteam(char *in) : score(0), numbases(-1), frags(0) {
			if(in && in[0]) copystring(name, in);
		}
		~extteam() {
			name[0] = 0;
			score = frags = numbases = -1;
		}

		char *isclan()
        {
            string clanname;
            int members = 0;
            loopv(players)
            {
                if(members){ if(strstr(players[i]->name, clanname)) members++; }
                else loopvk(game::clantags)
                {
                        if(strstr(players[i]->name, game::clantags[k]->name))  
                        {  
                            strcpy(clanname, game::clantags[k]->name); 
                            members ++;
                        }
                }
            }
            return (members*3) >= (players.length()*2) && players.length()>=2 ? newstring(clanname) : NULL;
        }

		static bool compare(extteam *a, extteam *b)
		{
			if(!a->name[0])
			{
				if(b->name[0]) return false;
			}
			else if(!b->name[0]) return true;
			if(a->score > b->score) return true;
			if(a->score < b->score) return false;
			if(a->players.length() > b->players.length()) return true;
			if(a->players.length() < b->players.length()) return false;
			return a->name[0] && b->name[0] ? strcmp(a->name, b->name) > 0 : false;
		}
	};
	struct foundplayer {
		string name;
		string clan;
		int frags;
		int cn;
		serverinfo *si;  //later with geoip
		foundplayer() { name[0] = clan[0] = 0; frags = cn = 0; }
	};
	extern vector <foundplayer *> foundplayers;
	extern bool playerfilter(serverinfo * si);
	extern void clearfoundplayers();

	extern const char *listteams(g3d_gui *cgui, vector<extteam *> &teams, int mode, bool icons, bool forcealive, bool frags, bool deaths, bool tks, bool acc, bool flags, bool cn, bool ping = false);
	extern const char *listspectators(g3d_gui *cgui, vector<extplayer *> &spectators, bool cn = true, bool ping = false);
	extern char *showdemoinfo(g3d_gui *cgui); ///
	extern char *showdemohappenings(g3d_gui *cgui, uint *bottom);
	extern char *demobrowser(g3d_gui *g);

	extern void playerextinfo(g3d_gui *cgui, serverinfo * si);
	extern void updateplayerdb();
	extern char *serverextinfo(g3d_gui *cgui, serverinfo * si);
	extern int serverfilter(serverinfo * si);
	extern void extinforequest(ENetSocket &pingsock, ENetAddress & address,serverinfo * si, bool watching = false);
	extern bool extinfoparse(ucharbuf & p, serverinfo * si);
	extern void getstatsofpl(int cn, int &tks, int &deaths, int &damage, int &shots);
}
namespace cloud
{
	extern void slice();
}
namespace irc
{
	extern void cleanup();
	extern void slice();
	extern char *ircwindow(g3d_gui *cgui);
	extern char *ircnames(g3d_gui *cgui);
	extern char *ircbuffer(g3d_gui *cgui);
}


