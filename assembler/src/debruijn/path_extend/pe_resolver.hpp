//***************************************************************************
//* Copyright (c) 2011-2013 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************

/*
 * pe_resolver.hpp
 *
 *  Created on: Mar 12, 2012
 *      Author: andrey
 */

#ifndef PE_RESOLVER_HPP_
#define PE_RESOLVER_HPP_

#include "path_extender.hpp"
#include "pe_io.hpp"

namespace path_extend {


class SimpleOverlapRemover {

public:
    SimpleOverlapRemover(const Graph& g, GraphCoverageMap& cm)
            : g_(g), coverage_map_(cm) {
    }

    void RemoveOverlaps(PathContainer& paths, size_t max_overlaped) const {
        for (size_t i = 0; i < paths.size(); i++) {
            FindAndRemovePathOverlap(paths, paths.Get(i), max_overlaped);
            FindAndRemovePathOverlap(paths, paths.GetConjugate(i), max_overlaped);
        }
    }

    void RemoveSimilarPaths(size_t max_overlap,
                            bool del_only_equal, bool del_subpaths,
                            bool del_begins, bool del_all) const {
        std::vector<EdgeId> edges = GetSortedEdges();
        for (size_t edgeId = 0; edgeId < edges.size(); ++edgeId) {
            EdgeId edge = edges.at(edgeId);
            std::set<BidirectionalPath *> cov_paths = coverage_map_
                    .GetCoveringPaths(edge);

            std::vector<BidirectionalPath*> cov_vect(cov_paths.begin(),
                                                     cov_paths.end());
            std::sort(cov_vect.begin(), cov_vect.end(), PathIdCompare);
            DEBUG("Analyze edge " << g_.int_id(edge) << " covered paths size " << cov_vect.size());
            for (size_t vect_i = 0; vect_i < cov_vect.size(); ++vect_i) {
                BidirectionalPath* path1 = cov_vect.at(vect_i);
                if (cov_paths.find(path1) == cov_paths.end()) {
                    continue;
                }
                for (size_t vect_i1 = vect_i + 1; vect_i1 < cov_vect.size();
                        ++vect_i1) {
                    BidirectionalPath* path2 = cov_vect.at(vect_i1);
                    if (cov_paths.find(path2) == cov_paths.end()) {
                        continue;
                    }
                    if ((*path1) == (*path2)) {
                        if (path2->IsOverlap()) {
                            path1->SetOverlap(true);
                        }
                        path2->Clear();
                        cov_paths = coverage_map_.GetCoveringPaths(edge);
                        continue;
                    }
                    if (g_.length(edge) <= max_overlap || path1->IsOverlap() || path2->IsOverlap() || del_only_equal) {
                        continue;
                    }
                    CompareAndCut(edge, path1, path2, (int) max_overlap,
                                  del_subpaths, del_begins, del_all);
                    cov_paths = coverage_map_.GetCoveringPaths(edge);
                }
            }
        }
        DEBUG("END ALL CUT")
    }

private:
    void CompareAndCut(EdgeId edge, BidirectionalPath* path1,
                       BidirectionalPath* path2, size_t max_overlap,
                       bool del_subpaths, bool del_begins, bool del_all) const {
        vector<size_t> poses1 = path1->FindAll(edge);
        for (size_t i1 = 0; i1 < poses1.size(); ++i1) {
            vector<size_t> poses2 = path2->FindAll(edge);
            for (size_t i2 = 0; i2 < poses2.size(); ++i2) {
                CompareAndCutFromPos(edge, path1, poses1[i1], path2,
                                     poses2[i2], max_overlap,
                                     del_subpaths, del_begins, del_all);
            }
        }
    }
    void CompareAndCutFromPos(EdgeId edge, BidirectionalPath* path1, int pos1,
                       BidirectionalPath* path2, int pos2, int max_overlap,
                       bool delete_subpaths, bool delete_begins,
                       bool delete_all) const {
        int last2 = pos2;
        int last1 = pos1;
        if (last1 >= (int) path1->Size() || last2 >= (int) path2->Size()) {
            return;
        }
        vector<int> other_path_end;
        pair<int, int> posRes = ComparePaths(last1, last2, *path1, *path2,
                                             max_overlap);
        last1 = posRes.first;
        last2 = posRes.second;
        BidirectionalPath* conj1 = path1->GetConjPath();
        BidirectionalPath* conj2 = path2->GetConjPath();
        size_t first1 = conj1->Size() - pos1 - 1;
        size_t first2 = conj2->Size() - pos2 - 1;
        posRes = ComparePaths(first1, first2, *conj1, *conj2, max_overlap);
        first2 = conj2->Size() - posRes.second - 1;
        first1 = conj1->Size() - posRes.first - 1;
        DEBUG("try to delete smth ");
        path1->Print();
        DEBUG("second path");
        path2->Print();
        DEBUG("path1 begin " << first1 << " path1 end " << last1 <<
              " path2_begin " << first2 << " path2_end " << last2 <<
              " path1_is_overlap " << path1->IsOverlap() <<
              " path2_is_overlap " << path2->IsOverlap() <<
              " path1 _has_overlaped_begin " << path1->HasOverlapedBegin() <<
              " path2_has_overlaped_begin " << path2->HasOverlapedBegin() <<
              " path1 _has_overlaped_end " << path1->HasOverlapedEnd() <<
              " path2_has_overlaped_end " << path2->HasOverlapedEnd() <<
              " delete_subpaths " << delete_subpaths <<
              " delete_begins " << delete_begins <<
              " delete_all " << delete_all);
        if (!CutOverlaps(path1, first1, last1, path1->Size(), path2,
                         first2, last2, path2->Size(), delete_subpaths,
                         delete_begins, delete_all)) {
            size_t common_length = path1->LengthAt(first1)
                    - path1->LengthAt(last1) + g_.length(path1->At(last1));
            if (common_length > cfg::get().max_repeat_length) {
                DEBUG("Similar paths were not deleted " << common_length);
            }
        }
    }

