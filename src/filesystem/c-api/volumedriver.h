// Copyright (C) 2016 iNuron NV
//
// This file is part of Open vStorage Open Source Edition (OSE),
// as available from
//
//      http://www.openvstorage.org and
//      http://www.openvstorage.com.
//
// This file is free software; you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License v3 (GNU AGPLv3)
// as published by the Free Software Foundation, in version 3 as it comes in
// the LICENSE.txt file of the Open vStorage OSE distribution.
// Open vStorage is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY of any kind.

#ifndef __LIB_OVS_VOLUMEDRIVER_H_
#define __LIB_OVS_VOLUMEDRIVER_H_

#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct ovs_buffer ovs_buffer_t;
typedef struct ovs_context_t ovs_ctx_t;
typedef struct ovs_context_attr_t ovs_ctx_attr_t;
typedef struct ovs_snapshot_info ovs_snapshot_info_t;
typedef struct ovs_aio_request ovs_aio_request;
typedef struct ovs_completion ovs_completion_t;
typedef void (*ovs_callback_t)(ovs_completion_t *cb, void *arg);

struct ovs_aiocb
{
    void *aio_buf;
    off_t aio_offset;
    size_t aio_nbytes;
    ovs_aio_request *request_;
};

struct ovs_snapshot_info
{
    const char *name;
    uint64_t size;
};

/*
 * Create context attributes object
 * param attr: Context attributes object
 * return: This function always succeeds returning 0
 * return: Context attributes object on success, or NULL on fail
 */
ovs_ctx_attr_t*
ovs_ctx_attr_new();

/*
 * Destroy context attributes object
 * param attr: Context attributes object
 * return: This function always succeeds returning 0
 */
int
ovs_ctx_attr_destroy(ovs_ctx_attr_t *attr);

/*
 * Set transport type
 * param attr: Context attributes object
 * param transport: Transport string ("shm", "tcp", "rdma)
 * param host: Host string (FQDN or ASCII)
 * param port: TCP/RDMA port number
 * return: 0 on success, -1 on fail
 */
int
ovs_ctx_attr_set_transport(ovs_ctx_attr_t *attr,
                           const char *transport,
                           const char *host,
                           int port);

/*
 * Set network queue depth
 * param attr: Context attributes object
 * param qdepth: Queue depth
 * return: 0 on success, -1 on fail
 */
int
ovs_ctx_attr_set_network_qdepth(ovs_ctx_attr_t *attr,
                                const uint64_t qdepth);

/*
 * Create Open vStorage context
 * param attr: Context attributes object
 * return: Open vStorage context on success, or NULL on fail
 */
ovs_ctx_t*
ovs_ctx_new(const ovs_ctx_attr_t *attr);

/*
 * Initialize Open vStorage context
 * param ctx: Open vStorage context
 * param volume_name: Volume name
 * param oflag: Open flags
 * return: Open vStorage context on success, or NULL on fail
 */
int
ovs_ctx_init(ovs_ctx_t *ctx,
             const char *volume_name,
             int oflag);

/*
 * Destroy Open vStorage context
 * param: Open vStorage context
 * return: 0 on success, -1 on fail
 */
int
ovs_ctx_destroy(ovs_ctx_t *ctx);

/*
 * Create an Open vStorage volume
 * param ctx: Open vStorage context
 * param volume_name: Volume name
 * param size: Size of volume in bytes
 * return: 0 on success, -1 on fail
 */
int
ovs_create_volume(ovs_ctx_t *ctx,
                  const char *volume_name,
                  uint64_t size);

/*
 * Remove an Open vStorage volume
 * param ctx: Open vStorage context
 * param volume_name: Volume name
 * return: 0 on success, -1 on fail
 */
int
ovs_remove_volume(ovs_ctx_t *ctx,
                  const char *volume_name);

/*
 * Create a snapshot of an Open vStorage volume
 * param ctx: Open vStorage context
 * param volume_name: Volume name
 * param snap_name: Snapshot name
 * param timeout: Timeout value in seconds
 * return: 0 on success, -1 on fail
 */
int
ovs_snapshot_create(ovs_ctx_t *ctx,
                    const char *volume_name,
                    const char *snap_name,
                    const int64_t timeout);

/*
 * Rollback a Open vStorage volume to a specific snapshot
 * param ctx: Open vStorage context
 * param volume_name: Volume name
 * param snap_name: Snapshot name
 * return: 0 on success, -1 on fail
 */
int
ovs_snapshot_rollback(ovs_ctx_t *ctx,
                      const char *volume_name,
                      const char *snap_name);

