
#include "lmps.h"

typedef struct  bits_buf
{
	uchar* data;
	uchar  mask;
	int size;
	int id;
}bbuff_m;

void bits_write(bbuff_m *p_buffer, int i_count, ullong i_bits);
int Send(net_t *sk1, char *data, int size);
int check_nalu_type(char *data);

void print_tnt(char *buf, int len, const char *comment)
{
	int i = 0;
	printf("%s start buf:%p len:%d\n", comment, buf, len);
	for(i = 0; i < len; i++)
	{
    	if((i>0) && (i%16 == 0))
			printf("\r\n");
    	 printf("%02x ", buf[i] & 0xff); //&0xff 是为了防止64位平台中打印0xffffffa0 这样的，实际打印0xa0即可
	}
	printf("\n");
}


inline void bits_write(bbuff_m *p_buffer, int i_count, ullong i_bits)
{
	while(i_count > 0)
	{
	    i_count--;
	    if((i_bits >> i_count )&0x01)
	    	p_buffer->data[p_buffer->id] |= p_buffer->mask;
	    else
	    	p_buffer->data[p_buffer->id] &= ~p_buffer->mask;
		
	    p_buffer->mask >>= 1;        /*操作完一个字节第一位后，操作第二位*/
	    if(p_buffer->mask == 0)     /*循环完一个字节的8位后，重新开始下一位*/
	    {
			p_buffer->id++;
			p_buffer->mask = 0x80;
	    }
	}
}

int file_init(char *files[], uchar ty, psfs_t *pft)
{
	PRT_COND(!pft, "Input pt is NULL ,exit !\n");

	const char *video_file = files[0];
	const char *audio_file = files[1];
	const char *flv_file   = files[2];

	printf("****[%s]:[%s]\n", files[0], files[1]);
	
	//video file
	if(video_file)
	{
		if(!(pft->v.vfd = fopen(video_file, "rb")) )
		{
			perror("Open vidoe file faile:");
			return -1;
		}
		
		pft->v.frameRate = 21;
		pft->v.timeScale = 3600;
		pft->v.pts 		 = 0;
		pft->v.cseq		 = 0;
		pft->v.ssrc		 = 1234567890123;
		pft->v.stream_id = 0xe0; //0x(C0~DF)指音频,0x(E0~EF)为视频,
		pft->v.sps = NULL;
		pft->v.pps = NULL;
	}
	
	//audio file, default g711x.
	if(audio_file && ty == TP_VIDEO_AUDIO)
	{
		if(!(pft->a.afd = fopen(audio_file, "rb")) )
		{
			perror("Open audio file failed:");
			return -1;
		}

		pft->a.timeScale = 22;//???????????????????
		pft->a.frameSize = 320;
		pft->a.stream_id = 0xc0;
	}

	return 0;
}

int file_destroy(psfs_t *hand)
{
	fclose(hand->v.vfd);
	fclose(hand->a.afd);
	return 0;
}

int get_file_remsize(FILE *fd)
{
	uint currpos, endpos;
	
	currpos =  ftell(fd);
	fseek(fd, 0L, SEEK_END);
	endpos = ftell(fd) - currpos;
	fseek(fd, currpos, SEEK_SET);
	
	return endpos;
}

inline int check_nalu_type(char *data)
{
	if( data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1)
		return 4;
	else if(data[0] == 0 && data[1] == 0 && data[2] == 1)
		return 3;

	return 0;
}

