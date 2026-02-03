#include "Backtester.h"
#include "TechnicalAnalysis.h"
#include <iostream>
#include <cmath>
#include <numeric>
#include <algorithm>

// === LEGACY HELPER (for backward compatibility) ===
// Simulates the original strategy decision logic
static int simulateDecision(const std::vector<Candle>& history) {
    if (history.size() < 50) return 0;

    std::vector<float> closes;
    for (const auto& c : history) closes.push_back(c.close);

    BollingerBands bb = computeBollingerBands(closes, 20, 2.0f);
    float rsi = computeRSI(closes, 14);
    float current = closes.back();

    // Mean Reversion Strategy
    if (rsi < 30.0f || current < bb.lower) return 1;   // Buy
    if (rsi > 70.0f || current > bb.upper) return -1;  // Sell

    return 0;  // Hold
}

// === CONSTRUCTOR ===
Backtester::Backtester(const BacktestConfig& config) : config_(config) {}

// === STATIC METHOD FOR BACKWARD COMPATIBILITY ===
BacktestResult Backtester::run(const std::vector<Candle>& candles) {
    BacktestResult res;
    if (candles.size() < 100) return res;

    float cash = 10000.0f;
    float holdings = 0.0f;
    float initialBalance = cash;
    float entryPrice = 0.0f;

    std::vector<float> dailyBalances;
    float peakBalance = initialBalance;

    for (size_t i = 60; i < candles.size(); ++i) {
        std::vector<Candle> history(candles.begin(), candles.begin() + i + 1);
        float price = candles[i].close;

        int action = simulateDecision(history);

        if (action == 1 && cash > 0) {
            holdings = cash / price;
            cash = 0;
            entryPrice = price;
            res.trades++;
        } else if (action == -1 && holdings > 0) {
            float proceeds = holdings * price;
            if (price > entryPrice) res.wins++;
            cash = proceeds;
            holdings = 0;
            res.trades++;
        }

        float currentBalance = cash + (holdings * price);
        dailyBalances.push_back(currentBalance);

        if (currentBalance > peakBalance) peakBalance = currentBalance;
        float drawdown = (peakBalance - currentBalance) / peakBalance;
        if (drawdown > res.maxDrawdown) res.maxDrawdown = drawdown;
    }

    float finalBalance = cash + (holdings * candles.back().close);
    res.totalReturn = (finalBalance - initialBalance) / initialBalance;

    // Compute Sharpe (using sample std dev)
    if (dailyBalances.size() > 1) {
        std::vector<float> returns;
        for (size_t i = 1; i < dailyBalances.size(); ++i) {
            if (dailyBalances[i - 1] > 0)
                returns.push_back((dailyBalances[i] - dailyBalances[i - 1]) / dailyBalances[i - 1]);
        }

        if (!returns.empty()) {
            float meanRet = std::accumulate(returns.begin(), returns.end(), 0.0f) / returns.size();
            float sqSum = 0.0f;
            for (float r : returns) sqSum += (r - meanRet) * (r - meanRet);
            float stdDev = std::sqrt(sqSum / (returns.size() - 1));  // Sample std dev

            if (stdDev > 1e-9)
                res.sharpeRatio = (meanRet / stdDev) * std::sqrt(252.0f);
        }
    }

    // Calculate win rate for compatibility
    res.winRate = res.trades > 0 ? (float)res.wins / res.trades : 0.0f;

    return res;
}

// === INSTANCE METHOD: Run with Strategy ===
BacktestResult Backtester::run(const std::vector<Candle>& candles, IStrategy& strategy) {
    int warmup = strategy.getWarmupPeriod();
    return run(candles, [&strategy](const std::vector<Candle>& h, size_t idx) {
        return strategy.generateSignal(h, idx);
    }, warmup);
}

