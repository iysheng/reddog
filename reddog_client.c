#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <poll.h>
#include <net/if.h>

#include "reddog.h"

int g_fd = 0;
int g_sockfd;
file_op_t g_file_opt;
file_frame_t g_file_frame;
file_info_t g_file_info;
struct sockaddr_in g_file_sock;
pthread_t g_recv_thread;
char g_scan_dev_name[SCAN_DEV_NAME_LEN];
//char * g_buf;

static int do_with_buf(int socked_fd, char *buf, int buf_len) {
    long int data_index = 0, data_index_tmp = 0;
    printf("YYS %s [%d] buf_len=%d  %d %s\n", __func__, __LINE__, buf_len, buf[0], &buf[1]);
    switch (buf[BUF_OPERATE_CODE_OFFSET]) {
    case FILE_DATA_REQUEST:
        if (g_fd == 0)
            g_fd = open(g_file_info.file_name, O_RDONLY);
        if (g_fd > 0) {
            g_file_frame.frame_data = (char *)calloc(0, sizeof(char) * FILE_UDP_FRAME_LEN);
            if (NULL == g_file_frame.frame_data) {
                close(g_fd);
                return -ENOMEM;
            }
            *(g_file_frame.frame_data) = FILE_DATA;
            memcpy(&data_index_tmp, g_file_frame.frame_data + BUF_DATA_INDEX_OFFSET,\
                sizeof(data_index_tmp));
            data_index = ntohl(data_index_tmp);
            if (g_file_info.length > data_index*(FILE_UDP_FRAME_LEN - BUF_FILE_DATA_OFFSET)) {
                g_file_frame.frame_len = FILE_UDP_FRAME_LEN - BUF_FILE_DATA_OFFSET;
            } else if (g_file_info.length > (data_index-1)*(FILE_UDP_FRAME_LEN - BUF_FILE_DATA_OFFSET)) {
                g_file_frame.frame_len = g_file_info.length - (data_index-1)*(FILE_UDP_FRAME_LEN - BUF_FILE_DATA_OFFSET);
            } else {
                close(g_fd);
                return 0;
            }
            pread(g_fd, g_file_frame.frame_data + BUF_FILE_DATA_OFFSET, g_file_frame.frame_len,\
                (data_index-1)*(FILE_UDP_FRAME_LEN - BUF_FILE_DATA_OFFSET));
            data_index++;
            data_index_tmp = htonl(data_index);
            memcpy(g_file_frame.frame_data + BUF_DATA_INDEX_OFFSET, &data_index_tmp,\
                BUF_DATA_INDEX_LEN * sizeof(char));
        } else {
            printf("failed to open file:%s\n", g_file_info.file_name);
            return errno;
        } 
        break;
    default:
        return -EINVAL;
        break;
    }
}

void *do_recv_from_server(void *argv)
{
#define POLL_READFD_NUM 1
    ssize_t ret = 0;
    struct sockaddr_in sockadd_src;
    socklen_t sockadd_src_len;
    
    char *buf = (char *)malloc(sizeof(char) * 10);
    printf("YYS %s [%d]\n", __func__, __LINE__);
    if (buf == NULL) {
        ret = -ENOMEM;
        goto rec_failed;
    }
    
    for (;;) {
        printf("YYS %s [%d]\n", __func__, __LINE__);
        if ((ret = recvfrom(*(int *)argv ,buf, MAX_NET_BUF, 0, (struct sockaddr *)(&sockadd_src),
            &sockadd_src_len)) > 0) {
            do_with_buf(*(int *)argv, buf, ret);
        } else {
            ret = (ssize_t)errno;
        }
    }

rec_failed:
    if (buf)
        free(buf);
    return (void *)ret;
}

