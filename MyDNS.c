/*
 * MyDNS.c
 *
 *  Created on: 2016年3月25日
 *      Author: apple
 */
#include "MyDNS.h"

void Nowtime()
{
	struct timeval tv;
	gettimeofday(&tv,NULL);
	struct tm *p;
	p = gmtime((time_t *)&tv.tv_sec);
	printf("[%02d:%02d:%02d.%06ld] ",(p->tm_hour+8)%24, p->tm_min, p->tm_sec, tv.tv_usec);
}

uint32_t UdpOpenNoBlock()
{
	uint32_t ret = -1;
	uint32_t sockfd;
	uint32_t flags;
	sockfd = socket(AF_INET,SOCK_DGRAM,0);
	if(sockfd < 0)
	{
		debug_info("socker() error");
		return ret;
	}
	flags = fcntl(sockfd,F_GETFL,0);
	flags |= O_NONBLOCK;
	if(fcntl(sockfd,F_SETFL,flags) < 0)
	{
		debug_info("fcntl() error");
		return ret;
	}
	ret = sockfd;
	return ret;
}
uint32_t UdpSend(uint32_t sockfd,uint8_t *buffer,uint32_t len,uint8_t *Domain,uint32_t Port)
{
	uint32_t sendlen = -1;
	struct sockaddr_in sockaddr;
	bzero(&sockaddr,sizeof(sockaddr));
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(Port);
	inet_pton(AF_INET,Domain,&sockaddr.sin_addr);

	sendlen = sendto(sockfd,buffer,len,0,(struct sockaddr *)&sockaddr,sizeof(sockaddr));
	if(sendlen <= 0)
		debug_info("sendto error");

	return sendlen;
}

/* = 0 no data to recv
 * > 0 return recvlen
 * < 0 sockfd error or timeout must reboot socket 	 */
uint32_t UdpRecv(uint32_t sockfd,uint8_t *buffer,uint32_t len,uint32_t mstimeout)
{
	uint32_t recvlen = 0;
	struct sockaddr_in sockaddr;
	bzero(&sockaddr,sizeof(sockaddr));
	socklen_t socklen = sizeof(sockaddr);
	fd_set readfd;
	struct timeval tv;
	struct timespec ts;
	tv.tv_sec = mstimeout / 1000;
	tv.tv_usec = mstimeout % 1000 * 1000;

	FD_ZERO(&readfd);
	FD_SET(sockfd,&readfd);

	if(select(sockfd + 1,&readfd,NULL,NULL,&tv) > 0)
	{
		if(FD_ISSET(sockfd,&readfd))
		{
			recvlen = recvfrom(sockfd,buffer,len,0,(struct sockaddr *)&sockaddr,&socklen);
			if(recvlen > 0)
				return recvlen;
			if(recvlen < 0)
				debug_info("recvfrom error");
		}
	}
	if(recvlen == 0)
		debug_info("There is no data to read");
	return recvlen;
}

void Package_Sendbuf(uint8_t *Domain,DNSInfo *dns,char *buffer)
{
	DNSInfo *dns_info = dns;
	uint32_t i,j = 0,Position = 0;

	uint16_t *randbuff = NULL;
	randbuff = (uint16_t *)buffer;

	strcpy(dns_info->Domain,Domain);
	dns_info->Domainlen = strlen(dns_info->Domain);


	randbuff[0] = rand();						//ID
	dns_info->randid = randbuff[0];

	Position ++;
	Position ++;
	buffer[Position++] = 0x01;					//Flags
	buffer[Position++] = 0x00;					//Flags 			0x0100

	buffer[Position++] = 0x00;					//Questions;
	buffer[Position++] = 0x01;					//Questions;		0x0001

	buffer[Position++] = 0x00;					//Answer RRs;
	buffer[Position++] = 0x00;					//Answer RRs;		0x0000

	buffer[Position++] = 0x00;					//Authority RRs;
	buffer[Position++] = 0x00;					//Authority RRs;	0x0000

	buffer[Position++] = 0x00;					//Additional RRs;
	buffer[Position++] = 0x00;					//Additional RRs;	0x0000

	/***************************** Queries *******************************/
	for(i = 0;i < dns_info->Domainlen; i ++)
	{
		if(dns_info->Domain[i] == '.')
		{
			buffer[Position] = j;
			Position += j + 1;
			j = 0;
		}
		else
		{
			buffer[12 + i + 1] = Domain[i];
			j++;
		}
	}
	buffer[Position] = j;
	Position += j + 1;
	buffer[Position++] = 0x00;					//end Queries

	buffer[Position++] = 0x00;					//Type
	buffer[Position++] = 0x01;					//Type

	buffer[Position++] = 0x00;					//Class
	buffer[Position++] = 0x01;					//Class
}

