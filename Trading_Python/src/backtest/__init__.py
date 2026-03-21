"""
Backtesting module for strategy evaluation.
"""

import os
import json
import warnings
from datetime import datetime
from typing import Optional, List, Dict, Any, Callable
from dataclasses import dataclass, field
from enum import Enum

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns


class PositionSide(Enum):
    """Position direction."""
    LONG = 1
    SHORT = -1
    FLAT = 0


class SignalStrategy:
    """
    Base strategy that generates signals from model predictions.
    """

    def __init__(
        self,
        threshold_long: float = 0.001,
        threshold_short: float = -0.001
    ):
        self.threshold_long = threshold_long
        self.threshold_short = threshold_short

    def generate_signals(self, predictions: np.ndarray) -> np.ndarray:
        """Generate trading signals from predictions."""
        signals = np.zeros_like(predictions)

        signals[predictions > self.threshold_long] = PositionSide.LONG.value
        signals[predictions < self.threshold_short] = PositionSide.SHORT.value

        return signals


@dataclass
class BacktestConfig:
    """Configuration for backtesting."""
    # Capital
    initial_capital: float = 100000.0

    # Position sizing
    position_size: float = 1.0  # Fraction of capital per trade
    max_positions: int = 1

    # Costs
    commission: float = 0.001  # 0.1%
    slippage: float = 0.0005    # 0.05%

    # Risk management
    stop_loss: Optional[float] = None  # e.g., 0.02 = 2%
    take_profit: Optional[float] = None

    # Execution
    signals: Optional[np.ndarray] = None


@dataclass
class BacktestResult:
    """Results from backtesting."""
    equity_curve: pd.Series
    returns: pd.Series
    positions: pd.Series
    trades: pd.DataFrame

    # Metrics
    total_return: float
    sharpe_ratio: float
    sortino_ratio: float
    max_drawdown: float
    calmar_ratio: float
    win_rate: float
    profit_factor: float
    num_trades: int


