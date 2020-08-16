#include "lmps.h"

//run:  ./xx 0 mm.h264           //only video
//      ./xx 1 mm.ha54 gg.g71a   //video and audio
int main(int argc, char *argv[])
{
	net_t nt;
	if(lm_connect_server(0, 20002, "192.168.1.157", &nt) < 0){
		printf("Connect server failed !\n");
		return -1;
	}

	lm_ps_fileStream_proc(&nt, &argv[2], atoi(argv[1]));

	return  0;
}

