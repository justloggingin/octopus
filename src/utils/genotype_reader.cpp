// Copyright (c) 2015-2020 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "genotype_reader.hpp"

#include <utility>
#include <functional>
#include <iterator>
#include <algorithm>
#include <initializer_list>
#include <cassert>

#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>

#include "basics/contig_region.hpp"
#include "basics/genomic_region.hpp"
#include "concepts/mappable.hpp"
#include "core/types/allele.hpp"
#include "core/types/variant.hpp"
#include "utils/mappable_algorithms.hpp"
#include "erase_if.hpp"
#include "io/variant/vcf_header.hpp"
#include "io/variant/vcf_spec.hpp"
#include "io/reference/reference_genome.hpp"

namespace octopus {

namespace {

bool is_missing(const VcfRecord::NucleotideSequence& allele) noexcept
{
    return allele == vcfspec::missingValue;
}

bool is_delete_masked(const VcfRecord::NucleotideSequence& allele) noexcept
{
    return allele == vcfspec::deleteMaskAllele;
}

void remove_missing_alleles(std::vector<VcfRecord::NucleotideSequence>& genotype)
{
    genotype.erase(std::remove_if(std::begin(genotype), std::end(genotype), is_missing), std::end(genotype));
}

void remove_deleted_alleles(std::vector<VcfRecord::NucleotideSequence>& genotype)
{
    genotype.erase(std::remove_if(std::begin(genotype), std::end(genotype), is_delete_masked), std::end(genotype));
}

bool is_complex(const VcfRecord::NucleotideSequence& ref, const VcfRecord::NucleotideSequence& alt) noexcept
{
    return !ref.empty() && !alt.empty() && ref.size() != alt.size() && ref.front() != alt.front();
}

bool is_ref_pad_size_known(const VcfRecord::NucleotideSequence& allele, const VcfRecord& call) noexcept
{
    return allele != call.ref() && !is_complex(call.ref(), allele);
}

auto num_matching_lhs_bases(const VcfRecord::NucleotideSequence& lhs, const VcfRecord::NucleotideSequence& rhs) noexcept
{
    auto p = std::mismatch(std::cbegin(lhs), std::cend(lhs), std::cbegin(rhs));
    return static_cast<int>(std::distance(std::cbegin(lhs), p.first));
}

auto calculate_ref_pad_size(const VcfRecord& call, const VcfRecord::NucleotideSequence& allele) noexcept
{
    if (is_delete_masked(allele)) {
        return 1;
    } else {
        return num_matching_lhs_bases(call.ref(), allele);
    }
}

bool has_indel(const VcfRecord& call) noexcept
{
    return std::any_of(std::cbegin(call.alt()), std::cend(call.alt()),
                       [&] (const auto& allele) { return allele.size() != call.ref().size(); });
}

bool has_simple_indel(const VcfRecord& call) noexcept
{
    return std::any_of(std::cbegin(call.alt()), std::cend(call.alt()),
                       [&] (const auto& allele) {
                           return allele.size() != call.ref().size() && (allele.size() == 1 || call.ref().size() == 1);
                       });
}

bool has_non_complex_indel(const VcfRecord& call) noexcept
{
    assert(!call.ref().empty());
    return std::any_of(std::cbegin(call.alt()), std::cend(call.alt()),
                       [&] (const auto& allele) {
                           assert(!allele.empty());
                           return allele.size() != call.ref().size() && allele.front() == call.ref().front();
                       });
}

auto get_region(const VcfRecord& call, GenomicRegion)
{
    return mapped_region(call);
}
auto get_region(const VcfRecord& call, ContigRegion)
{
    return contig_region(call);
}

template <typename AlleleType, typename RegionType>
boost::optional<AlleleType>
make_allele(const VcfRecord& call, VcfRecord::NucleotideSequence allele_sequence, const int max_ref_pad,
            const boost::optional<RegionType>& upstream_region = boost::none)
{
    if (is_missing(allele_sequence)) {
        return boost::none;
    } else {
        auto region = get_region(call, RegionType {});
        if (is_delete_masked(allele_sequence)) {
            if (upstream_region && overlaps(region, *upstream_region)) {
                const auto num_defined_bases = static_cast<std::size_t>(overlap_size(region, *upstream_region));
                if (num_defined_bases >= call.ref().size()) {
                    allele_sequence.clear();
                    region = expand_lhs(region, -1);
                } else {
                    allele_sequence = call.ref();
                    allele_sequence.erase(std::cbegin(allele_sequence), std::next(std::cbegin(allele_sequence), num_defined_bases));
                    region = right_overhang_region(region, *upstream_region);
                }
            } else {
                allele_sequence.clear();
                region = head_region(region);
            }
        } else if (max_ref_pad > 0) {
            auto p = std::mismatch(std::cbegin(call.ref()), std::next(std::cbegin(call.ref()), max_ref_pad),
                                   std::cbegin(allele_sequence), std::cend(allele_sequence));
            allele_sequence.erase(std::cbegin(allele_sequence), p.second);
            region = expand_lhs(region, std::distance(p.second, std::cbegin(allele_sequence)));
        }
        return AlleleType {region, std::move(allele_sequence)};
    }
}

auto extract_genotype(const VcfRecord& call, const SampleName& sample, const ReferenceGenome& reference,
                      const boost::optional<ContigRegion>& upstream_region = boost::none)
{
    if (is_refcall(call)) {
        auto refallele = demote(make_reference_allele(mapped_region(call), reference));
        return std::vector<boost::optional<ContigAllele>>(call.ploidy(sample), refallele);
    }
    auto genotype = get_genotype(call, sample);
    const auto ploidy = genotype.size();
    std::vector<boost::optional<ContigAllele>> result(ploidy, boost::none);
    if (ploidy == 0) return result;
    boost::optional<int> max_ref_pad {};
    std::vector<std::size_t> unknown_pad_indices {};
    for (std::size_t i {0}; i < ploidy; ++i) {
        auto& allele = genotype[i];
        if (is_ref_pad_size_known(allele, call)) {
            const auto allele_pad = num_matching_lhs_bases(call.ref(), allele);
            if (max_ref_pad) {
                max_ref_pad = std::max(*max_ref_pad, allele_pad);
            } else {
                max_ref_pad = allele_pad;
            }
            result[i] = make_allele<ContigAllele>(call, std::move(allele), allele_pad, upstream_region);
        } else {
            unknown_pad_indices.push_back(i);
        }
    }
    if (!max_ref_pad) {
        max_ref_pad = has_non_complex_indel(call) ? 1 : 0;
    }
    for (auto idx : unknown_pad_indices) {
        result[idx] = make_allele<ContigAllele>(call, std::move(genotype[idx]), *max_ref_pad, upstream_region);
    }
    return result;
}

bool is_spanning_deletion(const VcfRecord::NucleotideSequence& allele)
{
    return allele == vcfspec::deleteMaskAllele;
}

bool is_missing(const VcfRecord::AlleleIndex index, const VcfRecord& call)
{
    const auto& allele = get_allele(call, index);
    return is_missing(allele);
}

bool is_missing_or_spanning_deletion(const VcfRecord::AlleleIndex index, const VcfRecord& call)
{
    const auto& allele = get_allele(call, index);
    return is_missing(allele) || is_spanning_deletion(allele);
}

} // namespace

std::vector<boost::optional<Allele>>
get_resolved_alleles(const std::vector<VcfRecord>& calls, 
                     const std::size_t call_idx,
                     const VcfRecord::SampleName& sample,
                     const ReferencePadPolicy ref_pad_policy)
{
    assert(call_idx < calls.size());
    const auto& call = calls[call_idx];
    const auto& gt = call.genotype(sample);
    const auto call_region = mapped_region(call);
    boost::optional<GenomicRegion> defined_region {};
    if (call_idx > 0) {
        defined_region = encompassing_region(std::cbegin(calls), std::next(std::cbegin(calls), call_idx));
    }
    std::vector<boost::optional<Allele>> result(1 + call.alt().size());
    if (ref_pad_policy != ReferencePadPolicy::leave) {
        std::vector<std::pair<VcfRecord::AlleleIndex, Allele::NucleotideSequence>> unique_alleles {};
        unique_alleles.reserve(gt.size());
        for (const auto allele_index : gt) {
            if (!is_missing(allele_index, call)) {
                unique_alleles.emplace_back(allele_index, get_allele(call, allele_index));
            }
        }
        std::sort(std::begin(unique_alleles), std::end(unique_alleles));
        unique_alleles.erase(std::unique(std::begin(unique_alleles), std::end(unique_alleles)), std::end(unique_alleles));
        std::vector<std::size_t> unknwown_pad_allele_indices {};
        boost::optional<int> max_ref_pad {};
        for (unsigned idx {0}; idx < unique_alleles.size(); ++idx) {
            auto& allele = unique_alleles[idx].second;
            if (is_ref_pad_size_known(allele, call)) {
                const auto pad_size = calculate_ref_pad_size(call, allele);
                if (max_ref_pad) {
                    max_ref_pad = std::max(*max_ref_pad, pad_size);
                } else {
                    max_ref_pad = pad_size;
                }
                result[unique_alleles[idx].first] = make_allele<Allele>(call, std::move(allele), pad_size, defined_region);
            } else {
                unknwown_pad_allele_indices.push_back(idx);
            }
        }
        if (!max_ref_pad) {
            max_ref_pad = has_non_complex_indel(call) ? 1 : 0;
        }
        for (const auto idx : unknwown_pad_allele_indices) {
            auto& allele = unique_alleles[idx].second;
            result[unique_alleles[idx].first] = make_allele<Allele>(call, std::move(allele), *max_ref_pad, defined_region);
        }
    } else {
        for (const auto allele_index : gt) {
            if (!result[allele_index] && !is_missing(allele_index, call)) {
                result[allele_index] = make_allele<Allele>(call, get_allele(call, allele_index), 0, defined_region);
            }
        }
    }
    return result;
}

namespace {

auto extract_phase_region(const VcfRecord& call, const SampleName& sample)
{
    auto result = get_phase_region(call, sample);
    if (result) return *result;
    return GenomicRegion {call.chrom(), contig_region(call)};
}

struct CallWrapper : public Mappable<CallWrapper>
{
    CallWrapper(const VcfRecord& record, GenomicRegion phase_region)
    : call {std::cref(record)}
    , phase_region {std::move(phase_region)}
    {}
    CallWrapper(const VcfRecord& record, const SampleName& sample)
    : CallWrapper {record, extract_phase_region(record, sample)}
    {}
    
