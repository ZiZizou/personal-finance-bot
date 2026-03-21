#!/usr/bin/env python3
"""
ONNX Model Training Script for Trading Bot
Trains XGBoost/LightGBM models and exports to ONNX format for GPU inference.

Usage:
    python scripts/train_model.py --data data/historical.csv --output models/stock_predictor.onnx

Requirements:
    pip install xgboost lightgbm onnx onnxmltools scikit-learn pandas numpy
"""

import argparse
import os
import sys
import numpy as np
import pandas as pd
from datetime import datetime

# Check for required packages
try:
    import xgboost as xgb
    HAS_XGBOOST = True
except ImportError:
    HAS_XGBOOST = False

try:
    import lightgbm as lgb
    HAS_LIGHTGBM = True
except ImportError:
    HAS_LIGHTGBM = False

try:
    from skl2onnx import convert_sklearn
    from skl2onnx.common.data_types import FloatTensorType
    HAS_SKL2ONNX = True
except ImportError:
    HAS_SKL2ONNX = False

try:
    import onnx
    from onnx import helper
    HAS_ONNX = True
except ImportError:
    HAS_ONNX = False


def create_sample_data(n_samples: int = 1000) -> pd.DataFrame:
    """
    Create sample training data with realistic features.
    In production, replace with actual historical data.
    """
    np.random.seed(42)

    # Generate features
    data = {
        'rsi': np.random.uniform(20, 80, n_samples),
        'macd_hist': np.random.uniform(-0.5, 0.5, n_samples),
        'sentiment': np.random.uniform(-1, 1, n_samples),
        'garch_vol': np.random.uniform(0.01, 0.3, n_samples),
        'cycle_period': np.random.uniform(20, 60, n_samples),
        'lag_return_1': np.random.uniform(-0.05, 0.05, n_samples),
        'lag_return_2': np.random.uniform(-0.05, 0.05, n_samples),
        'lag_return_3': np.random.uniform(-0.05, 0.05, n_samples),
        'lag_return_5': np.random.uniform(-0.05, 0.05, n_samples),
        'lag_return_10': np.random.uniform(-0.05, 0.05, n_samples),
    }

    df = pd.DataFrame(data)

    # Create target: weighted combination of features + noise
    target = (
        -0.3 * (df['rsi'] - 50) / 50 +  # RSI mean reversion
        0.2 * df['macd_hist'] +            # MACD momentum
        0.1 * df['sentiment'] +            # Sentiment
        -0.2 * df['garch_vol'] +           # Volatility effect
        0.1 * df['lag_return_1'] +
        0.05 * df['lag_return_5'] +
        np.random.normal(0, 0.01, n_samples)  # Noise
    )

    df['target'] = target

    return df


def prepare_features(df: pd.DataFrame) -> tuple:
    """
    Prepare feature matrix matching ONNXPredictor's feature extraction.
    """
    feature_cols = [
        'rsi', 'macd_hist', 'sentiment', 'garch_vol',
        'lag_return_1', 'lag_return_2', 'lag_return_3',
        'lag_return_5', 'lag_return_10'
    ]

    # Add cross features (matching ONNXPredictor)
    X = df[feature_cols].copy()

    # Normalize RSI to 0-1
    X['rsi'] = X['rsi'] / 100.0

    # Clip MACD hist to -1 to 1
    X['macd_hist'] = X['macd_hist'].clip(-1, 1)

    # Normalize sentiment -1 to 1 -> 0 to 1
    X['sentiment'] = (X['sentiment'] + 1.0) / 2.0

    # Normalize volatility (typical range 0.01-0.5)
    X['garch_vol'] = X['garch_vol'].clip(0, 0.1)

    # Add cross features
    X['rsi_sentiment'] = X['rsi'] * X['sentiment']
    X['rsi_vol'] = X['rsi'] * X['garch_vol']
    X['macd_vol'] = X['macd_hist'] * X['garch_vol']
    X['lag1_lag2'] = X['lag_return_1'] * X['lag_return_2']
    X['sentiment_lag1'] = X['sentiment'] * X['lag_return_1']

    # Add cycle phase (cos)
    cycle_periods = df['cycle_period'].values
    day_indices = np.arange(len(df))
    X['cycle_cos'] = np.cos(2 * np.pi * day_indices / cycle_periods)

    y = df['target'].values

    return X.values.astype(np.float32), y.astype(np.float32)


