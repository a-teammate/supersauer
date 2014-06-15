#include "game.h"
int lasttimeupdate = 0;
XVARP(su_sbfrags,0,1,1);
XVARP(su_sbdeaths,0,1,1);
XVARP(su_sbteamkills,0,1,1);
XVARP(su_sbaccuracy,0,1,1);
XVARP(su_sbflags,0,1,1);
XVARP(su_sbping,0,0,1);
XVARP(su_sbclientnum,0,0,1);

#define NOOBLOUNGEIP "176.9.75.98" 
extern vector<serverinfo *> servers;
extern int guifadein;
///////// <-
namespace game
{
    bool intermission = false;
    int maptime = 0, maprealtime = 0, maplimit = -1;
    int respawnent = -1;
    int lasthit = 0, lastspawnattempt = 0;

    int following = -1, followdir = 0;

    fpsent *player1 = NULL;         // our client
    vector<fpsent *> players;       // other clients
    int savedammo[NUMGUNS];

    bool clientoption(const char *arg) { return false; }

    void taunt()
    {
        if(player1->state!=CS_ALIVE || player1->physstate<PHYS_SLOPE) return;
        if(lastmillis-player1->lasttaunt<1000) return;
        player1->lasttaunt = lastmillis;
        addmsg(N_TAUNT, "rc", player1);
    }
    COMMAND(taunt, "");

    ICOMMAND(getfollow, "", (),
    {
        fpsent *f = followingplayer();
        intret(f ? f->clientnum : -1);
    });

	void follow(char *arg)
    {
        if(arg[0] ? player1->state==CS_SPECTATOR : following>=0)
        {
            following = arg[0] ? parseplayer(arg) : -1;
            if(following==player1->clientnum) following = -1;
            followdir = 0;
            conoutf("follow %s", following>=0 ? "on" : "off");
        }
	}
    COMMAND(follow, "s");

    void nextfollow(int dir)
    {
        if(player1->state!=CS_SPECTATOR || clients.empty())
        {
            stopfollowing();
            return;
        }
        int cur = following >= 0 ? following : (dir < 0 ? clients.length() - 1 : 0);
        loopv(clients)
        {
            cur = (cur + dir + clients.length()) % clients.length();
            if(clients[cur] && clients[cur]->state!=CS_SPECTATOR)
            {
                if(following<0) conoutf("follow on");
                following = cur;
                followdir = dir;
                return;
            }
        }
        stopfollowing();
    }
    ICOMMAND(nextfollow, "i", (int *dir), nextfollow(*dir < 0 ? -1 : 1));


    const char *getclientmap() { return clientmap; }

    void resetgamestate()
    {
        if(m_classicsp)
        {
            clearmovables();
            clearmonsters();                 // all monsters back at their spawns for editing
            entities::resettriggers();
        }
        clearprojectiles();
        clearbouncers();
    }

    fpsent *spawnstate(fpsent *d)              // reset player state not persistent accross spawns
    {
        d->respawn();
        d->spawnstate(gamemode);
        return d;
    }

    void respawnself()
    {
        if(ispaused()) return;
        if(m_mp(gamemode))
        {
            int seq = (player1->lifesequence<<16)|((lastmillis/1000)&0xFFFF);
            if(player1->respawned!=seq) { addmsg(N_TRYSPAWN, "rc", player1); player1->respawned = seq; }
        }
        else
        {
            spawnplayer(player1);
            showscores(false);
            lasthit = 0;
            if(cmode) cmode->respawned(player1);
        }
    }

    fpsent *pointatplayer()
    {
        loopv(players) if(players[i] != player1 && intersect(players[i], player1->o, worldpos)) return players[i];
        return NULL;
    }

    void stopfollowing()
    {
        if(following<0) return;
        following = -1;
        followdir = 0;
        conoutf("follow off");
    }

    fpsent *followingplayer()
    {
        if(player1->state!=CS_SPECTATOR || following<0) return NULL;
        fpsent *target = getclient(following);
        if(target && target->state!=CS_SPECTATOR) return target;
        return NULL;
    }

    fpsent *hudplayer()
    {
        if(thirdperson) return player1;
        fpsent *target = followingplayer();
        return target ? target : player1;
    }

    void setupcamera()
    {
        fpsent *target = followingplayer();
        if(target)
        {
            player1->yaw = target->yaw;
            player1->pitch = target->state==CS_DEAD ? 0 : target->pitch;
            player1->o = target->o;
            player1->resetinterp();
        }
    }

    bool detachcamera()
    {
        fpsent *d = hudplayer();
        return d->state==CS_DEAD;
    }

    bool collidecamera()
    {
        switch(player1->state)
        {
            case CS_EDITING: return false;
            case CS_SPECTATOR: return followingplayer()!=NULL;
        }
        return true;
    }

    VARP(smoothmove, 0, 75, 100);
    VARP(smoothdist, 0, 32, 64);

    void predictplayer(fpsent *d, bool move)
    {
        d->o = d->newpos;
        d->yaw = d->newyaw;
        d->pitch = d->newpitch;
        d->roll = d->newroll;
        if(move)
        {
            moveplayer(d, 1, false);
            d->newpos = d->o;
        }
        float k = 1.0f - float(lastmillis - d->smoothmillis)/smoothmove;
        if(k>0)
        {
            d->o.add(vec(d->deltapos).mul(k));
            d->yaw += d->deltayaw*k;
            if(d->yaw<0) d->yaw += 360;
            else if(d->yaw>=360) d->yaw -= 360;
            d->pitch += d->deltapitch*k;
            d->roll += d->deltaroll*k;
        }
    }

    void otherplayers(int curtime)
    {
        loopv(players)
        {
            fpsent *d = players[i];
            if(d == player1 || d->ai) continue;

            if(d->state==CS_DEAD && d->ragdoll) moveragdoll(d);
            else if(!intermission)
            {
                if(lastmillis - d->lastaction >= d->gunwait) d->gunwait = 0;
                if(d->quadmillis) entities::checkquad(curtime, d);
            }

            const int lagtime = totalmillis-d->lastupdate;
            if(!lagtime || intermission) continue;
            else if(lagtime>1000 && d->state==CS_ALIVE)
            {
                d->state = CS_LAGGED;
                continue;
            }
            if(d->state==CS_ALIVE || d->state==CS_EDITING)
            {
                if(smoothmove && d->smoothmillis>0) predictplayer(d, true);
                else moveplayer(d, 1, false);
            }
            else if(d->state==CS_DEAD && !d->ragdoll && lastmillis-d->lastpain<2000) moveplayer(d, 1, true);
        }
    }

    VARFP(slowmosp, 0, 0, 1, { if(m_sp && !slowmosp) server::forcegamespeed(100); }); 

    void checkslowmo()
    {
        static int lastslowmohealth = 0;
        server::forcegamespeed(intermission ? 100 : clamp(player1->health, 25, 200));
        if(player1->health<player1->maxhealth && lastmillis-max(maptime, lastslowmohealth)>player1->health*player1->health/2)
        {
            lastslowmohealth = lastmillis;
            player1->health++;
        }
    }
    /////////

    vector<namestruct *> friends;
    vector<namestruct *> clantags;
    
    void addclan(const char *clantag) 
	{
		namestruct *ct = new namestruct;
		strncpy(ct->name, clantag, 16);
		ct->name[15] = '\0';
		loopi(15) if(ct->name[i] == '\'' || ct->name[i] == '"' || ct->name[i] == '[' || ct->name[i] == ']') ct->name[i] = ' ';
		filtertext(ct->name, ct->name, false, 16);
		clantags.add(ct);
	}
	
	void addfriend(const char *name)
	{
		namestruct *nm = new namestruct;
		strncpy(nm->name, name, 16);
		nm->name[15] = '\0';
		friends.add(nm);
	}

    ICOMMAND(addclan, "s", (const char *tag), addclan(tag));
	ICOMMAND(addfriend, "s", (const char *name), addfriend(name));
    XVARP(su_cutclantags,0,1,1);
    XVARP(su_autoscreenshotscw, 0, 0, 1);
    XVARP(su_autoscreenshotsduel, 0, 0, 1);
    XVARP(su_autoscreenshotseperatedirs, 0, 1, 1);
    XVARP(su_intermissiontext, 0, 1, 1);

    string battleheadline; //either clanxy vs clanxx or playerxy vs playerxx
    char *makefilename( const char *input)
    {
        string output;
        int len = min(259, (int)strlen(input));
        loopi(len)
        {
            if( (int)input[i] == 34 || // "
                (int)input[i] == 42 || // *
                (int)input[i] == 47 || // /
                (int)input[i] == 58 || //:
                (int)input[i] == 60 || //<
                (int)input[i] == 62 || //>
                (int)input[i] == 63 || //?
                (int)input[i] == 92 ||// backslash
                (int)input[i] == 124) // |
                output[i] = ' ';
            else output[i] = input[i];
        }
        output[len] = '\0';
        filtertext(output, output, false);
        return newstring(output);
    }
    const char *cutclantag(const char *name)
    {
        char myname[20];
		strcpy(myname, name);
		loopv(clantags) if(strstr(myname, clantags[i]->name))
        {
				char *pch;
				pch = strstr(myname, clantags[i]->name);
				strncpy(pch, "                    ", strlen(clantags[i]->name));
				puts(myname);
				filtertext(myname, myname, false, 19);
                return newstring(myname);
		}
        return name;
    }
    bool isduel(bool allowspec = false, int colors = 0)
    {
        extern int mastermode;
        if((!allowspec && player1->state == CS_SPECTATOR) || mastermode < MM_LOCKED || m_teammode) return false; 
        int playingguys = 0;
		fpsent *p1, *p2; p1 = p2 = NULL;
		loopv(players) if(players[i]->state != CS_SPECTATOR) { 
			playingguys++; 
			if(p1) p2 = players[i]; 
			else if(playingguys >2) break;
			else p1 = players[i]; }
		if(playingguys != 2) return false;
		
        string output;
        const char *p1name = su_cutclantags ? cutclantag(p1->name) : p1->name;
        const char *p2name = su_cutclantags ? cutclantag(p2->name) : p2->name;
		
		fpsent *f = followingplayer();
		if(!f && player1->state != CS_SPECTATOR) f = player1;
		if(!colors) formatstring(output) ("%s(%d) vs %s(%d)", p1name, p1->frags, p2name, p2->frags);
		else if(colors == 1 || !f) formatstring(output) ("\f2%s\f7(%d) \f4vs \f1%s\f7(%d)", p1name, p1->frags, p2name, p2->frags);
		else {
			bool winning = (f == p1 && p1->frags > p2->frags) || (f == p2 && p2->frags > p1->frags);
            
			formatstring(output) ("%s%s (\fs%s%d\fr) vs %s (\fs%s%d\fr)", winning ? "\fg" : "\fd", p1name, winning ? "\fG" : "\fR", p1->frags, p2name, winning ? "\fG" : "\fR", p2->frags);
		}
        strcpy(battleheadline, output);

        return true;
    }


	struct autoevent {
		int start;
		int time;
		string what;
		int type;
		autoevent() { copystring(what , "unknown"); start = 0; time = 0; type = 0;}
	};
	vector<autoevent> autoevents;

