#pragma once
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <cmath>
#include <numeric>
#include <algorithm>
#include "Portfolio.h"
#include "IStrategy.h"
#include "Backtester.h"

// Multi-asset Portfolio Backtester
class PortfolioBacktester {
private:
    PortfolioConfig config_;

public:
    PortfolioBacktester(const PortfolioConfig& config = PortfolioConfig::defaultConfig())
        : config_(config) {}

    // Run portfolio backtest with uniform strategy across all assets
    PortfolioBacktestResult run(
        const std::map<std::string, std::vector<Candle>>& multiAssetData,
        IStrategy& strategy,
        const TargetAllocation& initialAllocation) {

        PortfolioBacktestResult result;

        // Validate data
        if (multiAssetData.empty()) return result;

        // Find common date range and minimum length
        size_t minLength = SIZE_MAX;
        for (const auto& [symbol, candles] : multiAssetData) {
            minLength = std::min(minLength, candles.size());
        }

        if (minLength < (size_t)strategy.getWarmupPeriod() + 10) {
            return result;
        }

        // Initialize portfolio manager
        PortfolioManager manager(config_);

        // Track equity curve
        std::vector<float> equityCurve;
        equityCurve.reserve(minLength);

        float initialValue = config_.baseConfig.initialCapital;
        float peakValue = initialValue;

        // Generate signals per asset
        std::map<std::string, std::unique_ptr<IStrategy>> assetStrategies;
        for (const auto& [symbol, candles] : multiAssetData) {
            assetStrategies[symbol] = strategy.clone();
        }

        // Main backtest loop
        int warmupPeriod = strategy.getWarmupPeriod();
        for (size_t i = warmupPeriod; i < minLength; ++i) {
            // Get current prices
            std::map<std::string, float> currentPrices;
            std::string currentDate;
            for (const auto& [symbol, candles] : multiAssetData) {
                currentPrices[symbol] = candles[i].close;
                currentDate = candles[i].date;
            }

            // Update portfolio with current prices
            manager.updatePrices(currentPrices, currentDate);

            // Generate signals for each asset
            std::map<std::string, StrategySignal> signals;
            for (const auto& [symbol, candles] : multiAssetData) {
                std::vector<Candle> history(candles.begin(), candles.begin() + i + 1);
                signals[symbol] = assetStrategies[symbol]->generateSignal(history, i);
            }

            // Calculate target allocation based on signals
            TargetAllocation targetAlloc = calculateTargetAllocation(
                signals, initialAllocation, manager.getState());

            // Check if rebalancing is needed
            if (manager.needsRebalance(targetAlloc, i)) {
                RebalanceRecord rebalance = manager.rebalance(targetAlloc, currentPrices, i);
                result.rebalanceLog.push_back(rebalance);
                result.rebalanceCount++;
                result.totalRebalanceCost += rebalance.transactionCost;
            }

            // Record snapshot
            result.snapshots.push_back(manager.getState());

            // Track equity
            float currentValue = manager.getState().totalValue;
            equityCurve.push_back(currentValue);

            // Track drawdown
            if (currentValue > peakValue) peakValue = currentValue;
            float drawdown = (peakValue - currentValue) / peakValue;
            if (drawdown > result.aggregateMetrics.maxDrawdown) {
                result.aggregateMetrics.maxDrawdown = drawdown;
            }
        }

        // Calculate final metrics
        calculateFinalMetrics(result, equityCurve, multiAssetData);

        // Calculate per-asset results
        Backtester singleBacktester(config_.baseConfig);
        for (const auto& [symbol, candles] : multiAssetData) {
            result.perAssetResults[symbol] = singleBacktester.run(candles, *assetStrategies[symbol]);
        }

        return result;
    }

