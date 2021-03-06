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

#include "ShmIdlInterface.h"
#include "ShmClient.h"
#include "ShmCommon.h"

#include <youtils/Assert.h>
#include <youtils/UUID.h>
#include <youtils/OrbHelper.h>

namespace volumedriverfs
{

namespace ipc = boost::interprocess;
namespace yt = youtils;

namespace
{

TODO("AR: get rid of static OrbHelper");
// This is just a stopgap measure to fix the problems with the ShmServerTests (race
// with remote server startup, static OrbHelper instance used across several tests).
// In the long run it's cleaner to have an OrbHelper instance per ovs_context_t
// (e.g. via the associated ShmClient) if possible?
// Do we need locking around OrbHelper calls?
DECLARE_LOGGER("ShmClientUtils");

boost::mutex orb_helper_lock;
std::unique_ptr<yt::OrbHelper> orb_helper_instance;

#define LOCK_ORB_HELPER()                       \
    boost::lock_guard<decltype(orb_helper_lock)> g(orb_helper_lock)

}

void
ShmClient::init()
{
    LOCK_ORB_HELPER();

    VERIFY(not orb_helper_instance);
    orb_helper_instance = std::make_unique<yt::OrbHelper>("ShmClient");
}

void
ShmClient::fini()
{
    LOCK_ORB_HELPER();

    VERIFY(orb_helper_instance);
    orb_helper_instance = nullptr;
}

youtils::OrbHelper&
ShmClient::orb_helper()
{
    LOCK_ORB_HELPER();

    if (not orb_helper_instance)
    {
        orb_helper_instance = std::make_unique<yt::OrbHelper>("ShmClient");
    }
    return *orb_helper_instance;
}

ShmClient::ShmClient(const std::string& volume_name,
                     const std::string& vd_context_name,
                     const std::string& vd_context_kind,
                     const std::string& vd_object_name,
                     const std::string& vd_object_kind)
    : volume_name_(volume_name)
    , shm_segment_(new ipc::managed_shared_memory(ipc::open_only,
                                                  ShmSegmentDetails::Name()))
{
    CORBA::Object_var obj = orb_helper().getObjectReference(vd_context_name,
                                                            vd_context_kind,
                                                            vd_object_name,
                                                            vd_object_kind);

    assert(not CORBA::is_nil(obj));
    volumefactory_ref_ = ShmIdlInterface::VolumeFactory::_narrow(obj);
    assert(not CORBA::is_nil(volumefactory_ref_));

    ShmIdlInterface::CreateShmArguments createArguments;
    createArguments.volume_name = volume_name_.c_str();

    create_result.reset(volumefactory_ref_->create_shm_interface(createArguments));

    assert(youtils::UUID::isUUIDString(create_result->writerequest_uuid));
    assert(youtils::UUID::isUUIDString(create_result->writereply_uuid));
    assert(youtils::UUID::isUUIDString(create_result->readrequest_uuid));
    assert(youtils::UUID::isUUIDString(create_result->readreply_uuid));

    writerequest_mq_.reset(new ipc::message_queue(ipc::open_only,
                                                  create_result->writerequest_uuid));
    writereply_mq_.reset(new ipc::message_queue(ipc::open_only,
                                                create_result->writereply_uuid));
    readrequest_mq_.reset(new ipc::message_queue(ipc::open_only,
                                                 create_result->readrequest_uuid));
    readreply_mq_.reset(new ipc::message_queue(ipc::open_only,
                                               create_result->readreply_uuid));
    key_ = create_result->writerequest_uuid;
}

ShmClient::~ShmClient()
{
    try
    {
        volumefactory_ref_->stop_volume(volume_name_.c_str());
    }
    catch (CORBA::TRANSIENT& e)
    {
        //LOG_INFO("Transient exception when stopping mem client: server down?");
    }
    catch (std::exception& e)
    {
        //LOG_INFO("std::exception when stopping mem client: server down?");
    }
    catch (...)
    {
        //LOG_INFO("unknown exception when stopping mem client: server down?");
    }

    try
    {
        shm_segment_.reset();
    }
    catch (...)
    {
        return;
    }
}

void*
ShmClient::allocate(const uint64_t size_in_bytes)
{
    return shm_segment_->allocate(size_in_bytes,
                                  std::nothrow);
}

void
ShmClient::deallocate(void *shptr)
{
    shm_segment_->deallocate(shptr);
}

void*
ShmClient::get_address_from_handle(ipc::managed_shared_memory::handle_t handle)
{
    return shm_segment_->get_address_from_handle(handle);
}

ipc::managed_shared_memory::handle_t
ShmClient::get_handle_from_address(void *shptr)
{
    return shm_segment_->get_handle_from_address(shptr);
}

int
ShmClient::send_write_request(const void *buf,
                              const uint64_t size_in_bytes,
                              const uint64_t offset_in_bytes,
                              const void *opaque)
{
    ShmWriteRequest writerequest_;
    writerequest_.size_in_bytes = size_in_bytes;
    writerequest_.offset_in_bytes = offset_in_bytes;
    writerequest_.handle = shm_segment_->get_handle_from_address(buf);
    writerequest_.opaque = reinterpret_cast<uintptr_t>(opaque);

    try
    {
        writerequest_mq_->send(&writerequest_,
                               writerequest_size,
                               0);
    }
    catch (...)
    {
        errno = EIO;
        return -1;
    }
    return 0;
}

int
ShmClient::timed_send_write_request(const void *buf,
                                    const uint64_t size_in_bytes,
                                    const uint64_t offset_in_bytes,
                                    const void *opaque,
                                    const struct timespec* timeout)
{
    ShmWriteRequest writerequest_;
    writerequest_.size_in_bytes = size_in_bytes;
    writerequest_.offset_in_bytes = offset_in_bytes;
    writerequest_.handle = shm_segment_->get_handle_from_address(buf);
    writerequest_.opaque = reinterpret_cast<uintptr_t>(opaque);
    boost::posix_time::time_duration delay(boost::posix_time::seconds(timeout->tv_sec));
    boost::posix_time::ptime ptimeout =
        boost::posix_time::ptime(boost::posix_time::second_clock::universal_time());
    ptimeout += delay;

    try
    {
        int ret = writerequest_mq_->timed_send(&writerequest_,
                                               writerequest_size,
                                               0,
                                               ptimeout);
        if (not ret)
        {
            errno = ETIMEDOUT;
            return -1;
        }
    }
    catch (...)
    {
        errno = EIO;
        return -1;
    }
    return 0;
}

bool
ShmClient::receive_write_reply(size_t& size_in_bytes,
                               void **opaque)
{
    ShmWriteReply writereply_;
    unsigned int priority;
    ipc::message_queue::size_type received_size;

    try
    {
        writereply_mq_->receive(&writereply_,
                                writereply_size,
                                received_size,
                                priority);
        assert(received_size == writereply_size);
        *opaque = reinterpret_cast<void*>(writereply_.opaque);
    }
    catch (...)
    {
        *opaque = NULL;
        errno = EIO;
        return true;
    }
    size_in_bytes = writereply_.size_in_bytes;
    return writereply_.failed;
}

bool
ShmClient::timed_receive_write_reply(size_t& size_in_bytes,
                                     void **opaque,
                                     const struct timespec* timeout)
{
    ShmWriteReply writereply_;
    unsigned int priority;
    ipc::message_queue::size_type received_size;
    boost::posix_time::time_duration delay(boost::posix_time::seconds(timeout->tv_sec));
    boost::posix_time::ptime ptimeout =
        boost::posix_time::ptime(boost::posix_time::second_clock::universal_time());
    ptimeout += delay;

    try
    {
        int ret = writereply_mq_->timed_receive(&writereply_,
                                                writereply_size,
                                                received_size,
                                                priority,
                                                ptimeout);
        if (ret)
        {
            assert(received_size == writereply_size);
            *opaque = reinterpret_cast<void*>(writereply_.opaque);
        }
        else
        {
            *opaque = NULL;
            errno = ETIMEDOUT;
            return true;
        }
    }
    catch (...)
    {
        *opaque = NULL;
        errno = EIO;
        return true;
    }
    size_in_bytes = writereply_.size_in_bytes;
    return writereply_.failed;
}

int
ShmClient::send_read_request(const void *buf,
                             const uint64_t size_in_bytes,
                             const uint64_t offset_in_bytes,
                             const void *opaque)
{
    ShmReadRequest readrequest_;
    readrequest_.size_in_bytes = size_in_bytes;
    readrequest_.offset_in_bytes = offset_in_bytes;
    readrequest_.handle = shm_segment_->get_handle_from_address(buf);
    readrequest_.opaque = reinterpret_cast<uintptr_t>(opaque);

    try
    {
        readrequest_mq_->send(&readrequest_,
                              readrequest_size,
                              0);
    }
    catch (...)
    {
        errno = EIO;
        return -1;
    }
    return 0;
}

int
ShmClient::timed_send_read_request(const void *buf,
                                   const uint64_t size_in_bytes,
                                   const uint64_t offset_in_bytes,
                                   const void *opaque,
                                   const struct timespec* timeout)
{
    ShmReadRequest readrequest_;
    readrequest_.size_in_bytes = size_in_bytes;
    readrequest_.offset_in_bytes = offset_in_bytes;
    readrequest_.handle = shm_segment_->get_handle_from_address(buf);
    readrequest_.opaque = reinterpret_cast<uintptr_t>(opaque);
    boost::posix_time::time_duration delay(boost::posix_time::seconds(timeout->tv_sec));
    boost::posix_time::ptime ptimeout =
        boost::posix_time::ptime(boost::posix_time::second_clock::universal_time());
    ptimeout += delay;

    try
    {
        int ret = readrequest_mq_->timed_send(&readrequest_,
                                              readrequest_size,
                                              0,
                                              ptimeout);
        if (not ret)
        {
            errno = ETIMEDOUT;
            return -1;
        }
    }
    catch (...)
    {
        errno = EIO;
        return -1;
    }
    return 0;
}

bool
ShmClient::receive_read_reply(size_t& size_in_bytes,
                              void **opaque)
{
    ShmReadReply readreply_;
    unsigned int priority;
    ipc::message_queue::size_type received_size;

    try
    {
        readreply_mq_->receive(&readreply_,
                               readreply_size,
                               received_size,
                               priority);
        assert(received_size == readreply_size);
        *opaque = reinterpret_cast<void*>(readreply_.opaque);
    }
    catch (...)
    {
        *opaque = NULL;
        errno = EIO;
        return true;
    }
    size_in_bytes = readreply_.size_in_bytes;
    return readreply_.failed;
}

bool
ShmClient::timed_receive_read_reply(size_t& size_in_bytes,
                                    void **opaque,
                                    const struct timespec* timeout)
{
    ShmReadReply readreply_;
    unsigned int priority;
    ipc::message_queue::size_type received_size;
    boost::posix_time::time_duration delay(boost::posix_time::seconds(timeout->tv_sec));
    boost::posix_time::ptime ptimeout =
        boost::posix_time::ptime(boost::posix_time::second_clock::universal_time());
    ptimeout += delay;

    try
    {
        int ret = readreply_mq_->timed_receive(&readreply_,
                                               readreply_size,
                                               received_size,
                                               priority,
                                               ptimeout);
        if (ret)
        {
            assert(received_size == readreply_size);
            *opaque = reinterpret_cast<void*>(readreply_.opaque);
        }
        else
        {
            *opaque = NULL;
            errno = ETIMEDOUT;
            return true;
        }
    }
    catch (...)
    {
        *opaque = NULL;
        errno = EIO;
        return true;
    }
    size_in_bytes = readreply_.size_in_bytes;
    return readreply_.failed;
}

void
ShmClient::stop_reply_queues(int n)
{
    ShmReadReply readreply_;
    readreply_.opaque = 0;
    readreply_.stop = true;

    for (int i = 0; i < n; i++)
    {
        try
        {
            readreply_mq_->send(&readreply_,
                                readreply_size,
                                0);
        }
        catch (...)
        {}
    }

    ShmWriteReply writereply_;
    writereply_.opaque = 0;
    writereply_.stop = true;

    for (int i = 0; i < n; i++)
    {
        try
        {
            writereply_mq_->send(&writereply_,
                                 writereply_size,
                                 0);
        }
        catch (...)
        {}
    }
}

void
ShmClient::create_volume(const std::string& volume_name,
                         const uint64_t volume_size)
{
    CORBA::Object_var obj = orb_helper().getObjectReference(vd_context_name,
                                                            vd_context_kind,
                                                            vd_object_name,
                                                            vd_object_kind);
    assert(not CORBA::is_nil(obj));
    ShmIdlInterface::VolumeFactory_var volumefactory_ref =
        ShmIdlInterface::VolumeFactory::_narrow(obj);
    assert(not CORBA::is_nil(volumefactory_ref));
    volumefactory_ref->create_volume(volume_name.c_str(),
                                     volume_size);
}

void
ShmClient::remove_volume(const std::string& volume_name)
{
    CORBA::Object_var obj = orb_helper().getObjectReference(vd_context_name,
                                                            vd_context_kind,
                                                            vd_object_name,
                                                            vd_object_kind);
    assert(not CORBA::is_nil(obj));
    ShmIdlInterface::VolumeFactory_var volumefactory_ref =
        ShmIdlInterface::VolumeFactory::_narrow(obj);
    assert(not CORBA::is_nil(volumefactory_ref));
    volumefactory_ref->remove_volume(volume_name.c_str());
}

void
ShmClient::create_snapshot(const std::string& volume_name,
                           const std::string& snapshot_name,
                           const int64_t timeout)
{
    CORBA::Object_var obj = orb_helper().getObjectReference(vd_context_name,
                                                            vd_context_kind,
                                                            vd_object_name,
                                                            vd_object_kind);
    assert(not CORBA::is_nil(obj));
    ShmIdlInterface::VolumeFactory_var volumefactory_ref =
        ShmIdlInterface::VolumeFactory::_narrow(obj);
    assert(not CORBA::is_nil(volumefactory_ref));
    volumefactory_ref->create_snapshot(volume_name.c_str(),
                                       snapshot_name.c_str(),
                                       timeout);
}

void
ShmClient::rollback_snapshot(const std::string& volume_name,
                             const std::string& snapshot_name)
{
    CORBA::Object_var obj = orb_helper().getObjectReference(vd_context_name,
                                                            vd_context_kind,
                                                            vd_object_name,
                                                            vd_object_kind);
    assert(not CORBA::is_nil(obj));
    ShmIdlInterface::VolumeFactory_var volumefactory_ref =
        ShmIdlInterface::VolumeFactory::_narrow(obj);
    assert(not CORBA::is_nil(volumefactory_ref));
    volumefactory_ref->rollback_snapshot(volume_name.c_str(),
                                         snapshot_name.c_str());
}

void
ShmClient::delete_snapshot(const std::string& volume_name,
                           const std::string& snapshot_name)
{
    CORBA::Object_var obj = orb_helper().getObjectReference(vd_context_name,
                                                            vd_context_kind,
                                                            vd_object_name,
                                                            vd_object_kind);
    assert(not CORBA::is_nil(obj));
    ShmIdlInterface::VolumeFactory_var volumefactory_ref =
        ShmIdlInterface::VolumeFactory::_narrow(obj);
    assert(not CORBA::is_nil(volumefactory_ref));
    volumefactory_ref->delete_snapshot(volume_name.c_str(),
                                       snapshot_name.c_str());
}

std::vector<std::string>
ShmClient::list_snapshots(const std::string& volume_name,
                          uint64_t *size)
{
    CORBA::Object_var obj = orb_helper().getObjectReference(vd_context_name,
                                                            vd_context_kind,
                                                            vd_object_name,
                                                            vd_object_kind);
    assert(not CORBA::is_nil(obj));
    ShmIdlInterface::VolumeFactory_var volumefactory_ref =
        ShmIdlInterface::VolumeFactory::_narrow(obj);
    assert(not CORBA::is_nil(volumefactory_ref));

    ShmIdlInterface::StringSequence_var results;
    CORBA::ULongLong c_size;
    std::vector<std::string> snaps;
    volumefactory_ref->list_snapshots(volume_name.c_str(),
                                      results.out(),
                                      c_size);
    for (unsigned int i = 0; i < results->length(); i++)
    {
        snaps.push_back(results[i].in());
    }
    *size = c_size;
    return snaps;
}

int
ShmClient::is_snapshot_synced(const std::string& volume_name,
                              const std::string& snapshot_name)
{
    CORBA::Object_var obj = orb_helper().getObjectReference(vd_context_name,
                                                            vd_context_kind,
                                                            vd_object_name,
                                                            vd_object_kind);
    assert(not CORBA::is_nil(obj));
    ShmIdlInterface::VolumeFactory_var volumefactory_ref =
        ShmIdlInterface::VolumeFactory::_narrow(obj);
    assert(not CORBA::is_nil(volumefactory_ref));
    return volumefactory_ref->is_snapshot_synced(volume_name.c_str(),
                                                 snapshot_name.c_str());
}

std::vector<std::string>
ShmClient::list_volumes()
{
    CORBA::Object_var obj = orb_helper().getObjectReference(vd_context_name,
                                                            vd_context_kind,
                                                            vd_object_name,
                                                            vd_object_kind);
    assert(not CORBA::is_nil(obj));
    ShmIdlInterface::VolumeFactory_var volumefactory_ref =
        ShmIdlInterface::VolumeFactory::_narrow(obj);
    assert(not CORBA::is_nil(volumefactory_ref));

    ShmIdlInterface::StringSequence_var results;
    std::vector<std::string> volumes;
    volumefactory_ref->list_volumes(results.out());
    for (unsigned int i = 0; i < results->length(); i++)
    {
        volumes.push_back(results[i].in());
    }
    return volumes;
}

int
ShmClient::stat(struct stat *st)
{
    TODO("cnanakos: stat call from fs");
    st->st_blksize = 512;
    st->st_size = volume_size_in_bytes();
    return 0;
}

} //namespace volumedriverfs