class Backtester:
    """
    Comprehensive backtesting engine.
    """

    def __init__(self, config: Optional[BacktestConfig] = None):
        self.config = config or BacktestConfig()

    def run(
        self,
        data: pd.DataFrame,
        signals: Optional[np.ndarray] = None,
        predictions: Optional[np.ndarray] = None,
        strategy: Optional[SignalStrategy] = None
    ) -> BacktestResult:
        """
        Run backtest on price data.

        Args:
            data: DataFrame with price data (must have 'Close')
            signals: Pre-computed signals array
            predictions: Model predictions (used if signals not provided)
            strategy: Strategy to generate signals from predictions

        Returns:
            BacktestResult with equity curve and metrics
        """
        if 'Close' not in data.columns:
            raise ValueError("Data must contain 'Close' column")

        n = len(data)
        close = data['Close'].values

        # Generate signals if not provided
        if signals is None:
            if predictions is not None and strategy is not None:
                signals = strategy.generate_signals(predictions)
            else:
                raise ValueError("Must provide either signals or predictions+strategy")

        # Ensure signals is correct length
        signals = signals[:n]

        # Initialize tracking
        capital = self.config.initial_capital
        position = 0  # Current position (shares)
        position_value = 0

        equity = [capital]
        portfolio_returns = [0]
        positions = [0]

        trades = []

        # Track open position
        entry_price = 0
        entry_idx = 0

        for i in range(1, n):
            prev_position = position

            # Signal change - execute trade
            if signals[i] != signals[i-1] and position == 0:
                # Entry
                entry_price = close[i] * (1 + self.config.slippage)
                position_value = capital * self.config.position_size
                position = int(position_value / entry_price)

                entry_idx = i

            # Check stop loss / take profit
            if position > 0:
                pnl_pct = (close[i] - entry_price) / entry_price

                if self.config.stop_loss and pnl_pct < -self.config.stop_loss:
                    # Stop loss hit
                    exit_price = close[i] * (1 - self.config.slippage)
                    pnl = (exit_price - entry_price) * position
                    capital += pnl - (position * entry_price * self.config.commission)

                    trades.append({
                        'entry_date': data.index[entry_idx],
                        'exit_date': data.index[i],
                        'side': 'long',
                        'entry_price': entry_price,
                        'exit_price': exit_price,
                        'shares': position,
                        'pnl': pnl,
                        'return': pnl_pct
                    })

                    position = 0

                elif self.config.take_profit and pnl_pct > self.config.take_profit:
                    # Take profit hit
                    exit_price = close[i] * (1 - self.config.slippage)
                    pnl = (exit_price - entry_price) * position
                    capital += pnl - (position * entry_price * self.config.commission)

                    trades.append({
                        'entry_date': data.index[entry_idx],
                        'exit_date': data.index[i],
                        'side': 'long',
                        'entry_price': entry_price,
                        'exit_price': exit_price,
                        'shares': position,
                        'pnl': pnl,
                        'return': pnl_pct
                    })

                    position = 0

            # Update equity
            if position > 0:
                current_value = position * close[i]
            else:
                current_value = capital

            equity.append(current_value)

            if equity[-2] > 0:
                ret = (equity[-1] - equity[-2]) / equity[-2]
            else:
                ret = 0

            portfolio_returns.append(ret)
            positions.append(position)

        # Create results
        equity_curve = pd.Series(equity, index=data.index[:n+1])
        returns_series = pd.Series(portfolio_returns[1:], index=data.index[1:n+1])
        positions_series = pd.Series(positions[1:], index=data.index[1:n+1])
        trades_df = pd.DataFrame(trades) if trades else pd.DataFrame()

        # Calculate metrics
        metrics = self._calculate_metrics(equity_curve, returns_series, trades_df)

        return BacktestResult(
            equity_curve=equity_curve,
            returns=returns_series,
            positions=positions_series,
            trades=trades_df,
            **metrics
        )

    def _calculate_metrics(
        self,
        equity: pd.Series,
        returns: pd.Series,
        trades: pd.DataFrame
    ) -> Dict[str, float]:
        """Calculate performance metrics."""
        # Total return
        total_return = (equity.iloc[-1] - equity.iloc[0]) / equity.iloc[0]

        # Annualized metrics
        n_days = len(returns)
        n_years = n_days / 252
        annualized_return = (1 + total_return) ** (1 / n_years) - 1

        # Sharpe ratio
        if returns.std() > 0:
            sharpe_ratio = (returns.mean() / returns.std()) * np.sqrt(252)
        else:
            sharpe_ratio = 0

        # Sortino ratio
        downside = returns[returns < 0]
        if len(downside) > 0 and downside.std() > 0:
            sortino_ratio = (returns.mean() / downside.std()) * np.sqrt(252)
        else:
            sortino_ratio = 0

        # Max drawdown
        cumulative = (1 + returns).cumprod()
        running_max = cumulative.expanding().max()
        drawdown = (cumulative - running_max) / running_max
        max_drawdown = drawdown.min()

        # Calmar ratio
        if max_drawdown != 0:
            calmar_ratio = annualized_return / abs(max_drawdown)
        else:
            calmar_ratio = 0

        # Trade metrics
        if len(trades) > 0:
            num_trades = len(trades)
            winning_trades = trades[trades['pnl'] > 0]
            win_rate = len(winning_trades) / num_trades

            if len(trades[trades['pnl'] < 0]) > 0:
                avg_loss = trades[trades['pnl'] < 0]['pnl'].mean()
                avg_win = winning_trades['pnl'].mean()
                profit_factor = abs(avg_win / avg_loss) if avg_loss != 0 else 0
            else:
                profit_factor = 0
        else:
            num_trades = 0
            win_rate = 0
            profit_factor = 0

        return {
            'total_return': total_return,
            'sharpe_ratio': sharpe_ratio,
            'sortino_ratio': sortino_ratio,
            'max_drawdown': max_drawdown,
            'calmar_ratio': calmar_ratio,
            'win_rate': win_rate,
            'profit_factor': profit_factor,
            'num_trades': num_trades
        }

    def print_summary(self, result: BacktestResult):
        """Print backtest summary."""
        print("\n" + "="*60)
        print("BACKTEST RESULTS")
        print("="*60)
        print(f"Total Return:     {result.total_return*100:.2f}%")
        print(f"Sharpe Ratio:    {result.sharpe_ratio:.3f}")
        print(f"Sortino Ratio:   {result.sortino_ratio:.3f}")
        print(f"Max Drawdown:    {result.max_drawdown*100:.2f}%")
        print(f"Calmar Ratio:    {result.calmar_ratio:.3f}")
        print(f"Win Rate:        {result.win_rate*100:.1f}%")
        print(f"Profit Factor:   {result.profit_factor:.3f}")
        print(f"Total Trades:    {result.num_trades}")
        print("="*60)

    def plot_results(self, result: BacktestResult, save_path: Optional[str] = None):
        """Plot backtest results."""
        fig, axes = plt.subplots(3, 1, figsize=(12, 10))

        # Equity curve
        axes[0].plot(result.equity_curve.index, result.equity_curve.values)
        axes[0].set_title('Equity Curve')
        axes[0].set_ylabel('Portfolio Value ($)')
        axes[0].grid(True)

        # Returns
        axes[1].bar(result.returns.index, result.returns.values, alpha=0.7)
        axes[1].set_title('Daily Returns')
        axes[1].set_ylabel('Return')
        axes[1].axhline(y=0, color='k', linestyle='-', linewidth=0.5)
        axes[1].grid(True)

        # Drawdown
        cumulative = (1 + result.returns).cumprod()
        running_max = cumulative.expanding().max()
        drawdown = (cumulative - running_max) / running_max

        axes[2].fill_between(drawdown.index, drawdown.values, 0, alpha=0.7, color='red')
        axes[2].set_title('Drawdown')
        axes[2].set_ylabel('Drawdown')
        axes[2].grid(True)

        plt.tight_layout()

        if save_path:
            plt.savefig(save_path)
        else:
            plt.show()