// === INSTANCE METHOD: Run with Signal Function ===
BacktestResult Backtester::run(const std::vector<Candle>& candles, SignalFunction signalFunc, int warmupPeriod) {
    BacktestResult result;

    if ((int)candles.size() < warmupPeriod + 10) {
        return result;
    }

    // Initialize state
    float cash = config_.initialCapital;
    float equity = cash;
    float peakEquity = cash;
    Position position;
    int barsSinceLastTrade = config_.minBarsBetweenTrades;
    int consecutiveWins = 0;
    int consecutiveLosses = 0;
    float grossProfit = 0.0f;
    float grossLoss = 0.0f;
    int drawdownStartBar = -1;
    float longestDrawdown = 0.0f;

    result.equityCurve.reserve(candles.size());
    result.dailyReturns.reserve(candles.size());
    result.drawdownCurve.reserve(candles.size());

    // Main backtest loop
    for (size_t i = warmupPeriod; i < candles.size(); ++i) {
        const Candle& candle = candles[i];
        float prevEquity = equity;

        // Update trailing stop if enabled and position is open
        if (position.isOpen && config_.risk.enableTrailingStop) {
            updateTrailingStop(position, candle);
        }

        // Check stop-loss
        float stopExitPrice = 0.0f;
        if (position.isOpen && config_.risk.enableStopLoss) {
            if (checkStopLoss(position, candle, stopExitPrice)) {
                TradeRecord trade = closePosition(position, candle, i, "stop_loss");
                trade.exitPrice = stopExitPrice;
                result.tradeLog.push_back(trade);
                cash += trade.pnl + (trade.entryPrice * trade.quantity);

                // Update statistics
                if (trade.isWin()) {
                    result.wins++;
                    consecutiveWins++;
                    consecutiveLosses = 0;
                    grossProfit += trade.pnl;
                    if (trade.side == "long") result.winningLongTrades++;
                    else result.winningShortTrades++;
                } else {
                    consecutiveWins = 0;
                    consecutiveLosses++;
                    grossLoss += std::abs(trade.pnl);
                }

                result.maxConsecutiveWins = std::max(result.maxConsecutiveWins, consecutiveWins);
                result.maxConsecutiveLosses = std::max(result.maxConsecutiveLosses, consecutiveLosses);
                result.largestWin = std::max(result.largestWin, trade.pnl);
                result.largestLoss = std::min(result.largestLoss, trade.pnl);
                result.totalCommissions += trade.transactionCost;
                result.totalSlippage += trade.slippage;

                position.isOpen = false;
                barsSinceLastTrade = 0;
            }
        }

        // Check take-profit
        float tpExitPrice = 0.0f;
        if (position.isOpen && config_.risk.enableTakeProfit) {
            if (checkTakeProfit(position, candle, tpExitPrice)) {
                TradeRecord trade = closePosition(position, candle, i, "take_profit");
                trade.exitPrice = tpExitPrice;
                result.tradeLog.push_back(trade);
                cash += trade.pnl + (trade.entryPrice * trade.quantity);

                result.wins++;
                consecutiveWins++;
                consecutiveLosses = 0;
                grossProfit += trade.pnl;
                if (trade.side == "long") result.winningLongTrades++;
                else result.winningShortTrades++;

                result.maxConsecutiveWins = std::max(result.maxConsecutiveWins, consecutiveWins);
                result.largestWin = std::max(result.largestWin, trade.pnl);
                result.totalCommissions += trade.transactionCost;
                result.totalSlippage += trade.slippage;

                position.isOpen = false;
                barsSinceLastTrade = 0;
            }
        }

        // Generate signal if no position or checking for exits
        std::vector<Candle> history(candles.begin(), candles.begin() + i + 1);
        StrategySignal signal = signalFunc(history, i);

        // Process signal
        if (!position.isOpen && barsSinceLastTrade >= config_.minBarsBetweenTrades) {
            // Open new position
            if (signal.type == SignalType::Buy && cash > 0) {
                openPosition(position, candle, i, cash, signal, true);
                cash -= position.quantity * position.entryPrice + position.entryCost;
                result.longTrades++;
                result.trades++;
            } else if (signal.type == SignalType::Sell && config_.allowShort && cash > 0) {
                openPosition(position, candle, i, cash, signal, false);
                cash -= position.entryCost;  // For short, we receive proceeds later
                result.shortTrades++;
                result.trades++;
            }
        } else if (position.isOpen) {
            // Check for signal-based exit
            bool shouldClose = false;
            if (position.isLong && signal.type == SignalType::Sell) {
                shouldClose = true;
            } else if (!position.isLong && signal.type == SignalType::Buy) {
                shouldClose = true;
            }

            if (shouldClose) {
                TradeRecord trade = closePosition(position, candle, i, "signal");
                result.tradeLog.push_back(trade);
                cash += trade.pnl + (trade.entryPrice * trade.quantity);

                if (trade.isWin()) {
                    result.wins++;
                    consecutiveWins++;
                    consecutiveLosses = 0;
                    grossProfit += trade.pnl;
                    if (trade.side == "long") result.winningLongTrades++;
                    else result.winningShortTrades++;
                } else {
                    consecutiveWins = 0;
                    consecutiveLosses++;
                    grossLoss += std::abs(trade.pnl);
                }

                result.maxConsecutiveWins = std::max(result.maxConsecutiveWins, consecutiveWins);
                result.maxConsecutiveLosses = std::max(result.maxConsecutiveLosses, consecutiveLosses);
                result.largestWin = std::max(result.largestWin, trade.pnl);
                result.largestLoss = std::min(result.largestLoss, trade.pnl);
                result.totalCommissions += trade.transactionCost;
                result.totalSlippage += trade.slippage;

                position.isOpen = false;
                barsSinceLastTrade = 0;

                // Immediately open opposite position if signal warrants
                if (signal.type == SignalType::Buy && cash > 0) {
                    openPosition(position, candle, i, cash, signal, true);
                    cash -= position.quantity * position.entryPrice + position.entryCost;
                    result.longTrades++;
                    result.trades++;
                } else if (signal.type == SignalType::Sell && config_.allowShort && cash > 0) {
                    openPosition(position, candle, i, cash, signal, false);
                    cash -= position.entryCost;
                    result.shortTrades++;
                    result.trades++;
                }
            }
        }

        // Calculate current equity
        if (position.isOpen) {
            if (position.isLong) {
                equity = cash + position.quantity * candle.close;
            } else {
                // For short: profit = entry - current
                float shortPnL = (position.entryPrice - candle.close) * position.quantity;
                equity = cash + shortPnL;
            }
        } else {
            equity = cash;
        }

        // Track equity curve and returns
        result.equityCurve.push_back(equity);

        if (prevEquity > 0) {
            float dailyReturn = (equity - prevEquity) / prevEquity;
            result.dailyReturns.push_back(dailyReturn);
        }

        // Track drawdown
        if (equity > peakEquity) {
            peakEquity = equity;
            drawdownStartBar = -1;
        }
        float drawdown = (peakEquity - equity) / peakEquity;
        result.drawdownCurve.push_back(drawdown);

        if (drawdown > result.maxDrawdown) {
            result.maxDrawdown = drawdown;
        }

        if (drawdown > 0 && drawdownStartBar < 0) {
            drawdownStartBar = (int)i;
        } else if (drawdown == 0 && drawdownStartBar >= 0) {
            longestDrawdown = std::max(longestDrawdown, (float)(i - drawdownStartBar));
            drawdownStartBar = -1;
        }

        // Check max drawdown circuit breaker
        if (config_.risk.enableMaxDrawdownStop && drawdown >= config_.risk.maxDrawdownPercent) {
            // Close position and stop trading
            if (position.isOpen) {
                TradeRecord trade = closePosition(position, candle, i, "max_drawdown");
                result.tradeLog.push_back(trade);
                cash += trade.pnl + (trade.entryPrice * trade.quantity);
                position.isOpen = false;
            }
            break;  // Stop backtesting
        }

        barsSinceLastTrade++;
    }

    // Close any open position at end
    if (position.isOpen) {
        TradeRecord trade = closePosition(position, candles.back(), candles.size() - 1, "end_of_data");
        result.tradeLog.push_back(trade);
        cash += trade.pnl + (trade.entryPrice * trade.quantity);

        if (trade.isWin()) {
            result.wins++;
            grossProfit += trade.pnl;
        } else {
            grossLoss += std::abs(trade.pnl);
        }

        result.totalCommissions += trade.transactionCost;
        result.totalSlippage += trade.slippage;
    }

    // Calculate final equity
    equity = cash;

    // Calculate all metrics
    result.totalReturn = (equity - config_.initialCapital) / config_.initialCapital;
    result.totalCosts = result.totalCommissions + result.totalSlippage;

    // Trade statistics
    if (result.trades > 0) {
        result.winRate = (float)result.wins / result.trades;

        // Average win/loss
        int losses = result.trades - result.wins;
        if (result.wins > 0) result.avgWin = grossProfit / result.wins;
        if (losses > 0) result.avgLoss = grossLoss / losses;

        // Profit factor
        if (grossLoss > 0) result.profitFactor = grossProfit / grossLoss;

        // Expectancy
        result.expectancy = (result.winRate * result.avgWin) - ((1 - result.winRate) * result.avgLoss);

        // Average holding period
        float totalHolding = 0;
        for (const auto& trade : result.tradeLog) {
            totalHolding += trade.holdingPeriod;
        }
        result.avgHoldingPeriod = totalHolding / result.tradeLog.size();
    }

    // Calculate risk metrics
    calculateMetrics(result, result.dailyReturns, (int)(candles.size() - warmupPeriod));
    result.maxDrawdownDuration = longestDrawdown;

    return result;
}

