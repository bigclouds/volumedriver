// Copyright 2015 iNuron NV
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef VD_FAILOVER_CACHE_SYNCBRIDGE_H
#define VD_FAILOVER_CACHE_SYNCBRIDGE_H

#include "FailOverCacheBridgeCommon.h"
#include "FailOverCacheStreamers.h"
#include "FailOverCacheProxy.h"
#include "FailOverCacheClientInterface.h"
#include "SCO.h"

#include "failovercache/fungilib/CondVar.h"
#include "failovercache/fungilib/Runnable.h"
#include "failovercache/fungilib/Thread.h"

#include <youtils/FileDescriptor.h>
#include <youtils/IOException.h>

namespace volumedriver
{

class FailOverCacheTester;

class FailOverCacheSyncBridge
    : public FailOverCacheClientInterface
{
    friend class FailOverCacheTester;

public:
    FailOverCacheSyncBridge();

    FailOverCacheSyncBridge(const FailOverCacheSyncBridge&) = delete;

    FailOverCacheSyncBridge&
    operator=(const FailOverCacheSyncBridge&) = delete;

    ~FailOverCacheSyncBridge() = default;

    virtual void
    initialize(Volume* vol) override;

    virtual const char*
    getName() const override;

    virtual void
    destroy(SyncFailOverToBackend) override;

    virtual bool
    addEntries(const std::vector<ClusterLocation>& locs,
               size_t num_locs,
               uint64_t start_address,
               const uint8_t* data) override;

    virtual bool
    backup() override;

    virtual void
    newCache(std::unique_ptr<FailOverCacheProxy> cache) override;

    virtual void
    setRequestTimeout(const uint32_t seconds) override;

    virtual void
    removeUpTo(const SCO& sconame) override;

    virtual uint64_t
    getSCOFromFailOver(SCO sconame,
                       SCOProcessorFun processor) override;

    virtual void
    Flush() override;

    virtual void
    Clear() override;

    virtual FailOverCacheMode
    mode() const override;

    void
    handleException(std::exception& e,
                    const char* where);

private:
    DECLARE_LOGGER("FailOverCacheSyncBridge");

    std::unique_ptr<FailOverCacheProxy> cache_;
    fungi::Mutex mutex_;

    ClusterSize cluster_size_;
    ClusterMultiplier cluster_multiplier_;
    Volume* vol_ = { nullptr };
};

}

#endif // VD_FAILOVER_CACHE_SYNCBRIDGE

// Local Variables: **
// mode: c++ **
// End: **