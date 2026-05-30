#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <set>
#include <vector>
#include <cstdio>
#include <utility>
#include <queue>
#include <thread>

#include "dist.h"
#include "edit.h"
#include "print.h"
#include "cluster.h"

template <typename T>
inline bool contains(const std::unordered_set<T> & wave, const T & idx) {
    return wave.find(idx) != wave.end();
}
template <typename T, typename U>
inline bool contains(const std::unordered_map<T,U> & wave, const T & idx) {
    return wave.find(idx) != wave.end();
}

template <typename T>
struct RefList4 {
    const T* v[4];

    const T& operator[](size_t i) const {
        return *v[i];
    }
};

class PrThreadPool {
    std::deque<std::function<void()>> q_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::vector<std::thread> workers_;
    std::atomic<bool> stopped_{false};
    std::atomic<int> pending_{0};

    void loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [this]{ return stopped_.load() || !q_.empty(); });
                if (stopped_.load() && q_.empty()) return;
                task = std::move(q_.front());
                q_.pop_front();
            }
            task();
            pending_.fetch_sub(1, std::memory_order_release);
        }
    }

public:
    explicit PrThreadPool(int n) {
        workers_.reserve(n);
        for (int i = 0; i < n; i++)
            workers_.emplace_back([this]{ loop(); });
    }

    ~PrThreadPool() {
        stopped_.store(true);
        cv_.notify_all();
        for (auto & w : workers_) w.join();
    }

    void submit(std::function<void()> f) {
        pending_.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            q_.push_back(std::move(f));
        }
        cv_.notify_one();
    }

    bool try_steal_exec() {
        std::function<void()> task;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (q_.empty()) return false;
            task = std::move(q_.front());
            q_.pop_front();
        }
        task();
        pending_.fetch_sub(1, std::memory_order_release);
        return true;
    }

    void wait_idle() {
        while (pending_.load(std::memory_order_acquire) > 0) {
            if (!try_steal_exec()) std::this_thread::yield();
        }
    }

    int size() const { return static_cast<int>(workers_.size()); }
};

static PrThreadPool* g_pr_pool = nullptr;

/******************************************************************************/


int calc_ng50(std::vector<int> phase_blocks, size_t total_bases) {
    size_t total_phased = 0;
    std::sort(phase_blocks.begin(), phase_blocks.end(), std::greater<>());
    for (size_t i = 0; i < phase_blocks.size(); i++) {
        total_phased += phase_blocks[i];
        if (total_phased >= total_bases / 2)
            return phase_blocks[i];
    }
    return 0;
}


/******************************************************************************/


/* Reverse two strings and their associated pointers for reverse alignment. */
void reverse_ptrs_strs(
        std::string & query, std::string & ref,
        std::vector< std::vector<int> > & query_ptrs, 
        std::vector< std::vector<int> > & ref_ptrs) {

    // reverse strings and vectors
    std::reverse(query.begin(), query.end());
    std::reverse(ref.begin(), ref.end());
    std::reverse(query_ptrs[PTRS].begin(), query_ptrs[PTRS].end());
    std::reverse(query_ptrs[FLAGS].begin(), query_ptrs[FLAGS].end());
    std::reverse(ref_ptrs[PTRS].begin(), ref_ptrs[PTRS].end());
    std::reverse(ref_ptrs[FLAGS].begin(), ref_ptrs[FLAGS].end());

    // update pointers
    for (int & ptr : query_ptrs[PTRS]) { ptr = ref.size()-1 - ptr; }
    for (int & ptr : ref_ptrs[PTRS]) { ptr = query.size()-1 - ptr; }
    for (int & flags : query_ptrs[FLAGS]) { // flip beg/end if only one is set
        if (flags & PTR_VAR_BEG && !(flags & PTR_VAR_END)) {
            flags = PTR_VARIANT | PTR_VAR_END;
        } else if (flags & PTR_VAR_END && !(flags & PTR_VAR_BEG)) {
            flags = PTR_VARIANT | PTR_VAR_BEG;
        }
    }
    for (int & flags : ref_ptrs[FLAGS]) { // flip beg/end if only one is set
        if (flags & PTR_VAR_BEG && !(flags & PTR_VAR_END)) {
            flags = PTR_VARIANT | PTR_VAR_END;
        } else if (flags & PTR_VAR_END && !(flags & PTR_VAR_BEG)) {
            flags = PTR_VARIANT | PTR_VAR_BEG;
        }
    }
    return;
}


/******************************************************************************/


/* Generate the new sequence by applying variants to the reference. */
std::string generate_str(
        std::shared_ptr<fastaData> ref, 
        std::shared_ptr<ctgVariants> vars, const std::string & ctg,
        int beg_idx, int end_idx, int beg_pos, int end_pos, 
        int min_qual /* = 0 */) {

    try {
        return generate_str(ref->fasta.at(ctg), vars, ctg,
                beg_idx, end_idx, beg_pos, end_pos, min_qual);
    } catch (const std::out_of_range & e) {
        ERROR("Contig '%s' not in reference FASTA or position out of range (generate_str)", ctg.data());
    }
    return "";
}


std::string generate_str(
        const std::string & ref_seq,
        std::shared_ptr<ctgVariants> vars, const std::string & ctg,
        int beg_idx, int end_idx, int beg_pos, int end_pos,
        int min_qual /* = 0 */) {

    int var_idx = beg_idx;
    std::string str = "";
    str.reserve(std::max(0, end_pos - beg_pos));
    while (var_idx < vars->n && vars->poss[var_idx] < beg_pos) var_idx++;
    for (int ref_pos = beg_pos; ref_pos < end_pos; ) {

        // VARIANT
        if (var_idx < end_idx && ref_pos == vars->poss[var_idx]) {
            if (vars->var_quals[var_idx] >= min_qual) {
                switch (vars->types[var_idx]) {
                    case TYPE_INS:
                        str += vars->alts[var_idx];
                        break;
                    case TYPE_DEL:
                        ref_pos += vars->refs[var_idx].size();
                        break;
                    case TYPE_SUB:
                        str += vars->alts[var_idx];
                        ref_pos++;
                        break;
                    case TYPE_CPX:
                        str += vars->alts[var_idx];
                        ref_pos += vars->refs[var_idx].size();
                        break;
                }
            }
            var_idx++; // next variant

        } else { // NO VARIANT
            try {
                // add entire ref seq up to next variant or region end
                int ref_end = (var_idx < end_idx) ? 
                    std::min(end_pos, vars->poss[var_idx]) : end_pos;
                if (ref_end < ref_pos) {
                    printf("ref_pos:%d ref_end:%d beg_pos: %d end_pos: %d\n",
                            ref_pos, ref_end, beg_pos, end_pos);
                    for(int i = beg_idx; i < end_idx; i++) {
                        printf("%s:%d %s -> %s\n", ctg.data(), vars->poss[i], 
                                vars->refs[i].data(), vars->alts[i].data());
                    }
                    ERROR("No variant, but ref_end < ref_pos (generate_str)");
                }
                str.append(ref_seq, ref_pos, ref_end-ref_pos);
                ref_pos = ref_end;
            } catch (const std::out_of_range & e) {
                ERROR("Contig '%s' not in reference FASTA or position out of range (generate_str)", ctg.data());
            }
        }
    }
    return str;
}


/******************************************************************************/


/* For each position on hap1 and hap2, it generates pointers from the query to 
 * the reference and vice versa, as well as the strings for alignment.
 */
void generate_ptrs_strs(
        std::string & query_str, std::string & ref_str,
        std::vector< std::vector<int> > & query_ptrs, 
        std::vector< std::vector<int> > & ref_ptrs,
        std::shared_ptr<ctgVariants> query_vars,
        size_t query_clust_beg_idx, size_t query_clust_end_idx,
        int beg_pos, int end_pos, 
        std::shared_ptr<fastaData> ref, const std::string & ctg,
        int min_qual /* = 0 */
        ) {

    // generate query and ref strings and pointers
    query_ptrs.resize(PTR_DIMS);
    ref_ptrs.resize(PTR_DIMS);
    const std::string * ref_seq_ptr = NULL;
    try {
        ref_seq_ptr = &ref->fasta.at(ctg);
    } catch (const std::out_of_range & e) {
        ERROR("Contig '%s' not present in reference FASTA", ctg.data());
    }
    const std::string & ref_seq = *ref_seq_ptr;
    int ref_region_len = std::max(0, end_pos - beg_pos + 1);
    query_str.reserve(query_str.size() + ref_region_len);
    ref_str.reserve(ref_str.size() + ref_region_len);
    for (int dim = 0; dim < PTR_DIMS; dim++) {
        query_ptrs[dim].reserve(query_ptrs[dim].size() + ref_region_len);
        ref_ptrs[dim].reserve(ref_ptrs[dim].size() + ref_region_len);
    }
    int query_var_idx = query_vars->clusters.size() ? 
            query_vars->clusters[query_clust_beg_idx] : 0;
    int query_end_idx = query_vars->clusters.size() ? 
            query_vars->clusters[query_clust_end_idx] : 0;
    for (int ref_pos = beg_pos; ref_pos <= end_pos; ) {

        if (query_var_idx < query_end_idx && 
                ref_pos == query_vars->poss[query_var_idx]) { // start query variant
            if (query_vars->var_quals[query_var_idx] >= min_qual) {
                switch (query_vars->types[query_var_idx]) {
                    case TYPE_INS:
                        query_ptrs[PTRS].insert(query_ptrs[PTRS].end(), 
                                query_vars->alts[query_var_idx].size(), ref_str.size()-1);
                        query_ptrs[FLAGS].insert(query_ptrs[FLAGS].end(),
                                query_vars->alts[query_var_idx].size(), PTR_VARIANT);
                        query_ptrs[FLAGS][query_ptrs[FLAGS].size()-1] |= PTR_VAR_END;
                        query_ptrs[FLAGS][query_ptrs[FLAGS].size() - 
                            query_vars->alts[query_var_idx].size()] |= PTR_VAR_BEG | PTR_INS_LOC;
                        query_str += query_vars->alts[query_var_idx];
                        break;
                    case TYPE_DEL:
                        ref_ptrs[PTRS].insert(ref_ptrs[PTRS].end(),
                                query_vars->refs[query_var_idx].size(), query_str.size()-1);
                        ref_ptrs[FLAGS].insert(ref_ptrs[FLAGS].end(),
                                query_vars->refs[query_var_idx].size(), PTR_VARIANT);
                        ref_ptrs[FLAGS][ref_ptrs[FLAGS].size()-1] |= PTR_VAR_END;
                        ref_ptrs[FLAGS][ref_ptrs[FLAGS].size() - 
                            query_vars->refs[query_var_idx].size()] |= PTR_VAR_BEG;
                        ref_str += query_vars->refs[query_var_idx];
                        ref_pos += query_vars->refs[query_var_idx].size();
                        break;
                    case TYPE_SUB:
                        ref_ptrs[PTRS].push_back(query_str.size());
                        ref_ptrs[FLAGS].push_back(PTR_VARIANT|PTR_VAR_BEG|PTR_VAR_END);
                        query_ptrs[PTRS].push_back(ref_str.size());
                        query_ptrs[FLAGS].push_back(PTR_VARIANT|PTR_VAR_BEG|PTR_VAR_END);
                        ref_str += query_vars->refs[query_var_idx];
                        query_str += query_vars->alts[query_var_idx];
                        ref_pos++;
                        break;
                    default:
                        ERROR("Unexpected variant type '%d' in generate_ptrs_strs()",
                                query_vars->types[query_var_idx]);
                }
            }
            query_var_idx++; // next variant

        } else { // add all matching ref bases

            try {
                // find next position w/o ref match (next var or end)
                int ref_end = (query_var_idx < query_end_idx) ?
                    query_vars->poss[query_var_idx] : end_pos+1;

                int match_len = ref_end - ref_pos;
                int ref_start = ref_str.size();
                int query_start = query_str.size();

                // add pointers, query
                query_ptrs[FLAGS].insert(query_ptrs[FLAGS].end(), match_len, 0);
                query_ptrs[PTRS].reserve(query_ptrs[PTRS].size() + match_len);
                for (int i = 0; i < match_len; i++)
                    query_ptrs[PTRS].push_back(ref_start + i);

                // add pointers, ref
                ref_ptrs[FLAGS].insert(ref_ptrs[FLAGS].end(), match_len, 0);
                ref_ptrs[PTRS].reserve(ref_ptrs[PTRS].size() + match_len);
                for (int i = 0; i < match_len; i++)
                    ref_ptrs[PTRS].push_back(query_start + i);

                // add sequence, update positions
                query_str.append(ref_seq, ref_pos, match_len);
                ref_str.append(ref_seq, ref_pos, match_len);
                ref_pos = ref_end;

            } catch (const std::out_of_range & e) {
                ERROR("Contig '%s' not present in reference FASTA", ctg.data());
            }
        }
    }
}


/******************************************************************************/

/* Calculate initial forward-pass alignment for truth and query strings, given
 * pointers to/from reference. This function generates the pointer matrix, 
 * scores for each alignment, and pointer to if alignment ends on QUERY/REF.
 */
// Cell states for the precision/recall wavefront BFS frontier.  A single
// state matrix replaces the old `done` bool matrix plus the curr_wave/prev_wave
// hash-set membership tests: PR_NEW means never reached, PR_IN_WAVE means
// reached this wave but not yet committed, PR_DONE means committed.
static constexpr uint8_t PR_NEW = 0, PR_IN_WAVE = 1, PR_DONE = 2;

// Cell count threshold: alignments larger than this use the WFA corridor path.
static constexpr size_t PR_WFA_CORRIDOR_THRESHOLD = 1000000;

/*
 * WFA-corridor alignment for one (query/ref, truth) alignment triplet.
 *
 * Phase 1: WFA diagonal-offset sweep discovers the optimal edit distance and
 *   builds two flat corridor sets (one per space: QUERY and REF).  A diagonal
 *   d = qri - ti is added to the corridor when any cell on that diagonal is
 *   reached by the WFA sweep.  The corridor is grown monotonically across all
 *   score levels so it is a superset of all diagonals reachable on any
 *   optimal path.
 *
 * Phase 2: Corridor-bounded BFS fills ptrs and swap_pred_map using the same
 *   logic as the main BFS but only enqueues cells whose diagonal is inside the
 *   corridor.  This is correct because the corridor is a superset of the
 *   diagonals explored by any optimal path.
 *
 * Correctness invariant: no optimal-path cell is excluded because the corridor
 *   is built from WFA which is guaranteed to find the shortest path.  The BFS
 *   may also fill some extra non-optimal cells (within the corridor), which is
 *   harmless for the backtracker.
 */