    pair<size_t, size_t> ComparePaths(size_t start_pos1, size_t start_pos2,
                                const BidirectionalPath& path1,
                                const BidirectionalPath& path2, size_t max_overlap) const{
        size_t cur_pos = start_pos1;
        size_t last_pos2 = start_pos2;
        size_t last_pos1 = cur_pos;
        cur_pos++;
        size_t diff_len = 0;
        size_t diff_len2 = 0;
        while (cur_pos < (int) path1.Size()) {
            if (diff_len > max_overlap) {
                return make_pair(last_pos1, last_pos2);
            }
            EdgeId currentEdge = path1[cur_pos];
            vector<size_t> poses2 = path2.FindAll(currentEdge);
            bool found = false;
            for (size_t pos2 = 0; pos2 < poses2.size(); ++pos2) {
                if (poses2[pos2] > last_pos2) {
                    diff_len2 = path2.LengthAt(last_pos2) - path2.LengthAt(poses2[pos2]) - g_.length(path2.At(last_pos2));
                    if (diff_len2 > max_overlap) {
                        break;
                    }
                    last_pos2 = poses2[pos2];
                    last_pos1 = cur_pos;
                    found = true;
                    break;
                }
            }
            if (!found) {
                diff_len += g_.length(currentEdge) + path1.GapAt(cur_pos);
            } else {
                diff_len = 0;
            }
            cur_pos++;
        }
        return make_pair(last_pos1, last_pos2);
    }


	bool CutOverlaps(BidirectionalPath* path1, size_t first1, size_t last1, size_t size1,
                     BidirectionalPath* path2, size_t first2, size_t last2, size_t size2,
                     bool del_subpaths, bool del_begins, bool del_all) const {
        if (first1 == 0 && last1 == size1 - 1 && del_subpaths
                && !path1->HasOverlapedBegin() && !path1->HasOverlapedEnd()) {
            DEBUG("delete path 1");
            path1->Clear();
        } else if (first2 == 0 && last2 == size2 - 1 && del_subpaths
                && !path2->HasOverlapedBegin() && !path2->HasOverlapedEnd()) {
            DEBUG("delete path 2");
            path2->Clear();
        } else if (first2 == 0 && first1 == 0 && del_begins) {
            if (path1->Length() < path2->Length() && !path1->HasOverlapedBegin()) {
                DEBUG("delete begin path 1");
                path1->GetConjPath()->PopBack(last1 + 1);
            } else if (!path2->HasOverlapedBegin()) {
                DEBUG("delete begin path 2");
                path2->GetConjPath()->PopBack(last2 + 1);
            }
        } else if ((last1 == size1 - 1 && last2 == size2 - 1) && del_begins) {
            if (path1->Length() < path2->Length() && !path1->HasOverlapedEnd()) {
                DEBUG("delete end path 1");
                path1->PopBack(last1 + 1 - first1);
            } else if (!path2->HasOverlapedEnd()) {
                DEBUG("delete end path 2");
                path2->PopBack(last2 + 1 - first2);
            }
        } else if (first2 == 0 && del_all && !path2->HasOverlapedBegin()) {
            DEBUG("delete path 2 begin");
            path2->GetConjPath()->PopBack(last2 + 1);
        } else if (last2 == size2 - 1 && del_all && !path2->HasOverlapedEnd()) {
            DEBUG("delete path 2 end");
            path2->PopBack(last1 + 1 - first1);
        } else if (first1 == 0 && del_all && !path1->HasOverlapedBegin()) {
            DEBUG("delete path1 begin");
            path1->GetConjPath()->PopBack(last1 + 1);
        } else if (last1 == size1 - 1 && del_all && !path1->HasOverlapedEnd()) {
            path1->PopBack(last1 + 1 - first1);
            DEBUG("delete path1 end")
        } else {
            DEBUG("nothing delete");
            return false;
        }
        return true;
    }

