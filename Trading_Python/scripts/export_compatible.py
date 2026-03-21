"""
Export ONNX models compatible with C++ ONNXPredictor.
This script creates models that match the expected feature interface.
"""

import os
import numpy as np
import pandas as pd
import yfinance as yf
from sklearn.linear_model import Ridge
from sklearn.preprocessing import StandardScaler
import onnx
from onnx import helper, TensorProto


def create_compatible_model(
    df: pd.DataFrame,
    output_path: str = "model_compatible.onnx"
) -> tuple:
    """
    Create a model compatible with C++ ONNXPredictor (15 features).

    Feature mapping:
    0: RSI (normalized 0-1)
    1: MACD Hist (normalized -1 to 1)
    2: Sentiment (normalized 0-1)
    3: GARCH Vol (normalized)
    4: Cycle Phase (cos)
    5-9: Lagged returns (1,2,3,5,10 days)
    10-14: Cross features
    """
    from src.features import TechnicalIndicators

    ti = TechnicalIndicators()

    # Calculate features
    close = df['Close']
    high = df['High'] if 'High' in df.columns else close
    low = df['Low'] if 'Low' in df.columns else close

    # Feature calculation
    features = pd.DataFrame(index=df.index)

    # 0: RSI (0-100 -> 0-1)
    features['rsi'] = ti.rsi(close) / 100.0

    # 1: MACD Hist
    macd, signal, hist = ti.macd(close)
    features['macd_hist'] = hist.clip(-1, 1)

    # 2: Sentiment (simulated - would come from sentiment analysis)
    features['sentiment'] = 0.5  # Placeholder

    # 3: GARCH Vol (simplified - using rolling std)
    returns = close.pct_change()
    features['garch_vol'] = returns.rolling(20).std() * np.sqrt(252) * 10  # Scaled

    # 4: Cycle phase (using sine of day of year)
    day_of_year = df.index.dayofyear if hasattr(df.index, 'dayofyear') else np.arange(len(df)) % 252
    features['cycle_phase'] = np.cos(2 * np.pi * day_of_year / 252)

    # 5-9: Lagged returns
    for lag in [1, 2, 3, 5, 10]:
        features[f'return_lag_{lag}'] = returns.shift(lag)

    # 10-14: Cross features
    features['rsi_x_sentiment'] = features['rsi'] * features['sentiment']
    features['rsi_x_vol'] = features['rsi'] * features['garch_vol']
    features['macd_x_vol'] = features['macd_hist'] * features['garch_vol']
    features['lag1_x_lag2'] = features['return_lag_1'] * features['return_lag_2']
    features['sent_x_lag1'] = features['sentiment'] * features['return_lag_1']

    # Target: next day return
    target = returns.shift(-1)

    # Clean data
    data = features.dropna()
    target_clean = target.loc[data.index]

    X = data.values
    y = target_clean.values

    # Train model
    scaler = StandardScaler()
    X_scaled = scaler.fit_transform(X)

    model = Ridge(alpha=1.0)
    model.fit(X_scaled, y)

    # Export to ONNX
    from skl2onnx import convert_sklearn
    from skl2onnx.common.data_types import FloatTensorType

    initial_type = [('float_input', FloatTensorType([None, 15]))]

    onnx_model = convert_sklearn(
        model,
        initial_types=initial_type,
        target_opset=12
    )

    onnx.save(onnx_model, output_path)

    # Save metadata
    metadata = {
        'feature_names': list(features.columns),
        'feature_count': 15,
        'scaler_mean': scaler.mean_.tolist(),
        'scaler_scale': scaler.scale_.tolist(),
        'compatible_with': 'C++ ONNXPredictor'
    }

    import json
    with open(output_path.replace('.onnx', '_metadata.json'), 'w') as f:
        json.dump(metadata, f, indent=2)

    print(f"Model saved to: {output_path}")
    print(f"Feature names: {list(features.columns)}")

    return model, scaler, metadata


def validate_compatibility(onnx_path: str, n_features: int = 15):
    """Validate ONNX model input/output shape."""
    import onnxruntime as ort

    model = onnx.load(onnx_path)
    sess = ort.InferenceSession(onnx_path)

    input_name = sess.get_inputs()[0].name
    output_name = sess.get_outputs()[0].name

    print(f"Input: {input_name}, shape: {sess.get_inputs()[0].shape}")
    print(f"Output: {output_name}, shape: {sess.get_outputs()[0].shape}")

    # Test inference
    test_input = np.random.randn(1, n_features).astype(np.float32)
    result = sess.run(None, {input_name: test_input})
    print(f"Test output shape: {result[0].shape}")

    return True


if __name__ == "__main__":
    print("Fetching data...")
    data = yf.download("AAPL", start="2020-01-01", end="2024-01-01")

    print("\nCreating compatible model...")
    model, scaler, metadata = create_compatible_model(data, "./models/aapl_compatible.onnx")

    print("\nValidating...")
    validate_compatibility("./models/aapl_compatible.onnx")

    print("\nC++ Integration:")
    print("1. Copy 'models/aapl_compatible.onnx' to your C++ project")
    print("2. Update ONNXPredictor to load the new model")
    print("3. Ensure feature extraction matches Python order")