static void calc_prec_recall_aln_one_wfa(
        int i,
        const RefList4<std::string> & query,
        const RefList4<std::string> & truth,
        const std::string & ref,
        const RefList4< std::vector< std::vector<int> > > & query_ref_ptrs,
        const RefList4< std::vector< std::vector<int> > > & ref_query_ptrs,
        const RefList4< std::vector< std::vector<int> > > & truth_ref_ptrs,
        int qi, int ri,
        int query_len, int truth_len, int ref_len,
        int & score_out,
        std::vector< std::vector< std::vector<uint8_t> > > & ptrs,
        std::unordered_map<idx1, idx1> & swap_pred_map,
        int & pr_query_ref_end
        ) {

    // -----------------------------------------------------------------------
    // Phase 1: WFA diagonal-offset sweep to discover corridor diagonals.
    //
    // Diagonal: d = qri - ti.  Offset: qri (the position along qri axis).
    // wfa_q[d] = furthest qri reached in QUERY space on diagonal d.
    // wfa_r[d] = furthest qri reached in REF   space on diagonal d.
    // -1 sentinel = not reached.
    // -----------------------------------------------------------------------

    std::unordered_map<int, int> wfa_q, wfa_r; // diag -> max offset

    // Cumulative corridor sets (diagonals ever touched, across all scores).
    std::unordered_set<int> corr_q, corr_r;

    // Extend a match run in QUERY space from current offset along diagonal d.
    auto extend_q = [&](int d, int off) -> int {
        while (true) {
            int nqri = off + 1, nti = nqri - d;
            if (nqri < query_len && nti >= 0 && nti < truth_len &&
                    query[i][nqri] == truth[i][nti])
                off = nqri;
            else break;
        }
        return off;
    };

    // Extend a match run in REF space from current offset along diagonal d.
    auto extend_r = [&](int d, int off) -> int {
        while (true) {
            int nqri = off + 1, nti = nqri - d;
            if (nqri < ref_len && nti >= 0 && nti < truth_len &&
                    ref[nqri] == truth[i][nti])
                off = nqri;
            else break;
        }
        return off;
    };

    // Check swap conditions (same predicates as the main BFS).
    auto can_swap_q_to_r = [&](int qri, int ti) -> bool {
        return (!(query_ref_ptrs[i][FLAGS][qri] & PTR_VARIANT) ||
                 (query_ref_ptrs[i][FLAGS][qri] & PTR_VAR_END)) &&
               (!(truth_ref_ptrs[i][FLAGS][ti]  & PTR_VARIANT) ||
                 (truth_ref_ptrs[i][FLAGS][ti]  & PTR_VAR_END));
    };
    auto can_swap_r_to_q = [&](int rqri, int ti) -> bool {
        return (!(ref_query_ptrs[i][FLAGS][rqri] & PTR_VARIANT) ||
                 (ref_query_ptrs[i][FLAGS][rqri] & PTR_VAR_END)) &&
               (!(truth_ref_ptrs[i][FLAGS][ti]   & PTR_VARIANT) ||
                 (truth_ref_ptrs[i][FLAGS][ti]   & PTR_VAR_END));
    };

    // Propagate swaps from newly-added WFA entries until no more additions.
    // Adds resulting diagonals to new_q / new_r, and also directly to wfa_q/wfa_r.
    auto propagate_swaps = [&](std::vector<std::pair<int,int>> & new_q,
                               std::vector<std::pair<int,int>> & new_r) {
        bool changed = true;
        while (changed) {
            changed = false;
            std::vector<std::pair<int,int>> added_q, added_r;
            for (auto & [d, off] : new_q) {
                int ti = off - d;
                if (off < 0 || off >= query_len || ti < 0 || ti >= truth_len) continue;
                if (!can_swap_q_to_r(off, ti)) continue;
                int ref_pos = query_ref_ptrs[i][PTRS][off];
                int nrqri = ref_pos + 1, nti = ti + 1;
                if (nrqri >= ref_len || nti >= truth_len) continue;
                if (ref[nrqri] != truth[i][nti]) continue;
                int nd = nrqri - nti;
                int ext = extend_r(nd, nrqri);
                if (!wfa_r.count(nd) || wfa_r[nd] < ext) {
                    wfa_r[nd] = ext;
                    added_r.push_back({nd, ext});
                    changed = true;
                }
            }
            for (auto & [d, off] : new_r) {
                int ti = off - d;
                if (off < 0 || off >= ref_len || ti < 0 || ti >= truth_len) continue;
                if (!can_swap_r_to_q(off, ti)) continue;
                int qpos = ref_query_ptrs[i][PTRS][off];
                int nqqri = qpos + 1, nti = ti + 1;
                if (nqqri >= query_len || nti >= truth_len) continue;
                if (query[i][nqqri] != truth[i][nti]) continue;
                int nd = nqqri - nti;
                int ext = extend_q(nd, nqqri);
                if (!wfa_q.count(nd) || wfa_q[nd] < ext) {
                    wfa_q[nd] = ext;
                    added_q.push_back({nd, ext});
                    changed = true;
                }
            }
            for (auto & p : added_q) new_q.push_back(p);
            for (auto & p : added_r) new_r.push_back(p);
        }
    };

    // Helper: record all current wfa_q/wfa_r diagonals into the corridor.
    auto record_corridor = [&]() {
        for (auto & [d, off] : wfa_q) corr_q.insert(d);
        for (auto & [d, off] : wfa_r) corr_r.insert(d);
    };

    // Initialize score 0: seed both spaces at (qri=0, ti=0).
    {
        wfa_q[0] = extend_q(0, 0);
        wfa_r[0] = extend_r(0, 0);
        std::vector<std::pair<int,int>> new_q{{0, wfa_q[0]}};
        std::vector<std::pair<int,int>> new_r{{0, wfa_r[0]}};
        propagate_swaps(new_q, new_r);
        record_corridor();
    }

    // Termination check: QUERY done when wfa_q has diagonal dq at offset query_len-1;
    // REF done when wfa_r has diagonal dr at offset ref_len-1.
    int dq_end = (query_len - 1) - (truth_len - 1);
    int dr_end = (ref_len   - 1) - (truth_len - 1);
    auto is_done = [&]() -> bool {
        return (wfa_q.count(dq_end) && wfa_q.at(dq_end) >= query_len - 1) ||
               (wfa_r.count(dr_end) && wfa_r.at(dr_end) >= ref_len   - 1);
    };

    int score = 0;
    while (!is_done()) {
        score++;
        // Build new frontier by advancing each entry by one edit (INS/DEL/SUB).
        std::unordered_map<int, int> next_q = wfa_q;
        std::unordered_map<int, int> next_r = wfa_r;

        auto advance_q = [&](int nd, int noff) {
            if (noff < 0 || noff >= query_len) return;
            int nti = noff - nd;
            if (nti < 0 || nti >= truth_len) return;
            int ext = extend_q(nd, noff);
            if (!next_q.count(nd) || next_q[nd] < ext) next_q[nd] = ext;
        };
        auto advance_r = [&](int nd, int noff) {
            if (noff < 0 || noff >= ref_len) return;
            int nti = noff - nd;
            if (nti < 0 || nti >= truth_len) return;
            int ext = extend_r(nd, noff);
            if (!next_r.count(nd) || next_r[nd] < ext) next_r[nd] = ext;
        };

        for (auto & [d, off] : wfa_q) {
            advance_q(d + 1, off + 1); // INS: qri+1, ti unchanged
            advance_q(d - 1, off);     // DEL: qri unchanged, ti+1
            advance_q(d,     off + 1); // SUB: qri+1, ti+1
        }
        for (auto & [d, off] : wfa_r) {
            advance_r(d + 1, off + 1);
            advance_r(d - 1, off);
            advance_r(d,     off + 1);
        }

        // Cross-space swaps from newly promoted cells.
        std::vector<std::pair<int,int>> new_q, new_r;
        for (auto & [d, off] : next_q)
            if (!wfa_q.count(d) || wfa_q[d] < off) new_q.push_back({d, off});
        for (auto & [d, off] : next_r)
            if (!wfa_r.count(d) || wfa_r[d] < off) new_r.push_back({d, off});

        wfa_q = std::move(next_q);
        wfa_r = std::move(next_r);
        propagate_swaps(new_q, new_r);
        record_corridor();
    }

    // -----------------------------------------------------------------------
    // Phase 2: Corridor-bounded BFS fills ptrs and swap_pred_map.
    // Logic mirrors the main BFS; cells outside the corridor are skipped.
    // -----------------------------------------------------------------------

    std::vector<uint8_t> done_q_mat(query_len * truth_len, 0);
    std::vector<uint8_t> done_r_mat(ref_len   * truth_len, 0);
    auto dq = [&](int qri, int ti) -> uint8_t& { return done_q_mat[qri * truth_len + ti]; };
    auto dr = [&](int qri, int ti) -> uint8_t& { return done_r_mat[qri * truth_len + ti]; };

    std::queue<idx1> bfs_q;
    std::vector<idx1> curr_wave, prev_wave;

    // Seed: start both spaces at (0,0).
    ptrs[qi][0][0] |= PTR_MAT; dq(0, 0) = true;
    ptrs[ri][0][0] |= PTR_MAT; dr(0, 0) = true;
    bfs_q.push({qi, 0, 0});
    bfs_q.push({ri, 0, 0});

    int bfs_score = 0;
    while (true) {
        // EXTEND (free match/swap transitions at same score).
        while (!bfs_q.empty()) {
            idx1 x = bfs_q.front(); bfs_q.pop();
            prev_wave.push_back(x);

            if (x.hi == qi) {
                // Match in QUERY space.
                idx1 y(qi, x.qri + 1, x.ti + 1);
                if (y.qri < query_len && y.ti < truth_len &&
                        query[i][y.qri] == truth[i][y.ti] && !dq(y.qri, y.ti)) {
                    ptrs[y.hi][y.qri][y.ti] |= PTR_MAT;
                    if (corr_q.count(y.qri - y.ti)) {
                        dq(y.qri, y.ti) = true;
                        bfs_q.push(y); curr_wave.push_back(y);
                    }
                }
                // Swap QUERY -> REF.
                if (x.qri < query_len && x.ti < truth_len &&
                        can_swap_q_to_r(x.qri, x.ti)) {
                    idx1 z(ri, query_ref_ptrs[i][PTRS][x.qri] + 1, x.ti + 1);
                    if (z.qri < ref_len && z.ti < truth_len &&
                            ref[z.qri] == truth[i][z.ti] && !dr(z.qri, z.ti)) {
                        ptrs[z.hi][z.qri][z.ti] |= PTR_SWP_MAT;
                        swap_pred_map[z] = x;
                        if (corr_r.count(z.qri - z.ti)) {
                            dr(z.qri, z.ti) = true;
                            bfs_q.push(z); curr_wave.push_back(z);
                        }
                    }
                }
            } else {
                // Match in REF space.
                idx1 y(ri, x.qri + 1, x.ti + 1);
                if (y.qri < ref_len && y.ti < truth_len &&
                        ref[y.qri] == truth[i][y.ti] && !dr(y.qri, y.ti)) {
                    ptrs[y.hi][y.qri][y.ti] |= PTR_MAT;
                    if (corr_r.count(y.qri - y.ti)) {
                        dr(y.qri, y.ti) = true;
                        bfs_q.push(y); curr_wave.push_back(y);
                    }
                }
                // Swap REF -> QUERY.
                if (x.qri < ref_len && x.ti < truth_len &&
                        can_swap_r_to_q(x.qri, x.ti)) {
                    idx1 z(qi, ref_query_ptrs[i][PTRS][x.qri] + 1, x.ti + 1);
                    if (z.qri < query_len && z.ti < truth_len &&
                            query[i][z.qri] == truth[i][z.ti] && !dq(z.qri, z.ti)) {
                        ptrs[z.hi][z.qri][z.ti] |= PTR_SWP_MAT;
                        swap_pred_map[z] = x;
                        if (corr_q.count(z.qri - z.ti)) {
                            dq(z.qri, z.ti) = true;
                            bfs_q.push(z); curr_wave.push_back(z);
                        }
                    }
                }
            }
        } // extend

        // Mark cells as done (already done during extend — we mark on enqueue).
        curr_wave.clear();

        // Check termination.
        if (dq(query_len - 1, truth_len - 1)) {
            pr_query_ref_end = qi; break;
        }
        if (dr(ref_len - 1, truth_len - 1)) {
            pr_query_ref_end = ri; break;
        }

        // NEXT SCORE: INS / DEL / SUB transitions.
        bfs_score++;
        for (const idx1 & x : prev_wave) {
            int qr_len = (x.hi == qi) ? query_len : ref_len;
            bool is_q = (x.hi == qi);

            // INS: qri+1, ti
            if (x.qri + 1 < qr_len) {
                idx1 y(x.hi, x.qri + 1, x.ti);
                uint8_t & done_cell = is_q ? dq(y.qri, y.ti) : dr(y.qri, y.ti);
                if (!done_cell) {
                    ptrs[y.hi][y.qri][y.ti] |= PTR_INS;
                    bool in_corr = is_q ? corr_q.count(y.qri - y.ti) :
                                         corr_r.count(y.qri - y.ti);
                    if (in_corr) { done_cell = true; bfs_q.push(y); curr_wave.push_back(y); }
                }
            }
            // DEL: qri, ti+1
            if (x.ti + 1 < truth_len) {
                idx1 y(x.hi, x.qri, x.ti + 1);
                uint8_t & done_cell = is_q ? dq(y.qri, y.ti) : dr(y.qri, y.ti);
                if (!done_cell) {
                    ptrs[y.hi][y.qri][y.ti] |= PTR_DEL;
                    bool in_corr = is_q ? corr_q.count(y.qri - y.ti) :
                                         corr_r.count(y.qri - y.ti);
                    if (in_corr) { done_cell = true; bfs_q.push(y); curr_wave.push_back(y); }
                }
            }
            // SUB: qri+1, ti+1
            if (x.qri + 1 < qr_len && x.ti + 1 < truth_len) {
                idx1 y(x.hi, x.qri + 1, x.ti + 1);
                uint8_t & done_cell = is_q ? dq(y.qri, y.ti) : dr(y.qri, y.ti);
                if (!done_cell) {
                    ptrs[y.hi][y.qri][y.ti] |= PTR_SUB;
                    bool in_corr = is_q ? corr_q.count(y.qri - y.ti) :
                                         corr_r.count(y.qri - y.ti);
                    if (in_corr) { done_cell = true; bfs_q.push(y); curr_wave.push_back(y); }
                }
            }
        }
        prev_wave.clear();
    } // while bfs

    score_out = bfs_score;
}

void calc_prec_recall_aln(
        const std::string & query1, const std::string & query2,
        const std::string & truth1, const std::string & truth2, 
        const std::string & ref,
        const std::vector< std::vector<int> > & query1_ref_ptrs, 
        const std::vector< std::vector<int> > & ref_query1_ptrs,
        const std::vector< std::vector<int> > & query2_ref_ptrs, 
        const std::vector< std::vector<int> > & ref_query2_ptrs,
        const std::vector< std::vector<int> > & truth1_ref_ptrs, 
        const std::vector< std::vector<int> > & truth2_ref_ptrs,
        std::vector<int> & s, 
        std::vector< std::vector< std::vector<uint8_t> > > & ptrs,
        std::vector< std::shared_ptr< std::unordered_map<idx1, idx1> > > & swap_pred_maps,
        std::vector<int> & pr_query_ref_end, 
        int aln_start, int aln_stop, bool print
        ) {
    
    // set loop variables
    int ref_len = ref.size();
    RefList4<std::string> query {{&query1, &query1, &query2, &query2}};
    RefList4<std::string> truth {{&truth1, &truth2, &truth1, &truth2}};
    RefList4< std::vector< std::vector<int> > > query_ref_ptrs {{
            &query1_ref_ptrs, &query1_ref_ptrs, &query2_ref_ptrs, &query2_ref_ptrs }};
    RefList4< std::vector< std::vector<int> > > truth_ref_ptrs {{
            &truth1_ref_ptrs, &truth2_ref_ptrs, &truth1_ref_ptrs, &truth2_ref_ptrs }};
    RefList4< std::vector< std::vector<int> > > ref_query_ptrs {{
            &ref_query1_ptrs, &ref_query1_ptrs, &ref_query2_ptrs, &ref_query2_ptrs }};
    int query_lens[4] =
            {int(query1.size()), int(query1.size()), int(query2.size()), int(query2.size())};
    int truth_lens[4] =
            {int(truth1.size()), int(truth2.size()), int(truth1.size()), int(truth2.size())};

    std::vector< std::vector< std::vector<uint8_t> > > state;

    // for each combination of query and truth
    for (int i = aln_start; i < aln_stop; i++) {
        int qi = 2*i + QUERY; // query index (ptrs)
        int ri = 2*i + REF;   // ref index   (ptrs)
        int dqi = 2*(i-aln_start) + QUERY;
        int dri = 2*(i-aln_start) + REF;

        // For large alignments, use the WFA-corridor path to avoid allocating
        // and exploring the full O(query_len * truth_len) matrix. The exact
        // dense BFS maximizes true-positive credit but is slower on giants;
        // --exact-prec-recall forces it for byte-identical results.
        size_t cell_count = (size_t)query_lens[i] * truth_lens[i];
        if (!g.exact_prec_recall && cell_count > PR_WFA_CORRIDOR_THRESHOLD) {
            // Push placeholder state entries so dqi/dri indexing stays consistent
            // for subsequent alignments (state is not accessed for WFA path).
            state.push_back(std::vector< std::vector<uint8_t> >());
            state.push_back(std::vector< std::vector<uint8_t> >());
            calc_prec_recall_aln_one_wfa(
                    i, query, truth, ref,
                    query_ref_ptrs, ref_query_ptrs, truth_ref_ptrs,
                    qi, ri, query_lens[i], truth_lens[i], ref_len,
                    s[i], ptrs, *swap_pred_maps[i], pr_query_ref_end[i]);
            continue;
        }

        // init full pointer/state matrices
        state.push_back(std::vector< std::vector<uint8_t> >(query_lens[i],
                    std::vector<uint8_t>(truth_lens[i], PR_NEW)));
        state.push_back(std::vector< std::vector<uint8_t> >(ref_len,
                    std::vector<uint8_t>(truth_lens[i], PR_NEW)));

        // set first wavefront
        std::queue<idx1> queue; // still to be explored in this wave
        queue.push({qi, 0, 0});
        ptrs[qi][0][0] |= PTR_MAT;
        state[dqi][0][0] = PR_DONE;
        queue.push({ri, 0, 0});
        ptrs[ri][0][0] |= PTR_MAT;
        state[dqi][0][0] = PR_DONE;

        // continue looping until full alignment found
        std::vector<idx1> curr_wave; // everything reached this wave (not yet done)
        std::vector<idx1> prev_wave; // everything explored prev wave
        /* if (print) printf("\nFWD %s aln: (%d|%d, %d|%d, %d)\n", aln_strs[i].data(), */ 
        /*         qi, ri, query_lens[i], ref_len, truth_lens[i]); */
        while (true) {
            if (print) printf("  s = %d\n", s[i]);
            if (queue.empty()) ERROR("Empty queue in 'prec_recall_aln()'.");

            // EXTEND WAVEFRONT (stay at same score)
            while (!queue.empty()) {
                idx1 x = queue.front(); queue.pop();
                /* if (print) printf("    x = (%s, %d, %d)\n", */ 
                /*         (x.hi % 2) ? "REF  " : "QUERY", x.qri, x.ti); */
                prev_wave.push_back(x);
                if (x.hi == qi) { // QUERY
                    // allow match on query
                    idx1 y(qi, x.qri+1, x.ti+1);
                    if (y.qri < query_lens[i] && y.ti < truth_lens[i] &&
                            query[i][y.qri] == truth[i][y.ti]) {
                        uint8_t & st = state[dqi][y.qri][y.ti];
                        if (st != PR_DONE) {
                            if (st == PR_NEW) {
                                queue.push(y); curr_wave.push_back(y); st = PR_IN_WAVE;
                            }
                            ptrs[y.hi][y.qri][y.ti] |= PTR_MAT;
                        }
                    }
                    // allow match, swapping to reference
                    idx1 z(ri, query_ref_ptrs[i][PTRS][x.qri]+1, x.ti+1);
                    if ( (!(query_ref_ptrs[i][FLAGS][x.qri] & PTR_VARIANT) ||
                            query_ref_ptrs[i][FLAGS][x.qri] & PTR_VAR_END) &&
                         (!(truth_ref_ptrs[i][FLAGS][x.ti] & PTR_VARIANT) ||
                            truth_ref_ptrs[i][FLAGS][x.ti] & PTR_VAR_END)) {
                        if (z.qri < ref_len && z.ti < truth_lens[i] &&
                                ref[z.qri] == truth[i][z.ti]) {
                            uint8_t & st = state[dri][z.qri][z.ti];
                            if (st != PR_DONE) {
                                if (st == PR_NEW) {
                                    queue.push(z); curr_wave.push_back(z); st = PR_IN_WAVE;
                                }
                                ptrs[z.hi][z.qri][z.ti] |= PTR_SWP_MAT;
                                (*swap_pred_maps[i])[z] = x;
                            }
                        }
                    }
                } else { // x.hi == ri == REF
                    // allow match
                    idx1 y(ri, x.qri+1, x.ti+1);
                    if (y.qri < ref_len && y.ti < truth_lens[i] &&
                            ref[y.qri] == truth[i][y.ti]) {
                        uint8_t & st = state[dri][y.qri][y.ti];
                        if (st != PR_DONE) {
                            if (st == PR_NEW) {
                                queue.push(y); curr_wave.push_back(y); st = PR_IN_WAVE;
                            }
                            ptrs[y.hi][y.qri][y.ti] |= PTR_MAT;
                        }
                    }
                    // allow match, swapping to query
                    idx1 z(qi, ref_query_ptrs[i][PTRS][x.qri]+1, x.ti+1);
                    if ( (!(ref_query_ptrs[i][FLAGS][x.qri] & PTR_VARIANT) ||
                            ref_query_ptrs[i][FLAGS][x.qri] & PTR_VAR_END) &&
                         (!(truth_ref_ptrs[i][FLAGS][x.ti] & PTR_VARIANT) ||
                            truth_ref_ptrs[i][FLAGS][x.ti] & PTR_VAR_END)) {
                        if (z.qri < query_lens[i] && z.ti < truth_lens[i] &&
                                query[i][z.qri] == truth[i][z.ti]) {
                            uint8_t & st = state[dqi][z.qri][z.ti];
                            if (st != PR_DONE) {
                                if (st == PR_NEW) {
                                    queue.push(z); curr_wave.push_back(z); st = PR_IN_WAVE;
                                }
                                ptrs[z.hi][z.qri][z.ti] |= PTR_SWP_MAT;
                                (*swap_pred_maps[i])[z] = x;
                            }
                        }
                    }
                }
            }

            // mark all cells reached this wave as done
            for (const idx1 & x : curr_wave) {
                state[x.hi == ri ? dri : dqi][x.qri][x.ti] = PR_DONE;
            }
            curr_wave.clear();

            // exit if we're done aligning
            if (state[dqi][query_lens[i]-1][truth_lens[i]-1] == PR_DONE ||
                state[dri][ref_len-1][truth_lens[i]-1] == PR_DONE) break;


            // NEXT WAVEFRONT (increase score by one)
            for (const idx1 & x : prev_wave) {
                int qr_len = (x.hi == qi) ? query_lens[i] : ref_len;
                if (x.qri+1 < qr_len) { // INS
                    idx1 y(x.hi, x.qri+1, x.ti);
                    uint8_t & st = state[y.hi == ri ? dri : dqi][y.qri][y.ti];
                    if (st == PR_NEW) {
                        queue.push(y); curr_wave.push_back(y); st = PR_IN_WAVE;
                    }
                    if (st != PR_DONE)
                        ptrs[y.hi][y.qri][y.ti] |= PTR_INS;
                }
                if (x.ti+1 < truth_lens[i]) { // DEL
                    idx1 y(x.hi, x.qri, x.ti+1);
                    uint8_t & st = state[y.hi == ri ? dri : dqi][y.qri][y.ti];
                    if (st == PR_NEW) {
                        queue.push(y); curr_wave.push_back(y); st = PR_IN_WAVE;
                    }
                    if (st != PR_DONE)
                        ptrs[y.hi][y.qri][y.ti] |= PTR_DEL;
                }
                if (x.qri+1 < qr_len && x.ti+1 < truth_lens[i]) { // SUB
                    idx1 y(x.hi, x.qri+1, x.ti+1);
                    uint8_t & st = state[y.hi == ri ? dri : dqi][y.qri][y.ti];
                    if (st == PR_NEW) {
                        queue.push(y); curr_wave.push_back(y); st = PR_IN_WAVE;
                    }
                    if (st != PR_DONE)
                        ptrs[y.hi][y.qri][y.ti] |= PTR_SUB;
                }
            }
            prev_wave.clear();
            s[i]++;
        } // while loop (this alignment)

        if (print) printf("\nAlignment %s, aln_ptrs\n", aln_strs[i].data());
        if (print) printf("\nQUERY");
        if (print) print_ptrs(ptrs[qi], query[i], truth[i]);
        if (print) printf("\nREF");
        if (print) print_ptrs(ptrs[ri], ref, truth[i]);

        // save where to start backtrack (prefer query to maximize TPs)
        if (state[dqi][query_lens[i]-1][truth_lens[i]-1] == PR_DONE) {
            pr_query_ref_end[i] = qi;
        } else if (state[dri][ref_len-1][truth_lens[i]-1] == PR_DONE) {
            pr_query_ref_end[i] = ri;
        } else { ERROR("Alignment not finished in 'prec_recall_aln()'."); }

    } // 4 alignments
} // function


