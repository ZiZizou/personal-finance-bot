#pragma once
#define _USE_MATH_DEFINES
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Hidden Markov Model for market regime detection
namespace RegimeDetection {

// Gaussian Hidden Markov Model
// Each state has its own Gaussian emission distribution
class HiddenMarkovModel {
public:
    struct State {
        double mean;        // Mean of emission distribution
        double variance;    // Variance of emission distribution
        double mean2;       // Second feature mean
        double variance2;   // Second feature variance

        State() : mean(0.0), variance(1.0), mean2(0.0), variance2(1.0) {}
        State(double m, double v) : mean(m), variance(v), mean2(0.0), variance2(1.0) {}
        State(double m, double v, double m2, double v2)
            : mean(m), variance(v), mean2(m2), variance2(v2) {}
    };

private:
    int numStates_;
    int maxIterations_;
    double convergenceThreshold_;

    // Model parameters
    std::vector<double> initialProbs_;    // Initial state probabilities
    std::vector<std::vector<double>> transitionMatrix_;  // Transition matrix A[i][j]
    std::vector<State> states_;           // Emission parameters

    // Training data
    std::vector<std::vector<double>> observations_;

    // Training status
    bool isTrained_ = false;
    double finalLogLikelihood_ = 0.0;

public:
    HiddenMarkovModel(int numStates = 3, int maxIterations = 100,
                      double convergenceThreshold = 1e-4)
        : numStates_(numStates),
          maxIterations_(maxIterations),
          convergenceThreshold_(convergenceThreshold) {

        initialProbs_.resize(numStates_, 1.0 / numStates_);
        transitionMatrix_.resize(numStates_, std::vector<double>(numStates_, 1.0 / numStates_));
        states_.resize(numStates_);
    }

