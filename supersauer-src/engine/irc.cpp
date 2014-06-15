#include "cube.h"
#include "irc.h"

namespace irc
{

	SVAR(irccommandchar, "");
	time_t clocktime = 0;

	VAR(ircpingdelay, 0, 60, 600); // delay in seconds between network ping
	XSVARP(ircnick, "");

	char *gettime(char *format)
	{
		struct tm *t;
		static string buf;

		t = localtime (&clocktime);
		strftime (buf, sizeof (buf) - 1, format, t);
		return buf;
	}

	vector<network *> networks;

	network *getnetwork(const char *name)
	{
		if(name && *name)
		{
			loopv(networks) if(!strcmp(networks[i]->name, name)) return networks[i];
		}
		return NULL;
	}

	void network::establish()
	{
		this->lastattempt = clocktime;
		if(this->address.host == ENET_HOST_ANY)
		{
			conoutf("\fs\f8IRC: \frlooking up %s:[%d]...", this->serv, this->port);

			if(!resolverwait(this->serv, &this->address))
			{
				conoutf("\fs\f8IRC: \frunable to resolve %s:[%d]...", this->serv, this->port);

				this->state = IRC_DISC;
				return;
			}
		}

		ENetAddress address = { ENET_HOST_ANY, enet_uint16(this->port) };
		if(*this->ip && enet_address_set_host(&address, this->ip) < 0) conoutf("\fs\f8IRC: \frfailed to bind address: %s", this->ip);
		this->sock = enet_socket_create(ENET_SOCKET_TYPE_STREAM);
		if(this->sock != ENET_SOCKET_NULL && *this->ip && enet_socket_bind(this->sock, &address) < 0)
		{
			conoutf("\fs\f8IRC: \frfailed to bind connection socket: %s", this->ip);
			address.host = ENET_HOST_ANY;
		}

		// Remod hack, sometimes irc bot block client connections
		// set socket timeout to 5 seconds
		struct timeval timeout;
		timeout.tv_sec = 5;
		timeout.tv_usec = 0;
		if(setsockopt(this->sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) conoutf("\fs\f8IRC: \frsetsockopt SO_RCVTIMEO failed");
		if(setsockopt(this->sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0) conoutf("\fs\f8IRC: \frsetsockopt SO_SNDTIMEO failed");

		if(this->sock == ENET_SOCKET_NULL || connectwithtimeout(this->sock, this->serv, this->address) < 0)
		{
			conoutf(this->sock == ENET_SOCKET_NULL ? "\fs\f8IRC: \frcould not open socket to %s:[%d]" : "\fs\f8IRC: \frcould not connect to %s:[%d]", this->serv, this->port);

			if(this->sock != ENET_SOCKET_NULL)
			{
				enet_socket_destroy(this->sock);
				this->sock = ENET_SOCKET_NULL;
			}
			this->state = IRC_DISC;
			return;
		}
		this->state = IRC_ATTEMPT;
		conoutf("\fs\f8IRC: \frconnecting to %s:[%d]...", this->serv, this->port);
	}

	void network::send(const char *msg, ...)
	{
		defvformatstring(str, msg, msg);
		if(this->sock == ENET_SOCKET_NULL) return;

		concatstring(str, "\n");
		ENetBuffer buf;
		uchar ubuf[512];
		int len = strlen(str), carry = 0;
		while(carry < len)
		{
			int numu = encodeutf8(ubuf, sizeof(ubuf)-1, &((uchar *)str)[carry], len - carry, &carry);
			if(carry >= len) ubuf[numu++] = '\n';
			loopi(numu) switch(ubuf[i])
			{
			case '\v': ubuf[i] = '\x01'; break;
			case '\f': ubuf[i] = '\x03'; break;
			}
			buf.data = ubuf;
			buf.dataLength = numu;
			enet_socket_send(this->sock, NULL, &buf, 1);
		}
	}

	int network::receive()
	{
		if(this->sock == ENET_SOCKET_NULL) return -2;
		int total = 0;
		enet_uint32 events = ENET_SOCKET_WAIT_RECEIVE;
		while(enet_socket_wait(this->sock, &events, 0) >= 0 && events)
		{
			ENetBuffer buf;
			buf.data = this->input + this->inputlen;
			buf.dataLength = sizeof(this->input) - this->inputlen;
			int len = enet_socket_receive(this->sock, NULL, &buf, 1);
			if(len <= 0) return -3;
			loopi(len) switch(this->input[this->inputlen+i])
			{
			case '\x01': this->input[this->inputlen+i] = '\v'; break;
			case '\x03': this->input[this->inputlen+i] = '\f'; break;
			case '\v': case '\f': this->input[this->inputlen+i] = ' '; break;
			}
			this->inputlen += len;
			int carry = 0, decoded = decodeutf8(&this->input[this->inputcarry], this->inputlen - this->inputcarry, &this->input[this->inputcarry], this->inputlen - this->inputcarry, &carry);
			if(carry > decoded)
			{
				memmove(&this->input[this->inputcarry + decoded], &this->input[this->inputcarry + carry], this->inputlen - (this->inputcarry + carry));
				this->inputlen -= carry - decoded;
			}
			this->inputcarry += decoded;
			total += decoded;
		}
		return total;
	}

	void newnet(const char *name, const char *serv, int port, const char *nick, const char *ip)
	{
		if(!name || !*name || !serv || !*serv || !port || !nick || !*nick) return;
		network *m = getnetwork(name);
		if(m)
		{
			if(m->state != IRC_DISC) conoutf("network %s already exists", m->name);
			else m->establish();
			return;
		}
		network &n = *networks.add(new network);
		n.state = IRC_NEW;
		n.sock = ENET_SOCKET_NULL;
		n.port = port;
		n.lastattempt = 0;
		copystring(n.name, name);
		copystring(n.serv, serv);
		copystring(n.nick, nick);
		copystring(n.ip, ip);
		n.address.host = ENET_HOST_ANY;
		n.address.port = n.port;
		n.input[0] = 0;

		conoutf("\fs\f8IRC: \fradded network %s (%s:%d) [%s]", name, serv, port, nick);
	}

	void irc(const char *network)
	{
		newnet("gamesurge", network, 6667, ircnick);
	}
	ICOMMAND(irc,"s", (const char *n), irc(n));

	ircchan *network::getchannel(const char *name)
	{
		if(name && *name)
		{
			loopv(this->channels) if(!strcasecmp(this->channels[i].name, name))
				return &this->channels[i];
		}
		return NULL;
	}

	bool network::joinchannel(ircchan *c)
	{
		if(this->state != IRC_ONLINE)
		{
			conoutf("\fs\f8IRC: \f3cannot join %s until connection is online", this->name);

			return false;
		}
		else this->send("JOIN %s", c->name);
		c->state = IRCC_JOINING;
		c->lastjoin = clocktime;
		c->lastsync = 0;
		return true;
	}

	bool newchan(const char *name, const char *channel)
	{
		if(!name || !*name || !channel || !*channel) return false;
		network *n = getnetwork(name);
		if(!n)
		{
			conoutf("\fs\f8IRC: \fr\f3no such network: %s", name);
			return false;
		}
		ircchan *c = n->getchannel(channel);
		if(c)
		{
			conoutf("\fs\f8IRC: channel %s already exists", c->name);

			return false;
		}
		ircchan &d = n->channels.add();
		d.state = IRCC_NONE;
		d.lastjoin = d.lastsync = 0;
		copystring(d.name, channel);
		if(n->state == IRC_ONLINE) n->joinchannel(&d);

		conoutf("\fs\f8IRC: added channel %s", d.name);

		return true;
	}

	void join(const char *c)
	{
		newchan("gamesurge", c);
	}

	void process(network *n, char *user[3], int g, int numargs, char *w[])
	{
		if(!strcasecmp(w[g], "NOTICE") || !strcasecmp(w[g], "PRIVMSG"))
		{
			if(numargs > g+2)
			{
				bool ismsg = strcasecmp(w[g], "NOTICE")!=0;
				int len = strlen(w[g+2]);
				if(w[g+2][0] == '\v' && w[g+2][len-1] == '\v')
				{
					char *p = w[g+2];
					p++;
					const char *word = p;
					p += strcspn(p, " \v\0");
					if(p-word > 0)
					{
						char *q = newstring(word, p-word);
						p++;
						const char *start = p;
						p += strcspn(p, "\v\0");
						char *r = p-start > 0 ? newstring(start, p-start) : newstring("");
						if(ismsg)
						{
							if(!strcasecmp(q, "ACTION"))
							{
								//ircprintf(n, 1, g ? w[g+1] : NULL, "\fv* %s %s", user[0], r);
							}
							else
							{
								conoutf("\fs\f8IRC: \fr%s requests: %s %s", user[0], q, r);

								if(!strcasecmp(q, "VERSION")) n->send("NOTICE %s :\vVersion %s", user[0], super_version);
								else if(!strcasecmp(q, "PING")) // eh, echo back
									n->send("NOTICE %s :\vPING %s\v", user[0], r);
							}
						}
						else conoutf("\fs\f8IRC: \fr%s replied: %s %s", user[0], q, r);
						DELETEA(q); DELETEA(r);
					}
				}
				else if(ismsg)
				{
					const char *ftext; // command buffer

					const char *p = w[g+2];
					if (g &&
						((strcasecmp(w[g+1], n->nick) &&
						!strncasecmp(w[g+2], n->nick, strlen(n->nick)) &&
						strchr(":;, .\t", w[g+2][strlen(n->nick)]) &&
						(p += strlen(n->nick))) ||
						(irccommandchar &&
						strlen(irccommandchar) &&
						!strncasecmp(w[g+2], irccommandchar, strlen(irccommandchar)) &&
						(p += strlen(irccommandchar)) )))
					{
						while(p && (*p == ':' || *p == ';' || *p == ',' || *p == '.' || *p == ' ' || *p == '\t')) p++;

						if(p && *p)
						{
							// hightlighted message
							conoutf("\fs\f8IRC: \f3<\f1%s\f3>\fr %s", user[0], p);
						}
					}
					else
					{
						ftext = newstring(w[g+2]);
						ircchan *ch = n->getchannel(w[g+1]);
						ch->conbuf("\fs\f1<%s>\fr %s", user[0], ftext);
						DELETEA(ftext)
					}
				}
				//  else conoutf("\fs\f8IRC: \fr\fo-%s- %s", user[0], w[g+2]);
				//eg * or global..
			}
		}
		else if(!strcasecmp(w[g], "NICK"))
		{
			loopv(n->channels)
			{
				if(n->channels[i].rename(user[0], w[g+1])) continue;
				if(numargs > g+1) n->channels[i].conbuf("\f4%s (%s@%s) is now known as %s", user[0], user[1], user[2], w[g+1]);

			}
			if(numargs > g+1)
			{
				if(!strcasecmp(user[0], n->nick)) copystring(n->nick, w[g+1]);
			}
		}
		else if(!strcasecmp(w[g], "JOIN"))
		{
			if(numargs > g+1)
			{
				ircchan *c = n->getchannel(w[g+1]);
				if(c && !strcasecmp(user[0], n->nick))
				{
					c->state = IRCC_JOINED;
					c->lastjoin = c->lastsync = clocktime;
				}
				c->conbuf("\f4%s (%s@%s) has joined", user[0], user[1], user[2]);
				if(strcmp(n->nick, user[0]) != 0)
					c->adduser(user[0], NONE);
			}
		}
		else if(!strcasecmp(w[g], "PART"))
		{
			if(numargs > g+1)
			{
				ircchan *c = n->getchannel(w[g+1]);
				if(c && !strcasecmp(user[0], n->nick))
				{
					c->state = IRCC_NONE;
					c->lastjoin = clocktime;
					c->lastsync = 0;
				}
				c->conbuf("\f4%s (%s@%s) parted", user[0], user[1], user[2]);
				c->deluser(user[0]);
			}
		}
		else if(!strcasecmp(w[g], "QUIT"))
		{

			loopv(n->channels) if(n->channels[i].getuser(user[0]) != NULL)
			{        
				if(numargs > g+1) n->channels[i].conbuf("\f4%s (%s@%s) has quit: %s", user[0], user[1], user[2], w[g+1]);
				else n->channels[i].conbuf("\f4%s (%s@%s) has quit", user[0], user[1], user[2]);
				n->channels[i].deluser(user[0]);
			}
		}
		else if(!strcasecmp(w[g], "KICK"))
		{
			if(numargs > g+2)
			{
				ircchan *c = n->getchannel(w[g+1]);
				if(c && !strcasecmp(w[g+2], n->nick))
				{
					c->state = IRCC_KICKED;
					c->lastjoin = clocktime;
					c->lastsync = 0;
				}
				c->conbuf("\f3%s (%s@%s) has kicked %s from %s", user[0], user[1], user[2], w[g+2], w[g+1]);
				c->deluser(user[0]);
			}
		}
		else if(!strcasecmp(w[g], "MODE"))
		{
			if(numargs > g+2)
			{
				mkstring(modestr);
				loopi(numargs-g-2)
				{
					if(i) concatstring(modestr, " ");
					concatstring(modestr, w[g+2+i]);
				}
				ircchan *c = n->getchannel(w[g+1]);
				if(c)
				{
					if(w[g+2][0]=='-')      // -
						c->setusermode(w[g+3], NONE);
					else                    // +
					{
						switch(w[g+2][1])  
						{
						case 'o': c->setusermode(w[g+3], OP); break;
						case 'v': c->setusermode(w[g+3], VOICE); break;
						case 'm': break;
						default: c->setusermode(w[g+3], NONE); break;
						}
					}
				}
			}
		}
		else if(!strcasecmp(w[g], "PING"))
		{
			if(numargs > g+1)
			{
				n->send("PONG %s", w[g+1]);
			}
			else
			{
				n->send("PONG %d", clocktime);
			}
		}
		else if(!strcasecmp(w[g], "PONG"))
		{
			n->lastpong = clocktime;
		}
		else
		{
			int numeric = *w[g] && *w[g] >= '0' && *w[g] <= '9' ? atoi(w[g]) : 0, off = 0;
			mkstring(s);
#define irctarget(a) (!strcasecmp(n->nick, a) || *a == '#' || n->getchannel(a))
			char *targ = numargs > g+1 && irctarget(w[g+1]) ? w[g+1] : NULL;
			if(numeric)
			{
				off = numeric == 353 ? 2 : 1;
				if(numargs > g+off+1 && irctarget(w[g+off+1]))
				{
					targ = w[g+off+1];
					off++;
				}
			}
			else concatstring(s, user[0]);
			for(int i = g+off+1; numargs > i && w[i]; i++)
			{
				if(s[0]) concatstring(s, " ");
				concatstring(s, w[i]);
			}
			if(numeric) switch(numeric)
			{
			case 1:
				{
					if(n->state == IRC_CONN)
					{
						n->state = IRC_ONLINE;
						conoutf("\fs\f8IRC: \fr\f0now connected to %s as %s", user[0], n->nick);
					}
					break;
				}

			case 353:
				{
					// char *s - list of nicks
					char *nickname;
					char *pnickname;
					mkstring(s2);
					strcpy(s2, s); // dublicate users line
					usermode state=NONE;

					ircchan *c = n->getchannel(w[g+3]);
					c->resetusers();

					nickname = strtok(s2, " ");
					while(nickname!= NULL)
					{
						switch(nickname[0])
						{
						case '~':
						case '*': state = OWNER;	break;
						case '&':
						case '!': state = ADMIN;	break;
						case '@': state = OP;		break;
						case '%': state = HALFOP;	break;
						case '+': state = VOICE;	break;
						default: state = NONE;		break;
						}

						if(state != NONE)
						{
							int len = strlen(nickname);
							strncpy(nickname, &nickname[1], len-1);
							nickname[len-1] = '\0';
						}

						pnickname=newstring(nickname);
						c->adduser(pnickname, state);

						nickname = strtok(NULL, " ");
					}
					break;
				}
			case 433:
				{
					if(n->state == IRC_CONN)
					{
						concatstring(n->nick, "_");
						n->send("NICK %s", n->nick);
					}
					break;
				}
			case 471:
			case 473:
			case 474:
			case 475:
				{
					ircchan *c = n->getchannel(w[g+2]);
					if(c)
					{
						c->state = IRCC_BANNED;
						c->lastjoin = clocktime;
						c->lastsync = 0;
						c->conbuf("\f3[KICKED]waiting 5 mins to rejoin %s", c->name);
					}
					break;
				}
			default: break;
			}
			//  if(s[0]) conoutf("rest\fw%s %s", w[g], s);
			// else conoutf("rest\fw%s", w[g]);
		}
	}

	void network::parse()
	{
		const int MAXWORDS = 25;
		char *w[MAXWORDS], *p = (char *)this->input, *start = p, *end = &p[this->inputcarry];
		loopi(MAXWORDS) w[i] = NULL;
		while(p < end)
		{
			bool full = false;
			int numargs = 0, g = 0;
			while(iscubespace(*p)) { if(++p >= end) goto cleanup; }
			start = p;
			if(*p == ':') { g = 1; ++p; }
			for(;;)
			{
				const char *word = p;
				if(*p == ':') { word++; full = true; } // uses the rest of the input line then
				while(*p != '\r' && *p != '\n' && (full || *p != ' ')) { if(++p >= end) goto cleanup; }

				if(numargs < MAXWORDS) w[numargs++] = newstring(word, p-word);

				if(*p == '\n' || *p == '\r') { ++p; start = p; break; }
				else while(*p == ' ') { if(++p >= end) goto cleanup; }
			}
			if(numargs)
			{
				char *user[3] = { NULL, NULL, NULL };
				if(g)
				{
					char *t = w[0], *u = strrchr(t, '!');
					if(u)
					{
						user[0] = newstring(t, u-t);
						t = u + 1;
						u = strrchr(t, '@');
						if(u)
						{
							user[1] = newstring(t, u-t);
							if(*u++) user[2] = newstring(u);
							else user[2] = newstring("*");
						}
						else
						{
							user[1] = newstring("*");
							user[2] = newstring("*");
						}
					}
					else
					{
						user[0] = newstring(t);
						user[1] = newstring("*");
						user[2] = newstring("*");
					}
				}
				else
				{
					user[0] = newstring("*");
					user[1] = newstring("*");
					user[2] = newstring(this->serv);
				}
				if(numargs > g) process(this, user, g, numargs, w);
				loopi(3) DELETEA(user[i]);
			}
cleanup:
			loopi(MAXWORDS) DELETEA(w[i]);
		}
		int parsed = start - (char *)this->input;
		if(parsed > 0)
		{
			memmove(this->input, start, this->inputlen - parsed);
			this->inputcarry -= parsed;
			this->inputlen -= parsed;
		}
	}

	void network::disconnect(const char *msg)
	{
		if(msg) conoutf("disconnected from %s (%s:[%d]): %s", this->name, this->serv, this->port, msg);
		else conoutf("disconnected from %s (%s:[%d])", this->name, this->serv, this->port);
		enet_socket_destroy(this->sock);
		this->state = IRC_DISC;
		this->sock = ENET_SOCKET_NULL;
		this->lastattempt = -1;
	}

	void cleanup()
	{
		loopv(networks) if(networks[i]->sock != ENET_SOCKET_NULL)
		{
			network *n = networks[i];
			//        n->send("QUIT :%s, %s", RE_NAME, RE_URL);
			n->send("QUIT : %s %s", super_name, super_url);
			n->disconnect("shutdown");
		}
	}
	ICOMMAND(ircdisconnect, "", (), cleanup());
	void ping()
	{
		// don't ping
		if(ircpingdelay == 0) return;

		loopv(networks)
		{
			network *n = networks[i];
			if(n->state == IRC_ONLINE && (clocktime - n->lastping) > ircpingdelay)
			{
				n->send("PING %u", clocktime);
				n->lastping = clocktime;
			}
		}
	}
	void slice()
	{
		clocktime = time(NULL);

		loopv(networks)
		{
			network *n = networks[i];
			if(n->sock != ENET_SOCKET_NULL && n->state > IRC_DISC)
			{
				switch(n->state)
				{
				case IRC_ATTEMPT:
					{
						n->send("NICK %s", n->nick);
						n->send("USER %s +iw %s :%s %s", super_sname, super_sname, super_plattform, super_version);
						n->state = IRC_CONN;
						loopvj(n->channels)
						{
							ircchan *c = &n->channels[j];
							c->state = IRCC_NONE;
							c->lastjoin = clocktime;
							c->lastsync = 0;
						}
						break;
					}
				case IRC_ONLINE:
					{
						loopvj(n->channels)
						{
							ircchan *c = &n->channels[j];
							if(c->state != IRCC_JOINED && (!c->lastjoin || clocktime-c->lastjoin >= (c->state != IRCC_BANNED ? 5 : 300)))
								n->joinchannel(c);
						}
						// fall through
					}
				case IRC_CONN:
					{
						if(n->state == IRC_CONN && clocktime-n->lastattempt >= 60) n->disconnect("\fs\f8IRC:\fr connection attempt timed out");
						else switch(n->receive())
						{
						case -3: n->disconnect("\fs\f8IRC:\fr read error"); break;
						case -2: n->disconnect("\fs\f8IRC:\fr connection reset"); break;
						case -1: n->disconnect("\fs\f8IRC:\fr invalid connection"); break;
						case 0: break;
						default: n->parse(); break;
						}
						break;
					}
				default:
					{
						n->disconnect("encountered unknown connection state");
						break;
					}
				}
			}
			else if(!n->lastattempt /*|| (clocktime-n->lastattempt >= 60 && n->lastattempt > 0)*/) n->establish();
		}
		ping();
	}

	void ircsayto(char *to, char *msg)
	{
		if(!msg || !msg[0] || !strcmp(msg, " ")) return;
		if(msg[0] == '/')
		{
			if(!strncasecmp(msg, "/join ", 6))
			{
				if(msg[6]) join(&msg[6]);
			}
			else if(!strncasecmp(msg, "/nick ", 6))
			{
				if(!msg[6]) return;
				filtertext(&msg[6], &msg[6], false, 30);
				getnetwork("gamesurge")->send("NICK %s", &msg[6]);
				copystring(ircnick, &msg[6]);
			}
			else if(!strncasecmp(msg, "/quit", 5))
			{
				cleanup();
			}
			return;
		}
		loopv(networks)
		{
			network *n = networks[i];
			n->send("PRIVMSG %s :%s", to, msg);
			ircchan *c = n->getchannel(to);
			if(c) c->conbuf("\fy<\f1%s\fy>\f7%s", ircnick, msg);
		}
	}

	void ircaction(char *msg)
	{
		loopv(networks)
		{
			loopvj(networks[i]->channels)
			{
				char *to = networks[i]->channels[j].name;
				network *n = networks[i];
				n->send("PRIVMSG %s :%sACTION %s%s", to, "\001\0", msg, "\001\0");
				
			}
		}
	}
	bool netconnected()
	{
		if(!ircnick || !ircnick[0] || !strcmp(ircnick, " ")) ircnick = newstring(game::player1->name);
		return !(!networks.inrange(0) || !networks[0]);
	}
	bool netonline()
	{
		return networks.inrange(0) && networks[0] && networks[0]->state == IRC_ONLINE;
	}
	bool netconnecting()
	{
		return networks.inrange(0) && networks[0] && networks[0]->state == IRC_CONN;
	}

	ICOMMAND(ircconnected, "", (), intret(netconnected() ? 1 : 0));
	ICOMMAND(ircestablished, "", (), intret(netonline() ? 1 : 0));
	ICOMMAND(ircattempting, "", (), intret(netconnecting() ? 1 : 0));

	ircchan *selectedchan = NULL;
	char *ircnames(g3d_gui *cgui)
	{
		if(!netonline()) return NULL;
		if(!networks[0]->channels.inrange(0))
		{
			join(super_irc);
			return NULL; //?
		}
		if(!selectedchan) {
			selectedchan = &networks[0]->channels[0];
		}
		if(!selectedchan) return NULL;


		ircchan *c = selectedchan;
		loopv(c->users)
		{
			user *u = c->users[i];
			int color = 0xFFFFDD;
			if(u->state >= VOICE) color =  u->state >= HALFOP ?  0xFF8000 : 0x40FFA0; //normal admin and master color (master = voice)
			cgui->text(u->nick, color);
		}

		return NULL;
	}
	char *ircbuffer(g3d_gui *cgui)
	{
		if(!netonline()) return NULL;
		if(!networks[0]->channels.inrange(0))
		{
			join(super_irc);
			return NULL; //?
		}
		if(!selectedchan) {
			selectedchan = &networks[0]->channels[0];
		}
		if(!selectedchan) return NULL;

		ircchan *c = selectedchan;
		loopv(c->buffer)
		{
			cgui->text(c->buffer[i], 0xFFFFDD);
		}

		return NULL;
	}

	char *ircwindow(g3d_gui *cgui)
	{
		if(!netonline()) return NULL;
		if(!networks[0]->channels.inrange(0))
		{
			join(super_irc);
			return NULL; //?
		}

		
		cgui->strut(30);

		cgui->pushlist(); //vertical
			cgui->pushlist(); //horiz
			
				ircbuffer(cgui);
			cgui->poplist();

			cgui->pushlist();
				ircnames(cgui);
			cgui->poplist();

		cgui->poplist();//vertical
		return NULL;
	}

#if 1
	void irc_dumpnicks()
	{
		loopv(networks)
			loopvj(networks[i]->channels)

		{
			loopvk(networks[i]->channels[j].users)conoutf("networks[%i]->channels[%i].users[%i]=%s stat=%d\n", i, j, k, networks[i]->channels[j].users[k]->nick, networks[i]->channels[j].users[k]->state);
			loopvk(networks[i]->channels[j].buffer)conoutf(networks[i]->channels[j].buffer[k]);
		}
	}
	COMMAND(irc_dumpnicks, "");
#endif
	void ircsay(char *msg)
	{
		if(!netonline() || !selectedchan) return;
		ircsayto(selectedchan->name, msg);
	}
	COMMAND(ircsay, "s");
	COMMAND(ircsayto, "ss");
}