/******************************************************************************/


int store_phase( 
        superclusterData * clusterdata_ptr, 
        const std::string & ctg, int sc_idx,
        const std::vector<int> & s
        ) {

    // calculate best phasing
    int orig_phase_dist = s[QUERY1_TRUTH1] + s[QUERY2_TRUTH2];
    int swap_phase_dist = s[QUERY2_TRUTH1] + s[QUERY1_TRUTH2];
    int phase = PHASE_NONE; // default either way if insufficient evidence
    if (orig_phase_dist == swap_phase_dist) { // allow either phasing if equal dist
        phase = PHASE_NONE;
    } else if (orig_phase_dist == 0) { // protect division by zero
        phase = PHASE_ORIG;
    } else if (swap_phase_dist == 0) { // protect division by zero
        phase = PHASE_SWAP;
    } else if (1 - float(swap_phase_dist) / orig_phase_dist > g.phase_threshold) { // significant reduction by swapping
        phase = PHASE_SWAP;
    } else if (1 - float(orig_phase_dist) / swap_phase_dist > g.phase_threshold) { // significant reduction with orig
        phase = PHASE_ORIG;
    }

    // save alignment information
    clusterdata_ptr->superclusters[ctg]->set_phase(sc_idx,
            phase, orig_phase_dist, swap_phase_dist);
    return phase;
}


/******************************************************************************/


/* Given alignment pointers (aln_ptrs) from minED forward-pass, as well as 
 * reference pointers to/from truth/query, backtrack and find path which 
 * maximizes FP variants. Results are stored in path_ptrs. Then call 
 * get_prec_recall_path_sync() to parse this path.
 */
void calc_prec_recall_path(
        const std::string & ref,
        const std::string & query1, const std::string & query2, 
        const std::string & truth1, const std::string & truth2, 
        std::vector< std::vector<idx1> > & path, 
        std::vector< std::vector<bool> > & sync, 
        std::vector< std::vector<bool> > & edits, 
        std::vector< std::vector< std::vector<uint8_t> > > & aln_ptrs,
        std::vector< std::vector< std::vector<uint8_t> > > & path_ptrs,
        std::vector< std::vector< std::vector<int16_t> > > & path_scores,
        const std::vector< std::vector<int> > & query1_ref_ptrs, 
        const std::vector< std::vector<int> > & ref_query1_ptrs,
        const std::vector< std::vector<int> > & query2_ref_ptrs, 
        const std::vector< std::vector<int> > & ref_query2_ptrs,
        const std::vector< std::vector<int> > & truth1_ref_ptrs, 
        const std::vector< std::vector<int> > & truth2_ref_ptrs,
        const std::vector< std::shared_ptr< std::unordered_map<idx1,idx1> > > & swap_pred_maps,
        const std::vector<int> & pr_query_ref_end, bool print
        ) {

    RefList4<std::string> query {{&query1, &query1, &query2, &query2}};
    RefList4<std::string> truth {{&truth1, &truth2, &truth1, &truth2}};
    RefList4< std::vector< std::vector<int> > > query_ref_ptrs {{
            &query1_ref_ptrs, &query1_ref_ptrs, &query2_ref_ptrs, &query2_ref_ptrs }};
    RefList4< std::vector< std::vector<int> > > ref_query_ptrs {{
            &ref_query1_ptrs, &ref_query1_ptrs, &ref_query2_ptrs, &ref_query2_ptrs }};
    RefList4< std::vector< std::vector<int> > > truth_ref_ptrs {{
            &truth1_ref_ptrs, &truth2_ref_ptrs, &truth1_ref_ptrs, &truth2_ref_ptrs }};
    std::vector<int> pr_query_ref_beg(CALLSETS*HAPS);
    std::vector< std::vector< std::vector<bool> > > done;

    for (int i = 0; i < CALLSETS*HAPS; i++) {
        /* printf("\nBKWD %s path\n", aln_strs[i].data()); */

        // init
        int qi = i*2 + QUERY;
        int ri = i*2 + REF;
        path_ptrs.push_back(std::vector< std::vector<uint8_t> >(aln_ptrs[qi].size(), 
                    std::vector<uint8_t>(aln_ptrs[qi][0].size(), PTR_NONE)));
        path_ptrs.push_back(std::vector< std::vector<uint8_t> >(aln_ptrs[ri].size(), 
                    std::vector<uint8_t>(aln_ptrs[ri][0].size(), PTR_NONE)));
        path_scores.push_back(std::vector< std::vector<int16_t> >(aln_ptrs[qi].size(),
                    std::vector<int16_t>(aln_ptrs[qi][0].size(), -1)));
        path_scores.push_back(std::vector< std::vector<int16_t> >(aln_ptrs[ri].size(),
                    std::vector<int16_t>(aln_ptrs[ri][0].size(), -1)));
        done.push_back(std::vector< std::vector<bool> >(aln_ptrs[qi].size(), 
                    std::vector<bool>(aln_ptrs[qi][0].size(), false)));
        done.push_back(std::vector< std::vector<bool> >(aln_ptrs[ri].size(), 
                    std::vector<bool>(aln_ptrs[ri][0].size(), false)));

        // backtrack start
        std::queue<idx1> queue;
        int start_hi = pr_query_ref_end[i];
        int start_qri = (start_hi % 2 == QUERY) ? query_ref_ptrs[i][0].size()-1 :
                ref_query_ptrs[i][0].size()-1;
        int start_ti = truth_ref_ptrs[i][0].size()-1;
        idx1 start(start_hi, start_qri, start_ti);
        path_ptrs[start_hi][start_qri][start_ti] = PTR_MAT;
        aln_ptrs[start_hi][start_qri][start_ti] |= MAIN_PATH; // all alignments end here
        path_scores[start_hi][start_qri][start_ti] = 0;
        queue.push(start);
        std::unordered_set<idx1> curr_wave, prev_wave;

        while (true) {
            while (!queue.empty()) {
                idx1 x = queue.front(); queue.pop(); // current cell
                prev_wave.insert(x);
                /* printf("(%d, %d, %d)\n", x.hi, x.qri, x.ti); */

                // MATCH movement
                if (aln_ptrs[x.hi][x.qri][x.ti] & PTR_MAT && x.qri > 0 && x.ti > 0) {

                    // get next cell, add to path
                    idx1 y = idx1(x.hi, x.qri-1, x.ti-1);
                    aln_ptrs[y.hi][y.qri][y.ti] |= PATH;

                    // check if mvmt is in variant
                    bool in_truth_var = truth_ref_ptrs[i][FLAGS][x.ti] & PTR_VARIANT;
                    in_truth_var &= !(truth_ref_ptrs[i][FLAGS][x.ti] & PTR_VAR_BEG);
                    bool in_query_var = false;
                    if (x.hi == qi) {
                        in_query_var = query_ref_ptrs[i][FLAGS][x.qri] & PTR_VARIANT;
                        in_query_var &= !(query_ref_ptrs[i][FLAGS][x.qri] & PTR_VAR_BEG);
                    }

                    // check for tp
                    bool is_tp = x.hi == qi && ((query_ref_ptrs[i][PTRS][x.qri] != 
                            query_ref_ptrs[i][PTRS][y.qri]+1) || // INS
                            query_ref_ptrs[i][FLAGS][x.qri] & PTR_VAR_BEG); // SUB/DEL

                    // update score
                    if (!done[y.hi][y.qri][y.ti] &&
                            path_scores[x.hi][x.qri][x.ti] + is_tp > path_scores[y.hi][y.qri][y.ti]) {
                        path_ptrs[y.hi][y.qri][y.ti] = PTR_MAT;
                        path_scores[y.hi][y.qri][y.ti] = path_scores[x.hi][x.qri][x.ti] + is_tp;
                        if (!contains(curr_wave, y)) curr_wave.insert(y);
                        queue.push(y);
                    } else if (!done[y.hi][y.qri][y.ti] &&
                            path_scores[x.hi][x.qri][x.ti] + is_tp == path_scores[y.hi][y.qri][y.ti]) {
                        path_ptrs[y.hi][y.qri][y.ti] |= PTR_MAT;
                    }

                    if (print) printf("(%s, %2d, %2d) --%s-> (%s, %2d, %2d) %s %s %s\n",
                            x.hi % 2 ? "REF" : "QRY", x.qri, x.ti, "MAT",
                            y.hi % 2 ? "REF" : "QRY", y.qri, y.ti,
                            in_query_var ? GREEN("QUERY_VAR").data() : RED("QUERY_VAR").data(), 
                            in_truth_var ? GREEN("TRUTH_VAR").data() : RED("TRUTH_VAR").data(),
                            is_tp ? GREEN("TP").data() : RED("TP").data());

                }


                if (x.hi == ri) { // REF -> QUERY SWAP mvmt
                    if (aln_ptrs[x.hi][x.qri][x.ti] & PTR_SWP_MAT &&
                            (!(ref_query_ptrs[i][FLAGS][x.qri] & PTR_VARIANT) ||
                            ref_query_ptrs[i][FLAGS][x.qri] & PTR_VAR_BEG) && 
                            x.qri > 0 && x.ti > 0) {

                        // get next cell, add to path
                        if (!contains((*swap_pred_maps[i]), x))
                            ERROR("No swap predecessor, but PTR_SWP_MAT set.");
                        idx1 z = (*swap_pred_maps[i]).at(x);
                        aln_ptrs[z.hi][z.qri][z.ti] |= PATH;

                        // check if mvmt is in variant
                        bool in_truth_var = truth_ref_ptrs[i][FLAGS][x.ti] & PTR_VARIANT;
                        in_truth_var &= !(truth_ref_ptrs[i][FLAGS][x.ti] & PTR_VAR_BEG);
                        bool in_query_var = false;
                        bool is_tp = false;
                        
                        // update score
                        if (!done[z.hi][z.qri][z.ti] &&
                                path_scores[x.hi][x.qri][x.ti] + is_tp > path_scores[z.hi][z.qri][z.ti]) {
                            path_ptrs[z.hi][z.qri][z.ti] = PTR_SWP_MAT;
                            path_scores[z.hi][z.qri][z.ti] = path_scores[x.hi][x.qri][x.ti] + is_tp;
                            if (!contains(curr_wave, z)) curr_wave.insert(z);
                            queue.push(z);
                        } else if (!done[z.hi][z.qri][z.ti] &&
                                path_scores[x.hi][x.qri][x.ti] + is_tp == path_scores[z.hi][z.qri][z.ti]) {
                            path_ptrs[z.hi][z.qri][z.ti] |= PTR_SWP_MAT;
                        }

                        if (print) printf("(%s, %2d, %2d) --%s-> (%s, %2d, %2d) %s %s %s\n",
                                x.hi % 2 ? "REF" : "QRY", x.qri, x.ti, "SWP",
                                z.hi % 2 ? "REF" : "QRY", z.qri, z.ti,
                                in_query_var ? GREEN("QUERY_VAR").data() : RED("QUERY_VAR").data(), 
                                in_truth_var ? GREEN("TRUTH_VAR").data() : RED("TRUTH_VAR").data(),
                                is_tp ? GREEN("TP").data() : RED("TP").data());
                    }

                } else { // QUERY -> REF SWAP mvmt
                    if (aln_ptrs[x.hi][x.qri][x.ti] & PTR_SWP_MAT &&
                            (!(query_ref_ptrs[i][FLAGS][x.qri] & PTR_VARIANT) ||
                            query_ref_ptrs[i][FLAGS][x.qri] & PTR_VAR_BEG) && 
                            x.qri > 0 && x.ti > 0) {

                        // add to path
                        if (!contains((*swap_pred_maps[i]), x))
                            ERROR("No swap predecessor, but PTR_SWP_MAT set.");
                        idx1 z = (*swap_pred_maps[i]).at(x);
                        aln_ptrs[z.hi][z.qri][z.ti] |= PATH;

                        // check if mvmt is in variant
                        bool in_truth_var = truth_ref_ptrs[i][FLAGS][x.ti] & PTR_VARIANT;
                        in_truth_var &= !(truth_ref_ptrs[i][FLAGS][x.ti] & PTR_VAR_BEG);
                        bool in_query_var = (x.hi == ri) ? false : 
                            query_ref_ptrs[i][FLAGS][x.qri] & PTR_VARIANT;
                        in_query_var &= !(query_ref_ptrs[i][FLAGS][x.qri] & PTR_VAR_BEG);

                        // check for tp
                        bool is_tp = x.hi == qi && ((query_ref_ptrs[i][PTRS][x.qri] != 
                                query_ref_ptrs[i][PTRS][x.qri-1]+1) || // INS
                                query_ref_ptrs[i][FLAGS][x.qri] & PTR_VAR_BEG); // SUB/DEL

                        // update score
                        if (!done[z.hi][z.qri][z.ti] &&
                                path_scores[x.hi][x.qri][x.ti] + is_tp > path_scores[z.hi][z.qri][z.ti]) {
                            path_ptrs[z.hi][z.qri][z.ti] = PTR_SWP_MAT;
                            path_scores[z.hi][z.qri][z.ti] = path_scores[x.hi][x.qri][x.ti] + is_tp;
                            if (!contains(curr_wave, z)) curr_wave.insert(z);
                            queue.push(z);
                        } else if (!done[z.hi][z.qri][z.ti] &&
                                path_scores[x.hi][x.qri][x.ti] + is_tp == path_scores[z.hi][z.qri][z.ti]) {
                            path_ptrs[z.hi][z.qri][z.ti] |= PTR_SWP_MAT;
                        }

                        if (print) printf("(%s, %2d, %2d) --%s-> (%s, %2d, %2d) %s %s %s\n",
                                x.hi % 2 ? "REF" : "QRY", x.qri, x.ti, "SWP",
                                z.hi % 2 ? "REF" : "QRY", z.qri, z.ti,
                                in_query_var ? GREEN("QUERY_VAR").data() : RED("QUERY_VAR").data(), 
                                in_truth_var ? GREEN("TRUTH_VAR").data() : RED("TRUTH_VAR").data(),
                                is_tp ? GREEN("TP").data() : RED("TP").data());
                    }
                }

            } // end of curr_wave, same score

            for (idx1 x : curr_wave) {
                done[x.hi][x.qri][x.ti] = true;
            }
            curr_wave.clear();
            if (done[qi][0][0] || done[ri][0][0]) break;

            for (idx1 x : prev_wave) {

                // SUB movement
                if (aln_ptrs[x.hi][x.qri][x.ti] & PTR_SUB &&
                        x.qri > 0 && x.ti > 0) {

                    // get next cell, add to path
                    idx1 y = idx1(x.hi, x.qri-1, x.ti-1);
                    aln_ptrs[y.hi][y.qri][y.ti] |= PATH;

                    // check if mvmt is in variant
                    bool in_truth_var = truth_ref_ptrs[i][FLAGS][x.ti] & PTR_VARIANT;
                    in_truth_var &= !(truth_ref_ptrs[i][FLAGS][x.ti] & PTR_VAR_BEG);
                    bool in_query_var = false;
                    if (x.hi == qi) {
                        in_query_var = query_ref_ptrs[i][FLAGS][x.qri] & PTR_VARIANT;
                        in_query_var &= !(query_ref_ptrs[i][FLAGS][x.qri] & PTR_VAR_BEG);
                    }

                    // check for tp
                    bool is_tp = x.hi == qi && ((query_ref_ptrs[i][PTRS][x.qri] != 
                            query_ref_ptrs[i][PTRS][y.qri]+1) || // INS
                            query_ref_ptrs[i][FLAGS][x.qri] & PTR_VAR_BEG); // SUB/DEL

                    // update score
                    if (!done[y.hi][y.qri][y.ti] &&
                            path_scores[x.hi][x.qri][x.ti] + is_tp > path_scores[y.hi][y.qri][y.ti]) {
                        path_ptrs[y.hi][y.qri][y.ti] = PTR_SUB;
                        path_scores[y.hi][y.qri][y.ti] = path_scores[x.hi][x.qri][x.ti] + is_tp;
                        if (!contains(curr_wave, y)) curr_wave.insert(y);
                        queue.push(y);
                    } else if (!done[y.hi][y.qri][y.ti] &&
                            path_scores[x.hi][x.qri][x.ti] + is_tp == path_scores[y.hi][y.qri][y.ti]) {
                        path_ptrs[y.hi][y.qri][y.ti] |= PTR_SUB;
                    }

                    if (print) printf("(%s, %2d, %2d) --%s-> (%s, %2d, %2d) %s %s %s\n",
                            x.hi % 2 ? "REF" : "QRY", x.qri, x.ti, "SUB",
                            y.hi % 2 ? "REF" : "QRY", y.qri, y.ti,
                            in_query_var ? GREEN("QUERY_VAR").data() : RED("QUERY_VAR").data(), 
                            in_truth_var ? GREEN("TRUTH_VAR").data() : RED("TRUTH_VAR").data(),
                            is_tp ? GREEN("TP").data() : RED("TP").data());
                }

                // INS movement
                if (aln_ptrs[x.hi][x.qri][x.ti] & PTR_INS && x.qri > 0) {

                    // get next cell, add to path
                    idx1 y = idx1(x.hi, x.qri-1, x.ti);
                    aln_ptrs[y.hi][y.qri][y.ti] |= PATH;

                    // check if mvmt is in variant
                    bool in_truth_var = truth_ref_ptrs[i][FLAGS][x.ti] & PTR_VARIANT;
                    bool in_query_var = false;
                    if (x.hi == qi) {
                        in_query_var = query_ref_ptrs[i][FLAGS][x.qri] & PTR_VARIANT;
                        in_query_var &= !(query_ref_ptrs[i][FLAGS][x.qri] & PTR_VAR_BEG);
                    }

                    // check for tp
                    bool is_tp = x.hi == qi && ((query_ref_ptrs[i][PTRS][x.qri] != 
                            query_ref_ptrs[i][PTRS][y.qri]+1) || // INS
                            query_ref_ptrs[i][FLAGS][x.qri] & PTR_VAR_BEG); // SUB/DEL

                    // update score
                    if (!done[y.hi][y.qri][y.ti] &&
                            path_scores[x.hi][x.qri][x.ti] + is_tp > path_scores[y.hi][y.qri][y.ti]) {
                        path_ptrs[y.hi][y.qri][y.ti] = PTR_INS;
                        path_scores[y.hi][y.qri][y.ti] = path_scores[x.hi][x.qri][x.ti] + is_tp;
                        if (!contains(curr_wave, y)) curr_wave.insert(y);
                        queue.push(y);
                    } else if (!done[y.hi][y.qri][y.ti] &&
                            path_scores[x.hi][x.qri][x.ti] + is_tp == path_scores[y.hi][y.qri][y.ti]) {
                        path_ptrs[y.hi][y.qri][y.ti] |= PTR_INS;
                    }

                    if (print) printf("(%s, %2d, %2d) --%s-> (%s, %2d, %2d) %s %s %s\n",
                            x.hi % 2 ? "REF" : "QRY", x.qri, x.ti, "INS",
                            y.hi % 2 ? "REF" : "QRY", y.qri, y.ti,
                            in_query_var ? GREEN("QUERY_VAR").data() : RED("QUERY_VAR").data(), 
                            in_truth_var ? GREEN("TRUTH_VAR").data() : RED("TRUTH_VAR").data(),
                            is_tp ? GREEN("TP").data() : RED("TP").data());
                }

                // DEL movement
                if (aln_ptrs[x.hi][x.qri][x.ti] & PTR_DEL && x.ti > 0) {

                    // add to path
                    idx1 y = idx1(x.hi, x.qri, x.ti-1);
                    aln_ptrs[y.hi][y.qri][y.ti] |= PATH;

                    bool in_truth_var = truth_ref_ptrs[i][FLAGS][x.ti] & PTR_VARIANT;
                    in_truth_var &= !(truth_ref_ptrs[i][FLAGS][x.ti] & PTR_VAR_BEG);
                    bool in_query_var = (x.hi == ri) ? false : 
                        query_ref_ptrs[i][FLAGS][x.qri] & PTR_VARIANT;

                    // no need to check for TP since we don't consume a REF/QUERY base
                    
                    // update score
                    if (!done[y.hi][y.qri][y.ti] &&
                            path_scores[x.hi][x.qri][x.ti] > path_scores[y.hi][y.qri][y.ti]) {
                        path_ptrs[y.hi][y.qri][y.ti] = PTR_DEL;
                        path_scores[y.hi][y.qri][y.ti] = path_scores[x.hi][x.qri][x.ti];
                        if (!contains(curr_wave, y)) curr_wave.insert(y);
                        queue.push(y);
                    } else if (!done[y.hi][y.qri][y.ti] &&
                            path_scores[x.hi][x.qri][x.ti] == path_scores[y.hi][y.qri][y.ti]) {
                        path_ptrs[y.hi][y.qri][y.ti] |= PTR_DEL;
                    }

                    if (print) printf("(%s, %2d, %2d) --%s-> (%s, %2d, %2d) %s %s\n",
                            x.hi % 2 ? "REF" : "QRY", x.qri, x.ti, "DEL",
                            y.hi % 2 ? "REF" : "QRY", y.qri, y.ti,
                            in_query_var ? GREEN("QUERY_VAR").data() : RED("QUERY_VAR").data(), 
                            in_truth_var ? GREEN("TRUTH_VAR").data() : RED("TRUTH_VAR").data());
                }

            } // done adding to next wave
            prev_wave.clear();
        }

        // set pointers
        if (aln_ptrs[qi][0][0] & PATH)
            pr_query_ref_beg[i] = qi;
        else
            pr_query_ref_beg[i] = ri;

        // debug print
        if (print) printf("Alignment %s, path_ptrs\n", aln_strs[i].data());
        if (print) printf("\nQUERY");
        if (print) print_ptrs(path_ptrs[qi], query[i], truth[i]);
        if (print) printf("\nREF");
        if (print) print_ptrs(path_ptrs[ri], ref, truth[i]);

    } // 2 alignments

    // get path and sync points
    get_prec_recall_path_sync(path, sync, edits, 
            aln_ptrs, path_ptrs,
            query1_ref_ptrs, ref_query1_ptrs, 
            query2_ref_ptrs, ref_query2_ptrs,
            truth1_ref_ptrs, truth2_ref_ptrs, 
            pr_query_ref_beg, print
    );

}


