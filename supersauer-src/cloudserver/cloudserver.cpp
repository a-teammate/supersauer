// cloudserver.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "cloud.h"
namespace cloud {
	enum { M_USER = 0, M_REGISTER, M_LOGIN, M_TEXT, M_ENTINFO, M_GETENTS, M_GETUSER, M_GETFILE, M_SENDFILE };
	enum { E_OTHER = 0, E_MAP, E_DEMO,  E_SCREENSHOT}; // different types of uploadable entitys
	char fileendings[3][4] = { "ogz", "dmo", "jpg" };
	char typenames[3][20] = { "Map", "Demo", "Screenshot" };

	struct entity {	
		int type;
		int id; //id of the entity
		int uploader; //userid of the uploader
		char name[50], author[50], desc[250];
		c_date date;
		int size;
		string savepath; //where the file was stored on the cloudserver

		void sendfile(int userid)
		{

		}
		void receivefile(uchar *data, int len, int type, char *name);
	};

	struct user { //registered user
		char name[MAXNAMELEN+1];
		//string server; 
		//int port;
		ENetPeer *peer;
		string pw;
		int id; // != pos for user[pos]
		vector<entity *> ents; // demos/ Screenshots / maps ( 10 / 10 / 10 )
		int totalspace;
		int maps, screens, demos;

		user(int userid) : peer(NULL), totalspace(0), maps(0), screens(0), demos(0)
		{
			id = userid;
		}
	};
	vector<user *> users;
	//#define MAX_SPACE (25*1024*1024) //25 MB
	//#define MAX_FILES 5 // 5 demos, screenshots, maps
	//void entity::receivefile(uchar *data, int len, int type, char *name)
	//{
	//	if(len > 4*1024*1024) {
	//		defformatstring(send)( "ERROR: Your upload is too big (%f.25MB), maximum allowed is 4 MB", float(len)/float(1024*1024));
	//		sendmsg(uploader, "ris", M_TEXT, send);
	//		return;
	//	}
	//	if((len+totalspace) > MAX_SPACE)) {
	//		defformatstring(send)("ERROR: Your Webspace is limited to %dMB, currently you stored %f.25MB", MAX_SPACE, float(totalspace)/float(1024*1024));
	//		sendmsg(uploader, "ris", M_TEXT, send);
	//		return;
	//	}
	//
	//	bool toomany = false;
	//	user *u = users[uploader];
	//	switch(type) {
	//	case E_MAP: if(u->maps.length() >= MAX_FILES) toomany = true; break;
	//	case E_DEMO: if(u->demos.length() >= MAX_FILES)toomany = true; break;
	//	case E_SCREENSHOT: if(u->screens.length() >=MAX_FILES)toomany = true; break;
	//	}
	//	if(tomany)
	//	{
	//		defformatstring(send)("ERROR: Your Webspace is limited to %d %ss. Delete other %s before you can upload", MAX_FILES, typenames[type], typenames[type]);
	//		sendmsg(uploader, "riis", M_TEXT, -1, send);
	//		return;
	//	}
	//	defformatstring(filename) ("%s/%s", u->name, name);
	//
	//	mapdata = openfile(filename, "w+b");
	//	if(!mapdata) { sendf(sender, 1, "ris", N_SERVMSG, "failed to open temporary file for map"); return; }
	//	mapdata->write(data, len);
	//	sendmsg(uploader, "riis", M_TEXT, -1, "Your file is successfully uploaded");
	//}

	user *finduser(int id)
	{
		loopv(users)
		{
			if(users[i]->id == id) return users[i];
		}
		return NULL;
	}
	int adduser(char *name, char *pw, ENetPeer *clientpeer)
	{
		if(!name || !name[0] || !pw || !pw[0]) return 0;//|| !clientpeer

		user *u = new user(users.length()+1);
		strncpy(u->name, name, MAXNAMELEN);
		u->name[MAXNAMELEN] = '\0';
		strncpy(u->pw, pw, 32);
		u->name[32] = '\0';
		u->peer = clientpeer;
		loopv(users) if(!users[i])
		{
			users[i] = u;
			return u->id;
		}
		users.add(u);
		return u->id;
	}
	int deleteuser(char *n, int id)
	{
		loopv(users) if(!strcmp(users[i]->name, n) && id == users[i]->id)
		{
			DELETEP(users[i])
			break; //todo: clear files
		}
	}
	bool connectuser(int id, char *pw, ENetPeer *clientpeer)
	{
		user *u = finduser(id);
		if(!u || !pw || !pw[0]) return false;
		if(strcmp(u->pw, pw)) return false; 
		u->peer = clientpeer;
		return true;
	}
	void disconnectuser(ENetPeer *clientpeer)
	{
		loopv(users) if(users[i]->peer == clientpeer)
		{
			 users[i]->peer = NULL;
			 return;
		}
	}
	
