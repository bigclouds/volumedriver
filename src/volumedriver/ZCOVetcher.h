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

#ifndef ZCO_VETCHER_H
#define ZCO_VETCHER_H

#include "FailOverCacheConfigWrapper.h"
#include "SCOAccessData.h"
#include "SCOCache.h"
#include "VolManager.h"
#include "VolumeConfig.h"

#include <memory>

#include <backend/BackendInterface.h>

namespace volumedriver
{

class ZCOVetcher
{
public:
    ZCOVetcher(const VolumeConfig& volume_config)
        : ns_(volume_config.getNS())
        , sco_cache_(*VolManager::get()->getSCOCache())
        , scosize_(volume_config.getSCOSize())
        , lba_size_(volume_config.lba_size_)
        , cluster_mult_(volume_config.cluster_mult_)
    {
        if(not sco_cache_.hasDisabledNamespace(ns_))
        {
            if(sco_cache_.hasNamespace(ns_))
            {
                LOG_WARN("Trying to restart from a namespace that is still active " << ns_);
                throw fungi::IOException("ZCOVetcher: namespace still active");
            }
            else
            {
                LOG_WARN("Namespace " << ns_ <<
                         " not found in scocache, local restart not possible");
                throw fungi::IOException("ZCOVetcher: No such namespace");
            }
        }
        BackendInterfacePtr bi(VolManager::get()->createBackendInterface(ns_));
        SCOAccessDataPersistor sadp(bi->clone());
        SCOAccessDataPtr sad(sadp.pull());

        foc_config_wrapper_ =
            VolumeFactory::get_config_from_backend<FailOverCacheConfigWrapper>(*bi);
        const uint64_t max_non_disposable =
            VolManager::get()->get_sco_cache_max_non_disposable_bytes(volume_config);
        sco_cache_.enableNamespace(ns_,
                                   0,
                                   max_non_disposable,
                                   *sad);
    }

    ~ZCOVetcher()
    {
        sco_cache_.disableNamespace(ns_);
    }

    CachedSCOPtr
    getSCO(SCO sco)
    {
        if (foc_config_wrapper_->config())
        {
            LOG_INFO("With FailOverCache, trying to get SCO " << sco <<
                     " from the FOC or the backend");
            RawFailOverCacheSCOFetcher fetcher(sco,
                                               *foc_config_wrapper_->config(),
                                               ns_,
                                               lba_size_,
                                               cluster_mult_);

            return sco_cache_.getSCO(ns_,
                                     sco,
                                     scosize_,
                                     fetcher);
        }
        else
        {
            LOG_INFO("No FailOverCache, trying to get SCO " << sco <<
                     " from the backend");

            return sco_cache_.findSCO(ns_,
                                      sco);
        }
    }

    bool
    checkSCO(ClusterLocation cluster_location,
             CheckSum::value_type cs,
             bool retry)
    {
        try
        {
            CachedSCOPtr sco_ptr = getSCO(cluster_location.sco());
            if (not sco_ptr)
            {
                return false;
            }

            fs::path sco_path = sco_ptr->path();
            if(sco_path != incremental_checksum.first)
            {
                incremental_checksum.second.reset(new IncrementalChecksum(sco_path));
                incremental_checksum.first = sco_path;
            }

            try
            {
                const CheckSum& s =
                    incremental_checksum.second->checksum((cluster_location.offset() + 1) *
                                                          lba_size_ * cluster_mult_);

                if(s.getValue() == cs)
                {
                    return true;
                }
            }
            catch(youtils::CheckSumFileNotLargeEnough& e)
            {
                LOG_ERROR("File Not Large Enough");
            }

            if(retry and
               foc_config_wrapper_->config())
            {
                fs::path tmp = FileUtils::create_temp_file(sco_path);
                FileUtils::safe_copy(sco_path, tmp);
                fs::remove(sco_path);
                incremental_checksum.first.clear();
                incremental_checksum.second.reset(0);

                sco_cache_.removeSCO(ns_,
                                     cluster_location.sco(),
                                     true,
                                     false);
                if(checkSCO(cluster_location,
                            cs,
                            false))
                {
                    fs::remove(tmp);
                    return true;
                }

                fs::remove(sco_path);
                fs::rename(tmp, sco_path);
            }

            return false;
        }
        CATCH_STD_ALL_EWHAT({
                LOG_ERROR("Could not retrieve SCO for clusterlocation " <<
                          cluster_location << ": " << EWHAT);
                return false;
            });
    }

private:
    DECLARE_LOGGER("ZCOVetcher");

    const Namespace ns_;
    SCOCache& sco_cache_;
    const uint64_t scosize_;
    const LBASize lba_size_;
    const ClusterMultiplier cluster_mult_;
    std::pair<fs::path, std::unique_ptr<youtils::IncrementalChecksum> > incremental_checksum;
    std::unique_ptr<FailOverCacheConfigWrapper> foc_config_wrapper_;
};

}

#endif // ZCO_VETCHER_H

// Local Variables: **
// mode: c++ **
// End: **
