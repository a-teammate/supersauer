// creation of scoreboard
#include "game.h"
	XVARP(su_showfrags, 0, 1, 1); 
	XVARP(su_showkpd, 0, 0, 1);
	XVARP(su_showflags, 0, 0, 1); 
	XVARP(su_showdeaths, 0, 0, 1);
	XVARP(su_showaccuracy, 0, 1, 1);  

namespace game
{
    VARP(scoreboard2d, 0, 1, 1);
    VARP(showservinfo, 0, 1, 1);
    VARP(showclientnum, 0, 0, 1);
    VARP(showpj, 0, 0, 1);
    VARP(showping, 0, 1, 1);
    VARP(showspectators, 0, 1, 1);
    VARP(highlightscore, 0, 1, 1);
    VARP(showconnecting, 0, 0, 1);

    static hashset<teaminfo> teaminfos;

    void clearteaminfo()
    {
        teaminfos.clear();
    }

    void setteaminfo(const char *team, int frags)
    {
        teaminfo *t = teaminfos.access(team);
        if(!t) { t = &teaminfos[team]; copystring(t->team, team, sizeof(t->team)); }
        t->frags = frags;
    }
            
    static inline bool playersort(const fpsent *a, const fpsent *b)
    {
        if(a->state==CS_SPECTATOR)
        {
            if(b->state==CS_SPECTATOR) return strcmp(a->name, b->name) < 0;
            else return false;
        }
        else if(b->state==CS_SPECTATOR) return true;
        if(m_ctf || m_collect)
        {
            if(a->flags > b->flags) return true;
            if(a->flags < b->flags) return false;
        }
        if(a->frags > b->frags) return true;
        if(a->frags < b->frags) return false;
        return strcmp(a->name, b->name) < 0;
    }

    void getbestplayers(vector<fpsent *> &best)
    {
        loopv(players)
        {
            fpsent *o = players[i];
            if(o->state!=CS_SPECTATOR) best.add(o);
        }
        best.sort(playersort);
        while(best.length() > 1 && best.last()->frags < best[0]->frags) best.drop();
    }

    void getbestteams(vector<const char *> &best)
    {
        if(cmode && cmode->hidefrags()) 
        {
            vector<teamscore> teamscores;
            cmode->getteamscores(teamscores);
            teamscores.sort(teamscore::compare);
            while(teamscores.length() > 1 && teamscores.last().score < teamscores[0].score) teamscores.drop();
            loopv(teamscores) best.add(teamscores[i].team);
        }
        else 
        {
            int bestfrags = INT_MIN;
            enumerates(teaminfos, teaminfo, t, bestfrags = max(bestfrags, t.frags));
            if(bestfrags <= 0) loopv(players)
            {
                fpsent *o = players[i];
                if(o->state!=CS_SPECTATOR && !teaminfos.access(o->team) && best.htfind(o->team) < 0) { bestfrags = 0; best.add(o->team); } 
            }
            enumerates(teaminfos, teaminfo, t, if(t.frags >= bestfrags) best.add(t.team));
        }
    }

    struct scoregroup : teamscore
    {
        vector<fpsent *> players;
        char *isclan()
        {
            string clanname;
            int members = 0;
            loopv(players)
            {
                if(members){ if(strstr(players[i]->name, clanname)) members++; }
                else loopvk(clantags)
                {
                        if(strstr(players[i]->name, clantags[k]->name))  
                        {  
                            strcpy(clanname, clantags[k]->name); 
                            members ++;
                        }
                }
            }
            return (members*3) >= (players.length()*2) && players.length()>=2 ? newstring(clanname) : NULL;
        }
    };
    static vector<scoregroup *> groups;
    static vector<fpsent *> spectators;

    static inline bool scoregroupcmp(const scoregroup *x, const scoregroup *y)
    {
        if(!x->team)
        {
            if(y->team) return false;
        }
        else if(!y->team) return true;
        if(x->score > y->score) return true;
        if(x->score < y->score) return false;
        if(x->players.length() > y->players.length()) return true;
        if(x->players.length() < y->players.length()) return false;
        return x->team && y->team && strcmp(x->team, y->team) < 0;
    }

