//***************************************************************************
//* Copyright (c) 2011-2013 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************

/*
 * weight_counter.hpp
 *
 *  Created on: Feb 19, 2012
 *      Author: andrey
 */

#ifndef WEIGHT_COUNTER_HPP_
#define WEIGHT_COUNTER_HPP_

#include "bidirectional_path.hpp"
#include "paired_library.hpp"
#include <algorithm>
#include <boost/math/special_functions/fpclassify.hpp>

namespace path_extend {

struct EdgeWithPairedInfo {
	size_t e_;
	double pi_;

	EdgeWithPairedInfo(size_t e_, double pi) :
			e_(e_), pi_(pi) {

	}
};

struct EdgeWithDistance {
	EdgeId e_;
	int d_;

	EdgeWithDistance(EdgeId e, size_t d) :
			e_(e), d_((int) d) {
	}
};

class ExtentionAnalyzer {

protected:
	const Graph& g_;
	PairedInfoLibrary& lib_;

public:
	ExtentionAnalyzer(const Graph& g, PairedInfoLibrary& l) :
			g_(g), lib_(l) {
	}

	PairedInfoLibrary& getLib() {
		return lib_;
	}

	void FindCoveredEdges(const BidirectionalPath& path, EdgeId candidate,
                          std::vector<EdgeWithPairedInfo>& edges) {
        edges.clear();
        for (int i = (int) path.Size() - 1; i >= 0; --i) {
            double w = lib_.IdealPairedInfo(path[i], candidate,
                                            (int) path.LengthAt(i));
            if (math::gr(w, 0.)) {
                edges.push_back(EdgeWithPairedInfo(i, w));
            }
        }
    }

	void FindForwardEdges(const BidirectionalPath& /*path*/, EdgeId candidate,
			std::vector<EdgeWithDistance>& edges) {
		edges.clear();
		edges.push_back(EdgeWithDistance(candidate, 0));

		size_t i = 0;
		while (i < edges.size()) {
			size_t currentDistance = edges[i].d_ + g_.length(edges[i].e_);
			auto nextEdges = g_.OutgoingEdges(g_.EdgeEnd(edges[i].e_));

			if (edges[i].d_ < (int) lib_.GetISMax()) {
				for (auto edge = nextEdges.begin(); edge != nextEdges.end();
						++edge) {
					edges.push_back(EdgeWithDistance(*edge, currentDistance));
				}
			}
			++i;
		}
	}
};

class WeightCounter {

protected:
	const Graph& g_;
	PairedInfoLibraries& libs_;
	std::vector<ExtentionAnalyzer *> analyzers_;
	double avrageLibWeight_;

	double threshold_;
	bool normalizeWeight_;

	std::map<size_t, double> excluded_edges_;

public:

	WeightCounter(const Graph& g, PairedInfoLibraries& libs, double threshold = 0.0) :
			g_(g), libs_(libs), threshold_(threshold), normalizeWeight_(true), excluded_edges_() {
		avrageLibWeight_ = 0.0;
		analyzers_.reserve(libs_.size());
		for (auto iter = libs_.begin(); iter != libs_.end(); ++iter) {
			analyzers_.push_back(new ExtentionAnalyzer(g_, **iter));
			avrageLibWeight_ += (*iter)->GetCoverageCoeff();
		}
		avrageLibWeight_ /= (double) max(libs_.size(), (size_t) 1);
	}

	virtual ~WeightCounter() {
		for (auto iter = analyzers_.begin(); iter != analyzers_.end(); ++iter) {
			delete *iter;
		}
		analyzers_.clear();
	}

	virtual bool PairInfoExist(EdgeId first, EdgeId second, int distance) = 0;

	virtual double CountWeight(BidirectionalPath& path, EdgeId e,
			int gapLength = 0) = 0;

	virtual void GetDistances(EdgeId e1, EdgeId e2, std::vector<int>& dist,
			std::vector<double>& w) = 0;

	virtual double CountIdealInfo(EdgeId e1, EdgeId e2, size_t dist) = 0;

	virtual double CountIdealInfo(const BidirectionalPath& p, EdgeId e,
			size_t gap) = 0;

	virtual bool IsExtensionPossible(BidirectionalPath& path, EdgeId e) {
		return IsExtensionPossible(CountWeight(path, e)) ? true : false;
	}

	virtual bool IsExtensionPossible(double weight) const {
		return math::ge(weight, threshold_) ? true : false;
	}

