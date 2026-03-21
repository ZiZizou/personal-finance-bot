"""
Training script for trading models.
Usage: python scripts/train_model.py --ticker AAPL --start 2020-01-01 --end 2024-01-01
"""

import argparse
import os
import sys
from datetime import datetime

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import numpy as np
import pandas as pd
import yfinance as yf
from tqdm import tqdm

from src.data import fetch_yahoo_data
from src.features import FeatureEngine
from src.models import ModelTrainer, ModelConfig, ONNXExporter, train_and_export
from src.backtest import Backtester, BacktestConfig, SignalStrategy


def parse_args():
    parser = argparse.ArgumentParser(description="Train trading model")
    parser.add_argument("--ticker", type=str, default="AAPL", help="Stock ticker")
    parser.add_argument("--start", type=str, default="2020-01-01", help="Start date")
    parser.add_argument("--end", type=str, default="2024-01-01", help="End date")
    parser.add_argument("--model", type=str, default="ridge", choices=["ridge", "rf", "gb"])
    parser.add_argument("--output", type=str, default="./models", help="Output directory")
    parser.add_argument("--target", type=str, default="return_1d", help="Target column")
    return parser.parse_args()


def main():
    args = parse_args()

    print(f"Training model for {args.ticker}")
    print(f"Period: {args.start} to {args.end}")

    # Create output directory
    os.makedirs(args.output, exist_ok=True)

    # Fetch data
    print("\nFetching data...")
    data = fetch_yahoo_data(args.ticker, args.start, args.end)
    print(f"Loaded {len(data)} rows")

    # Add features
    print("\nEngineering features...")
    engine = FeatureEngine()
    features = engine.add_all_indicators(data)

    # Create target
    features['return_1d'] = features['Close'].pct_change(1)
    features['return_5d'] = features['Close'].pct_change(5)

    # Binary target for classification
    features['signal'] = 0
    features.loc[features['return_1d'] > 0.001, 'signal'] = 1
    features.loc[features['return_1d'] < -0.001, 'signal'] = -1

    # Drop NaN
    features = features.dropna()
    print(f"After feature engineering: {len(features)} rows, {len(features.columns)} columns")

    # Select target
    if args.target == "return_1d":
        target_col = "return_1d"
    elif args.target == "signal":
        target_col = "signal"
    else:
        target_col = args.target

    # Train and export
    print(f"\nTraining {args.model} model...")
    trainer, onnx_path = train_and_export(
        features,
        target_col=target_col,
        model_type=args.model,
        output_dir=args.output,
        model_name=f"{args.ticker}_{args.model}"
    )

    # Run backtest with model predictions
    print("\nRunning backtest...")

    # Get test data
    X, y = trainer.prepare_data(features, target_col)
    _, _, _, _, X_test, _ = trainer.split_data(X, y)

    # Make predictions
    predictions = trainer.predict(X_test)

    # Run backtest
    config = BacktestConfig(
        initial_capital=100000,
        stop_loss=0.05,
        take_profit=0.10
    )

    strategy = SignalStrategy(threshold_long=0, threshold_short=0)

    backtester = Backtester(config)
    test_data = features.iloc[-len(X_test):]

    result = backtester.run(
        test_data,
        predictions=predictions,
        strategy=strategy
    )

    backtester.print_summary(result)

    # Save summary
    summary_path = os.path.join(args.output, f"{args.ticker}_{args.model}_summary.json")

    import json
    summary = {
        'ticker': args.ticker,
        'model': args.model,
        'start_date': args.start,
        'end_date': args.end,
        'total_return': result.total_return,
        'sharpe_ratio': result.sharpe_ratio,
        'max_drawdown': result.max_drawdown,
        'num_trades': result.num_trades,
        'win_rate': result.win_rate,
        'onnx_path': onnx_path
    }

    with open(summary_path, 'w') as f:
        json.dump(summary, f, indent=2)

    print(f"\nSummary saved to: {summary_path}")
    print(f"ONNX model: {onnx_path}")

    # Instructions for C++ integration
    print("\n" + "="*60)
    print("INTEGRATION WITH C++")
    print("="*60)
    print(f"1. Copy {onnx_path} to your C++ project")
    print(f"2. Use your existing ONNXPredictor to load it")
    print(f"3. Feature names: {trainer.feature_names[:5]}... (total: {len(trainer.feature_names)})")
    print("="*60)


if __name__ == "__main__":
    main()