/******************************************************************************/


/* Follow path which maximizes TP and minimizes ED, saving sync points and edits.
 */
void get_prec_recall_path_sync(
        std::vector< std::vector<idx1> > & path, 
        std::vector< std::vector<bool> > & sync, 
        std::vector< std::vector<bool> > & edits, 
        std::vector< std::vector< std::vector<uint8_t> > > & aln_ptrs, 
        std::vector< std::vector< std::vector<uint8_t> > > & path_ptrs, 
        const std::vector< std::vector<int> > & query1_ref_ptrs, 
        const std::vector< std::vector<int> > & ref_query1_ptrs,
        const std::vector< std::vector<int> > & query2_ref_ptrs, 
        const std::vector< std::vector<int> > & ref_query2_ptrs,
        const std::vector< std::vector<int> > & truth1_ref_ptrs, 
        const std::vector< std::vector<int> > & truth2_ref_ptrs,
        const std::vector<int> & pr_query_ref_beg, bool print
        ) {

    // query <-> ref pointers
    RefList4< std::vector< std::vector<int> > > query_ref_ptrs {{
            &query1_ref_ptrs, &query1_ref_ptrs, &query2_ref_ptrs, &query2_ref_ptrs }};
    RefList4< std::vector< std::vector<int> > > truth_ref_ptrs {{
            &truth1_ref_ptrs, &truth2_ref_ptrs, &truth1_ref_ptrs, &truth2_ref_ptrs }};
    RefList4< std::vector< std::vector<int> > > ref_query_ptrs {{
            &ref_query1_ptrs, &ref_query1_ptrs, &ref_query2_ptrs, &ref_query2_ptrs }};

    for (int i = 0; i < CALLSETS*HAPS; i++) {
        /* printf("\nBKWD %s path\n", aln_strs[i].data()); */

        // init
        int qi = i*2 + QUERY;
        int ri = i*2 + REF;

        if (print) printf("Alignment %s:\n", aln_strs[i].data());

        // path start
        int hi = pr_query_ref_beg[i];
        int qri = 0;
        int ti = 0;
        int prev_hi, prev_qri, prev_ti = 0;
        int ptr_type = PTR_NONE;

        int r_size = ref_query_ptrs[i][PTRS].size();
        int q_size = query_ref_ptrs[i][PTRS].size();
        int t_size = truth_ref_ptrs[i][PTRS].size();

        // find all insertion locations (forbid sync points)
        std::vector<bool> ref_has_ins(r_size, false);
        for (int j = 0; j < q_size; j++) {
            if (query_ref_ptrs[i][FLAGS][j] & PTR_INS_LOC)
                ref_has_ins[query_ref_ptrs[i][PTRS][j]] = true;
        }
        for (int j = 0; j < t_size; j++) {
            if (truth_ref_ptrs[i][FLAGS][j] & PTR_INS_LOC)
                ref_has_ins[truth_ref_ptrs[i][PTRS][j]] = true;
        }

        // first position is sync point
        sync[i].push_back(true);
        edits[i].push_back(false);
        path[i].push_back(idx1(hi, qri, ti));
        aln_ptrs[hi][qri][ti] |= MAIN_PATH;
        aln_ptrs[hi][qri][ti] |= PTR_SYNC;
        path_ptrs[hi][qri][ti] |= MAIN_PATH;

        // follow best-path pointers, forward iteration
        while ( (hi == ri && qri < r_size-1) || (hi == qi && qri < q_size-1) || ti < t_size-1) {
            prev_hi = hi; prev_qri = qri; prev_ti = ti;
            if (hi == ri && path_ptrs[hi][qri][ti] & PTR_SWP_MAT) { // prefer TPs
                // if tie, default to moving to query, where variant may be considered TP
                ptr_type = PTR_SWP_MAT;
                hi = qi;
                qri = ref_query_ptrs[i][PTRS][qri];
                qri++; ti++; edits[i].push_back(false);

            } else if (path_ptrs[hi][qri][ti] & PTR_MAT) {
                ptr_type = PTR_MAT;
                qri++; ti++; edits[i].push_back(false);

            } else if (path_ptrs[hi][qri][ti] & PTR_SUB) {
                ptr_type = PTR_SUB;
                qri++; ti++; edits[i].push_back(true);

            } else if (path_ptrs[hi][qri][ti] & PTR_INS) {
                ptr_type = PTR_INS;
                qri++; edits[i].push_back(true);

            } else if (path_ptrs[hi][qri][ti] & PTR_DEL) {
                ptr_type = PTR_DEL;
                ti++; edits[i].push_back(true);

            } else if (hi == qi && path_ptrs[hi][qri][ti] & PTR_SWP_MAT) { // last choice is FP
                ptr_type = PTR_SWP_MAT;
                hi = ri;
                qri = query_ref_ptrs[i][PTRS][qri];
                qri++; ti++; edits[i].push_back(false);

            } else {
                ERROR("No valid pointer (value '%d') at (%d, %d, %d)", 
                        path_ptrs[hi][qri][ti], hi, qri, ti);
            }

            if ((hi == qi && qri >= q_size) || (hi == ri && qri >= r_size) || ti >= t_size) break;

            // add point to path
            path[i].push_back(idx1(hi, qri, ti));
            aln_ptrs[hi][qri][ti] |= MAIN_PATH;
            path_ptrs[hi][qri][ti] |= MAIN_PATH;

            // set boolean flags for if in query/truth variants
            bool in_truth_var = truth_ref_ptrs[i][FLAGS][ti] & PTR_VARIANT;    // inside variant
            if (ptr_type & (PTR_MAT|PTR_SWP_MAT|PTR_SUB|PTR_DEL))              // if consumes ref, then
                in_truth_var &= !(truth_ref_ptrs[i][FLAGS][ti] & PTR_VAR_BEG); // just moved into variant?

            bool in_query_var = (hi == ri) ? false : 
                    query_ref_ptrs[i][FLAGS][qri] & PTR_VARIANT;
            if (hi == qi && ptr_type & (PTR_MAT|PTR_SWP_MAT|PTR_SUB|PTR_DEL))
                in_query_var &= !(query_ref_ptrs[i][FLAGS][qri] & PTR_VAR_BEG);

            bool is_ins_loc = ref_has_ins[truth_ref_ptrs[i][PTRS][ti]] ||
                    (hi == ri ? ref_has_ins[qri] : 
                    ref_has_ins[query_ref_ptrs[i][PTRS][qri]]);

            // SYNC POINTS
            // enforce on main diag, diag mvmt, not in variant, not at insertion
            bool is_sync = !in_truth_var && !in_query_var && !is_ins_loc &&
                    truth_ref_ptrs[i][PTRS][ti] == (hi == ri ? qri :
                            query_ref_ptrs[i][PTRS][qri]) &&
                            ptr_type & (PTR_MAT | PTR_SWP_MAT | PTR_SUB);
            if (is_sync) aln_ptrs[hi][qri][ti] |= PTR_SYNC;
            sync[i].push_back(is_sync);

            // debug print
            std::string ptr_str;
            if (ptr_type == PTR_MAT) ptr_str = "MAT";
            if (ptr_type == PTR_SUB) ptr_str = "SUB";
            if (ptr_type == PTR_INS) ptr_str = "INS";
            if (ptr_type == PTR_DEL) ptr_str = "DEL";
            if (ptr_type == PTR_SWP_MAT) ptr_str = "SWP";
            if (print) printf("%s (%s, %2d, %2d) --%s-> (%s, %2d, %2d)\n",
                    is_sync ? "SYNC" : "    ", prev_hi % 2 ? "REF" : "QRY", 
                    prev_qri, prev_ti, 
                    ptr_str.data(), hi % 2 ? "REF" : "QRY", qri, ti);
        }

        if (print) {
            printf("%s ref_has_ins: ", aln_strs[i].data());
            for (int j = 0; j < r_size; j++) {
                printf("%s ", ref_has_ins[j] ? "T" : "F");
            }
            printf("\n");
        }

        // last position is sync
        if (print) printf("SYNC (%s, %2d, %2d) DONE \n",
                hi % 2 ? "REF" : "QRY", qri, ti);
        sync[i].push_back(true);
        aln_ptrs[hi][qri][ti] |= PTR_SYNC;
        edits[i].push_back(false);
    }
}


/******************************************************************************/


