/* (c)jp cocatrix May 2016 
 *
 * Copyright 2016 karawin (http://www.karawin.fr)

	quick and dirty telnet inplementation for wifi webradio
	minimal implementaion for log and command
*/
#include "lwip/sockets.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/api.h"
#include "esp_wifi.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
//#include "ssl/ssl_crypto.h"
#include "cencode_inc.h"
#include "esp_system.h"
#include "telnet.h"
#include "interface.h"



//const char strtMALLOC1[] = {"Telnet %s malloc fails\n"};
const char strtSOCKET[]  = {"Telnet Socket fails %s errno: %d\n"};
const char strtWELCOME[]  ={"Karadio telnet\n> "};


int telnetclients[NBCLIENTT];
//set of socket descriptors
// reception buffer
static char brec[256];
static char iac[3];
static bool inIac = false; // if in negociation
static char *obrec;
static uint16_t irec;
static uint8_t iiac;

///////////////////////
// init some data
void telnetinit(void)
{
	int i;
	for (i = 0;i<NBCLIENTT;i++) 
	{
		telnetclients[i] = -1;
	}
	memset(brec,0,sizeof(brec));
	irec = 0;
	iiac = 0;
	obrec = malloc(2);
}

/////////////////////////////////////////////////////////////////////
// a socket with a websocket request. Note it and answer to the client
bool telnetnewclient(int socket)
{
	int i ;
//	printf("ws newclient:%d\n",socket);
	for (i = 0;i<NBCLIENTT;i++) if (telnetclients[i] == socket) return true;
	else
	for (i = 0;i<NBCLIENTT;i++) if (telnetclients[i] == -1) 
	{
		telnetclients[i] = socket;
		return true;
	}	
	return false; // no more room
}
/////////////////////////////////////////////////////////////////////
// remove the client in the list of clients
void telnetremoveclient(int socket)
{
	int i ;
//	printf("ws removeclient:%d\n",socket);
	for (i = 0;i<NBCLIENTT;i++) 
		if (telnetclients[i] == socket) 
		{
			telnetclients[i] = -1;
//			printf("ws removeclient:%d removed\n",socket);
			close(socket);
			return;
		}
}
////////////////////////
// is socket a telnet one?
bool istelnet( int socket)
{
	int i ;
	for (i = 0;i<NBCLIENTT;i++) 
		if ((telnetclients[i]!= -1)&&(telnetclients[i] == socket)) return true;
	return false;
}


bool telnetAccept(int tsocket)
{
	if ((!istelnet(tsocket ))&&(telnetnewclient(tsocket))) 
	{
//			printf("telnet write accept\n");
			write(tsocket, strtWELCOME, strlen(strtWELCOME));  // reply to accept	
			return true;
	} else close(tsocket);
	return false;
}


//broadcast a txt data to all clients
void telnetWrite(uint32_t lenb,const char *fmt, ...)
{
	int i ;
	char *buf = NULL;
//	char* lfmt;
	int rlen;
	buf = (char *)malloc(lenb+1);
	if (buf == NULL) return;
	buf[0] = 0;
	strcpy(buf,"ok\n");
	
	va_list ap;
	va_start(ap, fmt);	
	rlen = 0;
/*	if (fmt> (char*)0x40100000)  // in flash
	{
		len = strlen(fmt);
		lfmt = (char *)malloc(len+16);
		if (lfmt!=NULL)
		{
			flashRead( lfmt, fmt, len );
			lfmt[len] = 0; // if aligned, trunkate
//			printf("lfmt: %s\n",lfmt);
			rlen = vsprintf(buf,lfmt, ap);
			free (lfmt);
		}
	}	
	else */
	{
		rlen = vsprintf(buf,fmt, ap);		
	}
	va_end(ap);
	buf = realloc(buf,rlen+1);
	if (buf == NULL) return;
	// write to all clients
	for (i = 0;i<NBCLIENTT;i++)	
		if (istelnet( telnetclients[i]))
		{
			write( telnetclients[i],  buf, strlen(buf));
		}	
		
	free (buf);

}

void telnetNego(int tsocket)
{
	const uint8_t NONEG[2] = {0xFF,0xFC}; // WON't

	if (iiac == 2)
	{
	// refuse all
		if (iac[0] == 251) { write(tsocket,NONEG,2);write(tsocket,iac+1,1);}
	}
	else
	{
		if (iac[0] == 246) write(tsocket,"\n>",2);  // are you there
	}
}
	
void telnetCommand(int tsocket)
{
	if (irec == 0) return;
//printf(PSTR("%sHEAPd0: %d #\n"),"##SYS.",xPortGetFreeHeapSize( ));	
	brec[irec] = 0x0;
	write(tsocket,"\n> ",1);
//	printf("%s\n",brec);
	obrec = realloc(obrec,strlen(brec)+1);
	strcpy(obrec,brec); // save old command
	checkCommand(irec, brec);
	write(tsocket,"> ",2);
	irec = 0;
}

int telnetRead(int tsocket)
{
	char *buf ;
	int32_t recbytes ;
	int i;	
	buf = (char *)malloc(MAXDATAT);	
	recbytes = 0;
    if (buf == NULL)
	{
		vTaskDelay(100); // wait a while and retry
		buf = (char *)malloc(MAXDATAT);	
	}	
	if (buf != NULL)
	{
		recbytes = read(tsocket , buf, MAXDATAT);

		if (recbytes <= 0) {
			if ((errno != EAGAIN )&& (errno != ENOTCONN) &&(errno != 0 ))
			{
				if (errno != ECONNRESET )
				{
					printf (strtSOCKET,"read", errno);	
				} 
			} 
			free(buf);
			return 0; // free the socket
		}	

		buf = realloc(buf,recbytes+2);
//		printf(PSTR("%sHEAPdi1: %d #\nrecbytes: %d\n"),"##SYS.",xPortGetFreeHeapSize(),recbytes);	
		if (buf != NULL)
		{
			for (i = 0;i< recbytes;i++)
			{
//				printf("%x ",buf[i]);
				if (!inIac)
				switch(buf[i]){
				case '\r':
				case '\n':
					telnetCommand(tsocket);
					break;
				case 0x08:	//backspace
				case 0x7F:	//delete
					if (irec >0) --irec;
					break;
				case 0x1B:
					if (i+2 <= recbytes)
					{
						if ((buf[i+1]=='[') && (buf[i+2]=='A')) // arrow up
						{
							strcpy(brec,obrec); 
							write(tsocket,"\r",1);
							write(tsocket,brec,strlen(brec));
							irec = strlen(brec);
							buf = realloc(buf,2);
							vTaskDelay(2);	
							telnetCommand(tsocket);
						}						
						i =recbytes; // exit for
					}
					break;
				case 0xff: // iac
					inIac = true;
				break;
				default:
					brec[irec++] = buf[i];
					if (irec == sizeof(brec)) irec = 0;	
				}
				else // in iac
				{
					iac[iiac++] = buf[i];
					if (iiac == 2)
					{	
						telnetNego(tsocket);
						inIac = false;
						iiac = 0;
					}
				}
			}	
			free(buf);	
		}		
	}
	return recbytes;
}