/*
 * List snapshots of an Open vStorage volume
 * param ctx: Open vStorage context
 * param volume_name: Volume name
 * param snap_list: Snapshot info list
 * param max_snaps: The size of the snap_list
 * On error the number of snapshots is returned
 * and errno is set to ERANGE.
 * return: The number of snapshots on success, -1 on fail
 */
int
ovs_snapshot_list(ovs_ctx_t *ctx,
                  const char *volume_name,
                  ovs_snapshot_info_t *snap_list,
                  int *max_snaps);

/*
 * Free snapshot info list
 * param snap_list: Snapshot info list
 */
void
ovs_snapshot_list_free(ovs_snapshot_info_t *snap_list);

/*
 * Remove Open vStorage volume snapshot
 * param ctx: Open vStorage context
 * param volume_name: Volume name
 * param snap_name: Snapshot name
 * return: 0 on success, -1 on fail
 */
int
ovs_snapshot_remove(ovs_ctx_t *ctx,
                    const char *volume_name,
                    const char *snap_name);

/*
 * Check if snapshot is synced on the backend
 * param ctx: Open vStorage context
 * param volume_name: Volume name
 * param snap_name: Snapshot name
 * return: 1 if synced, 0 if not synced and -1 on fail
 */
int
ovs_snapshot_is_synced(ovs_ctx_t *ctx,
                       const char *volume_name,
                       const char *snap_name);

/*
 * List volumes
 * param ctx: Open vStorage context
 * param names: Volumes string
 * param size: The size of the volumes string
 * On error the expected string size is returned and errno
 * is set to ERANGE.
 * return: The expected string size on success, -1 on fail.
 */
int
ovs_list_volumes(ovs_ctx_t *ctx,
                 char *names, size_t *size);

/*
 * Allocate buffer from the shared memory segment
 * param ctx: Open vStorage context
 * param size: Buffer size in bytes
 * return: Buffer pointer on success, or NULL on fail
 */
ovs_buffer_t*
ovs_allocate(ovs_ctx_t *ctx,
             size_t size);

/* Retrieve pointer to buffer content
 * param ptr: Pointer to buffer structure
 * return: Buffer pointer on success, or NULL on fail
 */
void*
ovs_buffer_data(ovs_buffer_t *ptr);

/* Retrieve size of buffer
 * param ptr: Pointer to buffer structure
 * return: Size of buffer on success, -1 on fail
 */
size_t
ovs_buffer_size(ovs_buffer_t *ptr);

/*
 * Deallocate previously allocated buffer
 * param ctx: Open vStorage context
 * param shptr: Buffer pointer
 * return: 0 on success, -1 on fail
 */
int
ovs_deallocate(ovs_ctx_t *ctx,
               ovs_buffer_t *ptr);

/*
 * Read from a volume
 * param ctx: Open vStorage context
 * param buf: Shared memory buffer
 * param nbytes: Size to read in bytes
 * param offset: Offset to read in volume
 * return: Number of bytes actually read, -1 on fail
 */
ssize_t
ovs_read(ovs_ctx_t *ctx,
         void *buf,
         size_t nbytes,
         off_t offset);

/*
 * Write to a volume
 * param ctx: Open vStorage context
 * param buf: Shared memory buffer
 * param nbytes: Size to write in bytes
 * param offset: Offset to write in volume
 * return: Number of bytes actually written, -1 on fail
 */
ssize_t
ovs_write(ovs_ctx_t *ctx,
          const void *buf,
          size_t nbytes,
          off_t offset);

/*
 * Syncronize a volume's in-core state with that on disk
 * param ctx: Open vStorage context
 * return: 0 on success, -1 on fail
 */
int
ovs_flush(ovs_ctx_t *ctx);

/*
 * Get volume status
 * param ctx: Open vStorage context
 * param buf: Pointer to a stat structure
 * return: 0 on success, -1 on fail
 */
int
ovs_stat(ovs_ctx_t *ctx,
         struct stat *buf);

/*
 * Suspend until asynchronous I/O operation or timeout complete
 * param ctx: Open vStorage context
 * param ovs_aiocb: Pointer to an AIO Control Block structure
 * param timeout: Pointer to a timespec structure
 * return: 0 on success, -1 on fail
 */
int
ovs_aio_suspend(ovs_ctx_t *ctx,
                struct ovs_aiocb *ovs_aiocb,
                const struct timespec *timeout);

