//
//  fixed_ploidy_genotype_likelihood_model.cpp
//  Octopus
//
//  Created by Daniel Cooke on 01/04/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#include "fixed_ploidy_genotype_likelihood_model.hpp"

#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <array>
#include <limits>

#include "maths.hpp"

#include <iostream> // TEST

namespace octopus
{
namespace model
{
    static constexpr auto ln(const unsigned n)
    {
        using T = double;
        
        constexpr std::array<T, 11> Ln
        {
            std::numeric_limits<T>::infinity(),
            0.0,
            0.693147180559945309417232121458176568075500134360255254120,
            1.098612288668109691395245236922525704647490557822749451734,
            1.386294361119890618834464242916353136151000268720510508241,
            1.609437912434100374600759333226187639525601354268517721912,
            1.791759469228055000812477358380702272722990692183004705855,
            1.945910149055313305105352743443179729637084729581861188459,
            2.079441541679835928251696364374529704226500403080765762362,
            2.197224577336219382790490473845051409294981115645498903469,
            2.302585092994045684017991454684364207601101488628772976033
        };
        return Ln[n];
    }
    
    FixedPloidyGenotypeLikelihoodModel::FixedPloidyGenotypeLikelihoodModel(unsigned ploidy,
                                                                           const HaplotypeLikelihoodCache& haplotype_likelihoods)
    :
    haplotype_likelihoods_ {haplotype_likelihoods},
    ploidy_ {ploidy}
    {}
    
    // ln p(read | genotype)  = ln sum {haplotype in genotype} p(read | haplotype) - ln ploidy
    // ln p(reads | genotype) = sum {read in reads} ln p(read | genotype)
    double FixedPloidyGenotypeLikelihoodModel::log_likelihood(const SampleName& sample,
                                                              const Genotype<Haplotype>& genotype) const
    {
        // These cases are just for optimisation
        switch (ploidy_) {
            case 1:
                return log_likelihood_haploid(sample, genotype);
            case 2:
                return log_likelihood_diploid(sample, genotype);
            case 3:
                return log_likelihood_triploid(sample, genotype);
            case 4:
                return log_likelihood_polyploid(sample, genotype);
                //return log_likelihood_tetraploid(sample, genotype);
            default:
                return log_likelihood_polyploid(sample, genotype);
        }
    }
    
    double FixedPloidyGenotypeLikelihoodModel::log_likelihood_haploid(const SampleName& sample,
                                                                      const Genotype<Haplotype>& genotype) const
    {
        const auto& log_likelihoods = haplotype_likelihoods_.get()(sample, genotype[0]);
        return std::accumulate(std::cbegin(log_likelihoods), std::cend(log_likelihoods), 0.0);
    }
    
    double FixedPloidyGenotypeLikelihoodModel::log_likelihood_diploid(const SampleName& sample,
                                                                      const Genotype<Haplotype>& genotype) const
    {
        const auto& log_likelihoods1 = haplotype_likelihoods_.get()(sample, genotype[0]);
        
        if (genotype.is_homozygous()) {
            return std::accumulate(std::cbegin(log_likelihoods1), std::cend(log_likelihoods1), 0.0);
        }
        
        const auto& log_likelihoods2 = haplotype_likelihoods_.get()(sample, genotype[1]);
        
        return std::inner_product(std::cbegin(log_likelihoods1), std::cend(log_likelihoods1),
                                  std::cbegin(log_likelihoods2), 0.0, std::plus<> {},
                                  [this] (const auto a, const auto b) -> double {
                                      return Maths::log_sum_exp(a, b) - ln(2);
                                  });
    }
    
    double FixedPloidyGenotypeLikelihoodModel::log_likelihood_triploid(const SampleName& sample,
                                                                       const Genotype<Haplotype>& genotype) const
    {
        using std::cbegin; using std::cend;
        
        const auto& log_likelihoods1 = haplotype_likelihoods_.get()(sample, genotype[0]);
        
        if (genotype.is_homozygous()) {
            return std::accumulate(cbegin(log_likelihoods1), cend(log_likelihoods1), 0.0);
        }
        
        if (genotype.zygosity() == 3) {
            const auto& log_likelihoods2 = haplotype_likelihoods_.get()(sample, genotype[1]);
            const auto& log_likelihoods3 = haplotype_likelihoods_.get()(sample, genotype[2]);
            return Maths::inner_product(cbegin(log_likelihoods1), cend(log_likelihoods1),
                                        cbegin(log_likelihoods2), cbegin(log_likelihoods3),
                                        0.0, std::plus<> {},
                                        [this] (const auto a, const auto b, const auto c) -> double {
                                            return Maths::log_sum_exp(a, b, c) - ln(3);
                                        });
        }
        
        static const double ln2 {std::log(2)};
        
        if (genotype[0] != genotype[1]) {
            const auto& log_likelihoods2 = haplotype_likelihoods_.get()(sample, genotype[1]);
            return std::inner_product(cbegin(log_likelihoods1), cend(log_likelihoods1),
                                      cbegin(log_likelihoods2), 0.0, std::plus<> {},
                                      [this] (const auto a, const auto b) -> double {
                                          return Maths::log_sum_exp(a, ln2 + b) - ln(3);
                                      });
        }
        
        const auto& log_likelihoods3 = haplotype_likelihoods_.get()(sample, genotype[2]);
        
        return std::inner_product(cbegin(log_likelihoods1), cend(log_likelihoods1),
                                  cbegin(log_likelihoods3), 0.0, std::plus<> {},
                                  [this] (const auto a, const auto b) -> double {
                                      return Maths::log_sum_exp(ln2 + a, b) - ln(3);
                                  });
    }
    