def walk_forward_validation(
    data: pd.DataFrame,
    train_func: Callable,
    n_splits: int = 5,
    train_window: int = 252,
    test_window: int = 63
) -> List[Dict[str, Any]]:
    """
    Perform walk-forward validation.

    Args:
        data: Feature DataFrame
        train_func: Function that takes (train_X, train_y) and returns trained model
        n_splits: Number of walk-forward splits
        train_window: Training window in days
        test_window: Test window in days

    Returns:
        List of results for each split
    """
    results = []
    n = len(data)

    for i in range(n_splits):
        train_end = train_window + i * test_window
        test_end = min(train_end + test_window, n)

        if test_end > n:
            break

        train_data = data.iloc[:train_end]
        test_data = data.iloc[train_end:test_end]

        print(f"Split {i+1}: Train [{0}:{train_end}], Test [{train_end}:{test_end}]")

        # Train
        model = train_func(train_data)

        # Test
        test_results = {
            'split': i,
            'train_size': len(train_data),
            'test_size': len(test_data)
        }

        results.append(test_results)

    return results


if __name__ == "__main__":
    import yfinance as yf
    from src.features import FeatureEngine

    # Fetch data
    data = yf.download("AAPL", start="2022-01-01", end="2024-01-01")

    # Add features
    engine = FeatureEngine()
    features = engine.add_all_indicators(data)

    # Simple strategy: predict RSI crossover
    features['signal'] = 0
    features.loc[features['rsi'] < 30, 'signal'] = 1   # Long when oversold
    features.loc[features['rsi'] > 70, 'signal'] = -1  # Short when overbought

    # Run backtest
    config = BacktestConfig(
        initial_capital=100000,
        stop_loss=0.05,
        take_profit=0.10
    )

    backtester = Backtester(config)
    result = backtester.run(
        features.dropna(),
        signals=features['signal'].values
    )

    backtester.print_summary(result)