int get_video_frame(Video_inf_t *vinf, char *buff0, char *frame0, int *frame_sz0, uint *nal_sz)
{
	PRT_COND(!frame0 || !vinf->vfd, "Input frame0 or fd is NULL !");
	PRT_COND(!buff0, "vidoe mem is NULL !");

	int sqe = 0;
	static int l_nalsz = 0;
	static int p   = 0;
	static int len = 0;
	static int remain = 0;
	static int file_sz = 0;
	static char end_flag = 1;
	static char *start = NULL;
	
	while(1)
	{
		if(end_flag)
		{
			p = 0;
			memset(buff0, 0, MAX_VIDEO_BUFF);
			if((len = fread(buff0, 1, MAX_VIDEO_BUFF, vinf->vfd)) <= 0)
			{
				printf("++++++video file over++----->\n");
				return -2; //file over.
			}

			end_flag = 0;
		}

		if(check_nalu_type(&buff0[p]))
		{	
			(buff0[p+2] == 1)?(sqe = 3):(sqe = 4);
			if(!start)
			{ //首次检测到�? 								
				start	 = &buff0[p];
				p		+= sqe;
				l_nalsz  = sqe;
				end_flag = 0;
				continue;
			}
			else
			{
				if(remain > 0)
				{
					memcpy(frame0 + remain, &buff0[0], p); //加上 上次不完整部分数�?
					*frame_sz0 = p + remain;
					remain = 0; //上次read 剩下的不完整�?
				}
				else
				{
					*frame_sz0 = &buff0[p] - start;
					memcpy(frame0, start, *frame_sz0);
				}
				
				start	 = &buff0[p];
				p	    += sqe;
				*nal_sz  = l_nalsz;
				l_nalsz  = sqe;
				end_flag = 0;
				
				return 1;
			} 
		}
		else if(len == p)
		{//存下这次read帧中, 剩下的不完整帧数�?
			remain = &buff0[p] - start; 
			if(remain > 0)
			{
				memcpy(frame0, start, remain);
				if(get_file_remsize(vinf->vfd) == 0)
				{ //写最后一帧数�?						
					return -2;
				}
			}

			file_sz += len;
			end_flag = 1;
			continue;
		}
		
		p++;
	}

	return 0;
}

int get_audio_frame(Audio_inf_t *ainf, char *buff1, char *frame1, int *frame_sz1)
{
	PRT_COND(!frame1 || !ainf->afd, "Input frame1 or fd is NULL !");
	PRT_COND(!buff1, "audio mem is NULL !");
	
	static int i = 0;
	static int re_sz = 0;
	static int a_remain = 0;
	static char end_flag = 1;
	static uchar fullframe = 1;
			
	while(1)
	{	
		if(end_flag)
		{
			memset(buff1, 0, MAX_AUDIO_BUFF);
			re_sz = fread(buff1, 1, MAX_AUDIO_BUFF, ainf->afd);
			//printf("size[%d]\n", re_sz);
			
			PRT_CONDX(re_sz<= 0, "read over !", -2);
			end_flag = 0;
		}
		
		if(ainf->frameSize > 0)
		{
			if(re_sz >= *frame_sz1)
			{
				memcpy(frame1, &buff1[i], *frame_sz1);
				i += *frame_sz1;	//加一帧的长度.
				re_sz -= *frame_sz1;//剩下的帧数据...
				
				return 0;
			}
			else
			{ //剩下帧不完整.
				memcpy(frame1, &buff1[i], re_sz); //保存剩下的帧�?
				a_remain = *frame_sz1 - re_sz;
				
				end_flag  = fullframe = 0;
				 i = re_sz = 0;
				end_flag = 1;
				continue;
			}
		}
		else
		{
			if(a_remain > 0)
			{//处理上次不完整的�?
				if(!fullframe)
				{
					fullframe = 1;
					memcpy(frame1 , &buff1[i], a_remain);
					i += a_remain;	 
					re_sz -= a_remain;
					a_remain = 0;
					return 0;
				}
				end_flag = 1;
			}
		}
		i++;
	} 

	return 0;
}

