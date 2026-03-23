#!/usr/bin/env python
"""Portfolio CRUD API endpoints for managing local portfolio state"""

import os
import sys
import json
from datetime import datetime
from typing import List, Optional, Dict, Any
from pathlib import Path
from fastapi import APIRouter, HTTPException, Query
from pydantic import BaseModel

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))

logger = None

def get_logger():
    global logger
    if logger is None:
        import logging
        logging.basicConfig(level=logging.INFO)
        logger = logging.getLogger(__name__)
    return logger


router = APIRouter()

# Portfolio data directory
DATA_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), "data")
PORTFOLIO_FILE = os.path.join(DATA_DIR, "portfolio.json")

# Ensure data directory exists
os.makedirs(DATA_DIR, exist_ok=True)


# Pydantic models
class Position(BaseModel):
    ticker: str
    shares: float
    avgCost: float
    currency: str  # CAD or USD


class PositionCreate(BaseModel):
    ticker: str
    shares: float
    price: float
    currency: str = "USD"


class PortfolioResponse(BaseModel):
    positions: List[Position]
    cash_usd: float = 0.0
    cash_cad: float = 0.0
    total_value_cad: float = 0.0
    total_value_usd: float = 0.0


class ExecuteTradeRequest(BaseModel):
    ticker: str
    action: str  # "buy" or "sell"
    shares: float
    price: Optional[float] = None  # Price per share (required for accurate cash tracking)


class ExecuteTradeResponse(BaseModel):
    success: bool
    message: str
    position: Optional[Position] = None
    cash_usd: Optional[float] = None
    cash_cad: Optional[float] = None


class CashUpdate(BaseModel):
    currency: str  # "USD" or "CAD"
    amount: float   # New cash balance for this currency


class CashUpdateResponse(BaseModel):
    success: bool
    cash_usd: float
    cash_cad: float
    message: str


def _load_portfolio() -> Dict[str, Any]:
    """Load portfolio from JSON file."""
    if not os.path.exists(PORTFOLIO_FILE):
        return {"positions": [], "cash_usd": 0.0, "cash_cad": 0.0}

    try:
        with open(PORTFOLIO_FILE, 'r') as f:
            return json.load(f)
    except Exception as e:
        get_logger().error(f"Error loading portfolio: {e}")
        return {"positions": [], "cash_usd": 0.0, "cash_cad": 0.0}


def _save_portfolio(portfolio: Dict[str, Any]) -> bool:
    """Save portfolio to JSON file."""
    try:
        with open(PORTFOLIO_FILE, 'w') as f:
            json.dump(portfolio, f, indent=2)
        return True
    except Exception as e:
        get_logger().error(f"Error saving portfolio: {e}")
        return False


def _get_currency_for_ticker(ticker: str) -> str:
    """Determine currency for a ticker based on tickers.csv."""
    tickers_file = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), "..", "Trading_cpp", "tickers.csv")

    if os.path.exists(tickers_file):
        try:
            with open(tickers_file, 'r') as f:
                reader = csv.DictReader(f)
                for row in reader:
                    if row.get('symbol', '').upper() == ticker.upper():
                        return row.get('currency', 'USD')
        except Exception:
            pass

    # Default: USD unless .TO suffix (Canadian stock)
    if ticker.upper().endswith('.TO'):
        return 'CAD'
    return 'USD'


import csv  # Import here for _get_currency_for_ticker


@router.get("/", response_model=PortfolioResponse)
async def get_portfolio():
    """
    Get current portfolio holdings.

    Returns all positions with shares, average cost, currency, and cash balances.
    """
    portfolio = _load_portfolio()

    positions = [
        Position(
            ticker=pos['ticker'],
            shares=pos['shares'],
            avgCost=pos['avgCost'],
            currency=pos['currency']
        )
        for pos in portfolio.get("positions", [])
    ]

    return PortfolioResponse(
        positions=positions,
        cash_usd=portfolio.get("cash_usd", 0.0),
        cash_cad=portfolio.get("cash_cad", 0.0),
        total_value_cad=0.0,  # Would need live prices to calculate
        total_value_usd=0.0
    )


@router.get("/position/{ticker}", response_model=Position)
async def get_position(ticker: str):
    """
    Get a specific position by ticker.

    Args:
        ticker: Stock ticker symbol

    Returns:
        Position details or 404 if not found
    """
    portfolio = _load_portfolio()
    ticker = ticker.upper()

    for pos in portfolio.get("positions", []):
        if pos['ticker'].upper() == ticker:
            return Position(
                ticker=pos['ticker'],
                shares=pos['shares'],
                avgCost=pos['avgCost'],
                currency=pos['currency']
            )

    raise HTTPException(status_code=404, detail=f"Position for {ticker} not found")


