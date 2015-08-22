//
//  program_options.cpp
//  Octopus
//
//  Created by Daniel Cooke on 27/02/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#include "program_options.h"

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <iterator>
#include <algorithm>  // std::transform, std::min
#include <functional> // std::function
#include <unordered_map>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

#include "genomic_region.h"
#include "reference_genome_factory.h"
#include "reference_genome.h"
#include "read_manager.h"
#include "mappable_algorithms.h"
#include "string_utils.h"

namespace fs = boost::filesystem;

namespace Octopus
{
    std::pair<po::variables_map, bool> parse_options(int argc, const char** argv)
    {
        try {
            po::positional_options_description p;
            p.add("command", -1);
            
            po::options_description general("General options");
            general.add_options()
            ("help,h", "produce help message")
            ("version", "output the version number")
            ("verbosity", po::value<unsigned>()->default_value(0), "level of logging. Verbosity 0 switches off logging")
            ;
            
            po::options_description backend("Backend options");
            backend.add_options()
            ("max-threads,t", po::value<unsigned>(), "the maximum number of threads allowed")
            ("compress-reads", po::value<bool>()->default_value(false), "compress the reads (slower)")
            ("max-open-files", po::value<unsigned>()->default_value(20), "the maximum number of files that can be open at one time")
            ;
            
            po::options_description input("Input/output options");
            input.add_options()
            ("reference,R", po::value<std::string>()->required(), "the reference genome file")
            ("reads,I", po::value<std::vector<std::string>>()->multitoken(), "space-seperated list of read file paths")
            ("reads-file", po::value<std::string>(), "path to a text file containing read file paths")
            ("regions,R", po::value<std::vector<std::string>>()->multitoken(), "space-seperated list of one-indexed variant search regions (chrom:begin-end)")
            ("regions-file,R", po::value<std::string>(), "path to a file containing list of one-indexed variant search regions (chrom:begin-end)")
            ("skip-regions", po::value<std::vector<std::string>>()->multitoken(), "space-seperated list of one-indexed regions (chrom:begin-end) to skip")
            ("skip-regions-file", po::value<std::string>(), "path to a file containing list of one-indexed regions (chrom:begin-end) to skip")
            ("known-variants", po::value<std::string>(), "variant file path containing known variants. These variants will automatically become candidates")
            ("output,o", po::value<std::string>(), "path of the output variant file")
            ("log-file", po::value<std::string>(), "path of the output log file")
            ;
            
            po::options_description filters("Read filter options");
            filters.add_options()
            ("min-mapping-quality", po::value<unsigned>()->default_value(20), "reads with smaller mapping quality are ignored")
            ("remove-duplicates", po::value<bool>()->default_value(false), "removes duplicate reads")
            ("trim-soft-clipped", po::value<bool>()->default_value(false), "trims soft clipped parts of the read")
            ("trim-flanks", po::value<bool>()->default_value(false), "trims the flanks of all reads")
            ("trim-adapters", po::value<bool>()->default_value(true), "trims any overlapping regions that pass the fragment size")
            ;
            
            po::options_description candidates("Candidate generation options");
            candidates.add_options()
            ("candidates-from-alignments", po::value<bool>()->default_value(true), "generate candidate variants from the aligned reads")
            ("candidates-from-assembler", po::value<bool>()->default_value(true), "generate candidate variants with the assembler")
            ("min-base-quality", po::value<unsigned>()->default_value(15), "only base changes with quality above this value are considered for snp generation")
            ("k", po::value<unsigned>()->default_value(15), "k-mer size to use")
            ("no-cycles", po::value<bool>()->default_value(false), "dissalow cycles in assembly graph")
            ;
            
            po::options_description model("Model options");
            model.add_options()
            ("ploidy", po::value<unsigned>()->default_value(2), "the organism ploidy")
            ("snp-prior", po::value<double>()->default_value(0.003), "the prior probability of a snp")
            ("insertion-prior", po::value<double>()->default_value(0.003), "the prior probability of an insertion into the reference")
            ("deletion-prior", po::value<double>()->default_value(0.003), "the prior probability of a deletion from the reference")
            ;
            
            po::options_description calling("Caller options");
            calling.add_options()
            ("min-posterior", po::value<unsigned>()->default_value(15), "the minimum variant posterior probability")
            ;
            
            po::options_description all("Allowed options");
            all.add(general).add(backend).add(input).add(filters).add(candidates).add(model).add(calling);
            
            po::variables_map vm;
            po::store(po::command_line_parser(argc, argv).options(all).positional(p).run(), vm);
            
            if (vm.count("help")) {
                std::cout << "Usage: octopus <command> [options]" << std::endl;
                std::cout << all << std::endl;
                return {vm, false};
            }
            
            // boost::option cannot handle option dependencies so we must do our own checks
            
            if (vm.count("reads") == 0 && vm.count("reads-file") == 0) {
                throw boost::program_options::required_option {"--reads | --reads-file"};
            }
            
            po::notify(vm);
            
            return {vm, true};
        }
        catch(std::exception& e) {
            std::cerr << e.what() << std::endl;
            return {po::variables_map {}, false};
        }
        catch(...) {
            std::cerr << "unknown error in option parsing" << std::endl;
            return {po::variables_map {}, false};;
        }
    }
    