void calc_prec_recall(
        superclusterData * clusterdata_ptr, int sc_idx, 
        const std::string & ctg, 
        const std::string & ref,
        const std::string & query1, const std::string & query2, 
        const std::string & truth1, const std::string & truth2, 
        const std::vector< std::vector<idx1> > & path,
        const std::vector< std::vector<bool> > & sync,
        const std::vector< std::vector<bool> > & edits,
        const std::vector< std::vector<int> > & query1_ref_ptrs, 
        const std::vector< std::vector<int> > & ref_query1_ptrs,
        const std::vector< std::vector<int> > & query2_ref_ptrs, 
        const std::vector< std::vector<int> > & ref_query2_ptrs,
        const std::vector< std::vector<int> > & truth1_ref_ptrs, 
        const std::vector< std::vector<int> > & truth2_ref_ptrs,
        const std::vector<int> & pr_query_ref_end, bool print
        ) {

    // set query/truth strings and pointers
    int beg = clusterdata_ptr->superclusters[ctg]->begs[sc_idx];
    RefList4<std::string> query {{&query1, &query1, &query2, &query2}};
    RefList4<std::string> truth {{&truth1, &truth2, &truth1, &truth2}};
    RefList4< std::vector< std::vector<int> > > query_ref_ptrs {{
            &query1_ref_ptrs, &query1_ref_ptrs, &query2_ref_ptrs, &query2_ref_ptrs }};
    RefList4< std::vector< std::vector<int> > > ref_query_ptrs {{
            &ref_query1_ptrs, &ref_query1_ptrs, &ref_query2_ptrs, &ref_query2_ptrs }};
    RefList4< std::vector< std::vector<int> > > truth_ref_ptrs {{
            &truth1_ref_ptrs, &truth2_ref_ptrs, &truth1_ref_ptrs, &truth2_ref_ptrs }};

    // for only the selected phasing
    for (int i = 0; i < CALLSETS*HAPS; i++) {

        bool swap = (i == QUERY1_TRUTH2 || i == QUERY2_TRUTH1);

        int ri = i*2 + REF;   // ref index
        /* int qi = i*2 + QUERY; // call index (unused -> commented) */
        int qhi = i >> 1;     // query hap index
        int thi = i & 1;      // truth hap index

        // set variant ranges
        std::shared_ptr<ctgVariants> query_vars = 
                clusterdata_ptr->superclusters[ctg]->ctg_variants[QUERY][qhi];
        std::shared_ptr<ctgVariants> truth_vars = 
                clusterdata_ptr->superclusters[ctg]->ctg_variants[TRUTH][thi];
        int query_beg_idx = query_vars->clusters.size() ? query_vars->clusters[
            clusterdata_ptr->superclusters[ctg]->superclusters[QUERY][qhi][sc_idx]] : 0;
        int query_end_idx = query_vars->clusters.size() ? query_vars->clusters[
            clusterdata_ptr->superclusters[ctg]->superclusters[QUERY][qhi][sc_idx+1]] : 0;
        int truth_beg_idx = truth_vars->clusters.size() ? truth_vars->clusters[
            clusterdata_ptr->superclusters[ctg]->superclusters[TRUTH][thi][sc_idx]] : 0;
        int truth_end_idx = truth_vars->clusters.size() ? truth_vars->clusters[
            clusterdata_ptr->superclusters[ctg]->superclusters[TRUTH][thi][sc_idx+1]] : 0;

        // init
        int sync_group = 0;
        int ti_size = truth_ref_ptrs[i][PTRS].size();
        int hi = pr_query_ref_end[i];
        int qri_size = (hi % 2 == QUERY) ? query_ref_ptrs[i][PTRS].size() :
                ref_query_ptrs[i][PTRS].size();
        int prev_hi = hi;
        int qri = qri_size-1;
        int sync_ref_idx = ref_query_ptrs[i][PTRS].size();
        int prev_sync_ref_idx = sync_ref_idx;
        int prev_qri = qri;
        int ti = ti_size-1;
        int prev_ti = ti;
        int sync_truth_idx = ti_size;
        int prev_sync_truth_idx = ti_size;
        int query_ed = 0;
        int query_var_ptr = query_end_idx-1;
        int query_var_pos = query_vars->poss.size() && query_var_ptr >= 0 ? 
                query_vars->poss[query_var_ptr] - beg : 0;
        int prev_query_var_ptr = query_var_ptr;
        int truth_var_ptr = truth_end_idx-1;
        int truth_var_pos = truth_vars->poss.size() && truth_var_ptr >= 0 ? 
                truth_vars->poss[truth_var_ptr] - beg : 0;
        int prev_truth_var_ptr = truth_var_ptr;
        int sync_idx = sync[i].size()-1;

        if (print) {
            printf("%s Path:\n", aln_strs[i].data());
            for (int j = sync_idx; j >= 0; j--) {
                if (j == sync_idx)
                    printf("%s %s\n", sync[i][j] ? "*" : ".", edits[i][j] ? "X" : "=");
                else
                    printf("(%s,%d,%d)\n%s %s\n", path[i][j].hi % 2 ? "REF" : "QRY", 
                            path[i][j].qri, path[i][j].ti, 
                            sync[i][j] ? "*" : ".", edits[i][j] ? "X" : "=");
            }
            printf("\n%s: %d\n", aln_strs[i].data(), beg);
            printf("  ref: %2d %s\n", int(ref.size()), ref.data());
            printf("query: %2d %s\n", int(query[i].size()), query[i].data());
            printf("truth: %2d %s\n", int(truth[i].size()), truth[i].data());
            printf("query->ref ptrs:\n");
            print_ref_ptrs(query_ref_ptrs[i]);
            printf("ref->query ptrs:\n");
            print_ref_ptrs(ref_query_ptrs[i]);
            printf("truth->ref ptrs:\n");
            print_ref_ptrs(truth_ref_ptrs[i]);
        }

        if (print) {
            std::shared_ptr<ctgSuperclusters> sc = clusterdata_ptr->superclusters[ctg];
            printf("\n\nSupercluster: %d (%d-%d)\n", sc_idx, sc->begs[sc_idx], sc->ends[sc_idx]);
            for (int j = 0; j < CALLSETS*HAPS; j++) {
                int callset = j >> 1;
                int hap = j % 2;
                int cluster_beg = sc->superclusters[callset][hap][sc_idx];
                int cluster_end = sc->superclusters[callset][hap][sc_idx+1];
                printf("%s%d: %d clusters (%d-%d)\n", 
                    callset_strs[callset].data(), hap+1,
                    cluster_end-cluster_beg,
                    cluster_beg, cluster_end);

                for (int k = cluster_beg; k < cluster_end; k++) {
                    std::shared_ptr<ctgVariants> vars = sc->ctg_variants[callset][hap];
                    int variant_beg = vars->clusters[k];
                    int variant_end = vars->clusters[k+1];
                    printf("\tCluster %d: %d variants (%d-%d)\n", k, 
                        variant_end-variant_beg, variant_beg, variant_end);
                    for (int l = variant_beg; l < variant_end; l++) {
                        printf("\t\t%s %d\t%s\t%s\tQ=%f\n", ctg.data(), vars->poss[l], 
                        vars->refs[l].size() ?  vars->refs[l].data() : "_", 
                        vars->alts[l].size() ?  vars->alts[l].data() : "_",
                        vars->var_quals[l]);
                    }
                }
            }
        }

        // sync occurs between prev_var and var
        while (sync_idx >= 0) {

            // set corresponding REF position
            int query_ref_pos = 0;
            if (prev_hi == ri) { // REF, no variants
                query_ref_pos = prev_qri;
            } else { // hi == qi, QUERY
                query_ref_pos = query_ref_ptrs[i][PTRS][prev_qri];
            }

            // mark FPs and record variants within sync group
            while (query_ref_pos < query_var_pos && query_var_ptr >= query_beg_idx) { // passed query variant

                if (print)
                    printf("query ref pos: %d, query_var_pos: %d, @%s(%2d,%2d) = REF(%2d,%2d)\n",
                        query_ref_pos, query_var_pos, 
                        (hi == ri) ? "REF" : "QRY", qri, ti, 
                        (hi == ri) ? qri : query_ref_ptrs[i][PTRS][qri],
                        truth_ref_ptrs[i][PTRS][ti]);

                // FP if we passed a query variant when aligning on REF
                if (hi == ri) {
                    query_vars->errtypes[swap][query_var_ptr] = ERRTYPE_FP;
                    query_vars->sync_group[swap][query_var_ptr] = sync_group++;
                    query_vars->credit[swap][query_var_ptr] = 0;
                    query_vars->ref_ed[swap][query_var_ptr] = 0;
                    query_vars->query_ed[swap][query_var_ptr] = 0;
                    query_vars->callq[swap][query_var_ptr] = 
                        query_vars->var_quals[query_var_ptr];
                    if (print) printf("%s: QUERY REF='%s'\tALT='%s'\t%s\t%f\n", ctg.data(),
                            query_vars->refs[query_var_ptr].data(),
                            query_vars->alts[query_var_ptr].data(), "FP", 0.0f);
                    query_var_ptr--;

                } else { // we passed the query variant when aligning on QUERY, calc status below
                    query_var_ptr--;
                }

                // update to next query variant to be found
                query_var_pos = (query_var_ptr < query_beg_idx) ? -1 :
                        query_vars->poss[query_var_ptr] - beg;
            }

            // count truth variants
            int truth_ref_pos = truth_ref_ptrs[i][PTRS][prev_ti];
            while (truth_ref_pos < truth_var_pos && truth_var_ptr >= truth_beg_idx) { // passed truth variant

                // update to next truth variant to be found
                truth_var_ptr--;
                truth_var_pos = (truth_var_ptr < truth_beg_idx) ? -1 :
                    truth_vars->poss[truth_var_ptr] - beg;
            }

            // sync point: set TP
            if (sync[i][sync_idx]) {

                // calculate edit distance without variants
                int ref_ed = 0;
                sync_ref_idx = (prev_hi == ri) ? prev_qri+1 : query_ref_ptrs[i][PTRS][prev_qri]+1;
                sync_truth_idx = prev_ti+1;
                std::vector< std::vector<int> > offs, ptrs;
                wf_ed(ref.substr(sync_ref_idx, prev_sync_ref_idx - sync_ref_idx), 
                        truth[i].substr(sync_truth_idx, prev_sync_truth_idx - sync_truth_idx), 
                        ref_ed, offs, ptrs);

                // the following cases should never happen
                bool warn = false;
                if (prev_truth_var_ptr == truth_var_ptr && ref_ed != 0) {
                    WARN("Nonzero reference edit distance with no truth variants at ctg %s supercluster %d", ctg.data(), sc_idx);
                    warn = true;
                }
                if (prev_query_var_ptr == query_var_ptr && query_ed != ref_ed) {
                    WARN("Query edit distance changed with no query variants at ctg %s supercluster %d", ctg.data(), sc_idx);
                    warn = true;
                }
                if (query_ed > ref_ed) {
                    WARN("Query edit distance exceeds reference edit distance at ctg %s supercluster %d", ctg.data(), sc_idx);
                    warn = true;
                }

                // This only happens when the truth VCF contains several variants that, when
                // combined, are equivalent to no variants. In this case, the truth VCF should
                // be fixed. Output a warning, and allow the evaluation to continue.
                if (ref_ed == 0 && truth_var_ptr != prev_truth_var_ptr) {
                    warn = true;
                    WARN("Zero edit distance with truth variants at ctg %s supercluster %d", ctg.data(), sc_idx);
                    ref_ed = 1; // prevent divide-by-zero
                }
                
                if (warn) {
                    printf("\n\nSupercluster: %d\n", sc_idx);
                    std::shared_ptr<ctgSuperclusters> sc = clusterdata_ptr->superclusters[ctg];
                    for (int j = 0; j < CALLSETS*HAPS; j++) {
                        int callset = j >> 1;
                        int hap = j % 2;
                        int cluster_beg = sc->superclusters[callset][hap][sc_idx];
                        int cluster_end = sc->superclusters[callset][hap][sc_idx+1];
                        printf("%s%d: %d clusters (%d-%d)\n", 
                            callset_strs[callset].data(), hap+1,
                            cluster_end-cluster_beg,
                            cluster_beg, cluster_end);

                        for (int k = cluster_beg; k < cluster_end; k++) {
                            std::shared_ptr<ctgVariants> vars = sc->ctg_variants[callset][hap];
                            int variant_beg = vars->clusters[k];
                            int variant_end = vars->clusters[k+1];
                            printf("\tCluster %d: %d variants (%d-%d)\n", k, 
                                variant_end-variant_beg, variant_beg, variant_end);
                            for (int l = variant_beg; l < variant_end; l++) {
                                printf("\t\t%s %d\t%s\t%s\tQ=%f\n", ctg.data(), vars->poss[l], 
                                vars->refs[l].size() ?  vars->refs[l].data() : "_", 
                                vars->alts[l].size() ?  vars->alts[l].data() : "_",
                                vars->var_quals[l]);
                            }
                        }
                    }
                }

                if (false) { // debug
                    printf("%s Path:\n", aln_strs[i].data());
                    for (int j = sync[i].size()-1; j >= 0; j--) {
                        if (j == int(sync[i].size())-1)
                            printf("%s %s\n", sync[i][j] ? "*" : ".", edits[i][j] ? "X" : "=");
                        else
                            printf("(%s,%d,%d)\n%s %s\n", path[i][j].hi % 2 ? "REF" : "QRY", 
                                    path[i][j].qri, path[i][j].ti, 
                                    sync[i][j] ? "*" : ".", edits[i][j] ? "X" : "=");
                    }
                    printf("\n%s: %d\n", aln_strs[i].data(), beg);
                    printf("  ref: %2d %s\n", int(ref.size()), ref.data());
                    printf("query: %2d %s\n", int(query[i].size()), query[i].data());
                    printf("truth: %2d %s\n", int(truth[i].size()), truth[i].data());
                    printf("query->ref ptrs:\n");
                    print_ref_ptrs(query_ref_ptrs[i]);
                    printf("ref->query ptrs:\n");
                    print_ref_ptrs(ref_query_ptrs[i]);
                    printf("truth->ref ptrs:\n");
                    print_ref_ptrs(truth_ref_ptrs[i]);

                    printf("  ref[%2d:+%2d] ", sync_ref_idx, prev_sync_ref_idx-sync_ref_idx);
                    printf("%s\n", ref.substr(sync_ref_idx, 
                                prev_sync_ref_idx - sync_ref_idx).data());
                    printf("truth[%2d:+%2d] ", sync_truth_idx, prev_sync_truth_idx-sync_truth_idx);
                    printf("%s\n", truth[i].substr(sync_truth_idx, 
                                prev_sync_truth_idx - sync_truth_idx).data());
                }

                // get min query var qual in sync section (for truth/query)
                float callq = g.max_qual;
                for (int query_var_idx = prev_query_var_ptr; 
                        query_var_idx > query_var_ptr; query_var_idx--) {
                    callq = std::min(callq, query_vars->var_quals[query_var_idx]);
                }

                // process QUERY variants
                for (int query_var_idx = prev_query_var_ptr; 
                        query_var_idx > query_var_ptr; query_var_idx--) {
                    float credit = 1 - float(query_ed)/ref_ed;
                    // don't overwrite FPs
                    if (query_vars->errtypes[swap][query_var_idx] == ERRTYPE_UN) {
                        if (credit >= g.credit_threshold) { // TP
                            query_vars->errtypes[swap][query_var_idx] = ERRTYPE_TP;
                            query_vars->sync_group[swap][query_var_idx] = sync_group;
                            query_vars->credit[swap][query_var_idx] = credit;
                            query_vars->ref_ed[swap][query_var_idx] = ref_ed;
                            query_vars->query_ed[swap][query_var_idx] = query_ed;
                            query_vars->callq[swap][query_var_idx] = callq;
                            if (print) printf("%s:%d QUERY REF='%s'\tALT='%s'\t%s\t%f\n", 
                                    ctg.data(), query_vars->poss[query_var_idx],
                                    query_vars->refs[query_var_idx].data(),
                                    query_vars->alts[query_var_idx].data(), 
                                    "TP", credit);
                        } else { // FP
                            query_vars->errtypes[swap][query_var_idx] = ERRTYPE_FP;
                            query_vars->sync_group[swap][query_var_idx] = sync_group;
                            query_vars->credit[swap][query_var_idx] = credit;
                            query_vars->ref_ed[swap][query_var_idx] = ref_ed;
                            query_vars->query_ed[swap][query_var_idx] = query_ed;
                            query_vars->callq[swap][query_var_idx] = callq;
                            if (print) printf("%s:%d QUERY REF='%s'\tALT='%s'\t%s\t%f\n", 
                                    ctg.data(), query_vars->poss[query_var_idx],
                                    query_vars->refs[query_var_idx].data(),
                                    query_vars->alts[query_var_idx].data(), 
                                    "FP", 0.0f);
                        }
                    }
                }

                // process TRUTH variants
                for (int truth_var_idx = prev_truth_var_ptr; 
                        truth_var_idx > truth_var_ptr; truth_var_idx--) {
                    float credit = 1 - float(query_ed)/ref_ed;
                    if (credit >= g.credit_threshold) { // TP
                        truth_vars->errtypes[swap][truth_var_idx] = ERRTYPE_TP;
                        truth_vars->sync_group[swap][truth_var_idx] = sync_group;
                        truth_vars->credit[swap][truth_var_idx] = credit;
                        truth_vars->ref_ed[swap][truth_var_idx] = ref_ed;
                        truth_vars->query_ed[swap][truth_var_idx] = query_ed;
                        truth_vars->callq[swap][truth_var_idx] = callq;
                        if (print) printf("%s:%d TRUTH REF='%s'\tALT='%s'\t%s\t%f\n", 
                                ctg.data(), truth_vars->poss[truth_var_idx],
                                truth_vars->refs[truth_var_idx].data(),
                                truth_vars->alts[truth_var_idx].data(), 
                                "TP", credit);
                    } else { // FP call, FN truth
                        truth_vars->errtypes[swap][truth_var_idx] = ERRTYPE_FN;
                        truth_vars->sync_group[swap][truth_var_idx] = sync_group;
                        truth_vars->credit[swap][truth_var_idx] = credit;
                        truth_vars->ref_ed[swap][truth_var_idx] = ref_ed;
                        truth_vars->query_ed[swap][truth_var_idx] = query_ed;
                        truth_vars->callq[swap][truth_var_idx] = g.max_qual;
                        if (print) printf("%s:%d TRUTH REF='%s'\tALT='%s'\t%s\t%f\n",
                                ctg.data(), truth_vars->poss[truth_var_idx],
                                truth_vars->refs[truth_var_idx].data(),
                                truth_vars->alts[truth_var_idx].data(), 
                                "FN", credit);
                    } 
                }

                if (print) printf("%s: SYNC @%s(%2d,%2d) = REF(%2d,%2d), ED %d->%d, QUERY %d-%d, TRUTH %d-%d\n\n", ctg.data(),
                        (prev_hi == ri) ? "REF" : "QRY", prev_qri, prev_ti,
                        (prev_hi == ri) ? prev_qri : query_ref_ptrs[i][PTRS][prev_qri],
                        truth_ref_ptrs[i][PTRS][prev_ti], ref_ed, query_ed, 
                        prev_query_var_ptr, query_var_ptr,
                        prev_truth_var_ptr, truth_var_ptr
                );

                // new sync group if there were any variants
                if (query_var_ptr != prev_query_var_ptr || 
                    truth_var_ptr != prev_truth_var_ptr) {
                    sync_group++;
                }
                prev_query_var_ptr = query_var_ptr;
                prev_truth_var_ptr = truth_var_ptr;
                prev_sync_ref_idx = sync_ref_idx;
                prev_sync_truth_idx = sync_truth_idx;
                query_ed = 0;
            }

            // update pointers and edit distance
            if (print) printf("%s %s (%2d,%2d) %s\n", 
                    sync[i][sync_idx] ? "*" : " ",
                    (prev_hi == ri) ? "REF" : "QRY", prev_qri, prev_ti,
                    edits[i][sync_idx] ? "X" : "=");

            // update path pointer
            query_ed += edits[i][sync_idx];
            sync_idx--;
            if (sync_idx < 0) break;

            // traversing backwards, but prev_i comes before i numerically
            qri = prev_qri;
            ti = prev_ti;
            hi = prev_hi;
            prev_qri = path[i][sync_idx].qri;
            prev_ti = path[i][sync_idx].ti;
            prev_hi = path[i][sync_idx].hi;

            // update next variant position
            query_var_pos = (query_var_ptr < query_beg_idx) ? -1 :
                query_vars->poss[query_var_ptr] - beg;
            truth_var_pos = (truth_var_ptr < truth_beg_idx) ? -1 :
                truth_vars->poss[truth_var_ptr] - beg;
        }
    }
}


/******************************************************************************/

void wf_ed(
        const std::string & query, const std::string & truth, int & s, 
        std::vector< std::vector<int> > & offs,
        std::vector< std::vector<int> > & ptrs, bool print
        ) {

    // alignment
    int query_len = query.size();
    int truth_len = truth.size();

    // early exit if either string is empty
    if (!query_len) { s = truth_len; return; }
    if (!truth_len) { s = query_len; return; }
    s = 0;

    int mat_len = query_len + truth_len - 1;
    offs.push_back(std::vector<int>(mat_len, -2));
    offs[0][query_len-1] = -1;
    ptrs.push_back(std::vector<int>(mat_len, PTR_NONE));
    bool done = false;
    while (true) {

        // EXTEND WAVEFRONT
        for (int d = 0; d < mat_len; d++) {
            int off = offs[s][d];
            int diag = d + 1 - query_len;

            // don't allow starting from untouched cells
            if (off == -2) continue;

            // check that it's within matrix
            if (diag + off + 1 < 0) continue;
            if (off > query_len - 1) continue;
            if (diag + off > truth_len - 1) continue;

            // extend
            while (off < query_len - 1 && 
                   diag + off < truth_len - 1) {
                if (query[off+1] == truth[diag+off+1]) off++;
                else break;
            }
            if (off > offs[s][d])
                if(print) printf("(%d, %d) extend\n", off, off+diag);
            offs[s][d] = off;

            // finish if done
            if (off == query_len - 1 && off + diag == truth_len - 1)
            { done = true; break; }

        }
        if (done) break;

        // debug print
        if(print) printf("offs %d:", s);
        for (int di = 0; di < int(query.size() + truth.size()-1); di++) {
            if(print) printf("\t%d", offs[s][di]);
        }
        if(print) printf("\n");

        // NEXT WAVEFRONT
        offs.push_back(std::vector<int>(mat_len, -2));
        ptrs.push_back(std::vector<int>(mat_len, PTR_NONE));
        s++;
        if(print) printf("\nscore = %d\n", s);
        for (int d = 0; d < mat_len; d++) {
            int diag = d + 1 - query_len;

            // SUB
            if (s-1 >= 0 && offs[s-1][d] != -2 && 
                            offs[s-1][d]+1 < query_len &&
                     diag + offs[s-1][d]+1 < truth_len &&
                            offs[s-1][d]+1 >= offs[s][d]) {
                offs[s][d] = offs[s-1][d] + 1;
                ptrs[s][d] |= PTR_SUB;
                if(print) printf("(%d, %d) sub\n", offs[s][d], offs[s][d]+diag);
            }

            // DEL
            if (s-1 >= 0 && d > 0 && 
                           offs[s-1][d-1] != -2 &&
                    diag + offs[s-1][d-1] < truth_len &&
                           offs[s-1][d-1] >= offs[s][d]) {
                offs[s][d] = offs[s-1][d-1];
                ptrs[s][d] |= PTR_DEL;
                if(print) printf("(%d, %d) del\n", offs[s][d], offs[s][d]+diag);
            }

            // INS
            if (s-1 >= 0 && d < mat_len-1 && 
                           offs[s-1][d+1] != -2 &&
                           offs[s-1][d+1]+1 < query_len &&
                    diag + offs[s-1][d+1]+1 < truth_len &&
                    diag + offs[s-1][d+1]+1 >= -1 &&
                           offs[s-1][d+1]+1 >= offs[s][d]) {
                offs[s][d] = offs[s-1][d+1]+1;
                ptrs[s][d] |= PTR_INS;
                if(print) printf("(%d, %d) ins\n", offs[s][d], offs[s][d]+diag);
            }
        }
    }
}