@router.post("/position", response_model=Position)
async def add_or_update_position(position: PositionCreate):
    """
    Add a new position or update existing one.

    If ticker exists, updates shares and recalculates average cost.
    If new ticker, creates a new position.

    Args:
        position: Position data (ticker, shares, price, currency)

    Returns:
        Updated position
    """
    portfolio = _load_portfolio()
    ticker = position.ticker.upper()
    positions = portfolio.get("positions", [])

    # Find existing position
    existing_idx = None
    for i, pos in enumerate(positions):
        if pos['ticker'].upper() == ticker:
            existing_idx = i
            break

    if existing_idx is not None:
        # Update existing position
        existing = positions[existing_idx]
        old_shares = existing['shares']
        old_cost = existing['avgCost']
        new_shares = position.shares

        if new_shares > 0:
            # Calculate new average cost
            total_cost = (old_shares * old_cost) + (new_shares * position.price)
            new_avg_cost = total_cost / (old_shares + new_shares)
            positions[existing_idx]['shares'] = old_shares + new_shares
            positions[existing_idx]['avgCost'] = new_avg_cost
        else:
            # Setting shares to 0 or negative - remove position
            positions.pop(existing_idx)
    else:
        # Create new position
        currency = position.currency if position.currency else _get_currency_for_ticker(ticker)
        positions.append({
            'ticker': ticker,
            'shares': position.shares,
            'avgCost': position.price,
            'currency': currency
        })

    portfolio["positions"] = positions
    _save_portfolio(portfolio)

    # Return updated position
    for pos in positions:
        if pos['ticker'].upper() == ticker:
            return Position(
                ticker=pos['ticker'],
                shares=pos['shares'],
                avgCost=pos['avgCost'],
                currency=pos['currency']
            )

    # If we get here, position was removed
    raise HTTPException(status_code=200, detail=f"Position {ticker} removed (shares = 0)")


@router.delete("/position/{ticker}")
async def remove_position(ticker: str):
    """
    Remove a position (set shares to 0).

    Args:
        ticker: Stock ticker symbol

    Returns:
        Success message
    """
    portfolio = _load_portfolio()
    ticker = ticker.upper()
    positions = portfolio.get("positions", [])

    # Find and remove position
    original_len = len(positions)
    positions = [p for p in positions if p['ticker'].upper() != ticker]

    if len(positions) == original_len:
        raise HTTPException(status_code=404, detail=f"Position for {ticker} not found")

    portfolio["positions"] = positions
    _save_portfolio(portfolio)

    return {"message": f"Position {ticker} removed", "ticker": ticker}


@router.post("/execute/{trade_id}", response_model=ExecuteTradeResponse)
async def execute_trade(trade_id: str, trade: ExecuteTradeRequest):
    """
    Execute an accepted trade from Telegram signal.

    This is called when user accepts a trade signal via inline keyboard.
    Updates portfolio.json with the executed trade.
    Cash is deducted on buy and added on sell.

    Args:
        trade_id: Unique trade identifier
        trade: Trade details (ticker, action, shares, price)

    Returns:
        Execution result
    """
    portfolio = _load_portfolio()
    ticker = trade.ticker.upper()
    positions = portfolio.get("positions", [])

    # Find existing position
    existing_idx = None
    for i, pos in enumerate(positions):
        if pos['ticker'].upper() == ticker:
            existing_idx = i
            break

    action = trade.action.lower()
    if action not in ['buy', 'sell']:
        return ExecuteTradeResponse(
            success=False,
            message=f"Invalid action: {action}. Must be 'buy' or 'sell'."
        )

    try:
        if action == 'buy':
            # Get currency for this ticker
            currency = _get_currency_for_ticker(ticker)

            # Check if we have enough cash
            cost = trade.shares * (trade.price or 0)
            if currency == "CAD":
                if portfolio.get("cash_cad", 0) < cost:
                    return ExecuteTradeResponse(
                        success=False,
                        message=f"Insufficient CAD cash. Need ${cost:.2f}, have ${portfolio.get('cash_cad', 0):.2f}"
                    )
                portfolio["cash_cad"] -= cost
            else:
                if portfolio.get("cash_usd", 0) < cost:
                    return ExecuteTradeResponse(
                        success=False,
                        message=f"Insufficient USD cash. Need ${cost:.2f}, have ${portfolio.get('cash_usd', 0):.2f}"
                    )
                portfolio["cash_usd"] -= cost

            if existing_idx is not None:
                # Update existing position
                existing = positions[existing_idx]
                old_shares = existing['shares']
                old_cost = existing['avgCost']
                new_shares = trade.shares

                if new_shares > 0:
                    total_cost = (old_shares * old_cost) + (new_shares * (trade.price or old_cost))
                    new_avg_cost = total_cost / (old_shares + new_shares)
                    positions[existing_idx]['shares'] = old_shares + new_shares
                    positions[existing_idx]['avgCost'] = new_avg_cost
            else:
                # Create new position
                positions.append({
                    'ticker': ticker,
                    'shares': trade.shares,
                    'avgCost': trade.price or 0,
                    'currency': currency
                })

            portfolio["positions"] = positions
            _save_portfolio(portfolio)

            return ExecuteTradeResponse(
                success=True,
                message=f"BUY executed: {trade.shares} shares of {ticker} at ${trade.price or 0:.2f}",
                position=Position(
                    ticker=ticker,
                    shares=trade.shares,
                    avgCost=trade.price or 0,
                    currency=currency
                ),
                cash_usd=portfolio.get("cash_usd", 0.0),
                cash_cad=portfolio.get("cash_cad", 0.0)
            )

        elif action == 'sell':
            if existing_idx is None:
                return ExecuteTradeResponse(
                    success=False,
                    message=f"Cannot sell {ticker}: no position found"
                )

            existing = positions[existing_idx]
            current_shares = existing['shares']
            currency = existing['currency']

            if trade.shares > current_shares:
                return ExecuteTradeResponse(
                    success=False,
                    message=f"Cannot sell {trade.shares} shares of {ticker}: only {current_shares} shares owned"
                )

            # Calculate proceeds
            proceeds = trade.shares * (trade.price or existing['avgCost'])

            # Add proceeds to cash
            if currency == "CAD":
                portfolio["cash_cad"] = portfolio.get("cash_cad", 0) + proceeds
            else:
                portfolio["cash_usd"] = portfolio.get("cash_usd", 0) + proceeds

            if trade.shares >= current_shares:
                # Sell all - remove position
                positions.pop(existing_idx)
                portfolio["positions"] = positions
                _save_portfolio(portfolio)
                return ExecuteTradeResponse(
                    success=True,
                    message=f"SELL executed: {current_shares} shares of {ticker} at ${trade.price or existing['avgCost']:.2f} (position closed, ${proceeds:.2f} added to {currency} cash)",
                    cash_usd=portfolio.get("cash_usd", 0.0),
                    cash_cad=portfolio.get("cash_cad", 0.0)
                )
            else:
                # Partial sell
                positions[existing_idx]['shares'] = current_shares - trade.shares
                portfolio["positions"] = positions
                _save_portfolio(portfolio)
                return ExecuteTradeResponse(
                    success=True,
                    message=f"SELL executed: {trade.shares} shares of {ticker} at ${trade.price or existing['avgCost']:.2f} ({current_shares - trade.shares} remaining, ${proceeds:.2f} added to {currency} cash)",
                    position=Position(
                        ticker=ticker,
                        shares=current_shares - trade.shares,
                        avgCost=existing['avgCost'],
                        currency=currency
                    ),
                    cash_usd=portfolio.get("cash_usd", 0.0),
                    cash_cad=portfolio.get("cash_cad", 0.0)
                )

    except Exception as e:
        get_logger().error(f"Error executing trade: {e}")
        return ExecuteTradeResponse(
            success=False,
            message=f"Trade execution failed: {str(e)}"
        )