// === POSITION MANAGEMENT ===
void Backtester::openPosition(Position& pos, const Candle& candle, size_t idx,
                              float capitalAvailable, const StrategySignal& signal, bool isLong) {
    pos.isOpen = true;
    pos.isLong = isLong;
    pos.entryIndex = idx;
    pos.entryDate = candle.date;

    // Apply slippage to entry price
    float rawPrice = candle.close;
    pos.entryPrice = config_.costs.applySlippage(rawPrice, isLong);
    pos.entrySlippage = std::abs(pos.entryPrice - rawPrice);

    // Calculate position size
    float fraction = config_.sizing.calculateFraction();
    float positionValue = capitalAvailable * fraction;

    // Calculate transaction cost
    pos.entryCost = config_.costs.calculateCost(positionValue);

    // Adjust position value for cost
    positionValue -= pos.entryCost;
    pos.quantity = positionValue / pos.entryPrice;

    // Set stop-loss
    if (config_.risk.enableStopLoss) {
        if (isLong) {
            pos.stopLossPrice = pos.entryPrice * (1.0f - config_.risk.stopLossPercent);
        } else {
            pos.stopLossPrice = pos.entryPrice * (1.0f + config_.risk.stopLossPercent);
        }
    }

    // Override with signal's stop-loss if provided
    if (signal.stopLossPrice > 0) {
        pos.stopLossPrice = signal.stopLossPrice;
    }

    // Set take-profit
    if (config_.risk.enableTakeProfit) {
        if (isLong) {
            pos.takeProfitPrice = pos.entryPrice * (1.0f + config_.risk.takeProfitPercent);
        } else {
            pos.takeProfitPrice = pos.entryPrice * (1.0f - config_.risk.takeProfitPercent);
        }
    }

    // Override with signal's take-profit if provided
    if (signal.takeProfitPrice > 0) {
        pos.takeProfitPrice = signal.takeProfitPrice;
    }

    // Initialize trailing stop tracking
    pos.highWaterMark = candle.high;
    pos.lowWaterMark = candle.low;
}