    enum { A_TEAMCHAT, A_CHAT, A_SCREENSHOT};
    enum { DIR_NORMAL, DIR_DUEL, DIR_CLANWAR };
    
	void checkautoevents() 
    {
		loopv(autoevents) {
			if(totalmillis-autoevents[i].start > autoevents[i].time)
			{	
				switch(autoevents[i].type) 
                {
                    case A_TEAMCHAT:    sayteam(autoevents[i].what);            break;
                    case A_CHAT:        toserver(autoevents[i].what);           break;
                    
                    case A_SCREENSHOT:  
                    {
                        if(!su_autoscreenshotsduel && !su_autoscreenshotscw && !su_intermissiontext) break;
                        bool iscw = false; if(su_autoscreenshotscw) iscw = isclanwar();
                        bool isdl = false; if(su_autoscreenshotsduel) isdl = isduel();
						if(su_intermissiontext) conoutf(battleheadline);
                        if(!iscw && !isdl) break;
                        else if(iscw && !su_autoscreenshotscw) break;
                        else if(isdl && !su_autoscreenshotsduel) break;
                        int prev = guifadein;
                        guifadein = 0;
                        showscores(true);
                        screenshot(makefilename(battleheadline), false, su_autoscreenshotseperatedirs ? (iscw ? DIR_CLANWAR : DIR_DUEL): DIR_NORMAL);
                        guifadein = prev;
                        break;
                    }
                    default:    conoutf("Es gibt ein Problem mit autoevents <.> <.>"); break;
                }
				autoevents.remove(i);
			}
		}
	}


    void updateworld()        // main game update loop
    {
        if(!maptime) { maptime = lastmillis; maprealtime = totalmillis; return; }
        if(!curtime) { gets2c(); if(player1->clientnum>=0) c2sinfo(); return; }

		checkautoevents();
        physicsframe();
        ai::navigate();
        if(player1->state != CS_DEAD && !intermission)
        {
            if(player1->quadmillis) entities::checkquad(curtime, player1);
        }
        updateweapons(curtime);
        otherplayers(curtime);
        ai::update();
        moveragdolls();
        gets2c();
        updatemovables(curtime);
        updatemonsters(curtime);
        if(player1->state == CS_DEAD)
        {
            if(player1->ragdoll) moveragdoll(player1);
            else if(lastmillis-player1->lastpain<2000)
            {
                player1->move = player1->strafe = 0;
                moveplayer(player1, 10, true);
            }
        }
        else if(!intermission)
        {
            if(player1->ragdoll) cleanragdoll(player1);
            moveplayer(player1, 10, true);
            swayhudgun(curtime);
            entities::checkitems(player1);
            if(m_sp)
            {
                if(slowmosp) checkslowmo();
                if(m_classicsp) entities::checktriggers();
            }
            else if(cmode) cmode->checkitems(player1);
        }
        if(player1->clientnum>=0) c2sinfo();   // do this last, to reduce the effective frame lag
    }

    void spawnplayer(fpsent *d)   // place at random spawn
    {
        if(cmode) cmode->pickspawn(d);
        else findplayerspawn(d, d==player1 && respawnent>=0 ? respawnent : -1);
        spawnstate(d);
        if(d==player1)
        {
            if(editmode) d->state = CS_EDITING;
            else if(d->state != CS_SPECTATOR) d->state = CS_ALIVE;
        }
        else d->state = CS_ALIVE;
    }

    VARP(spawnwait, 0, 0, 1000);

    void respawn()
    {
        if(player1->state==CS_DEAD)
        {
            player1->attacking = false;
            int wait = cmode ? cmode->respawnwait(player1) : 0;
            if(wait>0)
            {
                lastspawnattempt = lastmillis;
                //conoutf(CON_GAMEINFO, "\f2you must wait %d second%s before respawn!", wait, wait!=1 ? "s" : "");
                return;
            }
            if(lastmillis < player1->lastpain + spawnwait) return;
            if(m_dmsp) { changemap(clientmap, gamemode); return; }    // if we die in SP we try the same map again
            respawnself();
            if(m_classicsp)
            {
                conoutf(CON_GAMEINFO, "\f2You wasted another life! The monsters stole your armour and some ammo...");
                loopi(NUMGUNS) if(i!=GUN_PISTOL && (player1->ammo[i] = savedammo[i]) > 5) player1->ammo[i] = max(player1->ammo[i]/3, 5);
            }
        }
    }

    // inputs

    void doattack(bool on)
    {
        if(intermission) return;
        if((player1->attacking = on)) respawn();
    }

    bool canjump()
    {
        if(!intermission) respawn();
        return player1->state!=CS_DEAD && !intermission;
    }

    bool allowmove(physent *d)
    {
        if(d->type!=ENT_PLAYER) return true;
        return !((fpsent *)d)->lasttaunt || lastmillis-((fpsent *)d)->lasttaunt>=1000;
    }

    VARP(hitsound, 0, 0, 1);

    void damaged(int damage, fpsent *d, fpsent *actor, bool local)
    {
        if((d->state!=CS_ALIVE && d->state != CS_LAGGED && d->state != CS_SPAWNING) || intermission) return;

        if(local) damage = d->dodamage(damage);
        else if(actor==player1) return;

        fpsent *h = hudplayer();
        if(h!=player1 && actor==h && d!=actor)
        {
            if(hitsound && lasthit != lastmillis) playsound(S_HIT);
            lasthit = lastmillis;
        }
        if(d==h)
        {
            damageblend(damage);
            damagecompass(damage, actor->o);
        }
        damageeffect(damage, d, d!=h);

		ai::damaged(d, actor);

        if(m_sp && slowmosp && d==player1 && d->health < 1) d->health = 1;

        if(d->health<=0) { if(local) killed(d, actor); }
        else if(d==h) playsound(S_PAIN6);
        else playsound(S_PAIN1+rnd(5), &d->o);
    }

    VARP(deathscore, 0, 1, 1);

    void deathstate(fpsent *d, bool restore)
    {
        d->state = CS_DEAD;
        d->lastpain = lastmillis;
        if(!restore) gibeffect(max(-d->health, 0), d->vel, d);
        if(d==player1)
        {
            if(deathscore) showscores(true);
            disablezoom();
            if(!restore) loopi(NUMGUNS) savedammo[i] = player1->ammo[i];
            d->attacking = false;
            if(!restore) d->deaths++;
            //d->pitch = 0;
            d->roll = 0;
            playsound(S_DIE1+rnd(2));
        }
        else
        {
			if(!restore) d->deaths++; // update all players
            d->move = d->strafe = 0;
            d->resetinterp();
            d->smoothmillis = 0;
            playsound(S_DIE1+rnd(2), &d->o);
        }
    }

    VARP(teamcolorfrags, 0, 1, 1);


	int lastacc = 0;
	bool onecheck = true;
	int oldacc = 0;  

	XSVARP(su_autosrymsg,"Sorry");
	XSVARP(su_autonpmsg,"Np");
	XSVARP(su_autoggmsg, "good game");


	XVARP(su_sendname,0,1,1);
    

	XVARP(su_autogg,0,0,1);
	XVARP(su_autosry,0,0,1);
	XVARP(su_autonp,0,0,1);

	XVARP(su_autoggdelay,0,3,10);
	XVARP(su_autosrydelay,0,3,10);
	XVARP(su_autonpdelay,0,3,10);

	XVARP(su_fraginfo,0,0,1);

	int ssmun = 0;
	int dfda = 0;
	string who;
	int ssmun2 = 0;
	int dfda2 = 0;
	string dfda3;
	string dfda5;
	string dfda6;
	int wennst = 0;
	int wennst2 = 0;

    void killed(fpsent *d, fpsent *actor)
    {
        if(d->state==CS_EDITING)
        {
            d->editstate = CS_DEAD;
            if(d==player1) d->deaths++;
            else d->resetinterp();
            return;
        }
        else if((d->state!=CS_ALIVE && d->state != CS_LAGGED && d->state != CS_SPAWNING) || intermission) return;
		if(d==player1) { //you got killed
			if(su_totalstats) statslog[STATS_DEATHS]++; 
		}
        fpsent *h = followingplayer();
        if(!h) h = player1;
        int contype = d==h || actor==h ? CON_FRAG_SELF : CON_FRAG_OTHER;
        const char *dname = "", *aname = "";
        if(m_teammode && teamcolorfrags)
        {
            dname = teamcolorname(d, "you");
            aname = teamcolorname(actor, "you");
        }
        else
        {
            dname = colorname(d, NULL, "", "", "you");
            aname = colorname(actor, NULL, "", "", "you");
        }

		if(actor->type==ENT_AI){
            conoutf(contype, "\f2%s got killed by %s!", dname, aname);
			ssmun2 = 1;
			dfda2 = totalmillis;
			copystring(dfda3, aname);
			wennst = 0;
		}
        else if(d==actor || actor->type==ENT_INANIMATE)
		{
            conoutf(contype, "\f2%s suicided%s", dname, d==player1 ? "!" : "");
			if(d==player1) if(su_totalstats) statslog[STATS_SUIS]++; //you suicided
		}
        else if(isteam(d->team, actor->team))
        {
            contype |= CON_TEAMKILL;
			if(actor==player1){
				conoutf(contype, "\f6%s fragged a teammate (%s)", aname, dname);
                
                
				if(su_totalstats) statslog[STATS_TKS]++; //you fragged a teammate
				if (su_autosry && isconnected(false, false))
				{ 
                    defformatstring(sry)("%s %s",su_autosrymsg ,(su_sendname) ? dname : "" );
									
					if(su_cutclantags && su_sendname) 
                    {
                        formatstring(sry)("%s %s", su_autosrymsg, cutclantag(dname));
					}
                    autoevent &ev = autoevents.add();
					ev.start = totalmillis;
					ev.time = su_autosrydelay * 1000;					
					copystring(ev.what, newstring(sry));
					ev.type = A_TEAMCHAT;
				}
				ssmun = 1;
				dfda = totalmillis;
				copystring(who, dname);
				wennst2 = 1;			
			}
			else if(d==player1) {
				conoutf(contype, "\f6%s got fragged by a teammate (%s)", dname, aname);
				
				if(su_totalstats)statslog[STATS_GOT_TK]++; //you got teamkilled
				if (su_autonp && isconnected(false, false))
				{ 
                    defformatstring(tkill)("%s %s",su_autonpmsg ,(su_sendname) ? aname:"" );
                    
                    if(su_cutclantags && su_sendname) 
                    {
                           formatstring(tkill)("%s %s", su_autonpmsg, cutclantag(aname));
					}
					autoevent &ev = autoevents.add();
					ev.start = totalmillis;
					ev.time = su_autonpdelay * 1000;					
					copystring(ev.what, newstring(tkill));
					ev.type = A_TEAMCHAT;
				}			
				ssmun2 = 1;
				dfda2 = totalmillis;
				copystring(dfda3, aname);
				wennst = 1;
			}
            else conoutf(contype, "\f2%s fragged a teammate (%s)", aname, dname);
        }
        else
        {
            if(d==player1) conoutf(contype, "\f2%s got fragged by %s", dname, aname);
			else { conoutf(contype, "\f2%s fragged %s", aname, dname);} //you fragged someone
			if(actor==player1)
			{
				ssmun = 1;
				dfda = totalmillis;
				copystring(who, dname);
				wennst2 = 0;
                if(su_totalstats) { 
                        statslog[STATS_FRAGS]++; //you fragged someone
				        if(player1->gunselect == 0)statslog[STATS_CHAINFRAGS]++; //you fragged someone with chainsaw
                }
        }

		}
		defformatstring(dfda4)("\f2You got killed by %s %s", dfda3, wennst?"\f3your TEAMMATE!   ":"!    ");
		copystring(dfda5, dfda4);
		defformatstring(dfda2)("You killed %s %s", who, wennst2?"\f3your TEAMMATE!   ":"!    ");
		copystring(dfda6, dfda2);
        deathstate(d);
		ai::killed(d, actor);
    }

