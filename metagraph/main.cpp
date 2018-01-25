#include <fstream>
#include <ctime>
#include <zlib.h>

#include "dbg_succinct.hpp"
#include "dbg_succinct_chunk.hpp"
#include "dbg_succinct_construct.hpp"
#include "config.hpp"
#include "helpers.hpp"
#include "utils.hpp"
#include "vcf_parser.hpp"
#include "traverse.hpp"
#include "dbg_succinct_merge.hpp"
#include "annotate.hpp"
#include "unix_tools.hpp"
#include "kmer.hpp"


KSEQ_INIT(gzFile, gzread)


struct ParallelAnnotateContainer {
    kstring_t *seq;
    kstring_t *label;
    DBG_succ *graph;
    Config *config;
    uint64_t idx;
    uint64_t binsize;
    uint64_t total_bins;
    //pthread_mutex_t* anno_mutex;
};


const std::vector<std::string> annots = {
  "AC_AFR", "AC_EAS", "AC_AMR", "AC_ASJ",
  "AC_FIN", "AC_NFE", "AC_SAS", "AC_OTH"
};


/*
 * Distribute the annotation of all k-mers in a sequence over
 * a number of parallel bins.
 */
// void* parallel_annotate_wrapper(void *) {

//     uint64_t curr_idx, start, end;

//     while (true) {
//         pthread_mutex_lock (&mutex_bin_idx);
//         if (anno_data->idx == anno_data->total_bins) {
//             pthread_mutex_unlock (&mutex_bin_idx);
//             break;
//         } else {
//             curr_idx = anno_data->idx;
//             anno_data->idx++;
//             pthread_mutex_unlock (&mutex_bin_idx);

//             start = curr_idx * anno_data->binsize;
//             //end = std::min(((curr_idx + 1) * anno_data->binsize) + anno_data->graph->k - 1, anno_data->seq->l);
//             end = std::min((curr_idx + 1) * anno_data->binsize,
//                            static_cast<uint64_t>(anno_data->seq->l));
//             //std::cerr << "start " << start << " end " << end << std::endl;
//             annotate::annotate_seq(anno_data->graph,
//                                    anno_data->config,
//                                    *(anno_data->seq),
//                                    *(anno_data->label),
//                                    start, end, &mutex_annotate);
//         }
//     }
//     pthread_exit((void*) 0);
// }


DBG_succ* load_critical_graph_from_file(const std::string &filename) {
    std::string filetype = ".dbg";
    std::string filebase =
        std::equal(filetype.rbegin(), filetype.rend(), filename.rbegin())
        ? filename.substr(0, filename.size() - filetype.size())
        : filename;

    DBG_succ *graph = new DBG_succ();
    if (!graph->load(filebase)) {
        std::cerr << "ERROR: input file "
                  << filename << " corrupted" << std::endl;
        delete graph;
        exit(1);
    }
    return graph;
}