//dynamic: system clock, System clock ext.
int pack_ps_header(char *data, ullong s64Scr)
{
	ullong lScrExt = (s64Scr) % 100;  
	//由于sdp协议返回的video的频率是90000，帧率是25帧/s，所以每次递增的量是3600.
	//实际你应该根据你自己编码里的时间戳来处理, 以保证时间戳的增量为3600,这里除以100, 若不对，可能导致卡顿现象.
	
	bbuff_m  bitsBuffer;
	bitsBuffer.size = PS_HDR_LEN; 
	bitsBuffer.id 	= 0;
	bitsBuffer.mask = 0x80; // 二进制：10000000 这里是为了后面对一个字节的每一位进行操作，避免大小端夸字节字序错乱
	bitsBuffer.data = (uchar *)(data);
	memset(bitsBuffer.data, 0, PS_HDR_LEN);
	
	bits_write(&bitsBuffer, 32, 0x000001BA);		/*start codes*/
	bits_write(&bitsBuffer, 2,  1);					/*marker bits '01b'*/
	bits_write(&bitsBuffer, 3,  (s64Scr>>30)&0x07);	/*System clock [32..30]*/
	bits_write(&bitsBuffer, 1,  1);					/*marker bit*/
	bits_write(&bitsBuffer, 15, (s64Scr>>15)&0x7FFF);/*System clock [29..15]*/
	bits_write(&bitsBuffer, 1,  1);			        /*marker bit*/
	bits_write(&bitsBuffer, 15, s64Scr&0x7fff);		/*System clock [14..0]*/
	bits_write(&bitsBuffer, 1,  1);			        /*marker bit*/
	bits_write(&bitsBuffer, 9,  lScrExt&0x01ff);    /*System clock ext*/
	bits_write(&bitsBuffer, 1,  1);					/*marker bit*/
	bits_write(&bitsBuffer, 22, (255)&0x3fffff);	/*bit rate(n units of 50 bytes per second.)*/
	bits_write(&bitsBuffer, 2,  3);					/*marker bits '11'*/
	bits_write(&bitsBuffer, 5,  0x1f);		  		/*reserved(reserved for future use)*/
	bits_write(&bitsBuffer, 3,  0);					/*stuffing length*/
	return 0;
}

int pack_ps_system_header(char *data)
{
	bbuff_m   bitsBuffer;
	bitsBuffer.size = SYS_HDR_LEN;
	bitsBuffer.id   = 0;
	bitsBuffer.mask = 0x80;
	bitsBuffer.data = (uchar *)(data);
	memset(bitsBuffer.data, 0, SYS_HDR_LEN);
	
	/*system header*/
	bits_write(&bitsBuffer, 32, 0x000001BB);   /*start code*/
	bits_write(&bitsBuffer, 16, SYS_HDR_LEN-6);/*header_length 表示次字节后面的长度，后面的相关头也是次意思*/
	bits_write(&bitsBuffer, 1,  1);            /*marker_bit*/
	bits_write(&bitsBuffer, 22, 50000);    	   /*rate_bound*/
	bits_write(&bitsBuffer, 1,  1);            /*marker_bit*/
	bits_write(&bitsBuffer, 6,  1);            /*audio_bound*/
	bits_write(&bitsBuffer, 1,  0);            /*fixed_flag */
	bits_write(&bitsBuffer, 1,  1);            /*CSPS_flag */
	bits_write(&bitsBuffer, 1,  1);            /*system_audio_lock_flag*/
	bits_write(&bitsBuffer, 1,  1);            /*system_video_lock_flag*/
	bits_write(&bitsBuffer, 1,  1);          /*marker_bit*/
	bits_write(&bitsBuffer, 5,  1);          /*video_bound*/
	bits_write(&bitsBuffer, 1,  0);          /*dif from mpeg1*/
	bits_write(&bitsBuffer, 7,  0x7F);       /*reserver*/
	
	/*audio stream bound*/
	bits_write(&bitsBuffer, 8,  0xC0);       /*stream_id*/
	bits_write(&bitsBuffer, 2,  3);          /*marker_bit */
	bits_write(&bitsBuffer, 1,  0);          /*PSTD_buffer_bound_scale*/
	bits_write(&bitsBuffer, 13, 512);        /*PSTD_buffer_size_bound*/
	
	/*video stream bound*/
	bits_write(&bitsBuffer, 8,  0xE0);       /*stream_id*/
	bits_write(&bitsBuffer, 2,  3);          /*marker_bit */
	bits_write(&bitsBuffer, 1,  1);          /*PSTD_buffer_bound_scale*/
	bits_write(&bitsBuffer, 13, 2048);       /*PSTD_buffer_size_bound*/

	return 0;
}