int do_file_ack(int sockfd, struct sockaddr_in *dest_addr, file_frame_t * file_fram_ptr)
{
    int ret = 0;
    ret = sendto(sockfd, file_fram_ptr->frame_data, file_fram_ptr->frame_len, 0,\
        (const struct sockaddr *)dest_addr, sizeof(struct sockaddr_in));
    if (ret == -1) {
        ret = errno;
        close(sockfd);
        goto failed;
    }
    free(file_fram_ptr->frame_data);
    file_fram_ptr->frame_data = NULL;
    printf("YYS %s [%d] ret=%d fd=%d\n", __func__, __LINE__, ret, sockfd);
    do_recv_from_server(&sockfd);
//    ret = pthread_create(&g_recv_thread, NULL, do_recv_from_server, (void *)(&sockfd));
//    ret = pthread_create(&g_recv_thread, NULL, do_recv_from_server, NULL);
    printf("YYS %s [%d] ret=%d fd=%d\n", __func__, __LINE__, ret, sockfd);
    if (ret) 
        printf("failed to create pthread\n");
    while(1);

failed:
    printf("YYS %s [%d] ret=%d\n", __func__, __LINE__, ret);
    return ret;
}

static int do_op_name(file_op_t *file_op_ptr, file_info_t *file_info_ptr)
{
    int ret = -EINVAL;
    struct stat file_stat;
    struct ifreq netdev_ifreq;
    
    if ((file_op_ptr == NULL) || (file_info_ptr == NULL)) 
        return ret;
    printf("YYS %s [%d]\n", __func__, __LINE__);
    /* 填充数据帧 结构体g_file_frame */
    bzero(&file_stat, sizeof(file_stat));
    if ((file_op_ptr->op_code == FILE_UPLOAD) && \
        ((ret = stat(file_info_ptr->file_name , &file_stat)) == 0)) {
        printf("YYS %s [%d]\n", __func__, __LINE__);
        g_fd = open(file_info_ptr->file_name, O_RDONLY);
        if (g_fd > 0) {
            g_file_frame.frame_data = (char *)calloc(0, sizeof(char) * FILE_BUF_SIZE);
            if (NULL == g_file_frame.frame_data) {
                close(g_fd);
                return -ENOMEM;
            }
            *(g_file_frame.frame_data) = FILE_INFO_PUSH;
            g_file_info.length = file_stat.st_size;
            g_file_info.checkcode = 0x12345678; /* for test later */
            memcpy(g_file_frame.frame_data + 1, &g_file_info, sizeof(g_file_info));
            close(g_fd);
            g_fd = 0;
            g_file_frame.frame_len = (short int)(sizeof(g_file_info) + sizeof(char));
            printf("YYS %s [%d]\n", __func__, __LINE__);
        } else {
            printf("failed to open file:%s\n", file_info_ptr->file_name);
            return errno;
        }
    } else if (file_op_ptr->op_code == FILE_DOWNLOAD) {
        g_file_frame.frame_data = (char *)calloc(0, strlen(file_info_ptr->file_name)+2);
        if (NULL == g_file_frame.frame_data) {
            return -ENOMEM;
        }
        *(g_file_frame.frame_data) = FILE_DOWNLOAD;
        memcpy(g_file_frame.frame_data+1, file_info_ptr->file_name, strlen(file_info_ptr->file_name));
        g_file_frame.frame_len = (short int)(strlen(file_info_ptr->file_name) + sizeof(char));
    } else if (file_op_ptr->op_code == SERVERS_SCAN) {
        g_file_frame.frame_data = (char *)calloc(0, sizeof(char) * (strlen(RED_DOG_MAGIC_STR) + 1));
        if (NULL == g_file_frame.frame_data) {
            return -ENOMEM;
        }
        *(g_file_frame.frame_data) = SERVERS_SCAN;
        memcpy(g_file_frame.frame_data + 1, RED_DOG_MAGIC_STR, strlen(RED_DOG_MAGIC_STR));
        g_file_frame.frame_len = (short int)(sizeof(char) * (strlen(RED_DOG_MAGIC_STR) + 1));
        strncpy(netdev_ifreq.ifr_name, g_scan_dev_name, SCAN_DEV_NAME_LEN);
        if (-1 == ioctl(g_sockfd, SIOCGIFBRDADDR, (char *)&netdev_ifreq)) {
            return errno;
        }
        g_file_sock.sin_addr.s_addr = 
            ((struct sockaddr_in*)(&netdev_ifreq.ifr_broadaddr))->sin_addr.s_addr;
        /* 使能广播 */
        int broadcastEnable = 1;
        ret = setsockopt(g_sockfd, SOL_SOCKET, SO_BROADCAST, (const void *)&broadcastEnable, (socklen_t)sizeof(broadcastEnable));
        if (ret == -1) {
            printf("YYS %s [%d] failed to open boardcast mode\n", __func__, __LINE__);
            return errno;
        }
        printf("YYS %s [%d] %s\n", __func__, __LINE__, inet_ntoa(g_file_sock.sin_addr));
    } else {
        return ret;
    }
    
    ret = do_file_ack(g_sockfd, &g_file_sock, &g_file_frame);
    return ret;
}