/*
 * Retrieve error status of asynchronous I/O operation
 * param ctx: Open vStorage context
 * param ovs_aiocb: Pointer to an AIO Control Block structure
 * return: 0 on success, -1 on fail
 */
int
ovs_aio_error(ovs_ctx_t *ctx,
              struct ovs_aiocb *ovs_aiocbp);

/*
 * Retrieve return status of asynchronous I/O operation
 * param ctx: Open vStorage context
 * param ovs_aiocb: Pointer to an AIO Control Block structure
 * return: Number of bytes returned based on the operation, -1 on fail
 */
ssize_t
ovs_aio_return(ovs_ctx_t *ctx,
               struct ovs_aiocb *ovs_aiocbp);

/*
 * Cancel an oustanding asynchronous I/O operation
 * param ctx: Open vStorage context
 * param ovs_aiocb: Pointer to an AIO Control Block structure
 * return: 0 on success, -1 on fail
 */
int
ovs_aio_cancel(ovs_ctx_t *ctx,
               struct ovs_aiocb *ovs_aiocbp);

/*
 * Finish an asynchronous I/O operation
 * param ctx: Open vStorage context
 * param ovs_aiocb: Pointer to an AIO Control Block structure
 * return: 0 on success, -1 on fail
 */
int
ovs_aio_finish(ovs_ctx_t *ctx,
               struct ovs_aiocb* ovs_aiocbp);

/*
 * Asynchronous read from a volume
 * param ctx: Open vStorage context
 * param ovs_aiocb: Pointer to an AIO Control Block structure
 * return: 0 on success, -1 on fail
 */
int
ovs_aio_read(ovs_ctx_t *ctx,
             struct ovs_aiocb *ovs_aiocbp);

/*
 * Asynchronous write to a volume
 * param ctx: Open vStorage context
 * param ovs_aiocb: Pointer to an AIO Control Block structure
 * return: 0 on success, -1 on fail
 */
int
ovs_aio_write(ovs_ctx_t *ctx,
              struct ovs_aiocb *ovs_aiocbp);

/*
 * Asynchronous read from a volume with completion
 * param ctx: Open vStorage context
 * param ovs_aiocb: Pointer to an AIO Control Block structure
 * param completion: Pointer to a completion structure
 * return: 0 on success, -1 on fail
 */
int
ovs_aio_readcb(ovs_ctx_t *ctx,
               struct ovs_aiocb *ovs_aiocbp,
               ovs_completion_t *completion);

/*
 * Asynchronous write to a volume with completion
 * param ctx: Open vStorage context
 * param ovs_aiocb: Pointer to an AIO Control Block structure
 * param completion: Pointer to a completion structure
 * return: 0 on success, -1 on fail
 */
int
ovs_aio_writecb(ovs_ctx_t *ctx,
                struct ovs_aiocb *ovs_aiocbp,
                ovs_completion_t *completion);

/*
 * Asynchronously syncronize a volume's in-core state with that on disk with
 * completion
 * param ctx: Open vStorage context
 * param completion: Pointer to a completion structure
 * return: 0 on success, -1 on fail
 */
int
ovs_aio_flushcb(ovs_ctx_t *ctx,
                ovs_completion_t *completion);

/*
 * Create a new completion
 * param complete_cb: Pointer to an ovs_callback_t structure
 * param arg: Pointer to an argument passed to complete_cb
 * return: Completion pointer on success, or NULL on fail
 */
ovs_completion_t*
ovs_aio_create_completion(ovs_callback_t complete_cb,
                          void *arg);

/*
 * Retrieve return status of a completion
 * param completion: Pointer to a completion structure
 * return: Number of bytes returned based on the operation and the completion,
 * -1 on fail
 */
ssize_t
ovs_aio_return_completion(ovs_completion_t *completion);

/*
 * Suspend until completion or timeout complete
 * param completion: Pointer to completion structure
 * param timeout: Pointer to a timespec structure
 * return: 0 on success, -1 on fail
 */
int
ovs_aio_wait_completion(ovs_completion_t *completion,
                        const struct timespec *timeout);

/*
 * Signal a suspended completion
 * param completion: Pointer to completion structure
 * return: 0 on success, -1 on fail
 */
int
ovs_aio_signal_completion(ovs_completion_t *completion);

/*
 * Release completion
 * param completion: Pointer to completion structure
 * return: 0 on success, -1 on fail
 */
int
ovs_aio_release_completion(ovs_completion_t *completion);

#ifdef __cplusplus
} //extern "C" endif
#endif

#endif // __LIB_OVS_VOLUMEDRIVER_H_