    void timeupdate(int secs)
    {
        if(secs > 0)
        {
            maplimit = lastmillis + secs*1000;
        }
        else
        {
            intermission = true;
            player1->attacking = false;
            if(cmode) cmode->gameover();
            conoutf(CON_GAMEINFO, "\f2intermission:");
            conoutf(CON_GAMEINFO, "\f2game has ended!");
            if(m_ctf) conoutf(CON_GAMEINFO, "\f2player frags: %d, flags: %d, deaths: %d", player1->frags, player1->flags, player1->deaths);
            else if(m_collect) conoutf(CON_GAMEINFO, "\f2player frags: %d, skulls: %d, deaths: %d", player1->frags, player1->flags, player1->deaths);
            else conoutf(CON_GAMEINFO, "\f2player frags: %d, deaths: %d", player1->frags, player1->deaths);
            int accuracy = (player1->totaldamage*100)/max(player1->totalshots, 1);
            conoutf(CON_GAMEINFO, "\f2player total damage dealt: %d, damage wasted: %d, accuracy(%%): %d", player1->totaldamage, player1->totalshots-player1->totaldamage, accuracy);

            //if(isclanwar() || isduel()) conoutf(CON_GAMEINFO, "\f0%s", battleheadline);
         
			if(m_sp) spsummary(accuracy);
            showscores(true);
            disablezoom();
            if(su_autoscreenshotsduel || su_autoscreenshotscw)
            {
                defformatstring(versus) ("%s", "noneyet");
                autoevent &ev = autoevents.add();
				ev.start = totalmillis;
				ev.time = 1000;					
				copystring(ev.what, newstring(versus));
				ev.type = A_SCREENSHOT;
            }
			if(isconnected(false, false) && su_autogg) {
				autoevent &ev = autoevents.add();
				ev.start = totalmillis;
				ev.time = su_autoggdelay * 1000;					
				copystring(ev.what, newstring(su_autoggmsg));
				ev.type = A_CHAT;		
			}
			if(game::su_totalstats && player1->state != CS_SPECTATOR) {
				if(isclanwar()) statslog[STATS_CLANWARS]++;
				else if(isduel()) statslog[STATS_DUELS]++;
			}
            if(identexists("intermission")) execute("intermission");
        }
    }

	ICOMMAND(statslog, "i", (int *i), intret(game::statslog[*i]));

	const char *statsfile() { return "stats.ini"; }
	
	void loadstats() {
		stream *f = openutf8file(path(statsfile(), true), "r");
		if(!f) {
			loopi(STATS_NUM) statslog[i] = 0;
			return;
		}
		char buf[255] = "";
		loopi(STATS_NUM) {
			f->getline(buf, sizeof(buf));
			statslog[i] = atoi(buf);
		} 
		DELETEP(f);
	}

	void writestats() {
		stream *f = openutf8file(path(statsfile(), true), "w");
		if(!f) { conoutf("not able to save stats"); return; }
		string s;
		loopi(STATS_NUM) {
			formatstring(s)("%d",statslog[i]);
			f->putline(s);
		}
		DELETEP(f)
	}
	void clearstats() {

		loopi(STATS_NUM) statslog[i] = 0;
	}

	COMMAND(clearstats,"");
	COMMAND(loadstats,"");
	COMMAND(writestats,"");

		//// statslogs:
	//// 0 = smiled
	//// 1 = frags
	//// 2 = chainsaw kills 
		////	3 = teamkills
	//// 4 = deaths
	//// 5 = suicides
	//// 6 = killed by teammates
		//// 7 = totalshots
	//// 8 = jumps
	//// 9 = flags
	//// 10 = damage
		//// 11 = quad damage
	//// 12 = health boost
	//// 13 = ammo pack
	//// 14 = health pack
	//// 15 = weeks
	//// 16 = days
	//// 17 = hours
	//// 18 = minutes
	//// 19 = secs
	//// 20 = clanwars played
	//// 21 = duels played
	void dotime()   
	{
		if(totalmillis < lasttimeupdate+1000) return;
		statslog[STATS_SECONDS]++;  

		if(statslog[STATS_SECONDS] >= 60)  
		{
			statslog[STATS_SECONDS] = 0;    
			statslog[STATS_MINUTES]++;     
		}

		if(statslog[STATS_MINUTES] >= 60)   
		{
			statslog[STATS_MINUTES] = 0;    
			statslog[STATS_HOURS]++;   
		}

		if(statslog[STATS_HOURS] >= 24)   
		{
			statslog[STATS_HOURS] = 0;   
			statslog[STATS_DAYS]++;      
		}

		if(statslog[STATS_DAYS] >= 7)    
		{
			statslog[STATS_DAYS] = 0;    
			statslog[STATS_WEEKS]++;      
		}
		lasttimeupdate = totalmillis;
	}

    ICOMMAND(getfrags, "", (), intret(player1->frags));
    ICOMMAND(getflags, "", (), intret(player1->flags));
    ICOMMAND(getdeaths, "", (), intret(player1->deaths));
    ICOMMAND(getaccuracy, "", (), intret((player1->totaldamage*100)/max(player1->totalshots, 1)));
    ICOMMAND(gettotaldamage, "", (), intret(player1->totaldamage));
    ICOMMAND(gettotalshots, "", (), intret(player1->totalshots));

    vector<fpsent *> clients;

    fpsent *newclient(int cn)   // ensure valid entity
    {
        if(cn < 0 || cn > max(0xFF, MAXCLIENTS + MAXBOTS))
        {
            neterr("clientnum", false);
            return NULL;
        }

        if(cn == player1->clientnum) return player1;

        while(cn >= clients.length()) clients.add(NULL);
        if(!clients[cn])
        {
            fpsent *d = new fpsent;
            d->clientnum = cn;
            clients[cn] = d;
            players.add(d);
        }
        return clients[cn];
    }

    fpsent *getclient(int cn)   // ensure valid entity
    {
        if(cn == player1->clientnum) return player1;
        return clients.inrange(cn) ? clients[cn] : NULL;
    }

    void clientdisconnected(int cn, bool notify)
    {
        if(!clients.inrange(cn)) return;
        if(following==cn)
        {
            if(followdir) nextfollow(followdir);
            else stopfollowing();
        }
        unignore(cn);
        fpsent *d = clients[cn];
        if(!d) return;
        if(notify && d->name[0]) conoutf("\f4leave:\f7 %s", colorname(d));
        removeweapons(d);
        removetrackedparticles(d);
        removetrackeddynlights(d);
        if(cmode) cmode->removeplayer(d);
        players.removeobj(d);
        DELETEP(clients[cn]);
        cleardynentcache();
    }

    void clearclients(bool notify)
    {
        loopv(clients) if(clients[i]) clientdisconnected(i, notify);
    }

    void initclient()
    {
        player1 = spawnstate(new fpsent);
        filtertext(player1->name, "unnamed", false, MAXNAMELEN);
        players.add(player1);
    }

    VARP(showmodeinfo, 0, 1, 1);

    void startgame()
    {
        clearmovables();
        clearmonsters();

        clearprojectiles();
        clearbouncers();
        clearragdolls();

        clearteaminfo();

        // reset perma-state
        loopv(players)
        {
            fpsent *d = players[i];
            d->frags = d->flags = 0;
            d->deaths = 0;
            d->totaldamage = 0;
            d->totalshots = 0;
            d->maxhealth = 100;
            d->lifesequence = -1;
            d->respawned = d->suicided = -2;
        }

        setclientmode();

        intermission = false;
        maptime = maprealtime = 0;
        maplimit = -1;

        if(cmode)
        {
            cmode->preload();
            cmode->setup();
        }

        conoutf(CON_GAMEINFO, "\f2game mode is %s", server::modename(gamemode));

        if(m_sp)
        {
            defformatstring(scorename)("bestscore_%s", getclientmap());
            const char *best = getalias(scorename);
            if(*best) conoutf(CON_GAMEINFO, "\f2try to beat your best score so far: %s", best);
        }
        else
        {
            const char *info = m_valid(gamemode) ? gamemodes[gamemode - STARTGAMEMODE].info : NULL;
            if(showmodeinfo && info) conoutf(CON_GAMEINFO, "\f0%s", info);
        }

        if(player1->playermodel != playermodel) switchplayermodel(playermodel);

        showscores(false);
        disablezoom();
        lasthit = 0;

        if(identexists("mapstart")) execute("mapstart");
    }

    void startmap(const char *name)   // called just after a map load
    {
        ai::savewaypoints();
        ai::clearwaypoints(true);

        respawnent = -1; // so we don't respawn at an old spot
        if(!m_mp(gamemode)) spawnplayer(player1);
        else findplayerspawn(player1, -1);
        entities::resetspawns();
        copystring(clientmap, name ? name : "");
        
        sendmapinfo();
    }

    const char *getmapinfo()
    {
        return showmodeinfo && m_valid(gamemode) ? gamemodes[gamemode - STARTGAMEMODE].info : NULL;
    }

    void physicstrigger(physent *d, bool local, int floorlevel, int waterlevel, int material)
    {
        if(d->type==ENT_INANIMATE) return;
        if     (waterlevel>0) { if(material!=MAT_LAVA) playsound(S_SPLASH1, d==player1 ? NULL : &d->o); }
        else if(waterlevel<0) playsound(material==MAT_LAVA ? S_BURN : S_SPLASH2, d==player1 ? NULL : &d->o);
		if     (floorlevel>0) { if(d==player1 || d->type!=ENT_PLAYER || ((fpsent *)d)->ai) { msgsound(S_JUMP, d); if(su_totalstats && d==player1) statslog[STATS_JUMPS]++; } } //totalstats jumped			
        else if(floorlevel<0) { if(d==player1 || d->type!=ENT_PLAYER || ((fpsent *)d)->ai) msgsound(S_LAND, d); }
    }

    void dynentcollide(physent *d, physent *o, const vec &dir)
    {
        switch(d->type)
        {
            case ENT_AI: if(dir.z > 0) stackmonster((monster *)d, o); break;
            case ENT_INANIMATE: if(dir.z > 0) stackmovable((movable *)d, o); break;
        }
    }

    void msgsound(int n, physent *d)
    {
        if(!d || d==player1)
        {
            addmsg(N_SOUND, "ci", d, n);
            playsound(n);
        }
        else
        {
            if(d->type==ENT_PLAYER && ((fpsent *)d)->ai)
                addmsg(N_SOUND, "ci", d, n);
            playsound(n, &d->o);
        }
    }

