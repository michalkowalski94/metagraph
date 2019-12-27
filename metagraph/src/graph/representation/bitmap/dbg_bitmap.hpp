#ifndef __DBG_BITMAP_HPP__
#define __DBG_BITMAP_HPP__

#include <fstream>

#include "sequence_graph.hpp"
#include "kmer_extractor.hpp"
#include "bit_vector.hpp"

namespace mg {
namespace bitmap_graph {

class DBGBitmapConstructor;


/**
 * Node-centric de Bruijn graph
 *
 * In canonical mode, for each k-mer in the graph, its
 * reverse complement is stored in the graph as well.
 */
class DBGBitmap : public DeBruijnGraph {
    friend DBGBitmapConstructor;

  public:
    // Initialize complete graph
    explicit DBGBitmap(size_t k, bool canonical_mode = false);

    // Initialize graph from builder
    explicit DBGBitmap(DBGBitmapConstructor *builder);

    void add_sequence(const std::string &, bit_vector_dyn * = nullptr) {
        throw std::runtime_error("Not implemented");
    }

    // Traverse graph mapping sequence to the graph nodes
    // and run callback for each node until the termination condition is satisfied
    void map_to_nodes(const std::string &sequence,
                      const std::function<void(node_index)> &callback,
                      const std::function<bool()> &terminate = [](){ return false; }) const;

    // Traverse graph mapping sequence to the graph nodes
    // and run callback for each node until the termination condition is satisfied.
    // Guarantees that nodes are called in the same order as the input sequence.
    // In canonical mode, non-canonical k-mers are NOT mapped to canonical ones
    void map_to_nodes_sequentially(std::string::const_iterator begin,
                                   std::string::const_iterator end,
                                   const std::function<void(node_index)> &callback,
                                   const std::function<bool()> &terminate = [](){ return false; }) const;

    void call_outgoing_kmers(node_index, const OutgoingEdgeCallback&) const;
    void call_incoming_kmers(node_index, const IncomingEdgeCallback&) const;

    // Traverse the outgoing edge
    node_index traverse(node_index node, char next_char) const;
    // Traverse the incoming edge
    node_index traverse_back(node_index node, char prev_char) const;

    // Given a node index, call the target nodes of all edges outgoing from it.
    void adjacent_outgoing_nodes(node_index node,
                                 const std::function<void(node_index)> &callback) const;
    // Given a node index, call the source nodes of all edges incoming to it.
    void adjacent_incoming_nodes(node_index node,
                                 const std::function<void(node_index)> &callback) const;

    size_t outdegree(node_index) const;
    bool has_single_outgoing(node_index) const;
    bool has_multiple_outgoing(node_index) const;

    size_t indegree(node_index) const;
    bool has_no_incoming(node_index) const;
    bool has_single_incoming(node_index) const;

    node_index kmer_to_node(const std::string &kmer) const;

    std::string get_node_sequence(node_index node) const;

    inline size_t get_k() const { return k_; }
    inline bool is_canonical_mode() const { return canonical_mode_; }

    uint64_t num_nodes() const;

    bool in_graph(node_index node) const;

    void serialize(std::ostream &out) const;
    void serialize(const std::string &filename) const;

    bool load(std::istream &in);
    bool load(const std::string &filename);

    std::string file_extension() const { return kExtension; }

    bool operator==(const DBGBitmap &other) const { return equals(other, false); }
    bool operator!=(const DBGBitmap &other) const { return !(*this == other); }

    bool operator==(const DeBruijnGraph &other) const;

    bool equals(const DBGBitmap &other, bool verbose = false) const;

    bool is_complete() const { return complete_; }

    const std::string& alphabet() const { return seq_encoder_.alphabet; }

    void print(std::ostream &out) const;

    using Chunk = bit_vector_smart;

    static constexpr auto kChunkFileExtension = ".dbgsdchunk";

  private:
    using Kmer = KmerExtractor2Bit::Kmer64;

    Vector<std::pair<Kmer, bool>> sequence_to_kmers(const std::string &sequence,
                                                    bool canonical = false) const;

    uint64_t node_to_index(node_index node) const;
    Kmer node_to_kmer(node_index node) const;
    node_index to_node(const Kmer &kmer) const;

    size_t k_;
    bool canonical_mode_;
    KmerExtractor2Bit seq_encoder_;

    bit_vector_smart kmers_;

    bool complete_ = false;

    static constexpr auto kExtension = ".bitmapdbg";
};

} // namespace bitmap_graph
} // namespace mg

#endif // __DBG_BITMAP_HPP__