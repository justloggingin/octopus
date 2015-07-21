//
//  program_options.h
//  Octopus
//
//  Created by Daniel Cooke on 27/02/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#ifndef __Octopus__program_options__
#define __Octopus__program_options__

#include <string>
#include <vector>
#include <boost/program_options.hpp>

#include "common.h"

class ReferenceGenome;
class GenomicRegion;

namespace po = boost::program_options;

namespace Octopus
{
    std::pair<po::variables_map, bool> parse_options(int argc, char** argv);
    
    SearchRegions get_search_regions(const po::variables_map& options, const ReferenceGenome& the_reference);
    
    std::vector<std::string> get_read_paths(const po::variables_map& options);
    
} // end namespace Octopus

#endif /* defined(__Octopus__program_options__) */