    int numdynents() { return players.length()+monsters.length()+movables.length(); }

    dynent *iterdynents(int i)
    {
        if(i<players.length()) return players[i];
        i -= players.length();
        if(i<monsters.length()) return (dynent *)monsters[i];
        i -= monsters.length();
        if(i<movables.length()) return (dynent *)movables[i];
        return NULL;
    }

    bool duplicatename(fpsent *d, const char *name = NULL, const char *alt = NULL)
    {
        if(!name) name = d->name;
        if(alt && d != player1 && !strcmp(name, alt)) return true;
        loopv(players) if(d!=players[i] && !strcmp(name, players[i]->name)) return true;
        return false;
    }

    static string cname[3];
    static int cidx = 0;

    const char *colorname(fpsent *d, const char *name, const char *prefix, const char *suffix, const char *alt)
    {
        if(!name) name = alt && d == player1 ? alt : d->name; 
        bool dup = !name[0] || duplicatename(d, name, alt) || d->aitype != AI_NONE;
        if(dup || prefix[0] || suffix[0])
        {
            cidx = (cidx+1)%3;
            if(dup) formatstring(cname[cidx])(d->aitype == AI_NONE ? "%s%s \fs\f5(%d)\fr%s" : "%s%s \fs\f5[%d]\fr%s", prefix, name, d->clientnum, suffix);
            else formatstring(cname[cidx])("%s%s%s", prefix, name, suffix);
            return cname[cidx];
        }
        return name;
    }

    VARP(teamcolortext, 0, 1, 1);

    const char *teamcolorname(fpsent *d, const char *alt)
    {
        if(!teamcolortext || !m_teammode) return colorname(d, NULL, "", "", alt);
        return colorname(d, NULL, isteam(d->team, player1->team) ? "\fs\f1" : "\fs\f3", "\fr", alt); 
    }

    const char *teamcolor(const char *name, bool sameteam, const char *alt)
    {
        if(!teamcolortext || !m_teammode) return sameteam || !alt ? name : alt;
        cidx = (cidx+1)%3;
        formatstring(cname[cidx])(sameteam ? "\fs\f1%s\fr" : "\fs\f3%s\fr", sameteam || !alt ? name : alt);
        return cname[cidx];
    }    
    
    const char *teamcolor(const char *name, const char *team, const char *alt)
    {
        return teamcolor(name, team && isteam(team, player1->team), alt);
    }

    void suicide(physent *d)
    {
        if(d==player1 || (d->type==ENT_PLAYER && ((fpsent *)d)->ai))
        {
            if(d->state!=CS_ALIVE) return;
            fpsent *pl = (fpsent *)d;
            if(!m_mp(gamemode)) killed(pl, pl);
            else 
            {
                int seq = (pl->lifesequence<<16)|((lastmillis/1000)&0xFFFF);
                if(pl->suicided!=seq) { addmsg(N_SUICIDE, "rc", pl); pl->suicided = seq; }
            }
        }
        else if(d->type==ENT_AI) suicidemonster((monster *)d);
        else if(d->type==ENT_INANIMATE) suicidemovable((movable *)d);
    }
    ICOMMAND(kill, "", (), suicide(player1));

    bool needminimap() { return m_ctf || m_protect || m_hold || m_capture || m_collect; }


	XVARP(su_selogo, 0, 0, 1);

    void drawicon(int icon, float x, float y, float sz)
    {
		if(su_selogo) settexture("packages/hud/flags.png");
		else settexture("packages/hud/items.png");			
        glBegin(GL_TRIANGLE_STRIP);
        float tsz = 0.25f, tx = tsz*(icon%4), ty = tsz*(icon/4);
        glTexCoord2f(tx,     ty);     glVertex2f(x,    y);
        glTexCoord2f(tx+tsz, ty);     glVertex2f(x+sz, y);
        glTexCoord2f(tx,     ty+tsz); glVertex2f(x,    y+sz);
        glTexCoord2f(tx+tsz, ty+tsz); glVertex2f(x+sz, y+sz);
        glEnd();
    }

    float abovegameplayhud(int w, int h)
    {
        switch(hudplayer()->state)
        {
            case CS_EDITING:
            case CS_SPECTATOR:
                return 1;
            default:
                return 1650.0f/1800.0f;
        }
    }

    int ammohudup[3] = { GUN_CG, GUN_RL, GUN_GL },
        ammohuddown[3] = { GUN_RIFLE, GUN_SG, GUN_PISTOL },
        ammohudcycle[7] = { -1, -1, -1, -1, -1, -1, -1 };

    ICOMMAND(ammohudup, "V", (tagval *args, int numargs),
    {
        loopi(3) ammohudup[i] = i < numargs ? getweapon(args[i].getstr()) : -1;
    });

    ICOMMAND(ammohuddown, "V", (tagval *args, int numargs),
    {
        loopi(3) ammohuddown[i] = i < numargs ? getweapon(args[i].getstr()) : -1;
    });

    ICOMMAND(ammohudcycle, "V", (tagval *args, int numargs),
    {
        loopi(7) ammohudcycle[i] = i < numargs ? getweapon(args[i].getstr()) : -1;
    });

    VARP(ammohud, 0, 1, 1);

    void drawammohud(fpsent *d)
    {
        float x = HICON_X + 2*HICON_STEP, y = HICON_Y, sz = HICON_SIZE;
        glPushMatrix();
        glScalef(1/3.2f, 1/3.2f, 1);
        float xup = (x+sz)*3.2f, yup = y*3.2f + 0.1f*sz;
        loopi(3)
        {
            int gun = ammohudup[i];
            if(gun < GUN_FIST || gun > GUN_PISTOL || gun == d->gunselect || !d->ammo[gun]) continue;
            drawicon(HICON_FIST+gun, xup, yup, sz);
            yup += sz;
        }
        float xdown = x*3.2f - sz, ydown = (y+sz)*3.2f - 0.1f*sz;
        loopi(3)
        {
            int gun = ammohuddown[3-i-1];
            if(gun < GUN_FIST || gun > GUN_PISTOL || gun == d->gunselect || !d->ammo[gun]) continue;
            ydown -= sz;
            drawicon(HICON_FIST+gun, xdown, ydown, sz);
        }
        int offset = 0, num = 0;
        loopi(7)
        {
            int gun = ammohudcycle[i];
            if(gun < GUN_FIST || gun > GUN_PISTOL) continue;
            if(gun == d->gunselect) offset = i + 1;
            else if(d->ammo[gun]) num++;
        }
        float xcycle = (x+sz/2)*3.2f + 0.5f*num*sz, ycycle = y*3.2f-sz;
        loopi(7)
        {
            int gun = ammohudcycle[(i + offset)%7];
            if(gun < GUN_FIST || gun > GUN_PISTOL || gun == d->gunselect || !d->ammo[gun]) continue;
            xcycle -= sz;
            drawicon(HICON_FIST+gun, xcycle, ycycle, sz);
        }
        glPopMatrix();
    }

    void drawhudicons(fpsent *d)
    {
		if(su_selogo) return;
        glPushMatrix();
        glScalef(2, 2, 1);

        draw_textf("%d", (HICON_X + HICON_SIZE + HICON_SPACE)/2, HICON_TEXTY/2, d->state==CS_DEAD ? 0 : d->health);
        if(d->state!=CS_DEAD)
        {
            if(d->armour) draw_textf("%d", (HICON_X + HICON_STEP + HICON_SIZE + HICON_SPACE)/2, HICON_TEXTY/2, d->armour);
            draw_textf("%d", (HICON_X + 2*HICON_STEP + HICON_SIZE + HICON_SPACE)/2, HICON_TEXTY/2, d->ammo[d->gunselect]);
        }

        glPopMatrix();

        drawicon(HICON_HEALTH, HICON_X, HICON_Y);
        if(d->state!=CS_DEAD)
        {
            if(d->armour) drawicon(HICON_BLUE_ARMOUR+d->armourtype, HICON_X + HICON_STEP, HICON_Y);
            drawicon(HICON_FIST+d->gunselect, HICON_X + 2*HICON_STEP, HICON_Y);
            if(d->quadmillis) drawicon(HICON_QUAD, HICON_X + 3*HICON_STEP, HICON_Y);
            if(ammohud) drawammohud(d);
        }
    }