/******************************************************************************/

void wf_swg_align(
        const std::string & query, const std::string & truth, 
        std::vector< std::vector< std::vector<uint8_t> > > & ptrs,
        std::vector< std::vector< std::vector<int> > > & offs,
        int & s, int x, int o, int e, bool print
        ) {

    // init
    int query_len = query.size();
    int truth_len = truth.size();
    int mat_len = query_len + truth_len - 1;
    bool done = false;
    for (int m = 0; m < MATS; m++) {
        offs[m].push_back(std::vector<int>(mat_len, -2));
        ptrs[m].push_back(std::vector<uint8_t>(mat_len, PTR_NONE));
    }
    s = 0;
    offs[MAT_SUB][s][query_len-1] = -1; // main diag
    ptrs[MAT_SUB][s][query_len-1] = PTR_MAT;

    while (true) {

        // EXTEND WAVEFRONT (leave INS, DEL)
        for (int m = MAT_INS; m < MATS; m++) {
            for (int d = 0; d < mat_len; d++) {
                int off = offs[m][s][d];
                int diag = d + 1 - query_len;

                if (off >= 0 && off < query_len &&
                        diag+off >= 0 && diag+off < truth_len &&
                        offs[m][s][d] >= offs[MAT_SUB][s][d]) {
                    offs[MAT_SUB][s][d] = offs[m][s][d];
                    ptrs[MAT_SUB][s][d] |= (m == MAT_INS) ? PTR_INS : PTR_DEL;
                    if(print) printf("(S, %d, %d) swap\n", offs[MAT_SUB][s][d], 
                            offs[MAT_SUB][s][d]+d+1-query_len);
                }
            }
        }

        // EXTEND WAVEFRONT (diag, SUB only)
        for (int d = 0; d < mat_len; d++) {
            int off = offs[MAT_SUB][s][d];
            int diag = d + 1 - query_len;

            // extend
            while ( off != -2 && diag + off >= -1 && 
                    off < query_len - 1 && 
                    diag + off < truth_len - 1) {
                if (query[off+1] == truth[diag+off+1]) off++;
                else break;
            }
            if (off > offs[MAT_SUB][s][d])
                if(print) printf("(S, %d, %d) extend\n", off, off+diag);
            offs[MAT_SUB][s][d] = off;

            // finish if done
            if (off == query_len - 1 && off + diag == truth_len - 1)
            { done = true; break; }
        }
        if (done) break;

        // debug print
        for (int mi = 0; mi < MATS; mi++) {
            if(print) printf("\n%s matrix\n", type_strs[mi+1].data());
            if(print) printf("offs %d:", s);
            for (int di = 0; di < int(query.size() + truth.size()-1); di++) {
                if(print) printf("\t%d", offs[mi][s][di]);
            }
            if(print) printf("\n");
        }

        // NEXT WAVEFRONT
        s++;
        if(print) printf("\nscore = %d\n", s);
        for (int m = 0; m < MATS; m++) {
            offs[m].push_back(std::vector<int>(mat_len, -2));
            ptrs[m].push_back(std::vector<uint8_t>(mat_len, PTR_NONE));
        }

        for (int d = 0; d < mat_len; d++) {
            int diag = d + 1 - query_len;

            // sub (in SUB)
            if (s-x >= 0 && offs[MAT_SUB][s-x][d] != -2 && 
                            offs[MAT_SUB][s-x][d]+1 < query_len &&
                     diag + offs[MAT_SUB][s-x][d]+1 < truth_len &&
                            offs[MAT_SUB][s-x][d]+1 >= offs[MAT_SUB][s][d]) {
                offs[MAT_SUB][s][d] = offs[MAT_SUB][s-x][d] + 1;
                ptrs[MAT_SUB][s][d] |= PTR_SUB;
                if(print) printf("(S, %d, %d) sub\n", offs[MAT_SUB][s][d], 
                        offs[MAT_SUB][s][d]+diag);
            }

            // open gap (enter DEL)
            if (s-(o+e) >= 0 && d > 0 && 
                           offs[MAT_SUB][s-(o+e)][d-1] != -2 &&
                    diag + offs[MAT_SUB][s-(o+e)][d-1] < truth_len &&
                           offs[MAT_SUB][s-(o+e)][d-1] >= offs[MAT_DEL][s][d]) {
                offs[MAT_DEL][s][d] = offs[MAT_SUB][s-(o+e)][d-1];
                ptrs[MAT_DEL][s][d] |= PTR_SUB;
                if(print) printf("(D, %d, %d) open\n", offs[MAT_DEL][s][d], 
                        offs[MAT_DEL][s][d]+diag);
            }

            // open gap (enter INS)
            if (s-(o+e) >= 0 && d < mat_len-1 && 
                           offs[MAT_SUB][s-(o+e)][d+1] != -2 &&
                           offs[MAT_SUB][s-(o+e)][d+1]+1 < query_len &&
                    diag + offs[MAT_SUB][s-(o+e)][d+1]+1 < truth_len &&
                    diag + offs[MAT_SUB][s-(o+e)][d+1]+1 >= 0 &&
                           offs[MAT_SUB][s-(o+e)][d+1]+1 >= offs[MAT_INS][s][d]) {
                offs[MAT_INS][s][d] = offs[MAT_SUB][s-(o+e)][d+1]+1;
                ptrs[MAT_INS][s][d] |= PTR_SUB;
                if(print) printf("(I, %d, %d) open\n", offs[MAT_INS][s][d], 
                        offs[MAT_INS][s][d]+diag);
            }

            // extend gap (stay DEL)
            if (s-e >= 0 && d > 0 && 
                           offs[MAT_DEL][s-e][d-1] != -2 &&
                    diag + offs[MAT_DEL][s-e][d-1] < truth_len &&
                           offs[MAT_DEL][s-e][d-1] >= offs[MAT_DEL][s][d]) {
                offs[MAT_DEL][s][d] = offs[MAT_DEL][s-e][d-1];
                ptrs[MAT_DEL][s][d] |= PTR_DEL;
                if(print) printf("(D, %d, %d) extend\n", offs[MAT_DEL][s][d], 
                        offs[MAT_DEL][s][d]+diag);
            }

            // extend gap (stay INS)
            if (s-e >= 0 && d < mat_len-1 && 
                           offs[MAT_INS][s-e][d+1] != -2 &&
                           offs[MAT_INS][s-e][d+1]+1 < query_len &&
                    diag + offs[MAT_INS][s-e][d+1]+1 < truth_len &&
                    diag + offs[MAT_INS][s-e][d+1]+1 >= 0 &&
                           offs[MAT_INS][s-e][d+1]+1 >= offs[MAT_INS][s][d]) {
                offs[MAT_INS][s][d] = offs[MAT_INS][s-e][d+1]+1;
                ptrs[MAT_INS][s][d] |= PTR_INS;
                if(print) printf("(I, %d, %d) extend\n", offs[MAT_INS][s][d], 
                        offs[MAT_INS][s][d]+diag);
            }
        }
    }
}

/******************************************************************************/

void precision_recall_threads_wrapper(
        std::shared_ptr<superclusterData> clusterdata_ptr,
        std::vector< std::vector< std::vector<int> > > sc_groups) {
    if (g.verbosity >= 1) INFO(" ");
    if (g.verbosity >= 1) INFO("%s[5/8] Calculating precision and recall%s",
            COLOR_PURPLE, COLOR_WHITE);

    if (g.verbosity >= 1)
    for (int i = 0; i < g.thread_nsteps; i++) {
        INFO("  Superclusters using %7.3f to %7.3f GB RAM each (%3d threads): %8d",
                i == 0 ? 0 : g.ram_steps[i-1], g.ram_steps[i], g.thread_steps[i],
                int(sc_groups[i][CTG_IDX].size()));
    }

    int pool_workers = std::max(0, g.max_threads - 1);
    g_pr_pool = new PrThreadPool(pool_workers);

    for (int thread_step = g.thread_nsteps - 1; thread_step >= 0; thread_step--) {
        int nscs = static_cast<int>(sc_groups[thread_step][SC_IDX].size());
        if (nscs == 0) continue;

        std::atomic<int> next_sc(0);

        auto worker = [&]() {
            while (true) {
                int idx = next_sc.fetch_add(1, std::memory_order_relaxed);
                if (idx >= nscs) break;
                precision_recall_wrapper(
                    clusterdata_ptr.get(),
                    std::cref(sc_groups),
                    thread_step, idx, idx + 1);
            }
        };

        for (int t = 0; t < pool_workers; t++)
            g_pr_pool->submit(worker);
        worker();
        g_pr_pool->wait_idle();
    }

    delete g_pr_pool;
    g_pr_pool = nullptr;
}

/******************************************************************************/

void precision_recall_wrapper(
        superclusterData* clusterdata_ptr,
        const std::vector< std::vector< std::vector<int> > > & sc_groups,
        int thread_step, int start, int stop) {

    if (stop == start) return;

    for (int idx = start; idx < stop; idx++) {
        std::string ctg = clusterdata_ptr->contigs[
            sc_groups[thread_step][CTG_IDX][idx]];
        int sc_idx = sc_groups[thread_step][SC_IDX][idx];

        // set superclusters pointer
        std::shared_ptr<ctgSuperclusters> sc = 
                clusterdata_ptr->superclusters[ctg];

        /////////////////////////////////////////////////////////////////////
        // DEBUG PRINTING                                                    
        /////////////////////////////////////////////////////////////////////
        if (false) {
            // print cluster info
            printf("\n\nSupercluster: %d (%d-%d)\n", sc_idx, sc->begs[sc_idx], sc->ends[sc_idx]);
            for (int i = 0; i < CALLSETS*HAPS; i++) {
                int callset = i >> 1;
                int hap = i % 2;
                int cluster_beg = sc->superclusters[callset][hap][sc_idx];
                int cluster_end = sc->superclusters[callset][hap][sc_idx+1];
                printf("%s%d: %d clusters (%d-%d)\n", 
                    callset_strs[callset].data(), hap+1,
                    cluster_end-cluster_beg,
                    cluster_beg, cluster_end);

                for (int j = cluster_beg; j < cluster_end; j++) {
                    std::shared_ptr<ctgVariants> vars = sc->ctg_variants[callset][hap];
                    int variant_beg = vars->clusters[j];
                    int variant_end = vars->clusters[j+1];
                    printf("\tCluster %d: %d variants (%d-%d)\n", j, 
                        variant_end-variant_beg, variant_beg, variant_end);
                    for (int k = variant_beg; k < variant_end; k++) {
                        printf("\t\t%s %d\t%s\t%s\tQ=%f\n", ctg.data(), vars->poss[k], 
                        vars->refs[k].size() ?  vars->refs[k].data() : "_", 
                        vars->alts[k].size() ?  vars->alts[k].data() : "_",
                        vars->var_quals[k]);
                    }
                }
            }
        }

        /////////////////////////////////////////////////////////////////////
        // PRECISION-RECALL: allow skipping called variants                  
        /////////////////////////////////////////////////////////////////////
        
        // set pointers between each hap (query1/2, truth1/2) and reference
        std::string query1 = "", ref_q1 = ""; 
        std::vector< std::vector<int> > query1_ref_ptrs, ref_query1_ptrs;
        generate_ptrs_strs(
                query1, ref_q1, query1_ref_ptrs, ref_query1_ptrs, 
                sc->ctg_variants[QUERY][HAP1], 
                sc->superclusters[QUERY][HAP1][sc_idx],
                sc->superclusters[QUERY][HAP1][sc_idx+1],
                sc->begs[sc_idx], sc->ends[sc_idx], 
                clusterdata_ptr->ref, ctg);

        std::string query2 = "", ref_q2 = ""; 
        std::vector< std::vector<int> > query2_ref_ptrs, ref_query2_ptrs;
        generate_ptrs_strs(
                query2, ref_q2, query2_ref_ptrs, ref_query2_ptrs, 
                sc->ctg_variants[QUERY][HAP2],
                sc->superclusters[QUERY][HAP2][sc_idx],
                sc->superclusters[QUERY][HAP2][sc_idx+1],
                sc->begs[sc_idx], sc->ends[sc_idx], 
                clusterdata_ptr->ref, ctg);

        std::string truth1 = "", ref_t1 = ""; 
        std::vector< std::vector<int> > truth1_ref_ptrs, ref_truth1_ptrs;
        generate_ptrs_strs(
                truth1, ref_t1, truth1_ref_ptrs, ref_truth1_ptrs, 
                sc->ctg_variants[TRUTH][HAP1],
                sc->superclusters[TRUTH][HAP1][sc_idx],
                sc->superclusters[TRUTH][HAP1][sc_idx+1],
                sc->begs[sc_idx], sc->ends[sc_idx], 
                clusterdata_ptr->ref, ctg);

        std::string truth2 = "", ref_t2 = ""; 
        std::vector< std::vector<int> > truth2_ref_ptrs, ref_truth2_ptrs;
        generate_ptrs_strs(
                truth2, ref_t2, truth2_ref_ptrs, ref_truth2_ptrs, 
                sc->ctg_variants[TRUTH][HAP2],
                sc->superclusters[TRUTH][HAP2][sc_idx],
                sc->superclusters[TRUTH][HAP2][sc_idx+1],
                sc->begs[sc_idx], sc->ends[sc_idx], 
                clusterdata_ptr->ref, ctg);

        // calculate four forward-pass alignment edit dists
        // query1-truth2, query1-truth1, query2-truth1, query2-truth2
        std::vector<int> aln_score(HAPS*CALLSETS);
        std::vector<int> aln_query_ref_end(HAPS*CALLSETS);
        std::vector< std::vector< std::vector<uint8_t> > > aln_ptrs;
        aln_ptrs.push_back(std::vector< std::vector<uint8_t> >(query1.size(), // Q1T1
                    std::vector<uint8_t>(truth1.size(), PTR_NONE)));
        aln_ptrs.push_back(std::vector< std::vector<uint8_t> >(ref_q1.size(),
                    std::vector<uint8_t>(truth1.size(), PTR_NONE)));
        aln_ptrs.push_back(std::vector< std::vector<uint8_t> >(query1.size(), // Q1T2
                    std::vector<uint8_t>(truth2.size(), PTR_NONE)));
        aln_ptrs.push_back(std::vector< std::vector<uint8_t> >(ref_q1.size(),
                    std::vector<uint8_t>(truth2.size(), PTR_NONE)));
        aln_ptrs.push_back(std::vector< std::vector<uint8_t> >(query2.size(), // Q2T1
                    std::vector<uint8_t>(truth1.size(), PTR_NONE)));
        aln_ptrs.push_back(std::vector< std::vector<uint8_t> >(ref_q2.size(),
                    std::vector<uint8_t>(truth1.size(), PTR_NONE)));
        aln_ptrs.push_back(std::vector< std::vector<uint8_t> >(query2.size(), // Q2T2
                    std::vector<uint8_t>(truth2.size(), PTR_NONE)));
        aln_ptrs.push_back(std::vector< std::vector<uint8_t> >(ref_q2.size(),
                    std::vector<uint8_t>(truth2.size(), PTR_NONE)));
        std::vector< std::shared_ptr< std::unordered_map<idx1, idx1> > > swap_pred_maps; 
        for (int i = 0; i < CALLSETS*HAPS; i++)
            swap_pred_maps.push_back(std::shared_ptr< std::unordered_map<idx1, idx1> >(new std::unordered_map<idx1, idx1>()));

        if (g_pr_pool) {
            std::atomic<int> n_done{0};
            for (int ti = 0; ti < CALLSETS*HAPS; ti++) {
                g_pr_pool->submit([&, ti]{
                    calc_prec_recall_aln(
                        query1, query2, truth1, truth2, ref_q1,
                        query1_ref_ptrs, ref_query1_ptrs,
                        query2_ref_ptrs, ref_query2_ptrs,
                        truth1_ref_ptrs, truth2_ref_ptrs,
                        aln_score, aln_ptrs, swap_pred_maps,
                        aln_query_ref_end, ti, ti+1, false);
                    n_done.fetch_add(1, std::memory_order_relaxed);
                });
            }
            while (n_done.load(std::memory_order_relaxed) < CALLSETS*HAPS) {
                if (!g_pr_pool->try_steal_exec()) std::this_thread::yield();
            }
        } else {
            calc_prec_recall_aln(
                    query1, query2, truth1, truth2, ref_q1,
                    query1_ref_ptrs, ref_query1_ptrs,
                    query2_ref_ptrs, ref_query2_ptrs,
                    truth1_ref_ptrs, truth2_ref_ptrs,
                    aln_score, aln_ptrs, swap_pred_maps,
                    aln_query_ref_end, 0, CALLSETS*HAPS, false);
        }

        // store optimal phasing for each supercluster
        // ORIG: query1-truth1 and query2-truth2
        // SWAP: query1-truth2 and query2-truth1
        store_phase(clusterdata_ptr, ctg, sc_idx, aln_score);

        // calculate paths from alignment
        std::vector< std::vector<idx1> > path(CALLSETS*HAPS);
        std::vector< std::vector<bool> > sync(CALLSETS*HAPS);
        std::vector< std::vector<bool> > edit(CALLSETS*HAPS);
        std::vector< std::vector< std::vector<uint8_t> > > path_ptrs;
        std::vector< std::vector< std::vector<int16_t> > > path_scores;
        calc_prec_recall_path(
                ref_q1, query1, query2, truth1, truth2,
                path, sync, edit, aln_ptrs, path_ptrs, path_scores,
                query1_ref_ptrs, ref_query1_ptrs, 
                query2_ref_ptrs, ref_query2_ptrs, 
                truth1_ref_ptrs, truth2_ref_ptrs,
                swap_pred_maps, aln_query_ref_end, false);

        // calculate precision/recall from paths
        calc_prec_recall(
                clusterdata_ptr, sc_idx, ctg, ref_q1, query1, query2, 
                truth1, truth2, path, sync, edit,
                query1_ref_ptrs, ref_query1_ptrs, 
                query2_ref_ptrs, ref_query2_ptrs,
                truth1_ref_ptrs, truth2_ref_ptrs,
                aln_query_ref_end, false);
    }
}

/******************************************************************************/