@router.get("/summary")
async def get_portfolio_summary():
    """
    Get portfolio summary with basic metrics.

    Returns:
        Summary of positions, cash, and total values
    """
    portfolio = _load_portfolio()
    positions = portfolio.get("positions", [])

    total_cad_value = portfolio.get("cash_cad", 0.0)
    total_usd_value = portfolio.get("cash_usd", 0.0)
    total_positions = len([p for p in positions if p['shares'] > 0])

    for pos in positions:
        if pos['shares'] > 0:
            if pos['currency'] == 'CAD':
                total_cad_value += pos['shares'] * pos['avgCost']
            else:
                total_usd_value += pos['shares'] * pos['avgCost']

    return {
        "total_positions": total_positions,
        "cash_cad": portfolio.get("cash_cad", 0.0),
        "cash_usd": portfolio.get("cash_usd", 0.0),
        "total_cad_value": total_cad_value,
        "total_usd_value": total_usd_value,
        "timestamp": datetime.now().isoformat()
    }


@router.post("/cash", response_model=CashUpdateResponse)
async def update_cash(cash_update: CashUpdate):
    """
    Set or update cash balance for a currency.

    Use this to record how much cash you have available for trading.
    For example, after selling positions, update your cash balance here.

    Args:
        cash_update: Contains currency (USD/CAD) and amount

    Returns:
        Updated cash balances
    """
    portfolio = _load_portfolio()

    currency = cash_update.currency.upper()
    if currency not in ['USD', 'CAD']:
        raise HTTPException(status_code=400, detail="Currency must be USD or CAD")

    if cash_update.amount < 0:
        raise HTTPException(status_code=400, detail="Cash amount cannot be negative")

    if currency == 'CAD':
        portfolio["cash_cad"] = cash_update.amount
    else:
        portfolio["cash_usd"] = cash_update.amount

    _save_portfolio(portfolio)

    return CashUpdateResponse(
        success=True,
        cash_usd=portfolio.get("cash_usd", 0.0),
        cash_cad=portfolio.get("cash_cad", 0.0),
        message=f"Set {currency} cash to ${cash_update.amount:.2f}"
    )


@router.get("/cash")
async def get_cash():
    """
    Get current cash balances.

    Returns:
        Cash balances for USD and CAD
    """
    portfolio = _load_portfolio()

    return {
        "cash_usd": portfolio.get("cash_usd", 0.0),
        "cash_cad": portfolio.get("cash_cad", 0.0),
        "timestamp": datetime.now().isoformat()
    }
