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

#include "reddog.h"

int g_fd;
static int g_sockfd;
file_op_t g_file_opt;
file_frame_t g_file_frame;
file_info_t g_file_info;
struct sockaddr_in g_file_sock;
char * g_buf;
int g_buf_len;

//pthread_t g_recv_thread[MAX_CLIENT_NUM];
pthread_data_t g_recv_client_thread[MAX_CLIENT_NUM];
static char g_pthread_index;


static void *do_server_pthreads(void *argv)
{
    int ret = 0;
    char * buf_ptr = (char *)argv;
    int index = buf_ptr[BUF_PTHREAD_INDEX_OFFSET];
    int data_index = 0, data_length = 0, data_index_tmp = 0;
    pthread_mutex_init(&(g_recv_client_thread[index].mutex), NULL);
    pthread_cond_init(&(g_recv_client_thread[index].cond), NULL);
    for (;;) {
        pthread_mutex_lock(&(g_recv_client_thread[index].mutex));
        pthread_cond_wait(&(g_recv_client_thread[index].cond), &(g_recv_client_thread[index].mutex));
//        if (buf_ptr[BUF_OPERATE_CODE_OFFSET] == FILE_DATA)
        memcpy(&data_index_tmp, &buf_ptr[BUF_DATA_INDEX_OFFSET], sizeof(data_index_tmp));
        data_index = ntohl(data_index_tmp);
        if (g_recv_client_thread[index].file_info.length > (FILE_UDP_FRAME_LEN - BUF_FILE_DATA_OFFSET)\
            *(unsigned long)(data_index)) {
            ret = pwrite(g_recv_client_thread[index].file_fd, &buf_ptr[BUF_FILE_DATA_OFFSET], 
                FILE_UDP_FRAME_LEN - BUF_FILE_DATA_OFFSET, (data_index-1)*(FILE_UDP_FRAME_LEN - BUF_FILE_DATA_OFFSET));
        }
        else {
            ret = pwrite(g_recv_client_thread[index].file_fd, &buf_ptr[BUF_FILE_DATA_OFFSET], 
                ((g_recv_client_thread[index].file_info.length - FILE_UDP_FRAME_LEN - BUF_FILE_DATA_OFFSET)\
            *(unsigned long)(data_index-1)), (data_index-1)*(FILE_UDP_FRAME_LEN - BUF_FILE_DATA_OFFSET));
            close(g_recv_client_thread[index].file_fd);
        }
        buf_ptr[BUF_OPERATE_CODE_OFFSET] == FILE_DATA_REQUEST;
        data_index++;
        data_index_tmp = htonl(data_index);
        memcpy(&buf_ptr[BUF_DATA_INDEX_OFFSET], &data_index_tmp, sizeof(data_index));
        g_buf_len = BUF_DATA_INDEX_OFFSET + sizeof(data_index);
        pthread_mutex_unlock(&(g_recv_client_thread[index].mutex));
    }
    
    return (void *)ret;
}

