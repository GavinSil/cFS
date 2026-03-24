/**
 * @file uds_client.c
 * @brief ESA Stepping Terminal UDS 客户端层实现
 */

#include "uds_client.h"
#include "protocol.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/**
 * @brief 可靠读取指定字节数（处理部分读取和 EINTR）
 * @param[in] fd 文件描述符
 * @param[out] buf 缓冲区指针
 * @param[in] n 期望读取的字节数
 * @return 成功返回实际读取的字节数，失败返回 -1
 */
static ssize_t read_exact(int fd, void *buf, size_t n)
{
    size_t total = 0;
    char *p = buf;

    while (total < n)
    {
        ssize_t r = read(fd, p + total, n - total);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            return (ssize_t)total;
        total += (size_t)r;
    }

    return (ssize_t)total;
}

/**
 * @brief 可靠写入指定字节数（处理部分写入和 EINTR）
 * @param[in] fd 文件描述符
 * @param[in] buf 缓冲区指针
 * @param[in] n 期望写入的字节数
 * @return 成功返回实际写入的字节数，失败返回 -1
 */
static ssize_t write_exact(int fd, const void *buf, size_t n)
{
    size_t total = 0;
    const char *p = buf;

    while (total < n)
    {
        ssize_t w = write(fd, p + total, n - total);
        if (w < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (w == 0)
            return -1;
        total += (size_t)w;
    }

    return (ssize_t)total;
}

int est_uds_connect(const char *socket_path)
{
    int sfd;
    struct sockaddr_un addr;

    sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd == -1)
    {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(sfd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        perror("connect");
        close(sfd);
        return -1;
    }

    return sfd;
}

int est_uds_transact(const char *socket_path,
                     const void *req, size_t req_size,
                     void *resp, size_t resp_size)
{
    int fd = est_uds_connect(socket_path);
    if (fd < 0)
    {
        return -1;
    }

    if (write_exact(fd, req, req_size) < 0)
    {
        fprintf(stderr, "写入请求失败\n");
        est_uds_close(fd);
        return -1;
    }

    if (read_exact(fd, resp, resp_size) < (ssize_t)resp_size)
    {
        fprintf(stderr, "读取响应失败\n");
        est_uds_close(fd);
        return -1;
    }

    est_uds_close(fd);
    return 0;
}

void est_uds_close(int fd)
{
    close(fd);
}

const char *est_status_to_string(int32_t status)
{
    switch (status)
    {
        case EST_STATUS_SUCCESS:
            return "成功";
        case EST_STATUS_FAILURE:
            return "失败";
        case EST_STATUS_DUPLICATE_BEGIN:
            return "重复开始";
        case EST_STATUS_NOT_READY:
            return "未就绪";
        case EST_STATUS_TIMEOUT:
            return "超时";
        case EST_STATUS_ILLEGAL_COMPLETE:
            return "非法完成";
        case EST_STATUS_TRANSPORT_ERROR:
            return "传输错误";
        case EST_STATUS_PROTOCOL_ERROR:
            return "协议错误";
        case EST_STATUS_ILLEGAL_STATE:
            return "非法状态";
        default:
            return "未知状态码";
    }
}

const char *est_state_to_string(uint32_t state)
{
    switch (state)
    {
        case EST_STATE_INIT:
            return "初始化";
        case EST_STATE_READY:
            return "就绪";
        case EST_STATE_RUNNING:
            return "运行中";
        case EST_STATE_WAITING:
            return "等待中";
        case EST_STATE_COMPLETE:
            return "已完成";
        default:
            return "未知状态";
    }
}
