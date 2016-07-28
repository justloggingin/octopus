//
//  population_caller.hpp
//  Octopus
//
//  Created by Daniel Cooke on 15/09/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#ifndef __Octopus__population_caller__
#define __Octopus__population_caller__

#include <vector>
#include <string>
#include <memory>
#include <typeindex>

#include "common.hpp"
#include "variant_caller.hpp"
#include "population_genotype_model.hpp"
#include "variant_call.hpp"
#include "phred.hpp"

class GenomicRegion;
class ReadPipe;
class Variant;
class HaplotypeLikelihoodCache;

namespace octopus
{
class PopulationVariantCaller : public VariantCaller
{
public:
    using VariantCaller::CallTypeSet;
    using VariantCaller::CallerComponents;
    
    struct CallerParameters
    {
        Phred<double> min_variant_posterior;
        Phred<double> min_refcall_posterior;
        unsigned ploidy;
    };
    
    PopulationVariantCaller() = delete;
    
    PopulationVariantCaller(CallerComponents&& components, VariantCaller::CallerParameters general_parameters,
                            CallerParameters specific_parameters);
    
    PopulationVariantCaller(const PopulationVariantCaller&)            = delete;
    PopulationVariantCaller& operator=(const PopulationVariantCaller&) = delete;
    PopulationVariantCaller(PopulationVariantCaller&&)                 = delete;
    PopulationVariantCaller& operator=(PopulationVariantCaller&&)      = delete;
    
    ~PopulationVariantCaller() = default;
    
private:
    class Latents : public CallerLatents
    {
    public:
        using ModelInferences = model::Population::InferredLatents;
        
        using CallerLatents::HaplotypeProbabilityMap;
        using CallerLatents::GenotypeProbabilityMap;
        
        friend PopulationVariantCaller;
        
        explicit Latents(const std::vector<SampleName>& samples,
                         const std::vector<Haplotype>&,
                         std::vector<Genotype<Haplotype>>&& genotypes,
                         ModelInferences&&);
        explicit Latents(const std::vector<SampleName>& samples,
                         const std::vector<Haplotype>&,
                         std::vector<Genotype<Haplotype>>&& genotypes,
                         ModelInferences&&, ModelInferences&&);
        
        std::shared_ptr<HaplotypeProbabilityMap> haplotype_posteriors() const noexcept override;
        std::shared_ptr<GenotypeProbabilityMap> genotype_posteriors() const noexcept override;
        
    private:
        std::shared_ptr<GenotypeProbabilityMap> genotype_posteriors_;
        std::shared_ptr<HaplotypeProbabilityMap> haplotype_posteriors_;
        
        boost::optional<ModelInferences> dummy_latents_;
        
        double model_log_evidence_;
        
        HaplotypeProbabilityMap
        calculate_haplotype_posteriors(const std::vector<Haplotype>& haplotypes);
    };
    
    CallerParameters parameters_;
    
    CallTypeSet do_get_call_types() const override;
    
    std::unique_ptr<CallerLatents>
    infer_latents(const std::vector<Haplotype>& haplotypes,
                  const HaplotypeLikelihoodCache& haplotype_likelihoods) const override;
    
    std::vector<std::unique_ptr<VariantCall>>
    call_variants(const std::vector<Variant>& candidates, const CallerLatents& latents) const override;
    
    std::vector<std::unique_ptr<VariantCall>>
    call_variants(const std::vector<Variant>& candidates, const Latents& latents) const;
    
    std::vector<std::unique_ptr<ReferenceCall>>
    call_reference(const std::vector<Allele>& alleles, const CallerLatents& latents,
                   const ReadMap& reads) const override;
};
} // namespace octopus

#endif /* defined(__Octopus__population_caller__) */
