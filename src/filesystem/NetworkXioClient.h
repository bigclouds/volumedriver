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

#ifndef __NETWORK_XIO_CLIENT_H_
#define __NETWORK_XIO_CLIENT_H_

#include <libxio.h>

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <boost/thread/lock_guard.hpp>
#include <youtils/SpinLock.h>

#include <youtils/Logger.h>

#include "NetworkXioProtocol.h"

namespace volumedriverfs
{

MAKE_EXCEPTION(XioClientCreateException, fungi::IOException);
MAKE_EXCEPTION(XioClientRegHandlerException, fungi::IOException);
MAKE_EXCEPTION(XioClientQueueIsBusyException, fungi::IOException);

extern void ovs_xio_aio_complete_request(void *request,
                                         ssize_t retval,
                                         int errval);

extern void ovs_xio_complete_request_control(void *request,
                                             ssize_t retval,
                                             int errval);

class NetworkXioClient
{
public:
    NetworkXioClient(const std::string& uri, const uint64_t qd);

    ~NetworkXioClient();

    struct session_data
    {
        xio_context *ctx;
        bool disconnected;
        bool disconnecting;
    };

    struct xio_msg_s
    {
        xio_msg xreq;
        const void *opaque;
        NetworkXioMsg msg;
        std::string s_msg;
    };

    struct xio_ctl_s
    {
        xio_msg_s xmsg;
        session_data sdata;
        std::vector<std::string> *vec;
        uint64_t size;
    };

    void
    xio_send_open_request(const std::string& volname,
                          const void *opaque);

    void
    xio_send_close_request(const void *opaque);

    void
    xio_send_read_request(void *buf,
                          const uint64_t size_in_bytes,
                          const uint64_t offset_in_bytes,
                          const void *opaque);

    void
    xio_send_write_request(const void *buf,
                           const uint64_t size_in_bytes,
                           const uint64_t offset_in_bytes,
                           const void *opaque);

    void
    xio_send_flush_request(const void *opaque);

    int
    allocate(xio_reg_mem *mem,
             const uint64_t size);

    void
    deallocate(xio_reg_mem *reg_mem);

    int
    on_session_event(xio_session *session,
                     xio_session_event_data *event_data);

    int
    on_response(xio_session *session,
                xio_msg* reply,
                int last_in_rxq);

    int
    on_msg_error(xio_session *session,
                 xio_status error,
                 xio_msg_direction direction,
                 xio_msg *msg);

    void
    evfd_stop_loop(int fd, int events, void *data);

    void
    run(std::promise<bool>& promise);

    bool
    is_queue_empty();

    xio_msg_s*
    pop_request();

    void
    push_request(xio_msg_s *req);

    void
    xstop_loop();

    static void
    xio_create_volume(const std::string& uri,
                      const char* volume_name,
                      size_t size,
                      void *opaque);

    static void
    xio_remove_volume(const std::string& uri,
                      const char* volume_name,
                      void* opaque);

    static void
    xio_stat_volume(const std::string& uri,
                    const std::string& volume_name,
                    void *opaque);

    static void
    xio_list_volumes(const std::string& uri,
                     std::vector<std::string>& volumes);

    static void
    xio_list_snapshots(const std::string& uri,
                       const char* volume_name,
                       std::vector<std::string>& snapshots,
                       uint64_t *size,
                       void *opaque);

    static void
    xio_create_snapshot(const std::string& uri,
                        const char* volume_name,
                        const char* snap_name,
                        int64_t timeout,
                        void *opaque);

    static void
    xio_delete_snapshot(const std::string& uri,
                        const char* volume_name,
                        const char* snap_name,
                        void *opaque);

    static void
    xio_rollback_snapshot(const std::string& uri,
                          const char* volume_name,
                          const char* snap_name,
                          void *opaque);

    static void
    xio_is_snapshot_synced(const std::string& uri,
                           const char* volume_name,
                           const char* snap_name,
                           void *opaque);

    static void
    xio_destroy_ctx_shutdown(xio_context *ctx);
private:
    std::shared_ptr<xio_context> ctx;
    std::shared_ptr<xio_session> session;
    xio_connection *conn;
    xio_session_params params;
    xio_connection_params cparams;

    std::string uri_;
    bool stopping;
    bool stopped;
    std::thread xio_thread_;

    mutable fungi::SpinLock inflight_lock;
    std::queue<xio_msg_s*> inflight_reqs;

    xio_session_ops ses_ops;
    bool disconnected;
    bool disconnecting;

    int64_t nr_req_queue;
    std::mutex req_queue_lock;
    std::condition_variable req_queue_cond;

    EventFD evfd;

    std::shared_ptr<xio_mempool> mpool;

    void
    xio_run_loop_worker(void *arg);

    void
    req_queue_wait_until(xio_msg_s *xmsg);

    void
    req_queue_release();

    void
    shutdown();

    template<class E>
    void set_exception_ptr(E e);

    static xio_connection*
    create_connection_control(session_data *sdata,
                              const std::string& uri);

    static int
    on_msg_control(xio_session *session,
                   xio_msg *reply,
                   int last_in_rqx,
                   void *cb_user_context);

    static int
    on_msg_error_control(xio_session *session,
                         xio_status error,
                         xio_msg_direction direction,
                         xio_msg *msg,
                         void *cb_user_context);

    static int
    on_session_event_control(xio_session *session,
                             xio_session_event_data *event_data,
                             void *cb_user_context);

    static void
    xio_submit_request(const std::string& uri,
                       xio_ctl_s *xctl,
                       void *opaque);

    static void
    xio_msg_prepare(xio_msg_s *xmsg);

    static void
    handle_list_volumes(xio_ctl_s *xctl,
                        xio_iovec_ex *sglist,
                        int vec_size);

    static void
    handle_list_snapshots(xio_ctl_s *xctl,
                          xio_iovec_ex *sglist,
                          int vec_size,
                          int size);

    static void
    create_vec_from_buf(xio_ctl_s *xctl,
                        xio_iovec_ex *sglist,
                        int vec_size);
};

typedef std::shared_ptr<NetworkXioClient> NetworkXioClientPtr;

} //namespace volumedriverfs

#endif //__NETWORK_XIO_CLIENT_H_