    // Train the HMM using Baum-Welch algorithm
    // observations: vector of observation vectors (each obs is [feature1, feature2])
    bool train(const std::vector<std::vector<double>>& observations) {
        if (observations.empty() || observations[0].empty()) {
            return false;
        }

        observations_ = observations;
        int T = (int)observations.size();
        int obsDim = (int)observations[0].size();

        // Initialize model parameters randomly
        initializeRandom();

        double prevLogLikelihood = -1e100;

        // Baum-Welch iterations
        for (int iter = 0; iter < maxIterations_; ++iter) {
            // E-step: Compute forward and backward probabilities
            std::vector<std::vector<double>> alpha(T, std::vector<double>(numStates_));
            std::vector<std::vector<double>> beta(T, std::vector<double>(numStates_));
            std::vector<std::vector<double>> gamma(T, std::vector<double>(numStates_));
            std::vector<std::vector<std::vector<double>>> xi(T - 1,
                std::vector<std::vector<double>>(numStates_, std::vector<double>(numStates_)));

            // Forward pass
            for (int j = 0; j < numStates_; ++j) {
                alpha[0][j] = initialProbs_[j] * emissionProbability(0, j);
            }

            for (int t = 1; t < T; ++t) {
                for (int j = 0; j < numStates_; ++j) {
                    double sum = 0.0;
                    for (int i = 0; i < numStates_; ++i) {
                        sum += alpha[t-1][i] * transitionMatrix_[i][j];
                    }
                    alpha[t][j] = sum * emissionProbability(t, j);
                }
            }

            // Backward pass
            for (int j = 0; j < numStates_; ++j) {
                beta[T-1][j] = 1.0;
            }

            for (int t = T - 2; t >= 0; --t) {
                for (int i = 0; i < numStates_; ++i) {
                    double sum = 0.0;
                    for (int j = 0; j < numStates_; ++j) {
                        sum += transitionMatrix_[i][j] * emissionProbability(t + 1, j) * beta[t + 1][j];
                    }
                    beta[t][i] = sum;
                }
            }

            // Compute log likelihood
            double logLikelihood = 0.0;
            for (int j = 0; j < numStates_; ++j) {
                logLikelihood += alpha[T-1][j];
            }
            logLikelihood = std::log(std::max(logLikelihood, 1e-300));

            // Check convergence
            if (std::fabs(logLikelihood - prevLogLikelihood) < convergenceThreshold_) {
                finalLogLikelihood_ = logLikelihood;
                isTrained_ = true;
                return true;
            }
            prevLogLikelihood = logLikelihood;

            // Compute gamma (posterior state probabilities)
            for (int t = 0; t < T; ++t) {
                double sum = 0.0;
                for (int j = 0; j < numStates_; ++j) {
                    sum += alpha[t][j] * beta[t][j];
                }
                for (int j = 0; j < numStates_; ++j) {
                    gamma[t][j] = (alpha[t][j] * beta[t][j]) / std::max(sum, 1e-300);
                }
            }

            // Compute xi (posterior transition probabilities)
            for (int t = 0; t < T - 1; ++t) {
                double sum = 0.0;
                for (int i = 0; i < numStates_; ++i) {
                    for (int j = 0; j < numStates_; ++j) {
                        sum += alpha[t][i] * transitionMatrix_[i][j] *
                               emissionProbability(t + 1, j) * beta[t + 1][j];
                    }
                }
                for (int i = 0; i < numStates_; ++i) {
                    for (int j = 0; j < numStates_; ++j) {
                        xi[t][i][j] = (alpha[t][i] * transitionMatrix_[i][j] *
                                      emissionProbability(t + 1, j) * beta[t + 1][j]) /
                                     std::max(sum, 1e-300);
                    }
                }
            }

            // M-step: Update parameters
            // Update initial probabilities
            for (int j = 0; j < numStates_; ++j) {
                initialProbs_[j] = gamma[0][j];
            }

            // Update transition matrix
            for (int i = 0; i < numStates_; ++i) {
                double sum = 0.0;
                for (int t = 0; t < T - 1; ++t) {
                    sum += gamma[t][i];
                }
                for (int j = 0; j < numStates_; ++j) {
                    double numerator = 0.0;
                    for (int t = 0; t < T - 1; ++t) {
                        numerator += xi[t][i][j];
                    }
                    transitionMatrix_[i][j] = numerator / std::max(sum, 1e-300);
                }
            }

            // Update emission parameters (states)
            for (int j = 0; j < numStates_; ++j) {
                double sum = 0.0;
                for (int t = 0; t < T; ++t) {
                    sum += gamma[t][j];
                }

                // Update mean for each feature
                double newMean = 0.0;
                double newMean2 = 0.0;
                for (int t = 0; t < T; ++t) {
                    if (obsDim >= 1) {
                        newMean += gamma[t][j] * observations[t][0];
                    }
                    if (obsDim >= 2) {
                        newMean2 += gamma[t][j] * observations[t][1];
                    }
                }
                states_[j].mean = newMean / std::max(sum, 1e-300);
                if (obsDim >= 2) {
                    states_[j].mean2 = newMean2 / std::max(sum, 1e-300);
                }

                // Update variance for each feature
                double newVar = 0.0;
                double newVar2 = 0.0;
                for (int t = 0; t < T; ++t) {
                    if (obsDim >= 1) {
                        double diff = observations[t][0] - states_[j].mean;
                        newVar += gamma[t][j] * diff * diff;
                    }
                    if (obsDim >= 2) {
                        double diff2 = observations[t][1] - states_[j].mean2;
                        newVar2 += gamma[t][j] * diff2 * diff2;
                    }
                }
                states_[j].variance = newVar / std::max(sum, 1e-300);
                if (obsDim >= 2) {
                    states_[j].variance2 = newVar2 / std::max(sum, 1e-300);
                }

                // Ensure minimum variance to avoid numerical issues
                states_[j].variance = std::max(states_[j].variance, 1e-6);
                states_[j].variance2 = std::max(states_[j].variance2, 1e-6);
            }
        }

        finalLogLikelihood_ = prevLogLikelihood;
        isTrained_ = true;
        return true;
    }

    // Predict most likely state for each observation
    std::vector<int> predictStates(const std::vector<std::vector<double>>& observations) const {
        if (!isTrained_ || observations.empty()) {
            return std::vector<int>();
        }

        std::vector<int> predictions;
        predictions.reserve(observations.size());

        // Use Viterbi algorithm for most likely state sequence
        int T = (int)observations.size();
        std::vector<std::vector<double>> delta(T, std::vector<double>(numStates_));
        std::vector<std::vector<int>> psi(T, std::vector<int>(numStates_));

        // Initialize
        for (int j = 0; j < numStates_; ++j) {
            delta[0][j] = initialProbs_[j] * emissionProbability(0, j);
        }

        // Recursion
        for (int t = 1; t < T; ++t) {
            for (int j = 0; j < numStates_; ++j) {
                double maxProb = -1e100;
                int maxState = 0;
                for (int i = 0; i < numStates_; ++i) {
                    double prob = delta[t-1][i] * transitionMatrix_[i][j];
                    if (prob > maxProb) {
                        maxProb = prob;
                        maxState = i;
                    }
                }
                delta[t][j] = maxProb * emissionProbability(t, j);
                psi[t][j] = maxState;
            }
        }

        // Backtrack
        std::vector<int> states(T);
        double maxFinal = -1e100;
        int lastState = 0;
        for (int j = 0; j < numStates_; ++j) {
            if (delta[T-1][j] > maxFinal) {
                maxFinal = delta[T-1][j];
                lastState = j;
            }
        }
        states[T-1] = lastState;

        for (int t = T - 2; t >= 0; --t) {
            states[t] = psi[t + 1][states[t + 1]];
        }

        return states;
    }