TradeRecord Backtester::closePosition(Position& pos, const Candle& candle, size_t idx,
                                       const std::string& reason) {
    TradeRecord trade;
    trade.entryIndex = pos.entryIndex;
    trade.exitIndex = idx;
    trade.entryDate = pos.entryDate;
    trade.exitDate = candle.date;
    trade.entryPrice = pos.entryPrice;
    trade.quantity = pos.quantity;
    trade.side = pos.isLong ? "long" : "short";
    trade.exitReason = reason;
    trade.holdingPeriod = (int)(idx - pos.entryIndex);

    // Apply slippage to exit
    float rawExitPrice = candle.close;
    trade.exitPrice = config_.costs.applySlippage(rawExitPrice, !pos.isLong);  // Opposite of entry
    float exitSlippage = std::abs(trade.exitPrice - rawExitPrice);

    // Calculate exit cost
    float exitValue = trade.exitPrice * trade.quantity;
    float exitCost = config_.costs.calculateCost(exitValue);

    // Calculate P&L
    if (pos.isLong) {
        trade.pnl = (trade.exitPrice - trade.entryPrice) * trade.quantity - pos.entryCost - exitCost;
    } else {
        trade.pnl = (trade.entryPrice - trade.exitPrice) * trade.quantity - pos.entryCost - exitCost;
    }

    trade.pnlPercent = trade.entryPrice > 0 ? trade.pnl / (trade.entryPrice * trade.quantity) : 0;
    trade.transactionCost = pos.entryCost + exitCost;
    trade.slippage = pos.entrySlippage + exitSlippage;

    return trade;
}