    double FixedPloidyGenotypeLikelihoodModel::log_likelihood_tetraploid(const SampleName& sample,
                                                                         const Genotype<Haplotype>& genotype) const
    {
        const auto z = genotype.zygosity();
        
        const auto& log_likelihoods1 = haplotype_likelihoods_.get()(sample, genotype[0]);
        
        if (z == 1) {
            return std::accumulate(std::cbegin(log_likelihoods1), std::cend(log_likelihoods1), 0.0);
        }
        
        if (z == 4) {
            const auto& log_likelihoods2 = haplotype_likelihoods_.get()(sample, genotype[1]);
            const auto& log_likelihoods3 = haplotype_likelihoods_.get()(sample, genotype[2]);
            const auto& log_likelihoods4 = haplotype_likelihoods_.get()(sample, genotype[3]);
            return Maths::inner_product(std::cbegin(log_likelihoods1), std::cend(log_likelihoods1),
                                        std::cbegin(log_likelihoods2), std::cbegin(log_likelihoods3),
                                        std::cbegin(log_likelihoods4), 0.0, std::plus<> {},
                                        [this] (const auto a, const auto b, const auto c, const auto d) -> double {
                                            return Maths::log_sum_exp({a, b, c, d}) - ln(4);
                                        });
        }
        
        // TODO
        
        return 0;
    }
    
    double FixedPloidyGenotypeLikelihoodModel::log_likelihood_polyploid(const SampleName& sample,
                                                                        const Genotype<Haplotype>& genotype) const
    {
        const auto z = genotype.zygosity();
        
        const auto& log_likelihoods1 = haplotype_likelihoods_.get()(sample, genotype[0]);
        
        if (z == 1) {
            return std::accumulate(std::cbegin(log_likelihoods1), std::cend(log_likelihoods1), 0.0);
        }
        
        if (z == 2) {
            static const double lnpm1 {std::log(ploidy_ - 1)};
            
            const auto unique_haplotypes = genotype.copy_unique_ref();
            
            const auto& log_likelihoods2 = haplotype_likelihoods_.get()(sample, unique_haplotypes.back());
            
            if (genotype.count(unique_haplotypes.front()) == 1) {
                return std::inner_product(std::cbegin(log_likelihoods1), std::cend(log_likelihoods1),
                                          std::cbegin(log_likelihoods2), 0.0, std::plus<> {},
                                          [this] (const auto a, const auto b) -> double {
                                              return Maths::log_sum_exp(a, lnpm1 + b) - ln(ploidy_);
                                          });
            }
            
            return std::inner_product(std::cbegin(log_likelihoods1), std::cend(log_likelihoods1),
                                      std::cbegin(log_likelihoods2), 0.0, std::plus<> {},
                                      [this] (const auto a, const auto b) -> double {
                                          return Maths::log_sum_exp(lnpm1 + a, b) - ln(ploidy_);
                                      });
        }
        
        std::vector<HaplotypeLikelihoodCache::LikelihoodVectorRef> log_likelihoods {};
        log_likelihoods.reserve(ploidy_);
        
        std::transform(std::cbegin(genotype), std::cend(genotype), std::back_inserter(log_likelihoods),
                       [this, &sample] (const auto& haplotype)
                            -> const HaplotypeLikelihoodCache::LikelihoodVector& {
                           return haplotype_likelihoods_.get()(sample, haplotype);
                       });
        
        std::vector<double> tmp(ploidy_);
        
        double result {0};
        
        const auto num_likelihoods = log_likelihoods.front().get().size();
        
        for (std::size_t i {0}; i < num_likelihoods; ++i) {
            std::transform(std::cbegin(log_likelihoods), std::cend(log_likelihoods), std::begin(tmp),
                           [i] (const auto& haplotype_likelihoods) {
                               return haplotype_likelihoods.get()[i];
                           });
            
            result += Maths::log_sum_exp(tmp) - ln(ploidy_);
        }
        
        return result;
    }
}
} // namespace octopus