int pack_ps_system_map(char *data)
{
	bbuff_m   bitsBuffer;
	bitsBuffer.size = PSM_HDR_LEN;
	bitsBuffer.id   = 0;
	bitsBuffer.mask = 0x80;
	bitsBuffer.data = (uchar *)(data);
	memset(bitsBuffer.data, 0, PSM_HDR_LEN);

	bits_write(&bitsBuffer, 24,0x000001);/*start code*/
	bits_write(&bitsBuffer, 8, 0xBC);	 /*map stream id*/
	bits_write(&bitsBuffer, 16,18);	 	 /*program stream map length*/ 
	bits_write(&bitsBuffer, 1, 1); 		 /*current next indicator */
	bits_write(&bitsBuffer, 2, 3); 	 	 /*reserved*/
	bits_write(&bitsBuffer, 5, 0); 		 /*program stream map version*/
	bits_write(&bitsBuffer, 7, 0x7F);    /*reserved */
	bits_write(&bitsBuffer, 1, 1); 		 /*marker bit */
	bits_write(&bitsBuffer, 16,0); 		 /*programe stream info length*/
	bits_write(&bitsBuffer, 16, 8);		 /*elementary stream map length  is*/

	/*audio*/
	bits_write(&bitsBuffer, 8, 0x90);	 /*stream_type*/
	bits_write(&bitsBuffer, 8, 0xC0);    /*elementary_stream_id*/
	bits_write(&bitsBuffer, 16, 0);	 	 /*elementary_stream_info_length is*/

	/*video*/
	bits_write(&bitsBuffer, 8, 0x1B);	 /*stream_type*/
	bits_write(&bitsBuffer, 8, 0xE0);    /*elementary_stream_id*/
	bits_write(&bitsBuffer, 16, 0);	     /*elementary_stream_info_length */

	/*crc (2e b9 0f 3d)*/
	bits_write(&bitsBuffer, 8, 0x45);	 /*crc (24~31) bits*/
	bits_write(&bitsBuffer, 8, 0xBD);	 /*crc (16~23) bits*/
	bits_write(&bitsBuffer, 8, 0xDC);	 /*crc (8~15)  bits*/
	bits_write(&bitsBuffer, 8, 0xF4);	 /*crc (0~7)   bits*/
	return 0;
}

//dynamic: stream_id, payload_len, pts, dts.
int pack_pes_header(char *data, int stream_id, int payload_len, ullong pts, ullong dts)
{
	bbuff_m   bitsBuffer;
	bitsBuffer.size = PES_HDR_LEN;
	bitsBuffer.id   = 0;
	bitsBuffer.mask = 0x80;
	bitsBuffer.data = (uchar *)(data);
	memset(bitsBuffer.data, 0, PES_HDR_LEN);

	/*system header*/
	bits_write(&bitsBuffer, 24,0x000001);    /*start code*/
	bits_write(&bitsBuffer, 8, (stream_id)); /*streamID*/
	bits_write(&bitsBuffer, 16,(payload_len)+13);  /*packet_len*///指出pes分组之前的 ps_hdr+sys_hdr+psm_hdr + 该字节后的长度[13Byte].
	bits_write(&bitsBuffer, 2, 2);	 /*'10'*/
	bits_write(&bitsBuffer, 2, 0);	 /*scrambling_control*/
	bits_write(&bitsBuffer, 1, 0);	 /*priority*/
	bits_write(&bitsBuffer, 1, 0);	 /*data_alignment_indicator*/
	bits_write(&bitsBuffer, 1, 0);	 /*copyright*/
	bits_write(&bitsBuffer, 1, 0);	 /*original_or_copy*/
	bits_write(&bitsBuffer, 1, 1);	 /*PTS_flag*/
	bits_write(&bitsBuffer, 1, 1);	 /*DTS_flag*/
	bits_write(&bitsBuffer, 1, 0);	 /*ESCR_flag*/
	bits_write(&bitsBuffer, 1, 0);	 /*ES_rate_flag*/
	bits_write(&bitsBuffer, 1, 0);	 /*DSM_trick_mode_flag*/
	bits_write(&bitsBuffer, 1, 0);	 /*additional_copy_info_flag*/
	bits_write(&bitsBuffer, 1, 0);	 /*PES_CRC_flag*/
	bits_write(&bitsBuffer, 1, 0);	 /*PES_extension_flag*/
	bits_write(&bitsBuffer, 8, 10);	 /*header_data_length*/ 
	// 指出包含在 PES 分组标题中的可选字段和任何填充字节所占用的总字节数。该字段之前
	// 的字节指出了有无可选字段。

	/*PTS,DTS*/ 
	bits_write(&bitsBuffer, 4, 3); 				   	   /*'0011'*/
	bits_write(&bitsBuffer, 3, ((pts)>>30)&0x07);	   /*PTS[32..30]*/
	bits_write(&bitsBuffer, 1, 1);
	bits_write(&bitsBuffer, 15,((pts)>>15)&0x7FFF);    /*PTS[29..15]*/
	bits_write(&bitsBuffer, 1, 1);
	bits_write(&bitsBuffer, 15,(pts)&0x7FFF);		   /*PTS[14..0]*/
	bits_write(&bitsBuffer, 1, 1);
	bits_write(&bitsBuffer, 4, 1); 				   	   /*'0001'*/
	bits_write(&bitsBuffer, 3, ((dts)>>30)&0x07);	   /*DTS[32..30]*/
	bits_write(&bitsBuffer, 1, 1);
	bits_write(&bitsBuffer, 15,((dts)>>15)&0x7FFF);    /*DTS[29..15]*/
	bits_write(&bitsBuffer, 1, 1);
	bits_write(&bitsBuffer, 15,(dts)&0x7FFF);		   /*DTS[14..0]*/
	bits_write(&bitsBuffer, 1, 1);
	return 0;
}