bool Backtester::checkStopLoss(const Position& pos, const Candle& candle, float& exitPrice) const {
    if (pos.stopLossPrice <= 0) return false;

    if (pos.isLong) {
        if (candle.low <= pos.stopLossPrice) {
            exitPrice = pos.stopLossPrice;  // Assume execution at stop price
            return true;
        }
    } else {
        if (candle.high >= pos.stopLossPrice) {
            exitPrice = pos.stopLossPrice;
            return true;
        }
    }
    return false;
}

bool Backtester::checkTakeProfit(const Position& pos, const Candle& candle, float& exitPrice) const {
    if (pos.takeProfitPrice <= 0) return false;

    if (pos.isLong) {
        if (candle.high >= pos.takeProfitPrice) {
            exitPrice = pos.takeProfitPrice;
            return true;
        }
    } else {
        if (candle.low <= pos.takeProfitPrice) {
            exitPrice = pos.takeProfitPrice;
            return true;
        }
    }
    return false;
}

void Backtester::updateTrailingStop(Position& pos, const Candle& candle) {
    if (pos.isLong) {
        // Update high water mark
        if (candle.high > pos.highWaterMark) {
            pos.highWaterMark = candle.high;
            // Update trailing stop
            float newStop = pos.highWaterMark * (1.0f - config_.risk.trailingStopPercent);
            if (newStop > pos.stopLossPrice) {
                pos.stopLossPrice = newStop;
            }
        }
    } else {
        // Update low water mark for short
        if (candle.low < pos.lowWaterMark) {
            pos.lowWaterMark = candle.low;
            float newStop = pos.lowWaterMark * (1.0f + config_.risk.trailingStopPercent);
            if (newStop < pos.stopLossPrice || pos.stopLossPrice <= 0) {
                pos.stopLossPrice = newStop;
            }
        }
    }
}

// === METRIC CALCULATIONS ===
void Backtester::calculateMetrics(BacktestResult& result, const std::vector<float>& dailyReturns,
                                   int totalBars) const {
    if (dailyReturns.empty()) return;

    // Sharpe Ratio
    result.sharpeRatio = calculateSharpeRatio(dailyReturns);

    // Sortino Ratio
    result.sortinoRatio = calculateSortinoRatio(dailyReturns);

    // Annualized Return (CAGR)
    float years = (float)totalBars / config_.tradingDaysPerYear;
    if (years > 0 && result.totalReturn > -1.0f) {
        result.annualizedReturn = std::pow(1.0f + result.totalReturn, 1.0f / years) - 1.0f;
    }

    // Calmar Ratio
    if (result.maxDrawdown > 0) {
        result.calmarRatio = result.annualizedReturn / result.maxDrawdown;
    }

    // Volatility
    float meanRet = std::accumulate(dailyReturns.begin(), dailyReturns.end(), 0.0f) / dailyReturns.size();
    float variance = 0.0f;
    for (float r : dailyReturns) {
        variance += (r - meanRet) * (r - meanRet);
    }
    if (dailyReturns.size() > 1) {
        variance /= (dailyReturns.size() - 1);
    }
    result.volatility = std::sqrt(variance) * std::sqrt((float)config_.tradingDaysPerYear);

    // Downside deviation
    float sumDownside = 0.0f;
    int countDownside = 0;
    float dailyRiskFree = config_.riskFreeRate / config_.tradingDaysPerYear;
    for (float r : dailyReturns) {
        if (r < dailyRiskFree) {
            sumDownside += (r - dailyRiskFree) * (r - dailyRiskFree);
            countDownside++;
        }
    }
    if (countDownside > 1) {
        result.downsideDeviation = std::sqrt(sumDownside / (countDownside - 1)) *
                                   std::sqrt((float)config_.tradingDaysPerYear);
    }

    // VaR and CVaR
    result.valueAtRisk95 = calculateVaR(dailyReturns, 0.95f);
    result.cvar95 = calculateCVaR(dailyReturns, 0.95f);

    // Cost impact (compare return with vs without costs)
    if (result.totalCosts > 0 && config_.initialCapital > 0) {
        result.costImpact = result.totalCosts / config_.initialCapital;
    }
}

