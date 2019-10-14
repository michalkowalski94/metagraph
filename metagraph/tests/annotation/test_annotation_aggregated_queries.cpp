#include "gtest/gtest.h"

#include "test_annotation.hpp"


std::vector<std::string> get_labels(const annotate::MultiLabelEncoded<uint64_t, std::string> &annotator,
                                    const std::vector<uint64_t> &indices,
                                    double min_label_frequency = 0.0) {
    const auto& label_encoder = annotator.get_label_encoder();
    const size_t min_count = std::max(1.0,
                                      std::ceil(min_label_frequency * indices.size()));

    std::vector<std::pair<std::string, size_t>> label_counts;
    for (size_t j = 0; j < label_encoder.size(); ++j) {
        label_counts.emplace_back(label_encoder.decode(j), 0);
    }

    annotator.call_rows(
        indices,
        [&](auto&& label_indices) {
            for (auto j : label_indices) {
                label_counts[j].second++;
            }
        },
        [&]() {
            return std::all_of(label_counts.begin(), label_counts.end(),
                               [&](const auto &pair) { return pair.second >= min_count; });
        }
    );

    std::vector<std::string> labels;
    for (auto&& pair : label_counts) {
        if (pair.second >= min_count)
            labels.emplace_back(std::move(pair.first));
    }

    return labels;
}

TYPED_TEST(AnnotatorPresetTest, call_rows_get_labels) {
    EXPECT_EQ(std::vector<std::string>({}),
              get_labels(*this->annotation, {}, 1));

    EXPECT_EQ(convert_to_set({"Label1", "Label2"}),
              convert_to_set(get_labels(*this->annotation, { 2 }, 1)));

    EXPECT_EQ(convert_to_set({"Label1", "Label2"}),
              convert_to_set(get_labels(*this->annotation, { 2 }, 0)));

    EXPECT_EQ(convert_to_set({"Label1", "Label2"}),
              convert_to_set(get_labels(*this->annotation, { 2 }, 1)));

    EXPECT_EQ(convert_to_set({"Label1", "Label2"}),
              convert_to_set(get_labels(*this->annotation, { 2 }, 1)));

    EXPECT_EQ(convert_to_set({"Label1", "Label2"}),
              convert_to_set(get_labels(*this->annotation, { 2 }, 0.5)));

    EXPECT_EQ(convert_to_set({"Label2"}),
              convert_to_set(get_labels(*this->annotation, { 2, 4 }, 1)));

    EXPECT_EQ(convert_to_set({"Label1", "Label2"}),
              convert_to_set(get_labels(*this->annotation, { 2, 4 }, 0)));

    EXPECT_EQ(convert_to_set({"Label1", "Label2"}),
              convert_to_set(get_labels(*this->annotation, { 2, 4 }, 0.5)));

    EXPECT_EQ(convert_to_set({"Label2"}),
              convert_to_set(get_labels(*this->annotation, { 2, 4 }, 0.501)));

    EXPECT_EQ(convert_to_set({"Label2"}),
              convert_to_set(get_labels(*this->annotation, { 2, 4 }, 1)));

    EXPECT_EQ(convert_to_set({}),
              convert_to_set(get_labels(*this->annotation, { 0, 1, 2, 3, 4 }, 1)));

    EXPECT_EQ(convert_to_set({"Label0", "Label1", "Label2", "Label8"}),
              convert_to_set(get_labels(*this->annotation, { 0, 1, 2, 3, 4 }, 0)));

    EXPECT_EQ(convert_to_set({"Label0", "Label1", "Label2", "Label8"}),
              convert_to_set(get_labels(*this->annotation, { 0, 1, 2, 3, 4 }, 0.2)));

    EXPECT_EQ(convert_to_set({"Label1", "Label2", "Label8"}),
              convert_to_set(get_labels(*this->annotation, { 0, 1, 2, 3, 4 }, 0.201)));

    EXPECT_EQ(convert_to_set({"Label1", "Label2", "Label8"}),
              convert_to_set(get_labels(*this->annotation, { 0, 1, 2, 3, 4 }, 0.4)));

    EXPECT_EQ(convert_to_set({"Label2"}),
              convert_to_set(get_labels(*this->annotation, { 0, 1, 2, 3, 4 }, 0.401)));

    EXPECT_EQ(convert_to_set({"Label2"}),
              convert_to_set(get_labels(*this->annotation, { 0, 1, 2, 3, 4 }, 0.8)));

    EXPECT_EQ(convert_to_set({}),
              convert_to_set(get_labels(*this->annotation, { 0, 1, 2, 3, 4 }, 0.801)));

    EXPECT_EQ(convert_to_set({}),
              convert_to_set(get_labels(*this->annotation, { 0, 1, 2, 3, 4 }, 1)));
}

