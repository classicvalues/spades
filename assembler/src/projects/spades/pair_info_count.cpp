//***************************************************************************
//* Copyright (c) 2015 Saint Petersburg State University
//* Copyright (c) 2011-2014 Saint Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

#include <paired_info/is_counter.hpp>
#include "io/dataset_support/read_converter.hpp"

#include "pair_info_count.hpp"
#include "assembly_graph/graph_alignment/short_read_mapper.hpp"
#include "assembly_graph/graph_alignment/long_read_mapper.hpp"
#include "paired_info/pair_info_filler.hpp"
#include "algorithms/path_extend/split_graph_pair_info.hpp"
#include "paired_info/bwa_pair_info_filler.hpp"
#include "utils/adt/bf.hpp"
#include "utils/adt/hll.hpp"

namespace debruijn_graph {

typedef io::SequencingLibrary<config::DataSetData> SequencingLib;
using PairedInfoFilter = bf::counting_bloom_filter<std::pair<EdgeId, EdgeId>>;
using EdgePairCounter = hll::hll<std::pair<EdgeId, EdgeId>>;

class DEFilter : public SequenceMapperListener {
  public:
    DEFilter(PairedInfoFilter &filter)
            : bf_(filter) {}

    void ProcessPairedRead(size_t,
                           const io::PairedRead&,
                           const MappingPath<EdgeId>& read1,
                           const MappingPath<EdgeId>& read2) override {
        ProcessPairedRead(read1, read2);
    }
    void ProcessPairedRead(size_t,
                           const io::PairedReadSeq&,
                           const MappingPath<EdgeId>& read1,
                           const MappingPath<EdgeId>& read2) override {
        ProcessPairedRead(read1, read2);
    }
  private:
    void ProcessPairedRead(const MappingPath<EdgeId>& path1,
                           const MappingPath<EdgeId>& path2) {
        for (size_t i = 0; i < path1.size(); ++i) {
            std::pair<EdgeId, MappingRange> mapping_edge_1 = path1[i];
            for (size_t j = 0; j < path2.size(); ++j) {
                std::pair<EdgeId, MappingRange> mapping_edge_2 = path2[j];
                bf_.add({mapping_edge_1.first, mapping_edge_2.first});
            }
        }
    }

    PairedInfoFilter &bf_;
};

class EdgePairCounterFiller : public SequenceMapperListener {
    static uint64_t EdgePairHash(const std::pair<EdgeId, EdgeId> &e) {
        uint64_t h1 = e.first.hash();
        return CityHash64WithSeeds((const char*)&h1, sizeof(h1), e.second.hash(), 0x0BADF00D);
    }
    
  public:
    EdgePairCounterFiller(size_t thread_num)
            : counter_(EdgePairHash) {
        buf_.reserve(thread_num);
        for (unsigned i = 0; i < thread_num; ++i)
          buf_.emplace_back(EdgePairHash);
    }

    void MergeBuffer(size_t i) override {
        counter_.merge(buf_[i]);
        buf_[i].clear();
    }
    
    void ProcessPairedRead(size_t idx,
                           const io::PairedRead&,
                           const MappingPath<EdgeId>& read1,
                           const MappingPath<EdgeId>& read2) override {
        ProcessPairedRead(buf_[idx], read1, read2);
    }
    void ProcessPairedRead(size_t idx,
                           const io::PairedReadSeq&,
                           const MappingPath<EdgeId>& read1,
                           const MappingPath<EdgeId>& read2) override {
        ProcessPairedRead(buf_[idx], read1, read2);
    }

    double cardinality() const {
        std::pair<double, bool> res = counter_.cardinality();
        INFO("" << res.first << ":" << res.second);
        return (!res.second ? 512ull * 1024 * 1024 : res.first);
    }
  private:
    void ProcessPairedRead(EdgePairCounter &buf,
                           const MappingPath<EdgeId>& path1,
                           const MappingPath<EdgeId>& path2) {
        for (size_t i = 0; i < path1.size(); ++i) {
            std::pair<EdgeId, MappingRange> mapping_edge_1 = path1[i];
            for (size_t j = 0; j < path2.size(); ++j) {
                std::pair<EdgeId, MappingRange> mapping_edge_2 = path2[j];
                buf.add({mapping_edge_1.first, mapping_edge_2.first});
            }
        }
    }