static int do_with_buf(int socked_fd, char *buf, int buf_len) {
    int ret = 0;
    file_info_t file_info;
    char pthread_index = 0;
    struct stat file_stat;
    long int data_index_begin = 1;
    printf("YYS %s [%d] buf_len=%d\n", __func__, __LINE__, buf_len);
    switch (buf[0]) {
    case SERVERS_SCAN:
        buf[0] = SERVERS_SCAN_ACK;
        g_buf_len = 1;
        break;
    case FILE_INFO_PUSH:
        memcpy(&(g_recv_client_thread[g_pthread_index].file_info), &buf[BUF_FILE_INFO_OFFSET], 
            sizeof(g_recv_client_thread[g_pthread_index].file_info));
        /////////////端点续传
        printf("YYS %s [%d] name:%s len=%d crc=%#x\n", __func__, __LINE__, file_info.file_name,
            ntohl(g_recv_client_thread[g_pthread_index].file_info.length), ntohl(g_recv_client_thread[g_pthread_index].file_info.checkcode));
        if ((ret = stat(g_recv_client_thread[g_pthread_index].file_info.file_name , &file_stat)) == 0) {
            /////////////端点续传
        } else {
            g_recv_client_thread[g_pthread_index].file_fd = open(g_recv_client_thread[g_pthread_index].file_info.file_name,
                O_CREAT | O_RDWR);
            if (g_recv_client_thread[g_pthread_index].file_fd == -1)
                return errno;
        }
        /////////////
        buf[BUF_OPERATE_CODE_OFFSET] = FILE_DATA_REQUEST;
        buf[BUF_PTHREAD_INDEX_OFFSET] = g_pthread_index;
        data_index_begin = htonl(1);
        memcpy(&buf[BUF_DATA_INDEX_OFFSET], &data_index_begin, sizeof(data_index_begin));
        ret = pthread_create(&(g_recv_client_thread[g_pthread_index].pthread), NULL, do_server_pthreads, (void *)buf);
        if (ret) {
            printf("failed to create pthread\n");
            return ret;
        }
        g_buf_len = BUF_PTHREAD_INDEX_OFFSET + sizeof(g_pthread_index);
        printf("YYS %s [%d] g_buf_len=%d\n", __func__, __LINE__, g_buf_len);
        g_pthread_index++;
        break;
    case FILE_DATA:
        pthread_index = buf[BUF_PTHREAD_INDEX_OFFSET];
        if (pthread_index > MAX_CLIENT_NUM)
            return -EINVAL;
        pthread_cond_signal(&g_recv_client_thread[pthread_index].cond);
        break;
        //ret = pthread_create(&g_recv_client_thread[pthread_index++], NULL, do_recv_from_client, (void *)&g_sockfd);
        //if (ret) 
        //printf("failed to create pthread\n");
    default:
        break;
    }
    
    return ret;
}

static void *do_recv_from_client(void *argv)
{
#define POLL_READFD_NUM 1
#define POLL_TIME_OUT   -1
    ssize_t ret = 0;
    struct pollfd read_fd[POLL_READFD_NUM];
    struct sockaddr_in sockadd_src;
    socklen_t sockadd_src_len;

    read_fd[0].fd = *(int *)argv;
    read_fd[0].events = POLLIN;
    
    for (;;) {
        ret = poll(read_fd, POLL_READFD_NUM, POLL_TIME_OUT);
        if (read_fd[0].revents & POLLIN) {
            printf("YYS %s [%d]\n", __func__, __LINE__);
            ///对g_buf加锁访问
            if ((ret = recvfrom(*(int *)argv ,g_buf, MAX_NET_BUF, 0, (struct sockaddr *)(&sockadd_src),
                &sockadd_src_len)) > 0) {
                ret = do_with_buf(*(int *)argv, g_buf, ret);
                if (ret == 0) {
                    ret = sendto(*(int *)argv, g_buf, g_buf_len, 0, (struct sockaddr *)(&sockadd_src),
                    sockadd_src_len);
                    printf("YYS %s [%d] ret=%d errno=%d\n", __func__, __LINE__, ret, errno);
                }
            } else {
                ret = (ssize_t)errno;
            }
            ////释放g_buf的锁
        }
    }

rec_failed:
    return (void *)ret;
}


int main(int argc, char *argv[])
{
    int ret = 0;
    char buf[512];
    pthread_t recv_thread;
    g_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    
    if (g_sockfd == -1) {
        printf("failed to create socket errno=%d\n", errno);
        ret = errno;
        goto fail;
    }

    g_file_sock.sin_addr.s_addr = htonl(INADDR_ANY);
    g_file_sock.sin_family = AF_INET;
    g_file_sock.sin_port = htons(RED_DOG_PORT);
    ret = bind(g_sockfd, (const struct sockaddr *)(&g_file_sock), sizeof(struct sockaddr));
    if (ret == -1) {
        printf("failed to bind socket errno=%d\n", errno);
        ret = errno;
        goto fail;
    }
    g_buf = (char *)calloc(0, sizeof(char) * MAX_NET_BUF);
    if (g_buf == NULL) {
        ret = -ENOMEM;
        goto fail;
    }
    ret = pthread_create(&recv_thread, NULL, do_recv_from_client, (void *)(&g_sockfd));
    if (ret) 
        printf("failed to create pthread\n");

    while (1) {
        /*
        if ((ret = recv(g_sockfd ,buf, MAX_NET_BUF, 0)) > 0) {
                printf("YYS %s [%d] ret=%d\n", __func__, __LINE__, ret);
            } else {
                ret = (ssize_t)errno;
            }
            */
    }
    
    if (g_buf)
        free(g_buf);
fail:
    return ret;
}