static void usage(void)
{
    printf(
"reddog_client [options]\n"
"            -i|--ip                   server ip addr\n"
"            -f|--file        filename\n"
"            -o|--operate         [down][up]\n"
"            -p|--protocol         [udp][tcp]\n"
"            -s|--scanmode         scan valid server in local net with netdevice like:eth0...\n"
    );
}

int main(int argc, char *argv[])
{
    int ret = 0;
    int opt = 0;
    char dst_ip[IP_BUF_LENGTH] = {0};
    char file_name[MAX_FILE_NAME_LEN] = {0};

    while (-1 != (opt = getopt(argc, argv, "i:f:o:p:s:"))) {
        switch (opt) {
        case 'i':
            snprintf(dst_ip, IP_BUF_LENGTH, "%s", optarg);
            if (1 != (ret = inet_aton(dst_ip, &(g_file_sock.sin_addr)))) {
                printf("invalid server ip check your input\n");
                goto failed;
            }
            g_file_sock.sin_port = htons(RED_DOG_PORT);
            g_file_sock.sin_family = AF_INET;
            break;
        case 'f':
            snprintf(g_file_info.file_name , MAX_FILE_NAME_LEN, "%s", optarg);
            break;
        case 'o':
            if (strncmp("up", optarg, strlen("up")) == 0)
                g_file_opt.op_code = FILE_UPLOAD;
            else if (strncmp("down", optarg, strlen("down")) == 0)
                g_file_opt.op_code = FILE_DOWNLOAD;
            break;
        case 'p':
            if (strncmp("tcp", optarg, strlen("tcp")) == 0)
                g_file_opt.op_protocol = FILE_TRANS_TCP;
            else if (strncmp("udp", optarg, strlen("udp")) == 0) {
                g_file_opt.op_protocol = FILE_TRANS_UDP;
                g_file_opt.frame_block_size = FILE_UDP_FRAME_LEN;
            }
            break;
        case 's':
            g_file_opt.op_code = SERVERS_SCAN;
            memcpy(g_scan_dev_name, optarg, strlen(optarg)>SCAN_DEV_NAME_LEN ? 0 : strlen(optarg) );
            /* 广播模式，预先初始化端口和传输层协议 */
            g_file_sock.sin_port = htons(RED_DOG_PORT);
            g_file_sock.sin_family = AF_INET;
            break;
        default:
            usage();
            exit(1);
        }
    }
    
    /* 建立socket句柄 */
    if (g_file_opt.op_code == SERVERS_SCAN || g_file_opt.op_protocol == FILE_TRANS_UDP) {
        g_file_opt.op_protocol = FILE_TRANS_UDP;
        g_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    } else if (g_file_opt.op_protocol == FILE_TRANS_TCP) {
        g_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    }
    if (g_sockfd == -1) {
        ret = errno;
        //printf("failed to create sockfd err:%d\n", errno);
        goto failed;
    } else if (g_file_opt.op_protocol == FILE_TRANS_TCP) {
        ret = connect(g_sockfd, (const struct sockaddr *)&g_file_sock, sizeof(struct sockaddr));
        if (-1 == ret) {
            ret = errno;
            goto failed;
        }
    }
    
    printf("YYS %s [%d]\n", __func__, __LINE__);
    ret = do_op_name(&g_file_opt, &g_file_info);
    if (ret != 0)
        goto failed;
    
failed:
    if (g_file_frame.frame_data)
        free(g_file_frame.frame_data);
    printf("ret=%d\n", ret);
    return ret;
}