def train_xgboost(X_train: np.ndarray, y_train: np.ndarray,
                  X_val: np.ndarray, y_val: np.ndarray) -> xgb.XGBRegressor:
    """Train XGBoost model."""
    if not HAS_XGBOOST:
        raise ImportError("XGBoost not installed. Run: pip install xgboost")

    model = xgb.XGBRegressor(
        n_estimators=100,
        max_depth=6,
        learning_rate=0.1,
        subsample=0.8,
        colsample_bytree=0.8,
        random_state=42,
        n_jobs=-1
    )

    model.fit(
        X_train, y_train,
        eval_set=[(X_val, y_val)],
        verbose=False
    )

    return model


def train_lightgbm(X_train: np.ndarray, y_train: np.ndarray,
                   X_val: np.ndarray, y_val: np.ndarray) -> lgb.LGBMRegressor:
    """Train LightGBM model."""
    if not HAS_LIGHTGBM:
        raise ImportError("LightGBM not installed. Run: pip install lightgbm")

    model = lgb.LGBMRegressor(
        n_estimators=100,
        max_depth=6,
        learning_rate=0.1,
        subsample=0.8,
        colsample_bytree=0.8,
        random_state=42,
        n_jobs=-1,
        verbose=-1
    )

    model.fit(
        X_train, y_train,
        eval_set=[(X_val, y_val)],
    )

    return model


def export_to_onnx(model, output_path: str, input_dim: int = 15) -> bool:
    """Export sklearn-compatible model to ONNX format."""
    if not HAS_SKL2ONNX:
        print("ERROR: skl2onnx not installed. Run: pip install skl2onnx")
        return False

    # Define input type
    initial_type = [('float_input', FloatTensorType([None, input_dim]))]

    # Convert to ONNX
    onnx_model = convert_sklearn(model, initial_types=initial_type)

    # Save model
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'wb') as f:
        f.write(onnx_model.SerializeToString())

    print(f"ONNX model saved to: {output_path}")
    return True


def export_xgboost_onnx(model, output_path: str, input_dim: int = 15) -> bool:
    """Export XGBoost model to ONNX format using onnxmltools."""
    try:
        from xgboost import booster

        # For XGBoost, we need to use a different approach
        # First convert to sklearn, then to ONNX
        from sklearn.pipeline import make_pipeline
        from sklearn.preprocessing import FunctionTransformer

        # Create a wrapper that converts numpy array properly
        class XGBWrapper:
            def __init__(self, model):
                self.model = model
                self._feature_names = [f"f{i}" for i in range(input_dim)]

            def predict(self, X):
                return self.model.predict(X)

        wrapper = XGBWrapper(model)
        return export_to_onnx(wrapper, output_path, input_dim)

    except Exception as e:
        print(f"Error exporting XGBoost to ONNX: {e}")
        return False


def create_simple_onnx_model(output_path: str, input_dim: int = 15) -> bool:
    """
    Create a simple ONNX model for testing.
    This is a fallback if sklearn conversion fails.
    """
    if not HAS_ONNX:
        print("ERROR: onnx not installed. Run: pip install onnx")
        return False

    # Create input
    input_tensor = helper.make_tensor_value_info(
        'float_input',
        1,  # float
        [input_dim]
    )

    # Create output
    output_tensor = helper.make_tensor_value_info(
        'variable',
        1,  # float
        [1]
    )

    # Create simple identity model (for testing)
    # In production, this would be replaced with actual trained model
    constant_weight = np.eye(input_dim, 1, dtype=np.float32).flatten()
    const_init = helper.make_tensor(
        'const_weight',
        1,
        [input_dim],
        constant_weight.tolist()
    )

    const_node = helper.make_node(
        'ConstantOfShape',
        inputs=['float_input'],
        outputs=['intermediate'],
        value=helper.make_tensor('value', 1, [1], [0.0])
    )

    # Simple matmul for identity
    matmul_node = helper.make_node(
        'MatMul',
        inputs=['float_input', 'const_weight'],
        outputs=['variable']
    )

    # Create graph
    graph = helper.make_graph(
        [matmul_node],
        'simple-model',
        [input_tensor],
        [output_tensor],
        [const_init]
    )

    # Create model
    model = helper.make_model(graph, producer_name='trading-bot')
    model.opset_import[0].version = 13

    # Save
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'wb') as f:
        f.write(model.SerializeToString())

    print(f"Simple ONNX model created at: {output_path}")
    return True