std::vector<std::pair<std::string, size_t>>
count_labels(const annotate::ColumnCompressed<> &annotation,
             const std::unordered_map<uint64_t, size_t> &index_counts,
             std::function<bool(size_t /* checked */,
                                size_t /* matched */)> stop_counting_for_label);

std::vector<std::pair<std::string, size_t>>
count_labels(const annotate::MultiLabelEncoded<uint64_t, std::string> &annotation,
             const std::unordered_map<uint64_t, size_t> &index_counts,
             std::function<bool(size_t /* checked */,
                                size_t /* min_matched */,
                                size_t /* max_matched */)> /* stop_counting_labels */);


std::vector<std::string> get_labels_by_label(const annotate::MultiLabelEncoded<uint64_t, std::string> &annotator,
                                             const std::vector<uint64_t> &indices,
                                             double min_label_frequency = 0.0) {
    std::unordered_map<uint64_t, size_t> index_counts;
    for (auto i : indices) {
        index_counts[i] = 1;
    }

    const size_t min_count = std::max(1.0,
                                      std::ceil(min_label_frequency * indices.size()));

    auto label_counts
        = dynamic_cast<const annotate::ColumnCompressed<>*>(&annotator)
            // Iterate by column instead of by row for column-major annotators
            ? count_labels(dynamic_cast<const annotate::ColumnCompressed<>&>(annotator),
                           index_counts,
                           [&](size_t checked, size_t matched) {
                               return matched >= min_count
                                        || matched + (indices.size() - checked) < min_count;
                           })
            : count_labels(annotator,
                           index_counts,
                           [&](size_t checked, size_t min_matched, size_t max_matched) {
                               return min_matched >= min_count
                                        || max_matched + (indices.size() - checked) < min_count;
                           });

    std::vector<std::string> labels;
    for (size_t i = 0; i < label_counts.size(); ++i) {
        if (label_counts[i].second >= min_count)
            labels.emplace_back(std::move(label_counts[i].first));
    }

    return labels;
}

TYPED_TEST(AnnotatorPresetTest, call_rows_get_labels_by_label) {
    EXPECT_EQ(std::vector<std::string>({}),
              get_labels(*this->annotation, {}, 1));

    EXPECT_EQ(convert_to_set({"Label1", "Label2"}),
              convert_to_set(get_labels_by_label(*this->annotation, { 2 }, 1)));

    EXPECT_EQ(convert_to_set({"Label1", "Label2"}),
              convert_to_set(get_labels_by_label(*this->annotation, { 2 }, 0)));

    EXPECT_EQ(convert_to_set({"Label1", "Label2"}),
              convert_to_set(get_labels_by_label(*this->annotation, { 2 }, 1)));

    EXPECT_EQ(convert_to_set({"Label1", "Label2"}),
              convert_to_set(get_labels_by_label(*this->annotation, { 2 }, 1)));

    EXPECT_EQ(convert_to_set({"Label1", "Label2"}),
              convert_to_set(get_labels_by_label(*this->annotation, { 2 }, 0.5)));

    EXPECT_EQ(convert_to_set({"Label2"}),
              convert_to_set(get_labels_by_label(*this->annotation, { 2, 4 }, 1)));

    EXPECT_EQ(convert_to_set({"Label1", "Label2"}),
              convert_to_set(get_labels_by_label(*this->annotation, { 2, 4 }, 0)));

    EXPECT_EQ(convert_to_set({"Label1", "Label2"}),
              convert_to_set(get_labels_by_label(*this->annotation, { 2, 4 }, 0.5)));

    EXPECT_EQ(convert_to_set({"Label2"}),
              convert_to_set(get_labels_by_label(*this->annotation, { 2, 4 }, 0.501)));

    EXPECT_EQ(convert_to_set({"Label2"}),
              convert_to_set(get_labels_by_label(*this->annotation, { 2, 4 }, 1)));

    EXPECT_EQ(convert_to_set({}),
              convert_to_set(get_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }, 1)));

    EXPECT_EQ(convert_to_set({"Label0", "Label1", "Label2", "Label8"}),
              convert_to_set(get_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }, 0)));

    EXPECT_EQ(convert_to_set({"Label0", "Label1", "Label2", "Label8"}),
              convert_to_set(get_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }, 0.2)));

    EXPECT_EQ(convert_to_set({"Label1", "Label2", "Label8"}),
              convert_to_set(get_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }, 0.201)));

    EXPECT_EQ(convert_to_set({"Label1", "Label2", "Label8"}),
              convert_to_set(get_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }, 0.4)));

    EXPECT_EQ(convert_to_set({"Label2"}),
              convert_to_set(get_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }, 0.401)));

    EXPECT_EQ(convert_to_set({"Label2"}),
              convert_to_set(get_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }, 0.8)));

    EXPECT_EQ(convert_to_set({}),
              convert_to_set(get_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }, 0.801)));

    EXPECT_EQ(convert_to_set({}),
              convert_to_set(get_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }, 1)));
}


