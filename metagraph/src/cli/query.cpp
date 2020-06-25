#include "query.hpp"

#include <ips4o.hpp>
#include <tsl/ordered_set.h>

#include "common/logger.hpp"
#include "common/unix_tools.hpp"
#include "common/hash/hash.hpp"
#include "common/utils/template_utils.hpp"
#include "common/threads/threading.hpp"
#include "common/vectors/vector_algorithm.hpp"
#include "annotation/representation/annotation_matrix/static_annotators_def.hpp"
#include "graph/alignment/dbg_aligner.hpp"
#include "graph/representation/hash/dbg_hash_ordered.hpp"
#include "graph/representation/succinct/dbg_succinct.hpp"
#include "graph/representation/succinct/boss_construct.hpp"
#include "seq_io/sequence_io.hpp"
#include "config/config.hpp"
#include "load/load_graph.hpp"
#include "load/load_annotated_graph.hpp"

#include "align.hpp"


namespace mtg {
namespace cli {

const size_t kRowBatchSize = 100'000;
const bool kPrefilterWithBloom = true;

using mtg::common::logger;


std::string QueryExecutor::execute_query(const std::string &seq_name,
                                         const std::string &sequence,
                                         bool count_labels,
                                         bool print_signature,
                                         bool suppress_unlabeled,
                                         size_t num_top_labels,
                                         double discovery_fraction,
                                         std::string anno_labels_delimiter,
                                         const AnnotatedDBG &anno_graph,
                                         const DBGAlignerConfig *aligner_config) {
    std::unique_ptr<IDBGAligner::DBGQueryAlignment> alignment;
    if (aligner_config) {
        alignment = std::make_unique<IDBGAligner::DBGQueryAlignment>(
            build_masked_aligner(anno_graph, *aligner_config)->align(sequence)
        );
    }

    std::string output;
    output.reserve(1'000);

    if (print_signature) {
        if (alignment) {
            auto top_labels = alignment->get_top_label_cigars(num_top_labels,
                                                              discovery_fraction);

            if (!top_labels.size() && suppress_unlabeled)
                return "";

            output += seq_name;

            for (const auto &[label, cigar, score] : top_labels) {
                output += fmt::format("\t<{}>:{}:{}:{}", label,
                                      cigar.get_num_matches(),
                                      cigar.to_string(),
                                      score);
            }
        } else {
            auto top_labels
                = anno_graph.get_top_label_signatures(sequence,
                                                      num_top_labels,
                                                      discovery_fraction);

            if (!top_labels.size() && suppress_unlabeled)
                return "";

            output += seq_name;

            for (const auto &[label, kmer_presence_mask] : top_labels) {
                output += fmt::format("\t<{}>:{}:{}:{}", label,
                                      sdsl::util::cnt_one_bits(kmer_presence_mask),
                                      sdsl::util::to_string(kmer_presence_mask),
                                      anno_graph.score_kmer_presence_mask(kmer_presence_mask));
            }
        }

        output += '\n';

    } else if (count_labels) {
        auto top_labels = alignment
            ? alignment->get_top_labels(num_top_labels, discovery_fraction)
            : anno_graph.get_top_labels(sequence, num_top_labels, discovery_fraction);

        if (!top_labels.size() && suppress_unlabeled)
            return "";

        output += seq_name;

        for (const auto &[label, count] : top_labels) {
            output += "\t<";
            output += label;
            output += ">:";
            output += fmt::format_int(count).c_str();
        }

        output += '\n';

    } else {
        auto labels_discovered = alignment
            ? alignment->get_labels(discovery_fraction)
            : anno_graph.get_labels(sequence, discovery_fraction);

        if (!labels_discovered.size() && suppress_unlabeled)
            return "";

        output += seq_name;
        output += '\t';
        output += utils::join_strings(labels_discovered, anno_labels_delimiter);
        output += '\n';
    }

    return output;
}

/**
 * @brief      Construct annotation submatrix with a subset of rows extracted
 *             from the full annotation matrix
 *
 * @param[in]  full_annotation  The full annotation matrix.
 * @param[in]  index_in_full    Indexes of rows in the full annotation matrix.
 *                              index_in_full[i] = -1 means that the i-th row in
 *                              the submatrix is empty.
 * @param[in]  num_threads      The number of threads used.
 *
 * @return     Annotation submatrix in the UniqueRowAnnotator representation
 */
std::unique_ptr<annotate::UniqueRowAnnotator>
slice_annotation(const AnnotatedDBG::Annotator &full_annotation,
                 const std::vector<uint64_t> &index_in_full,
                 size_t num_threads) {
    const uint64_t npos = -1;

    if (auto *rb = dynamic_cast<const RainbowMatrix *>(&full_annotation.get_matrix())) {
        // shortcut construction for Rainbow<> annotation
        std::vector<uint64_t> row_indexes;
        row_indexes.reserve(index_in_full.size());
        for (uint64_t i : index_in_full) {
            if (i != npos) {
                row_indexes.push_back(i);
            } else {
                row_indexes.push_back(0);
            }
        }

        // get unique rows and set pointers to them in |row_indexes|
        auto unique_rows = rb->get_rows(&row_indexes, num_threads);

        if (unique_rows.size() >= std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("There must be less than 2^32 unique rows."
                                     " Reduce the query batch size.");
        }

        // if the 0-th row is not empty, we must insert an empty unique row
        // and reassign those indexes pointing to npos in |index_in_full|.
        if (rb->get_row(0).size()) {
            logger->trace("Add empty row");
            unique_rows.emplace_back();
            for (uint64_t i = 0; i < index_in_full.size(); ++i) {
                if (index_in_full[i] == npos) {
                    row_indexes[i] = unique_rows.size() - 1;
                }
            }
        }

        // copy annotations from the full graph to the query graph
        return std::make_unique<annotate::UniqueRowAnnotator>(
            std::make_unique<UniqueRowBinmat>(std::move(unique_rows),
                                              std::vector<uint32_t>(row_indexes.begin(),
                                                                    row_indexes.end()),
                                              full_annotation.num_labels()),
            full_annotation.get_label_encoder()
        );
    }

    std::vector<std::pair<uint64_t, uint64_t>> from_full_to_small;

    for (uint64_t i = 0; i < index_in_full.size(); ++i) {
        if (index_in_full[i] != npos)
            from_full_to_small.emplace_back(index_in_full[i], i);
    }

    ips4o::parallel::sort(from_full_to_small.begin(), from_full_to_small.end(),
                          utils::LessFirst(), num_threads);

    using RowSet = tsl::ordered_set<BinaryMatrix::SetBitPositions,
                                    utils::VectorHash,
                                    std::equal_to<BinaryMatrix::SetBitPositions>,
                                    std::allocator<BinaryMatrix::SetBitPositions>,
                                    std::vector<BinaryMatrix::SetBitPositions>,
                                    uint32_t>;
    RowSet unique_rows { BinaryMatrix::SetBitPositions() };
    std::vector<uint32_t> row_rank(index_in_full.size(), 0);

    #pragma omp parallel for num_threads(num_threads) schedule(dynamic)
    for (uint64_t batch_begin = 0;
                        batch_begin < from_full_to_small.size();
                                        batch_begin += kRowBatchSize) {
        const uint64_t batch_end
            = std::min(batch_begin + kRowBatchSize,
                       static_cast<uint64_t>(from_full_to_small.size()));

        std::vector<uint64_t> row_indexes;
        row_indexes.reserve(batch_end - batch_begin);
        for (uint64_t i = batch_begin; i < batch_end; ++i) {
            assert(from_full_to_small[i].first < full_annotation.num_objects());

            row_indexes.push_back(from_full_to_small[i].first);
        }

        auto rows = full_annotation.get_matrix().get_rows(row_indexes);

        assert(rows.size() == batch_end - batch_begin);

        #pragma omp critical
        {
            for (uint64_t i = batch_begin; i < batch_end; ++i) {
                const auto &row = rows[i - batch_begin];
                auto it = unique_rows.emplace(row).first;
                row_rank[from_full_to_small[i].second] = it - unique_rows.begin();
                if (unique_rows.size() == std::numeric_limits<uint32_t>::max())
                    throw std::runtime_error("There must be less than 2^32 unique rows."
                                             " Reduce the query batch size.");
            }
        }
    }

    auto &annotation_rows = const_cast<std::vector<BinaryMatrix::SetBitPositions>&>(
        unique_rows.values_container()
    );

    // copy annotations from the full graph to the query graph
    return std::make_unique<annotate::UniqueRowAnnotator>(
        std::make_unique<UniqueRowBinmat>(std::move(annotation_rows),
                                          std::move(row_rank),
                                          full_annotation.num_labels()),
        full_annotation.get_label_encoder()
    );
}

template <class Contigs>
std::shared_ptr<DBGSuccinct> convert_to_succinct(const DeBruijnGraph &full_dbg,
                                                 const Contigs &contigs,
                                                 bool canonical = false,
                                                 size_t num_threads = 1) {
    BOSSConstructor constructor(full_dbg.get_k() - 1, canonical, 0, "", num_threads);
    for (size_t i = 0; i < contigs.size(); ++i) {
        const std::string &contig = contigs[i].first;
        const auto &nodes_in_full = contigs[i].second;
        constructor.add_sequences([&](const CallString &callback) {
            auto it = nodes_in_full.begin();
            while ((it = std::find_if(it, nodes_in_full.end(), [&](auto i) { return i; }))
                    < nodes_in_full.end()) {
                auto next = std::find(it, nodes_in_full.end(), 0);
                assert(full_dbg.find(std::string_view(
                    contig.data() + (it - nodes_in_full.begin()),
                    next - it + full_dbg.get_k() - 1
                )));
                callback(std::string(contig.data() + (it - nodes_in_full.begin()),
                                     next - it + full_dbg.get_k() - 1));
                it = next;
            }
        });
    }

    return std::make_shared<DBGSuccinct>(new BOSS(&constructor), canonical);
}

/**
 * Construct a de Bruijn graph from the query sequences
 * fetched in |call_sequences|.
 *
 *  Algorithm.
 *
 * 1. Index k-mers from the query sequences in a non-canonical query de Bruijn
 *    graph (with pre-filtering by a Bloom filter, if initialized).
 *    This query graph will be rebuilt as a canonical one in step 2.b), if the
 *    full graph is canonical.
 *
 * 2. Extract contigs from this small de Bruijn graph and map them to the full
 *    graph to map each k-mer to its respective annotation row index.
 *    --> here we map each unique k-mer in sequences only once.
 *
 *    (b, canonical) If the full graph is canonical, rebuild the query graph
 *                   in the canonical mode storing all k-mers found in the full
 *                   graph.
 *
 * 3. If |discovery_fraction| is greater than zero, map all query sequences to
 *    the query graph and filter out those having too few k-mer matches.
 *    Then, remove from the query graph those k-mers occurring only in query
 *    sequences that have been filtered out.
 *
 * 4. Extract annotation for the nodes of the query graph and return.
 */
std::unique_ptr<AnnotatedDBG>
construct_query_graph(const AnnotatedDBG &anno_graph,
                      StringGenerator call_sequences,
                      double discovery_fraction,
                      size_t num_threads,
                      bool canonical,
                      size_t sub_k) {
    const auto &full_dbg = anno_graph.get_graph();
    const auto &full_annotation = anno_graph.get_annotation();

    canonical |= full_dbg.is_canonical_mode();

    Timer timer;

    // construct graph storing all k-mers in query
    auto graph_init = std::make_shared<DBGHashOrdered>(full_dbg.get_k(), false);

    const auto *dbg_succ = dynamic_cast<const DBGSuccinct *>(&full_dbg);
    if (kPrefilterWithBloom && dbg_succ) {
        if (dbg_succ->get_bloom_filter())
            logger->trace(
                    "[Query graph construction] Started indexing k-mers pre-filtered "
                    "with Bloom filter");

        call_sequences([&](const std::string &sequence) {
            // TODO: implement add_sequence with filter for all graph representations
            graph_init->add_sequence(
                sequence,
                get_missing_kmer_skipper(dbg_succ->get_bloom_filter(), sequence)
            );
        });
    } else {
        call_sequences([&](const std::string &sequence) {
            graph_init->add_sequence(sequence);
        });
    }

    std::shared_ptr<DeBruijnGraph> graph = std::move(graph_init);

    logger->trace("[Query graph construction] k-mer indexing took {} sec", timer.elapsed());
    timer.reset();

    // pull contigs from query graph
    std::vector<std::pair<std::string, std::vector<DeBruijnGraph::node_index>>> contigs;
    std::mutex seq_mutex;
    graph->call_sequences([&](auto&&... contig_args) {
                              std::lock_guard<std::mutex> lock(seq_mutex);
                              contigs.emplace_back(std::move(contig_args)...);
                          },
                          get_num_threads(),
                          canonical);  // pull only primary contigs when building canonical query graph

    logger->trace("[Query graph construction] Contig extraction took {} sec", timer.elapsed());
    timer.reset();

    // map contigs onto the full graph
    std::vector<uint64_t> index_in_full_graph;

    if (canonical) {
        #pragma omp parallel for num_threads(num_threads) schedule(dynamic, 10)
        for (size_t i = 0; i < contigs.size(); ++i) {
            std::string &contig = contigs[i].first;
            auto &nodes_in_full = contigs[i].second;

            if (full_dbg.is_canonical_mode()) {
                size_t j = 0;
                full_dbg.map_to_nodes(contig,
                    [&](auto node_in_full) { nodes_in_full[j++] = node_in_full; }
                );
            } else {
                size_t j = 0;
                // TODO: if a k-mer is found, don't search its reverse-complement
                // TODO: add `primary/canonical` mode to DBGSuccinct?
                full_dbg.map_to_nodes_sequentially(contig,
                    [&](auto node_in_full) { nodes_in_full[j++] = node_in_full; }
                );
                reverse_complement(contig.begin(), contig.end());
                full_dbg.map_to_nodes_sequentially(contig,
                    [&](auto node_in_full) {
                        --j;
                        if (node_in_full)
                            nodes_in_full[j] = node_in_full;
                    }
                );
                reverse_complement(contig.begin(), contig.end());
                assert(j == 0);
            }
        }

        logger->trace("[Query graph construction] Contigs mapped to graph in {} sec", timer.elapsed());
        timer.reset();

        // construct canonical graph storing all k-mers found in the full graph
        if (sub_k >= full_dbg.get_k()) {
            graph_init = std::make_shared<DBGHashOrdered>(full_dbg.get_k(), true);

            for (size_t i = 0; i < contigs.size(); ++i) {
                const std::string &contig = contigs[i].first;
                const auto &nodes_in_full = contigs[i].second;
                size_t j = 0;
                graph_init->add_sequence(contig, [&]() { return nodes_in_full[j++] == 0; });
            }

            graph = std::move(graph_init);
        } else {
            graph = convert_to_succinct(full_dbg, contigs, true, num_threads);
        }

        logger->trace("[Query graph construction] k-mers reindexed in canonical mode in {} sec",
                      timer.elapsed());
        timer.reset();

        index_in_full_graph.assign(graph->max_index() + 1, 0);

        #pragma omp parallel for num_threads(num_threads) schedule(dynamic, 10)
        for (size_t i = 0; i < contigs.size(); ++i) {
            const std::string &contig = contigs[i].first;
            const auto &nodes_in_full = contigs[i].second;

            size_t j = 0;
            graph->map_to_nodes(contig, [&](auto node) {
                index_in_full_graph[node] = nodes_in_full[j++];
            });
            assert(j == nodes_in_full.size());
        }

        logger->trace("[Query graph construction] Mapping between graphs constructed in {} sec",
                      timer.elapsed());
        timer.reset();

    } else {
        if (sub_k >= full_dbg.get_k()) {
            index_in_full_graph.assign(graph->max_index() + 1, 0);

            #pragma omp parallel for num_threads(num_threads) schedule(dynamic, 10)
            for (size_t i = 0; i < contigs.size(); ++i) {
                const std::string &contig = contigs[i].first;
                const auto &path = contigs[i].second;

                size_t j = 0;
                full_dbg.map_to_nodes(contig, [&](auto node_in_full) {
                    index_in_full_graph[path[j++]] = node_in_full;
                });
                assert(j == path.size());
            }
        } else {
            graph = convert_to_succinct(full_dbg, contigs, false, num_threads);
            contigs = decltype(contigs)();
            index_in_full_graph.assign(graph->max_index() + 1, 0);

            graph->call_sequences([&](const std::string &contig, const auto &path) {
                size_t j = 0;
                full_dbg.map_to_nodes(contig, [&](auto node_in_full) {
                    index_in_full_graph[path[j++]] = node_in_full;
                });
                assert(j == path.size());
            }, get_num_threads());
        }

        logger->trace("[Query graph construction] Contigs mapped to graph in {} sec", timer.elapsed());
        timer.reset();
    }

    contigs = decltype(contigs)();

    assert(!index_in_full_graph.at(0));

    if (discovery_fraction > 0 && sub_k >= full_dbg.get_k()) {
        sdsl::bit_vector mask(graph->max_index() + 1, false);

        call_sequences([&](const std::string &sequence) {
            if (sequence.length() < graph->get_k())
                return;

            const size_t num_kmers = sequence.length() - graph->get_k() + 1;
            const size_t max_kmers_missing = num_kmers * (1 - discovery_fraction);
            const size_t min_kmers_discovered = num_kmers - max_kmers_missing;
            size_t num_kmers_discovered = 0;
            size_t num_kmers_missing = 0;

            std::vector<DeBruijnGraph::node_index> nodes;
            nodes.reserve(num_kmers);

            graph->map_to_nodes(sequence,
                [&](auto node) {
                    if (index_in_full_graph[node]) {
                        num_kmers_discovered++;
                        nodes.push_back(node);
                    } else {
                        num_kmers_missing++;
                    }
                },
                [&]() { return num_kmers_missing > max_kmers_missing
                                || num_kmers_discovered >= min_kmers_discovered; }
            );

            if (num_kmers_missing <= max_kmers_missing) {
                for (auto node : nodes) {
                    mask[node] = true;
                }
            }
        });

        // correcting the mask
        call_zeros(mask, [&](auto i) { index_in_full_graph[i] = 0; });

        graph = std::make_shared<MaskedDeBruijnGraph>(
            graph,
            std::make_unique<bit_vector_stat>(std::move(mask))
        );

        logger->trace("[Query graph construction] Reduced k-mer dictionary in {} sec",
                      timer.elapsed());
        timer.reset();
    }

    // convert to annotation indexes: remove 0 and shift
    for (size_t i = 1; i < index_in_full_graph.size(); ++i) {
        if (index_in_full_graph[i]) {
            index_in_full_graph[i - 1]
                = AnnotatedDBG::graph_to_anno_index(index_in_full_graph[i]);
        } else {
            index_in_full_graph[i - 1] = -1;  // npos
        }
    }
    index_in_full_graph.pop_back();

    // initialize fast query annotation
    // copy annotations from the full graph to the query graph
    auto annotation = slice_annotation(full_annotation,
                                       index_in_full_graph,
                                       num_threads);

    logger->trace("[Query graph construction] Query annotation constructed in {} sec",
                  timer.elapsed());
    timer.reset();

    // build annotated graph from the query graph and copied annotations
    return std::make_unique<AnnotatedDBG>(graph, std::move(annotation));
}


int query_graph(Config *config) {
    assert(config);

    const auto &files = config->fnames;

    assert(config->infbase_annotators.size() == 1);

    auto graph = load_critical_dbg(config->infbase);
    auto anno_graph = initialize_annotated_dbg(graph, *config);

    ThreadPool thread_pool(get_num_threads(), 1000);

    Timer timer;

    QueryExecutor executor(*config, *anno_graph, thread_pool);

    // iterate over input files
    for (const auto &file : files) {
        Timer curr_timer;

        executor.query_fasta(file, [](const std::string &result) { std::cout << result; });
        logger->trace("File '{}' was processed in {} sec, total time: {}", file,
                      curr_timer.elapsed(), timer.elapsed());
    }

    return 0;
}

inline std::string query_sequence(size_t id,
                                  const std::string &name,
                                  const std::string &seq,
                                  const AnnotatedDBG &anno_graph,
                                  const Config &config,
                                  const DBGAlignerConfig *aligner_config) {
    return QueryExecutor::execute_query(fmt::format_int(id).str() + '\t' + name, seq,
                                        config.count_labels, config.print_signature,
                                        config.suppress_unlabeled, config.num_top_labels,
                                        config.discovery_fraction, config.anno_labels_delimiter,
                                        anno_graph, aligner_config);
}


QueryExecutor::QueryExecutor(const Config &config,
                             const AnnotatedDBG &anno_graph,
                             ThreadPool &thread_pool)
      : config_(config),
        anno_graph_(anno_graph),
        aligner_config_(config_.align_sequences
            ? new DBGAlignerConfig(initialize_aligner_config(anno_graph_.get_graph(),
                                                             config_))
            : nullptr),
        thread_pool_(thread_pool) {
    // the fwd_and_reverse argument in the aligner config returns the best of
    // the forward and reverse complement alignments, rather than both.
    // so, we want to prevent it from doing this
    if (aligner_config_)
        aligner_config_->forward_and_reverse_complement = false;
}

void QueryExecutor::query_fasta(const string &file,
                                const std::function<void(const std::string &)> &callback) {
    logger->trace("Parsing sequences from file '{}'", file);

    seq_io::FastaParser fasta_parser(file, config_.forward_and_reverse);

    if (config_.fast) {
        // Construct a query graph and query against it
        batched_query_fasta(fasta_parser, callback);
        return;
    }

    // Query sequences independently

    size_t seq_count = 0;

    for (const seq_io::kseq_t &kseq : fasta_parser) {
        thread_pool_.enqueue(
            [&](size_t id, const std::string &name, std::string &seq) {
                callback(query_sequence(id, name, seq, anno_graph_,
                                        config_, aligner_config_.get()));
            },
            seq_count++,
            std::string(kseq.name.s),
            std::string(kseq.seq.s)
        );
    }

    // wait while all threads finish processing the current file
    thread_pool_.join();
}

void QueryExecutor
::batched_query_fasta(seq_io::FastaParser &fasta_parser,
                      const std::function<void(const std::string &)> &callback) {
    auto begin = fasta_parser.begin();
    auto end = fasta_parser.end();

    const uint64_t batch_size = config_.query_batch_size_in_bytes;
    seq_io::FastaParser::iterator it;

    size_t seq_count = 0;
    size_t sub_k = aligner_config_
        ? aligner_config_->min_seed_length
        : std::numeric_limits<size_t>::max();

    while (begin != end) {
        Timer batch_timer;

        uint64_t num_bytes_read = 0;

        StringGenerator generate_batch = [&](auto call_sequence) {
            num_bytes_read = 0;
            for (it = begin; it != end && num_bytes_read <= batch_size; ++it) {
                call_sequence(it->seq.s);
                num_bytes_read += it->seq.l;
            }
        };

        auto query_graph = construct_query_graph(
            anno_graph_,
            generate_batch,
            config_.count_labels ? 0 : config_.discovery_fraction,
            get_num_threads(),
            anno_graph_.get_graph().is_canonical_mode() || config_.canonical,
            sub_k
        );

        logger->trace("Query graph constructed for batch of {} bytes from '{}' in {} sec",
                      num_bytes_read, fasta_parser.get_filename(), batch_timer.elapsed());

        batch_timer.reset();

        for ( ; begin != it; ++begin) {
            assert(begin != end);

            thread_pool_.enqueue(
                [&](size_t id, const std::string &name, const std::string &seq) {
                    callback(query_sequence(id, name, seq, *query_graph,
                                            config_, aligner_config_.get()));
                },
                seq_count++,
                std::string(begin->name.s),
                std::string(begin->seq.s)
            );
        }

        thread_pool_.join();
        logger->trace("Batch of {} bytes from '{}' queried in {} sec", num_bytes_read,
                      fasta_parser.get_filename(), batch_timer.elapsed());
    }
}

} // namespace cli
} // namespace mtg