    static int groupplayers()
    {
        int numgroups = 0;
        spectators.setsize(0);
        loopv(players)
        {
            fpsent *o = players[i];
            if(!showconnecting && !o->name[0]) continue;
            if(o->state==CS_SPECTATOR) { spectators.add(o); continue; }
            const char *team = m_teammode && o->team[0] ? o->team : NULL;
            bool found = false;
            loopj(numgroups)
            {
                scoregroup &g = *groups[j];
                if(team!=g.team && (!team || !g.team || strcmp(team, g.team))) continue;
                g.players.add(o);
                found = true;
            }
            if(found) continue;
            if(numgroups>=groups.length()) groups.add(new scoregroup);
            scoregroup &g = *groups[numgroups++];
            g.team = team;
            if(!team) g.score = 0;
            else if(cmode && cmode->hidefrags()) g.score = cmode->getteamscore(o->team);
            else { teaminfo *ti = teaminfos.access(team); g.score = ti ? ti->frags : 0; }
            g.players.setsize(0);
            g.players.add(o);
        }
        loopi(numgroups) groups[i]->players.sort(playersort);
        spectators.sort(playersort);
        groups.sort(scoregroupcmp, 0, numgroups);
        return numgroups;
    }

	const char *ownteamstatus()
	{
        int numgroups = groupplayers();
		if(!m_teammode || numgroups < 2) return NULL;
		
		scoregroup *ownteam;
		fpsent *f = followingplayer();
		if(!f && player1->state != CS_SPECTATOR) f = player1;
		if(!f && player1->state == CS_SPECTATOR) return NULL;
		
		loopv(groups) if(groups[i] && isteam(f->team, groups[i]->team)) ownteam = groups[i];
		loopv(groups) if(groups[i] && ownteam != groups[i]) {
			if(groups[i]->score > ownteam->score) return "\fRLosing";
			else if(groups[i]->score < ownteam->score) return "\fGWinning";
		}
		return "\f2Draw";
	}
    bool isclanwar(bool allowspec, int colors)
    {
        int numgroups = groupplayers();
        extern int mastermode;
        if(!m_teammode || (!allowspec && player1->state == CS_SPECTATOR) || numgroups < 2 || mastermode < MM_LOCKED) return false;
		groupplayers();

			fpsent *f = followingplayer();
			if(!f && player1->state != CS_SPECTATOR) f = player1;
			scoregroup *g1 = groups[0], *g2 = groups[1]; //g1 = your team g2 other team
			
			if(f && isteam(f->team, groups[1]->team)) //you are in the second team -> warp g1 and g2
			{
				g1 = groups[1];
				g2 = groups[0];
			}
        char *g1clan = g1->isclan();
        char *g2clan = g2->isclan();
        if(g1clan || g2clan) {
			if(!g1clan) g1clan = newstring("mixed");
			if(!g2clan) g2clan = newstring("mixed");
			if(!colors)	formatstring(battleheadline)("%s(%d) vs %s(%d)", g1clan , g1->score, g2clan, g2->score);
			else if(colors==1 || !f) formatstring(battleheadline)("\f2%s\f7(%d) vs \f2%s\f7(%d)", g1clan , g1->score, g2clan, g2->score);
			else {

				bool winning = g1->score > g2->score;
				bool draw = g1->score == g2->score;
				formatstring(battleheadline)("%s%s(\fs%s%d\fr) \fs\f7vs\fr %s(\fs%s%d\fr)", winning ? "\fg" : draw ? "\f2" : "\f3", g1clan , winning ? "\fG" : draw ? "\fy" : "\fR", g1->score, g2clan, winning ? "\fG" : draw ? "\fy" : "\fR", g2->score);
			}
			return groups[0]->players.length() == groups[1]->players.length();
        }
        return false;
    }

