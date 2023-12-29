#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/if_packet.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <sys/time.h>
#include <string.h>
#include <time.h>
	
#define ETHTYPE_OFFSET                  12
#define DOT1Q_ETHTYPE_OFFSET            16
#define IP_ID_OFFSET  			18
#define DST_IP_OFFSET 			30
#define PAYLOAD_OFFSET			42
#define IPV4_PACKET                     0x0008
#define DOT1Q_FRAME                     0x0081
#define DST_IP_FILTER 			0xefefef09	//239.239.239.9 is DST IP we want to read

//Define price entry
typedef struct PRICE_ENTRY {
        uint32_t price100;			//价格 x 100
        uint16_t volume;
} __attribute__((packed)) PRICE_ENTRY_S;

//SHFE-SUB structure
typedef struct SHFE_SUB
{
        uint32_t changeno;                      ///4  <会话编号
        uint32_t snap_time;                     ///8  <行情产生时间
        uint16_t snap_millisec;                 ///10 <行情产生时间
        uint16_t ins_no;                        ///12  <合约编号
        char     ins_id[8];                     ///20 <合约名字
        uint32_t last_price100;                 ///24 <最新价 x 100
        uint32_t volume;                        ///28 <最新总成交量
        uint64_t turnover100;                   ///36 <成交金额 x 100
        uint32_t open_interest_u;               ///40 <持仓量
        PRICE_ENTRY_S  bid_list[5];             ///70
        PRICE_ENTRY_S  ask_list[5];             ///100
} __attribute__((packed)) SHFE_SUB_S;

//total gaps
uint32_t total_ipid_gap = 0;
uint32_t total_changeno_gap = 0;

long long get_timestamp(void)//获取时间戳函数
{
    long long tmp;
    struct timeval tv;

    gettimeofday(&tv, NULL);
    tmp = tv.tv_sec;
    tmp = tmp * 1000;
    tmp = tmp + (tv.tv_usec / 1000);

    return tmp;
}

void get_format_time_string(char *str_time) //获取格式化时间
{
    time_t now;
    struct tm *tm_now;
    char datetime[128];
 
    time(&now);
    tm_now = localtime(&now);
    strftime(datetime, 128, "%Y-%m-%d %H:%M:%S", tm_now);
 
    printf("now datetime : %s\n", datetime);
	strcpy(str_time, datetime);
}

void get_format_time_ms(char *str_time) { 
    struct tm *tm_t;
    struct timeval time;
    
    gettimeofday(&time,NULL);
    tm_t = localtime(&time.tv_sec);
    if(NULL != tm_t) {
        sprintf(str_time,"%02d:%02d:%02d.%03ld",
            tm_t->tm_hour, 
            tm_t->tm_min, 
            tm_t->tm_sec,
            time.tv_usec/1000);
    }
    
    return;
}

int main(int argc, char** argv)
{
    int sockfd;
    char buf[10240];
    ssize_t n;
    int size = 0;
    uint16_t prev_ip_id = 0;
    uint32_t changenos[4096];
 
    struct ifreq req;    //网络接口地址
    struct sockaddr_ll addr;  

    //Clear changenos memory space
    memset(changenos, 0x00, sizeof(changenos)); 

    if (argc != 2) {
	printf("Usage: ./dce-sub-raw-socket ens1f[0:1]\n");
	return 1;
    } 

    if ((sockfd = socket(PF_PACKET,  SOCK_RAW, htons(ETH_P_ALL)))== -1)
    {    
        printf("socket error!\n");
        return 1;
    }
    strncpy(req.ifr_name, argv[1], IFNAMSIZ);            //指定网卡名称
    if(-1 == ioctl(sockfd, SIOCGIFFLAGS, &req))    //获取网络接口
    {
        perror("ioctl");
        //close(sockfd);
        exit(-1);
    }
     
    req.ifr_flags |= IFF_PROMISC;
    if(-1 == ioctl(sockfd, SIOCGIFFLAGS, &req))    //网卡设置混杂模式
    {
        perror("ioctl");
        //close(sockfd);
        exit(-1);
    }
    
    // 绑定套接字到指定网卡  
    memset(&addr, 0, sizeof(addr));  
    addr.sll_family = PF_PACKET;  
    addr.sll_protocol = htons(ETH_P_ALL);  
    addr.sll_ifindex = if_nametoindex(req.ifr_name);  
    if (addr.sll_ifindex == 0) {  
        fprintf(stderr, "Unknown interface: %s\n", req.ifr_name);  
        exit(EXIT_FAILURE);  
    }  
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {  
        perror("bind");  
        exit(EXIT_FAILURE);  
    }  

    while (1)
    {
        n = recv(sockfd, buf, sizeof(buf), 0);
        if (n == -1)
        {
            printf("recv error!\n");
            break;
        }
        else if (n==0)
            continue;

        //接收数据不包括数据链路帧头
	//printf("recv %ld\n", n);

        size = n;
	char* packet_buf = (char*)(buf);

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
                if((uint32_t)(changenos[deep->ins_no]+0x0001)!=deep->changeno && changenos[deep->ins_no]!=0)
                {
                        total_changeno_gap += deep->changeno - changenos[deep->ins_no] -1;
                        printf("<GAP_changeno> changeno gap previous/latest/total: %d/%d/%d \n", changenos[deep->ins_no], deep->changeno, total_changeno_gap);
                }
                changenos[deep->ins_no] = deep->changeno;

                printf("%u:%u, %u:%s, %u.%03u, %.2f, %u, %.2f, %u, %d_%.2f|%d_%.2f|%d_%.2f|%d_%.2f|%d_%.2f x %.2f_%d|%.2f_%d|%.2f_%d|%.2f_%d|%.2f_%d\n",
                                last_ip_id, deep->changeno, deep->ins_no, deep->ins_id, deep->snap_time, deep->snap_millisec,
                                (double)deep->last_price100/100, deep->volume, (double)deep->turnover100/100, deep->open_interest_u,
                                deep->bid_list[4].volume, (double)deep->bid_list[4].price100/100,
                                deep->bid_list[3].volume, (double)deep->bid_list[3].price100/100,
                                deep->bid_list[2].volume, (double)deep->bid_list[2].price100/100,
                                deep->bid_list[1].volume, (double)deep->bid_list[1].price100/100,
                                deep->bid_list[0].volume, (double)deep->bid_list[0].price100/100,
                                (double)deep->ask_list[0].price100/100, deep->ask_list[0].volume,
                                (double)deep->ask_list[1].price100/100, deep->ask_list[1].volume,
                                (double)deep->ask_list[2].price100/100, deep->ask_list[2].volume,
                                (double)deep->ask_list[3].price100/100, deep->ask_list[3].volume,
                                (double)deep->ask_list[4].price100/100, deep->ask_list[4].volume);

	}

    }
    //close(sockfd);
    return 0;
}

