#include "config.hpp"

#include <cstring>
#include <iostream>


Config::Config(int argc, const char *argv[]) {
    // provide help overview if no identity was given
    if (argc == 1) {
        print_usage(argv[0]);
        exit(-1);
    }

    // parse identity from first command line argument
    if (!strcmp(argv[1], "merge")) {
        identity = MERGE;
    } else if (!strcmp(argv[1], "compare")) {
        identity = COMPARE;
    } else if (!strcmp(argv[1], "align")) {
        identity = ALIGN;
    } else if (!strcmp(argv[1], "build")) {
        identity = BUILD;
    } else if (!strcmp(argv[1], "filter")) {
        identity = FILTER;
    } else if (!strcmp(argv[1], "experiment")) {
        identity = EXPERIMENT;
    } else if (!strcmp(argv[1], "stats")) {
        identity = STATS;
    } else if (!strcmp(argv[1], "annotate")) {
        identity = ANNOTATE;
    } else if (!strcmp(argv[1], "classify")) {
        identity = CLASSIFY;
    } else if (!strcmp(argv[1], "transform")) {
        identity = TRANSFORM;
    }

    // provide help screen for chosen identity
    if (argc == 2) {
        print_usage(argv[0], identity);
        exit(-1);
    }

    // parse remaining command line items
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) {
            verbose = true;
        } else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet")) {
            quiet = true;
        } else if (!strcmp(argv[i], "--print")) {
            print_graph_succ = true;
        } else if (!strcmp(argv[i], "--query")) {
            query = true;
        } else if (!strcmp(argv[i], "--traversal")) {
            traversal_merge = true;
        } else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--reverse")) {
            reverse = true;
        } else if (!strcmp(argv[i], "--fast")) {
            fast = true;
        } else if (!strcmp(argv[i], "--fasta-anno")) {
            fasta_anno = true;
        } else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--parallel")) {
            parallel = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--parts-total")) {
            parts_total = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--part-idx")) {
            part_idx = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-b") || !strcmp(argv[i], "--bins-per-thread")) {
            num_bins_per_thread = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-k") || !strcmp(argv[i], "--kmer-length")) {
            k = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--noise-freq")) {
            noise_kmer_frequency = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--mem-cap-gb")) {
            memory_available = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--succinct")) {
            succinct = true;
            //TODO: add into some USAGE description
        } else if (!strcmp(argv[i], "--dump-raw-anno")) {
            dump_raw_anno = true;
            //TODO: add into some USAGE description
        } else if (!strcmp(argv[i], "--bloom-false-pos-prob")) {
            bloom_fpp = std::stof(argv[++i]);
        } else if (!strcmp(argv[i], "--bloom-bits-per-edge")) {
            bloom_bits_per_edge = std::stof(argv[++i]);
        } else if (!strcmp(argv[i], "--discovery-fraction")) {
            discovery_fraction = std::stof(argv[++i]);
        } else if (!strcmp(argv[i], "--bloom-hash-functions")) {
            bloom_num_hash_functions = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--bloom-test-num-kmers")) {
            bloom_test_num_kmers = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-a") || !strcmp(argv[i], "--align-length")) {
            alignment_length = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "--frequency")) {
            frequency = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--distance")) {
            distance = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--outfile-base")) {
            outfbase = std::string(argv[++i]);
        } else if (!strcmp(argv[i], "--reference")) {
            refpath = std::string(argv[++i]);
        } else if (!strcmp(argv[i], "--fasta-header-delimiter")) {
            fasta_header_delimiter = std::string(argv[++i]);
        } else if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--num-splits")) {
            nsplits = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--suffix")) {
            suffix = argv[++i];
        } else if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "--state")) {
            state = static_cast<StateType>(atoi(argv[++i]));
        //} else if (!strcmp(argv[i], "--db-path")) {
        //    dbpath = std::string(argv[++i]);
        } else if (!strcmp(argv[i], "--sql-base")) {
            sqlfbase = std::string(argv[++i]);
        } else if (!strcmp(argv[i], "-i") || !strcmp(argv[i], "--infile-base")) {
            infbase = std::string(argv[++i]);
        } else if (!strcmp(argv[i], "--to-adj-list")) {
            to_adj_list = true;
        } else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--collect")) {
            collect = atoi(argv[++i]);
        //} else if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "--threads")) {
        //    num_threads = atoi(argv[++i]);
        //} else if (!strcmp(argv[i], "--debug")) {
        //    debug = true;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_usage(argv[0], identity);
            exit(0);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "\nERROR: Unknown option %s\n\n", argv[i]);
            print_usage(argv[0], identity);
            exit(-1);
        } else {
            fname.push_back(argv[i]);
        }
    }

    if (!fname.size()) {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.size())
                fname.push_back(line);
        }
    }

    bool print_usage_and_exit = false;

    if (nsplits == 0) {
        std::cerr << "Error: Invalid number of splits" << std::endl;
        print_usage_and_exit = true;
    }

    if (!fname.size())
        print_usage_and_exit = true;

    if (identity == FILTER && noise_kmer_frequency == 0)
        print_usage_and_exit = true;

    if (identity == ALIGN && infbase.empty())
        print_usage_and_exit = true;

    if (identity == CLASSIFY && infbase.empty())
        print_usage_and_exit = true;

    if (identity == ANNOTATE && infbase.empty())
        print_usage_and_exit = true;

    if (identity == MERGE && fname.size() < 2)
        print_usage_and_exit = true;

    if (identity == COMPARE && fname.size() != 2)
        print_usage_and_exit = true;

    // if misused, provide help screen for chosen identity and exit
    if (print_usage_and_exit) {
        print_usage(argv[0], identity);
        exit(-1);
    }
}