    void renderscoreboard(g3d_gui &g, bool firstpass)
    {
        const ENetAddress *address = connectedpeer();
        if(showservinfo && address)
        {
            string hostname;
            if(enet_address_get_host_ip(address, hostname, sizeof(hostname)) >= 0)
            {
                if(servinfo[0]) g.titlef("%.25s", 0xFFFF80, NULL,NULL, servinfo);
                else g.titlef("%s:%d", 0xFFFF80, NULL, NULL,hostname, address->port);
            }
        }
     
        g.pushlist();
        g.spring();
        g.text(server::modename(gamemode), 0xFFFF80);
        g.separator();
        const char *mname = getclientmap();
        g.text(mname[0] ? mname : "[new map]", 0xFFFF80);
        extern int gamespeed;
        if(gamespeed != 100) { g.separator(); g.textf("%d.%02dx", 0xFFFF80, NULL, NULL, gamespeed/100, gamespeed%100); }
        if(m_timed && mname[0] && (maplimit >= 0 || intermission))
        {
            g.separator();
            if(intermission) g.text("intermission", 0xFFFF80);
            else 
            {
                int secs = max(maplimit-lastmillis, 0)/1000, mins = secs/60;
                secs %= 60;
                g.pushlist();
                g.strut(mins >= 10 ? 4.5f : 3.5f);
                g.textf("%d:%02d", 0xFFFF80, NULL, NULL, mins, secs);
                g.poplist();
            }
        }
        if(ispaused()) { g.separator(); g.text("paused", 0xFFFF80); }
        g.spring();
        g.poplist();

        g.separator();
 
        int numgroups = groupplayers();
        loopk(numgroups)
        {
            if((k%2)==0) g.pushlist(); // horizontal
            
            scoregroup &sg = *groups[k];
            int bgcolor = sg.team && m_teammode ? (isteam(player1->team, sg.team) ? 0x3030C0 : 0xC03030) : 0,
                fgcolor = 0xFFFF80;

            g.pushlist(); // vertical
            g.pushlist(); // horizontal

            #define loopscoregroup(o, b) \
                loopv(sg.players) \
                { \
                    fpsent *o = sg.players[i]; \
                    b; \
                }    

            g.pushlist();
            if(sg.team && m_teammode)
            {
                g.pushlist();
                g.background(bgcolor, numgroups>1 ? 3 : 5);
                g.strut(1);
                g.poplist();
            }
            g.text("", 0, " ");
            loopscoregroup(o,
            {
                if(o==player1 && highlightscore && (multiplayer(false) || demoplayback || players.length() > 1))
                {
                    g.pushlist();
                    g.background(0x808080, numgroups>1 ? 3 : 5);
                }
                const playermodelinfo &mdl = getplayermodelinfo(o);
                const char *icon = sg.team && m_teammode ? (isteam(player1->team, sg.team) ? mdl.blueicon : mdl.redicon) : mdl.ffaicon;
                g.text("", 0, icon);
                if(o==player1 && highlightscore && (multiplayer(false) || demoplayback || players.length() > 1)) g.poplist();
            });
            g.poplist();

            if(sg.team && m_teammode)
            {
                g.pushlist(); // vertical
                char *clan = sg.isclan();
                if(sg.score>=10000) g.textf("%s: WIN", fgcolor, NULL,NULL, sg.team);
                else if(clan) g.textf("%s\fs\f4(%s)\fr: %d", fgcolor, NULL,NULL, clan, sg.team, sg.score);
                else g.textf("%s: %d", fgcolor, NULL,NULL, sg.team, sg.score);
                g.pushlist(); // horizontal
            }

            if(!cmode || !cmode->hidefrags() || su_showfrags)
            { 
                g.pushlist();
                g.strut(6);
                g.text("frags", fgcolor);
                loopscoregroup(o, g.textf("%d", 0xFFFFDD, NULL,NULL, o->frags));
                g.poplist();
            }
			if(su_showaccuracy)
            { 
                g.pushlist();
                g.strut(5);
                g.text("accu", fgcolor);
                loopscoregroup(o, g.textf("%d%%", 0XFFFFDD, NULL,NULL, min(100, (o->totaldamage*100)/max(o->totalshots, 1))));
                g.poplist();
            }
			if(su_showkpd)
            { 
                g.pushlist();
                g.strut(5);
                g.text("KpD", fgcolor);
                loopscoregroup(o, g.textf("%.2f", 0xFFFFDD, NULL,NULL, float(float(o->frags)/float(max(o->deaths, 1))) ));
                g.poplist();
            }
			if(su_showdeaths)
            { 
                g.pushlist();
                g.strut(6);
                g.text("deaths", fgcolor);
                loopscoregroup(o, g.textf("%d", 0xFFFFDD, NULL,NULL, o->deaths));
                g.poplist();
            }

			if(su_showflags && m_ctf)
			{
				g.pushlist();
				g.strut(5);
				g.text("flags",fgcolor);
				loopscoregroup(o, g.textf("%d", 0xFFFFDD, NULL,NULL, o->flags));
                g.poplist();
            }

            g.pushlist();
            g.text("name", fgcolor);
            g.strut(13);
            loopscoregroup(o, 
            {
                int status = o->state!=CS_DEAD ? 0xFFFFDD : 0x606060;
                if(o->privilege)
                {
                    status = o->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x40FF80;
                    if(o->state==CS_DEAD) status = (status>>1)&0x7F7F7F;
                }
                g.textf("%s ", status, NULL, NULL, colorname(o));
            });
            g.poplist();

            if(multiplayer(false) || demoplayback)
            {
                if(showpj)
                {
                    g.pushlist();
                    g.strut(6);
                    g.text("pj", fgcolor);
                    loopscoregroup(o,
                    {
                        if(o->state==CS_LAGGED) g.text("LAG", 0xFFFFDD);
                        else g.textf("%d", 0xFFFFDD, NULL,NULL, o->plag);
                    });
                    g.poplist();
                }

                if(showping)
                {
                    g.pushlist();
                    g.text("ping", fgcolor);
                    g.strut(6);
                    loopscoregroup(o,
                    {
                        fpsent *p = o->ownernum >= 0 ? getclient(o->ownernum) : o;
                        if(!p) p = o;
                        if(!showpj && p->state==CS_LAGGED) g.text("LAG", 0xFFFFDD);
                        else g.textf("%d", 0xFFFFDD, NULL,NULL, p->ping);
                    });
                    g.poplist();
                }
            }

            if(showclientnum || player1->privilege>=PRIV_MASTER)
            {
                g.space(1);
                g.pushlist();
                g.text("cn", fgcolor);
                loopscoregroup(o, g.textf("%d", 0xFFFFDD, NULL,NULL, o->clientnum));
                g.poplist();
            }
            
            if(sg.team && m_teammode)
            {
                g.poplist(); // horizontal
                g.poplist(); // vertical
            }

            g.poplist(); // horizontal
            g.poplist(); // vertical

            if(k+1<numgroups && (k+1)%2) g.space(2);
            else g.poplist(); // horizontal
        }
        
        if(showspectators && spectators.length())
        {
            if(showclientnum || player1->privilege>=PRIV_MASTER)
            {
                g.pushlist();
                
                g.pushlist();
                g.text("spectator", 0xFFFF80, " ");
                loopv(spectators) 
                {
                    fpsent *o = spectators[i];
                    int status = 0xFFFFDD;
                    if(o->privilege) status = o->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x40FF80;
                    if(o==player1 && highlightscore)
                    {
                        g.pushlist();
                        g.background(0x808080, 3);
                    }
                    g.text(colorname(o), status, "spectator");
                    if(o==player1 && highlightscore) g.poplist();
                }
                g.poplist();

                g.space(1);
                g.pushlist();
                g.text("cn", 0xFFFF80);
                loopv(spectators) g.textf("%d", 0xFFFFDD, NULL,NULL, spectators[i]->clientnum);
                g.poplist();

                g.poplist();
            }
            else
            {
                g.textf("%d spectator%s", 0xFFFF80, " ",NULL, spectators.length(), spectators.length()!=1 ? "s" : "");
                loopv(spectators)
                {
                    if((i%3)==0) 
                    {
                        g.pushlist();
                        g.text("", 0xFFFFDD, "spectator");
                    }
                    fpsent *o = spectators[i];
                    int status = 0xFFFFDD;
                    if(o->privilege) status = o->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x40FF80;
                    if(o==player1 && highlightscore)
                    {
                        g.pushlist();
                        g.background(0x808080);
                    }
                    g.text(colorname(o), status);
                    if(o==player1 && highlightscore) g.poplist();
                    if(i+1<spectators.length() && (i+1)%3) g.space(1);
                    else g.poplist();
                }
            }
        }
    }

    struct scoreboardgui : g3d_callback
    {
        bool showing;
        vec menupos;
        int menustart;

        scoreboardgui() : showing(false) {}

        void show(bool on)
        {
            if(!showing && on)
            {
                menupos = menuinfrontofplayer();
                menustart = starttime();
            }
            showing = on;
        }

        void gui(g3d_gui &g, bool firstpass)
        {
            g.start(menustart, 0.03f, NULL, false);
            renderscoreboard(g, firstpass);
            g.end();
        }

        void render()
        {
            if(showing) g3d_addgui(this, menupos, (scoreboard2d ? GUI_FORCE_2D : GUI_2D | GUI_FOLLOW) | GUI_BOTTOM);
        }

    } scoreboard;

    void g3d_gamemenus()
    {
        scoreboard.render();
    }

    VARFN(scoreboard, showscoreboard, 0, 0, 1, scoreboard.show(showscoreboard!=0));

    void showscores(bool on)
    {
        showscoreboard = on ? 1 : 0;
        scoreboard.show(on);
    }
    ICOMMAND(showscores, "D", (int *down), showscores(*down!=0));
}