int pack_rtp(char *data, uchar marker, ushort cseq, ullong curpts, uint ssrc)
{
	bbuff_m   bitsBuffer;
	bitsBuffer.size = RTP_HDR_LEN;
	bitsBuffer.id   = 0;
	bitsBuffer.mask = 0x80;
	bitsBuffer.data = (uchar *)(data);
	memset(bitsBuffer.data, 0, RTP_HDR_LEN);

	bits_write(&bitsBuffer, 2, RTP_VERSION);  /* rtp version  */
	bits_write(&bitsBuffer, 1, 0);		 	  /* rtp padding  */
	bits_write(&bitsBuffer, 1, 0);		 	  /* rtp extension	*/
	bits_write(&bitsBuffer, 4, 0);		      /* rtp CSRC count */
	bits_write(&bitsBuffer, 1, (marker));     /* rtp marker	*/
	bits_write(&bitsBuffer, 7, 96); 	      /* rtp payload type*/
	bits_write(&bitsBuffer, 16, (cseq));	  /* rtp sequence	 */
	bits_write(&bitsBuffer, 32, (curpts));	  /* rtp timestamp	 */
	bits_write(&bitsBuffer, 32, (ssrc));	  /* rtp SSRC    */

	return 0;
}

inline int Send(net_t *sk1, char *data, int size)
{
	int ret = sendto(sk1->sock, data, size, 0, (struct sockaddr *)&sk1->sadd_in, sk1->addr_len);
	if (ret <= 0) {
		printf("sendto failed , error:%s\n", strerror(errno));
		return -1;
	}

	//printf("Send data %d <=> %d:\n",  ret, size);
	//print_tnt(data,  size / 10 , "Send");
	
	return ret;
}

int send_data(net_t *sk1,  Video_inf_t *v2, char* data, int size) 
{
	//printf("\n=============[%d]=============\n", size);
	int ret = 0, tempsz = size, sendsz0, offsz = 0;
	int times = 0;
	uchar tmpmarker = 0;

	print_tnt(data, 120, "start");
	if(tempsz > RTP_MAX_PACKET_BUFF)
	{
		ret = Send(sk1, data, RTP_MAX_PACKET_BUFF);
		PRT_COND(ret < 0, "Send data failed !");
		
		tempsz -= RTP_MAX_PACKET_BUFF;
		offsz += RTP_MAX_PACKET_BUFF;
		do{
			//printf("********[%d]*********\n", times++);
			sendsz0 = ((tempsz - RTP_MAX_PACKET_BUFF) > 0) ? RTP_MAX_PACKET_BUFF : (tmpmarker = 1, tempsz);
			//printf("kkkkkkkkkk[%d:%d:%d]kkkkkkkkk\n", sendsz0, tempsz, offsz);
			pack_rtp(data + offsz - RTP_HDR_LEN, tmpmarker, ++v2->cseq, v2->pts, v2->ssrc);
			
			ret = Send(sk1, data + offsz - RTP_HDR_LEN, sendsz0 + RTP_HDR_LEN);
			PRT_COND(ret < 0, "Send data failed !");

			tempsz -= ret;
			offsz += ret;
		}while(tempsz > 0);
	}
	else
	{
		ret = Send(sk1, data, size);
		PRT_COND(ret < 0, "Send data failed !");
	}

	return ret;
}