    std::vector<EdgeId> GetSortedEdges() const {
        std::set<EdgeId> edges_set;
        for (auto iter = g_.SmartEdgeBegin(); !iter.IsEnd(); ++iter) {
            edges_set.insert(*iter);
            edges_set.insert(g_.conjugate(*iter));
        }
        std::vector<EdgeId> edges(edges_set.begin(), edges_set.end());
        std::sort(edges.begin(), edges.end(), EdgeLengthAndIdComparator(g_));
        return edges;
    }

    bool HasAlreadyOverlapedEnd(BidirectionalPath * path) const {
        return !path->IsOverlap() and path->HasOverlapedEnd();
    }

    bool HasAlreadyOverlapedBegin(BidirectionalPath * path) const {
        return !path->IsOverlap() and path->HasOverlapedBegin();
    }

    bool IsSamePath(BidirectionalPath * path1,
                    BidirectionalPath * path2) const {
        return *path2 == *path1 or *path2 == *path1->GetConjPath();
    }

    void RemoveOverlap(PathContainer& paths, BidirectionalPath* path1,
                       BidirectionalPath* path2, size_t overlap_size) const {
        DEBUG("remove overlaps, change paths " << overlap_size);
        path1->Print();
        DEBUG("next");
        path2->Print();
        BidirectionalPath* conj2 = path2->GetConjPath();
        if (path1->IsOverlap() && overlap_size == path1->Size()) {
            conj2->PopBack(overlap_size);
            DEBUG("change second path");
            path2->SetOverlapedBeginTo(path1);
        } else if (path2->IsOverlap() && path2->Size() == overlap_size) {
            path1->PopBack(overlap_size);
            DEBUG("change first path");
            path1->SetOverlapedEndTo(path2);
        } else if (overlap_size < path2->Size()
                && overlap_size < path1->Size()) {
            BidirectionalPath* overlap = new BidirectionalPath(g_,
                                                               path1->Head());
            BidirectionalPath* conj_overlap = new BidirectionalPath(
                    g_, g_.conjugate(path1->Head()));
            paths.AddPair(overlap, conj_overlap);
            path1->PopBack();
            conj2->PopBack();
            for (size_t i = 1; i < overlap_size; ++i) {
                conj_overlap->PushBack(g_.conjugate(path1->Head()));
                path1->PopBack();
                conj2->PopBack();
            }
            coverage_map_.Subscribe(overlap);
            overlap->SetOverlap(true);
            coverage_map_.Subscribe(conj_overlap);
            path1->SetOverlapedEndTo(overlap);
            path2->SetOverlapedBeginTo(overlap);
            DEBUG("add new overlap");
            overlap->Print();
        }
    }

    void FindAndRemovePathOverlap(PathContainer& all_paths,
                                  BidirectionalPath* path1,
                                  size_t max_overlap) const {
        int last = (int) path1->Size() - 1;
        if (last <= 0 or coverage_map_.GetCoverage(path1->At(last)) <= 1
                or HasAlreadyOverlapedEnd(path1)) {
            return;
        }
        std::set<BidirectionalPath *> paths =
                coverage_map_.GetCoveringPaths(path1->At(last));
        BidirectionalPath* overlap_path = NULL;
        size_t overlap_size = 0;
        for (auto path_iter = paths.begin(); path_iter != paths.end();
                ++path_iter) {
            if (IsSamePath(*path_iter, path1) || HasAlreadyOverlapedBegin(*path_iter)) {
                continue;
            }
            size_t over_size = path1->OverlapEndSize(*path_iter);
            if (over_size > overlap_size) {
                overlap_size = over_size;
                overlap_path = *path_iter;
            } else if (over_size == overlap_size &&
                    (overlap_path == NULL || (*path_iter)->GetId() < overlap_path->GetId())) {
                overlap_path = *path_iter;
            }
        }
        if (overlap_path == NULL) {
            return;
        }
        if (overlap_size > 0) {
            RemoveOverlap(all_paths, path1, overlap_path, overlap_size);
        }
    }