	void neuehud(int w, int h, fpsent *d) //new SauerEnhanced HUD
    {
        glPushMatrix();
        glScalef(1/1.2f, 1/1.2f, 1);
        if(!m_insta) draw_textf("%d", 80, h*1.2f-128, max(0, d->health));
        defformatstring(ammo)("%d", player1->ammo[d->gunselect]);
        int wb, hb;
        text_bounds(ammo, wb, hb);
        draw_textf("%d", w*1.2f-wb-80, h*1.2f-128, d->ammo[d->gunselect]);

        if(d->quadmillis)
        {
            settexture("packages/hud/hud_quaddamage_left.png");  //QuadDamage left glow
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f);   glVertex2f(0,   h*1.2f-207);
            glTexCoord2f(1.0f, 0.0f);   glVertex2f(539, h*1.2f-207);
            glTexCoord2f(1.0f, 1.0f);   glVertex2f(539, h*1.2f);
            glTexCoord2f(0.0f, 1.0f);   glVertex2f(0,   h*1.2f);
            glEnd();

            settexture("packages/hud/hud_quaddamage_right.png"); //QuadDamage right glow
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f);   glVertex2f(w*1.2f-135, h*1.2f-207);
            glTexCoord2f(1.0f, 0.0f);   glVertex2f(w*1.2f,     h*1.2f-207);
            glTexCoord2f(1.0f, 1.0f);   glVertex2f(w*1.2f,     h*1.2f);
            glTexCoord2f(0.0f, 1.0f);   glVertex2f(w*1.2f-135, h*1.2f);
            glEnd();
        }

        if(d->maxhealth > 100)
        {
            settexture("packages/hud/hud_megahealth.png");  //HealthBoost indicator
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f);   glVertex2f(0,   h*1.2f-207);
            glTexCoord2f(1.0f, 0.0f);   glVertex2f(539, h*1.2f-207);
            glTexCoord2f(1.0f, 1.0f);   glVertex2f(539, h*1.2f);
            glTexCoord2f(0.0f, 1.0f);   glVertex2f(0,   h*1.2f);
            glEnd();
        }

        int health = (d->health*100)/d->maxhealth,
            armour = (d->armour*100)/200,
            hh = (health*101)/100,
            ah = (armour*167)/100;

        float hs = (health*1.0f)/100,
              as = (armour*1.0f)/100;

        if(d->health > 0 && !m_insta)
        {
            settexture("packages/hud/hud_health.png");  //Health bar
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 1.0f-hs);   glVertex2f(47, h*1.2f-hh-56);
            glTexCoord2f(1.0f, 1.0f-hs);   glVertex2f(97, h*1.2f-hh-56);
            glTexCoord2f(1.0f, 1.0f);      glVertex2f(97, h*1.2f-57);
            glTexCoord2f(0.0f, 1.0f);      glVertex2f(47, h*1.2f-57);
            glEnd();
        }

        if(d->armour > 0)
        {
            settexture("packages/hud/hud_armour.png");  //Armour bar
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f,    0.0f);   glVertex2f(130,    h*1.2f-62);
            glTexCoord2f(as,      0.0f);   glVertex2f(130+ah, h*1.2f-62);
            glTexCoord2f(as,      1.0f);   glVertex2f(130+ah, h*1.2f-44);
            glTexCoord2f(0.0f,    1.0f);   glVertex2f(130,    h*1.2f-44);
            glEnd();
        }

        if(!m_insta)
        {
            settexture("packages/hud/hud_left.png"); //left HUD
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f);   glVertex2f(0,   h*1.2f-207);
            glTexCoord2f(1.0f, 0.0f);   glVertex2f(539, h*1.2f-207);
            glTexCoord2f(1.0f, 1.0f);   glVertex2f(539, h*1.2f);
            glTexCoord2f(0.0f, 1.0f);   glVertex2f(0,   h*1.2f);
            glEnd();
        }

        settexture("packages/hud/hud_right.png"); //right HUD
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f);   glVertex2f(w*1.2f-135, h*1.2f-207);
        glTexCoord2f(1.0f, 0.0f);   glVertex2f(w*1.2f,     h*1.2f-207);
        glTexCoord2f(1.0f, 1.0f);   glVertex2f(w*1.2f,     h*1.2f);
        glTexCoord2f(0.0f, 1.0f);   glVertex2f(w*1.2f-135, h*1.2f);
        glEnd();

        int maxammo = 1;

        switch(d->gunselect)
        {
            case GUN_FIST:
                maxammo = 1;
                break;

            case GUN_RL:
            case GUN_RIFLE:
                maxammo = m_insta ? 100 : 15;
                break;

            case GUN_SG:
            case GUN_GL:
                maxammo = 30;
                break;

            case GUN_CG:
                maxammo = 60;
                break;

            case GUN_PISTOL:
                maxammo = 120;
                break;
        }

        int curammo = min((d->ammo[d->gunselect]*100)/maxammo, maxammo),
            amh = (curammo*101)/100;

        float ams = (curammo*1.0f)/100;

        if(d->ammo[d->gunselect] > 0)
        {
            settexture("packages/hud/hud_health.png");  //Ammo bar
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 1.0f-ams);   glVertex2f(w*1.2f-47, h*1.2f-amh-56);
            glTexCoord2f(1.0f, 1.0f-ams);   glVertex2f(w*1.2f-97, h*1.2f-amh-56);
            glTexCoord2f(1.0f, 1.0f);       glVertex2f(w*1.2f-97, h*1.2f-57);
            glTexCoord2f(0.0f, 1.0f);       glVertex2f(w*1.2f-47, h*1.2f-57);
            glEnd();
        }
        glPopMatrix();
		glPushMatrix();

		glScalef(1/4.0f, 1/4.0f, 1);

		//Weapon icons

		defformatstring(icon)("packages/hud/guns/grey/%d.png", d->gunselect);
		settexture(icon);
		glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 0.0f);   glVertex2f(w*4.0f-1162,    h*4.0f-350);
		glTexCoord2f(1.0f, 0.0f);   glVertex2f(w*4.0f-650,     h*4.0f-350);
		glTexCoord2f(1.0f, 1.0f);   glVertex2f(w*4.0f-650,     h*4.0f-50);
		glTexCoord2f(0.0f, 1.0f);   glVertex2f(w*4.0f-1162,    h*4.0f-50);
		glEnd();
		glPopMatrix();
    }

	XVARP(su_statskpd,0,1,1);
	XVARP(su_statsfrags,0,1,1);
	XVARP(su_statsdeaths,0,1,1);
	XVARP(su_statsacc,0,1,1);
	XVARP(su_statsdamage,0,0,1);
	//VARP(su_statsshots,0,0,1);
	XVARP(su_statstime,0,1,1);
	XVARP(su_statsendstats,0,1,1);

	XVARP(su_statsposx,-400,10,3000);
	XVARP(su_statsposy,100,10,2300);

	XVARP(su_statscolor, 0, 7, 7);
	XVARP(su_statscolorw, 0, 4, 7);
	XFVARP(su_statssize, 70, 100, 180);


	void drawstats(fpsent *pla, int w, int h) 
	{
			int hh = 55 + su_statsposy;
			int starth = su_selogo == 1 ? 1450 : 1650;
            glPushMatrix();
			glTranslatef(w*1800/h, starth, 0);
			glScalef( (float(su_statssize) * 0.01f), (float(su_statssize) * 0.01f), 1);
			glTranslatef(-w*1800/h, -starth, 0);
			string statsstring1, statsstring2;
			int h1;
			int w1, w2;
			if(pla) { //else just draw the time
				if(su_statsendstats) {
					int howmany;
					float bruch = (m_overtime ? 900.0 : 600.0 ) / ((m_overtime ? 900.0 : 600.0) - (float(max(maplimit-lastmillis, 0)/1000.0)));
					howmany =  clamp(250, -250, int(bruch * pla->frags));
					formatstring(statsstring1)("\f%d~%d", su_statscolorw, howmany);
					formatstring(statsstring2)(" \f%dResult",su_statscolor);
					text_bounds(statsstring1, w1, h1); 
					text_bounds(statsstring2, w2, h1); 
					draw_text(statsstring1, w*1800/h-5 - w1- w2 - 43 - su_statsposx, starth - hh );	
					draw_text(statsstring2, w*1800/h-5 - w2 - su_statsposx, starth - hh);	
					hh+=55;
				}
				if(su_statskpd)	{
					int kpd = max(0, (pla->frags*100) / max(pla->deaths,1));
					formatstring(statsstring1)("\f%d%.2f", su_statscolorw, float(kpd)*0.01f);
					formatstring(statsstring2)(" \f%dKpD",su_statscolor);
					text_bounds(statsstring1, w1, h1); 
					text_bounds(statsstring2, w2, h1); 
					draw_text(statsstring1, w*1800/h-5 - w1- w2 - 97 - su_statsposx, starth - hh);	
					draw_text(statsstring2, w*1800/h-5 - w2 - su_statsposx, starth - hh);
					hh+=55;
				}
				if(su_statsdamage && !m_insta) {
					formatstring(statsstring1)("\f%d%d", su_statscolorw, pla->totaldamage);
					formatstring(statsstring2)(" \f%dDamage",su_statscolor);
					text_bounds(statsstring1, w1, h1); 
					text_bounds(statsstring2, w2, h1); 
					draw_text(statsstring1, w*1800/h-5 - w1- w2 + 10- su_statsposx, starth - hh);	
					draw_text(statsstring2, w*1800/h-5 - w2- su_statsposx, starth - hh);	
					hh+=55;
				}
				if(su_statsacc)	{
					int accuracy = min(100, pla->totaldamage*100/max(pla->totalshots, 1));
					formatstring(statsstring1)("\f%d%d", su_statscolorw, accuracy);
					formatstring(statsstring2)(" \f%dAcc",su_statscolor);
					text_bounds(statsstring1, w1, h1); 
					text_bounds(statsstring2, w2, h1); 
					draw_text(statsstring1, w*1800/h-5 - w1- w2 - 105- su_statsposx, starth - hh);	
					draw_text(statsstring2, w*1800/h-5 - w2- su_statsposx, starth - hh);	
					hh+=55;
				}
				

				if(su_statsdeaths) {
					formatstring(statsstring1)("\f%d%d", su_statscolorw, pla->deaths);
					formatstring(statsstring2)(" \f%dDeaths",su_statscolor);
					text_bounds(statsstring1, w1, h1); 
					text_bounds(statsstring2, w2, h1); 
					draw_text(statsstring1, w*1800/h-5 - w1- w2 - 22- su_statsposx, starth - hh);	
					draw_text(statsstring2, w*1800/h-5 - w2- su_statsposx, starth - hh);
					hh+=55;
				}

				if(su_statsfrags)	{
					formatstring(statsstring1)("\f%d%d", su_statscolorw, pla->frags);
					formatstring(statsstring2)(" \f%dFrags",su_statscolor);
					text_bounds(statsstring1, w1, h1); 
					text_bounds(statsstring2, w2, h1); 
					draw_text(statsstring1, w*1800/h-5 - w1- w2- 59- su_statsposx, starth - hh);	
					draw_text(statsstring2, w*1800/h-5 - w2- su_statsposx, starth - hh);
					hh+=55;
				}
			}

			if(su_statstime)	{
			//		glTranslatef(w*1800/h- w1-(intermission ? 10 :150)-lf +12, starth - hh -tf-20, 0);
				//	glScalef( 1.1, 1.1, 1);
			//		glTranslatef(-(w*1800/h- w1-(intermission ? 10 :150)-lf +12), -(starth - hh -tf-20), 0);
					int secs = max(maplimit-lastmillis, 0)/1000, mins = secs/60;
					secs %= 60;
					if(intermission) formatstring(statsstring1)(" \f3Intermission");
					else formatstring(statsstring1)("\f3%d:%02d", mins, secs);
					text_bounds(statsstring1, w1, h1); 

					draw_text(statsstring1, w*1800/h - w1-(intermission ? 10 :150)- su_statsposx +12, starth - hh -20);	
			}
			glPopMatrix();
	}		
	XVARP(su_hudline, 0, 1, 1);
	XVARP(su_hudlinepos, 0, 0, 4);	
	XVARP(su_hudlinecolor, 0, 2, 2);
	XVARP(su_hudlinescale, 3, 8, 18);
	XVARP(su_hudlineminisb, 0, 0, 1);
	void drawminiscoreboard(int w, int h, int posx, int posy)
	{
		if(!su_hudlineminisb) return;
		fpsent *f = followingplayer();
		if(!f) f = player1;
		fpsent *first = NULL;// *better, *worse;
		loopv(players)
		{
			fpsent *p = players[i];
			if(!p || p->state == CS_SPECTATOR) continue;
			if(!first) first = p;
			else if(p->frags > first->frags || ( p->frags == first->frags && p->deaths < first->deaths)) first = p;
		}
		int tw, th;
		text_bounds(" ", tw, th);

		glPushMatrix();
		glTranslatef(posx, posy, 0);
		glScalef(su_hudlinescale * 0.1f, su_hudlinescale * 0.1f, 1);
		glTranslatef(-posx, -posy, 0);
		
		if(m_teammode)
		{
			const char *winstatus = ownteamstatus();
			if(winstatus) {
				draw_textf("%s", posx, posy, winstatus);
				posy += th;
			}
		}
		
		if(first){
			draw_textf("1. %s %d", posx, posy, first->name, first->frags);
		}
		glPopMatrix();
	}
    void gameplayhud(int w, int h)
    {
        glPushMatrix();
		if(su_selogo) {
			        fpsent *d = hudplayer();
					if(d->state!=CS_EDITING) 
					neuehud(w, h, d);
		}
        glScalef(h/1800.0f, h/1800.0f, 1);
		int lineh, linew;
		text_bounds("  ", linew, lineh);
		int tw, th, fw, fh;
        text_bounds("SPECTATOR", tw, th);
        fpsent *f = followingplayer();
        text_bounds(f ? colorname(f) : " ", fw, fh);
        fh = max(fh, lineh);

        if(player1->state==CS_SPECTATOR)
        {
            draw_text("SPECTATOR", w*1100/h - tw/2 - linew, 0);
            if(f ) {
				draw_textf("%s \f%d(%d)", w*1100/h - fw/2 - linew, th, f->name, duplicatename(f, f->name)?5:4 ,f->clientnum);	
			}
			drawstats(f, w, h); //
        }
		else drawstats(player1, w, h); 
		if(su_hudline) {
			if(isclanwar(true, su_hudlinecolor) || isduel(true, su_hudlinecolor))
                {
				int tw, th;
				text_bounds(battleheadline, tw, th);
				int posx, posy;
				posx = w*1100/h - tw/2 - linew;
				posy = player1->state==CS_SPECTATOR ? 2*th : 0;

				glPushMatrix();
				glTranslatef(posx, posy, 0);
				glScalef(su_hudlinescale * 0.1f, su_hudlinescale * 0.1f, 1);
				glTranslatef(-posx, -posy, 0);

				draw_text(battleheadline, posx, posy );
				glPopMatrix();
                }
			else {
				drawminiscoreboard(w, h,  w*1100/h - (tw/2) - linew, player1->state==CS_SPECTATOR ? 2*th : 0);
            }
        }

        fpsent *d = hudplayer();
        if(d->state!=CS_EDITING)
        {
            if(d->state!=CS_SPECTATOR) drawhudicons(d);
            if(cmode) cmode->drawhud(d, w, h);
        }

        glPopMatrix();
    }

    int clipconsole(int w, int h)
    {
        if(cmode) return cmode->clipconsole(w, h);
        return 0;
    }

    VARP(teamcrosshair, 0, 1, 1);
    VARP(hitcrosshair, 0, 425, 1000);

    const char *defaultcrosshair(int index)
    {
        switch(index)
        {
            case 2: return "data/hit.png";
            case 1: return "data/teammate.png";
            default: return "data/crosshair.png";
        }
    }

	VAR(logo, 0, 1, 1);
    int selectcrosshair(float &r, float &g, float &b, int &w, int &h)
    {
        fpsent *d = hudplayer();
		dynent *o2 = intersectclosest(d->o, worldpos, d, intersectdist);
        if(d->state==CS_SPECTATOR || d->state==CS_DEAD) return -1;

        if(d->state!=CS_ALIVE) return 0;

        int crosshair = 0;
        if(lasthit && lastmillis - lasthit < hitcrosshair) crosshair = 2;
        else if(teamcrosshair)
        {
			int ht,hw;
            dynent *o = intersectclosest(d->o, worldpos, d);
            if(o && o->type==ENT_PLAYER && isteam(((fpsent *)o)->team, d->team))
            {
                crosshair = 1;
                r = g = 0;
				defformatstring(text)("%s",((fpsent *)o2)->name);
				text_bounds(text,hw,ht);				        
				if(logo) draw_text(text, w/2 - (hw/2), (h*0.9) -ht, !isteam(((fpsent*)o2)->team, d->team)? 255 : 0 , 0);
            }
        }

        if(crosshair!=1 && !editmode && !m_insta)
        {
            if(d->health<=25) { r = 1.0f; g = b = 0; }
            else if(d->health<=50) { r = 1.0f; g = 0.5f; b = 0; }
        }
        if(d->gunwait) { r *= 0.5f; g *= 0.5f; b *= 0.5f; }
        return crosshair;
    }

    void lighteffects(dynent *e, vec &color, vec &dir)
    {
#if 0
        fpsent *d = (fpsent *)e;
        if(d->state!=CS_DEAD && d->quadmillis)
        {
            float t = 0.5f + 0.5f*sinf(2*M_PI*lastmillis/1000.0f);
            color.y = color.y*(1-t) + t;
        }
#endif
    }

	///////////////////////////extended Serverbrowser /////////////////////
	extern int sortbythat;
	extern bool upsidedown;
	int hitcounter = 0;
    bool serverinfostartcolumn(g3d_gui *g, int i)
    {
        static const char * const names[] = { "ping ", "players ", "mode ", "map ", "time ", "master ", "host ", "port ", "description " };
        static const float struts[] =       { 7,       7,          12.5f,   14,      7,      8,         14,      7,       24.5f };
        if(size_t(i) >= sizeof(names)/sizeof(names[0])) return false;
        g->pushlist();
		if(g->button(names[i], 0xFF00D0)&G3D_UP)
		{ 
			if(sortbythat == i+1) 
			{
				if(hitcounter %2)
					upsidedown = false;
				else
					upsidedown = true;
				hitcounter++;
			}
			else {
				upsidedown = false;
				hitcounter = 0;
			}
			sortbythat = i+1;
		}
		g->strut(struts[i]);
		g->mergehits(true);
		return true;
	}
	bool playerinfostartcolumn(g3d_gui *g, int i) //lists players who are searched
	{
		static const char *names[] = { "name", "frags", "map", "mode" ,"host", "port", "description" };
		static const int struts[] = {	15,		5,		12,		12,		13,		6,		24 };
		if(size_t(i) > sizeof(names)/sizeof(names[0])) return false;
		g->pushlist();
		g->text(names[i], 0xB06FA5);
		g->strut(struts[i]);
        g->mergehits(true);
        return true;
    }

	bool playerinfoentry(g3d_gui *g, int i, const char *name, int frags, const char *host, int port, const char *sdesc, const char *map, const vector<int> &attr, int ping, int np)
	{
		if(ping < 0 || attr.empty() || attr[0]!=PROTOCOL_VERSION)
		{
			return false;
		}

		switch(i)
		{
		case 0:
			{
				const char *icon = attr.inrange(3) && np >= attr[3] ? "serverfull" : (attr.inrange(4) ? mastermodeicon(attr[4], "serverunk") : "serverunk");
				if(g->buttonf("%s ", 0xFFFFDD, icon, NULL,name)&G3D_UP) return true;
				break;
			}

		case 1:
			if(g->buttonf("%d ", 0xFFFFDD, NULL, NULL, frags)&G3D_UP) return true;
			break;

		case 2:
			if(g->buttonf("%.25s ", 0xFFFFDD, NULL, NULL,map)&G3D_UP) return true;
			break;

		case 3:
			if(g->buttonf("%s ", 0xFFFFDD, NULL, NULL,attr.length()>=2 ? server::modename(attr[1], "") : "")&G3D_UP) return true;
			break;

		case 4:
			if(g->buttonf("%s ", 0xFFFFDD, NULL, NULL,host)&G3D_UP) return true;
			break;

		case 5:
			if(g->buttonf("%d ", 0xFFFFDD, NULL, NULL,port)&G3D_UP) return true;
			break;

		case 6:
			if(g->buttonf("%.25s", 0xFFFFDD, NULL, NULL,sdesc)&G3D_UP) return true;
			break;
		}
		return false;
	}
	////////////////////////////////////////////////////

    void serverinfoendcolumn(g3d_gui *g, int i)
    {
        g->mergehits(false);
        g->column(i);
        g->poplist();
    }

    const char *mastermodecolor(int n, const char *unknown)
    {
        return (n>=MM_START && size_t(n-MM_START)<sizeof(mastermodecolors)/sizeof(mastermodecolors[0])) ? mastermodecolors[n-MM_START] : unknown;
    }

    const char *mastermodeicon(int n, const char *unknown)
    {
        return (n>=MM_START && size_t(n-MM_START)<sizeof(mastermodeicons)/sizeof(mastermodeicons[0])) ? mastermodeicons[n-MM_START] : unknown;
    }

    bool serverinfoentry(g3d_gui *g, int i, const char *name, int port, const char *sdesc, const char *map, int ping, const vector<int> &attr, int np)
    {
        if(ping < 0 || attr.empty() || attr[0]!=PROTOCOL_VERSION)
        {
            switch(i)
            {
                case 0:
                    if(g->button(" ", 0xFFFFDD, "serverunk")&G3D_UP) return true;
                    break;

                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                    if(g->button(" ", 0xFFFFDD)&G3D_UP) return true;
                    break;

                case 6:
				if(g->buttonf("%s ", 0xFFFFDD, NULL, NULL,name)&G3D_UP) return true;
                    break;

                case 7:
				if(g->buttonf("%d ", 0xFFFFDD, NULL, NULL,port)&G3D_UP) return true;
                    break;

                case 8:
                    if(ping < 0)
                    {
                        if(g->button(sdesc, 0xFFFFDD)&G3D_UP) return true;
                    }
				else if(g->buttonf("[%s protocol] ", 0xFFFFDD, NULL,NULL, attr.empty() ? "unknown" : (attr[0] < PROTOCOL_VERSION ? "older" : "newer"))&G3D_UP) return true;
                    break;
            }
            return false;
        }

        switch(i)
        {
            case 0:
            {
                const char *icon = attr.inrange(3) && np >= attr[3] ? "serverfull" : (attr.inrange(4) ? mastermodeicon(attr[4], "serverunk") : "serverunk");
				if(g->buttonf("%d ", 0xFFFFDD, icon, NULL,ping)&G3D_UP) return true;
                break;
            }

            case 1:
                if(attr.length()>=4)
                {
				if(g->buttonf(np >= attr[3] ? "\f3%d/%d " : "%d/%d ", 0xFFFFDD, NULL, NULL,np, attr[3])&G3D_UP) return true;
                }
			else if(g->buttonf("%d ", 0xFFFFDD, NULL, NULL,np)&G3D_UP) return true;
                break;

            case 2:
			if(g->buttonf("%s ", 0xFFFFDD, NULL, NULL,attr.length()>=2 ? server::modename(attr[1], "") : "")&G3D_UP) return true;
                break;

            case 3:
			if(g->buttonf("%.25s ", 0xFFFFDD, NULL,NULL, map)&G3D_UP) return true;
                break;

            case 4:
                if(attr.length()>=3 && attr[2] > 0)
                {
                    int secs = clamp(attr[2], 0, 59*60+59),
                        mins = secs/60;
                    secs %= 60;
				if(g->buttonf("%d:%02d ", 0xFFFFDD, NULL, NULL,mins, secs)&G3D_UP) return true;
                }
			else if(g->button(" ", 0xFFFFDD)&G3D_UP) return true;
                break;
            case 5:
			if(g->buttonf("%s%s ", 0xFFFFDD, NULL,NULL, attr.length()>=5 ? mastermodecolor(attr[4], "") : "", attr.length()>=5 ? server::mastermodename(attr[4], "") : "")&G3D_UP) return true;
                break;

            case 6:
			if(g->buttonf("%s ", 0xFFFFDD, NULL,NULL, name)&G3D_UP) return true;
                break;

            case 7:
			if(g->buttonf("%d ", 0xFFFFDD, NULL,NULL, port)&G3D_UP) return true;
                break;

            case 8:
			if(g->buttonf("%.25s", 0xFFFFDD, NULL,NULL, sdesc)&G3D_UP) return true;
                break;
        }
        return false;
    }

    // any data written into this vector will get saved with the map data. Must take care to do own versioning, and endianess if applicable. Will not get called when loading maps from other games, so provide defaults.
    void writegamedata(vector<char> &extras) {}
    void readgamedata(vector<char> &extras) {}

    const char *savedconfig() { return "config.cfg"; }
    const char *restoreconfig() { return "restore.cfg"; }
    const char *defaultconfig() { return "data/defaults.cfg"; }
    const char *autoexec() { return "autoexec.cfg"; }
    const char *savedservers() { return "servers.cfg"; }

    void loadconfigs()
    {
        execfile("auth.cfg", false);
    }
}
namespace preview {
	////////////////////////////////////////extended serverbrowser ///////////////////////////////