    // Predict most likely state for the last observation
    int predictCurrentState(const std::vector<double>& observation) const {
        if (!isTrained_) {
            return -1;
        }

        // Compute posterior probabilities for this single observation
        std::vector<double> posterior(numStates_);
        double sum = 0.0;

        for (int j = 0; j < numStates_; ++j) {
            posterior[j] = initialProbs_[j] * emissionProbability(observation, j);
            sum += posterior[j];
        }

        if (sum < 1e-300) {
            return 0;  // Default to first state
        }

        // Return most likely state
        double maxProb = -1e100;
        int bestState = 0;
        for (int j = 0; j < numStates_; ++j) {
            posterior[j] /= sum;
            if (posterior[j] > maxProb) {
                maxProb = posterior[j];
                bestState = j;
            }
        }

        return bestState;
    }

    // Get state probabilities for current observation
    std::vector<double> getStateProbabilities(const std::vector<double>& observation) const {
        std::vector<double> probs(numStates_, 0.0);

        if (!isTrained_) {
            return probs;
        }

        double sum = 0.0;
        for (int j = 0; j < numStates_; ++j) {
            probs[j] = initialProbs_[j] * emissionProbability(observation, j);
            sum += probs[j];
        }

        if (sum > 1e-300) {
            for (int j = 0; j < numStates_; ++j) {
                probs[j] /= sum;
            }
        }

        return probs;
    }

    // Getters
    int getNumStates() const { return numStates_; }
    const std::vector<double>& getInitialProbs() const { return initialProbs_; }
    const std::vector<std::vector<double>>& getTransitionMatrix() const { return transitionMatrix_; }
    const std::vector<State>& getStates() const { return states_; }
    bool isTrained() const { return isTrained_; }
    double getFinalLogLikelihood() const { return finalLogLikelihood_; }

private:
    void initializeRandom() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0.0, 1.0);

        // Random initial probabilities
        double sum = 0.0;
        for (int i = 0; i < numStates_; ++i) {
            initialProbs_[i] = dis(gen);
            sum += initialProbs_[i];
        }
        for (int i = 0; i < numStates_; ++i) {
            initialProbs_[i] /= sum;
        }

        // Random transition matrix
        for (int i = 0; i < numStates_; ++i) {
            sum = 0.0;
            for (int j = 0; j < numStates_; ++j) {
                transitionMatrix_[i][j] = dis(gen);
                sum += transitionMatrix_[i][j];
            }
            for (int j = 0; j < numStates_; ++j) {
                transitionMatrix_[i][j] /= sum;
            }
        }

        // Random state parameters
        for (int i = 0; i < numStates_; ++i) {
            states_[i].mean = dis(gen) * 2.0 - 1.0;  // -1 to 1
            states_[i].variance = dis(gen) * 0.5 + 0.1;  // 0.1 to 0.6
            states_[i].mean2 = dis(gen) * 2.0 - 1.0;
            states_[i].variance2 = dis(gen) * 0.5 + 0.1;
        }
    }

    // Compute emission probability for observation at time t
    double emissionProbability(int t, int state) const {
        if (t < 0 || t >= (int)observations_.size()) {
            return 1e-300;
        }
        return emissionProbability(observations_[t], state);
    }

    // Compute emission probability for a single observation
    double emissionProbability(const std::vector<double>& obs, int state) const {
        if (state < 0 || state >= numStates_ || obs.empty()) {
            return 1e-300;
        }

        const State& s = states_[state];
        double prob = 1.0;

        // Feature 1
        if (!obs.empty()) {
            double diff = obs[0] - s.mean;
            prob *= std::exp(-0.5 * diff * diff / s.variance) /
                   std::sqrt(2.0 * M_PI * s.variance);
        }

        // Feature 2
        if (obs.size() > 1) {
            double diff2 = obs[1] - s.mean2;
            prob *= std::exp(-0.5 * diff2 * diff2 / s.variance2) /
                   std::sqrt(2.0 * M_PI * s.variance2);
        }

        return std::max(prob, 1e-300);
    }
};

} // namespace RegimeDetection
