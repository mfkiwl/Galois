#include <iomanip>
#include <sys/time.h>
#include <sys/resource.h>
#include "gnn.h"
#include "math_functions.hpp"

class ResourceManager {
public:
	ResourceManager() {}
	~ResourceManager(){}
	//peak memory usage
	std::string get_peak_memory() {
		double kbm;
		struct rusage CurUsage;
		getrusage(RUSAGE_SELF, &CurUsage);
		kbm = (double)CurUsage.ru_maxrss;
		double mbm = kbm / 1024.0;
		double gbm = mbm / 1024.0;
		return
			"Peak memory: " +
			to_string_with_precision(mbm, 3) + " MB; " +
			to_string_with_precision(gbm, 3) + " GB";
	}
private:
	template <typename T = double>
	std::string to_string_with_precision(const T a_value, const int& n) {
		std::ostringstream out;
		out << std::fixed;
		out << std::setprecision(n) << a_value;
		return out.str();
	}
};

class Timer {
public:
	Timer() {}
	void Start() { gettimeofday(&start_time_, NULL); }
	void Stop() {
		gettimeofday(&elapsed_time_, NULL);
		elapsed_time_.tv_sec  -= start_time_.tv_sec;
		elapsed_time_.tv_usec -= start_time_.tv_usec;
	}
	double Seconds() const { return elapsed_time_.tv_sec + (double)elapsed_time_.tv_usec/1e6; }
	double Millisecs() const { return 1000*elapsed_time_.tv_sec + (double)elapsed_time_.tv_usec/1000; }
	double Microsecs() const { return 1e6*elapsed_time_.tv_sec + (double)elapsed_time_.tv_usec; }
private:
	struct timeval start_time_;
	struct timeval elapsed_time_;
};

inline AccT masked_avg_loss(std::vector<AccT> &loss, MaskList &masks) {
	size_t n = loss.size();
	AccT sum_mask = std::accumulate(masks.begin(), masks.end(), (AccT)0);
	//float avg_mask = reduce_mean<MaskT>(masks);
	AccT avg_mask = sum_mask / (AccT)n;
	for (size_t i = 0; i < n; i ++) loss[i] = loss[i] * (AccT)(masks[i]) / avg_mask;
	AccT sum_loss = std::accumulate(loss.begin(), loss.end(), (AccT)0);
	//AccT sum_loss = 0.0;
	//for (size_t i = 0; i < n; i ++) sum_loss += loss[i];
	return sum_loss / (AccT)n;
}

inline AccT masked_accuracy(LabelList &preds, LabelList &labels, MaskList &masks) {
	size_t n = labels.size();
	std::vector<AccT> accuracy_all(n, 0.0);
	for (size_t i = 0; i < n; i++)
		if (preds[i] == labels[i]) accuracy_all[i] = 1.0;
	auto sum_mask = std::accumulate(masks.begin(), masks.end(), (int)0);
	AccT avg_mask = (AccT)sum_mask / (AccT)n;
	for (size_t i = 0; i < n; i ++) 
		accuracy_all[i] = accuracy_all[i] * (AccT)masks[i] / avg_mask;
	AccT sum_accuracy_all = std::accumulate(accuracy_all.begin(), accuracy_all.end(), (AccT)0);
	return sum_accuracy_all / (AccT)n;
}

