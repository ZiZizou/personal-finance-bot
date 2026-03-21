#pragma once
#include <vector>
#include <memory>
#include <algorithm>
#include <numeric>
#include <functional>
#include "Backtester.h"
#include "IStrategy.h"

// Walk-forward optimization window configuration
struct WalkForwardWindow {
    size_t trainStart;
    size_t trainEnd;
    size_t testStart;
    size_t testEnd;
};

// Result for a single walk-forward window
struct WFWindowResult {
    WalkForwardWindow window;
    std::vector<StrategyParams> optimizedParams;
    BacktestResult trainResult;
    BacktestResult testResult;
    double efficiency;  // Test performance / Train performance
};

// Complete walk-forward analysis result
struct WalkForwardResult {
    std::vector<WFWindowResult> windowResults;
    BacktestResult aggregateResult;
    double walkForwardEfficiency;     // Average test/train ratio
    double robustnessScore;           // How consistent are results across windows
    std::vector<StrategyParams> bestParams;

    bool isRobust() const {
        // Consider robust if efficiency > 0.5 and robustness > 0.6
        return walkForwardEfficiency > 0.5 && robustnessScore > 0.6;
    }
};

// Parameter grid for optimization
struct ParameterGrid {
    std::string name;
    double minValue;
    double maxValue;
    double step;

    std::vector<double> generateValues() const {
        std::vector<double> values;
        for (double v = minValue; v <= maxValue; v += step) {
            values.push_back(v);
        }
        return values;
    }

    size_t numValues() const {
        return static_cast<size_t>((maxValue - minValue) / step) + 1;
    }
};

// Walk-Forward Optimizer
class WalkForwardOptimizer {
public:
    struct Config {
        int numWindows = 5;              // Number of rolling windows
        double trainRatio = 0.7;         // Portion of each window for training
        int minTrainBars = 100;          // Minimum training bars
        int minTestBars = 30;            // Minimum test bars
        bool anchoredWalk = false;       // If true, train always starts from beginning
        std::string optimizationTarget = "sharpe";  // sharpe, return, calmar
    };

private:
    Config config_;
    BacktestConfig backtestConfig_;

    // Optimization objective function
    double getObjectiveValue(const BacktestResult& result) const {
        if (config_.optimizationTarget == "sharpe") {
            return result.sharpeRatio;
        } else if (config_.optimizationTarget == "return") {
            return result.totalReturn;
        } else if (config_.optimizationTarget == "calmar") {
            return result.calmarRatio;
        } else if (config_.optimizationTarget == "sortino") {
            return result.sortinoRatio;
        }
        return result.sharpeRatio;
    }

public:
    WalkForwardOptimizer()
        : config_(Config()), backtestConfig_(BacktestConfig::defaultConfig()) {}

    WalkForwardOptimizer(const Config& config,
                         const BacktestConfig& backtestConfig = BacktestConfig::defaultConfig())
        : config_(config), backtestConfig_(backtestConfig) {}

    // Generate walk-forward windows from data
    std::vector<WalkForwardWindow> generateWindows(size_t dataSize) const {
        std::vector<WalkForwardWindow> windows;

        if (dataSize < (size_t)(config_.minTrainBars + config_.minTestBars)) {
            return windows;  // Not enough data
        }

        size_t windowSize = dataSize / config_.numWindows;
        size_t trainSize = (size_t)(windowSize * config_.trainRatio);
        size_t testSize = windowSize - trainSize;

        // Ensure minimum sizes
        trainSize = std::max(trainSize, (size_t)config_.minTrainBars);
        testSize = std::max(testSize, (size_t)config_.minTestBars);

        for (int i = 0; i < config_.numWindows; ++i) {
            WalkForwardWindow w;

            if (config_.anchoredWalk) {
                // Anchored: train always from beginning
                w.trainStart = 0;
                w.trainEnd = (i + 1) * trainSize / config_.numWindows + trainSize;
            } else {
                // Rolling: train window slides
                w.trainStart = i * (dataSize - trainSize - testSize) / (config_.numWindows - 1);
                w.trainEnd = w.trainStart + trainSize;
            }

            w.testStart = w.trainEnd;
            w.testEnd = std::min(w.testStart + testSize, dataSize);

            // Validate window
            if (w.testEnd > dataSize) continue;
            if (w.trainEnd - w.trainStart < (size_t)config_.minTrainBars) continue;
            if (w.testEnd - w.testStart < (size_t)config_.minTestBars) continue;

            windows.push_back(w);
        }

        return windows;
    }

