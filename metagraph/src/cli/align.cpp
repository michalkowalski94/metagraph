#include "align.hpp"

#include "common/logger.hpp"
#include "common/unix_tools.hpp"
#include "common/threads/threading.hpp"
#include "graph/representation/succinct/dbg_succinct.hpp"
#include "graph/representation/succinct/dbg_succinct_range.hpp"
#include "graph/representation/canonical_dbg.hpp"
#include "graph/alignment/dbg_aligner.hpp"
#include "graph/alignment/aligner_methods.hpp"
#include "seq_io/sequence_io.hpp"
#include "config/config.hpp"
#include "load/load_graph.hpp"


namespace mtg {
namespace cli {

using namespace mtg::graph;
using namespace mtg::graph::align;

using mtg::seq_io::kseq_t;
using mtg::common::logger;


DBGAlignerConfig initialize_aligner_config(size_t k, const Config &config) {
    assert(config.alignment_num_alternative_paths);

    DBGAlignerConfig aligner_config;

    aligner_config.queue_size = config.alignment_queue_size;
    aligner_config.bandwidth = config.alignment_vertical_bandwidth;
    aligner_config.num_alternative_paths = config.alignment_num_alternative_paths;
    aligner_config.min_seed_length = config.alignment_min_seed_length;
    aligner_config.max_seed_length = config.alignment_max_seed_length;
    aligner_config.max_nodes_per_seq_char = config.alignment_max_nodes_per_seq_char;
    aligner_config.max_ram_per_alignment = config.alignment_max_ram;
    aligner_config.min_cell_score = config.alignment_min_cell_score;
    aligner_config.min_path_score = config.alignment_min_path_score;
    aligner_config.xdrop = config.alignment_xdrop;
    aligner_config.exact_kmer_match_fraction = config.discovery_fraction;
    aligner_config.gap_opening_penalty = -config.alignment_gap_opening_penalty;
    aligner_config.gap_extension_penalty = -config.alignment_gap_extension_penalty;
    aligner_config.forward_and_reverse_complement = config.align_both_strands;
    aligner_config.alignment_edit_distance = config.alignment_edit_distance;
    aligner_config.alignment_match_score = config.alignment_match_score;
    aligner_config.alignment_mm_transition_score = config.alignment_mm_transition_score;
    aligner_config.alignment_mm_transversion_score = config.alignment_mm_transversion_score;
    aligner_config.chain_alignments = config.alignment_chain_alignments;

    if (!aligner_config.min_seed_length)
        aligner_config.min_seed_length = k;

    if (!aligner_config.max_seed_length)
        aligner_config.max_seed_length = k;

    logger->trace("Alignment settings:");
    logger->trace("\t Alignments to report: {}", aligner_config.num_alternative_paths);
    logger->trace("\t Priority queue size: {}", aligner_config.queue_size);
    logger->trace("\t Min seed length: {}", aligner_config.min_seed_length);
    logger->trace("\t Max seed length: {}", aligner_config.max_seed_length);
    logger->trace("\t Max num nodes per sequence char: {}", aligner_config.max_nodes_per_seq_char);
    logger->trace("\t Max RAM per alignment: {}", aligner_config.max_ram_per_alignment);
    logger->trace("\t Gap opening penalty: {}", int64_t(aligner_config.gap_opening_penalty));
    logger->trace("\t Gap extension penalty: {}", int64_t(aligner_config.gap_extension_penalty));
    logger->trace("\t Min DP table cell score: {}", int64_t(aligner_config.min_cell_score));
    logger->trace("\t Min alignment score: {}", aligner_config.min_path_score);
    logger->trace("\t Bandwidth: {}", aligner_config.bandwidth);
    logger->trace("\t X drop-off: {}", aligner_config.xdrop);
    logger->trace("\t Exact k-mer match fraction: {}", aligner_config.exact_kmer_match_fraction);
    logger->trace("\t Chain alignments: {}", aligner_config.chain_alignments);

    logger->trace("\t Scoring matrix: {}", config.alignment_edit_distance ? "unit costs" : "matrix");
    if (!config.alignment_edit_distance) {
        logger->trace("\t\t Match score: {}", int64_t(config.alignment_match_score));
        logger->trace("\t\t (DNA) Transition score: {}",
                      int64_t(config.alignment_mm_transition_score));
        logger->trace("\t\t (DNA) Transversion score: {}",
                      int64_t(config.alignment_mm_transversion_score));
    }

    aligner_config.set_scoring_matrix();

    return aligner_config;
}

std::unique_ptr<IDBGAligner> build_aligner(const DeBruijnGraph &graph, const Config &config) {
    assert(!config.canonical || graph.is_canonical_mode());

    return build_aligner(graph, initialize_aligner_config(graph.get_k(), config));
}

std::unique_ptr<IDBGAligner> build_aligner(const DeBruijnGraph &graph,
                                           const DBGAlignerConfig &aligner_config) {
    assert(aligner_config.min_seed_length <= aligner_config.max_seed_length);

    if (aligner_config.min_seed_length < graph.get_k()) {
        auto *range_graph = dynamic_cast<const DBGSuccinctRange*>(&graph);
        if (!range_graph) {
            auto *canonical = dynamic_cast<const CanonicalDBG*>(&graph);
            if (canonical)
                range_graph = dynamic_cast<const DBGSuccinctRange*>(&canonical->get_graph());
        }

        if (!range_graph) {
            logger->error("Seeds of length < k can only be found with the succinct graph representation");
            exit(1);
        }
    }

    if (aligner_config.max_seed_length == graph.get_k()) {
        // seeds are single k-mers
        return std::make_unique<DBGAligner<>>(graph, aligner_config);

    } else {
        // seeds are maximal matches within unitigs (uni-MEMs)
        return std::make_unique<DBGAligner<UniMEMSeeder<>>>(graph, aligner_config);
    }
}

void map_sequences_in_file(const std::string &file,
                           const DeBruijnGraph &graph,
                           const Config &config,
                           const Timer &timer,
                           ThreadPool *thread_pool = nullptr,
                           std::mutex *print_mutex = nullptr) {
    // TODO: multithreaded
    std::ignore = std::tie(thread_pool, print_mutex);

    std::ostream *out = config.outfbase.size()
        ? new std::ofstream(config.outfbase)
        : &std::cout;

    Timer data_reading_timer;

    const auto *range_graph = dynamic_cast<const DBGSuccinctRange*>(&graph);
    assert(config.alignment_length <= graph.get_k());
    assert(config.alignment_length == graph.get_k() || range_graph);

    seq_io::read_fasta_file_critical(file, [&](kseq_t *read_stream) {
        if (config.query_presence
                && config.alignment_length == graph.get_k()) {

            bool found = graph.find(read_stream->seq.s,
                                    config.discovery_fraction);

            if (!config.filter_present) {
                *out << found << "\n";

            } else if (found) {
                *out << ">" << read_stream->name.s << "\n"
                            << read_stream->seq.s << "\n";
            }

            return;
        }

        std::vector<DeBruijnGraph::node_index> graphindices;
        graph.map_to_nodes(read_stream->seq.s,
            [&](auto node) {
                if (range_graph) {
                    size_t match_length = graph.get_k() - range_graph->get_offset(node);
                    if (match_length < config.alignment_length)
                        node = 0;
                }

                graphindices.emplace_back(node);
            },
            [&]() {
                return graphindices.size() + config.alignment_length - 1
                    == read_stream->seq.l;
            }
        );

        size_t num_discovered = std::count_if(graphindices.begin(), graphindices.end(),
                                              [](const auto &x) { return x > 0; });

        const size_t num_kmers = graphindices.size();

        if (config.query_presence) {
            const size_t min_kmers_discovered = std::ceil(
                config.discovery_fraction * num_kmers
            );
            if (config.filter_present) {
                if (num_discovered >= min_kmers_discovered)
                    *out << ">" << read_stream->name.s << "\n"
                                << read_stream->seq.s << "\n";
            } else {
                *out << (num_discovered >= min_kmers_discovered) << "\n";
            }
            return;
        }

        if (config.count_kmers) {
            std::sort(graphindices.begin(), graphindices.end());
            size_t num_unique_matching_kmers = std::inner_product(
                graphindices.begin() + 1, graphindices.end(),
                graphindices.begin(),
                size_t(graphindices.front() != DeBruijnGraph::npos),
                std::plus<size_t>(),
                [](auto next, auto prev) { return next && next != prev; }
            );
            *out << read_stream->name.s << "\t"
                 << num_discovered << "/" << num_kmers << "/"
                 << num_unique_matching_kmers << "\n";
            return;
        }

        for (size_t i = 0; i < graphindices.size(); ++i) {
            assert(i + config.alignment_length <= read_stream->seq.l);
            std::string_view subseq(read_stream->seq.s + i, config.alignment_length);

            size_t offset = range_graph && graphindices[i]
                ? range_graph->get_offset(graphindices[i])
                : 0;
            if (!offset) {
                *out << subseq << ": " << graphindices[i] << "\n";
            } else {
                range_graph->call_nodes_in_range(graphindices[i], [&](auto node) {
                    *out << subseq << ": " << node << "\n";
                });
            }
        }

    }, config.forward_and_reverse);

    logger->trace("File '{}' processed in {} sec, current mem usage: {} MiB, total time {} sec",
                  file, data_reading_timer.elapsed(), get_curr_RSS() >> 20, timer.elapsed());

    if (config.outfbase.size())
        delete out;
}


int align_to_graph(Config *config) {
    assert(config);

    const auto &files = config->fnames;

    assert(config->infbase.size());

    // initialize aligner
    auto graph = load_critical_dbg(config->infbase);
    auto dbg = std::dynamic_pointer_cast<DBGSuccinct>(graph);

    // This speeds up mapping, and allows for node suffix matching
    if (dbg)
        dbg->reset_mask();

    if (dbg && (config->alignment_min_seed_length < graph->get_k()
            || config->alignment_length < graph->get_k())) {
        logger->trace("Wrap as suffix range succinct DBG");
        graph.reset(new DBGSuccinctRange(*dbg));
    }

    if (config->canonical && !graph->is_canonical_mode()) {
        logger->trace("Wrap as canonical DBG");
        // TODO: check and wrap into canonical only if the graph is primary
        graph.reset(new CanonicalDBG(graph, true));
    }

    Timer timer;
    ThreadPool thread_pool(get_num_threads());
    std::mutex print_mutex;

    if (config->map_sequences) {
        if (!config->alignment_length) {
            config->alignment_length = graph->get_k();
        } else if (config->alignment_length > graph->get_k()) {
            logger->warn("Mapping to k-mers longer than k is not supported");
            config->alignment_length = graph->get_k();
        }

        if (!dbg && config->alignment_length != graph->get_k()) {
            logger->error("Matching k-mers shorter than k only supported for DBGSuccinct");
            exit(1);
        }

        logger->trace("Map sequences against the de Bruijn graph with k={}",
                      graph->get_k());
        logger->trace("Length of mapped k-mers: {}", config->alignment_length);

        for (const auto &file : files) {
            logger->trace("Map sequences from file '{}'", file);

            map_sequences_in_file(file,
                                  *graph,
                                  *config,
                                  timer,
                                  &thread_pool,
                                  &print_mutex);
        }

        thread_pool.join();

        return 0;
    }

    auto aligner = build_aligner(*graph, *config);

    if (!dbg && aligner->get_config().min_seed_length < graph->get_k()) {
        logger->error("Matching k-mers shorter than k only supported for DBGSuccinct");
        exit(1);
    }

    for (const auto &file : files) {
        logger->trace("Align sequences from file '{}'", file);

        Timer data_reading_timer;

        std::ostream *out = config->outfbase.size()
            ? new std::ofstream(config->outfbase)
            : &std::cout;

        seq_io::read_fasta_file_critical(file, [&](kseq_t *read_stream) {
            thread_pool.enqueue([&](const std::string &query, const std::string &header) {
                auto paths = aligner->align(query);

                std::lock_guard<std::mutex> lock(print_mutex);
                if (!config->output_json) {
                    *out << header << "\t" << paths.get_query();
                    if (paths.empty()) {
                        *out << "\t*\t*\t" << config->alignment_min_path_score
                             << "\t*\t*\t*";
                    } else {
                        size_t total_score = 0;
                        for (const auto &path : paths) {
                            total_score += path.get_score();
                            *out << "\t" << path;
                        }

                        if (config->alignment_chain_alignments)
                            *out << "\t" << total_score;
                    }

                    *out << "\n";
                } else {
                    Json::StreamWriterBuilder builder;
                    builder["indentation"] = "";

                    bool secondary = false;
                    for (const auto &path : paths) {
                        const auto& path_query = path.get_orientation()
                            ? paths.get_query_reverse_complement()
                            : paths.get_query();

                        *out << Json::writeString(builder,
                                                  path.to_json(path_query,
                                                               *graph,
                                                               secondary,
                                                               header)) << "\n";

                        secondary = true;
                    }

                    if (paths.empty()) {
                        *out << Json::writeString(builder,
                                                  DBGAligner<>::DBGAlignment().to_json(
                                                      query,
                                                      *graph,
                                                      secondary,
                                                      header)
                                                  ) << "\n";
                    }
                }
            }, std::string(read_stream->seq.s),
               config->fasta_anno_comment_delim != Config::UNINITIALIZED_STR
                   && read_stream->comment.l
                       ? utils::join_strings(
                           { read_stream->name.s, read_stream->comment.s },
                           config->fasta_anno_comment_delim,
                           true)
                       : std::string(read_stream->name.s));
        });

        thread_pool.join();

        logger->trace("File '{}' processed in {} sec, "
                      "current mem usage: {} MiB, total time {} sec",
                      file, data_reading_timer.elapsed(),
                      get_curr_RSS() >> 20, timer.elapsed());

        if (config->outfbase.size())
            delete out;
    }

    return 0;
}

} // namespace cli
} // namespace mtg