uint32_t DNSSend(uint8_t *Domain,DNSInfo *dns)
{
	DNSInfo *dns_info = dns;
	uint32_t ret = -1;
	uint32_t sendlen;
	uint8_t buffer[256];

	dns_info->sockfd = UdpOpenNoBlock();
	if(dns->sockfd < 0)
		return ret;

	Package_Sendbuf(Domain,dns_info,buffer);

	sendlen = UdpSend(dns_info->sockfd,buffer,strlen(buffer),"192.168.1.1",53);
	debug_info("sendlen = %d",sendlen);
	if(sendlen != strlen(buffer))
		return ret;

	return sendlen;
}

void parse(uint8_t *buffer,uint32_t *param)
{
	*param = *buffer << 8;
	buffer ++;
	*param |= *buffer;
	buffer ++;
}

void parseNAME(uint8_t *buffer,uint8_t *Domain)
{
	int i;
	int j = *buffer;
	do{
		for(i = 0;i < j;i++)
		{
			buffer ++;
			Domain[i] = *buffer;
		}
		buffer ++;
		j = *buffer;
	}while(j != 0);
	buffer ++;
	buffer ++;
	buffer ++;
	buffer ++;
	buffer ++;
}

uint32_t Parse_Rcvbuf(DNSInfo *dns,uint8_t *buffer,IPInfo *ip)
{
	DNSInfo *dns_info = dns;
	uint32_t ret = -1;

	uint32_t transID;
	parse(buffer,&transID);
	debug_info("transID = %#x",transID);
	if(transID != dns_info->randid)
	{
		debug_info("recvbuf error");
		return ret;
	}
	else
	{
		uint32_t Flags;
		parse(buffer,&Flags);
		debug_info("Flags = %#x",Flags);

		uint32_t Question;
		parse(buffer,&Question);
		debug_info("Question = %#x",Question);

		uint32_t AnserRRs;
		parse(buffer,&AnserRRs);
		debug_info("AnserRRs = %#x",AnserRRs);

		uint32_t AdditionalRRs;
		parse(buffer,&AdditionalRRs);
		debug_info("AdditionalRRs = %#x",AdditionalRRs);

		uint8_t *Domain;
		parseNAME(buffer,Domain);
		debug_info("Domain = %s",Domain);

		//point to name
		while(*buffer == 0xc0)
		{
			buffer += 2; 				//->type;
			buffer += 2;				//->Class;
			buffer += 2;				//->TTL;
			buffer += 4;				//->length
			buffer += 1;				//->little
			if(*buffer != 4)			//not ip;
			{
				buffer += 1;
				buffer += *buffer;
			}
			else						//is ip
			{
				IPInfo *iiippp = malloc(sizeof(iiippp));
				buffer += 1;
				ip->IP[0] = *(buffer++);
				ip->IP[1] = *(buffer++);
				ip->IP[2] = *(buffer++);
				ip->IP[4] = *(buffer++);
				debug_info("ip = %s",ip->IP);
				ip->next = iiippp;
				iiippp->prev = ip;
			}
		}
	}
	return ret;
}

uint32_t DNSRecv(DNSInfo *dns,IPInfo *ip)
{
	uint32_t ret = -1;
	uint8_t recvbuf[512];
	uint32_t recvlen;
	DNSInfo *dns_info = dns;
	recvlen = UdpRecv(dns_info->sockfd,recvbuf,512,0);
	if(recvlen <= 0)
		return ret;

	/**************************Parse recvbuf********************************/
	if(Parse_Rcvbuf(dns_info,recvbuf,ip) < 0)
		return ret ;
	ret = 1;
	return ret;
}