void Config::print_usage(const std::string &prog_name, IdentityType identity) {
    fprintf(stderr, "Comprehensive metagenome graph representation -- Version 0.1\n\n");
    //fprintf(stderr, "This program is the first implementation of a\n");
    //fprintf(stderr, "meta-metagenome graph for identification and annotation\n");
    //fprintf(stderr, "purposes.\n\n");

    switch (identity) {
        case NO_IDENTITY: {
            fprintf(stderr, "Usage: %s <command> [command specific options]\n\n", prog_name.c_str());

            fprintf(stderr, "Available commands:\n\n");

            fprintf(stderr, "\texperiment\trun experiments\n\n");

            fprintf(stderr, "\tbuild\t\tconstruct a graph object from input sequence\n");
            fprintf(stderr, "\t\t\tfiles in fast[a|q] formats or integrate sequence\n");
            fprintf(stderr, "\t\t\tfiles in fast[a|q] formats into a given graph\n\n");

            fprintf(stderr, "\tfilter\t\tfilter out reads with rare k-mers\n");
            fprintf(stderr, "\t\t\tand dump the filters to disk\n\n");

            fprintf(stderr, "\tmerge\t\tintegrate a given set of graph structures\n");
            fprintf(stderr, "\t\t\tand output a new graph structure\n\n");

            fprintf(stderr, "\tcompare\t\tcheck whether two given graphs are identical\n\n");

            fprintf(stderr, "\talign\t\talign the reads provided in files in fast[a|q]\n");
            fprintf(stderr, "\t\t\tformats to the graph\n\n");

            fprintf(stderr, "\tstats\t\tprint graph statistics for given graph(s)\n\n");

            fprintf(stderr, "\tannotate\tgiven a graph and a fast[a|q] file, annotate\n");
            fprintf(stderr, "\t\t\tthe respective kmers\n\n");

            fprintf(stderr, "\ttransform\tgiven a graph, transform it to other formats\n\n");

            return;
        }
        case EXPERIMENT: {
            fprintf(stderr, "Usage: %s build [options] FASTQ1 [[FASTQ2] ...]\n\n", prog_name.c_str());

            fprintf(stderr, "Available options for build:\n");
            fprintf(stderr, "\t   --reference [STR] \t\tbasename of reference sequence []\n");
            fprintf(stderr, "\t   --fasta-header-delimiter [STR] \t\theader delimiter (for setting multiple annotations) []\n");
            fprintf(stderr, "\t-o --outfile-base [STR]\t\tbasename of output file []\n");
            fprintf(stderr, "\t   --sql-base [STR] \t\tbasename for SQL output file\n");
            fprintf(stderr, "\t   --mem-cap-gb [INT] \t\tmaximum memory available, in Gb [inf]\n");
            fprintf(stderr, "\t-k --kmer-length [INT] \t\tlength of the k-mer to use [3]\n");
            fprintf(stderr, "\t   --bloom-false-pos-prob [FLOAT] \tFalse positive probability in bloom filter [-1]\n");
            fprintf(stderr, "\t   --bloom-bits-per-edge [FLOAT] \tBits per edge used in bloom filter annotator [0.4]\n");
            fprintf(stderr, "\t   --bloom-hash-functions [INT] \tNumber of hash functions used in bloom filter [off]\n");
            fprintf(stderr, "\t   --bloom-test-num-kmers \tEstimate false positive rate for every n k-mers [0]\n");
            fprintf(stderr, "\t-r --reverse \t\t\tadd reverse complement reads [off]\n");
            fprintf(stderr, "\t   --fast \t\t\tuse fast build method [off]\n");
            fprintf(stderr, "\t   --print \t\t\tprint graph table to the screen [off]\n");
            fprintf(stderr, "\t-s --num-splits \t\tDefine the minimum number of bins to split kmers into [1]\n");
        } break;
        case BUILD: {
            fprintf(stderr, "Usage: %s build [options] FASTQ1 [[FASTQ2] ...]\n\n", prog_name.c_str());

            fprintf(stderr, "Available options for build:\n");
            fprintf(stderr, "\t   --reference [STR] \tbasename of reference sequence []\n");
            fprintf(stderr, "\t-o --outfile-base [STR]\tbasename of output file []\n");
            fprintf(stderr, "\t   --mem-cap-gb [INT] \tmaximum memory available, in Gb [inf]\n");
            fprintf(stderr, "\t-k --kmer-length [INT] \tlength of the k-mer to use [3]\n");
            fprintf(stderr, "\t-r --reverse \t\tadd reverse complement reads [off]\n");
            fprintf(stderr, "\t   --fast \t\tuse fast build method [off]\n");
            fprintf(stderr, "\t   --print \t\tprint graph table to the screen [off]\n");
            fprintf(stderr, "\t   --suffix \t\tbuild graph chunk only for k-mers with the suffix given [off]\n");
            fprintf(stderr, "\t-s --num-splits \tdefine the minimum number of bins to split kmers into [1]\n");
            fprintf(stderr, "\t-p --parallel [INT] \tuse multiple threads for computation [1]\n");
        } break;
        case FILTER: {
            fprintf(stderr, "Usage: %s filter [options] --noise-freq <cutoff> FASTQ1 [[FASTQ2] ...]\n\n", prog_name.c_str());

            fprintf(stderr, "Available options for filter:\n");
            fprintf(stderr, "\t-k --kmer-length [INT] \tlength of the k-mer to use [3]\n");
            fprintf(stderr, "\t-r --reverse \t\tadd reverse complement reads [off]\n");
            fprintf(stderr, "\t-p --parallel [INT] \tuse multiple threads for computation [1]\n");
        } break;
        case ALIGN: {
            fprintf(stderr, "Usage: %s align -i <graph_basename> [options] <FASTQ1> [[FASTQ2] ...]\n\n", prog_name.c_str());

            fprintf(stderr, "Available options for align:\n");
            fprintf(stderr, "\t   --query \tPrint the number of k-mers discovered [off]\n");
            fprintf(stderr, "\t-a --align-length [INT] \tLength of subsequences to align [k]\n");
            fprintf(stderr, "\t-d --distance [INT] \tMax allowed alignment distance [0]\n");
        } break;
        case COMPARE: {
            fprintf(stderr, "Usage: %s compare [options] GRAPH1 GRAPH2\n\n", prog_name.c_str());

            fprintf(stderr, "Available options for compare:\n");
        } break;
        case MERGE: {
            fprintf(stderr, "Usage: %s merge [options] GRAPH1 GRAPH2 [[GRAPH3] ...]\n\n", prog_name.c_str());

            fprintf(stderr, "Available options for merge:\n");
            fprintf(stderr, "\t-o --outfile-base [STR] \tbasename of output file []\n");
            fprintf(stderr, "\t-p --parallel [INT] \t\tuse multiple threads for computation [1]\n");
            fprintf(stderr, "\t-b --bins-per-thread [INT] \tnumber of bins each thread computes on average [1]\n");
            fprintf(stderr, "\t   --traversal \t\t\tmerge by traversing [off]\n");
            fprintf(stderr, "\t   --print \t\t\tprint graph table to the screen [off]\n");
            fprintf(stderr, "\t   --part-idx [INT] \t\tidx to use when doing external merge []\n");
            fprintf(stderr, "\t   --parts-total [INT] \t\ttotal number of parts in external merge[]\n");
            fprintf(stderr, "\t-c --collect [INT] \t\tinitiate collection of external merge, provide total number of splits [1]\n");
        } break;
        case STATS: {
            fprintf(stderr, "Usage: %s stats [options] GRAPH1 [[GRAPH2] ...]\n\n", prog_name.c_str());

            fprintf(stderr, "Available options for stats:\n");
            fprintf(stderr, "\t-o --outfile-base [STR] \tbasename of output file []\n");
            fprintf(stderr, "\t   --print \tprint graph table to the screen [off]\n");
        } break;
        case ANNOTATE: {
            fprintf(stderr, "Usage: %s annotate -i <graph_basename> [options] <PATH1> [[PATH2] ...]\n"
                            "\tEach path is given as file in fasta or fastq format.\n\n", prog_name.c_str());

            fprintf(stderr, "Available options for annotate:\n");
            fprintf(stderr, "\t-r --reverse \t\talso annotate reverse complement reads [off]\n");
            fprintf(stderr, "\t   --fasta-anno \textract annotations from file instead of using filenames [off]\n");
            //fprintf(stderr, "\t   --db-path \tpath that is used to store the annotations database []\n");
            // fprintf(stderr, "\t-p --parallel [INT] \t\tuse multiple threads for computation [1]\n");
            // fprintf(stderr, "\t-b --bins-per-thread [INT] \tnumber of bins each thread computes on average [1]\n");
            // fprintf(stderr, "\t-f --frequency [INT] \t\twhen a, annotate only every a-th kmer [1]\n");
        } break;
        case CLASSIFY: {
            fprintf(stderr, "Usage: %s classify -i <graph_basename> [options] <FILE1> [[FILE2] ...]\n"
                            "\tEach file is given in fasta or fastq format.\n\n", prog_name.c_str());

            fprintf(stderr, "Available options for classify:\n");
            fprintf(stderr, "\t-r --reverse \t\t\tclassify reverse complement sequences [off]\n");
            fprintf(stderr, "\t   --discovery-fraction \tfraction of labeled k-mers required for annotation [1.0]\n");
            // fprintf(stderr, "\t-d --distance [INT] \tMax allowed alignment distance [0]\n");
        } break;
        case TRANSFORM: {
            fprintf(stderr, "Usage: %s transform [options] GRAPH\n\n", prog_name.c_str());

            fprintf(stderr, "\t-o --outfile-base [STR] \tbasename of output file []\n");
            fprintf(stderr, "\t   --to-adj-list \t\twrite the adjacency list to file [off]\n");
        } break;
    }

    fprintf(stderr, "\n\tGeneral options:\n");
    fprintf(stderr, "\t-v --verbose \t\tswitch on verbose output [off]\n");
    fprintf(stderr, "\t-q --quiet \t\tproduce as little log output as posible [off]\n");
    fprintf(stderr, "\t-h --help \t\tprint usage info\n");
    fprintf(stderr, "\n");
}