	struct extinfo {
		//int gametime;
		int teamping,playerping,playerids;
        bool getnow;
		bool teamresp,playerresp;
		//int gamemode;
		bool /*teammode,*/ ignore;
		vector<extteam *> teams;
		vector<extplayer *> players,spectators;
		extinfo() : teamping(0),playerping(0),playerids(0),getnow(true),teamresp(true),playerresp(true),ignore(false) {}
	};

	void clearservercontent(serverinfo *si)
	{
		extinfo *x = (extinfo *)si->extinfo;
        if(x) {
		    x->teams.shrink(0);
		    x->spectators.shrink(0);
		    x->players.shrink(0);
            x->teamping = 0; 
            x->playerping = totalmillis - 900;
            x->getnow = false;
            x->teamresp = true;
        }
		si->reset();
	}
	void getstatsofpl(int cn, int &tks, int &deaths, int &damage, int &shots) //sync fpsent variables like acc and deaths with serverinfo entry (only flags and frags are initially send)
	{
		if(!isconnected(false, false)) return;
		loopvk(servers)
		{
			if(servers[k]->address.host != connectedpeer()->host || servers[k]->address.port-connectedpeer()->port != 1) continue;

				if(!servers[k]->numplayers) return;
				extinfo * x = NULL;
				if(servers[k]->extinfo == NULL) continue;
				else x = (extinfo *)servers[k]->extinfo;


				loopv(x->players)
				{
					if(x->players[i]->cn == cn)
					{

						deaths = x->players[i]->deaths;
						//if(m_insta) 
						{
							damage = max(x->players[i]->frags*100, 0);
							shots = max(100*damage/max(x->players[i]->accuracy, 1), 0);
						}
						tks = x->players[i]->teamkills;
						return;
					}
				}
				loopvk(x->spectators)
					if(x->spectators[k]->cn == cn)
					{
						deaths = x->spectators[k]->deaths;
						//if(m_insta) 
						{
							damage = x->players[k]->frags*100;
							shots = damage/max((float(x->players[k]->accuracy)*0.01f), 1.0f);
						}
						tks = x->spectators[k]->teamkills;
						return;
					}
		}
	}