	void receivecommands(ucharbuf &p, ENetPeer *senderpeer)
	{
		char text[MAXTRANS];
		while(p.remaining()) switch(getint(p))
		{
			case M_REGISTER:
			{
				char name[MAXNAMELEN+1];
				char pw[33];
				getstring(name, p, MAXNAMELEN+1);
				getstring(pw, p, 33);
				adduser(name, pw, senderpeer);
				break;
			}
			case M_LOGIN:
			{
				int id = getint(p);
				char pw[33];
				getstring(pw, p, 32);
				pw[32] = '\0';
				user *u = finduser(id);
				if(!u) sendf( senderpeer, MSG_CHAN, "riis", M_TEXT, -1, "no such user");
				else {
					if(!connectuser(id, pw, senderpeer)) sendf( senderpeer, MSG_CHAN, "riis", "wrong password");
					else sendf( senderpeer, MSG_CHAN, "riis", "successfully logged in");
				}
				break;
			}
			case M_GETUSER: //someone wanna retrieve a userlist / re-retrieve a single other user
			{
				int who = getint(p);
				if(who) 
				{
					user *u = finduser(who);
					if(!u) sendf( senderpeer, MSG_CHAN, "riis", M_TEXT, -1, "no such user");
					else sendf(senderpeer, MSG_CHAN, "riis", M_USER, u->id, u->name);
				}
				else loopv(users)
				{
					user *u = users[i];
					if(!u) continue;
					sendf(senderpeer, MSG_CHAN, "riis", M_USER, u->id, u->name);
				}
				break;
			}
			case M_GETENTS: //someone wants the previews of another users files
			{
				int who = getint(p);
				if(who > 0)
				{
					user *u = finduser(who);
					if(!u) { sendf( senderpeer, 0, "riis", M_TEXT, -1, "no such user"); break; }
					packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);

					putint(p, M_ENTINFO);
					putint(p, u->id);
					putint(p, u->ents.length());
				
					loopvk(u->ents)
					{
						entity *e = u->ents[k];
						putint(p, e->type);
						putint(p, e->id);
						putint(p, e->size);

						putint(p, e->date.sec);
						putint(p, e->date.min);
						putint(p, e->date.hour);
						putint(p, e->date.day);
						putint(p, e->date.mon);
						putint(p, e->date.year);

						sendstring(e->name, p);
						sendstring(e->author, p);
						sendstring(e->desc, p);
					}

					sendpacket(senderpeer, MSG_CHAN, p.finalize());
				}
				else loopv(users)
				{
					user *u = users[i];
					if(!u) continue;
					packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);

					putint(p, M_ENTINFO);
					putint(p, u->id);
					putint(p, u->ents.length());
				
					loopvk(u->ents)
					{
						entity *e = u->ents[k];
						putint(p, e->type);
						putint(p, e->id);
						putint(p, e->size);

						putint(p, e->date.sec);
						putint(p, e->date.min);
						putint(p, e->date.hour);
						putint(p, e->date.day);
						putint(p, e->date.mon);
						putint(p, e->date.year);

						sendstring(e->name, p);
						sendstring(e->author, p);
						sendstring(e->desc, p);
					}

					sendpacket(senderpeer, MSG_CHAN, p.finalize());
				}
				break;
			}
		}
	}

	void parsepacket(ENetPeer *sender, int chan, packetbuf p)
	{
		switch(chan)
		{
			case MSG_CHAN:	receivecommands(p, sender); break;
			case FILE_CHAN: break;
		}
	}

	void serverinit() 
	{ 
		adduser("Root", "stillewasser", NULL);
	}
	
	void updatedb(){

	}
	bool sendpackets(){
		return true;
	}
	
}