    class EdgeLengthAndIdComparator {
    public:
        EdgeLengthAndIdComparator(const Graph& g)
                : g_(g) {
        }
        bool operator()(const EdgeId& e1, const EdgeId& e2) const {
            if (g_.length(e1) < g_.length(e2)) {
                return true;
            }
            if (g_.length(e2) < g_.length(e1)) {
                return false;
            }
            return e1.int_id() < e2.int_id();
        }
    private:
        const Graph& g_;
    };

	const Graph& g_;
	GraphCoverageMap& coverage_map_;

};

class PathExtendResolver {

protected:
    const Graph& g_;
    size_t k_;

public:
    PathExtendResolver(const Graph& g): g_(g), k_(g.k()) {
    }

    PathContainer makeSimpleSeeds() {
		std::set<EdgeId> included;
		PathContainer edges;
		for (auto iter = g_.SmartEdgeBegin(); !iter.IsEnd(); ++iter) {
			if (g_.int_id(*iter) <= 0 or InCycle(*iter, g_)) {
				continue;
			}
			if (included.count(*iter) == 0) {
				edges.AddPair(new BidirectionalPath(g_, *iter),
						new BidirectionalPath(g_, g_.conjugate(*iter)));
				included.insert(*iter);
				included.insert(g_.conjugate(*iter));
			}
		}
		return edges;
	}

    PathContainer extendSeeds(PathContainer& seeds, PathExtender& pathExtender) {
        PathContainer paths;
        pathExtender.GrowAll(seeds, &paths);
        return paths;
    }

    void removeOverlaps(PathContainer& paths, GraphCoverageMap& coverage_map,
                        size_t max_overlap, ContigWriter& writer,
                        string output_dir) {
        SimpleOverlapRemover remover(g_, coverage_map);
        writer.writePaths(paths, output_dir + "/before.fasta");
        DEBUG("Removing subpaths");
        remover.RemoveSimilarPaths(max_overlap, false, true, true, false);
        writer.writePaths(paths, output_dir + "/remove_similar.fasta");
        DEBUG("Remove overlaps")
        remover.RemoveOverlaps(paths, max_overlap);
        writer.writePaths(paths, output_dir + "/after_remove_overlaps.fasta");
        remover.RemoveSimilarPaths(max_overlap, true, false, false, false);
        writer.writePaths(paths, output_dir + "/remove_equal.fasta");
        DEBUG("remove similar path. Max difference " << max_overlap);
        remover.RemoveSimilarPaths(max_overlap, false, true, true, true);
        DEBUG("end removing");
        writer.writePaths(paths, output_dir + "/remove_all.fasta");
    }

    void RemoveMatePairEnds(PathContainer& paths, size_t min_edge_len) const {
        DEBUG("remove mp ends");
        for (size_t i = 0; i < paths.size(); ++i) {
            RemoveMatePairEnd(*paths.Get(i), min_edge_len);
            RemoveMatePairEnd(*paths.GetConjugate(i), min_edge_len);
        }
    }

    void addUncoveredEdges(PathContainer& paths, GraphCoverageMap& coverageMap) {
        std::set<EdgeId> included;
        for (auto iter = g_.SmartEdgeBegin(); !iter.IsEnd(); ++iter) {
            if (included.count(*iter) == 0 && !coverageMap.IsCovered(*iter)) {
                paths.AddPair(new BidirectionalPath(g_, *iter), new BidirectionalPath(g_, g_.conjugate(*iter)));
                included.insert(*iter);
                included.insert(g_.conjugate(*iter));
            }
        }
    }

private:
    void RemoveMatePairEnd(BidirectionalPath& path, size_t min_edge_len) const {
        int pos = int(path.Size()) - 1;
        while (pos > 0 and g_.length(path.At(pos)) < min_edge_len) {
            path.PopBack();
            pos--;
        }
    }

};

} /* PE_RESOLVER_HPP_ */

#endif
