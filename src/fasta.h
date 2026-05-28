#ifndef _FASTA_H_
#define _FASTA_H_

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// zlib is required for kseq
#include "zlib.h"
#include "htslib/faidx.h"
#include "htslib/kseq.h"
KSEQ_INIT(int, read);

class fastaData {
public:
    fastaData() = default;

    fastaData(FILE * ref_fasta_fp) { this->load_from_stream(ref_fasta_fp); }

    fastaData(
            const std::string & ref_fasta_fn,
            const std::vector<std::string> & contigs_to_load = {}) {
        this->load(ref_fasta_fn, contigs_to_load);
    }

    void load(
            const std::string & ref_fasta_fn,
            const std::vector<std::string> & contigs_to_load = {}) {
        this->fasta.clear();
        this->lengths.clear();

        if (!contigs_to_load.empty()) {
            if (this->load_from_index(ref_fasta_fn, contigs_to_load)) return;
        }

        FILE * ref_fasta_fp = fopen(ref_fasta_fn.data(), "r");
        if (ref_fasta_fp == NULL) {
            fprintf(stderr, "[ERROR vcfdist] Failed to open reference FASTA file '%s'\n",
                    ref_fasta_fn.data());
            std::exit(1);
        }
        if (contigs_to_load.empty()) {
            this->load_from_stream(ref_fasta_fp);
        } else {
            std::unordered_set<std::string> requested(
                    contigs_to_load.begin(), contigs_to_load.end());
            this->load_from_stream(ref_fasta_fp, &requested);
        }
    }

    std::unordered_map<std::string,std::string> fasta;
    std::unordered_map<std::string,int> lengths;

private:
    static void uppercase(std::string & seq) {
        std::transform(seq.begin(), seq.end(), seq.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
    }

    bool load_from_index(
            const std::string & ref_fasta_fn,
            const std::vector<std::string> & contigs_to_load) {
        faidx_t * fai = fai_load3(ref_fasta_fn.data(), NULL, NULL, 0);
        if (fai == NULL) return false;

        bool loaded_all = true;
        for (const std::string & ctg : contigs_to_load) {
            if (this->fasta.find(ctg) != this->fasta.end()) continue;

            int seq_len = faidx_seq_len(fai, ctg.data());
            if (seq_len < 0) {
                loaded_all = false;
                continue;
            }

            int fetched_len = 0;
            char * seq = faidx_fetch_seq(fai, ctg.data(), 0, seq_len-1, &fetched_len);
            if (seq == NULL || fetched_len < 0) {
                if (seq != NULL) free(seq);
                loaded_all = false;
                continue;
            }

            this->fasta[ctg] = std::string(seq, fetched_len);
            free(seq);
            this->uppercase(this->fasta[ctg]);
            this->lengths[ctg] = fetched_len;
        }

        fai_destroy(fai);
        return loaded_all;
    }

    void load_from_stream(
            FILE * ref_fasta_fp,
            const std::unordered_set<std::string> * requested = NULL) {
        kseq_t * seq = kseq_init(fileno(ref_fasta_fp));
        while (kseq_read(seq) >= 0) {
            std::string ctg = seq->name.s;
            if (requested != NULL && requested->find(ctg) == requested->end()) continue;
            this->fasta[ctg] = seq->seq.s;
            this->uppercase(this->fasta[ctg]);
            this->lengths[ctg] = this->fasta.at(ctg).size();
        }
        kseq_destroy(seq);
        fclose(ref_fasta_fp);
    }
};

#endif