	std::map<size_t, double>& GetExcludedEdges() {
		return excluded_edges_;
	}

	double getThreshold() const {
		return threshold_;
	}

	bool isNormalizeWeight() const {
		return normalizeWeight_;
	}

	void setNormalizeWeight(bool normalizeWeight) {
		this->normalizeWeight_ = normalizeWeight;
	}

	void setThreshold(double threshold) {
		this->threshold_ = threshold;
	}

	PairedInfoLibraries& getLibs() {
		return libs_;
	}

};

class ReadCountWeightCounter: public WeightCounter {

protected:

	double CountSingleLib(int libIndex, BidirectionalPath& path, EdgeId e,
			int additionalGapLength = 0.0) {

		double weight = 0.0;

		std::vector<EdgeWithPairedInfo> coveredEdges;
		analyzers_[libIndex]->FindCoveredEdges(path, e, coveredEdges);

		for (auto iter = coveredEdges.begin(); iter != coveredEdges.end();
				++iter) {
			if (excluded_edges_.find((int) iter->e_) != excluded_edges_.end()) {
				continue;
			}
			double w = libs_[libIndex]->CountPairedInfo(path[iter->e_], e,
					(int) path.LengthAt(iter->e_) + additionalGapLength);

			if (normalizeWeight_) {
				w /= iter->pi_;
			}
			weight += w;

		}

		return weight;
	}

public:

	ReadCountWeightCounter(const Graph& g_, PairedInfoLibraries& libs_,
			double threshold_ = 0.0) :
			WeightCounter(g_, libs_, threshold_) {
	}

	virtual void GetDistances(EdgeId e1, EdgeId e2, std::vector<int>& dist,
			std::vector<double>& w) {
		for (size_t i = 0; i < libs_.size(); ++i) {
			libs_[i]->CountDistances(e1, e2, dist, w);
		}
	}

	virtual double CountIdealInfo(EdgeId e1, EdgeId e2, size_t dist) {
		double res = 0.0;
		for (size_t i = 0; i < libs_.size(); ++i) {
			res += libs_[i]->IdealPairedInfo(e1, e2, (int) dist);
		}
		return res;
	}

	virtual double CountIdealInfo(const BidirectionalPath& p, EdgeId e,
			size_t gap) {
		double w = 0.0;
		for (int i = (int) p.Size() - 1; i >= 0; --i) {
			w += CountIdealInfo(p[i], e, gap + p.LengthAt(i));
		}
		return w;
	}

	virtual double CountWeight(BidirectionalPath& path, EdgeId e,
			int gapLength = 0) {
		double weight = 0.0;
		std::vector<EdgeWithDistance> edges;

		for (size_t i = 0; i < libs_.size(); ++i) {
			weight += CountSingleLib((int) i, path, e, gapLength);
		}

		return weight;
	}

	virtual bool PairInfoExist(EdgeId first, EdgeId second, int distance) {
		for (size_t libIndex = 0; libIndex < libs_.size(); ++libIndex) {
			double w = libs_[libIndex]->CountPairedInfo(first, second,
					distance);
			double w_ideal = libs_[libIndex]->IdealPairedInfo(first, second,
					distance);
			if (w_ideal == 0) {
				continue;
			}
			if (normalizeWeight_) {
				w /= w_ideal;
			}
			if (w > 0) {
				return true;
			}
		}
		return false;
	}

};

class PathCoverWeightCounter: public WeightCounter {

protected:

	double singleThreshold;

	double CountSingleLib(int libIndex, BidirectionalPath& path, EdgeId e,
			int additionalGapLength = 0.0) {
		PairedInfoLibrary& pairedInfoLibrary = *libs_[libIndex];
		double weight = 0.0;
		double idealWeight = 0.0;

		std::vector<EdgeWithPairedInfo> coveredEdges;
		analyzers_[libIndex]->FindCoveredEdges(path, e, coveredEdges);
		for (auto iter = coveredEdges.begin(); iter != coveredEdges.end();
				++iter) {
			double ideal_weight = iter->pi_;
			if (excluded_edges_.find(iter->e_) != excluded_edges_.end()) {
				if (!math::gr(excluded_edges_[iter->e_], 0.0) or !math::gr(ideal_weight, 0.0)) {
					continue;
				} else {
					ideal_weight = excluded_edges_[iter->e_];
				}
			}
			double threshold =
					pairedInfoLibrary.GetSingleThreshold() >= 0.0 ?
							pairedInfoLibrary.GetSingleThreshold() :
							singleThreshold;
			double singleWeight = libs_[libIndex]->CountPairedInfo(
					path[iter->e_], e,
					(int) path.LengthAt(iter->e_) + additionalGapLength);
			/*DEBUG("weight edge " << iter->e_ <<
			      " weight " << singleWeight
			      << " norm " <<singleWeight / ideal_weight
			      <<" threshold " << threshold
			      <<" used " << math::ge(singleWeight, threshold));*/

			if (normalizeWeight_) {
				singleWeight /= ideal_weight;
			}
			if (math::ge(singleWeight, threshold)) {
				weight += ideal_weight;
			}
			idealWeight += ideal_weight;
		}

		return math::gr(idealWeight, 0.0) ? weight / idealWeight : 0.0;
	}

public:

	PathCoverWeightCounter(const Graph& g_, PairedInfoLibraries& libs_,
			double threshold_ = 0.0, double singleThreshold_ = 0.0) :
			WeightCounter(g_, libs_, threshold_), singleThreshold(
					singleThreshold_) {

	}

	virtual void GetDistances(EdgeId e1, EdgeId e2, std::vector<int>& dist,
			std::vector<double>& w) {
		for (size_t i = 0; i < libs_.size(); ++i) {
			libs_[i]->CountDistances(e1, e2, dist, w);
		}
	}

	virtual double CountIdealInfo(EdgeId e1, EdgeId e2, size_t dist) {
		double res = 0.0;
		for (size_t i = 0; i < libs_.size(); ++i) {
			res += libs_[i]->IdealPairedInfo(e1, e2, (int) dist);
		}
		return res;
	}

	virtual double CountIdealInfo(const BidirectionalPath& p, EdgeId e,
			size_t gap) {
		double w = 0.0;
		for (int i = (int) p.Size() - 1; i >= 0; --i) {
			w += g_.length(p[i]) ?
					CountIdealInfo(p[i], e, gap + p.LengthAt(i)) > 0 : 0;
		}
		return w;
	}

	virtual double CountWeight(BidirectionalPath& path, EdgeId e,
			int gapLength = 0) {
		double weight = 0.0;
		for (size_t i = 0; i < libs_.size(); ++i) {
			weight += CountSingleLib((int) i, path, e, gapLength);
		}

		return weight / (double) max(libs_.size(), (size_t) 1);
	}