    std::reference_wrapper<const VcfRecord> call;
    GenomicRegion phase_region;
    const GenomicRegion& mapped_region() const noexcept { return phase_region; }
    const VcfRecord& get() const noexcept { return call.get(); }
};

auto wrap_calls(const std::vector<VcfRecord>& calls, const SampleName& sample)
{
    std::vector<CallWrapper> result {};
    result.reserve(calls.size());
    const VcfRecord* last_variant = nullptr; 
    for (const auto& call : calls) {
        if (last_variant && is_refcall(call)) {
            result.emplace_back(call, closed_region(*last_variant, call));
        } else {
            result.emplace_back(call, sample);
            last_variant = std::addressof(call);
        }
    }
    return result;
}

std::vector<std::vector<CallWrapper>>
segment_into_contiguous_phase_blocks(const std::vector<VcfRecord>& calls, const SampleName& sample,
                                     const bool merge_unphased_refcalls = true)
{
    auto result = segment_overlapped_copy(wrap_calls(calls, sample));
    if (result.size() > 1 && merge_unphased_refcalls) {
        bool found_refcall {false};
        for (auto first_refcall_itr = std::begin(result); first_refcall_itr != std::end(result);) {
            const static auto is_refcall_block = [] (const auto& block) { return block.size() == 1 && is_refcall(block[0].get()); };
            first_refcall_itr = std::find_if(first_refcall_itr, std::end(result), is_refcall_block);
            if (first_refcall_itr == std::end(result)) break;
            const auto last_refcall_itr = std::find_if_not(std::next(first_refcall_itr), std::end(result), is_refcall_block);
            first_refcall_itr->reserve(std::distance(first_refcall_itr, last_refcall_itr));
            std::for_each(std::next(first_refcall_itr), last_refcall_itr, [&] (auto& refcall) {
                first_refcall_itr->push_back(std::move(refcall[0]));
                refcall.clear();
            });
            const auto block_region = encompassing_region(*first_refcall_itr);
            for (auto& call : *first_refcall_itr) {
                call.phase_region = block_region;
            }
            first_refcall_itr = last_refcall_itr;
            found_refcall = true;
        }
        if (found_refcall) {
            erase_if(result, [] (const auto& block) { return block.empty(); });
        }
    }
    return result;
}

auto get_max_ploidy(const std::vector<CallWrapper>& calls, const SampleName& sample)
{
    unsigned result {0};
    for (const auto& call : calls) {
        result = std::max(result, call.get().ploidy(sample));
    }
    return result;
}

auto make_genotype(std::vector<Haplotype::Builder>&& haplotypes)
{
    Genotype<Haplotype> result {static_cast<unsigned>(haplotypes.size())};
    for (auto& haplotype : haplotypes) {
        result.emplace(haplotype.build());
    }
    return result;
}

Genotype<Haplotype>
extract_genotype(const std::vector<CallWrapper>& phased_calls,
                 const GenomicRegion& region,
                 const SampleName& sample,
                 const ReferenceGenome& reference)
{
    assert(!phased_calls.empty());
    assert(contains(region, encompassing_region(phased_calls)));
    const auto max_ploidy = get_max_ploidy(phased_calls, sample);
    std::vector<Haplotype::Builder> haplotypes(max_ploidy, Haplotype::Builder {region, reference});
    boost::optional<ContigRegion> defined_region {};
    for (const auto& call : phased_calls) {
        auto genotype = extract_genotype(call.call, sample, reference, defined_region);
        assert(genotype.size() <= max_ploidy);
        for (unsigned i {0}; i < genotype.size(); ++i) {
            if (genotype[i] && haplotypes[i].can_push_back(*genotype[i])) {
                haplotypes[i].push_back(std::move(*genotype[i]));
            }
        }
        if (defined_region) {
            defined_region = closed_region(*defined_region, contig_region(call));
        } else {
            defined_region = contig_region(call);
        }
    }
    return make_genotype(std::move(haplotypes));
}

} // namespace

GenotypeMap
extract_genotypes(const std::vector<VcfRecord>& calls,
                  const std::vector<VcfRecord::SampleName>& samples,
                  const ReferenceGenome& reference,
                  boost::optional<GenomicRegion> call_region)
{
    if (calls.empty()) return {};
    GenotypeMap result {samples.size()};
    for (const auto& sample : samples) {
        const auto wrapped_calls = segment_into_contiguous_phase_blocks(calls, sample);
        if (wrapped_calls.size() == 1) {
            if (!call_region) {
                call_region = encompassing_region(wrapped_calls.front());
            }
            auto genotype = extract_genotype(wrapped_calls.front(), *call_region, sample, reference);
            if (genotype.ploidy() > 0) {
                result[sample] = {std::move(genotype)};
            } else {
                result[sample] = {};
            }
        } else { // wrapped_calls.size() > 1
            auto call_itr = std::cbegin(wrapped_calls);
            GenomicRegion region;
            if (call_region) {
                region = left_overhang_region(*call_region, std::next(call_itr)->front());
            } else {
                region = left_overhang_region(call_itr->front(), std::next(call_itr)->front());
            }
            auto genotype = extract_genotype(*call_itr, region, sample, reference);
            if (genotype.ploidy() > 0) result[sample] = {std::move(genotype)};
            ++call_itr;
            for (auto penultimate = std::prev(std::cend(wrapped_calls)); call_itr != penultimate; ++call_itr) {
                region = *intervening_region(std::prev(call_itr)->back(), std::next(call_itr)->front());
                genotype = extract_genotype(*call_itr, region, sample, reference);
                if (genotype.ploidy() > 0) result.at(sample).insert(std::move(genotype));
            }
            if (call_region) {
                region = right_overhang_region(*call_region, std::prev(call_itr)->back());
            } else {
                region = right_overhang_region(call_itr->back(), std::prev(call_itr)->back());
            }
            genotype = extract_genotype(*call_itr, region, sample, reference);
            if (genotype.ploidy() > 0) {
                result.at(sample).insert(std::move(genotype));
            } else {
                result[sample] = {};
            }
        }
    }
    return result;
}

} // namespace octopus