	//void getallstats() //get all player stats from the serverinfo, only working after serverbrowser was open and updated the serverinfo
	//{
	//	loopv(players) getstatsofpl(players[i]);
	//}
	//COMMAND(getallstats,"");

	void assignplayers(serverinfo * si)
	{
        if(!si || !si->extinfo) return;
        extinfo * x = (extinfo *)si->extinfo;
		loopv(x->teams) x->teams[i]->players.setsize(0);
        if(x->teams.length() == 0)  x->teams.add(new extteam());

		loopv(x->players) {
			extplayer * pl = x->players[i];
			loopvk(x->teams) if(pl->state != CS_SPECTATOR)
			{
				if(x->teams.length()==1)						x->teams[0]->players.add(pl); //just one team -> all players in
				else if(!strncmp(pl->team,x->teams[k]->name,5))	x->teams[k]->players.add(pl);
			}
		}
		x->spectators.setsize(0);
		loopv(x->players) {
			if(x->players[i]->state == CS_SPECTATOR) x->spectators.add(x->players[i]);
		}
		loopv(x->teams) x->teams[i]->players.sort(extplayer::compare);
		x->teams.sort(extteam::compare);
	}
    
	bool extinfoparse(ucharbuf & p, serverinfo * si) {
		//if(!(si->attr.length() && si->attr[0]==PROTOCOL_VERSION)) return false;
		if(getint(p) != 0) return false;
		extplayer * pl = NULL;
		extteam * tm = NULL;
		extinfo * x = NULL;


		int teamorplayer = getint(p);
        p.len = 6;
		int op = getint(p);
        if(op==0) return true;
         if(si->extinfo == NULL) {
			x = new extinfo;
			si->extinfo = x;
		} else x = (extinfo *)si->extinfo;
		if((!strcmp(si->name, NOOBLOUNGEIP) && teamorplayer == 101) || (strcmp(si->name, NOOBLOUNGEIP) && teamorplayer == 1)) { //playerinfo
			//Failsafe sanity check to prevent overflows
			if(x->ignore) return true;
			if(x->players.length() > MAXCLIENTS || x->teams.length() > MAXCLIENTS) {
				x->ignore = true;
				loopv(x->players) {
					pl = x->players[i];
					x->players.remove(i);
					delete pl;
				}
				loopv(x->teams) {
					tm = x->teams[i];
					x->teams.remove(i);
					delete tm;
				}
			}

			char text[MAXTRANS];   

			if(op == -10) {
				//check id list agains current list of known players and remove missing
				x->playerping = totalmillis;
				vector <int> cnlist;
				loopi(si->numplayers) {
					int cn = getint(p);
					cnlist.add(cn);
				}
				loopv(x->players) if(cnlist.find(x->players[i]->cn) == -1) {
					delete x->players[i];
					x->players.remove(i);		
				}
				loopv(x->spectators) if(cnlist.find(x->spectators[i]->cn) == -1) {
					x->spectators.remove(i);	
				}
				x->playerids = cnlist.length();
			}
			else if(op == -11) {
				x->playerping = lastmillis;
				int cn = getint(p);
				//get old, or create new player
				loopv(x->players) if(x->players[i]->cn == cn) pl = x->players[i];
				if(!pl) {
					pl = new extplayer;
					pl->cn = cn;
					x->players.add(pl);
				}

				pl->ping = getint(p);
				getstring(text,p);//name
				strncpy(pl->name,text,20);
				getstring(text,p);//team
				strncpy(pl->team,text, 5);
				pl->frags = getint(p);
				pl->flags = getint(p);
				pl->deaths = getint(p);
				pl->teamkills = getint(p);
				pl->accuracy = getint(p);
				p.len +=2; //health armor
                pl->gunselect = getint(p);
				pl->privilege = getint(p);
				pl->state = getint(p);

				if(x->playerids == x->players.length()) x->playerresp = true;
			}
		}
		else if((!strcmp(si->name, NOOBLOUNGEIP) && teamorplayer == 102) || (strcmp(si->name, NOOBLOUNGEIP) && teamorplayer == 2)) { //extteam
			x->teamping = totalmillis;
            x->getnow = false;
			x->teamresp = true;
			char text[MAXTRANS];
			vector<extteam *> tmlist;
			vector<int> bases;
			p.len = 5;
			//x->gamemode = getint(p);
			//x->gametime = getint(p);
            if(si->attr.length()>=2) si->attr[1] = getint(p); //gamemode
            if(si->attr.length()>=3) si->attr[2] = getint(p); //gametime

			while(p.remaining()) {
				tm = NULL;
				getstring(text,p);
				int score = getint(p);
				int numbases = getint(p);
				if(numbases > -1) {
					if(numbases > MAXCLIENTS) return true; //sanity check
                    loopi(numbases) getint(p);
					//loopi(numbases) bases.add(getint(p));
				}

				loopv(x->teams) if(!strncmp(x->teams[i]->name,text,5)) {
					tm = x->teams[i];
				}
				if(!tm) {
					tm = new extteam();
					strncpy(tm->name,text,260);
					x->teams.add(tm);
				}
				tmlist.add(tm);
				tm->score = score;
				tm->numbases = numbases;
				//tm->bases = bases;
			}
			//delete missing teams
			loopv(x->teams) if(tmlist.find(x->teams[i])==-1) {
				delete x->teams[i];
				x->teams.remove(i);
			}
			if(!x->teams.length()) { //// 
				tm = NULL;
				tm = new extteam();
				x->teams.add(tm);
			} //
		}
		return true;
	}

	void extinforequest(ENetSocket &pingsock, ENetAddress & address,serverinfo * si, bool watching) {
		//if(si->attr.empty() || si->attr[0]!=PROTOCOL_VERSION) return;
		extinfo * x = NULL;
		if(si != NULL)x = (extinfo *)si->extinfo;
		if(x != NULL && x->ignore) return;
        //if(lastmillis - x->playerping < 1000 || (lastmillis- x->playerping < 10000 && !x->playerresp)
        //    || (lastmillis - x->teamping < 2000) || x->getnow) return;
		ENetBuffer playerbuf,teambuf;
		uchar player[MAXTRANS], team[MAXTRANS];
		ucharbuf pl(player, sizeof(player));
		ucharbuf tm(team, sizeof(team));
		if(si != NULL && !strcmp(si->name, NOOBLOUNGEIP))//nooblounge handshake
		{
			putint(pl, 0);
			putint(pl, 101);
			putint(pl, -1);
			putint(tm, 0);
			putint(tm, 102);
		}
		else { //normal handshake
			putint(pl, 0);
			putint(pl, 1);
			putint(pl, -1);
			putint(tm, 0);
			putint(tm, 2);
		}
		if(x == NULL || (totalmillis - x->playerping > 2000 && x->playerresp) || totalmillis - x->playerping > 10000 || (watching && totalmillis - x->playerping > 1000)) 
        {//only needed every second and wait for response before requesting again in case of errors, re-request every 10 seconds.
			if(x != NULL) {
				x->playerping = 0;
				x->playerresp = false;
			}
			playerbuf.data = player;
			playerbuf.dataLength = pl.length();
			enet_socket_send(pingsock, &address, &playerbuf, 1);
		}

		if(x == NULL || (totalmillis - x->teamping > 3000 && !x->getnow && x->teamresp) || (totalmillis - x->teamping > 1000 && !x->getnow && x->teamresp && watching)) 
        {//only needed every two second and wait for response before requesting again
			if(x != NULL) {
				x->getnow = true;
				x->teamresp = false;
			}
			teambuf.data = team;
			teambuf.dataLength = tm.length();
			enet_socket_send(pingsock, &address, &teambuf, 1);
		}
	}
    bool debugnow = false;
    void debugextinfo()
    {
        debugnow = true;
    }
    COMMAND(debugextinfo, "");

