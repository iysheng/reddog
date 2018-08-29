#ifndef _RED_DOG
#define _RED_DOG

#define IP_BUF_LENGTH   16
#define MAX_FILE_NAME_LEN   32
#define FILE_UDP_FRAME_LEN  512
#define FILE_BUF_SIZE   1024
#define MAX_NET_BUF     1500
#define MAX_CLIENT_NUM  32
#define RED_DOG_PORT    8190
#define RED_DOG_MAGIC_STR   "reddog"
#define SCAN_DEV_NAME_LEN   8

#define BUF_OPERATE_CODE_OFFSET    0
#define BUF_PTHREAD_INDEX_OFFSET   1
#define BUF_DATA_INDEX_OFFSET   2
#define BUF_DATA_INDEX_LEN  4
#define BUF_FILE_DATA_OFFSET    6
//#define BUF_FILE_DATA_INDEX_OFFSET  

#define BUF_FILE_INFO_OFFSET    1

enum operate_code {
    FILE_UPLOAD = 1,
    FILE_DOWNLOAD,
    FILE_INFO_PUSH,
    FILE_DATA,
    FILE_DATA_REQUEST,
    SERVERS_SCAN,
    SERVERS_SCAN_ACK,
};

/*
enum frame_code {
    FILE_INFO_PUSH,
    FILE_DOWNLOAD,
};
*/
enum operate_protocol {
    FILE_TRANS_TCP,
    FILE_TRANS_UDP,
};

typedef struct file_operate_flag {
    char op_code;
    char op_protocol;
    short frame_block_size;
} file_op_t;

typedef struct file_frame_data {
    char *frame_data;
    short int frame_len;
} file_frame_t;

typedef struct file_info_data {
    char file_name[MAX_FILE_NAME_LEN];
    size_t length;
    int checkcode;
} file_info_t;

typedef struct pthread_data {
    pthread_t pthread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int file_fd;
    file_info_t file_info;
} pthread_data_t;

#endif