int main(int argc, const char *argv[]) {

    Timer timer;

    // parse command line arguments and options
    std::unique_ptr<Config> config(new Config(argc, argv));

    if (config->verbose)
        std::cout << "Welcome to MetaGraph" << std::endl;

    // create graph pointer
    DBG_succ *graph = NULL;

    const auto &files = config->fname;

    switch (config->identity) {
        //TODO: allow for building by appending/adding to an existing graph
        case Config::BUILD: {
            graph = new DBG_succ(config->k);

            if (config->verbose)
                std::cerr << "k is " << graph->get_k() << std::endl;

            if (config->fast) {
                //enumerate all suffices
                assert(DBG_succ::alph_size > 1);
                size_t suffix_len = std::min(
                    static_cast<uint64_t>(std::ceil(std::log2(config->nsplits)
                                                    / std::log2(DBG_succ::alph_size - 1))),
                    graph->get_k() - 1
                );
                auto suffices = utils::generate_strings(
                    DBG_succ::alphabet.substr(0, DBG_succ::alph_size),
                    suffix_len
                );

                DBG_succ::VectorChunk graph_data;

                //one pass per suffix
                for (const std::string &suffix : suffices) {
                    if (suffix.size())
                        std::cout << "Suffix: " << suffix << std::endl;

                    std::cout << "Start reading data and extracting k-mers..." << std::endl;

                    //add sink nodes
                    // graph->add_sink(config->parallel, suffix);

                    std::unique_ptr<KMerDBGSuccChunkConstructor> constructor(
                        new KMerDBGSuccChunkConstructor(graph->get_k(),
                                                        suffix, config->parallel)
                    );

                    // iterate over input files
                    for (unsigned int f = 0; f < files.size(); ++f) {
                        if (config->verbose) {
                            std::cout << std::endl << "Parsing " << files[f] << std::endl;
                        }

                        if (utils::get_filetype(files[f]) == "VCF") {
                            if (suffix.find('$') != std::string::npos)
                                continue;

                            //READ FROM VCF
                            uint64_t nbp = 0;
                            uint64_t nbplast = 0;

                            Timer data_reading_timer;

                            vcf_parser vcf;
                            if (!vcf.init(config->refpath, files[f], graph->get_k())) {
                                std::cerr << "ERROR reading VCF " << files[f] << std::endl;
                                exit(1);
                            }
                            std::cerr << "Loading VCF with " << config->parallel
                                                             << " threads per line\n";
                            std::string sequence;
                            std::string annotation;
                            for (size_t i = 1; vcf.get_seq(annots, &sequence, &annotation); ++i) {
                                if (i % 10'000 == 0) {
                                    std::cout << "." << std::flush;
                                    if (i % 100'000 == 0) {
                                        fprintf(stdout,
                                            "%zu - bp %" PRIu64 " / runtime %f sec / BPph %" PRIu64 "\n",
                                            i,
                                            nbp,
                                            timer.elapsed(),
                                            static_cast<uint64_t>(60 * 60
                                                * (nbp - nbplast)
                                                / data_reading_timer.elapsed())
                                        );
                                        nbplast = nbp;
                                        data_reading_timer.reset();
                                    }
                                }
                                annotation = "VCF:" + annotation;
                                nbp += sequence.length();

                                constructor->add_read(sequence);
                            }
                        } else if (utils::get_filetype(files[f]) == "FASTA"
                                    || utils::get_filetype(files[f]) == "FASTQ") {
                            // open stream
                            gzFile input_p = gzopen(files[f].c_str(), "r");
                            if (input_p == Z_NULL) {
                                std::cerr << "ERROR no such file " << files[f] << std::endl;
                                exit(1);
                            }

                            //TODO: handle read_stream->qual
                            kseq_t *read_stream = kseq_init(input_p);
                            if (read_stream == NULL) {
                                std::cerr << "ERROR while opening input file "
                                          << files[f] << std::endl;
                                exit(1);
                            }
                            while (kseq_read(read_stream) >= 0) {

                                // possibly reverse k-mers
                                if (config->reverse)
                                    reverse_complement(read_stream->seq);

                                constructor->add_read(read_stream->seq.s);
                            }
                            kseq_destroy(read_stream);

                            gzclose(input_p);
                        } else {
                            std::cerr << "ERROR: Filetype unknown for file "
                                      << files[f] << std::endl;
                            exit(1);
                        }
                        //fprintf(stdout, "current mem usage: %lu MB\n", get_curr_mem() / (1<<20));
                    }
                    get_RAM();
                    std::cout << "Reading data finished\t" << timer.elapsed() << "sec" << std::endl;

                    std::cout << "Sorting kmers and appending succinct"
                              << " representation from current bin...\t" << std::flush;
                    timer.reset();
                    auto next_block = constructor->build_chunk();
                    graph_data.extend(*next_block);
                    delete next_block;

                    std::cout << timer.elapsed() << "sec" << std::endl;
                }
                graph_data.initialize_graph(graph);

                if (config->state == Config::DYN) {
                    std::cerr << "Converting static graph to dynamic...\t" << std::flush;
                    timer.reset();
                    graph->switch_state(Config::DYN);
                    std::cout << timer.elapsed() << "sec" << std::endl;
                }
            } else {
                //slower method
                //TODO: merge in optimizations from seqmerge branch
                //TODO: add VCF parsing (needs previous merge)
                for (unsigned int f = 0; f < files.size(); ++f) {
                    if (config->verbose) {
                        std::cout << std::endl << "Parsing " << files[f] << std::endl;
                    }
                    // open stream
                    if (utils::get_filetype(files[f]) == "VCF") {
                        std::cerr << "ERROR: this method of reading VCFs not yet implemented" << std::endl;
                        exit(1);
                    } else if (utils::get_filetype(files[f]) == "FASTA"
                                || utils::get_filetype(files[f]) == "FASTQ") {
                        gzFile input_p = gzopen(files[f].c_str(), "r");
                        if (input_p == Z_NULL) {
                            std::cerr << "ERROR no such file " << files[f] << std::endl;
                            exit(1);
                        }
                        kseq_t *read_stream = kseq_init(input_p);
                        if (read_stream == NULL) {
                            std::cerr << "ERROR while opening input file "
                                      << files[f] << std::endl;
                            exit(1);
                        }
                        for (size_t i = 1; kseq_read(read_stream) >= 0; ++i) {
                            if (config->reverse)
                                reverse_complement(read_stream->seq);
                            graph->add_sequence(read_stream->seq.s);
                        }
                        kseq_destroy(read_stream);
                        gzclose(input_p);
                    } else {
                        std::cerr << "ERROR: Filetype unknown for file "
                                  << files[f] << std::endl;
                        exit(1);
                    }
                }
            }

            graph->switch_state(config->state);
            config->infbase = config->outfbase;
            // graph->annotationToFile(config->infbase + ".anno.dbg");
            //graph->print_state();
            break;
        }
        case Config::ANNOTATE: {
           //  // load graph
           //  graph = new DBG_succ(config->infbase);

           //  // load annotation (if file does not exist, empty annotation is created)
           //  // graph->annotationFromFile(config->infbase + ".anno.dbg");

           //  uint64_t total_seqs = 0;

           //  // iterate over input files
           //  for (unsigned int f = 0; f < files.size(); ++f) {

           //      if (config->verbose) {
           //          std::cout << std::endl << "Parsing " << files[f] << std::endl;
           //      }

           //      // open stream to fasta file
           //      gzFile input_p = gzopen(files[f].c_str(), "r");
           //      kseq_t *read_stream = kseq_init(input_p);

           //      if (read_stream == NULL) {
           //          std::cerr << "ERROR while opening input file " << files[f] << std::endl;
           //          exit(1);
           //      }

           //      while (kseq_read(read_stream) >= 0) {

           //          if (config->reverse)
           //              reverse_complement(read_stream->seq);

           //          if (config->parallel > 1) {
           //              pthread_t* threads = NULL;

           //              anno_data->seq = &(read_stream->seq);
           //              anno_data->label = &(read_stream->name);
           //              anno_data->graph = graph;
           //              anno_data->config = config;
           //              anno_data->idx = 0;
           //              anno_data->binsize = (read_stream->seq.l + 1) / (config->parallel * config->num_bins_per_thread);
           //              anno_data->total_bins = ((read_stream->seq.l + anno_data->binsize - 1) / anno_data->binsize);

           //              // create threads
           //              threads = new pthread_t[config->parallel];
           //              pthread_attr_init(&attr);
           //              pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

           //              // do the work
           //              for (size_t tid = 0; tid < config->parallel; tid++) {
           //                 pthread_create(&threads[tid], &attr, parallel_annotate_wrapper, (void *) tid);
           //                 //std::cerr << "starting thread " << tid << std::endl;
           //              }

           //              // join threads
           //              //if (config->verbose)
           //              //    std::cout << "Waiting for threads to join" << std::endl;
           //              for (size_t tid = 0; tid < config->parallel; tid++) {
           //                  pthread_join(threads[tid], NULL);
           //              }
           //              delete[] threads;

           //              total_seqs += 1;

           //              //if (config->verbose)
           //              //    std::cout << "added labels for " << total_seqs
           //              //              << " sequences, last was " << std::string(read_stream->name.s) << std::endl;
           //          } else {
           //              annotate::annotate_seq(graph, config, read_stream->seq, read_stream->name);
           //          }
           //          if (config->verbose) {
           //              //std::cout << "entries in annotation map: " << graph->combination_count << std::endl
           //              //          << "length of combination vector: " << graph->combination_vector.size() << std::endl;
           //              //std::cout << "added labels for " << total_seqs << " sequences, last was " << std::string(read_stream->name.s) << std::endl;
           //          }
           //      }
           //      kseq_destroy(read_stream);
           //      gzclose(input_p);
           //  }

           //  if (config->print_graph)
           //      annotate::annotationToScreen(graph);

           //  graph->annotationToFile(config->infbase + ".anno.dbg");
            break;
        }
        case Config::CLASSIFY: {
            // load graph
            graph = load_critical_graph_from_file(config->infbase);

            // load annotatioun (if file does not exist, empty annotation is created)
            // graph->annotationFromFile(config->infbase + ".anno.dbg");

            // iterate over input files
            for (unsigned int f = 0; f < files.size(); ++f) {
                if (config->verbose) {
                    std::cout << std::endl << "Parsing " << files[f] << std::endl;
                }

                // open stream to fasta file
                gzFile input_p = gzopen(files[f].c_str(), "r");
                if (input_p == Z_NULL) {
                    std::cerr << "ERROR no such file " << files[f] << std::endl;
                    exit(1);
                }
                kseq_t *read_stream = kseq_init(input_p);

                if (read_stream == NULL) {
                    std::cerr << "ERROR while opening input file " << files[f] << std::endl;
                    exit(1);
                }

                std::set<uint32_t> labels_fwd;
                std::set<uint32_t> labels_rev;
                while (kseq_read(read_stream) >= 0) {

                    std::cout << std::string(read_stream->name.s) << ": ";
                    // labels_fwd = annotate::classify_read(graph, read_stream->seq, config->distance);
                    // for (auto it = labels_fwd.begin(); it != labels_fwd.end(); it++)
                    //     std::cout << graph->id_to_label.at(*it + 1) << ":";
                    // std::cout << "\t";

                    reverse_complement(read_stream->seq);
                    // labels_rev = annotate::classify_read(graph, read_stream->seq, config->distance);
                    // for (auto it = labels_rev.begin(); it != labels_rev.end(); it++)
                    //     std::cout << graph->id_to_label.at(*it + 1) << ":";
                    // std::cout << std::endl;
                }
                kseq_destroy(read_stream);
                gzclose(input_p);
            }
            delete graph;
            graph = NULL;
            break;
        }
        case Config::COMPARE: {
            assert(files.size());
            std::cout << "Opening file " << files[0] << std::endl;
            graph = load_critical_graph_from_file(files[0]);

            for (size_t f = 1; f < files.size(); ++f) {
                std::cout << "Opening file for comparison ..." << files[f] << std::endl;
                DBG_succ *second = load_critical_graph_from_file(files[f]);
                if (*graph == *second) {
                    std::cout << "Graphs are identical" << std::endl;
                } else {
                    std::cout << "Graphs are not identical" << std::endl;
                }
                delete second;
            }
            delete graph;
            graph = NULL;
            break;
        }
        case Config::MERGE: {
            // collect results on an external merge
            if (config->collect > 1) {
                graph = merge::merge_chunks(
                    config->k,
                    std::vector<DBG_succ::Chunk*>(config->collect, NULL),
                    config->outfbase
                );
                break;
            }

            timer.reset();
            std::vector<const DBG_succ*> graphs;
            for (const auto &file : files) {
                std::cout << "Opening file " << file << std::endl;
                graphs.push_back(load_critical_graph_from_file(file));
                if (config->verbose) {
                    std::cout << "nodes: " << graphs.back()->num_nodes() << std::endl;
                    std::cout << "edges: " << graphs.back()->num_edges() << std::endl;
                    std::cout << "k: " << graphs.back()->get_k() << std::endl;
                }
            }
            std::cout << "Graphs are loaded\t" << timer.elapsed() << "sec" << std::endl;

            if (config->traversal_merge) {
                std::cout << "Start merging traversal" << std::endl;
                timer.reset();

                graph = const_cast<DBG_succ*>(graphs.at(0));
                graphs.erase(graphs.begin());

                for (size_t i = 0; i < graphs.size(); ++i) {
                    graph->merge(*graphs[i]);
                    std::cout << "traversal " << files[i + 1] << " done\t"
                              << timer.elapsed() << "sec" << std::endl;
                }
                std::cout << "Graphs merged\t" << timer.elapsed() << "sec" << std::endl;
            } else if (config->parallel > 1 || config->parts_total > 1) {
                std::cout << "Start merging blocks" << std::endl;
                timer.reset();

                auto *chunk = merge::merge_blocks_to_chunk(graphs, config->part_idx,
                                                                   config->parts_total,
                                                                   config->parallel,
                                                                   config->num_bins_per_thread,
                                                                   config->verbose);
                if (!chunk) {
                    std::cerr << "ERROR when building chunk " << config->part_idx << std::endl;
                    exit(1);
                }
                std::cout << "Blocks merged\t" << timer.elapsed() << "sec" << std::endl;

                if (config->parts_total > 1) {
                    chunk->serialize(config->outfbase
                                      + "." + std::to_string(config->part_idx)
                                      + "_" + std::to_string(config->parts_total));
                } else {
                    graph = new DBG_succ(graphs[0]->get_k());
                    chunk->initialize_graph(graph);
                    std::cout << "Graphs merged\t" << timer.elapsed() << "sec" << std::endl;
                }
                delete chunk;
            } else {
                std::cout << "Start merging graphs" << std::endl;
                timer.reset();

                graph = merge::merge(graphs, config->verbose);

                std::cout << "Graphs merged\t" << timer.elapsed() << "sec" << std::endl;
            }

            for (auto *graph_ : graphs) {
                delete graph_;
            }

            std::cerr << "... done merging." << std::endl;
            break;
        }
        case Config::STATS: {
            std::ofstream outstream;
            if (!config->outfbase.empty()) {
                outstream.open(config->outfbase + ".stats.dbg");
                outstream << "file\tnodes\tedges\tk" << std::endl;
            }
            for (const auto &file : files) {
                DBG_succ *graph_ = load_critical_graph_from_file(file);

                if (!config->quiet) {
                    std::cout << "Statistics for file " << file << std::endl;
                    std::cout << "nodes: " << graph_->num_nodes() << std::endl;
                    std::cout << "edges: " << graph_->num_edges() << std::endl;
                    std::cout << "k: " << graph_->get_k() << std::endl;
                }
                if (outstream.is_open()) {
                    outstream << file << "\t"
                              << graph_->num_nodes() << "\t"
                              << graph_->num_edges() << "\t"
                              << graph_->get_k() << std::endl;
                }
                if (config->print_graph_succ)
                    graph_->print_state();

                std::ifstream instream(file + ".anno.dbg");
                if (instream.good()) {
                    size_t anno_size = libmaus2::util::NumberSerialisation::deserialiseNumber(instream);
                    std::cout << "annot: " << anno_size << std::endl;
                }

                delete graph_;
            }
            break;
        }
        case Config::TRANSFORM: {
            //for (unsigned int f = 0; f < files.size(); ++f) {
                //DBG_succ* graph_ = new DBG_succ(files[f]);
                DBG_succ *graph_ = load_critical_graph_from_file(files[0]);
                // checks whether annotation exists and creates an empty one if not
                // graph_->annotationFromFile(config->infbase + ".anno.dbg");
                if (config->to_adj_list) {
                    if (config->outfbase.size()) {
                        std::ofstream outstream(config->outfbase + ".adjlist");
                        graph_->print_adj_list(outstream);
                    } else {
                        graph_->print_adj_list(std::cout);
                    }
                }
                delete graph_;
            //}
            return 0;
        }
        case Config::ALIGN: {
            // load graph
            assert(config->infbase.size());

            graph = load_critical_graph_from_file(config->infbase);

            for (const auto &file : files) {
                std::cout << "Opening file for alignment ..." << file << std::endl;

                // open stream to input fasta
                gzFile input_p = gzopen(file.c_str(), "r");
                if (input_p == Z_NULL) {
                    std::cerr << "ERROR no such file " << file << std::endl;
                    exit(1);
                }
                kseq_t *read_stream = kseq_init(input_p);
                if (read_stream == NULL) {
                    std::cerr << "ERROR while opening input file " << file << std::endl;
                    exit(1);
                }

                while (kseq_read(read_stream) >= 0) {
                    //bool reverse = false;

                    if (config->distance > 0) {
                        uint64_t aln_len = read_stream->seq.l;

                        // since we set aln_len = read_stream->seq.l, we only get a single hit vector
                        auto graphindices = graph->align_fuzzy(
                            std::string(read_stream->seq.s, read_stream->seq.l),
                            aln_len,
                            config->distance
                        );
                        //for (size_t i = 0; i < graphindices.size(); ++i) {
                        size_t i = 0;

                        int print_len = i + aln_len < read_stream->seq.l
                                        ? aln_len
                                        : (read_stream->seq.l - i);

                        printf("%.*s: ", print_len, read_stream->seq.s + i);

                        for (size_t l = 0;  l < graphindices.at(i).size(); ++l) {
                            HitInfo curr_hit(graphindices.at(i).at(l));
                            for (size_t m = 0; m < curr_hit.path.size(); ++m) {
                                std::cout << curr_hit.path.at(m) << ':';
                            }
                            for (size_t m = curr_hit.rl; m <= curr_hit.ru; ++m) {
                                std::cout << m << " ";
                            }
                            std::cout << "[" << curr_hit.cigar << "] ";
                        }
                        //}

                        // try reverse
                        if (graphindices.at(i).size() == 0) {
                            reverse_complement(read_stream->seq);
                            graphindices = graph->align_fuzzy(
                                std::string(read_stream->seq.s, read_stream->seq.l),
                                aln_len, config->distance
                            );
                            for (size_t l = 0; l < graphindices.at(i).size(); ++l) {
                                HitInfo curr_hit(graphindices.at(i).at(l));
                                for (size_t m = 0; m < curr_hit.path.size(); ++m) {
                                    std::cout << curr_hit.path.at(m) << ':';
                                }
                                for (size_t m = curr_hit.rl; m <= curr_hit.ru; ++m) {
                                    std::cout << m << " ";
                                }
                                std::cout << "[" << curr_hit.cigar << "] ";
                            }
                        }
                        std::cout << std::endl;
                    } else {

                        auto graphindices = graph->align(read_stream->seq.s,
                                                         config->alignment_length);

                        if (config->query) {
                            size_t num_discovered = std::count_if(
                                graphindices.begin(), graphindices.end(),
                                [](const auto &x) { return x > 0; }
                            );
                            std::cout << num_discovered
                                      << " / "
                                      << read_stream->seq.l << std::endl;
                        } else {
                            for (size_t i = 0; i < graphindices.size(); ++i) {
                                for (uint64_t j = 0; j < graph->get_k(); ++j) {
                                    std::cout << read_stream->seq.s[i + j];
                                }
                                std::cout << ": " << graphindices[i] << std::endl;
                            }
                        }
                    }
                }

                // close stream
                kseq_destroy(read_stream);
                gzclose(input_p);
            }
            delete graph;
            graph = NULL;
            break;
        }
        case Config::NO_IDENTITY: {
            assert(false);
            break;
        }
    }

    // output and cleanup
    if (graph) {
        // graph output
        if (config->print_graph_succ)
            graph->print_state();
        if (!config->sqlfbase.empty())
            traverse::toSQL(graph, config->fname, config->sqlfbase);
        if (!config->outfbase.empty())
            graph->serialize(config->outfbase);
        delete graph;
    }

    return 0;
}