int pack_video(Video_inf_t *v0, char *data, int *size, int stacod0)
{	
	PRT_COND(!data, "Input data is NULL !");

	int sz = RTP_HDR_LEN;
	uchar naluType = data[stacod0]&0x1F;
	char* tmpStart = NULL;
	
	if(naluType == 0x05)
	{
		tmpStart = data - MAX_EMPYT_SIZE;
		pack_rtp(tmpStart, 0, ++v0->cseq, v0->pts, v0->ssrc);
		pack_ps_header(tmpStart+sz, v0->pts);
		sz += PS_HDR_LEN;
		pack_ps_system_header(tmpStart+sz);
		sz += SYS_HDR_LEN;
		pack_ps_system_map(tmpStart+sz);
		sz += PSM_HDR_LEN;
		pack_pes_header(tmpStart+sz, v0->stream_id, sz, v0->pts, v0->pts);
		sz += PES_HDR_LEN;
	}
	/*
	else if(naluType == 0x07 || naluType == 0x08)
	{
		pack_pes_header(data+sz, v0->stream_id, sz, v0->timestamp, v0->timestamp);
		return 2;
	}*/
	else
	{
		tmpStart = data - MAX_EMPYT_SIZE + SYS_HDR_LEN + PSM_HDR_LEN;
		pack_rtp(tmpStart, 0, ++v0->cseq, v0->pts, v0->ssrc);
		pack_ps_header(tmpStart+sz, v0->pts);
		sz += PS_HDR_LEN;
		pack_pes_header(tmpStart+sz, v0->stream_id, sz, v0->pts, v0->pts);
		sz += PES_HDR_LEN;
	}

	*size += sz;
	
	return sz;
}

int pack_audio(Audio_inf_t *a0, char *data, int *size)
{
	#if 0
	pack_pes_header();
	pack_rtp(data, size);
	#endif
	
	return 0;
}

int send_av_stream(net_t *sk, Video_inf_t *v1, char *data, int len)
{
	int sz1 = 0, sendsz = 0, tmpsize = len;
	
	if(len > PS_PES_PAYLOAD_SIZE) //some video frame size >= 65k.
	{
		send_data(sk, v1, data, PS_PES_PAYLOAD_SIZE);
		tmpsize -= PS_PES_PAYLOAD_SIZE;
		sz1 += PS_PES_PAYLOAD_SIZE;
		while(tmpsize > 0)
		{
			sendsz = ((tmpsize - PS_PES_PAYLOAD_SIZE) > 0) ? PS_PES_PAYLOAD_SIZE : tmpsize;
			sz1 += sendsz;
			
			pack_rtp(data + sz1 - PES_SPLIT_SZ, 0, ++v1->cseq, v1->pts, v1->ssrc); //使用上次的空间来存放接下来封装的PES_HDR和RTP_HDR.
			pack_pes_header(data + sz1 - PES_HDR_LEN, v1->stream_id, sendsz, v1->pts, v1->pts);
			send_data(sk, v1, data + sz1 - PES_SPLIT_SZ, sendsz + PES_SPLIT_SZ);
			
			tmpsize -= sendsz; 
		}
	}
	else
	{
		send_data(sk, v1, data, len);
	}
	
	return 0;
}