    // Optimize parameters on training data using grid search
    std::vector<StrategyParams> optimizeParameters(
        IStrategy& strategy,
        const std::vector<Candle>& trainData,
        const std::vector<ParameterGrid>& paramGrids) {

        auto baseParams = strategy.getParameters();
        if (baseParams.empty() || paramGrids.empty()) {
            return baseParams;
        }

        // Generate all parameter combinations
        std::vector<std::vector<StrategyParams>> allCombinations;
        generateCombinations(baseParams, paramGrids, 0, allCombinations);

        double bestObjective = -1e18;
        std::vector<StrategyParams> bestParams = baseParams;

        Backtester backtester(backtestConfig_);

        // Test each combination
        for (const auto& paramSet : allCombinations) {
            auto strategyClone = strategy.clone();
            strategyClone->setParameters(paramSet);

            BacktestResult result = backtester.run(trainData, *strategyClone);
            double objective = getObjectiveValue(result);

            if (objective > bestObjective) {
                bestObjective = objective;
                bestParams = paramSet;
            }
        }

        return bestParams;
    }

    // Run complete walk-forward analysis
    WalkForwardResult run(
        IStrategy& strategy,
        const std::vector<Candle>& data,
        const std::vector<ParameterGrid>& paramGrids = {}) {

        WalkForwardResult result;

        // Generate windows
        auto windows = generateWindows(data.size());
        if (windows.empty()) {
            return result;
        }

        Backtester backtester(backtestConfig_);
        std::vector<double> efficiencies;
        std::vector<double> testReturns;

        // Process each window
        for (const auto& window : windows) {
            WFWindowResult wfResult;
            wfResult.window = window;

            // Extract train/test data
            std::vector<Candle> trainData(data.begin() + window.trainStart,
                                          data.begin() + window.trainEnd);
            std::vector<Candle> testData(data.begin() + window.testStart,
                                         data.begin() + window.testEnd);

            // Optimize on training data
            if (!paramGrids.empty()) {
                wfResult.optimizedParams = optimizeParameters(strategy, trainData, paramGrids);
                strategy.setParameters(wfResult.optimizedParams);
            }

            // Backtest on training data
            wfResult.trainResult = backtester.run(trainData, strategy);

            // Backtest on test data (out-of-sample)
            strategy.reset();
            wfResult.testResult = backtester.run(testData, strategy);

            // Calculate efficiency
            double trainObj = getObjectiveValue(wfResult.trainResult);
            double testObj = getObjectiveValue(wfResult.testResult);

            if (std::abs(trainObj) > 1e-12) {
                wfResult.efficiency = testObj / trainObj;
            } else {
                wfResult.efficiency = 0.0;
            }

            efficiencies.push_back(wfResult.efficiency);
            testReturns.push_back(wfResult.testResult.totalReturn);

            result.windowResults.push_back(wfResult);
        }

        // Calculate aggregate metrics
        result.walkForwardEfficiency = std::accumulate(efficiencies.begin(),
            efficiencies.end(), 0.0) / efficiencies.size();

        // Robustness: how consistent are the test returns
        double meanReturn = std::accumulate(testReturns.begin(),
            testReturns.end(), 0.0) / testReturns.size();
        double variance = 0.0;
        for (double r : testReturns) {
            variance += (r - meanReturn) * (r - meanReturn);
        }
        variance /= testReturns.size();

        // Robustness score: higher if returns are consistent
        if (std::abs(meanReturn) > 1e-12) {
            double cv = std::sqrt(variance) / std::abs(meanReturn);  // Coefficient of variation
            result.robustnessScore = 1.0 / (1.0 + cv);  // Transform to 0-1 scale
        } else {
            result.robustnessScore = 0.0;
        }

        // Use parameters from the best window for final params
        double bestOOSPerformance = -1e18;
        for (const auto& wr : result.windowResults) {
            double perf = getObjectiveValue(wr.testResult);
            if (perf > bestOOSPerformance) {
                bestOOSPerformance = perf;
                result.bestParams = wr.optimizedParams;
            }
        }

        // Calculate aggregate backtest result (combine all test periods)
        calculateAggregateResult(result);

        return result;
    }

private:
    void generateCombinations(
        std::vector<StrategyParams>& current,
        const std::vector<ParameterGrid>& grids,
        size_t gridIdx,
        std::vector<std::vector<StrategyParams>>& allCombinations) {

        if (gridIdx >= grids.size()) {
            allCombinations.push_back(current);
            return;
        }

        const auto& grid = grids[gridIdx];
        auto values = grid.generateValues();

        for (double val : values) {
            // Find and update the parameter
            for (auto& param : current) {
                if (param.name == grid.name) {
                    param.value = val;
                    break;
                }
            }
            generateCombinations(current, grids, gridIdx + 1, allCombinations);
        }
    }

