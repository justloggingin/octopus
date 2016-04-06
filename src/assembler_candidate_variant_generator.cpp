//
//  assembler_candidate_variant_generator.cpp
//  Octopus
//
//  Created by Daniel Cooke on 01/03/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#include "assembler_candidate_variant_generator.hpp"

#include <algorithm>
#include <iterator>
#include <deque>
#include <cassert>

#include "common.hpp"
#include "reference_genome.hpp"
#include "aligned_read.hpp"
#include "variant.hpp"
#include "sequence_utils.hpp"
#include "logging.hpp"

namespace Octopus
{
AssemblerCandidateVariantGenerator::AssemblerCandidateVariantGenerator(const ReferenceGenome& reference,
                                                                       std::vector<unsigned> kmer_sizes,
                                                                       QualityType min_base_quality,
                                                                       unsigned min_supporting_reads,
                                                                       SizeType max_variant_size)
:
reference_ {reference},
default_kmer_sizes_ {kmer_sizes},
fallback_kmer_sizes_ {},
assemblers_ {},
region_assembled_ {},
sequence_buffer_ {},
min_base_quality_ {min_base_quality},
min_supporting_reads_ {min_supporting_reads},
max_variant_size_ {max_variant_size}
{
    using std::begin; using std::end;
    
    if (default_kmer_sizes_.empty()) {
        return;
    }
    
    std::sort(begin(default_kmer_sizes_), end(default_kmer_sizes_));
    
    default_kmer_sizes_.erase(std::unique(begin(default_kmer_sizes_), end(default_kmer_sizes_)),
                              end(default_kmer_sizes_));
    
    assemblers_.reserve(kmer_sizes.size());
    
    for (const auto k : default_kmer_sizes_) {
        assemblers_.emplace_back(k);
    }
    
    constexpr unsigned num_fallbacks {6};
    constexpr unsigned fallback_interval_size {10};
    
    fallback_kmer_sizes_.resize(num_fallbacks);
    
    auto k = default_kmer_sizes_.back();
    std::generate_n(begin(fallback_kmer_sizes_), num_fallbacks,
                    [&k, fallback_interval_size] () {
                        k += fallback_interval_size;
                        return k;
                    });
}

bool AssemblerCandidateVariantGenerator::requires_reads() const noexcept
{
    return true;
}

bool are_all_bases_good_quality(const AlignedRead& read,
                                const AlignedRead::QualityType min_quality)
{
    return std::all_of(std::cbegin(read.get_qualities()),
                       std::cend(read.get_qualities()),
                       [min_quality] (const char quality) {
                           return quality >= min_quality;
                       });
}

AlignedRead::SequenceType
transform_low_base_qualities_to_n(const AlignedRead& read, const AlignedRead::QualityType min_quality)
{
    auto result = read.get_sequence();
    
    std::transform(std::cbegin(result), std::cend(result),
                   std::cbegin(read.get_qualities()),
                   std::begin(result),
                   [min_quality] (const char base, const auto quality) {
                       return (quality >= min_quality) ? base : 'N';
                   });
    
    return result;
}

void AssemblerCandidateVariantGenerator::add_read(const AlignedRead& read)
{
    if (are_all_bases_good_quality(read, min_base_quality_)) {
        for (auto& assembler : assemblers_) {
            assembler.insert_read(read.get_sequence());
        }
        sequence_buffer_.push_back(read.get_sequence());
    } else {
        auto masked_sequence = transform_low_base_qualities_to_n(read, min_base_quality_);
        for (auto& assembler : assemblers_) {
            assembler.insert_read(masked_sequence);
        }
        sequence_buffer_.push_back(std::move(masked_sequence));
    }
    
    if (region_assembled_) {
        region_assembled_ = encompassing_region(read, *region_assembled_);
    } else {
        region_assembled_ = mapped_region(read);
    }
}

void AssemblerCandidateVariantGenerator::add_reads(std::vector<AlignedRead>::const_iterator first,
                                                   std::vector<AlignedRead>::const_iterator last)
{
    std::for_each(first, last, [this] (const auto& read ) { add_read(read); });
}

void AssemblerCandidateVariantGenerator::add_reads(MappableFlatMultiSet<AlignedRead>::const_iterator first,
                                                   MappableFlatMultiSet<AlignedRead>::const_iterator last)
{
    std::for_each(first, last, [this] (const auto& read ) { add_read(read); });
}

template <typename Container>
void remove_nonoverlapping(Container& candidates, const GenomicRegion& region)
{
    const auto it = std::remove_if(std::begin(candidates), std::end(candidates),
                                   [&region] (const Variant& candidate) {
                                       return !overlaps(candidate, region);
                                   });
    candidates.erase(it, std::end(candidates));
}

std::vector<Variant> AssemblerCandidateVariantGenerator::generate_candidates(const GenomicRegion& region)
{
    std::vector<Variant> result {};
    
    if (!region_assembled_) {
        return result;
    }
    
    unsigned num_defaults_unsuccessful {0};
    
    for (Assembler& assembler : assemblers_) {
        const auto reference_region = expand(*region_assembled_, assembler.kmer_size());
        
        const auto reference_sequence = reference_.get().get_sequence(reference_region);
        
        if (has_ns(reference_sequence)) {
            if (!result.empty()) {
                remove_nonoverlapping(result, region);
            }
            return result;
        }
        
        assembler.insert_reference(reference_sequence);
        
        const auto success = try_assemble_region(assembler, reference_sequence,
                                                 reference_region, result);
        
        if (success) {
            result.reserve((assemblers_.size() - num_defaults_unsuccessful) * result.size());
            if (DEBUG_MODE) {
                Logging::DebugLogger log {};
                stream(log) << "Default assembler with kmer size " << assembler.kmer_size()
                            << " completed";
            }
        } else if (DEBUG_MODE) {
            Logging::DebugLogger log {};
            stream(log) << "Default assembler with kmer size " << assembler.kmer_size() << " failed";
            ++num_defaults_unsuccessful;
        }
    }
    
    if (num_defaults_unsuccessful == assemblers_.size()) {
        for (const auto k : fallback_kmer_sizes_) {
            const auto reference_region = expand(*region_assembled_, k);
            
            const auto reference_sequence = reference_.get().get_sequence(reference_region);
            
            if (has_ns(reference_sequence)) {
                if (!result.empty()) {
                    remove_nonoverlapping(result, region);
                }
                return result;
            }
            
            Assembler assembler {k, reference_sequence};
            
            for (const auto& read_sequence : sequence_buffer_) {
                assembler.insert_read(read_sequence);
            }
            
            const auto success = try_assemble_region(assembler, reference_sequence,
                                                     reference_region, result);
            
            if (success) {
                if (DEBUG_MODE) {
                    Logging::DebugLogger log {};
                    stream(log) << "Fallback assembler with kmer size " << k << " completed";
                }
                break;
            } else {
                if (DEBUG_MODE) {
                    Logging::DebugLogger log {};
                    stream(log) << "Fallback assembler with kmer size " << k << " failed";
                }
            }
        }
    }
    
    remove_nonoverlapping(result, region); // as we expanded original region
    
    if (DEBUG_MODE) {
        Logging::DebugLogger log {};
        debug::print_generated_candidates(stream(log), result, "local re-assembly");
    }
    
    return result;
}

void AssemblerCandidateVariantGenerator::clear()
{
    for (auto& assembler : assemblers_) {
        assembler.clear();
    }
    region_assembled_ = boost::none;
    sequence_buffer_.clear();
}

// private methods

void trim_reference(Assembler::Variant& v)
{
    const auto p = std::mismatch(std::begin(v.ref), std::end(v.ref),
                                 std::begin(v.alt), std::end(v.alt));
    
    v.begin_pos += std::distance(std::begin(v.ref), p.first);
    
    v.ref.erase(std::begin(v.ref), p.first);
    v.alt.erase(std::begin(v.alt), p.second);
    
    const auto p2 = std::mismatch(std::rbegin(v.ref), std::rend(v.ref),
                                  std::rbegin(v.alt), std::rend(v.alt));
    
    v.ref.erase(p2.first.base(), std::end(v.ref));
    v.alt.erase(p2.second.base(), std::end(v.alt));
}

template <typename Container>
void trim_reference(Container& variants)
{
    for (auto& v : variants) {
        trim_reference(v);
    }
}

bool is_mnv(const Assembler::Variant& v)
{
    return v.ref.size() > 1 && v.ref.size() == v.alt.size();
}

std::vector<Assembler::Variant> split_mnv(Assembler::Variant&& v)
{
    using std::begin; using std::end; using std::next; using std::prev; using std::distance;
    
    std::vector<Assembler::Variant> result {};
    result.reserve(4);
    
    result.emplace_back(v.begin_pos, v.ref.front(), v.alt.front());
    
    const auto last = prev(end(v.ref));
    
    auto p = std::mismatch(next(begin(v.ref)), last, next(begin(v.alt)));
    
    while (p.first != prev(end(v.ref))) {
        result.emplace_back(v.begin_pos + distance(begin(v.ref) - 1, p.first), *p.first, *p.second);
        p = std::mismatch(next(p.first), last, next(p.second));
    }
    
    const auto pos = v.begin_pos + v.ref.size() - 1;
    
    v.ref.erase(begin(v.ref), prev(end(v.ref)));
    v.alt.erase(begin(v.alt), prev(end(v.alt)));
    
    result.emplace_back(pos, std::move(v.ref), std::move(v.alt));
    
    return result;
}

template <typename Container>
void split_mnvs(Container& candidates)
{
    using std::begin; using std::end; using std::make_move_iterator;
    
    const auto it = std::stable_partition(begin(candidates), end(candidates),
                                          [] (const auto& candidate) {
                                              return !is_mnv(candidate);
                                          });
    
    std::deque<Assembler::Variant> snps {};
    
    std::for_each(make_move_iterator(it), make_move_iterator(end(candidates)),
                  [&snps] (auto&& mnp) {
                      auto mnv_snps = split_mnv(std::move(mnp));
                      snps.insert(end(snps), make_move_iterator(begin(mnv_snps)),
                                  make_move_iterator(end(mnv_snps)));
                  });
    
    const auto variant_less = [] (const auto& lhs, const auto& rhs) {
        return lhs.begin_pos < rhs.begin_pos || lhs.alt < rhs.alt;
    };
    
    std::sort(begin(snps), end(snps), variant_less);
    
    snps.erase(std::unique(begin(snps), end(snps)), end(snps));
    
    const auto num_mnvs = std::distance(it, end(candidates));
    
    if (snps.size() < num_mnvs) {
        const auto it3 = std::move(begin(snps), end(snps), it);
        candidates.erase(it3, end(candidates));
        std::inplace_merge(begin(candidates), it, end(candidates), variant_less);
    } else {
        const auto it2 = std::next(begin(snps), num_mnvs);
        
        std::move(begin(snps), it2, it);
        
        const auto it3 = candidates.insert(end(candidates),
                                           make_move_iterator(it2),
                                           make_move_iterator(end(snps)));
        
        std::inplace_merge(begin(candidates), it3, end(candidates), variant_less);
    }
}

template <typename C1, typename C2>
void add_to_mapped_variants(C1& result, C2&& variants, const GenomicRegion& region)
{
    result.reserve(result.size() + variants.size());
    
    const auto it = std::end(result);
    
    for (auto& variant : variants) {
        result.emplace_back(contig_name(region),
                            region.get_begin() + static_cast<GenomicRegion::SizeType>(variant.begin_pos),
                            std::move(variant.ref),
                            std::move(variant.alt)
                            );
    }
    
    std::sort(it, std::end(result));
    
    result.erase(std::unique(std::begin(result), std::end(result)), std::end(result));
}

bool AssemblerCandidateVariantGenerator::try_assemble_region(Assembler& assembler,
                                                             const SequenceType& reference_sequence,
                                                             const GenomicRegion& reference_region,
                                                             std::vector<Variant>& result)
{
    assembler.remove_trivial_nonreference_cycles();
    
    if (!assembler.prune(min_supporting_reads_)) {
        return false;
    }
    
    auto variants = assembler.extract_variants();
    
    assembler.clear();
    
    trim_reference(variants);
    split_mnvs(variants);
    
    add_to_mapped_variants(result, std::move(variants), reference_region);
    
    return true;
}

} // namespace Octopus
