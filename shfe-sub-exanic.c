#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <netinet/if_ether.h>
#include <netinet/udp.h>
#include <arpa/inet.h>

#include <exanic/exanic.h>
#include <exanic/fifo_rx.h>
#include <exanic/fifo_tx.h>
#include <exanic/util.h>

#define ETHTYPE_OFFSET                  12
#define DOT1Q_ETHTYPE_OFFSET            16
#define IP_ID_OFFSET  			18
#define DST_IP_OFFSET 			30
#define PAYLOAD_OFFSET			42
#define IPV4_PACKET                     0x0008
#define DOT1Q_FRAME                     0x0081
#define DST_IP_FILTER 			0xefefef09	//239.239.239.8 is DST IP we want to read

//Define price entry
typedef struct PRICE_ENTRY {
        uint32_t price100;
        uint16_t volume;
} __attribute__((packed)) PRICE_ENTRY_S;

//SHFE-SUB structure
typedef struct SHFE_SUB
{
        uint16_t changeno;                      ///2  <会话编号
        uint16_t ins_no;                        ///4  <合约编号
        uint32_t snap_time;                     ///8  <行情产生时间
        uint16_t snap_millisec;                 ///10 <行情产生时间
        char     ins_id[8];                     ///18 <合约名字
        uint32_t last_price100;                 ///22 <最新价 x 100
        uint32_t volume;                        ///26 <最新总成交量
        uint64_t turnover100;                   ///34 <成交金额
        uint32_t open_interest_u;               ///38 <持仓量
        PRICE_ENTRY_S  bid_list[5];             ///68
        PRICE_ENTRY_S  ask_list[5];             ///98
} __attribute__((packed)) SHFE_SUB_S;

//total gaps
uint32_t total_ipid_gap = 0;
uint32_t total_changeno_gap = 0;

int main(int argc, char *argv[])
{
    char *nic_name;
    uint8_t  nic_port = 0;
    int keep_running = 1;

    int size = 0;
    uint16_t prev_ip_id = 0;
    char packet_buf[2048];
    uint16_t changenos[4096];

    //reset changenos array
    memset(changenos, 0x00, sizeof(changenos));

    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s exanic[0-N]:[0,1]\n", argv[0]);
        return -1;
    }

    if(strchr(argv[1], ':') != NULL)
    {
	nic_name = strtok(argv[1], ":");
	nic_port = atoi(strtok(NULL, ":"));

        printf("nic_name:%s, nic_port:%d \n", nic_name, nic_port);
    }	
    else
    {
        fprintf(stderr, "Usage: %s exanic[0-N]:[0,1]\n", argv[0]);
        return -1;
    }
		
    /* acquire exanic device handle */
    exanic_t *nic = exanic_acquire_handle(nic_name);
    if (!nic)
    {
        fprintf(stderr, "exanic_acquire_handle: %s\n", exanic_get_last_error());
        return -1;
    }

    /* fpga upload data to port1, acquire rx buffer to receive data */
    exanic_rx_t *rx = exanic_acquire_rx_buffer(nic, nic_port, 0);
    if (!rx)
    {
        fprintf(stderr, "exanic_acquire_rx_buffer: %s\n", exanic_get_last_error());
        return -1;
    }

    while (keep_running)
    {
        size = exanic_receive_frame(rx, packet_buf, sizeof(packet_buf), 0);

	if(size > 64)
	{

        	uint16_t *ethtype = (uint16_t *)(packet_buf + ETHTYPE_OFFSET);
        	uint16_t *dot1q_ethtype = (uint16_t *)(packet_buf + DOT1Q_ETHTYPE_OFFSET);
		int dot1q_offset = 0;

        	if(*ethtype == IPV4_PACKET)
                	dot1q_offset = 0;
        	else if(*ethtype==DOT1Q_FRAME && *dot1q_ethtype==IPV4_PACKET)
                	dot1q_offset = 4;
        	else
			continue;

        	if(ntohl(*((uint32_t *)(packet_buf + dot1q_offset + DST_IP_OFFSET))) != DST_IP_FILTER)
			continue;
		
		uint16_t last_ip_id = ntohs(*((uint16_t *)(packet_buf + dot1q_offset + IP_ID_OFFSET)));
		if((uint16_t)(prev_ip_id+0x0001)!=last_ip_id && prev_ip_id!=0)
		{
			total_ipid_gap += last_ip_id - prev_ip_id -1;
			printf("<GAP_ipid> IP_ID gap previous/latest/total: %d/%d/%d \n", prev_ip_id, last_ip_id, total_ipid_gap);
		}
		prev_ip_id = last_ip_id;

		SHFE_SUB_S *deep = (SHFE_SUB_S *)(packet_buf + dot1q_offset + PAYLOAD_OFFSET);
		if((uint16_t)(changenos[deep->ins_no]+0x0001)!=deep->changeno && changenos[deep->ins_no]!=0)
		{
			total_changeno_gap += deep->changeno - changenos[deep->ins_no] -1;
			printf("<GAP_changeno> changeno gap previous/latest/total: %d/%d/%d \n", changenos[deep->ins_no], deep->changeno, total_changeno_gap);
		}
		changenos[deep->ins_no] = deep->changeno;

                printf("%u:%u, %u:%s, %u.%03u, %f, %u, %lu, %u, %d_%.2f|%d_%.2f|%d_%.2f|%d_%.2f|%d_%.2f x %.2f_%d|%.2f_%d|%.2f_%d|%.2f_%d|%.2f_%d\n",
                                last_ip_id, deep->changeno, deep->ins_no, deep->ins_id, deep->snap_time, deep->snap_millisec, 
				double(deep->last_price100)/100, deep->volume, deep->turnover100/100, deep->open_interest_u,
                                deep->bid_list[0].volume, double(deep->bid_list[0].price100)/100,
                                deep->bid_list[1].volume, double(deep->bid_list[1].price100)/100,
                                deep->bid_list[2].volume, double(deep->bid_list[2].price100)/100,
                                deep->bid_list[3].volume, double(deep->bid_list[3].price100)/100,
                                deep->bid_list[4].volume, double(deep->bid_list[0].price100)/100,
                                double(deep->ask_list[0].price100)/100, deep->ask_list[0].volume, 
                                double(deep->ask_list[1].price100)/100, deep->ask_list[1].volume, 
                                double(deep->ask_list[2].price100)/100, deep->ask_list[2].volume, 
                                double(deep->ask_list[3].price100)/100, deep->ask_list[3].volume, 
                                double(deep->ask_list[4].price100)/100, deep->ask_list[4].volume);
	}
    }

    return 0;
}