    void calculateAggregateResult(WalkForwardResult& result) {
        // Combine metrics from all test windows
        double totalReturn = 1.0;
        int totalTrades = 0;
        int totalWins = 0;
        std::vector<double> allReturns;

        for (const auto& wr : result.windowResults) {
            totalReturn *= (1.0 + wr.testResult.totalReturn);
            totalTrades += wr.testResult.trades;
            totalWins += wr.testResult.wins;

            for (double r : wr.testResult.dailyReturns) {
                allReturns.push_back(r);
            }
        }

        result.aggregateResult.totalReturn = totalReturn - 1.0;
        result.aggregateResult.trades = totalTrades;
        result.aggregateResult.wins = totalWins;
        result.aggregateResult.winRate = totalTrades > 0 ?
            static_cast<double>(totalWins) / totalTrades : 0.0;

        // Calculate Sharpe from combined returns
        if (allReturns.size() > 1) {
            double mean = std::accumulate(allReturns.begin(),
                allReturns.end(), 0.0) / allReturns.size();
            double variance = 0.0;
            for (double r : allReturns) {
                variance += (r - mean) * (r - mean);
            }
            variance /= (allReturns.size() - 1);
            double stdDev = std::sqrt(variance);

            if (stdDev > 1e-12) {
                result.aggregateResult.sharpeRatio = (mean / stdDev) * std::sqrt(252.0);
            }
        }
    }
};

// Monte Carlo simulation for parameter robustness
class MonteCarloValidator {
public:
    struct Result {
        double meanReturn;
        double stdReturn;
        double probability95;   // P(return > 0) at 95% confidence
        std::vector<double> returnDistribution;
    };

    static Result validate(
        IStrategy& strategy,
        const std::vector<Candle>& data,
        const BacktestConfig& config,
        int numSimulations = 100,
        double noiseLevel = 0.001) {

        Result result;
        result.returnDistribution.reserve(numSimulations);

        Backtester backtester(config);

        for (int sim = 0; sim < numSimulations; ++sim) {
            // Create noisy version of data
            std::vector<Candle> noisyData = addNoise(data, noiseLevel);

            // Run backtest
            auto btResult = backtester.run(noisyData, strategy);
            result.returnDistribution.push_back(btResult.totalReturn);

            strategy.reset();
        }

        // Calculate statistics
        result.meanReturn = std::accumulate(result.returnDistribution.begin(),
            result.returnDistribution.end(), 0.0) / numSimulations;

        double variance = 0.0;
        for (double r : result.returnDistribution) {
            variance += (r - result.meanReturn) * (r - result.meanReturn);
        }
        result.stdReturn = std::sqrt(variance / numSimulations);

        // Sort for percentile calculation
        std::vector<double> sorted = result.returnDistribution;
        std::sort(sorted.begin(), sorted.end());

        // 5th percentile (95% confidence lower bound)
        size_t idx5 = static_cast<size_t>(0.05 * sorted.size());
        result.probability95 = sorted[idx5];

        return result;
    }

private:
    static std::vector<Candle> addNoise(const std::vector<Candle>& data, double level) {
        std::vector<Candle> noisy = data;

        for (auto& c : noisy) {
            double noise = (static_cast<double>(rand()) / RAND_MAX - 0.5) * 2.0 * level;
            c.close *= (1.0 + noise);
            c.open *= (1.0 + noise * 0.8);
            c.high *= (1.0 + noise * 1.1);
            c.low *= (1.0 + noise * 0.9);

            // Ensure OHLC consistency
            c.high = std::max({c.open, c.close, c.high});
            c.low = std::min({c.open, c.close, c.low});
        }

        return noisy;
    }
};