    namespace detail
    {
        bool is_region_file_path(const std::string& region_option)
        {
            return fs::native(region_option);
        }
        
        struct Line
        {
            std::string line_data;
            
            operator std::string() const
            {
                return line_data;
            }
        };
        
        std::istream& operator>>(std::istream& str, Line& data)
        {
            std::getline(str, data.line_data);
            return str;
        }
        
        std::string to_region_format(const std::string& bed_line)
        {
            auto tokens = split(bed_line, '\t');
            
            switch (tokens.size()) {
                case 0:
                    throw std::runtime_error {"Empty line in input region bed file"};
                case 1:
                    return std::string {tokens[0]};
                case 2:
                    // Assume this represents a half range rather than a point
                    return std::string {tokens[0] + ':' + tokens[1] + '-'};
                default:
                    return std::string {tokens[0] + ':' + tokens[1] + '-' + tokens[2]};
            }
        }
        
        std::function<GenomicRegion(std::string)> get_line_parser(const fs::path& the_region_path,
                                                                  const ReferenceGenome& the_reference)
        {
            if (the_region_path.extension().string() == ".bed") {
                return [&the_reference] (const std::string& line) {
                    return parse_region(detail::to_region_format(line), the_reference);
                };
            } else {
                return [&the_reference] (const std::string& line) {
                    return parse_region(line, the_reference);
                };
            }
        }
        
        std::vector<GenomicRegion> get_regions_from_file(const std::string& file_path, const ReferenceGenome& the_reference)
        {
            std::vector<GenomicRegion> result {};
            
            fs::path the_path {file_path};
            
            if (!fs::exists(the_path)) {
                throw std::runtime_error {"cannot find given region file " + the_path.string()};
            }
            
            std::ifstream the_file {the_path.string()};
            
            std::transform(std::istream_iterator<Line>(the_file), std::istream_iterator<Line>(),
                           std::back_inserter(result), get_line_parser(the_path, the_reference));
            
            return result;
        }
        
        SearchRegions make_search_regions(const std::vector<GenomicRegion>& regions)
        {
            SearchRegions contig_mapped_regions {};
            
            for (const auto& region : regions) {
                contig_mapped_regions[region.get_contig_name()].insert(region);
            }
            
            SearchRegions result {};
            
            for (auto& contig_regions : contig_mapped_regions) {
                auto covered_contig_regions = get_covered_regions(std::cbegin(contig_regions.second),
                                                                  std::cend(contig_regions.second));
                result[contig_regions.first].insert(std::make_move_iterator(std::begin(covered_contig_regions)),
                                                    std::make_move_iterator(std::end(covered_contig_regions)));
            }
            
            return result;
        }
        
