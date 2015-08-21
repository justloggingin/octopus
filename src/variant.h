//
//  variant.h
//  Octopus
//
//  Created by Daniel Cooke on 05/02/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#ifndef Octopus_variant_h
#define Octopus_variant_h

#include <string>
#include <ostream>
#include <boost/functional/hash.hpp> // boost::hash_combine

#include "genomic_region.h"
#include "allele.h"
#include "comparable.h"
#include "mappable.h"

/**
 A variant is a combination of a reference allele and an alternative allele.
 */
class Variant : public Comparable<Variant>, public Mappable<Variant>
{
public:
    using SizeType     = GenomicRegion::SizeType;
    using SequenceType = Allele::SequenceType;
    
    Variant() = default;
    template <typename GenomicRegion_, typename SequenceType1, typename SequenceType2>
    explicit Variant(GenomicRegion_&& reference_allele_region, SequenceType1&& reference_allele,
                     SequenceType2&& alternative_allele);
    template <typename SequenceType1, typename SequenceType2, typename SequenceType3>
    explicit Variant(SequenceType1&& reference_contig_name, SizeType reference_begin,
                     SequenceType2&& reference_allele, SequenceType3&& alternative_allele);
    ~Variant() = default;
    
    Variant(const Variant&)            = default;
    Variant& operator=(const Variant&) = default;
    Variant(Variant&&)                 = default;
    Variant& operator=(Variant&&)      = default;
    
    const GenomicRegion& get_region() const noexcept;
    SizeType reference_allele_size() const noexcept;
    SizeType alternative_allele_size() const noexcept;
    Allele get_reference_allele() const;
    Allele get_alternative_allele() const;
    const SequenceType& get_reference_allele_sequence() const noexcept;
    const SequenceType& get_alternative_allele_sequence() const noexcept;
    
private:
    // WARNING: Do not change the order of these members! Order required by second constructor.
    // We just store the sequences and a single GenomicRegion rather than two Allele's to save
    // storing a redundant GenomicRegion. The disadvantage of this is why need to constuct a new
    // Allele object on calls to get_*_allele().
    SequenceType reference_allele_sequence_;
    GenomicRegion reference_allele_region_;
    SequenceType alternative_allele_sequence_;
};

template <typename GenomicRegion_, typename SequenceType1, typename SequenceType2>
Variant::Variant(GenomicRegion_&& reference_allele_region, SequenceType1&& reference_allele,
                 SequenceType2&& alternative_allele)
:
reference_allele_sequence_ {std::forward<SequenceType1>(reference_allele)},
reference_allele_region_ {std::forward<GenomicRegion_>(reference_allele_region)},
alternative_allele_sequence_ {std::forward<SequenceType2>(alternative_allele)}
{}

template <typename SequenceType1, typename SequenceType2, typename SequenceType3>
Variant::Variant(SequenceType1&& reference_contig_name, SizeType reference_begin,
                 SequenceType2&& reference_allele, SequenceType3&& alternative_allele)
:
// The reference allele needs to be initialised first so it's size can be used for the
// region initialisation.
reference_allele_sequence_ {std::forward<SequenceType2>(reference_allele)},
reference_allele_region_ {GenomicRegion{std::forward<SequenceType1>(reference_contig_name),
    reference_begin, reference_begin + static_cast<SizeType>(reference_allele_sequence_.size())}},
alternative_allele_sequence_ {std::forward<SequenceType3>(alternative_allele)}
{}

inline bool operator==(const Variant& lhs, const Variant& rhs)
{
    return lhs.get_region() == rhs.get_region() &&
           lhs.get_reference_allele_sequence() == rhs.get_reference_allele_sequence() &&
           lhs.get_alternative_allele_sequence() == rhs.get_alternative_allele_sequence();
}

inline bool operator<(const Variant& lhs, const Variant& rhs)
{
    if (lhs.get_region() == rhs.get_region()) { // This check is required for consistency with operator==
        return (lhs.get_reference_allele_sequence() < rhs.get_reference_allele_sequence()) ? true :
                    lhs.get_alternative_allele_sequence() < rhs.get_alternative_allele_sequence();
    } else {
        return lhs.get_region() < rhs.get_region();
    }
}

namespace std {
    template <> struct hash<Variant>
    {
        size_t operator()(const Variant& v) const
        {
            size_t seed {};
            boost::hash_combine(seed, hash<GenomicRegion>()(v.get_region()));
            boost::hash_combine(seed, hash<Allele::SequenceType>()(v.get_reference_allele_sequence()));
            boost::hash_combine(seed, hash<Allele::SequenceType>()(v.get_alternative_allele_sequence()));
            return seed;
        }
    };
}

std::ostream& operator<<(std::ostream& os, const Variant& a_variant);

#endif
