#ifndef __LMPS_H__
#define __LMPS_H__

#ifdef __cplusplus
extern "C" 
{
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>	  
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PS_HDR_LEN  14
#define SYS_HDR_LEN 18
#define PSM_HDR_LEN 24
#define PES_HDR_LEN 19
#define RTP_HDR_LEN 12
#define RTP_VERSION 2

#define PES_SPLIT_SZ   (PES_HDR_LEN + RTP_HDR_LEN)
#define MAX_EMPYT_SIZE (PS_HDR_LEN + SYS_HDR_LEN + PSM_HDR_LEN + PES_HDR_LEN + RTP_HDR_LEN)
#define RTP_MAX_PACKET_BUFF 1400
#define PS_PES_PAYLOAD_SIZE 65522

#define MAX_SOCK_NUMS      20
#define MAX_VIDEO_BUFF  (1024*900)
#define MAX_AUDIO_BUFF  (1024*256)
#define MAX_V_FAME_BUFF (1024*156)

#define TRUE   1
#define FALSE  0

#define NAME   "LmPs"

#define PRT_COND(val, str)  \
			{					\
				if((val) == TRUE){		\
					printf(NAME":%s%d:{%s}\n",__FUNCTION__, __LINE__, str); \
					return -1;	\
				}	\
			}

#define PRT_CONDX(val, str, X)  \
				{					\
					if((val) == FALSE){		\
						printf(NAME":%s%d:{%s}\n", __FUNCTION__, __LINE__, str); \
						return X;	\
					}	\
				}


typedef unsigned char      uchar;
typedef unsigned int       uint;
typedef unsigned short     ushort;
typedef unsigned long  	   ulong;
typedef unsigned long long ullong;

enum 
{
	TP_VIDEO = 0,
	TP_VIDEO_AUDIO,
	TP_MAX,
};

typedef struct
{
	char *data;
	uint  len;
}data_t;

typedef struct 
{
	FILE   *vfd;
	int    frameRate;
	uint   timeScale;
	uint   pts;
	ullong ssrc;
	ushort cseq;
	uchar  stream_id;

	data_t  *sps;
	data_t  *pps;
}Video_inf_t;

typedef struct 
{
	FILE  *afd;
	uint  timeScale;
	uint  frameSize;	
	uchar  stream_id;
}Audio_inf_t;

//network struct.
typedef struct
{
	int       		   sock;
	uint 			   addr_len;
	struct sockaddr_in sadd_in;
}net_t;

//ps file stream struct .
typedef struct 
{
	net_t		net;
	Video_inf_t  v;
	Audio_inf_t  a;
}psfs_t; 

int lm_connect_server(uchar istcp, int port, char* ip, net_t *nt);
int lm_ps_fileStream_proc(net_t *nt1, char *filename[], uchar type);
int lm_ps_liveStream_proc(int sock, char *data, uint size);

#ifdef __cplusplus
}
#endif

#endif