        SearchRegions get_all_regions_not_skipped(const ReferenceGenome& the_reference, std::vector<GenomicRegion>& skip_regions)
        {
            if (skip_regions.empty()) {
                return make_search_regions(get_all_contig_regions(the_reference));
            } else {
                auto skipped = make_search_regions(skip_regions);
                
                SearchRegions result {};
                
                return result;
            }
        }
        
        std::vector<std::string> get_read_paths_file(const std::string& file_path)
        {
            std::vector<std::string> result {};
            
            fs::path the_path {file_path};
            
            if (!fs::exists(the_path)) {
                throw std::runtime_error {"cannot find given read path file " + the_path.string()};
            }
            
            std::ifstream the_file {the_path.string()};
            
            std::transform(std::istream_iterator<Line>(the_file), std::istream_iterator<Line>(),
                           std::back_inserter(result), [] (const Line& line) { return line.line_data; });
            
            return result;
        }
    } // end namespace detail
    
    unsigned get_num_threads(const po::variables_map& options)
    {
        return options.at("num_threads").as<unsigned>();
    }
    
    ReferenceGenome get_reference(const po::variables_map& options)
    {
        return make_reference(options.at("reference").as<std::string>());
    }
    
    SearchRegions get_search_regions(const po::variables_map& options, const ReferenceGenome& the_reference)
    {
        std::vector<GenomicRegion> input_regions {};
        
        if (options.count("regions") == 0 && options.count("regions-file") == 0) {
            std::vector<GenomicRegion> skip_regions {};
            
            if (options.count("skip-regions") != 0) {
                const auto& regions = options.at("skip-regions").as<std::vector<std::string>>();
                skip_regions.reserve(skip_regions.size());
                std::transform(std::cbegin(regions), std::cend(regions), std::back_inserter(skip_regions),
                               [&the_reference] (const auto& region) {
                                   return parse_region(region, the_reference);
                               });
            }
            
            if (options.count("skip-regions-file") != 0) {
                const auto& skip_path = options.at("skip-regions-file").as<std::string>();
                auto skip_regions_from_file = detail::get_regions_from_file(skip_path, the_reference);
                skip_regions.insert(skip_regions.end(), std::make_move_iterator(std::begin(skip_regions_from_file)),
                                    std::make_move_iterator(std::end(skip_regions_from_file)));
            }
            
            return detail::get_all_regions_not_skipped(the_reference, skip_regions);
        } else {
            if (options.count("regions") != 0) {
                const auto& regions = options.at("regions").as<std::vector<std::string>>();
                input_regions.reserve(regions.size());
                std::transform(std::cbegin(regions), std::cend(regions), std::back_inserter(input_regions),
                               [&the_reference] (const auto& region) {
                                   return parse_region(region, the_reference);
                               });
            }
            
            if (options.count("regions-file") != 0) {
                const auto& regions_path = options.at("regions-file").as<std::string>();
                auto regions_from_file = detail::get_regions_from_file(regions_path, the_reference);
                input_regions.insert(input_regions.end(), std::make_move_iterator(std::begin(regions_from_file)),
                                    std::make_move_iterator(std::end(regions_from_file)));
            }
        }
        
        return detail::make_search_regions(input_regions);
    }
    
    std::vector<fs::path> get_read_paths(const po::variables_map& options)
    {
        std::vector<fs::path> result {};
        
        if (options.count("reads") != 0) {
            const auto& read_paths = options.at("reads").as<std::vector<std::string>>();
            result.insert(result.end(), std::cbegin(read_paths), std::cend(read_paths));
        }
        
        if (options.count("reads-file") != 0) {
            const auto& read_file_path = options.at("reads-file").as<std::string>();
            auto regions_from_file = detail::get_read_paths_file(read_file_path);
            result.insert(result.end(), std::make_move_iterator(std::begin(regions_from_file)),
                          std::make_move_iterator(std::end(regions_from_file)));
        }
        
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        
        return result;
    }
    
    ReadManager get_read_manager(const po::variables_map& options)
    {
        auto read_paths     = get_read_paths(options);
        auto max_open_files = options.at("max-open-files").as<unsigned>();
        
        return ReadManager {std::move(read_paths), max_open_files};
    }
} // end namespace Octopus