def main():
    parser = argparse.ArgumentParser(
        description='Train ML model and export to ONNX format'
    )
    parser.add_argument(
        '--data', '-d',
        type=str,
        help='Path to historical data CSV (optional, generates sample if not provided)'
    )
    parser.add_argument(
        '--output', '-o',
        type=str,
        default='models/stock_predictor.onnx',
        help='Output ONNX model path'
    )
    parser.add_argument(
        '--model', '-m',
        type=str,
        choices=['xgboost', 'lightgbm', 'auto'],
        default='auto',
        help='Model type to train'
    )
    parser.add_argument(
        '--test-only',
        action='store_true',
        help='Only create test ONNX model without training'
    )

    args = parser.parse_args()

    print("=" * 50)
    print("ONNX Model Training for Trading Bot")
    print("=" * 50)

    # Check dependencies
    print(f"\nChecking dependencies:")
    print(f"  XGBoost: {'OK' if HAS_XGBOOST else 'MISSING'}")
    print(f"  LightGBM: {'OK' if HAS_LIGHTGBM else 'MISSING'}")
    print(f"  skl2onnx: {'OK' if HAS_SKL2ONNX else 'MISSING'}")
    print(f"  onnx: {'OK' if HAS_ONNX else 'MISSING'}")

    if args.test_only:
        print("\nCreating test ONNX model...")
        create_simple_onnx_model(args.output)
        return 0

    # Load or generate data
    if args.data and os.path.exists(args.data):
        print(f"\nLoading data from: {args.data}")
        df = pd.read_csv(args.data)
    else:
        print("\nGenerating sample training data...")
        df = create_sample_data(2000)
        if args.data:
            os.makedirs(os.path.dirname(args.data), exist_ok=True)
            df.to_csv(args.data, index=False)
            print(f"Sample data saved to: {args.data}")

    # Prepare features
    print("\nPreparing features...")
    X, y = prepare_features(df)
    print(f"  Feature matrix shape: {X.shape}")
    print(f"  Target shape: {y.shape}")

    # Split data
    split_idx = int(len(X) * 0.8)
    X_train, X_val = X[:split_idx], X[split_idx:]
    y_train, y_val = y[:split_idx], y[split_idx:]

    # Train model
    print(f"\nTraining model (type: {args.model})...")

    if args.model == 'xgboost' or (args.model == 'auto' and HAS_XGBOOST):
        if not HAS_XGBOOST:
            print("XGBoost not available, trying LightGBM...")
            args.model = 'lightgbm'
        else:
            model = train_xgboost(X_train, y_train, X_val, y_val)
            print("  XGBoost model trained successfully")

            # Evaluate
            train_pred = model.predict(X_train)
            val_pred = model.predict(X_val)
            train_rmse = np.sqrt(np.mean((train_pred - y_train) ** 2))
            val_rmse = np.sqrt(np.mean((val_pred - y_val) ** 2))
            print(f"  Train RMSE: {train_rmse:.6f}")
            print(f"  Val RMSE: {val_rmse:.6f}")

            # Export
            if export_to_onnx(model, args.output, X.shape[1]):
                print(f"\nModel exported to: {args.output}")
                return 0
            else:
                print("Trying alternative export method...")
                if export_xgboost_onnx(model, args.output, X.shape[1]):
                    return 0

    if args.model == 'lightgbm' or (args.model == 'auto' and HAS_LIGHTGBM):
        if not HAS_LIGHTGBM:
            print("ERROR: Neither XGBoost nor LightGBM is available.")
            print("Please install at least one: pip install xgboost lightgbm")
            return 1

        model = train_lightgbm(X_train, y_train, X_val, y_val)
        print("  LightGBM model trained successfully")

        # Evaluate
        train_pred = model.predict(X_train)
        val_pred = model.predict(X_val)
        train_rmse = np.sqrt(np.mean((train_pred - y_train) ** 2))
        val_rmse = np.sqrt(np.mean((val_pred - y_val) ** 2))
        print(f"  Train RMSE: {train_rmse:.6f}")
        print(f"  Val RMSE: {val_rmse:.6f}")

        # Export
        if export_to_onnx(model, args.output, X.shape[1]):
            print(f"\nModel exported to: {args.output}")
            return 0

    # Fallback: create simple model
    print("\nFalling back to simple test model...")
    if create_simple_onnx_model(args.output):
        return 0

    print("ERROR: Failed to create ONNX model")
    return 1


if __name__ == '__main__':
    sys.exit(main())