int send_avps_pack(psfs_t *pf)
{
	uint time_v = 0, time_a = 0, offsz = 0;
	int ret = 0, avsize = 0, stacod = 0;

	net_t 		*sck = &pf->net;
	Video_inf_t  *v	 = &pf->v;
	Audio_inf_t  *a  = &pf->a;
	
	char video_end = 0; //file over.
	char audio_end = 0;

	char *temp = NULL;
	char *v_buff, *a_buff, *av_frame;
	char *vbuf,   *abuf, *avframe;

	int ed = 0,	toal = 0, currframe = 0;
	
	vbuf 	= v_buff   = (char *)malloc(MAX_VIDEO_BUFF);
	abuf 	= a_buff   = (char *)malloc(MAX_AUDIO_BUFF);
	avframe = av_frame = (char *)malloc(MAX_V_FAME_BUFF);
	if(!v_buff || !a_buff || !av_frame)
	{
		if(avframe != NULL)   free(avframe);
		if(vbuf != NULL)  free(vbuf);	
		if(abuf != NULL)  free(abuf);
		
		printf("Malloc file proc [v_buff or a_buff or av_frame] mem failed.\n");
		return -1;
	}

	while(1)
	{
		if((v->vfd) && (!video_end))
		{ // video.  
			if((time_v <= time_a) || (!a->afd))
			{	
				ret = get_video_frame(v, v_buff, &av_frame[MAX_EMPYT_SIZE], &avsize, &stacod);
				if(ret == -2){
					video_end = 1;
					printf("ugdsaddasdaddasd##############==>\n");
				}

				temp = &av_frame[MAX_EMPYT_SIZE];
				
				currframe = avsize;
				time_v += 1000/v->frameRate;
				ret = pack_video(v, &av_frame[MAX_EMPYT_SIZE], &avsize, stacod);
				av_frame += (MAX_EMPYT_SIZE - ret);//当p帧时,不需要封装sys_hdr, sys_map, av_frmae指向的数据开始位需要调整.

				//printf("++++++++=:[%4d]#[%6d]#[%6d]#[%6d]==[%02x %d]=\n", ed++, currframe, avsize, toal, 0XFF & temp[stacod], stacod);
				toal += avsize;
	
				ret = send_av_stream(sck, v, av_frame, avsize);				
				v->pts += v->timeScale;
			}
			else if(audio_end == 1) {
				//time_a += (1024000/(a->sampling * a->chns));
			 }
		}

		if((a->afd) && (!audio_end))
		{// audio. 
			printf("uuuubbbbbbbbbbddfsdfsd===>\n");
			if(time_v > time_a)
			{	
  				ret = get_audio_frame(a, a_buff, av_frame, &avsize);
				if(ret == -2)
					audio_end = 1;
								
				//time_a += (1024000/(a->sampling * a->chns));
				pack_audio(a, av_frame, &avsize);
			}
			else if(video_end == 1){
				time_v += 1000/v->frameRate;
			}
		}

		if(video_end && !a->afd)
			break;
		else if(audio_end && video_end)
			break;

		usleep(11 *v->timeScale);
	}
	
	if(avframe != NULL)   free(avframe);
	if(vbuf != NULL)  free(vbuf);	
	if(abuf != NULL)  free(abuf);

	return 0;
}

int lm_connect_server(uchar istcp, int port, char* ip, net_t *nt)
{
	PRT_COND(port <= 1024, "Input port should be greater than 1024, exit !");
	PRT_COND(!ip || !nt, "Input server ip or nt is NULL, exit !");
		
	int sock;
	if(istcp)
		sock = socket(AF_INET, SOCK_STREAM, 0);
	else
		sock = socket(AF_INET, SOCK_DGRAM, 0);

	if(sock < 0)
	{
		printf("socket failed, error:%s \n", strerror(errno));
		return -1;
	}

	socklen_t addrlen = sizeof(struct sockaddr); 
	struct sockaddr_in sk;
	memset(&sk, 0, addrlen);

	sk.sin_family 	   = AF_INET;
	sk.sin_port		   = htons(port);
	sk.sin_addr.s_addr = inet_addr(ip);

	nt->sock     = sock;
	nt->addr_len = addrlen;
	nt->sadd_in  = sk;
		
	return 0;
}

int lm_ps_fileStream_proc(net_t *nt, char *filename[], uchar type)
{
	int ret;
	psfs_t pft;
	memset(&pft, 0, sizeof(psfs_t));
	
	pft.net.sock     = nt->sock;
	pft.net.addr_len = nt->addr_len;
	pft.net.sadd_in  = nt->sadd_in;
		
	ret = file_init(filename, type, &pft);
	if(ret < 0)
	{
		close(nt->sock);
		printf("file_init function failed !\n");
		return -1;
	}
	
	send_avps_pack(&pft);
	
	file_destroy(&pft);
	close(nt->sock);
	return 0;
}

int lm_ps_liveStream_proc(int sock, char *data, uint size)
{

	return 0;
}

