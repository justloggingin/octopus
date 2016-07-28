//
//  measure.hpp
//  Octopus
//
//  Created by Daniel Cooke on 15/07/2016.
//  Copyright © 2016 Oxford University. All rights reserved.
//

#ifndef measure_hpp
#define measure_hpp

#include <string>
#include <memory>
#include <utility>

class VcfRecord;

namespace octopus { namespace CallFiltering
{
    class Measure
    {
    public:
        virtual double operator()(const VcfRecord& call) const = 0;
        virtual std::string name() const = 0;
    };
    
    class MeasureWrapper
    {
    public:
        MeasureWrapper() = delete;
        
        MeasureWrapper(std::unique_ptr<Measure> measure) : measure_ {std::move(measure)} {}
        
        MeasureWrapper(const MeasureWrapper&)            = delete;
        MeasureWrapper& operator=(const MeasureWrapper&) = delete;
        MeasureWrapper(MeasureWrapper&&)                 = default;
        MeasureWrapper& operator=(MeasureWrapper&&)      = default;
        
        ~MeasureWrapper() = default;
        
        auto operator()(const VcfRecord& call) const { return (*measure_)(call); }
        std::string name() const { return measure_->name(); }
    private:
        std::unique_ptr<Measure> measure_;
    };
    
    template <typename M, typename... Args>
    MeasureWrapper make_wrapped_measure(Args&&... args)
    {
        return MeasureWrapper {std::make_unique<M>(std::forward<Args>(args)...)};
    }
} // namespace CallFiltering
} // namespace octopus

#endif /* measure_hpp */