editData edits_wrapper(std::shared_ptr<superclusterData> clusterdata_ptr) {
    if (g.verbosity >= 1) INFO(" ");
    if (g.verbosity >= 1) INFO("%s[6/8] Calculating edit distance metrics%s",
            COLOR_PURPLE, COLOR_WHITE);

    // +2 since it's inclusive, but then also needs to include one quality higher
    // which doesn't contain any variants (to get draft reference edit dist)
    std::vector<int> all_qual_dists(g.max_qual+2, 0);
    editData edits;
    int ctg_id = 0;
    if (g.verbosity >= 1) INFO("  Contigs:");
    for (std::string ctg : clusterdata_ptr->contigs) {
        std::vector<int> ctg_qual_dists(g.max_qual+2,0);

        // set superclusters pointer
        std::shared_ptr<ctgSuperclusters> sc = clusterdata_ptr->superclusters[ctg];

        // iterate over superclusters
        for(int sc_idx = 0; sc_idx < sc->n; sc_idx++) {

            /////////////////////////////////////////////////////////////////////
            // DEBUG PRINTING                                                    
            /////////////////////////////////////////////////////////////////////
            if (false) {
                // print cluster info
                printf("\n\nSupercluster: %d\n", sc_idx);
                for (int i = 0; i < CALLSETS*HAPS; i++) {
                    int callset = i >> 1;
                    int hap = i % 2;
                    std::shared_ptr<ctgVariants> vars = sc->ctg_variants[callset][hap];
                    int cluster_beg = sc->superclusters[callset][hap][sc_idx];
                    int cluster_end = sc->superclusters[callset][hap][sc_idx+1];
                    printf("%s%d: %d clusters (%d-%d)\n", 
                        callset_strs[callset].data(), hap+1,
                        cluster_end-cluster_beg,
                        cluster_beg, cluster_end);

                    for (int j = cluster_beg; j < cluster_end; j++) {
                        int variant_beg = vars->clusters[j];
                        int variant_end = vars->clusters[j+1];
                        printf("\tCluster %d: %d variants (%d-%d)\n", j, 
                            variant_end-variant_beg, variant_beg, variant_end);
                        for (int k = variant_beg; k < variant_end; k++) {
                            printf("\t\t%s %d\t%s\t%s\tQ=%f\n", ctg.data(), vars->poss[k], 
                            vars->refs[k].size() ?  vars->refs[k].data() : "_", 
                            vars->alts[k].size() ?  vars->alts[k].data() : "_",
                            vars->var_quals[k]);
                        }
                    }
                }
            }

            // set pointers between truth1/2 and reference
            std::string truth1 = "", ref_t1 = ""; 
            std::vector< std::vector<int> > truth1_ref_ptrs, ref_truth1_ptrs;
            generate_ptrs_strs(
                    truth1, ref_t1, truth1_ref_ptrs, ref_truth1_ptrs, 
                    sc->ctg_variants[TRUTH][HAP1],
                    sc->superclusters[TRUTH][HAP1][sc_idx],
                    sc->superclusters[TRUTH][HAP1][sc_idx+1],
                    sc->begs[sc_idx], sc->ends[sc_idx], clusterdata_ptr->ref, ctg
            );
            std::string truth2 = "", ref_t2 = ""; 
            std::vector< std::vector<int> > truth2_ref_ptrs, ref_truth2_ptrs;
            generate_ptrs_strs(
                    truth2, ref_t2, truth2_ref_ptrs, ref_truth2_ptrs, 
                    sc->ctg_variants[TRUTH][HAP2],
                    sc->superclusters[TRUTH][HAP2][sc_idx],
                    sc->superclusters[TRUTH][HAP2][sc_idx+1],
                    sc->begs[sc_idx], sc->ends[sc_idx], clusterdata_ptr->ref, ctg
            );

            int phase = sc->sc_phase[sc_idx];

            /////////////////////////////////////////////////////////////////////
            // SMITH-WATERMAN DISTANCE: don't allow skipping called variants     
            /////////////////////////////////////////////////////////////////////
            
            // keep or swap truth haps based on previously decided phasing
            std::vector<std::string> truth(2);
            if (phase < 0) {
                ERROR("Phase never set for supercluster %d on contig '%s'",
                        sc_idx, ctg.data());
            } else if (phase == PHASE_SWAP) {
                truth[HAP1] = truth2; truth[HAP2] = truth1;
            } else {
                truth[HAP1] = truth1; truth[HAP2] = truth2;
            }

            // phasing is known, add scores for each hap
            std::vector<int> qual_dists(g.max_qual+2, 0);
            for (int hap = 0; hap < HAPS; hap++) {
				if (false) printf("truth/query haplotype %d\n", hap);

                // supercluster start/end indices
                int beg_idx = sc->ctg_variants[QUERY][hap]->clusters.size() ?
                    sc->ctg_variants[QUERY][hap]->clusters[
                        sc->superclusters[QUERY][hap][sc_idx]] : 0;
                int end_idx = sc->ctg_variants[QUERY][hap]->clusters.size() ?
                    sc->ctg_variants[QUERY][hap]->clusters[
                        sc->superclusters[QUERY][hap][sc_idx+1]] : 0;

                // calculate quality thresholds 
                // (where string would change when including/excluding variants)
                std::set<int> quals = {};
                for (int var_idx = beg_idx; var_idx < end_idx; var_idx++) {
                    quals.insert(sc->ctg_variants[QUERY][hap]->var_quals[var_idx]+1);
                }
                quals.insert(g.max_qual+2);

                // sweep through quality thresholds
                int prev_qual = 0;
                for (int qual : quals) {
                    if (false) printf("qual: %d-%d\n", prev_qual, qual-1);

                    // generate query string (only applying variants with Q>=qual)
                    std::string query, ref;
                    std::vector< std::vector<int> > query_ref_ptrs, ref_query_ptrs;
                    generate_ptrs_strs(
                            query, ref, query_ref_ptrs, ref_query_ptrs,
                            sc->ctg_variants[QUERY][hap], 
							sc->superclusters[QUERY][hap][sc_idx],
							sc->superclusters[QUERY][hap][sc_idx+1],
                            sc->begs[sc_idx], sc->ends[sc_idx], 
                            clusterdata_ptr->ref, ctg, prev_qual);
					if (false) printf("query: %s\n", query.data());
					if (false) printf("truth: %s\n", truth[hap].data());
					if (false) printf("ref: %s\n", ref.data());

                    // align strings, backtrack, calculate distance
                    std::vector< std::vector< std::vector<uint8_t> > > ptrs(MATS);
                    std::vector< std::vector< std::vector<int> > > offs(MATS);
                    int s = 0;
                    std::reverse(query.begin(), query.end());
                    std::reverse(truth[hap].begin(), truth[hap].end());
                    wf_swg_align(query, truth[hap], ptrs, offs,
                            s, g.eval_sub, g.eval_open, g.eval_extend, false);
                    std::vector<int> cigar = wf_swg_backtrack(query, truth[hap], 
                            ptrs, offs, s, g.eval_sub, g.eval_open, g.eval_extend, false);
                    std::reverse(query.begin(), query.end());
                    std::reverse(truth[hap].begin(), truth[hap].end());
                    std::reverse(cigar.begin(), cigar.end());
                    int dist = count_dist(cigar);
					if (false) printf("distance: %d\n", dist);

                    // add distance for range of corresponding quals
                    for (int q = prev_qual; q < qual; q++) {
                        all_qual_dists[q] += dist;
                        ctg_qual_dists[q] += dist;
                        qual_dists[q] += dist;
                    }
                    edits.add_edits(ctg, sc->begs[sc_idx], hap, cigar, sc_idx, prev_qual, qual);
                    prev_qual = qual;
                }
            }

        } // each cluster

        if (g.verbosity >= 1) {
            INFO("    [%2d] %s: %d", ctg_id, ctg.data(), 
                *std::min_element(ctg_qual_dists.begin(), ctg_qual_dists.end()));
        }
        ctg_id++;

    } // each contig
    INFO(" ");
    if (g.verbosity >= 1) INFO("  Total edit distance: %d", 
                *std::min_element(all_qual_dists.begin(), all_qual_dists.end()));
    return edits;
}


/******************************************************************************/


/* Calculate the combined Smith-Waterman score for all variants within 
 * one or several adjacent clusters.
 */
int calc_vcf_swg_score(std::shared_ptr<ctgVariants> vars, 
        int clust_beg_idx, int clust_end_idx, int sub, int open, int extend) {
    int score = 0;
    for (int var_idx = vars->clusters[clust_beg_idx]; 
            var_idx < vars->clusters[clust_end_idx]; var_idx++) {
        switch (vars->types[var_idx]) {
            case TYPE_SUB:
                score += sub;
                break;
            case TYPE_INS:
                score += open;
                score += extend * vars->alts[var_idx].size();
                break;
            case TYPE_DEL:
                score += open;
                score += extend * vars->refs[var_idx].size();
                break;
            default:
                ERROR("Unexpected variant type in calc_vcf_swg_score()");
        }
    }
    return score;
}


int calc_cig_swg_score(const std::vector<int> & cigar, 
        int sub, int open, int extend) {
    int score = 0;
    int prev = PTR_MAT;
    for (int i = 0; i < int(cigar.size()); i++) {
        switch (cigar[i]) {
            case PTR_MAT:
                i++; // increment by 2
                prev = PTR_MAT;
                break;
            case PTR_SUB:
                score += sub;
                prev = PTR_SUB;
                i++; // increment by 2
                break;
            case PTR_INS:
                score += (prev == PTR_INS ? extend : open + extend);
                prev = PTR_INS;
                break;
            case PTR_DEL:
                score += (prev == PTR_DEL ? extend : open + extend);
                prev = PTR_DEL;
                break;
            default:
                ERROR("Unexpected variant type in calc_cig_swg_score()");
        }
    }
    return score;
}


/******************************************************************************/

/* Perform WaveFront Smith-Waterman-Gotoh alignment of two strings, returning 
 * the farthest-reaching reference index of lesser or equal score to that provided.
 * Strings are generated by applying variants to draft ref, and skipping 
 * variants is not allowed. Neither is a diagonal reference transition past 
 * the existing supercluster.
 */
int wf_swg_max_reach(
        const std::string & query, const std::string & truth, 
        std::vector<int> & offs,
        int main_diag, int main_diag_start, int max_score, 
        int x, int o, int e, bool print /* false */, bool reverse /* false */
        ) {

    // init
    int query_len = query.size();
    int truth_len = truth.size();
    int mat_len = query_len + truth_len - 1;
    int main_diag_off = main_diag_start - main_diag;
    int s = 0;
    int s2 = 0;
    int scores = std::max(x, o+e)+1;
    int y = mat_len;
    int z = y * scores;
    offs[MAT_SUB*z + s2*y + query_len-1] = -1;

    while (true) {

        // EXTEND WAVEFRONT (leave INS, DEL forwards)
        if (!reverse) for (int m = MAT_INS; m < MATS; m++) {
            for (int d = 0; d < mat_len; d++) {
                int off = offs[m*z + s2*y + d];
                int diag = d + 1 - query_len;

                if (off >= 0 && off < query_len &&
                        diag+off >= 0 && diag+off < truth_len &&
                        off >= offs[MAT_SUB*z + s2*y + d]) {
                    offs[MAT_SUB*z + s2*y + d] = off;
                    if(print) printf("(S, %d, %d) swap fwd\n", off, off+diag);

                }
            }
        }

        // EXTEND WAVEFRONT (diag, SUB only)
        for (int d = 0; d < mat_len; d++) {
            int off = offs[MAT_SUB*z + s2*y + d];
            int diag = d + 1 - query_len;

            // extend
            while ( (diag != main_diag || off+1 < main_diag_off) &&
                        off != -2 && diag + off >= -1 && 
                        off < query_len - 1 && 
                        diag + off < truth_len - 1) {
                if (query[off+1] == truth[diag+off+1]) off++;
                else break;
            }
            if (off > offs[MAT_SUB*z + s2*y + d])
                if(print) printf("(S, %d, %d) extend\n", off, off+diag);
            offs[MAT_SUB*z + s2*y + d] = off;

            // finish if we've reached the last column
            if (off + diag == truth_len - 1) {
                return truth_len-1;
            }
            if (off == query_len - 1 && off+diag >= 0 && off+diag < truth_len-1)  {
                return off + diag;
            }

        }
        if (s == max_score) break;

        /* if (print) for (int mi = 0; mi < MATS; mi++) { */
        /*     printf("\n%s matrix\n", type_strs[mi+1].data()); */
        /*     printf("offs %d:", s); */
        /*     for (int di = 0; di < int(query.size() + truth.size()-1); di++) { */
        /*         printf("\t%d", offs[mi*z + s2*y + di]); */
        /*     } */
        /*     printf("\n"); */
        /* } */

        // NEXT WAVEFRONT
        s++; s2++;
        if (s2 == scores) s2 = 0;
        // init new row to all invalid
        for (int m = MAT_INS; m < MATS; m++) {
            for (int d = 0; d < mat_len; d++) {
                offs[m*z + s2*y + d] = -2;
            }
        }
        if (print) printf("\nscore = %d\n", s);

        for (int d = 0; d < mat_len; d++) {
            int diag = d + 1 - query_len;

            // sub (in SUB)
            int p = s - x;
            int p2 = s2 - x;
            if (p2 < 0) p2 += scores;
            if (p >= 0 && offs[MAT_SUB*z + p2*y + d] != -2 && 
                            offs[MAT_SUB*z + p2*y + d]+1 < query_len &&
                     diag + offs[MAT_SUB*z + p2*y + d]+1 < truth_len &&
                            offs[MAT_SUB*z + p2*y + d]+1 >= offs[MAT_SUB*z + s2*y + d]) {
                offs[MAT_SUB*z + s2*y + d] = offs[MAT_SUB*z + p2*y + d] + 1;
                if(print) printf("(S, %d, %d) sub\n", offs[MAT_SUB*z + s2*y + d], 
                        offs[MAT_SUB*z + s2*y + d]+diag);
            }

            // open gap (enter DEL, open fwd only)
            p = reverse ? s - e : s - (o+e);
            p2 = reverse ? s2 - e : s2 - (o+e);
            if (p2 < 0) p2 += scores;
            if (p >= 0 && d > 0 && 
                           offs[MAT_SUB*z + p2*y + d-1] != -2 &&
                    diag + offs[MAT_SUB*z + p2*y + d-1] < truth_len &&
                           offs[MAT_SUB*z + p2*y + d-1] >= offs[MAT_DEL*z + s2*y + d]) {
                offs[MAT_DEL*z + s2*y + d] = offs[MAT_SUB*z + p2*y + d-1];
                if(print) printf("(D, %d, %d) open\n", offs[MAT_DEL*z + s2*y + d], 
                        offs[MAT_DEL*z + s2*y + d]+diag);
            }
            // open gap (enter INS, open fwd only)
            if (p >= 0 && d < mat_len-1 && 
                           offs[MAT_SUB*z + p2*y + d+1] != -2 &&
                           offs[MAT_SUB*z + p2*y + d+1]+1 < query_len &&
                    diag + offs[MAT_SUB*z + p2*y + d+1]+1 < truth_len &&
                    diag + offs[MAT_SUB*z + p2*y + d+1]+1 >= 0 &&
                           offs[MAT_SUB*z + p2*y + d+1]+1 >= offs[MAT_INS*z + s2*y + d]) {
                offs[MAT_INS*z + s2*y + d] = offs[MAT_SUB*z + p2*y + d+1]+1;
                if(print) printf("(I, %d, %d) open\n", offs[MAT_INS*z + s2*y + d], 
                        offs[MAT_INS*z + s2*y + d]+diag);
            }

            // leave INDEL (open rev only)
            p = s - o;
            p2 = s2 - o;
            if (p2 < 0) p2 += scores;
            if (reverse && p >= 0) {
                for (int m = MAT_INS; m < MATS; m++) {
                    if (        offs[m*z + p2*y + d] >= 0 && 
                                offs[m*z + p2*y + d] < query_len &&
                         diag + offs[m*z + p2*y + d] >= 0 && 
                         diag + offs[m*z + p2*y + d] < truth_len &&
                                offs[m*z + p2*y + d] > offs[MAT_SUB*z + s2*y + d]) {
                        offs[MAT_SUB*z + s2*y + d] = offs[m*z + p2*y + d];
                        if(print) printf("(S, %d, %d) swap rev\n", 
                                offs[m*z + p2*y + d], diag+offs[m*z + p2*y + d]);
                    }
                }
            }

            // extend gap (stay DEL)
            p = s - e;
            p2 = s2 - e;
            if (p2 < 0) p2 += scores;
            if (p >= 0 && d > 0 && 
                           offs[MAT_DEL*z + p2*y + d-1] != -2 &&
                    diag + offs[MAT_DEL*z + p2*y + d-1] < truth_len &&
                           offs[MAT_DEL*z + p2*y + d-1] >= offs[MAT_DEL*z + s2*y + d]) {
                offs[MAT_DEL*z + s2*y + d] = offs[MAT_DEL*z + p2*y + d-1];
                if(print) printf("(D, %d, %d) extend\n", offs[MAT_DEL*z + s2*y + d], 
                        offs[MAT_DEL*z + s2*y + d]+diag);
            }
            // extend gap (stay INS)
            if (p >= 0 && d < mat_len-1 && 
                           offs[MAT_INS*z + p2*y + d+1] != -2 &&
                           offs[MAT_INS*z + p2*y + d+1]+1 < query_len &&
                    diag + offs[MAT_INS*z + p2*y + d+1]+1 < truth_len &&
                    diag + offs[MAT_INS*z + p2*y + d+1]+1 >= 0 &&
                           offs[MAT_INS*z + p2*y + d+1]+1 >= offs[MAT_INS*z + s2*y + d]) {
                offs[MAT_INS*z + s2*y + d] = offs[MAT_INS*z + p2*y + d+1]+1;
                if(print) printf("(I, %d, %d) extend\n", offs[MAT_INS*z + s2*y + d], 
                        offs[MAT_INS*z + s2*y + d]+diag);
            }
        }
    } // end reach

    // get max reach
    int max_reach = 0;
    for (int s2 = 0; s2 < scores; s2++) {
        for (int m = 0; m < MATS; m++) {
            for (int d = 0; d < mat_len; d++) {
                int off = offs[m*z + s2*y + d];
                int diag = d + 1 - query_len;
                if (off >= 0 && off < query_len &&
                        diag+off >= 0 && diag+off < truth_len)
                    max_reach = std::max(max_reach, diag + off);
            }
        }
    }
    return max_reach;
}


/* Perform Djikstra Smith-Waterman-Gotoh alignment of two strings, returning 
 * the farthest-reaching reference index of lesser or equal score to that provided.
 * Strings are generated by applying variants to draft ref, and skipping 
 * variants is not allowed. Neither is a diagonal reference transition.
 */