std::vector<std::pair<std::string, size_t>>
get_top_labels(const annotate::MultiLabelEncoded<uint64_t, std::string> &annotator,
               const std::vector<uint64_t> &indices,
               size_t num_top_labels = static_cast<size_t>(-1),
               double min_label_frequency = 0.0) {
    const auto& label_encoder = annotator.get_label_encoder();
    const size_t min_count = std::max(1.0,
                                      std::ceil(min_label_frequency * indices.size()));

    std::vector<std::pair<std::string, size_t>> label_counts;
    for (size_t i = 0; i < label_encoder.size(); ++i) {
        label_counts.emplace_back(label_encoder.decode(i), 0);
    }

    annotator.call_rows(
        indices,
        [&](auto&& label_indices) {
            for (auto j : label_indices) {
                label_counts[j].second++;
            }
        }
    );

    std::sort(label_counts.begin(), label_counts.end(),
              [](const auto &first, const auto &second) {
                  return first.second > second.second;
              });

    // remove labels which don't meet num_top_labels and min_label_frequency criteria
    label_counts.erase(std::find_if(label_counts.begin(),
                                    num_top_labels < label_counts.size()
                                        ? label_counts.begin() + num_top_labels
                                        : label_counts.end(),
                                    [&](const auto &pair) {
                                        return pair.second < min_count;
                                    }),
                       label_counts.end());

    return label_counts;
}

TYPED_TEST(AnnotatorPreset3Test, call_rows_get_top_labels) {
    typedef std::vector<std::pair<std::string, size_t>> VectorCounts;
    EXPECT_EQ(VectorCounts({}),
              get_top_labels(*this->annotation, { 0, 1, 2, 3, 4 }, 0));

    EXPECT_EQ(VectorCounts({}),
              get_top_labels(*this->annotation, {}));

    EXPECT_EQ(VectorCounts({ std::make_pair("Label2", 4),
                             std::make_pair("Label8", 3),
                             std::make_pair("Label1", 2),
                             std::make_pair("Label0", 1) }),
              get_top_labels(*this->annotation, { 0, 1, 2, 3, 4 }));

    EXPECT_EQ(to_set(VectorCounts({ std::make_pair("Label1", 1),
                                    std::make_pair("Label2", 1) })),
              to_set(get_top_labels(*this->annotation, { 2 })));

    EXPECT_EQ(VectorCounts({}),
              get_top_labels(*this->annotation, { 0, 1, 2, 3, 4 }, 0));

    EXPECT_EQ(VectorCounts({ std::make_pair("Label2", 4) }),
              get_top_labels(*this->annotation, { 0, 1, 2, 3, 4 }, 1));

    EXPECT_EQ(VectorCounts({ std::make_pair("Label2", 4),
                             std::make_pair("Label8", 3) }),
              get_top_labels(*this->annotation, { 0, 1, 2, 3, 4 }, 2));

    EXPECT_EQ(VectorCounts({ std::make_pair("Label2", 4),
                             std::make_pair("Label8", 3),
                             std::make_pair("Label1", 2) }),
              get_top_labels(*this->annotation, { 0, 1, 2, 3, 4 }, 3));

    EXPECT_EQ(VectorCounts({ std::make_pair("Label2", 4),
                             std::make_pair("Label8", 3),
                             std::make_pair("Label1", 2),
                             std::make_pair("Label0", 1) }),
              get_top_labels(*this->annotation, { 0, 1, 2, 3, 4 }, 4));

    EXPECT_EQ(VectorCounts({ std::make_pair("Label2", 4),
                             std::make_pair("Label8", 3),
                             std::make_pair("Label1", 2),
                             std::make_pair("Label0", 1) }),
              get_top_labels(*this->annotation, { 0, 1, 2, 3, 4 }, 1000));
}

