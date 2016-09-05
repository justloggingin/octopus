// Copyright (c) 2016 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef vcf_extractor_hpp
#define vcf_extractor_hpp

#include <vector>
#include <memory>

#include <boost/filesystem.hpp>

#include "io/variant/vcf_reader.hpp"
#include "core/types/variant.hpp"
#include "variant_generator.hpp"

namespace octopus {

class GenomicRegion;

namespace coretools {

class VcfExtractor : public VariantGenerator
{
public:
    struct Options
    {
        Variant::MappingDomain::Size max_variant_size = 100;
    };
    
    VcfExtractor() = delete;
    
    VcfExtractor(std::unique_ptr<const VcfReader> reader, Options options);
        
    VcfExtractor(const VcfExtractor&)            = default;
    VcfExtractor& operator=(const VcfExtractor&) = default;
    VcfExtractor(VcfExtractor&&)                 = default;
    VcfExtractor& operator=(VcfExtractor&&)      = default;
    
    ~VcfExtractor() override = default;
    
private:
    std::unique_ptr<VariantGenerator> do_clone() const override;
    
    std::vector<Variant> do_generate_variants(const GenomicRegion& region) override;
    
    std::string name() const override;
    
    std::shared_ptr<const VcfReader> reader_;
    Options options_;
};

} // namespace coretools
} // namespace octopus

#endif