int swg_max_reach(const std::string & query, const std::string & ref, 
        const std::vector< std::vector<int> > & query_ref_ptrs,
        const std::vector< std::vector<int> > & ref_query_ptrs,
        int sub, int open, int extend, int score, bool print,
        bool reverse /*= false*/, int ref_section /*= -1*/) {
    
    int ref_len = ref.size();
    int query_len = query.size();
    int sub_wave = 0, open_wave = 0, extend_wave = 0, end_wave = 0;
        
    // set first wavefront
    std::queue<idx2> queue;
    queue.push({MAT_SUB, 0, 0});
    std::unordered_set<idx2> done;
    done.insert({MAT_SUB, 0, 0});
    std::vector< std::unordered_set<idx2> > waves(score+1);

    // find furthest-reaching alignment with lesser or equal score
    for (int s = 0; s <= score; s++) {
        if (print) printf("s = %d\n", s);

        // EXTEND WAVEFRONT (stay at same score)
        while (!queue.empty()) {
            idx2 x = queue.front(); queue.pop();
            if (print) printf("  x = (%c, %d, %d)\n", 
                    std::string("SID")[x.mi], x.qi, x.ri);
            waves[s].insert(x);

            // allow non-main-diagonal match
            bool main_diag = x.ri+1 >= ref_section &&
                    x.qi+1 < query_len && x.ri+1 < ref_len && // repeat bounds check
                    !(query_ref_ptrs[FLAGS][x.qi+1] & PTR_VARIANT) &&
                    !(ref_query_ptrs[FLAGS][x.ri+1] & PTR_VARIANT) &&
                    query_ref_ptrs[PTRS][x.qi+1] == x.ri+1 &&
                    ref_query_ptrs[PTRS][x.ri+1] == x.qi+1;
            if (x.qi+1 < query_len && x.ri+1 < ref_len && 
                    !main_diag && x.mi == MAT_SUB &&
                    query[x.qi+1] == ref[x.ri+1] && 
                    !contains(done, {x.mi, x.qi+1, x.ri+1})) {
                idx2 next(x.mi, x.qi+1, x.ri+1);
                queue.push(next);
                done.insert(next);
            }

            // allow exiting D/I state (no penalty for forward only)
            if (!reverse && (x.mi == MAT_INS || x.mi == MAT_DEL) && 
                    !contains(done, {MAT_SUB, x.qi, x.ri})) {
                queue.push({MAT_SUB, x.qi, x.ri});
                done.insert({MAT_SUB, x.qi, x.ri});
            }
        }

        // exit if we're done aligning
        if (s == score || contains(done, {MAT_SUB, query_len-1, ref_len-1})) 
            break;

        // NEXT WAVEFRONT (increase score by one)
        
        // SUB transition (only from MAT_SUB)
        sub_wave = s + 1 - sub;
        if (sub_wave >= 0 && sub_wave <= score) {
            for (idx2 x : waves[sub_wave]) {
                if (x.mi == MAT_SUB && x.qi+1 < query_len && x.ri+1 < ref_len) {
                    idx2 next({x.mi, x.qi+1, x.ri+1});
                    if (!contains(done, next)) {
                        queue.push(next);
                        done.insert(next);
                    }
                }
            }
        }

        // INS/DEL opening (only from MAT_SUB)
        if (reverse) // g.open penalty when leaving INDEL
            open_wave = s + 1 - extend;
        else // g.open penalty when entering INDEL
            open_wave = s + 1 - (open+extend);
        if (open_wave >= 0 && open_wave <= score) {
            for(idx2 x : waves[open_wave]) {

                // INS opening
                if (x.mi == MAT_SUB && x.qi+1 < query_len) {
                    idx2 next({MAT_INS, x.qi+1, x.ri});
                    if (!contains(done, next)) {
                        queue.push(next);
                        done.insert(next);
                    }
                }

                // DEL opening
                if (x.mi == MAT_SUB && x.ri+1 < ref_len) {
                    idx2 next({MAT_DEL, x.qi, x.ri+1});
                    if (!contains(done, next)) {
                        queue.push(next);
                        done.insert(next);
                    }
                }
            }
        }

        // reverse alignment, penalize leaving INDEL
        end_wave = s + 1 - open;
        if (reverse && end_wave >= 0 && end_wave <= score) {
            for (idx2 x : waves[end_wave]) {
                if ((x.mi == MAT_INS || x.mi == MAT_DEL) && 
                        !contains(done, {MAT_SUB, x.qi, x.ri})) {
                    queue.push({MAT_SUB, x.qi, x.ri});
                    done.insert({MAT_SUB, x.qi, x.ri});
                }
            }
        }

        // INS/DEL extension
        extend_wave = s + 1 - extend;
        if (extend_wave >= 0 && extend_wave <= score) {
            for(idx2 x : waves[extend_wave]) {

                // INS extending (only from MAT_INS)
                if (x.mi == MAT_INS && x.qi+1 < query_len) {
                    idx2 next({x.mi, x.qi+1, x.ri});
                    if (!contains(done, next)) {
                        queue.push(next);
                        done.insert(next);
                    }
                }

                // DEL extending (only from MAT_DEL)
                if (x.mi == MAT_DEL && x.ri+1 < ref_len) {
                    idx2 next({x.mi, x.qi, x.ri+1});
                    if (!contains(done, next)) {
                        queue.push(next);
                        done.insert(next);
                    }
                }
            }
        }
    } // end reach

    // farthest reach isn't necessarily at max score
    int min_wave = std::max(0, std::min(std::min(
                    open_wave, extend_wave), sub_wave));
    // get max reach
    int max_reach = 0;
    for (int w = min_wave; w <= score; w++) {
        for (idx2 x : waves[w]) {
            max_reach = std::max(max_reach, x.ri);
        }
    }
    return max_reach;
}


/******************************************************************************/


struct RealignWorkItem {
    int         hap;
    std::string ctg;
    int         cluster_idx;
    int         beg_idx, end_idx;
    int         beg, end;
    float       qual;
    int         phase_set;
};

struct RealignResult {
    std::vector<int> cigar;
};

std::shared_ptr<variantData> wf_swg_realign(
        std::shared_ptr<variantData> vcf,
        std::shared_ptr<fastaData> ref_fasta,
        int sub, int open, int extend, int callset, bool print /* = false */) {
    (void)print;
    if (g.verbosity >= 1) INFO(" ");
    if (g.verbosity >= 1) INFO("%s[%s 2/8] Realigning %s VCF%s '%s'", COLOR_PURPLE,
            callset == QUERY ? "Q" : "T", callset_strs[callset].data(),
            COLOR_WHITE, vcf->filename.data());

    // build work list (serial, preserves hap=0->1, map order, cluster 0..N-1)
    std::vector<RealignWorkItem> work;
    for (int hap = 0; hap < 2; hap++) {
        for (auto itr = vcf->variants[hap].begin();
                itr != vcf->variants[hap].end(); itr++) {
            std::string ctg = itr->first;
            std::shared_ptr<ctgVariants> vars = itr->second;
            if (vars->poss.size() == 0) continue;

            for (int cluster = 0; cluster < int(vars->clusters.size()-1); cluster++) {
                RealignWorkItem w;
                w.hap         = hap;
                w.ctg         = ctg;
                w.cluster_idx = cluster;
                w.beg_idx     = vars->clusters[cluster];
                w.end_idx     = vars->clusters[cluster+1];
                w.beg         = vars->poss[w.beg_idx]-1;
                w.end         = vars->poss[w.end_idx-1] + vars->rlens[w.end_idx-1]+1;

                // variant qual is minimum in cluster
                w.qual = g.max_qual;
                for (int i = w.beg_idx; i < w.end_idx; i++)
                    w.qual = std::min(w.qual, vars->var_quals[i]);

                // phase set is first non-zero phase set
                w.phase_set = 0;
                for (int i = w.beg_idx; i < w.end_idx; i++) {
                    if (vars->phase_sets[i] != w.phase_set) {
                        w.phase_set = vars->phase_sets[i];
                        break;
                    }
                }

                work.push_back(std::move(w));
            }
        }
    }

    // pre-size result vector so workers can write by index without synchronisation
    std::vector<RealignResult> res(work.size());

    // parallel work-stealing dispatch
    std::atomic<size_t> next{0};
    // Budget for concurrent ptrs/offs allocations: cap at 3 GB or 30% of g.max_ram,
    // whichever is smaller. Each worker estimates its claim as MATS * span^2 * 5 bytes
    // (conservative upper bound on score s ≈ span). Small clusters claim almost
    // nothing and run freely; large clusters throttle themselves via this spinwait.
    const size_t realign_ram_budget =
        std::min((size_t)(g.max_ram * 1e9 * 0.30), (size_t)(3ULL << 30));
    std::atomic<size_t> ram_claimed{0};
    int nthreads = std::max(1, g.max_threads);
    auto worker = [&]() {
        for (;;) {
            size_t idx = next.fetch_add(1, std::memory_order_relaxed);
            if (idx >= work.size()) return;
            const RealignWorkItem& w = work[idx];
            size_t span = (size_t)(w.end - w.beg);
            size_t est = (size_t)MATS * span * span * 5;

            // Spinwait until this cluster's estimated RAM fits in the budget.
            // Cap claim to the budget so oversized clusters (est > budget) still
            // terminate: claiming the full budget forces cur==0, serializing them
            // one at a time rather than spinning forever.
            size_t claim = std::min(est, realign_ram_budget);
            for (;;) {
                size_t cur = ram_claimed.load(std::memory_order_relaxed);
                if (cur + claim <= realign_ram_budget &&
                    ram_claimed.compare_exchange_weak(cur, cur + claim,
                        std::memory_order_acquire, std::memory_order_relaxed))
                    break;
                std::this_thread::yield();
            }

            std::shared_ptr<ctgVariants> vars = vcf->variants[w.hap].at(w.ctg);
            std::string query = generate_str(ref_fasta, vars, w.ctg,
                                             w.beg_idx, w.end_idx, w.beg, w.end);
            std::string ref_s = ref_fasta->fasta.at(w.ctg).substr(w.beg, w.end - w.beg);
            std::vector< std::vector< std::vector<uint8_t> > > ptrs(MATS);
            std::vector< std::vector< std::vector<int> > > offs(MATS);
            int s = 0;
            std::reverse(query.begin(), query.end());
            std::reverse(ref_s.begin(), ref_s.end());
            wf_swg_align(query, ref_s, ptrs, offs, s, sub, open, extend, false);
            std::vector<int> cigar = wf_swg_backtrack(query, ref_s, ptrs, offs,
                                                      s, sub, open, extend, false);
            std::reverse(query.begin(), query.end());
            std::reverse(ref_s.begin(), ref_s.end());
            std::reverse(cigar.begin(), cigar.end());
            ram_claimed.fetch_sub(claim, std::memory_order_release);
            res[idx] = {std::move(cigar)};
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(nthreads);
    for (int t = 0; t < nthreads; t++) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    // serial gather — visits items in original order, keeping add_variants serial
    // query/ref strings are regenerated from the work item rather than stored in
    // res, avoiding O(total_clusters) string memory during the parallel phase.
    std::shared_ptr<variantData> results(new variantData());
    results->set_header(vcf);
    for (size_t i = 0; i < res.size(); i++) {
        const RealignWorkItem& w = work[i];
        std::shared_ptr<ctgVariants> vars = vcf->variants[w.hap].at(w.ctg);
        std::string query = generate_str(ref_fasta, vars, w.ctg,
                                         w.beg_idx, w.end_idx, w.beg, w.end);
        std::string ref_s = ref_fasta->fasta.at(w.ctg).substr(w.beg, w.end - w.beg);
        results->add_variants(res[i].cigar, w.hap, w.beg, w.ctg,
                              query, ref_s, w.qual, w.phase_set);
    }

    return results;
}


/******************************************************************************/


int count_dist(const std::vector<int> & cigar) {
    int cigar_ptr = 0;
    int dist = 0;
    while (cigar_ptr < int(cigar.size())) {
        switch (cigar[cigar_ptr]) {
        case PTR_MAT:
            cigar_ptr += 2;
            break;
        case PTR_SUB:
            dist++;
            cigar_ptr += 2;
            break;
        case PTR_INS:
        case PTR_DEL:
            dist++;
            cigar_ptr++;
            break;
        default:
            ERROR("Unexpected CIGAR in count_dist()");
        }
    }
    return dist;
}


/******************************************************************************/


/* After alignment, backtrack using pointers and save cigar. */
std::vector<int> wf_swg_backtrack(
        const std::string & query,
        const std::string & ref,
        const std::vector< std::vector< std::vector<uint8_t> > > & ptrs, 
        const std::vector< std::vector< std::vector<int> > > & offs, 
        int s, int x, int o, int e,
        bool print) {

    /* if (print) print_wfa_ptrs(query, ref, s, ptrs, offs); */
    /* for (int mi = 0; mi < MATS; mi++) { */
    /*     if(print) printf("\n%s matrix\n", type_strs[mi+1].data()); */
    /*     for (int si = 0; si <= s; si++) { */
    /*         if(print) printf("%d:\n", si); */
    /*         for (int di = 0; di < int(query.size() + ref.size()-1); di++) { */
    /*             if(print) printf("\t%d", offs[mi][si][di]); */
    /*         } */
    /*         if(print) printf("\n"); */
    /*     } */
    /* } */

    // initialize backtrack position and cigar string
    std::vector<int> cigar(query.size() + ref.size(), 0);
    int cigar_ptr = cigar.size()-1;
    idx2 pos(MAT_SUB, query.size()-1, ref.size()-1);

    if (print) printf("\nWF SWG Backtrack:\n");
    while (pos.qi >= 0 || pos.ri >= 0) {

        // debug print
        if (s < 0) ERROR("Negative wf_swg_backtrack() score at (%c, %d, %d)",
                std::string("SID")[pos.mi], pos.qi, pos.ri);

        // init
        int diag = pos.ri - pos.qi;
        int d = query.size()-1 + diag;
        
        if (pos.mi == MAT_SUB) {
            if (ptrs[MAT_SUB][s][d] & PTR_INS) { // end INS freely
                int prev_off = offs[MAT_INS][s][d];
                while (pos.qi > prev_off) { // match
                    if (print) printf("(%c, %d, %d) match ins\n",
                            std::string("SID")[pos.mi], pos.qi, pos.ri);
                    pos.qi--; cigar[cigar_ptr--] = PTR_MAT;
                    pos.ri--; cigar[cigar_ptr--] = PTR_MAT;
                    if (pos.qi < 0 || pos.ri < 0) ERROR("During wf_swg_backtrack() at (%c, %d, %d)",
                            std::string("SID")[pos.mi], pos.qi, pos.ri);
                }
                pos.mi = MAT_INS;
            } else if (ptrs[MAT_SUB][s][d] & PTR_DEL) { // end DEL freely
                int prev_off = offs[MAT_DEL][s][d];
                while (pos.qi > prev_off) { // match
                    if (print) printf("(%c, %d, %d) match del\n",
                            std::string("SID")[pos.mi], pos.qi, pos.ri);
                    pos.qi--; cigar[cigar_ptr--] = PTR_MAT;
                    pos.ri--; cigar[cigar_ptr--] = PTR_MAT;
                    if (pos.qi < 0 || pos.ri < 0) ERROR("During wf_swg_backtrack() at (%c, %d, %d)",
                            std::string("SID")[pos.mi], pos.qi, pos.ri);
                }
                pos.mi = MAT_DEL;
            } else if (ptrs[MAT_SUB][s][d] & PTR_SUB) { // sub
                if (s-x < 0) ERROR("Unexpected PTR_SUB");
                int prev_off = offs[MAT_SUB][s-x][d];
                while (pos.qi > prev_off+1) { // match
                    if (print) printf("(%c, %d, %d) match sub\n",
                            std::string("SID")[pos.mi], pos.qi, pos.ri);
                    pos.qi--; cigar[cigar_ptr--] = PTR_MAT;
                    pos.ri--; cigar[cigar_ptr--] = PTR_MAT;
                    if (pos.qi < 0 || pos.ri < 0) ERROR("During wf_swg_backtrack() at (%c, %d, %d)",
                            std::string("SID")[pos.mi], pos.qi, pos.ri);
                }
                if (print) printf("(%c, %d, %d) sub\n",
                        std::string("SID")[pos.mi], pos.qi, pos.ri);
                pos.qi--; cigar[cigar_ptr--] = PTR_SUB;
                pos.ri--; cigar[cigar_ptr--] = PTR_SUB;
                s -= x;
            } else if (ptrs[MAT_SUB][s][d] & PTR_MAT) { // only matches remain
                while (pos.qi >= 0 && pos.ri >= 0) { // match
                    if (print) printf("(%c, %d, %d) match mat\n",
                            std::string("SID")[pos.mi], pos.qi, pos.ri);
                    pos.qi--; cigar[cigar_ptr--] = PTR_MAT;
                    pos.ri--; cigar[cigar_ptr--] = PTR_MAT;
                }
                if (pos.qi >= 0 || pos.ri >= 0)
                    ERROR("PTR_MAT expected all matches, now at (%c, %d, %d)",
                        std::string("SID")[pos.mi], pos.qi, pos.ri);
            } else {
                ERROR("Unexpected pointer '%d' in wf_swg_backtrack() at (%c, %d, %d)",
                    ptrs[MAT_SUB][s][d], std::string("SID")[pos.mi], pos.qi, pos.ri);
            }

        } else if (pos.mi == MAT_INS) {
            if (ptrs[MAT_INS][s][d] & PTR_INS) { // extend INS
                if (print) printf("(%c, %d, %d) ext ins\n",
                        std::string("SID")[pos.mi], pos.qi, pos.ri);
                pos.qi--; cigar[cigar_ptr--] = PTR_INS;
                s -= e;
            } else if (ptrs[MAT_INS][s][d] & PTR_SUB) { // start INS
                if (print) printf("(%c, %d, %d) start ins\n",
                        std::string("SID")[pos.mi], pos.qi, pos.ri);
                pos.qi--; cigar[cigar_ptr--] = PTR_INS;
                pos.mi = MAT_SUB;
                s -= o + e;
            } else {
                ERROR("Unexpected pointer '%d' in wf_swg_backtrack() at (%c, %d, %d)",
                    ptrs[MAT_INS][s][d], std::string("SID")[pos.mi], pos.qi, pos.ri);
            }

        } else if (pos.mi == MAT_DEL) {
            if (ptrs[MAT_DEL][s][d] & PTR_DEL) { // extend DEL
                if (print) printf("(%c, %d, %d) ext del\n",
                        std::string("SID")[pos.mi], pos.qi, pos.ri);
                pos.ri--; cigar[cigar_ptr--] = PTR_DEL;
                s -= e;
            } else if (ptrs[MAT_DEL][s][d] & PTR_SUB) { // start DEL
                if (print) printf("(%c, %d, %d) start del\n",
                        std::string("SID")[pos.mi], pos.qi, pos.ri);
                pos.ri--; cigar[cigar_ptr--] = PTR_DEL;
                pos.mi = MAT_SUB;
                s -= o + e;
            } else {
                ERROR("Unexpected pointer '%d' in wf_swg_backtrack() at (%c, %d, %d)",
                    ptrs[MAT_DEL][s][d], std::string("SID")[pos.mi], pos.qi, pos.ri);
            }

        } else {
            ERROR("Unexpected MAT type in wf_swg_backtrack()");
        }
        if (!(pos.qi == -1 && pos.ri == -1) && (pos.qi < 0 || pos.ri < 0)) 
            ERROR("During wf_swg_backtrack() at (%c, %d, %d)",
                std::string("SID")[pos.mi], pos.qi, pos.ri);
    }
    return cigar;
}