    // Run with different strategies per asset
    PortfolioBacktestResult run(
        const std::map<std::string, std::vector<Candle>>& multiAssetData,
        const std::map<std::string, std::shared_ptr<IStrategy>>& assetStrategies,
        const TargetAllocation& initialAllocation) {

        PortfolioBacktestResult result;

        if (multiAssetData.empty() || assetStrategies.empty()) return result;

        size_t minLength = SIZE_MAX;
        int maxWarmup = 0;
        for (const auto& [symbol, candles] : multiAssetData) {
            minLength = std::min(minLength, candles.size());
            if (assetStrategies.count(symbol)) {
                maxWarmup = std::max(maxWarmup, assetStrategies.at(symbol)->getWarmupPeriod());
            }
        }

        if (minLength < (size_t)maxWarmup + 10) return result;

        PortfolioManager manager(config_);
        std::vector<float> equityCurve;
        float peakValue = config_.baseConfig.initialCapital;

        for (size_t i = maxWarmup; i < minLength; ++i) {
            std::map<std::string, float> currentPrices;
            std::string currentDate;
            for (const auto& [symbol, candles] : multiAssetData) {
                currentPrices[symbol] = candles[i].close;
                currentDate = candles[i].date;
            }

            manager.updatePrices(currentPrices, currentDate);

            std::map<std::string, StrategySignal> signals;
            for (const auto& [symbol, strategy] : assetStrategies) {
                if (multiAssetData.count(symbol)) {
                    const auto& candles = multiAssetData.at(symbol);
                    std::vector<Candle> history(candles.begin(), candles.begin() + i + 1);
                    signals[symbol] = strategy->generateSignal(history, i);
                }
            }

            TargetAllocation targetAlloc = calculateTargetAllocation(
                signals, initialAllocation, manager.getState());

            if (manager.needsRebalance(targetAlloc, i)) {
                RebalanceRecord rebalance = manager.rebalance(targetAlloc, currentPrices, i);
                result.rebalanceLog.push_back(rebalance);
                result.rebalanceCount++;
                result.totalRebalanceCost += rebalance.transactionCost;
            }

            result.snapshots.push_back(manager.getState());

            float currentValue = manager.getState().totalValue;
            equityCurve.push_back(currentValue);

            if (currentValue > peakValue) peakValue = currentValue;
            float drawdown = (peakValue - currentValue) / peakValue;
            if (drawdown > result.aggregateMetrics.maxDrawdown) {
                result.aggregateMetrics.maxDrawdown = drawdown;
            }
        }

        calculateFinalMetrics(result, equityCurve, multiAssetData);

        Backtester singleBacktester(config_.baseConfig);
        for (const auto& [symbol, strategy] : assetStrategies) {
            if (multiAssetData.count(symbol)) {
                result.perAssetResults[symbol] = singleBacktester.run(
                    multiAssetData.at(symbol), *strategy);
            }
        }

        return result;
    }

private:
    TargetAllocation calculateTargetAllocation(
        const std::map<std::string, StrategySignal>& signals,
        const TargetAllocation& baseAllocation,
        const PortfolioSnapshot& currentState) {

        TargetAllocation target = baseAllocation;

        // Adjust weights based on signals
        for (auto& [symbol, weight] : target.weights) {
            if (!signals.count(symbol)) continue;

            const auto& signal = signals.at(symbol);

            // Scale weight by signal strength
            if (signal.type == SignalType::Buy) {
                weight *= (1.0f + signal.strength * 0.5f);  // Up to 50% increase
            } else if (signal.type == SignalType::Sell) {
                weight *= (1.0f - std::abs(signal.strength) * 0.5f);  // Up to 50% decrease
            }

            // Apply constraints
            weight = std::max(config_.minSingleAssetWeight, weight);
            weight = std::min(config_.maxSingleAssetWeight, weight);
        }

        // Ensure minimum cash reserve
        float totalAssetWeight = 0.0f;
        for (const auto& [symbol, weight] : target.weights) {
            totalAssetWeight += weight;
        }

        float maxAssetWeight = 1.0f - config_.minCashReserve;
        if (totalAssetWeight > maxAssetWeight) {
            float scale = maxAssetWeight / totalAssetWeight;
            for (auto& [symbol, weight] : target.weights) {
                weight *= scale;
            }
        }

        return target;
    }

    void calculateFinalMetrics(
        PortfolioBacktestResult& result,
        const std::vector<float>& equityCurve,
        const std::map<std::string, std::vector<Candle>>& multiAssetData) {

        if (equityCurve.empty()) return;

        float initialValue = config_.baseConfig.initialCapital;
        float finalValue = equityCurve.back();

        // Total return
        result.aggregateMetrics.totalReturn = (finalValue - initialValue) / initialValue;

        // Calculate daily returns
        std::vector<float> dailyReturns;
        for (size_t i = 1; i < equityCurve.size(); ++i) {
            if (equityCurve[i - 1] > 0) {
                dailyReturns.push_back(
                    (equityCurve[i] - equityCurve[i - 1]) / equityCurve[i - 1]);
            }
        }
        result.aggregateMetrics.dailyReturns = dailyReturns;
        result.aggregateMetrics.equityCurve = equityCurve;

        // Sharpe ratio
        if (dailyReturns.size() > 1) {
            float mean = std::accumulate(dailyReturns.begin(),
                dailyReturns.end(), 0.0f) / dailyReturns.size();
            float variance = 0.0f;
            for (float r : dailyReturns) {
                variance += (r - mean) * (r - mean);
            }
            variance /= (dailyReturns.size() - 1);
            float stdDev = std::sqrt(variance);

            float dailyRiskFree = config_.baseConfig.riskFreeRate /
                config_.baseConfig.tradingDaysPerYear;

            if (stdDev > 1e-9f) {
                result.aggregateMetrics.sharpeRatio =
                    ((mean - dailyRiskFree) / stdDev) *
                    std::sqrt((float)config_.baseConfig.tradingDaysPerYear);
            }

            result.aggregateMetrics.volatility = stdDev *
                std::sqrt((float)config_.baseConfig.tradingDaysPerYear);
        }

        // Annualized return
        float years = (float)dailyReturns.size() / config_.baseConfig.tradingDaysPerYear;
        if (years > 0 && result.aggregateMetrics.totalReturn > -1.0f) {
            result.aggregateMetrics.annualizedReturn =
                std::pow(1.0f + result.aggregateMetrics.totalReturn, 1.0f / years) - 1.0f;
        }

        // Calmar ratio
        if (result.aggregateMetrics.maxDrawdown > 0) {
            result.aggregateMetrics.calmarRatio =
                result.aggregateMetrics.annualizedReturn / result.aggregateMetrics.maxDrawdown;
        }

        // Average turnover
        if (result.rebalanceCount > 0) {
            float totalTurnover = 0.0f;
            for (const auto& rb : result.rebalanceLog) {
                totalTurnover += rb.totalTurnover;
            }
            result.avgTurnover = totalTurnover / result.rebalanceCount / initialValue;
        }

        // Diversification metrics
        calculateDiversificationMetrics(result, multiAssetData);
    }