	const char *listteams(g3d_gui *cgui, vector<extteam *> &teams, int mode, bool icons, bool forcealive, bool frags, bool deaths, bool tks, bool acc, bool flags, bool cn, bool ping) 
	{
			loopvk(teams)
			{
				if((k%2)==0) cgui->pushlist(); // horizontal


				extteam * tm = teams[k];
				int bgcolor = 0x3030C0, fgcolor = 0xFFFF80;
				int mybgcolor = 0xC03030;

#define loopscoregroup(o, b) \
	loopv(tm->players) \
				{ \
				extplayer * o = tm->players[i]; \
				b; \
				}    

				if(tm->name[0] && m_check(mode, M_TEAM))
				{
					cgui->pushlist(); // vertical

					cgui->pushlist();
					cgui->background( (!k && teams.length()>1) ? bgcolor: mybgcolor, 1, 0);
					if(tm->score>=10000) cgui->textf("   %s: WIN", fgcolor, NULL,NULL, tm->name);
					else cgui->textf("   %s: %d", fgcolor, NULL,NULL, tm->name, tm->score);
					cgui->poplist();

					cgui->pushlist(); // horizontal
				}
				if(icons)
				{
					cgui->pushlist();
					cgui->text(" ", 0, " ");
					loopscoregroup(o, 
						const game::playermodelinfo *mdl = game::getplayermodelinfo(o->playermodel);
						const char *icon = m_check(mode, M_TEAM) ? (!k ? mdl->blueicon : mdl->redicon) : mdl->ffaicon;
						cgui->text("", 0, icon);
					);
					cgui->poplist();	
				}
				if(frags)
				{ 
					cgui->pushlist();
					cgui->strut(7);
					cgui->text("frags", fgcolor);
					loopscoregroup(o, cgui->textf("%d", 0xFFFFDD, NULL,NULL, o->frags));
					cgui->poplist();
				}


				if(deaths)
				{ 
					cgui->pushlist();
					cgui->strut(7);
					cgui->text("deaths", fgcolor);
					loopscoregroup(o, cgui->textf("%d", 0xFFFFDD,NULL, NULL, o->deaths));
					cgui->poplist();
				}

				if(tks)
				{ 
					cgui->pushlist();
					cgui->strut(7);
					cgui->text("tks", fgcolor);
					loopscoregroup(o, cgui->textf("%d", o->teamkills >= 5 ? 0xFF0000 : 0xFFFFDD,NULL, NULL, o->teamkills));
					cgui->poplist();
				}

				if(acc)
				{ 
					cgui->pushlist();
					cgui->strut(7);
					cgui->text("acc", fgcolor);
					loopscoregroup(o, cgui->textf("%d%%", 0xFFFFDD, NULL,NULL, o->accuracy));
					cgui->poplist();
				}

				if(flags && (m_check(mode, M_CTF) || m_check(mode, M_HOLD) || m_check(mode, M_PROTECT)))
				{ 
					cgui->pushlist();
					cgui->strut(7);
					cgui->text("flags", fgcolor);
					loopscoregroup(o, cgui->textf("%d", 0xFFFFDD, NULL,NULL, o->flags));
					cgui->poplist();
				}

				if(ping)
				{
					cgui->pushlist();
					cgui->text("ping", fgcolor);
					cgui->strut(6);
					loopscoregroup(o, 
					{
						if(o->state==CS_LAGGED) cgui->text("LAG", 0xFFFFDD);
						else cgui->textf("%d", 0xFFFFDD,NULL, NULL, o->ping);
					});
					cgui->poplist();
				}

				cgui->pushlist();
				cgui->text("name", fgcolor);
				cgui->strut(10);
				loopscoregroup(o, 
				{
					int status = o->state!=CS_DEAD || forcealive ? 0xFFFFDD : 0x606060;
					if(o->privilege)
					{
						status = o->privilege>=3 ? 0xFF8000 : (o->privilege==2 ? 0x40FFA0 : 0x40FF80 );
						if(o->state==CS_DEAD && !forcealive) status = (status>>1)&0x7F7F7F;
					}
					if(o->cn < MAXCLIENTS){
						cgui->textf("%s", status, 0,NULL, o->name);
					}
					else cgui->textf("%s \f5[%i]", status, NULL,NULL,o->name,o->cn);
				});
				cgui->poplist();

				if(cn)
				{
					cgui->space(1);
					cgui->pushlist();
					cgui->text("cn", fgcolor);
					loopscoregroup(o, cgui->textf("%d", 0xFFFFDD, NULL,NULL, o->cn));
					cgui->poplist();
				}
				if(tm->name[0] && m_check(mode, M_TEAM))
				{
					cgui->poplist(); // horizontal
					cgui->poplist(); // vertical
				}

				if(k+1<teams.length() && (k+1)%2) cgui->space(2);
				else cgui->poplist(); // horizontal
			}
			return NULL;
	}
	const char *listspectators(g3d_gui *cgui, vector<extplayer *> &spectators, bool cn, bool ping) 
	{
		cgui->pushlist();
			cgui->pushlist();
				cgui->text("spectator", 0xFFFF80);
				loopv(spectators) 
				{
					extplayer *pl = spectators[i];
					int status = 0xFFFFDD;
					if(pl->privilege) status = pl->privilege>=2 ? 0xFF8000 : 0x40FF80;
					cgui->text(pl->name, status, "spectator");
				}
			cgui->poplist();

			if(cn) {
				cgui->space(1);
				cgui->pushlist();
				cgui->text("cn", 0xFFFF80);
				loopv(spectators) cgui->textf("%d", 0xFFFFDD, NULL, NULL,spectators[i]->cn);
				cgui->poplist();
			}
			if(ping) {
				cgui->space(1);
				cgui->pushlist();
				cgui->text("ping", 0xFFFF80);
				loopv(spectators) cgui->textf("%d", 0xFFFFDD, NULL,NULL, spectators[i]->ping);
				cgui->poplist();
			}
		cgui->poplist();
		return NULL;
	}

	char *serverextinfo(g3d_gui *cgui, serverinfo * si) {
        if(si == NULL) return NULL;
        if(debugnow)
        {
            if(si->extinfo == NULL) conoutf("keine extinfo");
            else {
			    extinfo * x = (extinfo *)si->extinfo;
                conoutf("teams: %d", x->teams.length());
                loopv(x->teams) conoutf("team %d (%s), players: %d", i, x->teams[i]->name, x->teams[i]->players.length());
                conoutf("ping: teamresp %s, zähler = %d (teamping %d)", x->teamresp ? "true" : "false", (totalmillis - x->teamping), x->teamping);
            };
            debugnow = false;
        }
		cgui->pushlist(); //0
		cgui->spring(); 
		if(cgui->button("", 0xFFFFFF, "update")&G3D_UP) {
			clearservercontent(si);
		}
		cgui->space(14);
		cgui->titlef("%.25s", 0xFFEE80, NULL,NULL, si->sdesc);
		cgui->space(2);
		if(cgui->buttonf("%s:%d", 0x808080, NULL,NULL, si->name, si->port)&G3D_UP)
		{
			defformatstring(connectkey) ("/connect %s %d", si->name, si->port);
			game::toserver(connectkey);
		}

		cgui->spring();
		cgui->poplist();
		if(si->extinfo != NULL) {
			extinfo * x = (extinfo *)si->extinfo;
			assignplayers(si);
			cgui->pushlist(); //0
			cgui->spring();
			cgui->text(si->attr.length()>=2 ? server::modename(si->attr[1]) : "error", 0xFFFF80);
			cgui->separator();
			const char *mname = si->map;
			cgui->text(mname[0] ? mname : "[new map]", 0xFFFF80);
			cgui->separator();
			if(((si->attr.length()>=3) && si->attr[2] == 0) || !x->players.length()) cgui->text("intermission", 0xFFFF80);
			else
			{
				int subsecs = (totalmillis-x->teamping)/1000, secs = (si->attr.length()>=3 ? si->attr[2] : subsecs)-subsecs , mins = secs/60;
                secs %= 60;
				cgui->pushlist();
				cgui->strut(mins >= 10 ? 4.5f : 3.5f);
				cgui->textf("%d:%02d", 0xFFFF80, NULL, NULL,mins, secs);
				cgui->poplist();
			}
			//if(paused || ispaused()) { g.separator(); g.text("paused", 0xFFFF80); } //no pause/unpause sent in extinfo
			cgui->spring();
			cgui->poplist();

			cgui->separator();

			listteams(cgui, x->teams, si->attr.length()>=2 ? si->attr[1] : 0, false, false, su_sbfrags!=0, su_sbdeaths!=0, su_sbteamkills!=0, su_sbaccuracy!=0, su_sbflags!=0, su_sbclientnum!=0, su_sbping!=0);
			
			if(x->spectators.length() > 0)
			{
				listspectators(cgui, x->spectators, su_sbclientnum!=0, su_sbping!=0);
			}
		} //end scoreboard
        return NULL;
	}

	SVAR(filter,"");

	vector <foundplayer *> foundplayers;
	void addfoundpl(extplayer *pl, serverinfo *si, char *clan = (char *)NULL)
	{
		loopv(foundplayers) if( !strcmp(pl->name, foundplayers[i]->name) 
			&& si->address.host == foundplayers[i]->si->address.host 
			&& si->address.port == foundplayers[i]->si->address.port)
			return;
		foundplayer *fp = new foundplayer;
		strncpy(fp->name, pl->name, 16);
		fp->name[15] = '\0';
		if(clan) strncpy(fp->clan, clan, 10);
		fp->frags = pl->frags;
		fp->cn = pl->cn;
		fp->si = si;
		foundplayers.add(fp);
	}
	void clearfoundplayers() 
	{
        loopv(foundplayers) delete foundplayers[i];
		foundplayers.setsize(0);
	}
	bool playerfilter(serverinfo * si) {
		extinfo * x = (extinfo *)si->extinfo;
		if(!filter[0]) return false; //no player browser when no filtering is active!
		if(x == NULL) return false;
		char *myfilter = klbuchstaben(filter);
		loopv(x->players) {
			extplayer *pl = x->players[i];
			if(!strcmp(filter, "clans")) loopvj(game::clantags)if(strstr(pl->name, game::clantags[j]->name)) //search for clans
			{ 
				addfoundpl(pl, si, game::clantags[j]->name); continue; 
			} 
			if(!strcmp(filter, "friends")) loopvj(game::friends)if(strstr(pl->name, game::friends[j]->name)) //search for friends
			{ 
				addfoundpl(pl, si); continue; 
			} 
				char *pllowcasecountry = klbuchstaben(pl->country);
				if(!strcmp(pllowcasecountry, myfilter)) {
					addfoundpl(pl, si);
					continue;
				}
				char *pllowcasename = klbuchstaben(pl->name);
				if(strstr(pllowcasename,myfilter)) { 
					addfoundpl(pl, si);
				}

		}
		return true;
	}

	int serverfilter(serverinfo * si) {
		extinfo * x = (extinfo *)si->extinfo;
		if(!filter[0]) return true;
		if(x == NULL) return false;
		int is = 0;
		char *myfilter = klbuchstaben(filter);
		loopv(x->players) {
			if(!strcmp(filter, "clans")) loopvj(game::clantags)if(strstr(x->players[i]->name, game::clantags[j]->name)) { is++; continue; } //search for clans
			if(!strcmp(filter, "friends")) loopvj(game::friends)if(strstr(x->players[i]->name, game::friends[j]->name)) { is++; continue; } //search for friends

				char *pllowcasename = klbuchstaben(x->players[i]->name);
				if(strstr(pllowcasename,myfilter)) is++;
				DELETEP(pllowcasename)
		}

			char *lowcasedesc = klbuchstaben(si->sdesc);
			if(strstr(lowcasedesc,myfilter)) is++;	
			DELETEP(lowcasedesc)

		if(si->attr.length()>=5){
			if(strstr(server::modename(si->attr[1]), filter)) is++;
			if(strstr(server::mastermodename(si->attr[4]), filter)) is++;
		}
		DELETEP(myfilter)
		return is;
	}
	///////////////////////////////////
}
