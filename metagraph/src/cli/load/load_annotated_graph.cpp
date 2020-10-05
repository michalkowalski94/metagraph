#include "load_annotated_graph.hpp"

#include "annotation/binary_matrix/row_diff/row_diff.hpp"
#include "graph/representation/canonical_dbg.hpp"
#include "common/logger.hpp"
#include "load_graph.hpp"
#include "load_annotation.hpp"


namespace mtg {
namespace cli {

using namespace mtg::graph;

using mtg::common::logger;


std::unique_ptr<AnnotatedDBG> initialize_annotated_dbg(std::shared_ptr<DeBruijnGraph> graph,
                                                       const Config &config) {
    // TODO: check and wrap into canonical only if the graph is primary
    if (config.canonical && !graph->is_canonical_mode() && config.identity != Config::ASSEMBLE)
        graph = std::make_shared<CanonicalDBG>(graph, true);

    uint64_t max_index = graph->max_index();
    if (const auto *canonical = dynamic_cast<const CanonicalDBG*>(graph.get()))
        max_index = canonical->get_graph().max_index();

    auto annotation_temp = config.infbase_annotators.size()
            ? initialize_annotation(config.infbase_annotators.at(0), config, 0)
            : initialize_annotation(config.anno_type, config, max_index);

    if (config.infbase_annotators.size()) {
        if (!annotation_temp->load(config.infbase_annotators.at(0))) {
            logger->error("Cannot load annotations for graph {}, file corrupted",
                          config.infbase);
            exit(1);
        }
        const Config::AnnotationType input_anno_type
                = parse_annotation_type(config.infbase_annotators.at(0));
        // row_diff annotation is special, as it must know the graph structure
        if (input_anno_type == Config::AnnotationType::RowDiff) {
            auto dbg_graph = dynamic_cast<const DBGSuccinct *>(graph.get());

            if (!dbg_graph) {
                if (auto *canonical = dynamic_cast<CanonicalDBG *>(graph.get()))
                    dbg_graph = dynamic_cast<const DBGSuccinct *>(&canonical->get_graph());
            }

            if (!dbg_graph) {
                logger->error(
                        "Only succinct de Bruijn graph representations are "
                        "supported for row-diff annotations");
                std::exit(1);
            }
            // this is really ugly, but the alternative is to add a set_graph method to
            // all annotations
            dynamic_cast<annot::binmat::RowDiff &>(
                    const_cast<annot::binmat::BinaryMatrix &>(annotation_temp->get_matrix()))
                    .set_graph(dbg_graph);
        }
    }


    // load graph
    auto anno_graph
            = std::make_unique<AnnotatedDBG>(std::move(graph), std::move(annotation_temp));

    if (!anno_graph->check_compatibility()) {
        logger->error("Graph and annotation are not compatible");
        exit(1);
    }

    return anno_graph;
}

std::unique_ptr<AnnotatedDBG> initialize_annotated_dbg(const Config &config) {
    return initialize_annotated_dbg(load_critical_dbg(config.infbase), config);
}

} // namespace cli
} // namespace mtg