    std::vector<EdgePairCounter> buf_;
    EdgePairCounter counter_;
};

bool RefineInsertSizeForLib(const conj_graph_pack &gp,
                            PairedInfoFilter &filter,
                            size_t ilib, size_t edge_length_threshold) {
    INFO("Estimating insert size (takes a while)");
    InsertSizeCounter hist_counter(gp, edge_length_threshold, /* ignore negative */ true);
    DEFilter filter_counter(filter);
    EdgePairCounterFiller pcounter(cfg::get().max_threads);

    SequenceMapperNotifier notifier(gp);
    notifier.Subscribe(ilib, &hist_counter);
    notifier.Subscribe(ilib, &pcounter);
    notifier.Subscribe(ilib, &filter_counter);

    SequencingLibrary &reads = cfg::get_writable().ds.reads[ilib];
    auto &data = reads.data();
    VERIFY(data.read_length != 0);
    auto paired_streams = paired_binary_readers(reads, false);

    VERIFY(reads.data().read_length != 0);
    notifier.ProcessLibrary(paired_streams, ilib, *ChooseProperMapper(gp, reads));

    INFO(hist_counter.mapped() << " paired reads (" <<
         ((double) hist_counter.mapped() * 100.0 / (double) hist_counter.total()) <<
         "% of all) aligned to long edges");
    if (hist_counter.negative() > 3 * hist_counter.mapped())
        WARN("Too much reads aligned with negative insert size. Is the library orientation set properly?");
    if (hist_counter.mapped() == 0)
        return false;

    INFO("Edge pairs: " << pcounter.cardinality());
    
    std::map<size_t, size_t> percentiles;
    hist_counter.FindMean(data.mean_insert_size, data.insert_size_deviation, percentiles);
    hist_counter.FindMedian(data.median_insert_size, data.insert_size_mad,
                            data.insert_size_distribution);
    if (data.median_insert_size < gp.k_value + 2)
        return false;

    std::tie(data.insert_size_left_quantile,
             data.insert_size_right_quantile) = omnigraph::GetISInterval(0.8,
                                                                         data.insert_size_distribution);

    return !data.insert_size_distribution.empty();
}

void ProcessSingleReads(conj_graph_pack &gp,
                        size_t ilib,
                        bool use_binary = true,
                        bool map_paired = false) {
    //FIXME make const
    auto& reads = cfg::get_writable().ds.reads[ilib];

    SequenceMapperNotifier notifier(gp);
    //FIXME pretty awful, would be much better if listeners were shared ptrs
    LongReadMapper read_mapper(gp.g, gp.single_long_reads[ilib],
                               ChooseProperReadPathExtractor(gp.g, reads.type()));

    notifier.Subscribe(ilib, &read_mapper);

    auto mapper_ptr = ChooseProperMapper(gp, reads);
    if (use_binary) {
        auto single_streams = single_binary_readers(reads, false, map_paired);
        notifier.ProcessLibrary(single_streams, ilib, *mapper_ptr);
    } else {
        auto single_streams = single_easy_readers(reads, false,
                                                  map_paired, /*handle Ns*/false);
        notifier.ProcessLibrary(single_streams, ilib, *mapper_ptr);
    }
    cfg::get_writable().ds.reads[ilib].data().single_reads_mapped = true;
}

void ProcessPairedReads(conj_graph_pack &gp, size_t ilib, PairedInfoFilter &filter) {
    SequencingLibrary &reads = cfg::get_writable().ds.reads[ilib];
    const auto &data = reads.data();

    bool calculate_threshold = (cfg::get().mode != config::pipeline_type::meta &&
                                reads.type() == io::LibraryType::PairedEnd);
    SequenceMapperNotifier notifier(gp);
    INFO("Left insert size quantile " << data.insert_size_left_quantile <<
         ", right insert size quantile " << data.insert_size_right_quantile);

    path_extend::SplitGraphPairInfo
            split_graph(gp, (size_t)data.median_insert_size,
                        (size_t) data.insert_size_deviation,
                        (size_t) data.insert_size_left_quantile,
                        (size_t) data.insert_size_right_quantile,
                        data.read_length, gp.g.k(),
                        cfg::get().pe_params.param_set.split_edge_length,
                        data.insert_size_distribution);

    if (calculate_threshold)
        notifier.Subscribe(ilib, &split_graph);

    LatePairedIndexFiller pif(gp.g,
                              [&](const std::pair<EdgeId, EdgeId> &ep,
                                  const MappingRange&, const MappingRange&) {
                                  return (filter.lookup(ep) > 1 ? 1. : 0.);
                              },
                              gp.paired_indices[ilib]);
    notifier.Subscribe(ilib, &pif);

    auto paired_streams = paired_binary_readers(reads, false, (size_t) data.mean_insert_size);
    notifier.ProcessLibrary(paired_streams, ilib, *ChooseProperMapper(gp, reads));
    cfg::get_writable().ds.reads[ilib].data().pi_threshold = split_graph.GetThreshold();
}

static bool HasGoodRRLibs() {
    for (const auto &lib : cfg::get().ds.reads) {
        if (lib.is_contig_lib())
            continue;

        if (lib.is_paired() &&
            lib.data().mean_insert_size == 0.0)
            continue;

        if (lib.is_repeat_resolvable())
            return true;
    }

    return false;
}

static bool HasOnlyMP() {
    for (const auto &lib : cfg::get().ds.reads) {
        if (lib.type() == io::LibraryType::PathExtendContigs)
            continue;

        if (lib.type() != io::LibraryType::MatePairs &&
            lib.type() != io::LibraryType::HQMatePairs)
            return false;
    }

    return true;
}

//todo improve logic
static bool ShouldMapSingleReads(size_t ilib) {
    using config::single_read_resolving_mode;
    switch (cfg::get().single_reads_rr) {
        case single_read_resolving_mode::all:
            return true;
        case single_read_resolving_mode::only_single_libs:
            //Map when no PacBio/paried libs or only mate-pairs or single lib itself
            if (!HasGoodRRLibs() || HasOnlyMP() ||
                cfg::get().ds.reads[ilib].type() == io::LibraryType::SingleReads) {
                if (cfg::get().mode != debruijn_graph::config::pipeline_type::meta) {
                    return true;
                } else {
                    WARN("Single reads are not used in metagenomic mode");
                }
            }
            break;
        case single_read_resolving_mode::none:
            break;
        default:
            VERIFY_MSG(false, "Invalid mode value");
    }
    return false;
}

void PairInfoCount::run(conj_graph_pack &gp, const char *) {
    gp.InitRRIndices();
    gp.EnsureBasicMapping();

    //fixme implement better universal logic
    size_t edge_length_threshold = cfg::get().mode == config::pipeline_type::meta ? 1000 : stats::Nx(gp.g, 50);
    INFO("Min edge length for estimation: " << edge_length_threshold);

    bwa_pair_info::BWAPairInfoFiller bwa_counter(gp.g,
                                                 cfg::get().bwa.path_to_bwa,
                                                 path::append_path(cfg::get().output_dir, "bwa_count"),
                                                 cfg::get().max_threads, !cfg::get().bwa.debug);

    for (size_t i = 0; i < cfg::get().ds.reads.lib_count(); ++i) {
        const auto &lib = cfg::get().ds.reads[i];
        if (lib.is_hybrid_lib()) {
            INFO("Library #" << i << " was mapped earlier on hybrid aligning stage, skipping");
            continue;
        } else if (lib.is_contig_lib()) {
            INFO("Mapping contigs library #" << i);
            ProcessSingleReads(gp, i, false);
        } else if (cfg::get().bwa.bwa_enable && lib.is_bwa_alignable()) {
            bwa_counter.ProcessLib(i, cfg::get_writable().ds.reads[i], gp.paired_indices[i],
                                   edge_length_threshold, cfg::get().bwa.min_contig_len);
        } else {
            INFO("Estimating insert size for library #" << i);
            const auto &lib_data = lib.data();
            size_t rl = lib_data.read_length;
            size_t k = cfg::get().K;
            PairedInfoFilter
               filter([](const std::pair<EdgeId, EdgeId> &e, uint64_t seed) {
                       uint64_t h1 = e.first.hash();
                       return CityHash64WithSeeds((const char*)&h1, sizeof(h1), e.second.hash(), seed);
                   },
                   1024*1024*1024);

            if (!RefineInsertSizeForLib(gp, filter, i, edge_length_threshold)) {
                cfg::get_writable().ds.reads[i].data().mean_insert_size = 0.0;
                WARN("Unable to estimate insert size for paired library #" << i);
                if (rl > 0 && rl <= k) {
                    WARN("Maximum read length (" << rl << ") should be greater than K (" << k << ")");
                } else if (rl <= k * 11 / 10) {
                    WARN("Maximum read length (" << rl << ") is probably too close to K (" << k << ")");
                } else {
                    WARN("None of paired reads aligned properly. Please, check orientation of your read pairs.");
                }
                continue;
            }

            INFO("  Insert size = " << lib_data.mean_insert_size <<
                 ", deviation = " << lib_data.insert_size_deviation <<
                 ", left quantile = " << lib_data.insert_size_left_quantile <<
                 ", right quantile = " << lib_data.insert_size_right_quantile <<
                 ", read length = " << lib_data.read_length);

            if (lib_data.mean_insert_size < 1.1 * (double) rl)
                WARN("Estimated mean insert size " << lib_data.mean_insert_size
                     << " is very small compared to read length " << rl);

            INFO("Mapping library #" << i);
            bool map_single_reads = ShouldMapSingleReads(i);
            cfg::get_writable().use_single_reads |= map_single_reads;

            if (lib.is_paired() && lib.data().mean_insert_size != 0.0) {
                INFO("Mapping paired reads (takes a while) ");
                ProcessPairedReads(gp, i, filter);
            }

            if (map_single_reads) {
                INFO("Mapping single reads (takes a while) ");
                ProcessSingleReads(gp, i, /*use_binary*/true, /*map_paired*/true);
                INFO("Total paths obtained from single reads: " << gp.single_long_reads[i].size());
            }
        }
    }

    SensitiveReadMapper<Graph>::EraseIndices();
}

}
