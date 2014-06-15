//layer between the server networking and basic features and the cloud specific stuff


enum { MSG_CHAN = 0, FILE_CHAN, MAX_CHAN };

ENetPacket *sendf(ENetPeer *receiver, int chan, const char *format, ...);
extern void sendpacket(ENetPeer *receiver, int chan, ENetPacket *packet, int exclude = -1);

void conoutf(const char *fmt, ...);


namespace cloud {

	struct c_date {
		int sec, min, hour,
			day, mon, year;
	};

	
	#define MAXNAMELEN 20
	#define MAXTRANS 5000

	extern void serverinit();
	extern void disconnectuser(ENetPeer *clientpeer);
	
	extern void updatedb();
	extern void	parsepacket(ENetPeer *sender, int chan, packetbuf p);
	extern bool sendpackets();
}