#include "cube.h"
#include "game.h"

extern int connectwithtimeout(ENetSocket sock, const char *hostname, const ENetAddress &remoteaddress);
extern bool resolverwait(const char *name, ENetAddress *address);

#define mkstring(d) string d; d[0] = 0;
namespace irc
{


	enum { IRCC_NONE = 0, IRCC_JOINING, IRCC_JOINED, IRCC_KICKED, IRCC_BANNED };

	enum usermode
	{
		ERR, NONE, VOICE, HALFOP, OP, ADMIN, OWNER
	};

	struct user
	{
		string nick;
		usermode state;

		static bool compare(user * a, user * b) 
		{
			if(a->state > b->state) return true;
			if(a->state < b->state) return false;
			return a->nick[0] && b->nick[0] ? strncasecmp(a->nick, b->nick, 30) < 0 : false;
		}
	};

	struct ircchan
	{
		int state, lastjoin, lastsync;
		string name;

		// remod
		vector<user *> users;
		vector<char *> buffer;

		ircchan() { reset(); }
		~ircchan() { reset(); }

		void reset()
		{
			state = IRCC_NONE;
			lastjoin = lastsync = 0;
			name[0] = 0;
			resetusers();
			loopv(buffer) DELETEP( buffer[i] )
			buffer.setsize(0);
		}
		user *getuser(char *nick)
		{
			if(!nick) return NULL;
			loopv(users)
				if(users[i]) 
					if(strcmp(users[i]->nick, nick)==0)	return users[i];
			return NULL;
		}

		void adduser(char *nick, usermode state)
		{
			if(!nick) return;
			user *u = users.add(new user);
			strcpy(u->nick, nick);
			u->state=state;
			resortusers();
		}

		void deluser(char *nick)
		{
			if(!nick) return;
			loopv(users)
				if(users[i])
				if(strcmp(users[i]->nick, nick)==0)
				{
					DELETEP(users[i])
					users.remove(i);
					return;
				}
		}

		void setusermode(char *nick, usermode state)
		{
			if(!nick) return;
			loopv(users)
				if(users[i])
					if(strcmp(users[i]->nick, nick)==0) users[i]->state = state;
			resortusers();
		}

		void resortusers()
		{
			users.sort(user::compare);
		}

		void resetusers()
		{
			loopv(users) DELETEP(users[i])
			users.setsize(0);
		}

		bool rename(char *nick, char *newnick)
		{
			if(!nick || !newnick) return false;
			loopv(users)
				if(users[i]) 
					if(strcmp(users[i]->nick, nick)==0)
					{
						strncpy(users[i]->nick, newnick, 30);
						users[i]->nick[100] = '\0';
						resortusers();
						return true;
					}

				return false;
		}
		void conbuf( const char *msg, ...)
		{
			defvformatstring(str, msg, msg);
			concatstring(str, "\0");
			buffer.add(newstring(str));
			if(buffer.length()>40) //workaround untill scrollbox appears: delete the oldest news if necessary
			{
				DELETEA(buffer[0])
				buffer.remove(0);
			}
		}
	};
	enum { IRC_NEW = 0, IRC_DISC, IRC_ATTEMPT, IRC_CONN, IRC_ONLINE, IRC_MAX };
	
	struct network
	{
		int state, port, lastattempt, inputcarry, inputlen;
		string name, serv, nick, ip;
		ENetAddress address;
		ENetSocket sock;
		vector<ircchan> channels;
		uchar input[4096];

		// remod
		time_t lastping, lastpong;
		int updated;

		network() { reset(); }
		~network() { reset(); }

		void reset()
		{
			state = IRC_DISC;
			inputcarry = inputlen = 0;
			port = lastattempt = 0;
			name[0] = serv[0] = nick[0] = ip[0] = 0;
			channels.shrink(0);

			lastping = 0;
			lastpong = 0;
			updated = 0;
		}
		ircchan *getchannel(const char *name);
		bool joinchannel(ircchan *c);
		void disconnect(const char *msg = NULL);
		void establish();
		void send(const char *msg, ...);
		int receive();
		void parse();
	};

	extern vector<network *> networks;

	extern network *getnetwork(const char *name);

	extern void newnet(const char *name, const char *serv, int port, const char *nick, const char *ip = "");
	extern bool newchan(const char *name, const char *channel);
}