float Backtester::calculateSharpeRatio(const std::vector<float>& returns) const {
    if (returns.size() < 2) return 0.0f;

    float dailyRiskFree = config_.riskFreeRate / config_.tradingDaysPerYear;
    float meanRet = std::accumulate(returns.begin(), returns.end(), 0.0f) / returns.size();
    float excessReturn = meanRet - dailyRiskFree;

    float variance = 0.0f;
    for (float r : returns) {
        variance += (r - meanRet) * (r - meanRet);
    }
    variance /= (returns.size() - 1);  // Sample variance

    float stdDev = std::sqrt(variance);
    if (stdDev < 1e-9f) return 0.0f;

    return (excessReturn / stdDev) * std::sqrt((float)config_.tradingDaysPerYear);
}

float Backtester::calculateSortinoRatio(const std::vector<float>& returns) const {
    if (returns.size() < 2) return 0.0f;

    float dailyRiskFree = config_.riskFreeRate / config_.tradingDaysPerYear;
    float meanRet = std::accumulate(returns.begin(), returns.end(), 0.0f) / returns.size();
    float excessReturn = meanRet - dailyRiskFree;

    // Downside deviation (only negative returns)
    float sumSquaredDownside = 0.0f;
    int countDownside = 0;
    for (float r : returns) {
        if (r < dailyRiskFree) {
            sumSquaredDownside += (r - dailyRiskFree) * (r - dailyRiskFree);
            countDownside++;
        }
    }

    if (countDownside < 2) return 0.0f;

    float downsideStd = std::sqrt(sumSquaredDownside / (countDownside - 1));
    if (downsideStd < 1e-9f) return 0.0f;

    return (excessReturn / downsideStd) * std::sqrt((float)config_.tradingDaysPerYear);
}

float Backtester::calculateMaxDrawdown(const std::vector<float>& equityCurve, float& maxDuration) const {
    if (equityCurve.empty()) return 0.0f;

    float maxDD = 0.0f;
    float peak = equityCurve[0];
    maxDuration = 0;
    int drawdownStart = -1;

    for (size_t i = 0; i < equityCurve.size(); ++i) {
        if (equityCurve[i] > peak) {
            peak = equityCurve[i];
            if (drawdownStart >= 0) {
                maxDuration = std::max(maxDuration, (float)(i - drawdownStart));
                drawdownStart = -1;
            }
        }

        float dd = (peak - equityCurve[i]) / peak;
        if (dd > maxDD) {
            maxDD = dd;
        }

        if (dd > 0 && drawdownStart < 0) {
            drawdownStart = (int)i;
        }
    }

    return maxDD;
}

float Backtester::calculateVaR(std::vector<float> returns, float confidence) const {
    if (returns.empty()) return 0.0f;

    std::sort(returns.begin(), returns.end());
    size_t index = (size_t)((1.0f - confidence) * returns.size());
    if (index >= returns.size()) index = returns.size() - 1;

    return -returns[index];  // Return as positive number (loss)
}

float Backtester::calculateCVaR(std::vector<float> returns, float confidence) const {
    if (returns.empty()) return 0.0f;

    std::sort(returns.begin(), returns.end());
    size_t cutoff = (size_t)((1.0f - confidence) * returns.size());
    if (cutoff == 0) cutoff = 1;

    float sum = 0.0f;
    for (size_t i = 0; i < cutoff; ++i) {
        sum += returns[i];
    }

    return -sum / cutoff;  // Return as positive number (expected loss)
}
