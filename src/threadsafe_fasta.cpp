//
//  threadsafe_fasta.cpp
//  Octopus
//
//  Created by Daniel Cooke on 17/08/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#include "threadsafe_fasta.h"

#include "genomic_region.h"

ThreadsafeFasta::ThreadsafeFasta(fs::path fasta_path)
:
fasta_ {std::move(fasta_path)},
fasta_mutex_ {}
{}

ThreadsafeFasta::ThreadsafeFasta(fs::path fasta_path, fs::path fasta_index_path)
:
fasta_ {std::move(fasta_path), std::move(fasta_index_path)},
fasta_mutex_ {}
{}

std::string ThreadsafeFasta::get_reference_name() const
{
    return fasta_.get_reference_name(); // don't need mutex as const
}

std::vector<std::string> ThreadsafeFasta::get_contig_names()
{
    std::lock_guard<std::mutex> lock {fasta_mutex_};
    return fasta_.get_contig_names();
}

ThreadsafeFasta::SizeType ThreadsafeFasta::get_contig_size(const std::string& contig_name)
{
    std::lock_guard<std::mutex> lock {fasta_mutex_};
    return fasta_.get_contig_size(contig_name);
}

ThreadsafeFasta::SequenceType ThreadsafeFasta::get_sequence(const GenomicRegion& region)
{
    std::lock_guard<std::mutex> lock {fasta_mutex_};
    return fasta_.get_sequence(region);
}