    void calculateDiversificationMetrics(
        PortfolioBacktestResult& result,
        const std::map<std::string, std::vector<Candle>>& multiAssetData) {

        if (result.snapshots.empty()) return;

        // Calculate concentration index (Herfindahl)
        const auto& finalSnapshot = result.snapshots.back();
        float herfindahl = 0.0f;
        for (const auto& [symbol, weight] : finalSnapshot.weights) {
            herfindahl += weight * weight;
        }
        result.concentrationIndex = herfindahl;

        // Calculate correlation matrix and diversification ratio
        std::map<std::string, std::vector<float>> assetReturns;
        for (const auto& [symbol, candles] : multiAssetData) {
            std::vector<float> returns;
            for (size_t i = 1; i < candles.size(); ++i) {
                if (candles[i - 1].close > 0) {
                    returns.push_back(
                        (candles[i].close - candles[i - 1].close) / candles[i - 1].close);
                }
            }
            assetReturns[symbol] = returns;
        }

        // Calculate individual volatilities
        std::map<std::string, float> assetVols;
        for (const auto& [symbol, returns] : assetReturns) {
            if (returns.empty()) continue;
            float mean = std::accumulate(returns.begin(), returns.end(), 0.0f) / returns.size();
            float variance = 0.0f;
            for (float r : returns) {
                variance += (r - mean) * (r - mean);
            }
            assetVols[symbol] = std::sqrt(variance / returns.size());
        }

        // Calculate correlations
        std::vector<std::string> symbols;
        for (const auto& [s, _] : assetReturns) symbols.push_back(s);

        for (size_t i = 0; i < symbols.size(); ++i) {
            for (size_t j = i; j < symbols.size(); ++j) {
                const auto& r1 = assetReturns[symbols[i]];
                const auto& r2 = assetReturns[symbols[j]];

                size_t n = std::min(r1.size(), r2.size());
                if (n < 10) continue;

                float mean1 = std::accumulate(r1.begin(), r1.begin() + n, 0.0f) / n;
                float mean2 = std::accumulate(r2.begin(), r2.begin() + n, 0.0f) / n;

                float cov = 0.0f, var1 = 0.0f, var2 = 0.0f;
                for (size_t k = 0; k < n; ++k) {
                    cov += (r1[k] - mean1) * (r2[k] - mean2);
                    var1 += (r1[k] - mean1) * (r1[k] - mean1);
                    var2 += (r2[k] - mean2) * (r2[k] - mean2);
                }

                float corr = 0.0f;
                if (var1 > 0 && var2 > 0) {
                    corr = cov / std::sqrt(var1 * var2);
                }

                result.correlationMatrix[{symbols[i], symbols[j]}] = corr;
                result.correlationMatrix[{symbols[j], symbols[i]}] = corr;
            }
        }

        // Diversification ratio: weighted avg vol / portfolio vol
        float weightedVolSum = 0.0f;
        for (const auto& [symbol, weight] : finalSnapshot.weights) {
            if (assetVols.count(symbol)) {
                weightedVolSum += weight * assetVols[symbol];
            }
        }

        if (result.aggregateMetrics.volatility > 0) {
            result.diversificationRatio = weightedVolSum /
                (result.aggregateMetrics.volatility / std::sqrt(252.0f));
        }

        // Return contribution
        for (const auto& [symbol, btResult] : result.perAssetResults) {
            float weight = finalSnapshot.weights.count(symbol) ?
                finalSnapshot.weights.at(symbol) : 0.0f;
            result.returnContribution[symbol] = weight * btResult.totalReturn;
        }
    }
};
