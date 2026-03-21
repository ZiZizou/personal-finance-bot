"""
Qlib data setup and integration script.
Qlib provides institutional-grade data management and factor library.
"""

import os
import sys
import argparse

# Check for Qlib
try:
    import qlib
    from qlib.data import D
    from qlib.data.ops import Operators
    from qlib.config import C
    print(f"Qlib version: {qlib.__version__}")
except ImportError:
    print("Qlib not installed. Run: uv pip install qlib")
    sys.exit(1)

import pandas as pd
import numpy as np


def setup_qlib_data(data_dir: str = "./data/qlib"):
    """
    Initialize Qlib with local data directory.
    """
    print(f"Setting up Qlib data at: {data_dir}")

    # Initialize Qlib
    qlib.init(
        provider_uri=data_dir,
        region=C.REGION_US
    )

    return qlib


def download_csv_data(
    csv_dir: str = "./data/raw",
    qlib_dir: str = "./data/qlib"
):
    """
    Download data from Yahoo and convert to Qlib format.
    """
    import yfinance as yf

    os.makedirs(csv_dir, exist_ok=True)
    os.makedirs(qlib_dir, exist_ok=True)

    # Download sample data
    tickers = ["AAPL", "MSFT", "GOOGL", "AMZN", "TSLA"]

    for ticker in tickers:
        print(f"Downloading {ticker}...")
        data = yf.download(ticker, start="2015-01-01", end="2024-01-01")

        if not data.empty:
            # Save raw
            data.to_csv(f"{csv_dir}/{ticker}.csv")

    print(f"Data saved to {csv_dir}")
    print("\nTo use with Qlib, you need to format the data properly.")
    print("See: https://qlib.readthedocs.io/en/latest/advanced/data.html")


def example_qlib_pipeline():
    """
    Example: Using Qlib for data loading and factor computation.
    """
    # Initialize (requires pre-downloaded data)
    # qlib.init()

    # List available fields
    # fields = D.features()
    # print("Available fields:", fields[:10])

    # Load data for a symbol
    # symbol_data = D.features(["AAPL"], ["$open", "$close", "$volume"],
    #                          start_date="2020-01-01",
    #                          end_date="2024-01-01")
    # print(symbol_data.head())

    # Example: Computing alpha factors
    # from qlib.data.tournail import operators as ops

    # feature = ops.Rank(ops.EWMA(ops.Return("$close", 20), 10))

    pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Qlib setup utilities")
    parser.add_argument("--action", type=str, default="check",
                        choices=["check", "download", "example"])
    parser.add_argument("--data-dir", type=str, default="./data/qlib")

    args = parser.parse_args()

    if args.action == "check":
        print("Checking Qlib installation...")
        print(f"Qlib version: {qlib.__version__}")
        print("\nQlib components available:")
        print("  - Data: Market data management")
        print("  - Model: ML model training")
        print("  - Strategy: Trading strategies")
        print("  - Backtest: Strategy evaluation")

    elif args.action == "download":
        download_csv_data(qlib_dir=args.data_dir)

    elif args.action == "example":
        example_qlib_pipeline()