	virtual bool PairInfoExist(EdgeId first, EdgeId second, int distance) {
		for (size_t libIndex = 0; libIndex < libs_.size(); ++libIndex) {
			double w = libs_[libIndex]->CountPairedInfo(first, second,
					distance);
			double w_ideal = libs_[libIndex]->IdealPairedInfo(first, second,
					distance);
			if (w_ideal == 0.0) {
				continue;
			}
			if (normalizeWeight_) {
				w /= w_ideal;
			}
			double threshold =
					libs_[libIndex]->GetSingleThreshold() >= 0.0 ?
							libs_[libIndex]->GetSingleThreshold() :
							singleThreshold;
			if (w > threshold) {
				return true;
			}
		}
		return false;
	}

};
struct PathsPairIndexInfo {
    PathsPairIndexInfo(size_t edge1_, size_t edge2_, double w_, double dist_)
            : edge1(edge1_),
              edge2(edge2_),
              w(w_),
              dist(dist_) {

    }
    size_t edge1;
    size_t edge2;
    double w;
    double dist;
};
class PathsWeightCounter {
public:
    PathsWeightCounter(const Graph& g, PairedInfoLibrary& lib);
    map<size_t, double> FindPairInfoFromPath(
            const BidirectionalPath& path1,
            const BidirectionalPath& path2) const;
    double CountPairInfo(const BidirectionalPath& path1, size_t from1,
                         size_t to1, const BidirectionalPath& path2,
                         size_t from2, size_t to2) const;
    double CountPairInfo(const BidirectionalPath& path1, size_t from1,
                         size_t to1, EdgeId edge, size_t gap) const;
    void SetCommonWeightFrom(size_t iedge, double weight);
    void ClearCommonWeight();
private:
    void FindPairInfo(const BidirectionalPath& path1, size_t from1, size_t to1,
                      const BidirectionalPath& path2, size_t from2, size_t to2,
                      map<size_t, double>& pi, double& ideal_pi) const;
    void FindPairInfo(EdgeId e1, EdgeId e2, size_t dist, double& ideal_w,
                      double& result_w) const;
    const Graph& g_;
    PairedInfoLibrary& lib_;
    std::map<size_t, double> common_w_;
};
PathsWeightCounter::PathsWeightCounter(const Graph& g, PairedInfoLibrary& lib):g_(g), lib_(lib){

}
double PathsWeightCounter::CountPairInfo(const BidirectionalPath& path1,
                                         size_t from1, size_t to1,
                                         const BidirectionalPath& path2,
                                         size_t from2, size_t to2) const {
    map<size_t, double> pi;
    double ideal_pi = 0.0;
    FindPairInfo(path1, from1, to1, path2, from2, to2,
                                          pi, ideal_pi);
    double result = 0.0;
    double all_common = 0.0;
    for (size_t i = from1; i < to1; ++i) {
        if (common_w_.find(i) != common_w_.end()) {
            all_common += common_w_.at(i);
        }
        result += pi[i];
    }
    DEBUG("ideal _pi " << ideal_pi << " common " << all_common << " result " << result);
    ideal_pi -= all_common;
    result -= all_common;
    double total_result = math::ge(ideal_pi, 0.0) ? result / ideal_pi : 0.0;
    total_result = math::ge(total_result, 0.0) ? total_result : 0.0;
    DEBUG("ideal _pi " << ideal_pi << " result " << result << " total_result " << total_result);
    return total_result;
}

double PathsWeightCounter::CountPairInfo(const BidirectionalPath& path1,
                                         size_t from1, size_t to1, EdgeId edge,
                                         size_t gap) const {
    double result = 0.0;
    for (size_t i1 = from1; i1 < to1; ++i1) {
        double ideal_w, w;
        FindPairInfo(path1.At(i1), edge, gap + path1.LengthAt(i1), ideal_w, w);
        result += w;
    }
    return result;
}

void PathsWeightCounter::FindPairInfo(const BidirectionalPath& path1,
                                      size_t from1, size_t to1,
                                      const BidirectionalPath& path2,
                                      size_t from2, size_t to2,
                                      map<size_t, double>& pi,
                                      double& ideal_pi) const {
    stringstream str;
    for (size_t i = 0; i < path2.Size(); ++i) {
        str << g_.int_id(path2.At(i)) << " ";
    }DEBUG("pair info for path " << str.str());
    for (size_t i1 = from1; i1 < to1; ++i1) {
        for (size_t i2 = from2; i2 < to2; ++i2) {
            size_t dist = path1.LengthAt(i1) + path2.Length()
                    - path2.LengthAt(i2);
            double ideal_w = 0.0;
            double w = 0.0;
            FindPairInfo(path1.At(i1), path2.At(i2), dist, ideal_w, w);
            ideal_pi += ideal_w;
            if (math::gr(ideal_w, 0.0) && math::ls(w, ideal_w)) {
                DEBUG("i1 " << i1 << " i2 " << i2
                		<<" w " << w
                		<< " ideal " << ideal_w
                		<< " e1 " << g_.int_id (path1.At(i1))
                		<< " e2 " << g_.int_id(path2.At(i2)) << " dist " << dist);
            }
            if (pi.find(i1) == pi.end()) {
                pi[i1] = 0;
            }
            pi[i1] += w;
        }
    }
}

void PathsWeightCounter::FindPairInfo(EdgeId e1, EdgeId e2, size_t dist,
                                      double& ideal_w, double& result_w) const {
	ideal_w = lib_.IdealPairedInfo(e1, e2, (int) dist);
	result_w = 0.0;
    if (ideal_w == 0.0) {
        return;
    }
    double w = lib_.CountPairedInfo(e1, e2, (int) dist, true);
    if (w > 10.0) {
        result_w = ideal_w;
    }
}
map<size_t, double> PathsWeightCounter::FindPairInfoFromPath(
        const BidirectionalPath& path1, const BidirectionalPath& path2) const {
    map<size_t, double> pi;
    double ideal_pi = 0;
    FindPairInfo(path1, 0, path1.Size(), path2, 0, path2.Size(), pi, ideal_pi);
    return pi;
}
void PathsWeightCounter::SetCommonWeightFrom(size_t iedge, double weight) {
	common_w_[iedge] = weight;
}
void PathsWeightCounter::ClearCommonWeight() {
	common_w_.clear();
}
};

#endif /* WEIGHT_COUNTER_HPP_ */