std::vector<std::pair<std::string, size_t>>
get_top_labels_by_label(const annotate::MultiLabelEncoded<uint64_t, std::string> &annotator,
                        const std::vector<uint64_t> &indices,
                        size_t num_top_labels = static_cast<size_t>(-1),
                        double min_label_frequency = 0.0) {
    const size_t min_count = std::max(1.0,
                                      std::ceil(min_label_frequency * indices.size()));

    std::unordered_map<uint64_t, size_t> index_counts;
    for (auto i : indices) {
        index_counts[i] = 1;
    }

    auto label_counts
        = dynamic_cast<const annotate::ColumnCompressed<>*>(&annotator)
            // Iterate by column instead of by row for column-major annotators
            ? count_labels(dynamic_cast<const annotate::ColumnCompressed<>&>(annotator),
                           index_counts,
                           [&](size_t checked, size_t matched) {
                               return matched + (indices.size() - checked) < min_count;
                           })
            : count_labels(annotator,
                           index_counts,
                           [&](size_t checked, size_t /* min_matched */, size_t max_matched) {
                               return max_matched + (indices.size() - checked) < min_count;
                           });

    std::sort(label_counts.begin(), label_counts.end(),
              [](const auto &first, const auto &second) {
                  return first.second > second.second;
              });

    // remove labels which don't meet num_top_labels and min_label_frequency criteria
    label_counts.erase(std::find_if(label_counts.begin(),
                                    num_top_labels < label_counts.size()
                                        ? label_counts.begin() + num_top_labels
                                        : label_counts.end(),
                                    [&](const auto &pair) {
                                        return pair.second < min_count;
                                    }),
                       label_counts.end());

    return label_counts;
}

TYPED_TEST(AnnotatorPreset3Test, call_rows_get_top_labels_by_label) {
    typedef std::vector<std::pair<std::string, size_t>> VectorCounts;
    EXPECT_EQ(VectorCounts({}),
              get_top_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }, 0));

    EXPECT_EQ(VectorCounts({}),
              get_top_labels_by_label(*this->annotation, {}));

    EXPECT_EQ(VectorCounts({ std::make_pair("Label2", 4),
                             std::make_pair("Label8", 3),
                             std::make_pair("Label1", 2),
                             std::make_pair("Label0", 1) }),
              get_top_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }));

    EXPECT_EQ(to_set(VectorCounts({ std::make_pair("Label1", 1),
                                    std::make_pair("Label2", 1) })),
              to_set(get_top_labels_by_label(*this->annotation, { 2 })));

    EXPECT_EQ(VectorCounts({}),
              get_top_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }, 0));

    EXPECT_EQ(VectorCounts({ std::make_pair("Label2", 4) }),
              get_top_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }, 1));

    EXPECT_EQ(VectorCounts({ std::make_pair("Label2", 4),
                             std::make_pair("Label8", 3) }),
              get_top_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }, 2));

    EXPECT_EQ(VectorCounts({ std::make_pair("Label2", 4),
                             std::make_pair("Label8", 3),
                             std::make_pair("Label1", 2) }),
              get_top_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }, 3));

    EXPECT_EQ(VectorCounts({ std::make_pair("Label2", 4),
                             std::make_pair("Label8", 3),
                             std::make_pair("Label1", 2),
                             std::make_pair("Label0", 1) }),
              get_top_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }, 4));

    EXPECT_EQ(VectorCounts({ std::make_pair("Label2", 4),
                             std::make_pair("Label8", 3),
                             std::make_pair("Label1", 2),
                             std::make_pair("Label0", 1) }),
              get_top_labels_by_label(*this->annotation, { 0, 1, 2, 3, 4 }, 1000));
}